add_executable(message_server main.cc)
target_link_libraries(message_server PRIVATE im_common im_proto)
