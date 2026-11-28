#ifndef NATIVE_MAC_INSTALL_H
#define NATIVE_MAC_INSTALL_H

// macOS: the app bundle installs the game folder (docs/MACOS.md).
//
// CTR Decrypted.app carries a complete game folder: its own executable
// (Contents/MacOS/<exe>) and the rest of the release (Contents/Resources/game/).
// Launched from the bundle, the game copies that folder into
// ~/Library/Application Support/CTR Decrypted/ when the folder holds no game
// yet or was last installed by a different build of the app, then runs the
// copy there. From then on that folder is the game folder, laid out and
// updated exactly as on Windows and Linux.

#ifdef __APPLE__

// When this process is the executable inside an app bundle that carries a
// game folder: installs it if needed and writes the installed executable's
// path to `exe` (size `size`). Returns 1 then, 0 when not running from such a
// bundle, and -1 when the install failed (the reason is in `error`, size
// `errorSize`).
int NativeMacInstall_Prepare(char *exe, int size, char *error, int errorSize);

#endif

#endif // NATIVE_MAC_INSTALL_H
