#!/usr/bin/env python3
"""The PC port's replay test (docs/PORT.md "Testing"): the port replays the emulator's pad scripts and must reach what
the emulator reached. The test is psxstack's (tools/replay/port_test.py; GAME_CONTRACT.md "6. Tests"): this file is
this game's configuration of it (the disc, the scripts and expected files, the venv). NOT RUNNABLE until M1 (issue
#4): the port does not link yet (docs/STATUS.md), so `cmake --build build/port` fails and this test stops at the
build. It is here so that M1 has its gate ready. The stack is the psxstack submodule, or $PSXSTACK_DIR when it names
another checkout of it.

Usage: tests/port/run.py [SCRIPT ...] [--m32] [--sanitize] [--cd-speed instant|realistic] [--out DIR] [-j N]

Builds the port if needed (cmake -S port -B build/port -G Ninja; cmake --build), and for each script (default: every
tests/replay/scripts/<name>.json with a tests/replay/records/<name>.json; or the names given) runs
  build/port/dcb --disc disks/us/dcb_us.cue --script tests/replay/scripts/<name>.json --log ... --record ...
twice and requires the two logs and records to be byte-identical (determinism), then compares the record's cross-core
view (checkpoint names, stages, maps and stable profile hashes; the overlay and map sequences without frames) with
the emulator's expected file. The emulator's records come from PCSX-Redux's interpreter core (tests/replay/replay.py);
the cross-core view is what the two must share.
Exit codes: 0 pass, 1 fail, 2 something missing.
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PSXSTACK = Path(os.environ.get("PSXSTACK_DIR") or ROOT / "psxstack")
sys.path.insert(0, str(PSXSTACK / "tools/replay"))
import port_test  # noqa: E402  (psxstack's test)

SCRIPTS = ROOT / "tests/replay/scripts"
EXPECTED = ROOT / "tests/replay/records"
DISC = ROOT / "disks/us/dcb_us.cue"
VENV_BIN = ROOT / ".venv/bin"

CFG = port_test.configure(root=ROOT, game_json=ROOT / "port/game/game.json", disc=DISC, scripts_dir=SCRIPTS,
                          expected_dir=EXPECTED, venv_bin=VENV_BIN,
                          m32_log_exact=("boot",), m32_note="the other scripts' frames may differ with the heap "
                          "layout once the port runs; their cross-core view must agree")


def main():
    return port_test.main()


if __name__ == "__main__":
    sys.exit(main())
