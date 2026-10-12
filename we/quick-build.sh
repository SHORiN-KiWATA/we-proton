#!/bin/bash
# Rebuild a few Wine modules on top of the last full build and put them into a
# copy of a runner: a test build in minutes instead of we/build.sh's hour.
#
#   we/quick-build.sh --base DIR --name NAME [--prep] [--jobs N] <wine dir>...
#
#   <wine dir>    a directory in wine/ that builds one module: dlls/winewayland.drv,
#                 dlls/win32u, programs/explorer, server, ...
#   --base DIR    the runner to start from, normally build/<last release>
#   --name NAME   the test build: build/NAME, a reflink copy of --base with the
#                 modules replaced (replaced again in place on the next run)
#   --prep        re-patch the submodules first, like we/build.sh does: picks up
#                 changed and new files in patches/. Without it wine/ is built as
#                 it is (for edits made directly in wine/)
#   --jobs N      make -j (default: nproc)
#
# Needs build/obj from a full we/build.sh run (its configured Wine trees and
# the container image). Only the files in wine/ that changed since that build
# synced its sources are copied into build/obj/src-wine, by content, so make
# rebuilds just what they affect. Not for changes that need configure to run
# again (a new DLL, configure.ac): use we/build.sh for those, and for releases.
#
# The modules are built for x86_64 (64-bit PE and the unix side) and i386
# (32-bit PE and the old 32-bit unix side), stripped like a release, and only
# files the runner already has are replaced. Prefixes link their builtin DLLs to
# the runner, so a prefix already using build/NAME sees the new files at its
# next start; close what runs from build/NAME first.
#
# Example, after adding a patch to patches/wine/:
#   we/quick-build.sh --prep --base build/we-proton-ge11-7-26 --name test-0054 dlls/winewayland.drv
#   WEGAME_LAUNCHER_PROTON=$PWD/build/test-0054 wegame-launcher
set -euo pipefail
BASE=""
NAME=""
PREP=0
JOBS=$(nproc)
DIRS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --base) BASE=$2; shift 2 ;;
        --name) NAME=$2; shift 2 ;;
        --prep) PREP=1; shift ;;
        --jobs) JOBS=$2; shift 2 ;;
        -h|--help) sed -n '2,33p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) DIRS+=("${1%/}"); shift ;;
    esac
done
[ -n "$BASE" ] && [ -n "$NAME" ] && [ ${#DIRS[@]} -gt 0 ] || { sed -n '5,6p' "$0" >&2; exit 2; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OBJ=$ROOT/build/obj
OUT=$ROOT/build/$NAME
BASE=$(cd "$BASE" && pwd)
log() { printf '\033[1m::\033[0m %s\n' "$*"; }

[ -f "$BASE/files/lib/wine/x86_64-unix/ntdll.so" ] || { echo "$BASE is not a runner" >&2; exit 1; }
[ "$BASE" != "$OUT" ] || { echo "--name must differ from --base" >&2; exit 1; }
for a in x86_64 i386; do
    [ -f "$OBJ/obj-wine-$a/Makefile" ] || { echo "build/obj/obj-wine-$a is missing: run we/build.sh once" >&2; exit 1; }
done
[ -f "$OBJ/.wine-source" ] || { echo "build/obj/.wine-source is missing: run we/build.sh once" >&2; exit 1; }
for d in "${DIRS[@]}"; do
    [ -d "$ROOT/wine/$d" ] || { echo "wine/$d does not exist" >&2; exit 1; }
done

if [ "$PREP" = 1 ]; then
    "$ROOT/we/build.sh" --prep-only
fi

# Files changed in wine/ since build/obj/src-wine was synced. Compared by content
# and copied without their times: re-patching rewrites many files unchanged, and
# those must keep the old times or make rebuilds everything. configure is
# regenerated inside the container, so the copy in src-wine stays.
IMAGE=$(make -s -C "$OBJ" get-steamrt-image | tail -1)
CCACHE_DIR=${CCACHE_DIR:-$HOME/.ccache}
mkdir -p "$CCACHE_DIR"
in_container() {
    docker run --rm -v "$ROOT:$ROOT" -v "$CCACHE_DIR:$CCACHE_DIR" -w "$OBJ" \
        -e HOME -e USER -u "$(id -u):$(id -g)" -e CCACHE_DIR="$CCACHE_DIR" \
        -e PATH=/usr/lib/ccache:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
        "$IMAGE" "$@"
}

log "syncing changed sources"
LIST=$(mktemp)
trap 'rm -f "$LIST"' EXIT
(cd "$ROOT/wine" && find . -path ./.git -prune -o -path ./autom4te.cache -prune -o \
    -type f -newer "$OBJ/.wine-source" ! -name configure ! -name '*~' ! -name '*.orig' ! -name '*.rej' -print) > "$LIST"
rsync -rlpc --files-from="$LIST" --out-format='   %n' "$ROOT/wine/" "$OBJ/src-wine/"
# like Makefile.in's .wine-post-source: some .spec files are generated from
# others, and the copies in wine/ can be older; it rewrites only what changes
in_container sh -c 'cd src-wine && tools/make_specfiles' > /dev/null
targets=()
for d in "${DIRS[@]}"; do targets+=("$d/all"); done
for a in x86_64 i386; do
    log "building for $a (log: build/quick-build.log)"
    in_container make -j"$JOBS" -C "obj-wine-$a" "${targets[@]}" >> "$ROOT/build/quick-build.log" 2>&1 \
        || { tail -30 "$ROOT/build/quick-build.log" >&2; exit 1; }
done

log "copying $BASE to build/$NAME"
if [ ! -d "$OUT" ]; then
    cp -a --reflink=auto "$BASE" "$OUT.tmp"
    mv "$OUT.tmp" "$OUT"
fi
LIB=$OUT/files/lib/wine
# copy, strip, then rename over the old file, so running processes keep theirs
put() {
    cp "$1" "$2.new"
    objcopy -p --strip-debug "$2.new"
    chmod --reference="$1" "$2.new"
    mv -f "$2.new" "$2"
    echo "   ${2#$OUT/}"
}
n=0
for d in "${DIRS[@]}"; do
    for f in "$OBJ/obj-wine-x86_64/$d"/* "$OBJ/obj-wine-i386/$d"/*; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        case "$f" in
            "$OBJ/obj-wine-x86_64/"*) unix=x86_64-unix ;;
            *) unix=i386-unix ;;
        esac
        case "$b" in *.o|*.a|*.res|*.h|*.c|Makefile) continue ;; esac
        if [ -f "$LIB/$unix/$b" ]; then put "$f" "$LIB/$unix/$b"; n=$((n + 1))
        elif [ "$unix" = x86_64-unix ] && [ -f "$OUT/files/bin/$b" ]; then put "$f" "$OUT/files/bin/$b"; n=$((n + 1))
        fi
    done
    for a in x86_64 i386; do
        for f in "$OBJ/obj-wine-$a/$d/$a-windows"/*; do
            [ -f "$f" ] || continue
            b=$(basename "$f")
            case "$b" in *.o|*.a|*.res) continue ;; esac
            [ -f "$LIB/$a-windows/$b" ] || continue
            put "$f" "$LIB/$a-windows/$b"; n=$((n + 1))
        done
    done
done
[ "$n" -gt 0 ] || { echo "nothing built that the runner has" >&2; exit 1; }

echo "$(date +%s) $NAME" > "$OUT/version"
{
    echo
    echo "quick-build $(date -Is): base $(basename "$BASE"), modules ${DIRS[*]}"
    echo "we-proton: $(git -C "$ROOT" rev-parse HEAD)$([ -z "$(git -C "$ROOT" status --porcelain -- patches)" ] || echo -dirty)"
} >> "$OUT/we-proton-build-info"
log "done: build/$NAME ($n files replaced)"
