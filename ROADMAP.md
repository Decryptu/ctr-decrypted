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
- Mario Kart DS course converter ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)). All 32 race courses convert and 24 pass `mkds_batch.py --check`. Next steps, in this order:
  1. The owner's play-test of the 24. Everything reported has been treated or could not be reproduced, and the rebuilt packages are installed in the test game for a replay that confirms or reopens each row: drop what is gone, and fix the shared causes first. The groups are by symptom. Figure-8 Circuit is fine.
     - **Flickering surfaces.** The missing textures are fixed, shown on Cheep Cheep Beach only. Confirm in play that the rest stopped too: GBA Luigi Circuit, GCN Baby Park, GCN Mushroom Bridge, Luigi's Mansion, Mario Circuit, Shroom Ridge, Yoshi Falls, Peach Gardens, Wario Stadium.
     - **Ghost shadows on the ground, GBA Peach Circuit.** Fixed in `VehGroundShadow.c`: a kart's offset from the camera is kept x4 in 16 bits and wrapped at 8,192 units, putting a far kart's shadow by the camera. Not reproduced on the fixed build, as the owner's `F7` capture loads only in its own exe: confirm in play.
     - **Wrong ground and textures.** Near ground drew flat blue where a palette word equalled a level address, now read from the pointer map: seen gone on Desert Hills, confirm in play there and on GBA Luigi Circuit. DK Pass: on the snow descent after mid-lap, ground and CPU karts draw in the wrong depth order. Far walls bent or showed stretched texels on blocks with an uneven grid, drawn by face now: seen gone on Peach Gardens' castle, confirm in play. Custom tracks are one-player only: if they ever get split screen, its far emitter (`DrawLevelOvr1P_EmitSplitGroundListAQuadBlock`) must draw by face too. SNES Donut Plains 1: the wooden bridge's texture when riding on it.
     - **Thin line gaps at texture joins.** The near ones are overlapped now (`overlap_tjunctions`), measured on N64 Choco Mountain only. Confirm in play there and on N64 Moo Moo Farm, SNES Choco Island 2, SNES Koopa Beach 2 and SNES Mario Circuit 1. Still open: a hairline along a curved wall's foot from about depth 800, where a block draws as one far quad over its corners and misses the road's rim by about 0.2 pixels.
     - **Missing surroundings.** Backdrops and water are baked now, seen on SNES Mario Circuit 1, SNES Koopa Beach 2, GCN Yoshi Circuit and Tick-Tock Clock. Confirm in play. One-texture skies with clouds, sea or hills are baked too, seen on Desert Hills; Tick-Tock Clock's four road cogs stand as still floors until they can turn.
     - **Surfaces that act wrong.** Not reproduced, an `F7` capture at the spot is what is needed: Cheep Cheep Beach, the bumps where light sand meets wet sand are not smooth (no jolt on two laps and four straight runs across it; the seam's foam edge is jagged, if that is what was meant). GCN Mushroom Bridge, the kart is pulled forward in the middle of the road before the tunnel (on the autopilot's line real speed follows commanded speed within 2%, no boost, no special surface in the KCL). Off-route ground has no checkpoint now, which should end Aku Aku's grabs from infield grass (GBA Luigi Circuit, 592 blocks by a label jump, now 0) and the N64 Frappe Snowland respawn past the canyon (222, now 46): confirm both in play.
     - **Minimap.** The fill blends at retail's strength now (additive 64, it was 128), which should end the bleed over bright ground on GBA Peach Circuit, Peach Gardens, SNES Donut Plains 1 and SNES Koopa Beach 2: confirm in play.
     - **Frame rate.** All 32 fit 60 FPS by `mkds_batch.py --fps`: at most 1% of a race's frames over 16.7 ms, and Waluigi Pinball the slowest at a median of 8.9 ms. Confirm in play.
     - **Camera.** It rose over a wall behind the kart because converted walls carried `CAMERA_SEARCH`, which 91% of Crash Cove's do not: gone from them now, confirm in play on the Cheep Cheep Beach boost jump and in Desert Hills' sand.
  2. The 8 that stop at something not converted, each with no lap finished:
     - KCL 0x0f (cannon): Airship Fortress, Waluigi Pinball.
     - KCL 0x0c (jump pad): GBA Bowser Castle 2, GBA Sky Garden.
     - KCL 0x13 (loop) and the gaps round it: Rainbow Road.
     - Moving bridges: Bowser Castle (`rotary_bridge`), Delfino Square (`bridge`).
     - GCN Luigi Circuit: the kart wedges at LEV (-2090, -1226, -1550), three quarters round, and each hop forward is taken back there by Aku Aku: why it wedges is not known. Checkpoint labels 80 nodes apart touch near that spot, too close to the route for the off-route rule; see whether that is part of it.
     Waluigi Pinball peaks at 255 of the 256 near-camera blocks: when it can be driven, convert it with a larger `--max-edge`.
  3. Moving course objects (cars, Goombas, Thwomps, flippers): none are converted, and some courses are built round them. Tick-Tock Clock's cogs and clock hands stand still or are missing until then, and the cogs' floors should turn the kart.
  4. Publish the finished tracks in the catalogue (`tools/tracks/build_index.py`) and submit them to Saphi, then set `saphi_track_id` on any upload whose bytes differ. Catalogue tracks need a picture: the Mods screen shows only Saphi's today.

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
