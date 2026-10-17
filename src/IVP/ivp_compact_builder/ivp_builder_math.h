// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <cstring>

// ============================================================================
// Small numeric helpers shared by the polygon / tetra / pointsoup builder
// chain (2026-10-08, claude-code:cmn-ivp4b). Retail evaluates these on the
// x87 stack; every intermediate that the listing keeps on the stack is a
// double here and only the FSTP dword stores round to float.
// ============================================================================

// Raw .rdata float constants are written by their bit patterns.
inline float IVP_BuilderBitsToFloat(uint32_t Bits)
{
    float Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

inline double IVP_BuilderBitsToDouble(uint64_t Bits)
{
    double Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

// Retail FUN_0628bed0 (thiscall: this = Out, stdcall A, B; RET 8), the
// Vector cross product Out = A x B. Every product is formed on the x87 stack
// and each component is rounded once by its FSTP dword:
//   x = A.y*B.z - A.z*B.y, y = B.x*A.z - A.x*B.z, z = A.x*B.y - A.y*B.x
// (.text:0628bed8-0628bf05). All inputs are read before Out is written.
inline void IVP_BuilderCrossProduct(float* Out, const float* A, const float* B)
{
    const double AxBy = (double)(A[0]) * B[1];
    const double AyBx = (double)(A[1]) * B[0];
    const double Y = (double)(B[0]) * A[2] - (double)(A[0]) * B[2];
    const double X = (double)(A[1]) * B[2] - (double)(A[2]) * B[1];
    Out[0] = (float)(X);
    Out[1] = (float)(Y);
    Out[2] = (float)(AxBy - AyBx);
}
