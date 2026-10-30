# 60 FPS mode

CTR Decrypted can run the game at 60 FPS. The feature is ported from
[Crash Team Racing: High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane)
(branch `vita`). Both projects come from CTR-tools/ctr-native, so most of
High Octane's changes carried over with the timing logic unchanged.

## How it works

- `include/macros.h` defines `CTR_NATIVE_60FPS_ACTIVE` for every native build.
  It is true when `gNative60FpsEnabled` is set and `gNativeForce30Fps` is not.
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

## What forces 30 FPS

`gNativeForce30Fps` (kept from High Octane) is set for **Time Trial races**.
GhostTape records one sample per frame into a buffer sized for a 30 FPS lap,
and ghost playback steps per frame. Running Time Trial at 30 keeps new ghosts,
saved ghosts and the N. Tropy / N. Oxide ghosts compatible. The Time Trial
menus still run at 60.

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

## Known risks and untested areas

None of this has been tested in game yet. Things to check:

- Every race type at 60, especially jump heights, drifting/turbo feel, and
  the Hot Air Skyway CTR challenge "R" (fixed upstream by 9054bab51).
- Adventure hub: warp pads (vanilla, AP stage-2 gates, Cortex Vortex, custom
  cups), doors and keys, save points, garages, the pause menu, and podiums.
- Results screens for every mode (arcade, cup standings, relic, CTR token,
  battle) and the AP ceremony text on relic results.
- Archipelago: feed timing, DeathLink send/receive, traps, map flicker.
- Time Trial: confirm it drops to 30 and that ghosts record and play back
  correctly, then that the next menu is back at 60.
- Changing the option in the menu, then entering a race.
- Blinks and flashes: name entry, memory card and save screens, pause
  arrows, hub map icons, results text, battle setup, podium. All are
  expected to run at their 30 FPS speed now (see the audit above).
- The internal replay scheduler (`CTR_INTERNAL`) and the AI lap recorder
  (`nav_record`) count frames; recordings made at one frame rate will not
  match the other. Record and play them back with the same `fps60` setting.
- The `CTR_NATIVE` macros now read a global on every use. That is cheap, but
  it was not profiled.
