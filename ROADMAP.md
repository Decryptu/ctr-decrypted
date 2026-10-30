# Roadmap

## Base and layout
- **One repository.** The game and its Archipelago client live here. The `ctr.apworld` currently ships unchanged from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld). If we change generation logic, the world source moves into `apworld/` in this repo, so the client and world are always versioned together.
- **Base: dowlle/ctr-native-ap `main`** (v0.2.4 plus remappable controls and the hook split).
  - It already has the Archipelago client, a config menu, widescreen, borderless fullscreen, a dithering toggle, remappable controls, and CI.
  - Its additions are mostly in their own folders (`ap/`, `tools/`), which keeps syncing with upstream manageable.
- **High Octane is ported feature by feature, not merged.** A trial merge conflicts in 92 files, and High Octane carries Vita, web and online-leaderboard code we don't need.

## Planned

### 1. 60 FPS mode (top priority)
- Port High Octane's runtime 60 FPS option: the `FPS_DOUBLE`/`FPS_HALF` timing in the game logic and the follow-up fixes for animation speed and jump height.
- Make the Archipelago timers (traps, notices, item feed) respect it.
- Add VSync and frame-pacing options. No current build has a VSync option.

### 2. Fixes found in upstream issue trackers
- **Credits and scrapbook** after beating Oxide go to a black screen or softlock (ctr-native #45 and #52, High Octane #24).
- **Mystery Caves left path** crashes with 3–4 players (ctr-native #58). High Octane fixed this; port the fix.
- **Pre-race camera flyover** is wrong (ctr-native #14).
- **Archipelago:**
  - Stage-2 key count is ignored with vanilla unlock requirements (#499).
  - White noise in the hub after a race (#484).
  - Progressive stats bar doesn't match the items received (#466).
  - Invisible podium racers (#282).
  - A large reconnect catch-up stalls the game (#147). Move network I/O off the game thread.

### 3. High Octane enhancements worth porting
- Internal resolution scaling, FXAA, and PGXP perspective correction (anti-wobble).
- Post-processing filters (CRT, sharp bilinear).
- Playable N. Oxide.
- ModSDK mods ported by High Octane:
  - Reserves meter
  - Blue fire (USF)
  - Powerslide bar
  - Cyan super turbo pads

### 4. Platform
- Build SDL with libusb, so Switch 2 Pro controllers work without Steam Input.
- Merge newer ctr-native upstream work: decomp matching and gameplay fixes since July 2026.

## Not planned
- PS Vita and web builds, AdHoc netplay, and High Octane's online leaderboard. They depend on its author's server.
