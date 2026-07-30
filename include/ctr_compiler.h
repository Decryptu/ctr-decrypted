#ifndef CTR_COMPILER_H
#define CTR_COMPILER_H

#if defined(CTR_NATIVE)
#include <stdlib.h>
#endif

// NOTE(aalhendi): The MSVC C runtime exposes non-standard min/max macros in C mode. They collide with the project's typed helpers even when NOMINMAX is set.
#if defined(_MSC_VER)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#if (defined(__GNUC__) && ((__GNUC__ > 2) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 96)))) || defined(__clang__)
#define CTR_MAY_ALIAS __attribute__((may_alias))
#else
#define CTR_MAY_ALIAS
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CTR_PRINTF_FORMAT(fmtArg, firstVararg) __attribute__((format(printf, fmtArg, firstVararg)))
#define CTR_TRAP()                             __builtin_trap()
#else
#define CTR_PRINTF_FORMAT(fmtArg, firstVararg)
#define CTR_TRAP() abort()
#endif

#endif
