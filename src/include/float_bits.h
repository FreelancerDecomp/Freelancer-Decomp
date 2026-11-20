// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstring>

// std::bit_cast for C++17: a float and its 32-bit pattern, one to the other. The bits are
// copied with memcpy (no aliasing UB, no FLD/FSTP of the value in between), so a raw
// immediate such as retail's `PUSH 0x3cf5c28f` comes back bit for bit.
static_assert(sizeof(float) == sizeof(unsigned int), "float and unsigned int must be 32-bit");

inline float FloatFromBits(unsigned int Bits)
{
    float Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

inline unsigned int BitsFromFloat(float Value)
{
    unsigned int Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}
