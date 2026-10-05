# protobuf 接口编译：protoc 生成 *.pb.{h,cc} 并打包为 im_proto 静态库
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR})
set(PROTOS base user friend message_transmit message_storage file speech gateway)

foreach(p ${PROTOS})
  list(APPEND PB_SRCS ${PROTO_OUT}/${p}.pb.cc)
  list(APPEND PB_HDRS ${PROTO_OUT}/${p}.pb.h)
  add_custom_command(
    OUTPUT ${PROTO_OUT}/${p}.pb.cc ${PROTO_OUT}/${p}.pb.h
    COMMAND protoc --cpp_out=${PROTO_OUT} -I${PROTO_DIR} ${PROTO_DIR}/${p}.proto
    DEPENDS ${PROTO_DIR}/${p}.proto
    COMMENT "protoc: ${p}.proto"
    VERBATIM)
endforeach()

add_library(im_proto STATIC ${PB_SRCS})
target_include_directories(im_proto PUBLIC ${PROTO_OUT})
# brpc 的 service 桩依赖 protobuf 头；/usr/local 优先（服务器生效版本 3.20.2）
target_include_directories(im_proto SYSTEM PUBLIC /usr/local/include)
target_link_libraries(im_proto PUBLIC protobuf pthread)
