#!/usr/bin/env bash
# Project-local setup for this fork: the PS1 build's toolchain without sudo, and the disc files from the data
# checkout. Idempotent: a finished step is skipped. Nothing is installed outside the repository.
#
# Usage: scripts/setup.sh [step...]     (default: all of them, in this order)
#   submodules  external/* (maspsx, m2c, permuter, psyq_headers) and psxstack, at their pins
#   binutils    GNU binutils for mipsel-linux-gnu, built into bin/cross (only when the system has none on PATH),
#               plus bin/cross/bin/mipsel-linux-gnu-cpp, a wrapper over the host's cpp (the build runs it with
#               -undef -nostdinc, so any GCC's cpp does; Ubuntu's gcc-mipsel-linux-gnu is the upstream CI's)
#   python      a Python 3.12 (upstream's CI version) for the venv: the system's python3.12, else one uv installs
#               into bin/python (UV_PYTHON_INSTALL_DIR; no system change)
#   venv        .venv with requirements.txt; bin/cross/bin's tools are symlinked into .venv/bin, so activating the
#               venv (as upstream's README asks) is all `make` and tools/*.py need
#   deps        upstream's tools/dl_deps.sh (old GCCs, objdiff-cli, mkpsxiso into bin/), when any is missing
#   gamedata    disks/us/ from the data checkout (scripts/gamedata_dir.sh): SLUS_013.28 and P.DRV, as symlinks
#   disc        (not by default) the whole USA disc image, disks/us/dcb_us.bin + .cue, rebuilt from the data
#               checkout's xz parts and checked by SHA-1: what the emulator and the port read (215 MB)
#   redux       (not by default) the pinned PCSX-Redux, the test oracle, into bin/redux through psxstack's installer
#               (tools/replay/redux.sh): the zip from the data checkout's tools/prebuilt/, SHA-256 checked; test with
#               scripts/check_emulator.sh. $PSXSTACK_DIR names another checkout of the stack than the submodule
#               (stack work in progress)
#   sdl         (not by default) the PC port's window and launcher tools, SDL3, DXC and Dear ImGui at the psxstack
#               submodule's pins, built by the stack's own scripts/setup.sh (its steps sdl3 dxc imgui) in a checkout
#               of the stack at the submodule's commit, bin/psxstack-tools/ (the stack's setup installs into its own
#               checkout's tools/): bin/psxstack-tools/tools/ is the PSXSTACK_TOOLS_DIR scripts/port_build.sh --sdl
#               and scripts/launcher_build.sh pass. A few minutes (SDL3 from source). SDL3's backends follow the -dev
#               headers present (psxstack/scripts/setup.sh --sdl3-desktop-apt lists Ubuntu's); with no X11/Wayland
#               headers only the offscreen driver is built (the self-tests', not a window). Offline shortcut:
#               DCB_SDL_TOOLS_FROM=DIR (e.g. ../dw2003recomp/tools) first symlinks DIR's sdl3, dxc and imgui there;
#               the stack's setup then checks them against its pins and rebuilds any that differ
#   link        in a git worktree: bin/, .venv and disks/ of the main checkout, symlinked (worktrees share them)
#
# Worktrees: everything built goes into the MAIN checkout (bin/, .venv) and is symlinked into the worktree; run
# scripts/worktree_init.sh in a fresh one.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
JOBS="${DCB_JOBS:-$(nproc)}"

# Ubuntu 24.04's binutils, the version upstream's CI assembles and links with.
BINUTILS_VER=2.42
BINUTILS_SHA256=f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800
PYTHON_VER=3.12
# The USA disc image (SLUS-01328, one MODE2/2352 track); its executable and P.DRV have upstream's SHA-1s.
DISC_US_SHA1=b3945b3e76c1fcc554a7614e2b4211d974990105
# PCSX-Redux, the emulator the replays run in (docs/PORT.md "Testing"): the same build dw2003recomp pins, as the data
# checkout keeps it (tools/prebuilt/). It cannot be downloaded from distrib.app by its changeset, so there is no URL.
REDUX_ZIP=PCSX-Redux-bf4c9ceb-linux-x86_64.zip
REDUX_SHA256=5c0138d8a948c021e67aaba62648924c0a6e05d9d933c945d2c5077b4c758980
# Its AppImage is built against glibc 2.43: on an older host (Ubuntu 24.04, CI: 2.39) the installer unpacks this
# pinned runtime sysroot (Ubuntu 26.04 "resolute" packages, SHA-256 each; dw2003recomp's list) from the Ubuntu
# archive and runs the binary through that glibc's loader. DCB_UBUNTU_MIRROR: another archive (CI uses the runners').
REDUX_UBUNTU="${DCB_UBUNTU_MIRROR:-http://archive.ubuntu.com/ubuntu}"
REDUX_SYSROOT_DEBS=(
    "pool/main/g/glibc/libc6_2.43-2ubuntu2_amd64.deb c13775dc0c984403f3fcad229d14507a9f387763bd07ace1e5f93897ee6b8434"
    "pool/main/g/gcc-16/libgcc-s1_16-20260322-1ubuntu1_amd64.deb 2fb4d81c14fdf34251639ae82f5181f9f98480ea16125d535571ac1be9db3065"
    "pool/main/g/gcc-16/libstdc++6_16-20260322-1ubuntu1_amd64.deb a32b9ad585e39bdc7bd15d1eae6293461e6d466859a1dd2f7862d1e40f89b9d8"
    "pool/main/f/fontconfig/libfontconfig1_2.17.1-3ubuntu1_amd64.deb 72ba4fc43155566b46442b741a6892d5d1f1581ba58cdfd037aabf47ad9080f7"
    "pool/main/f/freetype/libfreetype6_2.14.2+dfsg-1_amd64.deb a76ad9102039122ef72b01c9c364e7d8967331414cbe19151c01d1391ff9fe25"
    "pool/main/h/harfbuzz/libharfbuzz0b_12.3.2-2_amd64.deb ec93ea39ddeb5a98179b60eab4af234b6c676f1958106645d1f4b19280d4cd09"
    "pool/main/f/fribidi/libfribidi0_1.0.16-5_amd64.deb 47fb50e96401a46c06d5122cd04cc42316f65530b9f868d2d450576a379d5d34"
    "pool/main/e/expat/libexpat1_2.7.4-1_amd64.deb ef78089497946219cac03209d951bbfff93480104e791b770a7bf6429452e475"
    "pool/main/g/gmp/libgmp10_6.3.0+dfsg-5ubuntu2_amd64.deb a9bbe9d4a4bcd5875bbd0f53e67c379bc881d1c1a80522d51dfb4b107cc31819"
    "pool/main/z/zlib/zlib1g_1.3.dfsg+really1.3.1-1ubuntu3_amd64.deb c45bbbf9c87457d90b8ba38720c5f01b9388c380d40f303c26f5c4953932da26"
    "pool/main/libx/libx11/libx11-6_1.8.13-1_amd64.deb 7e643e76063b94df9159c8b8fd4b8bf53d2fbf119c9889a5cc6842b6be272e4f"
    "pool/main/libx/libx11/libx11-xcb1_1.8.13-1_amd64.deb e20ab288f2f756800ae25e7a827ae70aee64272f692dd3cb8f706764b6767de9"
    "pool/main/libx/libxcb/libxcb1_1.17.0-2ubuntu1_amd64.deb 069e64705e4a5721a223b4d22643bbaeaa63a6efbb56ebd3d7c4a31c4a26fa1c"
    "pool/main/libx/libxcb/libxcb-dri3-0_1.17.0-2ubuntu1_amd64.deb 2e0e3916c647a74e081621f385aa296dab3052b2582b0781d9fbea2f15696ef4"
    "pool/main/libd/libdrm/libdrm2_2.4.131-1_amd64.deb b87e6f715d0795a88b53fbe268004a1acbd0d728da0cc0ab9108aff5ab4e1929"
)

log() { printf '\033[1;34m[setup]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[setup]\033[0m %s\n' "$*" >&2; exit 1; }
fetch() {
    curl -sSfL --retry 5 --retry-all-errors --retry-delay 5 --connect-timeout 30 --max-time 600 -o "$2" "$1"
}

step_submodules() {
    # a worktree borrows the main checkout's submodule objects (no download)
    if [[ "$MAIN" != "$ROOT" ]]; then
        local m
        for m in $(git -C "$ROOT" config -f .gitmodules --get-regexp '\.path$' | awk '{print $2}'); do
            [[ -e "$MAIN/$m/.git" && ! -e "$ROOT/$m/.git" ]] || continue
            git -C "$ROOT" submodule update --init --reference "$MAIN/$m" "$m" >/dev/null 2>&1 || true
        done
    fi
    git -C "$ROOT" submodule update --init --recursive >/dev/null
    log "submodules: $(git -C "$ROOT" submodule status | awk '{print $2}' | tr '\n' ' ')"
}

step_binutils() {
    local prefix="$MAIN/bin/cross"
    if command -v mipsel-linux-gnu-as >/dev/null && [[ "$(command -v mipsel-linux-gnu-as)" != "$prefix"/* ]] &&
       [[ "$(command -v mipsel-linux-gnu-as)" != "$MAIN/.venv/"* ]]; then
        log "binutils: the system's ($(command -v mipsel-linux-gnu-as)); nothing to build"
        return
    fi
    if [[ ! -x "$prefix/bin/mipsel-linux-gnu-as" ]]; then
        local src="$MAIN/bin/src" tarball
        mkdir -p "$src"
        tarball="$src/binutils-$BINUTILS_VER.tar.xz"
        if [[ ! -f "$tarball" ]]; then
            log "binutils: downloading $BINUTILS_VER"
            fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz" "$tarball.part"
            mv "$tarball.part" "$tarball"
        fi
        echo "$BINUTILS_SHA256  $tarball" | sha256sum -c --quiet - || die "binutils: checksum mismatch"
        rm -rf "$src/binutils-$BINUTILS_VER" "$src/binutils-build"
        tar -C "$src" -xf "$tarball"
        mkdir -p "$src/binutils-build"
        log "binutils: building $BINUTILS_VER for mipsel-linux-gnu with $JOBS jobs (a few minutes)"
        # gnu17: 2.42 names an array `static_assert`, a keyword in C23 (GCC 15's default and later)
        (cd "$src/binutils-build" && CFLAGS="-O2 -std=gnu17" "../binutils-$BINUTILS_VER/configure" \
            --prefix="$prefix" --target=mipsel-linux-gnu \
            --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
            --disable-sim --disable-gprofng --disable-libdecnumber --disable-readline \
            MAKEINFO=true >"$src/binutils.log" 2>&1) || die "binutils: configure failed, see $src/binutils.log"
        { make -C "$src/binutils-build" -j"$JOBS" MAKEINFO=true &&
          make -C "$src/binutils-build" install MAKEINFO=true; } >>"$src/binutils.log" 2>&1 ||
            die "binutils: build failed, see $src/binutils.log"
        rm -rf "$src/binutils-build" "$src/binutils-$BINUTILS_VER" "$src/binutils.log"
    fi
    if [[ ! -x "$prefix/bin/mipsel-linux-gnu-cpp" ]]; then
        command -v cpp >/dev/null || die "binutils: no host cpp (install gcc) for the mipsel-linux-gnu-cpp wrapper"
        cat > "$prefix/bin/mipsel-linux-gnu-cpp" <<'EOF'
#!/bin/sh
# The host's C preprocessor standing in for Ubuntu's mipsel-linux-gnu-cpp (scripts/setup.sh binutils): the build
# and tools/try_match.py run it with -undef -nostdinc and define everything themselves.
exec cpp "$@"
EOF
        chmod +x "$prefix/bin/mipsel-linux-gnu-cpp"
    fi
    log "binutils: $("$prefix/bin/mipsel-linux-gnu-as" --version | head -1) in $prefix"
}

PY=""
step_python() {
    local sys_py
    sys_py="$(command -v "python$PYTHON_VER" || true)"
    if [[ -n "$sys_py" ]]; then
        PY="$sys_py"
    else
        PY="$(ls -d "$MAIN"/bin/python/cpython-"$PYTHON_VER".*/bin/python"$PYTHON_VER" 2>/dev/null | sort -V | tail -1)"
        if [[ -z "$PY" ]]; then
            command -v uv >/dev/null || die "python: no python$PYTHON_VER and no uv (https://docs.astral.sh/uv/) to fetch one"
            # --no-bin: no python3.12 link in ~/.local/bin, the install stays inside bin/python
            UV_PYTHON_INSTALL_DIR="$MAIN/bin/python" uv python install --no-bin "$PYTHON_VER" >/dev/null 2>&1 ||
                die "python: uv could not install $PYTHON_VER"
            PY="$(ls -d "$MAIN"/bin/python/cpython-"$PYTHON_VER".*/bin/python"$PYTHON_VER" | sort -V | tail -1)"
        fi
    fi
    log "python: $PY ($("$PY" --version))"
}

step_venv() {
    local venv="$MAIN/.venv"
    if [[ ! -x "$venv/bin/python" ]] || ! "$venv/bin/python" -c 'import sys; sys.exit(sys.version_info[:2] != (3, 12))'; then
        [[ -n "$PY" ]] || step_python
        rm -rf "$venv"
        "$PY" -m venv "$venv"
        log "venv: created $venv"
    fi
    "$venv/bin/pip" install -q --disable-pip-version-check -r "$ROOT/requirements.txt"
    local t
    if [[ -d "$MAIN/bin/cross/bin" ]]; then
        for t in "$MAIN"/bin/cross/bin/mipsel-linux-gnu-*; do
            ln -sfn "$t" "$venv/bin/$(basename "$t")"
        done
    fi
    log "venv: $("$venv/bin/python" --version), requirements installed; activate it: . .venv/bin/activate"
}

step_deps() {
    local b="$MAIN/bin"
    if [[ -x "$b/gcc-2.95.2-psx/cc1" && -x "$b/gcc-2.7.2-psx/cc1" && -x "$b/gcc-2.8.1-psx/cc1" &&
          -x "$b/objdiff-cli-linux-x86_64" && -x "$b/mkpsxiso-2.20-Linux/bin/dumpsxiso" ]]; then
        log "deps: already in bin/"
        return
    fi
    step_link   # in a worktree, bin/ must already point at the main checkout's
    command -v wget >/dev/null || die "deps: upstream's tools/dl_deps.sh needs wget"
    "$ROOT/tools/dl_deps.sh"
    log "deps: downloaded into bin/"
}

step_gamedata() {
    local gd
    gd="$("$ROOT/scripts/gamedata_dir.sh")" || {
        log "gamedata: no data checkout (clone the private dcb-gamedata beside this repo, or set DCB_GAMEDATA);" \
            "else dump your own disc into disks/us/ as README.md \"Getting the game files\" says"
        return
    }
    local d="$MAIN/disks/us" f
    mkdir -p "$d"
    for f in SLUS_013.28 P.DRV; do
        [[ -f "$gd/gamedata/us/disc/$f" ]] || die "gamedata: $gd/gamedata/us/disc/$f is missing"
        [[ -e "$d/$f" && ! -L "$d/$f" ]] && continue   # the user's own dump stays
        ln -sfn "$gd/gamedata/us/disc/$f" "$d/$f"
    done
    log "gamedata: disks/us/ -> $gd/gamedata/us/disc"
}

step_disc() {
    local gd d="$MAIN/disks/us"
    if [[ -f "$d/dcb_us.bin" ]] && [[ "$(sha1sum < "$d/dcb_us.bin" | cut -d' ' -f1)" == "$DISC_US_SHA1" ]]; then
        log "disc: disks/us/dcb_us.bin already there"
        return
    fi
    gd="$("$ROOT/scripts/gamedata_dir.sh")" || die "disc: no data checkout; put your own image at disks/us/dcb_us.bin"
    mkdir -p "$d"
    cat "$gd"/gamedata/us/dcb_us.bin.xz.part* | xz -dc > "$d/dcb_us.bin.part"
    [[ "$(sha1sum < "$d/dcb_us.bin.part" | cut -d' ' -f1)" == "$DISC_US_SHA1" ]] || die "disc: SHA-1 mismatch"
    mv "$d/dcb_us.bin.part" "$d/dcb_us.bin"
    cp "$gd/gamedata/us/dcb_us.cue" "$d/dcb_us.cue"
    log "disc: disks/us/dcb_us.bin (+ .cue), SHA-1 checked"
}

step_redux() {
    local gd installer stack debs
    stack="${PSXSTACK_DIR:-$ROOT/psxstack}"
    installer="$stack/tools/replay/redux.sh"
    [[ -f "$installer" ]] || die "redux: $installer is missing: a psxstack with tools/replay/ (the submodule at a tag that has it, or PSXSTACK_DIR=<checkout>)"
    gd="$("$ROOT/scripts/gamedata_dir.sh")" || die "redux: no data checkout: the emulator zip is $REDUX_ZIP of its tools/prebuilt/"
    [[ -f "$gd/tools/prebuilt/$REDUX_ZIP" ]] || die "redux: $gd/tools/prebuilt/$REDUX_ZIP is missing"
    debs="$(mktemp)"
    printf '%s\n' "${REDUX_SYSROOT_DEBS[@]}" > "$debs"
    bash "$installer" --dest "$MAIN/bin/redux" --zip "$gd/tools/prebuilt/$REDUX_ZIP" --sha256 "$REDUX_SHA256" \
         --sysroot-debs "$debs" --mirror "$REDUX_UBUNTU"
    rm -f "$debs"
}

step_sdl() {
    local stack dest="$MAIN/bin/psxstack-tools" rev t drivers
    stack="${PSXSTACK_DIR:-$ROOT/psxstack}"
    [[ -f "$stack/scripts/setup.sh" ]] || die "sdl: $stack/scripts/setup.sh is missing (scripts/worktree_init.sh, or PSXSTACK_DIR=<checkout>)"
    rev="$(git -C "$stack" rev-parse HEAD)"
    # A checkout of the stack with its own .git: init + fetch rather than clone, so it also lands on a directory that
    # already holds tools (CI restores bin/psxstack-tools/tools from its cache first). Only tracked files change.
    [[ -d "$dest/.git" ]] || git init -q "$dest"
    if [[ "$(git -C "$dest" rev-parse -q --verify HEAD 2>/dev/null)" != "$rev" ]]; then
        git -C "$dest" cat-file -e "$rev^{commit}" 2>/dev/null || git -C "$dest" fetch -q --no-tags "$stack" HEAD
        git -C "$dest" -c advice.detachedHead=false checkout -q --force --detach "$rev"
    fi
    if [[ -n "${DCB_SDL_TOOLS_FROM:-}" ]]; then
        [[ -d "$DCB_SDL_TOOLS_FROM" ]] || die "sdl: DCB_SDL_TOOLS_FROM=$DCB_SDL_TOOLS_FROM is not a directory"
        for t in sdl3 dxc imgui; do
            [[ -e "$DCB_SDL_TOOLS_FROM/$t" && ! -e "$dest/tools/$t" ]] || continue
            ln -s "$(cd "$DCB_SDL_TOOLS_FROM/$t" && pwd)" "$dest/tools/$t"
            log "sdl: bin/psxstack-tools/tools/$t -> $DCB_SDL_TOOLS_FROM/$t (checked against the pins next)"
        done
    fi
    log "sdl: psxstack $(git -C "$stack" describe --tags --always)'s scripts/setup.sh sdl3 dxc imgui in bin/psxstack-tools"
    # the stack's setup takes cmake and ninja from the PATH (the venv's, as CI pip-installs them; else it pip-installs its own)
    PATH="$MAIN/.venv/bin:$PATH" bash "$dest/scripts/setup.sh" sdl3 dxc imgui
    drivers="$(nm -g --defined-only "$dest/tools/sdl3/lib/libSDL3.a" 2>/dev/null |
               sed -n 's/.* D \(X11\|Wayland\|OFFSCREEN\|PIPEWIRE\|PULSEAUDIO\|ALSA\)_bootstrap$/\1/p' | sort -u | tr '\n' ' ')"
    log "sdl: SDL3's drivers: ${drivers:-none found}"
    if ! grep -qE 'X11|Wayland' <<<"$drivers"; then
        log "sdl: no X11 or Wayland: the build runs on the offscreen driver only (the self-tests), no window. For one," \
            "install the -dev headers (Ubuntu: $(bash "$dest/scripts/setup.sh" --sdl3-desktop-apt | tr -s ' \n' ' '))," \
            "then rm -rf bin/psxstack-tools/tools/sdl3 && scripts/setup.sh sdl"
    fi
    log "sdl: PSXSTACK_TOOLS_DIR=$dest/tools"
}

step_link() {
    [[ "$MAIN" != "$ROOT" ]] || return 0
    local n ex
    # upstream's .gitignore has `bin/`, `.venv/`, `disks/`, which a symlink doesn't match: exclude the links in the
    # repository's shared info/exclude rather than editing upstream's file
    ex="$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)/info/exclude"
    mkdir -p "$(dirname "$ex")"
    for n in /bin /.venv /disks; do
        grep -qxF "$n" "$ex" 2>/dev/null || echo "$n" >> "$ex"
    done
    for n in bin .venv disks; do
        mkdir -p "$MAIN/$n"
        [[ -e "$ROOT/$n" || -L "$ROOT/$n" ]] && continue
        ln -s "$MAIN/$n" "$ROOT/$n"
        log "link: $n -> $MAIN/$n"
    done
}

steps=("$@")
[[ ${#steps[@]} -gt 0 ]] || steps=(submodules binutils python venv deps gamedata)
for s in "${steps[@]}"; do
    case "$s" in -h|--help) sed -n '2,33p' "$0"; exit 0 ;; esac
    declare -F "step_$s" >/dev/null || die "unknown step: $s"
    "step_$s"
done
step_link
log "done"
