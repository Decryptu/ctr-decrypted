"""Is every moving object at rest placed and turned as the course's map places it in MKDS?
(docs/TRACK-CONVERTER.md#checks)

  python tools/tracks/mover_pose_check.py <items.json> <poses file>

The poses file is CTR_DEV_MOVER_POSES of a run with CTR_DEV_MOVERS_REST, which check_track.ps1 makes:
each mover instance's place, matrix and scale as the renderer gets them. For each object the map turns
(its items.json entry has a rot), the turn MKDS gives it (mkds_convert.rotate of its axes) must be its
matrix, its pieces must lie where that turn puts them from its origin, and its scale must be its own.
Exit 1 on any that is not.
"""
import collections, json, math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkds_convert, mkds_movers

TURN_OFF = 0.02  # of a unit vector: about a degree
PLACE_OFF = 8  # level units
RECORD_POINTS = 7  # Mover.c's MOVER_RECORD_POINTS: the first record after the fixed ones


def rest_turn(records):
    """The turn a mover holds at rest about its own axis, after its placed one: a flipper's rest stroke
    (Mover.c, MOVER_SWING with no period); none for the rest."""
    turn = np.eye(3)
    if records[0][0] == mkds_movers.SWING and records[2][1] == 0:
        axis, angle = records[2][0], records[RECORD_POINTS][0] * 2 * math.pi / mkds_movers.TURN
        a, b = (axis + 1) % 3, (axis + 2) % 3
        turn[a, a] = turn[b, b] = math.cos(angle)
        turn[a, b], turn[b, a] = -math.sin(angle), math.sin(angle)
    return turn


def main():
    items, poses = sys.argv[1:3]
    with open(items) as f:
        placed = json.load(f)
    movers, lists = placed['movers'], placed['mover_lists']
    logged = collections.defaultdict(list)
    with open(poses) as f:
        for line in f:
            v = [int(x) for x in line.split()]
            logged[v[0]].append({'t': np.array(v[2:5], float), 'm': np.array(v[5:14], float).reshape(3, 3) / 4096, 'scale': v[14:17]})
    by_list = collections.defaultdict(list)
    for m in movers:
        by_list[m['list']].append(m)
    bad = checked = 0
    for listed, pieces in sorted(by_list.items()):
        rot = pieces[0].get('rot')
        if rot is None:
            continue
        checked += 1
        name = pieces[0]['name']
        drawn = logged.get(listed)
        if not drawn:
            print(f'{name:16} list {listed}: never placed')
            bad += 1
            continue
        want = np.array([mkds_convert.rotate(axis, rot) for axis in np.eye(3)]).T @ rest_turn(lists[listed])
        turn = max(np.abs(d['m'] - want).max() for d in drawn)
        origin = np.array(pieces[0]['pos'], float)
        places = [origin + want @ np.array(p['offset'], float) for p in pieces]
        place = max(min(np.linalg.norm(d['t'] - p) for d in drawn) for p in places)
        scales = sorted(tuple(p['scale']) for p in pieces) != sorted(tuple(d['scale']) for d in drawn)
        wrong = []
        if turn > TURN_OFF:
            wrong.append(f'turned off by {turn:.2f}')
        if place > PLACE_OFF:
            wrong.append(f'a piece {place:.0f} units from its place')
        if len(drawn) != len(pieces):
            wrong.append(f'{len(drawn)} of {len(pieces)} pieces placed')
        if scales:
            wrong.append('scaled otherwise')
        if wrong:
            bad += 1
            print(f'{name:16} list {listed} rot {rot}: ' + ', '.join(wrong))
    print(f'mover poses: {checked} turned objects, {bad} wrong')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
