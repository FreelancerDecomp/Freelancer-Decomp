#pragma once

#include <cstring>
#include <limits>

namespace PortableFloatLoad
{
    // Copy a binary32 through the same numerical load/store boundary as the
    // former FLD/FSTP pair. Raw memcpy alone would retain signaling NaNs and
    // omit load exceptions. Byte transport also permits unaligned/aliased data.
    inline void Copy(void* destination, const void* source)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                      std::numeric_limits<double>::digits >= 24, "IEEE binary32 required");
        float input;
        std::memcpy(&input, source, sizeof(input));
        const volatile double promoted = static_cast<double>(input);
        const float result = static_cast<float>(promoted);
        std::memcpy(destination, &result, sizeof(result));
    }
}
