// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ivp_anomaly_manager.h"
#include "ivp_time_event.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "../ivp_utility/ivp_u_vector.h"

// Forward declarations
class IVP_Real_Object;
class IVP_Collision_Filter;
class IVP_Environment_Manager; // defined below IVP_Environment
class IVP_Material_Manager; // ivp_material.h
class IVP_U_Active_Value_Manager; // ivp_active_value.h
class IVP_Cluster;                 // ivp_real_object.h
class IVP_OV_Tree_Manager;         // ivp_ov_element.h
class IVP_Cluster_Manager;         // ivp_cluster_manager.h
class IVP_Universe_Manager;        // ivp_cluster_manager.h
class IVP_Sim_Units_Manager;       // ivp_simulation_unit.h
class IVP_Environment;

// The IVP SDK's IVP_Controller_Manager: 4 bytes, only the environment
// (.text:0634c285-0634c29e stores `this` into a fresh operator new(4)). Its
// methods are the free functions in ivp_controller.h / common_physys.cpp that
// take this object as `this`.
class IVP_Controller_Manager
{
public:
    IVP_Environment* environment; // +0x00
};
static_assert(sizeof(IVP_Controller_Manager) == 4, "IVP_Controller_Manager is 4 bytes");

// IVP_Environment+0x94: the IVP SDK's IVP_Cache_Object_Manager. A ring of
// number_of_cache_objects 0x70-byte cache objects (IVP_NarrowPhaseCacheSlot,
// ivp_real_object.h), handed out round robin. No vtable. Every one of the 18
// retail callers of get_cache_object (0634d530) loads ECX from [env + 0x94].
struct IVP_NarrowPhaseCacheSlot;
class IVP_Cache_Object_Manager
{
public:
    int32_t number_of_cache_objects;          // +0x00: ctor argument (Create_Parms+0x00); a power of two
    int32_t next_free_index;                  // +0x04: round-robin cursor
    IVP_NarrowPhaseCacheSlot* cache_objects;  // +0x08: calloc(number_of_cache_objects, 0x70)

    explicit IVP_Cache_Object_Manager(int32_t NumberOfCacheObjects); // FUN_0634d5c0
    ~IVP_Cache_Object_Manager();                                      // FUN_0634d5f0
    IVP_NarrowPhaseCacheSlot* get_cache_object(IVP_Real_Object* Object); // FUN_0634d530
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Cache_Object_Manager) == 0xc, "retail operator new(0xc), :0634c44a");

// IVP_Environment+0x8C: a 0x20-byte object without a vtable, built by the
// environment ctor (operator new(0x20) + 063605e0), destroyed by ~IVP_Environment
// (06360640 + free) and driven once per IVP_Environment_SimulateDTime (063605a0).
// PORT-LOCAL NAME: no RTTI (no vtable) and no string names it; it records the
// simulation time and raises a flag while less than Threshold has passed since
// the last time it was let through (a file-static float, DAT_0658afdc). Nothing
// in Common.dll reads the flag, Field_0x00 or the two vectors.
class IVP_Env_Interval_Gate
{
public:
    int32_t Field_0x00;           // +0x00: 1 (ctor :06360613). TODO: no reader found.
    IVP_U_Vector<void> VectorA;   // +0x04: emptied by the ctor, cleared by the dtor. TODO: element type (never filled).
    IVP_U_Vector<void> VectorB;   // +0x0C: same.
    float LastTime;               // +0x14: set_time's argument. Not written by the ctor.
    int32_t bWithinInterval;      // +0x18: 1 (ctor), set by set_time
    float Threshold;              // +0x1C: 1.0f (ctor)

    IVP_Env_Interval_Gate();  // FUN_063605e0
    ~IVP_Env_Interval_Gate(); // FUN_06360640 (the two member vector clears)
    void set_time(float Time); // FUN_063605a0
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Env_Interval_Gate) == 0x20, "retail operator new(0x20), :0634c397");

struct IVP_U_Vector_Base
{
    uint16_t mem_size;     // Capacity
    uint16_t n_elems;      // Count
    void**   elems;        // Data buffer
};

#include "ivp_core.h"
#include "ivp_controller.h" // IVP_Controller_Independent (IVP_Standard_Gravity_Controller's base)
#include "ivp_listener.h"   // IVP_PerformanceCounter (IVP_PerformanceCounter_Simple's base), IVP_Collision_Filter

// src/IVP/ivp_physics/ivp_real_object.h -- retail's generic 0x14-byte fixed-slot
// priority pool (FUN_06362ef0/f70/f90/FUN_06363280). Only a pointer is needed
// here, and a forward declaration avoids pulling ivp_real_object.h into every
// translation unit that wants IVP_Environment.
class IVP_Priority_Pool;

#pragma pack(push, 4)

// Every payload of IVP_Sim_Unit_Manager's event pool is an IVP_Time_Event
// (ivp_time_event.h): FUN_063609c0 writes the pool slot into `param_1->+0x04`
// (`MOV dword ptr [ESI+0x4],EAX`, :063609e5), FUN_063609f0 reads it back
// (:063609f4), and the event loops dispatch slot 0 on it. (Was the raw
// `IVP_Timed_Event_Header {void* Vtable; int32 handle}` until 2026-10-08.)
class IVP_Sim_Unit_Manager;

// IVP_Time_Event_PSI -- the 8-byte event at IVP_Sim_Unit_Manager+0x0C, vtable
// 063a574c (one slot, LAB_06360ad0). Built by FUN_063608a0 (:06360908-
// :0636092b) and queued there with key 0. Its simulate_time_event is the PSI
// advance: see ivp_environment.cpp.
class IVP_Time_Event_PSI : public IVP_Time_Event
{
public:
    // LAB_06360ad0 (.text:06360ad0-06360bee, RET 4).
    void simulate_time_event(IVP_Environment* Environment) override;
};
static_assert(sizeof(IVP_Time_Event_PSI) == 0x08, "retail operator new(8), :06360915");

// IVP_Event_Manager -- the 8-byte strategy object at IVP_Sim_Unit_Manager+0x04,
// vtable 063a558c (two slots, no destructor slot: the manager's destructor
// frees it with a bare operator delete, FUN_06360960 :0636098b). Built by
// FUN_063608a0 (:063608c3-:063608e4), which zeroes +0x04.
class IVP_Event_Manager
{
public:
    // +0x04: zeroed by the ctor. Both event loops stop after an event when it
    // is 1 (`CMP dword ptr [ECX+0x4],0x1`, 063606d0 :0636078e); nothing in
    // Common.dll writes it afterwards. Library name: mode.
    int32_t Mode;

    IVP_Event_Manager() : Mode(0) {}

    // Slot 0, 063606d0 (.text:063606d0-063607b0, RET 0x10): run every queued
    // event due up to (UntilHi, UntilLo), then set the environment's time to it.
    virtual void simulate_time_events(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                      float UntilHi, float UntilLo);

    // Slot 1, 063607c0 (.text:063607c0-0636089c, RET 0x10; Ghidra left the
    // bytes undisassembled): run events until the PSI event comes up for the
    // second time, re-arming the PSI step to DeltaTime on the first.
    virtual void simulate_variable_time_step(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                             IVP_Time_Event_PSI* PsiEvent, float DeltaTime);
};
static_assert(sizeof(IVP_Event_Manager) == 0x08, "retail operator new(8), :063608c7");

// ============================================================================
// IVP_Sim_Unit_Manager -- the 0x1C-byte object at IVP_Environment+0x04.
// IDENTIFIED 2026-09-17 (lane claude:w26-simunit); this field was `void*` until
// then, which is what kept IVP_RescheduleCollisionEvent (item 3117) from ever
// queueing the event time it computes.
//
// It is the environment's TIME-ORDERED EVENT QUEUE: a priority pool of pending
// events keyed by "seconds from my own time base", plus the split-precision time
// base those keys are measured from, plus two permanently-registered listener
// objects of its own.
//
// Every field below is cited to an instruction:
//   +0x00  zeroed by the ctor's `REP STOSD` of 7 dwords (Common.dll.txt:
//          063608ba-063608c5) and never written by any function read this pass.
//          TODO(OPEN): unidentified.
//   +0x04  `operator new(8)`, slot 0 seeded with PTR_LAB_063a558c (:063608c3-
//          :063608df), slot 1 zeroed (:063608e4). FUN_06360a40 (:06360a88-
//          :06360a9d) dispatches this object's OWN vtable slot 1 with
//          (this manager, caller arg, the +0x0C listener, a clamped float), so
//          it is the manager's "schedule something" strategy object. Its class
//          is not modeled -- see TODO below.
//   +0x08  `operator new(0x14)` + FUN_06362ef0(p, 0x10) (:063608e2-:06360912):
//          THE pool. Three independent reads of its layout are quoted on
//          IVP_Priority_Pool itself.
//   +0x0C  `operator new(8)`, slot 0 seeded with PTR_LAB_063a574c -> LAB_06360ad0
//          (:06360908-:0636092b), then immediately inserted into the pool with
//          key 0 by FUN_063609c0(this, that, {0,0}) at :0636092e-:06360942.
//          LAB_06360ad0 is the PSI advance handler, which is what proves the
//          two time fields below.
//   +0x10  int32, zeroed by the ctor and reset to 0 by the PSI advance
//          (:06360bb5, `MOV dword ptr [EDX+0x10],0`). TODO(OPEN): unidentified.
//   +0x14/+0x18  the split-precision time base every pool key is relative to.
//          LAB_06360ad0 copies the environment's own previous-PSI stamp into
//          them -- `EAX=[ESI+0x108]; [EDX+0x14]=EAX` and `ECX=[ESI+0x10c];
//          [EDX+0x18]=ECX` (:06360ba1-:06360bb2, EDX = this manager, ESI = the
//          environment) -- immediately after walking every live pool entry and
//          subtracting the elapsed time from its Key (:06360b57-:06360b8e) and
//          from PoolKeyThreshold (:06360b96-:06360b9e). That rebase is exactly
//          why the enqueue key is (EventHi - TimeBaseHi) + EventLo - TimeBaseLo.
// ============================================================================
class IVP_Sim_Unit_Manager
{
public:
    uint32_t Field_0x00;            // +0x00: zeroed by the ctor; TODO(OPEN) unidentified.
    // +0x04 / +0x0C: the two 8-byte sub-objects, materialized 2026-10-08
    // (cmn-phy5) -- they were `void* TimeListenerA/B` holding null vtables, so
    // the PSI advance never ran.
    IVP_Event_Manager* EventManager; // +0x04: vtable 063a558c
    IVP_Priority_Pool* EventPool;   // +0x08
    IVP_Time_Event_PSI* PsiEvent;   // +0x0C: vtable 063a574c
    // +0x10: the key of the event the loops are dispatching -- both event loops
    // store the pool's minimum key here right before removing that event
    // (063606d0 :0636072b, 063607c0 :0636086d); the PSI advance resets it to 0
    // (:06360bb5). Written as a float bit pattern, read by nothing traced yet.
    float Field_0x10;               // +0x10
    float TimeBaseHi;               // +0x14
    float TimeBaseLo;               // +0x18

    // Retail's own enqueue key, read off TWO sites that agree instruction for
    // instruction -- FUN_063609c0 (:063609c0-:063609d6, the out-of-line helper)
    // and IVP_RescheduleCollisionEvent's inlined copy of it (:0634f967-:0634f978):
    //     FLD [EventHi]; FSUB [this+0x14]; FADD [EventLo]; FSUB [this+0x18]
    // i.e. ((EventHi - TimeBaseHi) + EventLo) - TimeBaseLo, in that exact
    // association order. Keys are therefore "seconds from the manager's own
    // time base", which is what lets the PSI advance rebase them all by one
    // subtraction per entry (LAB_06360ad0, :06360b57-:06360b8e).
    // The chain stays on the x87 stack and is spilled once (FSTP dword at
    // :063609d6 / :0634f978), so it is evaluated in double (cmn-phy8).
    float MakeEventKey(float EventHi, float EventLo) const
    {
        return (float)((((double)EventHi - TimeBaseHi) + EventLo) - TimeBaseLo);
    }

    // The inlined enqueue at IVP_RescheduleCollisionEvent:0634f964-:0634f98b.
    // Returns the pool slot; retail's caller stores it in the payload's own
    // +0x04 handle field.
    uint16_t EnqueueEvent(void* Payload, float EventHi, float EventLo);

    // FUN_063609f0 (decomp-tracker item 3403, Common.dll.txt:063609f0-06360a00,
    // 6 insns). Retail takes the payload and reads the handle out of it
    // (`MOV EAX,[ESP+4]; MOV EDX,[EAX+4]; MOV ECX,[ECX+8]; PUSH EDX; CALL
    // FUN_06363280`); this port takes the handle directly, because both of its
    // call sites already have it in hand and have just compared it against
    // 0xffff.
    void CancelEvent(int32_t Handle);

    // FUN_063609c0 (decomp-tracker item 3402, :063609c0-:063609e9, 15 insns) --
    // EnqueueEvent plus retail's own `MOV [ESI+0x4],EAX` write-back into the
    // listener's handle field.
    void ScheduleListener(IVP_Time_Event* Listener, float EventHi, float EventLo);

    // FUN_06360ab0 (.text:06360ab0-06360ac3, RET 0xc): forwards to
    // IVP_Environment_SetCurrentTime (FUN_0634ccd0). `this` is not read.
    void env_set_current_time(IVP_Environment* Environment, float TimeHi, float TimeLo);

    // FUN_06360a40 (.text:06360a40-06360aa7, RET 8): clamp DeltaTime into
    // [0.005, 0.1] and hand it to EventManager slot 1 together with PsiEvent.
    void simulate_variable_time_step(IVP_Environment* Environment, float DeltaTime);

    // FUN_06360a10 (decomp-tracker item 3404, :06360a10-:06360a3a, 17 insns):
    // take the pool's current minimum-key entry, remove it, and hand its payload
    // back to the caller. Returns null on an empty pool (retail's `CMP
    // word[ECX+0x10],0; JBE -> XOR EAX,EAX; RET`).
    void* PopMinEventPayload();

    // FUN_06360960 (decomp-tracker item 3401, :06360960-:063609bb, 31 insns):
    // this manager's own destructor body, called by ~IVP_Environment at
    // Common.dll.txt:0634c79a. It does NOT free the manager itself -- the
    // caller does that.
    void DestroyContents();
};
static_assert(sizeof(IVP_Sim_Unit_Manager) == 0x1c, "IVP_Sim_Unit_Manager size must be 0x1c -- retail's own 7-dword REP STOSD at Common.dll.txt:063608bc");
static_assert(offsetof(IVP_Sim_Unit_Manager, EventManager) == 0x04, "IVP_Sim_Unit_Manager+0x04");
static_assert(offsetof(IVP_Sim_Unit_Manager, EventPool) == 0x08, "IVP_Sim_Unit_Manager+0x08");
static_assert(offsetof(IVP_Sim_Unit_Manager, PsiEvent) == 0x0c, "IVP_Sim_Unit_Manager+0x0c");
static_assert(offsetof(IVP_Sim_Unit_Manager, Field_0x10) == 0x10, "IVP_Sim_Unit_Manager+0x10");
static_assert(offsetof(IVP_Sim_Unit_Manager, TimeBaseHi) == 0x14, "IVP_Sim_Unit_Manager+0x14");
static_assert(offsetof(IVP_Sim_Unit_Manager, TimeBaseLo) == 0x18, "IVP_Sim_Unit_Manager+0x18");

// src/IVP/ivp_physics/ivp_ov_element.h -- the 0x18-byte object at
// IVP_Environment+0x10 (see `mindist_manager` below). Forward-declared for the
// same reason IVP_Priority_Pool is.
class IVP_Mindist_Manager;

// ============================================================================
// IVP_Standard_Gravity_Controller -- IVP_Environment+0x00 (cmn-phy5, 2026-10-08).
// Library name (Ipion); every slot and field read off the listing.
//   Built inline by the environment ctor: operator_new(0x14), vptr 063a5570
//   (.text:0634c1fe-0634c21b), no field initialised there.
//   vtable 063a5570, 7 slots = IVP_Controller's layout (ivp_controller.h):
//     +0x00 062ed730 `RET 4`        core_is_going_to_be_deleted_event (base default)
//     +0x04 0627cd20 `FLD 1.0; RET` (base default)
//     +0x08 06285a60 get_associated_controlled_cores (IVP_Controller_Independent's)
//     +0x0c 0628f050 `RET 8`        (base default)
//     +0x10 06349530 do_simulation_controller            (this class)
//     +0x14 0634c6c0 `MOV EAX,0x3e8` get_controller_priority = 1000 (this class)
//     +0x18 0634b6b0 IVP_Controller's own scalar deleting destructor (no dtor here)
//   Every IVP_Core registers it: FUN_06347660 :063475fa-:06347602
//   (`MOV EAX,[core+0xc]; MOV ECX,[EAX]; PUSH ECX; CALL FUN_06349180`).
//   The environment destructor deletes it through slot 6 (:0634c76a-:0634c777).
// ============================================================================
class IVP_Standard_Gravity_Controller : public IVP_Controller_Independent
{
public:
    // The ctor writes only the vptr (:0634c20a); +0x04..+0x13 stay as
    // allocated until set_grav_vector.
    IVP_Standard_Gravity_Controller() {}

    // FUN_06349510 (.text:06349510-0634952b, RET 4): grav_vec.xyz = *Gravity;
    // grav_vec.hesse (+0x10) is not touched.
    void set_grav_vector(const IVP_U_Point* Gravity);

    // LAB_06349530 (slot 4).
    void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;

    // LAB_0634c6c0 (slot 5): 1000.
    IVP_CONTROLLER_PRIORITY get_controller_priority() override;

    IVP_U_Point grav_vec; // +0x04 (x, y, z; hesse at +0x10 never written)
};
static_assert(sizeof(IVP_Standard_Gravity_Controller) == 0x14, "operator_new(0x14) at .text:0634c1e8");

// ============================================================================
// IVP_Range_Manager -- IVP_Environment+0x1c, 0x34 bytes (cmn-phy5, 2026-10-08).
// Library name (Ipion). Built by the env ctor when the create parms carry none
// (.text:0634c3cd-0634c410: operator_new(0x34), FUN_06360360(env, 1)).
// vtable 063a5738, 3 slots (063a5744 is the float 0.18f, not a slot):
//   +0x00 063603e0 get_coll_range_intra_objects (RET 0x10)
//   +0x04 06360500 get_coll_range_in_world      (RET 4, float in ST0)
//   +0x08 063603c0 environment_will_be_deleted  (RET 4)
// No destructor slot: slot 2 frees with a bare operator delete.
// The ten floats are the RMGR_LOOK_AHEAD_* tuning PhySys writes after creating
// the environment (0628c9af-0628ca0c): +0x0c..+0x1c the _INTRA five,
// +0x20..+0x30 the _WORLD five, each in the order TIME, MAX_RADIUS,
// MIN_DISTANCE, MAX_DISTANCE, MIN_SECONDS.
// ============================================================================
class IVP_Range_Manager
{
public:
    // FUN_06360360 (.text:06360360-063603b6, RET 8).
    IVP_Range_Manager(IVP_Environment* Environment, int32_t bInDeleteOnEnvDelete);

    // LAB_063603e0 (slot 0).
    virtual void get_coll_range_intra_objects(const IVP_Real_Object* Object0, const IVP_Real_Object* Object1,
                                              float* Range0, float* Range1);

    // FUN_06360500 (slot 1). Returns the x87 value unrounded (double here).
    virtual double get_coll_range_in_world(const IVP_Real_Object* Object);

    // LAB_063603c0 (slot 2): `if (+0x04) operator delete(this)`.
    virtual void environment_will_be_deleted(IVP_Environment* Environment);

    int32_t          bDeleteOnEnvDelete;              // +0x04 (ctor's second argument)
    IVP_Environment* environment;                     // +0x08 (ctor's first argument)
    float            look_ahead_time_intra;           // +0x0c  default 0.5f
    float            look_ahead_max_radius_intra;     // +0x10  default 0.9f
    float            look_ahead_min_distance_intra;   // +0x14  default 0.8f
    float            look_ahead_max_distance_intra;   // +0x18  default 10.0f
    float            look_ahead_min_seconds_intra;    // +0x1c  default 0.1f
    float            look_ahead_time_world;           // +0x20  default 1.0f
    float            look_ahead_max_radius_world;     // +0x24  default 5.0f
    float            look_ahead_min_distance_world;   // +0x28  default 0.5f
    float            look_ahead_max_distance_world;   // +0x2c  default 15.0f
    float            look_ahead_min_seconds_world;    // +0x30  default 0.1f
};
static_assert(sizeof(IVP_Range_Manager) == 0x34, "operator_new(0x34) at .text:0634c3e0");

// ============================================================================
// IVP_PerformanceCounter_Simple -- the default IVP_Environment+0x28 object,
// 0xa8 bytes (cmn-phy5, 2026-10-08). Library name (Ipion). Built by the env
// ctor when the create parms carry none (.text:0634c413-0634c44c:
// operator_new(0xa8), FUN_063602a0). vtable 063a5714, 6 slots in
// IVP_PerformanceCounter's order (ivp_listener.h):
//   +0x00 06360350 Slot00 (start_pcount): current element = 0
//   +0x04 063602e0 Slot01 (pcount(element))
//   +0x08 062a8920 Slot02 (stop_pcount): `RET` (ICF-shared)
//   +0x0c 06360280 environment_is_going_to_be_deleted: delete this
//   +0x10 06360110 Slot04 (reset_and_print_performance_counters(IVP_Time))
//   +0x14 063602c0 deleting destructor (FUN_06360290 re-stamps the base 0639bac8)
// ============================================================================
class IVP_PerformanceCounter_Simple : public IVP_PerformanceCounter
{
public:
    // FUN_063602a0: vptr, then 0x29 dwords zeroed from +0x04 (REP STOSD).
    IVP_PerformanceCounter_Simple();

    void Slot00() override;
    void Slot01(int32_t Element) override;
    void Slot02() override {}
    void environment_is_going_to_be_deleted(IVP_Environment* Env) override;
    void Slot04(float TimeHi, float TimeLo) override;
    ~IVP_PerformanceCounter_Simple() override {}

    struct FCounter
    {
        int32_t Microseconds; // +0x00: accumulated by Slot01 (FIADD dword / ftol store)
        int32_t Field_0x04;   // +0x04: zeroed by the ctor and Slot04 only. TODO: no reader traced.
    };

    int32_t  Field_0x04;       // +0x04: zeroed only. TODO: no reader traced.
    uint32_t LastCounterLow;   // +0x08: QueryPerformanceCounter low dword of the previous Slot01
    int32_t  LastCounterHigh;  // +0x0c: its high dword (written, never read back)
    int32_t  CurrentElement;   // +0x10: element the next Slot01 charges the elapsed time to
    int32_t  CountPSIs;        // +0x14: Slot01(1) calls since the last reset
    FCounter Counters[17];     // +0x18..+0x9f
    IVP_Time TimeOfLastReset;  // +0xa0: Slot04's argument
};
static_assert(sizeof(IVP_PerformanceCounter_Simple) == 0xa8, "operator_new(0xa8) at .text:0634c41a");

// A node of IVP_Environment::buffer_node_list (+0x13C). Only the two members the
// teardown reads are known: the next link at +0x00 and a malloc'd buffer at +0x28
// that the node owns. Its destructor FUN_0634cd00 frees that buffer; the list walk
// FUN_0634cd20 frees it inline first and then deletes the node. No code in
// Common.dll allocates or links a node, so its size is unknown (the bytes between
// the two members are never touched).
struct IVP_Env_Buffer_Node
{
    IVP_Env_Buffer_Node* Next;        // +0x00
    uint8_t              Unread04[0x24];
    void*                OwnedBuffer; // +0x28 (malloc'd; freed with free)

    ~IVP_Env_Buffer_Node();           // FUN_0634cd00
};
static_assert(offsetof(IVP_Env_Buffer_Node, OwnedBuffer) == 0x28, "IVP_Env_Buffer_Node+0x28");

// IVP_Environment (0x148 / 328 bytes)
class IVP_Environment
{
public:
    // +0x00: IDENTIFIED 2026-10-08 (cmn-phy5). Was `IVP_Collision_Filter*
    // Field_0x00` holding a block with a NULL vptr: the "ICF-folded
    // boilerplate tag" the old note saw is the start of vtable 063a5570,
    // whose slots 4/5 (06349530 / 0634c6c0) are this class's own. See
    // IVP_Standard_Gravity_Controller above.
    IVP_Standard_Gravity_Controller* standard_gravity_controller; // +0x00
    // +0x04. RETYPED from `void*` 2026-09-17 (lane claude:w26-simunit) -- see
    // IVP_Sim_Unit_Manager above for the field-by-field trace. Size 0x1c, ctor
    // FUN_063608a0 (decomp-tracker item 3400), ported as
    // IVP_SimUnitManager_Construct in ivp_environment.cpp.
    IVP_Sim_Unit_Manager* sim_unit_manager;      // +0x04
    // +0x08: IVP_Sim_Units_Manager (ivp_simulation_unit.h), operator new(0x1a8)
    // + ctor 06349380 at .text:0634c2cf-0634c30d (cmn-phy8 2026-10-08; was
    // `void* time_manager` with a raw stub ctor).
    IVP_Sim_Units_Manager* sim_units_manager;     // +0x08
    // +0x0C: IVP_Cluster_Manager (ivp_cluster_manager.h), operator new(0x18) +
    // ctor 0634ad70 at .text:0634c2a9-0634c2dc. cmn-phy7 2026-10-08: renamed
    // from `controller_manager` (the IVP SDK's controller manager is the 4-byte
    // object at +0x90) and typed.
    IVP_Cluster_Manager*  cluster_manager;       // +0x0C
    // +0x10: IVP_Mindist_Manager (ivp_ov_element.h), operator new(0x18) + ctor
    // 06350390 at .text:0634c333-0634c371, dtor 063503c0.
    IVP_Mindist_Manager*  mindist_manager;       // +0x10
    // +0x14: IVP_OV_Tree_Manager (ivp_ov_element.h), the object-volume tree:
    // operator new(0x17c) + ctor 0635c410 at .text:0634c364-0634c3a1, dtor
    // 0635c500. cmn-phy7 2026-10-08: was `void* field_14_manager`.
    IVP_OV_Tree_Manager*  ov_tree_manager;       // +0x14
    // IDENTIFIED 2026-09-17 (lane claude:w19-ovtree), RENAMED the same day.
    // This field was called `statistic_manager` for weeks; that name was wrong.
    // What it actually holds is IVP's COLLISION FILTER: the per-environment
    // "may these two objects be collision-tested against each other at all?"
    // policy object. Nothing about it was ever statistical.
    //
    // Proof chain, all raw listing:
    //   * FUN_0634bcb0 (Common.dll.txt:0634bcb0-0634bcbf), the constructor this
    //     field's own allocation runs, writes only two words: the vtable
    //     .rdata:PTR_FUN_0639c3fc at +0x00 and its `int` argument at +0x04.
    //     The argument is the literal 1 at the env ctor's call site
    //     (:0634c0e8), and vtable slot 1 (LAB_0634bcd0, :0634bcd0-:0634bce2)
    //     reads exactly that field, bails when it is zero, and otherwise
    //     dispatches the object's own deleting destructor -- i.e. +0x04 is the
    //     familiar "the environment owns me, destroy me with it" flag every
    //     other adopt-or-construct slot in this class carries.
    //   * Vtable slot 0 is FUN_0634bc60 (:0634bc60-:0634bc9d), a PAIRWISE
    //     predicate over two IVP_Real_Object*: it reads each object's 8-byte
    //     fixed string at +0x84 (declared `char name[8]` in ivp_real_object.h,
    //     which is IVP's `coll_group_ident`), returns 1 outright if either is
    //     empty, and otherwise returns `strncmp(a+0x84, b+0x84, 8) != 0`. So
    //     two objects tagged with the SAME non-empty 8-character group string
    //     never collide -- which is stock IVP's default filter,
    //     IVP_Collision_Filter_Coll_Group_Ident, bolted onto the abstract
    //     IVP_Collision_Filter interface.
    //   * IVP_Mindist_Manager::RecheckOvElement is the one caller in this
    //     port's own code and uses it exactly that way -- as the LAST gate in
    //     its per-candidate-pair filter chain (:0634eed1-:0634eedd, see
    //     ivp_ov_element.cpp).
    //   * The same vtable is installed at :0628ec11 and :0629061d, i.e. the
    //     game's own 0x0628xxxx code supplies its own filter object of this
    //     shape -- which is exactly why the field is adopt-or-construct.
    //
    // INDEPENDENT CONFIRMATION: FUN_0634bc60 was already reached from the OTHER
    // direction by an earlier lane, via its non-vtable caller FUN_0628c770, and
    // was ALREADY PORTED under exactly this reading -- as `DefaultCollisionFilter`
    // (decomp-tracker item 3011; that free function sat in the former common_cequip.cpp and
    // is now ivp_listener.cpp's FUN_0634bc60), with FUN_0628c770 itself ported as
    // `NotifyCollisionFilter` (item 784; now common_physys.cpp's
    // check_objects_for_collision_detection override). Two
    // lanes arriving at "collision filter" from two unrelated call sites is
    // what makes this a finding rather than a hypothesis. That existing port is
    // a free function; nothing wires it to this field yet.
    //
    // The rename (`statistic_manager` -> `collision_filter`) was carried out
    // 2026-09-17 across ivp_environment.h/.cpp, ivp_ov_element.cpp and
    // common_stubs.cpp, together with IVP_Environment_Parms::StatisticManager
    // -> CollisionFilter. Pure rename, no behaviour change.
    // Typed 2026-10-08 (cmn-phy5) now that IVP_Collision_Filter /
    // IVP_Collision_Filter_Coll_Group_Ident are declared (ivp_listener.h).
    IVP_Collision_Filter* collision_filter;     // +0x18. Size 0x8, ctor
                                                  // FUN_0634bcb0(this,1)
                                                  // (already ready_unverified,
                                                  // item 3013). Reused from
                                                  // param_2->0x10 if non-null.
    IVP_Range_Manager*    range_manager;         // +0x1C. Adopted from
                                                  // Parms->RangeManager (+0x2c),
                                                  // else new
                                                  // IVP_Range_Manager(this, 1).
                                                  // Typed 2026-10-08 (cmn-phy5).
    // +0x20/+0x24: RETYPED and (for +0x20) RENAMED 2026-09-17 (D-ivp-anomaly
    // pass, lane claude:common-ivp). Both are now real modeled classes -- see
    // ivp_anomaly_manager.h for the complete field-by-field and slot-by-slot
    // evidence trail. +0x20 was called `performance_counter`, which is
    // disproved by its own vtable: slot 0 rescales a core's linear velocity to
    // (+0x24)->MaxVelocity * 0.99f and slot 3 is the freeze check
    // IVP_Impact_Solver::CommitCoreVelocityImmediate dispatches through. The
    // notes on decomp-tracker items 3266/3270 additionally described this
    // access chain as going through `controller_manager` (+0x0C); that is also
    // wrong -- every call site loads Core->Environment(+0x0C) and then indexes
    // +0x20 / +0x24 directly (Common.dll.txt:06357304-0635731d,
    // 06357d6e-06357d7a, 06357dab-06357dba).
    IVP_Anomaly_Manager*  anomaly_manager;       // +0x20. Size 0x8, ctor
                                                  // FUN_06360eb0(this,1) (item
                                                  // 3420). Reused from
                                                  // param_2->0x1c if non-null.
    IVP_Anomaly_Limits*   anomaly_limits;        // +0x24. Size 0x14, ctor
                                                  // FUN_06360e30(this,1) (item
                                                  // 3416). Reused from
                                                  // param_2->0x20 if non-null.
                                                  // A file-local
                                                  // AnomalyLimits06360e30 copy
                                                  // of this layout used to exist
                                                  // in src/Common/
                                                  // common_stubs.cpp (dissolved;
                                                  // no such copy remains).
    IVP_PerformanceCounter* performance_counter; // +0x28. Adopted from
                                                  // Parms->PerformanceCounter
                                                  // (+0x18), else new
                                                  // IVP_PerformanceCounter_Simple.
                                                  // Was `void* listener_objects`
                                                  // until 2026-10-08 (cmn-phy5).
    // +0x2C: the caller-supplied IVP_Universe_Manager (ivp_cluster_manager.h),
    // a plain copy of Create_Parms+0x14 (.text:0634c1f8), never constructed or
    // owned here. Users: simulate_psi 0634d090 (check_for_unused_objects),
    // IVP_Cluster_Manager::remove_object 0634b3e0 (slot 2), the OV element
    // re-file 0634ec9a (slot 0). cmn-phy7 2026-10-08: was `void* memory_manager`.
    IVP_Universe_Manager* universe_manager;      // +0x2C
    IVP_Real_Object*      static_object;         // +0x30. Size 0xa8, built via
                                                  // FUN_0634cdf0 (item 3050) ->
                                                  // FUN_06362570 (item 3487),
                                                  // now WIRED to real code
                                                  // (IVP_Ball::IVP_Ball in
                                                  // ivp_real_object.cpp and the
                                                  // constructor below,
                                                  // D-core-init-tail pass,
                                                  // 2026-09-10) -- allocates
                                                  // the 0xa8-byte IVP_Real_Object,
                                                  // looks up an id (FUN_0634cb90),
                                                  // then placement-constructs it
                                                  // via the real IVP_Real_Object
                                                  // ctor + IVP_InitCoreRestStateAndDamping.
                                                  // One cited gap remains (both
                                                  // items stay `partial`): retail
                                                  // also overrides surface_manager
                                                  // to a static global this port
                                                  // has not identified/ported yet
                                                  // -- see FUN_06362570's own
                                                  // comment in ivp_real_object.cpp
                                                  // (IVP_Ball::IVP_Ball).
                                                  // Header's existing
                                                  // IVP_Real_Object* typing is
                                                  // correct/unchanged.
    // +0x34..+0x87: was a single opaque `char manager_context[84]`. Split
    // 2026-09-17 (D-ivp-anomaly pass, lane claude:common-ivp) to expose three
    // dword counters that the impact solver bumps. All three are plain
    // `INC dword ptr [Environment+N]` with no other writer and no reader found
    // anywhere in Common.dll, so their WIDTH (4 bytes) and their increment
    // sites are proven while their meaning is not -- placeholder names per this
    // project's placeholder-field policy. (Item 3270's notes previously
    // described the +0x5C one as a 16-bit word reached through
    // controller_manager->+0x0C; the listing shows a 32-bit INC applied
    // directly to the Environment.)
    char                  manager_context_34[0x18]; // +0x34..+0x4B
    int32_t               counter_4c;            // +0x4C: ++ once per
                                                  // IVP_Impact_Solver::DoImpactSystem
                                                  // entry (Common.dll.txt:06357918).
    int32_t               counter_50;            // +0x50: += (candidate count + 1)
                                                  // after the ambiguous-winner search
                                                  // loop (.text:06359067-0635906f,
                                                  // `MOV ECX,[EAX+0x50]; INC EDI;
                                                  // ADD ECX,EDI; MOV [EAX+0x50],ECX`).
                                                  // Proven width, meaning unknown
                                                  // (no reader in Common.dll).
    char                  manager_context_54[0x08]; // +0x54..+0x5B
    int32_t               counter_5c;            // +0x5C: ++ on
                                                  // IVP_Impact_Solver::FinalizeImpact's
                                                  // deferred-commit path only
                                                  // (Common.dll.txt:06358204).
    int32_t               counter_60;            // +0x60: ++ per pair whose owner
                                                  // state byte (+0x54) != 1 in
                                                  // 063591a0 (.text:0635921f, `INC
                                                  // dword ptr [EAX+0x60]`). Proven
                                                  // width, meaning unknown.
    int32_t               counter_64;            // +0x64: ++ in DoImpactSystem's
                                                  // post-finalize "a core got
                                                  // flagged" tail
                                                  // (Common.dll.txt:06357f14).
    char                  manager_context_68[0x04]; // +0x68..+0x6B (float accumulator, see ivp_listmanager_topology.cpp)
    // +0x6c/+0x70/+0x74: the library's mindist statistics (cmn-phy5,
    // 2026-10-08). FUN_0634dd10 (IVP_Mindist ctor) does `INC [env+0x6c]; INC
    // [env+0x70]` (.text:0634dd2f/:0634dd32); FUN_0634dd60 (its dtor) does
    // `DEC [env+0x6c]; INC [env+0x74]` (.text:0634dd8c/:0634dd8f).
    int32_t               sum_of_mindists;       // +0x6C: live mindists
    int32_t               mindists_generated;    // +0x70
    int32_t               mindists_deleted;      // +0x74
    int32_t               range_intra_exceeded;  // +0x78: ++ in 06369a10 / 06362d90 before get_coll_range_intra_objects
    int32_t               counter_7c;            // +0x7C: ++ once per
                                                  // IVP_Mindist_Manager::
                                                  // RecheckOvElement entry, right
                                                  // after the element is pulled out
                                                  // of the OV tree -- `MOV EAX,
                                                  // [EBP+4]; MOV ECX,[EAX+0x7c];
                                                  // INC ECX; MOV [EAX+0x7c],ECX`,
                                                  // Common.dll.txt:0634ec49-0634ec50
                                                  // (carved out of
                                                  // manager_context_68 on
                                                  // 2026-09-17 by lane
                                                  // claude:w18-ivp). Same
                                                  // proven-width/unproven-meaning
                                                  // placeholder class as
                                                  // counter_4c/5c/64/80: no reader
                                                  // was found anywhere in
                                                  // Common.dll.
    int32_t               counter_80;            // +0x80: ++ once per
                                                  // FUN_0635a1e0 (the contact-
                                                  // frame dispatcher) entry,
                                                  // immediately after it
                                                  // allocates the pair's owning
                                                  // IVP_Impact_Solver_Long_Term -- `MOV EDX,
                                                  // [EAX+0x80]; INC EDX; MOV
                                                  // [EAX+0x80],EDX`,
                                                  // Common.dll.txt:0635a220-
                                                  // 0635a229. Same proven-width/
                                                  // unproven-meaning placeholder
                                                  // class as counter_4c/5c/64
                                                  // above: no reader was found
                                                  // anywhere in Common.dll.
    char                  manager_context_84[0x04]; // +0x84..+0x87
    float                 default_friction;      // +0x88 (0.3f)
    IVP_Env_Interval_Gate* interval_gate;        // +0x8C: new(0x20) + 063605e0 (:0634c397-:0634c3c7)
    // +0x90: the IVP SDK's IVP_Controller_Manager, a 4-byte object holding only
    // its environment: operator new(4) and an inline store of `this`
    // (.text:0634c285-0634c29e). It is the `this` of 06349040 / 06348fe0
    // (PhySys 06285ce6). cmn-phy7 2026-10-08: renamed from `cluster_manager`
    // (the cluster manager is +0x0C) and typed.
    IVP_Controller_Manager* controller_manager;  // +0x90
    // +0x94: new(0xc) + 0634d5c0(Create_Parms+0x00) (:0634c44a-:0634c482).
    IVP_Cache_Object_Manager* cache_object_manager;
    // +0x98: the IVP_U_Active_Value_Manager (ivp_active_value.h; vtable
    // 063a57ac, ctor FUN_063615a0, size 0x28). Reused from Parms->0x28 if
    // non-null, else `new IVP_U_Active_Value_Manager(1)` (0634c07e-0634c0ba).
    // Was named collision_listener; renamed 2026-10-08 (claude-code:cmn-phy4)
    // once its vtable was identified. SDK name l_active_value_manager.
    IVP_U_Active_Value_Manager* l_active_value_manager;
    // +0x9C: the IVP_Material_Manager (ivp_material.h; vtable 063a551c, ctor
    // FUN_0634bd80, size 8). Reused from Parms->0xc if non-null, else
    // `new IVP_Material_Manager(1)` (0634c047-0634c078). Was named
    // phantom_listener; renamed 2026-10-08 (claude-code:cmn-phy3) once its
    // vtable was identified. (The `material_manager` at +0x8C above is a
    // different, older port name for the 0x20-byte FUN_063605e0 object.)
    IVP_Material_Manager* l_material_manager;
    // +0xA0/+0xA4: RETYPED 2026-09-17 (lane claude:common-ivpenv) from `void*`
    // to the real arena type. The NAMES ARE STALE AND MISLEADING -- neither
    // field has anything to do with cores or with vectors. IVP_Environment's
    // own constructor takes ONE caller-supplied raw memory block
    // (Parms->MemoryBlock / Parms->MemoryBlockSize), splits it exactly in half,
    // and hands the halves to these two arenas via IVP_U_Memory::init_mem
    // (Common.dll.txt:0634c553-0634c57d -- the full instruction sequence is
    // quoted in ivp_u_memory.h's own class comment). Kept under the old names
    // only to avoid a rename sweep in the same change that introduces the
    // type; see ivp_u_memory.h for how three separate hand-rolled
    // transcriptions of this one struct were collapsed into it.
    IVP_U_Memory*         vector_cores_active;   // +0xA0. Arena over the FIRST
                                                  // half of Parms->MemoryBlock.
                                                  // Constructed via
                                                  // FUN_06344120 (item 2756).
    IVP_U_Memory*         vector_cores_frozen;   // +0xA4. Arena over the SECOND
                                                  // half. Same type and ctor.
    float                 field_a8;              // +0xA8
    float                 delta_time;            // +0xAC. delta_PSI_time.
                                                  // Default 1/66 s, written
                                                  // together with
                                                  // inv_delta_time by
                                                  // IVP_Environment::
                                                  // set_delta_PSI_time
                                                  // (FUN_0634c6d0).
    float                 inv_delta_time;        // +0xB0. Exactly
                                                  // 1.0f / delta_time.
    // +0xB4..+0xBC: CORRECTED 2026-09-17 (lane claude:common-ivpenv). The
    // gravity default is 9.83f, NOT 9.81f. The raw dword the constructor
    // builds on its own stack and hands to FUN_0634cb20 is 0x411d47ae
    // (Common.dll.txt:0634c225), which decodes to 9.829999923706055. 9.81f
    // would be 0x411cf5c3. Both this header and the former common_stubs.cpp previously
    // annotated the same constant as "9.80999994f", which was simply a
    // mis-decode -- no code depended on the wrong value, but the note did.
    float                 field_b4;              // +0xB4. Gravity vector X (0).
    float                 field_b8;              // +0xB8. Gravity vector Y
                                                  // (9.83f, see above).
    float                 field_bc;              // +0xBC. Gravity vector Z (0).
    float                 field_c0;              // +0xC0
    float                 field_c4;              // +0xC4. length(gravity
                                                  // vector), set by
                                                  // FUN_0634cb20 alongside
                                                  // field_b4/b8/bc above.
    IVP_U_Vector_Base     vector_listeners_c8;   // +0xC8 (8 bytes)
    IVP_U_Vector_Base     vector_listeners_d0;   // +0xD0 (8 bytes)
    IVP_U_Vector_Base     event_queue;           // +0xD8 (8 bytes: Time event queue)
    IVP_U_Vector_Base     vector_e0;             // +0xE0 (8 bytes)
    char*                 environment_name;      // +0xE8. `_strdup(param_3)`
                                                  // (FUN_06356920, already-
                                                  // known CRT strdup, item
                                                  // 3254), or null if param_3
                                                  // is null.
    void*                 client_data;           // +0xEC. Plain copy of the
                                                  // ctor's own param_4
                                                  // (Common.dll.txt:420514-
                                                  // 420518).
    // +0xF0: RE-CORRECTED 2026-09-17 (lane claude:common-ivpenv). This field
    // IS the literal constant 10, and the 2026-09-10 note that "corrected" it
    // to "a computed half-count (SAR of param_2->0x8 by 1)" was wrong. Both
    // values really do pass through EDI inside the constructor, which is what
    // made the mis-read easy, but they are separated by a reload:
    //   .text:0634c559  SAR EDI,0x1          ; EDI = half the memory-block size
    //   .text:0634c56b/0634c57d              ; ...consumed by the two arenas...
    //   .text:0634c58a  MOV EDI,0xa          ; EDI RELOADED with 10
    //   .text:0634c595  MOV word [ESI+0x118],DI
    //   .text:0634c5f1  MOV dword [ESI+0xf0],EDI  ; <- this field
    // By the time +0xF0 is written the half-count is long gone.
    // next_movement_check (+0x118) below takes its 10 from this same reloaded
    // register. TODO: meaning unknown -- no reader of IVP_Environment+0xF0 was
    // found in Common.dll (every `[reg+0xf0]` read in the listing is on a
    // different object, e.g. IVP_Core at 06345020 / 063479f8).
    int32_t               field_f0;              // +0xF0. Literal 10.
    // +0xF4: the ctor's param_1 (plain copy, :0634c4b0), always the
    // process-wide IVP_Environment_Manager singleton &DAT_0658aa00
    // (get_environment_manager 0634cc70 -> create_environment 0634cba0).
    // The ctor appends `this` to its Environments vector (:0634c25a-
    // :0634c281) and the destructor removes it (:0634c932-:0634c981).
    // Was `void* field_f4` until 2026-10-08 (cmn-phy5).
    IVP_Environment_Manager* environment_manager; // +0xF4
    // +0xF8..+0x107: TWO split-precision timestamps, not one timestamp plus two
    // loose scalars. Documented 2026-09-17 (lane claude:common-ivp5,
    // decomp-tracker items 3117/3128/3301).
    //
    // Every consumer treats them as a matched pair and differences them the
    // same way, `(hi2 - hi1) + (lo2 - lo1)`:
    //   FUN_0634f320 (item 3117) caches all four and computes the simulation
    //     horizon as `((+0x100 - +0xf8) + (+0x104 - +0xfc))`,
    //     Common.dll.txt:0634f532-0634f53e, and again at :0634f933-0634f953.
    //   FUN_06350050 (item 3128) and FUN_0635a1e0 (item 3301) both difference
    //     +0xf8/+0xfc against a stored copy of the same pair to get elapsed
    //     time (Common.dll.txt:06350080-06350093 / 0635a870-0635a88f).
    //   This class's own constructor seeds the second pair, at a point where
    //     current_time is still zero, as `split(delta_time)`
    //     (ivp_environment.cpp, Common.dll.txt:0634c4df/0634c4ef) -- i.e. it is
    //     the time of the NEXT PSI, expressed in the same split form.
    //
    // CLOSED 2026-09-17 (this paragraph used to say the opposite, and was stale
    // within hours of being written): the two halves are read by x87 FLD at every
    // site above, so they are floats, and `IVP_Time` (ivp_core.h) now declares
    // both members `float`. The real defect it described is fixed too --
    // ivp_environment.cpp's PSI advance used to write the pair back through
    // `static_cast<uint32_t>(...)`, storing an integer VALUE where every reader
    // interprets float BITS (123.75 became 123, read back as ~1.7e-43). It now
    // assigns an IVP_Time directly.
    IVP_Time              current_time;          // +0xF8 (8 bytes: +0xF8 / +0xFC)
    float                 next_psi_time_base;    // +0x100: high half of the second split-precision stamp (was field_100)
    float                 next_psi_time_offset;  // +0x104: low half (was field_104)
    // +0x108/+0x10C: the time of the PSI that is running (formerly field_108 /
    // field_10c). The PSI event (LAB_06360ad0, :06360ad9-:06360af0) copies
    // current_time here, then derives next_psi_time = this + delta_time; the
    // calc-next-PSI solver stamps it into IVP_Core::TimeOfLastPsi (:06351c6b).
    IVP_Time              time_of_last_psi;      // +0x108 (8 bytes: +0x108 / +0x10C)
    int32_t               sim_step_count;        // +0x110
    int32_t               field_114;             // +0x114: an int counter -- `INC EAX` read-modify-write at
                                                  // .text:06358d1a-06358d29; copied raw into IVP_Core+0x1b8
                                                  // (06347be7) and at 063461d5. TODO: meaning unknown.
    // +0x118 (formerly field_118): countdown, in sim-unit PSI steps, to the next
    // movement check. The ctor seeds 10 (:0634c595); every unit step DECs it
    // (:06349841 / :063498af) and on 0 reseeds 15 - ftol(rand * -5.0f) and runs
    // the motion/sleep check FUN_063495e0 for that unit.
    int16_t               next_movement_check;   // +0x118
    int16_t               field_11a;             // +0x11A
    int32_t               sim_substate;          // +0x11C
    float                 field_120;             // +0x120
    IVP_U_Vector_Base     object_listeners;      // +0x124 (8 bytes)
    IVP_U_Vector_Base     vector_12c;            // +0x12C (8 bytes). One
                                                  // element appended by the
                                                  // ctor (IVP_U_Vector_Base
                                                  // growable-vector convention,
                                                  // FUN_063441a0 / already-
                                                  // ported IVP_U_Vector<T>::
                                                  // Realloc): either reused
                                                  // from param_2->0x24, or a
                                                  // new element built via
                                                  // FUN_06360df0 = new
                                                  // IVP_Collision_Delegator_Root_Mindist
                                                  // (ivp_collision.h). Elements
                                                  // are IVP_Collision_Delegator_Root*.
    float                 field_134;             // +0x134
    float                 field_138;             // +0x138
    // +0x13C: head of a singly-linked list of IVP_Env_Buffer_Node, drained and
    // freed by the destructor (FUN_0634cd20 from 0634c983). Nothing in
    // Common.dll links a node in, so the list is empty in practice
    // (formerly void* field_13c_list_head).
    IVP_Env_Buffer_Node*  buffer_node_list;      // +0x13C
    float                 field_140;             // +0x140
    int32_t               state_magic;           // +0x144 (0x1E240 = 123456)
};

#pragma pack(pop)

static_assert(sizeof(IVP_Environment) == 0x148, "IVP_Environment size mismatch");
// Pinned 2026-09-17 (D-ivp-anomaly pass): every offset this pass either
// retyped or carved out of the old manager_context blob.
static_assert(offsetof(IVP_Environment, ov_tree_manager) == 0x14, "IVP_Environment+0x14");
static_assert(offsetof(IVP_Environment, anomaly_manager) == 0x20, "IVP_Environment+0x20");
static_assert(offsetof(IVP_Environment, anomaly_limits) == 0x24, "IVP_Environment+0x24");
static_assert(offsetof(IVP_Environment, manager_context_34) == 0x34, "IVP_Environment+0x34");
static_assert(offsetof(IVP_Environment, counter_4c) == 0x4C, "IVP_Environment+0x4C");
static_assert(offsetof(IVP_Environment, counter_5c) == 0x5C, "IVP_Environment+0x5C");
static_assert(offsetof(IVP_Environment, counter_64) == 0x64, "IVP_Environment+0x64");
static_assert(offsetof(IVP_Environment, counter_7c) == 0x7C, "IVP_Environment+0x7C");
static_assert(offsetof(IVP_Environment, counter_80) == 0x80, "IVP_Environment+0x80");
static_assert(offsetof(IVP_Environment, default_friction) == 0x88, "IVP_Environment+0x88");
static_assert(offsetof(IVP_Environment, inv_delta_time) == 0xB0, "IVP_Environment+0xB0");

// FUN_0634bbe0: drains the whole wake/event queue (ivp_environment.cpp).
void IVP_Environment_Process_Wake_Queue(IVP_Environment* env);

// FUN_0634bb20: single-entry removal from the wake/event queue -- used by
// IVP_Core::~IVP_Core (see IVP_Core_Remove_Object in ivp_core.cpp). Declared
// here (rather than only defined in ivp_environment.cpp) so other IVP
// translation units can call it. (ivp_environment.cpp)
void IVP_Environment_Dequeue_Core(IVP_Environment* env, IVP_Core* core);

// FUN_0634bb90 (.text:0634bb90-0634bbd1, thiscall on IVP_Environment, RET 4):
// the IVP SDK's add_revive_core. Appends `core` to event_queue (+0xD8) unless
// (core->Flags & 0x30) == 0x10, growing the vector through FUN_063441a0
// (IVP_U_Vector_Base_Realloc, 2n+1) when n_elems >= mem_size, then sets
// Flags = (Flags & ~0x20) | 0x10. The queue is drained by
// IVP_Environment_Process_Wake_Queue (0634bbe0). Callers: 06346170
// (IVP_Real_Object::EnsureInSimulation) and 06348f50 below. (ivp_environment.cpp)
void IVP_Environment_Enqueue_Core(IVP_Environment* env, IVP_Core* core);

// FUN_06348f50 (.text:06348f50-06348f5d, RET 4): the IVP SDK's
// IVP_Controller_Manager::ensure_core_to_be_in_simulation. Every retail caller
// loads ECX = environment->controller_manager (+0x90), but the body never reads
// ECX: it is `IVP_Environment_Enqueue_Core(core->Environment, core)`. Callers:
// the golem setters 063435e4 / 063436eb and CRemotePhysicsSimulation 062923cb /
// 062923f4 / 06292580 (common_physys.cpp). (ivp_environment.cpp)
void IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(IVP_Core* core);

// FUN_0634cf60 (decomp-tracker item 3056): broadcasts to every "armed"
// (flags byte bit 0 set) entry of env->vector_listeners_c8 via each
// listener's own vtable slot 0. Found this pass (2026-09-09) while tracing
// item 3276/FUN_06358680 (Common.dll.txt:06358aa7, this=ObjA->environment).
// See ivp_environment.cpp for the full evidence trail and the
// FULLPORT-DEFERRED caveat on the virtual dispatch itself.
void IVP_Environment_Notify_Listeners_C8(IVP_Environment* env, void* arg);

// Event payload for IVP_Environment_Notify_Object_Listeners below --
// FUN_0634b480 (item 2989) builds exactly this 2-field {Environment*,
// IVP_Real_Object*} record on its own stack and passes a pointer to it,
// confirmed field-for-field at Common.dll.txt:0634b563-06356b573 (Environment
// at offset 0, object at offset 4).
struct IVP_Environment_Object_Notify_Event
{
    IVP_Environment* Env;
    IVP_Real_Object* Object;
};

// FUN_0634cf30 (decomp-tracker item 3055, Common.dll.txt:421559-421585, 20
// insns). D-target-2989 pass (2026-09-10): `this`=Environment (confirmed:
// reads n_elems/elems at +0x126/+0x128, exactly env->object_listeners's own
// IVP_U_Vector_Base header at +0x124). Backward loop over
// env->object_listeners, calling each listener's own vtable slot+8 with the
// single stack arg unchanged (retail never dereferences it -- just forwards
// the caller's pointer straight through), unlike
// IVP_Environment_Notify_Listeners_C8 above this does NOT gate on any
// per-listener flag byte. Same FULLPORT-DEFERRED caveat as that function: no
// Listener interface modeled anywhere in this port yet.
void IVP_Environment_Notify_Object_Listeners(IVP_Environment* env, void* event_data);

// ============================================================================
// D-cluster-0634bcf0 pass (2026-09-10) additions -- decomp-tracker items
// 3034/3035/3047/3048/3051/3053/3054/3057/3058/3060/3072.
// ============================================================================

// FUN_0634c6f0 (decomp-tracker item 3034, Common.dll.txt:420597-420934, 299
// insns): the real ~IVP_Environment(). See ivp_environment.cpp for the full
// evidence trail (mirror-image of the already-documented constructor
// FUN_0634bf50 above) and the FULLPORT-DEFERRED[IVP] gaps (virtual calls
// through not-modeled sub-manager interfaces, and a handful of not-yet-
// ported nested-cleanup callees). Only free()/null-out/vector-teardown side
// effects are reproduced for those; everything self-contained (the
// environment_manager registry unregister, the buffer_node_list drain, all 6
// IVP_U_Vector_Base teardowns) is fully ported.
void IVP_Environment_Destructor(IVP_Environment* env);

// FUN_0634caf0 (decomp-tracker item 3035, Common.dll.txt:420950-420976, 20
// insns). Backward loop over env->vector_12c (+0x12C/+0x12E/+0x130),
// calling each listener's own vtable slot4 (+0x10) with `arg`. Same
// FULLPORT-DEFERRED[IVP] shape as IVP_Environment_Notify_Listeners_C8
// above (no Listener interface modeled) -- distinct from the destructor's
// OWN vector_12c loop, which calls slot6 instead.
void IVP_Environment_Notify_Vector12c_Slot4(IVP_Environment* env, void* arg);

// FUN_0634cd20 (decomp-tracker item 3048, .text:0634cd20-0634cd75). Walks and
// deletes the whole env->buffer_node_list (each node's buffer is freed inline
// first, then `delete node` runs ~IVP_Env_Buffer_Node and operator delete), then
// nulls the head. Called from the destructor (IVP_Environment_Destructor above).
void IVP_Environment_Free_Buffer_Node_List(IVP_Environment* env); // formerly IVP_Environment_Free_Field13c_List

// FUN_0634ce60 (decomp-tracker item 3051, Common.dll.txt:421433-421453, 15
// insns). Growable-vector append (IVP_U_Vector_Base convention) onto
// env->object_listeners (+0x124/+0x126/+0x128), reallocating via the
// already-ported IVP_U_Vector<T>::Realloc/FUN_063441a0 equivalent when
// full.
void IVP_Environment_Append_Object_Listener(IVP_Environment* env, void* listener);

// FUN_0634ced0 (decomp-tracker item 3053, Common.dll.txt:421496-421522, 20
// insns) / FUN_0634cf00 (decomp-tracker item 3054, Common.dll.txt:421528-
// 421554, 20 insns). Backward loops over env->object_listeners, calling
// each listener's own vtable slot0 / slot3 respectively with `arg`.
// FULLPORT-DEFERRED[IVP]: no Listener interface modeled, same gap as
// IVP_Environment_Notify_Object_Listeners above (which is this same
// vector's slot+8 sibling).
void IVP_Environment_Notify_Object_Listeners_Slot0(IVP_Environment* env, void* arg);
void IVP_Environment_Notify_Object_Listeners_Slot3(IVP_Environment* env, void* arg);

// FUN_0634cea0 (decomp-tracker item 3052, Common.dll.txt:421464-421491, 20
// insns). D-claude-common-ivp pass (2026-09-17): the fourth sibling of the
// object_listeners notify family -- slot 1, no per-listener null check.
// Called by both IVP_Real_Object leaf constructors (IVP_Polygon/IVP_Ball).
// See ivp_environment.cpp for the full evidence trail and the shared
// FULLPORT-DEFERRED[IVP] caveat.
void IVP_Environment_Notify_Object_Listeners_Slot1(IVP_Environment* env, void* arg);

// FUN_0634cfa0 (decomp-tracker item 3057, Common.dll.txt:421635-421664, 22
// insns) / FUN_0634cfe0 (decomp-tracker item 3058, Common.dll.txt:421679-
// 421708, 22 insns). Backward loops over env->vector_listeners_c8, gated
// per-entry on flags-byte bit 2 (&4, NOT the &1 gate IVP_Environment_
// Notify_Listeners_C8 above uses), calling each listener's own vtable
// slot2 / slot3 respectively with `arg`. FULLPORT-DEFERRED[IVP]: no
// Listener interface modeled.
void IVP_Environment_Notify_Listeners_C8_Slot2(IVP_Environment* env, void* arg);
void IVP_Environment_Notify_Listeners_C8_Slot3(IVP_Environment* env, void* arg);

// FUN_0634d050 (decomp-tracker item 3060, Common.dll.txt:421754-421774, 15
// insns). Growable-vector append onto env->vector_listeners_c8 (+0xC8/
// +0xCA/+0xCC), same IVP_U_Vector_Base convention as
// IVP_Environment_Append_Object_Listener above.
void IVP_Environment_Append_Listener_C8(IVP_Environment* env, void* listener);


// ============================================================================
// D-ivpenv-ctor pass (2026-09-17, lane claude:common-ivpenv) -- the pieces
// needed to finally write IVP_Environment's real constructor, FUN_0634bf50
// (decomp-tracker item 3031).
// ============================================================================

// FUN_063441a0 (decomp-tracker item 2759, Common.dll.txt:405246-405254, 26
// insns, __thiscall). The growable-vector realloc for the RAW
// IVP_U_Vector_Base header ({mem_size@+0, n_elems@+2, elems@+4}).
//
// THIS IS THE SAME RETAIL FUNCTION as IVP_U_Vector<T>::Realloc in
// ivp_utility/ivp_u_vector.h, and IVP_U_Vector_Base has the SAME layout as
// that template ({memsize@+0, n_elems@+2, elems@+4}) -- they are two names for
// one retail type. TODO(OPEN): unify them. Not done in this pass because the
// environment's six vector fields are declared IVP_U_Vector_Base, the two
// types spell the capacity field differently (mem_size vs memsize), and the
// template's Realloc is protected -- so unifying is a rename sweep across
// several files rather than a bug fix, and this pass could not build. Until
// then this is a second entry point to one algorithm, kept deliberately
// byte-faithful to the same listing so the two cannot silently diverge. (The
// parallel free function IVP_U_Vector_Base_Clear already in
// ivp_environment.cpp is the same situation.)
//
// Retail:
//   new = malloc(mem_size * 8 + 8);            // 063441a9-063441b1
//   mem_size = mem_size * 2 + 1;               // 063441b8-063441ca
//   copy n_elems pointers across;              // 063441cf-063441e1
//   if (elems != (void**)(v + 1) && elems)     // 063441e3-063441ef
//       free(elems);                           // 063441f2
//   elems = new;                               // 06344201
// The `v + 1` sentinel is the 8-byte inline buffer that sits immediately
// after the header for vectors that start out embedded -- the same test
// IVP_Environment_Process_Wake_Queue already makes on event_queue.
void IVP_U_Vector_Base_Realloc(IVP_U_Vector_Base* v);

// The append idiom the constructor (and every other IVP_U_Vector_Base user in
// Common.dll) open-codes around the realloc above: grow first if
// n_elems >= mem_size, then store and bump. Retail's own test is
// `CMP n_elems, mem_size; JC skip` -- i.e. it grows on n_elems >= mem_size,
// which is what this reproduces.
void IVP_U_Vector_Base_Append(IVP_U_Vector_Base* v, void* elem);

// ----------------------------------------------------------------------------
// IVP_Environment_Create_Parms -- port-local name (no retail symbol) for the
// 0x30-byte read-only descriptor the constructor takes as its second argument.
// Every field below is proven by a single load in FUN_0634bf50 and nothing
// else; the struct is never written to.
//
// The pattern is uniform for the sub-manager slots: a non-null value is
// ADOPTED as-is (the environment does not own it), and only a null one causes
// the environment to `operator new` and construct its own. That is why each
// such field is typed `void*` here -- retail's own construction sites are what
// give those objects their types, and they live in ivp_environment.cpp (the
// IVP_Environment constructor).
// ----------------------------------------------------------------------------
struct IVP_Environment_Create_Parms
{
    int32_t  NumberOfCacheObjects; // +0x00 -> IVP_Cache_Object_Manager ctor argument (0634c467).
    char*    MemoryBlock;        // +0x04 -> split in half across the two IVP_U_Memory arenas (0634c556).
    int32_t  MemoryBlockSize;    // +0x08 -> `SAR 1` gives each arena's half (0634c553-0634c559).
    IVP_Material_Manager* MaterialManager; // +0x0C -> +0x9C, else new IVP_Material_Manager(1) (0634c03c).
    IVP_Collision_Filter* CollisionFilter; // +0x10 -> +0x18, else new IVP_Collision_Filter_Coll_Group_Ident(1) (0634c0c0).
    IVP_Universe_Manager* UniverseManager; // +0x14 -> +0x2C verbatim, never constructed here (0634c1f8).
    IVP_PerformanceCounter* PerformanceCounter; // +0x18 -> +0x28, else new IVP_PerformanceCounter_Simple (0634c413).
    void*    AnomalyManager;     // +0x1C -> +0x20, else FUN_06360eb0 (0634c0fc).
    void*    AnomalyLimits;      // +0x20 -> +0x24, else FUN_06360e30 (0634c138).
    void*    Vector12cElement;   // +0x24 -> appended to vector_12c, else FUN_06360df0 (0634c174).
    IVP_U_Active_Value_Manager* ActiveValueManager; // +0x28 -> +0x98, else new IVP_U_Active_Value_Manager(1) (0634c07e).
    IVP_Range_Manager* RangeManager; // +0x2C -> +0x1C, else new IVP_Range_Manager(env, 1) (0634c3cd).

    // FUN_06350610 (.text:06350610-06350627): `REP STOSD` of 0xc dwords (the
    // whole 0x30 bytes) then `MOV [this],0x100`. PhySys's world init builds one
    // on its stack (0628c92c-0628c930). cmn-phy5 2026-10-08.
    IVP_Environment_Create_Parms()
    {
        std::memset(this, 0, sizeof(*this));
        NumberOfCacheObjects = 0x100;
    }
};
static_assert(sizeof(IVP_Environment_Create_Parms) == 0x30, "IVP_Environment_Create_Parms size mismatch");
static_assert(offsetof(IVP_Environment_Create_Parms, MemoryBlockSize) == 0x08, "Create_Parms+0x08");
static_assert(offsetof(IVP_Environment_Create_Parms, UniverseManager) == 0x14, "Create_Parms+0x14");
static_assert(offsetof(IVP_Environment_Create_Parms, RangeManager) == 0x2C, "Create_Parms+0x2C");

// FUN_063505f0 (decomp-tracker item 3141, Common.dll.txt:427376-427384, 9
// insns). Zero-fills the 0x54-byte sub-block embedded at IVP_Environment+0x34
// (`MOV ECX,0x15; REP STOSD`) and returns it. Sole caller is the constructor
// below, which runs it as a member sub-object ctor BEFORE its own body's
// P_MEM_CLEAR wipes the same bytes again -- see the constructor's own comment.
void* IVP_Environment_Init_Block34(void* block34);

// FUN_0634bf40 (decomp-tracker item 3030, Common.dll.txt:419977-419978, 2
// insns): `*(float*)this = 0.3f` (0x3e99999a). The one-field initializer for
// IVP_Environment::default_friction (+0x88).
void IVP_Environment_Init_DefaultFriction(void* block88);

// FUN_0634bf30 (decomp-tracker item 3029, Common.dll.txt:419962-419967, 6
// insns): the sub-object constructor proper -- delegates to
// IVP_Environment_Init_DefaultFriction above and returns `this`.
void* IVP_Environment_Init_Block88(void* block88);

// FUN_0634c6d0 (decomp-tracker item 3033, Common.dll.txt:420587-420592, 6
// insns, __thiscall + one stack arg). IVP_Environment::set_delta_PSI_time:
// stores the timestep at +0xAC and its reciprocal (1.0f / dt, with the 1.0f
// coming from DAT_063a53d0, confirmed `00 00 80 3f`) at +0xB0.
//
// CORRECTS item 3033's own long-standing note, which called `this` "the retail
// look-ahead RMGR object". It is the IVP_Environment itself: the constructor
// calls this with `MOV ECX,ESI` where ESI is the environment under
// construction (Common.dll.txt:0634c29c-0634c2a4), and +0xAC/+0xB0 are
// delta_time/inv_delta_time above. The other caller, FUN_0628c900, reaches it
// through PhySys's own environment pointer at [this+0x18].
void IVP_Environment_SetDeltaPsiTime(IVP_Environment* env, float dt);

// FUN_0634ccd0 (.text:0634ccd0-0634ccf1, thiscall, RET 8):
// ++env->sim_step_count (+0x110); current_time = {TimeHi, TimeLo} (+0xf8/+0xfc).
// cmn-phy5 2026-10-08.
void IVP_Environment_SetCurrentTime(IVP_Environment* env, float TimeHi, float TimeLo);

// FUN_0634cc80 (.text:0634cc80-0634ccc7, thiscall, RET 4) -- the library's
// IVP_Environment::simulate_dtime: Freeze-time bookkeeping on +0x8c with the
// target time (current offset + base + DeltaTime), then
// time_manager->simulate_variable_time_step(env, DeltaTime). PhySys calls it
// from its Update (0628caed / 0628cb63 / 0628cb9d). cmn-phy5 2026-10-08.
void IVP_Environment_SimulateDTime(IVP_Environment* env, float DeltaTime);

// FUN_0634cb20 (.text:0634cb20-0634cb5f, thiscall, RET 4) -- the library's
// IVP_Environment::set_gravity: +0xb4/+0xb8/+0xbc = *Gravity, +0xc4 =
// |Gravity| (FUN_06347e60), then standard_gravity_controller->
// set_grav_vector(Gravity). Callers: the env ctor (0634c235) and PhySys
// (0628ca2f). cmn-phy5 2026-10-08.
void IVP_Environment_SetGravity(IVP_Environment* env, const IVP_U_Point* Gravity);

// FUN_0634cb90 (.text:0634cb90-0634cb97): `MOV ECX,[ECX+0xc]; JMP 06261d90`,
// and 06261d90 is the /OPT:ICF-shared `MOV EAX,[ECX]; RET` (Ghidra labels it
// BaseData::get_base_id). So: env->cluster_manager (+0x0c) ->root_cluster
// (+0x00), the cluster every IVP object is created into -- the library's
// IVP_Environment::get_root_cluster. cmn-phy5 2026-10-08, typed cmn-phy7.
IVP_Cluster* IVP_Environment_GetRootCluster(IVP_Environment* env);

// FUN_0634cd80 (.text:0634cd80-0634cdea, RET 0x10) -- the library's
// IVP_Environment::create_polygon: `new(0xa8)` then FUN_06362280 (IVP_Polygon
// ctor) with (root cluster, SurfaceManager, Template, Rotation, Position).
// Retail callers: PhySys 0628af7e / 0628b367 / 0628c838. cmn-phy5 2026-10-08.
class IVP_Polygon;
class IVP_Ball;
class IVP_SurfaceManager;
struct IVP_RealObjectConstructionTemplate;
IVP_Polygon* IVP_Environment_CreatePolygon(IVP_Environment* env, IVP_SurfaceManager* SurfaceManager,
                                           const IVP_RealObjectConstructionTemplate* Template,
                                           const IVP_U_Quat* Rotation, const IVP_U_Point* Position);

// FUN_0634cdf0 (.text:0634cdf0-0634ce5a, RET 0x10) -- the library's
// IVP_Environment::create_ball: `new(0xa8)` then FUN_06362570 (IVP_Ball ctor)
// with (root cluster, BallRadius, Template, Rotation, Position). BallRadius is
// the library's IVP_Template_Ball*, whose only member read here is the float
// radius at +0 (FADD [EDX] at :063625c1). Retail callers: PhySys 0628c8f0 and
// the environment ctor's static_object (0634c678). cmn-phy5 2026-10-08.
IVP_Ball* IVP_Environment_CreateBall(IVP_Environment* env, const float* BallRadius,
                                     const IVP_RealObjectConstructionTemplate* Template,
                                     const IVP_U_Quat* Rotation, const IVP_U_Point* Position);

// The constructor's own default: 0x3c783e10 == 1.0f/66.0f exactly
// (Common.dll.txt:0634c297, `PUSH 0x3c783e10`). 66 Hz is IVP's stock PSI rate.
extern const float IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME;

// FUN_06360090 (item 3384). One of two once-only global dispatch-table
// initializers the constructor calls back to back (Common.dll.txt:0634c5c0 and
// 0634c5c5); each fills a function-pointer table in .data. See
// ivp_environment.cpp for the per-slot TODO list -- the table is materialized
// with the right shape and extent, but its handlers are not ported yet.
//
// Its sibling, FUN_06353940, is
// IVP_Mindist_Minimize_Solver::init_mms_function_table
// (src/IVP/ivp_collision/ivp_mindist_minimize.h).
void IVP_InitSimUnitDispatchTable(void);   // FUN_06360090 -> DAT_0658af9c

// FUN_063608a0 (decomp-tracker item 3400, Common.dll.txt:453612-453659).
// IVP_Environment::sim_unit_manager's (+0x04) own constructor. See
// ivp_environment.cpp for its remaining open sub-calls.
void* IVP_SimUnitManager_Construct(void* self);

// FUN_0634bf50 (decomp-tracker item 3031, Common.dll.txt:419989-420559, 487
// insns). IVP_Environment's real constructor.
//
// `Registry` is retail's param_1: always &DAT_0658aa00, the process-wide
// IVP_Environment_Manager (see environment_manager above). `Parms` is param_2.
// `Name` is param_3, strdup'd into environment_name. `ClientData` is param_4.
IVP_Environment* IVP_Environment_Construct(IVP_Environment* env,
                                           IVP_Environment_Manager* Registry,
                                           const IVP_Environment_Create_Parms* Parms,
                                           const char* Name,
                                           void* ClientData);

// ============================================================================
// IVP_Environment_Manager -- the 16-byte process-wide singleton at .data
// DAT_0658aa00 (cmn-phy5, 2026-10-08; it was an opaque `g_DAT_0658aa00[16]`
// blob in the former common_stubs.cpp).
//   static init LAB_0634cc20: FUN_0634cc30 -> FUN_0634cb70 (ctor: +0 = 0,
//     word +4 = word +6 = 0, +8 = 0) and FUN_0634cc40, which registers
//     LAB_0634cc50 with atexit (FUN_06391f9e); LAB_0634cc50 runs FUN_06348be0
//     once (guard bit 1 of DAT_0658a9f8) = the IVP_U_Vector destructor of +4
//     (free elems unless it equals this+0xc). A C++ global with these members
//     reproduces both.
//   +0x04 is the environment registry: IVP_Environment_Construct appends the
//     new environment (FUN_0634bf50 :0634c25a-:0634c281) and the destructor
//     removes it.
// ============================================================================
class IVP_Environment_Manager
{
public:
    int32_t Field_0x00;                        // +0x00: zeroed by FUN_0634cb70; TODO: no reader traced yet.
    IVP_U_Vector<IVP_Environment> Environments; // +0x04..+0x0b
    int32_t InlineSentinel_0x0c;               // +0x0c: never written; only its address is compared by FUN_06348be0.

    IVP_Environment_Manager() : Field_0x00(0), InlineSentinel_0x0c(0) {}

    // FUN_0634cc70 (.text:0634cc70-0634cc75): `MOV EAX,&DAT_0658aa00; RET`.
    static IVP_Environment_Manager* get_environment_manager();

    // FUN_0634cba0 (.text:0634cba0-0634cc10, RET 0xc): operator_new(0x148) and
    // FUN_0634bf50(this manager, Parms, LicenseName, LicenseKey). PhySys's world
    // init (0628c96d-0628c983) passes "digitalanvil" and 0x11c2de34.
    IVP_Environment* create_environment(IVP_Environment_Create_Parms* Parms,
                                        const char* LicenseName, uint32_t LicenseKey);
};
static_assert(sizeof(IVP_Environment_Manager) == 0x10, "IVP_Environment_Manager is the 16-byte DAT_0658aa00");
