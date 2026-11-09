#pragma once

#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>

namespace PortableIntegerConversion
{
    inline std::uint64_t ToInt64Bits(const double* input)
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                      "IEEE binary64 required");
        // Model truncating FISTP without an undefined out-of-range C++ cast.
        // Invalid conversions return the x87 integer-indefinite bit pattern.
        const volatile double loaded = *input;
        const double value = loaded;
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const bool negative = (bits >> 63) != 0;
        const unsigned exponent = static_cast<unsigned>((bits >> 52) & 0x7ff);
        const std::uint64_t fraction = bits & UINT64_C(0x000fffffffffffff);
        const int power = static_cast<int>(exponent) - 1023;
        if (power > 63 || (power == 63 && (!negative || fraction != 0)))
        {
            std::feraiseexcept(FE_INVALID);
            return UINT64_C(0x8000000000000000);
        }
        if (power < 0)
        {
            if (exponent || fraction) std::feraiseexcept(FE_INEXACT);
            return 0;
        }
        const std::uint64_t significand = fraction | UINT64_C(0x0010000000000000);
        std::uint64_t magnitude;
        if (power >= 52) magnitude = significand << (power - 52);
        else
        {
            const unsigned shift = 52 - power;
            if (significand & ((UINT64_C(1) << shift) - 1))
                std::feraiseexcept(FE_INEXACT);
            magnitude = significand >> shift;
        }
        return negative ? UINT64_C(0) - magnitude : magnitude;
    }

    inline void StoreFloatTriplet(const float* input, void* output, bool snapshotZFirst)
    {
        const auto load = [input](unsigned index) {
            float raw;
            std::memcpy(&raw, input + index, sizeof(raw));
            const volatile double promoted = static_cast<double>(raw);
            return static_cast<double>(promoted);
        };
        const auto store = [output](unsigned index, std::uint64_t value) {
            const auto word = static_cast<std::uint32_t>(value);
            std::memcpy(static_cast<unsigned char*>(output) + index * sizeof(word),
                        &word, sizeof(word));
        };
        // LIGHT loads one input ahead of each publication; ambient keeps Z
        // from the beginning. Preserve both orders when buffers overlap.
        double z = 0.0;
        if (snapshotZFirst) z = load(2);
        const double x = load(0);
        const auto ix = ToInt64Bits(&x);
        const double y = load(1);
        store(0, ix);
        const auto iy = ToInt64Bits(&y);
        if (!snapshotZFirst) z = load(2);
        store(1, iy);
        store(2, ToInt64Bits(&z));
    }
}
