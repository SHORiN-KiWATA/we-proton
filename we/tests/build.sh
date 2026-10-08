#!/bin/bash
# Build the Windows test programs in this directory (and we/diag) with mingw.
set -e
cd "$(dirname "$0")"
for c in *.c ../diag/*.c; do
    x86_64-w64-mingw32-gcc -O2 -static -o "${c%.c}.exe" "$c" -lws2_32 -lntdll -lgdi32 -lole32 -loleaut32 -luuid -lwinhttp
done
# probes whose behaviour has to be checked in 32-bit programs too
for c in typelib_noflags_probe.c; do
    i686-w64-mingw32-gcc -O2 -static -o "${c%.c}32.exe" "$c" -lole32 -loleaut32 -luuid
done
