#include <platform/native_track_minimap.h>
#include <platform/native_racers.h>
#include <platform/native_psx_space.h>
#include <platform/native_assets.h>
#include <platform/native_fs_utf8.h>
#include <platform/native_gpu.h>
#include <platform/native_renderer.h>
#include <platform/native_spu_memory.h>

// Custom racers at run time (docs/CUSTOM_RACERS.md): the installed library,
// each racer's models and its portrait.
//
// - The library is read once, on first use, from assets/racers
//   (NativeRacerLibrary_Scan, package builds only). CTR_DEV_FAKE_RACERS=n
//   replaces it with n placeholder racers that borrow the first eight retail
//   racers' looks, to fill the character select in any build.
// - Models are the racer's own model file, loaded like the game loads a DRAM
//   file (the pointer map relocated) when first asked for. The menu copy of
//   a racer with wheels keeps its race size, so the menu's wheels touch the
//   kart as in a race; one without wheels is scaled 5/8, as N. Oxide's.
// - Portraits live in the racer VRAM bank, a second VRAM sampled by
//   primitives whose tpage carries NATIVE_TPAGE_RACER_BANK. Portrait i takes a
//   64x32-texel slot: four across and eight down in a 4-bit page, from page 0
//   of the first page row. Its palette goes in page 15, four palettes per row.
//   Each racer gets an Icon whose layout points there, so every icon draw path
//   takes it unchanged.
// - The bank also holds the Mods screen's track picture: 15-bit texels from
//   VRAM (512, 256), page 8 of the second page row, which no portrait reaches,
//   and the title screen's Roo (ap/ap_title_roo.c) at x 512..895 of the first
//   page row, which 64 portraits (pages 0 and 1) never reach either.

#if defined(CTR_CUSTOM_PACKAGES)
#include <platform/native_racer_library.h>
#include <platform/native_mods_off.h>
#endif

enum
{
	NATIVE_RACERS_MENU_SCALE_NUM = 5,
	NATIVE_RACERS_MENU_SCALE_SHIFT = 3,
	NATIVE_RACERS_SLOT_W = 64,
	NATIVE_RACERS_SLOT_H = 32,
	NATIVE_RACERS_SLOTS_PER_PAGE = 32,
	NATIVE_RACERS_PORTRAIT_PAGES = 15,
	NATIVE_RACERS_CLUT_X = 960,
	// Retail portraits: 4-bit, semi-transparency mode 3 (tpage 0x6n).
	NATIVE_RACERS_PORTRAIT_TPAGE = 0x60,
};

struct NativeRacerSample
{
	char path[512];
	u32 bytes;
	u8 flags;
	u8 volume;
	u16 pitch;
	u16 duration;
};

struct NativeRacer
{
	int templateID;
	int engine;
	int wheels;
	int mask; // -1 template default, 0 Uka Uka, 1 Aku Aku
	char name[32];
	char shortName[16];
	char modelPath[512];
	char portraitPath[512];
	struct NativeRacerSample sampledVoices[NATIVE_RACERS_SAMPLED_VOICE_COUNT];
	u32 sampledVoiceSpuAddr[NATIVE_RACERS_SAMPLED_VOICE_COUNT];
	struct Model *menuModel;
	struct Model *raceModel;
	struct Icon portrait;
	u8 menuModelFailed;
	u8 raceModelFailed;
	u8 hasPortrait;
};

#define NATIVE_RACERS_MAX 64
_Static_assert(NATIVE_RACERS_MAX <= NATIVE_RACERS_PORTRAIT_PAGES * NATIVE_RACERS_SLOTS_PER_PAGE,
               "every portrait fits the first page row, clear of the track picture");

static struct NativeRacer s_racers[NATIVE_RACERS_MAX];
static int s_racerCount = -1;
static int s_portraitsUploaded;
static signed char s_playerRacer[4] = {-1, -1, -1, -1};
static signed char s_botRacer[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int s_activeVoiceDriver = -1;

static void NativeRacers_Load(void)
{
	if (s_racerCount >= 0)
		return;
	s_racerCount = 0;

	const int fake = NativeDev_EnvInt("CTR_DEV_FAKE_RACERS", 0);
	if (fake > 0)
	{
		s_racerCount = (fake > NATIVE_RACERS_MAX) ? NATIVE_RACERS_MAX : fake;
		for (int r = 0; r < s_racerCount; r++)
		{
			s_racers[r].templateID = r % 8;
			s_racers[r].engine = -1;
			s_racers[r].wheels = 1;
			s_racers[r].mask = -1;
		}
		return;
	}

#if defined(CTR_CUSTOM_PACKAGES)
	static struct NativeRacerEntry entries[NATIVE_RACERS_MAX];
	char dir[1024];
	snprintf(dir, sizeof dir, "%s/racers", NativeAssets_GetAssetDir());
	const int count = NativeRacerLibrary_Scan(dir, entries, NATIVE_RACERS_MAX);
	for (int r = 0; r < count; r++)
	{
		// Turned off on the Mods screen: installed, but not on the character select.
		if (NativeModsOff_Is(NATIVE_MODS_RACER, entries[r].id))
			continue;
		struct NativeRacer *racer = &s_racers[s_racerCount++];
		racer->templateID = entries[r].templateID;
		racer->engine = entries[r].engine;
		racer->wheels = entries[r].wheels;
		racer->mask = entries[r].mask;
		snprintf(racer->name, sizeof racer->name, "%s", entries[r].name);
		snprintf(racer->shortName, sizeof racer->shortName, "%s", entries[r].shortName);
		snprintf(racer->modelPath, sizeof racer->modelPath, "%s", entries[r].modelPath);
		snprintf(racer->portraitPath, sizeof racer->portraitPath, "%s", entries[r].portraitPath);
		for (int v = 0; v < NATIVE_RACERS_SAMPLED_VOICE_COUNT; v++)
		{
			if (entries[r].sampledVoices[v].bytes == 0)
				continue;
			snprintf(racer->sampledVoices[v].path, sizeof racer->sampledVoices[v].path, "%s", entries[r].sampledVoices[v].path);
			racer->sampledVoices[v].bytes = (u32)entries[r].sampledVoices[v].bytes;
			racer->sampledVoices[v].flags = entries[r].sampledVoices[v].flags;
			racer->sampledVoices[v].volume = entries[r].sampledVoices[v].volume;
			racer->sampledVoices[v].pitch = entries[r].sampledVoices[v].pitch;
			racer->sampledVoices[v].duration = entries[r].sampledVoices[v].duration;
		}
	}
	if (count > 0)
		printf("[Racers] %d custom racer(s) from %s\n", count, dir);
#endif
}

// Read the library again (the Mods screen installed or removed a racer).
// Called from the menus only: no race holds a racer model then. The players'
// picks are cleared, since racer indices change.
void NativeRacers_Reload(void)
{
	for (int r = 0; r < s_racerCount; r++)
	{
		// A model is the file buffer less its leading u32 pointer-map offset.
		if (s_racers[r].menuModel)
			PsxSpace_Free((u32 *)s_racers[r].menuModel - 1);
		if (s_racers[r].raceModel)
			PsxSpace_Free((u32 *)s_racers[r].raceModel - 1);
	}
	memset(s_racers, 0, sizeof s_racers);
	s_racerCount = -1;
	s_portraitsUploaded = 0;
	for (int p = 0; p < 4; p++)
		s_playerRacer[p] = -1;
	for (int p = 0; p < 8; p++)
		s_botRacer[p] = -1;
}

static int NativeRacers_Valid(int racer)
{
	NativeRacers_Load();
	return (racer >= 0) && (racer < s_racerCount);
}

int NativeRacers_Count(void)
{
	NativeRacers_Load();
	return s_racerCount;
}

int NativeRacers_Template(int racer)
{
	return NativeRacers_Valid(racer) ? s_racers[racer].templateID : CRASH_BANDICOOT;
}

int NativeRacers_Engine(int racer)
{
	return (NativeRacers_Valid(racer) && (s_racers[racer].engine >= 0)) ? s_racers[racer].engine
	                                                                     : GAME_CHARACTER_METADATA[NativeRacers_Template(racer)].engineID;
}

int NativeRacers_Wheels(int racer)
{
	return NativeRacers_Valid(racer) ? s_racers[racer].wheels : 1;
}

int NativeRacers_MaskGoodGuy(int racer, int fallback)
{
	if (!NativeRacers_Valid(racer) || (s_racers[racer].mask < 0))
		return fallback != 0;
	return s_racers[racer].mask != 0;
}

const char *NativeRacers_Name(int racer)
{
	return (NativeRacers_Valid(racer) && s_racers[racer].name[0]) ? s_racers[racer].name : NULL;
}

const char *NativeRacers_ShortName(int racer)
{
	return (NativeRacers_Valid(racer) && s_racers[racer].shortName[0]) ? s_racers[racer].shortName : NULL;
}

// Read a racer model file and relocate it, as the game loads a DRAM file:
// a u32 pointer-map offset, the model, then the pointer map. The buffer is in
// PSX space, since the model's pointers are PSX addresses.
static struct Model *NativeRacers_ReadModel(const char *path, int menuScale)
{
	FILE *f = NativeFs_OpenRead(path);
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	u32 *file = (size > 8) ? (u32 *)PsxSpace_Alloc((size_t)size + 4) : NULL;
	if ((file == NULL) || (fread(file, 1, (size_t)size, f) != (size_t)size))
	{
		fclose(f);
		PsxSpace_Free(file);
		return NULL;
	}
	fclose(f);

	const int ptrMapOffset = (int)file[0];
	char *model = (char *)&file[1];
	if ((ptrMapOffset <= 0) || ((long)ptrMapOffset + 8 > size - 4))
	{
		PsxSpace_Free(file);
		return NULL;
	}
	struct DramPointerMap *dpm = (struct DramPointerMap *)&model[ptrMapOffset];
	if ((long)ptrMapOffset + 4 + (long)dpm->numBytes > size - 4)
	{
		PsxSpace_Free(file);
		return NULL;
	}
	LOAD_RunPtrMap(model, (int *)DRAM_GETOFFSETS(dpm), dpm->numBytes >> 2);

	struct Model *m = (struct Model *)model;
	if (menuScale)
	{
		for (int i = 0; i < m->numHeaders; i++)
		{
			struct ModelHeader *header = &PSX_GET(m->headers)[i];
			header->scale.x = (s16)((header->scale.x * NATIVE_RACERS_MENU_SCALE_NUM) >> NATIVE_RACERS_MENU_SCALE_SHIFT);
			header->scale.y = (s16)((header->scale.y * NATIVE_RACERS_MENU_SCALE_NUM) >> NATIVE_RACERS_MENU_SCALE_SHIFT);
			header->scale.z = (s16)((header->scale.z * NATIVE_RACERS_MENU_SCALE_NUM) >> NATIVE_RACERS_MENU_SCALE_SHIFT);
		}
	}
	return m;
}

struct Model *NativeRacers_MenuModel(int racer)
{
	if (!NativeRacers_Valid(racer) || !s_racers[racer].modelPath[0] || s_racers[racer].menuModelFailed)
		return NULL;
	struct NativeRacer *r = &s_racers[racer];
	if (r->menuModel == NULL)
	{
		r->menuModel = NativeRacers_ReadModel(r->modelPath, !r->wheels);
		r->menuModelFailed = (r->menuModel == NULL);
	}
	return r->menuModel;
}

struct Model *NativeRacers_RaceModel(int racer)
{
	if (!NativeRacers_Valid(racer) || !s_racers[racer].modelPath[0] || s_racers[racer].raceModelFailed)
		return NULL;
	struct NativeRacer *r = &s_racers[racer];
	if (r->raceModel == NULL)
	{
		r->raceModel = NativeRacers_ReadModel(r->modelPath, 0);
		r->raceModelFailed = (r->raceModel == NULL);
	}
	return r->raceModel;
}

// The racer bank, one for the run; a reload writes over its slots.
static TextureID NativeRacers_Bank(void)
{
	static TextureID bank;
	static int bankCreated;
	if (!bankCreated)
	{
		bank = NativeRenderer_CreateVramBank();
		bankCreated = 1;
		NativeGpu_SetRacerBankTexture(bank);
	}
	return bank;
}

void NativeRacers_UploadBank(int x, int y, int width, int height, const u16 *texels)
{
	NativeRenderer_UploadVramBank(NativeRacers_Bank(), x, y, width, height, texels);
}

struct Icon *NativeRacers_TrackPicture(const u16 *texels, int width, int height)
{
	static struct Icon picture;
	if (!texels || (width < 1) || (width > 256) || (height < 1) || (height > 256))
		return NULL;
	NativeRenderer_UploadVramBank(NativeRacers_Bank(), 512, 256, width, height, texels);
	struct TextureLayout *t = &picture.texLayout;
	t->u0 = t->u2 = 0;
	t->u1 = t->u3 = (u8)width;
	t->v0 = t->v1 = 0;
	t->v2 = t->v3 = (u8)height;
	t->clut = 0;
	// 15-bit direct colour (tpage bits 7-8 = 2), page 8 of the second row.
	t->tpage = (u16)(NATIVE_TPAGE_RACER_BANK | (2 << 7) | (1 << 4) | 8);
	snprintf(picture.name, sizeof picture.name, "trackpicture");
	return &picture;
}

void NativeRacers_TrackMinimap(const struct NativeTrackMinimap *map, struct Icon **top, struct Icon **bottom)
{
	static struct Icon halves[2];
	const int h = map->topHeight + map->bottomHeight;
	NativeRenderer_UploadVramBank(NativeRacers_Bank(), 640, 256, NATIVE_TRACK_MINIMAP_SIDE, h, map->texels);
	for (int i = 0; i < 2; i++)
	{
		struct TextureLayout *t = &halves[i].texLayout;
		const int w = i ? map->bottomWidth : map->topWidth;
		const int v = i ? map->topHeight : 0;
		const int vh = i ? map->bottomHeight : map->topHeight;
		// u starts at 128: x 640 is 128 texels into page 8 (15-bit, 256 wide).
		t->u0 = t->u2 = 128;
		t->u1 = t->u3 = (u8)(128 + w);
		t->v0 = t->v1 = (u8)v;
		t->v2 = t->v3 = (u8)(v + vh);
		t->clut = 0;
		t->tpage = (u16)(NATIVE_TPAGE_RACER_BANK | (2 << 7) | (1 << 4) | 8);
		snprintf(halves[i].name, sizeof halves[i].name, "trackmap%d", i);
	}
	*top = &halves[0];
	*bottom = &halves[1];
}

// Every portrait into the racer bank, and an Icon per racer pointing at it.
static void NativeRacers_UploadPortraits(void)
{
	if (s_portraitsUploaded)
		return;
	s_portraitsUploaded = 1;

	int any = 0;
	for (int r = 0; r < s_racerCount; r++)
		any |= (s_racers[r].portraitPath[0] != 0);
	if (!any)
		return;

	const TextureID bank = NativeRacers_Bank();

	for (int r = 0; r < s_racerCount; r++)
	{
		struct NativeRacer *racer = &s_racers[r];
		if (!racer->portraitPath[0] || (r >= NATIVE_RACERS_PORTRAIT_PAGES * 2 * NATIVE_RACERS_SLOTS_PER_PAGE))
			continue;

		u8 file[4 + 4 + 16 * 2 + 11 * 26 * 2];
		FILE *f = NativeFs_OpenRead(racer->portraitPath);
		const size_t got = f ? fread(file, 1, sizeof file, f) : 0;
		if (f)
			fclose(f);
		u16 w, h;
		memcpy(&w, &file[4], 2);
		memcpy(&h, &file[6], 2);
		if ((got != sizeof file) || memcmp(file, "CDP1", 4) || (w != 44) || (h != 26))
			continue;
		const u16 *palette = (const u16 *)&file[8];
		const u16 *texels = (const u16 *)&file[8 + 32];

		const int page = r / NATIVE_RACERS_SLOTS_PER_PAGE;
		const int inPage = r % NATIVE_RACERS_SLOTS_PER_PAGE;
		const int pageX = page % NATIVE_RACERS_PORTRAIT_PAGES;
		const int pageY = page / NATIVE_RACERS_PORTRAIT_PAGES;
		const int u = (inPage % 4) * NATIVE_RACERS_SLOT_W;
		const int v = (inPage / 4) * NATIVE_RACERS_SLOT_H;
		NativeRenderer_UploadVramBank(bank, pageX * 64 + u / 4, pageY * 256 + v, 11, 26, texels);

		const int clutX = NATIVE_RACERS_CLUT_X + (r % 4) * 16;
		const int clutY = r / 4;
		NativeRenderer_UploadVramBank(bank, clutX, clutY, 16, 1, palette);

		struct TextureLayout *t = &racer->portrait.texLayout;
		t->u0 = t->u2 = (u8)u;
		t->u1 = t->u3 = (u8)(u + w - 1);
		t->v0 = t->v1 = (u8)v;
		t->v2 = t->v3 = (u8)(v + h - 1);
		t->clut = (u16)((clutY << 6) | (clutX >> 4));
		t->tpage = (u16)(NATIVE_TPAGE_RACER_BANK | NATIVE_RACERS_PORTRAIT_TPAGE | (pageY << 4) | pageX);
		snprintf(racer->portrait.name, sizeof racer->portrait.name, "racer%d", r);
		racer->hasPortrait = 1;
	}
}

struct Icon *NativeRacers_Portrait(int racer)
{
	if (!NativeRacers_Valid(racer))
		return NULL;
	NativeRacers_UploadPortraits();
	return s_racers[racer].hasPortrait ? &s_racers[racer].portrait : NULL;
}

void NativeRacers_SetPlayer(int player, int racer)
{
	if ((player >= 0) && (player < 4))
		s_playerRacer[player] = (signed char)(NativeRacers_Valid(racer) ? racer : -1);
}

int NativeRacers_Player(int player)
{
	return ((player >= 0) && (player < 4)) ? s_playerRacer[player] : -1;
}

int NativeRacers_Driver(int driverID)
{
	struct GameTracker *gGT = sdata->gGT;
	if ((gGT == NULL) || (driverID < 0) || (driverID >= 8) ||
	    ((gGT->gameMode1 & (ADVENTURE_MODE | MAIN_MENU)) != 0))
		return -1;
	const int racer = driverID < gGT->numPlyrCurrGame && driverID < 4 ? s_playerRacer[driverID] : s_botRacer[driverID];
	return (NativeRacers_Valid(racer) && (s_racers[racer].templateID == GAME_CHARACTER_IDS[driverID])) ? racer : -1;
}

void NativeRacers_SetBot(int driver, int racer)
{
	if (driver >= 0 && driver < 8)
		s_botRacer[driver] = (signed char)(NativeRacers_Valid(racer) ? racer : -1);
}

int NativeRacers_DriverEngine(int driverID, int retailEngine)
{
	const int racer = NativeRacers_Driver(driverID);
	return (racer >= 0) ? NativeRacers_Engine(racer) : retailEngine;
}

struct Icon *NativeRacers_DriverIcon(int driverID, struct Icon *retailIcon)
{
	struct Icon *icon = NativeRacers_Portrait(NativeRacers_Driver(driverID));
	return icon ? icon : retailIcon;
}

static int NativeRacers_SampledVoiceSlotForSound(const struct NativeRacer *racer, int soundID)
{
	if (racer == NULL)
		return -1;
	if (soundID == racer->templateID + 0x1c)
		return 0;
	if (soundID == racer->templateID + 0x2c)
		return 1;
	return -1;
}

void NativeRacers_SetActiveVoiceDriver(int driverID)
{
	s_activeVoiceDriver = ((driverID >= 0) && (driverID < 8)) ? driverID : -1;
}

void NativeRacers_LoadSelectedSamplesToSpu(void)
{
	NativeRacers_Load();
	for (int r = 0; r < s_racerCount; r++)
		memset(s_racers[r].sampledVoiceSpuAddr, 0, sizeof s_racers[r].sampledVoiceSpuAddr);

#if CTR_SPU_TOTAL_BYTES > CTR_SPU_RACER_START
	u64 selectedMask = 0;
	for (int driverID = 0; driverID < 8; driverID++)
	{
		const int racer = NativeRacers_Driver(driverID);
		if ((racer < 0) || (racer >= s_racerCount))
			continue;
		for (int v = 0; v < NATIVE_RACERS_SAMPLED_VOICE_COUNT; v++)
		{
			if (s_racers[racer].sampledVoices[v].bytes != 0)
			{
				selectedMask |= (u64)1 << racer;
				break;
			}
		}
	}
	if (selectedMask == 0)
		return;

	u32 cursor = CTR_SPU_RACER_START;
	for (int r = 0; r < s_racerCount; r++)
	{
		if ((selectedMask & ((u64)1 << r)) == 0)
			continue;
		struct NativeRacer *racer = &s_racers[r];
		for (int v = 0; v < NATIVE_RACERS_SAMPLED_VOICE_COUNT; v++)
		{
			const struct NativeRacerSample *sample = &racer->sampledVoices[v];
			if ((sample->bytes == 0) || !sample->path[0])
				continue;
			const u32 alignedSize = (sample->bytes + 15u) & ~15u;
			if ((cursor >= (u32)CTR_SPU_TOTAL_BYTES) || (alignedSize > (u32)CTR_SPU_TOTAL_BYTES - cursor))
			{
				fprintf(stderr, "[Racers] Not enough SPU RAM for sampled voice %d of %s; using retail audio.\n", v, racer->name);
				continue;
			}
			FILE *f = NativeFs_OpenRead(sample->path);
			u8 *buf = f ? (u8 *)malloc(sample->bytes) : NULL;
			const size_t got = (f && buf) ? fread(buf, 1, sample->bytes, f) : 0;
			if (f)
				fclose(f);
			if ((buf != NULL) && (got == sample->bytes) &&
			    (SpuSetTransferStartAddr(cursor) != 0) && (SpuWrite(buf, sample->bytes) == sample->bytes))
			{
				racer->sampledVoiceSpuAddr[v] = cursor;
				printf("[Racers] Loaded sampled voice %d for %s at SPU 0x%x (%u bytes)\n", v, racer->name, cursor, sample->bytes);
				cursor += alignedSize;
			}
			free(buf);
		}
	}
#endif
}

int NativeRacers_InitSampledVoiceChannelAttr(int racerIndex, int soundID, struct ChannelAttr *attr, int vol, int LR, int distort)
{
	if (!NativeRacers_Valid(racerIndex) || (attr == NULL))
		return 0;
	const struct NativeRacer *racer = &s_racers[racerIndex];
	const int slot = NativeRacers_SampledVoiceSlotForSound(racer, soundID);
	if ((slot < 0) || (racer->sampledVoices[slot].bytes == 0) || (racer->sampledVoiceSpuAddr[slot] == 0))
		return 0;

	const struct NativeRacerSample *sample = &racer->sampledVoices[slot];
	const u32 masterVol = (sample->flags & 4) != 0 ? (u32)sdata->vol_Voice : (u32)sdata->vol_FX;
	Channel_SetVolume(attr, (masterVol * sample->volume * (u32)vol) >> 10, LR);

	u32 pitch = sample->pitch;
	if (distort != HOWL_SFX_DISTORTION_NONE)
		pitch = (pitch * (u32)GAME_DISTORT_CONST_OTHER_FX[distort]) >> 16;
	attr->pitch = (u16)pitch;
	attr->ad = 0x80ff;
	attr->sr = 0x1fc2;
	attr->spuStartAddr = racer->sampledVoiceSpuAddr[slot];
	return 1;
}

int NativeRacers_UpdateSampledVoiceVolume(int racerIndex, int soundID, struct ChannelAttr *attr, int vol, int LR)
{
	if (!NativeRacers_Valid(racerIndex) || (attr == NULL))
		return 0;
	const struct NativeRacer *racer = &s_racers[racerIndex];
	const int slot = NativeRacers_SampledVoiceSlotForSound(racer, soundID);
	if ((slot < 0) || (racer->sampledVoices[slot].bytes == 0))
		return 0;

	const struct NativeRacerSample *sample = &racer->sampledVoices[slot];
	const u32 masterVol = (sample->flags & 4) != 0 ? (u32)sdata->vol_Voice : (u32)sdata->vol_FX;
	Channel_SetVolume(attr, (masterVol * sample->volume * (u32)vol) >> 10, LR);
	return 1;
}

int NativeRacers_PlayDriverSampledVoice(int driverID, int voiceType, int characterID, int antiSpam, int *soundIDCount)
{
	if ((voiceType < 0) || (voiceType >= NATIVE_RACERS_SAMPLED_VOICE_COUNT) || (driverID < 0) || (driverID >= 8))
		return 0;
	if (GAME_AUDIO_ENABLED == 0)
		return 0;

	const int racerIndex = NativeRacers_Driver(driverID);
	if (!NativeRacers_Valid(racerIndex))
		return 0;
	const struct NativeRacer *racer = &s_racers[racerIndex];
	if (racer->templateID != characterID)
		return 0;

	const int soundID = characterID + (voiceType == 0 ? 0x1c : 0x2c);
	struct ChannelAttr channelAttr;
	const u32 flags = HOWL_SFX_DEFAULT_FLAGS;
	const u32 LR = HowlSfx_LR(flags);
	const u32 distortion = HowlSfx_Distortion(flags);
	const u32 volume = HowlSfx_Volume(flags);
	const u16 echo = (u16)HowlSfx_Echo(flags);
	if (!NativeRacers_InitSampledVoiceChannelAttr(racerIndex, soundID, &channelAttr, (int)volume, (int)LR, (int)distortion))
		return 0;
	channelAttr.reverb = echo;

	Smart_EnterCriticalSection();
	if ((antiSpam == 2) && (Channel_FindSound(soundID) != 0))
	{
		Smart_ExitCriticalSection();
		return 1;
	}

	struct ChannelStats *channel = Channel_AllocSlot_AntiSpam((u16)soundID, (u8)antiSpam, HOWL_CHANNEL_UPDATE_ALL_ATTRS, &channelAttr);
	if (channel == NULL)
	{
		Smart_ExitCriticalSection();
		return 1;
	}

	const struct NativeRacerSample *sample = &racer->sampledVoices[voiceType];
	if ((sample->flags & 2) != 0)
		channel->flags |= 4;
	channel->type = HOWL_CHANNEL_TYPE_OTHER_FX;
	channel->unk2 = (u8)(racerIndex + 1);
	channel->echo = echo;
	channel->vol = (u8)volume;
	channel->distort = (u8)distortion;
	channel->LR = (u8)LR;
	channel->timeLeft = (s16)sample->duration;
	channel->soundID = (CountSounds() << 16) | (soundID & 0xffff);
	channel->startFrame = GAME_TRACKER->frameTimer_MainFrame_ResetDB;
	if (soundIDCount != NULL)
		*soundIDCount = channel->soundID;
	Smart_ExitCriticalSection();
	return 1;
}

int NativeRacers_PlayActiveSampledVoice(int voiceType, int characterID)
{
	return NativeRacers_PlayDriverSampledVoice(s_activeVoiceDriver, voiceType, characterID, 2, NULL);
}
