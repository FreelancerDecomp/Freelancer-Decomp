// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEThruster -- split out of common_cequip.cpp (CMN-B2).
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

CEThruster::CEThruster(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Thruster const* ArchObj,
                       bool bMissionFlag)
    : CAttachedEquip(EquipTypeThruster, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , CPhysControllerEquip()
{
    // .text:0629d120-0629d1ad: CAttachedEquip(0x400, ...), the CPhysControllerEquip base at +0x4c
    // (Controller(), [+0x5c] = 0), [+0x60] = 0, both tables stamped, then the non-virtual
    // CPhysControllerEquip::ControlObject([this+4] = Owner) through the import slot at
    // .text:0629d195 -- the thruster registers itself with its owner's physics on creation.
    // The pad bytes +0x61..+0x63 are not written.
    bCouldThrust = false;
    CPhysControllerEquip::ControlObject(Owner);
}

CEThruster::~CEThruster()
{
    // .text:0629d1b0-0629d234: CExternalEquip::Destroy(), then the base teardown the compiler
    // emits for us: ~CPhysControllerEquip (DisableController when [+0x5c] is set), ~Controller,
    // ~CAttachedEquip.
    CExternalEquip::Destroy();
}

float CEThruster::GetThrust() const
{
    // .text:0629d2a0-0629d2a9: MOV EAX,[this+0xc]; FLD [EAX+0x8c] -- no Arch test.
    return ThrusterArch()->MaxThrust;
}

float CEThruster::GetPowerDraw() const
{
    // .text:0629d2b0-0629d2b9: MOV EAX,[this+0xc]; FLD [EAX+0x88] -- no Arch test.
    return ThrusterArch()->PowerUsage;
}

bool CEThruster::CouldThrust() const
{
    // .text:0629d2c0: MOV AL,[this+0x60].
    return bCouldThrust;
}

void CEThruster::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:0629d2d0-0629d398 (RET 0x8; CoreList is never read). `this` in retail is the
    // CPhysControllerEquip subobject at +0x4c.
    //  - 0629d2df: bCouldThrust (+0x60) = 0, then CEquip vtbl[0xc] (IsFunctioning) false ->
    //    return.
    //  - Ship = Owner when its class matches 0x503, else null (dereferenced regardless).
    //  - Cost = Arch->PowerUsage (+0x88) * dt stays on the FPU stack (0629d30b/0629d315), so it
    //    is double here; FLD Ship->ThrustPower (+0x2a0); FLD ST1; FCOMP compares Cost with the
    //    pool; TEST AH,0x41; JP taken on 0 (Cost above the pool) or 0x41 (unordered) ->
    //    Notify(Event_0xF, 0) and return (0629d384).
    //  - Else (C0: below, C3: equal) FSUB ST0,ST1 / FSTP: ThrustPower = pool - unrounded Cost,
    //    rounded once (0629d328/0629d32a), and push(-(Owner's Z column), Arch->MaxThrust
    //    (+0x8c) * dt rounded to the float argument) -- the column is read from Owner
    //    (+0x10/+0x1c/+0x28), not from the cast Ship -- then bCouldThrust = 1.
    (void)CoreList;
    bCouldThrust = false;
    if (!IsFunctioning())
        return;

    CShip* Ship = nullptr;
    if (Owner != nullptr)
    {
        CObject* Candidate = Owner;
        if ((Candidate->ObjectClass & 0x503u) == 0x503u)
            Ship = (CShip*)(Owner);
    }

    Archetype::Thruster const* ThrustArch = ThrusterArch();
    double const Cost = (double)ThrustArch->PowerUsage * (double)EventSim->delta_time;
    double const Pool = Ship->ThrustPower;
    if (!(Cost <= Pool))
    {
        Notify(INotify::Event_0xF, nullptr);
        return;
    }
    Ship->ThrustPower = (float)(Pool - Cost);

    Vector Dir;
    Dir.x = -Owner->Orientation.E[0][2];
    Dir.y = -Owner->Orientation.E[1][2];
    Dir.z = -Owner->Orientation.E[2][2];
    push(Dir, ThrustArch->MaxThrust * EventSim->delta_time);
    bCouldThrust = true;
}

bool CEThruster::IsControllerEnabled() const
{
    // .text:0629d290: MOV AL,[this+0x5c] -- the CPhysControllerEquip base's flag.
    return bControllerEnabled;
}

bool CEThruster::Activate(bool bOn)
{
    // .text:0629d240-0629d26b: CEquip::Activate(bOn); on success PhySys::Controller::wakeup()
    // on the base at +0x4c and return true; on failure [+0x60] = 0 and return false.
    if (CEquip::Activate(bOn))
    {
        wakeup();
        return true;
    }
    bCouldThrust = false;
    return false;
}

bool CEThruster::EnableController()
{
    // .text:0629d270: ECX = this+0x4c, PUSH [this+4] (Owner), CALL [vtbl2+0x28] -- the virtual
    // ControlObject of the CPhysControllerEquip base; its result is returned.
    return ControlObject(Owner);
}

bool CEThruster::DisableController()
{
    // .text:0629d280: ADD ECX,0x4c; JMP CPhysControllerEquip::DisableController.
    return CPhysControllerEquip::DisableController();
}

CEThruster* __cdecl CEThruster::cast(CEquip* Equip)
{
    // Ghidra @ 0x3d3a0
    if (Equip == nullptr || Equip->GetType() != EquipTypeThruster)
        return nullptr;
    return (CEThruster*)(Equip);
}

CEThruster const* __cdecl CEThruster::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

Archetype::Thruster const* CEThruster::ThrusterArch() const
{
    return (Archetype::Thruster const*)(Arch);
}
