#include "platform/native_dev.h"
#include "platform/native_memory.h"
#include "platform/native_config.h"
#include "platform/native_memcard.h"
#ifdef CTR_CUSTOM_PACKAGES
#include "platform/native_assets.h"
#include "platform/native_custom_identity.h"
#include "platform/native_custom_offline.h"
#include "platform/native_custom_package.h"
#endif

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
	int wedges; // CTR_DEV_AUTOPILOT=3: in a row, near (wedgeX, wedgeZ)
	int grabbed; // Aku Aku has the kart: a fall counts as a wedge, so it does not repeat for ever
	int wedgeX, wedgeZ;
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
	const char *customPin; // CTR_DEV_CUSTOM
	int autopilot;
	int hopEvery;
	int autopilotLane;
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
	int warpHeldMS;
	u32 primPeak; // most primitive-arena bytes one frame used, printed at exit
	int primPeakLevel;
	u32 otPeak; // most instance ordering-table bytes one frame used
	int otPeakLevel;
	int profile;      // CTR_DEV_PROFILE, -1 if unset
	int profileToHub; // no race asked for: start in the profile's hub
} NativeDevRaceState;

// The run's save as the card holds it: struct MemcardProfile, padded, CRC last.
#define NATIVE_DEV_SLOTS_BYTES 0x1680
static union
{
	struct MemcardProfile mcp;
	u8 bytes[NATIVE_DEV_SLOTS_BYTES];
} s_devCard;

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
#ifdef CTR_CUSTOM_PACKAGES
		// CTR_DEV_CUSTOM=<pin>: an Arcade race on that installed package,
		// served through the custom pages' host slot.
		s_devRace.customPin = getenv("CTR_DEV_CUSTOM");
		if ((s_devRace.customPin != NULL) && (s_devRace.raceTrack < 0))
			s_devRace.raceTrack = CTR_CUSTOM_ARCADE_HOST_LEVEL;
#endif
		s_devRace.timeTrial = (mode != NULL) && (strcmp(mode, "tt") == 0);
		s_devRace.cup = (mode != NULL) && (strcmp(mode, "cup") == 0);
		s_devRace.adventure = s_devRace.cup || ((mode != NULL) && (strcmp(mode, "adv") == 0)) || (getenv("CTR_DEV_PROFILE") != NULL);
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
		// CTR_DEV_PROFILE=<0-3>: Adventure from that profile of the run's save
		// (memcards/slot0, written by tools/decrypted/ctr_save.py), as loading
		// it from the title would. With no race asked for, the run starts in
		// the hub the profile was saved in.
		s_devRace.profile = NativeDev_EnvInt("CTR_DEV_PROFILE", -1);
		if ((s_devRace.profile >= 0) && (s_devRace.profile < MEMCARD_ADV_PROFILE_COUNT))
		{
			u32 crc = 0;
			const int read = NativeMemcard_ReadSaveData("bu00:BASCUS-94426-SLOTS", s_devCard.bytes, NATIVE_DEV_SLOTS_BYTES, 0x100) == NATIVE_MEMCARD_OK;
			for (int i = 0; read && (i < NATIVE_DEV_SLOTS_BYTES + 2); i++)
				crc = MEMCARD_CRC16(crc, (i < NATIVE_DEV_SLOTS_BYTES) ? s_devCard.bytes[i] : 0);
			if (!read || ((crc & 0xffff) != 0) || (s_devCard.mcp.header[0] != MEMCARD_PROFILE_VERSION))
			{
				printf("[CTR Dev] CTR_DEV_PROFILE: no valid save in memcards/slot0\n");
				s_devRace.profile = -1;
			}
			else if (!s_devRace.cup && (s_devRace.raceTrack < 0))
			{
				const int hub = s_devCard.mcp.advProgress[s_devRace.profile].HubLevYouSavedOn;
				s_devRace.profileToHub = 1;
				s_devRace.raceTrack = ((hub >= GEM_STONE_VALLEY) && (hub <= CITADEL_CITY)) ? hub : N_SANITY_BEACH;
			}
		}
		else
			s_devRace.profile = -1;
		s_devRace.laps = NativeDev_EnvInt("CTR_DEV_LAPS", 0);
		s_devRace.autopilot = NativeDev_EnvInt("CTR_DEV_AUTOPILOT", 0);
		s_devRace.hopEvery = NativeDev_EnvInt("CTR_DEV_HOP_MS", 0);
		s_devRace.autopilotLane = NativeDev_EnvInt("CTR_DEV_AUTOPILOT_LANE", 0);
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

#ifdef CTR_CUSTOM_PACKAGES
// CTR_DEV_CUSTOM: acquire the installed package synchronously (the custom
// pages do it on a worker) and arm the offline loader for the host slot. A
// refusal is logged and the race runs on the host slot's retail track.
static void NativeDevRace_BeginCustom(struct GameTracker *gGT, const char *pin)
{
	char path[1024], error[256] = "";
	struct CustomPackageOwned *package = NULL;
	struct CustomOfflineRequest *request = NULL;
	const int pathLen = snprintf(path, sizeof path, "%s/tracks/packages/%.64s", NativeAssets_GetAssetDir(), pin);
	if ((pathLen < 0) || ((size_t)pathLen >= sizeof path))
		snprintf(error, sizeof error, "package path too long");
	if ((error[0] != 0) || !CustomPackage_AcquireDirectory(path, pin, &package, error, sizeof error) ||
		!CustomOffline_Prepare(&package, pin, &request, error, sizeof error) ||
		!CustomOffline_CheckStructure(request, CTR_OFFLINE_MODE_ARCADE, error, sizeof error) ||
		!CustomOffline_BeginRuntime(&request, CTR_CUSTOM_ARCADE_HOST_LEVEL, 0, CTR_OFFLINE_MODE_ARCADE))
	{
		Platform_Log("[CTR Dev] custom track %s refused: %s\n", pin, error);
		CustomOffline_Free(&request);
		CustomPackage_Free(&package);
		return;
	}
	gGT->numLaps = (char)CustomOffline_RuntimeLaps();
	Platform_Log("[CTR Dev] custom track %s, %d laps\n", pin, gGT->numLaps);
}
#endif

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
	// CTR_DEV_RACER: player 1 races as that installed custom racer (by
	// library order, docs/CUSTOM_RACERS.md), on its template's character ID.
	const int racer = NativeDev_EnvInt("CTR_DEV_RACER", -1);
	if ((racer >= 0) && (racer < NativeRacers_Count()))
	{
		character = NativeRacers_Template(racer);
		NativeRacers_SetPlayer(0, racer);
	}
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

	if (dev->profile >= 0)
	{
		struct MemcardProfile *mcp = &s_devCard.mcp;
		GAMEPROG_SyncGameAndCard(&mcp->gameSave.progress, &GAME_SAVE.progress);
		memcpy(&GAME_ADV_PROGRESS, &mcp->advProgress[dev->profile], sizeof(GAME_ADV_PROGRESS));
		if (CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, ADV_REWARD_BEAT_OXIDE_FIRST))
			UNLOCK_ADV_BIT(GAME_SAVE.progress.unlocks, GAME_UNLOCK_BIT_OXIDE);
		memcpy(gGT->prevNameEntered, GAME_ADV_PROGRESS.name, sizeof(gGT->prevNameEntered));
		const int racer = GAME_ADV_PROGRESS.characterID;
		if ((NativeDev_EnvInt("CTR_DEV_CHARACTER", -1) < 0) && (racer >= 0) && (racer < 16))
		{
			for (int i = 1; i < 8; i++)
				if (data.characterIDs[i] == racer)
					data.characterIDs[i] = data.characterIDs[0];
			data.characterIDs[0] = (s16)racer;
		}
		if (dev->profileToHub)
		{
			gGT->gameMode1 |= ADVENTURE_MODE;
			return dev->raceTrack;
		}
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
#ifdef CTR_CUSTOM_PACKAGES
		if (dev->customPin != NULL)
			NativeDevRace_BeginCustom(gGT, dev->customPin);
#endif
		if (dev->laps > 0)
			gGT->numLaps = (char)dev->laps;
		return dev->raceTrack;
	}

	// The same setup MM_TrackSelect does before the ghost menu.
	gGT->gameMode1 |= TIME_TRIAL;
	gGT->numLaps = 3;
	sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(0x3e00, "DevRace");
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
// secret racers in this run's save, =2 N. Oxide as well. With CTR_DEV_MODE=tt
// it is the Time Trial character select instead (returns 2).
int NativeDev_CharacterSelect(void)
{
	static int s_mode = -1;
	if (s_mode < 0)
		s_mode = NativeDev_EnvInt("CTR_DEV_CHARACTER_SELECT", 0);
	if (s_mode <= 0)
		return 0;
	const int unlocks = NativeDev_EnvInt("CTR_DEV_UNLOCKS", 0);
	for (int bit = 5; (unlocks >= 1) && (bit <= 11); bit++)
		UNLOCK_ADV_BIT(GAME_PROGRESS.unlocks, bit);
	if (unlocks >= 2)
		UNLOCK_ADV_BIT(GAME_PROGRESS.unlocks, GAME_UNLOCK_BIT_OXIDE);
	const int character = NativeDev_EnvInt("CTR_DEV_CHARACTER", -1);
	if ((character >= 0) && (character < 16))
		sdata->characterIDs_backup[0] = (s16)character;
	return NativeDevRace_State()->timeTrial ? 2 : 1;
}

int NativeDev_ModsTab(void)
{
	static int s_done;
	if (s_done)
		return 0;
	s_done = 1;
	return NativeDev_EnvInt("CTR_DEV_MODS", 0);
}

static int NativeDevRam_Pad(unsigned short *buttons, unsigned char *leftX);

#define AUTOPILOT_MOST_CUT 160 // level units the line may stray from a straight to the point aimed at

// Steer toward a nav point ahead on one of the AI's lines, with the throttle
// held, so the kart stays on the road through the real driving physics.
// Player 1 follows lane 0 (CTR_DEV_AUTOPILOT_LANE picks another); in VS each
// player takes the next lane, so on tracks with forks some players take the
// other branch.
int NativeDev_AutopilotPad(int player, unsigned short *buttons, unsigned char *leftX)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;

	if ((player == 0) && NativeDevRam_Pad(buttons, leftX))
		return 1;
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
	struct NavHeader *nh = sdata->NavPath_ptrHeader[(player + dev->autopilotLane) % 3];
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
	// A flight (VehFlight.c) ends far along the line.
	if ((p->navLevel != gGT->levelID) || (d->kartState == KS_FLIGHT))
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
		const int next = (best + k) % count;
		int dx = frames[next].pos.x - x, dz = frames[next].pos.z - z;

		// A straight line to a point round a bend leaves the road on the inside
		// of it: never aim where the line ahead strays from that straight.
		long long length = (long long)dx * dx + (long long)dz * dz;
		int cuts = 0;
		for (int m = 1; (m < k) && (length > 0); m++)
		{
			const int between = (best + m) % count;
			long long across = (long long)(frames[between].pos.x - x) * dz - (long long)(frames[between].pos.z - z) * dx;
			if (across * across > AUTOPILOT_MOST_CUT * AUTOPILOT_MOST_CUT * length)
			{
				cuts = 1;
				break;
			}
		}
		if (cuts && (k > 1))
			break;

		target = next;
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
			mx = x - p->wedgeX;
			mz = z - p->wedgeZ;
			if (mx * mx + mz * mz > 1000 * 1000)
				p->wedges = 0;
			p->wedgeX = x;
			p->wedgeZ = z;
			p->wedges++;
		}
	}

	// CTR_DEV_AUTOPILOT=3: wedged twice in one place, hop on along the line, so
	// a lap shows every place a kart gets stuck and not only the first. The
	// jump in the trace marks the place (tools/tracks/lap_check.py).
	if (d->kartState == KS_MASK_GRABBED)
	{
		if (!p->grabbed)
		{
			long long mx = x - p->wedgeX, mz = z - p->wedgeZ;
			if (mx * mx + mz * mz > 1000 * 1000)
				p->wedges = 0;
			p->wedgeX = x;
			p->wedgeZ = z;
			p->wedges++;
		}
		p->grabbed = 1;
	}
	else
		p->grabbed = 0;

	if ((dev->autopilot == 3) && (p->wedges >= 2) && (d->kartState != KS_MASK_GRABBED) && (d->kartState != KS_ENGINE_REVVING))
	{
		int to = (best + 6) % count;
		int next = (to + 1) % count;
		d->posCurr.x = d->posPrev.x = frames[to].pos.x << 8;
		d->posCurr.y = d->posPrev.y = (frames[to].pos.y + 32) << 8;
		d->posCurr.z = d->posPrev.z = frames[to].pos.z << 8;
		d->velocity.x = d->velocity.y = d->velocity.z = 0;
		d->xSpeed = d->ySpeed = d->zSpeed = 0;
		d->speed = d->speedApprox = 0;
		d->angle = (s16)(ratan2(frames[next].pos.x - frames[to].pos.x, frames[next].pos.z - frames[to].pos.z) & 0xfff);
		d->distanceToFinish_checkpoint = 0;
		p->navIndex = to;
		p->wedges = 0;
		p->reverseMS = 0;
		p->stuckX = frames[to].pos.x;
		p->stuckZ = frames[to].pos.z;
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
	else if ((dev->autopilot == 3) && (speedAbs > 6000) && ((err > 0x100) || (err < -0x100)))
		held = RAW_BTN_SQUARE; // a kart at full speed turns far wider than the corner
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
// world position and heading, at rest, for two seconds of game time while the
// camera settles, then let go: in a race that is before the lights at both
// frame rates, so 30 and 60 FPS runs start from rest at race time 0. Works in
// the adventure hubs too. The position of a player's F5 save is the kart
// Instance's matrix translation.
static void NativeDevRace_Warp(NativeDevRaceState *dev, struct GameTracker *gGT)
{
	if (!dev->warp || (gGT == NULL) || (sdata->Loading.stage != LOAD_IDLE) || (gGT->levelID == MAIN_MENU_LEVEL) || (gGT->drivers[0] == NULL))
	{
		dev->warpHeldMS = 0;
		return;
	}
	if (dev->warpHeldMS >= 2000)
		return;
	dev->warpHeldMS += gGT->elapsedTimeMS;

	struct Driver *d = gGT->drivers[0];
	d->posCurr.x = d->posPrev.x = dev->warpPos[0] << 8;
	d->posCurr.y = d->posPrev.y = dev->warpPos[1] << 8;
	d->posCurr.z = d->posPrev.z = dev->warpPos[2] << 8;
	d->velocity.x = d->velocity.y = d->velocity.z = 0;
	d->xSpeed = d->ySpeed = d->zSpeed = 0;
	d->speed = d->speedApprox = 0;
	d->angle = (s16)dev->warpPos[3];
	// In a race the mask-grab progress check would see the warp as a skipped
	// section and take the kart back: forget the last checkpoint progress, and
	// let the first quad touched become the respawn point.
	d->lastValid = NULL;
	d->distanceToFinish_checkpoint = 0;
}

// CTR_DEV_COLL_PROBE=<csv>: once a race's level is loaded, run the kart's own
// ground search (COLL_FIXED_PlayerSearch: same flags, probe span and BSP walk,
// without the previous-frame quadblock shortcut) straight down at every node of
// the level's three AI lanes and at lateral steps across them, and write one
// row per probe. A miss where the track has road is a hole a kart falls
// through; no driving needed to find it.
static void NativeDevRace_CollProbe(struct GameTracker *gGT)
{
	static int done;
	const char *path = getenv("CTR_DEV_COLL_PROBE");
	if (done || (path == NULL) || (gGT == NULL) || (gGT->level1 == NULL) || (sdata->Loading.stage != LOAD_IDLE) ||
		(gGT->level1->ptr_mesh_info == NULL) || (gGT->level1->LevNavTable == NULL))
		return;
	done = 1;
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f, "lane,node,offset,x,y,z,hit,hitY,flags,terrain,sphere\n");
	struct ScratchpadStruct *sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	int misses = 0, sphereMisses = 0, probes = 0;
	// CTR_DEV_COLL_PROBE_IN=<file>: probe these points ("x y z" per line, LEV units:
	// where the ground should be) instead of walking the AI lanes. Lane rows are
	// written as lane -1, node = line number.
	FILE *in = getenv("CTR_DEV_COLL_PROBE_IN") ? fopen(getenv("CTR_DEV_COLL_PROBE_IN"), "r") : NULL;
	for (int lane = (in != NULL) ? -1 : 0; lane < ((in != NULL) ? 0 : 3); lane++)
	{
		struct NavHeader *nh = (lane >= 0) ? gGT->level1->LevNavTable[lane] : NULL;
		if ((lane >= 0) && ((nh == NULL) || (nh->magicNumber != (s16)-0x1303)))
			continue;
		struct NavFrame *fr = (nh != NULL) ? NAVHEADER_GETFRAME(nh) : NULL;
		for (int n = 0; (in != NULL) || (n < nh->numPoints); n++)
		{
			int px, py, pz, dx = 0, dz = 0, len = 1;
			if (in != NULL)
			{
				if (fscanf(in, "%d %d %d", &px, &py, &pz) != 3)
					break;
			}
			else
			{
				struct NavFrame *a = &fr[n], *b = &fr[(n + 1) % nh->numPoints];
				px = a->pos.x, py = a->pos.y, pz = a->pos.z;
				dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;
				len = (int)sqrt((double)dx * dx + (double)dz * dz);
				if (len == 0)
					continue;
			}
			// on a lane: 64-unit steps across it, 1024 units each side
			for (int off = (in != NULL) ? 0 : -1024; off <= ((in != NULL) ? 0 : 1024); off += 64)
			{
				const s16 x = (s16)(px - dz * off / len), z = (s16)(pz + dx * off / len);
				// the span COLL_FIXED_PlayerSearch_SetupSearch gives a kart standing at py
				const SVec3 bottom = {.x = x, .y = (s16)(py - COLL_FIXED_PLAYER_SEARCH_BOTTOM_Y_OFFSET), .z = z};
				const SVec3 top = {.x = x, .y = (s16)(py + COLL_FIXED_PLAYER_SEARCH_TOP_Y_OFFSET), .z = z};
				sps->Union.QuadBlockColl.pos = top;
				sps->Input1.pos = bottom;
				sps->ptr_mesh_info = gGT->level1->ptr_mesh_info;
				sps->Union.QuadBlockColl.quadFlagsIgnored = QUADBLOCK_FLAG_NO_COLLISION_RESPONSE;
				sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE;
				sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
				sps->boolDidTouchQuadblock = 0;
				sps->boolDidTouchHitbox = 0;
				sps->numTrianglesTested = 0;
				sps->bbox.min.x = sps->bbox.max.x = x;
				sps->bbox.min.y = bottom.y;
				sps->bbox.max.y = top.y;
				sps->bbox.min.z = sps->bbox.max.z = z;
				sps->Union.QuadBlockColl.hitPos = bottom;
				COLL_SearchBSP_CallbackPARAM(sps->ptr_mesh_info->bspRoot, &sps->bbox, COLL_FIXED_BSPLEAF_TestQuadblocks, sps);
				const int hit = sps->boolDidTouchQuadblock != 0;
				const struct QuadBlock *q = hit ? sps->hit.ptrQuadblock : NULL;
				const int hitY = hit ? sps->Union.QuadBlockColl.hitPos.y : 0;
				const int flags = q ? q->quadFlags : 0, terrain = q ? q->terrain_type : 0;
				// The sphere sweep that stops a moving kart (COLL_MOVED_PlayerSearch's radius).
				sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE;
				sps->Union.QuadBlockColl.quadFlagsIgnored = 0;
				sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
				COLL_SearchBSP_CallbackQUADBLK(&bottom, &top, sps, COLL_MOVED_PLAYER_HIT_RADIUS);
				const int sphere = sps->boolDidTouchQuadblock != 0;
				fprintf(f, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", lane, n, off, x, py, z, hit, hitY, flags, terrain, sphere);
				probes++;
				misses += !hit;
				sphereMisses += !sphere;
			}
		}
	}
	if (in != NULL)
		fclose(in);
	fclose(f);
	Platform_Log("[CTR Dev] collision probe: %d probes, %d without ground under the line search, %d under the sphere sweep, written to %s\n",
		probes, misses, sphereMisses, path);
}

// CTR_DEV_MODEL_DUMP=<file>: append the models the running race has in
// gGT->modelPtr, once per level: id and name.
static void NativeDevRace_ModelDump(const struct GameTracker *gGT)
{
	static int dumpedLevel = -1;
	const char *path = getenv("CTR_DEV_MODEL_DUMP");
	if (path == NULL || *path == '\0' || gGT == NULL || dumpedLevel == (int)gGT->levelID)
		return;
	dumpedLevel = (int)gGT->levelID;
	FILE *f = fopen(path, "a");
	if (f == NULL)
		return;
	fprintf(f, "# level %d\n", (int)gGT->levelID);
	for (u32 i = 0; i < len(gGT->modelPtr); i++)
		if (gGT->modelPtr[i] != NULL)
			fprintf(f, "%u %s\n", i, gGT->modelPtr[i]->name);
	fclose(f);
}

// Once per presented frame, from NativeDev_EndFrame.
// The last ten seconds of player 1, kept in every build for an F7 capture: what a
// kart just did (a launch, a fall) is gone by the time a player can press the key.
#define NATIVE_DEV_KART_LOG_FRAMES 600

typedef struct
{
	int raceMS;
	int x, y, z;
	int speed;
	int vx, vy, vz;
	int reserves;
	int air;
	int kartState;
	int angle;
	int collisionFlags;
} NativeDevKartRow;

static NativeDevKartRow s_devKartLog[NATIVE_DEV_KART_LOG_FRAMES];
static unsigned int s_devKartLogCount;

static void NativeDevRace_LogKart(const struct Driver *d)
{
	NativeDevKartRow *row = &s_devKartLog[s_devKartLogCount++ % NATIVE_DEV_KART_LOG_FRAMES];

	row->raceMS = d->timeElapsedInRace;
	row->x = d->posCurr.x >> 8;
	row->y = d->posCurr.y >> 8;
	row->z = d->posCurr.z >> 8;
	row->speed = d->speedApprox;
	row->vx = d->velocity.x;
	row->vy = d->velocity.y;
	row->vz = d->velocity.z;
	row->reserves = d->reserves;
	row->air = (d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0;
	row->kartState = d->kartState;
	row->angle = d->angle & 0xfff;
	row->collisionFlags = d->collisionFlags;
}

void NativeDev_RaceWriteKartLog(const char *path)
{
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f, "raceMS,x,y,z,speed,vx,vy,vz,reserves,air,kart,angle,coll\n");
	const unsigned int count = s_devKartLogCount < NATIVE_DEV_KART_LOG_FRAMES ? s_devKartLogCount : NATIVE_DEV_KART_LOG_FRAMES;
	for (unsigned int i = s_devKartLogCount - count; i != s_devKartLogCount; i++)
	{
		const NativeDevKartRow *r = &s_devKartLog[i % NATIVE_DEV_KART_LOG_FRAMES];
		fprintf(f, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", r->raceMS, r->x, r->y, r->z, r->speed, r->vx, r->vy, r->vz, r->reserves, r->air, r->kartState,
		        r->angle, r->collisionFlags);
	}
	fclose(f);
}

// CTR_DEV_RAM=<log>: player 1 drives into each moving object of a converted track
// in turn, from the road behind it, and the log gets a line for each: what the
// object did to the kart (tools/tracks/ram_check.py). An object's own hit code
// reports what it did through NativeDev_MoverHit.
#define RAM_MOST 512
#define RAM_SETTLE_MS 1500 // of race time before the first, for every object to have a place
#define RAM_HOLD_MS 2500 // at rest first, past the grace a hit gives (Mover.c, MOVER_HIT_GRACE)
#define RAM_AFTER_MS 700 // driving on after a hit, to see what the kart does
#define RAM_DRIVE_MS 6000 // mud, or a hand that comes round, takes a while
#define RAM_BACK 500 // level units behind it along the road that the kart starts, past its own hit box
#define RAM_STAGGER_MS 900 // longer at rest each try: a Monty Mole is not met at the same point of its round again
#define RAM_PAST 300 // and past it that it has gone through
#define RAM_UP 40
#define RAM_TRIES 3
#define RAM_MOVING 2 // level units a frame: faster than that, the kart meets it head-on in its path

typedef struct
{
	int seen;
	int pos[3];
	int vel[3]; // from its last place, a frame
	int events;
} NativeDevRamTarget;

static NativeDevRamTarget s_ram[RAM_MOST];
static struct
{
	int loaded;
	FILE *log;
	const struct Level *level;
	int target; // the list being rammed, -1 none yet
	int phaseMS;
	int dir[2]; // the road's way at it, 4096 long
	int start[3]; // a point of the AI's line about back before it
	int back;     // RAM_BACK past its hit box
	int holdMS;   // RAM_HOLD_MS, and RAM_STAGGER_MS more each try
	int above;    // how far over the kart it was when the kart came nearest
	int worstState, nearest, passed;
	int anyEvents; // from any object while this one is rammed: a Pokey's lowest part hits for its upper ones
	int eventMS;   // when the first was
	int tries;     // a moving one is missed now and then: again, up to RAM_TRIES
	int done;
} s_ramRun;

static int NativeDevRam_On(void)
{
	if (!s_ramRun.loaded)
	{
		const char *path = getenv("CTR_DEV_RAM");
		s_ramRun.loaded = 1;
		s_ramRun.log = (path != NULL) ? fopen(path, "w") : NULL;
		s_ramRun.target = -1;
	}
	return s_ramRun.log != NULL;
}

void NativeDevRam_Mover(int list, int x, int y, int z)
{
	if (!NativeDevRam_On() || (list < 0) || (list >= RAM_MOST))
		return;
	if (s_ram[list].seen)
	{
		s_ram[list].vel[0] = x - s_ram[list].pos[0];
		s_ram[list].vel[1] = y - s_ram[list].pos[1];
		s_ram[list].vel[2] = z - s_ram[list].pos[2];
	}
	s_ram[list].seen = 1;
	s_ram[list].pos[0] = x;
	s_ram[list].pos[1] = y;
	s_ram[list].pos[2] = z;
}

void NativeDev_MoverHit(int list, int driver, int what)
{
	if ((driver == 0) && (list >= 0) && (list < RAM_MOST))
	{
		s_ram[list].events |= what;
		if (s_ramRun.anyEvents == 0)
			s_ramRun.eventMS = s_ramRun.phaseMS;
		s_ramRun.anyEvents |= what;
	}
}

// Whether that list's object does anything to a kart: a hit box (Mover.c's record 1),
// and a damage or the solid flag (record 0's z), not a floor that only carries karts.
static int NativeDevRam_Hits(const struct GameTracker *gGT, int list)
{
	const SVec3 *records;

	if ((gGT->level1 == NULL) || (list >= gGT->level1->numSpawnType2))
		return 0;
	records = gGT->level1->ptrSpawnType2[list].coords.positions;
	return (records[1].x != 0) && (((u16)records[0].z & 0x10ff) != 0);
}

// The road's heading at a place: the nearest point of the AI's first lane, to the next;
// and where the kart starts: the point of that lane about back before it, on the road.
static void NativeDevRam_RoadWay(const int pos[3], int dir[2])
{
	struct NavHeader *nh = sdata->NavPath_ptrHeader[0];
	dir[0] = 0;
	dir[1] = 4096;
	s_ramRun.start[0] = pos[0];
	s_ramRun.start[1] = pos[1];
	s_ramRun.start[2] = pos[2] - s_ramRun.back;
	if ((nh == NULL) || (nh->numPoints <= 1))
		return;
	struct NavFrame *frames = NAVHEADER_GETFRAME(nh);
	int best = 0;
	long long bestDist = -1;
	for (int i = 0; i < nh->numPoints; i++)
	{
		long long dx = frames[i].pos.x - pos[0], dy = frames[i].pos.y - pos[1], dz = frames[i].pos.z - pos[2];
		long long dist = dx * dx + dy * dy + dz * dz;
		if ((bestDist < 0) || (dist < bestDist))
		{
			bestDist = dist;
			best = i;
		}
	}
	for (int k = 1; k < nh->numPoints; k++)
	{
		const struct NavFrame *f = &frames[(best - k + nh->numPoints) % nh->numPoints];
		const double fx = f->pos.x - pos[0], fz = f->pos.z - pos[2];
		if ((fx * fx + fz * fz >= (double)s_ramRun.back * s_ramRun.back) || (k == nh->numPoints - 1))
		{
			s_ramRun.start[0] = f->pos.x;
			s_ramRun.start[1] = f->pos.y;
			s_ramRun.start[2] = f->pos.z;
			break;
		}
	}
	const struct NavFrame *a = &frames[best], *b = &frames[(best + 1) % nh->numPoints];
	const double dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z, length = sqrt(dx * dx + dz * dz);
	if (length > 0)
	{
		dir[0] = (int)(dx / length * 4096);
		dir[1] = (int)(dz / length * 4096);
	}
}

// A moving one is met head-on: the kart starts back ahead of it on its way,
// and "through" is past it along the line the kart drove.
static int NativeDevRam_HeadOn(const NativeDevRamTarget *t)
{
	const double vx = t->vel[0], vz = t->vel[2], speed = sqrt(vx * vx + vz * vz);
	if ((speed < RAM_MOVING) || (speed > s_ramRun.back))
		return 0;
	s_ramRun.start[0] = t->pos[0] + (int)(vx / speed * s_ramRun.back);
	s_ramRun.start[1] = t->pos[1];
	s_ramRun.start[2] = t->pos[2] + (int)(vz / speed * s_ramRun.back);
	s_ramRun.dir[0] = (int)(-vx / speed * 4096);
	s_ramRun.dir[1] = (int)(-vz / speed * 4096);
	return 1;
}

static void NativeDevRam_Report(int list)
{
	const NativeDevRamTarget *t = &s_ram[list];
	fprintf(s_ramRun.log, "ram %d events %d state %d nearest %d passed %d at %d %d %d any %d above %d\n", list, t->events, s_ramRun.worstState,
	        s_ramRun.nearest, s_ramRun.passed, t->pos[0], t->pos[1], t->pos[2], s_ramRun.anyEvents, s_ramRun.above);
	fflush(s_ramRun.log);
}

static void NativeDevRam_Frame(NativeDevRaceState *dev, struct GameTracker *gGT, struct Driver *d)
{
	if (!NativeDevRam_On() || s_ramRun.done || (d == NULL) || (gGT->trafficLightsTimer > 0) || (dev->raceClock < RAM_SETTLE_MS))
		return;

	if (s_ramRun.level != gGT->level1)
	{
		s_ramRun.level = gGT->level1;
		s_ramRun.target = -1;
	}

	s_ramRun.phaseMS += gGT->elapsedTimeMS;
	if (s_ramRun.target >= 0)
	{
		NativeDevRamTarget *t = &s_ram[s_ramRun.target];
		const int kx = d->posCurr.x >> 8, kz = d->posCurr.z >> 8;
		const int dx = kx - t->pos[0], dz = kz - t->pos[2];
		const int along = (dx * s_ramRun.dir[0] + dz * s_ramRun.dir[1]) >> 12;
		const int dist = (int)sqrt((double)dx * dx + (double)dz * dz);

		if (s_ramRun.phaseMS < s_ramRun.holdMS)
		{
			// at rest on the road behind it, or in its way, facing it
			NativeDevRam_HeadOn(t);
			d->posCurr.x = d->posPrev.x = s_ramRun.start[0] << 8;
			d->posCurr.y = d->posPrev.y = (s_ramRun.start[1] + RAM_UP) << 8;
			d->posCurr.z = d->posPrev.z = s_ramRun.start[2] << 8;
			d->velocity.x = d->velocity.y = d->velocity.z = 0;
			d->xSpeed = d->ySpeed = d->zSpeed = 0;
			d->speed = d->speedApprox = 0;
			d->angle = (s16)(ratan2(t->pos[0] - s_ramRun.start[0], t->pos[2] - s_ramRun.start[2]) & 0xfff);
			d->lastValid = NULL;
			d->distanceToFinish_checkpoint = 0;
			t->events = 0;
			s_ramRun.anyEvents = 0;
			s_ramRun.worstState = 0;
			s_ramRun.nearest = 0x7fffffff;
			s_ramRun.passed = 0;
			return;
		}

		if ((d->kartState != KS_NORMAL) && (d->kartState != KS_DRIFTING) && (d->kartState != KS_ENGINE_REVVING) && (s_ramRun.worstState == 0))
			s_ramRun.worstState = d->kartState;
		if (dist < s_ramRun.nearest)
		{
			s_ramRun.nearest = dist;
			s_ramRun.above = t->pos[1] - (d->posCurr.y >> 8);
		}
		if (along > RAM_PAST)
			s_ramRun.passed = 1;
		if ((s_ramRun.phaseMS < s_ramRun.holdMS + RAM_DRIVE_MS) && !s_ramRun.passed && (s_ramRun.anyEvents == 0))
			return;
		if ((s_ramRun.anyEvents != 0) && (s_ramRun.phaseMS < s_ramRun.eventMS + RAM_AFTER_MS))
			return;
		if ((s_ramRun.anyEvents == 0) && (++s_ramRun.tries < RAM_TRIES))
		{
			s_ramRun.phaseMS = 0;
			s_ramRun.holdMS += RAM_STAGGER_MS;
			NativeDevRam_RoadWay(t->pos, s_ramRun.dir);
			if (s_ramRun.tries == RAM_TRIES - 1)
			{
				// the AI's line may pass it on another level: straight back from it, at its height
				s_ramRun.start[0] = t->pos[0] - ((s_ramRun.back * s_ramRun.dir[0]) >> 12);
				s_ramRun.start[1] = t->pos[1];
				s_ramRun.start[2] = t->pos[2] - ((s_ramRun.back * s_ramRun.dir[1]) >> 12);
			}
			return;
		}
		NativeDevRam_Report(s_ramRun.target);
	}

	// the next object that can touch a kart
	int next = s_ramRun.target + 1;
	while ((next < RAM_MOST) && !(s_ram[next].seen && NativeDevRam_Hits(gGT, next)))
		next++;
	if (next >= RAM_MOST)
	{
		fprintf(s_ramRun.log, "done\n");
		fflush(s_ramRun.log);
		s_ramRun.done = 1;
		return;
	}
	s_ramRun.target = next;
	s_ramRun.phaseMS = 0;
	s_ramRun.tries = 0;
	s_ramRun.holdMS = RAM_HOLD_MS;
	{
		const SVec3 *half = &gGT->level1->ptrSpawnType2[next].coords.positions[1];
		s_ramRun.back = RAM_BACK + ((half->x > half->z) ? half->x : half->z);
	}
	NativeDevRam_RoadWay(s_ram[next].pos, s_ramRun.dir);
}

// While ramming, player 1 holds the throttle and steers at the object.
static int NativeDevRam_Pad(unsigned short *buttons, unsigned char *leftX)
{
	struct Driver *d = NativeDevRace_Player(0);
	if (!NativeDevRam_On() || s_ramRun.done || (s_ramRun.target < 0) || (d == NULL))
		return 0;
	const NativeDevRamTarget *t = &s_ram[s_ramRun.target];
	const int want = ratan2(t->pos[0] - (d->posCurr.x >> 8), t->pos[2] - (d->posCurr.z >> 8)) & 0xfff;
	const int err = ((want - (d->angle & 0xfff) + 0x800) & 0xfff) - 0x800;
	int steer = -err / 3;
	if (steer > 0)
		steer += 40;
	if (steer < 0)
		steer -= 40;
	if (steer > 127)
		steer = 127;
	if (steer < -127)
		steer = -127;
	*buttons = (unsigned short)~RAW_BTN_CROSS;
	*leftX = (unsigned char)(0x80 + steer);
	return 1;
}

void NativeDev_RaceFrame(void)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = NativeDevRace_Player(0);

	if (d != NULL)
		NativeDevRace_LogKart(d);
	if (d != NULL)
		NativeDevRace_CollProbe(gGT);
	if (d != NULL)
		NativeDevRace_ModelDump(gGT);

	// The trace and dumps also follow player 1 around an Adventure hub, where
	// nobody races (the autopilot and drive script stay race-only).
	if ((d == NULL) && (gGT != NULL) && (sdata->Loading.stage == LOAD_IDLE) && ((gGT->gameMode1 & ADVENTURE_ARENA) != 0) &&
	    ((u8)gGT->numPlyrCurrGame == 1))
		d = gGT->drivers[0];

	NativeDevRace_AdventureHub(dev, gGT);
	NativeDevRace_Warp(dev, gGT);

	// The frame just drawn is still in the back buffer's primitive arena.
	if ((gGT != NULL) && (gGT->backBuffer != NULL))
	{
		const struct PrimMem *pm = &gGT->backBuffer->primMem;
		const u32 used = (u32)((u8 *)pm->cursor - (u8 *)pm->start);
		if (used > dev->primPeak)
		{
			dev->primPeak = used;
			dev->primPeakLevel = gGT->levelID;
		}
		const struct OTMem *om = &gGT->backBuffer->otMem;
		const u32 otUsed = (u32)((u8 *)om->cursor - (u8 *)om->start);
		if (otUsed > dev->otPeak)
		{
			dev->otPeak = otUsed;
			dev->otPeakLevel = gGT->levelID;
		}
	}

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
	NativeDevRam_Frame(dev, gGT, d);

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
			fprintf(dev->trace, "frame,fps60,level,lights,end,raceMS,x,y,z,angle,speed,air,kart,lap,reserves,anim,animFrame,gx,gy,gz,gvis,nav,navTarget,want,steer,vx,vy,vz,coll,timer,fireCap,cx,cy,cz,crx,cry,crz,turn,rotW,camSpeed,pcx,pcy,pcz,pcrx,pcry,pcrz,spin,wobble,lerpVel,rotPrevW,drift,clock,base,fire,kx,ky,kz\n");
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
		fprintf(dev->trace, ",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d", ppos[0], ppos[1], ppos[2], prot[0], prot[1], prot[2], d->rotationSpinRate,
		        d->turnWobbleAngle, d->turnAngleLerpVel, d->rotPrev.w, d->KartStates.Drifting.numFramesDrifting, dev->raceClock, d->baseSpeed, d->fireSpeed);
		// The kart's position with its 8 fractional bits.
		fprintf(dev->trace, ",%.3f,%.3f,%.3f\n", d->posCurr.x / 256.0, d->posCurr.y / 256.0, d->posCurr.z / 256.0);
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
	Platform_Log("[CTR Dev] primitive arena peak %u of %u bytes (level %d)\n", s_devRace.primPeak, Platform_GetPrimArenaSize(), s_devRace.primPeakLevel);
	Platform_Log("[CTR Dev] instance ordering table peak %u of %u bytes (level %d), %u instances found it full\n", s_devRace.otPeak,
	             Platform_GetOTArenaSize(), s_devRace.otPeakLevel, gNativeOTRefused);
#ifdef CTR_CUSTOM_TRACKS
	CustomTrackDiag_FlushLevelLoad(); // the custom level's render report, normally written at the next level load
#endif
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

void NativeDev_RaceWriteWhere(const char *path)
{
	struct GameTracker *gGT = sdata->gGT;
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f, "version=%s\n", CTR_NATIVE_VERSION);
	if (gGT != NULL)
	{
		fprintf(f, "level=%d players=%d gameMode1=0x%08x\n", gGT->levelID, (u8)gGT->numPlyrCurrGame, (u32)gGT->gameMode1);
		struct Driver *d = gGT->drivers[0];
		if (d != NULL)
			fprintf(f, "CTR_DEV_WARP=%d,%d,%d,%d\n", d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8, d->angle);
		const struct PushBuffer *pb = &gGT->pushBuffer[0];
		fprintf(f, "camera pos=%d,%d,%d rot=%d,%d,%d\n", pb->pos.x, pb->pos.y, pb->pos.z, pb->rot.x, pb->rot.y, pb->rot.z);
	}
	fprintf(f, "camera_far=%d aspect_ratio=%d increase_draw_distance=%d render_scale=%d pgxp=%d window=%dx%d\n", g_config.cameraFar,
	        g_config.aspectRatio, g_config.increaseDrawDistance, g_config.renderScale, g_config.pgxp, g_config.windowWidth, g_config.windowHeight);
	fclose(f);
}
