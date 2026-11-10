#include <common.h>

void MM_Scrapbook_Init(void)
{
	D230.scrapbookState = SCRAP_INIT;

	// change checkered flag
	RaceFlag_SetDrawOrder(1);

	// clear gamepad input (for menus)
	RECTMENU_ClearInput();
}

#ifdef CTR_NATIVE
#include <platform.h>
#include <platform/native_audio.h>
#include <platform/native_renderer.h>
#include <platform/native_str.h>

#define SCRAPBOOK_NATIVE_XA_CHANNEL 1

global_variable s32 s_scrapbookNativeNextVBlank;
global_variable u32 s_scrapbookNativeSourceFrame;
#endif

void MM_Scrapbook_PlayMovie(struct RectMenu *menu)
{
	// NOTE(aalhendi): Native playback does not perform retail CD file lookup.
#ifndef CTR_NATIVE
	CdlFILE cdlFile;
	s32 cdPos;
#endif
	// book state (0,1,2,3,4)
	switch (D230.scrapbookState)
	{
	// Init State,
	// alter checkered flag
	case SCRAP_INIT:
	{
		s32 stateValue;

		stateValue = SCRAP_LOAD;
		if (RaceFlag_IsFullyOnScreen() == stateValue)
		{
			// checkered flag, begin transition off-screen
			RaceFlag_BeginTransition(2);
		}

		// go to Load State
		D230.scrapbookState = stateValue;
		menu->state &= ~NEEDS_TO_CLOSE;
		Audio_SetState_Safe(1);
		break;
	}

	// find the TEST.STR file
	case SCRAP_LOAD:
	{
		s32 fullyOffscreen;

		// if not fully off screen
		fullyOffscreen = RaceFlag_IsFullyOffScreen();
		if (fullyOffscreen != 1)
		{
#ifdef CTR_NATIVE
			// Level finalization may bring the flag back after SCRAP_INIT ran.
			if (RaceFlag_IsFullyOnScreen()) RaceFlag_BeginTransition(2);
#endif
			// quit, dont start video yet
			return;
		}

		CDSYS_SetMode_StreamData();

#ifdef CTR_NATIVE
		if (NativeSTR_StartScrapbook() != 0)
		{
			if (!NativeAudio_BeginInterleavedXA(SCRAPBOOK_NATIVE_XA_CHANNEL, sdata_static.vol_Music << 7, sdata_static.vol_Music << 7))
			{
				NativeSTR_Stop();
				goto GO_BACK;
			}
			s_scrapbookNativeNextVBlank = Platform_GetVBlankCount() + SCRAPBOOK_FRAME_VBLANKS;
			s_scrapbookNativeSourceFrame = 0;
			D230.scrapbookState = SCRAP_PLAY;
			return;
		}
#else
		// \TEST.STR;1
		// if file was found
		if (CdSearchFile(&cdlFile, R230.s_teststr1) != 0)
		{
			SpuSetCommonCDVolume(sdata_static.vol_Music << 7, sdata_static.vol_Music << 7);

			// Alloc memory to store Scrapbook
			MM_Video_AllocMem(SCRAPBOOK_VIDEO_WIDTH, SCRAPBOOK_VIDEO_HEIGHT, MM_VIDEO_FLAG_HAS_XA_AUDIO | MM_VIDEO_FLAG_SCRAPBOOK,
			                  MM_VIDEO_DEFAULT_RING_SECTORS, 1);

			cdPos = CdPosToInt(&cdlFile.pos);

			// CD position of video, and stream frame count
			MM_Video_StartStream(cdPos, SCRAPBOOK_STREAM_FRAMES);

			// start playing movie
			D230.scrapbookState = SCRAP_PLAY;

			return;
		}
#endif

		goto GO_BACK;
	}

	// Actually play the movie
	case SCRAP_PLAY:
	{
		s32 stateValue;
		struct GameTracker *gameTracker;
		u32 gameTrackerPage;
		s32 nativeUploaded;
		int getButtonPress;

#ifdef CTR_NATIVE
		(void)stateValue;
		(void)gameTracker;
		(void)gameTrackerPage;
#endif
#ifndef CTR_NATIVE
		CTR_PSX_LOAD_SYMBOL_PAGE(gameTrackerPage, RETAIL_GAME_TRACKER_ASM_NAME);
		stateValue = 1;
		// infinite loop (cause this is scrapbook),
		// keep doing DecodeFrame and VSync until done
		for (;;)
		{
			gameTracker = CTR_PSX_PAGE_LVALUE(struct GameTracker *, gameTrackerPage, MM_GAME_TRACKER_PAGE_OFFSET, GAME_TRACKER);
			if (MM_Video_DecodeFrame(gameTracker->db[stateValue - gameTracker->swapchainIndex].drawEnv.ofs[0],
			                         gameTracker->db[stateValue - gameTracker->swapchainIndex].drawEnv.ofs[1] + 4) != 0)
			{
				break;
			}
			VSync(0);
		}

		if ((MM_Video_CheckIfFinished(0) == stateValue) || ((MM_GAME_BUTTON_TAPS[0] & SCRAPBOOK_SKIP_INPUT) != 0))
#else
		getButtonPress = (MM_GAME_BUTTON_TAPS[0] & SCRAPBOOK_SKIP_INPUT);
		nativeUploaded = 0;


		if (getButtonPress == 0)
		{
			nativeUploaded = NativeSTR_UploadNextFrameToTexture();
		}

		if ((getButtonPress != 0) || (nativeUploaded == 0))
#endif
		{
			if ((MM_GAME_BUTTON_TAPS[0] & SCRAPBOOK_SKIP_INPUT) != 0)
			{
				RaceFlag_SetFullyOnScreen();
			}

			D230.scrapbookState = SCRAP_STOP;
		}
#ifdef CTR_NATIVE
		else
		{
			Platform_PinTextureDisplay(NativeSTR_GetTexture(), NativeSTR_GetHeight(), SCREEN_HEIGHT, 1);
		}
#endif

#ifdef CTR_NATIVE
		if ((getButtonPress == 0) && (nativeUploaded != 0))
		{
			// NOTE(aalhendi): Native decodes this frame on the CPU. Count any
			// elapsed decode vblanks toward the retail 15fps cadence instead of
			// adding them on top of a fresh VSync(4).
			const u32 sourceFrame = NativeSTR_GetSourceFrame();
			// Missing video frames hold the last picture; XA stays on its clock.
			if (sourceFrame > s_scrapbookNativeSourceFrame + 1 && sourceFrame - s_scrapbookNativeSourceFrame < SCRAPBOOK_STREAM_FRAMES)
				s_scrapbookNativeNextVBlank += (sourceFrame - s_scrapbookNativeSourceFrame - 1) * SCRAPBOOK_FRAME_VBLANKS;
			s_scrapbookNativeSourceFrame = sourceFrame;
			Platform_WaitUntilVBlank(s_scrapbookNativeNextVBlank);
			s_scrapbookNativeNextVBlank += SCRAPBOOK_FRAME_VBLANKS;
		}
		else
		{
			VSync(SCRAPBOOK_FRAME_VBLANKS);
		}
#else
		VSync(SCRAPBOOK_FRAME_VBLANKS);
#endif
		break;
	}

	// return disc to normal,
	// return checkered flag to normal
	case SCRAP_STOP:
#ifndef CTR_NATIVE
		SpuSetCommonCDVolume(0, 0);

		MM_Video_StopStream();

		MM_Video_ClearMem();
#else
		NativeAudio_StopXA();
		NativeSTR_Stop();
#endif

		if (RaceFlag_IsFullyOffScreen() == 1)
		{
			RaceFlag_BeginTransition(1);
		}
	GO_BACK:

		D230.scrapbookState = SCRAP_EXIT;
		break;

	case SCRAP_EXIT:
		if (RaceFlag_IsFullyOnScreen() == 1)
		{
			register s32 lev CTR_PSX_REGISTER("$4");

			// change checkered flag back
			RaceFlag_SetDrawOrder(0);

			if ((GAME_TRACKER->gameMode1 & ADVENTURE_MODE) != 0)
			{
				lev = GEM_STONE_VALLEY;
			}
			else
			{
				MM_JumpTo_Title_Returning();

				// return to main menu (adv, tt, arcade, vs, battle)
				sdata_static.mainMenuState = MAIN_MENU_TITLE;

				lev = MAIN_MENU_LEVEL;
			}

			MM_REQUEST_LEVEL(lev);

			RECTMENU_Hide(menu);
		}
		break;
	default:
		return;
	}
}
