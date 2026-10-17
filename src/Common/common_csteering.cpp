// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CPhysControllerEquip, CSteering, CNudgeEngine, CStrafeEngine (retail CStrafeEngine.cpp) -- split out of common_cequip.cpp (CMN-B2).
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

// ============================================================
// CNudgeEngine — ship controller applying nudge vectors
// ============================================================
// (The "FEquipDesc"/"FEquipDescVector"/"FEquipDescStatic" types this equip batch
// previously duplicated here as a distinct equipment-descriptor family have been
// removed: every one of their real callers -- the CEquip::GetEquipDesc virtual family,
// CEShield's override, CEScanner's cargo-scan vector, and CEquipManager::VerifyListSync
// -- retail-confirmed (mangled `AAUEquipDesc@@` on every GetEquipDesc override, plus
// CECargo::GetEquipDesc's raw +0x18 byte copy landing on EquipDesc's own bMission field)
// to actually operate on plain `EquipDesc` (common_goods.h), the same type already used
// by EquipDescList/EquipDescVector/GoodInfo's AddonLow/Mid/High elsewhere in this port.
// (The retail engine-object wrapper this equip batch previously duplicated as
// CEngineObject is ported once, correctly, as EngineObject in
// src/Common/common_engineobject.cpp -- see the note in common_cequip.h.)

// ============================================================
// CPhysControllerEquip — base equipment physics controller
// ============================================================

// .text:06286e40: Controller(), then [this+0x10] = 0. Nothing past +0x10 -- the base ends there.
CPhysControllerEquip::CPhysControllerEquip()
    : bControllerEnabled(false)
{
}

// .text:06286e70: Controller's three dwords (+4/+8/+0xc), then the +0x10 byte.
CPhysControllerEquip::CPhysControllerEquip(CPhysControllerEquip const& Other)
    : PhySys::Controller(Other)
    , bControllerEnabled(Other.bControllerEnabled)
{
}

// .text:06287870 (one body for the CPhysControllerEquip, CSteering, CNudgeEngine and
// CStrafeEngine destructors): a direct DisableController call when the controller is
// registered, then ~Controller.
CPhysControllerEquip::~CPhysControllerEquip()
{
    if (bControllerEnabled)
    {
        CPhysControllerEquip::DisableController();
    }
}

CPhysControllerEquip& CPhysControllerEquip::operator=(CPhysControllerEquip const& Other)
{
    // Listing 0x06280ad0 (ICF-folded with PhyCollisionStateManager::operator=): the Controller
    // base dwords (+4/+8/+0xc) and the bControllerEnabled byte (+0x10), with no self test.
    PhySys::Controller::operator=(Other);
    bControllerEnabled = Other.bControllerEnabled;
    return *this;
}

bool CPhysControllerEquip::ControlObject(CObject* Object)
{
    // Ghidra @ 0x3b090
    if (!bControllerEnabled)
    {
        bControllerEnabled = true;
        PhySys::Controller::Register(this, Object);
        wakeup();
    }
    return bControllerEnabled;
}

bool CPhysControllerEquip::DisableController()
{
    // Ghidra @ 0x3b0c0
    if (bControllerEnabled)
    {
        bControllerEnabled = false;
        PhySys::Controller::UnRegister(this);
    }
    return bControllerEnabled;
}

bool CPhysControllerEquip::IsControllerEnabled() const
{
    // Listing 0x26e60 (ICF-shared with CEquip::IsActive): return [this+0x10].
    return bControllerEnabled;
}

// ============================================================
// CSteering — ship steering controller
// ============================================================

float const CSteering::AXIS_CHANGE_THRESHOLD = 1e-6f; // retail DAT_0639ecc8 = 0.000001, not 1e-12

// .text:062bb5e0: base (+0x10 = 0), then +0x14 OwnerShip, +0x18 bActive, +0x1c..+0x24 zeroed.
CSteering::CSteering()
    : OwnerShip(nullptr)
    , bActive(false)
{
    AxisThrottle.x = 0.0f;
    AxisThrottle.y = 0.0f;
    AxisThrottle.z = 0.0f;
}

// .text:06287810: base copy (+4..+0x10), then +0x14 dword, +0x18 byte, +0x1c Vector.
CSteering::CSteering(CSteering const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , bActive(Other.bActive)
    , AxisThrottle(Other.AxisThrottle)
{
}

CSteering::~CSteering()
{
}

CSteering& CSteering::operator=(CSteering const& Other)
{
    // .text:06287950 (ICF-folded with CNudgeEngine::operator=): base dwords +4..+0xc and the
    // +0x10 byte, then OwnerShip (+0x14), bActive (+0x18), AxisThrottle (+0x1c); no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    bActive = Other.bActive;
    AxisThrottle = Other.AxisThrottle;
    return *this;
}

bool CSteering::EnableController()
{
    // Listing 0x4d330 (one body shared by CNudgeEngine/CSteering/CStrafeEngine::EnableController;
    // CPhysControllerEquip's own slot is _purecall): an unconditional virtual call to
    // this->vtable[0x28](OwnerShip), i.e. ControlObject(OwnerShip) -- no bControllerEnabled or
    // OwnerShip-null guard here (ControlObject is itself the idempotency check).
    return ControlObject(OwnerShip);
}

bool CSteering::Activate(bool bOn)
{
    // Ghidra @ 0x3bd0
    bActive = bOn;
    return bActive;
}

bool CSteering::IsActive() const
{
    // .text:062877f0 (one body for CSteering/CNudgeEngine::IsActive): the virtual
    // IsControllerEnabled (vtable +0x24), then bActive.
    return IsControllerEnabled() && bActive;
}

double CSteering::GetAxisSteeringTorque(unsigned Axis, float Factor) const
{
    // .text:062bb610-062bb699: the virtual IsControllerEnabled and bActive gate the result
    // (a local 0.0f otherwise); OwnerShip and its archetype (+0x88) are dereferenced without
    // a test. Axis 0/1/2 multiplies Factor by SteeringTorqueX/Y/Z; any other axis returns that
    // local 0.0f times Factor (062bb686). The product stays unrounded in ST0.
    float const Zero = 0.0f;
    if (!IsControllerEnabled() || !bActive)
        return Zero;
    Archetype::Ship const* ShipArch = OwnerShip->shiparch();
    if (Axis == 0)
        return (double)ShipArch->SteeringTorqueX * Factor;
    if (Axis == 1)
        return (double)ShipArch->SteeringTorqueY * Factor;
    if (Axis == 2)
        return (double)ShipArch->SteeringTorqueZ * Factor;
    return (double)Zero * Factor;
}

Vector const& CSteering::GetAxisThrottle() const
{
    return AxisThrottle;
}

void CSteering::SetAxisThrottle(Vector const& Throttle)
{
    // .text:062bb6a0-062bb7b2. Each axis (z, y, x) is clamped: FCOM -1.0 / TEST AH,5 / JP
    // keeps >= -1 or unordered, else -1; FCOM 1.0 / TEST AH,0x41 / JNZ keeps <= 1 or
    // unordered, else 1. A NaN passes through.
    float const ClampedZ = Throttle.z < -1.0f ? -1.0f : (Throttle.z > 1.0f ? 1.0f : Throttle.z);
    float const ClampedY = Throttle.y < -1.0f ? -1.0f : (Throttle.y > 1.0f ? 1.0f : Throttle.y);
    float const ClampedX = Throttle.x < -1.0f ? -1.0f : (Throttle.x > 1.0f ? 1.0f : Throttle.x);

    // 062bb743-062bb74f: the threshold is squared and spilled as a float.
    float const ThresholdSq = AXIS_CHANGE_THRESHOLD * AXIS_CHANGE_THRESHOLD;

    // 062bb753-062bb78d: per axis x, y, z the squared difference (kept on the FPU) is FCOMPed
    // with ThresholdSq; TEST AH,0x41 + JP takes "greater or unordered" straight to the update,
    // and only when all three are ordered <= does the function return unchanged.
    double const Dx = (double)AxisThrottle.x - ClampedX;
    double const Dy = (double)AxisThrottle.y - ClampedY;
    double const Dz = (double)AxisThrottle.z - ClampedZ;
    if (Dx * Dx <= ThresholdSq && Dy * Dy <= ThresholdSq && Dz * Dz <= ThresholdSq)
        return;

    AxisThrottle.x = ClampedX;
    AxisThrottle.y = ClampedY;
    AxisThrottle.z = ClampedZ;
    wakeup();
}

void CSteering::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CSteering::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062bb7c0-062bb8e7 (RET 0x8; CoreList is never read).
    //  - vtbl[0x24] (IsControllerEnabled) false -> return; bActive (+0x18) clear -> return.
    //  - (x*x + y*y) + z*z of AxisThrottle (+0x1c) compared with 0.0 (TEST AH,0x44; JNP):
    //    exactly zero -> return.
    //  - Math (lazily acquired, DAT_063fc490) slot 0x34 InverseRotate(Local, OwnerShip+0x08 as
    //    the transform, AxisThrottle).
    //  - Scaled = Local times Archetype::Ship::SteeringTorque X/Y/Z (+0x114/+0x118/+0x11c),
    //    component by component.
    //  - Math slot 0x30 Rotate(Torque, OwnerShip+0x08, Scaled); rotate(Torque, dt).
    (void)CoreList;
    if (!IsControllerEnabled())
        return;
    if (!bActive)
        return;
    // 062bb7df-062bb80b: summed on the FPU, (x*x + y*y) + z*z; a NaN does not return.
    double const LenSq = ((double)AxisThrottle.x * AxisThrottle.x + (double)AxisThrottle.y * AxisThrottle.y) +
                         (double)AxisThrottle.z * AxisThrottle.z;
    if (LenSq == 0.0)
        return;

    FTransform const& ShipXform = (FTransform const&)(OwnerShip->get_transform());
    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector Local;
    Math->InverseRotate((FVector&)(Local), ShipXform,
                        (FVector const&)(AxisThrottle)); // +0x34

    Archetype::Ship const* ShipArch = OwnerShip->shiparch();
    Vector Scaled;
    Scaled.x = Local.x * ShipArch->SteeringTorqueX;
    Scaled.y = Local.y * ShipArch->SteeringTorqueY;
    Scaled.z = Local.z * ShipArch->SteeringTorqueZ;

    Math = Acquire3DMathEngine();
    Vector Torque;
    Math->Rotate((FVector&)(Torque), ShipXform,
                 (FVector const&)(Scaled)); // +0x30
    rotate(Torque, EventSim->delta_time);
}

// ============================================================
// CNudgeEngine — lightweight controller that applies a nudge vector
// ============================================================

float const CNudgeEngine::NUDGE_CHANGE_THRESHOLD = 9.9999997e-05f; // retail DAT_0639e518 = ~1e-4, not 1e-8

// .text:062ad220: same shape as CSteering's ctor -- OwnerShip (+0x14) is this class's own.
CNudgeEngine::CNudgeEngine()
    : OwnerShip(nullptr)
    , bActive(false)
{
    CurrentNudge.x = 0.0f;
    CurrentNudge.y = 0.0f;
    CurrentNudge.z = 0.0f;
}

// .text:062878f0: base copy, then +0x14 OwnerShip, +0x18 bActive, +0x1c CurrentNudge.
CNudgeEngine::CNudgeEngine(CNudgeEngine const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , bActive(Other.bActive)
    , CurrentNudge(Other.CurrentNudge)
{
}

CNudgeEngine::~CNudgeEngine()
{
}

CNudgeEngine& CNudgeEngine::operator=(CNudgeEngine const& Other)
{
    // .text:06287950 (ICF-folded with CSteering::operator=): the base part stops at +0x10
    // (CPhysControllerEquip::operator=, .text:06280ad0), then OwnerShip (+0x14), bActive
    // (+0x18) and CurrentNudge (+0x1c); no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    bActive = Other.bActive;
    CurrentNudge = Other.CurrentNudge;
    return *this;
}

bool CNudgeEngine::EnableController()
{
    // Listing 0x4d330 (one body shared by CNudgeEngine/CSteering/CStrafeEngine::EnableController;
    // CPhysControllerEquip's own slot is _purecall): an unconditional virtual call to
    // this->vtable[0x28](OwnerShip), i.e. ControlObject(OwnerShip) -- no bControllerEnabled or
    // OwnerShip-null guard here (ControlObject is itself the idempotency check).
    return ControlObject(OwnerShip);
}

bool CNudgeEngine::Activate(bool bOn)
{
    bActive = bOn;
    return bActive;
}

bool CNudgeEngine::IsActive() const
{
    // .text:062877f0: the virtual IsControllerEnabled (vtable +0x24), then bActive.
    return IsControllerEnabled() && bActive;
}

Vector CNudgeEngine::GetNudgeVec() const
{
    return CurrentNudge;
}

void CNudgeEngine::SetNudgeVec(Vector const& Vec)
{
    // .text:062ad250-062ad326. The threshold is squared and spilled as a float (062ad251-
    // 062ad266). Per axis x, y, z the squared difference stays on the FPU and is FCOMPed with
    // it; TEST AH,0x41 + JP takes "greater or unordered" to the update, so the call returns
    // unchanged only when all three are ordered <=.
    float const ThresholdSq = NUDGE_CHANGE_THRESHOLD * NUDGE_CHANGE_THRESHOLD;
    double const DiffX = (double)CurrentNudge.x - Vec.x;
    double const DiffY = (double)CurrentNudge.y - Vec.y;
    double const DiffZ = (double)CurrentNudge.z - Vec.z;
    if (DiffX * DiffX <= ThresholdSq && DiffY * DiffY <= ThresholdSq && DiffZ * DiffZ <= ThresholdSq)
        return;

    // wakeup() comes before CurrentNudge is written, as in retail.
    wakeup();

    // 062ad2b2-062ad2dd: length on the FPU, summed (z*z + y*y) + x*x, FSQRT, then FCOM 1.0
    // (double) / TEST AH,0x41 / JNZ: <= 1 or unordered copies Vec unchanged; only an ordered
    // > 1 normalises, by the unrounded reciprocal (FDIVR 1.0), each product rounded once.
    double const Len = sqrt(((double)Vec.z * Vec.z + (double)Vec.y * Vec.y) + (double)Vec.x * Vec.x);
    CurrentNudge = Vec;
    if (Len > 1.0)
    {
        double const Inv = 1.0 / Len;
        CurrentNudge.x = (float)(Inv * CurrentNudge.x);
        CurrentNudge.y = (float)(Inv * CurrentNudge.y);
        CurrentNudge.z = (float)(Inv * CurrentNudge.z);
    }
}

void CNudgeEngine::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CNudgeEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062ad340-062ad418 (RET 0x8; CoreList is never read).
    //  - vtbl[0x24] (IsControllerEnabled) false -> return; bActive (+0x18) clear -> return.
    //  - (x*x + y*y) + z*z of CurrentNudge (+0x1c) compared with 0.0 (FCOMP 0.0; TEST AH,0x44;
    //    JNP): exactly zero -> return. The gate is 0.0, not NUDGE_CHANGE_THRESHOLD.
    //  - Math (lazily acquired, DAT_063fc324) slot 0x30 Rotate(World, OwnerShip+0x08 as the
    //    transform, CurrentNudge).
    //  - Force = World times Archetype::Ship::NudgeForce ((OwnerShip+0x88)+0x120), then
    //    push(Force, dt).
    (void)CoreList;
    if (!IsControllerEnabled())
        return;
    if (!bActive)
        return;
    // 062ad35f-062ad38b: summed on the FPU, (x*x + y*y) + z*z; a NaN does not return.
    double const LenSq = ((double)CurrentNudge.x * CurrentNudge.x + (double)CurrentNudge.y * CurrentNudge.y) +
                         (double)CurrentNudge.z * CurrentNudge.z;
    if (LenSq == 0.0)
        return;

    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector World;
    Math->Rotate((FVector&)(World),
                 (FTransform const&)(OwnerShip->get_transform()),
                 (FVector const&)(CurrentNudge)); // +0x30

    float const Scale = OwnerShip->shiparch()->NudgeForce;
    Vector Force;
    Force.x = World.x * Scale;
    Force.y = World.y * Scale;
    Force.z = World.z * Scale;
    push(Force, EventSim->delta_time);
}

// ============================================================
// CStrafeEngine — strafe thruster controller
// ============================================================

// .text:062bb920: base (+0x10 = 0), then +0x14 OwnerShip and +0x18 CurrentStrafe zeroed.
CStrafeEngine::CStrafeEngine()
    : OwnerShip(nullptr)
    , CurrentStrafe(StrafeNone)
{
}

// .text:06287a00: base copy, then +0x14 OwnerShip and +0x18 CurrentStrafe.
CStrafeEngine::CStrafeEngine(CStrafeEngine const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , CurrentStrafe(Other.CurrentStrafe)
{
}

CStrafeEngine::~CStrafeEngine()
{
}

CStrafeEngine& CStrafeEngine::operator=(CStrafeEngine const& Other)
{
    // .text:06287a50: the base part stops at +0x10, then OwnerShip (+0x14) and CurrentStrafe
    // (+0x18); no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    CurrentStrafe = Other.CurrentStrafe;
    return *this;
}

bool CStrafeEngine::EnableController()
{
    // Listing 0x4d330 (one body shared by CNudgeEngine/CSteering/CStrafeEngine::EnableController;
    // CPhysControllerEquip's own slot is _purecall): an unconditional virtual call to
    // this->vtable[0x28](OwnerShip), i.e. ControlObject(OwnerShip) -- no bControllerEnabled or
    // OwnerShip-null guard here (ControlObject is itself the idempotency check).
    return ControlObject(OwnerShip);
}

StrafeDir CStrafeEngine::GetStrafe() const
{
    return CurrentStrafe;
}

void CStrafeEngine::SetStrafe(StrafeDir Dir)
{
    // Ghidra @ 0x5b940
    if (Dir != CurrentStrafe)
    {
        CurrentStrafe = Dir;
        if (Dir != StrafeNone)
        {
            wakeup();
        }
    }
}

void CStrafeEngine::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CStrafeEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062bb960-062bbadd (RET 0x8; CoreList is never read).
    //  - CurrentStrafe (+0x18) == 0 -> return.
    //  - Cost = Archetype::Ship::StrafePowerUsage (+0x128) * dt against OwnerShip->ThrustPower
    //    (FCOMP; TEST AH,0x41; JP): above the pool (or unordered) -> CurrentStrafe = 0, return
    //    (062bbacd). Else ThrustPower -= Cost.
    //  - Jump table on CurrentStrafe - 1 (062bbae0), the ship orientation at OwnerShip+0x08:
    //      1 Left:  -(column 0), built through the Vector(x,y,z) ctor FUN_0628b030;
    //      2 Right: +(column 0);  3 Up: +(column 1);  4 Down: -(column 1).
    //    Any other value: FDUMP(0x100001, "%s(%d) : *** ERROR: %s", CStrafeEngine.cpp, 0x4d,
    //    "No strafe direction!") and return, after the power has been drained.
    //  - push(Dir * Archetype::Ship::StrafeForce (+0x124), dt).
    (void)CoreList;
    if (CurrentStrafe == StrafeNone)
        return;

    // 062bb97b-062bb99e: Cost stays unrounded on the FPU (FLD arch+0x128 / FMUL [dt]); FCOMP
    // Cost vs pool, TEST AH,0x41 / JP: taken on 0 (Cost above) or 0x41 (unordered); the
    // FSUB result is rounded once by the FSTP float.
    double const Cost = (double)OwnerShip->shiparch()->StrafePowerUsage * EventSim->delta_time;
    double const Pool = OwnerShip->ThrustPower;
    if (!(Cost <= Pool))
    {
        CurrentStrafe = StrafeNone;     // 062bbacf
        return;
    }
    OwnerShip->ThrustPower = (float)(Pool - Cost);

    Matrix const& Ori = OwnerShip->Orientation;
    Vector Dir;
    switch (CurrentStrafe)
    {
    case StrafeLeft:
        Dir.x = -Ori.E[0][0];
        Dir.y = -Ori.E[1][0];
        Dir.z = -Ori.E[2][0];
        break;
    case StrafeRight:
        Dir.x = Ori.E[0][0];
        Dir.y = Ori.E[1][0];
        Dir.z = Ori.E[2][0];
        break;
    case StrafeUp:
        Dir.x = Ori.E[0][1];
        Dir.y = Ori.E[1][1];
        Dir.z = Ori.E[2][1];
        break;
    case StrafeDown:
        Dir.x = -Ori.E[0][1];
        Dir.y = -Ori.E[1][1];
        Dir.z = -Ori.E[2][1];
        break;
    default:
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Common\\CStrafeEngine.cpp", 0x4d,
              "No strafe direction!");
        return;
    }

    float const StrafeForce = OwnerShip->shiparch()->StrafeForce;
    Vector Force;
    Force.x = Dir.x * StrafeForce;
    Force.y = Dir.y * StrafeForce;
    Force.z = Dir.z * StrafeForce;
    push(Force, EventSim->delta_time);
}
