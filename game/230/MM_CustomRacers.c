/* The Mods screen's Racers tab (docs/CUSTOM_RACERS.md). Included by
   MM_CustomManager.c.

   One list: every racer in the catalogue (github.com/Decryptu/ctr-decrypted-
   racers, read from its index.json), and every installed racer the catalogue
   does not list. Cross downloads the selected racer, or offers to remove an
   installed one. The panel shows the racer's portrait (a listed racer's is
   fetched on its own before any download), its authors and the retail racer
   it borrows. The Installed tab lists the installed racers too and uses the
   same panel and Cross (MM_ModsRacers_DrawPanel, MM_ModsRacers_Input). L2 reads the catalogue again; R2
   searches names and authors. An install or remove reloads the racers, so the
   character select has them at once. */
#include <platform/native_racer_library.h>

#define MODS_RACERS_INDEX_MAX 256

static struct NativeRacerCatalogueEntry s_racerIndex[MODS_RACERS_INDEX_MAX];
static int s_racerIndexCount, s_racerIndexLoaded, s_racerRefreshBusy, s_racerInstallBusy;
static struct NativeRacerEntry s_racerInstalled[NATIVE_RACER_MAX];
static int s_racerInstalledCount, s_racerInstalledStale = 1;
static size_t s_racerSelection;
static int s_racerConfirmRemove;
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

static int MM_ModsRacers_ConsumesBack(void)
{
    return s_racerConfirmRemove;
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
    /* CDP1: 16 palette words, then 4-bit texels, the low nibble first. */
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

/* One racer's panel, on the Racers and Installed tabs: portrait, name,
   authors, the racer it borrows, then Cross and the state, or the Remove
   question. */
static void MM_ModsRacers_DrawPanel(const struct NativeRacerCatalogueEntry *listed, const struct NativeRacerEntry *installed, uint32_t *ot)
{
    char line[256];
    const char *name = listed ? listed->name : installed->name;
    const char *authors = listed ? listed->authors : installed->authors;
    const int templateID = listed ? listed->templateID : installed->templateID;
    struct Icon *portrait = installed ? MM_ModsRacers_Portrait(installed) : MM_ModsRacers_Preview(listed);
    const int textX = MODS_PANEL_X + NATIVE_RACER_PORTRAIT_W + 8, textW = MODS_PANEL_W - NATIVE_RACER_PORTRAIT_W - 8;
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
    MM_CustomText_Draw(name, textX, 70, textW, 2, ORANGE, ot);
    MM_CustomText_Draw(authors, MODS_PANEL_X, 100, MODS_PANEL_W, 2, WHITE, ot);
    if (s_racerConfirmRemove)
    {
        MM_CustomText_Draw("Remove this racer and its files?", MODS_PANEL_X, 124, MODS_PANEL_W, 2, WHITE, ot);
        MM_CustomText_Draw(NativeMenu_Labels("* Yes  ^ No"), MODS_PANEL_X, 160, MODS_PANEL_W, 1, ORANGE, ot);
        return;
    }
    snprintf(line, sizeof line, "Based on %s", s_racerTemplateNames[templateID & 15]);
    MM_CustomText_Draw(line, MODS_PANEL_X, 124, MODS_PANEL_W, 1, WHITE, ot);
    MM_CustomText_Draw(s_racerInstallBusy ? "Downloading..." : installed ? NativeMenu_Labels("* Remove") : NativeMenu_Labels("* Download"), MODS_PANEL_X, 136, MODS_PANEL_W, 1, ORANGE, ot);
    MM_CustomText_Draw(s_racerMessage[0] ? s_racerMessage : !installed ? "Not installed" :
                       listed ? "Installed" : "Installed, not in the list", MODS_PANEL_X, 150, MODS_PANEL_W, 2, WHITE, ot);
}

/* Cross on one racer and the Remove question. Returns the taps it left. */
static int MM_ModsRacers_Input(const struct NativeRacerCatalogueEntry *listed, const struct NativeRacerEntry *installed, int tapped)
{
    char dir[MODS_PATH_MAX];
    MM_ModsRacers_Dir(dir, sizeof dir);
    if (!installed) s_racerConfirmRemove = 0;
    if (s_racerConfirmRemove)
    {
        if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            s_racerConfirmRemove = 0;
            OtherFX_Play(2, 1);
        }
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            char error[160] = {0};
            if (NativeRacerLibrary_Remove(dir, installed->folder, error, sizeof error))
            {
                NativeRacers_Reload();
                snprintf(s_racerMessage, sizeof s_racerMessage, "Removed.");
            }
            else snprintf(s_racerMessage, sizeof s_racerMessage, "%s", error);
            s_racerInstalledStale = 1;
            s_racerConfirmRemove = 0;
            OtherFX_Play(1, 1);
        }
        return 0;
    }
    if (tapped & (BTN_CROSS | BTN_CIRCLE))
    {
        if (s_racerInstallBusy) snprintf(s_racerMessage, sizeof s_racerMessage, "Wait for the download to finish.");
        else if (installed)
        {
            s_racerConfirmRemove = 1;
            s_racerMessage[0] = 0;
        }
        else if (listed)
        {
            s_racerInstallBusy = NativeRacerCatalogue_StartInstall(listed, dir);
            snprintf(s_racerMessage, sizeof s_racerMessage, "%s", s_racerInstallBusy ? "Files are checked first." : "Could not start the download.");
        }
        OtherFX_Play(1, 1);
        tapped &= ~(BTN_CROSS | BTN_CIRCLE);
    }
    return tapped;
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
    char line[256];

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
        const char *name = rows[i].listed ? rows[i].listed->name : rows[i].installed->name;
        MM_CustomText_Draw(name, 32, box.y + 2, rows[i].installed ? MODS_LIST_TEXT_W - 64 : MODS_LIST_TEXT_W, 1, i == s_racerSelection ? ORANGE : WHITE, ot);
        if (rows[i].installed) MM_CustomText_Draw("OWNED", MODS_LIST_RIGHT, box.y + 2, 80, 1, JUSTIFY_RIGHT | GRAY, ot);
        if (i == s_racerSelection) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    if (!count)
        MM_CustomText_Draw(s_racerRefreshBusy ? "Loading racers..." : *s_saphiQuery ? "No search matches" :
                           s_racerMessage[0] ? s_racerMessage : "Racer list unavailable", 32, 84, MODS_LIST_TEXT_W, 3, WHITE, ot);
    if (row) MM_ModsRacers_DrawPanel(row->listed, row->installed, ot);

    snprintf(line, sizeof line, "R2 Search: %s%s", s_saphiQuery, s_contentSearchEditing ? "_" : "");
    MM_CustomText_Draw(line, 256, 180, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    MM_CustomText_Draw("Select: Import track files", 256, 188, 456, 1, JUSTIFY_CENTER | ORANGE, ot);
    snprintf(line, sizeof line, "%u/%u   L2 Refresh   Back: %s", count ? (unsigned)s_racerSelection + 1 : 0, (unsigned)count,
             s_modsFromTitle ? "Title" : "Options");
    MM_CustomText_Draw(line, 256, 196, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    MM_Mods_DrawBoxes(ot);
}
