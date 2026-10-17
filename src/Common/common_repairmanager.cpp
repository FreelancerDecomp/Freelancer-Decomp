// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- RepairManager and its PotionHelper / HullHelper / ShieldHelper (SystemManager+0x14A8).
// CMN-SPLIT stage 1 (2026-10-14): split out of common_systemmanager.cpp unchanged.

#include "common_systemmanager.h"
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

// ============================================================================
// RepairManager / PotionHelper / HullHelper / ShieldHelper -- SystemManager+0x14A8.
// RTTI-attested (see common_systemmanager.h). Items 1780-1783, 1878-1880.
// ============================================================================
PotionHelper::PotionHelper()
    : State(0)                  // .text:062e62fc / 062e6358  [+0x04] = 0
    , Type(0)                   // not written by retail's ctor (see header)
    , EquipId(SUBOBJ_ID_NONE)   // .text:062e6314 / 062e636b  [+0x0C] = SUBOBJ_ID_NONE
    , Count(0)                  // .text:062e631b / 062e6372  [+0x10] = 0
    , PreUseDelay(2.0f)         // .text:062e6336 / 062e6378  0x40000000
    , PostUseDelay(5.0f)        // .text:062e6341 / 062e6365  0x40a00000
    , UseAtRatio(0.35f)         // .text:062e634c / 062e637e  0x3eb33333
    , Timer(0.0f)               // .text:062e6321 / 062e6384  [+0x20] = 0
{
    Pad0E[0] = 0;
    Pad0E[1] = 0;
}

// LAB_062e6400: `MOV dword ptr [ECX + 0x8],0x1a; RET`.
void HullHelper::SetType()
{
    Type = 0x1a;    // Archetype::AClassRepairKit
}

// FUN_062e6410:
//   PUSH ECX                         ; one float local
//   MOV  ECX,[ESP+param_1+4]         ; ECX = Object (the ARGUMENT, not this)
//   MOV  EAX,[ECX]
//   LEA  EDX,[ESP]; PUSH EDX
//   MOV  [ESP+local_4+8],0x3f800000  ; local = 1.0f
//   CALL [EAX+0x40]                  ; Object->get_status(float& Health), slot 16
//   FLD  [ESP]                       ; return local
//   POP ECX; RET 4
// The return code of get_status is ignored; when it fails to write, 1.0f comes back.
float HullHelper::GetRatio(IObjRW const* Object)
{
    float Health = 1.0f;
    (void)static_cast<IObjInspect const*>(Object)->get_status(Health);
    return Health;
}

// LAB_062e6430: `MOV dword ptr [ECX + 0x8],0x1b; RET`.
void ShieldHelper::SetType()
{
    Type = 0x1b;    // Archetype::AClassShieldBattery
}

// LAB_062e6440:
//   SUB  ESP,0x8
//   MOV  ECX,[ESP+0xc]               ; ECX = Object
//   LEA  EDX,[ESP+0x3]; PUSH EDX     ; arg2 = &bActive (entry_esp-5)
//   LEA  EDX,[ESP+0x8]; PUSH EDX     ; arg1 = &Health  (entry_esp-4)
//   MOV  byte [ESP+0xb],0            ; bActive = false
//   MOV  [ESP+0xc],0x3f800000        ; Health = 1.0f
//   CALL [EAX+0x48]                  ; Object->get_shield_status(float&, bool&), slot 18
//   FLD  [ESP+0x4]; ADD ESP,8; RET 4 ; return Health
float ShieldHelper::GetRatio(IObjRW const* Object)
{
    bool bActive = false;
    float Health = 1.0f;
    (void)static_cast<IObjInspect const*>(Object)->get_shield_status(Health, bActive);
    return Health;
}

// FUN_062eceb0, item 1878. Raw disasm Common.dll.txt 252888-252970 (72 insns), `RET 8`:
// ESI = this, EDI = Object ([ESP+0x14] after three pushes), Dt at [ESP+0x10].
//
//   CALL [EAX+0xbc] on Object      ; is_player() -> nonzero: return, Timer untouched
//   FLD Dt; FADD [ESI+0x20]; FST [ESI+0x20]   ; Timer += Dt (value kept in ST0)
//   switch (State):
//   0: FSTP ST0; if (Count <= 0) return;          (.text:062ecf2d `CMP EAX,EBX; JLE`)
//      CALL [EDX+0x4] (this->GetRatio(Object)); FCOMP [ESI+0x1c]; TEST AH,0x41; JP -> return
//        -- AH&0x41: 0 (ratio > UseAtRatio) and 0x41 (unordered) have even parity and
//           return; 0x01 (<) and 0x40 (==) fall through. So: ratio <= UseAtRatio.
//      Timer = 0; State = 1
//   1: FCOMP [ESI+0x14]; TEST AH,0x1; JNZ -> return   (Timer < PreUseDelay returns)
//      Object+4 (the IObjAffect base) ->vtbl[0x58](EquipId, 1, -1)
//      Timer = 0; State = 2
//   2: FCOMP [ESI+0x18]; TEST AH,0x1; JNZ -> return   (Timer < PostUseDelay returns)
//      State = 0; Timer = 0
//   default: FSTP ST0; return
void PotionHelper::Update(float Dt, IObjRW* Object)
{
    if (static_cast<IObjInspect*>(Object)->is_player())
        return;

    // FST keeps the unrounded sum in ST0 for the state-1/2 compares; MSVCRT runs the FPU at
    // 53-bit precision, so double reproduces it and the float store rounds as FST does.
    double const Elapsed = static_cast<double>(Dt) + static_cast<double>(Timer);
    Timer = static_cast<float>(Elapsed);

    switch (State)
    {
    case 0:
        if (Count <= 0)
            return;
        if (!(GetRatio(Object) <= UseAtRatio))
            return;
        Timer = 0.0f;
        State = 1;
        return;
    case 1:
        // .text:062ecefa `FCOMP Pre; TEST AH,0x1; JNZ` -- C0 is set on less AND on unordered,
        // so only greater-or-equal falls through.
        if (!(Elapsed >= PreUseDelay))
            return;
        // .text:062ecf04-062ecf15: MOV EDX,[EDI+4]; MOVZX EAX,[ESI+0xc]; PUSH -1; LEA ECX,[EDI+4];
        // PUSH 1; PUSH EAX; CALL [EDX+0x58] -- the IObjAffect sub-object's slot 22 with the
        // zero-extended equipment id, 1 and -1.
        static_cast<IObjAffect*>(Object)->slot_58(EquipId, 1, -1);
        Timer = 0.0f;
        State = 2;
        return;
    case 2:
        // .text:062ecee4: same idiom against PostUseDelay.
        if (!(Elapsed >= PostUseDelay))
            return;
        Timer = 0.0f;
        State = 0;
        return;
    default:
        return;
    }
}

RepairManager::RepairManager()
    : Owner(nullptr)        // not written by retail's ctor (FUN_062e6490 writes it)
    , Hull()
    , Shield()
    , CheckTimer(0.0f)      // not written by retail's ctor (FUN_062e6490 zeroes it)
{
}

// FUN_062ecf60, item 1879. Raw disasm Common.dll.txt 252977-253013, `RET 0xc`:
//   MOV ECX,[ESP+Stack[0x8]+0x4]     ; ECX = Arch (argument 2)
//   CALL [EAX+0x10]                  ; Arch->get_class_type()  (Archetype::Root slot 4)
//   CMP EAX,0x1a / JNZ               ; RepairKit
//     MOV DX,[[ESP+8]]               ; *Id (argument 1 is passed by address)
//     [ESI+0x14] = DX; [ESI+0x18] += [ESP+0x10] (Count) ; Hull.EquipId / Hull.Count
//     MOV AL,1; RET 0xc
//   CMP EAX,0x1b / JNZ               ; ShieldBattery
//     [ESI+0x38] = *Id; [ESI+0x3c] += Count             ; Shield.EquipId / Shield.Count
//   MOV AL,1; RET 0xc
// The enumerator never stops the walk. The ported free function this replaces took the
// class-type source as `void*` and dispatched slot 0x10 by hand; the argument is the cargo's
// Archetype::Equipment (IObjInspectImpl::enumerate_cargo pushes CEquip+0xc,
// .text:062c0bcf-062c0bd3), whose slot 4 is get_class_type.
bool RepairManager::enumerate(unsigned short const& Id, Archetype::Equipment const* Arch, int InCount)
{
    int const ClassType = static_cast<int>(Arch->get_class_type());
    if (ClassType == 0x1a)
    {
        Hull.EquipId = Id;
        Hull.Count = Hull.Count + InCount;
        return true;
    }
    if (ClassType == 0x1b)
    {
        Shield.EquipId = Id;
        Shield.Count = Shield.Count + InCount;
    }
    return true;
}

// ============================================================================
// FUN_062ecfb0, item 1880 -- RefreshRepairManagerFromPersonality.
// Raw disasm Common.dll.txt 253015-253078 (57 insns), `this` == ECX == the RepairManager
// (SystemManager+0x14A8) at BOTH call sites (.text:062e68b6 via `LEA EDI,[ESI+0x14a8]` at
// .text:062e67d9, and .text:062e696c via `LEA ECX,[ESI+0x14a8]` directly).
//
// The body is the structural twin of RefreshWeaponFireControlConfig (FUN_062eb6c0, item
// 1848, common_aifirecontrol.cpp): same is_player() gate, same `[+0x1370] - 8` watcher-node
// resolve, same `get_cobject` (vtable +0x150), same `ObjectClass & 0x103 == 0x103` test,
// same `CEqObj::get_behavior_interface() -> IBehaviorManager::get_sys()` pair, and the same
// `Owner->Unknown14 = 1` latch on success. Only the copied field set differs (RepairParams
// here vs the +0x17xx GunUseStruct block there).
//
// .text:062ed023 is spelled `CPlayerTradeOffer::GetTargetPlayerID` by Ghidra. That is an
// /OPT:ICF fold, not a real call: Common.dll.txt:133073-133092 lists the whole fold set
// sharing code address 0x466A0, and it explicitly contains
// `?get_sys@IBehaviorManager@@QAEPAVSystemManager@@XZ`. The return value is immediately
// used as a pointer (`MOV EDI,EAX`, then `MOV EAX,[EDI+0x16f4]`).
//
// Every guard below is retail's own: .text:062ecfb7 (Owner), .text:062ecfee/062ecff5 (the
// watched node is non-null and not the literal 8), .text:062ed001 (get_cobject result),
// .text:062ed01d (behavior interface). ExternalIface is dereferenced WITHOUT a test
// (.text:062ecfbf-062ecfc7), and so is the SystemManager get_sys() returns (.text:062ed031);
// the port's former extra ExternalIface null check is removed.
//
// Returns AL == 1 on every path (.text:062ed068 `MOV AL,0x1`), including the null-Owner
// early out at .text:062ecfb9.
// ============================================================================
bool RefreshRepairManagerFromPersonality(RepairManager* Self)
{
    // .text:062ecfb4  MOV EDI,[ESI+0x4]  /  .text:062ecfb9  JZ LAB_062ed067
    SystemManager* const Owner = Self->Owner;
    if (Owner == nullptr)
        return true;

    // EDI holds the "source" SystemManager for the whole tail; it starts as the owner itself
    // and is only replaced on the fully-successful chain at .text:062ed02b.
    SystemManager* Source = Owner;

    // .text:062ecfbf-062ecfcf  ExternalIface->is_player() (IObjInspect slot 47, +0x0BC).
    if (!Owner->SelfTarget.Iface->is_player()
        // .text:062ecfd4  MOV CL,[EAX+0x1820];  JZ  -> 0x1820-0x1584 == 0x29C
        //                 == FormationParams(+0x248) + 0x54.
        && Owner->PersonalityData.FormationParams.bLeaderMakesMeTougher != 0
        // .text:062ecfde  MOV CL,[EAX+0x1429];  JNZ -> 0x1429-0x1364 == 0xC5.
        && Owner->GunConvergenceStateBytes.bLeaderSuppressed == 0)
    {
        // .text:062ecfe8  MOV EAX,[EAX+0x1370] -> the block's FormationLeaderWatcher
        // .text:062ecff2  LEA ECX,[EAX + -0x8]  (the BaseWatcher node -> object)
        Watchable const* const Node = Owner->GunConvergenceStateBytes.FormationLeaderWatcher.target();
        if (Node != nullptr && Node != (Watchable const*)8)
        {
            IObjRW* const Handle = ObjRwFromWatchable(const_cast<Watchable*>(Node));
            // .text:062ecffb  CALL [EDX+0x150]  -- IObjInspect::cobject, slot 84.
            CObject const* const Obj = Handle->cobject();

            // .text:062ed005-062ed014  (ObjectClass & 0x103) == 0x103
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103) == 0x103)
            {
                IBehaviorManager* const Iface =
                    const_cast<CEqObj*>(static_cast<CEqObj const*>(Obj))->get_behavior_interface();
                if (Iface != nullptr)
                {
                    // .text:062ed023  ICF-folded IBehaviorManager::get_sys().
                    Source = Iface->get_sys();
                    // .text:062ed02d  MOV byte ptr [EDX + 0x14],0x1 -- on the OWNER, not on
                    // Source.
                    Owner->Unknown14 = 1;
                }
            }
        }
    }

    // .text:062ed031-062ed064, six dword copies. Destinations are Hull's
    // PreUseDelay/PostUseDelay/UseAtRatio (+0x1C/+0x20/+0x24 off this) and Shield's
    // (+0x40/+0x44/+0x48); the sources are RepairParams' six floats, in retail's own order.
    pub::AI::Personality::RepairUseStruct const& R = Source->PersonalityData.RepairParams;
    Self->Hull.PreUseDelay = R.UseHullRepairPreDelay;                // [EDI+0x16f4] -> [ESI+0x1c]
    Self->Hull.PostUseDelay = R.UseHullRepairPostDelay;              // [EDI+0x16fc] -> [ESI+0x20]
    Self->Hull.UseAtRatio = R.UseHullRepairAtDamagePercent;          // [EDI+0x16f8] -> [ESI+0x24]
    Self->Shield.PreUseDelay = R.UseShieldRepairPreDelay;            // [EDI+0x16e8] -> [ESI+0x40]
    Self->Shield.PostUseDelay = R.UseShieldRepairPostDelay;          // [EDI+0x16f0] -> [ESI+0x44]
    Self->Shield.UseAtRatio = R.UseShieldRepairAtDamagePercent;      // [EDI+0x16ec] -> [ESI+0x48]

    return true; // .text:062ed068  MOV AL,0x1
}
