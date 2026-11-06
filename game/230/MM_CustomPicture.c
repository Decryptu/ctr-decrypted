/* The selected track's picture in the Mods screen (docs/CUSTOM_TRACKS.md).
   Included by MM_CustomManager.c.

   A package's own picture (metadata/picture.ctp), or else Saphi's thumbnail of
   the track, cropped and shrunk to 112x42 by a background job
   (include/platform/native_track_picture.h) and kept in
   assets/tracks/pictures, so a track seen once shows offline too. The job
   starts once the selection has rested on a track for a moment, so scrolling
   through the list asks Saphi for nothing. One picture is in VRAM at a time. */
#include <platform/native_racers.h>
#include <platform/native_track_picture.h>

#define MODS_PICTURE_X MODS_PANEL_X
#define MODS_PICTURE_Y 68
/* Frames the selection rests on a track before its picture is fetched. */
#define MODS_PICTURE_WAIT 12

static uint16_t s_pictureTexels[NATIVE_TRACK_PICTURE_TEXELS];
static struct Icon *s_pictureIcon;
static int s_pictureShown, s_pictureWanted, s_pictureWait, s_pictureBusy, s_pictureFailed;
/* The racer whose portrait preview holds the slot instead (MM_CustomRacers.c),
   or "". */
static char s_racerPreviewShown[37];
/* The pin of the package whose own picture is in VRAM, or "". */
static char s_picturePackage[65];
/* The pin of the package that has none, so its file is looked for once. */
static char s_pictureNoPackage[65];

/* Every frame of the Mods screen: finish the job. */
static void MM_ModsPicture_Tick(void)
{
    if (!s_pictureBusy) return;
    int trackID = 0;
    const int result = NativeTrackPicture_Poll(&trackID, s_pictureTexels);
    if (result == 0 || result == -1) return;
    s_pictureBusy = 0;
    if (result == 1)
    {
        s_pictureIcon = NativeRacers_TrackPicture(s_pictureTexels, NATIVE_TRACK_PICTURE_WIDTH, NATIVE_TRACK_PICTURE_HEIGHT);
        s_pictureShown = s_pictureIcon ? trackID : 0;
        s_racerPreviewShown[0] = 0;
        s_picturePackage[0] = 0;
    }
    else s_pictureFailed = trackID;
}

/* The catalogue's picture of a track: its media id and path, or 0 without a
   catalogue (the cache answers then). */
static int MM_ModsPicture_Media(int trackID, const char **path)
{
    *path = "";
    for (size_t i = 0; s_saphiCatalogue && i < CustomSaphi_Count(s_saphiCatalogue); i++)
    {
        const struct CustomSaphiRevision *row = CustomSaphi_Row(s_saphiCatalogue, i);
        if (row->trackID != trackID || !row->picture.id) continue;
        *path = row->picture.path;
        return row->picture.id;
    }
    return 0;
}

/* Put a package's own picture in VRAM. 1 when it is there. */
static int MM_ModsPicture_LoadPackage(const char *pin)
{
    char path[MODS_PATH_MAX];
    size_t got = 0;

    if (!strcmp(pin, s_picturePackage)) return 1;
    if (!strcmp(pin, s_pictureNoPackage)) return 0;
    snprintf(path, sizeof path, "%s/tracks/packages/%s/metadata/picture.ctp", NativeAssets_GetAssetDir(), pin);
    FILE *file = fopen(path, "rb");
    if (file)
    {
        got = fread(s_pictureTexels, sizeof s_pictureTexels[0], NATIVE_TRACK_PICTURE_TEXELS, file);
        fclose(file);
    }
    if (got == NATIVE_TRACK_PICTURE_TEXELS) s_pictureIcon = NativeRacers_TrackPicture(s_pictureTexels, NATIVE_TRACK_PICTURE_WIDTH, NATIVE_TRACK_PICTURE_HEIGHT);
    if (got != NATIVE_TRACK_PICTURE_TEXELS || !s_pictureIcon)
    {
        snprintf(s_pictureNoPackage, sizeof s_pictureNoPackage, "%s", pin);
        return 0;
    }
    snprintf(s_picturePackage, sizeof s_picturePackage, "%s", pin);
    s_pictureShown = 0;
    s_racerPreviewShown[0] = 0;
    return 1;
}

/* The panel's top left corner: the picture of the package pin ("" for a track
   not installed) if it has one, else of trackID (a Saphi track id, 0 for a
   track that did not come from Saphi), or a frame saying there is none. */
static void MM_ModsPicture_Draw(int trackID, const char *pin, uint32_t *ot)
{
    if (pin[0] && MM_ModsPicture_LoadPackage(pin))
    {
        const Color color = MM_CHARACTER_SELECT_NEUTRAL_COLOR;
        RECTMENU_DrawPolyGT4(s_pictureIcon, MODS_PICTURE_X, MODS_PICTURE_Y, &sdata->gGT->backBuffer->primMem, ot,
                             color, color, color, color, 0, FP(1.0));
        return;
    }
    if (trackID != s_pictureWanted)
    {
        s_pictureWanted = trackID;
        s_pictureWait = 0;
        s_pictureFailed = 0;
    }
    if (trackID > 0 && s_pictureShown != trackID && !s_pictureBusy && s_pictureFailed != trackID &&
        ++s_pictureWait >= MODS_PICTURE_WAIT)
    {
        char dir[MODS_PATH_MAX];
        const char *path;
        const int mediaID = MM_ModsPicture_Media(trackID, &path);
        snprintf(dir, sizeof dir, "%s/tracks/pictures", NativeAssets_GetAssetDir());
        s_pictureBusy = NativeTrackPicture_Start(trackID, mediaID, path, dir);
        if (!s_pictureBusy) s_pictureFailed = trackID;
    }

    if (trackID > 0 && s_pictureShown == trackID)
    {
        const Color color = MM_CHARACTER_SELECT_NEUTRAL_COLOR;
        RECTMENU_DrawPolyGT4(s_pictureIcon, MODS_PICTURE_X, MODS_PICTURE_Y, &sdata->gGT->backBuffer->primMem, ot,
                             color, color, color, color, 0, FP(1.0));
        return;
    }
    if (trackID <= 0 || s_pictureFailed == trackID)
        MM_CustomText_Draw("No picture", MODS_PICTURE_X + NATIVE_TRACK_PICTURE_WIDTH / 2, MODS_PICTURE_Y + 15,
                           NATIVE_TRACK_PICTURE_WIDTH, 1, JUSTIFY_CENTER | GRAY, ot);
    RECT frame = {MODS_PICTURE_X, MODS_PICTURE_Y, NATIVE_TRACK_PICTURE_WIDTH, NATIVE_TRACK_PICTURE_HEIGHT};
    RECTMENU_DrawInnerRect(&frame, 0, ot);
}
