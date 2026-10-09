#!/usr/bin/env python3
"""The port's sound against the emulator's SPU write traces (docs/PORT.md "Sound", "Testing"; M3). Adapted from
dw2003recomp's tests/port/sound.py (docs/THIRD_PARTY.md).

Usage: tests/port/sound.py [replay] [TRACE ...] [--m32] [--sanitize] [--rate N] [--out DIR]
       tests/port/sound.py port [SCRIPT ...] [--out DIR] [--port-dir DIR]

`replay` (the default; CI's replay job): LIBSND as the PS1's, on the emulator's timeline. Each emulator trace
(default: the committed ones, tests/sound/traces/*.trace.gz, which tests/sound/spu_trace.py `record` writes) becomes a
replay script: the game's LIBSND and LIBSPU calls with their arguments (the VAB headers and bodies and the SEQs found
on the disc by the SHA-1 the trace gives them: tests/sound/disc.py), LIBSND's sequencer ticks where the emulator ran
them, and LIBCD's CdInit stores. tests/port/sound_replay.c, built from psxstack's psyq/libsnd*.c, psyq/libspu.c and
runtime/spu*.c, makes those calls on that timeline (rendering RATE SPU samples per vsync) and writes its SPU trace,
which must equal the emulator's: every store and DMA block, in order, at the same vsync (comments aside, as
spu_trace.py `diff` compares). Where it does not, the first difference is reported against tests/port/sound_known.json
(the known divergences, each with its reason: "dma_past_pak", the DMA blocks whose SHA-1 is not compared because their
last bytes are the RAM after the file, by their trace line (any trace of these scripts has them at those vsyncs), and
"first_difference", {trace name: {"event", "emulator", "why"}}, a trace whose first difference is that one passes as
"known", the part before it compared exactly; none now).
  --m32 / --sanitize   also the -m32 build and an ASan/UBSan build of the replay: the same trace, no report

`port` (informational): the port's own runs (tests/port/run.py's SPU traces, build/port-test/<script>/run1.spu.trace,
or --port-dir; default every script there) against the emulator's (its own trace, else the committed first_duel_play's, of
which the other scripts are prefixes): each function's calls in the same order with the same arguments (what the
port's trace shows: no pointer, no LIBSPU call, no tick; psxstack#63), where the merged order first differs (the
game's timing: the CD, the loaders, the movie, the script's presses meeting other frames), and after each SsSeqPlay
how long the music's key-ons stay the emulator's, tick for tick.

`wav PORT.wav PORT.spu.trace [--emulator-trace T]` (informational, M3's look at the audio itself): the port's `--wav`
output checked structurally (its length against the vsyncs at 735 frames each, per second the RMS and peak of each channel, clipped
samples, silences) from the first SsSeqPlay on, and, for each SsSeqPlay, the port's PCM against the replay's
rendering of the emulator's register writes (the committed trace replayed, sound_replay's WAV): placed by an exact
match of a 0.1 s window half a second in, then how long the two stay equal sample for sample; where they are not
equal, the mono mixes' correlation and level per second.

Needs the disc image (disks/us/dcb_us.bin: scripts/setup.sh disc), gcc and the venv; no emulator.
Exit 0 pass, 1 fail, 2 missing.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PSXSTACK = Path(os.environ.get("PSXSTACK_DIR") or ROOT / "psxstack")
sys.path.insert(0, str(ROOT / "tests/sound"))
import disc  # noqa: E402
from spu_trace import TRACES, events, read_trace, report_diff  # noqa: E402

KNOWN = ROOT / "tests/port/sound_known.json"
SRCS = [PSXSTACK / f for f in ("psyq/libsnd.c", "psyq/libsnd_seq.c", "psyq/libsnd_voice.c", "psyq/libsnd_spu.c",
                                "psyq/libspu.c", "runtime/spu.c", "runtime/spu_dsp.c", "runtime/sha1.c")]
SRCS.append(ROOT / "tests/port/sound_replay.c")
CFLAGS = ["-std=gnu99", "-O2", "-fwrapv", "-fsigned-char", "-fno-strict-aliasing", "-Wall", "-Wextra", "-Werror",
          "-DPC_PORT", "-DNON_MATCHING", f"-I{PSXSTACK}/include", f"-I{PSXSTACK}/include/psxstack",
          f"-I{PSXSTACK}/psyq", f"-I{PSXSTACK}/runtime"]
VARIANTS = {"m64": ["-m64"], "m32": ["-m32"],
            "san": ["-m64", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]}
# SPU samples per vsync. LIBSND reads every voice's envelope at the flush and reuses a voice once it reads 0, so the
# replay renders what the emulator rendered between two of its ticks. NTSC: 44,100 / 60 = 735 nominally.
EMULATOR_RATE = 735.0
CD_INIT = ["d80 mvol.l 3fff", "d82 mvol.r 3fff", "db0 cdvol.l 3fff", "db2 cdvol.r 3fff", "daa spucnt c001"]
# The calls the replay makes (sound_replay.c): the arguments it takes from the trace's (all of them, in order).
REPLAYED = {"SsInit", "SsSetTableSize", "SsSetTickMode", "SsStart", "SsStart2", "SsSetMVol", "SsSetStereo", "SsSetMono",
            "SsSetSerialAttr", "SsSetSerialVol", "SsUtSetReverbType", "SsUtSetReverbDepth", "SsUtReverbOn",
            "SsUtReverbOff", "SpuClearReverbWorkArea", "SsVabOpenHeadSticky", "SsVabTransBody", "SsVabTransCompleted",
            "SsVabClose", "SsSeqOpen", "SsSeqClose", "SsSeqPlay", "SsSeqStop", "SsSeqSetVol", "SsSeqGetVol",
            "SsUtAllKeyOff", "SsUtKeyOnV", "SsUtKeyOffV", "SpuSetVoiceAttr", "SpuSetCommonAttr"}
NO_SPU = {"CdInit"}   # logged, its stores replayed by the CD_INIT pattern (`cdinit`)

CALL_RE = re.compile(r"# (\d+) call (\w+)\(([^)]*)\) ra=([0-9a-f]+)(.*)$")


class Missing(Exception):
    pass


def s32(text):
    v = int(text, 16) if text.startswith("0x") else int(text)
    return v - (1 << 32) if v >= 1 << 31 else v


def parse_call(line):
    """(tick, name, args, extras) of an emulator call comment, or None (the library's calls of itself)."""
    m = CALL_RE.match(line)
    if not m or "(internal)" in m.group(5):
        return None
    args = [s32(x.strip()) for x in m.group(3).split(",") if x.strip()]
    extras = dict(e.split("=", 1) for e in m.group(5).split() if "=" in e)
    return int(m.group(1)), m.group(2), args, extras


def replay_script(text, work, chunks):
    """The replay script for a trace (sound_replay.c's format), its data files written into `work`; and the DMA blocks
    whose last bytes no file holds (a VAB body that ends its PAK: LIBSND sends whole 64-byte blocks, so the PS1's
    transfer reads up to 60 bytes of the RAM after the file, the heap's leftovers), as "<vsync> dma4 spu=.. len=..".
    """
    lines = text.splitlines()
    out, data = [], {}
    count, vsync = 0, None
    unknown = set()

    def data_no(sha1):
        if sha1 not in data:
            if sha1 not in chunks:
                raise RuntimeError(f"no chunk of the sound PAKs on the disc has the SHA-1 {sha1}")
            path = work / f"chunk_{sha1[:12]}.bin"
            path.write_bytes(chunks[sha1][5])
            data[sha1] = len(data)
            out.append(f"data {data[sha1]} {path}")
        return data[sha1]

    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("# ") and line.split()[2:3] == ["tick"]:
            # a tick that stored nothing before the next one: the vblank came while a key call (SsUtKeyOnV) held
            # LIBSND's lock, so its flush was skipped (the call spans the vsync on the PS1)
            nxt = next((x for x in lines[i + 1:] if not x.startswith("#") or x.split()[2:3] == ["tick"]), "")
            locked = nxt.startswith("#") or not nxt
            cycles = [w for w in line.split()[3:] if w.startswith("cycles=")]
            out.append(f"tick {count} {line.split()[1]}" + (" locked" if locked else "") + "".join(" " + c for c in cycles))
        elif line.startswith("# ") and " call " in line:
            c = parse_call(line)
            if c is not None:
                tick, name, args, extras = c
                if name in NO_SPU:
                    pass
                elif name not in REPLAYED:
                    raise RuntimeError(f"a call the replay does not make: {line}")
                elif "data" in extras:
                    sha1 = extras["data"].split(":")[1]
                    n = data_no(sha1)
                    out.append(f"call {tick} {name} {n}+0 " + " ".join(str(a) for a in args[1:]))
                    if name == "SsVabTransBody":
                        dma = next((x.split() for x in lines[i + 1:] if x.split()[1:2] == ["dma4"]), None)
                        if dma and int(dma[3].split("=")[1]) > len(chunks[sha1][5]):
                            unknown.add(" ".join(dma[:4]))
                elif "mem" in extras:
                    out.append(f"call {tick} {name} x{extras['mem']}")
                else:
                    out.append(f"call {tick} {name} " + " ".join(str(a) for a in args))
        elif not line.startswith("#") and line.strip():
            tick = int(line.split()[0])
            if tick != vsync:
                out.append(f"vsync {count} {tick}")
                vsync = tick
            body = [" ".join(x.split("#")[0].split()[1:]) for x in lines[i:i + 5]]
            if body == CD_INIT:
                out.append(f"cdinit {tick}")
            count += 1
        i += 1
    script = work / "replay.script"
    script.write_text("\n".join(out) + "\n")
    return script, unknown


def build(variant, out_dir):
    exe = out_dir / variant / "sound_replay"
    exe.parent.mkdir(parents=True, exist_ok=True)
    gen = ROOT / "build/port/gen/include"
    if not (gen / "psxstack_game_gen.h").exists():
        raise Missing("build/port/gen/include is missing: configure the port first (scripts/port_build.sh)")
    cmd = ["gcc", *CFLAGS, f"-I{gen}", *VARIANTS[variant], *map(str, SRCS), "-o", str(exe), "-lm"]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"the {variant} build of sound_replay failed:\n{proc.stdout}{proc.stderr}")
    return exe


def trace_name(path):
    return Path(path).name.split(".")[0]


def load_known():
    return json.loads(KNOWN.read_text()) if KNOWN.exists() else {}


def context(text, want_index):
    """What the game was doing at the trace's event `want_index`: the last call and the last marker before it."""
    count, last_call, last_mark = 0, None, None
    for line in text.splitlines():
        if line.startswith("# mark "):
            last_mark = line[2:]
        elif line.startswith("# ") and " call " in line and "(internal)" not in line:
            last_call = line[2:].split(" ra=")[0]
        elif not line.startswith("#") and line.strip():
            if count == want_index:
                break
            count += 1
    return last_call, last_mark


def replay(trace_path, variants, out_dir, rate, chunks, known):
    """Replays a trace's calls on its timeline (sound_replay, each build variant) and compares the result with the
    trace. Returns (failures, result line)."""
    name = trace_name(trace_path)
    work = out_dir / name
    work.mkdir(parents=True, exist_ok=True)
    text = read_trace(trace_path)
    script, unknown = replay_script(text, work, chunks)

    def past_file(evs):
        """The SHA-1 of a DMA block whose last bytes no file holds is not compared (its address and length are)."""
        return [e.split(" sha1=")[0] + " sha1=(past the file)" if e.split(" sha1=")[0] in unknown else e for e in evs]

    want = past_file(events(text))
    listed = known.get("dma_past_pak", {})
    failures = [f"{name}: a DMA block reads past its PAK and {KNOWN.name} does not list it: {u}"
                for u in sorted(unknown - set(listed))]
    last = int(want[-1].split()[0]) if want else 0
    for u in sorted(set(listed) - unknown):
        if int(u.split()[0]) <= last:
            print(f"  stale: {KNOWN.name} lists {u!r}, which this trace does not have")
    if unknown:
        print(f"  not compared (known, {KNOWN.name}): the SHA-1 of {len(unknown)} DMA block(s) that read past their PAK"
              f" into the RAM after it: {', '.join(sorted(unknown))}")
    first, result = None, None
    for v in variants:
        exe = build(v, out_dir)
        got_path = work / f"replay_{v}.trace"
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")
        proc = subprocess.run([str(exe), str(script), str(got_path), str(round(rate * 100)), "ntsc"],
                              capture_output=True, text=True, env=env, timeout=1200)
        if proc.returncode != 0 or "runtime error" in proc.stderr or "AddressSanitizer" in proc.stderr:
            failures.append(f"{name} [{v}]: sound_replay exit {proc.returncode}\n{proc.stderr[-3000:]}")
            continue
        got = past_file(events(got_path.read_text()))
        diff = report_diff(want, got, "emulator", "replay", max_lines=24)
        if first is None:
            first = got
            if not diff:
                result = (f"{name}: identical to the emulator's trace ({len(want)} stores and DMA blocks, vsyncs"
                          f" {want[0].split()[0]}..{want[-1].split()[0]}, {rate} samples per vsync)")
            else:
                k = next(i for i in range(min(len(want), len(got)) + 1)
                         if i >= len(want) or i >= len(got) or want[i] != got[i])
                call, mark = context(text, k)
                where = (f"event {k} of {len(want)}: emulator `{want[k] if k < len(want) else '<end>'}`, replay"
                         f" `{got[k] if k < len(got) else '<end>'}`; after `{call}` ({mark})")
                entry = known.get("first_difference", {}).get(name)
                if entry and entry.get("event") == k and entry.get("emulator") == (want[k] if k < len(want) else None):
                    result = f"{name}: KNOWN first difference at {where}: {entry['why']}"
                else:
                    failures.append(f"{name} [{v}]: the replay differs from the emulator's trace (replay: {got_path});"
                                    f" first difference at {where}\n    " + "\n    ".join(diff))
                    result = f"{name}: first difference at {where}"
        elif got != first:
            failures.append(f"{name} [{v}]: differs from the {variants[0]} build's replay")
        else:
            print(f"  [{v}] the same trace as the {variants[0]} build's, no sanitizer report")
    if result:
        print(f"  {result}")
    return failures, result


def check_libsnd(variants, out_dir, traces=None, rate=EMULATOR_RATE):
    """LIBSND against the committed emulator traces (or `traces`). Returns the failures."""
    if not disc.IMAGE.exists():
        raise Missing(f"no disc image ({disc.IMAGE}: scripts/setup.sh disc)")
    chunks = disc.sound_chunks(disc.Disc())
    known = load_known()
    failures, seen = [], set()
    paths = traces or sorted(TRACES.glob("*.trace.gz"))
    if not paths:
        raise Missing("no emulator trace (tests/sound/traces/*.trace.gz: tests/sound/spu_trace.py record)")
    for t in paths:
        print(f"sound: LIBSND replayed on the emulator's timeline: {t}")
        f, _ = replay(t, variants, Path(out_dir), rate, chunks, known)
        failures += f
        seen.add(trace_name(t))
    for name in sorted(set(known.get("first_difference", {})) - seen if not traces else ()):
        failures.append(f"{KNOWN.name} lists {name}, which has no trace")
    return failures


# ---- The port's own run ----

PORT_CALL_RE = re.compile(r"# (\d+) call (\w+)\(([^)]*)\)(?: = (-?\d+))?$")
# What the port's call comments show of each call (psxstack's psyq/libsnd.c): pointers are not shown, and a few
# calls show fewer arguments; compared on what both have.
PORT_ARGS = {"SsVabOpenHeadSticky": (1, 2), "SsVabTransBody": (1,), "SsSeqOpen": (1,), "SsSetTableSize": (1, 2)}
PORT_SILENT = {"SpuSetVoiceAttr", "SpuSetCommonAttr", "SpuClearReverbWorkArea", "SsSeqGetVol", "CdInit",
               "SsVabTransCompleted"}   # not in the port's trace, or (the polls) CPU time


def emulator_calls(text):
    out = []
    for line in text.splitlines():
        c = parse_call(line) if line.startswith("# ") and " call " in line else None
        if c is None or c[1] in PORT_SILENT:
            continue
        tick, name, args, _ = c
        keep = PORT_ARGS.get(name)
        out.append((tick, name, [args[i] for i in keep] if keep else args))
    return out


def port_calls(text):
    out = []
    for line in text.splitlines():
        m = PORT_CALL_RE.match(line)
        if not m or m.group(2) in PORT_SILENT:
            continue
        args = [int(x, 0) for x in m.group(3).replace(" ", "").split(",") if x]
        name = m.group(2)
        if name == "SsVabOpenHeadSticky":
            args = [args[0], args[1]]
        elif name == "SsSetTableSize":
            args = args[:2]
        out.append((int(m.group(1)), name, args))
    return out


def norm(name, args):
    """Arguments as the PS1 passed them (s16 and char arguments in their registers' width)."""
    return name, [a & 0xFFFF if a < 0 else a for a in args]


def stores_after(text, call_name, k):
    """The events after the k-th call of `call_name` (port or emulator comment), ticks counted from the call, with
    the vsync of the call."""
    out, base, seen = [], None, -1
    for line in text.splitlines():
        if base is None:
            if line.startswith("# ") and f" call {call_name}(" in line and "(internal)" not in line:
                seen += 1
                if seen == k:
                    base = int(line.split()[1])
            continue
        body = line.split("#", 1)[0].split()
        if body:
            out.append(f"{int(body[0]) - base} {' '.join(body[1:])}")
    return base, out


def key_ons(evs):
    """The key-ons of the music's voices (0-17; the effects play on 18-21: src/main/system/sound_play.c) in events
    with ticks counted from a call: [(tick, voice bits)]."""
    out = []
    for e in evs:
        f = e.split()
        if f[2] == "kon.lo" and int(f[3], 16):
            out.append((int(f[0]), int(f[3], 16)))
        elif f[2] == "kon.hi" and int(f[3], 16) & 3:
            out.append((int(f[0]), (int(f[3], 16) & 3) << 16))
    return out


def fmt_kon(k):
    return f"+{k[0]} voices {k[1]:05x}"


def check_port_run(port_trace, emu_trace):
    """A port run's trace against the emulator's (informational; the gate is `replay`): (1) each function's calls in
    the same order with the same arguments (the port's run of a shorter script: a prefix of them); (2) where the merged
    order differs (the game's timing: the CD, the loaders, the movie, the script's presses meeting another frame);
    (3) after each SsSeqPlay, how long the music's key-ons stay the emulator's (ticks counted from the call: the notes
    and their timing; the voices' other stores carry what sounded before, which the game's timing changes)."""
    pt, et = read_trace(port_trace), read_trace(emu_trace)
    pc, ec = port_calls(pt), emulator_calls(et)
    names = sorted({n for _, n, _ in pc} | {n for _, n, _ in ec})
    pnames = {n for _, n, _ in pc}
    same = True
    for name in names:
        a = [norm(n, x) for _, n, x in pc if n == name]
        b = [norm(n, x) for _, n, x in ec if n == name][:len(a)]
        k = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)
        if k is not None or len(a) > len(b):
            same = False
            k = len(b) if k is None else k
            print(f"  {name}: call {k} differs: port {a[k][1]}, emulator {b[k][1] if k < len(b) else '<none>'}")
    print(f"  {'the same' if same else 'NOT the same'} calls per function as the emulator's (in order, with their"
          f" arguments): {len(pc)} calls of {len(pnames)} functions" + ("" if len(pc) == len(ec) else
                                                                       f" (the emulator's run: {len(ec)})"))
    order = [(n, tuple(a)) for _, n, a in pc]
    eorder = [(n, tuple(a)) for _, n, a in ec][:len(order)]
    k = next((i for i, (x, y) in enumerate(zip(order, eorder)) if x != y), None)
    if k is None:
        print(f"  the same order throughout; first call at vsync {pc[0][0]} (port) / {ec[0][0]} (emulator)")
    else:
        print(f"  the same order up to call {k}: then port {pc[k][1]}{pc[k][2]} at vsync {pc[k][0]}, emulator"
              f" {ec[k][1]}{ec[k][2]} at vsync {ec[k][0]} (the game's timing)")
    plays = sum(1 for _, n, _ in pc if n == "SsSeqPlay")
    for k in range(plays):
        pb, pe = stores_after(pt, "SsSeqPlay", k)
        eb, ee = stores_after(et, "SsSeqPlay", k)
        if eb is None:
            continue
        a, b = key_ons(pe), key_ons(ee)
        i = next((j for j, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        why = (f"then port {fmt_kon(a[i])}, emulator {fmt_kon(b[i])}" if i < min(len(a), len(b))
               else "to the end of a trace")
        print(f"  SsSeqPlay #{k} (port vsync {pb}, emulator {eb}): the music's key-ons (voices 0-17, vsyncs counted from"
              f" the call) agree for {i} of {len(a)} / {len(b)}, through +{a[i - 1][0] if i else 0}; {why}")
    return []


# ---- The audio (wav) ----

SPU_RATE = 44100
VSYNC_FRAMES = 735          # 44,100 / 60: the port's frames per vsync (psxstack's runtime/audio.c)


def read_wav(path):
    """(the 16-bit stereo samples as bytes, frames) of a 44-byte-header WAV (the port's and sound_replay's)."""
    raw = Path(path).read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE" or raw[22:24] != b"\x02\x00" or raw[34:36] != b"\x10\x00":
        raise RuntimeError(f"{path}: not a 16-bit stereo WAV")
    data = raw[44:]
    return data, len(data) // 4


def wav_stats(data, start, end):
    """Per second from frame `start` to `end`: (second, rms_l, rms_r, peak, clipped)."""
    import array
    a = array.array("h")
    a.frombytes(data[start * 4:end * 4])
    out = []
    for s0 in range(0, len(a), SPU_RATE * 2):
        sec = a[s0:s0 + SPU_RATE * 2]
        left, right = sec[0::2], sec[1::2]
        n = max(len(left), 1)
        out.append((s0 // (SPU_RATE * 2), (sum(x * x for x in left) / n) ** 0.5, (sum(x * x for x in right) / n) ** 0.5,
                    max(max(left, default=0), -min(left, default=0), max(right, default=0), -min(right, default=0)),
                    sum(1 for x in sec if x in (32767, -32768))))
    return out


def next_play(plays, k, end):
    return plays[k + 1] if k + 1 < len(plays) else end


def correlate(a, b, a0, b0, vsyncs):
    """Per second over `vsyncs` from a0 / b0 (byte offsets): the normalized correlation of the mono mixes and the RMS
    ratio a / b, as text."""
    import array
    out = []
    for sec in range(int(vsyncs // 60)):
        x, y = array.array("h"), array.array("h")
        x.frombytes(a[a0 + sec * SPU_RATE * 4:a0 + (sec + 1) * SPU_RATE * 4])
        y.frombytes(b[b0 + sec * SPU_RATE * 4:b0 + (sec + 1) * SPU_RATE * 4])
        if len(x) != len(y) or not x:
            break
        mx = [x[i] + x[i + 1] for i in range(0, len(x), 2)]
        my = [y[i] + y[i + 1] for i in range(0, len(y), 2)]
        sxy = sum(p * q for p, q in zip(mx, my))
        sxx, syy = sum(p * p for p in mx), sum(q * q for q in my)
        out.append(f"{sxy / (sxx * syy) ** 0.5:.3f}/{(sxx / syy) ** 0.5:.2f}" if sxx and syy else "silent")
    return " ".join(out) if out else "(under a second)"


def replay_wav(trace, out_dir):
    """The replay's rendering of an emulator trace (sound_replay's WAV) and the vsync of its first sample's tick."""
    out_dir.mkdir(parents=True, exist_ok=True)
    chunks = disc.sound_chunks(disc.Disc())
    script, _ = replay_script(read_trace(trace), out_dir, chunks)
    exe = build("m64", out_dir)
    wav = out_dir / "emulator_timeline.wav"
    subprocess.run([str(exe), str(script), str(out_dir / "replay.trace"), str(EMULATOR_RATE * 100).split(".")[0],
                    "ntsc", str(wav)], check=True)
    first_tick = next(int(l.split()[1]) for l in read_trace(trace).splitlines() if l.split()[2:3] == ["tick"])
    return wav, first_tick


def check_wav(port_wav, port_trace, out_dir, emu_trace=None):
    pdata, pframes = read_wav(port_wav)
    pt = read_trace(port_trace)
    ticks = int(re.search(r"# ticks (\d+)", pt).group(1))
    print(f"sound: {port_wav}: {pframes} frames ({pframes / SPU_RATE:.2f} s); {ticks} vsyncs x {VSYNC_FRAMES} ="
          f" {ticks * VSYNC_FRAMES} ({'equal' if ticks * VSYNC_FRAMES == pframes else 'DIFFERENT'})")
    plays = [int(l.split()[1]) for l in pt.splitlines() if " call SsSeqPlay(" in l]
    if not plays:
        print("  no SsSeqPlay in the run")
        return []
    start = plays[0] * VSYNC_FRAMES
    stats = wav_stats(pdata, start, pframes)
    print(f"  from the first SsSeqPlay (vsync {plays[0]}), per second: RMS L/R, peak, clipped samples")
    for sec, rl, rr, peak, clip in stats:
        print(f"    {sec:4d} s  {rl:7.0f} {rr:7.0f}  {peak:6d}  {clip}")
    silent = [s[0] for s in stats if s[1] < 1 and s[2] < 1]
    print(f"  {len(stats)} s: RMS {min(min(s[1], s[2]) for s in stats):.0f}..{max(max(s[1], s[2]) for s in stats):.0f},"
          f" peak {max(s[3] for s in stats)}, clipped {sum(s[4] for s in stats)}, silent seconds {silent or 'none'}")
    # Against the emulator's register writes rendered by our SPU (the committed trace's replay).
    emu_trace = emu_trace or sorted(TRACES.glob("*.trace.gz"))[-1]
    ewav, t0 = replay_wav(emu_trace, out_dir / "replay")
    edata, eframes = read_wav(ewav)
    eplays = [int(l.split()[1]) for l in read_trace(emu_trace).splitlines() if " call SsSeqPlay(" in l]
    win = SPU_RATE // 10 * 4
    vs = VSYNC_FRAMES * 4
    for k, (pv, ev) in enumerate(zip(plays, eplays)):
        # a 0.1 s window of the port's music, half a second in (not silent), found exactly within 20 vsyncs of where
        # the emulator's vsync count puts it; then the equal stretch around it, both ways
        p0 = (pv + 30) * vs
        probe = pdata[p0:p0 + win]
        found = None
        if len(probe) == win and probe.count(0) < win:
            guess = (ev + 30 - t0) * vs
            for d in range(-20 * VSYNC_FRAMES, 20 * VSYNC_FRAMES + 1):
                e0 = guess + d * 4
                if e0 >= 0 and edata[e0:e0 + win] == probe:
                    found = e0
                    break
        if found is None:
            corr = correlate(pdata, edata, pv * vs, (ev + 1 - t0) * vs, next_play(plays, k, pframes // VSYNC_FRAMES) - pv)
            print(f"  SsSeqPlay #{k} (port vsync {pv}, emulator {ev}): not equal sample for sample (what sounded before"
                  f" differs: the reverb's history, notes of the music faded out at another point); per second from the"
                  f" call, aligned on it, the mono mix's correlation and level (port over emulator): {corr}")
            continue
        after = 0
        while p0 + after + 4 <= len(pdata) and found + after + 4 <= len(edata) and \
                pdata[p0 + after:p0 + after + 4] == edata[found + after:found + after + 4]:
            step = vs if pdata[p0 + after:p0 + after + vs] == edata[found + after:found + after + vs] else 4
            after += step
        before = 0
        while p0 - before - 4 >= 0 and found - before - 4 >= 0 and \
                pdata[p0 - before - 4:p0 - before] == edata[found - before - 4:found - before]:
            before += 4
        lo, hi = (p0 - before) / vs - pv, (p0 + after) / vs - pv
        end = "the end of the port's run" if p0 + after >= len(pdata) else "a difference"
        print(f"  SsSeqPlay #{k} (port vsync {pv}, emulator {ev}): the port's PCM equals the emulator timeline's"
              f" rendering sample for sample from {lo:+.1f} to {hi:+.1f} vsyncs around it ({(hi - max(lo, 0)) / 60:.1f} s"
              f" of music, to {end})")
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", nargs="?", default="replay")
    ap.add_argument("traces", nargs="*")
    ap.add_argument("--m32", action="store_true", help="also the -m32 build of the replay")
    ap.add_argument("--sanitize", action="store_true", help="also an ASan/UBSan build of the replay")
    ap.add_argument("--out", default=str(ROOT / "build/port-sound"))
    ap.add_argument("--port-dir", default=str(ROOT / "build/port-test"), help="port mode: tests/port/run.py's output")
    ap.add_argument("--emulator-trace", help="wav mode: the emulator trace to render (default: the committed one)")
    ap.add_argument("--rate", type=float, default=EMULATOR_RATE, help="SPU samples rendered per vsync in the replay")
    args = ap.parse_args()
    if args.mode == "wav":
        if len(args.traces) != 2:
            ap.error("wav PORT.wav PORT.spu.trace")
        return 1 if check_wav(args.traces[0], args.traces[1], Path(args.out) / "wav", args.emulator_trace) else 0
    if args.mode not in ("replay", "port"):
        args.traces.insert(0, args.mode)
        args.mode = "replay"
    try:
        if args.mode == "port":
            names = args.traces or sorted(p.parent.name for p in Path(args.port_dir).glob("*/run1.spu.trace"))
            committed = sorted(TRACES.glob("*.trace.gz"))
            if not committed:
                raise Missing("no emulator trace (tests/sound/traces/*.trace.gz)")
            failures = []
            for name in names:
                pt = Path(args.port_dir) / name / "run1.spu.trace"
                if not pt.exists():
                    print(f"sound: {name}: no port trace ({pt}: tests/port/run.py {name})")
                    continue
                # the scripts are prefixes of each other: a script without its own trace is compared with the
                # committed one's beginning
                et = TRACES / f"{name}.trace.gz"
                et = et if et.exists() else committed[-1]
                print(f"sound: the port's {name} run against the emulator's ({et.name})")
                failures += check_port_run(pt, et)
        else:
            variants = ["m64"] + (["m32"] if args.m32 else []) + (["san"] if args.sanitize else [])
            failures = check_libsnd(variants, Path(args.out), [Path(t) for t in args.traces] or None, args.rate)
    except Missing as e:
        print(f"sound: {e}")
        return 2
    except RuntimeError as e:
        failures = [str(e)]
    if failures:
        print("sound: FAIL\n  " + "\n  ".join(failures))
        return 1
    print("sound: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
