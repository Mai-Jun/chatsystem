# 换机迁移清单（2026-10-08 主力机切换）

> 背景：M0 阶段（10-05~10-07）在当前机器开发，10-08 起切换到另一台电脑。
> 项目的环境搭建已全部脚本化，迁移成本 = 以下清单按序执行一遍（约 1~2 小时，主要是下载与 brpc 编译）。

## 旧机收尾（离开前完成）

- [ ] M0 全部完成（脚本已在旧机验证通过，env.md 版本已回填）
- [ ] GitHub 远程仓库已建立，本地全部提交并 push（`git status` 干净）
- [ ] 如有未推内容：`git push -u origin main`

## 新机初始化（Windows 11 建议 25H2 亦可）

1. 安装 WSL2 + Ubuntu 22.04：
   - 管理员 PowerShell：`wsl --install -d Ubuntu-22.04`
   - 重启 → Ubuntu 终端设置用户名/密码
2. 把仓库 clone 到工作区（或 `git clone` 到 WSL home，编译更快）：
   - `wsl -d Ubuntu-22.04 -- git clone <仓库地址> /mnt/c/<路径>/im-system`
3. 安装编译依赖（约 20~40 分钟，含 brpc 源码编译）：
   - `wsl -d Ubuntu-22.04 -u root -- bash /mnt/c/<路径>/im-system/scripts/install_deps.sh`
   - 国内网络可先 `export GH_PROXY=https://ghfast.top`
4. 启动基础设施并验证：
   - `docker compose -f docker/docker-compose.dev.yml up -d`（WSL 内无 docker 时先装 Docker Desktop 或 docker engine，见 env.md）
   - `bash scripts/verify_infra.sh` 全绿
5. 编译冒烟测试：`cmake -S . -B build && cmake --build build && ctest --test-dir build`
6. 对照 `docs/env.md` 回填新机版本号，确认与旧机一致（brpc 1.9.0 / protobuf 3.12 为锁定组合，不得漂移）

## 本机下载物清理台账（防止遗忘）

> 背景：M0 阶段这台机器 WSL 因系统组件损伤不可用，改用 VirtualBox 虚拟机方案（im-dev，8核8G/60G）。

| 文件 | 状态 | 说明 |
|---|---|---|
| `C:\WSL\jammy-wsl.rootfs.tar.gz` | ✅ 已删除（2026-10-06） | WSL 专用 rootfs，VirtualBox 方案用不上；新机将重新下载 |
| `C:\WSL\ubuntu-22.04.5-live-server-amd64.iso`、`VirtualBox-*.exe`、`PortableGit.7z.exe`、bundle、临时脚本 | ✅ 已删除（2026-10-06 夜间清扫） | |
| VirtualBox 虚拟机 im-dev（注册+磁盘+目录） | ✅ 已删除（unregistervm --delete） | |
| VirtualBox **程序本体** | 待卸载（需管理员，留待用户） | 控制面板卸载即可 |
| `C:\WSL\plink.exe`、`C:\PortableGit\`、`~/.ssh/im_dev_key` | **暂保留**（推送/服务器访问在用） | 项目收口时删 |
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
- Docker 卷（MySQL/ES 数据）不随 git 走：M0 阶段可直接丢弃，M2 起有真实数据后用 `sql/` 脚本重建；
- 新机 BIOS 需开启 CPU 虚拟化（VT-x / SVM），Windows 功能"虚拟机平台"由 `wsl --install` 自动启用。
