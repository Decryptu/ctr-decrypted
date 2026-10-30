"""Frame pacing of a --perf run, measured against the display's refresh.

    perf_pacing.py FRAME_TIMES.csv HZ [--level N] [--skip N]

Reads frame_times.csv (written by ctr_native --perf / --perf-dir), keeps the
frames played on level N (default: every frame), skips the
first --skip of them (default 60), and prints one line:

- the median and 1st/99th percentile frame interval,
- how many display refreshes each frame stayed up, as a histogram,
- uneven: frames whose refresh count differs from the most common one,
- long: frames over 1.5 times their nominal length (a game frame of
  vsync_till_flip VBlanks at 59.94 Hz), the ones a player sees as a hitch.

A frame interval is the time from one frame's start to the next, so with
VSync On it includes the wait for the swap and follows the display.
"""
import csv
import statistics
import sys


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    path, hz = args[0], float(args[1])
    level = int(args[args.index('--level') + 1]) if '--level' in args else None
    skip = int(args[args.index('--skip') + 1]) if '--skip' in args else 60

    rows = []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            if level is not None and int(r['level_id']) != level:
                continue
            rows.append(r)
    rows = rows[skip:]
    if not rows:
        print('no frames')
        return

    refresh = 1000.0 / hz
    ms = [float(r['total_ms']) for r in rows]
    counts = [max(1, round(m / refresh)) for m in ms]
    hist = {}
    for c in counts:
        hist[c] = hist.get(c, 0) + 1
    mode = max(hist, key=hist.get)
    uneven = sum(1 for c in counts if c != mode)
    long_ = sum(1 for r, m in zip(rows, ms) if m > 1.5 * (1000.0 / 59.94) * max(1, int(r['vsync_till_flip'])))
    q = statistics.quantiles(ms, n=100)
    shown = ' '.join(f'{k}:{hist[k]}' for k in sorted(hist))
    print(f'{len(ms)} frames, median {statistics.median(ms):.2f} ms (p1 {q[0]:.2f}, p99 {q[98]:.2f}, max {max(ms):.2f}), '
          f'refreshes per frame {shown}, uneven {uneven} ({100.0 * uneven / len(ms):.1f}%), long {long_}')


if __name__ == '__main__':
    main()
