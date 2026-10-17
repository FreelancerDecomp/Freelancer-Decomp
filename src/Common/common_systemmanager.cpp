// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManager (the per-ship AI system block behind IBehaviorManager::Sys):
// ctor/dtor, the +0x1364 record's destructor, the SystemManager-level helpers, the personality
// hook, InitSystemManager and UpdateSystemManager.
// CMN-SPLIT stage 1 (2026-10-14): the timer-table functions moved to common_sysmgr_timertable.cpp,
// the gun-group manager to common_sysmgr_gungroups.cpp, RepairManager and its helpers to
// common_repairmanager.cpp, ThrustManager to common_thrustmanager.cpp, all unchanged.

#include "common_systemmanager.h"
#include "common_systemmanager_internal.h" // CmnSysMgr:: -- WriteFloatAt / IsWatcherLive / WatchedObject
#include "common_aisteer.h"   // SustainedThresholdTimer, ConstructGunConvergenceState, RefreshWeaponFireControlConfig
#include "common_behavior.h"
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IBehaviorManager::Pathfind, IObjInspect, ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager::IsCruiseUnobstructed (FUN_062de160)
#include "common_iustategraph.h" // ClearAiTargetVector, BuildAiTargetList (ScanGunGroupTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanGunGroupTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanGunGroupTargets)
#include "common_cobjdb.h"     // IObjDB::FindById (AppendSystemManagerTimerListEntry)
#include "dacom.h"             // DACOM_API

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

using CmnSysMgr::WriteFloatAt;
using CmnSysMgr::IsWatcherLive;
using CmnSysMgr::WatchedObject;

// ============================================================================
// SystemManager
// ============================================================================
SystemManager::SystemManager()
    : Unknown0(0)
    , Owner(nullptr)
    , Watcher()
    , ObjectDb(nullptr)
    , Unknown14(0)
    , Owner1498(nullptr)
    , Watcher149C()
    , Unknown14A4(SUBOBJ_ID_NONE)
    , Repair()
    , PersonalityData()
{
    // Common.dll.c 0x062e6260 (FUN_062e6260), item 1779. Raw disasm Common.dll.txt
    // 241928-242031. Retail order: Watcher (+0x08) zeroed (.text:062e6287-062e6289),
    // Thrust.Owner (+0x18) = 0, Timers (FUN_062e6af0), GunGroups (FUN_062e9330),
    // gun-convergence state (FUN_062ebbc0), Targeting (FUN_062ed570), the +0x1498 loot
    // sub-object's watcher and id (.text:062e62cf-062e62f8), the RepairManager
    // (.text:062e62fc-062e6394), SelfTarget's guards and interface pointers zeroed,
    // Personality ctor, then Owner/Unknown10/Unknown14 zeroed last (.text:062e63e3-062e63e9).
    // Owner (+0x04) gets its real value from InitSystemManager (FUN_062e6490), called by
    // IBehaviorManager's ctor right after this one.
    //
    // Not written by retail's ctor, zeroed here for determinism: Unknown0, Pad15, the rest
    // of the ThrustManager after its Owner dword (InitThrustManager fills it), Owner1498,
    // RepairManager::Owner/CheckTimer, and SelfTarget's cached slices (+0x14FC..+0x1577).
    std::memset(Pad15, 0, sizeof(Pad15));
    std::memset(&Thrust, 0, sizeof(Thrust));

    ConstructSystemManagerTimerTable(&Timers);
    ConstructSystemManagerGunGroupManager(&GunGroups);
    // +0x1364: GunConvergenceStateBytes is a real member; its ctor (FUN_062ebbc0,
    // .text:062e62bb) runs as a member initializer.

    // +0x142C: Targeting is a real TargetManager member; its own ctor (FUN_062ed570,
    // .text:062e62ca) runs as a member initializer.

    std::memset(Pad14A6, 0, sizeof(Pad14A6));

    // SelfTarget: retail writes only the four guards (.text:062e63b5-062e63c7) and the two
    // interface pointers (.text:062e63a9 and ExternalIface); the cached slices +0x00..+0x7B
    // are zeroed here for determinism.
    std::memset(&SelfTarget, 0, sizeof(SelfTarget));
}

// Retail has no out-of-line SystemManager destructor: FUN_062d2530 (IBehaviorManager's dtor)
// inlines it at .text:062d25bf-062d265d, in this order, and then frees the block
// (FUN_06391d5a). Everything not listed (Thrust, GunGroups' arrays, TargetManager, the
// RepairManager, Personality) has no destructor work in retail.
//
// The sub-objects with destructor work are RAII members (AGENTS RE rule 13: retail's ctor
// unwind funclets call their out-of-line destructors FUN_062d2700 / FUN_062d27d0), so the
// members unwind in reverse declaration order, which is retail's inlined order exactly:
// Watcher149C (.text:062d25dd), the +0x1364 record (FGunConvergenceStateBlock, +0x14 then
// +0x0C, 062d25f5 / 062d2603), GunGroups' +0x08 target watcher (Sys+0x134, 062d2614),
// Timers (+0xE8 watcher, list, Targets buffer, 062d262c-062d2649), Watcher (+0x8, 062d2657).
SystemManager::~SystemManager()
{
}

// FUN_062d27d0 (item 1546). Common.dll.txt 062d27d0-062d282e: the BaseWatcher at +0x14,
// then the one at +0x0C, each set_pointer(0) when it watches something -- the member
// destructors of FollowLeaderWatcher and FormationLeaderWatcher, in that (reverse
// declaration) order.
FGunConvergenceStateBlock::~FGunConvergenceStateBlock()
{
}

// FUN_062e6990, item 1789. Common.dll.c 117197-117231. "this" =
// &GunConvergenceStateBytes (the already-ported GunConvergenceState blob,
// common_aisteer.h -- its own two embedded BaseWatcher fields at Self+0x0C /
// Self+0x14 match this function's +0x1370/+0x1378 SystemManager-absolute
// offsets exactly: 0x1364+0x0C=0x1370, 0x1364+0x14=0x1378).
//
// Common.dll.txt .text:062e6990-062e6a0f, thiscall on the SystemManager, `RET 4`; the same
// block twice, for +0x1370 (062e6999-062e69cf) and then +0x1378 (062e69d4-062e6a0a):
//   Watched = [W]; 0 -> next; Watched - 8 == 0 -> next   (ADD EAX,-8; TEST; JZ)
//   ([W] ? [W] - 8 : 0)->get_id()  (IObjInspect slot 8, CALL [vt+0x20], thiscall)
//   != TargetId -> next
//   [W] == 0 -> next, else BaseWatcher::set_pointer(W, 0)
// The trailing `[W] != 0 -> set_pointer(0)` is BaseWatcher::set(nullptr).
void ClearGunConvergenceStateWatcherIfTarget(SystemManager* Self, int TargetId)
{
    BaseWatcher* const Watchers[2] =
    {
        &Self->GunConvergenceStateBytes.FormationLeaderWatcher, // Sys + 0x1370
        &Self->GunConvergenceStateBytes.FollowLeaderWatcher,    // Sys + 0x1378
    };

    for (int Index = 0; Index < 2; ++Index)
    {
        BaseWatcher* const W = Watchers[Index];
        IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(W->target()));
        if (Watched == nullptr)
            continue;
        if (static_cast<int>(Watched->get_id()) != TargetId)
            continue;
        W->set(nullptr);
    }
}

// FUN_062e68e0, item 1787. Common.dll.c 117165-117179 / raw disasm
// Common.dll.txt 242565-242585.
void SetSystemManagerSharedListHead(SystemManager* Self, void* NewHead)
{
    Self->ObjectDb = static_cast<IObjDB*>(NewHead);
    Self->Timers.ObjectDb = static_cast<IObjDB*>(NewHead);

    // .text:062e68e9-062e6901: `MOV EAX,[EDX+0x38]; ADD EAX,8; MOV EAX,[EAX]` --
    // Timers.Owner is dereferenced with no null test (retail has none), then the
    // watched pointer is tested for 0, for 8 (`ADD EAX,-8; TEST`) and NewHead for 0.
    SystemManager* Owner = Self->Timers.Owner;
    Watchable const* WatcherTarget = Owner->Watcher.target();
    if (WatcherTarget != nullptr && reinterpret_cast<std::uintptr_t>(WatcherTarget) != 8 && NewHead != nullptr)
    {
        // Absolute SystemManager+0x50/+0x48/+0x44 == Timers-relative
        // +0x1C/+0x14/+0x10: this is `bBuilt = 0; Targets.End = Targets.Begin;`,
        // i.e. empty the cached AI target list so the next
        // BuildAiTargetList rebuilds it from scratch. Retail inlines
        // vector::erase(begin, end) (.text:062e6904-062e6938): the 0x88-byte
        // element move loop runs from End to End (zero iterations), then End = Begin.
        Self->Timers.bBuilt = 0;
        Self->Timers.Targets.End = Self->Timers.Targets.Begin;
    }

    // .text:062e693c-062e6946: `LEA ECX,[EDX+0x142c]; JMP 0x062ed730` -- a
    // TargetManager member taking NewHead whose body is the ICF-folded
    // `RET 4`. It writes nothing, so nothing is done here. (The port used to
    // memcpy NewHead over SystemManager+0x142C, which is Targeting's vfptr.)
}

// ============================================================================
// FUN_062e6950, item 1788 -- SystemManager's "personality changed" hook.
// Common.dll.txt 242618-242637, 15 instructions, `this` == ECX == the
// SystemManager. Sole caller .text:062d441b is
// IBehaviorManager::set_personality.
// ============================================================================
void NotifySystemManagerPersonalityChanged(SystemManager* Self)
{
    // .text:062e6953  LEA ECX,[ESI+0x12c]; CALL FUN_062eb6c0
    (void)Self->GunGroups.RefreshFromPersonality();

    // .text:062e695e  LEA ECX,[ESI+0x34];  CALL FUN_062e72d0
    (void)RefreshSystemManagerTimerThresholdsFromPersonality(&Self->Timers);

    // .text:062e6966  LEA ECX,[ESI+0x14a8]; CALL FUN_062ecfb0 -- item 1880, on the
    // RepairManager (whose +0x04 Owner FUN_062e6490 writes at .text:062e667f).
    (void)RefreshRepairManagerFromPersonality(&Self->Repair);

    // .text:062e6971-062e6981: the block's Owner (+0x136c) tested, then its
    // JobParams.bForceAttackFormation (+0x1958) copied to the block's +0xC3 (Sys+0x1427).
    // Item 1786's own tick does the byte-identical copy at .text:062e68c2.
    SystemManager* const StateOwner = Self->GunConvergenceStateBytes.Owner;
    if (StateOwner != nullptr)
        Self->GunConvergenceStateBytes.bForceAttackFormation = StateOwner->PersonalityData.JobParams.bForceAttackFormation;
}

// ============================================================================
// SystemManager initialisation and per-tick update (items 1785, 1786, 1837).
// 2026-09-24, claude-code:w3-behaviormgr-sys.
// ============================================================================
// FUN_062e6490, item 1785. Raw disasm Common.dll.txt 242121-242335 (185 insns), `RET 8`,
// ESI = this, EBX = &Watcher, EBP = 0. Every "if live" below is its own re-read of the
// watcher in retail (.text:062e64da, 062e652f, 062e6592, 062e6613, 062e6633, 062e666b).
bool InitSystemManager(SystemManager* Self, IObjRW* Object, IBehaviorManager* Manager)
{
    // .text:062e6490-062e64b1: EAX = Object ? Object + 8 : 0; if (EAX != [EBX])
    // set_pointer(EAX) -- BaseWatcher::set on the object's Watchable base.
    Self->Watcher.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);

    Self->Owner = Manager;                                      // .text:062e64ba

    // .text:062e64bd-062e64d3: [ESI] = watched object's get_id() (IObjInspect slot 8). The
    // object is dereferenced without a test.
    Self->Unknown0 = static_cast<int>(static_cast<IObjInspect*>(WatchedObject(Self->Watcher))->get_id());

    (void)ArmSystemManagerTimerTable(&Self->Timers, Self);      // .text:062e64cf-062e64d5

    if (IsWatcherLive(Self->Watcher))
    {
        // .text:062e64e7-062e6528, FAiTarget::Init inlined (the guards cleared 7d, 7c, 7e,
        // 7f). The attitude reference comes from Timers.Owner (just set by
        // ArmSystemManagerTimerTable, which succeeds on this branch), dereferenced blind.
        IObjRW* const Own = WatchedObject(Self->Watcher);
        IObjRW* const TimersOwnerObject = WatchedObject(Self->Timers.Owner->Watcher);
        Self->SelfTarget.Init(TimersOwnerObject, Own);
    }

    if (IsWatcherLive(Self->Watcher))
        InitThrustManager(&Self->Thrust, Self);                 // .text:062e653c-062e658e

    if (IsWatcherLive(Self->Watcher))
    {
        // .text:062e65a5-062e6610, EDI = &GunGroups.
        SystemManagerGunGroupManager& G = Self->GunGroups;
        G.Owner = Self;                                         // [EDI+4] = ESI
        G.GunCount = 0;
        G.Field_0x834 = -1;
        (void)G.ResolveHardpoints();
        for (int Index = 0; Index < G.GunCount; ++Index)        // count re-read each pass (.text:062e65d8)
            G.RandomizeSlotTiming(&G.Guns[Index]);
        // .text:062e65e8-062e6610: ScanPeriod = 1.0f (DAT_063a07a4); ScanTimer = rand()/32768 * that.
        float const Period = 1.0f;
        G.ScanPeriod = Period;
        G.ScanTimer = (float)rand() * (1.0f / 32768.0f) * Period;
    }

    if (IsWatcherLive(Self->Watcher))
    {
        // .text:062e6621  [ESI+0x136c] = ESI -- the +0x1364 block's Owner.
        Self->GunConvergenceStateBytes.Owner = Self;
    }

    (void)Self->Targeting.Attach(Self);                         // .text:062e6627-062e662e

    if (IsWatcherLive(Self->Watcher))
    {
        // .text:062e6641-062e6665
        Self->Owner1498 = Self;
        (void)static_cast<IObjInspect*>(WatchedObject(Self->Watcher))
            ->find_equipment(&Self->Unknown14A4, 1, 0x100000);
    }

    if (IsWatcherLive(Self->Watcher))
    {
        // .text:062e667f-062e669c, EAX = &Repair.
        Self->Repair.Owner = Self;
        Self->Repair.CheckTimer = 0.0f;                         // [EAX+0x50]
        Self->Repair.Hull.Count = 0;                            // [EAX+0x18]
        Self->Repair.Shield.Count = 0;                          // [EAX+0x3c]
        (void)static_cast<IObjInspect*>(WatchedObject(Self->Watcher))->enumerate_cargo(&Self->Repair);
    }

    // .text:062e66a2-062e66a9: FUN_062eb9d0(this + 0x1344, this) -- GunGroups+0x1218.
    (void)Self->GunGroups.CmDroppers.Bind(Self);

    return true;                                                // .text:062e66b1 MOV AL,1
}

// FUN_062e66c0, item 1786. Raw disasm Common.dll.txt 242346-242553 (183 insns), `RET 4`,
// ESI = this, EBP = Dt from .text:062e6743 on.
void UpdateSystemManager(SystemManager* Self, float Dt)
{
    // .text:062e66c8-062e6718: re-resolve the two object pointers and clear the four
    // per-tick cache latches while the watcher is live. Timers.Owner is dereferenced blind.
    if (IsWatcherLive(Self->Watcher))
    {
        IObjRW* const Own = WatchedObject(Self->Watcher);
        IObjRW* const TimersOwnerObject = WatchedObject(Self->Timers.Owner->Watcher);
        Self->SelfTarget.Init(TimersOwnerObject, Own);          // FAiTarget::Init inlined
    }

    // .text:062e671e-062e674e: bBuilt = 0, then vector::erase(begin, end) on the AI target
    // list -- FUN_062ed460's copy runs from End to End (no element moves), JustifyNode::Execute
    // is DestroyAiTargetRange, End = Begin. That is ClearAiTargetVector exactly.
    Self->Timers.bBuilt = 0;
    ClearAiTargetVector(&Self->Timers.Targets);

    Self->Targeting.UpdateCommitTimer(Dt);                      // .text:062e6743-062e6751
    Self->GunGroups.UpdateTimers(Dt);                           // .text:062e6756-062e6761
    Self->GunGroups.CmDroppers.Update(Dt);                      // .text:062e6766-062e676d (+0x1344)

    // .text:062e6772-062e6783: Thrust.bCruiseUnobstructed = Thrust.Owner->Owner->Pathfind's
    // FUN_062de160 -- the cruise gate engage_cruise reads.
    Self->Thrust.bCruiseUnobstructed =Self->Thrust.Owner->Owner->Pathfind->IsCruiseUnobstructed() ? 1 : 0;

    // .text:062e6786-062e67bd: while the object is in cruise but the gate is closed,
    // disengage. The watched object is dereferenced blind.
    bool bCruise = false;
    (void)static_cast<IObjInspect*>(WatchedObject(Self->Thrust.Owner->Watcher))->is_cruise_active(bCruise);
    if (bCruise && Self->Thrust.bCruiseUnobstructed == 0)
        Self->Thrust.engage_cruise(false);

    UpdateSystemManagerTimerTable(&Self->Timers, Dt);           // .text:062e67c2-062e67ca

    // .text:062e67cf-062e6849, EDI = &Repair. Every three seconds (strictly past 3.0f,
    // DAT_063a07b0; FCOMP + TEST AH,0x41 + JNZ) the cargo is re-tallied.
    RepairManager& Repair = Self->Repair;
    double const CheckTimer = static_cast<double>(Dt) + static_cast<double>(Repair.CheckTimer);
    Repair.CheckTimer = static_cast<float>(CheckTimer);
    if (CheckTimer > 3.0)
    {
        Repair.CheckTimer = 0.0f;
        Repair.Hull.Count = 0;
        Repair.Shield.Count = 0;
        (void)static_cast<IObjInspect*>(WatchedObject(Repair.Owner->Watcher))->enumerate_cargo(&Repair);
    }
    Repair.Hull.Update(Dt, WatchedObject(Repair.Owner->Watcher));      // .text:062e6814-062e682c
    Repair.Shield.Update(Dt, WatchedObject(Repair.Owner->Watcher));    // .text:062e6831-062e6849

    Self->GunConvergenceStateBytes.Update(Dt);                  // .text:062e684e-062e6857

    // .text:062e685c-062e68c8: the personality-refresh latch. Unknown14 is set by
    // RefreshRepairManagerFromPersonality / RefreshWeaponFireControlConfig when they copied
    // another ship's parameters; it is consumed here once that borrowing no longer applies.
    if (Self->Unknown14 == 0)
        return;
    if (Self->SelfTarget.Iface->is_player())
        return;
    if (Self->PersonalityData.FormationParams.bLeaderMakesMeTougher == 0)   // byte +0x1820
        return;
    if (Self->GunConvergenceStateBytes.bLeaderSuppressed == 0)
    {
        // .text:062e6889-062e6895: a live formation leader keeps the borrowed parameters.
        Watchable const* const Node = Self->GunConvergenceStateBytes.FormationLeaderWatcher.target();
        if (Node != nullptr && Node != (Watchable const*)8)
            return;
    }

    Self->Unknown14 = 0;                                        // .text:062e689b
    Self->PersonalityData.FormationParams.bLeaderMakesMeTougher = 0;   // .text:062e689f
    (void)Self->GunGroups.RefreshFromPersonality();             // .text:062e68a6
    (void)RefreshSystemManagerTimerThresholdsFromPersonality(&Self->Timers);  // .text:062e68af
    (void)RefreshRepairManagerFromPersonality(&Self->Repair);   // .text:062e68b6

    // .text:062e68bb-062e68c8: the same JobParams.bForceAttackFormation copy
    // NotifySystemManagerPersonalityChanged makes.
    SystemManager* const StateOwner = Self->GunConvergenceStateBytes.Owner;
    if (StateOwner != nullptr)
        Self->GunConvergenceStateBytes.bForceAttackFormation = StateOwner->PersonalityData.JobParams.bForceAttackFormation;
}
