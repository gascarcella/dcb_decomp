#!/usr/bin/env python3
"""PC-port inventory for this game: psxstack's tools/port_inventory.py (the probe, the link check, the counts)
configured for this tree (docs/PORT.md "The host-compile probe"). Adapted from dw2003recomp's
tools/port_inventory.py (docs/THIRD_PARTY.md).

  .venv/bin/python port/tools/port_inventory.py counts                 # the inventory's numbers on this tree
  .venv/bin/python port/tools/port_inventory.py counts --sites KIND    # file:line of every site of one kind
  .venv/bin/python port/tools/port_inventory.py probe [FILES...]       # host-compile gate at -m64 (exit 0 = clean)
  .venv/bin/python port/tools/port_inventory.py probe -v               # with the compiler's messages
  .venv/bin/python port/tools/port_inventory.py probe --warnings       # also count the non-gating -Wall warnings
  .venv/bin/python port/tools/port_inventory.py probe --m32            # the same at -m32 (compile only)
  .venv/bin/python port/tools/port_inventory.py link                   # probe, then nm: duplicate / undefined globals

The units are the ones `mk/version/us.mk` builds (MAIN_C_SRC and <OVERLAY>_C_SRC: the tree also holds jp/eu-only
files and the Psy-Q objects' C, which the shim replaces), and the executable's game .bss as C, which
port/tools/port_bss.py writes to build/port_inventory/gen/bss_standins.c first (as the port's configure does to
build/port/gen/): the probe compiles it, and the link check sees the labels it defines. The probe's objects go to
build/port_inventory/m64/, its
override headers to build/port_inventory/include/; psxstack/tools/port_inventory.py's docstring describes the
commands and the site kinds. The stack writes the no-op override of include/gte.h's macros itself (the GTEMAC's
include path, psxstack >= 0.3.0), and the Psy-Q names of config/us/symbols.txt, whose one "PsyQ 4.7 SDK" section
carries no library comments, get their library from psyq_libraries(): the src/main/psyq/<library>_<object>.c
(or .s) that defines each name. PSXSTACK_DIR (the environment) names a checkout of the stack under development;
the default is the submodule.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PSXSTACK = Path(os.environ.get("PSXSTACK_DIR") or ROOT / "psxstack")   # the submodule, or a checkout under development
VERSION = "us"
OUT = ROOT / "build" / "port_inventory"
sys.path.insert(0, str(PSXSTACK / "tools"))
import port_inventory as inv  # noqa: E402
import port_bss  # noqa: E402

BSS = OUT / "gen" / "bss_standins.c"


# ---------------------------------------------------------------------------------------------------------------------
# The units: what mk/version/<version>.mk builds

def version_units(version=VERSION):
    """-> [Path] of every C unit <BINARY>_C_SRC lists, in the .mk's order."""
    text = (ROOT / "mk" / "version" / f"{version}.mk").read_text().replace("\\\n", " ")
    units = []
    for m in re.finditer(r"^(\w+)_C_SRC\s*:=\s*(.*)$", text, flags=re.M):
        body = m.group(2)
        for a in re.finditer(r"\$\(addprefix\s+(\S+),\s*([^)]*)\)", body):
            units += [a.group(1) + f for f in a.group(2).split()]
        units += re.sub(r"\$\(addprefix[^)]*\)", "", body).split()
    return [ROOT / u for u in units]


def probe_units():
    """The version's units, then the game .bss as C (port_bss.py, written now: it follows the tree)."""
    port_bss.generate(VERSION, BSS)
    return version_units() + [BSS]


def module_of(rel):
    """src/main/system/task.c -> main/system; src/main/main.c -> main; src/kawseg/ui/kaw_hud.c -> kawseg/ui; the
    generated .bss -> port_bss."""
    if ROOT / rel == BSS:
        return "port_bss"
    parts = rel.split("/")
    if parts[0] == "include":
        return "include/" + parts[-1]
    return "/".join(parts[1:-1]) if len(parts) > 3 else parts[1]


# ---------------------------------------------------------------------------------------------------------------------
# The derived files

def psyq_definitions():
    """-> {name: (library, object)} for every function or datum a src/main/psyq/<library>_<object>.{c,s} defines."""
    where = {}
    for p in sorted((ROOT / "src" / "main" / "psyq").iterdir()):
        if p.suffix not in (".c", ".s"):
            continue
        lib, _, obj = p.stem.partition("_")
        lib, obj = lib.upper(), obj.upper()
        if p.suffix == ".s":
            names = re.findall(r"^\s*glabel\s+(\w+)", p.read_text(errors="replace"), flags=re.M)
        else:
            text = inv.strip_code(p.read_text(errors="replace"))
            names = re.findall(r"INCLUDE_ASM\([^,]+,\s*(\w+)\s*\)", text)
            # a definition: `type name(args) {` at the start of a line, not a prototype
            names += re.findall(r"^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{}]*\)\s*\{", text, flags=re.M)
            # file-scope data: `type name[...] = ...;` or `type name;` without extern
            for m in re.finditer(r"^(?!\s*(?:extern|typedef|return)\b)[A-Za-z_][\w\s\*]*?\b(\w+)\s*(?:\[[^\]]*\]\s*)*(?:=|;)",
                                 text, flags=re.M):
                names.append(m.group(1))
        for n in names:
            where.setdefault(n, (lib, obj))
    return where


def psyq_libraries():
    """{name: library} for the Psy-Q names the game's symbol files carry without library comments (config/us/symbols.txt
    has one section for the whole SDK): the library of the src/main/psyq object that defines each name."""
    return {name: lib for name, (lib, obj) in psyq_definitions().items()}


# ---------------------------------------------------------------------------------------------------------------------

CFG = inv.configure(
    root=ROOT, game_json=ROOT / "port/game/game.json",
    sources=probe_units,
    headers=lambda: sorted((ROOT / "include").rglob("*.h")),
    include_dirs=[ROOT / "port/include", ROOT / "include", ROOT],
    symbol_files=[ROOT / "config" / VERSION / "symbols.txt"] + sorted((ROOT / "config" / VERSION).glob("symbols_*.txt")),
    module_of=module_of, gtemac=ROOT / "include/gte.h", include_asm=ROOT / "include/include_asm.h",
    port_h=ROOT / "include/port.h", psyq_dir=ROOT / "include", tool_dirs=[],
    defines=["VERSION_US", "SKIP_ASM"], out=OUT, psyq_libraries=psyq_libraries,
    psyq_decl_headers=[ROOT / "include/game.h", ROOT / "include/dcb/evoseg.h"])


def main():
    ap, _ = inv.build_parser()
    return inv.main(ap)


if __name__ == "__main__":
    sys.exit(main())
