#pragma once

#include <cmath>

namespace X86QuaternionMath
{
    template<class Matrix, class Quaternion>
    inline void ToMatrix(Matrix& dst, const Quaternion& source)
    {
        const double w = source.W, x = source.X, y = source.Y, z = source.Z;
        const double scale = 2.0 / (((w * w + x * x) + y * y) + z * z);
        const double xs = x * scale, ys = y * scale, zs = z * scale;
        Matrix result;
        result.D[0][0] = static_cast<float>(1.0 - (y * ys + z * zs));
        result.D[0][1] = static_cast<float>(x * ys - w * zs);
        result.D[0][2] = static_cast<float>(x * zs + w * ys);
        result.D[1][0] = static_cast<float>(x * ys + w * zs);
        result.D[1][1] = static_cast<float>(1.0 - (x * xs + z * zs));
        result.D[1][2] = static_cast<float>(y * zs - w * xs);
        result.D[2][0] = static_cast<float>(x * zs - w * ys);
        result.D[2][1] = static_cast<float>(y * zs + w * xs);
        result.D[2][2] = static_cast<float>(1.0 - (x * xs + y * ys));
        dst = result;
    }

    template<class Quaternion, class Matrix>
    inline void FromMatrix(Quaternion& dst, const Matrix& source)
    {
        const Matrix matrix = source;
        Quaternion result{};
        const double trace = (static_cast<double>(matrix.D[1][1]) + matrix.D[0][0]) + matrix.D[2][2];
        if (trace > 0.0)
        {
            const double root = std::sqrt(trace + 1.0);
            const double scale = 0.5 / root;
            result.W = static_cast<float>(0.5 * root);
            result.X = static_cast<float>((static_cast<double>(matrix.D[2][1]) - matrix.D[1][2]) * scale);
            result.Y = static_cast<float>((static_cast<double>(matrix.D[0][2]) - matrix.D[2][0]) * scale);
            result.Z = static_cast<float>((static_cast<double>(matrix.D[1][0]) - matrix.D[0][1]) * scale);
        }
        else
        {
            int i = 0;
            if (matrix.D[1][1] > matrix.D[0][0]) i = 1;
            if (matrix.D[2][2] > matrix.D[i][i]) i = 2;
            const int j = (i + 1) % 3, k = (j + 1) % 3;
            const double root = std::sqrt((static_cast<double>(matrix.D[i][i]) -
                (static_cast<double>(matrix.D[k][k]) + matrix.D[j][j])) + 1.0);
            const double scale = 0.5 / root;
            float Quaternion::* const lane[] = {&Quaternion::X, &Quaternion::Y, &Quaternion::Z};
            result.*lane[i] = static_cast<float>(0.5 * root);
            result.W = static_cast<float>((static_cast<double>(matrix.D[k][j]) - matrix.D[j][k]) * scale);
            result.*lane[j] = static_cast<float>((static_cast<double>(matrix.D[i][j]) + matrix.D[j][i]) * scale);
            result.*lane[k] = static_cast<float>((static_cast<double>(matrix.D[i][k]) + matrix.D[k][i]) * scale);
        }
        dst = result;
    }

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
