"""Find what is still drawn from the game's rounded (integer) camera.

Compares two polygon dumps (CTR_DEV_DUMP_FRAME) of the same frame, one run
with CTR_DEV_NUDGE moving player 1's integer camera by a unit. The precise
camera (PGXP) ignores the integer pose, so its polygons stay put to a few
hundredths of a pixel while their retail positions move. A polygon whose drawn
position moves with its retail one is drawn from the integer camera: it jumps
whenever the game's rounding of the camera steps, which is what the eye sees as
wobble or judder.

Polygons are matched between the dumps by name, bucket tag, vertex count and
texture (page, CLUT, UVs), then by nearest retail position and precise depth.
Groups (the stage or instance that drew it, from the dump's "@name", tag,
texture page, vertex count, whether the polygon has a precise position at all) are listed worst first, with the median movement and the
screen area of the polygons that follow. --png paints the frame: red follows
the integer camera, green is precise, grey did not move. Exits 1 when anything
follows.

Usage: python tools/decrypted/nudge_diff.py <base polys> <nudged polys> [--png out.png]
"""
import argparse
import collections
import statistics
import sys

BUCKETS = ["4x4R", "4x4L", "dynR", "dynL", "4x2R", "4x2L", "4x1R", "4x1L", "watR", "watL", "fullDyn", "clip"]

# A polygon whose retail position moved less than this does not depend on the
# camera (HUD, screen-space effects) and is left out.
MIN_RETAIL_MOVE = 0.25
# Drawn movement above this share of the retail movement means the polygon
# follows the integer camera.
FOLLOW_RATIO = 0.3


def tag_name(tag):
    return BUCKETS[tag] if 0 <= tag < len(BUCKETS) else "other"


def load(path):
    polys = []
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if f[0] == "T":
            polys[-1]["tex"] = tuple(f[1:])
            continue
        tag, count = int(f[0]), int(f[1])
        verts = []
        for i in range(count):
            b = 2 + i * 5
            verts.append((int(f[b]), int(f[b + 1]), float(f[b + 2]), float(f[b + 3]), float(f[b + 4])))
        name = f[2 + count * 5][1:] if len(f) > 2 + count * 5 else ""
        polys.append({"tag": tag, "verts": verts, "tex": None, "name": name})
    return polys


def centroid(p):
    n = len(p["verts"])
    return (sum(v[0] for v in p["verts"]) / n, sum(v[1] for v in p["verts"]) / n)


def match(base, nudged, max_dist=12.0):
    """Pairs (base, nudged) with the same name, tag, vertex count and
    texture, nearest retail centroid and precise depth first."""
    groups = collections.defaultdict(lambda: ([], []))
    for p in base:
        groups[(p["name"], p["tag"], len(p["verts"]), p["tex"])][0].append(p)
    for p in nudged:
        groups[(p["name"], p["tag"], len(p["verts"]), p["tex"])][1].append(p)
    pairs = []
    for a_list, b_list in groups.values():
        if not a_list or not b_list:
            continue
        cand = []
        for i, a in enumerate(a_list):
            ca = centroid(a)
            for j, b in enumerate(b_list):
                cb = centroid(b)
                d = abs(ca[0] - cb[0]) + abs(ca[1] - cb[1])
                if d > max_dist:
                    continue
                # The precise depth hardly moves with the integer camera, and
                # tells apart look-alike small triangles of a distant model.
                depth = sum(abs(va[4] - vb[4]) for va, vb in zip(a["verts"], b["verts"]))
                cand.append((d + depth, i, j))
        cand.sort()
        used_a, used_b = set(), set()
        for d, i, j in cand:
            if i in used_a or j in used_b:
                continue
            used_a.add(i)
            used_b.add(j)
            pairs.append((a_list[i], b_list[j]))
    return pairs


def moves(a, b):
    retail = statistics.mean(abs(va[0] - vb[0]) + abs(va[1] - vb[1]) for va, vb in zip(a["verts"], b["verts"]))
    drawn = statistics.mean(abs(va[2] - vb[2]) + abs(va[3] - vb[3]) for va, vb in zip(a["verts"], b["verts"]))
    return retail, drawn


def group_key(p):
    page = p["tex"][0] if p["tex"] else "flat"
    precise = "no-precise" if all(v[4] < 0 for v in p["verts"]) else "precise"
    return (p["name"] or "?", tag_name(p["tag"]), page, len(p["verts"]), precise)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("nudged")
    ap.add_argument("--png", help="paint the frame's polygons by verdict")
    ap.add_argument("--all", action="store_true", help="list precise groups too")
    args = ap.parse_args()

    base, nudged = load(args.base), load(args.nudged)
    pairs = match(base, nudged)

    groups = collections.defaultdict(list)
    verdicts = []
    for a, b in pairs:
        retail, drawn = moves(a, b)
        if retail < MIN_RETAIL_MOVE:
            verdicts.append((b, "still"))
            continue
        follows = drawn > FOLLOW_RATIO * retail
        bx = [v[2] for v in b["verts"]]
        by = [v[3] for v in b["verts"]]
        groups[group_key(b)].append((retail, drawn, follows, (min(bx), min(by), max(bx), max(by))))
        verdicts.append((b, "follows" if follows else "precise"))

    print(f"{len(base)} / {len(nudged)} polygons, {len(pairs)} matched")
    rows = []
    for key, items in groups.items():
        n_follow = sum(1 for i in items if i[2])
        rows.append((n_follow, key, items))
    rows.sort(key=lambda r: (-r[0], -len(r[2])))
    print(f"{'drawn by':16} {'tag':8} {'page':6} {'n':>2} {'shadow':10} {'polys':>6} {'follow':>6} {'retail':>7} {'drawn':>7}  where (x0,y0-x1,y1)")
    total_follow = 0
    for n_follow, key, items in rows:
        total_follow += n_follow
        if n_follow == 0 and not args.all:
            continue
        shown = [i for i in items if i[2]] or items
        retail = statistics.median(i[0] for i in shown)
        drawn = statistics.median(i[1] for i in shown)
        xs = [x for i in shown for x in (i[3][0], i[3][2])]
        ys = [y for i in shown for y in (i[3][1], i[3][3])]
        where = f"{min(xs):.0f},{min(ys):.0f}-{max(xs):.0f},{max(ys):.0f}"
        print(f"{key[0]:16} {key[1]:8} {key[2]:6} {key[3]:>2} {key[4]:10} {len(items):6d} {n_follow:6d} {retail:7.3f} {drawn:7.3f}  {where}")
    precise = [i[1] for items in groups.values() for i in items if not i[2]]
    if precise:
        print(f"precise polygons: {len(precise)}, drawn move max {max(precise):.3f} px")
    print(f"following the integer camera: {total_follow}")

    if args.png:
        from PIL import Image, ImageDraw
        xs = [v[2] for p, _ in verdicts for v in p["verts"]]
        ys = [v[3] for p, _ in verdicts for v in p["verts"]]
        x0, y0 = min(0, min(xs)), min(0, min(ys))
        w, h = int(max(xs) - x0) + 2, int(max(ys) - y0) + 2
        scale = 2
        img = Image.new("RGB", (w * scale, h * scale), (16, 16, 16))
        d = ImageDraw.Draw(img)
        colours = {"still": (90, 90, 90), "precise": (40, 160, 70), "follows": (240, 40, 40)}
        for verdict in ("still", "precise", "follows"):
            for p, v in verdicts:
                if v != verdict:
                    continue
                pts = [((x[2] - x0) * scale, (x[3] - y0) * scale) for x in p["verts"]]
                if len(pts) == 4:
                    pts = [pts[0], pts[1], pts[3], pts[2]]  # PS1 quads are Z-ordered
                d.polygon(pts, outline=colours[v], fill=colours[v] if v == "follows" else None)
        img.save(args.png)

    return 1 if total_follow else 0


if __name__ == "__main__":
    sys.exit(main())
