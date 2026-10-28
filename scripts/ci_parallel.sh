#!/usr/bin/env bash
# Runs a CI shard's tests side by side (.github/workflows/fork.yaml, the `replay` matrix): the emulator, the port and
# their tests are single-threaded, so a 4-core runner runs four at once. One command per line of stdin (or one per
# argument); empty lines and `#` comments are skipped. Each command runs under bash from the repository's root, its
# output into build/ci-parallel/<n>.log, printed whole when it ends (a collapsed group in the Actions log; a failure's
# log is printed open, with an error annotation), so the outputs never interleave. Then a table of every command's
# status and time.
#
#   scripts/ci_parallel.sh [-j N] [COMMAND ...]     # -j: at most N at once (default: the cores)
#
# Exit 0 when every command exited 0, else 1 (2: usage).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(nproc)"
while getopts 'j:h' opt; do
    case "$opt" in
        j) JOBS="$OPTARG" ;;
        h) sed -n '2,11p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "ci_parallel.sh: -j needs a positive number" >&2; exit 2; }

cmds=()
if (( $# )); then
    cmds=("$@")
else
    while IFS= read -r line; do cmds+=("$line"); done
fi
todo=()
for c in "${cmds[@]}"; do
    [[ "$c" =~ ^[[:space:]]*(#|$) ]] || todo+=("$c")
done
(( ${#todo[@]} )) || { echo "ci_parallel.sh: no command" >&2; exit 2; }

OUT="$ROOT/build/ci-parallel"
rm -rf "$OUT"
mkdir -p "$OUT"
GHA="${GITHUB_ACTIONS:-}"

declare -A index_of start_of
rcs=() secs=()
running=0
# Waits for one command to end and prints its log.
reap() {
    local pid rc i
    wait -n -p pid
    rc=$?
    i="${index_of[$pid]}"
    rcs[i]=$rc
    secs[i]=$(( SECONDS - start_of[$i] ))
    running=$(( running - 1 ))
    if [[ $rc -eq 0 ]]; then
        [[ -n "$GHA" ]] && echo "::group::pass (${secs[i]} s): ${todo[i]}" || echo "=== pass (${secs[i]} s): ${todo[i]}"
        cat "$OUT/$i.log"
        [[ -n "$GHA" ]] && echo "::endgroup::"
    else
        echo "=== FAIL (exit $rc, ${secs[i]} s): ${todo[i]}"
        cat "$OUT/$i.log"
        [[ -n "$GHA" ]] && echo "::error::failed (exit $rc): ${todo[i]}"
    fi
}

for i in "${!todo[@]}"; do
    while (( running >= JOBS )); do reap; done
    echo "start: ${todo[i]}"
    start_of[$i]=$SECONDS
    bash -c "${todo[i]}" > "$OUT/$i.log" 2>&1 < /dev/null &
    index_of[$!]=$i
    running=$(( running + 1 ))
done
while (( running > 0 )); do reap; done

failed=0
echo "ci_parallel: ${#todo[@]} command(s), $JOBS at a time, ${SECONDS} s:"
for i in "${!todo[@]}"; do
    if [[ "${rcs[i]}" -eq 0 ]]; then s=pass; else s="FAIL ${rcs[i]}"; failed=1; fi
    printf '  %-7s %5d s  %s\n' "$s" "${secs[i]}" "${todo[i]}"
done
exit "$failed"
