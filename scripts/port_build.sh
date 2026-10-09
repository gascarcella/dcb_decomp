#!/usr/bin/env bash
# The PC port's build (docs/PORT.md; psxstack's docs/RUNTIME.md "Build and run"): configures port/ into build/port,
# builds and links build/port/dcb (the link's own check, port_gen.py sections, runs after it), prints its version and
# checks that it refuses to start without a disc (game.json disc_required: status 64, #39). No disc needed. With --boot, also boots the disc headless (disks/us/dcb_us.cue: scripts/setup.sh disc) for a few
# hundred frames and requires the boot's first milestone: the executable loads OPENSEG for the opening movie (the
# emulator's `openseg_loaded`, tests/replay/scripts/boot.json), the log ticks every vsync and the run ends at the frame
# cap (status 0: no port_unimplemented, no fatal, no crash, no watchdog).
#
# With --sdl, the window build instead (docs/PORT.md "Running it"; psxstack's docs/RUNTIME.md "The window"):
# -DPSXSTACK_SDL=ON into build/port-sdl, SDL3 and DXC from scripts/setup.sh sdl (bin/psxstack-tools/tools, or
# $PSXSTACK_TOOLS_DIR), then the stack's input self-test on SDL's offscreen driver (--input-test: every default key and
# a virtual gamepad's buttons reach the pad, the hotkeys never do; without the disc, on the stack's pump alone).
# --sdl --boot boots in an offscreen window, paced in real time (10 s for 600 frames), and requires its log and
# its screenshot of frame N-100 to equal the headless build's (build/port, built and booted first): the window changes
# only when the vsyncs happen.
#
#   scripts/port_build.sh [--sdl] [--boot] [--frames N]   # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#
# Exit 0: built (and with --boot: booted to OPENSEG; with --sdl: the input self-test passed).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BOOT=0
SDL=0
FRAMES=600
while [[ $# -gt 0 ]]; do
    case "$1" in
        --boot) BOOT=1; shift ;;
        --sdl) SDL=1; shift ;;
        --frames) FRAMES="$2"; shift 2 ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
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

BUILD=build/port
cfg=(-G Ninja "-DPSXSTACK_PYTHON=$PY")
[[ -n "${PSXSTACK_DIR:-}" ]] && cfg+=("-DPSXSTACK_DIR=$PSXSTACK_DIR")
if [[ $SDL -eq 1 ]]; then
    BUILD=build/port-sdl
    TOOLS="${PSXSTACK_TOOLS_DIR:-$ROOT/bin/psxstack-tools/tools}"
    [[ -f "$TOOLS/sdl3/lib/libSDL3.a" && -x "$TOOLS/dxc/bin/dxc" ]] ||
        { echo "port_build.sh: no SDL3 or DXC in $TOOLS: scripts/setup.sh sdl" >&2; exit 2; }
    cfg+=(-DPSXSTACK_SDL=ON "-DPSXSTACK_TOOLS_DIR=$TOOLS")
fi
log "configure and build: cmake -S port -B $BUILD$([[ $SDL -eq 0 ]] || echo ' -DPSXSTACK_SDL=ON'); ninja -C $BUILD"
cmake -S port -B "$BUILD" "${cfg[@]}" > /dev/null
ninja -C "$BUILD"
"$BUILD/dcb" --version
# no disc: a message and status 64, not a game started on reads that find no data
set +e
"$BUILD/dcb" --max-frames 60 --crash-dir "$ROOT/$BUILD" 2> "$ROOT/$BUILD/no-disc.log"
rc=$?
set -e
[[ $rc -eq 64 ]] && grep -q 'no disc' "$ROOT/$BUILD/no-disc.log" ||
    { cat "$ROOT/$BUILD/no-disc.log" >&2; echo "port_build.sh: without a disc the port ended with status $rc, not 64" >&2; exit 1; }
log "no disc: refused (status 64)"

DISC="$ROOT/disks/us/dcb_us.cue"
# SDL's offscreen video and dummy audio: no display, no sound device (CI's runners have neither)
offscreen() { SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy "$@"; }
if [[ $SDL -eq 1 ]]; then
    log "input self-test: $BUILD/dcb --input-test --fps 0 (offscreen, no disc)"
    set +e
    offscreen "$BUILD/dcb" --input-test --fps 0 --crash-dir "$ROOT/$BUILD" > "$ROOT/$BUILD/input-test.log" 2>&1
    rc=$?
    set -e
    [[ $rc -eq 0 ]] || { tail -20 "$ROOT/$BUILD/input-test.log" >&2; echo "port_build.sh: the input self-test ended with status $rc ($BUILD/input-test.log)" >&2; exit 1; }
    log "input self-test: $(grep -m1 -o 'exit 0 after .*' "$ROOT/$BUILD/input-test.log")"
fi

[[ $BOOT -eq 1 ]] || exit 0

[[ -f "$DISC" ]] || { echo "port_build.sh: $DISC is missing (scripts/setup.sh disc)" >&2; exit 2; }
LOG="$ROOT/$BUILD/boot.log"
SHOT="$ROOT/$BUILD/boot-$((FRAMES - 100)).ppm"
run=("$BUILD/dcb" --disc "$DISC" --max-frames "$FRAMES" --log "$LOG" --screenshot "$((FRAMES - 100)):$SHOT"
     --crash-dir "$ROOT/$BUILD")
set +e
if [[ $SDL -eq 1 ]]; then
    log "boot: $BUILD/dcb --disc disks/us/dcb_us.cue --window --max-frames $FRAMES --log $BUILD/boot.log (offscreen, real time)"
    offscreen "${run[@]}" --window > "$ROOT/$BUILD/boot.out" 2>&1
else
    log "boot: $BUILD/dcb --disc disks/us/dcb_us.cue --max-frames $FRAMES --log $BUILD/boot.log"
    "${run[@]}"
fi
rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "port_build.sh: the boot ended with status $rc ($BUILD/boot.log; a crash report in $BUILD/)" >&2; exit 1; }
frames=$(grep -c '^F ' "$LOG" || true)
[[ $frames -eq $FRAMES ]] || { echo "port_build.sh: $frames frame lines in the log, not $FRAMES" >&2; exit 1; }
load=$(grep -m1 -E '^L [0-9]+ tier 1 file 0x[0-9a-f]+ OPENSEG word0 0x00000008 ' "$LOG" || true)
[[ -n "$load" ]] || { echo "port_build.sh: OPENSEG was not loaded in $FRAMES frames ($BUILD/boot.log)" >&2; exit 1; }
grep -qE '^S [0-9]+ stage 8 file 0$' "$LOG" || { echo "port_build.sh: the stage never became 8 (OPENSEG)" >&2; exit 1; }
log "booted: ${load%% word0*} (stage 8), $frames frames"

[[ $SDL -eq 1 ]] || exit 0
# The window run against the headless one: the same log and the same picture (psxstack's docs/RUNTIME.md "Real time").
"$0" --boot --frames "$FRAMES"
cmp -s "$LOG" "$ROOT/build/port/boot.log" ||
    { echo "port_build.sh: the window run's log differs from the headless run's (diff $BUILD/boot.log build/port/boot.log)" >&2; exit 1; }
cmp -s "$SHOT" "$ROOT/build/port/boot-$((FRAMES - 100)).ppm" ||
    { echo "port_build.sh: the window run's frame $((FRAMES - 100)) differs from the headless run's" >&2; exit 1; }
log "the window run's log and frame $((FRAMES - 100)) equal the headless run's"
