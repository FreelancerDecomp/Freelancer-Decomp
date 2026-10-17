// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_pub.h"       // pub::AI::DirectiveGotoOp
#include "common_aisteer.h"   // ICRSplineSegment
#include "common_goods.h"     // BaseWatcher
#include "common_idea.h"      // MoveIdea, FaceIdea, SplineMoveIdea

#include <cstddef>

// ============================================================================
// GotoBehavior -- AI "fly to a point" state machine, intermediate base of
// FreeFlightBehavior (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   TypeDescriptor @ .data:0x063ebe3c, name ".?AVGotoBehavior@@", referenced
//   as the MIDDLE entry of FreeFlightBehavior's own 3-entry BaseClassArray
//   (@0x063a8d54 = [FreeFlightBehavior(self), GotoBehavior(mdisp=0), Behavior
//   (mdisp=0)]) -- a genuine single-chain 2-level hierarchy, FreeFlightBehavior
//   : public GotoBehavior : public Behavior, NOT a flat 3-base multiple-
//   inheritance layout (numContainedBases progression in the array confirms
//   the chain shape). GotoBehavior's own vtable is `.rdata:0x063a2284`
//   (`PTR_FUN_063a2284`), a SEPARATE, EARLIER vtable than FreeFlightBehavior's
//   own `.rdata:0x0639f5dc` -- FreeFlightBehavior's ctor installs its own
//   vtable ON TOP of the one GotoBehavior's own ctor already installed (see
//   common_freeflightbehavior.cpp), textbook base-ctor-then-derived-vtable-
//   install C++ codegen.
//
// STRUCTURAL CONFIRMATION beyond the RTTI chain itself: comparing GotoBehavior's
// 27-slot vtable (.rdata:0x063a2284-0x063a22ec) against FreeFlightBehavior's own
// 27-slot vtable (.rdata:0x0639f5dc-0x0639f644) slot-by-slot shows the TWO
// vtables share the exact same function pointer in the majority of slots
// (1/2/5/7/8/9/11/12/13/14/16/17/18/19/20/21/22/23/24/25/26) and differ only in
// slots 0/3/4/6/10/15 (FreeFlightBehavior's own overrides) -- i.e.
// FreeFlightBehavior's vtable is GotoBehavior's vtable with exactly 6 slots
// replaced, the unmistakable signature of a derived class overriding a handful
// of virtuals while inheriting the rest. See common_freeflightbehavior.h for
// the override slot table.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062fd620 (Common.dll.txt 280834-280915). Calls the shared
// TractorArm-family base-init field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) first (item 1529, already a
// REAL field-initialising base ctor -- it writes +0x04..+0x29 (see Behavior in common_behavior.h); the long-recorded "no-op stub" reading was wrong), then default-
// constructs a MoveIdea, a FaceIdea and a SplineMoveIdea (all three
// RTTI-identified, common_idea.h -- see that header's own class comment),
// using the SAME shared helper functions DockBehavior/LootBehavior/
// LaunchBehavior already use:
//   +0x2c  FUN_062d4e60  MoveIdea ctor ("SubObjectA", 0x78 bytes, ends +0xa4)
//   +0xa4  FUN_062d6750  FaceIdea ctor ("SubObjectB", 0xc0 bytes, ends +0x164)
//   +0x164 FUN_062d9bf0  SplineMoveIdea ctor ("SubObjectC", 0x174 bytes, ends
//          +0x2d8) -- RTTI-identified 2026-09-17 (vtable .rdata:063a0568, COL
//          at [-4] == 063a0564 -> 063a9c18, pTypeDescriptor
//          .data:SplineMoveIdea::RTTI_Type_Descriptor). No longer a gap.
// then zeroes a BaseWatcher pair at +0x310/+0x314, default-constructs a REAL,
// already-ported ICRSplineSegment (common_aisteer.h) at +0x318 (0x40 bytes,
// ends +0x358), default-constructs a small (0x10-byte) POD at +0x370 via
// FUN_062c29b0 (decomp-tracker item 1253 -- confirmed this pass to zero 4
// dwords; the destructor, traced below, proves the first 8 of those 16 bytes
// are themselves a BaseWatcher), and default-constructs a REAL, already-ported
// pub::AI::DirectiveGotoOp (common_pub.h) at +0x38c (0x70 bytes, ends +0x3fc --
// matching item 1253's own independent confirmation that
// `operator_new(0x3fc)` is used at every GotoBehavior allocation site, i.e.
// sizeof(GotoBehavior) == 0x3fc exactly). Installs GotoBehavior's OWN vtable
// (PTR_FUN_063a2284), then explicitly zeroes/sets a block of own-data fields
// between +0x2d8 and +0x36c (see the field list below for each one). The
// trailing `CMP [this+0x310],0` / conditional `BaseWatcher::set_pointer`
// immediately after the zeroing is CONFIRMED DEAD CODE in the ctor path
// specifically (the field was JUST zeroed two instructions earlier, so the
// comparison always takes the "already null" branch) -- same pattern already
// documented in LootBehavior's own ctor; not modeled as a real ctor effect
// below.
//
// ----------------------------------------------------------------------------
// Destructor: FUN_062fd7a0 (Common.dll.txt 280941-281018), the real
// (non-thunk) body FUN_062fd780 (item 1989, MSVC's scalar-deleting-destructor
// boilerplate) calls. Fully traced this pass, byte-for-byte identical in
// shape to FreeFlightBehavior's own FUN_062c6df0 (see
// common_freeflightbehavior.cpp) -- unwatches SIX BaseWatcher-shaped pointer
// fields via `BaseWatcher::set_pointer(nullptr)`:
//   +0x370 (Watcher370, a top-level field this port models for real)
//   +0x310 (Watcher310, ditto)
//   +0x164+0xa0 = +0x204 (SubObjectC.Face.Watcher -- SplineMoveIdea embeds a
//                          FaceIdea at its own +0x8c, whose Watcher is +0x14)
//   +0x164+0x28 = +0x18c (SubObjectC.Move.Watcher -- SplineMoveIdea embeds a
//                          MoveIdea at its own +0x14, whose Watcher is +0x14)
//   +0xa4+0x14  = +0xb8  (SubObjectB's own MoveIdea/FaceIdea-family BaseWatcher
//                          -- SubObjectB.Watcher, now real, see below)
//   +0x2c+0x14  = +0x40  (SubObjectA's own BaseWatcher -- SubObjectA.Watcher,
//                          now real, see below)
// then tail-calls the shared FUN_062d2080(this, -1) base-class teardown
// continuation (same FULLPORT-DEFERRED stand-in DockBehavior's own real dtor
// uses). ALL SIX of the top-level/sub-object BaseWatcher fields are now
// cleared for real, as of 2026-09-17: the last two stopped being "inside an
// opaque SubObjectC buffer" the moment that buffer became a real
// SplineMoveIdea, and the fact that those two destructor offsets land exactly
// on its embedded MoveIdea's and FaceIdea's own Watcher members is independent
// structural confirmation of that class's layout.
// ============================================================================

// Intermediate base holding GotoBehavior's own +0x04..+0x2c fields -- the
// family-standard "shared context prefix" shape every Behavior-family sibling
// with a SetContext()-shaped vtable slot uses (see DockBehaviorFields /
// FleeBehavior's own field list for byte-identical precedent at the same
// offsets). GotoBehavior's own ctor never touches this span directly (it's
// populated later by the shared FUN_062d2090 context initializer, reached
// through GotoBehavior's own vtable slot 1 / LAB_062fd890 -- decomp-tracker
// item 1991, already partially ported as a free function in
// src/Common/common_gotobehavior.cpp by a different lane (it lived in
// src/Common/common_stubs.cpp until the 2026-09-17 TU split); not re-derived here since
// this pass's own budget went to structurally standing up the class).
class GotoBehaviorFields : public Behavior
{
public:
    explicit GotoBehaviorFields(int InTypeId);

    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.       // +0x08. Family-standard shape (see class comment).
    int ModeSelector0xc;        // +0x0c.
    int UpdateResult10;         // +0x10.
    void* OwnerContextPtr14;    // +0x14.
    unsigned char Opaque18[0x1c - 0x18];
    // +0x1c: the per-tick delta IBehaviorManager::UpdateBehaviorTick stores before it calls
    // slot 7 (.text:062d335d-062d3364, `FSTP float [EDI+0x1c]`). Update() hands it to each
    // Idea's Update (`PUSH [ESI+0x1c]` at .text:062fe19c / 062fe4b0 / 062fe527).
    float DeltaTime1c;
    // +0x20. TYPED 2026-09-17 (claude:common-behavior-fields) from GotoBehavior's OWN call
    // sites, not by carrying a sibling's proof across. Update() (LAB_062fdb80) loads the field
    // straight into ECX as a __thiscall receiver at seven separate sites:
    //   * .text:062fddf2 / 062fde74 / 062fdec1 / 062fdf6e / 062fe076 / 062fe608 --
    //     `MOV ECX,dword ptr [ESI + 0x20]` then `CALL FUN_062d29a0`, whose whole body writes
    //     +0x118 and +0x11c. Those are exactly the two fields IBehaviorManager writes on its
    //     OWN `this` at .text:062d2ea9-062d2eb3, in a function proven to be IBehaviorManager's
    //     by its reads of +0xc0, +0xc4 and the +0x174 dispatch table -- all three named by
    //     exported IBehaviorManager symbols. FUN_062d29a0 also sits inside the contiguous run
    //     of named IBehaviorManager methods (set_state_graph @062d28b0 .. get_scan_response
    //     @062d2b00), so the surviving /OPT:ICF copy is IBehaviorManager's.
    //   * .text:062fe28c -- `MOV EDX,[ESI+0x20]; MOV ECX,[EDX+0xc]; TEST ECX,ECX; CALL
    //     FUN_062de160`, byte-for-byte the same +0x0c load and same callee the NAMED retail
    //     symbol `IBehaviorManager::update_current_behavior_cruise` performs on its own `this`
    //     at .text:062d3bfa-062d3c01.
    // And FUN_062fe970 does `MOV ECX,[ESI+0x20]` at .text:062fe9d9 into `CALL FUN_062d4b20`
    // (== IBehaviorManager::get_turn_sensitivity_factor, `FLD [ECX+0x164]`, already ported in
    // common_behaviormanager.cpp). The shared writer FUN_062d2090 was deliberately NOT used as evidence
    // (fourteen callers, so Param2's type must come from each class's own dereferences).
    IBehaviorManager* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24, the SystemManager (binder).
    unsigned char Flags28;      // +0x28.
    unsigned char Flag29;       // +0x29.
    unsigned char Pad2a[0x2c - 0x2a];
};
static_assert(offsetof(GotoBehaviorFields, CachedHandle08) == 0x08, "GotoBehaviorFields::CachedHandle08 offset");
static_assert(offsetof(GotoBehaviorFields, ModeSelector0xc) == 0x0c, "GotoBehaviorFields::ModeSelector0xc offset");
static_assert(offsetof(GotoBehaviorFields, UpdateResult10) == 0x10, "GotoBehaviorFields::UpdateResult10 offset");
static_assert(offsetof(GotoBehaviorFields, OwnerContextPtr14) == 0x14, "GotoBehaviorFields::OwnerContextPtr14 offset");
static_assert(offsetof(GotoBehaviorFields, DeltaTime1c) == 0x1c, "GotoBehaviorFields::DeltaTime1c offset");
static_assert(offsetof(GotoBehaviorFields, BehaviorMgrPtr20) == 0x20, "GotoBehaviorFields::BehaviorMgrPtr20 offset");
static_assert(offsetof(GotoBehaviorFields, NavHelperPtr24) == 0x24, "GotoBehaviorFields::NavHelperPtr24 offset");
static_assert(offsetof(GotoBehaviorFields, Flags28) == 0x28, "GotoBehaviorFields::Flags28 offset");
static_assert(offsetof(GotoBehaviorFields, Flag29) == 0x29, "GotoBehaviorFields::Flag29 offset");
static_assert(sizeof(GotoBehaviorFields) == 0x2c, "GotoBehaviorFields size");

class GotoBehavior : public GotoBehaviorFields
{
public:
    // Retail's ctor argument is the behavior type id (2, "Goto"); it is forwarded to GotoBehaviorFields.
    // See Behavior::GetTypeId() in common_behavior.h.
    explicit GotoBehavior(int InTypeId);
    ~GotoBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x063a2284, 27 slots, COL [-4] 063a2280 -> 063a9f94). Own slots versus
    // Behavior::`vftable' 063a02c4 (raw dwords, 2026-10-08): 0, 1, 3, 4, 5, 6, 7, 10, 11, 12,
    // 13, 14, 18, 21, 24. Every other slot is a Behavior pointer (the ICF-folded symbol names
    // Ghidra prints there belong to unrelated classes). All own slots are overrides below:
    //   0 ~GotoBehavior (062fd780 / 062fd7a0)   1 SetContext 062fd890   3 Slot3 062fd920
    //   4 Slot4 062fd9c0   5 Slot5 062fda20   6 evaluate 062fda60   7 Slot7 -> Update 062fdb80
    //  10 Slot10 062fe670  11 Slot11 0627a710  12 set_direction 062fe970
    //  13 set_throttle 062feae0  14 set_cruise 062feb00  18 brake_reverse 062feb60
    //  21 Slot21 062c6d30  24 get_target 062c6db0
    // FreeFlightBehavior (0639f5dc) replaces 0/3/4/6/10 and adds 15; see its header.
    // ------------------------------------------------------------------

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. sizeof(GotoBehavior) == 0x3fc is a HARD confirmation
    // (decomp-tracker item 1253: `operator_new(0x3fc)` at every allocation
    // site), not just a lower bound -- see the sizeof static_assert below.
    // ------------------------------------------------------------------

    // +0x04..+0x2c: GotoBehavior's own context-prefix data (0x28 bytes) --
    // lives on the GotoBehaviorFields intermediate base above.

    MoveIdea SubObjectA;               // +0x2c, ends +0xa4. RTTI-identified as MoveIdea
                                       // (ctor FUN_062d4e60, common_idea.h) -- was an
                                       // opaque `unsigned char[0x78]` byte-blob prior to
                                       // this pass. sizeof(MoveIdea) == 0x78 confirmed
                                       // exactly matches this span. Its own BaseWatcher
                                       // lives at relative +0x14 (absolute +0x40), per the
                                       // real destructor's own confirmed unwatch call.
    FaceIdea SubObjectB;               // +0xa4, ends +0x164. RTTI-identified as FaceIdea
                                       // (ctor FUN_062d6750, common_idea.h) -- was an
                                       // opaque `unsigned char[0xc0]` byte-blob prior to
                                       // this pass. sizeof(FaceIdea) == 0xc0 confirmed
                                       // exactly matches this span. Its own BaseWatcher
                                       // lives at relative +0x14 (absolute +0xb8), per the
                                       // real destructor.
    SplineMoveIdea SubObjectC;        // +0x164, ends +0x2d8. RTTI-identified as
                                       // SplineMoveIdea 2026-09-17 (ctor FUN_062d9bf0,
                                       // vtable .rdata:063a0568, COL 063a9c18 ->
                                       // SplineMoveIdea::RTTI_Type_Descriptor; see
                                       // common_idea.h) -- was an opaque
                                       // `unsigned char[0x174]` byte-blob before.
                                       // sizeof(SplineMoveIdea) == 0x174 matches this span
                                       // exactly. The two BaseWatchers the real destructor
                                       // unwatches at its own relative +0x28/+0xa0
                                       // (absolute +0x18c/+0x204) are now named:
                                       // SubObjectC.Move.Watcher (0x14+0x14 == 0x28) and
                                       // SubObjectC.Face.Watcher (0x8c+0x14 == 0xa0) --
                                       // independent structural confirmation of the layout.

    // +0x2d8..+0x2e0: the steering triple set_direction (FUN_062fe970) writes -- -Dir.y and
    // -Dir.x scaled by 1/turn-sensitivity and clamped to [-1,1], then -Dir.z. Floats
    // (FSTP dword at .text:062fe9cb/062fe9de/062feaca); retyped from int 2026-10-08.
    float Field2d8;             // +0x2d8. Zeroed by ctor.
    float Field2dc;              // +0x2dc. Zeroed by ctor.
    float Field2e0;               // +0x2e0. Zeroed by ctor.
    // +0x2e4: a throttle. set_throttle (LAB_062feae0) stores its float argument here and
    // set_cruise / Slot3 copy Field360 into it. Retyped from int 2026-10-08.
    float Field2e4;                // +0x2e4. Zeroed by ctor.
    unsigned char Flag2e8;          // +0x2e8. Zeroed by ctor.
    unsigned char Flag2e9;           // +0x2e9. Set to 1 by ctor.
    unsigned char Flag2ea;            // +0x2ea. Zeroed by ctor.
    unsigned char Flag2eb;             // +0x2eb. Zeroed by ctor's own write, but
                                        // OVERWRITTEN to 1 by FreeFlightBehavior's own ctor
                                        // AFTER it re-installs its own vtable on top of
                                        // GotoBehavior's -- see common_freeflightbehavior.cpp.
    unsigned char Flag2ec;              // +0x2ec. Zeroed by ctor.
    unsigned char Pad2ed[0x2f0 - 0x2ed]; // unconfirmed span
    float Field2f0;               // +0x2f0. Set to 1.5f by ctor (retail: 0x3fc00000).
    int Field2f4;                  // +0x2f4. Set to 3 by ctor. Slot10 mirrors GotoOp's
                                   // Field_0C (the 0/1/2 goto kind) into it.
    // +0x2f8: lead point. Slot10's spline case copies Spline.P2 here (.text:062fe750-062fe763);
    // Update mirrors other points into it. Not written by the ctor (value-initialised here).
    Vector Field2f8;
    // +0x304..+0x30c: the goto destination point. Slot10's case 1 copies GotoOp.Destination
    // here (.text:062fe76b-062fe783); FreeFlightBehavior::Slot3 sets all three to 1e6f.
    // Retyped from three ints 2026-10-08; the ctor zeroes the three dwords.
    Vector Field304;
    BaseWatcher Watcher310;      // +0x310 (8 bytes, ends +0x318). Zeroed by ctor;
                                  // unwatched via set(nullptr) by the real destructor.
    ICRSplineSegment Spline;    // +0x318 (ends +0x358). Real, already-ported type
                                  // (common_aisteer.h).
    float Field358;             // +0x358. Set to 200.0f by ctor (retail: 0x43480000).
    float Field35c;               // +0x35c. Set to -1.0f by ctor (retail: 0xbf800000).
    float Field360;                // +0x360. Set to 1.0f by ctor (retail: 0x3f800000).
    // +0x364: Update()'s "far enough to cruise" byte -- written only at .text:062fe237 as
    // (clearance > 1750.0f) while Flag365 is set, read at 062fe26e / 062fe27a. Not written by
    // retail's ctor (value-initialised here).
    unsigned char Flag364;
    unsigned char Flag365;         // +0x365. Set to 1 by ctor.
    unsigned char Flag366;          // +0x366. Zeroed by ctor. Slot10: GotoOp.Field_58.
    unsigned char Flag367;          // +0x367. Not ctor-written. SetContext sets 1, Slot10 copies
                                    // GotoOp.Field_59; both feed MoveIdea::SetEnabled.
    unsigned char Flag368;         // +0x368. Zeroed by ctor.
    unsigned char Flag369;          // +0x369. Zeroed by ctor.
    unsigned char Flag36a;            // +0x36a. Zeroed by ctor.
    unsigned char Flag36b;             // +0x36b. Zeroed by ctor.
    unsigned char Flag36c;              // +0x36c. Set to 1 by ctor.
    unsigned char Pad36d[0x370 - 0x36d]; // unconfirmed span

    // +0x370 (ends +0x38c). Embedded POD, default-constructed via FUN_062c29b0
    // (decomp-tracker item 1253: zeroes 4 dwords). The real destructor's own
    // confirmed `BaseWatcher::set_pointer(nullptr)` call on this exact offset
    // proves the first 8 of those 16 zeroed bytes are a real BaseWatcher.
    //
    // 2026-09-17 -- THE REST OF THE TYPE IS NOW RECOVERED, and it is not
    // GotoBehavior-specific. It is the SAME 0x1c-byte "proximity watcher"
    // struct DockBehavior embeds at its own +0x450 (see
    // common_dockbehavior.h's DockProximityWatcher, recovered independently
    // by the 2026-09-11 DockBehavior pass). Four independent facts line up:
    //
    //   1. SIZE. This span runs +0x370..+0x38c, because GotoOp (a real,
    //      already-ported pub::AI::DirectiveGotoOp) starts at +0x38c. That is
    //      0x1c bytes -- exactly sizeof(DockBehavior::DockProximityWatcher),
    //      which its own header static_asserts. The old `Pad380[0xc]`
    //      "unconfirmed span" was this struct's tail all along.
    //   2. CONSTRUCTOR. Both classes build the object by calling the same
    //      shared FUN_062c29b0, which zeroes the first 4 dwords and nothing
    //      else -- see common_dockbehavior.cpp's ctor comment, which
    //      independently records "ProxWatcher450's first 4 dwords
    //      (+0x450/+0x454/+0x458/+0x45c)".
    //   3. METHODS. FUN_062c2af0 / FUN_062c2c70 / FUN_062c2d90 (items
    //      1257/1258/1259) are called on THIS object from GotoBehavior::Update
    //      -- `LEA EBX,[ESI+0x370]` at .text:062fe0dd, with ECX==EBX at every
    //      one of the four CALLs (.text:062fe0e6/062fe0f6/062fe16d/062fe435).
    //      Those three items were on record as "DockBehavior methods"; they
    //      are not. They are methods of this shared struct, which is why they
    //      show up under two unrelated owners. (Lesson #44 in reverse: a
    //      shared callee proves a shared PARAMETER type, not a shared owner.)
    //   4. FIELD USE. Item 1257 describes +0x14 as the "3-bit distance zone"
    //      flags dword. GotoBehavior::Update reads exactly that dword, at
    //      absolute +0x384 == 0x370 + 0x14: `TEST byte ptr [ESI+0x384],0x2`
    //      at .text:062fe1c4, one of the three conditions in this class's
    //      shared "clear to proceed" watcher guard.
    //
    // Fields are named after DockProximityWatcher's own recovered names where
    // those are established, and offset-named where they are not. The type is
    // NOT hoisted into a shared header here: that would edit
    // common_dockbehavior.h, which this lane does not own. Recorded on
    // decomp-tracker items 1257/1258/1259 instead so the owning lane can do it.
    BaseWatcher Watcher370;        // +0x370 (ends +0x378). Struct-relative +0x00.
    float Field378;                // +0x378. Struct-relative +0x08 (DockProximityWatcher
                                   // ::Unknown08). Zeroed by the shared ctor. A threshold
                                   // item 1257 compares a distance against; exact meaning
                                   // not recovered. TODO.
    float Field37c;                // +0x37c. Struct-relative +0x0c (::Unknown0c). Zeroed by
                                   // the shared ctor. Second threshold of the same triple.
                                   // TODO.
    float Field380;                // +0x380. Struct-relative +0x10 (::Unknown10). Cruise
                                   // speed ratio, written by ProxWatcherInit (FUN_062c29f0);
                                   // NOT zeroed by the shared ctor (which stops after 4
                                   // dwords). Retyped from unsigned 2026-10-08.
    unsigned Flags384;             // +0x384. Struct-relative +0x14 (::Flags14). Distance-zone
                                   // flags accumulator, bits 0x1/0x2/0x4, recomputed by
                                   // FUN_062c2af0. Bit 0x2 is read by this class's own
                                   // "clear to proceed" watcher guard, at both of its
                                   // inlined sites (.text:062fe1c4 and .text:062fe5b6).
    unsigned SavedFlags388;        // +0x388. Struct-relative +0x18 (::SavedFlags18).
                                   // Previous-frame snapshot of Flags384, per items
                                   // 1257/1259.
    pub::AI::DirectiveGotoOp GotoOp; // +0x38c (ends +0x3fc). Real, already-ported type
                                       // (common_pub.h).

    // ------------------------------------------------------------------
    // Methods
    // ------------------------------------------------------------------

    // Vtable slot 7 (.rdata:063a22a0), the family's per-tick update entry.
    // Retail body: .text:062fdb80, Common.dll.txt 281323-282292, 796
    // instructions -- one of the four largest single items in Common.dll
    // (decomp-tracker item 1996). Returns the tri-state result code (1/2/3)
    // it also stores into GotoBehaviorFields::UpdateResult10.
    //
    // NOT `virtual`: this class models retail's vtable pointer as data (see
    // Behavior's own layout), so a C++ virtual here would insert a SECOND
    // vptr and shift every offset the static_asserts below pin down. Same
    // non-virtual convention DockBehavior::Update() already uses.
    //
    // The body lives in src/Common/common_gotobehavior.cpp; its frame map and
    // branch structure are documented there and on the tracker item.
    int Update();

    // Retail's per-tick entry point is vtable slot 7 (+0x1c), not a non-virtual member:
    // IBehaviorManager::UpdateBehaviorTick writes the accumulated tick delta and then
    // dispatches through it -- Common.dll.txt .text:062d335d-062d336a,
    //     FLD float ptr [ESI + 0xd4]   /   FSTP float ptr [EDI + 0x1c]
    //     CALL dword ptr [EAX + 0x1c]  ; vtable +0x1c IS Slot7
    // EDI there is a bare Behavior* out of the dispatch table, so Update() below was
    // unreachable through a Behavior* until this override existed. Base Behavior::Slot7 is
    // _purecall in retail; see common_behavior.h. Same wiring as TrailBehavior.
    int Slot7() override { return Update(); }

    // ---- Retail's own slots of .rdata:063a2284, bound as overrides 2026-10-08
    // ---- (claude-code:cmn-ai5). Until then they were extern "C" free functions over a raw
    // ---- `unsigned char*` that the manager's Behavior* dispatch never reached, so goto and
    // ---- free-flight ships ran Behavior's base bodies. Bodies in common_gotobehavior.cpp.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;   //  1 LAB_062fd890 (RET 0xc)
    void Slot3() override;                                             //  3 FUN_062fd920
    void Slot4() override;                                             //  4 FUN_062fd9c0
    int Slot5(float Value) override;                                   //  5 LAB_062fda20 (RET 4)
    EvaluateResult evaluate(unsigned int Context) override;            //  6 LAB_062fda60 (RET 4)
    // 10 LAB_062fe670 (RET 4): applies a pub::AI::DirectiveGotoOp (0x70 bytes, copied into
    // GotoOp) and dispatches on its Field_0C goto kind.
    bool Slot10(void* DirectiveOp) override;
    bool Slot11() override { return true; }                            // 11 0627a710 `MOV AL,1; RET`
    void set_direction(Vector const& Dir) override;                    // 12 FUN_062fe970 (RET 4)
    void set_throttle(float Throttle) override;                        // 13 LAB_062feae0 (RET 4)
    void set_cruise(bool bOn) override;                                // 14 LAB_062feb00 (RET 4)
    void brake_reverse(bool bOn) override;                             // 18 LAB_062feb60 (RET 4)
    void Slot21(void* Arg) override;                                   // 21 FUN_062c6d30 (RET 4)
    IObjRW const* get_target() override;                               // 24 LAB_062c6db0
};

static_assert(offsetof(GotoBehavior, SubObjectA) == 0x2c, "GotoBehavior::SubObjectA offset");
static_assert(offsetof(GotoBehavior, SubObjectB) == 0xa4, "GotoBehavior::SubObjectB offset");
static_assert(offsetof(GotoBehavior, SubObjectC) == 0x164, "GotoBehavior::SubObjectC offset");
static_assert(offsetof(GotoBehavior, Field2d8) == 0x2d8, "GotoBehavior::Field2d8 offset");
static_assert(offsetof(GotoBehavior, Field2dc) == 0x2dc, "GotoBehavior::Field2dc offset");
static_assert(offsetof(GotoBehavior, Field2e0) == 0x2e0, "GotoBehavior::Field2e0 offset");
static_assert(offsetof(GotoBehavior, Field2e4) == 0x2e4, "GotoBehavior::Field2e4 offset");
static_assert(offsetof(GotoBehavior, Flag2e8) == 0x2e8, "GotoBehavior::Flag2e8 offset");
static_assert(offsetof(GotoBehavior, Flag2e9) == 0x2e9, "GotoBehavior::Flag2e9 offset");
static_assert(offsetof(GotoBehavior, Flag2ea) == 0x2ea, "GotoBehavior::Flag2ea offset");
static_assert(offsetof(GotoBehavior, Flag2eb) == 0x2eb, "GotoBehavior::Flag2eb offset");
static_assert(offsetof(GotoBehavior, Flag2ec) == 0x2ec, "GotoBehavior::Flag2ec offset");
static_assert(offsetof(GotoBehavior, Field2f0) == 0x2f0, "GotoBehavior::Field2f0 offset");
static_assert(offsetof(GotoBehavior, Field2f4) == 0x2f4, "GotoBehavior::Field2f4 offset");
static_assert(offsetof(GotoBehavior, Field2f8) == 0x2f8, "GotoBehavior::Field2f8 offset");
static_assert(offsetof(GotoBehavior, Field304) == 0x304, "GotoBehavior::Field304 offset");
static_assert(offsetof(GotoBehavior, Flag367) == 0x367, "GotoBehavior::Flag367 offset");
static_assert(offsetof(GotoBehavior, Watcher310) == 0x310, "GotoBehavior::Watcher310 offset");
static_assert(offsetof(GotoBehavior, Spline) == 0x318, "GotoBehavior::Spline offset");
static_assert(offsetof(GotoBehavior, Field358) == 0x358, "GotoBehavior::Field358 offset");
static_assert(offsetof(GotoBehavior, Field35c) == 0x35c, "GotoBehavior::Field35c offset");
static_assert(offsetof(GotoBehavior, Field360) == 0x360, "GotoBehavior::Field360 offset");
static_assert(offsetof(GotoBehavior, Flag364) == 0x364, "GotoBehavior::Flag364 offset");
static_assert(offsetof(GotoBehavior, Flag365) == 0x365, "GotoBehavior::Flag365 offset");
static_assert(offsetof(GotoBehavior, Flag366) == 0x366, "GotoBehavior::Flag366 offset");
static_assert(offsetof(GotoBehavior, Flag368) == 0x368, "GotoBehavior::Flag368 offset");
static_assert(offsetof(GotoBehavior, Flag369) == 0x369, "GotoBehavior::Flag369 offset");
static_assert(offsetof(GotoBehavior, Flag36a) == 0x36a, "GotoBehavior::Flag36a offset");
static_assert(offsetof(GotoBehavior, Flag36b) == 0x36b, "GotoBehavior::Flag36b offset");
static_assert(offsetof(GotoBehavior, Flag36c) == 0x36c, "GotoBehavior::Flag36c offset");
static_assert(offsetof(GotoBehavior, Watcher370) == 0x370, "GotoBehavior::Watcher370 offset");
static_assert(offsetof(GotoBehavior, Field378) == 0x378, "GotoBehavior::Field378 offset");
static_assert(offsetof(GotoBehavior, Field37c) == 0x37c, "GotoBehavior::Field37c offset");
static_assert(offsetof(GotoBehavior, Field380) == 0x380, "GotoBehavior::Field380 offset");
static_assert(offsetof(GotoBehavior, Flags384) == 0x384, "GotoBehavior::Flags384 offset (== Watcher370 + 0x14, the DockProximityWatcher::Flags14 slot read at .text:062fe1c4)");
static_assert(offsetof(GotoBehavior, SavedFlags388) == 0x388, "GotoBehavior::SavedFlags388 offset");
static_assert(offsetof(GotoBehavior, GotoOp) == 0x38c, "GotoBehavior::GotoOp offset");
static_assert(sizeof(GotoBehavior) == 0x3fc, "GotoBehavior sizeof 0x3fc (operator_new(0x3fc) confirmed at every allocation site, decomp-tracker item 1253)");

// ============================================================================
// The shared 0x1c-byte proximity-watcher struct's three methods
// (decomp-tracker items 1257 / 1258 / 1259, PORTED 2026-09-17
// claude:common-attack-goto). See GotoBehavior::Watcher370 above for the
// four-fact proof that the struct embedded at GotoBehavior+0x370 and at
// DockBehavior+0x450 (common_dockbehavior.h's DockProximityWatcher) is the
// same type; these three retail functions are called on BOTH.
//
// They take the struct's BASE ADDRESS as a raw `unsigned char*` rather than a
// typed pointer on purpose: the type is still declared twice (as this class's
// Watcher370/Field378/Field37c/Field380/Flags384/SavedFlags388 run of members,
// and as DockBehavior::DockProximityWatcher), and hoisting it into one shared
// declaration would edit a header outside this wave's file ownership. The
// struct-relative offsets used below are exactly the ones retail uses:
//   +0x00 BaseWatcher (Watchable* + chain link)
//   +0x08 float  near threshold   (cruise-scaled by +0x10 when the watched
//                                  object reports IObjInspect::is_cruise_active)
//   +0x0c float  far threshold    (same scaling)
//   +0x10 float  cruise multiplier
//   +0x14 unsigned  3-bit distance-zone flags (0x1 / 0x2 / 0x4)
//   +0x18 unsigned  previous-tick snapshot of +0x14
// TODO(items 1257/1258/1259): once one lane owns both headers, hoist the
// struct and turn these back into real member functions.
// ============================================================================

// FUN_062c29f0 (.text:062c29f0-062c2aa5, RET 0x14, item 1254). Re-arms the watcher:
// +0x08 = Near, +0x0c = Far, watch Target (Target+8, deduplicated), clear +0x14/+0x18; then,
// when something is watched, +0x10 = CruiseRatio if it is > 0.0, else own ship's
// get_speed(1.0f, cruise) / Target's get_speed(1.0f, engine), clamped up to 1.0f.
// `NavHelper` is the SystemManager whose +0x08 Watcher names the own ship. GotoBehavior's
// Slot10 calls it; DockBehavior::Slot10 inlines the same sequence.
void ProxWatcherInit(unsigned char* Watcher, void* NavHelper, void* Target, float Near, float Far,
                     float CruiseRatio);

// FUN_062c2af0 (Common.dll.txt 186949-187101, 135 insns, item 1257).
// Recomputes the +0x14 zone flags for this tick. `NavHelper` is the Behavior
// family's NavHelperPtr24; retail only ever uses `NavHelper + 0x14FC`, the
// AI steering element common_aisteer.h's GetSignedRadialClearanceToPoint /
// GetInterceptClearanceOfIface operate on. `RefPoint` is the world point the
// zones are measured against (GotoBehavior passes &this->Field2f8).
void ProxWatcherRecomputeZoneFlags(unsigned char* Watcher, void* NavHelper, Vector const& RefPoint);

// FUN_062c2c70 (Common.dll.txt 187107-187226, 99 insns, item 1258). Returns a
// speed cap: the lesser of the zone-derived speed and the owner's own
// IObjInspect::get_speed(SelectorValue-or-1.0f, Selector). `OwnerContext` is
// the Behavior family's OwnerContextPtr14, `NavHelper` its NavHelperPtr24.
// Retail returns 0.0f when nothing is watched. The result is left unrounded in ST0
// (DockBehavior::AdvancePreamble FCOMPs it directly at .text:062c489d), hence double.
double ProxWatcherComputeSpeedCap(unsigned char* Watcher, void* OwnerContext, void* NavHelper,
                                  int Selector, float SelectorValue);

// FUN_062c2d90 (Common.dll.txt 187229-187298, 60 insns, item 1259). Compares
// this tick's +0x14 flags against the +0x18 snapshot and, on one of three
// recognised transitions, raises notification code 1/2/3 through the behavior
// manager's own dispatcher (FUN_062d2a10, item 1550). Does nothing otherwise.
void ProxWatcherNotifyZoneChange(unsigned char* Watcher, void* NavHelper, void* BehaviorMgr,
                                            Vector const& RefPoint);

// ============================================================================
// THE FORMATION-FOLLOWER ENVELOPE -- decomp-tracker item 1855 (FUN_062ebf50,
// 471 insns, Common.dll.txt 251151-251678), PORTED 2026-09-17
// (claude:common-goto). This is the gate GotoBehavior::Update has been
// defaulting to `false` since the port began.
//
// THE RECORDED BLOCKERS WERE BOTH STALE, and both are retired here rather than
// re-filed. (a) The 2026-09-10 blocker_reason said the callee cluster
// FUN_062e5af0..ba0 / FUN_062e60e0 / FUN_062e6150 / FUN_062e1630 was "still
// mostly unported"; every one of items 1727/1728/1762/1765/1766/1771/1772 is
// ready_unverified today (Behavior_InvokeManagerSlotD8/D4, ClampedDot3,
// VecNormalize3, GetIfacePositionOrZero, GetNegatedIfaceDirection). (b) Item
// 1763 (FUN_062e5b20) was marked `simplified` because "IObjRW is
// FULLPORT-DEFERRED[OBJ], an opaque forward declaration with no vtable" --
// that has not been true since 2026-09-16: common_iface.h declares IObjRW for
// real, and its IObjInspect sub-object's slot +0xF0 is
// `get_angular_speed_XY(float, float)`. (c) The `unaff_EBX` / `unaff_retaddr`
// claim in Ghidra's decompile is the usual decompiler artifact, not a blocker:
// `unaff_retaddr` is simply this function's ONE stack argument (the out-struct
// at ESP+4 on entry, loaded into EBX at .text:062ebff0 as `MOV EBX,[ESP+0xc0]`
// with ESP == E-0xbc at that instruction), and `unaff_EBX` is that same
// register before the load. Eighth consecutive `unaff_` blocker disproved in
// this module.
//
// CALL SHAPE (proved from the three call sites -- .text:062c4839,
// .text:062d9d5e, .text:062fe2eb -- and the `ADD ESP,0xa4 / RET 0x4` tails at
// .text:062ec4f5 / .text:062ec507 / .text:062ec515):
//   `bool __thiscall FUN_062ebf50(This, Envelope* Out)`
// `This` is the SystemManager+0x1364 sub-object (the blob common_systemmanager.h
// calls GunConvergenceStateBytes and common_aisteer.h's
// ConstructGunConvergenceState builds), reached at every call site as
// `<SystemManager> + 0x1364`. It is the SAME receiver as item 1856
// (IsIdAmongOwnerFormationFollowers, common_gunconvergence.cpp) and uses the same two
// fields: `This+0x08` is the back-pointer to the owning SystemManager, and
// `This+0x1C .. This+0x9C` is its embedded 32-entry `IObjRW*` follower
// scratch array. `This+0x9C` and `This+0xA0` are the two angle thresholds
// ConstructGunConvergenceState seeds with 0.1f and 0.2f.
//
// The out-struct's five fields are all named from what retail computes into
// them, not guessed:
//   +0x00 Speed            -- get_speed(...) directly on the fallback path
//   +0x04 Throttle         -- get_projected_throttle(Speed, Type) (slot 0xD4)
//   +0x08 AngularSpeed     -- folded from get_angular_speed_XY (slot 0xF0)
//   +0x0C AngularThrottle  -- get_projected_axis_throttle_XY(...) (slot 0xF8)
//   +0x10 Flag10           -- confirmed one byte; its MEANING is not proven, so
//                             it keeps a placeholder name. Retail sets it to 1
//                             up front, and on the non-cruise path rewrites it
//                             to `MaxProjection < 20.0f && MinAlignCos > 0.99`.
// Callers zero only the first four dwords before the call and let the callee
// write the byte; GotoBehavior::Update reads +0x04/+0x08/+0x10, FUN_062c47f0
// reads +0x00/+0x08, SplineMoveIdea::Update (LAB_062d9d00) reads +0x04 twice (item 1649).
//
// TODO(ownership): this pair lives here only because src/Common/
// common_gotobehavior.cpp was this wave's writable file. Its natural home is
// common_aisteer.h / common_systemmanager.h next to the rest of the
// GunConvergenceState family; move it when one lane owns both.
// ============================================================================
struct FormationFollowerEnvelope
{
    float Speed;                 // +0x00
    float Throttle;              // +0x04
    float AngularSpeed;          // +0x08
    float AngularThrottle;       // +0x0c
    unsigned char Flag10;        // +0x10 -- TODO: semantic name unproven
};

static_assert(offsetof(FormationFollowerEnvelope, Speed) == 0x00, "FormationFollowerEnvelope::Speed offset");
static_assert(offsetof(FormationFollowerEnvelope, Throttle) == 0x04, "FormationFollowerEnvelope::Throttle offset");
static_assert(offsetof(FormationFollowerEnvelope, AngularSpeed) == 0x08, "FormationFollowerEnvelope::AngularSpeed offset");
static_assert(offsetof(FormationFollowerEnvelope, AngularThrottle) == 0x0c, "FormationFollowerEnvelope::AngularThrottle offset");
static_assert(offsetof(FormationFollowerEnvelope, Flag10) == 0x10, "FormationFollowerEnvelope::Flag10 offset");
static_assert(sizeof(FormationFollowerEnvelope) == 0x14, "FormationFollowerEnvelope is four floats plus a byte, 4-aligned");

// The envelope's producer, FUN_062ebf50 (item 1855), is
// FGunConvergenceStateBlock::ComputeFormationFollowerEnvelope (common_systemmanager.h);
// its definition stays in common_gotobehavior.cpp next to its file-local helpers.
