// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_trailbehavior.h"

#include "common_aisteer.h"        // FAiTarget, GetIfaceVectorMagnitude, GetIfacePositionOrZero,
                                   // ClampedDot3, BearingAngleToIface, IsAlignedAndOutsideHeadingCone,
                                   // GetInterceptClearanceOfIface, GetObjectTypeOrZero
#include "common_csys.h"           // Transform, Acquire3DMathEngine
#include "common_iface.h"          // IObjInspect, IObjRW, IBehaviorManager, ObjInspectOf, ObjRwFromWatchable
#include "common_misc.h"           // PlayerTrailAvailable
#include "common_pub.h"            // pub::AI::ContentCallback, TrailUseStruct
#include "common_systemmanager.h"  // SystemManager -- what NavHelper points at
#include "x86math.h"               // I3DMathEngine::TransformMatrix (+0x2c)

#include <cmath>
#include <cstring>


// TrailBehavior -- see common_trailbehavior.h. Every method re-ported from
// EXE/Disasm/Common.dll.txt 2026-10-08 (claude-code:cmn-ai5).

// FUN_062e74a0 (item 1798) is SystemManagerTimerTable::FindVerifiedObject.

namespace
{
    // .data:DAT_063fc538, the 3DMathEngine pointer Slot7 fetches lazily through FUN_06391cf0.
    I3DMathEngine* GTrailMathEngine = nullptr;

    // SystemManager+0x14fc, the owner's own FAiTarget record.
    FAiTarget* NavOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    pub::AI::Personality::TrailUseStruct const& TrailParamsOf(SystemManager* Sys)
    {
        return Sys->PersonalityData.TrailParams;
    }

    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The inlined get_type refresh on the FAiTarget (`TEST [Sys+0x1579]; ... CALL [EDX+0x88];
    // MOV [EDI+0x7d],1` at .text:062ca89b-062ca8c0): the flag is raised after the call.
    unsigned RefreshNavFlags(FAiTarget* Nav)
    {
        if (Nav->bFlagsValid == 0)
        {
            Nav->Iface->get_type(Nav->CachedFlags);
            Nav->bFlagsValid = 1;
        }
        return Nav->CachedFlags;
    }

    // `MOV EAX,[ExternalIface]; CALL [EAX+0xbc]`.
    bool OwnShipIsPlayer(SystemManager* Sys)
    {
        return Sys->SelfTarget.Iface->is_player();
    }

    // The [-1, 1] clamp set_direction applies to both axes (.text:062cb1d7-062cb27b): `TEST
    // AH,0x41; JNZ` sends <= -1 (and unordered) to -1, `TEST AH,0x5; JP` sends >= 1 to 1, and
    // the third compare re-tests the lower bound on a path where it can no longer hold.
    float ClampUnit(float Value)
    {
        if (!(Value > -1.0f))
            return -1.0f;
        if (!(Value < 1.0f))
            return 1.0f;
        return Value;
    }
}

// ----------------------------------------------------------------------------
// FUN_062ca350 (.text:062ca350-062ca496).
// ----------------------------------------------------------------------------
TrailBehavior::TrailBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContext(nullptr)
    , Opaque18{}
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , MatchThrottle()               // 062ca381 FUN_062d5aa0
    , Face()                        // 062ca391 FUN_062d6750
    , Move()                        // 062ca3a1 FUN_062d4e60
    , Afterburner()                 // 062ca3b1 FUN_062da500
    , DirectiveTarget()             // 062ca3b6-062ca3be, then the no-op set_pointer guard
    , LeaderId1dc(0)                // 062ca449
    , bHasDirectiveTarget1e0(0)     // not written by retail's ctor; SetContext clears it
    , Pad1e1{}
    , TrailOffset1e4(0.0f)          // 062ca3cf
    , TrailDistance1e8(50.0f)       // 062ca3d5 0x42480000
    , bLagging1ec(0)                // not written by retail's ctor; Slot3 clears it
    , Pad1ed{}
    , FacingCos1f0(static_cast<float>(std::cos(static_cast<double>(0.5235987901687622))))   // 062ca3c1 FCOS, 062ca454 FSTP
    , LagTimer1f4(0.0f)             // not written by retail's ctor; Slot3 clears it
    , AxisTimer1f8(0.0f)            // not written by retail's ctor; Slot3 clears it
    , LagTime1fc(2.5f)              // 062ca415 0x40200000
    , AxisPause200(5.0f)            // 062ca429 0x40a00000
    , AxisTime204(3.0f)             // 062ca41f 0x40400000
    , AxisZ208(0.0f)                // 062ca433
    , bAfterburner20c(0)            // 062ca439
    , Pad20d{}
    , Scale210(1.0f)                // 062ca43f 0x3f800000
    , bAxisArmed214(0)              // 062ca468
    , Pad215{}
    , SightTimer218(0.0f)           // not written by retail's ctor; Slot3 clears it
    , SightTime21c(4.0f)            // 062ca3f3 0x40800000
    , TrailExtent220(0.0f)          // not written by retail's ctor; Slot7 writes it
    , Field224(0.0385f)             // overwritten below with the exact bits
    , Field228(400.0f)              // 062ca3e9 0x43c80000
    , bDirectionSet22c(0)           // 062ca3fd
    , Pad22d{}
    , DirectionX230(0.0f)           // 062ca403
    , DirectionY234(0.0f)           // 062ca409
    , DirectionZ238(0.0f)           // 062ca40f
    , bMatchThrottle23c(0)          // 062ca46e
    , Pad23d{}
    , Field240(20.0f)               // 062ca474 0x41a00000
    , CatchUpRange244(1500.0f)      // 062ca47e 0x44bb8000
{
    ConstructBase(InTypeId);        // 062ca373 FUN_062d2020
    unsigned const Field224Bits = 0x3d1de9e7u;   // 062ca3df
    std::memcpy(&Field224, &Field224Bits, sizeof(Field224));
}

// FUN_062ca4c0 (FUN_062ca4a0 is the scalar-deleting thunk): the four set_pointer(0) calls are
// the members' own BaseWatcher destructors, then FUN_062d2080 (~Behavior).
TrailBehavior::~TrailBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062ca570 (.text:062ca570-062ca63a).
// ----------------------------------------------------------------------------
bool TrailBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // 062ca585 FUN_062d2090
    if (!bOk)
        return bOk;

    // Every idea gets (Object = +0x14, Handle = +0x24, Context = Manager). Results unread.
    bHasDirectiveTarget1e0 = 0;                                     // 062ca5ac
    Face.SetContext(OwnerContext, NavHelper, Manager);              // 062ca5b3
    MatchThrottle.SetContext(OwnerContext, NavHelper, Manager);     // 062ca5c6
    MatchThrottle.SetMaxBearing(3.14159274f);                       // 062ca5cf .text:063275d0, 0x40490fdb
    MatchThrottle.SetCatchUpRange(CatchUpRange244);                 // 062ca5dd FUN_062d6740
    if (!ObjInspectOf(OwnerContext)->is_player())                   // 062ca5e7 [+0xbc] on the owner
        MatchThrottle.SetFlag3b(true);                              // 062ca5f5 FUN_062d6730
    Move.SetContext(OwnerContext, NavHelper, Manager);              // 062ca611
    Move.SetMode(1);                                                // 062ca617 FUN_062d5980
    Afterburner.SetContext(OwnerContext, NavHelper, Manager);       // 062ca631
    return bOk;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062ca640 (.text:062ca640-062ca7a3).
// ----------------------------------------------------------------------------
void TrailBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062ca646 FUN_062d20d0
    SetCruiseLock(1, 3);                                            // 062ca651 FUN_062d21d0

    // 062ca656-062ca67b: the owner's target, else the directive target.
    IObjRW* Leader = NavHelper->Targeting.GetTarget();              // FUN_062ed5a0
    if (Leader == nullptr)
        Leader = WatchedObject(DirectiveTarget);

    bLagging1ec = 0;                                                // 062ca680
    LagTimer1f4 = 0.0f;                                             // 062ca686
    AxisTimer1f8 = 0.0f;                                            // 062ca68c

    if (OwnShipIsPlayer(NavHelper))                                 // 062ca6a1
    {
        // 062ca6ab-062ca6c4: the owner's own cruise state into MatchThrottle's +0x38.
        bool bCruise = false;
        static_cast<IObjRW*>(OwnerContext)->is_cruise_active(bCruise);   // [+0x60]
        MatchThrottle.SetFlag38(bCruise);                           // FUN_062d6700
    }

    bDirectionSet22c = 0;                                           // 062ca6cb
    DirectionX230 = 0.0f;
    DirectionY234 = 0.0f;
    DirectionZ238 = 0.0f;
    SightTimer218 = 0.0f;                                           // 062ca6e3

    if (Leader != nullptr)
    {
        Move.PublishTargetId(Leader->get_id());                     // 062ca6f9 FUN_062d4e50
        Face.PublishTargetId(Leader->get_id());                     // 062ca70c
        LeaderId1dc = Leader->get_id();                             // 062ca718
    }
    else
    {
        Move.PublishTargetId(0);                                    // 062ca727
        LeaderId1dc = 0;                                            // 062ca72c
    }

    // 062ca732-062ca79a: the personality's TrailUseStruct (Sys+0x16ac).
    pub::AI::Personality::TrailUseStruct const& P = TrailParamsOf(NavHelper);
    FacingCos1f0 = static_cast<float>(std::cos(static_cast<double>(P.TrailLockConeAngle)));   // FLD; FCOS; FSTP
    LagTime1fc = P.TrailBreakTime;
    AxisTime204 = P.TrailMinNoLockTime;
    AxisZ208 = P.TrailBreakRollThrottle;
    bAfterburner20c = P.bTrailBreakAfterburner;
    Scale210 = P.TrailMaxTurnThrottle;
    Face.SetScale94(P.TrailMaxTurnThrottle);                        // 062ca78e FUN_062d9490
    bAxisArmed214 = 0;                                              // 062ca794
    bMatchThrottle23c = 0;                                          // 062ca79a
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062ca7b0 (.text:062ca7b0-062ca811).
// ----------------------------------------------------------------------------
void TrailBehavior::Slot4()
{
    bHasDirectiveTarget1e0 = 0;                                     // 062ca7b9
    DirectiveTarget.set(nullptr);                                   // 062ca7c0-062ca7c8
    Face.Commit();                                                  // 062ca7d9
    MatchThrottle.SetFlag39(true);                                  // 062ca7e3 FUN_062d6710
    MatchThrottle.Commit();                                         // 062ca7ec
    Move.Commit();                                                  // 062ca7fb
    Afterburner.Commit();                                           // 062ca80a
    Behavior::Slot4();                                              // 062ca811 JMP FUN_062d20f0
}

// ----------------------------------------------------------------------------
// Slot 5, LAB_062ca820 (.text:062ca820-062ca936).
// ----------------------------------------------------------------------------
int TrailBehavior::Slot5(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = NavHelper;
    int Result = 2;                                                 // 062ca82f EBP

    if (!Sys->GunConvergenceStateBytes.FollowerDirectiveGateA())     // 062ca834 FUN_062ecb20
    {
        IObjRW* const Leader = Sys->Targeting.GetTarget();          // 062ca84a
        if (Leader != nullptr)
        {
            // 062ca859-062ca87b: FUN_062e61a0 on a fresh GetTarget(); `FCOMP 20.0f; TEST
            // AH,0x1; JNZ` leaves on less (or unordered).
            if (GetIfaceVectorMagnitude(Sys->Targeting.GetTarget()) >= 20.0f)
            {
                // 062ca881-062ca898: the leader's type; a 0x380000 type gives up at once.
                // The local is not pre-set in retail (get_type always writes it).
                unsigned LeaderType = 0;
                Leader->get_type(LeaderType);
                if ((LeaderType & 0x380000u) != 0)
                {
                    ModeSelector0xc = Flag29 ? 3 : Result;          // LAB_062ca917
                    return ModeSelector0xc;
                }
                if ((RefreshNavFlags(NavOf(Sys)) & 0x7f0000u) != 0)   // 062ca8c4
                    Result = 1;
                // 062ca8d3-062ca8eb: FUN_062e1aa0(Leader, Owner); `FCOMP pi/2 (.rdata:0639f5d4);
                // TEST AH,0x5; JP` leaves on >= (or unordered).
                if (BearingAngleToIface(Leader, OwnerContext) < 1.5707964f)
                {
                    ModeSelector0xc = Flag29 ? 3 : 2;               // 062ca8ed-062ca8fe
                    return ModeSelector0xc;
                }
            }
        }
        if (bHasDirectiveTarget1e0 != 0)                            // LAB_062ca908
            Result = 1;
    }

    ModeSelector0xc = Flag29 ? 3 : Result;                          // LAB_062ca917
    return ModeSelector0xc;
}

// ----------------------------------------------------------------------------
// Slot 6, LAB_062ca940 (.text:062ca940-062caa2f).
// ----------------------------------------------------------------------------
EvaluateResult TrailBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    if (!PlayerTrailAvailable)                                      // 062ca940
        return EvalSkip;

    IObjRW* const Owner = static_cast<IObjRW*>(OwnerContext);
    IObjRW* Target = nullptr;                                       // 062ca95b
    Owner->get_target(Target);                                      // 062ca966 [+0x74]
    if (Target == nullptr)
        return EvalSkip;

    bool bInLane = false;                                           // 062ca97c
    Owner->is_using_tradelane(&bInLane);                            // 062ca984 [+0x134]
    if (bInLane)
        return EvalSkip;

    // 062ca9a4-062ca9c5: get_type must succeed and name a ship-class type outside 0x200000.
    unsigned TargetType = 0;                                        // not pre-set in retail
    if (Target->get_type(TargetType) != 0)                          // [+0x88]
        return EvalSkip;
    if ((TargetType & 0x7f0000u) == 0 || (TargetType & 0x200000u) != 0)
        return EvalSkip;

    // 062ca9c7-062ca9de: behaviors 0xe, 0x11 and 8 cannot be trailed.
    int const TargetBehavior = Target->get_behavior_id();           // [+0x148]
    if (TargetBehavior == 0xe || TargetBehavior == 0x11 || TargetBehavior == 8)
        return EvalSkip;

    // 062ca9e0-062ca9f9: FUN_062e6200 re-reads the type the null-safe way.
    unsigned const TypeAgain = GetObjectTypeOrZero(Target);
    if ((TypeAgain & 0x7f0000u) == 0 || (TypeAgain & 0x600de0u) != 0)
        return EvalSkip;

    NavHelper->Targeting.RetargetOwner(Target, SUBOBJ_ID_NONE);     // 062caa13 FUN_062ee940
    return EvalBlocked;                                             // 062caa19, 1
}

// ----------------------------------------------------------------------------
// Slot 8, LAB_062caa40 (.text:062caa40-062caa59).
// ----------------------------------------------------------------------------
void TrailBehavior::Slot8()
{
    if (bMatchThrottle23c != 0)
        MatchThrottle.Slot2(DeltaTime);                             // 062caa56 [EDX+0x8]
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062caa60 (.text:062caa60-062cb10f). Frame names B+n are [ESP0+n], ESP0 = ESP
// after `SUB ESP,0x68`.
// ----------------------------------------------------------------------------
int TrailBehavior::Slot7()
{
    if (OwnerContext == nullptr)                                    // 062caa67
    {
        UpdateResult10 = 3;
        return 3;
    }

    SystemManager* const Sys = NavHelper;
    if (Sys->Timers.IsTimer0Active())                              // 062caa86 FUN_062e8810
    {
        UpdateResult10 = 2;
        return 2;
    }

    // B+2: set when the leader comes from the TargetManager rather than the directive.
    bool bTrailTarget = false;                                      // 062caaa7
    IObjRW* Leader = nullptr;                                       // EBX
    if (bHasDirectiveTarget1e0 != 0 && WatchedObject(DirectiveTarget) != nullptr)   // 062caa9f-062caabd
    {
        Leader = WatchedObject(DirectiveTarget);                    // 062caabf-062caacd
    }
    else
    {
        // LAB_062caad2.
        if (Sys->Targeting.GetTarget() == nullptr)
        {
            UpdateResult10 = 2;                                     // LAB_062cab51
            return 2;
        }
        Leader = Sys->Targeting.GetTarget();                        // 062caaed
        // 062caaf4-062cab36: an AI owner gives up on a target slower than 20 (FUN_062e61a0;
        // `TEST AH,0x5; JP` stays on >= or unordered).
        if (!OwnShipIsPlayer(Sys) && Leader != nullptr &&
            GetIfaceVectorMagnitude(Sys->Targeting.GetTarget()) < 20.0f)
        {
            UpdateResult10 = 3;                                     // 062cab38
            return 3;
        }
        bTrailTarget = true;                                        // LAB_062cab48
    }
    if (Leader == nullptr)                                          // LAB_062cab4d
    {
        UpdateResult10 = 2;
        return 2;
    }

    // LAB_062cab61: everyone watches the leader's id.
    MatchThrottle.PublishTargetId(Leader->get_id());                // 062cab6e FUN_062d4e50
    Face.PublishTargetId(Leader->get_id());                         // 062cab83
    Move.PublishTargetId(Leader->get_id());                         // 062cab98
    if (Leader->get_id() != LeaderId1dc)                            // 062caba4
        Slot3();                                                    // 062cabb1 [EAX+0xc]
    LeaderId1dc = Leader->get_id();                                 // 062cabbb

    if (bTrailTarget && !OwnShipIsPlayer(Sys))                      // 062cabc1-062cabe7
    {
        FAiTarget* const Nav = NavOf(Sys);
        Nav->RefreshPos();                                          // 062cabed-062cac14

        // 062cac17-062cac49: unit vector from the own centre to the leader's centre.
        Vector LeaderCenter;
        GetIfacePositionOrZero(&LeaderCenter, Leader);              // FUN_062e60e0 into B+0x20
        Vector ToLeader;
        ToLeader.x = LeaderCenter.x - Nav->CachedPos.x;
        ToLeader.y = LeaderCenter.y - Nav->CachedPos.y;
        ToLeader.z = LeaderCenter.z - Nav->CachedPos.z;
        Vector ToLeaderDir;
        VecNormalize3(&ToLeader, &ToLeaderDir);                     // FUN_062e1680 into B+0x38

        // 062cac4e-062cac9a: the own forward (column 2 negated) and the facing cosine (B+4).
        Transform const& OwnXf = Sys->SelfTarget.Iface->get_transform();
        Vector OwnFwd;
        OwnFwd.x = -OwnXf.Rotation.E[0][2];
        OwnFwd.y = -OwnXf.Rotation.E[1][2];
        OwnFwd.z = -OwnXf.Rotation.E[2][2];
        float const FacingCos = static_cast<float>(ClampedDot3(ToLeaderDir, OwnFwd));   // FUN_062e1630

        // 062cac9e-062cad5f: the "face the axis" pause.
        if (bAxisArmed214 != 0 && bLagging1ec == 0 &&
            static_cast<double>(AxisPause200) + AxisTime204 > AxisTimer1f8)          // `TEST AH,0x41; JNZ`
        {
            if (AxisTimer1f8 > AxisTime204)                         // 062cacda-062caceb
            {
                AxisTimer1f8 = static_cast<float>(static_cast<double>(AxisTimer1f8) + DeltaTime);
                bAxisArmed214 = 0;                                  // 062cad09
                Vector Axis;
                Axis.x = 0.0f;
                Axis.y = 0.0f;
                Axis.z = AxisZ208;                                  // 062cad10-062cad22
                Face.SetAxis(Axis);                                 // 062cad2a FUN_062d75e0
                Face.Update(DeltaTime);                             // 062cad37
                UpdateResult10 = 1;
                return 1;
            }
            AxisTimer1f8 = static_cast<float>(static_cast<double>(DeltaTime) + AxisTimer1f8);   // LAB_062cad4a
        }
        else
        {
            AxisTimer1f8 = 0.0f;                                    // LAB_062cad5d
        }

        // LAB_062cad65: lagging behind (facing away) -- turn to the axis for LagTime1fc.
        // `TEST AH,0x5; JP` stays on FacingCos < FacingCos1f0 and on LagTimer1f4 < LagTime1fc.
        if (bLagging1ec != 0 && FacingCos < FacingCos1f0 && LagTimer1f4 < LagTime1fc)
        {
            LagTimer1f4 = static_cast<float>(static_cast<double>(DeltaTime) + LagTimer1f4);
            Vector Axis;
            Axis.x = 0.0f;
            Axis.y = 0.0f;
            Axis.z = AxisZ208;                                      // 062cadbb-062cadd1
            Face.SetAxis(Axis);                                     // 062cadd5
            if (bAfterburner20c != 0)
                Afterburner.Update(DeltaTime);                      // 062cadf4
            Face.Update(DeltaTime);                                 // 062cadff
            Move.SetMode(0);                                        // 062cae05 FUN_062d5980
            Move.SetScale40(1.0f);                                  // 062cae11 .text:062a33e0
            Move.SetDirectionFrom(&Sys->SelfTarget.Iface->get_transform());   // 062cae2e FUN_062d58f0
            Move.SetAngle38(3.14159274f);                           // 062cae3a .text:063275f0
            Move.Update(DeltaTime);                                 // 062cae47
            UpdateResult10 = 0;
            return 0;
        }

        // LAB_062cae57.
        Afterburner.Commit();                                       // 062cae63
        LagTimer1f4 = 0.0f;                                         // 062cae70
        if (FacingCos > FacingCos1f0)                               // `TEST AH,0x41; JNZ`
        {
            bLagging1ec = 1;
            bAxisArmed214 = 1;
        }
        else
        {
            bLagging1ec = 0;                                        // LAB_062cae8d
        }

        // LAB_062cae94: once the leader has stayed in sight SightTime21c seconds, tell the
        // content callback (IBehaviorManager+0x110, ContentCallback slot 3).
        if (bLagging1ec != 0)
        {
            bool const bWasBelow = SightTimer218 < SightTime21c;    // `TEST AH,0x5; JP` -> CL
            SightTimer218 = static_cast<float>(static_cast<double>(DeltaTime) + SightTimer218);   // FST
            if (SightTimer218 > SightTime21c && bWasBelow)
            {
                pub::AI::ContentCallback* const Callback = Sys->Owner->ContentCbPtr;   // [[Sys+4]+0x110]
                if (Callback != nullptr)
                    Callback->ship_in_sights_event(Leader->get_id());   // 062caef1 [ESI+0xc]
            }
        }
        else
        {
            SightTimer218 = 0.0f;                                   // LAB_062caef6
        }
    }

    // LAB_062caefc: the trail point, TrailOffset1e4 behind the leader along its own axis.
    if (!OwnShipIsPlayer(Sys))
        TrailDistance1e8 = TrailParamsOf(Sys).TrailDistance;        // 062caf21 Sys+0x16c4
    TrailOffset1e4 = static_cast<float>(
        static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Leader)) + TrailDistance1e8);   // FUN_062e6010
    TrailExtent220 = static_cast<float>(
        static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Leader)) + TrailOffset1e4);

    bool bOwnCruise = false;                                        // B+3, never read again
    static_cast<IObjRW*>(OwnerContext)->is_cruise_active(bOwnCruise);   // 062caf5d [+0x60]

    Matrix const LeaderRotation = Leader->get_transform().Rotation; // 062caf61 FUN_062e61f0, REP MOVSD 9
    Vector LeaderCenter;
    GetIfacePositionOrZero(&LeaderCenter, Leader);                  // 062caf79 into B+0x2c

    Vector Local;                                                   // B+8
    Local.x = 0.0f;
    Local.y = 0.0f;
    Local.z = TrailOffset1e4;
    if (GTrailMathEngine == nullptr)                                // 062caf7e-062cafa9
        GTrailMathEngine = Acquire3DMathEngine();
    Vector Offset;                                                  // B+0x20
    GTrailMathEngine->TransformMatrix(reinterpret_cast<FVector&>(Offset),
                                      reinterpret_cast<FMatrix const&>(LeaderRotation),
                                      reinterpret_cast<FVector const&>(Local));   // 062cafc0 [EDX+0x2c]
    Vector TrailPoint;                                              // B+8
    TrailPoint.x = Offset.x + LeaderCenter.x;
    TrailPoint.y = Offset.y + LeaderCenter.y;
    TrailPoint.z = Offset.z + LeaderCenter.z;

    // 062cafc3-062cb01c: the MatchThrottle2Idea drives when the leader flies the same way and
    // the own ship lies outside its forward cone (FUN_062e5190 with cos 0).
    bMatchThrottle23c = IsAlignedAndOutsideHeadingCone(NavOf(Sys), Leader, 0.0f) ? 1 : 0;
    BaseIdea* Driver;
    if (bMatchThrottle23c != 0)
    {
        MatchThrottle.SetWatchAndPosition(Leader, TrailPoint);      // 062cb029 FUN_062d65f0
        Driver = &MatchThrottle;
    }
    else
    {
        Move.SetMode(0);                                            // 062cb03a FUN_062d5980
        Move.SetScale40(1.0f);                                      // 062cb046
        Move.SetWatchObject(Leader);                                // 062cb04e FUN_062d5760
        Move.SetAngle38(3.14159274f);                               // 062cb05a
        Move.SetRange3c(TrailDistance1e8);                          // 062cb068 .text:062a66b0
        Driver = &Move;
    }
    Driver->Update(DeltaTime);                                      // LAB_062cb06d [EDX+0x4]

    // 062cb078-062cb0b1: FUN_062e4bb0 for effect (FSTP ST0) unless a player owner has the
    // camera-level status cleared.
    if (!OwnShipIsPlayer(Sys) || BehaviorMgrPtr->get_camera_level_status())
        (void)GetInterceptClearanceOfIface(NavOf(Sys), Leader);

    // LAB_062cb0b4: face the leader, upright when the timer table asks for it.
    Face.SetWatchObject(Leader);                                    // 062cb0bd FUN_062d7510
    if (Sys->Timers.IsTimer5Active())                              // 062cb0c8 FUN_062e8840
    {
        Vector Up;
        Up.x = 0.0f;
        Up.y = 0.0f;
        Up.z = 1.0f;
        Face.SetUpVector(Up);                                       // 062cb0f0 FUN_062d7640
    }
    Face.Update(DeltaTime);                                         // 062cb0fd
    UpdateResult10 = 1;
    return 1;
}

// ----------------------------------------------------------------------------
// Slot 10, LAB_062cb110 (.text:062cb110-062cb16c).
// ----------------------------------------------------------------------------
bool TrailBehavior::Slot10(void* DirectiveOp)
{
    if (DirectiveOp != nullptr)
    {
        unsigned char* const Op = static_cast<unsigned char*>(DirectiveOp);
        TrailDistance1e8 = *reinterpret_cast<float const*>(Op + 0x10);   // 062cb11e DirectiveTrailOp+0x10
        void* const Target = NavHelper->Timers.FindVerifiedObject((unsigned const*)(Op + 0xc));   // 062cb12e
        if (Target != nullptr)
        {
            DirectiveTarget.set(reinterpret_cast<Watchable const*>(static_cast<char*>(Target) + 8));   // 062cb137-062cb14b
            bHasDirectiveTarget1e0 = 1;                             // 062cb158
            Flags28 = static_cast<unsigned char>(Flags28 | 0x2);    // 062cb15e
            return true;
        }
    }
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);           // LAB_062cb165
    return false;
}

// ----------------------------------------------------------------------------
// Slot 12, FUN_062cb170 (.text:062cb170-062cb29c).
// ----------------------------------------------------------------------------
void TrailBehavior::set_direction(Vector const& Dir)
{
    bDirectionSet22c = 1;                                           // 062cb17b
    DirectionX230 = -Dir.y;                                         // 062cb182-062cb187
    DirectionZ238 = 0.0f;                                           // 062cb18f
    DirectionY234 = -Dir.x;                                         // 062cb18d-062cb19b

    // 062cb1a1-062cb1d3: FUN_062d4b20; `FCOM 0.0 (double); TEST AH,0x41; JNZ` rescales only
    // a strictly positive factor, by 1/factor kept on the x87 stack.
    float const Factor = BehaviorMgrPtr->get_turn_sensitivity_factor();
    if (Factor > 0.0f)
    {
        double const Rescale = 1.0 / static_cast<double>(Factor);
        DirectionX230 = static_cast<float>(Rescale * DirectionX230);
        DirectionY234 = static_cast<float>(Rescale * DirectionY234);
    }
    DirectionX230 = ClampUnit(DirectionX230);                       // LAB_062cb1d7
    DirectionY234 = ClampUnit(DirectionY234);                       // 062cb22c

    // 062cb281-062cb294: a positive z also sets the trail distance (`TEST AH,0x41; JNZ`).
    if (Dir.z > 0.0f)
        TrailDistance1e8 = Dir.z;
}

// ----------------------------------------------------------------------------
// Slot 14, LAB_062cb2a0: forwards to MatchThrottle2Idea::SetFlag38 (FUN_062d6700).
// ----------------------------------------------------------------------------
void TrailBehavior::set_cruise(bool bOn)
{
    MatchThrottle.SetFlag38(bOn);
}
