#pragma once

#include "BatchTransform.h"

namespace X86MatrixMath
{
    template<class Matrix>
    inline void Transpose(Matrix& dst, const Matrix& source)
    {
        Matrix result;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                result.D[row][col] = source.D[col][row];
        dst = result;
    }

    template<class Matrix>
    inline void Multiply(Matrix& dst, const Matrix& a, const Matrix& b)
    {
        Matrix result;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                result.D[row][col] = static_cast<float>(
                    (static_cast<double>(a.D[row][2]) * b.D[2][col] +
                     static_cast<double>(a.D[row][1]) * b.D[1][col]) +
                     static_cast<double>(a.D[row][0]) * b.D[0][col]);
        dst = result;
    }

    template<class Vector, class Matrix>
    inline void TransformVector(Vector& dst, const Matrix& matrix, const Vector& source)
    {
        X86BatchMath::TransformList(&dst, matrix, &source, 1);
    }

    template<class Vector, class Matrix>
    inline void TransformTranspose(Vector& dst, const Matrix& matrix, const Vector& source)
    {
        Matrix transposed;
        Transpose(transposed, matrix);
        TransformVector(dst, transposed, source);
    }
}
