#!/usr/bin/env python3
"""Write the racer catalogue's index.json and credits from its racer folders.

The catalogue (github.com/Decryptu/ctr-decrypted-racers) keeps one folder per
racer under racers/, as convert_racer.py writes them. This checks every folder
(racer.json present, every listed file's size and SHA-256, no two racers with
one name) and writes:

- index.json: what the Mods screen downloads first, one entry per racer with
  its folder and the size and SHA-256 of racer.json, so a client can fetch
  racer.json, check it, then fetch and check the files it lists;
- CREDITS.md: every racer with its authors and source.

  build_index.py <catalogue root>
"""
import hashlib
import json
import os
import sys


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    root = sys.argv[1]
    racers_dir = os.path.join(root, "racers")
    entries = []
    for folder in sorted(os.listdir(racers_dir)):
        manifest_path = os.path.join(racers_dir, folder, "racer.json")
        if not os.path.isfile(manifest_path):
            continue
        with open(manifest_path, encoding="utf-8") as f:
            manifest = json.load(f)
        for role, info in manifest["files"].items():
            path = os.path.join(racers_dir, folder, info["path"])
            if os.path.getsize(path) != info["bytes"] or sha256(path) != info["sha256"]:
                sys.exit(f"{folder}: {role} does not match racer.json")
        if not manifest.get("authors"):
            sys.exit(f"{folder}: no authors; every racer is credited")
        entries.append({
            "id": manifest["id"],
            "name": manifest["name"],
            "authors": manifest["authors"],
            "template": manifest["template"],
            "folder": folder,
            "manifest": {"bytes": os.path.getsize(manifest_path), "sha256": sha256(manifest_path)},
        })
    entries.sort(key=lambda e: (e["name"], e["id"]))
    # One racer per character: two entries with one name would sit side by
    # side on the character select. Keep the better one and mark the other
    # {"skip": true, "reason": ...} in sources.json.
    for a, b in zip(entries, entries[1:]):
        if a["name"] == b["name"]:
            sys.exit(f"{a['folder']} and {b['folder']}: both are {a['name']}; skip one in sources.json")

    with open(os.path.join(root, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema_version": 1, "racers": entries}, f, indent=2, ensure_ascii=False)
        f.write("\n")

    lines = ["# Credits", "", "Every racer here is the work of the people listed. They were converted from the",
             "mods linked below with `tools/racers/convert_racer.py` (CTR Decrypted).", "",
             "| Racer | Authors | Source |", "|---|---|---|"]
    for e in entries:
        with open(os.path.join(racers_dir, e["folder"], "racer.json"), encoding="utf-8") as f:
            m = json.load(f)
        source = m["source"].get("url") or m["source"]["file"]
        credits = f" ({m['credits']})" if m.get("credits") else ""
        lines.append(f"| {m['name']} | {', '.join(m['authors'])}{credits} | {source} |")
    with open(os.path.join(root, "CREDITS.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(entries)} racers")


if __name__ == "__main__":
    main()
