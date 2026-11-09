"""Draw a Mario Kart DS course as the ROM holds it: the reference a converted course is compared with
(docs/TRACK-CONVERTER.md). Textures, vertex colours, texel and polygon alpha, and the sheet of water round the course; no sky. With --objects,
the objects that move (mkds_movers.MOVERS) stand where the course's map places them, at rest.

  python tools/tracks/mkds_view.py <rom.nds> <course> <out.png> eye_x eye_y eye_z at_x at_y at_z [--objects]   (MKDS units)
"""
import sys
import numpy as np, nds, nitro3d

from PIL import Image

MODEL_TO_WORLD = 16.0  # mkds_convert's
DEPTH_ONLY_WATER = {'Zwater'}  # mkds_convert's: drawn for the depth buffer alone


def placed_movers(arc):
    """-> (model, tex0, a function from a model point to the world) for each moving object the course's
    map places, as mkds_convert.add_movers picks its model."""
    import mkds_convert, mkds_movers, mkdsmap
    nkm = mkdsmap.nkm(arc['course_map.nkm'])
    out, models = [], {}
    for (object_id, pos, rot, size), settings in zip(mkdsmap.placed_objects(nkm), mkdsmap.all_object_settings(nkm)):
        spec = mkds_movers.MOVERS.get(object_id)
        if spec is None or spec.model is None:
            continue
        name = 'gear_white' if object_id == mkds_convert.GEAR and not settings[mkds_convert.WHITE_GEAR_SETTING] else spec.model
        if f'MapObj/{name}.nsbmd' not in arc:
            continue
        if name not in models:
            models[name] = (nitro3d.Model(arc[f'MapObj/{name}.nsbmd']), nitro3d.load_tex0(arc[f'MapObj/{name}.nsbmd']))
        mirror = -1.0 if settings[0] and object_id in mkds_convert.MIRRORED_BY_SETTING else 1.0
        out.append((*models[name], lambda p, pos=pos, rot=rot, size=size, mirror=mirror:
                    pos + mkds_convert.rotate(np.array(p) * MODEL_TO_WORLD * size * (mirror, 1.0, 1.0), rot)))
    return out


def placed_water(arc):
    """-> (model, tex0, a function from a model point to the world) for the sheet of water round the
    course: an object in MKDS (mkds_convert.WATER_OBJECTS), its flat layer (waterA) and the one under it
    (waterC), where the course's map places it."""
    import mkds_convert, mkdsmap
    nkm = mkdsmap.nkm(arc['course_map.nkm'])
    files = [f for f in arc if f.endswith(('waterA.nsbmd', 'waterC.nsbmd'))]
    out = []
    for object_id, pos, rot, size in mkdsmap.placed_objects(nkm):
        if object_id not in mkds_convert.WATER_OBJECTS:
            continue
        for f in files:
            out.append((nitro3d.Model(arc[f]), nitro3d.load_tex0(arc[f]), lambda p, pos=pos, rot=rot, size=size:
                        pos + mkds_convert.rotate_y(np.array(p) * MODEL_TO_WORLD * size, rot[1])))
    return out


def render(files, course, eye, at, W=800, H=450, FOV=108, sky=None, objects=False, depth=False):  # the game's far camera
    """-> the course from a camera, as an image. sky: (top colour, colour at the horizon), else black.
    depth: also return each pixel's distance from the camera (inf where nothing is drawn)."""
    a = nds.carc(files[f'data/Course/{course}.carc'])
    tex = nds.carc(files[f'data/Course/{course}Tex.carc']) if f'data/Course/{course}Tex.carc' in files else {}
    course_model = nitro3d.Model(a['course_model.nsbmd'])
    course_tex = nitro3d.load_tex0(tex['course_model.nsbtx'] if 'course_model.nsbtx' in tex else a['course_model.nsbmd'])
    drawn = [(course_model, course_tex, lambda p: np.array(p) * MODEL_TO_WORLD)] + placed_water(a)
    if objects:
        drawn += placed_movers(a)
    f = at - eye; f /= np.linalg.norm(f)
    r = np.cross(f, (0, 1, 0)); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    focal = (W / 2) / np.tan(np.radians(FOV / 2))
    near = 2.0


    def clip(poly):
        res = []
        for i in range(len(poly)):
            p, q = poly[i], poly[(i + 1) % len(poly)]
            dp, dq = p[2] - near, q[2] - near
            if dp >= 0:
                res.append(p)
            if dp * dq < 0:
                res.append(p + (q - p) * dp / (dp - dq))
        return res


    textures = {}
    def texture(m, t0, i):
        if (id(m), i) not in textures:
            mt = m.mats[i]
            textures[id(m), i] = t0.decode(mt.tex, mt.pal).astype(float) if mt.tex else None
        return textures[id(m), i]


    zbuf = np.full((H, W), np.inf)
    colour = np.zeros((H, W, 3))
    if sky is not None:
        for y in range(H):
            colour[y] = np.array(sky[0]) + (np.array(sky[1]) - sky[0]) * min(y / (H * 0.55), 1)


    def raster(mt, T, tri, translucent):
        if T is None:
            return
        sx, sy, tx, ty = mt.texmtx
        tw, th = T.shape[1], T.shape[0]
        rows = []
        for world, uv, col in tri:
            d = world - eye
            rows.append(np.array([d @ r, d @ u, d @ f, uv[0] * sx / mt.width + tx, uv[1] * sy / mt.height + ty, *col], float))
        poly = clip(rows)
        if len(poly) < 3:
            return
        for k in range(1, len(poly) - 1):
            tr = np.array([poly[0], poly[k], poly[k + 1]])
            z = tr[:, 2]
            px = W / 2 + focal * tr[:, 0] / z
            py = H / 2 - focal * tr[:, 1] / z
            x0, x1 = max(int(px.min()), 0), min(int(px.max()) + 1, W - 1)
            y0, y1 = max(int(py.min()), 0), min(int(py.max()) + 1, H - 1)
            if x0 > x1 or y0 > y1:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
            if abs(den) < 1e-9:
                continue
            l0 = ((py[1] - py[2]) * (gx + .5 - px[2]) + (px[2] - px[1]) * (gy + .5 - py[2])) / den
            l1 = ((py[2] - py[0]) * (gx + .5 - px[2]) + (px[0] - px[2]) * (gy + .5 - py[2])) / den
            l2 = 1 - l0 - l1
            ok = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ok.any():
                continue
            iz = l0 / z[0] + l1 / z[1] + l2 / z[2]
            depth = 1 / np.maximum(iz, 1e-9)
            ok &= depth < zbuf[gy, gx] + 0.5
            if not ok.any():
                continue
            def attr(c):
                return (l0 * tr[0, c] / z[0] + l1 * tr[1, c] / z[1] + l2 * tr[2, c] / z[2]) * depth
            s = attr(3); t = attr(4)
            teximg = mt.teximage
            rs, rt, fs, ft = (teximg >> 16) & 1, (teximg >> 17) & 1, (teximg >> 18) & 1, (teximg >> 19) & 1
            def wrap(c, rep, flip):
                if not rep:
                    return np.clip(c, 0, 0.9999)
                fl = np.floor(c)
                fr = c - fl
                return np.where(flip & (fl.astype(int) % 2 == 1), 0.9999 - fr, fr)
            ix = (wrap(s, rs, fs) * tw).astype(int).clip(0, tw - 1)
            iy = (wrap(t, rt, ft) * th).astype(int).clip(0, th - 1)
            tc = T[iy, ix]
            vc = np.dstack([attr(5), attr(6), attr(7)]) / 255.0
            rgb = tc[..., :3] * vc
            al = tc[..., 3] / 255.0 * (((mt.polygon_attr >> 16) & 31) / 31)
            if translucent:
                sel = ok & (al > 0.01)
                yy, xx = gy[sel], gx[sel]
                colour[yy, xx] = colour[yy, xx] * (1 - al[sel, None]) + rgb[sel] * al[sel, None]
            else:
                sel = ok & (al > 0.99)
                yy, xx = gy[sel], gx[sel]
                colour[yy, xx] = rgb[sel]
                zbuf[yy, xx] = depth[sel]


    solid, rest = [], []
    for m, t0, place in drawn:
        for mi, tri in m.triangles():
            if m.mats[mi].name in DEPTH_ONLY_WATER:
                continue
            T = texture(m, t0, mi)
            item = (m.mats[mi], T, [(place(pos), uv, col if col is not None else (255, 255, 255)) for pos, uv, col, _ in tri])
            if T is not None and (T[..., 3].min() < 250 or ((m.mats[mi].polygon_attr >> 16) & 31) < 31):
                rest.append(item)
            else:
                solid.append(item)
    for item in solid:
        raster(*item, False)
    # real screenshots show decals over a board of even alpha
    for item in sorted(rest, key=lambda e: e[1][..., 3].min() < 100):
        raster(*item, True)
    image = Image.fromarray(colour.clip(0, 255).astype('uint8'))
    return (image, zbuf) if depth else image


if __name__ == '__main__':
    rom, course, out = sys.argv[1:4]
    eye = np.array(list(map(float, sys.argv[4:7])))
    at = np.array(list(map(float, sys.argv[7:10])))
    render(nds.nitrofs(open(rom, 'rb').read()), course, eye, at, objects='--objects' in sys.argv).save(out)
