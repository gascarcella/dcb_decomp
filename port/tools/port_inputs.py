#!/usr/bin/env python3
"""The PC port's build inputs (psxstack's GAME_CONTRACT.md "5. The build inputs"; port/CMakeLists.txt runs it at
configure time): what psxstack's generators need to know about this game, read from the tracked sources only.
Adapted from dw2003recomp's tools/port_inputs.py (docs/THIRD_PARTY.md).

  port/tools/port_inputs.py --out build/port/gen/inputs [--bss FILE] [--version us] [-v]   # units.txt, overlays.txt

  units.txt      every C unit the version builds (MAIN_C_SRC and <OVERLAY>_C_SRC of mk/version/<version>.mk, the
                 same lists the Makefile compiles; src/main/psyq/ is not among them: the shim replaces the
                 libraries) and its overlay: MAIN for the executable's, else the overlay's name in capitals. With
                 --bss, also FILE as a MAIN unit: port_bss.py writes it there, the executable's game .bss as C
  overlays.txt   every overlay of the version's OVERLAYS list: its name, its slot (1: there is one, the overlay area
                 at the end of the executable's .bss), its file ID and its symbol file
                 (config/<version>/symbols_<overlay>.txt). psxstack keys an overlay by a file ID, and this game loads
                 its overlays by name from P.DRV ("P:\\kawseg.bin"): the ID is the overlay's 1-based index in the
                 OVERLAYS list of the .mk, and the adapter maps the loader's name to the same index (M1).

The hand-written assembly (<BINARY>_HASM_SRC: startup.s, libmath.s) is not a unit: the host replaces it. A C file
under src/ that this version does not list (another version's copy, <module>_jp.c) is reported, not an error.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TIER = 1   # the one slot of port/game/game.json's memory.slots


def mk_variables(version):
    """{NAME: [words]} for every `NAME := words` of mk/version/<version>.mk, with the Makefile's $(addprefix P, L)
    expanded and nothing else interpreted (the file is plain values, as its header says)."""
    path = ROOT / "mk" / "version" / f"{version}.mk"
    if not path.exists():
        sys.exit(f"port_inputs: no such version: {path}")
    text = path.read_text().replace("\\\n", " ")
    out = {}
    for m in re.finditer(r"^(\w+)\s*:=\s*(.*)$", text, flags=re.M):
        body = m.group(2).split("#")[0]
        words = []
        for a in re.finditer(r"\$\(addprefix\s+(\S+),\s*([^)]*)\)", body):
            words += [a.group(1) + f for f in a.group(2).split()]
        words += re.sub(r"\$\(addprefix[^)]*\)", "", body).split()
        if any("$" in w for w in words):
            sys.exit(f"port_inputs: {path.name}: {m.group(1)} uses a make function this script does not expand: {body.strip()}")
        out[m.group(1)] = words
    return out


def overlays(mk):
    """The overlay names of the version, lower-case, in the .mk's order (the file ID is the 1-based index)."""
    names = mk.get("OVERLAYS")
    if not names:
        sys.exit("port_inputs: OVERLAYS is empty or missing in the .mk")
    return names


def units(mk):
    """-> [(src path relative to ROOT, 'MAIN' or the overlay's name in capitals)] in the .mk's order."""
    out = []
    for binary in ["main"] + overlays(mk):
        key = f"{binary.upper()}_C_SRC"
        if key not in mk:
            sys.exit(f"port_inputs: {key} is missing in the .mk")
        out += [(src, "MAIN" if binary == "main" else binary.upper()) for src in mk[key]]
    return out


def symbols_file(version, name):
    return ROOT / "config" / version / f"symbols_{name}.txt"


def write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the directory for units.txt and overlays.txt")
    ap.add_argument("--bss", help="write the executable's game .bss as C there (port_bss.py) and list it as a MAIN unit")
    ap.add_argument("--version", default="us", help="the game version (mk/version/<version>.mk; default us)")
    ap.add_argument("-v", "--verbose", action="store_true", help="list the C files under src/ the version does not build")
    args = ap.parse_args()
    out = Path(args.out)
    mk = mk_variables(args.version)
    us = units(mk)
    missing = [u for u, _ in us if not (ROOT / u).exists()]
    if missing:
        sys.exit(f"port_inputs: missing {' '.join(missing)}")
    listed = set(u for u, _ in us)
    extra = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*.c")
                   if p.relative_to(ROOT).as_posix() not in listed and "src/main/psyq/" not in p.as_posix())
    names = overlays(mk)
    for n in names:
        if not symbols_file(args.version, n).exists():
            sys.exit(f"port_inputs: no symbol file for {n}: {symbols_file(args.version, n)}")
    rows = [(ROOT / u, o) for u, o in us]
    if args.bss:
        import port_bss
        bss = Path(args.bss).resolve()
        print(port_bss.generate(args.version, bss))
        n_main = sum(1 for _, o in rows if o == "MAIN")
        rows.insert(n_main, (bss, "MAIN"))
    write(out / "units.txt", f"# Generated by port/tools/port_inputs.py ({args.version}); do not edit. <source>\t<overlay>\n"
          + "".join(f"{u}\t{o}\n" for u, o in rows))
    write(out / "overlays.txt", f"# Generated by port/tools/port_inputs.py ({args.version}); do not edit. "
          "<name>\t<tier>\t<file id: the overlay's index in the .mk's OVERLAYS>\t<symbols>\n"
          + "".join(f"{n.upper()}\t{TIER}\t{i}\t{symbols_file(args.version, n)}\n" for i, n in enumerate(names, 1)))
    print(f"port_inputs: {args.version}: {len(us)} units{' and the game .bss' if args.bss else ''}, {len(names)} "
          f"overlays (tier {TIER}) -> {out}"
          + (f"; {len(extra)} C files under src/ are not built by {args.version} (other versions' modules; -v lists them)"
             if extra else ""))
    if args.verbose and extra:
        print("\n".join(f"  {e}" for e in extra))


if __name__ == "__main__":
    main()
