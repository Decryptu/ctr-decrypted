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
| Repo set up on dowlle/ctr-native-ap `main` | Imported through `tools/decrypted/sync-upstream.ps1`. `*.patch` files forced to LF so the vendor hash check passes with `core.autocrlf=true` |
| Native Switch 2 Pro Controller | Vendored libusb 1.0.30 (`externals/libusb`), so SDL's Switch 2 driver works without Steam. Steam must not be running, or it grabs the pad first |
| PGXP: anti-wobble and perspective-correct textures | OPTIONS → Video → PGXP (Anti-Wobble), default FULL. Port of High Octane 03014d84f, plus fixes for distant-floor holes and texture sampler rounding. See [docs/PGXP.md](docs/PGXP.md) |
| 60 FPS mode | OPTIONS → Video → 60 FPS (Next Load), on by default, every mode including Time Trial. Port of High Octane's option plus fork fixes: ghosts and AI lap recordings keep 30 FPS units, replays and savestates keep their mode, AI speed. Checked unattended on every track; what only a player can check is listed in [docs/60FPS.md](docs/60FPS.md) |
| Developer tools for unattended runs | `CTR_DEV_*` switches: attract demo or race on any track, autopilot, traces, fast-forward, hidden window, frame capture, polygon dumps, all bit-exact. `devrun.ps1`, `fps-parity.ps1` and analysis scripts; `--perf` budget follows the frame rate. See [docs/DEV-TOOLS.md](docs/DEV-TOOLS.md) |
| Archipelago: starting racer and hub picker | All eight vanilla starters unlocked → the garage picker runs at a new profile. 50% black backdrop behind the in-hub picker |
| Boot splash centred when the window starts maximized | `NativeRenderer_UpdatePresentationViewport` reads the live window size |
| Upstream bug reports closed | Fixed: Mystery Caves crash with 3–4 players (ctr-native #58). Already fine here, checked unattended: credits and scrapbook (ctr-native #45, #52), reconnect catch-up (ctr-native-ap #147), stat bars (#466), vanilla-mode stage 2 keys (#499, via the apworld we ship). Details in [docs/UPSTREAM-ISSUES.md](docs/UPSTREAM-ISSUES.md) |

## In progress
Nothing.

## Planned
Roughly in this order.

| Item | Source |
|---|---|
| Frame pacing at 60 FPS with VSync (Off / On / Adaptive) on 60 Hz and 144 Hz | High Octane lacks VSync (#46). The game paces itself on a 59.94 Hz timer and VSync On adds the display's wait on top, so a 60 Hz panel drifts against it and a 144 Hz one cannot show 60 evenly. `tools/decrypted/vsync-check.ps1` measures it; it needs the screen on and nobody at the machine, so it runs overnight. Then fix what it shows |
| White noise in the hub after winning a race | ctr-native-ap #484. Not reproduced: after an Adventure win on Crash Cove, through the podium into N. Sanity Beach, `audio_noise.py` finds no noise in either build, fast-forwarded or real time. The report is from a connected session over several races, which the dev tools can't drive yet |
| Invisible podium racers | ctr-native-ap #282. Oxide in 1st has no win-dance model in the original game. Crash, Papu Papu and Komodo Joe all show after an Adventure trophy race; still to try: a Gem Cup podium, and a relic race on a trial track hosted in another hub |
| Internal resolution scaling, FXAA | High Octane |
| Post-processing filters (CRT, sharp bilinear, …) | High Octane |
| Playable N. Oxide outside Archipelago restrictions | High Octane (Archipelago already has `remove_playable_oxide`) |
| Reserves meter, blue fire (USF), powerslide bar, cyan super turbo pads | ModSDK mods as ported by High Octane |
| Merge newer ctr-native upstream work (decomp matching, gameplay fixes since July 2026) | Both forks are 100+ commits behind. Expect ~460 conflict hunks |
| Pre-race camera flyover is wrong | ctr-native #14 |

## Maybe
- Retail-exact physics at 60 FPS: run the simulation at a fixed 30 Hz and interpolate rendering. Today's 60 FPS integrates in 16 ms steps, so curved kickers launch a little higher (Dingo Canyon: apex rise 24 at 30 FPS, 31 to 33 at 60). A different design from High Octane's, touching every rendered position.
- Mirror mode, reverse tracks, Super/Ultra Hard, extra arcade modes, boss fights on any track (High Octane). Less useful for Archipelago play.
- Custom racers (`.ctrr`) and High Octane's ghost replay tools.
- A 4:3 / 5:4 aspect option for CRTs (High Octane #44).
- Following dowlle's 0.3.0 work once it is released: boss randomization, Wumpa shop, solo mode, custom tracks.

## Not planned
- PS Vita and web builds, AdHoc netplay.
- High Octane's online leaderboard, which depends on its author's server.
- PAL/JP disc support. Upstream removed it on purpose (ctr-native #51).
