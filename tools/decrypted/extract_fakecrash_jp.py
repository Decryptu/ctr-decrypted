"""Install the Japanese release's Fake Crash model and textures from a raw Japanese CTR BIN.

  python tools/decrypted/extract_fakecrash_jp.py "<NTSC-U disc.bin>" "<JP disc.bin>" "<game folder>/assets"

Writes assets/FAKECRASH-JP/<BIGFILE index>.BIN: the three standalone models
taken whole from the Japanese disc, the five driver packs and the shared VRM
the main-menu level and the shared VRM rebuilt from the NTSC-U entries
with only Fake Crash's data replaced
(docs/CUSTOM_RACERS.md#regional-fake-crash-assets).
"""

import argparse
from contextlib import closing
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "racers"))
from ctrdisc import BigFile, Disc, RAW_SECTOR  # noqa: E402
from ctrmodel import Layout, Model, parse_vram  # noqa: E402

STANDALONE = (256, 322, 346)  # Fake Crash's high, medium and low models.
PACKS = (259, 274, 290, 306, 362)  # Driver packs holding a "fake" model.
MAIN_MENU_LEVEL = 216  # The main menu's level, whose "fake" model the character select draws.
SHARED_VRM = 258
VRAM_W = 1024
EXPECTED_VRAM_WORDS = 192


def split(raw):
    """A pointer-mapped file as (body, map offsets)."""
    size = struct.unpack_from("<I", raw, 0)[0]
    count = struct.unpack_from("<I", raw, 4 + size)[0] // 4
    if 8 + size + 4 * count != len(raw):
        raise ValueError("Unexpected pointer map size")
    return raw[4:4 + size], list(struct.unpack_from(f"<{count}I", raw, 8 + size))


def join(body, offsets):
    return struct.pack("<I", len(body)) + body + struct.pack("<I", 4 * len(offsets)) + struct.pack(
        f"<{len(offsets)}I", *offsets)


def model_list(body, cursor):
    """The model pointers of a null-terminated pointer array at cursor."""
    models = []
    while True:
        pointer = struct.unpack_from("<I", body, cursor)[0]
        if not pointer:
            return models
        models.append(pointer)
        cursor += 4


def fake_region(body, models, tail):
    """[start, end) of the "fake" model: up to the next model or the file's tail data."""
    named = [p for p in models if body[p:p + 16].split(b"\0")[0] == b"fake"]
    if len(named) != 1:
        raise ValueError("Expected one Fake Crash model")
    start = named[0]
    return start, min(p for p in models + [tail, len(body)] if p > start)


def pack_region(body):
    """A driver pack: its model array starts at +4, its shared data at the first word."""
    return fake_region(body, model_list(body, 4), struct.unpack_from("<I", body, 0)[0])


def menu_region(body, offsets):
    """The main-menu level: the model array that points at "fake"."""
    for o in offsets:
        target = struct.unpack_from("<I", body, o)[0]
        if body[target:target + 16].split(b"\0")[0] == b"fake" and target < len(body):
            first = o
            while first - 4 in offsets and struct.unpack_from("<I", body, first - 4)[0]:
                first -= 4
            return fake_region(body, model_list(body, first), len(body))
    raise ValueError("Main menu level has no Fake Crash model")


def texture_slots(body, start):
    """{texture index: body offset of its layout} for the model at start."""
    slots = {}
    count = struct.unpack_from("<h", body, start + 0x12)[0]
    headers = struct.unpack_from("<I", body, start + 0x14)[0]
    for h in range(count):
        commands = struct.unpack_from("<I", body, headers + h * 0x40 + 0x20)[0]
        table = struct.unpack_from("<I", body, headers + h * 0x40 + 0x28)[0]
        if not commands or not table:
            continue
        cursor = commands + 4
        while True:
            command = struct.unpack_from("<I", body, cursor)[0]
            cursor += 4
            if command == 0xFFFFFFFF:
                break
            index = command & 0x1FF
            if command & 0xFFFF0000 and index:
                slots[index] = struct.unpack_from("<I", body, table + (index - 1) * 4)[0]
    return slots


def shape(layout):
    """A layout's colour depth and the corners' offsets from its top-left texel."""
    return (layout.bpp, tuple(u - min(layout.u) for u in layout.u), tuple(v - min(layout.v) for v in layout.v))


def retarget_textures(raw, region, source_raw):
    """The menu model's texture layouts replaced, slot by slot, by those of the standalone
    model. The Japanese menu keeps Fake Crash's texels in its own VRM, where the NTSC-U
    menu has other textures; the standalone model samples the shared VRM, which the menu
    loads too."""
    body, offsets = split(raw)
    start, _ = region(body, set(offsets))
    source = split(source_raw)[0]
    own, theirs = texture_slots(body, start), texture_slots(source, 0)
    if own.keys() != theirs.keys():
        raise ValueError("The menu and race models use different texture slots")
    out = bytearray(body)
    for index, at in own.items():
        mine, other = Layout(body[at:at + 12], at), Layout(source[theirs[index]:theirs[index] + 12], 0)
        if shape(mine) != shape(other):
            raise ValueError(f"Texture slot {index} has a different shape in the race model")
        out[at:at + 12] = source[theirs[index]:theirs[index] + 12]
    return join(bytes(out), offsets)


def splice(us_raw, jp_raw, region):
    """The NTSC-U file with its Fake Crash model replaced by the Japanese one, moved to the
    NTSC-U model's place. The model must be self-contained: no pointer leaves it and none
    but the model array's enters it."""
    ub, um = split(us_raw)
    jb, jm = split(jp_raw)
    us, ue = region(ub, set(um)) if region is menu_region else region(ub)
    js, je = region(jb, set(jm)) if region is menu_region else region(jb)
    move = us - js
    delta = (je - js) - (ue - us)

    def inside(value, lo, hi):
        return lo <= value < hi

    body = bytearray(ub[:us] + jb[js:je] + ub[ue:])
    offsets = []
    for o in jm:
        if inside(o, js, je):
            value = struct.unpack_from("<I", jb, o)[0]
            if not inside(value, js, je):
                raise ValueError("The Japanese model points outside itself")
            struct.pack_into("<I", body, o + move, value + move)
            offsets.append(o + move)
    model_words = len(offsets)
    for o in um:
        if inside(o, us, ue):
            model_words -= 1
            continue
        value = struct.unpack_from("<I", ub, o)[0]
        if us < value < ue:
            raise ValueError("Another model points inside Fake Crash")
        new = o + delta if o >= ue else o
        struct.pack_into("<I", body, new, value + delta if value >= ue else value)
        offsets.append(new)
    if model_words:
        raise ValueError("The two Fake Crash models have different pointer counts")
    offsets.sort()
    return join(bytes(body), offsets), standalone(bytes(body), offsets, us, us + (je - js))


def standalone(body, offsets, start, end):
    """The spliced model as a model file of its own, to read its texture layouts."""
    out = bytearray(body[start:end])
    inner = [o - start for o in offsets if start <= o < end]
    for o in inner:
        struct.pack_into("<I", out, o, struct.unpack_from("<I", out, o)[0] - start)
    return Model(join(bytes(out), inner))


def patch_vram(us_raw, jp_raw, words):
    """The NTSC-U shared VRM with the given VRAM words taken from the Japanese one."""
    jp_words, _ = parse_vram(jp_raw)
    out = bytearray(us_raw)
    changed = 0

    def walk(off):
        nonlocal changed
        x, y, w, h = struct.unpack_from("<hhhh", out, off + 0x0C)
        pixels = off + 0x14
        for r in range(h):
            for c in range(w):
                word = (y + r) * VRAM_W + x + c
                if word in words:
                    at = pixels + 2 * (r * w + c)
                    value = jp_words[word]
                    if struct.unpack_from("<H", out, at)[0] != value:
                        struct.pack_into("<H", out, at, value)
                        changed += 1

    if struct.unpack_from("<I", out, 0)[0] != 0x20:
        walk(0)
    else:
        cursor = 4
        while cursor + 4 <= len(out):
            size = struct.unpack_from("<I", out, cursor)[0]
            cursor += 4
            if size == 0:
                break
            walk(cursor)
            cursor += size & ~3
    return bytes(out), changed


def extract(us_path, jp_path, assets):
    with closing(Disc(us_path)) as us_disc, closing(Disc(jp_path)) as jp_disc:
        if "SCUS_944.26" not in us_disc.files or us_path.stat().st_size % RAW_SECTOR:
            raise ValueError("Expected a raw MODE2/2352 NTSC-U CTR disc (SCUS-94426)")
        if "SCPS_101.18" not in jp_disc.files or jp_path.stat().st_size % RAW_SECTOR:
            raise ValueError("Expected a raw MODE2/2352 Japanese CTR disc (SCPS-10118)")
        us, jp = BigFile(us_disc), BigFile(jp_disc)

        files = {}
        words = set()
        for index in STANDALONE:
            raw = jp.read(index)
            model = Model(raw)
            if model.name != "fake":
                raise ValueError(f"BIGFILE entry {index} is not Fake Crash")
            if model.vram_words() != Model(us.read(index)).vram_words():
                raise ValueError(f"Fake Crash in entry {index} samples different VRAM on the two discs")
            words |= model.vram_words()
            files[index] = raw
        for index, region in [(i, pack_region) for i in PACKS] + [(MAIN_MENU_LEVEL, menu_region)]:
            files[index], model = splice(us.read(index), jp.read(index), region)
            if index == MAIN_MENU_LEVEL:
                files[index] = retarget_textures(files[index], region, files[STANDALONE[0]])
                body, offsets = split(files[index])
                start, end = region(body, set(offsets))
                model = standalone(body, offsets, start, end)
            if not model.vram_words() <= words:
                raise ValueError(f"Fake Crash in entry {index} samples VRAM outside the standalone models")
        files[SHARED_VRM], changed = patch_vram(us.read(SHARED_VRM), jp.read(SHARED_VRM), words)
        if changed != EXPECTED_VRAM_WORDS:
            raise ValueError(f"Expected {EXPECTED_VRAM_WORDS} changed VRAM words, found {changed}")

    destination = assets / "FAKECRASH-JP"
    destination.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=destination) as staging:
        staging = Path(staging)
        for index, raw in files.items():
            (staging / f"{index}.BIN").write_bytes(raw)
        for index in files:
            (staging / f"{index}.BIN").replace(destination / f"{index}.BIN")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("us_disc", type=Path, help="NTSC-U CTR .bin (SCUS-94426)")
    parser.add_argument("jp_disc", type=Path, help="Japanese CTR .bin (SCPS-10118)")
    parser.add_argument("assets", type=Path, help="the game's assets folder")
    args = parser.parse_args()
    try:
        destination = extract(args.us_disc, args.jp_disc, args.assets)
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f"error: {error}")
    print(f"Installed Japanese Fake Crash assets in {destination}")


if __name__ == "__main__":
    main()
