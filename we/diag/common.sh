# Sourced by the run-*.sh scripts here. Parses
#   [--exe NAME] -- <command> [args...]
# into $EXE and the remaining "$@", and points $WE_DIAG_PROTON at the
# diagnostic runner (build/diag-runner, release + the patches in we/diag).
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=
while [ $# -gt 0 ]; do
    case "$1" in
        --exe) EXE=$(echo "$2" | tr '[:upper:]' '[:lower:]'); shift 2 ;;
        --) shift; break ;;
        *) echo "unknown option: $1 (usage: $USAGE)" >&2; exit 2 ;;
    esac
done
[ $# -gt 0 ] || { echo "usage: $USAGE" >&2; exit 2; }
if [ -n "$NEED_EXE" ] && [ -z "$EXE" ]; then echo "usage: $USAGE" >&2; exit 2; fi
export WE_DIAG_PROTON="$ROOT/build/diag-runner/proton"
if [ -n "$NEED_DIAG_RUNNER" ]; then
    [ -x "$WE_DIAG_PROTON" ] || { echo "build/diag-runner is missing" >&2; exit 1; }
    echo "note: the command has to start the program with \$WE_DIAG_PROTON ($WE_DIAG_PROTON)" >&2
fi
