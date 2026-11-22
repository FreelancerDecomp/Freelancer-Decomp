// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// PORT(rpgpu): RPGPU.dll, the SDL3 + SDL_GPU render pipeline; no retail counterpart.

#pragma once

// RPGPU's state ids and matrix helper (RpGpuState.cpp).

#include <cstring>

namespace RpGpuDetail
{
    // Pipeline state ids (RP8's [RenderPipeline] keys).
    enum : int
    {
        kPsTexture = 0,
        kPsLighting = 1,
        kPsTextureLod = 2,
        kPsAllow8Bit = 3,
        kPsAllow32Bit = 4,
        kPsAllowDxt = 5,
        kPsMaxBlendStages = 6,
        kPsViewspaceLights = 7,
        kPsClearColor = 8,
        kPsClearZDepth = 9,
        kPsClearStencil = 10,
        kPsStateCache = 11,
        kPsBrokenMod2x = 12,
        kPsBrokenFlip = 13,
        kPsCount = 14,
    };

    inline void Identity(float* M)
    {
        std::memset(M, 0, 16 * sizeof(float));
        M[0] = M[5] = M[10] = M[15] = 1.0f;
    }
}
