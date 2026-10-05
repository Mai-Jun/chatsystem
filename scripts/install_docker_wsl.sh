#!/usr/bin/env bash
# ============================================================
# 在 WSL Ubuntu 22.04 内安装 docker engine（无 Docker Desktop 方案）
# 用法（root）: sudo bash scripts/install_docker_wsl.sh
# 可选: DOCKER_MIRROR=Aliyun   # 国内走阿里云镜像源（默认 Aliyun，置空走官方源）
# ============================================================
set -euo pipefail

DOCKER_MIRROR="${DOCKER_MIRROR:-Aliyun}"
log() { printf '\033[32m[docker]\033[0m %s\n' "$*"; }

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行: sudo bash scripts/install_docker_wsl.sh"; exit 1; }

# 1) 启用 WSL systemd（docker 服务需要）
if ! grep -q '^systemd=true' /etc/wsl.conf 2>/dev/null; then
  mkdir -p /etc
  {
    echo '[boot]'
    echo 'systemd=true'
    echo ''
    echo '[user]'
    echo 'default=root'
  } >> /etc/wsl.conf
  log "已写入 /etc/wsl.conf 启用 systemd —— 安装完需要重启 WSL（PowerShell: wsl --shutdown）"
fi

# 2) 安装 docker engine + compose 插件
if ! command -v docker >/dev/null 2>&1; then
  log "通过 get.docker.com 安装 docker engine（mirror=$DOCKER_MIRROR）..."
  if [ -n "$DOCKER_MIRROR" ]; then
    curl -fsSL https://get.docker.com | sh -s -- --mirror "$DOCKER_MIRROR"
  else
    curl -fsSL https://get.docker.com | sh
  fi
else
  log "docker 已安装: $(docker --version)"
fi

# 3) 启动并自启（systemd 生效后）
if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl enable --now docker || log "systemctl 启动失败——若刚改过 wsl.conf，请先重启 WSL 后重跑本脚本"
fi

docker --version
docker compose version || true
log "完成"
