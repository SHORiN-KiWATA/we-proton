#!/bin/bash
# For 0060: runs shutdown_block_probe.exe with a runner in the Steam Linux Runtime
# 4.0 (WoW64, new prefix under ~/.cache, private Xvfb) and writes its output.
# Compare with shutdown_block_probe.windows.txt.
#
#   we/tests/shutdown_block_probe.sh <runner dir> <output file>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
RUNNER=$(cd "$1" && pwd)
OUTFILE=$(realpath -m "$2")
ENTRY=$HOME/.local/share/umu/steamrt4/_v2-entry-point
WORK=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/we-shutdownblock.XXXXXX")
mkdir -p "$WORK/prefix" "$WORK/steam"
DPY=151
while [ -e /tmp/.X11-unix/X$DPY ] || [ -e /tmp/.X$DPY-lock ]; do DPY=$((DPY + 1)); done
Xvfb :$DPY -screen 0 1280x800x24 -nolisten tcp > /dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2> /dev/null || true; wait $XVFB 2> /dev/null || true; rm -rf "$WORK"' EXIT
sleep 1
run() {
    STEAM_COMPAT_DATA_PATH=$WORK/prefix STEAM_COMPAT_CLIENT_INSTALL_PATH=$WORK/steam \
    SteamAppId=0 SteamGameId=0 UMU_ID=wegame-launcher UMU_USE_STEAM=0 PROTON_USE_WOW64=1 \
    PYTHONDONTWRITEBYTECODE=1 WINEDEBUG=-all WAYLAND_DISPLAY= \
    DISPLAY=:$DPY timeout 180 "$ENTRY" --verb=waitforexitandrun -- \
        "$RUNNER/proton" waitforexitandrun 'C:\windows\system32\cmd.exe' /c "$1" \
        >> "$WORK/stderr.log" 2>&1
}
run "exit 0"
T=$WORK/prefix/pfx/drive_c/aa
mkdir -p "$T"
cp "$HERE/shutdown_block_probe.exe" "$T/"
run "cd /d C:\\aa && shutdown_block_probe.exe > C:\\aa\\out.txt 2>&1" || true
tr -d '\r' < "$T/out.txt" > "$OUTFILE"
echo "wrote $OUTFILE ($(wc -l < "$OUTFILE") lines)"
