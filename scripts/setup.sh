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
    local gd installer stack
    stack="${PSXSTACK_DIR:-$ROOT/psxstack}"
    installer="$stack/tools/replay/redux.sh"
    [[ -f "$installer" ]] || die "redux: $installer is missing: a psxstack with tools/replay/ (the submodule at a tag that has it, or PSXSTACK_DIR=<checkout>)"
    gd="$("$ROOT/scripts/gamedata_dir.sh")" || die "redux: no data checkout: the emulator zip is $REDUX_ZIP of its tools/prebuilt/"
    [[ -f "$gd/tools/prebuilt/$REDUX_ZIP" ]] || die "redux: $gd/tools/prebuilt/$REDUX_ZIP is missing"
    bash "$installer" --dest "$MAIN/bin/redux" --zip "$gd/tools/prebuilt/$REDUX_ZIP" --sha256 "$REDUX_SHA256"
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
    case "$s" in -h|--help) sed -n '2,24p' "$0"; exit 0 ;; esac
    declare -F "step_$s" >/dev/null || die "unknown step: $s"
    "step_$s"
done
step_link
log "done"
