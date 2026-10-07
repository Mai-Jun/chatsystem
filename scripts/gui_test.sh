#!/bin/bash
# M9 GUI 无头验证：Xvfb 虚拟显示 + im_client + xdotool 驱动 + 截图
# 用法: bash gui_test.sh <阶段>
#   1 = 启动并截登录页
#   2 = 登录并截主窗口
#   3 = 打开会话并截聊天窗
#   4 = 发消息 + 翻历史
set -u
export DISPLAY=:99
SHOTS=/root/gui_shots
mkdir -p "$SHOTS"

shot() { import -window root "$SHOTS/$1.png"; echo "[shot] $1"; }

start_app() {
  pkill -x im_client 2>/dev/null
  pkill -x Xvfb 2>/dev/null
  sleep 1
  Xvfb :99 -screen 0 1280x900x24 >/tmp/xvfb.log 2>&1 &
  sleep 3
  cd /root/chatsystem || exit 1
  nohup ./client/build-server/im_client >/root/im_client_gui.log 2>&1 &
  sleep 8
}

case "${1:-1}" in
1)
  start_app
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -10
  echo "--- geometry ---"
  xdotool search --name "IM 即时通讯" getwindowgeometry %@ 2>/dev/null
  echo "--- app log ---"
  cat /root/im_client_gui.log
  shot 1_login
  ;;
2)
  # 登录：手机号 + 密码 + 点登录
  xdotool search --name "IM 即时通讯" windowactivate --sync %@ 2>/dev/null
  sleep 1
  # 坐标由阶段 1 的截图确定，写在下面
  ;;
*)
  echo "unknown stage"
  ;;
esac
echo "--- done ---"
