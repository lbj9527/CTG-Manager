cmake_minimum_required(VERSION 3.10)
project(telegram_restricted_forwarder VERSION 1.0)

# C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找依赖库
find_package(Td REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# 添加头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TD_INCLUDE_DIR}
)

# 添加源文件
set(SOURCES
    src/main.cpp
    src/channel_resolver.cpp
    src/restricted_channel_forwarder.cpp
    src/config.cpp
    src/client_manager.cpp
    src/media_handler.cpp
    src/utils.cpp
)

# 创建可执行文件
add_executable(telegram_restricted_forwarder ${SOURCES})

# 链接依赖库
target_link_libraries(telegram_restricted_forwarder
    PRIVATE
    Td::TdStatic
    Threads::Threads
    CURL::libcurl
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# 安装目标
install(TARGETS telegram_restricted_forwarder
    RUNTIME DESTINATION bin
)

# 复制配置文件到构建目录
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.json
               ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config.json
               COPYONLY) 