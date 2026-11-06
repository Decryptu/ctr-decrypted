"""Is the road drawn where a kart drives? (docs/TRACK-CONVERTER.md#checks)

  python tools/tracks/floor_check.py <work folder> <course>

Takes every collision ground point the converter wrote (<course>_road.txt, every 64 LEV units, no kill
planes, nothing a mover draws) that lies on the CPU route's road, and looks for a drawn triangle of the OBJ at it. A point with none within GAP
under it, or with drawn road hanging over it within COVERED, is a kart on an invisible floor or under its
own road. Exit 1 when more than ALLOWED of the points are so, and it prints the worst places.
"""
import collections, json, os, sys

import numpy as np

GAP = 24  # LEV units a drawn surface may lie off the collision
COVERED = 120  # LEV units over the ground in which a flat drawn surface shuts a kart under it
FLAT = 0.3  # a triangle's normal y: a floor, however banked, not a wall
LAYER = 400  # LEV units: a ground this near over another hides it
CELL = 256
ROAD_REACH = 1.0  # of the route's half width
ALLOWED = 0.002
NOT_DRAWN = ('col_', 'item', 'blend', 'collision')


def read_obj(path):
    """-> the drawn triangles, as an array of (3 corners, x y z), in LEV units."""
    points, tris, drawn = [], [], True
    with open(path) as f:
        for line in f:
            if line.startswith('v '):
                points.append([float(c) * 64 for c in line.split()[1:4]])
            elif line.startswith('usemtl '):
                drawn = not line.split()[1].startswith(NOT_DRAWN)
            elif line.startswith('f ') and drawn:
                corners = [int(c.split('/')[0]) - 1 for c in line.split()[1:]]
                tris += [(corners[0], corners[i], corners[i + 1]) for i in range(1, len(corners) - 1)]
    return np.array(points)[np.array(tris)]


def heights(tris, cells, x, z):
    """The heights of the flat drawn triangles over (x, z)."""
    out = []
    for i in cells.get((int(x // CELL), int(z // CELL)), ()):
        a, b, c = tris[i]
        d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(d) < 1e-6:
            continue
        u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
        v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
        if u >= -0.01 and v >= -0.01 and u + v <= 1.01:
            out.append(u * a[1] + v * b[1] + (1 - u - v) * c[1])
    return out


def check(work, course):
    tris = read_obj(os.path.join(work, course + '.obj'))
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    tris = tris[np.abs(n[:, 1]) >= FLAT]
    cells = collections.defaultdict(list)
    for i, t in enumerate(tris):
        x0, z0, x1, z1 = t[:, 0].min(), t[:, 2].min(), t[:, 0].max(), t[:, 2].max()
        for cx in range(int(x0 // CELL), int(x1 // CELL) + 1):
            for cz in range(int(z0 // CELL), int(z1 // CELL) + 1):
                cells[cx, cz].append(i)

    with open(os.path.join(work, course + '_route.json')) as f:
        route = np.array(json.load(f))  # x, y, z, half width
    points = np.loadtxt(os.path.join(work, course + '_road.txt')).reshape(-1, 3)
    # MKDS keeps collision layers under its road that no kart reaches: only a place's top layer counts
    top = {}
    for x, y, z in points:
        top.setdefault((x, z), []).append(y)
    points = np.array([p for p in points if not any(p[1] < y <= p[1] + LAYER for y in top[p[0], p[2]])])
    bad, total = [], 0
    for chunk in np.array_split(points, max(len(points) // 5000, 1)):
        d = np.linalg.norm(chunk[:, None, :] - route[None, :, :3], axis=2)
        nearest = d.argmin(1)
        on_road = d[np.arange(len(chunk)), nearest] < route[nearest, 3] * ROAD_REACH
        for x, y, z in chunk[on_road]:
            total += 1
            found = heights(tris, cells, x, z)
            under = [h for h in found if abs(h - y) <= GAP]
            over = [h for h in found if GAP < h - y <= COVERED]
            if over and not under:
                bad.append((x, y, z, 'under a drawn floor'))
            elif not under:
                bad.append((x, y, z, 'nothing drawn'))
    return total, bad


def main():
    work, course = sys.argv[1:3]
    total, bad = check(work, course)
    share = len(bad) / max(total, 1)
    print(f'floor: {total} road points, {len(bad)} with no drawn floor ({share:.2%})')
    places = collections.Counter((int(x // 512) * 512, int(z // 512) * 512, why) for x, _, z, why in bad)
    for (x, z, why), count in places.most_common(5):
        print(f'  {count} near LEV ({x}, {z}): {why}')
    sys.exit(1 if share > ALLOWED else 0)


if __name__ == '__main__':
    main()
