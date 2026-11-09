#include <common.h>
#include <ctr_speed_class.h>
#include <platform/native_custom_records.h>
#include <platform/native_log.h>

// Time Trials whose best times and ghost live in files instead of on the
// memory card (include/platform/native_custom_records.h): a custom track at
// either class, and a retail track at 200cc (docs/SPEED-CLASS.md). Neither
// touches the retail high-score table, the N. Tropy and Oxide flags or the
// memory card's ghosts, and neither races a staff ghost.

// A custom-page race, in any mode.
static int TimeTrialRecords_CustomLoad(void)
{
#ifdef CTR_CUSTOM_PACKAGES
	return MainRaceTrack_OfflineCustomLoad();
#else
	return 0;
#endif
}

int TimeTrialRecords_Active(void)
{
#ifdef CTR_CUSTOM_PACKAGES
	if (MainRaceTrack_OfflineCustomTimeTrial())
		return 1;
#endif
	return SpeedClass_TimeTrial();
}

// A race that neither reads nor writes the memory card's Time Trial data: no
// retail high-score table, no N. Tropy or Oxide flags, ghost or Save Ghost row.
// A custom track's LEV carries no staff ghost and its host slot's data is not
// its own; at 200cc the staff ghosts would race at retail speed.
int TimeTrialRecords_OffCard(void)
{
	return TimeTrialRecords_CustomLoad() || SpeedClass_TimeTrial();
}

// The key of the running Time Trial and the folder its files go to. Valid in
// the menu too, once the mode, track and (for a custom track) package are set,
// so the ghost can be read before the level loads.
static int TimeTrialRecords_Key(struct CustomRecordKey *key, const char **dir)
{
	struct GameTracker *gGT = sdata->gGT;
	unsigned int speedClass = SpeedClass_TimeTrial() ? SPEED_CLASS_200CC : SPEED_CLASS_CLASSIC;

#ifdef CTR_CUSTOM_PACKAGES
	// a custom Time Trial's package, from the moment its page starts the run
	if (MainRaceTrack_CustomRecordKey(key))
	{
		*dir = CTR_RECORDS_DIR;
		return CustomRecords_SetClass(key, speedClass);
	}
	if (TimeTrialRecords_CustomLoad())
		return 0; // a custom Arcade race keeps no records
#endif
	if (speedClass != SPEED_CLASS_200CC)
		return 0;
	*dir = CTR_RETAIL_RECORDS_DIR;
	// In the menu the chosen track is currLEV (QueueLoadTrack loads it); in
	// the race it is the level.
	return CustomRecords_MakeRetailKey(key, (gGT->gameMode1 & MAIN_MENU) ? gGT->currLEV : gGT->levelID,
	                                   (unsigned int)gGT->numLaps, speedClass);
}

static void TimeTrialRecords_Defaults(struct HighScoreEntry *table)
{
	int i;
	for (i = 0; i < 12; i++)
	{
		int characterID = i % 6;
		table[i].time = 0x8c640; // 9:59.99
		table[i].characterID = characterID;
		snprintf(table[i].name, sizeof table[i].name, "%s",
		         sdata->lngStrings ? sdata->lngStrings[data.MetaDataCharacters[characterID].name_LNG_short] : "");
	}
}

static void TimeTrialRecords_Load(struct HighScoreEntry *table, const struct CustomRecordKey *key, const char *dir)
{
	struct CustomRecordTimes times;
	int i;

	TimeTrialRecords_Defaults(table);
	if (!CustomRecords_LoadTimes(dir, key, &times))
		return;
	for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
	{
		if (times.entry[i].characterID > NITROS_OXIDE)
			continue;
		table[i].time = times.entry[i].time;
		table[i].characterID = (u16)times.entry[i].characterID;
		memcpy(table[i].name, times.entry[i].name, sizeof table[i].name);
	}
	Platform_Log("[Records] best times loaded from %s\n", dir);
}

// The table for the running Time Trial: best lap and five best races, then a
// Relic half that stays at its defaults (only the Time Trial half is saved).
// Tracks without a file start from the retail defaults (9:59.99, six
// characters). Retail tracks keep one table each, read once; a custom track's
// is read once per custom run.
static struct HighScoreEntry s_retailTables[CTR_RECORDS_RETAIL_TRACKS][12];
static u8 s_retailLoaded[CTR_RECORDS_RETAIL_TRACKS];
#ifdef CTR_CUSTOM_PACKAGES
static struct HighScoreEntry s_customTable[12];
static uint64_t s_customRun;
static unsigned int s_customClass;
#endif

struct HighScoreEntry *TimeTrialRecords_HighScores(void)
{
	static struct HighScoreEntry s_none[12];
	struct CustomRecordKey key;
	const char *dir = NULL;
	int ok = TimeTrialRecords_Key(&key, &dir);

#ifdef CTR_CUSTOM_PACKAGES
	if (TimeTrialRecords_CustomLoad())
	{
		uint64_t run = CustomOffline_RuntimeGeneration();
		unsigned int speedClass = ok ? key.speedClass : SPEED_CLASS_CLASSIC;
		if (run != s_customRun || speedClass != s_customClass)
		{
			s_customRun = run;
			s_customClass = speedClass;
			if (ok)
				TimeTrialRecords_Load(s_customTable, &key, dir);
			else
				TimeTrialRecords_Defaults(s_customTable);
		}
		return s_customTable;
	}
#endif
	if (!ok || key.retailLevel < 0)
	{
		TimeTrialRecords_Defaults(s_none);
		return s_none;
	}
	if (!s_retailLoaded[key.retailLevel])
	{
		s_retailLoaded[key.retailLevel] = 1;
		TimeTrialRecords_Load(s_retailTables[key.retailLevel], &key, dir);
	}
	return s_retailTables[key.retailLevel];
}

void TimeTrialRecords_SaveHighScores(void)
{
	struct CustomRecordKey key;
	struct CustomRecordTimes times;
	const struct HighScoreEntry *table;
	const char *dir = NULL;
	int i;

	if (!TimeTrialRecords_Key(&key, &dir))
		return;
	table = TimeTrialRecords_HighScores();
	memset(&times, 0, sizeof times);
	for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
	{
		times.entry[i].time = table[i].time;
		times.entry[i].characterID = table[i].characterID;
		memcpy(times.entry[i].name, table[i].name, sizeof times.entry[i].name);
		times.entry[i].name[CTR_RECORDS_NAME - 1] = 0;
	}
	if (CustomRecords_SaveTimes(dir, &key, &times))
		Platform_Log("[Records] best times saved to %s\n", dir);
	else
		Platform_LogWarn("[Records] could not save best times to %s\n", dir);
}

// The saved ghost of the running Time Trial, read into the engine's playing
// buffer (0x3e00 bytes). Refused unless it is a complete retail-format ghost.
int TimeTrialRecords_LoadGhost(struct GhostHeader *dst)
{
	struct CustomRecordKey key;
	const char *dir = NULL;
	size_t bytes = 0;

	if (dst == NULL || !TimeTrialRecords_Key(&key, &dir) || !CustomRecords_LoadGhost(dir, &key, dst, 0x3e00, &bytes))
		return 0;
	if (bytes < sizeof(struct GhostHeader) || dst->version != -4 ||
	    bytes != sizeof(struct GhostHeader) + (size_t)dst->size || dst->size > 0x3dd4 || dst->characterID < 0 ||
	    dst->characterID > NITROS_OXIDE || dst->timeElapsedInRace <= 0)
	{
		memset(dst, 0, sizeof(struct GhostHeader));
		Platform_LogWarn("[Records] saved ghost refused: not a complete ghost\n");
		return 0;
	}
	Platform_Log("[Records] saved ghost loaded (%d bytes)\n", (int)bytes);
	return 1;
}

void TimeTrialRecords_SaveGhost(const struct GhostHeader *gh)
{
	struct CustomRecordKey key;
	const char *dir = NULL;

	if (gh == NULL || gh->version != -4 || gh->size <= 0 || gh->size > 0x3dd4 || !TimeTrialRecords_Key(&key, &dir))
		return;
	if (CustomRecords_SaveGhost(dir, &key, gh, sizeof(struct GhostHeader) + (size_t)gh->size))
		Platform_Log("[Records] ghost saved to %s\n", dir);
	else
		Platform_LogWarn("[Records] could not save the ghost to %s\n", dir);
}

// Leaving the track select with such a Time Trial chosen (a custom track's
// page, or a retail track at 200cc): the ghost comes from the records file
// into the retail playing buffer, and the race loads straight away, skipping
// the memory card's ghost selection.
void TimeTrialRecords_BeginFromTrackSelect(void)
{
	sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(0x3e00, "TimeTrialRecordsGhost");
	memset(sdata->ptrGhostTapePlaying, 0, 0x28);
	sdata->boolReplayHumanGhost = TimeTrialRecords_LoadGhost(sdata->ptrGhostTapePlaying);
	// The ghost's character is P2 (the Time Trial pack loads its model).
	data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;
	sdata->ptrDesiredMenu = &data.menuQueueLoadTrack;
	sdata->errorMessagePosIndex = 0;
}
