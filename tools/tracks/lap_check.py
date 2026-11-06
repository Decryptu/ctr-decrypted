"""Check from a CTR_DEV_TRACE file that a kart can drive a converted track; check_track.ps1 runs it.

  lap_check.py <trace.csv>

CTR_DEV_AUTOPILOT=3 hops along its line when wedged, and each hop is a jump in the trace. Prints the
laps finished and where it hopped (every place a kart stuck), and exits 1 when it finished no lap,
hopped more than MOST_HOPS times, or hopped twice from one place: being hit is chance, a place that
stops a kart every lap is the course.
"""
import csv, math, sys

JUMP = 500  # LEV units in one frame; a kart at top speed covers about 30
MOST_HOPS = 4  # a mover or a fall can cost a hop or two a lap; more is a place the autopilot cannot pass
SAME_PLACE = 256  # LEV units between two hops

rows = list(csv.DictReader(open(sys.argv[1])))
if not rows:
    sys.exit('no race in the trace')
laps = int(rows[-1]['lap'])
hops = []
for a, b in zip(rows, rows[1:]):
    if math.dist((int(a['x']), int(a['z'])), (int(b['x']), int(b['z']))) > JUMP:
        hops.append((int(a['x']), int(a['y']), int(a['z'])))
print(f'laps finished {laps} in {len(rows)} frames, {len(hops)} hops')
for hop in sorted(set(hops))[:12]:
    print('  stuck at LEV', hop)
if laps < 1:
    sys.exit('the autopilot finished no lap')
if len(hops) > MOST_HOPS:
    sys.exit(f'the autopilot hopped {len(hops)} times')
for i, a in enumerate(hops):
    if any(math.dist(a, b) < SAME_PLACE for b in hops[:i]):
        sys.exit(f'the autopilot hopped twice from LEV {a}')
