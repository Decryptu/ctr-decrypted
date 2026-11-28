#ifndef NATIVE_RELAUNCH_H
#define NATIVE_RELAUNCH_H

// Start a new copy of the running game, in the same working folder, with no
// arguments (a disc given with --disc is remembered in disc-path.txt anyway).
// The caller exits right after. Returns 1 when the new process started.
// Used by the Backups page to load a restored save; the updater starts the
// build it installed itself (ap_update_relaunch).
int NativeRelaunch_Self(void);

// macOS: the first thing main does; a no-op elsewhere. arm64 macOS loads
// every executable at a random address (the linker ignores -no_pie there),
// and savestates and replays hold raw pointers into the game's static memory,
// so a state would load only in the launch that made it. When this launch was
// slid, the process is replaced by itself launched without ASLR, at the linked
// address, as Windows and Linux builds always load (CMakeLists.txt,
// CTR_FIXED_BASE). The app's launcher starts the game that way already
// (docs/MACOS.md). Returns only in the process that is to run.
void NativeRelaunch_Startup(int argc, char **argv);

#endif // NATIVE_RELAUNCH_H
