add_executable(file_server main.cc)
target_link_libraries(file_server PRIVATE im_common im_proto stdc++fs)
