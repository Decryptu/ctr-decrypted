#include <platform/native_menu.h>
/* Custom pages at the normal track selector in Arcade and Time Trial, in
   every client with packages (MM_CUSTOM_TIME_TRIAL).
   Included by MM_TrackSelect.c inside CTR_CUSTOM_PACKAGES. Custom pages use
   owned library metadata, never indices into retail track/ghost tables.

   One custom track at a time: Cross prepares the chosen installed package,
   the second Cross hands it to the offline loader (native_custom_offline.h)
   and starts a one-player Arcade single race or a Time Trial under the track's
   own identity, CTR_CUSTOM_LEVEL_ID (native_custom_identity.h). The engine
   reads the package's LEV and VRM through one arcade BIGFILE subfile group;
   which group that is, is a loading detail below and never the track's
   identity. Leaving the race ends the custom runtime; Retry keeps it.

   Every listed revision shows, Ready or not, except a track turned off on
   the Mods screen (MM_Mods_IsOff). One that cannot race in this mode is drawn
   dimmed and says why (CustomTrackLibrary_Admission). In a room a race runs
   under AP_ArcadeHoldsRoom: its checks and items stay out of the room, and it
   never touches the room's pins. A Time Trial reads
   and writes its best times and ghost in custom-records/
   (native_custom_records.h), never the memory card. */
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_offline.h>
#include <platform/native_custom_music.h>
#include <platform/native_assets.h>
#include <platform/native_custom_revision.h>
#include <platform/native_input.h> // Platform_InputRawGamepadButtons

#include <platform/native_custom_identity.h>
#include <platform/native_custom_records.h>
#include <platform/native_track_minimap.h>
#include <platform/native_racers.h>
#include <ctr_speed_class.h>

/* The host slot (native_custom_identity.h); tools/test-custom-identity.c
   audits what the custom identity covers. */
#define MM_CUSTOM_ARCADE_HOST_LEVEL CTR_CUSTOM_ARCADE_HOST_LEVEL

/* The page title sits between the game's own arrow icon, pointing each way,
   as on the hub pause screen (icon 0x38 of iconGroup[4]). The font cannot
   do it: its '<' is a left arrow but its '>' is a dash. */
#define MM_CUSTOM_PAGE_ARROW_GAP 14

static void MM_CustomTrackSelect_DrawPageArrows(int centerX, int y, int titleWidth, u32 *ot)
{
    struct GameTracker *gGT = sdata->gGT;
    struct Icon **icons = ICONGROUP_GETICONS(gGT->iconGroup[4]);
    u32 *colors = data.ptrColor[ORANGE];
    const int x[2] = {centerX - titleWidth / 2 - MM_CUSTOM_PAGE_ARROW_GAP, centerX + titleWidth / 2 + MM_CUSTOM_PAGE_ARROW_GAP};
    const int rotation[2] = {0x800, 0};

    for (int i = 0; i < 2; i++)
    {
        DecalHUD_Arrow2D(icons[0x38], x[i], y + data.font_charPixHeight[FONT_SMALL] / 2, &gGT->backBuffer->primMem, ot,
                         ColorCode_Load(&colors[0]), ColorCode_Load(&colors[1]), ColorCode_Load(&colors[2]),
                         ColorCode_Load(&colors[3]), 0, 0x1000, rotation[i]);
    }
}

/* Every client with packages has the Time Trial pages: the player client got
   them on the ruling of 2026-11-02 (the plan's "Single Race and Time Trial
   both"). Best times and the ghost live in custom-records/ there as in the
   authoring builds. 0 would close the Time Trial pages again. */
#define MM_CUSTOM_TIME_TRIAL 1

static struct CustomTrackLibrary s_arcadeLibrary;
static size_t s_arcadePage;
static size_t s_arcadeRows[CTR_LIBRARY_MAX / CTR_LIBRARY_PAGE_ROWS + 1];
/* The scan the page's rows were built from. A menu entry reuses them, so only
   a scan that really ran costs a rebuild. */
static unsigned int s_arcadeGeneration = ~0u;
static int s_arcadeSearching, s_arcadeSearchPadPrev;
static char s_arcadeQuery[CTR_LIBRARY_QUERY_MAX + 1], s_arcadeQueryBackup[CTR_LIBRARY_QUERY_MAX + 1];
static int s_arcadePrepareBusy;
static int s_arcadeConfirm;
static int s_arcadeMotion, s_arcadeMotionDirection;
static struct CustomOfflineRequest *s_arcadeRequest;
static char s_arcadeMessage[512];
static int s_customTimeTrialPending;

/* CTR_OFFLINE_MODE_ARCADE or _TIME_TRIAL, 0 where custom pages do not show. */
static int MM_CustomTrackSelect_Mode(void)
{
    u32 mode1 = sdata->gGT->gameMode1;
    if ((mode1 & (BATTLE_MODE | ADVENTURE_MODE | ADVENTURE_CUP)) || (sdata->gGT->gameMode2 & CUP_ANY_KIND)) return 0;
    if ((mode1 & (ARCADE_MODE | TIME_TRIAL)) == ARCADE_MODE) return CTR_OFFLINE_MODE_ARCADE;
    if (MM_CUSTOM_TIME_TRIAL && (mode1 & (ARCADE_MODE | TIME_TRIAL)) == TIME_TRIAL) return CTR_OFFLINE_MODE_TIME_TRIAL;
    return 0;
}

static int MM_CustomTrackSelect_Enabled(void)
{
    return MM_CustomTrackSelect_Mode() != 0;
}

/* Whether a listed revision is Ready for this page's mode, and if not, a
   sentence for the detail panel (three 13-character lines at most). */
static int MM_CustomTrackSelect_Eligible(const struct CustomTrackLibraryEntry *entry, char *why, size_t whySize)
{
    int timeTrial = MM_CustomTrackSelect_Mode() == CTR_OFFLINE_MODE_TIME_TRIAL;
    enum CustomTrackAdmission admission = CustomTrackLibrary_Admission(entry, timeTrial);
    if (why && whySize) why[0] = 0;
    if (!entry) return 0;
    if (admission == CTR_ADMISSION_READY) return 1;
    if (why && whySize)
    {
        if (admission == CTR_ADMISSION_DISCOVERED)
            snprintf(why, whySize, "%s", entry->disabledReason[0] ? entry->disabledReason : "Not installed");
        else if (!timeTrial && entry->local.timeTrial && MM_CUSTOM_TIME_TRIAL)
            snprintf(why, whySize, "%s. Use Time Trial.", entry->local.arcadeReason);
        else
            snprintf(why, whySize, "%s", timeTrial ? entry->local.timeTrialReason : entry->local.arcadeReason);
    }
    return 0;
}

/* MM_TrackSelect.c, leaving the menu with a Time Trial chosen: a custom
   track loads its saved ghost from custom-records/ into the retail playing
   buffer and goes straight to the load, skipping the memory card's ghost
   selection. Returns 0 for retail tracks. */
static int MM_CustomTrackSelect_TimeTrialStart(void)
{
    if (!s_customTimeTrialPending) return 0;
    s_customTimeTrialPending = 0;
    if (CustomOffline_RuntimeMode() != CTR_OFFLINE_MODE_TIME_TRIAL) return 0;
    TimeTrialRecords_BeginFromTrackSelect();
    return 1;
}

static void MM_CustomTrackSelect_Init(void)
{
    if (!MM_CustomTrackSelect_Enabled()) return;
    /* Vanilla always opens first; row positions on custom pages survive Back. */
    s_arcadePage = 0;
    s_arcadeMotion = 0;
    s_arcadeConfirm = 0;
    s_arcadeMessage[0] = 0;
    s_customTimeTrialPending = 0;
    s_arcadeSearching = 0;
    s_arcadeQuery[0] = 0;
    CustomTrackLibrary_Filter(&s_arcadeLibrary, s_arcadeQuery, 0, 0);
    CustomOffline_Free(&s_arcadeRequest);
    /* The first entry loads and scans; later ones keep the cached rows. An
       install or the page's own L2 asks for a new scan. */
    MM_CustomLibrary_Tick(0);
}

/* After new rows or a new filter: the remembered revision's page and row, by
   pin. A revision that left the list leaves the page as it was. */
static void MM_CustomTrackSelect_FollowPin(void)
{
    const struct CustomTrackLibraryEntry *held = CustomTrackLibrary_Selected(&s_arcadeLibrary);
    size_t at = s_arcadeLibrary.selected;
    if (!held || strcmp(held->manifestSha256, s_arcadeLibrary.rememberedPin)) return;
    s_arcadeRows[at / CTR_LIBRARY_PAGE_ROWS] = at % CTR_LIBRARY_PAGE_ROWS;
    if (s_arcadePage) s_arcadePage = at / CTR_LIBRARY_PAGE_ROWS + 1;
}

/* The search box: the same keyboard entry and pad exits as the Track Manager. */
static void MM_CustomTrackSelect_SearchTick(void)
{
    int rawPad = Platform_InputRawGamepadButtons();
    int rawTapped = rawPad & ~s_arcadeSearchPadPrev;
    s_arcadeSearchPadPrev = rawPad;
    if (!s_arcadeSearching) return;
    if (!NativeText_Result())
    {
        if (rawTapped & (RAW_BTN_TRIANGLE | RAW_BTN_START)) NativeText_Resolve(2);
        else if (rawTapped & (RAW_BTN_CROSS | RAW_BTN_CIRCLE)) NativeText_Resolve(1);
    }
    if (NativeText_Result())
    {
        if (NativeText_Result() == 2) strcpy(s_arcadeQuery, s_arcadeQueryBackup);
        NativeText_End();
        s_arcadeSearching = 0;
    }
    if (strcmp(s_arcadeQuery, s_arcadeLibrary.query))
    {
        CustomTrackLibrary_Filter(&s_arcadeLibrary, s_arcadeQuery, 0, 0);
        MM_CustomTrackSelect_FollowPin();
    }
}

/* The Vanilla page's preview frame and lap box (MM_TrackSelect.c's enum, which
   comes after this file). */
#define MM_CUSTOM_PREVIEW_X 0x134
#define MM_CUSTOM_PREVIEW_W 0xb0
#define MM_CUSTOM_PREVIEW_H 0x4b
#define MM_CUSTOM_PICTURE_W 0xaa
#define MM_CUSTOM_PICTURE_H 0x47
#define MM_CUSTOM_LAP_MENU_WIDTH 0xa4

/* The highlighted track's minimap, read from its package
   (native_track_minimap.h) once per track and kept in VRAM. */
static char s_mapPin[65];
static int s_mapLoaded;
static struct Icon *s_mapTop, *s_mapBottom;

static int MM_CustomTrackSelect_Map(const struct CustomTrackLibraryEntry *entry, struct Icon **top, struct Icon **bottom)
{
    if (!entry->installed) return 0;
    if (strcmp(s_mapPin, entry->manifestSha256))
    {
        static struct NativeTrackMinimap map;
        snprintf(s_mapPin, sizeof s_mapPin, "%s", entry->manifestSha256);
        s_mapLoaded = NativeTrackMinimap_LoadPackage(NativeAssets_GetAssetDir(), entry->manifestSha256, &map);
        if (s_mapLoaded) NativeRacers_TrackMinimap(&map, &s_mapTop, &s_mapBottom);
    }
    *top = s_mapTop;
    *bottom = s_mapBottom;
    return s_mapLoaded;
}

/* Hand the prepared track to the loader and leave for the race. laps is the
   lap box's count in Arcade, 0 for the package's own (Time Trial). */
static void MM_CustomTrackSelect_Start(int laps)
{
    struct GameTracker *gGT = sdata->gGT;
    const int host = MM_CUSTOM_ARCADE_HOST_LEVEL;
    const int mode = MM_CustomTrackSelect_Mode();
    s_arcadeConfirm = 0;
    D230.trackSelect.lapBoxOpen = 0;
    if (gGT->gameMode2 & CHEAT_ONELAP) laps = 1;
    if (AP_CustomOfflineLaunchAllowed() && gGT->numPlyrNextGame == 1 &&
        CustomOffline_BeginRuntime(&s_arcadeRequest, host, 0, mode))
    {
        /* Preserve the players, difficulty and characters chosen in Arcade. */
        if (laps) CustomOffline_SetRuntimeLaps(laps);
        gGT->currLEV = host;
        gGT->numLaps = CustomOffline_RuntimeLaps();
        /* No .sca: an imported track plays the retail music of the track it
           replaces, any other plays Crash Cove's. */
        {
            const size_t page = s_arcadePage ? s_arcadePage - 1 : 0;
            const struct CustomTrackLibraryEntry *chosen =
                CustomTrackLibrary_Row(&s_arcadeLibrary, page * CTR_LIBRARY_PAGE_ROWS + s_arcadeRows[page]);
            struct CustomPackageManifest running;
            /* Only for the entry that was prepared (same pin). */
            CustomOffline_SetMusicFallbackLevel(chosen && chosen->local.xdelta &&
                CustomOffline_RuntimeManifest(&running) && !strcmp(running.sha256, chosen->manifestSha256) ?
                CustomMusic_ReplacedLevel(chosen->local.bigfileIndex) : -1);
        }
        D230.trackSelect.transition.startAfterExit = 1;
        D230.trackSelect.transition.state = EXITING_MENU;
        s_customTimeTrialPending = mode == CTR_OFFLINE_MODE_TIME_TRIAL;
        fprintf(stderr, "[CustomSelector] custom track %d (bytes via group %d) mode=%s players=%d difficulty=%d laps=%d\n",
            CTR_CUSTOM_LEVEL_ID, host, mode == CTR_OFFLINE_MODE_TIME_TRIAL ? "time trial" : "arcade",
            gGT->numPlyrNextGame, gGT->arcadeDifficulty, gGT->numLaps);
        OtherFX_Play(1, 1);
    }
    else
    {
        snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Could not start this track.");
        CustomOffline_Free(&s_arcadeRequest);
    }
}

/* Called after the retail transition state advances, before retail indexing.
   Returning 1 owns this frame; Vanilla returns to the unchanged retail proc. */
static int MM_CustomTrackSelect_Tick(void)
{
    struct GameTracker *gGT = sdata->gGT;
    uint32_t *ot = gGT->backBuffer->otMem.uiOT;
    size_t pages, rows, row, first;
    const struct CustomTrackLibraryEntry *entry = NULL;
    int tapped = sdata->buttonTapPerPlayer[0];
    char line[256], label[16];
    if (!MM_CustomTrackSelect_Enabled()) return 0;

    if (!s_arcadePrepareBusy) MM_CustomLibrary_Tick(0);
    MM_ModsPicture_Tick();
    if (s_arcadeGeneration != s_contentStoreGeneration && !MM_CustomLibrary_Busy())
    {
        /* Copy entries, not the manager's query/Current Seed filter. One row
           per track: the newest revision that can run in this mode, else the
           newest revision. A revision with no track id keeps its own row, so
           one that failed its checks still shows with its reason. */
        struct CustomTrackLibraryEntry *eligible = calloc(s_contentLibrary.count ? s_contentLibrary.count : 1, sizeof *eligible);
        size_t count = 0;
        if (eligible)
        {
            for (size_t i=0;i<s_contentLibrary.count;i++)
            {
                const struct CustomTrackLibraryEntry *item=&s_contentLibrary.entries[i];
                if (MM_Mods_IsOff(item)) continue;
                size_t at=0;
                for (;at<count;at++)
                    if (item->local.uuid[0] && !strcmp(item->local.uuid,eligible[at].local.uuid)) break;
                if (at==count) eligible[count++]=*item;
                else
                {
                    int itemOk = MM_CustomTrackSelect_Eligible(item, NULL, 0);
                    int heldOk = MM_CustomTrackSelect_Eligible(&eligible[at], NULL, 0);
                    if (itemOk != heldOk ? itemOk : CustomRevision_Compare(item->version,eligible[at].version)>0)
                        eligible[at]=*item;
                }
            }
            CustomTrackLibrary_Replace(&s_arcadeLibrary, eligible, count);
            MM_CustomTrackSelect_FollowPin();
            free(eligible);
        }
        s_arcadeGeneration = s_contentStoreGeneration;
    }
    /* The frame that ends the entry belongs to it too: its Cross or Triangle
       must not also pick a track or leave the page. */
    if (s_arcadeSearching) tapped = 0;
    MM_CustomTrackSelect_SearchTick();
    pages = CustomTrackLibrary_PageCount(&s_arcadeLibrary) + 1;
    if (s_arcadePage >= pages) s_arcadePage = pages - 1;
    if (D230.trackSelect.transition.state != IN_MENU || D230.trackSelect.lapBoxOpen ||
        D230.trackSelect.trackChangeFrames || s_arcadeMotion) tapped = 0;

    if (s_arcadePrepareBusy)
    {
        int result = CustomOffline_PollPrepare(&s_arcadeRequest, s_arcadeMessage, sizeof s_arcadeMessage);
        if (result != 0)
        {
            s_arcadePrepareBusy = 0;
            if (result == 1 && !CustomOffline_CheckStructure(s_arcadeRequest, MM_CustomTrackSelect_Mode(),
                                                             s_arcadeMessage, sizeof s_arcadeMessage))
                CustomOffline_Free(&s_arcadeRequest);
            else if (result == 1)
            {
                s_arcadeMessage[0] = 0;
                if (MM_CustomTrackSelect_Mode() == CTR_OFFLINE_MODE_TIME_TRIAL) MM_CustomTrackSelect_Start(0);
                else s_arcadeConfirm = D230.trackSelect.lapBoxOpen = 1;
            }
        }
        tapped = 0;
    }
    if (!s_arcadePrepareBusy && !s_arcadeConfirm && (tapped & (BTN_LEFT | BTN_RIGHT)))
    {
        s_arcadePage = (s_arcadePage + ((tapped & BTN_LEFT) ? pages - 1 : 1)) % pages;
        s_arcadeMessage[0] = 0;
        MM_TrackSelect_Video_State(1);
        OtherFX_Play(0, 1);
        RECTMENU_ClearInput();
        tapped = 0;
    }
    if (!s_arcadePage)
    {
        if (D230.trackSelect.transition.state == IN_MENU && !D230.trackSelect.lapBoxOpen)
        {
            DecalFont_DrawLineOT("Vanilla", 394, 178, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
            MM_CustomTrackSelect_DrawPageArrows(394, 178, DecalFont_GetLineWidth("Vanilla", FONT_SMALL), ot);
            snprintf(line, sizeof line, "Page 1/%u", (unsigned)pages);
            DecalFont_DrawLineOT(line, 394, 190, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
        }
        /* Drain an in-flight store scan before leaving this menu. */
        if (MM_CustomLibrary_Busy()) RECTMENU_ClearInput();
        return 0;
    }

    rows = CustomTrackLibrary_PageRowCount(&s_arcadeLibrary, s_arcadePage - 1);
    row = s_arcadeRows[s_arcadePage - 1];
    if (row >= rows) row = 0;
    first = (s_arcadePage - 1) * CTR_LIBRARY_PAGE_ROWS;
    if (s_arcadeConfirm)
    {
        /* The retail lap box, as on the Vanilla page (MM_TrackSelect_MenuProc):
           its laps replace the package's own for this Arcade race. */
        int lapResult = 0;
        D230.menuLapSel.rowSelected = sdata->uselessLapRowCopy;
        if (D230.trackSelect.transition.state == IN_MENU) lapResult = (s16)RECTMENU_ProcessInput(&D230.menuLapSel);
        RECTMENU_DrawSelf(&D230.menuLapSel, D230.trackTransitions.transitionMeta_trackSel[2].currX, D230.trackTransitions.transitionMeta_trackSel[2].currY,
                          MM_CUSTOM_LAP_MENU_WIDTH);
        sdata->uselessLapRowCopy = D230.menuLapSel.rowSelected;
        if (lapResult == 1) MM_CustomTrackSelect_Start(D230.lapCountByRow[D230.menuLapSel.rowSelected].lapCount);
        else if (lapResult == -1)
        {
            s_arcadeConfirm = 0;
            D230.trackSelect.lapBoxOpen = 0;
            CustomOffline_Free(&s_arcadeRequest);
        }
        tapped = 0;
    }
    else if (!s_arcadePrepareBusy)
    {
        if (rows && (tapped & (BTN_UP | BTN_DOWN)))
        {
            row = (row + ((tapped & BTN_UP) ? rows - 1 : 1)) % rows;
            s_arcadeMotion = rows > 1 ? 3 : 0;
            s_arcadeMotionDirection = (tapped & BTN_UP) ? -1 : 1;
            s_arcadeMessage[0] = 0;
            OtherFX_Play(0, 1);
        }
        if (tapped & BTN_R2)
        {
            strcpy(s_arcadeQueryBackup, s_arcadeQuery);
            NativeText_Begin(s_arcadeQuery, sizeof s_arcadeQuery, 28, 180, 456, 12, 0);
            s_arcadeSearching = 1;
            tapped = 0;
        }
        if ((tapped & BTN_L2) && !MM_CustomLibrary_Busy())
        {
            MM_CustomLibrary_Tick(1);
            s_arcadeMessage[0] = 0;
        }
        if (!MM_CustomLibrary_Busy() && (tapped & (BTN_TRIANGLE | BTN_START)))
        {
            D230.trackSelect.transition.startAfterExit = 0;
            D230.trackSelect.transition.state = EXITING_MENU;
            OtherFX_Play(2, 1);
        }
        entry = rows ? CustomTrackLibrary_Row(&s_arcadeLibrary, first + row) : NULL;
        if (entry && !MM_CustomLibrary_Busy() && (tapped & (BTN_CROSS_one | BTN_CIRCLE)))
        {
            char why[260];
            if (!MM_CustomTrackSelect_Eligible(entry, why, sizeof why))
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "%s", why);
            else if (!AP_CustomOfflineLaunchAllowed())
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "A room race is still loaded. Try again from the hub.");
            else if (gGT->numPlyrNextGame != 1)
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "Custom tracks are one player only. Choose one player.");
            else
            {
                s_arcadePrepareBusy = CustomOffline_StartPrepare(NativeAssets_GetAssetDir(), entry->manifestSha256);
                snprintf(s_arcadeMessage, sizeof s_arcadeMessage, "%s", s_arcadePrepareBusy ?
                    "Checking selected track..." : "Could not prepare this revision.");
            }
        }
    }
    s_arcadeRows[s_arcadePage - 1] = row;
    entry = rows ? CustomTrackLibrary_Row(&s_arcadeLibrary, first + row) : NULL;
    if (entry) memcpy(s_arcadeLibrary.rememberedPin, entry->manifestSha256, sizeof s_arcadeLibrary.rememberedPin);
    RECTMENU_ClearInput();

    /* The Vanilla page's layout (MM_TrackSelect_MenuProc): curved big-font
       rows, SELECT LEVEL over the track's picture, its minimap under it, and
       the page under the map. */
    struct TransitionMeta *t = D230.trackTransitions.transitionMeta_trackSel;
    for (size_t i = 0; i < rows; i++)
    {
        const struct CustomTrackLibraryEntry *item = CustomTrackLibrary_Row(&s_arcadeLibrary, first + i);
        int offset = (int)i - (int)row;
        if (offset > (int)rows / 2) offset -= (int)rows;
        if (offset < -(int)rows / 2) offset += (int)rows;
        if (offset == 4) offset = -4;
        int angle = offset * 0x73 + s_arcadeMotionDirection * s_arcadeMotion * 0x73 / 3;
        RECT box = {
            t[0].currX + (MATH_Cos(angle) * 25 >> 9) - 180,
            t[0].currY + (MATH_Sin(angle) * 200 >> 12) + 96,
            256, 25
        };
        MM_CustomText_DrawFont(item->title, box.x + 8, box.y + 5, 240, 1, FONT_BIG,
                               MM_CustomTrackSelect_Eligible(item, NULL, 0) ? ORANGE : ORANGE_DARKENED, ot);
        if (i == row && !s_arcadeMotion)
        {
            RECT highlight = {box.x + 6, box.y + 4, box.w - 12, box.h - 8};
            CTR_Box_DrawClearBox(&highlight, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
        }
        RECTMENU_DrawInnerRect(&box, 0, ot);
    }
    if (s_arcadeMotion) s_arcadeMotion--;

    /* Queued before the map, as on the Vanilla page, so it draws over it. */
    if (D230.trackSelect.transition.state == IN_MENU && !s_arcadeConfirm)
    {
        CustomTrackLibrary_PageLabel(&s_arcadeLibrary, s_arcadePage - 1, label, sizeof label);
        snprintf(line, sizeof line, "Custom %s", label);
        DecalFont_DrawLineOT(line, 394, 178, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
        MM_CustomTrackSelect_DrawPageArrows(394, 178, DecalFont_GetLineWidth(line, FONT_SMALL), ot);
        snprintf(line, sizeof line, "Page %u/%u", (unsigned)s_arcadePage + 1, (unsigned)pages);
        DecalFont_DrawLineOT(line, 394, 190, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
        /* The search box's text, or the two page controls: 248 pixels, to
           the screen's right edge, hold "R2 Search L2 Rescan". */
        if (s_arcadeSearching || s_arcadeQuery[0])
            snprintf(line, sizeof line, "Search: %s%s", s_arcadeQuery, s_arcadeSearching ? "_" : "");
        else
            snprintf(line, sizeof line, "R2 Search L2 Rescan");
        MM_CustomText_Draw(line, 386, 202, 248, 1, JUSTIFY_CENTER | GRAY, ot);
    }

    struct Icon *mapTop = NULL, *mapBottom = NULL;
    const int hasMap = entry && !s_arcadeMotion && MM_CustomTrackSelect_Map(entry, &mapTop, &mapBottom);
    RECT preview = {t[1].currX + MM_CUSTOM_PREVIEW_X, t[1].currY + (hasMap ? 5 : 0x3a), MM_CUSTOM_PREVIEW_W,
                    MM_CUSTOM_PREVIEW_H};
    DecalFont_DrawLineOT("SELECT", t[3].currX + 0x18c, t[3].currY + preview.y, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    DecalFont_DrawLineOT("LEVEL", t[3].currX + 0x18c, t[3].currY + preview.y + 0x10, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
    preview.y += 0x22;
    if (hasMap && !s_arcadeConfirm)
    {
        /* The six passes of the Vanilla page (shadow, outline, map), centred
           in the box under the picture as there. */
        const int mapW = mapTop->texLayout.u1 - mapTop->texLayout.u0 > mapBottom->texLayout.u1 - mapBottom->texLayout.u0 ?
                         mapTop->texLayout.u1 - mapTop->texLayout.u0 : mapBottom->texLayout.u1 - mapBottom->texLayout.u0;
        const int mapH = (mapTop->texLayout.v2 - mapTop->texLayout.v0) + (mapBottom->texLayout.v2 - mapBottom->texLayout.v0);
        const int mapX = preview.x + t[2].currX - t[1].currX;
        const int mapY = preview.y + t[2].currY - t[1].currY + 0x49;
        for (int layer = 0; layer < 6; layer++)
        {
            const struct TrackSelectMapOffset *o = &D230.drawMapOffset[layer];
            UI_Map_DrawMap(mapTop, mapBottom, o->offsetX + mapX + preview.w / 2 + mapW / 2,
                           o->offsetY + mapY + 100 / 2 + mapH / 2, &gGT->backBuffer->primMem,
                           gGT->pushBuffer_UI.ptrOT, o->type);
        }
    }
    {
        int none = 1;
        struct Icon *picture = entry && !s_arcadeMotion ?
            MM_ModsPicture_Icon(entry->local.trackID, entry->manifestSha256, &none) : NULL;
        /* In a room the lap box's race stays out of it (AP_ArcadeHoldsRoom):
           the note says so over the picture. */
        const char *note = s_arcadeConfirm ? AP_CustomOfflineRoomNote() : "";
        const char *message = s_arcadeMessage[0] ? s_arcadeMessage : note[0] ? note :
            !entry ? (MM_CustomLibrary_Busy() ? "Loading tracks..." : s_arcadeQuery[0] ? "No search matches" :
                      "No tracks. Get them from MODS on the title screen.") : NULL;
        if (message)
            MM_CustomText_Draw(message, preview.x + preview.w / 2, preview.y + 10, preview.w - 12, 4,
                               JUSTIFY_CENTER | WHITE, ot);
        else if (picture)
        {
            /* The picture fills the frame as the retail video does: one quad,
               not an icon, which widescreen would narrow to keep it square. */
            POLY_FT4 *p = (POLY_FT4 *)gGT->backBuffer->primMem.cursor;
            if ((u32)p <= (u32)gGT->backBuffer->primMem.guardEnd)
            {
                const int x = preview.x + 3, y = preview.y + 2;
                const int w = MM_CUSTOM_PICTURE_W, h = MM_CUSTOM_PICTURE_H;
                u32 *uiOT = gGT->pushBuffer_UI.ptrOT;
                p->code = 0x2d;
                CtrGpu_WritePackedUVWord(&p->u0, CTR_ReadU32LE(&picture->texLayout.u0));
                CtrGpu_WritePackedUVWord(&p->u1, CTR_ReadU32LE(&picture->texLayout.u1));
                CtrGpu_WritePackedUV(&p->u2, CTR_ReadU16LE(&picture->texLayout.u2));
                CtrGpu_WritePackedUV(&p->u3, CTR_ReadU16LE(&picture->texLayout.u3));
                CtrGpu_WritePackedXY(&p->x0, (u32)(u16)x | ((u32)y << 16));
                CtrGpu_WritePackedXY(&p->x1, (u32)(u16)(x + w) | ((u32)y << 16));
                CtrGpu_WritePackedXY(&p->x2, (u32)(u16)x | ((u32)(y + h) << 16));
                CtrGpu_WritePackedXY(&p->x3, (u32)(u16)(x + w) | ((u32)(y + h) << 16));
                p->tag = *uiOT | 0x09000000;
                *uiOT = CtrGpu_PrimToOTLink24(p);
                gGT->backBuffer->primMem.cursor = p + 1;
            }
        }
        (void)none;
    }
    RECTMENU_DrawInnerRect(&preview, 1, gGT->backBuffer->otMem.uiOT);

    return 1;
}
