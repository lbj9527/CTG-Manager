#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace tg_forwarder {

class Config {
public:
    // 单例访问
    static Config& instance();
    
    // 禁止复制和移动
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;
    
    // 初始化并加载配置
    bool load(const std::string& config_path = "config.json");
    
    // API相关设置
    int api_id() const;
    std::string api_hash() const;
    std::string phone_number() const;
    
    // 代理设置
    bool proxy_enabled() const;
    std::string proxy_type() const;
    std::string proxy_host() const;
    int proxy_port() const;
    std::string proxy_username() const;
    std::string proxy_password() const;
    
    // 频道设置
    std::string source_channel() const;
    std::string target_channel() const;
    
    // 转发设置
    int max_concurrent_downloads() const;
    int max_concurrent_uploads() const;
    int retry_count() const;
    int retry_delay() const;
    
    // 日志设置
    std::string log_level() const;
    std::string log_file() const;
    bool log_console() const;
    
    // 获取原始JSON配置对象
    const nlohmann::json& raw() const;
    
private:
    // 私有构造函数（单例模式）
    Config();
    
    // 配置数据
    nlohmann::json config_;
    bool is_loaded_ = false;
};

} // namespace tg_forwarder 