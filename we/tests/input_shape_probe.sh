#!/bin/bash
# Run input_shape_probe.exe on a private Xvfb and print, next to its own output,
# how many rectangles the X input shape of its window has (0 = clicks go through).
#
#   input_shape_probe.sh <wine command...> -- [layered]
#
# e.g. with a runner and a prefix:
#   WINEPREFIX=/tmp/pfx input_shape_probe.sh <runner>/files/bin/wine -- layered
# Needs Xvfb and python-xlib.
set -e
cd "$(dirname "$0")"
cmd=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do cmd+=("$1"); shift; done
shift || true
log=$(mktemp)
Xvfb :123 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XV=$!
trap 'kill $XV 2>/dev/null; rm -f "$log"' EXIT
sleep 1
DISPLAY=:123 "${cmd[@]}" input_shape_probe.exe "$log" "$@" >/dev/null 2>&1 &
PROBE=$!
for i in $(seq 60); do sleep 1; grep -q after "$log" 2>/dev/null && break; done
DISPLAY=:123 python3 - <<'PY'
from Xlib import display
from Xlib.ext import shape
d = display.Display(); root = d.screen().root
for w in root.query_tree().children:
    n = w.get_full_property(d.intern_atom("_NET_WM_NAME"), 0)
    if n and n.value == b"input_shape_probe":
        print("X input shape rectangles:", len(w.shape_get_rectangles(shape.SK.Input).rectangles))
PY
cat "$log"
# the window stays up a few more seconds; don't wait for a launcher that lingers
for i in $(seq 15); do kill -0 $PROBE 2>/dev/null || break; sleep 1; done
kill $PROBE 2>/dev/null || true
