// Rules behind the 200cc speed class (include/ctr_speed_class.h,
// docs/SPEED-CLASS.md).
// Build: cc -Wall -Wextra -I include -o test-speed-class tools/test-speed-class.c
#include <stdio.h>

#include <ctr_speed_class.h>

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-44s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

// gameMode1 flags (include/namespace_Main.h), copied so the test stays freestanding.
enum
{
	BATTLE_MODE = 0x20,
	MAIN_MENU = 0x2000,
	TIME_TRIAL = 0x20000,
	ADVENTURE_MODE = 0x80000,
	ARCADE_MODE = 0x400000,
	RELIC_RACE = 0x4000000,
	GAME_CUTSCENE = 0x20000000,
};

static int takes(unsigned gameMode1, int demo)
{
	return SpeedClass_ModeTakesClass(gameMode1, BATTLE_MODE | ADVENTURE_MODE | RELIC_RACE, MAIN_MENU | GAME_CUTSCENE, demo);
}

int main(void)
{
	// Retail values from data.metaPhys (game/zGlobal_DATA.c).
	expect("Classic leaves speeds alone", SpeedClass_ScaleSpeed(13140, 0), 13140);
	expect("200cc: Crash's top speed", SpeedClass_ScaleSpeed(13140, 1), 15768);
	expect("200cc: reserves acceleration", SpeedClass_ScaleSpeed(1152, 1), 1382);
	expect("200cc: turn rate 28", SpeedClass_ScaleTurn(28, 1), 30);
	expect("200cc: turn rate 30 fits a byte", SpeedClass_ScaleTurn(30, 1) <= 255, 1);

	// The caps stay in the s16 the game keeps speeds in. A super turbo pad's
	// push past the cap does not, which VehPhysCrash_ConvertVecToSpeed saturates.
	expect("200cc speed cap fits s16", SpeedClass_ScaleSpeed(0x6400, 1) <= 32767, 1);
	expect("200cc bot target cap fits s16", SpeedClass_ScaleSpeed(0x6900, 1) <= 32767, 1);

	expect("Arcade takes the class", takes(ARCADE_MODE, 0), 1);
	expect("VS takes the class", takes(0, 0), 1);
	expect("Time Trial takes the class", takes(TIME_TRIAL, 0), 1);
	expect("Relic Race stays retail", takes(TIME_TRIAL | ADVENTURE_MODE | RELIC_RACE, 0), 0);
	expect("Adventure stays retail", takes(ADVENTURE_MODE, 0), 0);
	expect("Battle stays retail", takes(BATTLE_MODE, 0), 0);
	expect("Attract demo stays retail", takes(ARCADE_MODE, 1), 0);
	expect("Menu stays retail", takes(MAIN_MENU, 0), 0);

	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
