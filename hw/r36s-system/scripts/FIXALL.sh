#!/bin/bash
# ZACHRANNY SKRIPT PRO R36S BT/WIFI/AUDIO
# Tento skript opravi vsechny soubory v /opt/system/

LOG="/roms/fix_log.txt"
echo "Zacinam opravu..." > "$LOG"

# 1. Vytvoreni monitoru hlasitosti
echo "Vytvarim bt_volume_monitor.sh..." >> "$LOG"
cat << 'EOF' > /opt/system/bt_volume_monitor.sh
#!/bin/bash
LAST_VOL=""
while true; do
    if ! pgrep bluealsad > /dev/null; then
        sleep 5
        continue
    fi
    CURRENT_VOL=$(amixer get Playback 2>/dev/null | grep -m1 -o "[0-9]*%" | head -n1)
    if [ -z "$CURRENT_VOL" ]; then
        sleep 1
        continue
    fi
    if [ "$CURRENT_VOL" != "$LAST_VOL" ]; then
        BT_CTRL=$(amixer -D bluealsa scontrols 2>/dev/null | head -n1 | cut -d"'" -f2)
        if [ ! -z "$BT_CTRL" ]; then
            amixer -D bluealsa sset "$BT_CTRL" "$CURRENT_VOL" > /dev/null 2>&1
            LAST_VOL="$CURRENT_VOL"
        fi
    fi
    sleep 0.2
done
EOF

# 2. Vytvoreni mute skriptu
echo "Vytvarim mute_toggle.sh..." >> "$LOG"
cat << 'EOF' > /opt/system/mute_toggle.sh
#!/bin/bash
amixer set Playback toggle 2>/dev/null
exit 0
EOF

# 3. Vytvoreni hlavniho menu (WiFi BT Toggle.sh)
echo "Opravuji WiFi BT Toggle.sh..." >> "$LOG"
cat << 'EOF' > "/opt/system/WiFi BT Toggle.sh"
#!/bin/bash
# WiFi & BT Toggle Menu v38 (Volume Fix + AutoRestore)

sudo chmod 666 /dev/tty1
sudo chmod 666 /dev/uinput
export TERM=linux

# Log file
LOGFILE="/roms/bt_debug.log"

# Kill old gptokeyb instances
pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null

# Clean screen
printf "\033c" > /dev/tty1

# Constants
WIFI_MOD="8821cu"
BT_MOD="rtk_btusb"
COEX_MOD="rtk_btcoex"
USB_DEV="/sys/bus/usb/devices/1-1"
BLACKLIST_FILE="/etc/modprobe.d/block-wifi-temp.conf"
VOL_MONITOR="/opt/system/bt_volume_monitor.sh"

# Python Reset Tool
RESET_TOOL="/tmp/usbreset.py"
cat <<END > "$RESET_TOOL"
import fcntl, os, sys
USBDEVFS_RESET = 21780
try:
    path = sys.argv[1]
    fd = os.open(path, os.O_WRONLY)
    fcntl.ioctl(fd, USBDEVFS_RESET, 0)
    os.close(fd)
except: pass
END

progress() {
    dialog --infobox "\n$1\n\nPlease wait..." 6 50
}

ExitMenu() {
  printf "\033c" > /dev/tty1
  pgrep -f gptokeyb | sudo xargs kill -9 2>/dev/null
  exit 0
}

GetStatus() {
  if hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"; then
      STATUS="BLUETOOTH"
  elif lsmod | grep -q "$WIFI_MOD"; then
      STATUS="WIFI"
  else
      STATUS="OFF"
  fi
}

StartBlueALSA() {
    progress "Starting BlueALSA..."
    sudo pkill bluealsad
    sleep 1
    sudo /usr/bin/bluealsad -p a2dp-source &
    sleep 2
    
    # Start Volume Monitor
    sudo pkill -f bt_volume_monitor.sh
    if [ -f "$VOL_MONITOR" ]; then
        sudo chmod +x "$VOL_MONITOR"
        nohup "$VOL_MONITOR" > /dev/null 2>&1 &
    fi
}

UpdateAlsaConfig() {
    MAC=$1
    if [ -z "$MAC" ]; then return; fi
    
    cat <<END > /home/ark/.asoundrc
defaults.bluealsa.interface "hci0"
defaults.bluealsa.profile "a2dp"
defaults.bluealsa.delay 10000

pcm.!default {
    type plug
    slave.pcm {
        type bluealsa
        device "$MAC"
        profile "a2dp"
    }
}
ctl.!default {
    type bluealsa
}
END
    chown ark:ark /home/ark/.asoundrc
    sudo alsactl restore 2>/dev/null
}

PerformPythonReset() {
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ] && [ -f "$dev/idProduct" ]; then
            vid=$(cat "$dev/idVendor")
            pid=$(cat "$dev/idProduct")
            if [ "$vid" == "0bda" ] && [ "$pid" == "c820" ]; then
                bus=$(cat "$dev/busnum")
                devnum=$(cat "$dev/devnum")
                python3 "$RESET_TOOL" $(printf "/dev/bus/usb/%03d/%03d" "$bus" "$devnum")
                break
            fi
        fi
    done
}

# === REMOTE SERVICES ===
EnableRemote() {
    progress "Enabling Remote Services..."
    sudo systemctl enable NetworkManager-wait-online
    sudo systemctl start NetworkManager-wait-online
    
    GW=$(ip route | awk '/default/ { print $3 }')
    if [ ! -z "$GW" ]; then
        sudo timedatectl set-ntp 1 &
        sudo systemctl start smbd
        sudo systemctl start nmbd
        sudo systemctl start ssh.service
        sudo filebrowser -a 0.0.0.0 -p 80 -d /home/ark/.config/filebrowser.db -r / &
        
        IP=$(ip route | awk '/src/ { print $9 }')
        dialog --msgbox "Remote Services ENABLED.\n\nIP: $IP\nSSH/SFTP Port: 22\nFileBrowser Port: 80" 10 50
    else
        dialog --msgbox "Error: No Network Connection.\nCheck WiFi settings." 6 50
    fi
}

DisableRemote() {
    progress "Disabling Remote Services..."
    sudo systemctl disable NetworkManager-wait-online
    sudo systemctl stop NetworkManager-wait-online
    sudo timedatectl set-ntp 0
    sudo systemctl stop smbd
    sudo systemctl stop nmbd
    sudo systemctl stop ssh.service
    sudo pkill filebrowser
    dialog --msgbox "Remote Services DISABLED." 5 40
}

RemoteMenu() {
    dialog --menu "Remote Services Manager:" 12 50 2 \
        1 "Enable Services" \
        2 "Disable Services" 2> /tmp/remote_choice
    
    case $(cat /tmp/remote_choice) in
        1) EnableRemote ;;
        2) DisableRemote ;;
    esac
}

SwitchToWifi() {
  progress "Switching to WiFi..."
  
  sudo pkill -f bt_volume_monitor.sh
  
  rm -f /home/ark/.asoundrc
  sudo rm -f "$BLACKLIST_FILE"
  sudo depmod -a
  
  sudo hciconfig hci0 down 2>/dev/null
  sudo systemctl stop bluetooth 2>/dev/null
  sudo pkill bluealsad
  
  sudo rmmod "$BT_MOD" 2>/dev/null
  sudo rmmod "$COEX_MOD" 2>/dev/null
  sudo rmmod btusb 2>/dev/null
  
  sleep 1
  PerformPythonReset
  sleep 2
  
  if ! lsmod | grep -q "$WIFI_MOD"; then
      sudo modprobe "$WIFI_MOD"
  fi
  
  sudo systemctl start wpa_supplicant 2>/dev/null
  sudo systemctl start NetworkManager 2>/dev/null
  sudo nmcli radio wifi on 2>/dev/null
  sudo ifconfig wlan0 up 2>/dev/null
  sleep 3
  
  dialog --msgbox "WiFi Enabled!" 5 30
}

SwitchToBt() {
  progress "Switching to Bluetooth..."
  
  echo "blacklist $WIFI_MOD" | sudo tee "$BLACKLIST_FILE" >/dev/null
  echo "install $WIFI_MOD /bin/true" | sudo tee -a "$BLACKLIST_FILE" >/dev/null
  sudo depmod -a
  
  sudo systemctl stop NetworkManager 2>/dev/null
  sudo systemctl stop wpa_supplicant 2>/dev/null
  sudo ifconfig wlan0 down 2>/dev/null
  sudo rmmod "$WIFI_MOD" 2>/dev/null
  
  sudo rmmod "$BT_MOD" 2>/dev/null
  sudo rmmod "$COEX_MOD" 2>/dev/null
  
  sleep 1
  
  echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend >/dev/null
  
  PerformPythonReset
  sleep 1
  if [ -d "$USB_DEV" ]; then
      echo "1-1" > /sys/bus/usb/drivers/usb/unbind 2>/dev/null
      sleep 2
      echo "1-1" > /sys/bus/usb/drivers/usb/bind 2>/dev/null
      sleep 3
  else
      sleep 3
  fi
  
  sudo modprobe "$BT_MOD" mp_drv_mode=1
  sleep 3
  
  if ! hciconfig -a | grep -q "BD Address"; then
      progress "Retrying..."
      sudo rmmod "$BT_MOD" 2>/dev/null
      sudo rmmod "$COEX_MOD" 2>/dev/null
      sleep 1
      PerformPythonReset
      sleep 2
      sudo modprobe "$BT_MOD" mp_drv_mode=1
      sleep 3
  fi
  
  sudo systemctl start bluetooth 2>/dev/null
  sudo hciconfig hci0 up 2>/dev/null
  
  StartBlueALSA
  
  dialog --msgbox "Bluetooth Enabled!" 5 30
}

TurnOffAll() {
  echo "blacklist $WIFI_MOD" | sudo tee "$BLACKLIST_FILE" >/dev/null
  sudo depmod -a
  
  sudo pkill -f bt_volume_monitor.sh
  sudo systemctl stop NetworkManager 2>/dev/null
  sudo systemctl stop wpa_supplicant 2>/dev/null
  sudo systemctl stop bluetooth 2>/dev/null
  sudo pkill bluealsad
  
  sudo systemctl stop ssh.service
  sudo systemctl stop smbd
  sudo pkill filebrowser
  
  sudo rmmod "$WIFI_MOD" 2>/dev/null
  sudo rmmod "$BT_MOD" 2>/dev/null
  sudo rmmod "$COEX_MOD" 2>/dev/null
  sudo rmmod btusb 2>/dev/null
  
  sleep 1
  dialog --msgbox "All Radios OFF." 5 30
}

ConnectSaved() {
    echo "devices Paired" | bluetoothctl > /tmp/bt_paired.txt
    if [ ! -s /tmp/bt_paired.txt ]; then dialog --msgbox "No paired devices." 6 40; return; fi
    DEVICES=()
    while read -r line; do
        MAC=$(echo "$line" | awk '{print $2}')
        NAME=$(echo "$line" | cut -d' ' -f3-)
        DEVICES+=("$MAC" "$NAME")
    done < /tmp/bt_paired.txt
    dialog --menu "Select Device:" 15 60 5 "${DEVICES[@]}" 2> /tmp/bt_choice
    if [ $? -eq 0 ]; then
        TARGET_MAC=$(cat /tmp/bt_choice)
        dialog --infobox "Connecting..." 5 40
        ( echo "connect $TARGET_MAC"; sleep 5; echo "exit"; ) | bluetoothctl > /tmp/bt_conn.log
        if grep -q "Connection successful" /tmp/bt_conn.log; then 
            UpdateAlsaConfig "$TARGET_MAC"
            dialog --msgbox "Connected & Audio Set!" 5 40
        else 
            dialog --msgbox "Failed." 5 30
        fi
    fi
}

PairDevice() {
    sudo hciconfig hci0 down; sudo hciconfig hci0 up
    dialog --infobox "Scanning..." 5 40
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
        if [ $? -eq 0 ]; then dialog --inputbox "MAC:" 8 40 2> /tmp/mac_input; TARGET_MAC=$(cat /tmp/mac_input); fi
    fi
    if [ -n "$TARGET_MAC" ]; then
        dialog --infobox "Pairing $TARGET_MAC..." 10 50
        ( echo "power on"; echo "agent on"; echo "default-agent"; echo "scan on"; sleep 2; echo "pair $TARGET_MAC"; sleep 10; echo "trust $TARGET_MAC"; sleep 2; echo "connect $TARGET_MAC"; sleep 5; echo "exit" ) | bluetoothctl > /tmp/bt_pair.log
        if grep -q "Connection successful" /tmp/bt_pair.log || grep -q "Connected: yes" /tmp/bt_pair.log; then 
            UpdateAlsaConfig "$TARGET_MAC"
            dialog --msgbox "SUCCESS!" 6 40
        else 
            tail -n 10 /tmp/bt_pair.log > /tmp/bt_error_show.txt
            dialog --textbox /tmp/bt_error_show.txt 15 60
        fi
    fi
}

ManageDevices() {
    echo "devices Paired" | bluetoothctl > /tmp/bt_paired.txt
    if [ ! -s /tmp/bt_paired.txt ]; then dialog --msgbox "No paired devices." 6 40; return; fi
    DEVICES=()
    while read -r line; do
        MAC=$(echo "$line" | awk '{print $2}')
        NAME=$(echo "$line" | cut -d' ' -f3-)
        DEVICES+=("$MAC" "$NAME")
    done < /tmp/bt_paired.txt
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

TestSound() {
    dialog --infobox "Playing sound..." 4 40
    aplay -D bluealsa /usr/share/sounds/alsa/Front_Center.wav 2>/dev/null
    if [ $? -eq 0 ]; then
        dialog --msgbox "BlueALSA OK!" 5 30
    else
        dialog --msgbox "Failed." 5 30
    fi
}

ShowInfo() {
    GetStatus
    INFO_FILE="/tmp/sys_info.txt"
    echo "Mode: $STATUS" > "$INFO_FILE"
    if [[ "$STATUS" == _"WIFI"_ ]]; then
        iwconfig wlan0 | grep "Signal level" >> "$INFO_FILE"
        ip addr show wlan0 | grep "inet " >> "$INFO_FILE"
        
        if systemctl is-active ssh >/dev/null; then
            echo "" >> "$INFO_FILE"
            echo "Remote: ACTIVE (SSH, SMB)" >> "$INFO_FILE"
        fi
    fi
    if [[ "$STATUS" == _"BLUETOOTH"_ ]]; then
        hciconfig hci0 | grep "BD Address" >> "$INFO_FILE"
        echo "" >> "$INFO_FILE"
        echo "BlueALSA:" >> "$INFO_FILE"
        pgrep -a bluealsad >> "$INFO_FILE"
        echo "" >> "$INFO_FILE"
        echo "Volume Sync:" >> "$INFO_FILE"
        pgrep -a -f bt_volume_monitor.sh >> "$INFO_FILE"
    fi
    dialog --textbox "$INFO_FILE" 20 70
}

# --- Start Main Loop ---
/opt/inttools/gptokeyb -1 "WiFi BT Toggle.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
GPTOKEYB_PID=$!

while true; do
  GetStatus
  dialog --backtitle "Mode: $STATUS" --title "R36S Manager v38 (Volume Fix)" --clear --cancel-label "Exit" \
         --menu "Select Action:" 18 60 8 \
         1 "WiFi Mode" 2 "Bluetooth Mode" 3 "Pair New Device" 4 "Manage Saved Devices" \
         5 "Remote Services (SSH/Samba)" 6 "System Status / Info" 7 "Test Sound" 8 "Turn OFF All" 9 "Exit" 2> /tmp/menuchoice
  ret=$?
  choice=$(cat /tmp/menuchoice)
  if [ $ret -ne 0 ]; then kill -9 $GPTOKEYB_PID 2>/dev/null; ExitMenu; fi
  case $choice in
    1) if [ "$STATUS" == "WIFI" ]; then dialog --msgbox "WiFi ON" 5 30; else SwitchToWifi; fi ;;
    2) if [ "$STATUS" == "BLUETOOTH" ]; then dialog --msgbox "BT ON" 5 30; else SwitchToBt; fi ;;
    3) PairDevice ;;
    4) ManageDevices ;;
    5) if [[ "$STATUS" == _"WIFI"_ ]]; then RemoteMenu; else dialog --msgbox "WiFi Only!" 5 30; fi ;;
    6) ShowInfo ;;
    7) TestSound ;;
    8) TurnOffAll ;;
    9) kill -9 $GPTOKEYB_PID 2>/dev/null; ExitMenu ;;
  esac
done
EOF

# 4. Oprava prav a formatovani (dos2unix workaround)
echo "Nastavuji prava a cistim formatovani..." >> "$LOG"

# Odstraneni \r znaku z vytvorenych souboru
sed -i 's/\r$//' /opt/system/bt_volume_monitor.sh
sed -i 's/\r$//' /opt/system/mute_toggle.sh
sed -i 's/\r$//' "/opt/system/WiFi BT Toggle.sh"

chmod +x /opt/system/bt_volume_monitor.sh
chmod +x /opt/system/mute_toggle.sh
chmod +x "/opt/system/WiFi BT Toggle.sh"

echo "HOTOVO. Restartuji EmulationStation..." >> "$LOG"
sync
sleep 2

# Dialog pro uzivatele
dialog --msgbox "Oprava dokoncena!\nVsechny skripty byly obnoveny.\nNyni se restartuje EmulationStation." 10 50

# Restart ES
touch /tmp/es-restart

exit 0