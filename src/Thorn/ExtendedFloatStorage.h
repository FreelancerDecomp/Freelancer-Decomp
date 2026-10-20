#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

namespace ThornPortableMath
{
    // Canonical little-endian binary80 transport, independent of the host's
    // long double size or endianness. Every binary32 value widens exactly.
    inline void StoreFloatToExtended80(unsigned char* output, const float* value)
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
                      std::numeric_limits<double>::digits == 53, "IEEE binary64 required");
        // Preserve the original load's NaN quieting and exception boundary
        // before any caller output is written. A raw bit copy would skip it.
        const volatile double promoted = static_cast<double>(*value);
        const double captured = promoted;
        std::uint64_t bits;
        std::memcpy(&bits, &captured, sizeof(bits));
        const unsigned exponent = static_cast<unsigned>((bits >> 52) & 0x7ffu);
        const std::uint64_t fraction = bits & 0x000fffffffffffffull;
        const unsigned extendedExponent = exponent == 0 ? 0 : exponent == 0x7ff ? 0x7fff : exponent + (16383 - 1023);
        const std::uint64_t significand = exponent == 0 ? 0 : 0x8000000000000000ull | (fraction << 11);
        for (unsigned i = 0; i < 8; ++i)
            output[i] = static_cast<unsigned char>(significand >> (i * 8));
        output[8] = static_cast<unsigned char>(extendedExponent);
        output[9] = static_cast<unsigned char>((extendedExponent >> 8) | ((bits >> 63) << 7));
    }
}
