#ifndef CTR_NATIVE_RENDERED_LIST_H
#define CTR_NATIVE_RENDERED_LIST_H

// Each native viewport owns a retry list. Capacity includes its NULL terminator.
// Retail's four 64-pointer slices share an array and can overwrite one another
// when widescreen and increased draw distance expose more water/near geometry.
#define CTR_NATIVE_RENDERED_LIST_PLAYERS 4
#define CTR_NATIVE_RENDERED_LIST_SLOTS 0x400uL

#endif
