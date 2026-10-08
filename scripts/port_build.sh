#!/usr/bin/env bash
# The PC port's build (docs/PORT.md; psxstack's docs/RUNTIME.md "Build and run"): configures port/ into build/port,
# builds and links build/port/dcb (the link's own check, port_gen.py sections, runs after it), and prints its version.
# No disc needed. With --boot, also boots the disc headless (disks/us/dcb_us.cue: scripts/setup.sh disc) for a few
# hundred frames and requires the boot's first milestone: the executable loads OPENSEG for the opening movie (the
# emulator's `openseg_loaded`, tests/replay/scripts/boot.json), the log ticks every vsync and the run ends at the frame
# cap (status 0: no port_unimplemented, no fatal, no crash, no watchdog).
#
#   scripts/port_build.sh [--boot] [--frames N]   # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#
# Exit 0: built (and with --boot: booted to OPENSEG).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BOOT=0
FRAMES=600
while [[ $# -gt 0 ]]; do
    case "$1" in
        --boot) BOOT=1; shift ;;
        --frames) FRAMES="$2"; shift 2 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "port_build.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || { echo "port_build.sh: $PY is missing: scripts/setup.sh (or scripts/worktree_init.sh in a worktree)" >&2; exit 2; }
export PATH="$ROOT/.venv/bin:$PATH"
for tool in cmake ninja gcc; do
    command -v "$tool" > /dev/null || { echo "port_build.sh: $tool not found (cmake and ninja: .venv/bin/pip install cmake ninja)" >&2; exit 2; }
done
log() { printf '\033[1;34m[port]\033[0m %s\n' "$*"; }

log "configure and build: cmake -S port -B build/port; ninja -C build/port"
cfg=(-S port -B build/port -G Ninja "-DPSXSTACK_PYTHON=$PY")
[[ -n "${PSXSTACK_DIR:-}" ]] && cfg+=("-DPSXSTACK_DIR=$PSXSTACK_DIR")
cmake "${cfg[@]}" > /dev/null
ninja -C build/port
build/port/dcb --version

[[ $BOOT -eq 1 ]] || exit 0

DISC="$ROOT/disks/us/dcb_us.cue"
[[ -f "$DISC" ]] || { echo "port_build.sh: $DISC is missing (scripts/setup.sh disc)" >&2; exit 2; }
LOG="$ROOT/build/port/boot.log"
log "boot: build/port/dcb --disc disks/us/dcb_us.cue --max-frames $FRAMES --log build/port/boot.log"
set +e
build/port/dcb --disc "$DISC" --max-frames "$FRAMES" --log "$LOG" --crash-dir "$ROOT/build/port"
rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "port_build.sh: the boot ended with status $rc (build/port/boot.log; a crash report in build/port/)" >&2; exit 1; }
frames=$(grep -c '^F ' "$LOG" || true)
[[ $frames -eq $FRAMES ]] || { echo "port_build.sh: $frames frame lines in the log, not $FRAMES" >&2; exit 1; }
load=$(grep -m1 -E '^L [0-9]+ tier 1 file 0x[0-9a-f]+ OPENSEG word0 0x00000008 ' "$LOG" || true)
[[ -n "$load" ]] || { echo "port_build.sh: OPENSEG was not loaded in $FRAMES frames (build/port/boot.log)" >&2; exit 1; }
grep -qE '^S [0-9]+ stage 8 file 0$' "$LOG" || { echo "port_build.sh: the stage never became 8 (OPENSEG)" >&2; exit 1; }
log "booted: ${load%% word0*} (stage 8), $frames frames"
