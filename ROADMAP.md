# Roadmap

Status of CTR Decrypted, kept current with every change. **Status values:** Done, In progress, Planned, Maybe, Not planned.

- How the project is set up and how to work on it: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- What the upstream projects' issue trackers say: [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md).

## Decisions
- **One repository** for the game and its Archipelago client. The `ctr.apworld` currently ships unchanged from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld). If we change generation logic, the world source moves into `apworld/` here, so the client and world are always versioned together.
- **Base: dowlle/ctr-native-ap `main`** (v0.2.4 plus remappable controls and the hook split). It had the Archipelago client, a config menu, widescreen, borderless fullscreen, a dithering toggle, remappable controls and CI. Seeds made with its `ctr.apworld` 0.2.4 work.
- **High Octane is ported feature by feature, never merged.** A trial merge conflicts in 92 files (351 hunks), and High Octane carries Vita, web and leaderboard code we don't want.
- **Windows first.** The Linux build from upstream should keep compiling, but only Windows is tested.

## Done
| Item | Notes |
|---|---|
| Repo set up on dowlle/ctr-native-ap `main` | Imported through `tools/decrypted/sync-upstream.ps1` |
| Windows checkout fix | `*.patch` files are forced to LF so the vendor hash check passes with `core.autocrlf=true` |
| Native Switch 2 Pro Controller | Vendored libusb 1.0.30 in `externals/libusb`, so SDL's Switch 2 driver works without Steam. Tested over USB. Steam must not be running, or it grabs the pad first |

## In progress
| Item | Notes |
|---|---|
| 60 FPS mode | Branch `feature/60fps`. Port of High Octane's runtime option: commit 706f1819b and about 20 follow-ups fixing animation speed and jump height. Toggle in the options menu, on by default. Archipelago timers are adjusted so they keep real-time durations. See `docs/60FPS.md` once merged |

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| VSync / frame-pacing option | No build has one (High Octane #46) |
| Softlock or black screen on the credits and scrapbook after beating Oxide | ctr-native #45 and #52, High Octane #24 |
| Mystery Caves left path crash with 3–4 players | ctr-native #58. Fixed in High Octane (#30); port it |
| Stage-2 key count ignored with vanilla unlock requirements | ctr-native-ap #499 |
| White noise in the hub after winning a race | ctr-native-ap #484 |
| Progressive stats bar doesn't match the items received | ctr-native-ap #466 |
| Invisible podium racers | ctr-native-ap #282 |
| Reconnect catch-up stalls the game; move network work off the game thread | ctr-native-ap #147 |
| Internal resolution scaling, FXAA, PGXP perspective correction (anti-wobble) | High Octane |
| Post-processing filters (CRT, sharp bilinear, …) | High Octane |
| Playable N. Oxide outside Archipelago restrictions | High Octane (Archipelago already has `remove_playable_oxide`) |
| Reserves meter, blue fire (USF), powerslide bar, cyan super turbo pads | ModSDK mods as ported by High Octane |
| Merge newer ctr-native upstream work (decomp matching, gameplay fixes since July 2026) | Both forks are 100+ commits behind. Expect ~460 conflict hunks |
| Pre-race camera flyover is wrong | ctr-native #14 |

## Maybe
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play.
- Custom racers (`.ctrr`) and High Octane's ghost replay tools.
- A 4:3 / 5:4 aspect option for CRTs (High Octane #44).
- Following dowlle's 0.3.0 work once it is released: boss randomization, Wumpa shop, solo mode, custom tracks.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- PAL/JP disc support. Upstream removed it on purpose (ctr-native #51).
