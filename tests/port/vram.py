#!/usr/bin/env python3
"""The port's VRAM and pictures against the emulator's at the replay scripts' checkpoints (docs/PORT.md "Testing";
M2). Adapted from dw2003recomp's tests/port/vram.py (docs/THIRD_PARTY.md).

Usage: tests/port/vram.py [SCRIPT ...] [--out DIR] [-j N] [--no-png] [--keep]

For each script (default: every tests/replay/scripts/<name>.json with a record), a variant is run in the emulator and
in the port: after every `checkpoint` step a `vram` step of the same name (the whole VRAM, 1024x512x16 bits, on the
checkpoint's frame), and at the end of `boot` and `new_game` a few more dumps (EXTRAS: the title's PRESS START and
SAISEG's first area, some frames on). A `vram` step takes its frame, so the steps after a checkpoint start one frame
later than in the committed script; both sides run the same variant, and every dump is keyed by its name, never by a
frame number (the port's frames are not the emulator's).
- The emulator (tests/replay/replay.py's run_once, the interpreter core) runs it with tests/port/vram.lua before
  psxstack's run.lua: each dump also writes the displayed picture (PCSX.GPU.takeScreenShot) and the last 120 vsyncs of
  the game's frame-buffer index and vblanksPerFrame.
- The port (build/port/dcb, DCB_PORT_CHECKPOINT_DIR) runs it twice: once for the record (the frame of each dump: every
  dump follows a checkpoint of its name), once more with `--screenshot FRAME:PATH` at those frames (the displayed
  picture, a PPM); the two records must be identical.

Per dump, three comparisons:
- **vram**: the whole VRAM equal. It can be only where the emulator's game was not CPU-bound ("steady": the
  frame-buffer index flipped on each of the last 60 vsyncs and vblanksPerFrame stayed 1; elsewhere the PS1 and the
  port pass through different frames: dw2003recomp's DECISIONS "The port is checked against the emulator") and where
  both reached the checkpoint after the same game frames (the animations' phase follows the frame count).
- **textures**: the VRAM outside the display buffers (the game's buffers start at x 0, both halves: x below the
  displayed width in VRAM pixels, 1.5 times the screen width for a 24-bit display) equal: the textures and CLUTs the
  game uploaded, and whatever it draws off screen. Expected everywhere.
- **frame**: the displayed picture equal (the emulator's 15-bit pixels widened as the port's video.c does). Under the
  same conditions as vram.
Every check is a gate: a difference fails unless tests/port/vram_known.json lists it (key "<checkpoint>":
{"<check>": "why, with the issue"}; a checkpoint's name is the same moment in every script, since the scripts are
prefixes of each other), so what matches now stays matched. A known entry that matches now is reported as stale. A
difference is reported with its pixel count and bounding rectangle [x0, y0, x1, y1] (inclusive; up to 16 pixels
also listed with their values, emulator then port), and, unless --no-png, written as PNGs (emulator | port | the
differing pixels in white) in DIR/<script>/: the VRAM 1024x512 (colours only, the mask bit not shown), the picture
at its size.

Writes DIR/report.json and prints the table. Nothing here is game data in the tree: the dumps stay in DIR
(build/port-vram by default). Exit codes: 0 pass, 1 fail, 2 something missing.
"""
import argparse
import array
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/port"))
sys.path.insert(0, str(ROOT / "tests/replay"))
import run as port_run  # noqa: E402  (configures psxstack's port_test for this game)
import port_test  # noqa: E402
import replay  # noqa: E402  (the emulator's runner, on the interpreter core)

OUT_DEFAULT = ROOT / "build/port-vram"
KNOWN = ROOT / "tests/port/vram_known.json"
PRELUDE = ROOT / "tests/port/vram.lua"
W, H = 1024, 512
CHECKS = ("vram", "textures", "frame")
# Dumps after the script's last checkpoint, in frames from it (a checkpoint "<last>+N" and its dump): the title's
# PRESS START blinking, and SAISEG's first area once its screen is up.
EXTRAS = {"boot": (120,), "new_game": (120, 600)}
STEADY_VSYNCS = 60


def vram_script(name, out):
    """The script with a vram step after each checkpoint, and the extras; returns (path, script, dump names)."""
    script = replay.load_script(port_run.SCRIPTS / f"{name}.json")
    steps, dumps = [], []
    for step in script["steps"]:
        steps.append(step)
        if step.get("type") == "checkpoint":
            steps.append({"type": "vram", "name": step["name"]})
            dumps.append(step["name"])
    last, prev = dumps[-1], 0
    for n in EXTRAS.get(name, ()):
        # the vram step ends its frame; wait_frames (n - prev - 1) then puts the checkpoint n frames after the last
        steps += [{"type": "wait_frames", "frames": n - prev - 1},
                  {"type": "checkpoint", "name": f"{last}+{n}", "image": False},
                  {"type": "vram", "name": f"{last}+{n}"}]
        dumps.append(f"{last}+{n}")
        prev = n
    # the port exits on the frame its script completes, before that frame's screenshots: one more frame
    steps.append({"type": "wait_frames", "frames": 2})
    script["name"] = f"{name}@vram"
    script["steps"] = steps
    script["max_frames"] = script.get("max_frames", 20000) + len(dumps) + sum(EXTRAS.get(name, ()))
    out.mkdir(parents=True, exist_ok=True)
    path = out / f"{name}@vram.json"
    path.write_text(json.dumps(script, indent=1) + "\n")
    return path, script, dumps


def run_emulator(path, script, out):
    wrapper = out / "vram_wrapper.lua"
    out.mkdir(parents=True, exist_ok=True)
    wrapper.write_text(f"dofile({json.dumps(str(PRELUDE))})\ndofile({json.dumps(str(replay.RUN_LUA))})\n")
    return replay.run_once(path, script, replay.bios_path("openbios"), out, lua=wrapper)


def run_port(binary, path, out, shots=None):
    """One port run; returns its record. `shots`: {frame: ppm path}."""
    out.mkdir(parents=True, exist_ok=True)
    rec = out / "record.json"
    cmd = [str(binary), "--disc", str(port_run.DISC), "--script", str(path), "--log", str(out / "frames.log"),
           "--record", str(rec)]
    for frame, ppm in sorted((shots or {}).items()):
        cmd += ["--screenshot", f"{frame}:{ppm}"]
    env = dict(os.environ, DCB_PORT_CHECKPOINT_DIR=str(out))
    with open(out / "stderr.txt", "w") as f:
        proc = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=900)
    if proc.returncode != 0 or not rec.exists():
        tail = "\n    ".join((out / "stderr.txt").read_text(errors="replace").splitlines()[-8:])
        raise RuntimeError(f"the port exited {proc.returncode} ({out / 'stderr.txt'}):\n    {tail}")
    return json.loads(rec.read_text())


def load_vram(path):
    data = array.array("H")
    data.frombytes(path.read_bytes())
    if len(data) != W * H:
        raise RuntimeError(f"{path}: {len(data) * 2} bytes, not one VRAM image")
    if sys.byteorder != "little":
        data.byteswap()
    return data


def rgb15(p):
    r, g, b = p & 31, (p >> 5) & 31, (p >> 10) & 31
    return bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))


RGB15 = [rgb15(p) for p in range(0x8000)]


def emu_screen(out, name):
    """The emulator's displayed picture as (w, h, RGB bytes), converted as the port's video.c does."""
    w, h, bpp = (int(v) for v in (out / f"screen_{name}.txt").read_text().split())
    raw = (out / f"screen_{name}.bin").read_bytes()
    if bpp == 24:
        return w, h, raw[:w * h * 3], bpp
    px = array.array("H")
    px.frombytes(raw[:w * h * 2])
    return w, h, b"".join(RGB15[p & 0x7FFF] for p in px), bpp


def read_ppm(path):
    data = path.read_bytes()
    parts = data.split(maxsplit=4)
    if parts[0] != b"P6" or parts[3] != b"255":
        raise RuntimeError(f"{path}: not a binary PPM")
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][:w * h * 3]


def steadiness(out, name):
    """(steady, summary) from vsyncs_<name>.txt: the game's frames over the last STEADY_VSYNCS vsyncs."""
    lines = (out / f"vsyncs_{name}.txt").read_text().split("\n")
    index = [int(v) for v in lines[0].split()][-STEADY_VSYNCS - 1:]
    vblanks = [int(v) for v in lines[1].split()][-STEADY_VSYNCS:]
    flips = sum(1 for a, b in zip(index, index[1:]) if a != b)
    steady = flips == len(index) - 1 and set(vblanks) == {1}
    return steady, f"{flips}/{len(index) - 1} flips, vbl {min(vblanks)}..{max(vblanks)}"


def diff_rect(a, b, width, height, x_from=0, x_to=None, step=1):
    """Pixels (each `step` items) that differ between two row-major images: (count, [x0, y0, x1, y1]) or (0, None)."""
    x_to = width if x_to is None else x_to
    count, x0, y0, x1, y1 = 0, width, height, -1, -1
    rw = width * step
    for y in range(height):
        ra, rb = a[y * rw + x_from * step:y * rw + x_to * step], b[y * rw + x_from * step:y * rw + x_to * step]
        if ra == rb:
            continue
        for x in range(x_to - x_from):
            if ra[x * step:(x + 1) * step] != rb[x * step:(x + 1) * step]:
                count += 1
                x0, x1 = min(x0, x + x_from), max(x1, x + x_from)
                y0, y1 = min(y0, y), max(y1, y)
    return (count, [x0, y0, x1, y1]) if count else (0, None)


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def side_by_side(path, w, h, left, right):
    """emulator | port | the pixels whose colours differ (white), each w x h; `left`/`right` RGB bytes."""
    rows = []
    for y in range(h):
        a, b = left[y * w * 3:(y + 1) * w * 3], right[y * w * 3:(y + 1) * w * 3]
        if a == b:
            mask = bytes(w * 3)
        else:
            mask = b"".join(b"\xff\xff\xff" if a[x * 3:x * 3 + 3] != b[x * 3:x * 3 + 3] else b"\0\0\0"
                            for x in range(w))
        rows.append(a + b + mask)
    write_png(path, w * 3, h, b"".join(rows))


def vram_rgb(v):
    return b"".join(RGB15[p & 0x7FFF] for p in v)


def compare_dump(name, emu_dir, port_dir, shot, png_dir):
    """The three comparisons of one dump: {check: None (equal) | {count, rect}}, plus the context."""
    ev, pv = load_vram(emu_dir / f"vram_{name}.bin"), load_vram(port_dir / f"vram_{name}.bin")
    steady, cadence = steadiness(emu_dir, name)
    sw, sh, srgb, bpp = emu_screen(emu_dir, name)
    disp_cols = min(W, sw * 3 // 2 if bpp == 24 else sw)
    res = {"steady": steady, "cadence": cadence, "display": [sw, sh, bpp], "display_vram_columns": disp_cols}
    n, rect = diff_rect(ev, pv, W, H)
    res["vram"] = {"count": n, "rect": rect} if n else None
    if 0 < n <= 16:   # a few pixels: which, and their values (emulator, port)
        res["vram"]["pixels"] = [[i % W, i // W, f"{ev[i]:04x}", f"{pv[i]:04x}"] for i in range(W * H) if ev[i] != pv[i]]
    n, rect = diff_rect(ev, pv, W, H, x_from=disp_cols)
    res["textures"] = {"count": n, "rect": rect} if n else None
    pw, ph, prgb = read_ppm(shot)
    if (pw, ph) != (sw, sh):
        res["frame"] = {"count": sw * sh, "rect": None, "size": [[sw, sh], [pw, ph]]}
    else:
        n, rect = diff_rect(srgb, prgb, sw, sh, step=3)
        res["frame"] = {"count": n, "rect": rect} if n else None
    if png_dir is not None:
        safe = name.replace("+", "_plus")
        if res["vram"]:
            side_by_side(png_dir / f"{safe}_vram.png", W, H, vram_rgb(ev), vram_rgb(pv))
        if res["frame"] and (pw, ph) == (sw, sh):
            side_by_side(png_dir / f"{safe}_frame.png", sw, sh, srgb, prgb)
    return res


def check_script(name, binary, out, png, keep=False):
    """Runs one script's variant on both sides and compares every dump; returns (rows, messages)."""
    sdir = out / name
    path, script, dumps = vram_script(name, sdir)
    emu = run_emulator(path, script, sdir / "emu")
    rec1 = run_port(binary, path, sdir / "port")
    frames = {cp["name"]: cp["frame"] for cp in rec1["checkpoints"]}
    emu_frames = {cp["name"]: cp["frame"] for cp in emu["checkpoints"]}
    missing = [d for d in dumps if d not in frames or d not in emu_frames]
    if missing:
        raise RuntimeError(f"{name}: no checkpoint for the dumps {missing}")
    shots = {frames[d]: sdir / "port" / f"screen_{d}.ppm" for d in dumps}
    rec2 = run_port(binary, path, sdir / "port", shots)
    msgs = []
    if json.dumps(rec1, sort_keys=True) != json.dumps(rec2, sort_keys=True):
        msgs.append(f"{name}: the port's two runs gave different records")
    for cp_e, cp_p in zip(emu["checkpoints"], rec1["checkpoints"]):
        if (cp_e["name"], cp_e["stage"], cp_e["map"]) != (cp_p["name"], cp_p["stage"], cp_p["map"]):
            msgs.append(f"{name}: checkpoint {cp_e['name']}: the emulator's stage {cp_e['stage']} map {cp_e['map']}, "
                        f"the port's {cp_p['name']} stage {cp_p['stage']} map {cp_p['map']}")
    rows = []
    for d in dumps:
        res = compare_dump(d, sdir / "emu", sdir / "port", shots[frames[d]], sdir if png else None)
        res.update(script=name, dump=d, emu_frame=emu_frames[d], port_frame=frames[d])
        rows.append(res)
    for side in ("emu", "port") if not keep else ():   # the PNGs and the report say what differed
        for f in (sdir / side).glob("vram_*.bin"):
            f.unlink()
    return rows, msgs


def fmt(r):
    return "equal" if r is None else (f"{r['count']} px in {r['rect']}" if r.get("rect") else
                                      f"size {r['size'][0]} vs {r['size'][1]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scripts", nargs="*")
    ap.add_argument("--out", default=str(OUT_DEFAULT))
    ap.add_argument("-j", "--jobs", type=int, default=2, help="scripts run in parallel (default 2)")
    ap.add_argument("--no-png", action="store_true", help="no side-by-side PNGs of the differences")
    ap.add_argument("--keep", action="store_true", help="keep the VRAM dumps (1 MB each) in DIR/<script>/{emu,port}")
    args = ap.parse_args()
    names = args.scripts or sorted(p.stem for p in port_run.SCRIPTS.glob("*.json")
                                   if (port_run.EXPECTED / p.name).exists())
    out = Path(args.out).resolve()
    try:
        if not port_run.DISC.exists():
            raise port_test.Missing(f"no disc image ({port_run.DISC.relative_to(ROOT)}: scripts/setup.sh disc)")
        if not replay.REDUX.exists():
            raise port_test.Missing("no emulator (scripts/setup.sh redux)")
        env = port_test.tool_env()
        binary = port_test.build("build/port", [], os.cpu_count() or 1, env)
    except port_test.Missing as e:
        print(f"vram: {e}")
        return 2
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    known = json.loads(KNOWN.read_text()) if KNOWN.exists() else {}
    known = {k: v for k, v in known.items() if not k.startswith("_")}
    print(f"vram: {', '.join(names)} (emulator and port, {args.jobs} at a time; {out})")
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {n: pool.submit(check_script, n, binary, out, not args.no_png, args.keep) for n in names}
    rows, failures, notes = [], [], []
    for n in names:
        try:
            r, msgs = futures[n].result()
        except Exception as e:  # noqa: BLE001  (a run that fails is a test failure, reported per script)
            failures.append(f"{n}: {e}")
            continue
        rows += r
        failures += msgs
    hdr = f"{'script':<11} {'dump':<15} {'emu':>6} {'port':>6}  {'emulator cadence':<31} {'vram':<38} " \
          f"{'textures':<38} frame"
    print(hdr)
    for r in rows:
        k = known.get(r["dump"], {})
        cells = []
        for c in CHECKS:
            text = fmt(r[c])
            if r[c] is not None:
                if c in k:
                    text += " (known)"
                else:
                    text += " FAIL"
                    failures.append(f"{r['script']}/{r['dump']}: {c} differs: {fmt(r[c])}")
            elif c in k:
                notes.append(f"{r['script']}/{r['dump']}: {c} is equal now: vram_known.json's entry is stale")
            cells.append(text)
        cad = ("steady " if r["steady"] else "not steady ") + r["cadence"]
        print(f"{r['script']:<11} {r['dump']:<15} {r['emu_frame']:>6} {r['port_frame']:>6}  {cad:<31} "
              f"{cells[0]:<38} {cells[1]:<38} {cells[2]}")
    (out / "report.json").write_text(json.dumps(rows, indent=1) + "\n")
    for m in notes:
        print(f"  note: {m}")
    if failures:
        for m in failures:
            print(f"  FAIL {m}")
        print(f"vram: FAIL ({out / 'report.json'})")
        return 1
    print("vram: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
