#!/bin/bash
# BT Volume Monitor v2 - Direct BT control

RETROARCH_CFG="/home/ark/.config/retroarch/retroarch.cfg"
RA32_CFG="/home/ark/.config/retroarch32/retroarch.cfg"
LAST_MAC=""

log() { echo "$(date +%H:%M:%S) $1" >> /roms/bt_audio_debug.log; }

EnsureLoopback() {
    if ! lsmod | grep -q snd_aloop; then
        modprobe snd-aloop
        sleep 2
    fi
}

StartLoopback() {
    local MAC=$1
    if [ "$MAC" == "$LAST_MAC" ]; then return; fi
    log "=== StartLoopback for $MAC ==="

    EnsureLoopback
    pkill alsaloop
            pkill -f bt_vol_sync.sh 2>/dev/null
    sleep 1

    # asound.conf
    cat > /etc/asound.conf << AEOF
pcm.blueout {
    type plug
    slave.pcm {
        type bluealsa
        device "$MAC"
        profile "a2dp"
    }
}
pcm.!default {
    type plug
    slave.pcm "hw:Loopback,0,0"
}
ctl.!default {
    type hw
    card 0
}
AEOF

    # Start alsaloop
    nohup alsaloop -C hw:Loopback,1,0 -P blueout -t 20000 --resample > /dev/null 2>&1 &

    # RetroArch config
    for CFG in "$RETROARCH_CFG" "$RA32_CFG"; do
        if [ -f "$CFG" ]; then
            sed -i "s/^audio_device =.*/audio_device = \"plughw:Loopback,0,0\"/" "$CFG"
            sed -i "s/^audio_driver =.*/audio_driver = \"alsathread\"/" "$CFG"
        fi
    done

    # BT volume na max - hw tlačítka ovládají přes BT AVRCP
    sleep 2
    amixer -D bluealsa sset "$BT_CTRL" 20% 2>/dev/null

    LAST_MAC="$MAC"
    pkill -f bt_vol_sync.sh 2>/dev/null
    /opt/bt_fix/bt_vol_sync.sh &
    log "=== Loopback READY ==="
}

while true; do
    if ! systemctl is-active --quiet bluealsa; then
        sleep 5
        continue
    fi

    BT_CTRL=$(amixer -D bluealsa scontrols 2>/dev/null | head -n1 | cut -d"'" -f2)

    if [ ! -z "$BT_CTRL" ]; then
        CONNECTED_MAC=$(bluetoothctl info 2>/dev/null | grep "Device" | head -n1 | awk "{print \$2}")

        if [ ! -z "$CONNECTED_MAC" ]; then
            StartLoopback "$CONNECTED_MAC"
        fi
    else
        if [ ! -z "$LAST_MAC" ]; then
            pkill alsaloop
            pkill -f bt_vol_sync.sh 2>/dev/null
            rm -f /etc/asound.conf
            LAST_MAC=""
        fi
    fi
    sleep 3
done
