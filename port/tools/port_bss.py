#!/usr/bin/env python3
"""The executable's game .bss as C, for the PC port (docs/PORT.md "The executable's game .bss"). port/CMakeLists.txt
runs it at configure time through port_inputs.py --bss, which lists the file as a MAIN unit; port_inventory.py
writes its own copy for the probe and the link check.

  port/tools/port_bss.py --out build/port/gen/bss_standins.c [--version us] [-v]   # -v: the labels, one per line

The matching build keeps the executable's zeroed data as splat `bss` segments (config/<version>/main.yaml): `game`,
then `psyq` up to the end of the executable's .bss (the segment's bss_size). The C declares the game's labels
(`extern` in include/ or in the units) and never defines them. On the host each needs a definition in a MAIN unit,
whose .bss the compile launcher (psxstack's port_gen.py rename) puts in the executable's overlay section, which the
console reset and the save states snapshot. Everything comes from tracked files, never from asm/ or the disc:

  the labels     every symbol of config/<version>/symbols.txt in the executable's .bss, except the symbol file's
                 Psy-Q section (the shim owns the SDK's state), in address order. The `psyq` segment starts with
                 Psy-Q data and holds more game labels after it (psylink's object order); both are covered. splat's
                 own D_<address> labels are not in the symbol file: their bytes are the preceding label's, unless
                 the tracked C declares one (src/main/psyq: a Psy-Q boundary; a unit: a game label)
  the sizes      each label's PS1 bytes: up to the next label of anyone, the Psy-Q's included, or to its `size:`
                 when that covers the next labels (those are inside it: kept apart on the host, see below). The
                 `game` segment's sizes add up to the segment
  the types      the label's `extern` declaration: in include/**/*.h (the file includes that header) or else in a
                 unit the version builds (the declaration is copied). The definition takes __typeof__ of the
                 declaration, so a type change in the C needs no new run
  the views      `(Type *)&NAME` casts in the units: the C reads the label as a larger object than it declares
                 (`extern s32 GRAPHICS;` read as a Graphics); the storage holds the largest of them

Each label is an alias of a union of its declared type, its views and its PS1 bytes: never smaller than the PS1
object, nor than the host's size of any type the C reads it as (a type that grows on 64-bit grows). A label no C
declares (only the hand-written asm uses it) is a byte array. A label inside another one's `size:` is a separate
object of its declared type: the host cannot alias a field whose offset may differ, and the C reaching the same
bytes by both names is a PS1 alias the host does not keep (docs/PORT.md). A label a header #defines, or a unit
defines, is left to the C.
"""
import argparse
import re
import sys
from pathlib import Path

import port_inputs as inputs

ROOT = inputs.ROOT
SEGMENT = "game"            # main.yaml's bss segment of the game's zeroed data; the executable's .bss starts there
PSYQ_SECTION = re.compile(r"^//.*\bPsyQ\b")   # the symbol file's section of the SDK's names
# PS1 sizes of the scalar types the declarations use, for an incomplete array's element count (`extern T *X[];`)
PS1_SIZES = {"char": 1, "s8": 1, "u8": 1, "short": 2, "s16": 2, "u16": 2, "int": 4, "long": 4, "s32": 4, "u32": 4,
             "s32p": 4, "u32p": 4, "float": 4, "double": 8, "s64": 8, "u64": 8}
SCALARS = set(PS1_SIZES) | {"void", "signed", "unsigned", "u_char", "u_short", "u_int", "u_long"}
QUALIFIERS = {"extern", "const", "volatile", "struct", "union", "enum"}

# The generated file's macros: the storage is a union named port_bss_<label>, the label an alias of it.
MACROS = r"""
#define PORT_BSS_STORAGE(name, member, ps1, views) \
    static union { member; views u8 ps1_bytes[ps1]; } port_bss_##name __attribute__((aligned(8)))
#define PORT_BSS_CHECK(name, ps1) \
    _Static_assert(sizeof(port_bss_##name) >= (ps1) && sizeof(port_bss_##name) >= sizeof(name), #name); \
    PORT_BSS_FITS(name, ps1)
#define PORT_BSS(name, ps1, views) \
    PORT_BSS_STORAGE(name, __typeof__(name) host, ps1, views); \
    extern __typeof__(name) name __attribute__((alias("port_bss_" #name))); \
    PORT_BSS_CHECK(name, ps1)
#define PORT_BSS_N(name, count, ps1, views) \
    PORT_BSS_STORAGE(name, __typeof__(name[0]) host[count], ps1, views); \
    extern __typeof__(name[0]) name[count] __attribute__((alias("port_bss_" #name))); \
    PORT_BSS_CHECK(name, ps1)
#define PORT_BSS_APART(name) __typeof__(name) name
#define PORT_BSS_BYTES(name, ps1) u8 name[ps1] __attribute__((aligned(8)))
#if defined(PORT_BSS_CHECK_PS1) && __SIZEOF_POINTER__ == 4
#define PORT_BSS_FITS(name, ps1) _Static_assert(sizeof(name) <= (ps1), #name " is larger than its PS1 bytes")
#else
#define PORT_BSS_FITS(name, ps1) _Static_assert(1, "")
#endif
"""


# ---------------------------------------------------------------------------------------------------------------------
# The tracked sources

def strip_comments(text):
    """The text with its comments blanked (strings and newlines kept: the line numbers stay)."""
    def blank(m):
        s = m.group(0)
        return s if s[0] == '"' else re.sub(r"[^\n]", " ", s)
    return re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\\n])*\"", blank, text, flags=re.S)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def bss_bounds(version):
    """(start, game_end, end): the executable's .bss starts at the vram of main.yaml's `bss` segment SEGMENT, which
    ends at the next segment's vram; the .bss ends bss_size after its start."""
    path = ROOT / "config" / version / "main.yaml"
    text = path.read_text()
    size = re.search(r"^\s*bss_size:\s*(0x[0-9A-Fa-f]+|\d+)", text, flags=re.M)
    entries = [(m.group(0), int(m.group(1), 16)) for m in
               re.finditer(r"\{[^}\n]*\bvram:\s*(0x[0-9A-Fa-f]+)[^}\n]*\}", text)]
    for i, (entry, vram) in enumerate(entries):
        if re.search(r"\btype:\s*bss\b", entry) and re.search(rf"\bname:\s*{SEGMENT}\b", entry):
            if not size or i + 1 == len(entries):
                sys.exit(f"port_bss: {path}: no bss_size, or the {SEGMENT} segment is the last one")
            return vram, entries[i + 1][1], vram + int(size.group(1), 0)
    sys.exit(f"port_bss: {path}: no bss segment named {SEGMENT}")


def symbol_labels(version, start, end):
    """{name: (address, size attribute or None, in the Psy-Q section)} of the symbol file's labels in [start, end)."""
    out, psyq = {}, False
    path = ROOT / "config" / version / "symbols.txt"
    for ln in path.read_text().splitlines():
        if ln.startswith("//"):
            psyq = bool(PSYQ_SECTION.match(ln))
            continue
        m = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;(.*)", ln)
        if m and start <= int(m.group(2), 16) < end:
            s = re.search(r"\bsize:(0x[0-9A-Fa-f]+|\d+)", m.group(3))
            out[m.group(1)] = (int(m.group(2), 16), int(s.group(1), 0) if s else None, psyq)
    return out


def declared_d_labels(paths, start, end):
    """{name: address} of the D_<address> names in [start, end) the files declare `extern`."""
    out = {}
    for path in paths:
        text = strip_comments(path.read_text(errors="replace"))
        for m in re.finditer(r"\bextern\b[^;{}]*?\b(D_([0-9A-Fa-f]{8}))\b", text):
            if start <= int(m.group(2), 16) < end:
                out[m.group(1)] = int(m.group(2), 16)
    return out


def declarators(stmt):
    """[(name, its declarator's text, an array without a size)] of an `extern ...` statement without its `;`."""
    stmt = re.sub(r"^\s*extern\s+", "", stmt)
    parts, depth, cur = [], 0, ""
    for ch in stmt:
        depth += ch in "([{"
        depth -= ch in ")]}"
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    out = []
    for p in parts:
        m = (re.search(r"\(\s*\*\s*(\w+)\s*((?:\[[^\]]*\]\s*)*)\)", p) or
             re.search(r"(\w+)\s*((?:\[[^\]]*\]\s*)*)$", p.strip()))
        if m:
            out.append((m.group(1), p, re.match(r"\[\s*\]", m.group(2) or "") is not None))
    return out


def scan_externs(paths, wanted):
    """{name: (path, line, statement, an array without a size)} of the first `extern` declaration of each wanted
    name in the files (data only: a prototype is skipped)."""
    found = {}
    for path in paths:
        text = strip_comments(path.read_text(errors="replace"))
        for m in re.finditer(r"\bextern\b[^;{}]*;", text):
            stmt = m.group(0)[:-1]
            if "(" in stmt and not re.search(r"\(\s*\*", stmt):
                continue
            for name, _, incomplete in declarators(stmt):
                if name in wanted and name not in found:
                    found[name] = (path, line_of(text, m.start()), " ".join(stmt.split()), incomplete)
    return found


def scan_macros(paths, wanted):
    """{name: path} of the wanted names a header #defines (the C maps the label itself)."""
    found = {}
    for path in paths:
        for m in re.finditer(r"^\s*#\s*define\s+(\w+)\b", path.read_text(errors="replace"), flags=re.M):
            if m.group(1) in wanted:
                found.setdefault(m.group(1), path)
    return found


def scan_definitions(paths, wanted):
    """{name: path} of the wanted names a unit defines at file scope (a line that starts with a type, no extern)."""
    found = {}
    for path in paths:
        text = strip_comments(path.read_text(errors="replace"))
        for m in re.finditer(r"^(?!extern\b|typedef\b|return\b|static\b)[A-Za-z_][\w \t\*]*?\b(\w+)\s*"
                             r"(?:\[[^\]]*\]\s*)*(?:=|;)", text, flags=re.M):
            if m.group(1) in wanted:
                found.setdefault(m.group(1), path)
    return found


def scan_views(paths, wanted):
    """{name: {type: first path:line}} of the `(Type *)&NAME` casts (not `&NAME->`, `&NAME.`, `&NAME[`)."""
    found = {}
    for path in paths:
        text = strip_comments(path.read_text(errors="replace"))
        for m in re.finditer(r"\(\s*(\w+)\s*\*\s*\)\s*&\s*(\w+)\b(?!\s*(?:->|\.|\[))", text):
            t, name = m.group(1), m.group(2)
            if name in wanted and t not in SCALARS:
                found.setdefault(name, {}).setdefault(t, f"{path.relative_to(ROOT)}:{line_of(text, m.start())}")
    return found


def typedef_headers(headers):
    """{type name: header} of the typedefs the headers define (`} Name;` closing a typedef, or `typedef ... Name;`)."""
    found = {}
    for path in headers:
        text = strip_comments(path.read_text(errors="replace"))
        for m in re.finditer(r"\}\s*(\w+)\s*;|\btypedef\b[^;{}]*?\b(\w+)\s*;", text):
            found.setdefault(m.group(1) or m.group(2), path)
    return found


def element_ps1_size(decl, name):
    """The PS1 size of an incomplete array's element from the declaration's text, or None (not a scalar or pointer)."""
    before = decl.split(name)[0]
    if "*" in before:
        return 4
    words = [w for w in re.findall(r"\w+", before) if w not in ("extern", "const", "volatile", "signed", "unsigned")]
    return PS1_SIZES.get(words[-1]) if words else None


# ---------------------------------------------------------------------------------------------------------------------
# The model

def layout(marks, start, game_end, end):
    """marks: [(address, name, size attribute, is the game's)] of every label in the .bss. -> [dict] per game label:
    addr, name, attr, size (its PS1 bytes), inside (the label whose size: covers it, or None), tail (past the
    `game` segment). The segment boundaries count as labels of no one: no game label runs over one."""
    marks = sorted(marks + [(game_end, None, None, False), (end, None, None, False)], key=lambda m: m[0])
    for a, b in zip(marks, marks[1:]):
        if a[0] == b[0] and a[1] and b[1]:
            sys.exit(f"port_bss: {a[1]} and {b[1]} share the address 0x{a[0]:08X}")
    rows, container = [], None
    for i, (addr, name, attr, game) in enumerate(marks):
        if container and addr < container["addr"] + container["attr"]:
            if game:
                rows.append(dict(addr=addr, name=name, attr=attr, size=0, inside=container, tail=addr >= game_end))
            continue
        container = None
        if not game:
            continue
        nxt = next((a for a, *_ in marks[i + 1:] if a >= addr + (attr or 1)), end)
        if attr and addr + attr > nxt:
            sys.exit(f"port_bss: {name}: its size:0x{attr:X} runs over 0x{nxt:08X}")
        row = dict(addr=addr, name=name, attr=attr, size=nxt - addr, inside=None, tail=addr >= game_end)
        rows.append(row)
        if attr and any(a < addr + attr for a, *_ in marks[i + 1:]):
            container = row
    return rows


def model(version):
    """-> (rows, (start, game_end, end), the headers to include). Each row: the label's layout, decl (path, line,
    statement, incomplete), from_unit, skip (why the C keeps it), views [(type, where)], elem (PS1 element size)."""
    start, game_end, end = bss_bounds(version)
    mk = inputs.mk_variables(version)
    units = [ROOT / u for u, _ in inputs.units(mk)]
    psyq_c = sorted((ROOT / "src" / "main" / "psyq").glob("*.c"))
    syms = symbol_labels(version, start, end)
    d_game = {n: a for n, a in declared_d_labels(units, start, end).items() if n not in syms}
    d_psyq = {n: a for n, a in declared_d_labels(psyq_c, start, end).items() if n not in syms and n not in d_game}
    marks = ([(a, n, s, not psyq) for n, (a, s, psyq) in syms.items()] + [(a, n, None, True) for n, a in d_game.items()]
             + [(a, n, None, False) for n, a in d_psyq.items()])
    rows = layout(marks, start, game_end, end)
    if not rows or rows[0]["addr"] != start:
        sys.exit(f"port_bss: no game label at the start of the .bss, 0x{start:08X}")
    names = {r["name"] for r in rows}
    headers = sorted((ROOT / "include").rglob("*.h"))
    in_headers = scan_externs(headers, names)
    in_units = scan_externs(units, names - set(in_headers))
    macros = scan_macros(headers, names)
    defined = scan_definitions(units, names)
    views = scan_views(units, names)
    typedefs = typedef_headers(headers)
    include = {ROOT / "include" / "game.h"}
    for r in rows:
        n = r["name"]
        r["decl"] = in_headers.get(n) or in_units.get(n)
        r["from_unit"] = n not in in_headers and n in in_units
        r["skip"] = (f"#defined in {macros[n].relative_to(ROOT)}" if n in macros else
                     f"defined in {defined[n].relative_to(ROOT)}" if n in defined else None)
        r["views"] = [(t, w) for t, w in sorted(views.get(n, {}).items()) if t in typedefs]
        include.update(typedefs[t] for t, _ in r["views"])
        if r["decl"] and not r["from_unit"] and not r["skip"]:
            include.add(r["decl"][0])
        r["elem"] = element_ps1_size(r["decl"][2], n) if r["decl"] and r["decl"][3] else None
        r["own_type"] = None
        if r["from_unit"]:
            words = re.findall(r"\w+", r["decl"][2].split(n)[0])
            unknown = [w for w in words if w not in SCALARS and w not in typedefs and w not in QUALIFIERS]
            if unknown:
                # a type the unit defines for itself: a pointer stays a pointer (void *), anything else is bytes
                r["own_type"] = unknown[0]
                pointer = "*" in r["decl"][2].split(n)[0]
                r["decl"] = (r["decl"][0], r["decl"][1], f"extern void *{n}", False) if pointer else None
    # A label a header #defines is a field of the label before it (issue #11): its bytes are that label's.
    prev = None
    for r in rows:
        if r["inside"]:
            continue
        if r["skip"] and r["skip"].startswith("#defined") and prev and prev["addr"] + prev["size"] == r["addr"] \
                and r["tail"] == prev["tail"]:
            prev["size"] += r["size"]
            r["size"] = 0
            continue
        prev = r
    return rows, (start, game_end, end), include


# ---------------------------------------------------------------------------------------------------------------------
# The file

def c_text(version, rows, bounds, include):
    start, game_end, end = bounds
    rel = lambda p: p.relative_to(ROOT).as_posix()
    tail = [r for r in rows if r["tail"]]
    lines = [
        f"/* Generated by port/tools/port_bss.py ({version}) from config/{version}/main.yaml, config/{version}/symbols.txt "
        "and the C's",
        f" * extern declarations; do not edit. The executable's game .bss: the `{SEGMENT}` segment (0x{start:08X} to "
        f"0x{game_end:08X},",
        f" * {len(rows) - len(tail)} labels) and the game labels after the Psy-Q data that opens the next one "
        f"({len(tail)}, up to 0x{end:08X}),",
        " * defined for the host in a MAIN unit: the compile launcher renames its .bss into the executable's overlay",
        " * section, which the reset and the save states snapshot (docs/PORT.md).",
        " *",
        " * PORT_BSS: the label is an alias of a union of its declared type, the types the C casts it to (the views) and",
        " * its PS1 bytes: never smaller than the PS1 object, nor than any type the host reads it as. PORT_BSS_N completes",
        " * an `extern T X[]` with the PS1 element count. PORT_BSS_APART: a label inside another one's size:, a separate",
        " * object on the host. PORT_BSS_BYTES: a label no C declares (the hand-written asm's). -DPORT_BSS_CHECK_PS1 at",
        " * -m32 checks that each declared type fits its PS1 bytes. */",
    ]
    game_h = ROOT / "include" / "game.h"
    lines += [f'#include "{p.relative_to(ROOT / "include").as_posix()}"' for p in [game_h] + sorted(include - {game_h})]
    unit_decls = [r for r in rows if r["from_unit"] and r["decl"] and not r["skip"]]
    if unit_decls:
        lines += ["", "/* The labels only units declare: their declarations */"]
        lines += [f"{r['decl'][2]}; /* {rel(r['decl'][0])}:{r['decl'][1]}"
                  + (f": its type, {r['own_type']} *, is the unit's own" if r["own_type"] else "") + " */"
                  for r in unit_decls]
    lines += ["", MACROS.strip(), ""]
    for r in rows:
        n = r["name"]
        if tail and r is tail[0]:
            lines.append(f"/* ---- past the {SEGMENT} segment (0x{game_end:08X}): the game labels after Psy-Q's first "
                         "data */")
        where = f" {rel(r['decl'][0])}:{r['decl'][1]}" if r["decl"] else ""
        head = f"/* 0x{r['addr']:08X}"
        if r["skip"]:
            lines.append(f"{head} {n}: {r['skip']}: not here */")
        elif r["inside"]:
            c = r["inside"]
            lines.append(f"{head} {n}: PS1 {c['name']} + 0x{r['addr'] - c['addr']:X} (inside its size:0x{c['attr']:X});"
                         f" apart on the host.{where} */")
            lines.append(f"PORT_BSS_APART({n});" if r["decl"] else f"PORT_BSS_BYTES({n}, 4);")
        elif not r["decl"]:
            why = (f"its type, {r['own_type']}, is its unit's own" if r["own_type"] else
                   "no C declaration (the hand-written asm's)")
            lines.append(f"{head} 0x{r['size']:X}: {why} */")
            lines.append(f"PORT_BSS_BYTES({n}, 0x{r['size']:X});")
        else:
            note = f" size:0x{r['attr']:X}" if r["attr"] else ""
            if r["views"]:
                where += "; read as " + ", ".join(f"{t} ({w})" for t, w in r["views"])
            lines.append(f"{head} 0x{r['size']:X}{note}{where} */")
            views = " ".join(f"{t} view{i};" for i, (t, _) in enumerate(r["views"]))
            if r["decl"][3]:
                count = (f"0x{-(-r['size'] // r['elem']):X}" if r["elem"] else
                         f"((0x{r['size']:X} + sizeof({n}[0]) - 1) / sizeof({n}[0]))")
                lines.append(f"PORT_BSS_N({n}, {count}, 0x{r['size']:X}, {views});")
            else:
                lines.append(f"PORT_BSS({n}, 0x{r['size']:X}, {views});")
    return "\n".join(lines) + "\n"


def generate(version, out, verbose=False):
    """Writes the file (when it changed); -> the one-line summary."""
    rows, bounds, include = model(version)
    start, game_end, end = bounds
    in_game = sum(r["size"] for r in rows if not r["tail"])
    if in_game != game_end - start:
        sys.exit(f"port_bss: the {SEGMENT} segment's labels add up to 0x{in_game:X}, not its 0x{game_end - start:X} bytes")
    inputs.write(out, c_text(version, rows, bounds, include))
    kept = [r for r in rows if not r["skip"]]
    typed = [r for r in kept if r["decl"] and not r["inside"]]
    tail = [r for r in rows if r["tail"]]
    if verbose:
        for r in rows:
            kind = ("skip: " + r["skip"] if r["skip"] else
                    f"inside {r['inside']['name']}" if r["inside"] else
                    f"bytes: {r['own_type']} is the unit's own" if not r["decl"] and r["own_type"] else
                    "bytes (no declaration)" if not r["decl"] else
                    "typed" + (" (a unit's declaration)" if r["from_unit"] else "") +
                    (f" as void *: {r['own_type']} is the unit's own" if r["own_type"] else "") +
                    (f", read as {', '.join(t for t, _ in r['views'])}" if r["views"] else ""))
            print(f"  0x{r['addr']:08X} 0x{r['size']:6X} {r['name']:30} {kind}")
    return (f"port_bss: {version}: {len(rows)} game labels, {len(rows) - len(tail)} in the {SEGMENT} segment "
            f"(0x{start:08X}-0x{game_end:08X}, 0x{in_game:X} bytes) and {len(tail)} after Psy-Q's first data "
            f"(0x{sum(r['size'] for r in tail):X} bytes); {len(typed)} typed ({sum(1 for r in typed if r['from_unit'])} "
            f"from a unit's declaration, {sum(1 for r in typed if r['views'])} widened to a cast's type), "
            f"{sum(1 for r in kept if not r['decl'])} byte arrays, {sum(1 for r in kept if r['inside'])} apart from "
            f"the label they are inside, {len(rows) - len(kept)} left to the C -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the C file to write")
    ap.add_argument("--version", default="us", help="the game version (config/<version>/; default us)")
    ap.add_argument("-v", "--verbose", action="store_true", help="the labels, one per line")
    args = ap.parse_args()
    print(generate(args.version, Path(args.out), args.verbose))


if __name__ == "__main__":
    main()
