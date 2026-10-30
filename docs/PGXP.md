# PGXP (precision geometry)

The PS1 GTE rounds every projected vertex to a whole pixel, and the PS1 GPU maps textures affinely (linearly in screen space). Together they cause the two artefacts the console is known for: polygons that jitter as the camera moves ("vertex wobble") and textures that bend and swim on large, close polygons ("texture warping"). PGXP (Parallel/Precision Geometry Transform Pipeline) keeps a floating-point copy of each projected vertex next to the integer one the game sees, and the renderer draws with the precise copy.

The game logic still runs on the retail integer values. Only what is drawn changes.

## Source

Ported from [High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) commit `03014d84f` ("Added PGXP implementation.", v1.5). High Octane adapted it from [CTR Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged) (revision `07d0c49f`), which credits DuckStation (Stenzek) and the original PGXP work by iCatButler. The code is GPLv3; see `THIRD_PARTY_NOTICES.md`.

High Octane has no later functional changes to PGXP. Commit `9369c5f11` (post-processing shaders) only moved the PGXP row in its own options menu, which we do not use. High Octane disables the geometry part on Vita for performance. Here every mode is available.

## Option

**OPTIONS → Video → PGXP (Anti-Wobble)**, saved as `pgxp` under `[Video & QoL]` in `config.ini`. The value is High Octane's mode number:

| Menu | `pgxp=` | Effect |
|---|---|---|
| OFF | 0 | Retail rendering |
| TEXTURES | 3 | Perspective-correct texturing only. Vertices stay on whole pixels |
| GEOMETRY | 1 | Sub-pixel vertices (anti-wobble) only. Textures stay affine |
| FULL | 2 | Both. **Default** |

The menu steps in High Octane's order (OFF, TEXTURES, GEOMETRY, FULL). Changes apply at the next frame boundary, so the option switches live with no restart. High Octane only applies a change after a restart because it compiles the shaders per mode. Here the shaders handle every mode, see below.

Back-face culling uses the precise vertices whenever GEOMETRY or FULL is on (`gNativePgxpIntegerNclipEnabled` = 0). High Octane has the same code behind a config-file-only `pgxp_precise_nclip`, off by default there, and that default is what caused holes in distant floors, see [Back-face culling](#back-face-culling).

## How it hooks in

- **Tracking (`platform/native_pgxp.c`).** A shadow of the GTE's SXY0..SXY2 FIFO holds the precise screen x/y and view depth of each projected vertex, keyed by the retail packed SXY value it was rounded to. Stores to memory (`CTR_GteStoreSXY*` in `include/ctr_gte.h`, `MFC2` and `MTC2` in `platform/native_inline_c.c`) copy the precise vertex into a table keyed by the destination address. Primitives in the two primitive arenas get one slot per word, registered from `MainInit_PrimMem` (`game/MAIN/MainInit.c`); other addresses use a 4096-entry hash cache. Every entry carries a frame generation, bumped in `Platform_EndFrame`, so stale data is never used.
- **GTE (`platform/native_gte_core.c`).** `GTE_RotTransPers` recomputes the projection in double precision, without the GTE's IR truncation and approximate division, and pushes the result into the shadow FIFO. MVMVA records its exact result so that the level renderer's subdivision can project precise midpoints. NCLIP uses the precise vertices while vertex precision is on, see [Back-face culling](#back-face-culling). The Archipelago mirror trap flips the rotation matrix rather than screen x, so the precise projection needs no mirror handling of its own.
- **Level renderer (`game/226/226_00_DrawLevelOvr1P.c`).** The hand-decompiled level renderer subdivides near quads and clips against the near plane in integer space. The hooks carry precise positions through midpoints, copies, clip records and near-plane interpolation, so subdivided track geometry lines up without cracks.
- **GPU (`platform/native_gpu.c`).** `MakeVertexTriangle` and `MakeVertexQuad` look up each vertex by address and packed value. A hit replaces x/y with the precise position (GEOMETRY/FULL). If every vertex of a textured polygon has a usable depth (at least 64), the depth is written to the new `w` field (TEXTURES/FULL). A miss keeps the retail position, so 2D elements, sprites and anything the game edits after projection draw exactly as before.
- **Renderer (`platform/native_renderer.c`, `include/platform/native_renderer_types.h`).** `GrVertex` now stores float `x, y, w` (32 bytes, was 24), and texture page and CLUT moved to their own `a_page_clut` attribute. The vertex shader multiplies `gl_Position` by `w` when `w > 0`, which leaves the screen position unchanged but makes the GPU interpolate the texture coordinates perspective-correct. Colour and dither coordinates are declared `noperspective`, so Gouraud shading and dithering stay screen-linear as on the PS1. Vertices without a depth have `w = 0`, which the shader treats as 1, so the same shaders serve every mode. This is why the option switches live.

## Back-face culling

The owner reported thin horizontal holes in distant floors (the Adventure hub, every race) and whole floor pieces missing on Dingo Canyon. The sky showed through them. They were caused by culling, not by drawing.

The level renderer culls back faces with the GTE's NCLIP, the signed area of the projected triangle. A distant floor polygon seen almost edge-on is only a fraction of a pixel tall. With retail integer vertices its area rounds to zero or flips sign, so it is culled, and that is harmless in retail because its neighbours round onto the same pixel rows and the strip it covers has no height. With precise vertices the neighbours keep their true sub-pixel positions, the strip opens, and nothing fills it. Culling now uses the same precise vertices the GPU draws with, so a polygon is culled exactly when its drawn shape faces away. Only rendering calls NCLIP (the level renderer and `RenderBucket_QueueExecute.c`), so gameplay is unaffected.

Measured with the [developer tools](DEV-TOOLS.md) on Dingo Canyon's demo, FULL at 2x render scale: integer culling had about 45% more see-through pixels than drawing at retail positions. Precise culling is level with the retail baseline on Dingo Canyon, Crash Cove, Tiger Temple, Blizzard Bluff, Dragon Mines and Mystery Caves, and the remaining count is detector noise that the baseline has too.

## Texture sampling fixes

The first in-game test showed thin horizontal holes in distant floors and walls. Part of them came from texture sampling, which perspective-correct texturing exposed, and was fixed as below. The rest came from culling (above).

- **Per-polygon UV clamp.** `GrVertex` carries the polygon's texel bounds (`uMin`, `vMin`, `uMax`, `vMax`, filled by `NativeGpu_SetUvLimits` in `platform/native_gpu.c`), and the fragment shader clamps the texture coordinate to them. At grazing angles the perspective-correct coordinate can land a fraction of a texel outside the polygon's own texels, often on the transparent colour, which shows as a hole. DuckStation solves the same problem the same way. All-zero bounds (rectangles, lines) mean no clamp.
- **Flat page and CLUT.** `v_page_clut` and `v_uvLimits` are `flat`. They are constant per polygon, and interpolating a constant perspective-correctly drifts by float error when the vertex depths differ a lot.
- **One rounding for word and index.** The 4- and 8-bit samplers used to compute the VRAM word and the index inside it with different rounding. Just below a word boundary they read the right word with the wrong index. This is not specific to PGXP: it also drew a one-texel column of wrong colour through the widescreen name-entry font. Each sampler now floors the coordinate once and fetches the word at its centre.

## Debugging

`CTR_PGXP_DEBUG` (environment variable) tints polygons:

| Value | Tint |
|---|---|
| 1 | By how many vertices PGXP recovered: red for some, blue for none, untinted for all. The sky backdrop is blue, so holes show as blue |
| 2 | As 1, and the recovered polygons switch between precise and retail positions every 2.5 seconds, tinted green in the retail phase |
| 3 | As 1, but always drawn at retail positions with affine textures: the baseline to compare a run against |
| 4 | Level geometry by the overlay 226 bucket that emitted it: red 4x4, yellow dynamic, green 4x2, cyan 4x1 (low LOD), lilac water, orange full dynamic, white near-plane clipped |

For reproducible captures, before/after measurements and polygon dumps, use the tools in [DEV-TOOLS.md](DEV-TOOLS.md) rather than playing.

## Differences from High Octane

- High Octane's renderer is a multithreaded frontend/backend with Vita paths. Ours is dowlle/ctr-native-ap's single-threaded one. The shader changes were applied by hand: the PC path only, always compiled in instead of behind a `PGXP_PC_TEXTURES` define.
- High Octane keeps a static 512 KiB primitive arena. Ours are MEMPACK allocations sized per level (larger for custom tracks), so the shadow table covers both arenas and is reallocated on level load. It is allocated even while PGXP is off.
- High Octane's own mirror mode flips screen x in the GTE. We don't have that mode, so its mirror handling was dropped.
- The menu, config and `load_config` changes went into our config system (`platform/native_config.c`, `game/230/MM_ConfigMenu.c`, `include/ctr_menu_pages.h`) instead of High Octane's menu.
- All `__vita__` code was removed.

## Known limitations

- Precision only reaches primitives whose vertices the game writes straight from the GTE (or copies through the hooked level-renderer helpers). Geometry that game code builds or edits in integer space after projection falls back to retail positions for those vertices.
- A polygon with some vertices recovered and some not draws with mixed positions. High Octane does the same deliberately, so shared corners stay precise.
- Perspective-correct texturing needs every vertex of a polygon to have a depth. Polygons that are partly behind the near plane, or very close, keep affine mapping.
- Larger vertices cost about 2 MiB more per vertex buffer, 6 MiB in all (the CPU array plus two GL buffers), and the shadow table takes roughly 5 bytes per byte of primitive arena (about 10 MiB with 1 MiB arenas).

## Needs in-game testing

The owner confirmed anti-wobble in Adventure and reported the distant holes. The texture sampling fixes and precise culling remove them in the attract-mode demos (measured, see above), and the owner confirmed the fix in game (build ffe21bf61). Still to check:

- Each mode on a race track, in the Adventure hub and in the menus: no cracks or flicker on track, walls and water, no displaced HUD, text or sprites.
- Switching the option live in the middle of a race.
- Split screen (2 to 4 players) and widescreen aspect ratios.
- Mirror trap (Archipelago), the AP item box visuals, reverse tracks and a custom track (large primitive arena).
- Render scale above ORIGINAL, where sub-pixel positions are most visible.
- Frame time with FULL compared with OFF.
