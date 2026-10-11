#!/bin/bash
# In headless gamescope (an XWayland server), run input_shape_probe.exe. Once its
# window is click-through (WS_EX_LAYERED | WS_EX_TRANSPARENT), read the window's
# X input shape (0 rectangles = the pointer goes through) and whether Wine still
# asks for its pointer motion, then move the pointer (XTest) from outside onto
# the window and let the probe report whether its low-level mouse hook saw it.
#
#   input_shape_probe.sh [--topmost] <wine command...>
#
# <wine command...> is e.g. a script that runs an exe with a runner and a prefix.
# --topmost makes the window WS_EX_TOPMOST, like an overlay. Needs gamescope and
# python-xlib.
set -e
if [ -z "$INPUT_SHAPE_PROBE_INNER" ]; then
    INPUT_SHAPE_PROBE_INNER=1 exec gamescope --backend headless -- "$0" "$@" 2>/dev/null
fi
cd "$(dirname "$0")"
mode=
if [ "$1" = --topmost ]; then mode=topmost; shift; fi
log=$(mktemp)
"$@" "$PWD/input_shape_probe.exe" "Z:$log" $mode >/dev/null 2>&1 &
PROBE=$!
for i in $(seq 90); do sleep 1; grep -q transparent "$log" 2>/dev/null && break; done
sleep 0.5
python3 - <<'PY'
import time
from Xlib import display, X
from Xlib.ext import shape, xtest
d = display.Display(); root = d.screen().root
print("XWAYLAND:", d.query_extension("XWAYLAND") is not None)
w = next(w for w in root.query_tree().children
         if (lambda n: n and n.value == b"input_shape_probe")(w.get_full_property(d.intern_atom("_NET_WM_NAME"), 0)))
def state():
    rects = len(w.shape_get_rectangles(shape.SK.Input).rectangles)
    motion = bool(w.get_attributes().all_event_masks & X.PointerMotionMask)
    return "input rectangles %d, pointer motion selected %s" % (rects, motion)
print("click-through:", state())
g = w.get_geometry(); pos = w.translate_coords(root, 0, 0)
x0, y0 = -pos.x, -pos.y
cx, cy = x0 + g.width // 2, y0 + g.height // 2
xtest.fake_input(d, X.MotionNotify, x=max(x0 - 60, 0), y=max(y0 - 60, 0)); d.sync(); time.sleep(0.3)
for i in range(1, 11):
    xtest.fake_input(d, X.MotionNotify, x=x0 - 60 + (cx - x0 + 60) * i // 10, y=y0 - 60 + (cy - y0 + 60) * i // 10)
    d.sync(); time.sleep(0.05)
time.sleep(1)
print("after the pointer came back:", state())
PY
for i in $(seq 15); do grep -q result "$log" 2>/dev/null && break; sleep 1; done
cat "$log"; rm -f "$log"
for i in $(seq 15); do kill -0 $PROBE 2>/dev/null || break; sleep 1; done
kill $PROBE 2>/dev/null || true
