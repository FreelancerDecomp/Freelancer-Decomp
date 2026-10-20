#pragma once

#include <cmath>

namespace X86QuaternionMath
{
    inline float Blend(float a, float b, float t, bool reverse)
    {
        const double delta = reverse ? static_cast<double>(a) + b : static_cast<double>(b) - a;
        const double scaled = static_cast<double>(t) * delta;
        return static_cast<float>(reverse ? static_cast<double>(a) - scaled : static_cast<double>(a) + scaled);
    }

    template<class Quaternion>
    inline void Nlerp(Quaternion& dst, const Quaternion& a, const Quaternion& b, const float* t)
    {
        // Preserve the original dot order and choose the forward path for NaN.
        const double dot = ((static_cast<double>(a.Y) * b.Y + static_cast<double>(a.X) * b.X) +
                            static_cast<double>(b.Z) * a.Z) + static_cast<double>(a.W) * b.W;
        const bool reverse = dot < 0.0;

        // Publish W/X/Y/Z in order. Read t for each component: it may alias dst.
        dst.W = Blend(a.W, b.W, *t, reverse);
        dst.X = Blend(a.X, b.X, *t, reverse);
        dst.Y = Blend(a.Y, b.Y, *t, reverse);
        dst.Z = Blend(a.Z, b.Z, *t, reverse);

        const double lengthSquared = ((static_cast<double>(dst.W) * dst.W +
                                       static_cast<double>(dst.X) * dst.X) +
                                       static_cast<double>(dst.Y) * dst.Y) +
                                       static_cast<double>(dst.Z) * dst.Z;
        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        // As before, a zero-length blend has no special fallback.
        dst.W = static_cast<float>(inverseLength * dst.W);
        dst.X = static_cast<float>(inverseLength * dst.X);
        dst.Y = static_cast<float>(inverseLength * dst.Y);
        dst.Z = static_cast<float>(inverseLength * dst.Z);
    }
}
