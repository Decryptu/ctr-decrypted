#!/usr/bin/env python3
"""How every racer moves, from a CTR_DEV_DRIVER_DUMP, against its own heading.

  driver_motion.py DUMP.bin TRACE.csv [--ms 3000] [--drivers 1-7]

Each frame's move is split into the part along the racer's heading and the
part across it. Over the first --ms of race time after the green light (taken
from the CTR_DEV_TRACE of the same run), per driver: the forward speed
reached, the largest and mean sideways speed, and how far it went sideways in
all, in world units and units per second. A racer sliding sideways at the
start shows up as a large sideways figure; compare a 30 FPS run with a 60 FPS
run of the same race.
"""
import argparse
import csv
import math
import struct

DRIVER_SIZE = 0x638
POS = 0x2d4      # Vec3 posCurr, world * 256
ANGLE = 0x39a    # s16 angle, 4096 per turn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("trace")
    ap.add_argument("--ms", type=int, default=3000)
    ap.add_argument("--drivers", default="1-7")
    args = ap.parse_args()
    lo, hi = (int(v) for v in args.drivers.split("-"))

    with open(args.trace, newline="") as f:
        rows = list(csv.DictReader(f))
    fps60 = rows[0]["fps60"] == "1"
    frame_ms = 1000.0 / (60 if fps60 else 30)
    green = next(int(r["frame"]) for r in rows if int(r["lights"]) <= 0)

    data = open(args.dump, "rb").read()
    record = 4 + 8 * DRIVER_SIZE
    frames = {}
    for i in range(len(data) // record):
        frame = struct.unpack_from("<i", data, i * record)[0]
        frames[frame] = i * record + 4

    last = green + int(args.ms / frame_ms)
    print(f"{args.dump}: {60 if fps60 else 30} FPS, green at race frame {green}, first {args.ms} ms")
    for drv in range(lo, hi + 1):
        fwd_max = side_max = side_sum = side_total = 0.0
        n = 0
        for frame in range(green + 1, last + 1):
            if frame not in frames or frame - 1 not in frames:
                continue
            a = frames[frame - 1] + drv * DRIVER_SIZE
            b = frames[frame] + drv * DRIVER_SIZE
            pa = struct.unpack_from("<3i", data, a + POS)
            pb = struct.unpack_from("<3i", data, b + POS)
            ang = struct.unpack_from("<h", data, b + ANGLE)[0] * 2 * math.pi / 4096
            dx, dz = (pb[0] - pa[0]) / 256.0, (pb[2] - pa[2]) / 256.0
            fwd = dx * math.sin(ang) + dz * math.cos(ang)
            side = dx * math.cos(ang) - dz * math.sin(ang)
            fwd_max = max(fwd_max, abs(fwd) * 1000 / frame_ms)
            side_max = max(side_max, abs(side) * 1000 / frame_ms)
            side_sum += abs(side) * 1000 / frame_ms
            side_total += side
            n += 1
        if n:
            print(f"  driver {drv}: forward up to {fwd_max:7.0f}/s, sideways up to {side_max:6.0f}/s, "
                  f"mean {side_sum / n:5.0f}/s, net {side_total:+6.0f} units")


if __name__ == "__main__":
    main()
