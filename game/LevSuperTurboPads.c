#include <common.h>
#include <ctr_extras.h>

// Extras "Cyan Super Turbo Pads" (High Octane, after CTR: Nitro-Fueled):
// super turbo pads are drawn cyan so they stand apart from ordinary pads.
//
// Retail draws both kinds with the same animated arrow texture, so the
// texture itself cannot change. Instead, when a level loads, every face that
// shows a super pad gets its own copy of the pad animation whose texture
// layouts carry CTR_EXTRAS_CYAN_CLUT_BIT in their palette word; the renderer
// recolours those texels (platform/native_renderer.c). Which faces show a
// super pad is read from the level's own trigger quads (rules and
// measurements in include/ctr_extras.h), so no track needs a special case,
// and the all-super-turbo-pads cheat turns every pad cyan, as it makes every
// pad a super pad. Visual only: collision reads trigger quads, never textures.
//
// The copies live in the level's memory pack, where the level renderer
// expects level texture data (DrawLevelOvr1P_IsNativeLevelSpan). Applied at
// level load, so turning the option on or off takes effect on the next load.

#ifdef CTR_NATIVE

enum
{
	SUPER_PAD_MAX_ANIMS = 4,   // pad animations per level (one in every retail track)
	SUPER_PAD_MAX_LAYOUTS = 8, // distinct low-detail pad textures per level
	SUPER_PAD_MAX_TRIGGERS = 64, // pad triggers per level (Turbo Track has 30)
	// Room the rest of the race may still need from the level pack after
	// level init. The copies are skipped rather than risk running it out.
	SUPER_PAD_PACK_RESERVE = 0x8000,
};

static struct
{
	struct Level *level;
	int numAnims;
	struct AnimTex *source[SUPER_PAD_MAX_ANIMS];
	struct AnimTex *copy[SUPER_PAD_MAX_ANIMS];
} s_superPads;

static int LevSuperTurboPads_IsPadTrigger(const struct QuadBlock *q, int stepMask)
{
	return (q->quadFlags & QUADBLOCK_FLAG_TRIGGER) != 0 && (q->terrain_type & stepMask) != 0;
}

static int LevSuperTurboPads_Covers(const struct QuadBlock *q, const struct QuadBlock *trigger)
{
	return CtrExtras_FootprintCover(trigger->bbox.min.x, trigger->bbox.max.x, trigger->bbox.min.z, trigger->bbox.max.z, q->bbox.min.x,
	                                q->bbox.max.x, q->bbox.min.z, q->bbox.max.z);
}

static int LevSuperTurboPads_SameHeight(const struct QuadBlock *q, const struct QuadBlock *trigger)
{
	return CtrExtras_QuadAtTriggerHeight(q->bbox.min.y + q->bbox.max.y, trigger->bbox.min.y + trigger->bbox.max.y);
}

static int LevSuperTurboPads_FindAnim(struct AnimTex *const *list, int count, const struct AnimTex *anim)
{
	for (int i = 0; i < count; i++)
		if (list[i] == anim)
			return i;
	return -1;
}

static void LevSuperTurboPads_CyanLayout(struct TextureLayout *dst, const struct TextureLayout *src)
{
	*dst = *src;
	dst->clut = (u16)(dst->clut | CTR_EXTRAS_CYAN_CLUT_BIT);
}

// A cyan copy of one pad animation: the AnimTex header, its frame table, and
// one IconGroup4 per frame. Laid out as the level stores it, so the tagged
// pointer (copy | 1) resolves exactly like the original.
static struct AnimTex *LevSuperTurboPads_CopyAnim(const struct AnimTex *src)
{
	const int frames = src->numFrames;
	struct AnimTex *copy = MEMPACK_AllocMem(sizeof(struct AnimTex) + frames * (int)sizeof(struct IconGroup4 *) +
	                                        frames * (int)sizeof(struct IconGroup4));
	struct IconGroup4 **dstFrames = (ANIMTEX_GETARRAY(copy));
	struct IconGroup4 *groups = (struct IconGroup4 *)&dstFrames[frames];
	struct IconGroup4 **srcFrames = (ANIMTEX_GETARRAY(src));

	*copy = *src;
	for (int f = 0; f < frames; f++)
	{
		LevSuperTurboPads_CyanLayout(&groups[f].far, &srcFrames[f]->far);
		LevSuperTurboPads_CyanLayout(&groups[f].middle, &srcFrames[f]->middle);
		LevSuperTurboPads_CyanLayout(&groups[f].near, &srcFrames[f]->near);
		LevSuperTurboPads_CyanLayout(&groups[f].mosaic, &srcFrames[f]->mosaic);
		dstFrames[f] = &groups[f];
	}
	copy->ptrActiveTex = (int *)dstFrames[src->frameCurr % frames];
	return copy;
}

void LevSuperTurboPads_Mark(struct GameTracker *gGT)
{
	s_superPads.level = NULL;
	s_superPads.numAnims = 0;

	struct Level *level = gGT->level1;
	if (!g_config.superPadColour || level == NULL || level->ptr_mesh_info == NULL)
		return;

	struct mesh_info *mesh = level->ptr_mesh_info;
	struct QuadBlock *quads = mesh->ptrQuadBlockArray;
	const int numQuads = mesh->numQuadBlock;
	const int allPadsSuper = (gGT->gameMode2 & CHEAT_TURBOPAD) != 0;
	const int superMask = allPadsSuper ? COLL_STEP_TRIGGER_TURBO_PAD_MASK : COLL_STEP_TRIGGER_SUPER_TURBO_PAD;

	const struct QuadBlock *triggers[SUPER_PAD_MAX_TRIGGERS];
	int numTriggers = 0;
	for (int t = 0; t < numQuads && numTriggers < SUPER_PAD_MAX_TRIGGERS; t++)
		if (LevSuperTurboPads_IsPadTrigger(&quads[t], COLL_STEP_TRIGGER_TURBO_PAD_MASK))
			triggers[numTriggers++] = &quads[t];

	// The pad animations: animated textures on quads laid over a pad trigger.
	struct AnimTex *padAnims[SUPER_PAD_MAX_ANIMS];
	int numPadAnims = 0;
	for (int t = 0; t < numTriggers; t++)
	{
		const struct QuadBlock *trigger = triggers[t];
		for (int i = 0; i < numQuads; i++)
		{
			const struct QuadBlock *q = &quads[i];
			if ((q->quadFlags & QUADBLOCK_FLAG_TRIGGER) != 0 || !LevSuperTurboPads_SameHeight(q, trigger) ||
			    !CtrExtras_QuadIsPadOverlay(LevSuperTurboPads_Covers(q, trigger)))
				continue;
			for (int face = 0; face < 4; face++)
			{
				uintptr_t p = (uintptr_t)q->ptr_texture_mid[face];
				struct AnimTex *anim = (struct AnimTex *)(p - 1);
				if ((p & 1) != 0 && numPadAnims < SUPER_PAD_MAX_ANIMS && LevSuperTurboPads_FindAnim(padAnims, numPadAnims, anim) < 0)
					padAnims[numPadAnims++] = anim;
			}
		}
	}
	if (numPadAnims == 0)
		return;

	// Size the copies before taking any memory, and leave the pack its reserve.
	int bytes = 0;
	for (int a = 0; a < numPadAnims; a++)
		bytes += (int)sizeof(struct AnimTex) + padAnims[a]->numFrames * (int)(sizeof(struct IconGroup4 *) + sizeof(struct IconGroup4)) + 4;
	bytes += SUPER_PAD_MAX_LAYOUTS * ((int)sizeof(struct TextureLayout) + 4);
	if (MEMPACK_GetFreeBytes() < bytes + SUPER_PAD_PACK_RESERVE)
		return;

	struct AnimTex *copies[SUPER_PAD_MAX_ANIMS] = {0};
	const struct TextureLayout *lowSource[SUPER_PAD_MAX_LAYOUTS];
	struct TextureLayout *lowCopy[SUPER_PAD_MAX_LAYOUTS];
	int numLow = 0;

	// Every visible face that shows a pad animation over a super pad trigger.
	for (int i = 0; i < numQuads; i++)
	{
		struct QuadBlock *q = &quads[i];
		if ((q->quadFlags & QUADBLOCK_FLAG_TRIGGER) != 0)
			continue;

		int overSuperPad = allPadsSuper;
		for (int t = 0; t < numTriggers && !overSuperPad; t++)
		{
			const struct QuadBlock *trigger = triggers[t];
			overSuperPad = LevSuperTurboPads_IsPadTrigger(trigger, superMask) && LevSuperTurboPads_SameHeight(q, trigger) &&
			               LevSuperTurboPads_Covers(q, trigger) > 0;
		}
		if (!overSuperPad)
			continue;

		int cyanFaces = 0;
		for (int face = 0; face < 4; face++)
		{
			uintptr_t p = (uintptr_t)q->ptr_texture_mid[face];
			int a = (p & 1) != 0 ? LevSuperTurboPads_FindAnim(padAnims, numPadAnims, (struct AnimTex *)(p - 1)) : -1;
			if (a < 0)
				continue;
			if (copies[a] == NULL)
				copies[a] = LevSuperTurboPads_CopyAnim(padAnims[a]);
			q->ptr_texture_mid[face] = (void *)((uintptr_t)copies[a] | 1);
			cyanFaces++;
		}

		// The low-detail texture is one picture of the whole quad, so it turns
		// cyan only when the whole quad is pad.
		const struct TextureLayout *low = q->ptr_texture_low;
		if (cyanFaces == 4 && low != NULL)
		{
			int l = 0;
			while (l < numLow && lowSource[l] != low)
				l++;
			if (l == numLow && numLow < SUPER_PAD_MAX_LAYOUTS)
			{
				lowSource[numLow] = low;
				lowCopy[numLow] = MEMPACK_AllocMem(sizeof(struct TextureLayout));
				LevSuperTurboPads_CyanLayout(lowCopy[numLow], low);
				numLow++;
			}
			if (l < numLow)
				q->ptr_texture_low = lowCopy[l];
		}
	}

	for (int a = 0; a < numPadAnims; a++)
	{
		if (copies[a] == NULL)
			continue;
		s_superPads.source[s_superPads.numAnims] = padAnims[a];
		s_superPads.copy[s_superPads.numAnims] = copies[a];
		s_superPads.numAnims++;
	}
	s_superPads.level = level;

#ifdef CTR_INTERNAL
	NativeDev_QuadDump(mesh); // CTR_DEV_QUAD_DUMP: again, with the cyan faces
#endif
}

// After CTR_CycleTex_LEV each frame: show the cyan frame matching the frame
// the original animation is on.
void LevSuperTurboPads_Cycle(struct GameTracker *gGT)
{
	if (s_superPads.level == NULL || s_superPads.level != gGT->level1)
		return;
	for (int a = 0; a < s_superPads.numAnims; a++)
	{
		const struct AnimTex *src = s_superPads.source[a];
		struct AnimTex *copy = s_superPads.copy[a];
		copy->frameCurr = src->frameCurr;
		copy->ptrActiveTex = (int *)(ANIMTEX_GETARRAY(copy))[src->frameCurr];
	}
}

#endif
