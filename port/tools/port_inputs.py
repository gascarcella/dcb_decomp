#!/usr/bin/env python3
"""The PC port's build inputs (psxstack's GAME_CONTRACT.md "5. The build inputs"; port/CMakeLists.txt runs it at
configure time): what psxstack's generators need to know about this game, read from the tracked sources only.
Adapted from dw2003recomp's tools/port_inputs.py (docs/THIRD_PARTY.md).

  port/tools/port_inputs.py --out build/port/gen/inputs [--bss FILE] [--version us] [-v]

  units.txt      every C unit the version builds (MAIN_C_SRC and <OVERLAY>_C_SRC of mk/version/<version>.mk, the
                 same lists the Makefile compiles; src/main/psyq/ is not among them: the shim replaces the
                 libraries) and its overlay: MAIN for the executable's, else the overlay's name in capitals. With
                 --bss, also FILE as a MAIN unit: port_bss.py writes it there, the executable's game .bss as C
  overlays.txt   every overlay of the version's OVERLAYS list: its name, its slot (1: there is one, the overlay area
                 at the end of the executable's .bss), its file ID and its symbol file
                 (config/<version>/symbols_<overlay>.txt). psxstack keys an overlay by a file ID, and this game loads
                 its overlays by name from P.DRV ("P:\\kawseg.bin"): the ID is the overlay's 1-based index in the
                 OVERLAYS list of the .mk, and the adapter maps the loader's name to the same index
                 (overlay_ids.h).
  volatile.txt   tests/replay/replay.py's VOLATILE_RANGES (the profile's bytes the stable hash zeroes): one definition
                 for the emulator's records and the port's (psxstack_add_game's VOLATILE)
  overlay_ids.h  for the adapter (port/game/game.c and state.c include it): the same overlay list as overlays.txt, as
                 GAME_OVERLAY_<NAME> file IDs and the loader's file names, and the PS1 addresses of the symbols the
                 adapter's probes map (PS1_<NAME>, from config/<version>/'s symbol files): one source for both

The hand-written assembly (<BINARY>_HASM_SRC: startup.s, libmath.s) is not a unit: the host replaces it. A C file
under src/ that this version does not list (another version's copy, <module>_jp.c) is reported, not an error.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TIER = 1   # the one slot of port/game/game.json's memory.slots
REPLAY_PY = ROOT / "tests" / "replay" / "replay.py"
# The PS1 symbols whose addresses the adapter needs (port/game/game.c and state.c: the overlay area, the scripts'
# wait_mem targets, and tests/port/vram.py's alignment waits), looked up in config/<version>/symbols*.txt and
# undefined_syms*.txt.
ADAPTER_SYMBOLS = ["OVERLAY_AREA", "PLAYER_PROFILES", "OPEN_TITLE_STATE", "OPEN_INTRO_TEXT",
                   "OPEN_MEMCARD", "OPEN_MEMCARD_STATE", "SCROLL_BACKGROUND", "SAI_AREA", "PAD_INPUT_ENABLED",
                   "DUEL_DIALOG", "KAW_TUTORIAL_WINDOW", "KAW_RESULT_SCREEN_STATE",
                   # SUBSEG (deck_edit.json)
                   "SUB_EDITOR", "SUB_DECK_EDIT", "SUB_DECK_MENU", "SUB_CARD_LIST_MENU", "SUB_CARD_SORT_MENU",
                   "SUB_DECK_SORT_MENU", "SUB_EQUIPMENT_MENU", "SUB_ABILITY_MENU", "SUB_WINDOWS", "SUB_PARTNER_WINDOW",
                   "SUB_ARMOR_WINDOW", "SUB_ABILITY_WINDOW", "SUB_EQUIPMENT_WINDOW", "SUB_DECK_SORT_WINDOW",
                   "SUB_PARTNER_TABS", "SUB_CARD_IMAGE_CACHE", "SUB_PARTNER_SLOT", "SUB_PARTNER_TITLE_SHOWN",
                   # SAISEG's world map, EVOSEG (fusion.json)
                   "SAI_WORLD_MAP", "EVO_FUSION", "EVO_DIALOG", "EVO_CURSOR_CARD", "EVO_CUTSCENE_STEP", "EVO_BANNER_FADE"]


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


def ps1_addresses(version, names):
    """{name: address} for `names`, from the version's symbol files (`NAME = 0xADDR;`); each must be defined once, or
    with one address."""
    found = {}
    for f in sorted((ROOT / "config" / version).glob("*syms*.txt")) + sorted((ROOT / "config" / version).glob("symbols*.txt")):
        for m in re.finditer(r"^\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", f.read_text(), flags=re.M):
            if m.group(1) in names:
                found.setdefault(m.group(1), set()).add(int(m.group(2), 16))
    for n in names:
        if len(found.get(n, ())) != 1:
            sys.exit(f"port_inputs: {n}: {'no address' if n not in found else 'several addresses'} in config/{version}/")
    return {n: found[n].pop() for n in names}


def volatile_ranges():
    """VOLATILE_RANGES of tests/replay/replay.py, read from its source (as dw2003recomp's tools/port_inputs.py)."""
    import ast
    for node in ast.parse(REPLAY_PY.read_text()).body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "VOLATILE_RANGES" for t in node.targets):
            ranges = ast.literal_eval(node.value)
            if not all(len(r) == 2 and 0 <= r[0] < r[1] for r in ranges):
                sys.exit(f"port_inputs: VOLATILE_RANGES in {REPLAY_PY.name} is not a list of (lo, hi) pairs")
            return [tuple(r) for r in ranges]
    sys.exit(f"port_inputs: no VOLATILE_RANGES in {REPLAY_PY}")


def overlay_ids_h(version, names, addrs):
    """The adapter's header: the overlay IDs and file names (the .mk's order, as overlays.txt) and the PS1 addresses."""
    w = max(len(n) for n in names)
    return (f"/* Generated by port/tools/port_inputs.py ({version}) from mk/version/{version}.mk's OVERLAYS and "
            f"config/{version}/; do not edit. */\n"
            "#ifndef DCB_OVERLAY_IDS_H\n#define DCB_OVERLAY_IDS_H\n\n"
            f"/* The overlays' slot (game.json memory.slots) and file IDs: the 1-based index in OVERLAYS (overlays.txt). */\n"
            f"#define GAME_OVERLAY_TIER {TIER}\n"
            + "".join(f"#define GAME_OVERLAY_{n.upper():<{w}} {i}\n" for i, n in enumerate(names, 1))
            + f"#define GAME_OVERLAY_COUNT {len(names)}\n"
            "/* {the name the loader opens in P.DRV (\"P:\\\\kawseg.bin\"), the file ID}, in file ID order */\n"
            "#define GAME_OVERLAY_FILES { \\\n"
            + "".join(f'        {{ "{n}.bin", {i} }}, \\\n' for i, n in enumerate(names, 1))
            + "    }\n\n"
            "/* The PS1 addresses the adapter maps (config/" + version + "/symbols*.txt, undefined_syms*.txt). */\n"
            + "".join(f"#define PS1_{n} 0x{a:08X}u\n" for n, a in addrs.items())
            + "\n#endif /* DCB_OVERLAY_IDS_H */\n")


def write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the directory for the generated files")
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
    vol = volatile_ranges()
    write(out / "volatile.txt", "# Generated by port/tools/port_inputs.py from tests/replay/replay.py VOLATILE_RANGES; "
          "do not edit.\n" + "".join(f"0x{lo:X} 0x{hi:X}\n" for lo, hi in vol))
    write(out / "overlay_ids.h", overlay_ids_h(args.version, names, ps1_addresses(args.version, ADAPTER_SYMBOLS)))
    print(f"port_inputs: {args.version}: {len(us)} units{' and the game .bss' if args.bss else ''}, {len(names)} "
          f"overlays (tier {TIER}), {len(vol)} volatile ranges -> {out}"
          + (f"; {len(extra)} C files under src/ are not built by {args.version} (other versions' modules; -v lists them)"
             if extra else ""))
    if args.verbose and extra:
        print("\n".join(f"  {e}" for e in extra))


if __name__ == "__main__":
    main()
