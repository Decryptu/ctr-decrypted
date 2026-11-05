"""Convert and check every Mario Kart DS race course, and print one line for each.

  python tools/tracks/mkds_batch.py <rom.nds> --disc <ctr-u.bin> [--check] [course ...]

Each course converts in its own process, into C:/t/mkds-<course>, its output in convert.log there.
With --check, the courses that converted race through check_track.ps1 (check.log). Courses left out
of the command line all run. The exit code is 1 when any course failed.
With --fps they then race one at a time through track_fps.ps1 at the owner's settings, and each prints
its frame times and the place of its worst frames: run nothing else meanwhile.
"""
import argparse, concurrent.futures, csv, os, queue, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

COURSES = {
    'cross_course': 'Figure-8 Circuit', 'bank_course': 'Yoshi Falls', 'beach_course': 'Cheep Cheep Beach',
    'mansion_course': "Luigi's Mansion", 'desert_course': 'Desert Hills', 'town_course': 'Delfino Square',
    'pinball_course': 'Waluigi Pinball', 'ridge_course': 'Shroom Ridge', 'snow_course': 'DK Pass',
    'clock_course': 'Tick-Tock Clock', 'mario_course': 'Mario Circuit', 'airship_course': 'Airship Fortress',
    'stadium_course': 'Wario Stadium', 'garden_course': 'Peach Gardens', 'koopa_course': 'Bowser Castle',
    'rainbow_course': 'Rainbow Road', 'old_mario_sfc': 'SNES Mario Circuit 1', 'old_momo_64': 'N64 Moo Moo Farm',
    'old_peach_agb': 'GBA Peach Circuit', 'old_luigi_gc': 'GCN Luigi Circuit', 'old_donut_sfc': 'SNES Donut Plains 1',
    'old_frappe_64': 'N64 Frappe Snowland', 'old_koopa_agb': 'GBA Bowser Castle 2', 'old_baby_gc': 'GCN Baby Park',
    'old_noko_sfc': 'SNES Koopa Beach 2', 'old_choco_64': 'N64 Choco Mountain', 'old_luigi_agb': 'GBA Luigi Circuit',
    'old_kinoko_gc': 'GCN Mushroom Bridge', 'old_choco_sfc': 'SNES Choco Island 2', 'old_hyudoro_64': 'N64 Banshee Boardwalk',
    'old_sky_agb': 'GBA Sky Garden', 'old_yoshi_gc': 'GCN Yoshi Circuit',
}


# A course that shows more of itself at once than the game's 256 near blocks takes larger blocks
MAX_EDGE = {'pinball_course': 48, 'airship_course': 32}


def reason(text):
    """The last line that says why a run failed."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    wanted = [line for line in lines if re.search(r'WARNING|Error|error|failed|fail|turned|no VRM|represent', line)]
    return (wanted or lines or ['no output'])[-1][:150]


def convert(rom, disc, course):
    work = f'C:/t/mkds-{course}'
    os.makedirs(work, exist_ok=True)
    pin = os.path.join(work, 'pin.txt')
    if os.path.exists(pin):
        os.remove(pin)
    edge = ['--max-edge', str(MAX_EDGE[course])] if course in MAX_EDGE else []
    r = subprocess.run([sys.executable, os.path.join(HERE, 'mkds_convert.py'), rom, course, '--title', COURSES[course], '--disc', disc, *edge],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    with open(os.path.join(work, 'convert.log'), 'w', encoding='utf-8') as f:
        f.write(r.stdout)
    return course, r.returncode == 0, reason(r.stdout)


def check(rom, course, runs):
    run = runs.get()
    try:
        r = subprocess.run(['pwsh', '-NoProfile', '-File', os.path.join(HERE, 'check_track.ps1'),
                            '-Rom', rom, '-Course', course, '-RunDir', run],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    finally:
        runs.put(run)
    with open(f'C:/t/mkds-{course}/check.log', 'w', encoding='utf-8') as f:
        f.write(r.stdout)
    render = re.search(r'rendered list peak (\d+)', r.stdout)
    laps = re.search(r'laps finished (\d+)', r.stdout)
    hops = re.search(r'(\d+) hops', r.stdout)
    detail = f"peak {render[1] if render else "?"}, laps {laps[1] if laps else "?"}, hops {hops[1] if hops else "?"}"
    return course, r.returncode == 0, detail if r.returncode == 0 else f'{detail}: {reason(r.stdout)}'


BUDGET_MS = 1000 / 59.94
WORST_FRAMES = 0.01  # the share of a course's frames its worst place is taken from


def frame_times(course):
    """-> a line of the race's frame times, and where its worst frames are. The trace starts with the
    level's first frame and has one row per frame, so the perf rows of that level pair off with it."""
    folder = f'C:/t/mkds-{course}/fps'
    with open(f'{folder}/trace.csv', newline='') as f:
        trace = list(csv.DictReader(f))
    with open(f'{folder}/frame_times.csv', newline='') as f:
        rows = [r for r in csv.DictReader(f) if r['level_id'] == trace[0]['level']][-len(trace):]
    start = next(i for i, t in enumerate(trace[-len(rows):]) if int(t['raceMS']) > 0)
    race = list(zip(rows, trace[-len(rows):]))[start:]
    ms = sorted(float(r['work_ms']) for r, _ in race)
    over = sum(m > BUDGET_MS for m in ms) / len(ms)
    worst = sorted(race, key=lambda p: -float(p[0]['work_ms']))[:max(1, int(len(race) * WORST_FRAMES))]
    x, y, z = (sorted(int(t[k]) for _, t in worst)[len(worst) // 2] for k in 'xyz')
    return (f'median {ms[len(ms) // 2]:.1f} ms, 1% worst {ms[int(len(ms) * (1 - WORST_FRAMES))]:.1f} ms, max {ms[-1]:.1f} ms, '
            f'{over:.0%} over {BUDGET_MS:.1f} ms; worst round ({x}, {y}, {z})')


def fps(course):
    r = subprocess.run(['pwsh', '-NoProfile', '-File', os.path.join(HERE, 'track_fps.ps1'), '-Course', course],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    if r.returncode != 0:
        return course, False, reason(r.stdout)
    return course, True, frame_times(course)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom')
    ap.add_argument('courses', nargs='*', help='default: all of them')
    ap.add_argument('--disc', required=True)
    ap.add_argument('--check', action='store_true', help='race the courses that converted')
    ap.add_argument('--fps', action='store_true', help='time the courses that converted, one at a time')
    ap.add_argument('--no-convert', action='store_true', help='check what the last run converted')
    ap.add_argument('--jobs', type=int, default=6, help='conversions at one time')
    ap.add_argument('--races', type=int, default=4, help='races at one time, each with its own run folder')
    a = ap.parse_args()
    courses = a.courses or list(COURSES)
    failed = False

    converted = courses
    if not a.no_convert:
        converted = []
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
            for future in concurrent.futures.as_completed([pool.submit(convert, a.rom, a.disc, c) for c in courses]):
                course, ok, why = future.result()
                print(f"convert {course:16} {'ok' if ok else 'FAIL ' + why}", flush=True)
                failed |= not ok
                if ok:
                    converted.append(course)

    if a.check:
        runs = queue.Queue()
        for i in range(a.races):
            runs.put(f'C:\\t\\mkds-run{i}')
        with concurrent.futures.ThreadPoolExecutor(a.races) as pool:
            for future in concurrent.futures.as_completed([pool.submit(check, a.rom, c, runs) for c in converted]):
                course, ok, why = future.result()
                print(f"check   {course:16} {'PASS ' if ok else 'FAIL '}{why}", flush=True)
                failed |= not ok

    if a.fps:
        for c in converted:
            course, ok, line = fps(c)
            print(f"fps     {course:16} {'' if ok else 'FAIL '}{line}", flush=True)
            failed |= not ok
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
