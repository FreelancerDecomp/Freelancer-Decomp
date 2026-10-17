// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CDeadReckonedVector (non-inline members; the inline ones are in common_cobject.h).
#include "common_cobject.h"

#include <cmath>

// ============================================================================
// CDeadReckonedVector implementation
//
// Q(t) = BasePosition + Velocity*t + 0.5*Acceleration*t^2 is fitted through the
// three newest samples (t=0 at the newest), and a corrective quadratic
// Q2(tau) = BlendBasePosition + Velocity2*tau + 0.5*Acceleration2*tau^2 through
// Samples[1] at tau=-T, the previously rendered position at tau=0 and the
// primary quadratic's value at tau=ConvergeWindow. Doit() renders Q2 until
// ElapsedTime reaches ConvergeWindow and Q from then on.
//
// PRECISION. Retail does this on the x87 stack at 53-bit precision; a value is
// float only where the listing stores it with FSTP float (every Vector
// component, because every Vector passes through the Vector(float,float,float)
// constructor FUN_0628b030 or the operator helpers FUN_06288800 (a-b),
// FUN_062887d0 (a+b), FUN_06288830 (v*s), which all FSTP float). Coefficients,
// lengths, dots and times stay double. The code below spells each of those
// roundings out; a float temporary means the listing stored one.
//
// The copy constructor, operator=, CalcError, GetSample and the two Normalize
// members are inline in common_cobject.h: retail compiled them into every
// module from the header (see the export table on the class).
// ============================================================================

namespace
{
    // FUN_0628b030 plus the FSTP float the caller did for each argument.
    inline Vector MakeVector(float X, float Y, float Z)
    {
        Vector Result;
        Result.x = X;
        Result.y = Y;
        Result.z = Z;
        return Result;
    }

    // v * s with s held on the x87 stack as a double: FLD float [c], FMUL ST1,
    // FSTP float, once per component.
    inline Vector ScaleByDouble(Vector const& V, double S)
    {
        return MakeVector((float)(V.x * S), (float)(V.y * S),
                          (float)(V.z * S));
    }

    // FUN_06288800 / FUN_062887d0 and the inline FLD float / FSUB|FADD float /
    // FSTP float triples: float operands, one rounding per component.
    inline Vector VecSub(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x - B.x, A.y - B.y, A.z - B.z);
    }

    inline Vector VecAdd(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x + B.x, A.y + B.y, A.z + B.z);
    }

    // The two FSQRT sequences of AddSample (.text:06290d08..06290d57): each
    // square is exact in double, summed (x^2 + z^2) + y^2, root kept double.
    inline double LengthXZY(Vector const& V)
    {
        double const X = V.x;
        double const Y = V.y;
        double const Z = V.z;
        return sqrt((X * X + Z * Z) + Y * Y);
    }
}

// .text:06290930..06290a07: the same stores as Reset, inlined, plus the vptr.
// Retail never writes Samples (+0x08..+0x2c) or Timestamps (+0x30..+0x47)
// here, so they start indeterminate; the port zeroes them first (safe
// over-init, owner rule 2026-10-14) -- AddSample overwrites all of them on the
// first sample, but GetSample / a SampleCount==0 Doit could read them before.
CDeadReckonedVector::CDeadReckonedVector()
{
    for (int Index = 0; Index < 3; ++Index)
    {
        Samples[Index] = MakeVector(0.0f, 0.0f, 0.0f);
        Timestamps[Index] = 0.0;
    }
    Reset();
}

CDeadReckonedVector::~CDeadReckonedVector()
{
}

void CDeadReckonedVector::Reset()
{
    // .text:06290a20..06290aef. MaxCorrectiveTime goes back to the retail
    // constant 0x3fd0000000000000 = 0.25. Samples and Timestamps are NOT
    // touched: a SampleCount==0 Doit after Reset still projects from the old
    // Samples[0] (062917ff).
    BasePosition = MakeVector(0.0f, 0.0f, 0.0f);
    BlendBasePosition = MakeVector(0.0f, 0.0f, 0.0f);
    ProjectedPosition = MakeVector(0.0f, 0.0f, 0.0f);
    TrackedPosition = MakeVector(0.0f, 0.0f, 0.0f);
    Velocity = MakeVector(0.0f, 0.0f, 0.0f);
    Velocity2 = MakeVector(0.0f, 0.0f, 0.0f);
    ConvergedVelocity = MakeVector(0.0f, 0.0f, 0.0f);
    Acceleration = MakeVector(0.0f, 0.0f, 0.0f);
    Acceleration2 = MakeVector(0.0f, 0.0f, 0.0f);
    SampleCount = 0;
    ConvergeWindow = 0.0;
    ElapsedTime = 0.0;
    MaxCorrectiveTime = 0.25;
}

// .text:06290af0 .. .text:062916dc, RET 0xC. Traced instruction by instruction;
// the addresses below are where each value is produced.
void CDeadReckonedVector::AddSample(Vector const& Pos, double Timestamp)
{
    // History shift (06290b00..06290c07). With no samples yet all three slots
    // take the new one; with one, only the two newest shift and Timestamps[2]
    // is left alone; otherwise all three shift.
    if (SampleCount == 0)
    {
        Samples[0] = Pos;
        Samples[1] = Pos;
        Samples[2] = Pos;
        Timestamps[0] = Timestamp;
        Timestamps[1] = Timestamp;
        Timestamps[2] = Timestamp;
    }
    else
    {
        if (SampleCount == 1)
        {
            Samples[1] = Samples[0];
            Samples[0] = Pos;
        }
        else
        {
            Samples[2] = Samples[1];
            Samples[1] = Samples[0];
            Samples[0] = Pos;
            Timestamps[2] = Timestamps[1];
        }
        Timestamps[1] = Timestamps[0];
        Timestamps[0] = Timestamp;
    }

    // 06290c0a..06290ca7.
    double const Dt01 = Timestamp - Timestamps[1];
    ++SampleCount;
    bool bNearStraight = false;                       // [ESP+0xf] = 0
    ConvergeWindow = 0.0;                             // 06290c2f / 06290c37
    double const Dt12 = Timestamps[1] - Timestamps[2];
    BasePosition = Samples[0];
    double const Span = Dt12 + Dt01;                  // 06290c63
    double PrevElapsed = ElapsedTime - Dt01;          // 06290c75
    if (PrevElapsed < 0.0)                            // FCOM 0.0 / TEST AH,5 / JP
    {
        PrevElapsed = 0.0;
    }
    double const SpanT = PrevElapsed + Dt01;          // 06290c8e
    ConvergeWindow = Dt01;                            // 06290ca7

    // Local copies the rest of the function reads (06290c8c..06290ccb).
    Vector const P0 = Samples[0];
    Vector const P1 = Samples[1];
    Vector const P2 = Samples[2];

    Vector D1 = VecSub(P1, P0);                       // 06290cec
    Vector D2 = VecSub(P1, P2);                       // 06290d03
    double const Len1 = LengthXZY(D1);                // 06290d39, kept on the stack
    double const Len2 = LengthXZY(D2);                // 06290d57, stored double

    bool bQuadraticFit = false;
    if (Dt01 == 0.0 || Dt12 == 0.0 || Len1 == 0.0 || Len2 == 0.0)
    {
        // 06290d72 / 06290d8a / 06290d9b / 06290db3 all land on 06291349,
        // which SETS the near-straight flag before the secant fallback -- so a
        // degenerate sample converges linearly below.
        bNearStraight = true;
    }
    else
    {
        double const InvLen1 = 1.0 / Len1;            // FDIVR float 1.0
        D1.x = (float)(D1.x * InvLen1);
        D1.y = (float)(D1.y * InvLen1);
        D1.z = (float)(D1.z * InvLen1);
        double const InvLen2 = 1.0 / Len2;
        D2.x = (float)(D2.x * InvLen2);
        D2.y = (float)(D2.y * InvLen2);
        D2.z = (float)(D2.z * InvLen2);

        // 06290e1e..06290e41: (x*x' + z*z') + y*y', on the stack.
        double const Dot = ((double)D2.x * D1.x + (double)D2.z * D1.z) +
                           (double)D2.y * D1.y;

        // FCOM 0.0 / TEST AH,0x41 / JZ fallback: the fit runs unless Dot > 0.
        if (!(Dot > 0.0))
        {
            // FCOMP double -0.996194 (.rdata:0639c428) / TEST AH,5 / JP.
            if (Dot < -0.996194)
            {
                bNearStraight = true;
            }

            // Velocity, 06290e66..06290fd1.
            double const InvSpanDt12 = 1.0 / (Span * Dt12);           // stored [ESP+0x10]
            double const CoefP2 = InvSpanDt12 * Dt01;
            Vector const P2Term = ScaleByDouble(P2, CoefP2);
            double const InvDt01 = 1.0 / Dt01;
            double const CoefP1 = 1.0 / Dt12 + InvDt01;
            Vector const P1Term = ScaleByDouble(P1, CoefP1);
            double const CoefP0 = 1.0 / Span + InvDt01;
            Vector const P0Term = ScaleByDouble(P0, CoefP0);
            Vector const Outer = VecSub(P2Term, P1Term);
            Velocity = VecAdd(P0Term, Outer);

            // Acceleration, 06290fb5..062910f9.
            Vector const P2Acc = ScaleByDouble(P2, InvSpanDt12 + InvSpanDt12);
            Vector const P1Acc = ScaleByDouble(P1, 2.0 / (Dt12 * Dt01));
            Vector const P0Acc = ScaleByDouble(P0, 2.0 / (Span * Dt01));
            Vector const OuterAcc = VecSub(P2Acc, P1Acc);
            Acceleration = VecAdd(P0Acc, OuterAcc);

            bQuadraticFit = true;
        }
    }

    if (!bQuadraticFit)
    {
        // 0629134e..06291465.
        if (Dt01 == 0.0)
        {
            Velocity = MakeVector(0.0f, 0.0f, 0.0f);
        }
        else
        {
            Vector const Secant = VecSub(P0, P1);
            Velocity = ScaleByDouble(Secant, 1.0 / Dt01);    // FDIVR float 1.0
        }
        Acceleration = MakeVector(0.0f, 0.0f, 0.0f);
        // FCOMP [+0xc8] / TEST AH,0x41 / JNZ: assign only when strictly greater.
        if (ConvergeWindow > MaxCorrectiveTime)
        {
            ConvergeWindow = MaxCorrectiveTime;
        }
    }

    // 062910fc: FCOMP 0.0 / TEST AH,0x41 / JNZ end.
    if (ConvergeWindow > 0.0)
    {
        Vector const OldProjected = ProjectedPosition;
        BlendBasePosition = ProjectedPosition;
        double const Window = ConvergeWindow;

        // The primary quadratic at tau = Window (06291142..06291263):
        // ((0.5*A)*W)*W, each product narrowed, then + (W*V + Base).
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const HalfAccW = ScaleByDouble(HalfAcc, Window);
        Vector const AccTerm = ScaleByDouble(HalfAccW, Window);
        Vector const VelTerm = ScaleByDouble(Velocity, Window);
        Vector const Anchored = VecAdd(VelTerm, BasePosition);
        Vector const Target = VecAdd(AccTerm, Anchored);
        Vector const Offset = VecSub(Target, OldProjected);           // 062912a7

        if (bNearStraight)
        {
            // 062912bf..06291344: straight line to the target.
            Velocity2 = ScaleByDouble(Offset, 1.0 / Window);            // FDIVR float 1.0
            Acceleration2 = MakeVector(0.0f, 0.0f, 0.0f);
        }
        else
        {
            // Velocity2, 0629146a..062915b6. Q2(Window) == Target and
            // Q2(-SpanT) == Samples[1] with these signs.
            double const K1 = 1.0 / SpanT - 1.0 / Window;               // FSUBP
            double const SpanWindow = SpanT + Window;
            double const K2 = SpanT / (SpanWindow * Window);            // FDIVR ST0,ST2
            double const K3 = Window / (SpanT * SpanWindow);            // FDIVR [+0xb8]
            Vector const P1K3 = ScaleByDouble(P1, K3);
            Vector const OldK1 = ScaleByDouble(OldProjected, K1);
            Vector const TargetK2 = ScaleByDouble(Target, K2);
            Vector const Mixed = VecSub(OldK1, P1K3);                   // + FCHS'd P1K3
            Velocity2 = VecAdd(TargetK2, Mixed);

            // Acceleration2, 062915bb..062916c5. The x87 register allocation
            // leaves three intermediates unrounded: Old.x*L1 and P1.z*L3 never
            // reach memory, and the z difference is added while still on the
            // stack. Everything else is stored as a float first.
            double const L3 = 2.0 / (SpanT * (SpanT + Window));
            float const P1xL3 = (float)(P1.x * L3);
            float const P1yL3 = (float)(P1.y * L3);
            double const P1zL3 = P1.z * L3;
            double const L1 = 2.0 / (SpanT * Window);
            double const OldxL1 = OldProjected.x * L1;
            float const OldyL1 = (float)(OldProjected.y * L1);
            float const OldzL1 = (float)(OldProjected.z * L1);
            double const L2 = 2.0 / (SpanWindow * Window);
            float const TargetxL2 = (float)(Target.x * L2);
            float const TargetyL2 = (float)(Target.y * L2);
            float const TargetzL2 = (float)(Target.z * L2);
            float const DiffX = (float)(P1xL3 - OldxL1);
            float const DiffY = P1yL3 - OldyL1;
            double const DiffZ = P1zL3 - OldzL1;
            Acceleration2.x = DiffX + TargetxL2;
            Acceleration2.y = DiffY + TargetyL2;
            Acceleration2.z = (float)(DiffZ + TargetzL2);
        }
    }

    ElapsedTime = 0.0;                                                 // 062916cb
}

double CDeadReckonedVector::ConvergeTime()
{
    // Common.dll.c 0x31780.
    return ConvergeWindow;
}

// .text:062917c0 .. .text:06291bab, RET 0x8.
void CDeadReckonedVector::Doit(double Dt)
{
    double const Elapsed = Dt + ElapsedTime;          // FLD Dt / FADD [+0xc0]
    ElapsedTime = Elapsed;

    if (SampleCount == 0)
    {
        // 062917e5..06291813: FSTP float narrows the time before FUN_06288830.
        float const T = (float)(Elapsed);
        Vector const Drift = MakeVector(ConvergedVelocity.x * T, ConvergedVelocity.y * T,
                                        ConvergedVelocity.z * T);
        ProjectedPosition = VecAdd(Samples[0], Drift);
        return;
    }

    double const Elapsed2 = Elapsed * Elapsed;        // FSTP double 0629181d
    // FCOMP [+0xb8] / TEST AH,5 / JP: blending while Elapsed < ConvergeWindow.
    bool const bBlending = Elapsed < ConvergeWindow;
    // 06291876: FST float of Elapsed2 into the same slot; every later reader
    // takes this float, only TrackedPosition uses the double still on the stack.
    float const Elapsed2f = (float)(Elapsed2);

    // TrackedPosition, 06291834..0629191b.
    {
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const AccTerm = ScaleByDouble(HalfAcc, Elapsed2);
        Vector const VelTerm = ScaleByDouble(Velocity, Elapsed);
        Vector const Anchored = VecAdd(BasePosition, VelTerm);
        TrackedPosition = VecAdd(AccTerm, Anchored);
    }

    if (!bBlending)
    {
        // 0629192c..062919e8.
        ProjectedPosition = TrackedPosition;
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const AccTerm = MakeVector(HalfAcc.x * Elapsed2f, HalfAcc.y * Elapsed2f,
                                          HalfAcc.z * Elapsed2f);
        ConvergedVelocity = VecAdd(AccTerm, Velocity);
        return;
    }

    // 062919f2..06291ba4.
    Vector const HalfAcc2 = MakeVector(Acceleration2.x * 0.5f, Acceleration2.y * 0.5f,
                                       Acceleration2.z * 0.5f);
    Vector const AccTerm2 = MakeVector(HalfAcc2.x * Elapsed2f, HalfAcc2.y * Elapsed2f,
                                       HalfAcc2.z * Elapsed2f);
    Vector const VelTerm2 = ScaleByDouble(Velocity2, ElapsedTime);
    Vector const Anchored2 = VecAdd(VelTerm2, BlendBasePosition);
    ProjectedPosition = VecAdd(AccTerm2, Anchored2);
    ConvergedVelocity = VecAdd(AccTerm2, Velocity2);
}

Vector const& CDeadReckonedVector::GetProjectedPosition()
{
    // Common.dll.c 0x317b0.
    return ProjectedPosition;
}

Vector CDeadReckonedVector::ProjectVelocity(double Dt)
{
    // .text:062916e0 .. .text:0629176e (RET 0xC, hidden return pointer). Formula:
    // (0.5*Acceleration)*Dt*Dt + Velocity -- not integrated velocity/position.
    // Dt stays a double on the x87 stack throughout; it is never rounded to float,
    // and Dt^2 is never formed on its own. The three axes narrow differently:
    //   H = float(Accel * 0.5f)                  062916e3..06291713 (FSTP float x3)
    //   x: float(H.x*Dt) (06291723), then float(that*Dt) (0629173b)
    //   y: float(H.y*Dt) (0629172d), then float(that*Dt) (06291745)
    //   z: (Dt*H.z)*Dt kept on the stack (06291731, 06291749) -- never stored
    // then + Velocity (+0x78/+0x7c/+0x80) and one FSTP float per component.
    float const Hx = Acceleration.x * 0.5f;   // .rdata:0639c13c == 0.5f
    float const Hy = Acceleration.y * 0.5f;
    float const Hz = Acceleration.z * 0.5f;
    float const Ax = (float)(Hx * Dt);
    float const Ay = (float)(Hy * Dt);
    float const Px = (float)(Ax * Dt);
    float const Py = (float)(Ay * Dt);
    double const Pz = (Dt * Hz) * Dt;
    return MakeVector(Px + Velocity.x,
                      Py + Velocity.y,
                      (float)(Pz + Velocity.z));
}

void CDeadReckonedVector::SetMaxCorrectiveConvergeTime(double Time)
{
    MaxCorrectiveTime = Time;
}
