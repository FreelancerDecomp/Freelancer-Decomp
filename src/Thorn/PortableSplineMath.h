#pragma once

namespace ThornPortableSplineMath
{
    inline void NegateVector(float* dst, const float* src)
    {
        // Read Z/Y/X before publishing X/Y/Z, including for overlapping ranges.
        const volatile double z = static_cast<double>(src[2]);
        const volatile double y = static_cast<double>(src[1]);
        const volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(-x);
        dst[1] = static_cast<float>(-y);
        dst[2] = static_cast<float>(-z);
    }

    inline void DoubleStored(float* dst, const float* src)
    {
        // Keep float additions and the original Z/Y/X publication order.
        // Widening and narrowing through a temporary can add an x87 waiting
        // load, altering C1 and when an unmasked overflow is delivered.
        dst[2] = src[2] + src[2];
        dst[1] = src[1] + src[1];
        dst[0] = src[0] + src[0];
    }

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
