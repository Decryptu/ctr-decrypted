#include "platform/native_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Unattended races for checking gameplay timing, mostly 60 FPS against 30:
// boot straight into a race, drive the player's kart through the normal
// physics with a scripted pad, and trace it. See docs/DEV-TOOLS.md.
// Part of the main.c unity build, after the game code, so it reads game state
// directly.

// Autopilot state for one player.
typedef struct
{
	int navIndex;
	int navLevel;
	int navTarget;
	int lastRaceMS;
	int stuckMS;
	int stuckX, stuckZ;
	int reverseMS;
	int wantAngle;
	int steer;
} NativeDevPilot;

typedef struct
{
	int loaded;
	int raceTrack;
	int timeTrial;
	int adventure;
	int cup; // CTR_DEV_MODE=cup: Adventure Gem Cups back to back
	int cups[8];
	int numCups;
	int nextCup;
	int cupRunning;
	int races[16]; // CTR_DEV_RACES: Adventure races back to back, LevelID | NATIVE_DEV_RELIC
	int numRaces;
	int nextRace;
	int hubFrames;
	const char *podiumPath;
	int players;
	int laps;
	int autopilot;
	int hopEvery;
	int exitAfterRace;
	int endFrames;
	const char *ghostIn;
	const char *ghostOut;
	const char *tracePath;
	const char *driverDumpPath;
	FILE *driverDump;
	const char *resultsPath;
	int resultsMS;
	int resultsWritten;
	int raceClock;
	FILE *trace;
	int ghostWritten;
	NativeDevPilot pilot[4];
	int raceFrame;
	int warp;
	int warpPos[4]; // x, y, z (world units), angle
	int warpFrames;
} NativeDevRaceState;

static NativeDevRaceState s_devRace;

// CTR_DEV_RACES: the entry is that track's relic race.
#define NATIVE_DEV_RELIC 0x100

static NativeDevRaceState *NativeDevRace_State(void)
{
	if (!s_devRace.loaded)
	{
		const char *mode = getenv("CTR_DEV_MODE");
		s_devRace.loaded = 1;
		s_devRace.raceTrack = NativeDev_EnvInt("CTR_DEV_RACE", -1);
		s_devRace.timeTrial = (mode != NULL) && (strcmp(mode, "tt") == 0);
		s_devRace.cup = (mode != NULL) && (strcmp(mode, "cup") == 0);
		s_devRace.adventure = s_devRace.cup || ((mode != NULL) && (strcmp(mode, "adv") == 0));
		if (s_devRace.cup)
		{
			// CTR_DEV_CUPS=0,4: the cups to run, in order (0 Red .. 4 Purple).
			const char *list = getenv("CTR_DEV_CUPS");
			const char *p = ((list != NULL) && (*list != '\0')) ? list : "4";
			while ((*p != '\0') && (s_devRace.numCups < 8))
			{
				char *end;
				const long cupID = strtol(p, &end, 10);
				if (end == p)
					break;
				if ((cupID >= 0) && (cupID < 5))
					s_devRace.cups[s_devRace.numCups++] = (int)cupID;
				p = (*end == ',') ? end + 1 : end;
			}
			s_devRace.podiumPath = getenv("CTR_DEV_PODIUM");
			// The title menu's race path is keyed on CTR_DEV_RACE; the cup
			// picks its own tracks.
			if ((s_devRace.numCups > 0) && (s_devRace.raceTrack < 0))
				s_devRace.raceTrack = 0;
		}
		if (s_devRace.adventure && !s_devRace.cup)
		{
			// CTR_DEV_RACES=3,6r,8: Adventure races in order, the first from
			// the title menu, the rest from the hub the previous one returns
			// to. An r makes it the track's relic race.
			const char *p = getenv("CTR_DEV_RACES");
			while ((p != NULL) && (*p != '\0') && (s_devRace.numRaces < 16))
			{
				char *end;
				const long levelID = strtol(p, &end, 10);
				if (end == p)
					break;
				int race = (int)levelID;
				if (*end == 'r')
				{
					race |= NATIVE_DEV_RELIC;
					end++;
				}
				if ((levelID >= 0) && (levelID < GEM_STONE_VALLEY))
					s_devRace.races[s_devRace.numRaces++] = race;
				p = (*end == ',') ? end + 1 : end;
			}
			if (s_devRace.numRaces > 0)
				s_devRace.raceTrack = s_devRace.races[s_devRace.nextRace++];
		}
		s_devRace.laps = NativeDev_EnvInt("CTR_DEV_LAPS", 0);
		s_devRace.autopilot = NativeDev_EnvInt("CTR_DEV_AUTOPILOT", 0);
		s_devRace.hopEvery = NativeDev_EnvInt("CTR_DEV_HOP_MS", 0);
		s_devRace.exitAfterRace = NativeDev_EnvInt("CTR_DEV_EXIT_AFTER_RACE", 0);
		s_devRace.ghostIn = getenv("CTR_DEV_GHOST_IN");
		s_devRace.ghostOut = getenv("CTR_DEV_GHOST_OUT");
		s_devRace.tracePath = getenv("CTR_DEV_TRACE");
		s_devRace.driverDumpPath = getenv("CTR_DEV_DRIVER_DUMP");
		const char *warp = getenv("CTR_DEV_WARP");
		s_devRace.warp = (warp != NULL) && (sscanf(warp, "%d,%d,%d,%d", &s_devRace.warpPos[0], &s_devRace.warpPos[1], &s_devRace.warpPos[2],
		                                            &s_devRace.warpPos[3]) == 4);
		s_devRace.resultsPath = getenv("CTR_DEV_RESULTS");
		s_devRace.resultsMS = NativeDev_EnvInt("CTR_DEV_RESULTS_MS", 30000);
		s_devRace.players = NativeDev_EnvInt("CTR_DEV_PLAYERS", 1);
		if (s_devRace.players < 1)
			s_devRace.players = 1;
		if (s_devRace.players > 4)
			s_devRace.players = 4;
		for (int i = 0; i < 4; i++)
			s_devRace.pilot[i].navLevel = -1;
	}
	return &s_devRace;
}

int NativeDev_RaceTrack(void) { return NativeDevRace_State()->raceTrack; }
int NativeDev_RacePlayers(void) { return NativeDevRace_State()->players; }

int NativeDev_BootLevel(void)
{
	int ending = NativeDev_EnvInt("CTR_DEV_ENDING", 0);
	if (ending <= 0)
		return -1;
	UNLOCK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_BEAT_OXIDE_SECOND);
	if (ending == 1)
	{
		for (int i = 0; i < 18; i++)
			UNLOCK_ADV_BIT(sdata->advProgress.rewards, ADV_REWARD_FIRST_SAPPHIRE_RELIC + i);
	}
	return OXIDE_TRUE_ENDING;
}

// The next cup in CTR_DEV_CUPS, set up as AH_WarpPad does for a Gem Cup pad.
// Returns its first track.
static int NativeDevRace_EnterCup(struct GameTracker *gGT, NativeDevRaceState *dev)
{
	const int cupID = dev->cups[dev->nextCup++];
	gGT->cup.cupID = cupID;
	gGT->cup.trackIndex = 0;
	for (int i = 0; i < 8; i++)
		gGT->cup.points[i] = 0;
	gGT->numLaps = (char)((dev->laps > 0) ? dev->laps : 3);
	dev->cupRunning = 1;
	return data.advCupTrackIDs[4 * cupID];
}

int NativeDev_StartRace(struct GameTracker *gGT)
{
	NativeDevRaceState *dev = NativeDevRace_State();

	gGT->boolDemoMode = 0;
	gGT->numPlyrNextGame = 1;
	// CTR_DEV_CHEATS: gameMode2 cheat bits for the race, as the title-screen
	// codes set them (0x100000 = every turbo pad is a super turbo pad).
	gGT->gameMode2 |= (u32)NativeDev_EnvInt("CTR_DEV_CHEATS", 0);
	for (int i = 0; i < 8; i++)
		data.characterIDs[i] = i;
	// CTR_DEV_CHARACTER: player 1's racer; the bot that had it takes Crash.
	int character = NativeDev_EnvInt("CTR_DEV_CHARACTER", -1);
	if ((character > 0) && (character < 16))
	{
		for (int i = 1; i < 8; i++)
			if (data.characterIDs[i] == character)
				data.characterIDs[i] = 0;
		data.characterIDs[0] = (s16)character;
	}

	// VS: no Arcade flag, two to four players.
	if (dev->players > 1)
	{
		gGT->numPlyrNextGame = (u8)dev->players;
		if (dev->laps > 0)
			gGT->numLaps = (char)dev->laps;
		return dev->raceTrack;
	}

	// Gem Cups from a new profile: the first one starts here, the rest from
	// the hub its podium lands in (NativeDevRace_AdventureHub).
	if (dev->cup)
	{
		gGT->gameMode1 |= ADVENTURE_MODE | ADVENTURE_CUP;
		return NativeDevRace_EnterCup(gGT, dev);
	}

	// Adventure: a trophy race from a new profile. Winning it goes through the
	// podium and back to the track's hub, like entering it from a warp pad.
	if (dev->adventure)
	{
		gGT->gameMode1 |= ADVENTURE_MODE;
		if ((dev->raceTrack & NATIVE_DEV_RELIC) != 0)
			gGT->gameMode1 |= RELIC_RACE;
		gGT->numLaps = (char)((dev->laps > 0) ? dev->laps : 3);
		return dev->raceTrack & ~NATIVE_DEV_RELIC;
	}

	if (!dev->timeTrial)
	{
		gGT->gameMode1 |= ARCADE_MODE;
		if (dev->laps > 0)
			gGT->numLaps = (char)dev->laps;
		return dev->raceTrack;
	}

	// The same setup MM_TrackSelect does before the ghost menu.
	gGT->gameMode1 |= TIME_TRIAL;
	gGT->numLaps = 3;
	sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(0x3e00);
	memset(sdata->ptrGhostTapePlaying, 0, 0x28);
	sdata->boolReplayHumanGhost = 0;

	if (dev->ghostIn != NULL)
	{
		FILE *f = fopen(dev->ghostIn, "rb");
		if (f != NULL)
		{
			if (fread(sdata->ptrGhostTapePlaying, 1, 0x3e00, f) == 0x3e00)
			{
				sdata->boolReplayHumanGhost = 1;
				data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;
			}
			fclose(f);
		}
		if (!sdata->boolReplayHumanGhost)
			printf("[CTR Dev] could not read ghost %s\n", dev->ghostIn);
	}
	return dev->raceTrack;
}

static struct Driver *NativeDevRace_Player(int player)
{
	struct GameTracker *gGT = sdata->gGT;
	if ((gGT == NULL) || (sdata->Loading.stage != LOAD_IDLE) || (gGT->levelID == MAIN_MENU_LEVEL))
		return NULL;
	const int vs = ((u8)gGT->numPlyrCurrGame > 1) && ((gGT->gameMode1 & (BATTLE_MODE | MAIN_MENU | GAME_CUTSCENE)) == 0);
	const int adventureRace = ((gGT->gameMode1 & ADVENTURE_MODE) != 0) && ((gGT->gameMode1 & (ADVENTURE_ARENA | GAME_CUTSCENE)) == 0);
	if (((gGT->gameMode1 & (ARCADE_MODE | TIME_TRIAL)) == 0) && !vs && !adventureRace)
		return NULL;
	if (player >= (u8)gGT->numPlyrCurrGame)
		return NULL;
	return gGT->drivers[player];
}

// The buttons a "<at>:<keys>[:<length>],..." script holds at `now`: each
// entry holds its keys from `at` for `length` (default `defaultLength`). Keys:
// U D L R, X (Cross), O (Circle), S (Square), T (Triangle), Z (Start), 2 (L2,
// the far camera in a hub), H (R1, hop and drift), J (L1).
static unsigned short NativeDev_ScriptHeld(const char *script, long now, long defaultLength)
{
	unsigned short held = 0;
	const char *p = script;
	while (*p != '\0')
	{
		char *end;
		const long at = strtol(p, &end, 10);
		if ((end == p) || (*end != ':'))
			break;
		unsigned short keys = 0;
		for (p = end + 1; (*p != '\0') && (*p != ',') && (*p != ':'); p++)
		{
			switch (*p)
			{
			case 'U': keys |= RAW_BTN_UP; break;
			case 'D': keys |= RAW_BTN_DOWN; break;
			case 'L': keys |= RAW_BTN_LEFT; break;
			case 'R': keys |= RAW_BTN_RIGHT; break;
			case 'X': keys |= RAW_BTN_CROSS; break;
			case 'O': keys |= RAW_BTN_CIRCLE; break;
			case 'S': keys |= RAW_BTN_SQUARE; break;
			case 'T': keys |= RAW_BTN_TRIANGLE; break;
			case 'Z': keys |= RAW_BTN_START; break;
			case '2': keys |= RAW_BTN_L2; break;
			case 'H': keys |= RAW_BTN_R1; break;
			case 'J': keys |= RAW_BTN_L1; break;
			default: break;
			}
		}
		long length = defaultLength;
		if (*p == ':')
		{
			length = strtol(p + 1, &end, 10);
			p = end;
		}
		if ((now >= at) && (now < at + length))
			held |= keys;
		if (*p == ',')
			p++;
	}
	return held;
}

// CTR_DEV_PAD="<frame>:<keys>[:<frames>],...": player 1 holds those buttons
// for two presented frames (or the given count) from that frame on, to drive
// menus or the kart. CTR_DEV_DRIVE is the same script timed in milliseconds
// of race time from the lights (default length 100 ms), so 30 and 60 FPS runs
// get the same input at the same moment of the race. While a script is set it
// owns player 1's pad, except between taps when the autopilot is on.
static int NativeDev_ScriptedPad(int autopilot, unsigned short *buttons, unsigned char *leftX)
{
	static const char *s_padScript;
	static const char *s_driveScript;
	static int s_loaded;
	if (!s_loaded)
	{
		s_loaded = 1;
		s_padScript = getenv("CTR_DEV_PAD");
		s_driveScript = getenv("CTR_DEV_DRIVE");
	}
	const int hasPad = (s_padScript != NULL) && (*s_padScript != '\0');
	const int hasDrive = (s_driveScript != NULL) && (*s_driveScript != '\0') && (NativeDevRace_Player(0) != NULL) &&
	                     (sdata->gGT->trafficLightsTimer <= 0);
	if (!hasPad && !hasDrive)
		return 0;

	unsigned short held = 0;
	if (hasPad)
		held |= NativeDev_ScriptHeld(s_padScript, NativeDev_State()->frame, 2);
	if (hasDrive)
		held |= NativeDev_ScriptHeld(s_driveScript, NativeDevRace_State()->raceClock, 100);
	if ((held == 0) && autopilot)
		return 0;
	*buttons = (unsigned short)~held;
	*leftX = 0x80;
	return 1;
}

// CTR_DEV_CHARACTER_SELECT=1: the title menu opens the Arcade character
// select (CTR_DEV_PLAYERS players) instead of idling. CTR_DEV_CHARACTER puts
// player 1's cursor on that racer. CTR_DEV_UNLOCKS=1 unlocks the retail
// secret racers in this run's save, =2 N. Oxide as well.
int NativeDev_CharacterSelect(void)
{
	static int s_mode = -1;
	if (s_mode < 0)
		s_mode = NativeDev_EnvInt("CTR_DEV_CHARACTER_SELECT", 0);
	if (s_mode <= 0)
		return 0;
	const int unlocks = NativeDev_EnvInt("CTR_DEV_UNLOCKS", 0);
	for (int bit = 5; (unlocks >= 1) && (bit <= 11); bit++)
		UNLOCK_ADV_BIT(sdata->gameProgress.unlocks, bit);
	if (unlocks >= 2)
		UNLOCK_ADV_BIT(sdata->gameProgress.unlocks, GAME_UNLOCK_BIT_OXIDE);
	const int character = NativeDev_EnvInt("CTR_DEV_CHARACTER", -1);
	if ((character >= 0) && (character < 16))
		sdata->characterIDs_backup[0] = (s16)character;
	return 1;
}

// Steer toward a nav point ahead on one of the AI's lines, with the throttle
// held, so the kart stays on the road through the real driving physics.
// Player 1 follows lane 0; in VS each player takes the next lane, so on tracks
// with forks some players take the other branch.
int NativeDev_AutopilotPad(int player, unsigned short *buttons, unsigned char *leftX)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;

	if ((player == 0) && NativeDev_ScriptedPad(dev->autopilot, buttons, leftX))
		return 1;

	if (!dev->autopilot)
		return 0;

	// Adventure: after the race and on the podium, tap Cross every half second
	// so the run continues into the hub, where the pad is left alone. Between
	// cups or CTR_DEV_RACES it also dismisses Aku Aku's hints, so the next one
	// can start.
	if (dev->adventure && (player == 0) && (gGT != NULL) && (sdata->Loading.stage == LOAD_IDLE) &&
	    ((((gGT->gameMode1 & END_OF_RACE) != 0) && (NativeDevRace_Player(0) != NULL)) || ((gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0) ||
	     ((dev->cup || (dev->numRaces > 0)) && (sdata->AkuAkuHintState != 0))))
	{
		unsigned short held = ((sdata->frameCounter / 15) & 1) ? RAW_BTN_CROSS : 0;
		*buttons = (unsigned short)~held;
		*leftX = 0x80;
		return 1;
	}

	if (((d = NativeDevRace_Player(player)) == NULL) || ((gGT->gameMode1 & END_OF_RACE) != 0))
		return 0;

	NativeDevPilot *p = &dev->pilot[player];
	struct NavHeader *nh = sdata->NavPath_ptrHeader[player % 3];
	if ((nh == NULL) || (nh->numPoints <= 0))
		nh = sdata->NavPath_ptrHeader[0];
	if ((nh == NULL) || (nh->numPoints <= 0))
		return 0;

	struct NavFrame *frames = NAVHEADER_GETFRAME(nh);
	const int count = nh->numPoints;
	const int x = d->posCurr.x >> 8, y = d->posCurr.y >> 8, z = d->posCurr.z >> 8;

	// Nearest node: a full search on a new level, then a window around the
	// last one so a lap boundary or a crossing section cannot make it jump.
	int from = p->navIndex - 8, span = 40;
	if (p->navLevel != gGT->levelID)
	{
		p->navLevel = gGT->levelID;
		from = 0;
		span = count;
	}
	int best = p->navIndex;
	long long bestDist = -1;
	for (int k = 0; k < span; k++)
	{
		int i = ((from + k) % count + count) % count;
		long long dx = frames[i].pos.x - x, dy = frames[i].pos.y - y, dz = frames[i].pos.z - z;
		long long dist = dx * dx + dy * dy * 4 + dz * dz;
		if ((bestDist < 0) || (dist < bestDist))
		{
			bestDist = dist;
			best = i;
		}
	}
	p->navIndex = best;

	// Aim at the line about half a second ahead. A fixed far point cuts
	// corners into the inside wall when the kart is slow, as it is after
	// reversing off a wall, so the same crash would repeat.
	int speedAbs = (d->speedApprox < 0) ? -d->speedApprox : d->speedApprox;
	int ahead = speedAbs / 12;
	if (ahead < 500)
		ahead = 500;
	if (ahead > 1200)
		ahead = 1200;
	int target = best;
	for (int k = 0; k < count; k++)
	{
		target = (best + k) % count;
		int dx = frames[target].pos.x - x, dz = frames[target].pos.z - z;
		if (dx * dx + dz * dz > ahead * ahead)
			break;
	}

	int want = ratan2(frames[target].pos.x - x, frames[target].pos.z - z) & 0xfff;
	int err = ((want - (d->angle & 0xfff) + 0x800) & 0xfff) - 0x800;
	// Past the analog deadzone as soon as there is any error to correct.
	int steer = -err / 3;
	if (steer > 0)
		steer += 40;
	if (steer < 0)
		steer -= 40;
	if (steer > 127)
		steer = 127;
	if (steer < -127)
		steer = -127;

	// Wedged (under 300 units travelled in a second and a half of race time):
	// reverse out for a second at full opposite lock. Distance rather than
	// speed, because a kart pinned on a wall can report speed and not move.
	// Full lock, because on a turbo pad against a wall the pad refills the
	// reserves every frame, the boost overrides the brake, and only turning
	// away breaks the contact.
	int raceMS = d->timeElapsedInRace;
	int dt = raceMS - p->lastRaceMS;
	p->lastRaceMS = raceMS;
	if ((dt > 0) && (dt < 100) && (p->reverseMS <= 0))
	{
		long long mx = x - p->stuckX, mz = z - p->stuckZ;
		if (mx * mx + mz * mz > 300 * 300)
		{
			p->stuckX = x;
			p->stuckZ = z;
			p->stuckMS = 0;
		}
		else
			p->stuckMS += dt;
		if (p->stuckMS > 1500)
		{
			p->stuckMS = 0;
			p->reverseMS = 1000;
		}
	}

	// The d-pad too on a hard turn: analog alone steers too gently.
	unsigned short held = (dev->autopilot == 2) ? 0 : RAW_BTN_CROSS;
	if (dev->autopilot == 2)
		steer = 0;
	else if (p->reverseMS > 0)
	{
		if (dt > 0)
			p->reverseMS -= dt;
		held = RAW_BTN_SQUARE | RAW_BTN_DOWN;
		steer = (steer > 0) ? -127 : 127;
	}
	if (steer < -48)
		held |= RAW_BTN_LEFT;
	if (steer > 48)
		held |= RAW_BTN_RIGHT;
	if (dev->hopEvery > 0)
	{
		int t = d->timeElapsedInRace % dev->hopEvery;
		if ((d->timeElapsedInRace > 0) && (t < 100))
			held |= RAW_BTN_R1;
	}

	p->navTarget = target;
	p->wantAngle = want;
	p->steer = steer;
	*buttons = (unsigned short)~held;
	*leftX = (unsigned char)(0x80 + steer);
	return 1;
}

// CTR_DEV_MODE=cup, in the hub a finished cup returns to. On arrival,
// CTR_DEV_PODIUM=<csv> gets the podium the cup set up: the racer on each step
// (-1 for an empty step) and the cup's final order. Once the podium is over,
// the next cup in CTR_DEV_CUPS starts as its warp pad would start it. With
// CTR_DEV_MODE=adv, the next race in CTR_DEV_RACES starts the same way.
static void NativeDevRace_AdventureHub(NativeDevRaceState *dev, struct GameTracker *gGT)
{
	if (!dev->adventure || (gGT == NULL) || (sdata->Loading.stage != LOAD_IDLE) || ((gGT->gameMode1 & ADVENTURE_ARENA) == 0))
	{
		dev->hubFrames = 0;
		return;
	}

	if (dev->cupRunning)
	{
		dev->cupRunning = 0;
		FILE *f = (dev->podiumPath != NULL) ? fopen(dev->podiumPath, "a") : NULL;
		if (f != NULL)
		{
			const u8 steps[3] = {gGT->podium_modelIndex_First, gGT->podium_modelIndex_Second, gGT->podium_modelIndex_Third};
			fprintf(f, "%d,%d", gGT->cup.cupID, gGT->podiumRewardID);
			for (int i = 0; i < 3; i++)
				fprintf(f, ",%d", (steps[i] != 0) ? steps[i] - STATIC_CRASHDANCE : -1);
			for (int i = 0; i < 8; i++)
				fprintf(f, ",%d", data.cupPositionPerPlayer[i]);
			fprintf(f, "\n");
			fclose(f);
		}
	}

	// A warp pad does not start while Aku Aku is talking (AH_WarpPad), and
	// the hint's update would carry on into the race level.
	if (((gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0) || (sdata->AkuAkuHintState != 0) || (sdata->AkuHint_RequestedHint != -1) ||
	    (sdata->boolDraw3D_AdvMask != 0))
	{
		dev->hubFrames = 0;
		return;
	}
	if ((++dev->hubFrames < 60) || (dev->cup ? (dev->nextCup >= dev->numCups) : (dev->nextRace >= dev->numRaces)))
		return;

	dev->hubFrames = 0;
	sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;
	if (dev->cup)
	{
		sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_CUP;
		MainRaceTrack_RequestLoad(NativeDevRace_EnterCup(gGT, dev));
		return;
	}
	const int race = dev->races[dev->nextRace++];
	if ((race & NATIVE_DEV_RELIC) != 0)
		sdata->Loading.OnBegin.AddBitsConfig0 |= RELIC_RACE;
	gGT->numLaps = (char)((dev->laps > 0) ? dev->laps : 3);
	MainRaceTrack_RequestLoad(race & ~NATIVE_DEV_RELIC);
}

// CTR_DEV_WARP=x,y,z,angle: once a level has loaded, hold player 1 at that
// world position and heading, at rest, for two seconds while the camera
// settles, then let go. Works in the adventure hubs too. The position of a
// player's F5 save is the kart Instance's matrix translation.
static void NativeDevRace_Warp(NativeDevRaceState *dev, struct GameTracker *gGT)
{
	if (!dev->warp || (gGT == NULL) || (sdata->Loading.stage != LOAD_IDLE) || (gGT->levelID == MAIN_MENU_LEVEL) || (gGT->drivers[0] == NULL))
	{
		dev->warpFrames = 0;
		return;
	}
	if (dev->warpFrames >= 120)
		return;
	dev->warpFrames++;

	struct Driver *d = gGT->drivers[0];
	for (int i = 0; i < 3; i++)
	{
		d->posCurr.v[i] = dev->warpPos[i] << 8;
		d->posPrev.v[i] = dev->warpPos[i] << 8;
		d->velocity.v[i] = 0;
	}
	d->xSpeed = d->ySpeed = d->zSpeed = 0;
	d->speed = d->speedApprox = 0;
	d->angle = (s16)dev->warpPos[3];
	// In a race the mask-grab progress check would see the warp as a skipped
	// section and take the kart back: forget the last checkpoint progress, and
	// let the first quad touched become the respawn point.
	d->lastValid = NULL;
	d->distanceToFinish_checkpoint = 0;
}

// Once per presented frame, from NativeDev_EndFrame.
void NativeDev_RaceFrame(void)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = NativeDevRace_Player(0);

	NativeDevRace_AdventureHub(dev, gGT);
	NativeDevRace_Warp(dev, gGT);

	if (d == NULL)
	{
		dev->raceFrame = 0;
		dev->raceClock = 0;
		return;
	}

	// A won Adventure race returns to prevLEV, which the load set to the main
	// menu; send it to the track's own hub instead, as a warp pad would.
	if (dev->adventure && (gGT->levelID < GEM_STONE_VALLEY))
		gGT->prevLEV = GEM_STONE_VALLEY + data.metaDataLEV[gGT->levelID].hubID;

	// Race time from the lights, kept here because AI drivers (the attract
	// demo's player 1) do not count their own timeElapsedInRace.
	if (gGT->trafficLightsTimer <= 0)
		dev->raceClock += gGT->elapsedTimeMS;

	if ((dev->driverDumpPath != NULL) && (dev->driverDump == NULL))
		dev->driverDump = fopen(dev->driverDumpPath, "wb");

	if (dev->driverDump != NULL)
	{
		// The race frame, then all eight Driver structs (zeros for an empty slot).
		static const struct Driver empty;
		fwrite(&dev->raceFrame, sizeof(dev->raceFrame), 1, dev->driverDump);
		for (int i = 0; i < 8; i++)
			fwrite(gGT->drivers[i] != NULL ? gGT->drivers[i] : &empty, sizeof(struct Driver), 1, dev->driverDump);
	}

	if ((dev->tracePath != NULL) && (dev->trace == NULL))
	{
		dev->trace = fopen(dev->tracePath, "w");
		if (dev->trace != NULL)
			fprintf(dev->trace, "frame,fps60,level,lights,end,raceMS,x,y,z,angle,speed,air,kart,lap,reserves,anim,animFrame,gx,gy,gz,gvis,nav,navTarget,want,steer,vx,vy,vz,coll,timer,fireCap,cx,cy,cz,crx,cry,crz,turn,rotW,camSpeed,pcx,pcy,pcz,pcrx,pcry,pcrz,spin,wobble,lerpVel,rotPrevW,drift,clock,base,fire\n");
	}

	if (dev->trace != NULL)
	{
		int gx = 0, gy = 0, gz = 0, gvis = 0;
		struct Thread *t = gGT->threadBuckets[GHOST].thread;
		for (; t != NULL; t = t->siblingThread)
		{
			struct Driver *g = t->object;
			if ((g != NULL) && (g->ghostID == 0) && (g->instSelf != NULL))
			{
				gx = g->instSelf->matrix.t[0];
				gy = g->instSelf->matrix.t[1];
				gz = g->instSelf->matrix.t[2];
				gvis = (g->instSelf->flags & HIDE_MODEL) == 0;
			}
		}
		fprintf(dev->trace, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", dev->raceFrame, CTR_NATIVE_60FPS_ACTIVE ? 1 : 0,
		        gGT->levelID, gGT->trafficLightsTimer, (gGT->gameMode1 & END_OF_RACE) != 0, d->timeElapsedInRace, d->posCurr.x >> 8,
		        d->posCurr.y >> 8, d->posCurr.z >> 8, d->angle & 0xfff, d->speedApprox, (d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0,
		        d->kartState, d->lapIndex, d->reserves, d->instSelf ? d->instSelf->animIndex : -1, d->instSelf ? d->instSelf->animFrame : -1, gx,
		        gy, gz, gvis, dev->pilot[0].navIndex, dev->pilot[0].navTarget, dev->pilot[0].wantAngle, dev->pilot[0].steer, d->velocity.x,
		        d->velocity.y, d->velocity.z, (int)d->collisionFlags, gGT->timer, d->fireSpeedCap);
		// Player 1's camera, and the kart angles it follows, for race_trace.py smooth.
		struct PushBuffer *pb = &gGT->pushBuffer[0];
		fprintf(dev->trace, ",%d,%d,%d,%d,%d,%d,%d,%d,%d", pb->pos.x, pb->pos.y, pb->pos.z, pb->rot.x, pb->rot.y, pb->rot.z, d->turnAngleCurr,
		        d->rotCurr.w, gGT->cameraDC[0].cameraMoveSpeed);
		// The same camera unrounded (PGXP precise camera), or the integer one,
		// then the turning state race_trace.py fps compares between 30 and 60.
		double ppos[3] = {pb->pos.x, pb->pos.y, pb->pos.z}, prot[3] = {pb->rot.x, pb->rot.y, pb->rot.z};
		NativePgxp_ViewGetPose(0, ppos, prot);
		fprintf(dev->trace, ",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d\n", ppos[0], ppos[1], ppos[2], prot[0], prot[1], prot[2], d->rotationSpinRate,
		        d->turnWobbleAngle, d->turnAngleLerpVel, d->rotPrev.w, d->KartStates.Drifting.numFramesDrifting, dev->raceClock, d->baseSpeed, d->fireSpeed);
	}
	dev->raceFrame++;

	// Where every racer is at a fixed race time, to compare the AI between
	// runs: rank, lap, and distance left to the finish line.
	if ((dev->resultsPath != NULL) && !dev->resultsWritten && (dev->raceClock >= dev->resultsMS))
	{
		FILE *f = fopen(dev->resultsPath, "w");
		dev->resultsWritten = 1;
		if (f != NULL)
		{
			// distanceToFinish counts down from trackLength after the start line.
			struct Level *lev = gGT->level1;
			int trackLength = ((lev != NULL) && (lev->ptr_restart_points != NULL)) ? (int)lev->ptr_restart_points[0].distToFinish << 3 : 0;
			fprintf(f, "driver,raceMS,rank,lap,distanceToFinish,trackLength\n");
			for (int i = 0; i < 8; i++)
			{
				struct Driver *r = gGT->drivers[i];
				if (r != NULL)
					fprintf(f, "%d,%d,%d,%d,%u,%d\n", i, dev->raceClock, r->driverRank, r->lapIndex, r->distanceToFinish_curr, trackLength);
			}
			fclose(f);
		}
	}

	// GhostTape_End has run and sized the tape once the run counts as a new
	// best (MainGameEnd_CheckTimeTrialGhost).
	if ((dev->ghostOut != NULL) && !dev->ghostWritten && ((gGT->gameModeEnd & PLAYER_GHOST_BEAT) != 0) &&
	    (sdata->GhostRecording.ptrGhost != NULL))
	{
		FILE *f = fopen(dev->ghostOut, "wb");
		dev->ghostWritten = 1;
		if (f != NULL)
		{
			fwrite(sdata->GhostRecording.ptrGhost, 1, 0x3e00, f);
			fclose(f);
		}
	}

	// A second after the finish, so the end-of-race bookkeeping above has run.
	if (dev->exitAfterRace && ((gGT->gameMode1 & END_OF_RACE) != 0) && (++dev->endFrames >= 60))
	{
		NativeDev_RaceShutdown();
		exit(0);
	}
}

void NativeDev_RaceShutdown(void)
{
	if (s_devRace.trace != NULL)
	{
		fclose(s_devRace.trace);
		s_devRace.trace = NULL;
	}
	if (s_devRace.driverDump != NULL)
	{
		fclose(s_devRace.driverDump);
		s_devRace.driverDump = NULL;
	}
}

// CTR_DEV_QUAD_DUMP=<csv>: every quadblock of the level as it loads (called
// from LevInstDef_UnPack), for studying level data offline: index, flags,
// terrain, bounding box, and each texture slot's pointer, palette (clut) and
// page. An animated slot (tagged pointer) reports its first frame and how many
// frames it has. tools/decrypted/quad_dump.py reads it.
static void NativeDev_QuadTexture(FILE *f, const void *slot)
{
	uintptr_t p = (uintptr_t)slot;
	const struct TextureLayout *t = (const struct TextureLayout *)p;
	int frames = 0;
	if (p & 1)
	{
		const struct AnimTex *anim = (const struct AnimTex *)(p - 1);
		frames = anim->numFrames;
		t = (const struct TextureLayout *)(ANIMTEX_GETARRAY(anim))[0];
	}
	if (t == NULL)
		fprintf(f, ",0,0,0,0");
	else
		fprintf(f, ",%lx,%d,%u,%u", (unsigned long)p, frames, t->clut, t->tpage);
}

void NativeDev_QuadDump(const struct mesh_info *mesh)
{
	const char *path = getenv("CTR_DEV_QUAD_DUMP");
	if (path == NULL || *path == '\0' || mesh == NULL)
		return;
	FILE *f = fopen(path, "a");
	if (f == NULL)
		return;
	fprintf(f, "# level %d, %d bytes free in the level pack\n", sdata->gGT->levelID, MEMPACK_GetFreeBytes());
	fprintf(f, "level,quad,flags,terrain,minx,miny,minz,maxx,maxy,maxz");
	fprintf(f, ",low,lowFrames,lowClut,lowTpage");
	for (int face = 0; face < 4; face++)
		fprintf(f, ",mid%d,mid%dFrames,mid%dClut,mid%dTpage", face, face, face, face);
	fprintf(f, "\n");
	for (int i = 0; i < mesh->numQuadBlock; i++)
	{
		const struct QuadBlock *q = &mesh->ptrQuadBlockArray[i];
		fprintf(f, "%d,%d,%u,%u,%d,%d,%d,%d,%d,%d", sdata->gGT->levelID, i, (unsigned)q->quadFlags, q->terrain_type,
		        q->bbox.min.x, q->bbox.min.y, q->bbox.min.z, q->bbox.max.x, q->bbox.max.y, q->bbox.max.z);
		NativeDev_QuadTexture(f, q->ptr_texture_low);
		for (int face = 0; face < 4; face++)
			NativeDev_QuadTexture(f, q->ptr_texture_mid[face]);
		fprintf(f, "\n");
	}
	fclose(f);
}
