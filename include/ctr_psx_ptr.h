#ifndef CTR_PSX_PTR_H
#define CTR_PSX_PTR_H

// Pointers inside PSX-format data (docs/64BIT.md).
//
// Files read from the disc (levels, models, nav, language) are used in place,
// so their structs keep retail's byte layout on every build. A pointer field in
// them is PSX_PTR(T): four bytes holding a PSX address, 0x80000000 plus the
// target's offset in PSX space, or 0 for NULL. PSX space is one host block
// holding the mempack and every disc-format buffer the host loads; under retail
// memory pressure its start is the 2 MiB PS1 RAM image, so stored values equal
// the addresses retail keeps in RAM.
//
//   PSX_GET(level->ptr_mesh_info)        the host pointer, typed
//   PSX_SET(instDef->ptrInstance, inst)  store a host pointer (type-checked)
//   PSX_ADDR(field)                      the raw 32-bit address
//
// Only memory inside PSX space can be stored; anything else is a fatal error.
// Every other struct uses plain pointers.

#include <ctr_types.h>

#define PSX_ADDR_BASE 0x80000000u

#if defined(CTR_NATIVE)

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Defined in platform/native_memory.c.
extern u8 gPsxSpace[];
extern const u32 gPsxSpaceBytes;
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void PsxSpace_Outside(const void *host);

static inline void *PsxSpace_ToHost(u32 addr)
{
	return addr ? (void *)&gPsxSpace[addr - PSX_ADDR_BASE] : NULL;
}

static inline u32 PsxSpace_FromHost(const void *host)
{
	uintptr_t offset;

	if (host == NULL)
		return 0;
	offset = (uintptr_t)((const u8 *)host - gPsxSpace);
	if (offset >= gPsxSpaceBytes)
		PsxSpace_Outside(host);
	return PSX_ADDR_BASE + (u32)offset;
}

static inline int PsxSpace_Contains(const void *host)
{
	return (uintptr_t)((const u8 *)host - gPsxSpace) < gPsxSpaceBytes;
}

// Is a word read from data a PSX address inside PSX space?
static inline int PsxSpace_IsAddr(u32 word)
{
	return (word >= PSX_ADDR_BASE) && (word - PSX_ADDR_BASE < gPsxSpaceBytes);
}

#if defined(__cplusplus)
}
#endif

// A zero-length array member carries the pointee type without taking space.
// It would still give the union a host pointer's alignment (8 on 64-bit), so
// the union is pinned to the four bytes of a PSX word.
#define PSX_PTR(T)    union __attribute__((packed, aligned(4))) { u32 addr; T *ptrType[0]; }
#define PSX_GET(p)    ((__typeof__((p).ptrType[0]))PsxSpace_ToHost((p).addr))
#define PSX_SET(p, v) ((p).addr = PsxSpace_FromHost(1 ? (v) : (p).ptrType[0]))
#define PSX_ADDR(p)   ((p).addr)

#else // PS1 build: a PSX address is the pointer itself.

#define PSX_PTR(T)    T *
#define PSX_GET(p)    (p)
#define PSX_SET(p, v) ((p) = (v))
#define PSX_ADDR(p)   (*(u32 *)&(p))
#define PsxSpace_ToHost(addr)   ((void *)(addr))
#define PsxSpace_FromHost(host) ((u32)(host))

#endif

// Elements of the pointer arrays inside disc-format data.
typedef PSX_PTR(void) PsxVoidPtr;
typedef PSX_PTR(struct Model) PsxModelPtr;
typedef PSX_PTR(struct InstDef) PsxInstDefPtr;
typedef PSX_PTR(struct Instance) PsxInstancePtr;
typedef PSX_PTR(struct NavHeader) PsxNavHeaderPtr;
typedef PSX_PTR(struct IconGroup) PsxIconGroupPtr;
typedef PSX_PTR(struct Icon) PsxIconPtr;
typedef PSX_PTR(struct IconGroup4) PsxIconGroup4Ptr;
typedef PSX_PTR(struct TextureLayout) PsxTextureLayoutPtr;
typedef PSX_PTR(struct ModelAnim) PsxModelAnimPtr;

#endif
