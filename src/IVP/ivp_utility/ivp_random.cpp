// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_random.h"

namespace
{
    // Retail DAT_063ee4dc: the generator's sole persistent state, a plain
    // 32-bit int (not thread-local, not reset anywhere in this cluster --
    // matches retail, which never re-seeds it from any function reachable in
    // this pass).
    unsigned g_IVP_FastRandomSeed = 1;
}

float IVP_FastRandomFloat()
{
    // seed *= 75 (== seed*3, then *5, then *5 again -- exactly retail's three
    // LEA-based multiplies, Common.dll.txt:443975-443977).
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 3u;
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 5u;
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 5u;

    const unsigned Low16 = g_IVP_FastRandomSeed & 0xffffu;
    return (float)Low16 * (1.0f / 65536.0f);
}
