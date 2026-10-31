# 60 FPS mode

CTR Decrypted can run the game at 60 FPS. The feature comes from
[Crash Team Racing: High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane)
(branch `vita`), ported commit by commit. Its 60 FPS timing is in sync up to
High Octane `9054bab51`; no later High Octane commit changes it. High
Octane's AdHoc multiplayer, input-replay ghosts, leaderboard, alternative
Tiger Temple and its own config file were left out.

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
  New code that blinks, flashes or counts frames must do the same; counters
  already in milliseconds or on the vsync clock need nothing.
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
  (`INSTANCE_AnimHasDoubledFrames`). `GhostReplay_ThTick` plays back by race
  time, and doubles the stored animation frame at 60. The ghost format is
  unchanged: saved ghosts and the N. Tropy / N. Oxide ghosts play at both
  rates. (High Octane pinned Time Trial to 30 FPS instead.)
- **AI lap recordings** (`nav_record`, `ap/ap_navrec.c`). Samples, node
  timestamps and lap lengths are in 30 Hz ticks; at 60 FPS every other frame
  is skipped. Files keep the same format and limits.

## Changes beyond High Octane

- Archipelago timers (`ap/`, and the AP hooks in `223.c`, `AH_Map.c`,
  `AH_WarpPad.c`) are frame-rate aware: item feed, DeathLink cooldowns and
  grace, ceremony, traps, hub map flicker, warp pad tint and glow, the item
  application throttle, content-gate re-verify windows, the attract-demo hold
  behind the link prompt. Trap durations are in milliseconds.
- `222.c`: the "already unlocked" CTR letter lerps are doubled too.
- `BOTS.c`, AI driving, which High Octane left at 30 FPS rates:
  - On the ground a bot's vertical speed is the height it gained this frame,
    applied per 32 ms, so at 60 the delta is doubled (`FPS_DOUBLE`), or bots
    float off every downhill.
  - The ground height a bot follows keeps its fraction at 60: a 60 FPS frame
    of gravity drops a bot less than one whole-unit rounding step.
  - A bot that was on the ground last frame lands if it is within one 60 FPS
    frame of gravity drop above the nav line. That gives the 30 FPS take-off
    threshold; without it bots take off at every small crest, and since they
    only accelerate on the ground they fall behind.
  - The speed steps (friction, acceleration, slope pull, throttle zones,
    deceleration to a target) run at 30 Hz, on every other frame, like the
    other `use30HzStep` code. Progress cooldowns are doubled.
  - 30 FPS bot driving is retail: a 30 FPS demo traces identically. The bots
    also slide sideways off the grid onto their lines exactly as much at 60
    as at 30 (`driver_motion.py`, Hot Air Skyway).
- The player's kart and its camera, checked field by field against 30 FPS
  with `fps-drive.ps1` (below):
  - Steering: the spin rate rises toward the stick by half a step a frame
    (`VehPhysGeneral.c`, the decay already did), the normal-driving turn
    wobble has the drift wobble's frame counts, and the drift turn-angle lerp
    closes 1/16 then 1/15 of the gap (High Octane's even-frame step closed
    2/15, half again too fast, in uneven steps the camera showed).
  - `PhysLerpRot`: the camera spin speed it ramps toward is already per
    32 ms, so it is no longer halved on odd frames; only the ramp of 8 a
    frame is halved.
  - `VehCalc_FrameStep` replaces `rate * elapsedTimeMS >> 5` where a slow
    lerp moves by it each frame (`rotCurr.w`, `turnAngleCurr`): at 60 the
    shift drops a unit a frame and the lerp stalls (`1 * 16 >> 5` is 0; the
    camera stayed 15 units off centre after every drift), so the even frame
    adds what the odd one dropped.
  - The surface spring (`VehPhysForce_CollideDrivers`, `velocity +=
    penetration << 6` every frame) and the push off a quad the kart touches
    without standing on it (`COLL.c`) are halved: at full strength twice a
    30 FPS frame the kart bounced up bumpy slopes and stalled climbing them
    (Sewer Speedway's ramp after the second tunnel, from low speed).
  - `CAM_FollowDriver_Normal`: the camera's speed and roll filters are
    retuned so two frames do what one 30 FPS frame does (`CAM_SmoothFrame`),
    and the damage pitch step and height smoothing use 60 FPS frame counts.
- The checkered flag's "LOADING..." letters move every frame, each odd frame
  halfway between two 30 FPS positions, instead of jumping 60 pixels every
  other frame.

## Known differences from 30 FPS

- **Curved kickers.** The physics integrate in 16 ms steps, so on a curved
  kicker the kart follows the curve one step further before leaving it and
  launches a little higher (Dingo Canyon's kicker before the canyon: apex
  rise 24 at 30 FPS, 31 to 33 at 60). Gravity per second matches. Retail-exact
  physics would need a fixed 30 Hz simulation with interpolated rendering
  (ROADMAP, Maybe).
- **Input lands a frame sooner.** Both rates read the pad one frame before
  the physics act on it, which is 16 ms at 60 and 33 at 30, so a 60 FPS kart
  turns slightly earlier and scripted 30/60 runs part after a few seconds.
- **Braking** sheds about 4% more speed per second at 60 (Crash Cove's start
  straight: 8.7 against 8.4 units/ms). `speedApprox` reads about half an
  acceleration step lower at full throttle (it includes this frame's
  impulse); the velocity itself matches.
- **Displays that are not 60 Hz.** On a 60 Hz display every frame stays one
  refresh at 60 FPS and two at 30, with VSync Off, On or Adaptive. 60 does not
  divide 144, so on a fixed 144 Hz display frames alternate between two and
  three refreshes whatever the game does; only variable refresh shows 60 FPS
  evenly there.

## Checking

- `tools/decrypted/fps-parity.ps1` drives a Time Trial on each track at 30
  and 60 FPS with the autopilot and compares the traces
  ([DEV-TOOLS.md](DEV-TOOLS.md#unattended-races)). The autopilot steers in
  closed loop and never boosts, so its laps vary by a few percent even at one
  frame rate, and more where it falls into a gap a different number of times
  (Hot Air Skyway, Papu's Pyramid, Cortex Castle; it never finishes N. Gin
  Labs or Polar Pass). Read it for stalls and falls that happen at one rate
  only (a lap minutes longer), and use `fps-drive.ps1` for anything smaller.
  Jumps launched from the same spot land within a 30 FPS frame of airtime,
  curved kickers aside.
  With `-Ghosts`, a 30 FPS ghost raced at 60 stays as close to its recording
  run as at 30.
- `tools/decrypted/fps-drive.ps1` drives the same scripted input
  (`CTR_DEV_DRIVE`, timed in race milliseconds) at both rates and prints, per
  kart and camera field, how far 60 strays from 30. Converted code keeps every
  field together until the runs part (a wall, input landing a frame sooner);
  a field that strays alone and early is a frame-counted step still to
  convert. Steering, a hop into a drift and braking on Crash Cove's start
  match this way. With `CTR_DEV_WARP` it starts anywhere on a track: Hot Air
  Skyway's super-turbo ramp after the spiral, from the same run-up with the
  throttle held, lands on the left platform within a few units of the same
  spot at both rates. The right platform needs a steer right and a hop at
  the lip; the hops that reach it (about 50 ms either side of take-off) are
  the same at 30 and 60.
- A replay recorded at 60 FPS plays back frame-exact with `fps60=false`.
- `tools/decrypted/vsync-check.ps1` measures frame pacing for every VSync
  option with a visible window ([DEV-TOOLS.md](DEV-TOOLS.md#frame-timing)).
