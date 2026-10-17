// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CMine and CCounterMeasure.
#include "common_cobject.h"
#include "common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbCollisionGroup

#include "common_physys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbCollisionGroup;
using CmnCobject::RotateWorldToObjectSpace;

// ============================================================================
// CMine implementation
// ============================================================================

CMine::CMine(Class ObjectClass)
    : CProjectile(ObjectClass)
{
}

CMine::~CMine()
{
}

void CMine::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // .text:062aca30..062acb8x. Parms: PhySys default angular damping, Type 2, +0x1c = 0,
    // bEnableCollisions 1, Name = _itoa(OwnerId +0xe4, radix 24), +0x26 / +0x28..+0x30 = 0;
    // mass = archetype Mass (+0x20) and linear damping = float(LinearDrag (+0x80) / Mass)
    // -- no null test on the archetype, no zero test on the mass; system id from IObjDB.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = 2;
    Phy.bEnableCollisions = 1;
    _itoa((int)OwnerId, Phy.Name, 0x18);

    Archetype::Mine const* const Arch = minearch();
    Phy.Mass = Arch->Mass;
    Phy.LinearDamping = Arch->LinearDrag / Arch->Mass;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);
    PhySys::BuildIVP_Real(this, Phy);
    PhySys::AddToVelocity(this, Velocity);
    // Common.dll.c 0x4ca30: rotate world-space AngularVelocity into object space
    // via Orientation^T before adding it — see RotateWorldToObjectSpace above.
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

int CMine::update(float Dt, unsigned Flags)
{
    // Common.dll.c 0x4ca00 [ICF-folded with CMine::expire_safe_time].
    return CProjectile::update(Dt, Flags);
}

void CMine::expire_safe_time()
{
    // Common.dll.c 0x4ca10
    PhySys::SetCollisionGroup(this, "");
}

Archetype::Mine const* CMine::minearch()
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold). No null check in retail.
    return (Archetype::Mine const*)Archetype;
}

// ============================================================================
// CCounterMeasure implementation
// ============================================================================

CCounterMeasure::CCounterMeasure(Class ObjectClass)
    : CProjectile(ObjectClass)
{
}

CCounterMeasure::~CCounterMeasure()
{
}

void CCounterMeasure::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // .text:062a85a0..062a86fx: the CMine::init_physics body (062aca30) for the
    // countermeasure archetype -- default angular damping, Type 2, bEnableCollisions 1,
    // Name = _itoa(OwnerId +0xe4, radix 24), mass = archetype Mass, linear damping =
    // float(LinearDrag (+0x80) / Mass) with no null or zero test, system id from IObjDB.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = 2;
    Phy.bEnableCollisions = 1;
    _itoa((int)OwnerId, Phy.Name, 0x18);

    Archetype::CounterMeasure const* const Arch = countermeasure_arch();
    Phy.Mass = Arch->Mass;
    Phy.LinearDamping = Arch->LinearDrag / Arch->Mass;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);
    PhySys::BuildIVP_Real(this, Phy);
    PhySys::AddToVelocity(this, Velocity);
    // Common.dll.c 0x485a0: rotate world-space AngularVelocity into object space
    // via Orientation^T before adding it — see RotateWorldToObjectSpace above.
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

int CCounterMeasure::update(float Dt, unsigned Flags)
{
    return CProjectile::update(Dt, Flags);
}

void CCounterMeasure::expire_safe_time()
{
    // Common.dll.c 0x4ca10 [ICF-folded with CMine].
    PhySys::SetCollisionGroup(this, "");
}

Archetype::CounterMeasure const* CCounterMeasure::countermeasure_arch()
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold). No null check in retail.
    return (Archetype::CounterMeasure const*)Archetype;
}
