#!/bin/bash
# Backup v14 — BT Fix + Performance Optimizations
BACKUP_DIR="/roms/backup/bt_fix"
mkdir -p "$BACKUP_DIR"/{bt_fix,firmware,libs,config,perf,retroarch,retroarch32,emulationstation}
echo "=== Backup v14 ==="

# Drivers
echo "- Drivers..."
cp /lib/modules/$(uname -r)/kernel/drivers/bluetooth/rtk_btusb.ko "$BACKUP_DIR/" 2>/dev/null
cp /lib/modules/$(uname -r)/kernel/sound/drivers/snd-aloop.ko "$BACKUP_DIR/" 2>/dev/null
cp /lib/modules/$(uname -r)/kernel/drivers/net/wireless/ew-7611ulb/8723bu.ko "$BACKUP_DIR/" 2>/dev/null

# Firmware
echo "- Firmware..."
cp /lib/firmware/rtl_bt/rtl8821c* "$BACKUP_DIR/firmware/" 2>/dev/null
cp /lib/firmware/rtlwifi/rtl8723bu_* "$BACKUP_DIR/firmware/" 2>/dev/null
cp /lib/firmware/rtl_bt/rtl8723b* "$BACKUP_DIR/firmware/" 2>/dev/null

# Config
echo "- Config..."
cp /etc/modules-load.d/8723bu.conf "$BACKUP_DIR/config/" 2>/dev/null
cp /etc/modprobe.d/blacklist-rtl8xxxu.conf "$BACKUP_DIR/config/" 2>/dev/null

# Scripts
echo "- Scripts..."
cp "/opt/system/Absolute.sh" "$BACKUP_DIR/" 2>/dev/null
cp "/opt/system/Backup.sh" "$BACKUP_DIR/" 2>/dev/null
cp /opt/bt_fix/*.sh "$BACKUP_DIR/bt_fix/" 2>/dev/null

# Services
echo "- Services..."
cp /etc/systemd/system/r36s-radio-off.service "$BACKUP_DIR/" 2>/dev/null
cp /etc/systemd/system/bluealsa.service "$BACKUP_DIR/" 2>/dev/null
cp /etc/systemd/system/bt-vol.service "$BACKUP_DIR/" 2>/dev/null
cp /etc/systemd/system/zram-swap.service "$BACKUP_DIR/perf/" 2>/dev/null

# BlueALSA binary
echo "- BlueALSA..."
cp /usr/bin/bluealsad "$BACKUP_DIR/" 2>/dev/null

# 32-bit ALSA libs
echo "- 32-bit libs..."
cp /usr/lib/arm-linux-gnueabihf/alsa-lib/libasound_module_pcm_bluealsa.so "$BACKUP_DIR/libs/" 2>/dev/null
cp /usr/lib/arm-linux-gnueabihf/alsa-lib/libasound_module_ctl_bluealsa.so "$BACKUP_DIR/libs/" 2>/dev/null

# RetroArch + ES config
echo "- RetroArch + ES config..."
cp /home/ark/.config/retroarch/retroarch.cfg "$BACKUP_DIR/retroarch/" 2>/dev/null
cp /home/ark/.config/retroarch/retroarch-core-options.cfg "$BACKUP_DIR/retroarch/" 2>/dev/null
cp -r /home/ark/.config/retroarch/config "$BACKUP_DIR/retroarch/" 2>/dev/null
cp -r /home/ark/.config/retroarch/overlays "$BACKUP_DIR/retroarch/" 2>/dev/null
cp /home/ark/.config/retroarch32/retroarch.cfg "$BACKUP_DIR/retroarch32/" 2>/dev/null
cp -r /home/ark/.config/retroarch32/config "$BACKUP_DIR/retroarch32/" 2>/dev/null
cp /home/ark/.emulationstation/*.cfg "$BACKUP_DIR/emulationstation/" 2>/dev/null

# Performance optimizations
echo "- Performance tweaks..."
cp /usr/local/bin/perfmax "$BACKUP_DIR/perf/" 2>/dev/null
cp /usr/local/bin/perfnorm "$BACKUP_DIR/perf/" 2>/dev/null
cp /usr/local/bin/perfmax.bak "$BACKUP_DIR/perf/" 2>/dev/null
cp /usr/local/bin/perfnorm.bak "$BACKUP_DIR/perf/" 2>/dev/null
cp /etc/sysctl.d/99-r36s-gaming.conf "$BACKUP_DIR/perf/" 2>/dev/null
cp /etc/udev/rules.d/60-ioscheduler.rules "$BACKUP_DIR/perf/" 2>/dev/null

# ASOUND config
echo "- ALSA config..."
cp /home/ark/.asoundrc "$BACKUP_DIR/" 2>/dev/null
cp /home/ark/.asoundrcbak "$BACKUP_DIR/" 2>/dev/null
cp /etc/asound.conf "$BACKUP_DIR/" 2>/dev/null

# Generate restore script
cat > "$BACKUP_DIR/restore.sh" << 'EOF'
#!/bin/bash
# Restore v14 — BT Fix + Performance
echo "=== Restore v14 ==="

# Create directories
mkdir -p /opt/bt_fix /usr/lib/arm-linux-gnueabihf/alsa-lib /lib/firmware/{rtlwifi,rtl_bt} /etc/{modules-load.d,modprobe.d,sysctl.d,udev/rules.d}

# Drivers
echo "- Drivers..."
cp rtk_btusb.ko /lib/modules/$(uname -r)/kernel/drivers/bluetooth/ 2>/dev/null
[ -f "snd-aloop.ko" ] && cp snd-aloop.ko /lib/modules/$(uname -r)/kernel/sound/drivers/
if [ -f "8723bu.ko" ]; then
    mkdir -p /lib/modules/$(uname -r)/kernel/drivers/net/wireless/ew-7611ulb/
    cp 8723bu.ko /lib/modules/$(uname -r)/kernel/drivers/net/wireless/ew-7611ulb/
fi
depmod -a

# Firmware
echo "- Firmware..."
cp firmware/rtl8821c* /lib/firmware/rtl_bt/ 2>/dev/null
cp firmware/rtl8723bu_* /lib/firmware/rtlwifi/ 2>/dev/null
cp firmware/rtl8723b* /lib/firmware/rtl_bt/ 2>/dev/null
ln -sf /lib/firmware/rtl_bt/rtl8821c_fw.bin /lib/firmware/rtl8821c_fw
ln -sf /lib/firmware/rtl_bt/rtl8821c_config.bin /lib/firmware/rtl8821c_config

# Config
echo "- Config..."
[ -d "config" ] && cp config/*.conf /etc/modules-load.d/ 2>/dev/null
[ -d "config" ] && cp config/blacklist-*.conf /etc/modprobe.d/ 2>/dev/null

# BlueALSA
echo "- BlueALSA..."
[ -f "bluealsad" ] && cp bluealsad /usr/bin/ && chmod +x /usr/bin/bluealsad

# Libraries
echo "- Libraries..."
[ -d "libs" ] && cp libs/* /usr/lib/arm-linux-gnueabihf/alsa-lib/

# Scripts
echo "- Scripts..."
[ -f "Absolute.sh" ] && cp Absolute.sh /opt/system/ && chmod +x /opt/system/Absolute.sh
[ -f "Backup.sh" ] && cp Backup.sh /opt/system/ && chmod +x /opt/system/Backup.sh
[ -d "bt_fix" ] && cp bt_fix/* /opt/bt_fix/ && chmod +x /opt/bt_fix/*.sh

# RetroArch + ES config
echo "- RetroArch + ES..."
[ -d "retroarch" ] && cp retroarch/retroarch.cfg /home/ark/.config/retroarch/ 2>/dev/null
[ -d "retroarch" ] && cp retroarch/retroarch-core-options.cfg /home/ark/.config/retroarch/ 2>/dev/null
[ -d "retroarch/config" ] && cp -r retroarch/config /home/ark/.config/retroarch/ 2>/dev/null
[ -d "retroarch/overlays" ] && cp -r retroarch/overlays /home/ark/.config/retroarch/ 2>/dev/null
[ -d "retroarch32" ] && cp retroarch32/retroarch.cfg /home/ark/.config/retroarch32/ 2>/dev/null
[ -d "retroarch32/config" ] && cp -r retroarch32/config /home/ark/.config/retroarch32/ 2>/dev/null
[ -d "emulationstation" ] && cp emulationstation/*.cfg /home/ark/.emulationstation/ 2>/dev/null

# ALSA config
echo "- ALSA config..."
[ -f ".asoundrc" ] && cp .asoundrc /home/ark/
[ -f ".asoundrcbak" ] && cp .asoundrcbak /home/ark/
[ -f "asound.conf" ] && cp asound.conf /etc/

# Performance optimizations
echo "- Performance tweaks..."
[ -d "perf" ] && cp perf/perfmax perf/perfnorm /usr/local/bin/ && chmod +x /usr/local/bin/perf{max,norm}
[ -f "perf/99-r36s-gaming.conf" ] && cp perf/99-r36s-gaming.conf /etc/sysctl.d/
[ -f "perf/60-ioscheduler.rules" ] && cp perf/60-ioscheduler.rules /etc/udev/rules.d/
[ -f "perf/zram-swap.service" ] && cp perf/zram-swap.service /etc/systemd/system/

# Services
echo "- Services..."
cp *.service /etc/systemd/system/ 2>/dev/null
systemctl daemon-reload
systemctl enable r36s-radio-off.service bluealsa.service bt-vol.service zram-swap.service 2>/dev/null
sysctl --system 2>/dev/null

echo "=== Restore v14 DONE! Reboot. ==="
EOF
chmod +x "$BACKUP_DIR/restore.sh"
echo "=== Backup v14 DONE ==="
