#pragma once

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <future>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <functional>
#include "utils.h"

namespace tg_forwarder {

// 媒体任务状态
enum class MediaTaskState {
    Pending,    // 等待处理
    Processing, // 正在处理
    Completed,  // 完成
    Failed,     // 失败
    Cancelled   // 已取消
};

// 媒体任务类型
enum class MediaTaskType {
    Download,   // 下载任务
    Upload      // 上传任务
};

// 媒体任务基类
class MediaTask {
public:
    MediaTask(MediaTaskType type, const Message& message);
    virtual ~MediaTask() = default;
    
    // 获取任务ID
    std::string id() const;
    
    // 获取任务类型
    MediaTaskType type() const;
    
    // 获取任务状态
    MediaTaskState state() const;
    
    // 设置任务状态
    void set_state(MediaTaskState state);
    
    // 获取关联的消息
    const Message& message() const;
    
    // 获取/设置媒体缓冲区
    MemoryBuffer& buffer();
    const MemoryBuffer& buffer() const;
    
    // 获取/设置错误信息
    std::string error() const;
    void set_error(const std::string& error);
    
    // 获取/设置进度
    int progress() const;
    void set_progress(int progress);
    
    // 任务开始和结束时间
    std::chrono::system_clock::time_point start_time() const;
    std::chrono::system_clock::time_point end_time() const;
    void set_start_time(std::chrono::system_clock::time_point time);
    void set_end_time(std::chrono::system_clock::time_point time);
    
    // 获取任务持续时间（毫秒）
    int64_t duration_ms() const;
    
private:
    std::string id_;
    MediaTaskType type_;
    MediaTaskState state_;
    Message message_;
    MemoryBuffer buffer_;
    std::string error_;
    int progress_;
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
};

// 媒体组任务，包含多个相关的媒体任务
class MediaGroupTask {
public:
    MediaGroupTask(const std::string& group_id);
    
    // 获取组ID
    std::string id() const;
    
    // 添加任务
    void add_task(std::shared_ptr<MediaTask> task);
    
    // 获取所有任务
    const std::vector<std::shared_ptr<MediaTask>>& tasks() const;
    
    // 获取完成的任务数量
    size_t completed_count() const;
    
    // 获取失败的任务数量
    size_t failed_count() const;
    
    // 检查所有任务是否已完成（成功或失败）
    bool is_completed() const;
    
    // 获取整体进度（0-100）
    int overall_progress() const;
    
    // 获取说明文字（从第一个消息中提取）
    std::string caption() const;
    
private:
    std::string id_;
    std::vector<std::shared_ptr<MediaTask>> tasks_;
};

// 媒体处理器类
class MediaHandler {
public:
    // 获取单例实例
    static MediaHandler& instance();
    
    // 禁止复制和移动
    MediaHandler(const MediaHandler&) = delete;
    MediaHandler& operator=(const MediaHandler&) = delete;
    MediaHandler(MediaHandler&&) = delete;
    MediaHandler& operator=(MediaHandler&&) = delete;
    
    // 初始化处理器
    void init();
    
    // 启动处理器
    void start();
    
    // 停止处理器
    void stop();
    
    // 下载单个媒体消息
    std::future<std::shared_ptr<MediaTask>> download_media(const Message& message);
    
    // 下载媒体组
    std::future<std::shared_ptr<MediaGroupTask>> download_media_group(const MessageVector& messages);
    
    // 上传媒体任务到目标频道
    std::future<Message> upload_media(Int64 chat_id, const std::shared_ptr<MediaTask>& task);
    
    // 上传媒体组到目标频道
    std::future<MessageVector> upload_media_group(Int64 chat_id, const std::shared_ptr<MediaGroupTask>& group_task);
    
    // 设置并发下载/上传限制
    void set_max_concurrent_downloads(int max);
    void set_max_concurrent_uploads(int max);
    
    // 获取当前活动任务数量
    int active_download_count() const;
    int active_upload_count() const;
    
private:
    // 私有构造函数（单例模式）
    MediaHandler();
    
    // 析构函数
    ~MediaHandler();
    
    // 下载线程函数
    void download_worker();
    
    // 上传线程函数
    void upload_worker();
    
    // 下载文件的具体实现
    void download_file(std::shared_ptr<MediaTask> task);
    
    // 上传文件的具体实现
    Message upload_file(Int64 chat_id, const std::shared_ptr<MediaTask>& task);
    
    // 根据媒体类型发送不同类型的媒体
    Message send_media_by_type(Int64 chat_id, const std::shared_ptr<MediaTask>& task);
    
    // 线程和同步
    std::atomic<bool> running_{false};
    std::vector<std::thread> download_threads_;
    std::vector<std::thread> upload_threads_;
    
    // 下载队列
    std::mutex download_mutex_;
    std::condition_variable download_cv_;
    std::queue<std::pair<std::shared_ptr<MediaTask>, std::promise<std::shared_ptr<MediaTask>>>> download_queue_;
    std::atomic<int> active_downloads_{0};
    
    // 上传队列
    std::mutex upload_mutex_;
    std::condition_variable upload_cv_;
    std::queue<std::tuple<Int64, std::shared_ptr<MediaTask>, std::promise<Message>>> upload_queue_;
    std::atomic<int> active_uploads_{0};
    
    // 并发限制
    int max_concurrent_downloads_;
    int max_concurrent_uploads_;
    
    // 媒体组任务管理
    std::mutex group_mutex_;
    std::map<std::string, std::shared_ptr<MediaGroupTask>> group_tasks_;
};

} // namespace tg_forwarder 