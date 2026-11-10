// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-selector.c
// platform/native_custom_track_library.c platform/native_custom_records.c -lm -o /tmp/test-custom-selector
/* Exercise the Arcade custom pages with renderer and worker boundaries stubbed.
   Built as is for the authoring builds, and with CTR_AP through
   test-custom-selector-player.c for the player client. This proves navigation
   and the handoff to the offline loader on host slot 6, not rendered
   acceptance. */
#include <math.h>
#include "menu-label-stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <common.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_offline.h>
#include <platform/native_assets.h>
#include <platform/native_custom_track_manager.h>

struct sData sdata_static;
struct Data data;
#define DecalFont_DrawLineOT HarnessUnusedDrawLineOT
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT
int MATH_Sin(u32 angle) { return (int)(4096 * sin((angle & 4095) * 6.283185307179586 / 4096)); }
int MATH_Cos(u32 angle) { return MATH_Sin(angle + 1024); }
#include "../game/230/MM_CustomText.c"

struct OverlayDATA_230 D230;
static struct GameTracker tracker;
static struct DB buffer;
static struct CustomTrackLibrary s_contentLibrary;
static unsigned int s_contentStoreGeneration;
static int busy, allowed = 1, prepared, launched, reloads, ticksSeen, checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void MM_CustomLibrary_Tick(int reload) { reloads += reload; ticksSeen++; }
static int MM_CustomLibrary_Busy(void) { return busy; }
/* The Mods screen's off switch (MM_CustomManager.c): one track title is off. */
static const char *offTitle = "";
static int MM_Mods_IsOff(const struct CustomTrackLibraryEntry *e) { return !strcmp(e->title, offTitle); }
int AP_CustomOfflineLaunchAllowed(void) { return allowed; }
static const char *roomNote = "";
const char *AP_CustomOfflineRoomNote(void) { return roomNote; }
static int rawPad, textResult, textActive;
static char *textBuffer;
int Platform_InputRawGamepadButtons(void) { return rawPad; }
void NativeText_Begin(char *buf, int cap, int x, int y, int w, int h, int password)
{ (void)cap; (void)x; (void)y; (void)w; (void)h; (void)password; textBuffer = buf; textActive = 1; textResult = 0; }
void NativeText_End(void) { textActive = 0; textResult = 0; }
int NativeText_Active(void) { return textActive; }
int NativeText_Result(void) { return textResult; }
void NativeText_Resolve(int result) { textResult = result; }
const char *NativeAssets_GetAssetDir(void) { return "/unused"; }
int OtherFX_Play(u32 sound, int flags) { (void)sound; (void)flags; return 0; }
void RECTMENU_ClearInput(void) { memset(sdata->buttonTapPerPlayer, 0, sizeof sdata->buttonTapPerPlayer); }
void MM_TrackSelect_Video_State(int state) { (void)state; }
/* Every string drawn this tick, for the reason and hint checks. */
static char drawn[4096];
static int drawnHas(const char *text) { return strstr(drawn, text) != NULL; }
void DecalFont_DrawLineOT(char *text, s16 x, s16 y, s16 font, s16 flags, u32 *ot)
{
    (void)ot;
    if (strlen(drawn) + strlen(text) + 2 < sizeof drawn) { strcat(drawn, text); strcat(drawn, "\n"); }
    int width = DecalFont_GetLineWidth(text, font);
    if (flags & JUSTIFY_CENTER) x -= width / 2;
    /* Retail curved rows intentionally enter/leave beyond the left/top edge.
       Detail and footer text must fit; every title fits its 240px row inset. */
    CHECK(x + width <= 512);
    if (font == FONT_SMALL) CHECK(x >= 0 && y >= 0 && y + 8 <= 216);
    else if (!(flags & JUSTIFY_CENTER)) CHECK(width <= 240);
}
static void box_check(const RECT *box)
{
    CHECK(box->x + box->w <= 512);
}
void RECTMENU_DrawInnerRect(RECT *r, s16 x, u32 *ot) { (void)x; (void)ot; box_check(r); }
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, s32 t, u32 *ot, struct PrimMem *primMem)
{ (void)c; (void)t; (void)ot; (void)primMem; box_check(r); }
void CTR_Box_DrawSolidBox(RECT *r, const Color *c, u32 *ot, struct PrimMem *primMem) { (void)c; (void)ot; (void)primMem; box_check(r); }
/* The page arrows: icon 0x38 of icon group 4, in orange. */
static struct { struct IconGroup group; struct Icon *icons[0x39]; } arrowGroup;
static struct Icon arrowIcon;
static u32 orange[4];
static int arrows;
void DecalHUD_Arrow2D(struct Icon *icon, s32 posX, s32 posY, struct PrimMem *primMem, u32 *ot, Color color0, Color color1,
                      Color color2, Color color3, u8 transparency, s16 scale, u16 rot)
{
    (void)posY; (void)primMem; (void)ot; (void)color0; (void)color1; (void)color2; (void)color3;
    (void)transparency; (void)scale; (void)rot;
    arrows++;
    CHECK(icon == &arrowIcon);
    CHECK(posX >= 0 && posX <= 512);
}
int CustomOffline_StartPrepare(const char *assets, const char *pin)
{ (void)assets; (void)pin; prepared++; return 1; }
int CustomOffline_PollPrepare(struct CustomOfflineRequest **out, char *e, size_t n)
{ (void)e; (void)n; *out = (struct CustomOfflineRequest *)1; return 1; }
void CustomOffline_Free(struct CustomOfflineRequest **out) { *out = NULL; }
static int checkedMode = -1;
int CustomOffline_CheckStructure(const struct CustomOfflineRequest *r, int mode, char *e, size_t n)
{ (void)r; (void)e; (void)n; checkedMode = mode; return 1; }
int CustomOffline_RetainPackage(const struct CustomOfflineRequest *r, struct CustomPackageOwned **out)
{ (void)r; *out = (struct CustomPackageOwned *)1; return 1; }
int CustomPackage_GetRaceLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ (void)p; (void)e; (void)n; *laps = 7; return 1; }
int CustomOffline_PackageLaps(const struct CustomPackageOwned *p, unsigned int *laps, char *e, size_t n)
{ return CustomPackage_GetRaceLaps(p, laps, e, n); }
void CustomPackage_Free(struct CustomPackageOwned **p) { *p = NULL; }
int CustomOffline_RuntimeLaps(void) { return 7; }
static int beganHost = -1, beganMode, ghostOnFile, ghostLoads;
int CustomOffline_BeginRuntime(struct CustomOfflineRequest **r, int host, int seed, int mode)
{
    if (!r || !*r || seed) return 0;
    launched++; *r = NULL; beganHost = host; beganMode = mode; return 1;
}
int CustomOffline_RuntimeMode(void) { return beganMode; }
static char runningPin[65];
static int fallbackSet = -2;
int CustomOffline_RuntimeManifest(struct CustomPackageManifest *m) { memset(m, 0, sizeof *m); memcpy(m->sha256, runningPin, 65); return 1; }
void CustomOffline_SetMusicFallbackLevel(int level) { fallbackSet = level; }
static unsigned char highMem[0x3e00];
void *MEMPACK_AllocHighMem(s32 size, const char *name) { (void)name; CHECK(size == 0x3e00); memset(highMem, 0xee, sizeof highMem); return highMem; }
int TimeTrialRecords_LoadGhost(struct GhostHeader *dst)
{
    ghostLoads++;
    if (!ghostOnFile) return 0;
    dst->version = -4; dst->characterID = COCO_BANDICOOT; dst->size = 0x100; dst->timeElapsedInRace = 90000;
    return 1;
}
/* game/TimeTrialRecords.c, with the records file stubbed above. */
void TimeTrialRecords_BeginFromTrackSelect(void)
{
    sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(0x3e00, "TimeTrialRecordsGhost");
    memset(sdata->ptrGhostTapePlaying, 0, 0x28);
    sdata->boolReplayHumanGhost = TimeTrialRecords_LoadGhost(sdata->ptrGhostTapePlaying);
    data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;
    sdata->ptrDesiredMenu = &data.menuQueueLoadTrack;
    sdata->errorMessagePosIndex = 0;
}
int SpeedClass_TimeTrial(void) { return 0; }
char *RECTMENU_DrawTime(int time) { static char t[16]; snprintf(t, sizeof t, "T%d", time); return t; }
/* The retail lap box: lapAnswer is what its input returns next (1 Cross, -1 Back). */
static int lapAnswer, lapBoxDrawn, overrideLaps;
s32 RECTMENU_ProcessInput(struct RectMenu *m) { (void)m; int a = lapAnswer; lapAnswer = 0; return a; }
void RECTMENU_DrawSelf(struct RectMenu *m, s16 x, s16 y, s16 w) { (void)m; (void)x; (void)y; (void)w; lapBoxDrawn++; }
int CustomOffline_SetRuntimeLaps(int laps) { overrideLaps = laps; return 1; }
/* Draw calls the stubbed screen never shows. */
uint32_t NativeGpuLinks_FromHostPointer(const void *hostPtr) { return (uint32_t)(uintptr_t)hostPtr; }
void DecalHUD_DrawPolyGT4(struct Icon *icon, s32 x, s32 y, struct PrimMem *pm, u32 *ot, Color c0, Color c1, Color c2, Color c3,
                          u8 transparency, s16 scale)
{ (void)icon; (void)x; (void)y; (void)pm; (void)ot; (void)c0; (void)c1; (void)c2; (void)c3; (void)transparency; (void)scale; }
/* Picture and minimap: none, so only the frame and text are drawn. */
static void MM_ModsPicture_Tick(void) {}
static struct Icon *MM_ModsPicture_Icon(int trackID, const char *pin, int *none) { (void)trackID; (void)pin; *none = 1; return NULL; }
int NativeTrackMinimap_LoadPackage(const char *dir, const char *pin, struct NativeTrackMinimap *out) { (void)dir; (void)pin; (void)out; return 0; }
void NativeRacers_TrackMinimap(const struct NativeTrackMinimap *m, struct Icon **t, struct Icon **b) { (void)m; *t = *b = NULL; }
void UI_Map_DrawMap(struct Icon *t, struct Icon *b, s32 x, s32 y, struct PrimMem *pm, u32 *ot, u8 c)
{ (void)t; (void)b; (void)x; (void)y; (void)pm; (void)ot; (void)c; }
#include "../game/230/MM_CustomTrackSelect.c"

/* What a finished store scan leaves behind. */
static void publish(const struct CustomTrackLibraryEntry *rows, size_t count)
{
    CHECK(CustomTrackLibrary_Replace(&s_contentLibrary, rows, count));
    s_contentStoreGeneration++;
}

static int tick(int buttons)
{
    while (s_arcadeMotion) { sdata->buttonTapPerPlayer[0] = 0; MM_CustomTrackSelect_Tick(); }
    sdata->buttonTapPerPlayer[0] = buttons;
    drawn[0] = 0;
    return MM_CustomTrackSelect_Tick();
}

/* Empty, one, 18 and 100 tracks: pages, wrap, and the Vanilla page in front. */
static void scale_scenarios(void)
{
    static struct CustomTrackLibraryEntry rows[100];
    const size_t sizes[] = {0, 1, 18, 100};
    memset(rows, 0, sizeof rows);
    for (size_t i = 0; i < 100; i++)
    {
        snprintf(rows[i].manifestSha256, 65, "%064zx", i + 500);
        snprintf(rows[i].title, sizeof rows[i].title, "Scale %03zu", i);
        strcpy(rows[i].version, "1.0.0");
        rows[i].installed = 1; rows[i].local.arcade = 1; rows[i].local.laps = 3;
    }
    for (size_t s = 0; s < 4; s++)
    {
        size_t count = sizes[s], pages = count ? (count + 7) / 8 + 1 : 2;
        publish(rows, count);
        memset(s_arcadeRows, 0, sizeof s_arcadeRows);
        memset(s_arcadeLibrary.rememberedPin, 0, sizeof s_arcadeLibrary.rememberedPin);
        MM_CustomTrackSelect_Init();
        D230.trackSelect.transition.state = IN_MENU;
        CHECK(tick(0) == 0 && s_arcadePage == 0); /* Vanilla first, every size. */
        CHECK(s_arcadeLibrary.visibleCount == count);
        CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == pages - 1);
        tick(0);
        CHECK(count ? !drawnHas("No tracks") : drawnHas("No tracks."));
        CHECK(tick(BTN_RIGHT) == 0 && s_arcadePage == 0); /* Pages wrap. */
        if (count)
        {
            tick(BTN_RIGHT);
            size_t rowsOnPage = CustomTrackLibrary_PageRowCount(&s_arcadeLibrary, 0);
            tick(BTN_UP); CHECK(s_arcadeRows[0] == rowsOnPage - 1); /* Rows wrap. */
            tick(BTN_DOWN); CHECK(s_arcadeRows[0] == 0);
            tick(BTN_LEFT);
        }
    }
}

static void generic_scenarios(const struct CustomTrackLibraryEntry *start)
{
    static struct CustomTrackLibraryEntry entries[17];
    int before;
    memcpy(entries, start, sizeof entries);
    for (int i = 0; i < 17; i++)
    {
        entries[i].local.uuid[0] = 0; entries[i].local.arcade = 1; entries[i].local.timeTrial = 0;
        entries[i].local.arcadeReason[0] = 0;
    }
    /* A menu entry keeps the cached rows and scans nothing. */
    publish(entries, 17);
    MM_CustomTrackSelect_Init(); D230.trackSelect.transition.state = IN_MENU; tick(0);
    {
        unsigned int built = s_arcadeGeneration;
        before = reloads;
        MM_CustomTrackSelect_Init(); tick(0);
        MM_CustomTrackSelect_Init(); tick(0);
        CHECK(reloads == before && s_arcadeGeneration == built);
    }
    /* L2 asks for a scan, on a custom page only. */
    tick(BTN_RIGHT); before = reloads;
    tick(BTN_L2); CHECK(reloads == before + 1);
    tick(BTN_LEFT);
    tick(BTN_L2); CHECK(reloads == before + 1);

    /* A new scan moves the rows but not the player: the pin stays selected. */
    tick(BTN_RIGHT); tick(BTN_RIGHT); /* page 2 */
    tick(BTN_DOWN); tick(BTN_DOWN); tick(0);
    {
        char pin[65];
        struct CustomTrackLibraryEntry more[18];
        const struct CustomTrackLibraryEntry *now;
        memcpy(pin, s_arcadeLibrary.rememberedPin, sizeof pin);
        memcpy(more, entries, 17 * sizeof *more);
        more[17] = entries[0];
        snprintf(more[17].manifestSha256, 65, "%064x", 99);
        strcpy(more[17].title, "AAA first in the alphabet");
        publish(more, 18);
        tick(0);
        now = CustomTrackLibrary_Row(&s_arcadeLibrary, (s_arcadePage - 1) * CTR_LIBRARY_PAGE_ROWS + s_arcadeRows[s_arcadePage - 1]);
        CHECK(now && !strcmp(now->manifestSha256, pin));
        /* The remembered revision left the store: the page stays valid. */
        publish(more, 2);
        tick(0); tick(0);
        CHECK(s_arcadePage == 1);
    }

    /* Search: live on the page's rows, by title, author and version; Triangle
       restores the old query; a query with no match says so. */
    strcpy(entries[3].author, "Some Author");
    strcpy(entries[4].version, "7.7.7");
    publish(entries, 17);
    MM_CustomTrackSelect_Init(); D230.trackSelect.transition.state = IN_MENU; tick(0); tick(BTN_RIGHT);
    CHECK(s_arcadeLibrary.visibleCount == 17);
    tick(BTN_R2); CHECK(s_arcadeSearching && textActive);
    strcpy(textBuffer, "some auth"); tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 1 && !strcmp(CustomTrackLibrary_Row(&s_arcadeLibrary, 0)->title, "Track 03"));
    CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == 1); /* Typing never navigates. */
    /* The Cross that commits is also a menu tap on that frame: it picks nothing. */
    before = prepared;
    rawPad = RAW_BTN_CROSS; tick(BTN_CROSS_one); rawPad = 0;
    CHECK(!s_arcadeSearching && !textActive && s_arcadeLibrary.visibleCount == 1);
    CHECK(prepared == before && !s_arcadePrepareBusy);
    tick(0); CHECK(!drawnHas("No search"));
    tick(BTN_R2); strcpy(textBuffer, "7.7.7"); tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 1 && !strcmp(CustomTrackLibrary_Row(&s_arcadeLibrary, 0)->title, "Track 04"));
    rawPad = RAW_BTN_TRIANGLE; tick(BTN_TRIANGLE); rawPad = 0;
    CHECK(!s_arcadeSearching && !strcmp(s_arcadeQuery, "some auth") && s_arcadeLibrary.visibleCount == 1);
    CHECK(D230.trackSelect.transition.state == IN_MENU); /* Its Triangle stays on the page. */
    tick(BTN_R2); strcpy(textBuffer, "nothing like it"); tick(0);
    rawPad = RAW_BTN_CIRCLE; tick(0); rawPad = 0; tick(0);
    CHECK(s_arcadeLibrary.visibleCount == 0 && drawnHas("No search"));
    /* A new entry starts with the whole list. */
    MM_CustomTrackSelect_Init(); CHECK(s_arcadeLibrary.visibleCount == 17);
    D230.trackSelect.transition.state = IN_MENU; tick(0);

    /* A revision that failed its checks stays visible, says why, and cannot
       start; Ready is the only state that prepares a launch. */
    {
        struct CustomTrackLibraryEntry mixed[3];
        memcpy(mixed, entries, sizeof mixed);
        mixed[0].installed = 0; mixed[0].local.arcade = 0;
        strcpy(mixed[0].disabledReason, "Hash mismatch");
        mixed[1].local.arcade = 0; strcpy(mixed[1].local.arcadeReason, "No AI paths");
        publish(mixed, 3);
        MM_CustomTrackSelect_Init(); D230.trackSelect.transition.state = IN_MENU; tick(0);
        CHECK(s_arcadeLibrary.count == 3);
        tick(BTN_RIGHT);
        s_arcadeRows[0] = 0; tick(0);
        before = prepared;
        tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy && !strcmp(s_arcadeMessage, "Hash mismatch"));
        tick(0); CHECK(drawnHas("Hash"));
        tick(BTN_DOWN); tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy);
        tick(BTN_DOWN); tick(BTN_CROSS_one); CHECK(prepared == before + 1 && s_arcadePrepareBusy);
        tick(0); CHECK(s_arcadeConfirm && D230.trackSelect.lapBoxOpen);
        lapAnswer = -1; tick(0); CHECK(!s_arcadeConfirm);
    }

    /* A room adds its note over the picture while the lap box is open. */
    {
        struct CustomTrackLibraryEntry one = entries[0];
        one.local.laps = 3; one.local.timeTrial = 1;
        publish(&one, 1);
        MM_CustomTrackSelect_Init(); D230.trackSelect.transition.state = IN_MENU; tick(0);
        tick(BTN_RIGHT); tick(0);
        tick(BTN_CROSS_one); tick(0); CHECK(s_arcadeConfirm && !drawnHas("Room:"));
        roomNote = "Room: no checks here, items wait";
        tick(0); CHECK(s_arcadeConfirm && drawnHas("Room:"));
        before = launched;
        lapAnswer = 1; tick(0); CHECK(launched == before + 1 && beganHost == 6); /* A room is no refusal. */
        roomNote = "";
        D230.trackSelect.transition.state = IN_MENU;
    }
    scale_scenarios();
    publish(start, 3);
    MM_CustomTrackSelect_Init(); D230.trackSelect.transition.state = IN_MENU; tick(0);
}

int main(void)
{
    struct CustomTrackLibraryEntry entries[17] = {0};
    data.font_charPixWidth[FONT_SMALL] = 13;
    data.font_charPixWidth[FONT_BIG] = 17;
    data.font_puncPixWidth[FONT_SMALL] = 7;
    data.font_puncPixWidth[FONT_BIG] = 11;
    data.font_buttonPixWidth[FONT_SMALL] = 2;
    data.font_buttonPixWidth[FONT_BIG] = 16;
    sdata->gGT = &tracker;
    tracker.backBuffer = &buffer;
    arrowGroup.icons[0x38] = &arrowIcon;
    tracker.iconGroup[4] = &arrowGroup.group;
    data.ptrColor[ORANGE] = orange;
    tracker.gameMode1 = ARCADE_MODE;
    tracker.numPlyrNextGame = 1;
    tracker.arcadeDifficulty = 123;
    for (int i = 0; i < 17; i++)
    {
        snprintf(entries[i].manifestSha256, 65, "%064x", i + 1);
        snprintf(entries[i].title, sizeof entries[i].title, "Track %02d", i);
        strcpy(entries[i].version, "1.0.2");
        entries[i].installed = 1; entries[i].local.arcade = 1;
    }
    publish(entries, 17);
    MM_CustomTrackSelect_Init();
    D230.trackSelect.transition.state = IN_MENU;
    CHECK(tick(0) == 0 && s_arcadePage == 0);
    CHECK(tick(BTN_LEFT) == 1 && s_arcadePage == 3);
    tick(BTN_DOWN); CHECK(s_arcadeRows[2] == 0); /* One row on final page. */
    CHECK(tick(BTN_RIGHT) == 0 && s_arcadePage == 0);
    tick(BTN_RIGHT); CHECK(s_arcadePage == 1);
    tick(BTN_UP); CHECK(s_arcadeRows[0] == 7);
    tick(BTN_RIGHT); tick(BTN_DOWN); CHECK(s_arcadeRows[1] == 1);
    tick(BTN_LEFT); CHECK(s_arcadeRows[0] == 7);
    tick(BTN_CROSS_one); CHECK(prepared == 1 && s_arcadePrepareBusy);
    tick(0); CHECK(s_arcadeConfirm && D230.trackSelect.lapBoxOpen && !s_arcadePrepareBusy); /* the lap box */
    lapBoxDrawn = 0; tick(0); CHECK(lapBoxDrawn == 1);
    lapAnswer = -1; tick(0); CHECK(!s_arcadeConfirm && !D230.trackSelect.lapBoxOpen && !s_arcadeRequest);
    CHECK(D230.trackSelect.transition.state == IN_MENU);
    allowed = 0; tick(BTN_CROSS_one); CHECK(prepared == 1);
    allowed = 1; tracker.numPlyrNextGame = 2;
    tick(BTN_CROSS_one); CHECK(prepared == 1);
    tracker.numPlyrNextGame = 1;
    D230.lapCountByRow[1].lapCount = 5; D230.menuLapSel.rowSelected = 1; sdata->uselessLapRowCopy = 1;
    tick(BTN_CROSS_one); tick(0); lapAnswer = 1; tick(0);
    /* RuntimeLaps is stubbed to the package's 7; the lap box's 5 is handed over. */
    CHECK(launched == 1 && beganHost == 6 && tracker.currLEV == 6 && overrideLaps == 5 && !D230.trackSelect.lapBoxOpen);
    CHECK(fallbackSet == -1); /* not an imported track: Crash Cove's music */
    CHECK(tracker.arcadeDifficulty == 123 && tracker.numPlyrNextGame == 1);
    CHECK(D230.trackSelect.transition.state == EXITING_MENU && D230.trackSelect.transition.startAfterExit == 1);
    tracker.gameMode1 = TIME_TRIAL | ARCADE_MODE; /* not a mode the pages know */
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1 = BATTLE_MODE;
    CHECK(tick(BTN_RIGHT) == 0);
    tracker.gameMode1=ARCADE_MODE;D230.trackSelect.transition.state=IN_MENU;
    CHECK(beganMode == CTR_OFFLINE_MODE_ARCADE && checkedMode == CTR_OFFLINE_MODE_ARCADE);
    CHECK(!MM_CustomTrackSelect_TimeTrialStart()); /* Arcade never takes the Time Trial exit. */

    /* Installed Time Trial-only content is listed, dimmed, and says why. */
    entries[0].local.arcade=0; entries[0].local.timeTrial=1;
    strcpy(entries[0].local.arcadeReason,"No AI paths");
    strcpy(entries[1].local.uuid,"same-track");strcpy(entries[2].local.uuid,"same-track");
    strcpy(entries[1].version,"1.9.0");strcpy(entries[2].version,"1.10.0");
    publish(entries,3);
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2);
    CHECK(!strcmp(s_arcadeLibrary.entries[0].title,"Track 00") && !strcmp(s_arcadeLibrary.entries[1].version,"1.10.0"));
    D230.trackSelect.transition.state=IN_MENU;
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
    CHECK(!drawnHas("No AI paths"));
    { int before = prepared; tick(BTN_CROSS_one); CHECK(prepared == before && !s_arcadePrepareBusy); }
    CHECK(!strcmp(s_arcadeMessage, "No AI paths. Use Time Trial."));
    tick(0); CHECK(drawnHas("No AI paths"));
    CHECK(!drawnHas("..."));

    /* A revision that can race beats a newer one that cannot. */
    entries[2].local.arcade=0; entries[2].local.timeTrial=1; strcpy(entries[2].local.arcadeReason,"No AI paths");
    publish(entries,3);
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==2 && !strcmp(s_arcadeLibrary.entries[1].version,"1.9.0"));
    entries[2].local.arcade=1; entries[2].local.arcadeReason[0]=0;

    /* A track turned off on the Mods screen is not listed. */
    offTitle="Track 00"; s_contentStoreGeneration++; /* MM_Mods_SetOff bumps it */
    MM_CustomTrackSelect_Init();tick(0);
    CHECK(s_arcadeLibrary.count==1 && strcmp(s_arcadeLibrary.entries[0].title,"Track 00"));
    offTitle=""; s_contentStoreGeneration++;

    /* Cached rows, rescan, search, refused revisions and the room note. */
    generic_scenarios(entries);
    /* Time Trial: the same pages, the Time Trial rule, the Time Trial runtime. */
    tracker.gameMode1=TIME_TRIAL; tracker.numLaps=3;
    entries[1].local.timeTrial=0; strcpy(entries[1].local.timeTrialReason,"No lap checkpoints");
    entries[2].local.timeTrial=0; strcpy(entries[2].local.timeTrialReason,"No lap checkpoints");
    strcpy(entries[0].local.uuid,"0f8fad5b-d9cb-469f-a165-70867728950e");
    publish(entries,3);
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);
    CHECK(s_arcadeLibrary.count==2);
    tick(BTN_RIGHT); CHECK(s_arcadePage==1);
    s_arcadeRows[0]=0; tick(0);
    launched=0; prepared=0; beganMode=0; overrideLaps=0;
    tick(BTN_CROSS_one); tick(0);
    CHECK(!D230.trackSelect.lapBoxOpen && checkedMode == CTR_OFFLINE_MODE_TIME_TRIAL);
    CHECK(launched == 1 && beganMode == CTR_OFFLINE_MODE_TIME_TRIAL && tracker.currLEV == 6 && tracker.numLaps == 7 && !overrideLaps);
    CHECK(D230.trackSelect.transition.state == EXITING_MENU && D230.trackSelect.transition.startAfterExit == 1);
    /* Leaving the menu: the saved ghost comes from custom-records/, the memory
       card's ghost selection is skipped, and the load is queued. */
    ghostOnFile = 1; ghostLoads = 0; sdata->boolReplayHumanGhost = 0; data.characterIDs[1] = CRASH_BANDICOOT;
    CHECK(MM_CustomTrackSelect_TimeTrialStart());
    CHECK(ghostLoads == 1 && sdata->boolReplayHumanGhost == 1 && data.characterIDs[1] == COCO_BANDICOOT);
    CHECK(sdata->ptrDesiredMenu == &data.menuQueueLoadTrack && sdata->ptrGhostTapePlaying == (void *)highMem);
    CHECK(!MM_CustomTrackSelect_TimeTrialStart()); /* once per launch */
    /* Without a ghost file: an empty playing ghost, no replay, P2 model id 0. */
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);tick(BTN_RIGHT);s_arcadeRows[0]=0;
    tick(BTN_CROSS_one); tick(0); CHECK(launched == 2);
    ghostOnFile = 0; sdata->boolReplayHumanGhost = 1;
    CHECK(MM_CustomTrackSelect_TimeTrialStart());
    CHECK(sdata->boolReplayHumanGhost == 0 && sdata->ptrGhostTapePlaying->characterID == 0 && data.characterIDs[1] == 0);
    /* An xdelta import of BIGFILE group 5 with no .sca plays that retail
       track's music, but only while the running package is the entry that
       was chosen. */
    entries[0].local.xdelta = 1; entries[0].local.bigfileIndex = 5 * 8 + 2;
    publish(entries,3);
    memcpy(runningPin, entries[0].manifestSha256, 65);
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);tick(BTN_RIGHT);s_arcadeRows[0]=0;
    tick(BTN_CROSS_one); tick(0); CHECK(launched == 3 && fallbackSet == 5);
    entries[0].local.bigfileIndex = 30 * 8; publish(entries,3);
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);tick(BTN_RIGHT);s_arcadeRows[0]=0;
    tick(BTN_CROSS_one); tick(0); CHECK(launched == 4 && fallbackSet == -1);
    entries[0].local.bigfileIndex = 5 * 8; publish(entries,3); runningPin[0] ^= 1;
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);tick(BTN_RIGHT);s_arcadeRows[0]=0;
    tick(BTN_CROSS_one); tick(0); CHECK(launched == 5 && fallbackSet == -1);
    MM_CustomTrackSelect_TimeTrialStart();
    entries[0].local.xdelta = 0; entries[0].local.bigfileIndex = 0; publish(entries,3);
    /* The track that cannot run a Time Trial says why and does not start. */
    MM_CustomTrackSelect_Init();D230.trackSelect.transition.state=IN_MENU;tick(0);tick(BTN_RIGHT);
    s_arcadeRows[0]=1; tick(0);
    { int before = prepared; tick(BTN_CROSS_one); CHECK(prepared == before); }
    tick(0); CHECK(drawnHas("No lap") && !strcmp(s_arcadeMessage, "No lap checkpoints"));
    CHECK(!drawnHas("..."));
    /* A retail Time Trial exit is left to the retail ghost selection. */
    CHECK(!MM_CustomTrackSelect_TimeTrialStart());
    tracker.gameMode1=ARCADE_MODE;
    CustomTrackLibrary_Free(&s_contentLibrary);
    CustomTrackLibrary_Free(&s_arcadeLibrary);
    CHECK(arrows > 0 && arrows % 2 == 0); /* both arrows, every time the title is drawn */
    printf("custom selector: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
