# PGXP (precision geometry)

The PS1 GTE rounds every projected vertex to a whole pixel, and the PS1 GPU maps textures affinely (linearly in screen space). That gives polygons that jitter as the camera moves ("vertex wobble") and textures that bend on large, close polygons ("texture warping"). PGXP (Parallel/Precision Geometry Transform Pipeline) keeps a floating-point copy of each projected vertex next to the integer one the game sees, and the renderer draws with the precise copy.

Game logic still runs on the retail integer values. Only what is drawn changes.

## Source

Ported from [High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) commit `03014d84f` (v1.5), which adapted it from [CTR Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged) (revision `07d0c49f`), crediting DuckStation (Stenzek) and the original PGXP work by iCatButler. The code is GPLv3; see `THIRD_PARTY_NOTICES.md`. High Octane has no later functional PGXP changes.

## Option

**OPTIONS → Video → PGXP (Anti-Wobble)**, saved as `pgxp` under `[Video & QoL]` in `config.ini`. The value is High Octane's mode number:

| Menu | `pgxp=` | Effect |
|---|---|---|
| OFF | 0 | Retail rendering |
| TEXTURES | 3 | Perspective-correct texturing only. Vertices stay on whole pixels |
| GEOMETRY | 1 | Sub-pixel vertices (anti-wobble) only. Textures stay affine |
| FULL | 2 | Both. **Default** |

The menu steps in that order. Changes apply at the next frame boundary, with no restart, because one set of shaders handles every mode (see Renderer below).

## How it hooks in

- **Tracking (`platform/native_pgxp.c`).** A shadow of the GTE's SXY0..SXY2 FIFO holds the precise screen x/y and view depth of each projected vertex, keyed by the retail packed SXY value. Stores to memory (`CTR_GteStoreSXY*` in `include/ctr_gte.h`, `MFC2` and `MTC2` in `platform/native_inline_c.c`) copy the precise vertex into a table keyed by destination address. The two primitive arenas get one slot per word, registered from `MainInit_PrimMem` (`game/MAIN/MainInit.c`); other addresses use a 4096-entry hash cache. Every entry carries a frame generation, bumped in `Platform_EndFrame`, so stale data is never used.
- **GTE (`platform/native_gte_core.c`).** `GTE_RotTransPers` recomputes the projection in double precision, without IR truncation or approximate division, through the [precise camera](#precise-camera) when the matrix has one, and pushes the result into the shadow FIFO. MVMVA records its exact result so the level renderer's subdivision can project precise midpoints. NCLIP uses the precise vertices, see [Back-face culling](#back-face-culling). The Archipelago mirror trap flips the rotation matrix, not screen x, so the precise projection needs no mirror handling.
- **Level renderer (`game/226/226_00_DrawLevelOvr1P.c`).** It subdivides near quads and clips against the near plane in integer space. The hooks carry precise positions through midpoints, copies, clip records and near-plane interpolation, so subdivided geometry lines up without cracks.
- **GPU (`platform/native_gpu.c`).** `MakeVertexTriangle` and `MakeVertexQuad` look up each vertex by address and packed value. A hit replaces x/y with the precise position (GEOMETRY/FULL). If every vertex of a textured polygon has a usable depth (at least 64), the depth goes in the `w` field (TEXTURES/FULL). A miss keeps the retail position, so 2D elements, sprites and anything edited after projection draw as before.
- **Renderer (`platform/native_renderer.c`, `include/platform/native_renderer_types.h`).** `GrVertex` stores float `x, y, w` (32 bytes), with texture page and CLUT in `a_page_clut`. The vertex shader multiplies `gl_Position` by `w` when `w > 0`: the screen position is unchanged but texture coordinates interpolate perspective-correct. Colour and dither coordinates are `noperspective`, so Gouraud shading and dithering stay screen-linear as on the PS1. Vertices without depth have `w = 0`, treated as 1, so the same shaders serve every mode.

## Precise camera

Sub-pixel vertices only help if the camera is smooth, and the game's camera is integer: position in whole world units, angles in 4096ths of a turn, from `ratan2` of integer vectors about 200 units long, so each one-unit change turns the view about 0.3°. While the camera-to-kart vector changes (accelerating, braking, turning) the whole screen steps. With GEOMETRY or FULL on, the renderer uses the same camera without the rounding (`platform/native_pgxp_view.c`). The game keeps its integer camera for everything it decides (culling, LOD, audio).

- **Camera.** `CAM_FollowDriver_Normal` hands its inputs and integer results to `NativePgxp_ViewFollowDriver`, which recomputes the orbit and look-at points in floating point around the kart's exact position (`posCurr` with its 8 fractional bits; rounding it to whole units makes the world step unevenly while speed changes). The kart's model (`driverInst`), tires, shadow and particles are registered at that exact position too ([camera-relative pieces](#precise-camera)), so the kart stays put on screen. What the GTE's flooring lost (up to two units per point) is carried onto the game's integer points, so the game's own adjustments (blast lerp, terrain floor, height smoothing) still apply. Angles come from `atan2` of the result, and the PushBuffer position deadband (`pushBufferPosCorrection`, +-2 units) runs unrounded beside the game's. Other camera modes (fly-in, transitions, end of race, first person) stay integer.
- **Camera speed (both frame rates, any PGXP mode).** Distance and pitch follow `cameraMoveSpeed`, a filter of the kart's speed. Retail's inward weights (keep `0xF0`, give `0x100 - 0xFF`) sum to 241/256, so while the kart slows each frame drops it 6% below the kart's speed and the outward filter climbs back, and distance and pitch saw in steps. `CAM_FollowDriver_Normal` stops the drop at the kart's speed: a hard stop still pulls in at 6% a frame, a gentle one follows smoothly.
- **View matrix.** `PushBuffer_SetMatrixVP` registers the precise view next to the integer `matrix_ViewProj`, as long as the PushBuffer still holds the integer pose the precise one was computed with. Each row's scale (aspect, widescreen, Archipelago mirror and upside-down traps) is measured from the integer matrix, so whatever the game applies to the view applies to the precise one.
- **Lookup by value.** The GTE finds the precise matrix by the integer one it was given (`NativePgxp_ViewLookup`, from `GTE_RotTransPers` and MVMVA). The camera's rotation words with any translation are geometry placed relative to the integer camera (the level, the sky, the camera-relative pieces below), and the translation is converted to the precise camera. The view's own translation (used by the level and sky) is replaced by the exact one: the game builds it as integer rotation times camera position, truncated and scaled for aspect, so converting it would move nearby ground up to half a pixel whenever the integer rotation steps. Instance model-view matrices (`ViewProj * model`) are registered one by one from `RenderBucket_QueueDraw`. Camera-facing models (`RenderBucket_BuildCustomMatrix`, such as the hub's warp-pad numbers) have no view rotation in their matrix, so `NativePgxp_ViewRegisterFacing` turns them towards the precise view position instead of the integer one. Split and screen-space models keep the integer camera. The fifth matrix word holds a lone R33, which the GTE register sign-extends, so it is compared sign-extended.
- **Screen-space pieces.** Code that copies GTE results into primitives by hand must attach the precise positions itself (`NativePgxp_StoreGteSXY`), as `DrawSky` does, or it draws with the integer camera. The horizon glow (`CAM_SkyboxGlow`) has no GTE: retail places its bands from the integer pitch and roll, so with the precise view it is drawn unclipped from the precise angles (`NativePgxp_ViewGetAngles`, `NativePgxp_SetXY`). The one piece still integer is the clear-colour split behind the sky (`CAM_ClearScreen`, a `TILE`).
- **Camera-relative pieces.** Tires (`DrawTires.c`), ground shadows (`VehGroundShadow.c`), skid marks (`VehGroundSkids.c`) and particles (`Particle_RenderList`) take positions relative to the integer camera themselves, times 4, and project them through the camera's rotation with no translation; near the camera one unit of rounding moves them a pixel or more. Between `NativePgxp_ViewBeginRelative` and `NativePgxp_ViewEndRelative`, the view lookup adds the camera's sub-unit shift at that scale and, for the followed kart's own pieces, the kart's sub-unit fraction (not its height for the shadow, which lies on the ground, nor for reflected tires, which would need it mirrored). Each copies screen positions into primitives by hand and attaches the precise ones (`NativePgxp_StoreGteSXY`, `NativePgxp_CopyXY`). A particle's corners are projected around its view-space centre with a screen-facing rotation, so the centre's exact value rides along as the GTE translation (`NativePgxp_GteSetTranslation`, used when the rotation has no precise view). A tire is a camera-facing sprite whose two axes the game builds from the integer direction to the wheel through rounded divisions; they are rebuilt in floating point from the precise direction (`NativePgxp_ViewRelativeShift`), and the corners go to the GTE as precise inputs.
- **Checking it.** The [rounding detector](DEV-TOOLS.md#rounding-detector) (`tools/decrypted/nudge.ps1`) lists everything in a frame still drawn from the integer camera. `CTR_DEV_TRACE` records the integer camera (`cx`..`crz`), the precise one (`pcx`..`pcrz`) and the kart's exact position (`kx`..`kz`), and `race_trace.py smooth` measures how unevenly each moves ([DEV-TOOLS.md](DEV-TOOLS.md#unattended-races)): the precise camera's pitch and position should be far smoother than the integer one's, with the remaining yaw being the kart's own steering. With `pgxp=0` frames must be identical to a build without PGXP, and `find_holes.py` totals must not rise.

## Level depth test

The PS1 has no depth buffer. It draws in ordering-table order, and the level renderer sorts coarsely: the 16 faces of a 4x4 quadblock share one slot, from the block's farthest corner. So water near the camera can paint over a nearer surface, in retail too (water over the N. Sanity Beach dock with the far camera). With TEXTURES or FULL, each level vertex has its view depth, and the level's water tests it:

- **Which polygons.** The level is the run of primitives `DrawLevelOvr1P`..`4P` write each frame (`NativeGpu_SetLevelPrimRange` from `RenderAllLevelGeometry`, cleared by `MainFrame_ResetDB`). Its water is what the two water buckets write, reported by `Ovr226_800a1e30_DrawWaterBspList` and the water case of `DrawLevelOvr1P_DrawRenderedQuadBlocks` (`NativeGpu_AddLevelWaterRange`), shared by 1P to 4P. Karts, instances, shadows, particles, the sky and the HUD keep ordering-table order exactly.
- **Only water tests.** Every other level polygon, opaque or translucent, draws in ordering-table order and writes its true depth (`GL_ALWAYS`), so the depth buffer holds the surface on screen. The art relies on that order: the cave between N. Sanity Beach and Gemstone Valley draws a dark lining, then a translucent wall over it that the lining crosses; depth-testing the wall draws black shapes on it.
- **How water tests.** `GL_LEQUAL` against a depth pulled 2% of the distance nearer (`NATIVE_DEPTH_TOLERANCE`), writing none, so water lying on a surface still wins near-ties as the ordering table intended.
- **Depth value.** `gl_Position.z = w - 32`, so window depth is `1 - 16/w`, linear in screen space so it interpolates correctly; nearer than 16 clamps (`GL_DEPTH_CLAMP`). A polygon with no depth (untextured, or a vertex nearer than 64) keeps ordering-table order: it writes the far plane, and as water it is never hidden.
- **Checking it.** `CTR_DEV_NO_LEVEL_DEPTH=1` turns it off for A/B captures ([DEV-TOOLS.md](DEV-TOOLS.md)). Away from water the two must match exactly, e.g. driving into the cave: `CTR_DEV_RACE=26`, `CTR_DEV_MODE=adv`, `CTR_DEV_WARP=-14700,150,-540,0`, `CTR_DEV_PAD=4400:X:500,4480:L:110`, frames 4540 to 4720, 0 pixels differ. At the dock with the far camera (`CTR_DEV_WARP=-25494,330,-8852,0`, `CTR_DEV_PAD=4050:2`, frame 4120) about 6,700 pixels change as the water leaves the planks. With `pgxp=0` frames must be identical.

## Back-face culling

The level renderer culls back faces with the GTE's NCLIP (the projected triangle's signed area), and NCLIP uses the same precise vertices the GPU draws with whenever GEOMETRY or FULL is on (`gNativePgxpIntegerNclipEnabled` = 0; High Octane's equivalent `pgxp_precise_nclip` is off by default). A distant floor polygon seen almost edge-on is a fraction of a pixel tall; with integer vertices its area rounds to zero or flips sign and it is culled. In retail its neighbours round onto the same rows so nothing shows, but with precise vertices the strip opens and the sky shows through. Only rendering calls NCLIP (the level renderer and `RenderBucket_QueueExecute.c`), so gameplay is unaffected. To check: `find_holes.py` totals with `CTR_PGXP_DEBUG=1` should match the retail baseline (`CTR_PGXP_DEBUG=3`).

## Texture sampling

- **Per-polygon UV clamp.** `GrVertex` carries the polygon's texel bounds (`uMin`, `vMin`, `uMax`, `vMax`, from `NativeGpu_SetUvLimits` in `platform/native_gpu.c`), and the fragment shader clamps to them. At grazing angles the perspective-correct coordinate can land a fraction of a texel outside the polygon's texels, often on the transparent colour, which shows as a hole (DuckStation does the same). All-zero bounds (rectangles, lines) mean no clamp.
- **Flat page and CLUT.** `v_page_clut` and `v_uvLimits` are `flat`: they are constant per polygon, and interpolating a constant perspective-correctly drifts by float error when vertex depths differ a lot.
- **One rounding for word and index.** The 4- and 8-bit samplers floor the coordinate once and derive both the VRAM word (fetched at its centre) and the index inside it from that value. Rounding them separately reads the right word with the wrong index just below a word boundary: a one-texel column of wrong colour.

## Debugging

`CTR_PGXP_DEBUG` (environment variable) tints polygons:

| Value | Tint |
|---|---|
| 1 | By how many vertices PGXP recovered: red for some, blue for none, untinted for all. The sky backdrop is blue, so holes show as blue |
| 2 | As 1, and recovered polygons switch between precise and retail positions every 2.5 seconds, tinted green in the retail phase |
| 3 | As 1, but always at retail positions with affine textures: the baseline to compare against |
| 4 | Level geometry by the overlay 226 bucket that emitted it: red 4x4, yellow dynamic, green 4x2, cyan 4x1 (low LOD), lilac water, orange full dynamic, white near-plane clipped |

For reproducible captures, measurements and polygon dumps, use the tools in [DEV-TOOLS.md](DEV-TOOLS.md).

## Differences from High Octane

- High Octane's renderer is a multithreaded frontend/backend with Vita paths; ours is dowlle/ctr-native-ap's single-threaded one. Only the PC shader path is used, always compiled in (no `PGXP_PC_TEXTURES` define). All `__vita__` code is removed, and every mode is available (High Octane disables geometry on Vita).
- High Octane keeps a static 512 KiB primitive arena. Ours are two static 2 MiB arenas (`CTR_NATIVE_PRIM_ARENA_BYTES`, `platform/native_memory.c`), registered on every level load (`MainInit_PrimMem`), so the shadow table covers both and is reallocated on level load. It is allocated even while PGXP is off.
- High Octane's own mirror mode flips screen x in the GTE. We don't have that mode, so its mirror handling is dropped.
- The menu and config live in our config system (`platform/native_config.c`, `game/230/MM_ConfigMenu.c`, `include/ctr_menu_pages.h`).

## Known limitations

- Precision only reaches primitives whose vertices the game writes straight from the GTE (or copies through the hooked level-renderer helpers). Geometry built or edited in integer space after projection falls back to retail positions for those vertices.
- A polygon with some vertices recovered and some not draws with mixed positions, deliberately (as in High Octane), so shared corners stay precise.
- Perspective-correct texturing needs every vertex to have a depth. Polygons partly behind the near plane, or very close, keep affine mapping. They also have no depth for the [level depth test](#level-depth-test), so water that close can still paint over nearer ground, as in retail.
- Larger vertices cost about 2 MiB more per vertex buffer, 6 MiB in all (the CPU array plus two GL buffers), and the shadow table takes roughly 5 bytes per byte of primitive arena (about 20 MiB with the two 2 MiB arenas).
