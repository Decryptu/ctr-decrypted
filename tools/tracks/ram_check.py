"""Does every moving object of a converted course do something to a kart that drives into it?

  python tools/tracks/ram_check.py <course> [--exe <exe>] [--run <run folder>]

Needs the work folder mkds_batch.py left (C:/t/mkds-<course>). The race runs with CTR_DEV_RAM: player 1
starts on the road behind each object that has a hit box, at rest, and drives at it for three seconds,
with CTR_DEV_MOVERS_OPEN running at once what waits for a lap or a kart. What any object did meanwhile counts:
a Pokey's lowest part hits for the parts above it.
Each prints what it did: hurt (the hazard code took the kart), pushed (solid, the kart was stopped at it),
crate (an item box broke), or nothing; out of reach when it was over or under the kart as it passed (a shot in
its arc). Exit 1 when one did nothing within reach.
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EXE = os.path.join(REPO, 'build-mrg-client', 'ctr_native_authoring.exe')
PUSH, HURT, CRATE = 1, 2, 4  # Mover.c's MOVER_EVENT_
KART = 48  # Mover.c's MOVER_KART_RADIUS
FRAMES_EACH = 1800  # presented frames one object may take: three tries of a hold and a drive
SETTLE = 1700


def run(course, exe, run_dir, count):
    work = f'C:/t/mkds-{course}'
    log = f'{work}/ram.log'
    pin = open(f'{work}/pin.txt').read().strip()
    if os.path.exists(log):
        os.remove(log)
    env = f"@{{CTR_DEV_CUSTOM='{pin}'; CTR_DEV_RAM='{log}'; CTR_DEV_MOVERS_OPEN=1}}"
    script = (f"$p='{run_dir}\\assets\\tracks\\packages'; New-Item -ItemType Directory -Force $p | Out-Null; "
              f"Get-ChildItem $p -Directory | Where-Object Name -ne '{pin}' | Remove-Item -Recurse -Force; "
              f"Copy-Item '{work}/packages/{pin}' $p -Recurse -Force; "
              f"& '{os.path.join(REPO, 'tools', 'decrypted', 'devrun.ps1')}' -Track -1 -Frames {SETTLE + FRAMES_EACH * count} -Every 100000 "
              f"-Exe '{exe}' -RunDir '{run_dir}' -Out '{work}/ram-shots' -TimeoutSec 900 -Env {env} | Out-Null")
    subprocess.run(['pwsh', '-NoProfile', '-Command', script], check=True)
    return open(log).read().split('\n') if os.path.exists(log) else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('course')
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--run', default='C:\\t\\ram-run')
    a = ap.parse_args()
    items = json.load(open(f'C:/t/mkds-{a.course}/{a.course}_items.json'))
    names = {m['list']: m['name'] for m in items['movers']}
    halves = {n: max(abs(h) for h in rows[1]) for n, rows in enumerate(items['mover_lists'])}
    lines = run(a.course, a.exe, a.run, len(names))
    bad, seen = [], 0
    for line in lines:
        parts = line.split()
        if not parts or parts[0] != 'ram':
            continue
        seen += 1
        listed, state, nearest, passed, events, above = int(parts[1]), int(parts[5]), int(parts[7]), int(parts[9]), int(parts[15]), int(parts[17])
        what = [w for bit, w in ((HURT, 'hurt'), (PUSH, 'pushed'), (CRATE, 'crate')) if events & bit] or ['nothing']
        reach = halves.get(listed, 0) + KART
        out_of_reach = events == 0 and nearest < reach and abs(above) > reach  # it passed over or under the kart
        print(f'{a.course:16} {listed:4} {names.get(listed, "?"):16} {"+".join(what):12} kart state {state} nearest {nearest} {above:+d} up'
              f'{" went through" if passed else ""}{" out of reach" if out_of_reach else ""}')
        if events == 0 and not out_of_reach:
            bad.append(names.get(listed, '?'))
    done = any(line.strip() == 'done' for line in lines)
    print(f'{a.course}: {seen} rammed, {len(bad)} did nothing {sorted(set(bad))}{"" if done else ", run ended before the last"}')
    sys.exit(1 if bad or not done else 0)


if __name__ == '__main__':
    main()
