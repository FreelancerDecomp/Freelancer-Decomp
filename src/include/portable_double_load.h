#pragma once

#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>

namespace PortableDoubleLoad
{
    inline void Copy(void* destination, const void* source)
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                      "IEEE binary64 required");
        double input;
        std::memcpy(&input, source, sizeof(input));
        // Retain numerical load effects, including x87 subnormal input flags.
        const volatile double loaded = input;
        const double result = loaded;
        std::uint64_t originalBits, resultBits;
        std::memcpy(&originalBits, source, sizeof(originalBits));
        std::memcpy(&resultBits, &result, sizeof(resultBits));
        if ((originalBits & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000) &&
            !(originalBits & UINT64_C(0x0008000000000000)))
        {
            // Guarantee quieting even on targets whose ordinary copies retain sNaN.
            std::feraiseexcept(FE_INVALID);
            resultBits = originalBits | UINT64_C(0x0008000000000000);
        }
        std::memcpy(destination, &resultBits, sizeof(resultBits));
    }
}
