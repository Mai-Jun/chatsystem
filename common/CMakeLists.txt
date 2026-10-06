# 公共基础库：flags/util/logger + etcd 注册发现 + brpc 通道管理
#            + ODB 实体(生成) + MQ 封装；ES/Redis 为纯头文件
find_package(spdlog REQUIRED)

# ---- ODB：由 odb 编译器生成 entities-odb.cxx/hxx ----
set(ODB_OUT ${CMAKE_CURRENT_BINARY_DIR}/odb)
file(MAKE_DIRECTORY ${ODB_OUT})
add_custom_command(
  OUTPUT ${ODB_OUT}/entities-odb.cxx ${ODB_OUT}/entities-odb.hxx ${ODB_OUT}/entities.hxx
  COMMAND odb --std c++14 -d mysql --generate-query -I${CMAKE_CURRENT_SOURCE_DIR}
          -o ${ODB_OUT} ${CMAKE_CURRENT_SOURCE_DIR}/odb/entities.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/odb/entities.hpp
  COMMENT "odb: 编译实体定义"
  VERBATIM)
add_library(im_odb STATIC ${ODB_OUT}/entities-odb.cxx)
# ODB 2.4 生成物按 C++14 语义编译（头文件也会被上层 C++17 TU 包含，实测兼容）
set_target_properties(im_odb PROPERTIES CXX_STANDARD 14)
target_include_directories(im_odb PUBLIC
  ${ODB_OUT}                        # entities-odb.hxx
  ${CMAKE_CURRENT_SOURCE_DIR}/odb   # 生成的 hxx 反向包含 entities.hpp
  ${CMAKE_CURRENT_SOURCE_DIR})      # "odb/entities.hpp" 形式包含
target_link_libraries(im_odb PUBLIC odb-mysql odb pthread)

add_library(im_common STATIC
  flags.cc
  util.cc
  etcd_client.cc
  channel_manager.cc
  mq.cc)
target_include_directories(im_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/third_party)
target_include_directories(im_common SYSTEM PUBLIC /usr/local/include)
# 多架构 Ubuntu 会先搜 /usr/lib/x86_64-linux-gnu（apt protobuf 3.12.4），
# 必须显式把 /usr/local/lib 提前，保证链接到服务器遗留的 protobuf 3.20.2
target_link_directories(im_common PUBLIC /usr/local/lib)
# 静态库按左→右解析：提供方必须排在消费者之后
# （brpc 依赖 gflags/protobuf/leveldb/ssl，故这些一律排在 brpc 之后）
target_link_libraries(im_common PUBLIC
  im_odb
  spdlog::spdlog
  brpc
  gflags
  protobuf
  leveldb
  rabbitmq
  hiredis
  redis++
  uuid
  ssl
  crypto
  z
  dl
  rt
  pthread)
