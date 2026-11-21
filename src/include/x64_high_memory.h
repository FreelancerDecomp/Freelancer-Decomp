// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// A test switch for the x64 build (PORT(x64), docs/x64.md): with FL_HIGH_MEMORY=1 in the
// environment, a program reserves all free address space below 4 GB when it starts, so later
// allocations, file mappings and DLLs land above it. Code that still keeps an address in 32 bits
// then fails at once, not only when the address happens to be high.
//
// Memory taken before the call stays where it is: the process heap's first segment is usually
// below 4 GB, and the heap keeps using it until it is full. So the switch catches most, not all,
// truncated addresses. Win32 ignores it.
//
// One window stays free: LowArena (x64_low_arena.h) hands out EngBase's handles below 2 GB,
// because retail fields keep them in 32 bits. Allocations that land in that window by chance
// are not caught.

#include "x64_low_arena.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#if defined(_WIN64)
inline void ReserveLowAddressSpaceIfRequested()
{
    char Value[8] = {};
    if (GetEnvironmentVariableA("FL_HIGH_MEMORY", Value, sizeof(Value)) == 0 || Value[0] != '1')
        return;

    const std::uintptr_t Limit = 0x100000000ull;
    const std::uintptr_t Granularity = 0x10000; // VirtualAlloc reserves in 64 KB units
    std::uintptr_t Reserved = 0;
    std::uintptr_t Address = Granularity;
    MEMORY_BASIC_INFORMATION Info;
    while (Address < Limit && VirtualQuery((void*)(Address), &Info, sizeof(Info)) == sizeof(Info))
    {
        const std::uintptr_t Base = (std::uintptr_t)(Info.BaseAddress);
        const std::uintptr_t End = Base + Info.RegionSize;
        if (Info.State == MEM_FREE)
        {
            const std::uintptr_t Start = (Base + Granularity - 1) & ~(Granularity - 1);
            const std::uintptr_t Stop = (End < Limit ? End : Limit) & ~(Granularity - 1);
            // The free range, minus LowArena's window: up to two pieces.
            const std::uintptr_t HoleStart = LowArena::WindowBase;
            const std::uintptr_t HoleStop = LowArena::WindowBase + LowArena::WindowSize;
            const std::uintptr_t Pieces[2][2] = {
                {Start, Stop < HoleStart ? Stop : HoleStart},
                {Start > HoleStop ? Start : HoleStop, Stop},
            };
            for (const auto& Piece : Pieces)
            {
                if (Piece[0] < Piece[1] &&
                    VirtualAlloc((void*)(Piece[0]), Piece[1] - Piece[0], MEM_RESERVE, PAGE_NOACCESS) != nullptr)
                    Reserved = Reserved + (Piece[1] - Piece[0]);
            }
        }
        Address = End;
    }
    std::fprintf(stderr, "FL_HIGH_MEMORY: reserved %llu MB below 4 GB (LowArena window left free)\n",
                 (unsigned long long)(Reserved >> 20));
}
#else
inline void ReserveLowAddressSpaceIfRequested()
{
}
#endif
