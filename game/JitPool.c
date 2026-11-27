#include <common.h>


void JitPool_Clear(struct JitPool *AP)
{
	u32 *currSlot = AP->ptrPoolData;
	u32 loopIndex;

	// clear list of free and taken
	LIST_Clear(&AP->free);
	LIST_Clear(&AP->taken);

	for (loopIndex = 0; loopIndex < (u32)AP->maxItems; loopIndex++)
	{
		// add all pool items to the free list
		LIST_AddFront(&AP->free, (struct Item *)currSlot);

		// NOTE(aalhendi): Retail truncates the record stride to whole words.
		currSlot += AP->itemSize / sizeof(*currSlot);
	}
}


void JitPool_Init(struct JitPool *AP, int maxItems, int itemSize, char *name)
{
	memset(AP, 0, sizeof(struct JitPool));
	AP->maxItems = maxItems;
	AP->itemSize = itemSize;
	AP->poolSize = maxItems * itemSize;
	AP->ptrPoolData = MEMPACK_AllocMem(AP->poolSize, name);
	JitPool_Clear(AP);
}


void *JitPool_Add(struct JitPool *AP)
{
	struct Item *item = AP->free.first;

	if (item != NULL)
	{
		LIST_RemoveMember(&AP->free, item);
		LIST_AddFront(&AP->taken, item);
	}

	return item;
}


void JitPool_Remove(struct JitPool *AP, struct Item *item)
{
	LIST_RemoveMember(&AP->taken, item);
	LIST_AddFront(&AP->free, item);
}
