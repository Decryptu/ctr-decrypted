#ifndef NATIVE_DISC_SEARCH_H
#define NATIVE_DISC_SEARCH_H

#include <stddef.h>

// First-launch disc search. When the game has no disc yet, it looks for one in
// the places a player usually keeps a download before the picker opens: the
// game folder and the folder above it, then Downloads, Desktop, Documents and
// Games in the user's home folder (and OneDrive's Desktop and Documents). The
// startup sequence asks the player before using what it found
// (NativeDiscResolution_Run in native_disc_resolution.h).
//
// The walk is bounded so a large Downloads folder cannot stall startup: three
// folder levels below each root and NATIVE_DISC_SEARCH_MAX_ENTRIES directory
// entries in all. Only .bin files of at least NATIVE_DISC_SEARCH_MIN_BYTES are
// offered to the validator, the files of a folder before its subfolders.
// Folders whose names start with '.' or '$', AppData and node_modules are skipped.

#define NATIVE_DISC_SEARCH_DEPTH 3
#define NATIVE_DISC_SEARCH_MAX_ENTRIES 20000
#define NATIVE_DISC_SEARCH_MIN_BYTES (100L * 1024L * 1024L)
#define NATIVE_DISC_SEARCH_ROOTS_MAX 10
#define NATIVE_DISC_SEARCH_PATH_MAX 1024

// 1 when path is the disc this search is after.
typedef int (*NativeDiscSearchAccept)(void *ctx, const char *path);

// Walk the roots in order and return 1 with the first accepted .bin in out.
// Missing or unreadable roots are skipped.
int NativeDiscSearch_Walk(const char *const *roots, int rootCount, NativeDiscSearchAccept accept, void *ctx,
                          char *out, size_t outSize);

// The production roots for a game folder and a home folder (either may be NULL
// or empty). Returns how many were written.
int NativeDiscSearch_Roots(const char *gameDir, const char *homeDir, char roots[][NATIVE_DISC_SEARCH_PATH_MAX], int max);

// Production use: NativeStartup_SearchDisc (platform/native_startup.c) passes
// the game and home folders and the real disc validator.

#endif // NATIVE_DISC_SEARCH_H
