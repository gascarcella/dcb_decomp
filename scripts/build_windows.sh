#!/usr/bin/env bash
# The Windows build (docs/PORT.md "Running it"; DECISIONS "Windows: cross-built from Linux with llvm-mingw, tested
# under Wine"): the port and the launcher cross-compiled for Windows x86_64 from Linux with psxstack's toolchain file
# (psxstack/cmake/windows-x86_64.cmake: llvm-mingw's clang and lld, UCRT, everything static, a PDB beside each
# executable) and SDL3 for Windows. Adapted from dw2003recomp's scripts/build_windows.sh (docs/THIRD_PARTY.md).
#
#   scripts/build_windows.sh [--no-launcher] [--test] [--jobs N]
#     (default)      the game, Release, -DPSXSTACK_SDL=ON, into build/port-win (dcb.exe, dcb.pdb, mods/), and the
#                    launcher (built from port/game/game.json) into build/launcher-win (dcb-launcher.exe, its PDB)
#     --no-launcher  the game only
#     --test         then under Wine (wine on PATH; the prefix build/wine-prefix/, headless), each run under `timeout`:
#                    the launcher's self-test (SDL's offscreen video through DCB_SELFTEST_VIDEO_DRIVER, as Wine drops
#                    SDL_VIDEO_DRIVER; with the disc, the real disc's SHA-1 and dcb.exe run 300 frames from the
#                    launcher's command); and with the disc (disks/us/dcb_us.cue: scripts/setup.sh disc) dcb.exe
#                    --input-test, then the boot and title replays (tests/port/run.py --exe build/port-win/dcb.exe
#                    --wine: deterministic, the emulator's cross-core view) and boot's frame log against the Linux
#                    headless build's (build/port, tests/port/run.py boot): with the record and the SPU trace, byte-identical
#     --jobs N       parallel jobs (default: every core)
# Needs: scripts/setup.sh windows (llvm-mingw, SDL3 for Windows, DXC for the hardware renderer's shaders, Dear ImGui at
# psxstack's pins in bin/psxstack-tools/tools, or $PSXSTACK_TOOLS_DIR); cmake and ninja (the venv's).
# PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule. PSXSTACK_VERSION names the build (default: git
# describe over this repository's vX.Y.Z tags: psxstack/cmake/version.cmake).
# Exit 0: everything asked for built (and with --test: passed), else 1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LAUNCHER=1
TEST=0
JOBS="${DCB_JOBS:-$(nproc)}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-launcher) LAUNCHER=0; shift ;;
        --test) TEST=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,23p' "$0"; exit 0 ;;
        *) echo "build_windows.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
log() { printf '\033[1;34m[windows]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[windows]\033[0m %s\n' "$*" >&2; exit 1; }

PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || die "$PY is missing: scripts/setup.sh (or scripts/worktree_init.sh in a worktree)"
export PATH="$ROOT/.venv/bin:$PATH"
for t in cmake ninja; do
    command -v "$t" > /dev/null || die "$t not found (.venv/bin/pip install cmake ninja)"
done
STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"
TOOLS="${PSXSTACK_TOOLS_DIR:-$ROOT/bin/psxstack-tools/tools}"
[[ -x "$TOOLS/llvm-mingw/bin/x86_64-w64-mingw32-clang" && -f "$TOOLS/sdl3-windows/lib/cmake/SDL3/SDL3Config.cmake" &&
   -x "$TOOLS/dxc/bin/dxc" ]] || die "no llvm-mingw, SDL3 for Windows or DXC in $TOOLS: scripts/setup.sh windows"
[[ $LAUNCHER -eq 0 || -f "$TOOLS/imgui/imgui.cpp" ]] || die "no Dear ImGui in $TOOLS: scripts/setup.sh windows"
TOOLCHAIN="$STACK/cmake/windows-x86_64.cmake"
export PSXSTACK_TOOLS_DIR="$TOOLS"   # the toolchain file's lookup, also in CMake's try_compile projects
mkdir -p build

quiet() { # quiet LOG CMD...: the output into LOG, its end shown when the command fails
    local f="$1"; shift
    "$@" > "$f" 2>&1 || { tail -n 30 "$f" >&2; die "failed: $* (the whole output: $f)"; }
}

# ---- The game: the window build (Release), as a GUI-subsystem dcb.exe with its PDB and the mods' manifests.
GAME=build/port-win
cfg=(-G Ninja "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release -DPSXSTACK_SDL=ON
     "-DPSXSTACK_TOOLS_DIR=$TOOLS" "-DPSXSTACK_PYTHON=$PY")
[[ -n "${PSXSTACK_DIR:-}" ]] && cfg+=("-DPSXSTACK_DIR=$PSXSTACK_DIR")
log "game: cmake -S port -B $GAME (windows-x86_64.cmake, Release, -DPSXSTACK_SDL=ON); ninja (log: $GAME.build.log)"
quiet "$GAME.configure.log" cmake -S port -B "$GAME" "${cfg[@]}"
quiet "$GAME.build.log" ninja -C "$GAME" -j "$JOBS"
for f in "$GAME/dcb.exe" "$GAME/dcb.pdb" "$GAME/mods"; do [[ -e "$f" ]] || die "the build made no $f"; done
log "game: $GAME/dcb.exe ($(du -h "$GAME/dcb.exe" | cut -f1)), dcb.pdb ($(du -h "$GAME/dcb.pdb" | cut -f1)), mods/"

# ---- The launcher, from game.json, with the static C++ runtime.
LDIR=build/launcher-win
if [[ $LAUNCHER -eq 1 ]]; then
    log "launcher: cmake -S ${STACK#"$ROOT/"}/launcher -B $LDIR (windows-x86_64.cmake, Release); ninja"
    quiet "$LDIR.configure.log" cmake -S "$STACK/launcher" -B "$LDIR" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DPSXSTACK_LAUNCHER_STATIC_RUNTIME=ON "-DPSXSTACK_GAME_JSON=$ROOT/port/game/game.json" \
        "-DPSXSTACK_VERSION_ROOT=$ROOT" "-DPSXSTACK_TOOLS_DIR=$TOOLS"
    quiet "$LDIR.build.log" ninja -C "$LDIR" -j "$JOBS"
    for f in "$LDIR/dcb-launcher.exe" "$LDIR/dcb-launcher.pdb"; do [[ -f "$f" ]] || die "the build made no $f"; done
    log "launcher: $LDIR/dcb-launcher.exe ($(du -h "$LDIR/dcb-launcher.exe" | cut -f1)), its PDB"
fi

# ---- The executables: x86-64 PE, GUI subsystem, only Windows' own DLLs imported (nothing to ship beside them).
OBJDUMP="$TOOLS/llvm-mingw/bin/x86_64-w64-mingw32-objdump"
exes=("$GAME/dcb.exe"); [[ $LAUNCHER -eq 1 ]] && exes+=("$LDIR/dcb-launcher.exe")
for exe in "${exes[@]}"; do
    headers="$("$OBJDUMP" -p "$exe")"
    grep -qE '^Subsystem[[:space:]]+0*2[[:space:]]' <<<"$headers" || die "$exe is not a Windows GUI program"
    dlls="$(sed -n 's/^[[:space:]]*DLL Name: //p' <<<"$headers" | sort -u | tr '\n' ' ')"
    bad="$(tr ' ' '\n' <<<"$dlls" | grep -iE '^(libc\+\+|libunwind|libwinpthread|libgcc|libstdc|SDL3)' || true)"
    [[ -z "$bad" ]] || die "$exe imports a DLL no package carries: $bad"
    log "$(basename "$exe"): GUI subsystem; imports $dlls"
done

[[ $TEST -eq 1 ]] || exit 0

# ---- Under Wine. Wine sees the Unix tree as drive Z:.
command -v wine > /dev/null || die "no wine on PATH for --test"
export WINEPREFIX="$ROOT/build/wine-prefix" WINEDEBUG=-all
mkdir -p "$WINEPREFIX"
DISC="$ROOT/disks/us/dcb_us.cue"
log "wine: $(wine --version)"
if [[ $LAUNCHER -eq 1 ]]; then
    st="$ROOT/$LDIR/selftest"
    rm -rf "$st"
    mkdir -p "$st"
    vars=(DCB_SELFTEST_VIDEO_DRIVER=offscreen SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy)
    if [[ -f "$DISC" ]]; then
        vars+=("DCB_SELFTEST_DISC=Z:$DISC" "DCB_SELFTEST_GAME=Z:$ROOT/$GAME/dcb.exe")
        log "launcher: self-test under wine, with the disc and $GAME/dcb.exe (log: $LDIR/selftest.log)"
    else
        log "launcher: self-test under wine, without the disc (log: $LDIR/selftest.log)"
    fi
    set +e
    env -u DCB_GAME -u DCB_CONFIG_DIR "${vars[@]}" timeout 900 wine "$LDIR/dcb-launcher.exe" --self-test "Z:$st" \
        > "$LDIR/selftest.log" 2>&1
    rc=$?
    set -e
    passed="$(grep -o '[0-9]* of [0-9]* checks passed' "$LDIR/selftest.log" || true)"
    if [[ $rc -ne 0 ]]; then
        grep 'FAILED' "$LDIR/selftest.log" | head -20 >&2 || true
        die "the launcher's self-test under wine failed (status $rc; ${passed:-no summary}; $LDIR/selftest.log)"
    fi
    log "launcher: self-test passed under wine: $passed"
fi
if [[ ! -f "$DISC" ]]; then
    log "no disc ($DISC: scripts/setup.sh disc): the game's checks under wine skipped"
    exit 0
fi

log "game: --input-test under wine (log: $GAME/input-test.log)"
set +e
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy timeout 600 wine "$GAME/dcb.exe" --disc "Z:$DISC" --input-test \
    --fps 0 --crash-dir "Z:$ROOT/$GAME" > "$GAME/input-test.log" 2>&1
rc=$?
set -e
[[ $rc -eq 0 ]] || { tail -20 "$GAME/input-test.log" >&2; die "the input self-test under wine ended with status $rc ($GAME/input-test.log)"; }
log "game: input self-test under wine: $(grep -m1 -o 'exit 0 after .*' "$GAME/input-test.log" || echo passed)"

log "game: the boot and title replays under wine (tests/port/run.py --exe $GAME/dcb.exe --wine)"
timeout 1800 "$PY" tests/port/run.py boot title --exe "$GAME/dcb.exe" --wine --out build/port-test-wine ||
    die "the replays under wine failed (build/port-test-wine/)"
log "game: boot on the Linux headless build (tests/port/run.py boot), for its frame log"
timeout 900 "$PY" tests/port/run.py boot --out build/port-test-linux > build/port-test-linux.log 2>&1 ||
    { tail -20 build/port-test-linux.log >&2; die "the Linux boot replay failed (build/port-test-linux.log)"; }
cmp -s build/port-test-wine/boot/run1.log build/port-test-linux/boot/run1.log ||
    die "boot's frame log under wine differs from the Linux build's (diff build/port-test-wine/boot/run1.log build/port-test-linux/boot/run1.log)"
w=build/port-test-wine/boot l=build/port-test-linux/boot
for f in run1.json run1.spu.trace; do
    cmp -s "$w/$f" "$l/$f" || die "boot's $f under wine differs from the Linux build's (cmp $w/$f $l/$f)"
done
log "game: boot under wine: its frame log, record and SPU trace equal the Linux headless build's, byte for byte"
log "done"
