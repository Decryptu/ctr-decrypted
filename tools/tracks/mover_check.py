"""Did every moving object of a converted track start, and move? (docs/TRACK-CONVERTER.md#checks)

  python tools/tracks/mover_check.py <work folder> <course>

Reads the log of CTR_DEV_MOVER_LOG, which check_track.ps1 sets, against the movers the converter placed.
Exit 1 when one never ran, or stayed in one place and pose over the race.
"""
import collections, json, os, sys


def main():
    work, course = sys.argv[1:3]
    with open(os.path.join(work, course + '_items.json')) as f:
        wanted = len(json.load(f)['movers'])
    log = os.path.join(work, 'movers.log')
    seen = collections.defaultdict(set)
    unborn = 0
    if os.path.exists(log):
        with open(log) as f:
            for line in f:
                parts = line.split()
                if parts[0] == 'unborn':
                    unborn += 1
                else:
                    seen[int(parts[0])].add(tuple(parts[1:]))
    still = sorted(i for i, poses in seen.items() if len(poses) < 2)
    missing = wanted - len(seen)
    print(f'movers: {wanted} placed, {len(seen)} ran, {unborn} found no thread, {len(still)} never moved {still[:12]}')
    sys.exit(1 if missing or unborn or still else 0)


if __name__ == '__main__':
    main()
