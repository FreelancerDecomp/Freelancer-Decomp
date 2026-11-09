// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI steering frames: BuildOrientationBasis, BuildPerpendicularFrame,
// BuildRandomSpreadFrame, TurnHelper::get_angular_throttle, TransformVectorByMatrix,
// LimitAxisThrottleForTurn, SteeringFrameFromRelativeDir, ComputeBankedSteeringFrame
// (retail 0x062e1f40-0x062e37e0, one contiguous block), plus the CmnAiSteer helpers FaceIdea::Update and
// DodgeIdea::Update share (common_aisteer_internal.h).
// See src/include/common_aisteer.h for the subsystem writeup.
//
// CMN-SPLIT stage 1 (2026-10-14): what was left of the old common_aisteer.cpp after
// PathfindManager (common_pathfind.cpp) and the gun-convergence block
// (common_gunconvergence.cpp) went out. The rest went to common_obstacleavoidance.cpp,
// common_crspline.cpp, common_aiintercept.cpp, common_aifirecontrol.cpp,
// common_airaycast.cpp, common_aitarget.cpp, common_faceidea.cpp, common_dodgeidea.cpp,
// common_splinemoveidea.cpp (SplineMoveIdea::Update) and common_idea.cpp
// (GenericWeightedPick).

#include "common_aisteer.h"
#include "common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_systemmanager.h" // SystemManager::ExternalIface

#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// BuildOrientationBasis -- see common_aisteer.h for the full writeup (retail
// address 0x062e1f40; TransformedBoxesIntersect 0x062e0a60 is in common_airaycast.cpp).
// ----------------------------------------------------------------------------

namespace
{
    // Defined further down in this file (anonymous namespace next to
    // ComputeBankedSteeringFrame): FUN_062c47a0's x87 dot and the
    // FUN_06347e60 / FST float / 0.01f / FUN_062dc410-or-zero normalize idiom.
    double BankDot(Vector const& A, Vector const& B);
    void BankNormalizeOrZero(Vector& V);

    // The dot-and-clamp the Reference-column fallback runs twice (062e2264-062e2298,
    // 062e22aa-062e22de): the FUN_062c47a0 dot is FST-rounded to a float local; the
    // unrounded value is tested `> 1.0f` (TEST AH,0x41 / JNZ, greater -> 1.0f), else the
    // ROUNDED local is tested `< -1.0f` (TEST AH,0x5 / JP, less -> -1.0f). A value that
    // exceeds 1.0 unrounded rounds to >= 1.0, so this is the clamp of the float image;
    // NaN survives both tests.
    float ClampedFloatDot(Vector const& A, Vector const& B)
    {
        float const D = (float)BankDot(A, B);
        if (D > 1.0f)
            return 1.0f;
        if (D < -1.0f)
            return -1.0f;
        return D;
    }
}

void BuildOrientationBasis(Matrix& Out, Matrix const& Reference, Vector const& Forward, Vector const* UpHint)
{
    // Retail builds the basis in a 9-dword local (local_48, zero-filled at
    // 062e1f6d-062e1f71) and copies it whole into Out at 062e2570-062e2582 on every
    // path, returning Out in EAX. Result plays local_48.
    Matrix Result{};

    // 062e1f40-062e1f51: Z_raw = -Forward (FUN_062c5a70, FCHS per component).
    Vector ZRaw;
    VecNegate3(&Forward, &ZRaw);

    // 062e1f56-062e1f8a: (y*y + z*z) + x*x on the x87 stack against the DOUBLE
    // 0.0001 at DAT_0639c2b8; TEST AH,0x5 / JNP leaves on "less" only (NaN goes on).
    // The degenerate exit copies the all-zero local_48.
    double const ZRawSq = ((double)ZRaw.y * ZRaw.y + (double)ZRaw.z * ZRaw.z) + (double)ZRaw.x * ZRaw.x;
    if (ZRawSq < 0.0001)
    {
        Out = Result;
        return;
    }

    // 062e1f90-062e1ff4: Z = Z_raw normalised by its FST-rounded length
    // (FUN_06347e60, FCOMP 0.01f, FUN_062dc410 / FUN_0636cf40(0,0,0)).
    Vector Z = ZRaw;
    BankNormalizeOrZero(Z);

    // 062e1ff8-062e2024: dot(Z, column 2 of Reference) is computed and popped
    // (`FSTP ST0`); FUN_0628b030 and FUN_062c47a0 are pure, so nothing is observable.

    // 062e2026-062e2032: UpHint == nullptr -> the Reference-column fallback.
    if (UpHint != nullptr)
    {
        // 062e2038-062e2097: D = FUN_062c47a0(UpHint, Z) stays in ST0, is clamped in
        // place (FCOM 1.0f: greater -> 1.0f; else FCOM -1.0f: less -> -1.0f), then
        // FABS against the DOUBLE 0.999 at DAT_0639fd40; TEST AH,0x5 / JP goes to the
        // fallback on not-less (and on NaN).
        double D = BankDot(*UpHint, Z);
        if (D > 1.0f)
            D = 1.0f;
        else if (D < -1.0f)
            D = -1.0f;
        if (std::fabs(D) < 0.999)
        {
            // 062e209d-062e20fd: Right = cross(UpHint, Z) (one rounding per
            // component); 062e2102-062e214a: normalised by its rounded length.
            Vector Right;
            VecCross3(&Right, UpHint, &Z);
            BankNormalizeOrZero(Right);
            // 062e214f-062e21af: Up = cross(Z, Right), not renormalised.
            Vector Up;
            VecCross3(&Up, &Z, &Right);

            // 062e21b4-062e21f8: columns [Right | Up | Z] into local_48.
            Result.E[0][0] = Right.x; Result.E[1][0] = Right.y; Result.E[2][0] = Right.z;
            Result.E[0][1] = Up.x;    Result.E[1][1] = Up.y;    Result.E[2][1] = Up.z;
            Result.E[0][2] = Z.x;     Result.E[1][2] = Z.y;     Result.E[2][2] = Z.z;
            Out = Result;
            return;
        }
    }

    // LAB_062e2201: C0 / C1 = columns 0 / 1 of Reference (FUN_0628b030 from
    // +0x00/+0x0c/+0x18 and +0x04/+0x10/+0x1c); D0 / D1 = their clamped float dots
    // with Z, D0 first.
    Vector Right = MatrixColumn0(Reference); // local_64
    Vector Up = MatrixColumn1(Reference);    // local_88
    float const D0 = ClampedFloatDot(Right, Z);
    float const D1 = ClampedFloatDot(Up, Z);

    // 062e22e6-062e22f7: |D1| against the DOUBLE at DAT_063a04e0 (== (double)0.9999f);
    // TEST AH,0x5 / JP takes the C0 arm on not-less (and on NaN).
    if (std::fabs((double)D1) < (double)0.9999f)
    {
        // 062e22fd-062e2373: Up = C1 - Z*D1 (FUN_06288830, FUN_062e2590), normalised
        // by its rounded length (the inline idiom, not FUN_062e1680).
        Vector ZScaled;
        ZScaled.x = D1 * Z.x;
        ZScaled.y = D1 * Z.y;
        ZScaled.z = D1 * Z.z;
        VecSubInPlace3(&Up, &ZScaled);
        BankNormalizeOrZero(Up);
        // 062e237b-062e23d8: Right = FUN_062e1680(cross(Up, Z)).
        Vector Cross;
        VecCross3(&Cross, &Up, &Z);
        VecNormalize3(&Cross, &Right);
        // 062e23d5-062e23fa: dot(Right, column 0 of Reference) (FUN_062e0480) < 0.0f
        // (TEST AH,0x5 / JP keeps on not-less and NaN) -> negate both, then
        // Up = FUN_062e1680(cross(Z, Right)).
        if (BankDot(Right, MatrixColumn0(Reference)) < 0.0)
        {
            VecScaleInPlace3(&Right, -1.0f);
            VecScaleInPlace3(&Up, -1.0f);
            VecCross3(&Cross, &Z, &Right);
            VecNormalize3(&Cross, &Up);
        }
    }
    else
    {
        // LAB_062e246f: Right = FUN_062e1680(C0 - Z*D0).
        Vector ZScaled;
        ZScaled.x = D0 * Z.x;
        ZScaled.y = D0 * Z.y;
        ZScaled.z = D0 * Z.z;
        VecSubInPlace3(&Right, &ZScaled);
        Vector Normalized;
        VecNormalize3(&Right, &Normalized);
        Right = Normalized;
        // 062e24b3-062e24f0: Up = FUN_062e1680(cross(Z, Right)).
        Vector Cross;
        VecCross3(&Cross, &Z, &Right);
        VecNormalize3(&Cross, &Up);
        // 062e2505-062e2543: dot(Up, column 1 of Reference) (FUN_062e04a0) < 0.0f ->
        // negate both, no recompute.
        if (BankDot(Up, MatrixColumn1(Reference)) < 0.0)
        {
            VecScaleInPlace3(&Right, -1.0f);
            VecScaleInPlace3(&Up, -1.0f);
        }
    }

    // LAB_062e2548: FUN_06275ee0 packs columns [Right | Up | Z]; copied into
    // local_48, then into Out.
    Result.E[0][0] = Right.x; Result.E[1][0] = Right.y; Result.E[2][0] = Right.z;
    Result.E[0][1] = Up.x;    Result.E[1][1] = Up.y;    Result.E[2][1] = Up.z;
    Result.E[0][2] = Z.x;     Result.E[1][2] = Z.y;     Result.E[2][2] = Z.z;
    Out = Result;
}

// Common.dll.c 0x062e3460 (FUN_062e3460), item 1741, Common.dll.txt:237495-237739.
// x87 terms are kept in double: every sum, square root, clamp and compare
// happens on the FPU stack; only the FSTP float stores round.
bool BuildPerpendicularFrame(Vector const& Direction, Matrix* OutFrame, float DotThreshold, Vector const* Reference)
{
    // 062e3460-062e3491: (x*x + y*y) + z*z against the double 1e-5; TEST AH,0x5 +
    // JNP leaves on "less" only.
    double const LenSqDir = ((double)(Direction.x) * Direction.x +
                             (double)(Direction.y) * Direction.y) +
                            (double)(Direction.z) * Direction.z;
    if (LenSqDir < 1e-05)
        return false;

    // 062e3497-062e351e: NegDir = -Direction (FUN_0628b030), copied to local_30, then
    // normalized by the FST-float length when the unrounded length is above 0.01f
    // (TEST AH,0x41 + JNZ: not-greater takes the zero arm, FUN_0636cf40(0,0,0)).
    Vector NegDir{ -Direction.x, -Direction.y, -Direction.z };
    double const NegDirLen = std::sqrt(((double)(NegDir.x) * NegDir.x +
                                        (double)(NegDir.y) * NegDir.y) +
                                       (double)(NegDir.z) * NegDir.z);
    if (NegDirLen > (double)(0.01f))
        VecNormalizeInPlaceByLength3(&NegDir, (float)(NegDirLen));
    else
        NegDir = Vector{ 0.0f, 0.0f, 0.0f };

    // 062e351e-062e3570: NegDir copied to local_c (the column-2 source), Reference
    // defaults to (0, 1, 0), a null OutFrame rejects.
    Vector const Column2 = NegDir;
    Vector Ref{ 0.0f, 1.0f, 0.0f };
    if (Reference != nullptr)
        Ref = *Reference;
    if (OutFrame == nullptr)
        return false;

    // 062e3576-062e35cb: Dot = (N.z*R.z + N.y*R.y) + R.x*N.x, clamped to [-1, 1]
    // (above 1.0f -> 1.0f; TEST AH,0x5 + JP keeps >= -1.0f, below -> -1.0f), FABS,
    // and `FLD DotThreshold; FCOMPP` + TEST AH,0x41 + JNZ rejects unless
    // DotThreshold > |Dot|.
    double Dot = ((double)(NegDir.z) * Ref.z + (double)(NegDir.y) * Ref.y) +
                 (double)(Ref.x) * NegDir.x;
    if (Dot > 1.0)
        Dot = 1.0;
    else if (Dot < -1.0)
        Dot = -1.0;
    if (!((double)(DotThreshold) > std::fabs(Dot)))
        return false;

    // 062e35d1-062e3679: Right = cross(Reference, NegDir), each term FSTP float,
    // length (z*z + y*y) + x*x, normalized above 0.01f, zeroed otherwise.
    Vector Right;
    Right.x = (float)((double)(NegDir.z) * Ref.y - (double)(NegDir.y) * Ref.z);
    Right.y = (float)((double)(Ref.z) * NegDir.x - (double)(NegDir.z) * Ref.x);
    Right.z = (float)((double)(NegDir.y) * Ref.x - (double)(Ref.y) * NegDir.x);
    double const RightLen = std::sqrt(((double)(Right.z) * Right.z +
                                       (double)(Right.y) * Right.y) +
                                      (double)(Right.x) * Right.x);
    if (RightLen > (double)(0.01f))
        VecNormalizeInPlaceByLength3(&Right, (float)(RightLen));
    else
        Right = Vector{ 0.0f, 0.0f, 0.0f };

    // 062e3679-062e36c1: Up = cross(NegDir, Right) built by FUN_0628b030 into local_18.
    Vector Up;
    Up.x = (float)((double)(Right.z) * NegDir.y - (double)(Right.y) * NegDir.z);
    Up.y = (float)((double)(NegDir.z) * Right.x - (double)(Right.z) * NegDir.x);
    Up.z = (float)((double)(Right.y) * NegDir.x - (double)(NegDir.y) * Right.x);

    // 062e36c6-062e36fb: column 0 = Right, column 1 = Up, column 2 = NegDir
    // (FUN_062e3710), return true.
    OutFrame->E[0][0] = Right.x;
    OutFrame->E[2][0] = Right.z;
    OutFrame->E[0][1] = Up.x;
    OutFrame->E[1][0] = Right.y;
    OutFrame->E[2][1] = Up.z;
    OutFrame->E[1][1] = Up.y;
    MatrixSetColumn2(*OutFrame, Column2);
    return true;
}

// Common.dll.c 0x062e2630 (FUN_062e2630), item 1738. PORTED FOR REAL 2026-09-17
// (claude:common-unaff-rederive) after a complete 205/205-instruction hand trace
// of Common.dll.txt:236333-236544 with byte-exact stack-depth tracking. See
// common_aisteer.h for the writeup, including the two corrections this trace
// makes to the previous pass's partial analysis.
//
// Frame map, relative to the entry ESP `E` (SUB 0xa4 + 3 pushes => a constant
// working depth of E-0xb0):
//   E-0x10 Quat0        MatrixToQuaternion(*Reference)
//   E-0x34 Working      the 9-float scratch zeroed by the prologue's STOSD.REP
//   E-0x58 Frame        QuaternionToMatrix output, then BuildOrientationBasis's Out
//   E-0x68 Quat1        the random-axis spread quaternion {cos, x, y, z}
//   E-0x78 Combined     MulQuat(Quat0, Quat1), copied to E-0x94 for the next call
//   E-0x84 RandomAxis   RandomVector(1.0f)'s return buffer
//   E-0xa0 UpHint / E-0x94 Forward (both branches reuse these two slots)
//
// Parameters (all four confirmed at all five call sites, each of which cleans
// exactly 0x10 bytes -- e.g. Common.dll.txt:222328-222336): Out is retail's
// hidden struct-return pointer at E+4, returned again in EAX; Reference is at
// E+8 (held in the callee-saved EBX for the whole body -- the old
// `unaff_EBX`); SpreadAngle is the float at E+0xC (read as `FLD [ESP+0xc4]` at
// a verified depth of 0xb8 -- the old `unaff_retaddr`); BankAngle is a FOURTH
// float parameter at E+0x10 (`FLD [ESP+0xc0]` at depth 0xb0, listing lines
// 236426 and 236472) that no previous pass had identified at all.
Matrix* BuildRandomSpreadFrame(Matrix& Out, Matrix const& Reference,
                                          float SpreadAngle, float BankAngle)
{
    // Prologue's `XOR EAX,EAX; MOV ECX,9; LEA EDI,[ESP+0x7c]; STOSD.REP`
    // (236337-236340). Dead in practice -- Working is unconditionally
    // overwritten by the Frame copy below -- but reproduced for fidelity.
    Matrix Working;
    Matrix Frame;
    Matrix Result;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Col = 0; Col < 3; ++Col)
        {
            Working.E[Row][Col] = 0.0f;
            Frame.E[Row][Col] = 0.0f;
            Result.E[Row][Col] = 0.0f;
        }
    }

    // 062e2646-062e266c: Quat0 (S+0xa0) = MatrixToQuaternion (+0x50) of Reference, the engine
    // created lazily with no null test.
    FQuaternion Quat0;
    Acquire3DMathEngine()->MatrixToQuaternion(Quat0, reinterpret_cast<FMatrix const&>(Reference));

    // 062e266f-062e26ec: the spread quaternion Quat1 (S+0x48). RandomAxis = RandomVector(1.0f)
    // (S+0x2c); Half = SpreadAngle * 0.5f on the FPU; W = FCOS(Half) FSTP float; K =
    // FSIN(Half) / sqrt((y*y + z*z) + x*x) FSTP float; XYZ = RandomAxis * K through
    // FUN_06288830 (one float product each).
    Vector const RandomAxis = RandomVector(1.0f);
    double const HalfAngle = (double)(SpreadAngle) * 0.5f;
    FQuaternion Quat1;
    Quat1.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
    double const AxisLen = std::sqrt(((double)(RandomAxis.y) * RandomAxis.y +
                                      (double)(RandomAxis.z) * RandomAxis.z) +
                                     (double)(RandomAxis.x) * RandomAxis.x);
    float const AxisScale = (float)(std::sin(static_cast<double>(HalfAngle)) / AxisLen);
    Quat1.X = RandomAxis.x * AxisScale;
    Quat1.Y = RandomAxis.y * AxisScale;
    Quat1.Z = RandomAxis.z * AxisScale;

    // 062e26f0-062e2737: Combined (S+0x38) = MulQuat (+0x18)(Quat0, Quat1), copied to S+0x1c.
    FQuaternion Combined;
    Acquire3DMathEngine()->MulQuat(Combined, Quat0, Quat1);

    // 062e273b-062e2754: Frame (S+0x58) = QuaternionToMatrix (+0x54) of the copy.
    Acquire3DMathEngine()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Frame), Combined);

    // 236428-236431: unconditional 9-dword copy Frame -> Working, executed
    // before the branch (interleaved with the FCOMP for pipelining). Working is
    // what BuildOrientationBasis receives as its Reference, precisely because
    // retail aims its Out at Frame's own storage.
    Working = Frame;

    // 236426/236427/236432-236434: `FLD BankAngle; FCOMP 0.0f; TEST AH,0x41;
    // JNZ` -- the taken branch is BankAngle <= 0.0f. This is the real gate; the
    // previous pass guessed it was "a value read from the MulQuat/
    // QuaternionToMatrix output", which the raw listing disproves.
    bool const bUseReferenceUp = BankAngle > 0.0f;

    // Both branches derive Forward the same way: the NEGATED column 2 of Frame
    // (listing 236443-236451 reads E-0x50/E-0x44/E-0x38 == Frame[2]/[5]/[8] as
    // a flat 9-float array, i.e. E[0][2]/E[1][2]/E[2][2]; the mirror branch at
    // 236505-236525 reads the identical three slots). This is the "scratch slot
    // that feeds Forward's Y component" the previous two passes could not pin
    // down: it is simply Frame.E[1][2], negated.
    Vector const Forward{ -Frame.E[0][2], -Frame.E[1][2], -Frame.E[2][2] };

    Vector UpHint;
    if (bUseReferenceUp)
    {
        // 236435-236442: Reference's own column 1, read straight off EBX as
        // [EBX+4]/[EBX+0x10]/[EBX+0x1c].
        UpHint = Vector{ Reference.E[0][1], Reference.E[1][1], Reference.E[2][1] };
    }
    else
    {
        // 236524/236526/236527: the literal world up (0, 1, 0).
        UpHint = Vector{ 0.0f, 1.0f, 0.0f };
    }

    // 236453-236466 / 236506-236533: BuildOrientationBasis(Frame, Working,
    // Forward, &UpHint), whose returned pointer retail copies back over
    // Working. Note BOTH branches pass a real UpHint pointer -- retail never
    // passes null here, contrary to the previous pass's "or null" note.
    BuildOrientationBasis(Result, Working, Forward, &UpHint);
    Working = Result;

    if (bUseReferenceUp)
    {
        // 236467-236502: a random-signed roll of BankAngle applied to each row
        // of Working in its local X/Y plane (column 2 untouched):
        //   NewCol0 = cos*Col0 + sin*Col1
        //   NewCol1 = cos*Col1 - sin*Col0
        //
        // DEVIATION NOTE (retail dead code, reproduced as-is): the sign draw at
        // 236467-236476 is `if (rand() * (1/32768) < 0.0f) BankAngle =
        // -BankAngle`. rand() is never negative and DAT_06399408 is 0.0f (bytes
        // verified at .rdata:06399408), so retail's FCHS at 236476 is
        // unreachable and the roll is always positive. Kept literal rather than
        // "fixed" to 0.5f, since guessing retail's intent would change
        // observable behaviour.
        // 062e27ee-062e2816: FILD rand() * (1/32768) (DAT_063997d0) FCOMP 0.0f, TEST AH,0x5 +
        // JP: only "less" negates (unreachable, kept literal).
        double RollAngle = BankAngle;
        double const RandFrac = (double)(std::rand()) * 3.0517578e-05f;
        if (RandFrac < 0.0)
            RollAngle = -RollAngle;

        // 062e2818-062e2857: cos FSTP float (S+0xc), sin stays on the FPU; per row
        // new0 = sin*old1 + old0*cos_f, new1 = cos_f*old1 - old0*sin, each FSTP float.
        float const CosRoll = static_cast<float>(std::cos(static_cast<double>(RollAngle)));
        double const SinRoll = std::sin(static_cast<double>(RollAngle));
        for (int Row = 0; Row < 3; ++Row)
        {
            double const Col0 = Working.E[Row][0];
            double const Col1 = Working.E[Row][1];
            Working.E[Row][0] = (float)(SinRoll * Col1 + Col0 * CosRoll);
            Working.E[Row][1] = (float)(CosRoll * Col1 - Col0 * SinRoll);
        }
    }

    // 236535-236539: the 9-dword copy out to the caller's buffer, plus retail's
    // EAX == Out return (every call site consumes it, e.g. 236462/219819).
    Out = Working;
    return &Out;
}

// Common.dll.c 0x062e2c00 (FUN_062e2c00), item 1739. See common_aisteer.h for
// the SrcArg-type resolution that unblocked this item.
// 062e2c00-062e2c60, cdecl (Out, Matrix*, x, y, z): the by-value vector is copied to
// local_18, the lazily created 3DMathEngine (DAT_063fca48 / FUN_06391cf0) is called
// unconditionally through +0x2c into local_c, and local_c is copied to Out (EAX = Out).
Vector TransformVectorByMatrix(Matrix const& M, Vector const& V)
{
    Vector const Src = V;
    Vector Result;
    Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(Result),
                                           reinterpret_cast<const FMatrix&>(M),
                                           reinterpret_cast<const FVector&>(Src)); // +0x2C
    return Result;
}

namespace
{
    // The projection LimitAxisThrottleForTurn open-codes five times (e.g. 062e2cfb-062e2d6a):
    // FUN_062e1680 of the source and of the column, FUN_062e1630 (ClampedDot3) of the two
    // FSTP float, the source's FUN_06347e60 length ((x*x + y*y) + z*z) times that float FSTP
    // float, and the normalised column scaled by it through FUN_06288830 (one float product per
    // component). This is not FUN_062e1730 (VecCosineProject3), which rounds differently.
    Vector LimitProjectOntoColumn(Vector const& Source, Vector const& Column)
    {
        Vector SourceDir;
        VecNormalize3(&Source, &SourceDir);
        Vector ColumnDir;
        VecNormalize3(&Column, &ColumnDir);
        float const Cosine = (float)(ClampedDot3(SourceDir, ColumnDir));
        double const Length = std::sqrt(((double)(Source.x) * Source.x +
                                         (double)(Source.y) * Source.y) +
                                        (double)(Source.z) * Source.z);
        float const Scale = (float)(Length * Cosine);
        Vector Projected;
        Projected.x = ColumnDir.x * Scale;
        Projected.y = ColumnDir.y * Scale;
        Projected.z = ColumnDir.z * Scale;
        return Projected;
    }

    // LimitAxisThrottleForTurn's two angle blocks (062e2ebe-062e2f22, 062e2f47-062e2fae):
    // FUN_062c47a0(A, B) = (A.z*B.z + A.y*B.y) + A.x*B.x FST float; above 1.0f (unrounded,
    // TEST AH,0x41 + JNZ) the slot becomes 1.0f, else the stored float below -1.0f (TEST AH,0x5
    // + JP) becomes -1.0f; _CIacos of the slot FSTP float.
    float LimitClampedAcos(Vector const& A, Vector const& B)
    {
        double const Dot = ((double)(A.z) * B.z + (double)(A.y) * B.y) +
                           (double)(A.x) * B.x;
        float Clamped = (float)(Dot);
        if (Dot > 1.0)
            Clamped = 1.0f;
        else if (Clamped < -1.0f)
            Clamped = -1.0f;
        return (float)(std::acos((double)(Clamped)));
    }
}

// ---------------------------------------------------------------------------
// FUN_062e2c70, decomp-tracker item 1740. Raw disassembly
// Common.dll.txt:236889-237493 (571 instructions). Ghidra emitted NO pseudocode
// at all for this one, so everything below is a hand trace of the raw bytes
// with full ESP-delta accounting. Six-digit numbers in the section comments are
// Common.dll.txt LINE numbers; eight-hex-digit ones starting 062e are .text
// addresses.
//
// SIGNATURE, proved from the call site at 062d72ee-062d734d (FaceIdea::Update)
// and re-confirmed at its sibling 062d731b-062d734d: plain __cdecl, nine
// caller-cleaned dwords (`ADD ESP,0x24` at 062d736e), EAX = param_1 on exit
// (the caller immediately does `MOV ECX,[EAX]`), and param_3..5 are one Vector
// passed BY VALUE -- the caller reserves the 12-byte hole with `SUB ESP,0xc`
// and fills it with three MOVs (062d7302-062d730d) rather than three pushes.
//
// param_2 IS AN IObjInspect, not the SystemManager::ExternalIface that this
// item's older notes assumed, and its six vtable slots are all already named in
// common_iface.h. That retires the "5 newly-discovered, still-unnamed
// ExternalIface vtable slots (0x8/0x50/0xe4/0xec/0xf8)" claim in
// common_aisteer.h's ADDENDUM 6 -- every one of them is an ordinary
// IObjInspect getter, and the way they fit together (angular velocity ->
// transpose into the local frame -> XY angular speed -> time-to-accelerate ->
// distance-travelled) is self-consistent only under that reading:
//   +0x008 get_angular_velocity()                 (Vector by hidden pointer)
//   +0x00C get_transform()                        (zero-arg, 9 dwords REP MOVSD'd
//                                                  out of it -- the Rotation part)
//   +0x050 get_axis_throttle(Vector&)
//   +0x0E4 get_time_to_accelerate_angularly(f,f,f,f)
//   +0x0EC get_angular_distance_travelled(f,f,f,f)
//   +0x0F8 get_projected_axis_throttle_XY(f)
// The caller passes `this->0x4` (the Idea's own bound Object), not Sys's iface.
//
// WHAT IT COMPUTES. Command is decomposed into its three scalar projections on
// the object's own orientation columns (P0/P1/P2). The column-0/column-1 pair
// -- the pitch/yaw plane -- is scaled by a ratio derived from how far the
// object still has to turn (the angle between its column 2 and the target
// frame's column 2) versus ReferenceAngle, then clamped down twice: once by the
// throttle the object can actually deliver in that plane, once by
// get_projected_axis_throttle_XY. The column-2 part is scaled independently by
// a bank/roll factor derived from the column-1 angle. Out = plane*Ratio + P2*ZFactor.
//
// .rdata constants, each decoded from its raw bytes in the listing:
//   DAT_0639e6b0  7b 14 ae 47 e1 7a 84 3f  = 0.01   (double)
//   DAT_0639e6b8  0a d7 23 3c              = 0.01f
//   DAT_0639c9f0  fc a9 f1 d2 4d 62 50 3f  = 0.001  (double)
//   DAT_0639e6c8  6f 12 83 3a              = 0.001f
//   DAT_063a0610  5c 8f c2 f5 28 5c ef 3f  = 0.98   (double)
//   DAT_063a0620  cd cc cc 3e              = 0.4f
//   DAT_0639f5d0  db 0f c9 3e              = 0.3926991f  (pi/8)
//   DAT_063a076c  83 f9 22 40              = 2.5464792f  (8/pi)
//   DAT_0639c13c  00 00 00 3f              = 0.5f
//   DAT_0639e2d8  00 00 00 60 fb 21 f9 3f  = 1.5707964    (pi/2, double)
//   DAT_0639a1d0 = 1.0f, DAT_06399404 = -1.0f, DAT_06399408 = 0.0f (already
//   established elsewhere in this port).
Vector* LimitAxisThrottleForTurn(Vector& Out, IObjInspect const* Obj, Vector Command,
                                            Matrix const* TargetFrame, float TurnAngle,
                                            float ReferenceAngle, bool bBankBlend)
{
    // 236900-236915: the whole object-less fast path -- Command is copied
    // straight through to *param_1 and EAX = param_1 is returned.
    if (Obj == nullptr)
    {
        Out = Command;
        return &Out;
    }

    // 236917-236928: `MOV EAX,[EBX]; MOV ECX,EBX; CALL [EAX+0xc]` with NO
    // argument push, then `MOV ECX,9; LEA EDI,[ESP+0x8c]; REP MOVSD` -- exactly
    // nine dwords, i.e. the 3x3, not the 12-dword Transform.
    Matrix const Frame = Obj->get_transform().Rotation;

    // Re-traced 2026-10-08 (cmn-ai9), 062e2c70-062e345f. S = ESP after the four pushes until
    // the POPs at 062e2f78, T = S + 0xc afterwards.
    Vector const Col0 = MatrixColumn0(Frame);
    Vector const Col1 = MatrixColumn1(Frame);
    Vector const Col2 = MatrixColumn2(Frame);

    // 062e2cda-062e2eb2: P0 (S+0x80), P1 (S+0x50), P2 (S+0x5c) = Command projected onto
    // columns 0, 1 and 2 by the open-coded LimitProjectOntoColumn sequence.
    Vector P0 = LimitProjectOntoColumn(Command, Col0);
    Vector P1 = LimitProjectOntoColumn(Command, Col1);
    Vector P2 = LimitProjectOntoColumn(Command, Col2);

    // 062e2eb7-062e2fae: AngleZ (S+0x48) / AngleY (S+0x4c) = _CIacos of FUN_062c47a0(target
    // column k, own column k) (k = 2, then 1), FST float; the unrounded dot above 1.0f stores
    // 1.0f, else the stored float below -1.0f stores -1.0f; the acos FSTP float.
    float const AngleZ = LimitClampedAcos(MatrixColumn2(*TargetFrame), Col2);
    float const AngleY = LimitClampedAcos(MatrixColumn1(*TargetFrame), Col1);

    // 062e2fb2-062e2fec: get_angular_velocity (+0x8) into T+0xa4, TransposeTransform (+0x38)
    // by the rotation copy into T+0xc, the engine created lazily with no null test.
    Vector const AngularVelocity = Obj->get_angular_velocity();
    Vector LocalAngularVelocity;
    Acquire3DMathEngine()->TransposeTransform(reinterpret_cast<FVector&>(LocalAngularVelocity),
                                              reinterpret_cast<FMatrix const&>(Frame),
                                              reinterpret_cast<FVector const&>(AngularVelocity));

    // 062e2fef-062e301c: AngXY (T+4) = sqrt(x*x + y*y) FST float; the unrounded value below
    // 0.01f (TEST AH,0x5 + JP: only "less") stores 0.
    double const AngularSpeedXYD = std::sqrt((double)(LocalAngularVelocity.x) * LocalAngularVelocity.x +
                                             (double)(LocalAngularVelocity.y) * LocalAngularVelocity.y);
    float AngularSpeedXY = (float)(AngularSpeedXYD);
    if (AngularSpeedXYD < (double)(0.01f))
        AngularSpeedXY = 0.0f;

    // 062e301c-062e3076: AngleForCall (T+0x30) = TurnAngle, 0 only when less than 0.0f; the
    // speed (T+8) = AngXY, 0 only when less than double 0.001; get_time_to_accelerate_angularly
    // (+0xe4)(0, 0, speed, angle), FST float into T+0x30.
    float const AngleForCall = (TurnAngle < 0.0f) ? 0.0f : TurnAngle;
    float const SpeedForTime = ((double)(AngularSpeedXY) < 0.001) ? 0.0f : AngularSpeedXY;
    float const TimeToTurn = Obj->get_time_to_accelerate_angularly(0.0f, 0.0f, SpeedForTime, AngleForCall);

    // 062e307c-062e30cb: FCOMP 0.0f + TEST AH,0x41 + JP: "less or equal" gives 0.0f, anything
    // else (greater, unordered) calls get_angular_distance_travelled (+0xec)(0, 0, speed with
    // the same 0.001 floor, TimeToTurn).
    double Reach;
    if (TimeToTurn < 0.0f || TimeToTurn == 0.0f)
    {
        Reach = 0.0;
    }
    else
    {
        float const SpeedForDistance = ((double)(AngularSpeedXY) < 0.001) ? 0.0f : AngularSpeedXY;
        Reach = Obj->get_angular_distance_travelled(0.0f, 0.0f, SpeedForDistance, TimeToTurn);
    }

    // 062e30ce-062e30e9: Gate = Reach * double 0.98 (DAT_063a0610) on the FPU, replaced by
    // 0.001f (DAT_0639e6c8) only when less (TEST AH,0x5 + JP).
    double Gate = Reach * 0.98;
    if (Gate < (double)(0.001f))
        Gate = (double)(0.001f);

    // 062e30e9-062e312c: AngleZ FCOMP Gate + TEST AH,0x5 + JP: only "less" touches PlaneLimit
    // (T+0x34, seeded 1.0f at 062e2cbf): a TurnAngle equal to 0.0f (TEST AH,0x44 + JP) gives 0,
    // anything else get_projected_axis_throttle_XY (+0xf8)(TurnAngle) FSTP float.
    float PlaneLimit = 1.0f;
    if ((double)(AngleZ) < Gate)
    {
        if (TurnAngle == 0.0f)
            PlaneLimit = 0.0f;
        else
            PlaneLimit = Obj->get_projected_axis_throttle_XY(TurnAngle);
    }

    // 062e312c-062e3157: Ratio (T+4) = 1.0f unless AngleZ < ReferenceAngle (TEST AH,0x5 + JP),
    // then AngleZ / ReferenceAngle FSTP float.
    float Ratio = 1.0f;
    if (AngleZ < ReferenceAngle)
        Ratio = (float)((double)(AngleZ) / ReferenceAngle);

    // 062e3157-062e3178: get_axis_throttle (+0x50) into a zeroed T+0x5c.
    Vector AxisThrottle{ 0.0f, 0.0f, 0.0f };
    Obj->get_axis_throttle(AxisThrottle);

    // 062e317b-062e329c: Q0 (T+0x18) / Q1 (T+0x24) = the axis throttle projected onto columns
    // 0 and 1, same sequence.
    Vector const Q0 = LimitProjectOntoColumn(AxisThrottle, Col0);
    Vector const Q1 = LimitProjectOntoColumn(AxisThrottle, Col1);

    // 062e32a5-062e32c6: CommandPlane (T+0xc) = P0 + P1, ThrottlePlane (T+0x68) = Q0 + Q1
    // (FUN_062887d0, float adds).
    Vector CommandPlane;
    CommandPlane.x = P0.x + P1.x;
    CommandPlane.y = P0.y + P1.y;
    CommandPlane.z = P0.z + P1.z;
    Vector ThrottlePlane;
    ThrottlePlane.x = Q0.x + Q1.x;
    ThrottlePlane.y = Q0.y + Q1.y;
    ThrottlePlane.z = Q0.z + Q1.z;

    // 062e32c9-062e331a: TL = FUN_06347e60 length of ThrottlePlane ((x*x + y*y) + z*z); the
    // command plane's sqrt((z*z + y*y) + x*x) + 0.4f (DAT_063a0620) must be greater than TL
    // (TEST AH,0x41 + JNZ), then Ratio greater than TL + 0.4f is replaced by it (FSTP float).
    double const ThrottlePlaneLen = std::sqrt(((double)(ThrottlePlane.x) * ThrottlePlane.x +
                                               (double)(ThrottlePlane.y) * ThrottlePlane.y) +
                                              (double)(ThrottlePlane.z) * ThrottlePlane.z);
    double const CommandPlaneLen = std::sqrt(((double)(CommandPlane.z) * CommandPlane.z +
                                              (double)(CommandPlane.y) * CommandPlane.y) +
                                             (double)(CommandPlane.x) * CommandPlane.x);
    if (CommandPlaneLen + 0.4f > ThrottlePlaneLen)
    {
        double const Limit = ThrottlePlaneLen + 0.4f;
        if ((double)(Ratio) > Limit)
            Ratio = (float)(Limit);
    }

    // 062e331a-062e3331: Ratio greater than PlaneLimit (TEST AH,0x41 + JNZ) takes it.
    if (Ratio > PlaneLimit)
        Ratio = PlaneLimit;

    // 062e3331-062e33ca: with the flag byte, ZFactor (T+0x38, seeded 1.0f at 062e2cb7): P2 is
    // re-normalised (FUN_062e1680) when FUN_062c4780's squared length is greater than double
    // 0.01; AngleY below pi/8 (DAT_0639f5d0, TEST AH,0x5 + JP) gives (AngleY * 8/pi
    // (DAT_063a076c))^2 * 0.5f on the FPU, FSTP float; otherwise 0.5f, raised to 1.0f unless
    // AngleY is less than double pi/2 (DAT_0639e2d8, TEST AH,0x5 + JNP).
    float ZFactor = 1.0f;
    if (bBankBlend)
    {
        double const P2LenSq = ((double)(P2.x) * P2.x + (double)(P2.y) * P2.y) +
                               (double)(P2.z) * P2.z;
        if (P2LenSq > 0.01)
        {
            Vector Normalized;
            VecNormalize3(&P2, &Normalized);
            P2 = Normalized;
        }

        if (AngleY < 0.39269909f)
        {
            double const T = (double)(AngleY) * 2.54647923f;
            ZFactor = (float)(T * T * 0.5f);
        }
        else
        {
            ZFactor = 0.5f;
            if (!((double)(AngleY) < 1.5707963705062866))
                ZFactor = 1.0f;
        }
    }

    // 062e33ca-062e3455: a dead FUN_0628b030(0, 0, 0) into T+0xb0; Out = (P0 + P1) * Ratio +
    // P2 * ZFactor, each step a float op (FUN_06288830 / FUN_062887d0).
    Vector BankPart;
    BankPart.x = P2.x * ZFactor;
    BankPart.y = P2.y * ZFactor;
    BankPart.z = P2.z * ZFactor;
    Vector Plane;
    Plane.x = P0.x + P1.x;
    Plane.y = P0.y + P1.y;
    Plane.z = P0.z + P1.z;
    Vector PlanePart;
    PlanePart.x = Plane.x * Ratio;
    PlanePart.y = Plane.y * Ratio;
    PlanePart.z = Plane.z * Ratio;
    Out.x = PlanePart.x + BankPart.x;
    Out.y = PlanePart.y + BankPart.y;
    Out.z = PlanePart.z + BankPart.z;
    return &Out;
}

namespace
{
    // FUN_062c47a0 (cdecl Dot(A, B)): (A.z*B.z + A.y*B.y) + A.x*B.x, left in ST0.
    double BankDot(Vector const& A, Vector const& B)
    {
        return ((double)(A.z) * B.z + (double)(A.y) * B.y) +
               (double)(A.x) * B.x;
    }

    // The normalize-or-zero idiom ComputeBankedSteeringFrame inlines six times:
    // FUN_06347e60 (sqrt((x*x + y*y) + z*z), left in ST0), `FST float; FCOMP 0.01f;
    // TEST AH,0x41; JNZ` -- greater normalizes through FUN_062dc410 with the FST float
    // length, not-greater (and unordered) zeroes through FUN_0636cf40(0, 0, 0).
    void BankNormalizeOrZero(Vector& V)
    {
        double const Length = std::sqrt(((double)(V.x) * V.x + (double)(V.y) * V.y) +
                                        (double)(V.z) * V.z);
        if (Length > (double)(0.01f))
            VecNormalizeInPlaceByLength3(&V, (float)(Length));
        else
            V = Vector{ 0.0f, 0.0f, 0.0f };
    }
}

// Common.dll.c 0x062e37e0 (FUN_062e37e0), item 1744, Common.dll.txt:237847-238755.
// Re-ported 2026-10-08 (cmn-ai9) from a full trace of the listing. Stack slots are named
// S+N with S = ESP after the four register pushes (frame 0xa8). Every x87 sum, product,
// FSQRT / FCOS / FSIN and compare is kept in double; a value is rounded to float only
// where the listing has FST / FSTP float.
bool ComputeBankedSteeringFrame(FAiTarget* Elem, Vector const& TargetDir, Matrix& WorkingFrame, Vector& BankVector, unsigned char Mode)
{
    // 062e37e8-062e381b: the cached get_type flags (FAiTarget::GetFlags inlined), bit
    // 0x30000 required. BL = 0 is the early-return value.
    if ((Elem->GetFlags() & 0x30000) == 0)
        return false;

    bool bSteered = false; // S+0x33, zeroed at 062e37f8

    // 062e3821-062e3858: S+0x14 = WorkingFrame column 2, S+0x34 = iface column 2.
    Vector const FrameForward = MatrixColumn2(WorkingFrame);
    Vector const IfaceForward = MatrixColumn2(Elem->Iface->get_transform().Rotation);

    // 062e385d-062e38fc: BankLocal (S+0x50). Mode != 0: `(F.x*I.x + F.z*I.z) + F.y*I.y`
    // FCOMP 0.98f (DAT_063a0784), TEST AH,0x5 + JP returns false on greater, equal or
    // unordered; else the 3DMathEngine's TransposeTransform (+0x38) of BankVector by the
    // iface transform (Dst = S+0x14). Mode == 0 copies BankVector.
    Vector BankLocal;
    if (Mode != 0)
    {
        double const Aligned = ((double)(FrameForward.x) * IfaceForward.x +
                                (double)(FrameForward.z) * IfaceForward.z) +
                               (double)(FrameForward.y) * IfaceForward.y;
        if (!(Aligned < (double)(0.98f)))
            return false;
        Transform const& Xform = Elem->Iface->get_transform();
        Vector Local;
        Acquire3DMathEngine()->TransposeTransform(reinterpret_cast<FVector&>(Local),
                                                  reinterpret_cast<FMatrix const&>(Xform.Rotation),
                                                  reinterpret_cast<FVector const&>(BankVector));
        BankLocal = Local;
    }
    else
    {
        BankLocal = BankVector;
    }

    // 062e3900-062e390b: NewFrame (S+0x70) = WorkingFrame.
    Matrix NewFrame = WorkingFrame;

    // 062e390d-062e3956: |(F.x*T.x + F.z*T.z) + F.y*T.y| of a fresh iface column 2,
    // FSTP float S+0x44. 062e395a-062e396d: S+0x14 = iface column 1.
    Vector const IfaceForward2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
    float const AbsTargetAlign = (float)(std::fabs(
        ((double)(IfaceForward2.x) * TargetDir.x + (double)(IfaceForward2.z) * TargetDir.z) +
        (double)(IfaceForward2.y) * TargetDir.y));
    Vector const IfaceUp = MatrixColumn1(Elem->Iface->get_transform().Rotation);

    // 062e3972-062e39bc: FCOMP 0.999f (DAT_063a0780) + TEST AH,0x41 + JZ -- greater goes to
    // the tail. Then `(U.x*T.x + U.z*T.z) + U.y*T.y` of the iface column 1 FCOMP 0.0f +
    // TEST AH,0x5 + JP: greater, equal or unordered steers; less asks is_player (+0xbc)
    // and goes to the tail when it is false.
    bool bSteer = false;
    if (!(AbsTargetAlign > 0.999f))
    {
        double const UpDot = ((double)(IfaceUp.x) * TargetDir.x + (double)(IfaceUp.z) * TargetDir.z) +
                             (double)(IfaceUp.y) * TargetDir.y;
        bSteer = !(UpDot < 0.0) || Elem->Iface->is_player();
    }

    if (bSteer)
    {
        // 062e39c2-062e39d7: steered = 1, RollDirSign (S+0x4c) seeded with BankLocal.y
        // (it survives into the blend when the angle test below sets the sign flag),
        // RollScale (S+0x40) = 0.
        bSteered = true;
        float RollDirSign = BankLocal.y;
        float RollScale = 0.0f;

        // 062e39df-062e3a24: PerpFrame (S+0x94) = FUN_062e3460(-(iface column 2), 0.707f
        // (DAT_063a0768), TargetDir); FUN_062c5a70 negates by FCHS, exactly.
        Vector const IfaceForward3 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        Vector const NegForward3{ -IfaceForward3.x, -IfaceForward3.y, -IfaceForward3.z };
        Matrix PerpFrame{};
        if (BuildPerpendicularFrame(NegForward3, &PerpFrame, 0.707f, &TargetDir))
        {
            // 062e3a2a-062e3a79: S+0x24 = PerpFrame column 0, S+0x14 = TransposeTransform of
            // it by the iface transform.
            Vector const PerpRight = MatrixColumn0(PerpFrame);
            Transform const& Xform = Elem->Iface->get_transform();
            Vector LocalRight;
            Acquire3DMathEngine()->TransposeTransform(reinterpret_cast<FVector&>(LocalRight),
                                                      reinterpret_cast<FMatrix const&>(Xform.Rotation),
                                                      reinterpret_cast<FVector const&>(PerpRight));

            // 062e3a7c-062e3afb: RollAxis (S+0x34) = (-BankLocal.y, BankLocal.x, 0),
            // normalized or zeroed.
            Vector RollAxis{ -BankLocal.y, BankLocal.x, 0.0f };
            BankNormalizeOrZero(RollAxis);

            // 062e3afb-062e3b4b: Dot(LocalRight, RollAxis) clamped on the FPU (`FCOM 1.0f;
            // TEST AH,0x41; JNZ` replaces only a greater value, `FCOM -1.0f; TEST AH,0x5; JP`
            // only a smaller one).
            double Dot = BankDot(LocalRight, RollAxis);
            if (Dot > 1.0)
                Dot = 1.0;
            else if (Dot < -1.0)
                Dot = -1.0;

            // 062e3b51-062e3ba7: -Dot * sqrt((z*z + x*x) + y*y) of BankLocal, FST float S+0x40;
            // the unrounded value FCOMP double -1.0 (TEST AH,0x5 + JP, less stores -1.0f), then
            // the stored float FCOMP double 1.0 (TEST AH,0x41 + JNZ, greater stores 1.0f).
            double const BankLength = std::sqrt(((double)(BankLocal.z) * BankLocal.z +
                                                 (double)(BankLocal.x) * BankLocal.x) +
                                                (double)(BankLocal.y) * BankLocal.y);
            double const Scaled = -Dot * BankLength;
            RollScale = (float)(Scaled);
            if (Scaled < -1.0)
                RollScale = -1.0f;
            if ((double)(RollScale) > 1.0)
                RollScale = 1.0f;
        }

        // 062e3ba7-062e3c33: RollSignSet (S+0x23) = 0, BankAngle (S+0x10) = 0, |RollScale|
        // FST double S+0x44 and FCOMP double 0.01f (DAT_0639e6c0) + TEST AH,0x41 + JNZ.
        // Greater: |get_throttle (+0x4c)|, floored to 0.4f only when strictly less (TEST
        // AH,0x5 + JP keeps greater/equal/unordered), times get_max_bank_angle (+0x100) and
        // |RollScale| on the FPU, FSTP float; negated when RollScale < 0.0f.
        bool bRollSignSet = false;
        float BankAngle = 0.0f;
        double const AbsRollScale = std::fabs((double)(RollScale));
        if (AbsRollScale > (double)(0.01f))
        {
            BankAngle = 0.0f;
            Elem->Iface->get_throttle(BankAngle);
            BankAngle = std::fabs(BankAngle);
            if (BankAngle < 0.4f)
                BankAngle = 0.4f;
            double const MaxBank = Elem->Iface->get_max_bank_angle();
            BankAngle = (float)(MaxBank * BankAngle * AbsRollScale);
            if (RollScale < 0.0f)
                BankAngle = -BankAngle;
        }

        // 062e3c37-062e3c47: FCOS of |BankAngle|, FSTP float S+0x60.
        float const CosAbsBank = static_cast<float>(std::cos(std::fabs((double)(BankAngle))));

        // 062e3c4b-062e3c9d: |(F.z*T.z + F.y*T.y) + F.x*T.x| of a fresh iface column 2
        // FCOMP double 0.9999f (DAT_063a04e0) + TEST AH,0x5 + JP, then the double |RollScale|
        // FCOMP 0.01f + TEST AH,0x41 + JNZ: the frame is built only when the first is less
        // and the second greater; otherwise LAB_062e4233.
        Vector const IfaceForward4 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        double const AbsForwardDot = std::fabs(
            ((double)(IfaceForward4.z) * TargetDir.z + (double)(IfaceForward4.y) * TargetDir.y) +
            (double)(IfaceForward4.x) * TargetDir.x);
        if (AbsForwardDot < (double)(0.9999f) && AbsRollScale > (double)(0.01f))
        {
            // 062e3ca3-062e3d03: S+0x24 = cross(F, T) of a fresh iface column 2, each term
            // FSTP float.
            Vector const F5 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector Right;
            Right.x = (float)((double)(F5.z) * TargetDir.y - (double)(F5.y) * TargetDir.z);
            Right.y = (float)((double)(F5.x) * TargetDir.z - (double)(F5.z) * TargetDir.x);
            Right.z = (float)((double)(F5.y) * TargetDir.x - (double)(F5.x) * TargetDir.y);

            // 062e3d08-062e3d59: S+0x34 = Right, normalized or zeroed.
            BankNormalizeOrZero(Right);

            // 062e3d59-062e3d73: S+0x64 = iface column 0. 062e3d78-062e3dc9: S+0x14 = S+0x34
            // normalized (or zeroed) a SECOND time.
            Vector const IfaceRight = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            Vector RightAgain = Right;
            BankNormalizeOrZero(RightAgain);

            // 062e3dc9-062e3e27: AngleCos (S+0x5c) = Dot(RightAgain, IfaceRight), FST float;
            // the unrounded dot above 1.0f stores 1.0f, else the stored float below -1.0f
            // stores -1.0f.
            double const AngleDot = BankDot(RightAgain, IfaceRight);
            float AngleCos = (float)(AngleDot);
            if (AngleDot > 1.0)
                AngleCos = 1.0f;
            else if (AngleCos < -1.0f)
                AngleCos = -1.0f;

            // 062e3e27-062e3e41: NewFrame column 0 = S+0x34.
            NewFrame.E[0][0] = Right.x;
            NewFrame.E[1][0] = Right.y;
            NewFrame.E[2][0] = Right.z;

            // 062e3e4a-062e3eaa: S+0x24 = cross(Right, F) of a fresh iface column 2 (S+0x14),
            // each term FSTP float. 062e3eaf-062e3f00: S+0x14 = that, normalized or zeroed.
            Vector const F6 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector Up;
            Up.x = (float)((double)(Right.z) * F6.y - (double)(Right.y) * F6.z);
            Up.y = (float)((double)(Right.x) * F6.z - (double)(Right.z) * F6.x);
            Up.z = (float)((double)(Right.y) * F6.x - (double)(Right.x) * F6.y);
            BankNormalizeOrZero(Up);

            // 062e3f00-062e3f1d: NewFrame column 1 = Up. 062e3f26-062e3f5b: NewFrame column 2 =
            // a fresh iface column 2 (S+0x24).
            NewFrame.E[0][1] = Up.x;
            NewFrame.E[1][1] = Up.y;
            NewFrame.E[2][1] = Up.z;
            Vector const F7 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            NewFrame.E[0][2] = F7.x;
            NewFrame.E[1][2] = F7.y;
            NewFrame.E[2][2] = F7.z;

            // 062e3f3e-062e3f94: FCOS / FSIN of BankAngle stay on the FPU; every row's columns
            // 0 and 1 are rotated, new0 = old0*cos + sin*old1, new1 = cos*old1 - old0*sin,
            // each FSTP float (old1 is read before column 1 is stored).
            double const CosBank = std::cos(static_cast<double>(BankAngle));
            double const SinBank = std::sin(static_cast<double>(BankAngle));
            for (int Row = 0; Row < 3; ++Row)
            {
                double const Old0 = NewFrame.E[Row][0];
                double const Old1 = NewFrame.E[Row][1];
                NewFrame.E[Row][0] = (float)(Old0 * CosBank + SinBank * Old1);
                NewFrame.E[Row][1] = (float)(CosBank * Old1 - Old0 * SinBank);
            }

            // 062e3f96-062e4023: S+0x64 = fresh iface column 0, S+0x24 = NewFrame column 0;
            // Dot FST float S+0x10, clamped as AngleCos above, _CIacos of the float, FSTP float
            // S+0x44 = RotatedAngle.
            Vector const IfaceRight2 = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            Vector const NewRight = MatrixColumn0(NewFrame);
            double const RotDot = BankDot(NewRight, IfaceRight2);
            float RotCos = (float)(RotDot);
            if (RotDot > 1.0)
                RotCos = 1.0f;
            else if (RotCos < -1.0f)
                RotCos = -1.0f;
            float const RotatedAngle = (float)(std::acos((double)(RotCos)));

            // 062e402f: SignProbe (S+0x10) = 0. 062e4037-062e4074: |(U.z*T.z + U.y*T.y) +
            // U.x*T.x| of a fresh iface column 1 FCOMP double 0.9999f + TEST AH,0x5 + JP:
            // greater, equal or unordered goes straight to the angle test (LAB_062e4175).
            float SignProbe = 0.0f;
            bool bAngleTest = true;
            Vector const IfaceUp2 = MatrixColumn1(Elem->Iface->get_transform().Rotation);
            double const AbsUpDot = std::fabs(
                ((double)(IfaceUp2.z) * TargetDir.z + (double)(IfaceUp2.y) * TargetDir.y) +
                (double)(IfaceUp2.x) * TargetDir.x);
            if (AbsUpDot < (double)(0.9999f))
            {
                // 062e407a-062e40da: S+0x34 = cross(U, T) of a fresh iface column 1, FSTP float.
                Vector const U3 = MatrixColumn1(Elem->Iface->get_transform().Rotation);
                Vector UpCross;
                UpCross.x = (float)((double)(U3.y) * TargetDir.z - (double)(U3.z) * TargetDir.y);
                UpCross.y = (float)((double)(U3.z) * TargetDir.x - (double)(U3.x) * TargetDir.z);
                UpCross.z = (float)((double)(U3.x) * TargetDir.y - (double)(U3.y) * TargetDir.x);

                // 062e40df-062e411b: SignProbe = (F.z*C.z + F.y*C.y) + F.x*C.x of a fresh iface
                // column 2 (S+0x24), FST float S+0x10.
                Vector const F8 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
                double const SignDot = ((double)(F8.z) * UpCross.z + (double)(F8.y) * UpCross.y) +
                                       (double)(F8.x) * UpCross.x;
                SignProbe = (float)(SignDot);

                // 062e411f-062e4173: unrounded SignDot > 0 and RollScale > 0 (TEST AH,0x41 + JNZ
                // both) -> flag set, sign +1 (LAB_062e41a4). Otherwise the stored SignProbe < 0
                // and RollScale < 0 (TEST AH,0x5 + JP both) -> flag set, sign -1. Anything else
                // falls to the angle test.
                if (SignDot > 0.0 && RollScale > 0.0f)
                {
                    bRollSignSet = true;
                    RollDirSign = 1.0f;
                    bAngleTest = false;
                }
                else if (SignProbe < 0.0f && RollScale < 0.0f)
                {
                    bRollSignSet = true;
                    RollDirSign = -1.0f;
                    bAngleTest = false;
                }
            }

            if (bAngleTest)
            {
                // LAB_062e4175: AngleCos FCOMP CosAbsBank + TEST AH,0x41 + JNZ -- greater sets
                // the flag and keeps the seeded RollDirSign; otherwise the sign is -1 and the
                // flag stays clear, and SignProbe not greater than 0 (TEST AH,0x41 + JZ)
                // makes it +1.
                if (AngleCos > CosAbsBank)
                {
                    bRollSignSet = true;
                }
                else
                {
                    RollDirSign = -1.0f;
                    if (!(SignProbe > 0.0f))
                        RollDirSign = 1.0f;
                }
            }

            // LAB_062e41ac: get_angular_speed_Z(1.0f) (+0xf4) FSTP float S+0x60, then
            // get_angular_distance_travelled(0.0f, that, 1000.0f (DAT_063a0760)) (+0xe8)
            // FADD double 0.3141592741012573 (DAT_063a0778) = Denom on the FPU.
            float const AngularSpeed = Elem->Iface->get_angular_speed_Z(1.0f);
            double const Denom =
                (double)(Elem->Iface->get_angular_distance_travelled(0.0f, AngularSpeed, 1000.0f)) +
                0.3141592741012573;

            // 062e41e5-062e422d: RotatedAngle FCOMP Denom + TEST AH,0x5 + JP -- greater, equal
            // or unordered gives 1.0f; less gives (RotatedAngle / Denom)^2 (FDIVR), replaced by
            // 0.0f only when below 0.005f (DAT_063a0770, TEST AH,0x5 + JP). Times RollDirSign,
            // negated when the flag is clear, FSTP float BankLocal.z (S+0x58).
            double Blend;
            if (!((double)(RotatedAngle) < Denom))
            {
                Blend = 1.0;
            }
            else
            {
                Blend = (double)(RotatedAngle) / Denom;
                Blend = Blend * Blend;
                if (Blend < (double)(0.005f))
                    Blend = 0.0;
            }
            double const Signed = Blend * RollDirSign;
            BankLocal.z = bRollSignSet ? (float)(Signed) : (float)(-Signed);
        }
        else
        {
            // LAB_062e4233: BankLocal.z = -Dot(iface column 0 (FUN_062e0480 into S+0x64),
            // TargetDir), FCHS + FSTP float; then FUN_062e3460(-(fresh iface column 2),
            // &NewFrame, 0.707f, TargetDir) with its result ignored.
            Vector const IfaceRightF = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            BankLocal.z = (float)(-BankDot(IfaceRightF, TargetDir));
            Vector const IfaceForwardF = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector const NegForwardF{ -IfaceForwardF.x, -IfaceForwardF.y, -IfaceForwardF.z };
            BuildPerpendicularFrame(NegForwardF, &NewFrame, 0.707f, &TargetDir);
        }
    }

    // LAB_062e4296: S+0x64 = -(WorkingFrame column 2) (FUN_062d3af0 + FUN_062c5a70; the
    // frame is still the caller's, unmodified), S+0x14 = NewFrame column 1, and
    // FUN_062e1f40(Out = S+0x94, Reference = iface transform, Forward = S+0x64,
    // UpHint = S+0x14) copied over WorkingFrame (REP MOVSD 9).
    Vector const TailForward = MatrixColumn2(WorkingFrame);
    Vector const NegTailForward{ -TailForward.x, -TailForward.y, -TailForward.z };
    Transform const& TailXform = Elem->Iface->get_transform();
    Vector const TailUp = MatrixColumn1(NewFrame);
    Matrix FinalFrame{};
    BuildOrientationBasis(FinalFrame, TailXform.Rotation, NegTailForward, &TailUp);
    WorkingFrame = FinalFrame;

    // 062e42ed-062e4327: BankVector = FUN_062e2c00(iface transform, BankLocal).
    BankVector = TransformVectorByMatrix(Elem->Iface->get_transform().Rotation, BankLocal);

    // 062e432a: AL = S+0x33.
    return bSteered;
}

namespace CmnAiSteer
{
    // The own ship's position cache: retail inlines FAiTarget::RefreshPos on Sys+0x14fc
    // (Common.dll.txt e.g. 062d69c6-062d69e1).
    void RefreshCachedIfacePosition(SystemManager* Sys)
    {
        Sys->SelfTarget.RefreshPos();
    }

    Vector CachedIfacePosition(SystemManager* Sys)
    {
        RefreshCachedIfacePosition(Sys);
        return Sys->SelfTarget.CachedPos;
    }

    // SelfTarget.Iface slot 3 (get_transform), whose Transform opens with the orientation
    // Matrix.
    Matrix* IfaceOrientation(SystemManager* Sys)
    {
        // Retail calls slot 0xc on SelfTarget.Iface with no null test (2026-10-08, cmn-ai8:
        // the null return this helper used to have was invented).
        // Slot 3 is get_transform(); a Transform opens with its Matrix.
        return const_cast<Matrix*>(
            &Sys->SelfTarget.Iface->get_transform().Rotation);
    }

    // =========================================================================
    // THE "TWO-ARGUMENT ExternalIface SLOT 0xC CALL" DOES NOT EXIST.
    // Resolved 2026-09-17 (claude:common-steering-close). This paragraph
    // replaces, and refutes, a blocker that had stalled FaceIdea::Update (item
    // 1603) and DodgeIdea::Update (item 1618) since 2026-09-11. Do not
    // re-litigate it.
    //
    // The claim was: slot 0xc is called at some sites with 0 pushed args and at
    // others with 2, so two different methods must collide at that slot, and
    // nothing downstream of the 2-arg shape could be ported until the second
    // method was identified. Concretely, the 2-arg shape looks like this
    // (Common.dll.txt:220179-220189, one of ~10 byte-identical instances):
    //
    //     PUSH  0x0                  ; <- "argument 2"
    //     LEA   ECX,[EBP+0x58]
    //     PUSH  ECX                  ; <- "argument 1"
    //     MOV   ECX,EAX              ; this = ExternalIface
    //     CALL  dword ptr [EDX+0xc]
    //     PUSH  EAX
    //     LEA   EAX,[ESP+0x1ac]
    //     PUSH  EAX
    //     CALL  FUN_062e1f40         ; BuildOrientationBasis
    //     ...
    //     ADD   ESP,0x10
    //
    // Those two PUSHes are not arguments to the vtable call at all. They are
    // BuildOrientationBasis's arguments 4 and 3. MSVC evaluates and pushes
    // __cdecl arguments right to left, and when producing an earlier argument
    // requires a call, that call is simply emitted after the later arguments
    // are already on the stack. So the real argument list, in declaration
    // order, is exactly the documented four:
    //   arg1 = Out           (the last LEA/PUSH pair)
    //   arg2 = the slot-0xc return value  == IObjInspect::get_transform()
    //   arg3 = Forward       (the second PUSH from the top)
    //   arg4 = UpHint        (the first PUSH; 0 == nullptr)
    // and the slot-0xc call takes ZERO explicit arguments, identically to every
    // "0-arg" site.
    //
    // The decisive evidence is the stack accounting, which balances only under
    // this reading. BuildOrientationBasis ends in a bare `RET`
    // (Common.dll.txt:062e258c) -- __cdecl, caller-cleaned -- and every one of
    // these sites cleans exactly 0x10 bytes, i.e. four dwords, for a single
    // BuildOrientationBasis call. Under the old reading the vtable call's own
    // callee-cleanup would consume two of them and the `ADD ESP,0x10` would
    // over-clean by 8 on a shipped, working retail binary.
    //
    // Corollaries worth keeping:
    //   * No unidentified interface method needs finding. Slot 0xc is
    //     IObjInspect::get_transform() everywhere, whose mangled name
    //     `?get_transform@IObjInspectImpl@@UBEABVTransform@@XZ` encodes zero
    //     parameters.
    //   * The "nullable Vector const* param2" written up in
    //     common_systemmanager.h's ExternalIface comment was really
    //     BuildOrientationBasis's UpHint -- which genuinely IS a nullable
    //     Vector const*. That observation was correct; only its attribution
    //     was wrong.
    //   * The same pattern recurs around FUN_062e2630/BuildRandomSpreadFrame
    //     and FUN_062e3730/SteeringFrameFromRelativeDir in this cluster. Count
    //     the caller's own stack cleanup before concluding an arg-count
    //     mismatch anywhere in this file.
    // =========================================================================

    // FUN_062e3730, item 1743, Common.dll.txt:237770-237844 (cdecl, four
    // arguments, caller-cleaned): (Out, Reference, RelDir, UpHint), the same
    // order as BuildOrientationBasis. Retail returns Out in EAX; the three call
    // sites (062d6b01, 062d6bb8, 062d8555) only use it to copy Out, so the port
    // returns nothing.
    void SteeringFrameFromRelativeDir(Matrix& Out, Matrix const& Reference, Vector const& RelDir, Vector const& UpHint)
    {
        // 062e3733-062e3767: UpHint copied to local_78, then
        // BuildPerpendicularFrame(RelDir, &Frame, 0x3f7fbe77 = 0.999f, &local_78).
        Vector Hint = UpHint;
        Matrix Frame;
        Matrix Basis;
        if (BuildPerpendicularFrame(RelDir, &Frame, 0.999f, &Hint))
        {
            // 062e3773-062e379e: local_78 = column 1 of Frame (+4/+0x10/+0x1c), passed
            // as BuildOrientationBasis's UpHint.
            Hint.x = Frame.E[0][1];
            Hint.y = Frame.E[1][1];
            Hint.z = Frame.E[2][1];
            BuildOrientationBasis(Basis, Reference, RelDir, &Hint);
        }
        else
        {
            // LAB_062e37a0: no UpHint.
            BuildOrientationBasis(Basis, Reference, RelDir, nullptr);
        }
        // 062e37b5-062e37d7: nine dwords through local_48 into Out.
        Out = Basis;
    }
}

// TurnHelper::get_angular_throttle, decomp-tracker item 71665. See
// common_aisteer.h for the full derivation writeup (signature correction,
// step-by-step pipeline, .rdata constant decoding). Retail 0x062e28f0,
// Common.dll.txt:236552-236812.
Vector TurnHelper::get_angular_throttle(Matrix const& A, Matrix const& B, float* OutAngle)
{
    // Step 1: TransposedA = Transpose(A)  [retail FUN_06284f70, the Matrix
    // 9-float ctor, already ready_verified as item 691/Matrix::Matrix].
    Matrix const TransposedA(A.E[0][0], A.E[1][0], A.E[2][0],
                              A.E[0][1], A.E[1][1], A.E[2][1],
                              A.E[0][2], A.E[1][2], A.E[2][2]);

    // Re-traced 2026-10-08 (cmn-ai9), 062e28f0-062e2bf2; S = ESP after the three pushes.

    // 062e2947-062e2989: RelativeMat (S+0x50, copied to S+0x98) = MulMatrix (+0x20)(B,
    // TransposedA), the engine created lazily with no null test.
    Matrix RelativeMat;
    Acquire3DMathEngine()->MulMatrix(reinterpret_cast<FMatrix&>(RelativeMat),
                                     reinterpret_cast<const FMatrix&>(B),
                                     reinterpret_cast<const FMatrix&>(TransposedA));

    // 062e298b-062e29a7: Quat (S+0x40, W X Y Z) = MatrixToQuaternion (+0x50).
    FQuaternion Quat;
    Acquire3DMathEngine()->MatrixToQuaternion(Quat, reinterpret_cast<const FMatrix&>(RelativeMat));

    // 062e29aa-062e29f1: W clamped to [-1, 1] (FCOMP -1.0f + TEST AH,0x5 + JP: only "less"
    // stores -1; FCOMP 1.0f + TEST AH,0x41 + JNZ: only "greater" stores 1); Angle2 (S+0x24) =
    // _CIacos * 2 (FADD ST0,ST0), FSTP float.
    float ClampedW = Quat.W;
    if (Quat.W < -1.0f)
        ClampedW = -1.0f;
    else if (Quat.W > 1.0f)
        ClampedW = 1.0f;
    float Angle2 = (float)(std::acos((double)(ClampedW)) * 2.0);

    // 062e29f5-062e2a67: Axis (S+0x18) = (asin X, asin Y) FSTP float, asin Z FST float with the
    // unrounded value kept; LenSq = (z * float(z) + x*x) + y*y; above 1e-5f (DAT_0639b984,
    // TEST AH,0x41 + JNZ) Inv = 1 / sqrt(LenSq) on the FPU scales x, y and float(z).
    Vector Axis;
    Axis.x = (float)(std::asin((double)(Quat.X)));
    Axis.y = (float)(std::asin((double)(Quat.Y)));
    double const AsinZ = std::asin((double)(Quat.Z));
    Axis.z = (float)(AsinZ);
    double const AxisLenSq = (AsinZ * Axis.z + (double)(Axis.x) * Axis.x) +
                             (double)(Axis.y) * Axis.y;
    if (AxisLenSq > (double)(9.99999975e-06f))
    {
        double const Inv = 1.0 / std::sqrt(AxisLenSq);
        Axis.x = (float)(Axis.x * Inv);
        Axis.y = (float)(Axis.y * Inv);
        Axis.z = (float)(Inv * Axis.z);
    }

    // 062e2a69-062e2a8a: LocalAxis (S+0xc) = TransposeTransform (+0x38)(A, Axis).
    Vector LocalAxis;
    Acquire3DMathEngine()->TransposeTransform(reinterpret_cast<FVector&>(LocalAxis),
                                              reinterpret_cast<const FMatrix&>(A),
                                              reinterpret_cast<const FVector&>(Axis));

    // 062e2a8d-062e2aef: Scaled (S+0x34) = LocalAxis; MaxXY = |x| if greater than |y| (TEST
    // AH,0x41 + JZ), else |y|; above double 0.001 (TEST AH,0x41 + JNZ) x and y are multiplied by
    // 1.0 / MaxXY (FDIVR double 1.0), each FSTP float.
    Vector Scaled = LocalAxis;
    double const AbsX = std::fabs((double)(LocalAxis.x));
    double const AbsY = std::fabs((double)(LocalAxis.y));
    double const MaxXY = (AbsX > AbsY) ? AbsX : AbsY;
    if (MaxXY > 0.001)
    {
        double const InvMax = 1.0 / MaxXY;
        Scaled.x = (float)(LocalAxis.x * InvMax);
        Scaled.y = (float)(InvMax * LocalAxis.y);
    }

    // 062e2aef-062e2b1b: |z| above double 0.1 (TEST AH,0x41 + JNZ skips otherwise) snaps z to
    // 1.0f, or to -1.0f unless z is greater than 0.0.
    if (std::fabs((double)(LocalAxis.z)) > 0.1)
    {
        Scaled.z = 1.0f;
        if (!((double)(LocalAxis.z) > 0.0))
            Scaled.z = -1.0f;
    }

    // 062e2b23-062e2b44: World (S+0x28) = TransformMatrix (+0x2c)(A, Scaled).
    Vector WorldThrottle;
    Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(WorldThrottle),
                                           reinterpret_cast<const FMatrix&>(A),
                                           reinterpret_cast<const FVector&>(Scaled));

    // 062e2b47-062e2bc7: Angle2 FCOM pi (DAT_0639d208) + TEST AH,0x41 + JNZ: greater negates
    // the result and takes 2pi - Angle2 (DAT_0639c9e8); else FCOM -pi (DAT_0639d2a0) + TEST
    // AH,0x5 + JP: only "less" negates and takes Angle2 + 2pi.
    float const Pi = 3.14159274f;
    float const TwoPi = 6.28318548f;
    if (Angle2 > Pi)
    {
        WorldThrottle.x = -WorldThrottle.x;
        WorldThrottle.y = -WorldThrottle.y;
        WorldThrottle.z = -WorldThrottle.z;
        Angle2 = TwoPi - Angle2;
    }
    else if (Angle2 < -Pi)
    {
        WorldThrottle.x = -WorldThrottle.x;
        WorldThrottle.y = -WorldThrottle.y;
        WorldThrottle.z = -WorldThrottle.z;
        Angle2 = Angle2 + TwoPi;
    }

    // Step 10: optional angle out-param (retail's 4th incoming stack dword,
    // dropped by Ghidra -- see common_aisteer.h for the signature note).
    if (OutAngle != nullptr)
        *OutAngle = Angle2;

    return WorldThrottle;
}
