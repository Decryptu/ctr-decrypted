"""A sheet of every moving object of the converted courses, each where it is on its course, to look at.

  python tools/tracks/mover_sheet.py <out.png> [course ...]

Needs the work folders mkds_batch.py left. Each course races once with the camera on one view of each of
its models, the level drawn (CTR_DEV_CAMERA_TOUR, CTR_DEV_MOVERS_HELD), and the frames go on one sheet,
each named by its course and model. Courses left out of the command line all run.
"""
import glob, json, os, subprocess, sys

from PIL import Image, ImageDraw

import mkds_batch

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EXE = os.path.join(REPO, 'build-mrg-client', 'ctr_native_authoring.exe')
TOUR_FROM, TOUR_EVERY = 1580, 20
CELL = (320, 180)
ACROSS = 6


def views(course):
    """One view of each model, from the four mover_tour.py writes. -> their names."""
    work = f'C:/t/mkds-{course}'
    subprocess.run([sys.executable, os.path.join(HERE, 'mover_tour.py'), f'{work}/{course}_items.json', f'{work}/tour.txt'], check=True)
    lines = open(f'{work}/tour.txt').read().split('\n')
    names = json.load(open(f'{work}/tour.txt.json'))
    kept = list(range(0, len(names), 4))
    with open(f'{work}/sheet.txt', 'w') as f:
        f.write('\n'.join(lines[i] for i in kept) + '\n')
    return [names[i]['name'] for i in kept]


def shoot(course, count, run_dir):
    work = f'C:/t/mkds-{course}'
    out = f'{work}/sheet'
    pin = open(f'{work}/pin.txt').read().strip()
    env = f"@{{CTR_DEV_CUSTOM='{pin}'; CTR_DEV_CAMERA_TOUR='{work}/sheet.txt'; CTR_DEV_CAMERA_TOUR_FROM={TOUR_FROM}; CTR_DEV_CAMERA_TOUR_EVERY={TOUR_EVERY}; CTR_DEV_MOVERS_HELD=1}}"
    script = (f"$p='{run_dir}\\assets\\tracks\\packages'; New-Item -ItemType Directory -Force $p | Out-Null; "
              f"Get-ChildItem $p -Directory | Where-Object Name -ne '{pin}' | Remove-Item -Recurse -Force; "
              f"Copy-Item '{work}/packages/{pin}' $p -Recurse -Force; "
              f"Remove-Item '{out}' -Recurse -Force -ErrorAction SilentlyContinue; "
              f"& '{os.path.join(REPO, 'tools', 'decrypted', 'devrun.ps1')}' -Track -1 -Frames {TOUR_FROM + TOUR_EVERY * count + 10} -From {TOUR_FROM + 10} -Every {TOUR_EVERY} "
              f"-Exe '{EXE}' -RunDir '{run_dir}' -Out '{out}' -TimeoutSec 600 -Env {env} | Out-Null")
    subprocess.run(['pwsh', '-NoProfile', '-Command', script], check=True)
    return sorted(glob.glob(f'{out}/f*.png'))


def main():
    out, courses = sys.argv[1], sys.argv[2:] or list(mkds_batch.COURSES)
    cells = []
    for course in courses:
        names = views(course)
        if not names:
            continue
        frames = shoot(course, len(names), 'C:\\t\\mkds-fps')
        for name, frame in zip(names, frames):
            cell = Image.open(frame).convert('RGB').resize(CELL)
            ImageDraw.Draw(cell).text((4, 4), f'{course} {name}', fill=(255, 255, 0))
            cells.append(cell)
    rows = (len(cells) + ACROSS - 1) // ACROSS
    sheet = Image.new('RGB', (ACROSS * CELL[0], rows * CELL[1]))
    for i, cell in enumerate(cells):
        sheet.paste(cell, (i % ACROSS * CELL[0], i // ACROSS * CELL[1]))
    sheet.save(out)


if __name__ == '__main__':
    main()
