#!/bin/bash
# Clean boot v2 - RTL8723BU Edition

# 1. Reset RetroArch Audio to Default (Speaker)
for CFG in "/home/ark/.config/retroarch/retroarch.cfg" "/home/ark/.config/retroarch32/retroarch.cfg"; do
    if [ -f "$CFG" ]; then
        sed -i 's/^audio_device =.*/audio_device = ""/' "$CFG"
        sed -i 's/^audio_driver =.*/audio_driver = "alsa"/' "$CFG"
    fi
done

# 2. Clean Audio Configs
rm -f /home/ark/.asoundrc
rm -f /etc/asound.conf

# 3. Disable Radios at boot (user enables via Absolute.sh)
rmmod 8723bu 2>/dev/null
systemctl stop bluetooth 2>/dev/null
systemctl stop bluealsa 2>/dev/null
pkill alsaloop
rmmod btrtl btusb rtk_btcoex rtk_btusb snd-aloop 2>/dev/null

exit 0
