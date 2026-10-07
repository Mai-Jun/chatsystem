#!/bin/bash
# M9 headless GUI verification: Xvfb + openbox + im_client + xdotool + screenshots
# Usage: bash gui_test.sh <stage> [phone] [password] [peer_phone]
#   1  = launch, screenshot login page
#   2  = login (skipped if a saved token auto-logs in), screenshot main window
#   3  = open a session, screenshot chat window
#   4  = send a text message from the chat window
#   5  = peer sends while chat window exists (live push)
#   6  = focus main window, peer sends (unread badge)
#   7  = close chat window, peer sends (unread badge, focus-independent)
#   8  = reopen session (unread clears)
#   9  = peer bursts 55 messages (pagination setup)
#  10  = close + reopen chat window (first page state)
#  11  = click "load more"
#  12  = restart client, fresh chat window first page (50 msgs, button enabled)
#  13  = click "load more" on the fresh window
#  14  = wipe local token, restart -> back to login page
#
# Notes:
#  - Without a window manager xdotool windowactivate does nothing and Qt's
#    isActiveWindow() stays true, so focus-dependent cases need openbox.
#  - Widgets are addressed as window geometry + offset; offsets calibrated
#    under openbox (xdotool Y and the client-area origin differ by a constant).
#  - ASCII only: window titles are Chinese, matched indirectly via window ids.
set -u
export DISPLAY=:99
SHOTS=/root/gui_shots
BIN=/root/chatsystem/client/build-server
mkdir -p "$SHOTS"

PHONE="${2:-19353589846}"
PASSWORD="${3:-pass123}"
PEER="${4:-18353589846}"

shot() { import -window root "$SHOTS/$1.png"; echo "[shot] $1"; }
fail() { echo "[fail] $1" >&2; exit 1; }

# geometry of a window id -> X/Y/WIDTH/HEIGHT
geom_id() { eval "$(xdotool getwindowgeometry --shell "$1")"; }

main_win() {
  local id
  id=$(xdotool search --onlyvisible --name '^IM - ' 2>/dev/null | head -1)
  [ -n "$id" ] || fail "main window not found"
  echo "$id"
}

# chat window = any other visible im_client window than the main one
chat_win() {
  local main_id pid id
  main_id=$(main_win) || return 1
  pid=$(pgrep -x im_client | head -1)
  for id in $(xdotool search --onlyvisible --pid "$pid" 2>/dev/null); do
    if [ "$id" != "$main_id" ]; then echo "$id"; return 0; fi
  done
  return 1
}

login_win() {
  local id
  id=$(xdotool search --onlyvisible --name 'IM .*/' 2>/dev/null | head -1)
  # login window title contains Chinese; fall back to: visible, not main, not chat
  if [ -z "$id" ]; then
    local main_id pid
    main_id=$(main_win 2>/dev/null) || main_id=""
    pid=$(pgrep -x im_client | head -1)
    for id in $(xdotool search --onlyvisible --pid "$pid" 2>/dev/null); do
      [ "$id" != "$main_id" ] && { echo "$id"; return 0; }
    done
    return 1
  fi
  echo "$id"
}

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
  local lid
  if lid=$(main_win 2>/dev/null); then
    echo "[login] token persisted, already logged in - skip"
    return 0
  fi
  lid=$(login_win) || fail "no login window"
  geom_id "$lid"
  xdotool windowactivate --sync "$lid" 2>/dev/null
  sleep 1
  xdotool mousemove $((X + 209)) $((Y + 138)) click 1  # phone field
  sleep 1
  xdotool type --delay 60 "$PHONE"
  sleep 1
  xdotool mousemove $((X + 209)) $((Y + 184)) click 1  # password field
  sleep 1
  xdotool type --delay 60 "$PASSWORD"
  sleep 1
  xdotool key Return
  sleep 6
}

# double-click first item of the session list
open_session() {
  local mid
  mid=$(main_win) || fail "no main window"
  geom_id "$mid"
  xdotool mousemove $((X + 200)) $((Y + 85)) click --repeat 2 --delay 120 1
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
  mid=$(main_win) || fail "no main window after login"
  geom_id "$mid"
  echo "main window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  echo "--- app log ---"
  cat /root/im_client_gui.log
  shot 2_main
  ;;
3)
  open_session
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  echo "chat window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  shot 3_chat
  ;;
4)
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  xdotool mousemove $((X + 280)) $((Y + 651)) click 1  # input field (640x700 layout)
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
  mid=$(main_win) || fail "no main window"
  xdotool windowactivate --sync "$mid" 2>/dev/null
  sleep 2
  peer_send "UNREAD-$(date +%H%M%S)"
  sleep 4
  shot 6_unread
  ;;
7)
  cid=$(chat_win) || fail "no chat window"
  xdotool windowactivate --sync "$cid" 2>/dev/null
  sleep 1
  xdotool key Escape          # close chat window: pushes must count as unread
  sleep 2
  echo "--- windows after close ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -5
  peer_send "CLOSED-$(date +%H%M%S)"
  sleep 4
  shot 7_unread_closed
  ;;
8)
  open_session
  shot 8_reopen
  ;;
9)
  "$BIN/dual_client_push" 127.0.0.1 9000 9001 --send-many "$PEER" "$PHONE" 55 "BURST"
  sleep 3
  ;;
10)
  cid=$(chat_win 2>/dev/null) && { xdotool windowactivate --sync "$cid"; xdotool key Escape; sleep 2; }
  open_session
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  echo "chat window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  shot 10_page1
  ;;
11)
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  xdotool mousemove $((X + 52)) $((Y + 5)) click 1    # "load more"
  sleep 6
  shot 11_page2
  ;;
12)
  start_app
  do_login
  open_session
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  echo "chat window: X=$X Y=$Y ${WIDTH}x${HEIGHT}"
  shot 12_page1_fresh
  ;;
13)
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  xdotool mousemove $((X + 52)) $((Y + 5)) click 1    # "load more"
  sleep 6
  shot 13_page2_fresh
  ;;
14)
  rm -f "$HOME/.config/im-system/im-client.conf"      # wipe token -> login page
  start_app
  echo "--- windows ---"
  xdotool search --name "" getwindowname %@ 2>/dev/null | grep -v '^$' | head -5
  shot 14_login_fresh
  ;;
15)
  cid=$(chat_win) || fail "no chat window"
  geom_id "$cid"
  # scroll the message view to the bottom via the vertical scrollbar trough
  xdotool mousemove $((X + 542)) $((Y + 540)) click --repeat 4 --delay 300 1
  sleep 1
  xdotool mousemove $((X + 280)) $((Y + 300)) click 1
  xdotool click --repeat 40 5                          # wheel down as a fallback
  sleep 3
  shot 15_image
  ;;
16)
  # peer sends an image while the chat window is open:
  # the push appends it and the view scrolls to the bottom -> inline render
  cid=$(chat_win) || fail "no chat window"
  "$BIN/dual_client_push" 127.0.0.1 9000 9001 --send-image "$PEER" "$PHONE"
  sleep 6
  shot 16_image_push
  ;;
*)
  echo "unknown stage"
  ;;
esac
echo "--- done ---"
