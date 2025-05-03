# C++ 版 Telegram 禁止转发频道消息转发器

这是一个使用C++和TDLib实现的Telegram禁止转发频道消息转发工具，它能够从设置了禁止转发的频道收集消息，并将其转发到目标频道。

## 主要功能

- 支持监听禁止转发的频道
- 使用内存中缓冲区处理媒体文件，避免磁盘IO
- 支持各种类型的消息（文本、图片、视频、文档等）
- 支持媒体组消息处理，保持原始顺序
- 支持媒体组并行下载和上传
- 支持SOCKS5代理
- 支持频道链接解析，可直接使用t.me链接或@username
- 错误处理和重试机制

## 与Python版本的区别

- 使用TDLib而非pyrogram库，提供更底层的API访问
- 完全使用C++17开发，提高性能和资源利用效率
- 添加了媒体组并行上传功能，进一步提高处理效率
- 更强大的错误处理和恢复机制
- 多线程架构，充分利用现代多核处理器

## 编译和安装

### 依赖

- CMake 3.10+
- C++17兼容的编译器
- TDLib (Telegram Database Library)
- spdlog (日志库)
- nlohmann/json (JSON处理库)
- libcurl (网络请求库)
- 线程库

### 编译步骤

```bash
mkdir build && cd build
cmake ..
make
```

### 安装

```bash
make install
```

## 配置

程序使用JSON格式的配置文件，默认为`config.json`：

```json
{
    "api": {
        "id": YOUR_API_ID,
        "hash": "YOUR_API_HASH",
        "phone": "YOUR_PHONE_NUMBER"
    },
    "proxy": {
        "enabled": true,
        "type": "socks5",
        "host": "127.0.0.1",
        "port": 7890,
        "username": null,
        "password": null
    },
    "channels": {
        "source": "https://t.me/source_channel",
        "target": "https://t.me/target_channel"
    },
    "forwarder": {
        "max_concurrent_downloads": 4,
        "max_concurrent_uploads": 4,
        "retry_count": 3,
        "retry_delay": 5
    },
    "log": {
        "level": "info",
        "file": "telegram_forwarder.log",
        "console": true
    }
}
```

## 使用方法

```bash
./telegram_restricted_forwarder [config_path]
```

如果不指定配置文件路径，程序将使用当前目录下的`config.json`文件。

## 注意事项

- 确保输入了正确的API ID、API Hash和电话号码
- 首次运行时需要完成Telegram验证步骤
- 确保有足够的权限访问源频道和目标频道
- 如果程序报错，请检查日志文件获取详细信息

## 许可证

MIT 