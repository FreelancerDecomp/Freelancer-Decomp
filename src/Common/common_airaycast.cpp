// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI ray / box / capsule / sphere tests and bearing angles (retail
// 0x062df830-0x062e1c90): RaySphereIntersect, RayAABBIntersect, RayOrientedBoxIntersect,
// RayTransformedBoxNearestHit, TransformedBoxesIntersect, BuildPerpendicularBasis, the
// capsule tests, ClampedDot3, SweptSphereCollisionTime, BearingAngleToIface/ToPoint.
// See src/include/common_aisteer.h for the per-function writeup.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_aisteer.cpp, code unchanged.

#include "common_aisteer.h"
#include "common_aisteer_internal.h"
#include "common_csys.h"          // Matrix, Transform, MatrixColumn0/1/2, Acquire3DMathEngine
#include "common_misc.h"          // FastInvSqrt
#include "x86math.h"              // I3DMathEngine::TransformXform

#include <cmath>
#include <cstring>

using namespace CmnAiSteer;

namespace
{
    // FUN_062e04c0 (item 1718), cdecl(A, B), EAX = 0 / 1; sole caller TransformedBoxesIntersect
    // (062e0cd9). The 15-axis OBB separating-axis test: Rij = dot(A.Axis_i, B.Axis_j),
    // T = B.Center - A.Center. T, the Rij, the |Rij| and Ta_i = dot(T, A.Axis_i) are float
    // locals in the sum orders written below; every radius and |projection| stays on the x87
    // stack, and a test separates (return 0) only on radius < |projection| -- `FCOMPP;
    // TEST AH,0x5; JP` goes on for not-less and NaN.
    bool OBBIntersect(OrientedBox const* A, OrientedBox const* B)
    {
        Vector const& AX = A->AxisX;
        Vector const& AY = A->AxisY;
        Vector const& AZ = A->AxisZ;
        Vector const& BX = B->AxisX;
        Vector const& BY = B->AxisY;
        Vector const& BZ = B->AxisZ;
        Vector const& AH = A->HalfExtent;
        Vector const& BH = B->HalfExtent;

        // 062e04cb-062e04e3: T = B.Center - A.Center.
        float const Tx = B->Center.x - A->Center.x;
        float const Ty = B->Center.y - A->Center.y;
        float const Tz = B->Center.z - A->Center.z;

        // 062e04e7-062e0574: row 0 and Ta0.
        float const R00 = (float)(((double)AX.z * BX.z + (double)BX.x * AX.x) + (double)AX.y * BX.y);
        float const R01 = (float)(((double)BY.z * AX.z + (double)BY.y * AX.y) + (double)BY.x * AX.x);
        float const R02 = (float)(((double)BZ.z * AX.z + (double)BZ.y * AX.y) + (double)BZ.x * AX.x);
        float const Ta0 = (float)(((double)Ty * AX.y + (double)Tx * AX.x) + (double)Tz * AX.z);
        float const AbsR00 = std::fabs(R00);
        float const AbsR01 = std::fabs(R01);
        float const AbsR02 = std::fabs(R02);
        // 062e0570-062e0599: axis A.AxisX.
        if ((((double)AbsR00 * BH.x + (double)AbsR02 * BH.z) + (double)AbsR01 * BH.y) + AH.x < std::fabs(Ta0))
            return false;

        // 062e05a1-062e0653: row 1, Ta1, axis A.AxisY.
        float const R10 = (float)(((double)AY.y * BX.y + (double)AY.z * BX.z) + (double)AY.x * BX.x);
        float const R11 = (float)(((double)AY.z * BY.z + (double)AY.y * BY.y) + (double)BY.x * AY.x);
        float const R12 = (float)(((double)AY.z * BZ.z + (double)AY.y * BZ.y) + (double)AY.x * BZ.x);
        float const Ta1 = (float)(((double)Tz * AY.z + (double)Ty * AY.y) + (double)Tx * AY.x);
        float const AbsR10 = std::fabs(R10);
        float const AbsR11 = std::fabs(R11);
        float const AbsR12 = std::fabs(R12);
        if ((((double)AbsR10 * BH.x + (double)AbsR12 * BH.z) + (double)AbsR11 * BH.y) + AH.y < std::fabs(Ta1))
            return false;

        // 062e065b-062e070d: row 2, Ta2, axis A.AxisZ.
        float const R20 = (float)(((double)AZ.y * BX.y + (double)AZ.z * BX.z) + (double)BX.x * AZ.x);
        float const R21 = (float)(((double)AZ.z * BY.z + (double)AZ.y * BY.y) + (double)BY.x * AZ.x);
        float const R22 = (float)(((double)AZ.z * BZ.z + (double)AZ.y * BZ.y) + (double)AZ.x * BZ.x);
        float const Ta2 = (float)(((double)Tz * AZ.z + (double)Ty * AZ.y) + (double)Tx * AZ.x);
        float const AbsR20 = std::fabs(R20);
        float const AbsR21 = std::fabs(R21);
        float const AbsR22 = std::fabs(R22);
        if ((((double)AbsR22 * BH.z + (double)AbsR21 * BH.y) + (double)AbsR20 * BH.x) + AH.z < std::fabs(Ta2))
            return false;

        // 062e0715-062e07df: axes B.AxisX / B.AxisY / B.AxisZ; the projections are not stored.
        if ((((double)AbsR20 * AH.z + (double)AbsR00 * AH.x) + (double)AbsR10 * AH.y) + BH.x <
            std::fabs(((double)Tz * BX.z + (double)Tx * BX.x) + (double)Ty * BX.y))
            return false;
        if ((((double)AbsR21 * AH.z + (double)AbsR01 * AH.x) + (double)AbsR11 * AH.y) + BH.y <
            std::fabs(((double)Tz * BY.z + (double)Ty * BY.y) + (double)Tx * BY.x))
            return false;
        if ((((double)AbsR22 * AH.z + (double)AbsR02 * AH.x) + (double)AbsR12 * AH.y) + BH.z <
            std::fabs(((double)Tz * BZ.z + (double)Ty * BZ.y) + (double)Tx * BZ.x))
            return false;

        // 062e07e7-062e0a4c: the nine cross axes, each radius summed as (p1 + p2) + (p3 + p4).
        if (((double)AbsR02 * BH.y + (double)AbsR01 * BH.z) + ((double)AbsR10 * AH.z + (double)AbsR20 * AH.y) <
            std::fabs((double)Ta2 * R10 - (double)R20 * Ta1))
            return false;
        if (((double)AbsR11 * AH.z + (double)AbsR21 * AH.y) + ((double)AbsR02 * BH.x + (double)AbsR00 * BH.z) <
            std::fabs((double)Ta2 * R11 - (double)R21 * Ta1))
            return false;
        if (((double)AbsR12 * AH.z + (double)AbsR22 * AH.y) + ((double)AbsR01 * BH.x + (double)AbsR00 * BH.y) <
            std::fabs((double)Ta2 * R12 - (double)R22 * Ta1))
            return false;
        if (((double)AbsR20 * AH.x + (double)AbsR00 * AH.z) + ((double)AbsR12 * BH.y + (double)AbsR11 * BH.z) <
            std::fabs((double)R20 * Ta0 - (double)Ta2 * R00))
            return false;
        if (((double)AbsR21 * AH.x + (double)AbsR01 * AH.z) + ((double)AbsR12 * BH.x + (double)AbsR10 * BH.z) <
            std::fabs((double)R21 * Ta0 - (double)Ta2 * R01))
            return false;
        if (((double)AbsR22 * AH.x + (double)AbsR02 * AH.z) + ((double)AbsR11 * BH.x + (double)AbsR10 * BH.y) <
            std::fabs((double)R22 * Ta0 - (double)Ta2 * R02))
            return false;
        if (((double)AbsR10 * AH.x + (double)AbsR00 * AH.y) + ((double)AbsR22 * BH.y + (double)AbsR21 * BH.z) <
            std::fabs((double)Ta1 * R00 - (double)R10 * Ta0))
            return false;
        if (((double)AbsR11 * AH.x + (double)AbsR01 * AH.y) + ((double)AbsR20 * BH.z + (double)AbsR22 * BH.x) <
            std::fabs((double)Ta1 * R01 - (double)R11 * Ta0))
            return false;
        if (((double)AbsR12 * AH.x + (double)AbsR02 * AH.y) + ((double)AbsR20 * BH.y + (double)AbsR21 * BH.x) <
            std::fabs((double)Ta1 * R02 - (double)R12 * Ta0))
            return false;

        // 062e0a54: MOV EAX,0x1.
        return true;
    }

    // The per-box setup retail inlines three times: twice in TransformedBoxesIntersect
    // (062e0a6e-062e0bb5 box A, 062e0bb8-062e0cd5 box B) and once in
    // RayTransformedBoxNearestHit (062dffde-062e00fe). Builds an OrientedBox from a
    // Transform and a local AABB given as {MinX,MaxX,MinY,MaxY,MinZ,MaxZ}:
    //   axes       = the rotation's columns 0/1/2 (+0/+0xc/+0x18, +4/+0x10/+0x1c,
    //                +8/+0x14/+0x20), raw, no sign flip;
    //   HalfExtent = |max - min| * 0.5 -- FSUB, FABS and the DOUBLE 0.5 at
    //                DAT_0639df88 on the x87 stack, one FSTP float;
    //   Center     = FUN_06299880(Xform, LocalMin + HalfExtent): the local centre is a
    //                float add (FUN_062887d0), then 3DMathEngine slot 0x28
    //                (TransformXform) through the cached engine singleton.
    OrientedBox BuildOBBFromTransformAndAABB(Transform const& Xform, float const* AabbMinMax)
    {
        OrientedBox Box;
        Box.AxisX = MatrixColumn0(Xform.Rotation);
        Box.AxisY = MatrixColumn1(Xform.Rotation);
        Box.AxisZ = MatrixColumn2(Xform.Rotation);
        Box.HalfExtent.x = (float)(std::fabs((double)AabbMinMax[1] - AabbMinMax[0]) * 0.5);
        Box.HalfExtent.y = (float)(std::fabs((double)AabbMinMax[3] - AabbMinMax[2]) * 0.5);
        Box.HalfExtent.z = (float)(std::fabs((double)AabbMinMax[5] - AabbMinMax[4]) * 0.5);
        Vector const LocalMin{ AabbMinMax[0], AabbMinMax[2], AabbMinMax[4] };
        Vector const LocalCenter = Add(LocalMin, Box.HalfExtent);
        Acquire3DMathEngine()->TransformXform((FVector&)Box.Center, (FTransform const&)Xform,
                                              (FVector const&)LocalCenter);
        return Box;
    }
}

bool TransformedBoxesIntersect(Transform const& XformA, float const* AabbA, Transform const& XformB, float const* AabbB)
{
    OrientedBox const BoxA = BuildOBBFromTransformAndAABB(XformA, AabbA);
    OrientedBox const BoxB = BuildOBBFromTransformAndAABB(XformB, AabbB);
    return OBBIntersect(&BoxA, &BoxB);
}

// ----------------------------------------------------------------------------
// Ray-intersection cluster (decomp-tracker items 1712-1714). See
// common_aisteer.h for the full per-function writeup and address/evidence
// citations.
// ----------------------------------------------------------------------------

namespace
{
    // Shared half-space clip used 6x by RayAABBIntersect (once per box face).
    // Generalized from the raw disassembly's two structurally-identical
    // per-axis blocks (Common.dll.txt:112826-112852 vs 112853-112877 etc.):
    // block 1 passes (Denom=Dir[i], Numer=-(Origin[i]+Half[i])), block 2
    // passes (Denom=-Dir[i], Numer=Origin[i]-Half[i]) -- algebraically the
    // same clip function both times, verified term-for-term against both
    // call shapes. Standard slab-test half-space clip: reject if the ray
    // starts on the wrong side and points away; otherwise tighten whichever
    // of TMin/TMax the intersection with this plane bounds.
    // Precision (each block of 062df976-062dfd46): Denom and Numer are FSTP'd floats,
    // the Denom*TMin / Denom*TMax products stay on the x87 stack for their compare
    // (TEST AH,0x5: JNP rejects on less, JP skips the store on not-less and NaN), and the
    // quotient is FSTP'd to float.
    bool ClipHalfSpace(float Denom, float Numer, float& TMin, float& TMax)
    {
        // TEST AH,0x41 / JNZ: not-greater (and NaN) goes to the second test.
        if (Denom > 0.0f)
        {
            if ((double)Denom * TMax < Numer)
                return false;
            if ((double)Denom * TMin < Numer)
                TMin = (float)((double)Numer / Denom);
        }
        // TEST AH,0x5 / JP: not-less (zero and NaN) goes to the parallel-face test.
        else if (Denom < 0.0f)
        {
            if ((double)Denom * TMin < Numer)
                return false;
            if ((double)Denom * TMax < Numer)
                TMax = (float)((double)Numer / Denom);
        }
        else // Denom == 0 (or NaN): ray parallel to this face's plane.
        {
            // `FCOMP 0.0f; TEST AH,0x41; JP <reject>`: greater AND unordered reject.
            if (!(Numer <= 0.0f))
                return false;
        }
        return true;
    }
}

// Common.dll.c 0x062df830 (FUN_062df830). See header for algorithm writeup.
bool RaySphereIntersect(Vector const& RayOrigin, Vector const& RayDir, Vector const& SphereCenter, float const& Radius, float* OutT)
{
    // 062df830-062df849: L = Center - Origin, all three differences kept on the x87 stack.
    double const Lx = (double)SphereCenter.x - RayOrigin.x;
    double const Ly = (double)SphereCenter.y - RayOrigin.y;
    double const Lz = (double)SphereCenter.z - RayOrigin.z;
    // 062df84c-062df862: B = (Lz*Dz + Ly*Dy) + Lx*Dx, FSTP'd to float (param_3's slot).
    float const B = (float)((Lz * RayDir.z + Ly * RayDir.y) + Lx * RayDir.x);
    // 062df866-062df88e: Disc = (B*B - ((Lx*Lx + Lz*Lz) + Ly*Ly)) + R*R, unrounded.
    double const Disc = ((double)B * B - ((Lx * Lx + Lz * Lz) + Ly * Ly)) + (double)Radius * Radius;
    // 062df890-062df89b: FCOM 0.0 (double); TEST AH,0x1 / JNZ leaves on less AND on unordered.
    if (!(Disc >= 0.0))
        return false;

    // 062df8a1-062df8b1: the unrounded root; T0 = B - Sq and T1 = Sq + B, each FSTP'd.
    double const Sq = std::sqrt(Disc);
    float const T0 = (float)(B - Sq);
    float const T1 = (float)(Sq + B);

    // 062df8b5-062df8d0: TEST AH,0x5 / JP on T0 -- not-less (or NaN) goes to LAB_062df8eb.
    if (T0 < 0.0f)
    {
        // 062df8d2-062df8ea: TEST AH,0x1 / JNZ on T1 -- less or NaN fails.
        if (!(T1 >= 0.0f))
            return false;
        if (OutT)
            *OutT = T1;
        return true;
    }
    // LAB_062df8eb: TEST AH,0x5 / JP on T1 -- T1 < 0 writes T0.
    if (T1 < 0.0f)
    {
        if (OutT)
            *OutT = T0;
        return true;
    }
    // LAB_062df904: T0 < T1 (TEST AH,0x5 / JP: not-less or NaN writes T1).
    if (OutT)
        *OutT = (T0 < T1) ? T0 : T1;
    return true;
}

// Common.dll.c 0x062df940 (FUN_062df940). See header for algorithm writeup.
bool RayAABBIntersect(Vector const& LocalOrigin, Vector const& LocalDir, Vector const& HalfExtent, float& TMin, float& TMax)
{
    float const InTMin = TMin;
    float const InTMax = TMax;

    if (!ClipHalfSpace(LocalDir.x, -(LocalOrigin.x + HalfExtent.x), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.x, LocalOrigin.x - HalfExtent.x, TMin, TMax))
        return false;
    if (!ClipHalfSpace(LocalDir.y, -(LocalOrigin.y + HalfExtent.y), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.y, LocalOrigin.y - HalfExtent.y, TMin, TMax))
        return false;
    if (!ClipHalfSpace(LocalDir.z, -(LocalOrigin.z + HalfExtent.z), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.z, LocalOrigin.z - HalfExtent.z, TMin, TMax))
        return false;

    // Retail's final guard: an interval that never actually moved from its
    // input means no face test ever tightened it (Common.dll.txt:112978-112981).
    return TMin != InTMin || TMax != InTMax;
}

// Common.dll.c 0x062dfd80 (FUN_062dfd80). See header for algorithm writeup.
bool RayOrientedBoxIntersect(Vector const& RayOrigin, Vector const& RayDir, OrientedBox const& Box, int* OutHitType, Vector* OutPoints)
{
    // 062dfd8f-062dfd96: O = RayOrigin - Box.Center (FUN_06288800, float per component).
    Vector const O = Sub(RayOrigin, Box.Center);

    // 062dfd9b-062dfe14: the recentred origin dotted with each axis, each sum
    // (z-term + y-term) + x-term on the x87 stack and FSTP'd to float.
    Vector const LocalOrigin{
        (float)(((double)O.z * Box.AxisX.z + (double)O.y * Box.AxisX.y) + (double)O.x * Box.AxisX.x),
        (float)(((double)O.z * Box.AxisY.z + (double)O.y * Box.AxisY.y) + (double)O.x * Box.AxisY.x),
        (float)(((double)O.z * Box.AxisZ.z + (double)O.y * Box.AxisZ.y) + (double)O.x * Box.AxisZ.x),
    };
    // 062dfe18-062dfe5f: the direction, summed (y-term + z-term) + x-term.
    Vector const LocalDir{
        (float)(((double)Box.AxisX.y * RayDir.y + (double)Box.AxisX.z * RayDir.z) + (double)Box.AxisX.x * RayDir.x),
        (float)(((double)Box.AxisY.y * RayDir.y + (double)Box.AxisY.z * RayDir.z) + (double)Box.AxisY.x * RayDir.x),
        (float)(((double)Box.AxisZ.y * RayDir.y + (double)Box.AxisZ.z * RayDir.z) + (double)RayDir.x * Box.AxisZ.x),
    };

    // 062dfdc0 / 062dfdcb: TMin = 0, TMax = 0x7f7fffff (FLT_MAX), in the param_1 /
    // param_3 slots.
    float TMin = 0.0f;
    float TMax = 3.4028235e+38f;
    // Retail has no null test on OutHitType or OutPoints (062dffbf, 062dfe98,
    // 062dff63 store unconditionally); its one caller (062e03a0) passes both.
    if (!RayAABBIntersect(LocalOrigin, LocalDir, Box.HalfExtent, TMin, TMax))
    {
        *OutHitType = 0;
        return false;
    }

    // The exit point both arms write (062dff0f-062dff55 / 062dff5f-062dffb0):
    // TMax*D.z is FSTP'd before its add, x and y add the origin to the unrounded
    // product.
    float const ExitZProduct = TMax * RayDir.z;
    Vector const Exit{
        (float)((double)TMax * RayDir.x + RayOrigin.x),
        (float)((double)TMax * RayDir.y + RayOrigin.y),
        ExitZProduct + RayOrigin.z };

    // 062dfe7f-062dfe8e: TEST AH,0x41 / JNZ -- not-greater (and NaN) is the inside arm.
    if (TMin > 0.0f)
    {
        // Entered from outside: hit type 2, entry point (TMin*D FSTP'd into a
        // Vector, then + origin, two roundings) in [0], exit point in [1].
        *OutHitType = 2;
        Vector const Scaled{ TMin * RayDir.x, TMin * RayDir.y, TMin * RayDir.z };
        OutPoints[0] = Vector{ Scaled.x + RayOrigin.x, Scaled.y + RayOrigin.y, Scaled.z + RayOrigin.z };
        OutPoints[1] = Exit;
        return true;
    }

    // Ray origin already inside the box: hit type 1, ONLY the exit point, into
    // OutPoints[0]; OutPoints[1] is left untouched.
    *OutHitType = 1;
    OutPoints[0] = Exit;
    return true;
}

// FUN_062dffd0, item 1715. Raw disasm Common.dll.txt:232432-232837, i.e. the
// whole body .text:062dffd0 (SUB ESP,0x88) through .text:062e0473 (the last
// RET) -- all 393 instructions, every one of the seven exit paths. There is no
// Ghidra decompile for this function (frame too large); it was read straight
// off the listing. See common_aisteer.h for the writeup and for why the old
// `blocked` reason did not survive contact with the bytes.
bool RayTransformedBoxNearestHit(Vector const& RayOrigin, Vector const& RayDir,
                                            Transform const& Xform, float const* AabbMinMax,
                                            Vector* OutPoint, bool* OutStartsInside)
{
    // 062dffd0-062e00d1: the three rotation columns, the per-axis half extents
    // (|max-min| * DAT_0639df88, and DAT_0639df88's raw bytes at .rdata:0639df88
    // are 00 00 00 00 00 00 E0 3F == the double 0.5), the local-space centre
    // (min + halfExtent, via FUN_062887d0 == Vector add) and finally the world
    // centre through FUN_06299880. That is instruction-for-instruction the same
    // sequence TransformedBoxesIntersect already factored out, and the retail
    // frame confirms it: locals local_64..local_2c are one contiguous run of 15
    // floats that FUN_062dfd80 later indexes as +0/+0xc/+0x18/+0x24/+0x30 --
    // exactly OrientedBox.
    OrientedBox const Box = BuildOBBFromTransformAndAABB(Xform, AabbMinMax);

    // 062e00e7-062e0104: the flag is cleared BEFORE any test, so it is written
    // even on the "no intersection" exits. Retail guards this one pointer
    // (TEST EDI,EDI) and no other.
    if (OutStartsInside != 0)
        *OutStartsInside = false;

    // 062e0107-062e0134: Diff = RayOrigin - Box.Center.
    Vector const Diff{ RayOrigin.x - Box.Center.x,
                       RayOrigin.y - Box.Center.y,
                       RayOrigin.z - Box.Center.z };

    // The rest is Eberly's ray-vs-OBB separating-axis test (Wild Magic
    // Ray3Box3 "TestIntersection"), unrolled by the compiler over the three box
    // axes and then the three RayDir x Diff cross axes. Retail's x87 comparison
    // idioms, spelled out so the branch senses can be checked against the bytes:
    //   062e0186 TEST AH,0x41 + JNZ  -> the JUMP is "<=", the FALL-THROUGH is
    //                                   strictly greater (the separating case);
    //   062e0197 TEST AH,0x1  + JNZ  -> the JUMP is strictly less than 0.0f
    //                                   (.rdata:DAT_06399408 reads 00 00 00 00),
    //                                   the FALL-THROUGH is >= 0 -> return 0.
    // The parallel term is deliberately NOT part of these first three tests:
    // this is the ray form of the test ("started outside the slab and heading
    // away from it"), not the segment form ("|DdU| > extent + |WdU|").

    // Precision (all six tests): the dots and products stay on the x87 stack; only
    // |W.U_i| is FSTP'd to a float local (local_70 / local_6c / local_68) for reuse.

    // --- Box.AxisX (062e0140-062e01a9) ---
    // `|D.U| FCOMP HalfExtent; TEST AH,0x41; JNZ` continues on not-greater; on greater,
    // `(D.U)*(W.U) FCOMP 0.0f; TEST AH,0x1; JNZ` continues on less AND unordered.
    double const WdU0 = ((double)Box.AxisX.z * RayDir.z + (double)Box.AxisX.y * RayDir.y) + (double)Box.AxisX.x * RayDir.x;
    float const AWdU0 = (float)std::fabs(WdU0);
    double const DdU0 = ((double)Diff.z * Box.AxisX.z + (double)Diff.y * Box.AxisX.y) + (double)Diff.x * Box.AxisX.x;
    if (std::fabs(DdU0) > Box.HalfExtent.x && DdU0 * WdU0 >= 0.0)
        return false;

    // --- Box.AxisY (062e01ab-062e0214) ---
    double const WdU1 = ((double)Box.AxisY.x * RayDir.x + (double)Box.AxisY.y * RayDir.y) + (double)Box.AxisY.z * RayDir.z;
    float const AWdU1 = (float)std::fabs(WdU1);
    double const DdU1 = ((double)Box.AxisY.z * Diff.z + (double)Box.AxisY.y * Diff.y) + (double)Box.AxisY.x * Diff.x;
    if (std::fabs(DdU1) > Box.HalfExtent.y && DdU1 * WdU1 >= 0.0)
        return false;

    // --- Box.AxisZ (062e0216-062e027f) ---
    double const WdU2 = ((double)Box.AxisZ.x * RayDir.x + (double)Box.AxisZ.y * RayDir.y) + (double)Box.AxisZ.z * RayDir.z;
    float const AWdU2 = (float)std::fabs(WdU2);
    double const DdU2 = ((double)Box.AxisZ.z * Diff.z + (double)Box.AxisZ.y * Diff.y) + (double)Box.AxisZ.x * Diff.x;
    if (std::fabs(DdU2) > Box.HalfExtent.z && DdU2 * WdU2 >= 0.0)
        return false;

    // 062e0281-062e02b7: WxD = RayDir x Diff, written as Diff.z*RayDir.y - Diff.y*RayDir.z
    // etc. (the same value as the textbook form). x and y are FSTP'd; z is FST'd to its
    // local but the FIRST cross test keeps using the unrounded z still in ST0.
    float const WxDx = (float)((double)Diff.z * RayDir.y - (double)Diff.y * RayDir.z);
    float const WxDy = (float)((double)Diff.x * RayDir.z - (double)Diff.z * RayDir.x);
    double const WxDzExact = (double)Diff.y * RayDir.x - (double)Diff.x * RayDir.y;
    float const WxDz = (float)WxDzExact;

    // --- RayDir x Box.AxisX (062e02bb-062e02f0) ---
    // `FXCH; FCOMPP; TEST AH,0x41; JNZ`: returns 0 only on |dot| > radius.
    double const AWxDdU0 = std::fabs((WxDzExact * Box.AxisX.z + (double)WxDy * Box.AxisX.y) + (double)WxDx * Box.AxisX.x);
    if (AWxDdU0 > (double)AWdU2 * Box.HalfExtent.y + (double)Box.HalfExtent.z * AWdU1)
        return false;

    // --- RayDir x Box.AxisY (062e02fd-062e0336) ---
    double const AWxDdU1 = std::fabs(((double)Box.AxisY.z * WxDz + (double)Box.AxisY.y * WxDy) + (double)Box.AxisY.x * WxDx);
    if (AWxDdU1 > (double)AWdU2 * Box.HalfExtent.x + (double)Box.HalfExtent.z * AWdU0)
        return false;

    // --- RayDir x Box.AxisZ (062e0343-062e037a) ---
    // Compiled the other way up (FCOMPP without FXCH, TEST AH,0x5 / JP continues on
    // radius >= |dot| and NaN), so the sense is the same as the two above.
    double const AWxDdU2 = std::fabs(((double)Box.AxisZ.z * WxDz + (double)Box.AxisZ.y * WxDy) + (double)Box.AxisZ.x * WxDx);
    if ((double)Box.HalfExtent.y * AWdU0 + (double)AWdU1 * Box.HalfExtent.x < AWxDdU2)
        return false;

    // 062e0387-062e03af: the slab clipper for the actual points. Retail seeds the
    // hit-type slot with 0 and ignores FUN_062dfd80's bool, switching on the slot.
    int HitType = 0;
    // RETAIL UB: this two-Vector scratch (local_24..local_10) is uninitialised in
    // retail. When FUN_062dfd80 reports type 0 after the SAT passed (reachable, e.g.
    // a NaN RayDir from a zero-length normalise in the caller, or a zero RayDir with
    // the origin inside the box), retail reads stack garbage in the two-point branch
    // below; the port zero-fills it instead.
    Vector HitPoints[2] = { Vector{ 0.0f, 0.0f, 0.0f }, Vector{ 0.0f, 0.0f, 0.0f } };
    RayOrientedBoxIntersect(RayOrigin, RayDir, Box, &HitType, HitPoints);

    if (HitType == 1)
    {
        // 062e03b1-062e03e0: the ray origin was already inside the box.
        if (OutStartsInside != 0)
            *OutStartsInside = true;
        *OutPoint = HitPoints[0];
        return true;
    }

    // 062e03e1-062e0443: return whichever point is nearer the ray origin, compared as
    // squared distances. The six differences stay on the x87 stack except D1.z, which
    // is FSTP'd to local_68 before it is squared.
    double const D0x = (double)HitPoints[0].x - RayOrigin.x;
    double const D0y = (double)HitPoints[0].y - RayOrigin.y;
    double const D0z = (double)HitPoints[0].z - RayOrigin.z;
    double const D1x = (double)HitPoints[1].x - RayOrigin.x;
    double const D1y = (double)HitPoints[1].y - RayOrigin.y;
    float const D1z = (float)((double)HitPoints[1].z - RayOrigin.z);
    double const Dist0Sq = (D0z * D0z + D0y * D0y) + D0x * D0x;
    double const Dist1Sq = ((double)D1z * D1z + D1y * D1y) + D1x * D1x;

    // 062e0440-062e044f: TEST AH,0x41 + JZ, so the FALL-THROUGH (point 1) is
    // taken when the comparison was NOT "greater" -- written as !(a > b) rather
    // than (a <= b) so that the unordered case picks the same side retail does.
    *OutPoint = !(Dist1Sq > Dist0Sq) ? HitPoints[1] : HitPoints[0];
    return true;
}

// Common.dll.c 0x062e0cf0 (FUN_062e0cf0). Item 1720. See common_aisteer.h for
// the algorithm writeup / the extraout_EDX Ghidra-artifact note.
namespace
{
    // FUN_062df7f0 (item 1711), cdecl, one float, result left in ST0. Its only two
    // call sites are in BuildPerpendicularBasis below. 062df7f0-062df827:
    //   h = X * 0.5f (DAT_063a073c); y = bits(0x5f3759df - (bits(X) >> 1)) with a
    //   LOGICAL shift (SHR); result = 1.5f*y - (h*y)*(y*y) (DAT_063a0740 == 1.5f,
    //   FXCH/FMULP/FSUBP), unrounded.
    double FastInvSqrtX87(float X)
    {
        double const Half = (double)X * 0.5f;
        unsigned Bits;
        std::memcpy(&Bits, &X, sizeof(Bits));
        Bits = 0x5f3759dfu - (Bits >> 1);
        float Y;
        std::memcpy(&Y, &Bits, sizeof(Y));
        return 1.5f * (double)Y - (Half * Y) * ((double)Y * Y);
    }
}

void BuildPerpendicularBasis(Vector* Out1, Vector* Out2, Vector* Dir, bool AlreadyNormalized)
{
    // 062e0cff-062e0d36: a real normalise -- sqrt((x*x + y*y) + z*z), `FDIVR 1.0f`, each
    // component times the unrounded reciprocal, FSTP'd back into *Dir.
    if (!AlreadyNormalized)
    {
        double const InvLen = 1.0 / std::sqrt(((double)Dir->x * Dir->x + (double)Dir->y * Dir->y) +
                                              (double)Dir->z * Dir->z);
        Dir->x = (float)(InvLen * Dir->x);
        Dir->y = (float)(InvLen * Dir->y);
        Dir->z = (float)(InvLen * Dir->z);
    }

    // 062e0d39-062e0d4d: `|y| FCOMPP |x|; TEST AH,0x41; JP` -- greater AND unordered take
    // the y-dominant arm (LAB_062e0d7f). Each arm passes the FSTP-float sum of squares to
    // FUN_062df7f0 and multiplies by its unrounded result.
    if (!(std::fabs(Dir->y) <= std::fabs(Dir->x)))
    {
        double const Scale = FastInvSqrtX87((float)((double)Dir->y * Dir->y + (double)Dir->z * Dir->z));
        Out1->x = 0.0f;
        Out1->y = (float)(Scale * Dir->z);
        Out1->z = (float)(-(Scale * Dir->y));
    }
    else
    {
        double const Scale = FastInvSqrtX87((float)((double)Dir->x * Dir->x + (double)Dir->z * Dir->z));
        Out1->y = 0.0f;
        Out1->x = (float)(-(Scale * Dir->z));
        Out1->z = (float)(Scale * Dir->x);
    }

    // 062e0db5-062e0dfc: Out2 = Out1 x Dir, one rounding per component.
    float const X = (float)((double)Out1->z * Dir->y - (double)Out1->y * Dir->z);
    float const Y = (float)((double)Out1->x * Dir->z - (double)Out1->z * Dir->x);
    float const Z = (float)((double)Out1->y * Dir->x - (double)Out1->x * Dir->y);
    Out2->x = X;
    Out2->y = Y;
    Out2->z = Z;
}

// Common.dll.c 0x062e0e10 (FUN_062e0e10). Item 1721. See header for the full
// ray-vs-capsule algorithm writeup and the correction of this cluster's
// earlier "ray-vs-cone" working title.
int RaySegmentCapsuleIntersect(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, float* OutT)
{
    // Retail passes SegStart and SegDir BY VALUE (param_4..6 / param_7..9); the port's
    // const references read the same floats. Precision below follows the listing: a
    // name ending in Exact stays on the x87 stack, every float is an FSTP / FST image.

    // 062e0e13-062e0e8f: L = (x*x + y*y) + z*z of SegDir; Height = the inlined fast
    // length of L (magic from L's FST float image, Newton step and multiply on the
    // unrounded L), FSTP'd to local_3c.
    double const AxisLenSqExact = ((double)SegDir.x * SegDir.x + (double)SegDir.y * SegDir.y) +
                                  (double)SegDir.z * SegDir.z;
    float const Height = (float)InlineFastLength(AxisLenSqExact);
    // 062e0e91-062e0eb3: a real FSQRT / FDIVR 1.0f; each axis component times the
    // unrounded reciprocal, FSTP'd (local_18 / local_14 / local_10).
    double const InvAxisLen = 1.0 / std::sqrt(AxisLenSqExact);
    Vector UnitAxis{ (float)(SegDir.x * InvAxisLen), (float)(SegDir.y * InvAxisLen), (float)(SegDir.z * InvAxisLen) };

    // 062e0e46-062e0eb9: BuildPerpendicularBasis(&b1 = local_30, &b2 = local_c,
    // &UnitAxis, 1) -- already normalised, so UnitAxis is not modified.
    Vector Basis1, Basis2;
    BuildPerpendicularBasis(&Basis1, &Basis2, &UnitAxis, true);

    // 062e0ebe-062e0f15: the three direction projections, each summed
    // (x-term + z-term) + y-term and FSTP'd.
    float const d1 = (float)(((double)Basis1.x * RayDir.x + (double)Basis1.z * RayDir.z) + (double)Basis1.y * RayDir.y);
    float const d2 = (float)(((double)Basis2.x * RayDir.x + (double)Basis2.z * RayDir.z) + (double)Basis2.y * RayDir.y);
    float const d3 = (float)(((double)UnitAxis.x * RayDir.x + (double)UnitAxis.z * RayDir.z) + (double)UnitAxis.y * RayDir.y);

    // 062e0f19-062e0f80: d2*d2 and d1*d1 are FSTP'd (local_38 / local_34); the length
    // (d3*d3 + d2d2) + d1d1 stays unrounded, its inlined fast length is inverted
    // (FDIVR 1.0f) and FSTP'd to local_40.
    float const D2Sq = d2 * d2;
    float const D1Sq = d1 * d1;
    double const DirLenSqExact = ((double)d3 * d3 + D2Sq) + D1Sq;
    float const InvDirLen = (float)(1.0 / InlineFastLength(DirLenSqExact));

    // 062e0f82-062e0fde: r = RayOrigin - SegStart on the x87 stack; o1 / o2 / o3 =
    // (z-term + y-term) + x-term against b1 / b2 / the axis, each FSTP'd.
    double const Rx = (double)RayOrigin.x - SegStart.x;
    double const Ry = (double)RayOrigin.y - SegStart.y;
    double const Rz = (double)RayOrigin.z - SegStart.z;
    float const o1 = (float)(((double)Basis1.z * Rz + Ry * Basis1.y) + Rx * Basis1.x);
    float const o2 = (float)(((double)Basis2.z * Rz + Ry * Basis2.y) + Rx * Basis2.x);
    float const o3 = (float)((Rz * UnitAxis.z + Ry * UnitAxis.y) + Rx * UnitAxis.x);

    // 062e0fe2: R*R, unrounded.
    double const RadiusSqExact = (double)Radius * Radius;

    // 062e0fea-062e0ffb: |d3| against the DOUBLE 0.999999999999 at DAT_063a0750;
    // TEST AH,0x1 / JNZ -- less and unordered take the general path.
    if (std::fabs(d3) >= 0.999999999999)
    {
        // Ray parallel to the axis. 062e0ffd-062e101c: Disc = (R*R - o1*o1) - o2*o2,
        // FCOM 0.0f, TEST AH,0x1 / JNZ -> return 0 on less AND unordered.
        double const Disc = (RadiusSqExact - (double)o1 * o1) - (double)o2 * o2;
        if (!(Disc >= 0.0))
            return 0;
        double const SqrtDisc = std::sqrt(Disc);
        // 062e1024-062e1040: OutT[0] = -((o3 + Sq) * Inv), OutT[1] = ((H - o3) + Sq) * Inv.
        OutT[0] = (float)(-((o3 + SqrtDisc) * InvDirLen));
        OutT[1] = (float)((((double)Height - o3) + SqrtDisc) * InvDirLen);
        return 2;
    }

    // LAB_062e1056: the lateral cylinder quadratic. A = d2d2 + d1d1, B = d2*o2 + d1*o1
    // and C = (o2*o2 + o1*o1) - R*R are FSTP'd (local_38 / param_2 / param_3).
    float const A = D2Sq + D1Sq;
    float B = (float)((double)d2 * o2 + (double)d1 * o1);
    float C = (float)(((double)o2 * o2 + (double)o1 * o1) - RadiusSqExact);
    // 062e1092-062e10b3: Disc = B*B - C*A; FST to param_1; the unrounded value is
    // compared (TEST AH,0x5 / JP continues on not-less and NaN, less returns 0).
    double const CylDiscExact = (double)B * B - (double)C * A;
    if (CylDiscExact < 0.0)
        return 0;
    float const CylDisc = (float)CylDiscExact;

    int Count = 0;
    // 062e10bb-062e10d0: the FST image against 0.0f; TEST AH,0x41 / JNZ -- not-greater
    // (zero, NaN) is the tangent arm LAB_062e1179.
    if (CylDisc > 0.0f)
    {
        // 062e10d6-062e1125: Sq = sqrt(Disc_f), InvA = 1.0f / A, T0 = (-B - Sq) * InvA
        // (FST to param_1); the axis test uses the UNROUNDED T0: 0.0f FCOMP ax -- greater
        // and unordered reject -- then ax FCOMP H, greater and unordered reject. The
        // stored root is the rounded T0 times Inv.
        double const SqrtCylDisc = std::sqrt((double)CylDisc);
        double const InvA = 1.0f / (double)A;
        double const T0Exact = (-(double)B - SqrtCylDisc) * InvA;
        float const T0 = (float)T0Exact;
        double const AxisCoord0 = T0Exact * d3 + o3;
        if (AxisCoord0 >= 0.0 && AxisCoord0 <= Height)
            OutT[Count++] = T0 * InvDirLen;

        // 062e1129-062e116a: T1 = (Sq - B) * InvA is FSTP'd first and the ROUNDED T1 is
        // used for the axis test.
        float const T1 = (float)((SqrtCylDisc - B) * InvA);
        double const AxisCoord1 = (double)T1 * d3 + o3;
        if (AxisCoord1 >= 0.0 && AxisCoord1 <= Height)
            OutT[Count++] = T1 * InvDirLen;

        // 062e116e-062e1178.
        if (Count == 2)
            return 2;
    }
    else
    {
        // LAB_062e1179: the tangent root T = -(B / A), FST to param_1; the unrounded T
        // goes through the same two axis tests; on a pass OutT[0] = T_f * Inv, return 1.
        double const TExact = -((double)B / A);
        float const T = (float)TExact;
        double const AxisCoord = TExact * d3 + o3;
        if (AxisCoord >= 0.0 && AxisCoord <= Height)
        {
            OutT[0] = T * InvDirLen;
            return 1;
        }
    }

    // LAB_062e11c2: the cap at SegStart. B' = o3*d3 + B and C' = o3*o3 + C are FSTP'd
    // over B / C; D = B'*B' - C' stays unrounded.
    B = (float)((double)o3 * d3 + B);
    C = (float)((double)o3 * o3 + C);
    double const DiscApex = (double)B * B - C;
    // 062e11ee-062e11f9: FCOM 0.0f; TEST AH,0x41 / JNZ -- not-greater goes to
    // LAB_062e1263, where `TEST AH,0x44 / JP` keeps only the equal case.
    bool bApexRoots = false;
    double TApexNear = 0.0;
    double TApexFar = 0.0;
    if (DiscApex > 0.0)
    {
        double const SqrtDiscApex = std::sqrt(DiscApex);
        TApexNear = -(double)B - SqrtDiscApex;
        TApexFar = SqrtDiscApex - B;
        bApexRoots = true;
        // 062e11fd-062e1231: a root counts when d3*T + o3 FCOMP 0.0f is not greater
        // (TEST AH,0x41 / JP rejects greater and unordered); the stored value is the
        // unrounded T times Inv.
        if ((double)d3 * TApexNear + o3 <= 0.0)
        {
            OutT[Count++] = (float)(TApexNear * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }
    else if (DiscApex == 0.0)
    {
        // 062e1270-062e1276: the single root -B', tested at LAB_062e1238.
        TApexFar = -(double)B;
        bApexRoots = true;
    }
    if (bApexRoots)
    {
        // LAB_062e1234 / LAB_062e1238: Sq - B' (or -B'), same test and store.
        if ((double)d3 * TApexFar + o3 <= 0.0)
        {
            OutT[Count++] = (float)(TApexFar * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }

    // LAB_062e127a: the cap at SegStart + SegDir. B'' = B' - d3*H is FST'd to param_2,
    // and its square is the UNROUNDED B'' times the rounded image; C'' = (H - (o3 + o3))*H
    // + C' stays unrounded.
    double const BTopExact = (double)B - (double)d3 * Height;
    B = (float)BTopExact;
    double const CTop = ((double)Height - ((double)o3 + o3)) * Height + C;
    double const DiscTop = BTopExact * B - CTop;
    bool bTopRoots = false;
    double TTopFar = 0.0;
    if (DiscTop > 0.0)
    {
        double const SqrtDiscTop = std::sqrt(DiscTop);
        double const TTopNear = -(double)B - SqrtDiscTop;
        TTopFar = SqrtDiscTop - B;
        bTopRoots = true;
        // 062e12b1-062e12e3: a root counts when d3*T + o3 FCOMP H is not less (TEST
        // AH,0x1 / JNZ rejects less and unordered).
        if ((double)d3 * TTopNear + o3 >= Height)
        {
            OutT[Count++] = (float)(TTopNear * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }
    else if (DiscTop == 0.0)
    {
        // 062e12ec-062e12fd: the single root -B''.
        TTopFar = -(double)B;
        bTopRoots = true;
    }
    if (bTopRoots)
    {
        // LAB_062e12e6 / LAB_062e12ff.
        if ((double)d3 * TTopFar + o3 >= Height)
        {
            OutT[Count++] = (float)(TTopFar * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }

    // LAB_062e132a.
    return Count;
}

// Common.dll.c 0x062e1330 (FUN_062e1330). Item 1722. See header for the
// "first hit, not last" correction writeup.
bool RaySegmentCapsuleFirstHit(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, Vector* OutPoint)
{
    // 062e1330-062e135d: retail's param_3 points at one 7-dword record {Radius, SegStart,
    // SegDir} that it copies onto the stack as FUN_062e0e10's by-value arguments; the
    // port passes the three pieces.
    float Ts[2];
    int const HitCount = RaySegmentCapsuleIntersect(RayOrigin, RayDir, Radius, SegStart, SegDir, Ts);

    // RETAIL UB: every t >= 0 is turned into a point stored at local_18 + 12*k, and
    // local_18 is copied to *OutPoint unconditionally at 062e13da; with no valid t the
    // copy is uninitialised stack. The port zero-fills instead.
    Vector Points[2] = { Vector{ 0.0f, 0.0f, 0.0f }, Vector{ 0.0f, 0.0f, 0.0f } };
    int ValidCount = 0;
    for (int i = 0; i < HitCount; ++i)
    {
        // 062e1370-062e137f: TEST AH,0x1 / JNZ skips less AND unordered.
        if (Ts[i] >= 0.0f)
        {
            // 062e1381-062e13ce: y and z products are FSTP'd before the origin is
            // added; x adds the origin to the unrounded t*dx.
            float const T = Ts[i];
            float const Ty = T * RayDir.y;
            float const Tz = T * RayDir.z;
            Points[ValidCount] = Vector{ (float)((double)T * RayDir.x + RayOrigin.x),
                                         Ty + RayOrigin.y,
                                         Tz + RayOrigin.z };
            ++ValidCount;
        }
    }
    *OutPoint = Points[0];
    // 062e13f4-062e13f9: SETG on the valid count.
    return ValidCount > 0;
}

// Common.dll.c 0x062e1410 (FUN_062e1410). Item 1723. See header for the
// point-vs-capsule algorithm writeup.
bool PointInCapsule(Vector const& Point, float Radius, Vector const& SegStart, Vector const& SegDir)
{
    // Retail's param_2 points at the same 7-dword capsule record {Radius, SegStart, SegDir}
    // RaySegmentCapsuleFirstHit takes; the port passes the pieces.
    // 062e141b-062e1429: the offset stays on the x87 stack.
    double Ox = (double)Point.x - SegStart.x;
    double Oy = (double)Point.y - SegStart.y;
    double Oz = (double)Point.z - SegStart.z;

    // 062e142c-062e144e: Dot = (Oz*Dz + Oy*Dy) + Ox*Dx, FST to a float; `FCOMP 0.0f;
    // TEST AH,0x41; JNP` skips the projection on less AND equal (greater and NaN project).
    // The FCOMP tests the unrounded sum; the FST image is what is reused below.
    double const DotExact = (Oz * SegDir.z + Oy * SegDir.y) + Ox * SegDir.x;
    float const Dot = (float)DotExact;
    if (!(DotExact <= 0.0))
    {
        // 062e1450-062e1480: |SegDir|^2 = (x*x + y*y) + z*z, FSTP float; Dot FCOMP it,
        // TEST AH,0x1 / JNZ -- less and NaN take the interior projection.
        float const SegLenSq = (float)(((double)SegDir.x * SegDir.x + (double)SegDir.y * SegDir.y) +
                                       (double)SegDir.z * SegDir.z);
        if (!(Dot >= SegLenSq))
        {
            // 062e1495-062e14c2: T = Dot / LenSq unrounded; T*Dx and T*Dy are FSTP'd
            // before the subtraction, T*Dz is not.
            double const T = (double)Dot / SegLenSq;
            Ox -= (float)(T * SegDir.x);
            Oy -= (float)(T * SegDir.y);
            Oz -= T * SegDir.z;
        }
        else
        {
            // 062e1482-062e1493: past the far end, offset from SegStart + SegDir.
            Ox -= SegDir.x;
            Oy -= SegDir.y;
            Oz -= SegDir.z;
        }
    }

    // 062e14c4-062e14f4: (Oz*Oz + Ox*Ox) + Oy*Oy against R*R, all unrounded;
    // `FCOMPP; TEST AH,0x1; JNZ` returns 0 on R*R less (and NaN).
    double const DistSq = (Oz * Oz + Ox * Ox) + Oy * Oy;
    return (double)Radius * Radius >= DistSq;
}

// FUN_062e1630 (Common.dll.txt 234782-234810), item 1727, cdecl, all 24 instructions:
// (A.z*B.z + A.y*B.y) + A.x*B.x on the FPU; `FCOM 1.0f; TEST AH,0x41; JNZ` -- only a
// strictly greater value falls through and is replaced by 1.0f; then `FCOM -1.0f;
// TEST AH,0x5; JP` -- only a strictly smaller value falls through and becomes -1.0f
// (NaN passes both tests unchanged). The result is returned in ST0 WITHOUT a float
// store, so it is a double here: a caller that FSTPs it to a float rounds exactly
// where retail rounds, and a caller that keeps it on the stack (acos, compares) keeps
// the full value.
double ClampedDot3(Vector const& A, Vector const& B)
{
    double const Dot = (static_cast<double>(A.z) * B.z + static_cast<double>(A.y) * B.y) +
                       static_cast<double>(A.x) * B.x;
    if (Dot > 1.0)
        return 1.0;
    if (Dot < -1.0)
        return -1.0;
    return Dot;
}

// Common.dll.c 0x062e18f0 (FUN_062e18f0). Item 1730. See header for the
// swept-sphere-vs-swept-sphere algorithm writeup.
bool SweptSphereCollisionTime(float RadiusA, Vector const& A0, Vector const& A1,
                                          float RadiusB, Vector const& B0, Vector const& B1, float* OutT)
{
    // 062e18f3-062e1913: dA = A1 - A0 on the x87 stack, z FSTP'd (local_4).
    double const DAx = (double)A1.x - A0.x;
    double const DAy = (double)A1.y - A0.y;
    float const DAz = A1.z - A0.z;
    // 062e1917-062e192b: dB = B1 - B0, x unrounded, y and z FSTP'd (local_20 / local_1c).
    double const DBx = (double)B1.x - B0.x;
    float const DBy = B1.y - B0.y;
    float const DBz = B1.z - B0.z;
    // 062e192f-062e1947: P = B0 - A0, each FSTP'd (local_30 / local_2c / local_28).
    float const Px = B0.x - A0.x;
    float const Py = B0.y - A0.y;
    float const Pz = B0.z - A0.z;
    // 062e194b-062e195f: V = dB - dA; x FSTP'd (local_18), y and z stay unrounded.
    float const Vx = (float)(DBx - DAx);
    double const Vy = DBy - DAy;
    double const Vz = (double)DBz - DAz;
    // 062e1963-062e196b: (RadiusA + RadiusB) FSTP'd into param_6's slot.
    float const RadiusSum = RadiusA + RadiusB;

    // 062e196f-062e1983: A = (Vz*Vz + Vy*Vy) + Vx*Vx, FSTP float.
    float const A = (float)((Vz * Vz + Vy * Vy) + (double)Vx * Vx);
    // 062e1987-062e199f: B = 2 * ((Vz*Pz + Vy*Py) + Vx*Px), FSTP float.
    double const BHalf = (Vz * Pz + Vy * Py) + (double)Vx * Px;
    float const B = (float)(BHalf + BHalf);
    // 062e19a3-062e19c3: C0 = (Pz*Pz + Py*Py) + Px*Px and R*R, both unrounded.
    double const C0 = ((double)Pz * Pz + (double)Py * Py) + (double)Px * Px;
    double const CombinedRadiusSq = (double)RadiusSum * RadiusSum;

    // 062e19c7-062e19e5: `C0 FCOMP R*R; TEST AH,0x41; JP` -- only greater and unordered
    // go on; already touching (C0 <= R*R) writes 0 and returns 1.
    if (C0 <= CombinedRadiusSq)
    {
        *OutT = 0.0f;
        return true;
    }

    // 062e19e6-062e1a04: Disc = B*B - ((C0 - R*R) * A) * 4.0f (DAT_063a05c0), FSTP float.
    float const Disc = (float)((double)B * B - ((C0 - CombinedRadiusSq) * A) * 4.0f);
    // 062e1a06-062e1a2a: Disc < 0 or A*A < 0.01f (TEST AH,0x1 / JNZ: less AND unordered)
    // -> LAB_062e1a84: *OutT = -1.0f, return 0.
    if (Disc >= 0.0f && (double)A * A >= 0.01f)
    {
        // 062e1a2c-062e1a54: Sq = sqrt(Disc_f), Inv = 1.0f / (A + A); Root2 =
        // (Sq - B) * Inv is FSTP'd, Root1 = (-B - Sq) * Inv stays unrounded.
        double const SqrtDisc = std::sqrt((double)Disc);
        double const InvTwoA = 1.0f / ((double)A + A);
        float const Root2 = (float)((SqrtDisc - B) * InvTwoA);
        double const Root1 = (-(double)B - SqrtDisc) * InvTwoA;
        // 062e1a56-062e1a6d: `Root2 FCOMP Root1; TEST AH,0x41; JZ` -- Root1 only when
        // Root2 is strictly greater; equal and unordered keep Root2.
        float const T = (Root2 > Root1) ? (float)Root1 : Root2;
        *OutT = T;
        // 062e1a6f-062e1a7c: TEST AH,0x5 / JNP -- only a negative T returns 0
        // (OutT keeps it); zero, positive and NaN return 1.
        return !(T < 0.0f);
    }

    *OutT = -1.0f;
    return false;
}

namespace
{
    // The clamp + _CIacos + FABS tail both bearing helpers share (062e1c25-062e1c78 /
    // 062e1dc6-062e1e17). The dot arrives as the FLOAT retail FSTPs it to; the clamp
    // first tests "greater than 1.0f" (FCOMP, TEST AH,0x41 + JNZ leaves for not-greater,
    // so the clamp is strictly greater-than), then "less than -1.0f" (TEST AH,0x5 + JP
    // leaves for greater, equal or unordered). acos runs on the float widened to double
    // and FABS'd; the result stays UNROUNDED in ST0 for the caller, hence double.
    double BearingClampedAcos(float Dot)
    {
        if (Dot > 1.0f)
        {
            Dot = 1.0f;   // 062e1c36 / 062e1dd7
        }
        else if (Dot < -1.0f)
        {
            Dot = -1.0f;  // 062e1c60 / 062e1e00
        }
        return std::fabs(std::acos(static_cast<double>(Dot)));
    }
}

// FUN_062e1aa0 (item 1731), Common.dll.txt 235241-235404, __cdecl, bare RET.
// Two-interface bearing: the angle between (Self's centre - Target's centre) and
// column 2 of Self's own transform.
double BearingAngleToIface(void* Self, void* Target)
{
    if (Self == nullptr || Target == nullptr)
    {
        return 0.0;   // 062e1c79, DAT_06399408 == 0.0f
    }

    // 062e1abd-062e1b58: retail queries Target first, then Self, each with the inlined
    // GetIfacePositionOrZero shape (slot 0x2c, slot 0x20 + zero on failure).
    Vector PosTarget;
    GetIfacePositionOrZero(&PosTarget, ObjInspectOf(Target));
    Vector PosSelf;
    GetIfacePositionOrZero(&PosSelf, ObjInspectOf(Self));

    // 062e1b5d: slot 0xc, get_transform; the matrix is read at +8/+0x14/+0x20.
    float const* const M = reinterpret_cast<float const*>(&ObjInspectOf(Self)->get_transform().Rotation);

    // 062e1b60-062e1b92: each difference FSTP'd to float.
    float const Dx = PosSelf.x - PosTarget.x;
    float const Dy = PosSelf.y - PosTarget.y;
    float const Dz = PosSelf.z - PosTarget.z;

    // 062e1b96-062e1bba: Len = sqrt((z*z + y*y) + x*x), unrounded.
    double const Len = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy)
                                 + static_cast<double>(Dx) * Dx);

    float Nx;
    float Ny;
    double Nz;
    // 062e1bbc: FCOM 0.01f, TEST AH,0x41 + JNZ takes the zero path for not-greater
    // (or unordered).
    if (Len > static_cast<double>(0.01f))
    {
        // 062e1bc9 FDIVR 1.0f: the inverse stays unrounded. x and y are FSTP'd to float,
        // z stays on the stack.
        double const Inv = 1.0 / Len;
        Nx = static_cast<float>(Dx * Inv);   // 062e1bd5
        Ny = static_cast<float>(Dy * Inv);   // 062e1bdf
        Nz = Dz * Inv;                       // 062e1be3
    }
    else
    {
        // 062e1be9-062e1bfa: FUN_0636cf40(0, 0, 0) zeroes the vector, z is reloaded.
        Nx = 0.0f;
        Ny = 0.0f;
        Nz = 0.0;
    }

    // 062e1bfe-062e1c1d: (z*m22 + y*m12) + x*m02, FSTP'd to float.
    float const Dot = static_cast<float>((Nz * M[8] + static_cast<double>(Ny) * M[5])
                                         + static_cast<double>(M[2]) * Nx);
    return BearingClampedAcos(Dot);
}

// FUN_062e1c90 (item 1732), Common.dll.txt 235417-235549, __cdecl(Self,
// Vector const* Point), bare RET. Ghidra counts one parameter; the second is read at
// 062e1d02 `MOV EAX,[ESP+0x34]` (entry ESP + 8) and all three callers push two dwords
// and clean 8 bytes. Same bearing as BearingAngleToIface against a caller-supplied
// point, but its normalisation rounds differently (see below).
double BearingAngleToPoint(void* Self, Vector const& ComparisonPoint)
{
    if (Self == nullptr)
    {
        return 0.0;   // 062e1c9c, DAT_06399408 == 0.0f
    }

    // 062e1ca7-062e1cf4: inlined GetIfacePositionOrZero(Self).
    Vector PosSelf;
    GetIfacePositionOrZero(&PosSelf, ObjInspectOf(Self));

    // 062e1cf9: slot 0xc, get_transform.
    float const* const M = reinterpret_cast<float const*>(&ObjInspectOf(Self)->get_transform().Rotation);

    // 062e1cfc-062e1d2e: each difference FSTP'd to float.
    float const Dx = PosSelf.x - ComparisonPoint.x;
    float const Dy = PosSelf.y - ComparisonPoint.y;
    float const Dz = PosSelf.z - ComparisonPoint.z;

    // 062e1d32-062e1d56: Len = sqrt((z*z + y*y) + x*x), unrounded.
    double const Len = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy)
                                 + static_cast<double>(Dx) * Dx);

    double Nx;
    double Ny;
    double Nz;
    // 062e1d58: FCOM 0.01f, TEST AH,0x41 + JNZ takes the zero path for not-greater
    // (or unordered).
    if (Len > static_cast<double>(0.01f))
    {
        // 062e1d65-062e1d6d: FLD 1.0f / FDIV ST0,ST1 / FSTP float -- unlike the
        // sibling above, the inverse IS rounded to float here, and the three products
        // stay unrounded on the stack.
        float const Inv = static_cast<float>(1.0 / Len);
        Nx = static_cast<double>(Inv) * Dx;   // 062e1d73-062e1d77
        Ny = static_cast<double>(Dy) * Inv;   // 062e1d7b-062e1d7f
        Nz = static_cast<double>(Dz) * Inv;   // 062e1d83-062e1d87
    }
    else
    {
        // 062e1d8d-062e1d9b: three FLD 0.0f.
        Nx = 0.0;
        Ny = 0.0;
        Nz = 0.0;
    }

    // 062e1da1-062e1dbc: (z*m22 + y*m12) + x*m02, FSTP'd to float.
    float const Dot = static_cast<float>((Nz * M[8] + Ny * M[5]) + Nx * M[2]);
    return BearingClampedAcos(Dot);
}
