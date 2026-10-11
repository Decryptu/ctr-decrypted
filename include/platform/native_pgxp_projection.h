#ifndef NATIVE_PGXP_PROJECTION_H
#define NATIVE_PGXP_PROJECTION_H

#include <math.h>

static inline float NativePgxp_TextureDepthFromGte(unsigned int sz3, int reciprocal)
{
    return (sz3 >= 64 && sz3 <= 65535 && reciprocal < 0x1ffff)
               ? (float)sz3 : 0.0f;
}

#endif
