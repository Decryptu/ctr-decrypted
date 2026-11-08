#ifndef NATIVE_SAPHI_CATALOGUE_H
#define NATIVE_SAPHI_CATALOGUE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Source identity before acquisition. A Saphi media pair is not a package
   pin or a Ready verdict; the downloaded, verified snapshot produces those. */
struct CustomSaphiMedia
{
    int id;
    unsigned int bytes;
    uint32_t crc32;
    char path[241];
};
struct CustomSaphiRevision
{
    int trackID;
    char title[641], author[81], version[65];
    struct CustomSaphiMedia lev, vrm;
    /* The track's audio (.sca), versioned apart from LEV and VRM. Set only on
       current revisions, and only when Saphi lists exactly one current .sca
       with a download; id zero otherwise. Optional: never disables a row. */
    struct CustomSaphiMedia sca;
    /* The track's picture (a JPEG thumbnail), shown in the Mods screen. Set on
       every row of the track when Saphi lists exactly one current picture;
       id zero otherwise. Saphi serves a smaller copy than its listed size
       and checksum describe, so only the id and path are used. On a
       Decrypted catalogue row without a Saphi picture it is the package's own
       picture.ctp instead: path is "tracks/<folder>/metadata/picture.ctp" in
       the catalogue, with its listed size and CRC32. */
    struct CustomSaphiMedia picture;
    unsigned int modeTags;
    int current;
    int sourceLaps; /* Track-level source metadata, zero if unspecified. */
    char disabledReason[129]; /* Ambiguous/missing pair remains visible. */
    /* The Decrypted track catalogue (docs/CUSTOM_TRACKS.md section 5). On its
       rows, decrypted is set and trackID is negative, or the Saphi track id
       when Saphi lists the same track, so the two pair as versions of one
       track. packageUuid is the catalogue package's UUID, also on the Saphi
       rows of such a track, so an installed copy from either source counts. */
    int decrypted;
    char packageUuid[37], pin[65], folder[129];
    unsigned int manifestBytes;
};
struct CustomSaphiCatalogue;
/* Parse a complete, bounded response transactionally. No downloads/writes.
   Exactly one LEV and VRM must match both version and mode signature. */
int CustomSaphi_ParseCatalogue(const char *json, size_t size,
    struct CustomSaphiCatalogue **out, char *error, size_t errorSize);
/* Add the Decrypted catalogue's index.json to a parsed catalogue, all or
   nothing. A track Saphi also lists (its saphi_track_id, or a Saphi revision
   with the same LEV and VRM size and CRC32) joins that track's rows, and is
   left out when an available Saphi row has the same version. */
int CustomSaphi_AddDecrypted(struct CustomSaphiCatalogue *catalogue, const char *json, size_t size,
    char *error, size_t errorSize);
size_t CustomSaphi_Count(const struct CustomSaphiCatalogue *catalogue);
const struct CustomSaphiRevision *CustomSaphi_Row(const struct CustomSaphiCatalogue *catalogue, size_t index);
void CustomSaphi_Free(struct CustomSaphiCatalogue **catalogue);
/* Main-thread network job. Poll: 0 running, 1 transfers completed catalogue,
   -1 idle, -2 failed. On failure the caller's prior catalogue is untouched. */
int CustomSaphi_StartRefresh(void);
int CustomSaphi_PollRefresh(struct CustomSaphiCatalogue **out, char *error, size_t errorSize);
/* Explicit download of the copied source revision. Installed result identifies
   the complete immutable package; it does not certify any gameplay mode. */
int CustomSaphi_StartInstall(const struct CustomSaphiRevision *revision, const char *assets);
int CustomSaphi_PollInstall(char pin[65], char *error, size_t errorSize);
#ifdef __cplusplus
}
#endif
#endif
