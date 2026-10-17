// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_explosion.h"

#include "DALib.h"
#include "client_cliobj.h"         // Archetype::HookUpInstanceTreeMaterials (FUN_00425480)
#include "client_debris.h"         // FDebrisArch, the Debris gameobj factories
#include "client_gameobj.h"
#include "client_objdb.h"          // ClientObjDb_GetKindHead (FUN_00540fc0)
#include "common_misc.h"           // random_on_unit_sphere / random_inside_sphere
#include "common_physys.h"
#include "common_universe.h"       // Archetype::GetSimpleID / GetSimple
#include "dacom.h"                 // FDUMP
#include "engine/engbase.h"
#include "hud_damage_indicator.h"  // GetHudDamageIndicator (DAT_00674a10), FUN_004d6f00
#include "particle_effect.h"       // CliCreateEffectWorldOneShot (FUN_00503f40)
#include "player_behavior.h"       // GetPlayerObjRW (FUN_0054baf0)
#include "star_sys.h"              // StarSysGetActiveSystemId (FUN_00554e90)
#include "vis_effect_arch.h"       // FindEffectArch (FUN_0040f080)
#include "x86math.h"               // FTransform

#include <cmath>
#include <cstdlib>
#include <cstring>

static_assert(sizeof(Action_Explode) == 0x28, "Action_Explode is IAction::operator new(0x28)");

namespace
{
    char const kExplosionSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\explosion.cpp";

    // DAT_005caeec: 1/32768, the rand() scale.
    constexpr float kRandScale = 3.0517578125e-05f;
    // DAT_005de598: the piece mass per unit of radius (0x0053691c, 0x00536df5).
    constexpr float kPieceMassPerRadius = 200.0f;
    // DAT_005de590 (double): the share of the radius the shatter piece's sphere gets
    // (0x00536976).
    constexpr double kPieceSphereScale = 0.65;
    // DAT_005dbc98 (double): below this centre distance a piece flies off in a random
    // direction (0x00536a0e).
    constexpr double kPieceMinDistance = 0.0001;
    // DAT_005cbf58 (double): the scatter radius as a share of debris_impulse (0x00536a94).
    constexpr double kPieceScatterShare = 0.25;

    // The Gameobj's object narrowed by (ObjectClass & Mask) == Mask, null otherwise -- the
    // `this`, `this + 0xc` and object tests every call site here inlines (0x005366a7..005366c8,
    // 0x0053692f..0053694f, 0x00536fb9..00536fd6, 0x005370db..005370f7).
    CObject* NarrowGameobjObject(FClientGameobj* Gameobj, unsigned Mask)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    // FUN_0054baf0 -- the player's IObjRW is the player's Gameobj (IObjRW is its first base).
    FClientGameobj* PlayerGameobj()
    {
        return static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    }
}

// ===========================================================================
// Action_Explode
// ===========================================================================

Action_Explode::Action_Explode(Archetype::Explosion* InArch, FClientGameobj* InGameobj,
                               Vector const& InPos)
    : Arch(InArch), Gameobj(InGameobj), Pos(InPos), Lifetime(0.0f), Elapsed(-1.0f),
      PrevElapsed(-1.0f)
{
    // 0x00536219..0x00536257: IAction's part (+0x04 = 0), +0x08 Arch, +0x0c Gameobj, +0x10 the
    // position copied, +0x1c = 0, +0x20 = +0x24 = -1.0f (0xbf800000).
    // 0x00536260..0x00536298: Lifetime = Arch->Lifetime + rand()/32768 * (LifetimeMax -
    // Arch->Lifetime).
    float const LifetimeMin = Arch->Lifetime;    // +0x18
    float const LifetimeMax = Arch->LifetimeMax; // +0x1c
    Lifetime = static_cast<float>(rand()) * kRandScale * (LifetimeMax - LifetimeMin) +
               LifetimeMin;
}

Action_Explode::~Action_Explode()
{
    // FUN_00536430: IAction::~IAction; the deleting form frees through IAction::operator delete.
}

Action_Explode* Action_Explode::Create(Archetype::Explosion* Arch, FClientGameobj* Gameobj,
                                       Vector const* Pos)
{
    // FUN_005361e0 (cdecl).
    Action_Explode* const Action = new Action_Explode(Arch, Gameobj, *Pos);

    // 0x0053629b..0x00536339: impulse > 0 -- the player's ship unless it is the one exploding,
    // then every Gameobj of ObjDB kind 10 (Debris) and kind 9, through FUN_005366a0.
    if (Arch->Impulse > 0.0f)
    {
        FClientGameobj* const Player = PlayerGameobj();
        if (Player != nullptr)
        {
            unsigned const OwnerId = Gameobj->get_id();                   // 0x005362bf
            if (Player->get_id() != OwnerId)                              // 0x005362c8
                Action->ApplyImpulse(Player);
        }
        int Kind = 10;
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);             // FUN_00540fc0
        while (Go != nullptr)
        {
            Action->ApplyImpulse(Go);
            Go = Go->ListNext;                                            // +0x14
        }
        Kind = 9;
        Go = ClientObjDb_GetKindHead(&Kind);
        while (Go != nullptr)
        {
            Action->ApplyImpulse(Go);
            Go = Go->ListNext;
        }
    }

    // 0x0053633b..0x005363f2: hull_damage > 0 or energy_damage > 0, and a player ship that is
    // not the one exploding with its physical sphere reaching the blast (distance to its
    // centre < its radius + the blast radius): the HUD damage indicator flashes towards the
    // blast (FUN_004d6f00; the radius pushed as its second argument is not read).
    if (Arch->HullDamage > 0.0f || Arch->EnergyDamage > 0.0f)
    {
        FClientGameobj* const Player = PlayerGameobj();
        if (Player != nullptr)
        {
            unsigned const PlayerId = Player->get_id();                   // 0x00536378
            if (PlayerId != Gameobj->get_id())                            // 0x00536383
            {
                float PlayerRadius = 0.0f;
                Vector PlayerCenter;
                Player->get_physical_radius(PlayerRadius, PlayerCenter);  // vt+0x2c
                float const Reach = PlayerRadius + Arch->Radius;
                float const Dx = Action->Pos.x - PlayerCenter.x;
                float const Dy = Action->Pos.y - PlayerCenter.y;
                float const Dz = Action->Pos.z - PlayerCenter.z;
                // 0x005363be..0x005363e1: Reach^2 against dx^2 + dz^2 + dy^2, TEST AH,0x41 /
                // JNZ -- on only when Reach^2 is strictly greater.
                if (Reach * Reach > Dx * Dx + Dz * Dz + Dy * Dy)
                {
                    HUD_DamageIndicator* const Indicator = GetHudDamageIndicator();
                    if (Indicator != nullptr)
                    {
                        int RadiusBits = 0;
                        std::memcpy(&RadiusBits, &Arch->Radius, sizeof(RadiusBits));
                        Indicator->FlashTowardsPoint(&Action->Pos, RadiusBits);
                    }
                }
            }
        }
    }

    // 0x005363f7..0x005363fd: the first update, with no time passed.
    Action->update(0.0f);
    return Action;
}

bool Action_Explode::LifetimeEndedThisTick() const
{
    // The lifetime is now <= 0 and either this is the first tick or it was still above 0
    // before it. Retail spells it out for a negative effect delay (0x00536500..0x0053652d),
    // for a negative innards start time (0x005365f8..0x00536621) and for the death
    // (0x0053662a..0x00536653).
    // TEST AH,0x41 / JP: on only when Lifetime <= 0 (ordered).
    if (!(Lifetime <= 0.0f))
        return false;
    // CMP [+0x24],0xbf800000 / JZ: the first tick.
    if (PrevElapsed == -1.0f)
        return true;
    // TEST AH,0x41 / JNZ: on only when Elapsed - PrevElapsed + Lifetime > 0.
    return Elapsed - PrevElapsed + Lifetime > 0.0f;
}

bool Action_Explode::DelayReachedThisTick(float Delay) const
{
    // Retail spells it out for each effect (0x005364c3..0x005364fb) and for the innards
    // (0x005365c2..0x005365f6).
    // TEST AH,0x1 / JNZ: below 0 or NaN takes the lifetime test.
    if (!(Delay >= 0.0f))
        return LifetimeEndedThisTick();
    // TEST AH,0x5 / JP: on only when PrevElapsed < Delay (ordered).
    if (!(PrevElapsed < Delay))
        return false;
    // TEST AH,0x1 / JZ: on when Elapsed is not below Delay (and not NaN).
    return Elapsed >= Delay;
}

bool Action_Explode::update(float Dt)
{
    // FUN_00536460 (RET 4).
    // 0x00536466..0x0053647b: the Gameobj is done (slot 86) -- the action is too.
    if (Gameobj->Slot158())
        return false;

    // 0x0053647e..0x005364ae: PrevElapsed = Elapsed; Elapsed = Dt the first time (still the
    // -1.0f pattern 0xbf800000), else Elapsed + Dt; Lifetime -= Dt.
    PrevElapsed = Elapsed;
    if (Elapsed == -1.0f)
        Elapsed = Dt;
    else
        Elapsed = Dt + Elapsed;
    Lifetime = Lifetime - Dt;

    // 0x005364b1..0x005365a7: every effect whose delay was reached starts as a world one-shot
    // at the blast point with the Gameobj's orientation and velocity (FUN_0040f080 +
    // FUN_00503f40 on DAT_00674c60, no companion).
    std::vector<Archetype::FExplosionEffect> const& Effects = Arch->Effects;
    size_t Index = 0;
    for (Index = 0; Index < Effects.size(); ++Index)
    {
        Archetype::FExplosionEffect const& Effect = Effects[Index];
        if (!DelayReachedThisTick(Effect.Strength))                       // node +0x0c
            continue;
        // 0x0053652f..0x00536568: the Gameobj's transform (vt+0x0c) -- its matrix, then the
        // blast point -- and its velocity (vt+0x04), both before the id test.
        Transform const& GameobjXform = Gameobj->get_transform();
        FTransform World;
        World.Rotation = reinterpret_cast<FMatrix const&>(GameobjXform.Rotation);
        World.Translation.X = Pos.x;
        World.Translation.Y = Pos.y;
        World.Translation.Z = Pos.z;
        Vector const Velocity = Gameobj->get_velocity();
        // 0x0053656b..0x00536579: an empty effect id starts nothing.
        if (Effect.Fx.IsEmpty())                                          // node +0x08
            continue;
        CliCreateEffectWorldOneShot(FindEffectArch(&Effect.Fx.Id), &World, &Velocity.x,
                                    nullptr);
    }

    // 0x005365ae..0x00536625: innards_debris_num > 0 and the innards start time reached.
    if (Arch->InnardsDebrisNum > 0.0f &&
        DelayReachedThisTick(Arch->InnardsDebrisStartTime))
        SpawnInnardsDebris();                                             // FUN_00536fb0

    // 0x0053662a..0x00536677: the lifetime ran out this tick -- unless process = none (2) the
    // Gameobj is finished (slot 87), and process = shatter (0) breaks it up (FUN_00536800).
    if (LifetimeEndedThisTick())
    {
        if (Arch->Process != 2u)
            Gameobj->Slot15c(true);
        if (Arch->Process == 0u)
            Shatter();
    }

    // 0x0053667c..0x0053669c: TEST AH,0x41 / JP -- still running while Lifetime > 0 (or NaN).
    return !(Lifetime <= 0.0f);
}

void Action_Explode::ApplyImpulse(FClientGameobj* Target)
{
    // FUN_005366a0 (RET 4). The target's object narrowed by class & 1 -- no null test on the
    // result (0x005366ca reads its vtable).
    CObject* const Simple = NarrowGameobjObject(Target, 1u);
    Vector const Center = Simple->get_center_of_mass();                  // vt+0x68

    // 0x005366d6..0x00536717: radius^2 against dz^2 + dy^2 + dx^2, TEST AH,0x41 / JNZ -- on
    // only when the radius^2 is strictly greater.
    float const Radius = Arch->Radius;
    float const Dx = Center.x - Pos.x;
    float const Dy = Center.y - Pos.y;
    float const Dz = Center.z - Pos.z;
    if (!(Radius * Radius > Dz * Dz + Dy * Dy + Dx * Dx))
        return;

    // 0x0053671d..0x005367ac: the direction away from the blast, normalised; a centre exactly
    // on the blast point (TEST AH,0x44 / JNP: length^2 == 0) takes random_on_unit_sphere.
    Vector Direction;
    Direction.x = Center.x - Pos.x;
    Direction.y = Center.y - Pos.y;
    Direction.z = Center.z - Pos.z;
    float const LengthSq =
        Direction.z * Direction.z + Direction.y * Direction.y + Direction.x * Direction.x;
    if (LengthSq == 0.0f)
    {
        Direction = random_on_unit_sphere();
    }
    else
    {
        float const InvLength = 1.0f / std::sqrt(LengthSq);              // DAT_005c75dc = 1.0f
        Direction.x = Direction.x * InvLength;
        Direction.y = Direction.y * InvLength;
        Direction.z = Direction.z * InvLength;
    }

    // 0x005367b0..0x005367ea: scaled by the impulse, applied at the blast point when the
    // object has a physics body.
    float const Impulse = Arch->Impulse;
    Direction.x = Direction.x * Impulse;
    Direction.y = Direction.y * Impulse;
    Direction.z = Direction.z * Impulse;
    if (Simple->BodyPtr != nullptr)
        PhySys::AddImpulseAtPoint(Simple, Direction, Pos);
}

void Action_Explode::Shatter()
{
    // FUN_00536800.
    // 0x00536820..0x00536854: every piece of the Gameobj's instance tree, at most
    // num_child_pieces (FUN_00536c20 on get_index, vt+0x1c).
    long const RootInstance = Gameobj->get_index();
    std::vector<CObject*> Pieces;
    CollectChildPieces(RootInstance, Arch->NumChildPieces, Pieces);

    size_t Index = 0;
    for (Index = 0; Index < Pieces.size(); ++Index)
    {
        CObject* const Piece = Pieces[Index];

        // 0x00536868..0x00536882, 0x00536b0c: a piece with no radius is released.
        Vector RadiusCenter;
        if (!(Piece->get_physical_radius_r(RadiusCenter) > 0.0f))
        {
            Piece->Release();
            continue;
        }

        // 0x00536888..0x0053689b: its debris record, drawn from debris_type.
        FDebrisArch* const DebrisArch = PickExplosionDebrisArch(&Arch->DebrisTypes);
        if (DebrisArch == nullptr)
            continue;

        // 0x005368a1..0x0053698e: a piece with no physics body gets a sphere: mass = radius *
        // 200, the default damping, type 2 with collisions on, no inertia, the current system,
        // and the exploding object's collision group as the name; sphere radius = radius * 0.65.
        if (Piece->BodyPtr == nullptr)
        {
            PhySys::CreateParms Parms;
            std::memset(&Parms, 0, sizeof(Parms));
            Parms.Mass = 1.0f;
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Parms.CollisionGroupId = 0;
            Parms.Type = 2;
            Parms.bFlag1c = 0;
            Parms.bEnableCollisions = 1;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            Parms.Name[0] = '\0';
            Vector MassCenter;
            Parms.Mass = Piece->get_physical_radius_r(MassCenter) * kPieceMassPerRadius;
            Parms.CollisionGroupId = StarSysGetActiveSystemId();                // FUN_00554e90
            // 0x0053692f..0x00536968: PhySys::GetCollisionGroup(the Gameobj's object narrowed
            // by class & 1) copied into the name (an inline strcpy, no length limit).
            char const* Group = PhySys::GetCollisionGroup(NarrowGameobjObject(Gameobj, 1u));
            char* Out = Parms.Name;
            char Ch = '\0';
            do
            {
                Ch = *Group;
                ++Group;
                *Out = Ch;
                ++Out;
            } while (Ch != '\0');
            Vector SphereCenter;
            float const SphereRadius = static_cast<float>(
                static_cast<double>(Piece->get_physical_radius_r(SphereCenter)) *
                kPieceSphereScale);
            PhySys::BuildIVP_Sphere(Piece, SphereRadius, Parms);
        }

        // 0x00536991..0x00536995: the piece becomes a Debris gameobj (FUN_00530620).
        CreateDebrisGameobjForObject(Piece, DebrisArch);

        // 0x0053699a..0x005369eb: the direction from the Gameobj's centre of mass (vt+0x14) to
        // the piece's (PhySys::GetCenterOfMass); the Gameobj's velocity (vt+0x04) is read
        // between the two.
        Vector const GameobjCenter = Gameobj->get_center_of_mass();
        Vector const GameobjVelocity = Gameobj->get_velocity();
        Vector const PieceCenter = PhySys::GetCenterOfMass(Piece);
        Vector Direction;
        Direction.x = PieceCenter.x - GameobjCenter.x;
        Direction.y = PieceCenter.y - GameobjCenter.y;
        Direction.z = PieceCenter.z - GameobjCenter.z;

        // 0x005369f0..0x00536a3c: closer than 0.0001 (TEST AH,0x5 / JP: strictly below) -- a
        // random direction.
        double const Dy = static_cast<double>(Direction.y);
        double const Dz = static_cast<double>(Direction.z);
        double const Dx = static_cast<double>(Direction.x);
        double const Distance = std::sqrt(Dy * Dy + Dz * Dz + Dx * Dx);
        if (Distance < kPieceMinDistance)
            Direction = random_on_unit_sphere();

        // 0x00536a40..0x00536abb: normalised and scaled by debris_impulse, plus a random point
        // inside a sphere of a quarter of debris_impulse.
        float const DebrisImpulse = Arch->DebrisImpulse;
        float const Length = std::sqrt(
            Direction.y * Direction.y + Direction.z * Direction.z + Direction.x * Direction.x);
        float const Scale = DebrisImpulse / Length;
        Direction.x = Direction.x * Scale;
        Direction.y = Direction.y * Scale;
        Direction.z = Direction.z * Scale;
        Vector const Scatter = random_inside_sphere(
            static_cast<float>(static_cast<double>(DebrisImpulse) * kPieceScatterShare));
        Direction.x = Direction.x + Scatter.x;
        Direction.y = Direction.y + Scatter.y;
        Direction.z = Direction.z + Scatter.z;

        // 0x00536ac2..0x00536b07: the Gameobj's velocity carried over, the impulse applied when
        // the piece has a body, and a random spin inside a sphere of 2.
        PhySys::AddToVelocity(Piece, GameobjVelocity);
        if (Piece->BodyPtr != nullptr)
            PhySys::LinearImpulse(Piece, Direction, 1.0f);
        PhySys::AddToAngularVelocityOS(Piece, random_inside_sphere(2.0f));
    }
    // 0x00536b28..0x00536b29: the vector's buffer freed (the local's destructor).
}

void Action_Explode::CollectChildPieces(long Instance, int MaxPieces,
                                        std::vector<CObject*>& Pieces)
{
    // FUN_00536c20 (RET 0xc). `this` is not read.
    IEngine* const Engine = DALib::Engine;
    void* const Inst = reinterpret_cast<void*>(Instance);

    // 0x00536c20..0x00536c7c: an instance with children -- each child is collected (which
    // detaches it) and the first child is asked for again until there is none.
    void* Child = Engine->GetRelatedInstance(Inst, 1u, G_InvalidEngineHandle);    // vt+0xd8
    if (Child != G_InvalidEngineHandle)
    {
        do
        {
            CollectChildPieces(reinterpret_cast<long>(Child), MaxPieces, Pieces);
            Child = Engine->GetRelatedInstance(Inst, 1u, G_InvalidEngineHandle);
        } while (Child != G_InvalidEngineHandle);
        return;
    }

    // 0x00536c7f..0x00536cd3: a leaf -- the root (no parent) stays; any other leaf is detached
    // from its parent (vt+0xc0 with (parent, instance)) after both owners are read (vt+0x80).
    void* const Parent = Engine->GetInstanceParent(Inst);                         // vt+0xd4
    if (Parent == G_InvalidEngineHandle)
        return;
    CObject* const Owner = static_cast<CObject*>(Engine->GetInstanceUserData(Inst));
    CObject* const ParentOwner = static_cast<CObject*>(Engine->GetInstanceUserData(Parent));
    Engine->DestroyJoint(Parent, Inst);

    // The piece count compares unsigned (JNC, 0x00536d00 / 0x00536d83).
    if (Owner == ParentOwner)
    {
        // 0x00536ce5..0x00536d65: a part of the parent's own object -- a new object for it
        // (CObject::Alloc(1), bound as the instance's transform through vt+0x70 and opened on
        // the parent object's archetype) while there is room, else the instance is destroyed
        // (vt+0x60).
        if (Pieces.size() < static_cast<unsigned>(MaxPieces))
        {
            CObject* const Piece = CObject::Alloc(CObject::ClassObject);
            Engine->SetInstanceTransform(Inst, static_cast<IEngineInstance*>(Piece));
            Piece->open(ParentOwner->get_archetype());
            Pieces.push_back(Piece);                                                // FUN_00537260
        }
        else
        {
            Engine->DestroyInstance(Inst);
        }
        return;
    }

    // 0x00536d68..0x00536fa6: an object of its own. While there is room: a body in mode 2 is
    // detached from the compound (the default damping, mass = radius * 200, no inertia) and
    // its collisions turned on, any other is deactivated; then it is collected. Without room it
    // is released.
    if (Pieces.size() < static_cast<unsigned>(MaxPieces))
    {
        if (Owner->PhysicsControlMode == 2)
        {
            PhySys::DetachParms Parms;
            std::memset(&Parms, 0, sizeof(Parms));
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping = PhySys::DEFAULT_ANGULAR_DAMPING;
            Parms.Mass = 1.0f;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            Parms.Name[0] = '\0';
            Vector Center;
            Parms.Mass = Owner->get_physical_radius_r(Center) * kPieceMassPerRadius;
            PhySys::Detach(Owner, Parms);
            Owner->enable_collisions_r(true);
        }
        else
        {
            PhySys::DeactivatePhysics(Owner);
        }
        Pieces.push_back(Owner);
    }
    else
    {
        Owner->Release();
    }
}

void Action_Explode::SpawnInnardsDebris()
{
    // FUN_00536fb0.
    // 0x00536fb6..0x00536ffc: the spawn radius -- innards_debris_radius when it is > 0, else
    // the Gameobj's physical radius (its object narrowed by class & 1, no null test).
    CObject* const Simple = NarrowGameobjObject(Gameobj, 1u);
    Vector RadiusCenter;
    float SpawnRadius = Simple->get_physical_radius_r(RadiusCenter);
    if (Arch->InnardsDebrisRadius > 0.0f)
        SpawnRadius = Arch->InnardsDebrisRadius;

    // 0x00537000..0x00537019, 0x00537223..0x00537250: no innards_debris_object entries is an
    // error. The count goes to FDUMP as the double it was pushed as (the format says %d).
    std::vector<unsigned> const& Objects = Arch->InnardsDebrisObjects;
    int const ObjectCount = static_cast<int>(Objects.size());
    if (ObjectCount <= 0)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: [Explosion] %s has innards_debris_num = %d, but has no "
              "'innards_debris_object =' entries",
              kExplosionSourceFile, 0x21e, Arch->Nickname.get_string(),
              static_cast<double>(Arch->InnardsDebrisNum));
        return;
    }

    // 0x0053701f..0x00537035: nothing while innards_debris_num is not above 0.
    if (!(0.0f < Arch->InnardsDebrisNum))
        return;

    int Spawned = 0;
    do
    {
        // 0x00537050..0x00537074: an object drawn uniformly -- (rand() * count) / 32768,
        // rounded towards zero.
        int const Pick = (rand() * ObjectCount) / 32768;
        // 0x00537077..0x005370a2: a random point inside the spawn radius, around the blast.
        Vector const Offset = random_inside_sphere(SpawnRadius);
        Vector SpawnPos;
        SpawnPos.x = Offset.x + Pos.x;
        SpawnPos.y = Offset.y + Pos.y;
        SpawnPos.z = Offset.z + Pos.z;
        // 0x005370a5..0x005370c6: its simple archetype, and a debris record from debris_type.
        ID_String ObjectName;
        ObjectName.Id = Objects[Pick];
        Archetype::Root* const ObjectArch =
            Archetype::GetSimple(Archetype::GetSimpleID(ObjectName));
        FDebrisArch* const DebrisArch = PickExplosionDebrisArch(&Arch->DebrisTypes);
        if (DebrisArch != nullptr)
        {
            // 0x005370cc..0x0053712e: the Debris gameobj (FUN_005306e0), its object (class & 1,
            // no null test) in the exploding object's collision group, its materials hooked
            // up (FUN_00425480).
            FClientDebrisGameobj* const Debris =
                CreateDebrisGameobjFromArch(ObjectArch, DebrisArch, &SpawnPos);
            CObject* const DebrisObject = NarrowGameobjObject(Debris, 1u);
            CObject* const Source = NarrowGameobjObject(Gameobj, 1u);
            PhySys::SetCollisionGroup(DebrisObject, PhySys::GetCollisionGroup(Source));
            Archetype::HookUpInstanceTreeMaterials(DebrisObject->EngineInstanceId);

            // 0x00537133..0x005371e5: flung along the spawn offset at rand()/32768 * 2 *
            // debris_impulse, plus the Gameobj's velocity (vt+0x04).
            float const Speed2 = Arch->DebrisImpulse + Arch->DebrisImpulse;
            Vector Velocity = Offset;
            float const Scale =
                static_cast<float>(rand()) * kRandScale * Speed2 /
                std::sqrt(Velocity.y * Velocity.y + Velocity.z * Velocity.z +
                          Velocity.x * Velocity.x);
            Velocity.x = Velocity.x * Scale;
            Velocity.y = Velocity.y * Scale;
            Velocity.z = Velocity.z * Scale;
            Vector const GameobjVelocity = Gameobj->get_velocity();
            Velocity.x = Velocity.x + GameobjVelocity.x;
            Velocity.y = Velocity.y + GameobjVelocity.y;
            Velocity.z = Velocity.z + GameobjVelocity.z;
            PhySys::AddToVelocity(DebrisObject, Velocity);
            // 0x005371eb..0x005371f9: a random spin inside a sphere of 5.
            PhySys::AddToAngularVelocityOS(DebrisObject, random_inside_sphere(5.0f));
        }
        // 0x00537202..0x00537215: TEST AH,0x5 / JNP -- again while Spawned < innards_debris_num.
        ++Spawned;
    } while (static_cast<float>(Spawned) < Arch->InnardsDebrisNum);
}

FDebrisArch* PickExplosionDebrisArch(std::vector<Archetype::FExplosionDebrisType> const* Types)
{
    // FUN_00536b50 (cdecl).
    float const Roll = static_cast<float>(rand()) * kRandScale;
    // The running sum stays on the x87 stack (FADD float per entry, 0x00536b92); double is the
    // closest the port has.
    double Sum = 0.0;
    int const Count = static_cast<int>(Types->size());
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        Sum = Sum + static_cast<double>((*Types)[Index].Count);
        // TEST AH,0x5 / JNP: taken when Roll < Sum.
        if (static_cast<double>(Roll) < Sum)
            break;
    }
    // 0x00536ba9..0x00536bb1: past the end -- the default record. (The Index == -1 test at
    // 0x00536bb6 cannot be taken: Index is in [0, Count) there.)
    if (Index >= Count)
        return GetDefaultDebrisArch();

    // 0x00536bc8..0x00536c08: the drawn name looked up (FUN_005309c0); a miss warns and falls
    // back to the default.
    Archetype::FExplosionDebrisType const& Drawn = (*Types)[Index];
    FDebrisArch* const Found = FindDebrisArch(&Drawn.Debris);
    if (Found != nullptr)
        return Found;
    FDUMP(0x100002,
          "%s(%d) : *** WARNING: Exploding: Couldn't find debris_type[%s]. using default.",
          kExplosionSourceFile, 0x70, Drawn.Debris.get_string());
    return GetDefaultDebrisArch();
}
