# Roadmap

Open work only: what is in progress, what is planned, what may come, and what is not planned. A finished item is deleted. Standing design rules live in the doc of the feature they govern ([docs/](docs/)), not here.

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## In progress
- First release with 64-bit Linux builds: its notes must tell Linux players to download `-linux-x86_64.tar.gz` (or `-linux-arm64.tar.gz`) by hand once. Their installed 32-bit build looks for `-linux-x86.tar.gz`, so its updater never offers the new release ([docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#releases-and-updates)). Delete this row once that release is out.
- macOS on Apple Silicon ([docs/MACOS.md](docs/MACOS.md)): built, packaged as an app, assembled into releases and updated like the other targets. Not yet checked: the in-game update on a Mac, which needs two releases that carry macOS; check it on the first update after the first release with macOS. Until that release, the Windows exe runs under CrossOver/Wine with GL 3.x (ctr-native #20, #33).

## Maybe
- CTR Native Expanded's two-player co-op Adventure. The Archipelago client would need to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race when one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane; CTR Native Expanded also has Super/Ultra Hard in Adventure, with matching relic and crystal times). Less useful for Archipelago play.
- Racers from other games (low priority): Mario Kart DS karts and drivers, or other PS1 formats, as custom racers.
- Better bots (low priority): smarter driving, item use, lines and shortcuts, with no stat advantage.
- PAL disc support (low priority): the port boots and plays from a PAL dump, with High Octane's multilanguage text. This is not taking single PAL assets or fixes onto the NTSC-U disc. Upstream removed it on purpose (ctr-native #51, #54).

## Not planned
- PS Vita and web builds, AdHoc netplay.
- Chaos effects as Archipelago traps or buffs (CTR Native Expanded's race effects). Trap items and their ids come from dowlle's apworld, which we follow; we add none of our own.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support: the port booting and playing from a Japanese dump. Upstream removed it on purpose (ctr-native #51). Single Japanese assets on the NTSC-U disc, such as Fake Crash ([CUSTOM_RACERS.md](docs/CUSTOM_RACERS.md)), are a separate thing.
- Travelling floors on converted courses (Bowser Castle's slabs, Delfino Square's drawbridge): the engine's track collision is static, so they keep a fixed shape ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
- Nitro-Fueled Engine Swap, Drift driving class and Ring Rally mode.
