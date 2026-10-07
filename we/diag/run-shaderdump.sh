#!/bin/bash
# vkd3d-proton dumps every DXIL shader it translates (and the SPIR-V it made)
# into build/shader-dump; Mesa dumps the SPIR-V modules it rejects into
# build/spirv-fail. Works with the normal runner.
#
#   we/diag/run-shaderdump.sh -- <command> [args...]
USAGE="$0 -- <command> [args...]"
. "$(dirname "$0")/common.sh"
mkdir -p "$ROOT/build/shader-dump" "$ROOT/build/spirv-fail"
export VKD3D_SHADER_DUMP_PATH="$ROOT/build/shader-dump"
export MESA_SPIRV_FAIL_DUMP_PATH="$ROOT/build/spirv-fail"
export WINEDEBUG="-all,+timestamp,+pid,+tid,err+all"
exec "$ROOT/we/run-logged.sh" "$@"
