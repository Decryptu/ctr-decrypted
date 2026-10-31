#include <common.h>

void UI_CupStandings_FinalizeCupRanks(void)
{
	register struct GameTracker *trackerInput CTR_PSX_REGISTER("$4");
	struct GameTracker *gGT;
	register s32 numDrivers CTR_PSX_REGISTER("$3");
	s16 tiedTopCount;
	s32 topScore;
	register s32 bestRank CTR_PSX_REGISTER("$8");
	register s16 selectedRankSlot CTR_PSX_REGISTER("$12");
	s16 i;
	s16 rankSlot;
	s32 *positions;

	bestRank = 99;
	CTR_PSX_OBSERVE_VALUE(bestRank);
	trackerInput = GAME_TRACKER;
	CTR_PSX_OBSERVE_VALUE(trackerInput);
	// NOTE(aalhendi): Keep the loop tracker compiler-managed so GCC can
	// cache the leading score rather than reload it on every comparison.
	gGT = trackerInput;
	CTR_PSX_FORGET_VALUE(gGT);
	// NOTE(aalhendi): This is a table slot, not a driver ID, and persists
	// between outer-loop iterations just as it does in retail.
	selectedRankSlot = -1;
	CTR_PSX_OBSERVE_VALUE(selectedRankSlot);

	numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	if (numDrivers >= 5)
	{
		numDrivers = 4;
	}

	// NOTE(aalhendi): Only the leading equal-score group is re-ranked.
	// Counts zero and one still process the first slot in the loop below.
	tiedTopCount = 0;
	i = 1;
	if (i < numDrivers)
	{
		register const s32 *leadingPositions CTR_PSX_REGISTER("$7");
		leadingPositions = data.cupPositionPerPlayer;
		CTR_PSX_OBSERVE_VALUE(leadingPositions);
		topScore = gGT->cup.points[leadingPositions[0]];
		for (; i < numDrivers; i++)
		{
			CTR_PSX_OBSERVE_VALUE(topScore);
			if (gGT->cup.points[leadingPositions[i]] != topScore)
			{
				break;
			}
			tiedTopCount++;
		}
	}

	CTR_PSX_RELOAD(GAME_TRACKER);
	for (rankSlot = 0; rankSlot < tiedTopCount + 1; rankSlot++)
	{
		struct GameTracker *rankGT;
		s32 previousRankValue;

		rankGT = GAME_TRACKER;
		positions = data.cupPositionPerPlayer;
		for (i = rankSlot; i < tiedTopCount + 1; i++)
		{
			struct Driver *driver;
			s32 driverRank;
			driver = rankGT->drivers[positions[i]];
			driverRank = (u16)driver->driverRank;

			if (driver->driverRank < (s16)bestRank)
			{
				bestRank = driverRank;
				selectedRankSlot = i;
			}
		}

		previousRankValue = (u16)positions[rankSlot];
#ifdef CTR_NATIVE
		// Fix: retail stored the slot number (selectedRankSlot) instead of the
		// driver in that slot, so breaking a tie for first in an Arcade or VS
		// cup handed the place to whichever driver had that index, and dropped
		// the racer who actually won the tie from the standings.
		positions[rankSlot] = positions[(s16)selectedRankSlot];
#else
		positions[rankSlot] = (s16)selectedRankSlot;
#endif
		positions[(s16)selectedRankSlot] = (s16)previousRankValue;

		bestRank = 99;
	}
}

void UI_CupStandings_UpdateCupRanks(void)
{
	register struct GameTracker *gGT CTR_PSX_REGISTER("$4");
	register struct GameTracker *cupGT CTR_PSX_REGISTER("$13");
	register s32 bestScore CTR_PSX_REGISTER("$9");
	s32 bestIndex;
	u8 assignedMask;
	register s32 players CTR_PSX_REGISTER("$3");
	register s32 totalDrivers CTR_PSX_REGISTER("$2");
	register s32 numDrivers CTR_PSX_REGISTER("$10");
	register s32 rankSlot CTR_PSX_REGISTER("$8");
	register s32 driverIndex CTR_PSX_REGISTER("$4");
	register s32 *positions CTR_PSX_REGISTER("$2");
	register u8 *scoreBase CTR_PSX_REGISTER("$5");

	bestScore = 0;
	CTR_PSX_OBSERVE_VALUE(bestScore);
	bestIndex = -1;
	CTR_PSX_OBSERVE_VALUE(bestIndex);
	gGT = GAME_TRACKER;
	CTR_PSX_OBSERVE_VALUE(gGT);
	assignedMask = 0;
	CTR_PSX_OBSERVE_VALUE(assignedMask);
	CTR_PSX_FORGET_VALUE(bestScore);
	CTR_PSX_FORGET_VALUE(bestIndex);

	players = gGT->numPlyrCurrGame;
	CTR_PSX_OBSERVE_VALUE(players);
	totalDrivers = gGT->numBotsNextGame;
	CTR_PSX_DEPEND_VALUE(totalDrivers, players);
	totalDrivers += players;
	rankSlot = 0;
	if (totalDrivers == 0)
	{
		return;
	}

	numDrivers = totalDrivers;
	cupGT = gGT;
	do
	{
		driverIndex = numDrivers - 1;
		CTR_PSX_FORGET_VALUE(driverIndex);
		if (driverIndex >= 0)
		{
			register u32 byteOffset CTR_PSX_REGISTER("$2");
			byteOffset = driverIndex * sizeof(s32);
			CTR_PSX_OBSERVE_VALUE(byteOffset);
			// NOTE(aalhendi): Keep the cursor within the whole tracker and add
			// the points offset at each access. MIPS/i686 pointers are 32 bits.
			scoreBase = (u8 *)(byteOffset + (u32)cupGT);
			do
			{
				const s32 *score;
				CTR_PSX_FORGET_VALUE(scoreBase);
				score = (const s32 *)(scoreBase + offsetof(struct GameTracker, cup.points));
				// NOTE(aalhendi): Scores are signed words, but the running best
				// compares as a signed halfword and retains the accepted low half.
				if ((*score >= (s16)bestScore) && (((assignedMask >> driverIndex) & 1) == 0))
				{
					bestScore = (u16)*score;

					if ((s16)bestIndex != -1)
					{
						assignedMask &= ~(1 << (s16)bestIndex);
					}

					bestIndex = driverIndex;
					assignedMask |= 1 << driverIndex;
				}
				driverIndex--;
				if (driverIndex >= 0)
				{
					scoreBase -= sizeof(s32);
				}
			} while (driverIndex >= 0);
		}

		positions = data.cupPositionPerPlayer;
		CTR_PSX_FORGET_VALUE(rankSlot);
		CTR_PSX_FORGET_VALUE(positions);
		positions[rankSlot] = (s16)bestIndex;
		bestScore = 0;
		bestIndex = -1;
		rankSlot++;
	} while (rankSlot < numDrivers);

#ifdef CTR_NATIVE
	// Fix: retail left the slots past the field as the last bigger cup wrote
	// them, so after an eight-racer cup the five-racer Purple Cup kept three
	// stale driver indices. The end of the cup ranks drivers[slot value] for
	// all eight slots, so a stale index that is in the field overwrote that
	// racer's place with 5..7 and left their podium step empty
	// (ctr-native-ap #282). The field's slots hold exactly drivers 0..n-1, so
	// filling the rest with n..7 (never spawned, NULL) keeps the array the
	// permutation it starts as.
	for (; rankSlot < 8; rankSlot++)
	{
		data.cupPositionPerPlayer[rankSlot] = (s16)rankSlot;
	}
#endif
}
