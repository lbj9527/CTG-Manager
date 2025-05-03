#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <spdlog/spdlog.h>

namespace tg_forwarder {

// 使用 TDLib 命名空间简化代码
namespace td_api = td::td_api;

// 常用类型定义
using Object = td_api::object_ptr<td_api::Object>;
using Function = td_api::object_ptr<td_api::Function>;
using Message = td_api::object_ptr<td_api::message>;
using MessageVector = std::vector<Message>;
using Int32 = std::int32_t;
using Int64 = std::int64_t;

// 错误类
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
    explicit Error(const char* message) : std::runtime_error(message) {}
};

// 网络错误（限流、连接错误等）
class NetworkError : public Error {
public:
    explicit NetworkError(const std::string& message, int retry_after = 0) 
        : Error(message), retry_after_(retry_after) {}
    
    int retry_after() const { return retry_after_; }
    
private:
    int retry_after_;
};

// 频道错误（权限、找不到频道等）
class ChannelError : public Error {
public:
    explicit ChannelError(const std::string& message) : Error(message) {}
};

// 媒体处理错误
class MediaError : public Error {
public:
    explicit MediaError(const std::string& message) : Error(message) {}
};

// 判断消息是否为媒体消息
bool is_media_message(const Message& message);

// 从消息中获取文件ID
std::vector<Int32> get_file_ids(const Message& message);

// 从消息中获取媒体组ID
std::optional<std::string> get_media_group_id(const Message& message);

// 从消息中获取说明文字
std::string get_caption(const Message& message);

// 从消息中获取文本
std::string get_text(const Message& message);

// 延迟执行（处理限流等情况）
void delay(int seconds);

// 生成唯一的消息ID字符串
std::string generate_message_id(Int64 chat_id, Int64 message_id);

// 内存缓冲区类，用于存储下载的媒体数据
class MemoryBuffer {
public:
    MemoryBuffer() = default;
    
    // 添加数据到缓冲区
    void append(const std::string& data);
    
    // 获取缓冲区中的所有数据
    const std::string& data() const;
    
    // 清空缓冲区
    void clear();
    
    // 获取缓冲区大小
    size_t size() const;
    
    // 设置文件名
    void set_name(const std::string& name);
    
    // 获取文件名
    const std::string& name() const;
    
private:
    std::string data_;
    std::string name_;
};

// 媒体组类型
enum class MediaType {
    Unknown,
    Photo,
    Video,
    Document,
    Audio,
    Animation,
    Sticker,
    VoiceNote,
    VideoNote
};

// 获取消息的媒体类型
MediaType get_media_type(const Message& message);

// 将媒体类型转换为字符串
std::string media_type_to_string(MediaType type);

// 根据媒体类型生成文件扩展名
std::string get_file_extension(MediaType type, const Message& message);

} // namespace tg_forwarder 