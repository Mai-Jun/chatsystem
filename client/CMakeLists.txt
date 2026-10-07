# 独立 CMake 工程：Qt6 桌面客户端（与服务器端构建解耦，不依赖 brpc/ODB）
# 依赖：Qt6(Widgets/Network/WebSockets/Multimedia) + protobuf(libprotobuf + protoc)
# 服务器编译验证（protobuf 须用 /usr/local 的 3.20.2，避开 apt 3.12.4）：
#   cmake -S client -B client/build-server -DCMAKE_PREFIX_PATH=/usr/local
#   cmake --build client/build-server -j1
cmake_minimum_required(VERSION 3.16)
project(im_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets Widgets Multimedia)
# protobuf：优先 CONFIG 模式（Windows/MSYS2 的新版 protobuf 需要 abseil 传递链接，
# 模块模式不会带上）；找不到配置文件再退回模块模式（服务器 /usr/local 的 3.20.2）
set(protobuf_MODULE_COMPATIBLE ON CACHE BOOL "" FORCE)
find_package(protobuf CONFIG QUIET)
if(NOT protobuf_FOUND)
  find_package(Protobuf REQUIRED)  # CMAKE_PREFIX_PATH=/usr/local 保证 3.20.2
endif()
# protobuf ≥3.21 的配置模式不再提供模块兼容变量，protoc 自行查找
if(NOT Protobuf_PROTOC_EXECUTABLE)
  find_program(Protobuf_PROTOC_EXECUTABLE protoc
               HINTS "${protobuf_DIR}/../../../bin" "${Protobuf_DIR}/../../../bin"
               REQUIRED)
endif()

message(STATUS "protobuf: ${Protobuf_VERSION}${protobuf_VERSION}, protoc: ${Protobuf_PROTOC_EXECUTABLE}")

# ---- protobuf 生成（与 proto/ 同源定义）----
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/pb)
file(MAKE_DIRECTORY ${PROTO_OUT})
set(PROTOS base user friend message_transmit message_storage file speech gateway)

foreach(p ${PROTOS})
  list(APPEND PB_SRCS ${PROTO_OUT}/${p}.pb.cc)
  add_custom_command(
    OUTPUT ${PROTO_OUT}/${p}.pb.cc ${PROTO_OUT}/${p}.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} --cpp_out=${PROTO_OUT} -I${PROTO_DIR}
            ${PROTO_DIR}/${p}.proto
    DEPENDS ${PROTO_DIR}/${p}.proto
    COMMENT "protoc(client): ${p}.proto"
    VERBATIM)
endforeach()

add_library(im_client_proto STATIC ${PB_SRCS})
target_include_directories(im_client_proto PUBLIC ${PROTO_OUT})
target_link_libraries(im_client_proto PUBLIC protobuf::libprotobuf)

# ---- 协议层（可脱离 GUI 复用；冒烟测试只链这部分）----
add_library(im_client_protocol STATIC
  src/protocol/gateway_client.cc)
target_include_directories(im_client_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(im_client_protocol PUBLIC
  im_client_proto
  Qt6::Core Qt6::Network Qt6::WebSockets)

# ---- 媒体层（录音，仅主程序使用；冒烟测试不链）----
add_library(im_client_media STATIC
  src/media/wav_recorder.cc)
target_include_directories(im_client_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(im_client_media PUBLIC Qt6::Core Qt6::Multimedia)

# ---- 主程序 ----
add_executable(im_client
  src/main.cc
  src/ui/login_window.cc
  src/ui/main_window.cc
  src/ui/chat_window.cc)
target_link_libraries(im_client PRIVATE
  im_client_protocol
  im_client_media
  Qt6::Widgets)

# ---- 协议冒烟测试（无 GUI，服务器上对真实网关运行）----
add_executable(client_smoke test/protocol_smoke.cc)
target_link_libraries(client_smoke PRIVATE im_client_protocol Qt6::Core)

# ---- 双客户端实时推送测试（M7 遗留项：A/B 互发 + WS 推送 + 离线补历史）----
add_executable(dual_client_push test/dual_client_push.cc)
target_link_libraries(dual_client_push PRIVATE im_client_protocol Qt6::Core)
