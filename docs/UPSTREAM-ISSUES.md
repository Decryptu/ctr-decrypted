# Upstream projects and their open issues

A snapshot of the projects CTR Decrypted draws from, taken 2026-10-30. Refresh it when you sync upstream or pick work from their trackers. Issue numbers refer to each project's own tracker.

## How they relate

```
CTR-tools/CTR-ModSDK            decompilation (now only mods and OnlineCTR)
  └─ CTR-tools/ctr-native       native PC port; contains ModSDK's full history
       ├─ Rinnegatamante/Crash-Team-Racing-High-Octane   enhanced fork (Vita first)
       └─ dowlle/ctr-native-ap  Archipelago client fork  ← our base
            └─ Decryptu/ctr-decrypted (this repo)
```

All four are GPL-3.0. dowlle's `ctr.apworld` is MIT.

| | Merge-base with ctr-native | Behind ctr-native | Notes |
|---|---|---|---|
| High Octane (`vita`) | 2026-07-16 | 118 commits | Last real upstream merge 2026-07-17 |
| ctr-native-ap (`main`) | 2026-07-02 | 201 commits | Syncs by cherry-pick. `dev/custom-pad-0.3.0` is ~135 commits ahead of `main` |

## CTR-tools/ctr-native
**State:**
- Accuracy-focused. Almost all commits since August are `match(...)` work restoring exact retail C.
- The maintainer turns down large feature PRs.
- No CI, and no release since Beta 7.1 (2026-07-07).
- 32-bit only; NTSC-U only.
- No config file, settings menu, widescreen or 60 FPS in master.

**Open issues:**
- #58: Mystery Caves left path crashes with 3–4 players. Fixed here (a null low-LOD texture read in the 3P/4P renderer, `226_00_DrawLevelOvr1P.c`).
- #12: black screen with a segfault on Manjaro Linux after the audio stream opens.
- #45, #52: black screen and no scrapbook after the credits, after beating Oxide. Not reproduced here: the ending, credits and scrapbook play through (`CTR_DEV_ENDING=1`). The Archipelago build sends a connected game to the hub instead of the scrapbook.
- #14: the pre-race camera flyover is wrong (suspected undefined behaviour or compiler-dependent).
- #61: unlock all characters including Oxide. Oxide is playable here (unlocked by beating him, or the Spyro 2 demo code); the other characters unlock as in retail.
- #54: PAL support. Out of scope per #51.
- #43: Steam Deck: needs Steam Linux Runtime 4.0, the cursor stays visible, 16:10 requested.
- #25: Android. #20: Mac.

**Open PRs:**
- #62: FXAA plus an ini config with physics multipliers. Old base, not mergeable.
- #27: reference implementation to unlock all characters.
- #41: Android GLES.
- #49: winget/SDK presets.

**Recurring themes:**
- Crashes from instance-pool exhaustion and memory growth (fixed).
- Rendering glitches.
- Large feature PRs closed unmerged: SSAA/PGXP, physics, custom tracks.

## High Octane
**State:**
- Feature-rich, Vita first, Windows releases since v1.3. Latest is v1.5 (2026-10-12).
- Heavy divergence: 709 files changed against the merge-base, and about 78 files carry `__vita__` ifdefs.
- The Vita build can't be reproduced from the public repo (needs a patched SDL3 and private shader blobs).

**Features it adds:**
- Graphics:
  - 60 FPS (runtime option)
  - 960x544 internal resolution, MSAA on Vita, FXAA on PC
  - PGXP
  - Post-processing filters
  - Dithering toggle
  - True widescreen
  - Borderless window
- Modes:
  - Mirror mode
  - Boss fights on any track
  - Super/Ultra Hard
  - Relic races outside Adventure
  - Reverse tracks and an alternative Tiger Temple
  - Lap count 1–9
  - Ghost replay tools, 7 ghosts per track
  - Online leaderboard
- Characters: playable N. Oxide, Penta 6/6/6, custom racers (`.ctrr`), stats viewer.
  - Playable Oxide is Vita-only there and embeds his menu model in the source. Here he is playable on PC with the model read from the player's disc ([EXTRAS.md](EXTRAS.md) has the other mods).
- Mods: reserves meter, custom cups, blue fire, OxideFix, cyan super turbo pads, powerslide bar.
- Other: rebindable controls, 4-player splitscreen, cheats menu, Discord presence, multilanguage text, PAL voices.

**Open issues:**
- Custom racers:
  - #48: a custom racer can't be selected (grid layout).
  - #36: picking one removes its template character from the race.
  - #43: the importer fails with a locked temp file.
- #35: mirror mode isn't applied to the Adventure hub.
- #34: Vita stick problems after AdHoc.
- #24: softlock after beating Oxide; High Octane-only, not reproduced here.
- #9: the all-super-turbopads cheat doesn't turn pads cyan. Handled here by Cyan Super Pads ([EXTRAS.md](EXTRAS.md)).
- #42: a Hot Air Skyway shortcut is harder than on emulator (physics).
- #39: Penta should use the PAL engine sound.
- #46: no VSync option.
- #44: 4:3 / 5:4 aspect.
- #41: 8-player splitscreen.
- #37: custom characters in Adventure.
- #31: analog U-turn.
- #29: random character pick.
- #22: texture filtering / resolution choice.
- #19: garage selector.
- #15: online lobbies.
- #13: delete ghosts.
- #33: macOS (CrossOver) needs GL 3.x.
- #16: Linux (the author says to use Proton).

**Open PRs:** #52 custom tracks (`.ctrt`) plus a Wayland GL fix, #50 web menu cleanup, #49 importer `--no-voice`, #28 vitasdk build fixes.

## dowlle/ctr-native-ap (our base)
**State:**
- Very active, with CI and signed releases (minisign manifest).
- The AP client lives in `ap/` (apclientpp + websocketpp + asio + OpenSSL).
- Hooks are `#ifdef CTR_AP` blocks in 58 `game/` files.
- 0.3.0 work happens on `dev/custom-pad-0.3.0` with large stacked PRs (#520–#531) and matching apworld PRs.

**Open bugs:**
- #499: with vanilla unlock requirements the stage-2 key count is ignored. Fixed in the apworld we ship (PR #154).
- #484: white noise in the hub after winning a race.
- #466: the progressive stats bar doesn't match the items collected. Fixed in client `main` (PR #469), which we have.
- #282: invisible podium racers. Both reports fixed here: Oxide in 1st wears his lose-dance model (his win-dance file on the disc is empty), and the cup standings no longer keep stale slots after a smaller field (`UI_CupStandings_UpdateCupRanks`, covered by `tools/test-cup-ranks.py`).
- #205: unguarded instance births. Fixed in #468; the issue is still open.
- #147: a large reconnect catch-up stalls the game thread. Not here: a 29,700-item catch-up keeps every frame under 23 ms (`ap_backlog.py`, [DEV-TOOLS.md](DEV-TOOLS.md#archipelago-server-load)).

**Refactors:** #305 (done), #306, #307, #309, #310.

**Feature requests (0.3.0 milestone and beyond):**
- Randomization and structure:
  - boss randomization #169
  - no-Trophy-Race structure #208
  - Crystalsanity #236
  - lap checks #344
  - win-all-cup-races check #246
  - relic-perfect checks for Cortex Vortex / custom tracks #453
  - per-pad requirements #463
  - Cortex Vortex placement #477
  - disable battle arenas #364
  - arcade-only arenas #259
- Wumpa goal and shop #249.
- Traps and filler: Tropy clock trap #455, buff traps #486, trap ideas #248, new filler #451, shortcut items #245.
- DeathLink as a weapon hit #483 and Turbo Grant every race #487 (both done on dev in #517).
- Music randomizer #485.
- Solo/offline mode #89.
- Signed updater #301.
- Custom tracks #133.
- Spoiler-free previews #459.
- Launch tracks from the tracker #375.
- Named box locations #331.
- Authored letter positions #186.
- Speedrunner relic times #120.

## CTR-tools/CTR-ModSDK
- Decomp work moved to ctr-native and CTR-in-C. ctr-native is 1263 commits ahead of it, and ModSDK has no code ctr-native lacks.
- Open PRs: #235 (enum/struct naming cleanup, old layout), #221 (warnings, stale), plus OnlineCTR server/client work (#251, #249, #151).
- Open issues are all about mods and tooling (VR mod, OxideFix, practice tool, LangMenu). None affect the decomp.
