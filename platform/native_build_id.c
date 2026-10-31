#include "platform/native_build_id.h"

// The build ID is its own translation unit. As a definition on main.c it
// changed the whole unity build with every commit, so the game could never be
// reused from a compiler cache and any reconfigure recompiled all of it.
#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

const char gNativeBuildId[] = CTR_NATIVE_BUILD_ID;
