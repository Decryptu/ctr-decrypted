#ifndef NATIVE_RELAUNCH_H
#define NATIVE_RELAUNCH_H

// Start a new copy of the running game, in the same working folder, with no
// arguments (a disc given with --disc is remembered in disc-path.txt anyway).
// The caller exits right after. Returns 1 when the new process started.
// Used by the Backups page to load a restored save; the updater starts the
// build it installed itself (ap_update_relaunch).
int NativeRelaunch_Self(void);

// macOS: the first thing main does. arm64 macOS loads every executable at a
// random address (the linker ignores -no_pie there), and savestates and
// replays hold raw pointers into the game's static memory, so a state would
// load only in the launch that made it. This replaces the process with itself
// launched without ASLR, at the linked address, as Windows and Linux builds
// always load (CMakeLists.txt, CTR_FIXED_BASE). Returns only in the process
// that is to run. A no-op elsewhere.
void NativeRelaunch_FixedBase(int argc, char **argv);

#endif // NATIVE_RELAUNCH_H
