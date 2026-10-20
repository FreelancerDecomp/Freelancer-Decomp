// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once
#include "PortableSplineMath.h"

// Private Thorn arithmetic. FUN_06f21000 and its vector helpers retain
// extended x87 values only where the retail listing omits a float store.
namespace ThornSplineMath
{
    inline void Scale(float* Dst, const float* Src, float Factor)
    {
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
    }

    inline void Add(float* Dst, const float* A, const float* B)
    {
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
    }

    inline void SubtractVector(float* Dst, const float* A, const float* B)
    {
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
    }

    inline void DoubleStored(float* Dst, const float* Src)
    {
        ThornPortableSplineMath::DoubleStored(Dst, Src);
    }

    // Inlined operations in 21000 evaluate/store Z, Y, X in that order.
    inline void Subtract(float* Dst, const float* A, const float* B)
    {
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

    inline void CatmullRom3D(const float* P0, const float* P1, const float* P2,
                             const float* P3, float U, float* ResultVector)
    {
        float U2;
        float U3;
        __asm
        {
            fld U
            fmul U
            fst U2
            fmul U
            fstp U3
        }
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
    }
    // 23145/23181, 232c5/23301, and 2347c/234b8 zero-extend the
    // sample-index dword before FILD qword, then store the float product.
    inline float SampleParameter(int Index, float Step)
    {
        const unsigned long long UnsignedIndex = static_cast<unsigned int>(Index);
        float Result;
        __asm
        {
            fild qword ptr UnsignedIndex
            fmul Step
            fstp Result
        }
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
        ScaleStored(ThreeP2, P2, 3.0f);      // 21508..21531
        ScaleStored(ThreeP1, P1, 3.0f);      // 21539..21566
        NegateStored(MinusP0, P0);           // 2156e..2158b
        AddStored(Cubic, ThreeP1, MinusP0);  // 21593..215c6
        Subtract(Cubic, Cubic, ThreeP2);     // 215ce..21601
        AddStored(Cubic, Cubic, P3);         // 21609..21635
        const float Three = 3.0f;
        // 2163d..2167b never stores U^2 or 3U^2 before multiplication.
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
        AddStored(Sum, Quadratic, Cubic);    // 21682..216a9
        AddStored(Sum, Delta, Sum);          // 216b1..216d8
        const float Half = 0.5f;
        const float One = 1.0f;
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
    }
}
