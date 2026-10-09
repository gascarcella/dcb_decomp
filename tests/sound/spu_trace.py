#!/usr/bin/env python3
"""SPU register-write traces of the original game: the oracle for the port's LIBSND and SPU (docs/PORT.md "Sound").
Adapted from dw2003recomp's tests/sound/spu_trace.py (docs/THIRD_PARTY.md).

Runs a replay script (tests/replay/scripts/*.json) in PCSX-Redux under -debugger -interpreter with
tests/sound/spu_trace.lua loaded before psxstack's run.lua: a write breakpoint over the SPU's registers
(0x1F801C00..0x1F801DFF) and DMA4's (0x1F8010C0..CF) records every CPU store with its vsync tick, in order; a DMA4 start
from RAM records the block's SPU address, length and SHA-1. Exec breakpoints on LIBSND's and LIBSPU's public functions
log the game's calls (its own, not the library's calls of itself) and LIBSND's sequencer tick (SsSeqCalledTbyT, from
the vblank interrupt once SsStart ran).

Usage:
  tests/sound/spu_trace.py run <script.json> [--until CHECKPOINT] [--extra-frames N] [--repeat N] [--detail]
                               [--out DIR] [--bios openbios|retail|FILE] [--prelude LUA]
  tests/sound/spu_trace.py record [SCRIPT ...] [-j N]   # the committed trace(s), tests/sound/traces/<script>.trace.gz
  tests/sound/spu_trace.py diff A.trace B.trace [--align MARKER] [--no-ticks] [--max N]

`run` writes <out>/run<i>/spu.trace (and keeps the emulator log and the replay result there); with --repeat N the N
traces must be identical (determinism). --until cuts the script after its checkpoint of that name, --extra-frames
appends a wait; --detail adds the storing pc and the DMA registers as comments; calls.txt has the call counts of every
LIBSND/LIBSPU function, internals included. `record` traces the scripts (default: COMMITTED), twice each, and writes
the gzipped trace when the two agree (tests/port/sound.py replays them). Only first_duel_play's is committed: the five
scripts are prefixes of each other, and so are their traces (verified when they were recorded: boot's, title's,
new_game's and first_duel's events, calls and ticks are first_duel_play's up to their ends).

Trace format (text, one event per line; '#' starts a comment, which comparisons ignore):
  <tick> <reg> <name> <value>                 an SPU register store: reg = address - 0x1F801000 (hex), name as
                                              psxstack's runtime/spu_trace.c names it ("v05.pitch", "kon.lo", ...),
                                              value hex; a width other than 16 bits is a suffix ("/8", "/32")
  <tick> dma4 spu=<addr> len=<bytes> sha1=<hex>   a DMA4 block from RAM into SPU RAM at <addr> (the transfer address the
                                              game set in 0x1F801DA6, x8, advanced by earlier blocks and FIFO writes)
  # <tick> call Name(args) ra=<hex> [data=<len>:<sha1>] [mem=<hex>]   a call of the game into LIBSND/LIBSPU: its
                                              arguments (hex; as many as the function takes, ARGS), where a0 points
                                              at a PAK chunk the chunk's length and SHA-1 (the VAB header and body,
                                              the SEQ), at a struct its bytes (SpuSetVoiceAttr, SpuSetCommonAttr)
  # <tick> tick cycles=<n>                    LIBSND's sequencer tick (SsSeqCalledTbyT: the flush, then the sequences)
                                              and the CPU's cycle count at it (768 cycles a SPU sample: the replay
                                              renders the samples between two ticks from it)
  # mark <frame> <kind> ...                   markers: exe_start (the game's entry point), reset, and from the replay
                                              result the checkpoints, overlay and map changes
<tick> is the vsync count since boot (the replay runner's frame).

Exit codes: 0 pass, 1 mismatch or emulator failure, 2 usage / missing tool.
"""
import argparse
import gzip
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/replay"))
import replay  # noqa: E402  (this game's configuration of psxstack's runner: run_once on the interpreter core)

TRACE_LUA = ROOT / "tests/sound/spu_trace.lua"
SYMBOLS = ROOT / "config/us/symbols.txt"
TRACES = ROOT / "tests/sound/traces"   # (upstream's .gitignore ignores `expected/`)
COMMITTED = ("first_duel_play",)       # the longest script: the others are its prefixes
EMU_ARGS = ("-debugger", "-interpreter")   # memory and exec breakpoints need both
FORMAT = "dcb spu trace v1"

# LIBSPU and LIBSND in SLUS_013.28: SpuClearReverbWorkArea (the first object, libspu_s_crwa) up to __SN_ENTRY_POINT
# (config/us/symbols.txt; config/us/psyq_objects.txt lists the objects in this order). A call whose ra is inside it
# comes from the library itself.
LIB_START, LIB_END = 0x8004A800, 0x80056270
# What a0 points at, for the calls whose data the replay needs: a PAK chunk (findPakChunk's result: the VAB header and
# body, the SEQ) or a struct of that many bytes (SpuVoiceAttr 0x40, SpuCommonAttr 0x28; psyq/libspu.h).
DUMPS = {"SsVabOpenHeadSticky": "chunk", "SsVabTransBody": "chunk", "SsSeqOpen": "chunk", "SsSepOpen": "chunk",
         "SpuSetVoiceAttr": 0x40, "SpuSetCommonAttr": 0x28, "SsSeqCalledTbyT": "cycles"}
# The arguments each function takes (Psy-Q's libsnd.h, libspu.h); the rest show 4 (a0..a3).
ARGS = {"SsInit": 0, "SsStart": 0, "SsStart2": 0, "SsSetStereo": 0, "SsSetMono": 0, "SsUtReverbOn": 0,
        "SsUtReverbOff": 0, "SsSeqCalledTbyT": 0, "SsSetTickMode": 1, "SsVabClose": 1, "SsVabTransCompleted": 1,
        "SsSeqClose": 1, "SsSeqStop": 1, "SsUtKeyOffV": 1, "SsUtAllKeyOff": 1, "SsUtSetReverbType": 1,
        "SpuClearReverbWorkArea": 1, "SpuSetVoiceAttr": 1, "SpuSetCommonAttr": 1, "SsSetMVol": 2, "SsSeqOpen": 2,
        "SsVabTransBody": 2, "SsUtSetReverbDepth": 2, "SsSeqPlay": 3, "SsSeqSetVol": 3, "SsSetTableSize": 3,
        "SsSetSerialAttr": 3, "SsSetSerialVol": 3, "SsVabOpenHeadSticky": 3, "SsSepOpen": 3, "SsSeqGetVol": 4,
        "SsUtKeyOnV": 8, "SsUtKeyOn": 7, "SsUtKeyOff": 5}
# Functions outside LIBSND/LIBSPU that store to the SPU, logged too: LIBCD's CdInit (its CD_initvol).
OTHER_LOGGED = ("CdInit",)

# SPU register names (psx-spx "SPU"): 24 voices of 8 registers, then the control block, then the reverb set.
VOICE_REGS = ("vol.l", "vol.r", "pitch", "addr", "adsr.lo", "adsr.hi", "adsr.vol", "loop")
CONTROL_REGS = {
    0xD80: "mvol.l", 0xD82: "mvol.r", 0xD84: "rvol.l", 0xD86: "rvol.r",
    0xD88: "kon.lo", 0xD8A: "kon.hi", 0xD8C: "koff.lo", 0xD8E: "koff.hi",
    0xD90: "pmon.lo", 0xD92: "pmon.hi", 0xD94: "non.lo", 0xD96: "non.hi",
    0xD98: "eon.lo", 0xD9A: "eon.hi", 0xD9C: "endx.lo", 0xD9E: "endx.hi",
    0xDA0: "unk_da0", 0xDA2: "rev.base", 0xDA4: "irq.addr", 0xDA6: "xfer.addr",
    0xDA8: "xfer.fifo", 0xDAA: "spucnt", 0xDAC: "xfer.ctrl", 0xDAE: "spustat",
    0xDB0: "cdvol.l", 0xDB2: "cdvol.r", 0xDB4: "extvol.l", 0xDB6: "extvol.r",
    0xDB8: "curvol.l", 0xDBA: "curvol.r", 0xDBC: "unk_dbc", 0xDBE: "unk_dbe",
}
REVERB_REGS = ("dAPF1", "dAPF2", "vIIR", "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4", "vWALL",
               "vAPF1", "vAPF2", "mLSAME", "mRSAME", "mLCOMB1", "mRCOMB1", "mLCOMB2", "mRCOMB2",
               "dLSAME", "dRSAME", "mLDIFF", "mRDIFF", "mLCOMB3", "mRCOMB3", "mLCOMB4", "mRCOMB4",
               "dLDIFF", "dRDIFF", "mLAPF1", "mRAPF1", "mLAPF2", "mRAPF2", "vLIN", "vRIN")
DMA4_REGS = {0x0C0: "dma4.madr", 0x0C4: "dma4.bcr", 0x0C8: "dma4.chcr", 0x0CC: "dma4.unk"}


def reg_name(off):
    """The name of the SPU or DMA4 register at 0x1F801000 + off (off rounded down to a halfword)."""
    off &= ~1
    if 0xC00 <= off < 0xD80:
        voice, r = divmod(off - 0xC00, 0x10)
        return f"v{voice:02d}.{VOICE_REGS[r // 2]}"
    if off in CONTROL_REGS:
        return CONTROL_REGS[off]
    if 0xDC0 <= off < 0xE00:
        return "rev." + REVERB_REGS[(off - 0xDC0) // 2]
    if (off & ~3) in DMA4_REGS:
        return DMA4_REGS[off & ~3] + (".hi" if off & 2 else "")
    return f"unk_{off:03x}"


def functions():
    """{name: addr} of config/us/symbols.txt's functions."""
    out = {}
    for line in SYMBOLS.read_text().splitlines():
        m = re.match(r"(\w+)\s*=\s*0x([0-9A-Fa-f]+);\s*//\s*type:func", line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def write_spec(path):
    """The Lua spec for spu_trace.lua: LIBSND's and LIBSPU's public functions (and OTHER_LOGGED) are logged, the
    others in the library counted."""
    logged, counted = [], []
    for name, addr in sorted(functions().items(), key=lambda x: x[1]):
        inside = LIB_START <= addr < LIB_END
        if (inside and not name.startswith("_")) or name in OTHER_LOGGED:
            dump = DUMPS.get(name)
            d = "" if dump is None else f", dump = {json.dumps(dump) if isinstance(dump, str) else dump}"
            logged.append(f"{{ addr = 0x{addr:08X}, name = '{name}'{d} }}")
        elif inside:
            counted.append(f"{{ addr = 0x{addr:08X}, name = '{name}' }}")
    path.write_text("return { calls = { " + ",\n  ".join(logged) + " },\n         counts = { " + ",\n  ".join(counted)
                    + " } }\n")


def cut_script(script, until, extra_frames):
    """The script up to (and including) the checkpoint named `until`, plus a wait of `extra_frames`."""
    script = dict(script)
    steps = list(script["steps"])
    if until:
        idx = [i for i, s in enumerate(steps) if s.get("type") == "checkpoint" and s.get("name") == until]
        if not idx:
            raise SystemExit(f"spu_trace: the script has no checkpoint named {until!r}")
        steps = steps[:idx[0] + 1]
        script["name"] = f"{script['name']}@{until}"
    if extra_frames:
        steps.append({"type": "wait_frames", "frames": extra_frames})
        script["name"] = f"{script['name']}+{extra_frames}"
    script["steps"] = steps
    return script


def call_text(name, regs):
    """`Name(a0, a1, ...)` with as many arguments as the function takes (hex, 0 as 0)."""
    n = ARGS.get(name, 4)
    return f"{name}(" + ", ".join("0x" + a.lstrip("0") if a.strip("0") else "0" for a in regs[:n]) + ")"


def build_trace(run_dir, header, result, detail):
    """Turns spu_trace.lua's raw.txt + data.bin and the replay's result.json into the trace text (and deletes
    data.bin)."""
    raw = (run_dir / "raw.txt").read_text().splitlines()
    stats = dict(kv.split("=") for kv in raw[0].split()[1:])
    data_path = run_dir / "data.bin"
    data = data_path.read_bytes()
    marks = []
    for cp in result.get("checkpoints", []):
        marks.append((cp["frame"], f"# mark {cp['frame']} checkpoint {cp['name']} stage={cp['stage']} map=0x{cp['map']:X}"))
    for o in result.get("overlay_sequence", []):
        marks.append((o["frame"], f"# mark {o['frame']} overlay stage={o['stage']} file={o['file']}"))
    for m in result.get("map_sequence", []):
        marks.append((m["frame"], f"# mark {m['frame']} map 0x{m['map']:X}"))
    marks.sort(key=lambda x: x[0])
    out = [f"# {FORMAT}"] + [f"# {h}" for h in header]
    out.append(f"# ticks {stats['ticks']}, writes {stats['writes']}, dma blocks {stats['dma']}, unknown values"
               f" {stats['unknown_values']}; BIOS writes before the EXE (not traced) {stats['boot_writes']}")
    xfer = 0           # the SPU transfer address (bytes), as the game set it
    mi = 0
    for line in raw[1:]:
        if not line:
            continue
        f = line.split()
        tick = int(f[1])
        while mi < len(marks) and marks[mi][0] <= tick:
            out.append(marks[mi][1])
            mi += 1
        if f[0] == "w":
            addr, width, value, pc = int(f[2], 16), int(f[3]), f[4], f[5]
            off = addr & 0xFFF
            name = reg_name(off)
            if width == 1 and off & 1:
                name += ".b1"
            elif width == 1:
                name += "/8"
            elif width == 4:
                name += "/32"
            if 0xC0 <= off < 0xD0:
                if detail:
                    out.append(f"# {tick} {name} {value} pc={pc}")
                continue
            vtext = value if value == "?" else f"{int(value, 16):04x}" if width == 2 else value
            out.append(f"{tick} {off:03x} {name} {vtext}" + (f"  # pc={pc}" if detail else ""))
            if value != "?":
                v = int(value, 16)
                if off == 0xDA6:
                    xfer = (v & 0xFFFF) * 8
                elif off == 0xDA8:
                    xfer = (xfer + width) & 0x7FFFF
        elif f[0] == "d":
            madr, bcr, chcr, offset, length = f[2], f[3], f[4], int(f[5]), int(f[6])
            sha1 = hashlib.sha1(data[offset:offset + length]).hexdigest()
            out.append(f"{tick} dma4 spu={xfer:05x} len={length} sha1={sha1}"
                       + (f"  # madr={madr} bcr={bcr} chcr={chcr}" if detail else ""))
            xfer = (xfer + length) & 0x7FFFF
        elif f[0] == "m":
            out.append(f"# mark {tick} {f[2]}")
        elif f[0] == "c":
            name, regs, ra, extra = f[2], f[3:11], f[11], f[12:]
            if name == "SsSeqCalledTbyT":
                out.append(f"# {tick} tick" + "".join(" " + e for e in extra))
                continue
            internal = LIB_START <= int(ra, 16) < LIB_END
            if internal and not detail:
                continue    # LIBSND calling its own public functions (SsPitchFromNote, SpuSetKey, ...)
            text = f"# {tick} call {call_text(name, regs)} ra={ra}" + (" (internal)" if internal else "")
            for e in extra:
                if e.startswith("chunk="):
                    at, n = (int(x) for x in e.split("=")[1].split(":"))
                    text += f" data={n}:{hashlib.sha1(data[at:at + n]).hexdigest()}"
                else:
                    text += " " + e
            out.append(text)
    for _, text in marks[mi:]:
        out.append(text)
    data_path.unlink()
    return "\n".join(out) + "\n", stats


def run_trace(script_path, script, out_dir, bios, detail=False, prelude=None):
    """One emulator run with the tracer; returns (trace text, stats, seconds)."""
    out_dir = Path(out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    spec = out_dir / "spu_trace_spec.lua"
    write_spec(spec)
    wrapper = out_dir / "spu_trace_wrapper.lua"
    wrapper.write_text((f"dofile({json.dumps(str(Path(prelude).resolve()))})\n" if prelude else "") +
                       f"dofile({json.dumps(str(TRACE_LUA))})\ndofile({json.dumps(str(replay.RUN_LUA))})\n")
    env_before = {k: os.environ.get(k) for k in ("DCB_SPU_TRACE_OUT", "DCB_SPU_TRACE_SPEC")}
    t0 = time.time()
    try:
        # run_once reads os.environ for the emulator's environment (one run at a time per process: see cmd_record)
        os.environ["DCB_SPU_TRACE_OUT"] = str(out_dir)
        os.environ["DCB_SPU_TRACE_SPEC"] = str(spec)
        replay.run_once(script_path, script, bios, out_dir, lua=wrapper, emu_args=EMU_ARGS)
    finally:
        for k, v in env_before.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    elapsed = time.time() - t0
    result = json.loads((out_dir / "result.json").read_text())
    emu = replay.emulator_info()
    header = [f"script {script['name']} ({Path(script_path).name}, sha1 {replay.sha1_file(script_path)[:12]}),"
              f" {result['frames']} frames",
              f"emulator {emu['name']} {emu['version']} build {emu['build_id']} ({emu['changeset'][:8]}), core interpreter,"
              f" bios {bios.name} ({replay.sha1_file(bios)[:12]})"]
    text, stats = build_trace(out_dir, header, result, detail)
    (out_dir / "calls.txt").write_text((out_dir / "counts.txt").read_text())
    (out_dir / "spu.trace").write_text(text)
    return text, stats, elapsed


def events(text, align=None, ticks=True):
    """The comparable lines of a trace: no comments, ticks rebased to the marker `align` (a substring of a '# mark'
    line, e.g. 'checkpoint title'; events before it are dropped) or dropped with ticks=False."""
    base = 0
    lines = text.splitlines()
    if align:
        start = None
        for i, line in enumerate(lines):
            if line.startswith("# mark ") and align in line:
                start = i
                break
        if start is None:
            raise SystemExit(f"spu_trace: no marker matching {align!r}")
        base = int(lines[start].split()[2])
        lines = lines[start:]
    out = []
    for line in lines:
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tick, rest = line.split(" ", 1)
        out.append(f"{int(tick) - base} {rest}" if ticks else rest)
    return out


def read_trace(path):
    path = Path(path)
    return gzip.decompress(path.read_bytes()).decode() if path.suffix == ".gz" else path.read_text()


def first_difference(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return None if len(a) == len(b) else min(len(a), len(b))


def report_diff(a, b, label_a, label_b, context=3, max_lines=20):
    i = first_difference(a, b)
    if i is None:
        return []
    out = [f"first difference at event {i} (of {len(a)} / {len(b)}):"]
    for j in range(max(0, i - context), min(max(len(a), len(b)), i + context + 1)):
        x = a[j] if j < len(a) else "<end>"
        y = b[j] if j < len(b) else "<end>"
        out.append(f"  {'*' if x != y else ' '} {label_a}: {x:<40} {label_b}: {y}")
    return out[:max_lines]


def trace_runs(script_path, script, base, bios, repeat, detail=False, prelude=None, log=print):
    """`repeat` traced runs into base/run<i>; returns (texts, status)."""
    texts = []
    for i in range(repeat):
        log(f"spu_trace {script['name']}, run {i + 1}/{repeat}")
        try:
            text, stats, elapsed = run_trace(script_path, script, base / f"run{i + 1}", bios, detail, prelude)
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            log(f"  FAIL: {e}")
            return texts, 1
        log(f"  {script['name']}: {stats['ticks']} ticks, {stats['writes']} writes, {stats['dma']} DMA blocks, unknown"
            f" values {stats['unknown_values']}; trace {len(text.encode())} bytes; {elapsed:.0f} s wall")
        texts.append(text)
    status = 0
    for i, t in enumerate(texts[1:], start=2):
        # The whole text, comments (calls, ticks, markers) included: every line must repeat.
        diff = report_diff(texts[0].splitlines(), t.splitlines(), "run1", f"run{i}")
        if diff:
            status = 1
            log(f"  {script['name']}: run 1 vs run {i} DIFFER (non-deterministic):\n    " + "\n    ".join(diff))
    if repeat > 1 and status == 0:
        log(f"  {script['name']}: determinism: {repeat} traces identical")
    return texts, status


def cmd_run(args):
    replay.check_tools()
    script_path = replay.script_file(args.script)   # resolved when it continues another ("after")
    script = cut_script(replay.load_script(script_path), args.until, args.extra_frames)
    bios = replay.bios_path(args.bios)
    base = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix="dcb_spu_trace_"))
    _, status = trace_runs(script_path, script, base, bios, args.repeat, args.detail, args.prelude)
    print(f"  outputs: {base}")
    return status


def cmd_record(args):
    """Traces the scripts twice each (in parallel, one emulator per script: each run's environment is set in its own
    subprocess) and writes tests/sound/traces/<name>.trace.gz when both runs agree."""
    replay.check_tools()
    names = args.scripts or list(COMMITTED)
    out = Path(args.out)

    def one(name):
        cmd = [sys.executable, str(Path(__file__).resolve()), "run", str(replay.SCRIPTS / f"{name}.json"), "--repeat",
               "2", "--out", str(out / name)]
        p = subprocess.run(cmd, capture_output=True, text=True)
        return name, p.returncode, p.stdout + p.stderr

    status = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, rc, text in pool.map(one, names):
            print(text, end="")
            if rc != 0:
                status = 1
                continue
            trace = (out / name / "run1" / "spu.trace").read_bytes()
            TRACES.mkdir(parents=True, exist_ok=True)
            dest = TRACES / f"{name}.trace.gz"
            dest.write_bytes(gzip.compress(trace, mtime=0))
            print(f"  recorded {dest.relative_to(ROOT)} ({dest.stat().st_size} bytes; {len(trace)} uncompressed)")
    return status


def cmd_diff(args):
    a = events(read_trace(args.a), args.align, not args.no_ticks)
    b = events(read_trace(args.b), args.align, not args.no_ticks)
    diff = report_diff(a, b, "A", "B", max_lines=args.max)
    if not diff:
        print(f"identical ({len(a)} events)")
        return 0
    print("\n".join(diff))
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="trace one script")
    r.add_argument("script")
    r.add_argument("--until", help="cut the script after its checkpoint of this name")
    r.add_argument("--extra-frames", type=int, default=0, help="frames to wait after the (cut) script")
    r.add_argument("--repeat", type=int, default=1, help="run N times and require identical traces")
    r.add_argument("--detail", action="store_true", help="add the storing pc, the DMA registers and LIBSND's calls"
                                                         " of itself as comments")
    r.add_argument("--bios", default="openbios", help="openbios (default), retail, or a BIOS file")
    r.add_argument("--out", help="output directory (default: a temp dir)")
    r.add_argument("--prelude", help="a Lua chunk run first")
    r.set_defaults(func=cmd_run)
    c = sub.add_parser("record", help="trace the scripts (twice each) and write tests/sound/traces/<name>.trace.gz")
    c.add_argument("scripts", nargs="*")
    c.add_argument("-j", "--jobs", type=int, default=2)
    c.add_argument("--out", default=str(ROOT / "build/spu-trace"))
    c.set_defaults(func=cmd_record)
    d = sub.add_parser("diff", help="compare two traces (comments ignored)")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--align", help="rebase ticks at the first '# mark' line containing this text")
    d.add_argument("--no-ticks", action="store_true", help="compare the order of events only")
    d.add_argument("--max", type=int, default=20, help="lines of context to print")
    d.set_defaults(func=cmd_diff)
    args = ap.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
