"""Thumbnail and banner for a converted course's Project Saphi submission (docs/CUSTOM_TRACKS.md).

Saphi's upload form needs both. It shows thumbnails at 300x200 and banners at 1400x200. The thumbnail
is the course from the start line (the Mods screen picture's camera, mkds_convert.Course.picture_view).
The banner is the course from above the road, with the track's name in the middle.

  python tools/tracks/saphi_art.py <rom.nds> <font.ttf> <out dir> [course ...]

course: archive names from mkds_batch.COURSES (default: all). The font is Lilita One (Google Fonts, OFL),
the site's title font. Writes <slug>-thumbnail.jpg and <slug>-banner.jpg.
"""
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
import nds, mkds_convert, mkds_view
from mkds_batch import COURSES

THUMB = (300, 200)
BANNER = (1400, 200)
BANNER_VIEW = (1400, 600)  # the banner is this view's middle band
BANNER_FOV = 70  # narrower than the game's 108, which stretches the sides of so wide a picture
# The banner's camera: the one of these (route points past the start, MKDS units above the road) whose
# band sees furthest over the course. Each looks OVER_LOOK route points further on.
OVER_CAMERAS = [(step, up) for step in (2, 4, 6, 9, 12, 16) for up in (300, 500, 800)]
OVER_LOOK = 6
OPEN_FAR = 6000  # MKDS units: a view that sees this far is open enough
DRAFT = 8  # the camera is picked on views this many times smaller
SUPER = 2  # drawn at twice the size, then scaled down: smooth edges
FILL_TOP, FILL_BOTTOM = (255, 236, 64), (255, 140, 0)  # the CTR title's yellow to orange
OUTLINE = (24, 16, 48)


def slug(title):
    return re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')


class Views:
    """One course's cameras and drawings."""

    def __init__(self, files, course):
        self.files, self.course = files, course
        c = mkds_convert.Course(files, course, 1 / 12)
        self.start_eye, self.start_at, self.sky = c.picture_view()
        start = mkds_convert.mkdsmap.start_positions(c.nkm)[0][0]
        self.points = np.array([p for p, _ in c.route])
        self.first = np.argmin(np.linalg.norm(self.points - start, axis=1))

    def over(self, step, up):
        """(eye, at) up above the route step points past the start, looking further on."""
        n = len(self.points)
        return (self.points[(self.first + step) % n] + (0.0, up, 0.0),
                self.points[(self.first + step + OVER_LOOK) % n])

    def draw(self, eye, at, size, fov=108, depth=False):
        return mkds_view.render(self.files, self.course, eye, at, *size, FOV=fov, sky=self.sky, objects=True, depth=depth)

    def thumbnail(self):
        w, h = THUMB
        return self.draw(self.start_eye, self.start_at, (w * SUPER, h * SUPER)).resize(THUMB, Image.Resampling.LANCZOS)

    def banner_view(self):
        """The banner's band, from the camera that sees furthest: a wall in front is close, and the void
        (nothing drawn) counts as nothing."""
        w, h = BANNER_VIEW[0] // DRAFT, BANNER_VIEW[1] // DRAFT
        band = slice((h - BANNER[1] // DRAFT) // 2, (h + BANNER[1] // DRAFT) // 2)
        def openness(camera):
            depth = self.draw(*self.over(*camera), (w, h), BANNER_FOV, depth=True)[1][band]
            return np.median(np.where(np.isfinite(depth), np.minimum(depth, OPEN_FAR), 0.0))
        eye, at = self.over(*max(OVER_CAMERAS, key=openness))
        w, h = BANNER_VIEW
        view = self.draw(eye, at, (w * SUPER, h * SUPER), BANNER_FOV).resize(BANNER_VIEW, Image.Resampling.LANCZOS)
        top = (h - BANNER[1]) // 2
        return view.crop((0, top, w, top + BANNER[1]))


def title_text(image, text, font_path, height):
    """text in the middle of image, CTR style: yellow-orange fill, dark outline, soft shadow."""
    W, H = image.size
    size = height
    font = ImageFont.truetype(font_path, size)
    while font.getbbox(text)[2] > W * 0.9:
        size -= 2
        font = ImageFont.truetype(font_path, size)
    stroke = max(3, size // 12)
    x0, y0, x1, y1 = font.getbbox(text, stroke_width=stroke)
    x, y = (W - (x1 - x0)) // 2 - x0, (H - (y1 - y0)) // 2 - y0
    shadow = Image.new('L', image.size, 0)
    ImageDraw.Draw(shadow).text((x + stroke, y + stroke * 2), text, font=font, fill=200, stroke_width=stroke)
    image.paste((0, 0, 0), mask=shadow.filter(ImageFilter.GaussianBlur(stroke)))
    ImageDraw.Draw(image).text((x, y), text, font=font, fill=OUTLINE, stroke_width=stroke, stroke_fill=OUTLINE)
    mask = Image.new('L', image.size, 0)
    ImageDraw.Draw(mask).text((x, y), text, font=font, fill=255)
    top, bottom = (np.array(c, float) for c in (FILL_TOP, FILL_BOTTOM))
    lo, hi = y + y0, y + y1  # the ramp spans the letters, not the image
    t = np.clip((np.arange(H) - lo) / max(hi - lo, 1), 0, 1)[:, None, None]
    fill = (top + (bottom - top) * t) * np.ones((1, W, 1))
    image.paste(Image.fromarray(fill.astype(np.uint8)), mask=mask)
    return image


def banner(views, title, font_path):
    band = views.banner_view()
    band = Image.blend(band, Image.new('RGB', band.size, (0, 0, 0)), 0.25)  # so the name reads
    return title_text(band, title, font_path, 120)


def main():
    rom, font_path, out = sys.argv[1:4]
    courses = sys.argv[4:] or list(COURSES)
    files = nds.nitrofs(open(rom, 'rb').read())
    os.makedirs(out, exist_ok=True)
    for course in courses:
        title = COURSES[course]
        name = slug(title)
        views = Views(files, course)
        views.thumbnail().save(os.path.join(out, name + '-thumbnail.jpg'), quality=92)
        banner(views, title, font_path).save(os.path.join(out, name + '-banner.jpg'), quality=92)
        print(name)


if __name__ == '__main__':
    main()
