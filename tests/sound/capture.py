#!/usr/bin/env python3
"""Our SPU's rendering of the emulator's register writes against what the emulator itself played (docs/PORT.md
"Sound"). Adapted from dw2003recomp's tests/spu/capture.py (docs/THIRD_PARTY.md).

  tests/sound/capture.py [SCRIPT] [--extra-frames N] [--trace TRACE] [--capture F32] [--keep DIR]

PCSX-Redux has no audio dump, but it plays through SDL3, and SDL3's `disk` driver writes what it plays (F32LE stereo,
44,100 Hz). The run is at speed 1 (real time; about two minutes to the title) and paced by the host, so the capture has
gaps and is not a golden. Ours: the emulator's SPU trace of the same run (default the committed first_duel_play trace, of
which every script here is a prefix; with --extra-frames a trace of the lengthened script is recorded first, ~3 min)
replayed through the stack's LIBSND and SPU (tests/port/sound.py's replay, its WAV). From the first SsSeqPlay to the
script's end, tests/sound/capture_compare.c finds each 0.1 s window of ours in the capture by cross-correlation and
prints its correlation and level. Redux's SPU is an emulation itself: this checks that the two agree on what the
writes sound like (samples, pitch, envelopes, volumes, reverb), not bit-exactness. Needs the emulator and the disc.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/replay"))
sys.path.insert(0, str(ROOT / "tests/sound"))
sys.path.insert(0, str(ROOT / "tests/port"))
import replay  # noqa: E402
import sound  # noqa: E402  (tests/port/sound.py: the replay and its WAV)
import spu_trace  # noqa: E402

VSYNC = sound.VSYNC_FRAMES


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("script", nargs="?", default=str(ROOT / "tests/replay/scripts/title.json"))
    ap.add_argument("--extra-frames", type=int, default=0, help="wait this long at the script's end")
    ap.add_argument("--trace", help="the emulator trace to render (default: the committed one, or a new one with"
                                    " --extra-frames)")
    ap.add_argument("--capture", help="use this capture (F32LE stereo) instead of running the emulator")
    ap.add_argument("--keep", help="where the runs go (default: a temp dir)")
    args = ap.parse_args()
    out = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="dcb_capture_"))
    out.mkdir(parents=True, exist_ok=True)
    script_path = Path(args.script)
    script = spu_trace.cut_script(replay.load_script(script_path), None, args.extra_frames)
    if args.extra_frames:
        script["max_frames"] = script.get("max_frames", 20000) + args.extra_frames
    trace = Path(args.trace) if args.trace else None
    if trace is None and args.extra_frames:
        replay.check_tools()
        spu_trace.run_trace(script_path, script, out / "trace", replay.bios_path("openbios"))
        trace = out / "trace/spu.trace"
    trace = trace or sorted(spu_trace.TRACES.glob("*.trace.gz"))[-1]
    ours, t0 = sound.replay_wav(trace, out / "replay")
    if args.capture:
        capture = Path(args.capture)
    else:
        replay.check_tools()
        capture = out / "capture.f32"
        os.environ["SDL_AUDIO_DRIVER"] = "disk"
        os.environ["SDL_AUDIO_DISK_OUTPUT_FILE"] = str(capture)
        result = replay.run_once(script_path, script, replay.bios_path("openbios"), out / "emulator", speed=1)
        print(f"capture: {capture} ({capture.stat().st_size} bytes)")
    frames = json.loads((out / "emulator/result.json").read_text())["frames"] if not args.capture else None
    text = spu_trace.read_trace(trace)
    play = next(int(l.split()[1]) for l in text.splitlines() if " call SsSeqPlay(" in l)
    tool = out / "capture_compare"
    subprocess.run(["gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", str(ROOT / "tests/sound/capture_compare.c"),
                    "-o", str(tool), "-lm"], check=True)
    # ours: the samples after the flush of vsync v start at (v + 1 - t0) x 735 (sound_replay renders a vsync's
    # samples before its tick); from the music's start to the script's last frame
    cmd = [str(tool), str(ours), str(capture), str((play + 1 - t0) * VSYNC), "4410"]
    if frames:
        cmd.append(str((frames - t0) * VSYNC))
    print(f"ours: {ours} (the trace's first tick {t0}); the music from vsync {play}")
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
