#!/usr/bin/env bash
# Prints the data checkout: a directory holding `gamedata/` (the game's files per region: gamedata/us/disc/ has the
# executable and P.DRV, gamedata/us/ the disc image) and `tools/prebuilt/` (pinned binaries that can't be downloaded
# everywhere). It is the owner's private repository, never part of this one; without it, dump your own disc into
# disks/<version>/ as README.md "Getting the game files" says. Lookup order:
#   1. $DCB_GAMEDATA (environment)   2. the sibling directory ../dcb-gamedata of the main checkout
# Prints nothing (exit 1) when none exists. Usage: gd="$(scripts/gamedata_dir.sh)" || gd=
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
dir="${DCB_GAMEDATA:-}"
[[ -n "$dir" || ! -d "$MAIN/../dcb-gamedata/gamedata" ]] || dir="$(cd "$MAIN/../dcb-gamedata" && pwd)"
[[ -n "$dir" && -d "$dir/gamedata" ]] || exit 1
echo "$dir"
