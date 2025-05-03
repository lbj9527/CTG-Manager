#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <future>
#include "utils.h"
#include "media_handler.h"

namespace tg_forwarder {

class RestrictedChannelForwarder {
public:
    // 获取单例实例
    static RestrictedChannelForwarder& instance();
    
    // 禁止复制和移动
    RestrictedChannelForwarder(const RestrictedChannelForwarder&) = delete;
    RestrictedChannelForwarder& operator=(const RestrictedChannelForwarder&) = delete;
    RestrictedChannelForwarder(RestrictedChannelForwarder&&) = delete;
    RestrictedChannelForwarder& operator=(RestrictedChannelForwarder&&) = delete;
    
    // 初始化转发器
    std::future<void> initialize();
    
    // 启动监听
    std::future<void> start();
    
    // 停止监听
    void stop();
    
    // 是否正在运行
    bool is_running() const;
    
    // 获取源频道ID
    Int64 source_channel_id() const;
    
    // 获取目标频道ID
    Int64 target_channel_id() const;
    
    // 获取已处理的消息数量
    int processed_message_count() const;
    
    // 获取已处理的媒体组数量
    int processed_media_group_count() const;
    
private:
    // 私有构造函数（单例模式）
    RestrictedChannelForwarder();
    
    // 析构函数
    ~RestrictedChannelForwarder();
    
    // 处理新消息
    void process_message(const Message& message);
    
    // 处理单条消息
    void handle_single_message(const Message& message);
    
    // 处理媒体组消息
    void handle_media_group_message(const Message& message);
    
    // 处理普通文本消息
    void handle_text_message(const Message& message);
    
    // 处理媒体消息
    void handle_media_message(const Message& message);
    
    // 转发媒体组
    void forward_media_group(const MessageVector& media_group);
    
    // 判断是否为媒体消息
    bool is_media_message(const Message& message) const;
    
    // 获取媒体组中的所有消息
    std::future<MessageVector> get_media_group(const Message& message);
    
    // 检查消息是否已处理
    bool is_message_processed(Int64 message_id) const;
    
    // 检查媒体组是否已处理
    bool is_media_group_processed(const std::string& media_group_id) const;
    
    // 标记消息为已处理
    void mark_message_processed(Int64 message_id);
    
    // 标记媒体组为已处理
    void mark_media_group_processed(const std::string& media_group_id);
    
    // 限制处理过的消息和媒体组集合大小
    void limit_processed_collections();
    
    // 更新处理函数（注册给ClientManager）
    void on_update_new_message(Object update);
    
    // 处理异常（如限流等）
    void handle_exception(const std::exception& e, const std::string& context);
    
    // 成员变量
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    Int64 source_channel_id_{0};
    Int64 target_channel_id_{0};
    
    // 已处理的消息ID集合
    mutable std::mutex processed_messages_mutex_;
    std::set<Int64> processed_messages_;
    
    // 已处理的媒体组ID集合
    mutable std::mutex processed_media_groups_mutex_;
    std::set<std::string> processed_media_groups_;
    
    // 统计信息
    std::atomic<int> processed_message_count_{0};
    std::atomic<int> processed_media_group_count_{0};
};

} // namespace tg_forwarder 