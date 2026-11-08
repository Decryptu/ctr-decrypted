/* The Mods screen: MODS on the title screen, or Options > Mods. Included by
   MM_ConfigMenu.c inside CTR_CUSTOM_PACKAGES only.

   Tabs, L1 / R1, drawn by the shared tab bar (MM_TabBar.c):
   - Seed (AP player client, a room whose plan names custom tracks): what the
     room needs and Install missing content (MM_CurrentSeed.c). The screen
     opens here in such a room.
   - Tracks: the Project Saphi catalogue and the Decrypted track catalogue, in
     one list (docs/CUSTOM_TRACKS.md section 5). Cross installs the selected
     revision, Square lists a track's versions.
   - Racers: the custom racer catalogue and the installed racers
     (MM_CustomRacers.c), with each racer's portrait.
   - Installed: everything installed, tracks first (their newest revision),
     then racers, tagged RACER. Cross on a track opens its actions: turn it
     off or on, remove it, check its files; on a racer, removes it.
   Tracks and Installed show the selected track's picture (MM_CustomPicture.c).
   Everywhere: Select imports an xdelta patch (MM_CustomImport.c, also on a
   patch dropped on the window), R2 searches, L2 refreshes the catalogue.

   A track that is off stays installed but leaves the Arcade and Time Trial
   pages. The list of tracks that are off is assets/tracks/mods-off.txt, one
   package UUID (or pin, for a package without one) per line. Seeds ignore it:
   a room that needs a track still finds it. */
#include <platform/native_custom_revision.h>
#include "MM_TabBar.c"

/* Every tab's list (left) and detail panel (right): a row is 15 pixels and
   holds MODS_LIST_TEXT_W pixels of text; the panel's text starts at
   MODS_PANEL_X and is MODS_PANEL_W wide. A track's picture takes the panel's
   top left corner (MM_CustomPicture.c), its text MODS_PANEL_SIDE_X onwards. */
#define MODS_LIST_TEXT_W 182
#define MODS_LIST_RIGHT 216
#define MODS_PANEL_X 241
#define MODS_PANEL_W 240
#define MODS_PANEL_SIDE_X 359
#define MODS_PANEL_SIDE_W 122
static RECT MM_Mods_RowBox(int row)
{
    RECT box = {24, 68 + row * 15, 198, 14};
    return box;
}
static void MM_Mods_DrawBoxes(uint32_t *ot)
{
    RECT listBox = {20, 64, 206, 108}, detailBox = {235, 64, 252, 108};
    RECTMENU_DrawInnerRect(&listBox, 0, ot);
    RECTMENU_DrawInnerRect(&detailBox, 0, ot);
}

#include "MM_CustomImport.c"
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
#include "MM_CurrentSeed.c"
#define MM_MODS_SEED_TAB() AP_SeedContentActive()
#else
#define MM_MODS_SEED_TAB() 0
#endif

static int s_saphiVersionTrack;
static size_t s_saphiBrowseSelection;
/* The revision being installed, for MM_CustomSourceRemoveTwins. */
static struct CustomSaphiRevision s_saphiInstalling;
static unsigned int s_modsLastFrame;

/* Installed track actions: the list, then the Remove confirmation. */
enum { MODS_ACTIONS_CLOSED, MODS_ACTIONS_OPEN, MODS_ACTIONS_CONFIRM_REMOVE };
enum { MODS_ACTION_SWITCH, MODS_ACTION_REMOVE, MODS_ACTION_CHECK, MODS_ACTION_COUNT };
static int s_modsActions, s_modsActionRow;

/* ---- Tracks that are off ---- */
#define MODS_OFF_MAX 256
#define MODS_PATH_MAX 4096
static char s_modsOff[MODS_OFF_MAX][65];
static int s_modsOffCount, s_modsOffLoaded;

static const char *MM_Mods_OffKey(const struct CustomTrackLibraryEntry *entry)
{
    return entry->local.uuid[0] ? entry->local.uuid : entry->manifestSha256;
}

static int MM_Mods_OffPath(char *path, size_t size)
{
    int length = snprintf(path, size, "%s/tracks/mods-off.txt", NativeAssets_GetAssetDir());
    return length > 0 && (size_t)length < size;
}

static void MM_Mods_LoadOff(void)
{
    char path[MODS_PATH_MAX], line[128];
    FILE *f;
    if (s_modsOffLoaded) return;
    s_modsOffLoaded = 1;
    s_modsOffCount = 0;
    if (!MM_Mods_OffPath(path, sizeof path) || (f = fopen(path, "r")) == NULL) return;
    while (s_modsOffCount < MODS_OFF_MAX && fgets(line, sizeof line, f))
    {
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        if (n > 0 && n < sizeof s_modsOff[0]) memcpy(s_modsOff[s_modsOffCount++], line, n + 1);
    }
    fclose(f);
}

static int MM_Mods_IsOff(const struct CustomTrackLibraryEntry *entry)
{
    const char *key = MM_Mods_OffKey(entry);
    MM_Mods_LoadOff();
    for (int i = 0; i < s_modsOffCount; i++)
        if (!strcmp(s_modsOff[i], key)) return 1;
    return 0;
}

static int MM_Mods_SetOff(const struct CustomTrackLibraryEntry *entry, int off)
{
    char path[MODS_PATH_MAX];
    const char *key = MM_Mods_OffKey(entry);
    FILE *f;
    int i;
    MM_Mods_LoadOff();
    for (i = 0; i < s_modsOffCount && strcmp(s_modsOff[i], key); i++) {}
    if (off && i == s_modsOffCount)
    {
        if (s_modsOffCount == MODS_OFF_MAX || strlen(key) >= sizeof s_modsOff[0]) return 0;
        snprintf(s_modsOff[s_modsOffCount++], sizeof s_modsOff[0], "%s", key);
    }
    else if (!off && i < s_modsOffCount)
        memmove(s_modsOff[i], s_modsOff[i + 1], (size_t)(--s_modsOffCount - i) * sizeof s_modsOff[0]);
    if (!MM_Mods_OffPath(path, sizeof path) || (f = fopen(path, "w")) == NULL) return 0;
    for (i = 0; i < s_modsOffCount; i++) fprintf(f, "%s\n", s_modsOff[i]);
    return fclose(f) == 0;
}

/* Remove every installed revision of the entry's track. */
static void MM_Mods_Remove(const struct CustomTrackLibraryEntry *entry, char *message, size_t size)
{
    int removed = 0, failed = 0;
    char error[256] = {0};
    for (size_t i = 0; i < s_contentLibrary.count; i++)
    {
        const struct CustomTrackLibraryEntry *r = &s_contentLibrary.entries[i];
        int same = entry->local.uuid[0] ? !strcmp(r->local.uuid, entry->local.uuid)
                                        : !strcmp(r->manifestSha256, entry->manifestSha256);
        if (!r->installed || !same) continue;
        if (CustomPackage_RemoveInstalled(NativeAssets_GetAssetDir(), r->manifestSha256, error, sizeof error)) removed++;
        else failed++;
    }
    if (removed && !failed) MM_Mods_SetOff(entry, 0);
    s_contentStoreWanted = 1;
    if (failed) snprintf(message, size, "%s", error);
    else snprintf(message, size, removed > 1 ? "Removed, with its older versions." : "Removed.");
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
    if (!AP_SeedContentInstallBusy()) AP_CustomContentRescan();
#endif
}

/* ---- Lists ---- */
static int MM_CustomSearchMatch(const char *text, const char *query)
{
    if (!*query) return 1;
    for (; *text; text++)
    {
        const char *a = text, *b = query;
        while (*a && *b)
        {
            int ac = *a >= 'A' && *a <= 'Z' ? *a + 32 : (unsigned char)*a;
            int bc = *b >= 'A' && *b <= 'Z' ? *b + 32 : (unsigned char)*b;
            if (ac != bc) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int MM_CustomSourcePreferred(const struct CustomSaphiRevision *a, const struct CustomSaphiRevision *b)
{
    if (!!a->disabledReason[0] != !!b->disabledReason[0]) return !a->disabledReason[0];
    if (a->current != b->current) return a->current;
    int order=CustomRevision_Compare(a->version,b->version);
    if (order) return order>0;
    return (a->modeTags & 1) && !(b->modeTags & 1);
}
static const struct CustomTrackLibraryEntry *MM_CustomSourceInstalled(const struct CustomSaphiRevision *source, int *older)
{
    const struct CustomTrackLibraryEntry *exact=NULL;
    *older=0;
    for(size_t i=0;i<s_contentLibrary.count;i++)
    {
        const struct CustomTrackLibraryEntry *r=&s_contentLibrary.entries[i];
        const int fromSaphi=r->local.trackID>0 && r->local.trackID==source->trackID;
        const int fromCatalogue=source->packageUuid[0] && !strcmp(r->local.uuid,source->packageUuid);
        if(!r->installed || (!fromSaphi && !fromCatalogue)) continue;
        /* The other source's copy of a track both list: the version decides. */
        if(fromSaphi ? source->decrypted : !source->decrypted)
        {
            const int order=CustomRevision_Compare(source->version,r->version);
            if(!order) exact=r;
            else if(order>0) *older=1;
            continue;
        }
        if(source->decrypted)
        {
            if(!strcmp(r->manifestSha256,source->pin)) exact=r;
            else if(CustomRevision_Compare(source->version,r->version)>=0) *older=1;
            continue;
        }
        int sameTrackFiles=r->local.levID==source->lev.id && r->local.vrmID==source->vrm.id && !strcmp(r->version,source->version);
        /* The same track files without the track's current music is an update. */
        if(sameTrackFiles && r->local.scaID==source->sca.id) exact=r;
        else if(sameTrackFiles || CustomRevision_Compare(source->version,r->version)>0) *older=1;
    }
    return exact;
}

/* A track both sources list has two package UUIDs. Once one source's newer
   revision is installed, the other source's copy would list the track twice,
   so it goes. Same-source older revisions stay, as they always have. */
static void MM_CustomSourceRemoveTwins(const struct CustomSaphiRevision *source)
{
    char error[256];
    for(size_t i=0;i<s_contentLibrary.count;i++)
    {
        const struct CustomTrackLibraryEntry *r=&s_contentLibrary.entries[i];
        const int twin=source->decrypted ? source->trackID>0 && r->local.trackID==source->trackID
                                         : source->packageUuid[0] && !strcmp(r->local.uuid,source->packageUuid);
        if(!r->installed || !twin) continue;
        if(!CustomPackage_RemoveInstalled(NativeAssets_GetAssetDir(),r->manifestSha256,error,sizeof error))
            fprintf(stderr,"[Tracks] remove %.16s: %s\n",r->manifestSha256,error);
    }
}

#include "MM_CustomPicture.c"
#include "MM_CustomRacers.c"

static int MM_Mods_ConsumesBack(void)
{
    return s_modsActions != MODS_ACTIONS_CLOSED || MM_ModsRacers_ConsumesBack();
}

/* L1 / R1 and the tab names. Tab 0 is Seed, shown only in a room that needs
   custom tracks; 1 Tracks, 2 Racers, 3 Installed. */
#define MODS_TABS 4
static int MM_Mods_Tab(void)
{
    return s_modsSeed ? 0 : s_modsRacers ? 2 : s_contentLibraryTab ? 3 : 1;
}

static void MM_Mods_SetTab(int tab)
{
    s_modsSeed = tab == 0;
    s_modsRacers = tab == 2;
    if (tab == 1 || tab == 3) s_contentLibraryTab = tab == 3;
    s_modsActions = MODS_ACTIONS_CLOSED;
    s_racerConfirmRemove = 0;
}

/* CTR_DEV_MODS (game/230/MM_MenuFlow.c): the title opens this screen on a tab. */
void MM_Mods_DevOpen(int tab)
{
    MM_Mods_OpenFromTitle();
    MM_Mods_SetTab(tab < MODS_TABS ? tab : 1);
    s_modsLastFrame = (unsigned int)sdata->gGT->timer;
}

static void MM_Mods_DrawTabs(int seedTab, uint32_t *ot)
{
    static const char *const names[MODS_TABS] = {"Seed", "Tracks", "Racers", "Installed"};
    static const char *const shortNames[MODS_TABS] = {NULL, NULL, NULL, "Owned"};
    const int low = seedTab ? 0 : 1;
    MM_TabBar_Draw(names + low, shortNames + low, MODS_TABS - low, MM_Mods_Tab() - low, 20, 492, 50, ot);
}

/* The detail panel below the picture and its text (y 124 to 172): the
   actions, or the Remove confirmation. */
static void MM_Mods_DrawActions(const struct CustomTrackLibraryEntry *installed, int off, uint32_t *ot)
{
    if (s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE)
    {
        MM_CustomText_Draw("Remove this track and its files?",MODS_PANEL_X,126,MODS_PANEL_W,2,WHITE,ot);
        MM_CustomText_Draw("* Yes  ^ No",MODS_PANEL_X,160,MODS_PANEL_W,1,ORANGE,ot);
        return;
    }
    for (int i = 0; i < MODS_ACTION_COUNT; i++)
    {
        static const char *const labels[MODS_ACTION_COUNT] = {NULL, "Remove", "Check files"};
        RECT box = {MODS_PANEL_X - 4,124 + i * 12,MODS_PANEL_W + 6,12};
        MM_CustomText_Draw(i == MODS_ACTION_SWITCH ? (off ? "Turn on" : "Turn off") : labels[i],
                           MODS_PANEL_X,box.y + 1,MODS_PANEL_W,1,i == s_modsActionRow ? ORANGE : WHITE,ot);
        if (i == s_modsActionRow) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    (void)installed;
    MM_CustomText_Draw("* OK  ^ Close",MODS_PANEL_X,160,MODS_PANEL_W,1,ORANGE,ot);
}

static void MM_ConfigProc_CustomContent(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
    size_t visible[CTR_LIBRARY_MAX], count = 0, first;
    const struct CustomSaphiRevision *source = NULL;
    const struct CustomTrackLibraryEntry *installed = NULL;
    int tapped = pad->buttonsTapped;
    int enterVersions = 0;
    int rawPad = Platform_InputRawGamepadButtons();
    int rawTapped = rawPad & ~s_contentSearchPadPrev;
    const int seedTab = MM_MODS_SEED_TAB();
    const unsigned int now = (unsigned int)sdata->gGT->timer;
    char line[256];
    (void)menu;
    s_contentSearchPadPrev = rawPad;
    /* A fresh visit opens on Seed in a room that needs custom tracks. */
    if (now - s_modsLastFrame > 2)
    {
        s_modsSeed = seedTab;
        if (seedTab) s_modsRacers = 0;
        s_modsActions = MODS_ACTIONS_CLOSED;
        s_racerConfirmRemove = 0;
    }
    s_modsLastFrame = now;
    if (!seedTab) s_modsSeed = 0;
    MM_CustomLibrary_Tick(0);
    MM_ModsRacers_Tick();
    MM_ModsPicture_Tick();
    MM_ModsRacers_PreviewTick();
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
    const int seedBusy = AP_SeedContentInstallBusy();
#else
    const int seedBusy = 0;
#endif
    if (!s_contentSearchEditing && !MM_CustomLibrary_Busy() && !s_saphiDownloading && !seedBusy) MM_CustomImport_TakeDrop();
    if (MM_CustomImport_Active()) { MM_CustomImport_Proc(ot, pad); return; }
    if (s_saphiRefreshBusy)
    {
        struct CustomSaphiCatalogue *fresh = NULL;
        int result = CustomSaphi_PollRefresh(&fresh, s_saphiMessage, sizeof s_saphiMessage);
        if (result == 1 || result == -2)
        {
            s_saphiRefreshBusy = 0;
            if (result == 1)
            {
                CustomSaphi_Free(&s_saphiCatalogue);
                s_saphiCatalogue = fresh;
                /* Catalogue rows' negative ids are handed out again. */
                if (s_pictureShown < 0) s_pictureShown = 0;
                if (s_pictureFailed < 0) s_pictureFailed = 0;
            }
        }
    }
    if (s_saphiDownloading)
    {
        int result = CustomSaphi_PollInstall(s_contentInstallPin, s_saphiMessage, sizeof s_saphiMessage);
        if (result == 1 || result == -2)
        {
            s_saphiDownloading = 0;
            if (result == 1)
            {
                if (!MM_CustomLibrary_Busy()) MM_CustomSourceRemoveTwins(&s_saphiInstalling);
                s_contentStoreWanted = 1;
                snprintf(s_saphiMessage, sizeof s_saphiMessage, "Installed. Files verified.");
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
                /* A room that needs this track finds it in the package store. */
                if (!seedBusy) AP_CustomContentRescan();
#endif
            }
            else fprintf(stderr, "[Saphi] install: %s\n", s_saphiMessage);
        }
    }
    if (s_contentSearchEditing)
    {
        if (!NativeText_Result())
        {
            if (rawTapped & CONN_PAD_CANCEL) NativeText_Resolve(2);
            else if (rawTapped & CONN_PAD_COMMIT) NativeText_Resolve(1);
        }
        if (NativeText_Result())
        {
            if (NativeText_Result() == 2) strcpy(s_saphiQuery, s_contentSearchBackup);
            NativeText_End(); s_contentSearchEditing = 0;
        }
        tapped = 0;
    }
    if ((!s_saphiLoaded || (tapped & BTN_L2)) && !s_saphiRefreshBusy)
    {
        s_saphiLoaded = 1;
        s_saphiRefreshBusy = CustomSaphi_StartRefresh();
        if (!s_saphiRefreshBusy) snprintf(s_saphiMessage,sizeof s_saphiMessage,"Could not refresh tracks. L2: Retry.");
        if (tapped & BTN_L2) s_contentStoreWanted = 1;
    }
    if ((tapped & BTN_SELECT) && !s_contentSearchEditing && !MM_CustomLibrary_Busy() && !seedBusy)
    {
        s_modsActions = MODS_ACTIONS_CLOSED;
        MM_CustomImport_Open();
        OtherFX_Play(1,1);
        return;
    }
    if (s_saphiVersionTrack && (tapped & (BTN_TRIANGLE | BTN_START)))
    {
        s_saphiVersionTrack=0;s_saphiSelection[0]=s_saphiBrowseSelection;tapped=0;
    }
    if ((tapped & (BTN_L1 | BTN_R1)) && s_modsActions == MODS_ACTIONS_CLOSED && !s_racerConfirmRemove)
    {
        const int low = seedTab ? 0 : 1, tabs = MODS_TABS - low;
        int tab = MM_Mods_Tab() - low;
        tab = (tab + ((tapped & BTN_R1) ? 1 : tabs - 1)) % tabs;
        if(s_saphiVersionTrack) s_saphiSelection[0]=s_saphiBrowseSelection;
        s_saphiVersionTrack=0;
        MM_Mods_SetTab(tab + low);
        OtherFX_Play(0,1);
        tapped &= ~(BTN_L1 | BTN_R1);
    }

    DecalFont_DrawLineOT("MODS",256,20,FONT_BIG,JUSTIFY_CENTER | ORANGE,ot);
    MM_Mods_DrawTabs(seedTab,ot);
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
    if (s_modsSeed)
    {
        if (!s_contentSearchEditing) MM_CurrentSeed_Page(ot, pad);
        return;
    }
#endif

    if ((tapped & BTN_R2) && s_modsActions == MODS_ACTIONS_CLOSED && !s_racerConfirmRemove)
    {
        strcpy(s_contentSearchBackup, s_saphiQuery);
        NativeText_Begin(s_saphiQuery, sizeof s_saphiQuery, 28, 180, 456, 12, 0);
        s_contentSearchEditing = 1;
        tapped = 0;
    }
    if (s_modsRacers)
    {
        MM_ModsRacers_Page(ot, tapped);
        return;
    }
    if (!s_contentLibraryTab)
    {
        for (size_t i = 0; i < CustomSaphi_Count(s_saphiCatalogue) && count < CTR_LIBRARY_MAX; i++)
        {
            const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, i);
            if (s_saphiVersionTrack)
            {
                if(r->trackID==s_saphiVersionTrack) visible[count++]=i;
                continue;
            }
            if (!(MM_CustomSearchMatch(r->title,s_saphiQuery) || MM_CustomSearchMatch(r->author,s_saphiQuery))) continue;
            size_t at=0;
            for(;at<count;at++) if(CustomSaphi_Row(s_saphiCatalogue,visible[at])->trackID==r->trackID) break;
            if(at==count) visible[count++]=i;
            else if(MM_CustomSourcePreferred(r,CustomSaphi_Row(s_saphiCatalogue,visible[at]))) visible[at]=i;
        }
    }
    else
    {
        for (size_t i = 0; i < s_contentLibrary.count && count < CTR_LIBRARY_MAX; i++)
        {
            const struct CustomTrackLibraryEntry *r = &s_contentLibrary.entries[i];
            if (r->installed && (MM_CustomSearchMatch(r->title,s_saphiQuery) || MM_CustomSearchMatch(r->author,s_saphiQuery) ||
                MM_CustomSearchMatch(r->version,s_saphiQuery)))
            {
                size_t at=0;
                for(;at<count;at++) if(r->local.uuid[0] && !strcmp(r->local.uuid,s_contentLibrary.entries[visible[at]].local.uuid)) break;
                if(at==count) visible[count++]=i;
                else if(CustomRevision_Compare(r->version,s_contentLibrary.entries[visible[at]].version)>0) visible[at]=i;
            }
        }
    }
    /* Installed: the racers come after the tracks (MM_CustomRacers.c). A row
       index below count is a track, from count on a racer. */
    const struct NativeRacerEntry *racers[NATIVE_RACER_MAX];
    const size_t racerCount = s_contentLibraryTab ? (size_t)MM_ModsRacers_InstalledRows(racers, NATIVE_RACER_MAX) : 0;
    const size_t total = count + racerCount;
    const struct NativeRacerEntry *racer = NULL;
    size_t *selected = &s_saphiSelection[s_contentLibraryTab];
    if (*selected >= total) *selected = 0;
    if (*selected < count)
    {
        if (!s_contentLibraryTab) source = CustomSaphi_Row(s_saphiCatalogue, visible[*selected]);
        else installed = &s_contentLibrary.entries[visible[*selected]];
    }
    else if (total) racer = racers[*selected - count];
    if (!installed) s_modsActions = MODS_ACTIONS_CLOSED;
    const int off = installed && MM_Mods_IsOff(installed);

    if (s_modsActions != MODS_ACTIONS_CLOSED)
    {
        /* The action list owns the pad; the store scan must be idle to change files. */
        if (tapped & (BTN_TRIANGLE | BTN_START))
        {
            s_modsActions = s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE ? MODS_ACTIONS_OPEN : MODS_ACTIONS_CLOSED;
            OtherFX_Play(2,1);
        }
        else if (s_modsActions == MODS_ACTIONS_OPEN && (tapped & (BTN_UP | BTN_DOWN)))
        {
            s_modsActionRow = (s_modsActionRow + ((tapped & BTN_UP) ? MODS_ACTION_COUNT - 1 : 1)) % MODS_ACTION_COUNT;
            OtherFX_Play(0,1);
        }
        else if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && MM_CustomLibrary_Busy())
            snprintf(s_saphiMessage,sizeof s_saphiMessage,"Wait for the check to finish.");
        else if (tapped & (BTN_CROSS | BTN_CIRCLE))
        {
            OtherFX_Play(1,1);
            if (s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE)
            {
                MM_Mods_Remove(installed, s_saphiMessage, sizeof s_saphiMessage);
                s_modsActions = MODS_ACTIONS_CLOSED;
            }
            else if (s_modsActionRow == MODS_ACTION_SWITCH)
            {
                snprintf(s_saphiMessage,sizeof s_saphiMessage,"%s",
                         !MM_Mods_SetOff(installed, !off) ? "Could not save the change." :
                         off ? "On: listed in Arcade and Time Trial." : "Off: hidden from Arcade and Time Trial.");
                s_modsActions = MODS_ACTIONS_CLOSED;
            }
            else if (s_modsActionRow == MODS_ACTION_REMOVE) s_modsActions = MODS_ACTIONS_CONFIRM_REMOVE;
            else
            {
                s_contentStoreWanted = 1;
                snprintf(s_saphiMessage,sizeof s_saphiMessage,"Checking installed files...");
                s_modsActions = MODS_ACTIONS_CLOSED;
            }
        }
        tapped = 0;
    }
    if (racer || s_racerConfirmRemove) tapped = MM_ModsRacers_Input(MM_ModsRacers_Listed(racer), racer, tapped);

    if (total && (tapped & (BTN_UP | BTN_DOWN)))
    {
        *selected = (*selected + ((tapped & BTN_UP) ? total - 1 : 1)) % total;
        if (!s_saphiDownloading) s_saphiMessage[0] = 0;
        if (!s_racerInstallBusy) s_racerMessage[0] = 0;
        OtherFX_Play(0,1);
        installed = NULL;
        source = NULL;
        racer = NULL;
        if (*selected < count)
        {
            installed = s_contentLibraryTab ? &s_contentLibrary.entries[visible[*selected]] : NULL;
            source = s_contentLibraryTab ? NULL : CustomSaphi_Row(s_saphiCatalogue, visible[*selected]);
        }
        else racer = racers[*selected - count];
    }
    if(source && !s_saphiVersionTrack && (tapped & BTN_SQUARE))
    {
        enterVersions=source->trackID;
        s_saphiMessage[0]=0;tapped=0;
    }
    int hasOlder=0;
    const struct CustomTrackLibraryEntry *sourceInstalled=source ? MM_CustomSourceInstalled(source,&hasOlder):NULL;
    if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && !MM_CustomLibrary_Busy())
    {
        if (sourceInstalled) { s_contentStoreWanted=1; snprintf(s_saphiMessage,sizeof s_saphiMessage,"Installed. Files verified."); }
        else if (source && !source->disabledReason[0])
        {
            s_saphiInstalling = *source;
            s_saphiDownloading = CustomSaphi_StartInstall(source,NativeAssets_GetAssetDir());
            snprintf(s_saphiMessage,sizeof s_saphiMessage,"%s",s_saphiDownloading ? "Downloading and checking selected revision..." : "Could not start download.");
        }
        else if (installed)
        {
            s_modsActions = MODS_ACTIONS_OPEN;
            s_modsActionRow = MODS_ACTION_SWITCH;
            s_saphiMessage[0] = 0;
        }
        OtherFX_Play(1,1);
    }
    first = (*selected / 7) * 7;
    for (size_t row = first; row < first + 7 && row < total; row++)
    {
        if (row >= count)
        {
            RECT box = MM_Mods_RowBox((int)(row-first));
            MM_CustomText_Draw(racers[row - count]->name,32,box.y+2,MODS_LIST_TEXT_W - 64,1,row==*selected ? ORANGE : WHITE,ot);
            MM_CustomText_Draw("RACER",MODS_LIST_RIGHT,box.y+2,80,1,JUSTIFY_RIGHT | GRAY,ot);
            if (row==*selected) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
            continue;
        }
        const char *title = !s_contentLibraryTab ? CustomSaphi_Row(s_saphiCatalogue,visible[row])->title : s_contentLibrary.entries[visible[row]].title;
        int rowOff = s_contentLibraryTab && MM_Mods_IsOff(&s_contentLibrary.entries[visible[row]]);
        RECT box = MM_Mods_RowBox((int)(row-first));
        if(s_saphiVersionTrack && !s_contentLibraryTab)
        {
            const struct CustomSaphiRevision *r=CustomSaphi_Row(s_saphiCatalogue,visible[row]);
            snprintf(line,sizeof line,"v%.32s %s",r->version,(r->modeTags&1)?"Arcade":(r->modeTags&2)?"TT":"Other");
            title=line;
        }
        /* Tracks: a track already installed is tagged OWNED, as on Racers. */
        int older = 0;
        const int rowOwned = !s_contentLibraryTab && !s_saphiVersionTrack &&
                             MM_CustomSourceInstalled(CustomSaphi_Row(s_saphiCatalogue,visible[row]),&older) != NULL;
        MM_CustomText_Draw(title,32,box.y+2,rowOff ? MODS_LIST_TEXT_W - 34 : rowOwned ? MODS_LIST_TEXT_W - 64 : MODS_LIST_TEXT_W,1,
                           row==*selected ? ORANGE : rowOff ? GRAY : WHITE,ot);
        if (rowOff) MM_CustomText_Draw("OFF",MODS_LIST_RIGHT,box.y+2,40,1,JUSTIFY_RIGHT | GRAY,ot);
        if (rowOwned) MM_CustomText_Draw("OWNED",MODS_LIST_RIGHT,box.y+2,80,1,JUSTIFY_RIGHT | GRAY,ot);
        if (row==*selected) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    if (!total) MM_CustomText_Draw(s_saphiRefreshBusy && !s_contentLibraryTab ? "Loading tracks..." :
        *s_saphiQuery ? "No search matches" : s_contentLibraryTab ? "Nothing installed yet. Get tracks and racers from their tabs." : "Catalogue unavailable",32,84,MODS_LIST_TEXT_W,3,WHITE,ot);
    if (racer) MM_ModsRacers_DrawPanel(MM_ModsRacers_Listed(racer), racer, ot);
    else if (source || installed)
    {
        const char *author = source ? source->author : installed->author[0] ? installed->author : installed->local.author;
        MM_ModsPicture_Draw(source ? source->trackID : installed->local.trackID, installed ? installed->manifestSha256 : source->decrypted ? source->pin : "", ot);
        /* Beside the picture, about 9 letters a line: the title (one or two
           lines), the version, then the author in what is left. */
        const char *title = source ? source->title : installed->title;
        snprintf(line,sizeof line,"%s",title);
        const int titleLines = DecalFont_GetLineWidth(line,FONT_SMALL) <= MODS_PANEL_SIDE_W ? 1 : 2;
        MM_CustomText_Draw(title,MODS_PANEL_SIDE_X,70,MODS_PANEL_SIDE_W,titleLines,ORANGE,ot);
        snprintf(line,sizeof line,"v%.32s",source ? source->version : installed->version);
        MM_CustomText_Draw(line,MODS_PANEL_SIDE_X,70 + 12 * titleLines,MODS_PANEL_SIDE_W,1,WHITE,ot);
        if (author[0])
        {
            snprintf(line,sizeof line,"by %.80s",author);
            MM_CustomText_Draw(line,MODS_PANEL_SIDE_X,82 + 12 * titleLines,MODS_PANEL_SIDE_W,3 - titleLines,WHITE,ot);
        }
        if (installed && s_modsActions != MODS_ACTIONS_CLOSED)
            MM_Mods_DrawActions(installed, off, ot);
        else
        {
            /* Installed: which custom pages list the track as raceable. */
            MM_CustomText_Draw(source ? ((source->modeTags&1) ? "Arcade tagged" : (source->modeTags&2) ? "Time Trial" : "Other modes") :
                off ? "Off" : installed->local.arcade ? "Arcade" : installed->local.timeTrial ? "Time Trial" : "No races",MODS_PANEL_X,126,MODS_PANEL_W,1,WHITE,ot);
            MM_CustomText_Draw(s_saphiDownloading ? "Downloading..." : source ?
                sourceInstalled ? "* Recheck" : source->disabledReason[0] ? "Unavailable" : hasOlder ? "* Update" : "* Download" : "* Options",MODS_PANEL_X,138,MODS_PANEL_W,1,ORANGE,ot);
            /* An installed track that is not on the Arcade pages says why, in up
               to two lines (native_custom_content_verify.h). */
            const char *why = installed && !installed->local.arcade ?
                (installed->local.timeTrial ? installed->local.arcadeReason : installed->local.timeTrialReason) : "";
            const char *status = source && source->disabledReason[0] ? source->disabledReason :
                sourceInstalled ? "Installed" : hasOlder && !s_saphiVersionTrack ? "Update available" : s_saphiMessage[0] ? s_saphiMessage : source ?
                (source->modeTags & 1) ? "Arcade tagged" : (source->modeTags & 2) ? "Time Trial tagged" : "Other modes" :
                off ? "Hidden from Arcade and Time Trial" : why[0] ? why : "Files verified";
            MM_CustomText_Draw(status,MODS_PANEL_X,150,MODS_PANEL_W,source ? 1 : 2,WHITE,ot);
            if(source) MM_CustomText_Draw(s_saphiVersionTrack ? "Back: Tracks" : "[ Versions",MODS_PANEL_X,162,MODS_PANEL_W,1,ORANGE,ot);
        }
    }
    snprintf(line,sizeof line,"R2 Search: %s%s",s_saphiQuery,s_contentSearchEditing ? "_" : "");
    MM_CustomText_Draw(line,256,180,456,1,JUSTIFY_CENTER | WHITE,ot);
    MM_CustomText_Draw("Select: Import track files",256,188,456,1,JUSTIFY_CENTER | ORANGE,ot);
    snprintf(line,sizeof line,"%u/%u   L2 Refresh   Back: %s",total ? (unsigned)*selected+1 : 0,(unsigned)total,
             s_modsFromTitle ? "Title" : "Options");
    MM_CustomText_Draw(line,256,196,456,1,JUSTIFY_CENTER | WHITE,ot);
    MM_Mods_DrawBoxes(ot);
    if(enterVersions) { s_saphiVersionTrack=enterVersions;s_saphiBrowseSelection=*selected;*selected=0; }
}
