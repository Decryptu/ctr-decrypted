"""Whether every moving object of a converted course is drawn, whole, from every side, where it is.

  python tools/tracks/mover_view.py <tour.txt.json> <frames with movers> <frames without>

Both runs drew no level and looked at each mover from all round it (CTR_DEV_CAMERA_TOUR,
CTR_DEV_MOVERS_ONLY, CTR_DEV_HIDE_MOVERS); what differs between their frames is the mover alone. A
picture that does not turn to the camera, or a body with no back, leaves a view with next to nothing; a
model drawn away from its origin leaves its pixels off the middle of the frame. Exit code 1 on a failure.
"""
import glob, json, os, sys

import numpy as np
from PIL import Image

DIFFERS = 24  # of 255, on any colour
FEWEST = 0.0006  # of the frame: nothing there
SIDES_ALIKE = 0.12  # a view may show this little of the fullest view of a model
MOST_OFF = 0.12  # of the frame's height, from the middle
CELL = 8  # pixels: parts of one model this close are one thing
SWEEPS = {1, 2, 4}  # SPIN, SWING, ORBIT: they go round, so the frame's middle says nothing, and a thin one turns edge on
COMES_AND_GOES = {5}  # PULSE: out of sight for part of its time
OUT_ONLY = 0x800


def frames(folder):
    return sorted(f for f in glob.glob(os.path.join(folder, 'f*.png')))


def nearest_part(mask):
    """The connected part of a mask nearest the middle of the frame, in cells of CELL pixels."""
    h, w = mask.shape
    ys, xs = np.nonzero(mask)
    i = np.argmin((ys - h / 2) ** 2 + (xs - w / 2) ** 2)
    cells = {(int(y) // CELL, int(x) // CELL) for y, x in zip(ys, xs)}
    todo, found = [(int(ys[i]) // CELL, int(xs[i]) // CELL)], set()
    while todo:
        cell = todo.pop()
        if cell in cells and cell not in found:
            found.add(cell)
            todo += [(cell[0] + a, cell[1] + b) for a in (-1, 0, 1) for b in (-1, 0, 1)]
    part = np.zeros_like(mask)
    for y, x in found:
        part[y * CELL:(y + 1) * CELL, x * CELL:(x + 1) * CELL] = True
    return part & mask


def main():
    views = json.load(open(sys.argv[1]))
    shown, hidden = frames(sys.argv[2]), frames(sys.argv[3])
    if len(shown) < len(views) or len(hidden) < len(views):
        sys.exit(f'{len(shown)} and {len(hidden)} frames for {len(views)} views')
    seen = {}
    for view, a, b in zip(views, shown, hidden):
        one, other = np.array(Image.open(a).convert('RGB'), int), np.array(Image.open(b).convert('RGB'), int)
        mask = (np.abs(one - other) > DIFFERS).any(2)
        h, w = mask.shape
        mask[:h // 8] = mask[h * 3 // 4:] = False  # the race's own display
        mask[:, :w // 8] = mask[:, w * 7 // 8:] = False
        # other movers can be in the frame: the one looked at is what lies nearest its middle
        area, off = 0.0, 0.0
        if mask.any():
            part = nearest_part(mask)
            ys, xs = np.nonzero(part)
            area = part.sum() / mask.size
            off = max(abs(ys.mean() - h / 2), abs(xs.mean() - w / 2)) / h
        seen.setdefault(view['name'], (view, []))[1].append((area, off))
    failed = 0
    for name, (view, sides) in seen.items():
        areas = [a for a, _ in sides]
        faults = []
        if max(areas) < FEWEST:
            faults.append('not drawn')
        elif view['kind'] not in COMES_AND_GOES and not view['flags'] & OUT_ONLY:
            if min(areas) < FEWEST or (view['kind'] not in SWEEPS and min(areas) < SIDES_ALIKE * max(areas)):
                faults.append('missing from a side')
        if view['kind'] not in SWEEPS | COMES_AND_GOES and max(areas) >= FEWEST and max(o for a, o in sides if a >= FEWEST) > MOST_OFF:
            faults.append('drawn away from its place')
        print(f"{name}: {' '.join(f'{a * 100:.2f}%' for a in areas)}, off {max(o for _, o in sides):.2f}{'  ' + ', '.join(faults) if faults else ''}")
        failed += bool(faults)
    print(f'mover view: {len(seen)} models, {failed} wrong')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
