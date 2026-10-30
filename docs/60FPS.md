# 60 FPS mode

CTR Decrypted can run the game at 60 FPS. The feature is ported from
[Crash Team Racing: High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane)
(branch `vita`). Both projects come from CTR-tools/ctr-native, so most of
High Octane's changes carried over with the timing logic unchanged.

## How it works

- `include/macros.h` defines `CTR_NATIVE_60FPS_ACTIVE` for every native build.
  It is true when `gNative60FpsEnabled` is set.
- When the mode is active the frame limiter asks for one vblank per frame
  instead of two (`sdata->vsyncTillFlip = FPS_HALF(2)`). The physics already
  scale with the measured `elapsedTimeMS`.
- Anything that counts frames goes through `FPS_DOUBLE` (durations, frame
  thresholds) or `FPS_HALF` (per-frame steps, blink/flash phases taken from
  `gGT->timer` / `sdata->frameCounter`). `FPS_LEFTSHIFT` / `FPS_RIGHTSHIFT`
  adjust shift amounts. Some per-frame updates run only on odd frames instead.
- Model animations play back at double length with interpolated in-between
  frames (`INSTANCE_Use60FpsAnimation`, `RenderBucket_QueueExecute.c`).
  Particles are rescaled when they spawn (`MainFrame.c`) and spawn less often
  (`Particle_Init`).

## The option

- `config.ini`, `[Video & QoL]` section: `fps60=true` (the default) or
  `fps60=false`.
- In-game: OPTIONS, Video page, **60 FPS (Next Load)**.
- The setting is applied only at boot and when a level finishes loading
  (`MainMain_ApplyFrameRateMode` in `game/MAIN/MainMain.c`). Frame timers are
  scaled when they are set, so switching mid-race would break them. A change
  made in the menu applies from the next track or menu load, which is why the
  label says "Next Load".
- Savestates and replay checkpoints store the mode with the timers it scaled
  and restore it (`native_checkpoint.c`). Input replays record the option and
  play back in that mode whatever `config.ini` says
  (`NativeReplayScheduler_GetFps60Option`).

## Recorded data stays in 30 FPS units

Every mode runs at 60, Time Trial included. Data recorded frame by frame is
kept in 30 FPS units, so recordings made in either mode are the same and
work in both:

- **Time Trial ghosts.** A ghost tape holds a position sample every 8 frames
  of 30 FPS, timestamps in milliseconds, and animation frames. At 60 FPS
  `GhostTape_WriteMoves` samples every 16 frames, the turbo-pad cooldown is
  doubled, and animation frames are stored at their 30 FPS length
  (`INSTANCE_AnimHasDoubledFrames`). `GhostReplay_ThTick` already plays back
  by race time, and doubles the stored animation frame at 60. The ghost
  format is unchanged: saved ghosts and the N. Tropy / N. Oxide ghosts play
  at both rates, and a ghost recorded at 60 plays at 30. High Octane instead
  pinned Time Trial to 30 FPS (`gNativeForce30Fps`), which is removed.
- **AI lap recordings** (`nav_record`, `ap/ap_navrec.c`). Samples, node
  timestamps and lap lengths are in 30 Hz ticks; at 60 FPS every other frame
  is skipped. Files keep the same format and limits.

## Ported High Octane commits

Cherry-picked with `-x`, with conflicts resolved against this fork's older
decomp names and its `#ifdef CTR_AP` hooks (all AP blocks kept):

706f1819b (core), ba707d99f, 7aadd44f7, a118bbe97, 802926a4f, 50c32f030,
d1d6a4dd6, 13ab86989, 08c001896, 1342cd2eb, 432e51382, 5aaa2fddd, 186d99460,
0de42c522, aadd9a305, d7453b41f, 9b1a0c0e8, 109b364ec, d404c53c9, fd92fcce8,
9054bab51.

Partial picks:

- f028f2803: only the rain particle-count change in `RenderWeather.c`.
- 8c160d1c9: only the high score arrow flash in `MM_HighScore.c`.
- 7f0d80466: only the indentation hunks for 60 FPS code (`222.c`, `225.c`).
- f2f93ffe7 (Windows support): not cherry-picked. Its only 60 FPS change was
  removing the `__vita__` gate from the macros, which is done here directly.

No High Octane commit after 9054bab51 changes the 60 FPS timing. The v1.4
release note "adjusted physics at 60 FPS" refers to 9054bab51 and the physics
parts of 706f1819b.

Left out (High Octane-only features): AdHoc multiplayer, input-replay ghosts
and their per-ghost FPS metadata (`NativeGhostInput.c`, the `RefreshCard` /
`SelectProfile` ghost FPS functions, `gNativeGhostReplayFpsOverride`), the
leaderboard, the alternative Tiger Temple, and High Octane's own config file and
options menu entry.

## Fork-specific changes

- Archipelago timers (`ap/`, and the AP hooks in `223.c`, `AH_Map.c`,
  `AH_WarpPad.c`): item feed lifetime, stagger and priming; DeathLink
  cooldowns and receive grace; ceremony cycling and flash; trap item roll
  timer; hub map flicker; warp pad tint and glow cycling; character swap arrow
  flash; the item application throttle; content-gate re-verify windows; the
  attract-demo hold behind the link prompt. Trap durations were already in
  milliseconds.
- Options menu: held-direction repeat and text cursor blink.
- `AH_WarpPad.c`: AP-only warp frame checks (`framesWarping` 60/61) doubled
  like the vanilla ones.
- `222.c`: the "already unlocked" CTR letter lerps are doubled too (High
  Octane left those two at 30 FPS lengths).
- `BOTS.c`, AI driving, which High Octane left at 30 FPS rates:
  - On the ground a bot's vertical speed is the height it gained this frame,
    and that speed is applied per 32 ms. At 60 the delta is doubled
    (`FPS_DOUBLE`), or bots float off every downhill.
  - The ground height a bot follows is rounded to whole units. At 60 the
    fraction is kept: a 60 FPS frame of gravity drops a bot only a quarter
    as far as a 30 FPS frame, less than one rounding step, so bots kept
    leaving the ground.
  - A bot accelerates only while on the ground, so those two together made
    bots airborne on 29% of frames (6% at 30) and slower. Distance driven in
    the first 40 s of the Crash Cove demo by the same AI driver: 2.9% short
    of 30 FPS before, 1.4% after. What is left comes from frames where a
    bot crosses a kink in the nav line (9% airborne against 6%), mostly at
    the start.
  - The speed steps (friction, acceleration, slope pull, throttle zones,
    deceleration to a target) run at 30 Hz, on every other frame, like the
    other `use30HzStep` code. They used to run every frame, which doubled
    the start acceleration. Progress cooldowns are doubled.
  - 30 FPS is unchanged: a demo traced before and after matches on every
    frame.

## Blink and flash audit

`game/` and `ap/` were searched for blinks, flashes, pulses and colour
cycles taken from frame counters (`gGT->timer`, `sdata->frameCounter`,
`framesInThisLEV`, menu and HUD countdowns, `& mask`, `% n`, `/ n`, sine
phases). Most were already fixed by the High Octane picks or the fork
changes above. These were not, and are fixed here:

- `SelectProfile.c`: memory card message header flash (red/normal), the
  SAVE COMPLETED flash and how long it stays up, and the spin of the 3D
  load/save icons (odd spin rates add the remainder on alternate frames).
- `SubmitName.c`: the name entry blink (letters, SAVE/CANCEL, cursor) now
  steps off `sdata->frameCounter`. It used `gGT->timer`, which stops while
  `PAUSE_ALL` is set; on an odd value it let the blink step every frame.
- `MainFreeze.c`: pause menu option arrows, the analog calibration wheel
  and neGcon wave.
- `MM_Battle.c`: battle setup "THERE MUST BE AT LEAST ONE WEAPON" colour.
- `MM_Characters.c`: colour pulse on players who have not picked yet.
- `CS_Thread.c`: cutscene alpha flicker (cutscene flag 0x2, used on the
  podium and other scripted scenes). Each state is held for two frames.
- `UI_Weapon.c`: weapon icon flicker while an item is blocked.
- `UI_DrawNum.c`, `UI_Reward.c` (AP): relic tier cycle on the HUD relic
  count and icon, one tier per second like the warp pad glow.
- `Display.c`: clock weapon blur wobble.
- `FLARE.c`: the 20-frame lens flare after a mask respawn.
- `MainFrame.c`: the VS/battle FINISHED/LOSER and quip countdown
  (`timerEndOfRaceVS`) counts 30 FPS frames.

Checked and left alone:

- Counters already in milliseconds or on the vsync clock: wrong way text
  (`elapsedEventTime`), tire hazard wobble, confetti
  (`frameTimer_Confetti`), traffic lights.
- Not visible as a blink: rumble pulse frequency (`GAMEPAD.c`), decal LOD
  alternation (`DecalMP.c`), particle mirroring on odd frames
  (`RB_FlameJet.c`, `VehEmitter.c`).
- Short input or audio delays counted in frames: hint screen input
  cooldown (`AH_HintMenu.c`, 30 frames), wrong way voice line delay
  (`framesDrivingSameDirection`, 30 frames). They halve at 60 but nothing
  blinks.
- `221.c` CTR token text colour checks `gGT->timer == 0`, which is almost
  never true at either frame rate.

## Automated checks

`tools/decrypted/fps-parity.ps1` drives a Time Trial on each track at 30 and
at 60 FPS with the autopilot (the AI's line through the normal kart physics)
and compares the traces ([DEV-TOOLS.md](DEV-TOOLS.md#unattended-races)).
Results on 2026-10-30:

| Track | Finish 30 / 60 FPS | Matched jumps: count, mean airtime difference | Ghost drift, mean units: raced at 30 / at 60 |
|---|---|---|---|
| Dingo Canyon | 30: 159.2 s / 60: did not finish (wedged on an inside wall) | | 44.3 / 44.3 |
| Dragon Mines | 141.02 / 138.18 s (-2.0%) | 12, +8 ms | 49.4 / 49.4 |
| Blizzard Bluff | 121.66 / 121.78 s (+0.1%) | 32, +12 ms | 62.1 / 62.1 |
| Crash Cove | 107.90 / 107.60 s (-0.3%) | 6, +29 ms | 53.0 / 52.7 |
| Tiger Temple | 137.92 / 140.78 s (+2.1%) | 32, +13 ms | 54.7 / 54.7 |
| Papu's Pyramid | 190.05 / 193.47 s (+1.8%) | 26, +13 ms | 46.0 / 45.9 |
| Roo's Tubes | 115.26 / 114.21 s (-0.9%) | 9, +0 ms | 53.1 / 52.7 |
| Hot Air Skyway | 330.43 / 518.94 s (+57%, see below) | 43, -182 ms | 62.6 / 62.0 |
| Sewer Speedway | 173.73 / 173.84 s (+0.1%) | 17, +16 ms | 51.9 / 51.9 |
| Mystery Caves | 154.98 / 155.58 s (+0.4%) | 29, -25 ms | 60.7 / 60.7 |
| Cortex Castle | 30: 243.8 s / 60: did not finish (pinned on a turbo pad, ROADMAP) | | 66.4 / 65.9 |
| N. Gin Labs | did not finish (falls into the same pit) | | |
| Polar Pass | did not finish (falls into the same pit) | | |
| Oxide Station | 279.97 / 279.21 s (-0.3%) | 22, +17 ms | 62.6 / 62.1 |
| Coco Park | 111.94 / 111.97 s (+0.0%) | none | 60.6 / 60.6 |
| Tiny Arena | 291.84 / 294.01 s (+0.7%) | 104, +7 ms | 48.0 / 48.0 |
| Slide Coliseum | 178.88 / 178.11 s (-0.4%) | none | 48.8 / 48.5 |
| Turbo Track | 160.54 / 160.96 s (+0.3%) | none | 63.9 / 63.9 |

Hot Air Skyway: the autopilot never boosts, so it misses the first gap at
both rates (takeoff speed 13133 and 12920, same distance short). At 60 each
respawn repeats the same failed attempt exactly, eleven times; at 30 the
retries differ and get across sooner. The jumps themselves match.

- **Physics scale with frame time.** Finish times agree within about a
  percent; the autopilot steers once per frame, so it never drives exactly
  the same line twice. A standing hop on the Crash Cove grid: apex rise 51 at
  30 FPS, 50 at 60; airtime 320 ms against 304 ms.
- **Jumps.** Jumps launched from the same spot land within a 30 FPS frame of
  airtime on average. The exception is a curved kicker: at 60 FPS the kart
  follows the curve one 16 ms step further before leaving it, so it leaves
  with more upward speed. The worst one found is the Dingo Canyon kicker
  before the canyon (apex rise 24 at 30 FPS, 31 to 33 at 60; airtime 768 ms
  against 800 ms). That is the integration step, not a missing scale factor:
  gravity per second matches. Retail-exact physics at 60 would need the
  simulation at a fixed 30 Hz with interpolated rendering, which is a
  different design from High Octane's (ROADMAP, Maybe).
- **Ghosts.** A ghost recorded at 30 FPS and raced at 60 stays as close to
  the run that recorded it as the same ghost raced at 30, on every track
  (table above). A 60 FPS ghost raced at 30 on Crash Cove: 26.9 units
  against 26.5 at 60.
- **Input replays.** A replay recorded at 60 FPS plays back frame-exact with
  `fps60=false` in `config.ini` (0 of 1474 traced frames differ).
- **AI lap recordings.** The same Crash Cove laps record as 1116 and 1125
  ticks at 30 FPS, 1113 and 1114 at 60.
- **AI.** `CTR_DEV_RESULTS` gives every racer's distance to the finish at a
  set race time. Weapons and the random start boost make single races noisy
  (several thousand units per bot), so the AI was checked on the demo's
  driver 0 instead (see `BOTS.c` above).
- **Frame pacing.** Measured hidden with `--perf`: 60 FPS frames take a median
  16.68 ms (5th to 95th percentile 16.49 to 16.85 ms, worst 17.25 ms).
  How that meets the display with VSync On or Adaptive cannot be measured
  in a hidden window, because the driver does not block the swap there.

## Left to check by playing

- Drifting and turbo feel, and the Hot Air Skyway CTR challenge "R" (fixed
  upstream by 9054bab51). The autopilot never drifts.
- VSync On and Adaptive at 60 FPS on a 60 Hz and on a high-refresh monitor.
  Run with `--perf` and look at `frames_over_1_5x_budget` in
  `debug/perf/perf-latest/summary.txt`: each one is a frame that stayed on
  screen at least one refresh too long.
- Adventure hub: warp pads (vanilla, AP stage-2 gates, Cortex Vortex, custom
  cups), doors and keys, save points, garages, the pause menu, and podiums.
- Results screens for every mode (arcade, cup standings, relic, CTR token,
  battle) and the AP ceremony text on relic results.
- Archipelago: feed timing, DeathLink send/receive, traps, map flicker.
- Changing the option in the menu, then entering a race.
- Blinks and flashes: name entry, memory card and save screens, pause
  arrows, hub map icons, results text, battle setup, podium. All are
  expected to run at their 30 FPS speed now (see the audit above).
