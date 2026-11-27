#include <common.h>

// NOTE(aalhendi): These lists alternate between InstDef* and Instance* in place.
// Shared PVS lists therefore toggle once per reference, just as retail does; do
// not deduplicate the traversal.
// A slot is the PSX address of either record, and each knows the other. Retail
// reads the peer at 0x2c of both; here an Instance is told apart by coming from
// the instance pool, since only the InstDef keeps retail's layout.
typedef PsxVoidPtr LevInstDefLink;

static u32 LevInstDef_Peer(u32 record)
{
	const struct JitPool *pool = &GAME_TRACKER->JitPools.instance;
	const u8 *host = PsxSpace_ToHost(record);
	const u8 *poolData = pool->ptrPoolData;

	if ((host >= poolData) && (host < poolData + pool->poolSize))
	{
		return PsxSpace_FromHost(((const struct Instance *)host)->instDef);
	}
	return PSX_ADDR(((const struct InstDef *)host)->ptrInstance);
}


void LevInstDef_UnPack(struct mesh_info *ptr_mesh_info)
{
	struct QuadBlock *qbCurr;
	struct QuadBlock *qbEnd;
	LevInstDefLink *visInstSrc;
	struct Level *level1;

	qbCurr = PSX_GET(ptr_mesh_info->ptrQuadBlockArray);
	qbEnd = qbCurr + ptr_mesh_info->numQuadBlock;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	NativeDev_QuadDump(ptr_mesh_info); // CTR_DEV_QUAD_DUMP
#endif

	// loop through all quadblocks
	for (; qbCurr < qbEnd; qbCurr++)
	{
		if ((PSX_GET(qbCurr->pvs) != 0) && (PSX_GET(PSX_GET(qbCurr->pvs)->visInstSrc) != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (LevInstDefLink *)PSX_GET(PSX_GET(qbCurr->pvs)->visInstSrc); PSX_ADDR(visInstSrc[0]) != 0; visInstSrc++)
			{
				PSX_ADDR(visInstSrc[0]) = LevInstDef_Peer(PSX_ADDR(visInstSrc[0]));
			}
		}
	}

	level1 = GAME_TRACKER->level1;
	visInstSrc = (LevInstDefLink *)PSX_GET(level1->ptrInstDefPtrArray);

	if (visInstSrc != NULL)
	{
		// loop through all instDef pointers in the LEV
		for (; PSX_ADDR(visInstSrc[0]) != 0; visInstSrc++)
		{
			PSX_ADDR(visInstSrc[0]) = LevInstDef_Peer(PSX_ADDR(visInstSrc[0]));
		}
	}
}


void LevInstDef_RePack(struct mesh_info *ptr_mesh_info, b32 boolAdvHub)
{
	struct QuadBlock *qbCurr;
	struct QuadBlock *qbEnd;
	LevInstDefLink *visInstSrc;
	struct Level *level1;
	struct Thread *th;

	qbCurr = PSX_GET(ptr_mesh_info->ptrQuadBlockArray);
	qbEnd = qbCurr + ptr_mesh_info->numQuadBlock;

	// loop through all quadblocks
	for (; qbCurr < qbEnd; qbCurr++)
	{
		if ((PSX_GET(qbCurr->pvs) != 0) && (PSX_GET(PSX_GET(qbCurr->pvs)->visInstSrc) != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (LevInstDefLink *)PSX_GET(PSX_GET(qbCurr->pvs)->visInstSrc); PSX_ADDR(visInstSrc[0]) != 0; visInstSrc++)
			{
				PSX_ADDR(visInstSrc[0]) = LevInstDef_Peer(PSX_ADDR(visInstSrc[0]));
			}
		}
	}

	level1 = GAME_TRACKER->level1;
	visInstSrc = (LevInstDefLink *)PSX_GET(level1->ptrInstDefPtrArray);

	if (visInstSrc != NULL)
	{
		// loop through all instDef pointers in the LEV
		for (; PSX_ADDR(visInstSrc[0]) != 0; visInstSrc++)
		{
			struct Instance *inst = PSX_GET(visInstSrc[0]);
			struct InstDef *instDef = inst->instDef;

			// if on adv hub
			if (boolAdvHub != 0)
			{
				th = inst->thread;
				if (th != 0)
				{
					th->flags |= THREAD_FLAG_DEAD;
				}

				// Return the level-owned instance to the free pool.
				LIST_AddFront(&GAME_TRACKER->JitPools.instance.free, (struct Item *)inst);
			}

			// go back to instDef
			PSX_SET(visInstSrc[0], instDef);
		}
	}

	PROC_CheckAllForDead();
}
