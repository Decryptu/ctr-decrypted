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
// a, b, c: PATH: mode, first point, 0. SPIN: axis, 4096ths of a turn a second.
// SWING: axis, period in ms, angle at its peak. HOP: period in ms, profile, phase
// in ms. ORBIT: radius, period in ms, phase in ms. PULSE: period in ms, how long it
// is out, phase in ms: it grows out of its origin and hits only then, as a flame jet.

#if defined(CTR_NATIVE)
#include <math.h>

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
	MOVER_DAMAGE_MASK = 0xff,

	MOVER_RECORD_MIDDLE = 6,
	MOVER_RECORD_POINTS = 7,
	MOVER_KART_RADIUS = 48,
	MOVER_TURN_MS = 250,
	MOVER_HIT_SOUND = 0x71,
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
	s32 homeX, homeY, homeZ;
};

CTR_STATIC_ASSERT(sizeof(struct ConvertedMover) <= 0x48);

static const SVec3 *Mover_Records(const struct ConvertedMover *mover)
{
	return GAME_TRACKER->level1->ptrSpawnType2[mover->list].coords.positions;
}

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

static void Mover_HitKarts(struct Instance *inst, const SVec3 *records, s32 m[3][3])
{
	const int damage = records[0].z & MOVER_DAMAGE_MASK;

	if ((records[1].x == 0) || (damage == 0))
	{
		return;
	}

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = GAME_TRACKER->drivers[i];
		s32 dx, dy, dz;
		int wasSpinning, inside = 1;

		if (driver == NULL)
		{
			continue;
		}

		dx = driver->instSelf->matrix.t[0] - inst->matrix.t[0];
		dy = driver->instSelf->matrix.t[1] - inst->matrix.t[1];
		dz = driver->instSelf->matrix.t[2] - inst->matrix.t[2];
		for (int axis = 0; axis < 3; axis++)
		{
			const s32 half = ((axis == 0) ? records[1].x : (axis == 1) ? records[1].y : records[1].z) + MOVER_KART_RADIUS;
			const SVec3 *middle = &records[MOVER_RECORD_MIDDLE];
			const s32 centre = (axis == 0) ? middle->x : (axis == 1) ? middle->y : middle->z;
			const s32 local = ((dx * m[0][axis] + dy * m[1][axis] + dz * m[2][axis]) >> 12) - centre;

			inside = inside && (local > -half) && (local < half);
		}

		if (!inside)
		{
			continue;
		}

		wasSpinning = driver->kartState == KS_SPINNING;
		if (RB_Hazard_HurtDriver(driver, damage, 0, 0) && !wasSpinning && ((driver->actionsFlagSet & ACTION_BOT) == 0))
		{
			OtherFX_Play_Echo(MOVER_HIT_SOUND, 1, (driver->actionsFlagSet & ACTION_ENGINE_ECHO) != 0);
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

static void ConvertedMover_ThTick(struct Thread *t)
{
	struct ConvertedMover *mover = t->object;
	struct Instance *inst = t->inst;
	const SVec3 *records = Mover_Records(mover);
	const s32 dt = GAME_TRACKER->elapsedTimeMS;
	s32 m[3][3], turn[3][3];

	mover->timeMS += dt;
	Mover_Base(records, m);

	switch (records[0].x)
	{
	case MOVER_PATH:
		Mover_FollowPath(inst, mover, records, m, dt);
		break;

	case MOVER_SPIN:
		Mover_AxisTurn(turn, records[2].x, (s32)(((u64)mover->timeMS * records[2].y / 1000) & 0xfff));
		Mover_Multiply(m, m, turn);
		break;

	case MOVER_SWING:
		Mover_AxisTurn(turn, records[2].x, (MATH_Sin((s32)(mover->timeMS % (u32)records[2].y) * 4096 / records[2].y) * records[2].z) >> 12);
		Mover_Multiply(m, m, turn);
		break;

	case MOVER_HOP:
	{
		const SVec3 *way = &records[MOVER_RECORD_POINTS];
		const s32 share = Mover_HopProfile(mover->timeMS + records[2].z, records[2].x, records[2].y);

		inst->matrix.t[0] = mover->homeX + ((way->x * share) >> 12);
		inst->matrix.t[1] = mover->homeY + ((way->y * share) >> 12);
		inst->matrix.t[2] = mover->homeZ + ((way->z * share) >> 12);
		break;
	}

	case MOVER_ORBIT:
	{
		const s32 angle = (s32)((mover->timeMS + records[2].z) % (u32)records[2].y) * 4096 / records[2].y;

		inst->matrix.t[0] = mover->homeX + ((records[2].x * MATH_Sin(angle)) >> 12);
		inst->matrix.t[2] = mover->homeZ + ((records[2].x * MATH_Cos(angle)) >> 12);
		break;
	}
	}

	Mover_Store(inst, m);
	Mover_Log(mover, inst, records);
	if (records[0].x == MOVER_PULSE)
	{
		const u32 phase = (mover->timeMS + records[2].z) % (u32)records[2].x;
		const int out = phase < (u32)records[2].y;

		inst->scale.y = (s16)(out ? inst->instDef->scale.y : 0);
		if (!out)
		{
			return;
		}
	}
	Mover_HitKarts(inst, records, m);
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
		mover->segment = records[2].y;
		mover->faceX = (float)records[3].z / 4096;
		mover->faceZ = (float)records[5].z / 4096;
	}
}
#endif
