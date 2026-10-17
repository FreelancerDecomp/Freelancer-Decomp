// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MoveIdea (ctor 0x062d4e60). See src/include/common_idea.h.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_idea.cpp, code unchanged.

#include "common_idea.h"
#include "common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // Retail's normalize-in-place tail, byte-identical in FUN_062d5890 and
    // FUN_062d58f0 (.text:062d58b1-062d58e9 / 062d593c-062d5973): FSQRT of the
    // sum of squares, then `FDIVR [DAT_0639a1d0]` (1.0f) to get the reciprocal,
    // then three FMULs. No zero-length guard in retail, and none added here. The sum is
    // (x*x + y*y) + z*z and the reciprocal stays on the FPU; each component is FSTP float.
    void NormalizeInPlace(Vector& V)
    {
        double const Len = std::sqrt((static_cast<double>(V.x) * V.x + static_cast<double>(V.y) * V.y) +
                                     static_cast<double>(V.z) * V.z);
        double const InvLen = 1.0 / Len;
        V.x = static_cast<float>(InvLen * V.x);
        V.y = static_cast<float>(InvLen * V.y);
        V.z = static_cast<float>(InvLen * V.z);
    }

    // The inline speed of an IObjInspect::get_velocity() result in MoveIdea::Update
    // (062d5249-062d526d and four siblings): x*x, z*z, FADDP, y*y, FADDP, FSQRT, with
    // nothing stored, so the magnitude is unrounded.
    double VelocityMagnitude(Vector const& V)
    {
        return std::sqrt((static_cast<double>(V.x) * V.x + static_cast<double>(V.z) * V.z)
                         + static_cast<double>(V.y) * V.y);
    }
}

// ----------------------------------------------------------------------------
// MoveIdea
// ----------------------------------------------------------------------------

// FUN_062d4e60, Common.dll.txt 215579-215649 (53 instructions, no Ghidra
// decompile). See common_idea.h's own class-level comment for the full
// field-by-field citation.
MoveIdea::MoveIdea()
    : BaseIdea()
    , Watcher()
    , Position{}
    , Direction{}
    , Mode(0)
    , MaxThrustBearing(3.14159274f)      // 0x40490fdb
    , ArrivalRange(200.0f)          // 0x43480000
    , Throttle(1.0f)               // 0x3f800000
    , StopStyle(0)
    , TargetKind48(0)
    , Offset4c{}
    , Field58(0)
    , bKillEngineOffBearing(1)
    , Pad5a{}
    , MaxSpeed5c(0.0f)
    , MaxAngularSpeedXY60(0.0f)
    , EmbeddedStop()
{
}

// Own vtable slot 0 override -- LAB_062d4f20 (item 1576), Common.dll.txt
// 215657-215704, RET 0xc, no Ghidra decompile. Ported in full 2026-09-17: all
// 46 instructions are accounted for, addresses cited inline.
bool MoveIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    // 062d4f22-062d4f32: arg1 (InObject, EDI) and arg2 (InHandle) are both
    // null-checked; either null jumps to LAB_062d4f8d, which does `XOR AL,AL`
    // and returns without touching a single field. Same guard shape as
    // BaseIdea::SetContext's own default.
    if (InObject == nullptr || InHandle == nullptr)
    {
        return false;
    }

    Handle = (SystemManager*)InHandle;   // 062d4f35
    Object = InObject;   // 062d4f46
    Context = InContext; // 062d4f49 (EAX loaded from [ESP+0x18] at 062d4f38,
                          // which is arg3 after the PUSH EBX at 062d4f34)
    Attached = 1;        // 062d4f4c, `MOV byte [ESI+0x10],BL` with EBX == 1

    // 062d4f4f-062d4f5b: `MOV EDX,[EDI]` / `PUSH 0x3f800000` / `PUSH 0x0` /
    // `MOV ECX,EDI` / `CALL [EDX+0xf0]` / `FSTP float ptr [ESI+0x60]`.
    // Slot 0xf0 of the rebuilt retail IObjInspect table is
    // get_angular_speed_XY(float Speed, float MaxSpeed) (common_iface.h). The
    // 0 is the argument closest to ESP, so it is Speed and 1.0f is MaxSpeed --
    // the opposite push order from the slot-0xd8 call immediately below, which
    // is why this cannot be written as one shared helper call.
    IObjInspect const* const Inspect = ObjInspectOf(InObject);
    MaxAngularSpeedXY60 = Inspect->get_angular_speed_XY(0.0f, 1.0f);

    // 062d4f5e-062d4f6e: `MOV EAX,[EDI]` / `PUSH EBX` (== 1) /
    // `PUSH 0x3f800000` / `MOV ECX,EDI` / `CALL [EAX+0xd8]` /
    // `FSTP float ptr [ESI+0x5c]`. Slot 0xd8 is
    // get_speed(float Throttle, ThrustEquipType Type); Throttle is closest to
    // ESP (1.0f) and the literal 1 is the enum, i.e. ThrustEquipEngine.
    MaxSpeed5c = Inspect->get_speed(1.0f, ThrustEquipEngine);

    // 062d4f71-062d4f83: reloads Context/Handle/Object out of THIS object's own
    // fields (not the incoming arguments -- same values, but worth matching) and
    // dispatches the embedded StopIdea's own vtable slot 0.
    EmbeddedStop.SetContext(Object, Handle, Context);

    return true; // 062d4f85 `MOV AL,BL`, EBX still 1
}

// Own vtable slot 1 override -- LAB_062d4fa0 (item 1577), Common.dll.txt
// 215717-216351, 596 instructions, RET 4, no Ghidra decompile. PORTED 2026-09-17
// (claude:common-idea-internals).
//
// Frame map, because every `[ESP+disp]` below depends on it. The prologue is
// `SUB ESP,0x50` then PUSH EBP/ESI/EDI, so call E the ESP after 062d4fac; the
// incoming DeltaTime is at E+0x60 and the return address at E+0x5c. A single
// extra `PUSH EBX` at .text:062d50e5 and its matching `POP EBX` at
// .text:062d5325 shift every displacement in between by four, which is the one
// thing that makes this function's stack slots easy to misread. Named slots:
//   E+0x0c  BestDistSq   (seeded 0x7effffff at 062d4fd6)
//   E+0x10  Result       (seeded 0 at 062d4fad; every `return Result` is this)
//   E+0x14  Goal         Vector
//   E+0x20  Lead         Vector
//   E+0x2c  scratch      Vector (out-param of several vtable calls)
//   E+0x44  Diff         Vector, E+0x50 ScaledDir Vector (ModeEff==0/1 tail)
//   E+0x60  DeltaTime on entry, reused as `Speed2` scratch from 062d560e on
//
// Retail returns 3 (no Object), 1 (arrived, after Commit()), 2 (the cruise gate
// refused) or 0. The 2 comes from EBP, which .text:062d51dc pins to the literal
// 2 for the rest of the body -- it is used both as an immediate argument and as
// that one return value, which is why it looks like a live value in a decompile.
int MoveIdea::Update(float DeltaTime)
{
    // 062d4fa7-062d4fc2: the only early exit that skips everything.
    if (Object == nullptr)
    {
        return 3;
    }

    // 062d4fad. Retail never stores anything but 0 here; the one `MOV [ESP+8],
    // EBP` at 062d5676 is a dead store into this slot on a path that returns EBP
    // directly. Kept as a local so the shape matches retail's own.
    int const Result = 0;

    float BestDistSq = BitsToFloat(0x7effffffu);               // 062d4fd6

    // 062d4fc5-062d4ff4: refresh the cached (radius, centre) at most once. The
    // gate reads `Handle[0x157a]` and the body writes `Nav[0x7e]`, and
    // 0x14fc + 0x7e == 0x157a -- the same byte, written before the call. Neither
    // Handle nor the +0x84 owner interface is tested.
    // FAiTarget::RefreshPos inlined: the guard set at 062d4fed before the call; push order
    // at 062d4fe9/062d4ff3 is (Radius = Nav+0x18, Centre = Nav+0x0c), result discarded.
    FAiTarget* const Nav = &Handle->SelfTarget;
    Nav->RefreshPos();

    // 062d4ff7-062d502e: rotate this object's own Offset4c out of the owner's
    // frame with the 3DMathEngine singleton (DAT_063fc974, lazily created by
    // FUN_06391cf0, never tested), then add the cached nav centre. Slot 0x2c of the
    // engine is TransformMatrix(FVector& Dst, FMatrix const&, FVector const& Source);
    // the matrix pointer is get_transform()'s return (EBP), whose Rotation sits at +0.
    Transform const& Xform = Handle->SelfTarget.Iface->get_transform(); // 062d5009, slot 0xc
    Vector Rotated;
    Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(Rotated),
                                           reinterpret_cast<FMatrix const&>(Xform.Rotation),
                                           reinterpret_cast<FVector const&>(Offset4c));

    // 062d5031-062d5052: Lead = Rotated + Nav[+0x0c], each FADD float / FSTP float.
    Vector const& NavCentre = Nav->CachedPos;
    Vector Lead;
    Lead.x = Rotated.x + NavCentre.x;
    Lead.y = Rotated.y + NavCentre.y;
    Lead.z = Rotated.z + NavCentre.z;

    // 062d5038/062d505b/062d508a: only target kinds 1 and 2 produce a distance;
    // kinds 0 and 3 leave BestDistSq at its 0x7effffff sentinel.
    // 062d508a-062d5096 and again at 062d51a9-062d51b5: the same `raw == 0 ? 0 :
    // raw - 8` outer-object adjustment MoveIdea::SetWatchObject uses, spelled out
    // twice in retail and hoisted once here.
    Watchable const* const RawBlock = Watcher.target();
    IObjRW const* const WatchedOuter = ObjRwFromWatchable(const_cast<Watchable*>(RawBlock));

    if (TargetKind48 == 1)
    {
        // 062d5098-062d50a2: FUN_062e4bb0(Handle + 0x14fc, WatchedOuter).
        // 062d50a7-062d50ae: `FLD ST0; FMUL ST1; FSTP float` -- the square of
        // the unrounded clearance, rounded once on the store.
        double const Clearance = NavInterceptClearanceOfIface(Nav, WatchedOuter);
        BestDistSq = static_cast<float>(Clearance * Clearance);
    }
    else if (TargetKind48 == 2)
    {
        // 062d505b-062d5080. FPU order preserved: (dz*dz + dy*dy) + dx*dx. The
        // three differences stay on the x87 stack (no FSTP until the sum), so
        // they are double; the only rounding is the final `FSTP float`.
        double const Dx = static_cast<double>(Lead.x) - Position.x;
        double const Dy = static_cast<double>(Lead.y) - Position.y;
        double const Dz = static_cast<double>(Lead.z) - Position.z;
        BestDistSq = static_cast<float>((Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    // 062d50b4-062d50dc: `FCOMPP` of BestDistSq against ArrivalRange
    // squared, then `TEST AH,0x5; JP` -- MSVC's "jump if greater-or-equal (or
    // unordered)" idiom, so the fall-through that commits is strictly less-than.
    // 062d50b4-062d50b9: `FLD Range; FLD ST0; FMUL ST1` -- Range*Range is never
    // stored, so the compare is against the unrounded square.
    float const Range = ArrivalRange;
    if (BestDistSq < static_cast<double>(Range) * Range)
    {
        Commit();   // 062d50ce, this->vtable[0x10]
        return 1;   // 062d50d3
    }

    // ---- LAB_062d50df: the steering body. `PUSH EBX` here is what shifts every
    // ---- later stack displacement by four. ----
    // 062d50df-062d50e6: EDI = [Context+0xc] + 0x214, never tested.
    unsigned char* const SlotBlock = SlotArrayOf(Context) + 0x214;

    // 062d5104-062d510b: bit 1 of the dword at SlotBlock+0x00, kept in BL.
    bool const bSlotOverride = ((*reinterpret_cast<unsigned int const*>(SlotBlock) >> 1) & 1u) != 0;

    Vector Goal{ 0.0f, 0.0f, 0.0f };   // 062d50ec-062d50fc, zeroed first
    if (bSlotOverride)
    {
        Goal = *reinterpret_cast<Vector const*>(SlotBlock + 0x18); // 062d510d-062d5120
    }
    else if (TargetKind48 == 1)
    {
        GetIfacePositionOrZero(&Goal, WatchedOuter); // 062d51bd
    }
    else if (TargetKind48 == 2)
    {
        Goal = Position;                                           // 062d5190-062d51a3
    }
    else if (TargetKind48 == 3)
    {
        // 062d5139-062d518e. DAT_0639f7a4 is 0x461c4000 == 10000.0f, read from
        // raw .rdata bytes -- i.e. "a point 10 km down the requested direction".
        // The three products are formed first; z's is FSTP'd to float at 062d5154
        // (into the DeltaTime slot) while x's and y's stay on the stack, unrounded,
        // until each FADD of the Lead component and its own FSTP float.
        double const Far = 10000.0;
        float const FarZ = static_cast<float>(static_cast<double>(Direction.z) * Far);
        Goal.x = static_cast<float>(static_cast<double>(Direction.x) * Far + Lead.x);
        Goal.y = static_cast<float>(static_cast<double>(Direction.y) * Far + Lead.y);
        Goal.z = static_cast<float>(static_cast<double>(FarZ) + Lead.z);
    }

    // 062d51d9-062d5205: leaving cruise. DAT_063a0408 is the float 3.0f and
    // DAT_063a0470 the double 300.0, so the threshold is 900.0 and retail
    // compares its SQUARE against BestDistSq. `TEST AH,0x41; JNZ` skips unless
    // the threshold squared is strictly greater, i.e. unless we are inside 900.
    if (Mode == 2 && StopStyle != 2)
    {
        double const Threshold = 3.0 * 300.0;
        if (Threshold * Threshold > static_cast<double>(BestDistSq))
        {
            Mode = 0;   // 062d5205
        }
    }

    // 062d520c-062d5317: a speed query whose flavour StopStyle selects. All three
    // arms leave one float on the FPU stack, which LAB_062d531a then scales --
    // the StopStyle >= 2 arm by simply never popping the 0.0f loaded at 062d520f.
    float Speed = 0.0f;   // DAT_06399408
    if (StopStyle == 0 || StopStyle == 1)
    {
        // The velocity fetch is IObjInspect::get_velocity(), which returns a
        // Vector BY VALUE -- the single pushed pointer at 062d5234/062d52e6 is
        // MSVC's hidden return buffer, not an out-parameter.
        Vector const Velocity = Handle->SelfTarget.Iface->get_velocity(); // 062d5246 / 062d5294 / 062d52e9
        // sqrt((x*x + z*z) + y*y) in double, FSTP'd to float straight into the
        // argument slot (062d526f / 062d52bd / 062d530e).
        float const Magnitude = static_cast<float>(VelocityMagnitude(Velocity));

        if (StopStyle == 0)
        {
            // 062d5311-062d5312: FUN_062e5ab0(Object, Magnitude).
            Speed = Behavior_QueryFixedFalloffEffect(Object, Magnitude);
        }
        else
        {
            // 062d5258-062d5278 / 062d52a6-062d52c6: FUN_062e58f0 with five
            // cdecl arguments (`ADD ESP,0x14`), in ascending stack order
            // (Object, -1.0f, Magnitude, 0.0f, Type). The `PUSH ECX` in the
            // middle is only reserving the slot the FSQRT result is then FSTP'd
            // into, not an argument of its own.
            ThrustEquipType const Type = (Mode == 1) ? ThrustEquipTradelane : ThrustEquipEngine;
            Speed = Behavior_QueryFalloffEffect(ObjInspectOf(Object), -1.0f, Magnitude, 0.0f, Type);
        }
    }

    // LAB_062d531a. DAT_063a0468 == 0x3f4ccccd == 0.8f. The product stays on the
    // FPU stack through the ModeEff selection below and is never stored.
    double const ScaledSpeed = static_cast<double>(Speed) * 0.8f;

    // 062d5320-062d533e: ModeEff is read AFTER the possible `Mode = 0` above.
    // The slot-array override can force it to 0.
    int ModeEff = Mode;
    if (bSlotOverride)
    {
        if (*reinterpret_cast<int const*>(SlotBlock + 0x2c) == 1)
        {
            ModeEff = 0;
        }
        else if (*reinterpret_cast<float const*>(SlotBlock + 0x28) < 1.0f)
        {
            // DAT_0639a1d0 == 1.0f. `TEST AH,5; JP` skips on >=, so the
            // ModeEff = 0 fall-through is strictly less-than.
            ModeEff = 0;
        }
    }

    // 062d5340-062d535c: Gate = ScaledSpeed + Range and its square, both unrounded;
    // `FLD BestDistSq; FCOMPP` with TEST AH,0x5 + JP clearing CL on greater, equal or
    // unordered, so bClose is strictly-less.
    double const Gate = ScaledSpeed + ArrivalRange;
    bool const bClose = (static_cast<double>(BestDistSq) < Gate * Gate);

    // The three velocity re-fetches below are genuinely three separate retail
    // calls (062d5394, 062d547b, 062d5573), one per ModeEff arm -- not one
    // hoisted query. Reproduced as such.
    // Every Behavior_* / engage_cruise receiver below is `[this+8] + 0x18`, recomputed
    // by retail at each call and never tested.
    if (ModeEff == 2)
    {
        // 062d5375: CL (bClose) gates the velocity check.
        if (bClose)
        {
            Vector const Velocity = Handle->SelfTarget.Iface->get_velocity(); // 062d5394
            // 062d53b5: the unrounded magnitude FCOMPs the DOUBLE 0.1 (DAT_063a0460
            // == 0x3fb999999999999a); TEST AH,0x41 + JNZ leaves for not-greater or
            // unordered, so the body runs only on strictly-greater.
            if (VelocityMagnitude(Velocity) > 0.1)
            {
                Handle->Thrust.engage_cruise(false);               // 062d53ca

                if (StopStyle == 0)
                {
                    Handle->Thrust.SetThrottle(0.0f, 2);   // 062d5402
                }
                else if (StopStyle == 1)
                {
                    EmbeddedStop.Update(DeltaTime);                  // 062d53e9, slot 1
                }
                return Result;   // any other StopStyle: JNZ LAB_062d55e3
            }
        }

        // LAB_062d5414: the unrounded bearing FCOMPs MaxThrustBearing; TEST AH,0x5 +
        // JP to 062d544c on greater, equal or unordered.
        double const Bearing = BearingAngleToPoint(Object, Goal);    // 062d541d
        if (Bearing < MaxThrustBearing)
        {
            Handle->Thrust.SetThrottle(Throttle, 2); // 062d543a
        }
        else
        {
            Handle->Thrust.engage_cruise(false);                   // 062d544e
        }
        return Result;
    }

    if (ModeEff == 1)
    {
        // LAB_062d5460.
        if (bClose)
        {
            Vector const Velocity = Handle->SelfTarget.Iface->get_velocity(); // 062d547b
            if (VelocityMagnitude(Velocity) > 0.1)                   // 062d549c, DOUBLE 0.1
            {
                Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };          // 062d54b4-062d54c4
                Handle->Thrust.SetNudgeDirection(ZeroVector); // 062d54cc
                return Result;
            }
        }

        // LAB_062d54de: steer along (Goal - Lead), normalized and scaled. Each
        // difference and each scaled component is FSTP'd to float.
        Vector const Diff{ Goal.x - Lead.x, Goal.y - Lead.y, Goal.z - Lead.z };
        Vector Dir;
        VecNormalize3(&Diff, &Dir);   // 062d550c, FUN_062e1680(Out = &Dir, Src = &Diff)
        Vector const ScaledDir{ Dir.x * Throttle,
                                Dir.y * Throttle,
                                Dir.z * Throttle };
        Handle->Thrust.SetNudgeDirection(ScaledDir); // 062d5542
        return Result;
    }

    if (ModeEff != 0)
    {
        return Result;   // 062d536f, any other ModeEff falls straight out
    }

    // LAB_062d5554.
    if (bClose)
    {
        Vector const Velocity = Handle->SelfTarget.Iface->get_velocity(); // 062d5573
        // 062d5594: this arm compares the unrounded magnitude against the FLOAT 0.1f
        // (DAT_0639d20c == 0x3dcccccd), not the double the other two arms use;
        // TEST AH,0x41 + JNZ, so strictly-greater runs the body.
        if (VelocityMagnitude(Velocity) > static_cast<double>(0.1f))
        {
            if (StopStyle == 0)
            {
                Handle->Thrust.SetThrottle(0.0f, 0);    // 062d55d1
                Handle->Thrust.SetEngineActive(true);   // LAB_062d55d6/55d8
            }
            else if (StopStyle == 1)
            {
                EmbeddedStop.Update(DeltaTime);                       // 062d55b7
            }
            return Result;   // any other StopStyle: JNZ LAB_062d55e3
        }
    }

    // LAB_062d55f0: the unrounded bearing FCOMPs MaxThrustBearing; TEST AH,0x5 + JP
    // jumps to 062d5683 on greater, equal or UNORDERED, so only strictly-less falls
    // through to the speed path.
    double const Bearing = BearingAngleToPoint(Object, Goal);         // 062d55f9
    if (!(Bearing < MaxThrustBearing))
    {
        // LAB_062d5683.
        Handle->Thrust.SetThrottle(0.0f, 0);  // 062d568d
        // 062d5692-062d569f: bKillEngineOffBearing picks the flag, inverted.
        Handle->Thrust.SetEngineActive(bKillEngineOffBearing == 0);
        return Result;
    }

    // 062d560b-062d565a: start from Throttle and take the smaller of it
    // and one other candidate; which candidate depends on the slot block's own
    // first dword, tested here as a whole dword (not the bit-1 test used above).
    float Speed2 = Throttle;
    if (*reinterpret_cast<int const*>(SlotBlock) != 0)
    {
        float const SlotValue = *reinterpret_cast<float const*>(SlotBlock + 0x28);
        if (SlotValue < Speed2)
        {
            Speed2 = SlotValue;   // 062d5625
        }
    }
    else
    {
        // LAB_062d562e: FUN_062e5c00(Object, &Goal, MaxAngularSpeedXY60,
        // MaxSpeed5c) -- push order verified, the two floats are the LAST two
        // arguments with the angular one first.
        // 062d5644: the unrounded ST0 result is compared (TEST AH,0x5 + JP keeps Speed2
        // on greater, equal or unordered) before its FSTP float.
        double const Cost = EstimateTurnCircleApproachCost(
            const_cast<IObjInspect*>(ObjInspectOf(Object)), Goal,
            MaxAngularSpeedXY60, MaxSpeed5c);
        if (Cost < static_cast<double>(Speed2))
        {
            Speed2 = static_cast<float>(Cost);        // 062d5652
        }
    }

    // 062d5667-062d5680: this is the one call whose RESULT retail tests. True
    // means the gate took it (return Result); false means it refused, and the
    // function reports 2.
    if (Handle->Thrust.SetThrottle(Speed2, 0))
    {
        return Result;
    }
    return 2;
}

// Own vtable slot 4 override -- LAB_062d56b0 (item 1578), Common.dll.txt
// 216364-216411, bare `RET`. Ported in full 2026-09-17: both helpers it calls
// are real ports already (see common_idea.h's slot-4 comment -- the prior
// pass's note that they were "already-documented gaps" did not survive a
// re-check of the tracker).
void MoveIdea::Commit()
{
    // 062d56b6-062d56d9: a 12-byte stack Vector is zeroed at 062d56c1/
    // 062d56c9/062d56d1 and passed to FUN_062e8ce0 with ECX = Handle + 0x18 --
    // the "ComponentThis" convention the whole generic component-registration
    // family uses.
    // No null test on Handle or Context anywhere in 062d56b0-062d5757.
    Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector);

    // 062d56de-062d5709: `CMP [ESI+0x34],1 / JZ tail`, then a SIGNED range
    // test on StopStyle (`TEST EAX,EAX / JL tail` and `CMP EAX,1 / JG tail`),
    // i.e. run the pair only when Mode != 1 and StopStyle is in [0, 1].
    // Argument order for both calls verified from the push pairs: the 0 and
    // then 2 sit at [ESP+4], i.e. they are the SECOND argument.
    if (Mode != 1 && StopStyle >= 0 && StopStyle <= 1)
    {
        Handle->Thrust.SetThrottle(0.0f, 0); // 062d56fa
        Handle->Thrust.SetThrottle(0.0f, 2); // 062d5709
    }

    // LAB_062d570e-062d5750: the unconditional tail. Zeroes the PathfindManager's
    // +0x64 dword and its +0x200..+0x208 Vector, re-reading Context->+0xc each time.
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;
    float* const Dest = reinterpret_cast<float*>(SlotArrayOf(Context) + 0x200);
    Dest[0] = 0.0f;
    Dest[1] = 0.0f;
    Dest[2] = 0.0f;
}

// ---------------------------------------------------------------------------
// MoveIdea's non-virtual setter family. Every one of these is reached by raw
// address in retail, and most share that address with a byte-identical method
// of an unrelated class -- see common_idea.h's "/OPT:ICF trap" section for the
// fold table and the constructor-LEA evidence that these really are MoveIdea's.
// ---------------------------------------------------------------------------

// FUN_062d5840, Common.dll.txt 216515-216546 (24 instructions), RET 4.
void MoveIdea::SetPosition(Vector const& Pos)
{
    TargetKind48 = 2; // 062d5844, `MOV dword [ECX+0x48],0x2`
    Position = Pos;   // 062d5853-062d5862, three dword copies to [ECX+0x1c..]

    // 062d5865-062d587c: mirror the freshly-stored Position into the owning
    // slot array at +0x200/+0x204/+0x208. Retail re-reads it out of Position
    // (EAX still points at this+0x1c), not out of Pos.
    unsigned char* const Slots = SlotArrayOf(Context);
    float* const Dest = reinterpret_cast<float*>(Slots + 0x200);
    Dest[0] = Position.x;
    Dest[1] = Position.y;
    Dest[2] = Position.z;
}

// FUN_062d5890, Common.dll.txt 216561-216603 (37 instructions), RET 4.
void MoveIdea::SetDirection(Vector const& Dir)
{
    TargetKind48 = 3; // 062d5893
    Direction = Dir;  // 062d589f-062d58ae
    NormalizeInPlace(Direction); // 062d58b1-062d58e9
}

// FUN_062d58f0, Common.dll.txt 216609-216656 (49 instructions), RET 4. Src's
// +0x08/+0x14/+0x20 are one column of a 3x3 basis (stride 0xc), so this reads
// as "face away from that object's chosen axis".
void MoveIdea::SetDirectionFrom(void const* Src)
{
    unsigned char const* const Base = static_cast<unsigned char const*>(Src);

    TargetKind48 = 3; // 062d5909

    // 062d58f7-062d5939: retail loads all three source floats onto the FPU
    // FIRST (in the order +0x08, +0x14, +0x20), then FCHS's each before storing
    // -- so the stores are the negated values, and the loads all happen before
    // any store, which matters only if Src aliased Direction (it never does at
    // any observed call site).
    float const SrcX = *reinterpret_cast<float const*>(Base + 0x08);
    float const SrcY = *reinterpret_cast<float const*>(Base + 0x14);
    float const SrcZ = *reinterpret_cast<float const*>(Base + 0x20);
    Direction.x = -SrcX;
    Direction.y = -SrcY;
    Direction.z = -SrcZ;

    NormalizeInPlace(Direction); // 062d593c-062d5973
}

// FUN_062d5760, Common.dll.txt 216421-216508 (73 instructions), RET 4. No
// Ghidra decompile for this one either (item 1579's own notes record the
// decompile as corrupted); the raw bytes are unambiguous.
void MoveIdea::SetWatchObject(void const* Object2)
{
    // 062d5767-062d576a: retail captures the slot array pointer BEFORE the null
    // test, so both branches use it.
    unsigned char* const Slots = SlotArrayOf(Context);

    if (Object2 == nullptr)
    {
        // LAB_062d57dd-062d5838: tag 0, clear the watch (dedup-guarded by
        // `CMP dword [ECX],0` -- BaseWatcher::set() already does that
        // internally), then zero both mirrored fields.
        TargetKind48 = 0;
        Watcher.set(nullptr);
        *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;  // 062d57f3-062d57f9
        float* const Dest = reinterpret_cast<float*>(Slots + 0x200); // 062d5800 (EBP)
        Dest[0] = 0.0f;
        Dest[1] = 0.0f;
        Dest[2] = 0.0f;
        return;
    }

    // 062d5779-062d578c: tag 1, then a dedup-guarded watch of Object2 + 8 --
    // the same IObjRW -> Watchable adjustment BaseWatcher(IObjRW*) applies.
    TargetKind48 = 1;
    Watcher.set(reinterpret_cast<Watchable const*>(
        static_cast<unsigned char const*>(Object2) + 8));

    // LAB_062d5791-062d57d3: both mirrors are taken from the watched block,
    // re-read from Watcher (not from Object2), each through the same
    // `raw == 0 ? 0 : raw - 8` outer-object adjustment and called through
    // without a test (the watcher was just set to a non-null pointer). The id
    // goes through a fresh Context->+0xc, the position through the captured one.
    IObjRW* const Outer = ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = static_cast<int>(Outer->get_id()); // 062d57a0-062d57a9
    IObjRW* const OuterAgain = ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    Vector const& Pos = OuterAgain->get_position();                 // 062d57bb
    float* const PosDest = reinterpret_cast<float*>(Slots + 0x200);  // 062d57bf (EBP)
    PosDest[0] = Pos.x;
    PosDest[1] = Pos.y;
    PosDest[2] = Pos.z;
}

// FUN_062d5980, Common.dll.txt 216669-216732 (49 instructions), RET 4. Retail's
// branch table, preserved exactly. Both helpers are real ports; ComponentThis
// is Handle + 0x18 (retail's `MOV ECX,[ESI+0x8]; ADD ECX,0x18`), i.e.
// BaseIdea::Handle, which is the NavHelper at every traced call site.
void MoveIdea::SetMode(int NewMode)
{
    int const Old = Mode; // 062d5986
    if (Old != NewMode)   // 062d598e, equal -> straight to the tail
    {
        // Handle + 0x18 (`MOV ECX,[ESI+8]; ADD ECX,0x18`), never tested.
        ThrustManager* const Thrust = &Handle->Thrust;

        if (Old == 2) // 062d5992
        {
            // 062d5997-062d59a0: PUSH EAX (== 2) then PUSH 0, so the 2 is the
            // second argument.
            Thrust->SetThrottle(0.0f, 2);
            Mode = NewMode; // 062d59a5
            return;
        }

        // LAB_062d59b0: `TEST EAX,EAX / JNZ` then `CMP EDI,2 / JZ` -- the pair
        // only fires for Old == 0 and NewMode != 2.
        if (Old == 0 && NewMode != 2)
        {
            // 062d59bc-062d59c1: PUSH EAX twice, and EAX is 0 here.
            Thrust->SetThrottle(0.0f, 0);
            Mode = NewMode; // 062d59c6
            return;
        }

        // LAB_062d59d1: `CMP EAX,1 / JNZ tail` -- only Old == 1 reaches the
        // zero-Vector sync, and it then falls THROUGH into the shared tail
        // rather than returning early.
        if (Old == 1)
        {
            Vector const ZeroVector{ 0.0f, 0.0f, 0.0f }; // 062d59e1-062d59f1
            Thrust->SetNudgeDirection(ZeroVector); // 062d59f9
        }
    }

    Mode = NewMode; // LAB_062d59fe
}

// Folded at .text:063275f0, whose other (real, correctly-named) owner is
// TextRenderContext::set_origin_h -- `MOV EAX,[ESP+4]; MOV [ECX+0x38],EAX;
// RET 4`.
void MoveIdea::SetAngle38(float Value)
{
    MaxThrustBearing = Value;
}

// Folded at .text:062a66b0, shared with DeviceTRC::bind_to_dc AND
// CAsteroidField::set_system -- `MOV [ECX+0x3c],EAX; RET 4`.
void MoveIdea::SetRange3c(float Value)
{
    ArrivalRange = Value;
}

// Folded at .text:062a33e0, shared with CmnAsteroid::CAsteroidCube::set_state
// -- `MOV [ECX+0x40],EAX; RET 4`.
void MoveIdea::SetScale40(float Value)
{
    Throttle = Value;
}

// FUN_062d5a20 (item 1585), Common.dll.txt 216757-216791, 26 instructions, read
// from the `PUSH ESI` at .text:062d5a20 to the `RET 0x4` at .text:062d5a60.
//
//     EAX = [ESI+0x34]                      ; Mode
//     ladder: 0 -> 1, 1 -> 3, 2 -> 4        ; 062d5a29 / 062d5a2c / 062d5a2f
//     default -> [ESP+param_1] raw dword    ; 062d5a46
//     ECX = [ESI+0x4]                       ; BaseIdea::Object
//     PUSH selector / PUSH param_1 / PUSH ECX
//     CALL FUN_062e5b70                     ; last push first => (Obj, Value, Type)
//     FSTP [ESI+0x40]                       ; Throttle
//
// Very strange, but retail's own: the `default` arm forwards Value's raw float BIT
// PATTERN as the ThrustEquipType. Reproduced as written -- Mode is not provably
// limited to 0..2 (LaunchBehavior passes SetMode a variable).
void MoveIdea::SetThrottleScale(float Value)
{
    ThrustEquipType Type;
    switch (Mode)
    {
    case 0:  Type = ThrustEquipEngine;    break;   // 062d5a3f `MOV EAX,0x1`
    case 1:  Type = ThrustEquipTradelane; break;   // 062d5a38 `MOV EAX,0x3`
    case 2:  Type = ThrustEquipCruise;    break;   // 062d5a31 `MOV EAX,0x4`
    default:                                       // 062d5a46 `MOV EAX,[ESP+param_1]`
    {
        int ValueBits;
        std::memcpy(&ValueBits, &Value, sizeof(ValueBits));
        Type = (ThrustEquipType)ValueBits;
        break;
    }
    }

    Throttle = Behavior_InvokeManagerSlotD4(ObjInspectOf(Object), Value, Type);
}

// Folded at .text:062d88b0 (item 1620) -- `MOV [ECX+0x44],EAX; RET 4`. Retail
// moves a raw dword, so this takes an int: every observed argument is a small
// integer literal (0 or 2) and Commit() reads the field back as a signed int.
void MoveIdea::SetField44(int Value)
{
    StopStyle = Value;
}

// Folded at .text:062d5a10 (item 1584, also FormationMoveFx::SetEnabled) --
// `MOV AL,[ESP+4]; MOV byte [ECX+0x58],AL; RET 4`.
void MoveIdea::SetEnabled(unsigned char Value)
{
    Field58 = Value;
}

// .text:062d5a70 (item 1586) -- `MOV AL,[ESP+4]; MOV byte [ECX+0x59],AL;
// RET 4`. Single call site .text:062c43f4, receiver DockBehavior+0x30.
// Ported 2026-09-17 (claude:w21-stubsweep).
void MoveIdea::SetField59(unsigned char Value)
{
    bKillEngineOffBearing = Value;
}
