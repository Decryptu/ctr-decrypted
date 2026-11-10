#ifndef NATIVE_RENDER_SCALE_H
#define NATIVE_RENDER_SCALE_H

#include <stdint.h>

// Intersect logical draw/display rectangles, then map their shared edges.
// Mapping widths separately can leave a one-pixel gap at fractional scales.
static inline void NativeRenderScale_ClipBox(int x, int y, int w, int h,
                                            int dx, int dy, int dw, int dh,
                                            int tw, int th, int out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	if (dw <= 0 || dh <= 0 || w <= 0 || h <= 0) return;
	if (tw <= 0) tw = dw;
	if (th <= 0) th = dh;
	int64_t left = (int64_t)x - dx, right = left + w;
	int64_t top = (int64_t)y - dy, bottom = top + h;
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > dw) right = dw;
	if (bottom > dh) bottom = dh;
	if (right <= left || bottom <= top) return;
	const int l = (int)((left * tw + dw / 2) / dw);
	const int r = (int)((right * tw + dw / 2) / dw);
	const int b = (int)(((dh - bottom) * th + dh / 2) / dh);
	const int t = (int)(((dh - top) * th + dh / 2) / dh);
	out[0] = l; out[1] = b; out[2] = r - l; out[3] = t - b;
}

// Runtime internal render scale for the MAIN render target, selected by the
// player through the options menu (g_config.renderScale; see
// platform/native_config.c). Modes:
//
//   1        Original: the PSX-sized raster and the shipped VRAM-roundtrip
//            presentation, byte-for-byte the pre-scale behavior. Default.
//   2, 3, 4  Fixed multiples of the PSX display raster.
//   0        Native window: the raster follows the letterboxed presentation
//            viewport (never smaller than the PSX raster).
//
// Coordinate spaces (the contract every helper below serves):
//
//   PSX display   activeDispEnv.disp, ~320x240. Logical space: game vertices,
//                 draw-area clips, AP HUD coordinates all live here. Never
//                 multiplied by the scale.
//   Main target   The RGBA framebuffer normal draws land in. Its raster is
//                 the logical display size mapped through the mode above.
//                 Projection still maps logical display coordinates onto the
//                 full viewport, so geometry scales in rasterization, not in
//                 game code. Scissor and clear rects must be scaled into this
//                 space per axis.
//   Offscreen     Warp/heat/clock feedback targets. Semantically PSX-sized;
//                 the scale never applies to them.
//   VRAM          The persistent 1024x512 RG8 texture of packed 15-bit PSX
//                 pixels. Always PSX-sized rects. Loading the main target
//                 FROM VRAM samples a PSX-sized source rect into the scaled
//                 target; packing the main target INTO VRAM downsamples back
//                 to the PSX-sized rect (the pack quad samples the whole
//                 source with normalized UVs, so destination viewport size
//                 alone decides the decimation).
//   Host window   The presentation viewport (letterboxed). In Original mode
//                 the frame reaches it through the VRAM roundtrip exactly as
//                 shipped; in every other mode the main target is blitted to
//                 it directly, skipping the 15-bit PSX downsample for the
//                 presented image only.
//
// The helpers below are pinned by tools/test-render-scale.c (standalone:
// gcc -Wall -Wextra tools/test-render-scale.c && ./a.out).

#define NATIVE_RENDER_SCALE_MODE_NATIVE   0
#define NATIVE_RENDER_SCALE_MODE_ORIGINAL 1
#define NATIVE_RENDER_SCALE_MODE_MAX      4

// Snap any persisted/hand-edited value onto the supported ladder. 0 stays
// Native; everything else clamps into 1..4.
static inline int NativeRenderScale_ClampMode(int mode)
{
	if (mode == NATIVE_RENDER_SCALE_MODE_NATIVE)
	{
		return mode;
	}
	if (mode < NATIVE_RENDER_SCALE_MODE_ORIGINAL)
	{
		return NATIVE_RENDER_SCALE_MODE_ORIGINAL;
	}
	if (mode > NATIVE_RENDER_SCALE_MODE_MAX)
	{
		return NATIVE_RENDER_SCALE_MODE_MAX;
	}
	return mode;
}

// Anything but Original presents the main target directly instead of the
// shipped VRAM roundtrip.
static inline int NativeRenderScale_ModeUsesDirectPresent(int mode)
{
	return NativeRenderScale_ClampMode(mode) != NATIVE_RENDER_SCALE_MODE_ORIGINAL;
}

// Main render target raster size for one axis. viewportDim is the
// presentation-viewport dimension on the same axis, consumed only by Native
// mode. The target never drops below the logical raster: packing a
// smaller-than-PSX target back into the PSX-sized VRAM rect would upsample
// and corrupt feedback semantics.
static inline int NativeRenderScale_TargetDimForMode(int logicalDim, int mode, int viewportDim)
{
	mode = NativeRenderScale_ClampMode(mode);
	if (logicalDim <= 0)
	{
		return logicalDim;
	}
	if (mode == NATIVE_RENDER_SCALE_MODE_NATIVE)
	{
		return viewportDim > logicalDim ? viewportDim : logicalDim;
	}
	return logicalDim * mode;
}

// Scale one scissor/clear coordinate from logical display space into main
// target space along one axis. Multiplies by the exact target/logical ratio
// BEFORE truncating: for the float-valued scissor math this keeps any
// sub-pixel rounding error at its shipped one-logical-pixel magnitude instead
// of magnifying it, and when target equals logical (Original mode, offscreen
// targets, or a not-yet-sized target guarded below) it truncates exactly like
// the implicit float-to-GLint conversion the shipped glScissor call did.
static inline int NativeRenderScale_ScaleAxisCoord(float logicalCoord, int logicalDim, int targetDim)
{
	if ((logicalDim <= 0) || (targetDim <= 0))
	{
		return (int)logicalCoord;
	}
	return (int)(logicalCoord * ((float)targetDim / (float)logicalDim));
}

// Executable documentation of the pack-shader downsample (not called by the
// GPU path, which does this arithmetic in the shader): packing a source of
// srcDim texels through a destination viewport of dstDim pixels samples, for
// destination index i, the source texel floor((i + 0.5) / dstDim * srcDim).
// For a 2x source this decimates to the odd texels: 640 -> 320 picks 2i + 1.
static inline int NativeRenderScale_PackSampleIndex(int dstIndex, int dstDim, int srcDim)
{
	if ((dstDim <= 0) || (srcDim <= 0))
	{
		return 0;
	}
	// Integer form of floor((dstIndex + 0.5) * srcDim / dstDim).
	return (int)(((2 * (long)dstIndex + 1) * srcDim) / (2 * (long)dstDim));
}

// Executable documentation of the pack flipY: GL framebuffer row r of an
// h-row target lands in VRAM row (h - 1 - r), converting bottom-up GL storage
// to the top-left-origin VRAM layout.
static inline int NativeRenderScale_PackFlipRow(int row, int height)
{
	return height - 1 - row;
}

#endif
