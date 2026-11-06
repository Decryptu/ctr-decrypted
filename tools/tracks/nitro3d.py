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
                    # modes 1 and 3 store two colours, and a palette's last entry may have no others after it
                    stored = 2 if mode & 1 else 4
                    c = [bgr555(u16(b, pal + poff + 2 * k)) + (255,) for k in range(stored)] + [(0, 0, 0, 0)] * (4 - stored)
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


def s32le(v):
    return v - (1 << 32) if v & (1 << 31) else v


def run_display_list(dl, emit, matrix=None, stack=None):
    """Decode a GX display list. emit(prim_type, [(pos, uv, color, normal), ...]) per BEGIN..END. matrix,
    stack: the position matrix and matrix stack a model's render commands left, which transform each
    vertex as it is given: a skinned mesh restores a stack slot between its vertices."""
    p = 0
    pos = [0.0, 0.0, 0.0]
    uv = (0.0, 0.0)
    col = None
    nrm = None
    prim = None
    verts = []
    current = np.eye(4) if matrix is None else matrix
    stack = {} if stack is None else stack
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
                emit(prim, verts); prim = None
            elif c == 0x13:
                stack[a[0] & 31] = current.copy()
            elif c == 0x14:
                current = stack.get(a[0] & 31, current)
            elif c == 0x15:
                current = np.eye(4)
            elif c == 0x1b:
                current = current @ np.diag([s32le(x) / 4096 for x in a] + [1.0])
            elif c == 0x1c:
                move = np.eye(4); move[:3, 3] = [s32le(x) / 4096 for x in a]
                current = current @ move
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
                world = current @ np.array([pos[0], pos[1], pos[2], 1.0])
                turned = None if nrm is None else tuple(current[:3, :3] @ np.array(nrm))
                verts.append((tuple(world[:3]), uv, col, turned))


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
        self.up_scale, self.down_scale = fx32(b, m + 0x1c), fx32(b, m + 0x20)
        self.scale = self.up_scale
        self.box = [fx16(b, m + 0x2c + 2 * i) for i in range(3)], [fx16(b, m + 0x32 + 2 * i) for i in range(3)]
        # objects: a node's matrix, its parent's by way of the render commands
        self.objs = []
        objs_dict = m + 0x40
        for oname, oe in nitro_dict(b, objs_dict):
            self.objs.append((oname, node_matrix(b, objs_dict + u32(oe, 0))))
        inv_at = m + u32(b, m + 0x10)
        self.inv_binds = []
        for i in range(self.num_objs):
            at = inv_at + 84 * i
            if at + 48 > len(b):
                break
            bind = np.eye(4)
            bind[:3, :4] = np.array([fx32(b, at + 4 * k) for k in range(12)]).reshape(4, 3).T
            self.inv_binds.append(bind)
        # materials
        md = m + mat_off
        tex_map_off, pal_map_off = u16(b, md), u16(b, md + 2)
        self.mats = []
        for mname, me in nitro_dict(b, md + 4):
            o = md + u32(me, 0)
            mat = Material()
            mat.name = mname
            mat.dif_amb, mat.spe_emi = u32(b, o + 4), u32(b, o + 8)
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
        self.ops = render_commands(b, m + sbc_off)

    def default_colour(self, mat):
        """A vertex's colour before any colour command: the material's diffuse, where it says so."""
        return bgr555(mat.dif_amb & 0x7fff) if mat.dif_amb & 0x8000 else (255, 255, 255)

    def lit_colour(self, mat, normal):
        """What the DS's lights make of a vertex with a normal, for one white light from above and in front."""
        strength = LIT_BACK + (1 - LIT_BACK) * max(0.0, float(np.dot(LIGHT, normal / max(np.linalg.norm(normal), 1e-6))))
        diffuse = np.array(bgr555(mat.dif_amb & 0x7fff), float)
        ambient = np.array(bgr555((mat.dif_amb >> 16) & 0x7fff), float)
        emission = np.array(bgr555((mat.spe_emi >> 16) & 0x7fff), float)
        return tuple(np.clip(emission + ambient + diffuse * strength, 0, 255))

    def triangles(self, lit=False):
        """-> list of (mat_index, [(pos, uv, color, normal) * 3]) in model space (scale applied), the model
        in its rest pose. lit: colours as the DS's lights make them, for what has normals: an object."""
        out = []
        current, stack, mat_i = np.eye(4), {}, 0
        scaled = any(op[0] == 'up' for op in self.ops)
        for op in self.ops:
            kind = op[0]
            if kind == 'load':
                current = stack.get(op[1], np.eye(4)).copy()
            elif kind == 'store':
                stack[op[1]] = current.copy()
            elif kind == 'node':
                current = current @ self.objs[op[1]][1]
            elif kind == 'skin':
                current = sum(weight * stack.get(slot, np.eye(4)) @ self.inv_binds[bind] for slot, bind, weight in op[1])
            elif kind == 'up':
                current = current @ np.diag([self.up_scale] * 3 + [1.0])
            elif kind == 'down':
                current = current @ np.diag([self.down_scale] * 3 + [1.0])
            elif kind == 'material':
                mat_i = op[1]
            elif kind == 'draw':
                mat = self.mats[mat_i]
                final = 1.0 if scaled else self.scale

                def emit(prim, verts, mat=mat, mat_i=mat_i):
                    for tri in triangles(prim, verts):
                        vv = []
                        for i in tri:
                            pos, uv, col, nrm = verts[i]
                            if col is None:
                                col = self.default_colour(mat) if lit else (255, 255, 255)
                            if lit and nrm is not None and mat.polygon_attr & 15:
                                col = self.lit_colour(mat, np.array(nrm))
                            vv.append((tuple(np.array(pos) * final), uv, col, nrm if nrm is not None else (0.0, 1.0, 0.0)))
                        out.append((mat_i, vv))
                run_display_list(self.meshes[op[1]][1], emit, current.copy(), stack)
        return out


LIGHT = np.array([0.0, 0.7071, 0.7071])
LIT_BACK = 0.55  # what a face turned from the light keeps: the game has no light on it either way, and a black side reads as a hole


def pivot_matrix(select, neg, a, b):
    """A node's rotation stored as two values: one axis is +-1, and the rest are a, b, c, d."""
    o = -1.0 if neg & 1 else 1.0
    c = -b if neg & 2 else b
    d = -a if neg & 4 else a
    columns = np.zeros((3, 3))
    major, minor = select // 3, select % 3
    rest = iter((a, b, c, d))
    for i in range(3):
        for j in range(3):
            columns[i, j] = o if (i, j) == (major, minor) else 0.0 if i == major or j == minor else next(rest)
    return columns.T


def node_matrix(b, o):
    """A node's rest transform, translation * rotation * scale. Rotations are stored by columns."""
    flags = u16(b, o)
    m0 = fx16(b, o + 2)
    q = o + 4
    translation, rotation, scale = (0.0, 0.0, 0.0), np.eye(3), (1.0, 1.0, 1.0)
    if not flags & 1:
        translation = (fx32(b, q), fx32(b, q + 4), fx32(b, q + 8)); q += 12
    if flags & 8:
        rotation = pivot_matrix((flags >> 4) & 15, (flags >> 8) & 15, fx16(b, q), fx16(b, q + 2)); q += 4
    elif not flags & 2:
        rotation = np.array([m0] + [fx16(b, q + 2 * i) for i in range(8)]).reshape(3, 3).T; q += 16
    if not flags & 4:
        scale = (fx32(b, q), fx32(b, q + 4), fx32(b, q + 8))
    out = np.eye(4)
    out[:3, :3] = rotation @ np.diag(scale)
    out[:3, 3] = translation
    return out


def render_commands(b, p):
    """A model's render commands as steps for Model.triangles."""
    ops = []
    sizes = {0x00: 0, 0x02: 2, 0x07: 1, 0x08: 1, 0x0c: 2, 0x0d: 2, 0x40: 0, 0x47: 2, 0x80: 0}
    while True:
        cmd = b[p]; p += 1
        if cmd == 0x01:
            return ops
        if cmd == 0x03:
            ops.append(('load', b[p])); p += 1
        elif cmd in (0x04, 0x24, 0x44):
            ops.append(('material', b[p])); p += 1
        elif cmd == 0x05:
            ops.append(('draw', b[p])); p += 1
        elif cmd in (0x06, 0x26, 0x46, 0x66):
            node = b[p]
            store = b[p + 3] if cmd in (0x26, 0x66) else None
            load = b[p + 3] if cmd == 0x46 else b[p + 4] if cmd == 0x66 else None
            p += {0x06: 3, 0x26: 4, 0x46: 4, 0x66: 5}[cmd]
            if load is not None:
                ops.append(('load', load))
            ops.append(('node', node))
            if store is not None:
                ops.append(('store', store))
        elif cmd == 0x09:
            count = b[p + 1]
            terms = [(b[p + 2 + 3 * i], b[p + 3 + 3 * i], b[p + 4 + 3 * i] / 256) for i in range(count)]
            ops.append(('skin', terms))
            ops.append(('store', b[p]))
            p += 2 + 3 * count
        elif cmd == 0x0b:
            ops.append(('up',))
        elif cmd == 0x2b:
            ops.append(('down',))
        elif cmd in sizes:
            p += sizes[cmd]
        else:
            raise ValueError(f'render command {cmd:#x}')
