#!/bin/bash
# +syscall trace of one process, without the QueryPerformanceCounter / yield /
# system time calls that drown everything else (diag patch 0001), plus thread
# names. Needs the diagnostic runner.
#
#   we/diag/run-syscall-trace.sh --exe NAME.exe -- <command> [args...]
USAGE="$0 --exe NAME.exe -- <command> [args...]" NEED_EXE=1 NEED_DIAG_RUNNER=1
. "$(dirname "$0")/common.sh"
export WINEDEBUG="-all,+timestamp,+pid,+tid,$EXE:+syscall,$EXE:+threadname"
exec "$ROOT/we/run-logged.sh" "$@"
