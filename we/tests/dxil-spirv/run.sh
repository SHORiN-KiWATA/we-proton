#!/bin/bash
# Control flow graphs (structurize-test format) that dxil-spirv used to turn into
# invalid SPIR-V. Builds dxil-spirv's structurize-test from the vkd3d-proton
# source that we/overlay-build.sh prepared (patches/vkd3d-proton applied) and
# checks that every *.st here comes out valid.
#
#   we/tests/dxil-spirv/run.sh [--unpatched]
#
#   --unpatched   use vkd3d-proton/subprojects/dxil-spirv as is, to see the bugs
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd ../../.. && pwd)
SRC=$ROOT/build/overlay/src-vkd3d-proton/subprojects/dxil-spirv
OBJ=$ROOT/build/dxil-spirv-test
if [ "${1:-}" = --unpatched ]; then
    SRC=$ROOT/vkd3d-proton/subprojects/dxil-spirv
    OBJ=$ROOT/build/dxil-spirv-test-unpatched
fi
[ -f "$SRC/CMakeLists.txt" ] || { echo "$SRC is missing; run we/overlay-build.sh first" >&2; exit 1; }
cmake -S "$SRC" -B "$OBJ" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDXIL_SPV_MISC_CLI=ON > "$OBJ.log" 2>&1
ninja -C "$OBJ" structurize-test >> "$OBJ.log" 2>&1 || { tail -20 "$OBJ.log" >&2; exit 1; }
fail=0
for st in *.st; do
    # this structurize-test always exits 0; it logs the validator's verdict
    if "$OBJ/structurize-test" "$st" 2>&1 | grep -q 'Validated successfully'; then
        echo "ok    $st"
    else
        echo "FAIL  $st"
        fail=1
    fi
done
exit $fail
