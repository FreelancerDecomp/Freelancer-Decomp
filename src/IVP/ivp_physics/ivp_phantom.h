// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_u_set_active.h"
#include "../ivp_utility/ivp_vhash.h"

// ============================================================================
// IVP phantoms (claude-code:cmn-ivp3d, 2026-10-08): IVP_Template_Phantom,
// IVP_Listener_Phantom and IVP_Controller_Phantom. Names are the IVP SDK's;
// Common.dll exports PhySys::PhyCollisionStateManager methods whose mangled
// names spell IVP_Controller_Phantom / IVP_Mindist_Base, and the RTTI type
// descriptor ".?AVIVP_Listener_Phantom@@" sits at .data:063ead78. Every
// layout, slot and body below is read off the retail listing.
//
// A phantom is the volume controller an IVP_Real_Object gets from
// convert_to_phantom (06345f10); it is stored in the object's IVP_Object
// +0x1c slot (first_child). The mindist manager hands it every mindist that
// enters or leaves the object (06350310 / 06350350 -> 06344400 / 06344580);
// it keeps the set of those mindists and, on request, reference-counted sets
// of the intruding objects and cores, and tells its listeners.
//
// IVP_Mindist_Base is the mindist base layer (vtable 063a55a4) of the port's
// IVP_Ledge_Mindist (ivp_mindist.h, retail IVP_Mindist); it is only forward-
// declared here and the phantom upcasts the IVP_Ledge_Mindist* it is handed
// where a listener or the mindist set needs it.
// ============================================================================

class IVP_Real_Object;
class IVP_Core;
class IVP_Mindist_Base;
class IVP_Ledge_Mindist;
class IVP_Controller_Phantom;

// ----------------------------------------------------------------------------
// IVP_Template_Phantom -- 0x18 bytes. Constructor FUN_06344210 (.text:
// 06344210-0634422c, thiscall, returns this): REP STOSD 6 dwords of zero,
// then +0x0c = +0x10 = 0x3f000000 (0.5f). Built on the stack by
// PhySys::PhyCollisionStateManager::enable_collisions (:06281594) and the
// three PhySys::CreatePhantom bodies (:0628af8d, :0628b16f, :0628b373).
// ----------------------------------------------------------------------------
class IVP_Template_Phantom
{
public:
    int32_t manage_intruding_objects;   // +0x00: nonzero -> the phantom builds its object set/store (06344710 :06344778)
    int32_t manage_intruding_cores;     // +0x04: nonzero -> core set/store (:06344805)
    int32_t dont_check_for_unmoveables; // +0x08: zero -> an unmoveable owner is re-filed (:0634488e)
    float exit_policy_extra_radius;     // +0x0c: copied to the phantom's +0x04 (:063448ba-:063448c0)
    float Field_0x10;                   // +0x10: 0.5f from the ctor. TODO: no reader traced (the phantom ctor does not read it).
    void* client_data;                  // +0x14: copied to the phantom's +0x40 (:063448c3-:063448c6)

    IVP_Template_Phantom()
        : manage_intruding_objects(0)
        , manage_intruding_cores(0)
        , dont_check_for_unmoveables(0)
        , exit_policy_extra_radius(0.5f)
        , Field_0x10(0.5f)
        , client_data(nullptr)
    {
    }
};
static_assert(sizeof(IVP_Template_Phantom) == 0x18, "IVP_Template_Phantom is 0x18 bytes (FUN_06344210 REP STOSD 6)");
static_assert(offsetof(IVP_Template_Phantom, exit_policy_extra_radius) == 0x0C, "IVP_Template_Phantom+0x0c");
static_assert(offsetof(IVP_Template_Phantom, client_data) == 0x14, "IVP_Template_Phantom+0x14");

// ----------------------------------------------------------------------------
// IVP_Listener_Phantom -- vtable .rdata:0639b874, three _purecall slots and no
// destructor slot (the table is followed by the next COL pointer at
// 0639b880). PhySys::PhyCollisionStateManager (vtable 0639b864) implements it
// and registers itself through add_listener_phantom (:062815bb).
//   slot 0 mindist_entered_volume(phantom, mindist)   06344569-06344573 `PUSH EBX; PUSH EDI; CALL [EDX]`
//   slot 1 mindist_left_volume(phantom, mindist)      063446ea-063446f4 `CALL [EDX+0x4]`
//   slot 2 phantom_is_going_to_be_deleted_event(phantom)  063442f2-063442fb `PUSH ESI; CALL [EDX+0x8]`
// ----------------------------------------------------------------------------
class IVP_Listener_Phantom
{
public:
    virtual void mindist_entered_volume(IVP_Controller_Phantom* Controller, IVP_Mindist_Base* Mindist) = 0;
    virtual void mindist_left_volume(IVP_Controller_Phantom* Controller, IVP_Mindist_Base* Mindist) = 0;
    virtual void phantom_is_going_to_be_deleted_event(IVP_Controller_Phantom* Controller) = 0;
};

// ----------------------------------------------------------------------------
// IVP_Controller_Phantom -- 0x44 bytes, no vtable (operator new(0x44) at
// 06345f41, constructor called directly, destructor FUN_063442c0 followed by
// operator delete at 06345ece / 06345f33 / 06345337).
// Defined in ivp_phantom.cpp.
// ----------------------------------------------------------------------------
class IVP_Controller_Phantom
{
public:
    IVP_Real_Object* object;                             // +0x00
    float exit_policy_extra_radius;                      // +0x04
    IVP_U_Vector<IVP_Listener_Phantom> listeners;        // +0x08 (n_elems +0x0a, elems +0x0c)
    IVP_U_Set_Active<IVP_Mindist_Base> mindist_set;      // +0x10 (0x18 bytes)
    IVP_U_Set_Active<IVP_Real_Object>* set_of_objects;   // +0x28: null unless manage_intruding_objects
    IVP_VHash_Store* object_store;                       // +0x2c: object -> mindist count, paired with +0x28
    IVP_VHash_Store* core_store;                         // +0x30: core -> mindist count, paired with +0x34
    IVP_U_Set_Active<IVP_Core>* set_of_cores;            // +0x34: null unless manage_intruding_cores
    uint32_t Field_0x38;                                 // +0x38: TODO: not written by the ctor; no access traced.
    uint32_t Field_0x3c;                                 // +0x3c: TODO: not written by the ctor; no access traced.
    void* client_data;                                   // +0x40

    // FUN_06344710 (thiscall, RET 8).
    IVP_Controller_Phantom(IVP_Real_Object* Object, const IVP_Template_Phantom* Template);
    // FUN_063442c0.
    ~IVP_Controller_Phantom();

    // FUN_06344400 (RET 4). Called by IVP_AddMindistToPhantoms (06350310).
    void mindist_entered_volume(IVP_Ledge_Mindist* Mindist);
    // FUN_06344580 (RET 4). Called by IVP_RemoveMindistFromPhantoms (06350350).
    void mindist_left_volume(IVP_Ledge_Mindist* Mindist);

    // FUN_06344230 (RET 4).
    void add_listener_phantom(IVP_Listener_Phantom* Listener);
    // FUN_06344260 (RET 4).
    void remove_listener_phantom(IVP_Listener_Phantom* Listener);
};
static_assert(sizeof(IVP_U_Set_Active<IVP_Mindist_Base>) == 0x18, "IVP_U_Set_Active is 0x18 bytes");
static_assert(offsetof(IVP_Controller_Phantom, listeners) == 0x08, "IVP_Controller_Phantom+0x08");
static_assert(offsetof(IVP_Controller_Phantom, mindist_set) == 0x10, "IVP_Controller_Phantom+0x10");
static_assert(offsetof(IVP_Controller_Phantom, set_of_objects) == 0x28, "IVP_Controller_Phantom+0x28");
static_assert(offsetof(IVP_Controller_Phantom, set_of_cores) == 0x34, "IVP_Controller_Phantom+0x34");
static_assert(offsetof(IVP_Controller_Phantom, client_data) == 0x40, "IVP_Controller_Phantom+0x40");
static_assert(sizeof(IVP_Controller_Phantom) == 0x44, "IVP_Controller_Phantom is 0x44 bytes (operator new(0x44), 06345f41)");
