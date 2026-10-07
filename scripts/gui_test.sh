#!/bin/bash
# M9 GUI 无头验证：Xvfb + openbox + im_client + xdotool 驱动 + 截图
# 用法: bash gui_test.sh <阶段> [手机号] [密码] [对端手机号]
#   1 = 启动并截登录页
#   2 = 登录并截主窗口
#   3 = 打开会话并截聊天窗
#   4 = 在聊天窗发一条文本
#   5 = 对端发消息（聊天窗存在）
#   6 = 主窗口聚焦后对端发消息（未读计数）
#   7 = 关掉聊天窗后对端发消息（未读计数，不依赖焦点）
#   8 = 重新打开会话（未读清零）
#   9 = 对端连发 55 条（分页压测）
#  10 = 打开会话看首屏分页状态
#  11 = 点「加载更多」翻页
#
# 说明：无窗口管理器时 xdotool windowactivate 无效（isActiveWindow 恒真），
# 焦点相关用例必须先起 openbox；窗口坐标一律按窗口几何 + 偏移计算，
# 避免被 WM 的标题栏/最大化改变位置后失效。
set -u
export DISPLAY=:99
SHOTS=/root/gui_shots
BIN=/root/chatsystem/client/build-server
mkdir -p "$SHOTS"

PHONE="${2:-19353589846}"
PASSWORD="${3:-pass123}"
PEER="${4:-18353589846}"

shot() { import -window root "$SHOTS/$1.png"; echo "[shot] $1"; }

# 按窗口名取几何到 X/Y/WIDTH/HEIGHT
geom() { eval "$(xdotool search --name "$1" getwindowgeometry --shell %@ 2>/dev/null)"; }

start_app() {
  pkill -x im_client 2>/dev/null
  pkill -x openbox 2>/dev/null
  pkill -x Xvfb 2>/dev/null
  sleep 1
  Xvfb :99 -screen 0 1280x900x24 >/tmp/xvfb.log 2>&1 &
  sleep 3
  if command -v openbox >/dev/null; then
    openbox >/tmp/openbox.log 2>&1 &
    sleep 2
  fi
  cd /root/chatsystem || exit 1
  nohup ./client/build-server/im_client >/root/im_client_gui.log 2>&1 &
  sleep 8
}

do_login() {
  geom "IM 即时通讯"
  xdotool windowactivate --sync "$(xdotool search --name 'IM 即时通讯' | head -1)" 2>/dev/null
  sleep 1
  xdotool mousemove $((X + 200)) $((Y + 56)) click 1   # 手机号输入框
  sleep 1
  xdotool type --delay 60 "$PHONE"
  sleep 1
  xdotool mousemove $((X + 200)) $((Y + 85)) click 1   # 密码输入框
  sleep 1
  xdotool type --delay 60 "$PASSWORD"
  sleep 1
  xdotool key Return
  sleep 6
}

# 双击会话列表首项打开聊天窗
open_session() {
  geom "IM -"
  xdotool mousemove $((X + 200)) $((Y + 57)) click --repeat 2 --delay 120 1
  sleep 5
}

peer_send() { "$BIN/dual_client_push" 127.0.0.1 9000 9001 --send "$PEER" "$PHONE" "$1"; }

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
  geom "IM -"
  echo "main window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  echo "--- app log ---"
  cat /root/im_client_gui.log
  shot 2_main
  ;;
3)
  open_session
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -10
  geom "会话 -"
  echo "chat window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  shot 3_chat
  ;;
4)
  geom "会话 -"
  xdotool mousemove $((X + 350)) $((Y + 598)) click 1   # 输入框
  sleep 1
  xdotool type --delay 60 "M9-GUI-$(date +%H%M%S)"
  sleep 1
  xdotool key Return
  sleep 5
  shot 4_sent
  ;;
5)
  peer_send "LIVE-$(date +%H%M%S)"
  sleep 4
  shot 5_live
  ;;
6)
  geom "IM -"
  xdotool windowactivate --sync "$(xdotool search --name 'IM -' | head -1)" 2>/dev/null
  sleep 2
  peer_send "UNREAD-$(date +%H%M%S)"
  sleep 4
  shot 6_unread
  ;;
7)
  geom "会话 -"
  xdotool windowactivate --sync "$(xdotool search --name '会话 -' | head -1)" 2>/dev/null
  sleep 1
  xdotool key Escape          # 关掉聊天窗：此后推送应计入未读
  sleep 2
  echo "--- windows after close ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -5
  peer_send "CLOSED-$(date +%H%M%S)"
  sleep 4
  shot 7_unread_closed
  ;;
8)
  open_session
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -5
  shot 8_reopen
  ;;
9)
  "$BIN/dual_client_push" 127.0.0.1 9000 9001 --send-many "$PEER" "$PHONE" 55 "BURST"
  sleep 3
  ;;
10)
  # 关掉旧聊天窗再重开，确保走首屏分页
  geom "会话 -" && xdotool key Escape && sleep 2
  open_session
  geom "会话 -"
  echo "chat window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  shot 10_page1
  ;;
11)
  geom "会话 -"
  xdotool mousemove $((X + 53)) $((Y + 24)) click 1   # 「加载更多」
  sleep 5
  shot 11_page2
  ;;
*)
  echo "unknown stage"
  ;;
esac
echo "--- done ---"
