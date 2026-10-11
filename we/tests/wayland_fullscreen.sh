#!/bin/bash
# For 0050 and 0052: fullscreen and focus with Wine's Wayland driver under niri.
# Needs a running niri (NIRI_SOCKET, WAYLAND_DISPLAY; a nested niri works, see
# we/fixes/0050-winewayland-compositor-fullscreen/README.md) and a runner. Prints
# the window size niri reports after each step; the output's size means
# fullscreen. Build the probes first (we/tests/build.sh).
#
#   we/tests/wayland_fullscreen.sh <runner dir>
#
# 1. notepad, fullscreen by niri (like a keybinding), then focus another window
#    and back, then niri ends the fullscreen
# 2. wayland_fullscreen_probe.exe makes itself fullscreen and back (like a game)
# 3. wayland_fullscreen_d3d9.exe, a game's Direct3D 9 window in windowed mode
#    (50 pixels lower than the monitor, puts its size back when resized),
#    fullscreen by niri and back; uses the runner's DXVK
# 4. with the grid overview of niri forks that have one: two notepads, the grid
#    is opened and the focus moved between them; screenshots go to
#    $WAYLAND_FULLSCREEN_SHOTS if set (the grid has to stay open)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$1" && pwd)/files
PFX=$(mktemp -d)
trap 'WINEPREFIX=$PFX "$R/bin/wineserver" -k 2> /dev/null; rm -rf "$PFX"' EXIT
export WINEPREFIX=$PFX WINE_GRAPHICS_DRIVER=wayland WINEDEBUG=-all DISPLAY=
size() { niri msg -j windows | python3 -c "import json,sys; [print('%dx%d' % tuple(w['layout']['window_size']) + (' floating' if w['is_floating'] else '')) for w in json.load(sys.stdin) if w.get('app_id') == '$1']" | head -1; }
winid() { niri msg -j windows | python3 -c "import json,sys; [print(w['id']) for w in json.load(sys.stdin) if w.get('app_id') == '$1']" | head -1; }
prev=$(niri msg -j focused-window | python3 -c "import json,sys; w = json.load(sys.stdin); print(w['id'] if w else '')")

"$R/bin/wine" notepad > /dev/null 2>&1 &
for i in $(seq 60); do id=$(winid notepad.exe); [ -n "$id" ] && break; sleep 1; done
sleep 1
echo "notepad:            $(size notepad.exe)"
niri msg action fullscreen-window --id "$id"; sleep 1
echo "niri fullscreen:    $(size notepad.exe)"
if [ -n "$prev" ]; then
    niri msg action focus-window --id "$prev"; sleep 1
    echo "focus elsewhere:    $(size notepad.exe)"
    niri msg action focus-window --id "$id"; sleep 1
    echo "focus back:         $(size notepad.exe)"
fi
niri msg action fullscreen-window --id "$id"; sleep 1
echo "niri ends it:       $(size notepad.exe)"
"$R/bin/wineserver" -k; sleep 1

"$R/bin/wine" "$HERE/wayland_fullscreen_probe.exe" > /dev/null 2>&1 &
out=""
for i in $(seq 16); do out="$out $(size wayland_fullscreen_probe.exe || true)"; sleep 0.5; done
echo "probe (every 0.5 s, fullscreen itself at 2 s, back at 5 s):$out"
"$R/bin/wineserver" -k 2> /dev/null || true; sleep 1

d3d9=$PFX/drive_c/windows/system32/d3d9.dll
cp "$R/lib/wine/dxvk/x86_64-windows/d3d9.dll" "$d3d9"
h=$(niri msg -j focused-output | python3 -c "import json,sys; print(json.load(sys.stdin)['logical']['height'] - 50)")
WINEDLLOVERRIDES=d3d9=n "$R/bin/wine" "$HERE/wayland_fullscreen_d3d9.exe" 20 "$h" > /dev/null 2>&1 &
for i in $(seq 60); do id=$(winid wayland_fullscreen_d3d9.exe); [ -n "$id" ] && break; sleep 1; done
sleep 1
echo "d3d9 window:        $(size wayland_fullscreen_d3d9.exe)"
niri msg action fullscreen-window --id "$id"; sleep 1
echo "niri fullscreen:    $(size wayland_fullscreen_d3d9.exe)"
sleep 2
echo "  2 s later:        $(size wayland_fullscreen_d3d9.exe)"
niri msg action fullscreen-window --id "$id"; sleep 1
echo "niri ends it:       $(size wayland_fullscreen_d3d9.exe)"
"$R/bin/wineserver" -k 2> /dev/null || true; sleep 1

if niri msg action open-grid-overview 2> /dev/null; then
    niri msg action close-grid-overview
    "$R/bin/wine" notepad > /dev/null 2>&1 &
    sleep 1
    "$R/bin/wine" notepad > /dev/null 2>&1 &
    for i in $(seq 60); do [ "$(niri msg -j windows | grep -c '"notepad.exe"')" = 2 ] && break; sleep 1; done
    sleep 1
    shots=${WAYLAND_FULLSCREEN_SHOTS:-}
    niri msg action open-grid-overview; sleep 1
    [ -z "$shots" ] || niri msg action screenshot-screen -d false --stdout > "$shots/grid-open.png"
    niri msg action focus-column-left; sleep 1
    [ -z "$shots" ] || niri msg action screenshot-screen -d false --stdout > "$shots/grid-left.png"
    niri msg action focus-column-right; sleep 1
    [ -z "$shots" ] || niri msg action screenshot-screen -d false --stdout > "$shots/grid-right.png"
    niri msg action close-grid-overview
    if [ -n "$shots" ]; then echo "grid overview:      screenshots in $shots"
    else echo "grid overview:      set WAYLAND_FULLSCREEN_SHOTS to look at it"; fi
fi
[ -z "$prev" ] || niri msg action focus-window --id "$prev"
