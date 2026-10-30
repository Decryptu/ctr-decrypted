#ifndef NATIVE_DEV_H
#define NATIVE_DEV_H

#include <stdio.h>

// Developer switches for reproducing and inspecting bugs without a person at
// the controls. Every switch is an environment variable, read once at first
// use; with none set the game behaves exactly as shipped. Full list and usage:
// docs/DEV-TOOLS.md. tools/decrypted/devrun.ps1 wraps them.

// CTR_DEV_DEMO=<LevelID>: the title menu starts the attract demo on that track
// as soon as it settles (no Oxide intro, no idle wait), every time. -1 if unset.
int NativeDev_DemoTrack(void);

// CTR_DEV_FAST=1: emit VBlanks without waiting for wall-clock time, so the
// game runs as fast as the host can render. Game logic is unchanged: it still
// advances by the same VBlank counts.
int NativeDev_Fast(void);

// CTR_DEV_HIDDEN=1: create the window hidden. Rendering still happens (into
// the main render target), so CTR_DEV_SHOT keeps working.
int NativeDev_Hidden(void);

// CTR_DEV_NO_ITEMS=1: weapon crates give nothing and bots are handed no
// weapons, so races between runs differ only by driving (for comparing the AI
// at 30 and 60 FPS).
int NativeDev_NoItems(void);

// Called once per presented frame, before the swap. Handles
// CTR_DEV_SHOT_DIR / CTR_DEV_SHOT_EVERY / CTR_DEV_SHOT_FROM (frame capture)
// and CTR_DEV_EXIT_FRAME (quit after that many frames).
void NativeDev_EndFrame(void);

// CTR_DEV_DUMP_FRAME=N: returns an open file (in CTR_DEV_SHOT_DIR, or the
// working folder) while frame N is being drawn, else NULL. Draw code writes
// one line per polygon; tools/decrypted/pgxp_cracks.py reads it.
FILE *NativeDev_DumpFile(void);

// Primitive-range tags: code that emits primitives can label the arena range
// it wrote, and debug views can ask which label a primitive came from. Used by
// CTR_PGXP_DEBUG=4 to tint level geometry by LOD bucket. Reset every frame.
void NativeDev_TagPrims(const void *start, const void *end, int tag);
int NativeDev_PrimTag(const void *addr);

// Unattended races (platform/native_dev_race.c).
// CTR_DEV_RACE=<LevelID>: the title menu starts a one-player race on that
// track instead of the attract demo. CTR_DEV_MODE=tt makes it a Time Trial
// (default Arcade), CTR_DEV_LAPS sets the Arcade lap count. -1 if unset.
int NativeDev_RaceTrack(void);

// Sets up the mode for CTR_DEV_RACE (and loads CTR_DEV_GHOST_IN in Time
// Trial) and returns the level to load.
struct GameTracker;
int NativeDev_StartRace(struct GameTracker *gGT);

// CTR_DEV_AUTOPILOT=1: during a race, replaces player 1's pad with throttle
// held and analog steering along the AI's nav line. CTR_DEV_AUTOPILOT=2 stands
// still on the grid instead (for measuring hops on flat ground).
// CTR_DEV_HOP_MS=n adds a hop every n ms of race time. Returns 1 when it wrote
// the pad.
int NativeDev_AutopilotPad(unsigned short *buttons, unsigned char *leftX);

// Per presented frame: CTR_DEV_TRACE=<csv> rows for player 1 (and the human
// ghost), CTR_DEV_GHOST_OUT=<file> gets the recorded Time Trial ghost, and
// CTR_DEV_EXIT_AFTER_RACE=1 quits a second after the finish.
// CTR_DEV_RESULTS=<csv> gets every racer's rank, lap and distance to the
// finish at CTR_DEV_RESULTS_MS of race time (default 30000).
void NativeDev_RaceFrame(void);
void NativeDev_RaceShutdown(void);

#endif
