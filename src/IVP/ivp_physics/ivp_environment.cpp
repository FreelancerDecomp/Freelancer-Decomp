// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_environment.h"
#include "../../Common/physics_tick_profile.h" // Owner-requested port-only profiling
#include "ivp_core.h"
#include "ivp_anomaly_manager.h"
#include "ivp_material.h"
#include "ivp_active_value.h"
#include "ivp_real_object.h"
#include "ivp_mindist.h" // IVP_Ledge_Mindist / IVP_Synapse_Real (IVP_Anomaly_Manager::inter_penetration)
#include "../ivp_collision/ivp_collision.h" // IVP_Collision_Delegator_Root(_Mindist): vector_12c elements
#include "../ivp_collision/ivp_mindist_minimize.h" // IVP_Mindist_Minimize_Solver::init_mms_function_table (06353940)
#include "ivp_listener.h" // IVP_Collision_Filter(_Coll_Group_Ident): collision_filter (+0x18); IVP_Listener_PSI
#include "ivp_ov_element.h" // IVP_Mindist_Manager (mindist_manager, +0x10)
#include "ivp_simulation_unit.h"       // IVP_Sim_Units_Manager (env+0x08; 06349380 / 063499c0)
#include "ivp_calc_next_psi_solver.h"  // IVP_Calc_Next_PSI_Solver::commit_all_* (06351eb0 / 06351f80)
#include "ivp_cluster_manager.h"       // IVP_Cluster_Manager (+0x0C), IVP_Universe_Manager (+0x2C)
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "../ivp_utility/ivp_u_vector.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// QueryPerformanceCounter / QueryPerformanceFrequency, which retail's
// IVP_PerformanceCounter_Simple::Slot01 (063602e0) calls through the IAT.
// Included after the IVP headers so its macros cannot reach them.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>


// ============================================================================
// FUN_0634bbe0 (.text:0634bbe0-0634bc4e), thiscall on the environment:
//   EDI = n_elems (+0xda) - 1, JS done; per entry from the top: ESI =
//   elems[EDI] (+0xdc); FUN_06347d80(core); core->Flags &= 0xffffffcf; EDI--, JNS.
//   Then the IVP_U_Vector clear: elems == the inline buffer (+0xe0) -> only
//   n_elems = 0; otherwise FUN_06343fb0(elems) when non-null, elems = 0,
//   mem_size (+0xd8) = 0, n_elems = 0.
// ============================================================================
void IVP_Environment_Process_Wake_Queue(IVP_Environment* env)
{
    for (int i = static_cast<int>(env->event_queue.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* core = static_cast<IVP_Core*>(env->event_queue.elems[i]);
        core->wake_or_ensure_simulation(); // :0634bbfc FUN_06347d80
        core->Flags &= ~0x30;              // :0634bc01-:0634bc07
    }

    if (env->event_queue.elems == reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(&env->event_queue) + 8))
    {
        env->event_queue.n_elems = 0;
        return;
    }

    if (env->event_queue.elems != nullptr)
    {
        free(env->event_queue.elems); // FUN_06343fb0
    }

    env->event_queue.elems = nullptr;
    env->event_queue.mem_size = 0;
    env->event_queue.n_elems = 0;
}

// ============================================================================
// FUN_0634bb20: IVP_Environment::dequeue_core (single-entry removal from the
// wake/event queue, as opposed to IVP_Environment_Process_Wake_Queue which
// drains the whole thing). Used when a specific IVP_Core is being torn down
// mid-queue (see FUN_063477e0 / IVP_Core destructor, Common.dll.txt:411272,
// which calls this on `Core->Environment` with `Core` itself as the search
// target, passing a constant 1 in an unused stack slot). Disasm-verified
// field-for-field against IVP_Environment::event_queue (+0xD8/+0xDA/+0xDC =
// mem_size/n_elems/elems, matching IVP_U_Vector_Base) and IVP_Core::Flags
// (+0x00): backward linear search of event_queue for `core`; if found, shift
// remaining elements down (same pattern as IVP_U_Vector::RemoveAt) and clear
// core->Flags &= ~0x30 (the same "already queued" bits 0x10/0x20 that
// IVP_Environment_Process_Wake_Queue also clears when draining). If `core`
// is not present in the queue, this is a no-op.
// ============================================================================
void IVP_Environment_Dequeue_Core(IVP_Environment* env, IVP_Core* core)
{
    IVP_U_Vector_Base& Queue = env->event_queue;

    int FoundIndex = -1;
    for (int i = static_cast<int>(Queue.n_elems) - 1; i >= 0; --i)
    {
        if (Queue.elems[i] == core)
        {
            FoundIndex = i;
            break;
        }
    }

    if (FoundIndex < 0)
    {
        return;
    }

    // Shift everything after FoundIndex down by one (retail's manual
    // IVP_U_Vector-shift loop, equivalent to IVP_U_Vector<T>::RemoveAt).
    for (int i = FoundIndex; i < static_cast<int>(Queue.n_elems) - 1; ++i)
    {
        Queue.elems[i] = Queue.elems[i + 1];
    }
    --Queue.n_elems;

    core->Flags &= ~0x30;
}

// ============================================================================
// FUN_0634bb90 (.text:0634bb90-0634bbd1): IVP_Environment::add_revive_core.
//   0634bb95-0634bb9c  (core->Flags & 0x30) == 0x10 (`AND EAX,0x30; CMP AL,0x10;
//                      JZ`) -> already queued, return.
//   0634bb9f-0634bbb0  ESI = &event_queue (+0xD8); `CMP n_elems, mem_size; JC`
//                      skips the grow, so FUN_063441a0 runs on n_elems >= mem_size.
//   0634bbb5-0634bbc1  elems[n_elems] = core; ++n_elems.
//   0634bbc5-0634bbcd  Flags = (Flags & ~0x20) | 0x10.
// No null test on either pointer.
// ============================================================================
void IVP_Environment_Enqueue_Core(IVP_Environment* env, IVP_Core* core)
{
    if ((core->Flags & 0x30u) == 0x10u)
    {
        return;
    }

    IVP_U_Vector_Base_Append(&env->event_queue, core);
    core->Flags = (core->Flags & ~0x20u) | 0x10u;
}

// ============================================================================
// FUN_06348f50 (.text:06348f50-06348f5d): IVP_Controller_Manager::
// ensure_core_to_be_in_simulation. `EAX = [ESP+4]; ECX = [EAX+0xc]; PUSH EAX;
// CALL FUN_0634bb90; RET 4` -- the caller's ECX (the controller manager) is
// overwritten unread.
// ============================================================================
void IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(IVP_Core* core)
{
    IVP_Environment_Enqueue_Core(core->Environment, core);
}

// Collision listeners are the canonical IVP_Listener_Collision objects.
// 2026-10-08 (claude-code:cmn-phy4): FUN_0634cf60, .text:0634cf60-0634cf8f.
// ESI = n_elems(+0xca) - 1 down to 0; elems (+0xcc) re-read every pass; no
// null test of the entry; `TEST byte ptr [ECX+4],1` gates `CALL [EDX]` (slot 0)
// with the argument. The old body skipped the call and added a null test.
void IVP_Environment_Notify_Listeners_C8(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->vector_listeners_c8.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)env->vector_listeners_c8.elems[i];
        if ((Listener->enabled_callbacks & 1) != 0)
        {
            Listener->event_pre_collision((IVP_Event_Collision*)arg);
        }
    }
}

// ============================================================================
// FUN_0634cf30: IVP_Environment_Notify_Object_Listeners (decomp-tracker item
// 3055). See header comment in ivp_environment.h for the full trace. Sole
// call site examined this pass: FUN_0634b480/IVP_Core::revive_pass (item
// 2989, ivp_core.cpp), which builds the {Environment*, IVP_Real_Object*}
// event record on its own stack per attached object.
// ============================================================================
// 2026-10-08 (claude-code:cmn-phy4): .text:0634cf30-0634cf5a. n_elems (+0x126)
// - 1 down to 0, elems (+0x128) re-read every pass, no null test, no gate:
// `CALL [EDX+0x8]` (slot 2) with the argument.
void IVP_Environment_Notify_Object_Listeners(IVP_Environment* env, void* event_data)
{
    for (int i = static_cast<int>(env->object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)env->object_listeners.elems[i])->event_object_revived((IVP_Environment_Object_Notify_Event*)event_data);
    }
}

// ============================================================================
// Shared helper: releases an IVP_U_Vector_Base's heap buffer unless `elems`
// is still pointing at the vector's own inline (1-element SSO) storage --
// the exact idiom IVP_Environment_Process_Wake_Queue above already applies
// by hand to env->event_queue. Factored out here since the real destructor
// below repeats it 6 times verbatim (Common.dll.txt:420839-420930).
// ============================================================================
static void IVP_U_Vector_Base_Clear(IVP_U_Vector_Base& v)
{
    void* inlineStorage = reinterpret_cast<uint8_t*>(&v) + 8;
    if (v.elems == inlineStorage)
    {
        v.n_elems = 0;
        return;
    }
    if (v.elems != nullptr)
    {
        free(v.elems); // FUN_06343fb0
    }
    v.elems = nullptr;
    v.mem_size = 0;
    v.n_elems = 0;
}

// ============================================================================
// FUN_0634caf0 (decomp-tracker item 3035). See header comment.
// ============================================================================
// 2026-10-08 (claude-code:cmn-phy4): .text:0634caf0-0634cb1a. n_elems (+0x12e)
// - 1 down to 0, elems (+0x130) re-read every pass, no gate: `CALL [EDX+0x10]`
// (slot 4) with the argument.
void IVP_Environment_Notify_Vector12c_Slot4(IVP_Environment* env, void* arg)
{
    // Slot 4 of IVP_Collision_Delegator_Root = object_is_removed_from_collision_detection
    // (cmn-phy5, 2026-10-08: typed now that the delegator is a real class).
    for (int i = static_cast<int>(env->vector_12c.n_elems) - 1; i >= 0; --i)
    {
        IVP_Collision_Delegator_Root* Delegator =
            static_cast<IVP_Collision_Delegator_Root*>(env->vector_12c.elems[i]);
        Delegator->object_is_removed_from_collision_detection(static_cast<IVP_Real_Object*>(arg));
    }
}

// ============================================================================
// FUN_0634cd00 (decomp-tracker item 3047). See header comment.
// ============================================================================
IVP_Env_Buffer_Node::~IVP_Env_Buffer_Node()
{
    if (OwnedBuffer != nullptr) // 0634cd03-0634cd08
    {
        free(OwnedBuffer);      // 0634cd0b FUN_06343fb0
        OwnedBuffer = nullptr;  // 0634cd13
    }
}

// ============================================================================
// FUN_0634cd20 (decomp-tracker item 3048). See header comment. Disasm-
// verified against the RAW .txt listing (Common.dll.txt:421270-421315) --
// each node's own +0x28 buffer is freed BEFORE IVP_Environment_Free_
// Field13c_Node runs on it (making that helper's own free a guaranteed
// no-op here, exactly as retail does it), then the node itself is freed.
// ============================================================================
void IVP_Environment_Free_Buffer_Node_List(IVP_Environment* env)
{
    IVP_Env_Buffer_Node* Node = env->buffer_node_list;  // 0634cd25
    while (Node != nullptr)                             // 0634cd2d / 0634cd5b
    {
        if (Node->OwnedBuffer != nullptr)               // 0634cd32-0634cd37
        {
            free(Node->OwnedBuffer);                    // 0634cd3a FUN_06343fb0
            Node->OwnedBuffer = nullptr;                // 0634cd42
        }

        IVP_Env_Buffer_Node* const Next = Node->Next;   // 0634cd47
        delete Node; // 0634cd4b ~IVP_Env_Buffer_Node (its free is a no-op now) + 0634cd53 operator delete
        Node = Next;
    }
    env->buffer_node_list = nullptr;                    // 0634cd63 / 0634cd6d
}

// ============================================================================
// FUN_0634ce60 (decomp-tracker item 3051). See header comment.
// ============================================================================
// 2026-09-17 (lane claude:common-ivpenv): the realloc is now REAL. This used
// to detect the full-vector case and then do nothing about it, falling
// straight through to `elems[n_elems] = listener` -- a one-past-the-end write
// on every append that should have grown the buffer, and an unconditional
// null dereference on a freshly-constructed environment (where elems is null
// and mem_size is 0, so the very first append takes this path). The old
// comment was right that IVP_U_Vector<T>::Realloc is the equivalent -- that
// template's layout is in fact identical to IVP_U_Vector_Base's -- it just
// never wired it up. See IVP_U_Vector_Base_Realloc below.
void IVP_Environment_Append_Object_Listener(IVP_Environment* env, void* listener)
{
    IVP_U_Vector_Base_Append(&env->object_listeners, listener);
}

// ============================================================================
// FUN_0634ced0 / FUN_0634cf00 (decomp-tracker items 3053/3054). See header
// comment.
// ============================================================================
// 2026-10-08 (claude-code:cmn-phy4): .text:0634ced0-0634cef9 and
// 0634cf00-0634cf2a. n_elems (+0x126) - 1 down to 0, elems (+0x128) re-read
// every pass, no null test, no gate: `CALL [EDX]` (slot 0) and
// `CALL [EDX+0xc]` (slot 3) with the argument.
void IVP_Environment_Notify_Object_Listeners_Slot0(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)env->object_listeners.elems[i])->event_object_deleted((IVP_Environment_Object_Notify_Event*)arg);
    }
}

void IVP_Environment_Notify_Object_Listeners_Slot3(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)env->object_listeners.elems[i])->event_object_frozen((IVP_Environment_Object_Notify_Event*)arg);
    }
}

// Notify each object listener of a newly created object, last to first.
void IVP_Environment_Notify_Object_Listeners_Slot1(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)env->object_listeners.elems[i])->event_object_created(
            (IVP_Environment_Object_Notify_Event*)arg);
    }
}

// ============================================================================
// FUN_0634cfa0 / FUN_0634cfe0 (decomp-tracker items 3057/3058). See header
// comment -- gated on flags-byte bit 2 (&4), unlike IVP_Environment_
// Notify_Listeners_C8's &1 gate.
// ============================================================================
// 2026-10-08 (claude-code:cmn-phy4): .text:0634cfa0-0634cfd0 and
// 0634cfe0-0634d010. n_elems (+0xca) - 1 down to 0, elems (+0xcc) re-read every
// pass, no null test; `TEST byte ptr [ECX+4],4` gates `CALL [EDX+0x8]` (slot 2)
// and `CALL [EDX+0xc]` (slot 3) respectively. The old bodies skipped the call
// and added a null test.
void IVP_Environment_Notify_Listeners_C8_Slot2(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->vector_listeners_c8.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)env->vector_listeners_c8.elems[i];
        if ((Listener->enabled_callbacks & 4) != 0)
        {
            Listener->Slot02(arg);
        }
    }
}

void IVP_Environment_Notify_Listeners_C8_Slot3(IVP_Environment* env, void* arg)
{
    for (int i = static_cast<int>(env->vector_listeners_c8.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)env->vector_listeners_c8.elems[i];
        if ((Listener->enabled_callbacks & 4) != 0)
        {
            Listener->Slot03(arg);
        }
    }
}

// ============================================================================
// FUN_0634d050 (decomp-tracker item 3060). See header comment.
// ============================================================================
void IVP_Environment_Append_Listener_C8(IVP_Environment* env, void* listener)
{
    // Realloc now real -- same fix as IVP_Environment_Append_Object_Listener
    // above, and the same one-past-the-end bug before it.
    IVP_U_Vector_Base_Append(&env->vector_listeners_c8, listener);
}

// ============================================================================
// IVP_Cache_Object_Manager (+0x94). get_cache_object (0634d530) is in
// ivp_real_object.cpp next to the cache-object refresh it calls.
// ============================================================================

// FUN_0634d5c0 (.text:0634d5c0-0634d5e1), thiscall (n), RET 4:
// [this] = n; [this+8] = FUN_06343f80(0x70, n) (calloc: malloc(n*0x70) + zero);
// [this+4] = 0.
IVP_Cache_Object_Manager::IVP_Cache_Object_Manager(int32_t NumberOfCacheObjects)
{
    number_of_cache_objects = NumberOfCacheObjects;
    cache_objects = static_cast<IVP_NarrowPhaseCacheSlot*>(
        calloc(static_cast<size_t>(NumberOfCacheObjects), sizeof(IVP_NarrowPhaseCacheSlot))); // FUN_06343f80
    next_free_index = 0;
}

// FUN_0634d5f0 (.text:0634d5f0-0634d63a), thiscall: for i in 0..n-1 (n re-read
// from [this] each pass, JLE skips when n <= 0) the slot's object (+8): when
// set, FUN_0634d510(object) drops it. Then FUN_06343fb0(cache_objects) and
// cache_objects = 0 when non-null.
IVP_Cache_Object_Manager::~IVP_Cache_Object_Manager()
{
    for (int32_t I = 0; I < number_of_cache_objects; ++I)
    {
        IVP_Real_Object* const Owner = cache_objects[I].Owner;
        if (Owner != nullptr)
        {
            IVP_Real_Object_Free_Cache_Object(Owner); // FUN_0634d510
        }
    }

    if (cache_objects != nullptr)
    {
        free(cache_objects); // FUN_06343fb0
        cache_objects = nullptr;
    }
}

// ============================================================================
// IVP_Env_Interval_Gate (+0x8C)
// ============================================================================

// DAT_0658afdc: .data, no initializer (zero). Written only by set_time.
static float GIvpIntervalGateLastTime = 0.0f;

// FUN_063605e0 (.text:063605e0-06360631), thiscall, SEH-framed (the two vector
// members): VectorA/VectorB = {0, 0, null} (words +4/+6, dword +8, words
// +0xc/+0xe, dword +0x10); [this] = 1; [this+0x1c] = 1.0f; [this+0x18] = 1.
// +0x14 is not written.
IVP_Env_Interval_Gate::IVP_Env_Interval_Gate()
{
    Field_0x00 = 1;
    Threshold = 1.0f;
    bWithinInterval = 1;
}

// FUN_06360640 (.text:06360640-063606c0): VectorB then VectorA cleared with the
// IVP_U_Vector clear idiom -- exactly the member destructors in reverse order.
IVP_Env_Interval_Gate::~IVP_Env_Interval_Gate()
{
}

// FUN_063605a0 (.text:063605a0-063605d6), thiscall (t), RET 4: [this+0x14] = t;
// FLD t; FSUB DAT_0658afdc; FCOMP [this+0x1c]; TEST AH,0x1; JE else ->
// less or unordered: [this+0x18] = 1. Else DAT_0658afdc = t; [this+0x18] = 0.
void IVP_Env_Interval_Gate::set_time(float Time)
{
    LastTime = Time;
    if (!((double)(Time) - GIvpIntervalGateLastTime >= Threshold))
    {
        bWithinInterval = 1;
        return;
    }
    GIvpIntervalGateLastTime = Time;
    bWithinInterval = 0;
}

// ============================================================================
// FUN_0634c6f0: ~IVP_Environment (decomp-tracker item 3034). See header
// comment for the summary and the FULLPORT-DEFERRED[IVP] policy applied
// throughout. Order below is disasm-verified against the RAW .txt listing
// (Common.dll.txt:420597-420934) instruction-by-instruction, NOT the
// Ghidra pseudocode (whose reordering would read several already-nulled
// fields through a stale pointer if followed literally).
// ============================================================================
void IVP_Environment_Destructor(IVP_Environment* env)
{
    if (env->state_magic == 0x1e240)
    {
        // 0634c72a-0634c746: vector_listeners_d0 (IVP_Listener_PSI), last to
        // first, slot 1 with the environment.
        for (int i = static_cast<int>(env->vector_listeners_d0.n_elems) - 1; i >= 0; --i)
        {
            static_cast<IVP_Listener_PSI*>(env->vector_listeners_d0.elems[i])->environment_will_be_deleted(env);
        }

        // 0634c748-0634c755: FUN_06345450(static_object) -- `if (obj)
        // obj->vtbl[0](1)`, the deleting destructor -- then +0x30 = 0.
        IVP_Real_Object* const StaticObject = env->static_object;
        if (StaticObject != nullptr)
        {
            delete StaticObject;
        }
        env->static_object = nullptr;

        // 0634c750-0634c76e: ~IVP_Cluster_Manager (0634b300) + operator delete
        // when set; +0x0c = 0 either way.
        IVP_Cluster_Manager* const ClusterManager = env->cluster_manager;
        if (ClusterManager != nullptr)
        {
            delete ClusterManager;
        }
        env->cluster_manager = nullptr;

        // 0634c76a-0634c777: `if (+0x00) +0x00->slot6(1)` -- the deleting
        // destructor; +0x00 = 0 at 0634c781.
        delete env->standard_gravity_controller;
        env->standard_gravity_controller = nullptr;

        // 0634c77a-0634c790: operator delete of the controller manager (+0x90),
        // no null test, then +0x90 = 0.
        ::operator delete(env->controller_manager); // FUN_06391d5a
        env->controller_manager = nullptr;

        IVP_Sim_Unit_Manager* simUnitManager = env->sim_unit_manager;
        if (simUnitManager != nullptr)
        {
            // FUN_06360960 (item 3401) -- ported 2026-09-17 (lane
            // claude:w26-simunit). Drains and frees every queued event payload,
            // then the +0x04 listener and the pool itself; retail's caller does
            // the free of the manager block, as here.
            simUnitManager->DestroyContents();   // FUN_06360960
            ::operator delete(simUnitManager);   // FUN_06391d5a (allocated with operator new(0x1c))
        }
        env->sim_unit_manager = nullptr;

        // 0634c7a8-0634c7bc: operator delete of time_manager (+0x08), no null
        // test and no destructor call, then +0x08 = 0.
        ::operator delete(env->sim_units_manager); // FUN_06391d5a
        env->sim_units_manager = nullptr;

        // 0634c7b4-0634c7d6: ~IVP_Mindist_Manager (063503c0) + operator delete
        // when set; +0x10 = 0 either way.
        IVP_Mindist_Manager* const MindistManager = env->mindist_manager;
        if (MindistManager != nullptr)
        {
            delete MindistManager;
        }
        env->mindist_manager = nullptr;

        // 0634c7d1-0634c7f3: ~IVP_OV_Tree_Manager (0635c500) + operator delete
        // when set; +0x14 = 0 either way.
        IVP_OV_Tree_Manager* const OvTreeManager = env->ov_tree_manager;
        if (OvTreeManager != nullptr)
        {
            delete OvTreeManager;
        }
        env->ov_tree_manager = nullptr;

        // 0634c7eb-0634c80b: ~IVP_Env_Interval_Gate (06360640) + operator
        // delete when set; +0x8c = 0.
        IVP_Env_Interval_Gate* const IntervalGate = env->interval_gate;
        if (IntervalGate != nullptr)
        {
            delete IntervalGate;
        }
        env->interval_gate = nullptr;

        // The next 7 sub-objects are guaranteed non-null in a fully
        // constructed Environment (retail calls through their vtables
        // WITHOUT a null check -- reproduced faithfully, no check added
        // here either). None frees the raw pointer directly; each one's
        // own vtable method is presumed to handle its own teardown
        // (matching l_material_manager's confirmed slot5 = "release-if-
        // owned", item 3019's own LAB_0634bda0).
        // 0634c808-0634c814: performance_counter (+0x28) slot 3
        // (`CALL [EDX+0xc]`); retail never nulls +0x28 afterwards.
        env->performance_counter->environment_is_going_to_be_deleted(env);
        // 0634c817-0634c823: collision_filter slot 1 (`CALL [EAX+4]`), then +0x18 = 0.
        env->collision_filter->environment_will_be_deleted(env);
        env->collision_filter = nullptr;
        // NO LONGER DEFERRED (D-ivp-anomaly pass, 2026-09-17): these two are
        // the +0x20 anomaly manager's slot 4 and the +0x24 anomaly limits'
        // slot 0, both now real modeled classes (ivp_anomaly_manager.h). Both
        // slots are the same "environment_will_be_deleted" body -- delete
        // yourself if you were created owned by the Environment -- which is
        // exactly the ownership flag the Environment ctor sets to 1, so this
        // dispatch really does free both objects. The vtable-slot numbers
        // retail uses here (+0x10 on the manager, +0x00 on the limits) were an
        // independent cross-check on the two vtable layouts.
        env->anomaly_manager->EnvironmentWillBeDeleted(env);
        env->anomaly_manager = nullptr;
        env->anomaly_limits->EnvironmentWillBeDeleted(env);
        env->anomaly_limits = nullptr;
        // l_active_value_manager slot 1 (LAB_063616a0, 0634c837-0634c84c):
        // delete itself if it was created owned (ctor argument 1).
        env->l_active_value_manager->EnvironmentWillBeDeleted(env);
        env->l_active_value_manager = nullptr;
        // l_material_manager slot 5 (LAB_0634bda0): delete itself if it was
        // created owned (ctor argument 1).
        env->l_material_manager->environment_will_be_deleted(env);
        env->l_material_manager = nullptr;
        // 0634c858-0634c869: range_manager (+0x1c) slot 2 (`CALL [EDX+8]`), then +0x1c = 0.
        env->range_manager->environment_will_be_deleted(env);
        env->range_manager = nullptr;

        // Notify + tear down vector_12c (own vtable slot6 per element, then
        // free the buffer if heap-allocated).
        // .text:0634c86c-0634c886: n_elems read once, elems re-read per pass,
        // slot 6 (`CALL [EDX+0x18]`) with the environment (cmn-phy5, 2026-10-08).
        for (int i = static_cast<int>(env->vector_12c.n_elems) - 1; i >= 0; --i)
        {
            IVP_Collision_Delegator_Root* Delegator =
                static_cast<IVP_Collision_Delegator_Root*>(env->vector_12c.elems[i]);
            Delegator->environment_is_going_to_be_deleted_event(env);
        }
        IVP_U_Vector_Base_Clear(env->vector_12c);

        // 0634c8b9-0634c8db / 0634c8d3-0634c8fb: the two arenas -- free_mem
        // (thunk 06344000 -> 06344150) + operator delete when set; 0 either way.
        delete env->vector_cores_active; // ~IVP_U_Memory (06344000) + FUN_06391d5a
        env->vector_cores_active = nullptr;

        delete env->vector_cores_frozen; // ~IVP_U_Memory (06344000) + FUN_06391d5a
        env->vector_cores_frozen = nullptr;

        // ~IVP_Cache_Object_Manager (0634d5f0) + operator delete when set; +0x94 = 0.
        IVP_Cache_Object_Manager* const CacheObjectManager = env->cache_object_manager;
        if (CacheObjectManager != nullptr)
        {
            delete CacheObjectManager;
        }
        env->cache_object_manager = nullptr;

        if (env->environment_name != nullptr)
        {
            free(env->environment_name); // FUN_06343fb0
            env->environment_name = nullptr;
        }

        // Unregister `env` from environment_manager->Environments (+0xf4, +0x4).
        // Reproduced literally (cmn-phy5, 2026-10-08). The `idx >= 0` guard
        // an earlier pass added here rested on "nothing ever registers an
        // environment"; that premise is false -- the constructor appends
        // `this` to the same vector (.text:0634c25a-0634c281,
        // IVP_Environment_Construct) -- so retail never takes the not-found
        // path and the guard was a scaffold. Retail decrements n_elems
        // unconditionally and shifts from the found index.
        {
            // .text:0634c932-0634c981, no null test on +0xf4 (retail has none).
            IVP_U_Vector<IVP_Environment>& Registry = env->environment_manager->Environments;
            unsigned short& regCount = Registry.n_elems;   // +0x6 of the manager
            IVP_Environment** regElems = Registry.elems;    // +0x8 of the manager

            const int32_t OldCount = static_cast<int32_t>(regCount);   // :0634c93d-:0634c943
            int32_t Idx = OldCount - 1;                                 // :0634c948 (JS skips the scan)
            while (Idx >= 0 && regElems[Idx] != env)                    // :0634c951-:0634c95b
            {
                --Idx;
            }

            regCount = static_cast<uint16_t>(OldCount - 1);              // :0634c95d-:0634c960, unconditional
            while (Idx < static_cast<int32_t>(regCount))                 // :0634c964-:0634c96c / :0634c978-:0634c981 (n_elems re-read)
            {
                regElems[Idx] = regElems[Idx + 1];                       // :0634c96e-:0634c975
                ++Idx;
            }
        }

        IVP_Environment_Free_Buffer_Node_List(env); // FUN_0634cd20, item 3048
    }

    // Regardless of full/partial construction, always release the 6
    // remaining IVP_U_Vector_Base fields if they hold heap-allocated
    // buffers (retail's own shared tail, reached both by fallthrough out
    // of the block above and directly when state_magic never matched).
    IVP_U_Vector_Base_Clear(env->vector_12c);
    IVP_U_Vector_Base_Clear(env->object_listeners);
    IVP_U_Vector_Base_Clear(env->vector_e0);
    IVP_U_Vector_Base_Clear(env->event_queue);
    IVP_U_Vector_Base_Clear(env->vector_listeners_d0);
    IVP_U_Vector_Base_Clear(env->vector_listeners_c8);
}

// ============================================================================
// FUN_0634d090: IVP_Environment::simulate_psi
// ============================================================================
// FUN_0634d020 (.text:0634d020-0634d04d, plain RET) -- the library's
// IVP_Environment::fire_event_PSI: a one-dword IVP_Event_PSI {env} on the
// stack; for every element of vector_listeners_d0, last to first (elems
// re-read each pass), slot 0 with its address. cmn-phy5 2026-10-08.
static void IVP_Environment_Fire_Event_PSI(IVP_Environment* env)
{
    IVP_Event_PSI Event;
    Event.environment = env;                                                       // :0634d02e
    for (int32_t I = static_cast<int32_t>(env->vector_listeners_d0.n_elems) - 1; I >= 0; --I) // :0634d025-:0634d033 / :0634d047-:0634d048
    {
        IVP_Listener_PSI* Listener = static_cast<IVP_Listener_PSI*>(env->vector_listeners_d0.elems[I]); // :0634d035-:0634d03b
        Listener->event_PSI(&Event);                                               // :0634d03e-:0634d045
    }
}

// FUN_0634d090 (.text:0634d090-0634d20b, RET 8) -- the library's
// IVP_Environment::simulate_psi. The IVP_Time argument is never read.
// The simulation-unit and calc-next-PSI stages are separate calls, identified
// below by their retail addresses. Port-only diagnostics measure these stage
// boundaries without changing their calculations or call order.
void IVP_Environment_Simulate_Psi(IVP_Environment* env, IVP_Time /*current_sim_time*/)
{
    FPhysicsTickScope PrepareProfile(EPhysicsTickSection::PsiPrepare);
    env->sim_substate = 0;                                                         // :0634d0b1

    if (env->event_queue.n_elems != 0)                                            // :0634d0b7-:0634d0be
    {
        IVP_Environment_Process_Wake_Queue(env);                                   // :0634d0c0 FUN_0634bbe0
    }

    if (env->universe_manager != nullptr)                                         // :0634d0c5-:0634d0ca
    {
        // :0634d0cc-:0634d0d0 FUN_0634b230 (ECX = +0x0c cluster manager, +0x2c pushed).
        env->cluster_manager->check_for_unused_objects(env->universe_manager);
    }

    // :0634d0d5-:0634d0de: l_active_value_manager (+0x98) slot 10 (+0x28).
    env->l_active_value_manager->RefreshPsiActiveValues(env);

    IVP_Environment_Fire_Event_PSI(env);                                           // :0634d0e1-:0634d0e3

    // :0634d0e8-:0634d0eb FUN_0634f230(env->mindist_manager), the library's
    // IVP_Mindist_Manager::recalc_all_exact_wheel_mindist over the +0x0c vector.
    env->mindist_manager->recalc_all_exact_wheel_mindist();

    // :0634d0f0-:0634d108 and :0634d128-:0634d137 -- two 128-slot inline
    // vectors on the stack (memsize 0x80, n_elems 0, elems = inline buffer).
    IVP_U_Vector_Fixed<IVP_Core, 128> ActiveCores;
    IVP_U_Vector_Fixed<IVP_Hull_Manager, 128> ActiveHullManagers;
    PrepareProfile.Stop();

    // :0634d110-:0634d123 FUN_063499c0(ECX = env->time_manager (+0x08), env,
    // &ActiveCores) -- every sim unit's PSI, filling the cores that need
    // calc_next_PSI_matrix.
    FPhysicsTickScope UnitsProfile(EPhysicsTickSection::SimUnits);
    env->sim_units_manager->simulate_sim_units_psi(env, &ActiveCores);
    UnitsProfile.Stop();

    // :0634d13c-:0634d152 FUN_06351eb0(env, &ActiveCores, &ActiveHullManagers).
    FPhysicsTickScope CommitProfile(EPhysicsTickSection::CommitHulls);
    IVP_Calc_Next_PSI_Solver::commit_all_calc_next_PSI_matrix(env, &ActiveCores, &ActiveHullManagers);

    env->sim_substate = 2;                                                         // :0634d15d
    // :0634d157-:0634d167 FUN_06351f80(env, &ActiveHullManagers).
    IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(env, &ActiveHullManagers);
    CommitProfile.Stop();

    env->sim_substate = 3;                                                         // :0634d172
    // :0634d16c/:0634d17c FUN_0634f300(env->mindist_manager), the library's
    // recalc_all_exact_mindists (FUN_06350460 per mindist of the exact list).
    FPhysicsTickScope ContactsProfile(EPhysicsTickSection::Contacts);
    env->mindist_manager->recalc_all_exact_mindists();

    env->sim_substate = 4;                                                         // :0634d184
    env->mindist_manager->recalc_all_exact_mindists_events();                      // :0634d181/:0634d18e FUN_0634f210
    ContactsProfile.Stop();

    env->sim_substate = 5;                                                         // :0634d19d
    // :0634d193-:0634d1f2: both vectors' destructors (free elems unless inline).
}

// The uncalled lookalike IVP_Environment_Simulate_DTime_Psi that stood here
// (clamp + time advance + Simulate_Psi in one function) was removed 2026-10-08
// (cmn-phy5): retail's chain is IVP_Environment_SimulateDTime (0634cc80) ->
// IVP_Sim_Unit_Manager::simulate_variable_time_step (06360a40) ->
// IVP_Event_Manager::simulate_variable_time_step (063607c0) ->
// IVP_Time_Event_PSI::simulate_time_event (06360ad0), all below.

// ============================================================================
// D-ivp-anomaly pass (2026-09-17, lane claude:common-ivp).
// IVP_Anomaly_Limits / IVP_Anomaly_Manager -- the two IVP_Environment
// sub-managers at +0x24 / +0x20. Declared (with the full evidence trail, every
// field and every vtable slot cited to Common.dll.txt) in
// ivp_anomaly_manager.h; implemented here rather than in a new translation
// unit because src\Common\Common.vcxproj enumerates its sources explicitly and
// this lane does not own that file.
//
// decomp-tracker items: 3416 (FUN_06360e30, IVP_Anomaly_Limits ctor) and 3420
// (FUN_06360eb0, IVP_Anomaly_Manager ctor).
// ============================================================================

// FUN_06360e30 (Common.dll.txt:454257-454264). Field values are the raw
// immediates retail stores, reinterpreted per field: 0x44fa0000 == 2000.0f,
// 0x00011170 == 70000 (integer), 0x3fc90fdb == 1.5707964f.
IVP_Anomaly_Limits::IVP_Anomaly_Limits(int32_t bDeleteWithEnvironment)
    : bDeleteOnEnvironmentDelete(bDeleteWithEnvironment)
    , MaxVelocity(2000.0f)
    , MaxCollisionsPerPsi(70000)
    , MaxAngularVelocityPerPsi(1.5707964f)
{
}

// vtable slot 0, LAB_06360e90 (Common.dll.txt:454309-454319). Retail's extra
// `TEST ECX,ECX` null-this guard is an artifact of the compiler inlining a
// "delete if owned" helper -- a virtual call can never arrive with a null
// this, so the C++ form below is exact.
void IVP_Anomaly_Limits::EnvironmentWillBeDeleted(IVP_Environment* /*Env*/)
{
    if (bDeleteOnEnvironmentDelete != 0)
    {
        delete this;
    }
}

// vtable slot 1, FUN_06360e60 -> FUN_06360e80 (Common.dll.txt:454279-454299):
// the destructor body itself only re-installs the vtable (i.e. it is trivial);
// the free() belongs to the MSVC scalar-deleting-destructor thunk, which
// `delete` above already supplies.
IVP_Anomaly_Limits::~IVP_Anomaly_Limits()
{
}

// FUN_06360eb0 (Common.dll.txt:454337-454341).
IVP_Anomaly_Manager::IVP_Anomaly_Manager(int32_t bDeleteWithEnvironment)
    : bDeleteOnEnvironmentDelete(bDeleteWithEnvironment)
{
}

// vtable slot 0, LAB_06360f20 (Common.dll.txt:454414-454444). Retail's FP
// order: target = Limits->MaxVelocity * 0.99f; len = sqrt(x*x+y*y+z*z);
// scale = target/len; then x, y, z are each multiplied by `scale` in that
// order. DAT_063a57a0 == 0x3F7D70A4 == 0.99f (raw .rdata bytes a4 70 7d 3f).
// x87 (06360f24-06360f66): target, |v| and the quotient never leave the FPU stack, so
// they are double here; only the three products are stored as floats.
void IVP_Anomaly_Manager::MaxVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* /*Core*/, Vector* Speed)
{
    const double Target = (double)Limits->MaxVelocity * (double)0.99f;                 // 06360f30-06360f33
    const double Length = std::sqrt(((double)Speed->x * Speed->x + (double)Speed->y * Speed->y) +
                                    (double)Speed->z * Speed->z);                       // 06360f39-06360f49
    const double Scale = Target / Length;                                              // 06360f4b-06360f4f

    Speed->x = (float)(Scale * Speed->x); // 06360f55-06360f59
    Speed->y = (float)(Scale * Speed->y); // 06360f5b-06360f60
    Speed->z = (float)(Scale * Speed->z); // 06360f63-06360f66
}

// vtable slot 1, LAB_06360f70 (Common.dll.txt:454449-454482). Same shape as
// slot 0; the bound is the per-PSI angular limit converted to a per-second one
// via the core's own Environment. DAT_063a53fc == 0x3F666666 == 0.9f (raw
// .rdata bytes 66 66 66 3f).
void IVP_Anomaly_Manager::MaxAngularVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* RotSpeed)
{
    // 06360f87-06360f90: FLD env->inv_delta_time; FMUL Limits+0x10; FMUL 0.9f, kept on the stack.
    const double Target = ((double)Core->Environment->inv_delta_time * Limits->MaxAngularVelocityPerPsi) * (double)0.9f;
    const double Length = std::sqrt(((double)RotSpeed->x * RotSpeed->x + (double)RotSpeed->y * RotSpeed->y) +
                                    (double)RotSpeed->z * RotSpeed->z);                 // 06360f96-06360fa6
    const double Scale = Target / Length;                                              // 06360fa8-06360fac

    RotSpeed->x = (float)(Scale * RotSpeed->x); // 06360fb2-06360fb6
    RotSpeed->y = (float)(Scale * RotSpeed->y); // 06360fb8-06360fbd
    RotSpeed->z = (float)(Scale * RotSpeed->z); // 06360fc0-06360fc3
}

// FUN_06360fd0 (.text:06360fd0-06361125, RET 8), cmn-physys4 2026-10-08.
//   06360fd8-06360fe8  Core0 = Object0->core, Core1 = Object1->core, Env = Object0->environment
//   06360fee-06361030  Dir = Core1->MWorldFCore.vv - Core0->MWorldFCore.vv, each component a
//                      float+float FSUB stored as float
//   06361034           Dir.normize() (06347ef0)
//   06361039-06361048  Speed = float(this->get_push_speed_penetration(Object0, Object1)) -- the
//                      virtual slot 5; FSTP float into the Object0 argument slot
//   0636104c-06361058  Core0's movement byte, signed, >= 8 (SHL 0x18; CMP 0x8000000; JGE) ->
//                      skip Object0
//   0636105a-0636108b  F = -(Core0->Mass * Speed) on the stack; Impulse = float(F * Dir[i]);
//                      Object0->async_push_object_ws(&Core0->MWorldFCore.vv, &Impulse)
//   06361090-063610af  Object0->async_add_rot_speed_object_cs(&{Env->delta_time, 0, 0})
//   063610b4-063610f5  the same for Object1 with F = +(Core1->Mass * Speed) -- and with
//                      Core0's position (ESI is still &Core0->MWorldFCore.vv at 063610ce)
//   063610fa-06361119  Object1->async_add_rot_speed_object_cs(&{Env->delta_time, 0, 0}),
//                      Env still Object0's environment
// No pointer is tested.
void IVP_Anomaly_Manager::solve_inter_penetration_simple(IVP_Real_Object* Object0, IVP_Real_Object* Object1)
{
    IVP_Environment* const Env = Object0->environment;
    IVP_Core* const Core0 = Object0->core;
    IVP_Core* const Core1 = Object1->core;
    const IVP_U_Point* const Center0 = &Core0->MWorldFCore.vv;
    const IVP_U_Point* const Center1 = &Core1->MWorldFCore.vv;
    const float DeltaTime = Env->delta_time;          // local_38

    IVP_U_Point Dir;                                   // local_30..local_28
    Dir.k[1] = Center1->k[1] - Center0->k[1];
    Dir.k[2] = Center1->k[2] - Center0->k[2];
    Dir.k[0] = Center1->k[0] - Center0->k[0];
    Dir.normize();

    const float Speed = (float)(get_push_speed_penetration(Object0, Object1));

    if (static_cast<int8_t>(Core0->MovementState & 0xffu) < 8)
    {
        const double Factor = -((double)(Core0->Mass) * Speed);
        IVP_U_Point Impulse;                           // local_20..local_18
        Impulse.k[0] = (float)(Factor * Dir.k[0]);
        Impulse.k[1] = (float)(Factor * Dir.k[1]);
        Impulse.k[2] = (float)(Factor * Dir.k[2]);
        Object0->async_push_object_ws(Center0, &Impulse);

        IVP_U_Point RotSpeed;                          // local_10..local_8
        RotSpeed.k[0] = DeltaTime;
        RotSpeed.k[1] = 0.0f;
        RotSpeed.k[2] = 0.0f;
        Object0->async_add_rot_speed_object_cs(&RotSpeed);
    }

    if (static_cast<int8_t>(Core1->MovementState & 0xffu) < 8)
    {
        const double Factor = (double)(Core1->Mass) * Speed;
        IVP_U_Point Impulse;                           // local_10..local_8
        Impulse.k[0] = (float)(Factor * Dir.k[0]);
        Impulse.k[1] = (float)(Factor * Dir.k[1]);
        Impulse.k[2] = (float)(Factor * Dir.k[2]);
        Object1->async_push_object_ws(Center0, &Impulse);

        IVP_U_Point RotSpeed;                          // local_20..local_18
        RotSpeed.k[0] = DeltaTime;
        RotSpeed.k[1] = 0.0f;
        RotSpeed.k[2] = 0.0f;
        Object1->async_add_rot_speed_object_cs(&RotSpeed);
    }
}

// vtable slot 2, LAB_06361130 (.text:06361130-0636145d, RET 0xc): inter_penetration.
// Its caller is IVP_Ledge_Mindist::mindist_rescue_push (06352b10, `CALL [vtbl+0x8]`).
// cmn-physys4 2026-10-08. Objects[] is the stack pair local+0x14 / +0x18.
//   0636114a-0636116a  Idx = (Object1->core flags byte & 0xc) ? 1 : 0
//   06361171-0636117e  Fixed = Objects[Idx]; its core flags byte & 0xc == 0 (neither side is
//                      fixed) -> solve_inter_penetration_simple(Object0, Object1), return
//   06361196-063611af  Mindist->synapse[Idx].status (+0x18 + Idx*0x1c + 0x1a, the physical
//                      slot) > 2 (signed, JLE continues) -> the same, return
//   063611c7-063611fd  Free = Objects[1 - Idx]; PositionWs = Free->core->MWorldFCore.vv
//   06361204-06361244  Slot = Fixed's cache object, inlined without a refcount (0634d530 /
//                      0634d640) -- get_cache_object_no_lock
//   06361247-06361259  PositionOs = Slot->WorldTransform.vimult4(PositionWs) (06348470)
//   0636125e-06361261  Ledge = synapse[Idx].get_ledge() (0634ded0)
//   0636126a-06361286  BestNormal = (1, 0, 0); Best = 0xe0ad78ec (-1e20f)
//   06361297-063613b0  for every triangle (count MOVSX word Ledge+0xc): P0 / P1 / P2 = the
//                      start points of edges 0 / 1 / 2; Plane = set_plane_from_3_points(P0,
//                      P2, P1) (063480a0, pushes &P1, &P2, &P0); normize_plane (06348150);
//                      D = ((n.z*p.z + n.y*p.y) + p.x*n.x) + hesse on the stack; FCOM Best;
//                      TEST AH,0x41; JNZ keeps the old best (less, equal or unordered); the
//                      fall-through (D > Best) stores Best = float(D) and BestNormal = n
//   063613ba-063613c6  NormalWs = Slot->WorldTransform.vmult(BestNormal) (06348580)
//   063613cb-063613f7  Dir = NormalWs (float copies); Speed = this->get_push_speed_penetration
//                      (Object0, Object1), kept on the stack
//   063613fa-06361410  Free's core movement byte, signed, >= 8 -> return
//   06361412-0636143f  Dir[i] = float(Dir[i] * Speed); Free->async_add_speed_object_ws(&Dir)
// No pointer is tested.
void IVP_Anomaly_Manager::inter_penetration(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object0, IVP_Real_Object* Object1)
{
    IVP_Real_Object* const Objects[2] = { Object0, Object1 };
    const int32_t Idx = ((Object1->core->Flags & 0xcu) != 0) ? 1 : 0;
    IVP_Real_Object* const Fixed = Objects[Idx];
    if ((Fixed->core->Flags & 0xcu) == 0)
    {
        solve_inter_penetration_simple(Object0, Object1);
        return;
    }

    const IVP_Synapse_Real* const Synapse = &Mindist->synapse[Idx];
    if (Synapse->status > 2)
    {
        solve_inter_penetration_simple(Object0, Object1);
        return;
    }

    IVP_Real_Object* const Free = Objects[1 - Idx];
    IVP_U_Point PositionWs;                            // local+0x7c
    PositionWs.k[0] = Free->core->MWorldFCore.vv.k[0];
    PositionWs.k[1] = Free->core->MWorldFCore.vv.k[1];
    PositionWs.k[2] = Free->core->MWorldFCore.vv.k[2];

    IVP_NarrowPhaseCacheSlot* const Slot = Fixed->get_cache_object_no_lock();
    const IVP_U_Matrix* const Transform = &Slot->WorldTransform;
    IVP_U_Point PositionOs;                            // local+0x5c
    Transform->vimult4(&PositionWs, &PositionOs);

    const IVP_Compact_Ledge* const Ledge = Synapse->get_ledge();

    IVP_U_Point BestNormal;                            // local+0x4c
    BestNormal.k[0] = 1.0f;
    BestNormal.k[1] = 0.0f;
    BestNormal.k[2] = 0.0f;
    float Best = -1e20f;                               // local+0x10, 0xe0ad78ec

    const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
    for (int32_t i = 0; i < static_cast<int32_t>(Ledge->GetNTriangles()); ++i, ++Triangle)
    {
        IVP_U_Point P0;                                // local+0x8c
        IVP_U_Point P1;                                // local+0x9c
        IVP_U_Point P2;                                // local+0xac
        P0.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(0))));
        P1.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(1))));
        P2.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(2))));

        IVP_U_Point Plane;                             // local+0x2c (hesse +0x38)
        Plane.set_plane_from_3_points(&P0, &P2, &P1);
        Plane.normize_plane();

        const double Distance = (((double)(Plane.k[2]) * PositionOs.k[2] +
                                  (double)(Plane.k[1]) * PositionOs.k[1]) +
                                 (double)(PositionOs.k[0]) * Plane.k[0]) +
                                Plane.k[3];
        if (Distance > Best)
        {
            Best = (float)(Distance);
            BestNormal.k[0] = Plane.k[0];
            BestNormal.k[1] = Plane.k[1];
            BestNormal.k[2] = Plane.k[2];
        }
    }

    IVP_U_Point NormalWs;                              // local+0x6c
    Transform->vmult(&BestNormal, &NormalWs);

    IVP_U_Point Dir;                                   // local+0x1c..+0x24
    Dir.k[0] = NormalWs.k[0];
    Dir.k[1] = NormalWs.k[1];
    Dir.k[2] = NormalWs.k[2];
    const double Speed = get_push_speed_penetration(Object0, Object1);

    if (!(static_cast<int8_t>(Free->core->MovementState & 0xffu) < 8))
    {
        return;
    }

    const float Y = (float)((double)(Dir.k[1]) * Speed);
    const float Z = (float)((double)(Dir.k[2]) * Speed);
    const float X = (float)((double)(Dir.k[0]) * Speed);
    Dir.k[2] = Z;
    Dir.k[0] = X;
    Dir.k[1] = Y;
    Free->async_add_speed_object_ws(&Dir);
}

// vtable slot 3, LAB_06361490 (Common.dll.txt:454874-454876): `MOV EAX,1; RET 8`.
int32_t IVP_Anomaly_Manager::MaxCollisionsExceededCheckFreezing(IVP_Anomaly_Limits* /*Limits*/, IVP_Core* /*Core*/)
{
    return 1;
}

// vtable slot 4, LAB_06360f00 (Common.dll.txt:454392-454402).
void IVP_Anomaly_Manager::EnvironmentWillBeDeleted(IVP_Environment* /*Env*/)
{
    if (bDeleteOnEnvironmentDelete != 0)
    {
        delete this;
    }
}

// vtable slot 5, LAB_06361460 (.text:06361460-06361482, RET 8), cmn-phy5
// 2026-10-08: env = Object0->environment; dt = env->delta_time (+0xac);
// FUN_06347e60 on env+0xb4 (sqrt(x*x + y*y + z*z), summed in that order);
// FADD ST0,ST0; FMUL dt. Computed in double; retail keeps the x87 80-bit
// intermediate (named gap, value-identical for the 3-float gravity vector).
double IVP_Anomaly_Manager::get_push_speed_penetration(IVP_Real_Object* Object0, IVP_Real_Object* /*Object1*/)
{
    IVP_Environment* Env = Object0->environment;                 // :06361464
    const float DeltaTime = Env->delta_time;                     // :06361467
    const double X = Env->field_b4;
    const double Y = Env->field_b8;
    const double Z = Env->field_bc;
    const double Length = std::sqrt((X * X + Y * Y) + Z * Z);     // :06361477 FUN_06347e60
    return (Length + Length) * DeltaTime;                        // :0636147c-:0636147e, left in ST0
}

// vtable slot 6, FUN_06360ed0 -> FUN_06360ef0 (Common.dll.txt:454362-454382).
IVP_Anomaly_Manager::~IVP_Anomaly_Manager()
{
}

// ############################################################################
// D-ivpenv-ctor pass (2026-09-17, lane claude:common-ivpenv).
//
// IVP_Environment's real constructor, FUN_0634bf50 (decomp-tracker item 3031),
// and the callees it needed that had no port anywhere in src/. Everything
// below is derived from Common.dll.txt only; every address cited is a line in
// that listing.
//
// ON THE "14 MISSING FUNCTIONS" DEPENDENCY LIST that item 3031's previous note
// left behind: it was measured against the symbol name FUN_xxxxxxxx rather
// than against the ported code, and eight of the fourteen turned out to be
// already ported under semantic names --
//   FUN_063441a0  the growable-vector realloc -- already ported as
//                 IVP_U_Vector<T>::Realloc (ivp_utility/ivp_u_vector.h). That
//                 template has the SAME layout as IVP_U_Vector_Base, so this
//                 file's IVP_U_Vector_Base_Realloc is a second entry point to
//                 one algorithm, not a second algorithm -- see the header for
//                 the TODO(OPEN) on unifying them.
//   FUN_06356920  plain _strdup
//   FUN_063486c0  IVP_RealObjectConstructionTemplate's constructor (ivp_real_object.cpp)
//   FUN_06348690  its long-name setter -- set_long_name
//   FUN_06348670  its destructor
//   thunk_FUN_06348670  a bare JMP to the above (0x06348710)
//   FUN_063481b0  IVP_U_Matrix identity init (ivp_math.h's IVP_U_Matrix::init,
//                 modulo the k[3] pads -- see the constructor body)
//   FUN_06344010  IVP_U_Memory::init_mem, now in ivp_utility/ivp_u_memory.h
// Six were genuinely absent and are ported here or in ivp_u_memory.h.
// ############################################################################

// ----------------------------------------------------------------------------
// Callees that used to live in the (since dissolved) src/Common/common_stubs.cpp, declared
// locally because src/include/common_stubs.h was another lane's file. Only the note
// below remains of that list.
// ----------------------------------------------------------------------------
// (FUN_06349380 is IVP_Sim_Units_Manager's constructor, ivp_simulation_unit.cpp.)

// ============================================================================
// FUN_063441a0 (item 2759) -- IVP_U_Vector_Base realloc. Same retail function
// as IVP_U_Vector<T>::Realloc; see the header for the instruction-by-
// instruction derivation and the TODO(OPEN) on unifying the two.
// ============================================================================
void IVP_U_Vector_Base_Realloc(IVP_U_Vector_Base* v)
{
    // 063441a9-063441b1: malloc(mem_size * 8 + 8). Note the *8, not *4: the
    // new capacity is itself 2*mem_size+1 pointers, and the +8 covers the +1.
    void** NewBuffer = static_cast<void**>(malloc(static_cast<size_t>(v->mem_size) * 8 + 8));

    // 063441b8-063441ca: capacity doubles, plus one. Retail computes this in
    // 16 bits (SHL DX,1; INC EDX with DX the live half), so it wraps at 0xFFFF
    // exactly as retail does -- deliberately not widened.
    v->mem_size = static_cast<uint16_t>(static_cast<uint16_t>(v->mem_size << 1) + 1);

    for (int i = 0; i < static_cast<int>(v->n_elems); ++i) // 063441cf-063441e1
    {
        NewBuffer[i] = v->elems[i];
    }

    // 063441e3-063441fa: free the old buffer unless it is the 8-byte inline
    // one that sits immediately after this header (the LEA EDX,[ESI+8]
    // sentinel -- the same test IVP_Environment_Process_Wake_Queue makes on
    // event_queue).
    void** InlineBuffer = reinterpret_cast<void**>(reinterpret_cast<char*>(v) + 8);
    if (v->elems != InlineBuffer && v->elems != nullptr)
    {
        free(v->elems);
        v->elems = nullptr; // 063441fa, redundant against the store below but real
    }

    v->elems = NewBuffer; // 06344201
}

void IVP_U_Vector_Base_Append(IVP_U_Vector_Base* v, void* elem)
{
    // Retail's guard is CMP n_elems, mem_size; JC skip -- an unsigned "below",
    // so the grow happens on n_elems >= mem_size (Common.dll.txt:0634c1a2).
    if (v->n_elems >= v->mem_size)
    {
        IVP_U_Vector_Base_Realloc(v);
    }
    v->elems[v->n_elems] = elem;
    ++v->n_elems;
}

// ============================================================================
// FUN_063505f0 (item 3141, Common.dll.txt:427376-427384).
// ============================================================================
void* IVP_Environment_Init_Block34(void* block34)
{
    std::memset(block34, 0, 0x54); // MOV ECX,0x15; REP STOSD
    return block34;
}

// ============================================================================
// FUN_0634bf40 (item 3030) / FUN_0634bf30 (item 3029).
//
// Both had been parked at blocked/simplified with the reason "cannot determine
// the owning C++ type without first porting that large constructor". That
// constructor is now ported, and it answers the question directly: the only
// call sites are LEA ECX,[ESI+0x88] (Common.dll.txt:0634bf7f and 0634c016), so
// the owning type is IVP_Environment and the field at +0x88 is
// default_friction. 0x3e99999a is 0.3f.
// ============================================================================
void IVP_Environment_Init_DefaultFriction(void* block88)
{
    *static_cast<float*>(block88) = 0.3f; // MOV dword ptr [ECX],0x3e99999a
}

void* IVP_Environment_Init_Block88(void* block88)
{
    IVP_Environment_Init_DefaultFriction(block88);
    return block88;
}

// ============================================================================
// FUN_0634c6d0 (item 3033) -- IVP_Environment::set_delta_PSI_time.
// ============================================================================
const float IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME = 1.0f / 66.0f; // 0x3c783e10

void IVP_Environment_SetDeltaPsiTime(IVP_Environment* env, float dt)
{
    env->delta_time = dt;            // MOV dword ptr [ECX+0xac],EAX -- the raw dword
    env->inv_delta_time = 1.0f / dt; // FLD [DAT_063a53d0]=1.0f; FDIV dt; FSTP [ECX+0xb0]
}

// ============================================================================
// FUN_06360090 (item 3384, Common.dll.txt:452636-452662).
//
// One of the two once-only global dispatch-table initializers the environment
// constructor calls back to back. It REP STOSDs a default handler across 16
// slots, then overwrites the slots that have a real handler -- running one
// dword past the cleared range, so the true array is 17 dwords wide.
// (Its sibling, FUN_06353940, is already ported elsewhere; see below.)
//
// STATUS (updated 2026-09-17, lane claude:w24-sweep-rest): THE HANDLERS HAVE
// LANDED. All four -- LAB_0635fef0, LAB_0635fd10, FUN_0635fe30 and the
// LAB_06360080 trap -- are ported in src/IVP/ivp_collision/
// ivp_ledge_sweep_solver.cpp (decomp-tracker items 3382/3380/3381/3383), and
// the real table lives beside them because that is where its entries are.
// This function now forwards to it rather than filling a second array of
// nullptr, so there is one table and one set of slot assignments to keep
// right.
//
// The local 17-slot array below is GONE for the same reason: two arrays for
// one retail global is exactly the duplication that made the estimator look
// unported for a week.
// ============================================================================

// Declared locally: ivp_ledge_sweep_solver.h pulls in the whole
// evaluator/driver family and this file needs exactly one symbol from it.
void IVP_InitSweepEstimatorTable();

void IVP_InitSimUnitDispatchTable(void)
{
    // The whole of FUN_06360090 -- the REP STOSD of the LAB_06360080 trap
    // across sixteen slots and all eleven subsequent stores, in retail's own
    // order -- now lives in ivp_ledge_sweep_solver.cpp next to the four
    // handlers those stores name.
    IVP_InitSweepEstimatorTable();
}

// ============================================================================
// IVP_Sim_Unit_Manager -- the environment's time-ordered event queue. See the
// class's own comment in ivp_environment.h for the field-by-field trace.
// ============================================================================

// The enqueue as IVP_RescheduleCollisionEvent inlines it,
// Common.dll.txt:0634f964-0634f987:
//     MOV EBP,[EBP+0x4]            ; EBP = env->sim_unit_manager
//     FLD  [ESP+0x4c]              ; EventHi
//     FSUB [EBP+0x14]              ; -= TimeBaseHi
//     MOV  ECX,[EBP+0x8]           ; ECX = EventPool  (the thiscall receiver)
//     FADD [ESP+0x50]              ; += EventLo
//     FSUB [EBP+0x18]              ; -= TimeBaseLo
//     FSTP [ESP+0x58] / MOV EDX,[ESP+0x58]
//     PUSH EDX / PUSH ESI / CALL FUN_06362f90
// The LAST push before the CALL is ESI (the payload), so Insert's first
// argument is the payload and its second is the key -- the same order
// IVP_Priority_Pool::Insert declares. Note `MOV ECX,[EBP+0x8]` loads the
// POINTER at +0x08: the pool is heap-allocated and pointed to, not embedded.
uint16_t IVP_Sim_Unit_Manager::EnqueueEvent(void* Payload, float EventHi, float EventLo)
{
    // 2026-10-08 (cmn-phy5): the "EventPool == nullptr" guard that stood here
    // is gone -- the constructor always builds the pool (throwing new), and
    // retail dereferences unconditionally.
    return EventPool->Insert(Payload, MakeEventKey(EventHi, EventLo));
}

// FUN_063609f0 (decomp-tracker item 3403, Common.dll.txt:063609f0-06360a00).
void IVP_Sim_Unit_Manager::CancelEvent(int32_t Handle)
{
    EventPool->Remove(static_cast<uint16_t>(Handle));
}

// FUN_063609c0 (decomp-tracker item 3402, Common.dll.txt:063609c0-063609e9).
void IVP_Sim_Unit_Manager::ScheduleListener(IVP_Time_Event* Listener, float EventHi, float EventLo)
{
    // :063609e5 `MOV dword ptr [ESI+0x4],EAX` -- the slot goes back into the
    // listener's own handle field.
    Listener->EventQueueHandle = static_cast<int32_t>(EnqueueEvent(Listener, EventHi, EventLo));
}

// FUN_06360a10 (decomp-tracker item 3404, Common.dll.txt:06360a10-06360a3a).
// Retail, instruction for instruction:
//     ECX = this->+0x08                      ; the pool
//     if (word[ECX+0x10] <= 0) return 0;     ; JBE, i.e. Count == 0
//     EDX = [ECX+0x04];  AX = word[ECX+0x0e] ; Entries, LiveIndex
//     EAX <<= 4;  ESI = [EAX + EDX + 0x0c]   ; Entries[LiveIndex].Payload
//     EAX = [ESI+0x04];  PUSH EAX; CALL FUN_06363280  ; pool->Remove(handle)
//     return ESI
// Note the removal goes through the PAYLOAD's own handle field, not through
// LiveIndex -- the two are the same slot here, but retail spells it this way.
void* IVP_Sim_Unit_Manager::PopMinEventPayload()
{
    if (EventPool->PoolCount == 0) // `CMP word[ECX+0x10],0; JBE`
        return nullptr;

    IVP_Time_Event* Payload =
        static_cast<IVP_Time_Event*>(EventPool->PoolEntries[EventPool->PoolLiveIndex].Payload);
    EventPool->Remove(static_cast<uint16_t>(Payload->EventQueueHandle));
    return Payload;
}

// FUN_06360960 (decomp-tracker item 3401, Common.dll.txt:06360960-063609bb).
// Retail:
//     while (this->+0x08->Count > 0) free(FUN_06360a10(this));
//     free(this->+0x04);  this->+0x04 = 0;
//     EDI = this->+0x08;
//     if (EDI) { FUN_06362f70(EDI); free(EDI); }
//     this->+0x08 = 0;
// The +0x0C listener is NOT freed separately: it lives in the pool, so the
// drain loop above has already freed it. Reproduced exactly, including that
// asymmetry.
void IVP_Sim_Unit_Manager::DestroyContents()
{
    while (EventPool->PoolCount > 0)
    {
        // FUN_06391d5a is the bare operator delete: no destructor runs on the
        // queued events (PSI event, mindists still queued), only the memory goes.
        ::operator delete(PopMinEventPayload());
    }

    delete EventManager;     // FUN_06391d5a, :0636098b (trivial dtor, so this is the bare operator delete)
    EventManager = nullptr;  // :06360999
    // PsiEvent (+0x0c) keeps its stale pointer, as in retail: it lived in the
    // pool and the drain loop above already freed it.

    // :063609a2-:063609b3 -- FUN_06362f70(pool) then free(pool). That pair is
    // exactly what `delete` is, and the pool is allocated with `new` in
    // IVP_SimUnitManager_Construct, so it is spelled `delete` here rather than
    // as an operator_new/free mismatch.
    delete EventPool;
    EventPool = nullptr; // :063609b3
}

// ============================================================================
// FUN_063608a0 (item 3400, Common.dll.txt:453612-453659) -- the constructor
// for IVP_Environment::sim_unit_manager (+0x04), a 0x1C-byte object owning
// three heap sub-objects.
//
// Retail:
//   memset(this, 0, 0x1c);                                    // 063608ba-063608c5
//   this->+0x04 = new(8);    its[0] = &PTR_LAB_063a558c;      // 063608c3-063608df
//                            its[4] = 0;                      // 063608e4
//   this->+0x08 = new(0x14); FUN_06362ef0(that, 0x10);        // 063608e2-06360912
//   this->+0x0c = new(8);    its[0] = &PTR_LAB_063a574c;      // 06360908-0636092b
//   { float rec[2] = {0,0}; FUN_063609c0(this, this->+0x0c, rec); } // 0636092e-06360942
//   return this;
//
// UPDATED 2026-09-17 (lane claude:w26-simunit): the pool and the registration
// call are now real. What the pool sub-object is, and why it is the extracted
// IVP_Priority_Pool rather than a fresh type, is argued on that class itself.
// The two dwords the registration call passes by value are read straight off
// :06360933/:06360935 (`MOV [EAX],EDI` / `MOV [EAX+4],EDI` with EDI == 0), and
// with TimeBaseHi/TimeBaseLo still zero at construction time the resulting key
// is exactly 0.0f.
//
// 2026-10-08 (cmn-phy5): both sub-objects are real classes now --
// IVP_Event_Manager (vtable 063a558c) and IVP_Time_Event_PSI (vtable 063a574c,
// LAB_06360ad0 = the PSI advance). They used to be allocated with null vtables,
// so the PSI advance never ran.
// ============================================================================
void* IVP_SimUnitManager_Construct(void* self)
{
    IVP_Sim_Unit_Manager* Self = static_cast<IVP_Sim_Unit_Manager*>(self);
    std::memset(Self, 0, sizeof(IVP_Sim_Unit_Manager)); // :063608ba-:063608c5, 7 dwords

    // :063608c3-:063608e4 -- new(8), vptr 063a558c, +4 = 0.
    Self->EventManager = new IVP_Event_Manager();

    // :063608e2-:06360912 -- `operator new(0x14)` then FUN_06362ef0(p, 0x10).
    Self->EventPool = new IVP_Priority_Pool(static_cast<uint16_t>(0x10));

    // :06360908-:0636092b -- new(8), vptr 063a574c. Retail does NOT initialize
    // its handle here; the ScheduleListener call below writes it (:063609e5).
    Self->PsiEvent = new IVP_Time_Event_PSI();

    // :0636092e-:06360942 -- FUN_063609c0(this, this->+0x0c, {0.0f, 0.0f}).
    Self->ScheduleListener(Self->PsiEvent, 0.0f, 0.0f);
    return Self;
}

// FUN_06360ab0 (.text:06360ab0-06360ac3, RET 0xc): `MOV ECX,[ESP+4]` (the
// environment) and CALL FUN_0634ccd0 with the two time halves.
void IVP_Sim_Unit_Manager::env_set_current_time(IVP_Environment* Environment, float TimeHi, float TimeLo)
{
    IVP_Environment_SetCurrentTime(Environment, TimeHi, TimeLo);
}

// FUN_06360a40 (.text:06360a40-06360aa7, RET 8). Retail brackets the call with
// FSTCW / OR 0x300 / FLDCW (x87 precision control = 64-bit mantissa) and
// restores the control word afterwards. Named gap: the port's arithmetic is
// SSE2 with x87 temporaries modelled as double, so the step's 64-bit-significand
// temporaries are not reproduced (MSVC long double == double). Trigonometry
// uses standard C++ math; its rounding can differ from retail instructions.
// Clamp: `FCOMP [063a5654]; TEST AH,0x1; JZ` -- C0 set (below 0.005f, or
// unordered: a NaN is clamped too) -> 0x3ba3d70a (0.005f); else
// `FCOMP [063a5464]; TEST AH,0x41; JNZ` -- not greater than 0.1f keeps it,
// greater -> 0x3dcccccd (0.1f).
void IVP_Sim_Unit_Manager::simulate_variable_time_step(IVP_Environment* Environment, float DeltaTime)
{
    if (!(DeltaTime >= 0.005f))          // :06360a58-:06360a66 (C0: less or unordered)
        DeltaTime = 0.005f;              // :06360a68
    else if (DeltaTime > 0.1f)           // :06360a71-:06360a7f
        DeltaTime = 0.1f;                // :06360a81

    // :06360a88-:06360a9d -- EventManager->slot1(this, env, PsiEvent, DeltaTime).
    EventManager->simulate_variable_time_step(this, Environment, PsiEvent, DeltaTime);
}

// ----------------------------------------------------------------------------
// IVP_Event_Manager slot 0 -- 063606d0 (.text:063606d0-063607b0, RET 0x10).
// x87: the loop test is `FLD key; FLD until_rel; FCOMPP; TEST AH,0x41; JNZ` --
// ST0 = until_rel is compared against ST1 = the pool's minimum key; C0|C3 set
// (until_rel <= key, or unordered) jumps out, so events run while
// until_rel > key. The time normalization below runs while lo > 1.0f
// (`FCOM 1.0; TEST AH,0x41; JNZ` skips when not greater).
// ----------------------------------------------------------------------------
void IVP_Event_Manager::simulate_time_events(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                             float UntilHi, float UntilLo)
{
    for (;;)
    {
        IVP_Priority_Pool* Pool = TimeManager->EventPool;                       // :063606de
        const float MinKey = Pool->PoolKeyThreshold;                            // :063606e1
        // x87: kept on the FPU stack (double) until FCOMPP.
        const double UntilRel = (((double)(UntilHi) - TimeManager->TimeBaseHi) + UntilLo) - TimeManager->TimeBaseLo; // :063606ec-:063606f7
        if (!(UntilRel > MinKey))                                               // :063606fa-:06360701
            break;

        IVP_Time_Event* Event =
            static_cast<IVP_Time_Event*>(Pool->PoolEntries[Pool->PoolLiveIndex].Payload); // :06360707-:06360713
        Pool->Remove(static_cast<uint16_t>(Event->EventQueueHandle));           // :06360717-:0636071b
        Event->EventQueueHandle = 0xffff;                                       // :06360724
        TimeManager->Field_0x10 = MinKey;                                       // :0636072b

        // x87: Hi/Lo stay on the FPU stack (double) through the loop and are
        // rounded to float only by the FSTPs at :0636076a/:0636076e.
        double Hi = TimeManager->TimeBaseHi;                                    // :0636072e
        double Lo = (double)(MinKey) + TimeManager->TimeBaseLo;      // :06360735-:06360739
        while (Lo > 1.0f)                                                       // :0636073c-:06360768
        {
            Hi = Hi + 1.0f;
            Lo = Lo - 1.0f;
        }
        TimeManager->env_set_current_time(Environment, (float)(Hi), (float)(Lo)); // :06360772-:0636077f
        Event->simulate_time_event(Environment);                                // :06360784-:06360789 (slot 0)

        if (TimeManager->EventManager->Mode == 1)                               // :0636078b-:06360792
            break;
    }
    TimeManager->env_set_current_time(Environment, UntilHi, UntilLo);           // :06360798-:063607a5
}

// ----------------------------------------------------------------------------
// IVP_Event_Manager slot 1 -- 063607c0 (.text:063607c0-0636089c, RET 0x10).
// Ghidra left this body as raw bytes; decoded by hand from the listing:
//   counter = 0
//   loop: key = pool->+8; Event = entries[pool->+0xe].payload;
//         Hi = TimeBaseHi; Lo = key + TimeBaseLo; while (Lo > 1.0f) { Hi += 1; Lo -= 1; }
//         env_set_current_time(env, Hi, Lo)                  (:06360839-:06360846)
//         if (Event == PsiEvent) { if (++counter == 2) return;   (:0636084b-:0636085b)
//                                  IVP_Environment_SetDeltaPsiTime(env, dt); } (:0636085d-:06360864)
//         TimeManager->+0x10 = key; pool->Remove(Event->+4);  (:06360869-:06360877)
//         Event->+4 = 0xffff; Event->simulate_time_event(env)   (:0636087c-:06360888)
// There is no until-time test and no Mode test in this loop.
// ----------------------------------------------------------------------------
void IVP_Event_Manager::simulate_variable_time_step(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                                    IVP_Time_Event_PSI* PsiEvent, float DeltaTime)
{
    int32_t PsiCount = 0;                                                       // :063607d3
    for (;;)
    {
        IVP_Priority_Pool* Pool = TimeManager->EventPool;                       // :063607db
        const float MinKey = Pool->PoolKeyThreshold;                            // :063607e1
        IVP_Time_Event* Event =
            static_cast<IVP_Time_Event*>(Pool->PoolEntries[Pool->PoolLiveIndex].Payload); // :063607e8-:06360805

        // x87: on the FPU stack (double) until the FSTPs at :06360831/:06360835.
        double Hi = TimeManager->TimeBaseHi;                                    // :063607de
        double Lo = (double)(MinKey) + TimeManager->TimeBaseLo;      // :063607f5-:063607f9
        while (Lo > 1.0f)                                                       // :063607fc-:0636082f
        {
            Hi = Hi + 1.0f;
            Lo = Lo - 1.0f;
        }
        TimeManager->env_set_current_time(Environment, (float)(Hi), (float)(Lo)); // :06360839-:06360846

        if (Event == PsiEvent)                                                  // :0636084b
        {
            ++PsiCount;                                                         // :0636084f-:06360857
            if (PsiCount == 2)                                                  // :0636085b
                return;
            IVP_Environment_SetDeltaPsiTime(Environment, DeltaTime);            // :0636085d-:06360864 FUN_0634c6d0
        }

        TimeManager->Field_0x10 = MinKey;                                       // :06360869-:0636086d
        Pool->Remove(static_cast<uint16_t>(Event->EventQueueHandle));           // :06360870-:06360877
        Event->EventQueueHandle = 0xffff;                                       // :06360881
        Event->simulate_time_event(Environment);                                // :0636087c-:06360888
    }
}

// ----------------------------------------------------------------------------
// IVP_Time_Event_PSI::simulate_time_event -- LAB_06360ad0
// (.text:06360ad0-06360bee, RET 4). The PSI advance.
// ----------------------------------------------------------------------------
void IVP_Time_Event_PSI::simulate_time_event(IVP_Environment* Environment)
{
    IVP_Sim_Unit_Manager* TimeManager = Environment->sim_unit_manager;           // :06360adf

    // :06360ad9-:06360af0 -- the PSI that starts now: +0x108/+0x10c = current_time.
    Environment->time_of_last_psi.time_base = Environment->current_time.time_base;     // :06360ae2
    Environment->time_of_last_psi.time_offset = Environment->current_time.time_offset; // :06360af0

    // :06360af6-:06360b51 -- the following PSI: {+0x108, delta_time + +0x10c},
    // normalized while lo > 1.0f (`FCOM 1.0; TEST AH,0x41; JNZ` skips the loop
    // when not greater, the loop's `JZ` repeats while greater), stored at
    // +0x100/+0x104.
    // x87: on the FPU stack (double) until the FSTPs at :06360b3b/:06360b3f.
    double Hi = Environment->time_of_last_psi.time_base;
    double Lo = (double)Environment->delta_time + Environment->time_of_last_psi.time_offset;
    while (Lo > 1.0f)
    {
        Hi = Hi + 1.0f;
        Lo = Lo - 1.0f;
    }
    Environment->next_psi_time_base = (float)(Hi);           // :06360b4b
    Environment->next_psi_time_offset = (float)(Lo);         // :06360b51

    // :06360b57-:06360b9e -- rebase every queued key and the pool's minimum by
    // (+0x10c + +0x108), walking the ascending list from PoolLiveIndex via Next.
    // Retail also tests the entry pointer for null (:06360b7e/:06360b84), but
    // only after it already read Entry->Next through it (:06360b80), so that
    // test can never be reached with a null pointer; not reproduced.
    // x87: Shift stays in ST1 (double) for every subtraction; each FSTP rounds to float.
    const double Shift = (double)Environment->time_of_last_psi.time_offset + Environment->time_of_last_psi.time_base;
    IVP_Priority_Pool* Pool = TimeManager->EventPool;
    uint32_t Index = Pool->PoolLiveIndex;
    while (Index != 0xffff)                                                     // :06360b6c
    {
        IVP_Priority_Pool::Entry* Entry = &Pool->PoolEntries[Index];            // :06360b74-:06360b7a
        Index = Entry->Next;                                                    // :06360b80
        Entry->Key = (float)(Entry->Key - Shift);                    // :06360b86-:06360b8b
    }
    Pool->PoolKeyThreshold = (float)(Pool->PoolKeyThreshold - Shift); // :06360b96-:06360b9e

    // :06360ba1-:06360bb5 -- the time manager's base moves to this PSI.
    TimeManager->TimeBaseHi = Environment->time_of_last_psi.time_base;
    TimeManager->TimeBaseLo = Environment->time_of_last_psi.time_offset;
    TimeManager->Field_0x10 = 0.0f;

    // :06360bbc-:06360bcc -- FUN_0634d090(env, {+0x108, +0x10c}).
    IVP_Environment_Simulate_Psi(Environment, Environment->time_of_last_psi);

    // :06360bd1-:06360be3 -- requeue this event at the following PSI.
    TimeManager->ScheduleListener(this, Environment->next_psi_time_base, Environment->next_psi_time_offset);
}

// FUN_0634ccd0 (.text:0634ccd0-0634ccf1, RET 8).
void IVP_Environment_SetCurrentTime(IVP_Environment* env, float TimeHi, float TimeLo)
{
    ++env->sim_step_count;                 // :0634ccd0-:0634ccdb (+0x110)
    env->current_time.time_base = TimeHi;  // :0634cce5 (+0xf8)
    env->current_time.time_offset = TimeLo; // :0634cceb (+0xfc)
}


// FUN_0634cc80 (.text:0634cc80-0634ccc7, RET 4).
void IVP_Environment_SimulateDTime(IVP_Environment* env, float DeltaTime)
{
    FPhysicsTickScope SimulateProfile(EPhysicsTickSection::Simulate);
    // :0634cc86-:0634ccad -- FLD offset(+0xfc); FADD base(+0xf8); FADD DeltaTime; FSTP float.
    // x87: on the FPU stack (double) until the FSTP float at :0634ccad.
    const float Target = (float)(
        ((double)(env->current_time.time_offset) + env->current_time.time_base) + DeltaTime);
    env->interval_gate->set_time(Target);                                    // :0634cca3-:0634ccb0 FUN_063605a0
    env->sim_unit_manager->simulate_variable_time_step(env, DeltaTime);      // :0634ccb5-:0634ccbe
}

// FUN_0634cb90: env->+0x0c ->+0x00 (see the header).
IVP_Cluster* IVP_Environment_GetRootCluster(IVP_Environment* env)
{
    return env->cluster_manager->get_root_cluster(); // :0634cb90 + 06261d90
}

// FUN_0634cd80 (.text:0634cd80-0634cdea). `operator new(0xa8)`; on non-null,
// FUN_0634cb90(env) and FUN_06362280(new, root, p1..p4); returns the result
// (null when the allocation failed -- unreachable with the throwing new).
IVP_Polygon* IVP_Environment_CreatePolygon(IVP_Environment* env, IVP_SurfaceManager* SurfaceManager,
                                           const IVP_RealObjectConstructionTemplate* Template,
                                           const IVP_U_Quat* Rotation, const IVP_U_Point* Position)
{
    return new IVP_Polygon(IVP_Environment_GetRootCluster(env), SurfaceManager, Template, Rotation, Position);
}

// FUN_0634cdf0 (.text:0634cdf0-0634ce5a). Same shape as 0634cd80 with
// FUN_06362570 (IVP_Ball) as the constructor.
IVP_Ball* IVP_Environment_CreateBall(IVP_Environment* env, const float* BallRadius,
                                     const IVP_RealObjectConstructionTemplate* Template,
                                     const IVP_U_Quat* Rotation, const IVP_U_Point* Position)
{
    return new IVP_Ball(IVP_Environment_GetRootCluster(env), BallRadius, Template, Rotation, Position);
}

// FUN_0634cb20 (.text:0634cb20-0634cb5f).
void IVP_Environment_SetGravity(IVP_Environment* env, const IVP_U_Point* Gravity)
{
    env->field_b4 = Gravity->x;                                              // :0634cb2c/:0634cb32
    env->field_bc = Gravity->z;                                              // :0634cb29/:0634cb38 (raw dword)
    env->field_b8 = Gravity->y;                                              // :0634cb26/:0634cb40
    // FUN_06347e60: FLD z, y, x; x*x + y*y + z*z on the x87 stack; FSQRT;
    // the caller's FSTP float (:0634cb4f).
    const double X = Gravity->x;
    const double Y = Gravity->y;
    const double Z = Gravity->z;
    env->field_c4 = (float)(std::sqrt(X * X + Y * Y + Z * Z));
    env->standard_gravity_controller->set_grav_vector(Gravity);             // :0634cb55-:0634cb58
}

// FUN_06349510 (.text:06349510-0634952b).
void IVP_Standard_Gravity_Controller::set_grav_vector(const IVP_U_Point* Gravity)
{
    grav_vec.x = Gravity->x; // :0634951e
    grav_vec.y = Gravity->y; // :06349525
    grav_vec.z = Gravity->z; // :06349528 (raw dword)
}

// LAB_06349530 (.text:06349530-063495af, RET 8). For every core of the list,
// last to first: skip it when `TEST AH,0x3` on its Flags (+0x00) is non-zero
// (bits 8/9); else FUN_06346980(core, EventSim->delta_time) and
// FUN_06346dd0(core), then Speed += delta_time * grav_vec. The three sums are
// formed on the x87 stack (double) and stored z (:06349590), x (:0634959c),
// y (:063495a2).
void IVP_Standard_Gravity_Controller::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    for (int32_t I = static_cast<int32_t>(CoreList->n_elems) - 1; I >= 0; --I)  // :06349536-:0634953f / :063495a8-:063495a9
    {
        IVP_Core* Core = CoreList->elems[I];                                    // :06349547-:0634954e
        if ((Core->Flags & 0x300u) != 0)                                        // :06349551-:06349556
            continue;

        Core->damp_velocities(EventSim->delta_time);                            // :06349558-:0634955e FUN_06346980
        Core->commit_speed_changes();                                           // :06349563-:06349565 FUN_06346dd0

        const double Dt = EventSim->delta_time;                                 // :0634956a
        const double NewY = Dt * grav_vec.y + Core->Speed.y;                    // :0634956f-:06349572
        const double NewZ = Dt * grav_vec.z + Core->Speed.z;                    // :0634957a-:0634957d
        Core->Speed.z = (float)(NewZ);                               // :06349583-:06349590
        Core->Speed.x = (float)(Dt * grav_vec.x + Core->Speed.x);    // :0634958d-:0634959c
        Core->Speed.y = (float)(NewY);                               // :063495a2
    }
}

// LAB_0634c6c0: `MOV EAX,0x3e8; RET`.
IVP_CONTROLLER_PRIORITY IVP_Standard_Gravity_Controller::get_controller_priority()
{
    return IVP_CP_GRAVITY;
}

// ============================================================================
// IVP_Range_Manager (cmn-phy5, 2026-10-08). Constants read off the listing:
// DAT_063a53d8 = 0x2edbe6ff (1e-10f), DAT_063a5748 = 0x3e4ccccd (0.2f),
// DAT_063a5744 = 0x3e3851ec (0.18f), DAT_063a53d0 = 1.0f. Values kept on the
// x87 stack are held as double and rounded to float exactly where retail
// FST/FSTPs; every FCOM/TEST AH,0x1 (C0: less or unordered) becomes
// `!(a >= b)`, every TEST AH,0x41 + JNZ keep becomes `a > b` -> replace.
// ============================================================================

// FUN_06360360 (.text:06360360-063603b6, RET 8): +0x04 = param_2, +0x08 =
// param_1, vptr 063a5738, then the ten defaults (raw dwords 0x3f000000,
// 0x3f666666, 0x3f4ccccd, 0x41200000, 0x3dcccccd, 0x3f800000, 0x40a00000,
// 0x3f000000, 0x41700000, 0x3dcccccd).
IVP_Range_Manager::IVP_Range_Manager(IVP_Environment* Environment, int32_t bInDeleteOnEnvDelete)
    : bDeleteOnEnvDelete(bInDeleteOnEnvDelete)          // :0636036a
    , environment(Environment)                          // :0636036d
    , look_ahead_time_intra(0.5f)                       // :0636039b
    , look_ahead_max_radius_intra(0.9f)                 // :0636039e
    , look_ahead_min_distance_intra(0.8f)               // :063603a5
    , look_ahead_max_distance_intra(10.0f)              // :063603ac
    , look_ahead_min_seconds_intra(0.1f)                // :063603b3
    , look_ahead_time_world(1.0f)                       // :06360380
    , look_ahead_max_radius_world(5.0f)                 // :06360387
    , look_ahead_min_distance_world(0.5f)               // :0636038e
    , look_ahead_max_distance_world(15.0f)              // :06360391
    , look_ahead_min_seconds_world(0.1f)                // :06360398
{
}

// LAB_063603e0 (.text:063603e0-063604f9, RET 0x10).
void IVP_Range_Manager::get_coll_range_intra_objects(const IVP_Real_Object* Object0, const IVP_Real_Object* Object1,
                                                     float* Range0, float* Range1)
{
    const IVP_Core* Core0 = Object0->core;                                              // :063603e7
    const IVP_Core* Core1 = Object1->core;                                              // :063603fe

    const float Speed0 = (float)(
        ((double)(Core0->BoundingRadius) + Core0->Margin) + 1.0e-10f);         // :063603ed-:0636040a
    const double Speed1Ext = ((double)(Core1->BoundingRadius) + Core1->Margin) + 1.0e-10f; // :0636040e-:0636041a
    const float Speed1 = (float)(Speed1Ext);                                 // :06360420 FST
    const float SpeedSum = (float)(Speed1Ext + Speed0);                      // :06360424-:06360428

    // :0636042c-:06360445 -- `FLD c0+4; FCOMP c1+4; TEST AH,0x1; JZ`: the
    // smaller upper-limit radius (core1's when not less).
    const float MinRadius = (Core0->UpperLimitRadius < Core1->UpperLimitRadius)
        ? Core0->UpperLimitRadius : Core1->UpperLimitRadius;

    const double A = (double)(SpeedSum) * look_ahead_time_intra;             // :06360449-:0636044e
    const double BExt = (double)(MinRadius) * look_ahead_max_radius_intra;   // :06360451-:06360455
    const float B = (float)(BExt);                                           // :06360458 FST
    double Value = !(BExt >= A) ? (double)(B) : A;                           // :0636045c-:06360467
    if (!(Value >= look_ahead_min_distance_intra))                                      // :0636046b-:06360477
        Value = look_ahead_min_distance_intra;
    if (Value > look_ahead_max_distance_intra)                                          // :0636047a-:06360486
        Value = look_ahead_max_distance_intra;

    Value = Value - (double)(environment->delta_time) * SpeedSum;            // :06360489-:06360496
    const float MinValue = (float)((double)(SpeedSum) * look_ahead_min_seconds_intra); // :06360498-:0636049f
    if (!(Value >= MinValue))                                                           // :063604a3-:063604b0
        Value = MinValue;

    // :063604b4-:063604f4 -- split Value between the two objects.
    const double Share0Ext = (double)(Speed1) * 0.2f + Speed0;               // :063604b4-:063604c6
    const float Share0 = (float)(Share0Ext);                                 // :063604ca FST
    const double Share1Ext = Share0Ext * 0.18f + Speed1;                                // :063604ce-:063604d4
    const float Share1 = (float)(Share1Ext);                                 // :063604d8 FST
    const double Scale = Value * (1.0f / (Share1Ext + Share0));                         // :063604dc-:063604e6
    *Range0 = (float)(Scale * Share0);                                       // :063604e8-:063604ee
    *Range1 = (float)(Scale * Share1);                                       // :063604f0-:063604f4
}

// FUN_06360500 (.text:06360500-06360596, RET 4). The result is left on the
// x87 stack unrounded; the port returns it as a double for that reason.
double IVP_Range_Manager::get_coll_range_in_world(const IVP_Real_Object* Object)
{
    const IVP_Core* Core = Object->core;                                                // :06360505
    const float Speed = (float)(
        ((double)(Core->BoundingRadius) + Core->Margin) + 1.0e-10f);           // :0636050b-:06360524
    const float Radius = Core->UpperLimitRadius;                                        // :06360511-:0636051a

    const double A = (double)(Speed) * look_ahead_time_world;                // :06360528-:0636052c
    const double BExt = (double)(Radius) * look_ahead_max_radius_world;      // :0636052f-:06360533
    const float B = (float)(BExt);                                           // :06360536 FST
    double Value = !(BExt >= A) ? (double)(B) : A;                           // :0636053a-:06360545
    if (!(Value >= look_ahead_min_distance_world))                                      // :06360549-:06360555
        Value = look_ahead_min_distance_world;
    if (Value > look_ahead_max_distance_world)                                          // :06360558-:06360564
        Value = look_ahead_max_distance_world;

    Value = Value - (double)(environment->delta_time) * Speed;               // :06360567-:06360574
    const float MinValue = (float)(
        (double)(Speed) * look_ahead_min_seconds_world + Core->UpperLimitRadius); // :06360576-:06360580
    if (!(Value >= MinValue))                                                           // :06360584-:06360591
        Value = MinValue;
    return Value;
}

// LAB_063603c0 (.text:063603c0-063603ce, RET 4): `if (+0x04) free(this)` --
// operator delete without a destructor call (the class has none).
void IVP_Range_Manager::environment_will_be_deleted(IVP_Environment* /*Environment*/)
{
    if (bDeleteOnEnvDelete != 0)
        delete this;
}

// ============================================================================
// IVP_PerformanceCounter_Simple (cmn-phy5, 2026-10-08).
// ============================================================================

// FUN_063602a0 (.text:063602a0-063602b8): vptr 063a5714, then `REP STOSD`
// ECX = 0x29 from +0x04 (0xa4 bytes, the whole object after the vptr).
IVP_PerformanceCounter_Simple::IVP_PerformanceCounter_Simple()
{
    std::memset(reinterpret_cast<char*>(this) + 4, 0, 0xa4);
}

// LAB_06360350: `MOV dword ptr [ECX+0x10],0; RET`.
void IVP_PerformanceCounter_Simple::Slot00()
{
    CurrentElement = 0;
}

// LAB_063602e0 (.text:063602e0-0636034c, RET 4).
void IVP_PerformanceCounter_Simple::Slot01(int32_t Element)
{
    if (Element == 1)                                                        // :063602e8-:063602ef
        ++CountPSIs;                                                         // :063602f1

    LARGE_INTEGER Counter;
    LARGE_INTEGER Frequency;
    QueryPerformanceCounter(&Counter);                                       // :063602f4-:063602f9
    QueryPerformanceFrequency(&Frequency);                                   // :063602ff-:06360304

    // :0636030a-:06360319 -- the 32-bit low-dword difference, FILD as a signed dword.
    const int32_t Delta = static_cast<int32_t>(static_cast<uint32_t>(Counter.LowPart) - LastCounterLow);
    LastCounterLow = static_cast<uint32_t>(Counter.LowPart);                 // :06360320
    LastCounterHigh = static_cast<int32_t>(Counter.HighPart);                // :06360323-:06360335

    // :06360319-:06360341 -- Delta * 1e6 (DAT_063a5730, double) / Frequency
    // (FILD qword) + Counters[CurrentElement] (FIADD dword), _ftol (truncate).
    FCounter& Slot = Counters[CurrentElement];                               // :0636032d
    const double Elapsed = ((double)(Delta) * 1000000.0) / (double)(Frequency.QuadPart);
    Slot.Microseconds = static_cast<int32_t>(Elapsed + Slot.Microseconds);   // :0636033a-:06360341

    CurrentElement = Element;                                                // :06360344
}

// LAB_06360280 (.text:06360280-0636028b, RET 4): `if (this) this->slot5(1)`.
void IVP_PerformanceCounter_Simple::environment_is_going_to_be_deleted(IVP_Environment* /*Env*/)
{
    delete this;
}

// FUN_06356960 lives in src/IVP/ivp_utility/ivp_message.cpp (the IVP message
// printf; C linkage because the vendored qhull also calls it).
extern "C" void FUN_06356960(const char* Format, ...);

// FUN_06360110 (.text:06360110-0636027a, RET 8) -- the library's
// reset_and_print_performance_counters(IVP_Time). Nothing happens unless at
// least one PSI was counted (`FILD +0x14; FCOMP 0.0; TEST AH,0x40; JNZ`).
void IVP_PerformanceCounter_Simple::Slot04(float TimeHi, float TimeLo)
{
    const float PsiCount = (float)(CountPSIs);                    // :0636011d-:06360121
    if (PsiCount == 0.0f)                                                     // :06360125-:06360130
        return;

    // :06360136-:06360171 -- integer sums: elements 1,4,5,6,8 and 2,3.
    const int32_t Coll = Counters[1].Microseconds + Counters[4].Microseconds + Counters[5].Microseconds
                       + Counters[6].Microseconds + Counters[8].Microseconds;
    const int32_t Dyn = Counters[2].Microseconds + Counters[3].Microseconds;

    // :0636017f-:063601ba -- 0.001f (DAT_063a5604) / PsiCount stays on the
    // stack for the total and is stored as the float `Scale` for the rest.
    const double ScaleExt = (double)(0.001f) / PsiCount;
    const float Scale = (float)(ScaleExt);
    const float Total = (float)(ScaleExt * ((double)(Dyn) + Coll));

    // :063601bc-:06360249 -- the eleven double arguments, 66.0f (DAT_063a5710)
    // and 0.1 (DAT_063a5708, double) for the percentage.
    FUN_06356960("TOT %2.1f%% %2.2f COLL %2.2f  DYN %2.2f     det:  UNIV: %2.2f CONTR: %2.2f INTEGR: %2.2f HULL: %2.2f SHORT: %2.2f CRITIC: %2.2f AT %2.2f",
                 ((double)(Total) * 66.0f) * 0.1,
                 (double)(Total),
                 (double)(Scale) * Coll,
                 (double)(Scale) * Dyn,
                 (double)(Counters[1].Microseconds) * Scale,
                 (double)(Counters[2].Microseconds) * Scale,
                 (double)(Counters[3].Microseconds) * Scale,
                 (double)(Counters[4].Microseconds) * Scale,
                 (double)(Counters[5].Microseconds) * Scale,
                 (double)(Counters[6].Microseconds) * Scale,
                 (double)(Counters[8].Microseconds) * Scale);

    // :06360256-:0636026e -- clear +0x04..+0xa7, then +0xa0/+0xa4 = the argument.
    std::memset(reinterpret_cast<char*>(this) + 4, 0, 0xa4);
    TimeOfLastReset.time_base = TimeHi;
    TimeOfLastReset.time_offset = TimeLo;
}

// ============================================================================
// FUN_0634bf50 (decomp-tracker item 3031, Common.dll.txt:419989-420559, 487
// insns) -- IVP_Environment's real constructor.
//
// SHAPE. The body is an install list and almost nothing else: it runs its
// embedded members' constructors, wipes itself, then fills ~20 slots. Fourteen
// of those slots follow one identical pattern, which is worth stating once
// instead of fourteen times:
//
//     EAX = Parms->Field;            // caller may supply the object
//     self->Slot = EAX;
//     if (EAX != 0) goto done;       // adopt it, do not construct
//     EAX = operator new(SIZE);
//     if (EAX != 0) EAX = Ctor(EAX, args); else EAX = 0;
//     self->Slot = EAX;
//   done:
//
// i.e. a caller-supplied object is ADOPTED (and not owned), and only a null
// one causes the environment to allocate and construct its own. The
// `if (EAX != 0)` around each constructor call is MSVC's standard
// operator-new-may-return-null guard; this port calls ::operator new, which
// throws instead, so those branches are unreachable here and are deliberately
// not reproduced -- the same precedent the former common_stubs.cpp's FUN_0634cdf0
// documented for itself.
//
// THE SELF-WIPE IS REAL AND IT IS ORDERED. At 0634c00b-0634c014 the body does
// `XOR EAX,EAX; MOV ECX,0x52; REP STOSD` over EDI=this -- memset(this, 0,
// 0x148), the whole object. That runs AFTER the compiler-emitted member
// constructors at 0634bf7a (the +0x34 block) and 0634bf85 (the +0x88 block),
// so it destroys their work; retail then re-runs the +0x88 one at 0634c016 and
// never re-runs the +0x34 one. This is not a decompilation artifact and it is
// not dead code -- it is Ipion's own P_MEM_CLEAR(this) idiom sitting in the
// constructor body, which by definition runs after member initialization. It
// is reproduced in that order, including the redundant first pass, because
// "harmless today" is not the same as "did not happen". The same applies to
// the six inline IVP_U_Vector_Base member constructors at 0634bf8c-0634c005.
//
// WHAT IS NOT REPRODUCED, and why:
//   * The SEH scope-index stores (`MOV byte [ESP+local_4], N`, 38 of them).
//     Pure MSVC unwind bookkeeping with no observable effect.
// Every polymorphic sub-object (collision filter, anomaly manager / limits,
// gravity controller 063a5570, range manager, performance counter, ...) is a
// real port class built with `new`, so its vptr is the compiler's.
// ============================================================================
IVP_Environment* IVP_Environment_Construct(IVP_Environment* env,
                                           IVP_Environment_Manager* Registry,
                                           const IVP_Environment_Create_Parms* Parms,
                                           const char* Name,
                                           void* ClientData)
{
    // ---- compiler-emitted member constructors, before the body -------------
    IVP_Environment_Init_Block34(env->manager_context_34);         // 0634bf7a, FUN_063505f0
    IVP_Environment_Init_Block88(&env->default_friction);          // 0634bf85, FUN_0634bf30

    // 0634bf8c-0634c005: six inline IVP_U_Vector_Base member ctors, each a
    // {mem_size=0, n_elems=0, elems=null} triple written as word/word/dword.
    env->vector_listeners_c8 = IVP_U_Vector_Base{0, 0, nullptr};   // +0xC8
    env->vector_listeners_d0 = IVP_U_Vector_Base{0, 0, nullptr};   // +0xD0
    env->event_queue         = IVP_U_Vector_Base{0, 0, nullptr};   // +0xD8
    env->vector_e0           = IVP_U_Vector_Base{0, 0, nullptr};   // +0xE0
    env->object_listeners    = IVP_U_Vector_Base{0, 0, nullptr};   // +0x124
    env->vector_12c          = IVP_U_Vector_Base{0, 0, nullptr};   // +0x12C

    // ---- the constructor body proper ---------------------------------------
    std::memset(env, 0, sizeof(IVP_Environment));                  // 0634c00b-0634c014
    IVP_Environment_Init_DefaultFriction(&env->default_friction);  // 0634c016, FUN_0634bf40

    env->sim_substate = 5;                                         // 0634c030
    // 0634c03a: the +0x34 block's first dword is a back-pointer to the owner.
    *reinterpret_cast<IVP_Environment**>(env->manager_context_34) = env;

    // ---- adopt-or-construct, in retail's own order -------------------------

    env->l_material_manager = Parms->MaterialManager;              // 0634c03c-0634c041
    if (env->l_material_manager == nullptr)                        // 0634c047
    {
        env->l_material_manager = new IVP_Material_Manager(1);     // 0634c049-0634c078, FUN_0634bd80
    }

    env->l_active_value_manager = Parms->ActiveValueManager;       // 0634c07e-0634c083
    if (env->l_active_value_manager == nullptr)                    // 0634c089
    {
        env->l_active_value_manager = new IVP_U_Active_Value_Manager(1); // 0634c08b-0634c0ba, FUN_063615a0
    }

    env->collision_filter = Parms->CollisionFilter;              // 0634c0c0-0634c0c5
    if (env->collision_filter == nullptr)                         // 0634c0c8
    {
        // 0634c0ca-0634c0f9: operator new(8), FUN_0634bcb0(1) (vtable 0639c3fc).
        env->collision_filter = new IVP_Collision_Filter_Coll_Group_Ident(1);
    }

    env->anomaly_manager = static_cast<IVP_Anomaly_Manager*>(Parms->AnomalyManager); // 0634c0fc-0634c101
    if (env->anomaly_manager == nullptr)                           // 0634c104
    {
        // 0634c106-0634c135: operator_new(8) + FUN_06360eb0(1) -- the real
        // polymorphic class (vtable 063a5784); the raw extern "C" copy that
        // stored a null vptr was removed 2026-10-08 (cmn-phy5).
        env->anomaly_manager = new IVP_Anomaly_Manager(1);
    }

    env->anomaly_limits = static_cast<IVP_Anomaly_Limits*>(Parms->AnomalyLimits); // 0634c138-0634c13d
    if (env->anomaly_limits == nullptr)                            // 0634c140
    {
        // 0634c142-0634c171: operator_new(0x14) + FUN_06360e30(1) (vtable 063a577c).
        env->anomaly_limits = new IVP_Anomaly_Limits(1);
    }

    // 0634c174-0634c1ed: this one is NOT a slot -- the object is appended to
    // vector_12c rather than stored in a fixed field, but the adopt-or-
    // construct choice is the same.
    void* Vector12cElement = Parms->Vector12cElement;              // 0634c174
    if (Vector12cElement == nullptr)                               // 0634c179
    {
        // 0634c17b-0634c197: operator_new(4) + FUN_06360df0, the ctor of the
        // library's default delegator (vtable 063a5760, no fields).
        Vector12cElement = static_cast<IVP_Collision_Delegator_Root*>(new IVP_Collision_Delegator_Root_Mindist());
    }
    IVP_U_Vector_Base_Append(&env->vector_12c, Vector12cElement);  // 0634c1a2-0634c1ed

    // 0634c1f8-0634c1fb: adopted verbatim, never constructed here.
    env->universe_manager = Parms->UniverseManager;

    // ---- standard_gravity_controller (+0x00) and the gravity vector --------
    // 0634c1e8-0634c21b: operator_new(0x14), vptr 063a5570, nothing else.
    {
        env->standard_gravity_controller = new IVP_Standard_Gravity_Controller();

        // 0634c21d-0634c22d: {0.0f, 9.83f, 0.0f}. The Y component is the raw
        // dword 0x411d47ae, which is 9.829999923706055 -- NOT the 9.80999994
        // this port's notes claimed for years (9.81f would be 0x411cf5c3).
        IVP_U_Point Gravity;
        Gravity.set(0.0f, 9.83f, 0.0f);

        IVP_Environment_SetGravity(env, &Gravity);                 // 0634c235

        // 0634c23a-0634c260: the same three floats are then copied into the
        // controller's grav_vec once more (the inlined set_grav_vector of the
        // library's ctor). Retail re-reads them from the stack after the call
        // (FUN_0634cb20 does not modify them).
        env->standard_gravity_controller->grav_vec.x = Gravity.x;
        env->standard_gravity_controller->grav_vec.y = Gravity.y;
        env->standard_gravity_controller->grav_vec.z = Gravity.z;
    }

    // ---- register this environment with the process-wide registry ----------
    // 0634c25a-0634c281. Registry->Environments (+0x4); `this` is appended to
    // it -- the mirror of the removal IVP_Environment_Destructor performs.
    IVP_U_Vector_Base_Append(
        reinterpret_cast<IVP_U_Vector_Base*>(&Registry->Environments), env);

    // ---- controller_manager (+0x90): 4 bytes holding `this` ----------------
    {
        IVP_Controller_Manager* const ControllerManager =
            static_cast<IVP_Controller_Manager*>(::operator new(4)); // 0634c285
        ControllerManager->environment = env;                         // 0634c291
        env->controller_manager = ControllerManager;                  // 0634c29e
    }

    // 0634c297-0634c2a4: PUSH 0x3c783e10 == 1.0f/66.0f.
    IVP_Environment_SetDeltaPsiTime(env, IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME);

    // ---- unconditional sub-managers (no adopt path) ------------------------

    env->cluster_manager = new IVP_Cluster_Manager(env);           // 0634c2a9-0634c2dc (ctor 0634ad70)
    env->sim_units_manager = new IVP_Sim_Units_Manager(env);       // 0634c2cf-0634c30d (ctor 06349380)
    {
        void* p = ::operator new(0x1C);                            // 0634c303
        IVP_SimUnitManager_Construct(p);                           // 0634c32a, FUN_063608a0
        env->sim_unit_manager = static_cast<IVP_Sim_Unit_Manager*>(p); // 0634c33d
    }
    env->mindist_manager = new IVP_Mindist_Manager(env);           // 0634c333-0634c371 (ctor 06350390)
    env->ov_tree_manager = new IVP_OV_Tree_Manager();              // 0634c364-0634c3a1 (ctor 0635c410)
    env->interval_gate = new IVP_Env_Interval_Gate();             // 0634c397-0634c3c7 (ctor 063605e0)

    // ---- two more adopt-or-construct slots ---------------------------------

    env->range_manager = Parms->RangeManager;                      // 0634c3cd-0634c3d0
    if (env->range_manager == nullptr)                             // 0634c3d3
    {
        env->range_manager = new IVP_Range_Manager(env, 1);        // 0634c3e0-0634c410, FUN_06360360
    }

    // 0634c413-0634c44c. Shaped slightly differently in retail -- the store to
    // +0x28 is shared by both paths and sits at the join -- but equivalent.
    env->performance_counter = Parms->PerformanceCounter;
    if (env->performance_counter == nullptr)
    {
        env->performance_counter = new IVP_PerformanceCounter_Simple(); // 0634c41a-0634c44c, FUN_063602a0
    }

    env->cache_object_manager = new IVP_Cache_Object_Manager(Parms->NumberOfCacheObjects); // 0634c44a-0634c482 (ctor 0634d5c0)

    // ---- time/step scalars -------------------------------------------------
    // 0634c475-0634c4f5. current_time is zeroed, environment_manager takes the registry
    // pointer, sim_step_count starts at 1, and +0x100/+0x104 receive the
    // integer and fractional parts of delta_time:
    //   FLD [this+0xac]; FLD ST0          ; two copies of dt
    //   CALL ftol                         ; EAX = (long)dt, pops one copy
    //   FILD it back; FST -> [this+0x100] ; the truncated part, as a float
    //   FXCH; FSUB ST0,ST1; FSTP -> [+0x104]  ; dt - trunc(dt)
    // With the default dt of 1/66 that is {0.0f, 0.0151515f}.
    env->current_time = IVP_Time{};                                // 0634c4a2 / 0634c4c0
    env->environment_manager = Registry;                           // 0634c4b0
    env->sim_step_count = 1;                                       // 0634c4b6
    {
        const float Truncated = (float)(static_cast<long>(env->delta_time)); // ftol
        env->next_psi_time_base = Truncated;                                // 0634c4df
        env->next_psi_time_offset = env->delta_time - Truncated;              // 0634c4ef
    }

    // ---- the two scratch arenas (+0xA0 / +0xA4) ----------------------------
    env->vector_cores_active = new IVP_U_Memory; // 0634c4f7 new(0x14), 0634c511 FUN_06344120, 0634c524
    env->vector_cores_frozen = new IVP_U_Memory; // 0634c52a new(0x14), 0634c544 FUN_06344120, 0634c54d

    // 0634c553-0634c57d: one caller-supplied block, split exactly in half.
    // `SAR EDI,1` is an arithmetic shift, so a negative size halves toward
    // negative infinity -- reproduced with a shift rather than a division,
    // which would round toward zero instead.
    {
        const int32_t HalfSize = Parms->MemoryBlockSize >> 1;
        env->vector_cores_active->init_mem(Parms->MemoryBlock, HalfSize);
        env->vector_cores_frozen->init_mem(Parms->MemoryBlock + HalfSize, HalfSize);
    }

    // ---- damping factor and the two global dispatch tables -----------------
    env->next_movement_check = 10;                                 // 0634c595

    // 0634c582-0634c5ba: pow(0.9, delta_time), open-coded as
    // exp2(log2e * dt * ln(0.9)) with the classic FYL2X / F2XM1 / FSCALE
    // sequence. DAT_063a5568 is the double 0x3FECCCCCC0000000, which is
    // exactly (double)0.9f -- i.e. a float 0.9 widened, not the double 0.9.
    env->field_120 = (float)(
        std::pow((double)(0.9f), (double)(env->delta_time)));

    IVP_Mindist_Minimize_Solver::init_mms_function_table();        // 0634c5c0, FUN_06353940
    IVP_InitSimUnitDispatchTable();                                // 0634c5c5, FUN_06360090

    // ---- name, client data -------------------------------------------------
    // 0634c5d2: FUN_06356920, IVP's strdup -- it returns null for a null
    // string itself (`TEST ESI,ESI; JZ`, .text:06356925), else malloc
    // (FUN_06343f70) of strlen + 1 and a copy. The null test is retail's.
    env->environment_name = (Name != nullptr) ? _strdup(Name) : nullptr; // 0634c5eb
    env->client_data = ClientData;                                 // 0634c5de
    env->field_f0 = 10;                                            // 0634c5f1

    // ---- static_object (+0x30) ---------------------------------------------
    // 0634c5f7-0634c68c. Retail builds the whole construction template on its
    // own stack, hands it to FUN_0634cdf0, and tears it down again.
    //
    // The stack arithmetic is what identifies the type: local_64 spans exactly
    // 0x58 bytes (-0x64 up to the SEH cookie at -0x0c), local_a4 spans exactly
    // 0x40 (-0xa4 to -0x64), and the stores retail makes into "separate"
    // locals land inside them -- local_58 is local_64+0x0c (FlagsHi) and
    // local_14 is local_64+0x50 (OptionalComOverride). So the 0x58-byte object
    // FUN_063486c0 constructs here IS the construction template (the former
    // IVP_Material duplicate was merged into it 2026-10-08).
    {
        float Bias = 1.0f;                        // 0634c5f7, local_c0 = 0x3f800000

        IVP_RealObjectConstructionTemplate Template; // 0634c5ff, FUN_063486c0
        Template.set_long_name("static_object");  // 0634c615, FUN_06348690

        // 0634c626, FUN_063481b0: an identity IVP_U_Matrix. Retail zero-fills
        // all 0x40 bytes and then stamps 1.0f at +0x00/+0x14/+0x28, so the
        // per-row k[3] pads end up zeroed too -- IVP_U_Matrix::init() alone
        // would leave them untouched, hence the explicit memset first.
        IVP_U_Matrix LocalCsys;
        std::memset(&LocalCsys, 0, sizeof(LocalCsys));
        LocalCsys.rows[0].k[0] = 1.0f;
        LocalCsys.rows[1].k[1] = 1.0f;
        LocalCsys.rows[2].k[2] = 1.0f;

        Template.FlagsHi = 1;                     // 0634c61e
        // 0634c639 -- OptionalComOverride. Typed void* on the template side;
        // this call site proves it points at an IVP_U_Matrix, which is exactly
        // consistent with that field's documented "+0x30/+0x34/+0x38 float
        // triple" reader in FUN_06345b20 (the matrix's own vv).
        Template.OptionalComOverride = &LocalCsys;

        // 0634c648-0634c658: LocalCsys.vv zeroed (again -- already covered by
        // the memset above, but retail writes it separately).
        LocalCsys.vv.k[0] = 0.0f;
        LocalCsys.vv.k[1] = 0.0f;
        LocalCsys.vv.k[2] = 0.0f;

        // 0634c660-0634c670: the position, reusing the same three stack slots
        // the gravity vector occupied earlier.
        IVP_U_Point Position;
        Position.k[0] = 0.0f;
        Position.k[1] = 0.0f;
        Position.k[2] = 0.0f;

        // 0634c678. Orientation is a literal NULL (PUSH EBP with EBP==0 at
        // 0634c634), meaning identity.
        env->static_object = IVP_Environment_CreateBall(env, &Bias, &Template, nullptr, &Position); // 0634c681

        // 0634c68c, thunk_FUN_06348670 -> FUN_06348670: the template's
        // destructor runs here at scope exit (and on the unwind path).
    }

    env->state_magic = 0x1E240;                                    // 0634c691, 123456
    return env;                                                    // 0634c69b, MOV EAX,ESI
}

// ============================================================================
// IVP_Environment_Manager (cmn-phy5, 2026-10-08) -- see ivp_environment.h.
// ============================================================================

// DAT_0658aa00. Constructed/destroyed by the C++ static-init/atexit machinery,
// as retail's LAB_0634cc20 / LAB_0634cc50 do it.
static IVP_Environment_Manager G_IVP_EnvironmentManager;

// FUN_0634cc70.
IVP_Environment_Manager* IVP_Environment_Manager::get_environment_manager()
{
    return &G_IVP_EnvironmentManager;
}

// FUN_0634cba0 (.text:0634cba0-0634cc10, RET 0xc). Retail returns 0 when
// operator_new(0x148) returns null; this port's ::operator new throws instead
// (the convention every IVP allocation site here follows).
IVP_Environment* IVP_Environment_Manager::create_environment(IVP_Environment_Create_Parms* Parms,
                                                             const char* LicenseName, uint32_t LicenseKey)
{
    IVP_Environment* Env = static_cast<IVP_Environment*>(::operator new(sizeof(IVP_Environment))); // :0634cbb7-:0634cbbe
    return IVP_Environment_Construct(Env, this, Parms, LicenseName,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(LicenseKey))); // :0634cbd6-:0634cbe8
}
