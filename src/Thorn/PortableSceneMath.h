#pragma once

#include <cstdint>
#include <cstring>

namespace ThornPortableSceneMath
{
    inline void Transpose9(float* dst, const float* src)
    {
        // Retain input order, and snapshot every input before any output.
        const volatile double v8 = static_cast<double>(src[8]);
        std::uint32_t raw3;
        std::memcpy(&raw3, src + 3, sizeof(raw3));
        const volatile double v5 = static_cast<double>(src[5]);
        const volatile double v2 = static_cast<double>(src[2]);
        const volatile double v7 = static_cast<double>(src[7]);
        const volatile double v4 = static_cast<double>(src[4]);
        const volatile double v1 = static_cast<double>(src[1]);
        const volatile double v6 = static_cast<double>(src[6]);
        const volatile double v0 = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(v0);
        std::memcpy(dst + 1, &raw3, sizeof(raw3));
        dst[2] = static_cast<float>(v6);
        dst[3] = static_cast<float>(v1);
        dst[4] = static_cast<float>(v4);
        dst[5] = static_cast<float>(v7);
        dst[6] = static_cast<float>(v2);
        dst[7] = static_cast<float>(v5);
        dst[8] = static_cast<float>(v8);
    }
}
