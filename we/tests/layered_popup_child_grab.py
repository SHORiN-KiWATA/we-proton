#!/usr/bin/env python3
"""Follow the output of layered_popup_child_probe.exe and read the host window
at each SAMPLE step.

    DISPLAY=:N layered_popup_child_grab.py <probe output file>

The pixels are read from the host's top-level X window (python-xlib), which
includes what its child X windows show. Works where top-level windows are
redirected (XWayland) or on a server without compositing. Also lists the other
mapped top-level X windows, to show whether the popup got an X window of its own.
"""
import re, sys, time
from Xlib import display, X

W, H = 600, 400
d = display.Display()
root = d.screen().root
NAME = d.intern_atom("_NET_WM_NAME")

def title(w):
    try:
        p = w.get_full_property(NAME, 0)
        return p.value.decode(errors="replace") if p else ""
    except Exception:
        return ""

def toplevels():
    out = []
    def walk(w, depth):
        for c in w.query_tree().children:
            try:
                a = c.get_attributes(); g = c.get_geometry()
            except Exception:
                continue
            if a.map_state != X.IsViewable or (g.width <= 1 and g.height <= 1):
                continue
            t = title(c)
            if t or depth:
                out.append((c, t, g, a))
            elif depth == 0:
                walk(c, 1)  # a reparenting window manager's frame
    walk(root, 0)
    return out

def grab(step):
    host = None
    for w, t, g, a in toplevels():
        if t == "layered_popup_child_probe":
            host = (w, g)
    if not host:
        print(f"{step}: host X window not found"); return
    w, g = host
    # the client area is the bottom right W x H of the X window (it may have a frame of Wine's own)
    ox, oy = g.width - W, g.height - H
    cols = []
    for i in range(1, 12, 2):
        img = w.get_image(ox + i * W // 12, oy + H // 2, 1, 1, X.ZPixmap, 0xffffffff)
        b, gr, r = img.data[0], img.data[1], img.data[2]
        cols.append("%2d/12 r=%3d g=%3d b=%3d" % (i, r, gr, b))
    print(f"{step}: host X window {g.width}x{g.height} depth {g.depth}")
    for c in cols: print("  " + c)
    for o, t, og, a in toplevels():
        if o.id != w.id:
            print(f"  other X window {og.width}x{og.height}+{og.x}+{og.y} depth {og.depth} '{t}'"
                  f"{' override-redirect' if a.override_redirect else ''}")
    sys.stdout.flush()

path = sys.argv[1]
pos = 0
deadline = time.time() + 120
done = False
while not done and time.time() < deadline:
    try:
        with open(path, "rb") as f:
            f.seek(pos)
            chunk = f.read()
    except FileNotFoundError:
        chunk = b""
    if b"\n" not in chunk:
        time.sleep(0.2); continue
    lines = chunk[:chunk.rfind(b"\n") + 1]
    pos += len(lines)
    for line in lines.decode(errors="replace").splitlines():
        line = line.rstrip()
        m = re.search(r"client area (\d+) x (\d+)", line)
        if m: W, H = int(m.group(1)), int(m.group(2))
        if line.startswith("SAMPLE "):
            step = line.split()[1]
            time.sleep(1.2)
            grab(step)
            if step == "popup-destroyed": done = True
        elif line:
            print(line, flush=True)
