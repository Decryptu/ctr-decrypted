"""Models a converted track needs from the player's CTR disc (docs/TRACK-CONVERTER.md#item-boxes).

The weapon crate and the Wumpa fruit are not in the shared packs: every retail track carries its own
copies in its LEV, the crate's textures in that track's VRM (the fruit has none). A converted LEV has
to carry them too, so they are read from Dingo Canyon on the disc the game already needs, and the
package stays on the player's machine.
"""
import os, struct, sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'racers'))
from ctrdisc import BigFile, Disc  # noqa: E402
from ctrmodel import Layout, parse_vram  # noqa: E402

SOURCE_LEVEL = 0  # Dingo Canyon
BIG_VRM, BIG_LEV = SOURCE_LEVEL * 8, SOURCE_LEVEL * 8 + 1  # the 1P files of the level's group
PU_WUMPA_FRUIT, PU_RANDOM_CRATE = 2, 8  # enum MODEL_ID
OFF_NUM_MODELS, OFF_MODELS = 0x14, 0x18
MODEL_HEADER_SIZE = 0x40
LAYOUT_SIZE = 12
ABR_MASK = 0x60  # a texture page word's blend mode


class ItemModel:
    """A model copied out of a retail LEV: `blob` is its bytes, every pointer in it an offset from the
    blob's start at the positions in `pointers`. `groups` are its textures: one image per (palette,
    page), with the offsets of the layouts that sample it and the corner of the image in the page."""

    def __init__(self, blob, pointers, groups):
        self.blob, self.pointers, self.groups = blob, pointers, groups

    def retarget(self, group, u, v, clut, tpage):
        """Point every layout of a group at where its image now is in the converted track's VRAM."""
        for offset in group['layouts']:
            old = Layout(bytes(self.blob[offset:offset + LAYOUT_SIZE]), offset)
            page = (tpage & ~ABR_MASK) | (old.tpage & ABR_MASK)
            us = [x - group['u'] + u for x in old.u]
            vs = [y - group['v'] + v for y in old.v]
            struct.pack_into('<BBHBBHBBBB', self.blob, offset, us[0], vs[0], clut, us[1], vs[1], page, us[2], vs[2], us[3], vs[3])


def decode(vram, layout, x0, y0, w, h):
    """(w, h) texels at page position (x0, y0), as RGBA. Colour 0 is transparent."""
    if layout.bpp > 1:
        raise ValueError('a direct-colour item texture')
    cx, cy, cw, _ = layout.clut_rect()
    palette = []
    for i in range(cw):
        c = vram[cy * 1024 + cx + i]
        palette.append(((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31, 255 if c else 0))
    bits, shift = (4, 2) if layout.bpp == 0 else (8, 1)
    out = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            u = x0 + x
            word = vram[(layout.page_y + y0 + y) * 1024 + layout.page_x + (u >> shift)]
            index = (word >> ((u & ((1 << shift) - 1)) * bits)) & ((1 << bits) - 1)
            out[y, x] = palette[index]
    return out


def read(disc_path):
    """{'crates': the weapon crate, 'fruit': the Wumpa fruit} of the player's disc, as ItemModels."""
    disc = Disc(disc_path)
    big = BigFile(disc)
    raw, vrm = big.read(BIG_LEV), big.read(BIG_VRM)
    disc.close()
    vram, _ = parse_vram(vrm)
    return {'crates': _model(raw, vram, PU_RANDOM_CRATE), 'fruit': _model(raw, vram, PU_WUMPA_FRUIT)}


def _model(raw, vram, model_id):
    size = struct.unpack_from('<I', raw, 0)[0]
    data = raw[4:4 + size]
    count = struct.unpack_from('<I', raw, 4 + size)[0] // 4
    ptrs = set(struct.unpack_from(f'<{count}I', raw, 8 + size))
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    table = u32(OFF_MODELS)
    models = sorted(u32(table + 4 * i) for i in range(u32(OFF_NUM_MODELS)))
    start = next(m for m in models if struct.unpack_from('<h', data, m + 0x10)[0] == model_id)
    end = models[models.index(start) + 1]  # models lie one after the other; neither is the last
    blob = bytearray(data[start:end])
    pointers = sorted(p - start for p in ptrs if start <= p < end)
    for p in pointers:
        target = struct.unpack_from('<I', blob, p)[0]
        if not start <= target < end:
            raise ValueError(f'model {model_id} points outside itself at +{p:#x}')
        struct.pack_into('<I', blob, p, target - start)

    by_texture = {}
    num_headers, headers = struct.unpack_from('<hI', blob, 0x12)
    for h in range(num_headers):
        slot = struct.unpack_from('<I', blob, headers + MODEL_HEADER_SIZE * h + 0x28)[0]
        while slot in pointers:
            offset = struct.unpack_from('<I', blob, slot)[0]
            layout = Layout(bytes(blob[offset:offset + LAYOUT_SIZE]), offset)
            by_texture.setdefault((layout.clut, layout.tpage), {})[offset] = layout
            slot += 4
    groups = []
    for layouts in by_texture.values():
        first = next(iter(layouts.values()))
        u0 = min(min(l.u) for l in layouts.values())
        v0 = min(min(l.v) for l in layouts.values())
        w = max(max(l.u) for l in layouts.values()) - u0 + 1
        h = max(max(l.v) for l in layouts.values()) - v0 + 1
        groups.append({'layouts': sorted(layouts), 'u': u0, 'v': v0, 'image': decode(vram, first, u0, v0, w, h)})
    return ItemModel(blob, pointers, groups)
