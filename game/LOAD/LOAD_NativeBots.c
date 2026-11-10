#ifdef CTR_NATIVE
#include <platform/native_racers.h>

static void *s_nativeBotBuffers[8];
static struct Model *s_nativeBotModels[8];
static int s_nativeBotCup = -1;
static int s_nativeBotHumans;
static int s_nativeBotIDs[8];
static int s_nativeBotPicks[8];

static int LOAD_NativeBotsEnabled(void)
{
	struct GameTracker *gt = GAME_TRACKER;
	return g_config.aiRacers > 0 && g_config.aiRacers <= 2 && !gt->boolDemoMode &&
	       (gt->gameMode1 & ARCADE_MODE) != 0 &&
	       gt->levelID < GEM_STONE_VALLEY && gt->numPlyrCurrGame >= 1 && gt->numPlyrCurrGame <= 2 &&
	       !(gt->gameMode1 & (ADVENTURE_MODE | ADVENTURE_ARENA | ADVENTURE_BOSS | ADVENTURE_CUP |
	                          BATTLE_MODE | TIME_TRIAL | RELIC_RACE | CRYSTAL_CHALLENGE | MAIN_MENU | GAME_CUTSCENE)) &&
	       !(gt->gameMode2 & (CREDITS | TOKEN_RACE));
}

static void LOAD_NativeBotsReset(void)
{
	for (int i = 0; i < 8; i++)
	{
		free(s_nativeBotBuffers[i]);
		s_nativeBotBuffers[i] = NULL;
		s_nativeBotModels[i] = NULL;
		NativeRacers_SetBot(i, -1);
	}
	if (!LOAD_NativeBotsEnabled())
		s_nativeBotCup = -1;
}

struct Model *NativeRacers_BotModel(int driver)
{
	return driver >= 0 && driver < 8 ? s_nativeBotModels[driver] : NULL;
}

static void LOAD_NativeBotModelLoaded(struct LoadQueueSlot *slot)
{
	for (int i = 0; i < 8; i++)
		if (s_nativeBotBuffers[i] && slot->ptrDestination == (u8 *)s_nativeBotBuffers[i] + 4)
			s_nativeBotModels[i] = (struct Model *)slot->ptrDestination;
}

// Keep retail's packs for shared assets, but own the opponent models outside
// their fixed mempack budget. LT_DRAM relocates them before driver birth.
static void LOAD_NativeBots(struct BigHeader *bigfile, int first, int count)
{
	struct GameTracker *gt = GAME_TRACKER;
	int pool[16 + 64], poolCount = 0;
	int cup = (gt->gameMode2 & CUP_ANY_KIND) != 0;
	int preserve = cup && gt->cup.trackIndex != 0 && s_nativeBotCup == gt->cup.cupID && s_nativeBotHumans == first;
	for (int i = 0; i < first; i++)
		if (s_nativeBotIDs[i] != GAME_CHARACTER_IDS[i] || s_nativeBotPicks[i] != NativeRacers_Player(i))
			preserve = 0;
	if (!preserve)
	{
		// Unlock bits match the retail character-select roster (ids 8..15).
		static const int unlocks[] = {10, 8, 7, 9, 5, 6, 11, GAME_UNLOCK_BIT_OXIDE};
		for (int id = 0; id < 16 + (g_config.aiRacers == 2 ? NativeRacers_Count() : 0); id++)
		{
			int taken = 0;
			if (id >= 8 && id < 16 && !CHECK_ADV_BIT(GAME_PROGRESS.unlocks, unlocks[id - 8]))
				continue;
			if (id >= 16 && NativeRacers_RaceModel(id - 16) == NULL)
				continue;
			for (int p = 0; p < first; p++)
			{
				int pick = NativeRacers_Player(p);
				if (pick >= 0 && NativeRacers_Template(pick) != GAME_CHARACTER_IDS[p])
					pick = -1;
				taken |= id == (pick >= 0 ? 16 + pick : GAME_CHARACTER_IDS[p]);
			}
			if (!taken)
				pool[poolCount++] = id;
		}
		u32 state = (u32)Timer_GetTime_Total() ^ (u32)sdata->randomNumber ^ 0x435452u;
		for (int i = first; i < count; i++)
		{
			state = state * 1664525u + 1013904223u;
			int index = (int)((state >> 16) % (u32)poolCount);
			int pick = pool[index];
			pool[index] = pool[--poolCount];
			s_nativeBotPicks[i] = pick >= 16 ? pick - 16 : -1;
			s_nativeBotIDs[i] = pick >= 16 ? NativeRacers_Template(pick - 16) : pick;
		}
	}
	for (int i = 0; i < first; i++)
	{
		s_nativeBotIDs[i] = GAME_CHARACTER_IDS[i];
		s_nativeBotPicks[i] = NativeRacers_Player(i);
	}
	s_nativeBotCup = cup ? gt->cup.cupID : -1;
	s_nativeBotHumans = first;
	if (sdata->queueLength + count - first > LOAD_QUEUE_SLOT_COUNT)
		Platform_Fatal("CTR Native - Opponent Roster", "Opponent models do not fit the load queue.");
	struct BigEntry *entries = BIG_GETENTRY(bigfile);
	for (int i = first; i < count; i++)
	{
		GAME_CHARACTER_IDS[i] = (s16)s_nativeBotIDs[i];
		NativeRacers_SetBot(i, s_nativeBotPicks[i]);
		int file = BI_RACERMODELMED + GAME_CHARACTER_IDS[i];
		u32 size = (entries[file].size + LOAD_CD_DATA_SECTOR_ROUND_MASK) & ~LOAD_CD_DATA_SECTOR_ROUND_MASK;
		s_nativeBotBuffers[i] = malloc(size);
		if (!s_nativeBotBuffers[i])
			Platform_Fatal("CTR Native - Opponent Roster", "Could not allocate an opponent model.");
		LOAD_AppendQueue(bigfile, LT_DRAM, file, s_nativeBotBuffers[i], LOAD_NativeBotModelLoaded);
		Platform_Log("[AI Racers] driver=%d character=%d custom=%d cup=%d preserve=%d\n", i, GAME_CHARACTER_IDS[i], s_nativeBotPicks[i], cup, preserve);
	}
}
#endif
