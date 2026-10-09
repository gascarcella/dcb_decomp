#!/usr/bin/env python3
"""Saves moving both ways between the PC port and the emulator (docs/PORT.md "Saves", "Testing"; M4's gate). Adapted
from dw2003recomp's tests/saves/run.py (docs/THIRD_PARTY.md).

Usage: tests/saves/run.py [--out DIR] [--keep] [--card-port MCD] [--card-emulator MCD] [-j N]

1. Both sides save: the port (build/port/dcb --memcard1 FILE, a new card) and the emulator (PCSX-Redux on its
   interpreter core, OpenBIOS, -memcard1 FILE, a new card) each run tests/replay/scripts/new_game.json, whose
   registration ends with the save to File 1; each `saiseg` checkpoint must have new_game's recorded stable hash
   (tests/replay/records/new_game.json).
2. The cards (tests/saves/cards.py): each has File 1 with the profile in the PS1's layout (its size 0x2774, both
   checksums right, its card pointers 4-byte heap addresses), and the two are equal byte for byte but the timing's and
   rand()'s fields and the stale buffer (cards.py masks()).
3. Each side loads the other's card (a copy) with tests/saves/continue.json (the title's Continue, File 1, into
   SAISEG): the `loaded` checkpoint's profile must be the other side's `saiseg` profile byte for byte (the name, the
   deck, the collection, the rand()-drawn bytes and the heap's stale bytes as that side saved them), but the play time
   (it runs on through the load); the two loads' cross-core views (stages, maps, the overlay and map sequences, the
   stable hash) must be equal, and the stable hash new_game's at `saiseg`.
--card-port / --card-emulator skip that side's save run and use the given card (a corrupted copy, to see the checks
fail). Outputs go to build/saves-test/ (--out): each run's log, record and checkpoint dumps, and the cards; --keep
keeps the runs on a pass (the cards stay). No card is committed (game data). Exit 0 pass, 1 fail, 2 something missing.
The two saves run in parallel (the emulator's ~11,700 frames are most of the time), then the two loads.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/replay"))
import replay  # noqa: E402  (this game's emulator configuration: the paths, the core, VOLATILE_RANGES)
sys.path.insert(0, str(ROOT / "tests/saves"))
import cards  # noqa: E402  (the card format checks)

# tests/port/run.py configures psxstack's port test (the build); loaded by path (this file is a run.py too).
_spec = importlib.util.spec_from_file_location("dcb_port_run", ROOT / "tests/port/run.py")
_port_run = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_port_run)
port_test = _port_run.port_test

SAVE_SCRIPT = ROOT / "tests/replay/scripts/new_game.json"
SAVE_RECORD = ROOT / "tests/replay/records/new_game.json"
LOAD_SCRIPT = ROOT / "tests/saves/continue.json"
SAVED, LOADED = "saiseg", "loaded"   # the checkpoints after the save and after the load
PLAY_TIME = (0x24, 0x28)              # PlayerProfile.playTime: it runs on through the load
RUN_TIMEOUT = 900


def stable_sha1(data):
    stable = bytearray(data)
    for lo, hi in replay.VOLATILE_RANGES:
        stable[lo:hi] = bytes(hi - lo)
    return hashlib.sha1(stable).hexdigest()


def run_emulator(script_path, card, out_dir):
    """The script in PCSX-Redux with `card` in slot 1 (created by the emulator when missing) and a new card in slot 2;
    returns (the record's cross-core view, {checkpoint: image bytes}, frames). The stack's run_once always starts from
    new cards, so this is its command (on this game's core) with the card given."""
    script = replay.load_script(script_path)
    out_dir.mkdir(parents=True, exist_ok=True)
    lua_script = out_dir / "script.lua"
    lua_script.write_text("return " + replay.lua_literal(replay.parse_ints(script)) + "\n")
    env = replay.emulator.replay_env(lua_script, out_dir)
    card2 = out_dir / "memcard2.mcd"
    card2.unlink(missing_ok=True)
    cmd = [str(replay.REDUX), "-no-ui", "-stdout", "-testmode", "-run", *replay.CORE_ARGS, "-iso", str(replay.ISO),
           "-bios", str(replay.OPENBIOS), "-memcard1", str(card.resolve()), "-memcard2", str(card2), "-dofile",
           str(replay.RUN_LUA)]
    log = out_dir / "emulator.log"
    with open(log, "w") as f:
        proc = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT,
                              timeout=script["max_frames"] / 25 + 120)
    result_path = out_dir / "result.json"
    if not result_path.exists():
        tail = "\n    ".join(log.read_text(errors="replace").splitlines()[-10:])
        raise RuntimeError(f"emulator exited {proc.returncode} without result.json ({log}):\n    {tail}")
    result = json.loads(result_path.read_text())
    if result.get("status") != "ok" or proc.returncode != 0:
        raise RuntimeError(f"emulator: exit {proc.returncode}: {result.get('message')} ({log})")
    images = {cp["name"]: (out_dir / cp["gamestate_file"]).read_bytes()
              for cp in result["checkpoints"] if replay.emulator.has_image(cp)}
    record = {"checkpoints": [], "overlay_sequence": result["overlay_sequence"],
              "map_sequence": result["map_sequence"]}
    for cp in result["checkpoints"]:
        entry = {k: cp[k] for k in ("name", "stage", "map")}
        if cp["name"] in images:
            entry["gamestate_sha1_stable"] = stable_sha1(images[cp["name"]])
        else:
            entry["image"] = False
        record["checkpoints"].append(entry)
    return replay.cross_core_view(record), images, result["frames"]


def run_port(binary, script_path, card, out_dir):
    """The script in the port with `card` in slot 1 (created formatted when missing); returns (the record's cross-core
    view, {checkpoint: image bytes}, frames)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    record, log, err = out_dir / "record.json", out_dir / "port.log", out_dir / "port.stderr"
    dumps = out_dir / "checkpoints"
    if dumps.exists():
        shutil.rmtree(dumps)
    dumps.mkdir()
    cmd = [str(binary), "--disc", str(replay.ISO), "--script", str(script_path), "--memcard1", str(card.resolve()),
           "--log", str(log), "--record", str(record)]
    env = dict(os.environ, DCB_PORT_CHECKPOINT_DIR=str(dumps))
    with open(err, "w") as f:
        proc = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT)
    if not record.exists() or record.stat().st_size == 0:
        tail = "\n    ".join(err.read_text(errors="replace").splitlines()[-10:])
        raise RuntimeError(f"port: exit {proc.returncode} without a record ({err}):\n    {tail}")
    rec = json.loads(record.read_text())
    if proc.returncode != 0 or rec.get("status") != 0:
        raise RuntimeError(f"port: exit {proc.returncode}, status {rec.get('status')}: {rec.get('reason')} ({err})")
    images = {f.stem.split("_", 1)[1]: f.read_bytes() for f in dumps.glob("cp*_*.bin")}
    return replay.cross_core_view(rec), images, rec["frames"]


def describe_diff(a, b):
    """The differing ranges of two profile images, in words."""
    diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    runs = []
    for i in diff:
        if runs and i == runs[-1][1]:
            runs[-1][1] = i + 1
        else:
            runs.append([i, i + 1])
    text = ", ".join(f"{lo:#x}..{hi:#x}" for lo, hi in runs[:8]) + (" ..." if len(runs) > 8 else "")
    return f"{len(diff)} byte(s) differ at {text}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="output directory (default build/saves-test/)")
    ap.add_argument("--keep", action="store_true", help="keep the run directories on a pass")
    ap.add_argument("--card-port", help="use this card as the port's save (skips the port's save run)")
    ap.add_argument("--card-emulator", help="use this card as the emulator's save (skips the emulator's save run)")
    ap.add_argument("-j", "--jobs", type=int, default=int(os.environ.get("DCB_JOBS", "0")) or None,
                    help="build jobs (default: $DCB_JOBS, else Ninja's)")
    args = ap.parse_args()

    missing = [str(p.relative_to(ROOT)) for p in (replay.ISO, SAVE_SCRIPT, SAVE_RECORD, LOAD_SCRIPT) if not p.exists()]
    missing += [] if replay.REDUX.exists() else ["bin/redux (scripts/setup.sh redux)"]
    try:
        env = port_test.tool_env()
    except port_test.Missing as e:
        missing.append(str(e))
    if missing:
        print("saves: missing " + ", ".join(missing), file=sys.stderr)
        return 2
    expected_rec = json.loads(SAVE_RECORD.read_text())
    if expected_rec.get("script_sha1") != replay.sha1_file(SAVE_SCRIPT):
        print(f"saves: {SAVE_SCRIPT.relative_to(ROOT)} changed since {SAVE_RECORD.relative_to(ROOT)} was recorded",
              file=sys.stderr)
        return 1
    expected = {cp["name"]: cp.get("gamestate_sha1_stable") for cp in expected_rec["checkpoints"]}[SAVED]
    given = {"port": Path(args.card_port).read_bytes() if args.card_port else None,
             "emulator": Path(args.card_emulator).read_bytes() if args.card_emulator else None}  # before out is cleared
    out = (Path(args.out) if args.out else ROOT / "build/saves-test").resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    try:
        binary = port_test.build("build/port", [], args.jobs, env)
    except (RuntimeError, subprocess.CalledProcessError) as e:
        print(f"saves: FAIL: {e}")
        return 1
    print(f"saves: {SAVE_SCRIPT.relative_to(ROOT)} saves to File 1, {LOAD_SCRIPT.relative_to(ROOT)} loads it")

    failures = []
    t0 = time.time()
    port_card, emu_card = out / "port.mcd", out / "emulator.mcd"
    saved = {}
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = {}
        if given["port"] is not None:
            port_card.write_bytes(given["port"])
        else:
            jobs["port"] = pool.submit(run_port, binary, SAVE_SCRIPT, port_card, out / "port_save")
        if given["emulator"] is not None:
            emu_card.write_bytes(given["emulator"])
        else:
            jobs["emulator"] = pool.submit(run_emulator, SAVE_SCRIPT, emu_card, out / "emulator_save")
        for who, job in jobs.items():
            try:
                _, images, frames = job.result()
            except (RuntimeError, subprocess.TimeoutExpired) as e:
                failures.append(f"{who} saves: {e}")
                continue
            saved[who] = images.get(SAVED)
            got = stable_sha1(saved[who]) if saved[who] is not None else "missing"
            ok = got == expected
            print(f"  {who + ' saves':<32} {SAVED:<7} {got[:12]} {'ok' if ok else 'DIFFERS'} ({frames} frames)")
            if not ok:
                failures.append(f"{who} saves: checkpoint {SAVED}: stable hash {got}, expected {expected} "
                                f"({SAVE_RECORD.relative_to(ROOT)})")
        if failures:
            return report(failures, out, args.keep, t0)

        print("  the cards (tests/saves/cards.py):")
        failures += cards.check_pair(port_card.read_bytes(), emu_card.read_bytes(), "port.mcd", "emulator.mcd",
                                     indent="    ")
        # Both sides are deterministic, so two runs print the same SHA-1s.
        print(f"    SHA-1: port.mcd {replay.sha1_file(port_card)}, emulator.mcd {replay.sha1_file(emu_card)}")

        # Each side loads a copy of the other's card (a load may write to the card: the cards stay as saved).
        shutil.copyfile(port_card, out / "port_for_emulator.mcd")
        shutil.copyfile(emu_card, out / "emulator_for_port.mcd")
        loads = {("emulator", "port"): pool.submit(run_emulator, LOAD_SCRIPT, out / "port_for_emulator.mcd",
                                                   out / "emulator_load"),
                 ("port", "emulator"): pool.submit(run_port, binary, LOAD_SCRIPT, out / "emulator_for_port.mcd",
                                                   out / "port_load")}
        views = {}
        for (who, whose), job in loads.items():
            label = f"{who} loads the {whose}'s card"
            try:
                views[who], images, frames = job.result()
            except (RuntimeError, subprocess.TimeoutExpired) as e:
                failures.append(f"{label}: {e}")
                continue
            got = images.get(LOADED)
            if got is None:
                failures.append(f"{label}: no image at checkpoint {LOADED}")
                continue
            ok_hash = stable_sha1(got) == expected
            want = saved.get(whose)
            if want is None:   # a given card: no image of the save to compare with
                same, note = True, "(no save image: a given card)"
            else:
                a, b = bytearray(got), bytearray(want)
                a[PLAY_TIME[0]:PLAY_TIME[1]] = b[PLAY_TIME[0]:PLAY_TIME[1]] = bytes(PLAY_TIME[1] - PLAY_TIME[0])
                same = a == b
                note = f"the {whose}'s saved profile but the play time: " + ("ok" if same else describe_diff(a, b))
            print(f"  {label:<32} {LOADED:<7} {stable_sha1(got)[:12]} {'ok' if ok_hash else 'DIFFERS'}; {note} "
                  f"({frames} frames)")
            if not ok_hash:
                failures.append(f"{label}: checkpoint {LOADED}: stable hash {stable_sha1(got)}, expected new_game's "
                                f"{SAVED} {expected}")
            if not same:
                failures.append(f"{label}: checkpoint {LOADED}: the profile is not the one saved: {note}")
        if len(views) == 2:
            if views["port"] == views["emulator"]:
                print("  the two loads' cross-core views (stages, maps, overlay and map sequences): equal")
            else:
                failures.append(f"the two loads' cross-core views differ: port {json.dumps(views['port'])[:300]}; "
                                f"emulator {json.dumps(views['emulator'])[:300]}")
    return report(failures, out, args.keep, t0)


def report(failures, out, keep, t0):
    elapsed = time.time() - t0
    if failures:
        print("saves: FAIL\n  " + "\n  ".join(failures) + f"\n  outputs: {out}")
        return 1
    if not keep:
        for d in out.iterdir():
            if d.is_dir():
                shutil.rmtree(d)
    print(f"saves: pass (port -> emulator, emulator -> port, the cards; {elapsed:.0f} s; cards in {out})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
