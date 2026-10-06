add_executable(speech_server main.cc)
target_link_libraries(speech_server PRIVATE im_common im_proto)
