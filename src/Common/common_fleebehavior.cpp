// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_fleebehavior.h"
#include "common_dockbehavior.h"     // DockBehavior::CopyDockOp (FUN_062c3590)
#include "common_aisteer.h"          // HasReadyWeaponSlot (FUN_062eb910)
#include "common_systemmanager.h"    // SystemManager
#include "dacom.h"                   // FDUMP

#include <cstdio>   // std::sprintf -- the dead diagnostics retail formats into stack buffers

// FleeBehavior -- see common_fleebehavior.h. Ported from EXE/Disasm/Common.dll.txt.

// FUN_062ecaf0 (item 1869) is FGunConvergenceStateBlock::GetCurrentLeader: the follow /
// formation leader watched at SystemManager+0x1364 (an IObjRW*, already "-8"ed), or 0.
// FUN_062fe940 (item 2001, common_gotobehavior.cpp): GotoBehavior's own DirectiveGotoOp copy-out.
extern "C" bool __fastcall FUN_062fe940(unsigned char* This, void* OutBuffer);

namespace
{
// DAT_0639f7a4: 00 40 1c 46 == 10000.0f. Ghidra merges its last byte into the string that
// follows ("FFleeBehavior::deactivate()..."); the string itself starts at 0639f7a8.
float const GFleeDestinationOffsetX = 10000.0f;

// The format strings of retail's dead diagnostics (formatted into a stack buffer and never
// read): 0639f6b8 and 0639f7a8.
char const* const GPerformUninitialized =
    "FleeBehavior::Perform() running uninitialized flee_type. Quitting behavior. Ship id: %d (decimal)";
char const* const GDeactivateUninitialized =
    "FleeBehavior::deactivate() running uninitialized flee_type. Quitting behavior. Ship id: %d (decimal)";

// Slot5's hostile-power-ratio thresholds, indexed by JobParams.FleeSceneThreatStyle (0..4), default
// 0.5f: DAT_0639e0e0 0.15f, DAT_0639e0dc 0.35f, DAT_0639e0d8 0.65f, DAT_0639e0d4 0.85f,
// DAT_0639a1d0 1.0f, DAT_0639c13c 0.5f (raw .rdata bytes 9a99193e / 3333b33e / 6666263f /
// 9a99593f / 0000803f / 0000003f).
float FleeHostileRatioThreshold(int Level)
{
    switch (Level)
    {
    case 0: return 0.15f;
    case 1: return 0.35f;
    case 2: return 0.65f;
    case 3: return 0.85f;
    case 4: return 1.0f;
    default: return 0.5f;
    }
}
}

// FUN_062c7020 (Common.dll.txt 193301-193339), all 31 instructions: FUN_062d2020(TypeId),
// DirectiveGotoOp ctor on +0x4c, DirectiveDockOp ctor on +0xbc, byte +0x2c = 0, byte +0x38 = 0,
// +0x34 = 0, vptr, +0x30 = 4. Retail leaves +0x3c..+0x48 unwritten (Slot3/4/5/22 write the
// three delegate pointers before anything reads them); the port zeroes them.
FleeBehavior::FleeBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , Opaque18{}
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , Field2c(0)
    , Pad2d{}
    , Field30(4)
    , Field34(0)
    , Field38(0)
    , Pad39{}
    , Field3c(nullptr)
    , Field40(nullptr)
    , Field44(nullptr)
    , Field48(0)
    , Pad49{}
    , GotoOp()
    , DockOp()
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

// Slot 0 is FUN_062c7090, the scalar-deleting dtor ICF-shared by seven behavior vtables:
// thunk_FUN_062d2080 (~Behavior) plus the conditional free. Nothing FleeBehavior-specific.
FleeBehavior::~FleeBehavior()
{
}

// Slot 1, LAB_062cf3e0 (Common.dll.txt 205851-205852): `JMP FUN_062d2090`.
bool FleeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// The three loads every one of slots 3/4/5/22 opens with, e.g. .text:062c7572-062c7598:
// `MOV ECX,[EAX+0x20]; MOV EDX,[ECX+0x17c]; MOV [EAX+0x3c],EDX; MOV EDX,[ECX+0x190];
// MOV [EAX+0x40],EDX; ... MOV ECX,[ECX+0x19c]; MOV [EAX+0x44],ECX`.
void FleeBehavior::RefreshDelegates()
{
    Field3c = BehaviorMgrPtr20->BehaviorDispatchTable[2];    // +0x17c
    Field40 = BehaviorMgrPtr20->BehaviorDispatchTable[7];    // +0x190
    Field44 = BehaviorMgrPtr20->BehaviorDispatchTable[10];   // +0x19c
}

// ============================================================================
// Slot 5 -- LAB_062c70b0 (Common.dll.txt 193365-193551), 161 instructions, `RET 4`.
// ============================================================================
int FleeBehavior::Slot5(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = NavHelperPtr24;
    pub::AI::Personality::JobStruct const& Job = Sys->PersonalityData.JobParams;

    // 062c70ba-062c70fc. EBP = 2 is the "do not flee" answer, also parked at [ESP+0x1c].
    Field38 = 0;
    int Result = 2;
    if (Job.bFleeWhenLeaderFleesStyle != 0)                                  // Sys+0x1950
    {
        IObjRW* const Watched = Sys->GunConvergenceStateBytes.GetCurrentLeader();   // FUN_062ecaf0
        if (Watched != nullptr)
        {
            // GetBehaviorManager(obj)->CurrentBehaviorIndex == 4: the target is fleeing.
            IBehaviorManager* const TargetMgr = GetBehaviorManager(Watched);
            if (TargetMgr->CurrentBehaviorIndex == 4)
            {
                Field38 = 1;
                Field2c = 1;
                Field30 = 1;
            }
        }
    }

    // 062c7103-062c712f: a flee already running answers 3 (with +0x29) or 0.
    if (Field2c != 0)
    {
        if (Flag29 != 0)
        {
            ModeSelector0xc = 3;
            return 3;
        }
        ModeSelector0xc = 0;
        return 0;
    }

    RefreshDelegates();                                                      // 062c7132-062c714d

    // 062c7150-062c718a: the watched object's shield, IObjInspect slot 17
    // get_shield_status(Cur, Max, bActive) into three zeroed locals. Sys+0x08 is the
    // SystemManager's own watcher; the object is its Watchable minus 8, or a null receiver
    // (retail calls through it either way).
    float ShieldCur = 0.0f;
    float ShieldMax = 0.0f;
    bool bShieldActive = false;
    {
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
        (void)ObjInspectOf(Object)->get_shield_status(ShieldCur, ShieldMax, bShieldActive);
    }

    // 062c718d-062c71c8: two get_status(HitPts, MaxHitPts) calls (slot 15) on
    // Sys->ExternalIface. The ratio below reads HitPts from the first and MaxHitPts from the
    // second ([ESP+0x18]/[ESP+0x1c] at the FLDs).
    float HitPtsA = 0.0f;
    float MaxHitPtsA = 0.0f;
    float HitPtsB = 0.0f;
    float MaxHitPtsB = 0.0f;
    IObjInspect const* const External = Sys->SelfTarget.Iface;
    (void)External->get_status(HitPtsA, MaxHitPtsA);
    (void)External->get_status(HitPtsB, MaxHitPtsB);

    // 062c71cb-062c71ef: (HitPtsA + ShieldCur) / (MaxHitPtsB + ShieldMax) FCOMP
    // Sys+0x194c (JobParams.FleeWhenHullDamagedPercent); `TEST AH,0x5; JP`
    // jumps (bFlee = 0) when C0 and C2 are both clear, i.e. ratio >= threshold or unordered
    // with C0==C2==1; fall-through (C0 alone, ratio < threshold) sets bFlee and Field30 = 2.
    bool bFlee = false;
    {
        double const Ratio = (static_cast<double>(HitPtsA) + static_cast<double>(ShieldCur)) /
                             (static_cast<double>(MaxHitPtsB) + static_cast<double>(ShieldMax));
        if (Ratio < static_cast<double>(Job.FleeWhenHullDamagedPercent))
        {
            bFlee = true;
            Field30 = 2;
        }
    }

    // 062c71f2-062c724b: Targeting.AggressionRatio (Sys+0x1438) FCOMP the per-level constant
    // after FXCH, so ST0 is the ratio; `TEST AH,0x41; JNZ` skips when ratio < or ==
    // constant; fall-through (ratio > constant) sets bFlee and Field30 = 0.
    if (static_cast<double>(Sys->Targeting.AggressionRatio) >
        static_cast<double>(FleeHostileRatioThreshold(Job.FleeSceneThreatStyle)))   // Sys+0x1948
    {
        bFlee = true;
        Field30 = 0;
    }

    // 062c7252-062c727c: with JobParams.bFleeNoWeaponsStyle (Sys+0x1951), no ready weapon in
    // GunGroups (Sys+0x12c) forces the flee with Field30 = 3 (EBP = 0 directly); a ready
    // weapon reloads EBP from [ESP+0x20] (the 2 parked above) and falls to the bFlee test.
    if (Job.bFleeNoWeaponsStyle != 0 && !Sys->GunGroups.HasReadyWeaponSlot())
    {
        Field30 = 3;
        Result = 0;
    }
    else if (bFlee)
    {
        Result = 0;                                                          // 062c727c
    }

    // 062c727e-062c72a4.
    if (Flag29 != 0)
    {
        ModeSelector0xc = 3;
        return 3;
    }
    ModeSelector0xc = Result;
    return Result;
}

// ============================================================================
// Slot 7 -- FleeBehavior::Perform, LAB_062c72c0 (Common.dll.txt 193556-193784), 212
// instructions. Re-traced 2026-10-08: the port's former null tests on NavHelper,
// ExternalIface, its +0x84 sub-object and the three delegates were not retail's and are gone.
// ============================================================================
int FleeBehavior::Perform()
{
    SystemManager* const Sys = NavHelperPtr24;

    if (Field2c == 0)                                                        // 062c72ca
    {
        Field2c = 1;                                                         // 062c72dd

        // 062c72e0-062c7316. Radius [ESP+8] is seeded with 500.0f (0x43fa0000); the type,
        // position and target-id out-params are uninitialised stack in retail.
        pub::AI::ContentCallback::FleeDestinationType Type =
            static_cast<pub::AI::ContentCallback::FleeDestinationType>(0);
        Vector Destination = { 0.0f, 0.0f, 0.0f };
        uint DestinationId = 0;
        float Radius = 500.0f;
        uint const OwnerId = Sys->SelfTarget.Iface->get_id();    // 062c72f7
        if (!BehaviorMgrPtr20->RequestFleeDestination(
                OwnerId, static_cast<pub::AI::ContentCallback::FleeReason>(Field30),
                Type, Destination, DestinationId, Radius))
        {
            // 062c731f-062c7389: type 3, destination = the cached physical centre pushed
            // 10000 along X only (one FADD at 062c7351), radius 500.
            Type = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
            if (Sys->SelfTarget.bPosValid == 0)                                      // [EDI+0x7e]
            {
                Sys->SelfTarget.bPosValid = 1;
                (void)Sys->SelfTarget.Iface->get_physical_radius(
                    Sys->SelfTarget.CachedRadius, Sys->SelfTarget.CachedPos);  // slot 11 (+0x2c)
            }
            Destination.x = Sys->SelfTarget.CachedPos.x + GFleeDestinationOffsetX;
            Destination.y = Sys->SelfTarget.CachedPos.y;
            Destination.z = Sys->SelfTarget.CachedPos.z;
            Radius = 500.0f;
        }

        Field34 = static_cast<int>(Type);                                    // 062c738e
        if (Field34 == 2)
        {
            // 062c73cc-062c7427. A fresh DirectiveDockOp with DockObjectId = target id.
            pub::AI::DirectiveDockOp Op;
            Op.DockObjectId = DestinationId;
            Field48 = 0;
            IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
            if (Current != nullptr &&
                *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 7)
            {
                if (static_cast<DockBehavior*>(Field40)->CopyDockOp(&DockOp))
                    Field48 = 1;
            }
            Field40->Slot10(&Op);
            Field40->Slot3();
        }
        else if (Field34 == 1 || Field34 == 3)
        {
            // 062c742c-062c74ad.
            pub::AI::DirectiveGotoOp Op;
            if (Field34 == 3)
            {
                Op.Field_0C = 1;
                Op.Destination = Destination;
            }
            else
            {
                Op.Field_0C = 0;
                Op.Field_1C = static_cast<int32_t>(DestinationId);
            }
            Op.Field_50 = Radius;
            Field48 = 0;
            IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
            if (Current != nullptr &&
                *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 2)
            {
                if (FUN_062fe940(reinterpret_cast<unsigned char*>(Field3c), &GotoOp))
                    Field48 = 1;
            }
            Field3c->Slot10(&Op);
            Field3c->Slot3();
        }
        else
        {
            // 062c73a2-062c73c7: FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
            // "E:\FL\Scratch\Source\Common\Ai\BehaviorClasses.cpp", 0x453, ...).
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Common\\Ai\\BehaviorClasses.cpp", 0x453,
                  "FleeBehavior::perform() has uninitialized flee type from callback. Come get Joel W");
        }

        // LAB_062c74b0.
        if (Field2c == 0)
        {
            UpdateResult10 = 1;
            return 1;
        }
    }

    // LAB_062c74b7.
    if (Field38 != 0)
    {
        Field44->Slot3();
        Field44->SetTickDelta(GetTickDelta());
        (void)Field44->Slot7();
        UpdateResult10 = 1;                                                  // LAB_062c74d7
        return 1;
    }

    // LAB_062c74e5: switch on +0x34 (DEC/JZ chain); types 1/3 drive Field3c (062c752d),
    // type 2 Field40 (062c751f), both called unguarded.
    if (Field34 == 1 || Field34 == 2 || Field34 == 3)
    {
        Behavior* const Delegate = (Field34 == 2) ? Field40 : Field3c;
        Delegate->SetTickDelta(GetTickDelta());
        int const ChildResult = Delegate->Slot7();                           // 062c753b
        if (ChildResult != 2 && ChildResult != 3)
        {
            UpdateResult10 = 1;                                              // LAB_062c74d7
            return 1;
        }
    }
    else
    {
        // 062c74f1-062c751d: sprintf into a stack buffer that is never read, then the
        // finish path.
        char Buffer[0xf4];
        std::sprintf(Buffer, GPerformUninitialized, Sys->SelfTarget.Iface->get_id());
    }

    // LAB_062c7548.
    Field2c = 0;
    BehaviorMgrPtr20->SetDirectiveResult(4, 0);                              // FUN_062d29a0
    UpdateResult10 = 2;
    return 2;
}

// ============================================================================
// Slot 3 -- LAB_062c7570 (Common.dll.txt 193788-193844), 52 instructions.
// ============================================================================
void FleeBehavior::Slot3()
{
    RefreshDelegates();
    if (Field2c == 0)                                                        // 062c759b
        return;
    if (Field38 != 0)
    {
        Field44->Slot3();                                                    // 062c75a6
        return;
    }
    if (Field34 == 1 || Field34 == 3)
    {
        Field3c->Slot3();                                                    // 062c7604
        return;
    }
    if (Field34 == 2)
    {
        Field40->Slot3();                                                    // 062c75f3
        return;
    }
    // 062c75bc-062c75e2: NavHelper+0x14fc+0x84 (ExternalIface) get_id, then the
    // "deactivate() running uninitialized" sprintf into a stack buffer that is never read.
    SystemManager* const Sys = NavHelperPtr24;
    char Buffer[0x100];
    std::sprintf(Buffer, GDeactivateUninitialized, Sys->SelfTarget.Iface->get_id());
}

// ============================================================================
// Slot 4 -- LAB_062c7610 (Common.dll.txt 193847-193938), 86 instructions.
// ============================================================================
void FleeBehavior::Slot4()
{
    RefreshDelegates();
    if (Field38 != 0)
    {
        Field44->Slot4();                                                    // 062c7642
        return;
    }
    if (Field34 == 1 || Field34 == 3)
    {
        // LAB_062c76cd.
        if (Field3c == nullptr)
            return;
        Field3c->Slot4();
        IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
        if (Current != nullptr &&
            *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 2 &&
            Field48 != 0)
        {
            Field3c->Slot10(&GotoOp);                                        // 062c76fb
        }
        return;
    }
    if (Field34 == 2)
    {
        // LAB_062c7689.
        if (Field40 == nullptr)
            return;
        Field40->Slot4();
        Behavior_ReleaseDirectiveTarget(Field40);                            // 062c7698 FUN_062d21c0
        IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
        if (Current != nullptr &&
            *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 7 &&
            Field48 != 0)
        {
            Field40->Slot10(&DockOp);                                        // 062c76c2
        }
        return;
    }
    // 062c7659-062c767e: ExternalIface (Sys+0x1580) get_id, sprintf into a never-read buffer.
    SystemManager* const Sys = NavHelperPtr24;
    char Buffer[0x100];
    std::sprintf(Buffer, GDeactivateUninitialized, Sys->SelfTarget.Iface->get_id());
}

// ============================================================================
// Slot 22 -- LAB_062c7710 (Common.dll.txt 193949-193997), 44 instructions, `RET 4`.
// ============================================================================
void FleeBehavior::Slot22(void* Event)
{
    RefreshDelegates();
    if (Field38 != 0)
    {
        if (Field44 != nullptr)
            Field44->Slot22(Event);                                          // 062c773d JMP [EAX+0x58]
        return;
    }
    if (Field34 == 1 || Field34 == 3)
    {
        if (Field3c != nullptr)
            Field3c->Slot22(Event);                                          // 062c778c
        return;
    }
    if (Field34 == 2)
    {
        if (Field40 != nullptr)
            Field40->Slot22(Event);                                          // 062c777e
        return;
    }
    // 062c774c-062c776d.
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
          "E:\\FL\\Scratch\\Source\\Common\\Ai\\BehaviorClasses.cpp", 0x504,
          "FleeBehavior::event_handler() has failed because of uninitialized values. Come get Joel W");
}
