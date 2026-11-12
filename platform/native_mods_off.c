// Mods turned off on the Mods screen (include/platform/native_mods_off.h).
#include <platform/native_mods_off.h>
#include <platform/native_assets.h>

#include <stdio.h>
#include <string.h>

#define MODS_OFF_MAX 256
#define MODS_OFF_KEY_BYTES 65

static const char *const s_modsOffFolder[NATIVE_MODS_KINDS] = {"tracks", "racers"};
static char s_modsOff[NATIVE_MODS_KINDS][MODS_OFF_MAX][MODS_OFF_KEY_BYTES];
static int s_modsOffCount[NATIVE_MODS_KINDS], s_modsOffLoaded[NATIVE_MODS_KINDS];

static int NativeModsOff_Path(int kind, char *path, size_t size)
{
	const int length = snprintf(path, size, "%s/%s/mods-off.txt", NativeAssets_GetAssetDir(), s_modsOffFolder[kind]);
	return length > 0 && (size_t)length < size;
}

static void NativeModsOff_Load(int kind)
{
	char path[4096], line[128];
	FILE *f;
	if (s_modsOffLoaded[kind])
		return;
	s_modsOffLoaded[kind] = 1;
	s_modsOffCount[kind] = 0;
	if (!NativeModsOff_Path(kind, path, sizeof path) || (f = fopen(path, "r")) == NULL)
		return;
	while (s_modsOffCount[kind] < MODS_OFF_MAX && fgets(line, sizeof line, f))
	{
		const size_t n = strcspn(line, "\r\n");
		line[n] = 0;
		if (n > 0 && n < MODS_OFF_KEY_BYTES)
			memcpy(s_modsOff[kind][s_modsOffCount[kind]++], line, n + 1);
	}
	fclose(f);
}

int NativeModsOff_Is(int kind, const char *key)
{
	if (kind < 0 || kind >= NATIVE_MODS_KINDS || !key || !*key)
		return 0;
	NativeModsOff_Load(kind);
	for (int i = 0; i < s_modsOffCount[kind]; i++)
		if (!strcmp(s_modsOff[kind][i], key))
			return 1;
	return 0;
}

int NativeModsOff_Set(int kind, const char *key, int off)
{
	char path[4096];
	FILE *f;
	int i;
	if (kind < 0 || kind >= NATIVE_MODS_KINDS || !key || !*key)
		return 0;
	NativeModsOff_Load(kind);
	char (*list)[MODS_OFF_KEY_BYTES] = s_modsOff[kind];
	int *count = &s_modsOffCount[kind];
	for (i = 0; i < *count && strcmp(list[i], key); i++) {}
	if (off && i == *count)
	{
		if (*count == MODS_OFF_MAX || strlen(key) >= MODS_OFF_KEY_BYTES)
			return 0;
		snprintf(list[(*count)++], MODS_OFF_KEY_BYTES, "%s", key);
	}
	else if (!off && i < *count)
		memmove(list[i], list[i + 1], (size_t)(--*count - i) * MODS_OFF_KEY_BYTES);
	if (!NativeModsOff_Path(kind, path, sizeof path) || (f = fopen(path, "w")) == NULL)
		return 0;
	for (i = 0; i < *count; i++)
		fprintf(f, "%s\n", list[i]);
	return fclose(f) == 0;
}
