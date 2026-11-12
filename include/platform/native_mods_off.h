#ifndef NATIVE_MODS_OFF_H
#define NATIVE_MODS_OFF_H

// Installed mods the player turned off on the Mods screen. An off mod stays
// installed but the game leaves it out: an off track is not on the Arcade and
// Time Trial custom pages, an off racer is not on the character select. Seeds
// ignore the list, so a room that needs a track still finds it.
//
// One file per kind, one key per line: assets/tracks/mods-off.txt (a track's
// package UUID, or its pin when it has none) and assets/racers/mods-off.txt
// (a racer's id). Package builds only.

#ifdef __cplusplus
extern "C" {
#endif

enum NativeModsKind
{
	NATIVE_MODS_TRACK,
	NATIVE_MODS_RACER,
	NATIVE_MODS_KINDS
};

int NativeModsOff_Is(int kind, const char *key);

// Turn a mod off (off = 1) or on, and save the list. Returns 0 when the list
// could not be saved or is full.
int NativeModsOff_Set(int kind, const char *key, int off);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_MODS_OFF_H
