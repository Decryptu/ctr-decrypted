"""Check a converted track with the in-game collision probe (CTR_DEV_COLL_PROBE, CTR_DEV_COLL_PROBE_IN);
check_track.ps1 runs it. Exits 1 on any hole or height problem.

Every probe point stands on an MKDS ground layer. For each, compare what the game found:
  hole         no ground where MKDS has ground all around (within the kart's radius)
  edge         no ground, but the point is on the rim of the drivable area (wall foot, drop)
  engine edge  no ground, but the point is inside a LEV triangle within 1 unit of its edge:
               the engine's fixed-point triangle test, retail tracks have these too
  height       ground found, but not on any MKDS layer within 64 units
  wall footprint  the probe line ran down a wall face (point on the wall's base line)
and how far the drawn surface is from the ground the game found.
"""
import csv, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkdsmap, levtri, nds

if len(sys.argv) < 5:
    sys.exit('usage: collision_check.py <probe.csv> <rom.nds> <course> <work folder> [scale, default 1/12]')
probe_csv, rom_path, course, work = sys.argv[1:5]
scale = float(sys.argv[5]) if len(sys.argv) > 5 else 1 / 12
obj_path = os.path.join(work, course + '.obj')
lev_path = os.path.join(work, 'lev', course + '_patched.lev')
t = mkdsmap.kcl(nds.carc(nds.nitrofs(open(rom_path, 'rb').read())[f'data/Course/{course}.carc'])['course_collision.kcl'])
P = np.array([v for tr in t for v in tr[:3]])
centre = (P.min(0) + P.max(0)) / 2
LEV = 64 * scale


def tri_grid(tris):
    g = {}
    for tri in tris:
        xs = [v[0] for v in tri[:3]]; zs = [v[2] for v in tri[:3]]
        for gx in range(int(min(xs) // 256), int(max(xs) // 256) + 1):
            for gz in range(int(min(zs) // 256), int(max(zs) // 256) + 1):
                g.setdefault((gx, gz), []).append(tri)
    return g


def heights(grid, x, z):
    out = []
    for a, b, c, payload in grid.get((int(x // 256), int(z // 256)), []):
        d = lambda u, v: (v[0] - u[0]) * (z - u[2]) - (v[2] - u[2]) * (x - u[0])
        s = (d(a, b), d(b, c), d(c, a))
        if min(s) >= 0 or max(s) <= 0:
            n = np.cross(np.array(b) - a, np.array(c) - a)
            if abs(n[1]) < 1e-6: continue
            out.append((a[1] - (n[0] * (x - a[0]) + n[2] * (z - a[2])) / n[1], payload))
    return out


# every upward-facing MKDS collision surface is somewhere a kart can stand (road, offroad, curb tops)
layers = tri_grid([(a, b, c, mkdsmap.kcl_type(attr)) for a, b, c, n, attr in t if n[1] > 0.3])

V = []; faces = []; cur = None
for l in open(obj_path):
    s = l.split()
    if not s: continue
    if s[0] == 'v': V.append([float(x) * 64 for x in s[1:4]])
    elif s[0] == 'usemtl': cur = s[1]
    elif s[0] == 'f' and not cur.startswith('col_'):
        poly = [V[int(x.split('/')[0]) - 1] for x in s[1:]]
        faces += [(poly[0], poly[i], poly[i + 1], cur) for i in range(1, len(poly) - 1)]
drawn_grid = tri_grid(faces)
LV, LQ = levtri.load(lev_path)


def edge_dist(x, z):
    """Distance to the nearest edge of a LEV ground triangle containing (x, z), or None."""
    best = None
    for qi, tri_idx, (a, b, c) in levtri.containing(LV, LQ, x, z):
        for u, w in ((a, b), (b, c), (c, a)):
            p, u2, w2 = np.array([x, z], float), np.array([u[0], u[2]], float), np.array([w[0], w[2]], float)
            tt = np.clip(np.dot(p - u2, w2 - u2) / max(np.dot(w2 - u2, w2 - u2), 1e-9), 0, 1)
            d = np.linalg.norm(p - (u2 + tt * (w2 - u2)))
            best = d if best is None else min(best, d)
    return best


counts = {'probes': 0, 'hole': 0, 'edge': 0, 'engine edge': 0, 'height': 0, 'wall footprint': 0}
holes = []; heights_off = []; dh_coll = []; dh_draw = []
for r in csv.DictReader(open(probe_csv)):
    x, y, z = int(r['x']), int(r['y']), int(r['z'])
    mx, mz = x / LEV + centre[0], z / LEV + centre[2]
    ny = y / LEV + centre[1]
    here = [gy for gy, _ in heights(layers, mx, mz)]
    if not any(abs(gy - ny) < 0x80 / LEV for gy in here):
        continue  # not over MKDS ground at this height (lane probes off the road)
    counts['probes'] += 1
    gy = min(here, key=lambda g: abs(g - ny))
    if r['hit'] == '0':
        rad = 0x19 / LEV
        ring = [heights(layers, mx + rad * np.cos(a), mz + rad * np.sin(a)) for a in np.linspace(0, 2 * np.pi, 8, endpoint=False)]
        if not all(any(abs(g2 - gy) < 0x80 / LEV for g2, _ in h) for h in ring):
            counts['edge'] += 1
        elif (edge_dist(x, z) or 99) < 1.0:
            counts['engine edge'] += 1
        else:
            counts['hole'] += 1
            holes.append((x, y, z))
        continue
    hy = int(r['hitY'])
    if min(abs((g - centre[1]) * LEV - hy) for g in here) > 64:
        # the line ran down a wall face: the point is on the wall's footprint, where no
        # kart centre can be (it stays its radius away)
        if int(r['flags']) & 0x3000 == 0x2000:
            counts['wall footprint'] += 1
            continue
        counts['height'] += 1
        heights_off.append((x, y, z, hy))
    dh_coll.append(hy - (gy - centre[1]) * LEV)
    drawn = [dy for dy, _ in heights(drawn_grid, x, z)]
    if drawn:
        dh_draw.append(min(drawn, key=lambda dy: abs(dy - hy)) - hy)

print(counts)
for h in holes[:20]: print('  hole at LEV', h)
for h in heights_off[:20]: print('  ground off its MKDS layer at LEV', h)
a = np.array(dh_coll); b = np.array(dh_draw)
print('game ground - MKDS ground: p1 %.1f  median %.1f  p99 %.1f' % (np.percentile(a, 1), np.median(a), np.percentile(a, 99)))
print('drawn surface - game ground: p1 %.1f  median %.1f  p99 %.1f  over 16 units: %d of %d' % (
    np.percentile(b, 1), np.median(b), np.percentile(b, 99), (np.abs(b) > 16).sum(), len(b)))
sys.exit(1 if counts['hole'] or counts['height'] else 0)
