"""Find our changes that an upstream merge lost.

Every line our side added since the merge base (git diff BASE OURS) should still exist somewhere
in the merged tree. Upstream moves functions between files, so a clean merge can silently drop an
edit we made inside a function that now lives in a file which never conflicted. This lists, per
file, each added line that appears nowhere in the merged tree (whitespace-insensitive).

Lines upstream renamed show up too; read each hit and either re-apply the change in upstream's
vocabulary or confirm upstream already covers it.

    python tools/decrypted/merge_audit.py <base> [ours=HEAD] [--tree <rev>]
    python tools/decrypted/merge_audit.py <base> [ours=HEAD] --moved

Without --tree it reads the working tree, so it runs in the middle of a merge.

--moved lists instead each of our diff hunks whose enclosing function (git's hunk header) is now
defined in a different file. Those are the edits a merge loses without a conflict; check that each
one was carried into the function's new home.
"""
import argparse
import os
import re
import subprocess
import sys

ROOTS = ("game", "include", "platform", "ap", "main.c")
EXTS = (".c", ".h", ".cpp", ".hpp")


def git(*args):
    return subprocess.run(("git",) + args, capture_output=True, check=True).stdout.decode("utf-8", "replace")


def norm(line):
    return re.sub(r"\s+", "", line)


def trivial(line):
    s = line.strip()
    if len(norm(s)) < 10:
        return True
    if s.startswith(("//", "/*", "*", "#include", "#endif", "#else")):
        return True
    return s in ("{", "}", "};", "break;", "return;", "else")


def tree_text(rev):
    chunks = []
    if rev:
        for path in git("ls-tree", "-r", "--name-only", rev, "--", *ROOTS).splitlines():
            if path.endswith(EXTS):
                chunks.append(git("show", f"{rev}:{path}"))
    else:
        for root in ROOTS:
            if os.path.isfile(root):
                chunks.append(open(root, encoding="utf-8", errors="replace").read())
                continue
            for dirpath, _, files in os.walk(root):
                for name in files:
                    if name.endswith(EXTS):
                        with open(os.path.join(dirpath, name), encoding="utf-8", errors="replace") as f:
                            chunks.append(f.read())
    return "\n".join(norm(l) for chunk in chunks for l in chunk.splitlines())


def source_files():
    for root in ROOTS:
        if os.path.isfile(root):
            yield root
            continue
        for dirpath, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d != "vendor"]
            for name in files:
                if name.endswith(EXTS):
                    yield os.path.join(dirpath, name).replace(os.sep, "/")


DEFINITION = re.compile(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*$", re.M)


def moved(base, ours):
    defined = {}
    for path in source_files():
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for m in DEFINITION.finditer(text):
            end = m.end()
            if text[end:end + 4].lstrip().startswith("{"):
                defined.setdefault(m.group(1), set()).add(path)

    diff = git("diff", "-U0", base, ours, "--", *ROOTS)
    path = None
    hits = {}
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif path and line.startswith("@@"):
            header = line.split("@@", 2)[-1]
            m = re.search(r"\b([A-Za-z_]\w*)\s*\(", header)
            if not m:
                continue
            homes = defined.get(m.group(1))
            if homes and path not in homes:
                hits.setdefault((path, m.group(1)), set()).update(homes)
    for (path, fn), homes in sorted(hits.items()):
        print(f"{path}: {fn} -> {', '.join(sorted(homes))}")
    print(f"{len(hits)} functions we changed now live in another file", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("ours", nargs="?", default="HEAD")
    ap.add_argument("--tree", help="merged revision to check (default: working tree)")
    ap.add_argument("--moved", action="store_true", help="list our changes in functions that moved")
    a = ap.parse_args()
    if a.moved:
        moved(a.base, a.ours)
        return

    merged = tree_text(a.tree)
    diff = git("diff", "-U0", a.base, a.ours, "--", *ROOTS)
    lost = {}
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif path and path.endswith(EXTS) and line.startswith("+") and not line.startswith("+++"):
            body = line[1:]
            if not trivial(body) and norm(body) not in merged:
                lost.setdefault(path, []).append(body.strip())

    total = 0
    for path in sorted(lost):
        print(f"{path}: {len(lost[path])}")
        for body in lost[path]:
            print(f"    {body}")
        total += len(lost[path])
    print(f"{total} added lines missing from the merged tree, in {len(lost)} files", file=sys.stderr)


if __name__ == "__main__":
    main()
