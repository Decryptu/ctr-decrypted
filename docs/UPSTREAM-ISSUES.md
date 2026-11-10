# Upstream projects and their open issues

The projects CTR Decrypted draws from, as of 2026-10-30. Refresh it when you sync upstream or pick work from their trackers. Issue numbers are each project's own.

## How they relate

```
CTR-tools/CTR-ModSDK            decompilation (now only mods and OnlineCTR)
  └─ CTR-tools/ctr-native       native PC port; contains ModSDK's full history
       ├─ Rinnegatamante/Crash-Team-Racing-High-Octane   enhanced fork (Vita first)
       ├─ thecodingbob/ctr-native ("CTR Native Expanded") options fork
       └─ dowlle/ctr-native-ap  Archipelago client fork  ← our base
            └─ Decryptu/ctr-decrypted (this repo)
```

All are GPL-3.0. dowlle's `ctr.apworld` is MIT.

How far behind each one we are, and which High Octane commits we ported: `python tools/decrypted/upstream_status.py` ([DEVELOPMENT.md](DEVELOPMENT.md#syncing-upstream)).

## CTR-tools/ctr-native
**State:** accuracy work (`match(...)` commits restoring exact retail C); the maintainer turns down large feature PRs (SSAA/PGXP, physics, custom tracks were closed). No CI, no release since Beta 7.1 (2026-07-07). 32-bit, NTSC-U only. No config file, settings menu, widescreen or 60 FPS in master.

**Open issues:**
- #58: Mystery Caves left path crashes with 3–4 players. Fixed here (`226_00_DrawLevelOvr1P.c`).
- #14: wrong pre-race camera flyover. Fixed here (`may_alias` GTE loads in `include/psx/inline_c.h`, `tools/test-138-camera-flyin.c`).
- #45, #52: black screen and no scrapbook after the credits. Native scrapbook playback uses a bounded STR/XA stream and direct texture presentation ([NATIVE-PRESENTATION.md](NATIVE-PRESENTATION.md)); the Archipelago build sends a connected game to the hub instead.
- #61: unlock all characters including Oxide. Oxide is playable here; the others unlock as in retail.
- #43: Steam Deck. Handled here ([STEAM-DECK.md](STEAM-DECK.md)).
- #12: black screen and segfault on Manjaro after the audio stream opens.
- #54: PAL support, out of scope per #51. #25: Android. #20: Mac.

**Open PRs:** #62 FXAA plus ini physics multipliers (old base, not mergeable), #27 unlock all characters, #41 Android GLES, #49 winget/SDK presets.

## High Octane
**State:** Vita first, Windows releases since v1.3, latest v1.5 (2026-10-12). 709 files changed against the merge-base, about 78 with `__vita__` ifdefs. The Vita build can't be reproduced from the public repo (patched SDL3, private shader blobs).

Ported feature by feature, never merged: a trial merge conflicts in 92 files, and it carries Vita, web and leaderboard code we don't want.

**Features** (`tools/decrypted/highoctane.txt` records what we did with each commit):
- Graphics: 60 FPS, 960x544 internal resolution, MSAA (Vita), FXAA (PC), PGXP, post-processing filters, dithering toggle, true widescreen, borderless window.
- Modes: mirror, boss fights on any track, Super/Ultra Hard, relic races outside Adventure, reverse tracks and an alternative Tiger Temple, lap count 1–9, ghost replay tools (7 ghosts per track), online leaderboard.
- Characters: playable N. Oxide (Vita only, model embedded in the source; ours reads it from the disc), Penta 6/6/6, custom racers (`.ctrr`), stats viewer.
- Mods: reserves meter, custom cups, blue fire, OxideFix, cyan super turbo pads, powerslide bar.
- Other: rebindable controls, 4-player split screen, cheats menu, Discord presence, multilanguage text, PAL voices.

**Open issues:**
- Custom racers: #48 can't be selected (grid layout), #36 removes its template character from the race, #43 importer fails on a locked temp file. Our design avoids #48 and #36 ([CUSTOM_RACERS.md](CUSTOM_RACERS.md)).
- #9: the all-super-turbopads cheat doesn't turn pads cyan. Handled here by Cyan Super Pads ([EXTRAS.md](EXTRAS.md)).
- #44: 4:3 / 5:4 aspect. Handled here: 4:3 is the default, letterboxed on 5:4.
- #24: softlock after beating Oxide. High Octane only.
- #35 mirror mode not applied to the Adventure hub, #34 Vita stick after AdHoc, #42 a Hot Air Skyway shortcut harder than on emulator, #39 Penta should use the PAL engine sound, #46 no VSync option.
- Requests: #41 8-player split screen, #37 custom characters in Adventure, #31 analog U-turn, #29 random character, #22 texture filtering / resolution, #19 garage selector, #15 online lobbies, #13 delete ghosts, #33 macOS (CrossOver needs GL 3.x), #16 Linux (author says use Proton).

**Open PRs:** #52 custom tracks (`.ctrt`) plus a Wayland GL fix, #50 web menu cleanup, #49 importer `--no-voice`, #28 vitasdk build fixes.

## thecodingbob/ctr-native (CTR Native Expanded)
**State** (2026-11-02): active, tagged releases (v2.1.1, 2026-11-01), 308 commits not in ctr-native `master`. Ported feature by feature, never merged: it removed the `CTR_NATIVE` guards and the retail matching pipeline and renamed the executable and prefixes. What is new: `https://github.com/thecodingbob/ctr-native/compare/CTR-tools:master...thecodingbob:master`. Its PGXP, widescreen, render scale, FXAA, VSync, rebinding and dead zone overlap ours.

**Worth a look** (the ones we want are in [ROADMAP.md](../ROADMAP.md)):
- Rendering fixes already covered by ours ([LEVEL-VISIBILITY.md](LEVEL-VISIBILITY.md)): widescreen faces lost to the clip-record window (`209b71fe`), one draw-distance factor for every limit (`fb83d103`, `e86365e1`, `e0fedbf2`, `a81bf65b`; we keep the retail near reject for models, which their `GeomScreenHalf` relaxes), distant tires at full width (`26e520a2`).
- Modes: two-player co-op Adventure, Chaos (38 race effects, external trigger), Super/Ultra Hard in Arcade and Adventure, 3P/4P Arcade, eight racers in split screen, race tracks in Battle, Oxide Station in multiplayer.
- Options: per-stat kart multipliers (speed, turn, jump, gravity, reserves), weapon and mask tuning, mask modes, skip intro, podiums, boss cutscenes and mask hints, save anywhere in the hubs.

**Open issues:** #92 custom track loader, #89 language selector, #88 rare GPU crash during Chaos.

Not a source: kkv0n/Gasmox-Launcher, a C# launcher for OnlineCTR in DuckStation; it shares no code with the native port.

## dowlle/ctr-native-ap (our base)
**State:** very active, CI, signed releases (minisign manifest). The client is in `ap/` (apclientpp, websocketpp, asio, OpenSSL); hooks are `#ifdef CTR_AP` blocks in 58 `game/` files. The `main` branches of the client and apworld carry the 0.3.0 content-plan format.

**Open bugs:**
- #499: vanilla unlock requirements ignore the stage-2 key count. Fixed in our apworld (PR #154).
- #484: white noise in the hub after winning a race. Not reproduced here (`audio_noise.py`); an `F7` capture right after hearing it would show the cause.
- #466: progressive stats bar doesn't match items. Fixed in client `main` (PR #469), which we have.
- #282: invisible podium racers. Fixed here (Oxide in 1st uses his lose-dance model since his win-dance file is empty; `UI_CupStandings_UpdateCupRanks`, `tools/test-cup-ranks.py`).
- #205: unguarded instance births. Fixed in #468; issue still open.
- #147: a large reconnect catch-up stalls the game thread. Not here: 29,700 items keep every frame under 23 ms (`ap_backlog.py`, [DEV-TOOLS.md](DEV-TOOLS.md#archipelago-server-load)).

**Refactors:** #306, #307, #309, #310.

**Feature requests (0.3.0 and beyond):**
- Randomization and structure: boss randomization #169, no-Trophy-Race structure #208, Crystalsanity #236, lap checks #344, win-all-cup-races #246, relic-perfect checks for Cortex Vortex / custom tracks #453, per-pad requirements #463, Cortex Vortex placement #477, disable battle arenas #364, arcade-only arenas #259.
- Wumpa goal and shop #249.
- Traps and filler: Tropy clock trap #455, buff traps #486, trap ideas #248, new filler #451, shortcut items #245.
- Music randomizer #485, solo/offline mode #89, signed updater #301, custom tracks #133, spoiler-free previews #459, launch tracks from the tracker #375, named box locations #331, authored letter positions #186, speedrunner relic times #120.

## CTR-tools/CTR-ModSDK
- Decomp work moved to ctr-native and CTR-in-C. ModSDK has no code ctr-native lacks.
- Open PRs: #235 enum/struct naming (old layout), #221 warnings (stale), OnlineCTR server/client work (#251, #249, #151).
- Open issues are about mods and tooling (VR mod, OxideFix, practice tool, LangMenu); none affect the decomp.
