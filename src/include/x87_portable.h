// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Portable equivalents of the short x87 sequences retail's packet codecs are built from, for
// the port-only x64 build (docs/x64.md), which has no inline assembly. The Win32 build keeps
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
