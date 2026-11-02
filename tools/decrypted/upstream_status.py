"""How far behind each upstream we are, and what we took from High Octane.

    python tools/decrypted/upstream_status.py              # fetch the upstream remotes, then report
    python tools/decrypted/upstream_status.py --no-fetch
    python tools/decrypted/upstream_status.py --ho open,maybe   # list High Octane commits by status
    python tools/decrypted/upstream_status.py --all        # every new commit, not only the latest few

An upstream commit counts as integrated when a commit with the same author time, author email and
subject is in our history (--base, default HEAD). That survives sync-upstream.ps1's message
rewrite, its path filter (the apworld), and cherry-picks, so no hash mapping is needed. For each
followed branch the report gives the newest integrated commit and every newer one we lack. Other
branches of dowlle and the apworld fork are listed when they hold commits that are neither ours nor
on a followed branch.

High Octane is ported, never merged. Its commits not in ctr-native are each either cherry-picked
(found by the key above or by `git cherry-pick -x`'s line) or listed with a status in
highoctane.txt next to this script; a line there wins over a cherry-pick, for one taken only in
part. A commit in neither place is new since the last review: decide what it is and add its line.
"""
import argparse
import os
import re
import subprocess
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
HO_FILE = os.path.join(HERE, "highoctane.txt")

# (remote, branch, path filter or None). DEVELOPMENT.md, Syncing upstream, says why these.
FOLLOWED = [
    ("ctr-native", "master", None),
    ("dowlle", "main", None),
    ("dowlle", "dev/custom-pad-0.3.0", None),
    ("dowlle", "feat/signed-updater-301", None),
    ("dowlle", "feat/xdelta-track-import", None),
    ("apworld", "dev/custom-pad-0.3.0", "worlds/ctr"),
    ("apworld", "dev/custom-pad-0.3.0", ".github/fuzz"),
]
# Remotes whose other branches are scanned for work we don't follow yet.
SCANNED = {"dowlle": None, "apworld": "worlds/ctr"}
HO_REF, HO_BASE = "highoctane/vita", "ctr-native/master"
HO_STATUSES = ("ported", "have", "own", "maybe", "open", "skip")

FMT = "%H%x1f%at%x1f%ae%x1f%s"


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode:
        sys.exit(f"git {' '.join(args)} failed:\n{r.stderr}")
    return r.stdout


def commits(*revs, path=None):
    """[(hash, time, key, subject)] newest first, merges left out."""
    args = ["log", "--no-merges", f"--format={FMT}", *revs]
    if path:
        args += ["--", path]
    out = []
    for line in git(*args).splitlines():
        h, t, email, subj = line.split("\x1f", 3)
        out.append((h, int(t), (t, email, subj), subj))
    return out


def day(t):
    import time
    return time.strftime("%Y-%m-%d", time.localtime(t))


def show(c):
    return f"{day(c[1])} {c[0][:9]} {c[3]}"


def report_branch(label, revs, path, ours, limit):
    log = commits(*revs, path=path)
    new = [c for c in log if c[2] not in ours]
    last = next((c for c in log if c[2] in ours), None)
    print(f"\n{label}")
    print(f"  last integrated: {show(last) if last else 'nothing'}")
    print(f"  new since: {len(new)}")
    for c in new[:limit]:
        print(f"    {show(c)}")
    if len(new) > limit:
        print(f"    ... {len(new) - limit} more (--all)")


def read_ho_file():
    entries = {}
    for n, line in enumerate(open(HO_FILE, encoding="utf-8"), 1):
        line = line.split("|", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 2)
        if len(parts) < 2 or parts[1] not in HO_STATUSES:
            sys.exit(f"{HO_FILE}:{n}: expected '<commit> <status> [note]', status one of {', '.join(HO_STATUSES)}")
        entries[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else "")
    return entries


def report_ho(ours, base, ho_list, limit):
    log = commits(HO_REF, f"^{HO_BASE}")
    # Our commits carrying `git cherry-pick -x` lines, by the High Octane hash they name.
    picked = {}
    for line in git("log", "--format=%h%x1f%B%x1e", "--grep=cherry picked from commit", base).split("\x1e"):
        if "\x1f" in line:
            h, body = line.strip().split("\x1f", 1)
            for src in re.findall(r"cherry picked from commit ([0-9a-f]{7,40})", body):
                picked[src] = h
    by_key = {key: h for h, _, key, _ in commits(base)}
    listed = {}
    for k, v in read_ho_file().items():
        full = git("rev-parse", "--verify", "--quiet", k + "^{commit}").strip() if k else ""
        listed[full or k] = v

    rows = []
    for c in log:
        h = c[0]
        if h in listed:
            rows.append((c, *listed[h]))
        elif h in picked or c[2] in by_key:
            rows.append((c, "ported", picked.get(h) or by_key[c[2]][:9]))
        else:
            rows.append((c, "new", ""))
    counts = Counter(r[1] for r in rows)
    ported = [r for r in rows if r[1] == "ported"]
    reviewed = [r for r in rows if r[1] != "new"]

    print(f"\nHigh Octane ({HO_REF}, {len(rows)} commits not in {HO_BASE})")
    print("  " + ", ".join(f"{s} {counts[s]}" for s in (*HO_STATUSES, "new") if counts[s]))
    print(f"  last ported: {show(ported[0][0]) if ported else 'nothing'}")
    print(f"  reviewed up to: {show(reviewed[0][0]) if reviewed else 'nothing'}")
    new = [r for r in rows if r[1] == "new"]
    print(f"  new since the last review: {len(new)}" + (" (add each to tools/decrypted/highoctane.txt)" if new else ""))
    for r in new[:limit]:
        print(f"    {show(r[0])}")
    if len(new) > limit:
        print(f"    ... {len(new) - limit} more (--all)")

    for status in ho_list:
        sel = [r for r in reversed(rows) if r[1] == status]
        print(f"\n  {status} ({len(sel)}):")
        for c, _, note in sel:
            print(f"    {show(c)}" + (f"  [{note}]" if note else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--no-fetch", action="store_true", help="use the remote refs as they are")
    ap.add_argument("--base", default="HEAD", help="our history to compare with (default HEAD)")
    ap.add_argument("--ho", default="ported", help="High Octane statuses to list, comma-separated, or 'none' (default ported)")
    ap.add_argument("--all", action="store_true", help="list every new commit")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    limit = 10**9 if args.all else 10
    ho_list = [] if args.ho == "none" else [s for s in args.ho.split(",") if s]
    for s in ho_list:
        if s not in (*HO_STATUSES, "new"):
            sys.exit(f"--ho: unknown status {s}")

    if not args.no_fetch:
        for remote in sorted({r for r, _, _ in FOLLOWED} | set(SCANNED) | {HO_REF.split("/")[0]}):
            print(f"fetching {remote}...", file=sys.stderr)
            subprocess.run(["git", "fetch", "--prune", "--no-tags", "--quiet", remote], check=True)

    ours = {c[2] for c in commits(args.base)}
    print(f"Compared with {args.base} ({git('rev-parse', '--short', args.base).strip()})")

    for remote, branch, path in FOLLOWED:
        report_branch(f"{remote}/{branch}" + (f" ({path})" if path else ""), [f"{remote}/{branch}"], path, ours, limit)

    followed = {f"{r}/{b}" for r, b, _ in FOLLOWED}
    for remote, path in SCANNED.items():
        others = []
        for ref in git("for-each-ref", "--format=%(refname:short)", f"refs/remotes/{remote}/").split():
            if ref in followed or ref.endswith("/HEAD") or ref == remote:
                continue
            excl = [f"^{f}" for f in followed if f.startswith(remote + "/")]
            new = [c for c in commits(ref, *excl, path=path) if c[2] not in ours]
            if new:
                others.append((new[0][1], ref, len(new)))
        if others:
            print(f"\nOther {remote} branches with work we don't have" + (f" ({path})" if path else "") + ":")
            for t, ref, n in sorted(others, reverse=True):
                print(f"  {day(t)} {ref}: {n}")

    report_ho(ours, args.base, ho_list, limit)


if __name__ == "__main__":
    main()
