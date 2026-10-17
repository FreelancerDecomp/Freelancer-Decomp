// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEEngine -- split out of common_cequip.cpp (CMN-B2).
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

CEEngine::CEEngine(CShip* OwnerObj, unsigned short EquipId, Archetype::Engine const* ArchObj,
                   bool bMissionFlag)
    : CInternalEquip(EquipTypeEngine, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , CPhysControllerEquip()
{
    // .text:06295c8c-06295cd9: CInternalEquip(...), then the CPhysControllerEquip base at +0x20
    // (Controller() and [+0x30] = 0, .text:06295ca1-06295caa), then the empty nozzle vector at
    // +0x34 (Nozzles' own default ctor), +0x44, +0x4c, +0x50 and the +0x54 byte zeroed; +0x48 is
    // left alone (the port zeroes it; only the copy ctor 06295b53 ever touches it).
    LastWakeThrottle = 0.0f;
    memset(Opaque48, 0, sizeof(Opaque48));
    CruiseChargeRate = 0.0f;
    CruiseChargeTimeElapsed = 0.0f;
    bCruiseActive = 0;

    // Common.dll.c 57901-57937 (0x35c40 tail): discover exhaust-nozzle hardpoints
    // "HpEngine0<N>" for N=1..(Owner's Root archetype, cast to Archetype::Ship,
    // NumExhaustNozzles at +0xFC -- Owner->Archetype is CObject+0x88, confirmed against
    // common_archetype.h's already-documented Archetype::Ship::NumExhaustNozzles field,
    // exactly the retail offset 0x88+0xfc). For each hardpoint that resolves
    // (FindHardpoint_OS, object space), append an ExhaustNozzleInfo.
    //
    // The retail vtable call `(**(code**)(*param_1+0xf4))(local_a8)` (param_1 = the owning
    // CShip) resolves to CEqObj::inst_to_subobj_id: confirmed via the raw named vtable dump
    // at Common.dll.txt:558519 (`CShip::vftable'{for CEqObj}`, base 0x0639c02c) -- slot
    // offset 0xf4 from that base lands at 0x0639c120, which the listing (line 558586) names
    // `CEqObj::inst_to_subobj_id`. Virtual dispatch through OwnerObj (typed CEqObj* here,
    // CShip* in retail's raw signature) reaches the identical override at runtime.
    //
    // .text:06295ce9..06295d11: the scratch CacheString is cleared and the hardpoint handle set
    // to -1 once, before the loop; the owner's archetype is read with no null test.
    CacheString HpName;
    HpName.clear();
    long HpHandle = -1;
    HardpointInfo Info;
    Archetype::Ship* ShipArch = (Archetype::Ship*)(OwnerObj->Archetype);
    int const NumNozzles = ShipArch->NumExhaustNozzles;
    for (int Index = 1; Index <= NumNozzles; ++Index)
    {
        // 06295d17..06295d3d: vsprintf + StringAlloc (FUN_06295e00), then
        // FindHardpoint_OS(Owner(+4)->+4, name, &handle, &info).
        BuildNozzleHPName(Index, HpName);
        if (!FindHardpoint_OS(Owner->EngineInstanceId, HpName.Value, HpHandle, Info))
            continue;

        unsigned short SubObjId = OwnerObj->inst_to_subobj_id(HpHandle);

        // .text:06295d5b-06295dad: insert(end, 1, <scratch element>) through the vector
        // helper FUN_06296640 on this+0x34, then the new last element's fields are written
        // in place: SubObjId, Index, ParentInstance, Orient, Position.
        Nozzles.insert(Nozzles.end(), 1, ExhaustNozzleInfo());
        ExhaustNozzleInfo& NewRecord = Nozzles.back();
        NewRecord.SubObjId = SubObjId;
        NewRecord.Index = Index;
        NewRecord.ParentInstance = HpHandle;
        NewRecord.Offset.Rotation = Info.Orient;
        NewRecord.Offset.Translation = Info.Position;
    }

    // .text:06295dc5..06295de2: CEquip::Activate(true) called directly (not CEEngine::Activate),
    // then vtbl+0x4c EnableController on success, vtbl+0x50 DisableController otherwise.
    if (CEquip::Activate(true))
        EnableController();
    else
        DisableController();
}

// Common.dll.c 57743-57814 (0x35a40, ??0CEEngine@@QAE@ABV0@@Z). Base fields (CEquip/
// CInternalEquip's own +0x04..+0x1c) are handled by CInternalEquip's already-ported copy
// ctor (retail inlines that base copy directly into this function's body, then re-installs
// its own vtable) -- same shape already established by every sibling CE* copy ctor in this
// file (e.g. CEInternalFX::CEInternalFX(CEInternalFX const&) above).
//
// CPhysControllerEquip is a real second base (see the class comment).
//
// .text:06295aa0-06295ad5: the CPhysControllerEquip base at +0x20 is copied exactly as its own
// copy ctor does (+0x24/+0x28/+0x2c dwords, +0x30 byte), then retail's inlined VC6
// vector<ExhaustNozzleInfo> copy (exact-fit operator_new(count*0x3c) + per-element copy).
CEEngine::CEEngine(CEEngine const& Other)
    : CInternalEquip(Other)
    , CPhysControllerEquip(Other)
    , Nozzles(Other.Nozzles)
{
    LastWakeThrottle = Other.LastWakeThrottle;
    memcpy(Opaque48, Other.Opaque48, sizeof(Opaque48));
    CruiseChargeRate = Other.CruiseChargeRate;
    CruiseChargeTimeElapsed = Other.CruiseChargeTimeElapsed;
    bCruiseActive = Other.bCruiseActive;
}

CEEngine::~CEEngine()
{
    // .text:06295e40: CEquip::Destroy(), then the inlined ~vector<ExhaustNozzleInfo> (buffer
    // freed through FUN_06391d5a, pointers nulled) -- Nozzles' own destructor here. The rest of
    // retail's body is the base teardown the compiler emits here too: ~CPhysControllerEquip
    // (DisableController when [+0x30] is set, .text:06295e9e-06295eaa), ~Controller,
    // ~CInternalEquip.
    Destroy();
}

CEEngine* __cdecl CEEngine::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeEngine)
        return nullptr;
    return (CEEngine*)(Equip);
}

CEEngine const* __cdecl CEEngine::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

void __cdecl CEEngine::BuildNozzleHPName(int Index, CacheString& Out)
{
    // .text:06296600 -> FUN_06295e00 (.text:06295e00..06295e39): vsprintf("HpEngine0%d", Index)
    // into a 0x100-byte stack buffer, then StringAlloc(buffer, false) into Out.
    char Scratch[0x100];
    sprintf(Scratch, "HpEngine0%d", Index);
    Out.Value = StringAlloc(Scratch, false);
}

// Common.dll.c 57997-58026 (0x35ee0). Real vtable slots resolved from the raw
// ??_7CEEngine@@6BCInternalEquip@@@ dump (Common.dll.txt ~559683-559711): +0x10=IsDisabled
// (CEEngine doesn't override -- always CEquip::IsDisabled==false), +8=IsDestroyed (inherited),
// +0x54=IsControllerEnabled (CEEngine's own override). cVar2 (the nozzle/indestructible OR)
// is true if the nozzle vector is non-empty, else falls back to Arch->bIndestructible.
bool CEEngine::IsFunctioning() const
{
    bool bHasNozzleOrIndestructible;
    if (!Nozzles.empty())
    {
        bHasNozzleOrIndestructible = true;
    }
    else
    {
        Archetype::Engine* EngineArch = (Archetype::Engine*)(Arch);
        bHasNozzleOrIndestructible = EngineArch->bIndestructible;
    }
    return IsActive() && !IsDisabled() && !IsDestroyed() && IsControllerEnabled() &&
           bHasNozzleOrIndestructible;
}

// Common.dll.c 58030-58048 (0x35f60). vtbl+0x4c/+0x50 confirmed as EnableController/
// DisableController by both the raw vtable dump and CEEngine's real ctor tail (per this
// item's decomp-tracker notes on CEEngine::Activate).
bool CEEngine::Activate(bool bOn)
{
    if (!bOn)
    {
        // Force-disengage cruise on deactivation (Common.dll.c 58038-58039).
        EngageCruise(false, false);
    }
    if (CEquip::Activate(bOn))
    {
        EnableController();
        return true;
    }
    DisableController();
    return false;
}

// .text:062961a0: IsActive() through vtbl+4; when true, the virtual ControlObject(Owner) of the
// CPhysControllerEquip base (vtbl+0x28 of the table at +0x20, .text:062961b6), whose result is
// returned; when false, IsActive's false.
bool CEEngine::EnableController()
{
    if (!IsActive())
        return false;
    return ControlObject(Owner);
}

// .text:062961c0: ADD ECX,0x20; JMP CPhysControllerEquip::DisableController.
bool CEEngine::DisableController()
{
    return CPhysControllerEquip::DisableController();
}

namespace
{
    // The `Owner && (Owner->ObjectClass & 0x503) == 0x503 ? Owner : 0` cast retail repeats before
    // every CShip call in this file (.text:0629627b, 062962dd, 06296352, 062960b2). The result
    // is used without a further test.
    CShip* OwnerShipOrNull(CEqObj* Owner)
    {
        if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            return (CShip*)(Owner);
        return nullptr;
    }
}

bool CEEngine::Update(float Dt, unsigned Flags)
{
    // .text:06295fa0..06296113. vtbl[0xc] = IsFunctioning (slot 3); not functioning skips both
    // the timer and the wakeup block.
    if (IsFunctioning())
    {
        Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
        if (bCruiseActive)
        {
            // 06295fc3: Elapsed FCOMP CruiseChargeTime (+0x80), TEST AH,0x5 / JP: only below it.
            if (CruiseChargeTimeElapsed < EngineArch->CruiseChargeTime)
            {
                // 06295fd7..06295fe5: New = Dt + Elapsed, FST'd but kept on the FPU stack.
                float const Prev = CruiseChargeTimeElapsed;
                double const Next = (double)(Dt) + Prev;
                CruiseChargeTimeElapsed = (float)(Next);
                // 06295fe8: Prev FCOMP 0.0, TEST AH,0x5 / JP: not less (or unordered) -> 0629603d.
                if (Prev < 0.0f)
                {
                    // 06295ff9: New FCOMP 0.0, TEST AH,0x1 / JNZ: less or unordered -> no notify.
                    if (Next >= 0.0)
                    {
                        // 0629600a..06296025: 2, or 3 unless CruiseChargeTime > 0.
                        int Code = 2;
                        if (!(EngineArch->CruiseChargeTime > 0.0f))
                            Code = 3;
                        Notify(INotify::EngineCruiseStateChanged, &Code);
                    }
                }
                else
                {
                    // 0629603d..0629606e: Prev < ChargeTime (TEST AH,0x5 / JP), then New not
                    // below ChargeTime (TEST AH,0x1 / JNZ) -> 3.
                    if (Prev < EngineArch->CruiseChargeTime &&
                        Next >= (double)(EngineArch->CruiseChargeTime))
                    {
                        int Code = 3;
                        Notify(INotify::EngineCruiseStateChanged, &Code);
                    }
                }
            }
        }
        else
        {
            // 06296075: Elapsed FCOMP 0.0, TEST AH,0x5 / JP: only below 0. New = Dt + Elapsed;
            // FCOM 0.0, TEST AH,0x5 / JNP keeps it when below 0, else (also unordered) 0.0.
            if (CruiseChargeTimeElapsed < 0.0f)
            {
                double const Next = (double)(Dt) + CruiseChargeTimeElapsed;
                CruiseChargeTimeElapsed = (Next < 0.0) ? (float)(Next) : 0.0f;
            }
        }

        // 062960aa..06296101: unless Flags has CObject::NO_DYNAMICS, read the ship throttle
        // (+0x29c, no null test after the class cast); FCOMP the cached +0x44, TEST AH,0x44 /
        // JNP skips when equal; then FCOMP 0.0 the same way skips when zero; otherwise wake the
        // controller and cache the throttle.
        if ((CObject::NO_DYNAMICS & Flags) == 0)
        {
            float const Throttle = OwnerShipOrNull(Owner)->Throttle;
            if (Throttle != LastWakeThrottle && Throttle != 0.0f)
            {
                wakeup();
                LastWakeThrottle = Throttle;
            }
        }
    }
    return CEquip::Update(Dt, Flags);
}

bool CEEngine::EngageCruise(bool Engage, bool Instant)
{
    // .text:06296250..062963db. vtbl+4 IsActive false, or no state change -> current state.
    if (!IsActive() || (bool)(bCruiseActive) == Engage)
        return bCruiseActive != 0;
    bCruiseActive = Engage;

    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    if (Engage)
    {
        OwnerShipOrNull(Owner)->set_throttle(1.0f);                            // 06296299
        if (Instant)
        {
            // 062962a6..062962cb: Elapsed = CruiseChargeTime, notify 3, wakeup.
            CruiseChargeTimeElapsed = EngineArch->CruiseChargeTime;
            int Code = 3;
            Notify(INotify::EngineCruiseStateChanged, &Code);
            wakeup();
            return bCruiseActive != 0;
        }
        // 062962d7..06296317: rate = CRUISE_ACCEL_TIME * ThrustMultiplier(+0x70) /
        // (get_linear_drag() * CRUISING_SPEED), kept on the FPU stack, FSTP float into +0x4c.
        float const Thrust = EngineArch->ThrustMultiplier;
        double const Drag = (double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                            EngineEquipConsts::CRUISING_SPEED;
        CruiseChargeRate = (float)(
            (double)(EngineEquipConsts::CRUISE_ACCEL_TIME) * Thrust / Drag);
        // 06296319: Elapsed FCOMP 0.0, TEST AH,0x1 / JNZ: less or unordered -> wakeup only;
        // otherwise notify 2 (no charge-time test) and wakeup.
        if (CruiseChargeTimeElapsed >= 0.0f)
        {
            int Code = 2;
            Notify(INotify::EngineCruiseStateChanged, &Code);
        }
        wakeup();
        return bCruiseActive != 0;
    }

    OwnerShipOrNull(Owner)->set_throttle(1.0f);                                // 06296370
    int Code;
    if (Instant)
    {
        // 0629637d..06296391: -CRUISE_DISRUPT_TIME, code 1.
        CruiseChargeTimeElapsed = -EngineEquipConsts::CRUISE_DISRUPT_TIME;
        Code = 1;
    }
    else
    {
        // 06296394..062963b3: Elapsed FCOMP 0.0, TEST AH,0x41 / JNZ keeps it when not greater
        // (or unordered), else 0.0; code 0.
        if (CruiseChargeTimeElapsed > 0.0f)
            CruiseChargeTimeElapsed = 0.0f;
        Code = 0;
    }
    Notify(INotify::EngineCruiseStateChanged, &Code);                          // 062963c3
    CruiseChargeRate = 0.0f;                                                   // 062963c8
    wakeup();                                                                  // 062963d2
    return bCruiseActive != 0;
}

// Common.dll.c 58118-58160 (0x36120): walks the nozzle vector removing every element whose
// SubObjId equals SubObjId, calling Notify(Event_0xD, SubObjId) once per removed element.
// Retail's removal is the inlined VC6 vector::erase (copy every following element down one
// slot, end -= 1); it rescans from the same slot after a removal (a new element has shifted
// into it) rather than advancing.
void CEEngine::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    std::vector<ExhaustNozzleInfo>::iterator It = Nozzles.begin();
    while (It != Nozzles.end())
    {
        if (It->SubObjId == SubObjId)
        {
            It = Nozzles.erase(It);
            Notify(INotify::Event_0xD, (void*)(uintptr_t)SubObjId);
        }
        else
        {
            ++It;
        }
    }
}

// Listing 0x361e0: MOV EAX,[this+0xc]; FLD [EAX+0x74]; RET
float CEEngine::GetLinearDrag() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->LinearDrag;
}

// .text:062963e0..062963ec: FLD [EAX+0x74]; FADD [.data:EngineEquipConsts::CRUISE_DRAG]; RET --
// reads the real global (compiled default 3.0, INI-overridable), not a hardcoded literal. The
// sum stays in ST0 unrounded (CShip::get_linear_drag FADDs onto it), so it is returned as double.
double CEEngine::GetCruiseDrag() const
{
    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    return (double)(EngineArch->LinearDrag) + EngineEquipConsts::CRUISE_DRAG;
}

// .text:062961f0..06296210: FLD Throttle; FCOM 0.0, TEST AH,0x5 / JP skips the multiply on
// not-less (or unordered); below 0 FMUL [arch+0x88] (reverse_fraction); FMUL [arch+0x70]
// (max_force) always. Both products stay on the FPU stack (double) until the float return.
float CEEngine::GetThrust(float Throttle) const
{
    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    double Result = Throttle;
    if (Throttle < 0.0f)
        Result = Result * EngineArch->ReverseFraction;
    return (float)(Result * EngineArch->ThrustMultiplier);
}

// .text:06296220..0629624c: vtbl+0xc IsFunctioning(); if functioning fabs(param_1)*[arch+0x78],
// else return 0.0. The compiler pre-stages the fallback value: MOV dword ptr [ESP+0x4], 0 happens
// BEFORE the vcall, and the other path's FLD reads that same slot -- i.e. it returns the literal
// constant 0.0f, not the incoming Throttle (whose stack slot is [ESP+0xc], untouched).
float CEEngine::GetPowerDraw(float Throttle) const
{
    if (IsFunctioning())
    {
        Archetype::Equipment* EquipmentArch = Arch;
        Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
        float AbsThrottle = Throttle;
        if (AbsThrottle < 0.0f)
            AbsThrottle = -AbsThrottle;
        return AbsThrottle * EngineArch->PowerDrawFactor;
    }
    return 0.0f;
}

// .text:062963f0..0629641b: the ship cast, then get_linear_drag() on it with no null test,
// * CRUISING_SPEED * the argument on the FPU stack (rounded by the float return type).
float CEEngine::GetCruiseThrust(float LinearDrag) const
{
    return (float)((double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                              EngineEquipConsts::CRUISING_SPEED * LinearDrag);
}

// Listing 0x36420: FLD [arch+0x7c]; RET
float CEEngine::GetCruisePowerDraw() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->CruisePowerUsage;
}

bool CEEngine::IsCruiseEngaged() const
{
    // Common.dll.c 0x35a10
    return bCruiseActive != 0;
}

bool CEEngine::IsControllerEnabled() const
{
    // .text:062961d0: MOV AL,[this+0x30] -- the CPhysControllerEquip base's flag.
    return bControllerEnabled;
}

float CEEngine::GetCruiseChargeTimeElapsed() const
{
    // Common.dll.c 0x3ba30
    return CruiseChargeTimeElapsed;
}

// Common.dll.c 0x35a20: FLD [arch+0x80]; RET
float CEEngine::GetCruiseChargeTime() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->CruiseChargeTime;
}

void CEEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:06296430-062965f7 (RET 0x8; CoreList is never read). `this` in retail is the
    // CPhysControllerEquip subobject (EDI); the engine is EDI-0x20.
    //  - 06296439: CEEngine vtbl[0xc] (IsFunctioning) false -> return.
    //  - 0629644c-0629646a: Ship = Owner when (Owner->ObjectClass & 0x503) == 0x503, else null
    //    (no null test before the Throttle/Power reads below -- none in retail either).
    //  - bCruiseActive (+0x54) set:
    //      * CruiseChargeTimeElapsed (+0x50) < Arch->CruiseChargeTime (+0x80) (FCOMP; TEST AH,5;
    //        JP to the charged arm): Force = Throttle (Ship+0x29c), times ReverseFraction (+0x88)
    //        when below 0.0 (FCOM 0.0; JP skips on >=), times ThrustMultiplier (+0x70).
    //      * otherwise (062964ba): Ramp = 1.0 (stored over the argument slot); +0x4c += dt,
    //        and while it is below CRUISE_ACCEL_TIME Ramp = +0x4c / CRUISE_ACCEL_TIME;
    //        Force = Ship->get_linear_drag() * CRUISING_SPEED * Throttle * Ramp.
    //      Both arms take PowerRate = Arch->CruisePowerUsage (+0x7c).
    //  - not cruising (06296524): PowerRate = IsFunctioning() ? |Throttle| * PowerDrawFactor
    //    (+0x78) : 0.0; Force = Throttle (times ReverseFraction below 0.0) * ThrustMultiplier.
    //  - 06296585: NewPower = Ship->Power (+0x198) - PowerRate * dt; C0 set (below 0.0 or
    //    unordered) -> return without storing. Else store it; Force == 0.0 (TEST AH,0x44; JNP)
    //    -> return; else push(Owner's Z column (+0x10/+0x1c/+0x28), -(Force * dt)).
    // x87: every product below stays on the FPU stack (double) until the FSTP named next to it.
    (void)CoreList;
    if (!IsFunctioning())
        return;

    CShip* const Ship = OwnerShipOrNull(Owner);

    Archetype::Engine const* EngineArch = (Archetype::Engine const*)(Arch);
    float const Dt = EventSim->delta_time;
    float Force;
    double PowerRate;
    if (bCruiseActive)
    {
        if (CruiseChargeTimeElapsed < EngineArch->CruiseChargeTime)
        {
            // 0629648c..062964ae: Throttle (* Reverse below 0) * ThrustMult, one FSTP.
            double Thrust = Ship->Throttle;
            if (Ship->Throttle < 0.0f)
                Thrust = Thrust * EngineArch->ReverseFraction;
            Force = (float)(Thrust * EngineArch->ThrustMultiplier);
        }
        else
        {
            // 062964ba..062964e4: the sum is FST'd into +0x4c but compared and divided unrounded.
            float Ramp = 1.0f;
            double const Accel = (double)(CruiseChargeRate) + Dt;
            CruiseChargeRate = (float)(Accel);
            if (Accel < EngineEquipConsts::CRUISE_ACCEL_TIME)
                Ramp = (float)(Accel / EngineEquipConsts::CRUISE_ACCEL_TIME);
            // 062964e8..0629651b: the throttle is read before the call; one FSTP at the end.
            float const Throttle = Ship->Throttle;
            Force = (float)((double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                                       EngineEquipConsts::CRUISING_SPEED * Throttle * Ramp);
        }
        PowerRate = EngineArch->CruisePowerUsage;
    }
    else
    {
        // 06296524..06296581: Rate = IsFunctioning ? |Throttle| * PowerDrawFactor (not rounded)
        // : 0.0; Force = Throttle (* Reverse below 0) * ThrustMult, one FSTP.
        float const Throttle = Ship->Throttle;
        PowerRate = 0.0;
        if (IsFunctioning())
            PowerRate = (double)(fabsf(Throttle)) * EngineArch->PowerDrawFactor;
        double Thrust = Throttle;
        if (Throttle < 0.0f)
            Thrust = Thrust * EngineArch->ReverseFraction;
        Force = (float)(Thrust * EngineArch->ThrustMultiplier);
    }

    // 06296585..0629659f: Power - Rate * dt in double; FCOM 0.0, TEST AH,0x1 / JNZ returns
    // on less or unordered without storing; otherwise FSTP into Power.
    double const NewPower = (double)(Ship->Power) - PowerRate * Dt;
    if (NewPower < 0.0 || NewPower != NewPower)
        return;
    Ship->Power = (float)(NewPower);
    // 062965a5..062965b4: FCOMP 0.0, TEST AH,0x44 / JNP returns only when equal.
    if (Force == 0.0f)
        return;

    Vector Dir;
    Dir.x = Ship->Orientation.E[0][2];
    Dir.y = Ship->Orientation.E[1][2];
    Dir.z = Ship->Orientation.E[2][2];
    push(Dir, -(Force * Dt));
}

Archetype::Engine const* CEEngine::EngineArch() const
{
    return (Archetype::Engine const*)(Arch);
}
