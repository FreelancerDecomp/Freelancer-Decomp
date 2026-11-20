// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Portable equivalents of the short x87 sequences retail is built from (the packet codecs, the
// client's compare-and-branch blocks), for the port-only x64 build (docs/x64.md), which has no
// inline assembly. The Win32 build keeps
// its inline x87; these are used only where _M_IX86 is not defined.
//
// They give the same bits as the x87 sequence when:
// - the x87 precision control is 53-bit: the CRT's default, which the server keeps (only a
//   Direct3D 8 device lowers it to 24-bit, and the server creates none). A product or
//   quotient the x87 rounds to 53 bits is then the same as the SSE2 double result. Every
//   product here has float or int operands and double constants, so no intermediate reaches
//   the extended exponent range where the two would differ;
// - floating-point exceptions are masked. The Win32 code orders its pointer commits around a
//   possible unmasked exception (FLDedicated --unmask-fpu); x64 has no such mode.

#include <cmath>
#include <cstdint>
#include <cstring>

// FLD dword / FSTP dword: the bits are copied, except that a signaling NaN comes out quiet.
// Either side may be unaligned (a packet buffer).
inline void X87CopyFloat(void* Destination, const void* Source)
{
    std::uint32_t Bits;
    std::memcpy(&Bits, Source, 4);
    if ((Bits & 0x7f800000u) == 0x7f800000u && (Bits & 0x003fffffu) != 0 && (Bits & 0x00400000u) == 0)
        Bits |= 0x00400000u;
    std::memcpy(Destination, &Bits, 4);
}

// FLD qword / FSTP qword: the same for a double.
inline void X87CopyDouble(void* Destination, const void* Source)
{
    std::uint64_t Bits;
    std::memcpy(&Bits, Source, 8);
    const std::uint64_t Exponent = 0x7ff0000000000000ull;
    const std::uint64_t Quiet = 0x0008000000000000ull;
    if ((Bits & Exponent) == Exponent && (Bits & (Quiet - 1)) != 0 && (Bits & Quiet) == 0)
        Bits |= Quiet;
    std::memcpy(Destination, &Bits, 8);
}

// FISTP qword with the rounding control set to truncate (as MSVCRT _ftol sets it). A NaN, an
// infinity or a value outside the int64 range gives the integer indefinite,
// 0x8000000000000000.
inline long long X87TruncateToInt64(double Value)
{
    if (!(Value > -9223372036854775808.0 && Value < 9223372036854775808.0))
        return static_cast<long long>(0x8000000000000000ull);
    return static_cast<long long>(Value);
}

// FCOM / FCOMP / FCOMPP / FICOM followed by FNSTSW: the condition codes, in the bit positions of
// the status word's high byte (what FNSTSW AX leaves in AH): C0 = 0x01, C2 = 0x04, C3 = 0x40.
//   ST(0) > source: 0x00     ST(0) < source: 0x01     equal: 0x40     unordered: 0x45
// Both operands are already rounded (a stored float or double, or a register value the caller
// computed at the right precision), so the comparison itself is exact on every platform.
inline unsigned char X87CompareStatus(double St0, double Source)
{
    if (St0 > Source)
        return 0x00;
    if (St0 < Source)
        return 0x01;
    if (St0 == Source)
        return 0x40;
    return 0x45;
}

// The x86 parity flag after TEST AH, Mask: set when the tested bits hold an even number of ones.
// SETNP / JNP take the odd case, SETP / JP the even case.
inline bool X87TestParityEven(unsigned char Status, unsigned char Mask)
{
    unsigned char Bits = static_cast<unsigned char>(Status & Mask);
    Bits = static_cast<unsigned char>(Bits ^ (Bits >> 4));
    Bits = static_cast<unsigned char>(Bits ^ (Bits >> 2));
    Bits = static_cast<unsigned char>(Bits ^ (Bits >> 1));
    return (Bits & 1) == 0;
}

// FLDLG2 / FXCH / FYL2X: log10 of a value. FYL2X is not affected by the precision control and
// keeps a 64-bit mantissa; the portable value is the CRT's double log10, which can differ from
// the x87 result in the last bits, like x87_trig.h. The caller rounds the following operations
// as the x87 would at its precision (docs/x64.md, "Client precision").
inline double X87Log10(double Value)
{
    return std::log10(Value);
}
