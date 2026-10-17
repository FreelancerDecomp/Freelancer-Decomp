// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CELightEquip, CEAttachedFX, CEInternalFX -- split out of common_cequip.cpp (CMN-B2).
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

CELightEquip::CELightEquip(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Light const* ArchObj,
                           bool bMissionFlag)
    : CExternalEquip(EquipTypeLight, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // .text:0629f180-0629f202: CEquip(1, ...) plus the inlined CExternalEquip ctor (+0x20
    // cleared, +0x24 = -1), vftable, then the ctor argument's bAlwaysOn (ArchObj+0x70) set ->
    // the direct, non-virtual CEquip::Activate(true) (0629f1eb), so always-on lights start
    // active.
    if (ArchObj->bAlwaysOn)
        CEquip::Activate(true);
}

CELightEquip::CELightEquip(CELightEquip const& Other)
    : CExternalEquip(Other)
{
    // Common.dll.c 54911 (0x329b0): plain field-by-field copy (CELightEquip adds no fields
    // of its own beyond CExternalEquip's), same shape as CExternalEquip's own copy ctor.
}

CELightEquip::~CELightEquip()
{
    // Listing 0x3f210 (119313-119346): retail force-calls the non-virtual CEquip::Destroy()
    // (bDestroyed=1 + EquipDestroying notify), then dispatches through the vtable+0x74 slot,
    // which this listing resolves unambiguously to CExternalEquip::Disconnect (no override
    // exists on CELightEquip, so the plain virtual call below reproduces that dispatch).
    CEquip::Destroy();
    Disconnect();
}

CELightEquip* __cdecl CELightEquip::cast(CEquip* Equip)
{
    // Common.dll.c 0x3f270
    if (Equip == nullptr || Equip->GetType() != EquipTypeLight)
        return nullptr;
    return (CELightEquip*)(Equip);
}

CELightEquip const* __cdecl CELightEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

CEAttachedFX::CEAttachedFX(CEqObj* OwnerObj, unsigned short EquipId,
                           Archetype::AttachedFXEquip const* ArchObj,
                           bool bMissionFlag)
    : CExternalEquip(EquipTypeAttachedFX, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CEAttachedFX::CEAttachedFX(CEAttachedFX const& Other)
    : CExternalEquip(Other)
{
    // Common.dll.c 54884 (0x328f0): plain field-by-field copy (CEAttachedFX adds no fields
    // of its own beyond CExternalEquip's), same shape as CExternalEquip's own copy ctor.
}

CEAttachedFX::~CEAttachedFX()
{
    // Listing 0x0629f0b0..0x0629f10b: CEquip::Destroy (qualified), then the vtable+0x74 call
    // with this class's vptr still installed = Disconnect, which CEAttachedFX inherits from
    // CExternalEquip (vftable slot 0639c590 -> 0629ede0); then the inlined (trivial)
    // ~CExternalEquip and CEquip::~CEquip.
    CEquip::Destroy();
    Disconnect();
}

CEAttachedFX* __cdecl CEAttachedFX::cast(CEquip* Equip)
{
    // Common.dll.c 66665-66674 (0x3f160). Both const and non-const overloads share the
    // same entry point in retail.
    if (Equip == nullptr || Equip->GetType() != EquipTypeAttachedFX)
        return nullptr;
    return (CEAttachedFX*)(Equip);
}

CEAttachedFX const* __cdecl CEAttachedFX::cast(CEquip const* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeAttachedFX)
        return nullptr;
    return (CEAttachedFX const*)(Equip);
}

CEInternalFX::CEInternalFX(CEqObj* OwnerObj, unsigned short EquipId,
                           Archetype::InternalFXEquip const* ArchObj,
                           bool bMissionFlag)
    : CInternalEquip(EquipTypeInternalFX, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // Listing .text:0629fcc0 .. .text:0629fd95 (RET 0x10). Anim starts at -1 (0629fd07). The
    // archetype's use_animation name (+0x70) is tested by its first char only (0629fd17); a
    // non-empty one starts the ambient playback: PlayScript (Anim vt+0x24) with the owner's
    // archetype script index (CAsteroid::asteroidarch, the ICF-folded `MOV EAX,[ECX+0x88]`,
    // then +0x44), the owner's engine instance (+0x4), the name, 0, 0 -> +0x20; when that is
    // not -1, EvalPlayback (vt+0x2c) pushing (handle, 2, -2.0f, 1.0f, 0, 1.0f, 1.0f, 0) and
    // AnimDB::Add(+0x20). No null test on the archetype, the owner's archetype or DALib::Anim.
    Archetype::InternalFXEquip const* const FxArch = ArchObj;
    char const* const AnimName = FxArch->UseAnimation.Value;
    if (AnimName[0] != '\0')
    {
        Anim.Index = DALib::Anim->PlayScript(AsOpaqueDword(OwnerObj->Archetype->get_script_index()),
                                             AsOpaqueDword(OwnerObj->EngineInstanceId),
                                             AnimName, nullptr, nullptr);
        if (Anim.Index != -1)
        {
            DALib::Anim->EvalPlayback(Anim.Index, 2, -2.0f, 1.0f, nullptr,
                                      AsOpaqueDword(0x3f800000),
                                      AsOpaqueDword(0x3f800000), nullptr);
            AnimDB::Add(Anim.Index);
        }
    }
}

CEInternalFX::CEInternalFX(CEInternalFX const& Other)
    : CInternalEquip(Other)
    , Anim(Other.Anim)
{
    // Common.dll.c 57065-57085 (0x34ff0). Base-then-derived copy shape: CInternalEquip(Other)
    // stamps the intermediate CInternalEquip vtable, then this class's own vtable is restamped.
}

CEInternalFX::~CEInternalFX()
{
    // Listing .text:0629fda0 .. .text:0629fe68. Body: a live handle is AnimDB::Rem'd and
    // released (FAnimScriptRef::Release inlined, 0629fdd3..0629fe03), then the non-virtual
    // CEquip::Destroy (0629fe0c). Member destruction follows: Anim's destructor inlined
    // (0629fe11..0629fe3d, a no-op once released), then ~CEquip.
    if (Anim.Index != -1)
    {
        AnimDB::Rem(Anim.Index);
        Anim.Release();
    }
    CEquip::Destroy();
}

void CEInternalFX::Destroy()
{
    // Listing .text:0629fe70 .. .text:0629feb5: the same release as the destructor body, then
    // a tail JMP to the non-virtual CEquip::Destroy.
    if (Anim.Index != -1)
    {
        AnimDB::Rem(Anim.Index);
        Anim.Release();
    }
    CEquip::Destroy();
}

CEInternalFX* __cdecl CEInternalFX::cast(CEquip* Equip)
{
    // Common.dll.c 0x3fec0
    if (Equip == nullptr || Equip->GetType() != EquipTypeInternalFX)
        return nullptr;
    return (CEInternalFX*)(Equip);
}

CEInternalFX const* __cdecl CEInternalFX::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

Archetype::Light const* CELightEquip::LightArch() const
{
    return (Archetype::Light const*)(Arch);
}

Archetype::AttachedFXEquip const* CEAttachedFX::FXArch() const
{
    return (Archetype::AttachedFXEquip const*)(Arch);
}

Archetype::InternalFXEquip const* CEInternalFX::InternalFXArch() const
{
    return (Archetype::InternalFXEquip const*)(Arch);
}

void CELightEquip::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    // Listing 0x3f110, one body shared with CEAttachedFX. vtable+0x58 is IsConnected, the
    // owner's vtable+0xf8 is CEqObj::sub_obj_id_to_inst, vtable+0x74 is Disconnect, and the
    // notification is EquipDestroying (0xa) with the equip id as its parameter.
    if (!IsConnected())
        return;
    long const GroupInst = Owner->sub_obj_id_to_inst(SubObjId);
    if (GroupInst == -1)
        return;
    if (!IsDescendant(GroupInst, ConnectionIndex))
        return;
    Disconnect();
    Notify(INotify::EquipDestroying, (void*)(uintptr_t)Id);
}

void CEAttachedFX::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    // Same body as CELightEquip::NotifyArchGroupDestroyed (listing 0x3f110).
    if (!IsConnected())
        return;
    long const GroupInst = Owner->sub_obj_id_to_inst(SubObjId);
    if (GroupInst == -1)
        return;
    if (!IsDescendant(GroupInst, ConnectionIndex))
        return;
    Disconnect();
    Notify(INotify::EquipDestroying, (void*)(uintptr_t)Id);
}
