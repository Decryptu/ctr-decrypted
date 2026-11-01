// gcc -std=c11 -Wall -Wextra -I include -o /tmp/test-track-picture
//   tools/test-track-picture.c platform/native_track_picture.c -lm
//
// Custom track pictures (platform/native_track_picture.c): a JPEG is cropped
// to 3:2 and shrunk to PS1 15-bit texels; anything that is not a JPEG of a
// sane size is refused. Run from the repository root (the fixtures are plain
// colour blocks, made for this test).
#include <platform/native_track_picture.h>

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

static unsigned char *ReadAll(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	unsigned char *bytes = malloc(1 << 20);
	*size = fread(bytes, 1, 1 << 20, f);
	fclose(f);
	return bytes;
}

// The texel's 5-bit channels.
static void Channels(uint16_t texel, int *r, int *g, int *b)
{
	*r = texel & 31;
	*g = (texel >> 5) & 31;
	*b = (texel >> 10) & 31;
}

static int Near(uint16_t texel, int r, int g, int b)
{
	int tr, tg, tb;
	Channels(texel, &tr, &tg, &tb);
	return abs(tr - r) <= 2 && abs(tg - g) <= 2 && abs(tb - b) <= 2;
}

int main(void)
{
	static uint16_t out[NATIVE_TRACK_PICTURE_TEXELS];
	const int w = NATIVE_TRACK_PICTURE_WIDTH, h = NATIVE_TRACK_PICTURE_HEIGHT;
	size_t size = 0;

	unsigned char *quadrants = ReadAll("tools/fixtures/track-picture/quadrants.jpg", &size);
	CHECK(quadrants != NULL);
	if (!quadrants)
		return 1;
	CHECK(NativeTrackPicture_Decode(quadrants, size, out));
	CHECK(Near(out[4 * w + 4], 31, 0, 0));
	CHECK(Near(out[4 * w + w - 5], 0, 31, 0));
	CHECK(Near(out[(h - 5) * w + 4], 0, 0, 31));
	CHECK(Near(out[(h - 5) * w + w - 5], 31, 31, 31));
	int zero = 0;
	for (int i = 0; i < NATIVE_TRACK_PICTURE_TEXELS; i++)
		zero |= (out[i] == 0);
	CHECK(!zero);

	// Refused: cut inside the header, not a JPEG, too large a picture.
	CHECK(!NativeTrackPicture_Decode(quadrants, 40, out));
	CHECK(!NativeTrackPicture_Decode((const unsigned char *)"\x89PNG\r\n\x1a\n", 8, out));
	CHECK(!NativeTrackPicture_Decode(NULL, 0, out));
	int sof = -1;
	for (size_t i = 2; i + 8 < size && sof < 0; i++)
		if (quadrants[i] == 0xFF && quadrants[i + 1] == 0xC0)
			sof = (int)i;
	CHECK(sof > 0);
	if (sof > 0)
	{
		// Width 4000 in the frame header: refused before decoding.
		quadrants[sof + 7] = 4000 >> 8;
		quadrants[sof + 8] = 4000 & 255;
		CHECK(!NativeTrackPicture_Decode(quadrants, size, out));
	}
	free(quadrants);

	// 2:1 with black bands outside the centred 3:2: no band survives the crop.
	unsigned char *wide = ReadAll("tools/fixtures/track-picture/wide.jpg", &size);
	CHECK(wide != NULL && NativeTrackPicture_Decode(wide, size, out));
	// Uncropped, each band would cover 14 texels; the edge texels blur into it.
	CHECK(Near(out[h / 2 * w + 2], 31, 31, 0));
	CHECK(Near(out[h / 2 * w + w - 3], 31, 31, 0));
	free(wide);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
