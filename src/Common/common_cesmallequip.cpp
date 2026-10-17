// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEArmor, CECargoPod, CERepairDroid, CETradeLaneEquip -- split out of common_cequip.cpp (CMN-B2).
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

CEArmor::CEArmor(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Armor const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypeArmor, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Activate(true);
}

CEArmor::CEArmor(CEArmor const& Other)
    : CInternalEquip(Other)
{
}

CEArmor::~CEArmor()
{
    // Common.dll.c 57139-57157 (0x351d0): retail force-calls the non-virtual CEquip::Destroy()
    // here unconditionally as a safety net during unwind -- same pattern as the
    // CEPower/CEAttachedFX/CERepairDroid/CEInternalFX/CECargo destructors.
    CEquip::Destroy();
}

Archetype::Armor const* CEArmor::ArmorArch() const
{
    return (Archetype::Armor const*)Arch;
}

CECargoPod::CECargoPod(CEqObj* OwnerObj, unsigned short EquipId, Archetype::CargoPod const* ArchObj,
                       bool bMissionFlag)
    : CAttachedEquip(EquipTypeCargoPod, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Cargo = nullptr;
    Activate(true);
}

CECargoPod::CECargoPod(CECargoPod const& Other)
    : CAttachedEquip(Other)
    , Cargo(Other.Cargo)
{
}

CECargoPod::~CECargoPod()
{
    // Common.dll.c 57580-57602 (0x358f0): retail's ~CECargoPod() inlines the same unlink logic
    // as EmptyPod() (checks Cargo/this+0x4c, clears both this->Cargo and Cargo->CargoPod)
    // BEFORE running the base Destroy() chain (CExternalEquip::Destroy -> CEquip::Destroy +
    // Disconnect), called here unconditionally exactly as retail does (even if the pod's
    // Destroy() was already invoked earlier through game logic). Its sibling ~CECargo()
    // (see above) performs the mirror-image unlink-then-Destroy() sequence too, just via a
    // call into the linked CECargoPod::EmptyPod() instead of inlining it -- both destructors
    // are the same "unlink + force Destroy()" pattern, not an asymmetry.
    EmptyPod();
    CExternalEquip::Destroy();   // 06295928: direct call
}

// Common.dll.c 0x35980. Links the two objects together: this->Cargo = Cargo, and
// Cargo->CargoPod = this (CECargo::CargoPod is the existing +0x20 back-pointer).
void CECargoPod::FillPod(CECargo* CargoObj)
{
    // Listing 0x06295980-0x0629598a: both stores, no null test.
    Cargo = CargoObj;
    CargoObj->CargoPod = this;
}

void CECargoPod::EmptyPod()
{
    // Common.dll.c 0x35990: if still linked, null out the CECargo's back-pointer to this pod,
    // then null out our own Cargo pointer. Symmetric inverse of FillPod.
    if (Cargo != nullptr)
    {
        Cargo->CargoPod = nullptr;
        Cargo = nullptr;
    }
}

bool CECargoPod::IsLootable() const
{
    // Common.dll.c 0x35950: lootable iff this pod currently holds a CECargo whose
    // stack count is non-zero (an emptied or unfilled pod isn't worth looting).
    return Cargo != nullptr && Cargo->Count != 0;
}

CECargoPod* __cdecl CECargoPod::cast(CEquip* Equip)
{
    // Common.dll.c 0x359a0
    if (Equip == nullptr || Equip->GetType() != EquipTypeCargoPod)
        return nullptr;
    return (CECargoPod*)Equip;
}

CECargoPod const* __cdecl CECargoPod::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

CERepairDroid::CERepairDroid(CEqObj* OwnerObj, unsigned short EquipId,
                             Archetype::RepairDroid const* ArchObj, bool bMissionFlag)
    : CInternalEquip(EquipTypeRepairDroid, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // Common.dll.txt 120523-120549 (0x3fbc0): retail's primary ctor never writes this+0x20
    // (HitPts) at all -- it is left as whatever garbage the allocator handed back. Zero-
    // initializing here is a deliberate, documented deviation (a safe, deterministic stand-in
    // for retail's uninitialized field) rather than an attempt to replicate that quirk.
    HitPts = 0.0f;
}

CERepairDroid::CERepairDroid(CERepairDroid const& Other)
    : CInternalEquip(Other)
    , HitPts(Other.HitPts)
{
    // Common.dll.c 56973-56995 (0x34e00). Base-then-derived copy shape: CInternalEquip(Other)
    // stamps the intermediate CInternalEquip vtable, then this class's own vtable is restamped.
}

CERepairDroid::~CERepairDroid()
{
    // Common.dll.c 67354-67373 (listing 0x3fbf0): retail unconditionally force-calls the
    // non-virtual CEquip::Destroy(this) here (bDestroyed=1 + EquipDestroying notify) as a
    // safety net during unwind. CEquipManager::Clear()/CleanUp() (this file) delete CEquip*
    // directly with no preceding ->Destroy() call, so without this the notification and
    // bDestroyed flag would silently never fire for repair-droid equip destroyed this way.
    CEquip::Destroy();
}

float CERepairDroid::GetHitPoints() const
{
    // Common.dll.c 0x34df0
    return HitPts;
}

void CERepairDroid::SetHitPoints(float Hit)
{
    // Common.dll.txt 120594-120612 (listing 0x3fc50). Stores Hit, then if it has dropped to
    // or below 0.0f, re-dispatches virtually through vtable+0x30 -- confirmed to be
    // CEquip::Activate's slot (Common.dll.txt 558301-558330, the raw CEquip::`vftable' dump
    // at 0x639bdcc: slot 0x30 resolves to the real, non-folded CEquip::Activate, not this
    // class's own SetHitPoints) -- with Activate(false); the pushed 0 stack operand is the
    // bool argument, not a recursive SetHitPoints(0.0f) call (both share the same 1-argument
    // RET 4 shape, which is why Ghidra's decompiled jumptable recovery failed here and just
    // emitted an untyped indirect call).
    HitPts = Hit;
    if (Hit <= 0.0f)
        Activate(false);
}

CERepairDroid* __cdecl CERepairDroid::cast(CEquip* Equip)
{
    // Common.dll.c 0x3fc80
    if (Equip == nullptr || Equip->GetType() != EquipTypeRepairDroid)
        return nullptr;
    return (CERepairDroid*)Equip;
}

CERepairDroid const* __cdecl CERepairDroid::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}


// ============================================================
// CETradeLaneEquip — tradelane jump equipment
// ============================================================

CETradeLaneEquip::CETradeLaneEquip(CEqObj* OwnerObj, unsigned short EquipId,
                                   Archetype::TradeLaneEquip const* ArchObj, bool bMissionFlag)
    : CInternalEquip(EquipTypeTradeLane, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CETradeLaneEquip::CETradeLaneEquip(CETradeLaneEquip const& Other)
    : CInternalEquip(Other)
{
}

CETradeLaneEquip::~CETradeLaneEquip()
{
    // Listing 0x06294ef0-0x06294f3e: the direct, non-virtual CEquip::Destroy (06294f1b).
    CEquip::Destroy();
}

CETradeLaneEquip* __cdecl CETradeLaneEquip::cast(CEquip* Equip)
{
    // Listing 0x0629fca0: both overloads share this body.
    if (Equip == nullptr || Equip->GetType() != EquipTypeTradeLane)
        return nullptr;
    return (CETradeLaneEquip*)Equip;
}

CETradeLaneEquip const* __cdecl CETradeLaneEquip::cast(CEquip const* Equip)
{
    return cast((CEquip*)Equip);
}

Archetype::TradeLaneEquip const* CETradeLaneEquip::TradeLaneArch() const
{
    return (Archetype::TradeLaneEquip const*)Arch;
}

Archetype::RepairDroid const* CERepairDroid::RepairDroidArch() const
{
    return (Archetype::RepairDroid const*)Arch;
}

CEArmor* __cdecl CEArmor::cast(CEquip* Equip)
{
    // Listing 0x35220: null-check, then Type == 0x1000000.
    if (Equip == nullptr || Equip->GetType() != EquipTypeArmor)
        return nullptr;
    return (CEArmor*)Equip;
}

CEArmor const* __cdecl CEArmor::cast(CEquip const* Equip)
{
    return cast((CEquip*)Equip);
}

void CECargoPod::Destroy()
{
    // Listing 0x35970: JMP CExternalEquip::Destroy.
    CExternalEquip::Destroy();
}

CECargo const* CECargoPod::GetContents() const
{
    // Listing 0x369c0 (ICF-shared): MOV EAX,[ECX+0x4c]; RET.
    return Cargo;
}
