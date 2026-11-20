// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#if !defined(_M_IX86)
// Port-only x64 build (docs/x64.md): portable versions of the x87 blocks below. Each value the
// x87 keeps on its stack is a double; it becomes a float only where the asm stores it.
#include <cmath>
#endif

// Private Thorn arithmetic. FUN_06f21000 and its vector helpers retain
// extended x87 values only where the retail listing omits a float store.
namespace ThornSplineMath
{
    inline void Scale(float* Dst, const float* Src, float Factor)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, Src
            mov eax, Dst
            fld Factor
            fmul dword ptr [ecx + 8]
            fld Factor
            fmul dword ptr [ecx + 4]
            fld Factor
            fmul dword ptr [ecx]
            fstp dword ptr [eax]
            fstp dword ptr [eax + 4]
            fstp dword ptr [eax + 8]
        }
#else
        // All three products are formed before the first store.
        const double ProductZ = static_cast<double>(Factor) * Src[2];
        const double ProductY = static_cast<double>(Factor) * Src[1];
        const double ProductX = static_cast<double>(Factor) * Src[0];
        Dst[0] = static_cast<float>(ProductX);
        Dst[1] = static_cast<float>(ProductY);
        Dst[2] = static_cast<float>(ProductZ);
#endif
    }

    inline void Add(float* Dst, const float* A, const float* B)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, A
            mov edx, B
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fadd dword ptr [edx + 8]
            fld dword ptr [ecx + 4]
            fadd dword ptr [edx + 4]
            fld dword ptr [ecx]
            fadd dword ptr [edx]
            fstp dword ptr [eax]
            fstp dword ptr [eax + 4]
            fstp dword ptr [eax + 8]
        }
#else
        const double SumZ = static_cast<double>(A[2]) + B[2];
        const double SumY = static_cast<double>(A[1]) + B[1];
        const double SumX = static_cast<double>(A[0]) + B[0];
        Dst[0] = static_cast<float>(SumX);
        Dst[1] = static_cast<float>(SumY);
        Dst[2] = static_cast<float>(SumZ);
#endif
    }

    inline void SubtractVector(float* Dst, const float* A, const float* B)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, A
            mov edx, B
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fsub dword ptr [edx + 8]
            fld dword ptr [ecx + 4]
            fsub dword ptr [edx + 4]
            fld dword ptr [ecx]
            fsub dword ptr [edx]
            fstp dword ptr [eax]
            fstp dword ptr [eax + 4]
            fstp dword ptr [eax + 8]
        }
#else
        const double DifferenceZ = static_cast<double>(A[2]) - B[2];
        const double DifferenceY = static_cast<double>(A[1]) - B[1];
        const double DifferenceX = static_cast<double>(A[0]) - B[0];
        Dst[0] = static_cast<float>(DifferenceX);
        Dst[1] = static_cast<float>(DifferenceY);
        Dst[2] = static_cast<float>(DifferenceZ);
#endif
    }

    inline void DoubleStored(float* Dst, const float* Src)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, Src
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fadd st(0), st(0)
            fstp dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fadd st(0), st(0)
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx]
            fadd st(0), st(0)
            fstp dword ptr [eax]
        }
#else
        const double SourceZ = Src[2];
        Dst[2] = static_cast<float>(SourceZ + SourceZ);
        const double SourceY = Src[1];
        Dst[1] = static_cast<float>(SourceY + SourceY);
        const double SourceX = Src[0];
        Dst[0] = static_cast<float>(SourceX + SourceX);
#endif
    }

    // Inlined operations in 21000 evaluate/store Z, Y, X in that order.
    inline void Subtract(float* Dst, const float* A, const float* B)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, A
            mov edx, B
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fsub dword ptr [edx + 8]
            fstp dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fsub dword ptr [edx + 4]
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx]
            fsub dword ptr [edx]
            fstp dword ptr [eax]
        }
#else
        Dst[2] = static_cast<float>(static_cast<double>(A[2]) - B[2]);
        Dst[1] = static_cast<float>(static_cast<double>(A[1]) - B[1]);
        Dst[0] = static_cast<float>(static_cast<double>(A[0]) - B[0]);
#endif
    }

    inline void AddStored(float* Dst, const float* A, const float* B)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, A
            mov edx, B
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fadd dword ptr [edx + 8]
            fstp dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fadd dword ptr [edx + 4]
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx]
            fadd dword ptr [edx]
            fstp dword ptr [eax]
        }
#else
        Dst[2] = static_cast<float>(static_cast<double>(A[2]) + B[2]);
        Dst[1] = static_cast<float>(static_cast<double>(A[1]) + B[1]);
        Dst[0] = static_cast<float>(static_cast<double>(A[0]) + B[0]);
#endif
    }

    inline void Negate(float* Dst, const float* Src)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, Src
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fchs
            fld dword ptr [ecx + 4]
            fchs
            fld dword ptr [ecx]
            fchs
            fstp dword ptr [eax]
            fstp dword ptr [eax + 4]
            fstp dword ptr [eax + 8]
        }
#else
        const double NegatedZ = -static_cast<double>(Src[2]);
        const double NegatedY = -static_cast<double>(Src[1]);
        const double NegatedX = -static_cast<double>(Src[0]);
        Dst[0] = static_cast<float>(NegatedX);
        Dst[1] = static_cast<float>(NegatedY);
        Dst[2] = static_cast<float>(NegatedZ);
#endif
    }

    inline void ScaleStored(float* Dst, const float* Src, float Factor)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, Src
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fmul Factor
            fstp dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fmul Factor
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx]
            fmul Factor
            fstp dword ptr [eax]
        }
#else
        Dst[2] = static_cast<float>(static_cast<double>(Src[2]) * Factor);
        Dst[1] = static_cast<float>(static_cast<double>(Src[1]) * Factor);
        Dst[0] = static_cast<float>(static_cast<double>(Src[0]) * Factor);
#endif
    }

    inline void NegateStored(float* Dst, const float* Src)
    {
#if defined(_M_IX86)
        __asm
        {
            mov ecx, Src
            mov eax, Dst
            fld dword ptr [ecx + 8]
            fchs
            fstp dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fchs
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx]
            fchs
            fstp dword ptr [eax]
        }
#else
        Dst[2] = static_cast<float>(-static_cast<double>(Src[2]));
        Dst[1] = static_cast<float>(-static_cast<double>(Src[1]));
        Dst[0] = static_cast<float>(-static_cast<double>(Src[0]));
#endif
    }

    inline void CatmullRom3D(const float* P0, const float* P1, const float* P2,
                             const float* P3, float U, float* ResultVector)
    {
        float U2;
        float U3;
#if defined(_M_IX86)
        __asm
        {
            fld U
            fmul U
            fst U2
            fmul U
            fstp U3
        }
#else
        // FST U2 rounds a copy; U^3 is formed from the unrounded U^2.
        const double USquared = static_cast<double>(U) * U;
        U2 = static_cast<float>(USquared);
        U3 = static_cast<float>(USquared * U);
#endif
        float TwiceP1[3], MinusP0[3], Delta[3], Linear[3];
        float FourP2[3], FiveP1[3], TwiceP0[3], Quadratic[3];
        float ThreeP2[3], ThreeP1[3], Cubic[3], Sum[3];
        Scale(TwiceP1, P1, 2.0f);             // 21039 / 235a0
        Negate(MinusP0, P0);                 // 21052 / 23090
        Add(Delta, MinusP0, P2);             // 2106f / 23540
        Scale(Linear, Delta, U);             // 21089 / 235a0
        Scale(FourP2, P2, 4.0f);             // 2109c
        Scale(FiveP1, P1, 5.0f);             // 210af
        DoubleStored(TwiceP0, P0);           // 210b4..210d4
        SubtractVector(Quadratic, TwiceP0, FiveP1); // 210f4 / 23570
        Add(Quadratic, Quadratic, FourP2);   // 2110e / 23540
        Subtract(Quadratic, Quadratic, P3);  // 21113..21140
        ScaleStored(Quadratic, Quadratic, U2); // 21148..21178
        ScaleStored(ThreeP2, P2, 3.0f);      // 21180..211aa
        ScaleStored(ThreeP1, P1, 3.0f);      // 211b2..211de
        NegateStored(MinusP0, P0);           // 211e6..21203
        AddStored(Cubic, ThreeP1, MinusP0);  // 2120b..2123e
        Subtract(Cubic, Cubic, ThreeP2);     // 21246..21279
        AddStored(Cubic, Cubic, P3);         // 21281..212b0
        ScaleStored(Cubic, Cubic, U3);       // 212b8..212eb
        AddStored(Sum, Quadratic, Cubic);    // 212f3..21323
        AddStored(Sum, Linear, Sum);         // 2132b..21352
        const float Half = 0.5f;
        float RoundedZ;
#if defined(_M_IX86)
        __asm
        {
            lea ecx, TwiceP1
            lea edx, Sum
            mov eax, ResultVector
            fld dword ptr [ecx]
            fadd dword ptr [edx]
            fld dword ptr [ecx + 4]
            fadd dword ptr [edx + 4]
            fld dword ptr [ecx + 8]
            fadd dword ptr [edx + 8]
            fstp RoundedZ
            fxch st(1)
            fmul Half
            fstp dword ptr [eax]
            fmul Half
            fstp dword ptr [eax + 4]
            fld RoundedZ
            fmul Half
            fstp dword ptr [eax + 8]
        }
#else
        const double SumX = static_cast<double>(TwiceP1[0]) + Sum[0];
        const double SumY = static_cast<double>(TwiceP1[1]) + Sum[1];
        const double SumZ = static_cast<double>(TwiceP1[2]) + Sum[2];
        RoundedZ = static_cast<float>(SumZ);
        ResultVector[0] = static_cast<float>(SumX * Half);
        ResultVector[1] = static_cast<float>(SumY * Half);
        ResultVector[2] = static_cast<float>(static_cast<double>(RoundedZ) * Half);
#endif
    }
    // 23145/23181, 232c5/23301, and 2347c/234b8 zero-extend the
    // sample-index dword before FILD qword, then store the float product.
    inline float SampleParameter(int Index, float Step)
    {
        const unsigned long long UnsignedIndex = static_cast<unsigned int>(Index);
        float Result;
#if defined(_M_IX86)
        __asm
        {
            fild qword ptr UnsignedIndex
            fmul Step
            fstp Result
        }
#else
        Result = static_cast<float>(static_cast<double>(UnsignedIndex) * Step);
#endif
        return Result;
    }

    // Identical interpolation tails at 231ac..2321b and 2332c..2339b.
    inline void Lerp3D(const float* A, const float* B, float Fraction, float* ResultVector)
    {
        float ZDifference;
        float XProduct;
        float XResult;
        float YResult;
        float ZResult;
#if defined(_M_IX86)
        __asm
        {
            mov ecx, A
            mov edx, B
            mov eax, ResultVector
            fld dword ptr [edx]
            fsub dword ptr [ecx]
            fld dword ptr [edx + 4]
            fsub dword ptr [ecx + 4]
            fld dword ptr [edx + 8]
            fsub dword ptr [ecx + 8]
            fstp ZDifference
            fxch st(1)
            fmul Fraction
            fstp XProduct
            fmul Fraction
            fld ZDifference
            fmul Fraction
            fld dword ptr [ecx]
            fadd XProduct
            fstp XResult
            fld dword ptr [ecx + 4]
            mov edx, XResult
            fadd st(0), st(2)
            mov dword ptr [eax], edx
            fstp YResult
            fld dword ptr [ecx + 8]
            mov edx, YResult
            fadd st(0), st(1)
            mov dword ptr [eax + 4], edx
            fstp ZResult
            mov edx, ZResult
            fstp st(0)
            mov dword ptr [eax + 8], edx
            fstp st(0)
        }
#else
        const double DifferenceX = static_cast<double>(B[0]) - A[0];
        const double DifferenceY = static_cast<double>(B[1]) - A[1];
        const double DifferenceZ = static_cast<double>(B[2]) - A[2];
        ZDifference = static_cast<float>(DifferenceZ);
        XProduct = static_cast<float>(DifferenceX * Fraction);
        const double YProduct = DifferenceY * Fraction;
        const double ZProduct = static_cast<double>(ZDifference) * Fraction;
        XResult = static_cast<float>(static_cast<double>(A[0]) + XProduct);
        // Each A element is loaded before the previous result word is published.
        const double AY = A[1];
        ResultVector[0] = XResult;
        YResult = static_cast<float>(AY + YProduct);
        const double AZ = A[2];
        ResultVector[1] = YResult;
        ZResult = static_cast<float>(AZ + ZProduct);
        ResultVector[2] = ZResult;
#endif
    }

    inline void CatmullRomTangent3D(const float* P0, const float* P1, const float* P2,
                                    const float* P3, float U, float* ResultVector)
    {
        float MinusP0[3], Delta[3], FourP2[3], FiveP1[3], TwiceP0[3];
        float Quadratic[3], ThreeP2[3], ThreeP1[3], Cubic[3], Sum[3];
        Negate(MinusP0, P0);                 // 213eb
        Add(Delta, MinusP0, P2);             // 21405
        Scale(FourP2, P2, 4.0f);             // 21418
        Scale(FiveP1, P1, 5.0f);             // 21432
        DoubleStored(TwiceP0, P0);           // 21437..21457
        SubtractVector(Quadratic, TwiceP0, FiveP1); // 21477
        Add(Quadratic, Quadratic, FourP2);   // 21491
        Subtract(Quadratic, Quadratic, P3);  // 21496..214c3
        // 214cb..21501 keeps 2U extended across all three products.
#if defined(_M_IX86)
        __asm
        {
            lea eax, Quadratic
            fld U
            fadd st(0), st(0)
            fld dword ptr [eax + 8]
            fmul st(0), st(1)
            fstp dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fmul st(0), st(1)
            fstp dword ptr [eax + 4]
            fld dword ptr [eax]
            fmul st(0), st(1)
            fstp dword ptr [eax]
            fstp st(0)
        }
#else
        const double TwiceU = static_cast<double>(U) + U;
        Quadratic[2] = static_cast<float>(Quadratic[2] * TwiceU);
        Quadratic[1] = static_cast<float>(Quadratic[1] * TwiceU);
        Quadratic[0] = static_cast<float>(Quadratic[0] * TwiceU);
#endif
        ScaleStored(ThreeP2, P2, 3.0f);      // 21508..21531
        ScaleStored(ThreeP1, P1, 3.0f);      // 21539..21566
        NegateStored(MinusP0, P0);           // 2156e..2158b
        AddStored(Cubic, ThreeP1, MinusP0);  // 21593..215c6
        Subtract(Cubic, Cubic, ThreeP2);     // 215ce..21601
        AddStored(Cubic, Cubic, P3);         // 21609..21635
        const float Three = 3.0f;
        // 2163d..2167b never stores U^2 or 3U^2 before multiplication.
#if defined(_M_IX86)
        __asm
        {
            lea eax, Cubic
            fld U
            fmul U
            fmul Three
            fld dword ptr [eax + 8]
            fmul st(0), st(1)
            fstp dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fmul st(0), st(1)
            fstp dword ptr [eax + 4]
            fld dword ptr [eax]
            fmul st(0), st(1)
            fstp dword ptr [eax]
            fstp st(0)
        }
#else
        const double ThreeUSquared = static_cast<double>(U) * U * Three;
        Cubic[2] = static_cast<float>(Cubic[2] * ThreeUSquared);
        Cubic[1] = static_cast<float>(Cubic[1] * ThreeUSquared);
        Cubic[0] = static_cast<float>(Cubic[0] * ThreeUSquared);
#endif
        AddStored(Sum, Quadratic, Cubic);    // 21682..216a9
        AddStored(Sum, Delta, Sum);          // 216b1..216d8
        const float Half = 0.5f;
        const float One = 1.0f;
#if defined(_M_IX86)
        __asm
        {
            lea ecx, Sum
            mov eax, ResultVector
            fld dword ptr [ecx]
            fmul Half
            fstp dword ptr [eax]
            fld dword ptr [ecx + 4]
            fmul Half
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx + 8]
            fmul Half
            fstp dword ptr [eax + 8]
            // 2172b..21749: extended (X^2+Y^2)+Z^2, extended sqrt.
            fld dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fld dword ptr [eax]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fsqrt
            fstp st(3)
            fstp st(0)
            fstp st(0)
            // Retail unconditionally divides, including a zero norm.
            fdivr One
            fld st(0)
            fmul dword ptr [eax]
            fstp dword ptr [eax]
            fld st(0)
            fmul dword ptr [eax + 4]
            fstp dword ptr [eax + 4]
            fmul dword ptr [eax + 8]
            fstp dword ptr [eax + 8]
        }
#else
        ResultVector[0] = static_cast<float>(static_cast<double>(Sum[0]) * Half);
        ResultVector[1] = static_cast<float>(static_cast<double>(Sum[1]) * Half);
        ResultVector[2] = static_cast<float>(static_cast<double>(Sum[2]) * Half);
        const double X = ResultVector[0];
        const double Y = ResultVector[1];
        const double Z = ResultVector[2];
        const double Inverse = One / std::sqrt((X * X + Y * Y) + Z * Z);
        ResultVector[0] = static_cast<float>(Inverse * ResultVector[0]);
        ResultVector[1] = static_cast<float>(Inverse * ResultVector[1]);
        ResultVector[2] = static_cast<float>(Inverse * ResultVector[2]);
#endif
    }
}
