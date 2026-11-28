#ifndef NATIVE_RELAUNCH_H
#define NATIVE_RELAUNCH_H

// Start a new copy of the running game, in the same working folder, with no
// arguments (a disc given with --disc is remembered in disc-path.txt anyway).
// The caller exits right after. Returns 1 when the new process started.
// Used by the Backups page to load a restored save; the updater starts the
// build it installed itself (ap_update_relaunch).
int NativeRelaunch_Self(void);

// macOS: the first thing main does; a no-op elsewhere. Returns only in the
// process that is to run the game.
// - Launched from the app bundle, it installs the game folder if needed and
//   becomes the game there (native_mac_install.h, docs/MACOS.md).
// - arm64 macOS loads every executable at a random address (the linker ignores
//   -no_pie there), and savestates and replays hold raw pointers into the
//   game's static memory, so a state would load only in the launch that made
//   it. The game runs launched without ASLR, at the linked address, as Windows
//   and Linux builds always load (CMakeLists.txt, CTR_FIXED_BASE).
// Both are one exec: the process is replaced by the game executable that is
// to run, at its linked address.
void NativeRelaunch_Startup(int argc, char **argv);

#endif // NATIVE_RELAUNCH_H
