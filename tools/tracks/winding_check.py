"""Check that CrashTeamEditor kept every drawn block facing the way the converter wrote it.

  python tools/tracks/winding_check.py <work>/<course>.obj <work>/lev/<course>_patched.lev

The game culls back faces, so a block wound the wrong way vanishes when seen from its front
(the road under the kart turned black). Blocks are matched by their corner positions; prints the
count of drawn blocks whose LEV triangles face against the OBJ normal and exits 1 if any do."""
import itertools, struct, sys
import numpy as np

# LEV index slots of the eight hi-LOD triangles (levtri.py); a triblock uses the first four
THIN_BLOCK = 2  # LEV units
TRIS = [(0, 4, 5), (4, 6, 5), (6, 4, 1), (5, 6, 2), (8, 6, 7), (7, 3, 8), (1, 7, 6), (2, 6, 8)]


def obj_blocks(path):
    """-> {sorted corner positions in LEV units: (name, normal)} for drawn blocks (q, g, r)."""
    V, VN, out, cur = [], [], {}, None
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            V.append(np.round(np.array(list(map(float, p[1:4]))) * 64).astype(int))
        elif p[0] == 'vn':
            VN.append(np.array(list(map(float, p[1:4]))))
        elif p[0] == 'o':
            cur = [p[1], [], None]
            if p[1][0] in 'qgr':
                out.setdefault('_list', []).append(cur)
        elif p[0] == 'f' and cur is not None:
            idx = [tuple(int(x) for x in t.split('/')) for t in p[1:]]
            cur[1] += [tuple(V[i[0] - 1]) for i in idx]
            cur[2] = VN[idx[0][2] - 1]
    blocks = {}
    for name, pts, n in out.get('_list', []):
        # corners: the points farthest along the block's extent (hull of the outer ring)
        blocks[name] = (set(pts), n)
    return blocks


def check(obj_path, lev_path):
    blocks = obj_blocks(obj_path)
    by_pts = {}  # a list: a two-sided surface is two blocks on one set of corners, facing apart
    for name, (pts, n) in blocks.items():
        by_pts.setdefault(frozenset(pts), []).append((name, n))
    by_point = {}
    for key, found in by_pts.items():
        for p in key:
            by_point.setdefault(tuple(int(c) for c in p), []).append((key, found))

    def near(key):
        """Blocks with these corners, give or take one LEV unit: an OBJ position that falls on a half can round either way, and two corners of a tiny block can round to one."""
        p = next(iter(key))
        for offset in itertools.product((-1, 0, 1), repeat=3):
            for pts, found in by_point.get(tuple(a + b for a, b in zip(p, offset)), ()):
                if len(pts) >= len(key) and all(any(max(abs(a - b) for a, b in zip(q, r)) <= 1 for r in pts) for q in key):
                    return found
        return None

    lev = open(lev_path, 'rb').read()[4:]
    mesh = struct.unpack_from('<I', lev, 0)[0]
    nq, nv, _, qoff, voff = struct.unpack_from('<5I', lev, mesh)
    V = np.array([struct.unpack_from('<3h', lev, voff + 16 * i) for i in range(nv)], float)
    drawn = flipped = unmatched = 0
    names = []
    for q in range(nq):
        o = qoff + 0x5c * q
        if struct.unpack_from('<H', lev, o + 0x12)[0]:
            continue  # collision-only
        drawn += 1
        idx = struct.unpack_from('<9H', lev, o)
        tri = idx[2] == idx[3]
        key = frozenset(tuple(int(c) for c in V[i]) for i in set(idx))
        P = V[list(idx)]
        # the game's triangles are wound clockwise seen from the front: their cross product points back
        back = sum(np.cross(P[b] - P[a], P[c] - P[a]) for a, b, c in (TRIS[:4] if tri else TRIS))
        if np.linalg.norm(back) < THIN_BLOCK * 4 * np.ptp(P, axis=0).max():
            continue  # thinner than the grid: it has no facing there, and draws next to nothing
        hit = by_pts.get(key) or near(key)
        if hit is None:
            unmatched += 1
            continue
        if all(np.dot(back, n) > 0 for _, n in hit):
            flipped += 1
            names.append(hit[0][0])
    return {'drawn': drawn, 'flipped': flipped, 'unmatched': unmatched, 'examples': names[:8]}


if __name__ == '__main__':
    r = check(sys.argv[1], sys.argv[2])
    print(r)
    sys.exit(1 if r['flipped'] or r['unmatched'] else 0)
