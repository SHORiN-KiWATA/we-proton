#!/bin/bash
# Turn the wine/ submodule into a development tree: GE-Proton's patched Wine as
# one base commit, and patches/dwproton/ and patches/wine/ on top of it as one
# commit per patch. See we/upstream-sync.md.
#
#   we/sync/wine-dev.sh [--base-only]
#
#   --base-only   stop after the base commit (to apply patches by hand)
#
# Branches in wine/ afterwards:
#   we-base   Valve's Wine + GE-Proton's patches (protonprep without the
#             DW-Proton / WE-Proton block), one commit
#   we-dw     we-base + patches/dwproton/*.patch
#   we-dev    we-dw + patches/wine/*.patch (checked out)
#
# All submodules are reset and patched like we/build.sh does (uncommitted
# changes in them are lost). When a patch doesn't apply, git am stops with the
# patch's changes merged as far as possible; see "git am 停下" in
# we/upstream-sync.md.
set -euo pipefail

BASE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --base-only) BASE_ONLY=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
mkdir -p build
LOG=build/wine-dev.log
git config user.name > /dev/null || { echo "set git user.name and user.email first" >&2; exit 1; }

echo ":: updating submodules (log: $LOG)"
git submodule update --init --force --filter=tree:0 --recursive > "$LOG" 2>&1 \
    || { tail -20 "$LOG" >&2; exit 1; }

# a run stopped halfway can leave git revert/am/cherry-pick state in wine/,
# which protonprep's git reset doesn't clear
for op in revert am cherry-pick; do git -C wine $op --quit > /dev/null 2>&1 || true; done

# protonprep without the block that applies patches/dwproton and patches/wine,
# and without autoreconf: configure is generated, the patches don't carry it
PREP=build/protonprep-ge-only.sh
sed -e '/^### (2-8) DW-PROTON AND WE-PROTON PATCHES ###$/,/^### END DW-PROTON AND WE-PROTON PATCHES ###$/d' \
    -e 's/^\(\s*\)autoreconf -f$/\1: # autoreconf skipped/' \
    patches/protonprep-valve-staging.sh > "$PREP"
if grep -q 'patches/dwproton\|patches/wine/' "$PREP"; then
    echo "could not cut the DW-Proton / WE-Proton block out of protonprep" >&2
    exit 1
fi
echo ":: applying GE-Proton's patches"
(LC_ALL=C bash "$PREP" < /dev/null) >> "$LOG" 2>&1 || { tail -20 "$LOG" >&2; exit 1; }
FAILURES='FAILED|can.t find file to patch|^error: |^fatal: |Failed to apply|^patch: \*\*\*\*|Only garbage|malformed patch|No such file or directory'
if grep -qE "$FAILURES" "$LOG" || [ -n "$(find wine -name '*.rej' -not -path 'wine/.git/*')" ]; then
    echo "GE-Proton's own patches did not apply ($LOG):" >&2
    grep -E "$FAILURES" "$LOG" >&2 || true
    exit 1
fi

cd wine
find . -name '*.orig' -not -path './.git/*' -delete
# reverting e813ca57 brings back an old configure and config.h.in; they are generated
rm -f configure include/config.h.in
git checkout -q -B we-base
git add -A
git commit -q -m "GE-Proton $(cat ../VERSION): Valve's Wine with GE-Proton's patches

Made by we/sync/wine-dev.sh: patches/protonprep-valve-staging.sh without the
DW-Proton / WE-Proton block and without autoreconf."
echo ":: we-base = $(git rev-parse --short HEAD)"
[ "$BASE_ONLY" = 0 ] || exit 0

git checkout -q -B we-dw
echo ":: applying patches/dwproton"
git am -q -3 ../patches/dwproton/*.patch || {
    echo "git am stopped in patches/dwproton (wine/ is on we-dw); see we/upstream-sync.md" >&2
    exit 1
}
git checkout -q -B we-dev
echo ":: applying patches/wine"
git am -q -3 ../patches/wine/*.patch || {
    echo "git am stopped in patches/wine (wine/ is on we-dev); see we/upstream-sync.md" >&2
    exit 1
}
echo ":: done: wine/ is on we-dev ($(git rev-list --count we-base..we-dw) DW-Proton commits, $(git rev-list --count we-dw..we-dev) WE-Proton commits)"
