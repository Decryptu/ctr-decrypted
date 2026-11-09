#ifndef CTR_SPEED_CLASS_H
#define CTR_SPEED_CLASS_H

// 200cc speed class (docs/SPEED-CLASS.md): pure rules, kept free of game state
// so tools/test-speed-class.c can check them.

enum
{
	SPEED_CLASS_CLASSIC = 0,
	SPEED_CLASS_200CC = 1,
	SPEED_CLASS_COUNT = 2,
};

// Speeds, boosts and accelerations scale by 6/5. The ceiling is the s16 the
// game keeps speeds in: a super turbo pad pushes about 3950 past the speed cap
// in one frame (retail peaks at 29552 against 0x6400), which at the scaled cap
// (30720) passes 32767, so VehPhysCrash_ConvertVecToSpeed saturates the stored
// speed. A larger factor would also overflow the bots' 0x6900 target cap.
#define SPEED_CLASS_SPEED_NUM 6
#define SPEED_CLASS_SPEED_DEN 5

// Steering rate scales by 11/10, about the square root of the speed factor:
// at the same steering rate a 20% faster kart takes 20% wider corners, and the
// full 20% would make low-speed steering twitchy.
#define SPEED_CLASS_TURN_NUM 11
#define SPEED_CLASS_TURN_DEN 10

static inline int SpeedClass_ScaleSpeed(int value, int active)
{
	return active ? value * SPEED_CLASS_SPEED_NUM / SPEED_CLASS_SPEED_DEN : value;
}

static inline int SpeedClass_ScaleTurn(int value, int active)
{
	return active ? value * SPEED_CLASS_TURN_NUM / SPEED_CLASS_TURN_DEN : value;
}

// Which races take the picked class: Arcade, VS and Time Trial races.
// Adventure (its relic times, bosses and the Archipelago logic are tuned for
// retail speed), Battle and the attract demo stay retail. The flags are
// gameMode1's (include/namespace_Main.h); VS is a race with none of the mode
// flags set.
static inline int SpeedClass_ModeTakesClass(unsigned gameMode1, unsigned modeFlags, unsigned menuFlags, int demo)
{
	if (demo || (gameMode1 & menuFlags) != 0)
		return 0;
	return (gameMode1 & modeFlags) == 0;
}

#endif
