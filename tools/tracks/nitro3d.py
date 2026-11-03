"""Read Nitro 3D files: NSBMD models (geometry from GX display lists) and NSBTX textures."""
import struct
import numpy as np

def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]
def fx32(b, o): return s32(b, o) / 4096.0
def fx16(b, o): return s16(b, o) / 4096.0


def sections(data):
    n = u16(data, 0x0e)
    return {data[u32(data, 0x10 + 4 * i):u32(data, 0x10 + 4 * i) + 4]: u32(data, 0x10 + 4 * i) for i in range(n)}


def nitro_dict(b, o):
    """Name dictionary -> list of (name, entry bytes)."""
    count = b[o + 1]
    p = o + u16(b, o + 6)  # skip the tree block
    unit, size = u16(b, p), u16(b, p + 2)
    entries = [b[p + 4 + unit * i:p + 4 + unit * (i + 1)] for i in range(count)]
    names_at = p + 4 + unit * count
    names = [b[names_at + 16 * i:names_at + 16 * (i + 1)].rstrip(b'\0').decode('latin1') for i in range(count)]
    return list(zip(names, entries))


# ---------------------------------------------------------------- textures

def bgr555(c):
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)


class Tex0:
    def __init__(self, data, base):
        b = data
        t = base
        self.data = b
        self.tex_size = u16(b, t + 0x0c) << 3
        tex_dict = t + u16(b, t + 0x0e)
        self.tex_data = t + u32(b, t + 0x14)
        self.cmp_data = t + u32(b, t + 0x24)
        self.cmp_info = t + u32(b, t + 0x28)
        pal_dict = t + u32(b, t + 0x34)
        self.pal_data = t + u32(b, t + 0x38)
        self.textures = {}
        for name, e in nitro_dict(b, tex_dict):
            param = u32(e, 0)
            self.textures[name] = param
        self.palettes = {}
        for name, e in nitro_dict(b, pal_dict):
            self.palettes[name] = u16(e, 0) << 3

    def decode(self, tex_name, pal_name):
        """-> RGBA uint8 array (h, w, 4)."""
        b = self.data
        param = self.textures[tex_name]
        off = (param & 0xffff) << 3
        w = 8 << ((param >> 20) & 7)
        h = 8 << ((param >> 23) & 7)
        fmt = (param >> 26) & 7
        transp0 = (param >> 29) & 1
        pal = self.pal_data + self.palettes[pal_name] if pal_name in self.palettes else None
        img = np.zeros((h, w, 4), np.uint8)

        def pc(i):
            return bgr555(u16(b, pal + 2 * i))
        if fmt == 5:
            base = self.cmp_data + off
            info = self.cmp_info + off // 2
            for by in range(h // 4):
                for bx in range(w // 4):
                    blk = by * (w // 4) + bx
                    texels = u32(b, base + 4 * blk)
                    pi = u16(b, info + 2 * blk)
                    poff = (pi & 0x3fff) * 4
                    mode = pi >> 14
                    c = [bgr555(u16(b, pal + poff + 2 * k)) + (255,) for k in range(4)] if pal is not None else []
                    if mode == 0:
                        c[3] = (0, 0, 0, 0)
                    elif mode == 1:
                        c[2] = tuple((c[0][k] + c[1][k]) // 2 for k in range(3)) + (255,)
                        c[3] = (0, 0, 0, 0)
                    elif mode == 3:
                        c[2] = tuple((c[0][k] * 5 + c[1][k] * 3) // 8 for k in range(3)) + (255,)
                        c[3] = tuple((c[0][k] * 3 + c[1][k] * 5) // 8 for k in range(3)) + (255,)
                    for y in range(4):
                        for x in range(4):
                            img[by * 4 + y, bx * 4 + x] = c[(texels >> (2 * (y * 4 + x))) & 3]
            return img
        base = self.tex_data + off
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if fmt == 1:    # A3I5
                    v = b[base + i]; col = pc(v & 31); a = (v >> 5) * 255 // 7
                elif fmt == 6:  # A5I3
                    v = b[base + i]; col = pc(v & 7); a = (v >> 3) * 255 // 31
                elif fmt == 2:
                    v = (b[base + i // 4] >> (2 * (i % 4))) & 3; col = pc(v); a = 0 if (transp0 and v == 0) else 255
                elif fmt == 3:
                    v = (b[base + i // 2] >> (4 * (i % 2))) & 15; col = pc(v); a = 0 if (transp0 and v == 0) else 255
                elif fmt == 4:
                    v = b[base + i]; col = pc(v); a = 0 if (transp0 and v == 0) else 255
                elif fmt == 7:
                    v = u16(b, base + 2 * i); col = bgr555(v); a = 255 if v & 0x8000 else 0
                else:
                    col, a = (255, 0, 255), 255
                img[y, x] = col + (a,)
        return img


def load_tex0(data):
    return Tex0(data, sections(data)[b'TEX0'])


# ---------------------------------------------------------------- models

GX_ARGS = {0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16,
           0x19: 12, 0x1a: 9, 0x1b: 3, 0x1c: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1,
           0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1, 0x2a: 1, 0x2b: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1,
           0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1}


def s10(v):
    return v - 1024 if v & 512 else v


def run_display_list(dl, emit):
    """Decode a GX display list. emit(prim_type, [(pos, uv, color, normal), ...]) per BEGIN..END."""
    p = 0
    pos = [0.0, 0.0, 0.0]
    uv = (0.0, 0.0)
    col = (255, 255, 255)
    nrm = (0.0, 1.0, 0.0)
    prim = None
    verts = []
    restore = 0
    while p + 4 <= len(dl):
        cmds = dl[p:p + 4]; p += 4
        for c in cmds:
            n = GX_ARGS.get(c)
            if n is None:
                raise ValueError(f'unknown GX command {c:#x}')
            a = [u32(dl, p + 4 * i) for i in range(n)]; p += 4 * n
            if c == 0x40:
                prim = a[0] & 3; verts = []
            elif c == 0x41:
                emit(prim, verts, restore); prim = None
            elif c == 0x14:
                restore = a[0] & 31
            elif c == 0x20:
                col = bgr555(a[0])
            elif c == 0x21:
                v = a[0]; nrm = (s10(v & 1023) / 512, s10((v >> 10) & 1023) / 512, s10((v >> 20) & 1023) / 512)
            elif c == 0x22:
                v = a[0]; uv = (struct.unpack('<h', struct.pack('<H', v & 0xffff))[0] / 16,
                                struct.unpack('<h', struct.pack('<H', v >> 16))[0] / 16)
            elif c in (0x23, 0x24, 0x25, 0x26, 0x27, 0x28):
                if c == 0x23:
                    pos = [struct.unpack('<h', struct.pack('<H', a[0] & 0xffff))[0] / 4096,
                           struct.unpack('<h', struct.pack('<H', a[0] >> 16))[0] / 4096,
                           struct.unpack('<h', struct.pack('<H', a[1] & 0xffff))[0] / 4096]
                elif c == 0x24:
                    v = a[0]; pos = [s10(v & 1023) / 64, s10((v >> 10) & 1023) / 64, s10((v >> 20) & 1023) / 64]
                elif c == 0x28:
                    v = a[0]; pos = [pos[0] + s10(v & 1023) / 4096, pos[1] + s10((v >> 10) & 1023) / 4096,
                                     pos[2] + s10((v >> 20) & 1023) / 4096]
                else:
                    lo = struct.unpack('<h', struct.pack('<H', a[0] & 0xffff))[0] / 4096
                    hi = struct.unpack('<h', struct.pack('<H', a[0] >> 16))[0] / 4096
                    if c == 0x25: pos = [lo, hi, pos[2]]
                    if c == 0x26: pos = [lo, pos[1], hi]
                    if c == 0x27: pos = [pos[0], lo, hi]
                verts.append((tuple(pos), uv, col, nrm))


def triangles(prim, verts):
    """Indices of triangles for a GX primitive."""
    out = []
    n = len(verts)
    if prim == 0:
        out = [(i, i + 1, i + 2) for i in range(0, n - 2, 3)]
    elif prim == 1:
        for i in range(0, n - 3, 4):
            out += [(i, i + 1, i + 2), (i, i + 2, i + 3)]
    elif prim == 2:
        for i in range(n - 2):
            out.append((i, i + 1, i + 2) if i % 2 == 0 else (i + 1, i, i + 2))
    elif prim == 3:
        for i in range(0, n - 3, 2):
            out += [(i, i + 1, i + 3), (i, i + 3, i + 2)]
    return out


class Material:
    pass


class Model:
    def __init__(self, data):
        b = data
        mdl = sections(data)[b'MDL0']
        name, e = nitro_dict(b, mdl + 8)[0]
        m = mdl + u32(e, 0)
        self.name = name
        sbc_off, mat_off, mesh_off = u32(b, m + 4), u32(b, m + 8), u32(b, m + 0x0c)
        self.num_objs, self.num_mats, self.num_meshes = b[m + 0x17], b[m + 0x18], b[m + 0x19]
        self.scale = fx32(b, m + 0x1c)
        # objects (node transforms)
        self.objs = []
        objs_dict = m + 0x40
        for oname, oe in nitro_dict(b, objs_dict):
            o = objs_dict + u32(oe, 0)
            flags = u16(b, o); q = o + 4
            t = (0.0, 0.0, 0.0); r = np.eye(3); s = (1.0, 1.0, 1.0)
            m0 = fx16(b, o + 2)
            if not flags & 1:
                t = (fx32(b, q), fx32(b, q + 4), fx32(b, q + 8)); q += 12
            if not flags & 2:
                if flags & 8:
                    q += 4  # pivot rotation: not expected in courses
                else:
                    vals = [m0] + [fx16(b, q + 2 * i) for i in range(8)]; q += 16
                    r = np.array(vals).reshape(3, 3)
            if not flags & 4:
                s = (fx32(b, q), fx32(b, q + 4), fx32(b, q + 8))
            self.objs.append((oname, t, r, s))
        # materials
        md = m + mat_off
        tex_map_off, pal_map_off = u16(b, md), u16(b, md + 2)
        self.mats = []
        for mname, me in nitro_dict(b, md + 4):
            o = md + u32(me, 0)
            mat = Material()
            mat.name = mname
            mat.dif_amb = u32(b, o + 4)
            mat.polygon_attr = u32(b, o + 0x0c)
            mat.teximage = u32(b, o + 0x14)
            mat.flags = u16(b, o + 0x1e)
            mat.width, mat.height = u16(b, o + 0x20), u16(b, o + 0x22)
            mat.tex = mat.pal = None
            q = o + 0x2c
            mat.texmtx = (1.0, 1.0, 0.0, 0.0, 0.0, 0.0)  # sx, sy, rot sin, rot cos, tx, ty
            sx = sy = 1.0; tx = ty = 0.0
            if not mat.flags & 2:
                sx, sy = fx32(b, q), fx32(b, q + 4); q += 8
            if not mat.flags & 4:
                q += 4
            if not mat.flags & 8:
                tx, ty = fx32(b, q), fx32(b, q + 4); q += 8
            mat.texmtx = (sx, sy, tx, ty)
            self.mats.append(mat)
        for key, off in ((0, tex_map_off), (1, pal_map_off)):
            for tname, te in nitro_dict(b, md + off):
                lst = md + u16(te, 0); cnt = te[2]
                for i in range(cnt):
                    mat = self.mats[b[lst + i]]
                    if key == 0: mat.tex = tname
                    else: mat.pal = tname
        # meshes
        self.meshes = []
        mshd = m + mesh_off
        for sname, se in nitro_dict(b, mshd):
            o = mshd + u32(se, 0)
            dl = o + u32(b, o + 8)
            self.meshes.append((sname, b[dl:dl + u32(b, o + 12)]))
        # render commands: which material each mesh is drawn with, and its node
        self.draws = []
        p = m + sbc_off
        cur_mat, cur_node = 0, 0
        ARGS = {0x00: 0, 0x01: 0, 0x02: 2, 0x03: 1, 0x04: 1, 0x05: 1, 0x06: 3, 0x07: 1, 0x08: 1, 0x09: None,
                0x0b: 0, 0x0c: 2, 0x0d: 2}
        while True:
            cmd = b[p]; op = cmd & 0x1f; p += 1
            if op == 0x01:
                break
            if op == 0x04:
                cur_mat = b[p]; p += 1
            elif op == 0x05:
                self.draws.append((b[p], cur_mat, cur_node)); p += 1
            elif op == 0x06:
                cur_node = b[p]; p += 3
                if cmd == 0x26 or cmd == 0x46: p += 1
                if cmd == 0x66: p += 2
            elif op == 0x03:
                cur_node = b[p]; p += 1
            elif op == 0x09:
                n = b[p + 1]; p += 2 + 3 * n
            elif op == 0x02:
                p += 2
            elif op in (0x07, 0x08):
                p += 1
            elif op in (0x0c, 0x0d):
                p += 2
            elif op in (0x00, 0x0b):
                pass
            else:
                raise ValueError(f'SBC op {cmd:#x} at {p - 1 - m:#x}')

    def triangles(self):
        """-> list of (mat_index, [(pos, uv, color, normal) * 3]) in model space (scale applied)."""
        out = []
        for mesh_i, mat_i, node in self.draws:
            _, t, r, s = self.objs[node]
            def emit(prim, verts, restore):
                for tri in triangles(prim, verts):
                    vv = []
                    for i in tri:
                        pos, uv, col, nrm = verts[i]
                        pv = (r @ (np.array(pos) * s)) + t
                        vv.append((tuple(pv * self.scale), uv, col, nrm))
                    out.append((mat_i, vv))
            run_display_list(self.meshes[mesh_i][1], emit)
        return out
