// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll CounterMeasure wrapper -- retail .text:06ce6170-06ce64fb, plus the shared
// damage override .text:06cf4230 its vtable (.rdata:06d64fcc) puts in two slots.
// The class is declared in src/include/server_gameobject.h.
//
// Layout (retail offsets): Projectile/GameObject +0x00..+0x5f and nothing else; the
// spawner (StarSystem FUN_06d092c0) allocates 0x60.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::explode / find_object
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE, DamageList
#include "../include/common_id.h"      // ID_String::IsEmpty
#include "../include/common_physys.h"  // PhySys::FindSphereCollisions
#include "../include/DALib.h"          // DALib::Engine (CounterMeasure::create, IEngine +0x6c)

#include <cstdlib>   // rand

// .data:DAT_06d8d588 -- the file-static hit buffer divert_missiles hands to
// PhySys::FindSphereCollisions (.text:06ce630a pushes its address with a
// maximum of 0x1e) and then reads by index (.text:06ce6345).
static CObject* GCounterMeasureHits[0x1e];

// ---------------------------------------------------------------------------
// FUN_06ce6170 (.text:06ce6170, RET 0).
// ---------------------------------------------------------------------------
CounterMeasure::CounterMeasure()
{
    // .text:06ce6173 FUN_06cee360 (GameObject's constructor), then the three
    // CounterMeasure vtables (.text:06ce6178 / 06ce617e / 06ce6185). No field
    // of its own is written.
}

// LAB_06ce61f0 (.text:06ce61f0): `MOV EAX,0x8000000 / RET`.
unsigned int CounterMeasure::get_type_bits() const
{
    return 0x8000000u;
}

CCounterMeasure* CounterMeasure::ccountermeasure() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x17u) != 0x17u)
        return nullptr;
    return static_cast<CCounterMeasure*>(Obj);
}

// ---------------------------------------------------------------------------
// LAB_06ce6200 (.text:06ce6200, RET 4) -- vtable +0x164.
// ---------------------------------------------------------------------------
bool CounterMeasure::create(Archetype::Root* Arch)
{
    // .text:06ce6202-06ce6216: CObject::Alloc(0x17) into CBase::Object.
    Object = CObject::Alloc(CObject::ClassCounterMeasure);

    // .text:06ce6213-06ce6219: Archetype::Root::create_instance(Object) (import
    // .rdata:06d6418c).
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // .text:06ce621f-06ce6234: DALib::Engine vt+0x6c, UpdateInstance(Object+4, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    // .text:06ce6237-06ce6250: open(Arch) (vt+0x4c) on the (ObjectClass & 3) == 3
    // narrowing, not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & 3u) != 3u)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    // .text:06ce6253-06ce6258: CBase::advise(true).
    advise(true);

    // .text:06ce625e-06ce626e: get_index() (vt+0x1c) != -1.
    return get_index() != -1;
}

// .text:06d02cb0 `XOR AL,AL / RET 4` -- the CounterMeasure vtable's +0x168.
bool CounterMeasure::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// ---------------------------------------------------------------------------
// FUN_06ce6280 (.text:06ce6280, RET 4) -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool CounterMeasure::update(float Dt)
{
    // .text:06ce6286-06ce6294: GameObject's base update (FUN_06cee9e0); false is
    // returned as is.
    if (!GameObject::update(Dt))
        return false;

    // .text:06ce6298-06ce62b8: the CCounterMeasure's own update (vt+0x50) with
    // UPDATE_DEFAULT; the narrowing is not null-tested.
    CCounterMeasure* const Cm = ccountermeasure();
    Cm->update(Dt, CObject::UPDATE_DEFAULT);

    // .text:06ce62bb-06ce62d7: FCOMP 0.0f (.rdata:06d65188), TEST AH,1 -- a spent
    // lifetime (CProjectile+0xe8 below zero, or unordered) destroys it quietly:
    // destroy(0, 0) through vtable +0x158.
    if (!(Cm->Lifetime >= 0.0f))
        destroy(0, 0);

    // .text:06ce62dd-06ce62ee: `NEG AL / SBB EAX,EAX / INC EAX` -- true unless destroyed.
    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// FUN_06ce6300 (.text:06ce6300, RET 0).
// ---------------------------------------------------------------------------
void CounterMeasure::divert_missiles()
{
    // .text:06ce6305-06ce6332: FindSphereCollisions(the system id (FUN_06d06cd0),
    // get_position() (vt[0]), the archetype's Range (+0x84, read through the raw
    // CBase::Object), the file-static buffer, 0x1e).
    Archetype::CounterMeasure const* const Arch =
        static_cast<Archetype::CounterMeasure const*>(Object->get_archetype());
    int const Count = PhySys::FindSphereCollisions(OwnerSystem->SystemInfo->NicknameId,
                                                   get_position(), Arch->Range,
                                                   GCounterMeasureHits, 0x1e);

    // .text:06ce6335-06ce63dd: for i in [0, Count) -- JLE skips a non-positive count.
    for (int Index = 0; Index < Count; ++Index)
    {
        // .text:06ce6345-06ce6360: a non-null hit whose class passes (& 0xa7) == 0xa7
        // (a CGuided).
        CObject* const Hit = GCounterMeasureHits[Index];
        if (Hit == nullptr || (static_cast<unsigned>(Hit->ObjectClass) & 0xa7u) != 0xa7u)
            continue;

        // .text:06ce6362-06ce6382: not a missile of this countermeasure's own owner
        // (CProjectile+0xe4 on both; this side's narrowing is not null-tested).
        CCounterMeasure* const Cm = ccountermeasure();
        if (static_cast<CProjectile*>(Hit)->OwnerId == Cm->OwnerId)
            continue;

        // .text:06ce6384-06ce6397: the missile's server object, by its CSimple id
        // (+0xb0), through the system (FUN_06d0c840).
        GameObject* const Missile = OwnerSystem->find_object(static_cast<CSimple*>(Hit)->Id);
        if (Missile == nullptr)
            continue;

        // .text:06ce6399-06ce63b0: `rand() * 100` divided by 0x8000 with the
        // signed-division rounding fix-up (CDQ / AND EDX,0x7fff / ADD / SAR 0xf).
        int const Roll = (std::rand() * 100) / 0x8000;

        // .text:06ce63a2-06ce63bf: JG -- a roll above DiversionPctg (+0x88, the
        // archetype fetched again through the raw CBase::Object) keeps the target.
        Archetype::CounterMeasure const* const RollArch =
            static_cast<Archetype::CounterMeasure const*>(Object->get_archetype());
        if (Roll > RollArch->DiversionPctg)
            continue;

        // .text:06ce63c1-06ce63d3: the missile's IObjAffect (+4) vt+0x48,
        // set_target(this, SUBOBJ_ID_NONE, 0).
        static_cast<IObjAffect*>(Missile)->set_target(static_cast<IObjRW*>(this),
                                                      SUBOBJ_ID_NONE, 0);
    }
}

// ---------------------------------------------------------------------------
// LAB_06ce63f0 (.text:06ce63f0, RET 8) -- vtable +0x158.
// ---------------------------------------------------------------------------
bool CounterMeasure::destroy(int Kind, unsigned int ObjId)
{
    // .text:06ce6401: GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // .text:06ce6406-06ce648d: only a real kill blows up, and only with an explosion
    // archetype that has a radius and some effect (the double 0.0001 at .rdata:06d65190;
    // each test is FCOMP / TEST AH,0x41, i.e. strictly greater).
    if (Kind == 1)
    {
        // .text:06ce640f-06ce6427: the archetype through the raw CBase::Object.
        if (!Object->get_archetype()->ExplosionArch.IsEmpty())
        {
            // .text:06ce642d-06ce6445: fetched again for Archetype::GetExplosion.
            Archetype::Explosion* const Explosion =
                Archetype::GetExplosion(Object->get_archetype()->ExplosionArch);
            double const Epsilon = 0.0001;
            if (Explosion != nullptr &&
                static_cast<double>(Explosion->Radius) > Epsilon &&
                (static_cast<double>(Explosion->Impulse) > Epsilon ||
                 static_cast<double>(Explosion->HullDamage) > Epsilon ||
                 static_cast<double>(Explosion->EnergyDamage) > Epsilon))
            {
                // .text:06ce648f-06ce64ed: {own id (vt+0x20), the raw object's
                // CProjectile owner (+0xe4, no narrowing), cause 4 (stored twice),
                // position (vt[0]), explosion, no skipped collision group} ->
                // StarSystem::explode (FUN_06d0b260).
                ExplosionDamageEvent Blast;
                Blast.ExplodingObjId = get_id();
                Blast.InflictorId = static_cast<CProjectile*>(Object)->OwnerId;
                Blast.Cause = static_cast<DamageCause>(4);
                Blast.Position = get_position();
                Blast.Explosion = Explosion;
                Blast.SkipCollisionGroup = nullptr;
                OwnerSystem->explode(Blast);
            }
        }
    }

    // .text:06ce64f3: always true.
    return true;
}

// ---------------------------------------------------------------------------
// LAB_06cf4230 (.text:06cf4230, RET 8) -- vtable +0x17c AND +0x180 here.
// `MOV EAX,[ESP+8] / MOV byte [EAX+0xc],1`: any hit destroys it.
// ---------------------------------------------------------------------------
void CounterMeasure::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

void CounterMeasure::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}
