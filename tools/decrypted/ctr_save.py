"""Read and edit the game save and a slot's Archipelago progress, so a test or a
bug repro can start from any point of the game.

The save is memcards/slot0/BASCUS-94426-SLOTS next to the exe: a 0x100-byte
card header, then struct MemcardProfile (include/namespace_Memcard.h): four
Adventure profiles and the game-wide progress (racers, tracks, cups, high
scores), padded to 0x1680 bytes and closed by a CRC-16.

  ctr_save.py show SAVE [--json]
  ctr_save.py new SAVE
  ctr_save.py edit SAVE [--profile N] [--name NAME] [--character C] [--hub H]
                        [--give WHAT ...] [--take WHAT ...]

WHAT is KIND[:WHICH]. WHICH is all (the default), a count (the first N, in the
order below) or a comma-separated list of names; for tracks a number is a
LevelID, not a count. Adventure profile:
  trophy, sapphire, gold, platinum, token   tracks (relics also take slide, turbo)
  key       roo, papu, joe, pinstripe
  gem       red, green, blue, yellow, purple
  purple    the hubs' purple tokens: beach, ruins, glacier, citadel
  door      beach_gemstone, gemstone_cups, beach_glacier, ruins_glacier, glacier_citadel
  oxide     first, second (the two endings)
  hint      every Aku Aku / Uka Uka hint, marked as already seen
  everything  all of the above except hints and the second ending
Game-wide:
  racer     tropy, penta, roo, papu, joe, pinstripe, fake_crash, oxide
  track     turbo_track, parking_lot, north_bowl, lab_basement
  cup       wumpa, crystal, nitro, crash (won on easy, medium and hard)
  scrapbook
  ghost     Time Trial tracks whose N. Tropy and N. Oxide ghosts are beaten

Tracks: dingo, dragon, blizzard, cove, tiger, papu, roo, hot_air, sewer,
mystery, cortex, ngin, polar, oxide_station, coco, tiny, slide, turbo (or their
LevelIDs). Hubs: beach, ruins, glacier, citadel, gemstone. Characters: crash,
cortex, tiny, coco, ngin, dingodile, polar, pura, pinstripe, papu, roo, joe,
tropy, penta, fake_crash, oxide (or 0-15).

Archipelago progress lives on the server, so these talk to it (127.0.0.1):
  ctr_save.py ap-show PORT SLOT          items received, locations checked
  ctr_save.py ap-give PORT SLOT ITEM [COUNT]   the server's item cheat
  ctr_save.py ap-check PORT SLOT LOCATION ...  check locations (names or
                                               wildcards, e.g. "*Trophy Race")
Standard library only.
"""
import argparse
import fnmatch
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
APWORLD_DATA = os.path.join(HERE, '..', '..', 'apworld', 'ctr', 'data')

HEADER = 0x100
DATA = 0x1680
PROFILE_VERSION = -18
PROFILE_SIZE = 0x1600
ADV_OFFSET = 0x4
ADV_SIZE = 0x50
GAME_OFFSET = 0x144          # struct GameProgress
HIGH_SCORE_TRACK = 0x124     # struct HighScoreTrack, timeTrialFlags at +0x120
DEFAULT_TIME = 0x8c640

TRACKS = ['dingo', 'dragon', 'blizzard', 'cove', 'tiger', 'papu', 'roo', 'hot_air', 'sewer', 'mystery',
          'cortex', 'ngin', 'polar', 'oxide_station', 'coco', 'tiny', 'slide', 'turbo']
CHARACTERS = ['crash', 'cortex', 'tiny', 'coco', 'ngin', 'dingodile', 'polar', 'pura', 'pinstripe', 'papu',
              'roo', 'joe', 'tropy', 'penta', 'fake_crash', 'oxide']
HUBS = {'gemstone': 25, 'beach': 26, 'ruins': 27, 'glacier': 28, 'citadel': 29}
HINTS = list(range(0, 8)) + list(range(0x0e, 0x13)) + list(range(0x16, 0x1e))

# Adventure reward bits (enum AdvRewardBitIndex): kind -> (names, first bit or bit list).
ADV = {
    'trophy': (TRACKS[:16], 0x06),
    'sapphire': (TRACKS, 0x16),
    'gold': (TRACKS, 0x28),
    'platinum': (TRACKS, 0x3a),
    'token': (TRACKS[:16], 0x4c),
    'key': (['roo', 'papu', 'joe', 'pinstripe'], 0x5e),
    'door': (['beach_gemstone', 'gemstone_cups', 'beach_glacier', 'ruins_glacier', 'glacier_citadel'], 0x64),
    'gem': (['red', 'green', 'blue', 'yellow', 'purple'], 0x6a),
    'purple': (['beach', 'ruins', 'glacier', 'citadel'], 0x6f),
    # Each ending sets its ending flag and its boss-clear flag.
    'oxide': (['first', 'second'], [(0x73, 0x62), (0x74, 0x63)]),
    'hint': ([f'{h:#x}' for h in HINTS], [0x76 + h for h in HINTS]),
}
EVERYTHING = ['trophy', 'sapphire', 'gold', 'platinum', 'token', 'key', 'door', 'gem', 'purple', 'oxide:first']

# GameProgress.unlocks bits (enum GameProgressUnlockBitIndex, UNLOCK_* masks, UnlockBitIndex).
GAME = {
    'racer': (['tropy', 'penta', 'roo', 'papu', 'joe', 'pinstripe', 'fake_crash', 'oxide'], [5, 6, 7, 8, 9, 10, 11, 37]),
    'track': (['turbo_track', 'parking_lot', 'north_bowl', 'lab_basement'], 1),
    # Arcade cups: bit 0x0c + 4 * difficulty + cup, and the same at 0x18 ("won before").
    'cup': (['wumpa', 'crystal', 'nitro', 'crash'], [[b + 4 * d + c for d in range(3) for b in (0x0c, 0x18)] for c in range(4)]),
    'scrapbook': (['scrapbook'], [36]),
}
TT_TROPY_OPEN, TT_TROPY_BEATEN, TT_OXIDE_BEATEN = 1, 2, 4


def crc16(data):
    # MEMCARD_CRC16 over the bytes, then two zero bytes (MEMCARD_ChecksumSave).
    crc = 0
    for byte in bytes(data) + b'\0\0':
        for i in range(7, -1, -1):
            crc = (crc << 1) | ((byte >> i) & 1)
            if crc & 0x10000:
                crc ^= 0x11021
    return crc & 0xffff


class Save:
    def __init__(self, raw):
        if len(raw) != HEADER + DATA:
            raise ValueError(f'{len(raw)} bytes, a SLOTS save is {HEADER + DATA}')
        self.header = bytearray(raw[:HEADER])
        self.data = bytearray(raw[HEADER:])
        version, size = struct.unpack_from('<hh', self.data, 0)
        if (version, size) != (PROFILE_VERSION, PROFILE_SIZE):
            raise ValueError(f'profile header {version}, {size:#x}: not a CTR SLOTS save of this version')
        self.crc_ok = crc16(self.data[:-2]) == struct.unpack_from('>H', self.data, DATA - 2)[0]

    @classmethod
    def new(cls):
        # GAMEPROG_InitFullMemcard, with empty high-score names (the game's own
        # defaults name a racer per entry; no code reads them back).
        header = bytearray(HEADER)
        header[0:4] = b'SC\x11\x01'
        data = bytearray(DATA)
        struct.pack_into('<hh', data, 0, PROFILE_VERSION, PROFILE_SIZE)
        for track in range(18):
            for entry in range(12):
                struct.pack_into('<I', data, GAME_OFFSET + 0xc + track * HIGH_SCORE_TRACK + entry * 0x18, DEFAULT_TIME)
        save = cls(bytes(header) + bytes(data))
        for p in range(4):
            save.clear_profile(p)
        return save

    def bytes(self):
        struct.pack_into('>H', self.data, DATA - 2, crc16(self.data[:-2]))
        self.crc_ok = True
        return bytes(self.header + self.data)

    # Adventure profiles
    def adv(self, p):
        return ADV_OFFSET + p * ADV_SIZE

    def clear_profile(self, p):
        o = self.adv(p)
        self.data[o:o + ADV_SIZE] = bytes(ADV_SIZE)
        struct.pack_into('<hh', self.data, o + 0x2a, -1, 0)
        struct.pack_into('<h', self.data, o + 0x2e, HUBS['beach'])

    def profile(self, p):
        o = self.adv(p)
        name = bytes(self.data[o + 0x18:o + 0x2a]).split(b'\0')[0].decode('latin-1')
        character, _, hub = struct.unpack_from('<hhh', self.data, o + 0x2a)
        return name, character, hub

    def set_profile(self, p, name=None, character=None, hub=None):
        o = self.adv(p)
        if name is not None:
            raw = name.encode('latin-1')[:17]
            self.data[o + 0x18:o + 0x2a] = raw + bytes(18 - len(raw))
        if character is not None:
            struct.pack_into('<h', self.data, o + 0x2a, character)
        if hub is not None:
            struct.pack_into('<h', self.data, o + 0x2e, hub)

    def adv_bit(self, p, bit, value=None):
        o = self.adv(p) + (bit >> 5) * 4
        word = struct.unpack_from('<I', self.data, o)[0]
        if value is None:
            return (word >> (bit & 31)) & 1
        word = (word | (1 << (bit & 31))) if value else (word & ~(1 << (bit & 31)))
        struct.pack_into('<I', self.data, o, word)

    # Game-wide progress
    def game_bit(self, bit, value=None):
        o = GAME_OFFSET + 4 + (bit >> 5) * 4
        word = struct.unpack_from('<I', self.data, o)[0]
        if value is None:
            return (word >> (bit & 31)) & 1
        word = (word | (1 << (bit & 31))) if value else (word & ~(1 << (bit & 31)))
        struct.pack_into('<I', self.data, o, word)

    def tt_flags(self, track, value=None):
        o = GAME_OFFSET + 0xc + track * HIGH_SCORE_TRACK + 0x120
        if value is None:
            return struct.unpack_from('<I', self.data, o)[0]
        struct.pack_into('<I', self.data, o, value)


def bits_of(kind_bits, i):
    if isinstance(kind_bits, int):
        return [kind_bits + i]
    b = kind_bits[i]
    return list(b) if isinstance(b, (list, tuple)) else [b]


def pick(names, which, aliases=None):
    """Indexes into NAMES for WHICH: all, a count, or names/numbers."""
    if which in (None, '', 'all'):
        return list(range(len(names)))
    if which.isdigit() and int(which) <= len(names) and not aliases:
        return list(range(int(which)))
    out = []
    for w in which.split(','):
        w = w.strip().lower()
        if aliases and w.isdigit():
            n = aliases.get(int(w))
            if n is None:
                raise SystemExit(f'no track with LevelID {w} here')
            out.append(n)
        elif w.isdigit() and int(w) <= len(names):
            out.extend(range(int(w)))
        elif w in names:
            out.append(names.index(w))
        else:
            raise SystemExit(f'unknown name {w!r}; one of: {", ".join(names)}')
    return out


def apply(save, profile, what, value):
    kind, _, which = what.partition(':')
    kind = kind.lower()
    if kind == 'everything':
        for w in EVERYTHING:
            apply(save, profile, w, value)
        return
    # Track lists take LevelIDs as numbers, not counts.
    tracky = kind in ('trophy', 'sapphire', 'gold', 'platinum', 'token', 'ghost')
    aliases = {i: i for i in range(18)} if tracky else None
    if kind in ADV:
        names, bits = ADV[kind]
        for i in pick(names, which, aliases):
            for bit in bits_of(bits, i):
                save.adv_bit(profile, bit, value)
    elif kind in GAME:
        names, bits = GAME[kind]
        for i in pick(names, which):
            for bit in bits_of(bits, i):
                save.game_bit(bit, value)
    elif kind == 'ghost':
        for t in pick(TRACKS, which, aliases):
            beaten = TT_TROPY_OPEN | TT_TROPY_BEATEN | TT_OXIDE_BEATEN
            save.tt_flags(t, (save.tt_flags(t) | beaten) if value else (save.tt_flags(t) & ~beaten))
    else:
        raise SystemExit(f'unknown kind {kind!r}')


def summary(save):
    out = {'checksum_ok': save.crc_ok, 'profiles': [], 'game': {}}
    for p in range(4):
        name, character, hub = save.profile(p)
        prof = {'slot': p, 'name': name, 'character': CHARACTERS[character] if 0 <= character < 16 else None,
                'hub': next((k for k, v in HUBS.items() if v == hub), hub)}
        if character >= 0:
            for kind, (names, bits) in ADV.items():
                prof[kind] = [n for i, n in enumerate(names) if all(save.adv_bit(p, b) for b in bits_of(bits, i))]
        out['profiles'].append(prof)
    for kind, (names, bits) in GAME.items():
        out['game'][kind] = [n for i, n in enumerate(names) if all(save.game_bit(b) for b in bits_of(bits, i))]
    out['game']['ghost'] = [TRACKS[t] for t in range(18) if save.tt_flags(t) & TT_OXIDE_BEATEN]
    return out


def show(save, as_json):
    s = summary(save)
    if as_json:
        print(json.dumps(s, indent=1))
        return
    if not s['checksum_ok']:
        print('checksum: BAD (the game rejects this save)')
    for prof in s['profiles']:
        if prof['character'] is None:
            print(f"profile {prof['slot']}: empty")
            continue
        print(f"profile {prof['slot']}: {prof['name']!r}, {prof['character']}, saved in {prof['hub']}")
        for kind, (names, _) in ADV.items():
            got = prof[kind]
            if kind == 'hint':
                print(f'  hints seen: {len(got)}/{len(names)}')
            elif got:
                print(f"  {kind}: {len(got)}/{len(names)}" + ('' if len(got) == len(names) else f" ({', '.join(got)})"))
    for kind, got in s['game'].items():
        names = GAME[kind][0] if kind in GAME else TRACKS
        print(f"game {kind}: {len(got)}/{len(names)}" + ('' if len(got) in (0, len(names)) else f" ({', '.join(got)})"))


def resolve(names, value, what, extra=None):
    v = value.lower()
    if extra and v in extra:
        return extra[v]
    if v.lstrip('-').isdigit():
        return int(v)
    if v in names:
        return names.index(v)
    raise SystemExit(f'unknown {what} {value!r}')


# Archipelago
def ap_names(kind):
    with open(os.path.join(APWORLD_DATA, f'{kind}.json'), encoding='utf-8') as f:
        return {e['code']: e['name'] for e in json.load(f)}


def ap_show(port, slot):
    from ap_backlog import connect, recv_packets
    s, got = connect(port, slot, items_handling=7, uuid='ctr-save')
    con = next(p for p in got if p.get('cmd') == 'Connected')
    if not any(p.get('cmd') == 'ReceivedItems' for p in got):
        got += recv_packets(s, lambda p: p.get('cmd') == 'ReceivedItems', timeout=5.0)
    s.close()
    items, locations = ap_names('items'), ap_names('locations')
    received = {}
    for p in got:
        if p.get('cmd') == 'ReceivedItems':
            for it in p['items']:
                n = items.get(it['item'], it['item'])
                received[n] = received.get(n, 0) + 1
    checked = con['checked_locations']
    print(f'{slot}: {sum(received.values())} items received, {len(checked)}/{len(checked) + len(con["missing_locations"])} locations checked')
    for n, c in sorted(received.items(), key=lambda kv: str(kv[0])):
        print(f'  {c} x {n}')
    for code in sorted(checked):
        print(f'  checked: {locations.get(code, code)}')


def ap_check(port, slot, patterns):
    from ap_backlog import connect, wait, ws_send
    names = ap_names('locations')
    codes = []
    for pat in patterns:
        hit = [c for c, n in names.items() if fnmatch.fnmatch(n.lower(), pat.lower())]
        if not hit:
            raise SystemExit(f'no location matches {pat!r} (names: apworld/ctr/data/locations.json)')
        codes += hit
    # A game client, not TextOnly: the server ignores location checks from those.
    s, got = connect(port, slot, game='Crash Team Racing', tags=(), uuid='ctr-save')
    con = next(p for p in got if p.get('cmd') == 'Connected')
    mine = set(con['missing_locations']) | set(con['checked_locations'])
    skipped = [c for c in codes if c not in mine]
    codes = [c for c in codes if c in mine]
    ws_send(s, json.dumps([{'cmd': 'LocationChecks', 'locations': codes}]))
    wait(s, 1.0)
    s.close()
    print(f'checked {len(codes)} locations' + (f', {len(skipped)} not in this seed' if skipped else ''))


def main():
    intro, _, rest = __doc__.partition('\n\n')
    ap = argparse.ArgumentParser(description=intro, formatter_class=argparse.RawDescriptionHelpFormatter, epilog=rest)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('show')
    p.add_argument('save')
    p.add_argument('--json', action='store_true')
    p = sub.add_parser('new')
    p.add_argument('save')
    p = sub.add_parser('edit')
    p.add_argument('save')
    p.add_argument('--profile', type=int, default=0, choices=range(4))
    p.add_argument('--name')
    p.add_argument('--character')
    p.add_argument('--hub')
    p.add_argument('--clear', action='store_true', help='empty the profile first')
    p.add_argument('--give', nargs='+', default=[])
    p.add_argument('--take', nargs='+', default=[])
    p = sub.add_parser('ap-show')
    p.add_argument('port', type=int)
    p.add_argument('slot')
    p = sub.add_parser('ap-give')
    p.add_argument('port', type=int)
    p.add_argument('slot')
    p.add_argument('item')
    p.add_argument('count', type=int, nargs='?', default=1)
    p = sub.add_parser('ap-check')
    p.add_argument('port', type=int)
    p.add_argument('slot')
    p.add_argument('locations', nargs='+')
    a = ap.parse_args()

    if a.cmd.startswith('ap-'):
        sys.path.insert(0, HERE)
        if a.cmd == 'ap-show':
            ap_show(a.port, a.slot)
        elif a.cmd == 'ap-check':
            ap_check(a.port, a.slot, a.locations)
        else:
            from ap_backlog import connect, getitem
            s, _ = connect(a.port, a.slot, uuid='ctr-save')
            print(f'{getitem(s, a.item, a.count)}/{a.count} x {a.item} given')
            s.close()
        return 0

    if a.cmd == 'new':
        save = Save.new()
    else:
        with open(a.save, 'rb') as f:
            save = Save(f.read())
    if a.cmd == 'show':
        show(save, a.json)
        return 0
    if a.cmd == 'edit':
        if a.clear:
            save.clear_profile(a.profile)
        name, character, _ = save.profile(a.profile)
        # Editing an empty profile makes one, as starting a new Adventure does.
        if character < 0 and a.character is None:
            a.character = 'crash'
        if not name and a.name is None:
            a.name = 'DEV'
        save.set_profile(a.profile, name=a.name,
                         character=None if a.character is None else resolve(CHARACTERS, a.character, 'character'),
                         hub=None if a.hub is None else resolve([], a.hub, 'hub', HUBS))
        for w in a.give:
            apply(save, a.profile, w, True)
        for w in a.take:
            apply(save, a.profile, w, False)
    os.makedirs(os.path.dirname(os.path.abspath(a.save)), exist_ok=True)
    with open(a.save, 'wb') as f:
        f.write(save.bytes())
    show(save, False)
    return 0


if __name__ == '__main__':
    sys.exit(main())
