#!/usr/bin/env bash
# The port's disc-free gate (CLAUDE.md "Commands"; docs/PORT.md "The host-compile probe"): the configure
# (cmake -S port -B build/port: game.json, the build inputs from mk/version/us.mk, psxstack's generators), then
# psxstack's host-compile probe and link check over every us unit (port/tools/port_inventory.py). The summaries
# also go to build/port_inventory/summary.txt, which CI keeps as an artifact.
#
#   scripts/probe.sh [-j N]            # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#
# Exit 0 means everything RAN: the configure, the probe and the link check. The probe's error count is the
# baseline docs/STATUS.md quotes, not a gate, until M1 drives it to zero (then this script gates on it).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="${JOBS:-$(nproc)}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) JOBS="$2"; shift 2 ;;
        -j*) JOBS="${1#-j}"; shift ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "probe.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

# The venv's python; cmake and ninja from the venv's bin (CI installs them there) or from PATH.
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || { echo "probe.sh: $PY is missing: scripts/setup.sh (or scripts/worktree_init.sh in a worktree)" >&2; exit 2; }
export PATH="$ROOT/.venv/bin:$PATH"
for tool in cmake ninja gcc nm; do
    command -v "$tool" > /dev/null || { echo "probe.sh: $tool not found (cmake and ninja: .venv/bin/pip install cmake ninja)" >&2; exit 2; }
done

OUT="$ROOT/build/port_inventory"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
log() { printf '\033[1;34m[probe]\033[0m %s\n' "$*"; }

log "configure: cmake -S port -B build/port"
cfg=(-S port -B build/port -G Ninja "-DPSXSTACK_PYTHON=$PY")
[[ -n "${PSXSTACK_DIR:-}" ]] && cfg+=("-DPSXSTACK_DIR=$PSXSTACK_DIR")
cmake "${cfg[@]}" 2>&1 | tee "$OUT/configure.txt" | grep -E "^-- (Game|port_inputs|port_gen)|CMake Error" || true
grep -q "CMake Error" "$OUT/configure.txt" && { echo "probe.sh: the configure failed (build/port_inventory/configure.txt)" >&2; exit 1; }
{ echo "== configure"; grep -E "^-- (Game|port_inputs|port_gen)" "$OUT/configure.txt"; echo; } >> "$SUMMARY"

log "probe: every us unit at -m64 (port/tools/port_inventory.py probe)"
set +e
"$PY" port/tools/port_inventory.py probe -j "$JOBS" > "$OUT/probe.txt" 2>&1
probe_rc=$?
set -e
[[ $probe_rc -le 1 ]] || { cat "$OUT/probe.txt"; echo "probe.sh: the probe could not run (exit $probe_rc)" >&2; exit 1; }
{ echo "== probe (exit $probe_rc; 1 = units fail, the baseline)"; grep -E "^port inventory|^flags|files compile|^probe:" "$OUT/probe.txt"; echo; } >> "$SUMMARY"
grep -E "files compile|^probe:" "$OUT/probe.txt"

log "link: duplicate and undefined globals (port/tools/port_inventory.py link)"
set +e
"$PY" port/tools/port_inventory.py link -v -j "$JOBS" > "$OUT/link.txt" 2>&1
link_rc=$?
set -e
[[ $link_rc -le 1 ]] || { cat "$OUT/link.txt"; echo "probe.sh: the link check could not run (exit $link_rc)" >&2; exit 1; }
{ echo "== link (exit $link_rc)"; cat "$OUT/link.txt"; } >> "$SUMMARY"
grep -E "^objects|^globals defined|^undefined everywhere|^  [A-Za-z_-]|^link probe" "$OUT/link.txt" | grep -vE "^  (Psy-Q|asm-only|defined in|other|late-bound)" | head -20

log "done: build/port_inventory/summary.txt (probe exit $probe_rc, link exit $link_rc; the counts are the baseline, not a gate)"
