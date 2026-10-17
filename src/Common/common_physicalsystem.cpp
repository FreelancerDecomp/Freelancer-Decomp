// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_physys.h"
#include "physics_tick_profile.h" // Port-only owner-requested diagnostics

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::GetZoneDb (PhySys::GenerateCollisions)
#include "common_id.h"
#include "common_universe.h" // Universe::ISystem (PhySys::GenerateCollisions)
#include "common_net.h"  // IsMPServer -- CRemotePhysicsSimulation::tracef (.text:06291be6)
#include "common_phyarch.h"
#include "common_misc.h"     // SinglePlayer
#include "common_server.h"

#include "dacom.h"
#include "DALib.h"
#include "x86math.h"  // I3DMathEngine::MatrixToQuaternion, FQuaternion, FMatrix
#include "../IVP/ivp_collision/ivp_collision.h" // IVP_Collision::get_objects (PhyCollisionStateManager)
#include "../IVP/ivp_physics/ivp_core.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_environment.h"
#include "../IVP/ivp_physics/ivp_controller.h"
#include "../IVP/ivp_physics/ivp_material.h"
#include "../IVP/ivp_physics/ivp_listener.h"
#include "../IVP/ivp_physics/ivp_mindist_settings.h"
#include "../IVP/ivp_physics/ivp_mindist.h" // IVP_Synapse_Real (PhySys_CalcClosestPointOnSynapse)
#include "common_csys.h"  // Acquire3DMathEngine
// PhySys::CalcClosestPointOnSynapse (FUN_0628b7a0, near GetClosestPhantomIntruderPos
// below) walks retail's compact half-edge mesh directly -- IVP_Compact_Edge /
// IVP_Compact_Triangle / IVP_Compact_Ledge and IVP_U_Float_Point.
#include "../IVP/ivp_compact_builder/ivp_compact_ledge.h"
#include "../IVP/ivp_compact_builder/ivp_surface_builder_pointsoup.h" // Cleanup, the box phantom's hull
#include "../IVP/ivp_compact_builder/ivp_surface_builder_ledge_soup.h" // the box phantom's surface
#include "../IVP/ivp_physics/ivp_surman_polygon.h" // PhySys::PhySurfaceManager's base
#include "../IVP/ivp_collision/ivp_ray_solver.h"   // PhySys::RaySolver_MaskedSurface's base
#include "../IVP/ivp_collision/ivp_sphere_query.h" // PhysicalSystem::FindSphereCollisions (06350740)

#include <assert.h>
#include <float.h>   // FLT_MAX (PhySys::GetClosestPhantomIntruderPos, DAT_0639c39c)
#include <math.h>
#include <new>
#include <stdarg.h>  // va_list -- CRemotePhysicsSimulation::tracef is __cdecl varargs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <mmsystem.h>  // timeGetTime (PhysicalSystem::Update, .text:0628ca84); winmm is linked by common_playeraccount.cpp


// Win32 macro conflict: PhySys::GetCurrentTime vs windows.h GetCurrentTime()
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// PhySys::PhyPerfCounter -- the IVP performance counter every PhysicalSystem hands to
// its environment (IVP_Environment_Create_Parms +0x18, FUN_0628c900 .text:0628c938..
// 0628c969). RTTI: .?AVPhyPerfCounter@PhySys@@ : IVP_PerformanceCounter (COL 063a6b68).
// vtable .rdata:0639baac, 0xc bytes.
// ============================================================================
namespace PhySys
{
    class PhyPerfCounter : public IVP_PerformanceCounter
    {
    public:
        // FUN_06285df0 (.text:06285df0..06285e00): +0x04 = 0, byte +0x08 = 0, vtable 0639baac.
        PhyPerfCounter()
            : Unread04(0)
            , Unread08(0)
        {
        }

        virtual void Slot00() override {}                // .text:062a8920 `RET` (ICF-shared)
        virtual void Slot01(int32_t /*Arg*/) override {} // .text:062ed730 `RET 4` (ICF-shared)
        virtual void Slot02() override {}                // .text:062a8920 `RET` (ICF-shared)

        // LAB_06285e30: `if (this) this->vtbl[5](1)` -- the deleting destructor.
        virtual void environment_is_going_to_be_deleted(IVP_Environment* /*Env*/) override
        {
            delete this;
        }

        virtual void Slot04(float /*Arg0*/, float /*Arg1*/) override {} // .text:0628f050 `RET 8`

        // .text:063602c0 (ICF-folded): FUN_06360290 stamps IVP_PerformanceCounter's vtable
        // 0639bac8, then operator delete when bit 0 of the flag is set.
        virtual ~PhyPerfCounter() {}

        // Never read: every slot of this class's vtable 0639baac is one of the no-ops above,
        // the deleting call or the destructor, so nothing looks at these after the ctor.
        int32_t Unread04;   // +0x04 (formerly Field_0x04), zeroed by the ctor
        uint8_t Unread08;   // +0x08 (formerly Field_0x08), zeroed by the ctor (byte store)
    };
}

namespace
{
    // .data:DAT_063fc0c8 / 063fc0cc: the multiplayer time accumulator FUN_0628ca80 keeps
    // (one double for all systems).
    double G_PhySysSimAccumulator = 0.0;

    // The IVP_Compact_Edge part id: the fourth dword of the edge's start point
    // (.text:0628c45c..0628c482 for edge A, 0628c489..0628c4af for edge B):
    // triangle = edge & ~0xf; ledge = triangle - ((tri_index & 0xfff) + 1) * 16;
    // point = ledge + ledge->c_point_offset + start_point_index * 16; part id = point+0xc.
    unsigned PhySys_PartIdOfEdge(IVP_Compact_Edge const* Edge)
    {
        IVP_U_Float_Point const* Point = Edge->GetStartPoint();
        unsigned PartId;
        memcpy(&PartId, &Point->HesseVal, sizeof(PartId));
        return PartId;
    }
}

PhysicalSystem::PhysicalSystem(unsigned InSystemId)
    : IVP_Listener_Collision(IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION) // .text:0628c223 +0x08 = 1
    , IVP_Collision_Filter_Coll_Group_Ident(0)                              // .text:0628c22a FUN_0634bcb0(0)
    , SystemId(InSystemId)                                                   // .text:0628c242
    , Environment(nullptr)                                                   // .text:0628c245
    , RefCount(0)                                                            // .text:0628c2bc
{
}

// LAB_0628c300 (.text:0628c300..0628c406). The object's client data (+0xa4) is the CObject;
// a null one is skipped, otherwise every set entry equal to it is erased (equal_range +
// erase, with the whole-tree clear fast path when the range is [begin, end)).
void PhysicalSystem::event_object_deleted(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    if (Object == nullptr)
        return;
    MovingObjects.erase(Object);
}

// LAB_0628c5b0 (.text:0628c5b0..0628c65b): a null client is skipped, otherwise a unique
// insert (lower-bound walk, FUN_0628fd80 when the key is absent).
void PhysicalSystem::event_object_revived(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    if (Object == nullptr)
        return;
    MovingObjects.insert(Object);
}

// LAB_0628c660 (.text:0628c660..0628c766): same erase as event_object_deleted but WITHOUT the
// null test -- a null client erases a null key (normally absent, so nothing happens).
void PhysicalSystem::event_object_frozen(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    MovingObjects.erase(Object);
}

// LAB_0628c410 (.text:0628c410..0628c5a0), called with this = the IVP_Listener_Collision
// sub-object (World+4).
void PhysicalSystem::event_pre_collision(IVP_Event_Collision* Event)
{
    // .text:0628c414..0628c425: FLD float d_time; FCOMP double MIN_TIME_BETWEEN_COLLISIONS;
    // TEST AH,0x41; JNZ -> return. C0 or C3 (less, equal, unordered) takes the return; the
    // fall-through is d_time > MIN_TIME_BETWEEN_COLLISIONS.
    if (!((double)(Event->d_time_since_last_collision) > PhySys::MIN_TIME_BETWEEN_COLLISIONS))
        return;

    IVP_Contact_Situation* const Situation = Event->contact_situation;
    CObject* const ObjectA = (CObject*)Situation->objects[0]->client_data; // .text:0628c42e
    CObject* const ObjectB = (CObject*)Situation->objects[1]->client_data; // .text:0628c43d

    if (ObjectA != nullptr)
    {
        if (ObjectB != nullptr)
        {
            // .text:0628c458..0628c547: build the event and append it (FUN_0628fa40 =
            // insert(_Last, 1, Event)).
            PhySys::CollisionEvent Collision;
            Collision.ObjectA = ObjectA;
            Collision.ObjectB = ObjectB;
            Collision.PartIdA = PhySys_PartIdOfEdge(Situation->compact_edges[0]);
            Collision.PartIdB = PhySys_PartIdOfEdge(Situation->compact_edges[1]);
            Collision.Point.x = Situation->contact_point_ws[0];      // R+0x20..0x28
            Collision.Point.y = Situation->contact_point_ws[1];
            Collision.Point.z = Situation->contact_point_ws[2];
            Collision.SurfaceNormal.x = Situation->surf_normal[0];      // R+0x00..0x08
            Collision.SurfaceNormal.y = Situation->surf_normal[1];
            Collision.SurfaceNormal.z = Situation->surf_normal[2];
            Collision.RelativeVelocity.x = Situation->speed[0];            // R+0x10..0x18
            Collision.RelativeVelocity.y = Situation->speed[1];
            Collision.RelativeVelocity.z = Situation->speed[2];
            Collisions.push_back(Collision);
        }

        // .text:0628c555..0628c576: an object whose class has the three low bits set
        // (the projectile classes) is taken out of collision after its first contact.
        if (((unsigned)ObjectA->ObjectClass & 7) == 7)
        {
            // .text:0628c563..0628c566: FUN_06344fb0 on ObjectA->BodyPtr (not tested).
            ObjectA->BodyPtr->disable_simulation();
            ObjectA->enable_collisions_r(false); // .text:0628c56f..0628c571
        }
    }

    // .text:0628c57a..0628c597: the same for ObjectB.
    if (ObjectB != nullptr && ((unsigned)ObjectB->ObjectClass & 7) == 7)
    {
        ObjectB->BodyPtr->disable_simulation(); // .text:0628c589..0628c58c
        ObjectB->enable_collisions_r(false); // .text:0628c595..0628c597
    }
}

// FUN_0628c770 (.text:0628c770..0628c799, this = the filter sub-object World+0xc). The base
// filter FUN_0634bc60 is called directly; when it allows the pair (== 1) and a game filter
// is installed, the game filter's answer is returned instead.
int32_t PhysicalSystem::check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B)
{
    int32_t const Result = IVP_Collision_Filter_Coll_Group_Ident::check_objects_for_collision_detection(A, B);
    if (PhySys::m_pCollisionFilter != nullptr && Result == 1)
        return (int32_t)PhySys::m_pCollisionFilter(A, B);
    return Result;
}

// LAB_0628c7a0 (.text:0628c7a0..0628c7a7): `MOV [ECX+0xc],0` with this = World+0xc, i.e.
// World+0x18 = 0. The base's self-delete is not called.
void PhysicalSystem::environment_will_be_deleted(IVP_Environment* /*Env*/)
{
    Environment = nullptr;
}


// FUN_0628cbe0 (.text:0628cbe0..0628ccea): copy an IVP body's pose into its CObject.
static void PhySys_SyncObjectFromBody(CObject* Object)
{
    // .text:0628cbe8..0628cbf5: IVP_Real_Object::calc_at_time_AT (FUN_06345710).
    IVP_U_Quat BodyQuat;
    IVP_U_Point BodyPos;
    Object->BodyPtr->calc_at_time_AT(&BodyQuat, &BodyPos);

    // .text:0628cbfa..0628cc35: position as three floats; the quaternion rotated from IVP's
    // (x, y, z, w) into the math engine's (w, x, y, z).
    Vector Position;
    Position.x = BodyPos.x;
    Position.y = BodyPos.y;
    Position.z = BodyPos.z;
    FQuaternion Quat;
    Quat.W = BodyQuat.w;
    Quat.X = BodyQuat.x;
    Quat.Y = BodyQuat.y;
    Quat.Z = BodyQuat.z;

    // .text:0628cbfe..0628cc54: DAT_063fc0b8 (lazily FUN_06391cf0) vtable+0x54.
    FMatrix Orientation;
    Acquire3DMathEngine()->QuaternionToMatrix(Orientation, Quat);

    // .text:0628cc57..0628cc7f: Object+0x2c = position, Object+0x08 = the 3x3 (REP MOVSD 9).
    static_assert(sizeof(Matrix) == sizeof(FMatrix), "EngineObject orientation is 9 floats");
    Object->WorldPosition = Position;
    memcpy(&Object->Orientation, &Orientation, sizeof(FMatrix));

    // .text:0628cc79..0628cccc: with an engine instance, read the instance extent (flags 1)
    // and write the same values back, then FUN_062883c0(instance, Object). The vector buffer
    // retail hands both calls is the position copy above, overwritten by the read.
    if (Object->EngineInstanceId != -1)
    {
        float Extent;
        DALib::Engine->GetInstanceExtent(PhySys_InstanceHandle(Object->EngineInstanceId), 1, &Extent, &Position.x);
        DALib::Engine->SetInstanceExtent(PhySys_InstanceHandle(Object->EngineInstanceId), 1, Extent, &Position.x);
        // .text:0628ccc2..0628ccc7: FUN_062883c0(Object->EngineInstanceId, Object).
        PhySys_UpdateJointedChildren(Object->EngineInstanceId, Object);
    }

    // .text:0628cccf..0628cce0: CSimple-derived classes ((class & 3) == 3) re-cache their
    // physical properties through vtable slot +0x84 (CSimple::cache_physical_props).
    if (((unsigned)Object->ObjectClass & 3) == 3)
        ((CSimple*)Object)->cache_physical_props();
}

// FUN_0628c900 (.text:0628c900..0628ca70).
void PhysicalSystem::AddRef()
{
    // .text:0628c91b..0628c926: post-increment; the environment is built only on 0 -> 1.
    int const PreviousRefCount = RefCount;
    RefCount = PreviousRefCount + 1;
    if (PreviousRefCount != 0)
        return;

    // .text:0628c92c..0628c969: default create parms (FUN_06350610), the collision filter is
    // this object's filter sub-object (World+0xc), the performance counter a new PhyPerfCounter.
    IVP_Environment_Create_Parms Parms;
    Parms.CollisionFilter = this;                              // implicit upcast: this + 0x0c
    Parms.PerformanceCounter = new PhySys::PhyPerfCounter();

    // .text:0628c96d..0628c98e: IVP_Environment_Manager::get_environment_manager() (FUN_0634cc70)
    // ->create_environment(&Parms, "digitalanvil", 0x11c2de34) (FUN_0634cba0).
    Environment = IVP_Environment_Manager::get_environment_manager()->create_environment(
        &Parms, "digitalanvil", 0x11c2de34);

    // .text:0628c988..0628c9aa: PSI time step = 1.0f / PHYSICAL_SIM_RATE (float FDIV); IVP's
    // copy of MAX_SPAWNED_MINDIST_COUNT (DAT_0658ab3c = settings +0x12c) is written between
    // the division and the call.
    float const PsiTime = 1.0f / PhySys::PHYSICAL_SIM_RATE;
    G_IVP_MindistSettings.max_spawned_mindist_count = PhySys::MAX_SPAWNED_MINDIST_COUNT;
    IVP_Environment_SetDeltaPsiTime(Environment, PsiTime);

    // .text:0628c9af..0628ca0c: the range manager's (environment +0x1c) look-ahead values
    // (plain dword copies of the PhySys constants 063eb340..063eb35c / 063fc0f4 / 063fc0f8).
    IVP_Range_Manager* const RangeManager = Environment->range_manager;
    RangeManager->look_ahead_time_world = PhySys::RMGR_LOOK_AHEAD_TIME_WORLD;                 // +0x20
    RangeManager->look_ahead_max_radius_world = PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD;     // +0x24
    RangeManager->look_ahead_min_distance_world = PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD; // +0x28
    RangeManager->look_ahead_max_distance_world = PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD; // +0x2c
    RangeManager->look_ahead_min_seconds_world = PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD;   // +0x30
    RangeManager->look_ahead_time_intra = PhySys::RMGR_LOOK_AHEAD_TIME_INTRA;                 // +0x0c
    RangeManager->look_ahead_max_radius_intra = PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA;     // +0x10
    RangeManager->look_ahead_min_distance_intra = PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA; // +0x14
    RangeManager->look_ahead_max_distance_intra = PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA; // +0x18
    RangeManager->look_ahead_min_seconds_intra = PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA;   // +0x1c

    // .text:0628ca0f..0628ca2f: zero gravity -- an IVP_U_Point on the stack with x, y, z = 0
    // (the fourth float is not written) handed to FUN_0634cb20 (set_gravity).
    IVP_U_Point Gravity;
    Gravity.set(0.0f, 0.0f, 0.0f);
    IVP_Environment_SetGravity(Environment, &Gravity);

    // .text:0628ca34..0628ca44: register as object listener (FUN_0634ce60) and as collision
    // listener (FUN_0634d050, the IVP_Listener_Collision sub-object World+4).
    // The IVP helpers take void*, so the base sub-objects are formed first (implicit upcasts:
    // IVP_Listener_Object at +0x00, IVP_Listener_Collision at +0x04).
    IVP_Listener_Object* const ObjectListener = this;
    IVP_Listener_Collision* const CollisionListener = this;
    IVP_Environment_Append_Object_Listener(Environment, ObjectListener);
    IVP_Environment_Append_Listener_C8(Environment, CollisionListener);

    // .text:0628ca49..0628ca5e: anomaly limits (environment +0x24).
    Environment->anomaly_limits->MaxVelocity = PhySys::ANOM_LIMITS_MAX_VELOCITY;
    Environment->anomaly_limits->MaxAngularVelocityPerPsi = PhySys::ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI;
}

// FUN_0628ca80 (.text:0628ca80..0628cbd2, thiscall RET 4).
void PhysicalSystem::Update(float Dt)
{
    // .text:0628ca84: the result is never used (EAX is overwritten at 0628ca8a).
    timeGetTime();

    // .text:0628ca8a..0628ca95: erase(_First, _Last) (FUN_0628f000).
    Collisions.clear();

    if (SinglePlayer())
    {
        // .text:0628cb7e..0628cb9d: FCOMP double 0.0999; TEST AH,0x41; JNZ keeps Dt (less,
        // equal, unordered); the fall-through (Dt > 0.0999) stores (float)0.0999.
        if ((double)(Dt) > 0.0999)
            Dt = (float)(0.0999);
        // .text:0628cb95..0628cb9d: FUN_0634cc80 (simulate_dtime).
        IVP_Environment_SimulateDTime(Environment, Dt);
    }
    else
    {
        // .text:0628caab..0628cac2: acc += Dt (double, FST); only acc > 0.0051 steps.
        G_PhySysSimAccumulator += (double)(Dt);
        if (G_PhySysSimAccumulator > 0.0051)
        {
            // .text:0628cac8..0628cb41: whole 0.0999 steps; after each the moving objects are
            // synced from their bodies.
            while (G_PhySysSimAccumulator > 0.0999)
            {
                // .text:0628cae0..0628caed: FUN_0634cc80 with (float)0.0999.
                IVP_Environment_SimulateDTime(Environment, (float)(0.0999));
                G_PhySysSimAccumulator -= 0.0999;
                FPhysicsTickScope SyncProfile(EPhysicsTickSection::SyncObjects);
                for (std::set<CObject*>::iterator It = MovingObjects.begin(); It != MovingObjects.end(); ++It)
                    PhySys_SyncObjectFromBody(*It);
            }
            // .text:0628cb43..0628cb63: the remainder when above (double)1e-5f (DAT_0639c3f0).
            if (G_PhySysSimAccumulator > (double)(1.0e-5f))
            {
                // .text:0628cb56..0628cb63: FUN_0634cc80 with (float)acc.
                IVP_Environment_SimulateDTime(Environment, (float)(G_PhySysSimAccumulator));
            }
            // .text:0628cb68..0628cb72
            G_PhySysSimAccumulator = 0.0;
        }
    }

    // .text:0628cba2..0628cbce: sync every moving object.
    FPhysicsTickScope SyncProfile(EPhysicsTickSection::SyncObjects);
    for (std::set<CObject*>::iterator It = MovingObjects.begin(); It != MovingObjects.end(); ++It)
        PhySys_SyncObjectFromBody(*It);
}

// FUN_0628ccf0 (.text:0628ccf0..0628d19c, thiscall RET 0xc). The beam's ray through this
// system's environment: the nearest hit whose object is not in the beam's collision group and
// has a client object moves the beam to the hit point and becomes one CollisionEvent.
void PhysicalSystem::GenerateCollisions(CBeam* Beam, Vector const& Direction, float Length)
{
    // .text:0628cd1c..0628cd60: the start is LastPos (+0x110) when the beam moved (+0x11c),
    // else its WorldPosition (+0x2c) (dword copies).
    Vector const& Source = (Beam->bMoved != 0) ? Beam->LastPos : Beam->WorldPosition;
    Vector Start = Source;

    // .text:0628cd36..0628cdd3: when BULLET_LENGTH_FUDGE * 0.5f < Length (FCOMP / TEST AH,0x5 /
    // JP skips on greater, equal or unordered): Length += fudge (FSTP float), and the start is
    // pulled back by (Direction * fudge) * 0.5f (FUN_06288830 twice, FUN_06288800).
    const float Fudge = PhySys::BULLET_LENGTH_FUDGE;
    if ((double)(Fudge) * 0.5f < Length)
    {
        Length = (float)((double)(Fudge) + Length);
        Vector Scaled;
        Scaled.x = (float)((double)(Fudge) * Direction.x);
        Scaled.y = (float)((double)(Fudge) * Direction.y);
        Scaled.z = (float)((double)(Fudge) * Direction.z);
        Vector Half;
        Half.x = (float)(0.5 * Scaled.x);
        Half.y = (float)(0.5 * Scaled.y);
        Half.z = (float)(0.5 * Scaled.z);
        Start.x = (float)((double)(Start.x) - Half.x);
        Start.y = (float)((double)(Start.y) - Half.y);
        Start.z = (float)((double)(Start.z) - Half.z);
    }

    // .text:0628cdd7..0628ce6c: the template -- the start (FLD / FSTP float), the direction as
    // given (FUN_0636cf40, not normalised here), Length, flags 0. UB in retail: both fourth
    // floats are a never-written stack slot; the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(Start.x, Start.y, Start.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(Direction.x, Direction.y, Direction.z);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = Length;
    Template.ray_flags = 0;

    // .text:0628ce5d..0628cecf: the solver (FUN_06349a60, FUN_0634d210(8), the hit array's
    // `eh vector constructor iterator` with the no-op Csys::Csys, vptr 0639c2c4) against
    // everything in the simulation (FUN_0634a990 on +0x18).
    IVP_Ray_Solver_Min_Hash Solver(&Template);
    Solver.check_ray_against_all_objects_in_sim(Environment);

    // .text:0628ced4..0628cedb
    if (Solver.min_hash.counter <= 0)
        return;

    for (;;)
    {
        // .text:0628cee1..0628cef8
        IVP_Ray_Hit* const Hit = (IVP_Ray_Hit*)Solver.min_hash.find_min_elem();
        if (Hit == nullptr)
            return;
        IVP_Real_Object* const HitObject = Hit->hit_real_object;

        // .text:0628cefe..0628cf69: a hit in the beam's own collision group (both names
        // non-empty and equal, the inlined strcmp), or one without a client object, is dropped
        // (FUN_0634d4f0 remove_min) and the next one is taken.
        bool bDrop = false;
        if (HitObject->name[0] != 0 && Beam->CollisionGroup[0] != 0 &&
            strcmp(HitObject->name, Beam->CollisionGroup) == 0)
            bDrop = true;
        else if (HitObject->client_data == nullptr)
            bDrop = true;
        if (bDrop)
        {
            Solver.min_hash.remove_min();
            continue;
        }

        // .text:0628cf6e..0628cfd8: the event's two objects and part ids; the distance is the
        // min hash element's key (stadium[1]->value).
        CObject* const Client = (CObject*)HitObject->client_data;
        const float Distance = Solver.min_hash.stadium[1]->value;
        PhySys::CollisionEvent Event;
        Event.ObjectA = Client;
        Event.ObjectB = Beam;
        Event.PartIdA = (Hit->hit_compact_ledge != nullptr) ? (unsigned)Hit->hit_compact_ledge->ClientData
                                                            : 0;
        Event.PartIdB = 0;

        // .text:0628cf9d..0628d01c: the hit point Start + Direction * Distance (FUN_06288830,
        // FUN_062887d0) becomes the beam's WorldPosition and the event's point.
        Vector Step;
        Step.x = (float)((double)(Distance) * Template.ray_normized_direction.x);
        Step.y = (float)((double)(Distance) * Template.ray_normized_direction.y);
        Step.z = (float)((double)(Distance) * Template.ray_normized_direction.z);
        Vector HitPoint;
        HitPoint.x = (float)((double)(Start.x) + Step.x);
        HitPoint.y = (float)((double)(Start.y) + Step.y);
        HitPoint.z = (float)((double)(Start.z) + Step.z);
        Beam->WorldPosition = HitPoint;
        Event.Point = HitPoint;

        // .text:0628d010..0628d06e: the hit object's cache slot (lazily claimed by
        // FUN_0634d530, referenced, refreshed by FUN_0634d640 when stale -- the inline
        // ResolveNarrowPhaseCacheSlot), the object-space hit normal rotated to world space
        // (FUN_0634d9d0), the reference dropped.
        IVP_NarrowPhaseCacheSlot* const Slot = HitObject->ResolveNarrowPhaseCacheSlot();
        IVP_U_Point NormalWs;
        IVP_RotateSlotToWorldSpace(Slot, &Hit->hit_surface_direction_os, &NormalWs);
        HitObject->ReleaseNarrowPhaseCacheSlot(Slot);
        Event.SurfaceNormal.x = NormalWs.x;
        Event.SurfaceNormal.y = NormalWs.y;
        Event.SurfaceNormal.z = NormalWs.z;

        // .text:0628d071..0628d0dc: the client body's surface speed at the hit point
        // (FUN_06346290 on Client->Body->core). UB in retail: the point's fourth float is never
        // written; the port stores 0.
        IVP_U_Point HitPointWs;
        HitPointWs.set(HitPoint.x, HitPoint.y, HitPoint.z);
        HitPointWs.hesse = 0.0f;
        IVP_U_Point SurfaceSpeed;
        Client->BodyPtr->core->get_surface_speed(&HitPointWs, &SurfaceSpeed);

        // .text:0628d0e1..0628d153: the beam's velocity (vtable +0x54) minus that speed, per
        // component (FSUB float, FSTP float).
        const Vector BeamVelocity = Beam->get_velocity();
        Event.RelativeVelocity.x = (float)((double)(BeamVelocity.x) - SurfaceSpeed.x);
        Event.RelativeVelocity.y = (float)((double)(BeamVelocity.y) - SurfaceSpeed.y);
        Event.RelativeVelocity.z = (float)((double)(BeamVelocity.z) - SurfaceSpeed.z);

        // .text:0628d14b..0628d15d: FUN_0628fa40(_Last, 1, &Event) -- appended to +0x1c. Only
        // the first accepted hit is used.
        Collisions.push_back(Event);
        return;
    }
}

namespace CmnPhySys
{
    // The pose conversion FUN_0628c7b0 / FUN_0628c860 share: the math engine (DAT_063fc0b8,
    // lazily FUN_06391cf0) slot +0x50 MatrixToQuaternion, then the (W, X, Y, Z) result is
    // stored as IVP's (x, y, z, w) (.text:0628c7f2..0628c817 / 0628c8a1..0628c8c6).
    void PhySys_OrientationToIvpQuat(Matrix const& Orientation, IVP_U_Quat& Out)
    {
        static_assert(sizeof(Matrix) == sizeof(FMatrix), "EngineObject orientation is 9 floats");
        FQuaternion Quat;
        Acquire3DMathEngine()->MatrixToQuaternion(Quat, (FMatrix const&)Orientation);
        Out.x = Quat.X;
        Out.y = Quat.Y;
        Out.z = Quat.Z;
        Out.w = Quat.W;
    }
}

// FUN_0628c7b0 (.text:0628c7b0..0628c851).
void PhysicalSystem::CreatePolygonBody(CObject* Object, IVP_RealObjectConstructionTemplate* Template)
{
    // .text:0628c7bf..0628c7d3: the position (three float copies; the fourth float of the
    // IVP_U_Point is not written).
    IVP_U_Point Position;
    Position.set(Object->WorldPosition.x, Object->WorldPosition.y, Object->WorldPosition.z);

    // .text:0628c7b3..0628c817: Object->Orientation (+0x08) as an IVP quaternion.
    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Object->Orientation, Rotation);

    // .text:0628c80a..0628c823: a template without a material gets the PhySys material.
    if (Template->Material == nullptr)
        Template->Material = G_PhySysMaterial;

    // .text:0628c826..0628c838: FUN_0634cd80(this->Environment, Object+0x5c, Template,
    // &Rotation, &Position). Object+0x5c is the surface manager BuildPartIVP_Real
    // (FUN_06289100) stores there.
    IVP_Polygon* const Body = IVP_Environment_CreatePolygon(Environment, Object->SurfaceManager, Template,
                                                            &Rotation, &Position);

    // .text:0628c83d..0628c847
    Object->BodyPtr = Body;
    Object->PhysicsSystemId = SystemId;
    Body->client_data = Object;
}

// FUN_0628c860 (.text:0628c860..0628c8f9).
IVP_Ball* PhysicalSystem::CreateBallBody(Vector const& Position, Matrix const& Orientation, float Radius,
                                         IVP_RealObjectConstructionTemplate* Template)
{
    // .text:0628c863..0628c881: the position (three float copies).
    IVP_U_Point BallPosition;
    BallPosition.set(Position.x, Position.y, Position.z);

    // .text:0628c87a..0628c8c6
    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Orientation, Rotation);

    // .text:0628c8b9..0628c8d2
    if (Template->Material == nullptr)
        Template->Material = G_PhySysMaterial;

    // .text:0628c8d5..0628c8f0: the radius is copied into the first argument slot and its
    // address passed: FUN_0634cdf0(this->Environment, &Radius, Template, &Rotation, &Position).
    float BallRadius = Radius;
    return IVP_Environment_CreateBall(Environment, &BallRadius, Template, &Rotation, &BallPosition);
}

// ============================================================================
// PhySys::Startup / Shutdown
// ============================================================================

void __cdecl PhySys::Startup(unsigned SystemId)
{
    // Common.dll.c 0x29510: material singleton once, std::map lookup by system id,
    // create world on miss, bump first-use counter.
    // .text:0628951e..0628957c: `if (DAT_063fc0bc == 0) DAT_063fc0bc = new(0x1c)
    // IVP_Material_Simple((float)MATERIAL_FRICTION, (float)MATERIAL_ELASTICITY)` -- both
    // constants are doubles narrowed by FSTP float.
    if (G_PhySysMaterial == nullptr)
    {
        G_PhySysMaterial = new IVP_Material_Simple((float)(MATERIAL_FRICTION),
                                                   (float)(MATERIAL_ELASTICITY));
    }

    // .text:06289581..062895c4: DAT_063fc098 lower_bound + `key < node key` test = find.
    std::map<unsigned, PhysicalSystem>::iterator It = G_PhysicalSystems.find(SystemId);
    if (It == G_PhysicalSystems.end())
    {
        // .text:062895ca..0628970e: build PhysicalSystem(SystemId) (FUN_0628c1f0) on the
        // stack, copy it into a pair (FUN_0628ebb0), insert the pair (FUN_0628f110), destroy
        // both temporaries. The copies are of a freshly built, empty object; the port builds
        // the mapped value in place (it is registered with IVP by address later, in AddRef).
        It = G_PhysicalSystems.emplace(std::piecewise_construct, std::forward_as_tuple(SystemId),
                                       std::forward_as_tuple(SystemId)).first;
    }

    // .text:0628970e..06289711: FUN_0628c900 on the mapped value (node + 0x10).
    It->second.AddRef();
}

void __cdecl PhySys::Shutdown(unsigned SystemId)
{
    // .text:06289910..06289950: find.
    std::map<unsigned, PhysicalSystem>::iterator It = G_PhysicalSystems.find(SystemId);
    if (It != G_PhysicalSystems.end())
    {
        PhysicalSystem& System = It->second;
        // .text:06289952..06289976: `if (--RefCount == 0)` delete the environment
        // (FUN_0634c6f0 then operator delete) and clear the pointer.
        if (--System.RefCount == 0)
        {
            IVP_Environment* const Env = System.Environment;
            if (Env != nullptr)
            {
                IVP_Environment_Destructor(Env);
                ::operator delete(Env);
            }
            System.Environment = nullptr;
        }
        // .text:06289977..06289989: erase the node (FUN_0628f320) once RefCount is 0.
        if (System.RefCount == 0)
            G_PhysicalSystems.erase(It);
    }

    // .text:0628998e..06289996: the material goes when the map (size DAT_063fc0a8) is empty --
    // tested whether or not SystemId was found.
    if (G_PhysicalSystems.empty())
    {
        // .text:06289998..062899b6: `if (DAT_063fc0bc) DAT_063fc0bc->vtbl[5](1)` (the
        // deleting destructor), `DAT_063fc0bc = 0`, then tail-JMP FUN_0636c7e0.
        if (G_PhySysMaterial != nullptr)
            delete G_PhySysMaterial;
        G_PhySysMaterial = nullptr;
        // .text:062899b6: tail JMP FUN_0636c7e0 -- free IVP's cached point-soup compact
        // surface (DAT_0658b008) and clear it.
        IVP_SurfaceBuilder_Pointsoup::Cleanup();
    }
}

// ============================================================================
// PhySys::Update
// ============================================================================

void __cdecl PhySys::Update(unsigned SystemId, float Dt)
{
    // .text:062899c0..06289a1a: lower_bound + key test, then FUN_0628ca80 on node + 0x10 in
    // both branches -- a missing SystemId updates the map head node's unconstructed storage
    // (PhySys_FindSystem: very strange, reproduced on purpose).
    PhySys_FindSystem(SystemId).Update(Dt);
}

// ============================================================================
// Queries: ray / sphere queries against the system's IVP environment
// (FUN_0628d1a0, FUN_06289c90, FUN_0628d450, FUN_0628ccf0).
// ============================================================================

// FUN_0628d1a0 (.text:0628d1a0..0628d444, thiscall RET 0x10). The ray From -> To against every
// object of this system's environment, nearest hit first, at most MaxHits; hits without a
// client object are skipped. Returns the number written.
int PhysicalSystem::FindRayCollisions(Vector const& From, Vector const& To, PhySys::RayHit* Hits, int MaxHits)
{
    // .text:0628d1c6..0628d1e0: Delta = To - From (FUN_06288800, FSUB float -> FSTP float).
    const float Dx = (float)((double)(To.x) - From.x);
    const float Dy = (float)((double)(To.y) - From.y);
    const float Dz = (float)((double)(To.z) - From.z);

    // .text:0628d1e5..0628d247: Length = sqrt((dy*dy + dz*dz) + dx*dx) on the stack; the
    // direction is Delta * (1.0f / Length), each FSTP float; Length is stored as a float.
    const double Length = sqrt(((double)(Dy) * Dy + (double)(Dz) * Dz) +
                               (double)(Dx) * Dx);
    const double InvLength = 1.0 / Length;
    const float DirX = (float)(Dx * InvLength);
    const float DirY = (float)(Dy * InvLength);
    const float DirZ = (float)(Dz * InvLength);

    // .text:0628d1ed..0628d2a9: the ray template. UB in retail: the fourth float of the start
    // point and of the direction is a never-written stack slot; the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(From.x, From.y, From.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(DirX, DirY, DirZ);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = (float)(Length);
    Template.ray_flags = 0;

    // .text:0628d2a1..0628d2e5: the solver (FUN_06349a60, min hash FUN_0634d210(8), vptr
    // 0639c2c4) against everything in the simulation (FUN_0634a990 on this+0x18).
    IVP_Ray_Solver_Min_Hash Solver(&Template);
    Solver.check_ray_against_all_objects_in_sim(Environment);

    // .text:0628d2ea..0628d406
    int Count = 0;
    PhySys::RayHit* Out = Hits;
    for (;;)
    {
        IVP_Ray_Hit* const Hit = (IVP_Ray_Hit*)Solver.min_hash.find_min_elem();
        if (Hit == nullptr || Count >= MaxHits)
            break;

        CObject* const Client = (CObject*)Hit->hit_real_object->client_data;
        if (Client != nullptr)
        {
            Out->Object = Client;

            // .text:0628d332..0628d384: V = distance * direction (FMUL, FSTP float each,
            // FUN_0628b030), Point = From + V (FUN_062887d0).
            const double Distance = Hit->hit_distance;
            const float Vx = (float)(DirX * Distance);
            const float Vy = (float)(DirY * Distance);
            const float Vz = (float)(DirZ * Distance);
            Out->Point.x = (float)((double)(From.x) + Vx);
            Out->Point.y = (float)((double)(From.y) + Vy);
            Out->Point.z = (float)((double)(From.z) + Vz);

            // .text:0628d387..0628d3d7: the hit normal (object space, FLD / FSTP float copies)
            // through the client's orientation (+0x08): TransformMatrix (+0x2c).
            FVector NormalOs;
            NormalOs.X = Hit->hit_surface_direction_os.x;
            NormalOs.Y = Hit->hit_surface_direction_os.y;
            NormalOs.Z = Hit->hit_surface_direction_os.z;
            FVector NormalWs;
            Acquire3DMathEngine()->TransformMatrix(NormalWs, (FMatrix const&)Client->Orientation, NormalOs);
            Out->Normal.x = NormalWs.X;
            Out->Normal.y = NormalWs.Y;
            Out->Normal.z = NormalWs.Z;

            // .text:0628d3da..0628d3e8: the hit ledge's client data (its part id), or 0.
            Out->PartId = (Hit->hit_compact_ledge != nullptr)
                              ? (unsigned)Hit->hit_compact_ledge->ClientData
                              : 0;
            ++Count;
            ++Out;
        }

        // .text:0628d3f5..0628d3fd
        Solver.min_hash.remove(Hit);
    }

    // .text:0628d40b..0628d425: ~IVP_Ray_Solver_Min_Hash.
    return Count;
}

// FUN_0628d450 (.text:0628d450..0628d55e, thiscall RET 0x10).
//   0628d46b..0628d4b6  the six-dword record at local_248: Center (float copies), an
//                       uninitialised dword (local_24c, never read by the callee), Radius, 4
//   0628d4be..0628d4c9  the query object at local_230: Found = {memsize 0x80, n_elems 0,
//                       elems = the inline buffer local_228}
//   0628d4d0..0628d4e8  Query.collect_objects(&Record, this->Environment (+0x18)) -- EH state 0
//                       from 0628d4dd, so the query is a RAII local
//   0628d4ed..0628d523  for i < n_elems while Count < MaxObjects (JGE -> stop): client =
//                       Found[i]->client_data (+0xa4); a non-null client goes to
//                       OutObjects[Count++]
//   0628d525..0628d542  the vector's own free of a grown buffer (not the inline one, not null)
// Returns Count. Nothing else is tested.
int PhysicalSystem::FindSphereCollisions(Vector const& Center, float Radius, CObject** OutObjects, int MaxObjects)
{
    FIVP_Sphere_Query_Record Record;
    Record.Center[0] = Center.x;
    Record.Center[1] = Center.y;
    Record.Center[2] = Center.z;
    Record.Unwritten_0x0c = 0.0f;  // UB in retail: an uninitialised stack dword; never read
    Record.Radius = Radius;
    Record.MaxLedgeTreeDepth = 4;

    IVP_Sphere_Object_Query Query;
    Query.collect_objects(&Record, Environment);

    int Count = 0;
    for (int i = 0; i < (int)Query.Found.n_elems; ++i)
    {
        if (Count >= MaxObjects)
        {
            break;
        }
        CObject* const Client = (CObject*)Query.Found.elems[i]->client_data;
        if (Client != nullptr)
        {
            OutObjects[Count] = Client;
            ++Count;
        }
    }
    return Count;
}

