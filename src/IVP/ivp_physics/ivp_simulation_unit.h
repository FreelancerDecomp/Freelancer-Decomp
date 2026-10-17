// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_core.h"
#include "../ivp_utility/ivp_u_vector.h"
#include <cstdint>
#include <cstddef>

// ============================================================================
// IVP_Simulation_Unit -- reverse-engineered this pass (decomp-tracker item
// 2919 and its five helper functions) purely from its DESTRUCTION path.
// D69 (2026-09-09, item 2866) identified that IVP_Core::~IVP_Core()
// (FUN_063477e0) detaches a dying Core from its owning IVP_Simulation_Unit
// via FUN_06348cb0, and left the whole subsystem unprocessed. D72 (2026-09-09)
// decoded FUN_06348cb0 plus its five callees:
//   FUN_06348ef0  Common.dll.txt:414099-414138  remove_core
//   FUN_06348c80  Common.dll.txt:413799-413820  find_controller_bucket_index
//   FUN_06348e60  Common.dll.txt:414020-414062  free_controller_bucket
//   FUN_06348d80  Common.dll.txt:413933-413986  free_vector_buffers (SEH-framed)
//   FUN_06349460  Common.dll.txt:414834-414848  unlink_from_manager (+ its own
//                 callee FUN_06349430, Common.dll.txt:414798-414817, a raw
//                 intrusive doubly-linked-list unlink)
//   FUN_06348cb0  Common.dll.txt:413832-413918  detach_core (orchestrator,
//                 the function IVP_Core's destructor actually calls)
//   FUN_06348be0  Common.dll.txt:413707-413729  bucket's own vector-Clear
//                 (byte-for-byte identical to IVP_U_Vector<T>::Clear() at the
//                 same relative offset, see IVP_Sim_Unit_Controller_Bucket
//                 below -- reused via IVP_U_Vector<T>::Clear(), not
//                 hand-duplicated)
//
// Confirmed field offsets (backward linear-search / RemoveAt patterns cross-
// checked against IVP_U_Vector<T>'s own Realloc/RemoveAt in ivp_u_vector.h,
// and against IVP_Core::MovementState's identical "shift byte to sign bit,
// signed CMP <8" idiom for state-based branching):
//   +0x00 State                  uint32_t; only the low byte is semantically
//                                 read (same idiom as IVP_Core::MovementState)
//                                 -- selects which of the owning manager's two
//                                 lists (manager+0x14 / manager+0x1A4) this
//                                 unit belongs to. Confirmed via FUN_06349460/
//                                 FUN_06349400/FUN_06349490 (Common.dll.txt:
//                                 414834-414906, 414767-414781, 414865-414906).
//   +0x04 Prev / +0x08 Next       IVP_Simulation_Unit*; intrusive doubly-
//                                 linked-list links within the manager's
//                                 active/sleeping list (FUN_06349430/
//                                 FUN_063493e0, Common.dll.txt:414798-414817,
//                                 414749-414763).
//   +0x0C Cores                   IVP_U_Vector<IVP_Core> header. Matches the
//                                 SAME n_elems@+0xE/elems@+0x10 fields
//                                 IVP_Core::ensure_in_simulation() and
//                                 ::revive_sleeping_core() already read via
//                                 raw offsets in ivp_core.cpp (written by an
//                                 earlier lane before this class existed).
//   +0x14 CoresInline[2]          CONFIRMED Fixed<T,2> (D-ivpenv-staticobject
//                                 pass, 2026-09-10, FUN_06348e10 -- the real
//                                 constructor, see below): `MOV [this+0x10],
//                                 this+0x14; MOV word[this+0xc],2` sets
//                                 Cores.elems to this inline buffer and
//                                 Cores.memsize to exactly 2.
//   +0x1C ControllerBuckets       IVP_U_Vector<IVP_Sim_Unit_Controller_Bucket>
//                                 header (mem_size@0x1C/n_elems@0x1E/
//                                 elems@0x20 -- read directly by FUN_06348c80/
//                                 FUN_06348cb0/FUN_06348e60/FUN_06348d80).
//
// Object size CONFIRMED 0x24 bytes (D-ivpenv-staticobject pass, 2026-09-10):
// FUN_06347550 (decomp-tracker item 2862, IVP_Core's own constructor)
// allocates the unit via `operator_new(0x24)`, and its constructor
// (FUN_06348e10 below) explicitly zero/inits ControllerBuckets' header
// (mem_size=0, n_elems=0, elems=nullptr -- NO inline buffer, unlike Cores)
// and touches nothing past +0x24. The previously-guessed trailing
// `ControllerBucketsInline[2]` field (which would have made this 0x2C) does
// not exist -- ControllerBuckets starts completely empty and is grown purely
// via IVP_U_Vector<T>::Realloc/Add, same as any non-Fixed IVP_U_Vector<T>.
// ============================================================================

// FUN_06348ec0/FUN_06348c10 (insertion side) show each bucket is keyed by an
// IVP_Controller* and owns its own small vector of the cores currently
// attached via that controller within one IVP_Simulation_Unit.
// FUN_06348be0 (Common.dll.txt:413707-413729) is reused here as
// IVP_U_Vector<IVP_Core>::Clear() -- confirmed field-for-field identical:
// retail's inline-buffer test (`elems == this+0xC`) is exactly
// `elems == reinterpret_cast<T**>(&Cores + 1)` for a vector header living at
// bucket+0x04, matching Clear()'s own `elems != reinterpret_cast<T**>(this+1)`
// check with `this` = &Cores.
//
// CONFIRMED Fixed<T,2> (D-cluster-06347f50 pass, 2026-09-10, while porting
// FUN_06348900/create_controller_bucket below, decomp-tracker Common.dll item
// 2910): the bucket is allocated via `operator_new(0x14)`, and its own ctor
// sets `Cores.memsize=2` and `Cores.elems = &Cores + 1` (i.e. the 2 T* slots
// immediately following the Cores header, ending exactly at the struct's own
// 0x14-byte extent) -- the previously-open "capacity unconfirmed" question is
// now settled: exactly 2 inline slots, no heap growth path ever exercised for
// a bucket in retail (a controller connects at most 2 cores by construction).
struct IVP_Sim_Unit_Controller_Bucket
{
    IVP_Controller* Controller;            // +0x00: bucket key
    IVP_U_Vector_Fixed<IVP_Core, 2> Cores; // +0x04: cores attached via Controller within this IVP_Simulation_Unit (12 bytes: 8-byte header + 2 inline slots)
};
static_assert(offsetof(IVP_Sim_Unit_Controller_Bucket, Cores) == 0x04, "IVP_Sim_Unit_Controller_Bucket+0x04");
static_assert(sizeof(IVP_Sim_Unit_Controller_Bucket) == 0x14, "IVP_Sim_Unit_Controller_Bucket size must be 0x14 (20 bytes)");

class IVP_Simulation_Unit
{
public:
    uint32_t State;                                            // +0x00
    IVP_Simulation_Unit* Prev;                                 // +0x04
    IVP_Simulation_Unit* Next;                                 // +0x08
    IVP_U_Vector_Fixed<IVP_Core, 2> Cores;                      // +0x0C header, +0x14 inline storage[2]
    IVP_U_Vector<IVP_Sim_Unit_Controller_Bucket> ControllerBuckets;  // +0x1C (no inline storage -- always heap-grown, see comment above)

    // FUN_06348e10 (Common.dll.txt:413994-414059, decomp-tracker item 2921).
    // D-ivpenv-staticobject pass (2026-09-10): the real constructor, traced
    // while porting IVP_Core's own constructor (FUN_06347550/FUN_06347660,
    // ivp_core.h/.cpp) -- FUN_06347550 is this constructor's sole caller
    // (`operator_new(0x24)` immediately followed by this ctor). Sets
    // Cores.elems = CoresInline, Cores.memsize = 2, Cores.n_elems = 0,
    // ControllerBuckets = {0,0,nullptr} (see the class comment above), and
    // State's low byte to 8. Prev/Next are NOT initialized here -- retail
    // leaves them genuinely uninitialized until this unit is first pushed
    // onto a manager list (IVP_Simulation_Unit_List_Push_Front below, which
    // unconditionally overwrites both before this unit becomes reachable via
    // Prev/Next from anywhere), confirmed by FUN_06347550 calling this ctor
    // then immediately going on to push the new unit via
    // IVP_Simulation_Unit_Push_Onto_Manager without ever reading Prev/Next in
    // between.
    //
    // Only the low byte of State is set to a *known* value; retail's own AND
    // mask (0xffffc008) leaves the object's pre-`operator_new` garbage bits
    // 14-31 untouched, which is harmless because every real consumer
    // (IVP_Simulation_Unit_Unlink_From_Manager/Push_Onto_Manager) only ever
    // reads State's low byte (`static_cast<int8_t>(State & 0xFF)`) -- this
    // port just zero-initializes State outright for determinism, which is
    // observationally identical.
    IVP_Simulation_Unit();
};
static_assert(offsetof(IVP_Simulation_Unit, Prev) == 0x04, "IVP_Simulation_Unit+0x04");
static_assert(offsetof(IVP_Simulation_Unit, Next) == 0x08, "IVP_Simulation_Unit+0x08");
static_assert(offsetof(IVP_Simulation_Unit, Cores) == 0x0C, "IVP_Simulation_Unit+0x0C");
static_assert(offsetof(IVP_Simulation_Unit, ControllerBuckets) == 0x1C, "IVP_Simulation_Unit+0x1C");
static_assert(sizeof(IVP_Simulation_Unit) == 0x24, "IVP_Simulation_Unit size must be 0x24 (36 bytes)");

// ============================================================================
// IVP_Sim_Units_Manager -- IVP_Environment+0x08, 0x1a8 bytes, no vtable
// (cmn-phy8 2026-10-08; was `void* time_manager` plus the raw stub
// FUN_06349380 / TimeManager06349380 in common_stubs.cpp). Built by the
// environment constructor (`PUSH 0x1a8; operator_new; JZ; PUSH ESI(env);
// MOV ECX,EAX; CALL FUN_06349380`, .text:0634c2cf-0634c30d) and released by
// the environment destructor with a bare operator delete (.text:0634c7a8-
// 0634c7bc, no destructor). Its thiscall methods 06349400 / 06349460 /
// 063499c0 file units on the two list heads, chosen by the unit's State low
// byte (`SHL 0x18; CMP 0x8000000; JL` -> +0x14, else +0x1a4). Method names are
// the Ipion library's (recalled, not in the binary).
// ============================================================================
class IVP_Sim_Units_Manager
{
public:
    IVP_Environment* environment;         // +0x00: the ctor's argument (:06349389)
    float Field_0x04;                     // +0x04: 9.0f (0x41100000, :063493ae). TODO: no reader traced.
    float Field_0x08;                     // +0x08: 0x3f3ae140 (~0.72851f, :063493bd). TODO: no reader traced.
    float Field_0x0c;                     // +0x0c: 0.0f (:063493cc). TODO: no reader traced.
    float Field_0x10;                     // +0x10: 0.3f (0x3e99999a, :063493cf). TODO: no reader traced.
    IVP_Simulation_Unit* sim_units_head;  // +0x14: units with State low byte < 8 (:06349399 = 0)
    uint8_t Field_0x18[0x18c];            // +0x18..+0x1a3: never written by the ctor (operator_new
                                          // memory). TODO: no access traced.
    IVP_Simulation_Unit* still_units_head; // +0x1a4: units with State low byte >= 8 (:0634939c = 0)

    // FUN_06349380 (.text:06349380-063493d5, thiscall, RET 4). Stores, in
    // retail order: +0x00 env, +0x14 = 0, +0x1a4 = 0, +0x04, +0x08, +0x0c,
    // +0x10 (the floats go through stack temporaries as dword copies).
    // +0x18..+0x1a3 are left as allocated.
    explicit IVP_Sim_Units_Manager(IVP_Environment* Environment);

    // FUN_06349400 (thiscall, RET 4): push Unit at the front of the list its
    // State selects (FUN_063493e0).
    void add_sim_unit_to_manager(IVP_Simulation_Unit* Unit);

    // FUN_06349460 (thiscall, RET 4): unlink Unit from the list its State
    // selects (FUN_06349430).
    void rem_sim_unit_from_manager(IVP_Simulation_Unit* Unit);

    // FUN_063499c0 (thiscall, RET 8): every active unit's PSI step. Called from
    // IVP_Environment::simulate_psi (FUN_0634d090 :0634d110-:0634d123) with the
    // caller's 0x80-inline core vector, which receives every core of every
    // active unit. Defined in ivp_simulation_unit.cpp.
    void simulate_sim_units_psi(IVP_Environment* Environment, IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi);
};
static_assert(offsetof(IVP_Sim_Units_Manager, sim_units_head) == 0x14, "IVP_Sim_Units_Manager+0x14");
static_assert(offsetof(IVP_Sim_Units_Manager, still_units_head) == 0x1a4, "IVP_Sim_Units_Manager+0x1a4");
static_assert(sizeof(IVP_Sim_Units_Manager) == 0x1a8, "IVP_Sim_Units_Manager is 0x1a8 bytes (operator_new(0x1a8), 0634c2cf)");

// FUN_06348cb0: IVP_Simulation_Unit::detach_core -- called from
// IVP_Core::~IVP_Core (FUN_063477e0, Common.dll.txt:411302-411306) whenever a
// dying Core's SimUnit pointer (IVP_Core+0x15C) is non-null. Removes Core from
// Unit's own Cores list and from every per-controller bucket it appeared in;
// if Unit ends up with zero attached Cores, tears the whole unit down (unlinks
// it from the owning manager's list, frees its vector buffers, frees Unit).
//
// Internally reaches the owning IVP_Sim_Units_Manager via `Core->Environment->
// sim_units_manager` (IVP_Environment+0x08, Common.dll.txt:413904-413907:
// `EAX=[EBP+0xc]=Core->Environment; ECX=[EAX+8]` immediately precedes the
// unlink call).
void IVP_Simulation_Unit_Detach_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core);

// (IVP_Simulation_Unit_Unlink_From_Manager / _Push_Onto_Manager, which took
// the manager as a void* and indexed +0x14 / +0x1A4 by hand, are now the
// thiscall members IVP_Sim_Units_Manager::rem_sim_unit_from_manager
// (06349460) / add_sim_unit_to_manager (06349400), cmn-phy8 2026-10-08.
// Both pick the list from Unit->State at call time, so a caller moving a unit
// between the lists unlinks first, then overwrites State, then pushes --
// retail's FUN_06349490 order, Common.dll.txt:414894-414902.)

// ============================================================================
// D-cluster-06348f60 pass (2026-09-10): controller-detach / per-unit wake /
// motion-classification family, continuing directly from the D72/D-ivpenv-
// staticobject passes above. Ported from Common.dll.txt:414165-415117
// (decomp-tracker items 2926/2927/2931/2934/2935/2936/2940/2943/2945/2947).
// ============================================================================

// FUN_06349210 (Common.dll.txt:414508-414590, item 2934, 69 insns). Removes
// `Core` from the specific per-controller bucket keyed by `Controller` within
// `Unit` (backward linear search + shift, same idiom as
// IVP_U_Vector<T>::RemoveElement); if that bucket's own Cores list becomes
// empty, frees the bucket (IVP_U_Vector<T>::Clear() on its Cores buffer, then
// free() the bucket record -- same as IVP_Simulation_Unit_Free_Controller_Bucket
// above) and removes the now-dangling slot from Unit->ControllerBuckets.
// Sole caller: IVP_Core::detach_controller() (ivp_core.cpp, retail
// FUN_06349110/item 2931), which reaches this via Core->OwningSimUnit right
// after removing `Controller` from Core->Controllers.
void IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(IVP_Simulation_Unit* Unit,
                                                              IVP_Controller* Controller,
                                                              IVP_Core* Core);

// FUN_063491d0 (Common.dll.txt:414468-414493, item 2933, 21 insns). The
// insertion-side twin of Detach_Core_From_Controller_Bucket above: ensures a
// bucket keyed by `Controller` exists in `Unit` (create-if-absent, same
// pattern IVP_Simulation_Unit_Rebuild_Controller_Buckets already uses),
// appends `Core` into it, then re-sorts ControllerBuckets by priority (item
// 2935). Sole caller: FUN_06349180 (decomp-tracker item 2932, IVP_Core's
// Controllers.Add() helper, ivp_core.cpp) right after adding `Controller` to
// Core->Controllers -- the exact structural counterpart of
// IVP_Core::detach_controller (item 2931) removing it.
void IVP_Simulation_Unit_Register_Core_With_Controller(IVP_Simulation_Unit* Unit,
                                                        IVP_Controller* Controller,
                                                        IVP_Core* Core);

// FUN_063492c0 (Common.dll.txt:414595-414661, item 2935, 57 insns).
// Insertion-sort of Unit->ControllerBuckets, ordered by each bucket's
// Controller's own vtable slot 5 (`CALL dword ptr[vtbl+0x14]`, ECX=Controller,
// no stack args, returning an int compared with signed `CMP`/`JLE`) --
// confirmed mechanically (a classic adjacent-compare-then-bubble-back
// insertion sort over ControllerBuckets via
// IVP_Simulation_Unit_Swap_Controller_Buckets below) but the retail NAME of
// that comparator is NOT recovered: it is neither of the 2 slots this port's
// IVP_Controller class already exposes (core_is_going_to_be_deleted_event=0,
// get_controller_priority=1, get_associated_controlled_cores=2) -- slot 5 is
// one of the "3 more un-overridden IVP_Controller defaults" ivp_controller.h
// already flags as unmodeled. Dispatched here via a raw vtable-pointer read
// (not added to the IVP_Controller class) rather than guessing a 4th/5th/6th
// slot name. No concrete IVP_Controller subclass exists in this port yet, so
// this method is currently unreachable in practice (ControllerBuckets is
// always empty) -- ported for completeness/correctness the day a real
// subclass exists.
void IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(IVP_Simulation_Unit* Unit);

// FUN_06349350 (Common.dll.txt:414675-414695, item 2936, 15 insns, tiny).
// Swaps ControllerBuckets.elems[i] and ControllerBuckets.elems[j] in place.
// Trivial helper for the insertion sort above.
void IVP_Simulation_Unit_Swap_Controller_Buckets(IVP_Simulation_Unit* Unit, int i, int j);

// FUN_063494f0 (Common.dll.txt:414913-414933, item 2943, 15 insns, tiny). For
// every Core currently in Unit->Cores (backward iteration), calls
// IVP_Core::update_time_of_last_motion() (FUN_063470e0, already ported). Sole
// caller within THIS cluster's own scope is the ensure_in_simulation() wake
// dispatcher (ivp_core.cpp) -- that function's own header comment already
// attributes its loop body to this exact retail function, so this free
// function is provided as the correctly-named, independently-callable form
// for THIS cluster's other 3 callers (FUN_06348f60/FUN_06348fe0/item
// 2950's FUN_06349a60 neighbourhood at Common.dll.txt:06349899, the last of
// which is outside this cluster's own item list). ensure_in_simulation()
// itself is left untouched (not refactored to call this) to avoid touching
// another lane's already-`ready_unverified` file outside this pass's actual
// assignment; the two implementations are intentionally byte-identical twins
// of the same retail function, not independently-invented logic.
void IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(IVP_Simulation_Unit* Unit);

// FUN_06349490's raw retail shape (ECX=SimUnit, stack=Environment*) -- see
// IVP_Core::revive_sleeping_core (ivp_core.cpp, decomp-tracker item 2942)'s
// own header comment, which documents that method as "a convenience wrapper,
// not retail's real shape" around this exact function. Exposed here in its
// real (Unit, Environment) shape for THIS cluster's callers (FUN_06348f60/
// FUN_06348fe0/FUN_06349040, decomp-tracker items 2926/2927/2930), which
// reach a SimUnit+representative-Core pair through an IVP_Controller's
// associated-cores list rather than through a specific `this` Core.
// Intentionally duplicates revive_sleeping_core()'s own while-loop body
// (same reasoning as Update_Cores_Last_Motion_Time above: two independently
// faithful ports of the same retail bytes, not a shared refactor, to avoid
// touching ivp_core.cpp outside this pass's assignment).
void IVP_Simulation_Unit_Revive_If_Sleeping(IVP_Simulation_Unit* Unit, IVP_Environment* Environment);

// FUN_063495e0 (Common.dll.txt:415037-415117, item 2947, 71 insns). Per-PSI
// motion-classification pass over Unit->Cores: for each Core, calls
// IVP_Core::classify_motion_state(Environment->current_time) (FUN_063478f0,
// already ported) and folds the 1/2/3 result into that Core's MovementState
// low byte (`MovementState = (MovementState & ~0xFF) | (Result & 0xFF)`,
// same masked-merge idiom classify_motion_state's own header documents).
// Accumulates `Verdict &= Result` (starting at 3) across all cores; if every
// core reported 3 ("long idle"), transitions the WHOLE unit to the sleeping
// list: for each Core, FUN_0634b5e0 (decomp-tracker item 2991, already
// independently marked `simplified` -- "a separate, real, and currently
// entirely unported group/formation-membership subsystem", NOT part of this
// cluster) is deferred as a documented no-op (IVP_DEFERRED_ convention, see
// ivp_core.cpp/ivp_real_object.cpp for the same pattern), then unlinks Unit
// from the manager, forces State's low byte to exactly 8 ("static/sleeping"),
// and re-pushes it. Returns 1 if the unit went to sleep this call, 0
// otherwise.
int IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(IVP_Simulation_Unit* Unit, IVP_Environment* Environment);

// FUN_063495c0 (Common.dll.txt:415020-415031, item 2946, 10 insns, tiny).
// NOT one of this pass's own 19 assigned items, closed as a direct
// dependency of item 2948 below. Clears+rebuilds Unit's controller buckets,
// rebalances it (splits off any now-disconnected core group), then clears
// State bits 0x300.
void IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(IVP_Simulation_Unit* Unit);

// FUN_063496a0 (.text:063496a0-063499af, thiscall on the unit, RET 8): the
// library's IVP_Simulation_Unit::simulate_single_sim_unit_psi. Called by
// IVP_Sim_Units_Manager_Simulate_Sim_Units_Psi (063499c0) with its
// IVP_Event_Sim and the cores_which_need_calc_next_psi vector.
class IVP_Event_Sim; // ivp_controller.h
void IVP_Simulation_Unit_Process_One_Psi_Step(IVP_Event_Sim* EventSim, IVP_Simulation_Unit* Unit, IVP_U_Vector<IVP_Core>* OutputCoreList);

// (FUN_063499c0 is IVP_Sim_Units_Manager::simulate_sim_units_psi, declared
// with its class above.)

// ============================================================================
// D-cluster-06347f50 pass (2026-09-10): controller-bucket build/merge/split
// side, decomp-tracker Common.dll items 2907/2908/2909/2910/2911/2912/2913/
// 2914/2916/2917 (Common.dll.txt:413186-413794). Rounds out the
// insertion/merge/split half of this class that D72's destructor-path pass
// (above) had left as "NOT ported this pass". See each function's own
// comment in ivp_simulation_unit.cpp for the full evidence trail. All keep
// this file's established "free function taking Unit* as first param" style
// rather than becoming real member methods, matching every function ported
// here so far.
// ============================================================================

// FUN_06348860 (Common.dll.txt:413260-413283, item 2908, 20 insns). Backward
// linear search of Unit->ControllerBuckets for one keyed by Controller --
// existence check only (byte-identical search to
// IVP_Simulation_Unit_Find_Controller_Bucket_Index above, but returns
// bool/found rather than an index; retail keeps them as 2 separate leaves,
// not reproduced as one call here either).
bool IVP_Simulation_Unit_Has_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller);

// FUN_06348900 (Common.dll.txt:413297-413338, item 2910, 33 insns). Allocates
// a fresh IVP_Sim_Unit_Controller_Bucket (operator_new(0x14), see that
// struct's own updated comment above for the now-confirmed Fixed<T,2> Cores
// layout), keys it by Controller, and appends it onto Unit->ControllerBuckets
// (growing via Realloc if at capacity -- same generic Add() the rest of this
// port already uses).
void IVP_Simulation_Unit_Create_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller);

// FUN_063488a0 (Common.dll.txt:413351-413387, item 2909, 30 insns). Re-finds
// the bucket for Controller (retail really does search again here rather
// than reusing a caller's already-known index or bucket pointer -- same
// "faithfully reproduced double search" precedent as
// IVP_Simulation_Unit_Free_Controller_Bucket above), then appends Core into
// that bucket's own Cores vector (growing it if at capacity). Caller must
// ensure a matching bucket already exists (see
// IVP_Simulation_Unit_Rebuild_Controller_Buckets below, its sole caller).
void IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller, IVP_Core* Core);

// FUN_063487f0 (Common.dll.txt:413186-413260, item 2907, 45 insns). Rebuilds
// Unit->ControllerBuckets from scratch by walking every attached Core's own
// Core->Controllers vector: for each (Core, Controller) pair, ensures a
// bucket exists (create-if-absent via the two functions above) and appends
// Core into it. Sole caller of both
// IVP_Simulation_Unit_Has_Controller_Bucket/Create_Controller_Bucket/
// Add_Core_To_Controller_Bucket. NOTE: retail's tail call to FUN_063492c0
// (decomp-tracker item 2935) is NOT reproduced here -- that item is `in_progress`
// under a concurrent lane (d225:cluster-06348f60) as of this pass; wiring it
// in is a follow-up once that lane lands.
void IVP_Simulation_Unit_Rebuild_Controller_Buckets(IVP_Simulation_Unit* Unit);

// FUN_06348b80 (Common.dll.txt:413433-413457, item 2914, 38 insns). Deep-clears
// EVERY bucket in Unit->ControllerBuckets (Bucket->Cores.Clear() + free(Bucket)
// for each, mirroring FUN_06348be0/Bucket-Clear's own generic reuse), then
// frees the ControllerBuckets array itself -- unlike
// IVP_Simulation_Unit_Free_Vector_Buffers (destructor-path helper above,
// which just does the equivalent of ControllerBuckets.Clear() assuming every
// bucket was ALREADY individually freed by the per-core detach loop), this is
// safe to call at ANY time (e.g. mid-life, before a rebuild), not only at
// full unit teardown.
void IVP_Simulation_Unit_Clear_Controller_Buckets(IVP_Simulation_Unit* Unit);

// FUN_06348d80 (.text:06348d80-06348e0d): the unit's destructor body --
// Clear_Controller_Buckets (06348b80), then the ControllerBuckets and Cores
// vector buffers. The caller frees the unit (operator delete) afterwards
// (06348cb0, 06356eef / 06356f05). cmn-ivp3d 2026-10-08: no longer static.
void IVP_Simulation_Unit_Free_Vector_Buffers(IVP_Simulation_Unit* Unit);

// FUN_06348ad0 (Common.dll.txt:413560-413644, item 2913, 70 insns).
// Connectivity check over Unit's current Cores via a union-find rebuild:
// 1) clears every attached Core's MergedCoreWhichReplacedIt; 2) for each
// ControllerBucket, dispatches a virtual call on Bucket->Controller (vtable
// slot at raw offset +0x8, thiscall) to fetch an IVP_U_Vector<IVP_Core>-shaped
// list of the cores THAT controller connects, then unions every pair in that
// list via FUN_063557c0/get_master_core; 3) walks Unit->Cores comparing each
// Core's master against the FIRST Core's master, returning the first
// DIFFERING master found (i.e. "this core's group is no longer connected to
// the rest"), or nullptr if the whole unit is still one connected component.
//
// The vtable slot dispatched in step 2 is called via a raw function-pointer
// cast (NOT IVP_Controller's own C++ virtual dispatch, see ivp_controller.h)
// because the observed slot index (raw offset +0x8, i.e. the 3rd vtable
// entry) does not line up with src/include/common_physys.h's own documented
// PhySys::Controller vtable ordering (which places its structurally-identical
// get_associated_controlled_cores() at a later slot, behind 3 more
// IVP_Controller default virtuals) -- an open discrepancy for a follow-up
// pass to reconcile, not resolved by guessing here. In PRACTICE this branch
// is unreachable in the current port: ivp_controller.h's own class comment
// confirms Core->Controllers is always empty (no concrete IVP_Controller
// subclass registers into it yet in this IVP-internal subsystem), so
// ControllerBuckets is always empty too and this loop never actually
// executes an iteration -- kept `partial` rather than `ready_unverified`
// pending that reconciliation.
IVP_Core* IVP_Simulation_Unit_Find_Disconnected_Core(IVP_Simulation_Unit* Unit);

// FUN_06348960 (Common.dll.txt:413401-413527, item 2911, 104 insns, medium).
// Splits every Core whose get_master_core() equals TargetMaster (and,
// transitively, any further wholly-disconnected group discovered among the
// cores left behind) out of Unit into one or more brand-new
// IVP_Simulation_Units, each pushed onto the same manager
// (TargetMaster->Environment->time_manager) with State forced to 1 ("awake"),
// and rebuilds each new unit's own controller buckets
// (IVP_Simulation_Unit_Rebuild_Controller_Buckets) before returning. See the
// .cpp definition for the precise multi-pass peel-off algorithm (faithfully
// reproduced from retail's own control flow, not simplified).
void IVP_Simulation_Unit_Split_Off_Core_Group(IVP_Simulation_Unit* Unit, IVP_Core* TargetMaster);

// FUN_06348aa0 (Common.dll.txt:413529-413527... see Common.dll.txt:413529-413551,
// item 2912, 17 insns, tiny). One-shot rebalance driver: if
// IVP_Simulation_Unit_Find_Disconnected_Core finds a core whose group is no
// longer connected to the rest of Unit, clears Unit's controller buckets,
// splits that group off (IVP_Simulation_Unit_Split_Off_Core_Group), then
// rebuilds Unit's own controller buckets. No-op if the unit is still fully
// connected.
void IVP_Simulation_Unit_Rebalance(IVP_Simulation_Unit* Unit);

// FUN_06348c10 (Common.dll.txt:413731-413767, item 2916, 30 insns). Transfers
// every Core out of Source->Cores into Unit->Cores (via the existing generic
// Cores.Add()), re-pointing each transferred Core->OwningSimUnit at Unit, then
// unlinks Source from the owning manager list (Source's own memory is NOT
// freed here -- matches retail exactly, the caller below decides Source's
// fate). Manager is read off the LAST transferred Core's own
// Core->Environment->time_manager (matching retail's exact register reuse);
// if Source has zero cores to begin with, retail's own raw pointer math would
// dereference near-null (a latent crash-on-empty precondition never violated
// by its real callers) -- NOT reproduced here, this port just skips the
// unlink call in that case instead of crashing, a safe, documented deviation.
void IVP_Simulation_Unit_Absorb_Cores_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source);

// FUN_06348c60 (Common.dll.txt:413779-413794, item 2917, 11 insns, tiny). The
// "merge 2 simulation units" driver: clears Unit's own controller buckets,
// absorbs every Core from Source into Unit (see
// IVP_Simulation_Unit_Absorb_Cores_From above), then rebuilds Unit's
// controller buckets from the now-merged Cores set. This is the structural
// inverse of IVP_Simulation_Unit_Split_Off_Core_Group above.
void IVP_Simulation_Unit_Merge_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source);
