"""Turn a CTR_DEV_VRAM_DUMP file (1024x512 raw 15-bit PSX pixels) into a PNG.

Each 16-bit word is drawn as its 15-bit colour, so palettes show as short
coloured rows and 4/8-bit texture pages as noise; words that are exactly 0 are
drawn magenta, which makes never-written areas easy to spot.

    python tools/decrypted/vram_png.py vram002000.bin [out.png]
"""
import struct
import sys

from PIL import Image


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    data = open(src, "rb").read()
    words = struct.unpack("<%dH" % (len(data) // 2), data)
    img = Image.new("RGB", (1024, 512))
    px = img.load()
    for i, w in enumerate(words):
        x, y = i % 1024, i // 1024
        if w == 0:
            px[x, y] = (255, 0, 255)
        else:
            px[x, y] = ((w & 31) << 3, ((w >> 5) & 31) << 3, ((w >> 10) & 31) << 3)
    img.save(out)
    print(out)


if __name__ == "__main__":
    main()
