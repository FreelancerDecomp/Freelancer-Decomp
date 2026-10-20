// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once
#include "PortableSplineMath.h"
#include <cmath>

// Private Thorn arithmetic. FUN_06f21000 and its vector helpers retain
// extended x87 values only where the retail listing omits a float store.
namespace ThornSplineMath
{
    inline void Scale(float* Dst, const float* Src, float Factor)
    {
        const float x = Src[0] * Factor;
        const float y = Src[1] * Factor;
        const float z = Src[2] * Factor;

        Dst[0] = x;
        Dst[1] = y;
        Dst[2] = z;
    }

    inline void Add(float* Dst, const float* A, const float* B)
    {
        const float x = A[0] + B[0];
        const float y = A[1] + B[1];
        const float z = A[2] + B[2];

        Dst[0] = x;
        Dst[1] = y;
        Dst[2] = z;
    }

    inline void SubtractVector(float* Dst, const float* A, const float* B)
    {
        const float x = A[0] - B[0];
        const float y = A[1] - B[1];
        const float z = A[2] - B[2];

        Dst[0] = x;
        Dst[1] = y;
        Dst[2] = z;
    }

    inline void DoubleStored(float* Dst, const float* Src)
    {
        ThornPortableSplineMath::DoubleStored(Dst, Src);
    }

    // Inlined operations in 21000 evaluate/store Z, Y, X in that order.
    inline void Subtract(float* Dst, const float* A, const float* B)
    {
        ThornPortableSplineMath::SubtractStored(Dst, A, B);
    }

    inline void AddStored(float* Dst, const float* A, const float* B)
    {
        ThornPortableSplineMath::AddStored(Dst, A, B);
    }

    inline void Negate(float* Dst, const float* Src)
    {
        ThornPortableSplineMath::NegateVector(Dst, Src);
    }

    inline void ScaleStored(float* Dst, const float* Src, float Factor)
    {
        ThornPortableSplineMath::ScaleStored(Dst, Src, &Factor);
    }

    inline void NegateStored(float* Dst, const float* Src)
    {
        ThornPortableSplineMath::NegateStored(Dst, Src);
    }

    inline void CatmullRom3D(const float* P0, const float* P1, const float* P2, const float* P3, float U, float* ResultVector) 
    {
        const float U2 = U * U;
        const float U3 = U2 * U;

        for (int i = 0; i < 3; ++i)
        {
            const float p0 = P0[i];
            const float p1 = P1[i];
            const float p2 = P2[i];
            const float p3 = P3[i];

            const float linear = -p0 + p2;
            const float quadratic = 2.0f * p0 - 5.0f * p1
                                  + 4.0f * p2 - p3;
            const float cubic = -p0 + 3.0f * p1
                              - 3.0f * p2 + p3;

            ResultVector[i] = 0.5f * (
                2.0f * p1 +
                linear * U +
                quadratic * U2 +
                cubic * U3
            );
        }
    }
    // 23145/23181, 232c5/23301, and 2347c/234b8 zero-extend the
    // sample-index dword before FILD qword, then store the float product.
    inline float SampleParameter(int Index, float Step)
    {
        const auto unsignedIndex = static_cast<unsigned int>(Index);

        return static_cast<float>(
            static_cast<long double>(unsignedIndex) *
            static_cast<long double>(Step)
        );
    }

    // Identical interpolation tails at 231ac..2321b and 2332c..2339b.
    inline void Lerp3D(const float* A, const float* B, float Fraction, float* ResultVector)
    {
        const float x = A[0] + (B[0] - A[0]) * Fraction;
        const float y = A[1] + (B[1] - A[1]) * Fraction;
        const float z = A[2] + (B[2] - A[2]) * Fraction;

        ResultVector[0] = x;
        ResultVector[1] = y;
        ResultVector[2] = z;
    }
    
    inline void CatmullRomTangent3D(
        const float* P0,
        const float* P1,
        const float* P2,
        const float* P3,
        float U,
        float* ResultVector)
    {
        const float u2 = U * U;
        const float twoU = 2.0f * U;
        const float threeU2 = 3.0f * u2;

        float tangent[3];

        for (int i = 0; i < 3; ++i)
        {
            const float p0 = P0[i];
            const float p1 = P1[i];
            const float p2 = P2[i];
            const float p3 = P3[i];

            const float linear = -p0 + p2;
            const float quadratic = 2.0f * p0 - 5.0f * p1
                                  + 4.0f * p2 - p3;
            const float cubic = -p0 + 3.0f * p1
                              - 3.0f * p2 + p3;

            tangent[i] = 0.5f * (
                linear +
                twoU * quadratic +
                threeU2 * cubic
            );
        }

        const float length = std::sqrt(
            tangent[0] * tangent[0] +
            tangent[1] * tangent[1] +
            tangent[2] * tangent[2]
        );

        // Preserve the original behavior: no zero-length check.
        const float invLength = 1.0f / length;

        ResultVector[0] = tangent[0] * invLength;
        ResultVector[1] = tangent[1] * invLength;
        ResultVector[2] = tangent[2] * invLength;
    }

}
