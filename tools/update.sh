#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Peter Hudak
#
# Updates namfx on the console from a GitHub release (no compiling).
#   tools/update.sh            install the latest release
#   tools/update.sh v0.4.0     install a specific tag
#   tools/update.sh --check    only say whether a newer release exists
# Your data/ (presets, models, settings) is not touched: the release tarball has no data/.
# The old binary is kept as build/namfx.old. Needs: curl, tar, and the runtime libraries (libasound2 libsdl2-2.0-0 liblilv-0-0).
set -e
REPO="${NAMFX_REPO:-Dirtysaurus/namfx}"
APP="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$APP/.release"

case "$(uname -m)" in aarch64|arm64) ;; *) echo "This script installs the aarch64 build; this machine is $(uname -m)." >&2; exit 1;; esac
command -v curl >/dev/null || { echo "curl is missing: sudo apt install curl" >&2; exit 1; }

latest() {  # the tag of the newest release, from the redirect of /releases/latest
  curl -fsSIL -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest" | sed 's|.*/tag/||'
}

ARG="$1"
if [ "$ARG" = "--check" ]; then
  NEW=$(latest)
  CUR=$(cat "$STAMP" 2>/dev/null || echo "unknown")
  echo "installed: $CUR, latest: $NEW"
  [ "$CUR" = "$NEW" ] && echo "Up to date." || echo "A newer release is available: run tools/update.sh"
  exit 0
fi

TAG="${ARG:-$(latest)}"
case "$TAG" in v[0-9]*) ;; *) echo "Could not find a release tag (got '$TAG')." >&2; exit 1;; esac
if pgrep -x namfx >/dev/null 2>&1; then echo "namfx is running - quit it first (Select + Start), then run this again." >&2; exit 1; fi
if [ "$(cat "$STAMP" 2>/dev/null)" = "$TAG" ]; then echo "$TAG is already installed."; exit 0; fi

FREE=$(df -Pk "$APP" | awk 'NR==2{print $4}')
[ "${FREE:-0}" -gt 60000 ] || { echo "Less than 60 MB free in $APP." >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/namfx-update.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
URL="https://github.com/$REPO/releases/download/$TAG/namfx-$TAG-aarch64.tar.gz"
echo "Downloading $TAG ..."
curl -fL --progress-bar -o "$TMP/namfx.tgz" "$URL"
tar -tzf "$TMP/namfx.tgz" >/dev/null   # a broken download stops here, before anything is replaced
tar -xzf "$TMP/namfx.tgz" -C "$TMP"
[ -x "$TMP/namfx/build/namfx" ] || { echo "The archive has no namfx/build/namfx." >&2; exit 1; }
"$TMP/namfx/build/namfx" --print-engines >/dev/null 2>&1 || { echo "The new binary does not start (missing libraries? sudo apt install libasound2 libsdl2-2.0-0 liblilv-0-0)." >&2; exit 1; }

[ -f "$APP/build/namfx" ] && cp -f "$APP/build/namfx" "$APP/build/namfx.old"
mkdir -p "$APP/build"
cp -a "$TMP/namfx/." "$APP/"
echo "$TAG" > "$STAMP"

# the launcher in EmulationStation -> Ports, if it is installed
for D in /roms/ports /roms2/ports; do
  if [ -f "$D/NAMFX.sh" ]; then sudo cp "$APP/ports/NAMFX.sh" "$D/NAMFX.sh" && sudo chmod +x "$D/NAMFX.sh" && echo "Launcher updated: $D/NAMFX.sh"; fi
done
echo "Installed $TAG. The previous binary is build/namfx.old. Start namfx from EmulationStation -> Ports."
