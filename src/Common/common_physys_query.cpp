// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_physys.h"

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
#include "../IVP/ivp_utility/ivp_core_trap.h"      // IVP_CoreTrap -- retail's `MOV [0],0` (06284308)

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
// Mass / Inertia
// ============================================================================

// .text:062841c0..06284216. No pointer is tested. The core's inertia (and mass dword) is
// copied first; FUN_06344d30 (IVP_Real_Object change_mass -> IVP_Core::set_mass 06347c10)
// scales it by NewMass / Mass; then FUN_06346bb0 on the re-read core writes the SAVED
// pre-scale inertia back -- net effect: only the mass changes.
void __cdecl PhySys::SetMass(CObject* Object, float Mass)
{
    IVP_Core* const Core = Object->BodyPtr->core;    // .text:062841c8..062841cb
    Vector const SavedInertia = Core->RotInertia;                                  // .text:062841d1..062841e4
    // .text:062841f3..062841fa: FUN_06344d30 on the body (ECX still [ESI+0x54]).
    Object->BodyPtr->change_mass(Mass);
    // .text:062841ff..0628420d
    Object->BodyPtr->core->set_rotation_inertia(&SavedInertia);
}

// .text:06284220..06284230: Object->Body->core->Mass, nothing tested.
float __cdecl PhySys::GetMass(CObject const* Object)
{
    return Object->BodyPtr->core->Mass;
}

// .text:06284240..06284282: a float copy of Inertia (plus the core's mass in the unused
// fourth slot), then FUN_06346bb0 on Object->Body->core. Nothing tested.
void __cdecl PhySys::SetMomentOfInertia(CObject* Object, Vector const& Inertia)
{
    Vector const Copy = Inertia;                                                     // .text:06284247..06284263
    Object->BodyPtr->core->set_rotation_inertia(&Copy); // .text:06284274..0628427a
}

// .text:06284290..062842df: Object->Body->core->RotInertia (+0x14..+0x1c), nothing tested.
Vector __cdecl PhySys::GetMomentOfInertia(CObject const* Object)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    Vector Result;
    Result.x = Core->RotInertia.x;
    Result.y = Core->RotInertia.y;
    Result.z = Core->RotInertia.z;
    return Result;
}

// ============================================================================
// Damping
// ============================================================================

// .text:06285570..062855b9: OutLinear = core->SpeedDamp (+0x44), OutAngular =
// core->RotSpeedDamp (+0x24..+0x2c), nothing tested.
void __cdecl PhySys::GetDamping(CObject const* Object, float& OutLinear, Vector& OutAngular)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    OutLinear = Core->SpeedDamp;
    OutAngular.x = Core->RotSpeedDamp.x;
    OutAngular.y = Core->RotSpeedDamp.y;
    OutAngular.z = Core->RotSpeedDamp.z;
}

// .text:062855c0..06285610: core->SpeedDamp = Linear; the angular triple is staged in a
// stack IVP_U_Float_Point and copied as four dwords into +0x24..+0x30. UB in retail: the
// fourth (+0x30) is the staging point's never-written slot; the port stores 0.
void __cdecl PhySys::SetDamping(CObject* Object, float Linear, Vector const& Angular)
{
    IVP_Core* const Core = Object->BodyPtr->core;
    Core->SpeedDamp = Linear;                 // .text:062855d8
    Core->RotSpeedDamp.x = Angular.x;         // .text:062855f6
    Core->RotSpeedDamp.y = Angular.y;         // .text:06285600
    Core->RotSpeedDamp.z = Angular.z;         // .text:06285607
    Core->CenterOfMassX = 0.0f;               // .text:0628560a (UB: local_4)
}

// ============================================================================
// Velocity / Angular velocity
// ============================================================================

Vector __cdecl PhySys::GetVelocity(CObject const* Object)
{
    // .text:062857f0-062858ba. Object is not null-tested (.text:062857fb reads it
    // unconditionally), and neither is the body's core (.text:0628584c).
    Vector Result;
    Result.x = 0.0f;
    Result.y = 0.0f;
    Result.z = 0.0f;

    if (!SinglePlayer())                                                   // .text:062857f4
    {
        // .text:06285801-06285821: __RTDynamicCast(Object+0x60, 0, IVP_Controller,
        // CRemotePhysicsController, 0); on a hit the velocity is the controller's +0x348
        // (.text:06285823), its CRemotePhysicsSimulation base's +0x278. A controller of any
        // other class (a FLControllerGolem) falls through to the core, as does null.
        // Was a raw +0x348 read of whatever sat at +0x60 before 2026-09-24.
        IVP_Controller* const Rc = Object->GolemController;
        if (Rc != nullptr)
        {
            CRemotePhysicsController* const Remote = dynamic_cast<CRemotePhysicsController*>(Rc);
            if (Remote != nullptr)
                return Remote->m_Velocity;                                 // this+0x348
        }
    }

    // .text:06285845-06285882: IVP body -> core -> Speed (+0xa4..+0xac); a null body gives
    // zero (.text:06285884).
    IVP_Real_Object const* const Body = Object->BodyPtr;
    if (Body != nullptr)
    {
        Result.x = Body->core->Speed.x;
        Result.y = Body->core->Speed.y;
        Result.z = Body->core->Speed.z;
    }
    return Result;
}

void __cdecl PhySys::AddToVelocity(CObject* Object, Vector const& Delta)
{
    // .text:06285740..0628576f: Delta float-copied into local_10 (fourth dword unwritten), then
    // FUN_06346080 on Object->Body (+0x54): EnsureInSimulation, core->SpeedChange += copy.
    // Nothing is tested.
    IVP_U_Point SpeedWs;                                // local_10
    SpeedWs.set(Delta.x, Delta.y, Delta.z);
    Object->BodyPtr->async_add_speed_object_ws(&SpeedWs);
}

// .text:062857a0..062857ed: Object->Body->core->RotSpeed (+0x94..+0x9c), nothing tested.
Vector __cdecl PhySys::GetAngularVelocityOS(CObject const* Object)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    Vector Result;
    Result.x = Core->RotSpeed.x;
    Result.y = Core->RotSpeed.y;
    Result.z = Core->RotSpeed.z;
    return Result;
}

void __cdecl PhySys::AddToAngularVelocityOS(CObject* Object, Vector const& Delta)
{
    // .text:06285770..0628579f: the same shape as AddToVelocity, into FUN_063460c0
    // (EnsureInSimulation, core->RotSpeedChange += copy). Nothing is tested.
    IVP_U_Point RotSpeedCs;                             // local_10
    RotSpeedCs.set(Delta.x, Delta.y, Delta.z);
    Object->BodyPtr->async_add_rot_speed_object_cs(&RotSpeedCs);
}

// ============================================================================
// Impulses
// ============================================================================

void __cdecl PhySys::AddImpulseAtPoint(CObject* Object, Vector const& Impulse, Vector const& Point)
{
    // .text:06285630..0628567c. Impulse is float-copied into local_20 (06285637..06285653) and
    // Point into local_10 (06285657..06285670), the fourth dword of each left unwritten; then
    // FUN_06346100 on Object->Body (+0x54) with (&PointCopy, &ImpulseCopy) -- PUSH EAX
    // (&local_20) goes first, so the last push, &local_10, is the first argument. Nothing is
    // tested.
    IVP_U_Point ImpulseWs;                              // local_20
    ImpulseWs.set(Impulse.x, Impulse.y, Impulse.z);
    IVP_U_Point PointWs;                                // local_10
    PointWs.set(Point.x, Point.y, Point.z);
    Object->BodyPtr->async_push_object_ws(&PointWs, &ImpulseWs);
}

// .text:06285680..062856ba: a float copy of Direction, then FUN_06346a00 on
// Object->Body->core (IVP_Core::push_core_linear). Nothing tested.
void __cdecl PhySys::LinearImpulse(CObject* Object, Vector const& Direction, float Magnitude)
{
    Vector const Copy = Direction;
    Object->BodyPtr->core->push_core_linear(&Copy, Magnitude);
}

// .text:062856c0..062856fa: the same through FUN_06346c80 (IVP_Core::push_core_angular_ws).
void __cdecl PhySys::AngularImpulse(CObject* Object, Vector const& Axis, float Magnitude)
{
    Vector const Copy = Axis;
    Object->BodyPtr->core->push_core_angular_ws(&Copy, Magnitude);
}

// .text:06285700..0628573a: the same through FUN_06346d80 (IVP_Core::push_core_angular_cs).
void __cdecl PhySys::AngularImpulseOS(CObject* Object, Vector const& Axis, float Magnitude)
{
    Vector const Copy = Axis;
    Object->BodyPtr->core->push_core_angular_cs(&Copy, Magnitude);
}

// ============================================================================
// Activation
// ============================================================================

void __cdecl PhySys::Wakeup(CObject* Object)
{
    // .text:06285620..06285627: ECX = Object->BodyPtr; JMP FUN_06346170
    // (IVP_Real_Object::ensure_in_simulation). Neither pointer is tested.
    Object->BodyPtr->EnsureInSimulation();
}

// ============================================================================
// Collision state
// ============================================================================

// .text:062858c0..062858d8: a body -> the manager's desired state (manager not tested),
// otherwise false.
bool __cdecl PhySys::GetCollisionState(CObject const* Object)
{
    if (Object->BodyPtr == nullptr)
        return false;
    return Object->CollisionStateMgr->get_desired_collision_state();
}

// .text:062858e0..062858fd: a body -> the manager's enable_collisions(bEnable, true).
void __cdecl PhySys::SetCollisionState(CObject* Object, bool bEnable)
{
    if (Object->BodyPtr == nullptr)
        return;
    Object->CollisionStateMgr->enable_collisions(bEnable, true);
}

// .text:06284e10..06284e1c: Object->Body->name (+0x84), nothing tested.
char const* __cdecl PhySys::GetCollisionGroup(CObject const* Object)
{
    return Object->BodyPtr->name;
}

// .text:06284e20..06284e42: safe_strcpy(Body->name, 8, Group), then a tail jump into
// FUN_06345530 on the body (IVP_Real_Object: with an OV element (+0x90), the mindist
// manager's RecheckOvElement (0634ebd0) on it). Nothing tested.
void __cdecl PhySys::SetCollisionGroup(CObject* Object, char const* const Group)
{
    IVP_Real_Object* const Body = Object->BodyPtr;
    safe_strcpy(Body->name, 8, Group);
    // .text:06284e3b..06284e42: ECX re-read from [ESI+0x54]; tail JMP FUN_06345530.
    Object->BodyPtr->recheck_collision_filter();
}

// ============================================================================
// Center of mass / Radius
// ============================================================================

// .text:06284570..06284686. No null tests.
Vector __cdecl PhySys::GetCenterOfMass(CObject const* Object)
{
    // .text:06284574..06284580: the body's world matrix now (FUN_063456f0).
    IVP_U_Matrix World;
    Object->BodyPtr->get_m_world_f_object_AT(&World);

    Vector Result;
    IVP_SurfaceManager* const Manager = Object->SurfaceManager;
    if (Manager == nullptr)
    {
        // .text:06284647..06284686: the body origin.
        Result.x = World.vv.k[0];
        Result.y = World.vv.k[1];
        Result.z = World.vv.k[2];
        return Result;
    }

    // .text:06284591..06284598: the surface's mass centre in object space (slot 1).
    float Center[3];
    Manager->get_mass_center(Center);

    // .text:0628459b..06284640, x87: the three first-column products stay on the stack and the
    // third is stored as a float (+0x70); the first two get the second-column product added and
    // are stored as floats ([ESP], [ESP+4]); the third-column product and the translation are
    // then added to those, each component rounded once at its FSTP.
    IVP_U_Point const* const Rows = World.rows;
    double const X0 = (double)(Rows[0].k[0]) * Center[0];
    double const Y0 = (double)(Rows[1].k[0]) * Center[0];
    float const Z0 = (float)((double)(Rows[2].k[0]) * Center[0]);
    float const X1 = (float)((double)(Rows[0].k[1]) * Center[1] + X0);
    float const Y1 = (float)((double)(Rows[1].k[1]) * Center[1] + Y0);
    double const Z1 = (double)(Rows[2].k[1]) * Center[1] + Z0;
    Result.x = (float)((double)(Rows[0].k[2]) * Center[2] + X1 + World.vv.k[0]);
    Result.y = (float)((double)(Rows[1].k[2]) * Center[2] + Y1 + World.vv.k[1]);
    Result.z = (float)((double)(Rows[2].k[2]) * Center[2] + Z1 + World.vv.k[2]);
    return Result;
}

namespace
{
    // FUN_062842e0 (.text:062842e0..06284366, cdecl): the radius of Object's own body.
    // False without a body. A body with a phantom controller (IVP_Object +0x1c) answers true
    // and leaves *OutRadius alone. Otherwise *OutRadius = field_94 + the shape radius:
    // object_type 2 asks the surface manager for the radius around its mass centre (slots 1
    // and 2), object_type 3 takes |shift_core_f_object| (FUN_06347e60 on +0x30).
    bool PhySys_GetBodyRadius(CObject const* Object, float* OutRadius)
    {
        IVP_Real_Object* const Body = Object->BodyPtr;
        if (Body == nullptr)
            return false;
        if (Body->first_child != nullptr)
            return true;

        float ShapeRadius = 0.0f;
        if (Body->object_type == 2)
        {
            // .text:06284322..06284343
            float MassCenter[3];
            float RadiusDeviation;
            Object->SurfaceManager->get_mass_center(MassCenter);
            Object->SurfaceManager->get_radius_and_radius_dev_to_given_center(MassCenter, &ShapeRadius,
                                                                              &RadiusDeviation);
        }
        else if (Body->object_type == 3)
        {
            // .text:06284314..0628431c: FUN_06347e60 keeps sqrt((x*x + y*y) + z*z) on the x87
            // stack; the FSTP into the radius slot rounds it.
            IVP_U_Point const& Shift = Body->shift_core_f_object;
            double const X = Shift.k[0];
            double const Y = Shift.k[1];
            double const Z = Shift.k[2];
            ShapeRadius = (float)(sqrt((X * X + Y * Y) + Z * Z));
        }
        else
        {
            // .text:06284308: retail's `MOV dword ptr [0],0` -- a real store to address zero
            // that traps in every build, then (only under a debugger that skips it)
            // JMP 06284346 with the radius slot unwritten.
            IVP_CoreTrap();
        }

        // .text:06284346..0628435a
        *OutRadius = (float)((double)(Body->field_94) + ShapeRadius);
        return true;
    }

    float PhySys_GetChildrenRadius(Vector const& Center, CObject const* Owner, long Instance,
                                   bool bIncludeChildren, uint32_t UseHullSlot);

    // FUN_06284390 (.text:06284390..0628447f, cdecl).
    // Very strange, but retail: the radius is kept in the UseHull argument's own stack slot
    // (param_3) and the hull flag in the low byte of the Object argument's slot (param_1).
    // UseHullSlot is that whole argument dword (only its low byte is the bool). A path that
    // writes neither -- GetBodyRadius without a body or for a phantom body, an unknown mode --
    // keeps the argument bits: the radius is UseHullSlot read as a float (GetRadiusR pushes 1,
    // i.e. 1.4e-45f), the flag the Object pointer's low byte. Reproduced; on the recursive
    // calls those bits are an object address, so their value differs from retail's.
    float PhySys_GetRadius(CObject const* Object, bool bIncludeChildren, uint32_t UseHullSlot, Vector& OutCenter)
    {
        float Radius;                                       // param_3
        memcpy(&Radius, &UseHullSlot, sizeof(Radius));
        uint32_t ObjectSlot = (uint32_t)(uintptr_t)Object; // param_1, its low byte the flag
        switch (Object->PhysicsControlMode)
        {
        case 0:
            // .text:06284406..0628441e: with phantoms attached the body radius, else the core's
            // (FUN_06345ea0: core +0x04).
            if (!Object->Phantoms.empty())
            {
                PhySys_GetBodyRadius(Object, &Radius);
                ObjectSlot = (ObjectSlot & ~0xffu) | 1u;    // 0628441e
            }
            else
            {
                // .text:0628440e -> 062843f3..062843ff: FUN_06345ea0 on the body, FSTP float.
                Radius = Object->BodyPtr->get_upper_limit_radius();
                ObjectSlot &= ~0xffu;
            }
            break;
        case 1:
            // .text:062843ef..06284404 (the same CALL 06345ea0 at 062843f6); else 062843e0.
            if (bIncludeChildren)
                Radius = Object->BodyPtr->get_upper_limit_radius();
            else
                Radius = 0.0f;
            ObjectSlot &= ~0xffu;
            break;
        case 2:
            // .text:062843d1..062843ed
            if ((uint8_t)UseHullSlot != 0)
            {
                PhySys_GetBodyRadius(Object, &Radius);
                ObjectSlot = (ObjectSlot & ~0xffu) | 1u;
            }
            else
            {
                Radius = 0.0f;                              // 062843e0
                ObjectSlot &= ~0xffu;                       // 062843e8
            }
            break;
        default:
            // .text:062843ab..062843cf: neither slot is written.
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhySysSource, 0xc8,
                  "PhySys::GetRadiusR(): Unknown anim_technique!");
            break;
        }

        // .text:06284423..0628444a
        OutCenter = PhySys::GetCenterOfMass(Object);

        // .text:0628444b..0628447a: with children or the flag byte, the farthest child sphere
        // wins when it is strictly larger (TEST AH,0x41 + JZ); the whole param_1 dword is
        // pushed as the callee's UseHull argument (06284455).
        if (bIncludeChildren || (uint8_t)ObjectSlot != 0)
        {
            float const ChildRadius = PhySys_GetChildrenRadius(OutCenter, Object, Object->EngineInstanceId,
                                                               bIncludeChildren, ObjectSlot);
            if (ChildRadius > Radius)
                return ChildRadius;
        }
        return Radius;
    }

    // FUN_06284480 (.text:06284480..0628456b, cdecl): the largest
    // `child radius + distance(child centre, Center)` over Instance's children; instances of
    // Owner itself are descended into.
    float PhySys_GetChildrenRadius(Vector const& Center, CObject const* Owner, long Instance,
                                   bool bIncludeChildren, uint32_t UseHullSlot)
    {
        float Max = 0.0f;
        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            CObject const* const ChildObject = PhySys_InstanceObject(Child);
            double Candidate;
            if (ChildObject == Owner)
            {
                // .text:06284519..06284527
                Candidate = PhySys_GetChildrenRadius(Center, Owner, Child, bIncludeChildren, UseHullSlot);
            }
            else
            {
                // .text:062844d5..06284515: radius + |ChildCenter - Center|, the squares summed
                // z, x, then y, all on the x87 stack.
                Vector ChildCenter;
                float const ChildRadius = PhySys_GetRadius(ChildObject, bIncludeChildren, UseHullSlot, ChildCenter);
                double const Dx = (double)(ChildCenter.x) - Center.x;
                double const Dy = (double)(ChildCenter.y) - Center.y;
                double const Dz = (double)(ChildCenter.z) - Center.z;
                Candidate = ChildRadius + sqrt((Dz * Dz + Dx * Dx) + Dy * Dy);
            }

            // .text:0628452a..0628453b: TEST AH,0x41 + JNZ keeps Max unless strictly greater.
            if (Candidate > Max)
                Max = (float)(Candidate);
        }
        return Max;
    }
}

// .text:06284370..06284389: FUN_06284390(Object, bIncludeChildren, true, OutCenter).
float __cdecl PhySys::GetRadiusR(CObject const* Object, bool bIncludeChildren, Vector& OutCenter)
{
    return PhySys_GetRadius(Object, bIncludeChildren, 1u, OutCenter);   // 0628437d PUSH 0x1
}

// ============================================================================
// BeamR
// ============================================================================

namespace
{
    // The phantom pose helpers BeamR uses (PhySys_GetPhantomPosition / PhySys_SetPhantomPosition)
    // are declared in common_physys_internal.h and defined with PhySys::Phantom in
    // common_phantom.cpp.

    // The object's Orientation (+0x08) and WorldPosition (+0x2c) as one 12-float transform --
    // retail copies it with REP MOVSD 0xc from / to +0x08 (06285175, 062851f8, 062852a3).
    FTransform& PhySys_ObjectTransform(CObject* Object)
    {
        static_assert(offsetof(CObject, WorldPosition) == offsetof(CObject, Orientation) + sizeof(FMatrix),
                      "CObject: Orientation and WorldPosition are one FTransform");
        return *(FTransform*)&Object->Orientation;
    }

    // FUN_06284f70 (thiscall, RET 0x24) called with the nine elements of a matrix pushed in
    // column order (0x24ef3 / 0x2507d): the transpose.
    FMatrix PhySys_Transpose(Matrix const& M)
    {
        FMatrix Out;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Out.D[Row][Column] = M.E[Column][Row];
        }
        return Out;
    }

    // FUN_06284fc0 (.text:06284fc0..06285188, cdecl): beams Object's body to NewXform, carries
    // every attached phantom along (its offset kept in the object's old frame), then the object
    // takes NewXform and its jointed children are re-posed.
    void PhySys_BeamObject(CObject* Object, FTransform const* NewXform, bool bOptimize)
    {
        I3DMathEngine* const Math = Acquire3DMathEngine();

        // .text:06284fcf..06284fe8: the new position as an IVP_U_Point (FLD / FSTP float).
        IVP_U_Point Position;
        Position.set(NewXform->Translation.X, NewXform->Translation.Y, NewXform->Translation.Z);

        // .text:06284fc6..06285035: MatrixToQuaternion (+0x50) on the new rotation, stored as
        // IVP's (x, y, z, w) from the engine's (W, X, Y, Z).
        FQuaternion Quat;
        Math->MatrixToQuaternion(Quat, NewXform->Rotation);
        IVP_U_Quat Rotation;
        Rotation.x = Quat.X;
        Rotation.y = Quat.Y;
        Rotation.z = Quat.Z;
        Rotation.w = Quat.W;

        // .text:06285027..06285047: FUN_06345f90(Body (+0x54), &Rotation, &Position,
        // SETNZ bOptimize). The body is not tested.
        Object->BodyPtr->beam_object_to_new_position(&Rotation, &Position, bOptimize ? 1 : 0);

        // .text:0628504c..06285162: with attached phantoms (list size +0x80), each one keeps its
        // offset: Local = transpose(old Orientation) * (PhantomPos - old WorldPosition)
        // (TransformMatrix +0x2c), NewPos = NewXform * Local (TransformXform +0x28).
        if (!Object->Phantoms.empty())
        {
            FMatrix const InvRotation = PhySys_Transpose(Object->Orientation);
            for (std::list<void*>::iterator It = Object->Phantoms.begin(); It != Object->Phantoms.end(); ++It)
            {
                // .text:06285097..062850c9: FUN_0628b740, then FUN_0628b030 of the three
                // FLD / FSUB float / FSTP float differences.
                Vector PhantomPosition;
                PhySys_GetPhantomPosition(*It, &PhantomPosition);
                FVector Offset;
                Offset.X = (float)((double)(PhantomPosition.x) - Object->WorldPosition.x);
                Offset.Y = (float)((double)(PhantomPosition.y) - Object->WorldPosition.y);
                Offset.Z = (float)((double)(PhantomPosition.z) - Object->WorldPosition.z);

                // .text:062850ce..06285111
                FVector Local;
                Math->TransformMatrix(Local, InvRotation, Offset);

                // .text:06285106..06285153
                FVector NewPosition;
                Math->TransformXform(NewPosition, *NewXform, Local);
                Vector Target;
                Target.x = NewPosition.X;
                Target.y = NewPosition.Y;
                Target.z = NewPosition.Z;
                PhySys_SetPhantomPosition(*It, &Target);
            }
        }

        // .text:06285168..06285177: Object +0x08..+0x37 = *NewXform, then FUN_062883c0(+0x04,
        // Object).
        PhySys_ObjectTransform(Object) = *NewXform;
        PhySys_UpdateJointedChildren(Object->EngineInstanceId, Object);
    }

    // FUN_06285190 (.text:06285190..06285343, cdecl; PhySys::_BeamR_Children, PhyObj.cpp):
    // every child instance of Instance (IEngine +0xd8, flags 1, -1 ends) is visited. A child
    // whose user data (+0x80) is Object itself is only recursed into. Any other child object
    // gets ChildNew = NewXform * (InvXform * ChildOld) (MulTransform +0x1c twice), then by its
    // control mode (+0x74): 1 -> FUN_06284fc0 (the body is beamed), 2 -> the transform is
    // stored and FUN_062883c0 runs, else an error; then it is recursed into. The child object
    // is not tested for null.
    void PhySys_BeamR_Children(CObject* Object, long Instance, FTransform const* NewXform,
                               FTransform const* InvXform, bool bOptimize)
    {
        // .text:06285190..062851b5 / 06285310..06285333
        for (long ChildInstance = PhySys_RelatedInstance(Instance, 1, -1); ChildInstance != -1;
             ChildInstance = PhySys_RelatedInstance(Instance, 1, ChildInstance))
        {
            // .text:062851c0..062851dc
            CObject* const ChildObject = PhySys_InstanceObject(ChildInstance);
            if (ChildObject == Object)
            {
                // .text:062852f1..0628530b
                PhySys_BeamR_Children(Object, ChildInstance, NewXform, InvXform, bOptimize);
                continue;
            }

            // .text:062851e2..06285271
            I3DMathEngine* const Math = Acquire3DMathEngine();
            FTransform const ChildOld = PhySys_ObjectTransform(ChildObject);
            FTransform Relative;
            Math->MulTransform(Relative, *InvXform, ChildOld);
            FTransform ChildNew;
            Math->MulTransform(ChildNew, *NewXform, Relative);

            // .text:0628525b..062852d2
            if (ChildObject->PhysicsControlMode == 1)
            {
                PhySys_BeamObject(ChildObject, &ChildNew, bOptimize);
            }
            else if (ChildObject->PhysicsControlMode == 2)
            {
                PhySys_ObjectTransform(ChildObject) = ChildNew;
                PhySys_UpdateJointedChildren(ChildInstance, ChildObject);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhyObjSource, 0x21d,
                      "Unknown anim_technique in PhySys::_BeamR_Children()");
            }

            // .text:062852d5..0628530b
            PhySys_BeamR_Children(ChildObject, ChildInstance, NewXform, InvXform, bOptimize);
        }
    }
}

// .text:06284e50..06284f64.
void __cdecl PhySys::BeamR(CObject* Object, Vector const& Pos, Matrix const& Orient, bool bFlag)
{
    // .text:06284e66..06284e90: the new transform (Orient by REP MOVSD 9, Pos by three dwords).
    FTransform NewXform;
    NewXform.Rotation = (FMatrix const&)Orient;   // Matrix and FMatrix: the same nine floats
    NewXform.Translation.X = Pos.x;
    NewXform.Translation.Y = Pos.y;
    NewXform.Translation.Z = Pos.z;

    // .text:06284e85..06284f37: the inverse of Object's current transform -- rotation =
    // transpose(Orientation) (FUN_06284f70), translation = -(TransposeTransform(+0x38) of
    // WorldPosition) (FCHS, FSTP float). Object is not tested.
    FVector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, (FMatrix const&)Object->Orientation,
                                              (FVector const&)Object->WorldPosition);
    FTransform InvXform;
    InvXform.Rotation = PhySys_Transpose(Object->Orientation);
    InvXform.Translation.X = -Rotated.X;
    InvXform.Translation.Y = -Rotated.Y;
    InvXform.Translation.Z = -Rotated.Z;

    // .text:06284f1a..06284f53: FUN_06284fc0(Object, &NewXform, bFlag), then
    // FUN_06285190(Object, Object+0x04, &NewXform, &InvXform, bFlag).
    PhySys_BeamObject(Object, &NewXform, bFlag);
    PhySys_BeamR_Children(Object, Object->EngineInstanceId, &NewXform, &InvXform, bFlag);
}

namespace
{
    // FUN_06289c90 (.text:06289c90..06289e45, cdecl): Template's ray against Object's body
    // only (FUN_0634a7d0); nearest hit first, at most MaxHits, client-less hits skipped. Unlike
    // FUN_0628d1a0 the point is start + distance * direction with x kept on the FPU stack, and
    // no normal is written.
    int PhySys_FindRayIntersectObject(CObject* Object, IVP_Ray_Solver_Template const* Template,
                                      PhySys::RayHit* Hits, int MaxHits)
    {
        // .text:06289cb9..06289cfc
        IVP_Ray_Solver_Min_Hash Solver(Template);
        Solver.check_ray_against_object(Object->BodyPtr);

        // .text:06289d01..06289d34: start and direction (dword copies).
        const float StartX = Template->ray_start_point.x;
        const float StartY = Template->ray_start_point.y;
        const float StartZ = Template->ray_start_point.z;
        const float DirX = Template->ray_normized_direction.x;
        const float DirY = Template->ray_normized_direction.y;
        const float DirZ = Template->ray_normized_direction.z;

        // .text:06289d40..06289e08
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

                // .text:06289d79..06289dcc: x * distance stays on the stack, y / z products are
                // FSTP float; each + start, FSTP float.
                const double Distance = Hit->hit_distance;
                const double ScaledX = DirX * Distance;
                const float ScaledY = (float)(DirY * Distance);
                const float ScaledZ = (float)(DirZ * Distance);
                Out->Point.x = (float)(ScaledX + StartX);
                Out->Point.y = (float)((double)(ScaledY) + StartY);
                Out->Point.z = (float)((double)(ScaledZ) + StartZ);

                // .text:06289dcf..06289df1
                Out->PartId = (Hit->hit_compact_ledge != nullptr)
                                  ? (unsigned)Hit->hit_compact_ledge->ClientData
                                  : 0;
                ++Count;
                ++Out;
            }

            Solver.min_hash.remove(Hit);
        }
        return Count;
    }
}

int __cdecl PhySys::FindRayCollisions(unsigned SystemId, Vector const& From, Vector const& To,
                                       RayHit* Hits, int MaxHits)
{
    // .text:06289c20..06289c62: the inline find (FUN_062fcfb0 lower_bound + key test); a miss
    // returns 0.
    std::map<unsigned, PhysicalSystem>::iterator SystemIt = G_PhysicalSystems.find(SystemId);
    if (SystemIt == G_PhysicalSystems.end())
        return 0;

    // .text:06289c63..06289c82
    return SystemIt->second.FindRayCollisions(From, To, Hits, MaxHits);
}

// .text:06289ef0..06289fe3.
int __cdecl PhySys::FindRayIntersect(CObject* Object, Vector const& From, Vector const& To,
                                      RayHit* Hits, int MaxHits)
{
    // .text:06289ef3..06289f1b: Delta = To - From, FSUB float, FSTP float; z is FST, so its
    // unrounded value stays on the stack for its own square.
    const float Dx = (float)((double)(To.x) - From.x);
    const float Dy = (float)((double)(To.y) - From.y);
    const double DzExact = (double)(To.z) - From.z;
    const float Dz = (float)(DzExact);

    // .text:06289f1f..06289f5b: Length = sqrt((DzExact * dz + dx*dx) + dy*dy); InvLength =
    // 1.0f / Length; direction components FSTP float; Length stored as a float. The z
    // component (InvLength * dz) stays on the stack until it is stored.
    const double Length = sqrt((DzExact * Dz + (double)(Dx) * Dx) + (double)(Dy) * Dy);
    const double InvLength = 1.0 / Length;
    const float DirX = (float)(Dx * InvLength);
    const float DirY = (float)(Dy * InvLength);
    const float DirZ = (float)(InvLength * Dz);

    // .text:06289efd / 06289f5f..06289fd0: the template (start From, the direction, Length,
    // flags 0). UB in retail: both fourth floats come from a never-written slot (local_2c);
    // the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(From.x, From.y, From.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(DirX, DirY, DirZ);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = (float)(Length);
    Template.ray_flags = 0;

    // .text:06289fc3..06289fdb: FUN_06289c90(Object, &Template, Hits, MaxHits).
    return PhySys_FindRayIntersectObject(Object, &Template, Hits, MaxHits);
}

// .text:06289ff0..0628a048 (cdecl). FUN_062fcfb0 = G_PhysicalSystems.lower_bound(SystemId);
// the end node or a key greater than SystemId (`CMP EDX,[EAX+0xc]; JC`) selects the map head,
// then FUN_0628d450 runs on node+0x10 with (Center, Radius, OutObjects, MaxObjects) -- with no
// end() test, unlike PhySys::FindRayCollisions (06289c5b). A miss queries the head node's
// unconstructed storage (PhySys_FindSystem: very strange, reproduced on purpose).
int __cdecl PhySys::FindSphereCollisions(unsigned SystemId, Vector const& Center, float Radius,
                                          CObject** OutObjects, int MaxObjects)
{
    return PhySys_FindSystem(SystemId).FindSphereCollisions(Center, Radius, OutObjects, MaxObjects);
}

void __cdecl PhySys::GenerateCollisions(CBeam* Beam)
{
    // .text:06289b20..06289c13. Delta = WorldPosition - (bMoved ? LastPos : WorldPosition);
    // FSTP float per component (.text:06289b3b..06289b54; z is FST and stays on the stack).
    Vector const& Source = (Beam->bMoved != 0) ? Beam->LastPos : Beam->WorldPosition;
    Vector Delta;
    Delta.x = (float)((double)(Beam->WorldPosition.x) - Source.x);
    Delta.y = (float)((double)(Beam->WorldPosition.y) - Source.y);
    double const DeltaZExact = (double)(Beam->WorldPosition.z) - Source.z;
    Delta.z = (float)(DeltaZExact);

    // .text:06289b58..06289b81: Len = sqrt((z_exact * z + x*x) + y*y) on the x87 stack -- the
    // unrounded z (FST keeps ST0) times its stored float -- FST float into the length slot;
    // FCOMP double 0.0001 (DAT_0639c2b8); TEST AH,0x41; JNZ -> return (less, equal,
    // unordered); the fall-through is Len > 0.0001.
    double const LengthX87 = sqrt((DeltaZExact * Delta.z + (double)(Delta.x) * Delta.x) +
                                  (double)(Delta.y) * Delta.y);
    float const Length = (float)(LengthX87);
    if (!(LengthX87 > 0.0001))
        return;

    // .text:06289b87..06289b98: the system id is the beam's object database zone
    // (IObjDB vtable +0x08) -> Universe::ISystem slot 0.
    Universe::ISystem const* const System = Beam->ObjectDb->GetZoneDb();
    unsigned const SystemId = System->GetId();

    // .text:06289b98..06289bd0: lower_bound + key test, no end() test -- a miss uses the map
    // head node's unconstructed storage (PhySys_FindSystem: very strange, reproduced on purpose).
    PhysicalSystem& PhysSystem = PhySys_FindSystem(SystemId);

    // .text:06289bd0..06289c08: Dir = Delta * (1.0f / (float)Len), each product FSTP float.
    double const InvLength = 1.0 / (double)(Length);
    Delta.x = (float)(Delta.x * InvLength);
    Delta.y = (float)(Delta.y * InvLength);
    Delta.z = (float)(Delta.z * InvLength);

    // .text:06289c0a: FUN_0628ccf0(this = the system, Beam, &Dir, Len).
    PhysSystem.GenerateCollisions(Beam, Delta, Length);
}

void __cdecl PhySys::GetCollisions(unsigned SystemId, CollisionEvent const*& Begin,
                                    CollisionEvent const*& End)
{
    // .text:06289a20..06289a6e: lower_bound + key test; Begin = the collision vector's _First
    // (node+0x30), End = its _Last (node+0x34). No end() test: a missing SystemId reads both
    // through the map head node (PhySys_FindSystem: very strange, reproduced on purpose).
    std::vector<CollisionEvent> const& Collisions = PhySys_FindSystem(SystemId).Collisions;
    Begin = Collisions.data();
    End = Collisions.data() + Collisions.size();
}

// ============================================================================
// Misc
// ============================================================================

bool __cdecl PhySys::AnyActive()
{
    // .text:06288960..0628896d: `return DAT_063fc0a8 != 0` -- the world map's size.
    return !G_PhysicalSystems.empty();
}

unsigned __cdecl PhySys::MovingObjectCount(unsigned SystemId)
{
    // .text:06289ad0..06289b16: lower_bound + key test; return the moving-object set's _Size
    // (node+0x4c). No end() test: a missing SystemId reads through the map head node
    // (PhySys_FindSystem: very strange, reproduced on purpose).
    return (unsigned)PhySys_FindSystem(SystemId).MovingObjects.size();
}

IVP_Time __cdecl PhySys::GetCurrentTime(unsigned SystemId)
{
    // FUN_06289a70 (.text:06289a70-06289ac4). REWRITTEN 2026-09-17 (lane claude:w19-integrator).
    //
    // The old body returned `Result.Value = World->SimTime` into a `double Value` member. That
    // was wrong twice over: IVP_Time is not a double (see its declaration in
    // common_physys.h for the two raw-byte proofs), and retail does not synthesise the value at
    // all -- it copies the environment's own split-precision stamp out verbatim:
    //     .text:06289aa9  MOV ECX,[EAX+0x28]        ; world->Environment
    //     .text:06289aac  MOV EDX,[ECX+0xf8]        ; current_time.time_base
    //     .text:06289ab6  MOV [EAX],EDX
    //     .text:06289ab8  MOV ECX,[ECX+0xfc]        ; current_time.time_offset
    //     .text:06289abe  MOV [EAX+0x4],ECX
    // Two dword moves, no FPU, no conversion -- which rules out a conversion happening HERE
    // but does not by itself distinguish a float pair from a struct{double}, since a blind
    // 8-byte copy looks the same either way. What settles it is the consumer:
    // FLControllerGolem's integrator (LAB_06343150) subtracts a stored copy of this pair from
    // the SAME env+0xf8/+0xfc pair with x87 FLOAT ops, so a double here fed that subtraction a
    // denormal and made every golem's elapsed time garbage. The golem also stores the pair at
    // its own +0x64, where an 8-byte-aligned double member could not sit.
    //
    // .text:06289a8e/06289a97 is the usual "map lookup missed, or the found node's key is
    // below the requested one" fallback: retail then reads through the head node
    // (.data:DAT_063fc09c) -- no end() test, and the environment pointer is not tested either
    // (PhySys_FindSystem: very strange, reproduced on purpose).
    // PhysicalSystem::Environment is the world node's +0x28 (value +0x18).
    IVP_Environment* const Environment = PhySys_FindSystem(SystemId).Environment;
    IVP_Time Result;
    Result.time_base = Environment->current_time.time_base;
    Result.time_offset = Environment->current_time.time_offset;
    return Result;
}

