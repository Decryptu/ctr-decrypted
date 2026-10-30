"""Measure screen-filter captures (CTR_DEV_SHOT_PRESENTED) against a baseline.

Usage: filter_check.py BASELINE.png CAPTURE.png [CAPTURE.png ...] [--lines 216]

For each capture, against the baseline (normally the Smooth capture of the
same frame), prints:
  brightness  mean luma as a percentage of the baseline's. A filter should
              keep it near 100.
  steps       99th percentile of the luma step between neighbouring pixels,
              as a percentage of the baseline's. Anti-aliasing lowers it.
  lines       share of the vertical luma variation at the PSX line frequency
              (--lines lines over the image height; CTR shows 216), in percent. Scanlines
              raise it; it stays near 0 without them.
Needs numpy and Pillow (run without -I).
"""
import argparse
import sys

import numpy as np
from PIL import Image


def luma(path):
    rgb = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64) / 255.0
    return rgb @ np.array([0.299, 0.587, 0.114])


def steps(lum):
    dx = np.abs(np.diff(lum, axis=1))[:-1, :]
    dy = np.abs(np.diff(lum, axis=0))[:, :-1]
    return np.percentile(np.maximum(dx, dy), 99)


def line_share(lum, lines):
    cols = lum - lum.mean(axis=0)
    power = (np.abs(np.fft.rfft(cols, axis=0)) ** 2).mean(axis=1)
    # Bin k is k cycles per image height: the line frequency is bin `lines`.
    band = power[lines - 2:lines + 3].sum()
    return 100.0 * band / power[1:].sum()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('baseline')
    ap.add_argument('captures', nargs='+')
    ap.add_argument('--lines', type=int, default=216)
    a = ap.parse_args()
    base = luma(a.baseline)
    base_mean, base_steps = base.mean(), steps(base)
    print(f'{"capture":40s} brightness  steps  lines')
    for path in [a.baseline] + a.captures:
        lum = luma(path)
        print(f'{path[-40:]:40s} {100 * lum.mean() / base_mean:9.1f}% {100 * steps(lum) / base_steps:5.0f}% '
              f'{line_share(lum, a.lines):5.1f}%')
    return 0


if __name__ == '__main__':
    sys.exit(main())
