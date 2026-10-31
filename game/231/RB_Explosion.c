#include <common.h>

void RB_Explosion_ThTick(struct Thread *t)
{
	struct Instance *inst = t->inst;

	if (inst->animFrame + 1 < INSTANCE_GetNumAnimFrames(inst, 0))
	{
		inst->animFrame++;
	}
	else
	{
		// dead thread
		t->flags |= THREAD_FLAG_DEAD;
	}

	ThTick_FastRET(t);
}

void RB_Explosion_InitPotion(struct Instance *inst)
{
	struct Instance *shatterInst;
	struct Particle *p;
	struct InstanceBirthParams birth;

	if (inst->model->id == STATIC_BEAKER_RED)
	{
		birth.modelID = STATIC_SHOCKWAVE_RED;
	}
	else
	{
		birth.modelID = STATIC_SHOCKWAVE_GREEN;
	}

	// create thread for shatter
	birth.name = rb_nameShatter;
	birth.poolType = SMALL;
	birth.bucket = OTHER;
	birth.funcThTick = RB_Explosion_ThTick;
	birth.objSize = 0;
	birth.parent = NULL;
	shatterInst = INSTANCE_BirthWithThread_Stack(&birth);

#if defined(CTR_NATIVE)
	// NOTE(dowlle): NULL when the native instance pool is full (#205). The shatter ring
	// is cosmetic. Skip it, keep the particles and the teeth search (gameplay).
	if (shatterInst != NULL)
	{
#endif
	shatterInst->flags |= (PIXEL_LOD | CUSTOM_MATRIX);

	// copy position and rotation from one instance to the other
	shatterInst->matrix = inst->matrix;
	shatterInst->thread->funcThDestroy = PROC_DestroyInstance;
	shatterInst->scale.z = 0x800;
	shatterInst->scale.y = 0x800;
	shatterInst->scale.x = 0x800;
#if defined(CTR_NATIVE)
	}
#endif

	// particles for potion shatter
	{
		int i;
		for (i = 0; i < 5; i++)
		{
			// Create instance in particle pool
			p = Particle_Init(0, GAME_TRACKER->iconGroup[1], R231.emSet_PotionShatter);

			if (p == NULL)
			{
				continue;
			}

			// The shatter instance shares the potion's position; read the potion so a
			// skipped shatter (full native pool) cannot dereference NULL.
			p->axis[0].startVal = (u32)p->axis[0].startVal + ((u32)inst->matrix.t[0] << 8);
			p->axis[1].startVal = (u32)p->axis[1].startVal + ((u32)inst->matrix.t[1] << 8);
			p->axis[2].startVal = (u32)p->axis[2].startVal + ((u32)inst->matrix.t[2] << 8);

			if (birth.modelID == STATIC_SHOCKWAVE_GREEN)
			{
				p->axis[7].startVal = 1;
				p->axis[8].startVal = 0xc800;
			}

			else
			{
				p->axis[7].startVal = 0xc800;
				p->axis[8].startVal = 1;
			}

			p->axis[9].startVal = 1;

			p->funcPtr = Particle_FuncPtr_PotionShatter;
			p->owner.modelID = birth.modelID;
		}
	}

	RB_Potion_OnShatter_TeethSearch(inst);
	return;
}


void RB_Explosion_InitGeneric(struct Instance *inst)
{
	struct Instance *explosion;
	struct InstanceBirthParams birth;

	// create thread for explosion
	birth.modelID = STATIC_CRATE_EXPLOSION;
	birth.name = rb_nameExplosion;
	birth.poolType = SMALL;
	birth.bucket = OTHER;
	birth.funcThTick = RB_Explosion_ThTick;
	birth.objSize = 0;
	birth.parent = NULL;
	explosion = INSTANCE_BirthWithThread_Stack(&birth);

#if defined(CTR_NATIVE)
	// NOTE(dowlle): NULL when the native instance pool is full (#205). Cosmetic only.
	if (explosion == NULL)
	{
		return;
	}
#endif

	// copy position and rotation from one instance to the other
	explosion->matrix = inst->matrix;

	// TNT uses the red explosion; other crates use green.
	if (inst->model->id == STATIC_CRATE_TNT)
	{
		// red
		explosion->colorRGBA = 0xad10000;
	}
	else
	{
		// green
		explosion->colorRGBA = 0x1eac000;
	}

	// set scale
	explosion->alphaScale = 0x1000;

	// set funcThDestroy to remove instance from instance pool
	explosion->thread->funcThDestroy = PROC_DestroyInstance;
	return;
}
