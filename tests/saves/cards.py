#!/usr/bin/env python3
"""Format checks of memory card images (.mcd) holding this game's save (docs/PORT.md "Saves"). Adapted from
dw2003recomp's tests/saves/cards.py (docs/THIRD_PARTY.md).

Usage: tests/saves/cards.py CARD.mcd [OTHER.mcd]
  one card:  its directory (frame checksums, File 1's two blocks, every other block free), the save file's Sony header
             (the icon's three frames, the title), and the saved profile: its size (the PS1's 0x2774), its two
             checksums (src/main/system/save_checksum.c: the XOR and the sum of the 0x2774 bytes, in the two after
             them), and its card pointers (heap addresses, 4 bytes: the PS1's layout, not the host's)
  two cards: both as above, then byte for byte equal except MASKS (each named, with its reason)
tests/saves/run.py calls check_pair on the port's and the emulator's card after their saves. Exit 0 pass, 1 fail.

The save (src/openseg/memcard/open_save.c): OPEN_buildSaveHeader's 0x200 bytes (the Sony header, 'SC', type 0x13:
three icon frames; its CLUT and the frames from VRAM), then the card buffer (OPEN_MEMCARD.buffer, 0x4000 bytes on the
heap): the player's PlayerProfile (include/game.h, 0x2774 bytes) and its checksums (writeSaveChecksum), then whatever the
buffer held, to the file's end (stepMemoryCardSave writes (2 * 0x2000 - 0x200) / 0x80 frames). The file is
"BASLUS-01328_A" for File 1 (_B, _C: Files 2 and 3), two blocks.
"""
import sys
from functools import reduce
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/replay"))
from replay import VOLATILE_RANGES  # noqa: E402

CARD_SIZE = 0x20000
BLOCK, FRAME = 0x2000, 0x80
FILE_NAME = b"BASLUS-01328_A"            # OPEN_SAVE_FILE_NAMES[0]: File 1 (us)
FILE_BLOCKS = 2                          # startMemoryCardSave(port, 2, ...)
DATA_AT = 0x200                          # the Sony header and the three icon frames, then the card buffer
PROFILE_SIZE = 0x2774                    # PlayerProfile (us), what the checksums cover
TITLE_AT, TITLE_PREFIX = 0x04, "ＤＣＢ［１］".encode("shift_jis")   # "ＤＣＢ［File］hours：minutes"
TITLE_TIME = (0x10, 0x1C)                # the title's hours and minutes (OPEN_buildSaveHeader: 3 and 2 SJIS digits)
# The game's heap, where the profile's card pointers point (src/main/system/heap.c: HEAP_ARENA_ADDR, HEAP_SIZE).
HEAP_LO, HEAP_HI = 0x8008C848, 0x8008C848 + 0x148000
# The profile's pointer fields (docs/PORT.md "Memory and pointers"): three partners (0x80, 0x298 bytes each: baseCard
# at +0x278, armorCard at +0x27C; cardId at +0x288) and three saved decks (0x2438, 0x110 bytes each: inUse at +0,
# thirty CardSlots of 8 bytes from +0x14, the card pointer at +4).
PARTNERS_AT, PARTNER_SIZE = 0x80, 0x298
DECKS_AT, DECK_SIZE = 0x2438, 0x110


def masks(file_at):
    """(start, end, name, reason) of every card range two saves of the same game state may differ in, for a save file
    at card offset file_at."""
    data = file_at + DATA_AT
    ms = [(file_at + TITLE_AT + TITLE_TIME[0], file_at + TITLE_AT + TITLE_TIME[1], "the title's play time",
           "hours and minutes at the save (frames since boot: the port's CD and movie timing differ from the "
           "emulator's)")]
    for lo, hi in VOLATILE_RANGES:
        ms.append((data + lo, data + hi, f"profile {lo:#x}..{hi:#x}",
                   "tests/replay/replay.py VOLATILE_RANGES: the profile id, the play time and the cards' serials "
                   "(rand()), the starter's bonus cards (rand() % 2), the heap the game never writes"))
    ms += [(data + PROFILE_SIZE, data + PROFILE_SIZE + 2, "the profile's checksums",
            "the XOR and the sum of the profile, which holds the fields above"),
           (data + PROFILE_SIZE + 2, file_at + FILE_BLOCKS * BLOCK, "the buffer's tail",
            "the card buffer after the profile: a heap block the game never clears (stale RAM), written whole")]
    return ms


def xor(data):
    return reduce(lambda a, b: a ^ b, data, 0)


def u16(c, at):
    return int.from_bytes(c[at:at + 2], "little")


def u32(c, at):
    return int.from_bytes(c[at:at + 4], "little")


def pointer_fields(profile):
    """(offset, name) of the profile's card pointers that the game set: the partners with a card (cardId != 0) and the
    slots of the saved decks in use; the others hold the heap's stale bytes."""
    fields = []
    for i in range(3):
        p = PARTNERS_AT + PARTNER_SIZE * i
        if profile[p + 0x288] != 0:
            fields += [(p + 0x278, f"partners[{i}].baseCard"), (p + 0x27C, f"partners[{i}].armorCard")]
        d = DECKS_AT + DECK_SIZE * i
        if profile[d] != 0:
            fields += [(d + 0x14 + 8 * j + 4, f"savedDecks[{i}].cards[{j}].card") for j in range(30)]
    return fields


def check_card(c, name):
    """The format checks of one card; returns (failures, the save file's card offset or None, a summary)."""
    if len(c) != CARD_SIZE:
        return [f"{name}: {len(c)} bytes, not a raw 128 KB card image"], None, ""
    fail = []
    if c[0:2] != b"MC":
        fail.append(f"{name}: directory frame 0 does not start with 'MC'")
    bad = [i for i in range(63) if xor(c[i * FRAME:i * FRAME + FRAME - 1]) != c[i * FRAME + FRAME - 1]]
    if bad:
        fail.append(f"{name}: directory frame checksum (byte 0x7F) wrong in frame(s) {bad}")
    entries = [c[i * FRAME:(i + 1) * FRAME] for i in range(1, 16)]
    first = [i for i, e in enumerate(entries) if e[0] == 0x51]
    files = [i for i in first if entries[i][0x0A:0x0A + len(FILE_NAME) + 1] == FILE_NAME + b"\0"]
    if len(files) != 1 or len(first) != 1:
        fail.append(f"{name}: {len(files)} file(s) named {FILE_NAME.decode()} and {len(first)} file(s) in all in "
                    "the directory (expected the save alone)")
        return fail, None, ""
    block, chain = files[0], []
    if u32(entries[block], 4) != FILE_BLOCKS * BLOCK:
        fail.append(f"{name}: the save file's size is {u32(entries[block], 4):#x}, not {FILE_BLOCKS * BLOCK:#x}")
    while block is not None and len(chain) <= 15:
        chain.append(block)
        nxt = u16(entries[block], 8)
        block = None if nxt == 0xFFFF else nxt
    states = [entries[b][0] for b in chain]
    want = [0x51] + [0x52] * (FILE_BLOCKS - 2) + [0x53]
    if states != want:
        fail.append(f"{name}: the save file's blocks {[b + 1 for b in chain]} have states "
                    f"{[hex(s) for s in states]}, not {[hex(s) for s in want]}")
    used = [i for i, e in enumerate(entries) if e[0] != 0xA0]
    if sorted(used) != sorted(chain):
        fail.append(f"{name}: directory entries {[i + 1 for i in used]} in use, the save file is "
                    f"{[b + 1 for b in chain]}")
    if chain != list(range(chain[0], chain[0] + FILE_BLOCKS)):
        fail.append(f"{name}: the save file's blocks {[b + 1 for b in chain]} are not contiguous")
        return fail, None, ""
    at = (chain[0] + 1) * BLOCK
    f = c[at:at + FILE_BLOCKS * BLOCK]
    if f[0:2] != b"SC" or f[2] != 0x13 or f[3] != FILE_BLOCKS:
        fail.append(f"{name}: Sony header {f[0:4].hex()}, not 'SC', 0x13 (three icon frames), {FILE_BLOCKS} blocks")
    if not f[TITLE_AT:].startswith(TITLE_PREFIX):
        fail.append(f"{name}: the title {f[TITLE_AT:TITLE_AT + 0x40].split(b'\0')[0]!r} is not File 1's")
    profile = f[DATA_AT:DATA_AT + PROFILE_SIZE]
    size = u16(profile, 0x16)
    if size != PROFILE_SIZE:
        fail.append(f"{name}: the profile's size (profileSize, +0x16) is {size:#x}, not the PS1's {PROFILE_SIZE:#x} "
                    "(the game calls such a file corrupted)")
    sums = f[DATA_AT + PROFILE_SIZE:DATA_AT + PROFILE_SIZE + 2]
    want_sums = bytes((xor(profile), sum(profile) & 0xFF))
    if sums != want_sums:
        fail.append(f"{name}: the profile's checksums {sums.hex()}, the XOR and sum of its {PROFILE_SIZE:#x} bytes are "
                    f"{want_sums.hex()}")
    ptrs = pointer_fields(profile)
    wrong = [(n, u32(profile, o)) for o, n in ptrs if not HEAP_LO <= u32(profile, o) < HEAP_HI]
    if not ptrs:
        fail.append(f"{name}: no partner and no saved deck in the profile")
    for n, v in wrong[:6]:
        fail.append(f"{name}: {n} is {v:#010x}, not a PS1 heap address")
    nm = profile[:0xD].split(b"\0")[0]
    return fail, at, (f"{FILE_NAME.decode()} at {at:#x}, the profile of {nm.decode(errors='replace')!r}: size "
                      f"{size:#x}, checksums {sums.hex()}, {len(ptrs) - len(wrong)} of {len(ptrs)} card pointers "
                      "heap addresses")


def describe(off, file_at):
    """Where a card offset is, in words."""
    if off < BLOCK:
        return f"directory frame {off // FRAME} +{off % FRAME:#x}"
    if file_at is None or not file_at <= off < file_at + FILE_BLOCKS * BLOCK:
        return f"block {off // BLOCK} +{off % BLOCK:#x} (no file)"
    o = off - file_at
    if o < FRAME:
        return f"save file Sony header +{o:#x}"
    if o < DATA_AT:
        return f"save file icon frame {o // FRAME - 1} +{o % FRAME:#x}"
    return f"profile +{o - DATA_AT:#x}" if o - DATA_AT < PROFILE_SIZE else f"the buffer +{o - DATA_AT:#x}"


def check_pair(a, b, name_a, name_b, indent=""):
    """Both cards' format checks, then byte equality outside masks(); prints one line per check, returns failures."""
    fa, at_a, sa = check_card(a, name_a)
    fb, at_b, sb = check_card(b, name_b)
    failures = fa + fb
    for n, f, s in ((name_a, fa, sa), (name_b, fb, sb)):
        print(f"{indent}{n}: {'ok' if not f else 'FAIL'} ({s or 'no save file'})")
    if at_a is None or at_b is None:
        return failures
    if at_a != at_b:
        return failures + [f"the save file is at {at_a:#x} on {name_a}, at {at_b:#x} on {name_b}"]
    ms = masks(at_a)
    masked = bytearray(CARD_SIZE)
    for lo, hi, _, _ in ms:
        masked[lo:hi] = b"\1" * (hi - lo)
    diff = [i for i in range(CARD_SIZE) if a[i] != b[i] and not masked[i]]
    if diff:
        runs = []
        for i in diff:
            if runs and i == runs[-1][1]:
                runs[-1][1] = i + 1
            else:
                runs.append([i, i + 1])
        for lo, hi in runs[:8]:
            failures.append(f"{name_a} vs {name_b}: {hi - lo} byte(s) differ at card {lo:#07x} "
                            f"({describe(lo, at_a)}): {a[lo:min(hi, lo + 16)].hex()} vs {b[lo:min(hi, lo + 16)].hex()}")
        if len(runs) > 8:
            failures.append(f"{name_a} vs {name_b}: ... {len(runs) - 8} more differing range(s)")
    differing = sum(1 for lo, hi, _, _ in ms if a[lo:hi] != b[lo:hi])
    print(f"{indent}{name_a} vs {name_b}: equal byte for byte but the {len(ms)} masked ranges: "
          f"{'ok' if not diff else f'FAIL ({len(diff)} bytes)'} ({differing} of the masked ranges differ)")
    return failures


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    cards = [Path(p) for p in sys.argv[1:]]
    if len(cards) == 1:
        failures, _, summary = check_card(cards[0].read_bytes(), cards[0].name)
        if not failures:
            print(f"{cards[0].name}: ok ({summary})")
    else:
        failures = check_pair(cards[0].read_bytes(), cards[1].read_bytes(), cards[0].name, cards[1].name)
    for f in failures:
        print("FAIL: " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
