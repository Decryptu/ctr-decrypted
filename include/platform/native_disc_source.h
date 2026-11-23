#ifndef NATIVE_DISC_SOURCE_H
#define NATIVE_DISC_SOURCE_H

#include <stdio.h>
#include <macros.h>

#define NATIVE_DISC_SOURCE_RAW_SECTOR 2352
#define NATIVE_DISC_SOURCE_FORM1_OFFSET 24

int NativeDiscSource_ReadSector(FILE *f, u32 lba, u8 *out);
int NativeDiscSource_FindPath(FILE *f, const char *path, u32 *lbaOut, u32 *sizeOut);
FILE *NativeDiscSource_Open(const char *bootName, char *pathOut, size_t pathOutSize);

#endif
