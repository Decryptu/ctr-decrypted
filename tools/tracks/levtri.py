"""LEV collision triangles (the 8 hi-LOD triangles COLL_FIXED_QUADBLK_TestTriangles tests) and point lookup."""
import struct
TRIS = [(0, 4, 5), (4, 6, 5), (6, 4, 1), (5, 6, 2), (8, 6, 7), (7, 3, 8), (1, 7, 6), (2, 6, 8)]

def load(path):
    lev = open(path, 'rb').read(); dl = struct.unpack_from('<I', lev, 0)[0]; D = lev[4:4 + dl]
    mesh = struct.unpack_from('<I', D, 0)[0]; nq, nv, _, qoff, voff = struct.unpack_from('<5I', D, mesh)
    V = [struct.unpack_from('<3h', D, voff + 16 * i) for i in range(nv)]
    quads = []
    for q in range(nq):
        o = qoff + 0x5c * q
        quads.append((struct.unpack_from('<9H', D, o), struct.unpack_from('<H', D, o + 0x12)[0], struct.unpack_from('<6h', D, o + 0x2c)))
    return V, quads

def containing(V, quads, x, z, flags=0x3000):
    out = []
    for qi, (idx, fl, bb) in enumerate(quads):
        if not fl & flags or not (bb[0] <= x <= bb[3] and bb[2] <= z <= bb[5]): continue
        for t in (TRIS if idx[2] != idx[3] else TRIS[:4]):
            a, b, c = (V[idx[i]] for i in t)
            e = [(b[0]-a[0])*(z-a[2])-(b[2]-a[2])*(x-a[0]), (c[0]-b[0])*(z-b[2])-(c[2]-b[2])*(x-b[0]), (a[0]-c[0])*(z-c[2])-(a[2]-c[2])*(x-c[0])]
            if min(e) >= 0 or max(e) <= 0: out.append((qi, t, (a, b, c)))
    return out
