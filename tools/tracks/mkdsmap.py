"""MKDS course data: KCL collision and NKM map (start, checkpoints, CPU route, objects)."""
import struct
import numpy as np


def s32(b, o): return struct.unpack_from('<i', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def fx32(b, o): return s32(b, o) / 4096.0
def vec32(b, o): return np.array([fx32(b, o), fx32(b, o + 4), fx32(b, o + 8)])
def vec16(b, o): return np.array([s16(b, o) / 4096.0, s16(b, o + 2) / 4096.0, s16(b, o + 4) / 4096.0])


def kcl(data):
    """-> list of (a, b, c, normal, attribute) triangles in world units."""
    pos_off, nrm_off, prism_off = u32(data, 0), u32(data, 4), u32(data, 8)
    block_off = u32(data, 12)
    tris = []
    for p in range(prism_off + 0x10, block_off, 0x10):
        h = fx32(data, p)
        pi, fn, e1, e2, e3, attr = struct.unpack_from('<HHHHHH', data, p + 4)
        a = vec32(data, pos_off + 12 * pi)
        n, n1, n2, n3 = (vec16(data, nrm_off + 6 * i) for i in (fn, e1, e2, e3))
        ca, cb = np.cross(n1, n), np.cross(n2, n)
        b = a + cb * (h / np.dot(cb, n3))
        c = a + ca * (h / np.dot(ca, n3))
        if np.isfinite([a, b, c]).all():  # a degenerate prism has no corners
            tris.append((a, b, c, n, attr))
    return tris


# KCL collision types (attribute >> 8 & 0x1f)
KCL_TYPES = {0x00: 'road', 0x01: 'slippery', 0x02: 'weak_offroad', 0x03: 'offroad', 0x04: 'sound',
             0x05: 'heavy_offroad', 0x06: 'slippery2', 0x07: 'boost', 0x08: 'wall', 0x09: 'invisible_wall',
             0x0a: 'out_of_bounds', 0x0b: 'fall', 0x0c: 'jump_pad', 0x0d: 'road_no_drivers',
             0x0e: 'wall_no_drivers', 0x0f: 'cannon', 0x10: 'edge_wall', 0x11: 'water_fall',
             0x12: 'boost_min_speed', 0x13: 'loop', 0x14: 'special_road', 0x15: 'wall3', 0x16: 'recalc_route'}


def kcl_type(attr):
    return (attr >> 8) & 0x1f


SECTION_SIZES = {b'OBJI': 0x3c, b'PATH': 4, b'POIT': 0x14, b'STAG': None, b'KTPS': 0x1c, b'KTPJ': 0x20,
                 b'KTP2': 0x1c, b'KTPC': 0x1c, b'KTPM': 0x1c, b'CPOI': 0x24, b'CPAT': 0x0c, b'IPOI': 0x14,
                 b'IPAT': 0x0c, b'EPOI': 0x18, b'EPAT': 0x0c, b'MEPO': 0x18, b'MEPA': 0x0c, b'AREA': 0x48,
                 b'CAME': 0x4c}


def nkm(data):
    assert data[:4] == b'NKMD'
    hlen = u16(data, 6)
    offs = [u32(data, 8 + 4 * i) for i in range((hlen - 8) // 4)]
    out = {}
    for o in offs:
        p = hlen + o
        magic = data[p:p + 4]
        if magic == b'STAG':
            continue
        n = u32(data, p + 4)
        size = SECTION_SIZES.get(magic)
        if size is None:
            continue
        out[magic.decode()] = [data[p + 8 + size * i:p + 8 + size * (i + 1)] for i in range(n)]
    return out


def start_positions(m):
    return [(vec32(e, 0), vec32(e, 12)) for e in m['KTPS']]


def cannons(m):
    """-> the point each cannon shoots a kart at, by the KCL variant of its wall."""
    return [vec32(e, 0) for e in m['KTPC']]


def checkpoints(m):
    """-> list of ((x1, z1), (x2, z2), next section info)."""
    return [((fx32(e, 0), fx32(e, 4)), (fx32(e, 8), fx32(e, 12)), u16(e, 0x1c), s16(e, 0x1e)) for e in m['CPOI']]


def paths(m, key):
    return [(u16(e, 0), u16(e, 2), list(e[4:7]), list(e[7:10])) for e in m[key]]


def cpu_route(m):
    """-> one lap of the CPU route as (point, radius). EPOI holds sections in no driving order, and EPAT
    links them, forking where a course does: follow each section's first link until the route closes."""
    sections = paths(m, 'EPAT')
    order, section = [], 0
    while section not in order:
        order.append(section)
        section = sections[section][2][0]
    order = order[order.index(section):]
    points = [(vec32(e, 0), fx32(e, 12)) for e in m['EPOI']]
    return [point for i in order for point in points[sections[i][0]:sections[i][0] + sections[i][1]]]


def objects(m):
    return [(u16(e, 0x24), vec32(e, 0), vec32(e, 12)) for e in m['OBJI']]


def placed_objects(m):
    """-> (object id, position, rotation in degrees, scale) per object."""
    return [(u16(e, 0x24), vec32(e, 0), vec32(e, 12), vec32(e, 0x18)) for e in m['OBJI']]


def object_settings(m, index=0):
    """-> a setting of each object, in placed_objects' order."""
    return [u16(e, 0x28 + 2 * index) for e in m['OBJI']]


def object_paths(m):
    """-> each object's path as (points, whether it loops), or None, in placed_objects' order."""
    routes, start = [], 0
    for e in m.get('PATH', []):
        count = u16(e, 2)
        routes.append(([vec32(p, 0) for p in m['POIT'][start:start + count]], bool(e[1])))
        start += count
    numbers = [s16(e, 0x26) for e in m['OBJI']]
    return [routes[r] if 0 <= r < len(routes) else None for r in numbers]


def all_object_settings(m):
    return [[u16(e, 0x28 + 2 * i) for i in range(8)] for e in m['OBJI']]


OBJ_ITEM_BOX, OBJ_MOVING_ITEM_BOX = 0x65, 0xc9


def item_boxes(m):
    return [pos for obj, pos, _ in objects(m) if obj == OBJ_ITEM_BOX]


def moving_item_boxes(m):
    """-> the points of each moving item box's path."""
    return [path[0] for (obj, _, _), path in zip(objects(m), object_paths(m)) if obj == OBJ_MOVING_ITEM_BOX and path]
