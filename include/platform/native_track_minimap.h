#ifndef NATIVE_TRACK_MINIMAP_H
#define NATIVE_TRACK_MINIMAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A custom track's minimap for the track select (docs/CUSTOM_TRACKS.md), read
   from its package's LEV and VRM before the track is loaded. The race HUD
   draws the map from the LEV's icons 3 (top half) and 4 (bottom half); their
   texture layouts point into the VRM. Both halves come out as 15-bit texels,
   the palette already applied, the top half's rows above the bottom half's,
   each left aligned in an image NATIVE_TRACK_MINIMAP_SIDE wide. */
#define NATIVE_TRACK_MINIMAP_SIDE 128
#define NATIVE_TRACK_MINIMAP_TEXELS (NATIVE_TRACK_MINIMAP_SIDE * NATIVE_TRACK_MINIMAP_SIDE)

struct NativeTrackMinimap
{
	int topWidth, topHeight;
	int bottomWidth, bottomHeight;
	uint16_t texels[NATIVE_TRACK_MINIMAP_TEXELS];
};

/* 1 with out filled; 0 when the LEV has no map icons, they point outside the
   VRM, or the halves do not fit together in the image. */
int NativeTrackMinimap_Extract(const unsigned char *lev, size_t levSize, const unsigned char *vrm, size_t vrmSize,
                               struct NativeTrackMinimap *out);

/* The same from the installed package pin (tracks/packages/<pin>/ under
   assetDir), through its manifest.json. */
int NativeTrackMinimap_LoadPackage(const char *assetDir, const char *pin, struct NativeTrackMinimap *out);

#ifdef __cplusplus
}
#endif
#endif
