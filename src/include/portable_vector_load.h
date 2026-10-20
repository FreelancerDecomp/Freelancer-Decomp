#pragma once

namespace PortableVectorLoad
{
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
