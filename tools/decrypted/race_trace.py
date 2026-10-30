#!/usr/bin/env python3
"""Compare race traces written by CTR_DEV_TRACE (platform/native_dev_race.c).

  race_trace.py laps  A.csv [B.csv ...]   lap splits and finish time per trace
  race_trace.py jumps A.csv [B.csv ...]   airborne spells: count, airtime, apex
  race_trace.py ghost SOURCE.csv PLAY.csv  how far the ghost in PLAY drifts from
                                           the player of SOURCE (who recorded it)
  race_trace.py parity A.csv B.csv         one line: finish times, and jumps
                                           launched from the same spot in both

Positions are in the trace's units (world >> 8). Typical use is a 30 FPS run
against a 60 FPS run of the same autopilot race; see docs/DEV-TOOLS.md.
"""
import bisect
import csv
import math
import sys


def load(path):
    with open(path, newline="") as f:
        return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f)]


def racing(rows):
    return [r for r in rows if r["lights"] <= 0 and not r["end"] and r["raceMS"] > 0]


def laps(paths):
    for path in paths:
        rows = load(path)
        splits, prev = [], 0
        for r in rows:
            if r["lap"] != prev:
                splits.append(r["raceMS"])
                prev = r["lap"]
        fps = 60 if any(r["fps60"] for r in rows) else 30
        laps_ms = [b - a for a, b in zip([0] + splits, splits)]
        print(f"{path}: {fps} FPS, splits {[s / 1000 for s in splits]}, laps {[l / 1000 for l in laps_ms]}")


def spells(rows):
    out, start = [], None
    for i, r in enumerate(rows):
        if r["air"] and start is None:
            start = i
        elif not r["air"] and start is not None:
            seg = rows[start:i]
            base = rows[start - 1]["y"] if start > 0 else seg[0]["y"]
            out.append({
                "ms": seg[0]["raceMS"],
                "air": rows[i]["raceMS"] - seg[0]["raceMS"],
                "rise": max(s["y"] for s in seg) - base,
                "x": seg[0]["x"], "z": seg[0]["z"],
            })
            start = None
    return out


def jumps(paths):
    for path in paths:
        sp = [s for s in spells(racing(load(path))) if s["air"] >= 200]
        if not sp:
            print(f"{path}: no airborne spell of 200 ms or more")
            continue
        air = sum(s["air"] for s in sp) / len(sp)
        rise = sum(s["rise"] for s in sp) / len(sp)
        print(f"{path}: {len(sp)} spells >= 200 ms, mean airtime {air:.0f} ms, mean apex rise {rise:.1f}, "
              f"max apex rise {max(s['rise'] for s in sp)}")


def finish(rows):
    for r in rows:
        if r["end"]:
            return r["raceMS"]
    return None


def matched_jumps(a_rows, b_rows, min_air=200, max_apart=400):
    a_sp = [s for s in spells(racing(a_rows)) if s["air"] >= min_air]
    b_sp = [s for s in spells(racing(b_rows)) if s["air"] >= min_air]
    pairs = []
    for s in a_sp:
        if not b_sp:
            break
        m = min(b_sp, key=lambda t: (t["x"] - s["x"]) ** 2 + (t["z"] - s["z"]) ** 2)
        if math.hypot(m["x"] - s["x"], m["z"] - s["z"]) < max_apart:
            pairs.append((s, m))
    return pairs


def parity(a_path, b_path):
    a, b = load(a_path), load(b_path)
    fa, fb = finish(a), finish(b)
    if fa is None or fb is None:
        print(f"{a_path} vs {b_path}: did not finish ({fa}, {fb})")
        return
    pairs = matched_jumps(a, b)
    line = f"finish {fa / 1000:.2f} s vs {fb / 1000:.2f} s ({(fb - fa) * 100 / fa:+.1f}%)"
    if pairs:
        air = sum(m["air"] - s["air"] for s, m in pairs) / len(pairs)
        rise = sum(m["rise"] - s["rise"] for s, m in pairs) / len(pairs)
        worst = max(abs(m["air"] - s["air"]) for s, m in pairs)
        line += f", {len(pairs)} matched jumps: airtime {air:+.0f} ms (worst {worst}), apex rise {rise:+.1f}"
    else:
        line += ", no matched jumps"
    print(line)


def ghost(src_path, play_path):
    src = racing(load(src_path))
    times = [r["raceMS"] for r in src]
    errs = []
    for r in racing(load(play_path)):
        if not r["gvis"] or (r["gx"] == 0 and r["gz"] == 0):
            continue
        t = r["raceMS"]
        i = bisect.bisect_left(times, t)
        if i <= 0 or i >= len(times):
            continue
        a, b = src[i - 1], src[i]
        f = (t - a["raceMS"]) / max(1, b["raceMS"] - a["raceMS"])
        ex = a["x"] + (b["x"] - a["x"]) * f
        ey = a["y"] + (b["y"] - a["y"]) * f
        ez = a["z"] + (b["z"] - a["z"]) * f
        errs.append(math.sqrt((r["gx"] - ex) ** 2 + (r["gy"] - ey) ** 2 + (r["gz"] - ez) ** 2))
    if not errs:
        print("no ghost samples")
        return
    errs.sort()
    print(f"{play_path}: {len(errs)} frames, ghost vs recorder: mean {sum(errs) / len(errs):.1f}, "
          f"p95 {errs[int(len(errs) * 0.95)]:.1f}, max {errs[-1]:.1f}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "laps":
        laps(args)
    elif cmd == "jumps":
        jumps(args)
    elif cmd == "ghost" and len(args) == 2:
        ghost(*args)
    elif cmd == "parity" and len(args) == 2:
        parity(*args)
    else:
        sys.exit(__doc__)
