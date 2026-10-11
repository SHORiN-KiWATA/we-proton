#!/bin/bash
# Run the probe programs in this directory with a runner, the way wegame-launcher
# does: inside the Steam Linux Runtime 4.0 (umu's copy in
# ~/.local/share/umu/steamrt4), in WoW64 mode, with a new prefix and a private
# Xvfb. Writes one <probe>.txt per probe into <output dir>.
#
#   we/tests/regress.sh <runner dir> <output dir>
#   we/tests/regress.sh --compare <old output dir> <new output dir>
#
# Build the probes first (we/tests/build.sh). --compare masks window handles and
# addresses (hex numbers of 8 or more digits), then lists each probe as same or
# different and prints the differences; it exits with status 1 if any differ.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)

PROBES=(inbv_probe.exe layered_child_probe.exe noactivate_probe.exe
        private_ns_probe.exe private_ns_probe32.exe
        typelib_noflags_probe.exe typelib_noflags_probe32.exe
        vcrt_version_probe.exe window_band_probe.exe window_band_probe32.exe
        winhttp_crlf_probe.exe writecopy_probe.exe
        "minprec_sig_probe.exe minprec_vs.dxbc minprec_hs.dxbc")

if [ "${1:-}" = --compare ]; then
    [ $# -eq 3 ] || { sed -n '2,13p' "$HERE/regress.sh"; exit 2; }
    mask() { sed -E 's/\b(0x)?[0-9A-Fa-f]{8,16}\b/H/g' "$1"; }
    differ=0
    for f in "$2"/*.txt; do
        b=$(basename "$f")
        if [ ! -f "$3/$b" ]; then echo "missing    $b"; differ=1; continue; fi
        if diff -q <(mask "$f") <(mask "$3/$b") > /dev/null; then
            echo "same       $b"
        else
            echo "different  $b"
            diff <(mask "$f") <(mask "$3/$b") | sed 's/^/    /'
            differ=1
        fi
    done
    exit $differ
fi

[ $# -eq 2 ] || { sed -n '2,13p' "$HERE/regress.sh"; exit 2; }
RUNNER=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
cd "$HERE"
ENTRY=$HOME/.local/share/umu/steamrt4/_v2-entry-point
[ -x "$RUNNER/proton" ] || { echo "$RUNNER/proton is missing" >&2; exit 1; }
[ -x "$ENTRY" ] || { echo "$ENTRY is missing (umu or wegame-launcher installs it)" >&2; exit 1; }
for p in "${PROBES[@]}"; do
    [ -f "${p%% *}" ] || { echo "${p%% *} is missing; run we/tests/build.sh" >&2; exit 1; }
done

rm -rf "$OUT/prefix" "$OUT/steam"
mkdir -p "$OUT/prefix" "$OUT/steam"
run() {
    STEAM_COMPAT_DATA_PATH=$OUT/prefix STEAM_COMPAT_CLIENT_INSTALL_PATH=$OUT/steam \
    SteamAppId=0 SteamGameId=0 UMU_ID=wegame-launcher UMU_USE_STEAM=0 PROTON_USE_WOW64=1 \
    DISPLAY=:$DPY timeout 120 "$ENTRY" --verb=waitforexitandrun -- \
        "$RUNNER/proton" waitforexitandrun 'C:\windows\system32\cmd.exe' /c "$1" \
        >> "$OUT/stderr.log" 2>&1
}
DPY=120
while [ -e /tmp/.X11-unix/X$DPY ]; do DPY=$((DPY + 1)); done
Xvfb :$DPY -screen 0 1280x800x24 -nolisten tcp > /dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2> /dev/null' EXIT
sleep 1
: > "$OUT/stderr.log"

run "exit 0"    # creates the prefix
T=$OUT/prefix/pfx/drive_c/we-tests
mkdir -p "$T"
cp ./*.exe ./*.dxbc "$T/"
for p in "${PROBES[@]}"; do
    exe=${p%% *}
    name=${exe%.exe}
    run "cd /d C:\\we-tests && $p > C:\\we-tests\\$name.out 2>&1" || echo "($name: exit $?)" >> "$OUT/stderr.log"
    tr -d '\r' < "$T/$name.out" > "$OUT/$name.txt" 2> /dev/null || echo "(no output)" > "$OUT/$name.txt"
    echo "  $name: $(wc -l < "$OUT/$name.txt") lines"
done
