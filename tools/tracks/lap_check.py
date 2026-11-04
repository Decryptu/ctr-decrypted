"""Check from a CTR_DEV_TRACE file that a kart can drive a converted track; check_track.ps1 runs it.

  lap_check.py <trace.csv>

CTR_DEV_AUTOPILOT=3 hops along its line when wedged, and each hop is a jump in the trace. Prints the
laps finished and where it hopped (every place a kart stuck), and exits 1 when it finished no lap.
"""
import csv, math, sys

JUMP = 500  # LEV units in one frame; a kart at top speed covers about 30

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
