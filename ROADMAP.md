# Roadmap

What is being worked on, what is next, and what was decided. Kept current with every change: a finished item is deleted (git history has it), and features are described in [docs/](docs/).

- How the project is set up and how to work on it: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream projects' issue trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game, its Archipelago client and its Archipelago world. The world lives in `apworld/ctr`, imported with its history from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld) (`worlds/ctr`), so the client and world are versioned together.
- **Base: dowlle/ctr-native-ap** `main` (v0.2.4) plus its unreleased 0.3.0 line (`dev/custom-pad-0.3.0`, the signed updater, Play Solo and the xdelta importer). Seeds made with `ctr.apworld` 0.2.4 still work.
- **Custom racers are new slots, not replacements.** Each racer has its own icon, name, model and textures, and borrows a retail character ID (its template) only for the systems keyed by character ID (voice, podium, records, Archipelago). Real IDs above 15 would touch about a hundred sites, save formats included (a memory card ghost packs the ID into 4 bits). Their portraits go to a second VRAM bank, so the template keeps its own and can race in the same field (High Octane overwrites the template's textures, its #36). The racer mods collected so far are untextured meshes; a textured racer would use the same bank.
- **Racers ship from [Decryptu/ctr-decrypted-racers](https://github.com/Decryptu/ctr-decrypted-racers)**, an index the Mods screen downloads from, like Saphi for tracks. Each racer is credited to its author. A package holds only the mod's own model and portrait, nothing read from the player's disc; a racer that reuses a look from another release credits it.
- **The Mods screen has no Karts tab**: a racer's model is driver and kart in one mesh, and wheels are shared sprites (owner decision, 2026-10-31).
- **60 FPS simulates at 60 Hz** (owner decision, 2026-11-02). Where it differs from 30 FPS, the step that runs once per frame instead of per unit of time is fixed where it happens ([60FPS.md](docs/60FPS.md)); no fixed 30 Hz simulation with interpolated rendering, which would bring back 33 ms input steps and touch every rendered position.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files (351 hunks), and High Octane carries Vita, web and leaderboard code we don't want.

## In progress
Nothing.

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| 200cc: a faster speed class for every racer, bots included, like Mario Kart's. Raises top speed and acceleration together and keeps turning and drift timing playable; Time Trial records and ghosts are kept apart per class. Whether Archipelago can require or place it is a later choice. CTR Native Expanded's per-stat multipliers (speed, turn, jump, gravity, reserves) show where the stats are applied | Owner request, 2026-11-02 |

## Maybe
- Title screen: a drawn logo in place of the DECRYPTED text line, and the release version in a corner. Owner idea, 2026-11-02.
- Chaos effects as Archipelago traps. CTR Native Expanded has 38 race effects, harmful and helpful (reversed steering, low gravity, giant karts, no items, shuffled controls, free reserves, a mask...), with an external trigger file. They could be the trap and buff items dowlle's tracker asks for (#248, #455, #486), sent by the multiworld instead of a timer.
- CTR Native Expanded's two-player co-op Adventure (both racers in hub and races, shared progress). It would need the Archipelago client to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race once only one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play. CTR Native Expanded also has Super/Ultra Hard, in Adventure too, kept per save with matching relic and crystal time limits.
- High Octane's ghost replay tools.
- Asset converters from other games (low priority): tracks, models and textures, for example from Mario Kart DS or from PS1 games with similar formats. Depends on custom tracks and racers.
- Better bots (low priority): smarter driving, item use, racing lines and shortcuts, with no speed or other stat advantage.
- PAL disc support (low priority). Upstream removed it on purpose (ctr-native #51, #54), so it would be our own work.
- macOS (low priority). First a clean 64-bit/arm64 build (today's builds are 32-bit x86 MinGW32), then a macOS platform layer: window, GL context, audio and controllers. Until then the Windows exe under CrossOver/Wine is the stopgap; it needs GL 3.x there (ctr-native #20, #33).
- Exact pointer tests in the level renderer: `226_00_DrawLevelOvr1P.c` tells a hi-res texture pointer from inline texture data by the word's value (inside the level buffer, word-aligned, plausible page). Checked against the loader's pointer map on Hot Air Skyway in both builds: no disagreement in 100,000 lookups, so it is left alone. The exact version needs a flag carried next to scratchpad word 0x84.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support. Upstream removed it on purpose (ctr-native #51).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The Archipelago client and our platform code use GCC extensions and options; Windows builds use MSYS2 MinGW32.
