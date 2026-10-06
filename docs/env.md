# 环境与版本记录（M0 已完成回填）

> 部署服务器时按此对齐版本。

## 开发环境（2026-10-06 定稿：阿里云服务器）

- **开发/运行环境：阿里云 ECS 47.112.192.119（Ubuntu 22.04.5 LTS，2核2G + 2G swap，40G 盘）**
- 本机 Windows：系统组件损伤，WSL2/VirtualBox 均不可用（详见 migration.md），仅作编辑器与 git 使用
- 10月8日换新机后：优先恢复 WSL2 Ubuntu 22.04（按 scripts/install_deps.sh 一键装），服务器转回纯线上角色
- 代码位置：服务器 `/root/chatsystem`（main 分支，含全部本地提交）；SSH 密钥已配置（本机 `~/.ssh/im_dev_key`）

## 工具链版本（服务器实测）

| 组件 | 版本 | 来源 |
|---|---|---|
| g++ | 11.4.0 | apt |
| cmake | 3.22.1 | apt |
| **protobuf（生效）** | **3.20.2** | **/usr/local（服务器原有遗留，链接器优先）** |
| libprotobuf（apt，被遮蔽） | 3.12.4 | apt |
| brpc | 1.9.0（/usr/local/lib/libbrpc.a） | 源码编译 |
| redis-plus-plus | master 快照（/usr/local） | 源码编译 |
| ODB 运行库 | 2.4.0（apt libodb/libodb-mysql） | apt |
| **odb 编译器** | **M1 首日必须验证**（apt `odb` 包安装成功但 --version 无输出；不行则源码编译 2.5，见下） | 待验证 |
| gtest | 1.11.0 | apt |
| spdlog / gflags | 1.9.2 / 2.2.2 | apt |
| websocketpp / librabbitmq / hiredis | 0.8.2 / 0.10.0 / 0.14.1 | apt |
| cpp-httplib | v0.15.3（third_party/httplib.h） | 单头下载 |
| docker / compose | 29.8.2 / v5.6.0（get.docker.com Aliyun 源） | 脚本 |

## 基础设施（docker compose dev，ES 暂缓）

| 容器 | 镜像 | 端口 | 状态 |
|---|---|---|---|
| im-mysql | mysql:8.0（performance_schema=off） | 3306 | ✅ healthy |
| im-redis | redis:7.2 | 6379 | ✅ healthy |
| im-etcd | **quay.io/coreos/etcd:v3.5.16**（bitnami 被镜像源拒；官方镜像+显式监听参数） | 2379 | ✅ |
| im-rabbitmq | rabbitmq:3.12-management | 5672/15672 | ✅ healthy |
| elasticsearch | **服务器暂缓**（内存策略，heap512 起不来时搜索降级 LIKE） | 9200 | ⏸ M6 前按内存决定 |

镜像拉取：Docker Hub 被污染，走 `docker.1ms.run` 前缀（compose 内已 retag 回标准名）；etcd 走 `quay.m.daocloud.io` 前缀。daemon.json 已配三个 mirror。

## 验证结果（M0 验收，2026-10-06）

- verify_infra.sh：**5/7 通过**（MySQL/Redis/etcd/RabbitMQ/管理台全绿；ES 两项暂缓）
- gtest 冒烟测试：**编译通过、运行通过**（1/1 passed）
- 内存基线：四件套运行中 used≈490MB，available≈900MB

## M1 注意事项

1. **odb 编译器验证**是 M1 第一件事：`odb --version`；不行则源码编译 ODB 2.5（步骤见 git 历史版本文档或 codesynthesis 官网）
2. protobuf 锁定 /usr/local 的 3.20.2：编译时确保 `-I/usr/local/include` 优先，勿让 apt 的 3.12.4 头混入
3. brpc/redis++/httplib 均在 /usr/local，链接无需额外路径

## ODB 源码编译兜底步骤（仅当 apt odb 编译器不可用时）

```bash
# ODB 2.5：odb 编译器（pre-built binary 包）+ libodb + libodb-mysql 源码编译
# 见 https://www.codesynthesis.com/products/odb/download.xhtml
```

## 已知坑位（部署阶段照抄）

- **【重要】2核1.6G 服务器禁止并行编译**：`cmake --build -j2` 编译 brpc+protobuf 头文件时，单个 cc1plus 峰值可吃 500MB~1GB，叠加 MySQL/容器后触发 OOM/swap 抖动，**会导致 sshd 无法响应**（现象：ping 正常 12ms、TCP 能建连，但 SSH 横幅永不返回，60 秒超时）。事故记录：2026-10-06 M4 构建 `-j2` 导致服务器失联约 2.5 小时。
  - **对策**：服务器上一律 `cmake --build build -j1`；或把 swap 提到 4G；或改为本地/CI 编译后只传二进制。已固化到 `scripts/` 的部署流程。
- Docker Hub 在国内被污染（解析到 Facebook IP）：必须配 mirror 或用 1ms.run/daocloud 前缀拉取后 retag
- bitnami/etcd 在主流 mirror 全被拒：改用官方 quay.io/coreos/etcd + 显式 --listen-client-urls 参数（compose 已更新）
- MySQL 闲置连接被服务端掐断（错误 4031）：已把 wait_timeout/interactive_timeout 调到 7 天（compose 已更新）
- jammy 的 odb 2.4 编译器硬绑 g++-10：需 `apt install g++-10`（仅用于代码生成，产物用 g++-11 编译）
- 多架构 Ubuntu 链接器优先搜 /usr/lib/x86_64-linux-gnu（apt protobuf 3.12.4），会与 brpc 所需的 3.20.2 冲突：CMake 里显式 `target_link_directories(... /usr/local/lib)`
- WSL/VBox 在旧 Windows 上不可用的完整经过见 migration.md
- **pkill -x 匹配不到超 15 字符进程名**（Linux comm 截断）：`message_storage_server` 的 comm 是 `message_storage`，`pkill -x` 漏杀 → 旧进程占端口、新实例 "Fail to listen"（M8 事故）。停服务一律 `pkill -f "build/server/<服务名>/"`。
- **httplib 开 OpenSSL 必须全局统一**：根 CMakeLists `add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)`（M8 百度 ASR 走 HTTPS）；只给单个 TU 定义会与其他包含 httplib 的 TU 构成 ODR 违规。
- **无头服务器装 Qt6 缺 GL 头**：qt6-base-dev/libqt6websockets6-dev 装完 find_package(Qt6) 仍报 "Failed to find Qt component Widgets"（配置文件其实在，是 Qt6Gui 的 OpenGL 依赖挂了）——必须补 `libgl1-mesa-dev libegl-dev`（M9 已装齐）。
- Qt 客户端编译用 `-DCMAKE_PREFIX_PATH=/usr/local`：否则 FindProtobuf 会抓到 apt 的 protobuf 3.12.4 头与 /usr/local 的 3.20.2 库混搭（与 brpc 同源问题）。
