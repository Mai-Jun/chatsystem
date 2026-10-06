# 网关按子服务拆分编译单元：降低单 TU 编译内存峰值
# （目标服务器 1.6G 内存，单 TU 含 brpc+redis+++全部 pb.h 会 OOM，详见 docs/env.md）
add_executable(gateway_server
  main.cc
  handlers_user.cc
  handlers_friend.cc
  handlers_msg_file.cc
  handlers_transmit.cc
  ws_push.cc
  mq_bridge.cc)
# websocketpp(asio_no_tls) 需要 boost_system + pthread
target_link_libraries(gateway_server PRIVATE im_common im_proto boost_system pthread)
