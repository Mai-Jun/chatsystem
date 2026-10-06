add_executable(user_server main.cc)
target_link_libraries(user_server PRIVATE im_common im_proto crypto)
