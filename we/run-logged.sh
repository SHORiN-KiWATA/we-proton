#!/bin/bash
# Run a command with a Proton log in build/logs (older logs are renamed, not
# overwritten). The command is whatever starts the program through Proton.
#
#   we/run-logged.sh <command> [args...]
#
# WINEDEBUG defaults to a set of channels that is useful for crashes; set it in
# the environment to override.
set -e
[ $# -gt 0 ] || { sed -n '2,8p' "$0"; exit 2; }
LOG=$(cd "$(dirname "$0")/.." && pwd)/build/logs
mkdir -p "$LOG"
[ -f "$LOG/steam-0.log" ] && mv "$LOG/steam-0.log" "$LOG/steam-0.$(date +%Y%m%d-%H%M%S).log"
export PROTON_LOG=1 PROTON_LOG_DIR="$LOG"
export WINEDEBUG="${WINEDEBUG:-+timestamp,+pid,+tid,+seh,+loaddll,+debugstr}"
exec "$@"
