// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// IVP listener / collision-filter / performance-counter interfaces
// (2026-10-08, claude-code:cmn-physys).
//
// Retail compiles these IVP SDK interface classes into Common.dll. Their names
// are RTTI-confirmed through the Complete Object Locators at vtable[-4] of the
// game classes that derive from them:
//   PhysicalSystem (COL .rdata:063a7344 / 063a73e0 / 063a7330) lists
//     IVP_Listener_Object at +0, IVP_Listener_Collision at +4,
//     IVP_Collision_Filter_Coll_Group_Ident and IVP_Collision_Filter at +0xc;
//   PhySys::PhyPerfCounter (COL .rdata:063a6b68) lists IVP_PerformanceCounter.
// Slot order and arity are read off each class's own vtable in .rdata and off
// the overriding bodies. Slots whose meaning no traced caller fixes keep a
// SlotNN name.
// ============================================================================

#include <cstdint>

class IVP_Environment;
class IVP_Real_Object;
class IVP_Compact_Edge;

// The event record IVP passes to IVP_Listener_Object slots: {environment,
// real object}. Declared in ivp_environment.h.
struct IVP_Environment_Object_Notify_Event;

// What an IVP_Event_Collision points at. Only the members a traced consumer
// reads are named; the object is larger and not modelled further here.
// Consumer: PhysicalSystem::event_pre_collision (.text:0628c42b..0628c547).
struct IVP_Contact_Situation
{
    float surf_normal[4];                     // +0x00 (x, y, z read)
    float speed[4];                           // +0x10 (x, y, z read; written at .text:06358a96..06358aa4)
    float contact_point_ws[4];                // +0x20 (x, y, z read)
    IVP_Real_Object* objects[2];              // +0x30 / +0x34
    const IVP_Compact_Edge* compact_edges[2]; // +0x38 / +0x3c
};

// Built on the stack by IVP right before it fires the collision listeners
// (.text:06358a7a..06358aa7 -> FUN_0634cf60).
struct IVP_Event_Collision
{
    float d_time_since_last_collision;        // +0x00
    IVP_Environment* environment;             // +0x04
    IVP_Contact_Situation* contact_situation; // +0x08
};

// vtable .rdata:0639c288: four _purecall slots + deleting dtor LAB_06289740.
// IVP fires the slots through env->object_listeners (+0x124):
// FUN_0634ced0 slot 0, FUN_0634cea0 slot 1, FUN_0634cf30 slot 2, FUN_0634cf00 slot 3.
class IVP_Listener_Object
{
public:
    virtual void event_object_deleted(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 0
    virtual void event_object_created(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 1
    virtual void event_object_revived(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 2
    virtual void event_object_frozen(IVP_Environment_Object_Notify_Event* Event) = 0;  // slot 3
    virtual ~IVP_Listener_Object() {}                                                  // slot 4
};

// IVP_Listener_Collision::enabled_callbacks bit gated by FUN_0634cf60
// (`TEST byte ptr [ECX+4],1` before it calls slot 0).
enum : int32_t
{
    IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION = 0x01,
};

// vtable .rdata:0639c2a0: four `RET 4` no-op slots (.text:062ed730) + deleting
// dtor LAB_06289770; one data member at +4. IVP keeps these in
// env->vector_listeners_c8 (FUN_0634d050 appends, FUN_0634cf60 fires slot 0).
class IVP_Listener_Collision
{
public:
    explicit IVP_Listener_Collision(int32_t InEnabledCallbacks)
        : enabled_callbacks(InEnabledCallbacks)
    {
    }

    virtual void event_pre_collision(IVP_Event_Collision* /*Event*/) {}  // slot 0
    // slot 1: fired by IVP_Cluster_Manager 0634b090 (enabled_callbacks & 2)
    // from ~IVP_Real_Object (.text:0634537a-06345381) with the object being
    // deleted. cmn-phy7 2026-10-08 (was event_post_collision(IVP_Event_Collision*)).
    virtual void event_collision_object_deleted(IVP_Real_Object* /*Object*/) {}
    virtual void Slot02(void* /*Arg*/) {}                                 // slot 2 (enabled_callbacks & 4): 0634b0e0, 0634cfa0
    virtual void Slot03(void* /*Arg*/) {}                                 // slot 3 (enabled_callbacks & 4): 0634b140, 0634cfe0
    virtual ~IVP_Listener_Collision() {}                              // slot 4

    int32_t enabled_callbacks; // +0x04
};

// ============================================================================
// IVP_Listener_Hull -- what an IVP_Hull_Manager's priority pool holds as entry
// payloads (cmn-phy5, 2026-10-08). Library name (Ipion). Abstract base vtable
// .rdata:063a5694, 4 slots: three _purecall + 0628f050 (`RET 8`), installed by
// IVP_OV_Element's ctor (:0635c024) before its own table. The hull manager
// dispatches slot 1 when the hull value passes an entry's key
// (FUN_06351f80 / FUN_06351b90, IVP_Calc_Next_PSI_Solver), slot 2 when it is
// destroyed (FUN_063521f0) and slot 3 when it rebases (FUN_06352220, args
// (-outer, -inner)). Implementations: IVP_OV_Element (vtable 063a5680) and the
// mindist synapses (vtable 063a55cc). No virtual destructor at this level.
// +0x04 is the entry's pool slot index, the full dword the insert returned.
// ============================================================================
class IVP_Hull_Manager;
class IVP_Listener_Hull
{
public:
    virtual int32_t get_type() = 0;                                                        // slot 0
    virtual void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) = 0; // slot 1 (RET 8)
    virtual void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) = 0;  // slot 2 (RET 4)
    virtual void hull_manager_is_reset(float /*DeltaTime*/, float /*CenterDeltaTime*/) {}   // slot 3, 0628f050 `RET 8`

    uint32_t HullPoolSlot; // +0x04

protected:
    IVP_Listener_Hull() {}
    ~IVP_Listener_Hull() {}
};

// The record IVP_Environment's PSI broadcast passes (FUN_0634d020): a one-dword
// stack local holding the environment (:0634d02e `MOV [ESP+8],EDI`, its
// address pushed at :0634d040-:0634d044). cmn-phy5 2026-10-08.
struct IVP_Event_PSI
{
    IVP_Environment* environment; // +0x00
};

// Elements of IVP_Environment::vector_listeners_d0 (+0xd0). The only call
// traced is slot 0 with an IVP_Event_PSI* (FUN_0634d020 :0634d03e-:0634d045,
// `MOV EDX,[ECX]; PUSH EAX; CALL [EDX]`); no implementation was traced, so no
// further slot is declared. Library name IVP_Listener_PSI. cmn-phy5 2026-10-08.
class IVP_Listener_PSI
{
public:
    virtual void event_PSI(IVP_Event_PSI* Event) = 0; // slot 0
    // slot 1: ~IVP_Environment (.text:0634c72c-0634c746) calls it on every
    // element, last to first, with the environment (cmn-phy7 2026-10-08).
    virtual void environment_will_be_deleted(IVP_Environment* Environment) = 0;
};

// vtable .rdata:0639c40c: two _purecall slots + deleting dtor FUN_0628edb0
// (stamps 0639c40c through FUN_0634bc50).
class IVP_Collision_Filter
{
public:
    // slot 0: nonzero (IVP_BOOL) = the pair may be collision-tested.
    virtual int32_t check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B) = 0;
    // slot 1
    virtual void environment_will_be_deleted(IVP_Environment* Env) = 0;
    // slot 2
    virtual ~IVP_Collision_Filter() {}
};

// vtable .rdata:0639c3fc, ctor FUN_0634bcb0, 8 bytes.
class IVP_Collision_Filter_Coll_Group_Ident : public IVP_Collision_Filter
{
public:
    // FUN_0634bcb0 (thiscall, RET 4): vtable 0639c3fc, +0x04 = the argument.
    explicit IVP_Collision_Filter_Coll_Group_Ident(int32_t bInDeleteOnEnvDelete);

    // FUN_0634bc60 (RET 8)
    virtual int32_t check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B) override;
    // LAB_0634bcd0 (RET 4)
    virtual void environment_will_be_deleted(IVP_Environment* Env) override;
    // FUN_0628ed90 (stamps 0639c3fc through FUN_0634bca0, then the base's).
    virtual ~IVP_Collision_Filter_Coll_Group_Ident() {}

    int32_t bDeleteOnEnvDelete; // +0x04
};

// vtable .rdata:0639bac8: five _purecall slots + deleting dtor LAB_06285e10.
// Arities from PhySys::PhyPerfCounter's overrides (vtable 0639baac):
// RET / RET 4 / RET / RET 4 / RET 8.
class IVP_PerformanceCounter
{
public:
    virtual void Slot00() = 0;                                                  // slot 0
    virtual void Slot01(int32_t Arg) = 0;                                       // slot 1
    virtual void Slot02() = 0;                                                  // slot 2
    // slot 3: PhyPerfCounter's body (LAB_06285e30) deletes the counter.
    virtual void environment_is_going_to_be_deleted(IVP_Environment* Env) = 0;
    // slot 4: 8 bytes of stack arguments; their type is not derived.
    virtual void Slot04(float Arg0, float Arg1) = 0;
    virtual ~IVP_PerformanceCounter() {}                                        // slot 5
};
