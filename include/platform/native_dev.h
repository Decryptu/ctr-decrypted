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

// CTR_DEV_BOT_SPEED=<percent>: caps the bots' top speed at that share of
// retail (default 100, no change), so the autopilot can win a race and reach
// the podium.
int NativeDev_BotSpeedPercent(void);

// CTR_DEV_AUDIO=<file>: the mixed game audio as raw 16-bit stereo PCM at
// 44100 Hz. In CTR_DEV_FAST runs audio is rendered on the VBlank clock, one
// VBlank (735 frames) per call; in real time it is what SDL's audio thread
// mixed. tools/decrypted/audio_noise.py reads it.
void NativeDev_AudioFrames(const short *frames, int frameCount);

// Called once per presented frame, before the swap. Handles
// CTR_DEV_SHOT_DIR / CTR_DEV_SHOT_EVERY / CTR_DEV_SHOT_FROM (frame capture)
// and CTR_DEV_EXIT_FRAME (quit after that many frames). With
// CTR_DEV_SHOT_PRESENTED=1 the capture is the presented image (after the
// screen filter) rather than the render target.
void NativeDev_EndFrame(void);
// The same for a frame presented straight from VRAM (boot splash, Scrapbook,
// other movies): captures that VRAM rectangle instead of the render target.
void NativeDev_EndFrameFromVRAM(int x, int y, int w, int h);

// CTR_DEV_DUMP_FRAME=N: returns an open file (in CTR_DEV_SHOT_DIR, or the
// working folder) while frame N is being drawn, else NULL. Draw code writes
// one line per polygon; tools/decrypted/pgxp_cracks.py reads it.
FILE *NativeDev_DumpFile(void);

// F7 in a running game: the next frame's polygon dump (as CTR_DEV_DUMP_FRAME),
// its image and its VRAM go to debug/captures, for bugs only a person can
// reach.
void NativeDev_RequestCapture(void);

// CTR_DEV_STACK_POISON=<byte>: at the top of each main-loop frame, fill the
// stack the frame is about to use with that byte. Two runs with different bytes play the
// same unless game code reads a local it never wrote.
void NativeDev_PoisonStack(void);

// Primitive-range tags: code that emits primitives can label the arena range
// it wrote, and debug views can ask which label a primitive came from. Used by
// CTR_PGXP_DEBUG=4 to tint level geometry by LOD bucket. Reset every frame.
void NativeDev_TagPrims(const void *start, const void *end, int tag);
int NativeDev_PrimTag(const void *addr);

// Unattended races (platform/native_dev_race.c).
// CTR_DEV_RACE=<LevelID>: the title menu starts a one-player race on that
// track instead of the attract demo. CTR_DEV_MODE=tt makes it a Time Trial,
// CTR_DEV_MODE=adv an Adventure trophy race (default Arcade); CTR_DEV_LAPS
// sets the lap count. -1 if unset. CTR_DEV_MODE=cup runs the Gem Cups listed
// in CTR_DEV_CUPS back to back (no CTR_DEV_RACE needed), and CTR_DEV_PODIUM
// gets each cup's podium.
int NativeDev_RaceTrack(void);
// CTR_DEV_PLAYERS=2..4 makes CTR_DEV_RACE a VS race for that many players.
// CTR_DEV_CHEATS=<bits> turns on gameMode2 cheats for it (CHEAT_* values).
int NativeDev_RacePlayers(void);

// CTR_DEV_CHARACTER_SELECT=1: the title menu opens the Arcade character
// select for CTR_DEV_PLAYERS players. CTR_DEV_CHARACTER puts player 1's
// cursor on that racer; CTR_DEV_UNLOCKS=1 unlocks the retail secret racers
// first, =2 N. Oxide as well.
// Returns 1 when set (called every idle title frame).
int NativeDev_CharacterSelect(void);

// CTR_DEV_ENDING=1: boot into Oxide's true ending, which runs into the
// credits, with every sapphire relic so the credits end in the Scrapbook.
// CTR_DEV_ENDING=2: the same without the relics, so they end in the hub.
// Returns the level to boot, or -1.
int NativeDev_BootLevel(void);

// Sets up the mode for CTR_DEV_RACE (and loads CTR_DEV_GHOST_IN in Time
// Trial) and returns the level to load.
struct GameTracker;
int NativeDev_StartRace(struct GameTracker *gGT);

// CTR_DEV_AUTOPILOT=1: during a race, replaces player 1's pad with throttle
// held and analog steering along the AI's nav line. CTR_DEV_AUTOPILOT=2 stands
// still on the grid instead (for measuring hops on flat ground).
// CTR_DEV_HOP_MS=n adds a hop every n ms of race time. Returns 1 when it wrote
// the pad. With CTR_DEV_PLAYERS=2..4 (a VS race) it drives every player, each
// on its own AI lane.
int NativeDev_AutopilotPad(int player, unsigned short *buttons, unsigned char *leftX);

// Per presented frame: CTR_DEV_TRACE=<csv> rows for player 1 (and the human
// ghost), CTR_DEV_GHOST_OUT=<file> gets the recorded Time Trial ghost, and
// CTR_DEV_EXIT_AFTER_RACE=1 quits a second after the finish.
// CTR_DEV_RESULTS=<csv> gets every racer's rank, lap and distance to the
// finish at CTR_DEV_RESULTS_MS of race time (default 30000).
// CTR_DEV_DRIVER_DUMP=<file> gets every Driver struct, every frame of a race
// (tools/decrypted/driver_diff.py compares two runs).
// CTR_DEV_QUAD_DUMP=<csv>: the level's quadblocks as it loads (LevInstDef_UnPack).
struct mesh_info;
void NativeDev_QuadDump(const struct mesh_info *mesh);

void NativeDev_RaceFrame(void);
void NativeDev_RaceShutdown(void);

#endif
