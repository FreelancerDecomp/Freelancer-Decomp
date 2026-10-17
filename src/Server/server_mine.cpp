// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll Mine wrapper -- retail .text:06cf3330-06cf3c6f plus the two damage overrides
// at .text:06cf4230 / 06cf4240 that its vtable (.rdata:06d6606c) shares with Guided.
// The class is declared in src/include/server_gameobject.h.
//
// Layout (retail offsets): Projectile/GameObject +0x00..+0x5f, PhySys::Controller +0x60
// (vtable .rdata:06d6622c), PhySys::CObjectCB +0x70 (vtable .rdata:06d65fd8), then the
// Mine's own fields +0x74..+0x9f. The spawner (StarSystem FUN_06d08ef0) allocates 0xa0.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::explode
#include "../include/common_misc.h"    // SUBOBJ_ID_*, DamageList
#include "../include/common_id.h"      // ID_String::IsEmpty
#include "../include/DALib.h"          // DALib::Engine (Mine::create, IEngine +0x6c)
#include "../IVP/ivp_physics/ivp_core.h"        // IVP_Core::Speed (+0xa4)
#include "../include/common_ivp_real_object.h" // IVP_Real_Object::core (+0x98)
#include "../include/common_ivp_controller.h"  // IVP_Event_Sim::delta_time

#include <cfloat>
#include <cmath>

// ---------------------------------------------------------------------------
// FUN_06cf3330 (.text:06cf3330, RET 0).
// ---------------------------------------------------------------------------
Mine::Mine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , bSeeking(false)
    , SeekTimer(FLT_MAX)
    , bTriggered(false)
    , TriggerObjId(0)
{
    // .text:06cf334f FUN_06cee360 (GameObject's constructor), the Projectile vtables
    // (inlined, .text:06cf3354-06cf3361), PhySys::Controller's constructor on +0x60
    // (.text:06cf3373), then:
    //   .text:06cf337d  +0x74 = 0          .text:06cf3389  +0x84 = 0
    //   .text:06cf3380  +0x78 = 0          .text:06cf338f  +0x88 = 0
    //   .text:06cf3383  byte +0x80 = 0     .text:06cf3395  +0x8c = 0
    //   .text:06cf33a1  byte +0x94 = 0     .text:06cf33a7  +0x98 = 0
    //   .text:06cf33ae  +0x90 = 0x7f7fffff (FLT_MAX)
    // +0x7c and +0x9c are not touched.
    Target.x = 0.0f;
    Target.y = 0.0f;
    Target.z = 0.0f;
}

// ---------------------------------------------------------------------------
// FUN_06cf3440 (.text:06cf3440), entered on the CBase sub-object (+0xc).
// ---------------------------------------------------------------------------
Mine::~Mine()
{
    // .text:06cf3484-06cf34a4: PhySys::DestroyPhantom(+0x74) -- no null test -- then
    // +0x74 = 0. PhySys::Controller's destructor (.text:06cf34ab) and GameObject's
    // (FUN_06cee4a0, .text:06cf34bb) follow as the base-class destructors.
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06cf3400 (.text:06cf3400): `MOV EAX,0x4000000 / RET`.
unsigned int Mine::get_type_bits() const
{
    return 0x4000000u;
}

CMine* Mine::cmine() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0xfu) != 0xfu)
        return nullptr;
    return static_cast<CMine*>(Obj);
}

// ---------------------------------------------------------------------------
// LAB_06cf34e0 (.text:06cf34e0, RET 4) -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Mine::create(Archetype::Root* Arch)
{
    // .text:06cf34e2-06cf34f6: CObject::Alloc(0xf) into CBase::Object.
    Object = CObject::Alloc(CObject::ClassMine);

    // .text:06cf34f3-06cf34f9: Archetype::Root::create_instance(Object) -- before open, so
    // the port's CSimple::open scaffold finds a valid instance and does nothing.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // .text:06cf34ff-06cf3514: DALib::Engine vt+0x6c, UpdateInstance(Object+4, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    // .text:06cf3517-06cf3530: open(Arch) (vt+0x4c) on the (ObjectClass & 3) == 3
    // narrowing, not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & 3u) != 3u)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    // .text:06cf3533-06cf3538: CBase::advise(true).
    advise(true);

    // .text:06cf353e-06cf354e: get_index() (vt+0x1c) != -1.
    return get_index() != -1;
}

// .text:06d02cb0 `XOR AL,AL / RET 4` -- the Mine vtable's +0x168.
bool Mine::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// ---------------------------------------------------------------------------
// FUN_06cf3560 (.text:06cf3560, RET 0).
// ---------------------------------------------------------------------------
void Mine::arm()
{
    // .text:06cf3566-06cf35cb: the archetype through the raw CBase::Object (no narrowing).
    // A seeking mine (SeekDist +0x88 > 0) registers its controller on the object and
    // uses SeekDist as the phantom radius; otherwise a mine with a DetonationDist (+0x84)
    // > 0 still takes SeekDist (+0x88, .text:06cf35c5) as the radius; neither -> return.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    float Radius;
    if (Arch->SeekDist > 0.0f)
    {
        Radius = Arch->SeekDist;
        PhySys::Controller::Register(static_cast<PhySys::Controller*>(this), Object); // .text:06cf359c
    }
    else
    {
        if (!(Arch->DetonationDist > 0.0f))
            return;
        Radius = Arch->SeekDist;
    }

    // .text:06cf35cf-06cf35de: FCOMP 0.0f, TEST AH,0x44 / JNP -- a radius of exactly zero
    // makes no phantom.
    if (Radius == 0.0f)
        return;

    // .text:06cf35e4-06cf3671: PhantomParms on the stack -- system id (FUN_06d06cd0, the
    // system's nickname id), +0x04 = 0, +0x05 = 1, empty name, this object's position,
    // identity orientation, listener = the CObjectCB sub-object (+0x70).
    PhySys::PhantomParms Parms;
    Parms.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Parms.Flag04 = 0;
    Parms.Flag05 = 1;
    Parms.Name[0] = '\0';
    Parms.Position = get_position();
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    // .text:06cf3676 CreatePhantom(Parms, Radius, +0x74); .text:06cf3683 AttachPhantom.
    PhySys::CreatePhantom(Parms, Radius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
}

// ---------------------------------------------------------------------------
// LAB_06cf36a0 (.text:06cf36a0, RET 4) -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Mine::update(float Dt)
{
    // .text:06cf36ac: GameObject's base update; false is returned as is.
    if (!GameObject::update(Dt))
        return false;

    // .text:06cf36bf-06cf36e1: the CMine's own update (vt+0x50) with UPDATE_DEFAULT; the
    // narrowing is not null-tested.
    CMine* const Cm = cmine();
    Cm->update(Dt, CObject::UPDATE_DEFAULT);

    // .text:06cf36e4-06cf36f5: FCOMP 0.0f, TEST AH,1 -- a spent lifetime (CProjectile+0xe8
    // below zero; C0 is also set for an unordered compare) destroys the mine quietly:
    // destroy(0, 0).
    if (!(Cm->Lifetime >= 0.0f))
    {
        destroy(0, 0);
    }
    else
    {
        // .text:06cf36fb-06cf3718: switch the phantom's collisions on if they are off.
        if (!PhySys::GetCollisionStatePhantom(Phantom))
            PhySys::SetCollisionStatePhantom(Phantom, true);

        if (IntruderCount != 0)
        {
            // .text:06cf3724-06cf3736: the owner is exempt only while the owner-safe time
            // runs.
            unsigned int Exempt = 0;
            if (Cm->is_owner_safe())
                Exempt = Cm->OwnerId;

            // .text:06cf3736-06cf375c: the closest intruder of mask 0x7ffdf8 (not Exempt);
            // its position lands in +0x84.
            CSimple* Closest = nullptr;
            PhySys::GetClosestPhantomIntruderPos(Phantom, 0x7ffdf8u, Exempt, Closest, Target);
            if (Closest != nullptr)
            {
                // .text:06cf3767-06cf3789: DetonationDist squared, stored as a float.
                Archetype::Mine const* const Arch =
                    static_cast<Archetype::Mine const*>(Object->get_archetype());
                float const DetonationSq = static_cast<float>(
                    static_cast<double>(Arch->DetonationDist) *
                    static_cast<double>(Arch->DetonationDist));

                // .text:06cf378d-06cf37be: (dz*dz + dx*dx) + dy*dy on the FPU against it;
                // TEST AH,5 / JP -- strictly inside detonates.
                Vector const& Pos = get_position();
                double const Dx = static_cast<double>(Target.x) - static_cast<double>(Pos.x);
                double const Dy = static_cast<double>(Target.y) - static_cast<double>(Pos.y);
                double const Dz = static_cast<double>(Target.z) - static_cast<double>(Pos.z);
                double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
                if (DistSq < static_cast<double>(DetonationSq))
                {
                    // .text:06cf37c0-06cf37cd: destroy(1, the intruder's id, CSimple+0xb0).
                    destroy(1, Closest->Id);
                }
                else
                {
                    // .text:06cf37cf-06cf3806: wake the controller up and seek; the timer
                    // sum stays on the FPU (FST) for the compare against 1.0f.
                    wakeup();
                    bSeeking = true;
                    double const NewTimer =
                        static_cast<double>(Dt) + static_cast<double>(SeekTimer);
                    SeekTimer = static_cast<float>(NewTimer);
                    if (NewTimer > 1.0)
                        SeekTimer = 0.0f;
                }
            }
            else
            {
                // .text:06cf3808-06cf3815: nobody close -- stop seeking.
                bSeeking = false;
                Target.z = 0.0f;
                Target.y = 0.0f;
                Target.x = 0.0f;
            }
        }
        else
        {
            // .text:06cf3819-06cf3845: empty phantom -- stop seeking; a touch latched by
            // added() detonates now: destroy(1, TriggerObjId).
            bSeeking = false;
            Target.z = 0.0f;
            Target.y = 0.0f;
            Target.x = 0.0f;
            if (bTriggered)
                destroy(1, TriggerObjId);
        }
    }

    // .text:06cf3853-06cf3865: true unless destroyed.
    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// LAB_06cf3870 (.text:06cf3870, RET 8) -- vtable +0x158.
// ---------------------------------------------------------------------------
bool Mine::destroy(int Kind, unsigned int ObjId)
{
    // .text:06cf3882: GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // .text:06cf3887-06cf390e: only a real kill blows up, and only with an explosion
    // archetype that has a radius and some effect (the double 0.0001 at .rdata:06d65190).
    if (Kind == 1)
    {
        Archetype::Root* const Arch = Object->get_archetype();
        if (!Arch->ExplosionArch.IsEmpty())
        {
            Archetype::Explosion* const Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
            double const Epsilon = 0.0001;
            if (Explosion != nullptr &&
                static_cast<double>(Explosion->Radius) > Epsilon &&
                (static_cast<double>(Explosion->Impulse) > Epsilon ||
                 static_cast<double>(Explosion->HullDamage) > Epsilon ||
                 static_cast<double>(Explosion->EnergyDamage) > Epsilon))
            {
                // .text:06cf3910-06cf397f: {own id, the CMine's owner (+0xe4, 0xf narrowing,
                // not null-tested), cause 7 (after a first store of 4), position, explosion,
                // no skipped collision group} -> StarSystem::explode (FUN_06d0b260).
                ExplosionDamageEvent Blast;
                Blast.ExplodingObjId = get_id();
                Blast.InflictorId = cmine()->OwnerId;
                Blast.Cause = static_cast<DamageCause>(7);
                Blast.Position = get_position();
                Blast.Explosion = Explosion;
                Blast.SkipCollisionGroup = nullptr;
                OwnerSystem->explode(Blast);
            }
        }
    }

    // .text:06cf3986: always true.
    return true;
}

// ---------------------------------------------------------------------------
// FUN_06cf3990 (.text:06cf3990, RET 4) -- CObjectCB slot 0 on the +0x70 sub-object.
// ---------------------------------------------------------------------------
void Mine::added(CObject* Obj)
{
    // .text:06cf3994-06cf39ae: SeekDist (+0x88) > 0.0f picks the seeking arm.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    if (Arch->SeekDist > 0.0f)
    {
        // .text:06cf39b0-06cf39d1: a CSimple root (GetRoot, (ObjectClass & 3) == 3) counts
        // as an intruder.
        CObject* const Root = GetRoot(Obj);
        if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
            IntruderCount = IntruderCount + 1;
        return;
    }

    // .text:06cf39d9-06cf39f5: a non-seeking mine latches the first CSimple root that
    // touches it ...
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr || (static_cast<unsigned>(Root->ObjectClass) & 3u) != 3u)
        return;
    CSimple* const Toucher = static_cast<CSimple*>(Root);

    // .text:06cf39f7-06cf3a38: ... unless it is the owner (CMine+0xe4, narrowing not
    // null-tested) while the owner-safe time still runs.
    CMine* const Cm = cmine();
    if (Toucher->Id == Cm->OwnerId && Cm->is_owner_safe())
        return;

    // .text:06cf3a3a-06cf3a44.
    bTriggered = true;
    TriggerObjId = Toucher->Id;
}

// ---------------------------------------------------------------------------
// FUN_06cf3a50 (.text:06cf3a50, RET 4) -- CObjectCB slot 1 on the +0x70 sub-object.
// ---------------------------------------------------------------------------
void Mine::deleted(CObject* Obj)
{
    // .text:06cf3a53-06cf3a8c: only a seeking mine counts; a CSimple root leaving takes
    // one off.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    if (!(Arch->SeekDist > 0.0f))
        return;
    CObject* const Root = GetRoot(Obj);
    if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
        IntruderCount = IntruderCount - 1;
}

// ---------------------------------------------------------------------------
// LAB_06cf3aa0 (.text:06cf3aa0, RET 8) -- PhySys::Controller slot 4 on +0x60.
// ---------------------------------------------------------------------------
void Mine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // .text:06cf3aa6-06cf3aac: nothing unless update found a target.
    if (!bSeeking)
        return;

    // .text:06cf3ab2-06cf3aeb: velocity (vt+4), position (vt[0]); the direction to the
    // target, each component stored as a float (z first).
    Vector const Velocity = get_velocity();
    Vector const& Pos = get_position();
    Vector Dir;
    Dir.z = static_cast<float>(static_cast<double>(Target.z) - static_cast<double>(Pos.z));
    Dir.y = static_cast<float>(static_cast<double>(Target.y) - static_cast<double>(Pos.y));
    Dir.x = static_cast<float>(static_cast<double>(Target.x) - static_cast<double>(Pos.x));

    // .text:06cf3af0-06cf3b3b: normalised by 1.0f / FSQRT((z*z + y*y) + x*x) -- no zero test.
    double const LengthSq = (static_cast<double>(Dir.z) * static_cast<double>(Dir.z) +
                             static_cast<double>(Dir.y) * static_cast<double>(Dir.y)) +
                            static_cast<double>(Dir.x) * static_cast<double>(Dir.x);
    double const Inverse = 1.0 / std::sqrt(LengthSq);
    Dir.x = static_cast<float>(static_cast<double>(Dir.x) * Inverse);
    Dir.y = static_cast<float>(static_cast<double>(Dir.y) * Inverse);
    Dir.z = static_cast<float>(static_cast<double>(Dir.z) * Inverse);

    // .text:06cf3b3d-06cf3b59: the speed already along it, (z*vz + vy*y) + vx*x, as a float.
    float const Speed = static_cast<float>(
        (static_cast<double>(Dir.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Velocity.y) * static_cast<double>(Dir.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Dir.x));

    // .text:06cf3b5d-06cf3b78: what is missing to TopSpeed (+0x90); stored as a float (FST)
    // but the unrounded difference is tested -- nothing to do unless positive.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    double const Missing = static_cast<double>(Arch->TopSpeed) - static_cast<double>(Speed);
    float Delta = static_cast<float>(Missing);
    if (!(Missing > 0.0))
        return;

    // .text:06cf3b7e-06cf3bad: capped at TopSpeed itself.
    if (!(Arch->TopSpeed > Delta))
        Delta = Arch->TopSpeed;

    // .text:06cf3bb2-06cf3bec: and, unless Acceleration (+0x8c) is exactly -1.0f
    // (.rdata:06d6572c; TEST AH,0x44 / JNP), at Acceleration * dt.
    float const Dt = EventSim->delta_time;
    if (!(Arch->Acceleration == -1.0f))
    {
        double const Cap = static_cast<double>(Arch->Acceleration) * static_cast<double>(Dt);
        if (static_cast<double>(Delta) > Cap)
            Delta = static_cast<float>(Cap);
    }

    // .text:06cf3bee-06cf3c23: the push, drag-compensated:
    //   K = (Delta + Speed) * ((LinearDrag +0x80 / Mass +0x20) * dt) + Delta
    double const DragDt = (static_cast<double>(Arch->LinearDrag) / static_cast<double>(Arch->Mass)) *
                          static_cast<double>(Dt);
    double const K = (static_cast<double>(Delta) + static_cast<double>(Speed)) * DragDt +
                     static_cast<double>(Delta);

    // .text:06cf3c03-06cf3c50: straight into the physics core's linear velocity
    // (CObject+0x54 body, +0x98 core, +0xa4 speed), no null tests.
    IVP_Real_Object* const Body = static_cast<IVP_Real_Object*>(Object->BodyPtr);
    Vector& CoreSpeed = Body->core->Speed;
    CoreSpeed.x = static_cast<float>(static_cast<double>(Dir.x) * K + static_cast<double>(CoreSpeed.x));
    CoreSpeed.y = static_cast<float>(static_cast<double>(Dir.y) * K + static_cast<double>(CoreSpeed.y));
    CoreSpeed.z = static_cast<float>(static_cast<double>(Dir.z) * K + static_cast<double>(CoreSpeed.z));
}

// ---------------------------------------------------------------------------
// LAB_06cf4230 (.text:06cf4230, RET 8) -- vtable +0x17c (Mine and Guided).
// Any sub-object hit destroys the projectile: byte +0xc of the DamageList = 1.
// ---------------------------------------------------------------------------
void Mine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

// ---------------------------------------------------------------------------
// LAB_06cf4240 (.text:06cf4240, RET 8) -- vtable +0x180 (Mine and Guided).
// ---------------------------------------------------------------------------
void Mine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    // .text:06cf4244-06cf4257: a seeker munition (Munition+0x90 != 0) always kills.
    if (Hit.Munition->Seeker != 0)
    {
        Damage.bDestroyed = true;
        return;
    }
    // .text:06cf425e-06cf4262: otherwise GameObject's body (JMP FUN_06ceeb40).
    GameObject::munition_hit(Hit, Damage);
}
