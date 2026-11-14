"""Convert a Mario Kart DS course from the player's own ROM into an installed custom track package.

  python tools/tracks/mkds_convert.py <rom.nds> cross_course --title "Figure-8 Circuit"

Steps (docs/TRACK-CONVERTER.md): the course becomes CrashTeamEditor input (OBJ + MTL + textures +
presets), cte_headless turns that into a LEV and VRM, lev_patch adds what CrashTeamEditor does not
write, and package installs the result under its pin, which is printed.

Render geometry comes from course_model.nsbmd (textured, vertex coloured, drawn but never collided).
Collision comes from course_collision.kcl (invisible quadblocks carrying terrain, walls and checkpoints).
Start, finish line and route come from course_map.nkm.
"""
import argparse, copy, json, math, os, shutil, subprocess, sys
from collections import defaultdict
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctr_items, nds, nitro3d, mkdsmap, mkds_movers, mkds_objects, mkds_view, lev_patch, package, track_picture, winding_check

PICTURE_BACK, PICTURE_UP, PICTURE_AHEAD = 220, 150, 3  # MKDS units, and points of the route
# The start camera looks into the underside of the cannon ramp on this course.
PICTURE_VIEWS = {'pinball_course': ((-21, 1471, 1681), (-323, 1221, 2465))}
# A vehicle's tyres, from its object's config (0x21589a8 car, 0x2158a18 bus, 0x21589e0 truck): the axle's
# width, its height and how far before and behind the middle it is, in world units, and its size
TYRES = {'car_a': (12.3, 4.2, 11.0, 1.0), 'bus_a': (22.2, 7.5, 31.0, 2.0), 'truck_a': (13.6, 4.7, 20.0, 1.2)}
MODEL_TO_WORLD = 16.0  # course models are drawn 16 times larger than their stored size

# Quadblock flags by the game's own names (include/namespace_Level.h). CrashTeamEditor's labels
# for these bits are misleading: its "Invisible" (0x1) is a reflection split line, "Out of Bounds"
# (0x10) is NO_COLLISION_RESPONSE, "Mask Grab" (0x200) is the kill plane, "Wall" (0x2000) is the
# collision surface and "No Collision" (0x4000) only stops the camera respawn probe.
CAMERA_SEARCH, KILL_PLANE, GROUND, COLLISION_SURFACE = 0x0800, 0x0200, 0x1000, 0x2000

# KCL type -> (material name, terrain label, quad flags, pathable). Collision blocks are drawn
# with nothing: navpatch sets their faces' draw mode to "none". Not Water or Fast Water on
# ground a kart drives on: they clip the kart's model at world height 0, as a retail level has them
# there (COLL.c), and a converted course lies at any height. Mud's same rules are off on one.
_ROAD = GROUND | CAMERA_SEARCH
# no CAMERA_SEARCH: the camera takes its height from what that flag marks, so a wall behind the kart
# would lift it; 91% of retail's walls on Crash Cove go without
_WALL = COLLISION_SURFACE
_FALL = GROUND | KILL_PLANE | CAMERA_SEARCH
# a collision triangle whose normal on the LEV grid is further than this from its source's is dropped
TURNED_COS = 0.9  # about 25 degrees
COLLISION = {
    0x00: ('col_road', 'Asphalt', _ROAD, True),
    0x01: ('col_road', 'Asphalt', _ROAD, True),
    0x06: ('col_soft', 'Mud', _ROAD, True),  # deep mud: Luigi's Mansion's bog, SNES Choco Island 2's
    0x0d: ('col_road', 'Asphalt', _ROAD, True),
    0x14: ('col_road', 'Asphalt', _ROAD, True),
    0x02: ('col_grass', 'Grass', _ROAD, True),
    0x03: ('col_offroad', 'Slow Grass', _ROAD, True),
    0x05: ('col_mud', 'Slow Dirt', _ROAD, True),
    0x07: ('col_boost', 'Asphalt', _ROAD, True),
    0x12: ('col_ramp', 'Asphalt', _ROAD, True),  # a boost ramp, the jumps: a super turbo pad
    0x0c: ('col_jump', 'Asphalt', _ROAD, True),  # a jump pad: throws a kart over the gap past it
    0x13: ('col_road', 'Asphalt', _ROAD, True),  # a loop's way in and out; what is steeper is a flight
    0x08: ('col_wall', 'Asphalt', _WALL, False),
    0x09: ('col_wall', 'Asphalt', _WALL, False),
    0x0e: ('col_wall', 'Asphalt', _WALL, False),
    0x10: ('col_wall', 'Asphalt', _WALL, False),
    0x15: ('col_wall', 'Asphalt', _WALL, False),
    0x0a: ('col_fall', 'Asphalt', _FALL, False),
    0x0b: ('col_fall', 'Asphalt', _FALL, False),
    0x11: ('col_river', 'River Asphalt', _ROAD, True),
    'mud': ('col_soft', 'Mud', _ROAD, True),  # by variant: KCL_MUD
}


# The variant (bits 5 to 7 of a KCL attribute) names the ground where its type alone does not: these
# (type, variant) are mud and dirt, for SNES Donut Plains 1's road, Wario Stadium, SNES Choco Island 2.
KCL_MUD = {(0x02, 3), (0x03, 0), (0x03, 1)}


DARK_HORIZON = 48
# Blocks' joins can round a pixel apart, and on a white sky that pixel flashes: a walled-in course's ground colour
BELOW_HORIZON = {'old_choco_64': (98, 66, 34)}
WIDE_ROUGH = 7  # mean colour error of 16 colours past which a wide texture gives up width for colours
BLEND_ALPHA = 24  # of 31: a surface MKDS draws this see-through or more blends here, half and half: the only mix a level has
ALPHA_SOLID = 200  # a level texel is solid or clear: only a texel nearly solid in MKDS is solid here
OVERLAY_MIN_ALPHA, OVERLAY_SHARE = 16, 0.02  # a decal on the floor: its texels this faint are clear, and this many of them lie between
OVERLAY_GAP, OVERLAY_LIFT = 12, 3.0  # MKDS units: how far over the floor it lies, and how far it rises off it
# (Waluigi Pinball's floor) drawn solid is hard blocks of colour
SLIVER_WIDTH = 3  # LEV units: thinner than this, the grid turns a collision triangle's pieces any way
MAX_VERTICES = 65000  # quadblocks index vertices with 16 bits; a little under, as the OBJ's count is close, not exact
MAX_COORD = 500  # CrashTeamEditor units; 512 is the LEV's 16-bit limit, and the carrier blocks sit 8 past the course
VRAM_SHARE = 0.85  # of the free texture pages, at first
MAX_FIT_ATTEMPTS = 12
BOOST_MATERIALS = {'col_boost', 'col_ramp', 'col_jump'}
LANE_FLAGS = {'col_boost': 0x1, 'col_ramp': 0x100}  # BOTS_NAV_FLAG_TURBO, BOTS_NAV_FLAG_SUPER_TURBO: a CPU kart's boost
LANE_REACHES_RAMP = 180  # MKDS units over the route a ramp may rise
LANE_SAMPLES = 6  # a pad is shorter than two nodes are apart
LANE_CLEAR_PASSES = 4
LANE_LAUNCHES = {'col_ramp': 0.2, 'col_jump': 0.6}  # the least rise per unit forward, off these
KART_SPEED, KART_GRAVITY = 340, 618  # MKDS units a second, and a second squared: a kart's, off a ramp
TRIGGER_TURBO_PAD, TRIGGER_SUPER_TURBO_PAD = 1, 2  # CrashTeamEditor's QuadblockTrigger
PRESET_TURBO_PAD = 4  # CrashTeamEditor's PresetHeader
# LEV units from the ground up to a crate or fruit instance's origin (retail tracks: 0 to 8)
CRATE_LIFT = 8
# Wumpa fruit: rows along the CPU route, as retail lays them. LEV units.
FRUIT_PER_ROW, FRUIT_SPACING = 4, 230
FRUIT_ROW_GAP = 13000  # along the route, from one row to the next
FRUIT_CLEAR_OF_START = 800  # no row this close to the finish line, either side
FRUIT_CLEAR_OF_CRATES = 500
FRUIT_ROAD = {'col_road', 'col_boost', 'col_ramp'}
# Flights (docs/TRACK-CONVERTER.md#flights). MKDS units, but speeds in LEV units a second.
KCL_CANNON, KCL_LOOP = 0x0f, 0x13
LOOP_ROAD = 0.7  # a loop's triangle facing up this much a kart drives on; steeper, it rides a flight
LOOP_LEVEL = 0.9  # a loop's flight runs on until its road faces up this much
LOOP_REACH = 170  # how far from a loop's road its route may run
FLIGHT_GAP = 600  # a stretch of route over nothing at all, longer than a kart jumps
FLIGHT_STEP = 100  # ground rising this much at once, which only a moving floor climbs in MKDS
CANNON_SPEED, GAP_SPEED, STEP_SPEED = 6000, 4000, 2500
CANNON_ARCS = (0.06, 0.03, 0.0, -0.03, 0.1, 0.15)  # rise at the middle over its length, the likeliest first
FLIGHT_SAMPLE = 16  # along the route
FLIGHT_POINT = 48  # from one point of a flight's path to the next
FLIGHT_LAND = 24  # above the ground, where a flight ends
# MKDS units: a route point may be this far under the road, and this far above it before it is a jump
ROUTE_ABOVE_GROUND, ROUTE_IN_THE_AIR = 20, 60
ROUTE_UNDER_BUMP = 80  # how far the top of a bump can stand above the route's line, MKDS units


# The minimap, in screen pixels. The game draws it with its bottom right corner here (UI_RenderFrame).
MAP_CORNER_X, MAP_CORNER_Y = 500, 195
MAP_MAX_WIDTH, MAP_MAX_HEIGHT = 96, 96  # the largest retail's maps reach
MAP_PIXEL_ASPECT = 1.5  # screen pixels across per pixel down for the same distance, as retail's maps
# UIMap::mode -> (x, z) of what is up on the map, as UI_Map_GetIconPos draws it
MAP_UP = ((0, -1), (-1, 0), (0, 1), (1, 0))
MAP_ICON_X, MAP_ICON_Y = 4, 2  # a kart icon's top left corner from the point it marks
MAP_ROAD = {'col_road', 'col_boost', 'col_ramp', 'col_jump'}
MAP_CORRIDOR, MAP_TOO_MUCH = 1.1, 1.5  # of a route point's radius; of the corridor's area
MAP_FILL, MAP_EDGE = (66, 66, 66, 255), (255, 255, 255, 255)  # lev_patch sets the fill to blend


# ---------------------------------------------------------------- geometry helpers

def lerp(a, b, t):
    return {k: a[k] + (b[k] - a[k]) * t for k in a}


def clip(poly, key, axis, value, keep_above):
    """Sutherland-Hodgman clip of a polygon of vertex dicts against vertex[key][axis] >= / <= value."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        da, db = a[key][axis] - value, b[key][axis] - value
        if not keep_above:
            da, db = -da, -db
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            v = {k: a[k] + (b[k] - a[k]) * t for k in a}
            out.append(v)
    return out


def split_by_tiles(tri, key='uv'):
    """Cut a triangle so every piece lies inside one integer UV tile. -> list of polygons."""
    pieces = [tri]
    for axis in (0, 1):
        out = []
        for poly in pieces:
            vals = [v[key][axis] for v in poly]
            lo, hi = math.floor(min(vals) + 1e-6), math.ceil(max(vals) - 1e-6)
            rest = poly
            for cut in range(lo + 1, hi):
                below = clip(rest, key, axis, cut, False)
                rest = clip(rest, key, axis, cut, True)
                if len(below) >= 3:
                    out.append(below)
            if len(rest) >= 3:
                out.append(rest)
        pieces = out
    return pieces


def close_tjunctions(items, tol):
    """Where a vertex lies on (within tol of) another triangle's edge without being one of its
    corners, insert it into that edge and fan the triangle from its centroid, so the two meet
    exactly. MKDS collision has such joins; MKDS's thick, radius-based collision hides the
    sliver they leave, CTR's plane-and-triangle test does not."""
    verts = {}
    for tri, _ in items:
        for v in tri:
            verts.setdefault(tuple(v['pos']), v['pos'])
    cell = max(tol * 8, 1e-6)
    grid = {}
    for k, p in verts.items():
        grid.setdefault(tuple(np.floor(p / cell).astype(int)), []).append(p)
    def on_edge(a, b):
        lo, hi = np.minimum(a, b) - tol, np.maximum(a, b) + tol
        found = []
        for cx in range(int(np.floor(lo[0] / cell)), int(np.floor(hi[0] / cell)) + 1):
            for cy in range(int(np.floor(lo[1] / cell)), int(np.floor(hi[1] / cell)) + 1):
                for cz in range(int(np.floor(lo[2] / cell)), int(np.floor(hi[2] / cell)) + 1):
                    for p in grid.get((cx, cy, cz), ()):
                        ab = b - a
                        t = np.dot(p - a, ab) / np.dot(ab, ab)
                        if 1e-6 < t < 1 - 1e-6 and np.linalg.norm(a + ab * t - p) <= tol:
                            found.append((t, p))
        return sorted(found, key=lambda x: x[0])
    out = []
    for tri, meta in items:
        poly = []
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            poly.append(a)
            for t, p in on_edge(a['pos'], b['pos']):
                v = {k: a[k] + (b[k] - a[k]) * t for k in a}
                v['pos'] = p
                poly.append(v)
        if len(poly) == 3:
            out.append((tri, meta))
            continue
        # points on one edge only: fan from the opposite corner (no slivers); else from the centroid
        corners = [next(j for j, w in enumerate(poly) if w is v) for v in tri]
        extra_edges = [i for i in range(3) if (corners[(i + 1) % 3] - corners[i]) % len(poly) > 1]
        if len(extra_edges) == 1:
            apex = corners[(extra_edges[0] + 2) % 3]
            ring = poly[apex + 1:] + poly[:apex]
            for i in range(len(ring) - 1):
                out.append(([poly[apex], ring[i], ring[i + 1]], meta))
            continue
        c = {k: sum(v[k] for v in tri) / 3 for k in tri[0]}
        for i in range(len(poly)):
            out.append(([poly[i], poly[(i + 1) % len(poly)], c], meta))
    return out


def overlap_tjunctions(items, tol, push):
    """Where a vertex lies on (within tol of) an open edge of another triangle, move it across that
    edge by push, into the triangle. Both sides are rounded to the LEV grid, so a corner on a neighbour's
    long edge leaves a hairline of sky; the surfaces overlap instead, which a depth-tested level does not
    show. Every triangle with that vertex moves with it, so nothing opens, and no block is added."""
    edges = defaultdict(list)
    for ti, (tri, _) in enumerate(items):
        for i in range(3):
            edges[frozenset((tuple(tri[i]['pos']), tuple(tri[(i + 1) % 3]['pos'])))].append(ti)
    open_edges = [(tuple(e), tis[0]) for e, tis in edges.items() if len(tis) == 1 and len(e) == 2]
    # only corners of open edges can leave a gap
    cell = 2.0
    grid = defaultdict(list)
    for (a, b), _ in open_edges:
        for p in (a, b):
            c = tuple(np.floor(np.array(p) / cell).astype(int).tolist())
            if p not in grid[c]:
                grid[c].append(p)
    moved = {}
    for (a, b), ti in open_edges:
        a, b = np.array(a), np.array(b)
        ab = b - a
        length2 = np.dot(ab, ab)
        tri = [v['pos'] for v in items[ti][0]]
        inward = sum(tri) / 3 - a
        inward -= ab * (np.dot(inward, ab) / length2)
        if np.linalg.norm(inward) < 1e-9:
            continue
        inward /= np.linalg.norm(inward)
        steps = int(math.sqrt(length2) / cell) + 1
        cells = set()
        for i in range(steps + 1):
            cx, cy, cz = np.floor((a + ab * (i / steps)) / cell).astype(int).tolist()
            cells.update((cx + dx, cy + dy, cz + dz) for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1))
        for c in cells:
            for key in grid.get(c, ()):
                p = np.array(key)
                t = np.dot(p - a, ab) / length2
                foot = a + ab * t
                if 1e-6 < t < 1 - 1e-6 and np.linalg.norm(foot - p) <= tol and key not in moved:
                    moved[key] = foot + inward * push
    out = []
    for tri, meta in items:
        out.append(([dict(v, pos=moved.get(tuple(v['pos']), v['pos'])) for v in tri], meta))
    return out, len(moved)


def refine(items, max_edge, weld_tol, snap=4, tjunction_tol=0.0):
    """Conforming refinement: split every edge longer than max_edge (a function of meta, or a number) at its midpoint, in every
    triangle that has it, until none is left. Neighbours always split a shared edge at the same
    point, so no T-junction (and no crack the game can fall through) appears.
    items: list of (triangle of 3 vertex dicts with 'pos', meta). Positions are snapped first so
    vertices the source shares only approximately become exactly shared."""
    def key(v):
        return tuple(v['pos'])
    def mid(a, b):
        m = {k: (a[k] + b[k]) / 2 for k in a}
        m['pos'] = np.round(m['pos'], snap)
        return m
    # weld: every position within weld_tol of an earlier one becomes that one exactly
    cell = weld_tol * 4
    reps = {}
    def weld(p):
        c = tuple(np.floor(p / cell).astype(int))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for r in reps.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                        if np.abs(r - p).max() <= weld_tol:
                            return r
        reps.setdefault(c, []).append(p)
        return p
    def snapv(v):
        v = dict(v); v['pos'] = weld(np.round(np.asarray(v['pos'], float), snap)); return v
    items = [([snapv(v) for v in tri], meta) for tri, meta in items]
    if tjunction_tol:
        items = close_tjunctions(items, tjunction_tol)
    while True:
        long = set()
        for tri, meta in items:
            for i in range(3):
                a, b = tri[i], tri[(i + 1) % 3]
                if np.linalg.norm(a['pos'] - b['pos']) > (max_edge(meta) if callable(max_edge) else max_edge):
                    long.add(frozenset((key(a), key(b))))
        if not long:
            return items
        out = []
        for tri, meta in items:
            marks = [frozenset((key(tri[i]), key(tri[(i + 1) % 3]))) in long for i in range(3)]
            n = sum(marks)
            if n == 0:
                out.append((tri, meta)); continue
            if n == 3:
                a, b, c = tri
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                out += [([a, ab, ca], meta), ([ab, b, bc], meta), ([ca, bc, c], meta), ([ab, bc, ca], meta)]
                continue
            if n == 1:
                i = marks.index(True)
                p, q, r = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
                m = mid(p, q)
                out += [([p, m, r], meta), ([m, q, r], meta)]
                continue
            i = next(i for i in range(3) if marks[i] and marks[(i + 1) % 3])
            p, q, r = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
            m1, m2 = mid(p, q), mid(q, r)
            out += [([m1, q, m2], meta), ([p, m1, m2], meta), ([p, m2, r], meta)]
        items = out


def close_slivers(items, width):
    """Remove triangles thinner than width without opening a crack. Such a sliver has a corner almost on
    its longest edge: every neighbour across that edge is split at this corner instead, so the sliver's
    two short edges take its place in the mesh. Dropping it would leave a strip with no ground, a kart's
    probe falling through it. items: as refine's, on the LEV grid. -> (items, slivers removed)."""
    def k(v):
        return tuple(np.round(np.asarray(v['pos']) * 64).astype(int))
    items = [item for item in items if len({k(v) for v in item[0]}) == 3]  # welded to a line
    removed = 0
    for _ in range(16):  # splitting a neighbour can leave it a sliver in turn
        edges = defaultdict(list)
        for i, (t, _meta) in enumerate(items):
            for e in range(3):
                edges[frozenset((k(t[e]), k(t[(e + 1) % 3])))].append((i, e))
        touched, gone, added = set(), set(), []
        for i, (t, _meta) in enumerate(items):
            if i in touched:
                continue
            P = [np.asarray(v['pos'], float) for v in t]
            e = max(range(3), key=lambda e: np.linalg.norm(P[(e + 1) % 3] - P[e]))
            base = np.linalg.norm(P[(e + 1) % 3] - P[e])
            if base == 0 or np.linalg.norm(np.cross(P[1] - P[0], P[2] - P[0])) / base >= width:
                continue
            apex = t[(e + 2) % 3]
            others = [(j, f) for j, f in edges[frozenset((k(t[e]), k(t[(e + 1) % 3])))] if j != i]
            if i in touched or any(j in touched for j, _f in others):
                continue  # next pass: its neighbours have changed
            touched.add(i)
            gone.add(i)
            removed += 1
            for j, f in others:
                touched.add(j)
                gone.add(j)
                n, meta = items[j]
                p, q, r = n[f], n[(f + 1) % 3], n[(f + 2) % 3]
                if k(apex) != k(r):  # two slivers back to back close each other
                    added += [([p, apex, r], meta), ([apex, q, r], meta)]
        if not gone:
            break
        items = [item for i, item in enumerate(items) if i not in gone] + added
    return items, removed


def pair_quads(items, max_dev):
    """Merge pairs of triangles that share an edge into quads, so one quadblock draws what two
    triblocks did. A pair merges only when one affine texture map covers both (the quad's
    midpoints and centre then get exactly the texture the triangles had), the shape is convex
    and it is flat within max_dev. -> (quads as 4 vertex dicts in winding order, leftover tris);
    both carry their meta."""
    def k(v):
        return tuple(v['pos'])
    edges = defaultdict(list)
    for i, (t, _) in enumerate(items):
        for e in range(3):
            edges[frozenset((k(t[e]), k(t[(e + 1) % 3])))].append((i, e))
    cands = []
    for lst in edges.values():
        if len(lst) != 2:
            continue
        (i, e), (j, f) = lst
        (ti, mi), (tj, mj) = items[i], items[j]
        if mi != mj:
            continue
        a, b, c = ti[e], ti[(e + 1) % 3], ti[(e + 2) % 3]
        if not (k(tj[f]) == k(b) and k(tj[(f + 1) % 3]) == k(a)):
            continue  # opposite winding: the pair would fold over
        b2, a2, d = tj[f], tj[(f + 1) % 3], tj[(f + 2) % 3]
        if any(np.abs(x[key] - y[key]).max() > 1e-4 for x, y in ((a, a2), (b, b2)) for key in ('uv', 'col')):
            continue  # texture or colour seam along the shared edge
        P = [np.asarray(v['pos'], float) for v in (a, b, c, d)]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        if abs(np.dot(P[3] - P[0], n)) / np.linalg.norm(n) > max_dev:
            continue  # not flat: the quad's centre would leave the triangles' surface
        # d in a, b, c's own affine frame: one texture map covers both when d's uv follows it
        M = np.stack([P[1] - P[0], P[2] - P[0]], 1)
        st = np.linalg.lstsq(M, P[3] - P[0], rcond=None)[0]
        uv = a['uv'] + st[0] * (b['uv'] - a['uv']) + st[1] * (c['uv'] - a['uv'])
        if np.abs(uv - d['uv']).max() > 2e-3:
            continue
        col = a['col'] + st[0] * (b['col'] - a['col']) + st[1] * (c['col'] - a['col'])
        if np.abs(col - d['col']).max() > 0.04:
            continue
        quad = [a, d, b, c]
        Q = [np.asarray(v['pos'], float) for v in quad]
        if any(np.dot(np.cross(Q[(q + 1) % 4] - Q[q], Q[(q + 2) % 4] - Q[(q + 1) % 4]), n) <= 0 for q in range(4)):
            continue  # concave or degenerate corner
        # prefer merging across the longest edge: the two triangles of a split rectangle
        cands.append((-np.linalg.norm(P[1] - P[0]), i, j, quad, mi))
    cands.sort(key=lambda x: x[0])
    used = set()
    quads = []
    for _, i, j, quad, mi in cands:
        if i in used or j in used:
            continue
        used |= {i, j}
        quads.append((quad, mi))
    return quads, [items[i] for i in range(len(items)) if i not in used]


def group_grids(quads, max_dev):
    """Group four quads that meet at one vertex, each sharing an edge with the next around it,
    into one quadblock: its 3x3 grid is then made of the source's own corners, so nothing is
    interpolated. Each face keeps its own UVs (a quadblock has a texture layout per face), so
    texture seams between them are fine. -> (grids as 4 quads in cyclic order around the shared
    centre, each rotated to start at it; leftover quads)."""
    def k(v):
        return tuple(v['pos'])
    around = defaultdict(list)
    for i, (q, _) in enumerate(quads):
        for c in range(4):
            around[k(q[c])].append((i, c))
    used = set()
    grids = []
    for centre, lst in around.items():
        if len(lst) != 4 or any(i in used for i, _ in lst):
            continue
        if len({quads[i][1] for i, _ in lst}) != 1:
            continue
        rot = {i: quads[i][0][c:] + quads[i][0][:c] for i, c in lst}  # v, x, o, y
        # the next quad around the centre starts along this one's last edge: its x is our y
        by_x = {k(r[1]): i for i, r in rot.items()}
        start = lst[0][0]
        order = [start]
        while len(order) < 4:
            nxt = by_x.get(k(rot[order[-1]][3]))
            if nxt is None or nxt in order:
                break
            order.append(nxt)
        if len(order) != 4 or k(rot[order[-1]][3]) != k(rot[start][1]):
            continue
        # every outer corner and edge point must be distinct, or CTE cannot read the grid
        pts = {k(rot[i][j]) for i in order for j in (1, 2)}
        if len(pts) != 8:
            continue
        # CTE takes the grid's facing from its outer corners (orient_quad): none may be straight
        if min(corner_sines([rot[i][2] for i in order])) < 0.1:
            continue
        # far away the game draws a block as one quad over its outer corners, skipping the edge
        # points, which the neighbours keep as corners: each must lie on its edge's straight line,
        # or a crack opens there (the sky through the hills)
        if any(line_distance(rot[order[n]][3], rot[order[n]][2], rot[order[(n + 1) % 4]][2]) > max_dev for n in range(4)):
            continue
        used |= set(order)
        grids.append(([rot[i] for i in order], quads[start][1]))
    return grids, [quads[i] for i in range(len(quads)) if i not in used]


def line_distance(v, a, b):
    """Distance of vertex dict v from the line through vertex dicts a and b."""
    P, A, B = (np.asarray(x['pos'], float) for x in (v, a, b))
    d = B - A
    return np.linalg.norm(np.cross(P - A, d)) / max(np.linalg.norm(d), 1e-12)


def corner_sines(ring):
    """|sin| of the angle at each corner of a polygon of vertex dicts (0 for a straight corner)."""
    P = [np.asarray(v['pos'], float) for v in ring]
    out = []
    for i in range(len(P)):
        a, b = P[i - 1] - P[i], P[(i + 1) % len(P)] - P[i]
        out.append(np.linalg.norm(np.cross(a, b)) / max(np.linalg.norm(a) * np.linalg.norm(b), 1e-12))
    return out


def orient_quad(quad):
    """CrashTeamEditor takes a quadblock's facing from its corners 0 and 2 (the triangles
    p0 p2 p6 and p2 p8 p6 of its grid) and turns the block over when that disagrees with the OBJ
    normal. A quad that undoes a midpoint split has a straight corner, which gives a zero normal
    and a block turned over at random: start the ring so that corners 0 and 2 are not straight."""
    s = corner_sines(quad)
    return quad if min(s[0], s[2]) >= min(s[1], s[3]) else quad[1:] + quad[:1]


def fan(poly):
    return [(poly[0], poly[i], poly[i + 1]) for i in range(1, len(poly) - 1)]


def area(tri):
    a, b, c = (np.array(v['pos']) for v in tri)
    return np.linalg.norm(np.cross(b - a, c - a)) / 2


def ground_under(x, z, top, tris):
    """The highest of tris (KCL triangles) under (x, z) at or below height top. -> (height, its triangle)."""
    best = None
    for tri in tris:
        a, b, c, nrm, _ = tri
        d = lambda u, v: (v[0] - u[0]) * (z - u[2]) - (v[2] - u[2]) * (x - u[0])
        e = (d(a, b), d(b, c), d(c, a))
        if min(e) >= 0 or max(e) <= 0:
            y = a[1] - (nrm[0] * (x - a[0]) + nrm[2] * (z - a[2])) / nrm[1]
            if y <= top and (best is None or y > best[0]):
                best = (y, tri)
    return best


def minimap(kcl, to_lev, forward, route):
    """The road from above: a white edge round a blended fill, as retail's, turned so that forward
    (x, z, the way the start line faces) is as near up as a quarter turn gets; a diagonal start, with
    two turns as near, takes the one that draws the road larger. -> (RGBA array, the UIMap's first
    nine fields, which put a world position on it)."""
    road = [[to_lev(p) for p in t[:3]] for t in kcl
            if (i := COLLISION.get(mkdsmap.kcl_type(t[4]))) and i[0] in MAP_ROAD and abs(t[3][1]) >= 0.3]
    points = np.array(road).reshape(-1, 3)
    def axes(mode):
        up_x, up_z = MAP_UP[mode]
        return (lambda p: p[0] * -up_z + p[2] * up_x,  # a quarter turn clockwise from up
                lambda p: -(p[0] * up_x + p[2] * up_z))

    def pixels_down(mode):  # per LEV unit, filling the box
        wide, tall = axes(mode)
        return min((MAP_MAX_HEIGHT - 5) / np.ptp(tall(points.T)), (MAP_MAX_WIDTH - 4) / MAP_PIXEL_ASPECT / np.ptp(wide(points.T)))

    mode = max(range(4), key=lambda m: (round(float(np.dot(forward, MAP_UP[m])), 2), pixels_down(m)))
    wide, tall = axes(mode)
    x0, z0 = wide(points.T).min(), tall(points.T).min()
    span_x, span_z = np.ptp(wide(points.T)), np.ptp(tall(points.T))
    down = pixels_down(mode)
    across = down * MAP_PIXEL_ASPECT
    width = math.ceil(span_x * across - 1e-6) + 4
    height = (math.ceil(span_z * down - 1e-6) + 5) // 2 * 2  # the game keeps half of it
    at = lambda p: ((wide(p) - x0) * across + 2, (tall(p) - z0) * down + 2)
    scale = 4  # drawn large and reduced, or a road narrower than a pixel breaks up
    image = Image.new('L', (width * scale, height * scale), 0)
    draw = ImageDraw.Draw(image)
    for tri in road:
        draw.polygon([(x * scale, y * scale) for x, y in map(at, tri)], fill=255)
    mask = np.array(image.resize((width, height), Image.Resampling.BOX)) > 40
    # a course whose infield is typed as road (GBA Luigi Circuit) draws a square: keep what lies along
    # the route. A normal course skips this, so a fork the route does not take stays on its map.
    corridor = Image.new('L', (width * scale, height * scale), 0)
    pen = ImageDraw.Draw(corridor)
    steps = [(np.array(a) + (np.array(b) - np.array(a)) * t / 8, r) for (a, r), (b, _) in zip(route, route[1:] + route[:1]) for t in range(8)]
    for p, r in steps:
        x, y = at(np.array(to_lev(p)))
        reach = np.linalg.norm(to_lev(p + np.array([r, 0, 0])) - to_lev(p))
        rx, ry = reach * across * scale * MAP_CORRIDOR, reach * down * scale * MAP_CORRIDOR
        pen.ellipse([x * scale - rx, y * scale - ry, x * scale + rx, y * scale + ry], fill=255)
    along_route = np.array(corridor.resize((width, height), Image.Resampling.BOX)) > 40
    if mask.sum() > MAP_TOO_MUCH * along_route.sum():
        mask &= along_route
    grown = mask.copy()
    for axis in (0, 1):
        for shift in (-1, 1):
            grown |= np.roll(mask, shift, axis)
    inner = grown.copy()
    for axis in (0, 1):
        for shift in (-1, 1):
            inner &= np.roll(grown, shift, axis)
    rgba = np.zeros((height, width, 4), np.uint8)
    rgba[grown] = MAP_EDGE
    rgba[inner] = MAP_FILL
    left, top = MAP_CORNER_X - width + 1, MAP_CORNER_Y - height + 1
    range_wide, range_tall = round(width / across), round(height / down)
    range_x, range_z = (range_tall, range_wide) if mode & 1 else (range_wide, range_tall)
    start_x = left + 2 - x0 * across - MAP_ICON_X
    start_y = top + 2 - z0 * down - MAP_ICON_Y + 0x10  # UI_Map_GetIconPos takes 0x10 off
    # only the ranges count, and each end has to fit 16 bits
    return rgba, [range_x // 2, range_z // 2, range_x // 2 - range_x, range_z // 2 - range_z, width, height // 2,
                  int(round(start_x)), int(round(start_y)), mode]


def route_line(route):
    """The route as a smooth closed line, FLIGHT_SAMPLE apart. -> positions, radii, way along it."""
    p = np.array([q for q, _ in route])
    r = np.array([q for _, q in route])
    n = len(p)
    points, radii = [], []
    for i in range(n):
        a, b, c, d = p[i - 1], p[i], p[(i + 1) % n], p[(i + 2) % n]
        steps = max(1, int(np.linalg.norm(c - b) / FLIGHT_SAMPLE))
        for k in range(steps):
            t = k / steps
            points.append(0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t ** 3))
            radii.append(r[i] + (r[(i + 1) % n] - r[i]) * t)
    points = np.array(points)
    way = np.roll(points, -1, 0) - points
    return points, np.array(radii), way / np.maximum(np.linalg.norm(way, axis=1, keepdims=True), 1e-9)


def path_up(points):
    """An up direction for each point of a path through the air: square to it, in the vertical plane."""
    ups = []
    for i, p in enumerate(points):
        way = points[min(i + 1, len(points) - 1)] - points[max(i - 1, 0)]
        way = way / max(np.linalg.norm(way), 1e-9)
        side = np.cross(way, (0.0, 1.0, 0.0))
        length = np.linalg.norm(side)
        ups.append(np.cross(side / length, way) if length > 1e-3 else np.array([0.0, 1.0, 0.0]))
    return ups


def arc(start, end, lift):
    """A path from start to end, rising lift over the straight line at its middle."""
    count = max(8, int(np.linalg.norm(end - start) / (FLIGHT_POINT * 4)))
    return [start + (end - start) * (i / count) + np.array([0.0, lift * 4 * (i / count) * (1 - i / count), 0.0]) for i in range(count + 1)]


def hit_by(p, q, triangles):
    """Which triangles (three arrays of corners) the straight segment from p to q runs through."""
    a, b, c = triangles
    d = q - p
    e1, e2 = b - a, c - a
    h = np.cross(d, e2)
    det = (e1 * h).sum(1)
    ok = np.abs(det) > 1e-9
    s = p - a
    u = (s * h).sum(1) / np.where(ok, det, 1)
    g = np.cross(s, e1)
    v = (g @ d) / np.where(ok, det, 1)
    t = (e2 * g).sum(1) / np.where(ok, det, 1)
    return ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 0) & (t <= 1)


def crossings(segments, triangles):
    """How many of the triangles the straight segments run through, in all."""
    return sum(int(hit_by(p, q, triangles).sum()) for p, q in segments)


def clear_arc(course, start, end):
    """A cannon's path: the lowest arc that passes through least of what is drawn, roofs and all."""
    length = np.linalg.norm(end - start)
    drawn = [tri for mi, tri in course.triangles if mi not in course.far_mats]
    triangles = [np.array([tri[k][0] for tri in drawn]) for k in range(3)]
    best = None
    for share in CANNON_ARCS:
        path = arc(start, end, length * share)
        hits = crossings(list(zip(path[1:-2], path[2:-1])), triangles)
        print(f'cannon arc {share}: through {hits} triangles')
        if best is None or hits < best[0]:
            best = (hits, path)
    return best[1]


def find_flights(course, to_lev, scale):
    """Find forced paths, their log lines and preview geometry, and the walls guarding their exits."""
    lev = 64 * scale
    line, radii, way = route_line(course.route)
    count = len(line)
    solid = [t for t in course.kcl if (i := COLLISION.get(mkdsmap.kcl_type(t[4]))) and i[2] & GROUND and not i[2] & KILL_PLANE and t[3][1] > 0.1]
    cells = defaultdict(list)
    for t in solid:
        xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
        for gx in range(int(min(xs) // 128), int(max(xs) // 128) + 1):
            for gz in range(int(min(zs) // 128), int(max(zs) // 128) + 1):
                cells[(gx, gz)].append(t)

    def under(p, top):
        found = ground_under(p[0], p[2], top, cells.get((int(p[0] // 128), int(p[2] // 128)), ()))
        return None if found is None else found[0]

    def on_ground(i):
        """The route's sample i put on the ground, a little above it."""
        h = under(line[i % count], line[i % count][1] + ROUTE_UNDER_BUMP)
        return np.array([line[i % count][0], (h if h is not None else line[i % count][1]) + FLIGHT_LAND, line[i % count][2]])

    flights, log, taken, paths, exit_walls = [], [], set(), [], []

    def add(kind, gate_width, gate_height, speed, points, ups, first, last):
        rows = [[*np.round(to_lev(p)).astype(int).tolist(), *np.round(u * 4096).astype(int).tolist()] for p, u in zip(points, ups)]
        flights.append({'gate': [int(gate_width * lev), int(gate_height * lev), speed], 'points': rows})
        length = sum(np.linalg.norm(b - a) for a, b in zip(points, points[1:]))
        log.append(f'{kind} {length:.0f} long at {np.round(points[0]).astype(int).tolist()} to {np.round(points[-1]).astype(int).tolist()}')
        paths.append(points)
        i = first
        while True:
            taken.add(i)
            if i == last:
                break
            i = (i + 1) % count

    def landing(i):
        """The first sample from i on that has ground under it."""
        for k in range(count):
            j = (i + k) % count
            if under(line[j], line[j][1] + ROUTE_UNDER_BUMP) is not None:
                return j
        return i % count

    def closing(j, path):
        """The end of a path, running on along the road so the kart lands facing it."""
        return path + [path[-1] + way[j] * FLIGHT_POINT]

    # Loops: the road's own surface, turned as it turns, kept a little off it
    rail = []
    for i in range(count):
        best = None
        for a, b, c, n, _ in course.loop_road:
            d = float(np.dot(line[i] - a, n))
            if abs(d) > LOOP_REACH or (best is not None and abs(d) >= abs(best[0])):
                continue
            foot = line[i] - n * d
            if all(np.dot(np.cross(v - u, foot - u), n) >= -8 for u, v in ((a, b), (b, c), (c, a))):
                best = (d, foot, n)
        rail.append(best)
    i = 0
    while i < count:
        if rail[i] is None or rail[i][2][1] >= LOOP_ROAD:  # the road up to a loop is road
            i += 1
            continue
        end = i
        k = i
        while k < count and k - end < 12:  # a route between two triangles finds neither for a moment
            if rail[k] is not None:
                end = k
                if rail[k][2][1] >= LOOP_LEVEL:  # level again: the kart runs out along the road
                    break
            k += 1
        if (end - i) * FLIGHT_SAMPLE >= FLIGHT_GAP / 2:
            found = [(k, rail[k]) for k in range(i, end + 1) if rail[k] is not None][::FLIGHT_POINT // FLIGHT_SAMPLE]
            points = [f + n * 8 for _, (_, f, n) in found]
            normals = [n for _, (_, _, n) in found]
            ups = [sum(normals[max(0, k - 2):k + 3]) for k in range(len(normals))]
            ups = [u / np.linalg.norm(u) for u in ups]
            j = landing(end + 4)
            points += [on_ground(j), on_ground(j) + way[j] * FLIGHT_POINT]
            ups += [np.array([0.0, 1.0, 0.0])] * 2
            add('loop', radii[i] * 2.5, 300, 0, points, ups, i, j)
            exit_walls += loop_exit_walls(course, rail[end][1], way[j])
        i = end + 1

    # Cannons: the wall of type KCL_CANNON a kart drives into, and the point it is shot at
    targets = mkdsmap.cannons(course.nkm)
    for variant in sorted({(t[4] >> 5) & 7 for t in course.cannons}):
        wall = np.array([v for t in course.cannons if (t[4] >> 5) & 7 == variant for v in t[:3]])
        middle = (wall.min(0) + wall.max(0)) / 2
        first = int(((line - middle)[:, [0, 2]] ** 2).sum(1).argmin())
        aim = int(((line - targets[variant]) ** 2).sum(1).argmin())
        j = landing(aim)
        h = under(middle, middle[1])
        start = np.array([middle[0], h if h is not None else wall[:, 1].min(), middle[2]])
        end = np.maximum(on_ground(j), targets[variant])
        points = closing(j, clear_arc(course, start, end))
        add('cannon', np.ptp(wall[:, [0, 2]], axis=0).max() / 2 + 40, np.ptp(wall[:, 1]) + 60, CANNON_SPEED, points, path_up(points), first, j)

    # Gaps longer than a kart jumps, as MKDS's lighter karts fly: the jump over Bowser Castle's lava
    bare = [under(line[i], 1e9) is None for i in range(count)]
    i = 0
    while i < count:
        if not bare[i]:
            i += 1
            continue
        j = i
        while j < count and bare[j]:
            j += 1
        length = (j - i) * FLIGHT_SAMPLE
        if length >= FLIGHT_GAP and not any(k in taken for k in range(i, j)):
            land = landing(j)
            start, end = on_ground(i - 2), on_ground(land)
            points = closing(land, arc(start, end, length * 0.1))
            add('gap', radii[i] * 2.5, 300, GAP_SPEED, points, path_up(points), i, land)
        i = j

    # Ground rising at once: a floor that lifts karts in MKDS
    heights = [under(line[i], line[i][1] + ROUTE_UNDER_BUMP) for i in range(count)]
    for i in range(count):
        before, after = heights[i - 1], heights[i]
        if before is not None and after is not None and after - before >= FLIGHT_STEP and i not in taken:
            first, land = (i - 10) % count, (i + 10) % count
            start = np.array([line[first][0], before + FLIGHT_LAND, line[first][2]])
            end = on_ground(land)
            points = closing(land, arc(start, end, FLIGHT_STEP))
            add('step', radii[i] * 2.5, 300, STEP_SPEED, points, path_up(points), first, land)
    return flights, log, paths, exit_walls


def fruit_rows(route_at, total, lev, crates, to_lev, ground):
    """Wumpa fruit positions in LEV units. route_at(s) is the route's point s MKDS units past the
    finish line. A row wanted somewhere it cannot lie (off the road, in the air, near a crate or the
    finish line) slides forward along the route; one that slides half a gap is dropped."""
    spacing, gap = FRUIT_SPACING / lev, FRUIT_ROW_GAP / lev
    clear_start, clear_crates = FRUIT_CLEAR_OF_START / lev, FRUIT_CLEAR_OF_CRATES / lev
    length = spacing * (FRUIT_PER_ROW - 1)

    def row_at(s):
        if s < clear_start or s + length > total - clear_start:
            return None
        row = []
        for k in range(FRUIT_PER_ROW):
            p = route_at(s + spacing * k)
            found = ground_under(p[0], p[2], p[1] + ROUTE_ABOVE_GROUND, ground)
            if found is None or p[1] - found[0] > ROUTE_IN_THE_AIR or COLLISION[mkdsmap.kcl_type(found[1][4])][0] not in FRUIT_ROAD:
                return None
            q = to_lev((p[0], found[0], p[2]))
            if any(math.dist(q, c) < FRUIT_CLEAR_OF_CRATES for c in crates):
                return None
            row.append([int(round(q[0])), int(round(q[1])) + CRATE_LIFT, int(round(q[2]))])
        return row

    rows = max(1, int(total / gap))
    fruit, dropped = [], 0
    for r in range(rows):
        wanted = (r + 0.5) * total / rows
        row = next((row for step in range(int(gap / 2 / spacing)) if (row := row_at(wanted + step * spacing))), None)
        if row:
            fruit += row
        else:
            dropped += 1
    return fruit, dropped


# ---------------------------------------------------------------- course

# Scenery objects that never move, and the model each draws. A course keeps its models in its own
# archive; an object whose model is missing there is reported and left out.
SCENERY = {
    0x12d: 'yoshi_tree', 0x12e: 'BeachTree1', 0x12f: 'earthen_pipe1', 0x130: 'opa_tree1', 0x131: 'OlgPipe1',
    0x132: 'OlgMush1', 0x133: 'of6yoshi1', 0x134: 'cow', 0x136: 'mini_dokan', 0x138: 'GardenTree1',
    0x13a: 'CrossTree1', 0x13e: 'Bank_Tree1', 0x142: 'MarioTree3', 0x145: 'TownTree1', 0x146: 'Snow_Tree1',
    0x148: 'DeTree1', 0x14b: 'KinoHouse1', 0x14c: 'KinoHouse2', 0x14d: 'KinoMount1', 0x14e: 'KinoMount2',
    0x14f: 'olaTree1c', 0x150: 'osaTree1c', 0xcb: 'gear_black', 0x191: 'kuribo', 0x1a6: 'ob_pakkun_sf', 0x153: 'om6Tree1',
    0x199: 'choropu', 0x1ac: 'crab', 0x1ad: 'sun', 0x135: 'NsCannon1', 0x155: 'Of6Tree1', 0x156: 'Of6Tree1', 0x19d: 'sman_bottom', 0x140: 'chandelier',
    0x1a7: 'poo', 0x13d: 'teresa', 0x151: 'picture1', 0x152: 'picture2', 0x13c: 'bakubaku', 0x19e: 'kanoke_64', 0x1a0: 'basabasa', 0x157: 'TownMonte',
    0x1a1: 'NsKiller1', 0x1b2: 'sanbo_b', 0x1aa: 'PakkunBody',
}
# What MKDS draws as parts of one object: each part is an object of its own here, this far over the first
SNOWMAN_HEAD, POKEY_BODY, POKEY_HEAD, PIRANHA_HEAD = range(0x10000, 0x10004)
FIRE_PLANT_SHOT = 0x10004
COFFIN = 0x10005  # a coffin's own box, its lid opening while its bats come out; the object itself is its bats
PIRANHA_MOUTH = 0x10006  # a fire plant's head itself: PakkunZHead is only a flat disc behind it
DRUM = 0xd2
STACKED_ID = SNOWMAN_HEAD
STACKED = {0x19d: [(SNOWMAN_HEAD, 19)], 0x1b2: [(POKEY_BODY, 40), (POKEY_BODY, 66), (POKEY_HEAD, 92)], 0x1aa: [(PIRANHA_HEAD, 70), (PIRANHA_MOUTH, 70)], 0x19e: [(COFFIN, 0)]}
SCENERY.update({SNOWMAN_HEAD: 'sman_top', POKEY_BODY: 'sanbo_b', POKEY_HEAD: 'sanbo_h', PIRANHA_HEAD: 'PakkunZHead'})
# A Pokey walks its path as one, and a fire plant's shots run along its own
POKEY_OBJECT = 0x1b2
POKEY_MOVER = mkds_movers.Mover('sanbo_b', (mkds_movers.PATH, mkds_movers.BACK_AND_FORTH), None, 3)
POKEY_PARTS = {POKEY_BODY: POKEY_MOVER, POKEY_HEAD: POKEY_MOVER._replace(model='sanbo_h')}
FIRE_PLANT, FIRE_PLANT_SHOTS = 0x1aa, ('FireBall',)
FIRE_PLANT_MOVER = mkds_movers.Mover('FireBall', (mkds_movers.KEYS, 'fire_plant'))
COFFIN_MOVER = mkds_movers.Mover('kanoke_64', (mkds_movers.KEYS, 'coffin_lid'))
CHAIN_CHOMPS = {0x196, 0x1a5}
# What a kart hitting it gets is the game's reaction to another object: bats are let out by their spawners,
# a fire plant's shot and a Pokey's parts are of the object they belong to (mkds_objects.REACTION)
# A snowman's head stands on its body and bursts with it (Mover.c, MOVER_STRUCK_BURST_HEAD)
SNOWMAN_HEAD_MOVER = mkds_movers.Mover('sman_top', (mkds_movers.STILL,), 0)  # the body's cylinder is the whole snowman's
STRUCK_AS = {SNOWMAN_HEAD: mkds_movers.BURST_HEAD}
REACTS_AS = {0x1a0: 0x19f, 0x19e: 0x19f, FIRE_PLANT_SHOT: 0x1ab, POKEY_BODY: POKEY_OBJECT, POKEY_HEAD: POKEY_OBJECT, SNOWMAN_HEAD: 0x19d}
SUN = 0x1ad
FLARE_MODEL, FLARE_COURSE = 'fireball2', 'stadium_course'  # a sun's flares are effects with no model: Wario Stadium's fire stands in
BOUND = 0x1a8
GEAR, WHITE_GEAR_SETTING = 0xcb, 5  # a Tick-Tock Clock gear is white where this setting of it is 0
HANGING_MODELS = {'gear_black', 'gear_white', 'chandelier', 'teresa', 'picture1', 'picture2', 'basabasa', 'kamome', 'sun', 'bakubaku'}  # the rest stand on the ground, however far under them their origin lies
WALL_MODELS = {'picture2'}  # flat, but it hangs on a wall: not crossed like a billboard
FLAT_MODEL = 1  # a model this thin is a billboard
BURNER = 0x1a4
WOOD_BOX = 0x67
TYRE_TEXTURE = (1.0, 2.0, 0.0, 1.0)  # half a tyre, mirrored at its hub: the quad spans it from one rim to the other
HOLE_LIFT = 1
HOLES = {0x1a7: 'hole', 0x199: 'hole', 0x196: 'wanwan_kui'}  # what a Rocky Wrench and a Monty Mole come out of, flat on the ground, and a Chain Chomp's stake
START_CLEAR = 700  # no solid object this close to the start line
# What moves in MKDS stands still here, at its placed spot: the game's track collision is static, and a
# course without its big obstacles is worse than one with them at rest. (id: model, solid, lift) in
# STILL_OBJECTS: solid gives it walls round its box, lift raises it clear of the road.
STILL_OBJECTS = {
    0xcd: ('second_hand', True, 0), 0xcf: ('pendulum', True, 0), 0xce: ('test_cylinder', True, 0), 0xd2: ('dram', True, 0), 0x1a9: ('flipper', True, 0),
    0x1a8: ('bound', True, 0), 0xd: ('puddle', False, 1), 0x195: ('bus_a', True, 0), 0x19a: ('car_a', True, 0), 0x19c: ('truck_a', True, 0),
    0x192: ('rock', True, 0), 0x1b1: ('rock2', True, 0), 0x196: ('wanwan', True, 0), 0x1a5: ('wanwan', True, 0),
    0x1a3: ('move_tree', True, 0), 0x193: ('dossun', False, 0), 0x154: ('RainStar', False, 0), 0x158: ('airship', False, 0),
    0x149: ('BankEgg1', False, 0), 0x19e: ('kanoke_64', True, 0),
}
MIRRORED_BY_SETTING = {0x1a9}  # Waluigi Pinball's flippers: one model, a left and a right
GROUNDED_STILL = {'wanwan': 28, 'rock': 28, 'rock2': 28, 'bound': 10}  # how far their origin is over their foot
# Scenery that karts drive on, and the radius of its flat top in its model. Tick-Tock Clock's turning
# cogs carry the road over a pit, in no collision of the course's own, and Bowser Castle's turning
# room is the floor of its first hall.
FLOOR_OBJECTS = {0xcb: 300, 0xd0: 472}
# Tick-Tock Clock's two upright wheels at its end, which a kart may drive onto: their radius and thickness,
# from the object's own collision (0x20e0764)
WHEELS = {0xce: (299, 50)}
FLOOR_MODELS = {}
# Delfino Square's drawbridge, which rises and falls in MKDS: its two halves stand a little open, each
# a ramp of dash panels that throws a kart over the top. Each half's own width and length, and the
# model's parts: its chains and posts run up to towers the course does not have, so only the deck stays.
OPEN_BRIDGE, OPEN_BRIDGE_WIDTH, OPEN_BRIDGE_LENGTH, OPEN_BRIDGE_ANGLE = 0xcc, 180, 350, 12
OPEN_BRIDGE_TOP = 60
OPEN_BRIDGE_LEFT_OUT = {'Bkusari'}
# Bowser Castle's slabs that slide over the lava, each a floor at its placed height. The model's top is at
# its height 0: half its width and depth.
SLAB_OBJECT, SLAB_HALF_WIDTH, SLAB_HALF_DEPTH = 0xca, 400, 175
# Bowser Castle's turning log: a kart rides along its top. Its model stands along -y from its end.
ROTARY_BRIDGE, ROTARY_BRIDGE_UP = 0xd1, 0.5
BRIDGE_RAIL = 64
FAR_EDGE = 8  # backdrops take blocks this many times longer: nothing rides on them
WATER_EDGE = 3  # water too, away from the road
WATER_NEAR, WATER_NEAR_CELL = 700, 240  # MKDS units: how far from the route a sheet takes small blocks, and their size
WATER_OBJECTS = {0x01, 0x03, 0x06, 0x09}  # the sheet of water round a course, one per course
DEPTH_ONLY_WATER = {'Zwater'}  # a water model's part that MKDS draws for the depth buffer alone
WATER_UNDER_ROUTE = 16
WATER_KEEPS_BELOW = 8
OBJECT_ABOVE_GROUND = 64
KCL_ROAD = 0x00
ROAD_KINDS = {0x00, 0x01, 0x06, 0x07, 0x0d, 0x12, 0x13, 0x14}
# Rainbow Road's rising ring: road half as wide as a kart turns at speed, so it takes edges here
EDGED_ROAD = {'rainbow_course': (-100, 2300, 800)}
EDGE_HEIGHT = 120  # a boost pad on the turn throws a kart this high off the road
EDGE_PROBE = 12  # how far past an edge there is no road, for it to be open
KCL_DASH = 0x07
KCL_WALL = 0x08
# LEV bounds of the jump across Rainbow Road's last bend, from the F7 capture.
# The course-wide fall plane lies below this opening.
SHORTCUT_OPENINGS = {'rainbow_course': ((9000, -2200, -200), (11200, 0, 2200))}


def rotate_y(p, degrees):
    a = math.radians(degrees)
    return np.array([p[0] * math.cos(a) + p[2] * math.sin(a), p[1], -p[0] * math.sin(a) + p[2] * math.cos(a)])


def rotate(p, degrees):
    """An object's rotation in the NKM: about x, then y."""
    a = math.radians(degrees[0])
    q = np.array([p[0], p[1] * math.cos(a) - p[2] * math.sin(a), p[1] * math.sin(a) + p[2] * math.cos(a)])
    return rotate_y(q, degrees[1])


def disc(pos, radius, others):
    """A flat round floor at pos, as upward-facing KCL triangles. Floors overlap as cogs do, at different
    heights, and each stops where it meets one of others (pos, radius), so a kart has one ground there."""
    ring = []
    for i in range(24):
        way = np.array([math.cos(i * math.pi / 12), 0.0, math.sin(i * math.pi / 12)])
        reach = radius
        for other, other_radius in others:
            apart = (other - pos) * (1.0, 0.0, 1.0)
            length = np.linalg.norm(apart)
            towards = way @ apart / length
            if towards > 0:  # the two circles' common chord is this far from pos
                reach = min(reach, (length ** 2 + radius ** 2 - other_radius ** 2) / (2 * length) / towards)
        ring.append(pos + way * reach)
    return [(pos, ring[(i + 1) % 24], ring[i], np.array([0.0, 1.0, 0.0]), KCL_ROAD << 8) for i in range(24)]


TURNING_ROAD = 1 << 16  # on a KCL triangle's attribute: static collision under a mover that draws it
WHEEL_SIDES = 32


def wheel(top, axis, radius, thickness):
    """A wheel turning on its axis, as KCL triangles: its rim, road where it faces up and wall where it
    faces sideways, and its two flat faces as walls. Turning about its axis it keeps its shape, so static
    collision holds it while its model turns (MKDS: 0x20e0764, the top face at the object's position)."""
    axis = axis / np.linalg.norm(axis)
    side = np.cross(axis, (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0))
    side /= np.linalg.norm(side)
    other = np.cross(axis, side)
    bottom = top - axis * thickness
    ring = [math.cos(2 * math.pi * i / WHEEL_SIDES) * side + math.sin(2 * math.pi * i / WHEEL_SIDES) * other for i in range(WHEEL_SIDES)]
    tris = []

    def face(a, b, c, outward, kind):
        n = np.cross(b - a, c - a)
        if np.dot(n, outward) < 0:
            b, c, n = c, b, -n
        tris.append((a, b, c, n / max(np.linalg.norm(n), 1e-9), kind << 8))

    for i in range(WHEEL_SIDES):
        r0, r1 = ring[i], ring[(i + 1) % WHEEL_SIDES]
        out = (r0 + r1) / 2
        if out[1] < -0.5:
            continue  # its underside is never reached
        kind = KCL_ROAD if out[1] > 0.5 else KCL_WALL
        a, b, c, d = bottom + r0 * radius, bottom + r1 * radius, top + r1 * radius, top + r0 * radius
        face(a, b, c, out, kind)
        face(a, c, d, out, kind)
        face(top, top + r0 * radius, top + r1 * radius, axis, KCL_WALL)
        face(bottom, bottom + r1 * radius, bottom + r0 * radius, -axis, KCL_WALL)
    return tris


def turning(triangles):
    return [(a, b, c, n, attr | TURNING_ROAD) for a, b, c, n, attr in triangles]


def placed_wheel(object_id, pos, rot, size):
    """A WHEELS object as it is placed, as KCL triangles."""
    radius, thickness = WHEELS[object_id]
    return turning(wheel(pos, rotate(np.array([0.0, 1.0, 0.0]), rot), radius * size[0], thickness * size[1]))


def floor_discs(nkm):
    """The FLOOR_OBJECTS a course places, as KCL triangles."""
    floors = [(pos, FLOOR_OBJECTS[object_id] * size[0]) for object_id, pos, _, size in mkdsmap.placed_objects(nkm) if object_id in FLOOR_OBJECTS]
    return [t for floor in floors for t in disc(*floor, [other for other in floors if other is not floor])]


def wall_quad(p, q, lift, inside):
    """The wall from p to q, lift high, as KCL triangles facing inside: a block stops a kart from its front alone."""
    tris = ((p, q, q + lift), (p, q + lift, p + lift))
    n = np.cross(q - p, lift)
    if np.dot(n, inside - p) < 0:
        tris = ((q, p, q + lift), (q + lift, p, p + lift))
        n = -n
    return [(*t, n / max(np.linalg.norm(n), 1e-9), KCL_WALL << 8) for t in tris]


def loop_exit_walls(course, exit_point, forward):
    """Block reverse entry where the drivable exit meets the collisionless part of a loop."""
    walls = []
    steep_edges = [(a, b) for t in course.loops if t[3][1] > 0
                   for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0]))]
    for a, b, c, normal, _ in course.loop_road:
        if normal[1] < LOOP_ROAD:
            continue
        for p, q, inside in ((a, b, c), (b, c, a), (c, a, b)):
            middle = (p + q) / 2
            offset = middle - exit_point
            if np.linalg.norm(offset) > 400 or offset @ forward > 0:
                continue
            wall = wall_quad(p, q, np.array([0.0, EDGE_HEIGHT, 0.0]), inside)
            if wall[0][3] @ forward <= 0.5:
                continue
            # KCL reconstructs the corners of each triangle independently.
            if any(np.linalg.norm(p - v) < 2 and np.linalg.norm(q - u) < 2 for u, v in steep_edges):
                walls += wall
    return walls


def open_shortcut(kcl, low, high):
    """Cut an opening in the local rescue surfaces while retaining the rest of each triangle."""
    for t in kcl:
        vertices = np.array(t[:3])
        info = COLLISION.get(mkdsmap.kcl_type(t[4]))
        if (info is None or not info[2] & KILL_PLANE or
                np.any(vertices.max(0) <= low) or np.any(vertices.min(0) >= high)):
            yield t
            continue
        inside = [{'pos': p} for p in vertices]
        outside = []
        for axis in range(3):
            for bound, keep_above in ((low[axis], True), (high[axis], False)):
                outside.extend(fan(clip(inside, 'pos', axis, bound, not keep_above)))
                inside = clip(inside, 'pos', axis, bound, keep_above)
        for tri in outside:
            if len(tri) == 3:
                yield (*(v['pos'] for v in tri), *t[3:])


def road_strip(start, end, width, rail=0, kind=None):
    """A road from start to end, flat across, as KCL triangles of type kind, between rails of height rail."""
    kind = KCL_ROAD if kind is None else kind
    way = (end - start) / np.linalg.norm(end - start)
    across = np.array([-way[2], 0.0, way[0]])
    side = across / np.linalg.norm(across) * width / 2  # to the left of way, looking down
    a, b, c, d = start + side, end + side, end - side, start - side
    up = np.cross(way, across)
    up = up / np.linalg.norm(up) * (1 if up[1] > 0 else -1)
    tris = [(a, b, c, up, kind << 8), (a, c, d, up, kind << 8)]
    tris = [t if np.cross(t[1] - t[0], t[2] - t[0])[1] > 0 else (t[0], t[2], t[1], t[3], t[4]) for t in tris]
    if rail:
        lift = np.array([0.0, rail, 0.0])
        for p, q in ((a, b), (c, d)):
            tris += wall_quad(p, q, lift, (start + end) / 2)
    return tris


def log_road(arc, pos, rot):
    """The road on Bowser Castle's turning log: its own upper faces, as KCL triangles. A flat strip as wide
    as the log rides on air at its edges."""
    out = []
    for _, tri in nitro3d.Model(arc['MapObj/rotary_bridge.nsbmd']).triangles():
        a, b, c = [pos + rotate(np.array(v[0]) * MODEL_TO_WORLD, rot) for v in tri]
        n = np.cross(b - a, c - a)
        n = n / max(np.linalg.norm(n), 1e-9)
        if n[1] > ROTARY_BRIDGE_UP:
            out.append((a, b, c, n, KCL_ROAD << 8))
    return out


def built_road(arc, nkm):
    """Road the converter adds where MKDS has a moving thing: what collision_check takes as ground."""
    out = floor_discs(nkm)
    for object_id, pos, rot, size in mkdsmap.placed_objects(nkm):
        if object_id == ROTARY_BRIDGE:
            out += log_road(arc, pos, rot)
        elif object_id in WHEELS:
            out += placed_wheel(object_id, pos, rot, size)
    return out


def road_edges(kcl, loops, place):
    """Low walls along the road's open edges inside place, (x, z, radius): KCL triangles. An edge is open
    when no road lies just past it: neighbouring KCL triangles need not share their corners exactly, and
    the road goes on into a loop, which has no collision (Course.loops)."""
    if place is None:
        return []
    road = [t for t in kcl if mkdsmap.kcl_type(t[4]) in ROAD_KINDS]
    ground = [t for t in road + loops if abs(t[3][1]) > 0.01]
    walls = []
    lift = np.array([0.0, EDGE_HEIGHT, 0.0])
    for t in road:
        for p, q, other in ((t[0], t[1], t[2]), (t[1], t[2], t[0]), (t[2], t[0], t[1])):
            middle = (p + q) / 2
            if math.hypot(middle[0] - place[0], middle[2] - place[1]) >= place[2]:
                continue
            along = q - p
            out = np.array([along[2], 0.0, -along[0]]) / max(math.hypot(along[0], along[2]), 1e-9)
            if np.dot(out, other - middle) > 0:
                out = -out
            past = middle + out * EDGE_PROBE
            level = middle[1] - (t[3][0] * out[0] + t[3][2] * out[2]) * EDGE_PROBE / t[3][1]  # the slope goes on past it
            found = ground_under(past[0], past[2], max(middle[1], level) + EDGE_PROBE, ground)
            if found is None or level - found[0] > EDGE_PROBE:
                walls += wall_quad(p, q, lift, middle - out)
    return walls


def box_walls(corners):
    """The four sides of a box as KCL walls. corners: its eight corners, the bottom four first, round it."""
    middle = sum(corners) / 8
    tris = []
    for i in range(4):
        a, b, d = corners[i], corners[(i + 1) % 4], corners[i + 4]
        tris += wall_quad(a, b, d - a, 2 * a - middle)  # a mirrored box turns its corners the other way round
    return tris


def pillar(pos, radius, height):
    """Six outward-facing wall quads round pos, as KCL triangles, from below the ground up."""
    tris = []
    ring = [np.array([math.cos(i * math.pi / 3), 0.0, math.sin(i * math.pi / 3)]) * radius for i in range(6)]
    for i in range(6):
        a, b = pos + ring[i], pos + ring[(i + 1) % 6]
        low, high = np.array([0.0, -32.0, 0.0]), np.array([0.0, height, 0.0])
        for t in ((a + low, b + high, b + low), (a + low, a + high, b + high)):
            n = np.cross(t[1] - t[0], t[2] - t[0])
            tris.append((t[0], t[1], t[2], n / np.linalg.norm(n), KCL_WALL << 8))
    return tris


class LayeredWater:
    """The texture of a water sheet: MKDS draws its sparkle (waterA, see-through but for its foam) over
    its water (waterC). The two sorted against each other and against the sea bed as the view turned,
    so they are one opaque texture here."""

    def __init__(self, top, under):
        self.top, self.under = top, under

    def decode(self, tex, pal):
        top = self.top.decode(tex, pal)
        under = self.under[0].decode(self.under[1].tex, self.under[1].pal)
        ys = np.arange(top.shape[0]) * under.shape[0] // top.shape[0]
        xs = np.arange(top.shape[1]) * under.shape[1] // top.shape[1]
        under = under[ys][:, xs]
        out = np.where(top[..., 3:] > 127, top, under)
        out[..., 3] = 255
        return out


def tyre_model(arc, name):
    """A vehicle's tyres. MKDS draws one car_tire model, an axle, twice under the body: at (0, height, +along)
    and (0, height, -along), stretched across by width and grown by size (TYRES), and turns it about its
    own x 0x600 a tick (0x20e2a00). -> (its materials, its triangles about its own middle, (height, along))."""
    tyre = nitro3d.Model(arc['MapObj/car_tire.nsbmd'])
    tex0 = nitro3d.load_tex0(arc['MapObj/car_tire.nsbmd'])
    for m in tyre.mats:
        m.tex0 = tex0
        m.texmtx = TYRE_TEXTURE
    width, height, along, size = TYRES[name]
    tris = [(mi, [(tuple(np.array(v[0]) * MODEL_TO_WORLD * (width, size, size)), v[1], v[2], v[3]) for v in tri]) for mi, tri in tyre.triangles()]
    return list(tyre.mats), tris, (height, along)


class Course:
    def __init__(self, rom_files, name, scale):
        arc = nds.carc(rom_files[f'data/Course/{name}.carc'])
        tex = nds.carc(rom_files[f'data/Course/{name}Tex.carc'])
        self.name = name
        self.flare_arc = nds.carc(rom_files[f'data/Course/{FLARE_COURSE}.carc'])
        self.model = nitro3d.Model(arc['course_model.nsbmd'])
        self.kcl = mkdsmap.kcl(arc['course_collision.kcl'])
        # no collision: a flight carries a kart past these (find_flights)
        self.loop_road = [t for t in self.kcl if mkdsmap.kcl_type(t[4]) == KCL_LOOP]
        self.loops = [t for t in self.loop_road if t[3][1] < LOOP_ROAD]
        self.kcl = [t for t in self.kcl if not (mkdsmap.kcl_type(t[4]) == KCL_LOOP and t[3][1] < LOOP_ROAD)]
        self.cannons = [t for t in self.kcl if mkdsmap.kcl_type(t[4]) == KCL_CANNON]
        self.nkm = mkdsmap.nkm(arc['course_map.nkm'])
        self.route = mkdsmap.cpu_route(self.nkm)
        self.item_boxes = mkdsmap.item_boxes(self.nkm)
        self.wood_boxes = [pos for object_id, pos, _, _ in mkdsmap.placed_objects(self.nkm) if object_id == WOOD_BOX]
        # some courses keep their textures inside the model file
        course_tex0 = nitro3d.load_tex0(tex['course_model.nsbtx'] if 'course_model.nsbtx' in tex else arc['course_model.nsbmd'])
        for m in self.model.mats:
            m.tex0 = course_tex0
        self.mats = list(self.model.mats)
        self.triangles = [(mi, [(np.array(pos) * MODEL_TO_WORLD, uv, col, nrm) for pos, uv, col, nrm in tri])
                          for mi, tri in self.model.triangles()]
        self.overlays = self.find_overlays()
        self.ground_triangles = len(self.kcl)  # the course is centred on these, without the scenery's pillars
        self.baked = {}  # model name: (model, its first material)
        self.add_scenery(arc)
        self.sky = None
        if 'course_model_V.nsbmd' in arc and 'course_model_V.nsbtx' in tex:
            self.sky = (nitro3d.Model(arc['course_model_V.nsbmd']), nitro3d.load_tex0(tex['course_model_V.nsbtx']))
        self.far_mats = set()
        self.water_mats = set()
        self.sea_colour = None  # what lies under the horizon, on a course with a sea
        self.add_water(arc)
        self.add_backdrop(MAX_COORD / scale)

    def find_overlays(self):
        """Translucent decals that lie on the course's floor: arrow panels, tree shadows. A level texel is
        solid or clear, so such a texture is blended here over what lies under it. MKDS draws them in
        the plane of that floor, which depth testing cannot, so those rise by OVERLAY_LIFT.
        -> {material: the colour under it}"""
        corners = np.array([[v[0] for v in tri] for _, tri in self.triangles])
        materials = np.array([mi for mi, _ in self.triangles])
        tint = np.array([np.mean([v[2] for v in tri], axis=0) for _, tri in self.triangles]) / 255
        a, b, c = corners[:, 0], corners[:, 1], corners[:, 2]
        area = (b[:, 2] - c[:, 2]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 2] - c[:, 2])
        overlays = {}
        for mi in np.unique(materials):
            m = self.mats[mi]
            alpha = m.tex0.decode(m.tex, m.pal)[..., 3]
            if ((alpha > OVERLAY_MIN_ALPHA) & (alpha <= ALPHA_SOLID)).mean() < OVERLAY_SHARE:
                continue
            found = []
            for centre in corners[materials == mi].mean(1):
                with np.errstate(divide='ignore', invalid='ignore'):
                    u = ((b[:, 2] - c[:, 2]) * (centre[0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (centre[2] - c[:, 2])) / area
                    v = ((c[:, 2] - a[:, 2]) * (centre[0] - c[:, 0]) + (a[:, 0] - c[:, 0]) * (centre[2] - c[:, 2])) / area
                    w = 1 - u - v
                    gap = centre[1] - (u * a[:, 1] + v * b[:, 1] + w * c[:, 1])
                inside = (u >= 0) & (v >= 0) & (w >= 0) & (materials != mi) & (np.abs(gap) < OVERLAY_GAP)
                if inside.any():
                    near = np.flatnonzero(inside)[np.abs(gap[inside]).argmin()]
                    found.append((abs(gap[near]), near))
            if len(found) * 2 < (materials == mi).sum():
                continue
            colours = []
            for _, near in found:
                under = self.mats[materials[near]]
                colours.append(under.tex0.decode(under.tex, under.pal)[..., :3].reshape(-1, 3).mean(0) * tint[near])
            overlays[int(mi)] = tuple(np.mean(colours, axis=0))
            if np.median([g for g, _ in found]) < OVERLAY_LIFT:
                self.triangles = [(i, [(p + (0.0, OVERLAY_LIFT, 0.0), *rest) for p, *rest in tri] if i == mi else tri) for i, tri in self.triangles]
        return overlays

    def add_water(self, arc):
        """Bake the course's sheet of water: an object in MKDS, a flat layer (waterA) under a moving one (waterC)."""
        for object_id, pos, rot, size in mkdsmap.placed_objects(self.nkm):
            if object_id not in WATER_OBJECTS:
                continue
            file = next((f for end in ('waterA.nsbmd', 'waterC.nsbmd') for f in arc if f.endswith(end)), None)
            if file is None:
                print(f'water: object {object_id:#x} has no model in this course')
                continue
            model = nitro3d.Model(arc[file])
            tex0 = nitro3d.load_tex0(arc[file])
            lower = file.replace('waterA', 'waterC')
            if lower != file and lower in arc:
                lower_mat = next(m for m in nitro3d.Model(arc[lower]).mats if m.name not in DEPTH_ONLY_WATER)
                tex0 = LayeredWater(tex0, (nitro3d.load_tex0(arc[lower]), lower_mat))
            first = len(self.mats)
            sea = next(m for m in model.mats if m.name not in DEPTH_ONLY_WATER)
            self.sea_colour = tuple(tex0.decode(sea.tex, sea.pal)[..., :3].reshape(-1, 3).mean(0))
            for m in model.mats:
                m.tex0 = tex0
                m.polygon_attr |= 1 << 6  # seen from under it too: a kart falls in before it is lifted out
                self.far_mats.add(len(self.mats))
                self.water_mats.add(len(self.mats))
                self.mats.append(m)
            tris = [(mi, tri) for mi, tri in model.triangles() if model.mats[mi].name not in DEPTH_ONLY_WATER]
            # A flat sheet rises and falls in MKDS: Cheep Cheep Beach's tide stands over the sand at its
            # placed height. Baked, it stays out at low tide, under the lap, and no further: Delfino
            # Square's sea lies above the basin's floor, under the road.
            heights = [pos[1] + v[0][1] * MODEL_TO_WORLD * size[1] for _, tri in tris for v in tri]
            drop = 0.0
            if max(heights) - min(heights) < 1:
                drop = max(0.0, max(heights) - (min(p[1] for p, _ in self.route) - WATER_UNDER_ROUTE))
                # What lies under the opaque sheet is never seen, unless a block of it fails beside the
                # camera, and then it is a black floor where the sky under the horizon is the sea's colour.
                surface = max(heights) - drop
                self.triangles = [(mi, tri) for mi, tri in self.triangles if max(v[0][1] for v in tri) >= surface - WATER_KEEPS_BELOW]
            # Where a kart comes near, a sheet takes road-sized blocks: a block larger than the near
            # stage's depth has all its corners far from a camera over its middle, so it is drawn
            # by the far stage, which does not clip, and the water vanishes as the view turns.
            near = {}
            for mi, tri in tris:
                placed = [(pos + rotate_y(np.array(v[0]) * MODEL_TO_WORLD * size, rot[1]) - (0, drop, 0), v[1], v[2], v[3]) for v in tri]
                for piece, close in self.split_water(placed):
                    index = first + mi
                    if close:
                        if mi not in near:
                            near[mi] = len(self.mats)
                            self.mats.append(copy.copy(self.mats[first + mi]))
                            self.far_mats.discard(near[mi])
                        index = near[mi]
                    self.triangles.append((index, piece))

    def split_water(self, tri):
        """A triangle of a water sheet, halved until its pieces are WATER_NEAR_CELL long where a kart
        comes within WATER_NEAR of them. -> (corners, whether a kart comes near), for each piece."""
        route = np.array([p for p, _ in self.route])
        route = np.array([a + (b - a) * t / 8 for a, b in zip(route, np.roll(route, -1, 0)) for t in range(8)])[:, [0, 2]]
        done, todo = [], [tri]
        while todo:
            t = todo.pop()
            corners = np.array([v[0] for v in t])[:, [0, 2]]
            middle = corners.mean(0)
            reach = np.linalg.norm(corners - middle, axis=1).max()
            close = np.linalg.norm(route - middle, axis=1).min() - reach < WATER_NEAR
            lengths = [np.linalg.norm(corners[i] - corners[i - 1]) for i in range(3)]
            longest = int(np.argmax(lengths))
            if not close or lengths[longest] <= WATER_NEAR_CELL:
                done.append((t, close))
                continue
            a, b, c = t[longest - 1], t[longest], t[(longest + 1) % 3]
            mid = tuple((np.array(p, float) + np.array(q, float)) / 2 for p, q in zip(a, b))
            todo += [[a, mid, c], [mid, b, c]]
        return done

    def add_backdrop(self, limit):
        """Bake the backdrop: hills, woods, walls, clouds. A model of one flat colour is only a sky, which
        the level's gradient stands for at no cost in blocks."""
        if self.sky is None:
            return
        model, tex0 = self.sky
        if len(model.mats) == 1 and len(np.unique(tex0.decode(model.mats[0].tex, model.mats[0].pal).reshape(-1, 4), axis=0)) == 1:
            return
        tris = model.triangles()
        kcl = np.array([v for t in self.kcl[:self.ground_triangles] for v in t[:3]])
        centre = (kcl.min(0) + kcl.max(0)) / 2
        # What lies round the course stays where it is, so it keeps clear of the course as in MKDS; only
        # what is past that closes in, on each axis, up to what LEV positions hold. Scaled evenly,
        # a backdrop's walls came to stand in the course's rooms.
        points = np.array([v[0] for _, tri in tris for v in tri]) * MODEL_TO_WORLD - centre
        reach = np.abs(points).max(0)
        edge = 0.98 * limit
        near = np.minimum(np.abs(kcl - centre).max(0) * 1.02, edge * 0.95)

        def fit(p):
            d = p - centre
            a = np.abs(d)
            far = (a > near) & (reach > edge)
            out = np.where(far, near + (a - near) * (edge - near) / np.maximum(reach - near, 1e-6), a)
            return centre + np.sign(d) * out
        first = len(self.mats)
        for m in model.mats:
            m.tex0 = tex0
            self.far_mats.add(len(self.mats))
            self.mats.append(m)
        for mi, tri in tris:
            self.triangles.append((first + mi, [(fit(np.array(v[0]) * MODEL_TO_WORLD), v[1], v[2], v[3]) for v in tri]))

    def add_scenery(self, arc):
        """Bake the static objects into the drawn mesh and give each a solid pillar.

        Most are one flat quad that MKDS turns to the camera. Level blocks cannot turn, so such a
        billboard is drawn as two crossed quads, seen from both sides.
        """
        loaded = {}
        ground = [t for t in self.kcl if t[3][1] > 0.1]
        missing = set()
        placed = mkdsmap.placed_objects(self.nkm)
        placed += [(part, p + (0.0, over * s[1], 0.0), r, s) for o, p, r, s in placed for part, over in STACKED.get(o, ())]
        for object_id, pos, rot, size in placed:
            name = SCENERY.get(object_id)
            if name is None or object_id in mkds_movers.MOVERS or object_id in POKEY_PARTS or object_id in (POKEY_OBJECT, PIRANHA_HEAD, SNOWMAN_HEAD):
                continue
            file = f'MapObj/{name}.nsbmd'
            if object_id < STACKED_ID:
                mkds_objects.check(object_id, name)
            if file not in arc:
                missing.add(name)
                continue
            if name not in loaded:
                model = nitro3d.Model(arc[file])
                first = len(self.mats)
                tex0 = nitro3d.load_tex0(arc[file])
                tris = model.triangles(lit=True)
                flat = np.ptp([v[0][2] * MODEL_TO_WORLD for _, tri in tris for v in tri]) < FLAT_MODEL
                for m in model.mats:
                    m.tex0 = tex0
                    if flat:
                        m.polygon_attr |= 1 << 6  # both sides
                    self.mats.append(m)
                loaded[name] = (first, tris, flat)
            first, tris, flat = loaded[name]
            turns = (rot[1], rot[1] + 90) if flat and name not in WALL_MODELS else (rot[1],)
            base = np.array([0.0, 0.0, 0.0])
            if name not in HANGING_MODELS and object_id < STACKED_ID:
                base[1] = -min(0.0, min(v[0][1] for _, tri in tris for v in tri)) * MODEL_TO_WORLD * size[1]
            for turn in turns:
                for mi, tri in tris:
                    vs = [(pos + base + rotate_y(np.array(v[0]) * MODEL_TO_WORLD * size, turn), v[1], v[2], v[3]) for v in tri]
                    self.triangles.append((first + mi, vs))
            # most stand outside the walls, where the course has no ground and no kart can come
            shape = mkds_objects.OBJECTS.get(object_id)
            if shape is not None and shape.shape == 'cylinder' and ground_under(pos[0], pos[2], pos[1] + OBJECT_ABOVE_GROUND, ground) is not None:
                self.kcl += pillar(pos, shape.a * size[0], shape.b * size[1])
        self.kcl += turning(floor_discs(self.nkm)) + road_edges(self.kcl, self.loops, EDGED_ROAD.get(self.name))
        self.add_bridges(arc)
        self.add_still_objects(arc)
        self.add_movers(arc)
        for name in sorted(missing):
            print(f'scenery: no model {name} in this course')

    def bake(self, arc, name, place, left_out=(), top=math.inf, mirror=False, draw=True):
        """Draw a course object's model. place: model position -> world position. left_out: its parts to
        leave off, by material; top: and all above this height of the model. mirror: x is turned over.
        -> the model's box, as (lowest corner, highest corner)."""
        if name not in self.baked:
            model = nitro3d.Model(arc[f'MapObj/{name}.nsbmd'])
            tex0 = nitro3d.load_tex0(arc[f'MapObj/{name}.nsbmd'])
            self.baked[name] = (model, len(self.mats))
            for m in model.mats:
                m.tex0 = tex0
                self.mats.append(m)
        model, first = self.baked[name]
        points = []
        for mi, tri in model.triangles(lit=True):
            if model.mats[mi].name not in left_out and max(v[0][1] * MODEL_TO_WORLD for v in tri) <= top:
                points += [np.array(v[0]) * MODEL_TO_WORLD for v in tri]
                ordered = tri[::-1] if mirror else tri  # a mirrored triangle would face away
                if draw:
                    self.triangles.append((first + mi, [(place(np.array(v[0]) * MODEL_TO_WORLD), v[1], v[2], v[3]) for v in ordered]))
        points = np.array(points)
        return points.min(0), points.max(0)

    def add_still_objects(self, arc):
        """What moves in MKDS, drawn where it is placed: see STILL_OBJECTS."""
        ground = [t for t in self.kcl if t[3][1] > 0.1]
        for (object_id, pos, rot, size), setting, path in zip(mkdsmap.placed_objects(self.nkm), mkdsmap.object_settings(self.nkm), mkdsmap.object_paths(self.nkm)):
            if object_id in HOLES and f'MapObj/{HOLES[object_id]}.nsbmd' in arc:
                found = ground_under(pos[0], pos[2], pos[1] + OBJECT_ABOVE_GROUND, ground)
                floor = np.array([pos[0], (found[0] if found else pos[1]) + HOLE_LIFT, pos[2]])
                self.bake(arc, HOLES[object_id], lambda p, floor=floor, size=size: floor + p * size)
            name, solid, lift = STILL_OBJECTS.get(object_id, FLOOR_MODELS.get(object_id) and (FLOOR_MODELS[object_id], False, 0) or (None, 0, 0))
            if name is None:
                continue
            if solid and np.linalg.norm(pos - mkdsmap.start_positions(self.nkm)[0][0]) < START_CLEAR:
                continue
            if f'MapObj/{name}.nsbmd' not in arc:
                print(f'still object: no model {name} in this course')
                continue
            if object_id == BOUND and path is not None:
                continue
            at = pos + (0.0, lift, 0.0)
            if name in GROUNDED_STILL:
                at = at + (0.0, GROUNDED_STILL[name] * size[1], 0.0)
            mirror = bool(setting) and object_id in MIRRORED_BY_SETTING
            def place(p, at=at, rot=rot, size=size, x=-1.0 if mirror else 1.0):
                return at + rotate(p * size * (x, 1.0, 1.0), rot)
            moves = object_id in mkds_movers.MOVERS
            if moves and name not in mkds_movers.SOLID_STILL:
                continue
            low, high = self.bake(arc, name, place, mirror=mirror, draw=not moves)
            if object_id in WHEELS:
                self.kcl += placed_wheel(object_id, pos, rot, size)
            elif solid:
                corners = [np.array([x, y, z]) for y in (low[1], high[1]) for x, z in ((low[0], low[2]), (high[0], low[2]), (high[0], high[2]), (low[0], high[2]))]
                self.kcl += box_walls([place(c) for c in corners])

    def add_movers(self, arc):
        """What moves in MKDS and moves here too: see mkds_movers. Left to `convert` to scale and write."""
        self.movers, self.mover_models = [], {}
        table = dict(mkds_movers.MOVERS)
        table.update({POKEY_OBJECT: POKEY_MOVER, **POKEY_PARTS})
        for (object_id, pos, rot, size), path, settings in zip(mkdsmap.placed_objects(self.nkm), mkdsmap.object_paths(self.nkm), mkdsmap.all_object_settings(self.nkm)):
            shots = FIRE_PLANT_SHOTS if object_id == FIRE_PLANT else ()
            parts = [(object_id, 0.0)] + [(part, over) for part, over in STACKED.get(object_id, ())] + [(FIRE_PLANT_SHOT, 0.0)] * (1 if shots else 0)
            table[FIRE_PLANT_SHOT] = FIRE_PLANT_MOVER
            table[PIRANHA_HEAD] = mkds_movers.FIRE_PLANT_HEAD
            table[PIRANHA_MOUTH] = mkds_movers.FIRE_PLANT_MOUTH
            table[SNOWMAN_HEAD] = SNOWMAN_HEAD_MOVER
            table[COFFIN] = COFFIN_MOVER
            for part, over in parts:
                spec = table.get(part)
                if part == BURNER and path is not None:
                    spec = mkds_movers.BURNER_ON_PATH
                if part == BOUND and path is not None:
                    spec = mkds_movers.BOUND_ON_PATH
                if part == GEAR and not settings[WHITE_GEAR_SETTING]:
                    spec = spec._replace(model='gear_white')
                if spec is None:
                    continue
                if spec.model is None:
                    self.movers.append({'bullet': None, 'key': None, 'spec': spec, 'pos': pos, 'rot': rot, 'size': size, 'path': path, 'settings': settings, 'over': 0.0, 'id': part})
                    continue
                if f'MapObj/{spec.model}.nsbmd' not in arc:
                    print(f'mover: no model {spec.model} in this course')
                    continue
                if spec.kind[0] == mkds_movers.PATH and path is None:
                    continue
                mirror = bool(settings[0]) and object_id in MIRRORED_BY_SETTING
                key = (spec.model, mirror)
                if key not in self.mover_models:
                    if part < STACKED_ID:
                        mkds_objects.check(part, spec.model)
                    model = nitro3d.Model(arc[f'MapObj/{spec.model}.nsbmd'])
                    tex0 = nitro3d.load_tex0(arc[f'MapObj/{spec.model}.nsbmd'])
                    for m in model.mats:
                        m.tex0 = tex0
                    tris = [(mi, [(tuple(np.array(v[0]) * MODEL_TO_WORLD), v[1], v[2], v[3]) for v in tri]) for mi, tri in model.triangles(lit=True)]
                    if spec.model in mkds_movers.ANIMATED:
                        places, _ = mkds_movers.animate(model, nitro3d.JointAnimation(arc[f'MapObj/{spec.model}.nsbca']))
                        places = iter(places * MODEL_TO_WORLD)
                        tris = [(mi, [(*v, next(places)) for v in tri]) for mi, tri in tris]
                    self.mover_models[key] = (model.mats, tris)
                tyres = None
                if spec.model in mkds_movers.TYRED:
                    tyre_mats, tyre_tris, axles = tyre_model(arc, spec.model)
                    tyres = ('car_tire ' + spec.model, False), axles
                    self.mover_models.setdefault(tyres[0], (tyre_mats, tyre_tris))
                bullet = None
                if spec.model == 'NsKiller1':
                    bullet = ('NsKiller2', False)
                    self.load_part_model('NsKiller2', arc)
                if part in CHAIN_CHOMPS:
                    self.load_part_model('wanwan_chain', arc)
                if part == SUN:
                    self.load_part_model(FLARE_MODEL, self.flare_arc)
                self.movers.append({'tyres': tyres, 'bullet': bullet, 'key': key, 'spec': spec, 'pos': pos + (0.0, over * size[1], 0.0), 'rot': rot, 'size': size, 'path': path, 'settings': settings, 'over': over, 'id': part,
                                    'reacts': REACTS_AS.get(part, part), 'struck': STRUCK_AS.get(part)})

    def load_part_model(self, name, arc):
        """A model an object brings with it: a cannon's shot, a chain's link."""
        if (name, False) not in self.mover_models:
            model = nitro3d.Model(arc[f'MapObj/{name}.nsbmd'])
            tex0 = nitro3d.load_tex0(arc[f'MapObj/{name}.nsbmd'])
            for m in model.mats:
                m.tex0 = tex0
            self.mover_models[(name, False)] = (model.mats, [(mi, [(tuple(np.array(v[0]) * MODEL_TO_WORLD), v[1], v[2], v[3]) for v in tri]) for mi, tri in model.triangles(lit=True)])

    def add_bridges(self, arc):
        """What MKDS moves to carry the road over a gap, solid and standing still. A kart rides on these,
        so each is road in the collision as well as a model."""
        for object_id, pos, rot, size in mkdsmap.placed_objects(self.nkm):
            if object_id == OPEN_BRIDGE:
                tilt = (-OPEN_BRIDGE_ANGLE, rot[1])
                self.bake(arc, 'bridge', lambda p, pos=pos, size=size: pos + rotate(p * size, tilt), OPEN_BRIDGE_LEFT_OUT, OPEN_BRIDGE_TOP)
                tip = pos + rotate(np.array([0.0, 0.0, OPEN_BRIDGE_LENGTH]), tilt)
                self.kcl += road_strip(pos, tip, OPEN_BRIDGE_WIDTH, BRIDGE_RAIL, KCL_DASH)
            elif object_id == ROTARY_BRIDGE:
                self.kcl += turning(log_road(arc, pos, rot))
            elif object_id == SLAB_OBJECT:
                self.bake(arc, 'koopa_block', lambda p, pos=pos, size=size: pos + p * size)
                half = np.array([0.0, 0.0, SLAB_HALF_DEPTH * size[2]])
                self.kcl += road_strip(pos - half, pos + half, SLAB_HALF_WIDTH * 2 * size[0])

    def picture_view(self):
        """-> the Mods screen's camera and sky. The default looks down the road from the start."""
        if self.name in PICTURE_VIEWS:
            eye, at = np.array(PICTURE_VIEWS[self.name], dtype=float)
            return eye, at, self.sky_colours()
        start, _ = mkdsmap.start_positions(self.nkm)[0]
        points = np.array([p for p, _ in self.route])
        ahead = points[(np.argmin(np.linalg.norm(points - start, axis=1)) + PICTURE_AHEAD) % len(points)]
        way = (ahead - start) * (1.0, 0.0, 1.0)
        way = way / max(np.linalg.norm(way), 1.0)
        return start - way * PICTURE_BACK + (0.0, PICTURE_UP, 0.0), ahead, self.sky_colours()

    def picture(self, files):
        eye, at, sky = self.picture_view()
        image = mkds_view.render(files, self.name, eye, at, *track_picture.RENDER, sky=sky,
                                 objects=self.name in PICTURE_VIEWS)
        return track_picture.texels(image)

    def sky_colours(self):
        """(zenith, horizon) RGB from the skybox texture's top and bottom rows; a plain blue without one."""
        if not self.sky:
            return (40, 90, 200), (150, 200, 240)
        model, tex0 = self.sky
        # a sky model can hold clouds too: the sky is the most colourful of its textures
        def colourful(m):
            a = tex0.decode(m.tex, m.pal)[..., :3].astype(float)
            return (a.max(2) - a.min(2)).mean()
        m = max(model.mats, key=colourful)
        a = tex0.decode(m.tex, m.pal)[..., :3].astype(float)
        h = a.shape[0]
        horizon = tuple(a[-h // 8:].reshape(-1, 3).mean(0))
        # a sky texture can end in black under its horizon, where a course with a sea has water
        if self.sea_colour is not None and max(horizon) < DARK_HORIZON:
            horizon = self.sea_colour
        return tuple(a[:h // 8].reshape(-1, 3).mean(0)), horizon


def reduce_colours(im, colours):
    """An RGB image in so many colours. k-means after the median cut halves the error of a fast octree
    over the 439 textures of the 32 courses, and a road in 16 colours no longer falls apart in blotches."""
    return im.quantize(colours, method=Image.Quantize.MEDIANCUT, kmeans=8).convert('RGB')


def quantize(img):
    """RGBA uint8 array -> PIL image with at most 256 colours (16 when that loses little)."""
    im = Image.fromarray(img[..., :3], 'RGB')
    alpha = img[..., 3]
    # a smooth wide gradient (Rainbow Road) turns to blotches in 16 colours: it takes 256 at half the width
    if img.shape[1] > 128:
        rough = np.array(reduce_colours(im, 15))
        if np.abs(rough[..., :3].astype(int) - img[..., :3].astype(int))[alpha > ALPHA_SOLID].mean() > WIDE_ROUGH if (alpha > ALPHA_SOLID).any() else False:
            return quantize(img[:, ::2])
    # one PS1 texture page is 64 VRAM words wide: 256 texels at 4 bits, 128 at 8. CTE refuses wider and
    # leaves the material with no texture, so transparency, a palette entry of its own, counts here
    clear = int((alpha <= ALPHA_SOLID).any())
    for colours in ((16,) if img.shape[1] > 128 else (16, 256)):
        qa = np.array(reduce_colours(im, colours - clear).convert('RGBA'))
        err = np.abs(qa[..., :3].astype(int) - img[..., :3].astype(int))[alpha > ALPHA_SOLID].mean() if (alpha > ALPHA_SOLID).any() else 0
        if err < 3.0 or colours == 256 or img.shape[1] > 128:
            break
    qa[alpha <= ALPHA_SOLID] = 0  # all clear texels are one palette entry
    return Image.fromarray(qa, 'RGBA'), colours


def fit_to_lev(course, centre, limit):
    """LEV positions are 16 bits, and a few courses reach further than that from their centre: sea and
    backdrop, not the road. A triangle wholly past the limit is dropped, and a corner of one that crosses
    it moves to the limit. limit: MKDS units either way from centre. -> triangles dropped."""
    low, high = centre - limit, centre + limit
    def past(points):
        p = np.array(points)
        return bool(np.any(np.all(p > high, axis=0) | np.all(p < low, axis=0)))
    def fit(p):
        return np.clip(p, low, high)
    kept = [t for t in course.kcl if not past(t[:3])]
    dropped = len(course.kcl) - len(kept)
    course.ground_triangles -= sum(past(t[:3]) for t in course.kcl[:course.ground_triangles])
    course.kcl = [(fit(a), fit(b), fit(c), n, attr) for a, b, c, n, attr in kept]
    drawn = [(mi, tri) for mi, tri in course.triangles if not past([v[0] for v in tri])]
    dropped += len(course.triangles) - len(drawn)
    course.triangles = [(mi, [(fit(p), uv, col, nrm) for p, uv, col, nrm in tri]) for mi, tri in drawn]
    return dropped


def convert(course, out_dir, name, scale, max_edge, crate, fruit_crate, vram_share):
    os.makedirs(out_dir, exist_ok=True)
    tex_dir = out_dir  # CTE resolves map_Kd next to the OBJ only
    os.makedirs(tex_dir, exist_ok=True)

    # world placement: centre the course, MKDS units -> CTE units
    kpts = np.array([v for t in course.kcl[:course.ground_triangles] for v in t[:3]])
    centre = (kpts.min(0) + kpts.max(0)) / 2
    outside = fit_to_lev(course, centre, MAX_COORD / scale)
    kpts = np.array([v for t in course.kcl[:course.ground_triangles] for v in t[:3]])

    def to_cte(p):
        q = (np.asarray(p) - centre) * scale
        return (q[0], q[1], q[2])  # OBJ and MKDS share axes (right-handed, Y up)

    to_lev = lambda p: np.array(to_cte(p)) * 64
    flights, flight_log, flight_paths, exit_walls = find_flights(course, to_lev, scale)
    collision_kcl = course.kcl
    if name in SHORTCUT_OPENINGS:
        low, high = np.array(SHORTCUT_OPENINGS[name]) / (64 * scale) + centre
        collision_kcl = list(open_shortcut(collision_kcl, low, high))
    # the start line, and the way across it
    m = course.nkm
    cps = mkdsmap.checkpoints(m)
    (x1, z1), (x2, z2), _, _ = cps[0]
    route = course.route
    start_pos, start_rot = mkdsmap.start_positions(m)[0]
    line_a, line_b = np.array([x1, z1]), np.array([x2, z2])
    along = (line_b - line_a) / np.linalg.norm(line_b - line_a)
    fwd = np.array([-along[1], along[0]])
    if np.dot(np.array([route[0][0][0], route[0][0][2]]) - line_a, fwd) < 0:
        fwd = -fwd
    seg_len = np.linalg.norm(line_b - line_a)
    map_image, ui_map = minimap(course.kcl + course.loops, to_lev, fwd, route)
    # what the game needs from the VRM besides the course: each gets a carrier block
    item_images = [g['image'] for g in crate.groups] + [map_image]
    lev_unit = scale * 64
    first_mover_image = len(item_images)
    mover_models, mover_halves, mover_index, images_in = [], [], {}, set()
    for key, (mats, tris) in course.mover_models.items():
        billboard = mkds_movers.is_flat([v[0] for _, tri in tris for v in tri]) and key[0] not in WALL_MODELS
        two_sided = any((m.polygon_attr >> 6) & 1 for m in mats)  # a picture may be wound away from the camera it turns to
        # built at its largest placed size, so no instance scales it up: the game multiplies a model's
        # scale by its instance's in 16 bits, and a big one overflows (Waluigi Pinball's bumpers are 10 times)
        largest = max([float(np.abs(m['size']).max()) for m in course.movers if key in (m['key'], (m.get('tyres') or (None,))[0])] + [1.0])
        grown = [(mi, [(tuple(np.array(v[0]) * largest), *v[1:4], *[np.asarray(f) * largest for f in v[4:]]) for v in tri]) for mi, tri in tris]
        pieces, half, middle, low = mkds_movers.build_model(key[0], grown, mats, lev_unit, key[1], billboard, two_sided)
        mover_index[key] = len(mover_halves)
        mover_halves.append((half / largest, middle / largest, [(len(mover_models) + n, centre / largest, up / largest) for n, (_, centre, up) in enumerate(pieces)],
                             low / largest, billboard, pieces[0][0].loop))
        for model, _, _ in pieces:
            mover_models.append(model)
            for g in model.groups:
                if id(g['image']) not in images_in:  # the pieces of a model share its textures
                    images_in.add(id(g['image']))
                    item_images.append(g['image'])
    course.mover_items = mover_models
    if course.wood_boxes:
        item_images += [g['image'] for g in fruit_crate.groups]

    # ---- materials and textures
    mtl = []
    mat_names = {}
    tiles = {}
    # Repeat a tiling texture inside its image so fewer triangles need cutting at tile edges, but
    # only as often as one triangle spans it, and only while every texture fits the level's VRAM.
    span = defaultdict(lambda: [1.0, 1.0])
    for mi, tri in course.triangles:
        m = course.mats[mi]
        for axis, size, sc in ((0, m.width, m.texmtx[0]), (1, m.height, m.texmtx[1])):
            vals = [v[1][axis] * sc / size for v in tri]
            span[mi][axis] = max(span[mi][axis], max(vals) - min(vals))
    def pow2_at_most(x):
        k = 1
        while k * 2 <= x: k *= 2
        return k
    for i, m in enumerate(course.mats):
        kx = pow2_at_most(min(256 // m.width, math.ceil(span[i][0]), 4)) if (m.teximage >> 16) & 1 else 1
        ky = pow2_at_most(min(256 // m.height, math.ceil(span[i][1]), 4)) if (m.teximage >> 17) & 1 else 1
        tiles[i] = [kx, ky]
    shrink = defaultdict(lambda: 1)  # the last resort, when even single tiles do not fit
    def vram_words(i):
        m = course.mats[i]
        w, h = min(m.width * tiles[i][0], 256) // shrink[i], min(m.height * tiles[i][1], 256) // shrink[i]
        return math.ceil(w / 4) * h  # 4-bit worst case; 8-bit textures are at most 128 wide
    budget = 14 * 64 * 256 * vram_share  # 14 free texture pages in a CTE VRM, less room for palettes
    budget -= sum(math.ceil(image.shape[1] / 4) * image.shape[0] for image in item_images)
    while sum(vram_words(i) for i in tiles) > budget:
        repeated = [i for i in tiles if max(tiles[i]) > 1]
        if repeated:
            i = max(repeated, key=vram_words)
            a = 0 if tiles[i][0] >= tiles[i][1] else 1
            tiles[i][a] //= 2
        else:
            shrink[max(tiles, key=vram_words)] *= 2
    blended = [i for i, m in enumerate(course.mats) if 0 < (m.polygon_attr >> 16) & 31 <= BLEND_ALPHA and m.tex]
    for i_mat, m in enumerate(course.mats):
        i = i_mat
        mname = f'v{i:02d}_{m.tex}'
        mat_names[i] = mname
        src = m.tex0.decode(m.tex, m.pal)[::shrink[i_mat], ::shrink[i_mat]]
        if i_mat in blended:  # a palette of its own, from a surface with this texture that stays solid
            src = src.copy()
            src[0, 0, 2] ^= 8
            # half of it is what shows, over the dark behind it: as bright as MKDS's own share of it
            share = ((m.polygon_attr >> 16) & 31) / 31
            src[..., :3] = np.clip(src[..., :3] * (share * 2), 0, 255).astype(np.uint8)
        if i_mat in course.overlays:
            alpha = src[..., 3:4].astype(float) / 255
            src = np.dstack([src[..., :3] * alpha + np.array(course.overlays[i_mat]) * (1 - alpha), np.where(src[..., 3] > OVERLAY_MIN_ALPHA, 255, 0)]).astype(np.uint8)
        kx, ky = tiles[i_mat]
        fx, fy = (m.teximage >> 18) & 1, (m.teximage >> 19) & 1
        rows = []
        for j in range(ky):
            row = []
            for i in range(kx):
                t = src
                if fx and i % 2: t = t[:, ::-1]
                if fy and j % 2: t = t[::-1, :]
                row.append(t)
            rows.append(np.concatenate(row, 1))
        tiled = np.concatenate(rows, 0)
        # a texture page is 256 texels each way: CTE refuses more, and the material ends up with no texture
        tiled = tiled[::math.ceil(tiled.shape[0] / 256), ::math.ceil(tiled.shape[1] / 256)]
        img, colours = quantize(np.ascontiguousarray(tiled))
        fn = f'{mname}.png'
        img.save(os.path.join(tex_dir, fn))
        mtl += [f'newmtl {mname}', f'map_Kd {fn}', '']
    # collision blocks: fully transparent (colour 0 on the PS1), so no draw path shows them
    grey = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    grey.save(os.path.join(tex_dir, 'collision.png'))
    for cname in sorted({c[0] for c in COLLISION.values()}):
        mtl += [f'newmtl {cname}', 'map_Kd collision.png', '']
    # CrashTeamEditor packs only what a block uses
    item_mats = []
    blend_mats = [f'blend{i}' for i in blended]
    for i, mat in zip(blended, blend_mats):
        mtl += [f'newmtl {mat}', f'map_Kd {mat_names[i]}.png', '']
    for i, image in enumerate(item_images):
        item_mats.append(f'item{i}')
        if i >= first_mover_image:  # as the course's textures: what CrashTeamEditor is given is in the colours it keeps
            saved = quantize(np.ascontiguousarray(image))[0]
            if saved.size != (image.shape[1], image.shape[0]):
                sys.exit(f'item{i}: its layouts are for {image.shape[1]} by {image.shape[0]}, and it was saved at {saved.size}')
            saved.save(os.path.join(tex_dir, f'item{i}.png'))
        else:
            Image.fromarray(image, 'RGBA').save(os.path.join(tex_dir, f'item{i}.png'))
        mtl += [f'newmtl item{i}', f'map_Kd item{i}.png', '']

    # ---- OBJ writer
    lines = ['mtllib ' + name + '.mtl']
    counts = {'v': 0, 'vt': 0, 'vn': 0}
    blocks = []  # (block name, material)

    def key(p):
        return tuple(round(c, 6) for c in p)  # LEV-grid positions are multiples of 1/64

    def write_triblock(bname, mat, tri, normal):
        """tri: 3 vertex dicts with pos (CTE units), uv (0..1, v up), col (0..1). Emits 4 sub-triangles."""
        a, b, c = tri
        ab, bc, ca = lerp(a, b, .5), lerp(b, c, .5), lerp(c, a, .5)
        verts = [a, b, c, ab, bc, ca]
        base_v, base_t = counts['v'], counts['vt']
        for v in verts:
            p = key(v['pos'])
            lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
            lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 6; counts['vt'] += 6; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        for f in ((0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)):
            lines.append('f ' + ' '.join(f'{base_v + i + 1}/{base_t + i + 1}/{n}' for i in f))
        blocks.append((bname, mat))

    def write_quadblock(bname, mat, quad, normal):
        """quad: 4 vertex dicts in winding order. Emits the 3x3 grid as 4 quads (CTE reads the
        corner quads in grid order: p0, p2, p6, p8)."""
        q0, q1, q2, q3 = quad
        c = {k: (q0[k] + q1[k] + q2[k] + q3[k]) / 4 for k in q0}
        # grid: 0 1 2 / 3 4 5 / 6 7 8 with corners 0=q0, 2=q1, 8=q2, 6=q3
        g = [q0, lerp(q0, q1, .5), q1, lerp(q3, q0, .5), c, lerp(q1, q2, .5), q3, lerp(q2, q3, .5), q2]
        base_v, base_t = counts['v'], counts['vt']
        for v in g:
            p = key(v['pos'])
            lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
            lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 9; counts['vt'] += 9; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        # each sub-quad wound like the quad: q0 -> q1 -> q2 -> q3 is 0 -> 2 -> 8 -> 6
        for f in ((0, 1, 4, 3), (1, 2, 5, 4), (3, 4, 7, 6), (4, 5, 8, 7)):
            lines.append('f ' + ' '.join(f'{base_v + i + 1}/{base_t + i + 1}/{n}' for i in f))
        blocks.append((bname, mat))

    def write_grid(bname, mat, faces, normal):
        """faces: 4 quads in cyclic order around the shared centre, each starting at it. CTE wants
        the face order q0, q1, q2, q3 with q0 next to q1 and q2, and q3 opposite q0."""
        base_v, base_t = counts['v'], counts['vt']
        for face in (faces[0], faces[1], faces[3], faces[2]):
            for v in face:
                p = key(v['pos'])
                lines.append('v %.6f %.6f %.6f %.4f %.4f %.4f' % (p + tuple(v['col'])))
                lines.append('vt %.5f %.5f' % (min(max(v['uv'][0], 0), 1), min(max(v['uv'][1], 0), 1)))
        lines.append('vn %.4f %.4f %.4f' % tuple(normal))
        counts['v'] += 16; counts['vt'] += 16; counts['vn'] += 1
        n = counts['vn']
        lines.append(f'o {bname}')
        lines.append(f'usemtl {mat}')
        for f in range(4):
            lines.append('f ' + ' '.join(f'{base_v + 4 * f + i + 1}/{base_t + 4 * f + i + 1}/{n}' for i in range(4)))
        blocks.append((bname, mat))

    def tri_normal(tri):
        a, b, c = (np.array(v['pos']) for v in tri)
        n = np.cross(b - a, c - a)
        l = np.linalg.norm(n)
        return n / l if l > 0 else np.array([0.0, 1.0, 0.0])

    # ---- render geometry
    stats = defaultdict(int)
    if outside:
        stats['triangles past the LEV limit, dropped'] = outside
    render = []
    for ti, (mi, tri) in enumerate(course.triangles):
        m = course.mats[mi]
        sx, sy, tx, ty = m.texmtx
        repeat_s, repeat_t = (m.teximage >> 16) & 1, (m.teximage >> 17) & 1
        flip_s, flip_t = (m.teximage >> 18) & 1, (m.teximage >> 19) & 1
        vs = []
        for pos, uv, col, nrm in tri:
            kx, ky = tiles[mi]
            u = (uv[0] * sx / m.width + tx) / kx
            v = (uv[1] * sy / m.height + ty) / ky
            vs.append({'pos': np.array(to_cte(pos)), 'uv': np.array([u, v]),
                       'col': np.array(col) / 255.0 * 0.5})
        if area(vs) < 1e-6:
            stats['degenerate'] += 1
            continue
        for poly in split_by_tiles(vs):
            for piece in fan(poly):
                if area(piece) < 1e-6:
                    continue
                cu = np.mean([p['uv'][0] for p in piece]); cv = np.mean([p['uv'][1] for p in piece])
                tu, tv = math.floor(cu), math.floor(cv)
                out = []
                for p in piece:
                    fu, fv = p['uv'][0] - tu, p['uv'][1] - tv
                    if not repeat_s: fu = min(max(p['uv'][0], 0), 1)
                    if not repeat_t: fv = min(max(p['uv'][1], 0), 1)
                    if flip_s and tiles[mi][0] == 1 and tu % 2: fu = 1 - fu
                    if flip_t and tiles[mi][1] == 1 and tv % 2: fv = 1 - fv
                    out.append({'pos': p['pos'], 'uv': np.array([fu, 1 - fv]), 'col': p['col']})
                render.append((out, mi))
    refined = refine(render, lambda mi: max_edge * (WATER_EDGE if mi in course.water_mats else FAR_EDGE if mi in course.far_mats else 1), weld_tol=1.5 / 64)
    refined, stats['render corners moved over an edge'] = overlap_tjunctions(refined, 2 / 64, 1.5 / 64)
    refined = [(tri, mi) for tri, mi in refined if area(tri) >= 1e-9]
    quads, tris = pair_quads(refined, max_dev=2 / 64)
    grids, quads = group_grids(quads, max_dev=2 / 64)
    for faces, mi in grids:
        stats['render grids'] += 1
        normal = sum(tri_normal(f[:3]) for f in faces)
        write_grid(f'g{stats["render grids"]:05d}', mat_names[mi], faces, normal / max(np.linalg.norm(normal), 1e-9))
    for quad, mi in quads:
        stats['render quads'] += 1
        quad = orient_quad(quad)
        Q = [np.asarray(v['pos'], float) for v in quad]
        normal = np.cross(Q[2] - Q[0], Q[3] - Q[1])  # the diagonals: defined even with a straight corner
        write_quadblock(f'q{stats["render quads"]:05d}', mat_names[mi], quad, normal / max(np.linalg.norm(normal), 1e-9))
    for tri, mi in tris:
        stats['render'] += 1
        write_triblock(f'r{stats["render"]:05d}', mat_names[mi], tri, tri_normal(tri))

    # ---- collision geometry

    def near_line(p):
        s = np.dot(np.array([p[0], p[2]]) - line_a, along)
        return -50 <= s <= seg_len + 50 and abs(p[1] - start_pos[1]) < 150

    start_quads, end_quads = [], []
    boost_blocks = {}
    jump_pads = []  # where each pad's middle is: CrashTeamEditor knows two triggers, and lev_patch sets these
    collision = []
    for a, b, c, nrm, attr in collision_kcl + exit_walls:
        ty = mkdsmap.kcl_type(attr)
        info = COLLISION.get(ty)
        if info is None:
            stats[f'kcl skipped type {ty:#x}'] += 1
            continue
        if (ty, (attr >> 5) & 7) in KCL_MUD:
            info = COLLISION['mud']
        cname, _, _, pathable = info
        tri = [{'pos': np.array(p), 'uv': np.array(uv), 'col': np.array([.5, .5, .5])}
               for p, uv in zip((a, b, c), ((0, 0), (1, 0), (0, 1)))]
        pieces = [tri]
        crossing = pathable and all(near_line(p) for p in (a, b, c))
        if crossing:
            d = [np.dot(np.array([p[0], p[2]]) - line_a, fwd) for p in (a, b, c)]
            if min(d) < -1e-3 and max(d) > 1e-3:
                for v in tri:
                    v['d'] = np.array([np.dot(np.array([v['pos'][0], v['pos'][2]]) - line_a, fwd)])
                pieces = [p for side in (True, False) for p in fan(clip(tri, 'd', 0, 0.0, side)) if len(p) == 3]
                for p in pieces:
                    for v in p: v.pop('d', None)
                for v in tri: v.pop('d', None)
        src_n = np.cross(np.subtract(b, a), np.subtract(c, a))
        src_n = src_n / (np.linalg.norm(src_n) or 1)
        collision += [(p, (cname, crossing, src_n)) for p in pieces if area(p) >= 1e-6]
    # Collision is checked by the game on integer LEV coordinates, so the mesh is snapped to that
    # grid here and checked as the game will test it. Splitting near-parallel source edges leaves
    # corners closer than the source weld, a unit or two apart on the grid: those are welded
    # again (GRID_WELD LEV units). A shared corner moves the same in every triangle that has it,
    # so welding opens no gap, and the sliver between the two corners collapses.
    GRID_WELD = 2
    reps = {}
    def grid_weld(p):
        q = np.round(np.asarray(to_cte(p)) * 64)
        cell = tuple((q // (2 * GRID_WELD)).astype(int))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for r in reps.get((cell[0] + dx, cell[1] + dy, cell[2] + dz), ()):
                        if np.linalg.norm(r - q) <= GRID_WELD:
                            return r / 64
        reps.setdefault(cell, []).append(q)
        return q / 64
    snapped = []
    for piece, meta in refine(collision, max_edge / scale, weld_tol=0.5, tjunction_tol=1.0):
        snapped.append(([{'pos': grid_weld(v['pos']), 'mk': np.asarray(v['pos'], float), 'uv': v['uv'], 'col': v['col']}
                         for v in piece], meta))
    snapped, stats['collision slivers closed'] = close_slivers(snapped, SLIVER_WIDTH / 64)
    for piece, (cname, crossing, src_n) in snapped:
            if area(piece) < 1e-9:
                stats['collision degenerate on the LEV grid'] += 1
                continue
            if np.dot(tri_normal(piece), src_n) < TURNED_COS:
                # turned over, the game would test it facing the wrong way; stood up across a road,
                # it is a wall that stops the kart dead
                stats['collision triangles turned by the LEV grid, dropped'] += 1
                continue
            stats['collision'] += 1
            bname = f'c{stats["collision"]:05d}'
            cte = [{'pos': v['pos'], 'uv': v['uv'], 'col': v['col']} for v in piece]
            write_triblock(bname, cname, cte, tri_normal(cte))
            if cname in BOOST_MATERIALS:
                boost_blocks[bname] = TRIGGER_SUPER_TURBO_PAD if cname == 'col_ramp' else TRIGGER_TURBO_PAD
            if cname == 'col_jump':
                jump_pads.append([int(round(c * 64)) for c in sum(v['pos'] for v in cte) / 3])
            if crossing:
                d = [np.dot(np.array([v['mk'][0], v['mk'][2]]) - line_a, fwd) for v in piece]
                touches = min(abs(x) for x in d) < 1e-2
                if touches and np.mean(d) > 0: start_quads.append(bname)
                if touches and np.mean(d) < 0: end_quads.append(bname)

    # ---- carrier blocks, under everything, hidden and out of reach like the rest of the collision
    # blocks (lev_patch finds them by their bounds and reads where their textures landed)
    low = np.ceil(np.array(to_cte(kpts.min(0))) * 64) / 64 - 8
    carriers = []
    item_mats = item_mats + blend_mats
    for i, mat in enumerate(item_mats):
        x0, x1, z0, z1 = low[0] + 4 * i, low[0] + 4 * i + 2, low[2], low[2] + 2
        corners = [np.array(c, float) for c in ((x0, low[1], z1), (x1, low[1], z1), (x1, low[1], z0), (x0, low[1], z0))]
        uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
        quad = [{'pos': c, 'uv': np.array(uv, float), 'col': np.array([.5, .5, .5])} for c, uv in zip(corners, uvs)]
        write_quadblock(f'i{i}', mat, quad, np.array([0.0, 1.0, 0.0]))
        carriers.append([int(round(c * 64)) for c in (x0, low[1], z0, x1, z1)])
    item_carriers = carriers[:len(item_mats) - len(blend_mats)]
    blend_carriers = carriers[len(item_carriers):]

    # ---- item boxes: one crate per MKDS box, on the ground under it
    ground = [t for t in course.kcl if (i := COLLISION.get(mkdsmap.kcl_type(t[4]))) and i[2] & GROUND and abs(t[3][1]) >= 0.3]
    crate_ground = [t for t in ground if not COLLISION[mkdsmap.kcl_type(t[4])][2] & KILL_PLANE]
    crates = []
    for pos in course.item_boxes:
        found = ground_under(pos[0], pos[2], pos[1] + ROUTE_ABOVE_GROUND, crate_ground)  # MKDS sinks some into the road
        if found is None:  # over a gap: a kart crosses it at the height of the road beside it, and only a leaf's hitboxes are tested
            nearest = min(crate_ground, key=lambda t: math.hypot(*(t[:3][0] + t[:3][1] + t[:3][2] - 3 * np.array(pos))[[0, 2]]))
            found = (float(np.mean([v[1] for v in nearest[:3]])),)
            stats['item boxes over a gap, at the height of the ground beside them'] += 1
        q = to_lev((pos[0], found[0], pos[2]))
        crates.append([int(round(q[0])), int(round(q[1])) + CRATE_LIFT, int(round(q[2]))])
    stats['item boxes'] = len(crates)
    fruit_crates = []  # MKDS's breakable wooden boxes: the crate CTR breaks
    for pos in course.wood_boxes:
        found = ground_under(pos[0], pos[2], pos[1] + ROUTE_ABOVE_GROUND, crate_ground)
        q = to_lev((pos[0], found[0] if found else pos[1], pos[2]))
        fruit_crates.append([int(round(q[0])), int(round(q[1])) + CRATE_LIFT, int(round(q[2]))])

    # ---- probe points for CTR_DEV_COLL_PROBE_IN: every 64 LEV units on every MKDS ground layer
    lev = 64 * scale
    step = 64 / lev
    pts, road_points = [], []
    for a, b, c, nrm, attr in collision_kcl:
        info = COLLISION.get(mkdsmap.kcl_type(attr))
        if info is None or not (info[2] & GROUND) or abs(nrm[1]) < 0.3:
            continue
        xs, zs = (a[0], b[0], c[0]), (a[2], b[2], c[2])
        for gx in np.arange(math.ceil(min(xs) / step) * step, max(xs), step):
            for gz in np.arange(math.ceil(min(zs) / step) * step, max(zs), step):
                d = lambda u, v: (v[0] - u[0]) * (gz - u[2]) - (v[2] - u[2]) * (gx - u[0])
                e = (d(a, b), d(b, c), d(c, a))
                if not (min(e) > 0 or max(e) < 0):
                    continue
                y = a[1] - (nrm[0] * (gx - a[0]) + nrm[2] * (gz - a[2])) / nrm[1]
                p = (np.array([gx, y, gz]) - centre) * lev
                pts.append('%d %d %d' % tuple(np.round(p).astype(int)))
                if not info[2] & KILL_PLANE and not attr & TURNING_ROAD:
                    road_points.append(pts[-1])
    json.dump([[*map(float, to_lev(p)), float(r) * lev] for p, r in route], open(os.path.join(out_dir, name + '_route.json'), 'w'))
    with open(os.path.join(out_dir, name + '_road.txt'), 'w') as f:
        f.write('\n'.join(road_points) + '\n')
    with open(os.path.join(out_dir, name + '_probe.txt'), 'w') as f:
        f.write('\n'.join(pts) + '\n')
    stats['probe points'] = len(pts)

    with open(os.path.join(out_dir, name + '.obj'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(out_dir, name + '.mtl'), 'w') as f:
        f.write('\n'.join(mtl) + '\n')

    # ---- presets
    pre = os.path.join(out_dir, name + '_presets')
    os.makedirs(pre, exist_ok=True)
    mats = {'header': 3, 'materials': sorted(set(mat_names.values()) | {c[0] for c in COLLISION.values()} | set(item_mats))}
    for i, mname in mat_names.items():
        mats[mname + '_quadflags'] = 0  # drawn only
        mats[mname + '_checkpoint'] = False
        # fences, rails and signs MKDS draws from both sides (polygon attribute bit 6, back faces)
        mats[mname + '_drawflags'] = bool((course.mats[i].polygon_attr >> 6) & 1)
    for cname, terrain, flags, pathable in COLLISION.values():
        mats[cname + '_terrain'] = terrain
        mats[cname + '_quadflags'] = flags
        mats[cname + '_checkpoint'] = True
        mats[cname + '_checkpointPathable'] = pathable
    for mat in item_mats:
        mats[mat + '_terrain'] = 'Asphalt'
        mats[mat + '_quadflags'] = _WALL
        mats[mat + '_checkpoint'] = True
        mats[mat + '_checkpointPathable'] = False
    json.dump(mats, open(os.path.join(pre, 'material.json'), 'w'), indent=1)

    # CrashTeamEditor raises a copy of each pad block as the trigger; the pad's look is the MKDS road
    turbo = {'header': PRESET_TURBO_PAD, 'turbopads': list(boost_blocks)}
    turbo.update({b + '_trigger': trigger for b, trigger in boost_blocks.items()})
    pad_preset = os.path.join(pre, 'turbopad.json')
    if boost_blocks:
        json.dump(turbo, open(pad_preset, 'w'), indent=1)
    elif os.path.exists(pad_preset):
        os.remove(pad_preset)
    stats['boost blocks'] = len(boost_blocks)

    # 8 spawns in two staggered columns behind the start, like MKDS's grid
    # a kart at game angle a faces (sin a, cos a) in x/z, and VehBirth turns the spawn's rot.y by
    # a quarter turn before using it
    heading = math.degrees(math.atan2(fwd[0], fwd[1])) - 90
    spawns = []
    side = np.array([-fwd[1], fwd[0]])
    for i in range(8):
        row, col = divmod(i, 2)
        off2 = -fwd * (60 + 70 * row + 35 * col) + side * (60 if col else -60)
        p = np.array([start_pos[0] + off2[0], start_pos[1] + 10, start_pos[2] + off2[1]])
        spawns.append({'pos': dict(zip('xyz', map(float, to_cte(p)))), 'rot': {'x': 0.0, 'y': heading, 'z': 0.0}})
    json.dump({'header': 0, 'spawn': spawns}, open(os.path.join(pre, 'spawn.json'), 'w'), indent=1)

    # sky: a gradient from the course's own skybox texture, in the order retail levels have it: band 0
    # highest, each from its upper edge to its lower one (positions are pixels above the horizon), running
    # past the horizon. CAM_SkyboxGlow only clips bands in that order; lev_patch sets the clear colours behind them.
    top, horizon = course.sky_colours()
    middle = (np.array(top) + horizon) / 2
    below = BELOW_HORIZON.get(name) or course.sea_colour or horizon  # a course with a sea has water under the horizon
    col = lambda c: {'r': c[0] / 255, 'g': c[1] / 255, 'b': c[2] / 255, 'a': 1.0}
    level = {'header': 1, 'configFlags': 1,
             'skyGradient': [{'posFrom': 160.0, 'posTo': 60.0, 'colorFrom': col(top), 'colorTo': col(middle)},
                             {'posFrom': 60.0, 'posTo': 0.0, 'colorFrom': col(middle), 'colorTo': col(horizon)},
                             {'posFrom': 0.0, 'posTo': -100.0, 'colorFrom': col(below), 'colorTo': col(below)}]}
    json.dump(level, open(os.path.join(pre, 'level.json'), 'w'), indent=1)

    path = {'header': 2, 'pathCount': 1, 'path0': {'index': 0, 'hasLeft': False, 'hasRight': False,
                                                    'quadStart': start_quads, 'quadEnd': end_quads, 'quadIgnore': []}}
    json.dump(path, open(os.path.join(pre, 'path.json'), 'w'), indent=1)

    # ---- CPU route: three lanes across the MKDS route, in CTE units, starting at the finish line
    pts = np.array([p for p, _ in route]); radii = np.array([r for _, r in route])
    seg = np.roll(pts, -1, 0) - pts
    lens = np.linalg.norm(seg, axis=1)
    total = lens.sum()
    count = max(64, min(400, int(total / 40)))
    sp = np.array([start_pos[0], start_pos[2]])
    # arc position of the finish line: closest point of the loop to the start
    best = (1e18, 0.0)
    acc = 0.0
    for i in range(len(pts)):
        a2, d2 = pts[i][[0, 2]], seg[i][[0, 2]]
        t = np.clip(np.dot(sp - a2, d2) / max(np.dot(d2, d2), 1e-9), 0, 1)
        dd = np.linalg.norm(a2 + d2 * t - sp)
        if dd < best[0]: best = (dd, acc + t * lens[i])
        acc += lens[i]
    cum = np.concatenate([[0], np.cumsum(lens)])

    def route_at(s):
        s = (best[1] + s) % total
        i = int(np.searchsorted(cum, s, side='right') - 1)
        return pts[i] + seg[i] * (s - cum[i]) / lens[i]

    fruit, dropped = fruit_rows(route_at, total, 64 * scale, crates, to_lev, ground)
    stats['wumpa fruit'] = len(fruit)
    stats['fruit rows dropped'] = dropped
    for line in flight_log:
        print('flight', line)
    stats['flights'] = len(flights)
    def settle(p, standing=[t for t in course.kcl if t[3][1] > 0.1]):
        found = ground_under(p[0], p[2], p[1] + OBJECT_ABOVE_GROUND, standing)
        return p if found is None else np.array([p[0], found[0], p[2]])
    def ground_height(p, floor=[t for t in course.kcl if t[3][1] > 0.1]):
        found = ground_under(p[0], p[2], p[1] + 40, floor)
        return None if found is None else found[0]
    routes = mkdsmap.all_object_paths(course.nkm)
    route_points = np.array([p for p, _ in course.route])
    motion_context = {
        'ground': ground_height, 'settled': settle, 'chain': 'wanwan_chain', 'flare': FLARE_MODEL, 'crate': mkds_movers.ITEM_BOX,
        'forward': lambda m: rotate(np.array([0.0, 0.0, 1.0]), m['rot']),
        'nearest_route': lambda p: route_points[np.argmin(np.linalg.norm(route_points - p, axis=1))],
        'fire_paths': lambda m: [routes[n].points for n in m['settings'][:7] if n < len(routes)],
        'drums': [(np.array(p, float), st) for (o, p, _, _), st in zip(mkdsmap.placed_objects(course.nkm), mkdsmap.all_object_settings(course.nkm)) if o == DRUM],
    }
    movers, mover_lists = mkds_movers.place(course.movers, mover_index, mover_halves, to_lev, lev_unit, rotate, settle, motion_context)
    stats['movers'], stats['blended materials'] = len(movers), len(blended)
    json.dump({'crates': crates, 'fruit': fruit, 'fruit_crates': fruit_crates, 'carriers': item_carriers, 'blends': blend_carriers, 'map': ui_map, 'jump_pads': jump_pads, 'flights': flights, 'movers': movers, 'mover_lists': mover_lists},
              open(os.path.join(out_dir, name + '_items.json'), 'w'))

    cells = {}
    for t in ground:
        if t[3][1] > 0:
            xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
            for gx in range(int(min(xs) // 256), int(max(xs) // 256) + 1):
                for gz in range(int(min(zs) // 256), int(max(zs) // 256) + 1):
                    cells.setdefault((gx, gz), []).append(t)
    def on_route(k, off):
        """Lane offset off's point at node k, and the way along the route there."""
        s_ = (best[1] + total * k / count) % total
        i = int(np.searchsorted(cum, s_, side='right') - 1)
        t = (s_ - cum[i]) / lens[i]
        d = seg[i] / lens[i]
        side = np.array([-d[2], 0.0, d[0]])
        side /= max(np.linalg.norm(side), 1e-9)
        return pts[i] + seg[i] * t + side * (radii[i] + (radii[(i + 1) % len(pts)] - radii[i]) * t) * off, d

    def ground_at(q):
        """The ground a CPU kart runs on at q: (height, material), or None over a gap. MKDS's route floats
        over the road and cuts through a ramp, so a ramp a little above it counts as well."""
        cell = cells.get((q[0] // 256, q[2] // 256), ())
        found = ground_under(q[0], q[2], q[1] + LANE_REACHES_RAMP, cell)
        if found is not None and (i := COLLISION.get(mkdsmap.kcl_type(found[1][4]))) and i[0] in BOOST_MATERIALS:
            return found[0], i[0], found[1][3]
        found = ground_under(q[0], q[2], q[1] + ROUTE_UNDER_BUMP, cell)
        if found is None or q[1] - found[0] > ROUTE_IN_THE_AIR:
            return None
        i = COLLISION.get(mkdsmap.kcl_type(found[1][4]))
        return found[0], i[0] if i else None, found[1][3]

    step = total / count
    lanes, lane_flags = [[], [], []], [[], [], []]
    for lane, off in enumerate((0.0, -0.4, 0.4)):
        heights, flags = [], []
        for k in range(count):
            # A CPU kart holds its lane's height, and takes what lies under its lane between two nodes
            flag = 0
            q, _ = on_route(k, off)
            found = ground_at(q)
            heights.append((q, found))
            for sub_step in range(LANE_SAMPLES):
                under = ground_at(on_route(k + sub_step / LANE_SAMPLES, off)[0])
                material = under[1] if under else None
                flag |= LANE_FLAGS.get(material, 0)
            flags.append(flag)
        for k, (q, found) in enumerate(heights):
            lanes[lane].append(np.array([q[0], found[0] if found else q[1], q[2]]))
        # Off a ramp or a jump pad a CPU kart flies as a player's does, and lands on the road
        for k, (q, found) in enumerate(heights):
            nxt = heights[(k + 1) % count][1]
            if not (found and found[1] in LANE_LAUNCHES) or (nxt and nxt[1] in LANE_LAUNCHES):
                continue
            n = found[2]
            rise = max(LANE_LAUNCHES[found[1]], math.sqrt(max(0.0, 1 - n[1] ** 2)) / max(n[1], 0.1)) * KART_SPEED
            for j in range(1, count // 4):
                t = j * step / KART_SPEED
                y = found[0] + rise * t - KART_GRAVITY * t * t / 2
                node = lanes[lane][(k + j) % count]
                if y <= node[1]:
                    break
                node[1] = y
        lane_flags[lane] = flags
        # Between two nodes a CPU kart runs straight, and over a crest that is under the ground, where
        # the depth test hides it: the nodes rise until the line clears the ground
        path = lanes[lane]
        for _ in range(LANE_CLEAR_PASSES):
            moved = False
            for k in range(count):
                a, b = path[k], path[(k + 1) % count]
                for sub_step in range(1, LANE_SAMPLES):
                    at = a + (b - a) * sub_step / LANE_SAMPLES
                    found = ground_at(at)
                    if found is not None and found[0] - at[1] > 1:
                        lift = found[0] - at[1]
                        a[1] += lift
                        b[1] += lift
                        moved = True
            if not moved:
                break
    # Where a player is carried a CPU kart keeps its lane, along the same path
    for path in flight_paths:
        path = np.array(path)
        along = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
        first = int(((np.array(lanes[0]) - path[0]) ** 2).sum(1).argmin())
        last = int(((np.array(lanes[0]) - path[-1]) ** 2).sum(1).argmin())
        nodes = (last - first) % count
        for n in range(nodes + 1):
            k = (first + n) % count
            at = along[-1] * n / max(nodes, 1)
            spot = np.array([np.interp(at, along, path[:, c]) for c in range(3)])
            centre_now = lanes[0][k].copy()
            for lane in range(3):
                lanes[lane][k] = spot + (lanes[lane][k] - centre_now) * (1.0, 0.0, 1.0)
    json.dump({'lanes': [[[float(c) for c in to_cte(q)] for q in lane] for lane in lanes], 'flags': lane_flags},
              open(os.path.join(out_dir, name + '_nav.json'), 'w'))
    stats['nav nodes'] = count

    stats['start quads'] = len(start_quads)
    stats['end quads'] = len(end_quads)
    allpos = np.array([v for t in course.kcl for v in t[:3]])
    ext = np.abs((allpos - centre) * scale).max()
    stats['max |coord| (limit 512)'] = round(float(ext), 1)
    return dict(stats)


def obj_vertices(path):
    """Distinct position and colour pairs in an OBJ: CrashTeamEditor's LEV vertices, to within a few."""
    with open(path) as f:
        return len({line for line in f if line.startswith('v ')})


def main():
    ap = argparse.ArgumentParser(description='Convert a Mario Kart DS course into an installed custom track package.')
    ap.add_argument('rom', help='the player\'s own Mario Kart DS ROM (.nds)')
    ap.add_argument('course', help='course archive name under data/Course, e.g. cross_course')
    ap.add_argument('--title', required=True, help='title shown on the custom tracks page')
    ap.add_argument('--disc', required=True, help='the player\'s own CTR (USA) disc image (.bin): the crate and fruit models come from it')
    ap.add_argument('--store', default=None,
                    help='packages folder to install into (default <work>/packages, where check_track.ps1 looks); '
                         'a game install\'s is assets/tracks/packages')
    ap.add_argument('--work', default=None, help='scratch folder for the OBJ, LEV and probe points (default C:/t/mkds-<course>)')
    ap.add_argument('--cte', default='C:/t/cte/cte_headless.exe', help='cte_headless.exe (tools/tracks/cte/build.sh)')
    ap.add_argument('--version', default='1.0.0', help='package version (semver)')
    ap.add_argument('--scale', type=float, default=1 / 12, help='MKDS units to CrashTeamEditor units')
    ap.add_argument('--max-edge', type=float, default=24.0,
                    help='longest triangle edge in CTE units (x64 = LEV units); retail quadblocks are ~900-1600 LEV units')
    a = ap.parse_args()

    work = a.work or f'C:/t/mkds-{a.course}'
    a.store = a.store or os.path.join(work, 'packages')
    os.makedirs(a.store, exist_ok=True)
    name = a.course
    files = nds.nitrofs(open(a.rom, 'rb').read())
    course = Course(files, a.course, a.scale)
    items = ctr_items.read(a.disc)
    lev_dir = os.path.join(work, 'lev')
    os.makedirs(lev_dir, exist_ok=True)  # CTE saves into an existing folder only
    # cte_headless links MinGW32's Python runtime
    env = dict(os.environ, PATH='C:/msys64/mingw32/bin' + os.pathsep + os.environ.get('PATH', ''))
    lev = os.path.join(lev_dir, name + '.lev')
    vrm = os.path.join(lev_dir, name + '.vrm')
    # Blocks grow until the vertices fit 16-bit indices, textures shrink until they fit the VRM
    max_edge, vram_share = a.max_edge, VRAM_SHARE
    for attempt in range(MAX_FIT_ATTEMPTS):
        for stale in (lev, vrm):
            if os.path.exists(stale):
                os.remove(stale)
        print('convert', convert(course, work, name, a.scale, max_edge, items['crates'], items['fruit_crates'], vram_share))
        if obj_vertices(os.path.join(work, name + '.obj')) > MAX_VERTICES:
            print(f'over {MAX_VERTICES} vertices at --max-edge {max_edge}')
            max_edge = round(max_edge * 1.25, 1)
            continue
        r = subprocess.run([a.cte, os.path.join(work, name + '.obj'), lev_dir], env=env, capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode != 0:
            sys.exit('cte_headless failed:\n' + r.stderr)
        if os.path.exists(vrm):
            break
        print(f'the textures do not fit the VRM at {vram_share} of it')
        vram_share = round(vram_share * 0.85, 3)
    else:
        sys.exit(f'nothing fits after {MAX_FIT_ATTEMPTS} attempts')
    print(f'fits at --max-edge {max_edge}, {vram_share} of the VRM')
    if lev_patch.vertex_count(lev) > 0xffff:
        sys.exit('more vertices than a quadblock can index')
    patched = os.path.join(lev_dir, name + '_patched.lev')
    patched_vrm = os.path.join(lev_dir, name + '_patched.vrm')
    placed = json.load(open(os.path.join(work, name + '_items.json')))
    items['movers'] = course.mover_items
    patched_stats = lev_patch.patch(lev, vrm, os.path.join(work, name + '_nav.json'), patched, patched_vrm, *course.sky_colours(), items, placed, BELOW_HORIZON.get(name) or course.sea_colour)
    print('lev_patch', patched_stats)
    if patched_stats['quadblocks over 2%']:
        sys.exit('collision normals the game cannot represent within 2% (the kart would stop against them)')
    winding = winding_check.check(os.path.join(work, name + '.obj'), patched)
    print('winding', winding)
    if winding['flipped'] or winding['unmatched']:
        sys.exit('CrashTeamEditor turned drawn blocks over (they would vanish from the front)')
    pin = package.build(patched, patched_vrm, a.title, 'mkds/' + a.course, a.version, a.store,
                       'Nintendo, converted by Decrypt', course.picture(files))
    # a re-conversion replaces the revision this work folder installed last
    pin_file = os.path.join(work, 'pin.txt')
    old = open(pin_file).read().strip() if os.path.exists(pin_file) else ''
    if old and old != pin and os.path.isdir(os.path.join(a.store, old)):
        shutil.rmtree(os.path.join(a.store, old))
    with open(pin_file, 'w') as f:
        f.write(pin + '\n')
    print('pin', pin)


if __name__ == '__main__':
    main()
