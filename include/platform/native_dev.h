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

#endif
