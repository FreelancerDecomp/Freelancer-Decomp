#pragma once

#include <cstring>
#include <limits>

namespace PortableFastDot
{
    inline float Zero() { return 0.0f; }

    template<unsigned Axis> inline float Component(const void* vector)
    {
        static_assert(Axis < 3 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "Three IEEE binary32 components required");
        float value;
        std::memcpy(&value, static_cast<const unsigned char*>(vector) + Axis * sizeof(float), sizeof(value));
        return value;
    }
}
