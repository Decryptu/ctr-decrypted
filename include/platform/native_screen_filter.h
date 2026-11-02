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
//   LCD             each PSX pixel as a cell of red, green and blue stripes.

#define NATIVE_SCREEN_FILTER_SMOOTH         0
#define NATIVE_SCREEN_FILTER_SHARP          1
#define NATIVE_SCREEN_FILTER_SHARP_BILINEAR 2
#define NATIVE_SCREEN_FILTER_FXAA           3
#define NATIVE_SCREEN_FILTER_SCANLINES      4
#define NATIVE_SCREEN_FILTER_CRT            5
#define NATIVE_SCREEN_FILTER_LCD            6
#define NATIVE_SCREEN_FILTER_COUNT          7

// How the picture's colours are changed before the screen filter shows it:
// Options -> Extras -> Colour Filter (g_config.colourFilter, config key
// colour_filter). Protanopia and Deuteranopia move the red-green difference
// those players can't see into lightness and blue.

#define NATIVE_COLOUR_FILTER_OFF          0
#define NATIVE_COLOUR_FILTER_GREYSCALE    1
#define NATIVE_COLOUR_FILTER_SEPIA        2
#define NATIVE_COLOUR_FILTER_NEGATIVE     3
#define NATIVE_COLOUR_FILTER_VIGNETTE     4
#define NATIVE_COLOUR_FILTER_PROTANOPIA   5
#define NATIVE_COLOUR_FILTER_DEUTERANOPIA 6
#define NATIVE_COLOUR_FILTER_COUNT        7

// A hand-edited value outside the ladder behaves as Smooth, the default.
static inline int NativeScreenFilter_Clamp(int filter)
{
	return (filter >= 0 && filter < NATIVE_SCREEN_FILTER_COUNT) ? filter : NATIVE_SCREEN_FILTER_SMOOTH;
}

// A hand-edited value outside the ladder behaves as Off.
static inline int NativeColourFilter_Clamp(int colour)
{
	return (colour >= 0 && colour < NATIVE_COLOUR_FILTER_COUNT) ? colour : NATIVE_COLOUR_FILTER_OFF;
}

// Whether the frame needs the shader pass. Smooth and Sharp with no colour
// filter keep the plain present paths, so the default look and cost are
// unchanged.
static inline int NativeScreenFilter_UsesShader(int filter, int colour)
{
	return NativeScreenFilter_Clamp(filter) >= NATIVE_SCREEN_FILTER_SHARP_BILINEAR ||
	       NativeColourFilter_Clamp(colour) != NATIVE_COLOUR_FILTER_OFF;
}

#endif
