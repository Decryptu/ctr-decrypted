#include <common.h>

// Movers: what moves in a Mario Kart DS course, as level instances with a thread
// (docs/TRACK-CONVERTER.md#movers). Each InstDef holds its record list's index in
// unk28 and CONVERTED_MOVER_ID in modelID. The lists are the level's
// ptrSpawnType2 entries, written by tools/tracks/lev_patch.py:
//
//   0  kind, speed in level units a second, damage (RB_Hazard_HurtDriver) | flags
//   1  half the hit box, level units, in the mover's own axes; x 0: it hits nothing
//   2  a, b, c, by kind (below)
//   3-5  its placed rotation, rows, 4096 is one
//   6  the middle of the hit box, from the origin, in its own axes
//   7+   MOVER_PATH: the path; MOVER_HOP: one record, the way it goes at its peak
//
// a, b, c: PATH: mode, first point, 0. SPIN: axis, 4096ths of a turn a second,
// less than 0 the other way round; with a record after the fixed ones, it turns
// for that many ticks, speeds up and slows down over y ticks, stops for z, and
// turns back, as MKDS's cogs do.
// SWING: axis, period in ms, angle at its peak. HOP: period in ms, profile, phase
// in ms. ORBIT: radius, less than 0 the other way round, period in ms, phase in ms.
// PULSE: period in ms, how long it is out, phase in ms: it grows out of its origin
// and hits only then, as a flame jet.

#if defined(CTR_NATIVE)
#include <math.h>

void NativeDev_TrackMover(int list, int x, int y, int z);

enum
{
	MOVER_PATH,
	MOVER_SPIN,
	MOVER_SWING,
	MOVER_HOP,
	MOVER_ORBIT,
	MOVER_PULSE,

	MOVER_PATH_LOOP = 0,
	MOVER_PATH_BACK_AND_FORTH = 1,
	MOVER_PATH_RESTART = 2,

	MOVER_HOP_ARC = 0,  // up and down, as a fish leaps
	MOVER_HOP_SLAM = 1, // waits at its peak, falls fast, rises slowly: a Thwomp

	MOVER_FLAG_FACE_PATH = 0x100,
	MOVER_FLAG_ROLL = 0x200,
	MOVER_FLAG_UPRIGHT = 0x400,  // an ORBIT stands in its own x, y plane, a hoop
	MOVER_FLAG_OUT_ONLY = 0x800, // a HOP hits only in its upper half: it hides under the ground
	MOVER_FLAG_SOLID = 0x1000,   // a kart stops at its box, and cannot be caught inside
	MOVER_FLAG_CARRIES = 0x2000, // a floor: a kart on it turns with it
	MOVER_FLAG_ONE_WAY = 0x4000, // a SPIN's cycle does not turn back
	MOVER_MOST_LISTS = 512,
	MOVER_DAMAGE_MASK = 0xff,

	MOVER_RECORD_MIDDLE = 6,
	MOVER_RECORD_POINTS = 7,
	MOVER_KART_RADIUS = 48,
	MOVER_TURN_MS = 250,
	MOVER_HIT_SOUND = 0x71,
	MOVER_HIT_GRACE = 120,	// frames: a kart just hit is let through, or what turns would carry it round for good
	MOVER_LEVEL_AXIS = 2048, // an axis this far from the vertical pushes a kart along the ground
	MOVER_HALF_OUT = 2048,
	MOVER_FLAME_GROW = 550,
	MOVER_FLAME_SHRINK = 1667,
	MOVER_ON_FLOOR = 96, // level units over a turning floor's top that a kart rides it
	MOVER_TICK_MS_X3 = 50, // three ticks of the game's objects
};

struct ConvertedMover
{
	s32 list;
	s32 segment;
	s32 direction;
	float along;
	u32 timeMS;
	float faceX, faceZ;
	s32 rollAngle;
	s32 homeX, homeY, homeZ; // a shot: how far its path is from where it was fired
};

CTR_STATIC_ASSERT(sizeof(struct ConvertedMover) <= 0x48);

static const SVec3 *Mover_Records(const struct ConvertedMover *mover)
{
	return GAME_TRACKER->level1->ptrSpawnType2[mover->list].coords.positions;
}

// Where each mover is, for a shot to start from the cannon that fires it.
static s32 s_moverAt[MOVER_MOST_LISTS][3];

static int Mover_NumPoints(const struct ConvertedMover *mover)
{
	return GAME_TRACKER->level1->ptrSpawnType2[mover->list].numCoords - MOVER_RECORD_POINTS;
}

static void Mover_Identity(s32 m[3][3])
{
	memset(m, 0, sizeof(s32) * 9);
	m[0][0] = m[1][1] = m[2][2] = 4096;
}

static void Mover_Multiply(s32 out[3][3], s32 a[3][3], s32 b[3][3])
{
	s32 r[3][3];

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			r[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]) >> 12;
		}
	}
	memcpy(out, r, sizeof(r));
}

// A turn round the mover's own x, y or z axis; 4096 is a full turn.
static void Mover_AxisTurn(s32 m[3][3], int axis, s32 angle)
{
	const int a = (axis + 1) % 3, b = (axis + 2) % 3;
	const s32 s = MATH_Sin(angle), c = MATH_Cos(angle);

	Mover_Identity(m);
	m[a][a] = c;
	m[a][b] = -s;
	m[b][a] = s;
	m[b][b] = c;
}

static void Mover_Base(const SVec3 *records, s32 m[3][3])
{
	for (int row = 0; row < 3; row++)
	{
		m[row][0] = records[3 + row].x;
		m[row][1] = records[3 + row].y;
		m[row][2] = records[3 + row].z;
	}
}

static void Mover_Store(struct Instance *inst, s32 m[3][3])
{
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			inst->matrix.m[i][j] = (s16)m[i][j];
		}
	}
}

static float Mover_SegmentLength(const SVec3 *a, const SVec3 *b)
{
	const float dx = (float)b->x - a->x, dy = (float)b->y - a->y, dz = (float)b->z - a->z;

	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void Mover_Walk(struct ConvertedMover *mover, const SVec3 *points, int count, int mode, float distance)
{
	while (distance > 0)
	{
		int next = mover->segment + mover->direction;
		float length;

		if ((next < 0) || (next >= count))
		{
			if (mode == MOVER_PATH_LOOP)
			{
				next = (next + count) % count;
			}
			else if (mode == MOVER_PATH_BACK_AND_FORTH)
			{
				mover->direction = -mover->direction;
				next = mover->segment + mover->direction;
			}
			else
			{
				mover->segment = 0;
				mover->along = 0;
				mover->faceX = 0; // it starts again facing the way it goes, at once
				mover->faceZ = 0;
				return;
			}
		}

		length = Mover_SegmentLength(&points[mover->segment], &points[next]);
		if (mover->along + distance < length)
		{
			mover->along += distance;
			return;
		}

		distance -= length - mover->along;
		mover->along = 0;
		mover->segment = next;
	}
}

// The speed of a cog in its cycle, 0 to 1, and which way: speeds up, turns, slows
// down, waits, then the same the other way.
static float Mover_CycleSpeed(u32 timeMS, const SVec3 *cycle, int oneWay)
{
	const u32 cruise = cycle->x * MOVER_TICK_MS_X3 / 3, ramp = cycle->y * MOVER_TICK_MS_X3 / 3, pause = cycle->z * MOVER_TICK_MS_X3 / 3;
	const u32 length = ramp + cruise + ramp + pause;
	const u32 number = timeMS / length, at = timeMS % length;
	const float way = ((number & 1) && !oneWay) ? -1.0f : 1.0f;

	if (at < ramp)
	{
		return way * at / ramp;
	}
	if (at < ramp + cruise)
	{
		return way;
	}
	if (at < ramp + cruise + ramp)
	{
		return way * (ramp + cruise + ramp - at) / ramp;
	}
	return 0;
}

// A kart on a turning floor goes round with it.
static void Mover_Carry(const struct Instance *inst, const SVec3 *records, s32 turn)
{
	const s32 s = MATH_Sin(turn), c = MATH_Cos(turn);
	const float reach = (float)records[1].x + MOVER_KART_RADIUS;

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = GAME_TRACKER->drivers[i];
		s32 dx, dy, dz;

		if (driver == NULL)
		{
			continue;
		}

		dx = driver->instSelf->matrix.t[0] - inst->matrix.t[0];
		dy = driver->instSelf->matrix.t[1] - inst->matrix.t[1];
		dz = driver->instSelf->matrix.t[2] - inst->matrix.t[2];
		if ((abs(dy) < MOVER_ON_FLOOR) && ((float)dx * dx + (float)dz * dz < reach * reach))
		{
			driver->posCurr.x += ((dx * c + dz * s) >> 12) - dx << 8;
			driver->posCurr.z += ((dz * c - dx * s) >> 12) - dz << 8;
			driver->angle += turn;
		}
	}
}

static void Mover_FollowPath(struct Instance *inst, struct ConvertedMover *mover, const SVec3 *records, s32 m[3][3], s32 dt)
{
	const SVec3 *points = &records[MOVER_RECORD_POINTS];
	const int count = Mover_NumPoints(mover);
	const int mode = records[2].x;
	const int flags = (u16)records[0].z;
	const float distance = (float)records[0].y * dt / 1000.0f;
	int next;
	float length, part;
	const SVec3 *from, *to;

	Mover_Walk(mover, points, count, mode, distance);
	next = mover->segment + mover->direction;
	if ((next < 0) || (next >= count))
	{
		next = (mode == MOVER_PATH_LOOP) ? ((next + count) % count) : mover->segment;
	}

	from = &points[mover->segment];
	to = &points[next];
	length = Mover_SegmentLength(from, to);
	part = (length > 0) ? (mover->along / length) : 0;
	inst->matrix.t[0] = (s32)(from->x + ((float)to->x - from->x) * part);
	inst->matrix.t[1] = (s32)(from->y + ((float)to->y - from->y) * part);
	inst->matrix.t[2] = (s32)(from->z + ((float)to->z - from->z) * part);
	if (records[2].z != 0)
	{
		// a shot: its path starts at the cannon's place when it was fired
		const s32 *cannon = s_moverAt[records[2].z - 1];

		if (mover->segment < mover->rollAngle)
		{
			mover->homeX = cannon[0] - points[0].x;
			mover->homeZ = cannon[2] - points[0].z;
		}
		mover->rollAngle = mover->segment;
		inst->matrix.t[0] += mover->homeX;
		inst->matrix.t[2] += mover->homeZ;
	}

	Mover_Base(records, m);
	if ((flags & MOVER_FLAG_FACE_PATH) && (length > 0))
	{
		const float wantX = ((float)to->x - from->x) / length;
		const float wantZ = ((float)to->z - from->z) / length;
		const float share = (dt < MOVER_TURN_MS) ? ((float)dt / MOVER_TURN_MS) : 1.0f;
		float size;

		mover->faceX += (wantX - mover->faceX) * share;
		mover->faceZ += (wantZ - mover->faceZ) * share;
		size = sqrtf(mover->faceX * mover->faceX + mover->faceZ * mover->faceZ);
		if (size > 0.001f)
		{
			const s32 fx = (s32)(mover->faceX / size * 4096), fz = (s32)(mover->faceZ / size * 4096);

			Mover_Identity(m);
			m[0][0] = fz;
			m[0][2] = fx;
			m[2][0] = -fx;
			m[2][2] = fz;
		}
	}

	if (flags & MOVER_FLAG_ROLL)
	{
		s32 roll[3][3];
		const s32 radius = records[1].y ? records[1].y : 64;

		mover->rollAngle = (mover->rollAngle + (s32)(distance * 4096 / (6.283f * radius))) & 0xfff;
		Mover_AxisTurn(roll, 0, mover->rollAngle);
		Mover_Multiply(m, m, roll);
	}
}

static s32 Mover_HopProfile(u32 time, int period, int profile)
{
	const s32 phase = (s32)(time % (u32)period) * 4096 / period;

	if (profile == MOVER_HOP_SLAM)
	{
		// away for half the time, a tenth to come home, a tenth there, the rest to go back
		if (phase < 2048)
		{
			return 4096;
		}
		if (phase < 2458)
		{
			return 4096 - (phase - 2048) * 10;
		}
		if (phase < 2867)
		{
			return 0;
		}
		return (phase - 2867) * 4096 / (4096 - 2867);
	}

	return MATH_Sin(phase / 2);
}

// CTR_DEV_MOVER_LOG: which kinds of mover hit a kart.
static void Mover_LogHit(const SVec3 *records)
{
	const char *path = getenv("CTR_DEV_MOVER_LOG");
	FILE *file = (path != NULL) ? fopen(path, "a") : NULL;

	if (file != NULL)
	{
		fprintf(file, "hit %d\n", records[0].x);
		fclose(file);
	}
}

// Out of a solid mover's box, along whichever level axis of it is the shortest way.
static void Mover_PushOut(struct Driver *driver, s32 m[3][3], const s32 local[3], const s32 half[3])
{
	int best = -1;
	s32 shortest = 0x7fffffff, push = 0;

	for (int axis = 0; axis < 3; axis++)
	{
		const s32 way = (local[axis] >= 0) ? (half[axis] - local[axis]) : (-half[axis] - local[axis]);
		const s32 length = (way >= 0) ? way : -way;

		if ((abs(m[1][axis]) < MOVER_LEVEL_AXIS) && (length < shortest))
		{
			best = axis;
			shortest = length;
			push = way;
		}
	}

	if (best >= 0)
	{
		driver->posCurr.x += ((push * m[0][best]) >> 12) << 8;
		driver->posCurr.z += ((push * m[2][best]) >> 12) << 8;
	}
}

static int s_moverGraceUntil[8];

static void Mover_HitKarts(struct Instance *inst, const SVec3 *records, s32 m[3][3])
{
	const int damage = records[0].z & MOVER_DAMAGE_MASK;

	if ((records[1].x == 0) || ((damage == 0) && !((u16)records[0].z & MOVER_FLAG_SOLID)))
	{
		return;
	}

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = GAME_TRACKER->drivers[i];
		s32 dx, dy, dz, local[3], half[3];
		int wasSpinning, inside = 1;

		if ((driver == NULL) || (GAME_TRACKER->framesInThisLEV < s_moverGraceUntil[i]))
		{
			continue;
		}

		dx = driver->instSelf->matrix.t[0] - inst->matrix.t[0];
		dy = driver->instSelf->matrix.t[1] - inst->matrix.t[1];
		dz = driver->instSelf->matrix.t[2] - inst->matrix.t[2];
		for (int axis = 0; axis < 3; axis++)
		{
			const SVec3 *middle = &records[MOVER_RECORD_MIDDLE];
			const s32 centre = (axis == 0) ? middle->x : (axis == 1) ? middle->y : middle->z;

			half[axis] = ((axis == 0) ? records[1].x : (axis == 1) ? records[1].y : records[1].z) + MOVER_KART_RADIUS;
			local[axis] = ((dx * m[0][axis] + dy * m[1][axis] + dz * m[2][axis]) >> 12) - centre;
			inside = inside && (local[axis] > -half[axis]) && (local[axis] < half[axis]);
		}

		if (!inside)
		{
			continue;
		}

		if ((u16)records[0].z & MOVER_FLAG_SOLID)
		{
			Mover_PushOut(driver, m, local, half);
		}

		if (damage == 0)
		{
			continue;
		}

		wasSpinning = driver->kartState == KS_SPINNING;
		if (RB_Hazard_HurtDriver(driver, damage, 0, 0) && !wasSpinning)
		{
			s_moverGraceUntil[i] = GAME_TRACKER->framesInThisLEV + MOVER_HIT_GRACE;
			Mover_LogHit(records);
			if ((driver->actionsFlagSet & ACTION_BOT) == 0)
			{
				OtherFX_Play_Echo(MOVER_HIT_SOUND, 1, (driver->actionsFlagSet & ACTION_ENGINE_ECHO) != 0);
			}
		}
	}
}

// CTR_DEV_MOVER_LOG=<file>: each mover's place and turn every 300 frames, for
// tools/tracks/mover_check.py.
static void Mover_Log(const struct ConvertedMover *mover, const struct Instance *inst, const SVec3 *records)
{
	const char *path = getenv("CTR_DEV_MOVER_LOG");

	if ((path != NULL) && ((GAME_TRACKER->framesInThisLEV % 293) == 0))
	{
		FILE *file = fopen(path, "a");

		if (file != NULL)
		{
			fprintf(file, "%d %d %d %d %d %d %d %d\n", mover->list, records[0].x, inst->matrix.t[0], inst->matrix.t[1], inst->matrix.t[2], inst->matrix.m[0][0],
			        inst->matrix.m[1][1], inst->scale.y);
			fclose(file);
		}
	}
}

// tools/tracks/mover_view.py: CTR_DEV_MOVERS_ONLY draws no level, so nothing hides a
// mover, and CTR_DEV_HIDE_MOVERS draws no mover, for the frame to compare with.
static void Mover_DevView(struct Instance *inst)
{
	if (getenv("CTR_DEV_MOVERS_ONLY") != NULL)
	{
		GAME_TRACKER->renderFlags &= ~RENDER_FLAG_DRAW_LEVEL;
	}
	if (getenv("CTR_DEV_HIDE_MOVERS") != NULL)
	{
		inst->flags |= HIDE_MODEL;
	}
}

static void ConvertedMover_ThTick(struct Thread *t)
{
	struct ConvertedMover *mover = t->object;
	struct Instance *inst = t->inst;
	const SVec3 *records = Mover_Records(mover);
	const s32 dt = GAME_TRACKER->elapsedTimeMS;
	s32 m[3][3], turn[3][3];
	int hidden = 0;

	mover->timeMS += dt;
	Mover_Base(records, m);

	switch (records[0].x)
	{
	case MOVER_PATH:
		Mover_FollowPath(inst, mover, records, m, dt);
		break;

	case MOVER_SPIN:
	{
		const float speed = (Mover_NumPoints(mover) > 0) ? Mover_CycleSpeed(mover->timeMS, &records[MOVER_RECORD_POINTS], ((u16)records[0].z & MOVER_FLAG_ONE_WAY) != 0) : 1.0f;
		const s32 before = mover->rollAngle;

		mover->along += speed * records[2].y * dt / 1000.0f;
		mover->rollAngle = (s32)mover->along;
		Mover_AxisTurn(turn, records[2].x, mover->rollAngle & 0xfff);
		Mover_Multiply(m, m, turn);
		if (((u16)records[0].z & MOVER_FLAG_CARRIES) && (mover->rollAngle != before))
		{
			Mover_Carry(inst, records, mover->rollAngle - before);
		}
		break;
	}

	case MOVER_SWING:
		if (records[2].y == 0)
		{
			// a flipper: rest, stroke out over 29 ticks and back, wait
			const SVec3 *stroke = &records[MOVER_RECORD_POINTS];
			const u32 out = 29 * MOVER_TICK_MS_X3 / 3, wait = stroke->z * MOVER_TICK_MS_X3 / 3;
			const u32 at = mover->timeMS % (out + out + wait);
			const s32 share = (at < out) ? (s32)(at * 4096 / out) : (at < out + out) ? (s32)((out + out - at) * 4096 / out) : 0;

			Mover_AxisTurn(turn, records[2].x, stroke->x - ((stroke->y * share) >> 12));
		}
		else
		{
			Mover_AxisTurn(turn, records[2].x, (MATH_Sin((s32)(mover->timeMS % (u32)records[2].y) * 4096 / records[2].y) * records[2].z) >> 12);
		}
		Mover_Multiply(m, m, turn);
		break;

	case MOVER_HOP:
	{
		const SVec3 *way = &records[MOVER_RECORD_POINTS];
		const s32 share = Mover_HopProfile(mover->timeMS + records[2].z, records[2].x, records[2].y);

		inst->matrix.t[0] = mover->homeX + ((way->x * share) >> 12);
		inst->matrix.t[1] = mover->homeY + ((way->y * share) >> 12);
		inst->matrix.t[2] = mover->homeZ + ((way->z * share) >> 12);
		hidden = ((u16)records[0].z & MOVER_FLAG_OUT_ONLY) && (share < MOVER_HALF_OUT);
		break;
	}

	case MOVER_ORBIT:
	{
		const s32 angle = (s32)((mover->timeMS + records[2].z) % (u32)records[2].y) * 4096 / records[2].y;
		const s32 across = (records[2].x * MATH_Sin(angle)) >> 12, along = (records[2].x * MATH_Cos(angle)) >> 12;

		if ((u16)records[0].z & MOVER_FLAG_UPRIGHT)
		{
			inst->matrix.t[0] = mover->homeX + ((m[0][0] * across + m[0][1] * along) >> 12);
			inst->matrix.t[1] = mover->homeY + ((m[1][0] * across + m[1][1] * along) >> 12);
			inst->matrix.t[2] = mover->homeZ + ((m[2][0] * across + m[2][1] * along) >> 12);
			Mover_Identity(m); // a picture: the game turns it to the camera
		}
		else
		{
			inst->matrix.t[0] = mover->homeX + across;
			inst->matrix.t[2] = mover->homeZ + along;
		}
		break;
	}
	}

	Mover_Store(inst, m);
	Mover_Log(mover, inst, records);
	NativeDev_TrackMover(mover->list, inst->matrix.t[0], inst->matrix.t[1], inst->matrix.t[2]);
	if (mover->list < MOVER_MOST_LISTS)
	{
		s_moverAt[mover->list][0] = inst->matrix.t[0];
		s_moverAt[mover->list][1] = inst->matrix.t[1];
		s_moverAt[mover->list][2] = inst->matrix.t[2];
	}
	Mover_DevView(inst);
	if (records[0].x == MOVER_PULSE)
	{
		const u32 phase = (records[2].x != 0) ? ((mover->timeMS + (u32)((u16)records[2].x - records[2].z)) % (u16)records[2].x) : 0;
		const int out = phase < (u32)records[2].y;

		// grows over 33 ticks, shrinks over 100 after its time
		const u32 on = records[2].y, off = (u32)records[2].x - on;
		float share = 0;

		if (records[2].x == 0)
		{
			share = 1;
		}
		else if (phase < MOVER_FLAME_GROW)
		{
			share = (float)phase / MOVER_FLAME_GROW;
		}
		else if (out)
		{
			share = 1;
		}
		else if (phase - on < MOVER_FLAME_SHRINK)
		{
			share = 1 - (float)(phase - on) / MOVER_FLAME_SHRINK;
		}
		(void)off;
		inst->scale.y = (s16)(inst->instDef->scale.y * share);
		if (share < 0.5f)
		{
			return;
		}
	}
	if (!hidden)
	{
		Mover_HitKarts(inst, records, m);
	}
}

void ConvertedMover_LInB(struct Instance *inst)
{
	struct ConvertedMover *mover;
	struct Thread *t;
	const SVec3 *records;

	if (inst->thread != NULL)
	{
		return;
	}

	t = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(sizeof(struct ConvertedMover), NONE, SMALL, STATIC), ConvertedMover_ThTick, "mover", NULL);
	inst->thread = t;
	if (t == NULL)
	{
		const char *path = getenv("CTR_DEV_MOVER_LOG");
		FILE *file = (path != NULL) ? fopen(path, "a") : NULL;

		if (file != NULL)
		{
			fprintf(file, "unborn %d\n", inst->instDef->unk28);
			fclose(file);
		}
		return;
	}

	mover = t->object;
	t->inst = inst;
	memset(mover, 0, sizeof(struct ConvertedMover));
	mover->list = inst->instDef->unk28;
	mover->direction = 1;
	mover->homeX = inst->matrix.t[0];
	mover->homeY = inst->matrix.t[1];
	mover->homeZ = inst->matrix.t[2];

	records = Mover_Records(mover);
	if (records[0].x == MOVER_PATH)
	{
		if (records[2].z != 0)
		{
			mover->homeX = mover->homeZ = 0;
		}
		mover->segment = records[2].y;
		mover->faceX = (float)records[3].z / 4096;
		mover->faceZ = (float)records[5].z / 4096;
	}
}
#endif
