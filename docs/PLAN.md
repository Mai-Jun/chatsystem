# 分布式 IM 即时通讯系统 · 项目计划 v2（已确认）

> 状态：已确认（2026-10-05）。本文件是项目的指导文档与决策记录，里程碑推进中持续更新。

## 〇、决策记录

| 日期 | 决策 |
|---|---|
| 2026-10-05 | 技术选型：WSL2(Ubuntu 22.04) + C++17/CMake + brpc + protobuf + etcd租约 + RabbitMQ + MySQL(ODB) + Redis + 本地磁盘文件存储 + 百度云ASR + Qt客户端 |
| 2026-10-05 | v2 追加：gtest 单测体系、ODB ORM、Elasticsearch(历史消息检索)、阿里云短信验证码、Docker 一键部署 |
| 2026-10-05 | 客户端↔网关：HTTP 承载请求-响应 + WebSocket 承载服务端主动推送 |
| 2026-10-05 | 2核2G 服务器跑全套(含ES)约1.9GB，属演示级（需swap）；**部署档位（prod-full/prod-minimal）推迟到 M10 真机实测后确定，切换零成本**；本地开发用含 ES 的 dev 全套 |
| 2026-10-05 | ES 采用「MySQL 权威存储 + ES 搜索索引」双写架构，ES 不可用时搜索降级 MySQL LIKE |

## 一、可行性结论

本地开发 → 部署 2核2G Ubuntu 服务器：**可行**。

1. **部署拓扑对代码透明**：所有子服务启动时向 etcd 注册「服务类型 → 节点(ip:port)」，网关调用时动态发现 + 负载均衡。单机 8 进程与多机分布式代码完全相同，只是启动参数不同。
2. **内存预算**：prod-minimal（不含 ES）约 1.15GB 可稳跑；prod-full（含 ES heap 512m）约 1.9GB，加 2GB swap 后演示级可用。
3. **编译只发生在 WSL2**，服务器只跑二进制/镜像；WSL2 与服务器同为 Ubuntu 22.04 避免 glibc 兼容问题。

## 二、技术栈

| 层 | 选型 |
|---|---|
| 语言/构建 | C++17 + CMake；开发环境 WSL2 (Ubuntu 22.04) |
| 服务间 RPC | brpc + protobuf |
| 注册中心 | etcd（lease TTL 租约 + 周期续约，宕机自动摘除） |
| 客户端↔网关 | HTTP（cpp-httplib，请求）+ WebSocket（websocketpp，推送） |
| 消息投递 | RabbitMQ 广播队列，各网关节点订阅推送 |
| 持久化 | MySQL（**ODB ORM**）+ Redis（redis-plus-plus：登录态/在线状态/验证码/缓存） |
| 消息搜索 | Elasticsearch 7.17 + IK 分词（历史消息索引） |
| 文件 | 服务器本地磁盘 + MySQL 元数据表 |
| 语音识别 | 百度云短语音识别 REST API |
| 短信 | 阿里云短信（SmsSender 接口抽象；开发期 FixedCodeSmsClient 固定码 666666） |
| 日志/配置/测试 | spdlog / gflags（--flagfile 配置文件）/ gtest + CTest |
| 部署 | Docker + docker-compose（dev / prod-full / prod-minimal 三套） |

设计取舍：不引入 etcd-cpp-apiv3（依赖整套 gRPC 编译，重），在 common 封装轻量 EtcdClient（cpp-httplib 调 etcd v3 HTTP/JSON API：租约授予/续期/KV 读写），服务发现定时拉取 + brpc 通道重建，后续可升级 watch。

## 三、总体架构

```
        ┌──────────── Qt 客户端 ────────────┐
        │   HTTP（发请求）     WebSocket（收推送）│
        └────────┬──────────────▲─────────┘
                 ▼              │
        ┌─────────────────────────────┐
        │      网关子服务（可多节点）      │───订阅广播──┐
        └──┬─────┬─────┬─────┬───────┘            │
           │ 按请求类型分发（etcd 发现 + brpc 负载均衡）
           ▼     ▼     ▼     ▼                  │
       ┌──────┐┌──────┐┌────────┐┌──────────┐   │
       │用户   ││好友   ││消息转发 ││消息存储    │   │
       └──┬───┘└──────┘└─┬───┬──┘└─┬────┬───┘   │
          │ 短信验证码     │   └─投递──────────►┤
   ┌──────▼─────┐   ┌────▼─────┐      ┌─────▼─────┐ │
   │阿里云短信    │   │百度云 ASR │      │Elasticsearch│
   └────────────┘   └──────────┘      └───────────┘
       文件子服务 → 本地磁盘；共享：MySQL / Redis / etcd / RabbitMQ
```

- **普通请求**：客户端 → HTTP → 网关（token 鉴权）→ 按 type 经 etcd 发现 + brpc 负载均衡调用对应子服务 → 响应原路返回。
- **聊天消息**：网关 → 消息转发子服务（校验会话/成员）→ ① 调消息存储子服务持久化 ② 投 RabbitMQ 广播 → 各网关节点消费 → WebSocket 推给在线接收者；离线用户登录后拉历史补齐。
- **图片/文件/语音消息**：先调文件子服务上传拿 file_id，消息体只带引用；语音消息由转发子服务顺带调百度 ASR，转写文本随消息入库。
- **短信链路**：客户端 → 网关 → 用户子服务 SendSmsCode → 阿里云短信下发；验证码存 Redis（TTL 5min，60s 限发）。
- **搜索链路**：消息存储子服务写 MySQL 后异步双写 ES（经 RabbitMQ 解耦）；关键字搜索走 ES，ES 不可用降级 MySQL LIKE。

## 四、端口规划（单机版）

| 端口 | 服务 |
|---|---|
| 2379 | etcd |
| 3306 / 6379 | MySQL / Redis |
| 5672 / 15672 | RabbitMQ / 管理台 |
| 9200 | Elasticsearch |
| 9000 / 9001 | 网关 HTTP / WebSocket |
| 10001~10006 | 用户 / 好友 / 消息转发 / 消息存储 / 文件 / 语音 |

## 五、数据模型（MySQL 7 张表）

- **user**：id(uuid), nickname, description, phone(唯一即账号), password_hash, avatar_file_id, create/update_time
- **friend_apply**：id, user_id(申请人), peer_id, status(pending/agree/reject), create/update_time
- **friend_relation**：id, user_id, peer_id（互为好友写两行，方便单边查询）
- **chat_session**：id, name, type(single/group), create_time（单聊会话在同意好友时自动创建）
- **chat_session_member**：id, session_id, user_id
- **message**：id, session_id, sender_id, type(text/image/file/voice), content, file_id, asr_text, create_time
- **file**：id, file_name, file_size, file_path, create_time

Redis key：`auth:token:{token}→user_id`、`online:{uid}→网关节点`、`sms:code:{phone}`、`cache:user:{uid}` / `cache:session:{sid}`。

ES 索引 `im-message`：session_id / sender_id / type / content(ik_max_word) / file_id / asr_text / create_time。

## 六、Proto 接口清单（proto/）

- **base.proto**：公共结构（UserInfo / MessageInfo / SessionInfo / FileData / 统一请求响应外壳）
- **user.proto**：发送短信验证码 / 注册 / 登录（密码、验证码两种）/ 登出 / 获取信息 / 修改信息 / 修改头像
- **friend.proto**：搜索用户 / 发好友申请 / 处理申请 / 删好友 / 好友列表 / 待处理事件（含群聊事件）/ 创建群会话 / 获取会话成员
- **message_transmit.proto**（转发子服务）：GetTransmitTarget
- **message_storage.proto**：历史消息 / 关键字搜索
- **file.proto**：单/批量上传、单/批量获取
- **speech.proto**：语音转文字
- 网关对客户端的 HTTP/WS 消息体统一为 protobuf 序列化，按 type 字段分发。

## 七、仓库结构

见 README.md。

每个节点用 gflags 指定：服务名、监听端口、etcd 地址、MySQL/Redis/RabbitMQ/ES 地址、实例 id——**同一二进制多起一份就是多节点**；启动方式 `./xxx_server --flagfile conf/xxx.flags`。

## 八、关键设计点

1. **注册与宕机摘除**：节点启动 → etcd 租约授予(TTL=10s) → 写注册 key → 每 3s 续约；宕机后租约到期注册记录自动消失，调用方拉取时自然剔除；重启重新注册即恢复。
2. **网关分发**：收包 → protobuf 解包 → 校验 token → 按 type 路由到对应子服务 brpc client（负载均衡到全部存活节点）。
3. **在线推送**：WS 连接登录后绑定 uid；MQ 消费线程按接收方查在线表，落在本节点的直接推，不在的忽略（对应网关节点自己也会消费到同一条广播）。
4. **鉴权**：登录成功下发 token（Redis TTL），HTTP 请求带 token，WS 首包认证。
5. **心跳与断线重连**：WS 心跳保活；客户端断线自动重连 + 增量拉取离线消息。
6. **短信**：SmsSender 接口双实现（阿里云 / 固定码），flagfile 一行切换；注册=手机号+验证码+昵称密码，登录=密码或验证码。

## 九、测试与联调体系

1. **单元测试**：gtest + CTest，common 库与各子服务核心逻辑，随里程碑交付；
2. **子服务级联调**：每个子服务配独立测试客户端（直连 brpc，脱离网关定位问题）；
3. **网关级联调**：模拟客户端走 HTTP + WebSocket 全链路；
4. **端到端联调**：双客户端真实互聊；M10 交付《联调清单》，部署后逐项勾验。

## 十、里程碑

| 阶段 | 内容 | 验收标准 | 估时 | 状态 |
|---|---|---|---|---|
| M0 | WSL2 环境 + 依赖脚本 + 仓库骨架 + compose(dev) + gtest 冒烟 | 五件套可用，冒烟测试通过 | 1~2 天 | ✅ 完成（环境改用阿里云服务器，ES 暂缓；详见 docs/env.md） |
| M1 | proto 全量 + 公共库（含 ODB 实体/ES 客户端/短信接口）+ gtest 套件 | 单测全绿 | 4~5 天 | ✅ 完成（8 proto 编译；etcd 注册发现/brpc 通道/ODB CRUD/Redis/MQ 集成测试全绿） |
| M2 | 建表 + ES 索引初始化 + 文件子服务 | curl 单/批量上传下载 + 单测 | 2~3 天 | ✅ 完成（curl 四接口实测通过；ES 服务器暂缓、索引脚本就绪） |
| M3 | 用户子服务（含短信验证码注册/登录） | 测试客户端全流程通过 | 3~4 天 | |
| M4 | 网关雏形：HTTP 分发 + token 鉴权 | 模拟客户端走通注册登录 | 2 天 | |
| M5 | 好友子服务 | 双账号互加好友、建群 | 3~4 天 | |
| M6 | 消息存储子服务（MySQL + ES 双写 + 搜索降级） | 历史拉取 + ES 搜索 | 3 天 | |
| M7 | 消息转发 + RabbitMQ + WS 推送（核心） | 端到端实时互聊 | 4~5 天 | |
| M8 | 语音子服务（百度 ASR） | 语音消息带转写文本 | 1~2 天 | |
| M9 | Qt 客户端完整界面（含录音） | 全功能手工测试 | 7~10 天 | |
| M10 | Docker 化 + 服务器部署 + 联调清单 | 公网双客户端互聊 | 3~4 天 | |

合计约 35~45 个有效工作日。M0~M4 完成即有最小可运行链路。

## 十一、部署方案（M10 细化）

- docker/base.Dockerfile（Ubuntu 22.04 + 全部依赖 + brpc 预编译层）+ 每服务轻量 Dockerfile；
- compose profiles：**dev**（仅基础设施，服务本地跑）/ **prod-full**（全套含 ES，适合 4G+）/ **prod-minimal**（2核2G 推荐，ES 不启用，搜索降级 LIKE）；`restart: unless-stopped` 替代 systemd；
- 2核2G + prod-full = 演示级运行（~1.9GB，需 2G swap，低负载可用，GC/OOM 风险自担）；prod-minimal ~1.15GB 稳定；
- 百度 ASR 与阿里云短信密钥放服务器配置（不入 git），服务器需公网出口；
- 服务器装 Ubuntu 22.04 与 WSL2 一致，编译产物直接 scp。

## 十二、新会话续接指南（给下一次会话/新机器的自己）

1. **计划与进度就在本文件**：`docs/PLAN.md` 的「十、里程碑」表里有每阶段状态列；已完成工作看 `git log --oneline`；环境事实看 `docs/env.md`；所有清理事项看 `docs/migration.md`。
2. **代码位置**：阿里云服务器 `/root/chatsystem`（最新，SSH 密钥登录：`ssh -i ~/.ssh/im_dev_key root@47.112.192.119`）；GitHub `Mai-Jun/chatsystem`（推送滞后时以服务器为准）。
3. **服务器上已可直接开发**：依赖全装好（见 env.md），基础设施四件套在跑（docker compose -f docker/docker-compose.dev.yml），冒烟测试 `cmake -S . -B build && cmake --build build && ctest --test-dir build`。
4. **恢复上下文的最短路径**：读本文件「二、技术栈」「八、关键设计点」「十、里程碑状态列」→ `git log` → env.md。当前阶段：M0 完成，M1 进行中。
5. M1 第一件事：验证服务器 odb 编译器（`odb --version`；不行按 env.md 的兜底步骤源码编译）。

## 十三、风险与备注

- brpc 对 protobuf 版本敏感：M0 锁定 protobuf 3.12（Ubuntu 22.04 apt 默认）+ brpc 1.9.0，记录于 env.md；
- WSL2 首次编译 brpc 约 20~40 分钟属正常；代码在 /mnt/c 上编译比 WSL 原生文件系统慢，可接受，追求速度可把仓库 clone 到 WSL home；
- ODB 若 apt 缺包则源码编译（M1 前完成即可）；
- 阿里云短信签名/模板审核需 1~2 天，开发期用固定码不阻塞；
- 国内网络拉取 GitHub/Docker Hub 慢：install_deps.sh 支持 GH_PROXY 环境变量，Docker 可配镜像加速。
