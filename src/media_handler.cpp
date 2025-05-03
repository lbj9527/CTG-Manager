#include <chrono>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "../include/media_handler.h"
#include "../include/client_manager.h"

namespace tg_forwarder {

// MediaTask 实现
MediaTask::MediaTask(MediaTaskType type, const Message& message)
    : type_(type),
      state_(MediaTaskState::Pending),
      message_(message),
      progress_(0) {
    
    // 使用消息ID和聊天ID生成唯一任务ID
    id_ = generate_message_id(message->chat_id_, message->id_);
    
    // 初始化时间
    start_time_ = std::chrono::system_clock::now();
    end_time_ = start_time_;
}

std::string MediaTask::id() const {
    return id_;
}

MediaTaskType MediaTask::type() const {
    return type_;
}

MediaTaskState MediaTask::state() const {
    return state_;
}

void MediaTask::set_state(MediaTaskState state) {
    state_ = state;
    
    // 设置结束时间（如果是完成或失败状态）
    if (state == MediaTaskState::Completed || state == MediaTaskState::Failed) {
        end_time_ = std::chrono::system_clock::now();
    }
}

const Message& MediaTask::message() const {
    return message_;
}

MemoryBuffer& MediaTask::buffer() {
    return buffer_;
}

const MemoryBuffer& MediaTask::buffer() const {
    return buffer_;
}

std::string MediaTask::error() const {
    return error_;
}

void MediaTask::set_error(const std::string& error) {
    error_ = error;
}

int MediaTask::progress() const {
    return progress_;
}

void MediaTask::set_progress(int progress) {
    progress_ = std::clamp(progress, 0, 100);
}

std::chrono::system_clock::time_point MediaTask::start_time() const {
    return start_time_;
}

std::chrono::system_clock::time_point MediaTask::end_time() const {
    return end_time_;
}

void MediaTask::set_start_time(std::chrono::system_clock::time_point time) {
    start_time_ = time;
}

void MediaTask::set_end_time(std::chrono::system_clock::time_point time) {
    end_time_ = time;
}

int64_t MediaTask::duration_ms() const {
    auto duration = end_time_ - start_time_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// MediaGroupTask 实现
MediaGroupTask::MediaGroupTask(const std::string& group_id)
    : id_(group_id) {
}

std::string MediaGroupTask::id() const {
    return id_;
}

void MediaGroupTask::add_task(std::shared_ptr<MediaTask> task) {
    tasks_.push_back(task);
}

const std::vector<std::shared_ptr<MediaTask>>& MediaGroupTask::tasks() const {
    return tasks_;
}

size_t MediaGroupTask::completed_count() const {
    return std::count_if(tasks_.begin(), tasks_.end(), [](const auto& task) {
        return task->state() == MediaTaskState::Completed;
    });
}

size_t MediaGroupTask::failed_count() const {
    return std::count_if(tasks_.begin(), tasks_.end(), [](const auto& task) {
        return task->state() == MediaTaskState::Failed;
    });
}

bool MediaGroupTask::is_completed() const {
    return completed_count() + failed_count() == tasks_.size();
}

int MediaGroupTask::overall_progress() const {
    if (tasks_.empty()) {
        return 0;
    }
    
    int total_progress = 0;
    for (const auto& task : tasks_) {
        total_progress += task->progress();
    }
    
    return total_progress / static_cast<int>(tasks_.size());
}

std::string MediaGroupTask::caption() const {
    // 寻找第一个有说明文字的消息
    for (const auto& task : tasks_) {
        std::string caption = get_caption(task->message());
        if (!caption.empty()) {
            return caption;
        }
    }
    
    return "";
}

// MediaHandler 实现
MediaHandler& MediaHandler::instance() {
    static MediaHandler instance;
    return instance;
}

MediaHandler::MediaHandler()
    : running_(false),
      max_concurrent_downloads_(2),
      max_concurrent_uploads_(2) {
}

MediaHandler::~MediaHandler() {
    if (running_) {
        stop();
    }
}

void MediaHandler::init() {
    spdlog::info("初始化媒体处理器");
}

void MediaHandler::start() {
    if (running_) {
        spdlog::warn("媒体处理器已经在运行中");
        return;
    }
    
    spdlog::info("启动媒体处理器");
    running_ = true;
    
    // 启动下载和上传线程
    for (int i = 0; i < max_concurrent_downloads_; ++i) {
        download_threads_.emplace_back(&MediaHandler::download_worker, this);
    }
    
    for (int i = 0; i < max_concurrent_uploads_; ++i) {
        upload_threads_.emplace_back(&MediaHandler::upload_worker, this);
    }
    
    spdlog::info("媒体处理器已启动，下载线程: {}，上传线程: {}", 
        max_concurrent_downloads_, max_concurrent_uploads_);
}

void MediaHandler::stop() {
    if (!running_) {
        return;
    }
    
    spdlog::info("停止媒体处理器");
    running_ = false;
    
    // 通知所有等待中的线程
    download_cv_.notify_all();
    upload_cv_.notify_all();
    
    // 等待所有线程结束
    for (auto& thread : download_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    for (auto& thread : upload_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    download_threads_.clear();
    upload_threads_.clear();
    
    spdlog::info("媒体处理器已停止");
}

std::future<std::shared_ptr<MediaTask>> MediaHandler::download_media(const Message& message) {
    std::promise<std::shared_ptr<MediaTask>> promise;
    auto future = promise.get_future();
    
    auto task = std::make_shared<MediaTask>(MediaTaskType::Download, message);
    
    {
        std::lock_guard<std::mutex> lock(download_mutex_);
        download_queue_.emplace(task, std::move(promise));
    }
    
    download_cv_.notify_one();
    
    return future;
}

std::future<std::shared_ptr<MediaGroupTask>> MediaHandler::download_media_group(const MessageVector& messages) {
    if (messages.empty()) {
        std::promise<std::shared_ptr<MediaGroupTask>> empty_promise;
        empty_promise.set_value(nullptr);
        return empty_promise.get_future();
    }
    
    // 获取媒体组ID
    auto media_group_id = get_media_group_id(messages[0]);
    if (!media_group_id) {
        spdlog::error("无法获取媒体组ID");
        std::promise<std::shared_ptr<MediaGroupTask>> empty_promise;
        empty_promise.set_value(nullptr);
        return empty_promise.get_future();
    }
    
    std::promise<std::shared_ptr<MediaGroupTask>> group_promise;
    auto group_future = group_promise.get_future();
    
    // 创建媒体组任务
    auto group_task = std::make_shared<MediaGroupTask>(*media_group_id);
    
    // 为组中的每个消息创建下载任务
    std::vector<std::future<std::shared_ptr<MediaTask>>> futures;
    for (const auto& message : messages) {
        futures.push_back(download_media(message));
    }
    
    // 启动一个独立线程来等待所有下载任务完成
    std::thread([futures = std::move(futures), group_task, group_promise = std::move(group_promise)]() mutable {
        for (auto& future : futures) {
            try {
                auto task = future.get();
                if (task) {
                    group_task->add_task(task);
                }
            } catch (const std::exception& e) {
                spdlog::error("等待下载任务时出错: {}", e.what());
            }
        }
        
        group_promise.set_value(group_task);
    }).detach();
    
    return group_future;
}

std::future<Message> MediaHandler::upload_media(Int64 chat_id, const std::shared_ptr<MediaTask>& task) {
    std::promise<Message> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(upload_mutex_);
        upload_queue_.emplace(chat_id, task, std::move(promise));
    }
    
    upload_cv_.notify_one();
    
    return future;
}

std::future<MessageVector> MediaHandler::upload_media_group(Int64 chat_id, const std::shared_ptr<MediaGroupTask>& group_task) {
    std::promise<MessageVector> promise;
    auto future = promise.get_future();
    
    // 启动一个线程来并行上传媒体组
    std::thread([this, chat_id, group_task, promise = std::move(promise)]() mutable {
        try {
            const auto& tasks = group_task->tasks();
            if (tasks.empty()) {
                promise.set_value(MessageVector());
                return;
            }
            
            // 创建输入媒体数组
            auto input_media_array = td_api::make_object<td_api::inputMessageMediaGroup>();
            std::vector<td_api::object_ptr<td_api::InputMessageContent>> media_contents;
            std::string caption = group_task->caption();
            
            for (size_t i = 0; i < tasks.size(); ++i) {
                const auto& task = tasks[i];
                const auto& buffer = task->buffer();
                
                // 根据媒体类型创建不同的输入媒体
                auto media_type = get_media_type(task->message());
                
                switch (media_type) {
                    case MediaType::Photo: {
                        auto input_photo = td_api::make_object<td_api::inputMessagePhoto>();
                        input_photo->photo_ = td_api::make_object<td_api::inputFileMemory>(
                            buffer.data(), buffer.name());
                        
                        // 仅第一个媒体设置说明文字
                        if (i == 0 && !caption.empty()) {
                            input_photo->caption_ = td_api::make_object<td_api::formattedText>(
                                caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
                        }
                        
                        media_contents.push_back(std::move(input_photo));
                        break;
                    }
                    case MediaType::Video: {
                        auto input_video = td_api::make_object<td_api::inputMessageVideo>();
                        input_video->video_ = td_api::make_object<td_api::inputFileMemory>(
                            buffer.data(), buffer.name());
                        
                        // 仅第一个媒体设置说明文字
                        if (i == 0 && !caption.empty()) {
                            input_video->caption_ = td_api::make_object<td_api::formattedText>(
                                caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
                        }
                        
                        media_contents.push_back(std::move(input_video));
                        break;
                    }
                    case MediaType::Document: {
                        auto input_document = td_api::make_object<td_api::inputMessageDocument>();
                        input_document->document_ = td_api::make_object<td_api::inputFileMemory>(
                            buffer.data(), buffer.name());
                        
                        // 仅第一个媒体设置说明文字
                        if (i == 0 && !caption.empty()) {
                            input_document->caption_ = td_api::make_object<td_api::formattedText>(
                                caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
                        }
                        
                        media_contents.push_back(std::move(input_document));
                        break;
                    }
                    default:
                        spdlog::warn("不支持的媒体类型: {}", static_cast<int>(media_type));
                        break;
                }
            }
            
            // 设置媒体组内容
            input_media_array->media_ = std::move(media_contents);
            
            // 发送媒体组
            auto send_message = td_api::make_object<td_api::sendMessageAlbum>();
            send_message->chat_id_ = chat_id;
            send_message->input_message_contents_ = std::move(input_media_array->media_);
            
            // 发送请求
            auto response = ClientManager::instance().send_query(std::move(send_message));
            
            // 处理响应
            if (response->get_id() == td_api::error::ID) {
                auto error = td::move_object_as<td_api::error>(response);
                throw std::runtime_error("发送媒体组失败: " + error->message_);
            }
            
            // 将响应转换为消息数组
            auto messages = td::move_object_as<td_api::messages>(response);
            MessageVector result;
            
            for (auto& message : messages->messages_) {
                result.push_back(std::move(message));
            }
            
            promise.set_value(std::move(result));
        } catch (const std::exception& e) {
            try {
                promise.set_exception(std::current_exception());
            } catch (...) {
                // Promise已设置，忽略
            }
        }
    }).detach();
    
    return future;
}

void MediaHandler::set_max_concurrent_downloads(int max) {
    if (max <= 0) {
        spdlog::warn("最大并发下载数必须大于0，设置为1");
        max = 1;
    }
    
    max_concurrent_downloads_ = max;
}

void MediaHandler::set_max_concurrent_uploads(int max) {
    if (max <= 0) {
        spdlog::warn("最大并发上传数必须大于0，设置为1");
        max = 1;
    }
    
    max_concurrent_uploads_ = max;
}

int MediaHandler::active_download_count() const {
    return active_downloads_;
}

int MediaHandler::active_upload_count() const {
    return active_uploads_;
}

void MediaHandler::download_worker() {
    spdlog::debug("下载线程已启动");
    
    while (running_) {
        std::shared_ptr<MediaTask> task;
        std::promise<std::shared_ptr<MediaTask>> promise;
        
        // 从队列中获取任务
        {
            std::unique_lock<std::mutex> lock(download_mutex_);
            download_cv_.wait(lock, [this] {
                return !running_ || !download_queue_.empty();
            });
            
            if (!running_) {
                break;
            }
            
            if (!download_queue_.empty()) {
                task = download_queue_.front().first;
                promise = std::move(download_queue_.front().second);
                download_queue_.pop();
            }
        }
        
        // 处理任务
        if (task && promise.get_future().valid()) {
            try {
                ++active_downloads_;
                task->set_state(MediaTaskState::Processing);
                
                // 下载文件
                download_file(task);
                
                task->set_state(MediaTaskState::Completed);
                promise.set_value(task);
            } catch (const std::exception& e) {
                spdlog::error("下载文件失败: {}", e.what());
                
                task->set_state(MediaTaskState::Failed);
                task->set_error(e.what());
                
                try {
                    promise.set_value(task);
                } catch (...) {
                    // 忽略已设置的promise
                }
            }
            
            --active_downloads_;
        }
    }
    
    spdlog::debug("下载线程已退出");
}

void MediaHandler::upload_worker() {
    spdlog::debug("上传线程已启动");
    
    while (running_) {
        Int64 chat_id = 0;
        std::shared_ptr<MediaTask> task;
        std::promise<Message> promise;
        
        // 从队列中获取任务
        {
            std::unique_lock<std::mutex> lock(upload_mutex_);
            upload_cv_.wait(lock, [this] {
                return !running_ || !upload_queue_.empty();
            });
            
            if (!running_) {
                break;
            }
            
            if (!upload_queue_.empty()) {
                std::tie(chat_id, task, promise) = std::move(upload_queue_.front());
                upload_queue_.pop();
            }
        }
        
        // 处理任务
        if (task && promise.get_future().valid()) {
            try {
                ++active_uploads_;
                task->set_state(MediaTaskState::Processing);
                
                // 上传文件
                auto message = upload_file(chat_id, task);
                
                task->set_state(MediaTaskState::Completed);
                promise.set_value(std::move(message));
            } catch (const std::exception& e) {
                spdlog::error("上传文件失败: {}", e.what());
                
                task->set_state(MediaTaskState::Failed);
                task->set_error(e.what());
                
                try {
                    promise.set_exception(std::current_exception());
                } catch (...) {
                    // 忽略已设置的promise
                }
            }
            
            --active_uploads_;
        }
    }
    
    spdlog::debug("上传线程已退出");
}

void MediaHandler::download_file(std::shared_ptr<MediaTask> task) {
    auto& message = task->message();
    
    // 获取文件ID
    auto file_ids = get_file_ids(message);
    if (file_ids.empty()) {
        throw MediaError("消息不包含媒体文件");
    }
    
    // 通常使用第一个（最大的）文件
    Int32 file_id = file_ids[0];
    
    // 获取文件信息
    auto get_file = td_api::make_object<td_api::getFile>();
    get_file->file_id_ = file_id;
    
    auto response = ClientManager::instance().send_query(std::move(get_file));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        throw MediaError("获取文件信息失败: " + error->message_);
    }
    
    auto file = td::move_object_as<td_api::file>(response);
    
    // 下载文件
    auto download_file = td_api::make_object<td_api::downloadFile>();
    download_file->file_id_ = file->id_;
    download_file->priority_ = 1;
    download_file->offset_ = 0;
    download_file->limit_ = 0; // 0表示下载整个文件
    download_file->synchronous_ = true;
    
    response = ClientManager::instance().send_query(std::move(download_file));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        throw MediaError("下载文件失败: " + error->message_);
    }
    
    file = td::move_object_as<td_api::file>(response);
    
    // 如果文件已下载，读取文件内容
    if (file->local_->is_downloading_completed_) {
        std::ifstream file_stream(file->local_->path_, std::ios::binary);
        if (!file_stream.is_open()) {
            throw MediaError("无法打开下载的文件: " + file->local_->path_);
        }
        
        // 读取文件内容到缓冲区
        std::string content((std::istreambuf_iterator<char>(file_stream)), 
                            std::istreambuf_iterator<char>());
        
        task->buffer().append(content);
        
        // 设置文件名
        auto media_type = get_media_type(message);
        std::string file_name = "media_" + std::to_string(message->id_);
        file_name += get_file_extension(media_type, message);
        
        task->buffer().set_name(file_name);
        
        spdlog::info("文件下载完成: {} ({} 字节)", file_name, content.size());
    } else {
        throw MediaError("文件下载未完成");
    }
}

Message MediaHandler::upload_file(Int64 chat_id, const std::shared_ptr<MediaTask>& task) {
    return send_media_by_type(chat_id, task);
}

Message MediaHandler::send_media_by_type(Int64 chat_id, const std::shared_ptr<MediaTask>& task) {
    const auto& message = task->message();
    const auto& buffer = task->buffer();
    
    // 根据媒体类型发送不同类型的消息
    auto media_type = get_media_type(message);
    std::string caption = get_caption(message);
    
    td_api::object_ptr<td_api::InputMessageContent> content;
    
    switch (media_type) {
        case MediaType::Photo: {
            auto input_photo = td_api::make_object<td_api::inputMessagePhoto>();
            input_photo->photo_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            if (!caption.empty()) {
                input_photo->caption_ = td_api::make_object<td_api::formattedText>(
                    caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
            }
            
            content = std::move(input_photo);
            break;
        }
        case MediaType::Video: {
            auto input_video = td_api::make_object<td_api::inputMessageVideo>();
            input_video->video_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            if (!caption.empty()) {
                input_video->caption_ = td_api::make_object<td_api::formattedText>(
                    caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
            }
            
            content = std::move(input_video);
            break;
        }
        case MediaType::Document: {
            auto input_document = td_api::make_object<td_api::inputMessageDocument>();
            input_document->document_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            if (!caption.empty()) {
                input_document->caption_ = td_api::make_object<td_api::formattedText>(
                    caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
            }
            
            content = std::move(input_document);
            break;
        }
        case MediaType::Audio: {
            auto input_audio = td_api::make_object<td_api::inputMessageAudio>();
            input_audio->audio_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            if (!caption.empty()) {
                input_audio->caption_ = td_api::make_object<td_api::formattedText>(
                    caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
            }
            
            content = std::move(input_audio);
            break;
        }
        case MediaType::Animation: {
            auto input_animation = td_api::make_object<td_api::inputMessageAnimation>();
            input_animation->animation_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            if (!caption.empty()) {
                input_animation->caption_ = td_api::make_object<td_api::formattedText>(
                    caption, std::vector<td_api::object_ptr<td_api::textEntity>>());
            }
            
            content = std::move(input_animation);
            break;
        }
        case MediaType::Sticker: {
            auto input_sticker = td_api::make_object<td_api::inputMessageSticker>();
            input_sticker->sticker_ = td_api::make_object<td_api::inputFileMemory>(
                buffer.data(), buffer.name());
            
            content = std::move(input_sticker);
            break;
        }
        default:
            throw MediaError("不支持的媒体类型");
    }
    
    // 发送消息
    auto send_message = td_api::make_object<td_api::sendMessage>();
    send_message->chat_id_ = chat_id;
    send_message->input_message_content_ = std::move(content);
    
    auto response = ClientManager::instance().send_query(std::move(send_message));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        throw MediaError("发送媒体消息失败: " + error->message_);
    }
    
    return td::move_object_as<td_api::message>(response);
}

} // namespace tg_forwarder 