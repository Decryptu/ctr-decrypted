"""Find where two races stop playing the same, from CTR_DEV_DRIVER_DUMP files.

Each record is the race frame (int32) followed by the eight Driver structs. Two
runs of the same build, or of builds loaded at different addresses, differ in
their pointer fields; a word is skipped when both values look like host
pointers, or when --delta is given and they differ by exactly that (the load
base difference of two fixed-base builds). The first frame with any other
difference is printed, with each differing word as driver, offset, values.

  python tools/decrypted/driver_diff.py A.bin B.bin [--delta 0xfc00000] [--size 0x638]
"""
import argparse, struct

ap = argparse.ArgumentParser()
ap.add_argument("a")
ap.add_argument("b")
ap.add_argument("--delta", type=lambda s: int(s, 0), default=None)
ap.add_argument("--size", type=lambda s: int(s, 0), default=0x638, help="sizeof(struct Driver)")
args = ap.parse_args()

a = open(args.a, "rb").read()
b = open(args.b, "rb").read()
rec = 4 + 8 * args.size
count = min(len(a), len(b)) // rec


def pointer_like(v):
    return 0x00100000 <= v < 0x7fff0000


for i in range(count):
    diffs = []
    for drv in range(8):
        base = i * rec + 4 + drv * args.size
        for off in range(0, args.size, 4):
            p = struct.unpack_from("<I", a, base + off)[0]
            q = struct.unpack_from("<I", b, base + off)[0]
            if p == q:
                continue
            if args.delta is not None and ((q - p) & 0xFFFFFFFF) == args.delta:
                continue
            if args.delta is None and pointer_like(p) and pointer_like(q):
                continue
            diffs.append((drv, off, p, q))
    if diffs:
        frame = struct.unpack_from("<i", a, i * rec)[0]
        print(f"first difference at race frame {frame} (record {i})")
        for drv, off, p, q in diffs[:24]:
            print(f"  driver {drv} +0x{off:03x}: 0x{p:08x} vs 0x{q:08x}")
        break
else:
    print(f"no difference in {count} frames")
