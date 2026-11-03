"""Finish a CrashTeamEditor LEV for a converted track (docs/TRACK-CONVERTER.md):
- collision-only quadblocks draw nothing, and their BSP subtree is cleared from the visibility lists;
- bot paths: a 3-entry LevNavTable, each a NavHeader plus NavFrames (CrashTeamEditor writes none)."""
import json, math, struct, sys

FP_ONE_GEO = 64
OFF_NUM_CHECKPOINTS, OFF_CHECKPOINTS, OFF_NAV_TABLE = 0x148, 0x14C, 0x188


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
    out = struct.pack('<I', len(data)) + bytes(data) + struct.pack('<I', 4 * len(ptrs)) + struct.pack(f'<{len(ptrs)}I', *ptrs)
    open(out_path, 'wb').write(out)
    return {'lanes': len(lanes), 'checkpoints': ncp, 'hidden quadblocks': hidden,
            'culled bsp nodes': len(culled), 'of': nbsp, 'vis lists': len(vis_lists)}


if __name__ == '__main__':
    print(patch(*sys.argv[1:4]))
