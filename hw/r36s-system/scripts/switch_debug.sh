#!/bin/bash
# Debug wrapper - simuluje SwitchToBt bez dialogů
LOG="/roms/bt_switch_debug.log"
echo "=== SwitchToBt Debug $(date) ===" > "$LOG"

echo "$(date +%T) Blacklisting wifi..." >> "$LOG"
echo "blacklist 8821cu" | sudo tee /etc/modprobe.d/block-wifi-temp.conf >/dev/null
echo "install 8821cu /bin/true" | sudo tee -a /etc/modprobe.d/block-wifi-temp.conf >/dev/null
sudo depmod -a
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Stopping network..." >> "$LOG"
sudo systemctl stop NetworkManager 2>/dev/null
sudo systemctl stop wpa_supplicant 2>/dev/null
sudo ifconfig wlan0 down 2>/dev/null
sudo rmmod 8821cu 2>/dev/null
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Removing BT modules..." >> "$LOG"
sudo rmmod rtk_btusb 2>/dev/null
sudo rmmod rtk_btcoex 2>/dev/null
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Disabling autosuspend..." >> "$LOG"
echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend >/dev/null
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Python reset..." >> "$LOG"
cat <<EOF > /tmp/usbreset.py
import fcntl, os, sys
USBDEVFS_RESET = 21780
try:
    path = sys.argv[1]
    fd = os.open(path, os.O_WRONLY)
    fcntl.ioctl(fd, USBDEVFS_RESET, 0)
    os.close(fd)
except: pass
EOF
for dev in /sys/bus/usb/devices/*; do
    if [ -f "$dev/idVendor" ]; then
        vid=$(cat "$dev/idVendor")
        if [ "$vid" == "0bda" ]; then
            bus=$(cat "$dev/busnum")
            devnum=$(cat "$dev/devnum")
            python3 /tmp/usbreset.py $(printf "/dev/bus/usb/%03d/%03d" "$bus" "$devnum")
        fi
    fi
done
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) USB unbind..." >> "$LOG"
echo "1-1" > /sys/bus/usb/drivers/usb/unbind 2>/dev/null
echo "$(date +%T) unbind done, sleeping 2..." >> "$LOG"
sleep 2
echo "$(date +%T) USB bind..." >> "$LOG"
echo "1-1" > /sys/bus/usb/drivers/usb/bind 2>/dev/null
echo "$(date +%T) bind done, sleeping 3..." >> "$LOG"
sleep 3

echo "$(date +%T) Loading BT module..." >> "$LOG"
sudo modprobe rtk_btusb mp_drv_mode=1
echo "$(date +%T) done, sleeping 3..." >> "$LOG"
sleep 3

echo "$(date +%T) Checking hci..." >> "$LOG"
hciconfig -a >> "$LOG" 2>&1
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Starting bluetooth..." >> "$LOG"
sudo systemctl start bluetooth 2>/dev/null
sudo hciconfig hci0 up 2>/dev/null
echo "$(date +%T) done" >> "$LOG"

echo "$(date +%T) Starting audio services..." >> "$LOG"
sudo systemctl restart bluealsa
sudo modprobe snd-aloop
sudo systemctl restart bt-vol
echo "$(date +%T) done" >> "$LOG"

echo "=== COMPLETE $(date) ===" >> "$LOG"
