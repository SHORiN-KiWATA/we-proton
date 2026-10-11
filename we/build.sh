#!/bin/bash
# Build WE-Proton from scratch. This is GE-Proton's build: patches/protonprep-valve-staging.sh
# patches the submodules (GE-Proton's patches, then patches/dwproton/ and patches/wine/
# for Wine and the we-* patches for vkd3d-proton), then configure.sh and make redist
# build everything in the Steam Linux Runtime SDK container named in Makefile.in.
# Needs docker. The first build takes hours; ccache (~/.ccache) speeds up later ones.
#
#   we/build.sh --name we-proton-<version> [--install] [--jobs N] [--skip-prep]
#
#   --name NAME   release name: the runner's version file, the Steam display name and
#                 the archive names
#   --install     also copy the result to ~/.local/share/proton/runners/WE-Proton Latest
#   --jobs N      make -j (default: nproc)
#   --skip-prep   don't update and re-patch the submodules (they are patched already)
#
# Without --skip-prep the submodules are reset to the commits this tree pins and
# re-patched: uncommitted changes in them are lost.
#
# Output: build/NAME/ (the runner), build/NAME.tar.xz, build/NAME.sha512sum and
# build/NAME-source.tar.xz (the patched Wine, vkd3d-proton and dxvk sources, and 7-Zip's).
set -euo pipefail

NAME=""
INSTALL=0
JOBS=$(nproc)
PREP=1
while [ $# -gt 0 ]; do
    case "$1" in
        --name) NAME=$2; shift 2 ;;
        --install) INSTALL=1; shift ;;
        --jobs) JOBS=$2; shift 2 ;;
        --skip-prep) PREP=0; shift ;;
        -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$NAME" ] || { echo "--name is required, e.g. --name we-proton-ge11-7-25" >&2; exit 2; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build
OBJ=$OUT/obj
DIST=$OUT/$NAME
RUNNERS=$HOME/.local/share/proton/runners
log() { printf '\033[1m::\033[0m %s\n' "$*"; }
mkdir -p "$OUT"

PATCHED="wine dxvk vkd3d-proton protonfixes dxvk-nvapi wineopenxr vklayers/low_latency_layer
    steam_helper umu_helper lsteamclient"
# patch(1), git apply (staging's patchinstall) and git revert report failures this way
FAILURES='FAILED|can.t find file to patch|^error: |^fatal: |Failed to apply|^patch: \*\*\*\*|Only garbage|malformed patch|No such file or directory'
if [ "$PREP" = 1 ]; then
    log "updating submodules (log: build/protonprep.log)"
    # --force: the submodules were patched in place by the last run
    git -C "$ROOT" submodule update --init --force --filter=tree:0 --recursive > "$OUT/protonprep.log" 2>&1 \
        || { tail -20 "$OUT/protonprep.log" >&2; exit 1; }
    # a run stopped halfway can leave git revert/am/cherry-pick state in wine/,
    # which protonprep's git reset doesn't clear
    for op in revert am cherry-pick; do git -C "$ROOT/wine" $op --quit > /dev/null 2>&1 || true; done
    # protonprep only checks out the files it patches in these top-level directories
    (cd "$ROOT" && find steam_helper umu_helper lsteamclient \( -name '*.rej' -o -name '*.orig' \) -delete)
    log "patching"
    # protonprep goes on when a patch doesn't apply and stops only when wine-mono
    # can't be prepared, so look for failures in its output (in English). With its
    # output going to the log, patch(1) doesn't stop to ask questions.
    (cd "$ROOT" && LC_ALL=C ./patches/protonprep-valve-staging.sh < /dev/null) >> "$OUT/protonprep.log" 2>&1 \
        || { tail -20 "$OUT/protonprep.log" >&2; exit 1; }
    rejects=$(cd "$ROOT" && find $PATCHED -name '*.rej' 2>/dev/null || true)
    if [ -n "$rejects" ] || grep -qE "$FAILURES" "$OUT/protonprep.log"; then
        echo "some patches did not apply (build/protonprep.log):" >&2
        grep -E "$FAILURES" "$OUT/protonprep.log" >&2 || true
        [ -z "$rejects" ] || printf '  %s\n' $rejects >&2
        exit 1
    fi
fi

# Always start from an empty build/obj. Makefile.in cannot build autotools packages
# whose configure is generated in the synced source copy (xz) a second time: the
# next sync sees the generated files, syncs again and deletes them, while the
# configure stamp stays. ccache keeps the rebuild reasonably fast.
rm -rf "$OBJ"
# configure.sh writes the build name into the Makefile, so reconfigure when it changes
mkdir -p "$OBJ"
if [ ! -f "$OBJ/Makefile" ] || ! grep -qx "BUILD_NAME := $NAME" "$OBJ/Makefile"; then
    log "configuring"
    (cd "$OBJ" && "$ROOT/configure.sh" --build-name="$NAME" --container-engine=docker)
fi
log "building (log: build/make.log)"
rm -rf "$OBJ/$NAME" "$OBJ/$NAME.tar.gz"
make -C "$OBJ" -j"$JOBS" ENABLE_CCACHE=1 redist > "$OUT/make.log" 2>&1 \
    || { tail -30 "$OUT/make.log" >&2; exit 1; }

log "packing $NAME"
rm -rf "$DIST"
mv "$OBJ/$NAME" "$DIST"
rm -f "$OBJ/$NAME.tar.gz" "$OBJ/$NAME.sha512sum"
# Makefile.in writes `git describe --tags`; launchers read the name from here
echo "$(date +%s) $NAME" > "$DIST/version"
dirty() { [ -z "$(git -C "$ROOT" status --porcelain -- patches)" ] || echo "-dirty"; }
{
    echo "name: $NAME"
    echo "we-proton: $(git -C "$ROOT" rev-parse HEAD)$(dirty)"
    echo "ge-proton: $(cat "$ROOT/VERSION")"
    echo "dw-proton: $(cat "$ROOT/we/dw-proton-version")"
    for m in wine wine-staging dxvk vkd3d-proton protonfixes; do
        echo "$m: $(git -C "$ROOT/$m" rev-parse HEAD)"
    done
    for p in "$ROOT"/patches/dwproton/*.patch; do echo "patch: dwproton/$(basename "$p")"; done
    for p in "$ROOT"/patches/wine/*.patch; do echo "patch: wine/$(basename "$p")"; done
    for p in "$ROOT"/patches/vkd3d-proton/we-*.patch; do echo "patch: vkd3d-proton/$(basename "$p")"; done
} > "$DIST/we-proton-build-info"
sed -e "s/@NAME@/$NAME/g" -e "s/@GE@/$(cat "$ROOT/VERSION")/g" -e "s/@DW@/$(cat "$ROOT/we/dw-proton-version")/g" "$ROOT/we/dist-README.md" > "$DIST/README-WE-Proton.md"
# files/utils/7zz (for DW-Proton's protonfixes) is 7-Zip: LGPL plus the unRAR restriction
cp "$ROOT/7zip/DOC/License.txt" "$DIST/LICENSE.7zip"
cp "$ROOT/7zip/DOC/copying.txt" "$DIST/COPYING.7zip"
export XZ_OPT=-T0
tar -C "$OUT" -cJf "$OUT/$NAME.tar.xz" "$NAME"
(cd "$OUT" && sha512sum "$NAME.tar.xz" > "$NAME.sha512sum")
# Wine, vkd3d-proton and 7-Zip are LGPL: ship the sources the binaries were built from
log "packing $NAME-source.tar.xz"
tar -C "$ROOT" -cJf "$OUT/$NAME-source.tar.xz" --exclude=.git --exclude='*.orig' \
    --transform "s,^,$NAME-source/," wine vkd3d-proton dxvk 7zip \
    -C "$DIST" we-proton-build-info
log "done: build/$NAME, build/$NAME.tar.xz, build/$NAME.sha512sum, build/$NAME-source.tar.xz"

if [ "$INSTALL" = 1 ]; then
    mkdir -p "$RUNNERS"
    rm -rf "$RUNNERS/WE-Proton Latest"
    cp -a --reflink=auto "$DIST" "$RUNNERS/WE-Proton Latest"
    log "installed to $RUNNERS/WE-Proton Latest"
fi
