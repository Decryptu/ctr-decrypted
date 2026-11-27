#include "platform/native_psx_space.h"
#include <ctr_psx_ptr.h>
#include <platform.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CTR_PSX_SPACE_BYTES < 0x7fffffffu, "PSX addresses must stay below 0xffffffff");

_Alignas(4096) u8 gPsxSpace[CTR_PSX_SPACE_BYTES];
const u32 gPsxSpaceBytes = CTR_PSX_SPACE_BYTES;

void PsxSpace_Outside(const void *host)
{
	char message[160];
	snprintf(message, sizeof message, "A pointer outside PSX space (%p, space %p..%p) was stored in disc-format data.",
	         host, (void *)gPsxSpace, (void *)&gPsxSpace[CTR_PSX_SPACE_BYTES]);
	Platform_Fatal("CTR Native - PSX space", message);
	abort();
}

// The heap: first-fit over its block of PSX space. Each block starts with a
// 16-byte header; free neighbours are merged while searching. It holds a few
// dozen long-lived buffers, so a linear walk is enough.

#define PSX_HEAP_ALIGN 16u

struct PsxHeapBlock
{
	u32 bytes; // whole block, header included
	u32 used;
	u32 reserved[2];
};

_Static_assert(sizeof(struct PsxHeapBlock) == PSX_HEAP_ALIGN, "a header keeps blocks aligned");

static u8 *const s_psxHeap = &gPsxSpace[CTR_PSX_SPACE_MEMPACK_BYTES];
static int s_psxHeapReady;

static struct PsxHeapBlock *PsxSpace_Block(u32 at)
{
	return (struct PsxHeapBlock *)&s_psxHeap[at];
}

void *PsxSpace_Alloc(size_t bytes)
{
	u32 need;
	u32 at;

	if (!s_psxHeapReady)
	{
		PsxSpace_Block(0)->bytes = CTR_PSX_SPACE_HEAP_BYTES;
		PsxSpace_Block(0)->used = 0;
		s_psxHeapReady = 1;
	}
	if (bytes > CTR_PSX_SPACE_HEAP_BYTES - sizeof(struct PsxHeapBlock))
		return NULL;
	need = (u32)((bytes + sizeof(struct PsxHeapBlock) + PSX_HEAP_ALIGN - 1) & ~(size_t)(PSX_HEAP_ALIGN - 1));

	for (at = 0; at < CTR_PSX_SPACE_HEAP_BYTES;)
	{
		struct PsxHeapBlock *block = PsxSpace_Block(at);

		if (!block->used)
		{
			while ((at + block->bytes < CTR_PSX_SPACE_HEAP_BYTES) && !PsxSpace_Block(at + block->bytes)->used)
				block->bytes += PsxSpace_Block(at + block->bytes)->bytes;
			if (block->bytes >= need)
			{
				if (block->bytes - need >= 2 * PSX_HEAP_ALIGN)
				{
					struct PsxHeapBlock *rest = PsxSpace_Block(at + need);
					rest->bytes = block->bytes - need;
					rest->used = 0;
					block->bytes = need;
				}
				block->used = 1;
				memset(block + 1, 0, block->bytes - sizeof(*block));
				return block + 1;
			}
		}
		at += block->bytes;
	}
	return NULL;
}

void PsxSpace_Free(void *ptr)
{
	struct PsxHeapBlock *block;

	if (ptr == NULL)
		return;
	block = (struct PsxHeapBlock *)ptr - 1;
	if (!s_psxHeapReady || ((u8 *)block < s_psxHeap) || ((u8 *)block >= s_psxHeap + CTR_PSX_SPACE_HEAP_BYTES) || !block->used)
		Platform_Fatal("CTR Native - PSX space", "PsxSpace_Free got a block it did not allocate.");
	block->used = 0;
}

size_t PsxSpace_UsedBytes(void)
{
	size_t used = 0;
	u32 at;

	if (!s_psxHeapReady)
		return 0;
	for (at = 0; at < CTR_PSX_SPACE_HEAP_BYTES; at += PsxSpace_Block(at)->bytes)
	{
		if (PsxSpace_Block(at)->used)
			used += PsxSpace_Block(at)->bytes;
	}
	return used;
}
