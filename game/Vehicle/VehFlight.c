#include <common.h>
#include <platform/native_draw_distance.h>

// Flights: what a converted track carries a kart over and CTR's physics cannot,
// such as a cannon's shot, a jump longer than a kart flies, or a loop, which needs
// a gravity that follows the road (docs/TRACK-CONVERTER.md#flights).
//
// The level's ptrSpawnType2_PosRot holds one list per flight, written by
// tools/tracks/lev_patch.py. Record 0 is the gate: pos.x and pos.y are how far
// from the first point, sideways and in height, a kart may cross it, pos.z the
// speed in level units a second (0: the kart's own). The other records are the
// path: pos, and rot as the kart's up direction scaled by 4096. A gate is the
// plane through the first point, square to the path, crossed going forward, so
// a fast kart cannot pass it between frames and a kart going back never fires it.
//
// CPU karts have no flight: their lanes run along the same paths.

#if defined(CTR_NATIVE)
#include <math.h>

enum
{
	VEH_FLIGHT_MIN_SPEED = 2500,       // level units a second: enough to round a loop
	VEH_FLIGHT_JOIN_DISTANCE = 500,    // a kart joins the path this far on, so one at the road's edge drifts in
	VEH_FLIGHT_UPRIGHT = 2867,         // up.y, 4096 is one: at or above it the kart's heading follows the path
	VEH_FLIGHT_MAX_KARTS = 8,
	VEH_FLIGHT_SPEED_TO_UNITS = 30,    // Driver.speed is 1/256 of a level unit per 30th of a second
};

static struct
{
	int x, z, level;
} s_flightLast[VEH_FLIGHT_MAX_KARTS];

static const struct SpawnPosRot *VehFlight_Records(const struct Level *level, int flight)
{
	return level->ptrSpawnType2_PosRot[flight].coords.posRot;
}

static int VehFlight_LastRecord(const struct Level *level, int flight)
{
	return level->ptrSpawnType2_PosRot[flight].numCoords - 1;
}

void VehFlight_CheckGates(struct Driver *d)
{
	const struct Level *level = GAME_TRACKER->level1;
	const int id = d->driverID & (VEH_FLIGHT_MAX_KARTS - 1);
	const int x = d->posCurr.x >> 8, y = d->posCurr.y >> 8, z = d->posCurr.z >> 8;
	const int fromX = s_flightLast[id].x, fromZ = s_flightLast[id].z;
	const int fresh = s_flightLast[id].level == GAME_TRACKER->levelID + 1;

	s_flightLast[id].x = x;
	s_flightLast[id].z = z;
	s_flightLast[id].level = GAME_TRACKER->levelID + 1;

	if (!fresh || (d->funcPtrs[DRIVER_FUNC_INIT] != NULL) || ((d->kartState != KS_NORMAL) && (d->kartState != KS_DRIFTING)))
	{
		return;
	}

	for (int flight = 0; flight < level->numSpawnType2_PosRot; flight++)
	{
		const struct SpawnPosRot *r = VehFlight_Records(level, flight);
		const float wayX = (float)(r[2].pos.x - r[1].pos.x), wayZ = (float)(r[2].pos.z - r[1].pos.z);
		const float length = sqrtf(wayX * wayX + wayZ * wayZ);
		const float before = (fromX - r[1].pos.x) * wayX + (fromZ - r[1].pos.z) * wayZ;
		const float after = (x - r[1].pos.x) * wayX + (z - r[1].pos.z) * wayZ;

		if ((length <= 0) || (before >= 0) || (after < 0))
		{
			continue;
		}
		if ((fabsf((x - r[1].pos.x) * wayZ - (z - r[1].pos.z) * wayX) / length > r[0].pos.x) || (abs(y - r[1].pos.y) > r[0].pos.y))
		{
			continue;
		}

		d->KartStates.Flight.flight = (s16)flight;
		d->funcPtrs[DRIVER_FUNC_INIT] = VehFlight_Init;
		return;
	}
}

// Faces the kart along the path: to is the point it heads for, along how far it is
// from the point before, 0 to 1, for the up direction between the two.
static void VehFlight_Face(struct Driver *d, const struct SpawnPosRot *from, const struct SpawnPosRot *to, float along)
{
	float forward[3], up[3], right[3];
	forward[0] = (float)((to->pos.x << 8) - d->posCurr.x);
	forward[1] = (float)((to->pos.y << 8) - d->posCurr.y);
	forward[2] = (float)((to->pos.z << 8) - d->posCurr.z);
	up[0] = from->rot.x + (to->rot.x - from->rot.x) * along;
	up[1] = from->rot.y + (to->rot.y - from->rot.y) * along;
	up[2] = from->rot.z + (to->rot.z - from->rot.z) * along;

	float length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	if (length <= 0)
	{
		return;
	}
	for (int i = 0; i < 3; i++)
	{
		forward[i] /= length;
	}
	const float onForward = up[0] * forward[0] + up[1] * forward[1] + up[2] * forward[2];
	for (int i = 0; i < 3; i++)
	{
		up[i] -= forward[i] * onForward;
	}
	length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
	if (length <= 0)
	{
		return;
	}
	for (int i = 0; i < 3; i++)
	{
		up[i] /= length;
	}
	right[0] = up[1] * forward[2] - up[2] * forward[1];
	right[1] = up[2] * forward[0] - up[0] * forward[2];
	right[2] = up[0] * forward[1] - up[1] * forward[0];

	for (int i = 0; i < 3; i++)
	{
		d->matrixFacingDir.m[i][0] = (s16)(right[i] * 4096.0f);
		d->matrixFacingDir.m[i][1] = (s16)(up[i] * 4096.0f);
		d->matrixFacingDir.m[i][2] = (s16)(forward[i] * 4096.0f);
		d->KartStates.Flight.forward[i] = (s16)(forward[i] * 4096.0f);
	}
	d->matrixMovingDir = d->matrixFacingDir;

	// Round a loop the kart's heading stays, or the camera would swing about at its top.
	if (up[1] * 4096.0f >= VEH_FLIGHT_UPRIGHT)
	{
		const s16 heading = (s16)(ratan2((int)(forward[0] * 4096.0f), (int)(forward[2] * 4096.0f)) & 0xfff);
		d->angle = heading;
		d->rotCurr.y = heading;
		d->rotPrev.y = heading;
	}
	d->AxisAngle1_normalVec.x = d->AxisAngle2_normalVec.x = (s16)(up[0] * 4096.0f);
	d->AxisAngle1_normalVec.y = d->AxisAngle2_normalVec.y = (s16)(up[1] * 4096.0f);
	d->AxisAngle1_normalVec.z = d->AxisAngle2_normalVec.z = (s16)(up[2] * 4096.0f);
}

static void VehFlight_End(struct Driver *d)
{
	const s16 *forward = d->KartStates.Flight.forward;
	const int speed = d->KartStates.Flight.speed / VEH_FLIGHT_SPEED_TO_UNITS;

	// The kart lands far along the lap, and that is no shortcut: the ground under it
	// sets its progress afresh (COLL_FIXED_PlayerSearch_CheckMaskGrabProgress).
	d->distanceToFinish_checkpoint = 0;
	d->lastValid = NULL;
	d->posPrev = d->posCurr;
	d->velocity.x = forward[0] * speed >> 4;
	// A path ends on the road, where it may still climb: the kart settles on it, it is not thrown.
	d->velocity.y = (forward[1] > 0) ? 0 : (forward[1] * speed >> 4);
	d->velocity.z = forward[2] * speed >> 4;
	d->AxisAngle1_normalVec.x = d->AxisAngle2_normalVec.x = 0;
	d->AxisAngle1_normalVec.y = d->AxisAngle2_normalVec.y = 0x1000;
	d->AxisAngle1_normalVec.z = d->AxisAngle2_normalVec.z = 0;
	d->funcPtrs[DRIVER_FUNC_INIT] = VehPhysProc_Driving_Init;
}

void VehFlight_PhysLinear(struct Thread *t, struct Driver *d)
{
	(void)t;
	const struct Level *level = GAME_TRACKER->level1;
	const int flight = d->KartStates.Flight.flight;
	const struct SpawnPosRot *r = VehFlight_Records(level, flight);
	const int last = VehFlight_LastRecord(level, flight);
	float step = d->KartStates.Flight.speed * GAME_TRACKER->elapsedTimeMS / 1000.0f * 256.0f;

	if (d->funcPtrs[DRIVER_FUNC_INIT] != NULL)
	{
		return;
	}

	for (;;)
	{
		const int point = d->KartStates.Flight.point;
		const float toX = (float)((r[point].pos.x << 8) - d->posCurr.x);
		const float toY = (float)((r[point].pos.y << 8) - d->posCurr.y);
		const float toZ = (float)((r[point].pos.z << 8) - d->posCurr.z);
		const float distance = sqrtf(toX * toX + toY * toY + toZ * toZ);

		if (distance > step)
		{
			d->posCurr.x += (int)(toX * step / distance);
			d->posCurr.y += (int)(toY * step / distance);
			d->posCurr.z += (int)(toZ * step / distance);
			VehFlight_Face(d, &r[point - 1], &r[point], 1.0f - distance / d->KartStates.Flight.segment);
			return;
		}

		d->posCurr.x = r[point].pos.x << 8;
		d->posCurr.y = r[point].pos.y << 8;
		d->posCurr.z = r[point].pos.z << 8;
		step -= distance;

		if (point == last)
		{
			VehFlight_End(d);
			return;
		}

		d->KartStates.Flight.point = (s16)(point + 1);
		const float nextX = (float)(r[point + 1].pos.x - r[point].pos.x);
		const float nextY = (float)(r[point + 1].pos.y - r[point].pos.y);
		const float nextZ = (float)(r[point + 1].pos.z - r[point].pos.z);
		d->KartStates.Flight.segment = sqrtf(nextX * nextX + nextY * nextY + nextZ * nextZ) * 256.0f;
	}
}

void VehFlight_TranslateMatrix(struct Thread *t, struct Driver *d)
{
	struct Instance *inst = t->inst;

	d->posPrev = d->posCurr;
	inst->matrix = d->matrixFacingDir;
	inst->matrix.t[0] = d->posCurr.x >> 8;
	inst->matrix.t[1] = d->posCurr.y >> 8;
	inst->matrix.t[2] = d->posCurr.z >> 8;
}

void VehFlight_Init(struct Thread *t, struct Driver *d)
{
	const struct Level *level = GAME_TRACKER->level1;
	const int flight = d->KartStates.Flight.flight;
	const struct SpawnPosRot *r = VehFlight_Records(level, flight);
	const int last = VehFlight_LastRecord(level, flight);
	const int own = abs(d->speedApprox) * VEH_FLIGHT_SPEED_TO_UNITS / 256;
	int point = 2;
	(void)t;

	for (; point < last; point++)
	{
		const float x = (float)(r[point].pos.x - (d->posCurr.x >> 8)), y = (float)(r[point].pos.y - (d->posCurr.y >> 8));
		const float z = (float)(r[point].pos.z - (d->posCurr.z >> 8));
		if (x * x + y * y + z * z > VEH_FLIGHT_JOIN_DISTANCE * VEH_FLIGHT_JOIN_DISTANCE)
		{
			break;
		}
	}

	const float toX = (float)((r[point].pos.x << 8) - d->posCurr.x), toY = (float)((r[point].pos.y << 8) - d->posCurr.y);
	const float toZ = (float)((r[point].pos.z << 8) - d->posCurr.z);

	d->kartState = KS_FLIGHT;
	d->KartStates.Flight.point = (s16)point;
	d->KartStates.Flight.segment = sqrtf(toX * toX + toY * toY + toZ * toZ);
	d->KartStates.Flight.speed = (r[0].pos.z != 0) ? r[0].pos.z : ((own < VEH_FLIGHT_MIN_SPEED) ? VEH_FLIGHT_MIN_SPEED : own);
	d->actionsFlagSet = (d->actionsFlagSet & ~ACTION_TOUCH_GROUND) | ACTION_AIRBORNE;
	d->matrixArray = BAKED_GTE_MATRIX_NONE;
	d->matrixIndex = 0;

	d->funcPtrs[DRIVER_FUNC_UPDATE] = NULL;
	d->funcPtrs[DRIVER_FUNC_PHYS_LINEAR] = VehFlight_PhysLinear;
	d->funcPtrs[DRIVER_FUNC_AUDIO] = VehPhysProc_Driving_Audio;
	d->funcPtrs[DRIVER_FUNC_PHYS_ANGULAR] = NULL;
	d->funcPtrs[DRIVER_FUNC_APPLY_FORCES] = NULL;
	d->funcPtrs[DRIVER_FUNC_COLL_MOVED] = NULL;
	d->funcPtrs[DRIVER_FUNC_COLLIDE_DRIVERS] = NULL;
	d->funcPtrs[DRIVER_FUNC_COLL_FIXED] = NULL;
	d->funcPtrs[DRIVER_FUNC_JUMP_FRICTION] = NULL;
	d->funcPtrs[DRIVER_FUNC_TRANSLATE_MATRIX] = VehFlight_TranslateMatrix;
	d->funcPtrs[DRIVER_FUNC_INIT] = NULL;
}
#endif
