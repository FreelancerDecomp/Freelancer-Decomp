// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP aligned allocator pair. Moved verbatim from src/Common/common_stubs.cpp
// (via common_stubs_a3.cpp) on 2026-10-14 (claude-code:cmn-a3); renamed from
// the raw extern "C" FUN_06343fc0 / FUN_06343ff0. No logic changed.

#include "ivp_malloc.h"

#include <cstdint>
#include <cstdlib>

// FUN_06343fc0 (.text:06343fc0-06343fea) -- generic aligned allocator, paired with the
// free half below. Mallocs `Size + Align + 8` bytes (FUN_06343f70 = malloc), rounds
// raw + Align + 7 down to a multiple of Align (`NEG ESI; AND EAX,ESI`, only correct when
// Align is a power of two -- true at every call site, e.g. 0627fc4b pushes 0x10), writes
// the 0x65981234 marker dword at the raw block's start (nothing reads it back) and keeps
// the raw malloc pointer at Aligned[-1] for ivp_free_aligned. Retail does not test the
// malloc result, so neither does this.
void* __cdecl ivp_malloc_aligned(unsigned Size, unsigned Align) // formerly extern "C" FUN_06343fc0
{
    unsigned char* Raw = (unsigned char*)malloc(Size + Align + 8);                           // 06343fc9-06343fce
    unsigned char* Aligned = (unsigned char*)(((uintptr_t)Raw + Align + 7) & (0 - (uintptr_t)Align)); // 06343fd5-06343fdb
    *(unsigned*)Raw = 0x65981234;                                                            // 06343fe0
    ((void**)Aligned)[-1] = Raw;                                                             // 06343fe6
    return Aligned;
}

// FUN_06343ff0 (.text:06343ff0-06343ffe) -- the free half: recover the raw malloc pointer
// kept one dword before the aligned block and free it (FUN_06343fb0 = free). No null
// test -- every caller tests first (e.g. PhyArch::Part::~Part 0627f9fc, Part::clear
// 0627fa41).
void __cdecl ivp_free_aligned(void* AlignedPtr) // formerly extern "C" FUN_06343ff0
{
    void* RawPtr = ((void**)AlignedPtr)[-1]; // 06343ff4
    free(RawPtr);                            // 06343ff8
}
