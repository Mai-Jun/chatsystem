# 换机迁移清单（2026-10-08 主力机切换；2026-10-07 M9 第四轮后按现状重写）

> 背景：M0~M9 在旧机开发，模式为「旧机 Windows 编辑 + 阿里云服务器编译/运行 + MSYS2 构建
> Windows 客户端」。10-08 起切换到新机继续开发。
> **换机不丢任何项目数据**：代码与全部运行状态（7 个子服务、docker 四件套、MySQL/Redis 数据）
> 都在服务器上；新机要重建的只有「开发工具链」和「Windows 客户端产物」。
> 后端环境已脚本化（`scripts/install_deps.sh`），迁移成本 ≈ 1~2 小时（主要是下载与 brpc 编译）。

## 零、资产在哪张表（先读，避免恐慌）

| 资产 | 位置 | 换机后 |
|---|---|---|
| 全部代码 | GitHub `Mai-Jun/chatsystem`（私有）+ 服务器 `/root/chatsystem`（**最完整副本**，HEAD=`9acee19`，M9 第四轮） | 服务器原样健在；新机 clone 即得 |
| 运行中的 7 个子服务 + docker 四件套 + 数据（含测试账号 A/B 的 68 条会话） | 服务器 | 原样健在，与换机无关 |
| Windows 客户端 `im_client.exe` / 根目录快捷方式 / 登录态 QSettings | 旧机 | ✅ **已于 2026-10-08 在新机重建**（三步构建 + 快捷方式，回归全过）；登录态无需迁移（重登即可） |
| 服务器 SSH 私钥 `~/.ssh/im_dev_key` | 旧机 | ✅ 新机已换新密钥（`~/.ssh/id_ed25519`，2026-10-08 已入 authorized_keys） |
| GitHub PAT | 旧机 `docs/github_pat.local`（gitignore，**不随 clone 走**）+ 各端 remote URL 内嵌 | ✅ 新机已从服务器 remote URL 取证并重建 `docs/github_pat.local` |
| 云服务密钥（百度 ASR/阿里云短信） | 服务器 `conf/*.local.flags`（未配置则开发模式旁路） | 原样健在 |

## 一、旧机收尾状态（2026-10-07 已完成）

- [x] M0~M9 全部提交：本地 main = 服务器 `/root/chatsystem` = `9acee19`
- [x] 旧机到 GitHub 直连不通（实测）：本地 push 失败，走 bundle+scp 同步服务器（流程见 HANDOFF 第六节）
- [x] **新机的第一件事（✅ 2026-10-08 完成）**：配好带 PAT 的 remote 后 `git push origin main`，让 GitHub 追平。
      新机同样直连不通，最终在服务器侧 `git config http.version HTTP/1.1` 后重试成功（`706e2b0..63a6b38`）；
      三端 HEAD 已对齐，此后 push 走「服务器侧代推」为主通道

## 二、新机初始化（按序执行）—— ✅ 已于 2026-10-08 全部完成，勾选留档

1. **SSH 通服务器**（一切的前提）：✅ 首次用密码经 paramiko 把新机 `~/.ssh/id_ed25519.pub`
   追加进服务器 `/root/.ssh/authorized_keys` → 免密验证通过
2. **取 PAT**（否则 clone 不了私有仓库，此处有先有鸡的死循环，用服务器破局）：✅
   `ssh root@47.112.192.119 "git -C /root/chatsystem remote -v"` → URL 里
   `https://<PAT>@github.com/...` 的 PAT 段就是；或直接向用户索取（用户有明文备份）
3. **clone + 配 remote**：✅ clone 走 gh-proxy（新机直连 GitHub 也不通），随后
   `git fetch ssh://root@47.112.192.119/root/chatsystem main` 追平服务器领先提交（推荐路径）；
   PAT 明文已存 `docs/github_pat.local`（已被 `*.local` 规则忽略；**绝不写进任何 git
   跟踪文件**，push protection 会拒推，2026-10-07 实测）→ `git push origin main` 见第一节
4. **服务器侧开发环境（二选一，按需）**：✅ 选 **A 纯 ssh 到服务器开发**（旧机 M4~M9 的实际模式）：
   新机有 git+ssh 就够了，零安装；编译/起服务/测试命令全部见 HANDOFF 第六节；
   （B 本地 WSL2 未装，env.md 早期规划保留备用：`wsl --install -d Ubuntu-22.04` →
   `bash scripts/install_deps.sh`；**protobuf 3.20.2 与 brpc 1.9.0 是锁定组合不得漂移**）
5. **Windows 客户端工具链**：✅ 按 env.md 坑位条「Windows 侧构建（MSYS2）」从零安装
   （TUNA 20260927 sfx 42MB 解压 + pacman 装 qt6-base/websockets/multimedia/protobuf/
   cmake/ninja/gcc/binutils/gdb，mirrorlist.mingw/msys 已插 TUNA）→ HANDOFF 第六节三步构建
   （`cmake --build` → `windeployqt` → `fix_runtime_dlls.sh`，第三步必跑见坑 19）→ 根目录快捷方式已重建
   → 验收：`client_smoke.exe 47.112.192.119 9000 9001` 8/8 + `dual_client_push` 40/40 + GUI 启动冒烟 全过
6. 回到 HANDOFF.md「五、待办」从 M9 真机手测继续（测试账号：A `19353589846`/pass123、
   B `18353589846`/pass123，服务器数据都在）→ **当前位置**

## 本机下载物清理台账（防止遗忘）

> 背景：M0 阶段这台机器 WSL 因系统组件损伤不可用，改用 VirtualBox 虚拟机方案（im-dev，8核8G/60G）。

| 文件 | 状态 | 说明 |
|---|---|---|
| `C:\WSL\jammy-wsl.rootfs.tar.gz` | ✅ 已删除（2026-10-06） | WSL 专用 rootfs，VirtualBox 方案用不上；新机将重新下载 |
| `C:\WSL\ubuntu-22.04.5-live-server-amd64.iso`、`VirtualBox-*.exe`、`PortableGit.7z.exe`、bundle、临时脚本 | ✅ 已删除（2026-10-06 夜间清扫） | |
| VirtualBox 虚拟机 im-dev（注册+磁盘+目录） | ✅ 已删除（unregistervm --delete） | |
| VirtualBox **程序本体** | 待卸载（需管理员，留待用户） | 控制面板卸载即可 |
| `C:\WSL\plink.exe`、`C:\PortableGit\`、`~/.ssh/im_dev_key` | **暂保留**（旧机退役时随旧机处置；私钥若留应删除，新机已换新密钥） | 旧机专用工具，新机用系统 git/ssh 即可 |
| `C:\PortableGit\` | 项目结束时可删 | Windows 侧便携 git |
| `C:\WSL\im-dev\` + `C:\Users\Mai\VirtualBox VMs\im-dev\` | 保留 | 虚拟机磁盘与配置；换新机时整目录可迁移或重建 |
| Windows 已启用的功能（虚拟机平台/容器/hypervisor=auto） | 保留无害 | 新机无需复制；这台机器日后想用 WSL 需先修复系统组件 |

## 服务器（47.112.192.119）清理与变更台账（2026-10-06，开发期持续维护）

**原则：服务器上所有项目期安装的东西都在这张表里；项目结束/迁回本地后按表清理。虚拟机类环境删除即消失。**

### 已执行的清理

| 动作 | 对象 | 说明 |
|---|---|---|
| ✅ 已删除 | `/home/mai/messagequeue-ubuntu20.44`（1.7G） | 旧项目环境目录 |
| ✅ 已停止 | mai 的 vscode-server 进程 | 重连 VSCode 会自动再起 |
| ✅ 已停止+禁用 | fwupd 服务 | 固件更新，服务器无用 |
| ✅ 已停止（长期如此） | mysql（native，disabled）、OJ 相关服务 | **/var/lib/mysql 数据原样保留** |
| 保留未删 | `/home/mai/messagequeue`（77M）、`cpp-OJ-vibe_coding`（14M）、`oj-data`（233M） | 旧代码/数据，确认不要可 `rm -rf` |

### 项目期新增（结束时清理）

| 对象 | 说明 |
|---|---|
| `/root/chatsystem/` | 本项目仓库（结束时可删或归档） |
| `/root/im-system.bundle`、`/root/deps_install.log`、`/root/docker_install.log` | 传输与安装日志（可删） |
| apt 包：build-essential/cmake/libprotobuf-dev/libodb-*/libgtest-dev/libspdlog-dev/libgflags-dev/libwebsocketpp-dev/librabbitmq-dev/libhiredis-dev 等 | `apt autoremove` 级清理 |
| `/usr/local`：libbrpc.a、redis-plus-plus、（原有 protobuf 3.20.2 保留——项目前就存在） | 源码安装 |
| docker + /etc/docker/daemon.json（mirror 配置）+ 镜像 mysql/redis/rabbitmq/etcd | `docker compose down` + 卸载 |
| 2G swap（`/swapfile`） | 项目前已存在，非本项目创建 |

### 本机（Windows）遗留

| 对象 | 状态 |
|---|---|
| `C:\WSL\jammy-wsl.rootfs.tar.gz` | ✅ 已删除（2026-10-06，WSL 弃用） |
| `C:\WSL\ubuntu-22.04.5-live-server-amd64.iso`、`C:\WSL\im-system.bundle`、`C:\WSL\plink.exe`、`C:\WSL\*.txt` 脚本 | 项目收口时可删 |
| `C:\WSL\VirtualBox-7.2.20-175154-Win.exe`、`C:\WSL\PortableGit.7z.exe` | 可删（已装完/解压） |
| `C:\PortableGit\` | Windows 侧 git，项目结束可删 |
| `C:\Users\Mai\.ssh\im_dev_key(.pub)` | 服务器 SSH 密钥，项目结束可删 |
| VirtualBox 程序 + `C:\WSL\im-dev\` + `C:\Users\Mai\VirtualBox VMs\im-dev\` | 已弃用，可整体卸载/删除 |
| Windows 功能（虚拟机平台/容器已启用，hypervisor=off） | 想恢复 WSL 时：`bcdedit /set {current} hypervisorlaunchtype auto` + 修复系统组件 |

## 注意事项

- 密钥类（百度 ASR、阿里云短信、数据库密码）：只存在于 conf/*.local.flags 与个人配置中，**不入 git**，换机时手动带走/重建；
- **GitHub PAT 同理**：`docs/github_pat.local` 被 `*.local` 忽略、不随 clone 走，新机取证见第二节第 2 步；raw PAT 绝不写入 git 跟踪文件（push protection 拒推）；
- Docker 卷（MySQL/ES 数据）不随 git 走：M0 阶段可直接丢弃，M2 起有真实数据后用 `sql/` 脚本重建；**当前真实数据全在服务器上，与换机无关**；
- 新机 BIOS 需开启 CPU 虚拟化（VT-x / SVM），Windows 功能"虚拟机平台"由 `wsl --install` 自动启用。
