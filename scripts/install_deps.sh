#!/usr/bin/env bash
# ============================================================
# im-system M0 依赖安装脚本（WSL2 / Ubuntu 22.04）
#
# 用法（root）: sudo bash scripts/install_deps.sh
# 可选环境变量:
#   GH_PROXY=https://ghfast.top     # GitHub 下载加速前缀（国内网络慢时设置）
# ============================================================
set -euo pipefail

GH_PROXY="${GH_PROXY:-}"
url() { if [ -n "$GH_PROXY" ]; then echo "${GH_PROXY%/}/$1"; else echo "$1"; fi; }
log() { printf '\033[32m[deps]\033[0m %s\n' "$*"; }
die() { printf '\033[31m[deps]\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "请用 root 运行: sudo bash scripts/install_deps.sh"
# shellcheck disable=SC1091
. /etc/os-release
[ "${VERSION_ID:-}" = "22.04" ] || log "警告: 当前系统 ${PRETTY_NAME:-unknown} 非预期（脚本针对 Ubuntu 22.04 编写，与部署服务器保持一致）"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

export DEBIAN_FRONTEND=noninteractive
log "apt 安装编译依赖..."
apt-get update -y
apt-get install -y \
  build-essential cmake git pkg-config curl ca-certificates unzip \
  libssl-dev zlib1g-dev \
  libprotobuf-dev protobuf-compiler \
  libgflags-dev libspdlog-dev libgtest-dev nlohmann-json3-dev \
  libleveldb-dev \
  libboost-dev libboost-system-dev libboost-thread-dev libboost-filesystem-dev \
  libboost-regex-dev libboost-date-time-dev libboost-chrono-dev libboost-random-dev \
  libwebsocketpp-dev \
  librabbitmq-dev \
  libhiredis-dev \
  default-libmysqlclient-dev

log "尝试 apt 安装 ODB（MySQL ORM）..."
if apt-get install -y odb libodb-dev libodb-mysql-dev >/dev/null 2>&1; then
  log "ODB（apt）安装成功"
else
  log "apt 源没有 ODB 包 → M1 前需源码编译安装，步骤已记录到 docs/env.md（不阻塞 M0）"
fi

if [ ! -f /usr/local/lib/libbrpc.a ] && [ ! -f /usr/local/lib64/libbrpc.a ]; then
  log "源码安装 brpc 1.9.0（首次编译 20~40 分钟，属正常）..."
  rm -rf /tmp/brpc
  git clone --depth 1 --branch 1.9.0 "$(url https://github.com/apache/brpc.git)" /tmp/brpc
  cmake -S /tmp/brpc -B /tmp/brpc/build -DCMAKE_BUILD_TYPE=Release
  cmake --build /tmp/brpc/build -j"$(nproc)"
  cmake --install /tmp/brpc/build
  ldconfig
  log "brpc 安装完成"
else
  log "brpc 已安装，跳过"
fi

if [ ! -f /usr/local/lib/libredis++.a ]; then
  log "源码安装 redis-plus-plus..."
  rm -rf /tmp/redis-plus-plus
  git clone --depth 1 "$(url https://github.com/sewenew/redis-plus-plus.git)" /tmp/redis-plus-plus
  cmake -S /tmp/redis-plus-plus -B /tmp/redis-plus-plus/build \
    -DCMAKE_BUILD_TYPE=Release -DREDIS_PLUS_PLUS_CXX_STANDARD=17
  cmake --build /tmp/redis-plus-plus/build -j"$(nproc)"
  cmake --install /tmp/redis-plus-plus/build
  ldconfig
  log "redis-plus-plus 安装完成"
else
  log "redis-plus-plus 已安装，跳过"
fi

log "下载 cpp-httplib 单头文件（v0.15.3 → third_party/）..."
mkdir -p "$REPO_ROOT/third_party"
curl -fsSL "$(url https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h)" \
  -o "$REPO_ROOT/third_party/httplib.h"

log "==== 版本汇总 ===="
g++ --version | head -1
cmake --version | head -1
protoc --version
if command -v docker >/dev/null 2>&1; then
  log "docker: $(docker --version)"
else
  log "提示: WSL 内没有 docker 命令 —— 若 Windows 上也没装 Docker Desktop，请先安装（见 docs/env.md），否则无法启动基础设施"
fi
log "全部完成"
