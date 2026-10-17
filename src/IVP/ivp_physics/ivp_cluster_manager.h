// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"

class IVP_Environment;
class IVP_Object;
class IVP_Cluster;
class IVP_Real_Object;
class IVP_Listener_Object;
class IVP_Listener_Collision;
struct IVP_Environment_Object_Notify_Event;

// ============================================================================
// IVP_Universe_Manager -- the caller-supplied object at IVP_Environment+0x2C
// (IVP_Environment_Create_Parms+0x14, never constructed or owned by IVP).
// Abstract; no implementation lives in Common.dll. Slots from the call sites:
//   slot 0  IVP_OV_Element re-file (.text:0634ecbe-0634ecc8): (object, &center, radius)
//   slot 1  check_for_unused_objects 0634b230 (:0634b28d / :0634b2e2): (object)
//   slot 2  IVP_Cluster_Manager::remove_object 0634b3e0 (:0634b467-0634b46a): (object)
//   slot 3  check_for_unused_objects (:0634b23a): () -> the four-int settings block
// Names are the IVP SDK's.
// ============================================================================
struct IVP_Universe_Manager_Settings
{
    int32_t num_objects_in_environment_threshold_0; // +0x00
    int32_t check_objects_per_second_threshold_0;   // +0x04
    int32_t num_objects_in_environment_threshold_1; // +0x08
    int32_t check_objects_per_second_threshold_1;   // +0x0c
};

class IVP_Universe_Manager
{
public:
    virtual void ensure_objects_in_environment(IVP_Real_Object* Object, float* SphereCenter, float SphereRadius) = 0; // slot 0
    virtual void object_no_longer_needed(IVP_Object* Object) = 0;                                                   // slot 1
    virtual void event_object_deleted(IVP_Object* Object) = 0;                                                      // slot 2
    virtual const IVP_Universe_Manager_Settings* provide_universe_settings() = 0;                                   // slot 3
};

// ============================================================================
// The per-object listener tables the cluster manager keeps in its two hashes.
// Layout from 0634aef0 (.text:0634af08-0634af24, the stack probe) and the fire
// loops (`MOV SI,[EDI+6]`, `MOV EAX,[EDI+8]`): +0x00 the object (the hash key),
// +0x04 an IVP_U_Vector of listeners whose inline sentinel is +0x0c. FUN_06348be0
// (.text:06348be0-06348c0e) is the table's destructor (the vector's Clear).
// ============================================================================
class IVP_Object_Callback_Table
{
public:
    IVP_Real_Object* real_object;                 // +0x00
    IVP_U_Vector<IVP_Listener_Object> listeners;  // +0x04
};
static_assert(sizeof(IVP_Object_Callback_Table) == 0xc, "IVP_Object_Callback_Table is 0xc bytes");

class IVP_Collision_Callback_Table
{
public:
    IVP_Real_Object* real_object;                   // +0x00
    IVP_U_Vector<IVP_Listener_Collision> listeners; // +0x04
};
static_assert(sizeof(IVP_Collision_Callback_Table) == 0xc, "IVP_Collision_Callback_Table is 0xc bytes");

// The two hashes. Built by the cluster manager's ctor (0634ad70) as
// IVP_VHash(0x10) (063522a0) with their own vtables:
//   IVP_Object_Callback_Table_Hash     vtbl 063a54c0 {0635d590 compare, 0634ae50 deleting dtor -> 0635d510}
//   IVP_Collision_Callback_Table_Hash  vtbl 063a54b8 {0635d590 compare, 0634ae70 deleting dtor -> 0635d450}
// compare (LAB_0635d590): the two tables' +0x00 keys are equal.
// The dtors (0635d510 / 0635d450, identical bodies) destroy and free every
// stored table from index SizeMm down to 0, then run ~IVP_VHash (063522f0).
// find_table / remove_table are 0634aef0 / 0634b350 (one body serves both
// hashes): a stack probe table holding only the key, hashed by 0635d4d0.
class IVP_Object_Callback_Table_Hash : public IVP_VHash
{
public:
    explicit IVP_Object_Callback_Table_Hash(int Size) : IVP_VHash(Size) {}
    ~IVP_Object_Callback_Table_Hash() override;
    int Compare(void* Elem0, void* Elem1) override;

    IVP_Object_Callback_Table* find_table(IVP_Real_Object* Object);   // 0634aef0
    IVP_Object_Callback_Table* remove_table(IVP_Real_Object* Object); // 0634b350
};

class IVP_Collision_Callback_Table_Hash : public IVP_VHash
{
public:
    explicit IVP_Collision_Callback_Table_Hash(int Size) : IVP_VHash(Size) {}
    ~IVP_Collision_Callback_Table_Hash() override;
    int Compare(void* Elem0, void* Elem1) override;

    IVP_Collision_Callback_Table* find_table(IVP_Real_Object* Object);   // 0634aef0
    IVP_Collision_Callback_Table* remove_table(IVP_Real_Object* Object); // 0634b350
};

// ============================================================================
// IVP_Cluster_Manager -- the 0x18-byte object at IVP_Environment+0x0C, no
// vtable. ctor 0634ad70 (env ctor .text:0634c2a9-0634c2dc), dtor 0634b300
// (env dtor). Class / method names are the IVP SDK's.
// ============================================================================
class IVP_Cluster_Manager
{
public:
    // FUN_0634ad70 (.text:0634ad70-0634ae4c, RET 4).
    explicit IVP_Cluster_Manager(IVP_Environment* Environment);

    // FUN_0634b300 (.text:0634b300-0634b340).
    ~IVP_Cluster_Manager();

    // FUN_0634b3d0 (RET 4): number_of_real_objects++ (the object is not read).
    void add_object(IVP_Real_Object* Object);

    // FUN_0634b3e0 (.text:0634b3e0-0634b470, RET 4).
    void remove_object(IVP_Real_Object* Object);

    // FUN_0634b1a0 (.text:0634b1a0-0634b222, RET 4): the object after `Object`
    // in a depth-first walk of the cluster tree, skipping clusters (object_type
    // 1); null when there are no real objects or the walk is exhausted. A null
    // `Object` starts at the root.
    IVP_Object* get_next_real_object_in_cluster_tree(IVP_Object* Object);

    // FUN_0634b230 (.text:0634b230-0634b2f4, RET 4). Called from simulate_psi
    // (0634d090 :0634d0c5-:0634d0d0) only when the universe manager is set.
    void check_for_unused_objects(IVP_Universe_Manager* UniverseManager);

    // The obj_callback_hash fire loops (+0x08), listener slots 0 / 3 / 2:
    void fire_event_object_deleted(IVP_Environment_Object_Notify_Event* Event); // 0634ae90
    void fire_event_object_frozen(IVP_Environment_Object_Notify_Event* Event);  // 0634af70
    void fire_event_object_revived(IVP_Environment_Object_Notify_Event* Event); // 0634afd0

    // The collision_callback_hash fire loops (+0x0c), gated on the listener's
    // enabled_callbacks (+0x04):
    void fire_event_pre_collision(IVP_Real_Object* Object, void* Event);  // 0634b030, flag 1, slot 0
    void fire_event_collision_object_deleted(IVP_Real_Object* Object);    // 0634b090, flag 2, slot 1
    void fire_event_friction_slot2(IVP_Real_Object* Object, void* Event); // 0634b0e0, flag 4, slot 2
    void fire_event_friction_slot3(IVP_Real_Object* Object, void* Event); // 0634b140, flag 4, slot 3

    // The ICF-shared getter 06261d90 (`MOV EAX,[ECX]; RET`) reached through
    // IVP_Environment::get_root_cluster (0634cb90).
    IVP_Cluster* get_root_cluster() { return root_cluster; }

    IVP_Cluster* root_cluster;                                  // +0x00
    IVP_Environment* environment;                               // +0x04
    IVP_Object_Callback_Table_Hash* obj_callback_hash;          // +0x08
    IVP_Collision_Callback_Table_Hash* collision_callback_hash; // +0x0c
    IVP_Object* an_object_to_be_checked;                        // +0x10 round-robin cursor of check_for_unused_objects
    int32_t number_of_real_objects;                             // +0x14
};
static_assert(sizeof(IVP_Cluster_Manager) == 0x18, "IVP_Cluster_Manager is 0x18 bytes (operator new(0x18) at .text:0634c2a9)");

// FUN_06345e40: IVP_Real_Object method -- -1 when the object has no OV element
// (+0x90), else the element's collision count (word at element+0x2a, the
// n_elems of collision_fvector).
int32_t IVP_Real_Object_Get_Collision_Check_Count(const IVP_Real_Object* Object);
