#ifndef NATIVE_RACER_LIBRARY_H
#define NATIVE_RACER_LIBRARY_H

// Installed custom racers: one folder per racer under assets/racers/, holding
// racer.json and the files it lists (docs/CUSTOM_RACERS.md). Scanning checks
// every manifest and every file's size and SHA-256; a racer that fails is
// left out and logged.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_RACER_MAX 64
#define NATIVE_RACER_NAME_BYTES 32
#define NATIVE_RACER_SHORT_NAME_BYTES 16
#define NATIVE_RACER_PATH_BYTES 512
// The portrait file: "CDP1", u16 width 44, u16 height 26, 16 palette words,
// then 11 words of 4-bit texels per row.
#define NATIVE_RACER_PORTRAIT_W 44
#define NATIVE_RACER_PORTRAIT_H 26
#define NATIVE_RACER_PORTRAIT_ROW_WORDS (NATIVE_RACER_PORTRAIT_W / 4)
#define NATIVE_RACER_PORTRAIT_BYTES (4 + 4 + 16 * 2 + NATIVE_RACER_PORTRAIT_ROW_WORDS * NATIVE_RACER_PORTRAIT_H * 2)
#define NATIVE_RACER_MODEL_MAX_BYTES (512 * 1024)

struct NativeRacerEntry
{
	char id[37];
	char name[NATIVE_RACER_NAME_BYTES];
	char shortName[NATIVE_RACER_SHORT_NAME_BYTES];
	char version[16];
	char authors[128]; // comma-separated
	int templateID;    // retail character ID it borrows
	int engine;        // 0 balanced, 1 accel, 2 speed, 3 turn (enum EngineClass)
	int wheels;
	char modelPath[NATIVE_RACER_PATH_BYTES];
	char portraitPath[NATIVE_RACER_PATH_BYTES];
};

// Fill `out` with up to `max` installed racers from `racersDir`, sorted by
// name. Returns the count, or -1 when the folder cannot be read (no folder is
// not an error: 0).
int NativeRacerLibrary_Scan(const char *racersDir, struct NativeRacerEntry *out, int max);

// Parse one racer.json (exposed for the harness). Returns 1 on success;
// `error` gets the reason otherwise. Paths are left relative.
int NativeRacerLibrary_ParseManifest(const char *json, size_t size, struct NativeRacerEntry *out, char *modelSha256, char *portraitSha256,
                                     size_t *modelBytes, size_t *portraitBytes, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif
#endif
