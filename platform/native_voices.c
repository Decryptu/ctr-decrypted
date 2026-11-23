// Voice language packs (OPTIONS > Extras > Voices). See include/platform/native_voices.h
// and docs/EXTRAS.md#voices.
//
// A pack is one language's voice folder copied from the player's own PAL or
// Japanese disc image into assets/XA/VOICES/<dir>: <dir>.XNF plus EXTRA/ and
// GAME/ with the XA files as raw 2352-byte sectors, the same form the NTSC-U
// clips are read in. The manifest is written last, so a pack whose manifest
// exists is complete. Clip ids are mapped from NTSC-U by native_voice_map.inc
// (tools/decrypted/voice_map.py).

#include <platform/native_voices.h>
#include <platform/native_assets.h>
#include <platform/native_config.h>
#include <platform/native_fs_utf8.h>
#include <platform/native_disc_source.h>

#include "native_voice_map.inc"

static const struct
{
	const char *dir;   // folder on the source disc and in assets/XA/VOICES
	const char *label; // options menu value
	int japanese;
} s_voiceLangs[NATIVE_VOICES_COUNT] = {
	{NULL, "US ENGLISH", 0},
	{"ENG", "UK ENGLISH", 0},
	{"FRN", "FRENCH", 0},
	{"GRM", "GERMAN", 0},
	{"SPN", "SPANISH", 0},
	{"ITL", "ITALIAN", 0},
	{"DCH", "DUTCH", 0},
	{"JPN", "JAPANESE", 1},
};

static enum NativeVoicesImport s_voiceLastImport[NATIVE_VOICES_COUNT];
static signed char s_voiceInstalled[NATIVE_VOICES_COUNT]; // 0 unknown, 1 yes, -1 no

int NativeVoices_Clamp(int lang)
{
	return (lang >= 0 && lang < NATIVE_VOICES_COUNT) ? lang : NATIVE_VOICES_US;
}

const char *NativeVoices_Label(int lang)
{
	return s_voiceLangs[NativeVoices_Clamp(lang)].label;
}

int NativeVoices_NeedsJapaneseDisc(int lang)
{
	lang = NativeVoices_Clamp(lang);
	return lang == NATIVE_VOICES_US ? -1 : s_voiceLangs[lang].japanese;
}

static int NativeVoices_PackPath(int lang, const char *file, char *dst, size_t dstSize)
{
	char rel[96];
	int n = file ? snprintf(rel, sizeof rel, "XA/VOICES/%s/%s", s_voiceLangs[lang].dir, file)
	             : snprintf(rel, sizeof rel, "XA/VOICES/%s", s_voiceLangs[lang].dir);
	return n > 0 && (size_t)n < sizeof rel && NativeAssets_BuildPath(rel, dst, dstSize);
}

int NativeVoices_Installed(int lang)
{
	lang = NativeVoices_Clamp(lang);
	if (lang == NATIVE_VOICES_US)
		return 1;
	if (s_voiceInstalled[lang] == 0)
	{
		char manifest[16];
		char path[1024];
		snprintf(manifest, sizeof manifest, "%s.XNF", s_voiceLangs[lang].dir);
		s_voiceInstalled[lang] = (NativeVoices_PackPath(lang, manifest, path, sizeof path) && NativeFs_FileExists(path)) ? 1 : -1;
	}
	return s_voiceInstalled[lang] > 0;
}

enum NativeVoicesImport NativeVoices_LastImport(int lang)
{
	return s_voiceLastImport[NativeVoices_Clamp(lang)];
}

static const char *NativeVoices_BootName(int lang)
{
	return s_voiceLangs[lang].japanese ? "SCPS_101.18" : "SCES_021.05";
}

int NativeVoices_HasSourceDisc(int lang)
{
	char path[1024];
	lang = NativeVoices_Clamp(lang);
	if (lang == NATIVE_VOICES_US)
		return 1;
	FILE *f = NativeDiscSource_Open(NativeVoices_BootName(lang), path, sizeof path);
	if (f != NULL)
		fclose(f);
	return f != NULL;
}

// Copies a disc file to dst through dst.part. Raw keeps whole 2352-byte sectors (XA).
static int NativeVoices_CopyFile(FILE *f, u32 lba, u32 size, int raw, const char *dst)
{
	char part[1100];
	u8 buffer[NATIVE_DISC_SOURCE_RAW_SECTOR];
	u32 sectors = (size + 2047) / 2048;
	snprintf(part, sizeof part, "%s.part", dst);
	NativeFs_Delete(part);
	FILE *out = NativeFs_CreateExclusive(part);
	if (out == NULL)
		return 0;
	int ok = fseek(f, (long)lba * NATIVE_DISC_SOURCE_RAW_SECTOR, SEEK_SET) == 0;
	for (u32 i = 0; ok && i < sectors; i++)
	{
		u32 keep = raw ? NATIVE_DISC_SOURCE_RAW_SECTOR : (i + 1 < sectors ? 2048 : size - i * 2048);
		ok = fread(buffer, 1, sizeof buffer, f) == sizeof buffer &&
		     fwrite(raw ? buffer : buffer + NATIVE_DISC_SOURCE_FORM1_OFFSET, 1, keep, out) == keep;
	}
	ok = (fclose(out) == 0) && ok;
	ok = ok && NativeFs_Replace(part, dst);
	if (!ok)
		NativeFs_Delete(part);
	return ok;
}

static int NativeVoices_MakeDirs(int lang)
{
	// MakeDirectory fails on a folder that already exists; a real failure
	// shows up when the files are written.
	char path[1024];
	if (!NativeAssets_BuildPath("XA", path, sizeof path))
		return 0;
	NativeFs_MakeDirectory(path);
	if (!NativeAssets_BuildPath("XA/VOICES", path, sizeof path))
		return 0;
	NativeFs_MakeDirectory(path);
	if (!NativeVoices_PackPath(lang, NULL, path, sizeof path))
		return 0;
	NativeFs_MakeDirectory(path);
	if (!NativeVoices_PackPath(lang, "GAME", path, sizeof path))
		return 0;
	NativeFs_MakeDirectory(path);
	if (!NativeVoices_PackPath(lang, "EXTRA", path, sizeof path))
		return 0;
	NativeFs_MakeDirectory(path);
	return 1;
}

static enum NativeVoicesImport NativeVoices_Import(int lang)
{
	const char *code = s_voiceLangs[lang].dir;
	const char *boot = NativeVoices_BootName(lang);
	char discPath[1024];
	char rel[64];
	char dst[1024];
	u32 lba, size;
	int ok = 1;

	FILE *f = NativeDiscSource_Open(boot, discPath, sizeof discPath);
	if (f == NULL)
		return NATIVE_VOICES_IMPORT_NO_DISC;

	ok = NativeVoices_MakeDirs(lang);
	for (int kind = 0; ok && kind < 2; kind++)
	{
		const char *sub = kind ? "GAME" : "EXTRA";
		for (int n = 0; ok && n < 100; n++)
		{
			snprintf(rel, sizeof rel, "XA/%s/%s/S%02d.XA", code, sub, n);
			if (!NativeDiscSource_FindPath(f, rel, &lba, &size))
				continue;
			snprintf(rel, sizeof rel, "%s/S%02d.XA", sub, n);
			ok = NativeVoices_PackPath(lang, rel, dst, sizeof dst) && NativeVoices_CopyFile(f, lba, size, 1, dst);
		}
	}
	snprintf(rel, sizeof rel, "XA/%s.XNF", code);
	ok = ok && NativeDiscSource_FindPath(f, rel, &lba, &size);
	snprintf(rel, sizeof rel, "%s.XNF", code);
	ok = ok && NativeVoices_PackPath(lang, rel, dst, sizeof dst) && NativeVoices_CopyFile(f, lba, size, 0, dst);
	fclose(f);

	fprintf(stderr, "[CTR Native] voices: %s %s from %s\n", ok ? "imported" : "failed to import", s_voiceLangs[lang].label, discPath);
	return ok ? NATIVE_VOICES_IMPORT_OK : NATIVE_VOICES_IMPORT_FAILED;
}

enum NativeVoicesImport NativeVoices_Ensure(int lang)
{
	lang = NativeVoices_Clamp(lang);
	if (NativeVoices_Installed(lang))
		return s_voiceLastImport[lang] = NATIVE_VOICES_IMPORT_OK;
	s_voiceLastImport[lang] = NativeVoices_Import(lang);
	s_voiceInstalled[lang] = 0;
	return s_voiceLastImport[lang];
}

void NativeVoices_Init(void)
{
	g_config.voices = NativeVoices_Clamp(g_config.voices);
	if (g_config.voices != NATIVE_VOICES_US)
		NativeVoices_Ensure(g_config.voices);
	// Penta's NTSC-U clips are placeholders; his finished PAL English ones play
	// with US English voices too, so take them whenever a PAL disc is there.
	if (!NativeVoices_Installed(NATIVE_VOICES_UK))
		NativeVoices_Ensure(NATIVE_VOICES_UK);
}

static int NativeVoices_Active(void)
{
	int lang = NativeVoices_Clamp(g_config.voices);
	return NativeVoices_Installed(lang) ? lang : NATIVE_VOICES_US;
}

int NativeVoices_ForCharacter(int characterID)
{
	int lang = NativeVoices_Active();
	if (lang == NATIVE_VOICES_US && characterID == PENTA_PENGUIN && NativeVoices_Installed(NATIVE_VOICES_UK))
		return NATIVE_VOICES_UK;
	return lang;
}

static int NativeVoices_IsPentaClip(int categoryID, int usID)
{
	// Penta's NTSC-U voice sets 0-7 (data.voiceData[PENTA_PENGUIN]).
	return categoryID == 2 && usID >= 247 && usID <= 262;
}

const char *NativeVoices_Map(int categoryID, int usID, int *idOut)
{
	int lang = NativeVoices_Active();
	const u16 *table;
	int count;

	if (lang == NATIVE_VOICES_US && NativeVoices_IsPentaClip(categoryID, usID) && NativeVoices_Installed(NATIVE_VOICES_UK))
		lang = NATIVE_VOICES_UK;
	if (lang == NATIVE_VOICES_US || usID < 0)
		return NULL;

	if (categoryID == 1)
	{
		table = s_voiceLangs[lang].japanese ? s_voiceMap_jp_extra : s_voiceMap_pal_extra;
		count = NATIVE_VOICE_MAP_EXTRA_COUNT;
	}
	else if (categoryID == 2)
	{
		table = s_voiceLangs[lang].japanese ? s_voiceMap_jp_game : s_voiceMap_pal_game;
		count = NATIVE_VOICE_MAP_GAME_COUNT;
	}
	else
	{
		return NULL;
	}
	if (usID >= count || table[usID] == 0xFFFF)
		return NULL;
	*idOut = table[usID];
	return s_voiceLangs[lang].dir;
}
