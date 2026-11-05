#include "platform/native_level_pointers.h"

#include <stdint.h>
#include <stdlib.h>

// One pack each for the levels that can be loaded at a time.
#define NATIVE_LEVEL_POINTER_RECORDS 4

struct NativeLevelPointerRecord
{
	const char *origin;
	uint32_t words;
	uint32_t *bits;
};

static struct NativeLevelPointerRecord sRecords[NATIVE_LEVEL_POINTER_RECORDS];
static int sNewest;

void NativeLevelPointers_Note(const char *origin, const int *offsets, int count)
{
	int slot = (sNewest + 1) % NATIVE_LEVEL_POINTER_RECORDS;
	for (int i = 0; i < NATIVE_LEVEL_POINTER_RECORDS; i++)
	{
		if (sRecords[i].origin == origin)
		{
			slot = i;
		}
	}

	struct NativeLevelPointerRecord *record = &sRecords[slot];
	free(record->bits);
	record->origin = NULL;
	record->words = 0;
	record->bits = NULL;
	sNewest = slot;

	uint32_t words = 0;
	for (int i = 0; i < count; i++)
	{
		uint32_t word = (uint32_t)offsets[i] >> 2;
		if (word >= words)
		{
			words = word + 1;
		}
	}

	uint32_t *bits = (uint32_t *)calloc((words + 31) / 32 + 1, sizeof(uint32_t));
	if (bits == NULL)
	{
		return;
	}

	for (int i = 0; i < count; i++)
	{
		uint32_t word = (uint32_t)offsets[i] >> 2;
		bits[word >> 5] |= 1u << (word & 31);
	}

	record->origin = origin;
	record->words = words;
	record->bits = bits;
}

int NativeLevelPointers_Query(const void *address)
{
	// newest first: a level's old record can still span a newer level's memory
	for (int i = 0; i < NATIVE_LEVEL_POINTER_RECORDS; i++)
	{
		const struct NativeLevelPointerRecord *record = &sRecords[(sNewest + NATIVE_LEVEL_POINTER_RECORDS - i) % NATIVE_LEVEL_POINTER_RECORDS];
		uintptr_t at = (uintptr_t)address;
		uintptr_t start = (uintptr_t)record->origin;
		if (record->bits == NULL || at < start || (at - start) >> 2 >= record->words)
		{
			continue;
		}

		uint32_t word = (uint32_t)((at - start) >> 2);
		return (record->bits[word >> 5] >> (word & 31)) & 1;
	}

	return -1;
}
