# High Octane fix integration

Open integration work for the **High Octane fixes** row in [ROADMAP.md](../ROADMAP.md). Commit IDs identify the source patches available in the local Git object database. This is a source audit, not a claim that every reported symptom has been reproduced. Delete completed rows; keep standing renderer rules in [LEVEL-VISIBILITY.md](LEVEL-VISIBILITY.md) and [PGXP.md](PGXP.md).

## What still needs work

| Source commit | Area | Current code and integration decision |
|---|---|---|
| `b5adce252` | Garage stat bars | **Missing.** Vanilla draws in `CS_Garage_MenuProc`; AP draws through `AP_StatBar_Draw`, also used outside the garage. Scale outline, shadow and segment endpoints in both paths, preserving logical stat lengths and AP progression. A garage-only port misses the AP build. |
| `9f21a312f` | Speedometer | **Missing.** `UI_Speedometer.c` still adds unscaled background X offsets. Audit the needle and labels as well as the source patch's background changes so they share an anchor and aspect correction. |
| `c8cae44bf` | Minimap seam | **Needs reproduction.** `UI_Map_DrawMap` has no one-pixel overlap, but our shader already clamps polygon UVs and fixes texel rounding. Capture the join before changing geometry; an unnecessary overlap can distort the map or its translucent outline. Preserve maps that draw only the bottom half. |
| `baa505759` | Menu shadows | **Missing source change; reproduce symptom.** `RECTMENU_DrawSelf` still uses a 12-pixel hierarchy gap. Check nested menus in our layouts before adopting the native two-pixel increase. |
| `7a839ca99` | Track preview | **Different implementation.** Our `MM_TrackSelect_Video_DrawNativePreview` uses a VRAM rectangle through `DISPLAY_Blur_SubFunc`; the source patch assumes a streaming texture. Check aspect and blending, then adapt to our path or integrate the texture presentation infrastructure first. Do not apply its packet links verbatim. |
| `714b2c9f8` | Multiplayer decals | **Still active.** `MainFrame_RenderFrame` calls `DecalMP_01/02/03`. These share the offscreen-rendering setup with the Wumpa HUD. Treat removal as part of the HUD/viewport change, not an isolated deletion. |
| `7417c03b3` | Two-player PVS/LOD | **Partial overlap.** Our native geometry ignores restrictive PVS lists, and draw-distance scaling already covers split-screen limits. We still use `DrawLevelOvr2P`, whereas the source routes each viewport through a context-aware 1P renderer. Reproduce a remaining emitter/LOD defect before replacing that path; preserve our per-face visibility, clip scratch and draw-distance rules. |
| `c25fcd3b8` | Wumpa HUD/viewports | **Mixed patch.** Its game-side Wumpa changes are Vita-guarded and are not a PC fix to copy. Its PC scissor changes intersect clip rectangles with the display and round shared edges consistently; our renderer still scales floating origins and widths independently. Reproduce at Native render scale with odd target dimensions, then adapt the scissor calculation while preserving draw-area semantics. |
| `7eb76d830` | Scrapbook presentation | **Missing.** `MM_Scrapbook_PlayMovie` uploads frames to VRAM and pins a VRAM rectangle. Add streaming-texture presentation with correct orientation, aspect, black borders and renderer-state restoration before changing the decoder's callers. Preserve direct frame capture and render-scale behaviour. |
| `276ad94fa` | Scrapbook decoding/audio | **Missing streaming infrastructure.** Our reader can skip unrelated CD records but returns failure on an incomplete frame; playback uses the older XA-file path. Adapt bounded interleaved XA feeding, damaged-frame recovery and texture upload together after presentation support. Preserve fast deterministic VBlank audio, skip/EOF cleanup and normal audio playback. |

The Crash Cove geometry PVS workaround (`8ababf381`) is superseded by native geometry visibility in `MainFrame_VisMemFullFrame`; do not add the local ship hack. The VS/Battle results Wumpa suppression (`8e27b0011`) already has an equivalent `END_OF_RACE` gate in `UI_RenderFrame_Wumpa3D_2P3P4P`. Both are recorded as `own` in `tools/decrypted/highoctane.txt`. Keep results captures in the multiplayer regression checks to ensure the existing gate still works with any new HUD path.

## Integration order

Work in small commits, with the relevant documentation and status entry updated in each. A missing source change alone does not establish a bug. For a symptom that retail may share, compare the unattended retail reference described in the local project instructions before choosing a behaviour change.

1. **Small UI fixes.** Garage bars first, then speedometer, then nested-menu spacing, one at a time. Capture AP and vanilla at 4:3 and 16:9. For bars, check zero, partial and full lengths and the AP hub picker; for the speedometer, check the needle at rest and speed. Reproduce the minimap join and track-preview issue before choosing their adaptations.
2. **Multiplayer rendering.** Fix scissor edges independently first. Capture odd-sized Native targets and fixed render scales in 2P/3P/4P. Change decals and Wumpa rendering together only if captures establish a defect. Replace the 2P geometry emitter only for a reproduced remaining problem, preserving the per-player retry storage and bounds.
3. **Scrapbook.** Integrate texture presentation, then decoder/interleaved audio support. Add a direct unattended scrapbook entry if the ending route is too slow. Check first frame, sustained playback, damaged-frame handling, audio synchronization, skip, natural EOF and returning to menus, at both frame rates and Original/Native render scale.

## Validation and handoff

- Use `tools/decrypted/devrun.ps1` with explicit run/output folders for captures. Existing switches cover `CTR_DEV_CHARACTER_SELECT`, `CTR_DEV_UNLOCKS`, `CTR_DEV_PLAYERS`, `CTR_DEV_RACE`, `CTR_DEV_AUTOPILOT`, `CTR_DEV_AUDIO` and `CTR_DEV_ENDING`; see [DEV-TOOLS.md](DEV-TOOLS.md). Add a dev switch where a scene cannot be reached reliably. Do not automate desktop input.
- Capture the baseline before editing and use identical frame counts, config and inputs afterward. Compare changed regions numerically and inspect the images; unchanged 4:3 behaviour is a regression check, not proof of a widescreen fix. Geometry changes also require `find_holes.py` totals against the baseline.
- Build AP and vanilla for each game-code change, and run focused harnesses. Changes shared with authoring clients require all client variants; renderer/audio changes require Linux CI as well as the Windows build.
- Preserve `CTR_AP` hooks, custom-track policy/diagnostics, native render scale, deterministic audio and non-native retail paths. Source patches include unrelated formatting and Vita/AdHoc assumptions; port only the required behaviour and credit the source commit.
- Remove a completed row here and from the roadmap, and change its `highoctane.txt` status in the same commit. Leave unresolved reproduction and integration work here so the next change needs no chat history.
