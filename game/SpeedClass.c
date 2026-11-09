#include <common.h>
#include <ctr_speed_class.h>

// 200cc speed class (docs/SPEED-CLASS.md). The pick is g_config.speedClass,
// made on the SPEED screen of the Arcade, VS and Time Trial menus
// (game/230/MM_MenuFlow.c) and kept in config.ini. It applies to Arcade, VS
// and Time Trial races only.
#define SPEED_CLASS_RETAIL_MODES                                                                                         	(BATTLE_MODE | ADVENTURE_MODE | ADVENTURE_ARENA | RELIC_RACE | CRYSTAL_CHALLENGE | ADVENTURE_CUP | ADVENTURE_BOSS)

int SpeedClass_Active(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (g_config.speedClass != SPEED_CLASS_200CC || gGT == NULL)
		return 0;
	return SpeedClass_ModeTakesClass(gGT->gameMode1, SPEED_CLASS_RETAIL_MODES, MAIN_MENU | GAME_CUTSCENE,
	                                 gGT->boolDemoMode != 0);
}

// A Time Trial at 200cc: its records and ghost go to files, not to the memory
// card (game/TimeTrialRecords.c). Also true in the menus once Time Trial and
// 200cc are picked, where the track select and the ghost load ask.
int SpeedClass_TimeTrial(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (g_config.speedClass != SPEED_CLASS_200CC || gGT == NULL || gGT->boolDemoMode != 0)
		return 0;
	return (gGT->gameMode1 & TIME_TRIAL) != 0 && (gGT->gameMode1 & SPEED_CLASS_RETAIL_MODES) == 0;
}

// A speed limit written as a retail constant, scaled for the class.
int SpeedClass_Speed(int value)
{
	return SpeedClass_ScaleSpeed(value, SpeedClass_Active());
}

// After VehBirth_SetConsts: every racer's speeds, boosts and accelerations,
// bots included (their speed targets read the same fields). Speeds that the
// physics code limits with literal constants go through SpeedClass_Speed.
void SpeedClass_ApplyConsts(struct Driver *d)
{
	if (!SpeedClass_Active())
		return;

	d->const_Accel_ClassStat = (s16)SpeedClass_ScaleSpeed(d->const_Accel_ClassStat, 1);
	d->const_Accel_Reserves = (s16)SpeedClass_ScaleSpeed(d->const_Accel_Reserves, 1);
	d->const_Speed_ClassStat = (s16)SpeedClass_ScaleSpeed(d->const_Speed_ClassStat, 1);
	d->const_AccelSpeed_ClassStat = (s16)SpeedClass_ScaleSpeed(d->const_AccelSpeed_ClassStat, 1);
	d->const_SingleTurboSpeed = (s16)SpeedClass_ScaleSpeed(d->const_SingleTurboSpeed, 1);
	d->const_SacredFireSpeed = (s16)SpeedClass_ScaleSpeed(d->const_SacredFireSpeed, 1);
	d->const_MaskSpeed = (s16)SpeedClass_ScaleSpeed(d->const_MaskSpeed, 1);
	d->const_DamagedSpeed = (s16)SpeedClass_ScaleSpeed(d->const_DamagedSpeed, 1);
	d->const_TurnRate = (u8)SpeedClass_ScaleTurn(d->const_TurnRate, 1);
}
