"""Convert a Mario Kart DS course from the player's own ROM into an installed custom track package.

  python tools/tracks/mkds_convert.py <rom.nds> cross_course --title "Figure-8 Circuit"

Steps (docs/TRACK-CONVERTER.md): the course becomes CrashTeamEditor input (OBJ + MTL + textures +
presets), cte_headless turns that into a LEV and VRM, lev_patch adds what CrashTeamEditor does not
write, and package installs the result under its pin, which is printed.

Render geometry comes from course_model.nsbmd (textured, vertex coloured, drawn but never collided).
Collision comes from course_collision.kcl (invisible quadblocks carrying terrain, walls and checkpoints).
Start, finish line and route come from course_map.nkm.
"""
import argparse, json, math, os, shutil, subprocess, sys
from collections import defaultdict
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nds, nitro3d, mkdsmap, lev_patch, package, winding_check

MODEL_TO_WORLD = 16.0  # course models are drawn 16 times larger than their stored size

# Quadblock flags by the game's own names (include/namespace_Level.h). CrashTeamEditor's labels
# for these bits are misleading: its "Invisible" (0x1) is a reflection split line, "Out of Bounds"
# (0x10) is NO_COLLISION_RESPONSE, "Mask Grab" (0x200) is the kill plane, "Wall" (0x2000) is the
# collision surface and "No Collision" (0x4000) only stops the camera respawn probe.
CAMERA_SEARCH, KILL_PLANE, GROUND, COLLISION_SURFACE = 0x0800, 0x0200, 0x1000, 0x2000

# KCL type -> (material name, terrain label, quad flags, pathable). Collision blocks are drawn
# with nothing: navpatch sets their faces' draw mode to "none".
_ROAD = GROUND | CAMERA_SEARCH
_WALL = COLLISION_SURFACE | CAMERA_SEARCH
_FALL = GROUND | KILL_PLANE | CAMERA_SEARCH
COLLISION = {
    0x00: ('col_road', 'Asphalt', _ROAD, True),
    0x01: ('col_road', 'Asphalt', _ROAD, True),
    0x06: ('col_road', 'Asphalt', _ROAD, True),
    0x0d: ('col_road', 'Asphalt', _ROAD, True),
    0x14: ('col_road', 'Asphalt', _ROAD, True),
    0x02: ('col_grass', 'Grass', _ROAD, True),
    0x03: ('col_offroad', 'Slow Grass', _ROAD, True),
    0x05: ('col_mud', 'Mud', _ROAD, True),
    0x07: ('col_boost', 'Asphalt', _ROAD, True),
    0x08: ('col_wall', 'Asphalt', _WALL, False),
    0x09: ('col_wall', 'Asphalt', _WALL, False),
    0x0e: ('col_wall', 'Asphalt', _WALL, False),
    0x10: ('col_wall', 'Asphalt', _WALL, False),
    0x15: ('col_wall', 'Asphalt', _WALL, False),
    0x0a: ('col_fall', 'Asphalt', _FALL, False),
    0x0b: ('col_fall', 'Asphalt', _FALL, False),
    0x11: ('col_fall', 'Water', _FALL, False),
}


# ---------------------------------------------------------------- geometry helpers

def lerp(a, b, t):
    return {k: a[k] + (b[k] - a[k]) * t for k in a}


def clip(poly, key, axis, value, keep_above):
    """Sutherland-Hodgman clip of a polygon of vertex dicts against vertex[key][axis] >= / <= value."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        da, db = a[key][axis] - value, b[key][axis] - value
        if not keep_above:
            da, db = -da, -db
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            v = {k: a[k] + (b[k] - a[k]) * t for k in a}
            out.append(v)
    return out


def split_by_tiles(tri, key='uv'):
    """Cut a triangle so every piece lies inside one integer UV tile. -> list of polygons."""
    pieces = [tri]
    for axis in (0, 1):
        out = []
        for poly in pieces:
            vals = [v[key][axis] for v in poly]
            lo, hi = math.floor(min(vals) + 1e-6), math.ceil(max(vals) - 1e-6)
            rest = poly
            for cut in range(lo + 1, hi):
                below = clip(rest, key, axis, cut, False)
                rest = clip(rest, key, axis, cut, True)
                if len(below) >= 3:
                    out.append(below)
            if len(rest) >= 3:
                out.append(rest)
        pieces = out
    return pieces


def close_tjunctions(items, tol):
    """Where a vertex lies on (within tol of) another triangle's edge without being one of its
    corners, insert it into that edge and fan the triangle from its centroid, so the two meet
    exactly. MKDS collision has such joins; MKDS's thick, radius-based collision hides the
    sliver they leave, CTR's plane-and-triangle test does not."""
    verts = {}
    for tri, _ in items:
        for v in tri:
            verts.setdefault(tuple(v['pos']), v['pos'])
    cell = max(tol * 8, 1e-6)
    grid = {}
    for k, p in verts.items():
        grid.setdefault(tuple(np.floor(p / cell).astype(int)), []).append(p)
    def on_edge(a, b):
        lo, hi = np.minimum(a, b) - tol, np.maximum(a, b) + tol
        found = []
        for cx in range(int(np.floor(lo[0] / cell)), int(np.floor(hi[0] / cell)) + 1):
            for cy in range(int(np.floor(lo[1] / cell)), int(np.floor(hi[1] / cell)) + 1):
                for cz in range(int(np.floor(lo[2] / cell)), int(np.floor(hi[2] / cell)) + 1):
                    for p in grid.get((cx, cy, cz), ()):
                        ab = b - a
                        t = np.dot(p - a, ab) / np.dot(ab, ab)
                        if 1e-6 < t < 1 - 1e-6 and np.linalg.norm(a + ab * t - p) <= tol:
                            found.append((t, p))
        return sorted(found, key=lambda x: x[0])
    out = []
    for tri, meta in items:
        poly = []
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            poly.append(a)
            for t, p in on_edge(a['pos'], b['pos']):
                v = {k: a[k] + (b[k] - a[k]) * t for k in a}
                v['pos'] = p
                poly.append(v)
        if len(poly) == 3:
            out.append((tri, meta))
            continue
        # points on one edge only: fan from the opposite corner (no slivers); else from the centroid
        corners = [next(j for j, w in enumerate(poly) if w is v) for v in tri]
        extra_edges = [i for i in range(3) if (corners[(i + 1) % 3] - corners[i]) % len(poly) > 1]
        if len(extra_edges) == 1:
            apex = corners[(extra_edges[0] + 2) % 3]
            ring = poly[apex + 1:] + poly[:apex]
            for i in range(len(ring) - 1):
                out.append(([poly[apex], ring[i], ring[i + 1]], meta))
            continue
        c = {k: sum(v[k] for v in tri) / 3 for k in tri[0]}
        for i in range(len(poly)):
            out.append(([poly[i], poly[(i + 1) % len(poly)], c], meta))
    return out


def refine(items, max_edge, weld_tol, snap=4, tjunction_tol=0.0):
    """Conforming refinement: split every edge longer than max_edge at its midpoint, in every
    triangle that has it, until none is left. Neighbours always split a shared edge at the same
    point, so no T-junction (and no crack the game can fall through) appears.
    items: list of (triangle of 3 vertex dicts with 'pos', meta). Positions are snapped first so
    vertices the source shares only approximately become exactly shared."""
    def key(v):
        return tuple(v['pos'])
    def mid(a, b):
        m = {k: (a[k] + b[k]) / 2 for k in a}
        m['pos'] = np.round(m['pos'], snap)
        return m
    # weld: every position within weld_tol of an earlier one becomes that one exactly
    cell = weld_tol * 4
    reps = {}
    def weld(p):
        c = tuple(np.floor(p / cell).astype(int))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for r in reps.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                        if np.abs(r - p).max() <= weld_tol:
                            return r
        reps.setdefault(c, []).append(p)
        return p
    def snapv(v):
        v = dict(v); v['pos'] = weld(np.round(np.asarray(v['pos'], float), snap)); return v
    items = [([snapv(v) for v in tri], meta) for tri, meta in items]
    if tjunction_tol:
        items = close_tjunctions(items, tjunction_tol)
    while True:
        long = set()
        for tri, _ in items:
            for i in range(3):
                a, b = tri[i], tri[(i + 1) % 3]
                if np.linalg.norm(a['pos'] - b['pos']) > max_edge:
                    long.add(frozenset((key(a), key(b))))
        if not long:
            return items
        out = []
        for tri, meta in items:
            marks = [frozenset((key(tri[i]), key(tri[(i + 1) % 3]))) in long for i in range(3)]
            n = sum(marks)
            if n == 0:
                out.append((tri, meta)); continue
            if n == 3:
                a, b, c = tri
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                out += [([a, ab, ca], meta), ([ab, b, bc], meta), ([ca, bc, c], meta), ([ab, bc, ca], meta)]
                continue
            if n == 1:
                i = marks.index(True)
                p, q, r = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
                m = mid(p, q)
                out += [([p, m, r], meta), ([m, q, r], meta)]
                continue
            i = next(i for i in range(3) if marks[i] and marks[(i + 1) % 3])
            p, q, r = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
            m1, m2 = mid(p, q), mid(q, r)
            out += [([m1, q, m2], meta), ([p, m1, m2], meta), ([p, m2, r], meta)]
        items = out


def pair_quads(items, max_dev):
    """Merge pairs of triangles that share an edge into quads, so one quadblock draws what two
    triblocks did. A pair merges only when one affine texture map covers both (the quad's
    midpoints and centre then get exactly the texture the triangles had), the shape is convex
    and it is flat within max_dev. -> (quads as 4 vertex dicts in winding order, leftover tris);
    both carry their meta."""
    def k(v):
        return tuple(v['pos'])
    edges = defaultdict(list)
    for i, (t, _) in enumerate(items):
        for e in range(3):
            edges[frozenset((k(t[e]), k(t[(e + 1) % 3])))].append((i, e))
    cands = []
    for lst in edges.values():
        if len(lst) != 2:
            continue
        (i, e), (j, f) = lst
        (ti, mi), (tj, mj) = items[i], items[j]
        if mi != mj:
            continue
        a, b, c = ti[e], ti[(e + 1) % 3], ti[(e + 2) % 3]
        if not (k(tj[f]) == k(b) and k(tj[(f + 1) % 3]) == k(a)):
            continue  # opposite winding: the pair would fold over
        b2, a2, d = tj[f], tj[(f + 1) % 3], tj[(f + 2) % 3]
        if any(np.abs(x[key] - y[key]).max() > 1e-4 for x, y in ((a, a2), (b, b2)) for key in ('uv', 'col')):
            continue  # texture or colour seam along the shared edge
        P = [np.asarray(v['pos'], float) for v in (a, b, c, d)]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        if abs(np.dot(P[3] - P[0], n)) / np.linalg.norm(n) > max_dev:
            continue  # not flat: the quad's centre would leave the triangles' surface
        # d in a, b, c's own affine frame: one texture map covers both when d's uv follows it
        M = np.stack([P[1] - P[0], P[2] - P[0]], 1)
        st = np.linalg.lstsq(M, P[3] - P[0], rcond=None)[0]
        uv = a['uv'] + st[0] * (b['uv'] - a['uv']) + st[1] * (c['uv'] - a['uv'])
        if np.abs(uv - d['uv']).max() > 2e-3:
            continue
        col = a['col'] + st[0] * (b['col'] - a['col']) + st[1] * (c['col'] - a['col'])
        if np.abs(col - d['col']).max() > 0.04:
            continue
        quad = [a, d, b, c]
        Q = [np.asarray(v['pos'], float) for v in quad]
        if any(np.dot(np.cross(Q[(q + 1) % 4] - Q[q], Q[(q + 2) % 4] - Q[(q + 1) % 4]), n) <= 0 for q in range(4)):
            continue  # concave or degenerate corner
        # prefer merging across the longest edge: the two triangles of a split rectangle
        cands.append((-np.linalg.norm(P[1] - P[0]), i, j, quad, mi))
    cands.sort(key=lambda x: x[0])
    used = set()
    quads = []
    for _, i, j, quad, mi in cands:
        if i in used or j in used:
            continue
        used |= {i, j}
        quads.append((quad, mi))
    return quads, [items[i] for i in range(len(items)) if i not in used]


def group_grids(quads):
    """Group four quads that meet at one vertex, each sharing an edge with the next around it,
    into one quadblock: its 3x3 grid is then made of the source's own corners, so nothing is
    interpolated. Each face keeps its own UVs (a quadblock has a texture layout per face), so
    texture seams between them are fine. -> (grids as 4 quads in cyclic order around the shared
    centre, each rotated to start at it; leftover quads)."""
    def k(v):
        return tuple(v['pos'])
    around = defaultdict(list)
    for i, (q, _) in enumerate(quads):
        for c in range(4):
            around[k(q[c])].append((i, c))
    used = set()
    grids = []
    for centre, lst in around.items():
        if len(lst) != 4 or any(i in used for i, _ in lst):
            continue
        if len({quads[i][1] for i, _ in lst}) != 1:
            continue
        rot = {i: quads[i][0][c:] + quads[i][0][:c] for i, c in lst}  # v, x, o, y
        # the next quad around the centre starts along this one's last edge: its x is our y
        by_x = {k(r[1]): i for i, r in rot.items()}
        start = lst[0][0]
        order = [start]
        while len(order) < 4:
            nxt = by_x.get(k(rot[order[-1]][3]))
            if nxt is None or nxt in order:
                break
            order.append(nxt)
        if len(order) != 4 or k(rot[order[-1]][3]) != k(rot[start][1]):
            continue
        # every outer corner and edge point must be distinct, or CTE cannot read the grid
        pts = {k(rot[i][j]) for i in order for j in (1, 2)}
        if len(pts) != 8:
            continue
        # CTE takes the grid's facing from its outer corners (orient_quad): none may be straight
        if min(corner_sines([rot[i][2] for i in order])) < 0.1:
            continue
        used |= set(order)
        grids.append(([rot[i] for i in order], quads[start][1]))
    return grids, [quads[i] for i in range(len(quads)) if i not in used]


def corner_sines(ring):
    """|sin| of the angle at each corner of a polygon of vertex dicts (0 for a straight corner)."""
    P = [np.asarray(v['pos'], float) for v in ring]
    out = []
    for i in range(len(P)):
        a, b = P[i - 1] - P[i], P[(i + 1) % len(P)] - P[i]
        out.append(np.linalg.norm(np.cross(a, b)) / max(np.linalg.norm(a) * np.linalg.norm(b), 1e-12))
    return out


def orient_quad(quad):
    """CrashTeamEditor takes a quadblock's facing from its corners 0 and 2 (the triangles
    p0 p2 p6 and p2 p8 p6 of its grid) and turns the block over when that disagrees with the OBJ
    normal. A quad that undoes a midpoint split has a straight corner, which gives a zero normal
    and a block turned over at random: start the ring so that corners 0 and 2 are not straight."""
    s = corner_sines(quad)
    return quad if min(s[0], s[2]) >= min(s[1], s[3]) else quad[1:] + quad[:1]


def fan(poly):
    return [(poly[0], poly[i], poly[i + 1]) for i in range(1, len(poly) - 1)]


def area(tri):
    a, b, c = (np.array(v['pos']) for v in tri)
    return np.linalg.norm(np.cross(b - a, c - a)) / 2


# ---------------------------------------------------------------- course

class Course:
    def __init__(self, rom_files, name):
        arc = nds.carc(rom_files[f'data/Course/{name}.carc'])
        tex = nds.carc(rom_files[f'data/Course/{name}Tex.carc'])
        self.model = nitro3d.Model(arc['course_model.nsbmd'])
        self.tex0 = nitro3d.load_tex0(tex['course_model.nsbtx'])
        self.kcl = mkdsmap.kcl(arc['course_collision.kcl'])
        self.nkm = mkdsmap.nkm(arc['course_map.nkm'])
        self.sky = None
        if 'course_model_V.nsbmd' in arc and 'course_model_V.nsbtx' in tex:
            self.sky = (nitro3d.Model(arc['course_model_V.nsbmd']), nitro3d.load_tex0(tex['course_model_V.nsbtx']))

    def sky_colours(self):
        """(zenith, horizon) RGB from the skybox texture's top and bottom rows; a plain blue without one."""
        if not self.sky:
            return (40, 90, 200), (150, 200, 240)
        model, tex0 = self.sky
        m = model.mats[0]
        a = tex0.decode(m.tex, m.pal)[..., :3].astype(float)
        h = a.shape[0]
        return tuple(a[:h // 8].reshape(-1, 3).mean(0)), tuple(a[-h // 8:].reshape(-1, 3).mean(0))


def quantize(img):
    """RGBA uint8 array -> PIL image with at most 256 colours (16 when that loses little)."""
    im = Image.fromarray(img, 'RGBA')
    alpha = img[..., 3]
    # one PS1 texture page is 64 VRAM words wide: 256 texels at 4 bits, 128 at 8 (CTE refuses wider)
    for colours in ((16,) if img.shape[1] > 128 else (16, 256)):
        q = im.quantize(colours, method=Image.Quantize.FASTOCTREE).convert('RGBA')
        qa = np.array(q)
        err = np.abs(qa[..., :3].astype(int) - img[..., :3].astype(int))[alpha > 127].mean() if (alpha > 127).any() else 0
        if err < 3.0 or colours == 256 or img.shape[1] > 128:
            break
    qa = np.array(q)
    qa[..., 3] = np.where(alpha > 127, 255, 0)
    return Image.fromarray(qa, 'RGBA'), colours


def convert(course, out_dir, name, scale, max_edge):
    os.makedirs(out_dir, exist_ok=True)
    tex_dir = out_dir  # CTE resolves map_Kd next to the OBJ only
    os.makedirs(tex_dir, exist_ok=True)

    # world placement: centre the course, MKDS units -> CTE units
    kpts = np.array([v for t in course.kcl for v in t[:3]])
    centre = (kpts.min(0) + kpts.max(0)) / 2

    def to_cte(p):
        q = (np.asarray(p) - centre) * scale
        return (q[0], q[1], q[2])  # OBJ and MKDS share axes (right-handed, Y up)

    # ---- materials and textures
    mtl = []
    mat_names = {}
    tiles = {}
    # Repeat a tiling texture inside its image so fewer triangles need cutting at tile edges, but
    # only as often as one triangle spans it, and only while every texture fits the level's VRAM.
    span = defaultdict(lambda: [1.0, 1.0])
    for mi, tri in course.model.triangles():
        m = course.model.mats[mi]
        for axis, size, sc in ((0, m.width, m.texmtx[0]), (1, m.height, m.texmtx[1])):
            vals = [v[1][axis] * sc / size for v in tri]
            span[mi][axis] = max(span[mi][axis], max(vals) - min(vals))
    def pow2_at_most(x):
        k = 1
        while k * 2 <= x: k *= 2
        return k
    for i, m in enumerate(course.model.mats):
        kx = pow2_at_most(min(256 // m.width, math.ceil(span[i][0]), 4)) if (m.teximage >> 16) & 1 else 1
        ky = pow2_at_most(min(256 // m.height, math.ceil(span[i][1]), 4)) if (m.teximage >> 17) & 1 else 1
        tiles[i] = [kx, ky]
    def vram_words(i):
        m = course.model.mats[i]
        w, h = m.width * tiles[i][0], m.height * tiles[i][1]
        return math.ceil(w / 4) * h  # 4-bit worst case; 8-bit textures are at most 128 wide
    budget = 14 * 64 * 256 * 0.85  # 14 free texture pages in a CTE VRM, minus room for palettes
    while sum(vram_words(i) for i in tiles) > budget:
        i = max((i for i in tiles if max(tiles[i]) > 1), key=vram_words)
        a = 0 if tiles[i][0] >= tiles[i][1] else 1
        tiles[i][a] //= 2
    for i_mat, m in enumerate(course.model.mats):
        i = i_mat
        mname = f'v{i:02d}_{m.tex}'
        mat_names[i] = mname
        src = course.tex0.decode(m.tex, m.pal)
        kx, ky = tiles[i_mat]
        fx, fy = (m.teximage >> 18) & 1, (m.teximage >> 19) & 1
        rows = []
        for j in range(ky):
            row = []
            for i in range(kx):
                t = src
                if fx and i % 2: t = t[:, ::-1]
                if fy and j % 2: t = t[::-1, :]
                row.append(t)
            rows.append(np.concatenate(row, 1))
        img, colours = quantize(np.ascontiguousarray(np.concatenate(rows, 0)))
        fn = f'{mname}.png'
        img.save(os.path.join(tex_dir, fn))
        mtl += [f'newmtl {mname}', f'map_Kd {fn}', '']
    # collision blocks: fully transparent (colour 0 on the PS1), so no draw path shows them
    grey = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    grey.save(os.path.join(tex_dir, 'collision.png'))
    for cname in sorted({c[0] for c in COLLISION.values()}):
        mtl += [f'newmtl {cname}', 'map_Kd collision.png', '']

    # ---- OBJ writer
    lines = ['mtllib ' + name + '.mtl']
    counts = {'v': 0, 'vt': 0, 'vn': 0}
    blocks = []  # (block name, material)

    def key(p):
        return tuple(round(c, 6) for c in p)  # LEV-grid positions are multiples of 1/64

    def write_triblock(bname, mat, tri, normal):
        """tri: 3 vertex dicts with pos (CTE units), uv (0..1, v up), col (0..1). Emits 4 sub-triangles."""
        a, b, c = tri
        ab, bc, ca = lerp(a, b, .5), lerp(b, c, .5), lerp(c, a, .5)
        verts = [a, b, c, ab, bc, ca]
        base_v, base_t = counts['v'], counts['vt']
        for v in verts:
            p = key(v['pos'])
            lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
            lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 6; counts['vt'] += 6; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        for f in ((0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)):
            lines.append('f ' + ' '.join(f'{base_v + i + 1}/{base_t + i + 1}/{n}' for i in f))
        blocks.append((bname, mat))

    def write_quadblock(bname, mat, quad, normal):
        """quad: 4 vertex dicts in winding order. Emits the 3x3 grid as 4 quads (CTE reads the
        corner quads in grid order: p0, p2, p6, p8)."""
        q0, q1, q2, q3 = quad
        c = {k: (q0[k] + q1[k] + q2[k] + q3[k]) / 4 for k in q0}
        # grid: 0 1 2 / 3 4 5 / 6 7 8 with corners 0=q0, 2=q1, 8=q2, 6=q3
        g = [q0, lerp(q0, q1, .5), q1, lerp(q3, q0, .5), c, lerp(q1, q2, .5), q3, lerp(q2, q3, .5), q2]
        base_v, base_t = counts['v'], counts['vt']
        for v in g:
            p = key(v['pos'])
            lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
            lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 9; counts['vt'] += 9; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        # each sub-quad wound like the quad: q0 -> q1 -> q2 -> q3 is 0 -> 2 -> 8 -> 6
        for f in ((0, 1, 4, 3), (1, 2, 5, 4), (3, 4, 7, 6), (4, 5, 8, 7)):
            lines.append('f ' + ' '.join(f'{base_v + i + 1}/{base_t + i + 1}/{n}' for i in f))
        blocks.append((bname, mat))

    def write_grid(bname, mat, faces, normal):
        """faces: 4 quads in cyclic order around the shared centre, each starting at it. CTE wants
        the face order q0, q1, q2, q3 with q0 next to q1 and q2, and q3 opposite q0."""
        base_v, base_t = counts['v'], counts['vt']
        for face in (faces[0], faces[1], faces[3], faces[2]):
            for v in face:
                p = key(v['pos'])
                lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
                lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 16; counts['vt'] += 16; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        for f in range(4):
            lines.append('f ' + ' '.join(f'{base_v + 4 * f + i + 1}/{base_t + 4 * f + i + 1}/{n}' for i in range(4)))
        blocks.append((bname, mat))

    def tri_normal(tri):
        a, b, c = (np.array(v['pos']) for v in tri)
        n = np.cross(b - a, c - a)
        l = np.linalg.norm(n)
        return n / l if l > 0 else np.array([0.0, 1.0, 0.0])

    # ---- render geometry
    stats = defaultdict(int)
    render = []
    for ti, (mi, tri) in enumerate(course.model.triangles()):
        m = course.model.mats[mi]
        sx, sy, tx, ty = m.texmtx
        repeat_s, repeat_t = (m.teximage >> 16) & 1, (m.teximage >> 17) & 1
        flip_s, flip_t = (m.teximage >> 18) & 1, (m.teximage >> 19) & 1
        vs = []
        for pos, uv, col, nrm in tri:
            kx, ky = tiles[mi]
            u = (uv[0] * sx / m.width + tx) / kx
            v = (uv[1] * sy / m.height + ty) / ky
            vs.append({'pos': np.array(to_cte(np.array(pos) * MODEL_TO_WORLD)), 'uv': np.array([u, v]),
                       'col': np.array(col) / 255.0 * 0.5})
        if area(vs) < 1e-6:
            stats['degenerate'] += 1
            continue
        for poly in split_by_tiles(vs):
            for piece in fan(poly):
                if area(piece) < 1e-6:
                    continue
                cu = np.mean([p['uv'][0] for p in piece]); cv = np.mean([p['uv'][1] for p in piece])
                tu, tv = math.floor(cu), math.floor(cv)
                out = []
                for p in piece:
                    fu, fv = p['uv'][0] - tu, p['uv'][1] - tv
                    if not repeat_s: fu = min(max(p['uv'][0], 0), 1)
                    if not repeat_t: fv = min(max(p['uv'][1], 0), 1)
                    if flip_s and tiles[mi][0] == 1 and tu % 2: fu = 1 - fu
                    if flip_t and tiles[mi][1] == 1 and tv % 2: fv = 1 - fv
                    out.append({'pos': p['pos'], 'uv': np.array([fu, 1 - fv]), 'col': p['col']})
                render.append((out, mi))
    refined = [(tri, mi) for tri, mi in refine(render, max_edge, weld_tol=0.25 / 64) if area(tri) >= 1e-9]
    quads, tris = pair_quads(refined, max_dev=2 / 64)
    grids, quads = group_grids(quads)
    for faces, mi in grids:
        stats['render grids'] += 1
        normal = sum(tri_normal(f[:3]) for f in faces)
        write_grid(f'g{stats["render grids"]:05d}', mat_names[mi], faces, normal / max(np.linalg.norm(normal), 1e-9))
    for quad, mi in quads:
        stats['render quads'] += 1
        quad = orient_quad(quad)
        Q = [np.asarray(v['pos'], float) for v in quad]
        normal = np.cross(Q[2] - Q[0], Q[3] - Q[1])  # the diagonals: defined even with a straight corner
        write_quadblock(f'q{stats["render quads"]:05d}', mat_names[mi], quad, normal / max(np.linalg.norm(normal), 1e-9))
    for tri, mi in tris:
        stats['render'] += 1
        write_triblock(f'r{stats["render"]:05d}', mat_names[mi], tri, tri_normal(tri))

    # ---- collision geometry
    m = course.nkm
    cps = mkdsmap.checkpoints(m)
    (x1, z1), (x2, z2), _, _ = cps[0]
    route = mkdsmap.cpu_route(m)
    start_pos, start_rot = mkdsmap.start_positions(m)[0]
    line_a, line_b = np.array([x1, z1]), np.array([x2, z2])
    along = (line_b - line_a) / np.linalg.norm(line_b - line_a)
    fwd = np.array([-along[1], along[0]])
    if np.dot(np.array([route[0][0][0], route[0][0][2]]) - line_a, fwd) < 0:
        fwd = -fwd
    seg_len = np.linalg.norm(line_b - line_a)

    def near_line(p):
        s = np.dot(np.array([p[0], p[2]]) - line_a, along)
        return -50 <= s <= seg_len + 50 and abs(p[1] - start_pos[1]) < 150

    start_quads, end_quads = [], []
    collision = []
    for a, b, c, nrm, attr in course.kcl:
        ty = mkdsmap.kcl_type(attr)
        info = COLLISION.get(ty)
        if info is None:
            stats[f'kcl skipped type {ty:#x}'] += 1
            continue
        cname, _, _, pathable = info
        tri = [{'pos': np.array(p), 'uv': np.array(uv), 'col': np.array([.5, .5, .5])}
               for p, uv in zip((a, b, c), ((0, 0), (1, 0), (0, 1)))]
        pieces = [tri]
        crossing = pathable and all(near_line(p) for p in (a, b, c))
        if crossing:
            d = [np.dot(np.array([p[0], p[2]]) - line_a, fwd) for p in (a, b, c)]
            if min(d) < -1e-3 and max(d) > 1e-3:
                for v in tri:
                    v['d'] = np.array([np.dot(np.array([v['pos'][0], v['pos'][2]]) - line_a, fwd)])
                pieces = [p for side in (True, False) for p in fan(clip(tri, 'd', 0, 0.0, side)) if len(p) == 3]
                for p in pieces:
                    for v in p: v.pop('d', None)
                for v in tri: v.pop('d', None)
        collision += [(p, (cname, crossing)) for p in pieces if area(p) >= 1e-6]
    # Collision is checked by the game on integer LEV coordinates: snap to that grid first, then
    # close the T-junctions the snapping and the source leave, so the mesh tested here is the
    # mesh the game tests.
    def to_grid(p):
        return np.round(np.asarray(to_cte(p)) * 64) / 64
    snapped = []
    for piece, meta in refine(collision, max_edge / scale, weld_tol=0.5, tjunction_tol=1.0):
        snapped.append(([{'pos': to_grid(v['pos']), 'mk': np.asarray(v['pos'], float), 'uv': v['uv'], 'col': v['col']}
                         for v in piece], meta))
    flipped = 0
    for piece, (cname, crossing) in snapped:
            for v in piece:
                v['pos'] = np.round(v['pos'] * 64) / 64
            if area(piece) < 1e-9:
                stats['collision degenerate on the LEV grid'] += 1
                continue
            mk_n = np.cross(piece[1]['mk'] - piece[0]['mk'], piece[2]['mk'] - piece[0]['mk'])
            if np.dot(tri_normal(piece), mk_n) < 0:
                # a sliver the grid turned over: its neighbours now overlap the strip it covered,
                # so dropping it leaves no gap, and the game would test it facing the wrong way
                stats['collision slivers turned over by the LEV grid, dropped'] += 1
                continue
            stats['collision'] += 1
            bname = f'c{stats["collision"]:05d}'
            cte = [{'pos': v['pos'], 'uv': v['uv'], 'col': v['col']} for v in piece]
            write_triblock(bname, cname, cte, tri_normal(cte))
            if crossing:
                d = [np.dot(np.array([v['mk'][0], v['mk'][2]]) - line_a, fwd) for v in piece]
                touches = min(abs(x) for x in d) < 1e-2
                if touches and np.mean(d) > 0: start_quads.append(bname)
                if touches and np.mean(d) < 0: end_quads.append(bname)

    # ---- probe points for CTR_DEV_COLL_PROBE_IN: every 64 LEV units on every MKDS ground layer
    lev = 64 * scale
    step = 64 / lev
    pts = []
    for a, b, c, nrm, attr in course.kcl:
        info = COLLISION.get(mkdsmap.kcl_type(attr))
        if info is None or not (info[2] & GROUND) or abs(nrm[1]) < 0.3:
            continue
        xs, zs = (a[0], b[0], c[0]), (a[2], b[2], c[2])
        for gx in np.arange(math.ceil(min(xs) / step) * step, max(xs), step):
            for gz in np.arange(math.ceil(min(zs) / step) * step, max(zs), step):
                d = lambda u, v: (v[0] - u[0]) * (gz - u[2]) - (v[2] - u[2]) * (gx - u[0])
                e = (d(a, b), d(b, c), d(c, a))
                if not (min(e) > 0 or max(e) < 0):
                    continue
                y = a[1] - (nrm[0] * (gx - a[0]) + nrm[2] * (gz - a[2])) / nrm[1]
                p = (np.array([gx, y, gz]) - centre) * lev
                pts.append('%d %d %d' % tuple(np.round(p).astype(int)))
    with open(os.path.join(out_dir, name + '_probe.txt'), 'w') as f:
        f.write('\n'.join(pts) + '\n')
    stats['probe points'] = len(pts)

    with open(os.path.join(out_dir, name + '.obj'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(out_dir, name + '.mtl'), 'w') as f:
        f.write('\n'.join(mtl) + '\n')

    # ---- presets
    pre = os.path.join(out_dir, name + '_presets')
    os.makedirs(pre, exist_ok=True)
    mats = {'header': 3, 'materials': sorted(set(mat_names.values()) | {c[0] for c in COLLISION.values()})}
    for i, mname in mat_names.items():
        mats[mname + '_quadflags'] = 0  # drawn only
        mats[mname + '_checkpoint'] = False
        # fences, rails and signs MKDS draws from both sides (polygon attribute bit 6, back faces)
        mats[mname + '_drawflags'] = bool((course.model.mats[i].polygon_attr >> 6) & 1)
    for cname, terrain, flags, pathable in COLLISION.values():
        mats[cname + '_terrain'] = terrain
        mats[cname + '_quadflags'] = flags
        mats[cname + '_checkpoint'] = True
        mats[cname + '_checkpointPathable'] = pathable
    json.dump(mats, open(os.path.join(pre, 'material.json'), 'w'), indent=1)

    # 8 spawns in two staggered columns behind the start, like MKDS's grid
    # a kart at game angle a faces (sin a, cos a) in x/z, and VehBirth turns the spawn's rot.y by
    # a quarter turn before using it
    heading = math.degrees(math.atan2(fwd[0], fwd[1])) - 90
    spawns = []
    side = np.array([-fwd[1], fwd[0]])
    for i in range(8):
        row, col = divmod(i, 2)
        off2 = -fwd * (60 + 70 * row + 35 * col) + side * (60 if col else -60)
        p = np.array([start_pos[0] + off2[0], start_pos[1] + 10, start_pos[2] + off2[1]])
        spawns.append({'pos': dict(zip('xyz', map(float, to_cte(p)))), 'rot': {'x': 0.0, 'y': heading, 'z': 0.0}})
    json.dump({'header': 0, 'spawn': spawns}, open(os.path.join(pre, 'spawn.json'), 'w'), indent=1)

    # sky: a gradient from the course's own skybox texture, horizon colour to zenith colour
    top, horizon = course.sky_colours()
    col = lambda c: {'r': c[0] / 255, 'g': c[1] / 255, 'b': c[2] / 255, 'a': 1.0}
    level = {'header': 1, 'configFlags': 1, 'clearColor': col(horizon),
             'skyGradient': [{'posFrom': 0.0, 'posTo': 60.0, 'colorFrom': col(horizon), 'colorTo': col((np.array(top) + horizon) / 2)},
                             {'posFrom': 60.0, 'posTo': 160.0, 'colorFrom': col((np.array(top) + horizon) / 2), 'colorTo': col(top)},
                             {'posFrom': 160.0, 'posTo': 400.0, 'colorFrom': col(top), 'colorTo': col(top)}]}
    json.dump(level, open(os.path.join(pre, 'level.json'), 'w'), indent=1)

    path = {'header': 2, 'pathCount': 1, 'path0': {'index': 0, 'hasLeft': False, 'hasRight': False,
                                                    'quadStart': start_quads, 'quadEnd': end_quads, 'quadIgnore': []}}
    json.dump(path, open(os.path.join(pre, 'path.json'), 'w'), indent=1)

    # ---- CPU route: three lanes across the MKDS route, in CTE units, starting at the finish line
    pts = np.array([p for p, _ in route]); radii = np.array([r for _, r in route])
    seg = np.roll(pts, -1, 0) - pts
    lens = np.linalg.norm(seg, axis=1)
    total = lens.sum()
    count = max(64, min(400, int(total / 40)))
    sp = np.array([start_pos[0], start_pos[2]])
    # arc position of the finish line: closest point of the loop to the start
    best = (1e18, 0.0)
    acc = 0.0
    for i in range(len(pts)):
        a2, d2 = pts[i][[0, 2]], seg[i][[0, 2]]
        t = np.clip(np.dot(sp - a2, d2) / max(np.dot(d2, d2), 1e-9), 0, 1)
        dd = np.linalg.norm(a2 + d2 * t - sp)
        if dd < best[0]: best = (dd, acc + t * lens[i])
        acc += lens[i]
    cum = np.concatenate([[0], np.cumsum(lens)])
    lanes = [[], [], []]
    for k in range(count):
        s_ = (best[1] + total * k / count) % total
        i = int(np.searchsorted(cum, s_, side='right') - 1)
        t = (s_ - cum[i]) / lens[i]
        p = pts[i] + seg[i] * t
        r = radii[i] + (radii[(i + 1) % len(pts)] - radii[i]) * t
        d = seg[i] / lens[i]
        side = np.array([-d[2], 0.0, d[0]])
        side /= max(np.linalg.norm(side), 1e-9)
        for lane, off in enumerate((0.0, -0.4, 0.4)):
            lanes[lane].append([float(c) for c in to_cte(p + side * r * off)])
    json.dump({'lanes': lanes}, open(os.path.join(out_dir, name + '_nav.json'), 'w'))
    stats['nav nodes'] = count

    stats['start quads'] = len(start_quads)
    stats['end quads'] = len(end_quads)
    allpos = np.array([v for t in course.kcl for v in t[:3]])
    ext = np.abs((allpos - centre) * scale).max()
    stats['max |coord| (limit 512)'] = round(float(ext), 1)
    return dict(stats)


def main():
    ap = argparse.ArgumentParser(description='Convert a Mario Kart DS course into an installed custom track package.')
    ap.add_argument('rom', help='the player\'s own Mario Kart DS ROM (.nds)')
    ap.add_argument('course', help='course archive name under data/Course, e.g. cross_course')
    ap.add_argument('--title', required=True, help='title shown on the custom tracks page')
    ap.add_argument('--store', default=None,
                    help='packages folder to install into (default <work>/packages, where check_track.ps1 looks); '
                         'a game install\'s is assets/tracks/packages')
    ap.add_argument('--work', default=None, help='scratch folder for the OBJ, LEV and probe points (default C:/t/mkds-<course>)')
    ap.add_argument('--cte', default='C:/t/cte/cte_headless.exe', help='cte_headless.exe (tools/tracks/cte/build.sh)')
    ap.add_argument('--version', default='1.0.0', help='package version (semver)')
    ap.add_argument('--scale', type=float, default=1 / 12, help='MKDS units to CrashTeamEditor units')
    ap.add_argument('--max-edge', type=float, default=24.0,
                    help='longest triangle edge in CTE units (x64 = LEV units); retail quadblocks are ~900-1600 LEV units')
    a = ap.parse_args()

    work = a.work or f'C:/t/mkds-{a.course}'
    a.store = a.store or os.path.join(work, 'packages')
    os.makedirs(a.store, exist_ok=True)
    name = a.course
    files = nds.nitrofs(open(a.rom, 'rb').read())
    print('convert', convert(Course(files, a.course), work, name, a.scale, a.max_edge))

    lev_dir = os.path.join(work, 'lev')
    os.makedirs(lev_dir, exist_ok=True)  # CTE saves into an existing folder only
    # cte_headless links MinGW32's Python runtime
    env = dict(os.environ, PATH='C:/msys64/mingw32/bin' + os.pathsep + os.environ.get('PATH', ''))
    r = subprocess.run([a.cte, os.path.join(work, name + '.obj'), lev_dir], env=env, capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        sys.exit('cte_headless failed:\n' + r.stderr)

    lev = os.path.join(lev_dir, name + '.lev')
    patched = os.path.join(lev_dir, name + '_patched.lev')
    print('lev_patch', lev_patch.patch(lev, os.path.join(work, name + '_nav.json'), patched))
    winding = winding_check.check(os.path.join(work, name + '.obj'), patched)
    print('winding', winding)
    if winding['flipped'] or winding['unmatched']:
        sys.exit('CrashTeamEditor turned drawn blocks over (they would vanish from the front)')
    pin = package.build(patched, os.path.join(lev_dir, name + '.vrm'), a.title, 'mkds/' + a.course, a.version, a.store)
    # a re-conversion replaces the revision this work folder installed last
    pin_file = os.path.join(work, 'pin.txt')
    old = open(pin_file).read().strip() if os.path.exists(pin_file) else ''
    if old and old != pin and os.path.isdir(os.path.join(a.store, old)):
        shutil.rmtree(os.path.join(a.store, old))
    with open(pin_file, 'w') as f:
        f.write(pin + '\n')
    print('pin', pin)


if __name__ == '__main__':
    main()
