#ifndef NATIVE_MAC_INSTALL_H
#define NATIVE_MAC_INSTALL_H

// macOS: the app bundle installs the game folder (docs/MACOS.md). Built into
// the bundle's launcher (platform/macos/ctr_launcher.c), not the game.
//
// CTR Decrypted.app/Contents/Resources/game/ is a complete game folder, the
// release folder of the other systems. The launcher copies it into
// ~/Library/Application Support/CTR Decrypted/ (each variant's app into its
// own folder) when that folder holds no game yet or was last installed by a
// different build of the app; the game then runs from there.

#ifdef __APPLE__

// For the launcher at `launcher` (its resolved path): installs the bundle's
// game folder into ~/Library/Application Support/<folder>/ if needed and
// writes the installed game executable `game`'s path to `exe` (size `size`).
// Returns 1 then, 0 when the launcher is not in an app bundle that carries a
// game folder, and -1 when the install failed (the reason is in `error`, size
// `errorSize`).
int NativeMacInstall_Prepare(const char *launcher, const char *folder, const char *game, char *exe, int size, char *error,
                             int errorSize);

#endif

#endif // NATIVE_MAC_INSTALL_H
