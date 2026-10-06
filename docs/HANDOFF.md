# 交接文档（HANDOFF）——新会话从这里开始

> 更新：2026-10-07。本文件是跨会话续接开发的**唯一权威入口**，每次阶段推进后更新。

## 一、30 秒了解现状

**分布式 IM 系统的完整后端已在阿里云服务器上开发、构建、验收通过**。里程碑 M0（环境）→ M1（公共库）→ M2（文件服务）→ M3（用户服务）→ M4（网关）→ M5（好友）→ M6（消息存储）→ M7（消息转发+MQ 广播）**全部完成并验收**，服务器上 6 个子服务正在运行。

剩余：**M8 语音子服务 → M9 Qt 桌面客户端 → M10 部署固化**（含 ES 上服务器）。

## 二、新会话续接三步

1. **读本文件 + `docs/PLAN.md`**（里程碑表状态列 = 进度；「二、技术栈」「八、关键设计点」是架构速览）。
2. **环境事实看 `docs/env.md`**（全部版本号 + 踩坑记录），**清理台账看 `docs/migration.md`**。
3. **确认开发位置**（见下节），然后从「五、待办」的第一个未完成项开工。

## 三、环境与访问方式

| 项 | 值 |
|---|---|
| **服务器（当前开发+运行环境）** | 阿里云 ECS `47.112.192.119`（Ubuntu 22.04.5，2核1.6G+4G swap） |
| 服务器 SSH | `ssh -i ~/.ssh/im_dev_key root@47.112.192.119`（密钥在本机 `C:\Users\Mai\.ssh\im_dev_key`；密码登录也可用 root/Aa@290631541、mai/290631541） |
| 服务器代码 | `/root/chatsystem`（main 分支，HEAD=7c5fbe7 附近，含全部功能代码） |
| 服务器构建产物 | `/root/chatsystem/build/`（6 个服务 + 测试工具） |
| GitHub | `Mai-Jun/chatsystem`（私有；推送用 Fine-grained PAT，在 git remote URL 或命令行携带；**PAT 形如 github_pat_…，在用户手里**） |
| 本地工作区 | `C:\Users\Mai\.zcode\workspace\default\im-system`（Windows；仅编辑与 git，**不能编译**——WSL/VBox 因本机系统损伤不可用，详见 migration.md） |
| 便携 git / plink | `C:\PortableGit\`、`C:\WSL\plink.exe`（Windows 侧操作服务器用） |

**重要**：GitHub 推送受本地网络波动影响时好时坏。**服务器 `/root/chatsystem` 是当前最完整的副本**；网络不畅时用 git bundle 走 scp 同步（见第六节）。

## 四、已完成里程碑与验收证据

| 里程碑 | 状态 | 验收方式 |
|---|---|---|
| M0 环境 | ✅ | 基础设施四件套 healthy + 冒烟测试通过 |
| M1 公共库 | ✅ | gtest 全绿（含 etcd 注册发现 / ODB CRUD / Redis / MQ 集成测试） |
| M2 文件服务 | ✅ | curl 单/批量上传下载内容一致 |
| M3 用户服务 | ✅ | curl 十步流程（发码→注册→双登录→信息→头像→登出） |
| M4 网关 | ✅ | 模拟客户端 8/8（含伪造 token 拦截） |
| M5 好友服务 | ✅ | E2E：搜索/申请/同意建会话/好友列表/群聊事件 |
| M6 消息存储 | ✅ | E2E：历史分页 / 关键字搜索（ES 暂缓→LIKE 降级） |
| M7 消息转发+MQ | ✅ | E2E：单聊+群消息转发/持久化/MQ 广播投递 |

验收工具（服务器上可随时重跑）：
- `ctest --test-dir build --output-on-failure`（单元+集成测试）
- `./build/test/gateway_sim_client`（M4 网关 8 步链路）
- `./build/test/e2e_friend_msg`（M5/M6/M7 十三项链路）

## 五、待办（按序开工）

### M8 语音子服务（下一个）
- 新建 `server/speech_server/`（仿照 friend_server 的结构：main.cc + CMakeLists + conf/speech_server.flags 已存在）
- 实现 `SpeechRecognition`：调百度云短语音识别 REST API（密钥走 `conf/speech_server.local.flags`，**开发期可不配**——message_server 的 try_asr 调不通时静默跳过，不阻断链路）
- 注册到 etcd（服务名 `speech_server`，端口 10006）；message_server 已实现 `try_asr()` 调用链路（channels.discover 已包含）
- 验收：向 message_server 发一条语音消息（file_id 指向 wav），message 表 asr_text 有值

### M9 Qt 桌面客户端
- 在**新机**或本机 Windows 装 Qt（建议 Qt6 + MinGW 或 MSVC；模块：Widgets/Network/WebSockets/Multimedia）
- 客户端协议 = `proto/gateway.proto`：HTTP POST `/gateway`（ClientRequest 外壳）+ WS `ws://47.112.192.119:9001`（首帧发 ClientRequest 带 token 鉴权，之后收 ServerPush）
- 页面：登录注册 / 主窗口（好友+群+待处理事件）/ 聊天窗（文本/图片/文件/语音）/ 设置
- **顺带完成 M7 遗留**：验证 WS 实时推送（两客户端互聊，B 在线时 A 发消息 B 实时收到）
- 注意：服务器安全组目前可能只开了 22——需在阿里云控制台放行 9000/9001 才能从外网连

### M10 部署固化
- 编写 Dockerfile（多阶段：build 环境 → 运行镜像）或直接二进制 + systemd
- compose 加 `restart: unless-stopped`；补 `message_server`/`gateway_server` 到 compose
- ES 上服务器（需升配 4G 或确认内存余量）；`bash sql/init_es.sh` 建索引后搜索自动从 LIKE 切到 ES
- 按 PLAN.md「十、里程碑」逐项勾验《联调清单》

## 六、操作手册（服务器常用命令）

```bash
# —— 代码同步（本地 → 服务器，网络不畅时的可靠路径）——
# 本地: git bundle create C:\WSL\incoming.bundle main
# 本地: scp -i ~/.ssh/im_dev_key C:\WSL\incoming.bundle root@47.112.192.119:/root/incoming.bundle
cd /root/chatsystem && git fetch /root/incoming.bundle main && git reset --hard FETCH_HEAD

# —— 构建（必须遵守内存约束！）——
# 先停容器和服务释放内存（数据在 volume 不丢），编译完再拉起：
docker compose -f docker/docker-compose.dev.yml stop mysql redis etcd rabbitmq
for s in file_server user_server friend_server message_storage_server message_server gateway_server; do pkill -x $s; done
cmake -S . -B build
systemd-run --scope --quiet -p MemoryMax=1100M nice -n 19 cmake --build build -j1   # 禁止 -j2！
docker compose -f docker/docker-compose.dev.yml up -d mysql redis etcd rabbitmq
sleep 15 && docker exec -i im-mysql mysql -uroot -pim123456 im_system < sql/im_system.sql

# —— 起服务 ——
for s in file_server user_server friend_server message_storage_server message_server; do
  nohup ./build/server/$s/$s --flagfile=conf/common.flags --flagfile=conf/$s.flags >>/root/$s.log 2>&1 &
done
nohup ./build/server/gateway_server/gateway_server --flagfile=conf/common.flags --flagfile=conf/gateway_server.flags >>/root/gateway_server.log 2>&1 &

# —— 验证 ——
bash scripts/verify_infra.sh          # 基础设施（ES 两项失败=正常，服务器暂缓）
./build/test/gateway_sim_client       # 网关 8 步
./build/test/e2e_friend_msg           # 十三项端到端
curl -s http://127.0.0.1:9000/health  # 网关存活
curl -s -X POST http://127.0.0.1:2379/v3/kv/range -H 'Content-Type: application/json' \
  -d '{"key":"L2ltL3JlZ2lzdHJ5Lw==","range_end":"L2ltL3JlZ2lzdHJ5MA=="}'   # 看注册节点
```

**日志**：`/root/<service>.log`（spdlog，同时有滚动文件在 conf 里配置的 log_dir）。

## 七、踩坑速查（详情见 env.md）

1. **服务器禁止 `-j2`/`-j4` 并行编译**：内存只有 1.6G，并行编译 brpc 头文件会 OOM 导致 sshd 失联（事故已记录）。一律 `-j1` + 停容器 + `systemd-run -p MemoryMax=1100M`。
2. protobuf 双版本：生效的是 /usr/local 的 **3.20.2**（服务器遗留），apt 的 3.12.4 被遮蔽；CMake 已用 link_directories 保证。
3. ODB 2.4 编译器绑 g++-10（已装）；`query_one` 返回**裸指针**（用完 delete 或包 unique_ptr）；时间列是 BIGINT 秒级时间戳（非 DATETIME）；原生 SQL 片段用 `+` 拼接（`&&` 不接受字符串）。
4. Docker Hub 被污染：镜像走 `docker.1ms.run` 前缀拉取后 retag（daemon.json 已配 mirror）。
5. bitnami/etcd 镜像被拒：用官方 `quay.io/coreos/etcd:v3.5.16`（compose 已改，含显式监听参数）。
6. Ubuntu 的 websocketpp 包 `asio_no_tls.hpp` 内容有误：用 `websocketpp::config::asio`（同为无 TLS）。
7. 静态链接顺序：提供方库排在消费者之后（common/CMakeLists 已修正，gflags 在 brpc 后）。
8. **RPC 处理函数内所有异常必须捕获**（含 odb 异常）——漏一个就会 terminate 整个服务进程（M3 事故）。
9. 阿里云安全组可能只开了 22；M9 联调前放行 9000/9001。

## 八、会话内决策记录（增量）

- 服务器 MySQL 密码 im123456 / Redis 无密码 / RabbitMQ guest——仅开发环境，M10 时改。
- 新增第 8 张表 `group_event`（群聊事件，原 7 表无处存放"被拉入群"事件），已入 sql/entities/文档。
- 群聊事件采用"消费即已读"语义（GetPendingEvents 返回未读并标记）。
- 验证码 Redis：`sms:code:{phone}` TTL 300s + `sms:limit:{phone}` 60s 限发；登录态 `auth:token:{token}` TTL 7 天；在线状态 `online:{uid}`=网关实例 id（WS 绑定时写、断开删）。
- MQ topic 交换机 `im.events`：routing key `message.new` / `friend.apply` / `group.event`；网关每节点独占队列 `gateway.<instance_id>` 绑 `#`。
- 百度 ASR / 阿里云短信密钥均未配置（走开发模式：ASR 调不通静默跳过、短信只打日志），接入点已留好。
