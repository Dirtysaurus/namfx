#!/bin/bash
LOG="/roms/bt_auto_test.log"
echo "=== Auto Test $(date) ===" > "$LOG"

# Kdo drží event3 (volume keys)?
echo "--- fuser event3 ---" >> "$LOG"
fuser -v /dev/input/event3 >> "$LOG" 2>&1

# Kdo drží všechny eventy?
echo "--- fuser all events ---" >> "$LOG"
fuser -v /dev/input/event* >> "$LOG" 2>&1

# gptokeyb procesy
echo "--- gptokeyb ---" >> "$LOG"
pgrep -a gptokeyb >> "$LOG" 2>&1

# Zkus zabít gptokeyb a otestovat volume
echo "--- killing gptokeyb ---" >> "$LOG"
sudo pkill gptokeyb
sleep 1
echo "gptokeyb after kill: $(pgrep -c gptokeyb)" >> "$LOG"

# Test volume po zabití gptokeyb
amixer -c 0 get Playback >> "$LOG" 2>&1
echo "NOW PRESS VOLUME BUTTONS FOR 5 SECONDS" >> "$LOG"
sleep 5
amixer -c 0 get Playback >> "$LOG" 2>&1

echo "=== DONE ===" >> "$LOG"
