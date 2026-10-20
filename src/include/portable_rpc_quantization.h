#pragma once

#include "portable_integer_conversion.h"

namespace PortableRpcQuantization
{
    inline std::uint64_t QuantizeFire(const float* value, bool medium)
    {
        // Power-of-two prescaling is exact; the integer multiplier needs at
        // most 39 significand bits, including for full-range binary32 inputs.
        const double prescaled = static_cast<double>(*value) *
            (medium ? 0.0001220703125 : 0.015625);
        const double scaled = prescaled * (medium ? 32767.0 : 127.0);
        return PortableIntegerConversion::ToInt64Bits(&scaled);
    }

    inline std::uint64_t QuantizeUpdate(const float* value, unsigned int* pointerToCommit,
                                       unsigned int committedPointer)
    {
        const volatile double scaled = static_cast<double>(*value) * 127.0;
        if (pointerToCommit) *pointerToCommit = committedPointer;
        const double result = scaled;
        return PortableIntegerConversion::ToInt64Bits(&result);
    }

    // For the recovered 127/255 integer scales, a binary32 product needs at
    // most 32 significand bits, so storing it in binary64 loses no precision.
    inline std::uint64_t QuantizeByte(const float* value, double scale)
    {
        const double scaled = static_cast<double>(*value) * scale;
        return PortableIntegerConversion::ToInt64Bits(&scaled);
    }

    inline void ExpandInteger(int value, double inverseScale, float* output)
    {
        *output = static_cast<float>(static_cast<double>(value) * inverseScale);
    }
}
