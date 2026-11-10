# Screen filters

**OPTIONS → Video → Screen Filter**, saved as `screen_filter` under `[Video & QoL]` in `config.ini`. It decides how the finished frame reaches the window, and switches live.

| Menu | `screen_filter=` | Effect |
|---|---|---|
| SMOOTH | 0 | Linear scaling at the scaled render modes. **Default.** At Render Scale ORIGINAL the frame is presented exactly as shipped |
| SHARP | 1 | Nearest-neighbour scaling at the scaled render modes |
| SHARP BILINEAR | 2 | Whole-pixel scaling with a linear seam about one window pixel wide: crisp pixels without nearest's uneven widths at a non-integer scale |
| FXAA | 3 | Fast approximate anti-aliasing of the rendered image, then Sharp Bilinear scaling |
| SCANLINES | 4 | The game's 216 lines as light beams with dark gaps, on crisp pixels |
| CRT | 5 | Scanlines with soft pixels, an aperture grille and a slightly curved screen |
| LCD | 6 | Each PSX pixel a cell of red, green and blue stripes with darker rows between cells, like a handheld's screen |

A `config.ini` with the old `smooth_scaling = false` and no `screen_filter` loads as SHARP.

Render Scale and Screen Filter are independent. Scanlines, CRT and LCD always draw the PSX picture pixel for pixel, so a higher render scale only costs time with them. FXAA does most at 2X and above, smoothing the stair-steps the higher raster still has.

## Colour filter

**OPTIONS → Extras → Colour Filter**, saved as `colour_filter` under `[Extras]`. It changes the picture's colours and the screen filter shows the result, so the two combine (CRT in greyscale, FXAA with a colour-blind mode). It switches live.

| Menu | `colour_filter=` | Effect |
|---|---|---|
| OFF | 0 | **Default** |
| GREYSCALE | 1 | Luma (Rec. 709 weights) |
| SEPIA | 2 | The usual sepia matrix |
| NEGATIVE | 3 | Inverted colours |
| VIGNETTE | 4 | Darkens toward the corners, to 35% |
| PROTANOPIA | 5 | Colour-blind correction for players without red cones |
| DEUTERANOPIA | 6 | Colour-blind correction for players without green cones |

The colour-blind modes are daltonize corrections: the colour as that player sees it (Machado, Oliveira and Fernandes 2009, full severity, linear light) is subtracted, and the missed difference is added into green and blue (Fidaner's shift), so red and green also differ in lightness and blue. This separates red from green, and orange from green, much more, at the cost of a few pairs that were already distinct. There is no tritanopia mode: no fixed shift made blue and yellow easier to tell apart without making more pairs harder.

## How it works

`platform/native_renderer.c`, from `NativeRenderer_DrawScreenFilter`. The ladder and its helpers are in `include/platform/native_screen_filter.h`.

- **Smooth and Sharp** with the colour filter off keep the plain present: the blit from the main render target at the scaled modes, the VRAM present at ORIGINAL. The default costs nothing extra.
- **The other filters, and any filter with a colour filter on,** run one shader pass over a frame texture onto the presentation viewport. At the scaled modes that texture is the main render target. At ORIGINAL, and for frames shown straight from VRAM (the boot splash), the shown VRAM rectangle is first decoded into an RGBA texture at its PSX size, because VRAM holds packed 15-bit pixels. The pass samples linearly; render targets otherwise keep nearest filtering, because the VRAM pack depends on it. With a colour filter, Smooth and Sharp take a pass that samples like their blit (linear or nearest), and a VRAM frame is always sampled nearest, as the plain VRAM present does, so only colours change.
- **Scrapbook** draws decoded RGBA into the main target, including its black borders, at every render scale. It uses the same direct presentation and filter passes as a rendered frame.
- **The colour filter** applies to every colour the pass reads from the frame (`Colour`, called from `Fetch`), so each screen filter works on the changed picture. The program is compiled per screen filter and colour filter pair (`SCREEN_FILTER`, `COLOUR_FILTER`).
- **FXAA** runs at the source size into its own target, then Sharp Bilinear scales that to the window; the colour filter is applied in the second pass. Per window pixel while upscaling it would trace halos around every edge, and a linear upscale would make the frame soft at ORIGINAL.
- **Scanlines and CRT** work in linear light (colour squared), so a gap between two bright lines darkens as a tube's does. Each line is a Gaussian beam that widens with brightness and is divided by its own area, so every colour keeps its average brightness. Where a line is under about 2.5 window pixels tall the gaps would alias into uneven bands, so the effect fades out there (windows under about 540 pixels tall). The CRT grille alternates red, green and blue per window pixel column and averages 1.
- **LCD** works in linear light too. Its stripes and rows each average 1, so colours keep their brightness except near white, where the bright stripe clips. Stripes fade out where a PSX pixel is under about 3 window pixels wide, rows under about 2.5 tall: at 1080p rows show fully and stripes faintly, at 4K both.
- Render targets clamp at their edges, so a linear read on the outer pixels does not blend in the opposite edge.
- Programs compile on first use. One that fails falls back to the plain present, with an error in the log.

## Checking

Capture after the filter with `CTR_DEV_SHOT_PRESENTED=1` and compare against SMOOTH with `tools/decrypted/filter_check.py` ([DEV-TOOLS.md](DEV-TOOLS.md#analysis-scripts)). Expected:

- Mean brightness within about half a percent of SMOOTH, except CRT, which loses a few percent to its black corners, and LCD, about 3% to clipped stripes.
- FXAA, Scanlines and CRT lower the 99th-percentile step between neighbouring pixels (softer edges); FXAA the most.
- Only Scanlines and CRT have variation at the 216-line frequency (CRT less, as the curve spreads it).
- At NATIVE the raster matches the window, so SHARP BILINEAR is identical to SMOOTH.
- The filter changes nothing in the game: main render target captures (`CTR_DEV_SHOT_PRESENTED` unset) are byte-identical with and without a filter.
- A colour filter only changes colours: SEPIA matches the sepia matrix applied to SMOOTH within 1/255 at ORIGINAL and 3/255 at 2X, and NEGATIVE's mean is 255 minus SMOOTH's.

## High Octane's effects

High Octane has 13 effects in one list, adapted from DaedalusX64-vitaGL's shaders and tuned to the Vita's 960×544. These filters are written for this renderer instead, so they follow the presentation viewport and PSX raster at any window size, and colour effects are a separate row so they combine with screen filters. Its LCD 3x became LCD, drawn on PSX pixels rather than a fixed 640×480 grid. Not ported: Hue (it turns colours around the wheel unevenly) and Gamma (the display's own setting does the same). Its colour-blind matrices are replaced by the correction above, which separates red from green further.
