#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build directly on the R36S (dArkOS, aarch64). Run over SSH as user ark.
#   ./tools/build.sh            # dependencies + build
#   JOBS=1 ./tools/build.sh     # when short on RAM
set -e
cd "$(dirname "$0")/.."
JOBS=${JOBS:-2}

need=""
for p in g++ cmake git pkg-config; do command -v $p > /dev/null || need="$need $p"; done
pkg-config --exists alsa || need="$need libasound2-dev"
pkg-config --exists sdl2 || need="$need libsdl2-dev"
pkg-config --exists lilv-0 || need="$need liblilv-dev"
if [ -n "$need" ]; then
  echo "Installing:$need"
  sudo apt-get update
  sudo apt-get install -y build-essential cmake git pkg-config libasound2-dev libsdl2-dev liblilv-dev lv2-dev
fi

./tools/fetch_deps.sh

ARCH=$(uname -m)
A35=OFF
[ "$ARCH" = "aarch64" ] && A35=ON
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNAMFX_TARGET_A35=$A35
cmake --build build -j "$JOBS"
echo
echo "Done: build/namfx"
echo "Test without a guitar:  ./build/namfx --offline data/wav/nam_example_di.wav /tmp/out.wav --preset 02_Plexi_Crunch.json"
