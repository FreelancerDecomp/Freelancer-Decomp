// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuShaders.h"

#include <cstring>

namespace
{
#include "RpGpuShaderBytecode.generated.inc"
}

const FRpGpuShaderBlob* FindRpGpuShader(const char* Name)
{
    if (Name == nullptr)
        return nullptr;
    for (const FRpGpuShaderBlob& Blob : kRpGpuShaders)
    {
        if (Blob.Name != nullptr && std::strcmp(Blob.Name, Name) == 0)
            return &Blob;
    }
    return nullptr;
}
