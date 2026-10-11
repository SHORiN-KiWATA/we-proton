#!/bin/bash
# Logs every NtTerminateProcess with caller PIDs, exit code and user stack
# (DBGTERM, diag patch 0003; the stack dump needs +unwind), plus exceptions and
# DLL loads of one process. Tells a crash apart from being terminated by
# another process. Needs the diagnostic runner.
#
#   we/diag/run-term-trace.sh --exe NAME.exe -- <command> [args...]
USAGE="$0 --exe NAME.exe -- <command> [args...]" NEED_EXE=1 NEED_DIAG_RUNNER=1
. "$(dirname "$0")/common.sh"
export WINEDEBUG="-all,err+process,err+seh,err+unwind,+timestamp,+pid,+tid,$EXE:+seh,$EXE:+loaddll"
exec "$ROOT/we/run-logged.sh" "$@"
