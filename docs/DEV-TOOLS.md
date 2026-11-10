# Developer tools: unattended runs, captures and render diagnostics

The game can run with nobody at the controls: hidden window, a clock as fast as the machine allows, frames captured straight from the renderer, and an attract-mode demo on any track. Runs are bit-exact, so two runs with the same settings give identical frames. Use this to reproduce visual bugs. Never drive the game with OS keystrokes or wait on a real-time demo.

## Quick start

```powershell
# Build, then capture Dingo Canyon's demo every 100 frames, at 2x render scale:
tools\decrypted\devrun.ps1 -Track 0 -Frames 4800 -From 1500 -Every 100 -Config @{render_scale=2}
```

The script prints the capture folder (`%TEMP%\ctrdev\shots-HHmmss` unless `-Out` is given; the next run deletes default folders older than a day): one `fNNNNNN.png` per captured frame and `sheet.png`, a labelled contact sheet. A race renders about 1,300 frames a second, so a whole demo takes about ten seconds. The race starts about 1,200 frames into a demo run.

`devrun.ps1` runs from its own folder (`%TEMP%\ctrdev\run`) with a baseline `config.ini` (60 FPS, PGXP FULL, 16:9, render scale ORIGINAL), so the owner's install and saves are never touched.

For an unattended intro smoke test, use `-Track -1 -Frames 16000 -From 15000 -Every 500 -Config @{skip_intro='true'}` with no `CTR_DEV_PAD` input; repeat with `fps60='false'`. The intro must swap its cutscene packs and return to the title menu. Adventure mask-hint streaming guards must allow these non-hub swaps.

| Parameter | Meaning |
|---|---|
| `-Track` | `LevelID` for the attract demo (`include/namespace_Level.h`: 0 Dingo Canyon, 3 Crash Cove, 4 Tiger Temple, 14 Coco Park...). `-1` for no demo |
| `-Frames` | Quit after this many presented frames |
| `-From`, `-Every` | First captured frame, and the capture interval |
| `-Config` | `config.ini` keys to override, e.g. `@{pgxp=0; render_scale=2}`. Keys outside `[Video & QoL]` are `Section.key`, e.g. `@{"Connection.uri"="ws://localhost:38300"}` |
| `-Env` | Extra environment variables, e.g. `@{CTR_PGXP_DEBUG=1}` |
| `-Visible`, `-RealTime` | Show the window, or keep wall-clock pacing |
| `-Exe` | Executable (default `build-vanilla\ctr_native.exe`) |
| `-RunDir` | Run folder (default `%TEMP%\ctrdev\run`). Give parallel runs a folder each |
| `-Save` | A save to copy into the run folder's `memcards\slot0` first ([Saves and progress](#saves-and-progress)). Without it the run keeps the save its folder already has |
| `-GameArgs` | Game command-line arguments, e.g. `'--perf-dir','C:\t\perf'` |

## Environment variables

All are read once, by `platform/native_dev.c` (`include/platform/native_dev.h`). With none set, the game behaves exactly as shipped.

| Variable | Effect |
|---|---|
| `CTR_DEV_DEMO=<LevelID>` | The title menu starts the attract demo on that track as soon as it settles, skipping the Oxide intro and idle wait |
| `CTR_DEV_FAST=1` | VBlanks are emitted without waiting for wall-clock time. SPU and XA audio are driven from the VBlank clock (the replay system's deterministic mode) instead of SDL's callback, because the game waits on them. Audio is muted. Game logic is unchanged |
| `CTR_DEV_HIDDEN=1` | Create the window hidden. Rendering still goes to the main render target |
| `CTR_DEV_MOVER_LOG=<file>` | Append each converted-track mover's place and turn every 293 frames, a prime so that no period is sampled at one phase (`CTR_DEV_MOVER_LOG_EVERY=<n>`: every n frames, to follow one mover against `CTR_DEV_TRACE`), and a line per hit on a kart, `hit <kind> <list> <kart> <x> <y> <z>` (`tools/tracks/mover_check.py`), and the same with `push` each frame a solid one pushes a kart out of its box |
| `CTR_DEV_CAMERA_TOUR=<file>` | Views of converted-track movers: lines of `list dx dy dz rx ry rz`, a camera that follows that mover at that offset (`tools/tracks/mover_tour.py`). `CTR_DEV_CAMERA_TOUR_FROM` is the first frame, `CTR_DEV_CAMERA_TOUR_EVERY` the frames a view lasts |
| `CTR_DEV_MOVERS_ONLY=1`, `CTR_DEV_HIDE_MOVERS=1` | Draw no level, so nothing hides a mover; draw no mover, for frames to compare with (`tools/tracks/mover_view.py`) |
| `CTR_DEV_RAM=<log>` | Player 1 drives into each converted-track mover that has a hit box in turn: held at rest on the road `RAM_BACK` behind its hit box (or, if it moves, in its way ahead of it; on the last try straight back from it at its height), then throttle held and steered at it for up to six seconds, three tries, each held a little longer than the last so a Monty Mole is met at another point of its round, and the log gets a line each: what any mover did to the kart (hurt, pushed, crate), its worst kart state, how near it came and whether it went through (`tools/tracks/ram_check.py`) |
| `CTR_DEV_MOVERS_OPEN=1` | A converted-track mover that waits for a lap or a kart runs at once, and keeps moving (`tools/tracks/ram_check.py`) |
| `CTR_DEV_MOVER_POSES=<file>`, `CTR_DEV_MOVERS_REST=1` | In frame `CTR_DEV_MOVER_POSES_FRAME` of the level (default 60), each converted-track mover instance's list, piece, place, matrix and scale as the renderer gets them; every mover holding its placed pose (`tools/tracks/mover_pose_check.py`) |
| `CTR_DEV_MOVERS_HELD=1` | A mover that waits for a lap or a kart, or is out of sight for part of its time, comes out and stays (implied by `CTR_DEV_MOVERS_ONLY`; `tools/tracks/mover_sheet.py`) |
| `CTR_DEV_SHOT_DIR=<dir>` | Capture frames from the main render target (at render scale) as `fNNNNNN.bmp` |
| `CTR_DEV_SHOT_EVERY=<n>`, `CTR_DEV_SHOT_FROM=<n>` | Capture interval (default 60) and first captured frame (default 0) |
| `CTR_DEV_SHOT_PRESENTED=1` | Capture what reaches the window instead: the presentation viewport after the screen filter, at window size. Presents then go through a window-sized target copied to the window, so a hidden window still captures. The Archipelago tracker overlay is drawn later and is not captured |
| `CTR_DEV_EXIT_FRAME=<n>` | Quit after n presented frames |
| `CTR_DEV_MODEL_DUMP=<file>` | Append the models a race has loaded (`gGT->modelPtr`), once per level: id and name. Shows what a custom level can use without carrying its own copy |
| `CTR_DEV_QUAD_DUMP=<csv>` | Append every quadblock of each level as it loads: flags, terrain byte, bounding box, and per texture slot the pointer, palette word, page and animation frame count. A `#` line before each level gives the bytes left in its memory pack. With Cyan Super Pads on, the level is dumped again after the pads are marked (palette words carry 0x8000). Read with `tools/decrypted/quad_dump.py` |
| `CTR_DEV_VRAM_DUMP=<n>` | Save all of VRAM at frame n as `vramNNNNNN.bin` (1024×512 raw 15-bit pixels) in the capture folder. `tools/decrypted/vram_png.py` makes a PNG, never-written words in magenta |
| `CTR_DEV_DUMP_FRAME=<n>` | Write every polygon drawn in frame n to `polys-NNNNNN.txt` in the capture folder: the level-renderer bucket tag; per vertex its retail screen position, drawn position and precise depth; then `@name`, the draw stage (`level`, `instances`, `particles`, `tires`, `shadows`, `skids`, `hud`...) or the instance that drew it (`MainFrame_RenderFrame`, `RenderBucket_Execute`). A textured polygon is followed by a `T` line: texture page, CLUT and each vertex's UV, to look texels up in a `CTR_DEV_VRAM_DUMP` of the same frame. Then a `C` line: each vertex's colour as the GPU blends it (128 leaves a texture as it is). `CTR_DEV_DUMP_EVERY=<k>` dumps every k frames after it too, e.g. along an autopilot race |
| `CTR_DEV_STATE_FILE=<path>` | The savestate `F5`, `F8`, `CTR_DEV_SAVE_STATE` and `CTR_DEV_LOAD_STATE` use instead of `debug/states/quick.ctrstates`, e.g. an `F7` capture's `state.ctrstates` |
| `CTR_DEV_LOAD_STATE=<n>` | At frame n, load `debug/states/quick.ctrstates` from the run folder (or `CTR_DEV_STATE_FILE`), as `F8` would. Use a frame after the boot (1300 with `-Track -1`); earlier, nothing loads. Needs the exact exe that saved it ([Determinism](#determinism-and-load-addresses)). Loading also restores the race switches' state, so `CTR_DEV_TRACE` and `CTR_DEV_DRIVER_DUMP` stop after the load; frame captures and `CTR_DEV_DUMP_FRAME` keep working |
| `CTR_DEV_SAVE_STATE=<n>` | At frame n, save `debug/states/quick.ctrstates`, as `F5` would |
| `CTR_DEV_NUDGE=pitch\|yaw\|roll\|x\|y\|z` | In frame `CTR_DEV_NUDGE_FRAME` (default `CTR_DEV_DUMP_FRAME`), move player 1's integer camera by `CTR_DEV_NUDGE_BY` units (default 1) on that axis, where the follow camera hands it to the precise camera, as the game's rounding does. [Rounding detector](#rounding-detector) |
| `CTR_DEV_CAPTURE=<n>` | At frame n, capture as `F7` would ([Capturing while playing](#capturing-a-frame-while-playing)) |
| `CTR_DEV_CAMERA=<x>,<y>,<z>,<rx>,<ry>,<rz>` | Hold player 1's camera at that integer position and rotation instead of following the kart: an `F7` capture's `where.txt` `camera` line. With `CTR_DEV_WARP` from the same file, any build shows the captured view (the kart at rest, so no skid marks or speed effects). The precise camera is off for it, so the sky's glow is placed the PGXP-off way |
| `CTR_DEV_WARP=<x>,<y>,<z>,<angle>` | Once a level loads (hubs too), hold player 1 at that position and heading, at rest, for two seconds of game time, then let go. In a race this happens before the lights at both frame rates, so 30 and 60 FPS runs start from rest at race time 0, and it clears checkpoint progress so the mask-grab check does not pull the kart back. Drop it a little above the road (trace `y` + 50). Works in any build, unlike a savestate. In a hub, add `CTR_DEV_PAD=4050:2` for the far camera once Aku Aku's hint ends, and capture from frame 4100 |
| `CTR_DEV_NO_LEVEL_DEPTH=1` | Draw the level in ordering-table order only, as the PS1 does, for A/B captures of the [level depth test](PGXP.md#level-depth-test) |
| `CTR_DEV_RETAIL_CULL=1` | Draw the level with its retail visibility lists and the sky the retail way, for A/B captures ([LEVEL-VISIBILITY.md](LEVEL-VISIBILITY.md)). A dev run also logs `[CTR Dev] primitive arena peak`, the most primitive-arena bytes one frame used, and `[CTR Dev] instance ordering table peak`, the most ordering-table bytes one frame used and how many instances found the table full and were not drawn |
| `CTR_PGXP_DEBUG=1..4` | PGXP tints, see [PGXP.md](PGXP.md#debugging) |
| `CTR_DEV_RACE=<LevelID>` | The title menu starts a one-player race on that track instead of the demo. Nobody drives unless the autopilot does |
| `CTR_DEV_CUSTOM=<pin>` | Package builds: the race is an Arcade race on that installed custom track package (`assets/tracks/packages/<pin>` in the run folder), loaded through the custom pages' host slot; `CTR_DEV_RACE` is not needed. A refused package is logged and the host slot's retail track runs. At the end of the run the log gets the custom level's render report ([TRACK-CONVERTER.md](TRACK-CONVERTER.md#checks)) |
| `CTR_DEV_CUSTOM_NEXT=<pin>` | With `CTR_DEV_CUSTOM`: at `CTR_DEV_CUSTOM_NEXT_AT` frames of the first course (default 600), race that installed package next in the same slot, as racing a second course from the menu does, to see what one course leaves the next. `CTR_DEV_WARP` and `CTR_DEV_CAMERA` apply to both |
| `CTR_DEV_COLL_PROBE=<csv>` | Once a race's level is loaded, run the kart's own ground search (`COLL_FIXED_PlayerSearch`'s span, flags and BSP walk) and its moving sphere sweep straight down at every node of the three AI lanes, and every 64 units across each lane up to 1024 to each side; one row per probe. A miss where the track has road is a hole, found without driving. `CTR_DEV_COLL_PROBE_IN=<file>` probes the `x y z` points listed there instead (LEV units, where the ground should be), as `tools/tracks/check_track.ps1` does |
| `CTR_DEV_MODE=tt` | Make `CTR_DEV_RACE` a Time Trial (default Arcade). `CTR_DEV_LAPS=n` sets the Arcade lap count |
| `CTR_DEV_MODE=adv` | Make `CTR_DEV_RACE` an Adventure trophy race from a new profile. A win goes through the podium into the track's hub, where nobody drives; the autopilot taps Cross after the race and on the podium |
| `CTR_DEV_RACES=<LevelIDs>` | With `CTR_DEV_MODE=adv`, Adventure races back to back instead of `CTR_DEV_RACE`: the first from the title menu, each next one from the hub the last returns to, after its podium and Aku Aku's hints (the autopilot taps through them). A trailing `r` makes it the relic race. `8,3,6,9` with `CTR_DEV_BOT_SPEED=25` wins all four N. Sanity Beach trophies, so the last podium leads into Ripper Roo's boss-door scene. A lost race is raced again, so give `-Frames` room |
| `CTR_DEV_MODE=cup` | Run Adventure Gem Cups from a new profile, back to back (`CTR_DEV_RACE` not needed). `CTR_DEV_CUPS=0,4` lists them in order (0 Red, 1 Green, 2 Blue, 3 Yellow, 4 Purple; default 4). Each podium lands in Gemstone Valley; after it and any hint, the next cup starts as its warp pad would. With the autopilot on, Cross is tapped through results, podium and hints. `CTR_DEV_LAPS=1` shortens every leg. Two cups take about a minute |
| `CTR_DEV_PROFILE=<0-3>` | Play Adventure from that profile of the run's save (`-Save`), as loading it from the title does: its rewards, racer and name, and the save's game-wide unlocks. With no `CTR_DEV_RACE` or `CTR_DEV_RACES` the run starts in the profile's saved hub; otherwise those races run from it. `CTR_DEV_CHARACTER` overrides its racer. In the Archipelago build a connected client still applies the slot's items |
| `CTR_DEV_PODIUM=<csv>` | With `CTR_DEV_MODE=cup`, append a row per finished cup: cup, podium reward model, the racers on the three steps (`include/namespace_Vehicle.h`, `-1` for empty), then the final order (`cupPositionPerPlayer`, eight driver indices) |
| `CTR_DEV_BOT_SPEED=<percent>` | Cap the bots' top speed at that share of the original (default 100), so the autopilot wins. 50 wins Crash Cove |
| `CTR_DEV_CHARACTER=<id>` | Player 1's racer (`include/namespace_Vehicle.h`: 9 Papu Papu, 11 Komodo Joe, 15 Oxide...). The bot that had it takes Crash |
| `CTR_DEV_RACER=<n>` | Player 1 races as the nth installed custom racer (library order, by name), on its template's character ID. Package builds ([CUSTOM_RACERS.md](CUSTOM_RACERS.md)) |
| `CTR_DEV_AUDIO=<file>` | Mixed game audio as raw 16-bit stereo PCM at 44100 Hz, fast-forwarded or real time. In a real-time run add `-Config @{mute_when_unfocused='true'}` to keep it silent; the dump is taken before that gain |
| `CTR_DEV_SPU_AUDIT=<csv>` | Every row of the SPU audit, which always runs (`platform/native_audio.c`): each 16-byte block of SPU RAM remembers the upload that last wrote it, and a voice that decodes anything but its own sample gets a `V` row: keyed on inside a sample (`midsample`), reading a block never uploaded (`unwritten`) or uploaded after its key-on, a bank load under a playing sound (`overwritten`). These show even when the noise is too quiet or short for `audio_noise.py`. Each bank upload is a `W,vblank,offset,size` row; `howl_banks.py` names the bank by its size |
| `CTR_DEV_PLAYERS=2..4` | Make `CTR_DEV_RACE` a VS race for that many players, and connect that many pads, so the title's VS player rows unlock. The autopilot drives every player, each on its own AI lane, so on forked tracks some take the other branch |
| `CTR_DEV_CHEATS=<bits>` | Turn on `gameMode2` cheats for `CTR_DEV_RACE`, as the title-screen codes would (`CHEAT_*` in `include/namespace_Main.h`, decimal). `1048576` makes every turbo pad a super turbo pad: on Turbo Track the autopilot reaches Ultimate Speed Fire on the first pad |
| `CTR_DEV_HUB_PICKER=1` | AP build with `dev_keys=1` in the run folder's `ap-config.txt`: request the hub picker as soon as it is safe, without keyboard input |
| `CTR_DEV_GARAGE=1` | Boot into the Adventure garage for stat-bar captures |
| `CTR_DEV_SCRAPBOOK=1` | Boot directly into Scrapbook; skip with `CTR_DEV_PAD`, or run 6500 frames to cover natural EOF and the return to the title |
| `CTR_DEV_MASK=1` | With `CTR_DEV_RACE`: activate player 1's mask at 2 and 8 seconds, and player 2's at 5 seconds in multiplayer (player 1 again in 1P). Log song-bit changes against race time. Use `CTR_DEV_AUDIO` for comparison |
| `CTR_DEV_ENDING=1` | Boot into Oxide's true ending and the credits, with every sapphire relic, so the credits end in the scrapbook. `=2` without the relics, so they end in the hub |
| `CTR_DEV_AUTOPILOT=1` | In a race, player 1's pad holds the throttle and steers along the AI's nav line (aiming about half a second ahead), through the normal physics. When wedged (under 300 units in 1.5 s of race time) it reverses for a second at full opposite lock. `=2` stands still on the grid. `=3`, for checking converted tracks, also brakes for corners, and when wedged twice in one place hops 6 nodes on along the line, so a lap shows every place a kart gets stuck (a jump in `CTR_DEV_TRACE`). `CTR_DEV_AUTOPILOT_LANE=1` or `2` follows another of the three AI lanes (on converted tracks, 40% of the route's width to each side), so a run covers more of the road |
| `CTR_DEV_HOP_MS=<n>` | The autopilot hops every n ms of race time |
| `CTR_DEV_TRACE=<csv>` | One row per frame of player 1 in a race or a one-player Adventure hub: race time, position, angle, speed, velocity, airborne, kart state, lap, reserves, fire speed cap (`fireCap`, 0x4800 at Ultimate Speed Fire), animation, the human ghost's position, the autopilot's target; then the camera's position and angles (`cx`..`crz`), the kart's `turnAngleCurr` and `rotCurr.w`, `cameraMoveSpeed`, the [precise camera](PGXP.md#precise-camera) (`pcx`..`pcrz`, the integer one when there is none), and last the kart's position with its fractional bits (`kx`..`kz`) |
| `CTR_DEV_GHOST_OUT=<file>` | Write the Time Trial ghost recorded in the race (`0x3e00` bytes, the memory card ghost layout) |
| `CTR_DEV_GHOST_IN=<file>` | Race a `CTR_DEV_GHOST_OUT` file in the Time Trial. Without it, a 200cc Time Trial (`-Config @{'State.speed_class'=1}`) races its saved ghost from the run folder's `records/`, as the track select would |
| `CTR_DEV_EXIT_AFTER_RACE=1` | Quit a second after the finish |
| `CTR_DEV_RESULTS=<csv>` | Every racer's rank, lap, distance to the finish and track length at `CTR_DEV_RESULTS_MS` of race time (default 30000, from the lights). Distance driven is `lap × length + length − distance`. Works in the attract demo too, to compare the AI |
| `CTR_DEV_TRACK_CATALOGUE=<folder>` | Package builds: read the Decrypted track catalogue from that local copy, and Saphi's list from its `saphi.json` (one `{"data": [...]}` of Saphi rows; none means an empty list). No network, so a catalogue change can be installed and raced before it is published |
| `CTR_DEV_MODS=<tab>` | Package builds: the title opens the Mods screen on that tab (`1` Tracks, `2` Racers, `3` Installed), once |
| `CTR_DEV_CHARACTER_SELECT=1` | The title menu opens the Arcade character select for `CTR_DEV_PLAYERS` players, with idle virtual pads connected for menu captures. `CTR_DEV_CHARACTER` puts player 1's cursor on that racer. `CTR_DEV_UNLOCKS=1` unlocks the retail secret racers in the run's save, `=2` N. Oxide too. With `CTR_DEV_MODE=tt` it is the Time Trial select, and `CTR_DEV_PAD` can carry on through track and ghost selects into the race |
| `CTR_DEV_FAKE_RACERS=<n>` | n placeholder custom racers on the character select instead of the installed ones, using the first eight retail racers' looks, to fill its pages ([CUSTOM_RACERS.md](CUSTOM_RACERS.md)) |
| `CTR_DEV_SETUP=1` | Archipelago build: the title opens the first-launch setup (`game/230/MM_Setup.c`) even when it already ran or a room is saved. `devrun.ps1` writes `setup_done = true` into every run's `config.ini`, so without this switch it never opens |
| `CTR_DEV_UPDATE=<vX.Y.Z>` | Archipelago build: the in-game updater checks as that version and answers its own prompts, so `v1.1.0` downloads the latest release, verifies it against the release key, installs it into the run folder (leaving `.old` copies) and quits. The latest version logs `no update`. Steps are in the run folder's `ctr-ap.log` (`[AP UPDATE]`). [Releases and updates](DEVELOPMENT.md#releases-and-updates) |
| `CTR_DEV_PAD=<frame>:<keys>[:<frames>],...` | Player 1 holds those buttons for two frames (or the given count) from each listed frame: `U D L R`, `X` Cross, `O` Circle, `S` Square, `T` Triangle, `Z` Start, `2` L2 (far camera in a hub), `H` R1 (hop, drift), `J` L1. Example: `500:D,560:L,620:X`; `4100:X:400` holds the throttle for 400 frames. Between taps the autopilot, when on, drives. `CTR_DEV_RACE=26` with `CTR_DEV_MODE=adv` boots a new adventure in N. Sanity Beach; Aku Aku's hint ends near frame 4000, so `4050:2,4100:X:400` drives forward from the spawn with the far camera |
| `CTR_DEV_DRIVE=<ms>:<keys>[:<ms>],...` | The same script timed in ms of race time from the lights (default hold 100 ms), so 30 and 60 FPS runs get the same input at the same moment. `0:X:20000,2000:L:600,4000:XHR:900` holds the throttle, steers left for 0.6 s, then drifts right. Without the autopilot it owns player 1's pad for the whole race |
| `CTR_DEV_DRIVER_DUMP=<file>` | Every race frame, the frame number and all eight `Driver` structs, raw. `tools/decrypted/driver_diff.py` finds the first frame and field where two runs differ |
| `CTR_DEV_STACK_POISON=<byte>` | At the top of every main-loop frame, fill the stack the frame will use with that byte. Two runs with different bytes play the same unless code reads a local it never wrote |
| `CTR_DEV_NO_ITEMS=1` | Weapon crates give nothing and bots get no weapons, so two runs differ only by driving |

Frame numbers count every presented frame, boot splash included. Frames shown straight from VRAM (the boot splash) are captured from VRAM. Scrapbook draws its streaming texture into the main target, so normal and presented captures include its picture and black borders.

`devrun.ps1` clears earlier captures from the output folder, and warns when the game exits with an error code before `-Frames` (a crash). Every build run with `CTR_DEV_FAST` writes `crash.txt` in the run folder when it faults or aborts (`platform/native_crash_log.c`): the exception and every stack word that points into the exe's code, a rough stack. `addr2line -f -p -e <the exe> <addresses>` names them. The Archipelago build's own crash handler also writes `ctr-ap-crash.txt` with offsets, named by `addr2line -f -e build-ap\ctr_native_ap.exe <0x400000 + offset>`. Release builds inline heavily, so check neighbouring symbols with `nm -n` too.

## Capturing a frame while playing

For bugs that only show where a player drives: press `F7` (every build has it). Each press makes its own folder, `debug/captures/<date>-<time>` next to the game:
- `state.ctrstates`: a savestate of that moment. Replay it with `CTR_DEV_STATE_FILE=<it>` and `CTR_DEV_LOAD_STATE=1300` on the exact exe that made it (keep a copy); the next frame is pixel-identical. A state saved on a custom track also needs `CTR_DEV_CUSTOM=<pin>` with that same package in the run folder, and a later frame (`CTR_DEV_LOAD_STATE=3000`), because the state is refused until the course has loaded. `where.txt` gives no pin: the log's refusal names the state's LEV hash, the first 12 digits of the `lev` sha256 in a package's `manifest.json`. A rebuilt exe crashes loading it, so to test a change there, use `where.txt`: its `CTR_DEV_WARP` line and its `camera` line as `CTR_DEV_CAMERA` give the captured view in any build.
- `where.txt`: the level, player 1's position and heading as a ready `CTR_DEV_WARP=` line, the camera's position and rotation, and the view settings (`camera_far`, aspect, draw distance, render scale, PGXP).
- `kart.csv`: player 1's last ten seconds, frame by frame: race time, position, speed, velocity, reserves, airborne, kart state, angle, collision flags. For what a kart just did, a launch or a fall, gone by the time the key is pressed.
- `polys-NNNNNN.txt`, `fNNNNNN.bmp`, `vramNNNNNN.bin`: the next frame's polygon dump (`CTR_DEV_DUMP_FRAME` format), image and VRAM. `pgxp_cracks.py`, `vram_png.py` and a short script over the dump show which polygons drew it.
- For sound bugs (press right after the sound): the last 20 seconds of mixed audio (`audio-NNNNNN.raw`, `CTR_DEV_AUDIO` format, read by `audio_noise.py`) and the SPU audit's last 128 rows (`spu-NNNNNN.csv`, then `N,vblank` with the VBlank of the press).

To compare a spot with the original game, set retail's `Driver.posCurr` (+0x2d4, world × 256) and `angle` (+0x39a) from `where.txt` in an emulator.

## Rounding detector

The game's camera is in whole units, and its rounding steps every few frames while it moves. Under the [precise camera](PGXP.md#precise-camera) nothing drawn should follow those steps; anything that does jumps up to a pixel against the scene (wobble, judder). The detector finds those draw paths in any frame:

```powershell
# The Adventure hub at frame 4000, then a race while the autopilot drives:
tools\decrypted\nudge.ps1 -Frame 4000 -Exe build-mrg\ctr_native_ap.exe -Env @{CTR_DEV_RACE=26; CTR_DEV_MODE='adv'}
tools\decrypted\nudge.ps1 -Frame 2600 -Env @{CTR_DEV_RACE=0; CTR_DEV_AUTOPILOT=1}
```

It runs the frame once as is and once per camera axis with `CTR_DEV_NUDGE`, in parallel, and `nudge_diff.py` compares each polygon dump with the base one, matching polygons by who drew them, texture and nearest position. Polygons whose retail position moved but drawn position did not are precise. Those whose drawn position moved too follow the integer camera, and are listed by `@name` with how far they moved and where. `--png` paints them red on the frame. The frame must show the follow camera (a race, or a hub after the intro).

Reading it: precise polygons move at most a few hundredths of a pixel. A whole group following is a draw path that copies GTE results into primitives without the precise positions (`NativePgxp_StoreGteSXY`, `NativePgxp_CopyXY`), takes positions relative to the integer camera outside a `NativePgxp_ViewBeginRelative` scope, or is an instance whose model-view is not registered (split and screen-space models are not); see [PGXP.md](PGXP.md#precise-camera). Two kinds of entry are noise, not rounding: one or two polygons of a precise model whose drawn move is about its retail move (look-alike triangles mismatched; their drawn positions barely move in the dump), and depth-less vertices at x 1023 (a skid mark corner behind the camera, clamped by the GTE as in retail). The two example frames above show only noise. After a fix, the group must leave the list and `find_holes.py` totals must not rise.

## Analysis scripts

- `tools/decrypted/find_holes.py <dir> [--ignore-top N] [--max-run N]` finds see-through holes in a `CTR_PGXP_DEBUG=1` capture, where the sky draws pure blue: a short vertical run of blue with geometry above and below is a hole. Prints a count per frame and a total, and writes `holes-*.png` with holes in magenta. Use `--ignore-top 110 --max-run 4` at 2x render scale to skip HUD text. HUD text, boost flames and gaps between distant rocks also count, so compare against a baseline run (same settings with `CTR_PGXP_DEBUG=3`).
- `tools/decrypted/audio_noise.py <audio.raw> [--flat F] [--wav out.wav]` finds white noise in a `CTR_DEV_AUDIO` dump or `F7` audio: loud, spectrally flat windows (white noise scores about 0.58, game audio under 0.2). Prints noisy spans with their VBlank and can write a .wav.
- `tools/decrypted/howl_banks.py <disc.bin> [--bank N ...] [--samples]` lists the sound banks in KART.HWL with the SPU bytes each uploads, to name a `W` row of the SPU audit and check that a set of banks fits under the SPU ceiling (`include/platform/native_spu_memory.h`). The largest hub set (common, hub, player's, three podium banks) ends at 0x75c00, under the retail 0x7e000.
- `tools/decrypted/filter_check.py <baseline.png> <capture.png>... [--lines 216]` compares `CTR_DEV_SHOT_PRESENTED` captures of one frame against a baseline (normally SMOOTH): mean brightness, 99th percentile of the step between neighbouring pixels (anti-aliasing lowers it), and the share of vertical variation at the 216-line frequency (scanlines raise it). See [SCREEN-FILTERS.md](SCREEN-FILTERS.md#checking).
- `tools/decrypted/quad_dump.py <dump.csv> [--level N]` lists a level's turbo pad triggers and the visible quads over each, with their texture slots (`*10` marks a 10-frame animation).
- `tools/decrypted/nudge.ps1` and `nudge_diff.py <base polys> <nudged polys> [--png out.png] [--all]`: the [rounding detector](#rounding-detector).
- `tools/decrypted/pgxp_cracks.py <polys-NNNNNN.txt> [--level-only]` reads a polygon dump. It reports level vertices drawn at more than one position (a shared corner that disagrees with itself), then T-junction candidates: vertices inside another polygon's edge in retail but not when drawn.
- `tools/decrypted/driver_motion.py <drivers.bin> <trace.csv> [--ms 3000] [--drivers 1-7]` splits every racer's movement in a `CTR_DEV_DRIVER_DUMP` into along and across its heading, from the green light (taken from the same run's trace): forward speed reached, sideways speed (max and mean) and net sideways distance. Compare 30 and 60 FPS runs for racers sliding at the start.
- `tools/decrypted/driver_diff.py <A.bin> <B.bin> [--delta N]` compares two `CTR_DEV_DRIVER_DUMP` files and prints the first race frame where any driver differs: driver, offset, both values. Words that look like host pointers in both runs are skipped, or, with `--delta`, words that differ by exactly the load-base difference.

## Determinism and load addresses

Runs are bit-exact only while nothing depends on where the executable is loaded. Level data, statics and function pointers all sit at the load address, so code that reads past the end of level data, or tests a pointer's value as retail tests for 0x80xxxxxx, behaves differently between builds, and between runs if the base moves.

The game is linked without ASLR (`--disable-dynamicbase` on Windows, `-no-pie` on Linux, in `CMakeLists.txt`), so every run of one build loads at the same address. Savestates and input replays store raw pointers and depend on this. A state loads only in the exact build that made it, since any code change moves addresses.

To check for address dependence, build at two fixed bases and compare races with `CTR_DEV_DRIVER_DUMP` and `driver_diff.py --delta`:

```sh
cmake -S . -B /c/t/fb400000 -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-dynamicbase -Wl,--image-base=0x400000"
cmake -S . -B /c/t/fb10000000 -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-dynamicbase -Wl,--image-base=0x10000000"
```

All 18 race tracks' attract demos match across the two bases for 4,500 frames of all eight drivers, so any difference is a regression.

## Unattended races

The race switches live in `platform/native_dev_race.c`. They check gameplay timing with nobody driving, mainly 60 FPS against 30:

```powershell
# Every track, a Time Trial at 30 and at 60 FPS, plus the 30 FPS ghost raced at both:
tools\decrypted\fps-parity.ps1 -Ghosts
# One track:
tools\decrypted\fps-parity.ps1 -Tracks 3
```

`tools/decrypted/race_trace.py` reads the traces:

- `laps A.csv B.csv`: lap splits.
- `jumps A.csv`: airborne spells, airtime and apex height.
- `parity A.csv B.csv`: finish times, and jumps launched from the same spot in both runs.
- `fps A.csv B.csv [--until MS]`: two runs of the same `CTR_DEV_DRIVE` script, normally 30 and 60 FPS. Per kart and camera field, the RMS difference as a share of the field's spread, and the race time it first strays by a tenth of that. `tools/decrypted/fps-drive.ps1 -Track 3 -Drive '0:X:5000,1500:L:1500' -Until 4000` runs both and prints this; `-Env @{CTR_DEV_WARP='5838,2374,7579,2683'}` adds switches to both (here, Dingo Canyon's run-up to the canyon jump).
- `smooth A.csv ...`: how unevenly player 1's camera moves: per channel, the median and 90th percentile of each frame's distance from the midpoint of its neighbours (zero at a steady rate), and `alt`, the share that flips sign every frame (something stepped on odd frames only). Integer and precise camera channels sit side by side.
- `ghost SOURCE.csv PLAY.csv`: how far the ghost in PLAY strays from the SOURCE player who recorded it.

A Time Trial takes 10 to 20 seconds at 30 FPS, twice that at 60. The autopilot never boosts, hops or drifts, so on N. Gin Labs and Polar Pass it keeps falling into the same pits and the run ends at the frame cap. Per-track expectations are in [60FPS.md](60FPS.md#checking). Input replays (`--record`, `--replay`, [REPLAYS.md](REPLAYS.md)) work with these switches; set the same `CTR_DEV_RACE` and `CTR_DEV_MODE` when playing back, because they change the title menu's flow.

## Saves and progress

`tools/decrypted/ctr_save.py` reads and edits the save, `memcards\slot0\BASCUS-94426-SLOTS` next to the exe: the four Adventure profiles and game-wide progress (`struct MemcardProfile`, `include/namespace_Memcard.h`). It rewrites the checksum so the game accepts the file. `python tools\decrypted\ctr_save.py -h` lists every name.

```powershell
$s = 'tools\decrypted\ctr_save.py'
python $s show <save>                    # what each profile has; --json for scripts
python $s new C:\t\sv\test.sav           # an empty card
# Profile 0 as Coco, saved in Glacier Park: 8 trophies, 2 keys, one relic, the doors
# to Glacier Park open, every hint already seen (no Aku Aku popups in the run).
python $s edit C:\t\sv\test.sav --character coco --hub glacier --give trophy:0,1,2,3,4,5,6,7 key:2 sapphire:blizzard door:beach_gemstone,beach_glacier,ruins_glacier hint
python $s edit C:\t\sv\test.sav --profile 1 --give everything racer cup ghost   # 100% and every unlock
tools\decrypted\devrun.ps1 -Track -1 -Frames 2600 -From 2400 -Save C:\t\sv\test.sav -Env @{CTR_DEV_PROFILE=0}
```

`--give` and `--take` take `KIND[:WHICH]`. Per profile: trophies, relics per tier, tokens, keys, gems, purple tokens, hub doors, the two Oxide endings and hints. Per save: racers, bonus tracks and arenas, Arcade cups, the scrapbook and beaten Time Trial ghosts. An edited empty profile becomes `DEV`, Crash, in N. Sanity Beach. A door stands open as if opened with keys (`game/232/AH_Door.c`); warp pads and boss garages still count trophies, relics and keys. In the Archipelago build doors follow the slot's door history instead.

Archipelago progress lives on the server, so the same script talks to a local server (127.0.0.1, any room; `ap-give` needs the item cheat, on by default in `host.yaml`):

```powershell
python $s ap-show 38300 Decrypt                         # items received, locations checked
python $s ap-give 38300 Decrypt "Sapphire Relic" 6      # the server's !getitem
python $s ap-check 38300 Decrypt "Crash Cove: *" "*: Trophy Race"   # names or wildcards
```

`ap-check` connects as a game client of the slot (the server ignores checks from text-only clients) and checks the matching locations, so their items go out as if found in game. Names are in `apworld/ctr/data/locations.json` and `items.json`. A killed server does not write its `.apsave`; stop it from its console (`/exit`) to keep what was given.

## README screenshots

`tools/decrypted/readme-shots.ps1` retakes the three README images in `docs/images` (1920x1080, 16:9, PGXP, increased draw distance): N. Oxide on Hot Air Skyway, the Archipelago item feed in a Crash Cove race, and the Mods screen's track browser. Retake them after a visible change to any of those; `-Only oxide` (or `archipelago`, `mods`) does one.

- The Oxide shot is fast-forwarded, so its frame is always the same.
- The Archipelago shot generates a throwaway two-player seed (Decrypt and Friend, item boxes as checks) with this repository's apworld, in the Archipelago checkout `apworld-test.ps1` sets up (run that once first). It hosts the seed on 127.0.0.1:38300 and races in real time while `tools/decrypted/ap_feed_demo.py` connects as Friend and checks Friend's locations holding Decrypt's items, so the feed shows `<ITEM> FROM FRIEND`.
- The Mods shot needs the network for the Project Saphi catalogue.

Real-time shots can land a little differently each run. Every captured frame stays under `C:\t\shots\<name>`; pick another with `-ApFrame` or `-ModsFrame`.

## Archipelago server load

`tools/decrypted/ap_backlog.py PORT SLOT COUNT [ITEM]` gives a slot a large item backlog on a local server through the item cheat (`!getitem`, allowed by default in `host.yaml`), so the next connect receives it all in one packet. To measure a connect:

```powershell
# A throwaway seed: a copy of a player YAML with its own name, in its own folder.
C:\ProgramData\Archipelago\ArchipelagoGenerate.exe --player_files_path C:\t\ap\players --outputpath C:\t\ap\out
# Host it on this machine only, then queue the items:
C:\ProgramData\Archipelago\ArchipelagoServer.exe --host 127.0.0.1 --port 38300 C:\t\ap\out\AP_<seed>.zip
python tools\decrypted\ap_backlog.py 38300 Bench 20000
tools\decrypted\devrun.ps1 -Exe build-ap\ctr_native_ap.exe -RealTime -Track -1 -Frames 2400 -From 100000000 -Config @{ mute_when_unfocused='true'; 'Connection.uri'='ws://localhost:38300'; 'Connection.slot'='Bench' } -GameArgs '--perf-dir','C:\t\ap\perf'
```

The client logs every frame over 250 ms as `[AP PERF] frame stall` in the run folder's log, and `frame_times.csv` has every frame.

The same server drives connected Adventure runs (`CTR_DEV_MODE=adv`, `CTR_DEV_RACES`) with the same `-Config` connection keys. An already-checked location changes the flow (a won trophy skips its podium), so for a first win stop the server, delete the `.apsave` next to the seed zip, restart it, and use a new `-RunDir` (the client keeps `ap-state.json` there).

## Frame timing

`ctr_native.exe --perf` writes `debug/perf/perf-latest/frame_times.csv` and `summary.txt`. A frame's budget is the VBlanks it waits for times 16.68 ms (33.37 ms at 30 FPS, 16.68 at 60); the summary counts frames over budget, over 1.5 times it (on screen a refresh too long) and over twice it. In a hidden window the swap does not wait for the display, so measure VSync On and Adaptive with the window visible.

`tools/decrypted/vsync-check.ps1` does that for every VSync option at 30 and 60 FPS, windowed and fullscreen: it plays the attract demo in a visible window in real time with `--perf-dir`, reads the display's refresh rate from Windows, and runs `tools/decrypted/perf_pacing.py` on each result. Per run it prints frame interval percentiles, how many refreshes each frame stayed on screen, the uneven frames (an unusual refresh count) and the long ones (over 1.5 times nominal). It takes about 25 minutes, and the screen must stay on and unlocked, so run it when nobody is using the machine.
