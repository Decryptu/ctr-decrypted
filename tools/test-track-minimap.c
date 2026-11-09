// gcc -std=c11 -Wall -Wextra -I include -o /tmp/test-track-minimap
//   tools/test-track-minimap.c platform/native_track_minimap.c
//
// The track select's custom minimap (platform/native_track_minimap.c): icons 3
// and 4 of a LEV are found through levTexLookup, their texels read from the
// VRM through the palette, and a converted track's single image (both icons
// alike) is kept once. The LEV and VRM are built here.
#include <platform/native_track_minimap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond)                                                       \
	do                                                                    \
	{                                                                     \
		checks++;                                                         \
		if (!(cond))                                                      \
		{                                                                 \
			failures++;                                                   \
			printf("FAIL line %d: %s\n", __LINE__, #cond);                \
		}                                                                 \
	} while (0)

static void w32(unsigned char *p, unsigned v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = v >> 24;
}

static void w16(unsigned char *p, unsigned v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

// A LEV of 0x200 data bytes: levTexLookup at 0x100, icons at 0x140.
// Each icon: u0, v0, w, h, 4-bit, page x 512 (tpage 8), palette at (0, 400).
static size_t MakeLev(unsigned char *lev, const int icons[][5], int count)
{
	memset(lev, 0, 0x208);
	w32(lev, 0x200);
	unsigned char *d = lev + 4;
	w32(d + 0x3c, 0x100);
	w32(d + 0x100, (unsigned)count);
	w32(d + 0x104, 0x140);
	for (int i = 0; i < count; i++)
	{
		unsigned char *icon = d + 0x140 + i * 0x20;
		w32(icon + 0x10, (unsigned)icons[i][0]);
		unsigned char *t = icon + 0x14;
		int u0 = icons[i][1], v0 = icons[i][2], w = icons[i][3], h = icons[i][4];
		t[0] = (unsigned char)u0;
		t[1] = (unsigned char)v0;
		w16(t + 2, (400u << 6) | 0);
		t[4] = (unsigned char)(u0 + w);
		t[5] = (unsigned char)v0;
		w16(t + 6, 8);
		t[8] = (unsigned char)u0;
		t[9] = (unsigned char)(v0 + h);
		t[10] = (unsigned char)(u0 + w);
		t[11] = (unsigned char)(v0 + h);
	}
	return 0x208;
}

// A packed VRM: the 4-bit page at (512, 0), every texel index (u + v) & 15,
// and a palette at (0, 400) whose entry i is 0x8000 | i.
static unsigned char *MakeVrm(size_t *size)
{
	const unsigned pw = 64, ph = 256;
	size_t img1 = 20 + (size_t)pw * ph * 2, img2 = 20 + 16 * 2;
	*size = 4 + 4 + img1 + 4 + img2 + 4;
	unsigned char *v = calloc(1, *size), *p = v;
	w32(p, 0x20);
	p += 4;
	w32(p, (unsigned)img1);
	p += 4;
	w16(p + 12, 512);
	w16(p + 14, 0);
	w16(p + 16, pw);
	w16(p + 18, ph);
	for (unsigned y = 0; y < ph; y++)
		for (unsigned x = 0; x < pw; x++)
		{
			unsigned word = 0;
			for (unsigned k = 0; k < 4; k++)
				word |= ((x * 4 + k + y) & 15) << (k * 4);
			w16(p + 20 + (y * pw + x) * 2, word);
		}
	p += img1;
	w32(p, (unsigned)img2);
	p += 4;
	w16(p + 12, 0);
	w16(p + 14, 400);
	w16(p + 16, 16);
	w16(p + 18, 1);
	for (unsigned i = 0; i < 16; i++)
		w16(p + 20 + i * 2, 0x8000 | i);
	return v;
}

int main(void)
{
	static unsigned char lev[0x208];
	static struct NativeTrackMinimap map;
	size_t vrmSize;
	unsigned char *vrm = MakeVrm(&vrmSize);

	// Retail: two halves.
	const int retail[][5] = {{3, 8, 10, 40, 20}, {4, 8, 30, 36, 18}};
	size_t levSize = MakeLev(lev, retail, 2);
	CHECK(NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize, &map));
	CHECK(map.topWidth == 40 && map.topHeight == 20 && map.bottomWidth == 36 && map.bottomHeight == 18);
	CHECK(map.texels[0] == (0x8000 | ((8 + 10) & 15)));
	CHECK(map.texels[5 * NATIVE_TRACK_MINIMAP_SIDE + 7] == (0x8000 | ((8 + 7 + 10 + 5) & 15)));
	CHECK(map.texels[20 * NATIVE_TRACK_MINIMAP_SIDE + 3] == (0x8000 | ((8 + 3 + 30) & 15)));

	// Converted: one image in both icons, kept once as the bottom half.
	const int converted[][5] = {{3, 0, 0, 95, 95}, {4, 0, 0, 95, 95}};
	levSize = MakeLev(lev, converted, 2);
	CHECK(NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize, &map));
	CHECK(map.topHeight == 0 && map.bottomWidth == 95 && map.bottomHeight == 95);

	// No map icons, or a map too tall: none.
	const int other[][5] = {{7, 0, 0, 10, 10}};
	levSize = MakeLev(lev, other, 1);
	CHECK(!NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize, &map));
	const int tall[][5] = {{3, 0, 0, 40, 100}, {4, 0, 100, 40, 100}};
	levSize = MakeLev(lev, tall, 2);
	CHECK(!NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize, &map));

	// A truncated VRM: none.
	levSize = MakeLev(lev, retail, 2);
	CHECK(!NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize - 10, &map));

	free(vrm);
	printf("%d/%d checks passed\n", checks - failures, checks);
	return failures != 0;
}
