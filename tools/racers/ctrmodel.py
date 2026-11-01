"""Read CTR model files (the BIGFILE racer models): headers, texture layouts
and the VRAM they sample.

A model file is a u32 pointer-map offset, then the model, then the pointer
map. Pointers in the model are offsets from the model's start (after the
u32). Struct layouts: include/namespace_Instance.h (Model, ModelHeader) and
include/namespace_Decal.h (TextureLayout).
"""
import struct

MODEL_HEADER_SIZE = 0x40
VRAM_W, VRAM_H = 1024, 512


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Layout:
    __slots__ = ("u", "v", "clut", "tpage", "offset")

    def __init__(self, raw, offset):
        u0, v0, clut, u1, v1, tpage, u2, v2, u3, v3 = struct.unpack("<BBHBBHBBBB", raw)
        self.u = (u0, u1, u2, u3)
        self.v = (v0, v1, v2, v3)
        self.clut = clut
        self.tpage = tpage
        self.offset = offset

    @property
    def bpp(self):
        return (self.tpage >> 7) & 3  # 0: 4-bit, 1: 8-bit, 2: 15-bit

    @property
    def page_x(self):
        return (self.tpage & 0xF) * 64

    @property
    def page_y(self):
        return ((self.tpage >> 4) & 1) * 256

    def texel_rect(self):
        """The VRAM rectangle (x, y, w, h) of the texels it samples."""
        shift = (2, 1, 0, 0)[self.bpp]
        x0 = self.page_x + (min(self.u) >> shift)
        x1 = self.page_x + (max(self.u) >> shift)
        y0 = self.page_y + min(self.v)
        y1 = self.page_y + max(self.v)
        return x0, y0, x1 - x0 + 1, y1 - y0 + 1

    def clut_rect(self):
        if self.bpp >= 2:
            return None
        return (self.clut & 0x3F) * 16, (self.clut >> 6) & 0x1FF, 16 if self.bpp == 0 else 256, 1


class Model:
    def __init__(self, raw):
        self.raw = raw
        self.map_offset = struct.unpack_from("<i", raw, 0)[0]
        self.body = raw[4:]
        b = self.body
        self.name = b[:16].split(b"\0")[0].decode("latin1")
        self.num_headers = struct.unpack_from("<h", b, 0x12)[0]
        self.headers_offset = u32(b, 0x14)
        self.header_names = []
        self.layouts = {}  # body offset -> Layout
        for h in range(self.num_headers):
            ho = self.headers_offset + h * MODEL_HEADER_SIZE
            self.header_names.append(b[ho:ho + 16].split(b"\0")[0].decode("latin1"))
            commands = u32(b, ho + 0x20)
            table = u32(b, ho + 0x28)
            if not commands or not table:
                continue
            for index in self._texture_indices(commands):
                lo = u32(b, table + (index - 1) * 4)
                if lo and lo not in self.layouts:
                    self.layouts[lo] = Layout(b[lo:lo + 12], lo)

    def _texture_indices(self, commands):
        b = self.body
        cursor = commands + 4
        found = set()
        while cursor + 4 <= self.map_offset:
            c = u32(b, cursor)
            cursor += 4
            if c == 0xFFFFFFFF:
                return found
            if c & 0xFFFF0000:
                index = c & 0x1FF
                if index:
                    found.add(index)
        raise ValueError("unterminated command list")

    def vram_words(self):
        """Every VRAM word (y * 1024 + x) the model samples: texels and palettes."""
        words = set()
        for layout in self.layouts.values():
            if layout.bpp > 2:
                continue
            for rect in (layout.texel_rect(), layout.clut_rect()):
                if rect is None:
                    continue
                x, y, w, h = rect
                for yy in range(y, min(y + h, VRAM_H)):
                    for xx in range(x, min(x + w, VRAM_W)):
                        words.add(yy * VRAM_W + xx)
        return words


def parse_vram(raw):
    """A VRM file as a dict of word -> value (packed multi-block or single block)."""
    def block(off, limit):
        x, y, w, h = struct.unpack_from("<hhhh", raw, off + 0x0C)
        px = off + 0x14
        return x, y, w, h, raw[px:px + w * h * 2]

    blocks = []
    if u32(raw, 0) != 0x20:
        blocks.append(block(0, len(raw)))
    else:
        cur = 4
        while cur + 4 <= len(raw):
            size = u32(raw, cur)
            cur += 4
            if size == 0:
                break
            blocks.append(block(cur, cur + (size & ~3)))
            cur += size & ~3
    words = {}
    for x, y, w, h, px in blocks:
        for r in range(h):
            row = struct.unpack_from(f"<{w}H", px, r * w * 2)
            base = (y + r) * VRAM_W + x
            for c in range(w):
                words[base + c] = row[c]
    return words, blocks
