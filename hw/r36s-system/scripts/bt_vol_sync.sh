#!/bin/bash
LAST_VOL=""
while true; do
    BT_CTRL=$(amixer -D bluealsa scontrols 2>/dev/null | head -n1 | cut -d"'" -f2)
    if [ -z "$BT_CTRL" ]; then sleep 3; continue; fi
    CUR=$(amixer -c 0 get Playback 2>/dev/null | grep -m1 -o "[0-9]*%" | head -1)
    if [ "$CUR" != "$LAST_VOL" ] && [ ! -z "$CUR" ]; then
        amixer -D bluealsa sset "$BT_CTRL" "$CUR" > /dev/null 2>&1
        LAST_VOL="$CUR"
    fi
    sleep 0.5
done
