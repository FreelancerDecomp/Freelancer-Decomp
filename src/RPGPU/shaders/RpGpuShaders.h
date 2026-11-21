// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU's compiled shaders: every src/RPGPU/shaders/*.hlsl in the three formats SDL_GPU takes
// (SPIR-V for Vulkan, DXIL for Direct3D 12, MSL for Metal), compiled offline by
// tools/rpgpu_shaders.ps1 into RpGpuShaderBytecode.generated.inc (docs/re/rpgpu.md, "Shaders").

#include <cstddef>

struct FRpGpuShaderBlob
{
    const char* Name; // the source name without .hlsl, e.g. "rpgpu_stage0.frag"
    const unsigned char* Spirv;
    std::size_t SpirvSize;
    const unsigned char* Dxil;
    std::size_t DxilSize;
    // Metal source text, zero-terminated; MslSize excludes the terminator. SPIRV-Cross renames
    // the entry point: it is main0 in MSL, main in SPIR-V and DXIL.
    const unsigned char* Msl;
    std::size_t MslSize;
};

// The shader of that name, or null when it is not compiled in.
const FRpGpuShaderBlob* FindRpGpuShader(const char* Name);
