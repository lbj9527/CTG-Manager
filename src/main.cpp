#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <csignal>
#include <thread>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "../include/client_manager.h"
#include "../include/channel_resolver.h"
#include "../include/media_handler.h"
#include "../include/restricted_channel_forwarder.h"
#include "../include/config.h"
#include "../include/version.h"

using json = nlohmann::json;
using namespace tg_forwarder;

// 全局转发器实例，用于信号处理
RestrictedChannelForwarder& forwarder = RestrictedChannelForwarder::instance();

// 信号处理函数
void signal_handler(int signal) {
    spdlog::info("接收到信号 {}，正在停止转发器...", signal);
    forwarder.stop();
    exit(signal);
}

// 初始化日志系统
void init_logger(const LogConfig& config) {
    try {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.log_file, config.max_size * 1024 * 1024, config.max_files);
        
        auto logger = std::make_shared<spdlog::logger>("logger", file_sink);
        
        // 设置日志级别
        spdlog::level::level_enum log_level = spdlog::level::info;
        if (config.level == "debug") {
            log_level = spdlog::level::debug;
        } else if (config.level == "info") {
            log_level = spdlog::level::info;
        } else if (config.level == "warn") {
            log_level = spdlog::level::warn;
        } else if (config.level == "error") {
            log_level = spdlog::level::err;
        } else if (config.level == "critical") {
            log_level = spdlog::level::critical;
        }
        
        logger->set_level(log_level);
        logger->flush_on(spdlog::level::info);
        
        // 设置默认日志格式
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        
        // 设置为默认记录器
        spdlog::set_default_logger(logger);
        
        spdlog::info("日志系统初始化完成，日志文件: {}, 日志级别: {}", 
            config.log_file, config.level);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "日志初始化失败: " << ex.what() << std::endl;
        exit(1);
    }
}

// 加载配置文件
Config load_config(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开配置文件: " + config_file);
    }
    
    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("配置文件解析错误: " + std::string(e.what()));
    }
    
    Config config;
    
    // API 配置
    if (j.contains("api")) {
        config.api.api_id = j["api"].value("api_id", 0);
        config.api.api_hash = j["api"].value("api_hash", "");
        config.api.phone_number = j["api"].value("phone_number", "");
        config.api.bot_token = j["api"].value("bot_token", "");
        config.api.use_bot = j["api"].value("use_bot", false);
    }
    
    // 代理配置
    if (j.contains("proxy")) {
        config.proxy.enabled = j["proxy"].value("enabled", false);
        config.proxy.server = j["proxy"].value("server", "");
        config.proxy.port = j["proxy"].value("port", 0);
        config.proxy.username = j["proxy"].value("username", "");
        config.proxy.password = j["proxy"].value("password", "");
    }
    
    // 转发器配置
    if (j.contains("forwarder")) {
        std::string mode = j["forwarder"].value("mode", "continuous");
        if (mode == "continuous") {
            config.forwarder.mode = ForwarderMode::Continuous;
        } else if (mode == "one_time") {
            config.forwarder.mode = ForwarderMode::OneTime;
        }
        
        config.forwarder.source_channel = j["forwarder"].value("source_channel", "");
        config.forwarder.target_channel = j["forwarder"].value("target_channel", "");
        config.forwarder.wait_time_ms = j["forwarder"].value("wait_time_ms", 1000);
        config.forwarder.max_history_messages = j["forwarder"].value("max_history_messages", 100);
        config.forwarder.max_concurrent_downloads = j["forwarder"].value("max_concurrent_downloads", 2);
        config.forwarder.max_concurrent_uploads = j["forwarder"].value("max_concurrent_uploads", 2);
        
        // 消息过滤器
        if (j["forwarder"].contains("message_filters") && j["forwarder"]["message_filters"].is_array()) {
            for (const auto& filter : j["forwarder"]["message_filters"]) {
                if (filter.is_string()) {
                    config.forwarder.message_filters.push_back(filter.get<std::string>());
                }
            }
        }
    }
    
    // 日志配置
    if (j.contains("logging")) {
        config.logging.level = j["logging"].value("level", "info");
        config.logging.log_file = j["logging"].value("log_file", "forwarder.log");
        config.logging.max_size = j["logging"].value("max_size", 10);
        config.logging.max_files = j["logging"].value("max_files", 5);
    }
    
    return config;
}

// 显示帮助信息
void show_help(const char* program_name) {
    std::cout << "限制频道消息转发工具 v" << VERSION_STRING << std::endl;
    std::cout << "使用方法: " << program_name << " [选项]" << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -h, --help             显示此帮助信息" << std::endl;
    std::cout << "  -v, --version          显示版本信息" << std::endl;
    std::cout << "  -c, --config <文件>    指定配置文件路径 (默认: config.json)" << std::endl;
    std::cout << "  -s, --source <频道>    指定源频道 (覆盖配置文件)" << std::endl;
    std::cout << "  -t, --target <频道>    指定目标频道 (覆盖配置文件)" << std::endl;
    std::cout << "  -o, --one-time         一次性转发模式 (覆盖配置文件)" << std::endl;
    std::cout << "  -d, --debug            启用调试日志" << std::endl;
    std::cout << std::endl;
    std::cout << "频道可以是用户名 (@username)、t.me 链接或频道 ID。" << std::endl;
}

// 显示版本信息
void show_version() {
    std::cout << "限制频道消息转发工具 v" << VERSION_STRING << std::endl;
    std::cout << "构建日期: " << BUILD_DATE << std::endl;
    std::cout << "版权所有 © " << COPYRIGHT_YEAR << " Restricted Channel Forwarder Team" << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认配置文件路径
    std::string config_file = "config.json";
    
    // 命令行参数覆盖
    std::string source_channel;
    std::string target_channel;
    bool one_time_mode = false;
    bool debug_mode = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            show_help(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            show_version();
            return 0;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                std::cerr << "错误: --config 选项需要一个参数。" << std::endl;
                return 1;
            }
        } else if (arg == "-s" || arg == "--source") {
            if (i + 1 < argc) {
                source_channel = argv[++i];
            } else {
                std::cerr << "错误: --source 选项需要一个参数。" << std::endl;
                return 1;
            }
        } else if (arg == "-t" || arg == "--target") {
            if (i + 1 < argc) {
                target_channel = argv[++i];
            } else {
                std::cerr << "错误: --target 选项需要一个参数。" << std::endl;
                return 1;
            }
        } else if (arg == "-o" || arg == "--one-time") {
            one_time_mode = true;
        } else if (arg == "-d" || arg == "--debug") {
            debug_mode = true;
        } else {
            std::cerr << "未知选项: " << arg << std::endl;
            std::cerr << "使用 --help 查看帮助。" << std::endl;
            return 1;
        }
    }
    
    try {
        // 加载配置
        std::cout << "正在加载配置文件: " << config_file << std::endl;
        Config config = load_config(config_file);
        
        // 命令行参数覆盖配置文件
        if (!source_channel.empty()) {
            config.forwarder.source_channel = source_channel;
        }
        
        if (!target_channel.empty()) {
            config.forwarder.target_channel = target_channel;
        }
        
        if (one_time_mode) {
            config.forwarder.mode = ForwarderMode::OneTime;
        }
        
        if (debug_mode) {
            config.logging.level = "debug";
        }
        
        // 初始化日志
        init_logger(config.logging);
        
        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // 输出版本信息
        spdlog::info("限制频道消息转发工具 v{}", VERSION_STRING);
        spdlog::info("初始化中...");
        
        // 初始化客户端管理器
        ClientManager::instance().init();
        
        // 设置代理
        if (config.proxy.enabled) {
            spdlog::info("设置代理: {}:{}", config.proxy.server, config.proxy.port);
            ClientManager::instance().set_proxy(
                config.proxy.server,
                config.proxy.port,
                config.proxy.username,
                config.proxy.password
            );
        }
        
        // 启动客户端
        spdlog::info("启动 Telegram 客户端...");
        bool use_bot = config.api.use_bot;
        
        if (use_bot) {
            if (config.api.bot_token.empty()) {
                spdlog::error("机器人模式下需要提供 bot_token");
                return 1;
            }
            
            spdlog::info("使用机器人模式");
            ClientManager::instance().set_bot_token(config.api.bot_token);
        } else {
            if (config.api.api_id == 0 || config.api.api_hash.empty()) {
                spdlog::error("用户模式下需要提供 api_id 和 api_hash");
                return 1;
            }
            
            if (config.api.phone_number.empty()) {
                spdlog::error("用户模式下需要提供 phone_number");
                return 1;
            }
            
            spdlog::info("使用用户模式");
            ClientManager::instance().set_api_id(config.api.api_id);
            ClientManager::instance().set_api_hash(config.api.api_hash);
            ClientManager::instance().set_phone_number(config.api.phone_number);
        }
        
        // 启动客户端
        if (!ClientManager::instance().start()) {
            spdlog::error("启动 Telegram 客户端失败");
            return 1;
        }
        
        // 初始化频道解析器
        ChannelResolver::instance();
        
        // 初始化媒体处理器
        MediaHandler::instance().init();
        
        // 初始化转发器
        forwarder.init(config.forwarder);
        
        // 检查源频道和目标频道
        if (config.forwarder.source_channel.empty()) {
            spdlog::error("未指定源频道");
            return 1;
        }
        
        if (config.forwarder.target_channel.empty()) {
            spdlog::error("未指定目标频道");
            return 1;
        }
        
        // 启动转发器
        if (!forwarder.start(config.forwarder.source_channel, config.forwarder.target_channel)) {
            spdlog::error("启动转发器失败");
            return 1;
        }
        
        // 主线程等待
        spdlog::info("转发器已启动，按 Ctrl+C 停止");
        
        // 等待转发器停止
        while (forwarder.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 正常停止
        spdlog::info("转发器已停止");
        
        // 关闭客户端
        ClientManager::instance().stop();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        spdlog::critical("程序异常终止: {}", e.what());
        return 1;
    }
} 