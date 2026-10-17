// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll ICRSplineSegment (retail 0x063327f0-0x06332df0): Catmull-Rom spline
// evaluation, arc length and parameter search. See src/include/common_aisteer.h for the
// field-layout writeup.
//
// Precision follows the listing: every Vector helper result (scale, negate, add, subtract,
// three-float constructor) is stored as float; values that stay on the x87 stack (chord
// components, sums of squares, the final sum of calculate_spline_vector) are double.

#include "common_aisteer.h"

#include <cmath>

namespace
{
    // One component of calculate_spline_vector, in retail's term order (063327f0-06332b9f):
    //   0.5 * (2*P1 + ((-P0 + P2)*T + ((2P0 - 5P1 + 4P2 - P3)*T2 + (3P1 + -P0 - 3P2 + P3)*T3)))
    // The two inner sums are added before the 2*P1 term; the last add and the * 0.5 run on
    // the stack.
    float SplineComponent(float P0, float P1, float P2, float P3, float T, float T2, float T3)
    {
        float const TwoP1 = P1 * 2.0f;
        float const Linear = (-P0 + P2) * T;
        float const Quadratic = ((P0 + P0) - P1 * 5.0f + P2 * 4.0f - P3) * T2;
        float const Cubic = (P1 * 3.0f + -P0 - P2 * 3.0f + P3) * T3;
        float const Rest = Linear + (Quadratic + Cubic);
        return (float)(((double)TwoP1 + Rest) * 0.5);
    }
}

// ----------------------------------------------------------------------------
// ICRSplineSegment -- see common_aisteer.h for the field-layout writeup.
// ----------------------------------------------------------------------------

Vector ICRSplineSegment::calculate_spline_vector(float T)
{
    // T*T is stored as float (FST); T^3 multiplies the unrounded T*T by T on the stack.
    float const T2 = T * T;
    float const T3 = (float)((double)T * T * T);

    Vector Result;
    Result.x = SplineComponent(P0.x, P1.x, P2.x, P3.x, T, T2, T3);
    Result.y = SplineComponent(P0.y, P1.y, P2.y, P3.y, T, T2, T3);
    Result.z = SplineComponent(P0.z, P1.z, P2.z, P3.z, T, T2, T3);
    return Result;
}

Vector ICRSplineSegment::calculate_spline_tangent(float T)
{
    // 06332bb0: the 0.001 offset is a double (0639c9f0); the shifted T goes out as float.
    Vector const A = calculate_spline_vector((float)((double)T - 0.001));
    Vector const B = calculate_spline_vector((float)((double)T + 0.001));

    // The differences stay on the stack, except that the z difference is also stored as
    // float (FST) and that float is the second factor of z*z and the source of Result.z.
    double const Dx = (double)B.x - A.x;
    double const Dy = (double)B.y - A.y;
    double const Dz = (double)B.z - A.z;
    float const DzStored = (float)Dz;

    float const InvLen = (float)(1.0 / std::sqrt(Dz * DzStored + Dy * Dy + Dx * Dx));
    Vector Result;
    Result.x = (float)(Dx * InvLen);
    Result.y = (float)(Dy * InvLen);
    Result.z = DzStored * InvLen;
    return Result;
}

void ICRSplineSegment::calculate_arclength()
{
    // 06332c60. The first chord is measured from the raw P0, not from spline(0) == P1.
    float PrevX = P0.x, PrevY = P0.y, PrevZ = P0.z;
    float const Step = (float)(1.0 / (double)SampleCount);
    ArcLength = 0.0f;
    for (unsigned i = 1; i <= SampleCount; ++i)
    {
        Vector const V = calculate_spline_vector((float)((double)i * Step));
        double const Dx = (double)V.x - PrevX;
        double const Dy = (double)V.y - PrevY;
        double const Dz = (double)V.z - PrevZ;
        PrevX = V.x;
        PrevY = V.y;
        PrevZ = V.z;
        ArcLength = (float)(std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx) * Step + ArcLength);
    }
}

float ICRSplineSegment::get_spline_length()
{
    // 06332d30. The first chord is measured from P1 here, unlike calculate_arclength.
    float PrevX = P1.x, PrevY = P1.y, PrevZ = P1.z;
    float Total = 0.0f;
    for (unsigned i = 1; i <= SampleCount; ++i)
    {
        // очень странно: retail computes the parameter with an unsigned integer DIV
        // (06332d66), so T is 0 for every step but the last, which is 1.
        Vector const V = calculate_spline_vector((float)(i / SampleCount));
        double const Dx = (double)V.x - PrevX;
        double const Dy = (double)V.y - PrevY;
        double const Dz = (double)V.z - PrevZ;
        PrevX = V.x;
        PrevY = V.y;
        PrevZ = V.z;
        Total = (float)(std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx) + Total);
    }
    return Total;
}

float ICRSplineSegment::get_t_from_point(Vector const& Point, float StartT, float StepT)
{
    // 06332df0. A forward line search: step T while the squared distance to Point does not
    // get worse, and return the last accepted T. Can loop forever, as retail.
    // The first distance sums dx^2 + dz^2 + dy^2 and is stored as float; the loop sums
    // dz^2 + dy^2 + dx^2 and compares the unstored value (FCOM float, TEST AH,0x41, JZ:
    // stop only when greater; equal or unordered keeps going).
    Vector const Start = calculate_spline_vector(StartT);
    double const StartDx = (double)Start.x - Point.x;
    double const StartDy = (double)Start.y - Point.y;
    double const StartDz = (double)Start.z - Point.z;
    float BestDist = (float)(StartDx * StartDx + StartDz * StartDz + StartDy * StartDy);

    float T = StartT;
    for (;;)
    {
        float const NextT = T + StepT;
        Vector const V = calculate_spline_vector(NextT);
        double const Dx = (double)V.x - Point.x;
        double const Dy = (double)V.y - Point.y;
        double const Dz = (double)V.z - Point.z;
        double const NextDist = Dz * Dz + Dy * Dy + Dx * Dx;
        if (NextDist > BestDist)
            break;
        BestDist = (float)NextDist;
        T = NextT;
    }
    return T;
}
