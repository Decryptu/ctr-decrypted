# Ghost Replay Tools

Ported from [Crash Team Racing - High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane).

## What is included

- **Per-track ghost limit**: up to 7 saved Time Trial ghosts per retail track (instead of 7 total across all tracks).
- **Input recording (`.rpl`)**: when you save a Classic Time Trial ghost to the memory card (`memcards/slot0/`), a companion input file (`replays/slot0/<ghost>.rpl`) is written alongside it with per-frame buttons, analog sticks, frame timing and 30/60 FPS mode. Ghost cards are labelled `MODERN` when a companion `.rpl` is present and `LEGACY` otherwise. Overwriting a ghost removes its old `.rpl`.
- **Ghost Replay**: from the title screen, choose **TIME TRIAL → GHOST REPLAY**, pick a track that has a `MODERN` ghost, and select the ghost. Playback switches automatically to the frame rate (30 or 60 FPS) the ghost was recorded in and replays player 1 deterministically without touching high scores, unlocks or ghost saves.
- **Playback controls** (shown on screen during playback):
  - **Cross**: Play / Pause replay.
  - **Square** (while paused): Advance one simulation frame.
  - **L1 / R1**: Adjust playback speed (`0.25X`, `0.50X`, `1.00X`, `1.50X`, `2.00X`). Wall-clock pacing scales without skipping simulation frames.
  - **Start**: Pause menu (`RESUME`, `RESTART`, `QUIT`, `OPTIONS`).
- **DualShock input viewer**: a live controller overlay in the bottom-left corner highlights held buttons and tracks both analog sticks during replay. The artwork is embedded in the executable (`platform/native_dualshock_overlay.c`).
- **Challenge Ghost**: at the end of a Ghost Replay, choose **CHALLENGE GHOST** to jump straight into a live Time Trial racing against that ghost.
- **Time Trial Personal Bests HUD**: during Time Trials, the top-left HUD shows `PB 3L` (best 3-lap time) and `PB L` (best lap) below the lap splits, and colours each lap against `PB L` (yellow if on or ahead of PB, light green within 1 s, orange within 2 s, red beyond 2 s).

## Implementation notes

- `game/NativeGhostInput.c`: `.rpl` recording, validation, deterministic playback, replay pause/step/speed state, and overlay input queries.
- `platform/native_memcard.c`: `replays/slot0/<ghost>.rpl` atomic read/write/remove and raised directory scan limit (`256`).
- `game/RefreshCard.c` and `game/SelectProfile.c`: per-track ghost profile index and `MODERN`/`LEGACY` ghost selection.
- `platform/native_platform.c`: `Platform_SetVBlankPacingScale` for replay speed control without altering VBlank counts.
- `platform/native_renderer.c` and `platform/native_dualshock_overlay.c`: embedded DualShock overlay drawn before present capture.
