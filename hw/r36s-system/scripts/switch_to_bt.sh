#!/bin/bash
# Background BT switch - runs without dialog
LOG="/roms/bt_switch_debug.log"
echo "=== SwitchToBt $(date) ===" > "$LOG"

echo "blacklist 8821cu" | tee /etc/modprobe.d/block-wifi-temp.conf >/dev/null
echo "install 8821cu /bin/true" | tee -a /etc/modprobe.d/block-wifi-temp.conf >/dev/null
depmod -a

systemctl stop NetworkManager 2>/dev/null
systemctl stop wpa_supplicant 2>/dev/null
ifconfig wlan0 down 2>/dev/null
rmmod 8821cu 2>/dev/null

rmmod rtk_btusb 2>/dev/null
rmmod rtk_btcoex 2>/dev/null

echo -1 > /sys/module/usbcore/parameters/autosuspend

# Python USB reset
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

echo "1-1" > /sys/bus/usb/drivers/usb/unbind 2>/dev/null
sleep 2
echo "1-1" > /sys/bus/usb/drivers/usb/bind 2>/dev/null
sleep 3

modprobe rtk_btusb mp_drv_mode=1
sleep 3

if ! hciconfig -a | grep -q "BD Address"; then
    rmmod rtk_btusb 2>/dev/null
    rmmod rtk_btcoex 2>/dev/null
    sleep 1
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ] && [ "$(cat $dev/idVendor)" == "0bda" ]; then
            bus=$(cat "$dev/busnum"); devnum=$(cat "$dev/devnum")
            python3 /tmp/usbreset.py $(printf "/dev/bus/usb/%03d/%03d" "$bus" "$devnum")
        fi
    done
    sleep 2
    modprobe rtk_btusb mp_drv_mode=1
    sleep 3
fi

systemctl start bluetooth 2>/dev/null
hciconfig hci0 up 2>/dev/null

systemctl restart bluealsa
modprobe snd-aloop
systemctl restart bt-vol
sleep 2

echo "=== COMPLETE $(date) ===" >> "$LOG"
