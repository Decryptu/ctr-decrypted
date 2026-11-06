"""Views of what moves on a converted course, for CTR_DEV_CAMERA_TOUR (docs/DEV-TOOLS.md).

  python tools/tracks/mover_tour.py <work>/<course>_items.json <out.txt>

A line per view: the mover's list, the camera's offset from it, the camera's rotation. One mover of each
model, from VIEWS sides, at a distance fitting its size, looking at the middle of its box. Writes the same
views as JSON beside it, for mover_view.py.
"""
import json, math, sys

TURN = 4096
LEVEL_PITCH = 2048
ELEVATION = 0.3  # radians above the mover
VIEWS = 4
FIRST_ANGLE = 0.5  # radians: no view looks along a model's own axes, where a flat one is edge on
DISTANCE = 3.5  # of its reach
NEAREST = 400


def view(list_number, middle, distance, angle):
    """The camera at this distance from the point `middle` from a mover and angle round it, looking at it."""
    height = math.sin(ELEVATION) * distance
    across = math.cos(ELEVATION) * distance
    dx, dz = math.sin(angle) * across, math.cos(angle) * across
    yaw = int(math.atan2(-dx, -dz) * TURN / (2 * math.pi)) + TURN // 2
    pitch = LEVEL_PITCH - int(ELEVATION * TURN / (2 * math.pi))
    return f'{list_number} {int(middle[0] + dx)} {int(middle[1] + height)} {int(middle[2] + dz)} {pitch} {yaw & (TURN - 1)} 0'


def main():
    placed = json.load(open(sys.argv[1]))
    lines, views, seen = [], [], set()
    for mover in placed['movers']:
        if mover['model'] in seen:
            continue
        seen.add(mover['model'])
        records = placed['mover_lists'][mover['list']]
        base = [[c / TURN for c in row] for row in records[3:6]]
        middle = [sum(a * b for a, b in zip(row, records[6])) for row in base]
        for i in range(VIEWS):
            lines.append(view(mover['list'], middle, max(mover['reach'] * DISTANCE, NEAREST), i * 2 * math.pi / VIEWS + FIRST_ANGLE))
            views.append({'name': mover['name'], 'kind': records[0][0], 'flags': records[0][2]})
    open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
    json.dump(views, open(sys.argv[2] + '.json', 'w'))


if __name__ == '__main__':
    main()
