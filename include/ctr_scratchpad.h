#ifndef CTR_SCRATCHPAD_H
#define CTR_SCRATCHPAD_H

#include <macros.h>

#define CTR_SCRATCHPAD_ADDR 0x1f800000u
#if defined(CTR_NATIVE)
// What the game keeps there is runtime data, at most twice retail's size on a
// 64-bit host (docs/64BIT.md), so the native scratchpad is twice the PS1's
// 0x400 bytes there.
#define CTR_SCRATCHPAD_SIZE ((u32)(0x400u * (sizeof(void *) / 4u)))
#else
#define CTR_SCRATCHPAD_SIZE 0x400u
#endif

// NOTE(aalhendi): Use these helpers when porting retail scratchpad temporaries.
// On PS1 this is hardware scratchpad RAM at 0x1f800000. On ctr-native, use the
// runtime base set by Platform_InitScratchpad; retail absolute addresses are
// translated back to offsets from the native buffer.
#if defined(CTR_NATIVE)
extern u8 *gCTRNativeScratchpadBase;
#define CTR_SCRATCHPAD_BASE                 (gCTRNativeScratchpadBase)
#define CTR_SCRATCHPAD_ADDR_PTR(type, addr) ((type *)(void *)(CTR_SCRATCHPAD_BASE + ((u32)(addr) - CTR_SCRATCHPAD_ADDR)))
#else
#define CTR_SCRATCHPAD_BASE                 ((u8 *)CTR_SCRATCHPAD_ADDR)
#define CTR_SCRATCHPAD_ADDR_PTR(type, addr) ((type *)(void *)(addr))
#endif

#define CTR_SCRATCHPAD_PTR(type, offset) ((type *)(void *)(CTR_SCRATCHPAD_BASE + (offset)))

#endif
