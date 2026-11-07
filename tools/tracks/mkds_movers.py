"""What moves in a Mario Kart DS course, as level instances (docs/TRACK-CONVERTER.md#movers).

An object in MOVERS is not baked into the course: its MKDS model becomes a CTR model the LEV carries, and
game/ConvertedTrack/Mover.c moves it and hits karts through the game's hazard code. `Mover` says how an
object moves; `build_model` writes the model; `records` writes the list the thread reads.
"""
import math, struct
from collections import namedtuple

import numpy as np
from PIL import Image

import ctr_items
import mkds_motion
import mkds_objects

MODEL_ID = -1  # no slot in the game's model table
HEADER_SIZE, LAYOUT_SIZE, MODEL_SIZE = 0x40, 12, 0x18
FRAME_SIZE = 0x1c
SCALE_ONE = 4096
TURN = 4096
LOD_ALWAYS = 0x7fff
BILLBOARD = 1  # ModelHeader.flags: turns round its height to face the camera, as the game's fruit does
CMD_FIRST, CMD_NEXT = 0x98, 0x18  # a new strip, culled, colours from the scratchpad; a further vertex
MAX_COLOURS = 128
UV_SLACK = 0.03  # of a texture: corners a hair past its edge are on it, not on the next repeat
MAX_MODEL_SCALE = 0x7fff
STACK_SLOT = 0x57  # a vertex's scratch slot, as retail models write it: lower ones overlap the colours
MAX_LAYOUTS = 511
MAX_TILES = 4  # of a repeating texture, each way, in a model's image
MAX_WIDTH, MAX_HEIGHT = 128, 256  # texels: wider, it would have to give up colours or be halved, and its layouts would miss
NEUTRAL = 128  # a texture is multiplied by a vertex colour over this

PATH, SPIN, SWING, HOP, ORBIT, PULSE, KEYS = range(7)
LOOP, BACK_AND_FORTH, RESTART = range(3)
FACE_PATH, ROLL, SOLID, CARRIES, ONE_WAY, DROPS = 0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000  # Mover.c's MOVER_FLAG_
TICKS = 60  # a second: the game's objects
SPIN_RATE = 4.8  # 4096ths of a turn a second per unit of a spinner's speed setting: (s << 7) / 100 of 65536 a tick
PATH_UNITS = 3.2  # level units a second per hundredth of a world unit a tick
SPIN_DAMAGE, BLAST, SQUISH = 1, 2, 3  # RB_Hazard_HurtDriver
ITEM_BOX = 4  # Mover.c: it breaks as a weapon crate does
KEY_HIDDEN = -32768
KEY_MS = 50  # three ticks
CRATE_MIDDLE = np.array([0.0, 48.0, 0.0])  # level units over its origin, which is its underside

# model; (kind, its arguments); damage; level units a second per unit of the speed setting; flags; how many
# share the object's path or centre; which setting is its speed; its speed in level units a second where the
# setting is 0. A model stands on the ground by its own lowest point, unless it is in FLYING.
BULLETS, BULLET_REACH, BULLET_UP = 3, 3600, 42  # a cannon's shots in the air at once; how far one flies, and how high over the cannon it starts
Mover = namedtuple('Mover', 'model kind damage factor flags count setting fixed', defaults=(0, 0.0, 0, 1, 0, 600))
MOVERS = {
    0x195: Mover('bus_a', (PATH, LOOP), BLAST, 3 * PATH_UNITS, FACE_PATH),
    0x19a: Mover('car_a', (PATH, LOOP), BLAST, 3 * PATH_UNITS, FACE_PATH),
    0x19c: Mover('truck_a', (PATH, LOOP), BLAST, 3 * PATH_UNITS, FACE_PATH),
    0x192: Mover('rock', (KEYS, 'rock'), BLAST, flags=ROLL),
    0x1b1: Mover('rock2', (KEYS, 'rock'), BLAST, flags=ROLL),
    0x1b0: Mover('IronBall', (KEYS, 'ball'), BLAST),
    0x191: Mover('kuribo', (PATH, BACK_AND_FORTH), SPIN_DAMAGE, 0.25 * PATH_UNITS),  # s0 / 200 a tick, half its time
    0x1ac: Mover('crab', (PATH, BACK_AND_FORTH), SPIN_DAMAGE, 0, fixed=50 * PATH_UNITS),
    0x1a5: Mover('wanwan', (KEYS, 'chain_path'), BLAST, flags=FACE_PATH),
    0x158: Mover('airship', (KEYS, 'airship'), 0, flags=FACE_PATH),
    0x1ad: Mover('sun', (KEYS, 'sun'), SPIN_DAMAGE),
    0x19e: Mover('basabasa', (KEYS, 'coffin'), SPIN_DAMAGE),
    0xc9: Mover(None, (KEYS, 'moving_box'), ITEM_BOX),
    0x1a1: Mover('NsKiller1', (PATH, LOOP), 0, 0, fixed=200 * PATH_UNITS),
    0x1a4: Mover('mkd_ef_burner', (PULSE,), SPIN_DAMAGE),
    0xcb: Mover('gear_black', (SPIN, 1, 0), 0, flags=CARRIES),
    0xce: Mover('test_cylinder', (SPIN, 1, 0), 0),
    0xd0: Mover('rotary_room', (SPIN, 1, 0), 0, flags=CARRIES),
    0xd1: Mover('rotary_bridge', (SPIN, 1, 0), 0),
    0xcd: Mover('second_hand', (SPIN, 1, 0), SPIN_DAMAGE, flags=SOLID),
    0xcf: Mover('pendulum', (SWING, 2, 0, 0), SPIN_DAMAGE, flags=SOLID),
    0xd2: Mover('dram', (SPIN, 0, 0), 0, flags=ONE_WAY),
    0x1a9: Mover('flipper', (SWING, 1, 0, 0), SPIN_DAMAGE, flags=SOLID),
    0x1a3: Mover('move_tree', (PATH, LOOP), 0, 2, flags=SOLID),
    0x154: Mover('RainStar', (SPIN, 1, 500)),
    0x193: Mover('dossun', (KEYS, 'thwomp'), SQUISH, flags=SOLID | DROPS),
    0x19b: Mover('pukupuku', (KEYS, 'cheep_cheep'), SPIN_DAMAGE),
    0x13c: Mover('bakubaku', (KEYS, 'bakubaku'), BLAST),
    0x1a7: Mover('poo', (KEYS, 'rocky_wrench'), SPIN_DAMAGE),
    0x199: Mover('choropu', (KEYS, 'monty_mole'), SPIN_DAMAGE),
    0x13d: Mover('teresa', (HOP, 3000)),
    0x196: Mover('wanwan', (KEYS, 'chain_staked'), BLAST),
    0x1a0: Mover('basabasa', (KEYS, 'bats'), SPIN_DAMAGE),
    0x1af: Mover('fireball2', (ORBIT,), SPIN_DAMAGE),
}
BOUND_ON_PATH = Mover('bound', (PATH, LOOP), SPIN_DAMAGE, 0, SOLID, fixed=300 * PATH_UNITS)
BULLET = Mover('NsKiller2', (PATH, RESTART), BLAST, 0, FACE_PATH, count=BULLETS, fixed=600 * PATH_UNITS)
BURNER_ON_PATH = Mover('mkd_ef_burner', (PATH, BACK_AND_FORTH), SPIN_DAMAGE, PATH_UNITS, setting=3)  # sweeps along its path, always lit
TYRED = {'car_a', 'bus_a', 'truck_a'}
SOLID_STILL = {'dram', 'test_cylinder', 'kanoke_64'}  # stays where it is and keeps its walls
PENDULUM_BOB = (100, 100, 50), (0, -500, 0)  # half its hit box and where it is: only the weight hits
# Hangs or flies where it is placed, by its own origin
FLYING = {'FireBall', 'kamome', 'airship', 'sun', 'NsKiller2', 'mkd_ef_burner', 'teresa', 'basabasa', 'bakubaku', 'pukupuku', 'fireball2', 'kanoke_64', 'second_hand', 'pendulum', 'test_cylinder', 'gear_black', 'gear_white', 'rotary_room', 'rotary_bridge', 'flipper', 'dram', 'RainStar'}
HIT_SHARE = {'fireball2': 0.5}  # of its box: a ball is round
ROUND_SHARE = 0.7  # the box of what is round or a picture
HOP_HEIGHT = {'teresa': 24}  # MKDS units up
FLIPPER_REST, FLIPPER_STROKE, FLIPPER_WAIT = 512, 660, 110  # 45 degrees; 58 degrees out in 29 ticks and back; ticks between, on average
FLAT_SHARE = 0.11  # a model this thin for its size is a picture: MKDS turns it to the camera


def is_flat(points):
    """Whether a model's points lie in its z = constant plane, near enough: a picture, not a body."""
    extent = np.ptp(np.array(points), axis=0)
    return bool(extent[2] < FLAT_SHARE * max(extent[0], extent[1]))


def image_of(material):
    """A material's picture as RGBA, every texel solid or clear; a plain one for a colour alone."""
    if material.tex:
        out = np.array(material.tex0.decode(material.tex, material.pal), np.uint8)
    else:
        out = np.full((4, 4, 4), 255, np.uint8)
    out[..., 3] = np.where(out[..., 3] > 127, 255, 0)
    return out


def fold(values, repeats, flips):
    """A triangle's texture coordinates along one axis, in textures, moved to the first tiles: a repeat of
    a texture stays in the model's image, and a mirrored one stays mirrored."""
    if not repeats:
        return np.clip(values, 0.0, 1.0)
    low = math.floor(values.min() + UV_SLACK)
    return values - (low - low % 2 if flips else low)


def tiled(image, tiles, flips):
    """A texture repeated (tiles along, tiles down) times, every second one turned over where it flips."""
    rows = []
    for j in range(tiles[1]):
        row = []
        for i in range(tiles[0]):
            tile = image
            if flips[0] and i % 2:
                tile = tile[:, ::-1]
            if flips[1] and j % 2:
                tile = tile[::-1]
            row.append(tile)
        rows.append(np.concatenate(row, 1))
    return np.concatenate(rows, 0)


def _colour_table(colours):
    """Vertex colours reduced to the model's limit. -> (their indexes, the table as 0xBBGGRR words)."""
    pixels = np.clip(np.round(np.array(colours) * NEUTRAL / 255), 0, 255).astype(np.uint8)
    image = Image.fromarray(pixels[None, :, :], 'RGB').quantize(MAX_COLOURS, Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = np.array(image.getpalette()[:3 * MAX_COLOURS]).reshape(-1, 3)
    return np.array(image)[0], [int(r) | int(g) << 8 | int(b) << 16 for r, g, b in palette]


def build_model(name, triangles, materials, lev_per_unit, mirror=False, billboard=False, two_sided=False):
    """A CTR model of an MKDS model. triangles: (material index, [(pos, uv, colour, normal) * 3]) in MKDS
    units. two_sided: seen from behind too. -> (ctr_items.ItemModel, half its size in level units as x, y,
    z, the middle of it, what an instance's scale takes on, as a model scale is 16 bits, and its lowest
    corner in level units)."""
    if two_sided:
        triangles = triangles + [(mi, tri[::-1]) for mi, tri in triangles]
    points = np.array([v[0] for _, tri in triangles for v in tri], float) * lev_per_unit
    if mirror:
        points[:, 0] = -points[:, 0]
    low, high = points.min(0), points.max(0)
    extent = np.maximum(high - low, 1.0)
    scale_up = max(1, math.ceil(extent.max() * SCALE_ONE / 255 / MAX_MODEL_SCALE))
    scale = np.ceil(extent / scale_up * SCALE_ONE / 255).astype(int)
    points = points / scale_up
    origin = np.floor(points.min(0) * SCALE_ONE / scale).astype(int)
    stored = np.clip(np.round(points * SCALE_ONE / scale) - origin, 0, 255).astype(int)

    colours = []
    for mi, tri in triangles:
        white = np.array([255.0, 255.0, 255.0])
        colours += [np.array(v[2]) if v[2] is not None else white for v in tri]
    indexes, table = _colour_table(colours)
    while len(table) % 4:
        table.append(0)

    used = sorted({mi for mi, _ in triangles})
    turn = (0, 1, 2) if mirror else (0, 2, 1)  # the game's front faces wind the other way round
    folded, tiles = [], {mi: [1, 1] for mi in used}
    for mi, tri in triangles:
        material = materials[mi]
        sx, sy, tx, ty = material.texmtx
        uv = np.array([tri[c][1] for c in turn], float) * (sx, sy) / (max(material.width, 1), max(material.height, 1)) - (tx, ty)  # its offset is in whole textures
        repeats, flips = ((material.teximage >> 16) & 1, (material.teximage >> 17) & 1), ((material.teximage >> 18) & 1, (material.teximage >> 19) & 1)
        uv = np.stack([fold(uv[:, axis], repeats[axis], flips[axis]) for axis in (0, 1)], 1)
        folded.append(uv)
        for axis in (0, 1):
            tiles[mi][axis] = min(max(tiles[mi][axis], math.ceil(uv[:, axis].max() - UV_SLACK)), MAX_TILES)
    images = {}
    for mi in used:
        material = materials[mi]
        whole = tiled(image_of(material), tiles[mi], ((material.teximage >> 18) & 1, (material.teximage >> 19) & 1))
        images[mi] = whole[::math.ceil(whole.shape[0] / MAX_HEIGHT), ::math.ceil(whole.shape[1] / MAX_WIDTH)]
    layouts, commands, order = {}, [], []
    for n, (mi, tri) in enumerate(triangles):
        image = images[mi]
        h, w = image.shape[:2]
        uv = folded[n] / tiles[mi]
        texels = np.clip(np.round(uv * (w, h)), 0, (w - 1, h - 1)).astype(int)
        key = (mi, *texels.ravel().tolist())
        if key not in layouts:
            if len(layouts) == MAX_LAYOUTS:
                key = min(layouts, key=lambda k: (k[0] != mi, sum(abs(a - b) for a, b in zip(k[1:], key[1:]))))
            else:
                layouts[key] = len(layouts) + 1
        for corner, c in enumerate(turn):
            flags = CMD_FIRST if corner == 0 else CMD_NEXT
            commands.append(flags << 24 | STACK_SLOT << 16 | int(indexes[3 * n + c]) << 9 | layouts[key])
            order.append(3 * n + c)
    vertices = bytearray()
    for i in order:
        x, y, z = stored[i]
        vertices += bytes((x, z, y))
    while len(vertices) % 4:
        vertices.append(0)

    blob = bytearray(MODEL_SIZE + HEADER_SIZE)
    blob[:16] = (name.encode()[:15] + bytes(16))[:16]
    struct.pack_into('<hhI', blob, 0x10, MODEL_ID, 1, MODEL_SIZE)
    head = MODEL_SIZE
    blob[head:head + 16] = blob[:16]
    struct.pack_into('<hH', blob, head + 0x14, LOD_ALWAYS, BILLBOARD if billboard else 0)
    struct.pack_into('<3h', blob, head + 0x18, int(scale[0]), int(scale[1]), int(scale[2]))
    pointers = [0x14]

    def field(offset):
        struct.pack_into('<I', blob, head + offset, len(blob))
        pointers.append(head + offset)

    field(0x20)
    blob += struct.pack('<I', len(table)) + struct.pack(f'<{len(commands)}I', *commands) + struct.pack('<I', 0xffffffff)
    field(0x24)
    frame = bytearray(FRAME_SIZE)
    struct.pack_into('<3h', frame, 0, int(origin[0]), int(origin[1]), int(origin[2]))  # x, height, depth, while a vertex's bytes are x, depth, height
    struct.pack_into('<I', frame, 0x18, FRAME_SIZE)
    blob += frame + vertices
    field(0x2c)
    blob += struct.pack(f'<{len(table)}I', *table)
    field(0x28)
    table_at = len(blob)
    blob += bytes(4 * len(layouts))
    groups = {mi: {'layouts': [], 'u': 0, 'v': 0, 'image': images[mi]} for mi in used}
    for key, index in layouts.items():
        at = len(blob)
        t = key[1:]
        blob += struct.pack('<BBHBBHBBBB', t[0], t[1], 0, t[2], t[3], 0, t[4], t[5], t[4], t[5])
        struct.pack_into('<I', blob, table_at + 4 * (index - 1), at)
        pointers.append(table_at + 4 * (index - 1))
        groups[key[0]]['layouts'].append(at)
    return ctr_items.ItemModel(blob, sorted(pointers), [g for g in groups.values() if g['layouts']]), extent / 2, (low + high) / 2, scale_up, low


def records(kind, speed, damage_flags, half, middle, base, mode_args, points):
    """The thread's list, as rows of three 16-bit values (see game/ConvertedTrack/Mover.c)."""
    rows = [[kind, int(round(speed)), damage_flags - 0x10000 if damage_flags & 0x8000 else damage_flags], [int(round(h)) for h in half], list(mode_args)]
    rows += [[int(round(c * SCALE_ONE)) for c in row] for row in base]
    rows += [[int(round(c)) for c in middle]]
    rows += [[int(round(c)) for c in p] for p in points]
    return rows


def key_records(track, spec, half, middle, base, flags, to_lev, lev_unit, size):
    """A MOVER_KEYS list, from a mkds_motion.Track. half: the object's own hit box."""
    step = track.step
    keys = [[0, KEY_HIDDEN, 0] if p is None else [int(round(c)) for c in to_lev(p)] for p in track.ticks[::step]]
    ticks_a_second = mkds_motion.TICKS
    rise, top = track.grow if track.grow else (0.0, 0.0)
    grow = [[int(round(rise * ticks_a_second * SCALE_ONE)), int(round(top * SCALE_ONE)), len(track.gates)]]
    gates = []
    for tick, radius, lap, point in track.gates:
        gates += [[tick // step, int(radius * lev_unit), lap], [int(round(c)) for c in to_lev(point)] if point is not None else [0, 0, 0]]
    if track.hit is not None:
        half = np.full(3, track.hit * lev_unit) * np.abs(size)
        if not track.hit:
            middle = np.zeros(3)
    damage = spec.damage if track.damage is None else track.damage
    args = (track.restart // step, track.lap_step, track.start // step)
    return records(KEYS, KEY_MS * step // 3, damage | flags, half, middle, base, args, grow + gates + keys), track.ticks


def place(movers, model_index, halves, to_lev, lev_unit, rotate, settle, context):
    """-> (one entry per instance, the record lists). An object on a path that several share gets a start
    spaced along it; one on a centre, a phase round it. settle: a point -> that point on the ground there.
    context: what mkds_motion's objects need of the course."""
    placed, lists = [], []
    for mover in movers:
        spec, pos, rot, size = mover['spec'], mover['pos'], mover['rot'], mover['size']
        if spec.model is None:
            base = np.eye(3)
            for track in mkds_motion.tracks(spec.kind[1], mover, context):
                listed, ticks = key_records(track, spec, np.zeros(3), CRATE_MIDDLE, base, 0, to_lev, lev_unit, np.ones(3))
                at = next(p for p in ticks if p is not None)
                placed.append({'model': 'crate', 'pos': [int(round(c)) for c in to_lev(at)], 'scale': [SCALE_ONE] * 3, 'list': len(lists), 'reach': 64.0, 'name': 'crate'})
                lists.append(listed)
            continue
        half, middle, scale_up, low, flat = halves[model_index[mover['key']]]
        base = np.eye(3) if flat and spec.kind[0] != ORBIT else np.array([rotate(np.array(axis, float), rot) for axis in np.eye(3)]).T
        flags = spec.flags & ~(FACE_PATH | ROLL) if flat else spec.flags  # the game turns a picture itself
        half, middle = half * np.abs(size), middle * size
        reach = float(half.max())
        if flat:
            half = np.array([half[0], half[1], half[0]])  # it faces every way, so it is as deep as it is wide
        half = half * HIT_SHARE.get(spec.model, ROUND_SHARE if flat or spec.flags & ROLL else 1.0)
        known = mkds_objects.OBJECTS.get(mover['id'])
        if known is not None and known.shape in ('sphere', 'cylinder', 'box'):  # the game's own, in world units
            sides = {'sphere': (known.a, known.a, known.a), 'cylinder': (known.a, known.b, known.a), 'box': (known.a, known.b, known.c)}[known.shape]
            half = np.array(sides, float) * np.abs(size) * lev_unit
        if spec.model == 'pendulum':
            half, middle = np.array(PENDULUM_BOB[0], float) * np.abs(size) * lev_unit, np.array(PENDULUM_BOB[1], float) * size * lev_unit
        if spec.damage == 0 and not spec.flags & (SOLID | CARRIES):
            half = half * 0
        setting = mover['settings'][spec.setting]
        speed = setting * spec.factor if setting and spec.factor else spec.fixed
        kind, args = spec.kind[0], spec.kind[1:]
        if kind == KEYS:
            own_scale = [int(round(c * SCALE_ONE * scale_up)) for c in size]
            foot = -low[1] / lev_unit * size[1]
            for n, track in enumerate(mkds_motion.tracks(args[0], mover, {**context, 'foot': foot})):
                model = model_index[(track.model, False)] if track.model else model_index[mover['key']]
                other = track.model is not None
                if other:
                    other_half, other_middle, other_up, _, other_flat = halves[model]
                    scale = [int(round(c * SCALE_ONE * other_up)) for c in (size if track.hit == 0 else np.ones(3))]
                else:
                    scale = own_scale
                listed, ticks = key_records(track, spec, half, middle, base, flags, to_lev, lev_unit, size)
                at = next(p for p in ticks if p is not None)
                placed.append({'model': model, 'pos': [int(round(c)) for c in to_lev(at)], 'scale': scale, 'list': len(lists), 'reach': reach, 'name': track.model or spec.model})
                lists.append(listed)
            continue
        standing = spec.model not in FLYING
        foot = low[1] / lev_unit * size[1]  # MKDS units from its origin down to its lowest point
        lift = np.array([0.0, mover['over'] * size[1] - (foot if standing else 0.0), 0.0])
        count, phases = spec.count, []
        settings = mover['settings']
        if spec.model == 'fireball2':
            count = max(settings[0], 1) * (settings[3] + 1)
        for i in range(count):
            at = pos
            points = []
            if kind == PATH:
                route = [(settle(p) if standing else p) + lift for p in mover['path'].points]
                first = path_start(route, pos) if count == 1 else len(route) * i // count
                mode = args[0]
                if mode == LOOP and not mover['path'].loop:
                    mode = BACK_AND_FORTH
                mode_args = (mode, first, 0)
                points = [to_lev(p) for p in route]
                at = route[first]
            elif kind == PULSE:
                on, off = settings[0], settings[1]  # ticks
                mode_args = ((off + on) * 1000 // TICKS, on * 1000 // TICKS, settings[2] * 1000 // TICKS)
            elif kind == SPIN:
                rate = args[1] or int(settings[0] * SPIN_RATE)
                mode_args = (args[0], -rate if settings[4] else rate, 0)  # Tick-Tock Clock's neighbours turn opposite ways
                if not args[1] and settings[1]:
                    points = [(settings[1], settings[2], settings[3])]  # ticks at speed, to speed up or slow down, stopped; then it turns back
            elif kind == SWING:
                if spec.model == 'flipper':
                    side = -1 if mover['key'][1] else 1
                    mode_args = (args[0], 0, 0)
                    points = [(side * FLIPPER_REST, side * FLIPPER_STROKE, FLIPPER_WAIT)]
                else:
                    period, peak = settings[0] * 40, settings[1] * TURN * 24 // 36000  # s0 * 2.4 ticks; 100 is 24 degrees
                    mode_args = (args[0], period, peak)
            elif kind == HOP:
                way = np.array([0.0, HOP_HEIGHT[spec.model] * size[1], 0.0])
                mode_args = (args[0], 0, (hash_phase(pos) % args[0]))
                points = [way * lev_unit]
                at = (settle(pos) if standing else pos) + lift
            else:
                # hoops a kart goes through: s0 balls a hoop, s3 + 1 hoops, s1 the outer radius, s2 their speed
                per_ring, rings = max(settings[0], 1), settings[3] + 1
                radius = settings[1] * ((i // per_ring) + 1) / rings
                period = int(65536 * 100 * 1000 / (max(settings[2], 1) * 364 * TICKS))
                mode_args = (int(radius * lev_unit), period, period * (i % per_ring) // per_ring)
                at = pos + lift
            placed.append({'model': model_index[mover['key']], 'pos': [int(round(c)) for c in to_lev(at)], 'scale': [int(round(c * SCALE_ONE * scale_up)) for c in size], 'list': len(lists), 'reach': reach, 'name': spec.model})
            lists.append(records(kind, speed, spec.damage | flags, half, middle, base, mode_args, points))
            if spec.model == 'NsKiller1':
                parent = len(lists) - 1
                start = pos + (0.0, BULLET_UP, 0.0)
                way = rotate(np.array([0.0, 0.0, BULLET_REACH / (BULLETS)]), rot)
                flight = [to_lev(start + way * n) for n in range(BULLETS + 1)]
                bullet_half = np.array([15.0, 15.0, 15.0]) * lev_unit
                for n in range(BULLETS):
                    placed.append({'model': model_index[mover['bullet']], 'pos': [int(round(c)) for c in flight[n]], 'scale': [SCALE_ONE] * 3, 'list': len(lists), 'reach': 200.0, 'name': BULLET.model})
                    lists.append(records(PATH, BULLET.fixed, BULLET.damage | BULLET.flags, bullet_half, np.zeros(3), base, (RESTART, n, parent + 1), flight))
    return placed, lists


def hash_phase(pos):
    """A start in a cycle that differs from one object to the next, the same each run."""
    return int(abs(pos[0]) * 7 + abs(pos[2]) * 13) & 0x7fff


def path_start(points, pos):
    """The point of a path nearest an object placed on it."""
    return int(np.argmin(np.linalg.norm(np.array(points) - pos, axis=1)))
