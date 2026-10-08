#!/bin/bash
# Type pinyin into a Wine window through an input method, without touching the
# desktop: a private Xvfb display, session bus and fcitx5 (XIM frontend, rime
# with luna_pinyin_simp), configured in a directory of its own.
#
#   ime_type.sh <window name> <keys...> -- <command...>
#
# Keys are xdotool key names, "TYPE:text" types text; ctrl+space switches
# fcitx5 to rime. The command runs with DISPLAY, XMODIFIERS and the bus set
# up, and should open a window whose name matches. Needs Xvfb, xdotool,
# fcitx5 and fcitx5-rime.
#
#   IME_DIR       configuration and logs (default: build/ime-test in the repository)
#   IME_DISPLAY   Xvfb display (default :117)
#   SETTLE        seconds to wait after the window shows up (default 3)
#   CLICK_X/Y     where to click in the window to focus it (default 30,30)
#   TITLE_AFTER   for programs that do not exit: seconds to wait after typing,
#                 then print the window name and kill the prefix's wineserver
#                 (needs WINE, the runner's files/bin/wine)
#
# For example, with a runner's wine and a new prefix:
#   WINEPREFIX=/tmp/pfx ime_type.sh ime_edit_probe ctrl+space TYPE:nihao space -- \
#       <runner>/files/bin/wine ime_edit_probe.exe 20 disable
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IME_DIR=${IME_DIR:-$ROOT/build/ime-test}
IME_DISPLAY=${IME_DISPLAY:-:117}

if [ -z "$IME_TYPE_INNER" ]; then
    mkdir -p "$IME_DIR"/config/fcitx5 "$IME_DIR"/data/fcitx5/rime "$IME_DIR"/cache
    cat > "$IME_DIR"/config/fcitx5/profile <<'EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=rime

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=rime
Layout=

[GroupOrder]
0=Default
EOF
    cat > "$IME_DIR"/data/fcitx5/rime/default.custom.yaml <<'EOF'
patch:
  schema_list:
    - schema: luna_pinyin_simp
EOF
    # nothing started on the private bus may reach the desktop's Wayland display or fcitx5
    exec env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS -u GTK_IM_MODULE -u QT_IM_MODULE \
        -u QT_IM_MODULES -u SDL_IM_MODULE IME_TYPE_INNER=1 \
        XDG_CONFIG_HOME="$IME_DIR"/config XDG_DATA_HOME="$IME_DIR"/data XDG_CACHE_HOME="$IME_DIR"/cache \
        DISPLAY=$IME_DISPLAY XMODIFIERS=@im=fcitx LANG=zh_CN.UTF-8 \
        dbus-run-session -- "$0" "$@"
fi
set +e

has_owner() {
    dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
        org.freedesktop.DBus.NameHasOwner string:"$1" 2>/dev/null | grep -q true
}

name=$1; shift
keys=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do keys+=("$1"); shift; done
shift

if ! xdotool getdisplaygeometry >/dev/null 2>&1; then
    Xvfb $IME_DISPLAY -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
    XVFB=$!
    sleep 1
fi
fcitx5 -D --disable=wayland,waylandim,kimpanel,notificationitem,ibusfrontend,fcitx4frontend,clipboard,virtualkeyboard \
    --verbose='*=4,key_trace=5' > "$IME_DIR"/fcitx5.log 2>&1 &
FCITX=$!
# wait without calling fcitx5 itself, which would have the bus start another one
for i in $(seq 100); do has_owner org.fcitx.Fcitx5 && break; sleep 0.2; done
has_owner org.fcitx.Fcitx5 || { echo "fcitx5 did not start, see $IME_DIR/fcitx5.log" >&2; exit 1; }

"$@" &
APP=$!
for i in $(seq 600); do
    w=$(xdotool search --name "$name" 2>/dev/null | head -1)
    [ -n "$w" ] && break
    sleep 0.2
done
[ -n "$w" ] || { echo "no window named $name" >&2; kill $APP $FCITX; exit 1; }
sleep ${SETTLE:-3}
xdotool windowfocus --sync $w
sleep 0.5
xdotool mousemove --window $w ${CLICK_X:-30} ${CLICK_Y:-30} click 1
sleep 0.5
for k in "${keys[@]}"; do
    case "$k" in
    TYPE:*) xdotool type --delay 150 "${k#TYPE:}" ;;
    *) xdotool key "$k" ;;
    esac
    sleep 0.5
done
if [ -n "$TITLE_AFTER" ]; then
    sleep $TITLE_AFTER
    echo "title: $(xdotool getwindowname $w)"
    "$(dirname "$WINE")/wineserver" -k
fi
wait $APP
kill $FCITX
wait $FCITX 2>/dev/null
[ -n "$XVFB" ] && kill $XVFB
exit 0
