#!/usr/bin/env bash
# Whether a change needs CI's heavy jobs (.github/workflows/fork.yaml's `changes` job; the idea is dw2003recomp's
# scripts/ci_areas.sh). Prints `code=true` or `code=false` for $GITHUB_OUTPUT, and each file's class on stderr.
#
#   scripts/ci_changes.sh FILE...              the class of these paths (as `git diff --name-only` prints them)
#   scripts/ci_changes.sh --diff BASE [HEAD]   what changed from BASE to HEAD (default HEAD)
#   scripts/ci_changes.sh --all                code=true (manual runs, an unknown base)
#
# A file is `none` when no heavy job reads it: the documentation (docs/, any .md, LICENSE, .gitignore, the issue
# templates) and the release's own files (release.yml runs on those: packaging/, the package scripts). README.md is
# `none` here too: the `names` job, which always runs, is the one that reads it (tools/hacks.py --check README.md).
# packaging/steam/ is `code`: game.json's launcher.steam_art names its pictures and the launcher's build embeds them
# (as are its sources, which a picture follows; its README.txt is `none`), so the launcher's jobs must build them.
# Anything else is `code`, and one such file runs every job: a new kind of file runs everything until it gets a rule.
set -euo pipefail

class_of() {
    case "$1" in
        packaging/steam/README.txt) echo none ;;
        packaging/steam/*) echo code ;;
        docs/*|*.md|LICENSE|.gitignore|.github/ISSUE_TEMPLATE/*) echo none ;;
        .github/workflows/release.yml|packaging/*|scripts/package_appimage.sh|scripts/package_windows.sh|scripts/release_local.sh) echo none ;;
        *) echo code ;;
    esac
}

files=()
case "${1:-}" in
    --all) echo "ci_changes: every job" >&2; echo "code=true"; exit 0 ;;
    --diff)
        [[ $# -ge 2 ]] || { echo "usage: $0 --diff BASE [HEAD]" >&2; exit 2; }
        mapfile -t files < <(git diff --name-only "$2" "${3:-HEAD}")
        echo "ci_changes: ${#files[@]} file(s) changed from $2 to ${3:-HEAD}" >&2 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) files=("$@") ;;
esac
code=false
for f in "${files[@]}"; do
    c="$(class_of "$f")"
    printf '  %-5s %s\n' "$c" "$f" >&2
    [[ "$c" == none ]] || code=true
done
echo "code=$code"
