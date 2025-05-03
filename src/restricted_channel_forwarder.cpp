#include <regex>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include "../include/restricted_channel_forwarder.h"
#include "../include/channel_resolver.h"
#include "../include/client_manager.h"
#include "../include/media_handler.h"
#include "../include/utils.h"

namespace tg_forwarder {

// 单例实例
RestrictedChannelForwarder& RestrictedChannelForwarder::instance() {
    static RestrictedChannelForwarder instance;
    return instance;
}

RestrictedChannelForwarder::RestrictedChannelForwarder()
    : running_(false),
      stopping_(false),
      wait_time_ms_(1000),
      source_chat_id_(0),
      target_chat_id_(0),
      last_message_id_(0),
      forwarded_count_(0),
      failed_count_(0) {
}

RestrictedChannelForwarder::~RestrictedChannelForwarder() {
    if (running_) {
        stop();
    }
}

void RestrictedChannelForwarder::init(const ForwarderConfig& config) {
    config_ = config;
    
    // 设置工作模式
    switch (config.mode) {
        case ForwarderMode::Continuous:
            spdlog::info("转发器工作模式: 连续模式");
            break;
        case ForwarderMode::OneTime:
            spdlog::info("转发器工作模式: 一次性模式");
            break;
        default:
            spdlog::warn("未知的转发器工作模式，默认使用连续模式");
            config_.mode = ForwarderMode::Continuous;
    }
    
    // 设置媒体处理器参数
    MediaHandler::instance().set_max_concurrent_downloads(config.max_concurrent_downloads);
    MediaHandler::instance().set_max_concurrent_uploads(config.max_concurrent_uploads);
    
    spdlog::info("最大并发下载数: {}", config.max_concurrent_downloads);
    spdlog::info("最大并发上传数: {}", config.max_concurrent_uploads);
    spdlog::info("历史消息数量限制: {}", config.max_history_messages);
    
    // 设置等待时间
    wait_time_ms_ = config.wait_time_ms;
    spdlog::info("轮询等待时间: {} ms", wait_time_ms_);
    
    // 初始化统计信息
    forwarded_count_ = 0;
    failed_count_ = 0;
    
    // 设置消息类型过滤器
    message_filters_.clear();
    for (const auto& filter : config.message_filters) {
        MessageTypeFilter filter_type = MessageTypeFilter::Unknown;
        
        if (filter == "text") {
            filter_type = MessageTypeFilter::Text;
        } else if (filter == "photo") {
            filter_type = MessageTypeFilter::Photo;
        } else if (filter == "video") {
            filter_type = MessageTypeFilter::Video;
        } else if (filter == "document") {
            filter_type = MessageTypeFilter::Document;
        } else if (filter == "audio") {
            filter_type = MessageTypeFilter::Audio;
        } else if (filter == "sticker") {
            filter_type = MessageTypeFilter::Sticker;
        } else if (filter == "animation") {
            filter_type = MessageTypeFilter::Animation;
        } else if (filter == "all") {
            filter_type = MessageTypeFilter::All;
        } else {
            spdlog::warn("未知的消息类型过滤器: {}", filter);
            continue;
        }
        
        message_filters_.push_back(filter_type);
        spdlog::info("添加消息类型过滤器: {}", filter);
    }
    
    // 如果没有设置过滤器，默认添加"全部"过滤器
    if (message_filters_.empty()) {
        message_filters_.push_back(MessageTypeFilter::All);
        spdlog::info("未设置消息类型过滤器，默认处理所有类型");
    }
}

bool RestrictedChannelForwarder::start(const std::string& source_channel, const std::string& target_channel) {
    if (running_) {
        spdlog::warn("转发器已经在运行中");
        return false;
    }
    
    spdlog::info("启动转发器...");
    spdlog::info("源频道: {}", source_channel);
    spdlog::info("目标频道: {}", target_channel);
    
    // 解析源频道和目标频道
    try {
        auto source_future = ChannelResolver::instance().resolve_channel(source_channel);
        auto target_future = ChannelResolver::instance().resolve_channel(target_channel);
        
        // 等待解析完成
        source_chat_id_ = source_future.get();
        target_chat_id_ = target_future.get();
        
        if (source_chat_id_ == 0) {
            spdlog::error("无法解析源频道: {}", source_channel);
            return false;
        }
        
        if (target_chat_id_ == 0) {
            spdlog::error("无法解析目标频道: {}", target_channel);
            return false;
        }
        
        spdlog::info("源频道ID: {}", source_chat_id_);
        spdlog::info("目标频道ID: {}", target_chat_id_);
    } catch (const std::exception& e) {
        spdlog::error("解析频道时出错: {}", e.what());
        return false;
    }
    
    // 检查机器人在目标频道中是否有发消息权限
    if (!check_send_message_permission(target_chat_id_)) {
        spdlog::error("在目标频道中没有发送消息的权限: {}", target_channel);
        return false;
    }
    
    // 获取当前最新消息ID作为起始点
    last_message_id_ = get_latest_message_id(source_chat_id_);
    if (last_message_id_ == 0) {
        spdlog::warn("无法获取源频道的最新消息ID，将从下一条消息开始转发");
    } else {
        spdlog::info("获取到源频道的最新消息ID: {}", last_message_id_);
    }
    
    // 启动转发线程
    running_ = true;
    stopping_ = false;
    forward_thread_ = std::thread(&RestrictedChannelForwarder::forward_worker, this);
    
    spdlog::info("转发器已启动");
    return true;
}

void RestrictedChannelForwarder::stop() {
    if (!running_) {
        return;
    }
    
    spdlog::info("停止转发器...");
    stopping_ = true;
    
    // 等待转发线程结束
    if (forward_thread_.joinable()) {
        forward_thread_.join();
    }
    
    running_ = false;
    stopping_ = false;
    
    spdlog::info("转发器已停止，总计转发 {} 条消息，失败 {} 条", 
        forwarded_count_, failed_count_);
}

bool RestrictedChannelForwarder::is_running() const {
    return running_;
}

int RestrictedChannelForwarder::get_forwarded_count() const {
    return forwarded_count_;
}

int RestrictedChannelForwarder::get_failed_count() const {
    return failed_count_;
}

void RestrictedChannelForwarder::forward_worker() {
    spdlog::debug("转发线程已启动");
    
    // 初始化媒体处理器
    MediaHandler::instance().start();
    
    // 主转发循环
    while (running_ && !stopping_) {
        try {
            // 获取新消息
            auto messages = get_new_messages(source_chat_id_, last_message_id_, config_.max_history_messages);
            
            if (!messages.empty()) {
                spdlog::info("获取到 {} 条新消息", messages.size());
                
                // 处理新消息
                for (auto& message : messages) {
                    try {
                        // 检查消息类型是否在过滤器中
                        if (!should_forward_message(message)) {
                            spdlog::debug("跳过消息 #{}: 消息类型不符合过滤条件", message->id_);
                            continue;
                        }
                        
                        // 获取媒体组ID
                        auto media_group_id = get_media_group_id(message);
                        
                        // 如果是媒体组的第一条消息，尝试获取整个媒体组
                        if (media_group_id && !media_group_processed(*media_group_id)) {
                            spdlog::info("发现媒体组消息: {}", *media_group_id);
                            
                            // 获取完整的媒体组
                            auto group_messages = get_media_group_messages(source_chat_id_, *media_group_id);
                            
                            if (!group_messages.empty()) {
                                spdlog::info("获取到媒体组的 {} 条消息", group_messages.size());
                                
                                // 转发整个媒体组
                                if (forward_media_group(group_messages)) {
                                    forwarded_count_ += group_messages.size();
                                    
                                    // 记录已处理的媒体组
                                    processed_media_groups_.insert(*media_group_id);
                                    
                                    // 更新最新消息ID
                                    update_last_message_id(group_messages);
                                } else {
                                    failed_count_ += group_messages.size();
                                }
                            }
                        }
                        // 如果不是媒体组消息或者媒体组已处理，则单独转发这条消息
                        else if (!media_group_id || (media_group_id && media_group_processed(*media_group_id))) {
                            // 跳过已处理的媒体组消息
                            if (media_group_id && media_group_processed(*media_group_id)) {
                                spdlog::debug("跳过消息 #{}: 媒体组 {} 已处理", message->id_, *media_group_id);
                                
                                // 更新最新消息ID
                                last_message_id_ = std::max(last_message_id_, message->id_);
                                continue;
                            }
                            
                            // 转发单条消息
                            if (forward_message(message)) {
                                ++forwarded_count_;
                                
                                // 更新最新消息ID
                                last_message_id_ = std::max(last_message_id_, message->id_);
                                
                                spdlog::info("消息 #{} 转发成功", message->id_);
                            } else {
                                ++failed_count_;
                                spdlog::error("消息 #{} 转发失败", message->id_);
                            }
                        }
                    } catch (const std::exception& e) {
                        ++failed_count_;
                        spdlog::error("处理消息 #{} 时出错: {}", message->id_, e.what());
                    }
                }
            } else {
                spdlog::debug("没有新消息");
            }
            
            // 如果是一次性模式并且已经处理了消息，则停止
            if (config_.mode == ForwarderMode::OneTime && !messages.empty()) {
                spdlog::info("一次性模式下完成转发，停止转发器");
                breaking;
            }
            
            // 等待一段时间后再次检查
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_time_ms_));
        } catch (const std::exception& e) {
            spdlog::error("转发过程中出错: {}", e.what());
            
            // 出错后等待一段时间再重试
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_time_ms_ * 2));
        }
    }
    
    // 停止媒体处理器
    MediaHandler::instance().stop();
    
    spdlog::debug("转发线程已退出");
}

MessageVector RestrictedChannelForwarder::get_new_messages(Int64 chat_id, Int64 last_message_id, int limit) {
    auto get_history = td_api::make_object<td_api::getChatHistory>();
    get_history->chat_id_ = chat_id;
    get_history->limit_ = limit;
    get_history->offset_ = -limit;
    get_history->only_local_ = false;
    
    auto response = ClientManager::instance().send_query(std::move(get_history));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        throw std::runtime_error("获取聊天历史记录失败: " + error->message_);
    }
    
    auto messages = td::move_object_as<td_api::messages>(response);
    MessageVector result;
    
    // 过滤出比last_message_id更新的消息，并按ID升序排序
    for (auto& message : messages->messages_) {
        if (message->id_ > last_message_id) {
            result.push_back(std::move(message));
        }
    }
    
    // 按消息ID升序排序
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a->id_ < b->id_;
    });
    
    return result;
}

Int64 RestrictedChannelForwarder::get_latest_message_id(Int64 chat_id) {
    auto get_history = td_api::make_object<td_api::getChatHistory>();
    get_history->chat_id_ = chat_id;
    get_history->limit_ = 1;
    get_history->offset_ = 0;
    get_history->only_local_ = false;
    
    auto response = ClientManager::instance().send_query(std::move(get_history));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        spdlog::error("获取最新消息ID失败: {}", error->message_);
        return 0;
    }
    
    auto messages = td::move_object_as<td_api::messages>(response);
    
    if (messages->messages_.empty()) {
        return 0;
    }
    
    return messages->messages_[0]->id_;
}

MessageVector RestrictedChannelForwarder::get_media_group_messages(Int64 chat_id, const std::string& media_group_id) {
    // 获取足够多的历史记录，确保能包含整个媒体组
    auto get_history = td_api::make_object<td_api::getChatHistory>();
    get_history->chat_id_ = chat_id;
    get_history->limit_ = 50; // 假设媒体组不会超过50条消息
    get_history->offset_ = 0;
    get_history->only_local_ = false;
    
    auto response = ClientManager::instance().send_query(std::move(get_history));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        throw std::runtime_error("获取媒体组消息失败: " + error->message_);
    }
    
    auto messages = td::move_object_as<td_api::messages>(response);
    MessageVector result;
    
    // 过滤出属于同一媒体组的消息
    for (auto& message : messages->messages_) {
        auto msg_media_group_id = get_media_group_id(message);
        if (msg_media_group_id && *msg_media_group_id == media_group_id) {
            result.push_back(std::move(message));
        }
    }
    
    // 按消息ID升序排序，确保媒体组内顺序正确
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a->id_ < b->id_;
    });
    
    return result;
}

bool RestrictedChannelForwarder::should_forward_message(const Message& message) {
    // 如果过滤器包含全部类型，直接返回true
    for (const auto& filter : message_filters_) {
        if (filter == MessageTypeFilter::All) {
            return true;
        }
    }
    
    // 获取消息类型
    auto content_type = message->content_->get_id();
    
    // 检查消息类型是否在过滤器中
    for (const auto& filter : message_filters_) {
        switch (filter) {
            case MessageTypeFilter::Text:
                if (content_type == td_api::messageText::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Photo:
                if (content_type == td_api::messagePhoto::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Video:
                if (content_type == td_api::messageVideo::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Document:
                if (content_type == td_api::messageDocument::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Audio:
                if (content_type == td_api::messageAudio::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Sticker:
                if (content_type == td_api::messageSticker::ID) {
                    return true;
                }
                break;
            case MessageTypeFilter::Animation:
                if (content_type == td_api::messageAnimation::ID) {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    
    return false;
}

bool RestrictedChannelForwarder::media_group_processed(const std::string& media_group_id) {
    return processed_media_groups_.find(media_group_id) != processed_media_groups_.end();
}

void RestrictedChannelForwarder::update_last_message_id(const MessageVector& messages) {
    for (const auto& message : messages) {
        last_message_id_ = std::max(last_message_id_, message->id_);
    }
}

bool RestrictedChannelForwarder::check_send_message_permission(Int64 chat_id) {
    auto get_chat = td_api::make_object<td_api::getChat>();
    get_chat->chat_id_ = chat_id;
    
    auto response = ClientManager::instance().send_query(std::move(get_chat));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        spdlog::error("获取聊天信息失败: {}", error->message_);
        return false;
    }
    
    auto chat = td::move_object_as<td_api::chat>(response);
    
    // 获取机器人在聊天中的权限
    auto get_chat_member = td_api::make_object<td_api::getChatMember>();
    get_chat_member->chat_id_ = chat_id;
    get_chat_member->member_id_ = td_api::make_object<td_api::messageSenderUser>(
        ClientManager::instance().get_my_id());
    
    response = ClientManager::instance().send_query(std::move(get_chat_member));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        spdlog::error("获取聊天成员信息失败: {}", error->message_);
        return false;
    }
    
    auto chat_member = td::move_object_as<td_api::chatMember>(response);
    
    // 检查是否有发送消息的权限
    if (chat_member->status_->get_id() == td_api::chatMemberStatusAdministrator::ID) {
        auto admin_status = static_cast<const td_api::chatMemberStatusAdministrator*>(chat_member->status_.get());
        return admin_status->can_post_messages_;
    } else if (chat_member->status_->get_id() == td_api::chatMemberStatusMember::ID) {
        // 对于普通成员，如果是普通群组，通常可以发送消息
        return chat->type_->get_id() != td_api::chatTypeSupergroup::ID ||
               !static_cast<const td_api::chatTypeSupergroup*>(chat->type_.get())->is_channel_;
    }
    
    return false;
}

bool RestrictedChannelForwarder::forward_message(const Message& message) {
    spdlog::info("转发消息 #{}", message->id_);
    
    // 获取消息类型
    auto content_type = message->content_->get_id();
    
    // 根据消息类型执行不同的转发逻辑
    if (content_type == td_api::messageText::ID) {
        // 文本消息
        return forward_text_message(message);
    } else if (is_media_message(content_type)) {
        // 媒体消息
        return forward_media_message(message);
    } else {
        spdlog::warn("不支持的消息类型: {}", content_type);
        return false;
    }
}

bool RestrictedChannelForwarder::forward_text_message(const Message& message) {
    auto content = static_cast<const td_api::messageText*>(message->content_.get());
    auto text = content->text_->text_;
    
    // 创建发送消息请求
    auto send_message = td_api::make_object<td_api::sendMessage>();
    send_message->chat_id_ = target_chat_id_;
    
    // 创建消息内容
    auto message_content = td_api::make_object<td_api::inputMessageText>();
    message_content->text_ = td_api::make_object<td_api::formattedText>(
        text, std::vector<td_api::object_ptr<td_api::textEntity>>());
    
    send_message->input_message_content_ = std::move(message_content);
    
    // 发送消息
    auto response = ClientManager::instance().send_query(std::move(send_message));
    
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        spdlog::error("转发文本消息失败: {}", error->message_);
        return false;
    }
    
    return true;
}

bool RestrictedChannelForwarder::forward_media_message(const Message& message) {
    try {
        // 下载媒体文件
        auto media_task_future = MediaHandler::instance().download_media(message);
        auto media_task = media_task_future.get();
        
        if (!media_task || media_task->state() != MediaTaskState::Completed) {
            spdlog::error("下载媒体文件失败");
            return false;
        }
        
        // 上传媒体文件
        auto upload_future = MediaHandler::instance().upload_media(target_chat_id_, media_task);
        auto new_message = upload_future.get();
        
        spdlog::info("媒体消息转发成功: 原ID #{}, 新ID #{}", message->id_, new_message->id_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("转发媒体消息时出错: {}", e.what());
        return false;
    }
}

bool RestrictedChannelForwarder::forward_media_group(const MessageVector& messages) {
    try {
        spdlog::info("转发媒体组，共 {} 条消息", messages.size());
        
        // 下载媒体组
        auto group_task_future = MediaHandler::instance().download_media_group(messages);
        auto group_task = group_task_future.get();
        
        if (!group_task) {
            spdlog::error("下载媒体组失败");
            return false;
        }
        
        // 等待所有媒体下载完成
        while (!group_task->is_completed()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 检查是否所有任务都成功完成
        if (group_task->failed_count() > 0) {
            spdlog::error("媒体组中有 {} 个任务下载失败", group_task->failed_count());
            return false;
        }
        
        // 上传媒体组
        auto upload_future = MediaHandler::instance().upload_media_group(target_chat_id_, group_task);
        auto new_messages = upload_future.get();
        
        if (new_messages.empty()) {
            spdlog::error("上传媒体组失败");
            return false;
        }
        
        spdlog::info("媒体组转发成功，共 {} 条消息", new_messages.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("转发媒体组时出错: {}", e.what());
        return false;
    }
}

bool RestrictedChannelForwarder::is_media_message(int32_t content_type) {
    return content_type == td_api::messagePhoto::ID ||
           content_type == td_api::messageVideo::ID ||
           content_type == td_api::messageDocument::ID ||
           content_type == td_api::messageAudio::ID ||
           content_type == td_api::messageAnimation::ID ||
           content_type == td_api::messageSticker::ID;
}

} // namespace tg_forwarder 