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
	char folder[64];   // its folder under assets/racers (set by Scan)
};

// One racer in the catalogue's index.json (github.com/Decryptu/ctr-decrypted-racers).
struct NativeRacerCatalogueEntry
{
	char id[37];
	char name[NATIVE_RACER_NAME_BYTES];
	char authors[128]; // comma-separated
	char folder[64];   // racers/<folder>/ in the catalogue, and the install folder
	int templateID;
	size_t manifestBytes;
	char manifestSha256[65];
};

// Fill `out` with up to `max` installed racers from `racersDir`, sorted by
// name. Returns the count, or -1 when the folder cannot be read (no folder is
// not an error: 0).
int NativeRacerLibrary_Scan(const char *racersDir, struct NativeRacerEntry *out, int max);

// Parse one racer.json (exposed for the harness). Returns 1 on success;
// `error` gets the reason otherwise. Paths are left relative.
int NativeRacerLibrary_ParseManifest(const char *json, size_t size, struct NativeRacerEntry *out, char *modelSha256, char *portraitSha256,
                                     size_t *modelBytes, size_t *portraitBytes, char *error, size_t errorSize);

// Parse the catalogue's index.json. Returns the count (at most `max`), or -1
// with `error` set.
int NativeRacerLibrary_ParseIndex(const char *json, size_t size, struct NativeRacerCatalogueEntry *out, int max,
                                  char *error, size_t errorSize);

// Install a downloaded racer as racersDir/<folder>: the manifest and both
// files are checked against each other first, written to a staging folder,
// then moved into place. Any installed folder with the same racer id is
// replaced. Returns 1 on success.
int NativeRacerLibrary_Install(const char *racersDir, const char *folder, const void *manifest, size_t manifestSize,
                               const void *model, size_t modelSize, const void *portrait, size_t portraitSize,
                               char *error, size_t errorSize);

// Delete racersDir/<folder> and everything in it. Returns 1 on success.
int NativeRacerLibrary_Remove(const char *racersDir, const char *folder, char *error, size_t errorSize);

// The catalogue over HTTPS (ap/ap_custom_track_download.cpp, from
// raw.githubusercontent.com). Start returns 1 when a worker was started.
// Poll returns 0 while running, 1 on success, -2 on failure (with `error`),
// -1 when idle; a finished result is handed over once.
int NativeRacerCatalogue_StartRefresh(void);
int NativeRacerCatalogue_PollRefresh(struct NativeRacerCatalogueEntry *out, int max, int *count, char *error, size_t errorSize);
int NativeRacerCatalogue_StartInstall(const struct NativeRacerCatalogueEntry *entry, const char *racersDir);
int NativeRacerCatalogue_PollInstall(char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif
#endif
