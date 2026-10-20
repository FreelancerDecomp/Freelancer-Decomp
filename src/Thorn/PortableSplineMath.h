#pragma once

namespace ThornPortableSplineMath
{
    inline void NegateStored(float* dst, const float* src)
    {
        // Widen before negating: a raw sign-bit XOR would fail to quiet an
        // input signaling NaN and would skip the original floating-point load.
        // Materialize each load in Z/Y/X order, including overlapping outputs.
        volatile double z = static_cast<double>(src[2]);
        dst[2] = static_cast<float>(-z);
        volatile double y = static_cast<double>(src[1]);
        dst[1] = static_cast<float>(-y);
        volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(-x);
    }
}
