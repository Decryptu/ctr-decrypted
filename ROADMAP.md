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
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files (351 hunks), and High Octane carries Vita, web and leaderboard code we don't want.

## In progress
Nothing.

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| Steam Deck test: the owner's tester runs the checklist in [STEAM-DECK.md](docs/STEAM-DECK.md#testing-on-a-deck) on v1.3.0, the first release with the Deck defaults. It has not run on a Deck yet | Owner request, 2026-10-31 |
| Level faces still vanish in widescreen with the far camera (`L2`), in a few spots. Leads, in order: the clip-record window in `game/226/226_00_DrawLevelOvr1P.c` (`DrawLevelOvr1P_SourceInsideClipRecordWindow`) is widened for the aspect ratio but sized for the near camera's distance; CTR Native Expanded drops that window and lets the near clipper decide (their `209b71fe`). Their increased-draw-distance fixes (`a81bf65b`, `e0fedbf2`: the far LOD missing from some paths, 2P overlay included) are the same family. Related: with increased draw distance, `DrawTires` skips a wheel whose camera-relative position does not fit the GTE's 16 bits, so distant karts lose their wheels. Do it their way (`26e520a2`): project those wheels at full width and draw them. Find a spot, reach it with `CTR_DEV_WARP` + `CTR_DEV_PAD` (`2` is the far camera) and prove the fix with a dumped frame | Owner report, 2026-11-02 |
| README screenshots: a few in-game shots (a race at 16:9 with PGXP, the Archipelago HUD, custom tracks, the Mods screen), captured with `devrun.ps1` so they can be retaken after a visual change | Owner request, 2026-11-02 |
| Save-file tool (dev): read and edit the game save (and the Archipelago progress next to it) from a script: unlocked racers and tracks, trophies, relics, keys, gems, tokens, Adventure position, so a test or a bug repro can start from any point of the game. Today only `CTR_DEV_UNLOCKS` (secret racers) and new-profile runs exist ([DEV-TOOLS.md](docs/DEV-TOOLS.md)) | Owner request, 2026-11-01 |
| 200cc: a faster speed class for every racer, bots included, like Mario Kart's. Raises top speed and acceleration together and keeps turning and drift timing playable; Time Trial records and ghosts are kept apart per class. Whether Archipelago can require or place it is a later choice. CTR Native Expanded's per-stat multipliers (speed, turn, jump, gravity, reserves) show where the stats are applied | Owner request, 2026-11-02 |
| Project website and guide (mid): GitHub Pages site with install steps, how to play with Archipelago, the dev toolbox, and our own YAML builder for `ctr.apworld` options, generated from the apworld's option definitions so it follows the version we ship. The README then links it instead of the external CTR YAML Builder | Owner request, 2026-10-31 and 2026-11-02 |

## Maybe
- Title screen and intro: our own touch on the title screen (CTR Native Expanded adds its name to the main menu), an option to skip the boot intros and copyright screen, and skippable podiums, boss cutscenes and mask hints (theirs: `skip_intro`, `skip_podium_ceremonies`, `boss_cutscenes`, `skip_hints`). Owner idea, 2026-11-02; what to change on the title screen is still open.
- Chaos effects as Archipelago traps. CTR Native Expanded has 38 race effects, harmful and helpful (reversed steering, low gravity, giant karts, no items, shuffled controls, free reserves, a mask...), with an external trigger file. They could be the trap and buff items dowlle's tracker asks for (#248, #455, #486), sent by the multiworld instead of a timer.
- CTR Native Expanded's two-player co-op Adventure (both racers in hub and races, shared progress). It would need the Archipelago client to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race once only one racer is left.
- Retail-exact physics at 60 FPS: run the simulation at a fixed 30 Hz and interpolate rendering. Today's 60 FPS integrates in 16 ms steps, so curved kickers launch a little higher (Dingo Canyon: apex rise 24 at 30 FPS, 31 to 33 at 60). A different design from High Octane's, touching every rendered position.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play. CTR Native Expanded also has Super/Ultra Hard, in Adventure too, kept per save with matching relic and crystal time limits.
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
