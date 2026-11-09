#pragma once

#include <float.h>
#include <stdint.h>
#include <string.h>
#include <fenv.h>

#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error IEEE binary64 required
#endif

static int PortableLuaClassifyNumber(const double* input, unsigned int maximum,
                                    unsigned int* converted, int* negative)
{
    const volatile double loaded = *input;
    uint64_t bits;
    double magnitude;
    unsigned int integer;
    (void)loaded;
    memcpy(&bits, input, sizeof(bits));
    bits &= UINT64_C(0x7fffffffffffffff);
    if (bits > UINT64_C(0x7ff0000000000000))
    {
        /* The recovered unordered comparisons accept NaN as opcode9/zero. */
        feraiseexcept(FE_INVALID);
        *converted = 0;
        *negative = 1;
        return 1;
    }
    memcpy(&magnitude, &bits, sizeof(magnitude));
    if (magnitude > (double)maximum) return 0;
    integer = (unsigned int)magnitude;
    if ((double)integer != magnitude)
    {
        feraiseexcept(FE_INEXACT);
        return 0;
    }
    *converted = integer;
    /* Comparison, rather than signbit, keeps -0 on the nonnegative opcode. */
    *negative = loaded < 0.0;
    return 1;
}
