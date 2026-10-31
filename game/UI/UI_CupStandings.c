#include <common.h>

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

enum
{
	UI_CUP_STANDINGS_MENU_READY_END_OPTIONS = 1,
	UI_CUP_STANDINGS_PAGE_TRACK_POINTS = 0x4,
	UI_CUP_STANDINGS_PAGE_OVERALL_POINTS = 0x8,
	UI_CUP_STANDINGS_CONFIRM_BUTTONS = BTN_CROSS_one | BTN_CIRCLE,
	UI_CUP_STANDINGS_QUICK_SKIP_FRAME = 60,
	UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME = 0xf0,
	UI_CUP_STANDINGS_DONE_FRAME = 0x10f,
	UI_CUP_STANDINGS_OVERALL_HOLD_FRAME = 400,
	UI_CUP_STANDINGS_ICON_REVEAL_STEP_FRAMES = 10,
	UI_CUP_STANDINGS_LERP_FRAMES = 0x14,
	UI_CUP_STANDINGS_TITLE_START_X = -0x96,
	UI_CUP_STANDINGS_TITLE_CENTER_X = 0x100,
	UI_CUP_STANDINGS_TITLE_END_X = 0x296,
	UI_CUP_STANDINGS_TITLE_Y = 0x1e,
	UI_CUP_STANDINGS_TITLE_Y_OFFSET = -0x11,
	UI_CUP_STANDINGS_TRACK_TEXT_Y_OFFSET = 0x11,
	UI_CUP_STANDINGS_PRESS_X = 0x100,
	UI_CUP_STANDINGS_PRESS_Y = 0xbe,
	UI_CUP_STANDINGS_PRESS_PROMPT_FRAME = 0x3b,
	UI_CUP_STANDINGS_PANEL_X = -10,
	UI_CUP_STANDINGS_PANEL_Y = 9,
	UI_CUP_STANDINGS_PANEL_W = 0x214,
	UI_CUP_STANDINGS_PANEL_H = 0x32,
	UI_CUP_STANDINGS_PANEL_MOVE_Y = 0x32,
	UI_CUP_STANDINGS_PURPLE_GEM_CUP_ID = 4,
	UI_CUP_STANDINGS_PURPLE_GEM_VISIBLE_DRIVERS = 5,
	UI_CUP_STANDINGS_TWO_PLAYER_ARCADE_DRIVER_COUNT = 6,
	UI_CUP_STANDINGS_MAX_VISIBLE_AWARD_DRIVERS = 4,
	UI_CUP_STANDINGS_ICON_SLOT_X = 0x60,
	UI_CUP_STANDINGS_ICON_SLOT_W = 0x5a,
	UI_CUP_STANDINGS_ICON_TOP_ROW_Y = 0x42,
	UI_CUP_STANDINGS_ICON_BOTTOM_ROW_Y = 0x79,
	UI_CUP_STANDINGS_ICON_PURPLE_GEM_ROW_Y = 0x60,
	UI_CUP_STANDINGS_ICON_VS_ROW_Y = 0x6c,
	UI_CUP_STANDINGS_ICON_LEFT_MARGIN = 0x20,
	UI_CUP_STANDINGS_ICON_RIGHT_MARGIN = 0x10,
	UI_CUP_STANDINGS_ICON_VS_CENTER_BIAS = 0xc,
	UI_CUP_STANDINGS_PLACE_TEXT_X_OFFSET = 0x20,
	UI_CUP_STANDINGS_PLACE_TEXT_Y_OFFSET = -1,
	UI_CUP_STANDINGS_POINTS_TEXT_X_OFFSET = 0x2d,
	UI_CUP_STANDINGS_POINTS_TEXT_Y_OFFSET = 6,
	UI_CUP_STANDINGS_ICON_SCALE = 0x1000,
	UI_CUP_STANDINGS_ICON_NEUTRAL_CHANNEL = 0x80,
	UI_CUP_STANDINGS_TRACKS_PER_CUP = 4,
	UI_CUP_STANDINGS_DRIVER_SLOTS = 8,
	UI_CUP_STANDINGS_ARCADE_DIFFICULTY_DIVISOR = 0x50,
	UI_CUP_STANDINGS_MAX_ARCADE_DIFFICULTY = 2,
	UI_CUP_STANDINGS_MAX_CUP_LOSSES = 10,
};


#include "UI_CupRanks.c"

void UI_CupStandings_InputAndDraw(void)
{
	struct GameTracker *gGT = sdata->gGT;
	s16 iconColumnOffset;
	int i;
	int animationFrame;
	int pageExitY;
	int pageEnterY;
	u32 nextMenuReadyFlags;
	s16 iconTargetX;
	u32 wasTrackPointsPage;
	int iconStartX;
	int iconEndX;
	int numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	s16 vsCupIconOffsetX;

	SVec2 drawPos;

	// Multiplayer Cup Game
	if (gGT->numPlyrCurrGame != 1)
	{
		if (((gGT->gameMode2 & CUP_ANY_KIND) != 0) && (RaceFlag_IsFullyOffScreen()))
		{
			RaceFlag_BeginTransition(1);
		}

		if (!RaceFlag_IsFullyOnScreen())
		{
			return;
		}

		// Assume FullyOnScreen
		RaceFlag_SetCanDraw(1);

		// disable loading screen,
		// set amount of confetti to zero
		gGT->confetti.numParticles_max = 0;
		gGT->confetti.vanishRate = 0;
		gGT->renderFlags &= RENDER_FLAG_CHECKERED_FLAG;
	}

	// Conditions to increment frame counter
	if ((sdata->framesSinceRaceEnded < FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME)) ||
	    ((sdata->framesSinceRaceEnded < FPS_DOUBLE(UI_CUP_STANDINGS_OVERALL_HOLD_FRAME)) &&
	     ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_OVERALL_POINTS) != 0)))
	{
		sdata->framesSinceRaceEnded++;
	}

	if ((sdata->framesSinceRaceEnded < FPS_DOUBLE(UI_CUP_STANDINGS_QUICK_SKIP_FRAME)) &&

	    // If you press Cross or Circle
	    ((sdata->AnyPlayerTap & UI_CUP_STANDINGS_CONFIRM_BUTTONS) != 0))
	{
		sdata->framesSinceRaceEnded = FPS_DOUBLE(UI_CUP_STANDINGS_QUICK_SKIP_FRAME);
		sdata->numIconsEOR = numDrivers;
		RECTMENU_ClearInput();
	}

	if ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS) == 0)
	{
		pageEnterY = -UI_CUP_STANDINGS_PANEL_MOVE_Y;
		pageExitY = UI_CUP_STANDINGS_TITLE_Y;
	}

	else
	{
		pageEnterY = UI_CUP_STANDINGS_TITLE_Y;
		pageExitY = -UI_CUP_STANDINGS_PANEL_MOVE_Y;
	}

	int titleStartX;
	int titleStartY;
	int titleEndX;
	int titleEndY;

	if (sdata->framesSinceRaceEnded <= FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME))
	{
		titleStartX = UI_CUP_STANDINGS_TITLE_START_X;
		titleStartY = pageEnterY;
		titleEndX = UI_CUP_STANDINGS_TITLE_CENTER_X;
		titleEndY = UI_CUP_STANDINGS_TITLE_Y;
		animationFrame = sdata->framesSinceRaceEnded;
	}

	else
	{
		titleStartX = UI_CUP_STANDINGS_TITLE_CENTER_X;
		titleStartY = UI_CUP_STANDINGS_TITLE_Y;
		titleEndX = UI_CUP_STANDINGS_TITLE_END_X;
		titleEndY = pageExitY;
		animationFrame = sdata->framesSinceRaceEnded - FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME);
	}

	UI_Lerp2D_Linear(CTR_VECTOR_DATA(&(drawPos)), titleStartX, titleStartY, titleEndX, titleEndY, animationFrame,
	                 FPS_DOUBLE(UI_CUP_STANDINGS_LERP_FRAMES));

	// "FINAL"
	int titleString = LNG_FINAL;
	int cupID = gGT->cup.cupID;

#ifdef CTR_CUSTOM_TRACKS
	// What a displaced cup is called, or NULL for the retail name. Assigned only
	// on the adventure-cup branch below, so it can never displace the two other
	// strings that reach the same draw: the level name and the arcade cup name.
	const char *ctCupName = NULL;
#endif

	if ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS) == 0)
	{
		// Level ID
		titleString = data.metaDataLEV[gGT->levelID].name_LNG;
	}

	else if (gGT->cup.trackIndex != UI_CUP_STANDINGS_TRACKS_PER_CUP - 1)
	{
		// If not in Arcade or VS cup
		if ((gGT->gameMode2 & CUP_ANY_KIND) == 0)
		{
			titleString = data.AdvCups[cupID].lngIndex_CupName;

#ifdef CTR_CUSTOM_TRACKS
			// A displaced cup's standings are the standings of one race on a
			// track the retail cup name does not name. Same redirect predicate as
			// the leg counter below and the completion fork above it.
			ctCupName = CustomTrack_CupDisplayName(cupID, 1);
#endif
		}

		// If Arcade or VS cup
		else
		{
			titleString = data.ArcadeCups[cupID].lngIndex_CupName;
		}
	}

	// title text
	DecalFont_DrawLine(
#ifdef CTR_CUSTOM_TRACKS
	    ctCupName != NULL ? (char *)ctCupName : sdata->lngStrings[titleString],
#else
	    sdata->lngStrings[titleString],
#endif
	    drawPos.x, drawPos.y + UI_CUP_STANDINGS_TITLE_Y_OFFSET, FONT_BIG, JUSTIFY_CENTER | ORANGE);

	DecalFont_DrawLine(sdata->lngStrings[LNG_STANDINGS], drawPos.x, drawPos.y, FONT_BIG, JUSTIFY_CENTER | ORANGE);

	// 24 characters, in case of other
	// languages with longer text
	char text[24];

	// TRACK 1/4, 2/4, 3/4, 4/4
#ifdef CTR_CUSTOM_TRACKS
	// A redirected event cup has ONE leg, not four, so the retail "/4" would
	// promise legs the game will never load. Same predicate as the completion
	// fork, so the counter cannot disagree with it.
	sprintf(text, "%s %ld/%d",

	        sdata->lngStrings[LNG_TRACK],

	        // Track Index (0, 1, 2, 3) + 1
	        CTR_PRINTF_PSX_LONG(gGT->cup.trackIndex + 1),

	        CustomTrack_CupLegCount(gGT->cup.cupID, (gGT->gameMode2 & CUP_ANY_KIND) == 0));
#else
	sprintf(text, "%s %ld/4",

	        sdata->lngStrings[LNG_TRACK],

	        // Track Index (0, 1, 2, 3) + 1
	        CTR_PRINTF_PSX_LONG(gGT->cup.trackIndex + 1));
#endif

	DecalFont_DrawLine(text, drawPos.x, drawPos.y + UI_CUP_STANDINGS_TRACK_TEXT_Y_OFFSET, FONT_SMALL, JUSTIFY_CENTER | ORANGE);

	if ((sdata->framesSinceRaceEnded ==
	     (sdata->framesSinceRaceEnded / FPS_DOUBLE(UI_CUP_STANDINGS_ICON_REVEAL_STEP_FRAMES)) * FPS_DOUBLE(UI_CUP_STANDINGS_ICON_REVEAL_STEP_FRAMES)) &&
	    (sdata->numIconsEOR < numDrivers))
	{
		sdata->numIconsEOR++;
	}

	int rectX = gGT->pushBuffer_UI.rect.x;
	int rectW = gGT->pushBuffer_UI.rect.w;

	int *points = &data.cupPointsPerPosition[0];

	vsCupIconOffsetX = 0;
	for (i = 0; i < sdata->numIconsEOR; i++)
	{
		s16 iconTargetY;
		iconColumnOffset = (s16)i;
		// If you are in Purple Gem Cup
#ifdef CTR_CUSTOM_TRACKS
		// The Purple layout lays five icons in one row and collapses everything
		// past the fifth to (0,0), so a displaced cup that grids more than five
		// would stack its extra icons in the corner. The ordinary eight-kart
		// layout below -- two rows of four -- already handles 0..7, so the fork
		// asks how many karts raced rather than which cup it was.
		//
		// The test is on numDrivers, not on the serve predicate, because it is
		// the field size that the layout actually cares about: a vanilla Purple
		// cup always grids five and takes exactly the branch it always took,
		// and a displaced cup clamped down to five does too.
		if (CustomTrackPolicy_StandingsUsesNarrowLayout(gGT->cup.cupID, numDrivers))
#else
		if (gGT->cup.cupID == UI_CUP_STANDINGS_PURPLE_GEM_CUP_ID)
#endif
		{
			if (i < UI_CUP_STANDINGS_PURPLE_GEM_VISIBLE_DRIVERS)
			{
				iconTargetY = UI_CUP_STANDINGS_ICON_PURPLE_GEM_ROW_Y;
				iconTargetX = rectX;
				iconColumnOffset = (s16)((rectW - UI_CUP_STANDINGS_ICON_LEFT_MARGIN) / UI_CUP_STANDINGS_PURPLE_GEM_VISIBLE_DRIVERS) * iconColumnOffset +
				                   UI_CUP_STANDINGS_ICON_RIGHT_MARGIN;
				goto AddIconColumnOffset;
			}
			iconTargetX = 0;
			iconTargetY = 0;
		}

		// If this is not Purple Gem Cup
		else
		{
			// Basically, if you're in 2P Arcade
			if (numDrivers == UI_CUP_STANDINGS_TWO_PLAYER_ARCADE_DRIVER_COUNT)
			{
				iconTargetY = UI_CUP_STANDINGS_ICON_TOP_ROW_Y;

				if (i < 3)
				{
					iconTargetX = rectX;
					iconColumnOffset = UI_CUP_STANDINGS_ICON_SLOT_X * iconColumnOffset + UI_CUP_STANDINGS_ICON_LEFT_MARGIN;
				}

				else
				{
					iconColumnOffset = iconColumnOffset + -2;

				IconBottomRow:
					iconTargetY = UI_CUP_STANDINGS_ICON_BOTTOM_ROW_Y;

					iconTargetX = rectX;
					iconColumnOffset = UI_CUP_STANDINGS_ICON_SLOT_X * iconColumnOffset + UI_CUP_STANDINGS_ICON_SLOT_X;
				}
			}

			// If you are not in 2P Arcade
			else
			{
				// If VS cup of any kind
				if (gGT->numBotsNextGame == 0)
				{
					iconTargetY = UI_CUP_STANDINGS_ICON_VS_ROW_Y;
					iconTargetX =
					    rectX + (s16)((rectW - (numDrivers * UI_CUP_STANDINGS_ICON_SLOT_W) + UI_CUP_STANDINGS_ICON_VS_CENTER_BIAS) / 2) + vsCupIconOffsetX;
					goto HaveIconTargetPos;
				}

				iconTargetY = UI_CUP_STANDINGS_ICON_TOP_ROW_Y;
				if (3 < i)
				{
					iconColumnOffset = iconColumnOffset + -4;
					goto IconBottomRow;
				}

				iconTargetX = rectX;
				iconColumnOffset = UI_CUP_STANDINGS_ICON_SLOT_X * iconColumnOffset + UI_CUP_STANDINGS_ICON_LEFT_MARGIN;
			}

		AddIconColumnOffset:
			iconTargetX = iconTargetX + iconColumnOffset;
		}

	HaveIconTargetPos:

		if (sdata->framesSinceRaceEnded <= FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME))
		{
			iconStartX = UI_CUP_STANDINGS_TITLE_END_X;
			iconEndX = (int)iconTargetX;
			animationFrame = sdata->framesSinceRaceEnded - (i * FPS_DOUBLE(UI_CUP_STANDINGS_ICON_REVEAL_STEP_FRAMES));
		}

		else
		{
			iconStartX = (int)iconTargetX;
			iconEndX = UI_CUP_STANDINGS_TITLE_START_X;
			animationFrame = sdata->framesSinceRaceEnded - FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME);
		}

		// Interpolate fly-in variables over 0x14 frames
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&(drawPos)), iconStartX, iconTargetY, iconEndX, iconTargetY, animationFrame,
		                 FPS_DOUBLE(UI_CUP_STANDINGS_LERP_FRAMES));

		// %d
		sprintf(text, (char *)&sdata->s_int, i + 1);

		DecalFont_DrawLine(text, drawPos.x + UI_CUP_STANDINGS_PLACE_TEXT_X_OFFSET, drawPos.y + UI_CUP_STANDINGS_PLACE_TEXT_Y_OFFSET, FONT_SMALL, RED);

		struct Driver *d;

		// If this is the first screen of cup standings,
		// where you see just amount of points added
		if ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS) == 0)
		{
			// get driver in order of race rank (for one track)
			d = gGT->driversInRaceOrder[i];
		}

		// if this is not the first page,
		// so now you see total points of drivers
		else
		{
			// get driver in order of cup rank (for collection of tracks)
			d = gGT->drivers[data.cupPositionPerPlayer[i]];
		}

		// Draw character icon
		UI_DrawDriverIcon(gGT->ptrIcons[data.MetaDataCharacters[data.characterIDs[d->driverID]].iconID],

		                  drawPos.x, drawPos.y, &gGT->backBuffer->primMem,

		                  gGT->pushBuffer_UI.ptrOT, TRANS_50_DECAL, UI_CUP_STANDINGS_ICON_SCALE,
		                  MakeColorPacked(UI_CUP_STANDINGS_ICON_NEUTRAL_CHANNEL, UI_CUP_STANDINGS_ICON_NEUTRAL_CHANNEL, UI_CUP_STANDINGS_ICON_NEUTRAL_CHANNEL));

		// If this is the first screen of cup standings,
		// where you see just amount of points added
		if ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS) == 0)
		{
			int awardedPoints = 0;

			if (i < UI_CUP_STANDINGS_MAX_VISIBLE_AWARD_DRIVERS)
			{
				if (gGT->numBotsNextGame == 0)
				{
					awardedPoints = gGT->numPlyrCurrGame - (i + 1);
				}
				else
				{
					awardedPoints = points[i];
				}
			}

			*(int *)&text[0] = '+' + (('0' + awardedPoints) << 8);
		}

		// if this is not the first page,
		// so now you see total points of drivers
		else
		{
			// The amount of points that each player has, in a cup
			sprintf(text, &sdata->s_longInt[0], gGT->cup.points[data.cupPositionPerPlayer[i]]);
		}

		vsCupIconOffsetX = vsCupIconOffsetX + UI_CUP_STANDINGS_ICON_SLOT_W;

		// draw string for number of points
		DecalFont_DrawLine(text, drawPos.x + UI_CUP_STANDINGS_POINTS_TEXT_X_OFFSET, drawPos.y + UI_CUP_STANDINGS_POINTS_TEXT_Y_OFFSET, FONT_BIG, PERIWINKLE);
	}

	// If this is the first screen of cup standings,
	// where you see just amount of points added
	if ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS) == 0)
	{
		// fly-in interpolation
		UI_Lerp2D_Linear(CTR_VECTOR_DATA(&(drawPos)), UI_CUP_STANDINGS_PANEL_X, pageEnterY, UI_CUP_STANDINGS_PANEL_X, UI_CUP_STANDINGS_PANEL_Y,
		                 sdata->framesSinceRaceEnded, FPS_DOUBLE(UI_CUP_STANDINGS_LERP_FRAMES));
	}

	// if it's not...
	else
	{
		if (sdata->framesSinceRaceEnded <= FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME))
		{
			drawPos.x = UI_CUP_STANDINGS_PANEL_X;
			drawPos.y = UI_CUP_STANDINGS_PANEL_Y;
		}

		else
		{
			// fly-in interpolation
			UI_Lerp2D_Linear(CTR_VECTOR_DATA(&(drawPos)), UI_CUP_STANDINGS_PANEL_X, UI_CUP_STANDINGS_PANEL_Y, UI_CUP_STANDINGS_PANEL_X, pageExitY,
			                 sdata->framesSinceRaceEnded - FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME), FPS_DOUBLE(UI_CUP_STANDINGS_LERP_FRAMES));
		}
	}

	RECT r;
	r.x = drawPos.x;
	r.y = drawPos.y;
	r.w = UI_CUP_STANDINGS_PANEL_W;
	r.h = UI_CUP_STANDINGS_PANEL_H;

	// Draw 2D Menu rectangle background
	RECTMENU_DrawInnerRect(&r, 4, gGT->backBuffer->otMem.uiOT);

	// Timer
	if (sdata->framesSinceRaceEnded < FPS_DOUBLE(UI_CUP_STANDINGS_DONE_FRAME))
	{
		if ((
		        // Timer
		        (FPS_DOUBLE(UI_CUP_STANDINGS_PRESS_PROMPT_FRAME) < sdata->framesSinceRaceEnded) &&

		        // If you are not in overall Cup standings
		        ((sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_OVERALL_POINTS) == 0)) &&
		    (DecalFont_DrawLine(sdata->lngStrings[LNG_PRESS_TO_CONTINUE], UI_CUP_STANDINGS_PRESS_X, UI_CUP_STANDINGS_PRESS_Y, FONT_BIG,
		                        JUSTIFY_CENTER | ORANGE),

		     // If you press Cross or Circle
		     (sdata->AnyPlayerTap & UI_CUP_STANDINGS_CONFIRM_BUTTONS) != 0))
		{
			// Timer
			sdata->framesSinceRaceEnded = FPS_DOUBLE(UI_CUP_STANDINGS_PAGE_TRANSITION_FRAME);

			// Proceed from Track standings to overall Cup standings,
			// where you see how many points each driver has overall
			sdata->menuReadyToPass = sdata->menuReadyToPass | UI_CUP_STANDINGS_PAGE_OVERALL_POINTS;

			// clear gamepad input (for menus)
			RECTMENU_ClearInput();
		}
	}

	else
	{
		sdata->numIconsEOR = 1;
		sdata->framesSinceRaceEnded = 0;
		nextMenuReadyFlags =
		    sdata->menuReadyToPass & ~(UI_CUP_STANDINGS_MENU_READY_END_OPTIONS | UI_CUP_STANDINGS_PAGE_TRACK_POINTS | UI_CUP_STANDINGS_PAGE_OVERALL_POINTS);
		wasTrackPointsPage = sdata->menuReadyToPass & UI_CUP_STANDINGS_PAGE_TRACK_POINTS;

		// Proceed from end-of-race menu to Track Standings,
		// where you see how many points are added just for this race
		sdata->menuReadyToPass = nextMenuReadyFlags | UI_CUP_STANDINGS_PAGE_TRACK_POINTS;

		// If the "4" flag was not enabled till just now,
		// If this is the first frame of Track Standings
		if (wasTrackPointsPage == 0)
		{
			if (numDrivers > UI_CUP_STANDINGS_MAX_VISIBLE_AWARD_DRIVERS)
			{
				numDrivers = UI_CUP_STANDINGS_MAX_VISIBLE_AWARD_DRIVERS;
			}

			for (i = 0; i < numDrivers; i++)
			{
				// the amount of points this player has in the cup
				gGT->cup.points[gGT->driversInRaceOrder[i]->driverID] +=

				    // Incremented by
				    // The amount of points that should be awarded to each position
				    (gGT->numBotsNextGame == 0) ? (gGT->numPlyrCurrGame - (i + 1)) : points[i];
			}

			// update cup rank of all drivers
			UI_CupStandings_UpdateCupRanks();
		}

		// If this is not the first frame of track standings
		else
		{
			sdata->menuReadyToPass = nextMenuReadyFlags;

			// enable drawing HUD
			gGT->hudFlags |= HUD_FLAG_RACE_HUD;

			// Disable types of HUD that are not needed for gameplay,
			// This includes Cup rankings, which is a flag in this byte
			gGT->hudFlags &= HUD_FLAG_CLEAR_CUP_STANDINGS_MASK;

			sdata->menuReadyToPass &= ~UI_CUP_STANDINGS_PAGE_TRACK_POINTS;

			// Increment Track Number by 1 (0-3 in the cup)
			gGT->cup.trackIndex++;

			int cupTrack = gGT->cup.trackIndex;

			// If this is not the last race in the cup
#ifdef CTR_CUSTOM_TRACKS
			// Baby T Park event destination: a redirected cup ran ONE race, so
			// it is complete at trackIndex 1 and falls through to the gem award
			// below instead of loading a leg 1 that does not exist for it. The
			// predicate is the same one AH_WarpPad.c used to redirect the entry,
			// reading the same config, so the two forks cannot disagree -- a cup
			// that became a single race always completes as one.
			if (!CustomTrack_CupIsComplete(cupID, (gGT->gameMode2 & CUP_ANY_KIND) == 0, cupTrack))
#else
			if (cupTrack < UI_CUP_STANDINGS_TRACKS_PER_CUP)
#endif
			{
				int nextLevelID;

				// If not in Arcade or VS cup
				if ((gGT->gameMode2 & CUP_ANY_KIND) == 0)
				{
#ifdef CTR_AP
					// #166: identity-safe, see AH_WarpPad.c's leg-0 load.
					nextLevelID = ctr_cfg_cup_leg(cupID, cupTrack);
#else
					nextLevelID = data.advCupTrackIDs[(UI_CUP_STANDINGS_TRACKS_PER_CUP * cupID) + cupTrack];
#endif
				}

				// If Arcade or VS cup
				else
				{
					nextLevelID = data.ArcadeCups[cupID].CupTrack[cupTrack].trackID;
				}

#ifdef CTR_AP
				// Schema 15: a leg of 110 loads Cortex Vortex on host level 13;
				// every other leg is selected as retail, so a Cortex Vortex leg
				// followed by an Oxide Station leg serves Oxide Station bytes.
				nextLevelID = AP_CortexTrackPrepareLoad(nextLevelID);
#endif
				MainRaceTrack_RequestLoad(nextLevelID);
			}

			// If this was the last race in the cup
			// If the cup is over
			else
			{
				if ((gGT->gameMode2 & CUP_ANY_KIND) != 0)
				{
					UI_CupStandings_FinalizeCupRanks();
				}

				gGT->cup.trackIndex = 0;

#ifdef CTR_CUSTOM_TRACKS
				// Undo the event destination's 7-lap override the moment its cup
				// is over. Nothing in adventure re-derives numLaps between races,
				// so without this the next trophy/boss/relic race entered from
				// the hub would inherit 7 laps. 3 is the boot default
				// (MainMain.c) and what MM_MenuFlow.c writes on every main-menu
				// row press, so it is the correct resting value. Written here
				// rather than only in the AP frame watcher so a clean build with
				// the guard on is equally correct.
				if (CustomTrack_RaceFeatureEnabled() && gGT->numLaps != 3)
				{
					gGT->numLaps = 3;
				}
#endif

				// Array with the final ranking of each player
				int *ranks = &data.cupPositionPerPlayer[0];
				for (i = 0; i < UI_CUP_STANDINGS_DRIVER_SLOTS; i++)
				{
					struct Driver *d;
					d = gGT->drivers[ranks[i]];

					// If driver is not nullptr
					if (d != 0)
					{
						// Set final ranking of each player
						d->driverRank = (s16)i;
					}

					// set driver's cup score to zero
					gGT->cup.points[i] = 0;
				}

				Podium_InitModels(gGT);

				i = gGT->cup.cupID;

				// If this is an Adventure Cup
				if ((gGT->gameMode2 & CUP_ANY_KIND) == 0)
				{
					// Array with the ranking of each player
					gGT->levelID = i + ADVENTURE_CUP_SYNTHETIC_LEVEL_ID_BASE;

					// when loading is done,
					// remove flag for adventure cup
					sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_CUP;

					// If player 1 won the cup
					if (data.cupPositionPerPlayer[0] == gGT->drivers[0]->driverID
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
					    && (!(ctr_cfg_active() && CustomTrack_CupRaceRedirectActive(i, 1) &&
					           (gGT->gameMode2 & TOKEN_RACE)) ||
					        AP_LetterTokenEarned(ctr_cfg.custom_track.host_level_id, 1,
					                            gGT->drivers[0]->PickupLetterHUD.numCollected))
#endif
					   )
					{
						int bitIndex = ADV_REWARD_FIRST_GEM + i;
						u32 *rewardsSet = sdata->advProgress.rewards;
						int customTrackTrophy = 0;

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
						customTrackTrophy = ctr_cfg_active() &&
							CustomTrack_CupRaceRedirectActive(i, 1);
#endif

#ifdef CTR_AP
						// AP: the vanilla "already won?" gate reads adv->rewards, but
						// AP_ApplyItems mirrors RECEIVED gem items into that same set,
						// so a cup whose gem bit is item-backed would read "already
						// won" and skip the location check + boss characters + gem
						// podium model (issue #35: any received gem used to set the
						// Purple bit first, eating the Purple cup's check). Gate on
						// the AP location CHECKED-state instead (same fix class as
						// the trophy gate in 222.c and ThTick:601 f9fbfa7a0).
						// AP_NotifyAdvReward dedupes, so a re-win is safe.
						if (ctr_cfg_active() ? (customTrackTrophy
							? ((gGT->gameMode2 & TOKEN_RACE)
							       ? !AP_CustomTrackCtrChecked() : !AP_CustomTrackTrophyChecked())
							: !AP_LocationCheckedByBit(bitIndex))
						                     : (CHECK_ADV_BIT(rewardsSet, bitIndex) == 0))
#else
						if (!CHECK_ADV_BIT(rewardsSet, bitIndex))
#endif
						{
							// A custom Trophy borrows this Cup's ceremony machinery, not
							// its progression rewards. Do not fabricate the displaced Gem
							// bit: AP would clear it on the next reconcile, and the player
							// would see a Purple Gem instead of the actual scouted item.
							if (!customTrackTrophy)
								UNLOCK_ADV_BIT(rewardsSet, bitIndex);
#ifdef CTR_AP
							// #286 product ruling (2026-09-20): the cup is won on
							// accumulated points, so its reward goes out in full even
							// when DeathLink forced a loss on the final leg. The
							// cup-aggregate entry points pass the forced-loss latch
							// deliberately; the forced leg's own finish and per-race
							// checks stay blocked. A cup NOT won on points never
							// reaches this block, as in retail.
							if (customTrackTrophy)
							{
								if (gGT->gameMode2 & TOKEN_RACE)
									AP_NotifyCupAggregateCustomTrackCtr(1, gGT->drivers[0]->PickupLetterHUD.numCollected);
								else AP_NotifyCupAggregateCustomTrackTrophy();
							}
							else
								AP_NotifyCupAggregateReward(bitIndex); // AP: retail gem cup location check
#endif

							// The boss character belongs to the displaced retail Cup too.
							// Only a real Gem Cup win may grant it.
							if (!customTrackTrophy)
							{
								// unlock Roo, Papu, Joe, Pinstripe, FCrash
								bitIndex = GAME_UNLOCK_BIT_BOSS_CHARACTER_FIRST + i;
								UNLOCK_ADV_BIT(sdata->gameSave.progress.unlocks, bitIndex);
							}

							// Set podium reward model to Gem
							gGT->podiumRewardID = STATIC_GEM;
						}

						// reset counter for number of times you lost cup, to zero
						sdata->advProgress.timesLostCupRace[gGT->cup.trackIndex] = 0;
					}

					// If player 1 did not win the cup
					else
					{
#ifdef CTR_AP
						// race_loss DeathLink: the whole cup is lost (a single lost
						// leg never sends).
						AP_DeathLinkOnCupLost();
#endif
						if (sdata->advProgress.timesLostCupRace[gGT->cup.trackIndex] < UI_CUP_STANDINGS_MAX_CUP_LOSSES)
						{
							sdata->advProgress.timesLostCupRace[gGT->cup.trackIndex]++;
						}
					}
				}

				// If this is not an Adventure Cup
				else
				{
					// Fix: retail checked driver[1] without asking whether it is a
					// player, so in a 1P Arcade cup the bot in that slot winning
					// gave the player completion credit (and could unlock a battle
					// map and the save prompt).

					// If Player 1 or Player 2 won the cup
					if ((gGT->drivers[0]->driverRank == 0) || (((u8)gGT->numPlyrCurrGame > 1) && (gGT->drivers[1]->driverRank == 0) &&

					                                           // If you're in Arcade Mode
					                                           ((gGT->gameMode1 & ARCADE_MODE) != 0)))
					{
						int difficulty = (gGT->arcadeDifficulty / UI_CUP_STANDINGS_ARCADE_DIFFICULTY_DIVISOR) - 1;
						if (difficulty > UI_CUP_STANDINGS_MAX_ARCADE_DIFFICULTY)
						{
							difficulty = UI_CUP_STANDINGS_MAX_ARCADE_DIFFICULTY;
						}

						u32 *rewardsSet = &sdata->gameSave.progress.unlocks[0];

						int baseIndex = sdata->UnlockBitIndex.CupCompletion_prev[difficulty];

						// if track was not unlocked "previously",
						// this writes when TakeCupProgress is saved
						int bitIndex = baseIndex + gGT->cup.cupID;
						if (!CHECK_ADV_BIT(rewardsSet, bitIndex))
						{
							// lets 233 know to prompt the Save Game box
							gGT->gameMode2 |= CUP_NEW_WIN;

							baseIndex = sdata->UnlockBitIndex.CupCompletion_curr[difficulty];

							bitIndex = baseIndex + gGT->cup.cupID;
							UNLOCK_ADV_BIT(rewardsSet, bitIndex);

							b32 boolUnlockMap = true;
							for (i = 0; i < UI_CUP_STANDINGS_TRACKS_PER_CUP; i++)
							{
								// if any of four cups on this difficulty was not won
								bitIndex = baseIndex + i;
								if (!CHECK_ADV_BIT(rewardsSet, bitIndex))
								{
									// you dont deserve to unlock a battle map
									boolUnlockMap = false;
									break;
								}
							}

							// If new Battle unlocked
							if (boolUnlockMap)
							{
								bitIndex = sdata->UnlockBitIndex.UnlockBattleMap[difficulty];
								UNLOCK_ADV_BIT(rewardsSet, bitIndex);

								// battle map is now unlocked (233 overlay)
								gGT->gameMode2 |= CUP_NEW_BATTLE;
							}
						}
					}

					// Set podium's reward model to "Empty"
					gGT->podiumRewardID = STATIC_BIG1;
				}

				// Level ID for Gemstone Valley (podiums)
#ifdef CTR_AP
				// AP (issue #34): under destination shuffle a gem cup can be
				// entered from any hub, and Gemstone Valley may still be locked --
				// the hardcoded GV load then strands the player outside the map
				// (the same hazard the quit paths fixed with AP_CupReturnHub).
				// Hold the podium in the recorded entry hub instead:
				// LOAD_TenStages keys the podium sequence on podiumRewardID, not
				// the level, so the ceremony machinery is hub-generic (boss keys
				// already podium in every hub). Arcade/VS cups and vanilla keep
				// the retail GV podium (AP_CupReturnHub returns -1 when AP is
				// inactive or no entry hub was recorded).
				{
					int levID = GEM_STONE_VALLEY;
					if ((gGT->gameMode2 & CUP_ANY_KIND) == 0)
					{
						int apCupReturn = AP_CupReturnHub();
						if (apCupReturn >= 0)
							levID = apCupReturn;
					}
					MainRaceTrack_RequestLoad(levID);
				}
#else
				MainRaceTrack_RequestLoad(GEM_STONE_VALLEY);
#endif

			}
		}
	}
}
