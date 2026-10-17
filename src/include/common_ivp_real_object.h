// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll's IVP object hierarchy (IVP_Object .. IVP_Real_Object, IVP_Polygon, IVP_Ball).
// Public since 2026-10-14 (claude-code:cmn-s2-phy8; formerly src/IVP/ivp_physics/
// ivp_real_object.h, which now only includes this file): Server.dll reads IVP_Real_Object
// fields directly (StarSystem_MpCollisionFilter 06d06c30: client_data +0xa4, first_child
// +0x1c; the mine push 06cf3c03: core +0x98), and the client's collision filter LAB_005544e0
// reads client_data, so the layout is a cross-DLL contract.

#include <cstdint>
#include "../IVP/ivp_utility/ivp_math.h"
#include "../IVP/ivp_physics/ivp_core.h"
// D-claude-common-ivp pass (2026-09-17): IVP_SurfaceManager used to be a bare
// forward declaration here (class-shape defect 3c from the vtable-
// identification lane). It is now a real, fully documented 11-slot abstract
// class with one concrete default implementation and its static instance --
// see ivp_surface_manager.h. IVP_Ball's constructor below needs the complete
// type (it stores the default instance's address into surface_manager), so
// this is a full include rather than a forward declaration.
#include "../IVP/ivp_physics/ivp_surface_manager.h"

// Forward declarations
class IVP_Environment;
class IVP_Core;
class IVP_Material;
class IVP_OV_Element; // src/IVP/ivp_physics/ivp_ov_element.h -- IVP_Real_Object::ov_element (+0x90).
// src/IVP/ivp_physics/ivp_mindist.h -- the node type IVP_Real_Object::
// synapse_list_head (+0x20) heads. Only a pointer is needed here.
class IVP_Synapse_Real;
class IVP_Controller_Phantom; // src/IVP/ivp_physics/ivp_phantom.h (cmn-ivp3d)
class IVP_Template_Phantom;   // src/IVP/ivp_physics/ivp_phantom.h (cmn-ivp3d)

#pragma pack(push, 4)

// ============================================================================
// IVP_Priority_Pool -- retail's generic fixed-slot priority pool, the 0x14-byte
// object FUN_06362ef0 (ctor) / FUN_06362f70 (dtor) / FUN_06362f90 (Insert,
// decomp-tracker item 3506) / FUN_06363280 (Remove, item 3507) operate on.
//
// EXTRACTED 2026-09-17 (lane claude:w26-simunit) out of IVP_Hull_Manager, which
// embeds one of these at its own +0x20 and which used to be the only place the
// port declared the layout. The extraction is what makes retail's OTHER
// instantiation expressible: IVP_Sim_Unit_Manager::EventPool (ivp_environment.h)
// is a STANDALONE, heap-allocated pool, `operator new(0x14)` + FUN_06362ef0(p,
// 0x10) at Common.dll.txt:063608e2-06360912, with no hull manager around it.
// Proof that the standalone allocation really is this same 0x14-byte layout,
// read off THREE independent sites rather than assumed from the size:
//   * :06360a13 `CMP word ptr [ECX+0x10],0x0`          -> Count   at pool+0x10
//   * :06360a1a/:06360a1f/:06360a24/:06360a27
//       `EDX=[ECX+4]; AX=[ECX+0xe]; SHL EAX,4; ESI=[EAX+EDX+0xc]`
//                                                      -> Entries at pool+0x04,
//                                                         LiveIndex at pool+0x0e,
//                                                         entry stride 0x10,
//                                                         Entry::Payload at +0x0c
//   * :06360b68/:06360b74-:06360b8b (the PSI rebase in LAB_06360ad0)
//       `CX=[EDI+0xe]` then per entry `EAX=[EDI+4]+idx*0x10; CX=[EAX+4];
//        [EAX+8] -= elapsed`                           -> Entry::Next at +0x04,
//                                                         Entry::Key  at +0x08
//     and finally :06360b96-:06360b9e `EAX=[EDX+8]; [EAX+8] -= elapsed`
//                                                      -> KeyThreshold at pool+0x08
// Every one of those matches IVP_Hull_Manager's own +0x20-relative field map
// exactly (hull+0x24 -> pool+0x04, hull+0x28 -> pool+0x08, hull+0x2e ->
// pool+0x0e, hull+0x30 -> pool+0x10), which is the independent confirmation
// that there is ONE pool layout here and not two.
//
// The member NAMES deliberately keep IVP_Hull_Manager's `Pool...` spelling so
// that the two implementations below are literally the same text, and so that
// src/Common/common_physys.cpp (which reads Hull.PoolEntries / Hull.PoolLiveIndex
// / Hull.PoolKeyThreshold directly, and which this lane must not touch) keeps
// compiling unchanged.
// ============================================================================
class IVP_Priority_Pool
{
public:
    // Confirmed retail per-entry shape (Common.dll.txt:06362f90-0636309e,
    // the shared pool's Insert path).
    // ENTRY LAYOUT CORRECTED 2026-09-17 (lane claude:w18-ivp) by porting the
    // pool's Insert (FUN_06362f90, Common.dll.txt:06362f90-0636327b, 247 insns)
    // and Remove (FUN_06363280, :06363280-0636333f, 62 insns) in full instead
    // of only the first ~40 instructions of Insert. The previous reading had
    // Prev at +0x2 and "Pad6" at +0x6; both are wrong. The pool threads TWO
    // separate intrusive doubly-linked lists through every entry:
    //   * the ASCENDING-KEY list at +0x4 (next) / +0x6 (prev), headed by
    //     PoolLiveIndex (+0x0e). Insert's fast path writes `new->+0x4 =
    //     PoolLiveIndex; Entries[PoolLiveIndex].+0x6 = new` (:063630b3/
    //     :063630ce) and Remove unlinks through exactly that pair
    //     (:06363294 reads +0x6 as prev, :0636329a reads +0x4 as next,
    //     :063632b0/:063632bd write them back).
    //   * an EXPRESS ("skip") list at +0x0 (next) / +0x2 (prev), headed by
    //     PoolSortedHead (+0x0c), used only to shortcut the ascending walk.
    //     Insert seeds every fresh entry's +0x0 with 0xfffe (:06363099) as the
    //     "not a member of the express list" tag, promotes a node into it when
    //     the ascending walk took more hops than the express walk saved
    //     (:063631a1-:06363271), and Remove skips the express unlink entirely
    //     when +0x0 still reads 0xfffe (:063632f1).
    struct Entry
    {
        uint16_t ExpressNext; // +0x0: express-list next, or the literal 0xfffe meaning "not in the express list" (Common.dll.txt:06363099 sets it, :063631e1 and :063632f1 both test for exactly 0xfffe).
        uint16_t ExpressPrev; // +0x2: express-list prev; 0xffff == this entry is the express head (Common.dll.txt:0636324e writes the sentinel, :06363301/:0636331b read it).
        uint16_t Next;        // +0x4: ascending-key-list next while in use, free-list "next free" while free (dual-purpose) -- Common.dll.txt:06362f28/06362fba (ctor/Insert free-list), :063630b3/:0636319d (key list), :06363335 (Remove pushes the slot back onto the free list).
        uint16_t Prev;        // +0x6: ascending-key-list prev; 0xffff == this entry is PoolLiveIndex (Common.dll.txt:063630da/:06363199/:06363238 write it, :06363294 reads it).
        float Key;            // +0x8: Insert's `param_2`, stored as a raw float (Common.dll.txt:0636308b/06363093).
        void* Payload;        // +0xc: Insert's `param_1` (06363096) -- a vtable-bearing listener object.
    };
    static_assert(sizeof(Entry) == 0x10, "IVP_Priority_Pool::Entry size");

    uint16_t PoolCapacity = 0;      // +0x00: Common.dll.txt:06362ef7.
    uint16_t PoolFreeHead = 0xffff; // +0x02: 0xffff == free list empty; Common.dll.txt:06362eff (ctor)/06362fa2 (Insert check).
    Entry* PoolEntries = nullptr;   // +0x04: Common.dll.txt:06362f0f (ctor alloc).
    float PoolKeyThreshold = 0.0f;  // +0x08: init'd from the literal bit pattern 0x501502f9 (Common.dll.txt:06362f59).
    uint16_t PoolSortedHead = 0xffff; // +0x0c: 0xffff == empty; Common.dll.txt:06362f4d.
    uint16_t PoolLiveIndex = 0xffff;  // +0x0e: 0xffff == empty; Common.dll.txt:06362f53.
    uint16_t PoolCount = 0;           // +0x10: incremented by Insert (Common.dll.txt:06362f98).
    uint16_t Field_0x12 = 0;          // +0x12: trailing bytes of the 0x14-byte allocation; never read by any pool function.

    IVP_Priority_Pool() {}
    // FUN_06362ef0 (Common.dll.txt:06362ef0-06362f63). `InitialCapacity` is
    // retail's own second argument: 8 from IVP_Hull_Manager's ctor
    // (Common.dll.txt:0635217e), 0x10 from IVP_SimUnitManager_Construct
    // (:063608fb).
    explicit IVP_Priority_Pool(uint16_t InitialCapacity) { Init(InitialCapacity); }
    ~IVP_Priority_Pool() { Destroy(); }

    void Init(uint16_t InitialCapacity);
    // FUN_06362f70 -- frees Entries if non-null.
    void Destroy();

    // FUN_06362f90 (item 3506) / FUN_06363280 (item 3507). Insert returns
    // retail's own return value: the slot index, which callers store so they
    // can Remove it later.
    uint16_t Insert(void* Payload, float Key);
    void Remove(uint16_t Slot);
};
static_assert(sizeof(IVP_Priority_Pool) == 0x14, "IVP_Priority_Pool size must be 0x14 -- retail's own `PUSH 0x14; CALL operator_new` at Common.dll.txt:063608e2");
static_assert(offsetof(IVP_Priority_Pool, PoolCapacity) == 0x00, "IVP_Priority_Pool+0x00");
static_assert(offsetof(IVP_Priority_Pool, PoolFreeHead) == 0x02, "IVP_Priority_Pool+0x02");
static_assert(offsetof(IVP_Priority_Pool, PoolEntries) == 0x04, "IVP_Priority_Pool+0x04");
static_assert(offsetof(IVP_Priority_Pool, PoolKeyThreshold) == 0x08, "IVP_Priority_Pool+0x08");
static_assert(offsetof(IVP_Priority_Pool, PoolSortedHead) == 0x0c, "IVP_Priority_Pool+0x0c");
static_assert(offsetof(IVP_Priority_Pool, PoolLiveIndex) == 0x0e, "IVP_Priority_Pool+0x0e");
static_assert(offsetof(IVP_Priority_Pool, PoolCount) == 0x10, "IVP_Priority_Pool+0x10");

// ============================================================================
// IVP_Hull_Manager (FUN_06352150 ctor / FUN_063521b0 dtor / FUN_063521f0 /
// FUN_06352220, decomp-tracker item 3166, Common.dll.txt:06352150-0635229c).
// Embedded BY VALUE inside IVP_Real_Object at +0x44 (see `hull_manager`
// below), 0x34 (52) bytes.
//
// OUTER-CLASS RESOLUTION (D-hullmanager-outer pass, 2026-09-10): a prior
// pass (D102) left this item `blocked`, flagging FUN_06345100/0x063622bc as
// the unexplored lead needed to identify the embedding outer class. Both
// are now resolved: FUN_06345100 (decomp-tracker item 2790) is IVP_Real_
// Object/IVP_Object's own confirmed real base constructor (see that item's
// notes), and it calls FUN_06352150 directly on `this+0x44`
// (Common.dll.txt:06345140, `LEA ECX,[ESI+0x44]` immediately before the
// CALL -- no allocation, a true by-value embedded member). 0x063622bc
// (Common.dll.txt:456688) turned out to be a dead end, exactly as D102
// feared it might not be: it is just a SIBLING call site of FUN_06345100
// inside FUN_06362280, another near-identical "type-specific second-half
// object constructor" (a twin of FUN_06362570/item 3487, building a
// different IVP_Real_Object subtype -- vtable PTR_FUN_063a5824 vs
// PTR_FUN_063a585c, IVP_Object::object_type tag 2 vs 3), not a new class.
// So the outer class was IVP_Real_Object all along.
//
// INTERNAL LAYOUT: this class embeds exactly one instance of the same
// low-level fixed-slot pool allocator IVP_Complex_Mindist::PriorityPool
// (ivp_mindist_limiter.cpp) already wraps -- its ctor/dtor call the
// IDENTICAL FUN_06362ef0/FUN_06362f70 (Common.dll.txt:06352150-0635219d /
// 063521b0-063521e8) -- but that existing port is explicitly a clean-room,
// NOT-byte-identical stand-in (see its own header comment there), and its
// hardcoded Entry shape (float Key/int Payload/Next/Prev/InUse) does not
// match what THIS class's own accessor functions read from the pool's
// entries (a different per-entry payload: a float Key plus a vtable-
// bearing listener pointer, at different byte offsets -- see `Entry`
// below). Retail's pool allocator (FUN_06362ef0/f70/f90/FUN_06363280) is
// used from 30+ unrelated call sites across the module (confirmed via its
// own XREF list), so it is a genuinely generic, caller-defined-payload
// allocator, not something specific to IVP_Complex_Mindist -- this class
// is simply a second, independent instantiation of it. Full field-by-field
// re-derivation this pass, straight from FUN_06362ef0/FUN_06362f90's own
// raw bytes (Common.dll.txt:06362ef0-06362f63 / 06362f90-0636309e), settles
// which bytes belong to the pool's own bookkeeping vs. to this class
// itself (see field comments below for exact citations).
class IVP_Hull_Manager
{
public:
    // The per-entry shape now lives on IVP_Priority_Pool above (extracted
    // 2026-09-17, lane claude:w26-simunit, so retail's OTHER instantiation of
    // the same allocator can be expressed). This alias keeps every existing
    // `IVP_Hull_Manager::Entry` spelling -- including the ones in
    // src/Common/common_physys.cpp, which this lane must not touch -- valid.
    // Entries read by this class's own WalkActiveEntries/DrainAndShift:
    // Entry::Next at +0x4 (:0635225d/:06352277), Entry::Key at +0x8
    // (06352263-0635226f), Entry::Payload at +0xc, dispatched through vtable
    // slots 2/3 (0635220d/06352274).
    typedef IVP_Priority_Pool::Entry Entry;

    // +0x00..+0x17: RESOLVED 2026-09-17 (lane claude:common-ivp5, decomp-tracker
    // items 3117/3128). This span used to read "8 dwords, all directly zeroed by
    // the real constructor (Common.dll.txt:06352170-06352196). None of these are
    // read by either accessor function or the destructor, so their purpose is
    // otherwise unconstrained" -- true only because the evidence lives OUTSIDE
    // this class's own 4 functions. FUN_06350050 (item 3128, Common.dll.txt:
    // 06350080-063500c7 and its mirrored second half at :063500e8-06350123)
    // reads all six of them off an IVP_Real_Object* at the object-relative
    // offsets +0x44/+0x48/+0x4c/+0x50/+0x54/+0x58 -- i.e. exactly this embedded
    // member's +0x00/+0x04/+0x08/+0x0c/+0x10/+0x14 -- and evaluates, twice per
    // side:
    //     dt          = (Env->current_time.hi - this->LastEditTimeHi)
    //                 + (Env->current_time.lo - this->LastEditTimeLo)
    //     outer(now)  = dt * OuterGradient + OuterValueAtLastEdit
    //     spread(now) = dt * (OuterGradient - InnerGradient)
    //                 + (OuterValueAtLastEdit - InnerValueAtLastEdit)
    // Every one of those operands is reached by an x87 FSUB/FMUL/FADD on the
    // memory operand itself (`FSUB [ESI+0x44]`, `FSUB [ESI+0x48]`,
    // `FMUL [ESI+0x4c]`, `FSUB [ESI+0x50]`, `FADD [ESI+0x54]`,
    // `FSUB [ESI+0x58]`), so all six are FLOATS, not the dwords this class used
    // to declare. This is a linearly-extrapolated OUTER/INNER value pair with a
    // shared split-precision timestamp, NOT the bounding sphere / AABB an
    // earlier blocker guessed at.
    float LastEditTimeHi = 0.0f;  // +0x00: high half of the split-precision "value was last edited at" stamp.
    float LastEditTimeLo = 0.0f;  // +0x04: low half. Differenced against IVP_Environment::current_time's own pair.
    float OuterGradient  = 0.0f;  // +0x08: d(outer value)/dt since LastEditTime.
    float InnerGradient  = 0.0f;  // +0x0c: d(inner value)/dt since LastEditTime.

    // +0x10/+0x14: the outer/inner values sampled AT LastEditTime, which the
    // gradients above extrapolate forward. Three independent write/read sites
    // agree, and they were previously named PendingDeltaA/PendingDeltaB on the
    // strength of only the third:
    //   1. FUN_06350050 (above) adds +0x10 after `dt * OuterGradient` and
    //      differences +0x10 against +0x14 -- an absolute base value, not a
    //      delta.
    //   2. IVP_InitCoreRestStateAndDamping's per-object hull edit (ivp_core.cpp,
    //      Common.dll.txt:06351da2-06351dc9) ZEROES both gradients and then does
    //      `OuterValueAtLastEdit = AccumulatedDeltaA + ExtraMargin` (a plain
    //      assignment of a running absolute total) and
    //      `InnerValueAtLastEdit += DistanceMoved` (the inner hull being eaten
    //      by motion). With both gradients zeroed the extrapolation degenerates
    //      to exactly these two values, which is what (1) then reads back.
    //   3. DrainAndShift below (FUN_06352220) negates both, folds -Outer into
    //      every live entry's Key and into PoolKeyThreshold/AccumulatedDeltaA
    //      (Common.dll.txt:06352263-06352292), hands both negated values to each
    //      entry's vtable-slot-3 callback (06352265/06352274, push order
    //      EBX=-Outer then EBP=-Inner) and then zeroes both. That is a REBASE of
    //      the whole hull-value coordinate system back to zero -- consistent
    //      with (1)/(2), and the reason the old "pending delta" reading looked
    //      plausible in isolation.
    // Names are this port's own; retail's own identifiers were not recovered.
    float OuterValueAtLastEdit = 0.0f; // +0x10
    float InnerValueAtLastEdit = 0.0f; // +0x14
    // +0x18: running total of every DrainAndShift rebase's OuterValueAtLastEdit
    // (only A, never B -- confirmed asymmetry, Common.dll.txt:0635228b-
    // 06352292 has no counterpart write using InnerValueAtLastEdit).
    float AccumulatedDeltaA = 0.0f; // +0x18
    // +0x1c (formerly Field_0x1c): integer time of the next rebase. Read by
    // FILD dword and compared with LastEditTimeHi; when behind, DrainAndShift
    // runs and this becomes ftol(LastEditTimeHi + 10.0f)
    // (hull check inlined in 06351b90 / 06351f80; golem controller :06343f31-:06343f53).
    int32_t TimeOfNextReset = 0;    // +0x1c

    // +0x20..+0x33: the embedded pool (fixed initial capacity 8, see ctor).
    // Field-by-field layout independently re-derived this pass from
    // FUN_06362ef0 (ctor, Common.dll.txt:06362ef0-06362f63) and FUN_06362f90
    // (Insert, :06362f90-0636309e) -- Insert is not one of this class's own
    // 4 functions but shares the SAME pool layout, so tracing it was the
    // only way to settle which bytes are pool-owned vs. class-owned (this
    // resolves an apparent boundary ambiguity an earlier evidence-gathering
    // pass left open: PoolKeyThreshold at +0x28 and PoolCount at +0x30 both
    // genuinely belong to the pool, not to this class directly).
    uint16_t PoolCapacity = 0;         // +0x20: Common.dll.txt:06362ef7.
    uint16_t PoolFreeHead = 0xffff;     // +0x22: 0xffff == free list empty; Common.dll.txt:06362eff (ctor)/06362fa2 (Insert check).
    Entry* PoolEntries = nullptr;        // +0x24: Common.dll.txt:06362f0f (ctor alloc) -- this is the field WalkActiveEntries/DrainAndShift read directly as `this+0x24`.
    float PoolKeyThreshold = 0.0f;        // +0x28: init'd from the literal bit pattern 0x501502f9 (Common.dll.txt:06362f59); compared against each inserted Key (FCOMP, 0636309e) and shifted by DrainAndShift below (06352279-06352288) -- exact semantic purpose (a running priority bound?) not confirmed.
    uint16_t PoolSortedHead = 0xffff;      // +0x2c: 0xffff == empty; Common.dll.txt:06362f4d (ctor). Retail name unconfirmed.
    uint16_t PoolLiveIndex = 0xffff;        // +0x2e: 0xffff == empty; Common.dll.txt:06362f53 (ctor) -- this is the field WalkActiveEntries/DrainAndShift both read directly as `this+0x2e` (their shared iteration cursor).
    uint16_t PoolCount = 0;                  // +0x30: incremented by Insert (Common.dll.txt:06362f98); this is the field WalkActiveEntries/DrainAndShift both read directly as `this+0x30` (their shared loop guard).
    uint16_t Field_0x32 = 0;                  // +0x32: unconfirmed trailing bytes -- outside the pool's own confirmed 0x20..0x31 footprint, never read by any of this class's own 4 functions.

    // The seven fields above ARE an IVP_Priority_Pool, byte for byte; retail
    // calls FUN_06362ef0/f70/f90/FUN_06363280 on `this+0x20` directly
    // (Common.dll.txt:0635217e ctor, :063521d8 dtor). They are kept spelled out
    // here rather than replaced by an `IVP_Priority_Pool Pool;` member only
    // because src/Common/common_physys.cpp reads Hull.PoolEntries /
    // Hull.PoolLiveIndex / Hull.PoolKeyThreshold by those names and is owned by
    // another lane. This accessor is the single bridge, and the static_asserts
    // right after the class pin all seven offsets across the two spellings, so
    // the aliasing cannot silently drift.
    IVP_Priority_Pool& Pool() { return *reinterpret_cast<IVP_Priority_Pool*>(&PoolCapacity); }
    const IVP_Priority_Pool& Pool() const { return *reinterpret_cast<const IVP_Priority_Pool*>(&PoolCapacity); }

    // FUN_06352150 (Common.dll.txt:06352150-063521a7, 30 insns).
    IVP_Hull_Manager();
    // FUN_063521b0 (Common.dll.txt:063521b0-063521e8, ~14 insns).
    ~IVP_Hull_Manager();

    // FUN_063521f0 (Common.dll.txt:063521f0-06352218, 16 insns). While
    // PoolCount > 0, repeatedly RE-READS PoolLiveIndex (never advanced by
    // this function's own body) and dispatches
    // `Entries[PoolLiveIndex].Payload->vtable[2](this)` -- the listener's
    // own handler is expected to mutate PoolLiveIndex/PoolCount as a side
    // effect of processing itself (e.g. removing itself from the pool);
    // this function would spin forever on a listener that does not. Name is
    // this port's own.
    void WalkActiveEntries();

    // FUN_06352220 (Common.dll.txt:06352220-0635229c, 49 insns). See the
    // Outer/InnerValueAtLastEdit field comments above for the full per-entry and tail
    // behavior. Name is this port's own.
    void DrainAndShift();

    // FUN_06362f90 (decomp-tracker item 3506, Common.dll.txt:06362f90-
    // 0636327b, 247 insns) and FUN_06363280 (item 3507, :06363280-0636333f,
    // 62 insns) -- the shared pool allocator's Insert/Remove, ported
    // byte-faithfully against THIS class's own pool layout by lane
    // claude:w18-ivp (2026-09-17). They were previously present only as the
    // clean-room, deliberately NOT-byte-identical IVP_Complex_Mindist::
    // PriorityPool in ivp_mindist_limiter.cpp, which is why every caller of
    // this pool (item 3128/FUN_06350050, item 3333/FUN_0635c130) had to stay
    // `simplified`. That clean-room copy is left alone: it is a SECOND,
    // independent instantiation of the same retail allocator for a different
    // embedding class, and retyping its Entry would be an unrelated change.
    //
    // PoolInsert returns retail's own return value: the slot index, which
    // callers store so they can PoolRemove it later (e.g. IVP_OV_Element::
    // HullPoolSlot). Both are one-line forwarders onto Pool() as of 2026-09-17
    // (lane claude:w26-simunit) -- the bodies moved verbatim to
    // IVP_Priority_Pool::Insert/Remove so there is exactly ONE copy of retail's
    // 247+62 instructions in the port.
    uint16_t PoolInsert(void* Payload, float Key) { return Pool().Insert(Payload, Key); }
    void PoolRemove(uint16_t Slot) { Pool().Remove(Slot); }
};
static_assert(sizeof(IVP_Hull_Manager) == 0x34, "IVP_Hull_Manager size must be 0x34 (52 bytes)");
// The embedded-pool aliasing contract: every IVP_Priority_Pool field must land
// on the identically-named IVP_Hull_Manager field, offset by exactly 0x20.
static_assert(offsetof(IVP_Hull_Manager, PoolCapacity)     == 0x20 + offsetof(IVP_Priority_Pool, PoolCapacity),     "hull/pool alias: PoolCapacity");
static_assert(offsetof(IVP_Hull_Manager, PoolFreeHead)     == 0x20 + offsetof(IVP_Priority_Pool, PoolFreeHead),     "hull/pool alias: PoolFreeHead");
static_assert(offsetof(IVP_Hull_Manager, PoolEntries)      == 0x20 + offsetof(IVP_Priority_Pool, PoolEntries),      "hull/pool alias: PoolEntries");
static_assert(offsetof(IVP_Hull_Manager, PoolKeyThreshold) == 0x20 + offsetof(IVP_Priority_Pool, PoolKeyThreshold), "hull/pool alias: PoolKeyThreshold");
static_assert(offsetof(IVP_Hull_Manager, PoolSortedHead)   == 0x20 + offsetof(IVP_Priority_Pool, PoolSortedHead),   "hull/pool alias: PoolSortedHead");
static_assert(offsetof(IVP_Hull_Manager, PoolLiveIndex)    == 0x20 + offsetof(IVP_Priority_Pool, PoolLiveIndex),    "hull/pool alias: PoolLiveIndex");
static_assert(offsetof(IVP_Hull_Manager, PoolCount)        == 0x20 + offsetof(IVP_Priority_Pool, PoolCount),        "hull/pool alias: PoolCount");
static_assert(offsetof(IVP_Hull_Manager, LastEditTimeHi) == 0x00, "IVP_Hull_Manager+0x00");
static_assert(offsetof(IVP_Hull_Manager, LastEditTimeLo) == 0x04, "IVP_Hull_Manager+0x04");
static_assert(offsetof(IVP_Hull_Manager, OuterGradient) == 0x08, "IVP_Hull_Manager+0x08");
static_assert(offsetof(IVP_Hull_Manager, InnerGradient) == 0x0c, "IVP_Hull_Manager+0x0c");
static_assert(offsetof(IVP_Hull_Manager, OuterValueAtLastEdit) == 0x10, "IVP_Hull_Manager+0x10");
static_assert(offsetof(IVP_Hull_Manager, InnerValueAtLastEdit) == 0x14, "IVP_Hull_Manager+0x14");
static_assert(offsetof(IVP_Hull_Manager, AccumulatedDeltaA) == 0x18, "IVP_Hull_Manager+0x18");
static_assert(offsetof(IVP_Hull_Manager, PoolCapacity) == 0x20, "IVP_Hull_Manager+0x20");
static_assert(offsetof(IVP_Hull_Manager, PoolEntries) == 0x24, "IVP_Hull_Manager+0x24");
static_assert(offsetof(IVP_Hull_Manager, PoolKeyThreshold) == 0x28, "IVP_Hull_Manager+0x28");
static_assert(offsetof(IVP_Hull_Manager, PoolLiveIndex) == 0x2e, "IVP_Hull_Manager+0x2e");
static_assert(offsetof(IVP_Hull_Manager, PoolCount) == 0x30, "IVP_Hull_Manager+0x30");

// Moved here unchanged from ivp_real_object.cpp (cmn-phy5, 2026-10-08) so the
// ray solver can read it (0634a600 reads slot->WorldTransform.vv at +0x4c).
// One slot of the pool above. Fields +0x0/+0x4/+0x8 are confirmed by
// FUN_0634d530 and FUN_06353510's own bytes; +0xc onward is written only by
// FUN_0634d640 (the refresh step, FULLPORT-DEFERRED below) and was not
// otherwise touched by either of the two functions this pass ported for
// real, so it is left as an opaque byte span rather than guessed at.
struct IVP_NarrowPhaseCacheSlot
{
    int32_t          LastRefreshTick; // +0x0: stamped from IVP_Environment::sim_step_count by FUN_0634d640 (confirmed: `MOV [EDI],EAX` at Common.dll.txt:0634d669, EAX==Env->sim_step_count read the line before).
    int32_t          RefCount;        // +0x4: 0 == free/reusable (tested by FUN_0634d530's claim scan); bumped by FUN_06353510/ResolveNarrowPhaseCacheSlot below, decremented by IVP_Real_Object::ReleaseNarrowPhaseCacheSlot below (D-item319x pass, 2026-09-10 -- see that method's own comment for the evidence trail that resolved the old "no decrement site traced" TODO here).
    IVP_Real_Object* Owner;           // +0x8: back-pointer to the cached object.
    // +0xc..+0x1b (16 bytes): d237:common-port pass (2026-09-10). Confirmed a
    // genuine IVP_U_Quat -- this slot's own cached/interpolated orientation,
    // written by IVP_RefreshNarrowPhaseCacheSlot below (either copied
    // verbatim from Core->QWorldFCore when DTime==0, or slerped from
    // Core->QWorldFCore/QWorldFCoreNext otherwise) and then immediately
    // consumed by set_matrix() to build WorldTransform's rotation rows below.
    IVP_U_Quat       Quat;              // +0xc..+0x1b (16 bytes)
    // +0x1c..+0x5b: the slot's cached world transform (an embedded
    // IVP_U_Matrix: rows at +0x1c / +0x2c / +0x3c, vv at +0x4c). Built by
    // IVP_RefreshNarrowPhaseCacheSlot below from Quat; read through the slot
    // transforms 0634d860 / 0634d8d0 / 0634d970 / 0634d9d0 by the
    // IVP_Compact_Ledge_Solver helpers (e.g. calc_pos_other_space 0635ae60,
    // which reaches it as IVP_Cache_Ledge_Point::cache_object + 0x1c) and by
    // the IVP_Mindist_Minimize_Solver walks; minimize_BB (063536d0) and the
    // ball walks read vv directly as the ball centre.
    IVP_U_Matrix     WorldTransform;   // +0x1c..+0x5b (64 bytes): rows[0..2] @ +0x1c/+0x2c/+0x3c, vv (translation) @ +0x4c.
    // +0x5c/+0x60/+0x64: a second 3-float vector. Every minimize walk dots
    // (this side's Secondary - the other side's) with the new
    // IVP_Ledge_Mindist::SeparatingNormal and stores the result in
    // IVP_Ledge_Mindist::SecondaryNormalDot (e.g. 063536d0 :063537fe,
    // 06363620 :06363774). No retail name recovered.
    Vector           Secondary;        // +0x5c..+0x67 (12 bytes)
    char             _pad68[0x70 - 0x68]; // +0x68..+0x6f: still unconfirmed.
};
static_assert(sizeof(IVP_NarrowPhaseCacheSlot) == 0x70, "IVP_NarrowPhaseCacheSlot size mismatch");
static_assert(offsetof(IVP_NarrowPhaseCacheSlot, WorldTransform) == 0x1c, "WorldTransform offset mismatch");
static_assert(offsetof(IVP_NarrowPhaseCacheSlot, Secondary) == 0x5c, "Secondary offset mismatch");

// ============================================================================
// IVP_RealObjectConstructionTemplate -- port-local name (no retail source) for
// the "CreateParms"-shaped struct pointer threaded through IVP_Environment::
// static_object's real construction chain: FUN_0634cdf0/item 3050 ->
// FUN_06362570/item 3487 -> BOTH FUN_06345100/item 2790 (this class's own
// constructor below, which reads +0x00/+0x0c/+0x10/+0x18/+0x54) AND, as a
// confirmed SIBLING call sharing the exact same pointer (not a child call --
// see FUN_06345b20's own header comment in ivp_real_object.cpp for the
// CALLER CORRECTION that established this), FUN_06345b20/item 2817 (which
// reads +0x38/+0x3c/+0x40/+0x44). D-rotinertia-selector pass (2026-09-10):
// +0x1c/+0x20/+0x24..+0x2c/+0x34 are now ALSO modeled (MassInput/
// RotInertiaShapePtr/InertiaBoxVector/RotInertiaClampFactor below), closing
// out FUN_06345b20's own RotInertia/Mass block. Only +0x14 (4 bytes) and
// +0x30 (4 bytes) remain genuine, real, unmodeled gaps -- confirmed off the
// same raw instructions to exist, but never read by either of this struct's
// 2 confirmed consumers, so left as opaque bytes rather than guessed at.
//
// +0x00/+0x04 (LongNamePtr/ShortNameTag): D-hullmanager-outer-wire pass
// (2026-09-10) CORRECTED a prior pass's "+0x4 name-source char*, feeding
// BOTH copies" reading. Direct re-trace of FUN_06345100's own call site
// (Common.dll.txt:06345124 `PUSH EBP` -- EBP holds the raw CreateParms
// pointer itself, offset +0, with NO +4 adjustment before the push) shows
// the template argument passed to FUN_06345610 (IVP_Object::IVP_Object)
// is CreateParms itself, so the name read is CreateParms+0x0,
// not +0x4. The OTHER copy (Common.dll.txt:06345179, `LEA EDX,[EBP+0x4]`,
// feeding IVP_Real_Object::name[8] via strncpy) independently confirms
// +0x4 is a SEPARATE 8-byte embedded field, not a second read of the same
// pointer -- and +0x4 + 8 bytes lands EXACTLY on the already-confirmed
// FlagsHi field at +0xc with zero gap, strong corroborating evidence for
// this corrected two-field reading (the old single-pointer-at-+0x4 model
// left an unaccounted 4-byte gap between +0x8 and +0xc).
struct IVP_RealObjectConstructionTemplate
{
    // +0x00: a "long" display-name pointer. Read unconditionally and strdup'd
    // into IVP_Object::object_name (+0x14) by IVP_Object(Father, Template)
    // (.text:0634562f-0634563e); 06356920 returns null for a null string.
    char* LongNamePtr;

    // +0x04..+0x0b: an embedded 8-byte "short" name/tag buffer, copied
    // (raw bytes, via strncpy, not a further pointer indirection) into
    // IVP_Real_Object::name[8] (Common.dll.txt:06345179-063451b1).
    char ShortNameTag[8];

    uint32_t FlagsHi; // +0x0c: -> IVP_Core::IVP_Core's own FlagsHi param; also tested for zero/nonzero to pick IVP_Real_Object::flags' (+0x78) initial 0x10-vs-0x8 bit pattern (Common.dll.txt:063451fd-06345224).
    uint32_t FlagsLo; // +0x10: -> IVP_Core::IVP_Core's own FlagsLo param (Common.dll.txt:063451d1).

    uint8_t _unknown_0x14[0x18 - 0x14]; // +0x14: real, unread by either of this struct's 2 confirmed consumers -- not guessed at.

    IVP_Material* Material; // +0x18: -> IVP_Real_Object::material (+0x8C), Common.dll.txt:06345227-0634522a.

    // +0x1c..+0x34: D-rotinertia-selector pass (2026-09-10). RotInertia/Mass
    // computation inputs, consumed by FUN_06345b20 (item 2817)'s
    // IVP_InitCoreRestStateAndDamping (ivp_real_object.cpp, Common.dll.txt:
    // 408294-408377). Resolved this pass by a full single-step physical-ESP
    // stack simulation of that function's first ~100 instructions -- see that
    // function's own header comment for the complete trace, including the
    // pass's central finding that the long-unresolved `[ESP+0x1c]` RotInertia
    // shape-selector (Common.dll.txt:408307) is simply Self->object_type,
    // re-read from a stack copy stashed at function entry (Common.dll.txt:
    // 06345b6e, `MOV [ESP+0x1c],EBP` where EBP==object_type) because by the
    // time the selector is needed, the register EBP has been repurposed to
    // point at &Core->RotInertia (Common.dll.txt:06345c67).
    float MassInput; // +0x1c: clamped to a minimum of 1.0f (Common.dll.txt:408294-408301, `MassValue = (MassInput < DAT_063a545c) ? 1.0f : MassInput`) to produce IVP_Core::Mass. DAT_063a545c is a small positive epsilon (~0x3b449ba6), not yet independently named.

    // +0x20: gates which of 2 RotInertia formulas applies (Common.dll.txt:
    // 408303-408306, `TEST EAX,EAX; JZ ...`). NULL -> RotInertia is copied
    // directly from InertiaBoxVector below (Common.dll.txt:408344-408352, no
    // mass/shape scaling at all). Non-NULL -> a further switch on
    // Self->object_type (2 or 3) computes a scaled RotInertia instead (see
    // IVP_InitCoreRestStateAndDamping for the full box-shape/sphere-shape
    // formulas). Only this pointer's nullness is load-bearing here -- it is
    // never dereferenced anywhere in FUN_06345b20, so its own pointee type
    // remains unidentified, per this project's placeholder-field policy.
    // 2026-10-08 (claude-code:cmn-phy3): not a pointer -- the template's own
    // constructor FUN_063486c0 stores the integer 1 here (`MOV [ESI+0x20],1`),
    // so it is a flag (the SDK's auto_check_rot_inertia).
    int32_t AutoCheckRotInertia; // +0x20

    // +0x24..+0x2c: a per-axis "shape factor" Vector, componentwise-multiplied
    // into RotInertia in BOTH the object_type==2 ("box", via
    // Self->surface_manager->vtable[3]) and object_type==3 ("sphere", via a
    // literal 0.4f == 2/5 solid-sphere coefficient, DAT_063a5458) sub-cases
    // (Common.dll.txt:408329-408342), and copied through UNSCALED when
    // RotInertiaShapePtr above is NULL (Common.dll.txt:408344-408352).
    Vector InertiaBoxVector; // +0x24

    uint8_t _unknown_0x30[0x34 - 0x30]; // +0x30: real (read nowhere in FUN_06345b20's own traced body), still unconfirmed -- not guessed at.

    // +0x34: RotInertia post-clamp factor (Common.dll.txt:408354-408377).
    // 0.0f (DAT_063a53d4, the confirmed exact-zero sentinel already used
    // elsewhere in this port, e.g. FUN_063481b0's identity-matrix test)
    // disables the clamp entirely. Otherwise: `Threshold = |Core->RotInertia|
    // * RotInertiaClampFactor` (via the already-ported FUN_06347e60/
    // VectorLength magnitude helper, item 2879), then each of RotInertia's 3
    // components is raised to Threshold if it is currently smaller --
    // a "don't let any single axis' inertia get too small relative to the
    // tensor's own overall magnitude" stability floor.
    float RotInertiaClampFactor; // +0x34

    // +0x38..+0x44: mass/damping inputs, consumed by FUN_06345b20 (item
    // 2817)'s IVP_InitCoreRestStateAndDamping (ivp_real_object.cpp), NOT by
    // this class's own constructor. See that function's own field comments
    // for the citation trail (Common.dll.txt:408377-408390).
    float SpeedDampInput;     // +0x38
    float RotSpeedDampInputX; // +0x3c
    float RotSpeedDampInputY; // +0x40
    float RotSpeedDampInputZ; // +0x44

    uint8_t _unknown_0x48[0x4c - 0x48];

    // +0x4c: D-core-init-tail pass (2026-09-10). Confirmed a genuine float,
    // read by FUN_06362570/item 3487 (Common.dll.txt:063625bf, `FLD float
    // ptr [EDI+0x4c]`, EDI==this Template pointer) and added to a caller-
    // supplied float* (that function's own param_2 -- `&local_c0`, holding
    // the literal 1.0f at the one confirmed call site, Common.dll.txt:
    // 420520) to produce Self->field_94 (see FUN_06362570's own port in
    // ivp_real_object.cpp, IVP_Ball::IVP_Ball). Semantic meaning not identified -- this whole
    // Template struct is a partially-uncharacterized stack buffer at that
    // one call site (see FUN_06362570's own header comment for the "NOT
    // safely resolved" provenance caveat), so only the primitive type/offset
    // are confirmed here, per this project's placeholder-field policy.
    float Field_0x4c; // +0x4c (ends exactly at +0x50, no further padding needed)

    // +0x50: D-item2785-wire pass (2026-09-10). Confirmed a genuine pointer,
    // read by FUN_06345b20/item 2817 itself (Common.dll.txt:06345b46-06345b72,
    // `MOV EAX,[EDI+0x50]; TEST EAX,EAX; JZ ...`) while building the LocalCsys
    // argument for the Self->vtable[2]/LAB_06344de0 call (item 2785, see
    // IVP_Real_Object::ApplyLocalCsys in ivp_real_object.cpp). When non-null,
    // its OWN +0x30/+0x34/+0x38 float triple (Common.dll.txt:06345b74-06345b8b)
    // is copied directly into LocalCsys.vv, bypassing surface_manager entirely
    // -- an explicit caller-supplied center-of-mass/shift override. When null,
    // Self->surface_manager->vtable[1] computes it instead (same unidentified-
    // surface_manager-class blocker as FUN_06345b20's own RotInertia/Mass
    // block). Pointed-to struct's own type/size not identified -- only the
    // +0x30/+0x34/+0x38 sub-offsets are confirmed, per this project's
    // placeholder-field policy.
    void* OptionalComOverride; // +0x50

    void* ClientData; // +0x54: -> IVP_Real_Object::client_data (+0xA4), Common.dll.txt:0634523c-06345240.

    // 2026-10-08 (claude-code:cmn-phy3): the four functions below were ported
    // on a duplicate class named IVP_Material (ivp_material.h) that is now the
    // real material class; they belong to this struct (the environment ctor
    // builds one at 0634c5ff with FUN_063486c0 and passes it to FUN_0634cdf0).

    // FUN_063486c0: `REP STOSD` 0x16 dwords of zero, then +0x14 = 0 again,
    // +0x1c = 1.0f, +0x20 = 1, +0x24/+0x28/+0x2c = 1.0f, +0x34 = 0x3cf5c28f,
    // +0x38..+0x44 = 0x3c23d70a. See ivp_real_object.cpp.
    IVP_RealObjectConstructionTemplate();
    // FUN_06348670 (also reached through thunk 06348710): frees LongNamePtr.
    ~IVP_RealObjectConstructionTemplate();
    IVP_RealObjectConstructionTemplate(const IVP_RealObjectConstructionTemplate&) = delete;
    IVP_RealObjectConstructionTemplate& operator=(const IVP_RealObjectConstructionTemplate&) = delete;

    // FUN_06348720: strncpy(ShortNameTag, Name, 8), null Name clears byte 0.
    void set_short_name(const char* Name);
    // FUN_06348690: free(LongNamePtr), then LongNamePtr = _strdup(Name).
    void set_long_name(const char* Name);
};
static_assert(sizeof(IVP_RealObjectConstructionTemplate) == 0x58, "FUN_063486c0 zero-fills 0x16 dwords");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, ShortNameTag) == 0x04, "IVP_RealObjectConstructionTemplate+0x04");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, FlagsHi) == 0x0c, "IVP_RealObjectConstructionTemplate+0x0c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, FlagsLo) == 0x10, "IVP_RealObjectConstructionTemplate+0x10");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, Material) == 0x18, "IVP_RealObjectConstructionTemplate+0x18");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, MassInput) == 0x1c, "IVP_RealObjectConstructionTemplate+0x1c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, AutoCheckRotInertia) == 0x20, "IVP_RealObjectConstructionTemplate+0x20");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, InertiaBoxVector) == 0x24, "IVP_RealObjectConstructionTemplate+0x24");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, RotInertiaClampFactor) == 0x34, "IVP_RealObjectConstructionTemplate+0x34");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, SpeedDampInput) == 0x38, "IVP_RealObjectConstructionTemplate+0x38");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, Field_0x4c) == 0x4c, "IVP_RealObjectConstructionTemplate+0x4c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, OptionalComOverride) == 0x50, "IVP_RealObjectConstructionTemplate+0x50");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, ClientData) == 0x54, "IVP_RealObjectConstructionTemplate+0x54");

// ============================================================================
// The IVP object hierarchy (cmn-phy7 2026-10-08).
//
//   IVP_Object                   vtbl 063a5450 {06345650 deleting dtor}, 0x1c
//   +- IVP_Cluster               vtbl 063a5454 {06345740 deleting dtor}, 0x20
//   +- IVP_Real_Object_Fast_Static  vtbl 063a544c {06345260}
//      +- IVP_Real_Object        vtbl 063a5440 {063452e0 deleting dtor,
//                                               06344f10, 06344de0}, 0xa8
//         +- IVP_Polygon         vtbl 063a5824 {06362320, 06344f10, 06344de0}
//         +- IVP_Ball            vtbl 063a585c {06362620, 06344f10, 06344de0}
//
// Evidence: 06345100 calls 06345610 (IVP_Object ctor) on `this`, then writes
// 063a544c, constructs the hull manager at +0x44 (06352150, EH state 0 -> 1),
// then writes 063a5440; the EH unwind funclets of that ctor call the
// 06345280 thunk (~IVP_Object) for state 0 and 06345290 (~hull manager +
// ~IVP_Object, i.e. ~IVP_Real_Object_Fast_Static) for state 1. 06345760 (the
// only caller of 06345670) builds the cluster: IVP_Object(env), vtbl
// 063a5454, +0x1c = 0, +0x04 = 1.
// Class names are the IVP SDK's; no RTTI COL survives in Common.dll.
// ============================================================================
class IVP_Cluster;

class IVP_Object
{
public:
    // FUN_06345610 (.text:06345610-06345645, RET 8): vptr 063a5450,
    // init_object_core(Father->environment), Father->add_object(this), then
    // object_name = strdup(*(char**)Template) (06356920 returns 0 for a null
    // string). No null test on Father or Template.
    IVP_Object(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template);

    // FUN_06345670 (.text:06345670-06345686, RET 4): vptr 063a5450,
    // init_object_core(Environment). Only caller: IVP_Cluster's ctor 06345760.
    explicit IVP_Object(IVP_Environment* Environment);

    // FUN_063456b0 (.text:063456b0-063456e5); the deleting dtor in slot 0 is
    // FUN_06345650. Unlinks from the father cluster when there is one
    // (06345820), frees the name (06343fb0) and nulls it, nulls environment.
    virtual ~IVP_Object();

    // FUN_06345690 (.text:06345690-063456a8, RET 4): zeroes +0x10, +0x0c,
    // +0x08, +0x14, +0x04 and stores Environment at +0x18.
    void init_object_core(IVP_Environment* Environment);

    int32_t          object_type;  // +0x04 1 = cluster (06345760), 2 = IVP_Polygon (06362280), 3 = IVP_Ball (06362570)
    IVP_Object*      next_object;  // +0x08 next_in_cluster (toward the oldest child)
    IVP_Object*      prev_object;  // +0x0C prev_in_cluster (toward IVP_Cluster::first_object)
    IVP_Cluster*     parent;       // +0x10 father_cluster; null for the root cluster
    char*            object_name;  // +0x14 strdup'd (06356920), freed by the dtor
    IVP_Environment* environment;  // +0x18
};
static_assert(sizeof(IVP_Object) == 0x1c, "IVP_Object is 0x1c bytes");

class IVP_Cluster : public IVP_Object
{
public:
    // FUN_06345760 (.text:06345760-06345784, RET 4). The only caller is the
    // cluster manager's ctor 0634ad70 (the root cluster).
    explicit IVP_Cluster(IVP_Environment* Environment);

    // FUN_06345790 (.text:06345790-063457ed); deleting dtor FUN_06345740.
    // While first_object is set, calls its slot 0 with 1 (delete); every
    // child's ~IVP_Object unlinks itself, which advances first_object.
    ~IVP_Cluster() override;

    // FUN_063457f0 (.text:063457f0-06345811, RET 4): push Object at the head.
    void add_object(IVP_Object* Object);

    // FUN_06345820 (.text:06345820-06345846, RET 4): unlink Object. Does not
    // clear Object->parent.
    void remove_object(IVP_Object* Object);

    IVP_Object* first_object; // +0x1c
};
static_assert(sizeof(IVP_Cluster) == 0x20, "IVP_Cluster is 0x20 bytes (operator new(0x20) at .text:0634ad98)");

// The intermediate layer (vtbl 063a544c). Its ctor is inlined into 06345100
// (.text:06345131-06345140): IVP_Object(Father, Template), vptr, then the hull
// manager member (06352150). Its dtor is FUN_06345290 (~hull manager 063521b0,
// then ~IVP_Object 063456b0), reached only from the EH unwind funclets
// 06397abb / 06397b03. Its vtable's only slot, FUN_06345260, deletes after
// ~IVP_Object alone; no object of this exact type is ever created, so that
// slot is never dispatched.
class IVP_Real_Object_Fast_Static : public IVP_Object
{
public:
    IVP_Real_Object_Fast_Static(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template);

    // +0x1c: the object's IVP_Controller_Phantom (convert_to_phantom
    // 06345f10, deleted by ~IVP_Real_Object at .text:06345326-06345345).
    // Name kept from the port's earlier flattened layout (many users).
    void* first_child; // +0x1C

    // +0x20: IDENTIFIED 2026-09-17 (lane claude:w26-simunit). Was `void*
    // field_20`. Head of this object's intrusive "every synapse attached to me"
    // list. The list is threaded through the SYNAPSE BASE (an IVP_Ledge_Mindist's
    // own +0x18 or +0x34 slot), so the stored pointer is a mindist + 0x18 or
    // + 0x34, never the mindist itself. Two independent sites, both read in
    // full this pass:
    //   * FUN_0634fec0 (item 3125, Common.dll.txt:0634ff06-0634ff2b and
    //     :0634ff2b-0634ff50) unlinks a mindist from BOTH of its slots and, on
    //     the "no prev" branch, writes the successor back to the object's own
    //     +0x20: `EAX=[ESI+0x28]; ECX=[ESI+0x20]; MOV [EAX+0x20],ECX`
    //     (:0634ff22-:0634ff28) for slot A, and the symmetric
    //     `EDX=[ESI+0x44]; EAX=[ESI+0x3c]; MOV [EDX+0x20],EAX`
    //     (:0634ff47-:0634ff4d) for slot B. ESI+0x28/+0x44 are ObjectA/ObjectB.
    //   * FUN_063454b0 (Common.dll.txt:063454b7-063454d4) walks it:
    //     `EAX=[ECX+0x20]` then per node `MOVSX ECX,word[EAX+0x18]; ADD ECX,EAX`
    //     (node + a self-relative int16 delta == the owning mindist) and
    //     `ESI=[EAX+0x8]` for the next node -- which independently pins Next at
    //     node+0x08, the same offset FUN_0634fec0 writes.
    IVP_Synapse_Real* synapse_list_head; // +0x20
    // +0x24: head of this object's list of synapses whose mindist is filed as
    // "invalid". FUN_0634e960 (.text:0634e98d-0634e9ca) pushes both synapses
    // of a mindist onto ObjectA/ObjectB's +0x24 with the same Next(+0x8)/
    // Prev(+0xc) threading as synapse_list_head (cmn-phy5, 2026-10-08; was
    // `void* field_24`).
    IVP_Synapse_Real* invalid_synapse_list_head; // +0x24
    void*               mindist_list;     // +0x28
    IVP_U_Quat*         q_core_f_object;  // +0x2C
    IVP_U_Point         shift_core_f_object; // +0x30 (16 bytes)
    // +0x40: D-item3192-leaves pass (2026-09-10). RETYPED from a bare
    // int32_t placeholder -- confirmed a lazily-populated narrow-phase
    // cache-slot pointer, not a plain integer. Written only by
    // IVP_AllocateCacheObject (FUN_0634d530, Common.dll.txt:0634d530-
    // 0634d5b5, `MOV [EAX+0x40],ESI` at 0634d595 where EAX==this and
    // ESI==the freshly claimed slot) and read only by
    // ResolveNarrowPhaseCacheSlot (FUN_06353510, ivp_real_object.cpp) --
    // see that method's comment for the full evidence trail, and
    // IVP_NarrowPhaseCacheSlot's own comment above for the slot layout.
    // Cleared by IVP_Real_Object_Free_Cache_Object (0634d510). Formerly
    // `void* field_40`.
    IVP_NarrowPhaseCacheSlot* cache_object; // +0x40
    IVP_Hull_Manager    hull_manager;     // +0x44 (0x34 bytes) -- see IVP_Hull_Manager's own class comment above for the full evidence trail (decomp-tracker item 3166).
};

struct IVP_Anchor;

// Port consumer contract for anchor teardown; no native actuator hierarchy is claimed.
class IIVPAnchorEvents
{
public:
    virtual void AnchorWillBeDeleted(IVP_Anchor* Anchor) = 0;

protected:
    ~IIVPAnchorEvents() = default;
};

// One node of IVP_Real_Object::anchors (+0x7c): a point an actuator holds on
// the object. Layout read off ApplyLocalCsys (06344e0a-06344e28: next at
// +0x00, object-space point +0x0c transformed into the core-space point
// +0x1c) and the object's teardown (063455c7-063455e6 -> 06356910: the
// interface at +0x2c gets vtable slot 7 with the node). Name and layout match
// the IVP SDK's IVP_Anchor; +0x04/+0x08 are not read by any traced code.
struct IVP_Anchor
{
    IVP_Anchor* anchor_next_in_object; // +0x00
    uint32_t    Field_0x04;            // +0x04 TODO (unread here)
    uint32_t    Field_0x08;            // +0x08 TODO (unread here)
    IVP_U_Point object_pos;            // +0x0c
    IVP_U_Point core_pos;              // +0x1c
    IIVPAnchorEvents* l_actuator;       // +0x2c
};
static_assert(offsetof(IVP_Anchor, object_pos) == 0x0c, "IVP_Anchor+0x0c");
static_assert(offsetof(IVP_Anchor, core_pos) == 0x1c, "IVP_Anchor+0x1c");
static_assert(offsetof(IVP_Anchor, l_actuator) == 0x2c, "IVP_Anchor+0x2c");

// IVP_Real_Object (0xA8 / 168 bytes), vtbl 063a5440.
class IVP_Real_Object : public IVP_Real_Object_Fast_Static
{
public:
    // FUN_06345100 (.text:06345100-06345259, RET 0x14). Father is the cluster
    // the object is created into (IVP_Polygon / IVP_Ball pass it through from
    // IVP_Environment_CreatePolygon / CreateBall, the root cluster).
    IVP_Real_Object(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                    const IVP_RealObjectConstructionTemplate* Parms,
                    const IVP_U_Quat* InitialOrientationOrNull,
                    const IVP_U_Point* InitialPosition);

    // FUN_06345300 (.text:06345300-06345447); deleting dtor FUN_063452e0
    // (slot 0 of 063a5440).
    ~IVP_Real_Object() override;

    uint32_t            flags;            // +0x78 (IVP_OBJECT_LOCATION / IVP_MOVEMENT_STATE flags)
    IVP_Anchor*         anchors;          // +0x7C (formerly void* field_7c): this object's anchor list
    IVP_SurfaceManager* surface_manager;  // +0x80
    char                name[8];          // +0x84 (8-byte fixed string)
    IVP_Material*       material;         // +0x8C
    // +0x90: IDENTIFIED 2026-09-17 (lane claude:w18-ivp). Was `void* field_90`.
    // This is the object's IVP_OV_Element -- the record that represents the
    // object inside the environment's spatial-subdivision (OV) tree. Proof
    // chain, all in Common.dll.txt:
    //   * FUN_0634f180 (item 3113, :0634f19d-:0634f1e9) reads this+0x90, calls
    //     its vtable slot 4 with arg 1 (the scalar deleting destructor, which
    //     is FUN_0635c070 at .rdata:063a5690), then `operator new(0x30)` and
    //     FUN_0635c020(newObj, this) -- so the allocation is exactly 0x30
    //     bytes and FUN_0635c020 is its constructor.
    //   * FUN_0635c020 (:0635c024-:0635c060) stores the OWNING object back
    //     into the new record's +0x24 and initialises the record's own
    //     0x10-slot IVP_U_Vector at +0x28.
    //   * FUN_0635c090 (item 3332, the real destructor) unlinks it from the
    //     owner's hull manager pool (:0635c0c8) and from the OV tree
    //     (:0635c0e8, FUN_0635d2e0 on Environment->+0x14).
    // See src/IVP/ivp_physics/ivp_ov_element.h for the full class.
    IVP_OV_Element*     ov_element;       // +0x90
    // +0x94: D-ivpenv-staticobject pass (2026-09-10). RETYPED from a bare
    // void* placeholder -- confirmed FLOAT by FUN_06362570 (Common.dll.txt:
    // 457092-457097, `FLD [name_holder+0x4c]; FADD [param2]; FSTP [this+0x94]`,
    // part of IVP_Environment's own static_object build chain, decomp-tracker
    // item 3487).
    //
    // CORRECTED [2026-09-10, D-item2817 dedicated pass]: an earlier version
    // of this comment claimed FUN_06345b20 (item 2817) "zeroes it again
    // shortly after", making this value transient. That was a misread --
    // FUN_06345b20 sets ESI = this->core (Common.dll.txt:408262, `MOV
    // ESI,[EBX+0x98]`) once, near its own midpoint, and ESI stays bound to
    // that IVP_Core* for the REST of the function; every `[ESI+...]` write
    // after that point (including the +0x94/+0x98/+0x9c/+0xa4/+0xa8/+0xac
    // zero-fill at Common.dll.txt:408392-408399) targets IVP_Core::RotSpeed/
    // Speed (ivp_core.h, same +0x94/+0xA4 offsets -- an ordinary "new rigid
    // body starts at rest" init), NOT this field. This object's OWN +0x94 is
    // read exactly once in the whole function (Common.dll.txt:408284, `FLD
    // float ptr [EBX+0x94]`, EBX=this here) as a real input added into
    // IVP_Core::UpperLimitRadius -- see IVP_InitCoreRestStateAndDamping() in
    // ivp_real_object.cpp for the full trace. So this field is NOT transient:
    // it is a genuine, load-bearing per-object bias term for the owning
    // core's angular-speed limit. Its semantic name is still not assigned.
    // CORRECTED [2026-09-10, D-core-init-tail pass]: the source operand was
    // misread as "[name_holder+0x4c]" by an earlier pass -- it is actually
    // `Template->Field_0x4c` (EDI at that point in FUN_06362570 is the SAME
    // Template pointer passed through to IVP_InitCoreRestStateAndDamping,
    // not a separate "name_holder" local; see IVP_RealObjectConstructionTemplate's
    // own Field_0x4c comment above). Only the primitive type and real usage
    // are confirmed, per this project's placeholder-field policy.
    float               field_94;         // +0x94
    IVP_Core*           core;             // +0x98 (Physical core)
    IVP_Core*           original_core;    // +0x9C
    IVP_Core*           field_a0;         // +0xA0
    void*               client_data;      // +0xA4 (CObject* back-pointer in Freelancer)

    void get_m_world_f_object_at_transform(IVP_Time Time, IVP_U_Matrix* OutMatrix);
    // FUN_063456f0: convenience overload using environment's current simulation time.
    void get_m_world_f_object_AT(IVP_U_Matrix* OutMatrix);

    // FUN_063459c0: like get_m_world_f_object_at_transform, but returns orientation
    // as a separate quaternion and position as a separate point instead of a packed matrix.
    void calc_at_time(IVP_Time Time, IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition);
    // FUN_06345710: convenience overload using environment's current simulation time.
    void calc_at_time_AT(IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition);

    // FUN_063454e0 (cmn-phy8): forces the core's movement-state byte to 0x21,
    // re-files this object's OV element (IVP_Mindist_Manager::RecheckOvElement
    // 0634ebd0), then restores the byte. Port-local name (behaviour); no OV
    // element test, unlike recheck_collision_filter 06345530. Body in
    // ivp_real_object.cpp.
    void recheck_ov_element_as_moving();

    // ---- claude-code:cmn-ivp3d block (2026-10-08): contact points, collision
    // detection and phantoms. Bodies in ivp_real_object.cpp under the same
    // banner; IVP_Controller_Phantom is in ivp_phantom.h. ----

    // FUN_06345550 (.text:06345550-063455b8, RET 4), the SDK's
    // unlink_contact_points (was the empty stub DeleteAllAnchors): removes
    // every contact-point record on this object's list (+0x28) from its
    // friction system, waking both cores first unless bSilently.
    void unlink_contact_points(bool bSilently);

    // FUN_06344d50 (.text:06344d50-06344db6, RET 4), the SDK's
    // enable_collision_detection: builds the OV element (mindist manager
    // 0634f180) and sets flag 0x100, or deletes it, clears 0x300 and unlinks
    // the contact points silently.
    void enable_collision_detection(bool bEnable);

    // FUN_06345f10 (.text:06345f10-06345f86, RET 4), the SDK's
    // convert_to_phantom: replaces any phantom in first_child (+0x1c) by a
    // new IVP_Controller_Phantom(this, Template) and returns it.
    IVP_Controller_Phantom* convert_to_phantom(const IVP_Template_Phantom* Template);

    // FUN_06345eb0 (.text:06345eb0-06345f0a, plain RET, returns 1): turns a
    // phantom back into an ordinary object -- collision detection off, the
    // phantom deleted, movement byte 9 -> 0x10, collision detection back on
    // when it was enabled (flags bits 8-9 == 01) before. Port-local name; the
    // SDK name was not recovered.
    int32_t convert_phantom_to_object();

    // FUN_063454b0 (.text:063454b0-063454d8): recalc_exact_mindist (06350460)
    // of the environment's mindist manager on the mindist of every synapse
    // in synapse_list_head (+0x20); the next link is read before each call.
    void recalc_exact_mindists();

    // FUN_06345460 (.text:06345460-063454ac): for every synapse in
    // invalid_synapse_list_head (+0x24), the mindist is re-solved (06352b90)
    // and, unless StateFlags & 0xc000 == 0x4000, moved from the invalid to the
    // exact list (0634ffa0, then 0634e8a0).
    void recalc_invalid_mindists();

    // ---- cmn-phy6 block (2026-10-08). Bodies in ivp_real_object.cpp under the
    // same banner. ----

    // FUN_06344dc0 (.text:06344dc0-06344dd1): if the core's movement byte
    // (+0x60) is exactly 8, tail-jump to ReviveOriginalCoreIfSleeping (063450e0).
    void ensure_in_simulation();

    // FUN_06344fb0 (.text:06344fb0-063450cb, plain RET): put the object's core
    // to sleep now. Port-local name (SDK name not recovered). Called by PhySys
    // event_pre_collision (0628c566 / 0628c58c).
    void disable_simulation();

    // FUN_06344f50 (.text:06344f50-06344fa5): for every synapse in
    // synapse_list_head (+0x20) whose two objects' cores carry different
    // +0x1b8 stamps, re-run the mindist's DispatchNarrowPhaseSolve (06352cf0)
    // and, unless StateFlags & 0xc000, reschedule it (0634f320(m, 0, 2)).
    // Port-local name. Callers 06343c72 (core split) and 06347c02.
    void recheck_mindists_of_other_cores();

    // FUN_06345f90 (.text:06345f90-06346075, RET 0xc), the SDK's
    // beam_object_to_new_position: moves the core so that this object ends
    // up at RotationWs / PositionWs, via IVP_Calc_Next_PSI_Solver::
    // set_transformation (06351b90). Callers PhySys 06285047 / 0628b72a and
    // 0634313d.
    void beam_object_to_new_position(const IVP_U_Quat* RotationWs, const IVP_U_Point* PositionWs, int32_t bOptimizeForRepeatedCalls);
    // ---- end cmn-phy6 block ----

    // FUN_06346170 (.text:06346170-06346190): movement byte (+0x78) == 8 ->
    // environment->add_revive_core(original_core) (0634bb90), else
    // core->update_time_of_last_motion() (063470e0).
    void EnsureInSimulation();

    // ---- cmn-physys4 block (2026-10-08). Bodies in ivp_real_object.cpp under the
    // same banner; IVP SDK names where the SDK has the method. ----

    // FUN_06344d30 (.text:06344d30-06344d40, RET 4): core->set_mass (06347c10).
    // Caller PhySys::SetMass 062841fa.
    void change_mass(float NewMass);

    // FUN_06345530 (.text:06345530-0634554c, plain RET): with an OV element
    // (+0x90), core->Environment->mindist_manager->RecheckOvElement(this)
    // (0634ebd0). Caller PhySys::SetCollisionGroup 06284e42 (tail jump).
    void recheck_collision_filter();

    // FUN_06345e60 (.text:06345e60-06345e91, RET 4): *Out = core->MWorldFCore.vv
    // (x, y, z; Out->hesse untouched). Port-local name. Caller
    // PhySys::GetClosestPhantomIntruderPos 0628c0a5.
    void get_core_position_ws(IVP_U_Point* Out) const;

    // FUN_06345ea0 (.text:06345ea0-06345ea9, plain RET): core->UpperLimitRadius
    // (+0x04) returned in ST0. Callers PhySys_GetRadius 062843f6 and
    // PhySys_CalcClosestPointOnSynapse 0628bd4f.
    float get_upper_limit_radius() const;

    // FUN_06346080 (.text:06346080-063460b8, RET 4): EnsureInSimulation, then
    // core->SpeedChange (+0x84) += *SpeedWs. Callers PhySys::AddToVelocity
    // 06285767, 0636143f.
    void async_add_speed_object_ws(const IVP_U_Point* SpeedWs);

    // FUN_063460c0 (.text:063460c0-063460f6, RET 4): EnsureInSimulation, then
    // core->RotSpeedChange (+0x74) += *RotSpeedCs. Callers
    // PhySys::AddToAngularVelocityOS 06285797, 063610af, 06361119.
    void async_add_rot_speed_object_cs(const IVP_U_Point* RotSpeedCs);

    // FUN_06346100 (.text:06346100-0634616c, RET 8): EnsureInSimulation; the
    // core's interpolated matrix at environment->current_time (06346890) takes
    // PositionWs (vimult4, 06348470) and ImpulseWs (vimult, 06366010) into core
    // space; core->apply_impulse_at_point(PosCs, ImpulseCs, ImpulseWs)
    // (06346c00). Callers PhySys::AddImpulseAtPoint 06285674, 0636108b, 063610f5.
    void async_push_object_ws(const IVP_U_Point* PositionWs, const IVP_U_Point* ImpulseWs);
    // ---- end cmn-physys4 block ----

    // FUN_063450e0 (Common.dll.txt:406987-406999, decomp-tracker item 2789).
    // RE-ATTRIBUTED 2026-09-09 (was incorrectly pointing at
    // IVP_Core::revive_sleeping_core -- see ivp_core.cpp's comment above that
    // method for the full evidence trail). This `this` is unambiguously
    // IVP_Real_Object* (this->flags at +0x78, this->original_core at +0x9C,
    // both already-documented fields above -- no offsetof static_assert exists
    // for this class yet, only sizeof), not IVP_Core*. Tiny
    // 8-insn gate: if (flags&0xFF)==8, forward into original_core's own
    // sim-unit wake pass.
    void ReviveOriginalCoreIfSleeping();

    // FUN_06353510 (Common.dll.txt:06353510-06353559, decomp-tracker item
    // 3196, 25 insns). D-item3192-leaves pass (2026-09-10): lazily resolves
    // this object's narrow-phase cache slot (see field_40 above and
    // IVP_NarrowPhaseCacheSlot's comment in ivp_real_object.cpp for the
    // full evidence trail), bumps its refcount, and conditionally
    // refreshes it. Opaque return type: the slot layout is a file-local
    // detail of ivp_real_object.cpp.
    struct IVP_NarrowPhaseCacheSlot* ResolveNarrowPhaseCacheSlot();

    // The library's get_cache_object_no_lock (cmn-ivp2b, 2026-10-08): the
    // same lazy resolve + conditional refresh as ResolveNarrowPhaseCacheSlot
    // WITHOUT the refcount bump. No own retail function: inlined twice in
    // FUN_0634e0b0 (.text:0634e18d-0634e1d1 / 0634e2a8-0634e2ec).
    struct IVP_NarrowPhaseCacheSlot* get_cache_object_no_lock();

    // D-item319x pass (2026-09-10): the matching decrement for the refcount
    // ResolveNarrowPhaseCacheSlot bumps above. Not a separate retail
    // function -- every one of the 3 real DAT_0658ab4c narrow-phase dispatch
    // handlers (FUN_06353370/FUN_06353560/FUN_063536d0, decomp-tracker items
    // 3195/3197/3200) ends its shared tail with a plain
    // `DEC dword ptr [Slot+0x4]` on each side's slot (e.g. Common.dll.txt:
    // 06353826/0635382c for FUN_063536d0's own two sides). This resolves
    // ivp_real_object.cpp's own "no decrement site traced this pass -- open
    // TODO" note on IVP_NarrowPhaseCacheSlot::RefCount.
    void ReleaseNarrowPhaseCacheSlot(struct IVP_NarrowPhaseCacheSlot* Slot);

    // LAB_06344de0 (Common.dll.txt:06344de0-06344f06, decomp-tracker item
    // 2785, 100 insns / 297 bytes). D-item2785-wire pass (2026-09-10):
    // IVP_Real_Object's real, concrete vtable slot 2 -- confirmed via a full
    // .rdata vtable dump by the immediately-prior pass (see item 2817's own
    // notes) to be the target of the unconditional `Self->vtable[2](&LocalCsys)`
    // call inside FUN_06345b20/IVP_InitCoreRestStateAndDamping below. "Apply a
    // new LOCAL (object-to-core) coordinate system": recomputes
    // q_core_f_object/shift_core_f_object (this object's fixed offset from its
    // physical IVP_Core, e.g. a turret mounted off-center on a ship's core),
    // refreshes every cached mindist-list transform via the just-inverted
    // matrix, and invalidates the narrow-phase cache slot. NewLocalCsys is a
    // genuine `const IVP_U_Matrix*` (this port's own class, NOT Freelancer's
    // tightly-packed `class Csys`) -- see ivp_real_object.cpp for the full
    // derivation that settled this (cross-checked against FUN_063481b0/item
    // 2889's own identity-matrix-builder byte pattern).
    // Declared below SetLocalCsysFromQuatAndPosition so the two virtuals take
    // slots 1 and 2 in retail's order (063a5444 = 06344f10, 063a5448 = 06344de0).

    // LAB_06344f10 (Common.dll.txt:06344f10-06344f4c, decomp-tracker item
    // 2786, 22 insns, kind=vtable_method). IVP_Real_Object's real, concrete
    // vtable slot 1 -- confirmed via the same PTR_FUN_063a5444/PTR_FUN_063a5828/
    // PTR_FUN_063a5860 .rdata XREF triple item 2785's own vtable dump used to
    // settle ApplyLocalCsys (slot 2). Builds a full IVP_U_Matrix from a
    // quaternion (via the already-generic IVP_U_Quat::set_matrix) plus an
    // explicit translation, then forwards to ApplyLocalCsys through slot 2
    // (this->vtable[2] in retail, a virtual call here). All 3 callees fully
    // ported: IVP_U_Quat::set_matrix (ivp_math.h), ApplyLocalCsys.
    virtual void SetLocalCsysFromQuatAndPosition(const IVP_U_Quat* Orientation, const IVP_U_Point* Position); // slot 1
    virtual void ApplyLocalCsys(const IVP_U_Matrix* NewLocalCsys);                                         // slot 2

    // FUN_063455c0 (Common.dll.txt:063455c0-0634560c, decomp-tracker item
    // 2802, 33 insns). D-cluster-ivp-adjacent pass (2026-09-10): destructor
    // phase-2 helper, sole caller is ~IVP_Real_Object (FUN_06345300/item
    // 2795) immediately after the hull_manager WalkActiveEntries broadcast.
    // Drains the anchors list (+0x7c) via the already-ported
    // FUN_06356910 notify trampoline (retail expects each listener to
    // unlink itself from the anchors list as a side effect -- an infinite-loop
    // safety net, the same assert-fail-on-stuck-head idiom already used by
    // IVP_Hull_Manager::WalkActiveEntries' own retail sibling), calls
    // unlink_contact_points(true), then dispatches vtable slot 4 on every node in
    // field_20's own listener chain (same self-unlinking-listener
    // assumption, raw vtable dispatch since that node's class is not
    // identified anywhere in this port -- matches this file's existing
    // convention for unidentified interface candidates).
    void TeardownMindistListenersAndCandidates();
};

// ============================================================================
// The two concrete IVP_Real_Object leaves (class-shape defect 3b from the
// vtable-identification lane; D-claude-common-ivp pass, 2026-09-17). Before
// this pass the port instantiated IVP_Real_Object directly and neither leaf
// existed, so `object_type` 2 and 3 were bare magic numbers set by nobody.
//
// Retail's evidence, identical in shape for both -- each is a "second-half"
// constructor that CALLs the shared base constructor FUN_06345100 with the
// same five arguments, then installs its OWN 3-slot vtable at [this+0] and
// tags [this+4] (IVP_Object::object_type):
//
//   FUN_06362280  (Common.dll.txt:456652-456720)  vtable 063a5824, type 2
//   FUN_06362570  (Common.dll.txt:457067-457131)  vtable 063a585c, type 3
//
// Byte-extent-checked vtable geometry (Common.dll.txt:569364-569383), both
// 3 slots -- the same shape as IVP_Real_Object's own 063a5440 table, with
// only slot 0 (the scalar-deleting destructor) overridden and slots 1 and 2
// inherited verbatim:
//
//   .rdata:063a5824  IVP_Polygon  {FUN_06362320, LAB_06344f10, LAB_06344de0}
//   .rdata:063a585c  IVP_Ball     {FUN_06362620, LAB_06344f10, LAB_06344de0}
//   .rdata:063a5440  IVP_Real_Object {FUN_063452e0, LAB_06344f10, LAB_06344de0}
//
// NAMING CAVEAT: "IVP_Polygon" and "IVP_Ball" come from the vtable-
// identification lane's report and match the upstream IVP/Ipion class names
// for the object_type 2 / 3 pair. No retail symbol survives to confirm
// either name from this module's own listing. What IS proven from the
// listing is the shape: exactly two leaves, type tags 2 and 3, and type 3
// alone carries a radius input and the default surface manager -- which is
// consistent with the sphere/polyhedron split those names describe. TODO:
// re-check if an RTTI COL or an exported symbol for either turns up.
//
// LAYOUT: neither adds a member. Their destructors are implicit: 06362320
// (Polygon slot 0) deletes after ~IVP_Real_Object (thunk to 06345300), and
// 06362620 (Ball slot 0) runs 06362640 (vptr 063a585c, then JMP 06345300)
// before deleting -- both are what the compiler generates for an empty
// derived destructor.
// ============================================================================

// FUN_06362280 (Common.dll.txt:456652-456720, 48 insns). object_type == 2.
// Sole caller: 0634cdd5.
class IVP_Polygon : public IVP_Real_Object
{
public:
    // Argument roles are identical to the base constructor's -- retail
    // forwards all five straight through (Common.dll.txt:063622b1-063622bc,
    // `PUSH EAX/ECX/EDI/EDX/EBX` in reverse-argument order ahead of
    // `CALL FUN_06345100`).
    IVP_Polygon(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                const IVP_RealObjectConstructionTemplate* Parms,
                const IVP_U_Quat* InitialOrientationOrNull,
                const IVP_U_Point* InitialPosition);
};
static_assert(sizeof(IVP_Polygon) == 0xA8, "IVP_Polygon must not change IVP_Real_Object's layout");

// FUN_06362570 (Common.dll.txt:457067-457131, 47 insns). object_type == 3.
// Sole caller: 0634ce45.
class IVP_Ball : public IVP_Real_Object
{
public:
    // NOTE the second argument's different role from IVP_Polygon's: retail
    // pushes a literal 0 as the base constructor's SurfaceManager argument
    // (Common.dll.txt:063625a0, `PUSH 0x0`) and instead dereferences this
    // parameter as a float, adding it to Parms->Field_0x4c
    // (Common.dll.txt:063625ae-063625d1, `MOV EDX,[ESP+param_2]; FLD
    // [EDI+0x4c]; FADD [EDX]; FSTP [ESI+0x94]`). The surface manager is then
    // overwritten unconditionally with the module's default instance
    // (Common.dll.txt:063625c4).
    IVP_Ball(IVP_Cluster* Father, const float* Radius,
             const IVP_RealObjectConstructionTemplate* Parms,
             const IVP_U_Quat* InitialOrientationOrNull,
             const IVP_U_Point* InitialPosition);
};
static_assert(sizeof(IVP_Ball) == 0xA8, "IVP_Ball must not change IVP_Real_Object's layout");

// FUN_06345b20 (Common.dll.txt:06345b20-06345e2f, decomp-tracker item 2817).
// The "second phase" of IVP_Real_Object construction -- rest-state (RotSpeed/
// Speed), damping, LocalCsys/ApplyLocalCsys, RotInertia/Mass, and the
// DeltaTime/InvDeltaTime world-transform snapshot. Always called AFTER
// IVP_Real_Object::IVP_Real_Object (Self->environment must already be set --
// see this function's own header comment in ivp_real_object.cpp for why).
// Declared here (not just file-local to ivp_real_object.cpp) so the
// IVP_Environment::static_object build chain (FUN_06362570/FUN_0634cdf0,
// decomp-tracker items 3487/3050; IVP_Ball::IVP_Ball in ivp_real_object.cpp and the
// IVP_Environment constructor in ivp_environment.cpp) can call it.
// FUN_06345b20: thiscall on Self, stack (Environment, Template), RET 8.
void IVP_InitCoreRestStateAndDamping(IVP_Real_Object* Self, IVP_Environment* Environment,
                                     const IVP_RealObjectConstructionTemplate* Template);

// ---- cmn-phy6 block (2026-10-08): core merge / split, both cdecl free
// functions in retail. Port-local names. ----

// FUN_06343700 (.text:06343700-0634393c): weld Object into Host's core.
// Distance < 0 (or NaN) means "measure it": the distance between the two
// cores' positions extrapolated to Host's environment time. The merged core's
// UpperLimitRadius (+0x04) and +0x08 grow to Distance + the old core's values
// when larger; Object keeps its world pose through PhySys::
// BeamBodyToWorldPose (06343cc0). Callers: PhySys 06284aca / 06284b69 /
// 06284c4d / 06284d20 / 06288eb2 / 0628b5c7.
void IVP_Merge_Object_Into_Core(IVP_Real_Object* Host, IVP_Real_Object* Object, float Distance);

// FUN_06343940 (.text:06343940-06343cb3): give Object a fresh IVP_Core built
// from Template at its current world pose, carrying over the old core's
// rotation speed and the old core's surface speed at Object's position.
// Callers: PhySys 062848e2 / 0628b6b1.
void IVP_Split_Object_From_Core(IVP_Real_Object* Object, const IVP_RealObjectConstructionTemplate* Template);
// ---- end cmn-phy6 block ----

// ============================================================================
// IVP_GetDefaultSurfaceManager() -- returns the module's one static
// IVP_SurfaceManager_Default instance, DAT_0658afe0.
//
// REWRITTEN AND CORRECTED (D-claude-common-ivp pass, 2026-09-17). The block
// that stood here described a hand-rolled 10-slot fake vtable over an opaque
// {VTable, Inner} pair, because IVP_SurfaceManager was only forward-declared.
// It is now a real class -- see ivp_surface_manager.h for the complete,
// byte-extent-checked 11-slot geometry of both the abstract base
// (.rdata:063a56ac) and this default implementation (.rdata:063a5830), the
// per-slot bodies, the /OPT:ICF caveat on the borrowed slot names, and the
// static-init/atexit lifetime of the instance itself.
//
// THREE CLAIMS FROM THE OLD BLOCK ARE NOW KNOWN TO BE WRONG, recorded here
// so they are not reintroduced:
//   1. "Vtable SIZE ... ambiguous past slot 9 ... Sized this port's own
//      table to 10 slots (0-9) to stay inside the unambiguous range."
//      Wrong: the table is unambiguously 11 slots. The abstract BASE table
//      at 063a56ac has exactly the same 0x28-byte extent and its slot 10 is
//      _purecall, which only makes sense if slot 10 belongs to this class
//      hierarchy. Slot 10's body is `MOV EAX,1; RET`.
//   2. "Slots 3-8 ... are real-but-unexamined bodies." They are examined
//      now; slots 7 and 8 are not even overrides -- both tables share the
//      one address 062ed730, a bare `RET 4` inherited from the base.
//   3. "this port's own singleton needs neither [static init nor atexit
//      teardown], being a simple non-owning zero-filled scratch block."
//      Retail's block is genuinely owned: allocated by FUN_06343fc0(0x20,
//      0x10) in the constructor and released by FUN_06343ff0 in the
//      destructor. The port now owns it the same way.
//
// This accessor is kept because existing callers use it; it is a port-only
// convenience, NOT a retail function -- retail has no getter and takes the
// instance's address directly (see ivp_surface_manager.h).
// ============================================================================
IVP_SurfaceManager* IVP_GetDefaultSurfaceManager();

// Slot transforms (ECX = slot, RET 8): retail's out-of-line copies of the
// IVP_U_Matrix operations on the slot's WorldTransform (+0x1c); each body is the
// same x87 sequence as the matrix method it forwards to. See ivp_real_object.cpp.
// FUN_0634d860 == vimult4 (06348470): world position -> slot space.
void IVP_TransformWorldToSlotSpace(struct IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldPos, IVP_U_Point* OutLocalPos);

// FUN_0634d9d0 == vmult (06348580): slot direction -> world.
void IVP_RotateSlotToWorldSpace(struct IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalDir, IVP_U_Point* OutWorldDir);

// FUN_0634d970 == vimult (06366010): world direction -> slot space.
void IVP_RotateWorldToSlotSpace(struct IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldDir, IVP_U_Point* OutLocalDir);

// FUN_0634d8d0 == vmult4 (063484e0): slot position -> world (called by
// give_world_coords_AT 0635bda0).
void IVP_TransformSlotToWorldSpace(struct IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalPos, IVP_U_Point* OutWorldPos);

#pragma pack(pop)

static_assert(sizeof(IVP_Real_Object) == 0xA8, "IVP_Real_Object size mismatch");
