#include <regex>
#include <future>
#include <spdlog/spdlog.h>
#include "../include/channel_resolver.h"
#include "../include/client_manager.h"

namespace tg_forwarder {

// 单例实现
ChannelResolver& ChannelResolver::instance() {
    static ChannelResolver instance;
    return instance;
}

ChannelResolver::ChannelResolver() {
    // 构造函数，初始化缓存
    spdlog::debug("频道解析器初始化");
}

std::future<Int64> ChannelResolver::resolve_channel(const std::string& channel_identifier) {
    // 返回一个future，异步解析频道ID
    return std::async(std::launch::async, [this, channel_identifier]() {
        return resolve_channel_sync(channel_identifier);
    });
}

Int64 ChannelResolver::resolve_channel_sync(const std::string& channel_identifier) {
    // 同步解析频道ID
    
    // 检查缓存
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = channel_cache_.find(channel_identifier);
        if (it != channel_cache_.end()) {
            spdlog::debug("频道 {} 已在缓存中，ID: {}", channel_identifier, it->second);
            return it->second;
        }
    }
    
    // 检查是否为整数ID
    if (is_valid_channel_id(channel_identifier)) {
        Int64 channel_id = std::stoll(channel_identifier);
        
        // 缓存并返回结果
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            channel_cache_[channel_identifier] = channel_id;
            spdlog::debug("频道ID {} 已加入缓存", channel_id);
        }
        
        return channel_id;
    }
    
    // 处理链接或用户名
    std::string username;
    
    // 处理t.me链接
    if (channel_identifier.find("t.me/") != std::string::npos) {
        username = extract_username_from_link(channel_identifier);
    } 
    // 处理@用户名
    else if (channel_identifier[0] == '@') {
        username = channel_identifier.substr(1); // 去掉@
    }
    // 否则假设它是一个用户名
    else {
        username = channel_identifier;
    }
    
    // 使用用户名获取频道ID
    Int64 channel_id = get_chat_id_by_username(username);
    
    // 缓存并返回结果
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        channel_cache_[channel_identifier] = channel_id;
        spdlog::debug("频道 {} (ID: {}) 已加入缓存", channel_identifier, channel_id);
    }
    
    return channel_id;
}

void ChannelResolver::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    channel_cache_.clear();
    spdlog::debug("频道缓存已清空");
}

std::string ChannelResolver::extract_username_from_link(const std::string& link) {
    // 使用正则表达式从t.me链接提取用户名
    std::regex pattern(R"(t\.me/([^/\s]+))");
    std::smatch matches;
    
    if (std::regex_search(link, matches, pattern) && matches.size() > 1) {
        std::string username = matches[1].str();
        spdlog::debug("从链接 {} 提取用户名: {}", link, username);
        return username;
    }
    
    throw ChannelError("无法从链接提取用户名: " + link);
}

bool ChannelResolver::is_valid_channel_id(const std::string& id_str) {
    // 检查是否是有效的频道ID格式（-100开头的大整数）
    if (id_str.empty()) {
        return false;
    }
    
    // 检查前缀
    if (id_str.substr(0, 4) != "-100") {
        return false;
    }
    
    // 检查剩余部分是否为数字
    for (size_t i = 4; i < id_str.size(); ++i) {
        if (!std::isdigit(id_str[i])) {
            return false;
        }
    }
    
    return id_str.size() > 4; // 确保前缀后有数字
}

Int64 ChannelResolver::get_chat_id_by_username(const std::string& username) {
    spdlog::debug("通过用户名查询频道ID: {}", username);
    
    // 创建搜索公共聊天请求
    auto query = td_api::make_object<td_api::searchPublicChat>();
    query->username_ = username;
    
    // 发送请求并等待响应
    auto response = ClientManager::instance().send_query(std::move(query));
    
    // 检查响应
    if (response->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(response);
        std::string error_message = "获取频道ID失败: " + error->message_;
        spdlog::error(error_message);
        throw ChannelError(error_message);
    }
    
    // 处理响应
    auto chat = td::move_object_as<td_api::chat>(response);
    Int64 chat_id = chat->id_;
    
    spdlog::debug("获取到频道 {} 的ID: {}", username, chat_id);
    return chat_id;
}

} // namespace tg_forwarder 