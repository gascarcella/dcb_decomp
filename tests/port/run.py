#!/usr/bin/env python3
"""The PC port's replay test (docs/PORT.md "Testing"): the port replays the emulator's pad scripts and must reach what
the emulator reached. The test is psxstack's (tools/replay/port_test.py; GAME_CONTRACT.md "6. Tests"): this file is
this game's configuration of it (the disc, the scripts and expected files, the venv). The stack is the psxstack
submodule, or $PSXSTACK_DIR when it names another checkout of it.

State (M1 complete, docs/STATUS.md): all four scripts pass and gate CI, with --sanitize: every checkpoint at the
emulator's stage and map, the stable profile hashes (the checkpoints before the profile exists carry no image, issue
#24), the overlay sequence, twice byte-identical, and the sanitizer build with no ASan/UBSan report (UBSan's
suppressions: tests/port/ubsan.supp).

Usage: tests/port/run.py [SCRIPT ...] [--m32] [--sanitize] [--cd-speed instant|realistic] [--out DIR] [-j N]

Builds the port if needed (cmake -S port -B build/port -G Ninja; cmake --build), and for each script (default: every
tests/replay/scripts/<name>.json with a tests/replay/records/<name>.json; or the names given) runs
  build/port/dcb --disc disks/us/dcb_us.cue --script build/replay/scripts/<name>.json --log ... --record ...
twice and requires the two logs and records to be byte-identical (determinism), then compares the record's cross-core
view (checkpoint names, stages, maps and stable profile hashes; the overlay and map sequences without frames) with
the emulator's expected file. A script that continues another (`"after"`, tests/replay/chain.py) runs combined:
the runner reads the resolved scripts in build/replay/scripts/. The emulator's records come from PCSX-Redux's
interpreter core (tests/replay/replay.py); the cross-core view is what the two must share.
Exit codes: 0 pass, 1 fail, 2 something missing.
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PSXSTACK = Path(os.environ.get("PSXSTACK_DIR") or ROOT / "psxstack")
sys.path.insert(0, str(PSXSTACK / "tools/replay"))
sys.path.insert(0, str(ROOT / "tests/replay"))
import port_test  # noqa: E402  (psxstack's test)
import chain  # noqa: E402  (scripts that continue another: "after")

SCRIPTS = chain.sync()   # every script resolved, under build/replay/scripts/ (tests/replay/chain.py)
EXPECTED = ROOT / "tests/replay/records"
DISC = ROOT / "disks/us/dcb_us.cue"
VENV_BIN = ROOT / ".venv/bin"

CFG = port_test.configure(root=ROOT, game_json=ROOT / "port/game/game.json", disc=DISC, scripts_dir=SCRIPTS,
                          expected_dir=EXPECTED, venv_bin=VENV_BIN, ubsan_suppressions=ROOT / "tests/port/ubsan.supp",
                          m32_log_exact=("boot",), m32_note="the other scripts' frames may differ with the heap "
                          "layout once the port runs; their cross-core view must agree")


def main():
    return port_test.main()


if __name__ == "__main__":
    sys.exit(main())
