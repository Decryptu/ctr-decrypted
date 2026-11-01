#include <common.h>
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
#include <platform/native_custom_tracks.h>
#endif

enum ArcadeAdventureEndMenuConstants
{
	AA_SCREEN_DEPTH = 0x200,
	AA_BIG_NUM_TARGET_SCALE = 0x1e00,
	AA_CTR_HUD_SLOT = 0x12,
	AA_CTR_LETTER_BASE_SCALE = 0x800,
	AA_CTR_LETTER_SCALE_BIAS_LOW = 0x401,
	AA_CTR_LETTER_GROW_STEP = 0x400,
	AA_CTR_LETTER_FLYIN_FRAMES = 8,
	AA_CTR_LETTER_FLYOUT_FRAMES = 10,
	AA_TOKEN_GROW_LIMIT = 0x2001,
	AA_TOKEN_GROW_STEP = 0x200,
	AA_TOKEN_AWARD_TEXT_FLY_FRAMES = 8,
	AA_CTR_TEXT_FLYIN_START_FRAME = 140,
	AA_CTR_TEXT_FLYOUT_START_FRAME = 230,
	AA_CTR_TEXT_FLYOUT_AWARD_OFFSET = 50,
	AA_CTR_ALREADY_UNLOCKED_FLYOUT_FRAME = CTR_SECONDS_TO_FRAMES(10),
	AA_CONFIRM_BUTTON_MASK = BTN_CROSS_one | BTN_CIRCLE,
	AA_MENU_READY_FLAG = 1,
	AA_RESULT_WAIT_FRAMES = CTR_SECONDS_TO_FRAMES(1),
	AA_RESULT_MAX_FRAMES = CTR_SECONDS_TO_FRAMES(30),
	AA_DRIVER_ICON_STAGGER_FRAMES = 10,
	AA_DRIVER_ICON_SPACING = 56,
	AA_DRIVER_ICON_EXIT_FRAME = CTR_SECONDS_TO_FRAMES(10),
	AA_DRIVER_ICON_SCALE = 0x1000,
	AA_DRIVER_ICON_GRAY_CHANNEL = 0x80,
	AA_CONTINUE_DELAY_FRAMES = 110,
	AA_CTR_LETTER_FALL_DELAY_FRAMES = 6,
	AA_CTR_LETTER_FALL_MIN_Y = -300,
	AA_CTR_LETTER_FALL_MIN_VEL_Y = -0x14,
	AA_KEY_BOSS_COUNT = 4,
	AA_OXIDE_SECOND_WIN_BOSS_ID = 5,
	AA_HUD_ELEMENTS_PER_DRIVER = 0x14,
	AA_TIME_DISPLAY_BIG_NUM_SLOT = 2,
	AA_TIME_DISPLAY_SUFFIX_SLOT = 5,
	AA_TIME_DISPLAY_SKIP_FRAME = 110,
	AA_TIME_DISPLAY_LATE_FRAME = CTR_SECONDS_TO_FRAMES(10),
	AA_TIME_DISPLAY_FLYIN_FRAMES = CTR_SECONDS_TO_FRAMES(1),
	AA_TIME_DISPLAY_FLYOUT_FRAMES = 15,
	AA_TIME_BOX_HEIGHT_7_LAPS = 0x49,
	AA_TIME_BOX_HEIGHT_5_LAPS = 0x39,
	AA_TIME_BOX_HEIGHT_DEFAULT = 0x44,
	AA_ADD_CONFIG_0_PAGE_OFFSET = -0x2f00,
};

// NOTE(aalhendi): Retail stores this writable one-character string before the
// overlay code.
global_variable s32 s_driverRankString222 CTR_PSX_MATCH_SECTION(".rodata") = 0x20;
extern struct RectMenu menu222;
extern struct RectMenu menu222_2P;

// NOTE(aalhendi): The matching build overrides these defaults through its
// private retail-symbol header. Native code reaches the same state through the
// canonical aggregates.
#ifndef gameHudC
#define gameHudC                 (sdata->ptrHudC)
#define gameHudT                 (sdata->ptrHudT)
#define gameHudR                 (sdata->ptrHudR)
#define gameNumIconsEOR          (sdata->numIconsEOR)
#define gameAddConfig8           (sdata->Loading.OnBegin.AddBitsConfig8)
#define gameRemoveConfig8        (sdata->Loading.OnBegin.RemBitsConfig8)
#define gameHudStructs           (data.hudStructPtr)
#define gameMenuRetryExit        (data.menuRetryExit)
#endif

void AA_EndEvent_DrawMenu(void)
{
	struct Driver *driver;
	b32 use30HzStep = true;
	b32 didEarnCtrToken;
	struct GameTracker **gameTrackerSlot;
	struct GameTracker *raceGameTracker;
	register struct GameTracker *adventureGameTracker CTR_PSX_REGISTER("$5");
	register struct PushBuffer *pushBuffer CTR_PSX_REGISTER("$22");
	struct UiElement2D *hudArray;
	struct Instance *hudC;
	register struct Instance *hudT CTR_PSX_REGISTER("$6");
	register struct Instance *hudWork CTR_PSX_REGISTER("$4");
	register struct Instance *hudToken CTR_PSX_REGISTER("$5");
	register struct UiElement3D *letter CTR_PSX_REGISTER("$5");
	register char *driverRankString;
	SVec2 pos;
	s16 timeOffsetFrames;
	s16 driverIconFrame;
	u16 continueDelayOffset;
	s32 rewardBit;
	s16 letterCOffsetX;
	s16 letterTOffsetX;
	s16 letterROffsetX;
	s32 letterTPositionOffsetX;
	s32 letterRPositionOffsetX;
	s32 letterRPositionY;
	s32 ctrAnimationFrame;
	b32 isCtrUnlockedFlyout;
	s16 ctrUnlockedFlyoutStartFrame;
	s32 tokenPositionY;
	b32 tokenGrowthDelayed;
	s32 tokenAwardFrame;
	s32 i;

	// NOTE(aalhendi): This compiler barrier preserves retail's address-page
	// allocation across initialization; it does not change game state.
	CTR_PSX_RELOAD(GAME_TRACKER);
	timeOffsetFrames = 0;
	continueDelayOffset = 0;
	driver = GAME_TRACKER->drivers[0];
	hudArray = gameHudStructs[GAME_TRACKER->numPlyrCurrGame - 1];
	pushBuffer = &GAME_TRACKER->pushBuffer[0];
	driverRankString = (char *)&s_driverRankString222;
#if CTR_NATIVE_60FPS
	// Per-frame steps that retail runs at 30 Hz advance on every other frame.
	use30HzStep = !CTR_NATIVE_60FPS_ACTIVE || ((GAME_TRACKER->timer & 1) != 0);
#endif
	didEarnCtrToken = (driver->driverRank == 0) && (driver->PickupLetterHUD.numCollected == 3);
#ifdef CTR_AP
	if (ctr_cfg_active() && (ctr_cfg.lettersanity_mode >= 2
#ifdef CTR_CUSTOM_TRACKS
	                         // A content-plan CTR Challenge reads its own
	                         // Lettersanity letters (#133 PR 3b).
	                         || AP_StandaloneRaceActive()
#endif
	                             ))
		didEarnCtrToken = AP_LetterTokenEarned(GAME_TRACKER->levelID, driver->driverRank == 0, driver->PickupLetterHUD.numCollected);
#endif
	if (GAME_FRAMES_SINCE_RACE_ENDED < FPS_DOUBLE(AA_RESULT_MAX_FRAMES))
	{
		GAME_FRAMES_SINCE_RACE_ENDED++;
	}
	CTR_PSX_RELOAD(GAME_FRAMES_SINCE_RACE_ENDED);

	if (driver->instBigNum->scale.x != AA_BIG_NUM_TARGET_SCALE)
	{
		driver->instFruitDisp->scale.x = 0;
		driver->instFruitDisp->scale.y = 0;
		driver->instFruitDisp->scale.z = 0;
	}

	adventureGameTracker = GAME_TRACKER;

	// If adventure token race
	if (((adventureGameTracker->gameMode1 & ADVENTURE_MODE) != 0) && ((adventureGameTracker->gameMode2 & TOKEN_RACE) != 0))
	{
		if (!didEarnCtrToken)
		{
			// A lost or incomplete token run drops the letters in a six-frame stagger.
			if (GAME_FRAMES_SINCE_RACE_ENDED < FPS_DOUBLE(AA_RESULT_MAX_FRAMES))
			{
				hudC = gameHudC;
				if (use30HzStep && ((hudC->flags & HIDE_MODEL) == 0) && (hudC->matrix.t[1] > AA_CTR_LETTER_FALL_MIN_Y))
				{
					letter = hudC->thread->object;
					hudC->matrix.t[0] += letter->vel[0];
					hudC->matrix.t[1] -= letter->vel[1];
					if (letter->vel[1] > AA_CTR_LETTER_FALL_MIN_VEL_Y)
					{
						letter->vel[1] -= 2;
					}
				}

				// NOTE(aalhendi): Retail applies T and R horizontal velocity to C
				// while each letter keeps its own vertical motion.
				hudT = gameHudT;
				if (use30HzStep && ((hudT->flags & HIDE_MODEL) == 0) && (GAME_FRAMES_SINCE_RACE_ENDED > FPS_DOUBLE(AA_CTR_LETTER_FALL_DELAY_FRAMES)) &&
				    (hudT->matrix.t[1] > AA_CTR_LETTER_FALL_MIN_Y))
				{
					letter = hudT->thread->object;
					gameHudC->matrix.t[0] += letter->vel[0];
					hudT->matrix.t[1] -= letter->vel[1];
					if (letter->vel[1] > AA_CTR_LETTER_FALL_MIN_VEL_Y)
					{
						letter->vel[1] -= 2;
					}
				}

				hudT = gameHudR;
				if (use30HzStep && ((hudT->flags & HIDE_MODEL) == 0) && (GAME_FRAMES_SINCE_RACE_ENDED > FPS_DOUBLE(AA_CTR_LETTER_FALL_DELAY_FRAMES * 2)) &&
				    (hudT->matrix.t[1] > AA_CTR_LETTER_FALL_MIN_Y))
				{
					letter = hudT->thread->object;
					gameHudC->matrix.t[0] += letter->vel[0];
					hudT->matrix.t[1] -= letter->vel[1];
					if (letter->vel[1] > AA_CTR_LETTER_FALL_MIN_VEL_Y)
					{
						letter->vel[1] -= 2;
					}
				}
			}

			driver->PickupLetterHUD.numCollected = 0;
		}
		else
		{
			register s32 tokenRewardBit CTR_PSX_REGISTER("$3");

			CTR_PSX_CLOBBER("$3");
			tokenRewardBit = adventureGameTracker->levelID + ADV_REWARD_FIRST_CTR_TOKEN;
#ifdef CTR_AP
			// Schema 15: the Cortex Vortex pad track's own CTR token identity,
			// never Oxide Station's token bit for LevelID 13.
			if (AP_CortexTrackActive())
				tokenRewardBit = AP_CortexTrackBit(AP_CV_SLOT_TOKEN);
#ifdef CTR_CUSTOM_TRACKS
			// A content-plan CTR Challenge celebrates its pad's own check
			// (#133 PR 3), never the engine host's token bit.
			else if (AP_StandaloneCtrBit() >= 0)
				tokenRewardBit = AP_StandaloneCtrBit();
#endif
#endif

#ifdef CTR_AP
			if (AP_CortexTrackActive()                          ? AP_CortexTrackChecked(AP_CV_SLOT_TOKEN)
#ifdef CTR_CUSTOM_TRACKS
			    : AP_StandaloneCtrBit() >= 0                     ? AP_LocationCheckedByBit(tokenRewardBit)
#endif
			    : AP_TrialTrackConfigured(GAME_TRACKER->levelID) ? AP_TrialTrackLocationChecked(GAME_TRACKER->levelID, CTR_CFG_TRIAL_CTR)
			                                                     : (CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, tokenRewardBit) != 0))
#else
			if (CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, tokenRewardBit))
#endif
			{
				ctrAnimationFrame = GAME_FRAMES_SINCE_RACE_ENDED;
				ctrUnlockedFlyoutStartFrame = FPS_DOUBLE(AA_CTR_ALREADY_UNLOCKED_FLYOUT_FRAME);
				isCtrUnlockedFlyout = ctrUnlockedFlyoutStartFrame < ctrAnimationFrame;
				if (isCtrUnlockedFlyout)
				{
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hudArray[AA_CTR_HUD_SLOT].x + 0x10, hudArray[AA_CTR_HUD_SLOT].y + 0x10, -400,
					                 hudArray[AA_CTR_HUD_SLOT].y + 0x10, ctrAnimationFrame - FPS_DOUBLE(AA_CTR_ALREADY_UNLOCKED_FLYOUT_FRAME), FPS_DOUBLE(AA_CTR_LETTER_FLYOUT_FRAMES));
				}
				else
				{
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hudArray[AA_CTR_HUD_SLOT].x, hudArray[AA_CTR_HUD_SLOT].y, hudArray[AA_CTR_HUD_SLOT].x + 0x10,
					                 hudArray[AA_CTR_HUD_SLOT].y + 0x10, ctrAnimationFrame, FPS_DOUBLE(AA_CTR_LETTER_FLYIN_FRAMES));
				}

				gameHudC->matrix.t[0] = UI_ConvertX_2(pos.x, AA_SCREEN_DEPTH);
				gameHudC->matrix.t[1] = UI_ConvertY_2(pos.y, AA_SCREEN_DEPTH);
				gameHudT->matrix.t[0] = UI_ConvertX_2(pos.x + 0x1d, AA_SCREEN_DEPTH);
				gameHudT->matrix.t[1] = UI_ConvertY_2(pos.y - 1, AA_SCREEN_DEPTH);
				gameHudR->matrix.t[0] = UI_ConvertX_2(pos.x + 0x3a, AA_SCREEN_DEPTH);
				gameHudR->matrix.t[1] = UI_ConvertY_2(pos.y, AA_SCREEN_DEPTH);
			}
			else
			{
				register s32 scaleBaseOffset CTR_PSX_REGISTER("$4");
				register s32 scaleOffset CTR_PSX_REGISTER("$3");

				scaleBaseOffset = gameHudC->scale.x - AA_CTR_LETTER_BASE_SCALE;
				scaleOffset = scaleBaseOffset;
				if (scaleBaseOffset < 0)
				{
					scaleOffset = gameHudC->scale.x - AA_CTR_LETTER_SCALE_BIAS_LOW;
				}
				scaleOffset >>= 10;
				CTR_PSX_KEEP_VALUE(scaleOffset);
				scaleOffset *= 3;
				CTR_PSX_KEEP_VALUE(scaleOffset);
				letterCOffsetX = 0;
				letterTOffsetX = scaleOffset * 4;
				CTR_PSX_KEEP_VALUE(letterTOffsetX);

				pos.x = hudArray[AA_CTR_HUD_SLOT].x;
				pos.y = hudArray[AA_CTR_HUD_SLOT].y;
				ctrAnimationFrame = GAME_FRAMES_SINCE_RACE_ENDED;
				letterROffsetX = scaleOffset * 8;

				if (FPS_DOUBLE(AA_CTR_TEXT_FLYOUT_START_FRAME) < ctrAnimationFrame)
				{
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hudArray[AA_CTR_HUD_SLOT].x + 0x10, hudArray[AA_CTR_HUD_SLOT].y + 0x50, -400,
					                 hudArray[AA_CTR_HUD_SLOT].y + 0x50, ctrAnimationFrame - FPS_DOUBLE(AA_CTR_TEXT_FLYOUT_START_FRAME), FPS_DOUBLE(AA_CTR_LETTER_FLYOUT_FRAMES));
				}
				else
				{
					if (FPS_DOUBLE(AA_CTR_TEXT_FLYIN_START_FRAME) < ctrAnimationFrame)
					{
						UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hudArray[AA_CTR_HUD_SLOT].x + 0x10, hudArray[AA_CTR_HUD_SLOT].y + 0x10,
						                 hudArray[AA_CTR_HUD_SLOT].x - 0x10, hudArray[AA_CTR_HUD_SLOT].y + 0x50,
						                 ctrAnimationFrame - FPS_DOUBLE(AA_CTR_TEXT_FLYIN_START_FRAME), FPS_DOUBLE(AA_CTR_LETTER_FLYIN_FRAMES));

						if (gameHudC->scale.x == AA_CTR_LETTER_BASE_SCALE)
						{
							OtherFX_Play(0x67, 1);
						}

						// NOTE(aalhendi): Retail ends growth when the X lerp reaches its
						// target; there is no independent scale clamp.
						if (use30HzStep && (pos.x != hudArray[AA_CTR_HUD_SLOT].x - 0x10))
						{
							register struct Instance *growthHudT CTR_PSX_REGISTER("$5");
							register u16 growthScale CTR_PSX_REGISTER("$2");
							register u32 growthHudPage CTR_PSX_REGISTER("$3");

							// NOTE(aalhendi): GCC 2.8.1 interleaves the next HUD pointer load
							// with each letter's scale stores. Keep the split updates in order.
							hudC = gameHudC;
							growthScale = hudC->scale.x + AA_CTR_LETTER_GROW_STEP;
							hudC->scale.y += AA_CTR_LETTER_GROW_STEP;
							CTR_PSX_MEMORY_BARRIER();
							CTR_PSX_LOAD_SYMBOL_PAGE(growthHudPage, GAME_HUD_T_ASM_NAME);
							hudC->scale.x = growthScale;
							growthScale = hudC->scale.z;
							CTR_PSX_LOAD_WORD_FROM_PAGE(growthHudT, growthHudPage, GAME_HUD_T_ASM_NAME, gameHudT);
							hudC->scale.z = growthScale + AA_CTR_LETTER_GROW_STEP;

							growthScale = growthHudT->scale.x + AA_CTR_LETTER_GROW_STEP;
							growthHudT->scale.y += AA_CTR_LETTER_GROW_STEP;
							CTR_PSX_MEMORY_BARRIER();
							CTR_PSX_LOAD_SYMBOL_PAGE(growthHudPage, GAME_HUD_R_ASM_NAME);
							growthHudT->scale.x = growthScale;
							growthScale = growthHudT->scale.z;
							CTR_PSX_LOAD_WORD_FROM_PAGE(hudWork, growthHudPage, GAME_HUD_R_ASM_NAME, gameHudR);
							growthHudT->scale.z = growthScale + AA_CTR_LETTER_GROW_STEP;

							hudWork->scale.x += AA_CTR_LETTER_GROW_STEP;
							hudWork->scale.y += AA_CTR_LETTER_GROW_STEP;
							hudWork->scale.z += AA_CTR_LETTER_GROW_STEP;
						}
					}
					else
					{
						UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hudArray[AA_CTR_HUD_SLOT].x, hudArray[AA_CTR_HUD_SLOT].y, hudArray[AA_CTR_HUD_SLOT].x + 0x10,
						                 hudArray[AA_CTR_HUD_SLOT].y + 0x10, ctrAnimationFrame, FPS_DOUBLE(AA_CTR_LETTER_FLYIN_FRAMES));
					}
				}

				gameHudC->matrix.t[0] = UI_ConvertX_2(pos.x + letterCOffsetX, AA_SCREEN_DEPTH);
				gameHudC->matrix.t[1] = UI_ConvertY_2(pos.y, AA_SCREEN_DEPTH);
				letterTPositionOffsetX = letterTOffsetX + 0x1d;
				gameHudT->matrix.t[0] = UI_ConvertX_2(pos.x + letterTPositionOffsetX, AA_SCREEN_DEPTH);
				gameHudT->matrix.t[1] = UI_ConvertY_2(pos.y - 1, AA_SCREEN_DEPTH);
				letterRPositionOffsetX = letterROffsetX + 0x3a;
				gameHudR->matrix.t[0] = UI_ConvertX_2(pos.x + letterRPositionOffsetX, AA_SCREEN_DEPTH);
				letterRPositionY = UI_ConvertY_2(pos.y, AA_SCREEN_DEPTH);
				hudWork = gameHudR;
				hudWork->depthBiasNormal = 1;

				hudToken = GAME_TOKEN;
				hudWork->matrix.t[1] = letterRPositionY;
#ifdef CTR_AP
				if (!AP_TrialTrackConfigured(GAME_TRACKER->levelID))
					AP_CeremonyRewardProp(hudToken, tokenRewardBit);
#endif
				hudToken->flags &= ~HIDE_MODEL;
				GAME_TOKEN->matrix.t[0] = UI_ConvertX_2(pos.x + letterTPositionOffsetX, AA_SCREEN_DEPTH);
				tokenPositionY = UI_ConvertY_2(pos.y + 0x18, AA_SCREEN_DEPTH);
				tokenGrowthDelayed = GAME_FRAMES_SINCE_RACE_ENDED <= FPS_DOUBLE(AA_CTR_TEXT_FLYIN_START_FRAME);
				hudWork = GAME_TOKEN;
				hudWork->matrix.t[1] = tokenPositionY;

				if (use30HzStep && !tokenGrowthDelayed && (hudWork->scale.x < AA_TOKEN_GROW_LIMIT))
				{
					hudWork->scale.x += AA_TOKEN_GROW_STEP;
					hudWork->scale.y += AA_TOKEN_GROW_STEP;
					hudWork->scale.z += AA_TOKEN_GROW_STEP;
				}
#ifdef CTR_AP
				{
					s32 growLimit = (s32)((u32)AA_TOKEN_GROW_LIMIT * (u32)AP_WarpPadRewardScale(tokenRewardBit) >> 12);
					if (hudWork->scale.x > growLimit)
						hudWork->scale.x = hudWork->scale.y = hudWork->scale.z = (s16)growLimit;
				}
#endif

				tokenAwardFrame = GAME_FRAMES_SINCE_RACE_ENDED;
				if (tokenAwardFrame >= FPS_DOUBLE(AA_CTR_TEXT_FLYOUT_START_FRAME) + 1)
				{
					// NOTE(aalhendi): Retail uses frames - 50 here, skipping most of
					// the text fly-out.
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x100, 0xa6, -150, 0xa6, tokenAwardFrame - FPS_DOUBLE(AA_CTR_TEXT_FLYOUT_AWARD_OFFSET),
					                 FPS_DOUBLE(AA_TOKEN_AWARD_TEXT_FLY_FRAMES));
#ifdef CTR_AP
					// AP: show the CTR-token challenge location's scouted reward. The
					// check sends on the continue-press, so pass the celebrated bit.
					if (AP_TrialTrackConfigured(GAME_TRACKER->levelID) || !AP_CeremonyDraw(pos.x, pos.y, tokenRewardBit, 0))
#endif
						DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_CTR_TOKEN_AWARDED], pos.x, pos.y, FONT_BIG,
						                   (FPS_HALF(GAME_TRACKER->timer) & 1) ? (s16)(JUSTIFY_CENTER | RED) : (s16)(JUSTIFY_CENTER | WHITE));
				}
				else
				{
					if (tokenAwardFrame <= FPS_DOUBLE(AA_CTR_TEXT_FLYIN_START_FRAME))
					{
						goto finish_token_award_text;
					}
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x264, 0xa6, 0x100, 0xa6, tokenAwardFrame - FPS_DOUBLE(AA_CTR_TEXT_FLYIN_START_FRAME),
					                 FPS_DOUBLE(AA_TOKEN_AWARD_TEXT_FLY_FRAMES));
#ifdef CTR_AP
					// AP: show the CTR-token challenge location's scouted reward. The
					// check sends on the continue-press, so pass the celebrated bit.
					if (AP_TrialTrackConfigured(GAME_TRACKER->levelID) || !AP_CeremonyDraw(pos.x, pos.y, tokenRewardBit, 0))
#endif
						DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_CTR_TOKEN_AWARDED], pos.x, pos.y, FONT_BIG,
						                   (FPS_HALF(GAME_TRACKER->timer) & 1) ? (s16)(JUSTIFY_CENTER | RED) : (s16)(JUSTIFY_CENTER | WHITE));
				}
			finish_token_award_text:

				continueDelayOffset = 120;
				timeOffsetFrames = 160;
			}
		}
	}

	for (i = 0; i < GAME_TRACKER->numPlyrCurrGame; i++)
	{
		// Draw how much time it took to finish laps and race
		AA_EndEvent_DisplayTime(i, timeOffsetFrames);
		CTR_PSX_CLOBBER("$23");
	}

	// NOTE(aalhendi): Keep the repeated frame test. GCC 2.8.1 emits the two
	// retail branches from this source shape.
	if ((GAME_FRAMES_SINCE_RACE_ENDED >= FPS_DOUBLE(AA_RESULT_WAIT_FRAMES)) && (GAME_FRAMES_SINCE_RACE_ENDED >= FPS_DOUBLE(AA_RESULT_WAIT_FRAMES)) &&
	    (GAME_TRACKER->numPlyrCurrGame == 1))
	{
		struct GameTracker *iconGameTracker;
		register struct MetaDataCHAR *characterMetadata CTR_PSX_REGISTER("$23");
		register s32 totalRacers CTR_PSX_REGISTER("$6");
		register s32 totalRacersPending CTR_PSX_REGISTER("$7");
		s32 loopTotalRacers;
		register s32 lastRacerIndex CTR_PSX_REGISTER("$20");
		s32 driverIconLerpFrame;

		CTR_PSX_KEEP_VALUE(driverRankString);
		iconGameTracker = GAME_TRACKER;
		CTR_PSX_CLOBBER("$6");

		// start counting time 1 second after race ends
		driverIconFrame = (GAME_FRAMES_SINCE_RACE_ENDED & 0xffff) - FPS_DOUBLE(AA_RESULT_WAIT_FRAMES);
		{
			register s32 botRacers CTR_PSX_REGISTER("$3");

			totalRacers = iconGameTracker->numPlyrCurrGame;
			botRacers = iconGameTracker->numBotsNextGame;
			totalRacers += botRacers;
		}
		totalRacersPending = totalRacers;

		if (
		    // Every 10 frames
		    ((s16)(driverIconFrame % FPS_DOUBLE(AA_DRIVER_ICON_STAGGER_FRAMES)) == 0) &&

		    // gameNumIconsEOR is the number of icons being
		    // drawn on the end-of-race menu in 1P mode

		    // If you have not drawn all drivers yet
		    (gameNumIconsEOR < totalRacers))
		{
			// add an icon to draw
			gameNumIconsEOR++;
		}

		i = 0;
		if (gameNumIconsEOR > 0)
		{
			register u32 characterMetadataPage CTR_PSX_REGISTER("$2");

			loopTotalRacers = totalRacersPending;
			CTR_PSX_KEEP_VALUE(loopTotalRacers);
			lastRacerIndex = loopTotalRacers - 1;
			CTR_PSX_KEEP_VALUE(lastRacerIndex);
			// NOTE(aalhendi): Materialize the absolute table address in retail's
			// two steps so the icon loop keeps the same register allocation.
			CTR_PSX_LOAD_SYMBOL_PAGE(characterMetadataPage, RETAIL_CHARACTER_METADATA_ASM_NAME);
			CTR_PSX_ADD_SYMBOL_LOW(characterMetadata, characterMetadataPage, RETAIL_CHARACTER_METADATA_ASM_NAME, GAME_CHARACTER_METADATA);
			driverIconLerpFrame = driverIconFrame;

			// loop through all the driver icons
			do
			{
				register s16 *characterIDs CTR_PSX_REGISTER("$3");
				register char *rankText CTR_PSX_REGISTER("$4");
				u32 iconColor;
				s32 characterID;
				s32 driverIconCurrentFrame;
				s32 driverIconTargetX;
				s32 iconID;

				driverIconTargetX =
				    (u16)pushBuffer->rect.x + (pushBuffer->rect.w - (loopTotalRacers * 44 + lastRacerIndex * 12)) / 2 + (i * AA_DRIVER_ICON_SPACING);

				driverIconCurrentFrame = GAME_FRAMES_SINCE_RACE_ENDED;
				if (FPS_DOUBLE(AA_DRIVER_ICON_EXIT_FRAME) - timeOffsetFrames < driverIconCurrentFrame)
				{
					s32 driverIconExitFrame;

					driverIconExitFrame = driverIconCurrentFrame - FPS_DOUBLE(AA_DRIVER_ICON_EXIT_FRAME);
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), driverIconTargetX, 0x60, -100, 0x60, driverIconExitFrame + timeOffsetFrames,
					                 FPS_DOUBLE(AA_DRIVER_ICON_STAGGER_FRAMES));
				}
				else
				{
					UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x218, 0x60, driverIconTargetX, 0x60, driverIconLerpFrame, FPS_DOUBLE(AA_DRIVER_ICON_STAGGER_FRAMES));
				}
				rankText = driverRankString;

				driverRankString[0] = (char)i + '1';

				// print a single character, a number 1-8,
				DecalFont_DrawLine(rankText, pos.x + 0x20, 0x5f, FONT_SMALL, WHITE);
				driverIconLerpFrame -= FPS_DOUBLE(AA_DRIVER_ICON_STAGGER_FRAMES);

				iconColor = MakeColorPacked(AA_DRIVER_ICON_GRAY_CHANNEL, AA_DRIVER_ICON_GRAY_CHANNEL, AA_DRIVER_ICON_GRAY_CHANNEL);
				CTR_PSX_FORGET_VALUE(iconColor);
				characterIDs = GAME_CHARACTER_IDS;
				characterID = characterIDs[GAME_TRACKER->driversInRaceOrder[i]->driverID];
				iconID = characterMetadata[characterID].iconID;

				// Draw the driver's character icon
				UI_DrawDriverIcon(

				    GAME_TRACKER->ptrIcons[iconID],

				    pos.x, 0x60, &GAME_TRACKER->backBuffer->primMem,

				    // pointer to OT mem
				    GAME_TRACKER->pushBuffer_UI.ptrOT,

				    1, AA_DRIVER_ICON_SCALE, iconColor);
				i++;
			} while (i < gameNumIconsEOR);
		}
	}

	// 0x78 + 0x6e = 0xe6 (230) frames waited for Token Race
	if (GAME_FRAMES_SINCE_RACE_ENDED < continueDelayOffset + AA_CONTINUE_DELAY_FRAMES)
	{
		return;
	}

	if (
	    // If you are in Adventure cup
	    ((GAME_TRACKER->gameMode1 & ADVENTURE_CUP) != 0) ||

	    // If you are in Arcade or VS cup
	    ((GAME_TRACKER->gameMode2 & CUP_ANY_KIND) != 0))
	{
		if (GAME_TRACKER->numPlyrCurrGame == 2)
		{
			DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_PRESS_TO_CONTINUE], 0x100, 100, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
		}
		else
		{
			DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_PRESS_TO_CONTINUE], 0x100, 0xbe, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
		}

		// If you do not "Press X to continue"
		if ((GAME_ANY_PLAYER_TAP & AA_CONFIRM_BUTTON_MASK) == 0)
		{
			return;
		}

		// If you are here, it means you pressed X to continue

		// clear gamepad input
		RECTMENU_ClearInput();

		GAME_MENU_READY = 0;
		GAME_FRAMES_SINCE_RACE_ENDED = 0;
		gameNumIconsEOR = 1;

		// Disable HUD
		GAME_TRACKER->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;

		// Enable Cup Standings
		GAME_TRACKER->hudFlags |= HUD_FLAG_CUP_STANDINGS;
		return;
	}

	// If you're in Arcade mode
	if ((GAME_TRACKER->gameMode1 & ARCADE_MODE) != 0)
	{
		if (GAME_MENU_READY & AA_MENU_READY_FLAG)
		{
			return;
		}

		if (GAME_TRACKER->numPlyrCurrGame == 1)
		{
			RECTMENU_Show(&menu222);
		}
		else
		{
			RECTMENU_Show(&menu222_2P);
		}

		// record that the menu is drawing
		GAME_MENU_READY |= AA_MENU_READY_FLAG;
		return;
	}

	// Normal Adventure races require first place. Token races also require all
	// three CTR letters.
	if ((GAME_TRACKER->gameMode2 & TOKEN_RACE) == 0)
	{
		if (driver->driverRank == 0)
		{
			goto race_won;
		}
	}
	else if (didEarnCtrToken)
	{
		goto race_won;
	}
	goto race_lost;

race_won:
#ifdef CTR_AP
	// AP: the plain trophy-win screen draws NO vanilla award text, so ADD the AP
	// award block in the free space above the standings icons (y=0x60). The trophy
	// location's entry is prefixed "TROPHY WON" (so the win is acknowledged) and
	// its scouted item + player shown; podium rungs cycle after it. Only a genuine
	// trophy win that is not a boss race or a CTR-token race (which draws its own
	// block above). The trophy check sends on the continue-press, so it is passed
	// as the primary bit; podium rungs come from the ledger.
	if (((GAME_TRACKER->gameMode2 & TOKEN_RACE) == 0) && !IS_BOSS_RACE(GAME_TRACKER->gameMode1) &&
#if defined(CTR_CUSTOM_TRACKS)
	    !AP_StandaloneRaceActive() &&
#endif
	    !AP_TrialTrackConfigured(GAME_TRACKER->levelID))
		AP_CeremonyDraw(0x100, 0x40, AP_CortexTrackActive() ? AP_CortexTrackBit(AP_CV_SLOT_TROPHY) : GAME_TRACKER->levelID + ADV_REWARD_FIRST_TROPHY, 1);
#endif
	DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_PRESS_TO_CONTINUE], 0x100, 0xbe, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
	if ((GAME_ANY_PLAYER_TAP & AA_CONFIRM_BUTTON_MASK) == 0)
	{
		return;
	}
	RECTMENU_ClearInput();
	GAME_FRAMES_SINCE_RACE_ENDED = 0;
	gameNumIconsEOR = 1;
	{
		register u32 arenaConfigMask CTR_PSX_REGISTER("$4");
		register u32 addConfig0Page CTR_PSX_REGISTER("$3");
		u32 addConfig0Value;

		// NOTE(aalhendi): A normal lvalue lets GCC fold this address and changes
		// the retail register schedule, so keep the read/modify/write page-relative.
		CTR_PSX_MEMORY_BARRIER();
		CTR_PSX_LOAD_SYMBOL_PAGE(addConfig0Page, RETAIL_ADD_CONFIG_0_ASM_NAME);
		CTR_PSX_KEEP_VALUE(addConfig0Page);
		arenaConfigMask = ADVENTURE_ARENA;
		gameTrackerSlot = &GAME_TRACKER;
		raceGameTracker = gameTrackerSlot[0];
		addConfig0Value = CTR_PSX_PAGE_LVALUE(u32, addConfig0Page, AA_ADD_CONFIG_0_PAGE_OFFSET, GAME_ADD_CONFIG_0);
		CTR_PSX_PAGE_LVALUE(u32, addConfig0Page, AA_ADD_CONFIG_0_PAGE_OFFSET, GAME_ADD_CONFIG_0) = addConfig0Value | arenaConfigMask;
	}

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	if (AP_StandaloneRaceActive())
	{
		const struct CustomTrackRaceContext *context = CustomTrack_StandaloneContext(GAME_TRACKER->levelID);
		// Report the logical route before any retail reward-bit computation.
		// The source hub stays stable through same-track restart loads.
		AP_NotifyStandaloneRace();
		GAME_TRACKER->podiumRewardID = STATIC_TROPHY;
		AP_SkipPodium(GAME_TRACKER->podiumRewardID);
		gameRemoveConfig8 |= TOKEN_RACE;
		MainRaceTrack_RequestLoad(context->returnHub);
		return;
	}
#endif

	{
		register u32 bossConfigMask CTR_PSX_REGISTER("$5");
		u32 *bossConfig;

		bossConfigMask = ADVENTURE_BOSS;
		if (IS_BOSS_RACE(raceGameTracker->gameMode1))
		{
			// Return to Adventure with boss spawning enabled and boss mode removed.
			bossConfig = &gameAddConfig8;
			*bossConfig |= SPAWN_AT_BOSS;
			bossConfig = &GAME_REMOVE_CONFIG_0;
			*bossConfig |= bossConfigMask;

			// The four area bosses award keys. Oxide records story progress and
			// uses the empty podium reward.
			if (raceGameTracker->bossID < AA_KEY_BOSS_COUNT)
			{
#ifdef CTR_AP
				// AP: in AP mode the boss-key bits in the rewards are the RECEIVED-Key
				// mirror (AP_ApplyItems high-end-fills them every frame), so a player
				// who already holds enough received Keys would read as "boss already
				// beaten" on a genuine first win: podiumRewardID never becomes
				// STATIC_KEY and the post-boss outro is skipped (issue #111). Decide
				// "first time" from the AP CHECKED location state instead, exactly
				// like the trophy gate below. Repeat wins still skip: the first win
				// sends the location via AP_NotifyAdvReward, so the bit reads checked
				// from then on.
				rewardBit = raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY;
				if (ctr_cfg_active() ? !AP_LocationCheckedByBit(rewardBit) : !CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, rewardBit))
				{
					GAME_TRACKER->podiumRewardID = STATIC_KEY;
				}
				UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, rewardBit);
				AP_NotifyAdvReward(rewardBit); // AP: boss-race reward location check
#else
				if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY))
				{
					UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY);
					GAME_TRACKER->podiumRewardID = STATIC_KEY;
				}
#endif
			}
			else
			{
				if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY))
				{
					UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY);
					UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, raceGameTracker->bossID + ADV_REWARD_FIRST_PURPLE_TOKEN);
				}

				// Retail always enters an Oxide ending here. In AP, issue #244
				// requires the composed seed goal first; AP_NotifyGoal below queues
				// the presentation only when all active goal arms are satisfied.
#ifndef CTR_AP
				GAME_TRACKER->podiumRewardID = STATIC_BIG1;
#else
				GAME_TRACKER->podiumRewardID = NOFUNC;
#endif
				if ((GAME_ADV_PROGRESS.rewards[ADV_PROGRESS_WORD_STORY] & ADV_REWARD_BEAT_OXIDE_FIRST_BOSS_MASK) == 0)
				{
					GAME_ADV_PROGRESS.rewards[ADV_PROGRESS_WORD_STORY] |= ADV_REWARD_OXIDE_FIRST_WIN_FLAGS;
				}

				// Fork addition: beating Oxide makes him playable outside Adventure.
				UNLOCK_ADV_BIT(GAME_PROGRESS.unlocks, GAME_UNLOCK_BIT_OXIDE);
#ifdef CTR_AP
				AP_NotifyAdvReward(raceGameTracker->bossID + ADV_REWARD_FIRST_BOSS_KEY); // AP: story reward location check
				// Opt-in final wins collect both rewards in one notification;
				// preserve the legacy first-then-final sequence in all other modes.
				if (!(raceGameTracker->bossID == AA_OXIDE_SECOND_WIN_BOSS_ID && ctr_cfg_active() && ctr_cfg.goal_oxide == 2 && ctr_cfg.oxide_1_optional))
					AP_NotifyGoal(0); // AP: beat Oxide (first win)
				if (raceGameTracker->bossID == AA_OXIDE_SECOND_WIN_BOSS_ID)
					AP_NotifyGoal(1); // AP: beat Oxide (final win = goal)
				if (AP_GoalClaimOxideEnding())
					GAME_TRACKER->podiumRewardID = STATIC_BIG1;
#endif
			}

			// Pinstripe's key podium is in Gem Stone Valley.
#ifdef CTR_AP
			// #169: "just beat Pinstripe" is the garage, not the track
			// a shuffled seed raced him on.
			if ((AP_BossRetailRulesLevel(GAME_TRACKER->levelID) == HOT_AIR_SKYWAY) && (GAME_TRACKER->podiumRewardID == STATIC_KEY))
#else
			if ((GAME_TRACKER->levelID == HOT_AIR_SKYWAY) && (GAME_TRACKER->podiumRewardID == STATIC_KEY))
#endif
			{
				MainRaceTrack_RequestLoad(GEM_STONE_VALLEY);
				return;
			}
			MainRaceTrack_RequestLoad(GAME_TRACKER->prevLEV);
			return;
		}
	}

	// A normal Adventure win awards the first-time trophy and, when all three
	// letters were collected, the track's CTR token.
#ifdef CTR_AP
	// Cortex Vortex pad track (schema 15): host LevelID 13 is Oxide Station, so
	// return with the track's direct codes before anything below can award
	// Oxide Station's Trophy bit 19 or CTR token bit 89. Same shape as the
	// trial-track branch that follows.
	if (AP_CortexTrackActive())
	{
		int token = (GAME_TRACKER->gameMode2 & TOKEN_RACE) != 0;
		if (!token && !AP_CortexTrackChecked(AP_CV_SLOT_TROPHY))
			GAME_TRACKER->podiumRewardID = STATIC_TROPHY;
		AP_NotifyCortexTrackRace(token);
		// #285: the Cortex check is sent above; only then may the ceremony be
		// skipped. The Cortex trophy has no retail trophy bit, so it classifies
		// as an ordinary Trophy here, which is skippable when the option is on.
		AP_SkipPodium(GAME_TRACKER->podiumRewardID);
		gameRemoveConfig8 |= TOKEN_RACE;
		MainRaceTrack_RequestLoad(GAME_TRACKER->prevLEV);
		return;
	}

	// Trial-track Trophy and CTR Challenge locations are direct schema-10 codes.
	// Return before the retail levelID-derived reward path can alias them into
	// Trophy bits 22/23 or Token bits 92/93.
	if (AP_TrialTrackConfigured(GAME_TRACKER->levelID))
	{
		int challenge = (GAME_TRACKER->gameMode2 & TOKEN_RACE) ? CTR_CFG_TRIAL_CTR : CTR_CFG_TRIAL_TROPHY;
		if (challenge == CTR_CFG_TRIAL_TROPHY && !AP_TrialTrackLocationChecked(GAME_TRACKER->levelID, challenge))
			GAME_TRACKER->podiumRewardID = STATIC_TROPHY;
		AP_NotifyTrialTrackRace(GAME_TRACKER->levelID, challenge);
		// #285: the trial check is sent above; only then may the ceremony be
		// skipped (an ordinary Trophy classification when the option is on).
		AP_SkipPodium(GAME_TRACKER->podiumRewardID);
		gameRemoveConfig8 |= TOKEN_RACE;
		MainRaceTrack_RequestLoad(GAME_TRACKER->prevLEV);
		return;
	}
#endif

	rewardBit = GAME_TRACKER->levelID;
	rewardBit += ADV_REWARD_FIRST_TROPHY;
#ifdef CTR_AP
	// AP: the vanilla "already won?" gate reads the rewards, but AP_ApplyItems
	// high-end-fills your RECEIVED-trophy bits into that same set every frame. A
	// trophy race whose bit is among the received set would read "already won" and
	// skip the location check + podium backstop, so a genuinely just-won race
	// would never send "<track>: Trophy Race". Gate on the AP location
	// CHECKED-state instead. AP_NotifyAdvReward dedupes, so a re-win is safe.
	if (ctr_cfg_active() ? !AP_LocationCheckedByBit(rewardBit) : !CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, rewardBit))
#else
	if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, rewardBit))
#endif
	{
		UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, rewardBit);
#ifdef CTR_AP
		AP_NotifyAdvReward(rewardBit); // AP: trophy location check
#endif
		GAME_TRACKER->podiumRewardID = STATIC_TROPHY;
	}

#ifdef CTR_AP
	if (((GAME_TRACKER->gameMode2 & TOKEN_RACE) != 0) && didEarnCtrToken)
	{
		UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, GAME_TRACKER->levelID + ADV_REWARD_FIRST_CTR_TOKEN);
		AP_NotifyAdvReward(GAME_TRACKER->levelID + ADV_REWARD_FIRST_CTR_TOKEN); // AP: CTR token location check
	}

	// #285: the trophy and token checks were sent above (AP_NotifyAdvReward,
	// including the podium-rung backstop), so only now may the ceremony be
	// skipped. A CTR Challenge classifies from the still-set TOKEN_RACE flag.
	AP_SkipPodium(GAME_TRACKER->podiumRewardID);
#else
	if (driver->PickupLetterHUD.numCollected == 3)
	{
		UNLOCK_ADV_BIT(GAME_ADV_PROGRESS.rewards, GAME_TRACKER->levelID + ADV_REWARD_FIRST_CTR_TOKEN);
	}
#endif

	{
		u32 *removeConfig8;

		// Leave token-race mode before returning to the previous Adventure hub.
		removeConfig8 = &gameRemoveConfig8;
		*removeConfig8 |= TOKEN_RACE;
		MainRaceTrack_RequestLoad(gameTrackerSlot[0]->prevLEV);
	}
	return;

race_lost:
	if ((GAME_MENU_READY & AA_MENU_READY_FLAG) == 0)
	{
		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_PRESS_TO_CONTINUE], 0x100, 0xbe, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

		if ((GAME_ANY_PLAYER_TAP & AA_CONFIRM_BUTTON_MASK) != 0)
		{
			RECTMENU_ClearInput();
			RECTMENU_Show(&gameMenuRetryExit);
			GAME_MENU_READY |= AA_MENU_READY_FLAG;
		}
	}
}
void AA_EndEvent_DisplayTime(s16 driverId, s16 timeOffsetFrames)
{
	struct Driver *driver;
	struct GameTracker *gGT;
	register u32 gameTrackerPage CTR_PSX_REGISTER("$2");
	register struct GameTracker *hudGT CTR_PSX_REGISTER("$4");
	register struct UiElement2D **hudStructs CTR_PSX_REGISTER("$3");
	struct UiElement2D *hudArray;
	struct UiElement2D *hud;
	RECT timeBoxRect;
	SVec2 pos;
	s16 timeBoxHeight;
	s16 bigNumY;
	s32 suffixY;
	s32 clockY;
	s32 hudArrayIndex;

	// NOTE(aalhendi): Retail materializes both absolute pages before either
	// lookup. Keeping that order preserves the entry register allocation.
	CTR_PSX_LOAD_SYMBOL_PAGE(gameTrackerPage, RETAIL_GAME_TRACKER_ASM_NAME);
	CTR_PSX_LOAD_SYMBOL_PAGE(hudStructs, GAME_HUD_STRUCTS_ASM_NAME);
	CTR_PSX_LOAD_WORD_FROM_PAGE(hudGT, gameTrackerPage, RETAIL_GAME_TRACKER_ASM_NAME, GAME_TRACKER);
	CTR_PSX_ADD_SYMBOL_LOW(hudStructs, hudStructs, GAME_HUD_STRUCTS_ASM_NAME, gameHudStructs);
	CTR_PSX_BIND_VALUE_CLOBBER(hudGT, "$31");
	hudArrayIndex = hudGT->numPlyrCurrGame - 1;
	hudArray = hudStructs[hudArrayIndex];

	timeBoxHeight = AA_TIME_BOX_HEIGHT_7_LAPS;
	if (hudGT->numLaps != 7)
	{
		timeBoxHeight = AA_TIME_BOX_HEIGHT_DEFAULT;
		if (hudGT->numLaps == 5)
		{
			timeBoxHeight = AA_TIME_BOX_HEIGHT_5_LAPS;
		}
	}

	bigNumY = 0x41;
	if (driverId == 0)
	{
		bigNumY = -0x3d;
		suffixY = 9;
		clockY = 0x3e;
	}
	else
	{
		suffixY = 0x89;
		clockY = 0xc3;
	}

	gGT = GAME_TRACKER;
	driver = gGT->drivers[driverId];

	// increment counter for number of frames since the player ended the race
	driver->framesSinceRaceEnded_forThisDriver++;

	if (
	    // if player ended race less than 110 frames ago
	    (driver->framesSinceRaceEnded_forThisDriver < AA_TIME_DISPLAY_SKIP_FRAME) &&

	    // If you press Cross or Circle
	    ((GAME_ANY_PLAYER_TAP & AA_CONFIRM_BUTTON_MASK) != 0) &&

	    // only one player
	    (gGT->numPlyrCurrGame == 1))
	{
		// Assume race ended 110 frames ago
		driver->framesSinceRaceEnded_forThisDriver = AA_TIME_DISPLAY_SKIP_FRAME;
		GAME_FRAMES_SINCE_RACE_ENDED = AA_TIME_DISPLAY_SKIP_FRAME;

		gameNumIconsEOR = gGT->numPlyrCurrGame + gGT->numBotsNextGame;

		// clear gamepad input (for menus)
		RECTMENU_ClearInput();
	}

	// === Draw BigNum ===

	// If race ended more than 10 seconds ago.
	if (FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME) - timeOffsetFrames < driver->framesSinceRaceEnded_forThisDriver)
	{
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), -0xae, bigNumY,
		                 UI_ConvertX_2(-100, hudArray[driverId * AA_HUD_ELEMENTS_PER_DRIVER + AA_TIME_DISPLAY_BIG_NUM_SLOT].z), bigNumY,
		                 driver->framesSinceRaceEnded_forThisDriver + (timeOffsetFrames - FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME)), AA_TIME_DISPLAY_FLYOUT_FRAMES);
	}

	// If not
	else
	{
		struct UiElement2D *bigNumHud;

		bigNumHud = (struct UiElement2D *)((u32)(driverId * AA_HUD_ELEMENTS_PER_DRIVER * sizeof(*hudArray)) + (u32)hudArray);
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), UI_ConvertX_2(bigNumHud[AA_TIME_DISPLAY_BIG_NUM_SLOT].x, bigNumHud[AA_TIME_DISPLAY_BIG_NUM_SLOT].z),
		                 UI_ConvertY_2(bigNumHud[AA_TIME_DISPLAY_BIG_NUM_SLOT].y, bigNumHud[AA_TIME_DISPLAY_BIG_NUM_SLOT].z), -0xae, bigNumY,
		                 driver->framesSinceRaceEnded_forThisDriver, FPS_DOUBLE(AA_TIME_DISPLAY_FLYIN_FRAMES));
	}

	driver->instBigNum->matrix.t[0] = pos.x;
	driver->instBigNum->matrix.t[1] = pos.y;

	hud = (struct UiElement2D *)((u32)(driverId * AA_HUD_ELEMENTS_PER_DRIVER * sizeof(*hudArray)) + (u32)hudArray);

	// interpolate scale to the target big-number size
	UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hud[AA_TIME_DISPLAY_BIG_NUM_SLOT].scale, 0, AA_BIG_NUM_TARGET_SCALE, 0, driver->framesSinceRaceEnded_forThisDriver,
	                 FPS_DOUBLE(AA_TIME_DISPLAY_FLYIN_FRAMES));

	driver->instBigNum->scale.x = pos.x;
	driver->instBigNum->scale.y = pos.x;
	driver->instBigNum->scale.z = pos.x;

	// === Draw Suffix ===

	if (FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME) - timeOffsetFrames < driver->framesSinceRaceEnded_forThisDriver)
	{
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x78, suffixY, -0x3c, suffixY,
		                 driver->framesSinceRaceEnded_forThisDriver + (timeOffsetFrames - FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME)), AA_TIME_DISPLAY_FLYOUT_FRAMES);
	}
	else
	{
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), hud[AA_TIME_DISPLAY_SUFFIX_SLOT].x, hud[AA_TIME_DISPLAY_SUFFIX_SLOT].y, 0x78, suffixY,
		                 driver->framesSinceRaceEnded_forThisDriver, FPS_DOUBLE(AA_TIME_DISPLAY_FLYIN_FRAMES));
	}

	UI_DrawPosSuffix(pos.x, pos.y, driver, 0);

	// === DrawRaceClock ===

	if (FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME) - timeOffsetFrames < driver->framesSinceRaceEnded_forThisDriver)
	{
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x150, clockY, 0x27c, clockY,
		                 driver->framesSinceRaceEnded_forThisDriver + (timeOffsetFrames - FPS_DOUBLE(AA_TIME_DISPLAY_LATE_FRAME)), AA_TIME_DISPLAY_FLYOUT_FRAMES);
	}

	else
	{
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&pos), 0x218, clockY, 0x150, clockY, driver->framesSinceRaceEnded_forThisDriver, FPS_DOUBLE(AA_TIME_DISPLAY_FLYIN_FRAMES));
	}

	UI_DrawRaceClock(pos.x, pos.y, UI_RACE_CLOCK_SHOW_RESULTS, driver);

	timeBoxRect.x = (pos.x - DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[LNG_TOTAL], FONT_BIG)) + -6;
	timeBoxRect.y = (pos.y - timeBoxHeight) + 0xd;
	timeBoxRect.w = DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[LNG_TOTAL], FONT_BIG) + 0x94;
	timeBoxRect.h = timeBoxHeight + 6;

	// Draw 2D Menu rectangle background
	RECTMENU_DrawInnerRect(&timeBoxRect, 4, GAME_TRACKER->backBuffer->otMem.uiOT);
	return;
}

struct MenuRow rows222[5] = {
    // Retry
    {
        .stringIndex = LNG_RETRY,
        .rowOnPressUp = 0,
        .rowOnPressDown = 1,
        .rowOnPressLeft = 0,
        .rowOnPressRight = 0,
    },
    // Change Level
    {
        .stringIndex = LNG_CHANGE_LEVEL,
        .rowOnPressUp = 0,
        .rowOnPressDown = 2,
        .rowOnPressLeft = 1,
        .rowOnPressRight = 1,
    },
    // Change Character
    {
        .stringIndex = LNG_CHANGE_CHARACTER,
        .rowOnPressUp = 1,
        .rowOnPressDown = 3,
        .rowOnPressLeft = 2,
        .rowOnPressRight = 2,
    },
    // Quit
    {
        .stringIndex = LNG_QUIT,
        .rowOnPressUp = 2,
        .rowOnPressDown = 3,
        .rowOnPressLeft = 3,
        .rowOnPressRight = 3,
    },
    // NULL, end of menu
    {
        .stringIndex = RECTMENU_STRING_NONE,
        .rowOnPressUp = 0,
        .rowOnPressDown = 0,
        .rowOnPressLeft = 0,
        .rowOnPressRight = 0,
    }};

struct RectMenu menu222 = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .posX_curr = 256,
    .posY_curr = 170,
    .unk1 = 0,
    .state = RECTMENU_STATE_SMALL_CENTERED,
    .rows = rows222,
    .funcPtr = UI_RaceEnd_MenuProc,
    .drawStyle = 4,
    // rest of variables all default zero
};

struct RectMenu menu222_2P = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .posX_curr = 256,
    .posY_curr = 108,
    .unk1 = 0,
    .state = RECTMENU_STATE_SMALL_CENTERED,
    .rows = rows222,
    .funcPtr = UI_RaceEnd_MenuProc,
    .drawStyle = 4,
    // rest of variables all default zero
};
