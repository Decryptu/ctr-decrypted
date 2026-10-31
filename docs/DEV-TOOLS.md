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
| `-Config` | `config.ini` keys to override, e.g. `@{pgxp=0; render_scale=2}`. Keys outside `[Video & QoL]` are named `Section.key`, e.g. `@{"Connection.uri"="ws://localhost:38300"}` |
| `-Env` | Extra environment variables, e.g. `@{CTR_PGXP_DEBUG=1}` |
| `-Visible`, `-RealTime` | Show the window, or keep wall-clock pacing |
| `-Exe` | Executable to run (default `build-vanilla\ctr_native.exe`) |
| `-RunDir` | Run folder (default `%TEMP%\ctrdev\run`). Give runs that happen at the same time a folder each |
| `-GameArgs` | Command-line arguments for the game, e.g. `'--perf-dir','C:\t\perf'` |

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
| `CTR_DEV_SHOT_PRESENTED=1` | Capture what reaches the window instead: the presentation viewport after the screen filter, at window size. Presents then go through a window-sized target that is copied to the window, so a hidden window still captures. The Archipelago tracker overlay is drawn after it and is not in the capture |
| `CTR_DEV_EXIT_FRAME=<n>` | Quit after n presented frames |
| `CTR_DEV_QUAD_DUMP=<csv>` | Append every quadblock of each level as it loads: flags, terrain byte, bounding box, and each texture slot's pointer, palette word, page and animation frame count; a `#` line before each level gives the bytes left in the level's memory pack. With Cyan Super Pads on, the level is dumped again after the pads are marked (their palette words carry 0x8000). `tools/decrypted/quad_dump.py` reads it |
| `CTR_DEV_VRAM_DUMP=<n>` | Save all of VRAM at frame n as `vramNNNNNN.bin` (1024×512 raw 15-bit pixels) in the capture folder. `tools/decrypted/vram_png.py` turns it into a PNG, never-written words in magenta |
| `CTR_DEV_DUMP_FRAME=<n>` | Write every polygon drawn in frame n to `polys-NNNNNN.txt` in the capture folder: the level-renderer bucket tag, then for each vertex its retail screen position, its drawn position and its precise depth. A textured polygon is followed by a `T` line: texture page, CLUT, and each vertex's UV, to look the texels up in a `CTR_DEV_VRAM_DUMP` of the same frame |
| `CTR_DEV_LOAD_STATE=<n>` | At frame n, load `debug/states/quick.ctrstates` from the run folder, as `F8` would. Copy a player's `F5` save there to replay their exact view unattended, with the exact exe that saved it (see [Determinism](#determinism-and-load-addresses)). Loading also restores the race switches' own state, so `CTR_DEV_TRACE` and `CTR_DEV_DRIVER_DUMP` stop after the load; frame captures and `CTR_DEV_DUMP_FRAME` keep working |
| `CTR_DEV_SAVE_STATE=<n>` | At frame n, save `debug/states/quick.ctrstates`, as `F5` would |
| `CTR_DEV_WARP=<x>,<y>,<z>,<angle>` | Once a level has loaded (the adventure hubs too), hold player 1 at that world position and heading, at rest, for two seconds, then let go. Reaches a spot a player reported in any build, unlike a savestate. In the adventure hub, add `CTR_DEV_PAD=4050:2` for the far camera once Aku Aku's hint ends, and capture from frame 4100 |
| `CTR_DEV_NO_LEVEL_DEPTH=1` | Draw the level in ordering-table order only, as the PS1 does, for A/B captures of the [level depth test](PGXP.md#level-depth-test) |
| `CTR_PGXP_DEBUG=1..4` | PGXP tints, see [PGXP.md](PGXP.md#debugging) |
| `CTR_DEV_RACE=<LevelID>` | The title menu starts a one-player race on that track instead of the demo, with nobody at the controls unless the autopilot drives |
| `CTR_DEV_MODE=tt` | Make `CTR_DEV_RACE` a Time Trial (default Arcade). `CTR_DEV_LAPS=n` sets the Arcade lap count |
| `CTR_DEV_MODE=adv` | Make `CTR_DEV_RACE` an Adventure trophy race from a new profile. A win goes through the podium into the track's hub, where nobody drives; the autopilot taps Cross after the race and on the podium |
| `CTR_DEV_MODE=cup` | Run Adventure Gem Cups from a new profile, back to back, instead of one race (`CTR_DEV_RACE` is not needed). `CTR_DEV_CUPS=0,4` lists them in order (0 Red, 1 Green, 2 Blue, 3 Yellow, 4 Purple; default 4). Each cup's podium lands in Gemstone Valley; once it and any Aku Aku hint are over, the next cup starts as its warp pad would. With the autopilot on, Cross is tapped through the results, the podium and the hints. `CTR_DEV_LAPS=1` shortens every leg. Two cups take about a minute |
| `CTR_DEV_PODIUM=<csv>` | With `CTR_DEV_MODE=cup`, a row per finished cup, appended: cup, podium reward model, the racer on the first, second and third steps (`include/namespace_Vehicle.h`, `-1` for an empty step), then the cup's final order (`cupPositionPerPlayer`, eight driver indices) |
| `CTR_DEV_BOT_SPEED=<percent>` | Cap the bots' top speed at that share of the original (default 100), so the autopilot wins and reaches the podium. 50 wins Crash Cove |
| `CTR_DEV_CHARACTER=<id>` | Player 1's racer (`include/namespace_Vehicle.h`: 9 Papu Papu, 11 Komodo Joe, 15 Oxide...). The bot that had it takes Crash |
| `CTR_DEV_AUDIO=<file>` | The mixed game audio as raw 16-bit stereo PCM at 44100 Hz, fast-forwarded or real time. Add `-Config @{mute_when_unfocused='true'}` to a real-time run so it stays silent; the dump is taken before that gain |
| `CTR_DEV_PLAYERS=2..4` | Make `CTR_DEV_RACE` a VS race for that many players. With the autopilot on, it drives every player, each on its own AI lane, so on tracks with forks some take the other branch |
| `CTR_DEV_CHEATS=<bits>` | Turn on `gameMode2` cheats for `CTR_DEV_RACE`, as the title-screen codes would (`CHEAT_*` in `include/namespace_Main.h`, decimal). `1048576` makes every turbo pad a super turbo pad: on Turbo Track the autopilot reaches Ultimate Speed Fire on the first pad |
| `CTR_DEV_ENDING=1` | Boot into Oxide's true ending, which runs into the credits, with every sapphire relic, so the credits end in the scrapbook. `=2` without the relics, so they end in the hub |
| `CTR_DEV_AUTOPILOT=1` | During a race, player 1's pad holds the throttle and steers along the AI's nav line (aiming about half a second ahead), so the kart drives through the normal physics. When wedged (under 300 units travelled in 1.5 s of race time) it reverses for a second at full opposite lock. `=2` stands still on the grid instead |
| `CTR_DEV_HOP_MS=<n>` | The autopilot hops every n ms of race time |
| `CTR_DEV_TRACE=<csv>` | One row per frame of player 1 in a race: race time, position, angle, speed, velocity, airborne, kart state, lap, reserves, fire speed cap (`fireCap`, 0x4800 at Ultimate Speed Fire), animation, the human ghost's position, and the autopilot's target |
| `CTR_DEV_GHOST_OUT=<file>` | Write the Time Trial ghost recorded in the race (`0x3e00` bytes, the memory card ghost layout) |
| `CTR_DEV_GHOST_IN=<file>` | Race a ghost file from `CTR_DEV_GHOST_OUT` in the Time Trial |
| `CTR_DEV_EXIT_AFTER_RACE=1` | Quit a second after the finish |
| `CTR_DEV_RESULTS=<csv>` | Every racer's rank, lap, distance to the finish and the track length at `CTR_DEV_RESULTS_MS` of race time (default 30000, counted from the lights). Distance driven since the start line is `lap × length + length − distance`. Works in the attract demo too, to compare the AI |
| `CTR_DEV_CHARACTER_SELECT=1` | The title menu opens the Arcade character select for `CTR_DEV_PLAYERS` players. `CTR_DEV_CHARACTER` puts player 1's cursor on that racer. `CTR_DEV_UNLOCKS=1` unlocks the retail secret racers in the run's save, `=2` N. Oxide as well |
| `CTR_DEV_PAD=<frame>:<keys>[:<frames>],...` | Player 1 holds those buttons for two frames (or the given count) from each listed frame, to drive menus or the kart: `U D L R`, `X` Cross, `O` Circle, `S` Square, `T` Triangle, `Z` Start, `2` L2 (the far camera in a hub). Example: `500:D,560:L,620:X`; `4100:X:400` holds the throttle for 400 frames. Between taps the autopilot, when on, drives. `CTR_DEV_RACE=26` with `CTR_DEV_MODE=adv` boots a new adventure in N. Sanity Beach; Aku Aku's hint is timed and ends near frame 4000, so `4050:2,4100:X:400` drives forward from the spawn with the far camera |
| `CTR_DEV_DRIVER_DUMP=<file>` | Every frame of a race, the race frame number and all eight `Driver` structs, raw. `tools/decrypted/driver_diff.py` finds the first frame and field where two runs differ |
| `CTR_DEV_STACK_POISON=<byte>` | At the top of every main-loop frame, fill the stack the frame is about to use with that byte. Two runs with different bytes play the same unless code reads a local it never wrote |
| `CTR_DEV_NO_ITEMS=1` | Weapon crates give nothing and bots are handed no weapons, so two runs differ only by driving |

Frame numbers count every presented frame, the boot splash included. Frames shown straight from VRAM (the boot splash, the scrapbook and other movies) are captured from VRAM.

`devrun.ps1` clears earlier captures from the output folder, and warns when the game exits with an error code before `-Frames` (a crash). For a stack, run the same switches on the Archipelago build (`-Exe build-ap\ctr_native_ap.exe`): its crash handler writes `ctr-ap-crash.txt` in the run folder, and `addr2line -f -e build-ap\ctr_native_ap.exe <0x400000 + offset>` names each frame. Release builds inline heavily, so check neighbouring symbols with `nm -n` too.

## Capturing a frame while playing

Some bugs only show where a player drives. In an internal build, press `F7` while one is on screen: the next frame's polygon dump (the `CTR_DEV_DUMP_FRAME` format), its image (`fNNNNNN.bmp`) and all of VRAM (`vramNNNNNN.bin`) go to `debug/captures` next to the game. Press it a few times around the bug. `pgxp_cracks.py`, `vram_png.py` and a short script over the dump then show which polygons drew it.

When the bug needs a view the player held, have them press `F5` there too, keep a copy of that exact exe, and replay the save with `CTR_DEV_LOAD_STATE`. To compare a spot with the original game, read the player's position from the save (an Instance's world position is at +0x44, after its 16-byte name at +0x08), then set retail's `Driver.posCurr` (+0x2d4, world × 256) and `angle` (+0x39a) to it in an emulator, and pass it to `CTR_DEV_WARP` for any later build.

## Analysis scripts

- `tools/decrypted/find_holes.py <dir> [--ignore-top N] [--max-run N]` finds see-through holes in a capture made with `CTR_PGXP_DEBUG=1`. In that mode the sky backdrop draws pure blue, so a short vertical run of blue with geometry above and below it is a hole. It prints a count per frame and a total, and writes `holes-*.png` with the holes painted magenta. Use `--ignore-top 110 --max-run 4` at 2x render scale to skip the HUD text. HUD text, boost flames and gaps between distant rocks also register, so compare totals against a baseline run (the same settings with `CTR_PGXP_DEBUG=3`) rather than reading them alone.
- `tools/decrypted/audio_noise.py <audio.raw> [--flat F] [--wav out.wav]` finds white noise in a `CTR_DEV_AUDIO` dump: windows that are loud and spectrally flat (white noise scores about 0.58, game audio stays under 0.2). It prints the noisy spans with their VBlank number and can write a .wav to listen to.
- `tools/decrypted/filter_check.py <baseline.png> <capture.png>... [--lines 216]` compares `CTR_DEV_SHOT_PRESENTED` captures of one frame against a baseline (normally SMOOTH): mean brightness, the 99th percentile of the step between neighbouring pixels (anti-aliasing lowers it), and the share of vertical variation at the 216-line frequency (scanlines raise it). See [SCREEN-FILTERS.md](SCREEN-FILTERS.md#checking).
- `tools/decrypted/quad_dump.py <dump.csv> [--level N]` lists a level's turbo pad triggers and the visible quads over each one, with their texture slots (`*10` marks a 10-frame animation).
- `tools/decrypted/pgxp_cracks.py <polys-NNNNNN.txt> [--level-only]` reads a polygon dump. It reports level vertices drawn at more than one position (a shared corner that disagrees with itself), then T-junction candidates: vertices that lie inside another polygon's edge in retail but not when drawn.
- `tools/decrypted/driver_diff.py <A.bin> <B.bin> [--delta N]` compares two `CTR_DEV_DRIVER_DUMP` files and prints the first race frame where any driver differs, as driver, offset and both values. Pointer fields differ between runs that load at different addresses, so words that look like host pointers in both runs are skipped, or, with `--delta`, words that differ by exactly the load-base difference.

## Determinism and load addresses

Runs are bit-exact only while nothing in the game depends on where the executable is loaded. The level data, statics and function pointers all sit at the load address, so code that reads past the end of level data, or tests a pointer's value the way retail tests for 0x80xxxxxx, behaves differently between builds, and between runs when the base moves.

The game is linked without ASLR (`--disable-dynamicbase` on Windows, `-no-pie` on Linux, in `CMakeLists.txt`), so every run of one build loads at the same address. Savestates and input replays store raw pointers and depend on this; with ASLR they crash when loaded in another launch. A state loads only in the exact build that made it, since any code change moves the addresses.

To check for address dependence, build twice at two fixed bases, then compare races with `CTR_DEV_DRIVER_DUMP` and `driver_diff.py --delta`:

```sh
cmake -S . -B /c/t/fb400000 -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-dynamicbase -Wl,--image-base=0x400000"
cmake -S . -B /c/t/fb10000000 -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-dynamicbase -Wl,--image-base=0x10000000"
```

Each fixed-base build repeats itself exactly; any difference between the two is an address dependence. All 18 race tracks' attract demos match across the two bases for 4,500 frames of all eight drivers, so any difference is a regression.

## Unattended races

The race switches above live in `platform/native_dev_race.c`. They check gameplay timing without anyone driving, mainly 60 FPS against 30:

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
- `ghost SOURCE.csv PLAY.csv`: how far the ghost in PLAY strays from the player of SOURCE, who recorded it.

A Time Trial takes 10 to 20 seconds to run at 30 FPS, twice that at 60. The autopilot never boosts, hops or drifts, so it falls into the same pits again and again on N. Gin Labs and Polar Pass; those runs end at the frame cap. What to expect per track is in [60FPS.md](60FPS.md#checking). Input replays (`--record`, `--replay`, [REPLAYS.md](REPLAYS.md)) work with these switches too. Set the same `CTR_DEV_RACE` and `CTR_DEV_MODE` when playing back, because they change the title menu's flow.

## Archipelago server load

`tools/decrypted/ap_backlog.py PORT SLOT COUNT [ITEM]` gives a slot a large item backlog on a local server, through the server's item cheat (`!getitem`, allowed by default in `host.yaml`), so the next connect receives it all in one packet. To measure a connect:

```powershell
# A throwaway seed: a copy of a player YAML with its own name, in its own folder.
C:\ProgramData\Archipelago\ArchipelagoGenerate.exe --player_files_path C:\t\ap\players --outputpath C:\t\ap\out
# Host it on this machine only, then queue the items:
C:\ProgramData\Archipelago\ArchipelagoServer.exe --host 127.0.0.1 --port 38300 C:\t\ap\out\AP_<seed>.zip
python tools\decrypted\ap_backlog.py 38300 Bench 20000
tools\decrypted\devrun.ps1 -Exe build-ap\ctr_native_ap.exe -RealTime -Track -1 -Frames 2400 -From 100000000 -Config @{ mute_when_unfocused='true'; 'Connection.uri'='ws://localhost:38300'; 'Connection.slot'='Bench' } -GameArgs '--perf-dir','C:\t\ap\perf'
```

The client logs every frame over 250 ms as `[AP PERF] frame stall` in the run folder's log, and `frame_times.csv` has every frame.

## Frame timing

`ctr_native.exe --perf` writes `debug/perf/perf-latest/frame_times.csv` and `summary.txt`. A frame's budget is the VBlanks it waits for times 16.68 ms (33.37 ms at 30 FPS, 16.68 ms at 60), and the summary counts frames over the budget, over 1.5 times it (shown on screen a refresh too long), and over twice it. The swap does not wait for the display in a hidden window, so measure VSync On and Adaptive with the window visible.

`tools/decrypted/vsync-check.ps1` does that for every VSync option at 30 and 60 FPS, windowed and fullscreen: it plays the attract demo in a visible window in real time with `--perf-dir`, reads the display's refresh rate from Windows, and runs `tools/decrypted/perf_pacing.py` on each result. Per run it prints the frame interval percentiles, how many refreshes each frame stayed on screen, the uneven frames (a refresh count other than the usual one) and the long ones (over 1.5 times nominal). It takes about 25 minutes. The screen has to stay on and unlocked throughout, so run it when nobody is using the machine.