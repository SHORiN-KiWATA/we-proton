#!/bin/bash
# For 0056: WM_ACTIVATEAPP and friends on foreground changes, under Wine.
#
#   we/tests/activateapp_probe.sh <runner dir> <output file>
#
# Runs activateapp_probe.exe (PROBE=activateapp_probe32.exe for 32-bit) in the
# Steam Linux Runtime 4.0 with WoW64, a new prefix under ~/.cache (removed
# afterwards, KEEP=1 keeps it) and a private Xvfb, with activateapp_x11_helper.py
# as a minimal window manager doing the native X11 focus changes. Compare with
# activateapp_probe.windows.txt (an interactive Windows 11 session).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
RUNNER=$(cd "$1" && pwd)
OUTFILE=$(realpath -m "$2")
ENTRY=$HOME/.local/share/umu/steamrt4/_v2-entry-point
WORK=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/we-activateapp.XXXXXX")
mkdir -p "$WORK/prefix" "$WORK/steam"
DPY=151
while [ -e /tmp/.X11-unix/X$DPY ] || [ -e /tmp/.X$DPY-lock ]; do DPY=$((DPY + 1)); done
Xvfb :$DPY -screen 0 1280x800x24 -nolisten tcp > /dev/null 2>&1 &
XVFB=$!
HELPER=
cleanup() {
    [ -n "$HELPER" ] && kill $HELPER 2> /dev/null || true
    kill $XVFB 2> /dev/null || true
    wait $XVFB 2> /dev/null || true
    if [ "${KEEP:-0}" = 1 ]; then echo "prefix kept in $WORK"; else rm -rf "$WORK"; fi
}
trap cleanup EXIT
sleep 1
echo "Xvfb :$DPY pid $XVFB"
LOG=$WORK/stderr.log
run() {
    STEAM_COMPAT_DATA_PATH=$WORK/prefix STEAM_COMPAT_CLIENT_INSTALL_PATH=$WORK/steam \
    SteamAppId=0 SteamGameId=0 UMU_ID=wegame-launcher UMU_USE_STEAM=0 PROTON_USE_WOW64=1 \
    PYTHONDONTWRITEBYTECODE=1 WINEDEBUG=${WDBG:--all} \
    DISPLAY=:$DPY timeout 180 "$ENTRY" --verb=waitforexitandrun -- \
        "$RUNNER/proton" waitforexitandrun 'C:\windows\system32\cmd.exe' /c "$1" \
        >> "$LOG" 2>&1
}
run "exit 0"
T=$WORK/prefix/pfx/drive_c/aa
mkdir -p "$T"
cp "$HERE"/activateapp_probe*.exe "$T/"
python3 "$HERE/activateapp_x11_helper.py" --display :$DPY --sync-dir "$T" > "$WORK/helper.log" 2>&1 &
HELPER=$!
run "cd /d C:\\aa && ${PROBE:-activateapp_probe.exe} --x11-sync C:\\aa > C:\\aa\\out.txt 2>&1" || echo "(exit $?)" >> "$LOG"
tr -d '\r' < "$T/out.txt" > "$OUTFILE"
kill $HELPER 2> /dev/null || true; wait $HELPER 2> /dev/null || true
HELPER=
echo "wrote $OUTFILE ($(wc -l < "$OUTFILE") lines)"
