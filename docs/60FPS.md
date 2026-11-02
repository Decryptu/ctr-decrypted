# 60 FPS mode

CTR Decrypted can run the game at 60 FPS. The feature is ported from
[Crash Team Racing: High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane)
(branch `vita`) and is in sync up to High Octane `9054bab51`. High Octane's
AdHoc multiplayer, input-replay ghosts, leaderboard, alternative Tiger Temple
and its own config file are left out.

## How it works

- `include/macros.h` defines `CTR_NATIVE_60FPS_ACTIVE` for every native build;
  it is true when `gNative60FpsEnabled` is set.
- In this mode the frame limiter waits one vblank per frame instead of two
  (`sdata->vsyncTillFlip = FPS_HALF(2)`). Physics already scale with the
  measured `elapsedTimeMS`.
- Anything that counts frames goes through `FPS_DOUBLE` (durations, frame
  thresholds) or `FPS_HALF` (per-frame steps, blink/flash phases from
  `gGT->timer` / `sdata->frameCounter`). `FPS_LEFTSHIFT` / `FPS_RIGHTSHIFT`
  adjust shift amounts. Some per-frame updates run only on odd frames.
  New code that blinks, flashes or counts frames must do the same; counters
  in milliseconds or on the vsync clock need nothing.
- Model animations play at double length with interpolated in-between frames
  (`INSTANCE_Use60FpsAnimation`, `RenderBucket_QueueExecute.c`). Particles are
  rescaled when they spawn (`MainFrame.c`) and spawn less often
  (`Particle_Init`). An emitter that must spawn every frame sets
  `gParticleInitUnthrottled` around its call.

## The option

- `config.ini`, `[Video & QoL]`: `fps60=true` (default) or `fps60=false`.
- In game: OPTIONS, Video page, **60 FPS (Next Load)**.
- The setting applies only at boot and when a level finishes loading
  (`MainMain_ApplyFrameRateMode` in `game/MAIN/MainMain.c`), because frame
  timers are scaled when they are set and switching mid-race would break them.
- Savestates and replay checkpoints store the mode with the timers it scaled
  and restore it (`native_checkpoint.c`). Input replays record the option and
  play back in that mode whatever `config.ini` says
  (`NativeReplayScheduler_GetFps60Option`).

## Recorded data stays in 30 FPS units

Every mode runs at 60, Time Trial included (High Octane pins Time Trial to
30). Data recorded frame by frame is kept in 30 FPS units, so recordings from
either mode are identical and work in both:

- **Time Trial ghosts.** A ghost tape holds a position sample every 8 frames
  of 30 FPS, timestamps in milliseconds, and animation frames. At 60 FPS
  `GhostTape_WriteMoves` samples every 16 frames, the turbo-pad cooldown is
  doubled, and animation frames are stored at their 30 FPS length
  (`INSTANCE_AnimHasDoubledFrames`). `GhostReplay_ThTick` plays back by race
  time and doubles the stored animation frame at 60. The ghost format is
  unchanged: saved ghosts and the N. Tropy / N. Oxide ghosts play at both rates.
- **AI lap recordings** (`nav_record`, `ap/ap_navrec.c`). Samples, node
  timestamps and lap lengths are in 30 Hz ticks; at 60 FPS every other frame
  is skipped. Same file format and limits.

## Changes beyond High Octane

- Archipelago timers (`ap/`, and the AP hooks in `223.c`, `AH_Map.c`,
  `AH_WarpPad.c`) are frame-rate aware: item feed, DeathLink cooldowns and
  grace, ceremony, traps, hub map flicker, warp pad tint and glow, the item
  application throttle, content-gate re-verify windows, the attract-demo hold
  behind the link prompt. Trap durations are in milliseconds.
- `222.c`: the "already unlocked" CTR letter lerps are doubled too.
- `BOTS.c`, AI driving:
  - On the ground a bot's vertical speed is the height gained this frame,
    applied per 32 ms, so at 60 the delta is doubled (`FPS_DOUBLE`), or bots
    float off every downhill.
  - The ground height a bot follows keeps its fraction at 60: a 60 FPS frame
    of gravity drops a bot less than one whole-unit rounding step.
  - A bot on the ground last frame lands if it is within one 60 FPS frame of
    gravity drop above the nav line. That matches the 30 FPS take-off
    threshold; without it bots take off at every small crest and, since they
    only accelerate on the ground, fall behind.
  - The speed steps (friction, acceleration, slope pull, throttle zones,
    deceleration to a target) run at 30 Hz, every other frame, like the other
    `use30HzStep` code. Progress cooldowns are doubled.
  - 30 FPS bot driving is retail: a 30 FPS demo traces identically. Bots slide
    off the grid onto their lines the same at both rates (`driver_motion.py`).
- The player's kart and camera, matched field by field against 30 FPS with
  `fps-drive.ps1`:
  - Steering: the spin rate rises toward the stick by half a step a frame
    (`VehPhysGeneral.c`, as the decay already did), the normal-driving turn
    wobble uses the drift wobble's frame counts, and the drift turn-angle lerp
    closes 1/16 then 1/15 of the gap.
  - `PhysLerpRot`: the camera spin speed it ramps toward is already per
    32 ms, so it is not halved on odd frames; only the ramp of 8 a frame is.
  - `VehCalc_FrameStep` replaces `rate * elapsedTimeMS >> 5` where a slow
    lerp moves by it each frame (`rotCurr.w`, `turnAngleCurr`). At 60 the
    shift drops a unit a frame (`1 * 16 >> 5` is 0) and the lerp stalls, so the
    even frame adds what the odd one dropped.
  - The surface spring (`VehPhysForce_CollideDrivers`, `velocity +=
    penetration << 6` every frame) and the push off a quad the kart touches
    without standing on it (`COLL.c`) are halved. At full strength twice per
    30 FPS frame the kart bounces up bumpy slopes and stalls climbing them.
  - `CAM_FollowDriver_Normal`: the camera's speed and roll filters are
    retuned so two frames do what one 30 FPS frame does (`CAM_SmoothFrame`),
    and the damage pitch step and height smoothing use 60 FPS frame counts.
  - Velocity conversions (`VehPhysForce_OnApplyForces`). Each frame retail
    rebuilds the velocity from speed and two angles, converts it to the kart's
    frame and back, then to speed and angles again. Each conversion shortens
    it (trig entries and `matrixMovingDir` rows are a hair under unit length,
    products truncate): about 0.13% of speed a frame, a drag nothing refills
    in the air, downhill, above top speed or braking. Twice as often, 60 FPS
    would lose speed faster. `VehCalc_KeepLossPerFrameTime` lets a frame
    shorter than 32 ms keep only its share of each loss.
  - Arcs (`COLL_MOVED_SetStepLead`). Retail moves the kart with its velocity
    at the end of a 32 ms frame, after that frame's gravity and friction, so
    its arcs trail exact motion by half a frame of those forces; a 16 ms frame
    trails by half as much, so jumps would fly higher and longer. The sweep
    moves a shorter frame with the difference added, taken from the velocity
    change across `VehPhysForce_OnGravity` (impulses such as jumps and turbo
    kicks arrive through speed and are not led). The lead loses its part into
    a surface the sweep hits, as the velocity does.
  - Both do nothing in a 32 ms frame: 30 FPS stays retail bit for bit.
- The checkered flag's "LOADING..." letters move every frame, each odd frame
  halfway between two 30 FPS positions.

## Known differences from 30 FPS

- **Curved kickers.** The kart touches a ramp once a frame and leaves with
  the direction of the last facet touched. At 60 that touch is nearer the lip,
  so on a ramp that curves up the kart leaves a little steeper. This is
  sampling, not a timing error; gravity on the arc and air speed match.
- **Input lands a frame sooner.** Both rates read the pad one frame before
  the physics act on it (16 ms at 60, 33 at 30), so a 60 FPS kart turns
  slightly earlier and scripted 30/60 runs part after a few seconds.
- **`speedApprox`** reads about half an acceleration step lower at full
  throttle (it includes this frame's impulse, half as large at 60); the
  velocity itself matches.
- **Displays that are not 60 Hz.** On 60 Hz every frame stays one refresh at
  60 FPS and two at 30, with any VSync mode. On a fixed 144 Hz display frames
  alternate between two and three refreshes whatever the game does; only
  variable refresh shows 60 FPS evenly there.

## Checking

- `tools/decrypted/fps-parity.ps1` drives a Time Trial on each track at 30
  and 60 FPS with the autopilot and compares the traces
  ([DEV-TOOLS.md](DEV-TOOLS.md#unattended-races)). The autopilot steers in
  closed loop and never boosts, so its laps vary by a few percent even at one
  rate, more where it falls into a gap a different number of times (Hot Air
  Skyway, Papu's Pyramid, Cortex Castle; it never finishes N. Gin Labs or
  Polar Pass). On Hot Air Skyway a respawn just before a gap can loop it for
  minutes at either rate. Read it for stalls and falls at one rate only; use
  `fps-drive.ps1` for anything smaller. Expected: matched jumps within 10 ms
  of airtime and a few units of apex; braking, downhill and air speed within
  about 1%. With `-Ghosts`, a 30 FPS ghost raced at 60 stays as close to its
  recording run as at 30.
- `tools/decrypted/fps-drive.ps1` drives the same scripted input
  (`CTR_DEV_DRIVE`, timed in race ms) at both rates and prints, per kart and
  camera field, how far 60 strays from 30. Converted code keeps every field
  together until the runs part (a wall, input landing a frame sooner); a field
  that strays alone and early is a frame-counted step still to convert. With
  `CTR_DEV_WARP` it starts anywhere on a track.
- A replay recorded at 60 FPS plays back frame-exact with `fps60=false`.
- `tools/decrypted/vsync-check.ps1` measures frame pacing for every VSync
  option with a visible window ([DEV-TOOLS.md](DEV-TOOLS.md#frame-timing)).
