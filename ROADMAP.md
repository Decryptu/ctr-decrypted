# Roadmap

Open work only: what is in progress, what is planned, what may come, and what is not planned. A finished item is deleted. Standing design rules live in the doc of the feature they govern ([docs/](docs/)), not here.

- How to work on the project: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Planned
- Nitro-Fueled gameplay enhancements as opt-in options/toggles (off by default; physics-changing toggles stay separated from Classic Time Trial records and Archipelago rules):
  - **Revamped USF / Blue Fire & reserves:** powersliding, ordinary turbo pads, boost ramps and jump/hang-time boosts feed reserves without cancelling or downgrading USF / higher fire tiers (instead of retail's reset on drift or lower-tier boost).
  - **U-turn reserve preservation:** air-braking and ground U-turning (Down + Brake + steer with gas released) keep reserves and active fire tier instead of wiping them on brake.
  - **Perfect-boost reserve weighting:** late/perfect powerslide boosts grant stronger reserve gains relative to early boosts, matching Nitro-Fueled's risk/reward.
- **Circular drift-boost ring** (opt-in HUD option): a Breath of the Wild-style circular stamina ring drawn near the top-right of the kart (near screen center) instead of having to watch the bottom-right bar. Only shown while drifting (`driftMeter > 0`), fills circularly with the green-to-red progression, and marks the boost threshold zones with splits/ticks so the Good/Perfect window is easy to read without taking eyes off the track.

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
- Nitro-Fueled Engine Swap, Drift driving class and Ring Rally mode.
