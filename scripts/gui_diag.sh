#!/bin/bash
# 诊断：脚本内中文窗口名匹配失败的原因（编码/字节比对）
export DISPLAY=:99
pat="会话 -"
echo "pattern bytes: $(printf '%s' "$pat" | od -An -tx1 | tr -s ' ')"
echo "search count: $(xdotool search --name "$pat" 2>/dev/null | wc -l)"
echo "--- all windows ---"
for id in $(xdotool search --name '' 2>/dev/null); do
  nm=$(xdotool getwindowname "$id" 2>/dev/null)
  echo "id=$id name=[$nm] bytes=$(printf '%s' "$nm" | od -An -tx1 | tr -s ' ' | head -1)"
done
