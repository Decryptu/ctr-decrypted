#include <common.h>

// Texture patterns: a converted track's surfaces whose texture changes as they
// play, such as MKDS's sea, lava and dash panels (docs/TRACK-CONVERTER.md#texture-patterns).
//
// The level's texPatterns (written by tools/tracks/lev_patch.py, add_patterns)
// lists them. The VRM holds each pattern's material image and an image of each
// frame, the same size and colour depth. Showing a frame copies its image and
// palette over the material's, so every face that draws with that image, near
// or far, shows it. Retail's AnimTex would need a texture group per face and
// frame, as a converted course's faces each have their own corners in the image.
//
// A step is 2^skip ticks of the 30 a second CTR_CycleTex_LEV counts. A pattern
// copies only when its step shows another frame than the one it shows.

#if defined(CTR_NATIVE)

struct TexPatternSource
{
	u16 imageX, imageY;
	u16 clutX, clutY;
};

struct TexPattern
{
	RECT16 image; // the material's own image, in VRAM words
	RECT16 clut;  // and its palette
	u16 numSteps;
	u16 skip;
	u16 numSources;
	u16 shown; // the source it shows now; 0xffff before the first
	// struct TexPatternSource sources[numSources];
	// u8 step[numSteps], then padding to 4 bytes
};

struct TexPatterns
{
	u32 count;
	// struct TexPattern patterns[count], each followed by its sources and steps
};

void ConvertedTrack_TexPatterns(struct Level *level, int timer)
{
	struct TexPatterns *table = level->texPatterns;
	u8 *at = (u8 *)(table + 1);
	static int s_off = -1; // CTR_DEV_NO_TEX_PATTERNS: every surface keeps its own image, for A/B captures

	if (s_off < 0)
	{
		s_off = getenv("CTR_DEV_NO_TEX_PATTERNS") != NULL;
	}
	if (s_off)
	{
		return;
	}

	for (u32 i = 0; i < table->count; i++)
	{
		struct TexPattern *pattern = (struct TexPattern *)at;
		const struct TexPatternSource *sources = (const struct TexPatternSource *)(pattern + 1);
		const u8 *steps = (const u8 *)(sources + pattern->numSources);
		const u32 step = ((u32)FPS_HALF(timer) >> pattern->skip) % pattern->numSteps;
		const u16 source = steps[step];

		at = (u8 *)steps + ((pattern->numSteps + 3) & ~3);
		if (source == pattern->shown)
		{
			continue;
		}
		pattern->shown = source;

		RECT16 rect = pattern->image;
		rect.x = sources[source].imageX;
		rect.y = sources[source].imageY;
		MoveImage(&rect, pattern->image.x, pattern->image.y);
		rect = pattern->clut;
		rect.x = sources[source].clutX;
		rect.y = sources[source].clutY;
		MoveImage(&rect, pattern->clut.x, pattern->clut.y);
	}
}

#endif
