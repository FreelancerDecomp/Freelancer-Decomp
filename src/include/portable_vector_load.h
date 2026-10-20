#pragma once

#include <cstdint>
#include <cstring>
#include <cfenv>

namespace PortableVectorLoad
{
    inline void CopyMixed(float* dst, const float* xy, const double* zInput)
    {
        std::fenv_t saved;
        std::fegetenv(&saved);
        double rawZ;
        std::memcpy(&rawZ, zInput, sizeof(rawZ));
        const volatile double z = rawZ;
        float rawX;
        std::memcpy(&rawX, xy, sizeof(rawX));
        const volatile double x = static_cast<double>(rawX);
        std::uint32_t rawY;
        std::memcpy(&rawY, xy + 1, sizeof(rawY));
        dst[0] = static_cast<float>(x);
        dst[2] = static_cast<float>(z);
        std::memcpy(dst + 1, &rawY, sizeof(rawY));
        // Discard denormal-input flags caused only by a compiler's narrowed
        // result reload. Retain standard exceptions, then original input loads.
        std::feupdateenv(&saved);
        const volatile double restoredZ = *static_cast<volatile double*>(&rawZ);
        const volatile double restoredX = static_cast<double>(*static_cast<volatile float*>(&rawX));
        (void)restoredZ;
        (void)restoredX;
    }

    inline void CopyFour(float* dst, const float* src)
    {
        const volatile double w = static_cast<double>(src[3]);
        const volatile double z = static_cast<double>(src[2]);
        const volatile double y = static_cast<double>(src[1]);
        const volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(x);
        dst[1] = static_cast<float>(y);
        dst[2] = static_cast<float>(z);
        dst[3] = static_cast<float>(w);
    }

    // Numerical Z/Y/X loads precede X/Y/Z stores. Snapshotting preserves aliases
    // and quiets signaling NaNs; source stride is measured in floats.
    inline void Copy(float* dst, const float* src, unsigned stride = 1)
    {
        const volatile double z = static_cast<double>(src[2 * stride]);
        const volatile double y = static_cast<double>(src[stride]);
        const volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(x);
        dst[1] = static_cast<float>(y);
        dst[2] = static_cast<float>(z);
    }
}
