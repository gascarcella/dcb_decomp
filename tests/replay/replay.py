#!/usr/bin/env python3
"""The emulator replay tests (docs/PORT.md "Testing"): boot the disc in PCSX-Redux, feed a pad script, hash the player
profile at named checkpoints. The runner is psxstack's (tools/replay/emulator.py and run.lua; GAME_CONTRACT.md
"6. Tests"): this file is this game's configuration of it (the emulator in bin/redux, the disc, the scripts and
their records (tests/replay/records/: upstream's .gitignore has `expected/`, hence the name), the probes tests/replay/probes.lua, the stable hash's volatile ranges) and the names the other tests
import from here. The stack is the psxstack submodule, or $PSXSTACK_DIR when it names another checkout of it.

Usage:
  tests/replay/replay.py run <script.json> [--record] [--repeat N] [--bios openbios|retail] [--out DIR] [-v]
  tests/replay/replay.py check [-j N] [script.json ...]     # every script with an expected file (the test entry point)
  tests/replay/replay.py boot [--bios ...] [--frames N]     # the boot check (scripts/check_emulator.sh)

A script (tests/replay/scripts/<name>.json) is a list of steps (psxstack docs/RUNTIME.md "The replay runners"); the
runner executes it in the emulator and writes result.json plus one profile dump per checkpoint. The driver hashes the
dumps (SHA-1), builds the record and compares it with tests/replay/records/<name>.json, or writes that file with
--record. --repeat N runs the script N times and requires identical records (determinism).

The core: PCSX-Redux's dynarec cannot run this game (the overlay loader's CD read never completes: FILE_LOADER_BUSY
stays 1 and the vblank event stops at frame ~165, with OpenBIOS and the retail BIOS alike; the interpreter core
plays it), so EVERY run here is on the interpreter: the stack's runner adds `-interpreter` only for `--interpreter`
and refuses to record with it, so run_once is wrapped below until psxstack lets a game choose its oracle core
(docs/PORT.md "Testing"). The expected files say so (`"core": "interpreter"`).

Exit codes: 0 pass, 1 mismatch or emulator failure, 2 usage / missing tool.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PSXSTACK = Path(os.environ.get("PSXSTACK_DIR") or ROOT / "psxstack")
sys.path.insert(0, str(PSXSTACK / "tools/replay"))
import emulator  # noqa: E402  (psxstack's runner)
from emulator import (CHECKPOINT_FIELDS, RUN_LUA, bios_path, check_tools, compare, cross_core_view,  # noqa: E402,F401
                      emulator_info, load_script, lua_literal, parse_ints, sha1_file)


def gamedata_dir():
    """The optional data checkout (scripts/gamedata_dir.sh, the same lookup): $DCB_GAMEDATA, else the sibling
    ../dcb-gamedata of the main checkout. None when there is none."""
    d = os.environ.get("DCB_GAMEDATA")
    if not d:
        import subprocess
        common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True).stdout.strip()
        main = Path(common).parent if common else ROOT
        if (main.parent / "dcb-gamedata/gamedata").is_dir():
            d = str(main.parent / "dcb-gamedata")
    return Path(d) if d and Path(d).is_dir() else None


GD = gamedata_dir()
RETAIL_BIOS = (GD / "gamedata/bios/scph7001.bin") if GD else None   # cross-check only: goldens come from OpenBIOS
# The emulator core every run uses (see the docstring).
CORE_ARGS = ("-interpreter",)
# Byte ranges of the PlayerProfile (include/game.h, us) that change with the timing, zeroed for gamestate_sha1_stable
# so the port can compare a checkpoint reached a few frames earlier or later. A literal: the port's inputs read it
# from this file's source (one definition for the emulator's records and the port's).
VOLATILE_RANGES = ((0x10, 0x12),   # profileId: drawn from rand() when the profile is created, and the seed's state
                                   # then depends on how many frames the boot took
                   (0x24, 0x28),   # playTime: frames
                   (0x15E0, 0x23FC),  # cardCopySerials[301][6]: assignCardCopySerial draws them from rand() at
                                      # resetPlayerData, so they depend on the seed's state the same way
                   # The heap the game never writes before the checkpoints: the emulator's is whatever the
                   # allocator left there, the port's is zero (nothing to compare)
                   (0x315, 0x318),    # partners[0]: its last 3 padding bytes (0x295-0x297 of the 0x298)
                   (0x318, 0x848),    # partners[1] and [2]
                   (0x243D, 0x244C),  # savedDecks[0].name after its terminator (deck bytes 0x5-0x13)
                   (0x253C, 0x2540),  # savedDecks[0].unk104
                   (0x2546, 0x2548),  # savedDecks[0].unk10E
                   (0x2548, 0x2768),  # savedDecks[1] and [2]
                   (0x15DF, 0x15E0),  # unk15DF
                   (0x2435, 0x2438),  # unk2435[3]
                   (0x2768, 0x276E),  # rewardCards[3]
                   (0x276E, 0x2771),  # rewardResults[3]
                   (0x2772, 0x2774),  # unk2771 bytes 1-2
                   # The starter card: open_starter.c draws it with rand() % 2, and the rand() sequence's position
                   # depends on how many times the idle loop ran per frame, so which of the two is owned differs
                   (0x14CE, 0x14CF),    # cardCollection[28] (0x14B2 + 28; plain literals: port_inputs reads this with ast)
                   (0x153B, 0x153C))  # cardCollection[137] (0x14B2 + 137)

CFG = emulator.configure(
    root=ROOT, game_json=ROOT / "port/game/game.json", redux_dir=ROOT / "bin/redux", iso=ROOT / "disks/us/dcb_us.cue",
    scripts_dir=ROOT / "tests/replay/scripts", expected_dir=ROOT / "tests/replay/records",
    probes=ROOT / "tests/replay/probes.lua", volatile_ranges=VOLATILE_RANGES, retail_bios=RETAIL_BIOS,
    tree_paths=("src", "include", "config", "mk"), interpreter_args=CORE_ARGS)
# The paths the other tests use.
REDUX, REDUX_VERSION, OPENBIOS = CFG.redux, CFG.redux_version, CFG.openbios
ISO, SCRIPTS, EXPECTED = CFG.iso, CFG.scripts_dir, CFG.expected_dir

_stack_run_once = emulator.run_once


def run_once(script_path, script, bios, out_dir, verbose=False, lua=None, emu_args=(), speed=0, iso=None):
    """The stack's run_once on the interpreter core, always; the record names the core."""
    args = tuple(emu_args) + tuple(a for a in CORE_ARGS if a not in emu_args)
    record = _stack_run_once(script_path, script, bios, out_dir, verbose=verbose, lua=lua, emu_args=args, speed=speed,
                             iso=iso)
    record["core"] = "interpreter"
    return record


emulator.run_once = run_once


class _Subprocess:
    """subprocess as the runner sees it: `boot` builds its own emulator command (cmd_boot), so the core goes in
    there too: `-interpreter` after `-run` when the command is the emulator's."""

    def __getattr__(self, name):
        return getattr(subprocess, name)

    def run(self, cmd, *args, **kwargs):
        if cmd and str(cmd[0]) == str(CFG.redux) and "-run" in cmd and not any(a in cmd for a in CORE_ARGS):
            i = cmd.index("-run") + 1
            cmd = list(cmd[:i]) + list(CORE_ARGS) + list(cmd[i:])
        return subprocess.run(cmd, *args, **kwargs)


emulator.subprocess = _Subprocess()


def main():
    return emulator.main()


if __name__ == "__main__":
    sys.exit(main())
