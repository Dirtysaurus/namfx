#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copies the launcher into EmulationStation -> Ports. Restart ES (or the console) afterwards.
set -e
cd "$(dirname "$0")/.."
DEST=/roms/ports
[ -d "$DEST" ] || DEST=/roms2/ports
sudo cp ports/NAMFX.sh "$DEST/NAMFX.sh"
sudo chmod +x "$DEST/NAMFX.sh"
echo "Launcher: $DEST/NAMFX.sh (app: $(pwd))"
if [ "$(pwd)" != "/home/ark/namfx" ]; then
  echo "NOTE: NAMFX.sh expects /home/ark/namfx - edit APP in $DEST/NAMFX.sh"
fi
