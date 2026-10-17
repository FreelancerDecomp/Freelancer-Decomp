// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAsteroid and CDynamicAsteroid (the asteroid objects; fields and cubes live in
// common_casteroid.cpp).
#include "common_cobject.h"

#include "common_physys.h"

#include <string.h>

// ============================================================================
// CAsteroid implementation
// ============================================================================

CAsteroid::CAsteroid()
    : CSimple(ClassAsteroid)
{
    // Common.dll.c 0x42840. Retail only zeroes AsteroidId/CubeId/CollisionGroupId/
    // OwnerField here (bMine/bInstantKill/OpaqueE4 stay uninitialized until init() runs,
    // which always immediately follows construction via the CObject factory) -- this port
    // zero-initializes everything up front instead of replicating that gap.
    memset(OpaqueE4, 0, sizeof(OpaqueE4));
    AsteroidId = 0;
    CubeId = 0;
    bMine = false;
    bInstantKill = false;
    CollisionGroupId = 0;
    OwnerField = nullptr;
}

CAsteroid::CAsteroid(CAsteroid const& Other)
    : CSimple(Other)
{
    // Common.dll.c 0x422f0.
    memcpy(OpaqueE4, Other.OpaqueE4, sizeof(OpaqueE4));
    AsteroidId = Other.AsteroidId;
    CubeId = Other.CubeId;
    bMine = Other.bMine;
    bInstantKill = Other.bInstantKill;
    CollisionGroupId = Other.CollisionGroupId;
    OwnerField = Other.OwnerField;
}

CAsteroid::~CAsteroid()
{
    // .text:062a2880-062a28d4: SetCollisionState(this, 0) here (062a28ae), then ~CSimple,
    // which makes the same call again (062b59b5).
    PhySys::SetCollisionState(this, false);
}

void CAsteroid::init(CreateParms const& Parms)
{
    // .text:062a28f0-062a292d: CSimple::init first, then +0x5c/+0x60/+0x64/+0x65/+0x68 into
    // +0xe8/+0xec/+0xf0/+0xf1/+0xf8.
    CSimple::init(Parms);
    AsteroidId = Parms.AsteroidId;
    CubeId = Parms.CubeId;
    bMine = Parms.bMine;
    bInstantKill = Parms.bInstantKill;
    OwnerField = Parms.OwnerField;
}

void CAsteroid::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // Common.dll.c 0x42930. Retail fills this CreateParms with engine-default damping
    // values first, then immediately overwrites every field below before calling
    // BuildIVP_Real -- only the final values matter, so the dead first pass is skipped
    // here. Velocity/AngularVelocity are accepted (virtual signature) but never applied:
    // retail doesn't call PhySys::AddToVelocity / AddToAngularVelocityOS for asteroids.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.Mass = 1000.0f;
    Phy.LinearDamping = 1000000.0f;
    Phy.AngularDamping[0] = 1000000.0f;
    Phy.AngularDamping[1] = 1000000.0f;
    Phy.AngularDamping[2] = 1000000.0f;
    Phy.CollisionGroupId = CollisionGroupId;
    Phy.Type = 2;
    Phy.bFlag1c = 1;
    Phy.bEnableCollisions = 1;
    // "Asteroid" is exactly 8 characters -- retail's sprintf() into this 8-byte field
    // overflows its NUL terminator into the immediately-following bHasInertia byte
    // (offsetof(CreateParms, bHasInertia) == 0x26 == 0x1e + sizeof(Name), no gap), which
    // is harmless there only because it's about to be set to 0 anyway below. Copying
    // exactly 8 raw bytes (no terminator) reproduces that exact byte content without
    // triggering undefined behavior from writing past the end of Name.
    memcpy(Phy.Name, "Asteroid", 8);
    Phy.bHasInertia = 0;
    Phy.Inertia.x = 0.0f;
    Phy.Inertia.y = 0.0f;
    Phy.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(this, Phy);
}

int CAsteroid::update(float Dt, unsigned Flags)
{
    // Common.dll.c 0x428e0. Trivial passthrough (COMDAT-folded with CDynamicAsteroid::update).
    return CObject::update(Dt, Flags);
}

Vector CAsteroid::get_velocity() const
{
    // Common.dll.c 0x1023d0. COMDAT-folded with CSolar::get_velocity: always zero.
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}

Archetype::Asteroid const* CAsteroid::asteroidarch() const
{
    // .text:06262d50 (ICF fold with the other per-class arch accessors): MOV EAX,[this+0x88];
    // RET -- no null check. Asteroid derives from Root at offset 0, so the downcast is the
    // same pointer.
    return (Archetype::Asteroid const*)Archetype;
}

unsigned long CAsteroid::get_asteroid_id()
{
    // Common.dll.c 0x422b0.
    return AsteroidId;
}

bool CAsteroid::is_mine()
{
    // Common.dll.c 0x422c0.
    return bMine;
}

bool CAsteroid::is_instant_kill()
{
    // Common.dll.c 0x422d0.
    return bInstantKill;
}

CmnAsteroid::CAsteroidField const* CAsteroid::get_owner_field()
{
    // Common.dll.c 0x422e0.
    return OwnerField;
}

void CAsteroid::set_system(unsigned int SystemId)
{
    // .text:0629d4d0: MOV EAX,[ESP+4] / MOV [this+0xf4],EAX / RET 4.
    CollisionGroupId = SystemId;
}

// ============================================================================
// CDynamicAsteroid implementation
// ============================================================================

CDynamicAsteroid::CDynamicAsteroid()
    : CObject(ClassDynamicAsteroid)
{
    // Common.dll.c 0x429f0. Adds no data members of its own -- nothing to initialize.
}

CDynamicAsteroid::CDynamicAsteroid(CDynamicAsteroid const& Other)
    : CObject(Other)
{
    // Common.dll.c 0x425f0. Adds no data members of its own -- CObject's own copy ctor
    // covers the entire field-by-field copy retail performs here.
}

CDynamicAsteroid::~CDynamicAsteroid()
{
    // Common.dll.c 0x42a10. Unlike CSimple::~CSimple, the ported CObject::~CObject does
    // NOT call PhySys::SetCollisionState, so (matching retail) it's done explicitly here.
    PhySys::SetCollisionState(this, false);
}

void CDynamicAsteroid::init(CreateParms const& Parms)
{
    // Common.dll.c 0x42a70. Retail forwards straight into the virtual init_physics slot
    // (vtable offset 0x80, the last entry in CDynamicAsteroid's own vtable) rather than
    // going through a CSimple-style init chain -- CDynamicAsteroid isn't a CSimple.
    init_physics(Parms.Velocity, Parms.AngularVelocity, Parms.CollisionGroupId);
}

void CDynamicAsteroid::init_physics(Vector const& Velocity, Vector const& AngularVelocity, unsigned CollisionGroupId)
{
    // Common.dll.c 0x42a90. Same dead-first-pass pattern as CAsteroid::init_physics above;
    // only the final values are used. Velocity/AngularVelocity are accepted but never applied.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.Mass = 1.0f;
    Phy.LinearDamping = 0.00001f;
    Phy.AngularDamping[0] = 0.0f;
    Phy.AngularDamping[1] = 0.0f;
    Phy.AngularDamping[2] = 0.0f;
    Phy.CollisionGroupId = CollisionGroupId;
    Phy.Type = 2;
    Phy.bFlag1c = 0;
    Phy.bEnableCollisions = 1;
    strcpy(Phy.Name, "DynAst");
    Phy.bHasInertia = 0;
    Phy.Inertia.x = 0.0f;
    Phy.Inertia.y = 0.0f;
    Phy.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(this, Phy);
}

int CDynamicAsteroid::update(float Dt, unsigned Flags)
{
    // Common.dll.txt 0639dc8c: vtable slot COMDAT-folded with CAsteroid::update.
    return CObject::update(Dt, Flags);
}

Archetype::DynamicAsteroid const* CDynamicAsteroid::dynamicAsteroidArch() const
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold).
    return (Archetype::DynamicAsteroid const*)Archetype;
}
