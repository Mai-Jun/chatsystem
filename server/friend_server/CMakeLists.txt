add_executable(friend_server main.cc)
target_link_libraries(friend_server PRIVATE im_common im_proto)
