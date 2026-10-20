#pragma once

#include "thorn_iface.h"

#if defined(__clang__) && defined(_M_IX86)
// The arithmetic kernels already own a result in ST(0). Give Clang an
// explicit typed return without rounding it through float/double storage.
[[maybe_unused]] static __declspec(naked) float __cdecl ThornReturnSt0Float()
{
    __asm { ret }
}
[[maybe_unused]] static __declspec(naked) double __cdecl ThornReturnSt0Double()
{
    __asm { ret }
}
#endif
