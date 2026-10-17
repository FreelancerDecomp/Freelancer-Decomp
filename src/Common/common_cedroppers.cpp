// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEMineDropper, CECounterMeasureDropper -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect)
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_cequip_internal.h"

using namespace CmnCequip;

CEMineDropper::CEMineDropper(CEqObj* OwnerObj, unsigned short EquipId,
                             Archetype::MineDropper const* ArchObj, bool bMissionFlag)
    : CELauncher(EquipTypeMineDropper, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CEMineDropper::CEMineDropper(CEMineDropper const& Other)
    : CELauncher(Other)
{
}

CEMineDropper::~CEMineDropper()
{
    // Common.dll.c: retail's ~CEMineDropper() is identical-code-folded with
    // ~CECounterMeasureDropper() at the same address (both are empty derived dtors that add
    // no fields beyond CELauncher) -- the Destroy()/Projectile-Release()/base-dtor work
    // happens in the automatically-invoked ~CELauncher(), not here. Empty body is correct.
}

FireResult CEMineDropper::CanFire(Vector const& Target) const
{
    // Common.dll.c 61604-61627 (0x3a270): defer to CELauncher::CanFire; if the launcher itself
    // reports FireOk, additionally block firing (FireCruise) when the owner is a CShip
    // currently in cruise mode -- mines can't be dropped while cruising. (Decomp inlines a
    // redundant re-check of the same "ship && ObjectClass==ClassShip" condition that can never
    // be false at that point -- a decompiler artifact, not additional logic -- so it is
    // collapsed here.)
    FireResult Result = CELauncher::CanFire(Target);
    if (Result == FireOk && Owner != nullptr &&
        (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        CShip* Ship = (CShip*)Owner;
        if (Ship->is_cruise_active())
            return FireCruise;
    }
    return Result;
}

CEMineDropper* __cdecl CEMineDropper::cast(CEquip* Equip)
{
    // Common.dll.c 0x3a2d0 (?cast@CEMineDropper@@...): type must be EquipTypeMineDropper (0x20).
    if (Equip == nullptr || Equip->GetType() != EquipTypeMineDropper)
        return nullptr;
    return (CEMineDropper*)Equip;
}

CEMineDropper const* __cdecl CEMineDropper::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

CECounterMeasureDropper::CECounterMeasureDropper(CEqObj* OwnerObj, unsigned short EquipId,
                                                 Archetype::CounterMeasureDropper const* ArchObj,
                                                 bool bMissionFlag)
    : CELauncher(EquipTypeCmDropper, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CECounterMeasureDropper::CECounterMeasureDropper(CECounterMeasureDropper const& Other)
    : CELauncher(Other)
{
    // Listing 0x62996bc0: straight memberwise copy through CELauncher's full extent (base-then-
    // derived vtable relocation ending in CECounterMeasureDropper::`vftable', no fields of its
    // own beyond CELauncher, same pattern as CEMineDropper's copy ctor above).
}

CECounterMeasureDropper::~CECounterMeasureDropper()
{
}

bool CECounterMeasureDropper::Update(float Dt, unsigned Flags)
{
    // Listing 0x0629a320-0x0629a490. When the dropper works, Flags lacks 0x20 and the AI range
    // is positive, walk the owner scanner's contacts: the first missile (get_type 0x1000000)
    // targeting the owner (get_target -> get_id == Owner->Id) within AiRangeSq (pre-squared)
    // fires Notify(Event_0x19) and ends the walk. The epilogue 0629a45e-0629a484 is
    // CELauncher::Update inlined (refire accumulate below 10000.0f, CAttachedEquip::Update).
    if (IsFunctioning() && (Flags & 0x20) == 0)                        // 0629a32b / 0629a33a
    {
        Archetype::CounterMeasureDropper const* CmArch = (Archetype::CounterMeasureDropper const*)Arch;
        // 0629a349: FCOMP 0.0f, TEST AH,0x41 / JNZ -> only a strictly positive range goes on.
        if (CmArch->AiRangeSq > 0.0f)
        {
            // 0629a36e: FindFirst's result is used as the scanner with no cast and no test.
            CEScanner const* Scanner = (CEScanner const*)Owner->EquipManager.FindFirst(EquipTypeScanner);
            for (int Index = 0; Index < Scanner->Scans.Count; ++Index)
            {
                Watchable const* Watched = Scanner->Scans.Entries[Index].target();
                if (Watched == nullptr)
                    continue;
                // 0629a39c: ESI = Watched - 8 (IObjRW's Watchable base), TEST ESI,ESI.
                IObjRW* Tracked = (IObjRW*)((char*)Watched - 8);
                if (Tracked == nullptr)
                    continue;

                unsigned Kind = 0;                  // retail leaves the slot unset before the call
                Tracked->get_type(Kind);            // vt+0x88
                if (Kind != 0x1000000)
                    continue;

                IObjRW* SubObj = nullptr;           // 0629a3c9
                Tracked->get_target(SubObj);        // vt+0x74
                if (SubObj == nullptr)
                    continue;
                if (SubObj->get_id() != Owner->Id)  // vt+0x20 vs [Owner+0xb0]
                    continue;

                Vector ShipPos;
                GetCenterOfMass(ShipPos);           // vt+0x70
                Vector const& TrackedPos = Tracked->get_position();   // vt+0x00

                // 0629a400-0629a438: the differences and the sum stay on the FPU, summed as
                // (dz^2 + dy^2) + dx^2; FCOMP vs AiRangeSq, TEST AH,0x5 / JNP: only "below".
                double const Dx = (double)TrackedPos.x - ShipPos.x;
                double const Dy = (double)TrackedPos.y - ShipPos.y;
                double const Dz = (double)TrackedPos.z - ShipPos.z;
                if (Dz * Dz + Dy * Dy + Dx * Dx < CmArch->AiRangeSq)
                {
                    Notify(INotify::Event_0x19, nullptr);   // 0629a459, then the epilogue
                    break;
                }
            }
        }
    }

    return CELauncher::Update(Dt, Flags);
}

CECounterMeasureDropper* __cdecl CECounterMeasureDropper::cast(CEquip* Equip)
{
    // Common.dll.c 61730-61739 (0x3a4a0): type must be EquipTypeCmDropper (0x40).
    if (Equip == nullptr || Equip->GetType() != EquipTypeCmDropper)
        return nullptr;
    return (CECounterMeasureDropper*)Equip;
}

CECounterMeasureDropper const* __cdecl CECounterMeasureDropper::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

Archetype::MineDropper const* CEMineDropper::MineDropperArch()
{
    return (Archetype::MineDropper const*)Arch;
}

Archetype::Mine const* CEMineDropper::MineArch()
{
    // Listing 0x369c0 (ICF-shared): MOV EAX,[ECX+0x4c]; RET.
    return (Archetype::Mine const*)Projectile;
}

Archetype::CounterMeasureDropper const* CECounterMeasureDropper::CounterMeasureDropperArch()
{
    return (Archetype::CounterMeasureDropper const*)Arch;
}

Archetype::CounterMeasure const* CECounterMeasureDropper::CounterMeasureArch()
{
    // Listing 0x369c0 (ICF-shared): MOV EAX,[ECX+0x4c]; RET.
    return (Archetype::CounterMeasure const*)Projectile;
}
