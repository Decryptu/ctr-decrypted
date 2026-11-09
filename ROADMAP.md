# Roadmap

Open work only: what is in progress, what is planned, what may come, and what is not planned. A finished item is deleted. Standing design rules live in the doc of the feature they govern ([docs/](docs/)), not here.

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## In progress
- Mario Kart DS course converter ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)). All 32 race courses convert, were replayed by the owner and are published in the [track catalogue](https://github.com/Decryptu/ctr-decrypted-tracks), version 1.0.0. Left:
  - Object behaviour read in the game's code and not built: tyres turning; a fire plant's head turning to its path; Boos ringing the kart, where here they hover at their spots; a staked Chain Chomp aiming at the kart, where here it lunges at the route; Waluigi Pinball's drums taking one of three speeds in turn; what a star does to a Thwomp, a Chomp or a flare; animated models (paintings, the crowd, Pokey's sway, the coffin's lid). Bowser Castle's slabs and Delfino Square's drawbridge do not travel, as the engine's track collision is static.
  - Custom tracks are one-player only. If they ever get split screen, its far emitter (`DrawLevelOvr1P_EmitSplitGroundListAQuadBlock`) must draw by face too.

## Planned
- Racer stats in the character select: a settings toggle that shows each racer's speed, acceleration and turning on the select screen, custom racers by their template. High Octane has a stats viewer to port from ([UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md#high-octane)).
- Sync dowlle: its 0.3.0 line went into `main`, which has about 230 client commits and 105 world commits we lack (`python tools/decrypted/upstream_status.py`). Import and merge as in [DEVELOPMENT.md](docs/DEVELOPMENT.md#syncing-upstream), then run the merge audit.
- The other High Octane features we want. Each `planned` line in `tools/decrypted/highoctane.txt` names its item here (`upstream_status.py --ho planned`):
  - **High Octane fixes:** check each one against our tree and retail first, as some may be fixed already or change retail behaviour. Widescreen garage stat bars and speedometer; TLUT precision, minimap seam, track preview aspect; Crash Cove ship PVS; mask voice in step with the audio; homing enemy missiles; deterministic Blizzard Bluffs and Sewer Speedway hazards; shadows over menu boxes; Wumpa fruit on the VS/Battle end screen; DualSense showing as two pads; scrapbook video playback; split-screen decals, PVS/LOD, Wumpa and viewports, and the quadblock retry list overflow.
  - **Default camera and HUD options:** the race starts with the chosen camera distance and HUD element.
  - **Lap count 1-9** in Arcade and Cup, with High Octane's fix for the mempack overrun at 8 and 9 laps.
  - **Cheats menu:** the retail cheat codes as toggles in Options.
  - **Main menu:** an Exit Game entry and cursor clamping.
  - **Bots racing as unlocked and custom racers:** a three-way AI Racers option (retail, unlocked, custom too).
  - **Multiplayer character select:** players 2-4 can change page, and players may pick the same racer.

## Maybe
- Title screen: a drawn logo in place of the DECRYPTED text, and the release version in a corner.
- Chaos effects as Archipelago traps. CTR Native Expanded has 38 race effects, harmful and helpful (reversed steering, low gravity, giant karts, no items...), with an external trigger file. They could be the trap and buff items dowlle's tracker asks for (#248, #455, #486), sent by the multiworld instead of a timer.
- CTR Native Expanded's two-player co-op Adventure. The Archipelago client would need to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race when one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane; CTR Native Expanded also has Super/Ultra Hard in Adventure, with matching relic and crystal times). Less useful for Archipelago play.
- High Octane's ghost replay tools.
- Custom racer sounds, mask and wheels: High Octane lets a custom racer bring sampled voice lines and choose its mask and wheels.
- Racers from other games (low priority): Mario Kart DS karts and drivers, or other PS1 formats, as custom racers.
- Better bots (low priority): smarter driving, item use, lines and shortcuts, with no stat advantage.
- PAL disc support (low priority), which would also bring High Octane's multilanguage text and PAL voices, read from the PAL disc. Upstream removed it on purpose (ctr-native #51, #54).
- macOS (low priority). First a clean 64-bit/arm64 build (builds are 32-bit x86 MinGW32 today), then a macOS platform layer: window, GL context, audio, controllers. Until then the Windows exe runs under CrossOver/Wine with GL 3.x (ctr-native #20, #33).

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support. Upstream removed it on purpose (ctr-native #51).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
