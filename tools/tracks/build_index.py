#!/usr/bin/env python3
"""Add tracks to the track catalogue and write its index.json and credits.

The catalogue (github.com/Decryptu/ctr-decrypted-tracks) keeps one folder per
track under tracks/, holding one installed package as it is, plus listing.json:
who made the track, where it comes from, and its Saphi track id once Saphi
lists it. Each folder is that track's newest version; git keeps the older ones.

  build_index.py <catalogue root> [<package folder> <authors> <source> ...]

Each package folder is an installed package (assets/tracks/packages/<pin>, or
the converter's --store). It replaces the track's folder, or starts a new one
with a listing.json naming <authors> (comma separated) and <source>, which keeps
what it already says when the track is updated.

This then checks every folder (each listed file's size and SHA-256, the pin, no
two tracks with one id) and writes:

- index.json: what the Mods screen downloads first, one entry per track with
  its folder, pin and LEV and VRM size and CRC32, so a client can fetch
  manifest.json, check it against the pin, then fetch the files it pins, and
  can tell the same track on Saphi; and its picture's size and CRC32 when the
  package has one, so the Mods screen can show it before the track is
  installed;
- CREDITS.md: every track with its authors and source.
"""
import hashlib
import json
import os
import re
import shutil
import sys
import zlib

import package

MODES = ("arcade", "time_trial")


def folder_name(manifest):
    slug = re.sub(r"[^a-z0-9]+", "-", manifest["title"].lower()).strip("-")
    return f"{slug}-{manifest['package_uuid'][:8]}"


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def add(root, source, authors, origin):
    manifest = load_json(os.path.join(source, "manifest.json"))
    tracks = os.path.join(root, "tracks")
    folder = os.path.join(tracks, folder_name(manifest))
    listing_path = os.path.join(folder, "listing.json")
    listing = load_json(listing_path) if os.path.isfile(listing_path) else {
        "authors": [a.strip() for a in authors.split(",") if a.strip()], "source": origin, "modes": list(MODES)}
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    shutil.copytree(source, folder)
    with open(listing_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(listing, f, indent=2, ensure_ascii=False)
        f.write("\n")


def entry(tracks, folder):
    path = os.path.join(tracks, folder)
    with open(os.path.join(path, "manifest.json"), "rb") as f:
        body = f.read()
    manifest = json.loads(body)
    listing = load_json(os.path.join(path, "listing.json"))
    if package.canonical(manifest) != body:
        sys.exit(f"{folder}: manifest.json is not canonical, so its hash is not its pin")
    if folder != folder_name(manifest):
        sys.exit(f"{folder}: should be {folder_name(manifest)}")
    if not listing.get("authors"):
        sys.exit(f"{folder}: no authors; every track is credited")
    result = {
        "id": manifest["package_uuid"],
        "title": manifest["title"],
        "author": ", ".join(listing["authors"]),
        "version": manifest["version"],
        "folder": folder,
        "pin": hashlib.sha256(body).hexdigest(),
        "manifest_bytes": len(body),
        "modes": listing.get("modes", list(MODES)),
    }
    if listing.get("saphi_track_id"):
        result["saphi_track_id"] = listing["saphi_track_id"]
    for file in manifest["files"]:
        with open(os.path.join(path, file["path"]), "rb") as f:
            data = f.read()
        if len(data) != file["bytes"] or hashlib.sha256(data).hexdigest() != file["sha256"]:
            sys.exit(f"{folder}: {file['path']} does not match manifest.json")
        if file["role"] in ("lev", "vrm", "picture"):
            result[file["role"]] = {"bytes": len(data), "crc32": zlib.crc32(data)}
    return result, listing


def main():
    root = sys.argv[1]
    additions = sys.argv[2:]
    if len(additions) % 3:
        sys.exit(__doc__)
    tracks = os.path.join(root, "tracks")
    os.makedirs(tracks, exist_ok=True)
    for i in range(0, len(additions), 3):
        add(root, *additions[i:i + 3])

    entries, listings = [], {}
    for folder in sorted(os.listdir(tracks)):
        if os.path.isfile(os.path.join(tracks, folder, "manifest.json")):
            e, listings[folder] = entry(tracks, folder)
            entries.append(e)
    entries.sort(key=lambda e: (e["title"].lower(), e["id"]))
    for a, b in zip(entries, entries[1:]):
        if a["id"] == b["id"]:
            sys.exit(f"{a['folder']} and {b['folder']}: one package id")

    with open(os.path.join(root, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema_version": 1, "tracks": entries}, f, indent=2, ensure_ascii=False)
        f.write("\n")

    lines = ["# Credits", "", "Every track here is the work of the people listed.", "",
             "| Track | Authors | Source |", "|---|---|---|"]
    for e in entries:
        listing = listings[e["folder"]]
        credits = f" ({listing['credits']})" if listing.get("credits") else ""
        lines.append(f"| {e['title']} | {e['author']}{credits} | {listing.get('source', '')} |")
    with open(os.path.join(root, "CREDITS.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(entries)} tracks")


if __name__ == "__main__":
    main()
