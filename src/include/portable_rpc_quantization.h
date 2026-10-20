#pragma once

namespace PortableRpcQuantization
{
    inline void ExpandByte(int value, double inverseScale, float* output)
    {
        *output = static_cast<float>(static_cast<double>(value) * inverseScale);
    }
}
