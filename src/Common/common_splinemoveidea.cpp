// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll SplineMoveIdea (ctor 0x062d9bf0, Update LAB_062d9d00). See
// src/include/common_idea.h.

#include "common_idea.h"
#include "common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_aisteer_internal.h"
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_gotobehavior.h" // FormationFollowerEnvelope (SplineMoveIdea::Update)
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;
using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// SplineMoveIdea
// ----------------------------------------------------------------------------

// FUN_062d9bf0 (item 1647), Common.dll.txt 222923-222962 (33 instructions).
// See common_idea.h's own SplineMoveIdea comment for the RTTI walk and the
// field-by-field citation. Retail leaves Spline/LookaheadScale/SpeedNorm/
// PersistedT untouched here; they are zero-initialized anyway per this
// marathon's documented "safe over-init" convention.
SplineMoveIdea::SplineMoveIdea()
    : BaseIdea()
    , Move()
    , Face()
    , Spline(nullptr)
    , WideMode(0)
    , Pad151{}
    , BankBlendCap(1.0f)   // 0x3f800000
    , Field158(0)
    , MaxSpeed15c(0.0f)
    , LookaheadScale(0.0f)
    , SpeedNorm(0.0f)
    , StepNumerator(10.0f) // 0x41200000
    , ExtrapolateBase(50.0f) // 0x42480000
    , PersistedT(0.0f)
{
}

// Own vtable slot 0 override -- FUN_062d9c80 (item 1648), Common.dll.txt
// 222971-223025, RET 0xc. Ported in full from the raw bytes; the tracker's
// older note on this item ("[this+0x14]->vtbl[0](Object,Other)" -- two
// arguments) is a Ghidra artifact: 062d9ca0/062d9ca4/062d9caa push EBX, EDI
// and EBP for BOTH forwarded calls, i.e. all three context pointers go to
// both embedded members.
bool SplineMoveIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    // 062d9c85/062d9c91: reject on a null Object or a null Handle -- the
    // identical guard BaseIdea::SetContext uses.
    if (InObject == nullptr || InHandle == nullptr)
    {
        return false;
    }

    Attached = 1;      // 062d9ca1
    Object = InObject; // 062d9cab
    Handle = (SystemManager*)InHandle; // 062d9cae
    Context = InContext; // 062d9cb1

    // 062d9cb4 / 062d9cc5: forward the same triple to both embedded members
    // through their OWN vtable slot 0 -- real virtual dispatch in retail,
    // preserved here (Move/Face override SetContext).
    Move.SetContext(InObject, InHandle, InContext);
    Face.SetContext(InObject, InHandle, InContext);

    Spline = nullptr; // 062d9ccc

    // 062d9cd2-062d9cd6: publish a zero into the owning slot array reached
    // through Context -- the SAME `*(Context+0xc) + 0x64` idiom MoveIdea::
    // Commit() and this class's own Commit() below use (common_idea.h). Context is
    // dereferenced unguarded, as retail does.
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;

    PersistedT = 0.0f; // 062d9cda
    return true;       // 062d9c9b sets AL=1 and 062d9ce0 reloads it
}

// Own vtable slot 4 override -- LAB_062da310, Common.dll.txt 223518-223537
// (20 instructions), bare `RET`.
void SplineMoveIdea::Commit()
{
    // 062da316-062da31c: `*(*(Context+0xc)) + 0x64 = 0` -- same publish idiom
    // as SetContext above.
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;

    // 062da329 / 062da338: both embedded members' OWN vtable slot 4 (0x10),
    // zero arguments -- i.e. Commit() on each, confirming the slot-4 identity
    // from the other direction.
    Move.Commit();
    Face.Commit();

    // 062da346-062da35e: a 12-byte stack scratch is zeroed and passed to
    // FUN_062e8ce0 (ThrustManager::SetNudgeDirection) with this = Handle + 0x18.
    Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector); // never tested

    PersistedT = 0.0f; // 062da363
}

// Folded at .text:062da4d0, whose other (real, correctly-named) owner is
// IBehaviorManager::set_camera_mode -- `MOV EAX,[ESP+4]; MOV [ECX+0x154],EAX;
// RET 4`. See the declaration in common_idea.h for the receiver proof.
void SplineMoveIdea::SetBankBlendCap(float Value)
{
    BankBlendCap = Value;
}

// FUN_062da4e0 (item 1656), Common.dll.txt 223720-223732, 12 instructions, read
// from the `MOV EAX,[ESP+param_1]` at .text:062da4e0 to the `RET 0x4` at
// .text:062da4fd. No Mode ladder: `PUSH 0x1` at .text:062da4ea pins the thrust
// type to ThrustEquipEngine unconditionally.
void SplineMoveIdea::SetThrottleScale(float Value)
{
    BankBlendCap = Behavior_InvokeManagerSlotD4(ObjInspectOf(Object), Value, ThrustEquipEngine);
}

// FUN_062da400 (item 1654), Common.dll.txt 223616-223623, RET 4. Whole body:
// `MOV EAX,[ESP+4]; MOV dword ptr [ECX+0x158],EAX; RET 4`. Ported 2026-09-17
// (claude:w21-stubsweep); see the declaration in common_idea.h for the
// receiver proof at both call sites.
void SplineMoveIdea::SetField158(int Value)
{
    Field158 = Value;
}

// FUN_062da410, Common.dll.txt 223630-223691, RET 4. Ported 2026-09-17
// (claude:common-idea-methods).
//
// Retail's body is a ~40-instruction branch tree, and tracing it is what proves
// the two lines below are the whole of it. Written out with this class's own
// field names (its `[ESI+0x48]` is 0x14 + 0x34 == Move.Mode, and its
// `[ESI+0x1c]` is 0x14 + 0x08 == Move.Handle):
//   WideMode = bWide;                                // 062da41a
//   OldMode = Move.Mode;                             // 062da422
//   if (bWide) {                                     // 062da420/062da425
//       if (OldMode != 2 && OldMode == 1)            // 062da427/062da42c
//           SyncPredictedPosition(Move.Handle+0x18, Zero);  // 062da454
//       Move.Mode = 2;                               // LAB_062da459
//   } else if (OldMode == 2) {                       // 062da467/062da46b
//       UpdateCruiseGate(Move.Handle+0x18, 0.0f, 2); // 062da479
//       Move.Mode = 0;
//   } else {
//       if (OldMode == 1)                            // 062da48c
//           SyncPredictedPosition(Move.Handle+0x18, Zero);  // 062da4b4
//       Move.Mode = 0;                               // LAB_062da4b9
//   }
// Compare that against MoveIdea::SetMode (FUN_062d5980) above, transition by
// transition, with NewMode == (bWide ? 2 : 0):
//   Old==New      -> SetMode falls straight to the tail; retail here also does
//                    nothing but the store (2->2 jumps to LAB_062da459, 0->0
//                    jumps to LAB_062da4b9).
//   Old==2, New==0 -> both do UpdateCruiseGate(.., 0.0f, 2) then store.
//   Old==1, New==2 -> both do SyncPredictedPosition(.., Zero) then store.
//   Old==1, New==0 -> both do SyncPredictedPosition(.., Zero) then store.
//   Old==0, New==2 -> SetMode's `Old==0 && New!=2` arm does NOT fire, so it just
//                     stores; retail here also just stores.
// Every case agrees, so this function IS `WideMode = bWide; Move.SetMode(...)`
// with SetMode inlined -- which is independent confirmation that FUN_062d5980 is
// MoveIdea's own SetMode and not merely a class-agnostic helper that happens to
// work on MoveIdea's offsets.
void SplineMoveIdea::SetWideMode(unsigned char Value)
{
    WideMode = Value; // 062da41a, the caller's byte stored raw
    Move.SetMode(Value != 0 ? 2 : 0);
}

// FUN_062da380 (item 1652), Common.dll.txt 223563-223589, RET 4. Ported 2026-09-17
// (claude:common-idea-internals) -- see the declaration in common_idea.h for why
// the "blocked on FUN_062e5ab0" note this replaces was already stale.
void SplineMoveIdea::SetSpline(ICRSplineSegment* NewSpline)
{
    // 062da387/062da397: `FLD [ESI+0x16c]; FSTP [ESI+0x50]`, and +0x50 is
    // 0x14 + 0x3c, i.e. the embedded MoveIdea's own range default.
    Move.ArrivalRange = ExtrapolateBase;

    Spline = NewSpline;   // 062da39b, [ESI+0x14c]
    Move.StopStyle = 2;     // 062da3a1, [ESI+0x58] == 0x14 + 0x44
    Move.bKillEngineOffBearing = 0;     // 062da3a8, [ESI+0x6d] == 0x14 + 0x59

    // 062da390-062da3b9: FUN_062e5af0(Object, 1.0f, 1) -- cdecl, three dwords,
    // and ThrustEquipType 1 is ThrustEquipEngine, so this is
    // IObjInspect::get_speed at full engine throttle.
    MaxSpeed15c = Behavior_InvokeManagerSlotD8(ObjInspectOf(Object), 1.0f, ThrustEquipEngine);

    // 062da3b1-062da3c9: retail FSTs the result above into a stack slot, reloads
    // it as a raw dword and pushes THAT, so the second query genuinely consumes
    // the first. FUN_062e5ab0 == Behavior_QueryFixedFalloffEffect (item 1761).
    LookaheadScale = Behavior_QueryFixedFalloffEffect(Object, MaxSpeed15c);

    // 062da3cf-062da3dd: ICRSplineSegment::get_spline_length on the spline just
    // stored, unconditionally -- retail has no null test here.
    SpeedNorm = Spline->get_spline_length();
}

// The always-true `RunCollisionProbeStub` that used to live here is GONE as of
// 2026-09-17 (claude:common-idea-updates): FUN_062e53e0 is ported for real,
// above, as IsPointOutsideHeadingCone. Two things the stub got wrong beyond
// merely not computing anything, both settled off the raw listing:
//   * its third parameter was typed `int Flag`. It is a FLOAT threshold -- the
//     tail compares the clamped dot against it (062e5595) and BreakAwayIdea's
//     own two call sites push 0xbf7ff972 and 0xbf34fdf4, which are floats.
//     The literal 0 the one call site below passes is 0.0f, same bit pattern,
//     so nothing about that site changes.
//   * its second parameter was typed `void* Ctx`. It is the IObjInspect whose
//     centre and heading the predicate is built from.
// NOTE (historical): the private FormationBehavior::RunCollisionProbe that sat in
// common_behavior.cpp was the SAME retail function and was a second always-true
// stub. It has since been retired (2026-10-08, see common_dockbehavior.h): its one
// call site now calls IsPointOutsideHeadingCone.

// Own vtable slot 1 -- LAB_062d9d00 (item 1649, formerly the free function
// UpdateSplineSteeringTask(void* Self, float Dt)), Common.dll.txt 223041-223498, RET 4.
// Returns 3 when not attached / no spline / no object, 0 while steering, 1 once the
// segment end is reached and the idea has committed itself.
int SplineMoveIdea::Update(float Dt)
{
    // Common.dll.txt:223045-223053.
    if (Attached == 0)
        return 3;
    if (Spline == nullptr)
        return 3;
    if (Object == nullptr)
        return 3;

    // .text:062d9d2d-062d9d6b -- FUN_062ebf50 (item 1855, ComputeFormationFollowerEnvelope)
    // on Handle+0x1364 into a stack envelope whose four floats retail zeroes first (not
    // Flag10). Its Throttle (E+0x74) is read twice: here into Move.Throttle and
    // again as the tail's first min-clamp (.text:062da0fd, same stack slot).
    FormationFollowerEnvelope Envelope;
    Envelope.Speed = 0.0f;
    Envelope.Throttle = 0.0f;
    Envelope.AngularSpeed = 0.0f;
    Envelope.AngularThrottle = 0.0f;
    if (Handle->GunConvergenceStateBytes.ComputeFormationFollowerEnvelope(&Envelope))
        Move.Throttle = Envelope.Throttle;

    FAiTarget* const Elem = &Handle->SelfTarget;

    // 062d9d71-062d9d9c: the owner's cached centre/radius refresh (FAiTarget::RefreshPos
    // inlined, +0x7e raised first).
    Elem->RefreshPos();
    // 062d9da5-062d9dc9: the centre is copied to G+0x4c (G = ESP after the three
    // callee-saved pushes) and that copy is what both get_t_from_point and the
    // look-ahead point below use.
    Vector const ElemPos = Elem->CachedPos;

    // 062d9d9f-062d9de5: StepT = (float)(+0x168 / SpeedNorm); T1 = get_t_from_point
    // (ElemPos, PersistedT, StepT), FST to G+0x14 and FSTP into PersistedT.
    float const T1 = Spline->get_t_from_point(ElemPos, PersistedT, StepNumerator / SpeedNorm);
    PersistedT = T1;
    // 062d9deb-062d9df7: the sample at T1 lands in G+0x64 and is never read again.
    (void)Spline->calculate_spline_vector(T1);

    // 062d9dfc-062d9e32: Object slot 1 get_velocity; |v| = sqrt((y*y + z*z) + x*x)
    // unrounded FCOMP 1.0f; TEST AH,0x5 / JP -- not-less (and NaN) normalises it.
    Vector const RawForward = ObjInspectOf(Object)->get_velocity();
    double const RawForwardLen = std::sqrt(((double)RawForward.y * RawForward.y + (double)RawForward.z * RawForward.z) +
                                           (double)RawForward.x * RawForward.x);
    Vector DirA;
    if (!(RawForwardLen < 1.0))
    {
        // LAB_062d9e86: FUN_062e1680.
        VecNormalize3(&RawForward, &DirA);
    }
    else
    {
        // 062d9e34-062d9e84: -(column 2) of SelfTarget.Iface's get_transform (slot 3),
        // built by FUN_0628b030 from +0x08/+0x14/+0x20 and negated by FUN_062c5a70.
        Vector const Column2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        DirA.x = -Column2.x;
        DirA.y = -Column2.y;
        DirA.z = -Column2.z;
    }

    // 062d9eac-062d9f07: LookaheadPoint = ElemPos + DirA * LookaheadScale (the scaled
    // vector is FSTP'd into a Vector first, then each sum FSTP'd) -- the ELEMENT
    // position, not the spline sample.
    Vector const ScaledDirA{ DirA.x * LookaheadScale, DirA.y * LookaheadScale, DirA.z * LookaheadScale };
    Vector const LookaheadPoint{ ElemPos.x + ScaledDirA.x, ElemPos.y + ScaledDirA.y, ElemPos.z + ScaledDirA.z };
    // 062d9f0c-062d9f2d: T2 = get_t_from_point(LookaheadPoint, T1, StepT recomputed),
    // FSTP to G+0x10.
    float const T2 = Spline->get_t_from_point(LookaheadPoint, T1, StepNumerator / SpeedNorm);

    // 062d9f31-062d9f52: BankBlend seeded with 1.0f (G+0x18); WideMode picks the arm.
    bool const bWide = WideMode != 0;
    double BankBlend = 1.0f;

    // Both arms refresh the cached radius again (062d9f58-062d9f6f / 062da072-062da089)
    // and sample the far point at T3 = ((Radius + bonus) / SpeedNorm) + T2, one FSTP.
    Elem->RefreshPos();
    float const LookaheadRadiusBonus = bWide ? 200.0f : 50.0f; // DAT_0639b22c / DAT_0639e6d4
    float const T3 = (float)(((double)Elem->CachedRadius + LookaheadRadiusBonus) / SpeedNorm + T2);
    Vector const FarPoint = Spline->calculate_spline_vector(T3);

    if (!bWide)
    {
        // 062d9f99-062d9fd7: Delta = FarPoint - LookaheadPoint, FSTP per component.
        Vector const Delta = Sub(FarPoint, LookaheadPoint);
        // 062d9fdc-062da005: |Delta| = sqrt((y*y + z*z) + x*x) unrounded FCOMP
        // 0.99989998f (DAT_0639f538); TEST AH,0x5 / JP -- not-less (and NaN) takes the
        // dot arm, less loads 1.0f.
        double const DeltaLen = std::sqrt(((double)Delta.y * Delta.y + (double)Delta.z * Delta.z) +
                                          (double)Delta.x * Delta.x);
        double BankFactor = 1.0f;
        if (!(DeltaLen < 0.99989998f))
        {
            // 062da00f-062da03e: FUN_062e1630(FUN_062e1680(Delta), DirA) left in ST0;
            // FCOM 0.1f, TEST AH,0x5 / JP keeps it on not-less (and NaN), else 0.1f.
            Vector NormDelta;
            VecNormalize3(&Delta, &NormDelta);
            double const Dot = ClampedDot3(NormDelta, DirA);
            BankFactor = (Dot < 0.1f) ? (double)0.1f : Dot;
        }

        // 062da044-062da06d: FUN_062e5bc0(Object, (float)(BankFactor * LookaheadScale))
        // FSTP'd, then FUN_062e5b70(Object, that, 1) whose return is the BankBlend.
        IObjInspect const* const Inspect = ObjInspectOf(Object);
        float const SlotDCResult = Behavior_InvokeManagerSlotDC(Inspect, (float)(BankFactor * LookaheadScale));
        BankBlend = Behavior_InvokeManagerSlotD4(Inspect, SlotDCResult, ThrustEquipEngine);
    }
    else
    {
        // 062da0bf-062da0fa: Move.Mode (this+0x48): 2 stays, 1 first zeroes the nudge
        // through Move.Handle's ThrustManager, then 2 (MoveIdea::SetMode(2) inlined).
        if (Move.Mode != 2 && Move.Mode == 1)
        {
            Vector ZeroDir{ 0.0f, 0.0f, 0.0f };
            Move.Handle->Thrust.SetNudgeDirection(ZeroDir);
        }
        Move.Mode = 2;
    }

    // 062da0fd-062da129: two min-clamps on ST0. `FCOM Envelope.Throttle; TEST AH,0x1;
    // JNZ` keeps ST0 on less AND unordered; `FCOM +0x154; TEST AH,0x41; JNZ` keeps it on
    // less, equal and unordered.
    if (!(BankBlend < Envelope.Throttle))
        BankBlend = Envelope.Throttle;
    if (BankBlend > BankBlendCap)
        BankBlend = BankBlendCap;

    // 062da129-062da13c: Move.Throttle = BankBlend (FSTP), Move's angle default
    // back to pi (raw 0x40490fdb), Move.TargetKind48 = 2.
    Move.Throttle = (float)BankBlend;
    Move.MaxThrustBearing = BitsToFloat(0x40490fdbu);
    Move.TargetKind48 = 2;

    // 062da13f-062da176: Spline->P2 into Move.Position and, from that copy, into
    // Move.Context->Pathfind->CachedTargetPosition. Retail dereferences the chain
    // unconditionally.
    Move.Position = Spline->P2;
    ((IBehaviorManager*)Move.Context)->Pathfind->CachedTargetPosition = Move.Position;

    // 062da179-062da17f: the same PathfindManager's +0x248 gets the raw 0x30038010.
    ((IBehaviorManager*)Move.Context)->Pathfind->Field_248 = 0x30038010;

    // 062da16f-062da199: Move's own vtable slot 1, Update(Dt), result kept in EBP.
    int const MoveResult = Move.Update(Dt);

    // 062da18f-062da206: T2 FCOMP the DOUBLE 1.0 (DAT_06399410); TEST AH,0x5 / JP --
    // not-less (and NaN) writes the chord P3 - P2 with tag 3, less writes FarPoint with
    // tag 2 into the embedded FaceIdea (this+0x8c).
    if (!((double)T2 < 1.0))
    {
        Vector const Chord{ Spline->P3.x - Spline->P2.x, Spline->P3.y - Spline->P2.y, Spline->P3.z - Spline->P2.z };
        Face.Direction28 = Chord;
        Face.TargetKind = 3;
    }
    else
    {
        Face.Position1c = FarPoint;
        Face.TargetKind = 2;
    }
    // 062da209-062da20c: Face's Update(Dt), result discarded.
    (void)Face.Update(Dt);

    // 062da210-062da226: probe only when Move returned 1, or else when T1 (E+8) > the
    // double 1.0 (TEST AH,0x41 / JNZ returns 0 on less, equal and NaN).
    if (MoveResult != 1 && !((double)T1 > 1.0))
        return 0;

    // 062da22c-062da2b5: ProbePos = P2 + normalize(P3 - P2) * s with s = -(+0x16c) - 1.0f
    // kept on the x87 stack; the x and y products are FSTP'd before their adds, the z
    // product is not.
    Vector const Chord{ Spline->P3.x - Spline->P2.x, Spline->P3.y - Spline->P2.y, Spline->P3.z - Spline->P2.z };
    Vector ChordDir;
    VecNormalize3(&Chord, &ChordDir);
    double const ExtrapolateFactor = -(double)ExtrapolateBase - 1.0f;
    float const ScaledX = (float)(ChordDir.x * ExtrapolateFactor);
    float const ScaledY = (float)(ChordDir.y * ExtrapolateFactor);
    Vector const ProbePos{
        ScaledX + Spline->P2.x,
        ScaledY + Spline->P2.y,
        (float)(ExtrapolateFactor * ChordDir.z + Spline->P2.z)
    };
    // FUN_062e53e0(&ProbePos, Object, 0.0f).
    if (!IsPointOutsideHeadingCone(ProbePos, ObjInspectOf(Object), 0.0f))
        return 0;

    // 062da2c1-062da2dd: Move.StopStyle = Field158 (raw dword), then Commit() through this
    // object's own vtable slot 4 (`MOV ECX,ESI; CALL [EDX+0x10]`), return 1.
    Move.StopStyle = Field158;
    Commit();
    return 1;
}
