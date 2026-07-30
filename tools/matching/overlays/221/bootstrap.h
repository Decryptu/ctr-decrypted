#ifndef CTR_OVERLAY_221_BOOTSTRAP_H
#define CTR_OVERLAY_221_BOOTSTRAP_H

// TODO(aalhendi): Remove this temporary scaffold once GCC 2.8.1 can compile
// game/221.c through the production include/common.h graph.
//
// NOTE(aalhendi): This bootstrap temporarily duplicates only the declarations
// and layouts needed by game/221.c; it must not become another source of truth.
// Defining the production header guard keeps it isolated while the shared header
// graph is made C90- and retail-layout-compatible.
#define COMMON_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;
typedef s32 b32;

#define NULL                              ((void *)0)
#define true                              1
#define false                             0

#define global_variable                   static

#define CTR_SECONDS_TO_FRAMES(seconds)    ((s32)((seconds) * 30))
#define CTR_MipsSra(value, shift)         ((s32)(value) >> ((u32)(shift) & 0x1f))

#define CHECK_ADV_BIT(rewards, bitIndex)  (((rewards)[(bitIndex) >> 5] >> ((bitIndex) & 0x1f)) & 1)
#define UNLOCK_ADV_BIT(rewards, bitIndex) ((rewards)[(bitIndex) >> 5] |= (1u << ((bitIndex) & 0x1f)))

enum
{
	DINGO_CANYON = 0,
	DRAGON_MINES = 1,
	BLIZZARD_BLUFF = 2,
	NITRO_COURT = 18,
	RAMPAGE_RUINS = 19,
	PARKING_LOT = 20,
	SKULL_ROCK = 21,
	THE_NORTH_BOWL = 22,
	ROCKY_ROAD = 23,
	LAB_BASEMENT = 24,
};

enum
{
	BTN_CROSS_one = 0x10,
	BTN_CIRCLE = 0x40,
	HIDE_MODEL = 0x80,
	FONT_BIG = 1,
	ORANGE = 0,
	WHITE = 4,
	JUSTIFY_CENTER = 0x8000,
	ADVENTURE_ARENA = 0x100000,
	CRYSTAL_CHALLENGE = 0x8000000,
	ADV_REWARD_FIRST_PURPLE_TOKEN = 0x6f,
	LNG_RETRY = 0x004,
	LNG_EXIT_TO_MAP = 0x00d,
	LNG_PRESS_TO_CONTINUE = 0x0c9,
	LNG_YOU_WIN = 0x16b,
	LNG_TRY_AGAIN = 0x16c,
	LNG_TIME_REMAINING = 0x16d,
	LNG_CTR_TOKEN_AWARDED = 0x16f,
	RECTMENU_STRING_NONE = -1,
	RECTMENU_STATE_CENTERED = 0x803,
};

typedef struct SVec2
{
	s16 x;
	s16 y;
} SVec2;

typedef struct SVec3
{
	s16 x;
	s16 y;
	s16 z;
} SVec3;

typedef struct MATRIX
{
	s16 m[3][3];
	s32 t[3];
} MATRIX;

struct PushBuffer
{
	SVec3 pos;
	u8 unused[0x110 - sizeof(SVec3)];
};

struct Driver
{
	u8 unused[0x31];
	s8 numCrystals;
};

struct Instance
{
	u8 beforeScale[0x1c];
	SVec3 scale;
	u8 beforeFlags[0x28 - 0x1c - sizeof(SVec3)];
	u32 flags;
	u8 beforeMatrix[0x30 - 0x2c];
	MATRIX matrix;
};

struct GameTracker
{
	u8 beforePushBuffer[0x168];
	struct PushBuffer pushBuffer[4];
	u8 beforeLevelID[0x1a10 - 0x168 - sizeof(struct PushBuffer) * 4];
	s32 levelID;
	u8 beforeTimer[0x1cec - 0x1a14];
	s32 timer;
	u8 beforeNumCrystalsInLEV[0x1e28 - 0x1cf0];
	s32 numCrystalsInLEV;
	u8 beforePrevLEV[0x1eb4 - 0x1e2c];
	s32 prevLEV;
	u8 beforeDrivers[0x24ec - 0x1eb8];
	struct Driver *drivers[8];
};

struct AdvProgress
{
	u32 rewards[6];
};

struct MenuRow
{
	s16 stringIndex;
	char rowOnPressUp;
	char rowOnPressDown;
	char rowOnPressLeft;
	char rowOnPressRight;
};

struct RectMenu
{
	s16 stringIndexTitle;
	u16 posX_curr;
	u16 posY_curr;
	u16 unk1;
	u32 state;
	struct MenuRow *rows;
	void (*funcPtr)(struct RectMenu *menu);
	u16 drawStyle;
	s16 posX_prev;
	s16 posY_prev;
	s16 rowSelected;
	s16 unk1c;
	s16 funcState;
	s16 width;
	s16 height;
	struct RectMenu *ptrNextBox_InHierarchy;
	struct RectMenu *ptrPrevBox_InHierarchy;
};

struct MatchLoading
{
	s32 stage;
	u32 levelID;
	struct
	{
		u32 AddBitsConfig0;
		u32 RemBitsConfig0;
		u32 AddBitsConfig8;
		u32 RemBitsConfig8;
	} OnBegin;
};

struct sData
{
	u8 beforeLoading[0x18c];
	struct MatchLoading Loading;
	u8 beforeGGT[0x340 - 0x18c - sizeof(struct MatchLoading)];
	struct GameTracker *gGT;
	u8 beforeMenuReadyToPass[0x550 - 0x344];
	s32 menuReadyToPass;
	u8 beforeFramesSinceRaceEnded[0x5c0 - 0x554];
	s32 framesSinceRaceEnded;
	u8 beforeLngStrings[0x90c - 0x5c4];
	char **lngStrings;
	u8 beforeAnyPlayerTap[0x9e4 - 0x910];
	s32 AnyPlayerTap;
	u8 beforePtrHudCrystal[0xa50 - 0x9e8];
	struct Instance *ptrHudCrystal;
	struct Instance *ptrMenuCrystal;
	u8 beforePtrToken[0xa64 - 0xa58];
	struct Instance *ptrToken;
	u8 beforeAdvProgress[0x2c38 - 0xa68];
	struct AdvProgress advProgress;
};

#define CTR_MATCH_OFFSET(type, member)     ((u32) & ((type *)0)->member)
#define CTR_MATCH_ASSERT(name, expression) typedef char ctr_match_assert_##name[(expression) ? 1 : -1]

CTR_MATCH_ASSERT(s16_size, sizeof(s16) == 2);
CTR_MATCH_ASSERT(s32_size, sizeof(s32) == 4);
CTR_MATCH_ASSERT(pointer_size, sizeof(void *) == 4);
CTR_MATCH_ASSERT(push_buffer_size, sizeof(struct PushBuffer) == 0x110);
CTR_MATCH_ASSERT(driver_num_crystals, CTR_MATCH_OFFSET(struct Driver, numCrystals) == 0x31);
CTR_MATCH_ASSERT(instance_flags, CTR_MATCH_OFFSET(struct Instance, flags) == 0x28);
CTR_MATCH_ASSERT(instance_matrix_t, CTR_MATCH_OFFSET(struct Instance, matrix.t) == 0x44);
CTR_MATCH_ASSERT(game_tracker_push_buffer, CTR_MATCH_OFFSET(struct GameTracker, pushBuffer) == 0x168);
CTR_MATCH_ASSERT(game_tracker_level_id, CTR_MATCH_OFFSET(struct GameTracker, levelID) == 0x1a10);
CTR_MATCH_ASSERT(game_tracker_timer, CTR_MATCH_OFFSET(struct GameTracker, timer) == 0x1cec);
CTR_MATCH_ASSERT(game_tracker_crystals, CTR_MATCH_OFFSET(struct GameTracker, numCrystalsInLEV) == 0x1e28);
CTR_MATCH_ASSERT(game_tracker_prev_level, CTR_MATCH_OFFSET(struct GameTracker, prevLEV) == 0x1eb4);
CTR_MATCH_ASSERT(game_tracker_drivers, CTR_MATCH_OFFSET(struct GameTracker, drivers) == 0x24ec);
CTR_MATCH_ASSERT(menu_row_size, sizeof(struct MenuRow) == 6);
CTR_MATCH_ASSERT(rect_menu_size, sizeof(struct RectMenu) == 0x2c);
CTR_MATCH_ASSERT(sdata_loading, CTR_MATCH_OFFSET(struct sData, Loading) == 0x18c);
CTR_MATCH_ASSERT(sdata_ggt, CTR_MATCH_OFFSET(struct sData, gGT) == 0x340);
CTR_MATCH_ASSERT(sdata_menu_ready, CTR_MATCH_OFFSET(struct sData, menuReadyToPass) == 0x550);
CTR_MATCH_ASSERT(sdata_frames, CTR_MATCH_OFFSET(struct sData, framesSinceRaceEnded) == 0x5c0);
CTR_MATCH_ASSERT(sdata_strings, CTR_MATCH_OFFSET(struct sData, lngStrings) == 0x90c);
CTR_MATCH_ASSERT(sdata_tap, CTR_MATCH_OFFSET(struct sData, AnyPlayerTap) == 0x9e4);
CTR_MATCH_ASSERT(sdata_hud_crystal, CTR_MATCH_OFFSET(struct sData, ptrHudCrystal) == 0xa50);
CTR_MATCH_ASSERT(sdata_menu_crystal, CTR_MATCH_OFFSET(struct sData, ptrMenuCrystal) == 0xa54);
CTR_MATCH_ASSERT(sdata_token, CTR_MATCH_OFFSET(struct sData, ptrToken) == 0xa64);
CTR_MATCH_ASSERT(sdata_progress, CTR_MATCH_OFFSET(struct sData, advProgress) == 0x2c38);

extern struct sData sdata_static;
#define sdata (&sdata_static)

void DecalFont_DrawLine(char *text, s16 x, s16 y, s16 font, s16 style);
void MainRaceTrack_RequestLoad(s16 levelID);
int OtherFX_Play(u32 soundID, int flags);
void RECTMENU_ClearInput(void);
void RECTMENU_Show(struct RectMenu *menu);
int UI_ConvertX_2(int x, int scale);
int UI_ConvertY_2(int y, int scale);
void UI_DrawLimitClock(s16 x, s16 y, s16 font);
void UI_DrawNumCrystal(s16 x, s16 y, struct Driver *driver);
void UI_Lerp2D_Linear(s16 *pos, s16 startX, s16 startY, s16 endX, s16 endY, int currentFrame, s16 endFrame);
void UI_RaceEnd_MenuProc(struct RectMenu *menu);

#endif
