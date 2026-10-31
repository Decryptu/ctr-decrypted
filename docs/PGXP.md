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
- **Renderer (`platform/native_renderer.c`, `include/platform/native_renderer_types.h`).** `GrVertex` stores float `x, y, w` (32 bytes), with texture page and CLUT in their own `a_page_clut` attribute. The vertex shader multiplies `gl_Position` by `w` when `w > 0`, which leaves the screen position unchanged but makes the GPU interpolate the texture coordinates perspective-correct. Colour and dither coordinates are declared `noperspective`, so Gouraud shading and dithering stay screen-linear as on the PS1. Vertices without a depth have `w = 0`, which the shader treats as 1, so the same shaders serve every mode. This is why the option switches live.

## Level depth test

The PS1 has no depth buffer. It draws in ordering-table order, and the level renderer sorts coarsely: the 16 faces of a 4x4 quadblock share one slot, taken from the block's farthest corner. So a nearer polygon can be drawn before a farther one and get painted over, in retail too (water over the N. Sanity Beach dock with the far camera, for one). With TEXTURES or FULL, PGXP gives each level vertex its view depth, so level polygons also test depth:

- **Which polygons.** Only the run of primitives `DrawLevelOvr1P`..`4P` write each frame. `RenderAllLevelGeometry` reports it with `NativeGpu_SetLevelPrimRange`, and `MainFrame_ResetDB` clears it when the arena resets. Karts, instances, shadows, particles, the sky and the HUD keep ordering-table order exactly.
- **How.** Opaque level splits draw twice. A depth-only pass writes the true depth with `GL_LESS`. A colour pass then tests with `GL_LEQUAL` against a depth pulled 2% of the distance nearer (`NATIVE_DEPTH_TOLERANCE`) and writes none. Translucent level polygons (water) get the colour pass only. The margin lets a later polygon win near-ties, as the ordering table intended: decals such as the Turbo Track pads sit on the road but not quite in its plane, and without it they lose whole triangles to it.
- **Depth value.** `gl_Position.z = w - 32`, so window depth is `1 - 16/w`. That is linear in screen space, so it interpolates correctly; nearer than 16 clamps (`GL_DEPTH_CLAMP`). A polygon with no depth (untextured, or a vertex nearer than 64) keeps ordering-table order: far plane in the depth pass, near plane in the colour pass.
- **Checking it.** `CTR_DEV_NO_LEVEL_DEPTH=1` turns it off for A/B captures ([DEV-TOOLS.md](DEV-TOOLS.md)). Across the 18 tracks' attract demos the difference is a few hundredths of a percent of the pixels, every one a sort error corrected; anything else is a regression. With `pgxp=0` frames must be identical.

## Back-face culling

The level renderer culls back faces with the GTE's NCLIP, the signed area of the projected triangle, and here NCLIP uses the same precise vertices the GPU draws with. A distant floor polygon seen almost edge-on is a fraction of a pixel tall. With integer vertices its area rounds to zero or flips sign and it is culled; that is harmless in retail, where its neighbours round onto the same pixel rows, but with precise vertices the strip it covered opens and the sky shows through (thin holes in distant floors). Only rendering calls NCLIP (the level renderer and `RenderBucket_QueueExecute.c`), so gameplay is unaffected. To check: `find_holes.py` totals with `CTR_PGXP_DEBUG=1` should match the retail baseline (`CTR_PGXP_DEBUG=3`).

## Texture sampling

- **Per-polygon UV clamp.** `GrVertex` carries the polygon's texel bounds (`uMin`, `vMin`, `uMax`, `vMax`, filled by `NativeGpu_SetUvLimits` in `platform/native_gpu.c`), and the fragment shader clamps the texture coordinate to them. At grazing angles the perspective-correct coordinate can land a fraction of a texel outside the polygon's own texels, often on the transparent colour, which shows as a hole. DuckStation solves the same problem the same way. All-zero bounds (rectangles, lines) mean no clamp.
- **Flat page and CLUT.** `v_page_clut` and `v_uvLimits` are `flat`. They are constant per polygon, and interpolating a constant perspective-correctly drifts by float error when the vertex depths differ a lot.
- **One rounding for word and index.** The 4- and 8-bit samplers floor the coordinate once and derive both the VRAM word (fetched at its centre) and the index inside it from that value. Rounding them separately reads the right word with the wrong index just below a word boundary, a one-texel column of wrong colour.

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
- High Octane keeps a static 512 KiB primitive arena. Ours are two 1 MiB host allocations made on every level load (`MainInit_PrimMem`), so the shadow table covers both arenas and is reallocated on level load. It is allocated even while PGXP is off.
- High Octane's own mirror mode flips screen x in the GTE. We don't have that mode, so its mirror handling was dropped.
- The menu, config and `load_config` changes went into our config system (`platform/native_config.c`, `game/230/MM_ConfigMenu.c`, `include/ctr_menu_pages.h`) instead of High Octane's menu.
- All `__vita__` code was removed.

## Known limitations

- Precision only reaches primitives whose vertices the game writes straight from the GTE (or copies through the hooked level-renderer helpers). Geometry that game code builds or edits in integer space after projection falls back to retail positions for those vertices.
- A polygon with some vertices recovered and some not draws with mixed positions. High Octane does the same deliberately, so shared corners stay precise.
- Perspective-correct texturing needs every vertex of a polygon to have a depth. Polygons that are partly behind the near plane, or very close, keep affine mapping. The same polygons keep ordering-table order in the [level depth test](#level-depth-test), so a sort error right at the camera can remain.
- Larger vertices cost about 2 MiB more per vertex buffer, 6 MiB in all (the CPU array plus two GL buffers), and the shadow table takes roughly 5 bytes per byte of primitive arena (about 10 MiB with 1 MiB arenas).
