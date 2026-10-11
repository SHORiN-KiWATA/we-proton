#!/usr/bin/env python3
"""Run layered_child_probe.exe and read the screen at each SAMPLE step.

    DISPLAY=:N layered_child_grab.py <wine> layered_child_probe.exe [pos]

Pixels are read from the X root window (python-xlib), so this needs an X
server without compositing, e.g. Xvfb. It also lists the mapped top-level X
windows at each step, to show whether the child ended up in a window of its own.
"""
import subprocess, sys
from Xlib import display, X

W, H = 320, 200
SPOTS = [("left, opaque green", W // 4, H // 2),
         ("right top, 50% red", 3 * W // 4, H // 4),
         ("right bottom, transparent", 3 * W // 4, 3 * H // 4)]

d = display.Display()
root = d.screen().root

def pixel(x, y):
    img = root.get_image(x, y, 1, 1, X.ZPixmap, 0xffffffff)
    b, g, r = img.data[0], img.data[1], img.data[2]
    return r, g, b

def toplevels():
    out = []
    for w in root.query_tree().children:
        a = w.get_attributes()
        if a.map_state != X.IsViewable:
            continue
        g = w.get_geometry()
        if g.width <= 1 and g.height <= 1:
            continue
        out.append(f"{w.id:#x} {g.width}x{g.height}+{g.x}+{g.y}{' override-redirect' if a.override_redirect else ''}")
    return out

proc = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, text=True)
for line in proc.stdout:
    line = line.rstrip()
    if not line.startswith("SAMPLE "):
        print(line, flush=True)
        continue
    _, step, x, y = line.split()
    x, y = int(x), int(y)
    print(f"{step}: host at {x},{y}")
    for name, dx, dy in SPOTS:
        print("  %-26s r=%3d g=%3d b=%3d" % (name, *pixel(x + dx, y + dy)))
    # where the child lands if its parent-relative position is taken as a screen position
    print("  at the screen origin:     " + "  ".join("r=%3d g=%3d b=%3d" % pixel(dx, dy) for _, dx, dy in SPOTS))
    for t in toplevels():
        print("  X window", t)
    sys.stdout.flush()
proc.wait()
