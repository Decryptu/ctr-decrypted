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
// less than 0 the other way round, and for a rim a kart rides how level the rim must be
// under it to carry it (4096 is level); with a record after the fixed ones, it turns
// for that many ticks, speeds up and slows down over y ticks, stops for z, and
// turns back, as MKDS's cogs do.
// SWING: axis, period in ms, angle at its peak. HOP: period in ms, 0, phase in ms. ORBIT: radius, period in ms, phase in ms, in its own x, y plane.
// PULSE: period in ms, how long it is out, phase in ms: it grows out of its origin
// and hits only then, as a flame jet.
// KEYS: where it is over time, worked out by tools/tracks/mkds_motion.py from the
// object's update code. Speed: ms a key. a: the key it runs on from after the last,
// b: laps added to a gate's each time it comes round, c: the key it starts at.
// Record 7: its scale grows by x (4096 is its placed size) a second up to y; z gates.
// Then two records a gate: the key that waits, how near a kart must come, the lap the
// race must reach; the point it must come near. Then the keys; MOVER_KEY_HIDDEN in y:
// it is not there.
// STILL: stays where it is placed, and only hits.

#if defined(CTR_NATIVE)
#include <math.h>

void NativeDev_TrackMover(int list, int x, int y, int z);
void NativeDev_MoverHit(int list, int driver, int what);

enum
{
	MOVER_EVENT_PUSH = 1,
	MOVER_EVENT_HURT = 2,
	MOVER_EVENT_CRATE = 4,
};

enum
{
	MOVER_PATH,
	MOVER_SPIN,
	MOVER_SWING,
	MOVER_HOP,
	MOVER_ORBIT,
	MOVER_PULSE,
	MOVER_KEYS,
	MOVER_STILL,

	MOVER_PATH_LOOP = 0,
	MOVER_PATH_BACK_AND_FORTH = 1,
	MOVER_PATH_RESTART = 2,

	MOVER_FLAG_FACE_PATH = 0x100,
	MOVER_FLAG_ROLL = 0x200,
	MOVER_FLAG_SOLID = 0x1000,   // a kart stops at its box, and cannot be caught inside
	MOVER_FLAG_CARRIES = 0x2000, // a floor: a kart on it turns with it
	MOVER_FLAG_ONE_WAY = 0x4000, // a SPIN's cycle does not turn back
	MOVER_FLAG_DROPS = 0x8000,   // KEYS: hurts only when it drops, and is merely solid otherwise
	MOVER_ITEM_BOX = 4,          // in place of a damage: it breaks as a weapon crate does
	MOVER_MOST_LISTS = 512,
	MOVER_DAMAGE_MASK = 0xff,

	MOVER_RECORD_MIDDLE = 6,
	MOVER_RECORD_POINTS = 7,
	MOVER_RECORD_GATES = 8,
	MOVER_KEY_HIDDEN = -32768,
	MOVER_DROP_KEY = 64,         // level units a key: what is slower does not crush
	MOVER_SCALE_ONE = 4096,
	MOVER_KART_RADIUS = 48,
	MOVER_TURN_MS = 250,
	MOVER_HIT_SOUND = 0x71,
	MOVER_HIT_GRACE = 120,	// frames: a kart just hit is let through, or what turns would carry it round for good
	MOVER_LEVEL_AXIS = 2048, // an axis this far from the vertical pushes a kart along the ground
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
	s32 hitShare;            // of its hit box, 4096 is as placed; 0 is the same
	s32 harmless;            // this frame it hurts nothing
};

// A thread's object comes from the small pool, which refuses one this big or bigger.
CTR_STATIC_ASSERT(sizeof(struct ConvertedMover) < THREAD_SMALL_STACK_SIZE - sizeof(struct Item));

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

// A kart on a turning floor goes round with it, about the axis it turns on. Where that
// axis stands up, the floor is the flat top (a cog, Bowser Castle's room) and the kart
// turns with it too; where it lies, the floor is the rim (a wheel, a rolling log).
static void Mover_Carry(const struct Instance *inst, const SVec3 *records, s32 m[3][3], s32 turn)
{
	const int axis = records[2].x, a = (axis + 1) % 3, b = (axis + 2) % 3;
	const float radius = (float)records[1].x, length = (float)records[1].y;
	const float ax = m[0][axis] / 4096.0f, ay = m[1][axis] / 4096.0f, az = m[2][axis] / 4096.0f;
	const int upright = abs(m[1][axis]) >= MOVER_LEVEL_AXIS;
	const float s = MATH_Sin(turn) / 4096.0f, c = MATH_Cos(turn) / 4096.0f;

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = GAME_TRACKER->drivers[i];
		float d[3], along, across, turned[3];

		if (driver == NULL)
		{
			continue;
		}

		d[0] = (float)(driver->instSelf->matrix.t[0] - inst->matrix.t[0]);
		d[1] = (float)(driver->instSelf->matrix.t[1] - inst->matrix.t[1]);
		d[2] = (float)(driver->instSelf->matrix.t[2] - inst->matrix.t[2]);
		along = d[0] * ax + d[1] * ay + d[2] * az;
		{
			const float la = (d[0] * m[0][a] + d[1] * m[1][a] + d[2] * m[2][a]) / 4096.0f;
			const float lb = (d[0] * m[0][b] + d[1] * m[1][b] + d[2] * m[2][b]) / 4096.0f;
			across = sqrtf(la * la + lb * lb);
		}
		// a rim runs from the origin (the model's top face) through the thickness: measured
		// from its middle, as the hit box is, and only a kart over it, not one beside it on
		// the road (MKDS 0x20e0764: the flat faces are walls)
		const SVec3 *middle = &records[MOVER_RECORD_MIDDLE];
		const float rim = along - (float)((axis == 0) ? middle->x : (axis == 1) ? middle->y : middle->z);
		// how level the rim is under the kart: the up part of the way out from the axis.
		// MKDS lets a kart go where that falls under the object's limit (records[2].z).
		const float level = (across > 0.0f) ? (d[1] - ay * along) / across : 0.0f;

		if (upright ? ((fabsf(along) > MOVER_ON_FLOOR) || (across > radius + MOVER_KART_RADIUS))
		            : ((fabsf(across - radius) > MOVER_ON_FLOOR) || (fabsf(rim) > length) || (level * 4096.0f < (float)records[2].z) || (d[1] < 0)))
		{
			continue;
		}

		// d turned about the axis (Rodrigues)
		{
			const float cx = ay * d[2] - az * d[1], cy = az * d[0] - ax * d[2], cz = ax * d[1] - ay * d[0];
			turned[0] = d[0] * c + cx * s + ax * along * (1 - c);
			turned[1] = d[1] * c + cy * s + ay * along * (1 - c);
			turned[2] = d[2] * c + cz * s + az * along * (1 - c);
		}
		{
			const s32 dx = (s32)((turned[0] - d[0]) * 256), dz = (s32)((turned[2] - d[2]) * 256);
			const s32 dy = upright ? 0 : (s32)((turned[1] - d[1]) * 256);

			// a player's kart is carried through its sweep, so walls stop it
			// (COLL_MOVED_AddCarry); a CPU kart keeps to its path and is placed
			if (driver->actionsFlagSet & ACTION_BOT)
			{
				driver->posCurr.x += dx;
				driver->posCurr.y += dy;
				driver->posCurr.z += dz;
			}
			else
			{
				COLL_MOVED_AddCarry(driver, dx, dy, dz);
			}
		}
		if (upright)
		{
			driver->angle += (ay > 0) ? turn : -turn;
		}
	}
}

// Turns towards the way it goes, over MOVER_TURN_MS.
static void Mover_Face(struct ConvertedMover *mover, int flags, s32 m[3][3], float wantX, float wantZ, s32 dt)
{
	const float share = (dt < MOVER_TURN_MS) ? ((float)dt / MOVER_TURN_MS) : 1.0f;
	float size;

	if (!(flags & MOVER_FLAG_FACE_PATH))
	{
		return;
	}

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

static void Mover_Roll(struct ConvertedMover *mover, const SVec3 *records, int flags, s32 m[3][3], float distance)
{
	if (flags & MOVER_FLAG_ROLL)
	{
		s32 roll[3][3];
		const s32 radius = records[1].y ? records[1].y : 64;

		mover->rollAngle = (mover->rollAngle + (s32)(distance * 4096 / (6.283f * radius))) & 0xfff;
		Mover_AxisTurn(roll, 0, mover->rollAngle);
		Mover_Multiply(m, m, roll);
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
	if (length > 0)
	{
		Mover_Face(mover, flags, m, ((float)to->x - from->x) / length, ((float)to->z - from->z) / length, dt);
	}
	Mover_Roll(mover, records, flags, m, distance);
}

// A model too big for one instance is in pieces (tools/tracks/mkds_movers.py, build_model),
// an instance each on the same list. InstDef _pad_scale is 1 on every piece but the first,
// which alone has a thread: it moves, tests karts and carries them, and puts the others
// where they are drawn. InstDef rot is where a piece is from the object's origin, in its
// own axes.

// A mover's polygons go into the frame's ordering table each at its own depth
// (RenderBucket_SpreadsOverOT), the middle of the polygon, and a kart, which does
// not test depth, at its own middle. The level's floor goes in at each quad's
// farthest corner, behind what stands on it. A floor a kart rides (MOVER_FLAG_CARRIES)
// goes this many slots (64 units) back too, as much as its polygons' far corners
// (mkds_movers.PIECE_EDGE, 300 level units) lie behind their middles: else the cog
// a kart stands on is drawn over its wheels.
#define MOVER_FLOOR_DEPTH_BIAS 4

struct MoverPiece
{
	struct Instance *inst;
	struct MoverPiece *next;
};

static struct MoverPiece *s_moverPieceHead[MOVER_MOST_LISTS];
static struct MoverPiece *s_moverPieceTail[MOVER_MOST_LISTS];
static struct MoverPiece *s_moverPieceFree;
static int s_moverPieceRemaining;

static int Mover_DrawnOnly(const struct Instance *inst)
{
	return inst->instDef->_pad_scale != 0;
}

static void Mover_AddPiece(struct Instance *inst)
{
	const int list = inst->instDef->unk28;

	if ((list < 0) || (list >= MOVER_MOST_LISTS))
	{
		return;
	}
	for (struct MoverPiece *piece = s_moverPieceHead[list]; piece != NULL; piece = piece->next)
	{
		if (piece->inst == inst)
		{
			return;
		}
	}
	if (s_moverPieceRemaining == 0)
	{
		return;
	}

	struct MoverPiece *piece = s_moverPieceFree++;
	s_moverPieceRemaining--;
	piece->inst = inst;
	piece->next = NULL;
	if (s_moverPieceTail[list] == NULL)
	{
		s_moverPieceHead[list] = piece;
	}
	else
	{
		s_moverPieceTail[list]->next = piece;
	}
	s_moverPieceTail[list] = piece;
}

static void Mover_AtOffset(struct Instance *inst, s32 m[3][3], const s32 origin[3])
{
	const SVec3 *off = &inst->instDef->rot;

	for (int row = 0; row < 3; row++)
	{
		inst->matrix.t[row] = origin[row] + ((m[row][0] * off->x + m[row][1] * off->y + m[row][2] * off->z) >> 12);
	}
}

// CTR_DEV_MOVER_POSES=<file>: in frame CTR_DEV_MOVER_POSES_FRAME of the race (default 60),
// each mover instance's list, piece, place, matrix and scale as the renderer gets them,
// for tools/tracks/mover_pose_check.py.
static void Mover_LogPose(int list, int piece, const struct Instance *inst)
{
	const char *path = getenv("CTR_DEV_MOVER_POSES");
	const char *at = getenv("CTR_DEV_MOVER_POSES_FRAME");

	if ((path == NULL) || (GAME_TRACKER->framesInThisLEV != ((at != NULL) ? atoi(at) : 60)))
	{
		return;
	}
	FILE *file = fopen(path, "a");

	if (file != NULL)
	{
		fprintf(file, "%d %d %d %d %d", list, piece, (int)inst->matrix.t[0], (int)inst->matrix.t[1], (int)inst->matrix.t[2]);
		for (int i = 0; i < 9; i++)
		{
			fprintf(file, " %d", inst->matrix.m[i / 3][i % 3]);
		}
		fprintf(file, " %d %d %d %d\n", inst->scale.x, inst->scale.y, inst->scale.z, (inst->flags & HIDE_MODEL) != 0);
		fclose(file);
	}
}

// Keeps where the object is, and puts it and its other pieces where they are drawn.
static void Mover_Place(struct ConvertedMover *mover, struct Instance *inst, s32 m[3][3])
{
	const s32 origin[3] = {inst->matrix.t[0], inst->matrix.t[1], inst->matrix.t[2]};

	Mover_AtOffset(inst, m, origin);
	Mover_LogPose(mover->list, 0, inst);
	if ((mover->list < 0) || (mover->list >= MOVER_MOST_LISTS))
	{
		return;
	}
	int i = 1;
	for (struct MoverPiece *entry = s_moverPieceHead[mover->list]; entry != NULL; entry = entry->next)
	{
		struct Instance *piece = entry->inst;

		memcpy(piece->matrix.m, inst->matrix.m, sizeof(piece->matrix.m));
		Mover_AtOffset(piece, m, origin);
		piece->flags = (piece->flags & ~HIDE_MODEL) | (inst->flags & HIDE_MODEL);
		piece->scale = inst->scale;
		piece->depthBiasNormal = inst->depthBiasNormal;
		Mover_LogPose(mover->list, i++, piece);
	}
}

// tools/tracks/mover_view.py and mover_sheet.py: what waits or hides is brought out and held.
static int Mover_DevHeld(void)
{
	return (getenv("CTR_DEV_MOVERS_ONLY") != NULL) || (getenv("CTR_DEV_MOVERS_HELD") != NULL);
}

// The lap the race has reached, from 1, by its leading kart; 0 before the lights go
// green, so what waits for lap 1 (Waluigi Pinball's balls) counts its delay from the
// start. Counted from the load, the countdown put the rail ball 9000 units ahead of
// the karts, faster than them, never seen (docs/TRACK-CONVERTER.md).
static int Mover_RaceLap(void)
{
	int lap = 0;

	if (GAME_TRACKER->trafficLightsTimer > 0)
	{
		return 0;
	}

	for (int i = 0; i < 8; i++)
	{
		const struct Driver *driver = GAME_TRACKER->drivers[i];

		if ((driver != NULL) && (driver->lapIndex > lap))
		{
			lap = driver->lapIndex;
		}
	}
	return lap + 1;
}

static int Mover_GateOpen(const struct ConvertedMover *mover, const SVec3 *gate, const SVec3 *records)
{
	// tools/tracks/ram_check.py: what waits for a lap or a kart runs at once, and on
	if (Mover_DevHeld() || (getenv("CTR_DEV_MOVERS_OPEN") != NULL))
	{
		return 1;
	}
	if (Mover_RaceLap() < gate[0].z + mover->direction * records[2].y)
	{
		return 0;
	}
	if (gate[0].y == 0)
	{
		return 1;
	}

	for (int i = 0; i < 8; i++)
	{
		const struct Driver *driver = GAME_TRACKER->drivers[i];

		if (driver != NULL)
		{
			const float dx = (float)driver->instSelf->matrix.t[0] - gate[1].x;
			const float dz = (float)driver->instSelf->matrix.t[2] - gate[1].z;

			if (dx * dx + dz * dz < (float)gate[0].y * gate[0].y)
			{
				return 1;
			}
		}
	}
	return 0;
}

// Runs the clock on, held at any gate not open yet. mover->segment is the next gate,
// mover->direction how many times the keys have come round.
static u32 Mover_KeyClock(struct ConvertedMover *mover, const SVec3 *records, int count)
{
	const SVec3 *gates = &records[MOVER_RECORD_GATES];
	const int numGates = records[MOVER_RECORD_POINTS].z;
	const u32 step = records[0].y;
	const u32 restart = (u32)records[2].x * step;
	const u32 last = (u32)(count - 1) * step;

	while (mover->segment < numGates)
	{
		const SVec3 *gate = &gates[2 * mover->segment];
		const u32 at = (u32)gate[0].x * step;

		if (mover->timeMS < at)
		{
			break;
		}
		if (!Mover_GateOpen(mover, gate, records))
		{
			mover->timeMS = at; // it waits here
			return at;
		}
		mover->segment++;
	}

	if (mover->timeMS >= last)
	{
		mover->timeMS = restart + mover->timeMS - last;
		mover->direction++;
		mover->segment = 0;
		while ((mover->segment < numGates) && ((u32)gates[2 * mover->segment].x * step < restart))
		{
			mover->segment++;
		}
	}
	return mover->timeMS;
}

// -> whether it is not there just now.
static int Mover_FollowKeys(struct Instance *inst, struct ConvertedMover *mover, const SVec3 *records, s32 m[3][3], s32 dt)
{
	const int numGates = records[MOVER_RECORD_POINTS].z;
	const SVec3 *keys = &records[MOVER_RECORD_GATES + 2 * numGates];
	const int count = GAME_TRACKER->level1->ptrSpawnType2[mover->list].numCoords - MOVER_RECORD_GATES - 2 * numGates;
	const int flags = (u16)records[0].z;
	const u32 step = records[0].y;
	const u32 clock = Mover_KeyClock(mover, records, count);
	const u32 key = clock / step;
	const SVec3 *from = &keys[key];
	const SVec3 *to = &keys[(key + 1 < (u32)count) ? (key + 1) : key];
	const float part = (float)(clock % step) / step;
	const float dx = (float)to->x - from->x, dy = (float)to->y - from->y, dz = (float)to->z - from->z;
	const float flat = sqrtf(dx * dx + dz * dz);

	Mover_Base(records, m);
	if ((from->y == MOVER_KEY_HIDDEN) || (to->y == MOVER_KEY_HIDDEN))
	{
		inst->flags |= HIDE_MODEL;
		if (Mover_DevHeld())
		{
			mover->timeMS += step * 16; // on to where it shows
		}
		return 1;
	}

	if (Mover_DevHeld())
	{
		mover->timeMS -= dt; // it stays where it shows, to be looked at
	}

	inst->flags &= ~HIDE_MODEL;
	inst->matrix.t[0] = (s32)(from->x + dx * part);
	inst->matrix.t[1] = (s32)(from->y + dy * part);
	inst->matrix.t[2] = (s32)(from->z + dz * part);
	mover->harmless = (flags & MOVER_FLAG_DROPS) && (dy > -MOVER_DROP_KEY);
	if (flat > 0)
	{
		Mover_Face(mover, flags, m, dx / flat, dz / flat, dt);
	}
	Mover_Roll(mover, records, flags, m, sqrtf(dx * dx + dy * dy + dz * dz) * dt / step);

	if (records[MOVER_RECORD_POINTS].x != 0)
	{
		const u32 from_start = (u32)records[2].x * step;
		const s32 ran = (clock > from_start) ? (s32)(clock - from_start) : 0;
		s32 share = MOVER_SCALE_ONE + records[MOVER_RECORD_POINTS].x * ran / 1000;

		if (share > records[MOVER_RECORD_POINTS].y)
		{
			share = records[MOVER_RECORD_POINTS].y;
		}
		mover->hitShare = share;
		inst->scale.x = (s16)((inst->instDef->scale.x * share) >> 12);
		inst->scale.y = (s16)((inst->instDef->scale.y * share) >> 12);
		inst->scale.z = (s16)((inst->instDef->scale.z * share) >> 12);
	}
	return 0;
}

// CTR_DEV_MOVER_LOG: which kinds of mover hit a kart.
// "<what> <kind> <list> <kart> <x> <y> <z>": the kart's place when the object hit it
// ("hit") or pushed it out of its box ("push", each frame it does).
static void Mover_LogHit(const struct ConvertedMover *mover, const SVec3 *records, int kart, const struct Driver *driver, const char *what)
{
	const char *path = getenv("CTR_DEV_MOVER_LOG");
	FILE *file = (path != NULL) ? fopen(path, "a") : NULL;

	if (file != NULL)
	{
		fprintf(file, "%s %d %d %d %d %d %d\n", what, records[0].x, mover->list, kart, (int)driver->instSelf->matrix.t[0], (int)driver->instSelf->matrix.t[1],
		        (int)driver->instSelf->matrix.t[2]);
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
		const s32 dx = ((push * m[0][best]) >> 12) << 8, dz = ((push * m[2][best]) >> 12) << 8;

		// a player's kart through its sweep, as Mover_Carry does: written straight to the
		// position, a push up a slope (driving into Waluigi Pinball's bumpers)
		// put the kart under the ground, and it fell through the course
		if (driver->actionsFlagSet & ACTION_BOT)
		{
			driver->posCurr.x += dx;
			driver->posCurr.z += dz;
		}
		else
		{
			COLL_MOVED_AddCarry(driver, dx, 0, dz);
		}
	}
}

static int s_moverGraceUntil[8];

// Whether a kart is still let through after a hit. The level's frame count starts
// again with each race (and a loaded state may take it back), so a grace that ends
// further off than one grace from now belongs to another race and is over.
static int Mover_InGrace(int driver)
{
	const int left = s_moverGraceUntil[driver] - GAME_TRACKER->framesInThisLEV;

	if (left > MOVER_HIT_GRACE)
	{
		s_moverGraceUntil[driver] = 0;
		return 0;
	}
	return left > 0;
}

static void Mover_HitKarts(struct ConvertedMover *mover, struct Instance *inst, const SVec3 *records, s32 m[3][3])
{
	const int damage = mover->harmless ? 0 : (records[0].z & MOVER_DAMAGE_MASK);
	const s32 share = mover->hitShare ? mover->hitShare : MOVER_SCALE_ONE;

	if ((records[1].x == 0) || ((damage == 0) && !((u16)records[0].z & MOVER_FLAG_SOLID)))
	{
		return;
	}

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = GAME_TRACKER->drivers[i];
		s32 dx, dy, dz, local[3], half[3];
		int wasSpinning, inside = 1;

		if ((driver == NULL) || Mover_InGrace(i))
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

			half[axis] = ((((axis == 0) ? records[1].x : (axis == 1) ? records[1].y : records[1].z) * share) >> 12) + MOVER_KART_RADIUS;
			local[axis] = ((dx * m[0][axis] + dy * m[1][axis] + dz * m[2][axis]) >> 12) - ((centre * share) >> 12);
			inside = inside && (local[axis] > -half[axis]) && (local[axis] < half[axis]);
		}

		if (!inside)
		{
			continue;
		}

		if (damage == MOVER_ITEM_BOX)
		{
			struct ScratchpadStruct sps;

			sps.Input1.modelID = DYNAMIC_PLAYER;
			if (RB_CrateWeapon_LInC(inst, driver->instSelf->thread, &sps))
			{
				Mover_LogHit(mover, records, i, driver, "hit");
				NativeDev_MoverHit(mover->list, i, MOVER_EVENT_CRATE);
			}
			continue;
		}

		if ((u16)records[0].z & MOVER_FLAG_SOLID)
		{
			Mover_PushOut(driver, m, local, half);
			Mover_LogHit(mover, records, i, driver, "push");
			NativeDev_MoverHit(mover->list, i, MOVER_EVENT_PUSH);
		}

		if (damage == 0)
		{
			continue;
		}

		wasSpinning = driver->kartState == KS_SPINNING;
		if (RB_Hazard_HurtDriver(driver, damage, 0, 0) && !wasSpinning)
		{
			s_moverGraceUntil[i] = GAME_TRACKER->framesInThisLEV + MOVER_HIT_GRACE;
			Mover_LogHit(mover, records, i, driver, "hit");
			NativeDev_MoverHit(mover->list, i, MOVER_EVENT_HURT);
			if ((driver->actionsFlagSet & ACTION_BOT) == 0)
			{
				OtherFX_Play_Echo(MOVER_HIT_SOUND, 1, (driver->actionsFlagSet & ACTION_ENGINE_ECHO) != 0);
			}
		}
	}
}

// CTR_DEV_MOVER_LOG=<file>: each mover's place and turn every 293 frames (or
// CTR_DEV_MOVER_LOG_EVERY), for tools/tracks/mover_check.py.
static void Mover_Log(const struct ConvertedMover *mover, const struct Instance *inst, const SVec3 *records)
{
	const char *path = getenv("CTR_DEV_MOVER_LOG");
	const char *every = getenv("CTR_DEV_MOVER_LOG_EVERY");
	const int period = (every != NULL) ? atoi(every) : 293;

	if ((path != NULL) && (period > 0) && ((GAME_TRACKER->framesInThisLEV % period) == 0))
	{
		FILE *file = fopen(path, "a");

		if (file != NULL)
		{
			// the whole matrix: a log turning about its own length keeps its diagonal
			fprintf(file, "%d %d %d %d %d", mover->list, records[0].x, inst->matrix.t[0], inst->matrix.t[1], inst->matrix.t[2]);
			for (int i = 0; i < 9; i++)
			{
				fprintf(file, " %d", inst->matrix.m[i / 3][i % 3]);
			}
			fprintf(file, " %d\n", inst->scale.y);
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
	// CTR_DEV_MOVERS_REST: every mover holds its placed pose (mover_pose_check.py)
	const s32 dt = (getenv("CTR_DEV_MOVERS_REST") != NULL) ? 0 : GAME_TRACKER->elapsedTimeMS;
	s32 m[3][3], turn[3][3];
	int hidden = 0;

	mover->timeMS += dt;
	if ((records[0].x == MOVER_SPIN) || (records[0].x == MOVER_SWING) || (records[0].x == MOVER_PULSE) || (records[0].x == MOVER_STILL))
	{
		// it stays where it was placed; the last tick drew it at its first piece's place
		inst->matrix.t[0] = mover->homeX;
		inst->matrix.t[1] = mover->homeY;
		inst->matrix.t[2] = mover->homeZ;
	}
	Mover_Base(records, m);

	switch (records[0].x)
	{
	case MOVER_PATH:
		Mover_FollowPath(inst, mover, records, m, dt);
		break;

	case MOVER_KEYS:
		hidden = Mover_FollowKeys(inst, mover, records, m, dt);
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
			Mover_Carry(inst, records, m, mover->rollAngle - before);
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
		const s32 phase = (s32)((mover->timeMS + records[2].z) % (u32)records[2].x) * 4096 / records[2].x;
		const s32 share = MATH_Sin(phase / 2);

		inst->matrix.t[0] = mover->homeX + ((way->x * share) >> 12);
		inst->matrix.t[1] = mover->homeY + ((way->y * share) >> 12);
		inst->matrix.t[2] = mover->homeZ + ((way->z * share) >> 12);
		break;
	}

	case MOVER_ORBIT:
	{
		const s32 angle = (s32)((mover->timeMS + records[2].z) % (u32)records[2].y) * 4096 / records[2].y;
		const s32 across = (records[2].x * MATH_Sin(angle)) >> 12, along = (records[2].x * MATH_Cos(angle)) >> 12;

		inst->matrix.t[0] = mover->homeX + ((m[0][0] * across + m[0][1] * along) >> 12);
		inst->matrix.t[1] = mover->homeY + ((m[1][0] * across + m[1][1] * along) >> 12);
		inst->matrix.t[2] = mover->homeZ + ((m[2][0] * across + m[2][1] * along) >> 12);
		Mover_Identity(m); // a picture: the game turns it to the camera
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
			Mover_Place(mover, inst, m);
			return;
		}
	}
	if (!hidden)
	{
		Mover_HitKarts(mover, inst, records, m);
	}
	Mover_Place(mover, inst, m);
}

// An item box's own thread is the crate's, from its first break to its regrowth, so
// its mover's is not in inst->thread.
static int Mover_HasThread(const struct Instance *inst)
{
	for (struct Thread *t = GAME_TRACKER->threadBuckets[STATIC].thread; t != NULL; t = t->siblingThread)
	{
		if ((t->inst == inst) && (t->funcThTick == ConvertedMover_ThTick))
		{
			return 1;
		}
	}
	return 0;
}

// Every level loads at the same address, so what is kept of one level's movers is
// cleared when the next level's instances are made (INSTANCE_LevInitAll): a list's
// pieces kept from the last course were other instances of this one, moved with it.
void ConvertedMover_LevelStart(const struct InstDef *definitions, int count)
{
	int pieces = 0;
	for (int i = 0; i < count; i++)
	{
		pieces += (definitions[i].modelID == CONVERTED_MOVER_ID) && (definitions[i]._pad_scale != 0);
	}
	s_moverPieceFree = pieces ? MEMPACK_AllocMem(pieces * sizeof(struct MoverPiece), "Mover pieces") : NULL;
	s_moverPieceRemaining = (s_moverPieceFree != NULL) ? pieces : 0;
	memset(s_moverPieceHead, 0, sizeof(s_moverPieceHead));
	memset(s_moverPieceTail, 0, sizeof(s_moverPieceTail));
	memset(s_moverAt, 0, sizeof(s_moverAt));
	memset(s_moverGraceUntil, 0, sizeof(s_moverGraceUntil));
}

void ConvertedMover_LInB(struct Instance *inst)
{
	struct ConvertedMover *mover;
	struct Thread *t;
	const SVec3 *records;
	int itemBox;

	if (Mover_DrawnOnly(inst))
	{
		Mover_AddPiece(inst);
		return;
	}
	if ((inst->thread != NULL) || Mover_HasThread(inst))
	{
		return;
	}

	t = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(sizeof(struct ConvertedMover), NONE, SMALL, STATIC), ConvertedMover_ThTick, "mover", NULL);
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
	itemBox = (Mover_Records(mover)[0].z & MOVER_DAMAGE_MASK) == MOVER_ITEM_BOX;
	inst->thread = itemBox ? NULL : t;
	mover->direction = 1;
	mover->homeX = inst->matrix.t[0];
	mover->homeY = inst->matrix.t[1];
	mover->homeZ = inst->matrix.t[2];

	records = Mover_Records(mover);
	if ((u16)records[0].z & MOVER_FLAG_CARRIES)
	{
		inst->depthBiasNormal = MOVER_FLOOR_DEPTH_BIAS;
	}
	if (records[0].x == MOVER_KEYS)
	{
		mover->direction = 0;
		mover->timeMS = (u32)records[2].z * records[0].y;
	}
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
