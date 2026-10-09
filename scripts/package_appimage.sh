#!/usr/bin/env bash
# The Linux release package (docs/PORT.md "Releases"; DECISIONS "Releases: tagged drafts, published by hand"): the
# game (the window build) and the launcher in Release, an AppDir with both in usr/bin/ (the launcher finds the game
# beside itself) and the mods' manifests in usr/bin/mods/, AppRun -> the launcher, a .desktop file, the icon and
# LICENSES/, then appimagetool with the pinned static runtime (no libfuse2 needed to run it). Players supply their own
# disc in the launcher. Adapted from dw2003recomp's scripts/package_appimage.sh (docs/THIRD_PARTY.md); psxstack has no
# packaging of its own (launcher/README.md "Releases": a game's packaging builds the launcher this way).
#
#   scripts/package_appimage.sh [--version V] [--out DIR] [--sdl3 DIR] [--test] [--shared-libstdcxx]
#     --version V  the version in the file name and the programs (default $DCB_VERSION, else the tag being built
#                  (GITHUB_REF refs/tags/vX.Y.Z), else `git describe` over the vX.Y.Z tags, else dev-<commit>; a
#                  leading v dropped)
#     --out DIR    where the AppImage, the .debug file and SHA256SUMS go (default build/release/)
#     --sdl3 DIR   the SDL3 install to link (default bin/psxstack-tools/tools/sdl3-desktop, else .../sdl3); it must have
#                  the desktop backends (X11, Wayland, PipeWire, PulseAudio, ALSA): scripts/setup.sh sdl3-desktop
#     --test       then the AppImage's smoke test: the launcher's --self-test inside the AppImage (extract-and-run,
#                  offscreen) with DCB_SELFTEST_GAME=beside (the bundled game and mods, found by the launcher itself)
#                  and, when disks/us/dcb_us.cue exists, the disc (the bundled game runs 300 frames from the launcher)
#     --shared-libstdcxx  a local test build on a system without the static libstdc++ (Fedora: libstdc++-static): the
#                  launcher then needs the system's libstdc++, so this AppImage is not one to publish
# Needs: scripts/setup.sh sdl (DXC, Dear ImGui), sdl3-desktop (or an sdl3 with the desktop backends), appimage; cmake,
# ninja (the venv's), gcc/g++ (with libstdc++.a), binutils.
# Outputs: <out>/dcb-<version>-linux-x86_64.AppImage, <out>/dcb-<version>-linux-x86_64.debug (the game's debug info, for
# crash reports' addresses), <out>/SHA256SUMS; the build trees and logs under build/release/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="${DCB_JOBS:-$(nproc)}"
log() { printf '\033[1;34m[appimage]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[appimage]\033[0m %s\n' "$*" >&2; exit 1; }

version="${DCB_VERSION:-}" out="$ROOT/build/release" sdl="" test=0 shared_cxx=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) version="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --sdl3) sdl="$2"; shift 2 ;;
        --test) test=1; shift ;;
        --shared-libstdcxx) shared_cxx=1; shift ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

# ---- The version, as scripts/package_windows.sh names its files.
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
export PSXSTACK_VERSION="$version"   # psxstack/cmake/version.cmake: the programs' --version
name="dcb-$version-linux-x86_64.AppImage"
debug="dcb-$version-linux-x86_64.debug"

# ---- The tools.
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || die "$PY is missing: scripts/setup.sh (or scripts/worktree_init.sh in a worktree)"
export PATH="$ROOT/.venv/bin:$PATH"
TOOLS="${PSXSTACK_TOOLS_DIR:-$ROOT/bin/psxstack-tools/tools}"
STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"
[[ -n "$sdl" ]] || { sdl="$TOOLS/sdl3-desktop"; [[ -d "$sdl" ]] || sdl="$TOOLS/sdl3"; }
[[ -f "$sdl/lib/libSDL3.a" && -f "$sdl/lib/cmake/SDL3/SDL3Config.cmake" ]] ||
    die "no SDL3 at $sdl: scripts/setup.sh sdl3-desktop (or --sdl3 DIR)"
for t in cmake ninja gcc g++ strip ldd nm objdump objcopy; do
    command -v "$t" > /dev/null || die "no $t on PATH"
done
# The desktop drivers (psxstack/scripts/setup.sh's SDL3_DESKTOP_DRIVERS): without them the AppImage could only open an
# offscreen window.
syms="$(nm -g --defined-only "$sdl/lib/libSDL3.a" 2>/dev/null)" || die "nm cannot read $sdl/lib/libSDL3.a"
for d in X11_bootstrap Wayland_bootstrap PIPEWIRE_bootstrap PULSEAUDIO_bootstrap ALSA_bootstrap; do
    grep -q " $d\$" <<<"$syms" || die "$sdl has no ${d%_bootstrap} driver: build the release's SDL with" \
        "scripts/setup.sh sdl3-desktop (it names the -dev packages it needs)"
done
[[ -x "$TOOLS/dxc/bin/dxc" ]] || die "no DXC for the hardware renderer's shaders: scripts/setup.sh sdl"
[[ -f "$TOOLS/imgui/imgui.cpp" ]] || die "no Dear ImGui: scripts/setup.sh sdl"
appimage="$ROOT/bin/appimage"
[[ -x "$appimage/appimagetool/AppRun" && -f "$appimage/runtime-x86_64" && -f "$appimage/runtime-LICENSE" ]] ||
    die "no appimagetool or runtime: scripts/setup.sh appimage"
static_cxx=ON
if [[ $shared_cxx -eq 1 ]]; then
    static_cxx=OFF
elif [[ "$(g++ -print-file-name=libstdc++.a)" != /* ]]; then
    die "no static libstdc++ (libstdc++.a) for g++: install it (Fedora: libstdc++-static; Ubuntu's g++ has it)," \
        "or --shared-libstdcxx for a local test build"
fi
log "dcb $version: SDL3 from $sdl"

# ---- The two programs, in Release (the game's -O3 build gives the tests' logs: build_windows.sh's replays compare a
# Release build with the default one).
work="$ROOT/build/release"
mkdir -p "$work" "$out"
out="$(cd "$out" && pwd)"
quiet() { # quiet LOG CMD...: the command's output into LOG, its end shown when it fails
    local f="$1"; shift
    "$@" >> "$f" 2>&1 || { tail -n 40 "$f" >&2; die "failed: $* (the whole output: $f)"; }
}
rm -f "$work/port-sdl.log" "$work/launcher.log"
cfg=(-G Ninja -DCMAKE_BUILD_TYPE=Release "-DSDL3_DIR=$sdl/lib/cmake/SDL3" "-DPSXSTACK_TOOLS_DIR=$TOOLS")
[[ -n "${PSXSTACK_DIR:-}" ]] && cfg+=("-DPSXSTACK_DIR=$PSXSTACK_DIR")
log "building the game (window build, Release; log: $work/port-sdl.log)"
quiet "$work/port-sdl.log" cmake -S port -B "$work/port-sdl" "${cfg[@]}" -DPSXSTACK_SDL=ON "-DPSXSTACK_PYTHON=$PY"
quiet "$work/port-sdl.log" ninja -C "$work/port-sdl" -j "$JOBS"
log "building the launcher (log: $work/launcher.log)"
quiet "$work/launcher.log" cmake -S "$STACK/launcher" -B "$work/launcher" "${cfg[@]}" \
    -DPSXSTACK_LAUNCHER_STATIC_RUNTIME="$static_cxx" "-DPSXSTACK_GAME_JSON=$ROOT/port/game/game.json" \
    "-DPSXSTACK_VERSION_ROOT=$ROOT"
quiet "$work/launcher.log" ninja -C "$work/launcher" -j "$JOBS"
got="$("$work/port-sdl/dcb" --version | head -1)"
[[ "$got" == "dcb $version "* ]] || die "the game says '$got', not $version"
log "$got"

# ---- The AppDir.
appdir="$work/AppDir"
pkg="$ROOT/packaging/appimage"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/scalable/apps" \
    "$appdir/LICENSES"
strip -o "$appdir/usr/bin/dcb" "$work/port-sdl/dcb"
rm -f "$out/$debug"
objcopy --only-keep-debug "$work/port-sdl/dcb" "$out/$debug"
strip -o "$appdir/usr/bin/dcb-launcher" "$work/launcher/dcb-launcher"
[[ -n "$(ls "$work/port-sdl/mods"/*/mod.json 2>/dev/null)" ]] || die "the game's build has no mods/*/mod.json"
cp -r "$work/port-sdl/mods" "$appdir/usr/bin/mods"
ln -s usr/bin/dcb-launcher "$appdir/AppRun"
cp "$pkg/dcb.desktop" "$appdir/dcb.desktop"
cp "$pkg/dcb.desktop" "$appdir/usr/share/applications/"
cp "$pkg/dcb.svg" "$appdir/dcb.svg"
cp "$pkg/dcb.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/"
cp "$ROOT/LICENSE" "$appdir/LICENSES/dcb_decomp.txt"
cp "$STACK/LICENSE" "$appdir/LICENSES/psxstack.txt"
cp "$sdl/share/licenses/SDL3/LICENSE.txt" "$appdir/LICENSES/SDL3.txt"
cp "$TOOLS/imgui/LICENSE.txt" "$appdir/LICENSES/imgui.txt"
cp "$appimage/runtime-LICENSE" "$appdir/LICENSES/appimage-runtime.txt"
cp "$pkg/LICENSES/"*.txt "$appdir/LICENSES/"

# ---- No game data: only the files listed here, none of them a disc image or a PS1 file.
bad="$(cd "$appdir" && find . -type f | grep -vE '^\./usr/bin/(dcb|dcb-launcher|mods/[^/]+/mod\.json)$|^\./LICENSES/[^/]+\.txt$|\.(desktop|svg)$' || true)"
[[ -z "$bad" ]] || die "unexpected files in the AppDir: $bad"

# ---- Only the C library's own libraries (SDL dlopens X11, Wayland, the audio servers, udev, ... at run time).
allowed='linux-vdso\.so\.1|/lib64/ld-linux-x86-64\.so\.2|ld-linux-x86-64\.so\.2|lib(c|m|dl|pthread|rt)\.so\.[0-9]+'
[[ $shared_cxx -eq 0 ]] || allowed="$allowed|libstdc\\+\\+\\.so\\.6|libgcc_s\\.so\\.1"
for bin in dcb dcb-launcher; do
    libs="$(ldd "$appdir/usr/bin/$bin" | awk '{print $1}')"
    bad="$(grep -vE "^($allowed)\$" <<<"$libs" || true)"
    [[ -z "$bad" ]] || die "$bin needs libraries a desktop may not have: $(echo $bad)"
    glibc="$(objdump -T "$appdir/usr/bin/$bin" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1)"
    log "$bin needs: $(echo $libs) (newest glibc symbol: $glibc)"
done
[[ $shared_cxx -eq 0 ]] || log "--shared-libstdcxx: the launcher needs the system's libstdc++ (a test build only)"

# ---- The AppImage.
rm -f "$out/$name"
log "appimagetool -> $out/$name"
rm -f "$work/appimagetool.log"
quiet "$work/appimagetool.log" env ARCH=x86_64 "$appimage/appimagetool/AppRun" --no-appstream \
    --runtime-file "$appimage/runtime-x86_64" "$appdir" "$out/$name"
(cd "$out" && sha256sum "$name" "$debug" > SHA256SUMS)
log "$out/$name ($(du -h "$out/$name" | cut -f1)); $debug ($(du -h "$out/$debug" | cut -f1))"

[[ $test -eq 1 ]] || exit 0

# ---- The smoke test: the launcher inside the AppImage finds the game and the mods beside itself.
scratch="$work/self-test"
rm -rf "$scratch"
mkdir -p "$scratch"
vars=(APPIMAGE_EXTRACT_AND_RUN=1 "TMPDIR=$scratch" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy DCB_SELFTEST_GAME=beside)
if [[ -f "$ROOT/disks/us/dcb_us.cue" ]]; then
    vars+=("DCB_SELFTEST_DISC=$ROOT/disks/us/dcb_us.cue")
    log "smoke test: the AppImage's self-test with the bundled game and the disc"
else
    log "smoke test: the AppImage's self-test with the bundled game (no disks/us/dcb_us.cue: no disc run)"
fi
if env -u DCB_GAME -u DCB_CONFIG_DIR "${vars[@]}" "$out/$name" --self-test "$scratch" > "$scratch.log" 2>&1; then
    log "smoke test passed: $(grep -o '[0-9]* of [0-9]* checks passed' "$scratch.log" || true)"
else
    grep 'FAILED' "$scratch.log" | head -20 >&2 || true
    die "the AppImage's self-test failed (log: $scratch.log)"
fi
