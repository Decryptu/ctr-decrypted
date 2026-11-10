#include <common.h>

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

enum UIRaceEndMenuOption
{
	UI_RACE_END_OPTION_QUIT = 3,
	UI_RACE_END_OPTION_RETRY = 4,
	UI_RACE_END_OPTION_CHANGE_CHARACTER = 5,
	UI_RACE_END_OPTION_CHANGE_LEVEL = 6,
	UI_RACE_END_OPTION_SAVE_GHOST = 9,
	UI_RACE_END_OPTION_CHANGE_SETUP = 10,
	UI_RACE_END_OPTION_EXIT_TO_MAP = 0xd,
	UI_RACE_END_OPTION_PRESS_TO_CONTINUE = 0xc9,
};

enum UIRaceFlowConstants
{
	UI_RACE_START_TITLE_EXIT_FRAME = 0x1f,
	UI_RACE_START_TITLE_EXIT_BASE_FRAME = 0x1e,
	UI_RACE_START_TITLE_CENTER_X = 0x100,
	UI_RACE_START_TITLE_TOP_Y_BIAS = 7,
	UI_RACE_START_CUP_TITLE_Y_OFFSET = -6,
	UI_RACE_START_CUP_TRACK_Y_OFFSET = 0xb,
	UI_RACE_START_LEVEL_TITLE_Y_OFFSET = -0x17,
	UI_RACE_START_DIVIDER_TOP_Y_OFFSET = 0x1c,
	UI_RACE_START_DIVIDER_BOTTOM_Y_OFFSET = -0x1e,
	UI_RACE_START_DIVIDER_HEIGHT = 2,
	UI_RACE_START_BAR_HEIGHT = 0x1e,
	UI_RACE_START_BAR_ALPHA_MASK = 0xff000000,
	UI_RACE_END_MIN_SPLITSCREEN_MENU_PLAYERS = 3,
	UI_RACE_END_INITIAL_ICON_COUNT = 1,
	UI_RACE_END_SAVE_GHOST_FRAME = 0x3f9,
};


#include "UI_RaceEndClock.c"


// You see this in 1P mode, right before traffic lights count down
void UI_RaceStart_IntroText1P(void)
{
	s16 barTransition;
	struct GameTracker *gGT;
	int textArrayIndex;
	int gameMode;
	int posX;
	s16 *txtArray;
	char *titleText;
	s16 titleY;
	s16 font;
	int textID;
	int titleTransition;
	char trackText[24];
#ifdef CTR_AP
	char apBossTitle[96];
#endif
	RECT rect;
	int colors[2];

#ifdef CTR_CUSTOM_TRACKS
	// What a displaced cup is called, or NULL for the retail name. Set only on
	// the adventure-cup branch below, so the arcade/VS branch that shares the
	// draw site cannot pick it up.
	const char *ctCupName = NULL;
#endif

	gGT = sdata->gGT;

	// by default, do not transition
	// title bars to off-screen
	titleTransition = 0;

	gameMode = gGT->gameMode1;

	// If you are not in a relic race
	if ((gameMode & RELIC_RACE) == 0)
	{
		// BONUS ROUND
		textID = LNG_BONUS_ROUND;

		// If you are not in Crystal challenge
		if ((gameMode & CRYSTAL_CHALLENGE) == 0)
		{
			// If you are not in Adventure Cup
			if ((gameMode & ADVENTURE_CUP) == 0)
			{
				// If you are not in Arcade or VS cup
				if ((gGT->gameMode2 & CUP_ANY_KIND) == 0)
				{
					// ARCADE
					textID = LNG_ARCADE;

					if (
					    // If you're in Arcade Mode
					    ((gameMode & ARCADE_MODE) != 0) ||

					    (
					        // TIME TRIAL
					        textID = LNG_TIME_TRIAL,

					        // if you are in time trial mode
					        (gameMode & TIME_TRIAL) != 0))
					{
						goto LAB_80055930;
					}

					if (-1 < gameMode)
					{
						// TROPHY RACE
						textID = LNG_TROPHY_RACE;

						// If you're in a CTR Token Race
						if ((gGT->gameMode2 & TOKEN_RACE) != 0)
						{
							// CTR CHALLENGE
							textID = LNG_CTR_CHALLENGE_TITLE;
						}
						goto LAB_80055930;
					}
					textArrayIndex = gGT->bossID;
					txtArray = &data.lng_challenge[0];
				}

				// If you are in Arcade or VS cup
				else
				{
					// Get Cup ID
					textArrayIndex = gGT->cup.cupID;
					txtArray = &data.arcadeVsCupStringIndex[0];
				}
			}

			// If you are in Adventure Cup
			else
			{
				// Get Cup ID
				textArrayIndex = gGT->cup.cupID;
				txtArray = &data.advCupStringIndex[0];

#ifdef CTR_CUSTOM_TRACKS
				// A displaced cup is one race on someone else's track, so the
				// retail cup name over the start line names content the player is
				// not about to race. Same redirect predicate as the leg counter
				// below, so the banner's two halves cannot disagree.
				ctCupName = CustomTrack_CupDisplayName(gGT->cup.cupID, 1);
#ifdef CTR_AP
				// A content-plan cup shows its plan name, and a boss cup is named
				// for its boss (A2-47), through the call the standings use (A3-5).
				if (AP_ContentCupName() != NULL)
					ctCupName = AP_ContentCupName();
#endif
#endif
			}

			// Get the name of the cup
			// Wumpa, Nitro, Crystal
			// Red, Green, Purple, etc
			textID = txtArray[textArrayIndex];
		}
	}

	// If you are in Relic Race
	else
	{
		// RELIC RACE
		textID = LNG_RELIC_RACE;
	}
LAB_80055930:

	// if fly-in animation is one second away from finishing
	if (gGT->cameraDC->transitionFrame < UI_RACE_START_TITLE_EXIT_FRAME)
	{
		// use this to transition title bars to off-screen
		titleTransition = UI_RACE_START_TITLE_EXIT_BASE_FRAME - gGT->cameraDC->transitionFrame;
	}

	// RaceFlag_IsFullyOnScreen
	int raceFlagFullyOnScreen = RaceFlag_IsFullyOnScreen();

	// if not
	if (raceFlagFullyOnScreen == 0)
	{
		// Draw big string
		font = FONT_BIG;

		// used for transitioning bars to off-screen
		barTransition = titleTransition;

		if (

		    // If you are not in Adventure cup
		    ((gameMode & ADVENTURE_CUP) == 0) &&

		    // If you are not in Arcade or VS cup
		    (((gGT->gameMode2 & CUP_ANY_KIND) == 0)))
		{
			// X-value, X + W/2
			posX = gGT->pushBuffer[0].rect.x + ((gGT->pushBuffer[0].rect.w << 0x10) >> 0x11);

			// string of top title bar
			titleText = sdata->lngStrings[textID];
#ifdef CTR_AP
			// #169 tier 2: a garage racing a drawn racer is that racer's
			// challenge here too, as the garage title says (A2-44).
			if ((gameMode & ADVENTURE_BOSS) != 0 &&
			    AP_BossChallengeTitle(gGT->bossID, apBossTitle, (int)sizeof apBossTitle))
				titleText = apBossTitle;
#endif

			// Y-value that transitions title text to off-screen
			titleY = gGT->pushBuffer[0].rect.y - (barTransition - UI_RACE_START_TITLE_TOP_Y_BIAS);
		}

		// If you are in any cup of any kind
		else
		{
			// Name of Cup

			// uVar9 * 4
			DecalFont_DrawLine(
#ifdef CTR_CUSTOM_TRACKS
			    ctCupName != NULL ? (char *)ctCupName : sdata->lngStrings[textID],
#else
			    sdata->lngStrings[textID],
#endif

			                   gGT->pushBuffer[0].rect.x + ((gGT->pushBuffer[0].rect.w << 0x10) >> 0x11),

			                   ((gGT->pushBuffer[0].rect.y - (titleTransition - UI_RACE_START_TITLE_TOP_Y_BIAS)) + UI_RACE_START_CUP_TITLE_Y_OFFSET), FONT_BIG,
			                   (JUSTIFY_CENTER | ORANGE));

			// Track 1/4, 2/4, 3/4, 4/4 in cup
#ifdef CTR_CUSTOM_TRACKS
			// A redirected event cup has ONE leg, not four, so the retail "/4" would
			// promise legs the game will never load. Same predicate as the completion
			// fork, so the counter cannot disagree with it.
			sprintf(trackText, "%s %ld/%d",

			        sdata->lngStrings[LNG_TRACK],

			        // Track Index (0, 1, 2, 3) + 1
			        CTR_PRINTF_PSX_LONG((gGT->cup.trackIndex) + 1),

			        CustomTrack_CupLegCount(gGT->cup.cupID, (gGT->gameMode2 & CUP_ANY_KIND) == 0));
#else
			sprintf(trackText, "%s %ld/4",

			        sdata->lngStrings[LNG_TRACK],

			        // Track Index (0, 1, 2, 3) + 1
			        CTR_PRINTF_PSX_LONG((gGT->cup.trackIndex) + 1));
#endif

			// string of top title bar
			titleText = trackText;

			// X-value, centered
			posX = UI_RACE_START_TITLE_CENTER_X;

			// Draw small string
			font = FONT_SMALL;

			// Y-value that transitions title text to off-screen
			titleY = (gGT->pushBuffer[0].rect.y - (titleTransition - UI_RACE_START_TITLE_TOP_Y_BIAS)) + UI_RACE_START_CUP_TRACK_Y_OFFSET;
		}

		// Print top title text "Arcade, Time Trial, etc"
		DecalFont_DrawLine(titleText, posX, titleY, font, (JUSTIFY_CENTER | ORANGE));

		// Print the name of the level
		// Crash Cove, Roos Tubes, etc
		DecalFont_DrawLine(

		    // 83a88 = 110
		    // 8d878 + 110*4 -> Dingo Canyon

		    // Level ID
#ifdef CTR_AP
		    // Schema 15: the Cortex Vortex pad track is not Oxide Station.
		    AP_CortexTrackDisplayName() ? (char *)AP_CortexTrackDisplayName() :
#endif
#ifdef CTR_CUSTOM_PACKAGES
		    // A custom-page race shows its own package title.
		    MainRaceTrack_OfflineCustomTitle() ? (char *)MainRaceTrack_OfflineCustomTitle() :
#endif
		    sdata->lngStrings[data.metaDataLEV[gGT->levelID].name_LNG],

		    gGT->pushBuffer[0].rect.x + ((gGT->pushBuffer[0].rect.w << 0x10) >> 0x11),

		    (gGT->pushBuffer[0].rect.y + gGT->pushBuffer[0].rect.h + titleTransition + UI_RACE_START_LEVEL_TITLE_Y_OFFSET), FONT_BIG,
		    (JUSTIFY_CENTER | ORANGE));

		// same for all
		rect.x = gGT->pushBuffer[0].rect.x;
		rect.w = gGT->pushBuffer[0].rect.w;

		// 2-pixel height
		// random generic color
		colors[0] = sdata->battleSetup_Color_UI_1;
		rect.h = UI_RACE_START_DIVIDER_HEIGHT;

		// Draw tiny rectangle near big black title bar (first)
		rect.y = gGT->pushBuffer[0].rect.y - (barTransition - UI_RACE_START_DIVIDER_TOP_Y_OFFSET);

		Color color;
		ColorCode_SetPacked(&color, colors[0]);

		u32 *ot = gGT->backBuffer->otMem.uiOT;

		CTR_Box_DrawSolidBox(&rect, &color, ot, &gGT->backBuffer->primMem);

		// Draw tiny rectangle near big black title bar (second)
		rect.y = gGT->pushBuffer[0].rect.y + gGT->pushBuffer[0].rect.h + barTransition + UI_RACE_START_DIVIDER_BOTTOM_Y_OFFSET;
		CTR_Box_DrawSolidBox(&rect, &color, ot, &gGT->backBuffer->primMem);

		// 30-pixel height
		// clear RGB, keep alpha (which is zero anyway)
		colors[0] = colors[0] & UI_RACE_START_BAR_ALPHA_MASK;
		ColorCode_SetPacked(&color, colors[0]);
		rect.h = UI_RACE_START_BAR_HEIGHT;

		// draw big black title bar (first)
		rect.y = gGT->pushBuffer[0].rect.y - barTransition;
		CTR_Box_DrawSolidBox(&rect, &color, ot, &gGT->backBuffer->primMem);

		// draw big black title bar (second)
		rect.y = gGT->pushBuffer[0].rect.y + gGT->pushBuffer[0].rect.h + barTransition + UI_RACE_START_DIVIDER_BOTTOM_Y_OFFSET;
		CTR_Box_DrawSolidBox(&rect, &color, ot, &gGT->backBuffer->primMem);
	}
	return;
}

// Exit To Map, shared by the race-end menu row and the AP relic results skip
// (RR_EndEvent_DrawMenu), which leaves the results for the hub the same way.
void UI_RaceEnd_ExitToMap(void)
{
	struct GameTracker *gGT = sdata->gGT;

#ifdef CTR_AP
	// #285: relic reward bookkeeping and the AP relic notification already
	// ran at race end (RR_EndEvent_UnlockAward). Decide the relic podium
	// here, before the hub load, while RELIC_RACE and the original prevLEV
	// hub return are still available. A qualifying Oxide relic keeps
	// STATIC_RELIC for CS_Camera_BoolGotoBoss; cup and boss exits share this
	// case and are never classified as skippable.
	//
	// The skip is an Adventure-relic-only action, so the invariant is
	// enforced HERE at the call site rather than left to the classification:
	// case 0xd is shared by unrelated modes, and a non-Adventure or
	// non-relic exit must not be able to reach the mutation at all.
	if (AP_PodiumSkipCallAllowed(gGT->gameMode1))
	{
		AP_SkipPodium(gGT->podiumRewardID);
	}
#endif
	sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_ARENA;
	sdata->Loading.OnBegin.RemBitsConfig8 |= TOKEN_RACE;

	sdata->Loading.OnBegin.RemBitsConfig0 |= (CRYSTAL_CHALLENGE | RELIC_RACE);

	if ((gGT->gameMode1 & ADVENTURE_CUP) != 0)
	{
#ifdef CTR_CUSTOM_TRACKS
		// Undo the event destination's lap override on the abandon path too.
		// The cup-end restore in UI_CupStandings.c only runs when the cup is
		// played out; a player who quits the 7-lap race would otherwise carry 7
		// laps into the next adventure race.
		if (CustomTrack_RaceFeatureEnabled() && gGT->numLaps != 3)
		{
			gGT->numLaps = 3;
		}
#endif

		sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_CUP;
		s16 cupReturn = GEM_STONE_VALLEY;
#ifdef CTR_AP
		// Under destination shuffle a gem cup can be entered from any hub, so
		// return to the recorded entry hub instead of always Gemstone Valley
		// (else exiting a cup with GV still locked spawns outside the map).
		int apCupReturn = AP_CupReturnHub();
		if (apCupReturn >= 0)
			cupReturn = (s16)apCupReturn;
#endif
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
		// A boss cup (#133 PR 6b) returns to its garage like a boss race.
		AP_ContentGarageCupReturn(gGT, AP_ContentGarageCupRunning());
#endif
		MainRaceTrack_RequestLoad(cupReturn);
		return;
	}

	if (IS_BOSS_RACE(gGT->gameMode1))
	{
		sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_BOSS;
		sdata->Loading.OnBegin.AddBitsConfig8 |= SPAWN_AT_BOSS;
	}

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	// A standalone content-plan race returns to the hub it was entered from,
	// as the pause menu's exit does (MainFreeze.c), even after a restart.
	{
		const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gGT->levelID);
		if (race)
		{
			MainRaceTrack_RequestLoad((s16)race->returnHub);
			return;
		}
	}
#endif
	MainRaceTrack_RequestLoad(gGT->prevLEV);
}

void UI_RaceEnd_MenuProc(struct RectMenu *menu)
{
	s16 option;
	struct GameTracker *gGT;

	gGT = sdata->gGT;

	if (menu->funcState != RECTMENU_FUNC_STATE_INPUT)
	{
		menu->drawStyle &= ~RECTMENU_DRAW_STYLE_3P4P_LAYOUT;

		// if more than 2 screens
		if (UI_RACE_END_MIN_SPLITSCREEN_MENU_PLAYERS <= gGT->numPlyrCurrGame)
		{
			menu->drawStyle |= RECTMENU_DRAW_STYLE_3P4P_LAYOUT;
		}

		return;
	}

	int row = menu->rowSelected;
	if (row < 0)
	{
		return;
	}

	option = menu->rows[row].stringIndex;

	// if not SAVE GHOST
	if (option != UI_RACE_END_OPTION_SAVE_GHOST)
	{
		// make Menu invisible
		RECTMENU_Hide(menu);
	}

	sdata->framesSinceRaceEnded = 0;
	sdata->numIconsEOR = UI_RACE_END_INITIAL_ICON_COUNT;

	// Press * To Continue
	// do not put this in the switch,
	// switch needs to be a "small" jump table,
	// and an offest this large could bloat table
	if (option == UI_RACE_END_OPTION_PRESS_TO_CONTINUE)
	{
		sdata->menuReadyToPass |= 1;
		return;
	}

	switch (option)
	{
	// Quit
	case UI_RACE_END_OPTION_QUIT:
	{
		// Erase ghost of previous race from RAM
		GhostTape_Destroy();

		// go back to main menu
		sdata->mainMenuState = MAIN_MENU_TITLE;

		// load LEV of main menu
		MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
		break;
	}

	case UI_RACE_END_OPTION_RETRY:
	{
		// Turn off HUD
		gGT->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;

		if (RaceFlag_IsFullyOffScreen())
		{
			RaceFlag_BeginTransition(1);
		}

		sdata->Loading.stage = LOAD_RESTART;

		// clear backup,
		// keep music,
		// destroy "most" fx, let menu fx play to end
		howl_StopAudio(1, 0, 0);

		// if did not improve time, then dont
		// overwrite old ghost with new ghost
		if ((gGT->gameModeEnd & PLAYER_GHOST_BEAT) == 0)
		{
			break;
		}

		sdata->boolReplayHumanGhost = 1;

		// slower than ND's copy, I know, we'll
		// come up with a modern-gcc friendly way
		// to sort the LWs and SWs later
		memcpy(sdata->ptrGhostTapePlaying, sdata->GhostRecording.ptrGhost, GHOST_RECORD_BUFFER_SIZE);

		// Make P2 the character that is saved in the
		// header of the ghost that you will see in the race
		data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;

		// no ghosts are drawing
		sdata->boolGhostsDrawing = 0;

		break;
	}

	// Change Character, or Change Level
	case UI_RACE_END_OPTION_CHANGE_CHARACTER:
	case UI_RACE_END_OPTION_CHANGE_LEVEL:
	{
		// Erase ghost of previous race from RAM
		GhostTape_Destroy();

		// MAIN_MENU_CHARACTERS or MAIN_MENU_TRACK_SELECT
		sdata->mainMenuState = (MainMenuState)(option - 4);

		// when loading is done
		// add flag for "in menus"
		sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

		// load LEV of main menu
		MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
		break;
	}

	// Save Ghost
	case UI_RACE_END_OPTION_SAVE_GHOST:
	{
#ifdef CTR_NATIVE
		// Never offered on a custom track or at 200cc (224.c); never opens the
		// memory card there.
		if (TimeTrialRecords_OffCard())
		{
			break;
		}
#endif
		sdata->framesSinceRaceEnded = FPS_DOUBLE(UI_RACE_END_SAVE_GHOST_FRAME);

		// Set Load/Save to Ghost mode
		SelectProfile_ToggleMode(SELECT_PROFILE_MODE_GHOST_SAVE);

		// Change active Menu to GhostSelection
		sdata->ptrDesiredMenu = &data.menuGhostSelection;
		break;
	}

	// Change Setup
	case UI_RACE_END_OPTION_CHANGE_SETUP:
	{
		// go to battle setup screen
		sdata->mainMenuState = MAIN_MENU_BATTLE_SETUP;

		sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

		// load LEV of main menu
		MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
		break;
	}

	// Exit To Map
	case UI_RACE_END_OPTION_EXIT_TO_MAP:
	{
		UI_RaceEnd_ExitToMap();
		break;
	}
}
}
