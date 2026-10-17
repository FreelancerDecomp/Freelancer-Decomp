// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEPower -- split out of common_cequip.cpp (CMN-B2).
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

CEPower::CEPower(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Power const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypePower, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // .text:0629f378..0629f38f: CEquip ctor (type 0x40000), CEPower table, then
    // CEquip::Activate(true) called directly.
    CEquip::Activate(true);
}

CEPower::CEPower(CEPower const& Other)
    : CInternalEquip(Other)
{
}

CEPower::~CEPower()
{
    // Common.dll.c 66840-66858 (listing 0x3f3b0): retail unconditionally force-calls the
    // non-virtual CEquip::Destroy(this) here (bDestroyed=1 + EquipDestroying notify) as a
    // safety net during unwind, regardless of whether game logic already called ->Destroy()
    // on this equip before deleting it. CEquipManager::Clear()/CleanUp() (this file) delete
    // CEquip* directly with no preceding ->Destroy() call, so without this the notification
    // and bDestroyed flag would silently never fire for power equip destroyed this way.
    CEquip::Destroy();
}

bool CEPower::Update(float Dt, unsigned Flags)
{
    // Common.dll.c 66863-66907 (listing 0x3f410): recharge the owner's main power capacitor
    // every tick while this power plant IsFunctioning(). Note this checks IsDestroyed()
    // separately from (and nested inside) the outer IsFunctioning() gate, and reads
    // Arch->ChargeRate directly rather than through GetChargeRate() -- retail's inline here
    // skips GetChargeRate()'s extra Arch-null/class-type guards, so they are intentionally
    // not reproduced (Arch is always a valid Archetype::Power for a constructed CEPower).
    // x87 (.text:0629f43a..0629f460 / 0629f497..0629f4ba): rate * Dt + current stays on the FPU
    // stack; FCOMPP against the cap, TEST AH,0x5 / JNP keeps the sum only when it is below the
    // cap -- equal, above or unordered store the cap -- then one FSTP float.
    if (IsFunctioning())
    {
        float const ChargeRate = IsDestroyed() ? 0.0f : PowerArch()->ChargeRate;
        double const NewPower = (double)(ChargeRate) * Dt + Owner->Power;
        Owner->Power = (NewPower < Owner->MaxPower) ? (float)(NewPower) : Owner->MaxPower;

        // Second capacitor (CShip::ThrustPower/MaxThrustPower, the thrust/afterburner reserve) only
        // exists on ships -- retail gates this half on Owner (null test at 0629f469) and
        // Owner->ObjectClass matching ClassShip's bit pattern before reinterpreting Owner.
        if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        {
            float const ThrustChargeRate =
                IsDestroyed() ? 0.0f : PowerArch()->ThrustChargeRate;
            CShip* Ship = (CShip*)(Owner);
            double const NewThrust = (double)ThrustChargeRate * Dt + Ship->ThrustPower;
            Ship->ThrustPower = (NewThrust < Ship->MaxThrustPower) ? (float)NewThrust : Ship->MaxThrustPower;
        }
    }
    return CEquip::Update(Dt, Flags);
}

float CEPower::GetChargeRate() const
{
    // .text:06294b40..06294b5b: IsDestroyed() (vtable +0x8) -> 0.0f, else Arch (+0xc, no
    // null or class test)->ChargeRate (+0x74).
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ChargeRate;
}

float CEPower::GetCapacity() const
{
    // .text:06294b60..06294b7b: IsDestroyed() -> 0.0f, else Arch->Capacity (+0x70).
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->Capacity;
}

float CEPower::GetThrustCapacity() const
{
    // .text:06294ba0..06294bbb: IsDestroyed() -> 0.0f, else Arch->ThrustCapacity (+0x78).
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ThrustCapacity;
}

float CEPower::GetThrustChargeRate() const
{
    // .text:06294b80..06294b9b: IsDestroyed() -> 0.0f, else Arch->ThrustChargeRate (+0x7c).
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ThrustChargeRate;
}

CEPower* __cdecl CEPower::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypePower)
        return nullptr;
    return (CEPower*)(Equip);
}

CEPower const* __cdecl CEPower::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

Archetype::Power const* CEPower::PowerArch() const
{
    return (Archetype::Power const*)(Arch);
}
