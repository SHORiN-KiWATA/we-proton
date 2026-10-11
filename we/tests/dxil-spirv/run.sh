#!/bin/bash
# Control flow graphs (structurize-test format) that dxil-spirv used to turn into
# invalid SPIR-V. Builds dxil-spirv's structurize-test from the patched
# vkd3d-proton tree (patches/protonprep-valve-staging.sh applies
# patches/vkd3d-proton/we-*.patch to vkd3d-proton/subprojects/dxil-spirv in place)
# and checks that every *.st here comes out valid.
#
#   we/tests/dxil-spirv/run.sh [--unpatched]
#
#   --unpatched   build the submodule's committed HEAD instead, to see the bugs
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd ../../.. && pwd)
SUB=$ROOT/vkd3d-proton/subprojects/dxil-spirv
SRC=$SUB
OBJ=$ROOT/build/dxil-spirv-test
[ -f "$SUB/CMakeLists.txt" ] || { echo "$SUB is missing; run we/build.sh (or protonprep) first" >&2; exit 1; }
if [ "${1:-}" = --unpatched ]; then
    # the working tree is patched; export the committed sources (with their own submodules)
    SRC=$ROOT/build/dxil-spirv-src-unpatched
    OBJ=$ROOT/build/dxil-spirv-test-unpatched
    rm -rf "$SRC"
    mkdir -p "$SRC"
    git -C "$SUB" archive HEAD | tar -x -C "$SRC"
    git -C "$SUB" submodule foreach --quiet --recursive \
        "mkdir -p \"$SRC/\$displaypath\" && git archive HEAD | tar -x -C \"$SRC/\$displaypath\""
fi
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
