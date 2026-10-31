#include <common.h>

void MainKillGame_StopCTR(void)
{
	EnterCriticalSection();
	DrawSyncCallback((void (*)(void))sdata->MainDrawCb_DrawSyncPtr);
	ExitCriticalSection();
	StopCallback();

#ifndef CTR_NATIVE
	MEMCARD_CloseCard();
#else
	// NOTE(aalhendi): Native skips PSX memcard event teardown.
#endif

	PadStopCom();
	ResetGraph(3);
	VSyncCallback(0);

	Timer_Destroy();
}

void MainKillGame_LaunchSpyro2(void)
{
#ifdef CTR_NATIVE
	// Fork change: native cannot chain-load the Spyro 2 demo, and the retail
	// path below ended in an endless loop, hanging the game. The code unlocks
	// N. Oxide instead, as in High Octane.
	UNLOCK_ADV_BIT(GAME_PROGRESS.unlocks, GAME_UNLOCK_BIT_OXIDE);
	OtherFX_Play(0x67, 1);
	return;
#endif

	CTR_ErrorScreen(0, 0, 0);

	Music_Stop();

	// clear backup, destroy music, destroy all fx
	howl_StopAudio(1, 1, 1);

	Bank_DestroyAll();

	howl_Disable();

	VSync(0x1e);

	MainKillGame_StopCTR();

#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native cannot chain-load the Spyro executable.

	while (1)
	{
	}

#else

	_96_remove();
	_96_init();

	LoadExec(rdata.s_PathTo_SpyroExe, 0x801fff00, 0);

#endif
}
