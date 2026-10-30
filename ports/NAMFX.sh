#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# EmulationStation (Ports) launcher for dArkOS.
# Goes into /roms/ports/; the app lives in ~/namfx (ext4, for executable files).
# Quit: Select + Start (or Start -> Quit).

APP="/home/ark/namfx"
LOG="$APP/namfx.log"
GOV=/sys/devices/system/cpu/cpufreq/policy0/scaling_governor

[ -x "$APP/build/namfx" ] || { echo "namfx is not built - run $APP/tools/build.sh" > /dev/tty1; sleep 4; exit 1; }

OLD_GOV=$(cat "$GOV" 2>/dev/null)
echo performance | sudo tee "$GOV" > /dev/null 2>&1

# Route the RK817 input to the mic path (control name as in dArkOS; harmless if missing).
# The sound card (USB interface or this internal codec) is chosen inside namfx: System -> Audio input.
amixer -q sset 'Capture MIC Path' 'Main Mic' 2>/dev/null || true

cd "$APP" || exit 1
printf "\033c" > /dev/tty1 2>/dev/null
sudo SDL_VIDEO_DRIVER=KMSDRM ./build/namfx \
  --data "$APP/data" --web "$APP/web" --fonts "$APP/fonts" --fx "$APP/fx" \
  --block 128 \
  "$@" > "$LOG" 2>&1

# files uploaded via the web were created by root - give them back to ark
sudo chown -R ark:ark "$APP/data" "$LOG" 2>/dev/null

[ -n "$OLD_GOV" ] && echo "$OLD_GOV" | sudo tee "$GOV" > /dev/null 2>&1
printf "\033c" > /dev/tty1 2>/dev/null
