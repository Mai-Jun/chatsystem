#!/bin/bash
# M9 GUI 无头验证：Xvfb 虚拟显示 + im_client + xdotool 驱动 + 截图
# 用法: bash gui_test.sh <阶段> [手机号] [密码]
#   1 = 启动并截登录页
#   2 = 登录并截主窗口
#   3 = 打开会话并截聊天窗
#   4 = 发消息 / 翻历史
set -u
export DISPLAY=:99
SHOTS=/root/gui_shots
mkdir -p "$SHOTS"

PHONE="${2:-19353589846}"
PASSWORD="${3:-pass123}"
PEER="${4:-18353589846}"   # 对端手机号（阶段 5/6 由它发消息）

# 登录窗几何（阶段 1 实测）：440,260 起，380x300
LOGIN_PHONE_XY="640 316"
LOGIN_PASS_XY="640 345"

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

do_login() {
  xdotool search --name "IM 即时通讯" windowactivate --sync %@ 2>/dev/null
  sleep 1
  xdotool mousemove $LOGIN_PHONE_XY click 1
  sleep 1
  xdotool type --delay 60 "$PHONE"
  sleep 1
  xdotool mousemove $LOGIN_PASS_XY click 1
  sleep 1
  xdotool type --delay 60 "$PASSWORD"
  sleep 1
  xdotool key Return
  sleep 6
}

case "${1:-1}" in
1)
  start_app
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -10
  echo "--- app log ---"
  cat /root/im_client_gui.log
  shot 1_login
  ;;
2)
  do_login
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -10
  echo "--- geometry ---"
  xdotool search --name "IM -" getwindowgeometry %@ 2>/dev/null
  echo "--- app log ---"
  cat /root/im_client_gui.log
  shot 2_main
  ;;
3)
  # 主窗口会话列表：双击第一项打开聊天窗（列表首项实测 y≈75）
  xdotool search --name "IM -" windowactivate --sync %@ 2>/dev/null
  sleep 1
  xdotool mousemove 200 75 click --repeat 2 --delay 120 1
  sleep 5
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -10
  echo "--- geometry ---"
  xdotool search --name "会话 -" getwindowgeometry %@ 2>/dev/null
  shot 3_chat
  ;;
4)
  # 在聊天窗输入框发一条文本（聊天窗实测 350,100 起，560x620，输入框 y≈698）
  xdotool search --name "会话 -" windowactivate --sync %@ 2>/dev/null
  sleep 1
  xdotool mousemove 700 698 click 1
  sleep 1
  xdotool type --delay 60 "M9-GUI-$(date +%H%M%S)"
  sleep 1
  xdotool key Return
  sleep 5
  shot 4_sent
  echo "--- app log ---"
  tail -20 /root/im_client_gui.log
  ;;
5)
  # 聊天窗保持聚焦，由对端（B）发消息 → 应实时上屏且未读为 0
  TEXT="LIVE-$(date +%H%M%S)"
  /root/chatsystem/client/build-server/dual_client_push 127.0.0.1 9000 9001 --send "$PEER" "$PHONE" "$TEXT"
  sleep 4
  shot 5_live_focused
  echo "[sent] $TEXT"
  ;;
6)
  # 主窗口聚焦（聊天窗失焦），对端再发一条 → 会话列表应出现未读计数
  xdotool search --name "IM -" windowactivate --sync %@ 2>/dev/null
  sleep 2
  TEXT="UNREAD-$(date +%H%M%S)"
  /root/chatsystem/client/build-server/dual_client_push 127.0.0.1 9000 9001 --send "$PEER" "$PHONE" "$TEXT"
  sleep 4
  shot 6_unread
  echo "[sent] $TEXT"
  ;;
*)
  echo "unknown stage"
  ;;
esac
echo "--- done ---"
