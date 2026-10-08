#!/usr/bin/env bash
# Smoke test for the emulator installed by `scripts/setup.sh redux`: boots the disc headlessly in PCSX-Redux and waits
# for the executable to load OPENSEG, the opening's overlay (tests/replay/probes.lua's booted(), through psxstack's
# boot check: tests/replay/replay.py boot). About 20 s.
# Usage: scripts/check_emulator.sh [--bios openbios|retail|<file>] [--iso <cue>] [--frames N]
#   --bios openbios   the OpenBIOS shipped with the emulator (default)
#   --bios retail     <data checkout>/gamedata/bios/scph7001.bin (scripts/gamedata_dir.sh)
#   --iso <cue>       default disks/us/dcb_us.cue (scripts/setup.sh disc)
#   --frames N        give up after N vsyncs (default 3000)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIOS=openbios
ISO="$ROOT/disks/us/dcb_us.cue"
FRAMES=3000
while [[ $# -gt 0 ]]; do
    case "$1" in
        --bios) BIOS="$2"; shift 2 ;;
        --iso) ISO="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done
[[ -x "$ROOT/bin/redux/pcsx-redux" ]] || { echo "missing $ROOT/bin/redux/pcsx-redux; run scripts/setup.sh redux" >&2; exit 1; }
[[ -f "$ISO" ]] || { echo "missing $ISO; run scripts/setup.sh disc" >&2; exit 1; }
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || PY=python3
exec "$PY" "$ROOT/tests/replay/replay.py" boot --bios "$BIOS" --iso "$ISO" --frames "$FRAMES"
