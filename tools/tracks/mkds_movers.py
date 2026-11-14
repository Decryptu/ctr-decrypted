"""What moves in a Mario Kart DS course, as level instances (docs/TRACK-CONVERTER.md#movers).

An object in MOVERS is not baked into the course: its MKDS model becomes a CTR model the LEV carries, and
game/ConvertedTrack/Mover.c moves it and hits karts through the game's hazard code. `Mover` says how an
object moves; `build_model` writes the model; `records` writes the list the thread reads.
"""
import copy, math, random, struct
from collections import namedtuple

import numpy as np
from PIL import Image

import ctr_items
import mkds_motion
import mkds_objects

MODEL_ID = -1  # no slot in the game's model table
HEADER_SIZE, LAYOUT_SIZE, MODEL_SIZE = 0x40, 12, 0x18
FRAME_SIZE = 0x1c
HALF_FRAMES = 0x8000  # ModelAnim.numFrames: it counts half frames, the odd ones eased
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
# Blend mode 3 in a layout's page: the instance renderer draws its polygons solid. No mover texel sets the
# PS1's blend bit, so another mode blends nothing, and the renderer gives each of its polygons a draw of
# its own (platform/native_gpu.c AddSplit): a cog's thousands of them slowed the frame.
OPAQUE_PAGE = ctr_items.ABR_MASK
MAX_WIDTH, MAX_HEIGHT = 128, 256  # texels: wider, it would have to give up colours or be halved, and its layouts would miss
NEUTRAL = 128  # a texture is multiplied by a vertex colour over this
LAYERED_BILLBOARD_SIZE = 128  # a square page region at 8 bits
CUTOUT_LIFT = -2.0  # world units along z, which a picture turns away from the camera: an animated picture's cutouts over its soft layers (lift_cutouts)

PATH, SPIN, SWING, _HOP, ORBIT, PULSE, KEYS, STILL, TURNS, CHOMP, LINK, GHOST = range(12)
LOOP, BACK_AND_FORTH, RESTART = range(3)
FACE_PATH, ROLL, SOLID, CARRIES, ANIM_KEYS, DROPS = 0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000  # Mover.c's MOVER_FLAG_
STAR_WALL, STRIKE_SHIFT = 0x400, 4  # Mover.c's MOVER_FLAG_STAR_WALL, MOVER_STRIKE_SHIFT
SWAY = 0x800  # Mover.c's MOVER_FLAG_SWAY: a Pokey's part, its last record its part and the gaps under it
TICKS = 60  # a second: the game's objects
SPIN_RATE = 4.8  # 4096ths of a turn a second per unit of a spinner's speed setting: (s << 7) / 100 of 65536 a tick
PATH_UNITS = 3.2  # level units a second per hundredth of a world unit a tick
SPIN_DAMAGE, BLAST, SQUISH = 1, 2, 3  # RB_Hazard_HurtDriver
ITEM_BOX = 4  # Mover.c: it breaks as a weapon crate does
KEY_HIDDEN = -32768
KEY_MS = 50  # three ticks
TURN_KEY_TICKS = 3
CRATE_MIDDLE = np.array([0.0, 48.0, 0.0])  # level units over its origin, which is its underside

# model; (kind, its arguments); what a hit does, where it is not the game's own (REACTIONS); level units a
# second per unit of the speed setting; flags; how many share the object's path or centre; which setting is
# its speed; its speed in level units a second where the setting is 0. A model stands on the ground by its own
# lowest point, unless it is in FLYING.
BULLETS, BULLET_REACH, BULLET_UP = 3, 3600, 42  # a cannon's shots in the air at once; how far one flies, and how high over the cannon it starts
Mover = namedtuple('Mover', 'model kind damage factor flags count setting fixed', defaults=(None, 0.0, 0, 1, 0, 600))
MOVERS = {
    0x195: Mover('bus_a', (PATH, LOOP), None, 3 * PATH_UNITS, FACE_PATH),
    0x19a: Mover('car_a', (PATH, LOOP), None, 3 * PATH_UNITS, FACE_PATH),
    0x19c: Mover('truck_a', (PATH, LOOP), None, 3 * PATH_UNITS, FACE_PATH),
    0x192: Mover('rock', (KEYS, 'rock'), flags=ROLL),
    0x1b1: Mover('rock2', (KEYS, 'rock'), flags=ROLL),
    0x1b0: Mover('IronBall', (KEYS, 'ball')),
    0x191: Mover('kuribo', (PATH, BACK_AND_FORTH), None, 0.25 * PATH_UNITS),  # s0 / 200 a tick, half its time
    0x1ac: Mover('crab', (PATH, BACK_AND_FORTH), None, 0, fixed=50 * PATH_UNITS),
    0x1a5: Mover('wanwan', (KEYS, 'chain_path'), flags=FACE_PATH),
    0x158: Mover('airship', (KEYS, 'airship'), flags=FACE_PATH),
    0x1ad: Mover('sun', (KEYS, 'sun')),
    0x19e: Mover('basabasa', (KEYS, 'coffin')),
    0xc9: Mover(None, (KEYS, 'moving_box'), ITEM_BOX),
    0x1a1: Mover('NsKiller1', (PATH, LOOP), None, 0, fixed=200 * PATH_UNITS),
    0x1a4: Mover('mkd_ef_burner', (PULSE,)),
    0xcb: Mover('gear_black', (SPIN, 1, 0), flags=CARRIES),
    0xce: Mover('test_cylinder', (SPIN, 1, 0), flags=CARRIES),
    0xd0: Mover('rotary_room', (SPIN, 1, 0), flags=CARRIES),
    0xd1: Mover('rotary_bridge', (SPIN, 1, 0), flags=CARRIES),
    0xcd: Mover('second_hand', (SPIN, 1, 0)),
    0xcf: Mover('pendulum', (SWING, 2, 0, 0)),
    0xd2: Mover('dram', (TURNS, 'drums', 0, 0)),
    0x1a9: Mover('flipper', (SWING, 1, 0, 0)),
    0x1a3: Mover('move_tree', (KEYS, 'move_tree')),
    0x154: Mover('RainStar', (SPIN, 1, 500)),
    0x193: Mover('dossun', (KEYS, 'thwomp'), SQUISH, flags=DROPS),  # a wall, and it crushes from under while it falls
    0x19b: Mover('pukupuku', (KEYS, 'cheep_cheep')),
    0x13c: Mover('bakubaku', (KEYS, 'bakubaku')),
    0x1a7: Mover('poo', (KEYS, 'rocky_wrench')),
    0x199: Mover('choropu', (KEYS, 'monty_mole')),
    0x13d: Mover('teresa', (GHOST,)),
    0x196: Mover('wanwan', (CHOMP,)),
    0x1a0: Mover('basabasa', (KEYS, 'bats')),
    0x1af: Mover('fireball2', (ORBIT,)),
    0x19d: Mover('sman_bottom', (STILL,)),
    0x1a6: Mover('ob_pakkun_sf', (STILL,)),
    0xd: Mover('puddle', (STILL,)),
    0x1a8: Mover('bound', (STILL,)),
    0x198: Mover('mkd_ef_bubble', (KEYS, 'bubble')),
    0x140: Mover('chandelier', (STILL,)),
    0x151: Mover('picture1', (STILL,)),
    0x152: Mover('picture2', (STILL,)),
}
# Models whose joint animation (MapObj/<model>.nsbca) plays, one frame a tick (animate). A STILL one loops
# from the tick START_TICK draws (first, and how many more at random), or anywhere in its loop; a KEYS one
# plays with its keys' clock and stands at its first frame while a gate holds it, unless its track says how
# far through its animation it is each tick (mkds_motion.Track.anim; a TURNS one, a third item a tick).
ANIMATED = {'picture1', 'picture2', 'chandelier', 'kanoke_64', 'sun', 'PakkunMouth', 'move_tree'}
START_TICK = {'chandelier': (60, 60)}  # MKDS 0x209f338
# Texture patterns (MapObj/<model>.nsbtp): which of its frames a model shows, read from each object's code.
# A timed one shows these frames a tick, round and round on its mover's clock (build_model writes them
# as the model's pattern), (frame, True) mirrored: a Goomba its step clock's tick in each half of its 40,
# the second half turned over, as the game draws it with its width negated (0x20db108); a crab's body
# and hands their animations' frame at an eighth of a frame a tick (0x209b124), a piranha plant bit 4 of
# its tick count (0x20e5c78), a bat a frame a tick (0x20927ac), a bumper on a path 1 to 29 (0x209c254).
TIMED_PATTERNS = {'kuribo': list(range(20)) + [(t, True) for t in range(20)], 'crab': [t // 8 for t in range(16)], 'crab_hand': [t // 8 for t in range(16)],
                  'ob_pakkun_sf': [(t >> 4) & 1 for t in range(32)], 'basabasa': list(range(13)), 'bound': list(range(1, 30))}
KEYED_PATTERNS = {'poo', 'choropu'}  # a frame a key, from its track (mkds_motion.Track.pattern); a Monty Mole's dazed face while flung (Mover.c)
# One frame an instance keeps: a car's or truck's colour is its setting 2 (0x20e2b7c); a Boo's face
# alternates through its group (0x2094c34), and the other is a model of its own (GHOSTS).
SETTING_PATTERNS = {'car_a': 2, 'truck_a': 2}
TIMED = 'timed'


def pattern_variant(spec, settings, has_path):
    """What of a mover's model's texture pattern it shows: None, a frame it keeps, or TIMED (its frames a
    tick, or its keys')."""
    if spec.model in SETTING_PATTERNS:
        return settings[SETTING_PATTERNS[spec.model]]
    if spec.model == 'teresa':
        return 0
    if spec.model in KEYED_PATTERNS or (spec.model in TIMED_PATTERNS and (spec.model != 'bound' or has_path)):
        return TIMED  # a bumper that stands still shows its first frame for good
    return None


def show_pattern_frame(materials, pattern, frame):
    """Draw materials with the textures a pattern shows at one of its frames."""
    for m in materials:
        names = pattern.at(m.name, frame)
        if names is not None:
            m.tex, m.pal = names
# What a kart that touches an object gets, by the game's reaction to it (mkds_objects.REACTION): (damage,
# flags). 2 is a wall; 3 to 7 spin a kart; 8 knocks it away; 9 and 10 throw it; 11 crushes it; 12 bounces it.
REACTIONS = {2: (0, SOLID), 3: (SPIN_DAMAGE, 0), 4: (SPIN_DAMAGE, 0), 5: (SPIN_DAMAGE, 0), 6: (SPIN_DAMAGE, 0), 7: (SPIN_DAMAGE, 0),
             8: (SPIN_DAMAGE, SOLID), 9: (BLAST, 0), 10: (BLAST, 0), 11: (SQUISH, 0), 12: (0, SOLID)}
# What a kart with the mask, CTR's Star, does to the objects the game lets a Star kart strike
# (mkds_objects.STRUCK): Mover.c's MOVER_STRUCK_*, which play each object's own struck reaction.
SPIN_AWAY, FLIP, FLIP_LIE, FLY_OFF, FALL, POP, BREAK, TUMBLE, KNOCK, KNOCK_GONE, KNOCK_CRAB, BURST, BURST_HEAD, BOUNCE, TOUCHED = range(1, 16)
# What any kart's touch sets off, not only a Star's (TOUCHED, Mover.c's Mover_Touched): a moving tree shakes, a Monty Mole is flung
TOUCHED_BY_ANY = {0x1a3, 0x199}
STRUCK = {0x193: SPIN_AWAY, 0x1a5: FLIP, 0x196: FLIP_LIE, 0x1ae: FLY_OFF, 0x1a2: FALL, 0x191: POP, 0x192: BREAK, 0x1b1: BREAK,
          0x1ab: BREAK, 0x195: TUMBLE, 0x19a: TUMBLE, 0x19c: TUMBLE, 0x19b: KNOCK, 0x1a6: KNOCK_GONE, 0x1ac: KNOCK_CRAB, 0x19d: BURST,
          0x1b0: BOUNCE}
# What an object brings that the game reacts to as another object: a sun's flares
PART_REACTS = {'fireball2': 0x1ae}
BOUND_ON_PATH = Mover('bound', (PATH, LOOP), None, 0, fixed=300 * PATH_UNITS)
BULLET = Mover('NsKiller2', (PATH, RESTART), None, 0, FACE_PATH, count=BULLETS, fixed=600 * PATH_UNITS)
BULLET_REACTS = 0x1a2
BURNER_ON_PATH = Mover('mkd_ef_burner', (PATH, BACK_AND_FORTH), None, PATH_UNITS, setting=3)  # sweeps along its path, always lit
TYRED = {'car_a', 'bus_a', 'truck_a'}
WHEEL = 2  # an instance's drawn_only: a piece that turns about its own x as well, a vehicle's tyre (Mover.c)
FIRE_PLANT_HEAD = Mover('PakkunZHead', (TURNS, 'fire_plant_head', 1, 0))  # turns to a path, nods
AT_ORIGIN = {'PakkunZHead', 'PakkunMouth'}  # TURNS: its origin, not its lowest point, is so far over the ground (MKDS 0x2098b68)
FIRE_PLANT_MOUTH = FIRE_PLANT_HEAD._replace(model='PakkunMouth', kind=(TURNS, 'fire_plant_mouth', 1, 0))  # with it, and opens to fire
SOLID_STILL = {'dram', 'test_cylinder', 'kanoke_64'}  # stays where it is and keeps its walls
# A rim a kart rides lets it go where its surface leans further than this from level, in 4096ths of the
# up component of its normal (MKDS 0x20e04f0 against the object's +0x120): 60 degrees on Bowser Castle's
# log, 85 on Tick-Tock Clock's wheels
RIM_FALL = {'rotary_bridge': 0x800, 'test_cylinder': 0x165}
PENDULUM_BOB = (100, 100, 50), (0, -500, 0)  # half its hit box and where it is: only the weight hits
# Hangs or flies where it is placed, by its own origin
FLYING = {'chandelier', 'picture1', 'picture2', 'mkd_ef_bubble','FireBall', 'kamome', 'airship', 'sun', 'NsKiller2', 'mkd_ef_burner', 'teresa', 'basabasa', 'bakubaku', 'pukupuku', 'fireball2', 'kanoke_64', 'second_hand', 'pendulum', 'test_cylinder', 'gear_black', 'gear_white', 'rotary_room', 'rotary_bridge', 'flipper', 'dram', 'RainStar'}
HIT_SHARE = {'fireball2': 0.5}  # of its box: a ball is round
ROUND_SHARE = 0.7  # the box of what is round or a picture
GHOSTS = 6  # Boos a group shows at most (MKDS 0x13d)
STAKE_LINKS, LINK_SAG = 7, 16  # a staked Chain Chomp's chain: links, and world units its middle sags
FLIPPER_REST, FLIPPER_STROKE, FLIPPER_WAIT = 512, 660, 110  # 45 degrees; 58 degrees out in 29 ticks and back; ticks between, on average
PIECE_MOST = 1900  # level units: the largest a model may be drawn, all of it at once (2040 is where it saturates)
PIECE_CELL = 1600  # level units: how big a piece of a larger one is
FLAT_SHARE = 0.11  # a model this thin for its size is a picture: MKDS turns it to the camera
FLAT_THIN = 0.5  # and thinner than this against its narrower side too


def is_flat(points):
    """Whether a model's points lie in its z = constant plane, near enough: a picture, not a body. A
    picture is thin against both its sides; a pole is thin against its length alone (Bowser Castle's
    log, 160 by 160 by 2150, is a body)."""
    extent = np.ptp(np.array(points), axis=0)
    return bool(extent[2] < FLAT_SHARE * max(extent[0], extent[1]) and extent[2] < FLAT_THIN * min(extent[0], extent[1]))


def image_of(material):
    """A material's picture as RGBA, every texel solid or clear; a plain one for a colour alone."""
    if material.tex:
        out = np.array(material.tex0.decode(material.tex, material.pal), np.uint8)
    else:
        out = np.full((4, 4, 4), 255, np.uint8)
    out[..., 3] = np.where(out[..., 3] > 127, 255, 0)
    return out


def soft_layers(triangles, materials):
    """A flat picture's materials with soft edges, when it lays cutouts over them: (the soft ones, their
    textures), or None."""
    used = sorted({mi for mi, _ in triangles})
    points = np.array([v[0] for _, tri in triangles for v in tri])
    if len(used) < 2 or np.ptp(points[:, 2]) != 0 or any(not materials[mi].tex for mi in used):
        return None
    textures = {mi: materials[mi].tex0.decode(materials[mi].tex, materials[mi].pal) for mi in used}
    soft = {mi for mi, image in textures.items() if np.any((image[..., 3] > 0) & (image[..., 3] < 255))}
    if not soft or len(soft) == len(used):
        return None
    return soft, textures


def lift_cutouts(triangles, materials):
    """An animated picture's layers move apart, so they cannot be baked into one (composite_billboard): its
    cutouts are lifted towards the camera instead, in every frame, so they draw over its soft effects."""
    layers = soft_layers(triangles, materials)
    if layers is None:
        return triangles
    lift = np.array([0.0, 0.0, CUTOUT_LIFT])
    return [(mi, tri if mi in layers[0] else [((*(np.asarray(v[0]) + lift),), *v[1:4], np.asarray(v[4]) + lift) for v in tri]) for mi, tri in triangles]


def composite_billboard(triangles, materials):
    """Bake coplanar cutouts over their soft effects so both draw paths see the same layer order."""
    layers = soft_layers(triangles, materials)
    if layers is None:
        return None
    soft, textures = layers
    points = np.array([v[0] for _, tri in triangles for v in tri])

    low, high = points.min(0), points.max(0)
    size = LAYERED_BILLBOARD_SIZE
    yy, xx = np.mgrid[:size, :size]
    grid = np.stack((low[0] + (xx + .5) / size * (high[0] - low[0]),
                     high[1] - (yy + .5) / size * (high[1] - low[1])), axis=-1)
    image = np.zeros((size, size, 4), dtype=np.uint8)
    winding = 0
    for mi, tri in sorted(triangles, key=lambda item: item[0] not in soft):
        positions = np.array([v[0][:2] for v in tri])
        edges = positions[1:] - positions[0]
        det = np.linalg.det(edges)
        if abs(det) < 1e-9:
            continue
        if winding and np.sign(det) != winding:
            return None  # keep the original faces if the layers have different front sides
        winding = np.sign(det)
        weights = (grid - positions[0]) @ np.linalg.inv(edges)
        weights = np.dstack((1 - weights.sum(axis=-1), weights))
        inside = (weights >= -1e-9).all(axis=-1)
        mat, texture = materials[mi], textures[mi]
        sx, sy, tx, ty = mat.texmtx
        uv = (weights[inside] @ np.array([v[1] for v in tri])) * (sx / mat.width, sy / mat.height) - (tx, ty)
        for axis in range(2):
            if mat.teximage & (1 << (16 + axis)):
                tile = np.floor(uv[:, axis])
                uv[:, axis] -= tile
                if mat.teximage & (1 << (18 + axis)):
                    uv[:, axis] = np.where(tile % 2 == 1, 1 - uv[:, axis], uv[:, axis])
        w, h = texture.shape[1], texture.shape[0]
        u, v = (np.clip(uv, 0, 1) * (w, h)).astype(int).T
        texels = texture[np.minimum(v, h - 1), np.minimum(u, w - 1)].copy()
        lit = weights[inside] @ np.array([corner[2] if corner[2] is not None else (255, 255, 255) for corner in tri])
        texels[:, :3] = np.round(texels[:, :3] * lit / 255).clip(0, 255)
        covered = texels[:, 3] > 127
        texels[:, 3] = 255
        y, x = np.nonzero(inside)
        image[y[covered], x[covered]] = texels[covered]

    z = low[2]
    corners = [((x, y, z), (u, v), (255, 255, 255), None)
               for x, y, u, v in ((low[0], low[1], 0, size), (high[0], low[1], size, size),
                                  (high[0], high[1], size, 0), (low[0], high[1], 0, 0))]
    if winding < 0:
        corners.reverse()
    material = copy.copy(materials[used[0]])
    material.width = material.height = size
    material.teximage = 0
    material.texmtx = (1, 1, 0, 0)
    return [(0, corners[:3]), (0, [corners[0], corners[2], corners[3]])], [material], image


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


def _between(a, b, t):
    """The vertex `t` of the way along an edge: place, texture coordinate, colour and normal."""
    def mix(p, q):
        return None if p is None or q is None else tuple(np.array(p, float) + (np.array(q, float) - np.array(p, float)) * t)
    out = (mix(a[0], b[0]), mix(a[1], b[1]), mix(a[2], b[2]), mix(a[3], b[3]))
    if len(a) > 4:  # its place in each frame of an animation
        out += (np.asarray(a[4], float) + (np.asarray(b[4], float) - np.asarray(a[4], float)) * t,)
    return out


def _midpoint(a, b):
    return _between(a, b, 0.5)


def cut_at_cells(triangles, low, cell):
    """Triangles cut along the planes between a grid's cells (low: its corner, cell: its size each way,
    in the triangles' own units), so each lies in one cell, then fanned back into triangles. The game
    draws a polygon close by clipped at the near plane and PGXP maps its texture with perspective, so
    nothing needs them smaller: cut into small ones regardless, Bowser Castle's log was 10,000 polygons."""
    out = []
    for mi, tri in triangles:
        polygons = [list(tri)]
        for axis in range(3):
            done = []
            for polygon in polygons:
                values = [v[0][axis] for v in polygon]
                first = math.floor((min(values) - low[axis]) / cell[axis]) + 1
                last = math.ceil((max(values) - low[axis]) / cell[axis]) - 1
                for k in range(first, last + 1):
                    plane = low[axis] + k * cell[axis]
                    below, above = [], []
                    for i, a in enumerate(polygon):
                        b = polygon[(i + 1) % len(polygon)]
                        da, db = a[0][axis] - plane, b[0][axis] - plane
                        (below if da < 0 else above).append(a)
                        if (da < 0) != (db < 0):
                            v = _between(a, b, da / (da - db))
                            below.append(v)
                            above.append(v)
                    if len(below) >= 3:
                        done.append(below)
                    polygon = above
                    if len(polygon) < 3:
                        break
                if len(polygon) >= 3:
                    done.append(polygon)
            polygons = done
        for polygon in polygons:
            out += [(mi, [polygon[0], polygon[i], polygon[i + 1]]) for i in range(1, len(polygon) - 1)]
    return out


def fit_tiles(triangles, materials):
    """Triangles cut in two across the edge that crosses the most repeats of their texture until each
    spans no more repeats than the model's image holds of it at full size (MAX_WIDTH by MAX_HEIGHT, at
    most MAX_TILES): past that, its corners are clamped to the image's edge and the texture smears into
    stripes (Bowser Castle's log repeats its rivets 12 times along each face)."""
    def reach(material):
        return [max(1, min(MAX_TILES, most // max(size, 1)) - 1)
                for most, size in ((MAX_WIDTH, material.width), (MAX_HEIGHT, material.height))]

    out, todo = [], list(triangles)
    while todo:
        mi, tri = todo.pop()
        material = materials[mi]
        repeats = ((material.teximage >> 16) & 1, (material.teximage >> 17) & 1)
        if not material.tex or not any(repeats):
            out.append((mi, tri))
            continue
        sx, sy, _, _ = material.texmtx
        uv = np.array([v[1] for v in tri], float) * (sx, sy) / (max(material.width, 1), max(material.height, 1))
        limit = reach(material)
        over = [repeats[a] and np.ptp(uv[:, a]) > limit[a] + UV_SLACK for a in (0, 1)]
        if not any(over):
            out.append((mi, tri))
            continue
        axis = 0 if over[0] and (not over[1] or np.ptp(uv[:, 0]) / limit[0] >= np.ptp(uv[:, 1]) / limit[1]) else 1
        k = int(np.argmax([abs(uv[(e + 1) % 3, axis] - uv[e, axis]) for e in range(3)]))
        a, b, c = tri[k], tri[(k + 1) % 3], tri[(k + 2) % 3]
        m = _midpoint(a, b)
        todo += [(mi, [a, m, c]), (mi, [m, b, c])]
    return out


def chunks(triangles, lev_per_unit):
    """A model too big for one instance, in pieces of it: the game holds a model's scale in 16 bits and
    multiplies it by the instance's, so a model over PIECE_MOST level units is drawn squashed, or not at
    all. (Retail draws a model near the camera four times larger, which no piece could take; the renderer
    keeps movers at their size, RenderBucket_NearScale.) -> lists of triangles, one per piece; one list
    when it fits."""
    points = np.array([v[0] for _, tri in triangles for v in tri], float) * lev_per_unit
    if np.ptp(points, axis=0).max() <= PIECE_MOST:
        return [triangles]
    unit = lev_per_unit
    low, extent = points.min(0), np.ptp(points, axis=0)
    cell = extent / np.maximum(np.ceil(extent / PIECE_CELL), 1) + 1e-6  # as few pieces as fit, of one size each way
    cut = cut_at_cells(triangles, low / unit, cell / unit)
    cells = {}
    for mi, tri in cut:
        middle = np.mean([v[0] for v in tri], axis=0) * unit
        cells.setdefault(tuple(np.floor((middle - low) / cell).astype(int)), []).append((mi, tri))
    return [cells[k] for k in sorted(cells)]


def build_model(name, triangles, materials, lev_per_unit, mirror=False, billboard=False, two_sided=False, pattern=None):
    """A CTR model of an MKDS model. triangles: (material index, [(pos, uv, colour, normal) * 3]) in MKDS
    units; an animated one's vertices carry a fifth item, their place in each of its frames (animate),
    an array of frames by x, y, z. two_sided: seen from behind too. pattern: (nitro3d.TexturePattern, the
    frame it shows each tick of its loop, or None for a frame a key) for one whose textures change. -> (the
    pieces, each (ctr_items.ItemModel, where its origin is from the object's, in level units at scale 1,
    what an instance's scale takes on, as a model scale is 16 bits), half its size in level units as x, y,
    z, the middle of it, and its lowest corner in level units). The pieces share their textures."""
    animated = len(triangles[0][1][0]) > 4
    composite = composite_billboard(triangles, materials) if billboard and not animated and pattern is None else None
    if billboard and (animated or pattern is not None):
        triangles = lift_cutouts(triangles, materials)
    if composite is not None:
        triangles, materials, composite_image = composite
    if two_sided:
        triangles = triangles + [(mi, tri[::-1]) for mi, tri in triangles]
    if mirror:
        flip = np.array([-1.0, 1.0, 1.0])
        triangles = [(mi, [((-v[0][0], v[0][1], v[0][2]), *v[1:4], *([np.asarray(v[4]) * flip] if animated else [])) for v in tri]) for mi, tri in triangles]
    triangles = fit_tiles(triangles, materials)
    points =np.array([v[0] for _, tri in triangles for v in tri], float) * lev_per_unit
    low, high = points.min(0), points.max(0)
    extent = np.maximum(high - low, 1.0)
    pieces = [triangles] if billboard else chunks(triangles, lev_per_unit)
    if animated and len(pieces) > 1:
        raise ValueError(f'mover model {name}: an animated model must fit one piece')
    turn = (0, 1, 2) if mirror else (0, 2, 1)  # the game's front faces wind the other way round

    def folded_uv(mi, tri):
        material = materials[mi]
        sx, sy, tx, ty = material.texmtx
        uv = np.array([tri[c][1] for c in turn], float) * (sx, sy) / (max(material.width, 1), max(material.height, 1)) - (tx, ty)  # its offset is in whole textures
        repeats, flips = ((material.teximage >> 16) & 1, (material.teximage >> 17) & 1), ((material.teximage >> 18) & 1, (material.teximage >> 19) & 1)
        return np.stack([fold(uv[:, axis], repeats[axis], flips[axis]) for axis in (0, 1)], 1)

    every = [t for piece in pieces for t in piece]
    used = sorted({mi for mi, _ in every})
    tiles = {mi: [1, 1] for mi in used}
    for mi, tri in every:
        uv = folded_uv(mi, tri)
        for axis in (0, 1):
            tiles[mi][axis] = min(max(tiles[mi][axis], math.ceil(uv[:, axis].max() - UV_SLACK)), MAX_TILES)
    def fitted(mi):
        material = materials[mi]
        whole = composite_image if composite is not None else tiled(
            image_of(material), tiles[mi], ((material.teximage >> 18) & 1, (material.teximage >> 19) & 1))
        return whole[::math.ceil(whole.shape[0] / MAX_HEIGHT), ::math.ceil(whole.shape[1] / MAX_WIDTH)]

    images = {mi: fitted(mi) for mi in used}
    steps = None
    if pattern is not None:
        # a step is one set of textures it shows, each maybe mirrored: those of the frames its loop
        # shows, or of every frame for keys
        texture_pattern, frames = pattern
        frames = [f if isinstance(f, tuple) else (f, False) for f in frames] if frames is not None else None
        shown = sorted(set(frames)) if frames is not None else [(f, False) for f in range(texture_pattern.frames)]
        sets = [(tuple(texture_pattern.at(materials[mi].name, f) for mi in used), mirrored) for f, mirrored in shown]
        order = sets if frames is None else list(dict.fromkeys(sets))
        step_of = {f: order.index(t) for f, t in zip(shown, sets)}
        steps = {'ticks': [step_of[f] for f in frames] if frames is not None else [], 'images': []}
        rest = [(materials[mi].tex, materials[mi].pal) for mi in used]
        for chosen, mirrored in order:
            step_images = {}
            for mi, names in zip(used, chosen):
                if names is None and not mirrored:
                    step_images[mi] = images[mi]
                    continue
                if names is not None:
                    materials[mi].tex, materials[mi].pal = names
                step_images[mi] = fitted(mi)[:, ::-1] if mirrored else fitted(mi)
                materials[mi].tex, materials[mi].pal = rest[used.index(mi)]
                if step_images[mi].shape != images[mi].shape:
                    raise ValueError(f'mover model {name}: a pattern texture is not the size of the one it stands for')
            steps['images'].append(step_images)
        for mi, (tex, pal) in zip(used, rest):
            materials[mi].tex, materials[mi].pal = tex, pal

    built, step = [], None
    if len(pieces) > 1:
        # The pieces share one grid, a whole number of level units a step each way, with their origins on
        # it: a corner two pieces share is then the same point in both. Each on a grid of its own, they
        # rounded it apart by up to half a step, and the course showed through the seams.
        sides = [np.ptp(np.array([v[0] for _, tri in piece for v in tri], float) * lev_per_unit, axis=0) for piece in pieces]
        step = np.maximum(np.ceil(np.max(sides, axis=0) / (255 - 2)), 1)
        anchor = np.floor(low)
    for piece in pieces:
        piece_points = np.array([v[0] for _, tri in piece for v in tri], float) * lev_per_unit
        if step is None:
            centre = np.zeros(3)
        else:
            centre = anchor + np.round(((piece_points.min(0) + piece_points.max(0)) / 2 - anchor) / step) * step
        frames = None
        if animated:
            frames = np.array([v[4] for _, tri in piece for v in tri], float).transpose(1, 0, 2) * lev_per_unit - centre
        built.append((_piece_model(name, piece, piece_points - centre, materials, images, tiles, turn, billboard, folded_uv, step, frames, steps), centre))
    return [(model, centre, up) for (model, up), centre in built], extent / 2, (low + high) / 2, low


def _piece_model(name, triangles, points, materials, images, tiles, turn, billboard, folded_uv, step=None, frames=None, steps=None):
    """One instance's model. points: its vertices in level units from its own origin. step: the level units
    a stored step is each way, whole numbers its origin is a multiple of (build_model); None fits the piece.
    frames: an animation's frames, each its vertices as points are (animate). steps: a texture pattern's
    (build_model): a table of layouts a step, one after another, each step's textures its own, and the
    pattern, which the header's word 0x10 points to (Mover.c's MoverPattern). -> (ctr_items.ItemModel, what an instance's scale
    takes on)."""
    stored_frames = []
    if step is None:
        every = points if frames is None else np.concatenate([points, *frames])  # one scale and origin fit every frame
        low, high = every.min(0), every.max(0)
        extent = np.maximum(high - low, 1.0)
        scale_up = max(1, math.ceil(extent.max() * SCALE_ONE / 255 / MAX_MODEL_SCALE))
        scale = np.ceil(extent / scale_up * SCALE_ONE / 255).astype(int)
        origin = np.floor(every.min(0) / scale_up * SCALE_ONE / scale).astype(int)

        def store(p):
            return np.clip(np.round(p / scale_up * SCALE_ONE / scale) - origin, 0, 255).astype(int)
        stored = store(points)
        stored_frames = [store(f) for f in frames] if frames is not None else []
    else:
        scale_up = 1
        while (step * SCALE_ONE / scale_up).max() > MAX_MODEL_SCALE:
            scale_up *= 2  # a power of two divides step * SCALE_ONE, so the scale stays whole
        scale = (step * SCALE_ONE / scale_up).astype(int)
        grid = np.round(points / step).astype(int)
        origin = grid.min(0)
        stored = grid - origin
        if stored.max() > 255:
            raise ValueError(f'mover model {name}: a piece is over 255 steps of its grid')

    colours = []
    for mi, tri in triangles:
        white = np.array([255.0, 255.0, 255.0])
        colours += [np.array(v[2]) if v[2] is not None else white for v in tri]
    indexes, table = _colour_table(colours)
    while len(table) % 4:
        table.append(0)

    used = sorted({mi for mi, _ in triangles})
    layouts, commands, order, crowded = {}, [], [], []
    for n, (mi, tri) in enumerate(triangles):
        image = images[mi]
        h, w = image.shape[:2]
        uv = folded_uv(mi, tri) / tiles[mi]
        texels = np.clip(np.round(uv * (w, h)), 0, (w - 1, h - 1)).astype(int)
        key = (mi, *texels.ravel().tolist())
        if key not in layouts:
            if len(layouts) == MAX_LAYOUTS:
                if not crowded:
                    print(f'mover model {name}: over {MAX_LAYOUTS} texture layouts, the rest share the nearest')
                    crowded.append(1)
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
    step_images = steps['images'] if steps is not None else [images]
    blob += bytes(4 * len(layouts) * len(step_images))
    groups = {(mi, n): {'layouts': [], 'u': 0, 'v': 0, 'image': these[mi]} for n, these in enumerate(step_images) for mi in used}
    for n, these in enumerate(step_images):
        for key, index in layouts.items():
            slot = table_at + 4 * (len(layouts) * n + index - 1)
            if n and these[key[0]] is step_images[0][key[0]]:
                struct.pack_into('<I', blob, slot, struct.unpack_from('<I', blob, table_at + 4 * (index - 1))[0])  # the pattern leaves it alone
            else:
                at = len(blob)
                t = key[1:]
                blob += struct.pack('<BBHBBHBBBB', t[0], t[1], 0, t[2], t[3], OPAQUE_PAGE, t[4], t[5], t[4], t[5])
                struct.pack_into('<I', blob, slot, at)
                groups[(key[0], n)]['layouts'].append(at)
            pointers.append(slot)
    if steps is not None:
        field(0x10)
        ticks = steps['ticks']
        blob += struct.pack('<HH', len(layouts), len(ticks)) + bytes(ticks)
        while len(blob) % 4:
            blob.append(0)
    if stored_frames:
        # One animation of uncompressed frames, all with the rest frame's commands. Its count is of half
        # frames, flagged: the renderer eases between two stored frames on an odd one, at 30 and 60 FPS
        # alike, and a half frame is one of MKDS's ticks (animate).
        struct.pack_into('<I', blob, head + 0x34, 1)
        field(0x38)
        table_at = len(blob)
        blob += bytes(4)
        struct.pack_into('<I', blob, table_at, len(blob))
        pointers.append(table_at)
        frame_size = FRAME_SIZE + len(vertices)
        blob += blob[:16] + struct.pack('<HhI', HALF_FRAMES | (2 * len(stored_frames) - 1), frame_size, 0)
        for frame_stored in stored_frames:
            frame = bytearray(FRAME_SIZE)
            struct.pack_into('<3h', frame, 0, int(origin[0]), int(origin[1]), int(origin[2]))
            struct.pack_into('<I', frame, 0x18, FRAME_SIZE)
            these = bytearray()
            for i in order:
                x, y, z = frame_stored[i]
                these += bytes((x, z, y))
            while len(these) % 4:
                these.append(0)
            blob += frame + these
    built = ctr_items.ItemModel(blob, sorted(pointers), [g for g in groups.values() if g['layouts']])
    built.loop = 2 * len(stored_frames) - 1 if stored_frames else 0  # ticks
    return built, scale_up


def records(kind, speed, damage_flags, half, middle, base, mode_args, points):
    """The thread's list, as rows of three 16-bit values (see game/ConvertedTrack/Mover.c)."""
    rows = [[kind, int(round(speed)), damage_flags - 0x10000 if damage_flags & 0x8000 else damage_flags], [int(round(h)) for h in half], list(mode_args)]
    rows += [[int(round(c * SCALE_ONE)) for c in row] for row in base]
    rows += [[int(round(c)) for c in middle]]
    rows += [[int(round(c)) for c in p] for p in points]
    return rows


def reaction(object_id):
    """(damage, flags) of a kart touching the object: REACTIONS of the game's own reaction to it."""
    return REACTIONS.get(mkds_objects.REACTION.get(object_id, 0), (0, 0))


def star(object_id, struck=None):
    """The bits of what a kart with the mask, CTR's Star, does to the object, and whether it stops that kart:
    MKDS's own column for a Star kart (mkds_objects.STAR_REACTION, STRUCK). struck: a kind in place of the id's."""
    kind = struck if struck is not None else TOUCHED if object_id in TOUCHED_BY_ANY else (STRUCK.get(object_id, 0) if object_id in mkds_objects.STRUCK else 0)
    return (kind << STRIKE_SHIFT) | (STAR_WALL if mkds_objects.STAR_REACTION.get(object_id) in (2, 12) else 0)


def key_records(track, damage, half, middle, base, flags, to_lev, lev_unit, size, loop=0):
    """A MOVER_KEYS list, from a mkds_motion.Track. half: the object's own hit box. loop: its model's
    animation's ticks (animate), which a track's anim (how far through it, a tick) plays; a track's
    pattern is the frame of its model's texture pattern a tick."""
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
    if track.damage is not None:
        damage = track.damage
    anim = track.anim if track.anim is not None and loop else None
    if anim is not None or track.pattern is not None:
        # a key's animation tick, and the frame of its texture pattern it shows (KEYED_PATTERNS)
        flags |= ANIM_KEYS
        count = len(track.ticks[::step])
        animated = [int(round(a * loop)) % loop for a in anim[::step]] if anim is not None else [0] * count
        shown = list(track.pattern[::step]) if track.pattern is not None else [0] * count
        keys += [[a, f, 0] for a, f in zip(animated, shown)]
    args = (track.restart // step, track.lap_step, track.start // step)
    return records(KEYS, KEY_MS * step // 3, damage | flags, half, middle, base, args, grow + gates + keys), track.ticks


def animate(model, animation):
    """An MKDS model's joint animation as frames for build_model: the model's triangles posed at every
    other tick of its loop, eased between by the renderer. The loop is stretched by a tick when it is even:
    a flagged count of half frames is always odd. -> (each vertex's place in each frame, an array of
    frames by x, y, z, in the order of model.triangles; the loop's length in ticks)."""
    length = animation.frames
    ticks = length if length % 2 else length + 1
    frames = [model.triangles(lit=True, pose=animation.pose(model, (2 * k * length / ticks) % length)) for k in range((ticks + 1) // 2)]
    places = np.array([[v[0] for _, tri in tris for v in tri] for tris in frames], float)
    if places.ndim != 3:
        raise ValueError(f'{model.name}: its frames draw different triangles')
    return places.transpose(1, 0, 2), ticks


def start_tick(name, mover, loop):
    """The tick of its loop a STILL animated model starts at, drawn once from its place as mkds_motion
    draws, so the copies of one model do not move as one. 0 for one with no animation."""
    if not loop:
        return 0
    first, more = START_TICK.get(name, (0, loop))
    seed = int(abs(mover['pos'][0]) * 7 + abs(mover['pos'][2]) * 13)
    return (first + random.Random(seed).randrange(more)) % loop


def instances(pieces, pos, size, scale_up_size, listed, reach, name, rot=None):
    """One instance of each piece of a model (build_model), all on one record list: the first tests the
    karts, the rest are drawn where they are from it. rot: the turn the map places it at, which it keeps
    at rest (mover_pose_check.py); None where it faces its way itself (a picture, a path, keys)."""
    out = []
    for n, (model, centre, up) in enumerate(pieces):
        out.append({'model': model, 'pos': [int(round(c)) for c in pos], 'scale': [int(round(c * SCALE_ONE * up)) for c in scale_up_size], 'list': listed,
                    'reach': reach, 'name': name, 'offset': [int(round(c)) for c in centre * size], 'drawn_only': n > 0,
                    'rot': None if rot is None else [float(r) for r in rot]})
    return out


def place(movers, model_index, halves, to_lev, lev_unit, rotate, settle, context):
    """-> (one entry per instance, the record lists). An object on a path that several share gets a start
    spaced along it; one on a centre, a phase round it. settle: a point -> that point on the ground there.
    context: what mkds_motion's objects need of the course. halves[model_index[key]]: (half its size, its
    middle, its pieces, its lowest corner, whether it is a picture, its animation's loop in ticks or 0)."""
    placed, lists = [], []
    for mover in movers:
        spec, pos, rot, size = mover['spec'], mover['pos'], mover['rot'], mover['size']
        if spec.model is None:
            base = np.eye(3)
            for track in mkds_motion.tracks(spec.kind[1], mover, context):
                listed, ticks = key_records(track, spec.damage, np.zeros(3), CRATE_MIDDLE, base, 0, to_lev, lev_unit, np.ones(3))
                at = next(p for p in ticks if p is not None)
                placed.append({'model': 'crate', 'pos': [int(round(c)) for c in to_lev(at)], 'scale': [SCALE_ONE] * 3, 'list': len(lists), 'reach': 64.0, 'name': 'crate',
                               'offset': [0, 0, 0], 'drawn_only': False})
                lists.append(listed)
            continue
        half, middle, pieces, low, flat, loop = halves[model_index[mover['key']]]
        base = np.eye(3) if flat and spec.kind[0] != ORBIT else np.array([rotate(np.array(axis, float), rot) for axis in np.eye(3)]).T
        damage, solid = reaction(mover.get('reacts', mover['id']))
        if spec.damage is not None:
            damage = spec.damage
        flags = spec.flags | (0 if spec.flags & CARRIES else solid)  # a floor a kart drives on is not a wall to push it off
        flags |= star(mover.get('reacts', mover['id']), mover.get('struck'))
        flags = flags & ~(FACE_PATH | ROLL) if flat else flags  # the game turns a picture itself
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
        if (damage == 0 and not flags & (SOLID | CARRIES)) or (known is not None and known.shape == 'none'):
            half = half * 0  # the game never tests a kart against it
        setting = mover['settings'][spec.setting]
        speed = setting * spec.factor if setting and spec.factor else spec.fixed
        kind, args = spec.kind[0], spec.kind[1:]
        if kind == TURNS:
            standing = spec.model not in FLYING
            foot = 0.0 if spec.model in AT_ORIGIN else low[1] / lev_unit * size[1]
            at = (settle(pos) if standing else pos) + np.array([0.0, mover['over'] * size[1] - (foot if standing else 0.0), 0.0])
            angles = mkds_motion.turns(args[0], mover, context)[::TURN_KEY_TICKS]
            keys = [[round(a[0] * TURN / mkds_motion.TURN) % TURN, round(a[1] * TURN / mkds_motion.TURN) % TURN, round(a[2] * loop) % loop if len(a) > 2 and loop else 0]
                    for a in angles]
            placed += instances(pieces, to_lev(at), size, size, len(lists), reach, spec.model, rot)
            lists.append(records(kind, KEY_MS * TURN_KEY_TICKS // 3, damage | flags, half, middle, base, (args[1], args[2], 0), keys))
            continue
        if kind == CHOMP:
            placed_chomp, lists_chomp = staked_chomp(mover, spec, pieces, halves[model_index[(context['chain'], False)]][2], damage | flags, half, middle, base,
                                                     reach, size, to_lev, lev_unit, len(lists))
            placed += placed_chomp
            lists += lists_chomp
            continue
        if kind == GHOST:
            unit = unit_256(lev_unit)
            for n in range(GHOSTS):
                face = halves[model_index.get(mover['key'][:2] + (n & 1,), model_index[mover['key']])][2]  # every other one shows the other face
                placed += instances(face, to_lev(pos), size, size, len(lists), reach, spec.model)
                lists.append(records(GHOST, 0, damage | flags, half, middle, base, (round(mover['settings'][0] * lev_unit), n, unit), [to_lev(pos)]))
            continue
        if kind == KEYS:
            foot = -low[1] / lev_unit * size[1]
            for n, track in enumerate(mkds_motion.tracks(args[0], mover, {**context, 'foot': foot})):
                other = track.model is not None
                own = halves[model_index[(track.model, False)]] if other else halves[model_index[mover['key']]]
                track_damage, track_flags = damage, flags
                if other and track.model in PART_REACTS:
                    track_damage, part_solid = reaction(PART_REACTS[track.model])
                    track_flags = (flags & ~(SOLID | STAR_WALL | (0xf << STRIKE_SHIFT))) | part_solid | star(PART_REACTS[track.model])
                elif other:
                    track_flags = flags & ~(0xf << STRIKE_SHIFT)  # a chain's links: the head is what is struck
                listed, ticks = key_records(track, track_damage, half, middle, base, track_flags, to_lev, lev_unit, size, own[5])
                at = next(p for p in ticks if p is not None)
                drawn = size if not other or track.hit == 0 else np.ones(3)
                placed += instances(own[2], to_lev(at), np.ones(3) if other and track.hit != 0 else size, drawn, len(lists), reach, track.model or spec.model)
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
                mode_args = (args[0], -rate if settings[4] else rate, RIM_FALL.get(spec.model, 0))  # Tick-Tock Clock's neighbours turn opposite ways
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
            elif kind == STILL:
                mode_args = (start_tick(spec.model, mover, loop), 0, 0)
                at = (settle(pos) if standing else pos) + lift
            else:
                # hoops a kart goes through: s0 balls a hoop, s3 + 1 hoops, s1 the outer radius, s2 their speed
                per_ring, rings = max(settings[0], 1), settings[3] + 1
                radius = settings[1] * ((i // per_ring) + 1) / rings
                period = int(65536 * 100 * 1000 / (max(settings[2], 1) * 364 * TICKS))
                mode_args = (int(radius * lev_unit), period, period * (i % per_ring) // per_ring)
                at = pos + lift
            placed += instances(pieces, to_lev(at), size, size, len(lists), reach, spec.model, None if flat or kind in (PATH, ORBIT) else rot)
            if mover.get('tyres') is not None:
                height, along = mover['tyres'][1]
                for z in (along, -along):
                    for tyre in instances(halves[model_index[mover['tyres'][0]]][2], to_lev(at), size, size, len(lists), reach, spec.model):
                        tyre.update(drawn_only=WHEEL, offset=[int(round(c)) for c in np.array([0.0, height, z]) * size * lev_unit])
                        placed.append(tyre)
            sway = 0
            if kind == PATH and mover.get('sway') is not None:
                part, overs = mover['sway']
                gap = [to_lev(at + np.array([0.0, (overs[j] - overs[j - 1]) * size[1], 0.0]))[1] - to_lev(at)[1] for j in (1, 2)]
                points = points + [(part, int(round(gap[0])), int(round(gap[1])))]
                sway = SWAY
            lists.append(records(kind, speed, damage | flags | sway, half, middle, base, mode_args, points))
            if spec.model == 'NsKiller1':
                parent = len(lists) - 1
                start = pos + (0.0, BULLET_UP, 0.0)
                way = rotate(np.array([0.0, 0.0, BULLET_REACH / (BULLETS)]), rot)
                flight = [to_lev(start + way * n) for n in range(BULLETS + 1)]
                bullet_half = np.array([15.0, 15.0, 15.0]) * lev_unit
                for n in range(BULLETS):
                    placed += instances(halves[model_index[mover['bullet']]][2], flight[n], np.ones(3), np.ones(3), len(lists), 200.0, BULLET.model)
                    bullet_damage, bullet_solid = reaction(BULLET_REACTS)
                    lists.append(records(PATH, BULLET.fixed, bullet_damage | BULLET.flags | bullet_solid | star(BULLET_REACTS), bullet_half, np.zeros(3), base, (RESTART, n, parent + 1), flight))
    return placed, lists


def unit_256(lev_unit):
    """Level units a world unit, times 256, as Mover.c reads it."""
    unit = int(round(lev_unit * 256))
    if not 0 < unit <= 0x7fff:
        raise ValueError(f'a world unit is {lev_unit} level units: more than a record holds')
    return unit


def staked_chomp(mover, spec, pieces, chain, damage_flags, half, middle, base, reach, size, to_lev, lev_unit, first):
    """MKDS 0x196, run by Mover.c (MOVER_CHOMP): the head's list, then its chain's links (MOVER_LINK), which
    hang from the stake's top to it. -> (instances, lists)."""
    st, sy = mover['settings'], size[1]
    stake = np.array(mover['pos'], float)
    rest, _ = mkds_motion.chomp_rest(mover)
    leash = float(st[0])
    plane = to_lev((stake[0], rest, stake[2]))
    head_args = (round(leash * lev_unit), round(st[1] * lev_unit), round((0.75 * leash - 60 * sy) * lev_unit))
    wander = (45 * lev_unit, 0.75 * leash * lev_unit, unit_256(lev_unit))
    placed = instances(pieces, to_lev((stake[0] + 60, rest, stake[2])), size, size, first, reach, spec.model)
    lists = [records(CHOMP, 0, damage_flags, half, middle, base, head_args, [plane, wander])]
    top = to_lev(stake + (0.0, 10.0, 0.0))
    links = damage_flags & ~(0xf << STRIKE_SHIFT) & ~(SOLID | STAR_WALL)  # the head is what is struck
    for link in range(STAKE_LINKS):
        share = (link + 0.5) / STAKE_LINKS
        sag = 4 * share * (1 - share) * LINK_SAG * lev_unit
        placed += instances(chain, top, size, size, first + len(lists), reach, 'wanwan_chain')
        lists.append(records(LINK, 0, links & ~0xf, np.zeros(3), np.zeros(3), base, (first, round(share * SCALE_ONE), round(sag)), [top, (plane[1], 0, 0)]))
    return placed, lists


def path_start(points, pos):
    """The point of a path nearest an object placed on it."""
    return int(np.argmin(np.linalg.norm(np.array(points) - pos, axis=1)))
