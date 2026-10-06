add_executable(gateway_server main.cc)
target_link_libraries(gateway_server PRIVATE im_common im_proto)
