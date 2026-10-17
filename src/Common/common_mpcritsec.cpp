// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MPCritSec: a lazily initialised Win32 critical section. Split out of common_net.cpp.

#include "common_net.h"
#include "common_misc.h" // GbSinglePlayer

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>

// ===========================================================================
// MPCritSec
// ===========================================================================

MPCritSec::MPCritSec()
    : bInitialized(false)
{
    // 06304750: retail stores only the byte flag; the section bytes stay indeterminate
    // until lock() initialises them. The port zeroes them (owner's over-initialisation rule).
    memset(&CriticalSection, 0, sizeof(CriticalSection));
}

MPCritSec::~MPCritSec()
{
    // 06304760: flag set -> clear it, then DeleteCriticalSection.
    if (bInitialized)
    {
        bInitialized = false;
        DeleteCriticalSection(&CriticalSection);
    }
}

MPCritSec& MPCritSec::operator=(MPCritSec const& Other)
{
    // Retail (0x06261d70): REP MOVSD of all 7 dwords (CriticalSection, the flag and its
    // padding), no self-assignment test (a self-copy is the identity, hence memmove). See
    // the header comment for the aliasing caveat this reproduces.
    memmove((void*)this, &Other, sizeof(MPCritSec));
    return *this;
}

void MPCritSec::lock()
{
    // 0x06304780: single-player (DAT_063ed17c != 0) -> return without touching the section.
    // Otherwise lazily InitializeCriticalSection on first use (byte flag +0x18), then Enter.
    if (GbSinglePlayer)
        return;
    if (!bInitialized)
    {
        InitializeCriticalSection(&CriticalSection);
        bInitialized = true;
    }
    EnterCriticalSection(&CriticalSection);
}

void MPCritSec::unlock()
{
    // 0x063047b0: single-player -> return; otherwise Leave (no bInitialized check).
    if (GbSinglePlayer)
        return;
    LeaveCriticalSection(&CriticalSection);
}
