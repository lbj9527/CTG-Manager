#pragma once

#include <string>
#include <functional>
#include <memory>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "utils.h"

namespace tg_forwarder {

// 客户端状态
enum class ClientState {
    Idle,              // 空闲状态
    Connecting,        // 正在连接
    WaitingPhoneNumber,// 等待输入手机号
    WaitingCode,       // 等待输入验证码
    WaitingPassword,   // 等待输入密码
    Ready,             // 已准备就绪
    Closed,            // 已关闭
    Error              // 发生错误
};

// 更新处理器类型
using UpdateHandler = std::function<void(Object)>;

class ClientManager {
public:
    // 单例访问
    static ClientManager& instance();
    
    // 禁止复制和移动
    ClientManager(const ClientManager&) = delete;
    ClientManager& operator=(const ClientManager&) = delete;
    ClientManager(ClientManager&&) = delete;
    ClientManager& operator=(ClientManager&&) = delete;
    
    // 初始化客户端
    void init();
    
    // 启动客户端
    bool start();
    
    // 停止客户端
    void stop();
    
    // 获取当前状态
    ClientState state() const;
    
    // 设置验证码（用于双重认证）
    void send_code(const std::string& code);
    
    // 设置密码（用于双重认证）
    void send_password(const std::string& password);
    
    // 发送请求并等待响应
    Object send_query(Function&& query, double timeout = 10.0);
    
    // 异步发送请求
    std::uint64_t send_query_async(Function&& query, std::function<void(Object)> handler = nullptr);
    
    // 注册更新处理器
    void register_update_handler(const std::string& type, UpdateHandler handler);
    
    // 取消注册更新处理器
    void unregister_update_handler(const std::string& type);
    
    // 清除所有更新处理器
    void clear_update_handlers();
    
private:
    // 私有构造函数（单例模式）
    ClientManager();
    
    // 析构函数
    ~ClientManager();
    
    // 处理TDLib更新
    void process_updates();
    
    // 设置状态
    void set_state(ClientState state);
    
    // 认证处理
    void handle_authorization_state(Object object);
    
    // 客户端标识符
    std::unique_ptr<std::int32_t> client_id_;
    
    // 状态
    std::atomic<ClientState> state_;
    
    // 更新处理线程
    std::unique_ptr<std::thread> update_thread_;
    
    // 线程运行标志
    std::atomic<bool> running_;
    
    // 认证状态同步
    std::mutex auth_mutex_;
    std::condition_variable auth_cond_;
    
    // 响应处理
    std::mutex response_mutex_;
    std::map<std::uint64_t, std::function<void(Object)>> response_handlers_;
    
    // 更新处理器
    std::mutex handlers_mutex_;
    std::map<std::string, UpdateHandler> update_handlers_;
    
    // 请求计数器
    std::atomic<std::uint64_t> query_id_{0};
};

} // namespace tg_forwarder 