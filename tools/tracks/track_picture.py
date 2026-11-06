"""A converted track's picture for the Mods screen (docs/TRACK-CONVERTER.md#pictures).

The game shows a package's metadata/picture.ctp as it is: 112x42 PS1 15-bit texels, little-endian, row
by row. 112x42 looks 3:2 on screen (include/platform/native_track_picture.h). No texel is 0, which the GPU
draws as transparent.
"""
import numpy as np
from PIL import Image

WIDTH, HEIGHT = 112, 42
RENDER = (450, 300)  # the size to draw the course at: 3:2


def texels(image):
    """An image's picture file, as bytes."""
    small = np.array(image.convert('RGB').resize((WIDTH, HEIGHT), Image.Resampling.BOX), np.uint16)
    words = (small[..., 0] >> 3) | (small[..., 1] >> 3) << 5 | (small[..., 2] >> 3) << 10
    words[words == 0] = 1 << 10
    return words.astype('<u2').tobytes()
