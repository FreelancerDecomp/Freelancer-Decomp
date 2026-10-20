#pragma once

#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ThornPortableComparison
{
    inline bool StrictLess(const float* left, const float* right)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "IEEE binary32 required");
        // The original loads the left operand before comparing. Preserve its
        // subnormal/signaling-NaN load effects even if the right operand is NaN.
        const volatile double promoted = static_cast<double>(*left);
        std::uint32_t a, b;
        std::memcpy(&a, left, sizeof(a));
        std::memcpy(&b, right, sizeof(b));
        if ((a & 0x7fffffffu) > 0x7f800000u || (b & 0x7fffffffu) > 0x7f800000u)
        {
            // FCOMP is signaling for quiet NaNs too; ordinary C++ < need not be.
            std::feraiseexcept(FE_INVALID);
            return false;
        }
        return promoted < *right;
    }
}
