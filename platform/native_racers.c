#include <platform/native_racers.h>
#include <platform/native_assets.h>
#include <platform/native_fs_utf8.h>
#include <platform/native_gpu.h>
#include <platform/native_renderer.h>

// Custom racers at run time (docs/CUSTOM_RACERS.md): the installed library,
// each racer's models and its portrait.
//
// - The library is read once, on first use, from assets/racers
//   (NativeRacerLibrary_Scan, package builds only). CTR_DEV_FAKE_RACERS=n
//   replaces it with n placeholder racers that borrow the first eight retail
//   racers' looks, to fill the character select in any build.
// - Models are the racer's own model file, loaded like the game loads a DRAM
//   file (the pointer map relocated) when first asked for. The menu copy is
//   scaled 5/8: the racer models are larger than the menu's own models.
// - Portraits live in the racer VRAM bank, a second VRAM sampled by
//   primitives whose tpage carries NATIVE_TPAGE_RACER_BANK. Portrait i takes a
//   64x32-texel slot: four across and eight down in a 4-bit page, pages 0-14
//   of both page rows. Its palette goes in page 15, four palettes per row.
//   Each racer gets an Icon whose layout points there, so every icon draw path
//   takes it unchanged.

#if defined(CTR_CUSTOM_PACKAGES)
#include <platform/native_racer_library.h>
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

struct NativeRacer
{
	int templateID;
	int engine;
	int wheels;
	char name[32];
	char shortName[16];
	char modelPath[512];
	char portraitPath[512];
	struct Model *menuModel;
	struct Model *raceModel;
	struct Icon portrait;
	u8 menuModelFailed;
	u8 raceModelFailed;
	u8 hasPortrait;
};

#define NATIVE_RACERS_MAX 64

static struct NativeRacer s_racers[NATIVE_RACERS_MAX];
static int s_racerCount = -1;
static int s_portraitsUploaded;
static signed char s_playerRacer[4] = {-1, -1, -1, -1};

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
		struct NativeRacer *racer = &s_racers[s_racerCount++];
		racer->templateID = entries[r].templateID;
		racer->engine = entries[r].engine;
		racer->wheels = entries[r].wheels;
		snprintf(racer->name, sizeof racer->name, "%s", entries[r].name);
		snprintf(racer->shortName, sizeof racer->shortName, "%s", entries[r].shortName);
		snprintf(racer->modelPath, sizeof racer->modelPath, "%s", entries[r].modelPath);
		snprintf(racer->portraitPath, sizeof racer->portraitPath, "%s", entries[r].portraitPath);
	}
	if (count > 0)
		printf("[Racers] %d custom racer(s) from %s\n", count, dir);
#endif
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

const char *NativeRacers_Name(int racer)
{
	return (NativeRacers_Valid(racer) && s_racers[racer].name[0]) ? s_racers[racer].name : NULL;
}

const char *NativeRacers_ShortName(int racer)
{
	return (NativeRacers_Valid(racer) && s_racers[racer].shortName[0]) ? s_racers[racer].shortName : NULL;
}

// Read a racer model file and relocate it, as the game loads a DRAM file:
// a u32 pointer-map offset, the model, then the pointer map.
static struct Model *NativeRacers_ReadModel(const char *path, int menuScale)
{
	FILE *f = NativeFs_OpenRead(path);
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	u32 *file = (size > 8) ? (u32 *)calloc(1, (size_t)size + 4) : NULL;
	if ((file == NULL) || (fread(file, 1, (size_t)size, f) != (size_t)size))
	{
		fclose(f);
		free(file);
		return NULL;
	}
	fclose(f);

	const int ptrMapOffset = (int)file[0];
	char *model = (char *)&file[1];
	if ((ptrMapOffset <= 0) || ((long)ptrMapOffset + 8 > size - 4))
	{
		free(file);
		return NULL;
	}
	struct DramPointerMap *dpm = (struct DramPointerMap *)&model[ptrMapOffset];
	if ((long)ptrMapOffset + 4 + (long)dpm->numBytes > size - 4)
	{
		free(file);
		return NULL;
	}
	LOAD_RunPtrMap(model, (int *)DRAM_GETOFFSETS(dpm), dpm->numBytes >> 2);

	struct Model *m = (struct Model *)model;
	if (menuScale)
	{
		for (int i = 0; i < m->numHeaders; i++)
		{
			struct ModelHeader *header = &m->headers[i];
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
		r->menuModel = NativeRacers_ReadModel(r->modelPath, 1);
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

	const TextureID bank = NativeRenderer_CreateVramBank();
	NativeGpu_SetRacerBankTexture(bank);

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
