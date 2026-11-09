#pragma once

#include "portable_integer_conversion.h"
#include <cmath>

namespace PortableRpcQuantization
{
    inline unsigned char PickFireTier(const float* value, bool hasMediumTier)
    {
        const volatile double promoted = static_cast<double>(*value);
        std::uint32_t bits;
        std::memcpy(&bits, value, sizeof(bits));
        if ((bits & 0x7fffffffu) > 0x7f800000u)
        {
            std::feraiseexcept(FE_INVALID);
            return 0;
        }
        const double magnitude = std::fabs(static_cast<double>(promoted));
        if (magnitude < 64.0) return 1;
        return hasMediumTier && magnitude < 8192.0 ? 2 : 0;
    }

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
