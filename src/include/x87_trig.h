// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail trigonometry: the x87 instructions VC6 inlines for sin / cos / tan / atan / atan2.
//
// Retail Common.dll never imports _CIsin/_CIcos/_CItan/_CIatan/_CIatan2: every sin, cos, tan,
// atan and atan2 is inlined as one FSIN / FCOS / FPTAN / FPATAN instruction on the x87 stack,
// with no range reduction and no SSE2 path (e.g. .text:06350b32, 062883c0, 06358c1c). The UCRT
// sin/cos/tan/atan/atan2 the port would otherwise call dispatch to an SSE2 implementation
// whenever MXCSR and the x87 exception masks are at their defaults, so their last bits differ
// from the x87 instruction. These helpers execute the instruction itself.
//
// Rounding: FSIN, FCOS, FPTAN and FPATAN ignore the x87 precision control and return a 64-bit
// significand. The result is rounded exactly once, to the width retail stores it at:
//   - the plain names return double (FSTP qword): retail keeps the result on the x87 stack
//     (the port's convention for a value retail keeps on the stack is double);
//   - the ...F names return float (FSTP dword straight from the register): retail stores the
//     result to a float right after the instruction. Never write (float)X87Sin(A) for such a
//     site: that rounds 80 -> 53 -> 24 bits where retail rounds 80 -> 24 once.
// The argument is passed as double; every retail argument found so far is a float load or a
// PC-rounded x87 product / quotient, which a double holds exactly.
//
// NOT here: asin / acos. Retail calls MSVCRT _CIasin / _CIacos (IAT thunks .text:06391fce /
// 06391fc2). On the target system the system msvcrt.dll's _CIasin / _CIacos and ucrtbase's
// asin / acos dispatch, with default MXCSR and masked x87 exceptions, to the same SSE2 code
// (instruction streams and constant tables compared byte for byte), so CRT asin / acos already
// match retail; see the tracker notes of 06350a60.
//
// Out-of-range operands (|x| >= 2^63) behave as the instruction does: FSIN / FCOS / FPTAN set
// C2 and leave the operand unchanged; retail has no fallback either.

#if !defined(_M_IX86)
#error "x87_trig.h: inline x87 assembly requires 32-bit x86"
#endif

// FLD; FSIN; FSTP qword.
inline double X87Sin(double Angle)
{
    double Result;
    __asm
    {
        fld qword ptr [Angle]
        fsin
        fstp qword ptr [Result]
    }
    return Result;
}

// FLD; FSIN; FSTP dword.
inline float X87SinF(double Angle)
{
    float Result;
    __asm
    {
        fld qword ptr [Angle]
        fsin
        fstp dword ptr [Result]
    }
    return Result;
}

// FLD; FCOS; FSTP qword.
inline double X87Cos(double Angle)
{
    double Result;
    __asm
    {
        fld qword ptr [Angle]
        fcos
        fstp qword ptr [Result]
    }
    return Result;
}

// FLD; FCOS; FSTP dword.
inline float X87CosF(double Angle)
{
    float Result;
    __asm
    {
        fld qword ptr [Angle]
        fcos
        fstp dword ptr [Result]
    }
    return Result;
}

// FLD; FPTAN; FSTP ST0 (drops the 1.0 FPTAN pushes); FSTP qword.
inline double X87Tan(double Angle)
{
    double Result;
    __asm
    {
        fld qword ptr [Angle]
        fptan
        fstp st(0)
        fstp qword ptr [Result]
    }
    return Result;
}

// FLD; FPTAN; FSTP ST0; FSTP dword.
inline float X87TanF(double Angle)
{
    float Result;
    __asm
    {
        fld qword ptr [Angle]
        fptan
        fstp st(0)
        fstp dword ptr [Result]
    }
    return Result;
}

// atan(X) as VC6 inlines it: FLD X; FLD1; FPATAN (= atan(ST1 / ST0)); FSTP qword.
inline double X87Atan(double X)
{
    double Result;
    __asm
    {
        fld qword ptr [X]
        fld1
        fpatan
        fstp qword ptr [Result]
    }
    return Result;
}

// FLD X; FLD1; FPATAN; FSTP dword.
inline float X87AtanF(double X)
{
    float Result;
    __asm
    {
        fld qword ptr [X]
        fld1
        fpatan
        fstp dword ptr [Result]
    }
    return Result;
}

// atan2(Y, X) as VC6 inlines it: FLD Y; FLD X; FPATAN (= atan(ST1 / ST0), quadrant from both
// signs); FSTP qword.
inline double X87Atan2(double Y, double X)
{
    double Result;
    __asm
    {
        fld qword ptr [Y]
        fld qword ptr [X]
        fpatan
        fstp qword ptr [Result]
    }
    return Result;
}

// FLD Y; FLD X; FPATAN; FSTP dword.
inline float X87Atan2F(double Y, double X)
{
    float Result;
    __asm
    {
        fld qword ptr [Y]
        fld qword ptr [X]
        fpatan
        fstp dword ptr [Result]
    }
    return Result;
}
