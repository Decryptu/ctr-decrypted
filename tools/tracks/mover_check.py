"""Did every moving object of a converted track start, and move? (docs/TRACK-CONVERTER.md#checks)

  python tools/tracks/mover_check.py <work folder> <course>

Reads the log of CTR_DEV_MOVER_LOG, which check_track.ps1 sets, against the movers the converter placed.
Exit 1 when one never ran, or stayed in one place and pose over the race, or a kart stood still for
longer than a hit lasts: something holds it.
"""
import collections, csv, json, os, sys

STOPPED = 300  # a kart's speed
LONGEST_STOP = 300  # frames


def longest_stop(trace):
    """The most frames in a row a racing kart stood still, and the frame that ended."""
    best, at, run = 0, 0, 0
    with open(trace) as f:
        for row in csv.DictReader(f):
            run = run + 1 if int(row['raceMS']) > 0 and int(row['end']) == 0 and abs(int(row['speed'])) < STOPPED else 0
            if run > best:
                best, at = run, int(row['frame'])
    return best, at


def main():
    work, course = sys.argv[1:3]
    with open(os.path.join(work, course + '_items.json')) as f:
        wanted = len(json.load(f)['movers'])
    log = os.path.join(work, 'movers.log')
    seen = collections.defaultdict(set)
    unborn = hits = 0
    if os.path.exists(log):
        with open(log) as f:
            for line in f:
                parts = line.split()
                if parts[0] == 'unborn':
                    unborn += 1
                elif parts[0] == 'hit':
                    hits += 1
                else:
                    seen[int(parts[0])].add(tuple(parts[1:]))
    still = sorted(i for i, poses in seen.items() if len(poses) < 2)
    missing = wanted - len(seen)
    print(f'movers: {wanted} placed, {len(seen)} ran, {unborn} found no thread, {len(still)} never moved {still[:12]}, {hits} hit a kart')
    stop, at = longest_stop(os.path.join(work, 'trace.csv'))
    print(f'longest stop {stop} frames, to frame {at}')
    sys.exit(1 if missing or unborn or still or stop > LONGEST_STOP else 0)


if __name__ == '__main__':
    main()
