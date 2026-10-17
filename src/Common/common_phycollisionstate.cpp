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

namespace
{
    // .rdata:0639b924 (PhyCollisionStateManager::mindist_left_volume, .text:0628179d).
    const char kPhyCollisionStateManagerSource[] = "E:\\FL\\Scratch\\Source\\Common\\PhyCollisionStateManager.cpp";
}

// ============================================================================
// PhySys::PhyCollisionStateManager
// ============================================================================

PhySys::PhyCollisionStateManager::PhyCollisionStateManager(CObject* Object)
    : OwnerObject(Object)
    , IntruderCount(0)
    , IntruderTime(0.0f)
    , CollisionType(0)
{
    // Pad11 (+0x11..+0x13) is not written, as in retail.
    // .text:062814b0..062814e4: +0x04 = Object, +0x08 / +0x0c = 0, byte +0x10 = 0, vtable;
    // then CAsteroid::asteroidarch(Object) (the ICF-folded `MOV EAX,[this+0x88]`, Object not
    // tested) and, with an archetype, byte +0x10 = Root byte +0x39 (phantom_physics).
    Archetype::Root const* const Arch = Object->Archetype;
    if (Arch != nullptr)
        CollisionType = (unsigned char)Arch->bPhantomPhysics;
}

// .text:06280aa0..06280aca: vptr, the dwords +0x04 / +0x08 / +0x0c and the byte +0x10; the
// padding +0x11..+0x13 is not copied.
PhySys::PhyCollisionStateManager::PhyCollisionStateManager(PhyCollisionStateManager const& Other)
    : OwnerObject(Other.OwnerObject)
    , IntruderCount(Other.IntruderCount)
    , IntruderTime(Other.IntruderTime)
    , CollisionType(Other.CollisionType)
{
}

PhySys::PhyCollisionStateManager::~PhyCollisionStateManager()
{
    // .text:062814f0..0628150f: Body = OwnerObject->BodyPtr (the owner is not tested); a body
    // with a phantom controller (+0x1c) has this manager removed from the phantom's listener
    // vector: FUN_06344260(ECX = phantom, this).
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return;
    IVP_Controller_Phantom* const Phantom = (IVP_Controller_Phantom*)Body->first_child;
    if (Phantom == nullptr)
        return;
    Phantom->remove_listener_phantom(this);                       // .text:06281509..0628150a
}

// .text:06280ad0..06280aee (ICF-shared with CPhysControllerEquip::operator=): the dwords
// +0x04 / +0x08 / +0x0c and the byte +0x10, no self test, padding not copied.
PhySys::PhyCollisionStateManager& PhySys::PhyCollisionStateManager::operator=(
    PhyCollisionStateManager const& Other)
{
    OwnerObject = Other.OwnerObject;
    IntruderCount = Other.IntruderCount;
    IntruderTime = Other.IntruderTime;
    CollisionType = Other.CollisionType;
    return *this;
}

// .text:06281510..06281620 (thiscall, RET 8).
void PhySys::PhyCollisionStateManager::enable_collisions(bool bEnable, bool bImmediate)
{
    // .text:06281517..06281522: Body = this->OwnerObject->BodyPtr; none -> return.
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return;

    // .text:06281528..0628153b: AND EDX,0x300; CMP EDX,0x100; SETZ BL -- collision detection
    // is enabled exactly when the two location bits read 01.
    bool const bWasEnabled = (Body->flags & 0x300u) == 0x100u;

    if (!bEnable)
    {
        // .text:0628160a..06281617: only an enabled body is switched off.
        if (bWasEnabled)
        {
            Body->enable_collision_detection(false);   // .text:0628160e..0628160f FUN_06344d50(0)
            IntruderCount = 0;                         // .text:06281614
            IntruderTime = 0.0f;                       // .text:06281617 (dword 0)
        }
        return;
    }

    // .text:06281546..06281553: neither immediate nor a phantom-physics archetype -> the
    // deferred path: drop the collision phantom and enable detection.
    if (!bImmediate && CollisionType == 0)
    {
        // .text:06281555..06281562: with a phantom (Body+0x1c), FUN_06345eb0 turns the body
        // back into an object, then IntruderCount / IntruderTime = 0.
        if (Body->first_child != nullptr)
        {
            Body->convert_phantom_to_object();         // .text:0628155a
            IntruderCount = 0;
            IntruderTime = 0.0f;
        }
        // .text:06281565..06281580: an already enabled body is left as it is.
        if (bWasEnabled)
            return;
        // .text:0628156d..06281575: OwnerObject->BodyPtr re-read, FUN_06344d50(1).
        OwnerObject->BodyPtr->enable_collision_detection(true);
        return;
    }

    // .text:06281583..06281585: immediate (or phantom-physics) path; nothing to do when the
    // body is already enabled.
    if (bWasEnabled)
        return;

    // .text:0628158b..062815bb: without a phantom one is built: an IVP_Template_Phantom on the
    // stack (FUN_06344210) whose client_data (+0x14, local_4) is 1 (.text:062815a0),
    // FUN_06345f10 on the re-read body converts it to a phantom, and this manager becomes a
    // listener of the re-read body's phantom (FUN_06344230).
    if (Body->first_child == nullptr)
    {
        IVP_Template_Phantom Template;
        Template.client_data = (void*)(uintptr_t)1;
        OwnerObject->BodyPtr->convert_to_phantom(&Template);
        IVP_Real_Object* const Converted = OwnerObject->BodyPtr;
        ((IVP_Controller_Phantom*)Converted->first_child)->add_listener_phantom(this);
    }

    // .text:062815c0..062815c8: OwnerObject->BodyPtr re-read, FUN_06344d50(1).
    OwnerObject->BodyPtr->enable_collision_detection(true);

    // .text:062815cd..062815fe: re-read the body; with a phantom, a non-phantom archetype,
    // detection enabled and nobody inside, the phantom is turned back into an object again
    // (FUN_06345eb0) and IntruderTime = 0 (IntruderCount is already 0).
    IVP_Real_Object* const BodyAfter = OwnerObject->BodyPtr;
    if (BodyAfter == nullptr || BodyAfter->first_child == nullptr)
        return;
    if (CollisionType != 0)
        return;
    if ((BodyAfter->flags & 0x300u) != 0x100u)
        return;
    if (IntruderCount != 0)
        return;
    BodyAfter->convert_phantom_to_object();            // .text:062815f9
    IntruderTime = 0.0f;                               // .text:062815fe
}

bool PhySys::PhyCollisionStateManager::get_desired_collision_state() const
{
    // .text:06281630..0628164f: Body = OwnerObject->BodyPtr (the owner is not tested); no body
    // -> false; else (Body->flags & 0x300) == 0x100 (SUB / NEG / SBB / INC).
    IVP_Real_Object const* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return false;
    return (Body->flags & 0x300u) == 0x100u;
}

bool PhySys::PhyCollisionStateManager::get_actual_collision_state() const
{
    // .text:06281650..0628167a: Body = OwnerObject->BodyPtr; no body or detection off -> false;
    // else true exactly when the body has no phantom controller (+0x1c, SETZ).
    IVP_Real_Object const* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return false;
    if ((Body->flags & 0x300u) != 0x100u)
        return false;
    return Body->first_child == nullptr;
}

void PhySys::PhyCollisionStateManager::update(float Dt)
{
    // .text:06281680..062816ce (thiscall, RET 4).
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)                      // .text:06281689
        return;
    if (Body->first_child == nullptr)         // .text:0628168d: no phantom controller
        return;
    if (CollisionType != 0)                   // .text:06281694
        return;
    if ((Body->flags & 0x300u) != 0x100u)     // .text:0628169b..062816aa
        return;
    if (IntruderCount == 0)                   // .text:062816ac..062816b1
    {
        Body->convert_phantom_to_object();    // .text:062816b3 FUN_06345eb0
        IntruderTime = 0.0f;                  // .text:062816b8 (dword 0)
        return;
    }
    IntruderTime += Dt;                       // .text:062816c3..062816ca FLD / FADD / FSTP float
}

namespace
{
    // `MindistBase->vtbl[1](&Pair)` at the top of every PhyCollisionStateManager callback:
    // slot 1 of IVP_Mindist_Base (ivp_mindist.h) is get_objects (thiscall, one out-array
    // argument).
    void PhySys_GetMindistObjects(IVP_Mindist_Base* MindistBase, IVP_Real_Object* OutPair[2])
    {
        MindistBase->get_objects(OutPair);
    }

    // The shared selection: the pair member that is not the owner -- Pair[1] when
    // Pair[0]->client_data (+0xa4) is the owner, else Pair[0].
    IVP_Real_Object* PhySys_SelectOther(IVP_Real_Object* const Pair[2], CObject const* Owner)
    {
        return (Owner == (CObject*)Pair[0]->client_data) ? Pair[1] : Pair[0];
    }

    // The intruder filter of .text:06281707..06281726 / 06281767..06281786: an object without
    // a client is ignored; when the owner's body location byte (+0x78) is 9, an intruder whose
    // own location byte (MOVSX) is 0x10 or 9 is ignored as well.
    bool PhySys_CountsAsIntruder(CObject const* Owner, IVP_Real_Object const* Other)
    {
        if (Other->client_data == nullptr)
            return false;
        IVP_Real_Object const* const OwnerBody = Owner->BodyPtr;
        if ((uint8_t)OwnerBody->flags != 9)
            return true;
        int8_t const OtherLocation = (int8_t)Other->flags;
        return OtherLocation != 0x10 && OtherLocation != 9;
    }
}

// .text:062816e0..0628172f (thiscall, RET 8).
void PhySys::PhyCollisionStateManager::mindist_entered_volume(IVP_Controller_Phantom* /*PhantomCtrl*/, IVP_Mindist_Base* MindistBase)
{
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);                 // .text:062816e6..062816f1
    IVP_Real_Object* const Other = PhySys_SelectOther(Pair, OwnerObject);
    if (PhySys_CountsAsIntruder(OwnerObject, Other))
        ++IntruderCount;                                          // .text:06281728
}

// .text:06281740..062817b6 (thiscall, RET 8).
void PhySys::PhyCollisionStateManager::mindist_left_volume(IVP_Controller_Phantom* /*PhantomCtrl*/, IVP_Mindist_Base* MindistBase)
{
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);                 // .text:06281746..06281751
    IVP_Real_Object* const Other = PhySys_SelectOther(Pair, OwnerObject);
    if (!PhySys_CountsAsIntruder(OwnerObject, Other))
        return;
    // .text:06281788..062817af: DEC dword +0x8; JNS -> done; a negative count is reported.
    --IntruderCount;
    if ((int)IntruderCount < 0)
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhyCollisionStateManagerSource, 0x11d,
              "Negative intruder count!");
}

void PhySys::PhyCollisionStateManager::phantom_is_going_to_be_deleted_event(IVP_Controller_Phantom* PhantomCtrl)
{
    // ICF-folded with CostumeDescriptions::warn @ 0x8D730.
    // Empty body — just returns.
    (void)PhantomCtrl;
}

IVP_Real_Object* PhySys::PhyCollisionStateManager::get_other_object(IVP_Mindist_Base* MindistBase)
{
    // .text:06281940..0628196b (thiscall, RET 4): get_objects, then the non-owner member.
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    return PhySys_SelectOther(Pair, OwnerObject);
}

CObject* PhySys::PhyCollisionStateManager::mindist_to_cobject(IVP_Mindist_Base* MindistBase)
{
    // .text:06281970..062819a1 (thiscall, RET 4): get_objects, the non-owner member, its
    // client (+0xa4).
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    return (CObject*)PhySys_SelectOther(Pair, OwnerObject)->client_data;
}

// .text:062817c0..06281933 (thiscall, RET 8).
int PhySys::PhyCollisionStateManager::get_intruding_cobjs(bool bRoot, CheapSet<CObject*, std::less<CObject*>>& Out)
{
    // An IVP_VHash slot: {hash index, element} (FUN_06352580 reads the element at +4 of an
    // 8-byte slot). TODO: IVP_VHash exposes no typed slot accessor yet; this view of its
    // Elems array is the only use outside ivp_vhash.h.
    struct FVHashSlot
    {
        unsigned HashIndex;
        IVP_Mindist_Base* Elem;
    };

    // .text:062817c6..062817e0: no body or no phantom (Body+0x1c) -> nothing is added.
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body != nullptr)
    {
        IVP_Controller_Phantom* const Phantom = (IVP_Controller_Phantom*)Body->first_child;
        if (Phantom != nullptr)
        {
            // .text:062817e6..06281911: every slot of the phantom's mindist set, SizeMm + 1 of
            // them (re-read on every pass).
            for (int Slot = 0; Slot < Phantom->mindist_set.SizeMm + 1; ++Slot)
            {
                IVP_Mindist_Base* const Mindist = ((FVHashSlot*)Phantom->mindist_set.Elems)[Slot].Elem;
                if (Mindist == nullptr)
                    continue;

                // .text:06281803..06281828: the non-owner object of the pair, its client.
                IVP_Real_Object* Pair[2];
                PhySys_GetMindistObjects(Mindist, Pair);
                CObject* const Client = (CObject*)PhySys_SelectOther(Pair, OwnerObject)->client_data;
                if (Client == nullptr)
                    continue;

                // .text:06281832..06281905: the root (GetRoot, 0630e660) or the client itself,
                // added unless present (the inlined find / insert-at-end / find; the client
                // path also builds the pair<iterator, bool> through FUN_062bed70).
                if (bRoot)
                    Out.Insert(GetRoot(Client));
                else
                    Out.Insert(Client);
            }
        }
    }

    // .text:06281917..06281933: (end - begin) >> 2, or 0 while begin is null.
    return Out.Num();
}

