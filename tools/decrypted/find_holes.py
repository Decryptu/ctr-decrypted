"""Find see-through holes in frames captured with CTR_PGXP_DEBUG=1.

With the debug tint, polygons PGXP could not recover draw pure blue. The sky
backdrop is such a polygon, so any blue pixel with geometry directly above and
below it in its column is a place where the sky shows through the world: a
crack or a missing polygon.

Usage: python -I tools/decrypted/find_holes.py <capture dir> [--max-run N]
Prints one line per frame (hole pixel count, bounding box) and writes
holes-<frame>.png next to each frame with holes, with holes painted magenta.
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image


def hole_mask(rgb, max_run):
    r, g, b = rgb[..., 0].astype(int), rgb[..., 1].astype(int), rgb[..., 2].astype(int)
    blue = (b > 150) & (r < 60) & (g < 60)
    h, w = blue.shape
    holes = np.zeros_like(blue)
    # Vertical runs of blue enclosed by non-blue above and below.
    for x in range(w):
        col = blue[:, x]
        y = 0
        while y < h:
            if not col[y]:
                y += 1
                continue
            start = y
            while y < h and col[y]:
                y += 1
            if start > 0 and y < h and (y - start) <= max_run:
                holes[start:y, x] = True
    return holes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--max-run", type=int, default=12, help="tallest hole in pixels")
    ap.add_argument("--ignore-top", type=int, default=0, help="skip this many rows (HUD text)")
    args = ap.parse_args()

    total = 0
    for path in sorted(glob.glob(os.path.join(args.dir, "f*.png"))):
        rgb = np.asarray(Image.open(path).convert("RGB"))
        mask = hole_mask(rgb, args.max_run)
        mask[: args.ignore_top] = False
        count = int(mask.sum())
        total += count
        name = os.path.splitext(os.path.basename(path))[0]
        if count:
            ys, xs = np.nonzero(mask)
            print(f"{name} holes={count} box=({xs.min()},{ys.min()})-({xs.max()},{ys.max()})")
            out = rgb.copy()
            out[mask] = (255, 0, 255)
            Image.fromarray(out).save(os.path.join(args.dir, f"holes-{name}.png"))
        else:
            print(f"{name} holes=0")
    print(f"TOTAL {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
