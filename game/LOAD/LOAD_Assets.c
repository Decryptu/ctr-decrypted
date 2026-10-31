#include <common.h>

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

#ifdef CTR_AP
#include "../../ap/ap_net.h" // ap_net_checked_count for the [AP HIT] roster line
#endif

void LOAD_RunPtrMap(char *origin, int *patchArr, int numPtrs)
{
	int *ptrCurrOffset = patchArr;
	int *ptrEndOffset = &patchArr[numPtrs];

	for (; ptrCurrOffset < ptrEndOffset; ptrCurrOffset++)
	{
		int offset = (*ptrCurrOffset >> 2) << 2;
		// NOTE(aalhendi): Retail adds the aligned offset before the 32-bit base address.
		int *location = (int *)((u32)offset + (u32)origin);
		*location = *location + (int)origin;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		NativeCheckpoint_RegisterPointerSlot((char *)location);
#endif
	}
}

void LOAD_Robots2P(struct BigHeader *bigfile, int p1, int p2, void (*callback)(struct LoadQueueSlot *))
{
	int setIndex = 0;
	int setOffset;
	int racerIndex;
	s16 *characterIDs = GAME_CHARACTER_IDS;
	u8 *robotSetBase = GAME_2P_AI_SETS[0];
	// NOTE(aalhendi): Retail keeps the set pointer in t1 while scanning via a separate table offset.
	register u8(*robotSet)[LOAD_2P_AI_SET_RACER_COUNT] CTR_PSX_REGISTER("$9") = GAME_2P_AI_SETS;
	b32 boolFoundRepeat;
	setOffset = 0;

	// 8 sets, but only check 7 because the last is the Gem Cups pack (4 bosses).
	for (; setIndex < LOAD_2P_AI_SET_COUNT; robotSet++, setIndex++, setOffset += LOAD_2P_AI_SET_RACER_COUNT)
	{
		boolFoundRepeat = false;
		racerIndex = 0;
		do
		{
			// NOTE(aalhendi): The offset-first 32-bit address keeps retail's two-add order.
			u8 racerID = *(u8 *)((u32)(racerIndex + setOffset) + (u32)robotSetBase);
			if ((p1 == racerID) || (p2 == racerID))
			{
				boolFoundRepeat = true;
			}
			racerIndex++;
		} while ((racerIndex < LOAD_2P_AI_SET_RACER_COUNT) && !boolFoundRepeat);

		if (!boolFoundRepeat)
		{
			characterIDs[2] = (*robotSet)[0];
			characterIDs[3] = (*robotSet)[1];
			characterIDs[4] = (*robotSet)[2];
			characterIDs[5] = (*robotSet)[3];

			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + setIndex, NULL, callback);
			return;
		}
	}
}

void LOAD_Robots1P(int characterID)
{
	int slotIndex = 1;
	int candidateID;
	s16 *slot;

	GAME_CHARACTER_IDS[0] = characterID;
	candidateID = 0;
	slot = &GAME_CHARACTER_IDS[1];

	// NOTE(aalhendi): Retail scans candidate IDs and advances the slot only for IDs kept.
	for (; candidateID < LOAD_CHARACTER_ID_COUNT; candidateID++)
	{
		if (slotIndex >= LOAD_CHARACTER_ID_COUNT)
		{
			break;
		}

		if (candidateID != characterID)
		{
			*slot++ = candidateID;
			slotIndex++;
		}
	}
}

static void (*const LOAD_DriverMPK_SetPointer)(struct LoadQueueSlot *) = LOAD_QUEUE_CALLBACK_SET_POINTER;

#ifdef CTR_AP
// Hit Character encounters (ticket 06). The engine ids whose HI models were
// sideloaded for this ordinary load, so stage 6 can refuse a required model that
// did not actually load before VehBirth ever dereferences it. Reset per load.
// The production roster array is sized by AP_HIT_FIELD_MAX; the field is P1 plus
// at most seven AI, so this pins the loader to the corrected bound.
CTR_STATIC_ASSERT(AP_HIT_FIELD_MAX == 7);
static ap_hit_extras_state s_hitExtras;

void LOAD_HitEncounterResetExtras(void)
{
	AP_HitExtrasResetPure(&s_hitExtras);
}

// Called from LOAD_TenStages stage 6, after fileBase->model conversion and
// before any birth. A required extra whose model is still NULL is an
// unrecoverable load failure: name it and terminate cleanly rather than let
// VehBirth_GetModelByName return NULL into a blind dereference.
void LOAD_HitEncounterValidateExtras(void)
{
	const void *models[3];
	int missing;
	int i;

	for (i = 0; i < 3; i++)
		models[i] = GAME_DRIVER_MODEL_EXTRAS[i].model;

	missing = AP_HitExtrasFirstMissingPure(&s_hitExtras, models);
	if (missing >= 0)
	{
		char msg[160];
		snprintf(msg, sizeof msg,
		         "Required driver model for character %d failed to load.",
		         s_hitExtras.ids[missing]);
		Platform_Fatal("CTR Native - Hit Character", msg);
	}
}
#endif

#ifdef CTR_AP
// "[a,b,c]" into buf, bounded.
static void LOAD_HitFormatIds(char *buf, int cap, const int *ids, int n)
{
	int used = 0;
	int k;
	used += snprintf(buf + used, cap - used, "[");
	for (k = 0; k < n && used < cap - 1; k++)
		used += snprintf(buf + used, cap - used, k ? ",%d" : "%d", ids[k]);
	if (used < cap - 1)
		snprintf(buf + used, cap - used, "]");
}

// One [AP HIT] roster line per Hit race load (ruling 6, 2026-09-14): what was
// seated, whether it was a fresh draw or a retry, and the inputs the draw saw.
static void LOAD_HitLogRoster(int level, int isCup, int player, int fresh,
                              const int *seated, int n, const int *extras, int nExtras)
{
	unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
	unsigned char unchecked[CTR_CFG_HIT_CHARACTER_COUNT];
	int elig[CTR_CFG_HIT_CHARACTER_COUNT], unhit[CTR_CFG_HIT_CHARACTER_COUNT];
	int nElig = 0, nUnhit = 0, k;
	int cur[3] = {-1, -1, -1};
	unsigned draws = 0;
	char sElig[64], sUnhit[80], sSeat[48], sExtra[24], line[400];

	AP_HitEncounterGather(eligible, unchecked);
	for (k = 0; k < CTR_CFG_HIT_CHARACTER_COUNT; k++)
	{
		if (k >= 8 && eligible[k])
			elig[nElig++] = k;
		if (k != player && eligible[k] && unchecked[k])
			unhit[nUnhit++] = k;
	}
	AP_HitEncounterDrawState(isCup ? 100 + level : level, cur, &draws);
	LOAD_HitFormatIds(sElig, sizeof sElig, elig, nElig);
	LOAD_HitFormatIds(sUnhit, sizeof sUnhit, unhit, nUnhit);
	LOAD_HitFormatIds(sSeat, sizeof sSeat, seated, n);
	LOAD_HitFormatIds(sExtra, sizeof sExtra, extras, nExtras);
	snprintf(line, sizeof line,
	         "[AP HIT] roster lvl=%d cup=%d player=%d apply=1 draw=%s n=%u checked=%d "
	         "eligible=%s unhit=%s seated=%s extras=%s cur=%d,%d,%d\n",
	         isCup ? 100 + level : level, isCup, player, fresh ? "fresh" : "reuse", draws,
	         ap_net_checked_count(), sElig, sUnhit, sSeat, sExtra, cur[0], cur[1], cur[2]);
	AP_LogLine(line);
}
#endif

// NOTE(aalhendi): Retail queues fixed LOD slots and pack files in branch-local calls.
int LOAD_DriverMPK(struct BigHeader *bigfile, int levelLOD, void (*callback)(struct LoadQueueSlot *))
{
	int gameMode1;
	struct GameTracker *gGT;

#ifdef CTR_AP
	// Every load entry clears the previous load's Hit-encounter extras, not just
	// the ordinary branch. A hub/menu/boss load that skips the branch below must
	// not leave stale required-model ids for stage 6 to validate (ticket 06).
	LOAD_HitEncounterResetExtras();
	// Pool draw: latch whether the previous driver load was an ordinary Hit
	// race, so a restart/retry reuses its field and any other load in between
	// makes the next race draw fresh.
	AP_HitLoadBegin();
#endif

	// 3P/4P
	if ((u32)(levelLOD - LOAD_LEVEL_LOD_3P) < LOAD_LEVEL_LOD_3P4P_COUNT)
	{
#if defined(CTR_NATIVE)
		// disable_split_screen_lod (config.ini, see platform/native_config.c): load
		// the hi-res character model in 3-4P split screen instead of the low LOD.
		int racerModel = g_config.disableSplitScreenLod ? BI_RACERMODELHI : BI_RACERMODELLOW;
#else
		int racerModel = BI_RACERMODELLOW;
#endif

		// low lod CTR models
		LOAD_AppendQueue(bigfile, LT_GETADDR, racerModel + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, racerModel + GAME_CHARACTER_IDS[1], &GAME_DRIVER_MODEL_EXTRAS[1].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, racerModel + GAME_CHARACTER_IDS[2], &GAME_DRIVER_MODEL_EXTRAS[2].fileBase, LOAD_DriverMPK_SetPointer);

		// load 4P MPK of fourth player
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_4PARCADEPACK + GAME_CHARACTER_IDS[3], NULL, callback);
		return sdata->ptrMPK;
	}

	if (levelLOD == LOAD_LEVEL_LOD_1P)
	{
		gGT = GAME_TRACKER;
		gameMode1 = gGT->gameMode1;
		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) == TIME_TRIAL)
		{
			goto CheckHighAndPack;
		}

		if (
		    // adv/cutscene mpk when we just need text from MPK
		    ((gameMode1 & (GAME_CUTSCENE | ADVENTURE_ARENA)) != 0) ||

		    // credits
		    ((gGT->gameMode2 & CREDITS) != 0) ||

		    // adventure character select
		    (gGT->levelID == ADVENTURE_GARAGE))
		{
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_ADVENTUREPACK + GAME_CHARACTER_IDS[0], NULL, callback);
			return sdata->ptrMPK;
		}

		if ((gameMode1 & ADVENTURE_BOSS) != 0)
		{
			goto LoadHighAndPack;
		}

		if (
		    // If you are in Adventure cup
		    ((gameMode1 & ADVENTURE_CUP) != 0) &&

		    // purple gem cup
		    (gGT->cup.cupID == 4)

#ifdef CTR_AP
		    // Ticket 11: with the Hit feature enabled the Purple cup takes the
		    // player arcade-pack + extra-model path (four AI seats) instead of
		    // the stock four-boss pack/ID override. Feature off keeps retail.
		    && !AP_HitEncounterEnabled()
#endif

#ifdef CTR_CUSTOM_TRACKS
		    // ... unless this load IS the event race. A displaced cup keeps
		    // cupID 4 because the Gem hangs off that identity, which is the only
		    // reason it would otherwise inherit the four-boss field below. Falling
		    // through to the ordinary 1P arcade branch instead swaps the boss pack
		    // for the player's own arcade pack, and that pack is what makes a
		    // shuffled field possible at all -- see decision 11.
		    //
		    // Same predicate as the subfile override and the arena sizing, so the
		    // roster a race gets, the arena it is given and the bytes it is served
		    // can never disagree about which race this is. All three facts it
		    // reads are committed before the ten-stage loader is armed, and this
		    // function is called from stage 6.
		    && !CustomTrack_ServingLoad((int)gGT->levelID, 1, gGT->cup.cupID)
#endif
		)
		{
			// high lod model
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);

			// pack of four AIs with bosses
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + LOAD_PURPLE_GEM_CUP_AI_SET_INDEX, NULL, callback);

			GAME_CHARACTER_IDS[1] = RIPPER_ROO;
			GAME_CHARACTER_IDS[2] = PAPU_PAPU;
			GAME_CHARACTER_IDS[3] = KOMODO_JOE;
			GAME_CHARACTER_IDS[4] = PINSTRIPE;

			return sdata->ptrMPK;
		}

		// NOTE(aalhendi): Retail rereads the tracker slot before deciding whether to choose AI drivers.
		if ((GAME_TRACKER_RELOAD()->gameMode1 & (TIME_TRIAL | MAIN_MENU)) != MAIN_MENU)
		{
			LOAD_Robots1P(GAME_CHARACTER_IDS[0]);

#ifdef CTR_CUSTOM_TRACKS
			// The event race, and only it, races a shuffled field. This PERMUTES
			// what LOAD_Robots1P just wrote rather than choosing from the roster,
			// which is what makes it safe without checking anything: an AI's model
			// is resolved by name against the single pack queued below
			// (VehBirth_GetModelByName), so an id outside that pack is a
			// null-pointer crash, not a wrong model. Every permutation of
			// LOAD_Robots1P's seven ids is still seven distinct ids, still none of
			// them the player's, still all of them in that pack.
			//
			// Placed after the LOAD_Robots1P call rather than inside it because
			// AH_WarpPad.c calls that function every hub frame for the grid order,
			// and a shuffle there would be overwritten by this load anyway.
			if (CustomTrack_ServingLoad((int)gGT->levelID, (gameMode1 & ADVENTURE_CUP) != 0, gGT->cup.cupID))
			{
				int rosterIDs[CTR_CT_ROBOT_SLOTS];
				int i;
				int rosterDraws[CTR_CT_ROBOT_SLOTS - 1];

				for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
					rosterIDs[i] = GAME_CHARACTER_IDS[CTR_CT_ROBOT_FIRST_SLOT + i];

				// The adventure stream, which is what AH_WarpPad.c's grid shuffle
				// already draws from, so this consumes an RNG the race path is
				// already known to perturb rather than introducing a new one.
				for (i = 0; i < CTR_CT_ROBOT_SLOTS - 1; i++)
					rosterDraws[i] = RngDeadCoed(&sdata->advRng);

				CustomTrackPolicy_PermuteRoster(rosterIDs, CTR_CT_ROBOT_SLOTS, rosterDraws);

				for (i = 0; i < CTR_CT_ROBOT_SLOTS; i++)
					GAME_CHARACTER_IDS[CTR_CT_ROBOT_FIRST_SLOT + i] = (s16)rosterIDs[i];

				CustomTrack_Log("[CustomTracks] event race field: %d %d %d %d (of %d candidates)\n",
				                (int)GAME_CHARACTER_IDS[1], (int)GAME_CHARACTER_IDS[2], (int)GAME_CHARACTER_IDS[3],
				                (int)GAME_CHARACTER_IDS[4], CTR_CT_ROBOT_SLOTS);
			}
#endif

#ifdef CTR_AP
			// Hit Character encounters (pool draw, block schema 2): replace the
			// stock default field with a draw from the defaults plus unlocked
			// guests. Ordinary Adventure races (0..17) draw fresh per race (a
			// restart/retry reuses the field), and a track's CTR Challenge is one
			// of them: it is the same driver load as its Trophy Race, only with
			// the gameMode2 TOKEN_RACE bit set, so it seats the same field from
			// the same destination key; an Adventure Gem Cup uses its
			// FROZEN snapshot (drawn once at the pad entry, reused across every
			// leg and same-session retry). Custom-served loads keep
			// their own permute and get no roster. The player's arcade pack is
			// built around the stock set LOAD_Robots1P just wrote, so any selected
			// opponent outside it needs a BI_RACERMODELHI extra.
			{
				int isCup = (gameMode1 & ADVENTURE_CUP) != 0;
				int customServed = 0;
				int hitApply = 0;
				int aiSeats = 7;

#ifdef CTR_CUSTOM_TRACKS
				customServed = CustomTrack_ServingLoad((int)gGT->levelID, isCup,
				                                       gGT->cup.cupID);
#endif
				if (!customServed && isCup && AP_HitEncounterEnabled())
				{
					// Gem Cup: four AI in the retail Purple cup, seven otherwise,
					// matching MainInit's field count.
					aiSeats = AP_HitEncounterCupFieldSize((int)gGT->cup.cupID);
					hitApply = 1;
				}
				else if (!customServed &&
				         AP_HitEncounterShouldApply(
				             (gameMode1 & ADVENTURE_MODE) != 0, (int)gGT->levelID,
				             isCup, IS_BOSS_RACE(gameMode1),
				             (gameMode1 & ARCADE_MODE) != 0,
				             (gameMode1 & RELIC_RACE) != 0,
				             (gGT->gameMode2 & TOKEN_RACE) != 0,
				             (gameMode1 & CRYSTAL_CHALLENGE) != 0,
				             (int)gGT->numPlyrNextGame))
				{
					aiSeats = 7;
					hitApply = 1;
				}

				if (hitApply)
				{
					int hitRoster[AP_HIT_FIELD_MAX];
					int hitExtras[3];
					int hitCount, hitNeed, k;
					int hitFresh = 1;

					if (isCup)
						hitCount = AP_HitCupSnapshotField((int)gGT->cup.cupID,
						                                  (int)gGT->cup.trackIndex,
						                                  (int)GAME_CHARACTER_IDS[0],
						                                  aiSeats, hitRoster);
					else
						hitCount = AP_HitRaceField((int)gGT->levelID,
						                           (int)GAME_CHARACTER_IDS[0],
						                           aiSeats, hitRoster, &hitFresh);
					for (k = 0; k < hitCount; k++)
						GAME_CHARACTER_IDS[1 + k] = (s16)hitRoster[k];

					hitNeed = AP_HitEncounterExtras(hitRoster, hitCount,
					                                (int)GAME_CHARACTER_IDS[0],
					                                hitExtras, 3);
					LOAD_HitLogRoster(isCup ? (int)gGT->cup.cupID : (int)gGT->levelID, isCup,
					                  (int)GAME_CHARACTER_IDS[0], hitFresh, hitRoster,
					                  hitCount, hitExtras, hitNeed > 3 ? 3 : hitNeed);
					if (hitNeed > 3)
						Platform_Fatal("CTR Native - Hit Character",
						               "Hit encounter needs more driver models than the loader can sideload.");
					// The pack is queued after the extras; LOAD_AppendQueue drops
					// silently past its eight slots, so refuse up front rather
					// than race with a missing opponent model.
					if (!AP_HitQueueFitsPure((int)sdata->queueLength, hitNeed + 1, 8))
						Platform_Fatal("CTR Native - Hit Character",
						               "Hit encounter driver models do not fit the load queue.");
					for (k = 0; k < hitNeed; k++)
					{
						LOAD_AppendQueue(bigfile, LT_GETADDR,
						                 BI_RACERMODELHI + hitExtras[k],
						                 &GAME_DRIVER_MODEL_EXTRAS[k].fileBase,
						                 LOAD_DriverMPK_SetPointer);
					}
					AP_HitExtrasRecordPure(&s_hitExtras, hitExtras, hitNeed);
				}
				else if (AP_HitEncounterEnabled())
				{
					char hitLine[200];
					snprintf(hitLine, sizeof hitLine,
					         "[AP HIT] roster lvl=%d apply=0 adv=%d cup=%d boss=%d arcade=%d "
					         "relic=%d token=%d crystal=%d players=%d custom=%d\n",
					         (int)gGT->levelID, (gameMode1 & ADVENTURE_MODE) != 0, isCup,
					         IS_BOSS_RACE(gameMode1) != 0, (gameMode1 & ARCADE_MODE) != 0,
					         (gameMode1 & RELIC_RACE) != 0,
					         (gGT->gameMode2 & TOKEN_RACE) != 0,
					         (gameMode1 & CRYSTAL_CHALLENGE) != 0,
					         (int)gGT->numPlyrNextGame, customServed);
					AP_LogLine(hitLine);
				}
			}
#endif
		}

		// arcade mpk
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_1PARCADEPACK + GAME_CHARACTER_IDS[0], NULL, callback);
		return sdata->ptrMPK;
	}

// NOTE(aalhendi): Time trial rechecks the high-LOD condition; boss races enter its body directly.
CheckHighAndPack:
	if ((levelLOD == LOAD_LEVEL_LOD_RELIC) || ((GAME_TRACKER->gameMode1 & TIME_TRIAL) != 0))
	{
	LoadHighAndPack:
		// Do NOT switch the order to optimize Relic,
		// if HI+IDs[1] and PACK+IDs[0] is loaded,
		// then mask-grab breaks for all characters
		// on Hot Air Skyway (except Crash Bandicoot)

		// Load Player 1 [0]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);

		// Load boss or ghost [1]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_TIMETRIALPACK + GAME_CHARACTER_IDS[1], NULL, callback);
		return sdata->ptrMPK;
	}

	// 2P and other standard LODs
	else
	{
		// med models
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELMED + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELMED + GAME_CHARACTER_IDS[1], &GAME_DRIVER_MODEL_EXTRAS[1].fileBase, LOAD_DriverMPK_SetPointer);

		LOAD_Robots2P(bigfile, GAME_CHARACTER_IDS[0], GAME_CHARACTER_IDS[1], callback);
		return sdata->ptrMPK;
	}
}

#include "LOAD_LangFile.c"

// NOTE(aalhendi): Variable-stride group bases come last to preserve retail's return scheduling.
int LOAD_GetBigfileIndex(u32 levelID, int lod, int fileIndexInGroup)
{
	if (levelID < NITRO_COURT)
	{
		return BI_ARCADETRACKS + levelID * LOAD_TRACK_FILES_PER_LOD_GROUP + GAME_LEVEL_BIG_LOD_INDEX[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - NITRO_COURT) < LOAD_BATTLE_TRACK_COUNT)
	{
		return (levelID - NITRO_COURT) * LOAD_TRACK_FILES_PER_LOD_GROUP + GAME_LEVEL_BIG_LOD_INDEX[lod - 1] + fileIndexInGroup + BI_BATTLETRACKS;
	}

	if ((u32)(levelID - INTRO_RACE_TODAY) < LOAD_INTRO_CUTSCENE_COUNT)
	{
		return (levelID - INTRO_RACE_TODAY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_CUTSCENES_INTRO;
	}

	if ((u32)(levelID - OXIDE_ENDING) < LOAD_OUTRO_CUTSCENE_COUNT)
	{
		return (levelID - OXIDE_ENDING) * LOAD_OUTRO_FILES_PER_LEVEL + fileIndexInGroup + BI_CUTSCENES_OUTRO;
	}

	if (levelID == ADVENTURE_GARAGE)
	{
		return BI_MAINMENUFILE + LOAD_MAIN_MENU_GARAGE_FILE_OFFSET + fileIndexInGroup;
	}

	if (levelID == NAUGHTY_DOG_CRATE)
	{
		return BI_NDBOX + fileIndexInGroup;
	}

	if ((u32)(levelID - CREDITS_CRASH) < LOAD_CREDIT_LEVEL_COUNT)
	{
		return (levelID - CREDITS_CRASH) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_CREDITS;
	}

	if (levelID == MAIN_MENU_LEVEL)
	{
		return BI_MAINMENUFILE + fileIndexInGroup;
	}

	if (levelID == SCRAPBOOK)
	{
		return BI_SCRAPBOOK + fileIndexInGroup;
	}

	return (levelID - GEM_STONE_VALLEY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_ADVENTUREHUB;
}
