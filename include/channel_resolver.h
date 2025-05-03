#pragma once

#include <string>
#include <map>
#include <future>
#include <mutex>
#include "utils.h"

namespace tg_forwarder {

class ChannelResolver {
public:
    // 获取单例实例
    static ChannelResolver& instance();
    
    // 禁止复制和移动
    ChannelResolver(const ChannelResolver&) = delete;
    ChannelResolver& operator=(const ChannelResolver&) = delete;
    ChannelResolver(ChannelResolver&&) = delete;
    ChannelResolver& operator=(ChannelResolver&&) = delete;
    
    // 解析频道标识符，获取频道ID
    // 支持格式：
    // - 频道链接：https://t.me/example_channel
    // - 用户名：@example_channel
    // - 频道ID：-1001234567890
    // 返回标准化的频道ID（如 -1001234567890）
    std::future<Int64> resolve_channel(const std::string& channel_identifier);
    
    // 同步版本，会阻塞直到解析完成
    Int64 resolve_channel_sync(const std::string& channel_identifier);
    
    // 清除缓存
    void clear_cache();
    
private:
    // 私有构造函数（单例模式）
    ChannelResolver();
    
    // 处理t.me链接，提取用户名部分
    std::string extract_username_from_link(const std::string& link);
    
    // 检查是否是合法的频道ID格式
    bool is_valid_channel_id(const std::string& id_str);
    
    // 通过API查询频道信息
    Int64 get_chat_id_by_username(const std::string& username);
    
    // 缓存和锁
    std::mutex cache_mutex_;
    std::map<std::string, Int64> channel_cache_;
};

} // namespace tg_forwarder 