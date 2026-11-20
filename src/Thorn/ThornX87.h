// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Port-only x64 build (docs/x64.md): helpers for the portable versions of Thorn's inline x87
// code. The Win32 build keeps its inline assembly; nothing here is used when _M_IX86 is defined.
//
// Convention (the same as x87_trig.h): a value retail keeps on the x87 stack, at extended
// precision, is a double on x64. A value retail stores to a float (FSTP dword) is rounded to
// float at that point and nowhere else. Products and quotients follow the x87 order of
// operations; at the server's 53-bit precision control they give the same bits as on Win32,
// except where the x87 kept a value at extended precision or used FSIN / FCOS / FPATAN.

#if !defined(_M_IX86)

#include "thorn_iface.h"
#include "x87_portable.h"
#include "x87_trig.h"

#include <cstring>

// FThornExtendedProgress holds an x87 extended value (FSTP tbyte) on Win32. On x64 it carries
// a double in its first eight bytes; only Thorn writes and reads it.
inline void ThornStoreExtended(FThornExtendedProgress* Out, double Value)
{
    std::memset(Out->Bytes, 0, sizeof(Out->Bytes));
    std::memcpy(Out->Bytes, &Value, sizeof(Value));
}

inline double ThornLoadExtended(const FThornExtendedProgress& In)
{
    double Value;
    std::memcpy(&Value, In.Bytes, sizeof(Value));
    return Value;
}

// FCOM / FCOMP / FUCOM followed by FNSTSW AX and a TEST AH,mask. The status word's condition
// bits: C0 (01h) "less", C2 (04h) "unordered", C3 (40h) "equal"; an unordered compare (a NaN)
// sets all three.
inline unsigned char X87CompareFlags(double A, double B)
{
    if (A != A || B != B)
        return 0x45;
    if (A < B)
        return 0x01;
    if (A == B)
        return 0x40;
    return 0x00;
}

#endif
