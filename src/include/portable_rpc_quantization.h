#pragma once

namespace PortableRpcQuantization
{
    inline void ExpandInteger(int value, double inverseScale, float* output)
    {
        *output = static_cast<float>(static_cast<double>(value) * inverseScale);
    }
}
