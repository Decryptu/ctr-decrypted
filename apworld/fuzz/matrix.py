"""Print the fuzz job matrix for a tier, from arms.txt.

    python matrix.py push   # the arms with a push count, at that count
    python matrix.py full   # every arm at its full count

Writes `matrix=<json>` for $GITHUB_OUTPUT, and the arm count and generation
total to stderr.
"""
import json
import os
import sys

tier = sys.argv[1]
if tier not in ("push", "full"):
    sys.exit(f"tier must be push or full, not {tier!r}")
arms = []
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "arms.txt"), encoding="utf-8") as f:
    for line in f:
        if not line.strip() or line.startswith("#"):
            continue
        name, full, push, meta, hook = line.split()
        runs = int(full if tier == "full" else push)
        if not runs:
            continue
        hook = "" if hook == "-" else hook
        arms.append({"name": name, "runs": runs, "meta": meta, "hook": hook,
                     "tracker": "yes" if hook == "hooks.ctr_ut_hook:Hook" else ""})
names = [arm["name"] for arm in arms]
if len(set(names)) != len(names):
    sys.exit("arms.txt names an arm twice")
print(f"{tier}: {len(arms)} arms, {sum(arm['runs'] for arm in arms)} generations", file=sys.stderr)
print("matrix=" + json.dumps({"include": arms}, separators=(",", ":")))
