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
#include "../IVP/ivp_utility/ivp_core_trap.h"      // IVP_CoreTrap -- retail's `MOV [0],0` (0628bcf9)

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
// Phantoms -- PhySys::Phantom (.text:0628aa70..0628b797)
// ============================================================================
//
// The handle every exported phantom function takes as void*. RTTI ".?AUPhantom@PhySys@@"
// (COL 063a72ec): struct Phantom : IVP_Listener_Set_Active<IVP_Real_Object>, vtable
// .rdata:0639c30c with exactly the three listener slots 0628aa70 / 0628aa90 / 0628aab0 (no
// destructor slot). Retail does not export the type. All three CreatePhantom bodies allocate it
// with operator new(0x1c) and zero every field (0628ab34..0628ab46 / 0628b128..0628b13a /
// 0628b22f..0628b241). DestroyPhantom inlines its (non-virtual) destructor: the vtable is
// re-stamped at 0628b4cc, then the body, the compact surface and the surface manager go.
#include "../IVP/ivp_utility/ivp_malloc.h" // ivp_free_aligned, the IVP aligned free (FUN_06343ff0)

namespace PhySys
{
    struct Phantom : public IVP_Listener_Set_Active<IVP_Real_Object>
    {
        Phantom()
            : Body(nullptr)
            , Surface(nullptr)
            , SurfaceManager(nullptr)
            , Listener(nullptr)
            , Owner(nullptr)
            , SystemId(0)
        {
        }

        // Inlined at .text:0628b4c7..0628b4f5.
        ~Phantom();

        // Slot 0, 0628aa70 (RET 8).
        void element_added(IVP_U_Set_Active<IVP_Real_Object>* Set, IVP_Real_Object* Elem) override;
        // Slot 1, 0628aa90 (RET 8).
        void element_removed(IVP_U_Set_Active<IVP_Real_Object>* Set, IVP_Real_Object* Elem) override;
        // Slot 2, 0628aab0 (RET 4).
        void pset_is_going_to_be_deleted(IVP_U_Set_Active<IVP_Real_Object>* Set) override;

        IVP_Real_Object* Body;                      // +0x04  the phantom's IVP body
        IVP_Compact_Surface* Surface;               // +0x08  built and owned by the box phantom only
        IVP_SurfaceManager_Polygon* SurfaceManager; // +0x0c  box and part phantoms
        CObjectCB* Listener;                        // +0x10  PhantomParms+0x40
        CObject* Owner;                             // +0x14  AttachPhantom
        unsigned SystemId;                          // +0x18  PhantomParms+0x00
    };
    static_assert(sizeof(Phantom) == 0x1c, "PhySys::Phantom is operator new(0x1c)");
}

// .text:0628b4c7..0628b4f5: the body through FUN_06345450 when set, the compact surface through
// FUN_06343ff0 when set, the surface manager through its deleting destructor (slot 9, flag 1)
// when set.
PhySys::Phantom::~Phantom()
{
    if (Body != nullptr)
        PhySys_DeleteBody(Body);
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0
    if (SurfaceManager != nullptr)
        delete SurfaceManager;
}

// 0628aa70..0628aa8a: an object entered the phantom -- with a listener and a client object
// (IVP_Real_Object+0xa4), the listener's slot 0 gets the client.
void PhySys::Phantom::element_added(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/, IVP_Real_Object* Elem)
{
    if (Listener == nullptr)
        return;
    CObject* const Client = (CObject*)Elem->client_data;
    if (Client == nullptr)
        return;
    Listener->added(Client);
}

// 0628aa90..0628aaab: the same, slot 1.
void PhySys::Phantom::element_removed(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/, IVP_Real_Object* Elem)
{
    if (Listener == nullptr)
        return;
    CObject* const Client = (CObject*)Elem->client_data;
    if (Client == nullptr)
        return;
    Listener->deleted(Client);
}

// 0628aab0..0628aab7: the set goes away -- only the listener pointer is dropped.
void PhySys::Phantom::pset_is_going_to_be_deleted(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/)
{
    Listener = nullptr;
}

namespace
{
    // The tail all three CreatePhantom bodies share (0628af8d..0628afed / 0628b16f..0628b1c6 /
    // 0628b373..0628b3ca): a stack IVP_Template_Phantom (FUN_06344210) with
    // manage_intruding_objects = 1 and client_data = MOVZX PhantomParms+0x05, the body becomes a
    // phantom (FUN_06345f10); with a listener (+0x40) the phantom stores it (+0x10) and joins
    // the listeners of the controller's object set (first_child +0x28, vector +0x10; the inline
    // IVP_U_Vector add with FUN_063441a0 on a full vector).
    void PhySys_ConvertToPhantom(PhySys::Phantom* NewPhantom, PhySys::PhantomParms const& Parms)
    {
        IVP_Template_Phantom PhantomTemplate;
        PhantomTemplate.manage_intruding_objects = 1;
        PhantomTemplate.client_data = (void*)(uintptr_t)Parms.Flag05;
        NewPhantom->Body->convert_to_phantom(&PhantomTemplate);

        PhySys::CObjectCB* const Listener = Parms.Listener;
        if (Listener != nullptr)
        {
            NewPhantom->Listener = Listener;
            ((IVP_Controller_Phantom*)NewPhantom->Body->first_child)->set_of_objects->listeners.Add(NewPhantom);
        }
    }
}

// The two pose helpers below are declared in common_physys_internal.h; their caller,
// PhySys_BeamObject, is in common_physys_query.cpp.
namespace CmnPhySys
{
    // FUN_0628b740 (.text:0628b740..0628b797, cdecl; only caller PhySys_BeamObject 062850a0):
    // null handle -> 1; else the phantom body's position (calc_at_time_AT, the quaternion is
    // dropped) into *OutPosition, 0.
    int PhySys_GetPhantomPosition(void* Handle, Vector* OutPosition)
    {
        if (Handle == nullptr)
            return 1;
        IVP_U_Quat Rotation;
        IVP_U_Point Position;
        ((PhySys::Phantom*)Handle)->Body->calc_at_time_AT(&Rotation, &Position);
        OutPosition->x = Position.x;
        OutPosition->y = Position.y;
        OutPosition->z = Position.z;
        return 0;
    }

    // FUN_0628b6e0 (.text:0628b6e0..0628b735, cdecl; only caller PhySys_BeamObject 06285153):
    // null handle -> 1; else the target (three FLD / FSTP float), the body's current
    // orientation (calc_at_time_AT), and FUN_06345f90 beam_object_to_new_position(&quat,
    // &target, 0); 0. UB in retail: the target point's fourth float is never written (the
    // port's set() leaves it as constructed); the beam reads x / y / z only.
    int PhySys_SetPhantomPosition(void* Handle, Vector const* NewPosition)
    {
        if (Handle == nullptr)
            return 1;
        IVP_Real_Object* const Body = ((PhySys::Phantom*)Handle)->Body;
        IVP_U_Point Target;
        Target.set(NewPosition->x, NewPosition->y, NewPosition->z);
        IVP_U_Quat Rotation;
        IVP_U_Point Position;
        Body->calc_at_time_AT(&Rotation, &Position);
        Body->beam_object_to_new_position(&Rotation, &Target, 0);
        return 0;
    }
}

// .text:0628aac0..0628b027. A box phantom; HalfExtents are the box's half sizes.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, Vector const& HalfExtents, void*& OutHandle)
{
    // .text:0628aadf..0628ab14: the construction template (FUN_063486c0), named after +0x06
    // (FUN_06348720), +0x0c = MOVZX +0x04.
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.Flag04;

    // .text:0628ab0f..0628ab21: FUN_06301640 = G_PhysicalSystems.find(+0x00); the iterator is
    // not tested -- a miss builds into the map head node's unconstructed storage
    // (PhySys_FindSystem: very strange, reproduced on purpose).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    // .text:0628ab26..0628ab5c
    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    {
        // .text:0628ab55..0628ab6e: a ledge soup (FUN_06369e70) and an empty point vector
        // (memsize 0, n_elems 0, elems 0); both are destroyed at 0628aec1 / 0628aed4.
        IVP_SurfaceBuilder_Ledge_Soup Soup;
        IVP_U_Vector<IVP_U_Float_Point> Points;

        // .text:0628ab72..0628ae6d: the eight corners on the stack, appended in this order. Each
        // negated component is an FLD / FCHS / FSTP float (exact). UB in retail: the fourth
        // float of every corner is never written (stack garbage); the port stores 0.
        const float Ex = HalfExtents.x;
        const float Ey = HalfExtents.y;
        const float Ez = HalfExtents.z;
        IVP_U_Float_Point Corners[8] = {
            {  Ex,  Ey,  Ez, 0.0f },   // local_1a0
            { -Ex, -Ey, -Ez, 0.0f },   // local_190
            { -Ex,  Ey,  Ez, 0.0f },   // local_180
            {  Ex, -Ey, -Ez, 0.0f },   // local_170 (FUN_0628b030 of local_180 negated)
            {  Ex, -Ey,  Ez, 0.0f },   // local_160
            { -Ex,  Ey, -Ez, 0.0f },   // local_150 (local_160 negated)
            {  Ex,  Ey, -Ez, 0.0f },   // local_140
            { -Ex, -Ey,  Ez, 0.0f },   // local_130 (local_140 negated)
        };
        for (int Index = 0; Index < 8; ++Index)
            Points.Add(&Corners[Index]);

        // .text:0628ae72..0628aeb6: the hull (FUN_0636c9a0) goes into the soup (FUN_0636a050),
        // which compiles (FUN_0636a0b0) with the options {0, 1, 0, 1} into phantom+0x08.
        IVP_Compact_Ledge* const Hull = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&Points);
        Soup.InsertLedge(Hull);
        const int32_t CompileOptions[4] = { 0, 1, 0, 1 };
        NewPhantom->Surface = (IVP_Compact_Surface*)Soup.Compile(CompileOptions);
    }

    // .text:0628aed9..0628aef9: operator new(8), the plain IVP_SurfaceManager_Polygon (vtable
    // 0639c2dc) over the new surface, at phantom+0x0c.
    NewPhantom->SurfaceManager = new IVP_SurfaceManager_Polygon(NewPhantom->Surface);

    // .text:0628aefc..0628af1a: the position, FUN_0636cf40(+0x10, +0x14, +0x18).
    IVP_U_Point Position;
    Position.set(Parms.Position.x, Parms.Position.y, Parms.Position.z);

    // .text:0628af1f..0628af4f: the orientation (+0x1c) through FUN_0628b070 (the math engine's
    // slot +0x50) and the (W, X, Y, Z) -> (x, y, z, w) copy.
    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Parms.Orientation, Rotation);

    // .text:0628af3c..0628af5a
    if (Template.Material == nullptr)
        Template.Material = G_PhySysMaterial;

    // .text:0628af61..0628af8a: FUN_0634cd80(System->Environment, SurfaceManager, &Template,
    // &Rotation, &Position) -> phantom+0x04.
    NewPhantom->Body = IVP_Environment_CreatePolygon(System.Environment, NewPhantom->SurfaceManager, &Template,
                                                     &Rotation, &Position);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    // .text:0628afed..0628b017
    OutHandle = NewPhantom;
    return 0;
}

// .text:0628b0a0..0628b1fd. A ball phantom.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, float Radius, void*& OutHandle)
{
    // .text:0628b0bc..0628b0e6
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.Flag04;

    // .text:0628b0ea..0628b118: the inline find (FUN_062fcfb0 lower_bound, then the end / key
    // test selecting the head node); a miss is not tested -- the head node's unconstructed
    // storage is used as the system (PhySys_FindSystem: very strange, reproduced on purpose).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    // .text:0628b11a..0628b154
    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    // .text:0628b14f..0628b16c: FUN_0628c860(System, &+0x10, &+0x1c, Radius, &Template).
    NewPhantom->Body = System.CreateBallBody(Parms.Position, Parms.Orientation, Radius, &Template);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    // .text:0628b1c6..0628b1ed
    OutHandle = NewPhantom;
    return 0;
}

// .text:0628b200..0628b43a. A phantom over a part's compact surface.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, PhyArch::Part const& Part,
                                   void*& OutHandle)
{
    // .text:0628b21f..0628b25d: the phantom comes first here.
    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    // .text:0628b260..0628b263 / 0628b3f0..0628b41d: a part without a surface is a warning and
    // result 1 with a null handle. Retail leaks the phantom on this path; so does the port.
    if (Part.Surface == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kPhySysSource, 0x5d0, "Failed to create Phantom surface.");
        OutHandle = nullptr;
        return 1;
    }

    // .text:0628b269..0628b295
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.Flag04;

    // .text:0628b28a..0628b2c5: the inline find (FUN_062fd030); a miss is not tested -- the
    // head node's unconstructed storage is used as the system (PhySys_FindSystem: very
    // strange, reproduced on purpose; see the ball phantom).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    // .text:0628b2c3..0628b2e7: operator new(8), IVP_SurfaceManager_Polygon over the part's own
    // surface (Part+0x04). phantom+0x08 stays 0 -- the part keeps owning its surface.
    NewPhantom->SurfaceManager = new IVP_SurfaceManager_Polygon(Part.Surface);

    // .text:0628b2ea..0628b302: the position (x as a dword, y and z through FLD / FSTP float).
    IVP_U_Point Position;
    Position.set(Parms.Position.x, Parms.Position.y, Parms.Position.z);

    // .text:0628b2f0..0628b341: the math engine's slot +0x50 on +0x1c inline, then the
    // (W, X, Y, Z) -> (x, y, z, w) copy.
    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Parms.Orientation, Rotation);

    // .text:0628b331..0628b34c
    if (Template.Material == nullptr)
        Template.Material = G_PhySysMaterial;

    // .text:0628b350..0628b370
    NewPhantom->Body = IVP_Environment_CreatePolygon(System.Environment, NewPhantom->SurfaceManager, &Template,
                                                     &Rotation, &Position);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    // .text:0628b3ca..0628b3e7
    OutHandle = NewPhantom;
    return 0;
}

void __cdecl PhySys::DestroyPhantom(void* Handle)
{
    // .text:0628b440..0628b447. `if (Handle == 0) return;`
    if (Handle == nullptr)
        return;
    Phantom* const OldPhantom = (Phantom*)Handle;

    // .text:0628b44d..0628b481: the phantom leaves the listeners of its controller's object set
    // (Body->first_child +0x28, vector +0x10; Body is not tested): FUN_062902b0 (backward
    // search), then an unchecked n_elems - 1 and shift-down = IVP_U_Vector::RemoveElement.
    // VERY STRANGE, REPRODUCED ON PURPOSE (inside RemoveElement): a phantom created without a
    // listener is not in the vector, so the search gives -1 and the vector is corrupted
    // (elems[-1] written, n_elems wraps when empty). Retail's callers always pass a listener.
    ((IVP_Controller_Phantom*)OldPhantom->Body->first_child)->set_of_objects->listeners.RemoveElement(OldPhantom);

    // .text:0628b483..0628b4c1: the owner's phantom list loses the node directly -- retail does
    // NOT go through DetachPhantom here (no IVP split, no construction template). The search
    // stops at the head, and unlike DetachPhantom this path checks for "not found".
    CObject* const Owner = OldPhantom->Owner;
    if (Owner != nullptr)
    {
        std::list<void*>::iterator It = Owner->Phantoms.begin();
        while (It != Owner->Phantoms.end() && *It != Handle)
            ++It;
        if (It != Owner->Phantoms.end())
            Owner->Phantoms.erase(It);
    }

    // .text:0628b4c7..0628b4fe: ~Phantom (body, surface, surface manager), operator delete.
    delete OldPhantom;
}

int __cdecl PhySys::AttachPhantom(CObject* Object, void* Handle)
{
    // .text:0628b560. `if (Handle == 0) return 1;` -- Object is not tested (retail reads
    // Object+0x2c / +0x54 unconditionally).
    if (Handle == nullptr)
        return 1;
    Phantom* const AttachedPhantom = (Phantom*)Handle;

    // .text:0628b578..0628b585: the phantom body's pose, FUN_06345710 =
    // IVP_Real_Object::calc_at_time_AT(&quat local_10, &position local_20).
    IVP_U_Quat PhantomRotation;
    IVP_U_Point PhantomPosition;
    AttachedPhantom->Body->calc_at_time_AT(&PhantomRotation, &PhantomPosition);

    // .text:0628b58e..0628b5c2: the distance from Object->WorldPosition (+0x2c) to it, on the
    // FPU stack: sqrt((dz*dz + dy*dy) + dx*dx), FSTP float.
    const double Dx = (double)(Object->WorldPosition.x) - PhantomPosition.x;
    const double Dy = (double)(Object->WorldPosition.y) - PhantomPosition.y;
    const double Dz = (double)(Object->WorldPosition.z) - PhantomPosition.z;
    const float Distance = (float)(sqrt((Dz * Dz + Dy * Dy) + Dx * Dx));

    // .text:0628b591..0628b5c7: FUN_06343700(Object->Body (+0x54), phantom body, Distance).
    IVP_Merge_Object_Into_Core(Object->BodyPtr, AttachedPhantom->Body, Distance);

    // .text:0628b5cc..0628b5f8: push_back onto CObject::Phantoms (the list at +0x78; node
    // inserted before the head, _Mysize at +0x80 incremented).
    Object->Phantoms.push_back(Handle);
    // .text:0628b5ff: phantom+0x14 = Object.
    AttachedPhantom->Owner = Object;
    return 0;
}

int __cdecl PhySys::DetachPhantom(void* Handle)
{
    // .text:0628b610..0628b634. `if (Handle == 0) return 1;` -- the owner at +0x14 is not tested.
    if (Handle == nullptr)
        return 1;
    Phantom* const DetachedPhantom = (Phantom*)Handle;

    // .text:0628b63b..0628b673: the construction template (FUN_063486c0) named after the owner
    // body's collision group (FUN_06348720(Owner->Body + 0x84)), +0x0c = 1, material =
    // DAT_063fc0bc.
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(DetachedPhantom->Owner->BodyPtr->name);
    Template.FlagsHi = 1;
    Template.Material = G_PhySysMaterial;

    // .text:0628b668..0628b6a2: find the node holding Handle in Owner->Phantoms and erase it.
    // VERY STRANGE, REPRODUCED ON PURPOSE: the search stops at the head but the erase does not
    // test for it -- when Handle is not in the list retail unlinks and frees the list head
    // itself (.text:0628b68d..0628b69d). Retail also leaves +0x14 pointing at Owner.
    CObject* const Owner = DetachedPhantom->Owner;
    std::list<void*>::iterator It = Owner->Phantoms.begin();
    while (It != Owner->Phantoms.end() && *It != Handle)
        ++It;
    Owner->Phantoms.erase(It);

    // .text:0628b6a8..0628b6b1: FUN_06343940(phantom body, &Template); the template is
    // destroyed on the way out (thunk_FUN_06348670).
    IVP_Split_Object_From_Core(DetachedPhantom->Body, &Template);
    return 0;
}

bool __cdecl PhySys::GetCollisionStatePhantom(void* const Handle)
{
    // .text:0628b510..0628b530: a handle with a body -> (Body->flags (+0x78) & 0x300) != 0
    // (the collision-detection bits); otherwise false.
    if (Handle == nullptr)
        return false;
    IVP_Real_Object* const Body = ((Phantom*)Handle)->Body;
    if (Body == nullptr)
        return false;
    return (Body->flags & 0x300) != 0;
}

void __cdecl PhySys::SetCollisionStatePhantom(void* Handle, bool bEnable)
{
    // .text:0628b540..0628b55c: FUN_06344d50 = IVP_Real_Object::enable_collision_detection on
    // the body (not tested).
    if (Handle == nullptr)
        return;
    ((Phantom*)Handle)->Body->enable_collision_detection(bEnable);
}

// ============================================================================
// PhySys::CalcClosestPointOnSynapse -- FUN_0628b7a0 (Common.dll.txt:83125-83689,
// 502 insns, decomp-tracker item 768).
//
// Retail is file-local: there is no exported/decorated symbol at 0x0628b7a0 and
// exactly one XREF (.text:0628c13b, inside PhySys::GetClosestPhantomIntruderPos
// below), so it is ported as a file-local `static` here. The retail name comes
// from this function's own FDUMP format string in the default branch.
//
// WHAT IT DOES: given a query point in world space and one IVP_Synapse_Real
// (one half of an IVP_Mindist collision pair), produce the world-space point on
// that synapse's own geometric feature -- vertex, edge, triangle face or ball
// surface -- that is closest to the query point.
//
// ---------------------------------------------------------------------------
// [IVP] DEFERRAL RETIRED (2026-09-17, lane claude:common-animphys)
// ---------------------------------------------------------------------------
// This item's ported_name used to read "PhySys::CalcClosestPointOnSynapse
// (FULLPORT-DEFERRED[IVP])" on the grounds that src/IVP/ was an unported black
// box. That is no longer true; every primitive retail uses here now exists and
// is called directly below:
//   * FUN_0634d530 + FUN_06353510, and the `INC [EBX+4]` / `DEC [EBX+4]` pair
//     that brackets EVERY return path (0628b7d0 and 0628b830/b969/bc81/bcbd/
//     bce6/bd03/bd67/bdad/bdc1) -> IVP_Real_Object::ResolveNarrowPhaseCacheSlot()
//     / ReleaseNarrowPhaseCacheSlot() (ivp_real_object.h/.cpp). Retail's inline
//     prologue at 0628b7b1-0628b7ec is byte-for-byte ResolveNarrowPhaseCacheSlot's
//     own body (lazy IVP_AllocateCacheObject, refcount bump, then the
//     `(int8_t)flags < 8 && env->sim_step_count > Slot->LastRefreshTick` refresh
//     test), so it is expressed as the one call here rather than re-inlined.
//   * FUN_0634d8d0 -> IVP_TransformSlotToWorldSpace() (= Slot->WorldTransform.
//     vmult4, which writes exactly the 3 floats retail writes).
//   * FUN_0634ded0 -> the synapse's feature pointer masked to its owning
//     IVP_Compact_Triangle and then to its IVP_Compact_Ledge. Retail's body is
//     `Tri = this->Feature & ~0xf; Ledge = Tri - ((*(uint32_t*)Tri & 0xfff)+1)*16`
//     (Common.dll.txt:0634ded0-0634dee4), which is exactly
//     IVP_Compact_Edge::GetCompactLedge() / IVP_Compact_Triangle::
//     GetCompactLedge() in ivp_compact_ledge.h. NOTE for the src/IVP/ lane:
//     decomp-tracker item 3097 currently describes FUN_0634ded0 as a "pool
//     allocator" -- that reading is wrong, it allocates nothing.
//   * FUN_06345ea0 -> IVP_Real_Object::get_upper_limit_radius() (IVP_Core+0x04,
//     UpperLimitRadius).
//   * FUN_06347ef0 -> IVP_U_Point::normize()'s retail original (reproduced as
//     PhySys_Vec3Normize below so the >= 1e-10f threshold matches exactly).
//   * IVP_Compact_Ledge::GetPointArray()/GetPoint(), IVP_Compact_Edge::
//     GetStartPointIndex()/GetOppositeIndex(), IVP_Compact_Triangle::GetEdge()
//     -- all already modeled in ivp_compact_ledge.h and confirmed against this
//     function's own raw bit manipulation (see the per-case comments).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Retail leaf vector helpers, reproduced locally so the body below reads the way
// retail's does. All four-of-a-kind confirmed from raw x87, not from prose.
// ---------------------------------------------------------------------------

// FUN_0628bdf0 / FUN_0628bf60 (a COMDAT-folded pair -- retail emits both, same
// body): Dst = Src.
static void PhySys_Vec3Copy(Vector& Dst, Vector const& Src)
{
    Dst.x = Src.x;
    Dst.y = Src.y;
    Dst.z = Src.z;
}

// FUN_0628be10(this=Dst; Src, Scale): Dst = Src * Scale.
static void PhySys_Vec3Scale(Vector& Dst, Vector const& Src, float Scale)
{
    Dst.x = Src.x * Scale;
    Dst.y = Src.y * Scale;
    Dst.z = Src.z * Scale;
}

// FUN_0628be40(this=A; B): dot(A, B), returned in ST0 (x87, so double here). Order
// .text:0628be44..0628be56: (z*z' + y*y') + x*x'. Each caller decides whether the result is
// stored as float or kept on the stack.
static double PhySys_Vec3Dot(Vector const& A, Vector const& B)
{
    return ((double)(B.z) * A.z + (double)(B.y) * A.y) +
           (double)(B.x) * A.x;
}

// FUN_0628be60(this=Dst; A, B, Scale): Dst = A + B * Scale. Retail's callers pass
// Scale == -1.0f (0xbf800000) wherever they want a plain subtraction.
// Declared in common_physys_internal.h: PhySys::BeamBodyToWorldPose (common_controllergolem.cpp)
// calls it too.
void CmnPhySys::PhySys_Vec3AddMul(Vector& Dst, Vector const& A, Vector const& B, float Scale)
{
    // .text:0628be60..0628be8e: FLD Scale / FMUL B / FADD A on the x87 stack, one FSTP float
    // per component.
    float const Rx = (float)((double)(Scale) * B.x + A.x);
    float const Ry = (float)((double)(Scale) * B.y + A.y);
    float const Rz = (float)((double)(Scale) * B.z + A.z);
    Dst.x = Rx;
    Dst.y = Ry;
    Dst.z = Rz;
}

// FUN_0628bea0(this=Dst; A, B): Dst = A - B.
static void PhySys_Vec3Sub(Vector& Dst, Vector const& A, Vector const& B)
{
    float const Rx = A.x - B.x;
    float const Ry = A.y - B.y;
    float const Rz = A.z - B.z;
    Dst.x = Rx;
    Dst.y = Ry;
    Dst.z = Rz;
}

// FUN_0628bed0(this=Dst; A, B): Dst = A x B. Operand order confirmed by
// simulating the whole FPU stack (Common.dll.txt:0628bed8-0628bf0a): the y
// component is built as `b.x*a.z - a.x*b.z`, i.e. the standard right-handed
// cross product with A == the FIRST stack argument.
// The products and differences stay on the x87 stack (double); only the three FSTP float
// stores round. Every operand is read before the first store.
static void PhySys_Vec3Cross(Vector& Dst, Vector const& A, Vector const& B)
{
    double const Rz = (double)(A.x) * B.y - (double)(A.y) * B.x; // 0628bed8..0628bee0, 0628bf03
    double const Ry = (double)(B.x) * A.z - (double)(A.x) * B.z; // 0628bee2..0628beec
    double const Rx = (double)(A.y) * B.z - (double)(A.z) * B.y; // 0628beee..0628befa
    Dst.x = (float)(Rx);
    Dst.y = (float)(Ry);
    Dst.z = (float)(Rz);
}

// FUN_06347ef0(this=V): normalize in place unless |V|^2 is below 1e-10f, in which
// case V is left untouched. Returns 1 when it normalized, 0 when it bailed; every
// caller in this function discards the result. The threshold is DAT_063a53d8,
// decoded from its raw .rdata bytes (ff e6 db 2e == 0x2edbe6ff == 1e-10f), and
// the reciprocal numerator is DAT_063a53d0 (00 00 80 3f == 1.0f). Retail's
// compare is `TEST AH,0x1` + `JZ normalize`, i.e. C0 clear == NOT less-than ==
// qlen >= 1e-10f takes the normalizing path (ivp_math.h's IVP_U_Point::normize()
// spells the same test `qlen > 1e-10f`; the boundary case differs, so this stays
// a local reproduction rather than a call into that one).
// x87 throughout (.text:06347ef0..06347f41): QuadLen = (x*x + y*y) + z*z, FSQRT, FDIVR 1.0f,
// then each component is FMUL'd by the double reciprocal and stored as float.
static bool PhySys_Vec3Normize(Vector& V)
{
    double const QuadLen = ((double)(V.x) * V.x + (double)(V.y) * V.y) +
                           (double)(V.z) * V.z;
    if (!(QuadLen >= 1e-10f))   // C0 is set by "less" AND by "unordered", so NaN bails too.
        return false;
    double const Inv = 1.0 / sqrt(QuadLen);
    V.x = (float)(Inv * V.x);
    V.y = (float)(Inv * V.y);
    V.z = (float)(Inv * V.z);
    return true;
}

// FUN_0628bf10(this=Dst; A, B): FUN_0628bed0 immediately followed by
// `CALL FUN_06347ef0` (Common.dll.txt:0628bf4a) -- i.e. cross THEN normalize.
// NOTE for the tracker: item 768's own notes call FUN_0628bf10 a COMDAT
// duplicate of FUN_0628bed0. It is not; that trailing CALL is the whole
// difference, and dropping it would leave every face normal below unnormalized.
static void PhySys_Vec3CrossNormalize(Vector& Dst, Vector const& A, Vector const& B)
{
    PhySys_Vec3Cross(Dst, A, B);
    PhySys_Vec3Normize(Dst);
}

// FUN_062c4780 (decomp-tracker item 1281, VecMagSq3): |V|^2 = (x*x + y*y) + z*z, returned in
// ST0 (double).
static double PhySys_Vec3QuadLength(Vector const& V)
{
    return ((double)(V.x) * V.x + (double)(V.y) * V.y) +
           (double)(V.z) * V.z;
}

// FUN_0628bf80(P, A, B, Out): closest point on the SEGMENT A..B to P.
// Fully traced (Common.dll.txt:0628bf80-0628c07b): t = -dot(A - P, B - A) /
// |B - A|^2, clamped to [0,1] by the same two-step retail idiom used in case 1
// below, then Out = A + (B - A) * t. Retail writes Out.y, Out.z, Out.x in that
// order; immaterial because Out never aliases A or B at any call site.
// x87 detail (each value is double unless the listing FSTPs it to a float):
//   Dy = A.y - P.y is stored as float (0628bfa6); Dz and Dx stay on the stack.
//   Ex, Ey are stored as float (0628bfbb / 0628bfc5); Ez is FST'd to float but its
//   unrounded double is what multiplies Dz (0628bfcf FST, 0628bfd3 FMULP).
//   S = (Dz*Ez + Ey_f*Dy_f) + Ex_f*Dx; L = (Ez_f^2 + Ey_f^2) + Ex_f^2; T = -(S / L), double.
//   The clamp constants are 0.0f (DAT_06399408) and 1.0f (DAT_0639a1d0).
static void PhySys_ClosestPointOnSegment(Vector const& P, Vector const& A, Vector const& B, Vector& Out)
{
    float const DyF = (float)((double)(A.y) - P.y);  // 0628bfa3..0628bfa6
    double const Dz = (double)(A.z) - P.z;                      // 0628bfaa
    double const Dx = (double)(A.x) - P.x;                      // 0628bfad..0628bfb1
    float const ExF = (float)((double)(B.x) - A.x);  // 0628bfb7..0628bfbb
    float const EyF = (float)((double)(B.y) - A.y);  // 0628bfbf..0628bfc5
    double const Ez = (double)(B.z) - A.z;                      // 0628bfc9..0628bfcc
    float const EzF = (float)(Ez);                              // 0628bfcf FST

    double const S = (Dz * Ez + (double)(EyF) * DyF) + (double)(ExF) * Dx;
    double const L = ((double)(EzF) * EzF + (double)(EyF) * EyF) +
                     (double)(ExF) * ExF;
    double T = -(S / L);                                                    // 0628c003..0628c007
    // 0628c009-0628c03a. Strictly-ordered comparisons, matching retail's
    // `TEST AH,0x5 / JNP` (strictly less) + `TEST AH,0x41 / JNZ` (less, equal or
    // unordered) pair: a NaN T takes neither branch and passes through
    // unclamped, which a `!(T >= 0)`-style guard would not reproduce.
    if (T < 0.0f)
        T = 0.0f;
    else if (T > 1.0f)
        T = 1.0f;

    // 0628c040..0628c073: Ey_f*T + A.y, Ez_f*T + A.z, Ex_f*T + A.x, each stored as float.
    float const OutY = (float)((double)(EyF) * T + A.y);
    float const OutZ = (float)((double)(EzF) * T + A.z);
    float const OutX = (float)((double)(ExF) * T + A.x);
    Out.y = OutY;
    Out.z = OutZ;
    Out.x = OutX;
}

// ---------------------------------------------------------------------------
// The synapse this function reads is IVP_Synapse_Real (ivp_mindist.h, laid out
// and asserted from the listing since 2026-10-08): l_obj at +0x10 (0628b7b1 /
// 0628bd14 / caller 0628c0ef), edge at +0x14 (0628b805; masked with ~0xf for
// type 2, unused for type 3), status at +0x1a (MOVSX'd switch key, 0628b7ee).
// The caller picks one of a mindist's two synapses at Mindist+0x18 / +0x34
// (0628c0e7 / 0628c0ec). (A local PhySysSynapseView duplicate stood here while
// the IVP declaration was wrong; removed by cmn-phy5.)
// ---------------------------------------------------------------------------

// Feature-type switch keys. Retail compares the MOVSX'd int16 with `CMP EAX,3` /
// `JA default`, so anything outside 0..3 (including every negative value, which
// the unsigned JA catches) falls into the FDUMP branch.
enum EPhySysSynapseFeature
{
    kPhySysSynapseVertex = 0,
    kPhySysSynapseEdge   = 1,
    kPhySysSynapseFace   = 2,
    kPhySysSynapseBall   = 3
};

// Helper: retail's FUN_0634d8d0 call, which transforms one compact-mesh vertex
// (an IVP_U_Float_Point, 16 bytes, of which only the first 3 floats are read)
// from the cached slot's local space into world space, writing exactly 3 floats.
static void PhySys_SlotPointToWorld(IVP_NarrowPhaseCacheSlot* Slot,
                                     IVP_U_Float_Point const* LocalPoint, Vector& OutWorld)
{
    // The IVP helper is declared over IVP_U_Point; only the first three floats of either
    // side are touched.
    IVP_TransformSlotToWorldSpace(Slot, (IVP_U_Point const*)LocalPoint, (IVP_U_Point*)&OutWorld);
}

static void PhySys_CalcClosestPointOnSynapse(Vector const& QueryPos, IVP_Synapse_Real const* Syn, Vector& Out)
{
    IVP_Real_Object* const Object = Syn->l_obj;

    // 0628b7b4-0628b7ec: lazily claim + refresh this object's narrow-phase cache
    // slot and take a reference on it. Every return path below drops it again.
    IVP_NarrowPhaseCacheSlot* const Slot = Object->ResolveNarrowPhaseCacheSlot();

    switch ((int)Syn->status)
    {
    case kPhySysSynapseVertex:
    {
        // caseD_0, 0628b805-0628b841. `Ledge->GetPointArray()[Edge->
        // GetStartPointIndex()]`, transformed to world space. Retail open-codes
        // the index as `(*(uint32_t*)Edge & 0xffff) << 4` added to the ledge's
        // own c_point_offset -- a 16-byte stride, i.e. IVP_U_Float_Point.
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge), Out);
        break;
    }

    case kPhySysSynapseEdge:
    {
        // caseD_1, 0628b842-0628b97a. A = this edge's start point, B = the start
        // point of `Edge[Edge->GetOppositeIndex()]`. Retail extracts the opposite
        // index as `((int)(Data * 2)) >> 17` (LEA EBP,[ECX+ECX*1] / SAR EBP,0x11
        // at 0628b85f/0628b868) -- the sign-extended 15-bit field at bits 16..30,
        // exactly IVP_Compact_Edge::GetOppositeIndex().
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();

        Vector A;
        Vector B;
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge), A);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge->GetOppositeEdge()), B);

        Vector AMinusQuery;
        PhySys_Vec3Sub(AMinusQuery, A, QueryPos);
        Vector Dir;
        PhySys_Vec3Sub(Dir, B, A);

        // 0628b8e5-0628b90b: the numerator dot is FSTP'd to a float (0628b8f3), the
        // denominator dot stays on the stack: T = -(Num_f / Den), FST'd to the float
        // TF (0628b90b) while the double T is what the first compare sees.
        float const NumF = (float)(PhySys_Vec3Dot(AMinusQuery, Dir));
        double const T = -((double)(NumF) / PhySys_Vec3Dot(Dir, Dir));
        float TF = (float)(T);
        // 0628b90f-0628b94e. `FCOMP 0` on the double T, `TEST AH,0x5 / JNP` -> clamp when
        // strictly less; else `FLD TF; FCOMP 1.0`, `TEST AH,0x41 / JNZ` keeps TF when less,
        // equal or unordered (a NaN passes unclamped). The clamp path (0628b92d) sets
        // TF = 0 and then re-tests the OLD TF against 0: strictly less keeps 0, anything
        // else stores 1.0f.
        if (T < 0.0 || TF > 1.0f)
        {
            float const OldTF = TF;
            TF = 0.0f;
            if (!(OldTF < 0.0f))
                TF = 1.0f;
        }

        PhySys_Vec3AddMul(Out, A, Dir, TF);
        break;
    }

    case kPhySysSynapseFace:
    {
        // caseD_2, 0628b97b-0628bd13. The triangle is the synapse's feature
        // pointer masked down to its own 16-byte IVP_Compact_Triangle
        // (`AND ECX,0xfffffff0`, 0628b987); V0/V1/V2 come from that triangle's
        // three edges at +0x4/+0x8/+0xc.
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();
        IVP_Compact_Triangle const* const Tri = Edge->GetCompactTriangle();

        Vector V0;
        Vector V1;
        Vector V2;
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(0)), V0);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(1)), V1);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(2)), V2);

        Vector Edge0;  // V1 - V0
        Vector Edge1;  // V2 - V0
        Vector Edge2;  // V2 - V1  -- retail computes it (0628ba17) and never reads it again.
        PhySys_Vec3AddMul(Edge0, V1, V0, -1.0f);
        PhySys_Vec3AddMul(Edge1, V2, V0, -1.0f);
        PhySys_Vec3AddMul(Edge2, V2, V1, -1.0f);
        (void)Edge2;

        // 0628ba32-0628ba5c: Normal = normalize(Edge1 x Edge0); Tangent =
        // Normal x Edge1. Note the operand order -- cross(Edge1, Edge0), not
        // cross(Edge0, Edge1).
        Vector Normal;
        PhySys_Vec3CrossNormalize(Normal, Edge1, Edge0);
        Vector Tangent;
        PhySys_Vec3Cross(Tangent, Normal, Edge1);

        // 0628ba61-0628ba8a. The two guard constants are DAT_0639c398 and
        // DAT_0639b984, decoded from raw .rdata bytes (ac c5 27 b7 == -1e-5f and
        // ac c5 27 37 == +1e-5f). Retail's pair is `FCOM -1e-5 / TEST AH,0x41 /
        // JNZ proceed` then `FCOM +1e-5 / TEST AH,0x5 / JNP bail`, i.e. it bails
        // only when BOTH compares are ordered and -1e-5 < Denom < +1e-5 (an
        // unordered/NaN Denom proceeds, which the && below reproduces). On the
        // bail path Out is left completely untouched -- retail's LAB_0628bdbf
        // just drops the cache-slot reference and returns.
        // Denom stays on the x87 stack (double) for both compares and the division.
        double const Denom = PhySys_Vec3Dot(Edge0, Tangent);
        if (Denom > -1e-5f && Denom < 1e-5f)
            break;

        // 0628ba90..0628baae: FLD 1.0f / FDIV ST0,ST1 / FSTP float.
        float const InvDenom = (float)(1.0 / Denom);

        Vector QueryMinusV0;
        PhySys_Vec3AddMul(QueryMinusV0, QueryPos, V0, -1.0f);

        // 0628bab9-0628bb02: the two barycentric weights, each a double dot times the float
        // InvDenom, stored as float (0628bad7 / 0628bb02).
        float const B1 = (float)(PhySys_Vec3Dot(QueryMinusV0, Tangent) * InvDenom);
        Vector CrossQE0;
        PhySys_Vec3Cross(CrossQE0, QueryMinusV0, Edge0);
        float const B2 = (float)(PhySys_Vec3Dot(Normal, CrossQE0) * InvDenom);

        // 0628bb06-0628bb47: Out = V0*(1-B1-B2) + V1*B1 + V2*B2, the query point
        // projected onto the triangle's plane. The weight is FLD 1.0f / FSUB B1 / FSUB B2
        // on the stack, then FSTP float (0628bb1a).
        PhySys_Vec3Scale(Out, V0, (float)((1.0 - B1) - B2));
        PhySys_Vec3AddMul(Out, Out, V1, B1);
        PhySys_Vec3AddMul(Out, Out, V2, B2);

        // 0628bb47-0628bb9e: inside-test. Retail's five compares are all
        // STRICTLY-ORDERED "is it outside" tests -- B1 < 0, B1 > 1, B2 < 0,
        // B2 > 1, B1 + B2 > 1 -- and the last one jumps to the epilogue
        // (LAB_0628bdc1) on "less, equal OR unordered", so NaN weights land on
        // the inside path and keep the planar projection. Expressed as the
        // outside-predicate for exactly that reason.
        bool const bOutside = (B1 < 0.0f) || (B1 > 1.0f) ||
                              (B2 < 0.0f) || (B2 > 1.0f) ||
                              (((double)(B2) + B1) > 1.0f); // 0628bb8b FLD/FADD: double sum
        if (!bOutside)
            break;

        // LAB_0628bba4, 0628bba4-0628bd13. Outside the triangle: take the closest
        // point on each of the three edges TO THE PLANAR PROJECTION (not to the
        // original query point -- retail passes ESI == Out at all three call
        // sites) and keep whichever is nearest to it.
        Vector CandV0V1;
        Vector CandV0V2;
        Vector CandV1V2;
        PhySys_ClosestPointOnSegment(Out, V0, V1, CandV0V1);
        PhySys_ClosestPointOnSegment(Out, V0, V2, CandV0V2);
        PhySys_ClosestPointOnSegment(Out, V1, V2, CandV1V2);

        Vector Delta;
        PhySys_Vec3AddMul(Delta, Out, CandV0V1, -1.0f);
        // D1 and D2 are FSTP'd to floats (0628bc37 / 0628bc47); D3 stays on the x87 stack
        // (double) for every compare it takes part in.
        float const D1 = (float)(PhySys_Vec3QuadLength(Delta));
        PhySys_Vec3AddMul(Delta, Out, CandV0V2, -1.0f);
        float const D2 = (float)(PhySys_Vec3QuadLength(Delta));
        PhySys_Vec3AddMul(Delta, Out, CandV1V2, -1.0f);
        double const D3 = PhySys_Vec3QuadLength(Delta);

        // 0628bc57-0628bd13. Each test is `TEST AH,0x41` + `JP`, i.e. "greater
        // OR unordered jumps away", so the fall-through (kept) case is
        // `<=` -- an ordered `<=`, because an unordered (NaN) compare also takes the JP.
        // The final `else` (0628bcf8..0628bd13) is IVP's core trap: `MOV dword ptr
        // [0],0` at 0628bcf9, then the slot reference is dropped and Out keeps the
        // planar projection. With ordered distances it is unreachable (D3 > D1 > D2
        // would have taken the second branch); only a NaN distance reaches it.
        if (D1 <= D2 && D1 <= D3)          // 0628bc57..0628bc71 (D3 is the double on the stack)
            PhySys_Vec3Copy(Out, CandV0V1);
        else if (D2 <= D1 && D2 <= D3)     // 0628bc93..0628bcad
            PhySys_Vec3Copy(Out, CandV0V2);
        else if (D3 <= D1)                 // 0628bccf..0628bcd8
            PhySys_Vec3Copy(Out, CandV1V2);
        else
            IVP_CoreTrap();                // 0628bcf9
        break;
    }

    case kPhySysSynapseBall:
    {
        // caseD_3, 0628bd14-0628bd78. Out = Center - normalize(Center - P) *
        // Radius, i.e. the point on the ball's surface facing the query point.
        // Center is the core's world transform translation (IVP_Core::
        // MWorldFCore.vv, core+0x124). Radius is FUN_06345ea0 on the object
        // (IVP_Real_Object::get_upper_limit_radius, IVP_Core+0x04).
        IVP_Core* const Core = Object->core;
        Vector Center;
        Center.x = Core->MWorldFCore.vv.k[0];
        Center.y = Core->MWorldFCore.vv.k[1];
        Center.z = Core->MWorldFCore.vv.k[2];

        PhySys_Vec3Copy(Out, Center);

        Vector Dir;
        PhySys_Vec3Sub(Dir, Out, QueryPos);
        PhySys_Vec3Normize(Dir);

        // 0628bd4d..0628bd57: ECX = the object (EDI); CALL 06345ea0; FCHS; FSTP float.
        float const Radius = Object->get_upper_limit_radius();
        PhySys_Vec3AddMul(Out, Out, Dir, -Radius);
        break;
    }

    default:
        // caseD_4, 0628bd80-0628bdbe. Retail's own error text and line number.
        // The trailing zero-fill is AmbientScriptInfo::clear (0x062ee230),
        // COMDAT-folded onto this call site -- it just zeroes the 3 floats. FDUMP is called
        // through the import without a test (0628bd80 / 0628bd9c).
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: PhySys::CalcClosestPointOnSynapse(): Unknown IVP_Synapse type (%d).",
              kPhySysSource, 0x6d6, (int)Syn->status);
        Out.x = 0.0f;
        Out.y = 0.0f;
        Out.z = 0.0f;
        break;
    }

    Object->ReleaseNarrowPhaseCacheSlot(Slot);
}

bool __cdecl PhySys::GetClosestPhantomIntruderPos(void* Handle, unsigned FilterA, unsigned FilterB,
                                                    CSimple*& OutObject, Vector& OutPos)
{
    // .text:0628c080..0628c097: a null handle, or a phantom body that is not a ball
    // (IVP_Object::object_type +0x04 != 3), -> false. Out parameters are written only when a
    // closer intruder is found.
    if (Handle == nullptr)
        return false;
    IVP_Real_Object* const Body = ((Phantom*)Handle)->Body;
    if (Body->object_type != 3)
        return false;

    // .text:0628c0a0..0628c0a5: FUN_06345e60 on the body -- the core's world translation.
    IVP_U_Point QueryPoint;
    Body->get_core_position_ws(&QueryPoint);
    Vector QueryPos;
    QueryPos.x = QueryPoint.x;
    QueryPos.y = QueryPoint.y;
    QueryPos.z = QueryPoint.z;

    // .text:0628c0aa..0628c0be: the phantom controller's mindist set (first_child +0x10),
    // walked from slot SizeMm (+0x14) down to 0; Best = FLT_MAX (0x7f7fffff).
    struct FVHashSlot
    {
        unsigned HashIndex;
        IVP_Mindist_Base* Elem;
    };
    IVP_Controller_Phantom* const Controller = (IVP_Controller_Phantom*)Body->first_child;
    float BestDistSq = FLT_MAX;
    for (int Slot = Controller->mindist_set.SizeMm; Slot >= 0; --Slot)
    {
        // .text:0628c0c2..0628c0db
        IVP_Mindist_Base* const Mindist = ((FVHashSlot*)Controller->mindist_set.Elems)[Slot].Elem;
        if (Mindist == nullptr)
            continue;

        // .text:0628c0dd..0628c0ef: the intruder's synapse -- +0x34 when the synapse at +0x18
        // belongs to the phantom body (its l_obj, +0x28), else +0x18.
        IVP_Synapse_Real const* Synapse = &Mindist->synapse[0];
        if (Synapse->l_obj == Body)
            Synapse = &Mindist->synapse[1];

        // .text:0628c0ef..0628c0f7: an intruder that is itself a phantom (+0x1c) is skipped.
        IVP_Real_Object* const Intruder = Synapse->l_obj;
        if (Intruder->first_child != nullptr)
            continue;

        // .text:0628c0f9..0628c12e: the root of its client must exist, have class bits 3
        // (+0x4c & 3, compared as a byte), share a type bit with FilterA (+0xe0) and not carry
        // the id FilterB (+0xb0).
        CObject* const Root = GetRoot((CObject*)Intruder->client_data);
        if (Root == nullptr)
            continue;
        if ((uint8_t)((unsigned)Root->ObjectClass & 3u) != 3)
            continue;
        CSimple* const Simple = (CSimple*)Root;
        if ((Simple->TypeBits & FilterA) == 0)
            continue;
        if (Simple->Id == FilterB)
            continue;

        // .text:0628c130..0628c13b: FUN_0628b7a0, the closest point on the intruder's feature.
        Vector Closest;
        PhySys_CalcClosestPointOnSynapse(QueryPos, Synapse, Closest);

        // .text:0628c140..0628c16f: DistSq = (dy*dy + dz*dz) + dx*dx on the stack.
        const double Dx = (double)(Closest.x) - QueryPos.x;
        const double Dy = (double)(Closest.y) - QueryPos.y;
        const double Dz = (double)(Closest.z) - QueryPos.z;
        const double DistSq = (Dy * Dy + Dz * Dz) + Dx * Dx;

        // .text:0628c171..0628c1ba: FCOM float Best / TEST AH,0x5 / JP -- only a strictly
        // smaller distance (C0 set, C2 clear) is taken: Best = (float)DistSq, the point and
        // the root go out.
        if (DistSq < BestDistSq)
        {
            BestDistSq = (float)(DistSq);
            OutPos = Closest;
            OutObject = Simple;
        }
    }

    // .text:0628c1bf..0628c1d9: true when Best < FLT_MAX (DAT_0639c39c, FCOMP / TEST AH,0x5 /
    // JP -> false).
    return BestDistSq < FLT_MAX;
}

