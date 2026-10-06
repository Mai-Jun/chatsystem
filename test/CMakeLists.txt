find_package(GTest REQUIRED)

# M0 冒烟测试（不依赖基础设施）
add_executable(smoke_test smoke_test.cc)
target_link_libraries(smoke_test PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME smoke_test COMMAND smoke_test)

# M1 公共库测试（部分用例依赖基础设施：redis/etcd/rabbitmq/mysql）
add_executable(common_tests common_tests.cc)
target_link_libraries(common_tests PRIVATE
  GTest::gtest GTest::gtest_main
  im_common im_proto)
# M4 模拟客户端（服务器上运行，走网关全链路）
# 显式补 gflags：im_common 展开后 brpc 仍需 gflags 符号，放最后确保被解析
add_executable(gateway_sim_client gateway_sim_client.cc)
target_link_libraries(gateway_sim_client PRIVATE im_common im_proto gflags)

# M5/M6/M7 端到端验收（好友/消息/搜索，服务器上运行）
add_executable(e2e_friend_msg e2e_friend_msg.cc)
target_link_libraries(e2e_friend_msg PRIVATE im_common im_proto gflags)

add_test(NAME common_tests COMMAND common_tests)
