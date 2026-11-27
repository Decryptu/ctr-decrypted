#ifndef PLATFORM_NATIVE_PSX_SPACE_H
#define PLATFORM_NATIVE_PSX_SPACE_H

#include <stddef.h>

// PSX space (include/ctr_psx_ptr.h, docs/64BIT.md): the one host block every
// PSX_PTR points into. It starts with the mempack backing
// (platform/native_memory.c), so under retail memory pressure an offset there
// is a retail RAM address; the host heap after it holds disc-format buffers
// the host loads outside the mempack, such as racer models. Pages are only
// touched once used.
#define CTR_PSX_SPACE_MEMPACK_BYTES (16u * 1024u * 1024u)
#define CTR_PSX_SPACE_HEAP_BYTES    (64u * 1024u * 1024u)
#define CTR_PSX_SPACE_BYTES         (CTR_PSX_SPACE_MEMPACK_BYTES + CTR_PSX_SPACE_HEAP_BYTES)

// The heap. Zeroed, 16-byte aligned; NULL when full. Main thread only.
void *PsxSpace_Alloc(size_t bytes);
void PsxSpace_Free(void *block);
// Bytes in use, headers included (for tests and logs).
size_t PsxSpace_UsedBytes(void);

#endif
