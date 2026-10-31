// The PGXP module reads g_config, which the game defines in native_config.c
// with everything that loads it. Tests that link the GTE core get the default
// (zeroed) configuration here instead.
#include "platform/native_config.h"

NativeConfig g_config;
