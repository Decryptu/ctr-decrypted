"""Finish a CrashTeamEditor LEV for a converted track (docs/TRACK-CONVERTER.md):
- collision-only quadblocks draw nothing, and their BSP subtree is cleared from the visibility lists;
- bot paths: a 3-entry LevNavTable, each a NavHeader plus NavFrames (CrashTeamEditor writes none);
- the level is marked depth-tested (LEVEL_CONFIG_DEPTH_TESTED): its art was made for a depth buffer;
- and as having one texture per LOD (LEVEL_CONFIG_ONE_TEXTURE_LOD), so the extended draw distance does not
  subdivide it farther;
- every collision triangle's normal scale is recomputed from the LEV's integer vertices, and the
  triangles the grid turned away from their block get none."""
import json, math, struct, sys

FP_ONE_GEO = 64
OFF_CONFIG_FLAGS, OFF_NUM_CHECKPOINTS, OFF_CHECKPOINTS, OFF_NAV_TABLE = 0xDC, 0x148, 0x14C, 0x188
LEVEL_CONFIG_DEPTH_TESTED = 0x80000000  # include/platform/native_gpu.h
LEVEL_CONFIG_ONE_TEXTURE_LOD = 0x40000000  # include/platform/native_draw_distance.h
# The triangles COLL_FIXED_QUADBLK_GetNormVecs_HiLOD / _LoLOD build, in triNormalVecDividend order
NORMAL_TRIS = [(0, 4, 5), (4, 6, 5), (6, 4, 1), (5, 6, 2), (8, 6, 7), (7, 3, 8), (1, 7, 6), (2, 6, 8), (0, 1, 2), (1, 3, 2)]
TURNED_COS = 0.9  # fix_normals: about 25 degrees


def _s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def game_normal_cross(a, b, c):
    """The GTE OP product COLL_FIXED_TRIANGL_GetNormVec starts from (MAC1..3)."""
    d = [_s16(c[k] - a[k]) for k in range(3)]
    e = [b[k] - a[k] for k in range(3)]
    return [d[1] * e[2] - d[2] * e[1], d[2] * e[0] - d[0] * e[2], d[0] * e[1] - d[1] * e[0]]


def game_normal_length(m, lod_shift, dividend, shift):
    n = [_s16(((x >> lod_shift) * dividend) >> shift) for x in m]
    return math.sqrt(sum(x * x for x in n)) / 4096


def exact_normal_scale(verts, tris):
    """One triNormalVecBitShift per quadblock and one dividend per triangle, so the game's fixed-point
    normal ((cross >> lodShift) * dividend >> bitShift) comes out 4096 long. CrashTeamEditor derives
    them from float positions before they are rounded to the LEV grid, and gives a triblock's four
    triangles one shared value: on slivers a few units wide that leaves normals far too short, and a
    short normal shrinks the distance the kart's sphere sweep measures, so it stops against nothing.
    Returns (shift, dividends, worst error); a too-short normal counts double, being the harmful side."""
    crosses = []
    for i, (t, lod) in enumerate(tris):
        m = game_normal_cross(*(verts[k] for k in t))
        crosses.append((i, [x >> lod for x in m], lod, m))
    best = None
    for shift in range(20):
        div = [0] * 10
        worst = 0.0
        for i, m, lod, raw in crosses:
            mag = math.sqrt(sum(x * x for x in m))
            if mag == 0:
                continue
            div[i] = max(1, min(32767, int(round(4096 * (1 << shift) / mag))))
            if any(abs(x * div[i]) >= 1 << 31 for x in m):
                worst = math.inf
                break
            length = game_normal_length(raw, lod, div[i], shift)
            worst = max(worst, math.inf if length == 0 else (1 / length - 1) * 2 if length < 1 else length - 1)
        if best is None or worst < best[2]:
            best = (shift, div, worst)
    return best


def _unit(m):
    length = math.sqrt(sum(x * x for x in m))
    return [x / length for x in m] if length else None


def fix_normals(data, mesh):
    """-> (worst error, quadblocks over 2%, sub-triangles given no normal), over the blocks collision
    tests (GROUND or COLLISION_SURFACE).
    A high-detail triangle turned more than TURNED_COS away from the half of the block it divides
    gets no normal (dividend 0), so the game ignores it like a zero-area one. CrashTeamEditor rounds
    a block's edge midpoints to the grid, and on a sliver a unit or two wide that stands a piece up:
    a wall that tall across the road, which stops the kart dead. Its neighbours cover the strip."""
    nq, nv, _, qoff, voff = struct.unpack_from('<5I', data, mesh)
    verts = [struct.unpack_from('<3h', data, voff + 16 * i) for i in range(nv)]
    worst = []
    ignored = 0
    for q in range(nq):
        o = qoff + 0x5c * q
        if not struct.unpack_from('<H', data, o + 0x12)[0] & 0x3000:
            continue
        idx = struct.unpack_from('<9H', data, o)
        tri = idx[2] == idx[3]  # a triblock: COLL_*_TestTriangles uses only the first four
        tris = [(tuple(idx[k] for k in t), 0) for t in NORMAL_TRIS[:8]] + [(tuple(idx[k] for k in t), 2) for t in NORMAL_TRIS[8:]]
        if tri:
            tris = tris[:4] + [((idx[0], idx[0], idx[0]), 0)] * 4 + tris[8:]
        halves = [_unit(game_normal_cross(*(verts[k] for k in tris[8 + h][0]))) for h in range(2)]
        for i in range(8):
            n = _unit(game_normal_cross(*(verts[k] for k in tris[i][0])))
            half = halves[i // 4]  # triangles 0-3 divide (0,1,2), 4-7 divide (1,3,2)
            if n and half and sum(a * b for a, b in zip(n, half)) < TURNED_COS:
                tris[i] = ((idx[0], idx[0], idx[0]), 0)
                ignored += 1
        shift, div, err = exact_normal_scale(verts, tris)
        data[o + 0x3f] = shift
        struct.pack_into('<10h', data, o + 0x48, *div)
        worst.append(err)
    return max(worst), sum(e > 0.02 for e in worst), ignored


def patch(lev_path, nav_path, out_path):
    lev = open(lev_path, 'rb').read()
    data_len = struct.unpack_from('<I', lev, 0)[0]
    data = bytearray(lev[4:4 + data_len])
    nptr = struct.unpack_from('<I', lev, 4 + data_len)[0] // 4
    ptrs = list(struct.unpack_from(f'<{nptr}I', lev, 8 + data_len))
    tail = lev[8 + data_len + 4 * nptr:]
    assert not tail, 'unexpected bytes after the pointer map'

    ncp = struct.unpack_from('<I', data, OFF_NUM_CHECKPOINTS)[0]
    cpo = struct.unpack_from('<I', data, OFF_CHECKPOINTS)[0]
    cps = [struct.unpack_from('<3h', data, cpo + 12 * i) for i in range(ncp)]

    # Collision blocks (the converter draws nothing but the render mesh, which has no collision
    # flags): retail hides a face by its draw mode
    # (2 bits per face at drawOrderLow bit 8 + 5*face + 3; 3 draws nothing).
    # CrashTeamEditor writes every face as drawn.
    mesh = struct.unpack_from('<I', data, 0)[0]
    nq, _, _, qoff = struct.unpack_from('<4I', data, mesh)
    normal_error, normal_bad, normal_ignored = fix_normals(data, mesh)
    hidden = 0
    for q in range(nq):
        o = qoff + 0x5c * q
        if struct.unpack_from('<H', data, o + 0x12)[0] & 0x3000:  # GROUND or COLLISION_SURFACE: collision-only blocks
            dl = struct.unpack_from('<I', data, o + 0x14)[0]
            for face in range(4):
                dl |= 3 << (8 + 5 * face + 3)
            struct.pack_into('<I', data, o + 0x14, dl)
            hidden += 1

    # The BSP keeps collision-only blocks in their own subtree (cte bsp.cpp root split): clear
    # every node of it in every visibility list, so the renderer never walks those blocks. The
    # game reads bit 31 - (id & 31) of word id >> 5 (RenderLists_IsVisible).
    bsp, nbsp = struct.unpack_from('<2I', data, mesh + 0x18)
    nodes = [struct.unpack_from('<HH', data, bsp + 0x20 * i) for i in range(nbsp)]
    assert all(nid == i for i, (_, nid) in enumerate(nodes)), 'BSP node ids are not array indexes'
    collision_only = {}
    def only_collision(i):
        flag = nodes[i][0]
        o = bsp + 0x20 * i
        if flag & 1:
            n, qp = struct.unpack_from('<iI', data, o + 0x18)
            ok = all(struct.unpack_from('<H', data, qp + 0x5c * k + 0x12)[0] & 0x3000 for k in range(n))
        else:
            ok = True
            for c in struct.unpack_from('<2H', data, o + 0x18):
                if c != 0xffff:
                    ok = only_collision(c & 0x3fff) and ok
        collision_only[i] = ok
        return ok
    only_collision(0)
    vis_lists = set()
    for q in range(nq):
        pvs = struct.unpack_from('<I', data, qoff + 0x5c * q + 0x44)[0]
        if pvs:
            vis_lists.add(struct.unpack_from('<I', data, pvs)[0])
    culled = [i for i, ok in collision_only.items() if ok and i != 0]
    for vl in vis_lists:
        for i in culled:
            o = vl + 4 * (i >> 5)
            struct.pack_into('<I', data, o, struct.unpack_from('<I', data, o)[0] & ~(1 << (31 - (i & 31))))

    lanes = json.load(open(nav_path))['lanes']
    while len(data) % 4: data.append(0)
    table = len(data)
    data += bytes(12)
    for lane_i, lane in enumerate(lanes[:3]):
        pos = [tuple(int(round(c * FP_ONE_GEO)) for c in p) for p in lane]
        n = len(pos)
        head = len(data)
        struct.pack_into('<I', data, table + 4 * lane_i, head)
        ptrs.append(table + 4 * lane_i)
        hdr = bytearray(0x4c)
        struct.pack_into('<hhi', hdr, 0, -0x1303, n, pos[0][1])
        data += hdr
        for i in range(n + 1):  # the engine reads one frame past the last (BOTS 'last'); repeat the first
            p, q = pos[i % n], pos[(i + 1) % n]
            dx, dy, dz = q[0] - p[0], q[1] - p[1], q[2] - p[2]
            yaw = int(round(math.atan2(dx, dz) * 4096 / (2 * math.pi))) & 0xfff
            near = min(range(ncp), key=lambda c: sum((cps[c][k] - p[k]) ** 2 for k in range(3))) if ncp else 0
            fr = bytearray(0x14)
            struct.pack_into('<3h', fr, 0, *p)
            fr[7] = yaw >> 4
            struct.pack_into('<hhhhBB', fr, 0x0a, min(int(math.sqrt(dx*dx+dy*dy+dz*dz)), 32767),
                             min(int(math.sqrt(dx*dx+dz*dz)), 32767), 0, 0x7fff, near, 0)
            data += fr
    struct.pack_into('<I', data, OFF_NAV_TABLE, table)
    # MKDS draws with a depth buffer: billboards, fences and signs hang a few units in front of the
    # walls they are on, which the per-block ordering table sort swaps as the camera moves. And
    # CrashTeamEditor writes one layout to all four LOD slots of a texture group.
    struct.pack_into('<I', data, OFF_CONFIG_FLAGS,
                     struct.unpack_from('<I', data, OFF_CONFIG_FLAGS)[0] | LEVEL_CONFIG_DEPTH_TESTED | LEVEL_CONFIG_ONE_TEXTURE_LOD)
    out = struct.pack('<I', len(data)) + bytes(data) + struct.pack('<I', 4 * len(ptrs)) + struct.pack(f'<{len(ptrs)}I', *ptrs)
    open(out_path, 'wb').write(out)
    return {'lanes': len(lanes), 'checkpoints': ncp, 'hidden quadblocks': hidden,
            'culled bsp nodes': len(culled), 'of': nbsp, 'vis lists': len(vis_lists),
            'worst collision normal error': round(normal_error, 4), 'quadblocks over 2%': normal_bad,
            'collision triangles turned by the grid, ignored': normal_ignored}


if __name__ == '__main__':
    print(patch(*sys.argv[1:4]))
