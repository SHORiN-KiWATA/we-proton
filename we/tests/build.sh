#!/bin/bash
# Build the Windows test programs in this directory (and we/diag) with mingw.
set -e
cd "$(dirname "$0")"
for c in *.c ../diag/*.c; do
    x86_64-w64-mingw32-gcc -O2 -static -o "${c%.c}.exe" "$c" -lws2_32 -lntdll
done
