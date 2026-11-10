#ifndef CTR_PENTA_H
#define CTR_PENTA_H

enum
{
	PENTA_STATS_NTSC = 0,
	PENTA_STATS_PAL = 1,
	PENTA_PAL_ACCEL_ROW = 9,
	PENTA_PAL_SPEED_ROW = 11,
	PENTA_PAL_ACCEL_SPEED_ROW = 12,
};

// Custom racers own their engine choice. A loaded Adventure seed owns its
// Penta/stat rules; the local preference applies elsewhere, to humans and bots.
static inline int Penta_UsePalStats(int style, int isPenta, int customRacer, int roomStats)
{
	return style == PENTA_STATS_PAL && isPenta && customRacer < 0 && !roomStats;
}

// High Octane's complete 65-row PAL Penta table matches our TURN column
// except these three halfwords. Keep the other rows and engine-audio ID intact.
static inline int Penta_PhysValue(int usePal, int row, int nativeValue)
{
	if (!usePal)
		return nativeValue;
	switch (row)
	{
	case PENTA_PAL_ACCEL_ROW: return 544;
	case PENTA_PAL_SPEED_ROW: return 13900;
	case PENTA_PAL_ACCEL_SPEED_ROW: return 15400;
	default: return nativeValue;
	}
}

#endif
