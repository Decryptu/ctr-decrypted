// First-launch disc search. See include/platform/native_disc_search.h.

#include "platform/native_disc_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/native_fs_utf8.h"

typedef struct
{
	NativeDiscSearchAccept accept;
	void *ctx;
	char *out;
	size_t outSize;
	int entries;
} NativeDiscSearchWalk;

static int NativeDiscSearch_EndsWithBin(const char *name)
{
	const size_t n = strlen(name);
	if (n < 5)
		return 0;
	return name[n - 4] == '.' && (name[n - 3] | 0x20) == 'b' && (name[n - 2] | 0x20) == 'i' && (name[n - 1] | 0x20) == 'n';
}

static int NativeDiscSearch_SkipFolder(const char *name)
{
	return name[0] == '.' || name[0] == '$' || strcmp(name, "AppData") == 0 || strcmp(name, "node_modules") == 0;
}

static int NativeDiscSearch_BigEnough(const char *path)
{
	FILE *f = NativeFs_OpenRead(path);
	long size;
	if (f == NULL)
		return 0;
	// A file past 2 GB fails here on the 32-bit build and is skipped: no CTR
	// image is that large.
	size = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
	fclose(f);
	return size >= NATIVE_DISC_SEARCH_MIN_BYTES;
}

static int NativeDiscSearch_IsFolder(const char *path)
{
	NativeFsDir *dir = NativeFs_OpenDir(path);
	if (dir == NULL)
		return 0;
	NativeFs_CloseDir(dir);
	return 1;
}

#ifdef _WIN32
#define NATIVE_DISC_SEARCH_SEP "\\"
#else
#define NATIVE_DISC_SEARCH_SEP "/"
#endif

// The platform's own separator, so the path the player is asked about reads
// the way their file manager shows it.
static int NativeDiscSearch_Join(char *dst, size_t size, const char *dir, const char *name)
{
	const int n = snprintf(dst, size, "%s" NATIVE_DISC_SEARCH_SEP "%s", dir, name);
	return n > 0 && (size_t)n < size;
}

// One folder: its .bin files first, then its subfolders, depth levels deep.
static int NativeDiscSearch_Folder(NativeDiscSearchWalk *walk, const char *dir, int depth)
{
	char name[512];
	char path[NATIVE_DISC_SEARCH_PATH_MAX];
	int pass;

	for (pass = 0; pass < 2; pass++)
	{
		NativeFsDir *handle;
		if (pass == 1 && depth <= 0)
			break;
		handle = NativeFs_OpenDir(dir);
		if (handle == NULL)
			return 0;
		while (NativeFs_ReadDir(handle, name, sizeof name))
		{
			if (++walk->entries > NATIVE_DISC_SEARCH_MAX_ENTRIES)
			{
				NativeFs_CloseDir(handle);
				return 0;
			}
			if (!NativeDiscSearch_Join(path, sizeof path, dir, name))
				continue;
			if (pass == 0)
			{
				if (NativeDiscSearch_EndsWithBin(name) && !NativeDiscSearch_IsFolder(path) && NativeDiscSearch_BigEnough(path) &&
				    walk->accept(walk->ctx, path))
				{
					NativeFs_CloseDir(handle);
					snprintf(walk->out, walk->outSize, "%s", path);
					return 1;
				}
			}
			else if (!NativeDiscSearch_SkipFolder(name) && !NativeDiscSearch_EndsWithBin(name) && NativeDiscSearch_IsFolder(path))
			{
				if (NativeDiscSearch_Folder(walk, path, depth - 1))
				{
					NativeFs_CloseDir(handle);
					return 1;
				}
			}
		}
		NativeFs_CloseDir(handle);
	}
	return 0;
}

int NativeDiscSearch_Walk(const char *const *roots, int rootCount, NativeDiscSearchAccept accept, void *ctx,
                          char *out, size_t outSize)
{
	NativeDiscSearchWalk walk;
	int i;

	if (out == NULL || outSize == 0)
		return 0;
	out[0] = '\0';
	walk.accept = accept;
	walk.ctx = ctx;
	walk.out = out;
	walk.outSize = outSize;
	walk.entries = 0;
	for (i = 0; i < rootCount; i++)
	{
		if (roots[i] == NULL || roots[i][0] == '\0')
			continue;
		if (NativeDiscSearch_Folder(&walk, roots[i], NATIVE_DISC_SEARCH_DEPTH))
			return 1;
		if (walk.entries > NATIVE_DISC_SEARCH_MAX_ENTRIES)
			break;
	}
	out[0] = '\0';
	return 0;
}

int NativeDiscSearch_Roots(const char *gameDir, const char *homeDir, char roots[][NATIVE_DISC_SEARCH_PATH_MAX], int max)
{
	static const char *const homeFolders[] = {"Downloads", "Desktop", "Documents", "Games", "OneDrive" NATIVE_DISC_SEARCH_SEP "Desktop",
	                                          "OneDrive" NATIVE_DISC_SEARCH_SEP "Documents"};
	int count = 0;
	size_t i;

	if (gameDir != NULL && gameDir[0] != '\0' && count < max)
	{
		char *slash;
		snprintf(roots[count++], NATIVE_DISC_SEARCH_PATH_MAX, "%s", gameDir);
		// The folder above: a player who unzipped the game next to the disc.
		if (count < max)
		{
			snprintf(roots[count], NATIVE_DISC_SEARCH_PATH_MAX, "%s", gameDir);
			slash = strrchr(roots[count], '/');
			if (strrchr(roots[count], '\\') > slash)
				slash = strrchr(roots[count], '\\');
			if (slash != NULL && slash != roots[count])
			{
				*slash = '\0';
				count++;
			}
		}
	}
	for (i = 0; homeDir != NULL && homeDir[0] != '\0' && i < sizeof homeFolders / sizeof homeFolders[0]; i++)
	{
		if (count >= max)
			break;
		if (NativeDiscSearch_Join(roots[count], NATIVE_DISC_SEARCH_PATH_MAX, homeDir, homeFolders[i]))
			count++;
	}
	return count;
}
