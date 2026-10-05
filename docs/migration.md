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

## 注意事项

- 密钥类（百度 ASR、阿里云短信、数据库密码）：只存在于 conf/*.local.flags 与个人配置中，**不入 git**，换机时手动带走/重建；
- Docker 卷（MySQL/ES 数据）不随 git 走：M0 阶段可直接丢弃，M2 起有真实数据后用 `sql/` 脚本重建；
- 新机 BIOS 需开启 CPU 虚拟化（VT-x / SVM），Windows 功能"虚拟机平台"由 `wsl --install` 自动启用。
