// Production Arcade/Cup roster and model ownership, with an immediate disc stub.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef unsigned char u8;
typedef short s16;
#define CTR_NATIVE
enum { GEM_STONE_VALLEY = 20, ADVENTURE_MODE = 1, ADVENTURE_ARENA = 2,
 ADVENTURE_BOSS = 4, ADVENTURE_CUP = 8, BATTLE_MODE = 16, TIME_TRIAL = 32,
 RELIC_RACE = 64, CRYSTAL_CHALLENGE = 128, MAIN_MENU = 256, GAME_CUTSCENE = 512,
 ARCADE_MODE = 1024, CREDITS = 1, TOKEN_RACE = 2, CUP_ANY_KIND = 4, GAME_UNLOCK_BIT_OXIDE = 37,
 BI_RACERMODELMED = 0, LOAD_CD_DATA_SECTOR_ROUND_MASK = 2047,
 LOAD_QUEUE_SLOT_COUNT = 8, LT_DRAM = 1 };
struct Model { int value; };
struct GameTracker { int boolDemoMode, levelID, numPlyrCurrGame, gameMode1, gameMode2;
 struct { int trackIndex, cupID; } cup; } testTracker;
struct { int aiRacers; } g_config;
struct { uint64_t unlocks; } progress;
struct { int queueLength, randomNumber; } sd, *sdata = &sd;
static s16 ids[8];
#define GAME_TRACKER (&testTracker)
#define GAME_CHARACTER_IDS ids
#define GAME_PROGRESS progress
#define CHECK_ADV_BIT(bits, bit) (((bits) >> (bit)) & 1)
struct BigEntry { u32 size; };
struct BigHeader { struct BigEntry entries[16]; } big;
#define BIG_GETENTRY(b) ((b)->entries)
struct LoadQueueSlot { void *ptrDestination; };
static int humans[4], bots[8], installed = 3;
static u32 tick;
static int NativeRacers_Count(void) { return installed; }
static int NativeRacers_Template(int racer) { return racer == 2 ? 15 : 0; }
static int NativeRacers_Player(int p) { return humans[p]; }
static struct Model model;
static struct Model *NativeRacers_RaceModel(int racer) { return racer >= 0 && racer < installed ? &model : NULL; }
// Public declarations are supplied by the production include.
void NativeRacers_SetBot(int driver, int racer) { bots[driver] = racer; }
static u32 Timer_GetTime_Total(void) { return tick++; }
static void Platform_Fatal(const char *title, const char *message)
{ fprintf(stderr, "%s: %s\n", title, message); abort(); }
static void LOAD_AppendQueue(struct BigHeader *b, int type, int file, void *dest,
 void (*callback)(struct LoadQueueSlot *))
{
 (void)b; (void)file;
 assert(type == LT_DRAM && ++sd.queueLength <= 8 && dest);
 struct LoadQueueSlot slot = {(u8 *)dest + 4};
 callback(&slot);
}
#define Platform_Log(...) ((void)0)
#include "../game/LOAD/LOAD_NativeBots.c"
#undef Platform_Log

static void reset(int players, int mode, uint64_t unlocks)
{
 memset(&testTracker, 0, sizeof testTracker);
 testTracker.numPlyrCurrGame = players;
 testTracker.gameMode1 = ARCADE_MODE;
 g_config.aiRacers = mode;
 progress.unlocks = unlocks;
 for (int i = 0; i < 8; i++) ids[i] = (s16)i;
 for (int i = 0; i < 4; i++) humans[i] = -1;
 s_nativeBotCup = -1;
 LOAD_NativeBotsReset();
}
static void draw(int players)
{
 sd.queueLength = players == 1 ? 1 : 3; // retail pack + human models
 LOAD_NativeBots(&big, players, players == 1 ? 8 : 6);
 int seen[80] = {0};
 for (int i = 0; i < (players == 1 ? 8 : 6); i++)
 {
  int custom = i < players ? humans[i] : bots[i];
  int identity = custom >= 0 ? 16 + custom : ids[i];
  assert(identity >= 0 && identity < 80 && !seen[identity]++);
  if (i >= players) assert(NativeRacers_BotModel(i) != NULL);
 }
}
int main(void)
{
 for (int i = 0; i < 16; i++) big.entries[i].size = 64;
 int sawCustom = 0, sawOxide = 0;
 for (int players = 1; players <= 2; players++)
 for (int mode = 1; mode <= 2; mode++)
 for (int unlocked = 0; unlocked <= 1; unlocked++)
 for (int trial = 0; trial < 2000; trial++)
 {
  reset(players, mode, unlocked ? UINT64_MAX : 0);
  ids[0] = (s16)(trial % 8);
  if (players == 2) ids[1] = (s16)((ids[0] + 1) % 8);
  if (trial & 1) { humans[0] = 0; ids[0] = 0; if (players == 2) ids[1] = 1; }
  draw(players);
  for (int i = players; i < (players == 1 ? 8 : 6); i++)
  {
   if (bots[i] >= 0) { assert(mode == 2); sawCustom++; }
   else { assert(unlocked || ids[i] < 8); sawOxide += ids[i] == 15; }
  }
 }
 assert(sawCustom && sawOxide);
 reset(1, 2, UINT64_MAX);
 testTracker.gameMode2 = CUP_ANY_KIND;
 draw(1);
 int savedIDs[8], savedPicks[8];
 for (int i = 0; i < 8; i++) { savedIDs[i] = ids[i]; savedPicks[i] = bots[i]; }
 for (int leg = 1; leg < 4; leg++)
 {
  testTracker.cup.trackIndex = leg;
  LOAD_NativeBotsReset();
  for (int i = 1; i < 8; i++) ids[i] = (s16)i; // retail loader overwrites IDs
  draw(1);
  for (int i = 1; i < 8; i++) assert(ids[i] == savedIDs[i] && bots[i] == savedPicks[i]);
 }
 int excluded[] = {ADVENTURE_MODE, ADVENTURE_ARENA, ADVENTURE_BOSS, ADVENTURE_CUP,
 BATTLE_MODE, TIME_TRIAL, RELIC_RACE, CRYSTAL_CHALLENGE, MAIN_MENU, GAME_CUTSCENE};
 for (unsigned i = 0; i < sizeof excluded / sizeof *excluded; i++)
 { reset(1, 2, UINT64_MAX); testTracker.gameMode1 = excluded[i]; assert(!LOAD_NativeBotsEnabled()); }
 reset(1, 0, 0); assert(!LOAD_NativeBotsEnabled());
 reset(1, 1, 0); testTracker.gameMode1 = 0; assert(!LOAD_NativeBotsEnabled());
 reset(1, 1, 0); testTracker.gameMode2 = CREDITS; assert(!LOAD_NativeBotsEnabled());
 reset(1, 1, 0); testTracker.gameMode2 = TOKEN_RACE; assert(!LOAD_NativeBotsEnabled());
 reset(1, 1, 0); testTracker.levelID = GEM_STONE_VALLEY; assert(!LOAD_NativeBotsEnabled());
 reset(1, 1, 0); testTracker.boolDemoMode = 1; assert(!LOAD_NativeBotsEnabled());
 reset(3, 1, 0); assert(!LOAD_NativeBotsEnabled());
 reset(1, 2, 0); installed = 0; draw(1);
 reset(1, 0, 0);
 for (int i = 0; i < 8; i++) assert(bots[i] == -1 && NativeRacers_BotModel(i) == NULL);
 puts("AI racers: 16000 roster draws, cup continuity, mode guards and model cleanup passed");
 return 0;
}
