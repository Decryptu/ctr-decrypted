/* The Mods screen's Racers tab (docs/CUSTOM_RACERS.md). Included by
   MM_CustomManager.c.

   One list: every racer in the catalogue (github.com/Decryptu/ctr-decrypted-
   racers, read from its index.json), and every installed racer the catalogue
   does not list, tagged OWNED, UPDATE or OFF. Cross downloads the selected
   racer or its update; on an installed racer it opens the options every mod
   has (MM_Mods_ActionsInput): turn it off or on, remove it. An off racer stays
   installed but leaves the character select (assets/racers/mods-off.txt,
   platform/native_mods_off.c). The panel is the one the tracks use: the
   racer's portrait (a listed racer's is fetched on its own before any
   download), name, version, authors and the retail racer it borrows. The
   Installed tab lists the installed racers too and uses the same panel and
   Cross (MM_ModsRacers_DrawPanel, MM_ModsRacers_Input). An install, update,
   remove or switch reloads the racers, so the character select has them at
   once. */
#include <platform/native_racer_library.h>

#define MODS_RACERS_INDEX_MAX 256

static struct NativeRacerCatalogueEntry s_racerIndex[MODS_RACERS_INDEX_MAX];
static int s_racerIndexCount, s_racerIndexLoaded, s_racerRefreshBusy, s_racerInstallBusy;
static struct NativeRacerEntry s_racerInstalled[NATIVE_RACER_MAX];
static int s_racerInstalledCount, s_racerInstalledStale = 1;
static size_t s_racerSelection;
static char s_racerMessage[256];

/* A row: a catalogue entry, an installed racer, or both. */
struct ModsRacerRow
{
    const struct NativeRacerCatalogueEntry *listed;
    const struct NativeRacerEntry *installed;
};

static const char *const s_racerTemplateNames[16] = {
    "Crash", "Cortex", "Tiny", "Coco", "N. Gin", "Dingodile", "Polar", "Pura",
    "Pinstripe", "Papu Papu", "Ripper Roo", "Komodo Joe", "N. Tropy", "Penta", "Fake Crash", "N. Oxide"};

static void MM_ModsRacers_Dir(char *dir, size_t size)
{
    snprintf(dir, size, "%s/racers", NativeAssets_GetAssetDir());
}

static int MM_ModsRacers_IsOff(const struct NativeRacerEntry *racer)
{
    return NativeModsOff_Is(NATIVE_MODS_RACER, racer->id);
}

/* The character select reads the racers again without an off one. */
static int MM_ModsRacers_SetOff(const struct NativeRacerEntry *racer, int off)
{
    const int saved = NativeModsOff_Set(NATIVE_MODS_RACER, racer->id, off);
    NativeRacers_Reload();
    return saved;
}

static void MM_ModsRacers_Remove(const struct NativeRacerEntry *racer, char *message, size_t size)
{
    char dir[MODS_PATH_MAX], error[160] = {0}, id[sizeof racer->id];
    MM_ModsRacers_Dir(dir, sizeof dir);
    snprintf(id, sizeof id, "%s", racer->id);
    if (NativeRacerLibrary_Remove(dir, racer->folder, error, sizeof error))
    {
        NativeModsOff_Set(NATIVE_MODS_RACER, id, 0);
        NativeRacers_Reload();
        snprintf(message, size, "Removed.");
    }
    else snprintf(message, size, "%s", error);
    s_racerInstalledStale = 1;
}

/* The catalogue lists a newer racer.json than the installed one. */
static int MM_ModsRacers_HasUpdate(const struct NativeRacerCatalogueEntry *listed, const struct NativeRacerEntry *installed)
{
    return listed && installed && installed->manifestSha256[0] && listed->manifestSha256[0] &&
           strcmp(listed->manifestSha256, installed->manifestSha256) != 0;
}

/* Every frame of the Mods screen, on any tab: finish background jobs. */
static void MM_ModsRacers_Tick(void)
{
    /* Read the catalogue on any tab: Installed tells listed racers apart too. */
    if (!s_racerIndexLoaded && !s_racerRefreshBusy)
    {
        s_racerIndexLoaded = 1;
        s_racerInstalledStale = 1;
        s_racerRefreshBusy = NativeRacerCatalogue_StartRefresh();
        if (!s_racerRefreshBusy) snprintf(s_racerMessage, sizeof s_racerMessage, "Could not read the racer list. L2: Retry.");
    }
    if (s_racerRefreshBusy)
    {
        int count = 0;
        const int result = NativeRacerCatalogue_PollRefresh(s_racerIndex, MODS_RACERS_INDEX_MAX, &count, s_racerMessage, sizeof s_racerMessage);
        if (result == 1 || result == -2)
        {
            s_racerRefreshBusy = 0;
            if (result == 1)
            {
                s_racerIndexCount = count;
                s_racerMessage[0] = 0;
            }
            else fprintf(stderr, "[Racers] catalogue: %s\n", s_racerMessage);
        }
    }
    if (s_racerInstallBusy)
    {
        const int result = NativeRacerCatalogue_PollInstall(s_racerMessage, sizeof s_racerMessage);
        if (result == 1 || result == -2)
        {
            s_racerInstallBusy = 0;
            s_racerInstalledStale = 1;
            if (result == 1)
            {
                NativeRacers_Reload();
                snprintf(s_racerMessage, sizeof s_racerMessage, "Installed and verified.");
            }
            else fprintf(stderr, "[Racers] install: %s\n", s_racerMessage);
        }
    }
    if (s_racerInstalledStale && !s_racerInstallBusy)
    {
        char dir[MODS_PATH_MAX];
        MM_ModsRacers_Dir(dir, sizeof dir);
        s_racerInstalledCount = NativeRacerLibrary_Scan(dir, s_racerInstalled, NATIVE_RACER_MAX);
        if (s_racerInstalledCount < 0) s_racerInstalledCount = 0;
        s_racerInstalledStale = 0;
    }
}

/* The rows matching the search, by name. */
static size_t MM_ModsRacers_Rows(struct ModsRacerRow *rows, size_t max)
{
    size_t count = 0;
    for (int i = 0; i < s_racerIndexCount && count < max; i++)
    {
        const struct NativeRacerCatalogueEntry *e = &s_racerIndex[i];
        if (!MM_CustomSearchMatch(e->name, s_saphiQuery) && !MM_CustomSearchMatch(e->authors, s_saphiQuery)) continue;
        rows[count].listed = e;
        rows[count].installed = NULL;
        for (int k = 0; k < s_racerInstalledCount; k++)
            if (!strcmp(s_racerInstalled[k].id, e->id)) rows[count].installed = &s_racerInstalled[k];
        count++;
    }
    for (int k = 0; k < s_racerInstalledCount && count < max; k++)
    {
        const struct NativeRacerEntry *r = &s_racerInstalled[k];
        int listed = 0;
        for (int i = 0; i < s_racerIndexCount && !listed; i++) listed = !strcmp(s_racerIndex[i].id, r->id);
        if (listed || (!MM_CustomSearchMatch(r->name, s_saphiQuery) && !MM_CustomSearchMatch(r->authors, s_saphiQuery))) continue;
        size_t at = count++;
        for (; at > 0; at--)
        {
            const struct ModsRacerRow *prev = &rows[at - 1];
            if (strcmp(prev->listed ? prev->listed->name : prev->installed->name, r->name) <= 0) break;
            rows[at] = rows[at - 1];
        }
        rows[at].listed = NULL;
        rows[at].installed = r;
    }
    return count;
}

/* The portrait of an installed racer: its index in the loaded racers. */
static struct Icon *MM_ModsRacers_Portrait(const struct NativeRacerEntry *installed)
{
    for (int r = 0; installed && r < NativeRacers_Count(); r++)
    {
        const char *name = NativeRacers_Name(r);
        if (name && !strcmp(name, installed->name) && NativeRacers_Template(r) == installed->templateID)
            return NativeRacers_Portrait(r);
    }
    return NULL;
}

/* A listed racer that is not installed shows its portrait too: fetched once the
   selection has rested on it (NativeRacerCatalogue_StartPreview), kept in
   assets/racers/previews, and drawn from the picture slot the track pictures
   use, so showing one hides the other (MM_CustomPicture.c). */
static char s_racerPreviewWanted[37], s_racerPreviewFailed[37], s_racerPreviewBusy;
static int s_racerPreviewWait;
static unsigned char s_racerPreviewFile[NATIVE_RACER_PORTRAIT_BYTES];
static uint16_t s_racerPreviewTexels[NATIVE_RACER_PORTRAIT_W * NATIVE_RACER_PORTRAIT_H];

/* Put the CDP1 portrait in s_racerPreviewFile (16 palette words, then 4-bit
   texels, the low nibble first) in the picture slot, as racer `id`. */
static void MM_ModsRacers_ShowPortrait(const char *id)
{
    const uint8_t *palette = &s_racerPreviewFile[8];
    const uint8_t *texels = &s_racerPreviewFile[8 + 32];
    for (int y = 0; y < NATIVE_RACER_PORTRAIT_H; y++)
        for (int x = 0; x < NATIVE_RACER_PORTRAIT_W; x++)
        {
            const int word = y * NATIVE_RACER_PORTRAIT_ROW_WORDS + x / 4;
            const uint16_t bits = (uint16_t)(texels[word * 2] | (texels[word * 2 + 1] << 8));
            const int index = (bits >> ((x & 3) * 4)) & 15;
            s_racerPreviewTexels[y * NATIVE_RACER_PORTRAIT_W + x] = (uint16_t)(palette[index * 2] | (palette[index * 2 + 1] << 8));
        }
    s_pictureIcon = NativeRacers_TrackPicture(s_racerPreviewTexels, NATIVE_RACER_PORTRAIT_W, NATIVE_RACER_PORTRAIT_H);
    s_pictureShown = 0; /* the track picture that was in the slot is gone */
    s_picturePackage[0] = 0;
    snprintf(s_racerPreviewShown, sizeof s_racerPreviewShown, "%s", s_pictureIcon ? id : "");
}

/* Every frame of the Mods screen: finish the job. */
static void MM_ModsRacers_PreviewTick(void)
{
    char id[37];
    if (!s_racerPreviewBusy) return;
    const int result = NativeRacerCatalogue_PollPreview(id, s_racerPreviewFile);
    if (result == 0 || result == -1) return;
    s_racerPreviewBusy = 0;
    if (result != 1)
    {
        snprintf(s_racerPreviewFailed, sizeof s_racerPreviewFailed, "%s", id);
        return;
    }
    MM_ModsRacers_ShowPortrait(id);
}

/* An off racer is not loaded, so its portrait comes from its own file. */
static struct Icon *MM_ModsRacers_FilePortrait(const struct NativeRacerEntry *installed)
{
    FILE *f;
    if (!strcmp(s_racerPreviewShown, installed->id)) return s_pictureIcon;
    if (!strcmp(s_racerPreviewFailed, installed->id) || s_racerPreviewBusy) return NULL;
    f = fopen(installed->portraitPath, "rb");
    if (!f || fread(s_racerPreviewFile, 1, sizeof s_racerPreviewFile, f) != sizeof s_racerPreviewFile)
        snprintf(s_racerPreviewFailed, sizeof s_racerPreviewFailed, "%s", installed->id);
    else MM_ModsRacers_ShowPortrait(installed->id);
    if (f) fclose(f);
    return strcmp(s_racerPreviewShown, installed->id) ? NULL : s_pictureIcon;
}

/* The preview of a listed racer, or NULL while it is not there yet. */
static struct Icon *MM_ModsRacers_Preview(const struct NativeRacerCatalogueEntry *listed)
{
    if (!listed) return NULL;
    if (strcmp(listed->id, s_racerPreviewWanted) != 0)
    {
        snprintf(s_racerPreviewWanted, sizeof s_racerPreviewWanted, "%s", listed->id);
        s_racerPreviewWait = 0;
    }
    if (!strcmp(s_racerPreviewShown, listed->id)) return s_pictureIcon;
    if (!s_racerPreviewBusy && !s_pictureBusy && strcmp(s_racerPreviewFailed, listed->id) != 0 && ++s_racerPreviewWait >= MODS_PICTURE_WAIT)
    {
        char dir[MODS_PATH_MAX];
        snprintf(dir, sizeof dir, "%s/racers/previews", NativeAssets_GetAssetDir());
        s_racerPreviewBusy = (char)NativeRacerCatalogue_StartPreview(listed, dir);
        if (!s_racerPreviewBusy) snprintf(s_racerPreviewFailed, sizeof s_racerPreviewFailed, "%s", listed->id);
    }
    return NULL;
}

/* One racer's panel, on the Racers and Installed tabs: the panel every tab
   has (MM_Mods_DrawPanelHead), with the racer it borrows as its info line. */
static void MM_ModsRacers_DrawPanel(const struct NativeRacerCatalogueEntry *listed, const struct NativeRacerEntry *installed, uint32_t *ot)
{
    char info[64];
    const int off = installed && MM_ModsRacers_IsOff(installed);
    const int update = MM_ModsRacers_HasUpdate(listed, installed);
    struct Icon *portrait = installed ? (off ? MM_ModsRacers_FilePortrait(installed) : MM_ModsRacers_Portrait(installed))
                                      : MM_ModsRacers_Preview(listed);
    if (portrait)
    {
        struct GameTracker *gGT = sdata->gGT;
        const Color color = MM_CHARACTER_SELECT_NEUTRAL_COLOR;
        RECTMENU_DrawPolyGT4(portrait, MODS_PANEL_X, 70, &gGT->backBuffer->primMem, ot, color, color, color, color, TRANS_50_DECAL, FP(1.0));
    }
    else
    {
        /* The portrait's place, so the name does not move when it arrives. */
        RECT frame = {MODS_PANEL_X, 70, NATIVE_RACER_PORTRAIT_W, NATIVE_RACER_PORTRAIT_H};
        RECTMENU_DrawInnerRect(&frame, 0, ot);
    }
    MM_Mods_DrawPanelHead(MODS_PANEL_X + NATIVE_RACER_PORTRAIT_W + 8, listed ? listed->name : installed->name,
                          installed ? installed->version : "", listed ? listed->authors : installed->authors, ot);
    if (installed && s_modsActions != MODS_ACTIONS_CLOSED)
    {
        MM_Mods_DrawActions(NULL, installed, ot);
        return;
    }
    snprintf(info, sizeof info, "Based on %s", s_racerTemplateNames[(listed ? listed->templateID : installed->templateID) & 15]);
    MM_Mods_DrawPanelBody(info,
                          s_racerMessage[0] ? s_racerMessage : !installed ? "Not installed" :
                          off ? "Off: hidden from character select" : update ? "Update available" :
                          listed ? "Installed" : "Installed, not in the list",
                          s_racerInstallBusy ? "Downloading..." : installed && !update ? "* Options" : update ? "* Update" : "* Download", ot);
}

/* Cross on one racer: download it or its update, or open its options.
   Returns the taps it left. */
static int MM_ModsRacers_Input(const struct NativeRacerCatalogueEntry *listed, const struct NativeRacerEntry *installed, int tapped)
{
    char dir[MODS_PATH_MAX];
    if (!(tapped & (BTN_CROSS | BTN_CIRCLE))) return tapped;
    MM_ModsRacers_Dir(dir, sizeof dir);
    if (s_racerInstallBusy) snprintf(s_racerMessage, sizeof s_racerMessage, "Wait for the download to finish.");
    else if (installed && !MM_ModsRacers_HasUpdate(listed, installed))
    {
        s_modsActions = MODS_ACTIONS_OPEN;
        s_modsActionRow = MODS_ACTION_SWITCH;
        s_racerMessage[0] = 0;
    }
    else if (listed)
    {
        s_racerInstallBusy = NativeRacerCatalogue_StartInstall(listed, dir);
        snprintf(s_racerMessage, sizeof s_racerMessage, "%s", s_racerInstallBusy ? "Files are checked first." : "Could not start the download.");
    }
    OtherFX_Play(1, 1);
    return tapped & ~(BTN_CROSS | BTN_CIRCLE);
}

/* The Installed tab's racers: the installed ones matching the search, by name. */
static int MM_ModsRacers_InstalledRows(const struct NativeRacerEntry **out, int max)
{
    int count = 0;
    for (int k = 0; k < s_racerInstalledCount && count < max; k++)
    {
        const struct NativeRacerEntry *r = &s_racerInstalled[k];
        if (MM_CustomSearchMatch(r->name, s_saphiQuery) || MM_CustomSearchMatch(r->authors, s_saphiQuery)) out[count++] = r;
    }
    return count;
}

/* The catalogue entry of an installed racer, or NULL when it is not listed. */
static const struct NativeRacerCatalogueEntry *MM_ModsRacers_Listed(const struct NativeRacerEntry *installed)
{
    for (int i = 0; installed && i < s_racerIndexCount; i++)
        if (!strcmp(s_racerIndex[i].id, installed->id)) return &s_racerIndex[i];
    return NULL;
}

static void MM_ModsRacers_Page(uint32_t *ot, int tapped)
{
    static struct ModsRacerRow rows[MODS_RACERS_INDEX_MAX + NATIVE_RACER_MAX];

    if ((!s_racerIndexLoaded || (tapped & BTN_L2)) && !s_racerRefreshBusy)
    {
        s_racerIndexLoaded = 1;
        s_racerInstalledStale = 1;
        s_racerRefreshBusy = NativeRacerCatalogue_StartRefresh();
        if (!s_racerRefreshBusy) snprintf(s_racerMessage, sizeof s_racerMessage, "Could not read the racer list. L2: Retry.");
    }

    const size_t count = MM_ModsRacers_Rows(rows, sizeof rows / sizeof rows[0]);
    if (s_racerSelection >= count) s_racerSelection = 0;
    const struct ModsRacerRow *row = count ? &rows[s_racerSelection] : NULL;

    if (!row || !row->installed) s_modsActions = MODS_ACTIONS_CLOSED;
    tapped = MM_Mods_ActionsInput(tapped, NULL, row ? row->installed : NULL, s_racerMessage, sizeof s_racerMessage);
    tapped = MM_ModsRacers_Input(row ? row->listed : NULL, row ? row->installed : NULL, tapped);
    if (count && (tapped & (BTN_UP | BTN_DOWN)))
    {
        s_racerSelection = (s_racerSelection + ((tapped & BTN_UP) ? count - 1 : 1)) % count;
        row = &rows[s_racerSelection];
        if (!s_racerInstallBusy) s_racerMessage[0] = 0;
        OtherFX_Play(0, 1);
    }

    const size_t first = (s_racerSelection / 7) * 7;
    for (size_t i = first; i < first + 7 && i < count; i++)
    {
        RECT box = MM_Mods_RowBox((int)(i - first));
        const struct NativeRacerEntry *installed = rows[i].installed;
        const int off = installed && MM_ModsRacers_IsOff(installed);
        const char *tag = off ? "OFF" : MM_ModsRacers_HasUpdate(rows[i].listed, installed) ? "UPDATE" : installed ? "OWNED" : NULL;
        const char *name = rows[i].listed ? rows[i].listed->name : installed->name;
        MM_CustomText_Draw(name, 32, box.y + 2, tag ? MODS_LIST_TEXT_W - 64 : MODS_LIST_TEXT_W, 1,
                           i == s_racerSelection ? ORANGE : off ? GRAY : WHITE, ot);
        if (tag) MM_CustomText_Draw(tag, MODS_LIST_RIGHT, box.y + 2, 80, 1, JUSTIFY_RIGHT | GRAY, ot);
        if (i == s_racerSelection) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    if (!count)
        MM_CustomText_Draw(s_racerRefreshBusy ? "Loading racers..." : *s_saphiQuery ? "No search matches" :
                           s_racerMessage[0] ? s_racerMessage : "Racer list unavailable", 32, 84, MODS_LIST_TEXT_W, 3, WHITE, ot);
    if (row) MM_ModsRacers_DrawPanel(row->listed, row->installed, ot);
    MM_Mods_DrawFooter(NULL, "R2 Search", 0, s_racerSelection, count, NULL, ot);
    MM_Mods_DrawBoxes(ot);
}
