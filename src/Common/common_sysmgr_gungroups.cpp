// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManagerGunGroupManager (SystemManager+0x12C): construction, slot timing,
// the hardpoint resolver family, the auto-turret target scan and the per-tick timers. The
// fire passes are in common_aifirecontrol.cpp; the class is declared in common_systemmanager.h.
// CMN-SPLIT stage 1 (2026-10-14): split out of common_systemmanager.cpp unchanged.
// CMN-S2-AIR (2026-10-14): typed fields and member functions instead of byte offsets on an
// opaque blob; no behaviour change.

#include "common_systemmanager.h"
#include "common_systemmanager_internal.h" // CmnSysMgr::IsWatcherLive / WatchedObject
#include "common_aisteer.h"   // FAiTarget, GetCachedRange
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IObjInspect, IObjRW, ObjRwFromWatchable
#include "common_iustategraph.h" // BuildAiTargetList, GetAiTargetList (ScanAutoTurretTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanAutoTurretTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanAutoTurretTargets)

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>

using CmnSysMgr::IsWatcherLive;
using CmnSysMgr::WatchedObject;

// ============================================================================
// SystemManagerGunGroupManager -- Common.dll.c 0x062e9330 (FUN_062e9330), item 1828.
// ============================================================================
namespace
{
    // The ctor's per-slot loop (retail's puVar1 walks each record's +0x03 byte).
    void InitGunGroupArray(FGunGroupSlot* Slots)
    {
        for (int i = 0; i < 0x40; ++i)
        {
            FGunGroupSlot& Slot = Slots[i];
            Slot.bAutoTurret = 0;
            Slot.bSeekerMunition = 0;
            Slot.bFireTick = 0;
            Slot.BurstTime = 3.0f;
            Slot.NoBurstTime = 1.5f;
            Slot.FireInterval = 0.5f;
            Slot.Phase = 0.0f;                                  // retail stores int 0
            Slot.Field_0x1C = 0;
        }
    }
}

void ConstructSystemManagerGunGroupManager(SystemManagerGunGroupManager* Self)
{
    std::memset(Self, 0, sizeof(*Self));

    // The target watcher at +0x08/+0x0C (zeroed), +0x28 = 0, ScanPeriod = 1.0f.
    Self->ScanPeriod = 1.0f;

    InitGunGroupArray(Self->Guns);
    InitGunGroupArray(Self->MineDroppers);

    // LineOfFireCache[16]: {InUse=0, TargetId=0, HardpointId=0, Age=0, RefreshTimer=0,
    // NeedsRefresh=1, bBlocked=0}.
    for (int i = 0; i < 0x10; ++i)
    {
        FLauncherFireCacheSlot& Slot = Self->LineOfFireCache[i];
        Slot.InUse = 0;
        Slot.TargetId = 0;
        Slot.HardpointId = 0;
        Slot.NeedsRefresh = 1;
        Slot.Age = 0.0f;
        Slot.RefreshTimer = 0.0f;
        Slot.bBlocked = 0;
    }

    // Trailing scalar fields (all confirmed via the raw disassembly of FUN_062e9330; retail
    // literal hex values decoded to float below).
    Self->Owner = nullptr;
    Self->bEnabled = 1;

    Self->GunFireIntervalTime = 0.5f;
    Self->AutoTurretIntervalTime = 0.5f;
    Self->MissileLaunchIntervalVariance = 0.5f;
    Self->GunFireNoBurstIntervalTime = 2.0f;
    Self->AutoTurretNoBurstIntervalTime = 2.0f;

    Self->Field_0x10 = 0;
    Self->CruiseHeldTime = 0.0f;
    Self->GunCount = 0;
    Self->bFireAllGuns = 0;

    Self->Field_0x108C = 0.09817477f;                         // 0x3dc90fdb == PI/32
    Self->GunFireBurstIntervalVariance = 0.3f;
    Self->AutoTurretBurstIntervalVariance = 0.3f;
    Self->Field_0x107C = 0.0f;
    Self->MissileLaunchAllowOutOfRange = 0.0f;
    Self->WideConeAngle = 0.09817477f;

    Self->AimRefreshTimer = 0.0f;
    Self->Field_0x834 = -1;                                    // 0xffffffff

    Self->GunFireIntervalVariance = 0.2f;
    Self->GunFireBurstIntervalTime = 4.0f;
    Self->AutoTurretBurstIntervalTime = 4.0f;
    Self->MissileLaunchIntervalTime = 3.0f;
    Self->MissileLaunchConeAngle = 3.14159274f;
    Self->MineLaunchInterval = 5.0f;
    Self->MineLaunchConeAngle = 0.3926991f;
    Self->GunFireAccuracyConeAngle = 0.05f;
    Self->GunRange = 1100.0f;
    Self->MissileLaunchRange = 800.0f;
}

namespace
{
    // FILD rand(); FMUL float 2^-15 (.rdata:063997d0, 00 00 00 38) -- exact, kept on the
    // x87 stack by every user below.
    double RandUnit()
    {
        return (double)rand() * (double)3.0517578125e-05f;
    }

    // The jitter term FUN_062e9500 repeats five times (e.g. 062e95dd-062e961d): Neg = -Half
    // stored as a float, then ((Half - Neg) * RandUnit + Neg) * Scale + Scale on the x87 stack,
    // rounded once by the FSTP.
    float JitterAround(float Half, float Scale)
    {
        float const Neg = -Half;
        double const Unit = RandUnit();
        return (float)((((double)Half - Neg) * Unit + Neg) * Scale + Scale);
    }
}

// FUN_062e9500, item 1829. Common.dll.c 119365-119435. thiscall on the gun groups, RET 4: the
// slot (a Guns or MineDroppers record).
void SystemManagerGunGroupManager::RandomizeSlotTiming(FGunGroupSlot* Slot)
{
    int const Case = Slot->MunitionKind;

    // CORRECTED 2026-10-14 (CMN-S2 AIS): every term below stays on the x87 stack until its
    // FSTP (the port used float arithmetic), and the closing Phase term had its sign wrong.
    bool HaveResult = true;
    float Result = 0.0f;
    switch (Case)
    {
    case FGunGroupSlot::KindPlain:
        if (Slot->bAutoTurret == 0)
        {
            // 062e95dd-062e96a3
            Slot->BurstTime = JitterAround(GunFireBurstIntervalVariance, GunFireBurstIntervalTime);
            Slot->NoBurstTime = JitterAround(GunFireBurstIntervalVariance, GunFireNoBurstIntervalTime);
            Result = JitterAround(GunFireIntervalVariance, GunFireIntervalTime);
        }
        else
        {
            // 062e9530-062e95d8: the third term is (Unit * 0.5 + 0.75) * AutoTurretIntervalTime,
            // doubles .rdata:0639df88 / 0639fe28.
            Slot->BurstTime = JitterAround(AutoTurretBurstIntervalVariance, AutoTurretBurstIntervalTime);
            Slot->NoBurstTime = JitterAround(AutoTurretBurstIntervalVariance, AutoTurretNoBurstIntervalTime);
            Result = (float)((RandUnit() * 0.5 + 0.75) * AutoTurretIntervalTime);
        }
        break;
    case FGunGroupSlot::KindSeeker:
        // 062e96a5-062e96e5
        Result = JitterAround(MissileLaunchIntervalVariance, MissileLaunchIntervalTime);
        break;
    case FGunGroupSlot::KindHpType10:
    case FGunGroupSlot::KindMineDropper:
        // 062e96f5-062e9719: the doubles at .rdata:063a07f0 / 063a07e8 are (double)0.40000004f
        // and (double)0.8f (3fd99999c0000000 / 3fe99999a0000000).
        Slot->BurstTime = 0.0f;
        Slot->NoBurstTime = 0.0f;
        Result = (float)((RandUnit() * (double)0.40000004f + (double)0.8f) * MissileLaunchIntervalTime);
        break;
    case FGunGroupSlot::KindCruiseDisruptor:
        // Owner+0x176c: anti_cruise_missile_interval_time.
        Slot->FireInterval = Owner->PersonalityData.MissileParams.AntiCruiseMissileIntervalTime;
        HaveResult = false;
        break;
    default:
        HaveResult = false;
        break;
    }

    if (HaveResult)
        Slot->FireInterval = Result;

    // 062e9722-062e9747: Neg = -FireInterval stored as a float; FLD Neg; FCHS; FMULP with the
    // unit; FADD Neg -- i.e. Phase = S * Unit - S, in [-S, 0). The port used to store
    // -S * Unit - S (sign of the product inverted, range [-2S, -S]).
    float const Neg = -Slot->FireInterval;
    double const Unit = RandUnit();
    Slot->Phase = (float)(-(double)Neg * Unit + Neg);
}

// FUN_062e9770, item 1830. Common.dll.c 119439-119578 / raw disasm Common.dll.txt
// 247154-247386 (187 insns). Ghidra mis-scopes the stack slot reused as every
// get_equipment_val's out-parameter as the iteration counter and as param_1; the raw ESP
// displacements show it is one reused scratch slot.
//
// local_8 / local_4 (the "SUB ESP,0xC" frame) are an EquipStatus: its flag bits are cleared
// once before the loop (`AND 0xf8`, `AND 0xe0000000`), get_equipment_status (+0x118) writes
// them through `LEA EAX,[ESP+0x14]`, and 062e991a tests bit 0 afterwards.
int SystemManagerGunGroupManager::PopulateSlots(unsigned short const* SourceIds, int MaxCount,
                                                FGunGroupSlot* Slots, bool bMineDroppers)
{
    if (SourceIds == nullptr || Slots == nullptr)
        return 0;

    int Count = 0;
    if (MaxCount > 0)
    {
        FGunGroupSlot* Slot = Slots;
        do
        {
            unsigned short const Id = SourceIds[Count];
            if (Id == SUBOBJ_ID_NONE)
                break;
            Slot->HardpointId = Id;
            Slot->bAutoTurret = 0;
            Slot->MunitionKind = FGunGroupSlot::KindUnclassified;
            ++Count;
            ++Slot;
        } while (Count < MaxCount);
    }

    if (Count == 0)
        return 0;

    // 062e9792-062e97ab: one EquipStatus on the frame, built once before the loop (its three
    // flag bits cleared, `AND 0xf8`, and its +4 dword masked with 0xe0000000) and reused by
    // every get_equipment_status call below -- the callee only rewrites those flag bits.
    EquipStatus Status{};

    // No own-ship tests anywhere below: every owner resolution has retail's dereferencing
    // `XOR ECX,ECX` arm (062e9825, 062e987f, 062e98b7, 062e98e7, 062e990c, 062e9932).
    unsigned short const* SourceCursor = SourceIds;
    FGunGroupSlot* Slot = Slots;
    for (int Remaining = Count; Remaining != 0; --Remaining)
    {
        if (bMineDroppers)
        {
            Slot->MunitionKind = FGunGroupSlot::KindMineDropper;
        }
        else
        {
            // 062e9829-062e9840: get_equipment_val(EquipValMunitionKind) into the param_1
            // stack slot, read back as a dword.
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValMunitionKind, 0.0f);
            if (Result == 0)
            {
                int Classify;
                std::memcpy(&Classify, &Val, sizeof(Classify));
                int NewKind = -1;
                switch (Classify)
                {
                case 0: NewKind = FGunGroupSlot::KindPlain; break;
                case 1: NewKind = FGunGroupSlot::KindSeeker; break;
                case 2: NewKind = FGunGroupSlot::KindHpType10; break;
                case 3: NewKind = FGunGroupSlot::KindCruiseDisruptor; break;
                default: break; // retail's own jump table has exactly 4 entries; anything else leaves the field untouched
                }
                if (NewKind != -1)
                    Slot->MunitionKind = NewKind;
            }
        }

        {
            // 062e986e-062e98a2: get_equipment_val(EquipValAutoTurret); only the low byte is
            // tested (`MOV AL`).
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValAutoTurret, 0.0f);
            unsigned char LowByte;
            std::memcpy(&LowByte, &Val, sizeof(LowByte));
            if (Result == 0 && LowByte != 0)
                Slot->bAutoTurret = 1;
        }

        {
            // 062e98a6-062e98d3: get_equipment_val(EquipValSeeker); its low byte is stored.
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValSeeker, 0.0f);
            if (Result == 0)
                std::memcpy(&Slot->bSeekerMunition, &Val, sizeof(Slot->bSeekerMunition));
        }

        // 062e98d6-062e9942: get_equipment_status (+0x118) into the shared Status; on success,
        // a non-player ship whose Status bit 0 is clear (`TEST byte [ESP+0x10],1` at 062e991a --
        // the bit get_equipment_status just wrote) gets IObjAffect slot 0x4c
        // (&SourceIds[i], 1, -1).
        if (OwnShip()->get_equipment_status(Status, Slot->HardpointId) == 0 &&
            !OwnShip()->is_player() &&
            (Status.Flags & 0x01) == 0)
        {
            OwnShip()->slot_4c(SourceCursor, true, -1);
        }

        ++SourceCursor;
        ++Slot;
    }

    return Count;
}

// FUN_062e9990, item 1831. Common.dll.c 119582-119627 / raw disasm Common.dll.txt
// 247392-247472 (68 insns). find_equipment(Ids, 0x40, EquipTypeGun) feeds Guns,
// find_equipment(Ids, 0x40, EquipTypeMineDropper) feeds MineDroppers (forced to
// KindMineDropper); a failed query leaves that count at 0 (247429 / 247458). False only
// without an own ship.
bool SystemManagerGunGroupManager::ResolveHardpoints()
{
    // 062e9999-062e99a8: the watched pointer and its -8 are tested; every later resolution
    // dereferences its `XOR` arm (062e99ca, 062e9a18).
    if (OwnShip() == nullptr)
        return false;

    GunCount = 0;
    {
        unsigned short GunIds[0x40];
        int const Result = OwnShip()->find_equipment(GunIds, 0x40, EquipTypeGun);
        if (Result == 0)
            GunCount = PopulateSlots(GunIds, 0x40, Guns, false);
    }

    MineDropperCount = 0;
    {
        unsigned short MineDropperIds[0x40];
        int const Result = OwnShip()->find_equipment(MineDropperIds, 0x40, EquipTypeMineDropper);
        if (Result == 0)
            MineDropperCount = PopulateSlots(MineDropperIds, 0x40, MineDroppers, true);
    }

    return true;
}

// FUN_062e9a60, item 1832. Common.dll.c 119631-119663 / raw disasm Common.dll.txt
// 247480-247540 (50 insns), RET 4 (Ghidra's "unaff_ESI" is this stack argument, read by
// `MOV EAX,[ESP+0xc]` at 247530). The id local is pre-seeded with SUBOBJ_ID_NONE; the
// unconditional `MOV AL,1` at 247537 is the result even when find_equipment fails.
bool SystemManagerGunGroupManager::SetShieldActive(int Mode)
{
    if (OwnShip() == nullptr)
        return false;

    // 062e9a7c-062e9ad2: find_equipment(&Id, 1, EquipTypeShield) through a re-resolved owner
    // and, on success, IObjAffect slot 0x4c (&Id, Mode, -1) through another one; both `XOR`
    // arms (062e9a93, 062e9abe) dereference 0. Every retail caller pushes 0 or 1 for Mode.
    unsigned short HardpointId = SUBOBJ_ID_NONE;
    int const Result = OwnShip()->find_equipment(&HardpointId, 1, EquipTypeShield);
    if (Result == 0)
    {
        OwnShip()->slot_4c(&HardpointId, Mode != 0, -1);
    }

    return true;
}

// FUN_062e9ae0, item 1833. Common.dll.c 119667-119743 / raw disasm Common.dll.txt
// 247546-247680 (113 insns), RET 4.
//
// CORRECTED 2026-09-24 (claude-code:w4-behaviormgr-stubs) against Common.dll.txt
// .text:062e9ae0-062e9bf4: the per-gun announce pushes `LEA EBX,[ESI+0x30]` (062e9bab), the
// gun's equipment id; retail resolves `[[this+4]+8] ? -8 : 0` and calls through it untested.
// GunCount is unsigned (JBE / JC) and re-read after every call (062e9bd9).
bool SystemManagerGunGroupManager::Retarget(IObjRW* NewTarget)
{
    if (NewTarget == nullptr)
        return false; // 062e9ae5-062e9aea -> LAB_062e9bf0

    BaseWatcher& Watcher = TargetWatcher();

    // 062e9af0-062e9b04: already watching NewTarget (`W ? W - 8 : 0` == NewTarget) -> true.
    if (NewTarget == CurrentTarget())
        return true;

    // 062e9b0a-062e9b16: `NewTarget + 8` (its Watchable), set_pointer only when it differs.
    Watcher.set(static_cast<Watchable const*>(NewTarget));

    // 062e9b1b-062e9b44: a live watch (W != 0 && W - 8 != 0) gets get_type (+0x88) into the
    // reused argument slot, which is discarded.
    if (IsWatcherLive(Watcher))
    {
        unsigned Scratch = 0;
        (void)CurrentTarget()->get_type(Scratch);
    }

    // 062e9b4a-062e9b7c: our ship's IObjAffect set_target (+0x48) with `W ? W - 8 : 0`.
    OwnShip()->set_target(CurrentTarget(), SUBOBJ_ID_NONE, -1);

    // 062e9b7f-062e9b9c: a player ship stops here.
    if (OwnShip()->is_player())
        return true;

    // 062e9b9e-062e9be5: point every gun at NewTarget through IObjAffect +0x44.
    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
        OwnShip()->slot_44(&Guns[Index].HardpointId, NewTarget, SUBOBJ_ID_NONE, -1);
    return true;
}

// FUN_062e9c00, item 1834. Common.dll.c 119747-119771 / raw disasm Common.dll.txt
// 247691-247733 (35 insns). `LEA EBX,[ESI+0x30]` (062e9c11) is the pushed id pointer.
// CORRECTED 2026-09-24: the port pushed the +0x02 byte and skipped the call when the ship
// was null; retail has no such test (the null arm dereferences address 4). GunCount is
// compared unsigned (JBE / JC) and re-read after every call (062e9c3e).
void SystemManagerGunGroupManager::ClearGunTargets()
{
    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
        OwnShip()->slot_44(&Guns[Index].HardpointId, nullptr, SUBOBJ_ID_NONE, -1);
}

// FUN_062e9c50 (item 1835), Common.dll.txt 247743-248080, `RET 4`. The only caller is
// IBehaviorManager::UpdateBehaviorTick (.text:062d2ce9), on ticks where our own ship is not
// the player. Stack frame after the four pushes: [+0x12] bSelfMayEngage, [+0x13]
// bAnyHostile, [+0x14] current slot, [+0x18] DistSq, [+0x1c] BestDistSq, [+0x20] BestIface,
// [+0x24] PlayerTarget, [+0x28] PlayerSubTarget, [+0x2c] SubTarget, [+0x30] slot index,
// [+0x34] RangeSq, [+0x38] target list, [+0x3c] barrel position, [+0x4c] the Dt slot reused
// as an out-parameter.
void SystemManagerGunGroupManager::ScanAutoTurretTargets(float Dt)
{
    // 062e9c53-062e9c6d: `Dt + ScanTimer`, FST'd (rounded) to both the Dt slot and +0x28, but
    // the FCOMP against ScanPeriod uses the still-unrounded ST0. TEST AH,0x41 / JNZ returns on
    // less, equal or unordered -- the scan runs only when the sum strictly exceeds the period.
    double const SumD = (double)Dt + (double)ScanTimer;
    float const NewTimer = (float)SumD;
    ScanTimer = NewTimer;
    if (!(SumD > (double)ScanPeriod))
        return;

    // 062e9c73-062e9c81: _CIfmod on the stored (rounded) float sum.
    ScanTimer = (float)std::fmod((double)NewTimer, (double)ScanPeriod);

    // 062e9c84-062e9cb3: RangeSq = (GunRange * 1.2 [double DAT_0639fa60])^2, kept on the x87
    // stack and rounded to float once.
    double const Range = (double)GunRange * 1.2;
    float const RangeSq = (float)(Range * Range);

    SystemManager* const Sys = Owner;
    IObjInspect const* const OwnIface = Sys->SelfTarget.Iface;

    // 062e9c8a-062e9d0b: when our ship is the player, remember its own target and subtarget
    // (get_target +0x74, get_subtarget +0x78 into the reused Dt slot); the subtarget local is
    // pre-seeded with SUBOBJ_ID_NONE and only overwritten when a target exists.
    unsigned short PlayerSubTarget = SUBOBJ_ID_NONE;
    IObjRW* PlayerTarget = nullptr;
    if (OwnIface->is_player())
    {
        IObjRW* Queried = nullptr;
        OwnIface->get_target(Queried);
        PlayerTarget = Queried;
        if (Queried != nullptr)
        {
            // The out-slot is the Dt argument slot, which get_target has just filled with the
            // target pointer: its low word is what stays if get_subtarget writes nothing.
            unsigned short QueriedSub = (unsigned short)(std::uintptr_t)Queried;
            OwnIface->get_subtarget(QueriedSub);
            PlayerSubTarget = QueriedSub;
        }
    }

    // 062e9d0f-062e9d34: the self record (SystemManager+0x14FC, an FAiTarget) refreshes its
    // lazily cached get_type flags, guard (+0x1579) raised after the call.
    FAiTarget* const SelfRecord = &Sys->SelfTarget;
    if (SelfRecord->bFlagsValid == 0)
    {
        SelfRecord->Iface->get_type(SelfRecord->CachedFlags);
        SelfRecord->bFlagsValid = 1;
    }

    // 062e9d38-062e9d4c: `AND BL,0xa0; NEG; SBB; INC` -- 1 when neither 0x80 nor 0x20 is set.
    bool const bSelfMayEngage = (SelfRecord->CachedFlags & 0xa0) == 0;
    bool bAnyHostile = false;

    // 062e9d3b-062e9d62 / 062e9fc9-062e9fe5: unsigned index over the guns, GunCount re-read on
    // every pass.
    for (unsigned SlotIndex = 0; SlotIndex < (unsigned)GunCount; ++SlotIndex)
    {
        FGunGroupSlot& Slot = Guns[SlotIndex];

        // 062e9d66-062e9d6f: only auto-turrets are scanned for.
        if (Slot.bAutoTurret == 0)
            continue;

        // 062e9d75-062e9d98: best candidate so far = none at FLT_MAX (0x7f7fffff).
        float BestDistSq = 3.40282347e+38f;
        IObjInspect* BestIface = nullptr;

        FAiTargetVector* const List = BuildAiTargetList(GetAiTargetList(Sys));
        for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
        {
            // 062e9da0-062e9db1: FUN_062ee530 -- the candidate must match the job attack order.
            if (!Sys->Targeting.MatchesAttackOrder(Elem))
                continue;

            // 062e9db7-062e9e09: our ship's CEqObj (class mask 0x103, else 0 -- not tested
            // again: the EquipManager at +0xE4 of a null CEqObj is what retail would read),
            // the launcher on this slot's equipment id, none -> next candidate.
            CObject const* const OwnerCObj = OwnShip()->cobject();
            CEqObj* OwnerEq = nullptr;
            if (OwnerCObj != nullptr && ((unsigned)OwnerCObj->ObjectClass & 0x103) == 0x103)
                OwnerEq = (CEqObj*)OwnerCObj;
            CELauncher* const Launcher = CELauncher::cast(OwnerEq->EquipManager.FindByID(Slot.HardpointId));
            if (Launcher == nullptr)
                continue;

            // 062e9e0f-062e9e5b: barrel 0's world position, then the squared distance to the
            // candidate's get_position() (+0x00), summed on the x87 stack as
            // (dz*dz + dy*dy) + dx*dx and rounded to float once.
            Vector const BarrelPos = Launcher->GetBarrelPosWS(0);
            Vector const& ElemPos = Elem->Iface->get_position();
            double const Dx = (double)ElemPos.x - (double)BarrelPos.x;
            double const Dy = (double)ElemPos.y - (double)BarrelPos.y;
            double const Dz = (double)ElemPos.z - (double)BarrelPos.z;
            float const DistSq = (float)((Dz * Dz + Dy * Dy) + Dx * Dx);

            // 062e9e5d-062e9e8e: a ship-class candidate (mask 0x503) whose behavior manager was
            // created with content creation type 2 is skipped unless bSelfMayEngage.
            CObject const* const ElemCObj = Elem->Iface->cobject();
            if (ElemCObj != nullptr && ((unsigned)ElemCObj->ObjectClass & 0x503) == 0x503)
            {
                IBehaviorManager* const ElemMgr = ((CEqObj*)ElemCObj)->get_behavior_interface();
                if (ElemMgr != nullptr && ElemMgr->ContentCbCreationType == 2 && !bSelfMayEngage)
                    continue;
            }

            // 062e9e94-062e9edb: the lazily cached attitude pair (FUN_062e6a80 inlined).
            // 062e9edf-062e9eec: `TEST AH,0x41 / JP next` -- only an attitude <= -0.6f
            // (DAT_063a0790, ordered) counts as hostile.
            float Attitude = 0.0f;
            float Reverse = 0.0f;
            GetCachedRange(Elem, &Attitude, &Reverse);
            if (!(Attitude <= -0.6f))
                continue;

            // 062e9eee-062e9f00: any hostile candidate counts, in range or not; out of range
            // (`TEST AH,0x41 / JZ` -- strictly greater, unordered stays in) goes to the next.
            bAnyHostile = true;
            if (DistSq > RangeSq)
                continue;

            // 062e9f02-062e9f38: our ship's IObjInspect::can_point_at (+0x110) with this slot's
            // id, the candidate's position and 0.3f (0x3e99999a); the bool out-slot is cleared
            // first (062e9f08).
            bool bCanPoint = false;
            OwnShip()->can_point_at(bCanPoint, Slot.HardpointId, Elem->Iface->get_position(), 0.3f);

            // 062e9f3e-062e9f63: keep the nearest pointable candidate (`TEST AH,0x5 / JP` --
            // strictly less, ordered).
            if (bCanPoint && DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestIface = Elem->Iface;
            }
        }

        // 062e9f80-062e9f9b: the player's own subtarget carries over only when the pick is the
        // player's own target.
        unsigned short const SubTarget = ((void*)BestIface == (void*)PlayerTarget)
            ? PlayerSubTarget
            : SUBOBJ_ID_NONE;

        // 062e9fa0-062e9fc6: our ship's IObjAffect slot 17 (+0x44) points this gun at the pick
        // (or at nothing), source client -1.
        OwnShip()->slot_44(&Slot.HardpointId, (IObjRW*)BestIface, SubTarget, -1);
    }

    // 062e9feb-062ea010: the self record's type flags again (same lazy refresh).
    if (SelfRecord->bFlagsValid == 0)
    {
        SelfRecord->Iface->get_type(SelfRecord->CachedFlags);
        SelfRecord->bFlagsValid = 1;
    }

    // 062ea014-062ea038: unless our own type is in 0x7f0000, TargetManager::SetAttackState
    // (062ee280) on Targeting with 0 when any hostile was seen this scan and 2 otherwise
    // (`SETNZ; DEC; AND 2`).
    if ((SelfRecord->CachedFlags & 0x7f0000) != 0)
        return;
    Sys->Targeting.SetAttackState(bAnyHostile ? 0 : 2);
}

namespace
{
    // FUN_062ea220's per-slot phase step, shared by the gun (+0x44) and mine-dropper (+0x84c)
    // loops -- the two loops are byte-identical apart from the base and the count. ESI in the
    // listing points at the slot's FireInterval (+0x14).
    void StepGunGroupPhase(FGunGroupSlot& Slot, float Dt)
    {
        float const Phase = Slot.Phase;

        if (Slot.MunitionKind != FGunGroupSlot::KindPlain)
        {
            // .text:062ea30b-062ea316: bFireTick = 1; Phase = Dt + Phase; done.
            Slot.bFireTick = 1;
            Slot.Phase = Dt + Phase;
            return;
        }

        float const Period = Slot.FireInterval;
        // .text:062ea318-062ea325  FCOMP 0.0f; TEST AH,0x41; JNZ -> Period <= 0 (or unordered)
        if (!(Period > 0.0f))
        {
            Slot.bFireTick = 1;                                 // .text:062ea361
        }
        else
        {
            // .text:062ea327-062ea358: A = fmod(Dt + Phase, Period); B = fmod(Phase, Period);
            // FCOMPP (ST0 = B, ST1 = A); TEST AH,0x41; JNZ -> B <= A gives 0, otherwise 1 --
            // the flag is set on the tick the phase wraps past a period boundary.
            double const A = std::fmod((double)Dt + (double)Phase, (double)Period);
            double const B = std::fmod((double)Phase, (double)Period);
            Slot.bFireTick = (B > A) ? 1 : 0;
        }

        // .text:062ea365-062ea37e: Phase += Dt (FST keeps the unrounded sum); FCOMP BurstTime;
        // TEST AH,0x41; JNZ -> keep. Past BurstTime, Phase = -NoBurstTime.
        double const NewPhase = (double)Dt + (double)Phase;
        Slot.Phase = (float)NewPhase;
        if (NewPhase > (double)Slot.BurstTime)
            Slot.Phase = -Slot.NoBurstTime;
    }
}

// FUN_062ea220, item 1837. Raw disasm Common.dll.txt 248277-248489 (182 insns), `RET 4`,
// EBP = &GunGroups, Dt at [ESP+0x1c] once all four registers are pushed.
void SystemManagerGunGroupManager::UpdateTimers(float Dt)
{
    // .text:062ea227-062ea23b  AimRefreshTimer -= Dt
    AimRefreshTimer = AimRefreshTimer - Dt;

    // .text:062ea230-062ea2a7: the sixteen line-of-fire cache records (the layout
    // IsLineOfFireBlocked uses). ECX walks Slot.NeedsRefresh.
    for (int SlotIndex = 0; SlotIndex < 0x10; ++SlotIndex)
    {
        FLauncherFireCacheSlot& Slot = LineOfFireCache[SlotIndex];
        if (Slot.InUse == 0)
            continue;

        // FLD Dt; FADD Age; FST local; FSTP Age -- the local is a float.
        float const Age = Dt + Slot.Age;
        Slot.Age = Age;

        if (Slot.NeedsRefresh == 0)
        {
            // .text:062ea259-062ea273: refresh timer; FCOMP 1.0f (DAT_063a07ac); TEST AH,0x1;
            // JNZ -> below 1.0 (or unordered, C0 set) keeps counting, otherwise the record is
            // marked for a refresh.
            double const RefreshAge = (double)Dt + (double)Slot.RefreshTimer;
            Slot.RefreshTimer = (float)RefreshAge;
            if (RefreshAge >= 1.0)
            {
                Slot.RefreshTimer = 0.0f;
                Slot.NeedsRefresh = 1;
            }
        }

        // .text:062ea276-062ea2a0: FLD local; FCOMP 10.0f (DAT_063a07a8); TEST AH,0x41; JNZ
        // -> a record older than 10 s is evicted.
        if (Age > 10.0f)
        {
            Slot.InUse = 0;
            Slot.TargetId = 0;
            Slot.HardpointId = SUBOBJ_ID_NONE;
            Slot.NeedsRefresh = 1;
            Slot.Age = 0.0f;
            Slot.RefreshTimer = 0.0f;
            Slot.bBlocked = 0;
        }
    }

    // .text:062ea2a9-062ea2f0: while the target watcher holds a live object, CruiseHeldTime
    // accumulates Dt as long as that object reports is_cruise_active (IObjInspect slot 24,
    // +0x60) and resets to 0.0f otherwise; with no object it is zeroed.
    if (IsWatcherLive(TargetWatcher()))
    {
        bool bCruise = false;
        (void)CurrentTarget()->is_cruise_active(bCruise);
        if (bCruise)
            CruiseHeldTime = Dt + CruiseHeldTime;
        else
            CruiseHeldTime = 0.0f;                              // DAT_06399408 == 0.0f
    }
    else
    {
        CruiseHeldTime = 0.0f;                                  // .text:062ea2f0 MOV [EBP+0x24],0
    }

    // .text:062ea2f3-062ea38d: guns, GunCount re-read every iteration.
    for (int Index = 0; Index < GunCount; ++Index)
        StepGunGroupPhase(Guns[Index], Dt);

    // .text:062ea393-062ea437: mine droppers, MineDropperCount re-read every iteration.
    for (int Index = 0; Index < MineDropperCount; ++Index)
        StepGunGroupPhase(MineDroppers[Index], Dt);
}
