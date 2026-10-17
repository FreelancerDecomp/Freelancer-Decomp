// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_collision.h"                  // IVP_Collision, IVP_Collision_Delegator
#include "../ivp_physics/ivp_listener.h"    // IVP_Listener_Hull
#include "../ivp_utility/ivp_u_vector.h"    // IVP_U_Vector

class IVP_Real_Object;
class IVP_Hull_Manager;
class IVP_OO_Watcher;

// ============================================================================
// IVP_OO_Watcher / IVP_Synapse_OO (cmn-ivp2b, 2026-10-08). Library names
// (Ipion). The collision record IVP_Collision_Delegator_Root_Mindist::
// delegate_collisions_for_object (FUN_06360c80) creates for an object pair
// whose surface managers do not both hand out a single convex ledge
// (.text:06360d5a-06360d7e: operator_new(0x40) + FUN_06369be0(this, obj0,
// obj1)). The watcher keeps one hull listener on each object and, whenever
// either hull value passes its key, rebuilds the pair's exact mindists
// (FUN_0634e0b0) into its own vector, acting as their delegator.
//
// Retail vtables (Common.dll.txt .rdata):
//   063a58e0  IVP_OO_Watcher (IVP_Collision part, +0x00):
//             0634dc00 (base trap) / 06369e20 get_objects / 06369e40 get_ledges
//             / 0634dc10 (base) / 06369cc0 deleting dtor
//   063a58d0  IVP_OO_Watcher (IVP_Collision_Delegator part, +0x14):
//             06369db0 collision_is_going_to_be_deleted_event / 06369e60
//             (SUB ECX,0x14 thunk to 06369cc0) / 062ed730 (base, RET 4) /
//             06306080 (base, -1)
//   063a58b8  IVP_Synapse_OO (IVP_Listener_Hull):
//             062d1740 get_type (= 2) / 06369a00 hull_limit_exceeded_event /
//             06369b20 hull_manager_is_going_to_be_deleted_event /
//             0628f050 hull_manager_is_reset (base, RET 8) / 06369b50
//             deleting dtor
// ============================================================================

// 0x10 bytes. Built in place by the `eh vector constructor iterator`
// (FUN_06391ef0(this+0x18, 0x10, 2, LAB_06369cb0, FUN_06369b30)): the
// constructor LAB_06369cb0 stores only the vptr, the fields are filled by
// init_synapse_oo.
class IVP_Synapse_OO : public IVP_Listener_Hull
{
public:
    // +0x04 HullPoolSlot (IVP_Listener_Hull): the slot the object's hull pool
    // returned for this listener.
    IVP_Real_Object* object;   // +0x08
    IVP_OO_Watcher*  watcher;  // +0x0c

    // LAB_06369cb0: vptr only.
    IVP_Synapse_OO() {}

    // FUN_06369b30 (+ deleting wrapper FUN_06369b50).
    virtual ~IVP_Synapse_OO();

    // FUN_06369b70 (RET 8).
    void init_synapse_oo(IVP_OO_Watcher* Watcher, IVP_Real_Object* Object);

    // 062d1740 (ICF-shared `MOV EAX,2; RET`).
    int32_t get_type() override { return 2; }

    // LAB_06369a00 (RET 8): watcher->hull_limit_exceeded_event(); the two
    // arguments are not read.
    void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) override;

    // LAB_06369b20 (RET 4) -> FUN_06369bd0: `if (watcher) watcher->slot4(1)`.
    void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) override;

    // Slot 3 is the base's 0628f050 (`RET 8`), not overridden.
};
static_assert(sizeof(IVP_Synapse_OO) == 0x10, "eh vector constructor iterator element size 0x10 (.text:06369c36)");

class IVP_OO_Watcher : public IVP_Collision, public IVP_Collision_Delegator
{
public:
    IVP_Synapse_OO synapses[2];             // +0x18 / +0x28
    IVP_U_Vector<IVP_Collision> mindists;   // +0x38: memsize 8, 0x20-byte heap buffer from the ctor

    // FUN_06369be0 (.text:06369be0-06369ca6, RET 0xc).
    IVP_OO_Watcher(IVP_Collision_Delegator* Delegator, IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // FUN_06369ce0 (+ deleting wrapper FUN_06369cc0 and the secondary-base
    // thunk LAB_06369e60).
    ~IVP_OO_Watcher() override;

    // FUN_06369a10 (plain RET).
    void hull_limit_exceeded_event();

    // IVP_Collision slot 1: LAB_06369e20 (RET 4).
    void get_objects(IVP_Real_Object* Out[2]) override;

    // IVP_Collision slot 2: LAB_06369e40 (RET 4).
    void get_ledges(const IVP_Compact_Ledge* Out[2]) override;

    // IVP_Collision_Delegator slot 0: LAB_06369db0 (RET 4).
    void collision_is_going_to_be_deleted_event(IVP_Collision* Collision) override;
};
static_assert(sizeof(IVP_OO_Watcher) == 0x40, "operator_new(0x40) at .text:06360d5a");
