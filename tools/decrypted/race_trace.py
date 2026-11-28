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
  race_trace.py judder A.csv [B.csv ...]   rounding judder per channel and driving
                                           phase, ranked (judder_sweep.ps1)
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


# Channels judder reads: (name, wrap, floor, group). A residual is measured
# against the channel's own per-frame motion, never less than `floor`, so a
# still channel's noise is not divided by zero. 'drawn' is what the screen
# follows under the precise camera; 'int cam' is the game's integer camera,
# which steps by design and is not drawn while the precise camera is on.
JUDDER_CHANNELS = [
    ("pcx", 0, 1, "drawn"), ("pcy", 0, 1, "drawn"), ("pcz", 0, 1, "drawn"),
    ("pcrx", 4096, 1, "drawn"), ("pcry", 4096, 1, "drawn"), ("pcrz", 4096, 1, "drawn"),
    ("kx", 0, 1, "kart"), ("ky", 0, 1, "kart"), ("kz", 0, 1, "kart"), ("angle", 4096, 1, "kart"),
    ("speed", 0, 16, "kart"), ("turn", 0, 1, "kart"), ("rotW", 0, 1, "kart"), ("wobble", 0, 1, "kart"),
    ("camSpeed", 0, 16, "camera"), ("rollIn", 0, 1, "camera"), ("roll", 0, 1, "camera"), ("zoomOff", 0, 256, "camera"),
    ("cx", 0, 1, "int cam"), ("cy", 0, 1, "int cam"), ("cz", 0, 1, "int cam"),
    ("crx", 4096, 1, "int cam"), ("cry", 4096, 1, "int cam"), ("crz", 4096, 1, "int cam"),
]

TRACK_NAMES = {0: "Dingo Canyon", 1: "Dragon Mines", 2: "Blizzard Bluff", 3: "Crash Cove", 4: "Tiger Temple",
               5: "Papu's Pyramid", 6: "Roo's Tubes", 7: "Hot Air Skyway", 8: "Sewer Speedway", 9: "Mystery Caves",
               10: "Cortex Castle", 11: "N. Gin Labs", 12: "Polar Pass", 13: "Oxide Station", 14: "Coco Park",
               15: "Tiny Arena", 16: "Slide Coliseum", 17: "Turbo Track", 26: "N. Sanity Beach"}

PHASES = ("steady", "rising", "falling", "turning")


def unwrap(v, wrap):
    if not wrap:
        return list(v)
    out = [v[0]]
    for x in v[1:]:
        out.append(out[-1] + ((x - out[-1] + wrap / 2) % wrap - wrap / 2))
    return out


def judder_phases(rows, fps):
    """Each frame's phase, or None where it is not measured: airborne and just
    after landing, a respawn, frames near a phase change (the input changed, a
    real jerk), the lights and the end of the race."""
    n, w = len(rows), max(2, fps // 8)
    speed = [r["speed"] for r in rows]
    ang = unwrap([r["angle"] for r in rows], 4096)
    top = max(1, sorted(speed)[len(speed) * 95 // 100])
    phase = [None] * n
    for i in range(w, n - w):
        ds = (speed[i + w] - speed[i - w]) / (2 * w)
        da = abs(ang[i + w] - ang[i - w]) / (2 * w)
        # Per frame at 60 FPS: 0.1% of top speed, 1/4096 of a turn.
        k = 60 / fps
        if ds > top * 0.001 * k:
            phase[i] = "rising"
        elif ds < -top * 0.001 * k:
            phase[i] = "falling"
        elif da > 1.0 * k:
            phase[i] = "turning"
        elif speed[i] > top * 0.3:
            phase[i] = "steady"
    land = fps // 6
    bad = [False] * n
    for i, r in enumerate(rows):
        if r["air"] or r["lights"] > 0 or r["end"]:
            for j in range(max(0, i - 2), min(n, i + land)):
                bad[j] = True
        if i > 0 and abs(r["kx"] - rows[i - 1]["kx"]) + abs(r["kz"] - rows[i - 1]["kz"]) > 200:
            for j in range(max(0, i - fps), min(n, i + fps)):
                bad[j] = True
    m = fps // 10
    out = [None] * n
    for i in range(m, n - m):
        p = phase[i]
        if p and not bad[i] and all(phase[j] == p for j in range(i - m, i + m + 1)):
            out[i] = p
    return out


def judder_stats(res, d1, floor):
    """Size: mean |residual| over mean motion (per-frame motion, at least
    floor), the top 1% dropped (a wall, a bump). Signatures: 'steps' when the
    second differences are whole or half units (a rounded value, or one
    halved after rounding), 'alt' when the residual flips sign every frame
    (a value updated at 30 Hz holds for two frames), 'saw'
    when it is lopsided (small pushes one way, a large snap back: a filter
    overshooting)."""
    pairs = sorted(zip(res, d1), key=lambda p: abs(p[0][1]))
    pairs = pairs[: max(1, len(pairs) * 99 // 100)]
    num = sum(abs(r) for (_, r), _ in pairs)
    den = sum(max(abs(d), floor) for _, d in pairs)
    size = num / max(den, 1e-9)
    d2 = [-2 * r for (_, r), _ in pairs]
    big = [x for x in d2 if abs(x) >= 0.25]
    whole = sum(1 for x in big if abs(2 * x - round(2 * x)) < 0.16) / len(big) if len(big) >= 10 else 0
    # The residual's two-frame component, summed within each run of
    # consecutive frames (the odd/even phase of a 30 Hz step can change
    # between runs) over its total size: near 1 for a value held two frames,
    # small for noise and for slower oscillations (steering corrections).
    at = {i: r for (i, r), _ in pairs}
    runs, total = [], 0.0
    for i in sorted(at):
        if not runs or i != runs[-1][0] + 1:
            runs.append([i, 0.0, 0])
        runs[-1][0] = i
        runs[-1][1] += at[i] * (1 if i & 1 else -1)
        runs[-1][2] += 1
        total += abs(at[i])
    alt = sum(abs(s) for _, s, n in runs if n >= 8) / total if total > 1e-9 and len(at) >= 20 else 0
    mean = sum(d2) / len(d2)
    var = sum((x - mean) ** 2 for x in d2) / len(d2)
    skew = (sum((x - mean) ** 3 for x in d2) / len(d2)) / var ** 1.5 if var > 1e-9 else 0
    tags = []
    if whole > 0.6:
        tags.append("steps")
    if alt > 0.5:
        tags.append(f"alt {alt:.2f}")
    if abs(skew) > 1.5:
        tags.append(f"saw {skew:+.1f}")
    motion = den / len(pairs)
    return size, motion, " ".join(tags) or "-"


def judder(paths, top_n=25):
    """Frame-to-frame judder per channel and driving phase, ranked; see
    JUDDER_CHANNELS and docs/DEV-TOOLS.md."""
    cells = []  # (channel, group, phase, track, frames, size, motion, signature)
    for path in paths:
        every = load(path)
        if not every or "pcx" not in every[0]:
            print(f"{path}: no camera columns, skipped")
            continue
        fps = 60 if any(r["fps60"] for r in every) else 30
        # A run can change level (a hub drive through a warp pad): each level on its own.
        segments = []
        for r in every:
            if not segments or segments[-1][-1]["level"] != r["level"]:
                segments.append([])
            segments[-1].append(r)
        for rows in segments:
            if len(rows) < fps * 2:
                continue
            judder_segment(path, rows, fps, cells)
    if not cells:
        return
    judder_report(cells, top_n)


def judder_segment(path, rows, fps, cells):
    """One level of one trace: a cell per channel and phase."""
    phase = judder_phases(rows, fps)
    track = TRACK_NAMES.get(rows[0]["level"], f"level {rows[0]['level']}")
    counts = {p: phase.count(p) for p in PHASES}
    print(f"{path}: {track}, {fps} FPS, {len(rows)} frames: " + ", ".join(f"{p} {c}" for p, c in counts.items()))
    for name, wrap, floor, group in JUDDER_CHANNELS:
        if name not in rows[0]:
            continue
        v = unwrap([r[name] for r in rows], wrap)
        for p in PHASES:
            idx = [i for i in range(1, len(v) - 1) if phase[i] == p]
            if len(idx) < 30:
                continue
            res = [(i, v[i] - (v[i - 1] + v[i + 1]) / 2) for i in idx]
            d1 = [(v[i + 1] - v[i - 1]) / 2 for i in idx]
            size, motion, sig = judder_stats(res, d1, floor)
            cells.append((name, group, p, track, len(idx), size, motion, sig))


def judder_report(cells, top_n):
    # Steady driving is the reference: per channel, the median size there.
    base = {}
    for name, *_ in JUDDER_CHANNELS:
        s = sorted(c[5] for c in cells if c[0] == name and c[2] == "steady")
        if s:
            base[name] = s[len(s) // 2]

    print("\nWorse than steady driving (size: mean residual over per-frame motion; x: times the steady median):")
    print(f"{'channel':9} {'group':7} {'phase':8} {'tracks':>6} {'median':>7} {'worst':>7} {'x':>5}  {'motion':>7}  signature, worst track")
    groups = {}
    for c in cells:
        if c[2] == "steady" or c[0] not in base:
            continue
        x = c[5] / max(base[c[0]], 0.01)
        if x >= 2 and c[5] >= 0.02:
            groups.setdefault((c[0], c[1], c[2]), []).append((c, x))
    ranked = []
    for (name, group, p), hits in groups.items():
        sizes = sorted(h[0][5] for h in hits)
        worst, wx = max(hits, key=lambda h: h[0][5])
        ranked.append((len(hits) * sizes[len(sizes) // 2], name, group, p, len(hits), sizes[len(sizes) // 2], worst, wx))
    ranked.sort(key=lambda r: (r[2] == "int cam", -r[0]))
    for _, name, group, p, k, med, worst, wx in ranked[:top_n]:
        print(f"{name:9} {group:7} {p:8} {k:>6} {med:>7.1%} {worst[5]:>7.1%} {wx:>5.1f}  {worst[6]:>7.2f}  {worst[7]}, {worst[3]}")

    print("\n30 Hz and filter signatures, per channel (tracks showing it in any phase, of tracks traced):")
    tracks = len({c[3] for c in cells})
    for name, wrap, floor, group in JUDDER_CHANNELS:
        alt = {c[3] for c in cells if c[0] == name and "alt" in c[7]}
        saw = {c[3] for c in cells if c[0] == name and "saw" in c[7]}
        if len(alt) >= 3 or len(saw) >= 3:
            print(f"{name:9} {group:7} alt {len(alt):>2}/{tracks}  saw {len(saw):>2}/{tracks}")

    print("\nSteady driving, per channel (median and worst track):")
    print(f"{'channel':9} {'group':7} {'median':>7} {'worst':>7}  {'motion':>7}  signature, worst track")
    for name, wrap, floor, group in JUDDER_CHANNELS:
        s = [c for c in cells if c[0] == name and c[2] == "steady"]
        if not s:
            continue
        worst = max(s, key=lambda c: c[5])
        print(f"{name:9} {group:7} {base[name]:>7.1%} {worst[5]:>7.1%}  {worst[6]:>7.2f}  {worst[7]}, {worst[3]}")


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
    elif cmd == "judder":
        judder(args)
    elif cmd == "fps" and len(args) in (2, 4) and (len(args) == 2 or args[2] == "--until"):
        fps(args[0], args[1], int(args[3]) if len(args) == 4 else None)
    elif cmd == "parity" and len(args) == 2:
        parity(*args)
    else:
        sys.exit(__doc__)
