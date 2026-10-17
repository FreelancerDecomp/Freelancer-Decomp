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
// SphereIntruderChecker
// ============================================================================

// .text:06332470..0633249e: the list member's default constructor (operator new(0xc) head,
// self-linked, size 0), then the vftable 063a4964.
SphereIntruderChecker::SphereIntruderChecker()
{
}

// .text:063324a0..06332521: the list member's copy constructor -- a fresh head, then every
// element of Other's list appended in order (insert before the head, ++size).
SphereIntruderChecker::SphereIntruderChecker(SphereIntruderChecker const& Other)
    : Intruders(Other.Intruders)
{
}

// .text:06332610..0633265e: the list member's destructor (each node unlinked and freed, then the
// head; head / size zeroed).
SphereIntruderChecker::~SphereIntruderChecker()
{
}

// .text:06332530..06332600: `CMP &Intruders, &Other.Intruders; JZ` -> nothing; otherwise the VC6
// list assignment: values copied over the existing nodes (06332560..06332570), the surplus
// erased (06332578..0633259d), the rest of Other appended (063325b0..063325eb). The vftable and
// the allocator byte are not touched. Returns *this.
SphereIntruderChecker& SphereIntruderChecker::operator=(SphereIntruderChecker const& Other)
{
    if (&Intruders != &Other.Intruders)
    {
        Intruders = Other.Intruders;
    }
    return *this;
}

// .text:06332420..0633245f (RET 4): push_back on the list. VC6 _Node is {_Next +0, _Prev +4,
// _Value +8}: node->_Next = head, node->_Prev = head->_Prev, head->_Prev = node,
// node->_Prev->_Next = node, value = Object, ++size -- inserted before the head, at the end.
void SphereIntruderChecker::added(CObject* Object)
{
    Intruders.push_back(Object);
}

// The vftable's slot 1 (.rdata:063a4968 -> 062ed730, `RET 4`, ICF-shared): nothing.
void SphereIntruderChecker::deleted(CObject* /*Object*/)
{
}

// .text:06332660..063327dd (thiscall RET 0x10).
//   06332668..06332688  erase every node of Intruders (clear)
//   0633268a..063326f7  PhantomParms on the stack (local_44): +0x00 SystemId, +0x04 / +0x05 /
//                       +0x06 bytes 0 (empty name), +0x10 Center, +0x1c the identity 3x3 (1.0f
//                       at +0x1c / +0x2c / +0x3c, 0 elsewhere), +0x40 = this (the listener);
//                       +0x07..+0x0f are not written
//   063326fb            PhySys::CreatePhantom(Parms, Radius, Handle) -- the sphere overload
//   06332700..06332711  SetCollisionStatePhantom(Handle, true), then DestroyPhantom(Handle); the
//                       phantom's object set reports each intruder through added() while the
//                       state is switched on
//   06332716..06332774  adjacent duplicates removed (node value == next node value -> the next
//                       node is unlinked and freed, --size) -- list::unique
//   06332776..063327d1  if (&Out != &Intruders) Out = Intruders (VC6 list::operator=, inlined)
// The result of CreatePhantom is not tested.
void SphereIntruderChecker::check_sphere(unsigned SystemId, Vector const& Center, float Radius,
                                         std::list<CObject*>& Out)
{
    Intruders.clear();

    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));  // +0x07..+0x0f: uninitialised stack in retail; never read past the NUL at +0x06
    Parms.SystemId = SystemId;
    Parms.Flag04 = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Position = Center;
    Parms.Orientation.E[0][0] = 1.0f;
    Parms.Orientation.E[0][1] = 0.0f;
    Parms.Orientation.E[0][2] = 0.0f;
    Parms.Orientation.E[1][0] = 0.0f;
    Parms.Orientation.E[1][1] = 1.0f;
    Parms.Orientation.E[1][2] = 0.0f;
    Parms.Orientation.E[2][0] = 0.0f;
    Parms.Orientation.E[2][1] = 0.0f;
    Parms.Orientation.E[2][2] = 1.0f;
    Parms.Listener = this;

    void* Handle = nullptr;  // local_4c; CreatePhantom writes it on every path
    PhySys::CreatePhantom(Parms, Radius, Handle);
    PhySys::SetCollisionStatePhantom(Handle, true);
    PhySys::DestroyPhantom(Handle);

    Intruders.unique();

    if (&Out != &Intruders)
    {
        Out = Intruders;
    }
}

