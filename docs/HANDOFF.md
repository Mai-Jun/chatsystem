# 交接文档（HANDOFF）——新会话从这里开始

> 更新：2026-10-07。本文件是跨会话续接开发的**唯一权威入口**，每次阶段推进后更新。

## 一、30 秒了解现状

**分布式 IM 系统的完整后端已在阿里云服务器上开发、构建、验收通过**。里程碑 M0（环境）→ M1（公共库）→ M2（文件服务）→ M3（用户服务）→ M4（网关）→ M5（好友）→ M6（消息存储）→ M7（消息转发+MQ 广播）→ M8（语音子服务）**全部完成并验收**，服务器上 7 个子服务正在运行。**M9 Qt 客户端已完成并验证（2026-10-07 四轮迭代）**：文本/图片/文件/语音四类消息、未读计数、历史分页、头像、账号密码+验证码双登录、微信风格界面全部就绪；第四轮落实用户反馈：退出登录回到登录/注册页（不再退出程序）、登录页预填手机号、项目根目录快捷方式 `IM即时通讯.lnk` 双击即用、修复部署不自包含（0xC0000135 坑 19）。协议冒烟 8/8、双客户端 40/40、无头 GUI 截图逐项验证；Windows 客户端已重建并经 UIA 实机验证（自动登录→退出→登录页→重登→推送已连接 全链路）。剩余：真机手测收尾（进行中）→ 录音真机验证 → **M10 部署固化**（含 ES 上服务器）。

剩余：**M9 Qt 桌面客户端（剩余部分）→ M10 部署固化**（含 ES 上服务器）。

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
| 服务器构建产物 | `/root/chatsystem/build/`（7 个服务 + 测试工具） |
| GitHub | `Mai-Jun/chatsystem`（私有）。**PAT 已由用户记录在项目内两处（均不入 git 仓库）**：① 本地与服务器的 origin remote URL 已内置 PAT——`git push/fetch origin` 直接可用；② 本地明文备份 `docs/github_pat.local`（被 `.gitignore` 的 `*.local` 规则忽略）。**raw PAT 不能写进任何 git 跟踪文件**：GitHub push protection 会拒绝携带密钥的推送（2026-10-07 实测）。新机器首次 clone：用 ②中的 PAT 或向用户索取；PAT 失效（过期/吊销/转公开仓库被吊销）时更新两端 remote：`git remote set-url origin https://<PAT>@github.com/Mai-Jun/chatsystem.git` |
| 本地工作区 | `C:\Users\Mai\.zcode\workspace\default\im-system`（Windows；仅编辑与 git，**不能编译**——WSL/VBox 因本机系统损伤不可用，详见 migration.md） |
| 便携 git / plink | `C:\PortableGit\`、`C:\WSL\plink.exe`（Windows 侧操作服务器用） |
| **Windows 客户端（新装 2026-10-07）** | 工具链 MSYS2 @ `C:\msys64`（TUNA 源：Qt 6.11.2 base/websockets/multimedia + protobuf 36.2 + gcc 16.2 + cmake/ninja）；产物 `client/cmake-build-win/`（windeployqt 已部署，`im_client.exe` 可直接双击）；重建/部署命令见第六节 |

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
| M8 语音子服务 | ✅ | E2E 17/17（新增 4 项语音链路）；message 表语音消息 asr_text 有值 |
| M9 Qt 客户端 | ✅（功能完成，真机手测中） | client_smoke 8/8（含短信验证码登录）；dual_client_push 40/40（双端注册加好友→文本/文件/图片/语音→WS 推送→离线补历史）；Xvfb 无头 GUI 截图逐项验证（三轮）；Windows 版公网冒烟 7/7 |

验收工具（服务器上可随时重跑）：
- `ctest --test-dir build --output-on-failure`（单元+集成测试）
- `./build/test/gateway_sim_client`（M4 网关 8 步链路）
- `./build/test/e2e_friend_msg`（M5~M8 十七项链路，语音步骤需 speech_server 在线）
- `./client/build-server/client_smoke 127.0.0.1 9000 9001`（M9 协议冒烟 8 项，含短信验证码登录）
- `./client/build-server/dual_client_push 127.0.0.1 9000 9001`（M9 双客户端 40 项：注册→加好友→
  文本/文件/图片/语音四类消息→WS 实时推送→离线补历史；另有辅助模式 `--send/--send-many/
  --send-image/--set-avatar`，用于给指定账号造数据，见 test/dual_client_push.cc 用法注释）
- Windows 版：`client\cmake-build-win\client_smoke.exe 47.112.192.119 9000 9001`（公网冒烟）；
  控制台中文乱码是 cmd 码页问题（GBK），仅观感

## 五、待办（按序开工）

### M9 Qt 桌面客户端（进行中，首轮脚手架已完成 2026-10-07）
**已完成（可编译、协议层冒烟 7/7 通过）**：
- `client/` 独立 CMake 工程（与服务器端构建解耦，只依赖 Qt6 + protobuf）：
  `src/protocol/gateway_client.{hpp,cc}`（HTTP 请求 + WS 推送 + token 管理 + 断线自动重连 + ping 保活）、
  `src/ui/login_window`（登录/注册，开发模式固定码 666666）、`src/ui/main_window`（会话/好友列表、
  搜索加好友、建群、待处理事件）、`src/ui/chat_window`（文本聊天，历史+发送+推送去重）、
  `test/protocol_smoke.cc`（无 GUI 协议冒烟）
- **服务器编译环境已装好**：qt6-base-dev + libqt6websockets6-dev + libgl1-mesa-dev + libegl-dev
  （Qt6Gui 需要 GL 头，无头服务器必须补装后两个包，坑已记 env.md）
- 构建命令：`cmake -S client -B client/build-server -DCMAKE_PREFIX_PATH=/usr/local && cmake --build client/build-server -j1`
- 冒烟：`./client/build-server/client_smoke 127.0.0.1 9000 9001`（发码→固定码注册→登录→
  token 鉴权→搜索→会话列表→WS 鉴权连接，7 项）
- 配套：user_server 开发模式接受固定码 666666（PLAN 设计落地，真机手工测试依赖此旁路）
- Qt 版本基线 6.2.4（jammy apt），Windows 侧后续构建建议 Qt6.2+ + vcpkg protobuf（CMake 已用
  find_package，可迁移）

**已完成（第二轮 2026-10-07，全部验证）**：
- 四类消息 UI：图片/文件走「上传 PutSingle → TRANSMIT(type/file_id/file_name/file_size)」，
  图片下载后经 QTextDocument 图片资源内嵌渲染（220px 等比缩放）+「保存」锚点，文件消息「另存」
  锚点走 QFileDialog 落盘；语音「录音」按钮 → WavRecorder（QAudioSource 拉模式，16k/单声道/
  16bit PCM 手工拼 44 字节 RIFF 头）→ 上传 → 服务端 ASR（转写文本随推送回来显示在气泡），
  「播放」锚点下载到临时文件后 QMediaPlayer 回放
- 未读计数：推送到达且会话窗未聚焦（或未打开/已关闭）时累加，列表项显示「名字 (N)」，
  打开/聚焦即清零；自己发的消息经广播回显不计未读
- 历史分页：首屏最新 50 条，「加载更多」按最旧时间戳+1 向前翻（开区间游标避免同秒消息被跳过，
  重复由 message_id 去重），翻页后视口位置保持
- 头像：会话列表（GetSessionMember 的 avatar_file_id）与好友列表（GetFriendList）异步下载、
  内存缓存、28x28 平滑缩放贴 QIcon
- 协议层新增 upload_file/download_file 便捷封装（GatewayClient）
- 测试：`dual_client_push`（40 项自动断言，含离线补历史与四类消息字节级一致性）+
  4 个造数据辅助模式；`scripts/gui_test.sh`（Xvfb+openbox+xdotool 无头 GUI 驱动，16 阶段截图，
  截图在服务器 /root/gui_shots/）
- 修 bug：登录/注册页两个 tab 各自把 QFormLayout 和 QVBoxLayout 同时设了父，导致登录按钮、
  手机号标签丢失（首轮截图暴露，已改单一布局持有 + 登录按钮 setDefault 支持回车提交）
- 服务器新增依赖：qt6-multimedia-dev、xvfb、openbox、xdotool、imagemagick（apt 直装）

**已完成（第三轮 2026-10-07，登录双方式 + 微信风格界面）**：
- 登录窗重构为三标签：**账号密码（主入口）/ 验证码登录 / 注册**；验证码登录走 `LOGIN_BY_SMS`
  （服务端本就支持，开发模式固定码 666666，60s 限发）；冒烟测试补短信登录步骤（8/8）
- 全局 QSS 主题（`src/ui/theme.h`，微信配色：绿 #07C160 / 链接蓝 #576B95 / 底 #EDEDED），
  登录页卡片式三标签布局，主按钮绿色 primary 样式
- 聊天窗改为真实控件行渲染（不再用 QTextBrowser HTML）：对方白气泡居左、自己绿气泡居右、
  首字圆形头像（`src/ui/avatar.h`，同人同色，无头像用户默认显示）、群聊显示发送者昵称、
  文件/语音操作按钮化（另存/播放，不再用锚点协议）、图片异步下载后就地替换（≤220px）；
  打开会话/来新消息自动贴底
- 主窗口列表加默认头像与扁平化工具条
- **坑**：控件级 inline 样式表的无选择器规则（`background:#FFFFFF`）会下压到所有子控件，
  且优先于全局 QSS——把 `#primary` 按钮盖成白底白字「隐身」（截图对比+点击实验定位）。
  子树背景一律用 objectName 规则 + `Qt::WA_StyledBackground`，禁用 inline 样式表
- Windows 侧已重建同版（MSYS2），GUI 脚本坐标已按新布局重标定（登录窗 420x470、聊天窗 640x700）

**已完成（第四轮 2026-10-07，用户反馈驱动：退出登录行为 + 启动便利性）**：
- **退出登录改为回到登录/注册页（不退出进程）**：MainWindow 新增 `logoutRequested` 信号，
  main.cc 抽出 `show_login_window()/open_main_window()` 两函数互相切换（登录窗/主窗口均
  WA_DeleteOnClose 循环重建）；登出时发 LOGOUT RPC + `disconnect_push()`（并停重连定时器）+
  清内存 token + 删 QSettings `account/token`（下次启动不再自动登录）+ deleteLater 全部聊天窗。
  登录页预填上次登录手机号（QSettings `account/phone`）
- **UIA 实机验证全链路通过**（PowerShell UIAutomation 驱动，Qt 控件名可被识别）：
  自动登录进主窗 → 点「退出登录」→ 登录页出现（同进程）→ 重新登录 → 主窗出现且状态栏
  「推送: 已连接」（disconnect/reconnect 正确）
- **项目根目录新增快捷方式 `IM即时通讯.lnk`** → `client\cmake-build-win\im_client.exe`
  （工作目录设为 exe 所在目录；.lnk 属本机产物已加 .gitignore `*.lnk`）
- **服务器地址确认已内置，无需命令行参数**：main.cc/login_window.cc 默认
  47.112.192.119:9000（WS 9001 硬编码），登录页地址栏可改并持久化；需要参数的只是
  client_smoke/dual_client_push 等控制台测试工具
- **修复部署不自包含（坑 19）**：新增 `client/fix_runtime_dlls.sh`（ASCII-only），重建后必须跑

**待办（M9 收尾，2026-10-07 第四轮后）**：
- **用户真机手测（进行中）**：Windows 客户端已交付（双击项目根目录 `IM即时通讯.lnk`）。
  用户已提三轮反馈（登录方式、界面美化、退出登录行为/启动便利性），均已落实，等下一轮反馈。
  测试账号：端A `19353589846`/pass123、端B `18353589846`/pass123（两者有
  68 条含四类消息的会话）；造数据：`dual_client_push --send-many <对端> <自己> 20 "demo"`
- **录音真机验证**：QtMultimedia 录音链路已实现（WavRecorder→16k/单声道/16bit WAV→上传→
  服务端 ASR），无头服务器无麦克风只能验证编译与协议层（合成 WAV 已验通），用户点「录音」
  说话后看气泡转写文本
- **已知问题（M10 处理，非阻塞）**：消息排序同秒不稳定——message 表只有秒级 create_time，
  服务端 `ORDER BY create_time` 无次序键，同一秒内的消息顺序不确定（正常人工聊天不受影响，
  压测/刷屏时可见乱序）。建议 M10 给 message 表加 `seq BIGINT NOT NULL AUTO_INCREMENT,UNIQUE(seq)`
  并改为 `ORDER BY create_time DESC, seq DESC`（需 ODB 实体同步 + 重建服务端）
- 小项：会话列表头像只在首次解析会话时拉取（对方换头像不实时刷新，重登后更新）；
  ChatWindow 关闭只是 hide，重开不重拉历史（靠推送保鲜，WS 断连期间漏的消息要重登才补）；
  控制台测试程序在 Windows cmd 下中文乱码（printf UTF-8 vs 控制台 GBK，仅观感，GUI 不受影响）

### M10 部署固化
- 编写 Dockerfile（多阶段：build 环境 → 运行镜像）或直接二进制 + systemd
- compose 加 `restart: unless-stopped`；补 `message_server`/`gateway_server`/`speech_server` 到 compose
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
# 注意必须用 pkill -f 按路径匹配：pkill -x 按进程名匹配上限 15 字符，
# "message_storage_server" 会被截断为 "message_storage" 而漏杀（踩坑 10）
for s in file_server user_server friend_server message_storage_server message_server gateway_server speech_server; do pkill -f "build/server/$s/"; done
cmake -S . -B build
systemd-run --scope --quiet -p MemoryMax=1100M nice -n 19 cmake --build build -j1   # 禁止 -j2！
docker compose -f docker/docker-compose.dev.yml up -d mysql redis etcd rabbitmq
sleep 15 && docker exec -i im-mysql mysql -uroot -pim123456 im_system < sql/im_system.sql

# —— 起服务 ——
for s in file_server user_server friend_server message_storage_server message_server speech_server; do
  nohup ./build/server/$s/$s --flagfile=conf/common.flags --flagfile=conf/$s.flags >>/root/$s.log 2>&1 &
done
nohup ./build/server/gateway_server/gateway_server --flagfile=conf/common.flags --flagfile=conf/gateway_server.flags >>/root/gateway_server.log 2>&1 &

# —— Qt 客户端构建（M9，独立工程；依赖 qt6-base/websockets/multimedia-dev）——
cmake -S client -B client/build-server -DCMAKE_PREFIX_PATH=/usr/local   # 锁 /usr/local 的 protobuf 3.20.2
systemd-run --scope --quiet -p MemoryMax=1100M nice -n 19 cmake --build client/build-server -j1
./client/build-server/client_smoke 127.0.0.1 9000 9001        # 协议冒烟（7 项）
./client/build-server/dual_client_push 127.0.0.1 9000 9001    # 双客户端四类消息+推送（40 项）

# —— 无头 GUI 验证（可选；Xvfb+openbox+xdotool+imagemagick 已装）——
cp scripts/gui_test.sh /root/gui_test.sh && tr -d '\r' < /root/gui_test.sh > /root/gui_test.sh.c
bash /root/gui_test.sh.c 1      # 阶段 1~16：启动/登录/会话/发消息/未读/分页/图片渲染，
                                # 每阶段截图到 /root/gui_shots/，窗口坐标偏移已按 openbox 标定

# —— Windows 客户端（MSYS2，本机 cmd；GUI 界面迭代时在本机构建验证）——
# 首次配置（已完成）：cmake -S client -B client/cmake-build-win -G Ninja
#   -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 -DCMAKE_PREFIX_PATH=/mingw64
set MSYSTEM=MINGW64&& C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/Mai/.zcode/workspace/default/im-system && cmake --build client/cmake-build-win && /mingw64/bin/windeployqt.exe --release client/cmake-build-win/im_client.exe && bash client/fix_runtime_dlls.sh"
# fix_runtime_dlls.sh 必跑：windeployqt 不复制编译器运行时与 protobuf/abseil DLL（坑 19），
# 脚本幂等，扫 exe+全部 DLL 的导入闭包从 /mingw64/bin 补齐到 exe 目录
# 运行：双击项目根目录 IM即时通讯.lnk（或 client\cmake-build-win\im_client.exe，已自包含）
# 服务器地址内置 47.112.192.119:9000，im_client.exe 不需要任何命令行参数
# 注意：MSYS2 protobuf 36 需 CONFIG 模式 + 自查 protoc（CMakeLists 已适配，详见 env.md）

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
10. **pkill -x 杀不掉超 15 字符的进程名**（comm 截断）：`message_storage_server` 实际 comm 是 `message_storage`，`pkill -x` 漏杀导致旧进程占住 10004 端口、新实例起不来（M8 事故）。停服务一律 `pkill -f "build/server/<服务名>/"`。
11. httplib 需要全局统一启用 OpenSSL：根 CMakeLists 的 `add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)`，否则不同编译单元宏不一致会构成 ODR 违规（M8 起百度 ASR 走 HTTPS）。
12. **本机 PowerShell 改写 UTF-8 文件会转成 GBK**（`Set-Content` 用系统 ANSI 码页），之后 Edit 工具按 GBK 保留，传到服务器后中文全部失配。`scripts/*.sh` 一律 ASCII-only（窗口名等中文模式改用窗口 id/ASCII 子串匹配）；本地改动入库前可用 `od -An -tx1 | grep 'bb e1'` 之类抽查字节。客户端 .cc 均为 UTF-8 未受影响。
13. **ssh 远程命令里的 `$var` 不需要转义**（cmd 不展开 `$`；写成 `\$var` 远端会收到字面量）。同因 `echo EXIT=\$?` 会打出字面量。
14. `pkill -f <模式>` 会匹配**含该字符串的 ssh 远程命令自身**（bash -c 的 cmdline 里有同样文本），直接杀死会话且无任何输出；`[t]` 方括号技巧也救不了（命令行其他位置还有该串）。杀进程用 `pkill -x <comm>`（≤15 字符的进程名）或先起后杀分两条命令。
15. 无头 GUI 验证三件套：Xvfb（虚拟显示）+ **openbox**（无 WM 时 xdotool windowactivate 无效、Qt 的 isActiveWindow() 恒真，未读计数等焦点用例必须起 WM）+ xdotool（坐标点击/键入，截图用 `import -window root`）。xdotool 报告的窗口 Y 与 Qt 客户区原点在 openbox 下有固定差，控件偏移需按截图实测标定（gui_test.sh 内有注释）。
16. 客户端 token 持久化在 `~/.config/im-system/im-client.conf`（QSettings），启动时 GET_USER_INFO 校验通过则跳过登录页——自动化测试想回到登录页要先删该文件。Windows 侧同机制，文件在 `%APPDATA%\im-system\im-client.conf`。
17. **Qt 控件级 inline 样式表会「吃掉」全局 QSS 的 objectName 规则**：`widget->setStyleSheet("background:#FFF")` 这类无选择器规则作用于该 widget 及**全部子孙**，且优先级高于 app 级 QSS——曾把 `#primary` 绿色主按钮盖成白底白字「隐身」（现象：按钮位置空白但点击有效）。子树背景一律用 objectName 规则（`QWidget#inputPanel{...}`）+ `setAttribute(Qt::WA_StyledBackground)`，禁用 inline 样式表。
18. **GUI 界面验证工作流**：服务器 Xvfb 截图初检 → 发现疑点用「裁图放大 + 点击实验」定位（例：按钮看不见时，往输入框打字点空白槽位，消息上屏即证明按钮存在只是没画出来）→ 修复后**必须重启客户端再截图**（ChatWindow 关闭只是 hide，旧进程截图会误导）。每次改 UI 后窗口尺寸/控件位置会变，gui_test.sh 的偏移量要按截图重标定。
19. **windeployqt 产物不自包含**：它不复制编译器运行时（libgcc_s_seh-1/libstdc++-6/libwinpthread-1）和 protobuf/abseil 系 DLL（exe 直接导入，只有 exe 依赖它们），且二次运行因「已是最新」全部跳过。症状：双击报「找不到 xxx.dll」或静默退出（退出码 0xC0000135）。此前"可直接双击"是因启动环境 PATH 恰好含 MSYS2，换环境即挂。补齐脚本 `client/fix_runtime_dlls.sh`（ASCII-only）：扫 exe+全部 DLL 的导入表，从 /mingw64/bin 递归补齐到 exe 目录直到收敛；结果里 UNRESOLVED 的 `api-ms-win-*` 是系统虚拟 API 集，属正常。
20. **PowerShell UIAutomation 可驱动 Qt 客户端做 Windows 实机 GUI 验证**：Qt 控件名（如「退出登录」按钮）可被 UIA 识别，按钮 InvokePattern.Invoke、输入框 ValuePattern.SetValue，无需坐标点击；配合截图可全自动走「登录→退出→重登」链路。注意：通过 -Command 传中文串安全（命令行是 UTF-16），但 .ps1 文件内容必须 ASCII-only（同坑 12）。

## 八、会话内决策记录（增量）

- 服务器 MySQL 密码 im123456 / Redis 无密码 / RabbitMQ guest——仅开发环境，M10 时改。
- 新增第 8 张表 `group_event`（群聊事件，原 7 表无处存放"被拉入群"事件），已入 sql/entities/文档。
- 群聊事件采用"消费即已读"语义（GetPendingEvents 返回未读并标记）。
- 验证码 Redis：`sms:code:{phone}` TTL 300s + `sms:limit:{phone}` 60s 限发；登录态 `auth:token:{token}` TTL 7 天；在线状态 `online:{uid}`=网关实例 id（WS 绑定时写、断开删）。
- MQ topic 交换机 `im.events`：routing key `message.new` / `friend.apply` / `group.event`；网关每节点独占队列 `gateway.<instance_id>` 绑 `#`。
- 百度 ASR / 阿里云短信密钥均未配置（走开发模式：ASR 调不通静默跳过、短信只打日志），接入点已留好。
- M8 决策：speech_server 未配置密钥时返回**占位转写**「（开发模式）语音消息」（success=true），保证语音链路 asr_text 可验收——与短信固定码同一思路；真实密钥写入 `conf/speech_server.local.flags`（模板 `conf/speech_server.local.flags.example`）后重启即切正式识别，零代码改动。
- M8 实现要点：百度 REST 客户端在 `server/speech_server/baidu_asr.hpp`（token 缓存 + 极简 JSON 解析 + WAV 采样率探测），`--baidu_dev_pid` 默认 1537（普通话有标点 16k）；网关新增 `REQ_TYPE_SPEECH_RECOGNITION`(type=40) 直达路由；语音服务无状态，不连 MySQL/Redis/MQ。
- M9 决策：client/ 为**独立 CMake 工程**（只依赖 Qt6+protobuf，不链 brpc/ODB/im_common）；protobuf 用 find_package（服务器上配 `-DCMAKE_PREFIX_PATH=/usr/local` 锁 3.20.2）；Qt 基线 6.2.4（jammy apt，无 QtProtobuf，客户端 pb 用 protoc 生成 C++）；开发模式固定码 666666 补进 user_server（注册+短信登录双入口，PLAN 既定设计）。
- M9 第二轮决策（2026-10-07）：语音消息**只传文件不传转写**——客户端上传 WAV 后按 VOICE 类型 TRANSMIT，ASR 由 message_server 在持久化前调用 speech_server 完成（asr_text 随消息入库与推送），客户端不做语音识别；录音格式 16k/单声道/16bit PCM WAV（WavRecorder 手工拼 RIFF 头），与百度短语音识别对齐。
- M9 第二轮：图片消息渲染走 QTextDocument::addResource（`<img src="imimg:<file_id">`），下载异步、不把 base64 灌进 HTML；文件/图片/语音气泡用自定义协议锚点（imsave:/imgsave:/implay:）+ anchorClicked 分发。
- M9 第二轮：WS 推送在线时自己发的消息**以广播回显上屏**（message_id 去重）；仅当推送未连通才用 MsgTransmitResp 本地补显，避免双份。
- M9 第二轮：未读计数在客户端内存维护（服务端无已读模型）；会话窗聚焦/打开即清零，不为离线消息补未读（拉历史可见）。
- 双客户端验证用测试工具 `client/test/dual_client_push.cc`：40 项断言覆盖注册→加好友→四类消息→推送→离线补历史；兼作造数据工具（--send/--send-many/--send-image/--set-avatar，密码固定 pass123）。
- 已知问题移交 M10：message 表同秒排序不稳（见第五节待办）；本地 Windows → GitHub 直连不通，push 走服务器（remote 已带 PAT）。
- M9 第三轮决策（2026-10-07，用户反馈驱动）：①登录改「账号密码主入口 + 验证码登录 + 注册」三标签（服务端零改动，协议本就双方式）；②界面微信风格：全局 QSS 主题（theme.h）+ 聊天窗从 QTextBrowser/HTML 改为 **QListWidget+setItemWidget 真实控件行**（气泡=QLabel/QFrame + objectName 样式，头像=avatar.h 首字生成，图片异步下载后 rebuild_row 就地替换）。
- M9 第三轮：消息行渲染改控件后，历史分页的「视口保持」改在 QListWidget 上实现（prepend 时记录 scrollbar value/maximum，插行后按增量补偿）；贴底滚动用 QTimer::singleShot(0) 等 sizeHint 落定。
- M9 第三轮：Windows 交付链路固定为「cmake --build → windeployqt --release」两步，产物自包含可直接双击；用户反馈迭代时在 Windows 侧直接重建，服务器侧仅作协议回归（client_smoke + dual_client_push）。
- M9 第四轮决策（2026-10-07，用户反馈驱动）：①退出登录**不退出进程**，改为登录页/主窗口两函数互相切换、窗口均 WA_DeleteOnClose 循环重建（重新登录可能是另一个账号，主窗/聊天窗状态必须全新）；登出即 disconnect_push + 删本地 token，重登后 connect_push 以新 token 重新鉴权。②部署链路固定为「cmake --build → windeployqt → fix_runtime_dlls.sh」三步（windeployqt 不复制运行时依赖，见坑 19）。③服务器地址视为内置配置（默认 47.112.192.119:9000，登录页可改持久化），客户端不提供命令行参数入口。
