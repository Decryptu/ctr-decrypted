// The title screen's Roo: Roo Tirazoo, the project's icon, dances where retail
// has Crash catch the trophy (MM_Title.c). Crash's title model and its
// animation exist only for Crash, and models carry per-frame vertex animation
// with no skeleton, so Roo comes in with his own podium dance instead.
//
// Both files are read from the player's disc on first use, as the podium
// would (LOAD_TenStages.c, podium first place), but without the load queue:
// - the podium texture file for pack 1 (BI_PODIUMVRMS). Its first block,
//   VRAM (512, 0) 384 x 256, holds every texel and palette Roo samples. It
//   goes to the same place in the racer VRAM bank, not VRAM, so the main
//   menu's own textures stay as they are;
// - Roo's win dance model for pack 1, one 90-frame animation "dance". Its
//   texture layouts are tagged NATIVE_TPAGE_RACER_BANK so they sample the bank.
// Either read failing leaves retail Crash and his trophy in place. Once the
// logo settles he gives one of his race laughs (AP_TitleRoo_Tick).

#ifdef CTR_AP

#include <common.h>
#include <stdlib.h>

#include "ap_title_roo.h"
#include "ap_retail_asset.h"
#include "ap_hooks.h" // AP_LogLine

#include <platform/native_gpu.h>    // NATIVE_TPAGE_RACER_BANK
#include <platform/native_racers.h> // NativeRacers_UploadBank

enum
{
	AP_TITLE_ROO_PACK = 1,
	AP_TITLE_ROO_DANCE = STATIC_ROODANCE - STATIC_CRASHDANCE,
	AP_TITLE_ROO_VRM_BYTES = 240 * 1024,
	AP_TITLE_ROO_MODEL_BYTES = 128 * 1024,
	// The one podium texture block Roo samples, and where it lands in the bank.
	AP_TITLE_ROO_TEX_X = 512,
	AP_TITLE_ROO_TEX_Y = 0,
	AP_TITLE_ROO_TEX_W = 384,
	AP_TITLE_ROO_TEX_H = 256,
	// 90% of the title's model scale: at full size Roo stands taller than
	// Crash did (owner's call).
	AP_TITLE_ROO_SCALE = TITLE_MATRIX_SCALE * 9 / 10,
	// His voice set 5, the laugh a racer gives when an item hits someone
	// (data.voiceID maps that voice line to set 5): GAME XA 0xC8 or 0xC9.
	AP_TITLE_ROO_LAUGH_SET = 5,
};

static struct Model *s_titleRoo;
static int s_titleRooTried;
static int s_titleRooLaughed;

// Upload the block of a packed VRAM file that is exactly Roo's area; 1 when found.
static int AP_TitleRoo_UploadTextures(const u8 *file, int size)
{
	int cur = 4;

	if (size < 8 || *(const u32 *)file != 0x20)
		return 0;

	while (cur + 4 <= size)
	{
		const int blockSize = *(const int *)(file + cur);
		const s16 *rect;

		cur += 4;
		if (blockSize <= 0x14 || cur + blockSize > size)
			return 0;

		rect = (const s16 *)(file + cur + 0x0C);
		if (rect[0] == AP_TITLE_ROO_TEX_X && rect[1] == AP_TITLE_ROO_TEX_Y && rect[2] == AP_TITLE_ROO_TEX_W &&
		    rect[3] == AP_TITLE_ROO_TEX_H && 0x14 + AP_TITLE_ROO_TEX_W * AP_TITLE_ROO_TEX_H * 2 <= blockSize)
		{
			NativeRacers_UploadBank(AP_TITLE_ROO_TEX_X, AP_TITLE_ROO_TEX_Y, AP_TITLE_ROO_TEX_W, AP_TITLE_ROO_TEX_H,
			                        (const u16 *)(file + cur + 0x14));
			return 1;
		}
		cur += blockSize & ~3;
	}
	return 0;
}

// Point every texture layout the model's draw commands use at the bank.
static void AP_TitleRoo_TagLayouts(struct Model *model)
{
	for (int h = 0; h < model->numHeaders; h++)
	{
		struct ModelHeader *header = &model->headers[h];
		const u32 *cmd = (const u32 *)header->ptrCommandList;

		if (cmd == NULL || header->ptrTexLayout == NULL)
			continue;

		// The commands start after the list's first word (tools/racers/ctrmodel.py).
		for (cmd++; *cmd != 0xFFFFFFFF; cmd++)
		{
			const int index = *cmd & 0x1FF;

			if ((*cmd & 0xFFFF0000) && index && header->ptrTexLayout[index - 1])
				header->ptrTexLayout[index - 1]->tpage |= NATIVE_TPAGE_RACER_BANK;
		}
	}
}

struct Model *AP_TitleRoo_Model(void)
{
	u8 *vrm, *model;
	int vrmSize = 0;

	if (s_titleRooTried)
		return s_titleRoo;
	s_titleRooTried = 1;

	vrm = (u8 *)malloc(AP_TITLE_ROO_VRM_BYTES);
	model = (u8 *)malloc(AP_TITLE_ROO_MODEL_BYTES);
	if (vrm == NULL || model == NULL)
		goto fail;

	if (!AP_RetailAsset_ReadSubfile(BI_PODIUMVRMS + AP_TITLE_ROO_PACK - 1, 0, vrm, AP_TITLE_ROO_VRM_BYTES, &vrmSize) ||
	    !AP_TitleRoo_UploadTextures(vrm, vrmSize))
		goto fail;

	s_titleRoo = (struct Model *)AP_RetailAsset_ReadSubfile(
	    BI_DANCEMODELWIN + AP_TITLE_ROO_DANCE * LOAD_PODIUM_MODEL_FILE_STRIDE + AP_TITLE_ROO_PACK - 1, 1, model, AP_TITLE_ROO_MODEL_BYTES, NULL);
	if (s_titleRoo == NULL || s_titleRoo->numHeaders <= 0)
		goto fail;

	AP_TitleRoo_TagLayouts(s_titleRoo);
	free(vrm);
	// The model stays for the run, in its file buffer.
	return s_titleRoo;

fail:
	AP_LogLine("[AP] Title Roo: podium files unreadable, keeping Crash\n");
	s_titleRoo = NULL;
	free(vrm);
	free(model);
	return NULL;
}

void AP_TitleRoo_Reset(void)
{
	s_titleRooLaughed = 0;
}

void AP_TitleRoo_Tick(struct Instance *inst, int settled)
{
	static u32 frame;
	const s32 numFrames = INSTANCE_GetNumAnimFrames(inst, 0);

	// One laugh once the logo settles, straight through CDSYS_XAPlay: the
	// Voiceline queue is off in menus, and the main menu loads no character
	// bank, but XA needs none. The main menu's music is sequenced, not XA, so
	// nothing else is using the stream.
	if (settled && !s_titleRooLaughed && (sdata->XA_State == XA_IDLE))
	{
		const s16 *laugh = (const s16 *)data.voiceData[RIPPER_ROO].voiceSet[AP_TITLE_ROO_LAUGH_SET].ptr;
		const int count = data.voiceData[RIPPER_ROO].voiceSet[AP_TITLE_ROO_LAUGH_SET].num;

		s_titleRooLaughed = 1;
		if (count > 0)
			CDSYS_XAPlay(CDSYS_XA_TYPE_GAME, (u16)laugh[(GAME_TRACKER->frameTimer_VsyncCallback >> 1) % (u32)count]);
	}

	inst->matrix.m[0][0] = inst->matrix.m[1][1] = inst->matrix.m[2][2] = AP_TITLE_ROO_SCALE;
	inst->animIndex = 0;
	inst->animFrame = (numFrames > 0) ? (s16)((frame++ / FPS_DOUBLE(1)) % (u32)numFrames) : 0;}

#endif // CTR_AP
