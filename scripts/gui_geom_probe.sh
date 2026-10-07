#!/bin/bash
# 诊断：聊天窗/主窗口的 xdotool 匹配与几何（确认点击偏移基准）
export DISPLAY=:99
for pat in "会话 -" "IM -"; do
  echo "=== pattern: $pat ==="
  ids=$(xdotool search --name "$pat" 2>/dev/null)
  for id in $ids; do
    name=$(xdotool getwindowname "$id" 2>/dev/null)
    geo=$(xdotool getwindowgeometry --shell "$id" 2>/dev/null | tr '\n' ' ')
    echo "id=$id name='$name' $geo"
  done
done
