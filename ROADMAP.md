# Roadmap

Open work only: what is in progress, what is planned, what may come, and what is not planned. A finished item is deleted. Standing design rules live in the doc of the feature they govern ([docs/](docs/)), not here.

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Planned
- Oxide Station CTR Challenge: the "T" (9277, 422, 18400) and "R" (7168, 498, 11648) are reported unreachable at 60 FPS in play. Scripted `fps-drive.ps1` runs of the R launch (`CTR_DEV_WARP=6900,150,17600,2048`, throttle, hop at the lip) reach it as closely at 60 as at 30, so the cause is not found yet: get the player's exact line (an input replay, [REPLAYS.md](docs/REPLAYS.md)) and compare it at both rates and against retail in PCSX-Redux. Letter positions come from the token-race level (`CTR_DEV_MODE=adv`, `CTR_DEV_CHEATS=8`).
- Japanese Fake Crash imported by the game: an Extras row that takes the Japanese `.bin` from `assets`, with the same help lines as Voices ([EXTRAS.md](docs/EXTRAS.md#voices)), instead of running `extract_fakecrash_jp.py` ([CUSTOM_RACERS.md](docs/CUSTOM_RACERS.md)).
- Japanese voices: match the 12 racer clips outside the voice set tables (NTSC-U 190, 191, 304–313). They play in US English today ([EXTRAS.md](docs/EXTRAS.md#voices)).

- Archipelago chat in game: a text box (from the pause menu, and wherever the client is connected) that sends a line to the room as chat or a `!command` (`!hint`, `!getitem`, `!release`...), with the recent room messages shown above it, so nobody needs the Archipelago Text Client open alongside. It reuses the SDL text input the Connection page already has, and needs an on-screen prompt for the key or button that opens it.

## Maybe
- CTR Native Expanded's two-player co-op Adventure. The Archipelago client would need to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race when one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane; CTR Native Expanded also has Super/Ultra Hard in Adventure, with matching relic and crystal times). Less useful for Archipelago play.
- High Octane's ghost replay tools.
- Custom racer sounds, mask and wheels: High Octane lets a custom racer bring sampled voice lines and choose its mask and wheels.
- Racers from other games (low priority): Mario Kart DS karts and drivers, or other PS1 formats, as custom racers.
- Better bots (low priority): smarter driving, item use, lines and shortcuts, with no stat advantage.
- PAL disc support (low priority): the port boots and plays from a PAL dump, with High Octane's multilanguage text. This is not taking single PAL assets or fixes onto the NTSC-U disc. Upstream removed it on purpose (ctr-native #51, #54).
- macOS (low priority). First a clean 64-bit/arm64 build (builds are 32-bit x86 MinGW32 today), then a macOS platform layer: window, GL context, audio, controllers. Until then the Windows exe runs under CrossOver/Wine with GL 3.x (ctr-native #20, #33).

## Not planned
- PS Vita and web builds, AdHoc netplay.
- Chaos effects as Archipelago traps or buffs (CTR Native Expanded's race effects). Trap items and their ids come from dowlle's apworld, which we follow; we add none of our own.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support: the port booting and playing from a Japanese dump. Upstream removed it on purpose (ctr-native #51). Single Japanese assets on the NTSC-U disc, such as Fake Crash ([CUSTOM_RACERS.md](docs/CUSTOM_RACERS.md)), are a separate thing.
- Travelling floors on converted courses (Bowser Castle's slabs, Delfino Square's drawbridge): the engine's track collision is static, so they keep a fixed shape ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
