#!/bin/bash
# In headless gamescope (an XWayland server), run input_shape_probe.exe, read the
# X input shape of its window once it is WS_EX_TRANSPARENT (0 rectangles = clicks
# go through), then take the focus away and give it back, and read it again.
#
#   input_shape_probe.sh <wine command...>
#
# e.g. a script that runs an exe with a runner and a prefix. Needs gamescope and
# python-xlib.
set -e
if [ -z "$INPUT_SHAPE_PROBE_INNER" ]; then
    INPUT_SHAPE_PROBE_INNER=1 exec gamescope --backend headless -- "$0" "$@" 2>/dev/null
fi
cd "$(dirname "$0")"
log=$(mktemp)
"$@" "$PWD/input_shape_probe.exe" "Z:$log" >/dev/null 2>&1 &
PROBE=$!
for i in $(seq 90); do sleep 1; grep -q after "$log" 2>/dev/null && break; done
python3 - <<'PY'
import time
from Xlib import display, X
from Xlib.ext import shape
d = display.Display(); root = d.screen().root
print("XWAYLAND:", d.query_extension("XWAYLAND") is not None)
w = next(w for w in root.query_tree().children
         if (lambda n: n and n.value == b"input_shape_probe")(w.get_full_property(d.intern_atom("_NET_WM_NAME"), 0)))
print("after WS_EX_TRANSPARENT: input rectangles", len(w.shape_get_rectangles(shape.SK.Input).rectangles))
root.set_input_focus(X.RevertToPointerRoot, X.CurrentTime); d.sync(); time.sleep(0.5)
w.set_input_focus(X.RevertToParent, X.CurrentTime); d.sync(); time.sleep(1.5)
print("after it got the focus again: input rectangles", len(w.shape_get_rectangles(shape.SK.Input).rectangles))
PY
cat "$log"; rm -f "$log"
for i in $(seq 15); do kill -0 $PROBE 2>/dev/null || break; sleep 1; done
kill $PROBE 2>/dev/null || true
