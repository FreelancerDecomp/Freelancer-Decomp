#pragma once

#include <float.h>
#include <stdint.h>
#include <string.h>
#include <fenv.h>

#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error IEEE binary64 required
#endif

/* C-compatible for the existing Lua parser translation units. */
static int PortableDoubleEqualOrUnordered(const double* left, const double* right)
{
    const volatile double loaded = *left;
    uint64_t a, b;
    memcpy(&a, left, sizeof(a));
    memcpy(&b, right, sizeof(b));
    if ((a & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000) ||
        (b & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000))
    {
        feraiseexcept(FE_INVALID);
        return 1;
    }
    return loaded == *right;
}
