// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior

#include <cstddef>

// ============================================================================
// NullBehavior -- AI "do nothing" no-op state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a98bc (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor @ .data:0x063ec510, name ".?AVNullBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a98ac, numBaseClasses = 2,
//        BaseClassArray @ 0x063a98a0 = [ NullBehavior(self, numContained=1),
//                                        Behavior(numContained=0, mdisp=0,
//                                        the SAME shared Behavior
//                                        BaseClassDescriptor every other
//                                        sibling in this family reuses) ]
//
// Single-inheritance direct child of Behavior -- same shape as
// IdleBehavior/CruiseBehavior/LaunchBehavior, no secondary vtable.
//
// ----------------------------------------------------------------------------
// "Constructor": there is no dedicated ctor function -- NullBehavior is only
// ever produced by a CREATE_BEHAVIOR-shaped factory wrapper, FUN_062d16d0
// (Common.dll.txt 209444-209474, XREF'd from the behavior-type dispatch
// table entry @ .rdata:0x063a0030 -- the same table CREATE_BEHAVIOR/
// BehaviorTypes::get_behavior_name/get_behavior_id already ported nearby
// index). The wrapper: `operator new(0x2c)`, null-check, `CALL
// FUN_062d2020(newObj, CallerThis->+0x4)` (the family's shared, already-
// documented no-op base-init stub -- item 1529 precedent, touches nothing),
// `[newObj] = PTR_FUN_063a0244` (install vtable), return newObj (or nullptr
// on allocation failure, not modeled by this port's plain `new`). Object
// size is a CONFIRMED 0x2c bytes (the literal `PUSH 0x2c` operand feeding
// operator_new), matching the same "context prefix, no sub-objects" size as
// CruiseBehavior/NullBehavior's other minimal siblings.
//
// ----------------------------------------------------------------------------
// This is, by a wide margin, the SIMPLEST class in the whole TractorArm
// family identified so far: every vtable slot NullBehavior actually
// overrides with real (non-ICF-shared-with-unrelated-classes) code is a
// 2-instruction constant-returner. Slot 1 (SetContext) does NOT call the
// family's shared FUN_062d2090 context initializer at all -- unlike every
// other sibling ported so far, it ignores all three incoming pointers
// outright. As a direct consequence, NO field anywhere in the object
// (CachedHandle08/ModeSelector0xc/UpdateResult10/OwnerContextPtr14/
// BehaviorMgrPtr20/NavHelperPtr24/Flags28/Flag29 -- the shared context-prefix
// shape documented on every other sibling) is ever read or written by any
// traced NullBehavior method. This class exists purely so the AI-directive
// dispatch table has a valid, harmless "no behavior" placeholder entry.
// ============================================================================

class NullBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (0, "NULL"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit NullBehavior(int InTypeId);
    ~NullBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x063a0244, 27 slots, 0x063a0244-0x063a02ac inclusive --
    // byte distance to the next label (DAT_063a02b0) confirms this bound).
    //
    //   0  (0x00) FUN_062c7090        scalar-deleting-dtor thunk, ICF-shared across 7
    //                                 unrelated vtables (decomp-tracker item 1320) -- no
    //                                 NullBehavior-specific cleanup exists anywhere in it
    //   1  (0x04) LAB_062d76a0        ported for real: SetContext() -- `MOV AL,1; RET 0xc`,
    //                                 2 instructions, ignores all 3 incoming pointers entirely
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not NullBehavior-specific
    //   3  (0x0c) Exclusion::AsteroidExclusionZon..  bare RET (.text:062a8920) -- Slot3() override
    //   4  (0x10) Exclusion::AsteroidExclusionZon..  same bare RET -- Slot4() override
    //   5  (0x14) LAB_062d1730        `MOV EAX,1; RET 4`, ICF-shared with CruiseBehavior's and
    //                                 FreeFlightBehavior's own slot 6 -- Slot5() override
    //   6  (0x18) LAB_062cf5e0        trivial `MOV EAX,2; RET 4` stub, ICF-shared 14-way fold
    //                                 (same one documented in common_idlebehavior.h's own slot 6)
    //   7  (0x1c) Archetype::Solar::get_class_type  ported for real (as Update()): ICF-folded
    //                                 with an UNRELATED retail symbol (`MOV EAX,2; RET`, 2
    //                                 instructions) -- reproduced here as NullBehavior's own
    //                                 method rather than a call to that unrelated function,
    //                                 matching this marathon's established ICF-fold policy
    //   8  (0x20) Exclusion::AsteroidExclusionZon..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) LAB_062c2f40        trivial `MOV AL,1; RET 4` stub (unconditional `return
    //                                 true`, NOT the Flags28-touching RequestFlag() variant
    //                                 IdleBehavior's own slot 10 uses) -- ICF-shared, skipped
    //  11  (0x2c) CEquip::DisableController        ICF fold
    //  12-15 (0x30-0x3c) MetaList<GoodDesc>::operator[]  ICF fold (x4)
    //  16  (0x40) FUN_062d2110        family-shared "audio cue" helper, not reachable from any
    //                                 confirmed NullBehavior call site (all its slots are
    //                                 constant-returners with no call-outs) -- FULLPORT-DEFERRED
    //  17  (0x44) FUN_062d21b0        ditto
    //  18  (0x48) MetaList<GoodDesc>::operator[]     ICF fold
    //  19  (0x4c) Archetype::AttachedEquipment::i..  ICF fold
    //  20  (0x50) LAB_062c2f40        trivial, same target as slot 10
    //  21-22 (0x54-0x58) MetaList<GoodDesc>::operator[]  ICF fold (x2)
    //  23  (0x5c) LAB_062c2f50        trivial `AND byte[ECX+0x28],0xfd; RET` (clears bit 0x2 of
    //                                 a Flags28-shaped field), ICF-shared, skipped
    //  24  (0x60) RDLNode::operator_class_AttribS...ICF fold
    //  25  (0x64) CEShield::GetEquipDesc            ICF fold
    //  26  (0x68) RDLNode::operator_class_AttribS...ICF fold (same target as slot 24)
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 219477-219478
    // (LAB_062d76a0), 2 instructions: `MOV AL,1; RET 0xc`. Unconditionally
    // succeeds and touches nothing -- the one genuinely NullBehavior-specific
    // piece of evidence (every other sibling ported so far routes SetContext
    // through the shared FUN_062d2090 initializer; this class deliberately
    // does not).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 7 (offset 0x1c). Common.dll.txt 209514 (the retail
    // function is ICF-folded with Archetype::Solar::get_class_type), 2
    // instructions: `MOV EAX,2; RET`. Always returns the constant 2.
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

    // 2026-10-08 (claude-code:cmn-ai2). Slots 3 and 4 of .rdata:063a0244 are NOT Behavior's
    // FUN_062d20d0/FUN_062d20f0 (base vtable 063a02c4 slots 3/4) but the bare `RET` at
    // .text:062a8920 (folded with Exclusion::AsteroidExclusionZone). Inheriting the base bodies
    // made a NullBehavior reset +0x0c/+0x10/+0x1c/+0x28 and clear the PathfindManager's +0x64
    // when the manager engaged/disengaged it. Slot 5 is LAB_062d1730 `MOV EAX,1; RET 4`
    // (base slot 5 is _purecall).
    void Slot3() override {}                                   // 3 .text:062a8920 RET
    void Slot4() override {}                                   // 4 .text:062a8920 RET
    int Slot5(float) override { return 1; }                    // 5 LAB_062d1730

    // ---- Behavior's own +0x04..+0x29 block, ADDED 2026-09-17 (claude:common-factory) -----
    // This class used to declare no storage at all, which made `sizeof(NullBehavior)` 4 while
    // retail's factory allocates 0x2c. That was harmless only for as long as nothing could
    // construct one. It stops being harmless the moment CREATE_BEHAVIOR works, because
    // Behavior::SetTickDelta writes a float at raw offset +0x1c through a bare `Behavior*`
    // (IBehaviorManager::UpdateBehaviorTick does exactly that, .text:062d335d-062d336a, on
    // every behavior including this one) -- twenty-four bytes past the end of a four-byte
    // object, on every tick.
    //
    // The block is not invented for this class: FUN_062d2020, the shared base constructor
    // retail's NULL factory calls directly at .text:062d170a, writes +0x04, +0x08, +0x0c (=2),
    // +0x10 (=1), +0x14, +0x18, +0x1c, +0x24 and the two flag bytes +0x28/+0x29 on whatever
    // object it is handed. Every other class in this family already declares the same block at
    // the same offsets; this class was the one exception. It stays HERE rather than on
    // Behavior for the reason common_behavior.h spells out at length -- hoisting it has to
    // retire every duplicate in one commit.
    int TypeId;                  // +0x04, the ctor argument (id 0, "NULL").
    unsigned CachedHandle08;     // +0x08, zeroed by FUN_062d2020.
    int ModeSelector0xc;         // +0x0c, set to 2 by FUN_062d2020.
    int UpdateResult10;          // +0x10, set to 1 by FUN_062d2020.
    void* OwnerContext;          // +0x14, zeroed.
    unsigned char Opaque18[0x1c - 0x18]; // +0x18, zeroed as a dword.
    float DeltaTime;             // +0x1c, zeroed; the slot SetTickDelta writes.
    void* BehaviorMgrPtr;        // +0x20. NOT written by FUN_062d2020 -- the family's shared
                                 // context binder FUN_062d2090 is what fills it, and this
                                 // class's SetContext (LAB_062d76a0) deliberately does not
                                 // call it. Declared for layout; left null.
    SystemManager* NavHelper;    // +0x24, zeroed by FUN_062d2020.
    unsigned char Flags28;       // +0x28, bits 0 and 1 cleared by FUN_062d2020 (`AND CL,0xfc`).
    unsigned char Flag29;        // +0x29, zeroed.
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only.
};
static_assert(offsetof(NullBehavior, TypeId) == 0x04, "NullBehavior::TypeId offset");
static_assert(offsetof(NullBehavior, CachedHandle08) == 0x08, "NullBehavior::CachedHandle08 offset");
static_assert(offsetof(NullBehavior, ModeSelector0xc) == 0x0c, "NullBehavior::ModeSelector0xc offset");
static_assert(offsetof(NullBehavior, UpdateResult10) == 0x10, "NullBehavior::UpdateResult10 offset");
static_assert(offsetof(NullBehavior, OwnerContext) == 0x14, "NullBehavior::OwnerContext offset");
static_assert(offsetof(NullBehavior, DeltaTime) == 0x1c, "NullBehavior::DeltaTime offset");
static_assert(offsetof(NullBehavior, BehaviorMgrPtr) == 0x20, "NullBehavior::BehaviorMgrPtr offset");
static_assert(offsetof(NullBehavior, NavHelper) == 0x24, "NullBehavior::NavHelper offset");
static_assert(offsetof(NullBehavior, Flags28) == 0x28, "NullBehavior::Flags28 offset");
static_assert(offsetof(NullBehavior, Flag29) == 0x29, "NullBehavior::Flag29 offset");
static_assert(sizeof(NullBehavior) == 0x2c, "NullBehavior size == retail operator_new argument at .text:062d16e8");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x2c`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(NullBehavior) <= 0x2c, "NullBehavior must not exceed its retail size 0x2c");
