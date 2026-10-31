#include <common.h>

#ifdef CTR_AP
#include "../../ap/ap_hooks.h" // AP_PodiumExitTerminalWork (#235)
#include "../../ap/ap_boss_door_scene_logic.h" // AP_BossDoorSceneTaken (#377)
#endif

enum CutsceneCameraConstants
{
	CS_CAMERA_FADE_FULL = FP_ONE,
	CS_CAMERA_FADE_STEP = 0x400,
	CS_BOSS_MODEL_SCALE = FP_ONE,
	CS_BOSS_CAMERA_ROT_X_OFFSET = ANG_PI,
	CS_PODIUM_PATH_FRAME_FRACTION_BITS = 5,
	CS_PODIUM_PATH_FRAME_UNIT = 1 << CS_PODIUM_PATH_FRAME_FRACTION_BITS,
	CS_PODIUM_PRIZE_DROP_LEAD_TIME = 0x12c0,
	CS_PODIUM_CONTINUE_TEXT_X = 0x100,
	CS_PODIUM_CONTINUE_TEXT_Y = 0xbe,
};

b32 CS_Camera_BoolGotoBoss(void)
{
	struct Instance *inst;
	const SVec3 *podiumPos;
	struct GameTracker *gGT = GAME_TRACKER;

	// If the relic requirement that opens Oxide's Final Challenge is now met.
	// AP (WO-A4): follow the SHIPPED gate (AP_OxideFinalOpen, issue #23 --
	// per-seed relic mode + count) instead of the retail 18-Sapphire rule.
	// Under AP `numRelics` counts RECEIVED Sapphire Relic items, so the retail
	// rule fires on the 18th item to arrive from anywhere in the multiworld,
	// which is not this seed's Oxide gate. Vanilla answer is unchanged.
	//
	// The beat-Oxide suppression is the SAME term AP_ShouldSkipPodium consults
	// (issue #285): one helper answers "this relic podium will go to Oxide" for
	// both, so a skipped relic can never be one whose transition still plays.
	// Issue #377: under AP the scene plays once per seed, on reaching go mode
	// for the Final Challenge (AP_OxideFinalSceneReady, ap/ap_oxide_scene.c).
#ifdef CTR_AP
	if (AP_PodiumRelicWillGotoOxide(
	        gGT->podiumRewardID == STATIC_RELIC,
	        AP_OxideFinalSceneReady(gGT->currAdvProfile.numRelics),
	        (sdata->advProgress.rewards[MEMCARD_BIT_WORD(ADV_REWARD_BEAT_OXIDE_SECOND)] & ADV_REWARD_BEAT_OXIDE_SECOND_MASK) != 0))
	{
		return 1;
	}
#else
	// If just got 18th relic
	if ((gGT->podiumRewardID == STATIC_RELIC) && (gGT->currAdvProfile.numRelics >= ADV_OXIDE_FINAL_RELIC_COUNT))
	{
		// If Oxide was not beaten twice yet
		if ((sdata->advProgress.rewards[MEMCARD_BIT_WORD(ADV_REWARD_BEAT_OXIDE_SECOND)] & ADV_REWARD_BEAT_OXIDE_SECOND_MASK) == 0)
		{
			return 1;
		}
	}
#endif

	gGT = GAME_TRACKER;
	// If just unlocked Key
	if (gGT->podiumRewardID == STATIC_KEY)
	{
		return 1;
	}

	inst = gGT->drivers[0]->instSelf;
	podiumPos = &gGT->level1->ptrSpawnType2_PosRot[1].coords.posRot->pos;

	// TRUE if TeleportSelf did NOT spawn on podium (goto boss door)
	if (inst->matrix.t[0] == podiumPos->x && inst->matrix.t[2] == podiumPos->z)
		return 0;
	return 1;
}

void CS_Camera_ThTick_Boss(struct Thread *t)
{
	const struct BossCutsceneData *bcd;
	s32 i;
	struct CsThreadInitData initData;
	// NOTE(aalhendi): Retail reserves 32 unused bytes after the thread setup.
	u8 unusedWorkspace[32];
	(void)unusedWorkspace;
	if (CS_BOSS_INDEX >= 0)
		bcd = &D233.bossCS[CS_BOSS_INDEX];
	else
	{
		struct GameTracker *gt = GAME_TRACKER;
		// Each hub has an arrival scene and a key-award scene.
		i = (gt->levelID - GEM_STONE_VALLEY) * 2;
		if (gt->podiumRewardID == STATIC_KEY)
			i++;
		bcd = &D233.bossCS[i];
	}
	switch (CS_PHASE)
	{
	case CS_CAMERA_PAN:
	case CS_WAIT_INPUT:
	{
		struct GameTracker *gt = GAME_TRACKER;
		gt->pushBuffer_UI.fadeFromBlack_desiredResult = 0;
		gt->pushBuffer_UI.fade_step = -CS_CAMERA_FADE_STEP;
		CS_PHASE = CS_FADE_OUT;
		break;
	}
	case CS_FADE_OUT:
	{
		struct Thread *cur;
		struct GameTracker *gt = GAME_TRACKER;
		if (gt->pushBuffer_UI.fadeFromBlack_currentValue != 0)
			break;
		for (cur = gt->threadBuckets[OTHER].thread; cur != NULL; cur = cur->siblingThread)
			cur->flags |= THREAD_FLAG_DEAD;
		// Wait for the recycler before loading the next model pack.
		if (GAME_TRACKER->threadBuckets[OTHER].thread != NULL)
			break;
		CS_LoadBoss(bcd);
		CS_PHASE = CS_LOADING;
		break;
	}
	case CS_LOADING:
	{
		struct Thread *spawned;
		struct GameTracker *gt;
		struct PushBuffer *pb;
		if (CS_BOSS_HEAD_MODEL == NULL)
			break;
		// The loader publishes the head last; the body file also has a size word.
		i = 0;
		{
			struct GameTracker *modelTracker;
			modelTracker = GAME_TRACKER;
			for (; i < 2; i++)
			{
				if (CS_BOSS_MODELS[i] != NULL)
				{
					if (i != 0)
						CS_BOSS_MODELS[i] = (struct Model *)((u8 *)CS_BOSS_MODELS[i] + 4);
					modelTracker->modelPtr[CS_BOSS_MODELS[i]->id] = CS_BOSS_MODELS[i];
				}
			}
		}
		MEMPACK_SwapPacks(GAME_TRACKER->activeMempackIndex);
		spawned = NULL;
		initData.podiumPos.x = bcd->bossPos.x;
		initData.podiumPos.y = bcd->bossPos.y;
		initData.podiumPos.z = bcd->bossPos.z;
		initData.rot.x = bcd->bossRot.x;
		initData.rot.y = bcd->bossRot.y;
		initData.rot.z = bcd->bossRot.z;
		initData.characterPos.x = 0;
		initData.characterPos.y = 0;
		initData.characterPos.z = 0;
		for (i = 1; i >= 0; i--)
		{
			struct Instance *inst;
			struct CutsceneObj *cs;
			// Spawn the body first, then attach the scripted head to it.
			if (i > 0)
				spawned = NULL;
			if (CS_BOSS_MODELS[i] == NULL)
				continue;
			spawned = CS_Thread_Init(CS_BOSS_MODELS[i]->id, CS_BOSS_MODELS[i]->name, &initData, 0, spawned);
			if (spawned == NULL)
				continue;
			inst = spawned->inst;
			cs = spawned->object;
			if (i != 0)
			{
				inst->scale.x = CS_BOSS_MODEL_SCALE;
				inst->scale.y = CS_BOSS_MODEL_SCALE;
				inst->scale.z = CS_BOSS_MODEL_SCALE;
				cs->desiredScale = CS_BOSS_MODEL_SCALE;
			}
			else
			{
				CS_ScriptCmd_OpcodeAt(cs, bcd->opcode);
				cs->opcodeDuration = 0;
			}
		}
		gt = GAME_TRACKER;
		gt->pushBuffer[0].pos.x = bcd->camPos.x;
		pb = &gt->pushBuffer[0];
		pb->pos.y = bcd->camPos.y;
		pb->pos.z = bcd->camPos.z;
		pb->rot.x = bcd->camRot.x + CS_BOSS_CAMERA_ROT_X_OFFSET;
		pb->rot.y = bcd->camRot.y;
		pb->rot.z = bcd->camRot.z;
		gt->pushBuffer_UI.fadeFromBlack_desiredResult = CS_CAMERA_FADE_FULL;
		gt->pushBuffer_UI.fade_step = CS_CAMERA_FADE_STEP;
		CS_PHASE = CS_FADE_IN;
		break;
	}
	case CS_FADE_IN:
		if (GAME_TRACKER->pushBuffer_UI.fadeFromBlack_currentValue != CS_CAMERA_FADE_FULL)
			break;
		CS_PHASE = CS_WAIT_END;
		break;
	case CS_WAIT_END:
		if (CS_FINISHED != 1)
			break;
		GAME_TRACKER->podiumRewardID = NOFUNC;
		t->flags |= THREAD_FLAG_DEAD;
	}
}

void CS_Camera_ThTick_Podium(struct Thread *th)
{
	struct GameTracker *gGT;
	struct CsPodiumCameraThreadObj *podium = th->object;

#ifdef CTR_AP
	// The custom Trophy check is sent on the Cup-standings continue press, after
	// the race screen has finished drawing. Present its scouted reward (plus the
	// podium-rung ledger) on the hub podium where the player can actually see it.
	if (sdata->ptrActiveMenu == NULL)
		AP_CustomTrackTrophyCeremonyDraw(0x100, 0x38);

	// Issue #235: an AP retail-Trophy podium bypasses the retail prize/count-up
	// (CS_Podium_FullScene_Init), so present the AP-owned received count and the
	// #330 item text here instead. Self-gates on AP-active + an ordinary retail
	// Trophy, so it is a no-op beside the custom-Trophy block above and on every
	// other podium.
	if (sdata->ptrActiveMenu == NULL)
		AP_TrophyPodiumCeremonyDraw(0x100, 0x38);
#endif

	if (podium->pathFrame32 == 0)
	{
		GAME_TRACKER->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] = VehStuckProc_RIP_Init;
	}

	if (GAME_TRACKER->cameraDC[0].cameraMode != CAMERA_MODE_FREECAM)
	{
		if (CS_PHASE < CS_WAIT_INPUT)
		{
			CS_PHASE = CS_WAIT_INPUT;
		}

		CS_PRIZE_DROP_READY = 1;
	}

	if (((CS_PHASE != CS_CAMERA_PAN || CS_CAN_SKIP != 0) && ((GAME_TRACKER->gameMode2 & CUP_NEW_WIN) != 0)) && sdata->ptrActiveMenu == NULL)
	{
		s16 stringIndex = LNG_SAVE_YOUR_CUP_PROGRESS;

		if ((GAME_TRACKER->gameMode2 & CUP_NEW_BATTLE) != 0)
		{
			stringIndex = LNG_SAVE_YOUR_CUP_PROGRESS_NEW_BATTLE_ARENA_OPENED;
		}

		TakeCupProgress_Activate(stringIndex);
		{
			// The save menu can replace the active tracker.
			struct GameTracker *cupTracker;
			cupTracker = GAME_TRACKER;
			cupTracker->gameMode2 &= ~CUP_NEW_WIN;
			cupTracker->gameMode2 &= ~CUP_NEW_BATTLE;
		}
	}

	if (CS_PHASE != CS_CAMERA_PAN && sdata->ptrActiveMenu == NULL)
	{
		if ((GAME_TRACKER->gameMode2 & CUP_NEW_WIN) != 0)
		{
			goto check_skip_button;
		}

		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_PRESS_TO_CONTINUE], CS_PODIUM_CONTINUE_TEXT_X, CS_PODIUM_CONTINUE_TEXT_Y, FONT_BIG,
		                   JUSTIFY_CENTER | ORANGE);
	}
	else
	{
		s32 numPoints = CAM_Path_GetNumPoints();
		// NOTE(aalhendi): Retail truncates the path clock before signed comparisons.
		s16 maxFrame = numPoints * CS_PODIUM_PATH_FRAME_UNIT;

		if (maxFrame != 0)
		{
			u16 frameTime = podium->pathFrame32 + GAME_TRACKER->elapsedTimeMS;
			s32 frameTimeSigned = (s16)frameTime;
			SVec3 pos;
			SVec3 rot;
			s16 camPath[4];
			s32 frame;

			if (maxFrame - CS_PODIUM_PRIZE_DROP_LEAD_TIME < frameTimeSigned)
			{
				CS_PRIZE_DROP_READY = 1;
			}

			if (maxFrame <= frameTimeSigned)
			{
				frameTime = numPoints * CS_PODIUM_PATH_FRAME_UNIT - 1;

				if (CS_PHASE < CS_WAIT_INPUT)
				{
					CS_PHASE = CS_WAIT_INPUT;
				}
			}

			frame = (s16)frameTime >> CS_PODIUM_PATH_FRAME_FRACTION_BITS;
			CS_PODIUM_CAMERA_FRAME = frame;
			podium->pathFrame32 = frameTime;

			CAM_Path_Move(frame, CTR_VECTOR_DATA(&(pos)), CTR_VECTOR_DATA(&(rot)), camPath);

			{
				struct GameTracker *cameraTracker;
				struct PushBuffer *pb;
				cameraTracker = GAME_TRACKER;
				cameraTracker->pushBuffer[0].pos.x = pos.x;
				pb = &cameraTracker->pushBuffer[0];
				pb->pos.y = pos.y;
				pb->pos.z = pos.z;
				pb->rot.x = rot.x;
				pb->rot.y = rot.y;
				pb->rot.z = rot.z;
			}
		}
	}

	if (((GAME_TRACKER->gameMode2 & CUP_NEW_WIN) == 0) && sdata->ptrActiveMenu == NULL)
	{
		u32 modeFlags = GAME_TRACKER->gameMode2;
		u32 tapped = GAMEPADS->gamepad[0].buttonsTapped;
		s32 rewardId;

		if (((tapped & BTN_START) == 0) && ((CS_PHASE == CS_CAMERA_PAN || (tapped & (BTN_START | BTN_CROSS_one)) == 0)) &&
		    ((modeFlags & VEH_FREEZE_PODIUM) != 0))
		{
			return;
		}

		gGT = GAME_TRACKER;
		if ((gGT->gameMode1 & ADVENTURE_MODE) != 0)
		{
			CS_PRIZE_DROP_READY = 1;
			rewardId = gGT->podiumRewardID;
			gGT->numWinners = 0;
			gGT->renderFlags &= ~RENDER_FLAG_CONFETTI;

			if (rewardId == STATIC_BIG1)
			{
				gGT->podiumRewardID = NOFUNC;
				gGT->gameMode1 &= ~ADVENTURE_ARENA;
				gGT->gameMode2 &= ~VEH_FREEZE_PODIUM;

				if (sdata->advProgress.rewards[ADV_REWARD_BEAT_OXIDE_SECOND >> 5] & (1u << (ADV_REWARD_BEAT_OXIDE_SECOND & 31)))
					MainRaceTrack_RequestLoad(OXIDE_TRUE_ENDING);
				else
					MainRaceTrack_RequestLoad(OXIDE_ENDING);

				th->flags |= THREAD_FLAG_DEAD;
				return;
			}
			else
			{
#ifdef CTR_AP
				// Issue #377, local Skip Cutscenes option: a podium that would hand
				// over to a boss or Oxide scene takes the ordinary exit below
				// instead (see AP_CutsceneSkipDecision, ap/ap_podium_skip_logic.h).
				// A skipped Oxide Final Challenge scene, or the Gemstone key outro
				// that chains into it, still counts as seen for this slot.
				int apSkipScene = AP_CutsceneSkipDecision(AP_SkipCutscenes(),
				                                          CS_Camera_BoolGotoBoss() != 0);
				if (apSkipScene &&
				    AP_CutsceneSkipCoversOxideScene(
				        rewardId == STATIC_RELIC, rewardId == STATIC_KEY,
				        gGT->levelID == GEM_STONE_VALLEY,
				        AP_OxideFinalSceneReady(gGT->currAdvProfile.numRelics)))
				{
					AP_OxideFinalSceneMarkPlayed(); // #377: skipped counts as seen
				}
				// Issue #377: a Trophy podium only hands over to a scene when
				// VehBirth spawned the driver at the boss door, which under AP
				// happens once per hub (AP_BossDoorSceneReady). Record the hub
				// here, watched or skipped, so the next Trophy win stays on the
				// podium.
				if (AP_BossDoorSceneTaken(rewardId == STATIC_TROPHY,
				                          CS_Camera_BoolGotoBoss() != 0))
				{
					AP_BossDoorSceneMarkPlayed(gGT->levelID - N_SANITY_BEACH);
				}
				if (apSkipScene || !CS_Camera_BoolGotoBoss())
#else
				if (!CS_Camera_BoolGotoBoss())
#endif
				{
					s32 hintID;

					CS_FINISHED = 1;
					th->flags |= THREAD_FLAG_DEAD;

					CS_DestroyPodium_StartDriving();

#ifdef CTR_AP
					// Issue #235: the AP ordinary-Trophy presentation births no
					// prize thread, so reproduce the terminal work that thread
					// would have performed when the ceremony ended. No-op on every
					// podium whose prize thread still exists.
					AP_PodiumExitTerminalWork();
#endif

					// Use the resumed race's reward and driver for the spoken hint.
					switch (GAME_TRACKER->podiumRewardID)
					{
					case STATIC_TROPHY:
						hintID = ADV_MASK_HINT_ID_TROPHY_AWARDED;
						break;
					case STATIC_KEY:
						hintID = ADV_MASK_HINT_ID_KEY_AWARDED;
						break;
					case STATIC_RELIC:
						hintID = ADV_MASK_HINT_ID_RELIC_AWARDED;
						break;
					case STATIC_TOKEN:
						hintID = ADV_MASK_HINT_ID_CTR_TOKEN_AWARDED;
						break;
					default:
						hintID = ADV_MASK_HINT_ID_GEM_AWARDED;
						break;
					}

					if (!(s16)VehPickupItem_MaskBoolGoodGuy(GAME_TRACKER->drivers[0]))
					{
						hintID += ADV_MASK_HINT_UKA_UKA_XA_OFFSET;
					}

					CDSYS_XAPauseForce();
					CDSYS_XAPlay(1, hintID);

					GAME_TRACKER->podiumRewardID = NOFUNC;
					return;
				}

				{
					struct GameTracker *bossTracker;
					th->funcThTick = CS_Camera_ThTick_Boss;
					bossTracker = GAME_TRACKER;

					CS_BOSS_INDEX = -1;
					if (bossTracker->podiumRewardID != STATIC_RELIC)
						return;
					// AP (WO-A4): same gate as CS_Camera_BoolGotoBoss above. These two
					// MUST use one predicate: BoolGotoBoss decides that a boss cutscene
					// happens at all, and this decides it is the OXIDE_RELICS_<hub> one.
					// If they disagreed, a relic win would enter the boss path and then
					// fall through to the ordinary hub intro (bossCutsceneIndex -1).
					// Issue #377: both run in this frame, before the play is recorded
					// below, so they read the same once-per-seed answer.
#ifdef CTR_AP
					if (!AP_OxideFinalSceneReady(bossTracker->currAdvProfile.numRelics))
#else
					if (bossTracker->currAdvProfile.numRelics < ADV_OXIDE_FINAL_RELIC_COUNT)
#endif
						return;
					CS_BOSS_INDEX = bossTracker->levelID - GEM_STONE_VALLEY + OXIDE_RELICS_GEMSTONE;
#ifdef CTR_AP
					AP_OxideFinalSceneMarkPlayed(); // #377: this slot has now seen it
#endif
					return;
				}
			}
		}
		else
		{
			CS_MAIN_MENU_STATE = MAIN_MENU_TITLE;
			gGT->gameMode1 = (gGT->gameMode1 | MAIN_MENU) & ~ADVENTURE_ARENA;
			gGT->podiumRewardID = NOFUNC;
			gGT->gameMode2 &= ~VEH_FREEZE_PODIUM;
			RaceFlag_SetDrawOrder(0);
			MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
			return;
		}
	}

check_skip_button:
	if ((GAMEPADS->gamepad[0].buttonsTapped & BTN_START) != 0)
	{
		CS_CAN_SKIP = 1;
	}
}
