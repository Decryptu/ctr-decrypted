# Level visibility

What decides which faces of a track, its sky and the karts' wheels are drawn far away, and what the native version changes. Read this before touching level culling in `game/MAIN/MainFrame.c`, `game/MAIN/MainFrame_RenderFrame.c`, the level renderers (`game/226` to `game/229`), `game/DrawSky.c` or `game/DrawTires.c`.

## The stages
A level face is drawn only if it passes every stage, in this order:

1. **Visibility lists (PVS).** Each quadblock can carry precomputed lists: the BSP leaves and quadblocks visible from it, the level instances near it, and the water and vertex-colour vertices to animate. The camera picks the lists of the quadblock under it (`CAM.c`, after `CAM_FindClosestQuadblock`), and `MainFrame_VisMemFullFrame` copies them into `gGT->visMem1`. The lists were baked for the retail view: 4:3, the retail draw distance and the camera positions the developers sampled. **Native ignores them for level geometry**: `MainFrame_VisMemFullFrame` sets every leaf, quadblock, water vertex and vertex-colour vertex visible each frame. Instances keep their lists, because warp pads (`AH_WarpPad.c`) and cutscenes (`CS_Instance.c`) read them as gameplay state.
2. **Frustum.** The BSP walk drops leaves outside the view. Widescreen widens it (`Widescreen_ScaleFrustumX`).
3. **Distance.** Far leaves and quadblocks drop out, and textures and models change LOD by distance. `increase_draw_distance` moves every one of these limits out by the same factor, 4 (`include/platform/native_draw_distance.h`): the frustum's far clip (`PushBuffer.c`), the depth where a BSP leaf goes to the cheaper full-dynamic emitter (1P and split screen), a model's LOD distance, and the depths that pick a level texture's LOD and how finely a quadblock is subdivided. The last two go together: a quadblock's full-resolution texture only loads in the deepest subdivision, so scaling only the texture LOD would leave far faces blurry. Without one factor the limits cut in at different distances, and whatever the nearest one drops is what the player sees go.
4. **Near clip.** Faces that cross the near plane are clipped through clip records. A face is clipped only when one of its corners is inside a box in front of the camera (`DrawLevelOvr1P_SourceInsideClipRecordWindow`); otherwise it is dropped whole. The box widens with the aspect ratio.

## Why the lists are off
With widescreen, the far camera (`L2`) or `increase_draw_distance`, the camera sees past what its quadblock's lists hold. Whole stretches of far road and scenery then vanish, and come back a step later when the camera reaches a quadblock with other lists. On Hot Air Skyway: far road under the overhead platform at the loop, and the road behind the guardrails near the end of the lap. A split-screen camera can even lose the floor right under its kart.

The lists only saved PS1 time. Measured at 16:9 with the far camera, autopilot driving every track:
- Without the lists, the primitive arena peak per track is 120 to 292 KB (79 to 130 KB with them).
- With `increase_draw_distance` too: 235 to 586 KB in 1P, up to 820 KB in two-player split screen (N. Gin Labs) and 365 KB in four-player. The arena is 2 MB (`CTR_NATIVE_PRIM_ARENA_BYTES`, `platform/native_memory.c`). `[CTR Dev] primitive arena peak` in the log of a dev run gives it.
- Frame work on N. Gin Labs, the heaviest, with `increase_draw_distance`: about 1.5 ms a frame more than the retail limits in 1P and 2.7 ms more in 2P, out of 16.7 at 60 FPS. The option is off by default, the Steam Deck defaults included.

Faces the camera can't actually see are still drawn when they lie behind nearer ones. The ordering table puts them earlier, so nearer faces paint over them. In the A/B captures of all 18 tracks, every changed pixel is geometry the lists had dropped.

## The sky
The sky (`game/DrawSky.c`) is a dome of large gouraud triangles around the camera, cut into eight 45-degree slices. Retail draws the four slices facing the camera and drops a face whole when the GTE flags a corner too close to the camera plane, and the GPU drops any triangle 512 or more pixels tall (`NativeGpu_HardwareSkipsTriangle`). From the far camera and in widescreen, a face that still covers a corner of the screen hits one of those, and the clear colour shows through in a hard-edged patch until the camera moves on.

Native draws all eight slices and sends every face through `DrawSky_ClipFace`: clipped in view space against the near plane and the screen's edges (8 pixels outside), projected through the same view as the level (the precise one under PGXP), drawn as a fan with colours interpolated at the cuts. The pieces stay within the GPU's limits and keep their true shape. Every face goes this way, not only the ones retail would drop, so neighbours share bit-identical corners: mixing GTE-projected and clipped faces left hairline seams.

## Distant wheels
A kart's tires are four camera-facing sprites (`game/DrawTires.c`), drawn only at the kart's top model LOD. Their centres are taken relative to the camera at 4 times world scale and go through the GTE's 16-bit vectors, which hold about 8,000 world units. Retail never drew a top-LOD kart that far; with `increase_draw_distance` it does. Such a wheel keeps its full-width centre (the GTE's 32-bit MAC1 to MAC3 rather than the saturated IR1 to IR3), takes its facing from it, and has its corners projected by `NativeGte_ProjectWide` (`platform/native_gte_core.c`), an RTPS with a 32-bit input that advances the same registers and PGXP state. Wheels that fit keep the retail GTE path bit for bit. Packing an out-of-range corner into 16 bits wraps it to the other side of the camera, which draws a long dark sliver across the screen.

## Checking it
- `CTR_DEV_RETAIL_CULL=1` keeps the retail lists and the retail sky, for A/B captures ([DEV-TOOLS.md](DEV-TOOLS.md)). Every pixel that differs should be far geometry the lists dropped or sky that was missing, never a near face overdrawn. The clear colour must not show anywhere in a frame with the sky: on Hot Air Skyway at `CTR_DEV_WARP=10799,325,-2381,2271` with the far camera, 5,665 clear-colour pixels with the retail sky, none without, with or without PGXP.
- Distant wheels: the wheel A/B over all 18 tracks changes no pixel where wheels fit, and on Dingo Canyon (frame 3390 of a `CTR_DEV_RACE=0` autopilot run) Coco's and Cortex's far karts get their wheels back. `CTR_DEV_DUMP_FRAME` lists them as `@tires` quads next to the karts' own polygons.
- A spot a player reports: they press `F7` there, and `where.txt` in the capture gives the `CTR_DEV_WARP` line. Warp there with `CTR_DEV_PAD=2400:2` for the far camera and capture both ways.
