// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <cmath>
#include "common_csys.h"
#include "x86math.h" // I3DMathEngine

#include "DALib.h"   // DALib::Engine, for DALib_Engine_get_csys
#include "dacom.h"   // DACOM_Acquire, AGGDESC

#include <math.h>
#include <stdlib.h>  // abs(int) -- retail truncates the dot product through it

namespace
{
    void CopyVector(Vector& Dst, Vector const& Src)
    {
        Dst.x = Src.x;
        Dst.y = Src.y;
        Dst.z = Src.z;
    }

    void CopyMatrix(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Row][Col];
        }
    }

    void TransposeMatrix(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Col][Row];
        }
    }

}

void CsysFromPosOrient(Csys& Out, float const* Pos3, float const* Orient9)
{
    Out.Pos.x = Pos3[0];
    Out.Pos.y = Pos3[1];
    Out.Pos.z = Pos3[2];
    unsigned Cell = 0;
    for (; Cell < 9; ++Cell)
        Out.Orient.E[Cell / 3][Cell % 3] = Orient9[Cell];
}

Csys::Csys()
{
}

void Csys::set_identity()
{
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

void Csys::init(Vector const& NewPos, Matrix const& NewOrient)
{
    CopyVector(Pos, NewPos);
    CopyMatrix(Orient, NewOrient);
}

Transform Csys::get_transform() const
{
    Transform Out;
    CopyMatrix(Out.Rotation, Orient);
    CopyVector(Out.Translation, Pos);
    return Out;
}

void Csys::set_transform(Transform const& Xform)
{
    CopyVector(Pos, Xform.Translation);
    CopyMatrix(Orient, Xform.Rotation);
}

// The Csys transforms go through the 3DMathEngine (retail's TU cache DAT_063fceb8, lazily
// filled by FUN_06391cf0 = Acquire3DMathEngine); its return codes are ignored, as in retail.

// .text:06307490..063074fb: slot +0x2c TransformMatrix(Rotated, Orient, Local), then
// Rotated + Pos, one FADD / FSTP float per component.
Vector Csys::trans_from_frame(Vector const& Local) const
{
    FVector Rotated;
    Acquire3DMathEngine()->TransformMatrix(Rotated, (FMatrix const&)Orient, (FVector const&)Local);
    Vector Out;
    Out.x = Rotated.X + Pos.x;
    Out.y = Rotated.Y + Pos.y;
    Out.z = Rotated.Z + Pos.z;
    return Out;
}

// .text:06307500..06307544: slot +0x20 MulMatrix(Out, Orient, Local).
Matrix Csys::trans_from_frame(Matrix const& Local) const
{
    Matrix Out;
    Acquire3DMathEngine()->MulMatrix((FMatrix&)Out, (FMatrix const&)Orient, (FMatrix const&)Local);
    return Out;
}

// .text:06307550..06307603: the two members above in this order (TransformMatrix + Pos, then
// MulMatrix).
Csys Csys::trans_from_frame(Csys const& Local) const
{
    Csys Out;
    Out.Pos = trans_from_frame(Local.Pos);
    Out.Orient = trans_from_frame(Local.Orient);
    return Out;
}

// .text:06307610..0630767b: Delta = World - Pos (FSUB, FSTP float each), then slot +0x38
// TransposeTransform(Out, Orient, Delta).
Vector Csys::trans_to_frame(Vector const& World) const
{
    Vector Delta;
    Delta.x = World.x - Pos.x;
    Delta.y = World.y - Pos.y;
    Delta.z = World.z - Pos.z;
    Vector Out;
    Acquire3DMathEngine()->TransposeTransform((FVector&)Out, (FMatrix const&)Orient, (FVector const&)Delta);
    return Out;
}

// .text:06307680..06307702: transposed copy of Orient (FLD/FSTP float), then slot +0x20
// MulMatrix(Out, Transposed, World).
Matrix Csys::trans_to_frame(Matrix const& World) const
{
    Matrix Transposed;
    TransposeMatrix(Transposed, Orient);
    Matrix Out;
    Acquire3DMathEngine()->MulMatrix((FMatrix&)Out, (FMatrix const&)Transposed, (FMatrix const&)World);
    return Out;
}

// .text:06307710..0630780c: the two members above in this order.
Csys Csys::trans_to_frame(Csys const& World) const
{
    Csys Out;
    Out.Pos = trans_to_frame(World.Pos);
    Out.Orient = trans_to_frame(World.Orient);
    return Out;
}

// .text:063079b0..06307a73: slot +0x38 TransposeTransform(Back, Orient, Pos); Pos' = -Back
// (FCHS, FSTP float); Orient' = transpose(Orient).
Csys Csys::get_inverse() const
{
    FVector Back;
    Acquire3DMathEngine()->TransposeTransform(Back, (FMatrix const&)Orient, (FVector const&)Pos);
    Csys Out;
    Out.Pos.x = -Back.X;
    Out.Pos.y = -Back.Y;
    Out.Pos.z = -Back.Z;
    TransposeMatrix(Out.Orient, Orient);
    return Out;
}

Csys __cdecl Csys_identity()
{
    Csys Out;
    Out.set_identity();
    return Out;
}

Csys __cdecl Csys_init(Vector const& Pos, Matrix const& Orient)
{
    Csys Out;
    Out.init(Pos, Orient);
    return Out;
}

// .text:06307a80..06307b2f: the body of Csys::trans_from_frame(Csys) with Left as `this`.
Csys __cdecl operator*(Csys const& Left, Csys const& Right)
{
    return Left.trans_from_frame(Right);
}

// .text:063038b0..06303a55 (cdecl, hidden Matrix* result). Values the listing keeps on the x87
// stack are double here; every FSTP float is a float here.
//   063038b4..063038d5  L = sqrt((x*x + y*y) + z*z) on the stack
//   063038d7..063038e2  FCOM double 1e-4 (DAT_0639c2b8); TEST AH,0x41; JNZ -> the degenerate
//                       look (1, 0, 0) on less, equal or unordered; the fall-through is L > 1e-4
//   063038e4..06303922  Inv = -1.0 (DAT_063a03f0) / L; Look[i] = float(Inv * Dir[i])
//   0630393a..0630396e  |LookX| then |LookY| against double 1e-4: `TEST AH,0x5; JP` jumps on
//                       greater, equal or unordered -> Side = (-LookX, LookY); only when both
//                       are strictly less does Side become (0.0f, 1.0f) (DAT_06399408 /
//                       DAT_0639a1d0)
//   06303970..063039b4  C0 = float(LookY*0 - LookZ*SideX), C1 = float(LookZ*SideY - LookX*0),
//                       C2 = float(SideX*LookX - SideY*LookY) -- the *0.0f terms are real
//                       multiplies (they carry a NaN / infinity through)
//   063039b8..063039eb  InvN = float(1.0f / sqrt((C2*C2 + C1*C1) + C0*C0)); no length guard
//   063039ef..06303a07  C0n = C0*InvN and C1n = C1*InvN stay on the stack, C2n is FSTP float
//   06303a0f..06303a4f  E[0][0] = LookZ*C1n - C2n*LookY; E[1][0] = C2n*LookX - LookZ*C0n;
//                       E[2][0] = C0n*LookY - C1n*LookX; E[0][1] = C0n; E[1][1] = C1n;
//                       E[2][1] = C2n; column 2 = Look (E[2][2] stored at 063039d4)
Matrix __cdecl LookMatrix(Vector const& Direction)
{
    Matrix Out;

    const double X = Direction.x;
    const double Y = Direction.y;
    const double Z = Direction.z;
    const double Length = sqrt((X * X + Y * Y) + Z * Z);

    float LookX;
    float LookY;
    float LookZ;
    if (Length > 1e-4)
    {
        const double Inv = -1.0 / Length;
        LookX = (float)(Inv * Direction.x);
        LookY = (float)(Inv * Direction.y);
        LookZ = (float)(Inv * Direction.z);
    }
    else
    {
        LookX = 1.0f;
        LookY = 0.0f;
        LookZ = 0.0f;
    }

    double SideX;
    double SideY;
    if (!(fabs((double)(LookX)) < 1e-4) || !(fabs((double)(LookY)) < 1e-4))
    {
        SideX = -(double)(LookX);
        SideY = LookY;
    }
    else
    {
        SideX = 0.0;
        SideY = 1.0;
    }

    const float C0 = (float)((double)(LookY) * 0.0 - (double)(LookZ) * SideX);
    const float C1 = (float)((double)(LookZ) * SideY - (double)(LookX) * 0.0);
    const float C2 = (float)(SideX * LookX - SideY * LookY);

    const float InvN = (float)(1.0 / sqrt(((double)(C2) * C2 + (double)(C1) * C1) +
                                                     (double)(C0) * C0));
    const double C0n = (double)(C0) * InvN;
    const double C1n = (double)(C1) * InvN;
    const float C2n = (float)((double)(C2) * InvN);

    Out.E[2][2] = LookZ;
    Out.E[2][1] = C2n;
    Out.E[0][0] = (float)((double)(LookZ) * C1n - (double)(C2n) * LookY);
    Out.E[1][0] = (float)((double)(C2n) * LookX - (double)(LookZ) * C0n);
    Out.E[2][0] = (float)(C0n * LookY - C1n * LookX);
    Out.E[0][1] = (float)(C0n);
    Out.E[1][1] = (float)(C1n);
    Out.E[0][2] = LookX;
    Out.E[1][2] = LookY;
    return Out;
}

Matrix __cdecl LookMatrixYup(Vector const& Direction)
{
    // Common.dll listing 06303a60..06303c0d (RVA 0xa3a60). Hidden Matrix* dest at [ESP+4], the
    // Vector reference at [ESP+8]. Values the listing keeps on the x87 stack are double here;
    // every FSTP float is a float here.
    Matrix Out;

    // 06303a64..06303a7d: LenSq = (x*x + y*y) + z*z on the stack. 06303a85: FCOM double 1e-4
    // (DAT_0639c2b8); TEST AH,0x41; JNZ -> the degenerate look (0, 0, -1) on less, equal or
    // unordered; the fall-through (LenSq > 1e-4) normalises with -1.0 (DAT_063a03f0) / sqrt.
    const double X = Direction.x;
    const double Y = Direction.y;
    const double Z = Direction.z;
    const double LenSq = (X * X + Y * Y) + Z * Z;
    float LookX;
    float LookY;
    float LookZ;
    if (LenSq > 1e-4)
    {
        const double Inv = -1.0 / sqrt(LenSq);
        LookX = (float)(Inv * X);   // 06303a9e
        LookY = (float)(Inv * Y);   // 06303aaf
        LookZ = (float)(Inv * Z);   // 06303abe
    }
    else
    {
        LookX = 0.0f;                                     // 06303ade (DAT_06399408)
        LookY = 0.0f;                                     // 06303ae4
        LookZ = -1.0f;                                    // 06303ad6 (0xbf800000)
    }

    // 06303aea..06303b22: |LookX| then |LookZ| against double 1e-4, `TEST AH,0x5; JP` jumps on
    // greater, equal or unordered -> Ref = (-LookX, 0, LookZ); only when both are strictly less
    // is Ref = (0, 0, 1) (DAT_06399408 / DAT_0639a1d0). Both stay on the x87 stack.
    // The float Look components as the x87 stack holds them (exact).
    const double Lx = LookX;
    const double Ly = LookY;
    const double Lz = LookZ;
    double RefX;
    double RefZ;
    if (!(fabs(Lx) < 1e-4) || !(fabs(Lz) < 1e-4))
    {
        RefX = -Lx;
        RefZ = Lz;
    }
    else
    {
        RefX = 0.0;
        RefZ = 1.0;
    }

    // 06303b24..06303b6a: each product and difference on the stack, one FSTP float each. The
    // X and Z components are the cross product of (RefX, 0, RefZ) with Look; the middle one is
    // RefZ*LookZ - RefX*LookX (still perpendicular to Look). The *0.0f terms are real
    // multiplies (they carry a NaN / infinity through).
    const double UpX = (float)(RefX * Ly - Lz * 0.0f);
    const double UpY = (float)(RefZ * Lz - RefX * Lx);
    const double UpZ = (float)(Lx * 0.0f - Ly * RefZ);

    // 06303b78..06303ba3: UpInv = float(1.0f / sqrt((UpZ^2 + UpY^2) + UpX^2)) -- no length
    // guard. 06303ba7..06303bbf: UpX and UpY scaled stay on the stack, UpZ is FSTP float.
    const float UpInv = (float)(1.0f / sqrt((UpZ * UpZ + UpY * UpY) + UpX * UpX));
    const double UpXn = UpX * UpInv;
    const double UpYn = UpY * UpInv;
    const double UpZn = (float)(UpZ * UpInv);

    // 06303b84 / 06303bcb / 06303bc7..06303c07: column 2 = Look, column 1 = Up, column 0 =
    // Up x Look, each element one double expression narrowed by its FSTP float.
    Out.E[2][2] = LookZ;
    Out.E[2][1] = (float)UpZn;
    Out.E[0][0] = (float)(Lz * UpYn - UpZn * Ly);
    Out.E[1][0] = (float)(UpZn * Lx - UpXn * Lz);
    Out.E[2][0] = (float)(Ly * UpXn - UpYn * Lx);
    Out.E[0][1] = (float)UpXn;
    Out.E[1][1] = (float)UpYn;
    Out.E[0][2] = LookX;
    Out.E[1][2] = LookY;
    return Out;
}

Matrix __cdecl EulerMatrix(Vector const& AnglesDegrees)
{
    // Common.dll.txt 06304230..0630431b. The three angles are degrees * DAT_0639d210, each FSTP
    // float (06304237..0630426a). FSIN / FCOS of those floats stay on the x87 stack (double
    // here); cos(yaw) is spilled as a DOUBLE (06304286 FSTP double local_38), so it is not
    // narrowed either. Only SinR*CosP (0630429e) and CosR*CosP (063042a6) are FSTP float;
    // SinY*SinP (06304296) stays on the stack. Every element is one double expression narrowed
    // by its own FSTP float into the nine-float local copied out at 06304310.
    static const float kDegToRad = 0.017453292f;

    const float Pitch = AnglesDegrees.x * kDegToRad;
    const float Yaw = AnglesDegrees.y * kDegToRad;
    const float Roll = AnglesDegrees.z * kDegToRad;

    const double SinP = std::sin(static_cast<double>(Pitch));
    const double CosP = std::cos(static_cast<double>(Pitch));
    const double SinY = std::sin(static_cast<double>(Yaw));
    const double CosY = std::cos(static_cast<double>(Yaw));
    const double SinR = std::sin(static_cast<double>(Roll));
    const double CosR = std::cos(static_cast<double>(Roll));

    const double SinYSinP = SinY * SinP;
    const double SinRCosP = (float)(SinR * CosP);
    const double CosRCosP = (float)(CosR * CosP);

    Matrix Out;
    Out.E[0][0] = (float)(CosR * CosY);                       // 063042b0
    Out.E[0][1] = (float)(SinYSinP * CosR - SinRCosP);        // 063042bc
    Out.E[0][2] = (float)(CosRCosP * SinY + SinR * SinP);     // 063042cc
    Out.E[1][0] = (float)(SinR * CosY);                       // 063042d6
    Out.E[1][1] = (float)(SinYSinP * SinR + CosRCosP);        // 063042e0
    Out.E[1][2] = (float)(SinRCosP * SinY - CosR * SinP);     // 063042f0
    Out.E[2][0] = (float)(-SinY);                             // 063042f8
    Out.E[2][1] = (float)(CosY * SinP);                       // 06304302
    Out.E[2][2] = (float)(CosY * CosP);                       // 0630430c
    return Out;
}

Matrix __cdecl RotateMatrix(Vector const& AnglesDegrees)
{
    // Common.dll.txt:06303fe0-063041ab (2026-10-08, claude-code:cmn-phy3: re-ported onto
    // the math engine, closing the "Мелкие экспорты Common" ledger item). Accum starts as
    // identity (06303ff5-0630403a) and one axis at a time is folded in, in the order Y
    // (06303fe8), X (06304091), Z (06304135):
    //     I3DMathEngine slot 8 (+0x20) MulMatrix(&Tmp, &Accum, &Rot); Accum = Tmp (REP MOVSD 9)
    // through the file-static engine cache DAT_063fce08 (lazy FUN_06391cf0 ==
    // Acquire3DMathEngine). An axis whose |angle| is not greater than 0.01 (double
    // .rdata:0639e6b0; `TEST AH,0x41; JNZ` skips on less/equal/unordered) is skipped with its
    // multiply. Angles are degrees * DAT_0639d210 (0.017453292f).
    //   Y: FUN_063041b0(float(y*k)): cos/sin of that float on the FPU, each stored as float;
    //      E00 = cos, E02 = sin, E20 = -sin, E22 = cos, E11 = 1, rest 0.
    //   X: inlined (063040a6-06304101): `FST float [arg]` keeps x*k UNROUNDED on the stack
    //      for FCOS, while FSIN reads back the float-rounded copy;
    //      E00 = 1, E11 = cos, E12 = -sin, E21 = sin, E22 = cos, rest 0.
    //   Z: FUN_063041f0(float(z*k)): E00 = cos, E01 = -sin, E10 = sin, E11 = cos, E22 = 1.
    // The engine's return code is ignored, as in retail.
    static const float kDegToRad = 0.017453292f;   // .rdata:0639d210
    static const double kAngleEpsilon = 0.01;      // .rdata:0639e6b0, compared against fabs()

    Matrix Accum;
    MatrixSetIdentity(Accum);
    Matrix Tmp;

    if (fabs((double)(AnglesDegrees.y)) > kAngleEpsilon)
    {
        const float Angle = AnglesDegrees.y * kDegToRad;
        const float Cos = static_cast<float>(std::cos(static_cast<double>(Angle)));
        const float Sin = static_cast<float>(std::sin(static_cast<double>(Angle)));
        Matrix Rot;
        Rot.E[0][0] = Cos;  Rot.E[0][1] = 0.0f; Rot.E[0][2] = Sin;
        Rot.E[1][0] = 0.0f; Rot.E[1][1] = 1.0f; Rot.E[1][2] = 0.0f;
        Rot.E[2][0] = -Sin; Rot.E[2][1] = 0.0f; Rot.E[2][2] = Cos;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)Tmp, (FMatrix const&)Accum, (FMatrix const&)Rot);
        CopyMatrix(Accum, Tmp);
    }

    if (fabs((double)(AnglesDegrees.x)) > kAngleEpsilon)
    {
        const double AngleExact = (double)(AnglesDegrees.x) * kDegToRad;
        const float AngleRounded = (float)(AngleExact);
        const float Cos = static_cast<float>(std::cos(static_cast<double>(AngleExact)));
        const float Sin = static_cast<float>(std::sin(static_cast<double>(AngleRounded)));
        Matrix Rot;
        Rot.E[0][0] = 1.0f; Rot.E[0][1] = 0.0f; Rot.E[0][2] = 0.0f;
        Rot.E[1][0] = 0.0f; Rot.E[1][1] = Cos;  Rot.E[1][2] = -Sin;
        Rot.E[2][0] = 0.0f; Rot.E[2][1] = Sin;  Rot.E[2][2] = Cos;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)Tmp, (FMatrix const&)Accum, (FMatrix const&)Rot);
        CopyMatrix(Accum, Tmp);
    }

    if (fabs((double)(AnglesDegrees.z)) > kAngleEpsilon)
    {
        const float Angle = AnglesDegrees.z * kDegToRad;
        const float Cos = static_cast<float>(std::cos(static_cast<double>(Angle)));
        const float Sin = static_cast<float>(std::sin(static_cast<double>(Angle)));
        Matrix Rot;
        Rot.E[0][0] = Cos;  Rot.E[0][1] = -Sin; Rot.E[0][2] = 0.0f;
        Rot.E[1][0] = Sin;  Rot.E[1][1] = Cos;  Rot.E[1][2] = 0.0f;
        Rot.E[2][0] = 0.0f; Rot.E[2][1] = 0.0f; Rot.E[2][2] = 1.0f;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)Tmp, (FMatrix const&)Accum, (FMatrix const&)Rot);
        CopyMatrix(Accum, Tmp);
    }

    return Accum;
}

Matrix __cdecl GetDirectionMatrix(Transform const& Xform, Vector const& Target)
{
    // Common.dll.txt:06303c10. Reads as "aim the transform at Target": column 2 becomes the
    // normalised vector from Target back to the transform's own position (06303c3d subtracts
    // in that order), and the other two columns are rebuilt around it.
    Matrix Base = Xform.Rotation;   // local_48, nine dwords copied at 06303c2a

    Vector Delta;
    Delta.x = Xform.Translation.x - Target.x;
    Delta.y = Xform.Translation.y - Target.y;
    Delta.z = Xform.Translation.z - Target.z;

    // 06303c46..06303c94: each component FCOMP float 0.0 with `TEST AH,0x44; JP` -- the jump to
    // the rebuild is taken on not-equal AND on unordered, so only three exact zeros return the
    // rotation unchanged.
    if (Delta.x == 0.0f && Delta.y == 0.0f && Delta.z == 0.0f)
        return Base;

    // Values the listing keeps on the x87 stack are double below; every FSTP float is a float.
    // 06303c99..06303cd5: InvLen = 1.0 (double, DAT_06399410) / sqrt((z*z + y*y) + x*x), kept on
    // the stack; Dir = float(Delta * InvLen).
    const double Dx = Delta.x;
    const double Dy = Delta.y;
    const double Dz = Delta.z;
    const double InvLen = 1.0 / sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
    Vector Dir;
    Dir.x = (float)(Dx * InvLen);
    Dir.y = (float)(Dy * InvLen);
    Dir.z = (float)(InvLen * Dz);

    // 06303cd9..06303d16: how parallel the transform's column 1 already is to Dir:
    // (B11*Dir.y + B21*Dir.z) + B01*Dir.x on the stack, through MSVCRT _ftol (64-bit truncation;
    // CDQ keeps the low dword) and the INTEGER abs, then FILD / FCOMP double 0.98
    // (DAT_063a0610) with `TEST AH,0x41; JNZ` -- less or equal takes column 1.
    const double Dot = ((double)Base.E[1][1] * Dir.y + (double)Base.E[2][1] * Dir.z) +
                       (double)Base.E[0][1] * Dir.x;
    const int Truncated = abs((int)(long long)Dot);

    Vector Col0;
    Vector Col1;
    if ((double)Truncated <= 0.98)
    {
        // 06303e50..06303f98: reference axis is column 1 of the incoming rotation
        // (FUN_0628b030 into local_88).
        Vector Ref;
        Ref.x = Base.E[0][1];
        Ref.y = Base.E[1][1];
        Ref.z = Base.E[2][1];

        // Along = float((Rz*Dz + Ry*Dy) + Rx*Dx) (06303e68..06303ea5); Proj = Dir * Along
        // (FUN_06288830, one float product each); Ortho = Ref - Proj (FUN_06288800).
        const float Along = (float)(((double)Ref.z * Dir.z + (double)Ref.y * Dir.y) + (double)Ref.x * Dir.x);
        Vector Ortho;
        Ortho.x = Ref.x - Along * Dir.x;
        Ortho.y = Ref.y - Along * Dir.y;
        Ortho.z = Ref.z - Along * Dir.z;

        // 06303edb..06303f34: Inv = 1.0f / sqrt((x*x + y*y) + z*z) on the stack; Col1 =
        // float(Ortho * Inv).
        const double Ox = Ortho.x;
        const double Oy = Ortho.y;
        const double Oz = Ortho.z;
        const double Inv = 1.0f / sqrt((Ox * Ox + Oy * Oy) + Oz * Oz);
        Col1.x = (float)(Ox * Inv);
        Col1.y = (float)(Oy * Inv);
        Col1.z = (float)(Oz * Inv);

        // 06303f3a..06303f7f: Col0 = Col1 x Dir, each difference of products FSTP float as an
        // argument of FUN_0628b030.
        Col0.z = (float)((double)Col1.x * Dir.y - (double)Col1.y * Dir.x);
        Col0.y = (float)((double)Col1.z * Dir.x - (double)Col1.x * Dir.z);
        Col0.x = (float)((double)Col1.y * Dir.z - (double)Col1.z * Dir.y);
    }
    else
    {
        // 06303d1c..06303e4b: reference axis is column 0 instead (FLD/FSTP float copies).
        Vector Ref;
        Ref.x = Base.E[0][0];
        Ref.y = Base.E[1][0];
        Ref.z = Base.E[2][0];

        // Along as in the other arm (06303d3c..06303d71); Proj (FUN_06288830); Ortho
        // (FUN_06288800).
        const float Along = (float)(((double)Ref.z * Dir.z + (double)Ref.y * Dir.y) + (double)Ref.x * Dir.x);
        Vector Ortho;
        Ortho.x = Ref.x - Along * Dir.x;
        Ortho.y = Ref.y - Along * Dir.y;
        Ortho.z = Ref.z - Along * Dir.z;

        // 06303dc6..06303deb: FUN_06347e60 = sqrt((x*x + y*y) + z*z), FDIVR float 1.0, the
        // reciprocal kept on the stack; Col0 = float(Ortho * Inv).
        const double Ox = Ortho.x;
        const double Oy = Ortho.y;
        const double Oz = Ortho.z;
        const double Inv = 1.0f / sqrt((Ox * Ox + Oy * Oy) + Oz * Oz);
        Col0.x = (float)(Ox * Inv);
        Col0.y = (float)(Oy * Inv);
        Col0.z = (float)(Oz * Inv);

        // 06303df1..06303e3f: Dir x Col0 -- the opposite order from the arm above, which keeps
        // this basis right-handed too (Col0 x Col1 == Dir either way).
        Col1.x = (float)((double)Col0.z * Dir.y - (double)Col0.y * Dir.z);
        Col1.y = (float)((double)Col0.x * Dir.z - (double)Col0.z * Dir.x);
        Col1.z = (float)((double)Col0.y * Dir.x - (double)Col0.x * Dir.y);
    }

    // FUN_06275ee0 writes its three arguments as COLUMNS, in the order (Col0, Col1, Dir).
    Matrix Out;
    Out.E[0][0] = Col0.x; Out.E[1][0] = Col0.y; Out.E[2][0] = Col0.z;
    Out.E[0][1] = Col1.x; Out.E[1][1] = Col1.y; Out.E[2][1] = Col1.z;
    Out.E[0][2] = Dir.x;  Out.E[1][2] = Dir.y;  Out.E[2][2] = Dir.z;
    return Out;
}

Vector __cdecl MatrixColumn2(Matrix const& M)
{
    // Common.dll.c 0x62d3af0. Retail (thiscall, this=Matrix*, stack arg=Vector* Out):
    //   Out->x = this->E[0][2]; Out->y = this->E[1][2]; Out->z = this->E[2][2];
    // Raw column 2, no sign flip -- see the header comment for the contrast with
    // this port's many already-inlined "Forward = -E[*][2]" call sites elsewhere.
    Vector Out;
    Out.x = M.E[0][2];
    Out.y = M.E[1][2];
    Out.z = M.E[2][2];
    return Out;
}

Vector __cdecl MatrixColumn0(Matrix const& M)
{
    // Common.dll.c 0x62e0480 (FUN_062e0480). Retail (thiscall, this=Matrix*,
    // stack arg=Vector* Out, EAX=Out on return):
    //   Out->x = this->E[0][0]; Out->y = this->E[1][0]; Out->z = this->E[2][0];
    // Raw column 0, no sign flip.
    Vector Out;
    Out.x = M.E[0][0];
    Out.y = M.E[1][0];
    Out.z = M.E[2][0];
    return Out;
}

Vector __cdecl MatrixColumn1(Matrix const& M)
{
    // Common.dll.c 0x62e04a0 (FUN_062e04a0). Retail (thiscall, this=Matrix*,
    // stack arg=Vector* Out, EAX=Out on return):
    //   Out->x = this->E[0][1]; Out->y = this->E[1][1]; Out->z = this->E[2][1];
    // Raw column 1, no sign flip -- see MatrixColumn0/MatrixColumn2 above.
    Vector Out;
    Out.x = M.E[0][1];
    Out.y = M.E[1][1];
    Out.z = M.E[2][1];
    return Out;
}

void __cdecl MatrixSetColumn2(Matrix& M, Vector const& Src)
{
    // Common.dll.c 0x62e3710 (FUN_062e3710). Plain thiscall setter, the
    // mirror of MatrixColumn2 above -- see the header comment.
    M.E[0][2] = Src.x;
    M.E[1][2] = Src.y;
    M.E[2][2] = Src.z;
}

Csys __cdecl DALib_Engine_get_csys(long Handle)
{
    // .text:06307900..06307954 (cdecl, hidden Csys* result). 06307903..06307914: DALib::Engine
    // (through the import, untested) slot 40 (+0xa0, GetInstanceTransform) on Handle;
    // 0630791a..06307933: the Transform's +0x24 vector into the local Csys position;
    // 06307937..06307940: its 3x3 (9 dwords, REP MOVSD) into the orientation; 06307942..0630794d:
    // the 0x30-byte local copied to the result. Nothing is tested.
    Transform const* const Xform =
        (Transform const*)DALib::Engine->GetInstanceTransform((void*)Handle);

    Csys Out;
    Out.Pos = Xform->Translation;
    Out.Orient = Xform->Rotation;
    return Out;
}


// .text:06307cc0..06307f1a: the largest |A - B| over the position and the nine matrix
// elements; a difference replaces the running maximum only when strictly greater, so a NaN
// difference is never taken.
float __cdecl Csys_error(Csys const& A, Csys const& B)
{
    float Error = 0.0f;
    float const DeltaX = fabs(A.Pos.x - B.Pos.x);
    float const DeltaY = fabs(A.Pos.y - B.Pos.y);
    float const DeltaZ = fabs(A.Pos.z - B.Pos.z);

    if (Error < DeltaX) Error = DeltaX;
    if (Error < DeltaY) Error = DeltaY;
    if (Error < DeltaZ) Error = DeltaZ;

    unsigned Row = 0;
    for (; Row < 3; ++Row)
    {
        unsigned Col = 0;
        for (; Col < 3; ++Col)
        {
            float const Delta = fabs(A.Orient.E[Row][Col] - B.Orient.E[Row][Col]);
            if (Error < Delta) Error = Delta;
        }
    }
    return Error;
}

// Csys_interpolate -- ordinal 1332, .text:06307b40-06307cbe (cdecl, hidden result pointer at
// entry+4, A at +8, B at +0x38, T at +0x68).
//
// Position: the x87 sequence at .text:06307b46-06307bd5 keeps some intermediates on the FPU
// stack and spills others through float locals, so the rounding points are reproduced here:
//   dx, dy stay on the stack (double); dz is spilled FSTP dword local_8c (float);
//   dx*T is spilled FSTP dword local_84 (float); dy*T and dz_f*T stay on the stack;
//   each component is rounded to float once, at its FSTP into the result Pos.
// Orientation: the file-static 3DMathEngine singleton (DAT_063fceb8, lazy-acquired through
// FUN_06391cf0 == Acquire3DMathEngine before every call):
//   +0x50 MatrixToQuaternion(qB, B.Orient)        .text:06307bf5
//   +0x50 MatrixToQuaternion(qA, A.Orient)        .text:06307c1b
//   +0x58 QuatNlerp(qOut, qA, qB, T)              .text:06307c4b
//   qOut copied to a temporary                     .text:06307c4e-06307c71
//   +0x54 QuaternionToMatrix(M, qTmp)             .text:06307c90
// Return codes are ignored by retail. Result = {Pos, M} copied through the hidden pointer.
Csys __cdecl Csys_interpolate(Csys A, Csys B, float T)
{
    double const Dx = (double)(B.Pos.x) - (double)(A.Pos.x);
    double const Dy = (double)(B.Pos.y) - (double)(A.Pos.y);
    float const DzF = (float)((double)(B.Pos.z) - (double)(A.Pos.z));
    float const DxTF = (float)(Dx * (double)(T));
    double const DyT = Dy * (double)(T);
    double const DzT = (double)(DzF) * (double)(T);

    Csys Result;
    Result.Pos.x = (float)((double)(DxTF) + (double)(A.Pos.x));
    Result.Pos.y = (float)(DyT + (double)(A.Pos.y));
    Result.Pos.z = (float)(DzT + (double)(A.Pos.z));

    FQuaternion QuatB;
    FQuaternion QuatA;
    FQuaternion QuatOut;
    Acquire3DMathEngine()->MatrixToQuaternion(QuatB, (FMatrix const&)B.Orient);
    Acquire3DMathEngine()->MatrixToQuaternion(QuatA, (FMatrix const&)A.Orient);
    Acquire3DMathEngine()->QuatNlerp(QuatOut, QuatA, QuatB, T);
    FQuaternion const QuatTmp = QuatOut;
    Acquire3DMathEngine()->QuaternionToMatrix((FMatrix&)Result.Orient, QuatTmp);
    return Result;
}

// .text:06307960..063079ab: the Csys repacked as a Transform on the stack (the 3x3 at +0xc,
// REP MOVSD 9, then Pos into +0x24) and handed to IEngine slot 39 (+0x9c, SetInstanceMatrix)
// through the DALib::Engine import, which is not tested.
void __cdecl DALib_Engine_set_csys(long Handle, Csys const& InCsys)
{
    Transform Xform = InCsys.get_transform();
    DALib::Engine->SetInstanceMatrix((void*)Handle, &Xform);
}

// DELIBERATE DEVIATION FROM RETAIL, 2026-09-17 -- read this before "fixing" it back.
//
// Retail's own body (.text:06391cf0-06391d28) does NOT cache: it builds the AGGDESC on the
// stack, calls DACOM_Acquire, dispatches CreateInstance through the manager's vtable+0x0c and
// returns, every single time. The caching in retail lives at the CALL SITES -- twenty-five
// per-translation-unit file statics in `.data`, each using the MSVC lazy-singleton idiom:
//     MOV EAX,[DAT_xxxxxxxx] / TEST EAX,EAX / JNZ skip
//     CALL Acquire3DMathEngine / MOV [DAT_xxxxxxxx],EAX
// (those 25 globals were identified and closed on 2026-09-17 by the globals lane).
//
// The port has 114 call sites and none of them cache, so before this change every one of them
// -- including per-tick AI steering and equipment paths -- ran a DACOM acquire plus a COM
// CreateInstance and then dropped the returned reference without Release.
//
// Rather than retrofit 114 call-site statics across eleven files, the cache is collapsed into
// one here. The observable difference from retail: retail may hold up to twenty-five
// references to the engine, this holds one. That is safe in this port precisely because no
// call site ever released its reference anyway, so the port was never balancing them.
//
// TODO: if the per-TU statics ever matter (e.g. if CreateInstance stops returning a shared
// instance), move the cache back out to the call sites and restore this body to retail's.
I3DMathEngine* __cdecl Acquire3DMathEngine()
{
    static I3DMathEngine* CachedEngine = nullptr;
    if (CachedEngine != nullptr)
        return CachedEngine;

    // A 12-byte descriptor: AGGDESC's {size, interface name} plus a null third dword.
    struct FAGGDESC12
    {
        AGGDESC Base;
        void* Reserved08;
    };

    FAGGDESC12 Desc;
    Desc.Base.size = 12;
    Desc.Base.interface_name = "3DMathEngine";
    Desc.Reserved08 = nullptr;

    // .text:06391d0b..06391d1e: DACOM_Acquire, then its slot +0x0c CreateInstance with no test
    // of the manager.
    void* Engine = nullptr;
    ICOManager* const Manager = DACOM_Acquire();
    Manager->CreateInstance(&Desc.Base, &Engine);

    CachedEngine = (I3DMathEngine*)Engine;
    return CachedEngine;
}
