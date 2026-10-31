# Roadmap

Status of CTR Decrypted, kept current with every change. **Status values:** Done, In progress, Planned, Maybe, Not planned.

- How the project is set up and how to work on it: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream projects' issue trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game and its Archipelago client. The `ctr.apworld` currently ships unchanged from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld). If we change generation logic, the world source moves into `apworld/` here, so the client and world are always versioned together.
- **Base: dowlle/ctr-native-ap `main`** (v0.2.4 plus remappable controls and the hook split). It had the Archipelago client, a config menu, widescreen, borderless fullscreen, a dithering toggle, remappable controls and CI. Seeds made with its `ctr.apworld` 0.2.4 work.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files (351 hunks), and High Octane carries Vita, web and leaderboard code we don't want.
- **Windows first.** The Linux build from upstream should keep compiling, but only Windows is tested.

## Done
| Item | Notes |
|---|---|
| Repo set up on dowlle/ctr-native-ap `main` | Imported through `tools/decrypted/sync-upstream.ps1`. `*.patch` files forced to LF so the vendor hash check passes with `core.autocrlf=true` |
| Native Switch 2 Pro Controller | Vendored libusb 1.0.30 (`externals/libusb`), so SDL's Switch 2 driver works without Steam. Steam must not be running, or it grabs the pad first |
| PGXP: anti-wobble and perspective-correct textures | OPTIONS → Video → PGXP (Anti-Wobble), default FULL. Port of High Octane 03014d84f, plus fixes for distant-floor holes and texture sampler rounding. See [docs/PGXP.md](docs/PGXP.md) |
| 60 FPS mode | OPTIONS → Video → 60 FPS (Next Load), on by default, every mode including Time Trial. Port of High Octane's option plus fork fixes: ghosts and AI lap recordings keep 30 FPS units, replays and savestates keep their mode, AI speed. Checked unattended on every track; what only a player can check is listed in [docs/60FPS.md](docs/60FPS.md) |
| Developer tools for unattended runs | `CTR_DEV_*` switches: attract demo or race on any track, autopilot, traces, fast-forward, hidden window, frame capture, polygon dumps, all bit-exact. `devrun.ps1`, `fps-parity.ps1` and analysis scripts; `--perf` budget follows the frame rate. See [docs/DEV-TOOLS.md](docs/DEV-TOOLS.md) |
| Archipelago: starting racer and hub picker | All eight vanilla starters unlocked → the garage picker runs at a new profile. 50% black backdrop behind the in-hub picker |
| Screen filters: Sharp Bilinear, FXAA, Scanlines, CRT | OPTIONS → Video → Screen Filter, replacing Smooth Scaling, at every render scale (internal resolution scaling was already in the base). Written for this renderer rather than ported from High Octane's Vita-tuned shaders. Measured from captures of the presented image. See [docs/SCREEN-FILTERS.md](docs/SCREEN-FILTERS.md) |
| Playable N. Oxide | Unlocked by beating him in Adventure (older saves unlock on loading such a profile) or by the Spyro 2 demo code, which used to hang the PC build. He takes a 16th icon in Arcade, VS, Battle and Time Trial character select in every player count; while locked the screen is exactly retail. His menu model is read from the player's disc instead of shipping High Octane's copy of it. Races, Time Trial, VS and Adventure as Oxide checked unattended; see `game/230/MM_Characters.c` |
| Extras: reserves meter, keep reserve fire, blue fire at USF, slide meter gradient, cyan super pads | OPTIONS → Extras, each its own row, default off (the reserves meter defaults on in the Archipelago build, which always drew it). The ModSDK mods as High Octane ships them, sharing one implementation with the Archipelago build's meter and blue fire. Visual only: a race trace is identical with all of them on. Cyan pads are found from each level's trigger and pad-animation data (no per-track exceptions, and the all-super-pads cheat turns every pad cyan) and coloured in the shader through a spare palette bit. See [docs/EXTRAS.md](docs/EXTRAS.md) |
| Frame pacing with VSync on a 60 Hz display | Measured overnight with `tools/decrypted/vsync-check.ps1`: 30 and 60 FPS, VSync Off, On and Adaptive, windowed and fullscreen are all even (every frame one refresh at 60 FPS, two at 30; no uneven or long frame in about a minute each), so no pacing change was needed. A 144 Hz display is not measured; 60 FPS cannot be even there without variable refresh. See [docs/60FPS.md](docs/60FPS.md) |
| Boot splash centred when the window starts maximized | `NativeRenderer_UpdatePresentationViewport` reads the live window size |
| Upstream bug reports closed | Fixed: Mystery Caves crash with 3–4 players (ctr-native #58); Oxide missing from first place on the podium (ctr-native-ap #282, report 1: his win-dance file is empty on the disc, so he wears his lose-dance model, as the ending cutscene does). Already fine here, checked unattended: credits and scrapbook (ctr-native #45, #52), reconnect catch-up (ctr-native-ap #147), stat bars (#466), vanilla-mode stage 2 keys (#499, via the apworld we ship). Details in [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md) |
| Cup standings fixes (retail bugs) | Podium racers missing after a Purple Gem Cup that follows an eight-racer cup (ctr-native-ap #282, report 2): the cup's final order kept stale slots past the field. An Arcade or VS tie for first went to the driver whose index matched the tie-break slot instead of the racer who won it. In a 1P Arcade cup, the bot in slot 1 winning gave the player completion credit. Reproduced with `CTR_DEV_MODE=cup`, which runs Gem Cups back to back; `tools/test-cup-ranks.py` runs the real ranking code over every field size and cup order |
| Line endings for every git | `* text=auto` in `.gitattributes`: MSYS2's git, which has no `core.autocrlf`, no longer sees every file of a Windows checkout as modified |
| Repository cleanup | Only `main`, `dev` and the `clean/*` import refs remain; merged feature and `ci/*` branches and the extra worktree are gone. The root and `tools/` keep dowlle's layout on purpose: moving upstream files would conflict on every sync, and the root files ship in the client packages (reasoning in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)). `client-artifacts/` is ignored |
| Faster CI | Build clients went from 15 to 19 minutes to about 3.5 minutes (about 7 with an empty compiler cache). Each variant builds in its own parallel job on both platforms, compiles go through ccache (by compiler content, without precompiled headers, and with the commit hash moved out of `main.c` into `platform/native_build_id.c`, so a commit that leaves the game code alone no longer recompiles all of it), MSYS2 packages are cached, and builds use every core. Vendor verification hashed each file in its own process, 2m20s on Windows; one `sha256sum` call gives the same hashes in under a second. Host harnesses run in parallel. Both workflows run on `dev`; other branches start them with `gh workflow run`. Tag builds skip the compiler cache. The LF checkout step in `build.yml` stays: it makes packaged text files identical on Windows and Linux |
| Floor vanishing with Increase Draw Distance | Owner playtest 2026-10-31: no floor from 8th on the Hot Air Skyway grid, floor textures vanishing mid-race. With `increase_draw_distance` the level renderer ran out of primitive memory on 722 of 724 sampled frames. The arena was the PS1's per-level budget inside its 2 MiB of RAM, and the buckets draw far to near, so the floor under the camera was what got dropped. Native builds now give both arenas 1 MiB of host memory on every load (`MainInit_PrimMem`, `platform/native_memory.c`); Hot Air Skyway peaks at about 140 KB. Standing 8th on the grid, sky showing through the floor went from 1,366,329 to 172,323 pixels (the rest is open sky beside the track). This replaces upstream's custom-track-only 1 MiB floor (decision 8 in `native_custom_tracks_policy.h`). The N. Sanity Beach bridge from the same playtest (also with draw distance on, in a hub whose retail arena was already 93% full) fits the same cause, but that spot is not reached unattended yet: check it in game |
| Giant polygons across the screen | Owner playtest 2026-10-31, Hot Air Skyway. The PS1 GPU skips any triangle 1024 or more pixels wide or 512 tall; the native GPU drew them. Effects that join a visible point to one behind the camera (clamped to x = -1024) became dark sheets across the sky. `NativeGpu_CommitTriangle`/`CommitQuad` in `platform/native_gpu.c` now apply the hardware rule per triangle on the retail integer positions. Over a lap of Hot Air Skyway that removes 3,189 such triangles at default settings and 53,625 with draw distance on. `increase_draw_distance` also no longer relaxes the model near-reject in `RenderBucket_AcceptProjectedBounds`; its comment called it a far-Z reject, but it let models at the camera through |
| Races that played differently from run to run | Found while checking the above: the same build, run twice, could play a race differently. Retail `VehLap_UpdateProgress` loads a bot's checkpoint index with `lbu` and only bails on a negative one, so a bot on a quadblock with no checkpoint (0xff) reads 255 entries past the checkpoint array. On the PS1 those bytes are fixed; natively they include host pointers, which move with the load address (ASLR), so rankings and then the race changed with where Windows loaded the exe, and between builds. 0xff now means no checkpoint for bots, as it already did for players. All 18 race tracks' demos now match across two fixed load bases. Method in [docs/DEV-TOOLS.md](docs/DEV-TOOLS.md#determinism-and-load-addresses) |

## In progress
Nothing.

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| Wobble while accelerating, braking and turning | Owner playtest, 2026-10-31. Driving straight at constant speed is smooth with PGXP, but the wobble is still visible while speeding up, slowing down and probably in turns |
| Widescreen culling | Owner playtest, 2026-10-31. In widescreen, geometry at the left and right edges is unloaded while still on screen. Not reproduced on Hot Air Skyway at 16:9 or 21:9: the BSP frustum is widened correctly (the corners go through the unscaled camera matrix), model culling is in the compressed screen space, and ignoring the precomputed visibility lists changes almost nothing at the edges. Needs the place it was seen |
| Racers moving sideways at the race start | Owner playtest, 2026-10-31, Hot Air Skyway. The AI racers moved strangely sideways at the start, maybe bumping into each other, maybe a 60 FPS timing bug |
| Flicker in the flag transition back to the hub | Owner playtest, 2026-10-31. Leaving a race for the Adventure hub, the checkered flag transition flickers |
| White noise in the hub after winning a race | ctr-native-ap #484. Not reproduced: after an Adventure win on Crash Cove, through the podium into N. Sanity Beach, `audio_noise.py` finds no noise in either build, fast-forwarded or real time. Nor over several races in a row: the Yellow and Purple Gem Cups back to back (`CTR_DEV_MODE=cup`, eight races, two podiums, two hub returns, 42 minutes of game audio) have none. What is left untested from the report is a connected Archipelago session |
| Merge newer ctr-native upstream work (decomp matching, gameplay fixes since July 2026) | Both forks are 100+ commits behind. Expect ~460 conflict hunks |
| Pre-race camera flyover is wrong | ctr-native #14 |

## Maybe
- Retail-exact physics at 60 FPS: run the simulation at a fixed 30 Hz and interpolate rendering. Today's 60 FPS integrates in 16 ms steps, so curved kickers launch a little higher (Dingo Canyon: apex rise 24 at 30 FPS, 31 to 33 at 60). A different design from High Octane's, touching every rendered position.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play.
- Custom racers (`.ctrr`) and High Octane's ghost replay tools.
- A 4:3 / 5:4 aspect option for CRTs (High Octane #44).
- High Octane's colour effects as more Screen Filter entries: Gamma, Greyscale, Hue, Negative, Sepia, Vignette, LCD 3x, colour-blind modes.
- Following dowlle's 0.3.0 work once it is released: boss randomization, Wumpa shop, solo mode, custom tracks.

- Exact pointer tests in the level renderer: `226_00_DrawLevelOvr1P.c` tells a hi-res texture pointer from inline texture data by the word's value (inside the level buffer, word-aligned, plausible page). Checked against the loader's pointer map on Hot Air Skyway in both builds: no disagreement in 100,000 lookups, so it is left alone. The exact version needs a flag carried next to scratchpad word 0x84.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- PAL/JP disc support. Upstream removed it on purpose (ctr-native #51).
