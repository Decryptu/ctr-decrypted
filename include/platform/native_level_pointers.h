#ifndef NATIVE_LEVEL_POINTERS_H
#define NATIVE_LEVEL_POINTERS_H

// Which words of a loaded level are pointers, kept from its pointer map. On
// retail a level's pointers are negative, so a sign test tells them from other
// data. Here they are host addresses, which any palette and UV word can equal.

// Note a level's pointer map. Replaces what was noted for the same origin.
void NativeLevelPointers_Note(const char *origin, const int *offsets, int count);

// 1 when the word at `address` is a pointer, 0 when it is plain data, -1 when
// no noted level holds that address.
int NativeLevelPointers_Query(const void *address);

#endif
