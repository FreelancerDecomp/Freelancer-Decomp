// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// PORT(rpgpu): RPGPU.dll, the SDL3 + SDL_GPU render pipeline; no retail counterpart.

#pragma once

// Helpers every RPGPU source file shares: the GENRESULT values RP8's callers test for, the -1
// handle, the debug log, RP8's primitive count and the mip-chain length.

#include "dacom.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace RpGpuDetail
{
    // GENRESULT values RP8's callers test for.
    inline constexpr GENRESULT kUnsupported = (GENRESULT)0xFFFFFFF9; // the operation does not exist
    inline constexpr GENRESULT kFail = (GENRESULT)0x80004005;        // E_FAIL, the buffer calls

    // Handle -1: the scratch index buffer, or the pool vertex buffer of a source FVF.
    inline void* const kMinusOne = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));

    inline void Log(const char* Format, ...)
    {
        char Text[512];
        va_list Args;
        va_start(Args, Format);
        std::vsnprintf(Text, sizeof(Text), Format, Args);
        va_end(Args);
        OutputDebugStringA(Text);
    }

    // RP8's PrimCountFromVerts: primitives from a vertex or index count, with RP8's signed
    // divisions, so a count too small stays <= 0.
    inline int PrimCountFromVerts(unsigned long Prim, unsigned int Count)
    {
        const int Signed = static_cast<int>(Count);
        switch (Prim)
        {
        case 1: return Signed;
        case 2: return Signed / 2;
        case 3: return Signed - 1;
        case 4: return Signed / 3;
        case 5:
        case 6: return Signed - 2;
        default: return 0;
        }
    }

    inline unsigned int MaxMipLevels(unsigned int Width, unsigned int Height)
    {
        unsigned int Levels = 1;
        for (unsigned int Size = std::max(Width, Height); Size > 1; Size >>= 1)
            ++Levels;
        return Levels;
    }
}
