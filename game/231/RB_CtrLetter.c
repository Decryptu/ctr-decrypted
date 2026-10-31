#include "RB_Pickup.h"

#ifdef CTR_AP
static int AP_CtrLetterIndex(int modelID)
{
	if (modelID == STATIC_C) return 0;
	if (modelID == STATIC_T) return 1;
	if (modelID == STATIC_R) return 2;
	return -1;
}
#endif

int RB_CtrLetter_ThCollide(struct Thread *letterTh, struct Thread *driverTh, void *funcThCollide, struct ScratchpadStruct *sps)
{
	SVec4 posWorld;
	union RBPickupScreen posScreen;
	struct Driver *driver;
	struct Instance *letterInst;
	(void)funcThCollide;


	letterInst = letterTh->inst;
	if (sps->Input1.modelID == DYNAMIC_PLAYER)
	{
		driver = driverTh->object;
#ifdef CTR_AP
		int apLetter = AP_CtrLetterIndex(letterInst->model->id);
		if (apLetter >= 0 && !AP_LetterAvailable(GAME_TRACKER->levelID, apLetter))
		{
			// `padding` is owned by the AP build and starts at zero when this letter's
			// per-race object is born. The collision callback can run every frame while
			// the kart overlaps a ghost letter, so log the refusal once for this letter
			// instance rather than flooding ctr-ap.log.
			struct CtrLetter *letterObj = letterTh->object;
			if (letterObj != NULL && letterObj->padding == 0)
			{
				letterObj->padding = 1;
				AP_LetterUnavailableTouched(GAME_TRACKER->levelID, apLetter);
			}
			return 0;
		}
#endif

		posWorld.x = (s16)letterInst->matrix.t[0];
		posWorld.y = (s16)letterInst->matrix.t[1];
		posWorld.z = (s16)letterInst->matrix.t[2];
		RB_Pickup_SetCamera(driver);
		CTR_GteLoadPositionV0(&posWorld);
		gte_rtps();
		CTR_GteStorePositionXY(posScreen.coords);

		driver->PickupLetterHUD.startX = posScreen.coords[0] + GAME_TRACKER->pushBuffer[driver->driverID].rect.x;
		driver->PickupLetterHUD.startY = posScreen.coords[1] + GAME_TRACKER->pushBuffer[driver->driverID].rect.y - 0x14;
		driver->PickupLetterHUD.cooldown = FPS_DOUBLE(10);
		driver->PickupLetterHUD.modelID = letterInst->model->id;
		driver->PickupLetterHUD.numCollected++;
#ifdef CTR_AP
		if (apLetter >= 0) AP_LetterCollected(GAME_TRACKER->levelID, apLetter);
#endif

		letterInst->scale.x = 0;
		letterInst->scale.y = 0;
		letterInst->scale.z = 0;
		letterInst->thread = 0;
		letterInst->flags |= HIDE_MODEL;

		OtherFX_Play(100, 1);
		letterTh->flags |= THREAD_FLAG_DEAD;

		return 1;
	}
	return 0;
}


int RB_CtrLetter_LInC(struct Instance *letterInst, struct Thread *driverTh, struct ScratchpadStruct *sps)
{
	struct Thread *letterTh;
	s32 result;

	letterTh = letterInst->thread;
	if (letterTh == NULL)
	{
		letterInst->thread = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(4, NONE, SMALL, STATIC), RB_CtrLetter_ThTick, "ctr", NULL);

		letterTh = letterInst->thread;
		if (letterTh == NULL)
		{
			return 0;
		}

		letterTh->inst = letterInst;
		letterTh->funcThCollide = (void *)RB_CtrLetter_ThCollide;
	}
	letterTh = letterInst->thread;

	if (letterTh == NULL)
	{
		return 0;
	}

	if (letterTh->funcThCollide == NULL)
	{
		return 0;
	}

	if (letterInst->scale.x != 0)
	{
		result = ((ThreadScratchCollideFunc)letterTh->funcThCollide)(letterTh, driverTh, letterTh->funcThCollide, sps);
	}
	else
	{
		result = 0;
	}
	return result;
}

SVec3 letterLightDir = {0x94F, 0x94F, -0x94F};

#ifdef CTR_AP
static void AP_CtrLetter_UpdateVisual(struct Instance *inst)
{
	int letter = AP_CtrLetterIndex(inst->model->id);
	if (letter < 0 || inst->scale.x == 0 ||
	    (inst->thread && (inst->thread->flags & THREAD_FLAG_DEAD))) return;
	if (AP_LetterAvailable(GAME_TRACKER->levelID, letter))
	{
		inst->flags &= ~(DRAW_TRANSPARENT | GHOST_DRAW_TRANSPARENT);
		inst->flags |= USE_SPECULAR_LIGHT;
		inst->alphaScale = 0;
		inst->colorRGBA = 0xffc8000;
	}
	else
	{
		inst->flags &= ~(DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
		inst->flags |= GHOST_DRAW_TRANSPARENT;
		inst->alphaScale = 0xa00;
		inst->colorRGBA = 0;
	}
}
#endif


void RB_CtrLetter_ThTick(struct Thread *t)
{
	struct Instance *letterInst;
	struct CtrLetter *letterObj;

	letterInst = t->inst;
	letterObj = t->object;

	// rotate each frame
	letterObj->rot.y += FPS_HALF(0x40);
	ConvertRotToMatrix(&letterInst->matrix, &letterObj->rot);

	Vector_SpecLightSpin3D(letterInst, &letterObj->rot, &letterLightDir);
#ifdef CTR_AP
	AP_CtrLetter_UpdateVisual(letterInst);
#endif
}


void RB_CtrLetter_LInB(struct Instance *inst)
{
	struct CtrLetter *letterObj;
	struct Thread *t;

	if (inst->thread == NULL)
	{
		t = PROC_BirthWithObject(
		    // creation flags
		    SIZE_RELATIVE_POOL_BUCKET(sizeof(struct CtrLetter), NONE, SMALL, STATIC),

		    RB_CtrLetter_ThTick, // behavior
		    "ctr",               // debug name
		    0                    // thread relative
		);

		inst->thread = t;
		if (t == 0)
		{
			return;
		}

		t->funcThCollide = (void *)RB_CtrLetter_ThCollide;
		t->inst = inst;

		letterObj = ((struct CtrLetter *)t->object);
		letterObj->rot.x = 0;
		letterObj->rot.y = 0;
		letterObj->rot.z = 0;
		letterObj->padding = 0;

		inst->scale.x = 0x1800;
		inst->scale.y = 0x1800;
		inst->scale.z = 0x1800;

		inst->colorRGBA = 0xffc8000;

		inst->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
	}

	RB_Default_LInB(inst);
#ifdef CTR_AP
	AP_CtrLetter_UpdateVisual(inst);
#endif
}
