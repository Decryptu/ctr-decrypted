# Roadmap

What is in progress, what is next, and what was decided. A finished item is deleted (git history has it); features are described in [docs/](docs/).

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game, its Archipelago client and its world. The world lives in `apworld/ctr`, imported with its history from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld) (`worlds/ctr`), so client and world are versioned together.
- **Base: dowlle/ctr-native-ap** `main` (v0.2.4) plus its unreleased 0.3.0 line (`dev/custom-pad-0.3.0`, the signed updater, Play Solo, the xdelta importer). Seeds made with `ctr.apworld` 0.2.4 still work.
- **Custom racers are new slots, not replacements.** Each has its own icon, name, model and textures, and borrows a retail character ID (its template) only for systems keyed by that ID (voice, podium, records, Archipelago). Real IDs above 15 would touch about a hundred sites, save formats included (a memory card ghost packs the ID into 4 bits). Portraits go to a second VRAM bank, so the template keeps its own and can race in the same field (High Octane overwrites the template's textures, its #36).
- **Racers ship from [Decryptu/ctr-decrypted-racers](https://github.com/Decryptu/ctr-decrypted-racers)**, an index the Mods screen downloads from, like Saphi for tracks. Each racer is credited to its author. A package holds only the mod's own model and portrait, nothing from the player's disc; a racer that reuses a look from another release credits it.
- **The Mods screen has no Karts tab**: a racer's model is driver and kart in one mesh, and wheels are shared sprites (owner decision, 2026-10-31).
- **60 FPS simulates at 60 Hz** (owner decision, 2026-11-02). A step that runs once per frame instead of per unit of time is fixed where it happens ([60FPS.md](docs/60FPS.md)). No fixed 30 Hz simulation with interpolated rendering: it would bring back 33 ms input steps and touch every rendered position.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files, and it carries Vita, web and leaderboard code we don't want.

## In progress
- Mario Kart DS course converter ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)). Figure-8 Circuit converts and passes the checks; the owner plays it next. Then: the minimap (an image in the package's VRAM, icons 3 and 4 pointed at it, and an `ST1_MAP` entry with the world bounds), item boxes and the other course objects, then every course in one batch run with `check_track.ps1`.

## Planned
Nothing.

## Maybe
- 200cc beyond Arcade and VS ([SPEED-CLASS.md](docs/SPEED-CLASS.md)): Time Trial with records and ghosts kept per class (a save format change), and whether Archipelago can require or place it.
- Title screen: a drawn logo in place of the DECRYPTED text, and the release version in a corner. Owner idea, 2026-11-02.
- Chaos effects as Archipelago traps. CTR Native Expanded has 38 race effects, harmful and helpful (reversed steering, low gravity, giant karts, no items...), with an external trigger file. They could be the trap and buff items dowlle's tracker asks for (#248, #455, #486), sent by the multiworld instead of a timer.
- CTR Native Expanded's two-player co-op Adventure. The Archipelago client would need to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race when one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane; CTR Native Expanded also has Super/Ultra Hard in Adventure, with matching relic and crystal times). Less useful for Archipelago play.
- High Octane's ghost replay tools.
- Racers from other games (low priority): Mario Kart DS karts and drivers, or other PS1 formats, as custom racers.
- Better bots (low priority): smarter driving, item use, lines and shortcuts, with no stat advantage.
- PAL disc support (low priority). Upstream removed it on purpose (ctr-native #51, #54).
- macOS (low priority). First a clean 64-bit/arm64 build (builds are 32-bit x86 MinGW32 today), then a macOS platform layer: window, GL context, audio, controllers. Until then the Windows exe runs under CrossOver/Wine with GL 3.x (ctr-native #20, #33).
- Exact pointer tests in the level renderer: `226_00_DrawLevelOvr1P.c` tells a hi-res texture pointer from inline texture data by the word's value (inside the level buffer, word-aligned, plausible page). It agreed with the loader's pointer map in 100,000 lookups on Hot Air Skyway, so it is left alone. The exact version needs a flag carried next to scratchpad word 0x84.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support. Upstream removed it on purpose (ctr-native #51).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
