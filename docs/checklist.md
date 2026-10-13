# M10 部署联调清单（部署后逐项勾验）

> 依据 PLAN.md「十、里程碑」M10 验收标准：公网双客户端互聊。
> 本清单在部署固化完成后执行；每项记录命令与预期结果，执行时勾选并填实际结果。
> 更新：2026-10-12 建立（M10 部署固化轮次）。

## 〇、部署形态速查

| 项 | dev 形态（M0~M9 期间） | prod 形态（M10 起） |
|---|---|---|
| 应用服务 | 服务器裸进程（nohup，`/root/chatsystem/build`） | Docker 容器（`im-runtime:1.0`，host 网络，`restart: unless-stopped`） |
| 基础设施 | `docker-compose.dev.yml`（4 件套，ES 暂缓） | `docker-compose.prod.yml`（5 件套，含 ES） |
| 掉线自愈 | 无（裸进程挂了要手工拉起） | 容器 restart 策略自动拉起 |
| 代码更新 | 服务器上编译 + 手工重启 | 服务器上编译 + `docker compose restart <svc>` |

切换命令（服务器上执行）：

```bash
cd /root/chatsystem
# —— 从 dev 切 prod ——（顺序敏感：先停裸进程，再停 dev 栈，最后起 prod 栈避免端口冲突）
pkill -f "build/server/[a-z_]*_server/" || true
docker compose -f docker/docker-compose.dev.yml down
docker compose -f docker/docker-compose.prod.yml up -d
docker compose -f docker/docker-compose.prod.yml ps
# —— 从 prod 切回 dev ——（prod down 会停掉基础设施与全部应用容器）
docker compose -f docker/docker-compose.prod.yml down
docker compose -f docker/docker-compose.dev.yml up -d
```

## 一、容器化部署检查

- [x] `docker compose -f docker/docker-compose.prod.yml ps` 显示 12 个容器（5 基础设施 + 7 应用）——实测 11 个在跑 + ES 随后启用（2026-10-13 01:00 切换成功）
- [x] `docker ps` 无残留的重复容器；`ss -lntp` 各端口监听正常（dev→prod 切换脚本执行时先停裸进程）
- [x] `curl -s http://127.0.0.1:9000/health` 返回 ok（容器形态实测）
- [x] etcd 注册表可见全部 7 个子服务（网关日志「服务注册成功」+ 各服务日志）
- [x] 容器日志无 ERROR 级启动错误（**发现并修复**：网关 MQ 桥接在编排启动顺序下永不重试——
      `MqSubscriber::start()` 失败即放弃；已改为后台持续重试 + 修空闲超时误判重连，见 commit）

## 二、掉线自愈（restart 策略验证）

- [x] 容器崩溃自动重启：宿主机 `kill -9 <容器主进程>` → `RestartCount=1` → 服务恢复健康（2026-10-13 01:09 实测）
- [x] 重启后 `curl health` 恢复 ok、WS 端口 9001 恢复监听、登录态不受影响（Redis 数据完好）
- [x] 网关崩溃自愈后实时推送恢复（dual_client_push 41/41 复验通过）
- **笔记**：`docker stop/kill` 属"手动停止"，restart 策略**故意不适用**（Docker 语义）；
  验证自愈必须从宿主机杀容器内 PID（容器内 `kill -1` 会被 PID namespace 保护而静默忽略）
  ——这是本轮踩过的测试方法坑，已记入 deviation-log。

## 三、功能回归（工具链）——容器形态全绿（2026-10-13 01:12）

- [x] `ctest --test-dir build --output-on-failure` 2/2 全绿
- [x] `./build/test/gateway_sim_client` 8/8
- [x] `./build/test/e2e_friend_msg` 0 项失败（含语音链路、搜索）
- [x] `./client/build-server/client_smoke 127.0.0.1 9000 9001` 0 项失败
- [x] `./client/build-server/dual_client_push 127.0.0.1 9000 9001` 41/41（含同秒排序断言）

## 四、ES 启用检查

- [x] `curl -s localhost:9200/_cluster/health` status=green（256m 堆，1.6G 机器实测稳定：swap 0 / load 0.43）
- [x] `bash sql/init_es.sh` 建索引成功；`bash scripts/backfill_es.sh` 存量回填 231 条，0 失败
- [x] **ES 搜索主路径复验**：`ok(es)` 命中（D6 括号修复生效，e2e 引擎标识实测；2026-10-13）
- [x] **降级路径复验**：停 ES → `ok(like-fallback)` 命中 → 起回 ES（两条路径都实测）
- [x] 内存观察：ES(256m)+全栈 used≈1.2G / swap 0 / available≈220M，稳定运行（对照：512m 堆曾两度引发抖动）
- [x] e2e 测试适配 ES 最终一致语义（异步双写重试 6 次 + 打印引擎标识）

## 五、公网双客户端互聊（M10 验收标准）——✅ 完成（2026-10-13）

- [x] 本机（Windows）`im_client.exe` 登录账号 A（端A）并给 B（端B）发消息
- [x] 真实数据加载：会话列表（末条消息预览「你好」+ 微信式时间）、聊天窗 50 条真实历史（含图片/语音）+ 真实头像
- [x] 公网发送成功：「公网验收通过-微信风格UI」绿气泡上屏，MySQL 落库（message 表 298 条）
- [x] WS 推送连接（状态栏「推送: 已连接」）
- [x] 未读计数/历史分页/微信式时间戳（本地 Mock 全界面验证；公网实测加载与发送）

## 六、异常场景

- [x] 容器崩溃自愈（见第二节）
- [x] **重启自愈**：服务器重启后 12 容器全部自动拉起（restart: unless-stopped 生效，2026-10-13 实测）
- [x] **mem_limit 防御**：12 服务全部配置内存上限（重建容器后 `docker inspect` 验证生效：
      网关 192M / ES 768M / MySQL 512M），单容器超用不再拖垮整机
- [x] **编译纪律工具化**：`scripts/safe_build.sh`（停 ES → -j1 编译 → 起 ES 并等 green 全自动），
      首秀实测通过（e2e 重建全程 SSH 未受影响）

## 执行记录（每轮部署后追加）

| 日期 | 执行人 | 范围 | 结果摘要 | 问题/备注 |
|---|---|---|---|---|
| 2026-10-12 | 协调会话+WS1/WS4 | 开工基线 | client_smoke 8/8、dual_client_push 40/40 | 服务器三端一致 de87318 |
| 2026-10-12 深夜 | 协调会话 | seq 修复 | dual_client_push×3 全过（41 项，同秒断言 1 秒内 12 条严格有序）+ smoke/e2e 全绿 | 服务器两次资源抖动（D1/D5），用户重启恢复 |
| 2026-10-13 凌晨 | 协调会话 | M10 容器化 | 11 容器全 Up；全量回归 4/4 全绿；自愈实测通过 | 发现修复 3 个真 bug（MQ 自愈、MQ 空闲误判、ES 查询 JSON）；ES 搜索走 ES 路径待复验 |
| 2026-10-13 上午 | 协调会话 | 微信风格 UI + 公网终验 | 登录页/主窗/会话列表/聊天窗微信化；Mock 全界面走查 + **公网端到端验收通过**（登录/历史/发送/落库） | D7 第四次抖动（编译未停 ES 复发）→ 固化 safe_build.sh 工具；容器加 mem_limit |
| （待续） | | 录音真机验证 | | 需用户真机点「录音」说话验证 |

