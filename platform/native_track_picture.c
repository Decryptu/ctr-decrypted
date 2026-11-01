#include <platform/native_track_picture.h>

#include <stdlib.h>

// Custom track pictures (include/platform/native_track_picture.h). The JPEG
// decoder is stb_image (vendor/stb), built here for JPEG only and private to
// this file.

// stb_image declares functions this build leaves out; GCC reports those at the
// end of the file, after any pop.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "../vendor/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int NativeTrackPicture_Decode(const unsigned char *jpeg, size_t size, uint16_t out[NATIVE_TRACK_PICTURE_TEXELS])
{
	int w, h, channels;
	// The size is checked from the header before anything is decoded.
	if (!jpeg || size < 4 || size > NATIVE_TRACK_PICTURE_MAX_BYTES || jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
	    !stbi_info_from_memory(jpeg, (int)size, &w, &h, &channels) || w < 1 || h < 1 ||
	    w > NATIVE_TRACK_PICTURE_MAX_SIDE || h > NATIVE_TRACK_PICTURE_MAX_SIDE)
		return 0;
	unsigned char *rgb = stbi_load_from_memory(jpeg, (int)size, &w, &h, &channels, 3);
	if (!rgb)
		return 0;

	// The centred 3:2 crop, then a box filter: each texel averages the source
	// pixels it covers.
	int cropW = w, cropH = h;
	if (w * NATIVE_TRACK_PICTURE_CROP_H > h * NATIVE_TRACK_PICTURE_CROP_W)
		cropW = h * NATIVE_TRACK_PICTURE_CROP_W / NATIVE_TRACK_PICTURE_CROP_H;
	else
		cropH = w * NATIVE_TRACK_PICTURE_CROP_H / NATIVE_TRACK_PICTURE_CROP_W;
	if (cropW < 1)
		cropW = 1;
	if (cropH < 1)
		cropH = 1;
	const int left = (w - cropW) / 2, top = (h - cropH) / 2;
	for (int ty = 0; ty < NATIVE_TRACK_PICTURE_HEIGHT; ty++)
	{
		const int y0 = top + ty * cropH / NATIVE_TRACK_PICTURE_HEIGHT;
		int y1 = top + (ty + 1) * cropH / NATIVE_TRACK_PICTURE_HEIGHT;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int tx = 0; tx < NATIVE_TRACK_PICTURE_WIDTH; tx++)
		{
			const int x0 = left + tx * cropW / NATIVE_TRACK_PICTURE_WIDTH;
			int x1 = left + (tx + 1) * cropW / NATIVE_TRACK_PICTURE_WIDTH;
			if (x1 <= x0)
				x1 = x0 + 1;
			unsigned sum[3] = {0, 0, 0}, n = 0;
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++, n++)
					for (int c = 0; c < 3; c++)
						sum[c] += rgb[((size_t)y * w + x) * 3 + c];
			const unsigned r = (sum[0] / n) >> 3, g = (sum[1] / n) >> 3, b = (sum[2] / n) >> 3;
			const uint16_t texel = (uint16_t)(r | (g << 5) | (b << 10));
			out[ty * NATIVE_TRACK_PICTURE_WIDTH + tx] = texel ? texel : 1;
		}
	}
	stbi_image_free(rgb);
	return 1;
}
