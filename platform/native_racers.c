#include <platform/native_track_minimap.h>
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
_Static_assert(NATIVE_RACERS_MAX <= NATIVE_RACERS_PORTRAIT_PAGES * NATIVE_RACERS_SLOTS_PER_PAGE,
               "every portrait fits the first page row, clear of the track picture");

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

// Read the library again (the Mods screen installed or removed a racer).
// Called from the menus only: no race holds a racer model then. The players'
// picks are cleared, since racer indices change.
void NativeRacers_Reload(void)
{
	for (int r = 0; r < s_racerCount; r++)
	{
		// A model is the file buffer less its leading u32 pointer-map offset.
		if (s_racers[r].menuModel)
			free((u32 *)s_racers[r].menuModel - 1);
		if (s_racers[r].raceModel)
			free((u32 *)s_racers[r].raceModel - 1);
	}
	memset(s_racers, 0, sizeof s_racers);
	s_racerCount = -1;
	s_portraitsUploaded = 0;
	for (int p = 0; p < 4; p++)
		s_playerRacer[p] = -1;
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
	if ((gGT == NULL) || (driverID < 0) || (driverID >= gGT->numPlyrCurrGame) || (driverID >= 4) ||
	    ((gGT->gameMode1 & (ADVENTURE_MODE | MAIN_MENU)) != 0))
		return -1;
	const int racer = s_playerRacer[driverID];
	return (NativeRacers_Valid(racer) && (s_racers[racer].templateID == GAME_CHARACTER_IDS[driverID])) ? racer : -1;
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
