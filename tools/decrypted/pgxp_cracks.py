"""Find PGXP cracks in a polygon dump (CTR_DEV_DUMP_FRAME).

Two polygons that share a corner in retail (same integer screen vertex) must
also share it when drawn, or the edge between them opens a crack. This lists
retail vertices that were drawn at more than one position, grouped by the LOD
bucket tags of the polygons involved, and the biggest disagreements.

Usage: python tools/decrypted/pgxp_cracks.py <polys-NNNNNN.txt> [--min 0.01]
"""
import argparse
import collections
import sys

BUCKETS = ["4x4R", "4x4L", "dynR", "dynL", "4x2R", "4x2L", "4x1R", "4x1L", "watR", "watL", "fullDyn", "clip"]


def tag_name(tag):
    return BUCKETS[tag] if 0 <= tag < len(BUCKETS) else "other"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--min", type=float, default=0.01, help="ignore disagreements below this (pixels)")
    ap.add_argument("--level-only", action="store_true", help="only polygons tagged by the level renderer")
    args = ap.parse_args()

    # (retail x, retail y, depth) -> list of (drawn x, drawn y, tag, polygon index)
    seen = collections.defaultdict(list)
    polys = 0
    for line in open(args.dump):
        f = line.split()
        if f[0] == "T":
            continue  # texture line of the polygon before
        tag, count = int(f[0]), int(f[1])
        if args.level_only and tag < 0:
            continue
        for i in range(count):
            b = 2 + i * 5
            rx, ry, dx, dy, w = int(f[b]), int(f[b + 1]), float(f[b + 2]), float(f[b + 3]), float(f[b + 4])
            if w < 0:
                continue  # missed lookups are reported by the debug tint
            # Same retail pixel and same precise depth: the same world vertex.
            seen[(rx, ry, round(w, 2))].append((dx, dy, tag, polys))
        polys += 1

    pairs = collections.Counter()
    worst = []
    split = 0
    for key, uses in seen.items():
        xs = [u[0] for u in uses]
        ys = [u[1] for u in uses]
        spread = max(max(xs) - min(xs), max(ys) - min(ys))
        if spread < args.min:
            continue
        split += 1
        tags = tuple(sorted({tag_name(u[2]) for u in uses}))
        pairs[tags] += 1
        worst.append((spread, key, uses))

    print(f"polygons={polys} retail_vertices={len(seen)} split={split}")
    print("splits by bucket combination:")
    for tags, n in pairs.most_common(15):
        print(f"  {n:6d}  {' + '.join(tags)}")
    worst.sort(reverse=True)
    print("largest:")
    for spread, key, uses in worst[:15]:
        desc = ", ".join(f"({u[0]:.2f},{u[1]:.2f}) {tag_name(u[2])}" for u in uses[:6])
        print(f"  {spread:7.3f}px retail={key}: {desc}")

    tjunctions(args.dump, args.level_only)
    return 0


def edges_of(verts):
    n = len(verts)
    if n == 3:
        return [(0, 1), (1, 2), (2, 0)]
    # Quads are dumped in MakeVertexQuad slot order (x0, x1, x3, x2), which
    # walks the outline in order.
    return [(0, 1), (1, 2), (2, 3), (3, 0)]


def tjunctions(path, level_only):
    """A vertex lying inside another polygon's edge (in retail) must lie on
    that edge when drawn too, or a sliver opens between them."""
    polys = []
    for line in open(path):
        f = line.split()
        if f[0] == "T":
            continue  # texture line of the polygon before
        tag, count = int(f[0]), int(f[1])
        if level_only and tag < 0:
            continue
        verts = []
        for i in range(count):
            b = 2 + i * 5
            verts.append((int(f[b]), int(f[b + 1]), float(f[b + 2]), float(f[b + 3])))
        polys.append((tag, verts))

    vertices = {}
    for pi, (tag, verts) in enumerate(polys):
        for v in verts:
            vertices.setdefault((v[0], v[1]), (v, tag))

    found = []
    for pi, (tag, verts) in enumerate(polys):
        for a, b in edges_of(verts):
            A, B = verts[a], verts[b]
            ex, ey = B[0] - A[0], B[1] - A[1]
            length2 = ex * ex + ey * ey
            if length2 < 4:
                continue
            minx, maxx = min(A[0], B[0]), max(A[0], B[0])
            miny, maxy = min(A[1], B[1]), max(A[1], B[1])
            for key, (V, vtag) in vertices.items():
                if not (minx <= V[0] <= maxx and miny <= V[1] <= maxy):
                    continue
                if key == (A[0], A[1]) or key == (B[0], B[1]):
                    continue
                t = ((V[0] - A[0]) * ex + (V[1] - A[1]) * ey) / length2
                if not (0.05 < t < 0.95):
                    continue
                # retail distance of V from the edge line, in pixels
                cross = (V[0] - A[0]) * ey - (V[1] - A[1]) * ex
                rdist = abs(cross) / length2 ** 0.5
                if rdist > 0.75:
                    continue
                dex, dey = B[2] - A[2], B[3] - A[3]
                dl = (dex * dex + dey * dey) ** 0.5
                if dl < 1e-6:
                    continue
                ddist = ((V[2] - A[2]) * dey - (V[3] - A[3]) * dex) / dl
                found.append((abs(ddist), rdist, tag_name(tag), tag_name(vtag), key))
    found.sort(reverse=True)
    big = [x for x in found if x[0] > 0.25]
    print(f"t-junction candidates={len(found)} drawn-off-edge>0.25px={len(big)}")
    by = collections.Counter((x[2], x[3]) for x in big)
    for (et, vt), n in by.most_common(10):
        print(f"  {n:6d}  edge in {et}, vertex from {vt}")
    for x in found[:12]:
        print(f"  drawn {x[0]:.3f}px off edge (retail {x[1]:.2f}px) edge {x[2]} vertex {x[3]} at {x[4]}")


if __name__ == "__main__":
    sys.exit(main())
