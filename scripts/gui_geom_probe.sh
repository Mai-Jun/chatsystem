#!/bin/bash
# probe: geometry of every visible im_client window (identify login dialog by size)
export DISPLAY=:99
pid=$(pgrep -x im_client | head -1)
echo "pid=$pid"
for id in $(xdotool search --onlyvisible --pid "$pid" 2>/dev/null); do
  geo=$(xdotool getwindowgeometry --shell "$id" 2>/dev/null | tr '\n' ' ')
  echo "id=$id $geo"
done
