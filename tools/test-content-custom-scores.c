// cc -m32 -std=c99 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_AP -DCTR_CUSTOM_TRACKS -DBUILD=926 -I ap -I include -I . tools/test-content-custom-scores.c
// -lm -o /tmp/test-content-custom-scores
//
// A content-plan Relic Race on a custom package keeps no high scores (#133 PR
// 3a review, 2026-10-16). The real GAMEPROG.c, MainGameEnd.c and 223.c run
// with a custom standalone context on host slot 6:
//
// - no time offers a record, a best lap or name entry, including a run under
//   ten seconds with every time crate, whose adjusted time is negative (the
//   zeroed scratch table alone let that through);
// - the high-score pointer and the end-of-race code never touch the host
//   slot's saved table;
// - the Relic Race results draw no best-times table (it would be the host's);
// - the same calls without a custom context still offer a retail record, so
//   the harness would notice if the policy were always on.
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap_hooks.h"
#include "ap_race_attempt_logic.h"
// Declared here so a compiler that rejects implicit declarations (gcc 15) builds the units below.
int AP_RaceAttempt_ProducerBlocked(int producerClass);
void AP_DeathLinkOnRaceEnd(struct GameTracker *gGT, struct Driver *player);
// MainGameEnd.c and 223.c call DeathLink hooks; the unity build declares them
// through game_unity.h.
#include "ap_deathlink.h"
#include "game/GAMEPROG.c"
#include "game/MAIN/MainGameEnd.c"
#include "game/223.c"

struct sData sdata_static;
struct Data data;
// The results screen runs at retail's 30 FPS timing.
int gNative60FpsEnabled = 0;

static struct CustomTrackRaceContext race;
static int custom_on;
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level)
{
    return custom_on && level == 6 ? &race : 0;
}

// game/TimeTrialRecords.c: a custom track's race stays off the memory card (no 200cc here).
int TimeTrialRecords_OffCard(void) { return custom_on; }
struct HighScoreEntry *TimeTrialRecords_HighScores(void)
{
    static struct HighScoreEntry entries[2 * 6];
    return entries;
}

// The drawing calls RR_EndEvent_DrawHighScore makes once past its policy check.
static int draws;
void DecalFont_DrawLine(char *str, s16 posX, s16 posY, s16 fontType, s16 flags)
{ (void)str; (void)posX; (void)posY; (void)fontType; (void)flags; ++draws; }
void UI_Lerp2D_Linear(s16 *ptrPos, s16 startX, s16 startY, s16 endX, s16 endY, s32 curFrame, s16 endFrame)
{ (void)startX; (void)endX; (void)endY; (void)curFrame; (void)endFrame; ptrPos[0] = startX; ptrPos[1] = startY; ++draws; }

void CTR_Box_DrawClearBox(const RECT *r, const Color *color, s32 transparency, u32 *ot, struct PrimMem *primMem)
{ (void)r; (void)color; (void)transparency; (void)ot; (void)primMem; ++draws; }
void RECTMENU_DrawInnerRect(RECT *r, s16 x, u32 *ot) { (void)r; (void)x; (void)ot; ++draws; }
char *RECTMENU_DrawTime(s32 ticks) { (void)ticks; ++draws; return "0:00.00"; }
void RECTMENU_DrawPolyGT4(struct Icon *icon, s32 posX, s32 posY, struct PrimMem *primMem, u32 *ot, Color color0, Color color1, Color color2, Color color3,
                          u8 transparency, s16 scale)
{
    (void)icon; (void)posX; (void)posY; (void)primMem; (void)ot; (void)color0; (void)color1; (void)color2; (void)color3;
    (void)transparency; (void)scale; ++draws;
}

static int checks, failures;
static void expect(int good, const char *why)
{
    ++checks;
    if (!good) { ++failures; printf("FAIL: %s\n", why); }
}

static struct GameTracker gt;
/* UI_Reward.c keeps every lap in a native table; this harness sets the retail one. */
s32 UI_LapTime_Get(s32 lapIndex, s16 driverID) { (void)driverID; return gt.lapTime[lapIndex]; }
static struct Driver driver;
static struct Thread thread;

static void finish(int elapsed, int lap)
{
    GAMEPROG_GetPtrHighScoreTrack();
    driver.timeElapsedInRace = elapsed;
    gt.lapTime[0] = lap;
    gt.gameModeEnd = 0;
    MainGameEnd_SoloRaceGetReward(1);
}

static int offered(void)
{
    return gt.newHighScoreIndex >= 0 || (gt.gameModeEnd & 0x8c000000) != 0;
}

int main(void)
{
    static struct HighScoreTrack saved;
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    sdata->gGT = &gt;
    gt.gameMode1 = ADVENTURE_MODE | RELIC_RACE;
    gt.levelID = 6; gt.numLaps = 1; gt.numPlyrCurrGame = 1; gt.timeCratesInLEV = 1;
    race.mode = 2; race.physicalPad = 6; race.returnHub = 25; race.laps = 1; race.slot = 1;
    race.hostLevelID = 6; race.seedEpoch = 5; race.trophyLocation = 35022000;
    strcpy(race.entryID, "entry/fixture"); strcpy(race.trackID, "custom/fixture/1");
    memset(race.levSha256, 'a', 64); memset(race.vrmSha256, 'b', 64);
    expect(CustomRaceContext_Valid(&race), "fixture context is valid");
    gt.drivers[0] = &driver;
    thread.object = &driver; gt.threadBuckets[PLAYER].thread = &thread;
    driver.numTimeCrates = 1;
    // The host slot's saved table: every time beatable, so a leak would show.
    for (i = 0; i < 12; ++i)
        GAME_PROGRESS.highScoreTracks[6].scoreEntry[i / 6][i % 6].time = 0x7fffff;
    memcpy(&saved, &GAME_PROGRESS.highScoreTracks[6], sizeof saved);

    custom_on = 1;
    finish(19200, 19200);
    expect(!offered(), "custom: a twenty-second run offers no record or best lap");
    expect((gt.gameModeEnd & NEW_HIGH_SCORE) == 0, "custom: no name entry for a twenty-second run");
    finish(4800, 4800);
    expect(!offered(), "custom: a five-second all-crates run (adjusted time negative) offers no record");
    expect((gt.gameModeEnd & NEW_HIGH_SCORE) == 0, "custom: no name entry for a negative adjusted time");
    finish(1, 1);
    expect(!offered(), "custom: the shortest possible run offers no record or best lap");
    expect(gt.bestLapTime == 0, "custom: best lap comes from the scratch table");
    expect((char *)sdata->ptrActiveHighScoreEntry < (char *)&GAME_PROGRESS ||
               (char *)sdata->ptrActiveHighScoreEntry >= (char *)(&GAME_PROGRESS + 1),
           "custom: the high-score pointer is outside the save");
    expect(memcmp(&saved, &GAME_PROGRESS.highScoreTracks[6], sizeof saved) == 0,
           "custom: the host slot's saved table and flags are unchanged");
    draws = 0;
    RR_EndEvent_DrawHighScore(0x100, 10, RR_SCORE_MODE_RELIC_RACE);
    expect(draws == 0, "custom: the results draw no best-times table");

    custom_on = 0;
    finish(4800, 4800);
    expect(sdata->ptrActiveHighScoreEntry == &GAME_PROGRESS.highScoreTracks[6].scoreEntry[1][0],
           "retail: the pointer names the track's Relic Race table");
    expect(gt.newHighScoreIndex == 0 && (gt.gameModeEnd & NEW_HIGH_SCORE) != 0,
           "retail: a beatable time still offers a record");

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
