#pragma once

namespace X86BatchMath
{
    template<class Vector, class Matrix>
    inline void TransformList(Vector* dst, const Matrix& matrix, const Vector* src,
                              int count, const Vector* translation = nullptr)
    {
        if (count <= 0) return;

        // Parameters remain constant even if output overlaps their storage.
        const Matrix coefficients = matrix;
        const Vector offset = translation ? *translation : Vector{0.0f, 0.0f, 0.0f};
        for (int index = 0; index < count; ++index)
        {
            // Snapshot the source vector so exact in-place transforms are safe.
            const double x = src[index].X;
            const double y = src[index].Y;
            const double z = src[index].Z;
            const auto component = [&](int row, double addend)
            {
                return static_cast<float>(((z * coefficients.D[row][2] +
                                            y * coefficients.D[row][1]) +
                                            x * coefficients.D[row][0]) + addend);
            };
            dst[index].X = component(0, offset.X);
            dst[index].Y = component(1, offset.Y);
            dst[index].Z = component(2, offset.Z);
        }
    }
}
