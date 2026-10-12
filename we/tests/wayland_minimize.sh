#!/bin/bash
# Minimize and restore a presenting Direct3D 11 window with Wine's Wayland driver
# under niri. Needs a niri fork with minimize support (the unminimize-window
# action) in NIRI_SOCKET / WAYLAND_DISPLAY (a nested niri works, see
# we/fixes/0050-winewayland-compositor-fullscreen/README.md), and a runner.
#
#   we/tests/wayland_minimize.sh <runner dir> [normal|popup]
#
# minimize_restore_probe.exe presents with vsync from its message loop and
# minimizes itself at 3 s; niri restores it at 6 s. The probe prints its frame
# count every half second: after the restore it has to keep growing.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$1" && pwd)/files
KIND=${2:-normal}
# the prefix is a few hundred MB; /tmp may have a quota
mkdir -p "${XDG_CACHE_HOME:-$HOME/.cache}"
PFX=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/we-wayland-minimize.XXXXXX")
trap 'WINEPREFIX=$PFX "$R/bin/wineserver" -k 2> /dev/null; rm -rf "$PFX"' EXIT
export WINEPREFIX=$PFX WINE_GRAPHICS_DRIVER=wayland WINEDEBUG=${WINEDEBUG:--all} DISPLAY=
"$R/bin/wine" wineboot -i > /dev/null 2>&1
"$R/bin/wineserver" -w
for dll in d3d11 dxgi; do
    cp "$R/lib/wine/dxvk/x86_64-windows/$dll.dll" "$PFX/drive_c/windows/system32/$dll.dll"
done
OUT=$PFX/probe.out
WINEDLLOVERRIDES="d3d11,dxgi=n" "$R/bin/wine" "$HERE/minimize_restore_probe.exe" 3 12 $([ "$KIND" = popup ] && echo popup) > "$OUT" 2>&1 &
sleep 6.5
niri msg action unminimize-window
echo "== niri unminimize-window at $(date +%T.%N | cut -c1-12)" >> "$OUT"
wait || true
cat "$OUT"
