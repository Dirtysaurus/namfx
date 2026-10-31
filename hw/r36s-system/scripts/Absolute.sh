#!/bin/bash
# Absolute.sh - v71 (RTL8723BU All-in-One)

sudo chmod 666 /dev/tty1
sudo chmod 666 /dev/uinput
export TERM=linux

# Log file
LOGFILE="/roms/bt_debug.log"

# Constants
WIFI_MOD="8723bu"
BT_MOD="rtk_btusb"
COEX_MOD="rtk_btcoex"
USB_DEV="/sys/bus/usb/devices/1-1"
BLACKLIST_FILE="/etc/modprobe.d/block-wifi-temp.conf"

# Python Reset Tool
RESET_TOOL="/tmp/usbreset.py"
cat <<EOF > "$RESET_TOOL"
import fcntl, os, sys
USBDEVFS_RESET = 21780
try:
    path = sys.argv[1]
    fd = os.open(path, os.O_WRONLY)
    fcntl.ioctl(fd, USBDEVFS_RESET, 0)
    os.close(fd)
except: pass
EOF

log() { echo "$(date '+%H:%M:%S') - $1" >> "$LOGFILE"; }
progress() { dialog --infobox "\n$1\n\nPlease wait..." 6 50; }

ExitMenu() {
  printf "\033c" > /dev/tty1
  pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null
  exit 0
}

GetStatus() {
  WIFI_ON="OFF"
  BT_ON="OFF"
  if lsmod | grep -q "$WIFI_MOD"; then WIFI_ON="ON"; fi
  if hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"; then BT_ON="ON"; fi
  STATUS="WiFi:$WIFI_ON | BT:$BT_ON"
}

StartServices() {
    progress "Starting Audio Services..."
    sudo systemctl restart bluealsa
    if ! lsmod | grep -q "snd_aloop"; then
        sudo modprobe snd-aloop
    fi
    sudo systemctl restart bt-vol
    sleep 2
}

StopServices() {
    sudo systemctl stop bluealsa
    sudo systemctl stop bt-vol
    sudo pkill bluealsad
    sudo pkill alsaloop
    sudo rmmod snd-aloop 2>/dev/null
}

PerformPythonReset() {
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ]; then
            vid=$(cat "$dev/idVendor")
            if [ "$vid" == "0bda" ]; then
                bus=$(cat "$dev/busnum")
                devnum=$(cat "$dev/devnum")
                python3 "$RESET_TOOL" $(printf "/dev/bus/usb/%03d/%03d" "$bus" "$devnum")
            fi
        fi
    done
}

ResetRetroArch() {
    for CFG in "/home/ark/.config/retroarch/retroarch.cfg" "/home/ark/.config/retroarch32/retroarch.cfg"; do
        if [ -f "$CFG" ]; then
            sed -i 's/^audio_device =.*/audio_device = ""/' "$CFG"
            sed -i 's/^audio_driver =.*/audio_driver = "alsa"/' "$CFG"
        fi
    done
}

# =============================================================================
# WIFI FUNCTIONS
# =============================================================================

EnableWifi() {
    progress "Enabling WiFi..."
    sudo rm -f "$BLACKLIST_FILE"
    sudo depmod -a
    if ! lsmod | grep -q "$WIFI_MOD"; then
        sudo modprobe "$WIFI_MOD" rtw_btcoex_enable=1
    fi
    sudo systemctl start wpa_supplicant 2>/dev/null
    sudo systemctl start NetworkManager 2>/dev/null
    sudo nmcli radio wifi on 2>/dev/null
    sudo ifconfig wlan0 up 2>/dev/null
    sleep 3
    dialog --msgbox "WiFi Enabled!" 5 30
}

DisableWifi() {
    progress "Disabling WiFi..."
    sudo systemctl stop NetworkManager 2>/dev/null
    sudo systemctl stop wpa_supplicant 2>/dev/null
    sudo ifconfig wlan0 down 2>/dev/null
    sudo rmmod "$WIFI_MOD" 2>/dev/null
    sleep 1
    dialog --msgbox "WiFi Disabled." 5 30
}

WifiScan() {
    progress "Scanning WiFi networks..."
    sleep 1
    clist=$(sudo nmcli -f ALL --mode tabular --terse --fields IN-USE,SSID,CHAN,SIGNAL,SECURITY dev wifi 2>/dev/null)
    if [ -z "$clist" ]; then
        sleep 2
        clist=$(sudo nmcli -f ALL --mode tabular --terse --fields IN-USE,SSID,CHAN,SIGNAL,SECURITY dev wifi 2>/dev/null)
    fi

    cur_ap=$(iw dev wlan0 info 2>/dev/null | grep ssid | cut -c 7-30)
    if [ -z "$cur_ap" ]; then
        cur_ap=$(sudo nmcli -t -f name,device connection show --active 2>/dev/null | grep wlan0 | cut -d: -f1)
    fi

    local IFS=':'
    unset coptions
    declare -a coptions
    while IFS= read -r line; do
        read -a strarr <<< "$line"
        INUSE=$(printf '%-5s' "${strarr[0]}")
        SSID="${strarr[1]}"
        CHAN=$(printf '%-5s' "${strarr[2]}")
        SIGNAL=$(printf '%-5s' "${strarr[3]}%")
        SECURITY="${strarr[4]}"
        [ -z "$SSID" ] && continue
        coptions+=("$SSID" "$INUSE $CHAN $SIGNAL $SECURITY")
    done <<< "$clist"

    if [ ${#coptions[@]} -eq 0 ]; then
        dialog --msgbox "No networks found." 5 30
        return
    fi

    local selection
    selection=$(dialog \
        --backtitle "Connected: $cur_ap" \
        --title "SSID  USE  CH  SIG  SEC" \
        --no-collapse --clear --cancel-label "Back" \
        --menu "" 18 60 10 "${coptions[@]}" 2>&1 >/dev/tty1)

    [ $? -ne 0 ] && return

    WifiConnect "$selection"
}

WifiConnect() {
    local SSID="$1"
    progress "Connecting to $SSID..."

    # Check if it needs a password
    local SEC=$(sudo nmcli -f ALL --mode tabular --terse --fields SSID,SECURITY dev wifi 2>/dev/null | grep "^$SSID:" | head -1 | cut -d: -f2)

    local PASS=""
    if echo "$SEC" | grep -qi "WPA\|WEP"; then
        # Kill gptokeyb before OSK
        pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null
        PASS=$(osk --backtitle "WiFi Password" --inputbox "Password for $SSID" --minchars 8 2>&1 >/dev/null)
        local osk_ret=$?
        # Restart gptokeyb
        /opt/inttools/gptokeyb -1 "Absolute.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
        if [ $osk_ret -ne 0 ] || [ -z "$PASS" ]; then
            dialog --msgbox "Cancelled." 5 30
            return
        fi
    fi

    # Delete old connection, connect fresh
    sudo nmcli con delete "$SSID" 2>/dev/null
    local output
    if [ -z "$PASS" ]; then
        output=$(sudo nmcli device wifi connect "$SSID" 2>&1)
    else
        output=$(sudo nmcli device wifi connect "$SSID" password "$PASS" 2>&1)
    fi

    if echo "$output" | grep -q "successfully"; then
        dialog --msgbox "Connected to $SSID!" 5 40
    else
        sudo rm -f "/etc/NetworkManager/system-connections/$SSID.nmconnection"
        dialog --msgbox "Failed to connect to $SSID" 5 40
    fi
}

WifiSaved() {
    declare -a aoptions
    while IFS= read -r ssid; do
        [ -z "$ssid" ] && continue
        aoptions+=("$ssid" ".")
    done < <(ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | rev | cut -c 14- | rev)

    if [ ${#aoptions[@]} -eq 0 ]; then
        dialog --msgbox "No saved connections." 5 30
        return
    fi

    local cur_ap=$(iw dev wlan0 info 2>/dev/null | grep ssid | cut -c 7-30)

    local choice
    choice=$(dialog \
        --backtitle "Connected: $cur_ap" \
        --title "Saved WiFi Networks" \
        --no-collapse --clear --cancel-label "Back" \
        --menu "" 15 50 8 "${aoptions[@]}" 2>&1 >/dev/tty1)

    [ $? -ne 0 ] && return

    progress "Connecting to $choice..."
    sudo nmcli con down "$cur_ap" 2>/dev/null
    sleep 1
    local output=$(sudo nmcli con up "$choice" 2>&1)

    if echo "$output" | grep -q "successfully"; then
        dialog --msgbox "Connected to $choice!" 5 40
    else
        dialog --msgbox "Failed to connect to $choice" 5 40
    fi
}

WifiDelete() {
    declare -a deloptions
    while IFS= read -r ssid; do
        [ -z "$ssid" ] && continue
        deloptions+=("$ssid" ".")
    done < <(ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | rev | cut -c 14- | rev)

    if [ ${#deloptions[@]} -eq 0 ]; then
        dialog --msgbox "No saved connections." 5 30
        return
    fi

    local choice
    choice=$(dialog \
        --title "Delete WiFi Connection" \
        --no-collapse --clear --cancel-label "Back" \
        --menu "" 15 50 8 "${deloptions[@]}" 2>&1 >/dev/tty1)

    [ $? -ne 0 ] && return

    dialog --yesno "Remove $choice?" 6 40
    if [ $? -eq 0 ]; then
        sudo rm -f "/etc/NetworkManager/system-connections/$choice.nmconnection"
        dialog --msgbox "Removed $choice" 5 30
    fi
}

WifiInfo() {
    local INFO="/tmp/wifi_info.txt"
    local gateway=$(ip r | grep default | awk '{print $3}')
    local currentip=$(ip -f inet addr show wlan0 2>/dev/null | sed -En 's/.*inet ([0-9.]+).*/\1/p')
    local currentssid=$(iw dev wlan0 info 2>/dev/null | grep ssid | cut -c 7-30)
    if [ -z "$currentssid" ]; then
        currentssid=$(sudo nmcli -t -f name,device connection show --active 2>/dev/null | grep wlan0 | cut -d: -f1)
    fi

    echo "SSID: $currentssid" > "$INFO"
    echo "IP: $currentip" >> "$INFO"
    echo "Gateway: $gateway" >> "$INFO"
    echo "" >> "$INFO"
    if systemctl is-active ssh >/dev/null 2>&1; then echo "SSH: ACTIVE" >> "$INFO"; fi
    if systemctl is-active smbd >/dev/null 2>&1; then echo "Samba: ACTIVE" >> "$INFO"; fi

    dialog --title "Network Info" --textbox "$INFO" 12 50
}

WifiMenu() {
    while true; do
        local cur_ap=$(iw dev wlan0 info 2>/dev/null | grep ssid | cut -c 7-30)
        [ -z "$cur_ap" ] && cur_ap="Not connected"

        local choice
        choice=$(dialog \
            --backtitle "Connected: $cur_ap" \
            --title "WiFi Manager" \
            --clear --cancel-label "Back" \
            --menu "" 15 50 6 \
            1 "Scan & Connect" \
            2 "Saved Networks" \
            3 "Delete Connection" \
            4 "Network Info" \
            5 "Back" 2>&1 >/dev/tty1)

        [ $? -ne 0 ] && return
        case $choice in
            1) WifiScan ;;
            2) WifiSaved ;;
            3) WifiDelete ;;
            4) WifiInfo ;;
            5) return ;;
        esac
    done
}

# =============================================================================
# BLUETOOTH FUNCTIONS
# =============================================================================

EnableBt() {
    progress "Enabling Bluetooth..."
    if ! lsmod | grep -q "$WIFI_MOD"; then
        sudo modprobe "$WIFI_MOD" rtw_btcoex_enable=1
        sleep 2
    fi

    echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend >/dev/null
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1
    sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    sleep 3
    sudo modprobe "$BT_MOD" mp_drv_mode=1 2>/dev/null
    sleep 3

    if ! hciconfig -a | grep -q "BD Address"; then
        progress "Retrying BT..."
        sudo rmmod "$BT_MOD" 2>/dev/null
        sudo rmmod "$COEX_MOD" 2>/dev/null
        sleep 1
        PerformPythonReset
        sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1
    sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    sleep 3
        sudo modprobe "$BT_MOD" mp_drv_mode=1 2>/dev/null
        sleep 3
    fi

    sudo systemctl start bluetooth 2>/dev/null
    sudo hciconfig hci0 up 2>/dev/null
    StartServices
    dialog --msgbox "Bluetooth Enabled!" 5 30
}

DisableBt() {
    progress "Disabling Bluetooth..."
    StopServices
    rm -f /home/ark/.asoundrc
    rm -f /etc/asound.conf
    ResetRetroArch
    sudo hciconfig hci0 down 2>/dev/null
    sudo systemctl stop bluetooth 2>/dev/null
    sudo rmmod "$BT_MOD" 2>/dev/null
    sudo rmmod "$COEX_MOD" 2>/dev/null
    sleep 1
    dialog --msgbox "Bluetooth Disabled." 5 30
}

EnableBoth() {
    progress "Enabling WiFi + Bluetooth..."
    sudo rm -f "$BLACKLIST_FILE"
    sudo depmod -a

    if ! lsmod | grep -q "$WIFI_MOD"; then
        sudo modprobe "$WIFI_MOD" rtw_btcoex_enable=1
    fi
    sleep 2

    sudo systemctl start wpa_supplicant 2>/dev/null
    sudo systemctl start NetworkManager 2>/dev/null
    sudo nmcli radio wifi on 2>/dev/null
    sudo ifconfig wlan0 up 2>/dev/null

    echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend >/dev/null
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1
    sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    sleep 3
    sudo modprobe "$BT_MOD" mp_drv_mode=1 2>/dev/null
    sleep 3

    if ! hciconfig -a | grep -q "BD Address"; then
        progress "Retrying BT..."
        sudo rmmod "$BT_MOD" 2>/dev/null
        sudo rmmod "$COEX_MOD" 2>/dev/null
        sleep 1
        PerformPythonReset
        sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1
    sleep 2
    echo "1-1" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    sleep 3
        sudo modprobe "$BT_MOD" mp_drv_mode=1 2>/dev/null
        sleep 3
    fi

    sudo systemctl start bluetooth 2>/dev/null
    sudo hciconfig hci0 up 2>/dev/null
    StartServices
    sleep 1
    dialog --msgbox "WiFi + Bluetooth Enabled!" 5 40
}

TurnOffAll() {
    progress "Turning off all radios..."
    StopServices
    ResetRetroArch
    rm -f /home/ark/.asoundrc
    rm -f /etc/asound.conf

    sudo hciconfig hci0 down 2>/dev/null
    sudo systemctl stop bluetooth 2>/dev/null
    sudo systemctl stop NetworkManager 2>/dev/null
    sudo systemctl stop ssh.service
    sudo systemctl stop smbd

    sudo rmmod "$BT_MOD" 2>/dev/null
    sudo rmmod "$COEX_MOD" 2>/dev/null
    sudo rmmod "$WIFI_MOD" 2>/dev/null
    sleep 1
    dialog --msgbox "All Radios OFF." 5 30
}

ConnectSaved() {
    local paired=$(bluetoothctl devices Paired 2>/dev/null)
    if [ -z "$paired" ]; then dialog --msgbox "No paired devices." 6 40; return; fi
    DEVICES=()
    while read -r line; do
        MAC=$(echo "$line" | awk '{print $2}')
        NAME=$(echo "$line" | cut -d' ' -f3-)
        [ -z "$MAC" ] && continue
        DEVICES+=("$MAC" "$NAME")
    done <<< "$paired"
    dialog --menu "Select Device:" 15 60 5 "${DEVICES[@]}" 2> /tmp/bt_choice
    if [ $? -eq 0 ]; then
        TARGET_MAC=$(cat /tmp/bt_choice)
        dialog --infobox "Connecting..." 5 40
        ( echo "connect $TARGET_MAC"; sleep 5; echo "exit"; ) | bluetoothctl > /tmp/bt_conn.log
        if grep -q "Connection successful" /tmp/bt_conn.log; then
             dialog --msgbox "Connected!" 5 30
        else
             dialog --msgbox "Failed." 5 30
        fi
    fi
}

PairDevice() {
    sudo hciconfig hci0 down 2>/dev/null; sudo hciconfig hci0 up 2>/dev/null
    dialog --infobox "Scanning BT devices..." 5 40
    sudo hcitool scan > /tmp/bt_scan_raw.txt
    tail -n +2 /tmp/bt_scan_raw.txt > /tmp/bt_devices.txt
    TARGET_MAC=""
    if [ -s /tmp/bt_devices.txt ]; then
        DEVICES=()
        while read -r line; do
            MAC=$(echo "$line" | awk '{print $1}')
            NAME=$(echo "$line" | cut -d' ' -f2-)
            DEVICES+=("$MAC" "$NAME")
        done < /tmp/bt_devices.txt
        dialog --menu "Select:" 15 60 5 "${DEVICES[@]}" 2> /tmp/bt_choice
        if [ $? -eq 0 ]; then TARGET_MAC=$(cat /tmp/bt_choice); fi
    else
        dialog --yesno "Nothing found. Enter MAC?" 6 40
        if [ $? -eq 0 ]; then
            pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null
            TARGET_MAC=$(osk --backtitle "Bluetooth" --inputbox "Enter MAC address" 2>&1 >/dev/null)
            /opt/inttools/gptokeyb -1 "Absolute.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
        fi
    fi
    if [ -n "$TARGET_MAC" ]; then
        dialog --infobox "Pairing..." 10 50
        ( echo "power on"; echo "agent on"; echo "default-agent"; echo "scan on"; sleep 2; echo "pair $TARGET_MAC"; sleep 10; echo "trust $TARGET_MAC"; sleep 2; echo "connect $TARGET_MAC"; sleep 5; echo "exit" ) | bluetoothctl > /tmp/bt_pair.log
        if grep -q "Connection successful" /tmp/bt_pair.log || grep -q "Connected: yes" /tmp/bt_pair.log; then
             dialog --msgbox "SUCCESS!" 6 40
        else
             tail -n 10 /tmp/bt_pair.log > /tmp/bt_error_show.txt
             dialog --textbox /tmp/bt_error_show.txt 15 60
        fi
    fi
}

ManageDevices() {
    local paired=$(bluetoothctl devices Paired 2>/dev/null)
    if [ -z "$paired" ]; then dialog --msgbox "No paired devices." 6 40; return; fi
    DEVICES=()
    while read -r line; do
        MAC=$(echo "$line" | awk '{print $2}')
        NAME=$(echo "$line" | cut -d' ' -f3-)
        [ -z "$MAC" ] && continue
        DEVICES+=("$MAC" "$NAME")
    done <<< "$paired"
    dialog --menu "Manage:" 15 60 5 "${DEVICES[@]}" 2> /tmp/bt_choice
    if [ $? -eq 0 ]; then
        TARGET_MAC=$(cat /tmp/bt_choice)
        dialog --menu "Action:" 12 50 2 1 "Connect" 2 "Forget" 2> /tmp/dev_action
        case $(cat /tmp/dev_action) in
            1) ConnectSaved ;;
            2) ( echo "remove $TARGET_MAC"; sleep 2; echo "exit"; ) | bluetoothctl; dialog --msgbox "Removed." 5 30 ;;
        esac
    fi
}

BtMenu() {
    while true; do
        local choice
        choice=$(dialog \
            --title "Bluetooth Manager" \
            --clear --cancel-label "Back" \
            --menu "" 15 50 5 \
            1 "Pair New Device" \
            2 "Connect Saved" \
            3 "Manage Devices" \
            4 "Test Sound" \
            5 "Back" 2>&1 >/dev/tty1)

        [ $? -ne 0 ] && return
        case $choice in
            1) PairDevice ;;
            2) ConnectSaved ;;
            3) ManageDevices ;;
            4) TestSound ;;
            5) return ;;
        esac
    done
}

TestSound() {
    dialog --infobox "Playing sound (Loopback)..." 4 40
    timeout 5 aplay -D plughw:Loopback,0,0 /usr/share/sounds/alsa/Front_Center.wav 2>/dev/null
    if [ $? -eq 0 ]; then
        dialog --msgbox "Sound OK! (Sent to Loopback)" 5 40
    else
        dialog --msgbox "Failed." 5 30
    fi
}

# =============================================================================
# REMOTE SERVICES
# =============================================================================

EnableRemote() {
    progress "Enabling Remote Services..."
    sudo systemctl start NetworkManager-wait-online
    sudo systemctl start smbd
    sudo systemctl start nmbd
    sudo systemctl start ssh.service
    sudo filebrowser -a 0.0.0.0 -p 80 -d /home/ark/.config/filebrowser.db -r / &
    IP=$(ip route | awk '/src/ { print $9 }')
    dialog --msgbox "Remote Enabled.\nIP: $IP" 10 50
}

DisableRemote() {
    progress "Disabling Remote Services..."
    sudo systemctl stop smbd
    sudo systemctl stop nmbd
    sudo systemctl stop ssh.service
    sudo pkill filebrowser
    dialog --msgbox "Remote Disabled." 5 40
}

RemoteMenu() {
    dialog --menu "Remote:" 12 50 2 1 "Enable" 2 "Disable" 2> /tmp/r
    case $(cat /tmp/r) in 1) EnableRemote ;; 2) DisableRemote ;; esac
}

# =============================================================================
# TERMINAL
# =============================================================================

TerminalMenu() {
    while true; do
        local choice
        choice=$(dialog \
            --title "Quick Terminal" \
            --clear --cancel-label "Back" \
            --menu "" 20 60 12 \
            1 "dmesg (last 50)" \
            2 "lsusb" \
            3 "lsmod" \
            4 "ip addr" \
            5 "hciconfig -a" \
            6 "df -h" \
            7 "top (snapshot)" \
            8 "dmesg | grep 8723" \
            9 "amixer" \
            10 "Custom command (OSK)" \
            11 "Back" 2>&1 >/dev/tty1)

        [ $? -ne 0 ] && return
        case $choice in
            1) RunCmd "dmesg | tail -50" ;;
            2) RunCmd "lsusb" ;;
            3) RunCmd "lsmod" ;;
            4) RunCmd "ip addr" ;;
            5) RunCmd "hciconfig -a" ;;
            6) RunCmd "df -h" ;;
            7) RunCmd "top -bn1 | head -20" ;;
            8) RunCmd "dmesg | grep -i 8723" ;;
            9) RunCmd "amixer" ;;
            10) CustomCmd ;;
            11) return ;;
        esac
    done
}

RunCmd() {
    eval "$1" > /tmp/term_output.txt 2>&1
    dialog --title "$1" --textbox /tmp/term_output.txt 20 70
}

CustomCmd() {
    pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null
    sleep 0.5
    osk "Enter command" 2> /tmp/custom_cmd
    local osk_ret=$?
    sleep 0.5
    /opt/inttools/gptokeyb -1 "Absolute.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
    sleep 0.5
    sudo setfont /usr/share/consolefonts/Lat7-Terminus20x10.psf.gz
    printf "\033c" > /dev/tty1
    if [ $osk_ret -eq 0 ]; then
        local CMD=$(cat /tmp/custom_cmd)
        if [ ! -z "$CMD" ]; then RunCmd "$CMD"; fi
    fi
}

# =============================================================================
# SYSTEM INFO
# =============================================================================

ShowInfo() {
    GetStatus
    INFO_FILE="/tmp/sys_info.txt"
    echo "=== Status: $STATUS ===" > "$INFO_FILE"
    echo "" >> "$INFO_FILE"

    if [ "$WIFI_ON" == "ON" ]; then
        echo "--- WiFi ---" >> "$INFO_FILE"
        ip addr show wlan0 2>/dev/null | grep "inet " >> "$INFO_FILE"
        local ssid=$(iw dev wlan0 info 2>/dev/null | grep ssid | cut -c 7-30)
        echo "SSID: $ssid" >> "$INFO_FILE"
        echo "" >> "$INFO_FILE"
    fi

    if [ "$BT_ON" == "ON" ]; then
        echo "--- Bluetooth ---" >> "$INFO_FILE"
        hciconfig hci0 | grep "BD Address" >> "$INFO_FILE"
        echo "Alsaloop:" >> "$INFO_FILE"
        pgrep -a alsaloop >> "$INFO_FILE" 2>/dev/null
        echo "Paired:" >> "$INFO_FILE"
        bluetoothctl devices Paired >> "$INFO_FILE" 2>/dev/null
        echo "" >> "$INFO_FILE"
    fi

    if systemctl is-active ssh >/dev/null 2>&1; then
        echo "SSH: ACTIVE" >> "$INFO_FILE"
        IP=$(ip route | awk '/src/ { print $9 }')
        echo "IP: $IP" >> "$INFO_FILE"
    fi

    dialog --title "System Info" --textbox "$INFO_FILE" 20 70
}

# =============================================================================
# MAIN MENU
# =============================================================================

/opt/inttools/gptokeyb -1 "Absolute.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
GPTOKEYB_PID=$!

while true; do
  GetStatus
  dialog --backtitle "$STATUS" --title "R36S Manager v71" --clear --cancel-label "Exit" \
         --menu "" 20 60 12 \
         1 "WiFi + BT (Both ON)" \
         2 "WiFi Only" \
         3 "Bluetooth Only" \
         4 "WiFi Manager" \
         5 "Bluetooth Manager" \
         6 "Remote Services" \
         7 "System Info" \
         8 "Terminal" \
         9 "Turn OFF All" \
         10 "Exit" 2> /tmp/menuchoice
  ret=$?
  choice=$(cat /tmp/menuchoice)
  if [ $ret -ne 0 ]; then kill -9 $GPTOKEYB_PID 2>/dev/null; ExitMenu; fi
  case $choice in
    1) EnableBoth ;;
    2) DisableBt; EnableWifi ;;
    3) DisableWifi; EnableBt ;;
    4) WifiMenu ;;
    5) BtMenu ;;
    6) RemoteMenu ;;
    7) ShowInfo ;;
    8) TerminalMenu ;;
    9) TurnOffAll ;;
    10) kill -9 $GPTOKEYB_PID 2>/dev/null; ExitMenu ;;
  esac
done
