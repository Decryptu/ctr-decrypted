#ifndef NATIVE_RELAUNCH_H
#define NATIVE_RELAUNCH_H

// Start a new copy of the running game, in the same working folder, with no
// arguments (a disc given with --disc is remembered in disc-path.txt anyway).
// The caller exits right after. Returns 1 when the new process started.
// Used by the Backups page to load a restored save; the updater starts the
// build it installed itself (ap_update_relaunch).
int NativeRelaunch_Self(void);

#endif // NATIVE_RELAUNCH_H
