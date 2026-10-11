#!/bin/bash
# Exceptions, debug output, Wine err/fixme messages, DLL loads and thread names
# of one process only. Works with the normal runner.
#
#   we/diag/run-exe-trace.sh --exe NAME.exe -- <command> [args...]
USAGE="$0 --exe NAME.exe -- <command> [args...]" NEED_EXE=1
. "$(dirname "$0")/common.sh"
export WINEDEBUG="-all,+timestamp,+pid,+tid,$EXE:+seh,$EXE:+loaddll,$EXE:+debugstr,$EXE:fixme+all,$EXE:err+all,$EXE:+threadname"
exec "$ROOT/we/run-logged.sh" "$@"
