// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gate_tunnel.h"

#include "client_gameobj.h"  // FClientShipGameobj::GetObject
#include "common_cobject.h"  // CObject::beam_object (vt+0x7c)
#include "common_ini.h"
#include "engine_light.h"
#include "hud_target.h"      // HudTransformPoint (FUN_004e7090)
#include "space_camera.h"    // AsCamera
#include "win3d_immediate.h"
#include "win3d_render.h"    // Win3DApplyCamera (FUN_00422950)

#include "DALib.h"
#include "dacom.h" // FDUMP
#include "render/IRenderPipeline.h"
#include "render/shading.h" // ITextureLibrary2

#include <cmath>
#include <cstdlib>
#include <new>

namespace
{
    // .rdata 0x005da560.
    char const* const kGateTunnelCpp = "E:\\FL\\Scratch\\Source\\Client\\Space\\GateTunnel.cpp";

    // DAT_005da778 -- 1/32767 as a float: rand() onto [0, 1].
    float const kRandUnit = 1.0f / 32767.0f;
    // DAT_005caeec -- 1/32768 as a float: the light jitter's rand() onto [0, 1).
    float const kRandHalfUnit = 1.0f / 32768.0f;
    // DAT_005c7f68 -- 1/255 as a float, the loader's byte-to-colour scale.
    float const kInv255 = 1.0f / 255.0f;
    // DAT_005da774 -- 2*pi as a float.
    float const kTwoPi = 6.2831855f;
    // DAT_005da738 -- the path step BuildFrame looks ahead by (a double).
    double const kFrameEps = 5e-6;

    // DAT_00613254 / DAT_00613258 / DAT_0061325c: .data dwords 3, 3, 1 with no writer in the
    // listing. Update and RebuildColorOffsets add them (+1) into the scroll shift (8); Update
    // subtracts the first and third (+1) from the control-point count for the scroll limit.
    int const kScrollHead = 3;
    int const kScrollTail = 3;
    int const kScrollPad = 1;
    // DAT_00613260 -- the light direction's jitter weight (6.0).
    float const kLightDirWeight = 6.0f;
    // DAT_00613264 -- a .data byte 1 with no writer: whether Update moves the ship along the path.
    bool const bMoveAlongPath = true;
    // DAT_00613268 -- how far ahead of the ship Update samples the path for its heading.
    float const kShipLookAhead = 0.02f;
    // DAT_0061326c -- EmitLayer's per-strip growth of the path step.
    float const kStepGrowth = 1.1f;
    // DAT_00613270 -- EmitLayer's alpha ramp bias.
    float const kAlphaBias = 0.05f;

    // DAT_00674fbc -- Draw's call counter (written, never read).
    unsigned G_GateTunnelDrawCount = 0;
    // DAT_00674fb8 -- EmitLayer's ring-step counter (written, never read).
    unsigned G_GateTunnelRingVertCount = 0;
    // DAT_00674fb4 -- EmitLayer's 3DMathEngine, acquired (FUN_005b73e0) on first use at both
    // Rotate sites (0x00500852 / 0x005008a1).
    I3DMathEngine* G_GateTunnelMath = nullptr;

    // The draw-side face of the csys transform the tunnel keeps at +0x230 (same 0x30 layout).
    static_assert(sizeof(Transform) == sizeof(FTransform), "Transform and FTransform share 0x30");
    FTransform const* AsFTransform(Transform const* Xform)
    {
        return reinterpret_cast<FTransform const*>(Xform);
    }

    // The inlined draw `rand() * DAT_005da778 * (Maximum - Minimum) + Minimum`, stored as float
    // (0x005012fd.., 0x0050147f.., 0x00500b02.., 0x00500c58..).
    float RandomInRange(float Minimum, float Maximum)
    {
        return static_cast<float>(static_cast<double>(rand()) * kRandUnit *
                                      (static_cast<double>(Maximum) - Minimum) +
                                  Minimum);
    }

    // FUN_00410190 (thiscall on the vector) -- sqrt((x*x + y*y) + z*z), left on the FPU.
    double VectorLength(FVector const* V)
    {
        return sqrt(static_cast<double>(V->X) * V->X + static_cast<double>(V->Y) * V->Y +
                    static_cast<double>(V->Z) * V->Z);
    }

    // EmitLayer's inlined Brightness sample (0x005006b5.. / 0x00500750..): the curve at T as
    // FUN_00501810 computes it, except that the fraction is rounded to a float and evaluated
    // through FUN_00501c40.
    double SampleWithFloatFraction(FGateTunnelCurve const* Curve, float T)
    {
        int const Count = static_cast<int>(Curve->Coeffs.size());
        if (T == 1.0f)
            return Curve->Samples.data()[Count];
        double IntPart = 0.0;
        float const Frac =
            static_cast<float>(modf(static_cast<double>(Count) * static_cast<double>(T), &IntPart));
        return GateTunnelCoeffEval(Curve->Coeffs.data() + static_cast<int>(IntPart), Frac);
    }

    // The normalise the light code inlines twice (0x00501205.. / 0x004ffa29..): 1/sqrt of the
    // stored float components, each component multiplied by it and stored back.
    void NormalizeInPlace(Vector* V, double LengthSq)
    {
        double const InvLength = 1.0 / sqrt(LengthSq);
        V->x = static_cast<float>(InvLength * V->x);
        V->y = static_cast<float>(InvLength * V->y);
        V->z = static_cast<float>(InvLength * V->z);
    }
}

FGateTunnelList G_GateTunnels;

FGateTunnelList::~FGateTunnelList()
{
    // FUN_004fe0f0 (atexit, via LAB_004fe0e0): each non-null element deleted through slot 0
    // (flag 1), the end re-read after each; then the elements are erased and the storage freed
    // (0x004fe136..0x004fe169), which the base destructor does.
    for (size_t Index = 0; Index < size(); ++Index)
    {
        FGateTunnel* const Tunnel = (*this)[Index];
        if (Tunnel != nullptr)
            delete Tunnel;
    }
}

// ---------------------------------------------------------------------------------------------
// The spline

double GateTunnelCoeffEval(FGateTunnelCoeff const* Coeff, float T)
{
    // FUN_00501c40: FLD T, FMUL c3, FADD c2, FMUL T, FADD c1, FMUL T, FADD c0.
    return ((static_cast<double>(T) * Coeff->C3 + Coeff->C2) * T + Coeff->C1) * T + Coeff->C0;
}

void GateTunnelSolveSpline(float const* Y, int N, std::vector<FGateTunnelCoeff>* Out)
{
    // FUN_005018a0. 0x005018c8: Out emptied (FUN_00501f80 over [first, last)).
    Out->clear();

    // Three scratch vectors of N + 1 zeros (0x005018e5.., 0x00501943.., 0x005019af..): the
    // eliminated diagonal, the right-hand side and the tangents.
    std::vector<float> GammaStore(static_cast<size_t>(N + 1), 0.0f);
    std::vector<float> DeltaStore(static_cast<size_t>(N + 1), 0.0f);
    std::vector<float> TangentStore(static_cast<size_t>(N + 1), 0.0f);
    float* const Gamma = GammaStore.data();
    float* const Delta = DeltaStore.data();
    float* const D = TangentStore.data();

    // 0x00501a19..0x00501a58.
    Gamma[0] = 0.5f;
    for (int Index = 1; Index < N; ++Index)
        Gamma[Index] = static_cast<float>(1.0 / (4.0 - Gamma[Index - 1]));
    Gamma[N] = static_cast<float>(1.0 / (2.0 - Gamma[N - 1]));

    // 0x00501a5b..0x00501acc.
    Delta[0] = static_cast<float>((static_cast<double>(Y[1]) - Y[0]) * Gamma[0] * 3.0);
    for (int Index = 1; Index < N; ++Index)
    {
        Delta[Index] = static_cast<float>(
            ((static_cast<double>(Y[Index + 1]) - Y[Index - 1]) * 3.0 - Delta[Index - 1]) *
            Gamma[Index]);
    }
    Delta[N] = static_cast<float>(
        ((static_cast<double>(Y[N]) - Y[N - 1]) * 3.0 - Delta[N - 1]) * Gamma[N]);

    // 0x00501ad0..0x00501b1a: back substitution.
    D[N] = Delta[N];
    for (int Index = N - 1; Index >= 0; --Index)
        D[Index] = static_cast<float>(Delta[Index] - static_cast<double>(D[Index + 1]) * Gamma[Index]);

    // 0x00501b1c..0x00501c00: N segments.
    Out->resize(static_cast<size_t>(N));
    FGateTunnelCoeff* const Coeffs = Out->data();
    for (int Index = 0; Index < N; ++Index)
    {
        Coeffs[Index].C0 = Y[Index];
        Coeffs[Index].C1 = D[Index];
        Coeffs[Index].C2 = static_cast<float>(
            (static_cast<double>(Y[Index + 1]) - Y[Index]) * 3.0 -
            (static_cast<double>(D[Index]) + D[Index]) - D[Index + 1]);
        Coeffs[Index].C3 = static_cast<float>(
            (static_cast<double>(Y[Index]) - Y[Index + 1]) * 2.0 + D[Index] + D[Index + 1]);
    }
}

FGateTunnelCurve::FGateTunnelCurve()
    : Minimum(0.0f)
    , Maximum(1.0f)
{
    // FUN_00501720 builds the two empty vectors; the tunnel constructor then stores
    // [+0x20] = 0 and [+0x24] = 1.0 on each of the nine curves (0x004fee8a..0x004fef4d).
}

double FGateTunnelCurve::Sample(float T) const
{
    // FUN_00501810. T == 1 (TEST AH,0x44 / JP: equal falls through) returns the last control
    // value; anything else, NaN included, evaluates segment int(Count * T) at the fraction.
    int const Count = static_cast<int>(Coeffs.size());
    if (T == 1.0f)
        return Samples.data()[Count];
    double IntPart = 0.0;
    double const Frac = modf(static_cast<double>(Count) * static_cast<double>(T), &IntPart);
    FGateTunnelCoeff const* const Coeff = Coeffs.data() + static_cast<int>(IntPart);
    return ((Frac * Coeff->C3 + Coeff->C2) * Frac + Coeff->C1) * Frac + Coeff->C0;
}

void FGateTunnelCurve::Randomize(int Count, float NewMinimum, float NewMaximum, int ZeroHead,
                                 int ZeroTail)
{
    // FUN_005012e0: a scratch vector of Count zeros, the range, the draws, the zeroed ends,
    // then the control values resized to Count, copied, and splined.
    std::vector<float> ValueStore(static_cast<size_t>(Count), 0.0f);
    float* const Values = ValueStore.data();
    Minimum = NewMinimum;
    Maximum = NewMaximum;
    for (int Index = 0; Index < Count; ++Index)
        Values[Index] = RandomInRange(Minimum, Maximum);
    for (int Index = 0; Index < ZeroHead; ++Index)
        Values[Index] = 0.0f;
    for (int Index = Count - ZeroTail; Index < Count; ++Index)
        Values[Index] = 0.0f;

    Samples.resize(static_cast<size_t>(Count), 0.0f);
    float* const Dest = Samples.data();
    for (int Index = 0; Index < Count; ++Index)
        Dest[Index] = Values[Index];
    GateTunnelSolveSpline(Dest, Count - 1, &Coeffs);
}

double FGateTunnelCurve::Scroll(int Shift)
{
    // FUN_00501470.
    int const Count = static_cast<int>(Samples.size());
    if (Shift < 8 || Count < 8)
        return 0.0; // 0x00501521: FLD DAT_005c7474 (0.0)

    float* const Values = Samples.data();
    int Index = 0;
    for (; Index < Shift; ++Index)
        Values[Index] = Values[Count - Shift + Index];
    for (; Index < Count; ++Index)
        Values[Index] = RandomInRange(Minimum, Maximum);
    GateTunnelSolveSpline(Values, Count - 1, &Coeffs);
    // 0x00501505..0x00501519: integer division, negated, FILD.
    return static_cast<double>(-((Count - Shift) / (Count - 1)));
}

void FGateTunnelCurve::SetFromSamples(int Count, float const* Source, int Stride)
{
    // FUN_00501750.
    Samples.resize(static_cast<size_t>(Count), 0.0f);
    float* const Values = Samples.data();
    for (int Index = 0; Index < Count; ++Index)
        Values[Index] = Source[Index * Stride];
    GateTunnelSolveSpline(Values, Count - 1, &Coeffs);
}

FGateTunnelLayer::FGateTunnelLayer()
{
    // LAB_004ff090 is FTextureRef's constructor (record null, +4 = -1); the other fields are
    // set only by the loader's [layer] section (0x004fe719..0x004fe770).
}

// ---------------------------------------------------------------------------------------------
// GateTunnel

FGateTunnel::FGateTunnel()
{
    // FUN_004fee30.
    Id.clear();

    // 0x004fef50..0x004fef6b: +0x230 identity (FUN_004215b0) with a zero translation.
    Frame.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Frame.Translation.x = 0.0f;
    Frame.Translation.y = 0.0f;
    Frame.Translation.z = 0.0f;
    LightDirection.x = 0.0f;
    LightDirection.y = 1.0f;
    LightDirection.z = 0.0f;

    Id.Id = CreateID("default");
    MaxRadius = 50.0f;
    MinRadius = 50.0f;
    MinRgb.X = 0.0f;
    MinRgb.Y = 0.0f;
    MinRgb.Z = 0.0f;
    MaxRgb.X = 0.0f;
    MaxRgb.Y = 0.0f;
    MaxRgb.Z = 0.0f;
    bWriteDepthBuffer = 1;
    NumSplineControlPoints = 11;
    ZRange = 0.0f;
    YRange = 0.0f;
    XRange = 0.0f;
    FarRadiusFactor = 1.0f;
    MinSpeed = 3.0e-5f;         // 0x37fba882
    MaxSpeed = 0.06f;           // 0x3d75c28f
    TimeToMaxSpeed = 8.0f;
    NumTSteps = 200.0f;
    NumSSteps = 12.0f;
    MinRotation = -3.14159265f; // 0xc0490fdb
    MaxRotation = 3.14159265f;  // 0x40490fdb
    LayerCount = 0;
    State = 0;

    // 0x004ff052: slot 1 through the vtable just stored.
    EnterState0();
    Light = nullptr;
}

FGateTunnel::~FGateTunnel()
{
    // FUN_004ff0d0: each loaded layer's texture record goes back to the library (vt+0x38)
    // and is cleared.
    for (int Index = 0; Index < LayerCount; ++Index)
    {
        FGateTunnelLayer* const Layer = &Layers[Index];
        if (Layer->Texture.Texture != nullptr)
        {
            DALib::TextureLib->ReleaseTextureRecord(Layer->Texture.Texture);
            Layer->Texture.Texture = nullptr;
        }
    }

    // 0x004ff131..0x004ff154: the tunnel light, disabled (vt+8) when enabled, then released.
    if (Light != nullptr)
    {
        if ((Light->Flags & 0x08) != 0)
            GameLightVirtualDisable(Light); // vt+8
        GameLightRelease(Light);
        Light = nullptr;
    }
    // The nine curves and the two layers' FTextureRef destructors (FUN_00426de0) follow as
    // member destruction.
}

void FGateTunnel::EnterState0()
{
    // FUN_00501080.
    State = 0;
    SegmentIndex = 0;
    Phase = 0.0f;
    SpeedClock = 0.0f;
    Ship = nullptr;
}

void FGateTunnel::EnterState1()
{
    // LAB_005010b0: every curve rebuilt from scratch.
    SpeedClock = 0.0f;
    RebuildPath(0);
    Radius.Randomize(NumSplineControlPoints, MinRadius, MaxRadius, 0, 0);
    Rotation.Randomize(NumSplineControlPoints, MinRotation, MaxRotation, 3, 0);
    RebuildColorOffsets(0);
    Brightness.Randomize(NumSplineControlPoints, 0.5f, 1.0f, 0, 0);

    // 0x0050112f..0x00501153: a GameLight (operator new FUN_00537a90 + FUN_005374e0); the
    // pointer is used below without a test.
    void* const Memory = GameLightHeapAlloc(0x12c);
    Light = Memory != nullptr ? GameLightConstruct(static_cast<FGameLight*>(Memory)) : nullptr;

    // Directional, at the origin, white, range 500000.
    EngineLightSetType(Light, 3, false);
    float const Origin[3] = {0.0f, 0.0f, 0.0f};
    EngineLightSetPosition(Light, Origin);
    unsigned char const White[3] = {0xff, 0xff, 0xff};
    EngineLightSetColorBytesAlt(Light, White);
    EngineLightSetRangeRaw(Light, 500000.0f);

    // 0x005011ba..0x00501202: the first rand() is kept as a double for z; the second becomes x.
    double const RandZ = static_cast<double>(rand()) * kRandHalfUnit;
    LightDirection.y = -5.0f;
    LightDirection.x = static_cast<float>(static_cast<double>(rand()) * kRandHalfUnit * 2.0 - 1.0);
    LightDirection.z = static_cast<float>(RandZ * 2.0 - 1.0);
    // 0x00501205..0x0050123c: (y*y + z*z) + x*x.
    NormalizeInPlace(&LightDirection,
                     static_cast<double>(LightDirection.y) * LightDirection.y +
                         static_cast<double>(LightDirection.z) * LightDirection.z +
                         static_cast<double>(LightDirection.x) * LightDirection.x);
    EngineLightSetDirection(Light, &LightDirection.x);

    // 0x0050124a..0x00501280: enable (vt+4); a refusal releases it, an accepted light is
    // switched off again (vt+8) until Draw needs it.
    if (!GameLightEnable(Light))
    {
        GameLightRelease(Light);
        Light = nullptr;
        return;
    }
    if ((Light->Flags & 0x08) != 0)
        GameLightVirtualDisable(Light); // vt+8
}

void FGateTunnel::EnterState2()
{
    // LAB_00501290.
    SpeedClock = 0.0f;
}

void FGateTunnel::EnterState3()
{
    // FUN_005012a0.
    if (Light != nullptr)
    {
        if ((Light->Flags & 0x08) != 0)
            GameLightVirtualDisable(Light); // vt+8
        GameLightRelease(Light);
        Light = nullptr;
    }
}

void FGateTunnel::SetState(int NewState)
{
    // FUN_004ff360: the state is stored first; 0..3 (unsigned JA) dispatch slot 1 + state.
    State = NewState;
    switch (NewState)
    {
    case 0:
        EnterState0();
        break;
    case 1:
        EnterState1();
        break;
    case 2:
        EnterState2();
        break;
    case 3:
        EnterState3();
        break;
    default:
        break;
    }
}

void FGateTunnel::RebuildPath(int Shift)
{
    // FUN_00500a80.
    if (Shift > 0)
    {
        // 0x00500aaa..0x00500bef: FUN_00501470's body inlined on X and Y (the return value is
        // not computed); Z is not scrolled.
        PathX.Scroll(Shift);
        PathY.Scroll(Shift);
        return;
    }

    int const Count = NumSplineControlPoints;

    // 0x00500bf4..0x00500cb4: X drawn from [-XRange/2, XRange/2] into a scratch vector, the
    // first three values zeroed (unconditionally), then set through FUN_00501750.
    std::vector<float> XStore;
    XStore.resize(static_cast<size_t>(Count), 0.0f);
    float* const XValues = XStore.data();
    PathX.Minimum = static_cast<float>(static_cast<double>(XRange) * -0.5);
    PathX.Maximum = static_cast<float>(static_cast<double>(XRange) * 0.5);
    for (int Index = 0; Index < Count; ++Index)
        XValues[Index] = RandomInRange(PathX.Minimum, PathX.Maximum);
    for (int Index = 0; Index < 3; ++Index)
        XValues[Index] = 0.0f;
    PathX.SetFromSamples(Count, XValues, 1);

    // 0x00500cb9..0x00500cea.
    PathY.Randomize(NumSplineControlPoints, static_cast<float>(static_cast<double>(YRange) * -0.5),
                    static_cast<float>(static_cast<double>(YRange) * 0.5), 3, 0);

    // 0x00500cef..0x00500daf: Z falls away linearly, -(i * ZRange / Count) - 5.
    int const ZCount = NumSplineControlPoints;
    std::vector<float> ZStore;
    ZStore.resize(static_cast<size_t>(ZCount), 0.0f);
    float* const ZValues = ZStore.data();
    for (int Index = 0; Index < NumSplineControlPoints; ++Index)
    {
        ZValues[Index] = static_cast<float>(
            -(static_cast<double>(Index) * ZRange / NumSplineControlPoints) - 5.0);
    }
    PathZ.SetFromSamples(NumSplineControlPoints, ZValues, 1);
}

void FGateTunnel::RebuildColorOffsets(int Shift)
{
    // FUN_00500de0.
    if (Shift > 0)
    {
        // 0x00500df2..0x0050100c: FUN_00501470's body inlined three times with the constant
        // shift kScrollHead + kScrollPad + kScrollTail + 1, not the argument.
        int const ScrollShift = kScrollHead + kScrollPad + kScrollTail + 1;
        ColorOffsetR.Scroll(ScrollShift);
        ColorOffsetG.Scroll(ScrollShift);
        ColorOffsetB.Scroll(ScrollShift);
        return;
    }
    // 0x00501016..0x00501074.
    ColorOffsetR.Randomize(NumSplineControlPoints, MinRgb.X, MaxRgb.X, 0, 0);
    ColorOffsetG.Randomize(NumSplineControlPoints, MinRgb.Y, MaxRgb.Y, 0, 0);
    ColorOffsetB.Randomize(NumSplineControlPoints, MinRgb.Z, MaxRgb.Z, 0, 0);
}

void FGateTunnel::Update(float Dt)
{
    // FUN_004ff3c0.
    if (State == 0)
        return;

    if (State == 2 || State == 3)
    {
        // 0x004ff3e4..0x004ff40b: the speed ramp, capped at 1 (NaN kept).
        // FST keeps the unrounded clock on the FPU for the divide.
        double const Clock = static_cast<double>(Dt) + SpeedClock;
        SpeedClock = static_cast<float>(Clock);
        double Ratio = Clock / TimeToMaxSpeed;
        if (1.0 < Ratio)
            Ratio = 1.0;

        if (bMoveAlongPath)
        {
            // 0x004ff420..0x004ff453: Phase advances; the segment index truncates the unrounded
            // phase times (Count - 1).
            double const NewPhase =
                (Ratio * (static_cast<double>(MaxSpeed) - MinSpeed) + MinSpeed) * Dt + Phase;
            Phase = static_cast<float>(NewPhase);
            SegmentIndex =
                static_cast<int>(static_cast<double>(NumSplineControlPoints - 1) * NewPhase);
        }

        // 0x004ff45d..0x004ff5c3: near the end of the path everything scrolls by
        // kScrollTail + kScrollPad + kScrollHead + 1 and the ship moves back three segments.
        int const Limit = NumSplineControlPoints - kScrollPad - kScrollHead - 1;
        if (SegmentIndex >= Limit)
        {
            int const ScrollShift = kScrollTail + kScrollPad + kScrollHead + 1;
            RebuildPath(ScrollShift);
            if (ScrollShift > 0)
                Rotation.Scroll(ScrollShift);
            else
                Rotation.Randomize(NumSplineControlPoints, MinRotation, MaxRotation, 3, 0);
            if (ScrollShift > 0)
                Radius.Scroll(ScrollShift);
            else
                Radius.Randomize(NumSplineControlPoints, MinRadius, MaxRadius, 0, 0);
            RebuildColorOffsets(ScrollShift);
            if (ScrollShift > 0)
                Brightness.Scroll(ScrollShift);
            else
                Brightness.Randomize(NumSplineControlPoints, 0.5f, 1.0f, 0, 0);

            double const Segments = static_cast<double>(NumSplineControlPoints - 1);
            SegmentIndex = 3;
            Phase = static_cast<float>(Phase - static_cast<double>(Limit) / Segments +
                                       3.0 / Segments);
        }

        if (bMoveAlongPath)
        {
            FTransform const* const World = AsFTransform(&Frame);

            // 0x004ff5d2..0x004ff662: the ship's point on the path (samples Z, Y, X).
            float const Ahead = static_cast<float>(static_cast<double>(kShipLookAhead) + Phase);
            float const Here = Phase;
            FVector Local;
            Local.Z = static_cast<float>(PathZ.Sample(Here));
            Local.Y = static_cast<float>(PathY.Sample(Here));
            Local.X = static_cast<float>(PathX.Sample(Here));
            FVector Point;
            HudTransformPoint(World, &Point, &Local);
            ShipPosition.x = Point.X;
            ShipPosition.y = Point.Y;
            ShipPosition.z = Point.Z;

            // 0x004ff665..0x004ff6a2: the point kShipLookAhead further on.
            Local.Z = static_cast<float>(PathZ.Sample(Ahead));
            Local.Y = static_cast<float>(PathY.Sample(Ahead));
            Local.X = static_cast<float>(PathX.Sample(Ahead));
            FVector AheadPoint;
            HudTransformPoint(World, &AheadPoint, &Local);

            // 0x004ff6a7..0x004ff6f6: the heading (FUN_00423b60, FUN_00410190), normalised.
            FVector Dir;
            Dir.X = AheadPoint.X - ShipPosition.x;
            Dir.Y = AheadPoint.Y - ShipPosition.y;
            Dir.Z = AheadPoint.Z - ShipPosition.z;
            double const InvDirLength = 1.0 / VectorLength(&Dir);
            Dir.X = static_cast<float>(Dir.X * InvDirLength);
            Dir.Y = static_cast<float>(Dir.Y * InvDirLength);
            Dir.Z = static_cast<float>(Dir.Z * InvDirLength);

            // FUN_00411350: the back axis N = -Dir.
            FVector Back;
            Back.X = -Dir.X;
            Back.Y = -Dir.Y;
            Back.Z = -Dir.Z;

            // 0x004ff6fd..0x004ff747: N x (0, 1, 0), negated -- the right axis.
            FVector Cross;
            Cross.X = static_cast<float>(static_cast<double>(Back.Y) * 0.0 - Back.Z);
            Cross.Y = static_cast<float>(static_cast<double>(Back.Z) * 0.0 -
                                         static_cast<double>(Back.X) * 0.0);
            Cross.Z = static_cast<float>(Back.X - static_cast<double>(Back.Y) * 0.0);
            FVector Right;
            Right.X = -Cross.X;
            Right.Y = -Cross.Y;
            Right.Z = -Cross.Z;

            // 0x004ff74c..0x004ff794: up = N x R.
            FVector Up;
            Up.X = static_cast<float>(static_cast<double>(Right.Z) * Back.Y -
                                      static_cast<double>(Right.Y) * Back.Z);
            Up.Y = static_cast<float>(static_cast<double>(Back.Z) * Right.X -
                                      static_cast<double>(Right.Z) * Back.X);
            Up.Z = static_cast<float>(static_cast<double>(Right.Y) * Back.X -
                                      static_cast<double>(Back.Y) * Right.X);

            // 0x004ff799..0x004ff85a: the columns of +0x280, each normalised.
            double const InvRight = 1.0 / VectorLength(&Right);
            ShipOrientation.E[0][0] = static_cast<float>(Right.X * InvRight);
            ShipOrientation.E[1][0] = static_cast<float>(Right.Y * InvRight);
            ShipOrientation.E[2][0] = static_cast<float>(Right.Z * InvRight);
            double const InvUp = 1.0 / VectorLength(&Up);
            ShipOrientation.E[0][1] = static_cast<float>(Up.X * InvUp);
            ShipOrientation.E[1][1] = static_cast<float>(Up.Y * InvUp);
            ShipOrientation.E[2][1] = static_cast<float>(Up.Z * InvUp);
            double const InvBack = 1.0 / VectorLength(&Back);
            ShipOrientation.E[0][2] = static_cast<float>(Back.X * InvBack);
            ShipOrientation.E[1][2] = static_cast<float>(Back.Y * InvBack);
            ShipOrientation.E[2][2] = static_cast<float>(Back.Z * InvBack);

            // 0x004ff85d..0x004ff890: the ship's CObject narrowed by 0x503 (null otherwise, and
            // then called anyway) is beamed there: vt+0x7c beam_object(pos, orient, false).
            if (Ship != nullptr)
            {
                CObject* Obj = Ship->GetObject();
                if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
                    Obj = nullptr;
                Obj->beam_object(ShipPosition, ShipOrientation, false);
            }
        }
    }

    // 0x004ff893..0x004ffa6a: an enabled tunnel light flickers in colour and direction.
    if (Light != nullptr && (Light->Flags & 0x08) != 0)
    {
        float Colour[3] = {0.8f, 0.8f, 1.0f};
        float const RandA = static_cast<float>(static_cast<double>(rand()) * kRandHalfUnit);
        double const Dim =
            1.0 - static_cast<double>(rand()) * RandA * static_cast<double>(kRandHalfUnit);
        Colour[0] = static_cast<float>(Colour[0] * Dim);
        Colour[1] = static_cast<float>(Colour[1] * Dim);
        Colour[2] = static_cast<float>(Colour[2] * Dim);

        // FUN_00411d60 then ftol and the [0, 255] clamp (inline for blue, FUN_004104c0 for green
        // and red), packed (FUN_004a1ad0) with alpha 0xff.
        float Scaled[3];
        ScaleVec3(Scaled, Colour, 255.0f);
        int Blue = static_cast<int>(Scaled[2]);
        if (Blue < 0)
            Blue = 0;
        else if (Blue > 0xff)
            Blue = 0xff;
        int Green = static_cast<int>(Scaled[1]);
        if (Green < 0)
            Green = 0;
        else if (Green > 0xff)
            Green = 0xff;
        int Red = static_cast<int>(Scaled[0]);
        if (Red < 0)
            Red = 0;
        else if (Red > 0xff)
            Red = 0xff;
        unsigned char const Packed[4] = {static_cast<unsigned char>(Red),
                                         static_cast<unsigned char>(Green),
                                         static_cast<unsigned char>(Blue), 0xff};
        EngineLightSetColorBytes(Light, Packed);

        // 0x004ff993..0x004ffa26: direction weighted by kLightDirWeight plus three fresh
        // [-1, 1) draws (the first goes to z, rounded to a float, the second to y, the third
        // to x), normalised as (x*x + y*y) + z*z.
        LightDirection.x = static_cast<float>(static_cast<double>(kLightDirWeight) * LightDirection.x);
        LightDirection.y = static_cast<float>(static_cast<double>(kLightDirWeight) * LightDirection.y);
        LightDirection.z = static_cast<float>(static_cast<double>(kLightDirWeight) * LightDirection.z);
        double const RandZ = static_cast<double>(rand()) * kRandHalfUnit;
        double const RandY = static_cast<double>(rand()) * kRandHalfUnit;
        double const JitterX = static_cast<double>(rand()) * kRandHalfUnit * 2.0 - 1.0;
        double const JitterY = RandY * 2.0 - 1.0;
        float const JitterZ = static_cast<float>(RandZ * 2.0 - 1.0);
        LightDirection.x = static_cast<float>(JitterX + LightDirection.x);
        LightDirection.y = static_cast<float>(JitterY + LightDirection.y);
        LightDirection.z = static_cast<float>(static_cast<double>(JitterZ) + LightDirection.z);
        NormalizeInPlace(&LightDirection,
                         static_cast<double>(LightDirection.x) * LightDirection.x +
                             static_cast<double>(LightDirection.y) * LightDirection.y +
                             static_cast<double>(LightDirection.z) * LightDirection.z);
        EngineLightSetDirection(Light, &LightDirection.x);
    }

    // 0x004ffa6f..0x004ffaa3: every layer's texture scrolls.
    for (int Index = 0; Index < LayerCount; ++Index)
    {
        FGateTunnelLayer* const Layer = &Layers[Index];
        Layer->UOffset = static_cast<float>(static_cast<double>(Dt) * Layer->Du + Layer->UOffset);
        Layer->VOffset = static_cast<float>(static_cast<double>(Dt) * Layer->Dv + Layer->VOffset);
    }
}

void FGateTunnel::Draw(FSpaceCamera* Camera)
{
    // FUN_004ffab0.
    ++G_GateTunnelDrawCount;
    if (State == 0)
        return;

    // 0x004ffac7..0x004ffadc: a light that is off is switched on (vt+4) for the draw.
    if (Light != nullptr && (Light->Flags & 0x08) == 0)
        GameLightEnable(Light);

    Win3DApplyCamera(AsCamera(Camera));
    Win3DDisable(kWin3DCullFace);
    Win3DSetDepthWrite(bWriteDepthBuffer);
    Win3DEnable(kWin3DBlend);
    IRenderPipeline* const Pipe = DALib::Pipeline;
    Pipe->SetTextureStageState(0, 0xd, 1);
    Pipe->SetTextureStageState(0, 0xe, 1);

    // 0x004ffb38..0x004ffb7d: each layer from 0.1 behind to 0.01 ahead of the ship, with the
    // step count negated around the call (the strips run backwards along the path).
    for (int Index = 0; Index < LayerCount; ++Index)
    {
        NumTSteps = -NumTSteps;
        EmitLayer(0.1f, -0.01f, &Layers[Index], Index);
        NumTSteps = -NumTSteps;
    }

    Win3DEnable(kWin3DCullFace);
    Win3DSetDepthWrite(1);
    Win3DDisable(kWin3DBlend);
}

void FGateTunnel::BuildFrame(FTransform* Out, float T) const
{
    // FUN_004ffbb0. The look-ahead parameter steps back when stepping forward passes 1 (the
    // test is on the unrounded sum: TEST AH,0x41 / JNZ keeps it for <= 1 and NaN).
    double const AheadSum = static_cast<double>(T) + kFrameEps;
    float Ahead = static_cast<float>(AheadSum);
    if (AheadSum > 1.0)
        Ahead = static_cast<float>(static_cast<double>(T) - kFrameEps);

    FTransform const* const World = AsFTransform(&Frame);

    // 0x004ffbeb..0x004ffc4c: the point at T (samples Z, Y, X).
    FVector Local;
    Local.Z = static_cast<float>(PathZ.Sample(T));
    Local.Y = static_cast<float>(PathY.Sample(T));
    Local.X = static_cast<float>(PathX.Sample(T));
    FVector Point;
    HudTransformPoint(World, &Point, &Local);

    // 0x004ffc51..0x004ffc9a: the point at the look-ahead.
    Local.Z = static_cast<float>(PathZ.Sample(Ahead));
    Local.Y = static_cast<float>(PathY.Sample(Ahead));
    Local.X = static_cast<float>(PathX.Sample(Ahead));
    FVector AheadPoint;
    HudTransformPoint(World, &AheadPoint, &Local);

    // 0x004ffc9f..0x004ffcf4: forward = -(Ahead - Point) / |Ahead - Point|.
    FVector Delta;
    Delta.X = AheadPoint.X - Point.X;
    Delta.Y = AheadPoint.Y - Point.Y;
    Delta.Z = AheadPoint.Z - Point.Z;
    double const InvLength = 1.0 / VectorLength(&Delta);
    FVector Forward;
    Forward.X = static_cast<float>(-(Delta.X * InvLength));
    Forward.Y = static_cast<float>(-(Delta.Y * InvLength));
    Forward.Z = static_cast<float>(-(Delta.Z * InvLength));

    // 0x004ffd02..0x004ffd67: F x (0, 1, 0), negated -- the left axis (not normalised).
    FVector Cross;
    Cross.X = static_cast<float>(static_cast<double>(Forward.Y) * 0.0 - Forward.Z);
    Cross.Y = static_cast<float>(static_cast<double>(Forward.Z) * 0.0 -
                                 static_cast<double>(Forward.X) * 0.0);
    Cross.Z = static_cast<float>(Forward.X - static_cast<double>(Forward.Y) * 0.0);
    FVector Left;
    Left.X = -Cross.X;
    Left.Y = -Cross.Y;
    Left.Z = -Cross.Z;

    // 0x004ffd6c..0x004ffdb4: up = F x L.
    FVector Up;
    Up.X = static_cast<float>(static_cast<double>(Left.Z) * Forward.Y -
                              static_cast<double>(Left.Y) * Forward.Z);
    Up.Y = static_cast<float>(static_cast<double>(Forward.Z) * Left.X -
                              static_cast<double>(Left.Z) * Forward.X);
    Up.Z = static_cast<float>(static_cast<double>(Left.Y) * Forward.X -
                              static_cast<double>(Forward.Y) * Left.X);

    // 0x004ffcf9 (identity) and 0x004ffdb9..0x004ffe34: columns (left, up, forward), the point
    // as translation, copied out whole.
    Out->Rotation.D[0][0] = Left.X;
    Out->Rotation.D[0][1] = Up.X;
    Out->Rotation.D[0][2] = Forward.X;
    Out->Rotation.D[1][0] = Left.Y;
    Out->Rotation.D[1][1] = Up.Y;
    Out->Rotation.D[1][2] = Forward.Y;
    Out->Rotation.D[2][0] = Left.Z;
    Out->Rotation.D[2][1] = Up.Z;
    Out->Rotation.D[2][2] = Forward.Z;
    Out->Translation = Point;
}

void FGateTunnel::EmitLayer(float T0, float T1, FGateTunnelLayer* Layer, int LayerIndex)
{
    // FUN_004ffe50.
    float Step = static_cast<float>(1.0 / NumTSteps);
    float const RingStep = static_cast<float>(1.0 / NumSSteps);

    // 0x004ffe89..0x004ffe9e / 0x00500a47: layer 0 blends by alpha, layer 1 additively.
    if (LayerIndex == 0)
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    else if (LayerIndex == 1)
        Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendInvSrcAlpha);
    else
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kGateTunnelCpp, 0x21e,
              "Only two render layers in the jumpgate supported");
        return;
    }

    // 0x004ffea3..0x004ffec0: the layer texture's frame 0 (ITextureLibrary2 vt+0x58).
    unsigned long TextureFrame[5];
    DALib::TextureLib->GetTextureFrame(Layer->Texture.Texture, 0, TextureFrame);

    // 0x004ffec3..0x004fff75: the window [T0, T1] around the ship, clamped to [0, 1] (NaN
    // kept); the span is measured against the unrounded start.
    double StartSum = static_cast<double>(T0) + Phase;
    if (1.0 < StartSum)
        StartSum = 1.0;
    else if (0.0 > StartSum)
        StartSum = 0.0;
    float const Start = static_cast<float>(StartSum);
    double EndSum = static_cast<double>(T1) + Phase;
    if (1.0 < EndSum)
        EndSum = 1.0;
    else if (0.0 > EndSum)
        EndSum = 0.0;
    float const End = static_cast<float>(EndSum);
    float const Span = static_cast<float>(fabs(static_cast<double>(End) - StartSum));

    float T = Start;
    for (;;)
    {
        // 0x004fff80..0x004fffa9: forward steps run while T < End, backward ones while T > End.
        if (Step > 0.0f)
        {
            if (!(T < End))
                break;
        }
        else if (!(T > End))
            break;

        // 0x004fffaf..0x0050001d: the step grows by kStepGrowth; the next T is clamped to 0 below
        // and to End in the step's direction.
        double const GrownStep = static_cast<double>(kStepGrowth) * Step;
        Step = static_cast<float>(GrownStep);
        float const Unclamped = static_cast<float>(static_cast<double>(T) + GrownStep);
        float Next = Unclamped;
        if (0.0f > Unclamped)
            Next = 0.0f;
        if (GrownStep > 0.0)
        {
            if (End < Next)
                Next = End;
        }
        else if (End > Next)
            Next = End;

        // 0x0050001d..0x005000cd: how far each end is from the ship, as a [0, 1] fraction.
        double NearFraction = fabs(static_cast<double>(T) - Phase) / Span;
        if (0.0 > NearFraction)
            NearFraction = 0.0;
        else if (NearFraction > 1.0)
            NearFraction = 1.0;
        float const Fraction0 = static_cast<float>(NearFraction);
        double FarFraction = fabs(static_cast<double>(Next) - Phase) / Span;
        if (0.0 > FarFraction)
            FarFraction = 0.0;
        else if (FarFraction > 1.0)
            FarFraction = 1.0;
        float const Fraction1 = static_cast<float>(FarFraction);

        // 0x005000c7..0x00500208: the ring radius, blended toward FarRadiusFactor with distance.
        double const Radius0 = Radius.Sample(T) * Layer->RadiusFactor;
        float const Size0 =
            static_cast<float>((Radius0 * FarRadiusFactor - Radius0) * Fraction0 + Radius0);
        double const Radius1 = Radius.Sample(Next) * Layer->RadiusFactor;
        float const Size1 =
            static_cast<float>((Radius1 * FarRadiusFactor - Radius1) * Fraction1 + Radius1);

        // 0x0050020e / 0x00500222.
        FTransform Frame0;
        BuildFrame(&Frame0, T);
        FTransform Frame1;
        BuildFrame(&Frame1, Next);

        // 0x00500227..0x005002e3: alpha from near to far (swapped for a backward step), cubed.
        float FromAlpha = Layer->NearAlpha;
        float ToAlpha = Layer->FarAlpha;
        if (Step < 0.0f)
        {
            float const Swap = FromAlpha;
            FromAlpha = ToAlpha;
            ToAlpha = Swap;
        }
        double Progress0 = fabs(static_cast<double>(T) - Start) / Span + kAlphaBias;
        if (!(Progress0 < 1.0))
            Progress0 = 1.0;
        double Progress1 = fabs(static_cast<double>(Next) - Start) / Span + kAlphaBias;
        if (!(Progress1 < 1.0))
            Progress1 = 1.0;
        double const AlphaRange = static_cast<double>(ToAlpha) - FromAlpha;
        float const AlphaRangeStored = static_cast<float>(AlphaRange);
        float const AlphaBase0 = static_cast<float>(AlphaRange * Progress0 + FromAlpha);
        float const Alpha0 =
            static_cast<float>(static_cast<double>(AlphaBase0) * AlphaBase0 * AlphaBase0);
        double const AlphaBase1 = Progress1 * AlphaRangeStored + FromAlpha;
        float const Alpha1 = static_cast<float>(AlphaBase1 * AlphaBase1 * AlphaBase1);

        // 0x005002e5..0x0050069e: the colour offsets. Blue reads the red curve at +0x1b8 (the
        // blue one at +0x208 is built but never sampled); the last sample stays on the FPU.
        float const OffsetR0 = static_cast<float>(ColorOffsetR.Sample(T));
        float const OffsetR1 = static_cast<float>(ColorOffsetR.Sample(Next));
        float const OffsetG0 = static_cast<float>(ColorOffsetG.Sample(T));
        float const OffsetG1 = static_cast<float>(ColorOffsetG.Sample(Next));
        float const OffsetB0 = static_cast<float>(ColorOffsetR.Sample(T));
        double const OffsetB1 = ColorOffsetR.Sample(Next);
        float Colour0[3];
        float Colour1[3];
        Colour0[0] = static_cast<float>(static_cast<double>(Layer->Color.X) + OffsetR0);
        Colour0[1] = static_cast<float>(static_cast<double>(Layer->Color.Y) + OffsetG0);
        Colour0[2] = static_cast<float>(static_cast<double>(Layer->Color.Z) + OffsetB0);
        Colour1[0] = static_cast<float>(static_cast<double>(Layer->Color.X) + OffsetR1);
        Colour1[1] = static_cast<float>(static_cast<double>(Layer->Color.Y) + OffsetG1);
        Colour1[2] = static_cast<float>(static_cast<double>(Layer->Color.Z) + OffsetB1);

        // 0x005006a4..0x005007d6: scaled by the brightness curve.
        double const Brightness0 = SampleWithFloatFraction(&Brightness, T);
        Colour0[0] = static_cast<float>(Colour0[0] * Brightness0);
        Colour0[1] = static_cast<float>(Colour0[1] * Brightness0);
        Colour0[2] = static_cast<float>(Brightness0 * Colour0[2]);
        double const Brightness1 = SampleWithFloatFraction(&Brightness, Next);
        Colour1[0] = static_cast<float>(Colour1[0] * Brightness1);
        Colour1[1] = static_cast<float>(Colour1[1] * Brightness1);
        Colour1[2] = static_cast<float>(Brightness1 * Colour1[2]);

        Win3DBindTexture(reinterpret_cast<void*>(TextureFrame[0]));
        Win3DBegin(kWin3DTriangleStrip);

        // 0x005007ed..0x00500a2f: one strip around the ring, while the (unrounded) angle stays
        // <= 1 (TEST AH,0x41 / JNP: below or equal loops, above or NaN stops).
        float Angle = 0.0f;
        for (;;)
        {
            double const Radians = static_cast<double>(Angle) * kTwoPi;
            double const CosA = cos(Radians);
            double const SinA = sin(Radians);
            FVector Ring0;
            Ring0.X = static_cast<float>(Size0 * CosA);
            Ring0.Y = static_cast<float>(Size0 * SinA);
            Ring0.Z = 0.0f;
            FVector Ring1;
            Ring1.X = static_cast<float>(Size1 * CosA);
            Ring1.Y = static_cast<float>(Size1 * SinA);
            Ring1.Z = 0.0f;

            // I3DMathEngine vt+0x30 Rotate, each site acquiring the engine when null.
            if (G_GateTunnelMath == nullptr)
                G_GateTunnelMath = Acquire3DMathEngine();
            FVector Rotated0;
            G_GateTunnelMath->Rotate(Rotated0, Frame0, Ring0);
            if (G_GateTunnelMath == nullptr)
                G_GateTunnelMath = Acquire3DMathEngine();
            FVector Rotated1;
            G_GateTunnelMath->Rotate(Rotated1, Frame1, Ring1);

            Win3DTexCoord2f(static_cast<float>(static_cast<double>(Angle) + Layer->UOffset),
                            static_cast<float>(static_cast<double>(T) * Layer->VScale +
                                               Layer->VOffset));
            Win3DColor4f(Colour0[0], Colour0[1], Colour0[2], Alpha0);
            Win3DVertex3f(static_cast<float>(static_cast<double>(Frame0.Translation.X) + Rotated0.X),
                          static_cast<float>(static_cast<double>(Frame0.Translation.Y) + Rotated0.Y),
                          static_cast<float>(static_cast<double>(Frame0.Translation.Z) + Rotated0.Z));

            Win3DTexCoord2f(static_cast<float>(static_cast<double>(Angle) + Layer->UOffset),
                            static_cast<float>(static_cast<double>(Next) * Layer->VScale +
                                               Layer->VOffset));
            Win3DColor4f(Colour1[0], Colour1[1], Colour1[2], Alpha1);
            Win3DVertex3f(static_cast<float>(static_cast<double>(Frame1.Translation.X) + Rotated1.X),
                          static_cast<float>(static_cast<double>(Frame1.Translation.Y) + Rotated1.Y),
                          static_cast<float>(static_cast<double>(Frame1.Translation.Z) + Rotated1.Z));

            double const AngleSum = static_cast<double>(Angle) + RingStep;
            ++G_GateTunnelRingVertCount;
            Angle = static_cast<float>(AngleSum);
            if (!(AngleSum <= 1.0))
                break;
        }
        Win3DEnd();

        // 0x00500a3a: the next strip starts at the unclamped step end.
        T = Unclamped;
    }
}

// ---------------------------------------------------------------------------------------------
// The tunnel list

FGateTunnel* GateTunnelFind(ID_String const* Id)
{
    // FUN_004fedd0: walk begin (DAT_00674f9c) .. end (DAT_00674fa0), comparing each tunnel's +4
    // with the id's dword (read once, 0x004fede6). No test of Id or of an element.
    unsigned const Wanted = Id->Id;
    for (size_t Index = 0; Index < G_GateTunnels.size(); ++Index)
    {
        FGateTunnel* const Tunnel = G_GateTunnels[Index];
        if (Tunnel->Id.Id == Wanted)
            return Tunnel;
    }
    // 0x004fedf6..0x004fee1c: FDUMP 0x100002 with the id's string, line 0xfc; null.
    FDUMP(0x100002, "%s(%d) : *** WARNING: GateTunnel %s not found", kGateTunnelCpp, 0xfc,
          Id->get_string());
    return nullptr;
}

void GateTunnelSetShip(FGateTunnel* Tunnel, FClientShipGameobj* Ship)
{
    // FUN_004ff3b0.
    Tunnel->Ship = Ship;
}

void GateTunnelsClear()
{
    // FUN_004fe180: every non-null entry deleted through slot 0 (scalar deleting, flag 1), the
    // bounds re-read after each; then end = begin.
    for (size_t Index = 0; Index < G_GateTunnels.size(); ++Index)
    {
        FGateTunnel* const Tunnel = G_GateTunnels[Index];
        if (Tunnel != nullptr)
            delete Tunnel;
    }
    G_GateTunnels.clear();
}

void GateTunnelIniLoad(char const* Path)
{
    // FUN_004fe1c0.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;

    FGateTunnel* Tunnel = nullptr;
    while (Reader.read_header())
    {
        if (Reader.is_header("gate_tunnel"))
        {
            // 0x004fe249..0x004fe27d: operator new(0x2b8) + FUN_004fee30, null on failure; the
            // keys below write through it unchecked.
            Tunnel = new (std::nothrow) FGateTunnel();
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                    Tunnel->Id.Id = CreateID(Reader.get_value_string());
                else if (Reader.is_value("write_depth_buffer"))
                {
                    // FCOMP 0.0 / TEST AH,0x44 / JNP: only an exact 0 clears it (NaN sets it).
                    Tunnel->bWriteDepthBuffer = Reader.get_value_float(0) != 0.0f ? 1 : 0;
                }
                else if (Reader.is_value("num_spline_control_points"))
                    Tunnel->NumSplineControlPoints = static_cast<int>(Reader.get_value_float(0));
                else if (Reader.is_value("x_range"))
                    Tunnel->XRange = Reader.get_value_float(0);
                else if (Reader.is_value("y_range"))
                    Tunnel->YRange = Reader.get_value_float(0);
                else if (Reader.is_value("z_range"))
                    Tunnel->ZRange = Reader.get_value_float(0);
                else if (Reader.is_value("min_radius"))
                    Tunnel->MinRadius = Reader.get_value_float(0);
                else if (Reader.is_value("max_radius"))
                    Tunnel->MaxRadius = Reader.get_value_float(0);
                else if (Reader.is_value("far_radius_factor"))
                    Tunnel->FarRadiusFactor = Reader.get_value_float(0);
                else if (Reader.is_value("min_speed"))
                    Tunnel->MinSpeed = Reader.get_value_float(0);
                else if (Reader.is_value("max_speed"))
                    Tunnel->MaxSpeed = Reader.get_value_float(0);
                else if (Reader.is_value("time_to_max_speed"))
                    Tunnel->TimeToMaxSpeed = Reader.get_value_float(0);
                else if (Reader.is_value("num_t_steps"))
                    Tunnel->NumTSteps = Reader.get_value_float(0);
                else if (Reader.is_value("num_s_steps"))
                    Tunnel->NumSSteps = Reader.get_value_float(0);
                else if (Reader.is_value("min_rotation"))
                    Tunnel->MinRotation = Reader.get_value_float(0);
                else if (Reader.is_value("max_rotation"))
                    Tunnel->MaxRotation = Reader.get_value_float(0);
                else if (Reader.is_value("min_rgb"))
                {
                    // Three byte values, read index 2, 1, 0, each times 1/255.
                    float const Blue = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(2)) * kInv255);
                    float const Green = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(1)) * kInv255);
                    float const Red = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(0)) * kInv255);
                    Tunnel->MinRgb.X = Red;
                    Tunnel->MinRgb.Y = Green;
                    Tunnel->MinRgb.Z = Blue;
                }
                else if (Reader.is_value("max_rgb"))
                {
                    float const Blue = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(2)) * kInv255);
                    float const Green = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(1)) * kInv255);
                    float const Red = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(0)) * kInv255);
                    Tunnel->MaxRgb.X = Red;
                    Tunnel->MaxRgb.Y = Green;
                    Tunnel->MaxRgb.Z = Blue;
                }
                // Any other key is ignored.
            }

            // 0x004fe644..0x004fe6c7: with control points, every curve is built now.
            if (Tunnel->NumSplineControlPoints > 0)
            {
                Tunnel->RebuildPath(0);
                Tunnel->Radius.Randomize(Tunnel->NumSplineControlPoints, Tunnel->MinRadius,
                                         Tunnel->MaxRadius, 0, 0);
                Tunnel->Rotation.Randomize(Tunnel->NumSplineControlPoints, Tunnel->MinRotation,
                                           Tunnel->MaxRotation, 3, 0);
                Tunnel->RebuildColorOffsets(0);
                Tunnel->Brightness.Randomize(Tunnel->NumSplineControlPoints, 0.5f, 1.0f, 0, 0);
            }

            // 0x004fe6cc..0x004fe6df: appended (FUN_00537260 insert at end).
            G_GateTunnels.push_back(Tunnel);
        }
        else if (Reader.is_header("layer"))
        {
            // 0x004fe6fc..0x004fe706 / 0x004fea00: a third layer is refused.
            if (Tunnel->LayerCount >= 2)
            {
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: more than two layers specified in gate tunnel %s",
                      kGateTunnelCpp, 0xe9, Tunnel->Id.get_string());
                continue;
            }

            // 0x004fe70c..0x004fe770: the next layer, with its defaults.
            FGateTunnelLayer* const Layer = &Tunnel->Layers[Tunnel->LayerCount];
            ++Tunnel->LayerCount;
            Layer->Color.X = 0.5f;
            Layer->Color.Y = 0.5f;
            Layer->Color.Z = 0.5f;
            Layer->NearAlpha = 1.0f;
            Layer->RadiusFactor = 1.0f;
            Layer->FarAlpha = 0.0f;
            Layer->UOffset = 0.0f;
            Layer->VOffset = 0.0f;
            Layer->Du = 0.09f; // 0x3db851ec
            Layer->Dv = 0.07f; // 0x3d8f5c29
            Layer->VScale = 10.0f;

            while (Reader.read_value())
            {
                // "color" is its own test (0x004fe808 falls into the next chain's first test).
                if (Reader.is_value("color"))
                {
                    float const Blue = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(2)) * kInv255);
                    float const Green = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(1)) * kInv255);
                    float const Red = static_cast<float>(
                        static_cast<double>(Reader.get_value_int(0)) * kInv255);
                    Layer->Color.X = Red;
                    Layer->Color.Y = Green;
                    Layer->Color.Z = Blue;
                }
                if (Reader.is_value("near_alpha"))
                    Layer->NearAlpha = Reader.get_value_float(0);
                else if (Reader.is_value("far_alpha"))
                    Layer->FarAlpha = Reader.get_value_float(0);
                else if (Reader.is_value("radius_factor"))
                    Layer->RadiusFactor = Reader.get_value_float(0);
                else if (Reader.is_value("u_offset"))
                    Layer->UOffset = Reader.get_value_float(0);
                else if (Reader.is_value("v_offset"))
                    Layer->VOffset = Reader.get_value_float(0);
                else if (Reader.is_value("du"))
                    Layer->Du = Reader.get_value_float(0);
                else if (Reader.is_value("dv"))
                    Layer->Dv = Reader.get_value_float(0);
                else if (Reader.is_value("v_scale"))
                    Layer->VScale = Reader.get_value_float(0);
                else if (Reader.is_value("texture"))
                {
                    // 0x004fe916..0x004fe9e8: the effect ShapeDB lookup (its own miss warning,
                    // ShapeDB.h line 0xf2), then the layer takes the shape's texture record
                    // (FUN_00425680) or this file warns too.
                    FTexShape* const Shape = G_EffectShapeDB.Find(Reader.get_value_string());
                    if (Shape != nullptr)
                        TexShapeSetTexture(&Layer->Texture, Shape->Texture);
                    else
                    {
                        FDUMP(0x100002,
                              "%s(%d) : *** WARNING: Couldn't find Effect shape %s for the Gate Tunnel",
                              kGateTunnelCpp, 0xe3, Reader.get_value_string());
                    }
                }
            }
        }
    }
}
