# 开发历程与技术要点（简历/面试版）

> 面向读者：项目作者本人（简历撰写与面试准备）。本文把 `PLAN.md`（计划）与
> `HANDOFF.md`（操作手册）里的工程细节，转写成"能讲出来的故事"。
> 更新：2026-10-13（M10 部署固化轮次）。

---

## 一、一句话项目介绍

**分布式即时通讯系统**：C++17 从零实现的完整 IM 后端（7 个微服务 + 网关）加 Qt6 桌面客户端，
部署在 2核1.6G 公网云服务器上，支持文本/图片/文件/语音四类消息、好友与群聊、
在线推送（WebSocket）、离线补历史、中文分词历史搜索——全部功能端到端可运行、可验收。

**规模数据**（可写简历）：约 9,400 行 C++（后端 2,800 + 公共库 1,600 + 客户端 2,600 + 测试 1,600），
8 张 MySQL 表，8 个 proto 协议文件（25 种网关请求类型），7 个独立服务进程/容器，
100+ 条自动化断言覆盖全链路。

---

## 二、技术栈（一句话版）

| 层 | 选型 | 为什么值得写 |
|---|---|---|
| 语言/构建 | C++17 + CMake + gflags | 现代 C++ 工程能力 |
| 服务间通信 | brpc + protobuf（RPC） | 工业级 RPC 框架 |
| 服务发现 | 自研 etcd 轻量客户端（租约 + 续约） | 无重型依赖实现注册中心 |
| 客户端入口 | cpp-httplib（HTTP）+ websocketpp（推送） | 双通道分离设计 |
| 消息总线 | RabbitMQ topic 广播 | 多网关节点水平扩展基础 |
| 存储 | MySQL（ODB ORM）+ Redis | ORM 与缓存双技能 |
| 搜索 | Elasticsearch 7.17 + IK 中文分词（含 LIKE 降级） | 搜索引擎集成 + 降级设计 |
| 客户端 | Qt6 Widgets + WebSockets + Multimedia | 桌面 GUI + 录音播放 |
| 部署 | Docker Compose（多阶段镜像 + restart 策略） | 容器化与运维 |

---

## 三、架构故事（面试怎么讲）

### 3.1 整体拓扑

```
Qt 客户端 ──HTTP(9000 请求)──┐
        └──WS(9001 推送)────┤
                          ▼
                     网关 gateway_server（鉴权 + 按 type 分发）
                          │ etcd 服务发现 + brpc 负载均衡
        ┌────────┬────────┼────────┬──────────┐
        ▼        ▼        ▼        ▼          ▼
    用户服务   好友服务  消息转发  消息存储   文件/语音服务
      │         │        │(核心)    │(M6)      │
      │         │        ├─ MySQL 写入        └─ 本地磁盘
      │         │        └─ RabbitMQ 广播 im.events
      │         │                    │
      ▼         ▼                    ▼
   阿里云短信  ODB/Redis         Elasticsearch（IK 分词）
```

**核心消息链路（最值得讲的部分）**：
客户端发消息 → 网关鉴权 → 消息转发服务校验会话成员 → ①消息存储服务落 MySQL（权威）
②投递 RabbitMQ 广播 → 各网关节点消费 → 按在线表查接收者 → WebSocket 实时推送；
离线用户下次登录由客户端拉历史补齐（服务端不做离线队列）。

**为什么这样设计**（面试金句素材）：
- **HTTP 与 WS 分离**：请求-响应用 HTTP（简单可靠），服务端主动推送才用长连接——
  避免把"响应等待"也塞进长连接状态机；
- **网关无状态**：token 校验走 Redis（`auth:token:{token}` TTL 7 天），任一网关节点都能服务——
  多节点直接水平扩展；
- **MQ 广播而非点对点**：网关节点各自订阅广播（`gateway.<instance_id>` 独占队列绑 `#`），
  每个节点只推"自己在线用户"，节点间不需要互相寻址；
- **MySQL 权威 + ES 索引双写**：ES 挂了不丢消息（异步双写失败只告警），
  搜索自动降级 `LIKE`——可用性优先；
- **同秒消息排序（M10 修复，真实踩坑）**：`create_time` 只有秒级精度，
  同一秒内多条消息顺序不稳定；加 `seq BIGINT AUTO_INCREMENT` 存储层排序键
  （不进 proto/ODB 实体，只在原生 SQL 的 `ORDER BY create_time DESC, seq DESC` 用）
  ——演示"发现问题→设计最小改动方案→回归验证"的完整过程。

### 3.2 一条消息的完整生命周期（适合画图讲）

1. 客户端 `TRANSMIT_MESSAGE`（protobuf）→ 网关 HTTP 9000
2. 网关查 Redis 拿 user_id（**防伪造：客户端传的 user_id 一律丢弃**）
3. brpc 调消息转发服务：校验发送者在会话内 → 生成 message_id
4. 同步调消息存储服务：入库（ODB），后台异步写 ES 索引
5. 投 RabbitMQ `im.events` / routing_key `message.new`（载荷含会话全部成员 id）
6. 网关节点（可能多个）各自消费 → 查 Redis 在线表 `online:{uid}` → 本节点在线就推送
7. 客户端 WS 收到 `PUSH_TYPE_NEW_MESSAGE` → 按 message_id 去重显示 → 会话列表未读 +1
8. 发送者自己也收到广播回显（避免"本地先显示然后被服务器版本替换"的双份问题）

---

## 四、里程碑时间线（可讲故事）

| 阶段 | 内容 | 亮点数据 |
|---|---|---|
| M0 | 环境与骨架 | 云服务器（非本地！）+ docker 四件套 + 冒烟 |
| M1 | 公共库 | etcd 注册发现 / ODB ORM / Redis / MQ 集成测试全绿 |
| M2~M3 | 文件服务、用户服务 | 短信验证码（开发模式固定码旁路设计）、双登录方式 |
| M4 | 网关 | 模拟客户端 8/8（含伪造 token 拦截） |
| M5~M8 | 好友/群聊、消息存储、转发+MQ、语音 ASR | E2E 17 项链路、百度 ASR 集成（开发模式占位转写） |
| M9 | Qt 客户端 | 四轮迭代：四类消息/未读/分页/头像 → 双登录 → 微信风 UI → 退出登录行为；40+8 项自动断言 + 无头 GUI 截图验证 |
| M10 | 部署固化 | 容器化（7 服务 + 基础设施 compose，restart 自愈）、ES 上机（256m 堆适配 1.6G 内存）、公网验收 |

**真实开发节奏**：约一周内从零到 M9，累计 70+ 次提交；每轮以"可运行 + 可验收"为完成标准
（gtest / 模拟客户端 / 双客户端脚本 / 无头 GUI 截图四层验证体系）。

---

## 五、难点与解决（面试高频考点，选 5 个背熟）

### 难点 1：2核1.6G 服务器上的 C++ 编译（资源约束下的工程）
- **问题**：编译 brpc/protobuf 头文件时单个 cc1plus 峰值 500MB~1GB，叠加 MySQL/容器后
  OOM 抖动，**sshd 失联约 2.5 小时**（两次事故）。
- **解决**：`-j1` 串行 + `systemd-run -p MemoryMax=1100M` 限制 + 编译前停不必要容器
  + 重型操作（编译/镜像/启动 ES）严格串行。
- **体现的能力**：对进程内存行为的理解、把事故转化为可复用的操作纪律（写进交接文档）。

### 难点 2：服务发现的自研实现（拒绝重型依赖）
- **决策**：不引入 etcd-cpp-apiv3（拖入整套 gRPC 编译），自研 `EtcdClient`——
  用 cpp-httplib 调 etcd v3 的 HTTP/JSON API，实现租约授予/周期续约/KV 读取。
- **机制**：节点启动注册 `服务名/node N → ip:port`，TTL 10 秒 + 每 3 秒续约；
  宕机租约到期自动摘除；调用方定时拉取列表 + brpc 通道重建。
- **体现的能力**：理解注册中心本质（KV + TTL），能权衡"引框架 vs 写薄封装"。

### 难点 3：Qt 客户端"看起来对但全是坑"的三轮排查（GUI 调试方法论）
- **案例A**：主按钮"隐身"——控件级 inline 样式表（无选择器规则）下压到全部子控件，
  且优先级高于全局 QSS，把绿色主按钮盖成白底白字。
  **定位方法**：截图对比 + 点击实验（往"空白"处点击有效 → 按钮在只是没画出来）。
- **案例B**：无头服务器 GUI 验证——需要 Xvfb + openbox（无窗口管理器时焦点语义全错）
  + xdotool 三件套，控件坐标要按截图实测标定。
- **案例C**：分页游标方向错误 + 补拉插入时序竞态（代码复查发现）——
  服务端升序返回时翻旧页游标应取本页**最旧**一条；补拉期间实时推送先到会导致历史插到末尾之后，
  改为从尾部回扫定位插入点。
- **体现的能力**：GUI 自动化测试能力、样式系统（QSS 优先级）的深入理解、静态复查抓并发 bug。

### 难点 4：ES 集成中的"潜伏 bug 暴露"（环境差异测试的价值）
- **现象**：ES 首次真正上机后，搜索全部 400 错误、静默降级 LIKE。
- **根因**：查询 JSON 拼装多一个 `]`（`"...}}]"` 提前关闭 must 数组）——
  该 bug 从 M6 就存在，但此前 ES 从未部署（一直走降级路径），**从未被执行过**。
- **修复**：删掉多余括号 + 重建 + 验证走 ES 路径（`ok(es)` 标记）。
- **体现的能力**：对"未执行代码路径=未知风险"的敏感性、"降级设计"如何掩盖 bug 的双面性。

### 难点 5：容器化后的启动顺序问题（分布式系统自愈）
- **现象**：切到容器编排后，网关的 MQ 消费者连接失败且**永久放弃**——
  实时推送链路整体失效（HTTP 请求全部正常，只有推送坏）。
- **根因**：`MqSubscriber::start()` 只做一次同步连接，失败返回 false；
  虽然消费循环里有重连逻辑，但循环根本没被启动。容器编排下服务启动顺序不定，
  网关可能早于 RabbitMQ 就绪。
- **修复**：start() 失败不放弃、仍启动消费线程走统一重连路径；
  顺手修了"空闲超时被计为连接失败"导致的 3 秒一次无谓重连噪音。
- **验证**：从宿主机 `kill -9` 容器主进程 → `RestartCount=1` → 健康自动恢复（restart 策略自愈）。
- **体现的能力**：容器编排时序理解、从日志定位"半坏"状态、自愈能力的实证验证。

---

## 六、质量工程（容易被忽略但很加分）

1. **四层验证体系**：
   - 单元/集成测试（gtest + CTest：公共库、ODB CRUD、etcd、Redis、MQ）；
   - 协议冒烟（client_smoke 8 项：注册→双登录→鉴权→搜索→会话→WS）；
   - 双客户端脚本（dual_client_push 41 项：双端注册加好友→四类消息→实时推送→离线补历史
     →同秒排序稳定性，含字节级文件一致性比对）；
   - 无头 GUI 验证（Xvfb + openbox + xdotool 截图 16 阶段）。
2. **可演示的测试工具**：`mock_gateway`（本地 Mock 网关，离线跑全 UI）+ 造数据辅助模式
   （`--send/--send-many/--send-image/--set-avatar`）。
3. **工程化细节**：规范化的迁移脚本（`sql/migration_0002_message_seq.sql` 存量库 +
   建表语句双轨）、ES 存量数据回填脚本（MySQL JSON_OBJECT 生成 bulk 载荷规避转义问题）、
   运行镜像构建脚本（从宿主机抽取 /usr/local 动态库进镜像）。
4. **文档体系**：计划（PLAN）/交接（HANDOFF）/环境坑位（env）/偏离记录（deviation-log）/
   部署联调清单（checklist）/自检报告——**75+ 条真实踩坑记录**，每条含现象、根因、对策。

---

## 七、简历写法建议

### 项目经历条目（可直接改写使用）

> **分布式即时通讯系统**（个人项目）｜C++17 / brpc / Qt6 / MySQL / Redis / RabbitMQ / ES / Docker
> - 从零实现 7 微服务 + 网关的完整 IM 后端与 Qt 桌面客户端，约 9,400 行 C++，
>   公网云服务器端到端运行；
> - 基于 **etcd 租约**实现服务注册发现（自研轻量客户端，避免 gRPC 重型依赖），
>   网关按类型分发 + 负载均衡，支持多节点水平扩展；
> - 设计 **MySQL 权威 + RabbitMQ 广播 + WebSocket 推送**消息链路：
>   在线实时达、离线拉历史，四类消息（文本/图片/文件/语音 ASR）全链路打通；
> - 集成 **Elasticsearch + IK 中文分词**做历史搜索，设计 **MySQL LIKE 自动降级**保证可用性；
> - 搭建 gtest/协议冒烟/双客户端/无头 GUI 四层验证体系（100+ 自动断言），
>   全功能自动化回归；
> - 完成 **Docker Compose 容器化部署**（restart 策略自愈），在 2核1.6G 服务器上解决
>   资源约束下的稳定性问题（内存限制编译、ES 堆调优、重型操作串行纪律）。

### 英文版（如需）

> **Distributed Instant Messaging System** (Personal Project) — C++17, brpc, Qt6, MySQL, Redis, RabbitMQ, Elasticsearch, Docker
> - Built a complete IM platform from scratch: 7 microservices + API gateway backend (~9.4k LOC C++)
>   and a Qt6 desktop client, deployed and verified end-to-end on a public cloud VM;
> - Implemented service discovery on etcd leases with a hand-rolled lightweight client;
>   gateway routes 25 request types with load balancing across service nodes;
> - Designed the message path: MySQL as source of truth + RabbitMQ fan-out + WebSocket push,
>   with offline history sync; 4 message types incl. voice ASR;
> - Integrated Elasticsearch with IK Chinese tokenizer for message search with automatic
>   MySQL LIKE fallback for high availability;
> - Built a 4-layer automated verification suite (unit/integration, protocol smoke,
>   dual-client E2E, headless GUI screenshots) with 100+ assertions;
> - Containerized the whole stack with Docker Compose and self-healing restart policies,
>   tuning memory for a 2-core/1.6GB server.

### 面试可能追问与准备方向

| 追问 | 准备要点 |
|---|---|
| 消息可靠吗？会丢吗？ | MySQL 落库是同步的（失败即回错），MQ 广播是尽力投递；离线靠拉历史兜底；说清"至少一次 vs 最多一次"的取舍 |
| 为什么不用 Redis 做离线消息队列？ | 可讲：离线消息存 DB 拉取更简单；Redis 只存在线表与 token（有 TTL 语义）；避免消息堆积内存 |
| 多网关节点怎么做到消息不重复推送？ | 各节点查自己本地的在线连接表，只推本节点在线用户——节点间零冲突；重复推送的兜底是客户端 message_id 去重 |
| token 怎么设计？ | 登录发 UUID token 存 Redis（TTL 7 天），HTTP/WS 都带；网关查 Redis 得 user_id 注入子请求（客户端传的 user_id 一律丢弃） |
| 服务挂了怎么发现？ | etcd 租约 TTL 10s + 3s 续约；宕机租约过期注册消失，调用方拉取时自然剔除 |
| ES 和 MySQL 数据一致性？ | 异步双写 + 可用性优先；ES 做索引不做权威；说清最终一致的取舍 |
| 最大的收获/最难的点？ | 建议讲：①资源约束下的工程纪律（把 OOM 事故变成制度）；②"未执行路径=未知 bug"（ES 400 案例）；③容器化的时序问题（MQ 自愈修复） |

---

## 八、项目文件导览（自己复习用）

| 想复习 | 看哪 |
|---|---|
| **开发过程复盘（最推荐先读）** | `docs/retrospective.md` |
| 完整计划与设计决策 | `docs/PLAN.md` |
| 现状/操作命令/踩坑速查 | `docs/HANDOFF.md` |
| 环境版本与所有坑位 | `docs/env.md` |
| 本轮偏离与事故记录 | `docs/deviation-log.md` |
| 部署后联调清单 | `docs/checklist.md` |
| 本轮自检报告 | `docs/self-check-2026-10-12.md` |
| 协议定义 | `proto/*.proto`（8 个文件） |
| 核心链路代码 | `server/message_server/main.cc`（转发）、`server/gateway_server/`（分发/推送）、`common/mq.cc`（广播） |
| 客户端核心 | `client/src/protocol/gateway_client.cc`（协议层）、`client/src/ui/chat_window.cc`（聊天渲染） |
