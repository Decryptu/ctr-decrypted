"""Read files from a CTR (USA) disc image: the ISO 9660 tree and BIGFILE.BIG.

Raw 2352-byte Mode 2 Form 1 sectors (the .bin of a .cue). BIGFILE.BIG starts
with (cdpos, numEntry), then numEntry (offset in sectors, size in bytes)
pairs; offsets are relative to the BIGFILE's first sector
(include/namespace_Load.h).
"""
import struct

RAW_SECTOR = 2352
FORM1_OFFSET = 24
FORM1_SIZE = 2048

# BIGFILE groups (include/namespace_Load.h, enum BigIndex).
BI_LANGUAGEFILE = 234
BI_RACERMODELHI = 242
BI_SHAREDMPKVRM = 258
BI_1PARCADEPACK = 260
BI_ADVENTUREPACK = 276
BI_TIMETRIALPACK = 292
BI_RACERMODELMED = 308
BI_2PARCADEPACK = 324
BI_RACERMODELLOW = 332
BI_4PARCADEPACK = 348
BI_PODIUMVRMS = 364
BI_DANCEMODELWIN = 366
BI_DANCEMODELLOSE = 398
BI_MAINMENUFILE = 214

GROUPS = [
    (0, 144, "arcade track"),
    (144, 200, "battle track"),
    (200, 215, "adventure hub"),
    (214, 220, "main menu"),
    (220, 234, "overlay"),
    (234, 242, "language"),
    (242, 258, "racer model hi"),
    (258, 260, "shared vrm"),
    (260, 276, "1p arcade pack"),
    (276, 292, "adventure pack"),
    (292, 308, "time trial pack"),
    (308, 324, "racer model med"),
    (324, 332, "2p arcade pack"),
    (332, 348, "racer model low"),
    (348, 364, "4p arcade pack"),
    (364, 366, "podium vrm"),
    (366, 398, "dance win"),
    (398, 430, "dance lose"),
]

CHARACTER_NAMES = (
    "Crash Bandicoot", "Dr. Neo Cortex", "Tiny Tiger", "Coco Bandicoot", "N. Gin", "Dingodile", "Polar", "Pura",
    "Pinstripe", "Papu Papu", "Ripper Roo", "Komodo Joe", "N. Tropy", "Penta Penguin", "Fake Crash", "N. Oxide",
)


def group_name(index):
    for lo, hi, name in GROUPS:
        if lo <= index < hi:
            if name in ("racer model hi", "racer model med", "racer model low", "1p arcade pack", "adventure pack",
                        "time trial pack", "4p arcade pack"):
                return f"{name} [{CHARACTER_NAMES[index - lo]}]"
            if name in ("dance win", "dance lose"):
                return f"{name} [{CHARACTER_NAMES[(index - lo) // 2]}]"
            return f"{name} +{index - lo}"
    return "other"


class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.files = {}
        root = self._read_sector(16)
        self._walk(root[156:156 + 34], "")

    def close(self):
        self.f.close()

    def _read_sector(self, lba):
        self.f.seek(lba * RAW_SECTOR + FORM1_OFFSET)
        return self.f.read(FORM1_SIZE)

    def read(self, lba, size):
        out = bytearray()
        while len(out) < size:
            out += self._read_sector(lba)
            lba += 1
        return bytes(out[:size])

    def _walk(self, record, prefix):
        lba, size = struct.unpack_from("<I", record, 2)[0], struct.unpack_from("<I", record, 10)[0]
        data = self.read(lba, size)
        pos = 0
        while pos < len(data):
            length = data[pos]
            if length == 0:
                pos = (pos // FORM1_SIZE + 1) * FORM1_SIZE
                continue
            rec = data[pos:pos + length]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            flags = rec[25]
            if name not in (b"\x00", b"\x01"):
                name = name.decode("ascii").split(";")[0]
                child_lba = struct.unpack_from("<I", rec, 2)[0]
                child_size = struct.unpack_from("<I", rec, 10)[0]
                if flags & 2:
                    self._walk(rec, prefix + name + "/")
                else:
                    self.files[prefix + name] = (child_lba, child_size)
            pos += length

    def file(self, name):
        lba, size = self.files[name]
        return self.read(lba, size)


class BigFile:
    def __init__(self, disc):
        self.disc = disc
        self.lba, self.size = disc.files["BIGFILE.BIG"]
        head = disc.read(self.lba, 8)
        self.count = struct.unpack_from("<I", head, 4)[0]
        table = disc.read(self.lba, 8 + 8 * self.count)
        self.entries = [struct.unpack_from("<II", table, 8 + 8 * i) for i in range(self.count)]

    def read(self, index):
        offset, size = self.entries[index]
        return self.disc.read(self.lba + offset, size)
