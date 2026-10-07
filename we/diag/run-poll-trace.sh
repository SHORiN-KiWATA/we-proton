#!/bin/bash
# wineserver logs every blocking AFD poll (DBGPOLL) and every datagram connect
# with the polls pending on that socket (DBGCONN), diag patch 0002. Needs the
# diagnostic runner.
#
#   we/diag/run-poll-trace.sh -- <command> [args...]
USAGE="$0 -- <command> [args...]" NEED_DIAG_RUNNER=1
. "$(dirname "$0")/common.sh"
export WINEDEBUG="-all,+timestamp,+pid,+tid"
exec "$ROOT/we/run-logged.sh" "$@"
