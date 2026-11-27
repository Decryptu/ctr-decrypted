#ifndef HARNESS_PSX_SPACE_H
#define HARNESS_PSX_SPACE_H

// PSX space for a host harness whose fixture stands in for disc-format data
// (docs/64BIT.md): the engine's PSX_PTR macros and PSX heap, and a
// Platform_Fatal that fails the run. Fixture structs declare their pointer
// fields with PSX_PTR like the real ones, and whatever such a field points at
// comes from PsxSpace_Alloc. Build with -DCTR_NATIVE and -I include.

#include <stdio.h>
#include <stdlib.h>

#include "../platform/native_psx_space.c"

void Platform_Fatal(const char *title, const char *message)
{
	printf("FATAL %s: %s\n", title, message);
	exit(1);
}

#endif
