#include <common.h>

#if defined(CTR_NATIVE)
// The PSX address of an AnimTex's frame-th texture (its array follows it).
#define CYCLE_TEX_FRAME(animtex, frame) PSX_ADDR(ANIMTEX_GETARRAY(animtex)[(s16)(frame)])
#else
// NOTE(aalhendi): Index from the AnimTex base so GCC preserves retail's signed 16-bit frame index and pointer-add order.
#define CYCLE_TEX_FRAME(animtex, frame) \
	((u32)((struct IconGroup4 **)((u32)(animtex) + ((u32)(s16)(frame) << 2)))[sizeof(struct AnimTex) / sizeof(struct IconGroup4 *)])
#endif

void CTR_CycleTex_LEV(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
	while (PSX_ADDR(curAnimTex->ptrActiveTex) != PsxSpace_FromHost(animtex))
	{
		// which texture to draw this frame
		frameCurr = FPS_HALF(timer) + curAnimTex->frameOffset;

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;

		// save result
		curAnimTex->frameCurr = frameCurr;

		// Save new frame
		// For levels, this is just a pointer
		PSX_ADDR(curAnimTex->ptrActiveTex) = CYCLE_TEX_FRAME(curAnimTex, frameCurr);

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&(ANIMTEX_GETARRAY(curAnimTex))[curAnimTex->numFrames];
	}
}

void CTR_CycleTex_Model(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
	while (PSX_ADDR(curAnimTex->ptrActiveTex) != PsxSpace_FromHost(animtex))
	{
		// which texture to draw this frame
		frameCurr = FPS_HALF(timer) + curAnimTex->frameOffset;

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;

		// save result
		curAnimTex->frameCurr = frameCurr;

		// Save new frame
		// For Model, this is a pointer to a pointer
		*PSX_GET(curAnimTex->ptrActiveTex) = (int)CYCLE_TEX_FRAME(curAnimTex, frameCurr);

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&(ANIMTEX_GETARRAY(curAnimTex))[curAnimTex->numFrames];
	}
}

void CTR_CycleTex_AllModels(u32 numModels, PsxModelPtr *pModelArray, int timer)
{
	struct Model *pModel;
	struct ModelHeader *pHeader;
	struct ModelHeader *pHeaderEnd;
	s32 headerCount;

	if (pModelArray == NULL)
	{
		return;
	}

	if (numModels == 0)
	{
		return;
	}

	while (true)
	{
		pModel = PSX_GET(*pModelArray);
		if (pModel == NULL)
		{
			return;
		}

		headerCount = pModel->numHeaders;
		// NOTE(aalhendi): Keep the short-lived count distinct from the header end pointer for retail register allocation.
		CTR_PSX_KEEP_VALUE(headerCount);
		pHeader = PSX_GET(pModel->headers);
		pHeaderEnd = pHeader + headerCount;
		while (pHeader < pHeaderEnd)
		{
			if ((PSX_GET(pHeader->animtex) != NULL) && ((pHeader->flags & 2) == 0))
			{
				CTR_CycleTex_Model(PSX_GET(pHeader->animtex), timer);
			}
			pHeader++;
		}

		numModels--;
		if (numModels == 0)
		{
			return;
		}

		pModelArray++;
	}
}

void CTR_CycleTex_2p3p4pWumpaHUD(u32 *ptrActiveTex, u32 *ptrArray, int numFrames)
{
	ptrArray[0] = ptrActiveTex[0];
	ptrArray += numFrames;
	ptrArray--;
	ptrActiveTex[0] = CtrGpu_PrimToOTLink24(ptrArray);
}
