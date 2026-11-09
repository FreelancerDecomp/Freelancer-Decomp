// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_evadebehavior.h"

#include "common_aisteer.h"        // IsPointOutsideHeadingCone, IsWithinInterceptRangeOfIface,
                                   // GetIfaceVectorMagnitude
#include "common_csys.h"           // Transform
#include "common_iface.h"          // IObjInspect, IObjRW, ObjInspectOf, ObjRwFromWatchable
#include "common_iustategraph.h"   // FindClosestHostileTarget
#include "common_systemmanager.h"  // SystemManager -- what NavHelper points at

#include <cmath>
#include <cstdint>
#include <cstdlib>


// EvadeBehavior -- see common_evadebehavior.h. Every method re-ported from
// EXE/Disasm/Common.dll.txt 2026-10-08 (claude-code:cmn-ai4).

// FUN_062e74a0 (item 1798) is SystemManagerTimerTable::FindVerifiedObject.

namespace
{
    // .rdata:063997d0 == 0x38000000 == 1/32768, the rand() scale every pick below uses.
    float const GRandScale = 3.0517578125e-05f;

    // `[ESI+0x2a0]` with the family's `Ptr ? Ptr - 8 : 0` downcast.
    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // `MOV EAX,[W]; TEST EAX,EAX; JZ; ADD EAX,-8; TEST EAX,EAX; JZ` -- watched and not the
    // 8 sentinel.
    bool IsWatching(BaseWatcher const& Watcher)
    {
        return WatchedObject(Watcher) != nullptr;
    }

    // The watched object (and only it) must lie outside the owner's heading cone:
    // `PUSH Cos; PUSH [ESI+0x14]; CALL [Watched->vtbl] (get_position); PUSH EAX;
    // CALL FUN_062e53e0`.
    bool WatchedOutsideCone(BaseWatcher const& Watcher, void* Owner, float Cos)
    {
        IObjRW* const Watched = WatchedObject(Watcher);
        return IsPointOutsideHeadingCone(Watched->get_position(),
                                         static_cast<IObjInspect const*>(static_cast<IObjRW*>(Owner)),
                                         Cos);
    }
}

// ----------------------------------------------------------------------------
// FUN_062c7790, .text:062c7790-062c786d.
// ----------------------------------------------------------------------------
EvadeBehavior::EvadeBehavior(int InTypeId)
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
    , Mode2c(0)                  // 062c7836
    , Dodge()                    // 062c77c1 FUN_062d77d0
    , BreakAway()                // 062c77d1 FUN_062d8970
    , DodgeMissile()             // 062c77e1 FUN_062d94b0
    , Face()                     // 062c77f1 FUN_062d6750
    , Afterburner()              // 062c7801 FUN_062da500
    , Watcher2a0()               // 062c7806-062c7821
    , DodgeTime2a8(0.0f)         // not written by the ctor; SetContext sets it
    , BreakTime2ac(3.0f)         // 062c784b 0x40400000
    , Elapsed2b0(0.0f)           // not written by the ctor; SetContext sets it
    , Flag2b4(0)                 // 062c782a
    , Flag2b5(0)                 // 062c7830
    , Flag2b6(0)                 // 062c7839
    , Flag2b7(0)                 // 062c783f
    , ConeCos2b8(0.0f)           // 062c7845 (dword 0)
    , ConeCos2bc(0.707f)         // 062c7855 0x3f34fdf4 (0.707f, not cos 45)
{
    ConstructBase(InTypeId);     // 062c77b3 FUN_062d2020
}

// FUN_062c7900 (FUN_062c78e0 is the scalar-deleting thunk): the five set_pointer(0)
// calls are the members' own BaseWatcher destructors (Watcher2a0, Face, DodgeMissile,
// BreakAway.Picker, Dodge.Picker -- reverse member order), then FUN_062d2080 (~Behavior).
EvadeBehavior::~EvadeBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062c79d0 (.text:062c79d0-062c7aa3).
// ----------------------------------------------------------------------------
bool EvadeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // 062c79e6 FUN_062d2090
    if (!bOk)
        return bOk;

    // Every idea gets (Object = +0x14, Handle = +0x24, Context = Manager): `PUSH EBP
    // (Manager); PUSH [ESI+0x24]; PUSH [ESI+0x14]; CALL [vtbl]`. Results unread.
    Dodge.SetContext(OwnerContext, NavHelper, Manager);             // 062c7a07
    Dodge.ConeAngle = 1.5707964f;                                   // 062c7a10 .text:062a66b0, 0x3fc90fdb
    Dodge.DodgeDistance = 50.0f;                                    // 062c7a1c .text:062a33d0, 0x42480000
    Dodge.IntervalTime = 6.0f;                                      // 062c7a28 .text:063275d0, 0x40c00000
    BreakAway.SetContext(OwnerContext, NavHelper, Manager);         // 062c7a42
    DodgeMissile.SetContext(OwnerContext, NavHelper, Manager);      // 062c7a59
    Face.SetContext(OwnerContext, NavHelper, Manager);              // 062c7a70
    Afterburner.SetContext(OwnerContext, NavHelper, Manager);       // 062c7a87

    DodgeTime2a8 = 10.0f;                                           // 062c7a89 0x41200000
    Elapsed2b0 = 0.0f;                                              // 062c7a93 (dword 0)
    return bOk;
}

// ----------------------------------------------------------------------------
// FUN_062c8690 (.text:062c8690-062c872a). r = rand() * (F04 + F08 + F0C + F10) / 32768
// with the sum stored to a float local first (062c86b1 FSTP float) and r kept on the
// x87 stack. Each `TEST AH,0x5; JP` skips on r >= bound (or unordered):
//   r < F04 -> 0;  r < F04 + F08 -> 1;  r < F04 + F08 + F0C -> 2 (the final `JNP`);  3.
// ----------------------------------------------------------------------------
int EvadeBehavior::PickDodgeStyle()
{
    pub::AI::Personality::EvadeDodgeUseStruct const& P = NavHelper->PersonalityData.EvadeDodgeParams;
    float const Sum = static_cast<float>(((static_cast<double>(P.EvadeDodgeStyleWeightWaggle) +
                                            P.EvadeDodgeStyleWeightWaggleRandom) +
                                           P.EvadeDodgeStyleWeightSlide) +
                                          P.EvadeDodgeStyleWeightCorkscrew);
    double const R = static_cast<double>(std::rand()) * Sum * GRandScale;   // 062c86b5-062c86ca

    double Bound = P.EvadeDodgeStyleWeightWaggle;                   // 062c86d0
    if (R < Bound)
        return 0;                                                   // 062c86e3
    Bound = Bound + P.EvadeDodgeStyleWeightWaggleRandom;            // 062c86ec
    if (R < Bound)
        return 1;                                                   // 062c86ff
    Bound = Bound + P.EvadeDodgeStyleWeightSlide;                   // 062c870b
    if (R < Bound)
        return 2;                                                   // 062c871a
    return 3;                                                       // 062c8721
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062c8260 (.text:062c8260-062c85d5).
// ----------------------------------------------------------------------------
void EvadeBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062c8269 FUN_062d20d0
    Mode2c = 0;                                                     // 062c8276
    SetCruiseLock(1, 3);                                            // 062c8279 FUN_062d21d0
    Elapsed2b0 = 0.0f;                                              // 062c827e (dword 0)

    Dodge.Picker.SetWatchObject(WatchedObject(Watcher2a0));         // 062c829b FUN_062d88e0
    BreakAway.Picker.SetWatchObject(WatchedObject(Watcher2a0));     // 062c82ba FUN_062d88e0

    if (NavHelper->SelfTarget.Iface->is_player())                                 // 062c82d1
    {
        // 062c82db-062c8303: a 1.0f local, IObjInspect::get_throttle (+0x4c) on the owner
        // (result unread), then FUN_062d9490 on Dodge (+0x94) and .text:062a33e0 on
        // BreakAway (+0x40) with the local.
        float Throttle = 1.0f;
        static_cast<IObjInspect const*>(static_cast<IObjRW*>(OwnerContext))->get_throttle(Throttle);
        Dodge.Throttle = Throttle;
        BreakAway.Throttle = Throttle;
    }

    pub::AI::Personality const& Pers = NavHelper->PersonalityData;
    pub::AI::Personality::EvadeDodgeUseStruct const& D = Pers.EvadeDodgeParams;
    pub::AI::Personality::EvadeBreakUseStruct const& B = Pers.EvadeBreakParams;

    // 062c8308-062c837a: seven one-line setters on Dodge, each a raw dword copy.
    Dodge.ConeAngle = D.EvadeDodgeConeAngle;                        // .text:062a66b0 +0x3c <- Sys+0x1598
    Dodge.ConeAngleVariancePercent = D.EvadeDodgeConeAngleVariancePercent;       // .text:062a33e0 +0x40 <- 0x159c
    Dodge.WaggleAxisConeAngle = D.EvadeDodgeWaggleAxisConeAngle;    // FUN_062d88b0  +0x44 <- 0x15a0
    Dodge.RollAngle = D.EvadeDodgeRollAngle;                        // .text:062a33c0 +0x48 <- 0x15a4
    Dodge.IntervalTime = D.EvadeDodgeIntervalTime;                  // .text:063275d0 +0x34 <- 0x15a8
    Dodge.IntervalTimeVariancePercent = D.EvadeDodgeIntervalTimeVariancePercent; // .text:063275f0 +0x38 <- 0x15ac
    Dodge.DodgeDistance = D.EvadeDodgeDistance;                     // .text:062a33d0 +0x4c <- 0x15b0

    DodgeTime2a8 = D.EvadeDodgeTime;                                // 062c8388 <- 0x15b4
    BreakTime2ac = B.EvadeBreakTime;                                // 062c8394 <- 0x15dc

    Dodge.TurnThrottle = D.EvadeDodgeTurnThrottle;                  // FUN_062d8910 +0x90 <- 0x15bc
    Dodge.SlideThrottle = D.EvadeDodgeSlideThrottle;                // FUN_062d8930 +0xc8 <- 0x15b8
    Dodge.VectorCc.x = D.EvadeDodgeCorkscrewTurnThrottle;           // FUN_062d8940 +0xcc <- 0x15c0
    Dodge.VectorCc.z = D.EvadeDodgeCorkscrewRollThrottle;           // FUN_062d8950 +0xd4 <- 0x15c4
    Dodge.bRollFlipDirection = D.bEvadeDodgeCorkscrewRollFlipDirection; // FUN_062d8960 +0xd8 <- byte 0x15c8
    Dodge.Picker.SetWeights(D.EvadeDodgeDirectionWeightLeft, D.EvadeDodgeDirectionWeightRight,
                            D.EvadeDodgeDirectionWeightUp, D.EvadeDodgeDirectionWeightDown);  // 062c8410 FUN_062d76b0 on +0x44

    int const Style = PickDodgeStyle();                             // 062c8417 FUN_062c8690
    Dodge.DodgeStyle = Style;                                       // 062c8421 .text:062d8920 +0xb4
    if (Style == 2)
    {
        // 062c842b-062c8435: FLD double [.rdata:0639f8e0] (1.0471975803375244); FCOS;
        // FCHS; FSTP float [+0x2b8].
        ConeCos2b8 = -static_cast<float>(std::cos(static_cast<double>(1.0471975803375244)));
    }

    // 062c843b-062c84a7: the break-away fate. The sum is FLD F2C; FADD F28; FADD F30 into a
    // float local; r = rand() * sum / 32768 stays on the x87 stack. `FCOM [F30]; TEST
    // AH,0x5; JP` skips on r >= F30, so r < F30 gives 2; then `FLD F2C; FADD F30; FXCH;
    // FCOMPP; TEST AH,0x5; JP` -> r < F2C + F30 gives 0, otherwise 1.
    int Fate;
    {
        float const Sum = static_cast<float>((static_cast<double>(B.EvadeBreakStyleWeightOutrun) +
                                              B.EvadeBreakStyleWeightSideways) +
                                             B.EvadeBreakStyleWeightReverse);
        double const R = static_cast<double>(std::rand()) * Sum * GRandScale;
        if (R < B.EvadeBreakStyleWeightReverse)
            Fate = 2;
        else if (R < static_cast<double>(B.EvadeBreakStyleWeightOutrun) + B.EvadeBreakStyleWeightReverse)
            Fate = 0;
        else
            Fate = 1;
    }
    BreakAway.BreakStyle = Fate;                                    // 062c84aa .text:063275d0 +0x34
    BreakAway.AttemptReverseTime = B.EvadeBreakAttemptReverseTime;  // .text:062a33d0 +0x4c <- 0x1610
    BreakAway.ReverseDistance = B.EvadeBreakReverseDistance;        // FUN_062d93b0  +0x54 <- 0x1614
    BreakAway.TurnThrottle = B.EvadeBreakTurnThrottle;              // FUN_062d9490  +0x94 <- 0x1600
    BreakAway.RollThrottle = B.EvadeBreakRollThrottle;              // FUN_062d94a0  +0x98 <- 0x15fc
    BreakAway.IntervalTime = B.EvadeBreakIntervalTime;              // FUN_062d9480  +0x88 <- 0x15e0
    BreakAway.Picker.SetWeights(B.EvadeBreakDirectionWeightLeft, B.EvadeBreakDirectionWeightRight,
                                B.EvadeBreakDirectionWeightUp, B.EvadeBreakDirectionWeightDown);  // 062c8529 on +0x124

    // 062c852e-062c8568: FLD F08; FMUL F0C; FSTP float local; r = rand() * local / 32768;
    // FLD F08; FSUB r -> FUN_062d93c0 (+0x8c).
    {
        float const Spread = static_cast<float>(static_cast<double>(B.EvadeBreakAfterburnerDelay) *
                                                B.EvadeBreakAfterburnerDelayVariancePercent);
        double const R = static_cast<double>(std::rand()) * Spread * GRandScale;
        BreakAway.AfterburnerDelay = static_cast<float>(static_cast<double>(B.EvadeBreakAfterburnerDelay) - R);
    }

    if (NavHelper->SelfTarget.Iface->is_player())                                 // 062c857f
    {
        // 062c8589-062c85c9: get_transform (+0xc) on ExternalIface, its +0x8/+0x14/+0x20
        // (column 2 of the rotation) negated into a local, handed to FUN_062d88c0.
        Transform const& Xf = NavHelper->SelfTarget.Iface->get_transform();
        Vector Forward;
        Forward.x = -Xf.Rotation.E[0][2];                           // +0x08
        Forward.y = -Xf.Rotation.E[1][2];                           // +0x14
        Forward.z = -Xf.Rotation.E[2][2];                           // +0x20
        Dodge.SetField58(Forward);
    }
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062c85e0 (.text:062c85e0-062c8623).
// ----------------------------------------------------------------------------
void EvadeBehavior::Slot4()
{
    Flag2b5 = 0;                                                    // 062c85e9
    Dodge.Commit();                                                 // 062c85f0
    BreakAway.Commit();                                             // 062c85ff
    DodgeMissile.Commit();                                          // 062c860e
    Afterburner.Commit();                                           // 062c861d
    Behavior::Slot4();                                              // 062c8623 JMP FUN_062d20f0
}

// ----------------------------------------------------------------------------
// Slot 5, LAB_062c7ab0 (.text:062c7ab0-062c7cf7).
// ----------------------------------------------------------------------------
int EvadeBehavior::Slot5(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = NavHelper;
    int Result = 2;                                                 // 062c7ac9 [ESP+0x14]

    // 062c7abb-062c7aee: JobParams.bForceAttackFormation (Sys+0x1958) and Timers[0].Active.
    if (Sys->PersonalityData.JobParams.bForceAttackFormation != 0 && NavHelper->Timers.IsTimer0Active())
    {
        Flag2b4 = 1;
        Elapsed2b0 = DodgeTime2a8;
        Result = 0;
    }

    if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers))   // 062c7afc FUN_062e8870
    {
        ModeSelector0xc = Flag29 ? 3 : 0;                           // 062c7b05-062c7b21
        return ModeSelector0xc;
    }
    if (Sys->GunConvergenceStateBytes.FollowerDirectiveGateA())       // 062c7b36 FUN_062ecb20
    {
        ModeSelector0xc = Flag29 ? 3 : 2;                           // 062c7b3f-062c7b58
        return ModeSelector0xc;
    }

    // 062c7b67-062c7b91: the closest hostile within EvadeDodge.EvadeActivateRange, watched.
    IObjInspect* const Closest = FindClosestHostileTarget(&Sys->Timers,
                                                          Sys->PersonalityData.EvadeDodgeParams.EvadeActivateRange);
    Watcher2a0.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Closest)));

    if (IsWatching(Watcher2a0))                                     // 062c7b96-062c7ba5
    {
        IObjRW* Victim = nullptr;                                   // 062c7baf
        WatchedObject(Watcher2a0)->get_target(Victim);              // 062c7bc7 +0x74
        if (Victim != nullptr)                                      // 062c7bce
        {
            // 062c7be4-062c7bf7: GetIfaceVectorMagnitude (FUN_062e61a0) FCOMP 1.0f; `TEST
            // AH,0x41; JNZ` skips when not greater.
            if (GetIfaceVectorMagnitude(WatchedObject(Watcher2a0)) > 1.0f &&
                WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2bc))   // 062c7c16
            {
                // 062c7c22-062c7c44: is the watched object's target this very ship?
                unsigned int const SelfId = Sys->SelfTarget.Iface->get_id();
                if (Victim->get_id() == SelfId)
                {
                    ModeSelector0xc = Flag29 ? 3 : 1;               // LAB_062c7c46
                    return ModeSelector0xc;
                }
            }

            // LAB_062c7c68: ... or one of this ship's formation followers.
            IObjRW* Followers[0x10];
            int const Count = static_cast<IObjRW*>(OwnerContext)->get_formation_followers(Followers, 0x10);
            for (int Index = 0; Index < Count; ++Index)             // 062c7c7c-062c7cdb
            {
                IObjRW* const Follower = Followers[Index];
                if (Follower == nullptr)
                    continue;
                // 062c7c9b-062c7ca8: the cone test against the FOLLOWER's heading.
                if (!IsPointOutsideHeadingCone(WatchedObject(Watcher2a0)->get_position(), Follower, ConeCos2bc))
                    continue;
                if (Victim->get_id() == Follower->get_id())         // 062c7cb4-062c7cd2
                {
                    ModeSelector0xc = Flag29 ? 3 : 1;
                    return ModeSelector0xc;
                }
            }
        }
    }

    // LAB_062c7cdd.
    ModeSelector0xc = Flag29 ? 3 : Result;
    return ModeSelector0xc;
}

// ----------------------------------------------------------------------------
// Slot 8, FUN_062c7d00 (.text:062c7d00-062c7d57).
// ----------------------------------------------------------------------------
void EvadeBehavior::Slot8()
{
    SystemManager* const Sys = NavHelper;
    Flag2b4 = 0;                                                    // 062c7d06
    if (Sys->PersonalityData.JobParams.bForceAttackFormation != 0)  // 062c7d0d
        Flag2b4 = NavHelper->Timers.IsTimer0Active() ? 1 : 0;    // 062c7d1a FUN_062e8810
    if (Flag2b4 != 0)
        Elapsed2b0 = DodgeTime2a8;                                  // 062c7d2f
    if (Flag2b5 == 0)
        Flag2b5 = NavHelper->Timers.IsTimer1Active() ? 1 : 0;    // 062c7d4b FUN_062e8820
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062c7d60 (.text:062c7d60-062c824a). Switch table at 062c824c:
// {062c8030, 062c8219, 062c7fd2, 062c7f03} for Mode2c 1..4; the JA default (062c7ef6,
// rel32 -> 062c823f) is the shared `return 2`.
// ----------------------------------------------------------------------------
int EvadeBehavior::Slot7()
{
    SystemManager* const Sys = NavHelper;
    pub::AI::Personality const& Pers = Sys->PersonalityData;

    // 062c7d67-062c7dc8: a non-player ship evades only something it is watching, and only
    // while that object lies outside its own heading cone.
    if (!NavHelper->SelfTarget.Iface->is_player())
    {
        if (!IsWatching(Watcher2a0) || !WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2bc))
        {
            UpdateResult10 = 2;                                     // LAB_062c823a
            return 2;
        }
    }

    int const OldMode = Mode2c;                                     // 062c7dd1 EDI
    if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers))   // 062c7dd7
    {
        Mode2c = 4;
    }
    else if (NavHelper->Timers.IsTimer2Active())                 // 062c7df4 FUN_062e8860
    {
        Mode2c = 3;
    }
    // 062c7e06-062c7e21: `FLD [2b0]; FCOMP [2a8]; TEST AH,0x5; JP` skips on
    // Elapsed >= DodgeTime (or unordered), so mode 1 needs Elapsed < DodgeTime.
    else if (Flag2b6 == 0 && Elapsed2b0 < DodgeTime2a8)
    {
        Mode2c = 1;
    }
    else
    {
        if (Flag2b7 != 0)                                           // 062c7e34
        {
            UpdateResult10 = 2;
            return 2;
        }
        // 062c7e3a-062c7e51: `FLD [2ac]; FADD [2a8]; FCOMP [2b0]; TEST AH,0x41; JNZ` fails
        // when not greater, so mode 2 needs BreakTime + DodgeTime > Elapsed.
        if (!(static_cast<double>(BreakTime2ac) + DodgeTime2a8 > Elapsed2b0))
        {
            UpdateResult10 = 2;
            return 2;
        }
        Mode2c = 2;                                                 // 062c7e57
    }

    if (OldMode != Mode2c)                                          // 062c7e5d
    {
        if (Mode2c == 1)                                            // 062c7e61-062c7e98
        {
            if (Pers.FormationParams.bBreakApartFormationOnEvadeDodge != 0)   // Sys+0x17fd
                NavHelper->GunConvergenceStateBytes.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnEvadeDodgeTime);
            if (Pers.FormationParams.bRegroupFormationOnEvadeDodge != 0)      // Sys+0x1804
                NavHelper->GunConvergenceStateBytes.NotifyFormationLeader2();
        }
        if (Mode2c == 2)                                            // 062c7e9d-062c7ed4
        {
            if (Pers.FormationParams.bBreakApartFormationOnEvadeBreak != 0)   // Sys+0x1805
                NavHelper->GunConvergenceStateBytes.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnEvadeBreakTime);
            if (Pers.FormationParams.bRegroupFormationOnEvadeBreak != 0)      // Sys+0x180c
                NavHelper->GunConvergenceStateBytes.NotifyFormationLeader2();
        }
        Slot4();                                                    // 062c7edd CALL [EAX+0x10]
    }

    Elapsed2b0 = Elapsed2b0 + DeltaTime;                            // 062c7ee0-062c7ef0

    switch (Mode2c)
    {
    case 1:                                                         // 062c8030
    {
        if (IsWatching(Watcher2a0) && !WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2b8))
        {
            // 062c8076-062c80e7: the threat is back inside the narrower cone -- stop
            // steering: zero position into FUN_062e8c20 and FUN_062e8ce0, then
            // FUN_062e89a0(1.0f, 0), all on the ThrustManager (Sys+0x18).
            Vector Zero1;
            Zero1.x = 0.0f;
            Zero1.y = 0.0f;
            Zero1.z = 0.0f;
            Sys->Thrust.SetAxisThrottle(Zero1);
            Vector Zero2;
            Zero2.x = 0.0f;
            Zero2.y = 0.0f;
            Zero2.z = 0.0f;
            Sys->Thrust.SetNudgeDirection(Zero2);
            Sys->Thrust.SetThrottle(1.0f, 0);
            UpdateResult10 = 0;
            return 0;
        }

        if (Flag2b5 != 0)                                           // LAB_062c80e8
        {
            pub::AI::Personality::EvadeDodgeUseStruct const& D = Pers.EvadeDodgeParams;
            // 062c80f6-062c8128: v = F14 * 3.0f (DAT_0639dfd4); `FCOMP pi/2 (DAT_0639f5d4);
            // TEST AH,0x41; JNZ` keeps v when not greater, otherwise v = pi/2.
            // The FST rounds into the local, but FCOMP tests the unrounded product.
            double const Angle = static_cast<double>(D.EvadeDodgeConeAngle) * 3.0f;
            Dodge.ConeAngle = (Angle > 1.5707964f) ? 1.5707964f : static_cast<float>(Angle);   // .text:062a66b0
            // 062c812d-062c815c: the same clamp on F1C -> FUN_062d88b0 (+0x44).
            double const Turn = static_cast<double>(D.EvadeDodgeWaggleAxisConeAngle) * 3.0f;
            Dodge.WaggleAxisConeAngle = (Turn > 1.5707964f) ? 1.5707964f : static_cast<float>(Turn);
            // 062c8161-062c818c: `FLD F24; FCOM 1.0f; TEST AH,0x41; JNZ` -> not greater
            // doubles it (FADD ST0,ST0), greater halves it (FMUL 0.5f).
            double Interval = D.EvadeDodgeIntervalTime;
            if (Interval > 1.0)
                Interval = Interval * 0.5f;
            else
                Interval = Interval + Interval;
            Dodge.IntervalTime = static_cast<float>(Interval);      // .text:063275d0
            Dodge.TurnThrottle = 1.0f;                              // FUN_062d8910
            Dodge.SlideThrottle = 1.0f;                             // FUN_062d8930
        }

        Dodge.Update(DeltaTime);                                    // LAB_062c81a9

        // 062c81b6-062c81ed: FUN_062e4500(Sys+0x14fc, watched-or-null, 50.0f).
        if (IsWithinInterceptRangeOfIface(&Sys->SelfTarget,WatchedObject(Watcher2a0), 50.0f))
            Afterburner.Update(DeltaTime);                          // 062c81f3
        else
            Afterburner.Commit();                                   // 062c8206
        UpdateResult10 = 0;
        return 0;
    }

    case 2:                                                         // 062c8219
    {
        int const R = BreakAway.Update(DeltaTime);
        if (R != 0)
        {
            UpdateResult10 = 2;
            return 2;
        }
        UpdateResult10 = R;
        return R;
    }

    case 3:                                                         // 062c7fd2
    {
        if (IsWatching(Watcher2a0) && !WatchedOutsideCone(Watcher2a0, OwnerContext, 0.7f))  // 0x3f333333
        {
            UpdateResult10 = 2;
            return 2;
        }
        BreakAway.BreakStyle = 2;                                   // 062c801b .text:063275d0 (EBP)
        int const R = BreakAway.Update(DeltaTime);                  // 062c8028
        if (R != 0)
        {
            UpdateResult10 = 2;
            return 2;
        }
        UpdateResult10 = R;
        return R;
    }

    case 4:                                                         // 062c7f03
    {
        // The missile the timer table is tracking (Timers+0xe8, Sys+0x11c), and one of three
        // dodge modes chosen by the Personality::MissileReaction range it falls inside.
        void* const MissileRaw = *reinterpret_cast<void* const*>(Sys->Timers.BestCandidateWatcherBytes);
        int Mode;
        if (TimerTableMissileWithinReaction00(&Sys->Timers))        // 062c7f09 FUN_062e8910
            Mode = 1;
        else if (TimerTableMissileWithinReaction04(&Sys->Timers))   // 062c7f3f FUN_062e8940
            Mode = 2;
        else if (TimerTableMissileWithinReaction08(&Sys->Timers))   // 062c7f83 FUN_062e8970
            Mode = 0;
        else
        {
            UpdateResult10 = 1;                                     // LAB_062c7fc3
            return 1;
        }
        DodgeMissile.SetWatchObject(ObjRwFromWatchable(MissileRaw));    // FUN_062d9ae0
        DodgeMissile.Mode1c = Mode;                                 // 062c7fb3 .text:0631d950
        DodgeMissile.Update(DeltaTime);                             // 062c7fc0
        UpdateResult10 = 1;
        return 1;
    }

    default:
        UpdateResult10 = 2;                                         // 062c823f
        return 2;
    }
}

// ----------------------------------------------------------------------------
// Slot 10, LAB_062c8630 (.text:062c8630-062c8680).
// ----------------------------------------------------------------------------
bool EvadeBehavior::Slot10(void* DirectiveOp)
{
    SystemManager* const Sys = NavHelper;
    void* Target;
    if (DirectiveOp != nullptr)
    {
        // 062c863b-062c8645: FUN_062e74a0(Sys+0x34, Op+0xc).
        Target = Sys->Timers.FindVerifiedObject((unsigned const*)(static_cast<char*>(DirectiveOp) + 0xc));
    }
    else
    {
        // LAB_062c8659: FindClosestHostileTarget(Sys+0x34, EvadeDodge.EvadeActivateRange).
        Target = FindClosestHostileTarget(&Sys->Timers, Sys->PersonalityData.EvadeDodgeParams.EvadeActivateRange);
    }
    // LAB_062c864a: `Target ? Target + 8 : 0`, deduplicated set_pointer.
    Watcher2a0.set(Target != nullptr
                       ? reinterpret_cast<Watchable const*>(static_cast<char*>(Target) + 8)
                       : nullptr);
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);           // 062c8679
    return true;
}

// ----------------------------------------------------------------------------
// Slot 21, LAB_062c7870 (.text:062c7870-062c78af).
// ----------------------------------------------------------------------------
void EvadeBehavior::Slot21(void* Arg)
{
    if (!IsWatching(Watcher2a0))                                    // 062c7876-062c7886
        return;
    // 062c7888-062c789e: get_id (+0x20) on the watched object, compared with the argument.
    if (WatchedObject(Watcher2a0)->get_id() != static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg)))
        return;
    Watcher2a0.set(nullptr);                                        // 062c78a0-062c78a9
}

// ----------------------------------------------------------------------------
// Slot 24, LAB_062c78c0: `Watched ? Watched - 8 : 0`.
// ----------------------------------------------------------------------------
IObjRW const* EvadeBehavior::get_target()
{
    return WatchedObject(Watcher2a0);
}
