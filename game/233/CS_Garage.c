#include <common.h>

enum GarageInputMask
{
	GARAGE_INPUT_HORIZONTAL = BTN_LEFT | BTN_RIGHT,
	GARAGE_INPUT_MENU = BTN_TRIANGLE | BTN_CROSS_one | BTN_CIRCLE | BTN_SQUARE_one,
	GARAGE_INPUT_CONFIRM = BTN_CROSS_one | BTN_CIRCLE,
	GARAGE_INPUT_BACK = BTN_TRIANGLE | BTN_SQUARE_one,
};

enum
{
	GARAGE_STAT_BAR_RATE = 3,
	GARAGE_STAT_BAR_SEGMENT_COUNT = 6,
	GARAGE_STAT_BAR_SEGMENT_WIDTH = 13,
	GARAGE_STAT_BAR_ROW_STEP = 15,
	GARAGE_STAT_BAR_POLY_G4_CODE = 0x38,
	GARAGE_STAT_BAR_POLY_G4_COLOR_CODE = GARAGE_STAT_BAR_POLY_G4_CODE << 24,
	GARAGE_STAT_NAME_SPEED_Y = 30,
	GARAGE_STAT_NAME_ACCEL_Y = 0x2d,
	GARAGE_STAT_NAME_TURN_Y = 60,
	GARAGE_CLASS_NAME_Y = 15,
	GARAGE_STAT_BAR_SHADOW_Y = 34,
	GARAGE_STAT_BAR_HEIGHT = 7,
	GARAGE_STAT_BAR_SHADOW_HEIGHT = 5,
	GARAGE_STAT_BOX_Y = 11,
	GARAGE_STAT_BOX_HEIGHT = 68,
	GARAGE_STAT_BOX_PADDING_X = 6,
	GARAGE_OSK_DELAY_FRAMES = 60,
	GARAGE_CAMERA_FRAME_STEP = 30,
	GARAGE_CAMERA_WRAP_RIGHT_FRAME = 240,
	GARAGE_CAMERA_WRAP_LEFT_FRAME = 210,
	GARAGE_CHARACTER_NAME_X = 0x100,
	GARAGE_CHARACTER_NAME_Y = 0xb4,
	GARAGE_CHARACTER_ARROW_Y = 187,
	GARAGE_CHARACTER_ARROW_LEFT_BASE_X = 236,
	GARAGE_CHARACTER_ARROW_RIGHT_BASE_X = 274,
	GARAGE_CHARACTER_ARROW_ICON_INDEX = 0x38,
	GARAGE_CHARACTER_ARROW_SCALE = 0x1000,
	GARAGE_CHARACTER_ARROW_ROT_LEFT = 0x800,
};

#ifdef CTR_AP
// Per-SESSION latch for the adventure-start garage skip (#54/#209).
//
// File scope rather than a gGarage member, because gGarage mirrors a retail
// overlay-233 data layout that is size-asserted, and rather than a
// function-local static, because CS_Garage_ZoomOut below has to re-arm it and
// that is the whole point: see ap/ap_garageskip.h for the two soft-locks a
// process-lifetime latch caused here.
static struct AP_GarageSkipState apGarageSkip = {0};
#endif

void CS_Garage_ZoomOut(s16 zoomState)
{
	if (zoomState != 0)
	{
		// number of frames to zoom in, or out,
		// when selecting or cancelling OSK
		CS_GARAGE_ZOOM_IN = CS_GARAGE_ZOOM_FRAMES;
		CS_GARAGE_ZOOM_OUT = CS_GARAGE_ZOOM_FRAMES;
	}
	else
	{
		CS_GARAGE_ZOOM_IN = 0;
		CS_GARAGE_ZOOM_OUT = 0;
	}

	CS_GARAGE_MOVE_FRAME = 0;
	CS_GARAGE_SELECTED = 0;
	CS_GARAGE_DELAY = 0;

	GAME_TRACKER->gameMode2 &= ~(GARAGE_OSK);

#ifdef CTR_AP
	// A garage SESSION begins here, so re-arm the adventure-start skip (#54/#209).
	//
	// CS_Garage_Init below always calls this (zoomState 0, a fresh garage level
	// load). The name-entry CANCEL branch calls it (zoomState 1) only when the
	// AP handler declines the exit and the retail return to this menu executes
	// (game/SubmitName.c, Adventure Cancel). An AP-skipped name-entry Cancel
	// never reaches this function; it leaves for the main-menu level instead, see
	// CS_Garage_APNameEntryCancelToMainMenu.
	//
	// Re-arming here is what stops the skip from soft-locking the game. Its latch
	// used to live for the whole process, so on any second visit to the garage the
	// skip returned before re-issuing its OSK handoff, and menuGarage's state
	// (DISABLE_INPUT_ALLOW_FUNCPTRS | EXECUTE_FUNCPTR) means its funcPtr is the
	// only thing driving that screen: an early return left nothing at all. Both a
	// cancelled name entry and a second new adventure in one process reached it.
	AP_GarageSkip_NewSession(&apGarageSkip);
#endif

	// if just entered garage
	if (zoomState == 0)
	{
		Garage_Init();
		Garage_Enter((s16)sdata->advCharSelectIndex_curr);

		Audio_SetState_Safe(AUDIO_GARAGE);
	}
}

#ifdef CTR_AP
// Force the AP-authoritative racer over whatever a Garage confirm just wrote.
//
// The two commit sites in CS_Garage_MenuProc are the only places in the tree
// that can seat a racer of the player's choosing at adventure start. When the
// seed owns the racer they are unreachable, because CS_Garage_MenuProc returns
// early -- but writing the invariant at the point of the write, rather than
// trusting the arrangement that keeps us away from it, is what stops a future
// refactor of the skip from silently reopening the unlock bypass.
static void CS_Garage_ApplyApRacerOverride(void)
{
	int apRacer = AP_CharSwap_GarageRacer();

	if (apRacer < 0)
	{
		// Retail pick: either no character phase (a no-op) or every starter is
		// unlocked and the choice is the player's, so store it.
		AP_CharSwap_GarageChoice(GAME_CHARACTER_IDS[0]);
		return;
	}
	if (GAME_CHARACTER_IDS[0] == (s16)apRacer)
		return;

	AP_LogLine("[AP CHARSWAP] garage commit overridden; the seed owns the racer\n");

	GAME_CHARACTER_IDS[0] = (s16)apRacer;
	sdata->advProgress.characterID = (s16)apRacer;
}

// AP-owned Adventure name-entry Cancel: leave the garage level entirely.
//
// Called first from the Adventure CANCEL branch of SubmitName_MenuProc (H6-02).
// When this name-entry session was reached through the AP garage skip and AP
// still owns the racer, there is no real character choice to revisit, so this
// runs the same state, cleanup and load sequence the retail garage's Back uses
// to reach the title (CS_Garage_MenuProc's "return to main menu" branch,
// MAIN_MENU_TITLE + Garage_Leave + MAIN_MENU_LEVEL). When the predicate fails
// it returns 0 and the caller keeps its unchanged retail fallback, which points
// ptrDesiredMenu back at this garage and calls CS_Garage_ZoomOut(1).
//
// Deliberately an engine helper, not a pure helper: it performs the
// garage-owned exit. It does not point at the main menu while overlay 233 is
// active, and it deliberately does not call CS_Garage_ZoomOut, so an AP-owned
// Cancel cannot re-arm the skip and reopen name entry.
int CS_Garage_APNameEntryCancelToMainMenu(void)
{
	int apRacer = AP_CharSwap_GarageRacer();

	if (!AP_GarageSkip_ShouldExitToMainMenu(&apGarageSkip, apRacer))
		return 0;

	// Exact transition used when Back leaves the retail garage.
	sdata->mainMenuState = MAIN_MENU_TITLE;
	Garage_Leave();
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
	return 1;
}
#endif

void CS_Garage_MenuProc(struct RectMenu *menu)
{
	s32 garageFrames;
	s32 i;
	s16 *statBarLength;
	u16 classNamePosX;
	u32 statNamePosX;
	u32 statBarPosX;

	struct PushBuffer *pushBuffer;

	RECT r;
#ifndef CTR_AP
	RECT barRect;
	union
	{
		RECT rect;
		Color color;
	} scratch;
#endif
	s32 engineID;
	s16 classStringIndex;

#ifndef CTR_AP
	s32 rowIndex;
	s32 statBarShadows_Y;
#endif
	s16 classMaxLen;

	s32 arrowColor;

	s32 nameLen;

	SVec3 camPos;
	SVec3 camRot;
	s16 pathFlags;
	s32 zoom;
	(void)menu;

	GAME_TRACKER->cameraDC[0].cameraMode = CAMERA_MODE_FREECAM;

#ifdef CTR_AP
	// ------------------------------------------------------------------
	// The adventure-start character select is SKIPPED when the seed owns the
	// racer (#54/#209).
	//
	// THE BYPASS THIS CLOSES. This function is the whole of the vanilla garage
	// picker (the menuGarage funcPtr, run per frame from RECTMENU), and it
	// commits the highlighted racer to GAME_CHARACTER_IDS[0] plus
	// sdata->advProgress.characterID at its two confirm sites. Those are
	// precisely the two fields AP seats the seed's racer into, and the garage
	// writes them afterwards, so the player's pick simply won. Observed live on
	// a seed whose starting racer was Neo Cortex with character unlocks on:
	// picking Dingodile made you Dingodile. GAME_CHARACTER_IDS[0] is then the
	// authority for both the adventure MPK fetch (LOAD_Assets.c) and the driver
	// birth (VehBirth.c), so the wrong racer really is the one that races.
	//
	// WHY SKIP RATHER THAN FILTER. The garage's roster is CS_GARAGE_CHARACTERS,
	// the eight vanilla starters. It has no row for Penta, Oxide or any
	// unlockable, so a seed that starts you as one of them cannot be expressed
	// here at all. Constraining the picker to a single legal tile would also
	// present a choice that is not one.
	//
	// HOW THE SKIP WORKS. It performs exactly what the double-tap confirm branch
	// performs -- the same two writes, the same SubmitName_RestoreName(0), the
	// same handoff to the OSK -- with the racer AP resolved instead of the
	// highlighted one, and returns before any input or drawing. Everything
	// downstream (name entry, profile slot, hub load) is the untouched retail
	// path. This runs only with LOAD_IDLE, because MainFrame_RenderFrame gates
	// RECTMENU_ProcessState on it, so the handoff can never fire mid-load.
	//
	// It sits AFTER the freecam camera-mode write above deliberately. That line
	// runs on every frame of the retail garage, and the on-screen keyboard this
	// hands off to draws over the garage scene, so skipping it would hand the OSK
	// a camera state no retail path ever gives it.
	//
	// The garage may briefly have loaded the previous racer's model, since the
	// MPK was queued from GAME_CHARACTER_IDS[0] before we ran. That is cosmetic
	// and lasts a frame: the hub's own load re-queues from the value written
	// here.
	{
		int apRacer = AP_CharSwap_GarageRacer();

		if (AP_GarageSkip_Owns(apRacer))
		{
			// Commit once per garage SESSION, not once per process. The latch is
			// re-armed from CS_Garage_ZoomOut, which means "a garage session
			// begins": a fresh garage load via CS_Garage_Init always re-arms. A
			// cancelled name entry no longer returns here at all when AP owns the
			// racer: it exits to the main-menu level instead (see
			// CS_Garage_APNameEntryCancelToMainMenu), and a later new Adventure is
			// re-armed by CS_Garage_Init on the fresh load. The retail fallback
			// return still re-arms. ap/ap_garageskip.h records the two soft-locks
			// a process-lifetime latch produced here.
			//
			// Re-running the commit is safe: both writes set the same values, and
			// SubmitName_RestoreName(0) only re-seeds the OSK from
			// prevNameEntered. The sound and the log line are the only things
			// worth not repeating every frame, which is the whole job of the
			// latch.
			if (AP_GarageSkip_ShouldCommit(&apGarageSkip, apRacer))
			{
				CS_DESIRED_MENU = &data.menuSubmitName;

				GAME_CHARACTER_IDS[0] = (s16)apRacer;
				sdata->advProgress.characterID = (s16)apRacer;

				SubmitName_RestoreName(0);
				OtherFX_Play(1, 1);

				AP_LogLine("[AP CHARSWAP] adventure-start garage select skipped; the seed owns the racer\n");
			}
			return;
		}

		// The seed does not own the racer. Re-arm (so a later connect commits)
		// and fall through to the retail garage below.
		AP_GarageSkip_ShouldCommit(&apGarageSkip, apRacer);
	}
#endif

	// Draw and accept input only while the selection camera is stationary.
	if (CS_GARAGE_MOVE_FRAME == 0)
	{
		// Retain the unused garage-frame counter.
		CS_GARAGE_UNUSED_FRAMES++;

		// Animate each bar toward the selected engine class.
		{
			s16 *selectedID;
			const void *selectedBase = CS_GARAGE_CHARACTERS;
			selectedID = (s16 *)((const u8 *)selectedBase + CS_GARAGE_CURRENT * 2);
			for (i = 0; i < 3; i++)
			{
				const void *targetBase = CS_GARAGE_STAT_TARGETS;

				statBarLength = &CS_GARAGE_STAT_LENGTHS[i];

#ifdef CTR_AP
				// Replacement design: when Progressive Stats is live, show the viewed
				// racer's current AP rank outright. In shared_global mode the explicit
				// character is intentionally ignored; in per_character mode it selects
				// the PR #216 received-count row. An inactive or unknown configuration
				// returns -1 and preserves the retail class-table bar byte-for-byte.
				// The APPLIED rank: a rank the player lowered in the hub picker is
				// what this racer drives, so it is what the bar shows.
				{
					int rank = AP_CapabilityAppliedRankForCharacter(AP_CAP_CHAIN_TOP_SPEED + i, *selectedID);
					if (rank >= 0)
					{
						s16 target = (s16)AP_StatBar_LenForRank(rank);
						if (*statBarLength < target)
						{
							*statBarLength = *statBarLength + GARAGE_STAT_BAR_RATE;
						}
						if (*statBarLength > target)
						{
							*statBarLength = target;
						}
						continue;
					}
				}
#endif

				if (*statBarLength < *(s16 *)((GAME_CHARACTER_METADATA[*selectedID].engineID * 6 + i * 2) + (u8 *)targetBase))
				{
					*statBarLength = *statBarLength + GARAGE_STAT_BAR_RATE;
				}
				if (*statBarLength > *(s16 *)((GAME_CHARACTER_METADATA[*selectedID].engineID * 6 + i * 2) + (u8 *)targetBase))
				{
					*statBarLength = *(s16 *)((GAME_CHARACTER_METADATA[*selectedID].engineID * 6 + i * 2) + (u8 *)targetBase);
				}
			}
		}

		if ((GAME_CHARACTER_METADATA[CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT]].name_LNG_long != LNG_TINY_TIGER) &&

		    (statNamePosX = 383,

		     GAME_CHARACTER_METADATA[CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT]].name_LNG_long != LNG_PURA_FULL))
		{
			classNamePosX = 384;
			statBarPosX = 393;
		}
		else
		{
			statNamePosX = 129;
			classNamePosX = 128;
			statBarPosX = 139;
		}

		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_SPEED], statNamePosX, GARAGE_STAT_NAME_SPEED_Y, FONT_BIG, JUSTIFY_RIGHT | ORANGE_RED);
		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_ACCEL], statNamePosX, GARAGE_STAT_NAME_ACCEL_Y, FONT_BIG, JUSTIFY_RIGHT | LIME_GREEN);
		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_TURN], statNamePosX, GARAGE_STAT_NAME_TURN_Y, FONT_BIG, JUSTIFY_RIGHT | BLUE);

		engineID = GAME_CHARACTER_METADATA[CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT]].engineID;
		if (engineID == 3)
		{
			classStringIndex = 0;
		}
		else if ((u32)engineID < 2)
		{
			classStringIndex = 1;
		}
		else
		{
			classStringIndex = 2;
		}

		// Draw class name
		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[CS_GARAGE_CLASS_STRINGS[classStringIndex]], classNamePosX, GARAGE_CLASS_NAME_Y, FONT_BIG,
		                   (JUSTIFY_CENTER | ORANGE));

#ifdef CTR_AP
		// The bars are drawn through the shared renderer (ap/ap_statbar.h), which is
		// this very block with the stat lengths and the locals turned into
		// arguments. The hub character picker calls the same function, so the two
		// AP surfaces cannot drift apart -- the whole point of the #220 ruling that
		// the picker look like the Garage.
		//
		// The Garage still passes its OWN colour array, so nothing about its
		// appearance depends on the AP copy the hub uses. The retail block below is
		// preserved for the vanilla build, which is therefore byte-identical.
		for (i = 0; i < 3; i++)
		{
			AP_StatBar_Draw(statBarPosX, GARAGE_STAT_BAR_SHADOW_Y - 1 + i * AP_STATBAR_ROW_PITCH, CS_GARAGE_STAT_LENGTHS[i], CS_GARAGE_STAT_COLORS,
			                GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);
		}
#else
		// NOTE(aalhendi): Keep the drawing row separate from the loop count so
		// GCC advances the row coordinates before the length pointer, as in retail.
		i = 0;
		rowIndex = 0;
		for (statBarLength = CS_GARAGE_STAT_LENGTHS; i < 3; rowIndex++, statBarLength++, i++)
		{
			s32 segmentStart;
			s32 segmentEnd;

			statBarShadows_Y = GARAGE_STAT_BAR_SHADOW_Y + GARAGE_STAT_BAR_ROW_STEP * rowIndex;
			scratch.rect.x = statBarPosX;
			scratch.rect.y = statBarShadows_Y - 1;
			scratch.rect.w = *statBarLength;
			scratch.rect.h = GARAGE_STAT_BAR_HEIGHT;

			// Copy the white template before reusing the scratch color for shadows.
			barRect = scratch.rect;
			scratch.color = csGarageWhite;
			CTR_Box_DrawWireBox(&barRect, &scratch.color, GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);

			// bar shadows
			barRect.x = statBarPosX + 1;
			barRect.y = statBarShadows_Y;
			barRect.w = *statBarLength - 2;
			barRect.h = GARAGE_STAT_BAR_SHADOW_HEIGHT;

			// The shadow has the same code byte and black RGB.
			scratch.color.r = 0;
			scratch.color.g = 0;
			scratch.color.b = 0;
			CTR_Box_DrawWireBox(&barRect, &scratch.color, GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);

			{
				s16 *length;
				s32 top, bottom;
				s32 segmentIndex = 0;
				length = statBarLength;

				for (; segmentIndex < GARAGE_STAT_BAR_SEGMENT_COUNT; segmentIndex++)
				{
					// Each segment blends adjacent colors in the seven-color ramp.
					u32 *barColor = &CS_GARAGE_STAT_COLORS[segmentIndex];
					s32 segmentWidth = GARAGE_STAT_BAR_SEGMENT_WIDTH;
					s16 currSegmentLen;
					s32 primitiveColorCode = GARAGE_STAT_BAR_POLY_G4_COLOR_CODE;
					top = statBarShadows_Y - 1;
					bottom = statBarShadows_Y + 6;
					segmentEnd = segmentWidth + segmentWidth * segmentIndex;
					segmentStart = segmentEnd - GARAGE_STAT_BAR_SEGMENT_WIDTH;
					if (*length <= segmentEnd)
					{
						currSegmentLen = *length - segmentStart;
					}
					else
					{
						currSegmentLen = GARAGE_STAT_BAR_SEGMENT_WIDTH;
					}
					if ((s32)((u32)currSegmentLen << 16) < 0)
					{
						currSegmentLen = 0;
					}
					if (segmentStart + currSegmentLen <= *length)
					{
						// The guard reserves space for one complete primitive.
						struct DB *buffer = GAME_TRACKER->backBuffer;
						POLY_G4 *p = NULL;
						if (buffer->primMem.cursor <= buffer->primMem.guardEnd)
						{
							p = buffer->primMem.cursor;
							buffer->primMem.cursor = p + 1;
						}
						if (!p)
						{
							return;
						}
						// color data
						CtrGpu_WriteColorCode(&p->r0, barColor[0] | primitiveColorCode);
						CtrGpu_WriteColorCode(&p->r1, CS_GARAGE_STAT_COLORS[segmentIndex + 1] | primitiveColorCode);
						CtrGpu_WriteColorCode(&p->r2, barColor[0] | primitiveColorCode);
						CtrGpu_WriteColorCode(&p->r3, CS_GARAGE_STAT_COLORS[segmentIndex + 1] | primitiveColorCode);

						// NOTE(aalhendi): Narrow the signed offset before packing X; this keeps
						// GCC from creating a second, absolute-X induction counter.
						p->x0 = statBarPosX - (s16)-segmentStart;
						p->x2 = p->x0;
						p->x1 = p->x0 + currSegmentLen;
						p->y0 = top;
						p->y1 = top;
						p->y2 = bottom;
						p->y3 = bottom;
						p->x3 = p->x2 + currSegmentLen;
						{
							u32 *ot = GAME_TRACKER->pushBuffer_UI.ptrOT;
							u32 link = CtrGpu_PrimToOTLink24(p);
							p->tag = *ot | 0x08000000;
							*ot = link;
						}
					}
				}
			}
		}

#endif

		classMaxLen = DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[LNG_INTERMEDIATE], FONT_BIG);

		// Stats box
		r.x = (classNamePosX - (classMaxLen >> 1)) - GARAGE_STAT_BOX_PADDING_X;
		r.y = GARAGE_STAT_BOX_Y;
		r.w = DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[LNG_INTERMEDIATE], FONT_BIG) + GARAGE_STAT_BOX_PADDING_X * 2;
		r.h = GARAGE_STAT_BOX_HEIGHT;

		// Draw 2D Menu rectangle background
		RECTMENU_DrawInnerRect(&r, 4, GAME_TRACKER->backBuffer->otMem.uiOT);

		DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[GAME_CHARACTER_METADATA[CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT]].name_LNG_long], GARAGE_CHARACTER_NAME_X,
		                   GARAGE_CHARACTER_NAME_Y, FONT_BIG, JUSTIFY_CENTER | ORANGE);
		arrowColor = ORANGE;
		if ((FPS_HALF(CS_FRAME_COUNTER_LOW) & 4) == 0)
		{
			arrowColor = RED;
		}
		nameLen =
		    (s16)DecalFont_GetLineWidth(GAME_LANGUAGE_STRINGS[GAME_CHARACTER_METADATA[CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT]].name_LNG_long], FONT_BIG) / 2;
		CS_DECALHUD_ARROW_2D((ICONGROUP_GETICONS(GAME_TRACKER->iconGroup[4]))[GARAGE_CHARACTER_ARROW_ICON_INDEX], GARAGE_CHARACTER_ARROW_LEFT_BASE_X - nameLen,
		                     GARAGE_CHARACTER_ARROW_Y, &GAME_TRACKER->backBuffer->primMem, GAME_TRACKER->pushBuffer_UI.ptrOT,
		                     ((Color *)CS_COLOR_POINTERS[arrowColor])[0], ((Color *)CS_COLOR_POINTERS[arrowColor])[1],
		                     ((Color *)CS_COLOR_POINTERS[arrowColor])[2], ((Color *)CS_COLOR_POINTERS[arrowColor])[3], 0, GARAGE_CHARACTER_ARROW_SCALE,
		                     GARAGE_CHARACTER_ARROW_ROT_LEFT);
		CS_DECALHUD_ARROW_2D((ICONGROUP_GETICONS(GAME_TRACKER->iconGroup[4]))[GARAGE_CHARACTER_ARROW_ICON_INDEX], GARAGE_CHARACTER_ARROW_RIGHT_BASE_X + nameLen,
		                     GARAGE_CHARACTER_ARROW_Y, &GAME_TRACKER->backBuffer->primMem, GAME_TRACKER->pushBuffer_UI.ptrOT,
		                     ((Color *)CS_COLOR_POINTERS[arrowColor])[0], ((Color *)CS_COLOR_POINTERS[arrowColor])[1],
		                     ((Color *)CS_COLOR_POINTERS[arrowColor])[2], ((Color *)CS_COLOR_POINTERS[arrowColor])[3], 0, GARAGE_CHARACTER_ARROW_SCALE, 0);

		if (((GAME_TRACKER->renderFlags & RENDER_FLAG_CHECKERED_FLAG) != 0) ||
		    (((GAME_ANY_PLAYER_TAP & GARAGE_INPUT_MENU) == 0) && ((CS_BUTTONS_HOLD & GARAGE_INPUT_HORIZONTAL) == 0)))
		{
			goto update_garage_camera;
		}

		if ((CS_BUTTONS_HOLD & GARAGE_INPUT_HORIZONTAL) != 0)
		{
			// erase animated bars

			for (i = 2; i > -1; i--)
			{
				statBarLength = &CS_GARAGE_STAT_LENGTHS[i];
				*statBarLength = 0;
			}

			// Play Sound
			OtherFX_Play(0, 1);

			if (CS_BUTTONS_HOLD & BTN_LEFT)
			{
				CS_GARAGE_PREVIOUS = CS_GARAGE_CURRENT;
				CS_GARAGE_CURRENT = (CS_GARAGE_CURRENT - 1) & 7;
				Garage_MoveLR(CS_GARAGE_CURRENT);
			}
			else if (CS_BUTTONS_HOLD & BTN_RIGHT)
			{
				CS_GARAGE_PREVIOUS = CS_GARAGE_CURRENT;
				CS_GARAGE_CURRENT = (CS_GARAGE_CURRENT + 1) & 7;
				Garage_MoveLR(CS_GARAGE_CURRENT);
			}

			// reset frame counter to max number of frames
			CS_GARAGE_MOVE_FRAME = CS_GARAGE_MOVE_FRAMES;

			if (CS_GARAGE_ZOOM_IN < CS_GARAGE_ZOOM_FRAMES)
			{
				CS_GARAGE_ZOOM_OUT = CS_GARAGE_ZOOM_FRAMES - CS_GARAGE_ZOOM_IN;
			}

			CS_GARAGE_SELECTED = 0;
			GAME_TRACKER->gameMode2 &= ~GARAGE_OSK;
		}
		else
		{
			if ((GAME_ANY_PLAYER_TAP & GARAGE_INPUT_CONFIRM) != 0)
			{
				// First confirm selects the character; a second confirm skips the wait.
				switch (CS_GARAGE_SELECTED)
				{
				case 0:
				{
					CS_GARAGE_SELECTED = 1;
					break;
				}
				case 1:
				{
					// Confirm again to open the name-entry menu.
					CS_DESIRED_MENU = &data.menuSubmitName;

					{
						s16 character = CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT];
						sdata->advProgress.characterID = character;
						GAME_CHARACTER_IDS[0] = character;
					}

#ifdef CTR_AP
					// Belt and braces on the unlock bypass (#54/#209). The skip
					// at the top of this function returns before we can get
					// here whenever the seed owns the racer, so this cannot
					// currently fire -- but this is one of only two lines in
					// the tree that can seat a racer the seed did not choose,
					// and the invariant is worth stating where it is enforced
					// rather than only where it is arranged.
					CS_Garage_ApplyApRacerOverride();
#endif

					SubmitName_RestoreName(0);
					OtherFX_Play(1, 1);
					break;
				}
				}
			}
			else
			{
				// If you press Triangle or Square
				if ((GAME_ANY_PLAYER_TAP & GARAGE_INPUT_BACK) != 0)
				{
					// Play Sound
					OtherFX_Play(2, 1);

					if (CS_GARAGE_SELECTED == 1)
					{
						CS_GARAGE_SELECTED = 0;
						GAME_TRACKER->gameMode2 &= ~GARAGE_OSK;

						if (CS_GARAGE_ZOOM_IN < CS_GARAGE_ZOOM_FRAMES)
						{
							CS_GARAGE_ZOOM_OUT = CS_GARAGE_ZOOM_FRAMES - CS_GARAGE_ZOOM_IN;
						}
					}
					else
					{
						// return to main menu
						CS_MAIN_MENU_STATE = MAIN_MENU_TITLE;

						Garage_Leave();

						// load main menu LEV
						MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
					}
				}
			}
		}
		// clear gamepad input (for menus)
		RECTMENU_ClearInput();
	}
	else
	{
		CS_GARAGE_MOVE_FRAME--;
	}
update_garage_camera:

	// Advance the selection zoom and delayed name-entry transition.
	if (0 < CS_GARAGE_ZOOM_IN)
	{
		// decrease zoom frame timer
		CS_GARAGE_ZOOM_IN--;
	}

	// if pressed X once, and waited for countdown clock
	if ((CS_GARAGE_SELECTED == 1) && (CS_GARAGE_ZOOM_IN == 0))
	{
		if (
		    // frames remaining for animation
		    (GARAGE_OSK_DELAY_FRAMES <= CS_GARAGE_DELAY) || ((GAME_TRACKER->gameMode2 & GARAGE_OSK) != 0))
		{
			// set desiredMenu to OSK (on-screen keyboard)
			CS_DESIRED_MENU = &data.menuSubmitName;

			{
				s16 character = CS_GARAGE_CHARACTERS[CS_GARAGE_CURRENT];
				sdata->advProgress.characterID = character;
				GAME_CHARACTER_IDS[0] = character;
			}

#ifdef CTR_AP
			// The second of the two commit sites; same reasoning as the first.
			CS_Garage_ApplyApRacerOverride();
#endif

			SubmitName_RestoreName(0);
			OtherFX_Play(1, 1);
		}
		else
		{
			CS_GARAGE_DELAY++;
		}
	}

#ifdef CTR_NATIVE
	if (CS_DESIRED_MENU == &data.menuSubmitName)
	{
		// NOTE(aalhendi): Flush a pending native Enter press so it cannot
		// submit the name immediately when the keyboard opens.
		NikoGetEnterKey();
	}
#endif

	if (CS_GARAGE_SELECTED == 0)
	{
		CS_GARAGE_ZOOM_IN = CS_GARAGE_ZOOM_FRAMES;
	}

	if (CS_GARAGE_ZOOM_OUT != 0)
	{
		CS_GARAGE_ZOOM_OUT--;
	}

	pushBuffer = &GAME_TRACKER->pushBuffer[0];

	// Pura->Crash
	if ((CS_GARAGE_CURRENT == 0) && (CS_GARAGE_PREVIOUS == 7))
	{
		garageFrames = GARAGE_CAMERA_WRAP_RIGHT_FRAME - CS_GARAGE_MOVE_FRAME;
	}
	// Crash->Pura
	else if ((CS_GARAGE_CURRENT == 7) && (CS_GARAGE_PREVIOUS == 0))
	{
		garageFrames = CS_GARAGE_MOVE_FRAME + GARAGE_CAMERA_WRAP_LEFT_FRAME;
	}
	// Move Right
	else if (CS_GARAGE_CURRENT > CS_GARAGE_PREVIOUS)
	{
		garageFrames = CS_GARAGE_CURRENT * GARAGE_CAMERA_FRAME_STEP - CS_GARAGE_MOVE_FRAME;
	}
	// Move Left
	else
	{
		garageFrames = CS_GARAGE_CURRENT * GARAGE_CAMERA_FRAME_STEP + CS_GARAGE_MOVE_FRAME;
	}

	// animation frame index,
	// pointer to position,
	// pointer to rotation

	CAM_Path_Move((s16)garageFrames, CTR_VECTOR_DATA(&(camPos)), CTR_VECTOR_DATA(&(camRot)), &pathFlags);

	// set position and rotation to pushBuffer
	pushBuffer->pos.x = camPos.x;
	pushBuffer->pos.y = camPos.y;
	pushBuffer->pos.z = camPos.z;

	pushBuffer->rot.x = camRot.x;
	pushBuffer->rot.y = camRot.y;
	pushBuffer->rot.z = camRot.z;

	if (CS_GARAGE_ZOOM_OUT != 0)
	{
		zoom = CS_GARAGE_FOV_MIN + CS_GARAGE_ZOOM_OUT * (CS_GARAGE_FOV_MAX - CS_GARAGE_FOV_MIN) / CS_GARAGE_ZOOM_FRAMES;
	}
	else
	{
		zoom = CS_GARAGE_FOV_MIN + (CS_GARAGE_ZOOM_FRAMES - CS_GARAGE_ZOOM_IN) * (CS_GARAGE_FOV_MAX - CS_GARAGE_FOV_MIN) / CS_GARAGE_ZOOM_FRAMES;
	}
	pushBuffer->distanceToScreen_CURR = zoom;
	pushBuffer->distanceToScreen_PREV = zoom;
}

struct RectMenu *CS_Garage_GetMenuPtr(void)
{
	return &gGarage.menuGarage;
}

void CS_Garage_Init(void)
{
	struct RectMenu *menu = &gGarage.menuGarage;

	// go to 3D character selection
	sdata->ptrActiveMenu = menu;

	menu->state &= ~ONLY_DRAW_TITLE;

	// 0 = just entered garage
	CS_Garage_ZoomOut(0);
}

const Color csGarageWhite CTR_PSX_MATCH_SECTION(".CS_garage_white") = {.r = 255, .g = 255, .b = 255};
