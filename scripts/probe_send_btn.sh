#!/bin/bash
# probe: click where the send button should be; verify message send (button exists?)
export DISPLAY=:99
cid=$(xdotool search --name "^Chat" 2>/dev/null | head -1)
# chat window title is Chinese; find by exclusion: visible im_client window that is not main
main_id=$(xdotool search --onlyvisible --name '^IM - ' | head -1)
pid=$(pgrep -x im_client | head -1)
for id in $(xdotool search --onlyvisible --pid "$pid" 2>/dev/null); do
  if [ "$id" != "$main_id" ]; then cid="$id"; break; fi
done
[ -n "$cid" ] || { echo "no chat window"; exit 1; }
eval "$(xdotool getwindowgeometry --shell "$cid")"
echo "chat window X=$X Y=$Y ${WIDTH}x${HEIGHT}"

# input field: bottom area of the window
xdotool mousemove $((X + 260)) $((Y + 648)) click 1
sleep 1
xdotool type --delay 50 "BTNTEST"
sleep 1
# candidate send-button slot (right end of the input row)
xdotool mousemove $((X + 588)) $((Y + 657)) click 1
sleep 3
import -window root /root/gui_shots/btn_probe.png
echo PROBE_DONE
