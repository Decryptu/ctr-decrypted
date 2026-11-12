/* The Mods screen: MODS on the title screen, or Options > Mods. Included by
   MM_ConfigMenu.c inside CTR_CUSTOM_PACKAGES only.

   Tabs, L1 / R1, drawn by the shared tab bar (MM_TabBar.c):
   - Seed (AP player client, a room whose plan names custom tracks): what the
     room needs and Install missing content (MM_CurrentSeed.c). The screen
     opens here in such a room.
   - Tracks: the Project Saphi catalogue and the Decrypted track catalogue, in
     one list (docs/CUSTOM_TRACKS.md section 5). Square lists a track's
     versions.
   - Racers: the custom racer catalogue and the installed racers
     (MM_CustomRacers.c).
   - Installed: everything installed, tracks first (their newest revision),
     then racers, tagged RACER.

   Every tab has the same layout (MM_ModsPanel.c: list, panel, footer) and the
   same buttons. Cross downloads what is not installed or has an update; on an
   installed track or racer it opens its options (MM_Mods_ActionsInput): turn
   it off or on, remove it, and for a track check its files. L2 refreshes,
   R2 searches, Select imports an xdelta patch (MM_CustomImport.c, also on a
   patch dropped on the window). Tracks and Installed show the selected
   track's picture (MM_CustomPicture.c).

   An off mod stays installed but the game leaves it out: a track leaves the
   Arcade and Time Trial pages, a racer the character select
   (platform/native_mods_off.c). Seeds ignore the list: a room that needs a
   track still finds it. */
#include <platform/native_menu.h>
#include <platform/native_custom_revision.h>
#include <platform/native_mods_off.h>
#include <platform/native_racer_library.h>
#include "../../ap/ap_custom_track_download.h"
#include "MM_TabBar.c"

#include "MM_ModsPanel.c"

#include "MM_CustomImport.c"

static int s_saphiVersionTrack;
static size_t s_saphiBrowseSelection;
/* The revision being installed, for MM_CustomSourceRemoveTwins. */
static struct CustomSaphiRevision s_saphiInstalling;
static unsigned int s_modsLastFrame;

/* Installed track actions: the list, then the Remove confirmation. */
enum { MODS_ACTIONS_CLOSED, MODS_ACTIONS_OPEN, MODS_ACTIONS_CONFIRM_REMOVE };
enum { MODS_ACTION_SWITCH, MODS_ACTION_REMOVE, MODS_ACTION_CHECK, MODS_ACTION_COUNT };
static int s_modsActions, s_modsActionRow;

/* ---- Mods that are off (platform/native_mods_off.c) ---- */
#define MODS_PATH_MAX 4096

static const char *MM_Mods_OffKey(const struct CustomTrackLibraryEntry *entry)
{
    return entry->local.uuid[0] ? entry->local.uuid : entry->manifestSha256;
}

static int MM_Mods_IsOff(const struct CustomTrackLibraryEntry *entry)
{
    return NativeModsOff_Is(NATIVE_MODS_TRACK, MM_Mods_OffKey(entry));
}

static int MM_Mods_SetOff(const struct CustomTrackLibraryEntry *entry, int off)
{
    const int saved = NativeModsOff_Set(NATIVE_MODS_TRACK, MM_Mods_OffKey(entry), off);
    /* The Arcade and Time Trial pages rebuild their rows on a new generation. */
    s_contentStoreGeneration++;
    return saved;
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
        if(!r->installed) continue;
        /* Seed packages have a room UUID, not either catalogue's identity. */
        if(!fromSaphi && !fromCatalogue && source->lev.sha256[0] && source->vrm.sha256[0] &&
           !strcmp(r->local.levSha256,source->lev.sha256) &&
           !strcmp(r->local.vrmSha256,source->vrm.sha256))
        {
            exact=r;
            continue;
        }
        if(!fromSaphi && !fromCatalogue) continue;
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

/* A mod's options, opened with Cross on an installed track or racer on any
   tab: turn it off or on, remove it, and for a track check its files. */
static int MM_Mods_ActionsInput(int tapped, const struct CustomTrackLibraryEntry *track,
                                const struct NativeRacerEntry *racer, char *message, size_t size);
static void MM_Mods_DrawActions(const struct CustomTrackLibraryEntry *track, const struct NativeRacerEntry *racer, uint32_t *ot);

#include "MM_CustomPicture.c"
#include "MM_CustomRacers.c"
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
#include "MM_CurrentSeed.c"
#define MM_MODS_SEED_TAB() AP_SeedContentActive()
#else
#define MM_MODS_SEED_TAB() 0
#endif

static int MM_Mods_ConsumesBack(void)
{
    return s_modsActions != MODS_ACTIONS_CLOSED;
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

static int MM_Mods_ActionsInput(int tapped, const struct CustomTrackLibraryEntry *track,
                                const struct NativeRacerEntry *racer, char *message, size_t size)
{
    const int rows = racer ? MODS_ACTION_CHECK : MODS_ACTION_COUNT;
    if (s_modsActions == MODS_ACTIONS_CLOSED) return tapped;
    if (!track && !racer)
    {
        s_modsActions = MODS_ACTIONS_CLOSED;
        return tapped;
    }
    if (s_modsActionRow >= rows) s_modsActionRow = MODS_ACTION_SWITCH;
    const int off = racer ? MM_ModsRacers_IsOff(racer) : MM_Mods_IsOff(track);
    if (tapped & (BTN_TRIANGLE | BTN_START))
    {
        s_modsActions = s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE ? MODS_ACTIONS_OPEN : MODS_ACTIONS_CLOSED;
        OtherFX_Play(2,1);
    }
    else if (s_modsActions == MODS_ACTIONS_OPEN && (tapped & (BTN_UP | BTN_DOWN)))
    {
        s_modsActionRow = (s_modsActionRow + ((tapped & BTN_UP) ? rows - 1 : 1)) % rows;
        OtherFX_Play(0,1);
    }
    /* A track's files change only while the store scan is idle. */
    else if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && track && MM_CustomLibrary_Busy())
        snprintf(message, size, "Wait for the check to finish.");
    else if (tapped & (BTN_CROSS | BTN_CIRCLE))
    {
        OtherFX_Play(1,1);
        if (s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE)
        {
            if (racer) MM_ModsRacers_Remove(racer, message, size);
            else MM_Mods_Remove(track, message, size);
            s_modsActions = MODS_ACTIONS_CLOSED;
        }
        else if (s_modsActionRow == MODS_ACTION_SWITCH)
        {
            const int saved = racer ? MM_ModsRacers_SetOff(racer, !off) : MM_Mods_SetOff(track, !off);
            snprintf(message, size, "%s", !saved ? "Could not save the change." :
                     racer ? (off ? "On: back on character select." : "Off: hidden from character select.") :
                     off ? "On: in Arcade and Time Trial." : "Off: not in Arcade or Time Trial.");
            s_modsActions = MODS_ACTIONS_CLOSED;
        }
        else if (s_modsActionRow == MODS_ACTION_REMOVE) s_modsActions = MODS_ACTIONS_CONFIRM_REMOVE;
        else
        {
            s_contentStoreWanted = 1;
            snprintf(message, size, "Checking installed files...");
            s_modsActions = MODS_ACTIONS_CLOSED;
        }
    }
    return 0;
}

static void MM_Mods_DrawActions(const struct CustomTrackLibraryEntry *track, const struct NativeRacerEntry *racer, uint32_t *ot)
{
    if (s_modsActions == MODS_ACTIONS_CONFIRM_REMOVE)
    {
        MM_CustomText_Draw(racer ? "Remove this racer and its files?" : "Remove this track and its files?",
                           MODS_PANEL_X,MODS_PANEL_INFO_Y + 2,MODS_PANEL_W,2,WHITE,ot);
        MM_CustomText_Draw(NativeMenu_Labels("* Yes  ^ No"),MODS_PANEL_X,MODS_PANEL_PROMPT_Y,MODS_PANEL_W,1,ORANGE,ot);
        return;
    }
    const int off = racer ? MM_ModsRacers_IsOff(racer) : MM_Mods_IsOff(track);
    const int rows = racer ? MODS_ACTION_CHECK : MODS_ACTION_COUNT;
    for (int i = 0; i < rows; i++)
    {
        static const char *const labels[MODS_ACTION_COUNT] = {NULL, "Remove", "Check files"};
        RECT box = {MODS_PANEL_X - 4,MODS_PANEL_INFO_Y + i * 12,MODS_PANEL_W + 6,12};
        MM_CustomText_Draw(i == MODS_ACTION_SWITCH ? (off ? "Turn on" : "Turn off") : labels[i],
                           MODS_PANEL_X,box.y + 1,MODS_PANEL_W,1,i == s_modsActionRow ? ORANGE : WHITE,ot);
        if (i == s_modsActionRow) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    MM_CustomText_Draw(NativeMenu_Labels("* OK  ^ Close"),MODS_PANEL_X,MODS_PANEL_PROMPT_Y,MODS_PANEL_W,1,ORANGE,ot);
}

/* Drain the catalogue refresh and the track download, every frame the
   Mods screen is open. */
static int s_saphiRefreshFailed; /* the last refresh failed; an older catalogue may remain */
static int s_saphiFailed; /* s_saphiMessage holds a refresh or install failure, drawn in full */
static void MM_CustomManager_Poll(void)
{
    if (s_saphiRefreshBusy)
    {
        struct CustomSaphiCatalogue *fresh = NULL;
        int result = CustomSaphi_PollRefresh(&fresh, s_saphiMessage, sizeof s_saphiMessage);
        if (result == 1 || result == -2)
        {
            s_saphiRefreshBusy = 0;
            s_saphiRefreshFailed = result == -2;
            /* A successful refresh empties s_saphiMessage, so no failure is left to show. */
            if (result == -2) { s_saphiFailed = 1; fprintf(stderr, "[Saphi] refresh: %s\n", s_saphiMessage); }
            else s_saphiFailed = 0;
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
            s_saphiFailed = result == -2;
            if (result == 1)
            {
                if (!MM_CustomLibrary_Busy()) MM_CustomSourceRemoveTwins(&s_saphiInstalling);
                s_contentStoreWanted = 1;
#ifdef CTR_AP
                {
                    char publicationError[256];
                    if (!AP_Solo_PublishInstalledStore(s_contentInstallPin, publicationError, sizeof publicationError))
                        snprintf(s_saphiMessage, sizeof s_saphiMessage,
                                 "Files installed, but Play Solo publication failed: %.170s", publicationError);
                    else
                        snprintf(s_saphiMessage, sizeof s_saphiMessage, "Installed. Files verified and listed in Play Solo.");
                }
#else
                snprintf(s_saphiMessage, sizeof s_saphiMessage, "Installed. Files verified.");
#endif
#if defined(CTR_AP) && !defined(CTR_AP_AUTHORING)
                /* A room that needs this track finds it in the package store. */
                if (!AP_SeedContentInstallBusy()) AP_CustomContentRescan();
#endif
            }
            else fprintf(stderr, "[Saphi] install: %s\n", s_saphiMessage);
        }
    }
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
    MM_CustomManager_Poll();
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
    if ((tapped & BTN_SELECT) && !s_modsRacers && !s_contentSearchEditing && !MM_CustomLibrary_Busy() && !seedBusy)
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
    if ((tapped & (BTN_L1 | BTN_R1)) && s_modsActions == MODS_ACTIONS_CLOSED)
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

    if ((tapped & BTN_R2) && s_modsActions == MODS_ACTIONS_CLOSED)
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
            /* A revision that failed its checks stays listed with its reason. */
            if ((r->installed || r->scanFailed) && (MM_CustomSearchMatch(r->title,s_saphiQuery) || MM_CustomSearchMatch(r->author,s_saphiQuery) ||
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
    /* The installed copy the selected track row stands for: the row itself on
       Installed, the exact installed revision of a catalogue row on Tracks. */
    int hasOlder = 0;
    const struct CustomTrackLibraryEntry *sourceInstalled = source ? MM_CustomSourceInstalled(source,&hasOlder) : NULL;
    const struct CustomTrackLibraryEntry *owned = installed ? installed : sourceInstalled;
    if (!owned && !racer) s_modsActions = MODS_ACTIONS_CLOSED;
    tapped = MM_Mods_ActionsInput(tapped, racer ? NULL : owned, racer,
                                  racer ? s_racerMessage : s_saphiMessage, racer ? sizeof s_racerMessage : sizeof s_saphiMessage);
    if (racer) tapped = MM_ModsRacers_Input(MM_ModsRacers_Listed(racer), racer, tapped);

    if (total && (tapped & (BTN_UP | BTN_DOWN)))
    {
        *selected = (*selected + ((tapped & BTN_UP) ? total - 1 : 1)) % total;
        if (!s_saphiDownloading) { s_saphiMessage[0] = 0; s_saphiFailed = 0; }
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
        sourceInstalled = source ? MM_CustomSourceInstalled(source,&hasOlder) : NULL;
        owned = installed ? installed : sourceInstalled;
    }
    if(source && !s_saphiVersionTrack && (tapped & BTN_SQUARE))
    {
        enterVersions=source->trackID;
        s_saphiMessage[0]=0;s_saphiFailed=0;tapped=0;
    }
    /* Cross: download a track not installed or an update; on an installed
       track, its options (MM_Mods_ActionsInput). */
    if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && !racer && !MM_CustomLibrary_Busy())
    {
        if (owned)
        {
            s_modsActions = MODS_ACTIONS_OPEN;
            s_modsActionRow = MODS_ACTION_SWITCH;
            s_saphiMessage[0] = 0;
            s_saphiFailed = 0;
        }
        else if (source && !source->disabledReason[0] && !s_saphiDownloading)
        {
            s_saphiInstalling = *source;
            s_saphiDownloading = CustomSaphi_StartInstall(source,NativeAssets_GetAssetDir());
            s_saphiFailed = !s_saphiDownloading;
            snprintf(s_saphiMessage,sizeof s_saphiMessage,"%s",s_saphiDownloading ? "Downloading and checking..." : "Could not start download.");
        }
        OtherFX_Play(1,1);
    }
    first = (*selected / 7) * 7;
    for (size_t row = first; row < first + 7 && row < total; row++)
    {
        RECT box = MM_Mods_RowBox((int)(row-first));
        const char *title, *tag = NULL;
        int off = 0;
        if (row >= count)
        {
            off = MM_ModsRacers_IsOff(racers[row - count]);
            title = racers[row - count]->name;
            tag = off ? "OFF" : "RACER";
        }
        else if (s_contentLibraryTab)
        {
            const struct CustomTrackLibraryEntry *item = &s_contentLibrary.entries[visible[row]];
            off = MM_Mods_IsOff(item);
            title = item->title;
            tag = off ? "OFF" : NULL;
        }
        else
        {
            /* Tracks: OWNED, OFF or UPDATE, as on Racers. */
            const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue,visible[row]);
            int older = 0;
            const struct CustomTrackLibraryEntry *exact = MM_CustomSourceInstalled(r,&older);
            off = exact && MM_Mods_IsOff(exact);
            title = r->title;
            if (s_saphiVersionTrack)
            {
                snprintf(line,sizeof line,"v%.32s %s",r->version,(r->modeTags&1)?"Arcade":(r->modeTags&2)?"TT":"Other");
                title = line;
            }
            tag = off ? "OFF" : exact ? "OWNED" : older && !s_saphiVersionTrack ? "UPDATE" : NULL;
        }
        MM_CustomText_Draw(title,32,box.y+2,tag ? MODS_LIST_TEXT_W - 64 : MODS_LIST_TEXT_W,1,
                           row==*selected ? ORANGE : off ? GRAY : WHITE,ot);
        if (tag) MM_CustomText_Draw(tag,MODS_LIST_RIGHT,box.y+2,80,1,JUSTIFY_RIGHT | GRAY,ot);
        if (row==*selected) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
    }
    if (!total) MM_CustomText_Draw(s_saphiRefreshBusy && !s_contentLibraryTab ? "Loading tracks..." :
        *s_saphiQuery ? "No search matches" : s_contentLibraryTab ? "Nothing installed yet. Get tracks and racers from their tabs." : "Catalogue unavailable",32,84,MODS_LIST_TEXT_W,3,WHITE,ot);
    if (racer) MM_ModsRacers_DrawPanel(MM_ModsRacers_Listed(racer), racer, ot);
    else if (source || installed)
    {
        const char *author = source ? source->author : installed->author[0] ? installed->author : installed->local.author;
        const int off = owned && MM_Mods_IsOff(owned);
        MM_ModsPicture_Draw(source ? source->trackID : installed->local.trackID, installed ? installed->manifestSha256 : source->decrypted ? source->pin : "", ot);
        MM_Mods_DrawPanelHead(MODS_PANEL_SIDE_X, source ? source->title : installed->title,
                              source ? source->version : installed->version, author, ot);
        if (owned && s_modsActions != MODS_ACTIONS_CLOSED) MM_Mods_DrawActions(owned, NULL, ot);
        /* A failure takes the lines under the author, so its HTTP status shows;
           Up/Down clears it. */
        else if (s_saphiFailed && s_saphiMessage[0])
            MM_CustomText_Draw(s_saphiMessage,MODS_PANEL_X,MODS_PANEL_INFO_Y,MODS_PANEL_W,4,WHITE,ot);
        else if (source)
        {
            MM_Mods_DrawPanelBody(MM_Mods_ModesText(source->modeTags & 1, source->modeTags & 2),
                source->disabledReason[0] ? source->disabledReason : s_saphiMessage[0] ? s_saphiMessage :
                off ? "Off: not in Arcade or Time Trial" : sourceInstalled ? "Installed" :
                hasOlder ? "Update available" : "Not installed",
                s_saphiDownloading ? "Downloading..." : source->disabledReason[0] ? "" :
                sourceInstalled ? "* Options" : hasOlder ? "* Update" : "* Download", ot);
        }
        else
        {
            /* Installed: which custom pages list the track, and if one does
               not, why (native_custom_content_verify.h); a revision the
               store scan refused says why it is not installed. */
            const char *why = !installed->local.arcade ?
                (installed->local.timeTrial ? installed->local.arcadeReason : installed->local.timeTrialReason) : "";
            MM_Mods_DrawPanelBody(!installed->installed ? "Not installed" : off ? "Off" :
                                  MM_Mods_ModesText(installed->local.arcade, installed->local.timeTrial),
                                  s_saphiMessage[0] ? s_saphiMessage : !installed->installed ? installed->disabledReason :
                                  off ? "Not in Arcade or Time Trial" : why[0] ? why : "Files verified",
                                  "* Options", ot);
        }
    }
    {
        char busy[256];
        const int saphiBusy = ap_custom_track_download_busy(busy, sizeof busy);
        MM_Mods_DrawFooter(saphiBusy ? busy : NULL, s_saphiVersionTrack ? "" : !s_contentLibraryTab ? "[ Versions   R2 Search" : "R2 Search", 1,
                           *selected, total, s_saphiVersionTrack ? "^: Tracks" : NULL, ot);
    }
    MM_Mods_DrawBoxes(ot);
    if(enterVersions) { s_saphiVersionTrack=enterVersions;s_saphiBrowseSelection=*selected;*selected=0; }
}
