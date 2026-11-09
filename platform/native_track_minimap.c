/* A custom track's minimap, read from its package before it loads
   (include/platform/native_track_minimap.h). */
#include <platform/native_track_minimap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_package.h>
#endif

#define VRAM_W 1024
#define VRAM_H 512
/* struct Level: levTexLookup (include/namespace_Level.h). */
#define LEV_TEX_LOOKUP 0x3c
/* struct Icon: global_IconArray_Index, then the TextureLayout. */
#define ICON_SIZE 0x20
#define ICON_INDEX 0x10
#define ICON_LAYOUT 0x14
#define MAP_TOP_ICON 3
#define MAP_BOTTOM_ICON 4

static uint32_t rd32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* One VRM image (a TIM-like header: x, y, w, h at 12) into vram. */
static int Blit(uint16_t *vram, const unsigned char *p, size_t n)
{
	if (n < 20)
		return 0;
	unsigned x = rd16(p + 12), y = rd16(p + 14), w = rd16(p + 16), h = rd16(p + 18);
	if (!w || !h || x + w > VRAM_W || y + h > VRAM_H || n != 20 + (size_t)w * h * 2)
		return 0;
	for (unsigned row = 0; row < h; row++)
		for (unsigned col = 0; col < w; col++)
			vram[(y + row) * VRAM_W + x + col] = (uint16_t)rd16(p + 20 + ((size_t)row * w + col) * 2);
	return 1;
}

/* A VRM is one image, or 0x20 then images each led by their size, ended by 0. */
static int LoadVram(uint16_t *vram, const unsigned char *p, size_t n)
{
	if (n < 4)
		return 0;
	if (rd32(p) != 0x20)
		return Blit(vram, p, n);
	for (size_t at = 4;;)
	{
		if (at + 4 > n)
			return 0;
		size_t size = rd32(p + at);
		at += 4;
		if (!size)
			return 1;
		if (size > n - at || !Blit(vram, p + at, size))
			return 0;
		at += size;
	}
}

/* The LEV icon whose global index is wanted: its TextureLayout, or NULL. */
static const unsigned char *FindIcon(const unsigned char *lev, size_t levSize, int wanted)
{
	if (levSize < 8)
		return NULL;
	const unsigned char *data = lev + 4;
	size_t size = rd32(lev);
	if (size > levSize - 4 || size < LEV_TEX_LOOKUP + 4)
		return NULL;
	uint32_t lookup = rd32(data + LEV_TEX_LOOKUP);
	if (!lookup || lookup > size - 8)
		return NULL;
	uint32_t count = rd32(data + lookup), first = rd32(data + lookup + 4);
	if (!first || count > 4096 || first > size || (size_t)count * ICON_SIZE > size - first)
		return NULL;
	for (uint32_t i = 0; i < count; i++)
	{
		const unsigned char *icon = data + first + (size_t)i * ICON_SIZE;
		if ((int)rd32(icon + ICON_INDEX) == wanted)
			return icon + ICON_LAYOUT;
	}
	return NULL;
}

/* Copy one icon's texels, palette applied, to out at row y. Its width and
   height are returned. */
static int CopyIcon(const uint16_t *vram, const unsigned char *layout, struct NativeTrackMinimap *out, int y,
                    int *width, int *height)
{
	int u0 = layout[0], v0 = layout[1], u1 = layout[4], v2 = layout[9];
	unsigned clut = rd16(layout + 2), tpage = rd16(layout + 6);
	int w = u1 - u0, h = v2 - v0;
	if (w <= 0 || h <= 0 || w > NATIVE_TRACK_MINIMAP_SIDE || y + h > NATIVE_TRACK_MINIMAP_SIDE)
		return 0;
	int pageX = (int)(tpage & 0xf) * 64, pageY = (int)((tpage >> 4) & 1) * 256, depth = (int)((tpage >> 7) & 3);
	int clutX = (int)(clut & 0x3f) * 16, clutY = (int)(clut >> 6) & 0x1ff;
	if (depth == 3 || pageY + v0 + h > VRAM_H || clutX + (depth == 0 ? 16 : 256) > VRAM_W)
		return 0;
	for (int row = 0; row < h; row++)
	{
		const uint16_t *line = vram + (size_t)(pageY + v0 + row) * VRAM_W;
		for (int col = 0; col < w; col++)
		{
			int u = u0 + col, word = depth == 0 ? u / 4 : depth == 1 ? u / 2 : u;
			if (pageX + word >= VRAM_W)
				return 0;
			uint16_t t = line[pageX + word];
			if (depth == 0)
				t = vram[clutY * VRAM_W + clutX + ((t >> ((u & 3) * 4)) & 0xf)];
			else if (depth == 1)
				t = vram[clutY * VRAM_W + clutX + ((t >> ((u & 1) * 8)) & 0xff)];
			out->texels[(y + row) * NATIVE_TRACK_MINIMAP_SIDE + col] = t;
		}
	}
	*width = w;
	*height = h;
	return 1;
}

int NativeTrackMinimap_Extract(const unsigned char *lev, size_t levSize, const unsigned char *vrm, size_t vrmSize,
                               struct NativeTrackMinimap *out)
{
	const unsigned char *top = FindIcon(lev, levSize, MAP_TOP_ICON);
	const unsigned char *bottom = FindIcon(lev, levSize, MAP_BOTTOM_ICON);
	if (!top || !bottom || !out)
		return 0;
	uint16_t *vram = calloc((size_t)VRAM_W * VRAM_H, sizeof *vram);
	if (!vram)
		return 0;
	memset(out, 0, sizeof *out);
	/* A converted track (tools/tracks/lev_patch.py) points both icons at the
	   whole image, and draws only the bottom one in a race (topHalfMode 1).
	   Keep one copy, as the bottom half, with an empty top half. */
	int ok = LoadVram(vram, vrm, vrmSize);
	if (ok && memcmp(top, bottom, 12))
		ok = CopyIcon(vram, top, out, 0, &out->topWidth, &out->topHeight);
	ok = ok && CopyIcon(vram, bottom, out, out->topHeight, &out->bottomWidth, &out->bottomHeight);
	free(vram);
	return ok;
}

#ifdef CTR_CUSTOM_PACKAGES
static unsigned char *ReadAll(const char *path, size_t limit, size_t *size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *bytes = NULL;
	long n = -1;
	if (f && !fseek(f, 0, SEEK_END))
		n = ftell(f);
	if (n > 0 && (size_t)n <= limit && !fseek(f, 0, SEEK_SET) && (bytes = malloc((size_t)n)) &&
	    fread(bytes, 1, (size_t)n, f) != (size_t)n)
	{
		free(bytes);
		bytes = NULL;
	}
	if (f)
		fclose(f);
	*size = bytes ? (size_t)n : 0;
	return bytes;
}

int NativeTrackMinimap_LoadPackage(const char *assetDir, const char *pin, struct NativeTrackMinimap *out)
{
	char packageDir[768], path[1024];
	size_t size = 0, levSize = 0, vrmSize = 0;
	struct CustomPackageManifest *manifest = calloc(1, sizeof *manifest);
	unsigned char *lev = NULL, *vrm = NULL;
	int ok = 0;
	snprintf(packageDir, sizeof packageDir, "%s/tracks/packages/%s", assetDir, pin);
	snprintf(path, sizeof path, "%s/manifest.json", packageDir);
	unsigned char *json = ReadAll(path, 1024 * 1024, &size);
	if (manifest && json && CustomPackage_ParseManifest((const char *)json, size, pin, manifest, NULL, 0))
	{
		for (unsigned i = 0; i < manifest->count; i++)
		{
			snprintf(path, sizeof path, "%s/%s", packageDir, manifest->files[i].path);
			if (!strcmp(manifest->files[i].role, "lev") && !lev)
				lev = ReadAll(path, 16 * 1024 * 1024, &levSize);
			else if (!strcmp(manifest->files[i].role, "vrm") && !vrm)
				vrm = ReadAll(path, 4 * 1024 * 1024, &vrmSize);
		}
		ok = lev && vrm && NativeTrackMinimap_Extract(lev, levSize, vrm, vrmSize, out);
	}
	free(json);
	free(lev);
	free(vrm);
	free(manifest);
	return ok;
}
#endif
