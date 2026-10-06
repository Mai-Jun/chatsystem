cmake_minimum_required(VERSION 3.16)
project(im_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 生成物带 RPATH，便于服务器直接运行
set(CMAKE_BUILD_RPATH "/usr/local/lib")

# httplib 启用 OpenSSL（M8：百度 ASR 走 HTTPS）。
# 必须全局统一：同一头文件在不同 TU 用不同宏定义会构成 ODR 违规。
# 对既有纯 HTTP 用法（etcd/网关/测试）无行为影响。
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

add_subdirectory(proto)
add_subdirectory(common)
add_subdirectory(server/file_server)  # M2
add_subdirectory(server/user_server)  # M3
add_subdirectory(server/gateway_server)  # M4
add_subdirectory(server/friend_server)  # M5
add_subdirectory(server/message_storage_server)  # M6
add_subdirectory(server/message_server)  # M7
add_subdirectory(server/speech_server)  # M8

enable_testing()
add_subdirectory(test)
