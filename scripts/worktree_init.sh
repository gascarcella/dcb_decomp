#!/usr/bin/env bash
# Prepare a fresh git worktree (Orca sessions run in one). A worktree has only tracked files: no bin/ (the
# toolchain), no .venv, no disks/, empty submodules. This links the main checkout's bin/, .venv and disks/ and checks
# the submodules out at their pins from the main checkout's objects (scripts/setup.sh link submodules). Idempotent,
# about a second. The main checkout itself is set up once with scripts/setup.sh (a few minutes: binutils is built).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
if [[ "$MAIN" == "$ROOT" ]]; then
    exec "$ROOT/scripts/setup.sh"
fi
if [[ ! -x "$MAIN/.venv/bin/python" || ! -x "$MAIN/bin/gcc-2.95.2-psx/cc1" ]]; then
    echo "the main checkout $MAIN is not set up: run scripts/setup.sh there first" >&2
    exit 1
fi
[[ -f "$MAIN/disks/us/SLUS_013.28" ]] || "$ROOT/scripts/setup.sh" gamedata
"$ROOT/scripts/setup.sh" link submodules
echo "worktree ready: $ROOT (build: scripts/build.sh)"
