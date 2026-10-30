#ifndef NATIVE_SCREEN_FILTER_H
#define NATIVE_SCREEN_FILTER_H

// How the finished frame reaches the window: Options -> Video -> Screen Filter
// (g_config.screenFilter, config key screen_filter). Stored as these raw
// values. Smooth and Sharp are the plain scaling filters; the others run one
// shader pass over the frame (NativeRenderer_DrawScreenFilter in
// platform/native_renderer.c). See docs/SCREEN-FILTERS.md.
//
//   Smooth          linear scaling at the scaled render modes (the default).
//                   At Original the frame is presented exactly as shipped.
//   Sharp           nearest-neighbour scaling at the scaled render modes.
//   Sharp Bilinear  whole-pixel scaling with a one-pixel linear seam: crisp
//                   pixels without the uneven widths of nearest at a
//                   non-integer scale.
//   FXAA            fast approximate anti-aliasing on the rendered raster.
//   Scanlines       the game's 216 lines drawn as beams with dark gaps.
//   CRT             scanlines plus an aperture-grille mask and a slightly
//                   curved screen.

#define NATIVE_SCREEN_FILTER_SMOOTH         0
#define NATIVE_SCREEN_FILTER_SHARP          1
#define NATIVE_SCREEN_FILTER_SHARP_BILINEAR 2
#define NATIVE_SCREEN_FILTER_FXAA           3
#define NATIVE_SCREEN_FILTER_SCANLINES      4
#define NATIVE_SCREEN_FILTER_CRT            5
#define NATIVE_SCREEN_FILTER_COUNT          6

// A hand-edited value outside the ladder behaves as Smooth, the default.
static inline int NativeScreenFilter_Clamp(int filter)
{
	return (filter >= 0 && filter < NATIVE_SCREEN_FILTER_COUNT) ? filter : NATIVE_SCREEN_FILTER_SMOOTH;
}

// Filters that need the shader pass. Smooth and Sharp keep the plain present
// paths, so the default look and cost are unchanged.
static inline int NativeScreenFilter_UsesShader(int filter)
{
	return NativeScreenFilter_Clamp(filter) >= NATIVE_SCREEN_FILTER_SHARP_BILINEAR;
}

#endif
