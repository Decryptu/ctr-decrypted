#include "platform/native_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Unattended races for checking gameplay timing, mostly 60 FPS against 30:
// boot straight into a race, drive the player's kart through the normal
// physics with a scripted pad, and trace it. See docs/DEV-TOOLS.md.
// Part of the main.c unity build, after the game code, so it reads game state
// directly.

typedef struct
{
	int loaded;
	int raceTrack;
	int timeTrial;
	int laps;
	int autopilot;
	int hopEvery;
	int exitAfterRace;
	int endFrames;
	const char *ghostIn;
	const char *ghostOut;
	const char *tracePath;
	const char *resultsPath;
	int resultsMS;
	int resultsWritten;
	int raceClock;
	FILE *trace;
	int ghostWritten;
	int navIndex;
	int navLevel;
	int navTarget;
	int lastRaceMS;
	int stuckMS;
	int stuckX, stuckZ;
	int reverseMS;
	int wantAngle;
	int steer;
	int raceFrame;
} NativeDevRaceState;

static NativeDevRaceState s_devRace;

static NativeDevRaceState *NativeDevRace_State(void)
{
	if (!s_devRace.loaded)
	{
		const char *mode = getenv("CTR_DEV_MODE");
		s_devRace.loaded = 1;
		s_devRace.raceTrack = NativeDev_EnvInt("CTR_DEV_RACE", -1);
		s_devRace.timeTrial = (mode != NULL) && (strcmp(mode, "tt") == 0);
		s_devRace.laps = NativeDev_EnvInt("CTR_DEV_LAPS", 0);
		s_devRace.autopilot = NativeDev_EnvInt("CTR_DEV_AUTOPILOT", 0);
		s_devRace.hopEvery = NativeDev_EnvInt("CTR_DEV_HOP_MS", 0);
		s_devRace.exitAfterRace = NativeDev_EnvInt("CTR_DEV_EXIT_AFTER_RACE", 0);
		s_devRace.ghostIn = getenv("CTR_DEV_GHOST_IN");
		s_devRace.ghostOut = getenv("CTR_DEV_GHOST_OUT");
		s_devRace.tracePath = getenv("CTR_DEV_TRACE");
		s_devRace.resultsPath = getenv("CTR_DEV_RESULTS");
		s_devRace.resultsMS = NativeDev_EnvInt("CTR_DEV_RESULTS_MS", 30000);
		s_devRace.navLevel = -1;
	}
	return &s_devRace;
}

int NativeDev_RaceTrack(void) { return NativeDevRace_State()->raceTrack; }

int NativeDev_StartRace(struct GameTracker *gGT)
{
	NativeDevRaceState *dev = NativeDevRace_State();

	gGT->boolDemoMode = 0;
	gGT->numPlyrNextGame = 1;
	for (int i = 0; i < 8; i++)
		data.characterIDs[i] = i;

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

static struct Driver *NativeDevRace_Player(void)
{
	struct GameTracker *gGT = sdata->gGT;
	if ((gGT == NULL) || (sdata->Loading.stage != LOAD_IDLE) || (gGT->levelID == MAIN_MENU_LEVEL))
		return NULL;
	if ((gGT->gameMode1 & (ARCADE_MODE | TIME_TRIAL)) == 0)
		return NULL;
	return gGT->drivers[0];
}

// Steer toward a nav point about one lane-width-and-a-half ahead on lane 0,
// with the throttle held. Lane 0 is the line the AI drives, so the kart stays
// on the road through the real driving physics.
int NativeDev_AutopilotPad(unsigned short *buttons, unsigned char *leftX)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;

	if (!dev->autopilot || ((d = NativeDevRace_Player()) == NULL) || ((gGT->gameMode1 & END_OF_RACE) != 0))
		return 0;

	struct NavHeader *nh = sdata->NavPath_ptrHeader[0];
	if ((nh == NULL) || (nh->numPoints <= 0))
		return 0;

	struct NavFrame *frames = NAVHEADER_GETFRAME(nh);
	const int count = nh->numPoints;
	const int x = d->posCurr.x >> 8, y = d->posCurr.y >> 8, z = d->posCurr.z >> 8;

	// Nearest node: a full search on a new level, then a window around the
	// last one so a lap boundary or a crossing section cannot make it jump.
	int from = dev->navIndex - 8, span = 40;
	if (dev->navLevel != gGT->levelID)
	{
		dev->navLevel = gGT->levelID;
		from = 0;
		span = count;
	}
	int best = dev->navIndex;
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
	dev->navIndex = best;

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
	int dt = raceMS - dev->lastRaceMS;
	dev->lastRaceMS = raceMS;
	if ((dt > 0) && (dt < 100) && (dev->reverseMS <= 0))
	{
		long long mx = x - dev->stuckX, mz = z - dev->stuckZ;
		if (mx * mx + mz * mz > 300 * 300)
		{
			dev->stuckX = x;
			dev->stuckZ = z;
			dev->stuckMS = 0;
		}
		else
			dev->stuckMS += dt;
		if (dev->stuckMS > 1500)
		{
			dev->stuckMS = 0;
			dev->reverseMS = 1000;
		}
	}

	// The d-pad too on a hard turn: analog alone steers too gently.
	unsigned short held = (dev->autopilot == 2) ? 0 : RAW_BTN_CROSS;
	if (dev->autopilot == 2)
		steer = 0;
	else if (dev->reverseMS > 0)
	{
		if (dt > 0)
			dev->reverseMS -= dt;
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

	dev->navTarget = target;
	dev->wantAngle = want;
	dev->steer = steer;
	*buttons = (unsigned short)~held;
	*leftX = (unsigned char)(0x80 + steer);
	return 1;
}

// Once per presented frame, from NativeDev_EndFrame.
void NativeDev_RaceFrame(void)
{
	NativeDevRaceState *dev = NativeDevRace_State();
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = NativeDevRace_Player();

	if (d == NULL)
	{
		dev->raceFrame = 0;
		dev->raceClock = 0;
		return;
	}

	// Race time from the lights, kept here because AI drivers (the attract
	// demo's player 1) do not count their own timeElapsedInRace.
	if (gGT->trafficLightsTimer <= 0)
		dev->raceClock += gGT->elapsedTimeMS;

	if ((dev->tracePath != NULL) && (dev->trace == NULL))
	{
		dev->trace = fopen(dev->tracePath, "w");
		if (dev->trace != NULL)
			fprintf(dev->trace, "frame,fps60,level,lights,end,raceMS,x,y,z,angle,speed,air,kart,lap,reserves,anim,animFrame,gx,gy,gz,gvis,nav,navTarget,want,steer,vx,vy,vz,coll,timer\n");
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
		fprintf(dev->trace, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", dev->raceFrame, CTR_NATIVE_60FPS_ACTIVE ? 1 : 0,
		        gGT->levelID, gGT->trafficLightsTimer, (gGT->gameMode1 & END_OF_RACE) != 0, d->timeElapsedInRace, d->posCurr.x >> 8,
		        d->posCurr.y >> 8, d->posCurr.z >> 8, d->angle & 0xfff, d->speedApprox, (d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0,
		        d->kartState, d->lapIndex, d->reserves, d->instSelf ? d->instSelf->animIndex : -1, d->instSelf ? d->instSelf->animFrame : -1, gx,
		        gy, gz, gvis, dev->navIndex, dev->navTarget, dev->wantAngle, dev->steer, d->velocity.x,
		        d->velocity.y, d->velocity.z, (int)d->collisionFlags, gGT->timer);
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
}
