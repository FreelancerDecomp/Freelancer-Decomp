#ifndef THORN_PORTABLE_NUMBER_MATH_H
#define THORN_PORTABLE_NUMBER_MATH_H

#include <stdint.h>
#include <string.h>
#include <fenv.h>
#include <float.h>
#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error Thorn Lua number helpers require IEEE binary64
#endif
typedef char ThornLuaNumberSizeCheck[sizeof(double)==8 ? 1 : -1];

/* The right operand was loaded first in the original reverse instructions.
   Preserve its NaN quieting/selection before the raw left-memory operand. */
static int ThornLuaBinaryNaN(uint64_t loaded, uint64_t memory, double* output)
{
    const int a=(loaded&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000);
    const int b=(memory&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000);
    const uint64_t quiet=UINT64_C(0x0008000000000000);
    uint64_t result;
    if (!a && !b) return 0;
    if ((a && !(loaded&quiet)) || (b && !(memory&quiet))) feraiseexcept(FE_INVALID);
    result=memory|quiet;
    if (a) {
        result=loaded|quiet;
        if (b && (memory&quiet)) {
            const uint64_t x=result&UINT64_C(0x7fffffffffffffff);
            const uint64_t y=memory&UINT64_C(0x7fffffffffffffff);
            if (x<y || (x==y && memory<result)) result=memory;
        }
    }
    memcpy(output,&result,sizeof(result));
    return 1;
}

static void ThornLuaBinaryNumber(double* left, const double* right, int divide)
{
    uint64_t a,b;
    const uint64_t magnitude=UINT64_C(0x7fffffffffffffff);
    const uint64_t infinity=UINT64_C(0x7ff0000000000000);
    const volatile double loaded=*right;
    memcpy(&b,right,sizeof(b));
    memcpy(&a,left,sizeof(a));
    if (ThornLuaBinaryNaN(b,a,left)) return;
    if (divide ? ((a&magnitude)==0 && (b&magnitude)==0) ||
                 ((a&magnitude)==infinity && (b&magnitude)==infinity) :
                 ((a&magnitude)==infinity && (b&magnitude)==infinity && !((a^b)>>63))) {
        const uint64_t indefinite=UINT64_C(0xfff8000000000000);
        feraiseexcept(FE_INVALID);
        memcpy(left,&indefinite,sizeof(indefinite));
    } else if (divide) *left=*left/loaded;
    else *left=*left-loaded;
}
#endif
