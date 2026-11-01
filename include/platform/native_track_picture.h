#ifndef NATIVE_TRACK_PICTURE_H
#define NATIVE_TRACK_PICTURE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A custom track's picture in the Mods screen (docs/CUSTOM_TRACKS.md): Saphi's
   track thumbnail, a baseline JPEG, cropped to 3:2 and shrunk to a fixed size
   of PS1 15-bit texels. The game's 512x216 frame fills a 4:3 screen, so a
   texel shows about 1.78 times as tall as wide: 112x42 texels look 3:2. */
#define NATIVE_TRACK_PICTURE_WIDTH 112
#define NATIVE_TRACK_PICTURE_HEIGHT 42
#define NATIVE_TRACK_PICTURE_CROP_W 3
#define NATIVE_TRACK_PICTURE_CROP_H 2
#define NATIVE_TRACK_PICTURE_TEXELS (NATIVE_TRACK_PICTURE_WIDTH * NATIVE_TRACK_PICTURE_HEIGHT)
/* Saphi serves pictures of about 20 KB, at most 300x200. */
#define NATIVE_TRACK_PICTURE_MAX_BYTES (1024 * 1024)
#define NATIVE_TRACK_PICTURE_MAX_SIDE 2048

/* 1 with out filled, 0 when the bytes are not a JPEG of a sane size. No texel
   is 0, which the GPU would draw as transparent. */
int NativeTrackPicture_Decode(const unsigned char *jpeg, size_t size, uint16_t out[NATIVE_TRACK_PICTURE_TEXELS]);

/* Background job (package builds): the picture of a Saphi track, from path
   on Saphi. The decoded texels are cached as cacheDir/<trackID>.bin with the
   picture's media id; mediaID 0 (no catalogue yet) takes whatever the cache
   holds. One job at a time. Poll: 0 running, 1 out filled, -2 failed, -1 idle. */
int NativeTrackPicture_Start(int trackID, int mediaID, const char *path, const char *cacheDir);
int NativeTrackPicture_Poll(int *trackID, uint16_t out[NATIVE_TRACK_PICTURE_TEXELS]);

#ifdef __cplusplus
}
#endif
#endif
