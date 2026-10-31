#!/bin/bash
# R36S Project Dump Tool
# Uloží obsah všech našich skriptů a služeb do jednoho souboru

OUTfile="/roms/R36S_Project_Current_State.txt"

header() {
    echo "================================================================================" >> "$OUTfile"
    echo "$1" >> "$OUTfile"
    echo "================================================================================" >> "$OUTfile"
}

file_dump() {
    FILE="$1"
    echo "" >> "$OUTfile"
    echo "--------------------------------------------------------------------------------" >> "$OUTfile"
    echo "SOUBOR: $FILE" >> "$OUTfile"
    echo "--------------------------------------------------------------------------------" >> "$OUTfile"
    if [ -f "$FILE" ]; then
        cat "$FILE" >> "$OUTfile"
    else
        echo "[FILE NOT FOUND]" >> "$OUTfile"
    fi
    echo "" >> "$OUTfile"
}

# Start
echo "Generuji dump do $OUTfile ..."
echo "R36S PROJECT DUMP - $(date)" > "$OUTfile"

header "1. SYSTEM SERVICES"
file_dump "/etc/systemd/system/bluealsa.service"
file_dump "/etc/systemd/system/bt-vol.service"
file_dump "/etc/systemd/system/r36s-radio-off.service"

header "2. BACKEND SCRIPTS (/opt/bt_fix)"
file_dump "/opt/bt_fix/clean_boot.sh"
file_dump "/opt/bt_fix/bt_volume_monitor.sh"
file_dump "/opt/bt_fix/switch_to_bt.sh"

header "3. FRONTEND SCRIPT (/opt/system)"
file_dump "/opt/system/Absolute.sh"
# Pro jistotu zkontrolujeme i starý název
file_dump "/opt/system/WiFi BT Toggle.sh"

header "4. AUDIO CONFIGURATION"
file_dump "/etc/asound.conf"
file_dump "/home/ark/.asoundrc"

header "5. BACKUP SCRIPT"
file_dump "/roms/create_backup.sh"

header "6. SYSTEM STATUS"
echo "--- Kernel Modules ---" >> "$OUTfile"
lsmod | grep -E "snd|blue|rtk" >> "$OUTfile"
echo "" >> "$OUTfile"
echo "--- Bluetooth ---" >> "$OUTfile"
hciconfig -a >> "$OUTfile"
echo "" >> "$OUTfile"
echo "--- Sound Cards ---" >> "$OUTfile"
cat /proc/asound/cards >> "$OUTfile"

echo "HOTOVO! Soubor uložen v: $OUTfile"
