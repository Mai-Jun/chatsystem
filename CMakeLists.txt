cmake_minimum_required(VERSION 3.16)
project(im_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# M0：仅冒烟测试；M1 起逐步加入 common 公共库与各子服务
enable_testing()
add_subdirectory(test)
