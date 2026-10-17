// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MatchThrottle2Idea (ctor 0x062d5aa0). See src/include/common_idea.h.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_idea.cpp, code unchanged.

#include "common_idea.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"  // Behavior_InvokeManagerSlotD4 / D8, Behavior_QueryRadiusEffect / ...
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

// ----------------------------------------------------------------------------
// MatchThrottle2Idea -- re-ported from the listing 2026-10-08 (claude-code:cmn-ai5).
// Handle is the SystemManager, Context the IBehaviorManager, Object the owner IObjRW.
// ----------------------------------------------------------------------------

namespace
{
    PathfindManager* MatchPathfindOf(void* Context)
    {
        return static_cast<IBehaviorManager*>(Context)->Pathfind;
    }

    IObjRW* MatchWatched(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The shared tail of FUN_062d65f0 / FUN_062d6670: `MOV ESI,[[this+0xc]+0xc]`, the
    // watcher re-read with the `-8` downcast and NO null test (ECX == 0 faults in
    // retail), slot 0 get_position, three dwords into +0x200.
    void MirrorWatchedPosition(void* Context, BaseWatcher const& Watcher)
    {
        PathfindManager* const Pathfind = MatchPathfindOf(Context);
        Pathfind->CachedTargetPosition = MatchWatched(Watcher)->get_position();
    }
}

// FUN_062d5aa0 (.text:062d5aa0-062d5b48). BaseIdea's Object / Handle / Attached are
// zeroed (Context is not, BaseIdea's ctor zeroes it here -- the port's convention), the
// +0x14 watcher is zeroed (EH state 1), then the scalars. +0x28 and +0x4c/+0x50 are
// never written by retail's ctor (Distance28 before SetWatchAnd*, Speed4c/50 before
// SetContext); the port zeroes them so the object is deterministic.
MatchThrottle2Idea::MatchThrottle2Idea()
    : BaseIdea()
    , Watcher()
    , Position{ 0.0f, 0.0f, 0.0f }  // 062d5aed-062d5af3
    , Distance28(0.0f)              // not written by retail
    , Speed2c(0.0f)                 // 062d5af6
    , ThrottleBias30(0.0f)          // 062d5af9
    , MaxBearing34(0.78539819f)     // 062d5b10 0x3f490fdb
    , Flag38(1)                     // 062d5b08
    , Flag39(0)                     // 062d5afc
    , Flag3a(0)                     // 062d5aff
    , Flag3b(0)                     // 062d5b02
    , Flag3c(1)                     // 062d5b0c
    , Flag3d(0)                     // 062d5b05
    , Pad3e{}
    , ClosingFloor40(-200.0f)       // 062d5b17 0xc3480000
    , ClosingBias44(-0.5f)          // 062d5b1e 0xbf000000
    , Field48(0.05f)                // 062d5b25 0x3d4ccccd
    , Speed4c(0.0f)                 // not written by retail
    , Speed50(0.0f)                 // not written by retail
    , ClosingWindow54(5.0f)         // 062d5b2c 0x40a00000
    , CatchUpRange58(500.0f)        // 062d5b33 0x43fa0000
{
}

// Slot 0, LAB_062d5b50 (.text:062d5b50-062d5be6, RET 0xc).
bool MatchThrottle2Idea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (InObject == nullptr || InHandle == nullptr)                 // 062d5b54 / 062d5b63
        return false;

    Handle = (SystemManager*)InHandle;                              // 062d5b6e
    Object = InObject;                                              // 062d5b80
    Context = InContext;                                            // 062d5b83
    Attached = 1;                                                   // 062d5b86

    IObjInspect const* const Owner = static_cast<IObjInspect const*>(static_cast<IObjRW*>(Object));

    // 062d5b68-062d5bad: the two calls share one frame. `PUSH 4; PUSH 0; PUSH 4;
    // PUSH 1.0f; PUSH Object; CALL FUN_062e5af0` is get_speed(1.0f, Cruise); its result
    // is FSTP'd over the third pushed dword, `ADD ESP,8` drops two, and `PUSH 0; PUSH
    // Object; CALL FUN_062e5850` reads (Object, 0, speed, 0, 4). FADD 300.0f
    // (.rdata:0639fe30) and FSTP to +0x4c.
    float const CruiseSpeed = Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipCruise);
    Speed4c = static_cast<float>(
        static_cast<double>(Behavior_QueryRadiusEffect(Owner, 0.0f, CruiseSpeed, 0.0f, ThrustEquipCruise)) + 300.0f);

    // 062d5bac-062d5bdc: the same pair with type 1 (Engine), FADD 50.0f (.rdata:0639e6d4)
    // into +0x50.
    float const EngineSpeed = Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipEngine);
    Speed50 = static_cast<float>(
        static_cast<double>(Behavior_QueryRadiusEffect(Owner, 0.0f, EngineSpeed, 0.0f, ThrustEquipEngine)) + 50.0f);
    return true;                                                    // 062d5bd9 AL = BL = 1
}

// Slot 2, LAB_062d5bf0 (.text:062d5bf0-062d5cd0, RET 4). The argument is never read.
void MatchThrottle2Idea::Slot2(float DeltaTime)
{
    (void)DeltaTime;
    IObjRW* const Watched = MatchWatched(Watcher);
    if (Watched == nullptr || Object == nullptr)                    // 062d5bf6-062d5c11
        return;

    ThrustManager& Thrust = Handle->Thrust;
    if (Flag38 == 0)                                                // 062d5c17
    {
        Thrust.engage_cruise(false);                                // LAB_062d5cbf
        return;
    }

    bool bCruise = false;                                           // 062d5c27
    Watched->is_cruise_active(bCruise);                             // 062d5c3c [EAX+0x60]
    Vector const Velocity = Watched->get_velocity();                // 062d5c54 [EAX+0x04]
    unsigned char const bEngaged = Thrust.bCruiseEngaged;           // 062d5c60 [Handle+0x2c]

    if (bCruise)
    {
        // 062d5c65-062d5c8c: (y*y + z*z) + x*x against 900.0f (.rdata:063a0494);
        // `TEST AH,0x41; JNZ` leaves on not-greater.
        double const SpeedSq = (static_cast<double>(Velocity.y) * Velocity.y +
                                static_cast<double>(Velocity.z) * Velocity.z) +
                               static_cast<double>(Velocity.x) * Velocity.x;
        if (SpeedSq > 900.0f && bEngaged == 0)
            Thrust.engage_cruise(true);                             // 062d5c97
        return;
    }

    if (bEngaged != 0 && Flag3d == 0)                               // LAB_062d5ca3
        Thrust.engage_cruise(true);                                 // 062d5cb3
}

// Slot 4, LAB_062d6550 (.text:062d6550-062d65ea, RET 0).
void MatchThrottle2Idea::Commit()
{
    SystemManager* const Sys = Handle;
    Vector const Zero{ 0.0f, 0.0f, 0.0f };
    Sys->Thrust.SetNudgeDirection(Zero);                            // 062d6579 FUN_062e8ce0
    if (Flag39 == 0)                                                // 062d657e
    {
        Sys->Thrust.SetThrottle(0.0f, 0);                           // 062d658f FUN_062e89a0
        Sys->Thrust.engage_cruise(false);                           // 062d659c
    }
    Sys->Thrust.SetThrustersActive(false);                          // LAB_062d65a1 FUN_062e9120
    MatchPathfindOf(Context)->CachedTargetPosition = Vector{ 0.0f, 0.0f, 0.0f };   // 062d65ae-062d65e3
}

// FUN_062d65f0 (.text:062d65f0-062d665f, RET 8).
void MatchThrottle2Idea::SetWatchAndPosition(void const* Object2, Vector const& Point)
{
    // `raw = Object2 ? Object2 + 8 : 0; if (raw != [EDI]) set_pointer(raw)` == set().
    Watcher.set(Object2 != nullptr
        ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
        : nullptr);
    Position = Point;                                               // 062d6612-062d6629
    Distance28 = 0.0f;                                              // 062d662c
    MirrorWatchedPosition(Context, Watcher);                        // 062d6626-062d665b
}

// FUN_062d6670 (.text:062d6670-062d66fd, RET 8). Same as FUN_062d65f0 except that the
// point is zeroed (three zero locals copied in) and the float lands in +0x28.
void MatchThrottle2Idea::SetWatchAndDistance(void const* Object2, float Distance)
{
    Watcher.set(Object2 != nullptr
        ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
        : nullptr);
    Position = Vector{ 0.0f, 0.0f, 0.0f };                          // 062d6695-062d66c5
    Distance28 = Distance;                                          // 062d66cb
    MirrorWatchedPosition(Context, Watcher);                        // 062d66c8-062d66f6
}

// Slot 1, LAB_062d5ce0 (.text:062d5ce0-062d6531, RET 4). DeltaTime is never read.
// Frame names below are B+n for [ESP0+n], ESP0 = ESP after `SUB ESP,0x64`.
int MatchThrottle2Idea::Update(float DeltaTime)
{
    (void)DeltaTime;
    Flag3d = 0;                                                     // 062d5ce6
    IObjRW* const Watched = MatchWatched(Watcher);
    if (Watched == nullptr || Object == nullptr)                    // 062d5cea-062d5d05
        return 3;                                                   // LAB_062d6528

    IObjRW* const Owner = static_cast<IObjRW*>(Object);
    SystemManager* const Sys = Handle;
    FAiTarget* const Nav = &Handle->SelfTarget;

    // 062d5d1c-062d5d70: own cached centre minus the watched position, built through
    // Vec3Assign (FUN_0628b030) from three FSTP'd floats into B+0x28.
    Nav->RefreshPos();
    Vector const& WatchedPos = Watched->get_position();             // 062d5d4b [EAX]
    Vector Delta;
    Delta.z = Nav->CachedPos.z - WatchedPos.z;
    Delta.y = Nav->CachedPos.y - WatchedPos.y;
    Delta.x = Nav->CachedPos.x - WatchedPos.x;

    // 062d5d75-062d5dd5: the distance by the 0x5f3759df trick. (x*x + y*y) + z*z stays
    // on the x87 stack; FST [B+0x14] rounds it for the bit pattern; the seed goes to B+8;
    // DistSq * s * (1.5 - 0.5 * s * s * DistSq) is FSTP'd over B+0x14.
    double const DistSq = (static_cast<double>(Delta.x) * Delta.x + static_cast<double>(Delta.y) * Delta.y) +
                          static_cast<double>(Delta.z) * Delta.z;
    float const DistSqF = static_cast<float>(DistSq);
    int DistSqBits;
    std::memcpy(&DistSqBits, &DistSqF, sizeof(DistSqBits));
    int const SeedBits = 0x5f3759df - (DistSqBits >> 1);           // SAR ECX,1; SUB
    float Seed;
    std::memcpy(&Seed, &SeedBits, sizeof(Seed));
    float const Distance = static_cast<float>(
        ((1.5f - ((static_cast<double>(Seed) * Seed) * DistSq) * 0.5f) * Seed) * DistSq);   // B+0x14

    // 062d5dd7-062d5df3: Flag38 asks the watched ship whether it cruises / rides a lane
    // (slot 23 is_cruise_active(bool&, bool&) into B+2 / B+3, both pre-cleared).
    bool bWatchedCruise = false;
    bool bWatchedLane = false;
    if (Flag38 != 0)
        Watched->is_cruise_active(bWatchedCruise, bWatchedLane);

    // 062d5df6-062d5e67: both speeds, (x*x + y*y) + z*z then FSQRT, FSTP float. The
    // watched one lands on B+8 (over the dead seed), the own one on B+0x10.
    Vector const WatchedVel = Watched->get_velocity();
    float const WatchedSpeed = static_cast<float>(std::sqrt(
        (static_cast<double>(WatchedVel.x) * WatchedVel.x + static_cast<double>(WatchedVel.y) * WatchedVel.y) +
        static_cast<double>(WatchedVel.z) * WatchedVel.z));
    Vector const OwnVel = Owner->get_velocity();
    float const OwnSpeed = static_cast<float>(std::sqrt(
        (static_cast<double>(OwnVel.x) * OwnVel.x + static_cast<double>(OwnVel.y) * OwnVel.y) +
        static_cast<double>(OwnVel.z) * OwnVel.z));

    // 062d5e69-062d5e90: the throttle that matches the watched speed -- cruise type 4
    // only when cruising and not in a lane, engine type 1 otherwise. B+0xc.
    ThrustEquipType const MatchType = (bWatchedCruise && !bWatchedLane) ? ThrustEquipCruise : ThrustEquipEngine;
    float const MatchThrottle = Behavior_InvokeManagerSlotD4(Owner, WatchedSpeed, MatchType);

    // B+0x1c: the aim vector, from the own centre.
    Vector Aim;
    // 062d5e94-062d5ea5: `FLD [+0x28]; FCOMP 0; TEST AH,0x41; JNZ LAB_062d6072`.
    if (Distance28 > 0.0f)
    {
        // 062d5eab-062d5ec4: FUN_062e60e0 writes the watched physical centre into B+0x28
        // (Delta's slot, dead by now).
        Vector WatchedCenter;
        GetIfacePositionOrZero(&WatchedCenter, Watched);

        // 062d5ec4-062d5ee3: Distance28 is parked at B+4; `Distance28 * 0.95 (double
        // .rdata:063a04a0) FCOMPP Distance; TEST AH,0x41; JNZ` -- inside 95% of the
        // standoff the aim is zero.
        if (static_cast<double>(Distance28) * 0.95 > Distance)
        {
            Aim = Vector{ 0.0f, 0.0f, 0.0f };                       // 062d5ee5-062d5ef5
        }
        else
        {
            // LAB_062d5f02: the standoff point. R = max(radius, atmosphere) of the watched
            // object (FUN_062e6010) + own cached radius (B+0x18) + Distance28 (B+4).
            Nav->RefreshPos();
            float const OwnRadius = Nav->CachedRadius;              // 062d5f31
            float const Standoff = static_cast<float>(
                (static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) + OwnRadius) + Distance28);
            // 062d5f80-062d5fab: unit vector from the watched centre to the own centre.
            Nav->RefreshPos();
            Vector Away;
            Away.z = Nav->CachedPos.z - WatchedCenter.z;
            Away.y = Nav->CachedPos.y - WatchedCenter.y;
            Away.x = Nav->CachedPos.x - WatchedCenter.x;
            Vector AwayDir;
            VecNormalize3(&Away, &AwayDir);
            // 062d5fb0-062d6014: (Standoff * dir) FSTP'd, then + the watched centre.
            Aim.x = Standoff * AwayDir.x;
            Aim.y = Standoff * AwayDir.y;
            Aim.z = Standoff * AwayDir.z;
            Aim.x = Aim.x + WatchedCenter.x;
            Aim.y = Aim.y + WatchedCenter.y;
            Aim.z = Aim.z + WatchedCenter.z;
            // 062d6018-062d6053: relative to the own centre.
            Nav->RefreshPos();
            Aim.x = Aim.x - Nav->CachedPos.x;
            Aim.y = Aim.y - Nav->CachedPos.y;
            Aim.z = Aim.z - Nav->CachedPos.z;
        }
        Position = WatchedCenter;                                   // LAB_062d6057 (from B+0x28)
    }
    else
    {
        // LAB_062d6072: the fixed point relative to the own centre.
        Nav->RefreshPos();
        Aim.x = Position.x - Nav->CachedPos.x;
        Aim.y = Position.y - Nav->CachedPos.y;
        Aim.z = Position.z - Nav->CachedPos.z;
    }

    // LAB_062d60d5: both forward vectors -- the watched one by FUN_062e6150 (B+0x40), the
    // own one from ExternalIface's transform (column 2 negated, Vec3Assign into B+0x4c).
    Vector WatchedFwd;
    GetNegatedIfaceDirection(&WatchedFwd, Watched);
    Transform const& OwnXf = Sys->SelfTarget.Iface->get_transform();
    Vector OwnFwd;
    OwnFwd.x = -OwnXf.Rotation.E[0][2];                             // +0x08
    OwnFwd.y = -OwnXf.Rotation.E[1][2];                             // +0x14
    OwnFwd.z = -OwnXf.Rotation.E[2][2];                             // +0x20

    float Throttle;                                                 // B+4
    int Mode;                                                       // EDI
    // 062d612c-062d6163: (Own.x*W.x + Own.z*W.z) + Own.y*W.y. `TEST AH,0x41; JZ` takes
    // the closing-speed path when greater than 0, and so does Distance28 > 0.
    double const Facing = (static_cast<double>(OwnFwd.x) * WatchedFwd.x + static_cast<double>(OwnFwd.z) * WatchedFwd.z) +
                          static_cast<double>(OwnFwd.y) * WatchedFwd.y;
    if (!(Facing > 0.0) && !(Distance28 > 0.0f))
    {
        Throttle = 1.0f;                                            // 062d6165
    }
    else
    {
        // LAB_062d6172: closing = |Aim| * clamp(dot(WatchedFwd, unit(Aim))), i.e. how far
        // ahead of the owner, along the watched heading, the aim point lies.
        Vector AimDir;
        VecNormalize3(&Aim, &AimDir);                               // FUN_062e1680 into B+0x58
        double const Cos = ClampedDot3(WatchedFwd, AimDir);         // FUN_062e1630
        double const AimLen = std::sqrt((static_cast<double>(Aim.x) * Aim.x + static_cast<double>(Aim.z) * Aim.z) +
                                        static_cast<double>(Aim.y) * Aim.y);
        float const Closing = static_cast<float>(AimLen * Cos);     // 062d61b5 B+4

        // 062d61bb-062d61f5: get_speed(1.0f, type) is called and its result popped
        // unread (FSTP ST0 at 062d61f5); Speed2c takes the matching cached speed.
        if (bWatchedCruise)
        {
            (void)Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipCruise);
            Speed2c = Speed4c;
            Mode = 4;
        }
        else
        {
            (void)Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipEngine);
            Speed2c = Speed50;
            Mode = 1;
        }

        ThrottleBias30 = 0.0f;                                      // 062d61fe
        // 062d61fa-062d6211: `FLD |Closing|; FLD [+0x54]; FCOMPP; TEST AH,0x5; JP` --
        // the bias block runs only when ClosingWindow54 < |Closing|.
        if (ClosingWindow54 < std::fabs(Closing))
        {
            ThrottleBias30 = static_cast<float>(static_cast<double>(Closing) / Speed2c);   // 062d6217-062d621e
            // 062d6221-062d6250: three `TEST AH,0x41; JNZ` gates, each needs greater.
            if (static_cast<double>(WatchedSpeed) > 0.01 && OwnSpeed > WatchedSpeed && Closing > 0.0f)
            {
                // 062d6252-062d6278: time to slow from the own speed to the watched one,
                // and the distance covered meanwhile (FST into +0x2c).
                ThrustEquipType const Type = static_cast<ThrustEquipType>(Mode);
                float const SlowTime = Behavior_QueryRadiusEffect(Owner, 0.0f, OwnSpeed, WatchedSpeed, Type);
                float const SlowDistance = Behavior_QueryFalloffEffect(Owner, 0.0f, OwnSpeed, SlowTime, Type);
                Speed2c = SlowDistance;
                // 062d627d-062d62a1: `FLD Closing; FCOMP; TEST AH,0x41; JP` falls through on
                // Closing <= SlowDistance; then `FCOMP 0.01; TEST AH,0x41; JNZ` needs
                // SlowDistance > 0.01 to drop the bias.
                if (Closing <= SlowDistance && static_cast<double>(SlowDistance) > 0.01)
                    ThrottleBias30 = 0.0f;
            }
            // LAB_062d62a5: `TEST AH,0x5; JP` falls through on Closing < ClosingFloor40.
            if (Closing < ClosingFloor40)
                ThrottleBias30 = ClosingBias44;
        }

        // LAB_062d62b9: FLD MatchThrottle; FADD bias; FST B+4; negative -> 0.
        Throttle = static_cast<float>(static_cast<double>(MatchThrottle) + ThrottleBias30);
        if (Throttle < 0.0f)
            Throttle = 0.0f;
    }

    // LAB_062d62d9.
    if (bWatchedCruise)
    {
        Mode = 2;
        // 062d62e1-062d6308: `TEST AH,0x41; JNZ` needs MatchThrottle > 0.8f, then `TEST
        // AH,0x5; JP` falls through on Throttle < 0.8f.
        if (MatchThrottle > 0.8f && Throttle < 0.8f)
            Throttle = 0.8f;                                        // 0x3f4ccccd
    }
    else
    {
        Mode = 0;                                                   // LAB_062d6312
    }

    // LAB_062d6314: target behind the bearing cone and more than 200 out -> go straight
    // to the gate with the throttle (Mode 2) or with 0.
    if (BearingAngleToPoint(Owner, Position) > MaxBearing34 &&                 // FUN_062e1c90
        GetSignedRadialClearanceToPoint(Nav, Position) > 200.0f)              // FUN_062e4ea0
    {
        Sys->Thrust.SetThrottle((Mode == 2) ? Throttle : 0.0f, Mode);   // LAB_062d63e6
    }
    else
    {
        // LAB_062d6362: catch up when too far behind.
        if (Flag3c != 0 && Distance > CatchUpRange58)
        {
            Flag3d = 1;
            Throttle = 1.2f;                                        // 0x3f99999a
            Mode = 2;
        }
        // LAB_062d6388: afterburner notify.
        if (Flag3b != 0 || Sys->Timers.IsTimer4Active())   // FUN_062e8850
        {
            if (!Sys->SelfTarget.Iface->is_player() && Mode == 0)   // 062d63b0 / 062d63ba
                Sys->Thrust.SetThrustersActive(Throttle > 2.0f);       // 062d63be-062d63db
        }
        else
        {
            Sys->Thrust.SetThrustersActive(false);                 // LAB_062d63d3
        }
        Sys->Thrust.SetThrottle(Throttle, Mode);                   // LAB_062d63e0
    }

    // 062d63f1-062d640c: no nudge matching while PathfindManager::Field_214 bit 0 is set
    // or Flag3a is clear.
    if ((MatchPathfindOf(Context)->Field_214 & 1) != 0 || Flag3a == 0)
        return 0;

    // 062d6412-062d6464: the watched ship's nudge vector (slot 21) and its length,
    // (z*z + y*y) + x*x, FST to B+0x18 and compared unrounded with 0.1f.
    Vector Nudge{ 0.0f, 0.0f, 0.0f };
    Watched->get_nudge_vec(Nudge);
    double const NudgeLenD = std::sqrt((static_cast<double>(Nudge.z) * Nudge.z + static_cast<double>(Nudge.y) * Nudge.y) +
                                       static_cast<double>(Nudge.x) * Nudge.x);
    float const NudgeLen = static_cast<float>(NudgeLenD);
    if (!(NudgeLenD > 0.1f))
    {
        Vector const Zero{ 0.0f, 0.0f, 0.0f };
        Sys->Thrust.SetNudgeDirection(Zero);                        // LAB_062d64f7
        return 0;
    }

    // 062d6475-062d64eb: watched speed for that nudge (slot 0xd8, type 3), the own
    // throttle for that speed (slot 0xd4, type 3), along the unit nudge, into
    // IObjAffect::set_nudge_vec (Object+4, slot 2). Direct vcalls, no null tests.
    float const NudgeSpeed = Watched->get_speed(NudgeLen, ThrustEquipTradelane);
    float const NudgeThrottle = Owner->get_projected_throttle(NudgeSpeed, ThrustEquipTradelane);
    Vector NudgeDir;
    VecNormalize3(&Nudge, &NudgeDir);
    Vector Out;
    Out.x = NudgeThrottle * NudgeDir.x;
    Out.y = NudgeThrottle * NudgeDir.y;
    Out.z = NudgeThrottle * NudgeDir.z;
    static_cast<IObjAffect*>(Owner)->set_nudge_vec(Out);
    return 0;
}
