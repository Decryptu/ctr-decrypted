# Screen filters

**OPTIONS → Video → Screen Filter**, saved as `screen_filter` under `[Video & QoL]` in `config.ini`. It decides how the finished frame reaches the window, and switches live.

| Menu | `screen_filter=` | Effect |
|---|---|---|
| SMOOTH | 0 | Linear scaling at the scaled render modes. **Default.** At Render Scale ORIGINAL the frame is presented exactly as shipped |
| SHARP | 1 | Nearest-neighbour scaling at the scaled render modes |
| SHARP BILINEAR | 2 | Whole-pixel scaling with a linear seam about one window pixel wide: crisp pixels without the uneven pixel widths nearest gives at a non-integer scale |
| FXAA | 3 | Fast approximate anti-aliasing of the rendered image, then Sharp Bilinear scaling |
| SCANLINES | 4 | The game's 216 lines drawn as light beams with dark gaps between them, on crisp pixels |
| CRT | 5 | Scanlines with soft pixels, an aperture grille and a slightly curved screen |

The row replaces **Smooth Scaling**. A `config.ini` that still has `smooth_scaling = false` and no `screen_filter` loads as SHARP.

Render Scale and Screen Filter are independent. Scanlines and CRT always draw the PSX picture, line for line, so a higher render scale only costs time with them. FXAA does the most at 2X and above, where it smooths the stair-steps the higher raster still has.

## How it works

`platform/native_renderer.c`, from `NativeRenderer_DrawScreenFilter`. The ladder and its helpers are in `include/platform/native_screen_filter.h`.

- **Smooth and Sharp** keep the plain present: the blit from the main render target at the scaled modes, and the VRAM present at ORIGINAL. The default costs nothing new.
- **The other filters** run one shader pass over a frame texture onto the presentation viewport. At the scaled modes that texture is the main render target. At ORIGINAL, and for frames shown straight from VRAM (the boot splash, movies, the scrapbook), the shown VRAM rectangle is first decoded into an RGBA texture at its PSX size, because VRAM holds packed 15-bit pixels. The pass samples linearly; render targets keep nearest filtering otherwise, because the VRAM pack depends on it.
- **FXAA** runs at the source size into its own target, then Sharp Bilinear scales that to the window. Run once per window pixel while upscaling, it traced halos around every edge; scaled linearly, the whole frame went soft at ORIGINAL.
- **Scanlines and CRT** work in linear light (colour squared), so a gap between two bright lines darkens the way a tube's does. Each line is a Gaussian beam that widens with brightness and is divided by its own area, so every colour keeps its average brightness and only the line structure changes. Where a line is under about 2.5 window pixels tall the gaps would alias into uneven bands, so the effect fades out there, in windows under about 540 pixels tall. The CRT grille alternates red, green and blue on each window pixel column and averages 1.
- The programs compile on first use. One that fails to compile falls back to the plain present, with an error in the log.

## Measurements

Crash Cove's attract demo, frame 1500, a 1600×900 window, captured after the filter with `CTR_DEV_SHOT_PRESENTED=1` and measured by `tools/decrypted/filter_check.py` against SMOOTH:

| Filter | ORIGINAL: brightness / steps / lines | NATIVE: brightness / steps / lines |
|---|---|---|
| SMOOTH | 100% / 100% / 0.0% | 100% / 100% / 0.0% |
| SHARP BILINEAR | 99.9% / 84% / 0.0% | 100% / 100% / 0.0% |
| FXAA | 99.9% / 52% / 0.0% | 100% / 70% / 0.0% |
| SCANLINES | 99.6% / 68% / 5.2% | 99.6% / 64% / 5.7% |
| CRT | 96.5% / 67% / 2.8% | 96.4% / 62% / 3.1% |

- *Brightness* is mean luma against SMOOTH. CRT loses 3.5% to its black corners.
- *Steps* is the 99th percentile of the luma step between neighbouring pixels: lower means softer edges.
- *Lines* is the share of vertical variation at the 216-line frequency. CRT's is lower because the curve spreads it.
- At NATIVE the raster matches the window, so SHARP BILINEAR is identical to SMOOTH, pixel for pixel.

The filter changes nothing in the game: the main render target captures (`CTR_DEV_SHOT_PRESENTED` unset) are byte-identical with and without a filter.

## Not ported from High Octane

High Octane offers 13 effects, adapted from DaedalusX64-vitaGL's shaders and tuned to the Vita's 960×544. These filters are written for this renderer instead, so they follow the presentation viewport and the PSX raster at any window size. Its colour effects (Gamma, Greyscale, Hue, Negative, Sepia, Vignette, LCD 3x, colour-blind modes) are on the roadmap under Maybe.
