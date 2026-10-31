#!/usr/bin/env python3
"""Compare race traces written by CTR_DEV_TRACE (platform/native_dev_race.c).

  race_trace.py laps  A.csv [B.csv ...]   lap splits and finish time per trace
  race_trace.py jumps A.csv [B.csv ...]   airborne spells: count, airtime, apex
  race_trace.py ghost SOURCE.csv PLAY.csv  how far the ghost in PLAY drifts from
                                           the player of SOURCE (who recorded it)
  race_trace.py parity A.csv B.csv         one line: finish times, and jumps
                                           launched from the same spot in both
  race_trace.py smooth A.csv [B.csv ...]   how unevenly player 1's camera and
                                           kart angles move from frame to frame
  race_trace.py fps A.csv B.csv [--until MS]
                                           the same scripted drive (CTR_DEV_DRIVE)
                                           at two frame rates, field by field

Positions are in the trace's units (world >> 8). Typical use is a 30 FPS run
against a 60 FPS run of the same autopilot race; see docs/DEV-TOOLS.md.
"""
import bisect
import csv
import math
import sys


def load(path):
    with open(path, newline="") as f:
        return [{k: (float(v) if "." in v else int(v)) for k, v in row.items()} for row in csv.DictReader(f)]


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


# Integer camera angles and position, then the PGXP precise camera's.
SMOOTH_CHANNELS = [("cry", 4096), ("crx", 4096), ("crz", 4096), ("cx", 0), ("cy", 0), ("cz", 0),
                   ("pcry", 4096), ("pcrx", 4096), ("pcrz", 4096), ("pcx", 0), ("pcy", 0), ("pcz", 0)]


def smooth(paths):
    """Per channel, the residual of each frame from the midpoint of its two
    neighbours: zero for motion at a steady rate, so it measures how unevenly a
    value moves (judder), in the channel's own units, as median/90th percentile. 'alt' is the share of it
    that flips sign every frame, the signature of something stepped on odd
    frames only. Angles are in 4096ths of a turn."""
    for path in paths:
        rows = [r for r in racing(load(path)) if "cry" in r]
        fps = 60 if any(r["fps60"] for r in rows) else 30
        parts = []
        for name, wrap in SMOOTH_CHANNELS:
            if name not in rows[0]:
                continue
            v = [r[name] for r in rows]
            res = []
            for i in range(1, len(v) - 1):
                a, b, c = v[i - 1], v[i], v[i + 1]
                if wrap:
                    b = a + ((b - a + wrap // 2) % wrap - wrap // 2)
                    c = b + ((c - b + wrap // 2) % wrap - wrap // 2)
                res.append(b - (a + c) / 2)
            if not res:
                continue
            # Percentiles, not a mean: a respawn or mask grab is one huge step.
            mag = sorted(abs(x) for x in res)
            p50, p90 = mag[len(mag) // 2], mag[len(mag) * 9 // 10]
            keep = [(i, x) for i, x in enumerate(res) if abs(x) <= 4 * p90 + 1]
            alt = abs(sum(x * (1 if i & 1 else -1) for i, x in keep)) / max(1e-9, sum(abs(x) for _, x in keep))
            parts.append(f"{name} {p50:.2f}/{p90:.2f} (alt {alt:.2f})")
        print(f"{path}: {fps} FPS, {len(rows)} frames: " + ", ".join(parts))


# Kart state that must follow the same course in time at any frame rate
# (vertical velocity left out: on the ground it is contact noise at both).
FPS_FIELDS = [("x", 0), ("z", 0), ("y", 0), ("angle", 4096), ("speed", 0), ("vx", 0), ("vz", 0),
              ("turn", 0), ("rotW", 0), ("spin", 0), ("wobble", 0), ("lerpVel", 0), ("rotPrevW", 0),
              ("camSpeed", 0), ("crx", 4096), ("cry", 4096)]


def fps(a_path, b_path, until=None):
    """B (normally 60 FPS) against A (normally 30 FPS) on the same scripted
    drive, at A's race times, B interpolated. Per field: the RMS difference as
    a share of A's own spread over the run, and the race time it first strays
    by more than a tenth of that spread. A field that scales correctly with
    frame time stays near zero until the runs part (a wall, a jump) and every
    field strays together; one that strays alone and early is a frame-counted
    step that was not converted for 60 FPS."""
    a = [r for r in load(a_path) if r["lights"] <= 0 and not r["end"]]
    b = [r for r in load(b_path) if r["lights"] <= 0 and not r["end"]]
    if until is not None:
        a = [r for r in a if r["clock"] <= until]
    times = [r["clock"] for r in b]
    print(f"{a_path} ({len(a)} frames) vs {b_path} ({len(b)} frames)")
    for name, wrap in FPS_FIELDS:
        if name not in a[0]:
            continue
        diffs, values, first = [], [], None
        for r in a:
            t = r["clock"]
            i = bisect.bisect_left(times, t)
            if i <= 0 or i >= len(times):
                continue
            p, q = b[i - 1], b[i]
            f = (t - p["clock"]) / max(1, q["clock"] - p["clock"])
            pv, qv, av = p[name], q[name], r[name]
            if wrap:
                qv = pv + ((qv - pv + wrap // 2) % wrap - wrap // 2)
            bv = pv + (qv - pv) * f
            d = bv - av
            if wrap:
                d = (d + wrap / 2) % wrap - wrap / 2
            diffs.append((t, d))
            values.append(av)
        if not diffs:
            continue
        mean = sum(values) / len(values)
        spread = math.sqrt(sum((v - mean) ** 2 for v in values) / len(values)) if not wrap else 512.0
        spread = max(spread, 1.0)
        rms = math.sqrt(sum(d * d for _, d in diffs) / len(diffs))
        for t, d in diffs:
            if abs(d) > spread / 10:
                first = t
                break
        when = f"strays at {first} ms" if first is not None else "stays within 10%"
        print(f"  {name:9} rms diff {rms:9.1f} = {rms / spread:6.1%} of spread {spread:8.1f}, {when}")


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
    elif cmd == "smooth":
        smooth(args)
    elif cmd == "fps" and len(args) in (2, 4) and (len(args) == 2 or args[2] == "--until"):
        fps(args[0], args[1], int(args[3]) if len(args) == 4 else None)
    elif cmd == "parity" and len(args) == 2:
        parity(*args)
    else:
        sys.exit(__doc__)
