// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Helpers every RPGPU source file shares: the GENRESULT values RP8's callers test for, the -1
// handle, the debug log and the mip-chain length.

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

    inline unsigned int MaxMipLevels(unsigned int Width, unsigned int Height)
    {
        unsigned int Levels = 1;
        for (unsigned int Size = std::max(Width, Height); Size > 1; Size >>= 1)
            ++Levels;
        return Levels;
    }
}
