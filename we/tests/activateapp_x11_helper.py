#!/usr/bin/env python3
"""Host side of activateapp_probe --x11-sync.

Acts as a minimal ICCCM window manager on the private Xvfb (sets WM_STATE on
map/unmap, honours ConfigureRequest; no EWMH, so Wine takes the
XSetInputFocus / WM_TAKE_FOCUS paths), and performs focus changes the way a
window manager would when the probe asks for them: it waits for
<dir>/<step>.req, acts, then writes <dir>/<step>.done.

  away    map a native (non-Wine) X11 window once and XSetInputFocus to it
  back    WM_TAKE_FOCUS to the Wine window titled "A (P.main)"
  back_c  WM_TAKE_FOCUS to the Wine window titled "C (child)"
  quit    exit
"""
import argparse
import os
import sys
import time

from Xlib import X, display, protocol, error


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--display", required=True)
    ap.add_argument("--sync-dir", required=True)
    ap.add_argument("--timeout", type=float, default=170)
    args = ap.parse_args()

    d = display.Display(args.display)
    scr = d.screen()
    root = scr.root
    WM_STATE = d.intern_atom("WM_STATE")
    WM_PROTOCOLS = d.intern_atom("WM_PROTOCOLS")
    WM_TAKE_FOCUS = d.intern_atom("WM_TAKE_FOCUS")
    TIME_ATOM = d.intern_atom("_ACTPROBE_TIME")
    STRING = d.intern_atom("STRING")

    def log(msg):
        print(f"[x11_helper {time.monotonic():.3f}] {msg}", flush=True)

    root.change_attributes(event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
    d.sync()

    native = root.create_window(700, 400, 200, 120, 0, scr.root_depth, X.InputOutput, X.CopyFromParent,
                                background_pixel=scr.white_pixel,
                                event_mask=X.StructureNotifyMask | X.PropertyChangeMask)
    native.set_wm_name("native-x11")
    state = {"mapped": False, "time": None}

    def set_wm_state(w, value):
        try:
            w.change_property(WM_STATE, WM_STATE, 32, [value, 0])
        except error.XError:
            pass

    def process(ev):
        if ev.type == X.MapRequest:
            set_wm_state(ev.window, 1)  # NormalState
            ev.window.map()
        elif ev.type == X.ConfigureRequest:
            kw = {}
            m = ev.value_mask
            if m & X.CWX: kw["x"] = ev.x
            if m & X.CWY: kw["y"] = ev.y
            if m & X.CWWidth: kw["width"] = ev.width
            if m & X.CWHeight: kw["height"] = ev.height
            if m & X.CWBorderWidth: kw["border_width"] = ev.border_width
            if m & X.CWStackMode: kw["stack_mode"] = ev.stack_mode
            if m & X.CWSibling and m & X.CWStackMode: kw["sibling"] = ev.above
            try:
                ev.window.configure(**kw)
            except error.XError:
                pass
        elif ev.type == X.UnmapNotify:
            if ev.window != native:
                set_wm_state(ev.window, 0)  # WithdrawnState
        elif ev.type == X.MapNotify and ev.window == native:
            state["mapped"] = True
        elif ev.type == X.PropertyNotify and ev.window == native and ev.atom == TIME_ATOM:
            state["time"] = ev.time

    def drain():
        while d.pending_events():
            process(d.next_event())

    def wait_for(cond):
        d.flush()
        while not cond():
            process(d.next_event())

    def server_time():
        state["time"] = None
        native.change_property(TIME_ATOM, STRING, 8, b"x")
        wait_for(lambda: state["time"] is not None)
        return state["time"]

    def focus_id():
        f = d.get_input_focus().focus
        return f if isinstance(f, int) else f.id

    def find_window(title):
        stack = [root]
        while stack:
            w = stack.pop()
            try:
                if w.get_wm_name() == title:
                    return w
                stack.extend(w.query_tree().children)
            except error.XError:
                pass
        return None

    end = time.time() + args.timeout
    while time.time() < end:
        drain()
        reqs = sorted(f for f in os.listdir(args.sync_dir) if f.endswith(".req"))
        if not reqs:
            time.sleep(0.02)
            continue
        for r in reqs:
            step = r[:-4]
            os.unlink(os.path.join(args.sync_dir, r))
            if step == "away":
                if not state["mapped"]:
                    native.map()
                    wait_for(lambda: state["mapped"])
                t = server_time()
                native.set_input_focus(X.RevertToParent, t)
                d.sync()
                log(f"away: XSetInputFocus(native {native.id:#x}, time {t}); focus now {focus_id():#x}")
            elif step in ("back", "back_c"):
                title = "A (P.main)" if step == "back" else "C (child)"
                w = find_window(title)
                if not w:
                    log(f"{step}: window {title!r} not found")
                else:
                    t = server_time()
                    ev = protocol.event.ClientMessage(window=w, client_type=WM_PROTOCOLS,
                                                      data=(32, [WM_TAKE_FOCUS, t, 0, 0, 0]))
                    w.send_event(ev, event_mask=0)
                    d.sync()
                    log(f"{step}: WM_TAKE_FOCUS(time {t}) -> {title!r} {w.id:#x}")
            elif step == "quit":
                open(os.path.join(args.sync_dir, "quit.done"), "w").close()
                log("quit")
                return 0
            # let Wine react before the probe continues
            t_end = time.time() + 0.3
            while time.time() < t_end:
                drain()
                time.sleep(0.01)
            if step in ("away", "back", "back_c"):
                log(f"{step}: X focus after 0.3 s: {focus_id():#x}")
            open(os.path.join(args.sync_dir, step + ".done"), "w").close()
    log("timeout")
    return 1


if __name__ == "__main__":
    sys.exit(main())
