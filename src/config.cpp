#include <fstream>
#include <iostream>
#include "../include/config.h"

namespace tg_forwarder {

Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config() : is_loaded_(false) {
    // 默认构造函数
}

bool Config::load(const std::string& config_path) {
    try {
        // 打开配置文件
        std::ifstream file(config_path);
        if (!file.is_open()) {
            std::cerr << "无法打开配置文件: " << config_path << std::endl;
            return false;
        }
        
        // 解析JSON
        file >> config_;
        file.close();
        
        // 验证必要的配置项
        if (!config_.contains("api") || 
            !config_["api"].contains("id") || 
            !config_["api"].contains("hash") || 
            !config_["api"].contains("phone")) {
            std::cerr << "配置文件缺少API相关必要字段" << std::endl;
            return false;
        }
        
        if (!config_.contains("channels") || 
            !config_["channels"].contains("source") || 
            !config_["channels"].contains("target")) {
            std::cerr << "配置文件缺少频道相关必要字段" << std::endl;
            return false;
        }
        
        is_loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载配置文件失败: " << e.what() << std::endl;
        return false;
    }
}

int Config::api_id() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_["api"]["id"].get<int>();
}

std::string Config::api_hash() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_["api"]["hash"].get<std::string>();
}

std::string Config::phone_number() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_["api"]["phone"].get<std::string>();
}

bool Config::proxy_enabled() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("enabled")) {
        return false;
    }
    
    return config_["proxy"]["enabled"].get<bool>();
}

std::string Config::proxy_type() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("type")) {
        return "socks5";
    }
    
    return config_["proxy"]["type"].get<std::string>();
}

std::string Config::proxy_host() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("host")) {
        return "127.0.0.1";
    }
    
    return config_["proxy"]["host"].get<std::string>();
}

int Config::proxy_port() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("port")) {
        return 1080;
    }
    
    return config_["proxy"]["port"].get<int>();
}

std::string Config::proxy_username() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("username") || config_["proxy"]["username"].is_null()) {
        return "";
    }
    
    return config_["proxy"]["username"].get<std::string>();
}

std::string Config::proxy_password() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("proxy") || !config_["proxy"].contains("password") || config_["proxy"]["password"].is_null()) {
        return "";
    }
    
    return config_["proxy"]["password"].get<std::string>();
}

std::string Config::source_channel() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_["channels"]["source"].get<std::string>();
}

std::string Config::target_channel() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_["channels"]["target"].get<std::string>();
}

int Config::max_concurrent_downloads() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("forwarder") || !config_["forwarder"].contains("max_concurrent_downloads")) {
        return 2; // 默认值
    }
    
    return config_["forwarder"]["max_concurrent_downloads"].get<int>();
}

int Config::max_concurrent_uploads() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("forwarder") || !config_["forwarder"].contains("max_concurrent_uploads")) {
        return 2; // 默认值
    }
    
    return config_["forwarder"]["max_concurrent_uploads"].get<int>();
}

int Config::retry_count() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("forwarder") || !config_["forwarder"].contains("retry_count")) {
        return 3; // 默认值
    }
    
    return config_["forwarder"]["retry_count"].get<int>();
}

int Config::retry_delay() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("forwarder") || !config_["forwarder"].contains("retry_delay")) {
        return 5; // 默认值
    }
    
    return config_["forwarder"]["retry_delay"].get<int>();
}

std::string Config::log_level() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("log") || !config_["log"].contains("level")) {
        return "info"; // 默认值
    }
    
    return config_["log"]["level"].get<std::string>();
}

std::string Config::log_file() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("log") || !config_["log"].contains("file")) {
        return "telegram_forwarder.log"; // 默认值
    }
    
    return config_["log"]["file"].get<std::string>();
}

bool Config::log_console() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    
    if (!config_.contains("log") || !config_["log"].contains("console")) {
        return true; // 默认值
    }
    
    return config_["log"]["console"].get<bool>();
}

const nlohmann::json& Config::raw() const {
    if (!is_loaded_) {
        throw std::runtime_error("配置未加载");
    }
    return config_;
}

} // namespace tg_forwarder 