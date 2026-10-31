#!/bin/bash
# WiFi & BT Toggle Menu v43 (Soldered Edition)

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
BLACKLIST_FILE="/etc/modprobe.d/block-wifi-temp.conf"
VOL_MONITOR="/opt/system/bt_volume_monitor.sh"

# Python Reset Tool (Nuclear option)
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

log() {
    echo "$(date '+%H:%M:%S') - $1" >> "$LOGFILE"
}

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
    
    # Start Volume Monitor if exists
    sudo pkill -f bt_volume_monitor.sh
    if [ -f "$VOL_MONITOR" ]; then
        sudo nohup "$VOL_MONITOR" > /dev/null 2>&1 &
    fi
}

UpdateAlsaConfig() {
    MAC=$1
    if [ -z "$MAC" ]; then return; fi
    log "Configuring ALSA for $MAC"
    
    cat <<EOF > /home/ark/.asoundrc
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
EOF
    chown ark:ark /home/ark/.asoundrc
    sudo alsactl restore 2>/dev/null
}

# Najde dongle a provede tvrdý reset
PerformResetSequence() {
    # 1. Najdeme zařízení
    DEV_PATH=""
    USB_BUS_ID=""
    
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ]; then
            if [ "$(cat "$dev/idVendor")" == "0bda" ] && [ "$(cat "$dev/idProduct")" == "c820" ]; then
                bus=$(cat "$dev/busnum")
                devnum=$(cat "$dev/devnum")
                DEV_PATH=$(printf "/dev/bus/usb/%03d/%03d" "$bus" "$devnum")
                USB_BUS_ID=$(basename "$dev")
                break
            fi
        fi
    done

    if [ -z "$DEV_PATH" ]; then
        log "Dongle not found!"
        return
    fi

    log "Resetting $USB_BUS_ID at $DEV_PATH"

    # 2. IOCTL Reset (Python)
    python3 "$RESET_TOOL" "$DEV_PATH"
    
    # 3. Kernel Unbind/Bind (Soft Replug)
    if [ -d "/sys/bus/usb/drivers/usb" ]; then
        echo "$USB_BUS_ID" > /sys/bus/usb/drivers/usb/unbind 2>/dev/null
        sleep 1
        echo "$USB_BUS_ID" > /sys/bus/usb/drivers/usb/bind 2>/dev/null
    fi
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
        dialog --msgbox "Remote Services ENABLED.\n\nIP: $IP\nSSH Port: 22" 10 50
    else
        dialog --msgbox "Error: No Network." 6 50
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
    dialog --menu "Remote Services:" 12 50 2 1 "Enable" 2 "Disable" 2> /tmp/remote_choice
    case $(cat /tmp/remote_choice) in
        1) EnableRemote ;; 2) DisableRemote ;;
    esac
}

SwitchToWifi() {
  progress "Switching to WiFi..."
  
  # Clean up
  sudo pkill -f bt_volume_monitor.sh
  rm -f /home/ark/.asoundrc
  sudo rm -f "$BLACKLIST_FILE"
  sudo depmod -a
  
  # Kill BT
  sudo hciconfig hci0 down 2>/dev/null
  sudo systemctl stop bluetooth 2>/dev/null
  sudo pkill bluealsad
  sudo rmmod "$BT_MOD" 2>/dev/null
  sudo rmmod "$COEX_MOD" 2>/dev/null
  sudo rmmod btusb 2>/dev/null
  
  sleep 1
  # Reset, aby se čip probral z BT módu
  PerformResetSequence
  sleep 3
  
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

  # 1. Zablokovat WiFi (aby se po resetu nenacetla)
  echo "blacklist $WIFI_MOD" | sudo tee "$BLACKLIST_FILE" >/dev/null
  echo "install $WIFI_MOD /bin/true" | sudo tee -a "$BLACKLIST_FILE" >/dev/null
  sudo depmod -a
  
  # 2. Kill WiFi
  sudo systemctl stop NetworkManager 2>/dev/null
  sudo systemctl stop wpa_supplicant 2>/dev/null
  sudo ifconfig wlan0 down 2>/dev/null
  sudo rmmod "$WIFI_MOD" 2>/dev/null
  
  # 3. Kill BT Residues
  sudo rmmod "$BT_MOD" 2>/dev/null
  sudo rmmod "$COEX_MOD" 2>/dev/null
  
  sleep 1
  
  # 4. Power Tweak
  echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend >/dev/null
  
  # 5. HARD RESET SEQUENCE
  PerformResetSequence
  sleep 3
  
  # 6. Load Driver
  sudo modprobe "$BT_MOD" mp_drv_mode=1
  sleep 3
  
  # 7. Check & Retry (Pokud nenabehl, zkusime to znova)
  if ! hciconfig -a | grep -q "BD Address"; then
      progress "First attempt failed. Retrying hard..."
      
      sudo rmmod "$BT_MOD" 2>/dev/null
      sudo rmmod "$COEX_MOD" 2>/dev/null
      sleep 1
      
      # Druhý reset
      PerformResetSequence
      sleep 3
      
      sudo modprobe "$BT_MOD" mp_drv_mode=1
      sleep 3
  fi
  
  sudo systemctl start bluetooth 2>/dev/null
  sudo hciconfig hci0 up 2>/dev/null
  
  StartBlueALSA
  
  if hciconfig -a | grep -q "BD Address"; then
      dialog --msgbox "Bluetooth Enabled!" 5 30
  else
      dialog --msgbox "ERROR: BT Failed.\nTry rebooting." 5 40
  fi
}

TurnOffAll() {
  echo "blacklist $WIFI_MOD" | sudo tee "$BLACKLIST_FILE" >/dev/null
  sudo depmod -a
  sudo pkill -f bt_volume_monitor.sh
  sudo systemctl stop NetworkManager 2>/dev/null
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

# --- MENU FUNCTIONS ---
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
    if [[ "$STATUS" == *"WIFI"* ]]; then
        iwconfig wlan0 | grep "Signal level" >> "$INFO_FILE"
        ip addr show wlan0 | grep "inet " >> "$INFO_FILE"
        if systemctl is-active ssh >/dev/null; then
            echo "" >> "$INFO_FILE"
            echo "Remote: ACTIVE (SSH, SMB)" >> "$INFO_FILE"
        fi
    fi
    if [[ "$STATUS" == *"BLUETOOTH"* ]]; then
        hciconfig hci0 | grep "BD Address" >> "$INFO_FILE"
        echo "" >> "$INFO_FILE"
        echo "BlueALSA:" >> "$INFO_FILE"
        pgrep -a bluealsad >> "$INFO_FILE"
    fi
    dialog --textbox "$INFO_FILE" 20 70
}

# --- Start Main Loop ---
/opt/inttools/gptokeyb -1 "WiFi BT Toggle.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
GPTOKEYB_PID=$!

while true; do
  GetStatus
  dialog --backtitle "Mode: $STATUS" --title "R36S Manager v43" --clear --cancel-label "Exit" \
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
    5) if [[ "$STATUS" == *"WIFI"* ]]; then RemoteMenu; else dialog --msgbox "WiFi Only!" 5 30; fi ;;
    6) ShowInfo ;;
    7) TestSound ;;
    8) TurnOffAll ;;
    9) kill -9 $GPTOKEYB_PID 2>/dev/null; ExitMenu ;;
  esac
done