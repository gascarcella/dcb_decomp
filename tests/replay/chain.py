"""Replay scripts that continue another (docs/PORT.md "Testing", "A script after another"): a script may name the one
it starts from, `"after": "<name>"`, and holds only its own steps; the tools run the combined script, the named one's
steps (resolved the same way: chains are allowed) followed by its own. psxstack's runners know nothing of it: every
script is written out resolved under build/replay/scripts/ (a script without "after" byte for byte, so its record's
script_sha1 is unchanged), and that directory is the one the emulator's and the port's runners are configured with
(tests/replay/replay.py, tests/port/run.py, so tests/port/vram.py and tests/sound/spu_trace.py too).

The combined script: the continuing script's own keys (name, description, ...), its steps after the base's;
`max_frames` the base's plus its own (each defaults to the runners' 20000: a continuation budgets its own steps);
`default_timeout` its own (else the base's), the base's steps that relied on a different default getting theirs as a
`timeout`; and two keys the runners ignore, `after` (the base's name) and `after_steps` (how many steps came from the
base: where the script's own begin). A checkpoint name may appear once in the whole chain. A record is the combined
script's, its script_sha1 that of the combined file: changing a base asks for its continuations to be re-recorded too.
"""
import json
import os
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/replay/scripts"          # the committed scripts
BUILT = ROOT / "build/replay/scripts"           # every committed script, resolved (the runners' scripts directory)
SCRATCH = ROOT / "build/replay/scratch"         # a continuing script from elsewhere, resolved
DEFAULT_MAX_FRAMES = 20000                      # psxstack's run.lua and runtime/script.c
DEFAULT_TIMEOUT = 3000


def _read(path):
    script = json.loads(Path(path).read_text())
    script.setdefault("name", Path(path).stem)
    return script


def resolve(path, _seen=()):
    """The script at `path` with its "after" chain resolved (a script without one as it is). The base is looked up
    beside the continuing script, then in tests/replay/scripts/."""
    path = Path(path)
    script = _read(path)
    after = script.get("after")
    if not after:
        return script
    if script["name"] in _seen or after in _seen + (script["name"],):
        raise SystemExit(f"replay: script {script['name']}: its \"after\" chain loops "
                         f"({' -> '.join(_seen + (script['name'], after))})")
    base_path = next((p for p in (path.parent / f"{after}.json", SOURCE / f"{after}.json") if p.exists()), None)
    if base_path is None:
        raise SystemExit(f"replay: script {script['name']}: \"after\": no script {after!r} (beside it or in "
                         f"{SOURCE.relative_to(ROOT)})")
    base = resolve(base_path, _seen + (script["name"],))
    base_timeout = base.get("default_timeout", DEFAULT_TIMEOUT)
    timeout = script.get("default_timeout", base_timeout)
    steps = []
    for step in base["steps"]:
        if timeout != base_timeout and "timeout" not in step:
            step = {**step, "timeout": base_timeout}
        steps.append(step)
    combined = {**script,
                "max_frames": base.get("max_frames", DEFAULT_MAX_FRAMES) + script.get("max_frames", DEFAULT_MAX_FRAMES),
                "default_timeout": timeout, "after_steps": len(steps), "steps": steps + list(script["steps"])}
    names = [s["name"] for s in combined["steps"] if s.get("type") == "checkpoint"]
    dups = sorted({n for n in names if names.count(n) > 1})
    if dups:
        raise SystemExit(f"replay: script {script['name']}: checkpoint names repeated after {after}: {', '.join(dups)}")
    return combined


def _write(dest, data):
    """`data` to `dest` unless it holds it already; atomically (several tools may run at once)."""
    if dest.exists() and dest.read_bytes() == data:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=dest.parent, prefix=f".{dest.name}.")
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    os.replace(tmp, dest)
    return dest


def _data(path):
    script = _read(path)
    if not script.get("after"):
        return Path(path).read_bytes()
    return (json.dumps(resolve(path), indent=1) + "\n").encode()


def sync():
    """Writes every committed script, resolved, into build/replay/scripts/ (and removes the ones no longer there);
    returns that directory."""
    sources = sorted(SOURCE.glob("*.json"))
    for p in sources:
        _write(BUILT / p.name, _data(p))
    names = {p.name for p in sources}
    for p in BUILT.glob("*.json"):
        if p.name not in names:
            p.unlink(missing_ok=True)
    return BUILT


def script_file(path):
    """The file a runner gets for the script at `path`: a committed script's resolved copy; another script with an
    "after" written resolved under build/replay/scratch/; any other script itself."""
    path = Path(path).resolve()
    if path.parent == SOURCE:
        return _write(BUILT / path.name, _data(path))
    if _read(path).get("after"):
        return _write(SCRATCH / path.name, _data(path))
    return path
