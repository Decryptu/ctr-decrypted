"""Issue #205: a failed INSTANCE_BirthWithThread must not be dereferenced.

On native the instance pool can be full, so the birth returns NULL. This scans
each hardened call site and requires a NULL check on the returned variable
right after every birth that assigns it. Sites already guarded elsewhere are
included so a later edit cannot quietly drop them.

Run: python3 tools/test-spawn-null-guards.py
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent

# (file, variable the birth result is assigned to)
SITES = [
    ("game/231/RB_GenericMine.c", "instCrate"),
    ("game/231/RB_Crate.c", "explosionInst"),
    ("game/231/RB_MaskShieldCloud.c", "bombInst"),
    ("game/231/RB_MaskShieldCloud.c", "cloudInst"),
    ("game/231/RB_Explosion.c", "shatterInst"),
    ("game/231/RB_Explosion.c", "explosion"),
    ("game/Vehicle/VehTalkMask.c", "mhInst"),
]


def main():
    failed = 0
    for rel, var in SITES:
        text = (ROOT / rel).read_text()
        births = list(re.finditer(r"\b%s = INSTANCE_BirthWithThread\w*\([^;]*\);" % re.escape(var), text))
        ok = len(births) >= 1
        for m in births:
            tail = text[m.end():m.end() + 600]
            if not re.search(r"\b%s [!=]= NULL\b" % re.escape(var), tail):
                ok = False
        print("%s %s %s (%d site(s))" % ("PASS" if ok else "FAIL", rel, var, len(births)))
        failed += 0 if ok else 1
    sys.exit(1 if failed else 0)


main()
