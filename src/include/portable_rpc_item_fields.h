#pragma once

#include <cstring>

namespace PortableRpcItemFields
{
    inline void Store(unsigned int* idOutput, int* countOutput, float* healthOutput,
                      unsigned char* mountedOutput, unsigned int id, int count,
                      const float* health, unsigned char mounted)
    {
        const volatile double loadedHealth = static_cast<double>(*health);
        *idOutput = id;
        *countOutput = count;
        *mountedOutput = mounted;
        const float result = static_cast<float>(loadedHealth);
        std::memcpy(healthOutput, &result, sizeof(result));
    }
}
