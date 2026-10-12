# 项目自检报告（2026-10-12，M10 轮次）

> 范围：M0~M10 全量自检——服务状态、代码一致性、测试回归、部署形态、文档完整性。
> 执行方式：协调会话 + 两个并行工作流（WS1 服务端 seq 修复 / WS4 客户端小项），
> 全部变更经协调者复核 diff 后才进入验收环节。
> 本报告随服务器恢复后的收尾验收持续更新。

## 一、结论速览

| 维度 | 状态 | 说明 |
|---|---|---|
| 后端功能 | ✅ 基线全绿 | client_smoke 8/8、dual_client_push 40/40（开工基线，服务器失联前实测） |
| 消息排序修复 | 🟡 代码完成、待部署验收 | seq 迁移已应用数据库；服务端二进制重建未完成（失联中断） |
| 客户端小项 | ✅ 代码完成、本地构建通过 | sync_latest 补拉 + 会话头像刷新；Windows 重建 + GUI 冒烟两遍通过 |
| ES 上服务器 | ⏸ 待执行 | 镜像已启动拉取（失联前 Download complete 阶段）；IK + 索引 + 搜索切 ES 待恢复后做 |
| M10 部署固化 | 🟡 产物就绪、待执行 | runtime.Dockerfile / prod compose / 切换脚本已写好并通过 YAML 校验 |
| 服务器可用性 | ❌ 失联中 | 2026-10-12 ~21:00 起「横幅超时」，疑似重型操作并发致资源抖动（见 deviation-log D1） |
| 文档 | 🟡 进行中 | deviation-log / checklist 新建；HANDOFF 工作区路径已更新；PLAN 状态待收尾时更新 |

## 二、开工基线（服务器失联前实测，2026-10-12 20:50）

- 服务器：`uptime 6天`、Mem used 735M / available 671M、7 个服务进程全在（含 message_server——
  HANDOFF 待办清单里漏列了它，实际是 7 个服务全运行）、docker 四件套 healthy。
- `client_smoke 127.0.0.1 9000 9001`：8/8 全过（含短信验证码登录）。
- `dual_client_push 127.0.0.1 9000 9001`：40/40 全过（四类消息+WS 推送+离线补历史）。
- 三端 HEAD 一致：本地 main = 服务器 = GitHub（`de87318`）。

## 三、代码一致性核查

- 本地工作副本：`D:\Project\chatsystem`（⚠️ 与文档记录不一致——文档原写 C: 工作区已废弃，见 D2；
  已更新 HANDOFF）。
- 本地 = 服务器 = GitHub（开工时）；本轮改动共 8 个修改文件 + 7 个新增文件待统一提交（见下）。

## 四、本轮改动清单（含审查结论）

| 文件 | 改动 | 审查 | 验收状态 |
|---|---|---|---|
| `server/message_storage_server/main.cc` | ORDER BY 加 seq 次序键（4 处原生 SQL） | ✅ 已复核 | 待重建+重启后跑 3 次 dual_client_push |
| `sql/migration_0002_message_seq.sql` | 新增 seq 列+唯一键迁移 | ✅ 已复核 | ✅ 已在服务器应用（120 行全有 seq，MAX(seq)=120） |
| `sql/im_system.sql` | message 建表同步加 seq | ✅ 已复核 | 待全新部署场景验证（收尾时在 prod 切换中顺带验证） |
| `client/test/dual_client_push.cc` | 新增「同秒连发 12 条顺序稳定」断言 | ✅ 已复核 | 修复前基线 FAIL（乱序复现）；待修复后 3 连过 |
| `client/src/ui/chat_window.{hpp,cc}` | sync_latest 补拉 + 游标修正 + 尾部回扫插入 | ✅ 复核发现 2 处问题（游标 bug、插入时序）均已修 | 待联网场景验证（GUI 手测/无头脚本） |
| `client/src/ui/main_window.{hpp,cc}` | 打开会话刷新对方头像昵称 + 重连补拉触发 | ✅ 已复核 | 同上 |
| `.gitignore` | 忽略 docker/lib（运行镜像构建上下文） | ✅ | — |
| `docker/runtime.Dockerfile`（新） | 运行镜像（apt 运行库 + /usr/local 动态库副本） | ✅ YAML/语法校验 | 待 im-runtime:1.0 构建验证 |
| `docker/docker-compose.prod.yml`（新） | 生产栈（5 基础设施 + 7 应用容器，restart 策略，ES 堆 256m） | ✅ YAML 校验通过 | 待切换验证 + 掉线自愈验证 |
| `scripts/prepare_runtime_image.sh`（新） | 镜像构建前置（拷 /usr/local 库入上下文） | ✅ | 待执行 |
| `scripts/switch_to_prod.sh`（新） | dev→prod 切换脚本 | ✅ | 待执行 |
| `docs/checklist.md`（新） | M10 联调清单（六节勾验表） | ✅ | 待逐项执行 |
| `docs/deviation-log.md`（新） | 偏离记录（D1~D4 本轮 4 项） | ✅ | 持续更新 |

## 五、发现并已处理的问题（本轮）

1. **[代码级] sync_latest 翻页游标方向错误**（WS4 首版用本页最新时间戳当游标）——协调者复核 diff
   发现，已修（本页最旧时间戳+1），重编译通过。
2. **[代码级] sync_latest 插入时序竞态**（补拉期间实时推送先落末尾会显示错序）——协调者二次
   复核发现，已改成从尾部回扫定位插入点，重编译 + GUI 冒烟通过。
3. **[环境级] 工作区路径漂移**（C:→D:，且 cmake 缓存指向旧路径）——WS4 清掉构建目录重配解决；
   文档已更新（D2）。
4. **[运维级] 服务器资源抖动失联**——见 deviation-log D1/D5/D7；三次事故后纪律升级为
   「编译前一律停 ES」。
5. **[小项] `im_system.sql` 与迁移脚本需并存**（全新部署走建表语句，存量库走 ALTER）——已用
   `CREATE TABLE IF NOT EXISTS` + 独立迁移满足。
6. **[代码级·重大] 网关 MQ 桥接"启动失败即永久放弃"**（容器编排下网关先于 RabbitMQ 就绪 →
   实时推送链路整体失效；HTTP 全部正常只有推送坏）——改为 start 失败仍启动消费线程持续重试；
   顺手修「空闲超时被计为连接失败」的 3 秒重连噪音。容器形态 dual_client_push 41/41 复验通过。
7. **[代码级·重大] ES 查询 JSON 多余 `]`**（潜伏自 M6，从未执行过——ES 此前从未部署、搜索一直走
   LIKE 降级）——ES 上机后搜索全新 400 暴露；已修（详见 deviation-log D6）。
8. **[界面级] 聊天窗每条消息带时间戳**（视觉噪音）——优化为微信式「间隔 >5 分钟才显示」，
   Mock 全界面走查 + 截图对比验证（`docs/ui_shots/08→09` 对比）。
9. **[工具级] 本地离线开发能力**：新增 Mock 网关（`client/test/mock_gateway.cc`）——
   预置数据跑全界面 + WS 推送演示 + 文件动态生成；protocol_smoke 8/8 对 Mock 通过
   （修复 keep-alive 连接复用、身份语义两处保真度问题后）。

## 六、待收尾验收项（服务器恢复后执行，按序串行）

1. ~~WS1 收尾~~ ✅ 已完成（dual_client_push×3 全过 + smoke + e2e）；
2. ~~WS2 容器化~~ ✅ 已完成（11 容器全 Up、全量回归 4/4、自愈实测通过）；
3. ES 搜索主路径复验（D6 修复后的 message_storage_server 重建，**先停 ES 再编译**）：
   `ok(es)` 命中 + 停 ES 降级 + 起回恢复；
4. 公网 Windows 客户端双端验证（M10 验收标准终项）；
5. 文档终稿：HANDOFF 现状行、PLAN 里程碑状态、checklist 执行记录；
6. ~~提交与三端同步~~ ✅ **已完成（2026-10-13 04:38）**：本地 7 个提交在直连网络窗口期
   （"时好时坏"的通窗口）直接 `git push origin main` 成功（`de87318..27841e4`），
   无需走服务器代推；GitHub 三端（本地/GitHub）已同步，服务器待恢复后 fetch 追平。

## 七、风险与残余问题

- **同秒翻页死角**：proto 游标为秒级时间戳，单秒消息数 >50 时客户端分页理论上仍有可能跳过；
  本轮 seq 修复只保证「查询返回顺序稳定」，不改变游标语义（属协议级改动，记录备查）。
- **ES 256m 堆在 1.6G 机器的实际表现**（是否稳定、是否挤压其他服务）为 M10 实测项，见 checklist。
- **服务器失联复发风险**：已固化「重型操作串行」纪律；后续如需频繁编译+镜像操作，建议错峰或
  临时升配（PLAN 已有 prod-minimal/prod-full 档位讨论）。
