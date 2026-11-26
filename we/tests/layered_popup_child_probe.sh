#!/bin/bash
# A layered popup window given to a Direct3D parent with SetParent, under Wine.
#
#   we/tests/layered_popup_child_probe.sh <runner dir> <output file> [probe arguments]
#
# Runs layered_popup_child_probe.exe in the Steam Linux Runtime 4.0 with a new
# prefix under ~/.cache (removed afterwards, KEEP=1 keeps it).
#
# By default with Wine's X11 driver in a headless gamescope (an XWayland server
# with a GPU): layered_popup_child_grab.py reads the host's X window at each
# step. Needs gamescope and python-xlib.
#
# WAYLAND=1 uses Wine's Wayland driver in the current niri session instead: the
# window is captured about twice a second with "niri msg agent capture" and the
# six spots are printed whenever they change, so the steps show up as a
# sequence of rows. Needs a niri with the agent commands and python-pillow.
#
# WDBG=<channels> sets WINEDEBUG and saves the log as <output file>.log.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${LPC_INNER:-}" ] && [ "${WAYLAND:-0}" != 1 ]; then
    LPC_INNER=1 exec gamescope --backend headless -W 1280 -H 800 -- "$0" "$@" 2> /dev/null
fi
RUNNER=$(cd "$1" && pwd)
OUTFILE=$(realpath -m "$2")
shift 2
ENTRY=$HOME/.local/share/umu/steamrt4/_v2-entry-point
WORK=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/we-layered-popup.XXXXXX")
mkdir -p "$WORK/prefix" "$WORK/steam"
cleanup() {
    if [ "${KEEP:-0}" = 1 ]; then echo "prefix kept in $WORK"; else rm -rf "$WORK"; fi
}
trap cleanup EXIT
LOG=$WORK/stderr.log
run() {
    STEAM_COMPAT_DATA_PATH=$WORK/prefix STEAM_COMPAT_CLIENT_INSTALL_PATH=$WORK/steam \
    SteamAppId=0 SteamGameId=0 UMU_ID=wegame-launcher UMU_USE_STEAM=0 PROTON_USE_WOW64=1 \
    PYTHONDONTWRITEBYTECODE=1 WINEDEBUG=${WDBG:--all} PROTON_ENABLE_WAYLAND=${WAYLAND:-0} \
    timeout 180 "$ENTRY" --verb=waitforexitandrun -- \
        "$RUNNER/proton" waitforexitandrun 'C:\windows\system32\cmd.exe' /c "$1" \
        >> "$LOG" 2>&1
}
run "exit 0"
T=$WORK/prefix/pfx/drive_c/aa
mkdir -p "$T"
cp "$HERE/layered_popup_child_probe.exe" "$T/"
run "cd /d C:\\aa && layered_popup_child_probe.exe $* > C:\\aa\\out.txt 2>&1" &
if [ "${WAYLAND:-0}" = 1 ]; then
    python3 - "$T/out.txt" "$WORK" > "$OUTFILE" <<'PY'
import json, os, re, subprocess, sys, time
from PIL import Image
out, work = sys.argv[1], sys.argv[2]
png = os.path.join(work, "capture.png")
def window():
    for w in json.loads(subprocess.run(["niri", "msg", "-j", "windows"], capture_output=True, text=True).stdout):
        if w.get("title") == "layered_popup_child_probe": return w["id"]
def output():
    try: return open(out, errors="replace").read()
    except OSError: return ""
def finished():
    return "SAMPLE popup-destroyed" in output()
def client_size(im):
    # a tiling compositor may make the window larger than the client area the probe asked for
    m = re.search(r"client area (\d+) x (\d+)", output())
    if m and int(m.group(1)) <= im.width and int(m.group(2)) <= im.height: return int(m.group(1)), int(m.group(2))
    return im.width, im.height
prev, start, end = None, time.time(), None
while time.time() - start < 150:
    if end is None and finished(): end = time.time() + 2.5
    if end and time.time() > end: break
    wid = window()
    if wid is not None and subprocess.run(["niri", "msg", "agent", "capture", "--id", str(wid), "--path", png],
                                          capture_output=True).returncode == 0:
        try:
            im = Image.open(png).convert("RGB")
            w, h = client_size(im)
            row = " ".join("%02x%02x%02x" % im.getpixel((i * w // 12, h // 2)) for i in range(1, 12, 2))
            if row != prev: print("capture %dx%d: %s" % (im.width, im.height, row), flush=True)
            prev = row
        except OSError:
            pass
    time.sleep(0.5)
print(open(out, errors="replace").read().replace("\r", ""), end="")
PY
else
    python3 "$HERE/layered_popup_child_grab.py" "$T/out.txt" | tr -d '\r' > "$OUTFILE"
fi
wait || echo "(exit $?)" >> "$LOG"
[ -n "${WDBG:-}" ] && cp "$LOG" "$OUTFILE.log"
echo "wrote $OUTFILE ($(wc -l < "$OUTFILE") lines)" >&2
