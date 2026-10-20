#pragma once

#include <cmath>

namespace X86ScalarMath
{
    inline float Sqrt(float x)
    {
        return std::sqrt(x);
    }

    inline float InvSqrt(float x)
    {
        // Widen before the reciprocal to avoid rounding the square root to
        // float first. Every positive finite float has a finite inverse root.
        return static_cast<float>(1.0 / std::sqrt(static_cast<double>(x)));
    }
}
