# Roadmap

What is being worked on, what is next, and what was decided. Kept current with every change: a finished item is deleted (git history has it), and features are described in [docs/](docs/).

- How the project is set up and how to work on it: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream projects' issue trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game, its Archipelago client and its Archipelago world. The world lives in `apworld/ctr`, imported with its history from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld) (`worlds/ctr`), so the client and world are versioned together.
- **Base: dowlle/ctr-native-ap** `main` (v0.2.4) plus its unreleased 0.3.0 line (`dev/custom-pad-0.3.0`, the signed updater, Play Solo and the xdelta importer). Seeds made with `ctr.apworld` 0.2.4 still work.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files (351 hunks), and High Octane carries Vita, web and leaderboard code we don't want.

## In progress
Nothing.

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| **Medium priority.** A Mods menu on the main screen, next to Adventure and Time Trial, so players never touch files or folders. Tabs for Tracks, Characters (`.ctrr`) and Karts, each entry with a picture, name and author; one-click install from an online catalogue (Project Saphi for tracks), drag and drop of a downloaded file or disc patch onto the window, an on/off switch and Remove per mod. Installed mods show up in the normal track and character selectors, and a seed that needs missing content offers to download it | Owner decision, 2026-10-31. Builds on 0.3.0's package verifier, xdelta importer and Install missing content |
| Upstream status check (mid): one command that shows, for dowlle (`main`, `dev/custom-pad-0.3.0`, its feature branches and the apworld's `worlds/ctr`), ctr-native and High Octane, the last commit we integrated and what is new since, so we always know how far behind we are. The High Octane features already ported get listed the same way | Owner request, 2026-10-31. Today `tools/decrypted/sync-upstream.ps1` imports a branch and [UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md) snapshots the trackers, but nothing tracks what is integrated |
| Easy download and update (mid): one download for players and an in-game "update available, install?" with one click. Start from 0.3.0's signed updater (`ap/ap_update.c`) and point it at our releases | Owner request, 2026-10-31 |
| Ship our own apworld (high, before the next release): `release-prepare.yml` and [RELEASING.md](RELEASING.md) still take `ctr.apworld` from a run of dowlle's repository; pack it from `apworld/ctr` instead (`apworld/ctr/tools/build_apworld.py`), and run the world's pytest suite and fuzz in our CI (dowlle's `ctr-world.yml` is the recipe). Until then the world's tests have only been run upstream; our `starting_roster` change was checked by generating seeds | 0.3.0 port, 2026-11-01 |
| First-launch setup (mid): a guided screen the first time the game starts: find the player's disc image, set up Archipelago (server, slot name, password) and a few basic options, so nobody edits `config.ini` | Owner request, 2026-10-31 |
| Save backups (mid): keep the last few copies of the save and Archipelago progress automatically, with a restore option in the menu | Owner request, 2026-10-31 |
| Linux and Steam Deck (mid): publish a Linux build and have it tested on a Steam Deck (the owner has a tester), including controller and default resolution | Owner request, 2026-10-31. Only Windows is tested today |
| Take the rest off the game's rounded camera: kart tires, shadows, skid marks, particles and the hub's warp-pad numbers still jump with its rounding (the [rounding detector](docs/DEV-TOOLS.md#rounding-detector) lists them in every race and hub), the same judder the sky and ground had | Detector run, 2026-11-01 |
| Save-file tool (dev): read and edit the game save (and the Archipelago progress next to it) from a script: unlocked racers and tracks, trophies, relics, keys, gems, tokens, Adventure position, so a test or a bug repro can start from any point of the game. Today only `CTR_DEV_UNLOCKS` (secret racers) and new-profile runs exist ([DEV-TOOLS.md](docs/DEV-TOOLS.md)) | Owner request, 2026-11-01 |
| Project website and guide (mid): GitHub Pages site with install steps, how to play with Archipelago, and the dev toolbox | Owner request, 2026-10-31 |

## Maybe
- Retail-exact physics at 60 FPS: run the simulation at a fixed 30 Hz and interpolate rendering. Today's 60 FPS integrates in 16 ms steps, so curved kickers launch a little higher (Dingo Canyon: apex rise 24 at 30 FPS, 31 to 33 at 60). A different design from High Octane's, touching every rendered position.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play.
- High Octane's ghost replay tools.
- Asset converters from other games (low priority): tracks, models and textures, for example from Mario Kart DS or from PS1 games with similar formats. Depends on custom tracks and racers.
- Better bots (low priority): smarter driving, item use, racing lines and shortcuts, with no speed or other stat advantage.
- PAL disc support (low priority). Upstream removed it on purpose (ctr-native #51, #54), so it would be our own work.
- A 4:3 / 5:4 aspect option for CRTs (High Octane #44).
- High Octane's colour effects as more Screen Filter entries: Gamma, Greyscale, Hue, Negative, Sepia, Vignette, LCD 3x, colour-blind modes.
- macOS (low priority). First a clean 64-bit/arm64 build (today's builds are 32-bit x86 MinGW32), then a macOS platform layer: window, GL context, audio and controllers. Until then the Windows exe under CrossOver/Wine is the stopgap; it needs GL 3.x there (ctr-native #20, #33).
- Exact pointer tests in the level renderer: `226_00_DrawLevelOvr1P.c` tells a hi-res texture pointer from inline texture data by the word's value (inside the level buffer, word-aligned, plausible page). Checked against the loader's pointer map on Hot Air Skyway in both builds: no disagreement in 100,000 lookups, so it is left alone. The exact version needs a flag carried next to scratchpad word 0x84.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support. Upstream removed it on purpose (ctr-native #51).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The Archipelago client and our platform code use GCC extensions and options; Windows builds use MSYS2 MinGW32.
