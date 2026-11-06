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

MODEL_ID = -1  # no slot in the game's model table
HEADER_SIZE, LAYOUT_SIZE, MODEL_SIZE = 0x40, 12, 0x18
FRAME_SIZE = 0x1c
SCALE_ONE = 4096
TURN = 4096
LOD_ALWAYS = 0x7fff
BILLBOARD = 2  # ModelHeader.flags: always faces the camera
CMD_FIRST, CMD_NEXT = 0x98, 0x18  # a new strip, culled, colours from the scratchpad; a further vertex
MAX_COLOURS = 128
UV_SLACK = 0.03  # of a texture: corners a hair past its edge are on it, not on the next repeat
MAX_MODEL_SCALE = 0x7fff
STACK_SLOT = 0x57  # a vertex's scratch slot, as retail models write it: lower ones overlap the colours
MAX_LAYOUTS = 511
NEUTRAL = 128  # a texture is multiplied by a vertex colour over this

PATH, SPIN, SWING, HOP, ORBIT, PULSE = range(6)
LOOP, BACK_AND_FORTH, RESTART = range(3)
ARC, SLAM = range(2)
FACE_PATH, ROLL = 0x100, 0x200
SPIN_DAMAGE, BLAST, SQUISH = 1, 2, 3  # RB_Hazard_HurtDriver

# model; (kind, its arguments); damage; level units a second per unit of the speed setting; flags; how many
# share the object's path or centre; lift off its path in MKDS units, as its origin lies in its middle;
# which setting is its speed; its speed in level units a second where the setting is 0.
Mover = namedtuple('Mover', 'model kind damage factor flags count lift setting fixed', defaults=(0, 0.0, 0, 1, 0.0, 0, 600))
MOVERS = {
    0x195: Mover('car_a', (PATH, LOOP), BLAST, 10, FACE_PATH),
    0x19a: Mover('bus_a', (PATH, LOOP), BLAST, 10, FACE_PATH),
    0x19c: Mover('truck_a', (PATH, LOOP), BLAST, 10, FACE_PATH),
    0x192: Mover('rock', (PATH, RESTART), BLAST, 8, ROLL, lift=28),
    0x1b1: Mover('rock2', (PATH, RESTART), BLAST, 10, ROLL, lift=28, setting=1),
    0x1b0: Mover('IronBall', (PATH, RESTART), BLAST, 25, ROLL, count=3, lift=16),
    0x191: Mover('kuribo', (PATH, BACK_AND_FORTH), SPIN_DAMAGE, 2, FACE_PATH),
    0x1ac: Mover('crab', (PATH, BACK_AND_FORTH), SPIN_DAMAGE, 3),
    0x1a5: Mover('wanwan', (PATH, LOOP), BLAST, 4, FACE_PATH, lift=28),
    0x139: Mover('kamome', (PATH, LOOP), 0, 8, FACE_PATH),
    0x158: Mover('airship', (PATH, LOOP), 0, 15, FACE_PATH),
    0x1ad: Mover('sun', (PATH, LOOP), 0, 0.2),
    0x13c: Mover('kanoke_64', (PATH, BACK_AND_FORTH), 0, 0, FACE_PATH),
    0x1a1: Mover('NsKiller2', (PATH, RESTART), BLAST, 0, FACE_PATH, fixed=2000),
    0x1a4: Mover('mkd_ef_burner', (PULSE,), SPIN_DAMAGE),
    0xcd: Mover('second_hand', (SPIN, 1, 0), SPIN_DAMAGE),
    0xce: Mover('pendulum', (SWING, 2, 0, 0), BLAST),
    0xd2: Mover('dram', (SPIN, 0, 800)),
    0x1a9: Mover('flipper', (SWING, 1, 2400, 300), BLAST),
    0x1a3: Mover('move_tree', (PATH, LOOP), SPIN_DAMAGE, 2, FACE_PATH),
    0x154: Mover('RainStar', (SPIN, 1, 500)),
    0x193: Mover('dossun', (HOP, 4000, SLAM), SQUISH),
    0x19b: Mover('pukupuku', (HOP, 3000, ARC), SPIN_DAMAGE),
    0x19e: Mover('bakubaku', (HOP, 5000, ARC), BLAST),
    0x1a7: Mover('poo', (HOP, 4000, ARC), SPIN_DAMAGE),
    0x13d: Mover('teresa', (HOP, 3000, ARC)),
    0x196: Mover('wanwan', (HOP, 5000, SLAM), BLAST, lift=28),
    0x1a0: Mover('basabasa', (ORBIT, 120, 3000), SPIN_DAMAGE, count=4),
    0x1af: Mover('fireball2', (ORBIT, 0, 3600), SPIN_DAMAGE),
}
TYRED = {'car_a', 'bus_a', 'truck_a'}
TWO_SIDED = {'mkd_ef_burner'}  # flat, and it does not turn to the camera
SOLID_STILL = {'dram'}  # stays where it is and keeps its walls
HIT_SHARE = {'move_tree': 0.3}  # of its box: its branches swing over a kart
HOP_HEIGHT = {'pukupuku': 90, 'bakubaku': 170, 'poo': 40, 'teresa': 24, 'dossun': 400}  # MKDS units up
LUNGE = {'wanwan': 120}  # MKDS units along its facing: a chained Chomp


def image_of(material):
    """A material's picture as RGBA, every texel solid or clear; a plain one for a colour alone."""
    if material.tex:
        out = np.array(material.tex0.decode(material.tex, material.pal), np.uint8)
    else:
        out = np.full((4, 4, 4), 255, np.uint8)
    out[..., 3] = np.where(out[..., 3] > 127, 255, 0)
    return out


def _colour_table(colours):
    """Vertex colours reduced to the model's limit. -> (their indexes, the table as 0xBBGGRR words)."""
    pixels = np.clip(np.round(np.array(colours) * NEUTRAL / 255), 0, 255).astype(np.uint8)
    image = Image.fromarray(pixels[None, :, :], 'RGB').quantize(MAX_COLOURS, Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = np.array(image.getpalette()[:3 * MAX_COLOURS]).reshape(-1, 3)
    return np.array(image)[0], [int(r) | int(g) << 8 | int(b) << 16 for r, g, b in palette]


def build_model(name, triangles, materials, lev_per_unit, mirror=False, billboard=False, two_sided=False):
    """A CTR model of an MKDS model. triangles: (material index, [(pos, uv, colour, normal) * 3]) in MKDS
    units. two_sided: a flat model seen from behind too. -> (ctr_items.ItemModel, half its size in level
    units as x, y, z, the middle of it, and what an instance's scale takes on: a model scale is 16 bits)."""
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
    images = {mi: image_of(materials[mi]) for mi in used}
    layouts, commands, order = {}, [], []
    for n, (mi, tri) in enumerate(triangles):
        material, image = materials[mi], images[mi]
        h, w = image.shape[:2]
        sx, sy = material.texmtx[:2]
        turn = (0, 1, 2) if mirror else (0, 2, 1)  # the game's front faces wind the other way round
        uv = np.array([tri[c][1] for c in turn], float) * (sx / max(material.width, 1), sy / max(material.height, 1))
        uv -= np.floor(uv.min(0) + UV_SLACK)  # a repeat of a texture in a model stays on its first tile
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
    struct.pack_into('<3h', frame, 0, int(origin[0]), int(origin[2]), int(origin[1]))  # x, depth, height: see below
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
    return ctr_items.ItemModel(blob, sorted(pointers), [g for g in groups.values() if g['layouts']]), extent / 2, (low + high) / 2, scale_up


def records(kind, speed, damage_flags, half, middle, base, mode_args, points):
    """The thread's list, as rows of three 16-bit values (see game/ConvertedTrack/Mover.c)."""
    rows = [[kind, int(round(speed)), damage_flags], [int(round(h)) for h in half], list(mode_args)]
    rows += [[int(round(c * SCALE_ONE)) for c in row] for row in base]
    rows += [[int(round(c)) for c in middle]]
    rows += [[int(round(c)) for c in p] for p in points]
    return rows


def place(movers, model_index, halves, to_lev, lev_unit, rotate):
    """-> (one entry per instance, the record lists). An object on a path that several share gets a start
    spaced along it; one on a centre, a phase round it."""
    placed, lists = [], []
    for mover in movers:
        spec, pos, rot, size = mover['spec'], mover['pos'], mover['rot'], mover['size']
        base = np.array([rotate(np.array(axis, float), rot) for axis in np.eye(3)]).T
        half, middle, scale_up = halves[model_index[mover['key']]]
        half, middle = half * np.abs(size), middle * size
        half = half * HIT_SHARE.get(spec.model, 0.7 if spec.flags & ROLL or spec.kind[0] != PATH else 1.0)
        if spec.damage == 0:
            half = half * 0
        setting = mover['settings'][spec.setting]
        speed = setting * spec.factor if setting and spec.factor else spec.fixed
        kind, args = spec.kind[0], spec.kind[1:]
        lift = np.array([0.0, spec.lift * size[1] + mover['over'] * size[1], 0.0])
        count, phases = spec.count, []
        if kind == ORBIT and spec.model == 'fireball2':
            count = max(mover['settings'][0], 1)
        for i in range(count):
            at = pos
            points = []
            if kind == PATH:
                route = [p + lift for p in mover['path'][0]]
                first = path_start(route, pos) if count == 1 else len(route) * i // count
                mode = args[0]
                if mode == LOOP and not mover['path'][1]:
                    mode = BACK_AND_FORTH
                mode_args = (mode, first, 0)
                points = [to_lev(p) for p in route]
                at = route[first]
            elif kind == PULSE:
                settings = mover['settings']
                off, on = settings[0] or 120, settings[1] or settings[3] or 120  # frames
                mode_args = ((off + on) * 1000 // 60, on * 1000 // 60, hash_phase(pos) % ((off + on) * 1000 // 60))
            elif kind == SPIN:
                mode_args = (args[0], args[1] or mover['settings'][0] * TURN // 720, 0)  # half its setting, in degrees a second
            elif kind == SWING:
                period, peak = args[1] or mover['settings'][1], args[2] or mover['settings'][3] * TURN // 360
                mode_args = (args[0], period, -peak if mover['key'][1] else peak)
            elif kind == HOP:
                if spec.model in LUNGE:
                    way = rotate(np.array([0.0, 0.0, LUNGE[spec.model]]), rot) * size[2]
                else:
                    way = np.array([0.0, HOP_HEIGHT[spec.model] * size[1], 0.0])
                mode_args = (args[0], args[1], (hash_phase(pos) % args[0]))
                points = [way * lev_unit]
                at = pos + lift
            else:
                radius = args[0] or mover['settings'][2]
                mode_args = (int(radius * lev_unit), args[1], args[1] * i // count)
                at = pos + lift
            placed.append({'model': model_index[mover['key']], 'pos': [int(round(c)) for c in to_lev(at)], 'scale': [int(round(c * SCALE_ONE * scale_up)) for c in size], 'list': len(lists)})
            lists.append(records(kind, speed, spec.damage | spec.flags, half, middle, base, mode_args, points))
    return placed, lists


def hash_phase(pos):
    """A start in a cycle that differs from one object to the next, the same each run."""
    return int(abs(pos[0]) * 7 + abs(pos[2]) * 13) & 0x7fff


def path_start(points, pos):
    """The point of a path nearest an object placed on it."""
    return int(np.argmin(np.linalg.norm(np.array(points) - pos, axis=1)))
