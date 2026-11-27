# Roadmap

Open work only: what is in progress, what is planned, what may come, and what is not planned. A finished item is deleted. Standing design rules live in the doc of the feature they govern ([docs/](docs/)), not here.

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## In progress
- 64-bit engine (x86_64, then arm64), so the game can later run natively on macOS and 64-bit Linux. Disc data uses PSX pointers and runtime structs size themselves ([docs/64BIT.md](docs/64BIT.md)). `-DCTR_64BIT=ON` builds it with a 64-bit compiler. The x86_64 builds (vanilla, and Archipelago with custom tracks) draw the same frames and give the same traces and results as the 32-bit builds on the title menu, three attract demos, both autopilot races, the N. Sanity Beach hub, a Gem Cup, two battle arenas and the Cortex Vortex custom race; savestates work on them. Left: build and run it on 64-bit Linux and arm64, and a CI job that builds the 64-bit engine on Linux x86_64, Linux arm64 and Windows x86_64.
- Cortex Vortex (built-in custom track) draws 160 level faces with a pointer table read as a texture layout: in a `CTR_DEV_DUMP_FRAME=4000` dump of an autopilot `CTR_DEV_CUSTOM` race their `T` lines carry tpage/CLUT that are the halves of a relocated address (`027a`/`025a` on the 32-bit build, `8030`/`8010` with PSX pointers), and a wall left of the start line shows the garbage. Likely the hi-res (mosaic) table behind those faces' texture slot 0x24 is laid out differently in this original-format LEV than the renderer expects (`DrawLevelOvr1P_ReadMosaicWord`); find the face, compare with a retail LEV's table, and fix the data or the reader.

## Maybe
- CTR Native Expanded's two-player co-op Adventure. The Archipelago client would need to know about a second player.
- More multiplayer from CTR Native Expanded: 3P/4P Arcade, eight racers in split screen, full-detail split screen, Oxide Station in multiplayer, race tracks in Battle, ending a race when one racer is left.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane; CTR Native Expanded also has Super/Ultra Hard in Adventure, with matching relic and crystal times). Less useful for Archipelago play.
- Racers from other games (low priority): Mario Kart DS karts and drivers, or other PS1 formats, as custom racers.
- Better bots (low priority): smarter driving, item use, lines and shortcuts, with no stat advantage.
- PAL disc support (low priority): the port boots and plays from a PAL dump, with High Octane's multilanguage text. This is not taking single PAL assets or fixes onto the NTSC-U disc. Upstream removed it on purpose (ctr-native #51, #54).
- macOS (low priority). After the 64-bit engine (In progress), a macOS platform layer: window, GL context, audio, controllers. Until then the Windows exe runs under CrossOver/Wine with GL 3.x (ctr-native #20, #33).

## Not planned
- PS Vita and web builds, AdHoc netplay.
- Chaos effects as Archipelago traps or buffs (CTR Native Expanded's race effects). Trap items and their ids come from dowlle's apworld, which we follow; we add none of our own.
- High Octane's online leaderboard, which depends on its author's server.
- JP disc support: the port booting and playing from a Japanese dump. Upstream removed it on purpose (ctr-native #51). Single Japanese assets on the NTSC-U disc, such as Fake Crash ([CUSTOM_RACERS.md](docs/CUSTOM_RACERS.md)), are a separate thing.
- Travelling floors on converted courses (Bowser Castle's slabs, Delfino Square's drawbridge): the engine's track collision is static, so they keep a fixed shape ([TRACK-CONVERTER.md](docs/TRACK-CONVERTER.md)).
- Upstream's MSVC build (`build-msvc.bat`, the MSVC presets in `CMakePresets.json`). The client and our platform code use GCC extensions; Windows builds use MSYS2 MinGW32.
- Nitro-Fueled Engine Swap, Drift driving class and Ring Rally mode.
