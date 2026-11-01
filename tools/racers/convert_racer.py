#!/usr/bin/env python3
"""Turn racer mods made for the PS1 disc into racer packages (docs/CUSTOM_RACERS.md).

Racer mods replace a retail racer on the disc: an xdelta patch rewrites the
racer's model, its portrait in the shared VRM and its name in the language
file. This applies each patch to a clean disc, finds every racer it replaces,
and writes one package per racer: racer.json, model.ctr and portrait.bin
(plus portrait.png for people). Nothing from the retail disc goes into a
package: a portrait the mod left unchanged is refused, and the model is the
mod's own mesh.

  convert_racer.py --disc "CTR (USA).bin" --out racers/ [--sources sources.json] mod.xdelta...

sources.json gives what the patch cannot: authors, credits, a source URL, and
names for racers whose mod kept the retail name. Keys are patch file names:

  {"ctr-u_CrunchKart.xdelta": {"authors": ["robthefryman", "Angel123"],
                               "url": "https://...",
                               "racers": {"crash": {"name": "CRUNCH BANDICOOT"}}}}

A racer can be skipped with {"skip": true} under its template slug.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctrdisc import BI_RACERMODELHI, BI_SHAREDMPKVRM, BigFile, Disc  # noqa: E402
from ctrmodel import Model, parse_vram  # noqa: E402

SLUGS = ("crash", "cortex", "tiny", "coco", "ngin", "dingodile", "polar", "pura", "pinstripe", "papu", "roo", "komodo",
         "tropy", "penta", "fakecrash", "oxide")
ENGINES = ("balanced", "balanced", "speed", "accel", "accel", "speed", "turn", "turn", "accel", "speed", "turn",
           "balanced", "speed", "turn", "balanced", "balanced")
# Language file string indices of each racer's long and short name (MetaDataCharacters).
LNG_LONG = (44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 58, 59, 57)
LNG_SHORT = (60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 74, 75, 73)
BI_LANGUAGE_ENGLISH = 235
# Each retail racer's portrait icon: page and palette words, and the 44x26
# texel rectangle (u, v) in its 4-bit page (read from the game's ptrIcons).
PORTRAITS = (
    (0x65, 0x3EC1, 192, 216), (0x64, 0x3F01, 0, 216), (0x64, 0x3F41, 44, 216), (0x64, 0x3F81, 88, 216),
    (0x64, 0x3FC1, 132, 216), (0x64, 0x3E02, 176, 216), (0x6E, 0x3E42, 96, 144), (0x6E, 0x3E82, 140, 144),
    (0x6E, 0x3F82, 88, 192), (0x6E, 0x3F02, 0, 192), (0x6E, 0x3EC2, 184, 144), (0x6E, 0x3F42, 44, 192),
    (0x6E, 0x3FC2, 132, 192), (0x6E, 0x3E03, 176, 192), (0x6E, 0x3E43, 0, 218), (0x6E, 0x3E83, 44, 218),
)
PORTRAIT_W, PORTRAIT_H = 44, 26
PORTRAIT_MAGIC = b"CDP1"
# A UUIDv5 namespace for racer ids: the id is derived from the model's bytes,
# so converting the same mod twice gives the same racer.
RACER_NAMESPACE = uuid.UUID("5d0c7a0e-8f0b-5b8e-9a51-2f0f6c1e7a10")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def lng_strings(raw):
    count, table = struct.unpack_from("<ii", raw, 0)
    out = []
    for i in range(count):
        p = struct.unpack_from("<I", raw, table + 4 * i)[0]
        out.append(raw[p:raw.index(b"\0", p)].decode("latin1"))
    return out


def portrait_words(vram, template):
    """The portrait's 4-bit texel words (11 per row) and its 16-colour palette."""
    tpage, clut, u, v = PORTRAITS[template]
    page_x = (tpage & 0xF) * 64
    page_y = ((tpage >> 4) & 1) * 256
    words = []
    for row in range(PORTRAIT_H):
        y = page_y + v + row
        for col in range(PORTRAIT_W // 4):
            words.append(vram.get(y * 1024 + page_x + u // 4 + col, 0))
    clut_x, clut_y = (clut & 0x3F) * 16, clut >> 6
    palette = [vram.get(clut_y * 1024 + clut_x + i, 0) for i in range(16)]
    return words, palette


def portrait_bin(words, palette):
    """CDP1: magic, u16 width, u16 height (texels), 16 palette words, then the
    4-bit texels, (width / 4) words per row. PS1 15-bit colours."""
    return PORTRAIT_MAGIC + struct.pack("<HH", PORTRAIT_W, PORTRAIT_H) + struct.pack("<16H", *palette) + \
        struct.pack(f"<{len(words)}H", *words)


def portrait_png(words, palette):
    def rgba(c):
        if c == 0:
            return b"\0\0\0\0"
        return bytes(((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31, 255))

    rows = []
    for row in range(PORTRAIT_H):
        line = bytearray(b"\0")
        for col in range(PORTRAIT_W):
            word = words[row * (PORTRAIT_W // 4) + col // 4]
            line += rgba(palette[(word >> ((col & 3) * 4)) & 15])
        rows.append(bytes(line))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", PORTRAIT_W, PORTRAIT_H, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")


def slugify(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "racer"


def apply_patch(tool, disc, patch, out):
    subprocess.run([tool, disc, patch, out], check=True)


def convert(args):
    sources = {}
    if args.sources:
        with open(args.sources, encoding="utf-8") as f:
            sources = json.load(f)

    clean = BigFile(Disc(args.disc))
    clean_models = [clean.read(BI_RACERMODELHI + t) for t in range(16)]
    clean_vram, _ = parse_vram(clean.read(BI_SHAREDMPKVRM))
    clean_names = lng_strings(clean.read(BI_LANGUAGE_ENGLISH))
    os.makedirs(args.out, exist_ok=True)

    seen = {}
    for existing in os.listdir(args.out):
        path = os.path.join(args.out, existing, "racer.json")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                seen[json.load(f)["files"]["model"]["sha256"]] = existing

    report = []
    tmp = tempfile.mkdtemp(prefix="racer-")
    try:
        for patch in args.patches:
            base = os.path.basename(patch)
            info = sources.get(base, {})
            with open(patch, "rb") as f:
                patch_sha = sha256(f.read())
            patched_path = os.path.join(tmp, "patched.bin")
            apply_patch(args.xdelta, args.disc, patch, patched_path)
            patched = BigFile(Disc(patched_path))
            vram, _ = parse_vram(patched.read(BI_SHAREDMPKVRM))
            names = lng_strings(patched.read(BI_LANGUAGE_ENGLISH))

            for t in range(16):
                model_raw = patched.read(BI_RACERMODELHI + t)
                if model_raw == clean_models[t]:
                    continue
                over = info.get("racers", {}).get(SLUGS[t], {})
                label = f"{base} [{SLUGS[t]}]"
                if over.get("skip"):
                    report.append(f"skipped {label}: {over.get('reason', 'marked skip')}")
                    continue
                Model(model_raw)  # refuse what does not parse

                model_sha = sha256(model_raw)
                if model_sha in seen:
                    report.append(f"same model as {seen[model_sha]}: {label}")
                    continue

                name = over.get("name") or names[LNG_LONG[t]]
                short = over.get("short_name") or names[LNG_SHORT[t]]
                if not over.get("name") and names[LNG_LONG[t]] == clean_names[LNG_LONG[t]]:
                    report.append(f"NEEDS A NAME (kept the retail one): {label}")
                    continue

                words, palette = portrait_words(vram, t)
                if (words, palette) == portrait_words(clean_vram, t):
                    report.append(f"NEEDS A PORTRAIT (kept the retail one): {label}")
                    continue

                racer_id = str(uuid.uuid5(RACER_NAMESPACE, model_sha))
                folder = f"{slugify(name)}-{racer_id[:8]}"
                out = os.path.join(args.out, folder)
                os.makedirs(out, exist_ok=True)
                files = {
                    "model": ("model.ctr", model_raw),
                    "portrait": ("portrait.bin", portrait_bin(words, palette)),
                }
                manifest_files = {}
                for role, (path, data) in files.items():
                    with open(os.path.join(out, path), "wb") as f:
                        f.write(data)
                    manifest_files[role] = {"path": path, "bytes": len(data), "sha256": sha256(data)}
                with open(os.path.join(out, "portrait.png"), "wb") as f:
                    f.write(portrait_png(words, palette))

                manifest = {
                    "schema_version": 1,
                    "id": racer_id,
                    "name": name,
                    "short_name": short,
                    "version": over.get("version", "1"),
                    "authors": over.get("authors", info.get("authors", [])),
                    "credits": over.get("credits", info.get("credits", "")),
                    "source": {"file": base, "sha256": patch_sha, "url": info.get("url", "")},
                    "template": SLUGS[t],
                    "engine": over.get("engine", ENGINES[t]),
                    "wheels": over.get("wheels", t != 15),
                    "files": manifest_files,
                }
                with open(os.path.join(out, "racer.json"), "w", encoding="utf-8", newline="\n") as f:
                    json.dump(manifest, f, indent=2, ensure_ascii=False)
                    f.write("\n")
                seen[model_sha] = folder
                report.append(f"wrote {folder}: {name} ({label})")
            patched.disc.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n".join(report))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--disc", required=True, help="clean CTR (USA) .bin")
    p.add_argument("--out", required=True, help="folder that receives one folder per racer")
    p.add_argument("--sources", help="sources.json with authors, URLs and name overrides")
    p.add_argument("--xdelta", default=os.path.join(here, "xdelta_apply.exe" if os.name == "nt" else "xdelta_apply"),
                   help="xdelta_apply built from xdelta_apply.c")
    p.add_argument("patches", nargs="+")
    convert(p.parse_args())


if __name__ == "__main__":
    main()
