# im-system — 基于 C++ 微服务的即时通讯系统

类聊天室 IM 系统：**Qt 桌面客户端** + **7 个子服务**（入口网关 / 用户管理 / 好友管理 / 消息转发 / 消息存储 / 文件存储 / 语音识别），底层由 **etcd 注册中心**（brpc 服务发现与负载均衡）、**RabbitMQ**（消息投递总线）、**MySQL(ODB) / Redis / Elasticsearch** 支撑。

- 项目计划与决策记录：[docs/PLAN.md](docs/PLAN.md)
- 环境与版本记录：[docs/env.md](docs/env.md)
- 当前进度：**M0 环境搭建**

## 目录结构

```
im-system/
├── proto/                      # protobuf 接口定义（M1）
├── common/                     # 公共基础库：logger/flags/EtcdClient/odb/redis/mq 等（M1）
├── server/                     # 7 个子服务，各出一个可执行程序
│   ├── gateway_server/         #   入口网关：HTTP 请求分发 + WebSocket 推送（M4/M7）
│   ├── user_server/            #   用户：注册/登录(短信验证码)/信息/头像（M3）
│   ├── friend_server/          #   好友：搜索/申请/群聊/待处理事件（M5）
│   ├── message_server/         #   消息转发：获取目标会话并投递（M7）
│   ├── message_storage_server/ #   消息存储：MySQL 持久化 + ES 索引（M6）
│   ├── file_server/            #   文件：单/批量上传下载（M2）
│   └── speech_server/          #   语音：百度云 ASR（M8）
├── client/                     # Qt 桌面客户端（M9）
├── test/                       # gtest 单元测试 + 冒烟测试
├── sql/                        # 建表脚本 + ES 索引初始化
├── conf/                       # 各节点 gflags 配置文件（--flagfile 加载）
├── third_party/                # 外部单头文件库（cpp-httplib）
├── docker/                     # docker-compose（dev 基础设施）+ ES+IK 镜像构建
├── scripts/                    # 依赖安装 / 环境验证脚本
└── docs/                       # PLAN.md 计划、env.md 环境记录
```

## 快速开始（开发环境 WSL2 Ubuntu 22.04）

```bash
# 1. 安装编译依赖（root，首次 20~40 分钟，含 brpc 源码编译）
sudo bash scripts/install_deps.sh

# 2. 启动基础设施五件套（MySQL/Redis/etcd/RabbitMQ/ES+IK）
docker compose -f docker/docker-compose.dev.yml up -d

# 3. 验证基础设施
bash scripts/verify_infra.sh

# 4. 编译并运行冒烟测试
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

> 端口约定与资源预算见 docs/PLAN.md；服务器部署档位（prod-full / prod-minimal）在 M10 部署阶段确定。
