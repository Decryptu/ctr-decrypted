# Developer tools: unattended runs, captures and render diagnostics

The game can run with nobody at the controls: a hidden window, a clock that runs as fast as the machine allows, frames captured straight from the renderer, and a fixed attract-mode demo on any track. Runs are bit-exact, so two runs with the same settings produce identical frames and before/after comparisons are reliable. Use this to reproduce visual bugs. Never drive the game with OS keystrokes or wait on a real-time demo.

## Quick start

```powershell
# Build, then capture Dingo Canyon's demo every 100 frames, at 2x render scale:
tools\decrypted\devrun.ps1 -Track 0 -Frames 4800 -From 1500 -Every 100 -Config @{render_scale=2}
```

The script prints the capture folder (`%TEMP%\ctrdev\shots-HHmmss` unless `-Out` is given). It holds `fNNNNNN.png` per captured frame and `sheet.png`, a labelled contact sheet of them all. About 1,300 frames render per second in a race, so a whole demo takes around ten seconds.

`devrun.ps1` runs from its own folder (`%TEMP%\ctrdev\run`) with a baseline `config.ini` (60 FPS, PGXP FULL, 16:9, render scale ORIGINAL), so the owner's install and saves are never touched. The parameters:

| Parameter | Meaning |
|---|---|
| `-Track` | `LevelID` for the attract demo (`include/namespace_Level.h`: 0 Dingo Canyon, 3 Crash Cove, 4 Tiger Temple, 14 Coco Park...). `-1` for no demo |
| `-Frames` | Quit after this many presented frames |
| `-From`, `-Every` | First captured frame, and the capture interval |
| `-Config` | `config.ini` keys to override, e.g. `@{pgxp=0; render_scale=2}` |
| `-Env` | Extra environment variables, e.g. `@{CTR_PGXP_DEBUG=1}` |
| `-Visible`, `-RealTime` | Show the window, or keep wall-clock pacing |
| `-Exe` | Executable to run (default `build-vanilla\ctr_native.exe`) |

The race starts about 1,200 frames into a demo run.

## Environment variables

All of these are read once, by `platform/native_dev.c` (`include/platform/native_dev.h`). With none set, the game behaves exactly as shipped.

| Variable | Effect |
|---|---|
| `CTR_DEV_DEMO=<LevelID>` | The title menu starts the attract demo on that track as soon as it settles. It skips the Oxide intro and the idle wait, every time |
| `CTR_DEV_FAST=1` | VBlanks are emitted without waiting for wall-clock time. The SPU and XA audio streams are driven from the VBlank clock (the replay system's deterministic mode) instead of SDL's real-time callback, because the game waits on them. Audio is muted. Game logic is unchanged |
| `CTR_DEV_HIDDEN=1` | The window is created hidden. Rendering still happens into the main render target |
| `CTR_DEV_SHOT_DIR=<dir>` | Capture frames from the main render target (at render scale) as `fNNNNNN.bmp` |
| `CTR_DEV_SHOT_EVERY=<n>`, `CTR_DEV_SHOT_FROM=<n>` | Capture interval (default 60) and first captured frame (default 0) |
| `CTR_DEV_EXIT_FRAME=<n>` | Quit after n presented frames |
| `CTR_DEV_DUMP_FRAME=<n>` | Write every polygon drawn in frame n to `polys-NNNNNN.txt` in the capture folder: the level-renderer bucket tag, then for each vertex its retail screen position, its drawn position and its precise depth |
| `CTR_PGXP_DEBUG=1..4` | PGXP tints, see [PGXP.md](PGXP.md#debugging) |

Frame numbers count every presented frame, the boot splash included.

## Analysis scripts

- `tools/decrypted/find_holes.py <dir> [--ignore-top N] [--max-run N]` finds see-through holes in a capture made with `CTR_PGXP_DEBUG=1`. In that mode the sky backdrop draws pure blue, so a short vertical run of blue with geometry above and below it is a hole. It prints a count per frame and a total, and writes `holes-*.png` with the holes painted magenta. Use `--ignore-top 110 --max-run 4` at 2x render scale to skip the HUD text. HUD text, boost flames and gaps between distant rocks also register, so compare totals against a baseline run (the same settings with `CTR_PGXP_DEBUG=3`) rather than reading them alone.
- `tools/decrypted/pgxp_cracks.py <polys-NNNNNN.txt> [--level-only]` reads a polygon dump. It reports level vertices drawn at more than one position (a shared corner that disagrees with itself), then T-junction candidates: vertices that lie inside another polygon's edge in retail but not when drawn.

## Worked example: the distant-floor holes

This is how the PGXP floor holes were found (see [PGXP.md](PGXP.md#back-face-culling)).

1. `devrun.ps1 -Track 0 ... -Env @{CTR_PGXP_DEBUG=1}` against `CTR_PGXP_DEBUG=3`, both through `find_holes.py`. PGXP had about 45% more holes than the retail baseline, in modes FULL and GEOMETRY but not TEXTURES, so the cause was vertex positions.
2. `CTR_PGXP_DEBUG=4` showed the holes inside one level bucket (dynamic), not along bucket boundaries.
3. `CTR_DEV_DUMP_FRAME` and `pgxp_cracks.py` showed no shared vertex drawn twice, and T-junction candidates whose depths did not match. So neither theory held.
4. Testing actual hole pixels against the dumped polygons showed the strip between two drawn polygons was covered by no polygon at all. The polygon that belonged there had been culled.
