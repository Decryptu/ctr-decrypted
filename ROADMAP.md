# Roadmap

What is in progress, what is next, and what was decided. A finished item is deleted (git history has it); features are described in [docs/](docs/).

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game, its Archipelago client and its world. The world lives in `apworld/ctr`, imported with its history from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld) (`worlds/ctr`), so client and world are versioned together.
- **Base: dowlle/ctr-native-ap** `main` (v0.2.4) plus its unreleased 0.3.0 line (`dev/custom-pad-0.3.0`, the signed updater, Play Solo, the xdelta importer). Seeds made with `ctr.apworld` 0.2.4 still work.
- **Custom racers are new slots, not replacements.** Each has its own icon, name, model and textures, and borrows a retail character ID (its template) only for systems keyed by that ID (voice, podium, records, Archipelago). Real IDs above 15 would touch about a hundred sites, save formats included (a memory card ghost packs the ID into 4 bits). Portraits go to a second VRAM bank, so the template keeps its own and can race in the same field (High Octane overwrites the template's textures, its #36).
- **Racers ship from [Decryptu/ctr-decrypted-racers](https://github.com/Decryptu/ctr-decrypted-racers)**, an index the Mods screen downloads from, like Saphi for tracks. Each racer is credited to its author. A package holds only the mod's own model and portrait, nothing from the player's disc; a racer that reuses a look from another release credits it.
- **Tracks also ship from [Decryptu/ctr-decrypted-tracks](https://github.com/Decryptu/ctr-decrypted-tracks)**, listed with Saphi's on the Tracks tab, so a track need not wait for Saphi. A track both list shows once, with Saphi's row winning a tie ([CUSTOM_TRACKS.md](docs/CUSTOM_TRACKS.md#the-decrypted-track-catalogue)).
- **The Mods screen has no Karts tab**: a racer's model is driver and kart in one mesh, and wheels are shared sprites (owner decision, 2026-10-31).
- **60 FPS simulates at 60 Hz** (owner decision, 2026-11-02). A step that runs once per frame instead of per unit of time is fixed where it happens ([60FPS.md](docs/60FPS.md)). No fixed 30 Hz simulation with interpolated rendering: it would bring back 33 ms input steps and touch every rendered position.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files, and it carries Vita, web and leaderboard code we don't want.

## In progress
- Mario Kart DS course converter ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)). All 32 race courses convert and finish laps on autopilot. Next steps, in this order:
  1. The owner's second replay of the 24. Treated since it, and installed in the test game for a third pass: confirm or reopen each row, then drop what is gone.
     - **Kart launched forward on the road.** Open, not reproduced. The owner feels it on most courses, often while hopping: DK Pass, GCN Mushroom Bridge (at about 14919,3928, driving to the tunnel), N64 Banshee Boardwalk (at about 6932,9008). There the collision is smooth or flat, the kart's own ground search agrees with it on a 48-unit grid, no trigger or special surface is near, and scripted drives with hops at three offsets across the road show no jump in speed. An `F7` right after one now saves the kart's last ten seconds (`kart.csv`), which is what this needs.
     - **Thin line gaps at texture joins.** Opaque polygons of a converted track grow by a fraction of a pixel now, so joins that round apart overlap. No gap shows in hidden runs at 1536 wide, so the fix itself is unmeasured: confirm in play on N64 Choco Mountain and GCN Mushroom Bridge's last tunnel.
     - **CPU karts.** They float over ramps and drive through bumps no more, as lanes lie on the ground (DK Pass's snow descent, N64 Banshee Boardwalk's ramps, Wario Stadium's bumps): confirm in play, with how they take DK Pass's jump pad near the end. DK Pass's wrong depth order of ground and CPU karts on the same descent may have been this too.
     - **Item boxes in the air.** Boxes reach a kart over them now on every course. The hops tried did not rise past the old limit, so confirm in play.
     - **Surfaces.** SNES Donut Plains 1's mud road, Wario Stadium and SNES Choco Island 2 take the game's Mud terrain, by the KCL's own variant: confirm the look and the slowdown. Boost ramps are super turbo pads (Cheep Cheep Beach, Wario Stadium, Waluigi Pinball, Bowser Castle): confirm they are not too strong. SNES Donut Plains 1, the wooden bridge's texture when riding on it, is still open.
     - **N64 Frappe Snowland's Aku Aku shortcut.** Off-road ground closer to another part of the lap has no checkpoint now: confirm on the left of the canyon, at about 2514,13259.
     - **Frame rate.** Not re-measured since the sky fix: the last `--fps` run shared the machine with other work. The fix adds up to 10% more polygons at a peak (Waluigi Pinball), none at the frames sampled on DK Pass and Cheep Cheep Beach. Run `mkds_batch.py --fps` on a quiet machine, with the full run after steps 2 and 3.
     - **Left from the first replay, no report either way:** the flicker on GBA Luigi Circuit, GCN Baby Park, Luigi's Mansion, Mario Circuit, Shroom Ridge, Yoshi Falls; the ghost shadows on GBA Peach Circuit; the minimap fill's strength; the camera on Cheep Cheep Beach's boost jump. Custom tracks are one-player only: if they ever get split screen, its far emitter (`DrawLevelOvr1P_EmitSplitGroundListAQuadBlock`) must draw by face too.
  2. The 8 courses that used to stop at something not converted now finish laps on autopilot, none of it seen in play yet. Flights ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md#flights)) carry the cannons, Rainbow Road's two loops and Bowser Castle's lava jump and room exit; jump pads, lowered bridges and a checkpoint fix do the rest.
     - **Play all 8:** how a flight feels (speed, height, camera, the kart upside down in the loops), the jump pads' height on GBA Bowser Castle 2 and GBA Sky Garden, how the baked bridges look on Delfino Square and Bowser Castle.
     - **Rainbow Road:** the autopilot falls off the rising ring at about LEV (-1800, 600, 11600), road half as wide as the kart turns at speed, and hops on 5 times in two laps. MKDS has no rails there: see in play whether a player keeps to it, or whether it needs edges.
     - **Bowser Castle:** `--check` fails on 2 height probes of 659,414, at LEV (-7744, 182, -2176), on a thin road ledge beside the turning log's hub that a kart does not reach; the autopilot also meets the hub's wall once a lap.
     - **CPU karts have no flights:** they hold their lanes through the air, at their own speed and upright.
  3. Moving course objects (cars, Goombas, Thwomps, flippers): none are converted, and some courses are built round them. Obstacles reuse CTR's own moving hazards. Floors that move are not planned, as the engine's track collision is static (owner decision, 2026-11-05): Tick-Tock Clock's cogs stay still floors, and moving bridges are baked solid in their open position.
  4. Publish the finished tracks in the catalogue (`tools/tracks/build_index.py`) and submit them to Saphi, then set `saphi_track_id` on any upload whose bytes differ. Catalogue tracks need a picture, the 32 converted courses too: the Mods screen shows only Saphi's today (owner reminder, 2026-11-05).

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

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support. Upstream removed it on purpose (ctr-native #51).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
