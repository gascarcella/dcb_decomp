#!/usr/bin/env bash
# The PS1 build, as upstream's CI runs it: generate (splat), build, compare every binary with the original.
# Activates .venv (scripts/setup.sh puts the mipsel toolchain in it). VERSION from the environment, us by default.
#
# Usage: scripts/build.sh [--clean] [make args...]   e.g. scripts/build.sh, VERSION=eu scripts/build.sh
#   --clean  `make reset` first (removes build/, asm/, expected/ and the generated files of that version)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[[ -f .venv/bin/activate ]] || { echo "no .venv: run scripts/setup.sh (or scripts/worktree_init.sh in a worktree)" >&2; exit 1; }
# shellcheck disable=SC1091
. .venv/bin/activate
export VERSION="${VERSION:-us}"
if [[ "${1:-}" == "--clean" ]]; then shift; make reset >/dev/null; fi
make generate "$@"
make -j"${DCB_JOBS:-$(nproc)}" "$@"
make compare "$@"
