cmake_minimum_required(VERSION 3.16)
project(im_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 生成物带 RPATH，便于服务器直接运行
set(CMAKE_BUILD_RPATH "/usr/local/lib")

add_subdirectory(proto)
add_subdirectory(common)
add_subdirectory(server/file_server)  # M2
add_subdirectory(server/user_server)  # M3
add_subdirectory(server/gateway_server)  # M4

enable_testing()
add_subdirectory(test)
