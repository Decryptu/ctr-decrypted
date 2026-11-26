#include <platform/native_dualshock_overlay.h>

#include <stdlib.h>

// Embedded DualShock controller overlay for the Ghost Replay input viewer.
// Decoded once on demand with stb_image (PNG only, private to this file).

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "../vendor/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "native_dualshock_png.h"

unsigned char *NativeDualshockOverlay_DecodeRGBA(int *outWidth, int *outHeight)
{
	int w = 0;
	int h = 0;
	int channels = 0;
	unsigned char *pixels = stbi_load_from_memory(
	    kNativeDualshockPng, (int)sizeof(kNativeDualshockPng), &w, &h, &channels, 4);
	if (pixels == NULL || w <= 0 || h <= 0)
	{
		if (pixels != NULL)
			stbi_image_free(pixels);
		return NULL;
	}
	if (outWidth != NULL)
		*outWidth = w;
	if (outHeight != NULL)
		*outHeight = h;
	return pixels;
}

void NativeDualshockOverlay_Free(unsigned char *pixels)
{
	if (pixels != NULL)
		stbi_image_free(pixels);
}
