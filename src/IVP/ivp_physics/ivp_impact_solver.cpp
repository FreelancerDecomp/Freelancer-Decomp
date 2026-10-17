// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_mindist.h"
#include "ivp_mindist_settings.h"
#include "ivp_environment.h"
#include "ivp_anomaly_manager.h"
#include <cmath>

// ============================================================================
// IVP_Impact_Solver (retail .text:06356f30-0635867c, the 0x110-byte stack
// object built by do_impact_long_term FUN_06358d60 in ivp_impact.cpp).
//
// cmn-ivp3f 2026-10-09: every method re-ported against the listing with x87
// precision. Retail keeps intermediates on the FPU stack; the port computes
// them in double and rounds to float exactly where the listing has an
// FSTP/FST dword. The file-local float helpers that used to live here
// (TransformWSToOS, VectorLength, NormalizeAndGetLength, NormalizeIfNotZero,
// CosTaylorApprox) were lookalikes -- wrong term order, float precision and a
// 9.874e-11f epsilon where retail's DAT_063a53d8 is 1e-10f (0x2edbe6ff) -- and
// are gone. The real retail helpers are IVP_U_Matrix3::vimult (FUN_06366010),
// IVP_U_Matrix::vmult4 (FUN_063484e0), IVP_U_Point::normize (FUN_06347ef0) and
// IVP_U_Point::real_length_plus_normize (FUN_06347e90), all in ivp_math.h.
// ============================================================================

namespace
{
    // The solver's vectors are Freelancer Vector (x/y/z at +0/+4/+8); the
    // IVP_U_Point helpers used here only touch k[0..2].
    const IVP_U_Point* AsPoint(const Vector* V)
    {
        return reinterpret_cast<const IVP_U_Point*>(V);
    }

    IVP_U_Point* AsPoint(Vector* V)
    {
        return reinterpret_cast<IVP_U_Point*>(V);
    }

    const Vector* AsVector(const IVP_U_Point* P)
    {
        return reinterpret_cast<const Vector*>(P);
    }

    // The 4th-order cosine retail inlines twice in FUN_06357120
    // (063571a0-063571be and 0635720c-06357228), on the unrounded _CIasin result:
    //   a2 = a*a; c = (1.0f - a2*0.5f) + (a2*a2)*0.041666668f
    // DAT_063a53d0 = 1.0f, DAT_063a5460 = 0.5f, DAT_063a5660 = 0.041666668f.
    double IVP_ImpactSolver_CosTaylor(double Angle)
    {
        const double AngleSq = Angle * Angle;
        return ((double)(1.0f) - AngleSq * (double)(0.5f)) +
               (AngleSq * AngleSq) * (double)(0.041666668f);
    }

    // FUN_06358350's inlined WS -> OS rotation (063583a9-06358436 for core 0,
    // 0635849c-0635850f for core 1). Unlike FUN_06366010 every component uses
    // the same order: (r2.c*Z + r1.c*Y) + r0.c*X, kept extended, then FSTP dword.
    void IVP_ImpactSolver_RotateWSToOSInline(const IVP_U_Matrix* M, double X, double Y, double Z, Vector* Out)
    {
        Out->x = (float)((M->rows[2].k[0] * Z + M->rows[1].k[0] * Y) + M->rows[0].k[0] * X);
        Out->y = (float)((M->rows[2].k[1] * Z + M->rows[1].k[1] * Y) + M->rows[0].k[1] * X);
        Out->z = (float)((M->rows[2].k[2] * Z + M->rows[1].k[2] * Y) + M->rows[0].k[2] * X);
    }

    // `(N.z*V.z + N.y*V.y) + N.x*V.x`, the dot order used by every dot product
    // against NormalVectorWS in this class (06357a58, 06357adf, 06357b5d,
    // 06357c0c, 06356fea, 06358645).
    double IVP_ImpactSolver_DotZYX(const Vector* N, const Vector* V)
    {
        return ((double)(N->z) * V->z + (double)(N->y) * V->y) +
               (double)(N->x) * V->x;
    }
}

// Retail FUN_06357400 (.text:06357400-0635789b, thiscall, RET 8).
//   1. FUN_063484e0 (vmult4) of both contact points into locals that are never
//      read again (0635740b-06357434).
//   2. P0 = *PushVector * -1.0f (DAT_063a5498). V0/V1 = the contact point
//      speeds from the working Speed{N}/RotSpeed{N} (FUN_063461a0 on copies).
//      Rel = (dz*P0.z + dy*P0.y) + dx*P0.x with d = V1 - V0, extended.
//   3. bNormalDirection == 1 and Rel > 0 -> Rel = 0.0 (CMP 1; FCOM 0.0;
//      TEST AH,0x41; JNZ keeps Rel when it is <= 0 or unordered).
//   4. Needed = float(RescueSpeed - Rel); below zero or unordered -> return
//      (FCOMP 0.0; TEST AH,0x1; JNZ).
//   5. Denominator: core 1 first with P0, then core 0 with -P0 (== *PushVector),
//      each `(P.z*v.z + P.y*v.y) + v.x*P.x` of the point speed produced by a
//      unit push (FUN_06366010, FUN_06346cf0, FUN_063461a0); core 0's term is
//      added to the float spill of core 1's.
//   6. Magnitude = Needed / Denominator (FST dword); below zero or unordered
//      -> return.
//   7. Core 0: impulse float(P0 * -Magnitude) through FUN_06346cf0 into the
//      solver fields +0x84 (linear) / +0x64 (angular), then added into
//      Speed0 / RotSpeed0. Core 1: float(P0 * Magnitude) into +0x94 / +0x74,
//      added into Speed1 / RotSpeed1.
void IVP_Impact_Solver::PushContactPoint(const Vector* PushVector, int32_t bNormalDirection)
{
    IVP_U_Point WorldContactPoint0;
    IVP_U_Point WorldContactPoint1;
    MWorldFCore[0]->vmult4(AsPoint(ContactCoordsOS[0]), &WorldContactPoint0); // 0635741d, never read
    MWorldFCore[1]->vmult4(AsPoint(ContactCoordsOS[1]), &WorldContactPoint1); // 06357434, never read

    Vector Push0;
    Push0.x = -PushVector->x; // FMUL DAT_063a5498 (-1.0f)
    Push0.y = -PushVector->y;
    Push0.z = -PushVector->z;

    Vector PointSpeed0;
    Vector PointSpeed1;
    Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &Speed0, &RotSpeed0, &PointSpeed0); // 063574b2
    Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &Speed1, &RotSpeed1, &PointSpeed1); // 06357500

    const double Dx = (double)(PointSpeed1.x) - PointSpeed0.x;
    const double Dy = (double)(PointSpeed1.y) - PointSpeed0.y;
    const double Dz = (double)(PointSpeed1.z) - PointSpeed0.z;
    double RelativeSpeed = (Dz * Push0.z + Dy * Push0.y) + Dx * Push0.x;

    if (bNormalDirection == 1 && RelativeSpeed > 0.0)
    {
        RelativeSpeed = 0.0; // 0635755e, DAT_063a53d4
    }

    const float Needed = (float)((double)(RescueSpeed) - RelativeSpeed); // 06357564-06357568
    if (!(Needed >= 0.0f))
    {
        return; // 0635757d
    }

    Vector PushWS = Push0; // local_e4
    float Denominator = 0.0f; // 06357595, integer 0

    if ((Cores[1]->Flags & 0x0C) == 0)
    {
        IVP_U_Point PushOS;
        MWorldFCore[1]->vimult(AsPoint(&PushWS), &PushOS); // 063575bf
        Vector LinearDelta;
        Vector AngularDelta;
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], AsVector(&PushOS), &PushWS, &LinearDelta, &AngularDelta); // 063575ee
        Vector PointDelta;
        Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &LinearDelta, &AngularDelta, &PointDelta); // 06357615
        Denominator = (float)(((double)(PushWS.z) * PointDelta.z +
                                          (double)(PushWS.y) * PointDelta.y) +
                                         (double)(PointDelta.x) * PushWS.x); // 06357636
    }

    if ((Cores[0]->Flags & 0x0C) == 0)
    {
        PushWS.x = -PushWS.x; // 06357649-06357684, FMUL DAT_063a5498 (-1.0f)
        PushWS.y = -PushWS.y;
        PushWS.z = -PushWS.z;
        IVP_U_Point PushOS;
        MWorldFCore[0]->vimult(AsPoint(&PushWS), &PushOS); // 06357688
        Vector LinearDelta;
        Vector AngularDelta;
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], AsVector(&PushOS), &PushWS, &LinearDelta, &AngularDelta); // 063576b7
        Vector PointDelta;
        Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &LinearDelta, &AngularDelta, &PointDelta); // 063576de
        Denominator = (float)((((double)(PushWS.z) * PointDelta.z +
                                           (double)(PushWS.y) * PointDelta.y) +
                                          (double)(PointDelta.x) * PushWS.x) +
                                         (double)(Denominator)); // 063576ff-06357703
    }

    const double MagnitudeExt = (double)(Needed) / Denominator; // 0635770b
    const float Magnitude = (float)(MagnitudeExt);              // 0635770f FST dword
    if (!(MagnitudeExt >= 0.0))
    {
        return; // 0635771e
    }

    if ((Cores[0]->Flags & 0x0C) == 0)
    {
        const double NegMagnitude = -(double)(Magnitude); // 0635773a FCHS
        Vector Impulse;
        Impulse.x = (float)(NegMagnitude * Push0.x);
        Impulse.y = (float)((double)(Push0.y) * NegMagnitude);
        Impulse.z = (float)((double)(Push0.z) * NegMagnitude);
        IVP_U_Point ImpulseOS;
        MWorldFCore[0]->vimult(AsPoint(&Impulse), &ImpulseOS); // 06357769
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], AsVector(&ImpulseOS), &Impulse,
                                      &SpeedDelta0, &AngularDelta0Scratch); // 06357793
        RotSpeed0.x = AngularDelta0Scratch.x + RotSpeed0.x; // 06357798-063577b7
        RotSpeed0.y = AngularDelta0Scratch.y + RotSpeed0.y;
        RotSpeed0.z = AngularDelta0Scratch.z + RotSpeed0.z;
        Speed0.x = SpeedDelta0.x + Speed0.x; // 063577ba-063577d9
        Speed0.y = SpeedDelta0.y + Speed0.y;
        Speed0.z = SpeedDelta0.z + Speed0.z;
    }

    if ((Cores[1]->Flags & 0x0C) == 0)
    {
        Vector Impulse;
        Impulse.x = Push0.x * Magnitude; // 063577eb-0635781b, single products
        Impulse.y = Push0.y * Magnitude;
        Impulse.z = Push0.z * Magnitude;
        IVP_U_Point ImpulseOS;
        MWorldFCore[1]->vimult(AsPoint(&Impulse), &ImpulseOS); // 0635781f
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], AsVector(&ImpulseOS), &Impulse,
                                      &RotDelta1, &RotDelta0); // 06357849
        RotSpeed1.x = RotDelta0.x + RotSpeed1.x; // 0635784e-0635786d
        RotSpeed1.y = RotDelta0.y + RotSpeed1.y;
        RotSpeed1.z = RotDelta0.z + RotSpeed1.z;
        Speed1.x = RotDelta1.x + Speed1.x; // 06357870-0635788f
        Speed1.y = RotDelta1.y + Speed1.y;
        Speed1.z = RotDelta1.z + Speed1.z;
    }
}

// Retail FUN_06356f30 (.text:06356f30-06356fa2, thiscall, no arguments):
// SpeedDelta1 = point speed of core 1 - point speed of core 0, both from the
// working Speed{N}/RotSpeed{N} (FUN_063461a0); one FSUB per component.
void IVP_Impact_Solver::UpdateRelativeSpeed()
{
    Vector PointSpeed0;
    Vector PointSpeed1;
    Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &Speed0, &RotSpeed0, &PointSpeed0); // 06356f50
    Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &Speed1, &RotSpeed1, &PointSpeed1); // 06356f6f
    SpeedDelta1.x = PointSpeed1.x - PointSpeed0.x;
    SpeedDelta1.y = PointSpeed1.y - PointSpeed0.y;
    SpeedDelta1.z = PointSpeed1.z - PointSpeed0.z;
}

// Retail FUN_06358560 (.text:06358560-06358627, thiscall, RET 4). Core 1 first:
// NormalOS = vimult(MWorldFCore[N], Normal) (FUN_06366010), EffectiveMass{N} =
// float(FUN_06346790(ContactCoordsOS[N], NormalOS, Normal)), then Normal *=
// -1.0f. Afterwards a core with Flags & 0xC takes the other core's value times
// 100000.0f (DAT_063a5670), core 0's override first.
void IVP_Impact_Solver::PopulateEffectiveMasses(const Vector* NormalWS)
{
    Vector LocalNormal = *NormalWS;

    for (int32_t CoreIndex = 1; CoreIndex >= 0; --CoreIndex)
    {
        IVP_U_Point NormalOS;
        MWorldFCore[CoreIndex]->vimult(AsPoint(&LocalNormal), &NormalOS); // 0635859b
        const float EffectiveMass = (float)(
            Cores[CoreIndex]->calc_effective_mass_along_direction(ContactCoordsOS[CoreIndex], AsVector(&NormalOS),
                                                                  &LocalNormal)); // 063585b0-063585b5
        if (CoreIndex == 1)
        {
            EffectiveMass1 = EffectiveMass;
        }
        else
        {
            EffectiveMass0 = EffectiveMass;
        }

        LocalNormal.x = -LocalNormal.x; // 063585bb-063585ed, FMUL DAT_063a5498
        LocalNormal.y = -LocalNormal.y;
        LocalNormal.z = -LocalNormal.z;
    }

    if ((Cores[0]->Flags & 0x0C) != 0)
    {
        EffectiveMass0 = EffectiveMass1 * 100000.0f; // 063585fe-06358607
    }
    if ((Cores[1]->Flags & 0x0C) != 0)
    {
        EffectiveMass1 = EffectiveMass0 * 100000.0f; // 06358615-0635861e
    }
}

// Retail FUN_06358630 (.text:06358630-0635867c, thiscall, no arguments). After
// PopulateEffectiveMasses(NormalVectorWS):
//   ((-Dot * EffectiveMass1) * 2) * (1.0 / (EffectiveMass0 + EffectiveMass1)) * EffectiveMass0
// with Dot = (N.z*SD1.z + N.y*SD1.y) + N.x*SD1.x; returned unrounded in ST0.
double IVP_Impact_Solver::CalcImpulseNeeded2()
{
    PopulateEffectiveMasses(NormalVectorWS);

    const double NegDot = -IVP_ImpactSolver_DotZYX(NormalVectorWS, &SpeedDelta1);
    const double Scaled = NegDot * EffectiveMass1;
    const double Doubled = Scaled + Scaled; // FADD ST0,ST0
    const double InvSum = 1.0 / ((double)(EffectiveMass0) + EffectiveMass1);
    return (Doubled * InvSum) * EffectiveMass0;
}

// Retail FUN_06358350 (.text:06358350-0635854f, thiscall, RET 4).
// Impulse = float(PushDirection * Magnitude) per component. A dynamic core 0
// gets the impulse rotated by the inlined transform above and pushed through
// FUN_06346cf0 into SpeedDelta0 (+0x84) / AngularDelta0Scratch (+0x64), which
// FUN_06355020 then adds into RotSpeed0 / Speed0. A dynamic core 1 gets the
// negated impulse into RotDelta1 (+0x94, linear) / RotDelta0 (+0x74, angular),
// added into RotSpeed1 (FUN_06355020) and Speed1 (inline).
void IVP_Impact_Solver::StepImpulse2(float ImpulseMagnitude)
{
    Vector Impulse;
    Impulse.y = PushDirection.y * ImpulseMagnitude; // 0635836e
    Impulse.z = PushDirection.z * ImpulseMagnitude; // 0635837e
    Impulse.x = PushDirection.x * ImpulseMagnitude; // 0635838e

    if ((Cores[0]->Flags & 0x0C) == 0)
    {
        Vector ImpulseOS;
        IVP_ImpactSolver_RotateWSToOSInline(MWorldFCore[0], Impulse.x, Impulse.y, Impulse.z, &ImpulseOS);
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], &ImpulseOS, &Impulse, &SpeedDelta0,
                                      &AngularDelta0Scratch); // 0635843a
        RotSpeed0.x = RotSpeed0.x + AngularDelta0Scratch.x; // 06358444 FUN_06355020
        RotSpeed0.y = RotSpeed0.y + AngularDelta0Scratch.y;
        RotSpeed0.z = RotSpeed0.z + AngularDelta0Scratch.z;
        Speed0.x = Speed0.x + SpeedDelta0.x; // 0635844e FUN_06355020
        Speed0.y = Speed0.y + SpeedDelta0.y;
        Speed0.z = Speed0.z + SpeedDelta0.z;
    }

    if ((Cores[1]->Flags & 0x0C) == 0)
    {
        Vector NegImpulse;
        NegImpulse.x = -Impulse.x; // 06358462-0635849c, FMUL DAT_063a5498
        NegImpulse.y = -Impulse.y;
        NegImpulse.z = -Impulse.z;
        Vector ImpulseOS;
        IVP_ImpactSolver_RotateWSToOSInline(MWorldFCore[1], NegImpulse.x, NegImpulse.y, NegImpulse.z, &ImpulseOS);
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], &ImpulseOS, &NegImpulse, &RotDelta1,
                                      &RotDelta0); // 06358517
        RotSpeed1.x = RotSpeed1.x + RotDelta0.x; // 06358521 FUN_06355020
        RotSpeed1.y = RotSpeed1.y + RotDelta0.y;
        RotSpeed1.z = RotSpeed1.z + RotDelta0.z;
        Speed1.x = RotDelta1.x + Speed1.x; // 06358526-06358545
        Speed1.y = RotDelta1.y + Speed1.y;
        Speed1.z = RotDelta1.z + Speed1.z;
    }
}

// Retail FUN_06357340 (.text:06357340-063573f6, thiscall, no arguments): for each
// dynamic core, subtract the deltas StepImpulse2 last wrote (one FSUB each).
void IVP_Impact_Solver::UndoLastStepImpulse()
{
    if ((Cores[0]->Flags & 0x0C) == 0)
    {
        RotSpeed0.x = RotSpeed0.x - AngularDelta0Scratch.x;
        RotSpeed0.y = RotSpeed0.y - AngularDelta0Scratch.y;
        RotSpeed0.z = RotSpeed0.z - AngularDelta0Scratch.z;
        Speed0.x = Speed0.x - SpeedDelta0.x;
        Speed0.y = Speed0.y - SpeedDelta0.y;
        Speed0.z = Speed0.z - SpeedDelta0.z;
    }

    if ((Cores[1]->Flags & 0x0C) == 0)
    {
        RotSpeed1.x = RotSpeed1.x - RotDelta0.x;
        RotSpeed1.y = RotSpeed1.y - RotDelta0.y;
        RotSpeed1.z = RotSpeed1.z - RotDelta0.z;
        Speed1.x = Speed1.x - RotDelta1.x;
        Speed1.y = Speed1.y - RotDelta1.y;
        Speed1.z = Speed1.z - RotDelta1.z;
    }
}

// Retail FUN_06356fb0 (.text:06356fb0-0635711f, thiscall, no arguments).
//   PushDirection = SpeedDelta1, normize (FUN_06347ef0).
//   Dot = (N.z*PD.z + N.y*PD.y) + N.x*PD.x, FST dword; the sign test uses the
//   extended value (FCOMP 0.0; TEST AH,0x41; JNZ).
//   Dot > 0: PushDirection = normize(FrictionImpulse); return.
//   UseFrictionConeBlend != 0: FUN_06357120(float Dot); return.
//   -NormalCoeff >= Dot (FCOMP; TEST AH,0x1; JZ): return.
//   Tangent = float(PD + (-Dot)*N), normize; S = float(Tangent * TangentCoeff);
//   PushDirection = float(S + (-NormalCoeff)*N).
void IVP_Impact_Solver::UpdateFrictionPushDirection()
{
    PushDirection = SpeedDelta1;
    AsPoint(&PushDirection)->normize(); // 06356fdf

    const Vector* Normal = NormalVectorWS;
    const double DotExt = IVP_ImpactSolver_DotZYX(Normal, &PushDirection);
    const float Dot = (float)(DotExt); // 06356ffe FST dword

    if (DotExt > 0.0)
    {
        PushDirection = FrictionImpulse;
        AsPoint(&PushDirection)->normize(); // 06357031
        return;
    }

    if (UseFrictionConeBlend != 0)
    {
        ClampPushDirectionToFrictionCone(Dot); // 06357115
        return;
    }

    if (-NormalCoeff >= Dot)
    {
        return; // 0635705b
    }

    const double NegDot = -(double)(Dot);
    IVP_U_Point Tangent;
    Tangent.k[1] = (float)(NegDot * Normal->y + PushDirection.y);
    Tangent.k[2] = (float)(NegDot * Normal->z + PushDirection.z);
    Tangent.k[0] = (float)(NegDot * Normal->x + PushDirection.x);
    Tangent.normize(); // 06357095

    const float ScaledY = Tangent.k[1] * TangentCoeff; // 063570a4
    const float ScaledZ = Tangent.k[2] * TangentCoeff; // 063570b2
    const float ScaledX = Tangent.k[0] * TangentCoeff; // 063570c4

    const double NegNormalCoeff = -(double)(NormalCoeff);
    const float NewY = (float)(NegNormalCoeff * Normal->y + ScaledY);
    const float NewZ = (float)(NegNormalCoeff * Normal->z + ScaledZ);
    const float NewX = (float)(NegNormalCoeff * Normal->x + ScaledX);
    PushDirection.z = NewZ;
    PushDirection.x = NewX;
    PushDirection.y = NewY;
}

// Retail FUN_06357120 (.text:06357120-063572a5, thiscall, RET 4, float Dot).
//   Tangent = float(PD + (-Dot)*N); Length = float(FUN_06347e90(&Tangent)).
//   AbsDot = |(T.z*A.z + T.y*A.y) + T.x*A.x| (A = FrictionReferenceAxis), FST dword
//   ADf, the unrounded value goes to _CIasin. C1 = cos-Taylor(asin).
//   Y = ((TC*TC)*C1)*C1 + ((CBS*CBS)*ADf)*ADf (TC = TangentCoeff, CBS = ConeBlendScale).
//   Length*Length <= Y or unordered (FCOMP; TEST AH,0x41; JNZ): return.
//   S = sqrt(Y), FST dword Sf; C2 = cos-Taylor(asin(S)).
//   PushDirection = float(-C2 * N), then PushDirection = float(T*Sf + PushDirection).
void IVP_Impact_Solver::ClampPushDirectionToFrictionCone(float Dot)
{
    const Vector* Normal = NormalVectorWS;
    const double NegDot = -(double)(Dot);

    IVP_U_Point Tangent;
    Tangent.k[1] = (float)(NegDot * Normal->y + PushDirection.y);
    Tangent.k[2] = (float)(NegDot * Normal->z + PushDirection.z);
    Tangent.k[0] = (float)(NegDot * Normal->x + PushDirection.x);
    const float Length = (float)(Tangent.real_length_plus_normize()); // 0635716a-0635716f

    const double AbsDotExt = std::fabs(((double)(Tangent.k[2]) * FrictionReferenceAxis.z +
                                        (double)(Tangent.k[1]) * FrictionReferenceAxis.y) +
                                       (double)(Tangent.k[0]) * FrictionReferenceAxis.x);
    const float AbsDotF = (float)(AbsDotExt); // 06357197 FST dword
    const double Cos1 = IVP_ImpactSolver_CosTaylor(std::asin(AbsDotExt)); // 0635719b _CIasin

    const double TangentCoeffD = TangentCoeff;
    const double ConeBlendScaleD = ConeBlendScale;
    const double ConeRadiusSq = ((TangentCoeffD * TangentCoeffD) * Cos1) * Cos1 +
                                ((ConeBlendScaleD * ConeBlendScaleD) * AbsDotF) * AbsDotF;

    const double LengthSq = (double)(Length) * Length;
    if (!(LengthSq > ConeRadiusSq))
    {
        return; // 063571f5
    }

    const double Sin2 = std::sqrt(ConeRadiusSq);              // 063571fb
    const float Sin2F = (float)(Sin2);             // 063571fd FST dword
    const double NegCos2 = -IVP_ImpactSolver_CosTaylor(std::asin(Sin2)); // 06357201-0635722a

    PushDirection.x = (float)(NegCos2 * Normal->x);
    PushDirection.y = (float)(NegCos2 * Normal->y);
    PushDirection.z = (float)(NegCos2 * Normal->z);
    PushDirection.x = (float)((double)(Tangent.k[0]) * Sin2F + PushDirection.x);
    PushDirection.y = (float)((double)(Tangent.k[1]) * Sin2F + PushDirection.y);
    PushDirection.z = (float)((double)(Tangent.k[2]) * Sin2F + PushDirection.z);
}

// Retail FUN_06346340 (.text:06346340-063463e5, cdecl, 9 arguments).
//   Out = (CoreA dynamic ? CoreA->calc_point_speed_ws(ArmA, LinVelA, AngVelA) : 0)
//       - (CoreB dynamic ? CoreB->calc_point_speed_ws(ArmB, LinVelB, AngVelB) : 0)
// one FSUB per component. Only caller: FinalizeImpact (063581b8).
static void CalcRelativePointVelocity(IVP_Core* CoreA, IVP_Core* CoreB,
                                       const Vector* ArmA, const Vector* ArmB,
                                       const Vector* LinVelA, const Vector* AngVelA,
                                       const Vector* LinVelB, const Vector* AngVelB,
                                       Vector* Out)
{
    Vector VelA;
    if ((CoreA->Flags & 0x0C) == 0)
    {
        CoreA->calc_point_speed_ws(ArmA, LinVelA, AngVelA, &VelA);
    }
    else
    {
        VelA.x = 0.0f;
        VelA.y = 0.0f;
        VelA.z = 0.0f;
    }

    Vector VelB;
    if ((CoreB->Flags & 0x0C) == 0)
    {
        CoreB->calc_point_speed_ws(ArmB, LinVelB, AngVelB, &VelB);
    }
    else
    {
        VelB.x = 0.0f;
        VelB.y = 0.0f;
        VelB.z = 0.0f;
    }

    Out->x = VelA.x - VelB.x;
    Out->y = VelA.y - VelB.y;
    Out->z = VelA.z - VelB.z;
}

// Retail FUN_063582f0 (.text:063582f0-06358345): zero RotSpeedChange (+0x74) and
// SpeedChange (+0x84) of both cores.
void IVP_Impact_Solver::ClearPendingCoreChanges()
{
    Cores[0]->RotSpeedChange = Vector{0.0f, 0.0f, 0.0f};
    Cores[0]->SpeedChange    = Vector{0.0f, 0.0f, 0.0f};
    Cores[1]->RotSpeedChange = Vector{0.0f, 0.0f, 0.0f};
    Cores[1]->SpeedChange    = Vector{0.0f, 0.0f, 0.0f};
}

// Retail FUN_063572b0 (.text:063572b0-06357333, thiscall, RET 4).
//   Core->Speed = Speed{Index}; Core->RotSpeed = RotSpeed{Index} (dword copies).
//   06357304: Env = Core->Environment; Limits = Env->anomaly_limits (+0x24);
//   MOVSX word Core+0x64 > Limits->MaxCollisionsPerPsi (signed, JLE skips):
//   AL = Env->anomaly_manager (+0x20) slot 3 (Limits, Core), spliced into
//   MovementState bits 8..15 (`XOR EDX,EDX; MOV DH,AL; AND 0xffff00ff; OR`).
void IVP_Impact_Solver::CommitCoreVelocityImmediate(int32_t CoreIndex)
{
    IVP_Core* Core = Cores[CoreIndex];
    Core->Speed = (CoreIndex == 0) ? Speed0 : Speed1;
    Core->RotSpeed = (CoreIndex == 0) ? RotSpeed0 : RotSpeed1;

    IVP_Environment* Env = Core->Environment;
    IVP_Anomaly_Limits* Limits = Env->anomaly_limits;

    if (static_cast<int32_t>(Core->PendingImpactCount) > Limits->MaxCollisionsPerPsi)
    {
        const int32_t bFreeze = Env->anomaly_manager->MaxCollisionsExceededCheckFreezing(Limits, Core);
        Core->MovementState = (Core->MovementState & 0xFFFF00FFu) |
                              ((static_cast<uint32_t>(bFreeze) & 0xFFu) << 8);
    }
}

// Retail FUN_06358270 (.text:06358270-063582e2, thiscall, RET 4):
//   Core->SpeedChange = Speed{Index} - Core->Speed;
//   Core->RotSpeedChange = RotSpeed{Index} - Core->RotSpeed (one FSUB each).
void IVP_Impact_Solver::CommitCoreVelocityDeferred(int32_t CoreIndex)
{
    IVP_Core* Core = Cores[CoreIndex];
    const Vector& WorkingSpeed = (CoreIndex == 0) ? Speed0 : Speed1;
    const Vector& WorkingRotSpeed = (CoreIndex == 0) ? RotSpeed0 : RotSpeed1;
    Core->SpeedChange.x = WorkingSpeed.x - Core->Speed.x;
    Core->SpeedChange.y = WorkingSpeed.y - Core->Speed.y;
    Core->SpeedChange.z = WorkingSpeed.z - Core->Speed.z;
    Core->RotSpeedChange.x = WorkingRotSpeed.x - Core->RotSpeed.x;
    Core->RotSpeedChange.y = WorkingRotSpeed.y - Core->RotSpeed.y;
    Core->RotSpeedChange.z = WorkingRotSpeed.z - Core->RotSpeed.z;
}

// Retail FUN_06358070 (.text:06358070-0635825f, thiscall, RET 4).
//   1. Both cores: word PendingImpactCount (+0x64) += 1 - TypeCode, TypeCode =
//      the sign-extended Flags[3:2] (SHL 0x1c; SAR 0x1e).
//   2. bAllowDeferredCommit == 0 (integer TEST at 063580a9): commit both.
//   3. Winner = 0 when EffectiveMass0 > EffectiveMass1 (FCOMP; TEST AH,0x41;
//      JNZ -> 1 on <=, equal or unordered); CoresOut[Winner] with Flags & 0xC
//      -> commit both.
//   4. RelVel = FUN_06346340(winner's own Speed/RotSpeed, loser's working
//      Speed/RotSpeed); Dot = (y + z) + x against NormalVectorWS, *-1.0f when
//      Winner != 0; Dot not below RescueSpeed * -0.8333333f (DAT_063a566c;
//      FCOMPP; TEST AH,0x1; JZ) -> commit both.
//   5. Otherwise: ++Cores[0]->Environment->counter_5c (32-bit), CoresOut[Winner]
//      = null, ClearPendingCoreChanges, CommitCoreVelocityImmediate(Loser),
//      CommitCoreVelocityDeferred(Winner), --Cores[Winner]->PendingImpactCount.
//   "Commit both" = ClearPendingCoreChanges + CommitCoreVelocityImmediate(0)
//   + CommitCoreVelocityImmediate(1).
void IVP_Impact_Solver::FinalizeImpact(IVP_Core* CoresOut[2])
{
    auto BumpPendingCount = [](IVP_Core* Core)
    {
        const int32_t TypeCode = static_cast<int32_t>(Core->Flags << 28) >> 30;
        Core->PendingImpactCount = static_cast<int16_t>(Core->PendingImpactCount + (1 - TypeCode));
    };
    BumpPendingCount(Cores[0]);
    BumpPendingCount(Cores[1]);

    auto CommitBothImmediately = [this]()
    {
        ClearPendingCoreChanges();
        CommitCoreVelocityImmediate(0);
        CommitCoreVelocityImmediate(1);
    };

    if (bAllowDeferredCommit == 0)
    {
        CommitBothImmediately();
        return;
    }

    const int32_t WinnerIdx = (EffectiveMass0 > EffectiveMass1) ? 0 : 1;
    const int32_t LoserIdx = 1 - WinnerIdx;

    if ((CoresOut[WinnerIdx]->Flags & 0x0C) != 0)
    {
        CommitBothImmediately();
        return;
    }

    const Vector* LoserSpeed = (LoserIdx == 0) ? &Speed0 : &Speed1;
    const Vector* LoserRotSpeed = (LoserIdx == 0) ? &RotSpeed0 : &RotSpeed1;
    Vector RelVel;
    CalcRelativePointVelocity(Cores[WinnerIdx], Cores[LoserIdx],
                              ContactCoordsOS[WinnerIdx], ContactCoordsOS[LoserIdx],
                              &Cores[WinnerIdx]->Speed, &Cores[WinnerIdx]->RotSpeed,
                              LoserSpeed, LoserRotSpeed, &RelVel); // 063581b8

    const Vector* Normal = NormalVectorWS;
    double Dot = ((double)(RelVel.y) * Normal->y + (double)(RelVel.z) * Normal->z) +
                 (double)(RelVel.x) * Normal->x;
    if (WinnerIdx != 0)
    {
        Dot = Dot * -1.0; // 063581e2, DAT_063a5498
    }

    const double Threshold = (double)(RescueSpeed) * (double)(-0.8333333f); // DAT_063a566c
    if (!(Dot < Threshold))
    {
        CommitBothImmediately();
        return;
    }

    Cores[0]->Environment->counter_5c++; // 063581fb-06358204, always Cores[0]

    CoresOut[WinnerIdx] = nullptr;
    ClearPendingCoreChanges();
    CommitCoreVelocityImmediate(LoserIdx);
    CommitCoreVelocityDeferred(WinnerIdx);
    Cores[WinnerIdx]->PendingImpactCount = static_cast<int16_t>(Cores[WinnerIdx]->PendingImpactCount - 1);
}

// The velocity-sanity block retail inlines once per core in DoImpactSystem's
// tail (06357c7f-06357dba for core 0, 06357dbc-06357ee9 for core 1, the same
// shape). It works on the solver's working RotSpeed{N}/Speed{N}.
//   * Core->SpinClipping (+0x4C) != null: per component, `W > L -> W = L`
//     (FCOMP; TEST AH,0x41; JNZ) else `-L > W -> W = -L`.
//   * Otherwise, unless AxisLock (+0x10) != null and SpeedLimit (+0x08) compares
//     equal to 0.0f or unordered (FCOMP; TEST AH,0x40; JNZ skips):
//     M = inv_delta_time * MaxAngularVelocityPerPsi (extended);
//     M*M < (z*z + y*y) + x*x (FCOMPP; TEST AH,0x1) -> anomaly_manager slot 1.
//   * Then unconditionally MaxVelocity*MaxVelocity < FUN_062c4780(&Speed)
//     ((x*x + y*y) + z*z) -> anomaly_manager slot 0.
static void ClampResolvedCoreVelocities(IVP_Core* Core, Vector* WorkingRotSpeed, Vector* WorkingSpeed)
{
    IVP_Environment* Env = Core->Environment;
    IVP_Anomaly_Limits* Limits = Env->anomaly_limits;

    const Vector* PerAxisRotLimit = Core->SpinClipping;
    if (PerAxisRotLimit != nullptr)
    {
        if (WorkingRotSpeed->x > PerAxisRotLimit->x)
        {
            WorkingRotSpeed->x = PerAxisRotLimit->x;
        }
        else if (-PerAxisRotLimit->x > WorkingRotSpeed->x)
        {
            WorkingRotSpeed->x = -PerAxisRotLimit->x;
        }

        if (WorkingRotSpeed->y > PerAxisRotLimit->y)
        {
            WorkingRotSpeed->y = PerAxisRotLimit->y;
        }
        else if (-PerAxisRotLimit->y > WorkingRotSpeed->y)
        {
            WorkingRotSpeed->y = -PerAxisRotLimit->y;
        }

        if (WorkingRotSpeed->z > PerAxisRotLimit->z)
        {
            WorkingRotSpeed->z = PerAxisRotLimit->z;
        }
        else if (-PerAxisRotLimit->z > WorkingRotSpeed->z)
        {
            WorkingRotSpeed->z = -PerAxisRotLimit->z;
        }
    }
    else if (Core->AxisLock == nullptr || Core->SpeedLimit < 0.0f || Core->SpeedLimit > 0.0f)
    {
        const double MaxRotSpeed = (double)(Env->inv_delta_time) * Limits->MaxAngularVelocityPerPsi;
        const double RotX = WorkingRotSpeed->x;
        const double RotY = WorkingRotSpeed->y;
        const double RotZ = WorkingRotSpeed->z;
        const double RotMagSq = (RotZ * RotZ + RotY * RotY) + RotX * RotX;
        // FCOMPP (M*M vs Q); TEST AH,0x1; JZ skips when M*M >= Q, so the call
        // also fires on unordered (C0 set).
        if (!(MaxRotSpeed * MaxRotSpeed >= RotMagSq))
        {
            Env->anomaly_manager->MaxAngularVelocityExceeded(Limits, Core, WorkingRotSpeed);
        }
    }

    const double MaxSpeed = Limits->MaxVelocity;
    const double SpeedX = WorkingSpeed->x;
    const double SpeedY = WorkingSpeed->y;
    const double SpeedZ = WorkingSpeed->z;
    const double SpeedMagSq = (SpeedX * SpeedX + SpeedY * SpeedY) + SpeedZ * SpeedZ; // FUN_062c4780
    // Same mask/jump as above: the call is skipped only when V*V >= Q.
    if (!(MaxSpeed * MaxSpeed >= SpeedMagSq))
    {
        Env->anomaly_manager->MaxVelocityExceeded(Limits, Core, WorkingSpeed);
    }
}

// Retail FUN_063578a0 (.text:063578a0-0635805e, thiscall, RET 0x10). Only caller:
// do_impact_long_term FUN_06358d60 (06358ed0), which passes (PushedCores, 1,
// MOVSX word [LongTerm+0x52], rescue speed).
//  1. Prologue: N = FILD ImpactCounter (also FST dword local_1c);
//     RestitutionFactor = float(1 - (1 - Restitution) * (1 / (N*0.5f + 1)));
//     bAllowDeferredCommit = raw argument; RescueSpeed = float((DAT_0658ab38 =
//     G_IVP_MindistSettings.min_vertical_speed_at_collision + RescueSpeedAddon) * 1.2f);
//     CoresOut[] = Cores[]; ++Cores[0]->Environment->counter_4c;
//     MWorldFCore[N] = &Cores[N]->MWorldFCore (+0xF4); working velocities =
//     Core->RotSpeed + RotSpeedChange / Speed + SpeedChange; dword 0 into +0x18.
//  2. UpdateRelativeSpeed; *FrictionVectorWS = SpeedDelta1;
//     ImpulseStep = float(CalcImpulseNeeded2() * 0.1f) (DAT_063a5464).
//  3. Dot(N, SD1) > -1e-4f (DAT_063a5664; TEST AH,0x41; JNZ): PushContactPoint
//     (-N, 1); ImpactCounter > 10 -> bAllowDeferredCommit = 0.
//  4. Else: UpdateFrictionPushDirection; FrictionImpulse = 0;
//     ClosingSpeed = float(-Dot); TargetSpeed = float(sqrt(RF)*ClosingSpeed +
//     sqrt(float N)*0.01f) (DAT_063a5404); loop while ClosingSpeed > 0 and
//     Iter < 100 { ++Iter; StepImpulse2(ImpulseStep); UpdateRelativeSpeed;
//     ClosingSpeed = float(-Dot); UpdateFrictionPushDirection; };
//     Total = ClosingSpeed + TargetSpeed (FST dword, sign test extended);
//     PushDirection = N * -1.0f; Total > 0 and Iter != 100 -> unit probe:
//     StepImpulse2(1.0f); UpdateRelativeSpeed; Denom = Dot + ClosingSpeed
//     (extended); Scale = |Denom| > 1e-4f (DAT_063a547c) ? float(Total/Denom)
//     : 0; UndoLastStepImpulse; StepImpulse2(Scale). PushContactPoint(PD, 0).
//  5. Tail: ClampResolvedCoreVelocities for both cores; FinalizeImpact; if
//     (Cores[0]->MovementState | Cores[1]->MovementState) has any of bits
//     8..15: ++Cores[0]->Environment->counter_64 and, core 0 then core 1,
//     MovementState bits 8..15 = (0x100 - (TypeCode << 8)), SpeedChange +=
//     Speed, RotSpeedChange += RotSpeed, Speed = RotSpeed = 0, CoresOut[N] = core.
void IVP_Impact_Solver::DoImpactSystem(IVP_Core* CoresOut[2], int32_t bAllowDeferred, int32_t ImpactCounter,
                                       float RescueSpeedAddon)
{
    // ---- 1. prologue -------------------------------------------------------
    const double ImpactCounterExt = (double)(ImpactCounter); // 063578a3 FILD
    const float ImpactCounterF = (float)(ImpactCounterExt);  // 063578ad FST dword
    const float RestitutionFactor = (float)(
        (double)(1.0f) -
        ((double)(1.0f) - (double)(Restitution)) *
            ((double)(1.0f) / (ImpactCounterExt * (double)(0.5f) + (double)(1.0f))));

    bAllowDeferredCommit = bAllowDeferred; // 063578f1
    RescueSpeed = (float)(((double)(G_IVP_MindistSettings.min_vertical_speed_at_collision) + RescueSpeedAddon) *
                                     (double)(1.2f)); // 063578eb-06357902, DAT_063a5668

    CoresOut[0] = Cores[0]; // 06357904
    CoresOut[1] = Cores[1]; // 0635790c

    Cores[0]->Environment->counter_4c++; // 0635790f-06357918

    MWorldFCore[0] = &Cores[0]->MWorldFCore; // 0635792d
    MWorldFCore[1] = &Cores[1]->MWorldFCore; // 06357936

    RotSpeed0.x = Cores[0]->RotSpeedChange.x + Cores[0]->RotSpeed.x; // 06357939-06357961
    RotSpeed0.y = Cores[0]->RotSpeed.y + Cores[0]->RotSpeedChange.y;
    RotSpeed0.z = Cores[0]->RotSpeed.z + Cores[0]->RotSpeedChange.z;
    Speed0.x = Cores[0]->Speed.x + Cores[0]->SpeedChange.x; // 06357964-0635799c
    Speed0.y = Cores[0]->Speed.y + Cores[0]->SpeedChange.y;
    Speed0.z = Cores[0]->Speed.z + Cores[0]->SpeedChange.z;
    RotSpeed1.x = Cores[1]->RotSpeed.x + Cores[1]->RotSpeedChange.x; // 0635799f-063579cd
    RotSpeed1.y = Cores[1]->RotSpeed.y + Cores[1]->RotSpeedChange.y;
    RotSpeed1.z = Cores[1]->RotSpeed.z + Cores[1]->RotSpeedChange.z;
    Speed1.x = Cores[1]->Speed.x + Cores[1]->SpeedChange.x; // 063579d0-06357a0c
    Speed1.y = Cores[1]->Speed.y + Cores[1]->SpeedChange.y;
    Speed1.z = Cores[1]->Speed.z + Cores[1]->SpeedChange.z;

    // 06357a0f `MOV [ESI+0x18],EBP` (EBP == 0): a dword zero at +0x18. Nothing in
    // retail reads +0x10..+0x1B (see the field's comment in ivp_mindist.h); the
    // store is reproduced as the integer zero it is.
    Unread_0x10[2] = 0;

    // ---- 2. initial relative contact velocity ------------------------------
    UpdateRelativeSpeed();            // 06357a12
    *FrictionVectorWS = SpeedDelta1;  // 06357a17-06357a3c

    int32_t Iter = 0; // 06357a3f
    const float ImpulseStep = (float)(CalcImpulseNeeded2() * (double)(0.1f)); // 06357a43-06357a54

    // ---- 3. already-separating contact -------------------------------------
    if (IVP_ImpactSolver_DotZYX(NormalVectorWS, &SpeedDelta1) > (double)(-0.0001f)) // 06357a58-06357a81
    {
        Vector NegNormal;
        NegNormal.x = -NormalVectorWS->x; // 06357a83-06357aaa FCHS
        NegNormal.y = -NormalVectorWS->y;
        NegNormal.z = -NormalVectorWS->z;
        PushContactPoint(&NegNormal, 1); // 06357aae
        if (ImpactCounter > 10)          // 06357ab3-06357ab8
        {
            bAllowDeferredCommit = 0;    // 06357abe
        }
    }
    else
    {
        // ---- 4. restitution solve ------------------------------------------
        UpdateFrictionPushDirection();                  // 06357ac8
        FrictionImpulse = Vector{0.0f, 0.0f, 0.0f};    // 06357acd-06357ad9

        // (SD1.z*N.z + SD1.y*N.y) + SD1.x*N.x, FCHS, FSTP dword (06357adf-06357b05).
        float ClosingSpeed = (float)(-IVP_ImpactSolver_DotZYX(NormalVectorWS, &SpeedDelta1));
        const float TargetSpeed = (float)(
            std::sqrt((double)(RestitutionFactor)) * ClosingSpeed +
            std::sqrt((double)(ImpactCounterF)) * (double)(0.01f)); // 06357b09-06357b25

        while (ClosingSpeed > 0.0f) // 06357b29-06357b38, 06357b8e-06357b9d
        {
            if (Iter >= 100) // 06357b3e-06357b41
            {
                break;
            }
            ++Iter;                                                                         // 06357b49
            StepImpulse2(ImpulseStep);                                                      // 06357b51
            UpdateRelativeSpeed();                                                          // 06357b58
            ClosingSpeed = (float)(-IVP_ImpactSolver_DotZYX(NormalVectorWS, &SpeedDelta1)); // 06357b5d-06357b85
            UpdateFrictionPushDirection();                                                  // 06357b89
        }

        const double TotalSpeedExt = (double)(ClosingSpeed) + TargetSpeed; // 06357ba5-06357ba9
        const float TotalSpeed = (float)(TotalSpeedExt);                    // 06357bad FST dword

        PushDirection.x = -NormalVectorWS->x; // 06357bb1-06357bdf, FMUL DAT_063a5498
        PushDirection.y = -NormalVectorWS->y;
        PushDirection.z = -NormalVectorWS->z;

        // FCOMP 0.0; TEST AH,0x41; JNZ, then `CMP Iter,0x64; JZ` (equality).
        if (TotalSpeedExt > 0.0 && Iter != 100) // 06357be5-06357bf7
        {
            StepImpulse2(1.0f);    // 06357bf9-06357c00
            UpdateRelativeSpeed(); // 06357c07

            const double SpeedPerUnitImpulse =
                IVP_ImpactSolver_DotZYX(NormalVectorWS, &SpeedDelta1) + (double)(ClosingSpeed); // 06357c0c-06357c30
            float Scale;
            if (std::fabs(SpeedPerUnitImpulse) > (double)(0.0001f)) // 06357c34-06357c43
            {
                Scale = (float)((double)(TotalSpeed) / SpeedPerUnitImpulse); // 06357c45-06357c4b
            }
            else
            {
                Scale = 0.0f; // 06357c55
            }

            UndoLastStepImpulse(); // 06357c5f
            StepImpulse2(Scale);   // 06357c6b
        }

        PushContactPoint(&PushDirection, 0); // 06357c70-06357c7a
    }

    // ---- 5. tail -----------------------------------------------------------
    ClampResolvedCoreVelocities(Cores[0], &RotSpeed0, &Speed0); // 06357c7f-06357dba
    ClampResolvedCoreVelocities(Cores[1], &RotSpeed1, &Speed1); // 06357dbc-06357ee9

    FinalizeImpact(CoresOut); // 06357eea-06357ef1

    if ((((Cores[0]->MovementState | Cores[1]->MovementState) >> 8) & 0xFFu) == 0) // 06357ef6-06357f0b
    {
        return;
    }

    Cores[0]->Environment->counter_64++; // 06357f11-06357f14

    for (int32_t Idx = 0; Idx < 2; ++Idx)
    {
        IVP_Core* Core = Cores[Idx];

        // `x ^ ((x ^ (0x100 - (TypeCode << 8))) & 0xff00)` (06357f1d-06357f3c).
        const int32_t TypeCode = static_cast<int32_t>(Core->Flags << 28) >> 30;
        const uint32_t NewBits = static_cast<uint32_t>(0x100 - (TypeCode << 8)) & 0xFF00u;
        Core->MovementState = (Core->MovementState & 0xFFFF00FFu) | NewBits;

        // SpeedChange = SpeedChange + Speed (FUN_06355020 for core 0, inline for
        // core 1), RotSpeedChange = RotSpeed + RotSpeedChange, then dword zeroes.
        Core->SpeedChange.x = Core->SpeedChange.x + Core->Speed.x;
        Core->SpeedChange.y = Core->SpeedChange.y + Core->Speed.y;
        Core->SpeedChange.z = Core->SpeedChange.z + Core->Speed.z;
        Core->RotSpeedChange.x = Core->RotSpeedChange.x + Core->RotSpeed.x;
        Core->RotSpeedChange.y = Core->RotSpeed.y + Core->RotSpeedChange.y;
        Core->RotSpeedChange.z = Core->RotSpeed.z + Core->RotSpeedChange.z;
        Core->Speed = Vector{0.0f, 0.0f, 0.0f};
        Core->RotSpeed = Vector{0.0f, 0.0f, 0.0f};

        CoresOut[Idx] = Core; // 06357f9c / 06358054
    }
}

// FUN_06358d60 (do_impact_long_term) is ported in ivp_impact.cpp as
// IVP_Impact_Solver_Long_Term_Do_Impact_Long_Term.
