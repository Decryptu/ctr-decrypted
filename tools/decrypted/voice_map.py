"""Write platform/native_voice_map.inc: NTSC-U voice clip ids -> PAL and Japanese ids.

Usage: python tools/decrypted/voice_map.py <NTSC-U bin> <PAL bin> <JP bin>

The game asks for voice clips by their NTSC-U number. The PAL and Japanese
discs number them differently, so the game looks each one up in these tables
(platform/native_voices.c). Character voices come from each executable's own
voice set table (16 characters, sets 0-7; NTSC-U's sets 8-10 do not exist on
the other discs). Aku Aku / Uka Uka hints and the other "extra" clips come from
each executable's wrong-way clip ids and N. Tropy line table. The script checks
every PAL English pair has the same length (to one sector; Penta's NTSC-U clips are placeholders) as the NTSC-U clip, so a wrong
shift fails here instead of playing the wrong line.
"""

import argparse
from contextlib import closing
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "racers"))
from ctrdisc import Disc  # noqa: E402

NONE = 0xFFFF

# Both regional manifests list these two clips directly before Ripper Roo's
# voice set clips. The eight boss lines are referenced by PLAY_XA in overlay
# 233 on all three discs. NTSC-U 306 and 307 are absent from those scripts
# and the racer voice sets, so leave them unmapped.
REGIONAL_GAME_CLIPS = (
    (190, 160), (191, 161),
    (304, 256), (305, 257),  # Ripper Roo
    (308, 258), (309, 259),  # Papu Papu
    (310, 260), (311, 261),  # Komodo Joe
    (312, 262), (313, 263),  # Pinstripe
)


def tracks(disc, manifest):
    m = disc.file(manifest)
    nx = struct.unpack_from("<I", m, 12)[0]
    num = struct.unpack_from("<3I", m, 0x2C)
    first = struct.unpack_from("<3I", m, 0x38)
    e = 0x44 + 4 * nx
    return [[struct.unpack_from("<BBh", m, e + 4 * (first[c] + i)) for i in range(num[c])] for c in range(3)]


def voice_sets(exe):
    """Each character's voice sets, found through the 16-pointer voiceSetPtr array."""
    base = struct.unpack_from("<I", exe, 0x18)[0] - 0x800
    for o in range(0x60000, len(exe) - 64, 4):
        p = struct.unpack_from("<16I", exe, o)
        d = p[1] - p[0]
        if 0x60 <= d <= 0x100 and all(p[i + 1] - p[i] == d for i in range(15)) and base < p[0] < base + len(exe):
            break
    else:
        raise ValueError("voice set table not found")
    rows = []
    for c in range(16):
        sets = []
        for s in range(8):
            ptr, n = struct.unpack_from("<Ii", exe, p[c] - base + 8 * s)
            sets.append(list(struct.unpack_from("<%dh" % n, exe, ptr - base)))
        rows.append(sets)
    return rows


def wrong_way_ids(exe):
    """The two EXTRA clips Voiceline_Update plays (li a0,1 next to li a1,<id>)."""
    w = struct.unpack_from("<%dI" % (len(exe) // 4), exe)
    ids = []
    for i in range(3, len(w) - 4):
        if w[i] == 0x24040001:
            for j in range(i - 3, i + 4):
                if w[j] >> 16 == 0x2405 and 20 < (w[j] & 0xFFFF) < 70 and (w[j] & 0xFFFF) not in ids:
                    ids.append(w[j] & 0xFFFF)
    return ids[:2]


def ntropy_lines(exe):
    for o in range(0x60000, len(exe) - 12, 2):
        v = struct.unpack_from("<6h", exe, o)
        if all(0x38 <= x <= 0x50 for x in v) and len(set(v)) == 6 and sorted(v) != list(range(min(v), min(v) + 6)):
            return v
    raise ValueError("N. Tropy line table not found")


def load(path, exe_name, manifest):
    with closing(Disc(path)) as d:
        if exe_name not in d.files:
            raise ValueError(f"{path}: not the expected disc ({exe_name} missing)")
        exe = d.file(exe_name)
        return tracks(d, manifest), voice_sets(exe), wrong_way_ids(exe), ntropy_lines(exe)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("us")
    ap.add_argument("pal")
    ap.add_argument("jp")
    ap.add_argument("-o", default=str(Path(__file__).resolve().parents[2] / "platform" / "native_voice_map.inc"))
    a = ap.parse_args()

    uT, uS, uW, uN = load(a.us, "SCUS_944.26", "XA/ENG.XNF")
    pT, pS, pW, pN = load(a.pal, "SCES_021.05", "XA/ENG.XNF")
    jT, jS, jW, jN = load(a.jp, "SCPS_101.18", "XA/JPN.XNF")
    assert uW == [30, 61] and pW == [30, 61] and jW == [31, 63], (uW, pW, jW)
    assert [x - 2 for x in uN] == list(pN) and uN == jN, (uN, pN, jN)

    nU = [len(uT[1]), len(uT[2])]
    maps = {}
    for tag, S, T in (("pal", pS, pT), ("jp", jS, jT)):
        extra = [NONE] * nU[0]
        game = [NONE] * nU[1]
        for c in range(16):
            for s in range(8):
                for i, x in enumerate(uS[c][s]):
                    game[x] = S[c][s][i]
        for k in range(nU[0]):
            if tag == "pal":
                extra[k] = k if k <= 61 else k - 2 if 64 <= k <= 82 else NONE
            else:
                extra[k] = k if k <= 29 else 31 if k == 30 else k + 1 if k <= 60 else 63 if k == 61 else k if 64 <= k <= 82 else NONE
        for us_id, regional_id in REGIONAL_GAME_CLIPS:
            assert game[us_id] == NONE and regional_id not in game, (tag, us_id, regional_id)
            game[us_id] = regional_id
        if tag == "pal":
            for cat, table in ((1, extra), (2, game)):
                for us_id, pal_id in enumerate(table):
                    if pal_id != NONE:
                        # Penta's NTSC-U clips are placeholders; two others were re-encoded one sector shorter.
                        if not (cat == 2 and us_id in uS[13][0] + uS[13][1]):
                            assert abs(uT[cat][us_id][2] - T[cat][pal_id][2]) <= 1, (cat, us_id, pal_id)
        for cat, table in ((1, extra), (2, game)):
            for x in table:
                assert x == NONE or x < len(T[cat]), (tag, cat, x)
        maps[tag] = (extra, game)

    def arr(name, values):
        rows = [", ".join("0x%04X" % v for v in values[i:i + 12]) for i in range(0, len(values), 12)]
        return "static const u16 %s[%d] = {\n\t%s,\n};\n" % (name, len(values), ",\n\t".join(rows))

    out = ["// Generated by tools/decrypted/voice_map.py. Do not edit.",
           "// NTSC-U voice clip id -> PAL / Japanese id; 0xFFFF = no counterpart (the NTSC-U clip plays).",
           "#define NATIVE_VOICE_MAP_EXTRA_COUNT %d" % nU[0],
           "#define NATIVE_VOICE_MAP_GAME_COUNT %d" % nU[1], ""]
    for tag in ("pal", "jp"):
        out.append(arr("s_voiceMap_%s_extra" % tag, maps[tag][0]))
        out.append(arr("s_voiceMap_%s_game" % tag, maps[tag][1]))
    Path(a.o).write_text("\n".join(out), newline="\n")
    for tag in ("pal", "jp"):
        e, g = maps[tag]
        print(tag, "extra", sum(x != NONE for x in e), "/", len(e), "game", sum(x != NONE for x in g), "/", len(g))


if __name__ == "__main__":
    main()
