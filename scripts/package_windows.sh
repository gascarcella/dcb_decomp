#!/usr/bin/env bash
# The Windows release package (docs/PORT.md "Releases"; DECISIONS "Releases: tagged drafts, published by hand"): the
# game (the window build) and the launcher cross-compiled for Windows x86_64 in Release (scripts/build_windows.sh), in
# one folder with the mods' manifests, LICENSES/ and a README, zipped; the two PDBs (the symbols of crash reports and
# minidumps) in a second zip. Players supply their own disc in the launcher. Adapted from dw2003recomp's
# scripts/package_windows.sh (docs/THIRD_PARTY.md).
#
#   scripts/package_windows.sh [--version V] [--out DIR] [--test] [--jobs N]
#     --version V  the version in the file names and the programs (default $DCB_VERSION, else the tag being built
#                  (GITHUB_REF refs/tags/vX.Y.Z), else `git describe` over the vX.Y.Z tags, else dev-<commit>; a
#                  leading v dropped)
#     --out DIR    where the zips and SHA256SUMS-windows go (default build/release/)
#     --test       then the package itself, unzipped, under Wine (build/wine-prefix/): the launcher's self-test with
#                  DCB_SELFTEST_GAME=beside (the bundled game and mods, found by the launcher's own lookup) and, when
#                  disks/us/dcb_us.cue exists, the disc (the bundled game runs 300 frames from the launcher's command)
#     --jobs N     parallel jobs (default: every core)
# Needs: scripts/setup.sh windows; cmake and ninja (the venv's); wine for --test.
# Outputs: <out>/dcb-<version>-windows-x86_64.zip (dcb-<version>-windows-x86_64/: dcb.exe, dcb-launcher.exe, mods/,
# README.txt, LICENSES/), <out>/dcb-<version>-windows-x86_64-debug.zip (the PDBs), <out>/SHA256SUMS-windows; the
# staging under build/release/windows/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="${DCB_JOBS:-$(nproc)}"
log() { printf '\033[1;34m[windows-package]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[windows-package]\033[0m %s\n' "$*" >&2; exit 1; }

version="${DCB_VERSION:-}" out="$ROOT/build/release" test=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) version="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --test) test=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

# ---- The version: the release tag being built (CI), else git describe over the release tags, as
# psxstack/cmake/version.cmake names the build (PSXSTACK_VERSION, set below, makes the programs say the same).
if [[ -z "$version" ]]; then
    if [[ "${GITHUB_REF:-}" == refs/tags/v* ]]; then
        version="${GITHUB_REF#refs/tags/}"
    else
        version="$(git -C "$ROOT" describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --always --dirty)"
        [[ "$version" == v* ]] || version="dev-$version"
    fi
fi
version="${version#v}"
[[ "$version" =~ ^[A-Za-z0-9._+-]+$ ]] || die "a version of letters, digits and ._+- only: '$version'"
export PSXSTACK_VERSION="$version"
base="dcb-$version-windows-x86_64"
name="$base.zip"
debug="$base-debug.zip"

TOOLS="${PSXSTACK_TOOLS_DIR:-$ROOT/bin/psxstack-tools/tools}"
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || die "$PY is missing: scripts/setup.sh (or scripts/worktree_init.sh in a worktree)"
for f in "$TOOLS/sdl3-windows/share/licenses/SDL3/LICENSE.txt" "$TOOLS/imgui/LICENSE.txt" "$TOOLS/llvm-mingw/LICENSE.TXT"; do
    [[ -f "$f" ]] || die "no $f: scripts/setup.sh windows"
done
pkg="$ROOT/packaging/windows"
STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"

# ---- The two programs (scripts/build_windows.sh's build directories: build/port-win, build/launcher-win).
work="$ROOT/build/release/windows"
mkdir -p "$work" "$out"
out="$(cd "$out" && pwd)"
log "dcb $version for Windows: scripts/build_windows.sh (log: $work/build.log)"
"$ROOT/scripts/build_windows.sh" --jobs "$JOBS" > "$work/build.log" 2>&1 ||
    { tail -n 30 "$work/build.log" >&2; die "the Windows build failed (the whole output: $work/build.log)"; }
game=build/port-win launcher=build/launcher-win
for f in "$game/dcb.exe" "$game/dcb.pdb" "$launcher/dcb-launcher.exe" "$launcher/dcb-launcher.pdb"; do
    [[ -f "$f" ]] || die "the build made no $f"
done
[[ -n "$(ls "$game/mods"/*/mod.json 2>/dev/null)" ]] || die "the game's build has no mods/*/mod.json"
# The programs say the package's version (the launcher shows it; a crash report carries it).
got="$(WINEDEBUG=-all wine "$game/dcb.exe" --version 2>/dev/null | head -1 || true)"
[[ -z "$got" || "$got" == "dcb $version "* ]] || die "dcb.exe --version says '$got', not $version"

# ---- The package folder and the debug folder.
stage="$work/$base"
syms="$work/$base-debug"
rm -rf "$stage" "$syms"
mkdir -p "$stage/LICENSES" "$syms"
cp "$game/dcb.exe" "$launcher/dcb-launcher.exe" "$stage/"
cp -r "$game/mods" "$stage/mods"
sed "s/@VERSION@/$version/g" "$pkg/README.txt" > "$stage/README.txt"
cp "$ROOT/LICENSE" "$stage/LICENSES/dcb_decomp.txt"
cp "$STACK/LICENSE" "$stage/LICENSES/psxstack.txt"
cp "$TOOLS/sdl3-windows/share/licenses/SDL3/LICENSE.txt" "$stage/LICENSES/SDL3.txt"
cp "$TOOLS/imgui/LICENSE.txt" "$stage/LICENSES/imgui.txt"
cp "$ROOT/packaging/appimage/LICENSES/ProggyClean.txt" "$ROOT/packaging/appimage/LICENSES/ProggyForever.txt" "$stage/LICENSES/"
cp "$TOOLS/llvm-mingw/LICENSE.TXT" "$stage/LICENSES/llvm.txt"
cp "$pkg/LICENSES/"*.txt "$stage/LICENSES/"
# Windows line endings for the text files a player opens in Notepad (the licences as their authors wrote them).
sed -i 's/\r\?$/\r/' "$stage/README.txt" "$stage/LICENSES/README.txt"
cp "$game/dcb.pdb" "$launcher/dcb-launcher.pdb" "$syms/"
printf 'The PDB files of dcb %s for Windows (%s): the symbols of its crash reports and minidumps.\r\nNot needed to play.\r\n' \
    "$version" "$name" > "$syms/README.txt"

# ---- No game data: only the files listed here, none of them a disc image or a PS1 file.
bad="$(cd "$work" && find "$base" "$base-debug" -type f | grep -viE '\.(exe|pdb|txt|json)$' || true)"
[[ -z "$bad" ]] || die "unexpected files in the package: $bad"

# ---- The zips (one folder each at the root, so that unzipping spills nothing) and the checksums.
rm -f "$out/$name" "$out/$debug"
(cd "$work" && "$PY" -m zipfile -c "$out/$name" "$base")
(cd "$work" && "$PY" -m zipfile -c "$out/$debug" "$base-debug")
(cd "$out" && sha256sum "$name" "$debug" > SHA256SUMS-windows)
log "$out/$name ($(du -h "$out/$name" | cut -f1))"
log "$out/$debug ($(du -h "$out/$debug" | cut -f1))"

[[ $test -eq 1 ]] || exit 0

# ---- The package itself, unzipped, under Wine (Wine sees the Unix tree as drive Z:).
command -v wine > /dev/null || die "no wine on PATH for --test"
unz="$work/test"
rm -rf "$unz"
mkdir -p "$unz" "$ROOT/build/wine-prefix"
"$PY" -m zipfile -e "$out/$name" "$unz"
[[ -f "$unz/$base/dcb-launcher.exe" && -f "$unz/$base/dcb.exe" && -n "$(ls "$unz/$base"/mods/*/mod.json 2>/dev/null)" ]] ||
    die "the zip does not unpack to $base/ with the programs and the mods"
export WINEPREFIX="$ROOT/build/wine-prefix" WINEDEBUG=-all
scratch="$unz/selftest"
mkdir -p "$scratch"
vars=(DCB_SELFTEST_VIDEO_DRIVER=offscreen SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy DCB_SELFTEST_GAME=beside)
if [[ -f "$ROOT/disks/us/dcb_us.cue" ]]; then
    vars+=("DCB_SELFTEST_DISC=Z:$ROOT/disks/us/dcb_us.cue")
    log "test: the launcher's self-test under wine ($(wine --version)) with the bundled game, the mods and the disc"
else
    log "test: the launcher's self-test under wine ($(wine --version)) with the bundled game and the mods (no disc)"
fi
if env -u DCB_GAME -u DCB_CONFIG_DIR "${vars[@]}" timeout 900 wine "$unz/$base/dcb-launcher.exe" --self-test "Z:$scratch" \
    > "$scratch.log" 2>&1; then
    log "test: passed: $(grep -o '[0-9]* of [0-9]* checks passed' "$scratch.log" || true)"
else
    grep 'FAILED' "$scratch.log" | head -20 >&2 || true
    die "the package's self-test under wine failed (log: $scratch.log)"
fi
