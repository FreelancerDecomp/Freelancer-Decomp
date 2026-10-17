// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CECloakingDevice -- split out of common_cequip.cpp (CMN-B2).
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

CECloakingDevice::CECloakingDevice(CEqObj* OwnerObj, unsigned short EquipId,
                                   Archetype::CloakingDevice const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(EquipTypeCloak, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , FadeTarget(0)
    , BaselineState(0)
{
    // .text:06294460-06294516: retail inlines the CAttachedEquip ctor body (the base ctor
    // above), then +0x50 = +0x51 = 0 (the pad bytes +0x52/+0x53 and the timer +0x4c are not
    // written), calls CECloakingDevice::Activate(true) directly (062944f3), and finally copies
    // Arch->CloakOutTime (+0x90) into the timer, overwriting what Activate stored.
    CECloakingDevice::Activate(true);
    CloakTimer = CloakArch()->CloakOutTime;
}

CECloakingDevice::CECloakingDevice(CECloakingDevice const& Other)
    : CAttachedEquip(Other)
    , CloakTimer(Other.CloakTimer)
    , FadeTarget(Other.FadeTarget)
    , BaselineState(Other.BaselineState)
{
    // Common.dll.c 55259 (0x32fb0): plain field-by-field copy re-stamping the vtable at
    // each base level (CEquip::_vftable_, CExternalEquip::_vftable_, CAttachedEquip::
    // _vftable_, then this class's own) -- ordinary MSVC base-then-derived copy-ctor
    // codegen, reproduced here via CAttachedEquip(Other)'s own copy plus this level's own
    // fields. Retail only copies the first 6 of the trailing 8 bytes here (the dword at
    // +0x4c plus the two bytes at +0x50/+0x51) -- the trailing +0x52/+0x53 pad bytes are
    // left uninitialized by this ctor, so Pad52 is intentionally not copied.
}

CECloakingDevice::~CECloakingDevice()
{
    // Common.dll.c 56458 (0x34520): retail force-calls the non-virtual CExternalEquip::
    // Destroy() here (bDestroyed=1 + EquipDestroying notify, then Disconnect()) as a safety
    // net during unwind -- same "Destroy() force-call" pattern as CEPower/CEAttachedFX/
    // CERepairDroid/CEInternalFX's destructors in this file. The Debris/Attachment/Arch
    // cleanup that follows in the decompilation is identical to ~CAttachedEquip()'s body
    // and runs automatically as that base destructor executes after this one returns.
    CExternalEquip::Destroy();
}

bool CECloakingDevice::Update(float Dt, unsigned Flags)
{
    // .text:06294660-0629476c.
    //  - vt+8 IsDestroyed false and FadeTarget != BaselineState: Total = FadeTarget ? Arch+0x90
    //    (CloakOutTime) : Arch+0x8c (CloakInTime); FLD Dt; FADD timer; FST float timer -- the
    //    compare (FCOMP vs Total, TEST AH,0x41 / JNZ) uses the unrounded sum: only a strictly
    //    greater, ordered sum settles the fade (BaselineState = FadeTarget, timer = 0).
    //  - Owner non-null: Owner+0x140 = the inlined cloak_percent() body, one FSTP float.
    //  - Tail: vt+8 IsDestroyed and Debris (+0x2c) -> true; else CExternalEquip::Update.
    if (!IsDestroyed() && FadeTarget != BaselineState)
    {
        Archetype::CloakingDevice const* CloakingArch = CloakArch();
        float const Total = (FadeTarget != 0) ? CloakingArch->CloakOutTime : CloakingArch->CloakInTime;
        double const Elapsed = (double)Dt + (double)CloakTimer;
        CloakTimer = (float)Elapsed;
        if (Elapsed > Total)
        {
            BaselineState = FadeTarget;
            CloakTimer = 0.0f;
        }
        if (Owner != nullptr)
            Owner->CloakPercent = (float)cloak_percent();
    }
    if (IsDestroyed() && Debris != nullptr)
        return true;
    return CExternalEquip::Update(Dt, Flags);
}

bool CECloakingDevice::Activate(bool bActive)
{
    // .text:06294770-06294852.
    //  - vt+4 IsActive() == bActive -> return bActive.
    //  - Result = CEquip::Activate(bActive) (direct); New = CEquip::bActive (+0x10).
    //  - FadeTarget == New -> return Result. FadeTarget == BaselineState -> FadeTarget = New,
    //    timer = 0, return Result.
    //  - Mid-fade: Frac = the inlined cloak_percent() fade branch, kept on the FPU stack;
    //    old FadeTarget != 0 -> timer = (1 - Frac) * CloakInTime (+0x8c), else
    //    timer = Frac * CloakOutTime (+0x90); one FSTP float; FadeTarget = New.
    if (IsActive() == bActive)
        return bActive;

    bool const Result = CEquip::Activate(bActive);
    unsigned char const NewFadeTarget = this->bActive; // CEquip::bActive, post-toggle
    unsigned char const OldFadeTarget = FadeTarget;
    if (OldFadeTarget == NewFadeTarget)
        return Result;
    if (OldFadeTarget == BaselineState)
    {
        FadeTarget = NewFadeTarget;
        CloakTimer = 0.0f;
        return Result;
    }
    Archetype::CloakingDevice const* CloakingArch = CloakArch();
    double const Frac = cloak_percent();
    if (OldFadeTarget != 0)
        CloakTimer = (float)((1.0 - Frac) * (double)CloakingArch->CloakInTime);
    else
        CloakTimer = (float)(Frac * (double)CloakingArch->CloakOutTime);
    FadeTarget = NewFadeTarget;
    return Result;
}

double CECloakingDevice::cloak_percent() const
{
    // .text:062945d0-06294653. Settled (FadeTarget == BaselineState): BaselineState ? 1.0f
    // : 0.0f. Fading in (FadeTarget != 0): CloakOutTime (+0x90) FCOMP the double 0.001
    // (.rdata:0639c9f0), TEST AH,0x41 / JNZ: not greater or unordered -> 1.0f, else
    // timer / CloakOutTime. Fading out: 0.0 or timer / CloakInTime (+0x8c) by the same test,
    // then FSUBR the double 1.0 (.rdata:06399410). The result is left unrounded in ST0.
    if (FadeTarget == BaselineState)
        return (BaselineState != 0) ? 1.0f : 0.0f;
    Archetype::CloakingDevice const* CloakingArch = CloakArch();
    if (FadeTarget != 0)
    {
        if (CloakingArch->CloakOutTime > 0.001)
            return (double)CloakTimer / (double)CloakingArch->CloakOutTime;
        return 1.0f;
    }
    double Frac = 0.0f;
    if (CloakingArch->CloakInTime > 0.001)
        Frac = (double)CloakTimer / (double)CloakingArch->CloakInTime;
    return 1.0 - Frac;
}

Archetype::CloakingDevice const* CECloakingDevice::CloakArch() const
{
    return (Archetype::CloakingDevice const*)(Arch);
}

CECloakingDevice* __cdecl CECloakingDevice::cast(CEquip* Equip)
{
    // Listing 0x34860: null-check, then Type == 0x1000.
    if (Equip == nullptr || Equip->GetType() != EquipTypeCloak)
        return nullptr;
    return (CECloakingDevice*)(Equip);
}

CECloakingDevice const* __cdecl CECloakingDevice::cast(CEquip const* Equip)
{
    return cast(const_cast<CEquip*>(Equip));
}
