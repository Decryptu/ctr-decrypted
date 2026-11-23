#include <platform/native_disc_source.h>
#include <platform/native_assets.h>
#include <platform/native_disc_image.h>
#include <platform/native_fs_utf8.h>
#include <platform/native_path.h>
#include <limits.h>

// The NTSC-U disc stays mounted (native_disc_image.c); the source disc is read
// here through its own FILE, with the same ISO 9660 record helpers.

int NativeDiscSource_ReadSector(FILE *f, u32 lba, u8 *out)
{
	u8 sector[NATIVE_DISC_SOURCE_RAW_SECTOR];
	const u64 offset = (u64)lba * NATIVE_DISC_SOURCE_RAW_SECTOR;
	if (offset > (u64)LONG_MAX || fseek(f, (long)offset, SEEK_SET) != 0 ||
	    fread(sector, 1, sizeof sector, f) != sizeof sector ||
	    !NativeDiscImage_CheckRawSectorHeader(sector))
		return 0;
	memcpy(out, sector + NATIVE_DISC_SOURCE_FORM1_OFFSET, 2048);
	return 1;
}

// Finds name in the directory at (lba, size). Returns 1 with the entry's lba and size.
static int NativeDiscSource_FindEntry(FILE *f, u32 lba, u32 size, const char *name, u32 *lbaOut, u32 *sizeOut)
{
	u8 sector[2048];
	for (u64 done = 0; done < size; done += 2048, lba++)
	{
		if (!NativeDiscSource_ReadSector(f, lba, sector))
			return 0;
		for (u32 pos = 0; pos + 34 <= 2048 && sector[pos] != 0; pos += sector[pos])
		{
			struct NativeDiscImageDirRecord record;
			if (!NativeDiscImage_ParseDirRecord(&sector[pos], 2048 - pos, &record))
				break;
			if (NativeDiscImage_NameEquals(&record, NativeStr8_FromCString(name)))
			{
				*lbaOut = record.lba;
				*sizeOut = record.size;
				return 1;
			}
		}
	}
	return 0;
}

// Resolves "A/B/C" from the root. Returns 1 with the file's lba and size.
int NativeDiscSource_FindPath(FILE *f, const char *path, u32 *lbaOut, u32 *sizeOut)
{
	u8 pvd[2048];
	struct NativeDiscImageDirRecord root;
	char part[64];
	if (!NativeDiscSource_ReadSector(f, 16, pvd) || memcmp(&pvd[1], "CD001", 5) != 0 ||
	    !NativeDiscImage_ParseDirRecord(&pvd[156], 34, &root))
		return 0;
	u32 lba = root.lba, size = root.size;
	while (*path)
	{
		size_t n = strcspn(path, "/");
		if (n == 0 || n >= sizeof part)
			return 0;
		memcpy(part, path, n);
		part[n] = 0;
		if (!NativeDiscSource_FindEntry(f, lba, size, part, &lba, &size))
			return 0;
		path += n + (path[n] == '/');
	}
	*lbaOut = lba;
	*sizeOut = size;
	return 1;
}

// Opens the first .bin in assets/ whose boot executable is bootName.
FILE *NativeDiscSource_Open(const char *bootName, char *pathOut, size_t pathOutSize)
{
	const char *assetsDir = NativeAssets_GetAssetDir();
	NativeFsDir *dir = assetsDir ? NativeFs_OpenDir(assetsDir) : NULL;
	char entry[512];
	if (dir == NULL)
		return NULL;
	while (NativeFs_ReadDir(dir, entry, sizeof entry))
	{
		size_t len = strlen(entry);
		u32 lba, size;
		if (len < 5 || NativeStr8_ToUpperAscii(entry[len - 3]) != 'B' || NativeStr8_ToUpperAscii(entry[len - 2]) != 'I' ||
		    NativeStr8_ToUpperAscii(entry[len - 1]) != 'N' || entry[len - 4] != '.')
			continue;
		if (!NativePath_Join(pathOut, pathOutSize, NativeStr8_FromCString(assetsDir), NativeStr8_FromCString(entry)))
			continue;
		FILE *f = NativeFs_OpenRead(pathOut);
		if (f == NULL)
			continue;
		if (NativeDiscSource_FindPath(f, bootName, &lba, &size))
		{
			NativeFs_CloseDir(dir);
			return f;
		}
		fclose(f);
	}
	NativeFs_CloseDir(dir);
	return NULL;
}
