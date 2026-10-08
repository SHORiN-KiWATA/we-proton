#!/usr/bin/env python3
"""Play a StatusNotifierItem panel for tray_sni_probe.exe.

    dbus-run-session -- env DISPLAY=:N WINEPREFIX=<new prefix> \\
        sni_host.py <wine> tray_sni_probe.exe [v0]

Owns org.kde.StatusNotifierWatcher and org.freedesktop.Notifications on a
session bus of its own, so it has to run in a prefix whose wineserver is not
running yet (explorer looks for the watcher when it starts). It prints what
the item shows, then clicks it the way waybar does: Activate, ContextMenu, a
double click (waybar calls Activate for both presses and again for the double
click) and SecondaryActivate, on which the probe deletes the icon. Lines from
the probe are prefixed with "probe:".
"""
import subprocess, sys, threading, time
import dbus, dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

ITEM = "org.kde.StatusNotifierItem"
WATCHER = "org.kde.StatusNotifierWatcher"

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
loop = GLib.MainLoop()
registered = []
signals = []

def out(line):
    print(line, flush=True)

class Watcher(dbus.service.Object):
    @dbus.service.method(WATCHER, in_signature="s", sender_keyword="sender")
    def RegisterStatusNotifierItem(self, service, sender):
        registered.append(service)

class Notifications(dbus.service.Object):
    @dbus.service.method("org.freedesktop.Notifications", in_signature="susssasa{sv}i", out_signature="u")
    def Notify(self, app, replaces, icon, summary, body, actions, hints, timeout):
        out(f"host: notification app {str(app)!r} summary {str(summary)!r} body {str(body)!r} timeout {int(timeout)}")
        return 1

watcher_name = dbus.service.BusName(WATCHER, bus)
Watcher(bus, "/StatusNotifierWatcher")
notify_name = dbus.service.BusName("org.freedesktop.Notifications", bus)
Notifications(bus, "/org/freedesktop/Notifications")
bus.add_signal_receiver(lambda *a, member=None: signals.append(str(member)), dbus_interface=ITEM, member_keyword="member")

def wait(cond, seconds):
    end = time.monotonic() + seconds
    ctx = loop.get_context()
    while not cond() and time.monotonic() < end:
        ctx.iteration(False) or time.sleep(0.01)
    return cond()

def pump(seconds):
    wait(lambda: False, seconds)

def show_pixmaps(pixmaps):
    sizes = [f"{int(w)}x{int(h)}" for w, h, _ in pixmaps]
    out(f"host: IconPixmap {' '.join(sizes)}")
    for w, h, data in pixmaps:
        data = bytes(data)
        spots = []
        for name, x, y in (("left", w // 4, h // 2), ("right top", 3 * w // 4, h // 4), ("right bottom", 3 * w // 4, 3 * h // 4)):
            a, r, g, b = data[(y * w + x) * 4:(y * w + x) * 4 + 4]
            spots.append(f"{name} {a:02x}{r:02x}{g:02x}{b:02x}")
        out(f"host:   {int(w)}x{int(h)} ARGB: {', '.join(spots)}")

def show_item(proxy):
    props = proxy.GetAll(ITEM, dbus_interface="org.freedesktop.DBus.Properties")
    for key in ("Category", "Id", "Title", "Status", "WindowId", "IconName", "ItemIsMenu"):
        out(f"host: {key} {props[key]!r}")
    tip = props["ToolTip"]
    out(f"host: ToolTip title {str(tip[2])!r} text {str(tip[3])!r}")
    out(f"host: Menu {'present' if 'Menu' in props else 'absent'}")
    show_pixmaps(props["IconPixmap"])

def call(proxy, method, *args):
    out(f"host: {method}{args}")
    getattr(proxy, method)(*[dbus.Int32(a) for a in args], dbus_interface=ITEM,
                           reply_handler=lambda: None, error_handler=lambda e: out(f"host: {method} failed: {e}"))

probe = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, text=True)
threading.Thread(target=lambda: [out("probe: " + l.rstrip()) for l in probe.stdout], daemon=True).start()

wait(lambda: registered or probe.poll() is not None, 300)
if not registered:
    out("host: no item registered")
    probe.kill()
    sys.exit(1)
name = registered[0]
out(f"host: registered {'org.kde.StatusNotifierItem-<pid>-<n>' if name.startswith(ITEM + '-') else name}")
proxy = bus.get_object(name, "/StatusNotifierItem")
show_item(proxy)

wait(lambda: "NewToolTip" in signals, 10)
pump(0.5)
out(f"host: signals {' '.join(sorted(set(signals)))}")
tip = proxy.Get(ITEM, "ToolTip", dbus_interface="org.freedesktop.DBus.Properties")
out(f"host: ToolTip title now {str(tip[2])!r}, Title {str(proxy.Get(ITEM, 'Title', dbus_interface='org.freedesktop.DBus.Properties'))!r}")

# a restarted panel comes with a new watcher, which has to learn about the item again
registered.clear()
bus.release_name(WATCHER)
pump(0.2)
bus.request_name(WATCHER)
out(f"host: watcher restarted, item {'registered again' if wait(lambda: registered, 10) else 'not registered again'}")

pump(1)
call(proxy, "Activate", 100, 200)
pump(1)
call(proxy, "ContextMenu", 300, 400)
pump(1)
call(proxy, "Activate", 110, 210)
pump(0.2)
call(proxy, "Activate", 110, 210)
call(proxy, "Activate", 110, 210)
pump(1)
call(proxy, "SecondaryActivate", 120, 220)

gone = wait(lambda: not bus.name_has_owner(name), 10)
out(f"host: item {'gone' if gone else 'still there'}")
probe.wait(10)
pump(0.2)
