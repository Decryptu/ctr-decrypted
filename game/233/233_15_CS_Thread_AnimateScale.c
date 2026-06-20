#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae2b8-0x800ae318
void CS_Thread_AnimateScale(struct Thread *t)
{
	struct Instance *inst = t->inst;
	struct CutsceneObj *cs = t->object;

	if (!inst)
		return;

	if (cs->scaleSpeed == 0)
		return;

	int newScale = (int)inst->scale.x + (int)cs->scaleSpeed;
	int desiredScale = (int)cs->desiredScale;

	if (cs->scaleSpeed > 0)
	{
		if (newScale >= desiredScale)
		{
			newScale = desiredScale;
			cs->scaleSpeed = 0;
		}
	}
	else
	{
		if (newScale <= desiredScale)
		{
			newScale = desiredScale;
			cs->scaleSpeed = 0;
		}
	}

	inst->scale.x = (s16)newScale;
	inst->scale.y = (s16)newScale;
	inst->scale.z = (s16)newScale;
}
