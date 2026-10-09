"""The game's files on the disc image (disks/us/dcb_us.bin: one MODE2/2352 track, ISO 9660) and the PAK chunks the sound
code loads: the oracle's traces name the data LIBSND was given by its SHA-1 (tests/sound/spu_trace.py), and the replay
(tests/port/sound.py) finds those bytes here. Nothing is extracted into the tree: the bytes are read from the image.

A PAK (src/main/system/archive.c findPakChunk) is a list of chunks, each an 8-byte header (s16 id, s16 sub, s32 size)
and `size` bytes, ended by a negative id. The sound PAKs (src/main/system/sound.c): A:\\SE<n>.PAK (the effects' VAB:
chunks 7 and 8) and A:\\BGM\\BGM<nn>.PAK (a track: the SEQ 6, the VAB header 7 and body 8)."""
import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGE = ROOT / "disks/us/dcb_us.bin"
SECTOR, USER = 2352, 24        # raw sector size; the user data's offset in a Mode 2 Form 1 sector


class Disc:
    def __init__(self, image=IMAGE):
        self.f = open(image, "rb")
        self.files = {}           # "DIR/NAME.EXT" (upper case, no version) -> (lba, size)
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError(f"{image}: no ISO 9660 volume descriptor")
        root = pvd[156:156 + 34]
        self._walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")

    def sector(self, lba):
        self.f.seek(lba * SECTOR + USER)
        return self.f.read(2048)

    def read_extent(self, lba, size):
        out = bytearray()
        for i in range((size + 2047) // 2048):
            out += self.sector(lba + i)
        return bytes(out[:size])

    def _walk(self, lba, size, prefix):
        data = self.read_extent(lba, size)
        pos = 0
        while pos < len(data):
            n = data[pos]
            if n == 0:                       # records do not cross sectors: the rest of this one is padding
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = data[pos:pos + n]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            if name not in (b"\x00", b"\x01"):
                name = name.decode("ascii").split(";")[0]
                e_lba, e_size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
                if rec[25] & 2:
                    self._walk(e_lba, e_size, prefix + name + "/")
                else:
                    self.files[prefix + name] = (e_lba, e_size)
            pos += n

    def read(self, name):
        lba, size = self.files[name.upper()]
        return self.read_extent(lba, size)


def pak_chunks(data):
    """[(id, sub, offset of the data, size)] of a PAK."""
    out, pos = [], 0
    while pos + 8 <= len(data):
        cid, sub, size = struct.unpack_from("<hhi", data, pos)
        if cid < 0:
            break
        out.append((cid, sub, pos + 8, size))
        pos += 8 + size
    return out


def drive_files(drive):
    """{"DIR/NAME": bytes} of a drive file (X.DRV, the game's drive X:, src/main/system/cd_file.c openDiscFile; upstream's
    tools/extract_drv.py): a directory of 32-byte entries (s32 key, s32 sector, s32 size, 4 bytes, the name in 16),
    sectors counted from the drive's start; key 0x80 is a subdirectory at that sector, whose entries may run on into
    the sectors after it (up to the first file's)."""
    out = {}

    def walk(sector, prefix):
        entries, s = [], sector
        while True:
            block = drive[s * 2048:(s + 1) * 2048]
            got = [struct.unpack_from("<iii", block, i) + (block[i + 16:i + 32].split(b"\0")[0].decode("ascii"),)
                   for i in range(0, 2048, 32) if block[i + 16]]
            if not got:
                break
            entries += got
            first_file = min((e[1] for e in entries if e[0] != 0x80), default=s + 1)
            s += 1
            if s >= first_file or len(got) < 64:
                break
        for key, sec, size, name in entries:
            if key == 0x80:
                walk(sec, prefix + name + "/")
            else:
                out[prefix + name] = drive[sec * 2048:sec * 2048 + size]

    walk(0, "")
    return out


def sound_chunks(disc):
    """{sha1: (file, offset, size, id, sub, bytes)} of the chunks of the sound PAKs on drive A: (SE<n>, BGM/BGM<nn>);
    `bytes` runs from the chunk to the file's end: LIBSND's body transfer sends whole 64-byte blocks, so it reads past
    a VAB body's chunk into what follows it in the PAK, as the game has the whole PAK in RAM."""
    index = {}
    for name, data in sorted(drive_files(disc.read("A.DRV")).items()):
        if not (name.startswith("SE") or name.startswith("BGM/")):
            continue
        for cid, sub, off, size in pak_chunks(data):
            index.setdefault(hashlib.sha1(data[off:off + size]).hexdigest(),
                             (f"A:{name}.PAK", off, size, cid, sub, data[off:]))
    return index
