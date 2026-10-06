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
add_executable(gateway_sim_client gateway_sim_client.cc)
target_link_libraries(gateway_sim_client PRIVATE im_common im_proto)

add_test(NAME common_tests COMMAND common_tests)
