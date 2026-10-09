#!/usr/bin/env bash
# The release's packages built locally (docs/PORT.md "Releases"; DECISIONS "Releases: tagged drafts, published by
# hand"): the same steps as .github/workflows/release.yml's package job, in a Docker ubuntu:24.04 container (the CI
# runner's base: the AppImage's glibc floor is 2.39 and its launcher links libstdc++ statically), on a clean checkout
# of a commit: the tools (scripts/setup.sh windows sdl3-desktop appimage), scripts/package_appimage.sh --test and
# scripts/package_windows.sh --test under the container's Wine. The packages land in build/release/. Nothing is tagged,
# pushed or published: a release is a pushed tag (release.yml drafts it; the owner publishes it). Adapted from
# dw2003recomp's scripts/release_local.sh (docs/THIRD_PARTY.md).
#
#   scripts/release_local.sh [--commit REF] [--version V] [--host]
#     --commit REF  what to build (default HEAD; committed, on a branch: uncommitted changes are not in it)
#     --version V   the version in the file names and the programs (default: `git describe` of REF over the vX.Y.Z
#                   tags, as release.yml's, else dev-<commit>)
#     --host        build on this machine instead (this checkout as it is, with its tools: scripts/setup.sh sdl
#                   windows appimage): quicker, but the AppImage needs this host's glibc, and with no static
#                   libstdc++ (Fedora) its launcher needs the system's: packages to try, not to publish
# Needs: docker (the image dcb-release: ubuntu:24.04 with the build packages, SDL's desktop -dev packages and wine,
# built on first use); the network on the first run (the tools; then kept in build/release-local/ between runs, about
# 10 min cold, 2 warm). With the disc (disks/us/dcb_us.bin: scripts/setup.sh disc) mounted read-only, the smoke tests
# also run the bundled game on it, as fork.yaml's jobs do; without it they run without the disc, as release.yml does.
# Output: build/release/dcb-<version>-linux-x86_64.AppImage, its .debug, SHA256SUMS,
# dcb-<version>-windows-x86_64.zip, its -debug.zip, SHA256SUMS-windows.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
WORK="$ROOT/build/release-local"
IMAGE=dcb-release
log() { printf '\033[1;34m[release]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[release]\033[0m %s\n' "$*" >&2; exit 1; }

ref=HEAD version="" host=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --commit) ref="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --host) host=1; shift ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done
out="$ROOT/build/release"
mkdir -p "$out"

if [[ $host -eq 1 ]]; then
    cxx=()
    if [[ "$(g++ -print-file-name=libstdc++.a)" != /* ]]; then
        cxx=(--shared-libstdcxx)
        log "no static libstdc++ here: the AppImage's launcher needs the system's (a package to try, not to publish)"
    fi
    v=(); [[ -z "$version" ]] || v=(--version "$version")
    "$ROOT/scripts/package_appimage.sh" --test "${cxx[@]}" "${v[@]}"
    "$ROOT/scripts/package_windows.sh" --test "${v[@]}"
    log "the packages: $out"
    exit 0
fi

command -v docker > /dev/null || die "docker is required (or --host)"
commit="$(git -C "$ROOT" rev-parse --verify "$ref^{commit}")" || die "no such commit: $ref"
[[ -n "$(git -C "$MAIN" branch -a --contains "$commit")" ]] || die "$ref ($commit) is on no branch: commit it first"
[[ "$ref" != HEAD || -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] ||
    log "uncommitted changes in this checkout are not built (HEAD is $(git -C "$ROOT" rev-parse --short HEAD))"
if [[ -z "$version" ]]; then
    version="$(git -C "$ROOT" describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --always "$commit")"
    [[ "$version" == v* ]] || version="dev-$version"
fi
version="${version#v}"
log "dcb $version = $(git -C "$ROOT" log --oneline -1 "$commit")"

# ---- A clean checkout of the commit, from this repository (no network): its own main checkout, so that setup.sh
# installs the tools into it (kept between runs, as the container's).
src="$WORK/src"
if [[ ! -d "$src/.git" ]]; then
    mkdir -p "$WORK"
    git clone -q --no-checkout "$MAIN" "$src"
fi
git -C "$src" fetch -q --tags "$MAIN" '+refs/heads/*:refs/remotes/local/*'
git -C "$src" -c advice.detachedHead=false checkout -q --force --detach "$commit"
git -C "$src" submodule init psxstack
git -C "$src" config submodule.psxstack.url "$MAIN/.git/modules/psxstack"
git -C "$src" -c protocol.file.allow=always submodule update -q psxstack
git -C "$src" clean -qfdx -e bin/ -e .venv/ -e .home/ -e build/wine-prefix/
mkdir -p "$src/disks/us" "$src/.home"
disc=()
if [[ -f "$ROOT/disks/us/dcb_us.bin" && -f "$ROOT/disks/us/dcb_us.cue" ]]; then
    disc=(-v "$(readlink -f "$ROOT/disks/us/dcb_us.bin"):/work/disks/us/dcb_us.bin:ro"
          -v "$(readlink -f "$ROOT/disks/us/dcb_us.cue"):/work/disks/us/dcb_us.cue:ro")
    log "the disc is mounted: the smoke tests run the bundled game on it"
fi

# ---- The image: ubuntu:24.04 with release.yml's packages (Docker caches it); wine64 for the Windows package's test.
log "image $IMAGE (ubuntu:24.04)"
docker build -q -t "$IMAGE" - > /dev/null <<EOF
FROM ubuntu:24.04
RUN apt-get update -q && DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
    build-essential git curl wget ca-certificates python3 python3-venv python3-pip ninja-build cmake xz-utils file \
    pkg-config binutils wine wine64 $(bash "$ROOT/psxstack/scripts/setup.sh" --sdl3-desktop-apt)
EOF

log "building and testing in the container (log: $WORK/build.log)"
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/work/.home -e "DCB_VERSION=$version" -e WINEDEBUG=-all \
    -v "$src:/work" "${disc[@]}" -w /work "$IMAGE" bash -c '
set -euxo pipefail
git config --global --add safe.directory "*"
[[ -x .venv/bin/python ]] || python3 -m venv .venv
scripts/setup.sh windows sdl3-desktop appimage
scripts/package_appimage.sh --test
mkdir -p build/wine-prefix
WINEPREFIX=/work/build/wine-prefix wineboot --init
scripts/package_windows.sh --test
' > "$WORK/build.log" 2>&1 || { tail -30 "$WORK/build.log"; die "the build or a test failed (log: $WORK/build.log)"; }

built="$src/build/release"
(cd "$built" && sha256sum -c --quiet SHA256SUMS && sha256sum -c --quiet SHA256SUMS-windows) || die "SHA256SUMS does not match"
for f in "dcb-$version-linux-x86_64.AppImage" "dcb-$version-linux-x86_64.debug" SHA256SUMS \
         "dcb-$version-windows-x86_64.zip" "dcb-$version-windows-x86_64-debug.zip" SHA256SUMS-windows; do
    [[ -f "$built/$f" ]] || die "the container made no $f"
    cp "$built/$f" "$out/"
    log "$out/$f ($(du -h "$out/$f" | cut -f1))"
done
grep -E 'smoke test passed|test: passed' "$WORK/build.log" | sed 's/\x1b\[[0-9;]*m//g' | sed 's/^/  /'
