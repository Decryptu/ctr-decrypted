/* The Mods screen's Racers tab (docs/CUSTOM_RACERS.md). Included by
   MM_CustomManager.c.

   One list: every racer in the catalogue (github.com/Decryptu/ctr-decrypted-
   racers, read from its index.json), and every installed racer the catalogue
   does not list. Cross downloads the selected racer, or offers to remove an
   installed one. The panel shows the racer's portrait once installed, its
   authors and the retail racer it borrows. L2 reads the catalogue again; R2
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

static void MM_ModsRacers_Page(uint32_t *ot, int tapped)
{
    static struct ModsRacerRow rows[MODS_RACERS_INDEX_MAX + NATIVE_RACER_MAX];
    char line[256], dir[MODS_PATH_MAX];
    MM_ModsRacers_Dir(dir, sizeof dir);

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
    if (!row || !row->installed) s_racerConfirmRemove = 0;

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
            if (NativeRacerLibrary_Remove(dir, row->installed->folder, error, sizeof error))
            {
                NativeRacers_Reload();
                snprintf(s_racerMessage, sizeof s_racerMessage, "Removed.");
            }
            else snprintf(s_racerMessage, sizeof s_racerMessage, "%s", error);
            s_racerInstalledStale = 1;
            s_racerConfirmRemove = 0;
            OtherFX_Play(1, 1);
        }
        tapped = 0;
    }
    if (count && (tapped & (BTN_UP | BTN_DOWN)))
    {
        s_racerSelection = (s_racerSelection + ((tapped & BTN_UP) ? count - 1 : 1)) % count;
        row = &rows[s_racerSelection];
        if (!s_racerInstallBusy) s_racerMessage[0] = 0;
        OtherFX_Play(0, 1);
    }
    if (row && (tapped & (BTN_CROSS | BTN_CIRCLE)))
    {
        if (s_racerInstallBusy) snprintf(s_racerMessage, sizeof s_racerMessage, "Wait for the download to finish.");
        else if (row->installed)
        {
            s_racerConfirmRemove = 1;
            s_racerMessage[0] = 0;
        }
        else
        {
            s_racerInstallBusy = NativeRacerCatalogue_StartInstall(row->listed, dir);
            snprintf(s_racerMessage, sizeof s_racerMessage, "%s", s_racerInstallBusy ? "Files are checked first." : "Could not start the download.");
        }
        OtherFX_Play(1, 1);
    }

    const size_t first = (s_racerSelection / 7) * 7;
    for (size_t i = first; i < first + 7 && i < count; i++)
    {
        RECT box = {24, 68 + (int)(i - first) * 15, 260, 14};
        const char *name = rows[i].listed ? rows[i].listed->name : rows[i].installed->name;
        MM_CustomText_Draw(name, 32, box.y + 2, rows[i].installed ? 180 : 244, 1, i == s_racerSelection ? ORANGE : WHITE, ot);
        if (rows[i].installed) MM_CustomText_Draw("OWNED", 278, box.y + 2, 80, 1, JUSTIFY_RIGHT | GRAY, ot);
        if (i == s_racerSelection) CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    if (!count)
        MM_CustomText_Draw(s_racerRefreshBusy ? "Loading racers..." : *s_saphiQuery ? "No search matches" :
                           s_racerMessage[0] ? s_racerMessage : "Racer list unavailable", 32, 84, 244, 3, WHITE, ot);

    if (row)
    {
        const char *name = row->listed ? row->listed->name : row->installed->name;
        const char *authors = row->listed ? row->listed->authors : row->installed->authors;
        const int templateID = row->listed ? row->listed->templateID : row->installed->templateID;
        struct Icon *portrait = MM_ModsRacers_Portrait(row->installed);
        const int textX = portrait ? 357 : 305, textW = portrait ? 124 : 176;
        if (portrait)
        {
            struct GameTracker *gGT = sdata->gGT;
            const Color color = MM_CHARACTER_SELECT_NEUTRAL_COLOR;
            RECTMENU_DrawPolyGT4(portrait, 305, 70, &gGT->backBuffer->primMem, ot, color, color, color, color, TRANS_50_DECAL, FP(1.0));
        }
        MM_CustomText_Draw(name, textX, 70, textW, 2, ORANGE, ot);
        MM_CustomText_Draw(authors, 305, 96, 176, 2, WHITE, ot);
        if (s_racerConfirmRemove)
        {
            MM_CustomText_Draw("Remove this racer and its files?", 305, 120, 176, 2, WHITE, ot);
            MM_CustomText_Draw("* Yes  ^ No", 305, 158, 176, 1, ORANGE, ot);
        }
        else
        {
            snprintf(line, sizeof line, "Based on %s", s_racerTemplateNames[templateID & 15]);
            MM_CustomText_Draw(line, 305, 120, 176, 1, WHITE, ot);
            MM_CustomText_Draw(s_racerInstallBusy ? "Downloading..." : row->installed ? "* Remove" : "* Download", 305, 132, 176, 1, ORANGE, ot);
            MM_CustomText_Draw(s_racerMessage[0] ? s_racerMessage : !row->installed ? "Not installed" :
                               row->listed ? "Installed" : "Installed, not in the list", 305, 146, 176, 2, WHITE, ot);
        }
    }

    snprintf(line, sizeof line, "R2 Search: %s%s", s_saphiQuery, s_contentSearchEditing ? "_" : "");
    MM_CustomText_Draw(line, 256, 180, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    MM_CustomText_Draw("Select: Import track files", 256, 188, 456, 1, JUSTIFY_CENTER | ORANGE, ot);
    snprintf(line, sizeof line, "%u/%u   L2 Refresh   Back: %s", count ? (unsigned)s_racerSelection + 1 : 0, (unsigned)count,
             s_modsFromTitle ? "Title" : "Options");
    MM_CustomText_Draw(line, 256, 196, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    RECT listBox = {20, 64, 268, 108}, detailBox = {297, 64, 190, 108};
    RECTMENU_DrawInnerRect(&listBox, 0, ot);
    RECTMENU_DrawInnerRect(&detailBox, 0, ot);
}
