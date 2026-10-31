"""Read a CTR_DEV_QUAD_DUMP file and show the turbo pads in it.

For every turbo-pad trigger quad (an invisible quad flagged as a trigger whose
terrain byte carries the turbo or super turbo step bit) it lists the visible,
textured quads under its footprint, with their texture slots, so the quads that
draw a pad can be told apart from the road around it.

    python tools/decrypted/quad_dump.py dump.csv [--level N]
"""
import argparse
import collections
import csv

TRIGGER = 0x40
TURBO, SUPER = 0x1, 0x2


def overlap(a, b):
    """Share of a's XZ footprint that b covers, 0..1."""
    w = min(a["maxx"], b["maxx"]) - max(a["minx"], b["minx"])
    d = min(a["maxz"], b["maxz"]) - max(a["minz"], b["minz"])
    if w <= 0 or d <= 0:
        return 0.0
    area = max(1, (a["maxx"] - a["minx"])) * max(1, (a["maxz"] - a["minz"]))
    return w * d / area


def textures(q):
    slots = [q["low"]] + [q["mid%d" % i] for i in range(4)]
    return tuple(s for s in slots if s != "0")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--level", type=int)
    args = ap.parse_args()

    rows = []
    with open(args.dump, newline="") as f:
        for r in csv.DictReader(line for line in f if not line.startswith("#")):
            if r["level"] == "level":
                continue  # header of a later level
            for k in ("level", "quad", "flags", "terrain", "minx", "miny", "minz", "maxx", "maxy", "maxz"):
                r[k] = int(r[k])
            rows.append(r)

    levels = collections.defaultdict(list)
    for r in rows:
        levels[r["level"]].append(r)

    for level, quads in sorted(levels.items()):
        if args.level is not None and level != args.level:
            continue
        triggers = [q for q in quads if q["flags"] & TRIGGER and q["terrain"] & (TURBO | SUPER)]
        visible = [q for q in quads if not q["flags"] & TRIGGER and textures(q)]
        print("level %d: %d quads, %d turbo triggers (%d super)" % (
            level, len(quads), len(triggers), sum(1 for t in triggers if t["terrain"] & SUPER)))
        for t in triggers:
            kind = "SUPER" if t["terrain"] & SUPER else "turbo"
            print("  %s trigger %d  x %d..%d  y %d..%d  z %d..%d" % (
                kind, t["quad"], t["minx"], t["maxx"], t["miny"], t["maxy"], t["minz"], t["maxz"]))
            cy = (t["miny"] + t["maxy"]) / 2
            for q in visible:
                cover = overlap(q, t)
                if cover < 0.25 or abs((q["miny"] + q["maxy"]) / 2 - cy) > 256:
                    continue
                print("    quad %5d cover %3.0f%%  low %s/%s  mid %s" % (
                    q["quad"], cover * 100, q["lowClut"], q["lowTpage"],
                    " ".join("%s:%s/%s%s" % (q["mid%d" % i], q["mid%dClut" % i], q["mid%dTpage" % i],
                                              "*%s" % q["mid%dFrames" % i] if q["mid%dFrames" % i] != "0" else "")
                             for i in range(4))))


if __name__ == "__main__":
    main()
