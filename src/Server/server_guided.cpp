// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll Guided (missile) and Bullet (gun round) wrappers -- retail .text:06cf3ce0-06cf4ecf,
// minus the Vector helpers the compiler emitted out of line (FUN_06cf4960 .. FUN_06cf4a40,
// inlined here) and the two damage bodies Guided shares with Mine (.text:06cf4230 /
// 06cf4240, see server_mine.cpp). The classes are declared in src/include/server_gameobject.h.
//
// Guided layout (retail offsets): Munition/Projectile/GameObject +0x00..+0x5f,
// PhySys::Controller +0x60 (vtable .rdata:06d66700), PhySys::CObjectCB +0x70 (vtable
// .rdata:06d664a4), then +0x74..+0x7f. The spawner (StarSystem FUN_06d09640) allocates 0x80.
// Bullet has no field of its own (operator new(0x60) in FUN_06d099e0).

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"       // StarSystem::explode / munition_collision / find_object
#include "../include/server_createinfo.h"  // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/x86math.h"            // I3DMathEngine::TransformMatrix
#include "../include/common_misc.h"        // SUBOBJ_ID_ROOT, DamageList
#include "../include/common_id.h"          // ID_String::IsEmpty
#include "../include/common_csys.h"        // Matrix, Transform (get_transform)
#include "../include/DALib.h"              // DALib::Engine (create, IEngine +0x6c)
#include "../include/common_ivp_controller.h" // IVP_Event_Sim::delta_time

#include <cmath>
#include <cstdio>

// .data:DAT_06d8d7c4 -- Guided.cpp's own lazily fetched math engine (FUN_06cf4a90).
static I3DMathEngine* GGuidedMathEngine = nullptr;

// ---------------------------------------------------------------------------
// FUN_06cf4a90 (.text:06cf4a90, cdecl, hidden return slot): Matrix * Vector through
// I3DMathEngine slot 11, TransformMatrix (vt+0x2c, .text:06cf4ab8). The engine is fetched
// once (FUN_06d5ff70) and never null-tested; the result is copied out of a stack temporary.
// ---------------------------------------------------------------------------
static Vector Guided_TransformMatrix(Matrix const& M, Vector const& V)
{
    if (GGuidedMathEngine == nullptr)
        GGuidedMathEngine = Server_Acquire3DMathEngine();
    FVector Tmp;
    GGuidedMathEngine->TransformMatrix(Tmp, reinterpret_cast<FMatrix const&>(M),
                                       reinterpret_cast<FVector const&>(V));
    Vector Out;
    Out.x = Tmp.X;
    Out.y = Tmp.Y;
    Out.z = Tmp.Z;
    return Out;
}

// ===========================================================================
// Guided
// ===========================================================================

// ---------------------------------------------------------------------------
// FUN_06cf3ce0 (.text:06cf3ce0, RET 0).
// ---------------------------------------------------------------------------
Guided::Guided()
    : Phantom(nullptr)
    , bTriggered(false)
    , TriggerObjId(0)
{
    // .text:06cf3cff FUN_06cee360 (GameObject's constructor), the Munition vtables
    // (.text:06cf3d04-06cf3d11), PhySys::Controller's constructor on +0x60
    // (.text:06cf3d23), then .text:06cf3d33 +0x74 = 0, .text:06cf3d36 byte +0x78 = 0,
    // .text:06cf3d39 +0x7c = 0, and the Guided vtables.
}

// ---------------------------------------------------------------------------
// FUN_06cf3db0 (.text:06cf3db0), entered on the CBase sub-object (+0xc).
// ---------------------------------------------------------------------------
Guided::~Guided()
{
    // .text:06cf3dfe: `CALL [.rdata:06d66698]` -- the +0x15c entry of Guided's own table,
    // i.e. destroy_notify as seen from ~Guided, which is GameObject's (FUN_06cee670).
    GameObject::destroy_notify();

    // .text:06cf3e04-06cf3e1c: PhySys::DestroyPhantom(+0x74) -- no null test -- then
    // +0x74 = 0. PhySys::Controller's destructor (.text:06cf3e23) and GameObject's
    // (FUN_06cee4a0, .text:06cf3e33) follow as the base-class destructors.
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06cf3d70 (.text:06cf3d70): `MOV EAX,0x1000000 / RET`.
unsigned int Guided::get_type_bits() const
{
    return 0x1000000u;
}

CGuided* Guided::cguided() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0xa7u) != 0xa7u)
        return nullptr;
    return static_cast<CGuided*>(Obj);
}

// ---------------------------------------------------------------------------
// LAB_06cf3e50 (.text:06cf3e50, RET 4) -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Guided::create(Archetype::Root* Arch)
{
    // .text:06cf3e52-06cf3e69: CObject::Alloc(0xa7) into CBase::Object.
    Object = CObject::Alloc(CObject::ClassGuided);

    // .text:06cf3e66-06cf3e6c: Archetype::Root::create_instance(Object) -- before open.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // .text:06cf3e72-06cf3e87: DALib::Engine vt+0x6c, UpdateInstance(Object+4, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    // .text:06cf3e8a-06cf3ea6: open(Arch) (vt+0x4c) on the (ObjectClass & 0xa7) == 0xa7
    // narrowing, not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & 0xa7u) != 0xa7u)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    // .text:06cf3ea9-06cf3eae: CBase::advise(true).
    advise(true);

    // .text:06cf3eb4-06cf3ec4: get_index() (vt+0x1c) != -1.
    return get_index() != -1;
}

// ---------------------------------------------------------------------------
// LAB_06cf3ed0 (.text:06cf3ed0, RET 4) -- vtable +0x168.
// ---------------------------------------------------------------------------
bool Guided::find(unsigned int const& Id)
{
    // .text:06cf3ed5-06cf3eeb: CObject::Find(Id, 0xa7) into CBase::Object, then
    // CBase::advise(true); true when something was found.
    Object = CObject::Find(Id, CObject::ClassGuided);
    advise(true);
    return Object != nullptr;
}

// ---------------------------------------------------------------------------
// LAB_06cf4310 (.text:06cf4310, RET 0) -- vtable +0x1bc.
// ---------------------------------------------------------------------------
void Guided::arm()
{
    // .text:06cf4318-06cf432a: PhySys::Controller::Register(this ? this+0x60 : 0, Object).
    PhySys::Controller::Register(static_cast<PhySys::Controller*>(this), Object);

    // .text:06cf4330-06cf434f: nothing more without a detonation distance
    // (Munition+0xa4, squared; TEST AH,0x41 -- only strictly positive goes on).
    Archetype::Munition const* const Arch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    if (!(Arch->DetonationDistSq > 0.0f))
        return;

    // .text:06cf4355-06cf43c9: PhantomParms on the stack -- +0x00 = 0, +0x04/+0x05/+0x06
    // bytes = 0, position 0, identity orientation, listener 0.
    PhySys::PhantomParms Parms;
    Parms.SystemId = 0;
    Parms.Flag04 = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Position.x = 0.0f;
    Parms.Position.y = 0.0f;
    Parms.Position.z = 0.0f;
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = nullptr;

    // .text:06cf43cd-06cf43d6: the system id (FUN_06d06cd0, the system's nickname id).
    Parms.SystemId = OwnerSystem->SystemInfo->NicknameId;

    // .text:06cf43da-06cf43e6: sprintf(Name, PhySys::GetCollisionGroup(Object)) -- the
    // group name is the format string, exactly as retail passes it.
    std::sprintf(Parms.Name, PhySys::GetCollisionGroup(Object));

    // .text:06cf43ec-06cf4417: this object's position (vt[0]), the CObjectCB sub-object
    // (+0x70) as listener, +0x05 = 1.
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);
    Parms.Flag05 = 1;

    // .text:06cf441c-06cf442f: radius = FSQRT(DetonationDistSq) stored as a float;
    // CreatePhantom(Parms, radius, +0x74). .text:06cf443c AttachPhantom(Object, +0x74).
    float const Radius = static_cast<float>(std::sqrt(static_cast<double>(Arch->DetonationDistSq)));
    PhySys::CreatePhantom(Parms, Radius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
}

// ---------------------------------------------------------------------------
// LAB_06cf3f10 (.text:06cf3f10, RET 4) -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Guided::update(float Dt)
{
    // .text:06cf3f1c-06cf3f2a: GameObject's base update; false is returned as is.
    if (!GameObject::update(Dt))
        return false;

    // .text:06cf3f2d-06cf3f56: the CGuided's own update (vt+0x50) with UPDATE_DEFAULT; the
    // narrowing is not null-tested.
    CGuided* const Cg = cguided();
    Cg->update(Dt, CObject::UPDATE_DEFAULT);

    // .text:06cf3f59-06cf3f6a: FCOMP 0.0f, TEST AH,1 -- a spent lifetime (CProjectile+0xe8
    // below zero, or unordered) is a kill: destroy(1, 0) (.text:06cf41ff-06cf4208).
    if (!(Cg->Lifetime >= 0.0f))
    {
        destroy(1, 0);
        return !is_destroyed();
    }

    // .text:06cf3f70-06cf3fa4: the phantom's collisions go on first; once they are on,
    // a touch latched by added() detonates: destroy(1, TriggerObjId).
    if (!PhySys::GetCollisionStatePhantom(Phantom))
        PhySys::SetCollisionStatePhantom(Phantom, true);
    else if (bTriggered)
        destroy(1, TriggerObjId);

    // .text:06cf3faa-06cf3fad: PhySys::Controller::wakeup on +0x60.
    wakeup();

    // .text:06cf3fb3-06cf3fda: only a lock-on seeker (Munition+0x90 == 2; retail reads it
    // twice: zero, then minus two) steers.
    Archetype::Munition const* const Arch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    if (Arch->Seeker == 0)
        return !is_destroyed();
    if (Arch->Seeker != 2)
        return !is_destroyed();

    // .text:06cf3fe0-06cf400e: ... and only with a target (CGuided+0xf0, the watched
    // IObjRW+8; the narrowing is not null-tested).
    if (cguided()->get_target() == nullptr)
        return !is_destroyed();

    // .text:06cf4014-06cf40d8: the aim point, into [ESP+0xc]. The target is read again;
    // a sub-targeted CEqObj ((ObjectClass & 0x103) == 0x103, sub-target not
    // SUBOBJ_ID_ROOT) gives its sub-object's centre of mass (CEqObj vt+0xe0); otherwise
    // -- or when that call fails -- the target's centre of mass (IObjInspect vt+0x14).
    // Retail leaves the slot unwritten when the second read finds no target, which the
    // first read just ruled out; the port starts it at zero.
    Vector TargetPos;
    TargetPos.x = 0.0f;
    TargetPos.y = 0.0f;
    TargetPos.z = 0.0f;
    IObjRW* const PosTarget = cguided()->get_target();
    if (PosTarget != nullptr)
    {
        bool bHavePos = false;
        CObject const* const TargetObj = PosTarget->cobject();                     // vt+0x150
        if (TargetObj != nullptr &&
            (static_cast<unsigned>(TargetObj->ObjectClass) & 0x103u) == 0x103u)
        {
            unsigned short const SubTarget = cguided()->SubTarget;
            if (SUBOBJ_ID_ROOT != SubTarget)
            {
                bHavePos = static_cast<CEqObj const*>(TargetObj)->get_sub_obj_center_of_mass(
                    cguided()->SubTarget, TargetPos);
            }
        }
        if (!bHavePos)
            TargetPos = PosTarget->get_center_of_mass();
    }

    // .text:06cf40dc-06cf4102: CGuided::seeker_can_see(aim point).
    if (cguided()->seeker_can_see(TargetPos))
    {
        // .text:06cf4108-06cf41cf: the target's velocity, into [ESP+0x18] -- the same
        // shape: a sub-targeted CEqObj's sub-object velocity (CEqObj vt+0xdc), otherwise
        // IObjInspect::get_velocity (vt+4).
        Vector TargetVel;
        TargetVel.x = 0.0f;
        TargetVel.y = 0.0f;
        TargetVel.z = 0.0f;
        IObjRW* const VelTarget = cguided()->get_target();
        if (VelTarget != nullptr)
        {
            bool bHaveVel = false;
            CObject const* const TargetObj = VelTarget->cobject();
            if (TargetObj != nullptr &&
                (static_cast<unsigned>(TargetObj->ObjectClass) & 0x103u) == 0x103u)
            {
                unsigned short const SubTarget = cguided()->SubTarget;
                if (SUBOBJ_ID_ROOT != SubTarget)
                {
                    bHaveVel = static_cast<CEqObj const*>(TargetObj)->get_sub_obj_velocity(
                        cguided()->SubTarget, TargetVel);
                }
            }
            if (!bHaveVel)
                TargetVel = VelTarget->get_velocity();
        }

        // .text:06cf41d3-06cf41df: FUN_06cf45a0(aim point, velocity).
        seek(TargetPos, TargetVel);
    }
    else
    {
        // .text:06cf41e6-06cf41fa: lost it -- IObjAffect (+4) vt+0x48, this Guided's own
        // set_target(nullptr, SUBOBJ_ID_ROOT, 0).
        set_target(nullptr, SUBOBJ_ID_ROOT, 0);
    }

    // .text:06cf420e-06cf4220: true unless destroyed.
    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// FUN_06cf45a0 (.text:06cf45a0, RET 8). F below is ESP after `PUSH ESI`; every
// out-of-line Vector helper (FUN_06cf49e0 subtract, FUN_06cf4a10 scale, FUN_06cf4960
// length, FUN_06cf4990 negate, FUN_06cf49b0 add, FUN_06ce7c10 construct) stores floats,
// so each intermediate below is rounded to float where retail stores it.
// ---------------------------------------------------------------------------
void Guided::seek(Vector const& TargetPos, Vector const& TargetVel)
{
    // .text:06cf45a9-06cf45bb: F+0x20 = TargetPos - position (vt[0]), FUN_06cf49e0.
    Vector const& MyPos = get_position();
    Vector RelPos;
    RelPos.x = static_cast<float>(static_cast<double>(TargetPos.x) - static_cast<double>(MyPos.x));
    RelPos.y = static_cast<float>(static_cast<double>(TargetPos.y) - static_cast<double>(MyPos.y));
    RelPos.z = static_cast<float>(static_cast<double>(TargetPos.z) - static_cast<double>(MyPos.z));

    // .text:06cf45c0-06cf45dd: F+0x38 = TargetVel - velocity (vt+4), FUN_06cf49e0.
    Vector const MyVel = get_velocity();
    Vector RelVel;
    RelVel.x = static_cast<float>(static_cast<double>(TargetVel.x) - static_cast<double>(MyVel.x));
    RelVel.y = static_cast<float>(static_cast<double>(TargetVel.y) - static_cast<double>(MyVel.y));
    RelVel.z = static_cast<float>(static_cast<double>(TargetVel.z) - static_cast<double>(MyVel.z));

    // .text:06cf45e2-06cf460c: (vz*pz + vy*py) + vx*px against 0.0f, TEST AH,5 / JP --
    // the turn happens only while the target closes in (strictly negative).
    double const Closing =
        (static_cast<double>(RelVel.z) * static_cast<double>(RelPos.z) +
         static_cast<double>(RelVel.y) * static_cast<double>(RelPos.y)) +
        static_cast<double>(RelVel.x) * static_cast<double>(RelPos.x);
    if (Closing < 0.0)
    {
        // .text:06cf4612-06cf465a: F+0x44 = RelPos x RelVel (FUN_06ce7c10, z then y then x).
        Vector Cross;
        Cross.z = static_cast<float>(static_cast<double>(RelVel.y) * static_cast<double>(RelPos.x) -
                                     static_cast<double>(RelPos.y) * static_cast<double>(RelVel.x));
        Cross.y = static_cast<float>(static_cast<double>(RelPos.z) * static_cast<double>(RelVel.x) -
                                     static_cast<double>(RelVel.z) * static_cast<double>(RelPos.x));
        Cross.x = static_cast<float>(static_cast<double>(RelVel.z) * static_cast<double>(RelPos.y) -
                                     static_cast<double>(RelPos.z) * static_cast<double>(RelVel.y));

        // .text:06cf465f-06cf4686: |RelPos|^2 = (z*z + y*y) + x*x; FCOM 0.0001f
        // (.rdata:06d66968), TEST AH,1 -- below it (or unordered) no turn.
        double const DistSq =
            (static_cast<double>(RelPos.z) * static_cast<double>(RelPos.z) +
             static_cast<double>(RelPos.y) * static_cast<double>(RelPos.y)) +
            static_cast<double>(RelPos.x) * static_cast<double>(RelPos.x);
        if (DistSq >= static_cast<double>(0.0001f))
        {
            // .text:06cf468c-06cf46a1: F+0x68 = Cross * float(1.0 / |RelPos|^2)
            // (.rdata:06d64f00 double 1.0, FDIVR), FUN_06cf4a10 -- the line-of-sight rate.
            float const InvDistSq = static_cast<float>(1.0 / DistSq);
            Vector Omega;
            Omega.x = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.x));
            Omega.y = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.y));
            Omega.z = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.z));

            // .text:06cf46a6-06cf46bc: F+0x8 = Omega * 3.0f (.rdata:06d6649c), FUN_06cf4a10.
            Vector Desired;
            Desired.x = static_cast<float>(3.0 * static_cast<double>(Omega.x));
            Desired.y = static_cast<float>(3.0 * static_cast<double>(Omega.y));
            Desired.z = static_cast<float>(3.0 * static_cast<double>(Omega.z));

            // .text:06cf46c1-06cf46eb: |Desired| = FSQRT((y*y + z*z) + x*x), stored float.
            float const Magnitude = static_cast<float>(std::sqrt(
                (static_cast<double>(Desired.y) * static_cast<double>(Desired.y) +
                 static_cast<double>(Desired.z) * static_cast<double>(Desired.z)) +
                static_cast<double>(Desired.x) * static_cast<double>(Desired.x)));

            // .text:06cf46ef-06cf4700: against MaxAngularVelocity (Munition+0xa0),
            // TEST AH,0x41 -- only a strictly larger rate is clamped.
            Archetype::Munition const* const Arch =
                static_cast<Archetype::Munition const*>(Object->get_archetype());
            if (Magnitude > Arch->MaxAngularVelocity)
            {
                // .text:06cf4702-06cf4732: normalise -- FUN_06cf4960's (x*x + y*y) + z*z,
                // 1.0f / that length kept on the FPU, each component stored.
                double const Length = std::sqrt(
                    (static_cast<double>(Desired.x) * static_cast<double>(Desired.x) +
                     static_cast<double>(Desired.y) * static_cast<double>(Desired.y)) +
                    static_cast<double>(Desired.z) * static_cast<double>(Desired.z));
                double const Inverse = 1.0 / Length;
                Desired.x = static_cast<float>(static_cast<double>(Desired.x) * Inverse);
                Desired.y = static_cast<float>(static_cast<double>(Desired.y) * Inverse);
                Desired.z = static_cast<float>(static_cast<double>(Desired.z) * Inverse);

                // .text:06cf4734-06cf4769: times MaxAngularVelocity (FUN_06cf4a10 into
                // F+0x14, copied back to F+0x8).
                double const MaxRate = static_cast<double>(Arch->MaxAngularVelocity);
                Desired.x = static_cast<float>(MaxRate * static_cast<double>(Desired.x));
                Desired.y = static_cast<float>(MaxRate * static_cast<double>(Desired.y));
                Desired.z = static_cast<float>(MaxRate * static_cast<double>(Desired.z));
            }

            // .text:06cf476d-06cf4786: F+0x5c = -PhySys::GetAngularVelocityOS(Object)
            // (FUN_06cf4990).
            Vector const AngularVelocity = PhySys::GetAngularVelocityOS(Object);
            Vector NegAngular;
            NegAngular.x = -AngularVelocity.x;
            NegAngular.y = -AngularVelocity.y;
            NegAngular.z = -AngularVelocity.z;

            // .text:06cf478b-06cf47bb: F+0x74 = the transpose of the object's orientation
            // (CObject+0x08, FUN_06cf4a40 filled column by column).
            Matrix const& Orientation = Object->Orientation;
            Matrix const Transposed(Orientation.E[0][0], Orientation.E[1][0], Orientation.E[2][0],
                                    Orientation.E[0][1], Orientation.E[1][1], Orientation.E[2][1],
                                    Orientation.E[0][2], Orientation.E[1][2], Orientation.E[2][2]);

            // .text:06cf47c0-06cf47cf: F+0x14 = Transposed * Desired (FUN_06cf4a90) -- the
            // wanted rate in object space.
            Vector const LocalRate = Guided_TransformMatrix(Transposed, Desired);

            // .text:06cf47d4-06cf47f1: F+0x50 = -current + wanted (FUN_06cf49b0), added to the
            // object-space angular velocity: the rate becomes the wanted one.
            Vector Change;
            Change.x = static_cast<float>(static_cast<double>(NegAngular.x) + static_cast<double>(LocalRate.x));
            Change.y = static_cast<float>(static_cast<double>(NegAngular.y) + static_cast<double>(LocalRate.y));
            Change.z = static_cast<float>(static_cast<double>(NegAngular.z) + static_cast<double>(LocalRate.z));
            PhySys::AddToAngularVelocityOS(Object, Change);
        }
    }

    // .text:06cf47ff-06cf4833: F+0x2c = velocity (vt+4); F+0x14 = minus the third column of
    // get_transform (vt+0xc) -- the nose.
    Vector const Velocity = get_velocity();
    Transform const& Xform = get_transform();
    Vector Nose;
    Nose.z = -Xform.Rotation.E[2][2];
    Nose.y = -Xform.Rotation.E[1][2];
    Nose.x = -Xform.Rotation.E[0][2];

    // .text:06cf4838-06cf485f: (nz*vz + ny*vy) + vx*nx against the double 0.0
    // (.rdata:06d65538), TEST AH,0x41 -- nothing unless flying nose first.
    double const Along =
        (static_cast<double>(Nose.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Nose.y) * static_cast<double>(Velocity.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Nose.x);
    if (!(Along > 0.0))
        return;

    // .text:06cf4865-06cf4887: |velocity| = FSQRT((z*z + y*y) + x*x), stored float.
    float const Speed = static_cast<float>(std::sqrt(
        (static_cast<double>(Velocity.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Velocity.y) * static_cast<double>(Velocity.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Velocity.x)));

    // .text:06cf488b-06cf48ac: the third column again (vt+0xc), negated by FUN_06cf4990.
    Transform const& Xform2 = get_transform();
    Vector Forward;
    Forward.x = -Xform2.Rotation.E[0][2];
    Forward.y = -Xform2.Rotation.E[1][2];
    Forward.z = -Xform2.Rotation.E[2][2];

    // .text:06cf48b1-06cf48db: F+0x14 = Forward * Speed (FUN_06ce7c10).
    Vector Wanted;
    Wanted.z = static_cast<float>(static_cast<double>(Forward.z) * static_cast<double>(Speed));
    Wanted.y = static_cast<float>(static_cast<double>(Forward.y) * static_cast<double>(Speed));
    Wanted.x = static_cast<float>(static_cast<double>(Forward.x) * static_cast<double>(Speed));

    // .text:06cf48e0-06cf4904: F+0x44 = -velocity.
    Vector NegVelocity;
    NegVelocity.z = -Velocity.z;
    NegVelocity.y = -Velocity.y;
    NegVelocity.x = -Velocity.x;

    // .text:06cf4909-06cf4941: F+0x50 = Wanted + NegVelocity -> PhySys::AddToVelocity:
    // the whole speed is put back on the nose.
    Vector Change;
    Change.z = static_cast<float>(static_cast<double>(Wanted.z) + static_cast<double>(NegVelocity.z));
    Change.y = static_cast<float>(static_cast<double>(Wanted.y) + static_cast<double>(NegVelocity.y));
    Change.x = static_cast<float>(static_cast<double>(Wanted.x) + static_cast<double>(NegVelocity.x));
    PhySys::AddToVelocity(Object, Change);
}

// ---------------------------------------------------------------------------
// LAB_06cf4450 (.text:06cf4450, RET 8) -- vtable +0x158.
// ---------------------------------------------------------------------------
bool Guided::destroy(int Kind, unsigned int ObjId)
{
    // .text:06cf4463: GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // .text:06cf4468-06cf4481: only a real kill, and not one by a countermeasure
    // (StarSystem FUN_06d0c8d0 with type mask 0x8000000 finds the killer there).
    if (Kind != 1)
        return true;
    if (OwnerSystem->find_object(ObjId, 0x8000000u) != nullptr)
        return true;

    // .text:06cf4487-06cf4505: an explosion archetype with a radius and some effect (the
    // double 0.0001 at .rdata:06d65190).
    Archetype::Root* const Arch = Object->get_archetype();
    if (Arch->ExplosionArch.IsEmpty())
        return true;
    Archetype::Explosion* const Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    if (Explosion == nullptr)
        return true;
    double const Epsilon = 0.0001;
    if (!(static_cast<double>(Explosion->Radius) > Epsilon))
        return true;
    if (!(static_cast<double>(Explosion->Impulse) > Epsilon) &&
        !(static_cast<double>(Explosion->HullDamage) > Epsilon) &&
        !(static_cast<double>(Explosion->EnergyDamage) > Epsilon))
        return true;

    // .text:06cf450b-06cf458f: {own id, the CGuided's owner (+0xe4, 0xa7 narrowing, not
    // null-tested), cause 6 for a cruise disruptor (Munition+0xac) else 5 (after a first
    // store of 4), position, explosion, no skipped collision group} -> StarSystem::explode.
    ExplosionDamageEvent Blast;
    Blast.ExplodingObjId = get_id();
    Blast.InflictorId = cguided()->OwnerId;
    Archetype::Munition const* const MunitionArch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    Blast.Cause = static_cast<DamageCause>(MunitionArch->bCruiseDisruptor ? 6 : 5);
    Blast.Position = get_position();
    Blast.Explosion = Explosion;
    Blast.SkipCollisionGroup = nullptr;
    OwnerSystem->explode(Blast);

    // .text:06cf4597: always true.
    return true;
}

// .text:06cf4230 (RET 8) -- Guided's +0x17c, the body Mine::damage_sub_obj ports.
void Guided::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

// .text:06cf4240 (RET 8) -- Guided's +0x180, the body Mine::munition_hit ports.
void Guided::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (Hit.Munition->Seeker != 0)
    {
        Damage.bDestroyed = true;
        return;
    }
    GameObject::munition_hit(Hit, Damage);
}

// ---------------------------------------------------------------------------
// LAB_06cf4270 (.text:06cf4270, RET 4) -- vtable +0x1a4.
// ---------------------------------------------------------------------------
void Guided::on_collision(FCollisionEvent const& Hit)
{
    // .text:06cf427b-06cf4286: nothing for an object without an id (vt+0x20).
    if (Hit.Other->get_id() == 0)
        return;

    // .text:06cf4288-06cf42a3: the CGuided (0xa7 narrowing), used later, not null-tested.
    CGuided* const Cg = cguided();

    // .text:06cf42a3-06cf42c2: a player-owned object (its CSimple+0xb4 under the & 3
    // narrowing, not null-tested) handles the hit on its own client.
    if (Hit.Other->csimple()->OwnerPlayer != 0)
        return;

    // .text:06cf42c4-06cf42f8: the record at [ESP+0xc] -- munition archetype id
    // (get_archetype()+0x08 on the raw Object), owner (CGuided+0xe4), the object hit,
    // the sub-object hit (Hit+8) -> StarSystem::munition_collision (FUN_06d0d500).
    // Retail never writes the record's +0x10..+0x1b (hit position); the port zeroes it.
    SSPMunitionCollisionInfo Info = {};
    Info.Field_0x00 = Object->get_archetype()->Nickname.Id;
    Info.Field_0x04 = Cg->OwnerId;
    Info.ObjectId = Hit.Other->get_id();
    Info.Field_0x0c = Hit.OtherSubObjId;
    OwnerSystem->munition_collision(Info);
}

// ---------------------------------------------------------------------------
// LAB_06cf4ae0 (.text:06cf4ae0, RET 4) -- CObjectCB slot 0 on the +0x70 sub-object.
// ---------------------------------------------------------------------------
void Guided::added(CObject* Obj)
{
    // .text:06cf4ae9-06cf4b03: the id of the root, when the root is a CSimple; else 0.
    CObject* const Root = GetRoot(Obj);
    unsigned int Id = 0;
    if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
        Id = static_cast<CSimple*>(Root)->Id;

    // .text:06cf4b09-06cf4b36: the owner is exempt while its owner-safe time runs (the
    // CGuided narrowing is not null-tested).
    CGuided* const Cg = cguided();
    if (Id != 0 && Id == Cg->OwnerId && Cg->is_owner_safe())
        return;

    // .text:06cf4b38-06cf4b3c.
    bTriggered = true;
    TriggerObjId = Id;
}

// .text:06ce1050 `RET 4` -- CObjectCB slot 1 of Guided's table: nothing.
void Guided::deleted(CObject* Obj)
{
    (void)Obj;
}

// ---------------------------------------------------------------------------
// LAB_06cf4b50 (.text:06cf4b50, RET 8) -- PhySys::Controller slot 4 on +0x60.
// ---------------------------------------------------------------------------
void Guided::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // .text:06cf4b56-06cf4b9f: only while the motor burns -- MotorData (CGuided+0xfc)
    // set, SeekerElapsed (+0x100) not below Delay (+8), and Delay + Lifetime (+0x10) not
    // below SeekerElapsed (both TEST AH,1: an unordered compare also stops it).
    CGuided* const Cg = cguided();
    Archetype::MotorData const* const Motor = Cg->MotorArch;
    if (Motor == nullptr)
        return;
    if (!(Cg->SeekerElapsed >= Motor->Delay))
        return;
    double const BurnEnd = static_cast<double>(Motor->Lifetime) + static_cast<double>(Motor->Delay);
    if (!(BurnEnd >= static_cast<double>(Cg->SeekerElapsed)))
        return;

    // .text:06cf4ba1-06cf4bdc: the motor again (re-narrowed), and the third column of
    // get_transform (vt+0xc).
    Archetype::MotorData const* const Motor2 = cguided()->MotorArch;
    Transform const& Xform = get_transform();
    Vector Axis;
    Axis.x = Xform.Rotation.E[0][2];
    Axis.y = Xform.Rotation.E[1][2];
    Axis.z = Xform.Rotation.E[2][2];

    // .text:06cf4be0-06cf4bf9: PhySys::Controller::push(Axis, -((mass * Accel) * dt))
    // with get_mass (vt+0x30), MotorData+0xc and IVP_Event_Sim+0; stored as a float.
    float const Mass = get_mass();
    float const Amount = static_cast<float>(
        -((static_cast<double>(Mass) * static_cast<double>(Motor2->Accel)) *
          static_cast<double>(EventSim->delta_time)));
    push(Axis, Amount);
}

// ---------------------------------------------------------------------------
// LAB_06cf4c10 (.text:06cf4c10, RET 0xc) -- IObjAffect +0x48 on the +4 sub-object.
// ---------------------------------------------------------------------------
int Guided::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    // .text:06cf4c10-06cf4c1c: a source of -1 is taken as 0.
    if (SourceClient == -1)
        SourceClient = 0;

    // .text:06cf4c24-06cf4c4e: a munition that does not seek (Munition+0x90 == 0 on the
    // CGuided's archetype, narrowing not null-tested) refuses: -3.
    if (cguided()->munitionarch()->Seeker == 0)
        return -3;

    // .text:06cf4c54-06cf4c6a: in single player a change from a client is only relayed.
    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        // .text:06cf4c70-06cf4cf2: a different target or sub-target is taken (CGuided's
        // own set_target / set_sub_target, both gated on the seeker, inlined in retail).
        CGuided* const Cg = cguided();
        if (Cg->get_target() != Target || Cg->SubTarget != SubTarget)
        {
            bChanged = true;
            Cg->set_target(Target);
            Cg->set_sub_target(SubTarget);
        }

        // .text:06cf4cf9-06cf4d04: nothing to tell when it came from nobody and changed
        // nothing.
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    // .text:06cf4d06-06cf4d2d: StarSystem FUN_06d0e670 -- IClient XSetTarget for this
    // object (get_id, vt+0x20), sub-object SUBOBJ_ID_ROOT, the new target pair.
    unsigned int const ObjId = get_id();
    OwnerSystem->notify_set_target(ObjId, SUBOBJ_ID_ROOT, Target, SubTarget, SourceClient);

    // .text:06cf4d32.
    return 0;
}

// ===========================================================================
// Bullet
// ===========================================================================

// .text:06d09b90 `MOV EAX,0x2000000 / RET`.
unsigned int Bullet::get_type_bits() const
{
    return 0x2000000u;
}

CBeam* Bullet::cbeam() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x67u) != 0x67u)
        return nullptr;
    return static_cast<CBeam*>(Obj);
}

// ---------------------------------------------------------------------------
// FUN_06cf4d40 (.text:06cf4d40, RET 4) -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Bullet::create(Archetype::Root* Arch)
{
    // .text:06cf4d42-06cf4d56: CObject::Alloc(0x67) into CBase::Object.
    Object = CObject::Alloc(CObject::ClassBeam);

    // .text:06cf4d53-06cf4d59: Archetype::Root::create_instance(Object) -- before open.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // .text:06cf4d5f-06cf4d74: DALib::Engine vt+0x6c, UpdateInstance(Object+4, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    // .text:06cf4d77-06cf4d90: open(Arch) (vt+0x4c) on the (ObjectClass & 0x67) == 0x67
    // narrowing, not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & 0x67u) != 0x67u)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    // .text:06cf4d93-06cf4d98: CBase::advise(true).
    advise(true);

    // .text:06cf4d9e-06cf4dae: get_index() (vt+0x1c) != -1.
    return get_index() != -1;
}

// .text:06d02cb0 `XOR AL,AL / RET 4` -- the Bullet vtable's +0x168.
bool Bullet::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// ---------------------------------------------------------------------------
// FUN_06cf4dc0 (.text:06cf4dc0, RET 4) -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Bullet::update(float Dt)
{
    // .text:06cf4dc9-06cf4dd4: GameObject's base update; false is returned as is.
    if (!GameObject::update(Dt))
        return false;

    // .text:06cf4dd7-06cf4df8: the CBeam's own update (vt+0x50) with UPDATE_DEFAULT; the
    // narrowing is not null-tested.
    CBeam* const Cb = cbeam();
    Cb->update(Dt, CObject::UPDATE_DEFAULT);

    // .text:06cf4dfb-06cf4e17: FCOMP 0.0f, TEST AH,1 -- a spent lifetime (CProjectile+0xe8
    // below zero, or unordered) removes it quietly: destroy(0, 0).
    if (!(Cb->Lifetime >= 0.0f))
        destroy(0, 0);

    // .text:06cf4e1d-06cf4e2d: true unless destroyed.
    return !is_destroyed();
}

// .text:06d28880 `RET 8` -- Bullet's +0x17c: nothing.
void Bullet::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8` -- Bullet's +0x180: nothing.
void Bullet::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8` -- Bullet's +0x184: nothing.
void Bullet::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06d28880 `RET 8` -- Bullet's own +0x1bc: nothing.
void Bullet::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// ---------------------------------------------------------------------------
// LAB_06cf4e40 (.text:06cf4e40, RET 4) -- vtable +0x1a4.
// ---------------------------------------------------------------------------
void Bullet::on_collision(FCollisionEvent const& Hit)
{
    // .text:06cf4e46-06cf4e96: the record at [ESP+8] -- munition archetype id
    // (get_archetype()+0x08 on the raw Object), owner (CBeam+0xe4 under the 0x67
    // narrowing, not null-tested), the object hit (vt+0x20), the sub-object hit (Hit+8)
    // -> StarSystem::munition_collision (FUN_06d0d500). Retail never writes the record's
    // +0x10..+0x1b (hit position); the port zeroes it.
    CBeam* const Cb = cbeam();
    SSPMunitionCollisionInfo Info = {};
    Info.Field_0x00 = Object->get_archetype()->Nickname.Id;
    Info.Field_0x04 = Cb->OwnerId;
    Info.ObjectId = Hit.Other->get_id();
    Info.Field_0x0c = Hit.OtherSubObjId;
    OwnerSystem->munition_collision(Info);

    // .text:06cf4e9b-06cf4eaa: the round is spent -- destroy(1, the object hit).
    destroy(1, Hit.Other->get_id());
}
