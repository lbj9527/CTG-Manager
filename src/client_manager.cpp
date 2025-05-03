#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <td/telegram/td_api.h>
#include <td/telegram/Client.h>
#include <spdlog/spdlog.h>

#include "../include/client_manager.h"
#include "../include/config.h"

namespace tg_forwarder {

// 单例访问
ClientManager& ClientManager::instance() {
    static ClientManager instance;
    return instance;
}

ClientManager::ClientManager()
    : state_(ClientState::Idle), 
      running_(false) {
    spdlog::debug("客户端管理器初始化");
}

ClientManager::~ClientManager() {
    if (running_) {
        stop();
    }
    spdlog::debug("客户端管理器析构");
}

void ClientManager::init() {
    spdlog::info("初始化Telegram客户端");
    
    // 初始化TDLib日志
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(2));
    
    // 创建客户端实例
    client_id_ = std::make_unique<std::int32_t>(td::ClientManager::create());
    
    spdlog::debug("客户端实例创建成功，ID: {}", *client_id_);
}

bool ClientManager::start() {
    if (running_) {
        spdlog::warn("客户端已经在运行中");
        return true;
    }
    
    if (!client_id_) {
        spdlog::error("客户端未初始化");
        return false;
    }
    
    // 启动更新处理线程
    running_ = true;
    update_thread_ = std::make_unique<std::thread>(&ClientManager::process_updates, this);
    
    // 等待客户端准备就绪
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(60); // 最多等待60秒
    
    std::unique_lock<std::mutex> lock(auth_mutex_);
    bool ready = auth_cond_.wait_for(lock, timeout, [this] {
        return state_ == ClientState::Ready;
    });
    
    if (!ready) {
        spdlog::error("客户端启动超时");
        stop();
        return false;
    }
    
    spdlog::info("客户端启动成功");
    return true;
}

void ClientManager::stop() {
    if (!running_) {
        return;
    }
    
    spdlog::info("正在停止客户端");
    
    // 设置停止标志
    running_ = false;
    
    // 等待更新处理线程结束
    if (update_thread_ && update_thread_->joinable()) {
        update_thread_->join();
        update_thread_.reset();
    }
    
    // 关闭客户端
    if (client_id_) {
        td::ClientManager::destroy(*client_id_);
        client_id_.reset();
    }
    
    // 清理资源
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_handlers_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        update_handlers_.clear();
    }
    
    set_state(ClientState::Closed);
    spdlog::info("客户端已停止");
}

ClientState ClientManager::state() const {
    return state_;
}

void ClientManager::set_state(ClientState state) {
    ClientState old_state = state_;
    state_ = state;
    
    if (old_state != state) {
        spdlog::info("客户端状态变更: {} -> {}", 
            static_cast<int>(old_state), 
            static_cast<int>(state));
        
        if (state == ClientState::Ready || 
            state == ClientState::Error ||
            state == ClientState::Closed) {
            std::lock_guard<std::mutex> lock(auth_mutex_);
            auth_cond_.notify_all();
        }
    }
}

void ClientManager::send_code(const std::string& code) {
    if (state_ != ClientState::WaitingCode) {
        spdlog::warn("客户端当前状态不是等待验证码: {}", static_cast<int>(state_));
        return;
    }
    
    spdlog::info("发送验证码: {}", code);
    
    auto check_code = td_api::make_object<td_api::checkAuthenticationCode>();
    check_code->code_ = code;
    
    send_query_async(std::move(check_code));
}

void ClientManager::send_password(const std::string& password) {
    if (state_ != ClientState::WaitingPassword) {
        spdlog::warn("客户端当前状态不是等待密码: {}", static_cast<int>(state_));
        return;
    }
    
    spdlog::info("发送两步验证密码");
    
    auto check_password = td_api::make_object<td_api::checkAuthenticationPassword>();
    check_password->password_ = password;
    
    send_query_async(std::move(check_password));
}

Object ClientManager::send_query(Function&& query, double timeout) {
    if (!client_id_) {
        throw std::runtime_error("客户端未初始化");
    }
    
    // 创建Promise用于等待响应
    std::promise<Object> promise;
    auto future = promise.get_future();
    
    // 创建异步请求
    auto query_id = send_query_async(std::move(query), [&promise](Object object) {
        promise.set_value(std::move(object));
    });
    
    // 等待响应或超时
    if (future.wait_for(std::chrono::duration<double>(timeout)) == std::future_status::timeout) {
        // 超时，移除处理器
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_handlers_.erase(query_id);
        throw std::runtime_error("请求超时");
    }
    
    // 返回响应
    return future.get();
}

std::uint64_t ClientManager::send_query_async(Function&& query, std::function<void(Object)> handler) {
    if (!client_id_) {
        throw std::runtime_error("客户端未初始化");
    }
    
    // 生成请求ID
    auto query_id = ++query_id_;
    
    // 如果提供了处理器，保存它
    if (handler) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_handlers_[query_id] = std::move(handler);
    }
    
    // 发送请求
    td::ClientManager::send(*client_id_, query_id, std::move(query));
    
    return query_id;
}

void ClientManager::register_update_handler(const std::string& type, UpdateHandler handler) {
    if (!handler) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    update_handlers_[type] = std::move(handler);
    
    spdlog::debug("注册更新处理器: {}", type);
}

void ClientManager::unregister_update_handler(const std::string& type) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    update_handlers_.erase(type);
    
    spdlog::debug("取消注册更新处理器: {}", type);
}

void ClientManager::clear_update_handlers() {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    update_handlers_.clear();
    
    spdlog::debug("清除所有更新处理器");
}

void ClientManager::process_updates() {
    spdlog::info("启动更新处理线程");
    
    // 设置代理
    auto& config = Config::instance();
    if (config.proxy_enabled()) {
        spdlog::info("配置代理: {}://{}:{}", 
            config.proxy_type(), 
            config.proxy_host(), 
            config.proxy_port());
        
        auto proxy_type = td_api::make_object<td_api::proxyTypeSocks5>();
        if (!config.proxy_username().empty()) {
            proxy_type->username_ = config.proxy_username();
            proxy_type->password_ = config.proxy_password();
        }
        
        auto proxy = td_api::make_object<td_api::addProxy>();
        proxy->server_ = config.proxy_host();
        proxy->port_ = config.proxy_port();
        proxy->enable_ = true;
        proxy->type_ = std::move(proxy_type);
        
        td::ClientManager::send(*client_id_, 1, std::move(proxy));
    }
    
    // 请求设置参数
    auto parameters = td_api::make_object<td_api::setTdlibParameters>();
    parameters->database_directory_ = "tdlib-db";
    parameters->use_message_database_ = true;
    parameters->use_secret_chats_ = false;
    parameters->api_id_ = config.api_id();
    parameters->api_hash_ = config.api_hash();
    parameters->system_language_code_ = "zh";
    parameters->device_model_ = "Desktop";
    parameters->application_version_ = "1.0";
    parameters->enable_storage_optimizer_ = true;
    
    td::ClientManager::send(*client_id_, 2, std::move(parameters));
    
    // 主循环
    while (running_) {
        auto response = td::ClientManager::receive(0.1);
        if (!response) {
            continue;
        }
        
        if (!client_id_ || response.client_id != *client_id_) {
            continue;
        }
        
        process_response(std::move(response.object));
    }
    
    spdlog::info("更新处理线程已退出");
}

void ClientManager::process_response(Object object) {
    if (!object) {
        return;
    }
    
    // 检查是否是响应
    if (object->get_id() == td_api::updateAuthorizationState::ID) {
        auto update = td::move_object_as<td_api::updateAuthorizationState>(object);
        handle_authorization_state(std::move(update->authorization_state_));
        return;
    }
    
    std::uint64_t query_id = 0;
    Object response;
    std::function<void(Object)> handler;
    
    // 检查是否是响应回调
    if (object->get_id() == static_cast<std::int32_t>(td::td_api::MessageContentType::Proxy)) {
        auto result = td::move_object_as<td_api::updateOption>(object);
    } else if (object->get_id() == td_api::error::ID) {
        auto error = td::move_object_as<td_api::error>(object);
        spdlog::error("TDLib错误: {} {}", error->code_, error->message_);
    }
    
    // 处理未处理的消息内容类型
    if (object->get_id() == td_api::updateNewMessage::ID) {
        // 交给更新处理器处理
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = update_handlers_.find("updateNewMessage");
        if (it != update_handlers_.end()) {
            it->second(std::move(object));
        }
    }
    
    // 处理请求的响应
    if (object->get_id() == td_api::ok::ID || object->get_id() == td_api::error::ID) {
        if (response_handlers_.empty()) {
            return;
        }
        
        // 获取查询ID和处理器
        {
            std::lock_guard<std::mutex> lock(response_mutex_);
            auto it = response_handlers_.begin();
            query_id = it->first;
            handler = std::move(it->second);
            response_handlers_.erase(it);
        }
        
        // 调用处理器
        if (handler) {
            handler(std::move(object));
        }
    }
}

void ClientManager::handle_authorization_state(Object object) {
    if (!object) {
        return;
    }
    
    auto auth_state_id = object->get_id();
    
    switch (auth_state_id) {
        case td_api::authorizationStateWaitTdlibParameters::ID:
            set_state(ClientState::Connecting);
            break;
            
        case td_api::authorizationStateWaitEncryptionKey::ID:
            spdlog::info("等待加密密钥");
            td::ClientManager::send(*client_id_, 3, td_api::make_object<td_api::checkDatabaseEncryptionKey>());
            break;
            
        case td_api::authorizationStateWaitPhoneNumber::ID:
            spdlog::info("等待手机号码");
            set_state(ClientState::WaitingPhoneNumber);
            
            // 自动发送手机号码
            {
                auto& config = Config::instance();
                auto phone_number = config.phone_number();
                
                if (!phone_number.empty()) {
                    spdlog::info("使用配置的手机号码: {}", phone_number);
                    
                    auto set_phone = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
                    set_phone->phone_number_ = phone_number;
                    
                    td::ClientManager::send(*client_id_, 4, std::move(set_phone));
                } else {
                    spdlog::error("未配置手机号码");
                    set_state(ClientState::Error);
                }
            }
            break;
            
        case td_api::authorizationStateWaitCode::ID:
            spdlog::info("等待验证码");
            set_state(ClientState::WaitingCode);
            
            // 在实际应用中，你可能需要从控制台或UI获取验证码
            {
                std::string code;
                spdlog::info("请输入验证码:");
                std::cin >> code;
                
                // 发送验证码
                send_code(code);
            }
            break;
            
        case td_api::authorizationStateWaitPassword::ID:
            spdlog::info("等待两步验证密码");
            set_state(ClientState::WaitingPassword);
            
            // 在实际应用中，你可能需要从控制台或UI获取密码
            {
                std::string password;
                spdlog::info("请输入两步验证密码:");
                std::cin >> password;
                
                // 发送密码
                send_password(password);
            }
            break;
            
        case td_api::authorizationStateReady::ID:
            spdlog::info("授权成功");
            set_state(ClientState::Ready);
            break;
            
        case td_api::authorizationStateLoggingOut::ID:
            spdlog::info("正在注销");
            set_state(ClientState::Idle);
            break;
            
        case td_api::authorizationStateClosing::ID:
            spdlog::info("正在关闭");
            set_state(ClientState::Idle);
            break;
            
        case td_api::authorizationStateClosed::ID:
            spdlog::info("已关闭");
            set_state(ClientState::Closed);
            break;
            
        default:
            spdlog::error("未知的授权状态: {}", auth_state_id);
            break;
    }
}

} // namespace tg_forwarder 