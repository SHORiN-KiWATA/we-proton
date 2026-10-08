#!/bin/bash
# Build WE-Proton quickly: take the official dwproton release this branch is
# based on, rebuild only the wine binaries touched by patches/wine/*.patch (and
# vkd3d-proton's d3d12*.dll if patches/vkd3d-proton has patches), and drop
# them in. The full container build (make redist) gives the same result from
# scratch but takes hours.
#
# Wine is built in the same Steam Runtime SDK image and with the same flags as
# Makefile.in uses, so the unix libraries only need what the runtime provides
# (built on the host they picked up e.g. libunwind and failed to load inside
# the runtime). Needs docker.
#
#   we/overlay-build.sh [--release N] [--install] [--jobs N]
#
#   --release N   WE-Proton release number (default 1) -> we-proton-<base>-N
#   --install     also copy the result to ~/.local/share/proton/runners/WE-Proton
#   --jobs N      make -j (default: nproc)
set -euo pipefail

BASE=dwproton-11.0-14
RELEASE=1
INSTALL=0
JOBS=$(nproc)
while [ $# -gt 0 ]; do
    case "$1" in
        --release) RELEASE=$2; shift 2 ;;
        --install) INSTALL=1; shift ;;
        --jobs) JOBS=$2; shift 2 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=we-proton-${BASE#dwproton-}-$RELEASE
OBJ=$ROOT/build/overlay
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/we-proton
DIST=$ROOT/build/$NAME
RUNNERS=$HOME/.local/share/proton/runners
mkdir -p "$OBJ" "$CACHE"
log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

# --- which binaries each patched source directory ends up in -----------------
# Space-separated build:runner pairs; runner paths are relative to
# files/lib/wine. PE DLLs that 32-bit programs load too need both arches.
declare -A TARGETS=(
    [dlls/ntdll/unix]="dlls/ntdll/ntdll.so:x86_64-unix/ntdll.so"
    [server]="server/wineserver:../../bin/wineserver"
    [dlls/ntoskrnl.exe]="dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe:x86_64-windows/ntoskrnl.exe"
    [dlls/win32u]="dlls/win32u/win32u.so:x86_64-unix/win32u.so"
    [dlls/rpcrt4]="dlls/rpcrt4/x86_64-windows/rpcrt4.dll:x86_64-windows/rpcrt4.dll dlls/rpcrt4/i386-windows/rpcrt4.dll:i386-windows/rpcrt4.dll"
    [dlls/winhttp]="dlls/winhttp/x86_64-windows/winhttp.dll:x86_64-windows/winhttp.dll dlls/winhttp/i386-windows/winhttp.dll:i386-windows/winhttp.dll"
)
# d3dcompiler_43's sources are built into d3d10 and every other d3dcompiler_*.
for m in d3d10 d3dcompiler_{33..43} d3dcompiler_46 d3dcompiler_47; do
    for arch in x86_64 i386; do
        TARGETS[dlls/d3dcompiler_43]+=" dlls/$m/$arch-windows/$m.dll:$arch-windows/$m.dll"
    done
done
declare -A WANT=()
for p in "$ROOT"/patches/wine/*.patch; do
    for f in $(grep -E '^\+\+\+ b/' "$p" | sed 's#^+++ b/##'); do
        [[ $f == */tests/* ]] && continue  # conformance tests are not shipped
        hit=
        for dir in "${!TARGETS[@]}"; do
            [[ $f == "$dir"/* ]] && WANT[$dir]=1 && hit=1
        done
        [ -n "$hit" ] || { echo "$(basename "$p") touches $f, which overlay-build.sh does not know how to rebuild" >&2; exit 1; }
    done
done
log "patched: ${!WANT[*]}"

# --- base release ------------------------------------------------------------
TARBALL=$CACHE/$BASE-x86_64.tar.xz
if [ ! -f "$TARBALL" ]; then
    log "downloading $BASE"
    URL=https://dawn.wine/dawn-winery/dwproton/releases/download/$BASE
    curl -fL -o "$TARBALL.part" "$URL/$BASE-x86_64.tar.xz"
    curl -fsL -o "$CACHE/$BASE-x86_64.sha512sum" "$URL/$BASE-x86_64.sha512sum"
    [ "$(sha512sum < "$TARBALL.part" | cut -d' ' -f1)" = "$(cut -d' ' -f1 "$CACHE/$BASE-x86_64.sha512sum")" ] \
        || { echo "checksum mismatch for $BASE" >&2; rm -f "$TARBALL.part"; exit 1; }
    mv "$TARBALL.part" "$TARBALL"
fi

# --- patched wine source, the same way Makefile.in does it ---------------------
SRC=$OBJ/src-wine
STAMP=$(cat "$ROOT"/patches/wine/*.patch | sha256sum | cut -c1-16)-$(git -C "$ROOT/wine" rev-parse HEAD)
if [ "$(cat "$OBJ/.src-stamp" 2>/dev/null)" != "$STAMP" ]; then
    log "preparing wine source"
    [ -z "$(git -C "$ROOT/wine" status --porcelain --untracked-files=no)" ] \
        || { echo "wine/ has local changes; the build applies patches/wine itself" >&2; exit 1; }
    rm -rf "$SRC" "$OBJ/wine"
    mkdir -p "$SRC"
    git -C "$ROOT/wine" archive HEAD | tar -x -C "$SRC"
    for p in "$ROOT"/patches/wine/*.patch; do patch -d "$SRC" -Np1 -s -i "$p"; done
    # make_vulkan only works with the registry next to it, like Makefile.in runs it
    cp "$ROOT"/Vulkan-Headers/registry/{vk,video}.xml "$SRC"/
    (cd "$SRC" && autoreconf -fi >/dev/null 2>&1 \
        && XDG_CACHE_HOME=$OBJ dlls/winevulkan/make_vulkan -x vk.xml -X video.xml >/dev/null \
        && tools/make_specfiles >/dev/null)
    echo "$STAMP" > "$OBJ/.src-stamp"
fi

# --- build only what the patches touch ---------------------------------------
# The SDK image and the compiler flags are the ones Makefile.in uses for wine:
# HOST_CFLAGS/CCOS_CFLAGS/<arch>_CFLAGS/CFLAGS for gcc, WINE_CFLAGS and
# WINE_AUTOCONF_ARGS (gstreamer, ffmpeg, pcap and wayland are left out: none of the
# binaries replaced here use them, and Makefile.in builds their libraries itself).
SDK_IMAGE=$(sed -n 's/^\s*STEAMRT_IMAGE ?= \(.*\/sdk\/x86_64:.*\)$/\1/p' "$ROOT/Makefile.in")
[ -n "$SDK_IMAGE" ] || { echo "no x86_64 STEAMRT_IMAGE in Makefile.in" >&2; exit 1; }
COMMON_CFLAGS="-O2 -march=nocona -mtune=core-avx2 -pipe -mfpmath=sse -mno-avx -mno-avx2 -mno-avx512f -fvect-cost-model=cheap"
COMMON_CFLAGS+=" -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -fno-omit-frame-pointer"
COMMON_CFLAGS+=" -Wno-discarded-qualifiers -Wno-stringop-overflow -Wno-incompatible-pointer-types"
UNIX_CFLAGS="-mcmodel=small $COMMON_CFLAGS -Wl,--exclude-libs=libstdc++.a"
X86_64_PE_CFLAGS="-mcmodel=small $COMMON_CFLAGS"
I386_PE_CFLAGS="-mstackrealign $COMMON_CFLAGS"
CONFIGURE_ARGS="--with-mingw=gcc --enable-build-id --disable-tests --with-x"
CONFIGURE_ARGS+=" --without-gstreamer --without-ffmpeg --without-pcap --without-unwind --without-oss --enable-archs=x86_64,i386 --enable-win64"
CONFIG_STAMP="$SDK_IMAGE $CONFIGURE_ARGS $UNIX_CFLAGS $X86_64_PE_CFLAGS $I386_PE_CFLAGS"

in_sdk() {
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$OBJ:$OBJ" -w "$B" "$SDK_IMAGE" bash -c "$1"
}

B=$OBJ/wine
if [ ! -f "$B/Makefile" ] || [ "$(cat "$B/.config-stamp" 2>/dev/null)" != "$CONFIG_STAMP" ]; then
    log "configuring in $SDK_IMAGE"
    rm -rf "$B"
    mkdir -p "$B"
    in_sdk "$(printf '%q ' env \
        CC=x86_64-linux-gnu-gcc CXX=x86_64-linux-gnu-g++ PKG_CONFIG=x86_64-linux-gnu-pkg-config \
        CFLAGS="$UNIX_CFLAGS" CXXFLAGS="$UNIX_CFLAGS" \
        CROSSCFLAGS="$X86_64_PE_CFLAGS" \
        x86_64_CC=x86_64-w64-mingw32-gcc x86_64_CFLAGS="$X86_64_PE_CFLAGS" \
        i386_CC=i686-w64-mingw32-gcc i386_CFLAGS="$I386_PE_CFLAGS" \
        "$SRC/configure" $CONFIGURE_ARGS) > configure.log 2>&1" \
        || { tail -20 "$B/configure.log" >&2; exit 1; }
    echo "$CONFIG_STAMP" > "$B/.config-stamp"
fi
make_targets=()
for dir in "${!WANT[@]}"; do
    for pair in ${TARGETS[$dir]}; do make_targets+=("${pair%%:*}"); done
done
log "building ${make_targets[*]} in the SDK"
in_sdk "make -j$JOBS $(printf '%q ' "${make_targets[@]}")" > "$OBJ/make.log" 2>&1 \
    || { grep -m20 -E 'error' "$OBJ/make.log" >&2; exit 1; }

# --- vkd3d-proton, only if patches/vkd3d-proton has something --------------------
# Same source prep as .vkd3d-proton-post-source and the same flags as rules-meson
# in Makefile.in (GCC, stripped), so only the patches differ from the official DLLs.
VKD3D_PATCHES=$(find "$ROOT/patches/vkd3d-proton" -name '*.patch' 2>/dev/null | sort)
VKD3D_ARCHS=(x86_64 i386)
declare -A MINGW=([x86_64]=x86_64-w64-mingw32 [i386]=i686-w64-mingw32)
declare -A MESON_CPU=([x86_64]=x86_64 [i386]=x86)
declare -A ARCH_CFLAGS=(
    [x86_64]="-mcmodel=small -O2 -march=nocona -mtune=core-avx2 -pipe -mfpmath=sse -mno-avx -mno-avx2 -mno-avx512f -fvect-cost-model=cheap"
    [i386]="-mstackrealign -O2 -march=nocona -mtune=core-avx2 -pipe -mfpmath=sse -mno-avx -mno-avx2 -mno-avx512f -fvect-cost-model=cheap"
)
declare -A VKD3D_ARCH_CFLAGS=([x86_64]="-O3" [i386]="-O3 -mpreferred-stack-boundary=2")
COMMON_CFLAGS="-fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -fno-omit-frame-pointer -Wl,--exclude-libs=libstdc++.a -s"
# The official DLLs import msvcrt.dll. Arch's mingw-w64 targets UCRT and its libstdc++
# headers do not build with -mcrtdll=msvcrt-os, so these import api-ms-win-crt-*
# instead; Wine has both as builtins.
CRT_FLAGS=
quote_list() { local out= w; for w in $1; do out+="${out:+, }'$w'"; done; echo "$out"; }
if [ -n "$VKD3D_PATCHES" ]; then
    V=$ROOT/vkd3d-proton
    VSRC=$OBJ/src-vkd3d-proton
    [ -f "$V/meson.build" ] \
        || { echo "vkd3d-proton/ is not checked out: git submodule update --init --recursive vkd3d-proton" >&2; exit 1; }
    VSTAMP=$(cat $VKD3D_PATCHES | sha256sum | cut -c1-16)-$(git -C "$V" rev-parse HEAD)-$(git -C "$V" submodule status --recursive | sha256sum | cut -c1-16)
    if [ "$(cat "$OBJ/.vkd3d-src-stamp" 2>/dev/null)" != "$VSTAMP" ]; then
        log "preparing vkd3d-proton source"
        [ -z "$(git -C "$V" status --porcelain --untracked-files=no)" ] \
            || { echo "vkd3d-proton/ has local changes; the build applies patches/vkd3d-proton itself" >&2; exit 1; }
        rm -rf "$VSRC" "$OBJ"/vkd3d-proton-*
        rsync -a --filter=:C --exclude '*~' --exclude .git --exclude compile_commands.json "$V/" "$VSRC/"
        for p in $VKD3D_PATCHES; do patch -d "$VSRC" -Np1 -s -i "$p"; done
        # vkd3d_build is the shader cache key: the patched build must not reuse caches
        # from the official one, so tag it like a dirty tree (hash + 0, version + '+')
        sed -re "s#@VCS_TAG@#$(git -C "$V" describe --always --exclude='*' --abbrev=15)0#" \
            "$V/vkd3d_build.h.in" > "$VSRC/vkd3d_build.h.in"
        sed -re "s#@VCS_TAG@#$(git -C "$V" describe --always --tags)+#" \
            "$V/vkd3d_version.h.in" > "$VSRC/vkd3d_version.h.in"
        echo "$VSTAMP" > "$OBJ/.vkd3d-src-stamp"
    fi
    make -C "$B" tools/widl/widl >> "$OBJ/make.log" 2>&1 || { echo "building widl failed, see $OBJ/make.log" >&2; exit 1; }
    for arch in "${VKD3D_ARCHS[@]}"; do
        VB=$OBJ/vkd3d-proton-$arch
        m=${MINGW[$arch]}
        cflags=$(quote_list "${ARCH_CFLAGS[$arch]} $COMMON_CFLAGS $CRT_FLAGS ${VKD3D_ARCH_CFLAGS[$arch]}")
        ldflags=$(quote_list "-static -static-libgcc -static-libstdc++ $CRT_FLAGS ${VKD3D_ARCH_CFLAGS[$arch]}")
        cross=$(cat <<EOF
[binaries]
ar = '$m-ar'
c = '$m-gcc'
cpp = '$m-g++'
windres = '$m-windres'
strip = '$m-strip'
widl = '$B/tools/widl/widl'

[properties]
needs_exe_wrapper = true
c_args = [$cflags]
cpp_args = [$cflags]
c_link_args = [$ldflags]
cpp_link_args = [$ldflags]

[host_machine]
system = 'windows'
cpu_family = '${MESON_CPU[$arch]}'
cpu = '${MESON_CPU[$arch]}'
endian = 'little'
EOF
)
        if [ ! -f "$VB/build.ninja" ] || [ "$(cat "$VB/cross.txt")" != "$cross" ]; then
            log "configuring vkd3d-proton ($arch)"
            rm -rf "$VB"
            mkdir -p "$VB"
            echo "$cross" > "$VB/cross.txt"
            meson setup "$VB" "$VSRC" --cross-file "$VB/cross.txt" --buildtype=plain \
                -Db_ndebug=true -Denable_extended_emulation=true > "$VB/configure.log" 2>&1 \
                || { tail -20 "$VB/configure.log" >&2; exit 1; }
        fi
        log "building vkd3d-proton ($arch)"
        ninja -C "$VB" > "$VB/build.log" 2>&1 || { grep -m20 -E 'error' "$VB/build.log" >&2; exit 1; }
    done
fi

# --- assemble the runner ------------------------------------------------------
log "assembling $NAME"
rm -rf "$DIST" "$OBJ/unpack"
mkdir -p "$OBJ/unpack"
tar -xf "$TARBALL" -C "$OBJ/unpack"
mv "$OBJ/unpack/$BASE-x86_64" "$DIST"
for dir in "${!WANT[@]}"; do
    for pair in ${TARGETS[$dir]}; do
        from=${pair%%:*}; to=$DIST/files/lib/wine/${pair#*:}
        case "$from" in
            *.so|*/wineserver) strip --strip-unneeded -o "$to" "$B/$from" ;;
            */i386-windows/*) i686-w64-mingw32-strip -o "$to" "$B/$from" ;;
            *) x86_64-w64-mingw32-strip -o "$to" "$B/$from" ;;
        esac
        echo "  $from -> ${to#$DIST/}"
    done
done
if [ -n "$VKD3D_PATCHES" ]; then
    for arch in "${VKD3D_ARCHS[@]}"; do
        for dll in d3d12 d3d12core; do
            to=$DIST/files/lib/wine/vkd3d-proton/$arch-windows/$dll.dll
            "${MINGW[$arch]}-strip" -o "$to" "$OBJ/vkd3d-proton-$arch/libs/$dll/$dll.dll"
            echo "  vkd3d-proton-$arch/libs/$dll/$dll.dll -> ${to#$DIST/}"
        done
    done
fi
sed -i "s/\"$BASE-x86_64\"/\"$NAME\"/; s/\"display_name\" \"[^\"]*\"/\"display_name\" \"WE-Proton ${BASE#dwproton-}-$RELEASE\"/" \
    "$DIST/compatibilitytool.vdf"
sed -i "s/ $BASE\$/ $NAME/" "$DIST/version"
{
    echo "base: $BASE"
    echo "wine: $(git -C "$ROOT/wine" rev-parse HEAD)"
    [ -z "$VKD3D_PATCHES" ] || echo "vkd3d-proton: $(git -C "$ROOT/vkd3d-proton" rev-parse HEAD)"
    echo "we-proton: $(git -C "$ROOT" rev-parse HEAD)$(git -C "$ROOT" diff --quiet HEAD -- patches || echo '-dirty')"
    for p in "$ROOT"/patches/wine/*.patch; do echo "patch: wine/$(basename "$p")"; done
    for p in $VKD3D_PATCHES; do echo "patch: vkd3d-proton/$(basename "$p")"; done
} > "$DIST/we-proton-build-info"
# Wine and vkd3d-proton are LGPL: ship the changes with the binaries, plus how to use them
mkdir -p "$DIST/we-proton-patches/wine"
cp "$ROOT"/patches/wine/*.patch "$DIST/we-proton-patches/wine/"
if [ -n "$VKD3D_PATCHES" ]; then
    mkdir -p "$DIST/we-proton-patches/vkd3d-proton"
    cp $VKD3D_PATCHES "$DIST/we-proton-patches/vkd3d-proton/"
fi
sed -e "s/@NAME@/$NAME/g" -e "s/@BASE@/$BASE/g" -e "s/@WINE@/$(git -C "$ROOT/wine" rev-parse HEAD)/g" \
    -e "s/@VKD3D@/$(git -C "$ROOT/vkd3d-proton" rev-parse HEAD 2>/dev/null)/g" \
    "$ROOT/we/dist-README.md" > "$DIST/README-WE-Proton.md"
export XZ_OPT=-T0
tar -C "$ROOT/build" -cJf "$ROOT/build/$NAME.tar.xz" "$NAME"
(cd "$ROOT/build" && sha512sum "$NAME.tar.xz" > "$NAME.sha512sum")
# The complete corresponding source of the rebuilt binaries: the patched Wine and
# vkd3d-proton trees they were built from, for the release next to the runner.
log "packing $NAME-source.tar.xz"
src_args=(--transform "s,^src-wine,$NAME-source/wine," src-wine)
[ -z "$VKD3D_PATCHES" ] || src_args+=(--transform "s,^src-vkd3d-proton,$NAME-source/vkd3d-proton," src-vkd3d-proton)
tar -C "$OBJ" -cJf "$ROOT/build/$NAME-source.tar.xz" "${src_args[@]}" \
    -C "$DIST" --transform "s,^we-proton-build-info,$NAME-source/we-proton-build-info," we-proton-build-info
log "done: build/$NAME, build/$NAME.tar.xz, build/$NAME.sha512sum, build/$NAME-source.tar.xz"

if [ "$INSTALL" = 1 ]; then
    rm -rf "$RUNNERS/WE-Proton"
    cp -a --reflink=auto "$DIST" "$RUNNERS/WE-Proton"
    log "installed to $RUNNERS/WE-Proton"
fi
