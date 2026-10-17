// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CECargo (retail CECargo.cpp) -- split out of common_cequip.cpp (CMN-B2).
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
#include "common_cequip_internal.h"   // CallDacomStricmp (DACOM.DLL!stricmp, IAT 0639902c)

using namespace CmnCequip;

namespace
{
    const char kCECargoSource[] = "E:\\FL\\Scratch\\Source\\Common\\CECargo.cpp";
}

CECargo::CECargo(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Equipment const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypeCargo, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    CargoPod = nullptr;
    HitPts = 0.0f;
    Count = 0;
    CEquip::Activate(false);   // 06295495: direct, non-virtual call
}

CECargo::CECargo(CECargo const& Other)
    : CInternalEquip(Other)
    , CargoPod(Other.CargoPod)
    , HitPts(Other.HitPts)
    , Count(Other.Count)
{
}

CECargo::~CECargo()
{
    // Common.dll.c 57275-57296 (0x354b0): retail's actual destructor performs the same
    // CargoPod-unlink as Destroy() (unconditionally, even if ->Destroy() already ran earlier
    // through game logic) as a safety net during unwind, then force-calls the non-virtual
    // CEquip::Destroy() -- same pattern as the CEPower/CEAttachedFX/CERepairDroid/CEInternalFX
    // destructors.
    if (CargoPod != nullptr)
        CargoPod->EmptyPod();
    CEquip::Destroy();
}

bool CECargo::IsDestroyed() const
{
    if (Count != 0)
        return CEquip::IsDestroyed();
    return true;
}

void CECargo::Destroy()
{
    // Common.dll.c 0x357f0: retail calls into the linked pod to clear the link symmetrically
    // (CargoPod->EmptyPod() nulls both CargoPod->Cargo and this->CargoPod), rather than just
    // dropping this side's pointer and leaving the pod with a stale back-reference.
    if (CargoPod != nullptr)
        CargoPod->EmptyPod();
    CEquip::Destroy();
}

void CECargo::Init(float HealthFrac, unsigned StackCount, CacheString const& Hardpoint)
{
    // Listing 0x06295510..0x06295628.
    HitPts = GetMaxHitPoints() * HealthFrac;   // vt+0x38
    Count = StackCount;

    char const* Hp = Hardpoint.Value;
    if (Hp[0] == '\0')
        return;
    if (CallDacomStricmp(Hp, EquipDesc::CARGO_BAY_HP_NAME.Value) == 0)   // DACOM stricmp
        return;

    // First connected cargo pod (vt+0x58 IsConnected) whose GetParentHPName (vt+0x60)
    // matches Hp; the walk ends at the first equip CECargoPod::cast rejects.
    CEquipTraverser Walk(EquipTypeCargoPod);   // 0x800
    CECargoPod* Pod = CECargoPod::cast(Owner->EquipManager.Traverse(Walk));
    while (Pod != nullptr)
    {
        if (Pod->IsConnected() && CallDacomStricmp(Pod->GetParentHPName().Value, Hp) == 0)
        {
            CargoPod = Pod;
            Pod->FillPod(this);
            break;
        }
        Pod = CECargoPod::cast(Owner->EquipManager.Traverse(Walk));
    }

    if (CargoPod == nullptr)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: %s cannot find a cargo pod attached to hardpoint %s for %s.",
              kCECargoSource, 0x39, IdentifyEquipment(), Hp,
              Owner->Archetype->Nickname.get_string());
    }
}

void CECargo::AddToStack(unsigned Amount, float HealthFrac)
{
    // Listing 0x06295630-0x062956cc. The partial-unit fraction is GetRelativeHealth (vt+0x44,
    // left unrounded in ST0). Every compare against 1.0f (.rdata:0639a1d0) is FCOMP /
    // TEST AH,0x5 / JP: the jump is taken unless C0 alone is set, i.e. on >= and on NaN,
    // hence the `!(x < 1)` predicates.
    if (!(GetRelativeHealth() < 1.0))                   // 06295636-06295644
    {
        Count += Amount;                                // 062956aa
    }
    else
    {
        if (!(HealthFrac < 1.0f))                       // 06295646-06295655
        {
            Count += Amount;                            // 06295699: no SetHitPoints
            return;
        }
        HealthFrac = (float)(GetRelativeHealth() + HealthFrac);   // 0629565b-06295669 FSTP float
        if (!(HealthFrac < 1.0f))                       // 0629566f-0629567e
        {
            Count += Amount;
            HealthFrac -= 1.0f;                         // 06295686-06295693
        }
        else
        {
            Count += Amount - 1;                        // 06295680 DEC
        }
    }
    SetHitPoints(GetMaxHitPoints() * HealthFrac);       // 062956b6-062956c7 vt+0x38 / vt+0x40
}

void CECargo::RemoveFromStack(unsigned Amount)
{
    if (Count < Amount)
        Count = 0;
    else
        Count -= Amount;
    SetHitPoints(GetMaxHitPoints());
}

float CECargo::GetHitPoints() const
{
    // Common.dll.c 0x35810
    return HitPts;
}

void CECargo::SetHitPoints(float Hit)
{
    // Common.dll.c 0x35820: *(this+0x24) = Hit. Overrides the base CEquip
    // no-op so cargo hit points can actually be set (e.g. CEqObj::add_item).
    HitPts = Hit;
}

bool CECargo::GetEquipDesc(EquipDesc& Desc) const
{
    // Differs from CEquip::GetEquipDesc: bails out on Count == 0, reports the stack Count,
    // set_equipped(false), and takes the hardpoint from the linked pod (else make_internal).
    // Listing 0x06295750-0x062957da: no Arch null test (0629575a); the health is the inline
    // HitPts / Arch->HitPts (0629577b-06295787, FSTP float), not the virtual GetRelativeHealth;
    // the pod's hardpoint comes from its vt+0x60 GetParentHPName (062957b7).
    if (Count == 0)
        return false;
    Desc.set_arch_id(Arch->get_id());
    Desc.set_id(Id);
    Desc.set_status(HitPts / Arch->HitPts);
    Desc.Count = (int)Count;
    Desc.set_equipped(false);
    Desc.bMission = bMission;
    if (CargoPod != nullptr)
    {
        Desc.set_hardpoint(CargoPod->GetParentHPName());
        return true;
    }
    Desc.make_internal();
    return true;
}

unsigned CECargo::GetCount() const
{
    // Common.dll.c 0xdbaf0: `MOV EAX,dword ptr [this+0x28]; RET`.
    return Count;
}

void CECargo::SetCount(unsigned NewCount)
{
    // Common.dll.txt 0xdbae0: `MOV dword ptr [this+0x28],param_1; RET`.
    Count = NewCount;
}

float CECargo::GetDecayDamagePerSecond() const
{
    // Listing 0x06295710-0x06295730: Arch's vt+0x10 compared with 0xa, no Arch null test.
    if (Arch->get_class_type() != Archetype::AClassCommodity)
        return 0.0f;                                    // .rdata:06399408
    return ((Archetype::Commodity*)Arch)->get_decay_per_second();   // tail JMP
}

CECargo* __cdecl CECargo::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeCargo)
        return nullptr;
    return (CECargo*)Equip;
}

CECargo const* __cdecl CECargo::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

unsigned CECargo::GetType() const
{
    // Listing 0x35740: MOV EAX,0x10000; RET.
    return EquipTypeCargo;
}

CECargoPod const* CECargo::GetContainer() const
{
    // Listing 0x3d880 (ICF-shared): MOV EAX,[ECX+0x20]; RET.
    return CargoPod;
}
