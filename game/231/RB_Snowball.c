#include "RB_Collision.h"

static inline const struct SpawnPosRot *RB_Snowball_GetFrame(struct Snowball *snowObj, s32 index)
{
	const struct SpawnPosRot *base = GAME_TRACKER->level1->ptrSpawnType2_PosRot[snowObj->snowID].coords.posRot;
	// NOTE(aalhendi): Keep the byte stride before the base in this 32-bit address calculation.
	return (const struct SpawnPosRot *)(index * sizeof(struct SpawnPosRot) + (u32)base);
}

void RB_Snowball_ThTick(struct Thread *t)
{
	struct Instance *snowInst;
	struct Snowball *snowObj;

	int modelID;
	int soundID;
	int pointIndex;
	int frameIndex;
	SVec3 rot;
	struct Instance *hitInst;

	snowObj = (struct Snowball *)t->object;
	snowInst = t->inst;

	do
	{
		if (GAME_TRACKER->level1->numSpawnType2_PosRot != 0)
		{
			modelID = snowInst->model->id;

			// Retail checks DYNAMIC_SNOWBALL, but Blizzard Bluff uses TEMP_SNOWBALL.
			if (modelID == DYNAMIC_SNOWBALL)
			{
				// snowball roll
				soundID = 0x73;
				PlaySound3D_Flags(&snowObj->soundIDCount, soundID, snowInst);
			}

			// sewer speedway barrel
			else if (modelID == DYNAMIC_BARREL)
			{
				// barrel roll
				soundID = 0x74;
				PlaySound3D_Flags(&snowObj->soundIDCount, soundID, snowInst);
			}

			pointIndex = snowObj->pointIndex;
			if (pointIndex <= snowObj->numPoints)
			{
				frameIndex = pointIndex;
			}
			else
			{
				frameIndex = (snowObj->numPoints * 2) - pointIndex;
			}

			rot.x = RB_Snowball_GetFrame(snowObj, frameIndex)->rot.x;
			rot.y = RB_Snowball_GetFrame(snowObj, frameIndex)->rot.y;
			rot.z = RB_Snowball_GetFrame(snowObj, frameIndex)->rot.z;

#if defined(CTR_NATIVE)
			SVec3 pos = RB_Snowball_GetFrame(snowObj, frameIndex)->pos;

			// 60 FPS: the path holds one point per 30 FPS frame. Odd frames
			// advance to the next point and draw halfway between the two.
			if (CTR_NATIVE_60FPS_ACTIVE && ((GAME_TRACKER->timer & 1) != 0))
			{
				snowObj->pointIndex = (snowObj->pointIndex + 1) % (snowObj->numPoints * 2);
				pointIndex = snowObj->pointIndex;
				if (pointIndex <= snowObj->numPoints)
				{
					frameIndex = pointIndex;
				}
				else
				{
					frameIndex = (snowObj->numPoints * 2) - pointIndex;
				}

				const struct SpawnPosRot *nextFrame = RB_Snowball_GetFrame(snowObj, frameIndex);
				{
					const int sameZ = rot.z == nextFrame->rot.z;
					pos.x = (s16)(((int)pos.x + (int)nextFrame->pos.x) / 2);
					pos.y = (s16)(((int)pos.y + (int)nextFrame->pos.y) / 2);
					pos.z = (s16)(((int)pos.z + (int)nextFrame->pos.z) / 2);
					if (sameZ)
					{
						rot.x = (s16)(((int)rot.x + (int)nextFrame->rot.x) / 2);
						rot.y = (s16)(((int)rot.y + (int)nextFrame->rot.y) / 2);
						rot.z = (s16)(((int)rot.z + (int)nextFrame->rot.z) / 2);
					}
				}
			}

			ConvertRotToMatrix(&snowInst->matrix, &rot);

			snowInst->matrix.t[0] = pos.x;
			snowInst->matrix.t[1] = pos.y;
			snowInst->matrix.t[2] = pos.z;
#else
			ConvertRotToMatrix(&snowInst->matrix, &rot);

			snowInst->matrix.t[0] = RB_Snowball_GetFrame(snowObj, frameIndex)->pos.x;
			snowInst->matrix.t[1] = RB_Snowball_GetFrame(snowObj, frameIndex)->pos.y;
			snowInst->matrix.t[2] = RB_Snowball_GetFrame(snowObj, frameIndex)->pos.z;
#endif

			hitInst = RB_FindDriverCollision(snowInst, t);
			if (hitInst != NULL)
			{
				RB_Hazard_HurtDriver(hitInst->thread->object, 3, 0, 0);
			}
		}

		if (!CTR_NATIVE_60FPS_ACTIVE)
		{
			snowObj->pointIndex = (snowObj->pointIndex + 1) % (snowObj->numPoints * 2);
		}
		ThTick_FastRET(t);
#ifdef CTR_NATIVE
		// NOTE(aalhendi): Native ticks return as callbacks; retail yields through FastRET.
		return;
#endif
	} while (1);
}

void RB_Snowball_LInB(struct Instance *inst)
{
	struct Snowball *snowObj;
	struct Thread *t;

	if (inst->thread != 0)
	{
		return;
	}

	t = PROC_BirthWithObject(
	    // creation flags
	    SIZE_RELATIVE_POOL_BUCKET(sizeof(struct Snowball), NONE, SMALL, STATIC),

	    RB_Snowball_ThTick, // behavior
	    "snowball",         // debug name
	    0                   // thread relative
	);

	inst->thread = t;
	if (t == 0)
	{
		return;
	}
	t->inst = inst;

	snowObj = ((struct Snowball *)t->object);
	snowObj->rot_unused.x = 0;

	snowObj->snowID = inst->name[strlen(inst->name) - 1] - '0';

	snowObj->numPoints = GAME_TRACKER->level1->ptrSpawnType2_PosRot[snowObj->snowID].numCoords - 1;

	inst->scale.x = 0x1000;
	inst->scale.y = 0x1000;
	inst->scale.z = 0x1000;

	snowObj->rot_unused.y = RB_ShiftRightS16(inst->matrix.m[0][2], 2);
	snowObj->rot_unused.z = RB_ShiftRightS16(inst->matrix.m[2][2], 2);
	snowObj->soundIDCount = 0;
}
