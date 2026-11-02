"""Fail when the tree sends players or the program to dowlle's repository.

CTR Decrypted is built on dowlle/ctr-native-ap, and every merge from it can bring back his
identity: links to his releases and issue tracker in what we ship, URLs in the program, his
release key. Those fail silently (the updater checked his releases for weeks), so this check
runs with the harnesses (tools/ci/harnesses.txt) and fails CI on any of them.

    python tools/decrypted/check_upstream_refs.py

Credits, the docs about upstream, `NOTE(dowlle)` comments and references to his issue numbers
are fine and not matched. A reference that must stay goes in ALLOWED_FILES or EXCEPTIONS below,
with the reason.
"""
import re
import subprocess
import sys

# Docs about upstream itself, and this script.
ALLOWED_FILES = {
    "docs/UPSTREAM-ISSUES.md",
    "docs/DEVELOPMENT.md",
    "ROADMAP.md",
    "RELEASING.md",  # dowlle's release process, kept as he wrote it
    "tools/authoring-branch/AUTHORING-CLIENT.md",  # dowlle's authoring branch, which we don't build
    "tools/decrypted/upstream_status.py",
    "tools/decrypted/check_upstream_refs.py",
    "tools/test-update-core.cpp",  # pins dowlle's signed release to prove it is refused
}
# (file, text on the line) pairs that name something only dowlle's repository has.
EXCEPTIONS = [
    ("tools/extract-assets/RELEASE-NOTES-DRAFT.md", "releases/tag/v0.1.0"),  # his first release, the template's example
    ("apworld/ctr/README.md", "AI_USAGE.md"),  # his note about his own work
    ("docs/BOX_AUTHORING.md", "/issues/"),  # the issues the box tools were built for
    ("ap/ap_seedcfg.cpp", "ctr-native-ap#71"),  # a log line naming the upstream issue a seed field waits on
]

RULES = [
    # Where players download, report and read: his releases, issue tracker, docs and wiki.
    (r"github\.com/dowlle/ctr-native-ap/(releases|issues|blob|wiki)", None,
     "links to dowlle's releases, issues or docs; use github.com/Decryptu/ctr-decrypted"),
    # His repositories inside a string in the program or the packaging scripts, as a URL or a
    # bare path ("/repos/dowlle/...", the form the updater used).
    (r"\"[^\"\n]*(?i:dowlle)/[^\"\n]*\"", ("ap/", "platform/", "game/", "include/", "main.c",
                                           "tools/ci/", "tools/release/", ".sh", ".bat"),
     "the program or a packaging script uses dowlle's repository"),
    (r"RWQz1vK\+2CnxRlzUFiAM8FlyI706TxjELuc\+8d4N9uh06nS1zAKGuD6x|46F129D8BEF2D633", None,
     "dowlle's release key; ours is ctr-release-minisign.pub"),
    (r"ctr-native-ap-updater", None, "the updater names itself after dowlle's client"),
]


def main():
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                          check=True).stdout.strip()
    bad = []
    for pattern, scope, why in RULES:
        out = subprocess.run(["git", "grep", "-n", "-I", "-P", pattern], cwd=root, capture_output=True,
                             text=True, encoding="utf-8", errors="replace").stdout
        for line in out.splitlines():
            path, num, text = line.split(":", 2)
            if path in ALLOWED_FILES:
                continue
            if scope and not any(path.startswith(s) or path.endswith(s) for s in scope):
                continue
            if any(path == f and t in text for f, t in EXCEPTIONS):
                continue
            bad.append(f"{path}:{num}: {why}\n    {text.strip()[:160]}")
    if bad:
        print("\n".join(bad))
        print(f"\n{len(bad)} reference(s) to dowlle's repository (tools/decrypted/check_upstream_refs.py)")
        return 1
    print("no references to dowlle's repository in what we ship")
    return 0


if __name__ == "__main__":
    sys.exit(main())
