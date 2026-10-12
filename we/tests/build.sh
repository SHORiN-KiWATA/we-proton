#!/bin/bash
# Build the Windows test programs in this directory (and we/diag) with mingw.
set -e
cd "$(dirname "$0")"
for c in *.c ../diag/*.c; do
    x86_64-w64-mingw32-gcc -O2 -static -o "${c%.c}.exe" "$c" -lws2_32 -lntdll -lgdi32 -lole32 -loleaut32 -luuid -lshell32 -lwinhttp -lstrmiids -liphlpapi -limm32 -lversion -ld3d9 -ld3d11 -ldxgi -lmfplat -lmfuuid
done
# probes whose behaviour has to be checked in 32-bit programs too
for c in typelib_noflags_probe.c capture_mt_probe.c window_band_probe.c private_ns_probe.c activateapp_probe.c; do
    i686-w64-mingw32-gcc -O2 -static -o "${c%.c}32.exe" "$c" -lole32 -loleaut32 -luuid -lstrmiids
done
i686-w64-mingw32-gcc -O2 -static -o mf_shutdown_probe32.exe mf_shutdown_probe.c -lmfplat -lmfuuid -lole32 -luuid
# the wintun.dll the probe loads is 32-bit
i686-w64-mingw32-gcc -O2 -static -o wintun_probe32.exe wintun_probe.c -liphlpapi
