#!/usr/bin/env bash
# The launcher (psxstack's launcher/README.md, docs/LAUNCHER.md; docs/PORT.md "Running it"): psxstack/launcher built for
# this game's description (port/game/game.json: its id `dcb`, title, disc and hint) into build/launcher/dcb-launcher,
# SDL3 and Dear ImGui from scripts/setup.sh sdl (bin/psxstack-tools/tools, or $PSXSTACK_TOOLS_DIR), then its self-test
# on SDL's offscreen driver (no display, no disc: the settings directory's lookup, settings.json's round trips, every
# screen walked with injected keys and a virtual gamepad, the play path with the launcher as the game's stand-in). When
# the disc (disks/us/dcb_us.cue) and the window build (build/port-sdl/dcb: scripts/port_build.sh --sdl) are there, the
# self-test also checks the real disc's SHA-1 and runs the real game 300 frames from the launcher's command.
#
#   scripts/launcher_build.sh [--no-test]     # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#
# Exit 0: built and the self-test passed (any failure fails the script).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TEST=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-test) TEST=0; shift ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "launcher_build.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
export PATH="$ROOT/.venv/bin:$PATH"
for tool in cmake ninja g++; do
    command -v "$tool" > /dev/null || { echo "launcher_build.sh: $tool not found" >&2; exit 2; }
done
log() { printf '\033[1;34m[launcher]\033[0m %s\n' "$*"; }

STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"
TOOLS="${PSXSTACK_TOOLS_DIR:-$ROOT/bin/psxstack-tools/tools}"
[[ -f "$TOOLS/sdl3/lib/libSDL3.a" && -f "$TOOLS/imgui/imgui.cpp" ]] ||
    { echo "launcher_build.sh: no SDL3 or Dear ImGui in $TOOLS: scripts/setup.sh sdl" >&2; exit 2; }
log "configure and build: cmake -S ${STACK#"$ROOT/"}/launcher -B build/launcher; ninja -C build/launcher"
cmake -S "$STACK/launcher" -B build/launcher -G Ninja -DPSXSTACK_GAME_JSON="$ROOT/port/game/game.json" \
      -DPSXSTACK_VERSION_ROOT="$ROOT" -DPSXSTACK_TOOLS_DIR="$TOOLS" > /dev/null
ninja -C build/launcher
[[ $TEST -eq 1 ]] || exit 0

env=(SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy)
if [[ -f disks/us/dcb_us.cue && -x build/port-sdl/dcb ]]; then
    env+=("DCB_SELFTEST_DISC=$ROOT/disks/us/dcb_us.cue" "DCB_SELFTEST_GAME=$ROOT/build/port-sdl/dcb")
    log "self-test with the disc and build/port-sdl/dcb"
else
    log "self-test (no disc or no build/port-sdl/dcb: without the real game)"
fi
OUT="$ROOT/build/launcher/self-test.log"
set +e
env "${env[@]}" build/launcher/dcb-launcher --self-test build/launcher-test > "$OUT" 2>&1
rc=$?
set -e
grep -E '^self-test: ' "$OUT" | grep -v '^self-test: FAILED' || true
failed="$(grep '^self-test: FAILED: ' "$OUT" | sed 's/^self-test: FAILED: //' || true)"
if [[ $rc -eq 0 ]]; then
    log "self-test passed (screens in build/launcher-test/launcher-self-test/screens/)"
else
    printf 'launcher_build.sh: the self-test failed (status %s; %s):\n%s\n' "$rc" "build/launcher/self-test.log" "$failed" >&2
    exit 1
fi
