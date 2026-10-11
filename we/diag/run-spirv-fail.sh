#!/bin/bash
# Mesa dumps every SPIR-V module that spirv_to_nir rejects into build/spirv-fail
# (RADV then crashes on the NULL result). Works with the normal runner.
#
#   we/diag/run-spirv-fail.sh -- <command> [args...]
USAGE="$0 -- <command> [args...]"
. "$(dirname "$0")/common.sh"
mkdir -p "$ROOT/build/spirv-fail"
export MESA_SPIRV_FAIL_DUMP_PATH="$ROOT/build/spirv-fail"
export WINEDEBUG="-all,+timestamp,+pid,+tid,err+all"
exec "$ROOT/we/run-logged.sh" "$@"
