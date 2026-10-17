// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_idea.h"      // IdleIdea (Watcher2c)

#include <cstddef>

// ============================================================================
// IdleBehavior -- AI "idle/loiter" state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a904c (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor @ .data:0x063ebf94, name ".?AVIdleBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a903c, numBaseClasses = 2,
//        BaseClassArray @ 0x063a9030 = [ IdleBehavior(self, numContained=1),
//                                        Behavior(numContained=0, mdisp=0) ]
//
// This is a CONFIRMED CORRECTION to this marathon's earlier assumption
// (common_behavior.h's class-hierarchy comment, and this task's own brief):
// IdleBehavior does NOT derive from BaseFollowerBehavior. Its own
// RTTIBaseClassDescriptor self-entry (@0x063a9018) reports numContainedBases
// = 1 (one ancestor total), and its ClassHierarchyDescriptor's own
// numBaseClasses field is 2 (self + Behavior, full stop) -- both read
// directly from raw .rdata bytes and independently re-verified twice this
// pass. Compare FormationBehavior/FollowBehavior, whose own
// ClassHierarchyDescriptors both say numBaseClasses=3 (self +
// BaseFollowerBehavior + Behavior; re-checked this pass at
// 0x063a8fa8/0x063a8ff4 for exact comparison). IdleBehavior is a direct,
// single-inheritance child of Behavior alone.
//
// Despite NOT literally inheriting BaseFollowerBehavior, IdleBehavior's own
// ctor/vtable-slot bodies independently CONVERGE on the exact same field
// shape BaseFollowerBehavior/DockBehaviorFields (common_behavior.h /
// common_dockbehavior.h) document at absolute offsets +0x08/+0x0c/+0x10/
// +0x14/+0x1c/+0x20/+0x24/+0x28/+0x29 -- proven two independent ways:
//   1. IdleBehavior's own vtable slot 1 (SetContext, LAB_062cd590) calls the
//      SAME shared Behavior-family context initializer FUN_062d2090
//      (already documented in common_dockbehavior.h/.cpp, decomp-tracker
//      item 1532) with `this` UNCHANGED -- that function writes its P1/P2/P3
//      arguments into +0x14/+0x20/+0x24 and a `P1->vtbl[0x20]()` cached
//      handle into +0x08, on WHATEVER `this` it's called with.
//   2. IdleBehavior's own vtable slot 10 (LAB_062cd630) is the LITERAL SAME
//      retail function address FormationBehavior's vtable slot 10 uses
//      (BaseFollowerBehavior_DefaultRequestFlag, common_behavior.h/.cpp) --
//      confirmed via that function's own 2-entry XREF list (.rdata:0639fb8c
//      FormationBehavior's slot 10, 0639fc84 IdleBehavior's slot 10). This
//      is textbook linker ICF (identical code folding): both classes'
//      compiled bodies for this member happened to be byte-identical
//      (`OR byte[this+0x28],0x2; MOV AL,1; RET 4`), so the linker merged
//      them -- unlike the OTHER, semantically-unrelated ICF folds elsewhere
//      in this vtable (CEquip::ConnectServerEquip and friends), this one
//      really is the right behavior for both classes given their matching
//      field layout.
// This is the SAME "shared retail function called across sibling classes at
// matching field offsets" pattern already documented for
// FormationBehavior/DockBehavior (see common_dockbehavior.h's "IMPORTANT
// CROSS-CLASS FINDING") -- evidently a wider convention across this whole
// family (all built from a shared internal template/mixin for the "context +
// flags" prefix), not literal C++ inheritance. Per that precedent's
// guidance, this port does NOT reinterpret_cast an IdleBehavior* to
// BaseFollowerBehavior*/DockBehaviorFields* to reuse their already-ported
// methods (an unverified-offset bridge between unrelated types was already
// tried and rejected once on this marathon); IdleBehavior gets its own
// small, narrowly-scoped, independently-verified equivalents below
// (IdleBehavior::RequestFlag() duplicates BaseFollowerBehavior_
// DefaultRequestFlag's 3-instruction body verbatim as IdleBehavior's own
// method, which is trivially safe since no cast is involved).
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062cd530 (Common.dll.txt 202691-202720). Already tracked
// (decomp-tracker item 1410) under the "TractorArm dispatch table"
// investigation ("TractorArm::Idle (goto-directive mode)") before this pass
// connected it to the IdleBehavior RTTI name -- same relationship as
// DockBehavior's ctor/item 1261 (see common_dockbehavior.h). Calls the
// shared TractorArm-family base-init helper FUN_062d2020 (item 1529,
// REAL field-initialising base ctor -- it writes +0x04..+0x29, see Behavior in common_behavior.h) then default-constructs ONE embedded sub-object at
// +0x2c via FUN_062d9b10 (item 1644) before installing the vtable.
//
// IdleWatcher -- the +0x2c embedded sub-object. FUN_062d9b10 default-
// constructs it: vtable = PTR_LAB_063a054c (a distinct, 7-slot vtable at
// .rdata:0x063a054c-0x063a0568 -- NOT the same vtable as FormationMoveFx's
// PTR_LAB_063a047c, a different class), two dwords (+0x04/+0x08) and one
// byte (+0x10) zeroed. IdleBehavior's own vtable-slot bodies call through it
// exactly like FormationBehavior calls through WatcherA/WatcherB
// (common_behavior.h): slot-index 1 (offset 0x04) as an
// `Evaluate(float dt)`-shaped call (from IdleBehavior::Update(), return
// value discarded) and slot-index 4 (offset 0x10) as a `Reset()`-shaped call
// (from IdleBehavior::Disengage()) -- the same calling SHAPE as
// FormationMoveFx's own slot 4/0x10 convention, but this is a smaller,
// separately-identified class (min confirmed size 0x11 bytes: vfptr + 2
// dwords + 1 byte). No double-embed spacing evidence exists anywhere in this
// module to pin down its true total size the way FormationMoveFx's real size
// (0x5c, corrected 2026-09-11 from an earlier, erroneous 0x78 estimate --
// see common_behavior.h's own FormationMoveFx class comment) was pinned down
// via its own confirmed enclosing-ctor gaps (IdleBehavior embeds only one),
// so this port sizes the byte buffer
// to the smallest 4-byte-aligned bound past every confirmed write/read
// (0x14) and leaves the true size an open question for a follow-up pass.
// Kept as an opaque byte buffer (no dedicated class, no vtable dereferenced
// by name) matching this marathon's placeholder-field policy for a
// still-unidentified sub-object type.
// ============================================================================

class IdleBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (6, "Idle"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit IdleBehavior(int InTypeId);
    ~IdleBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fc5c, 27 slots, 0x0639fc5c-0x0639fcc4 inclusive --
    // byte distance to the next COL/vtable head at 0x0639fcc8/0x0639fccc
    // confirms this bound). Slot -> retail address -> decomp-tracker item.
    // The SAME set of ICF-folded slots documented on DockBehavior's own
    // vtable (common_dockbehavior.h) recur here too, confirming the whole
    // family shares one compiler-emitted 27-slot vtable "shape". IdleBehavior
    // overrides far FEWER of the shape's real slots than DockBehavior/
    // FormationBehavior do -- matching this task's own hint that Idle is one
    // of the simpler siblings -- slots 11/12-15/18-19/21-22/24/26 all fold to
    // generic ICF stubs here (CEquip::DisableController / MetaList /
    // Archetype / RDLNode), i.e. IdleBehavior genuinely does NOT override
    // IsClearanceState/SubmitGotoDirective/GetWatchedRawBlock/etc the way
    // DockBehavior does.
    //
    //   0  (0x00) FUN_062c7090        item 1320  scalar-deleting-dtor thunk (MSVC boilerplate;
    //                                            shared verbatim across 7 unrelated vtables --
    //                                            tail-calls thunk_FUN_062d2080 then conditionally
    //                                            frees, NO class-specific cleanup at all)
    //   1  (0x04) LAB_062cd590        item 1411  ported (outer logic): SetContext()
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not IdleBehavior-specific
    //   3  (0x0c) LAB_062cd5d0        item 1412  ported for real: ResetTickState() (tail-calls
    //                                            the shared FUN_062d20d0, `this` unchanged)
    //   4  (0x10) FUN_062cd5e0        item 1413  ported (outer logic): Disengage()
    //   5  (0x14) LAB_062ce3c0        item 1430  ported for real: SetModeFromFlags()
    //   6  (0x18) LAB_062cf5e0        item 1460  trivial ICF-shared "return 2" stub (14-way
    //                                            fold, ownership genuinely ambiguous per that
    //                                            item's own ``blocked`` notes) -- no dedicated
    //                                            method, documented here only
    //   7  (0x1c) FUN_062cd600        item 1414  ported (outer logic): Update()
    //   8  (0x20) Exclusion::AsteroidExclusionZone..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) LAB_062cd630        item 1415  ported for real: RequestFlag() -- the SAME
    //                                            retail function as FormationBehavior's own
    //                                            slot 10 (BaseFollowerBehavior_DefaultRequestFlag,
    //                                            common_behavior.h/.cpp); reproduced here as its
    //                                            own small method rather than cast to that
    //                                            unrelated type (see class comment above)
    //  11  (0x2c) CEquip::DisableController        ICF fold (DockBehavior's equivalent slot,
    //                                               IsClearanceState, is a REAL override there --
    //                                               IdleBehavior does not override this slot)
    //  12  (0x30) MetaList<GoodDesc>::operator[]    ICF fold
    //  13  (0x34) MetaList<GoodDesc>::operator[]    ICF fold
    //  14  (0x38) MetaList<GoodDesc>::operator[]    ICF fold
    //  15  (0x3c) MetaList<GoodDesc>::operator[]    ICF fold
    //  16  (0x40) FUN_062d2110        --         not reachable from any confirmed IdleBehavior
    //                                            call site this pass traced -- FULLPORT-DEFERRED
    //  17  (0x44) FUN_062d21b0        --         ditto
    //  18  (0x48) MetaList<GoodDesc>::operator[]    ICF fold
    //  19  (0x4c) Archetype::AttachedEquipment::i.. ICF fold
    //  20  (0x50) LAB_062c2f40        item 1262  trivial (2 insns), skipped (same as DockBehavior)
    //  21  (0x54) MetaList<GoodDesc>::operator[]    ICF fold
    //  22  (0x58) MetaList<GoodDesc>::operator[]    ICF fold
    //  23  (0x5c) LAB_062c2f50        item 1263  trivial (2 insns), skipped (same as DockBehavior)
    //  24  (0x60) RDLNode::operator_class_AttribS...ICF fold
    //  25  (0x64) CEShield::GetEquipDesc            ICF fold
    //  26  (0x68) RDLNode::operator_class_AttribS...ICF fold (same target as slot 24)
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 202736-202765
    // (LAB_062cd590), 28 instructions. Forwards Param1/Param2/Param3 verbatim
    // to the shared FUN_062d2090(this, P1, P2, P3) (see class comment); if
    // it returns false (retail's P1/P3-null check), returns false here too
    // with no further side effect. Otherwise calls IdleWatcher's own vtable
    // slot 0 with args drawn from the now-populated OwnerContextPtr14/
    // NavHelperPtr24 fields plus the raw incoming Param2 -- FULLPORT-DEFERRED
    // (IdleWatcher's real class isn't identified, so this vcall cannot be
    // made safely; see .cpp for the exact retail arg order this pass traced).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 202772-202773
    // (LAB_062cd5d0), a pure `JMP FUN_062d20d0` tail-call with `this`
    // unchanged. FUN_062d20d0's own body (decomp-tracker item 1533,
    // Common.dll.txt 210736-210749) is fully understood and safe to
    // reproduce for THIS call site: `this[0xc]=1; this[0x10]=1;
    // this[0x28] &= ~1; this[0x1c]=0`. (Item 1533 is marked `simplified`
    // globally because most of its OTHER 8 call sites belong to an
    // unrelated, not-yet-ported GUI text-widget class -- that caveat does
    // not apply to this specific, fully-modeled IdleBehavior call site.)
    void ResetTickState();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 202786-202797
    // (FUN_062cd5e0), 8 instructions. Calls IdleWatcher's own vtable slot 4
    // (offset 0x10, `Reset()`-shaped -- FULLPORT-DEFERRED, IdleWatcher's
    // real class isn't identified) then tail-calls the shared
    // FUN_062d20f0(this) with `this` UNCHANGED (the full IdleBehavior*, not
    // the sub-object) -- decomp-tracker item 1534. That function's own body
    // reads BehaviorMgrPtr20 (+0x20), dereferences ITS +0xc field, and
    // (if non-null) zeroes THAT target's +0x64 field, then clears Flag29
    // (+0x29). Item 1534 is marked `simplified` globally against a
    // DIFFERENT confirmed usage (CExternalEquip::HardpointName's StringCache
    // entry). For the Behavior-family usage here the dereference-and-clear is
    // STILL FULLPORT-DEFERRED (only the unconditional Flag29 clear is
    // reproduced), but the reason is now narrower and worth stating exactly:
    // 2026-09-17 (claude:common-behavior-fields) established that the +0xc link
    // is NOT the obstacle -- on the siblings whose +0x20 is typed it is
    // IBehaviorManager::Pathfind (PathfindManager*), the same pointer the NAMED retail symbol
    // `IBehaviorManager::update_current_behavior_cruise` loads at .text:062d3bfa.
    // The obstacle is IdleBehavior's OWN +0x20, which that pass searched for and
    // could not type from this class (see the field comment below). The same tail
    // is now ported in full in common_delaybehavior.cpp / common_facebehavior.cpp /
    // common_guidebehavior.cpp. TODO: type the field, then copy that body here.
    void Disengage();

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 204100-204108
    // (LAB_062ce3c0), 7 instructions. Fully self-contained, no sub-object
    // dependency: `ModeSelector0xc = Flag29 ? 3 : 1;`. Shared (via linker
    // ICF) with two OTHER sibling classes' own slot-5 overrides at the exact
    // same vtable position (.rdata:0639fd60/0639ffb0, not investigated this
    // pass) -- decomp-tracker item 1430.
    int SetModeFromFlags();   // returns the mode (EAX), RET 4

    // Own vtable slot 7 (offset 0x1c). Common.dll.txt 202811-202833
    // (FUN_062cd600), 18 instructions. Confirmed 1:1 against both raw disasm
    // and Common.dll.c 103415-103425 (decomp-tracker item 1414, previously
    // tracked under an "unidentified mode class" placeholder name before
    // this pass's RTTI identification). If OwnerContextPtr14 (+0x14) is
    // null, sets UpdateResult10=3 (idle/no-target) and returns. Otherwise
    // calls IdleWatcher's own vtable slot 1 with DeltaTime1c as its sole
    // argument (return value discarded, matching FormationBehavior's
    // EvaluateWatcher-shape precedent in common_behavior.h) and sets
    // UpdateResult10=1.
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

    // Own vtable slot 10 (offset 0x28). Common.dll.txt 202839-202842
    // (LAB_062cd630), 3 instructions -- see class comment for the ICF-shared
    // identity with FormationBehavior's own slot 10
    // (BaseFollowerBehavior_DefaultRequestFlag, common_behavior.h/.cpp).
    // `Flags28 |= 0x2; return true;` (the one stack dword the retail `RET 4`
    // pops is never read by the body, hence no parameter here either --
    // matches BaseFollowerBehavior_DefaultRequestFlag's own `Unused` param
    // in spirit, just omitted since this port's callers don't need it).
    bool RequestFlag();

    // Slots 3/4/5/10 of retail's IdleBehavior vftable (.rdata:0639fc5c), wired 2026-10-08
    // (claude-code:cmn-ai2) -- until then the manager's Behavior* dispatch reached the base
    // bodies. Slot 3 is `JMP FUN_062d20d0` (Behavior::Slot3 itself); slot 5 (LAB_062ce3c0)
    // and slot 10 (LAB_062cd630) are `RET 4`, their argument unread.
    void Slot4() override { Disengage(); }                           // 4 FUN_062cd5e0
    int Slot5(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    bool Slot10(void*) override { return RequestFlag(); }            // 10 LAB_062cd630

public:
    // ------------------------------------------------------------------
    // Confirmed fields, in ascending absolute-offset order (own data starts
    // right after the inherited vfptr @ +0x00 -- IdleBehavior derives
    // directly from Behavior, no BaseFollowerBehavior fields in between, see
    // class comment above for the RTTI proof). Everything else is Opaque
    // padding. Struct is sized only to the smallest confirmed lower bound
    // (0x2c + 0x14 = 0x40) -- NOT asserted via sizeof() the way
    // FormationBehavior/DockBehavior are, since IdleWatcher's own true size
    // is not independently confirmed (see class comment).
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.      // +0x08. Set by SetContext() from
                               // `Param1->vtbl[0x20](Param1)` (via the shared
                               // FUN_062d2090) -- same role/offset as
                               // DockBehaviorFields::CachedHandle08.
    int ModeSelector0xc;       // +0x0c. Written by ResetTickState() (=1) and
                               // SetModeFromFlags() (1 or 3) -- same
                               // role/offset as DockBehaviorFields::ModeSelector0xc.
    int UpdateResult10;        // +0x10. Update()'s own tri-state result
                               // (1=active/3=idle) and ResetTickState()'s
                               // own write (=1) -- same role/offset as
                               // DockBehaviorFields::UpdateResult10.
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1); null-checked by
                               // Update(). Same role/offset as
                               // BaseFollowerBehavior::OwnerContext /
                               // DockBehaviorFields::ThreatSourcePtr14.
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime1c;         // +0x1c. Zeroed by ResetTickState(); passed to
                               // IdleWatcher's Evaluate(dt) call in Update().
                               // Same role/offset as
                               // BaseFollowerBehavior::DeltaTime.
    // +0x20. Set by SetContext() (Param2); read (and its own +0xc field dereferenced) by the
    // shared FUN_062d20f0 Disengage() tail-calls. Same role/offset as
    // BaseFollowerBehavior::BehaviorMgrPtr / DockBehaviorFields::Field20.
    //
    // LEFT `void*` DELIBERATELY. SEARCHED AND NOT FOUND, 2026-09-17
    // (claude:common-behavior-fields) -- a recorded negative result, not an unexamined field.
    // The lead is real (TrailBehavior's own +0x20 is PROVEN `IBehaviorManager*` at
    // .text:062cb094-062cb097, `MOV ECX,[EBP+0x20]` into
    // `CALL IBehaviorManager::get_camera_level_status`; FaceBehavior, GotoBehavior,
    // GuideBehavior and DelayBehavior were typed this same pass from their own such sites),
    // but the shared writer FUN_062d2090 has fourteen SetContext() callers and so cannot carry
    // a type across. Every instruction of IdleBehavior's own methods (.text:062cd530 ctor,
    // 062cd590 SetContext, 062cd5d0 ResetTickState, 062cd5e0 Disengage, 062cd600 Update,
    // 062cd630 RequestFlag -- .text:062cd530..062cd640) was scanned for `[reg + 0x20]` in any
    // form. There is not a single one. Note also that Disengage()'s own FUN_062d20f0 address is
    // ICF-shared with DelayBehavior's slot 4, which proves nothing about the receiver.
    // WHAT WOULD CLOSE IT: a site that loads +0x20 into ECX ahead of a call, or walks it to a
    // confirmed IBehaviorManager offset (+0x08 Sys, +0xc0 CurrentBehaviorIndex, +0xc4,
    // +0x110 ContentCbPtr, +0x118/+0x11c, +0x164 TurnSensitivity, +0x174 dispatch table).
    void* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3). Same
                               // role/offset as BaseFollowerBehavior::NavHelper /
                               // DockBehaviorFields::Field24.
    unsigned char Flags28;     // +0x28. Bit 0x1 cleared by ResetTickState();
                               // bit 0x2 set by RequestFlag(). Same role/offset
                               // as BaseFollowerBehavior::Flags28 /
                               // DockBehaviorFields::Flags28.
    unsigned char Flag29;      // +0x29. Read by SetModeFromFlags(); cleared by
                               // Disengage()'s FUN_062d20f0 tail-call. Same
                               // role/offset as DockBehaviorFields::Flag29.
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only (identical shape to
                               // DockBehaviorFields::Pad2a).

    IdleIdea Watcher2c;        // +0x2c. IDENTIFIED 2026-10-08: an IdleIdea (common_idea.h;
                               // ctor FUN_062d9b10, vtable 063a054c, RTTI COL 063a9bd0),
                               // 0x14 bytes. The notes below predate the identification.
                               // Confirmed vtable-having (SetContext()/
                               // Update()/Disengage() all call through
                               // `*(void**)&Watcher2c`), but this port still
                               // doesn't have a concrete class to attach to
                               // it (FULLPORT-DEFERRED at every call site).
                               // Size is a documented LOWER BOUND only (see
                               // class comment) -- no static_assert on
                               // sizeof(IdleBehavior) as a result.
};

static_assert(offsetof(IdleBehavior, CachedHandle08) == 0x08, "IdleBehavior::CachedHandle08 offset");
static_assert(offsetof(IdleBehavior, ModeSelector0xc) == 0x0c, "IdleBehavior::ModeSelector0xc offset");
static_assert(offsetof(IdleBehavior, UpdateResult10) == 0x10, "IdleBehavior::UpdateResult10 offset");
static_assert(offsetof(IdleBehavior, OwnerContextPtr14) == 0x14, "IdleBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(IdleBehavior, DeltaTime1c) == 0x1c, "IdleBehavior::DeltaTime1c offset");
static_assert(offsetof(IdleBehavior, BehaviorMgrPtr20) == 0x20, "IdleBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(IdleBehavior, NavHelperPtr24) == 0x24, "IdleBehavior::NavHelperPtr24 offset");
static_assert(offsetof(IdleBehavior, Flags28) == 0x28, "IdleBehavior::Flags28 offset");
static_assert(offsetof(IdleBehavior, Flag29) == 0x29, "IdleBehavior::Flag29 offset");
static_assert(offsetof(IdleBehavior, Watcher2c) == 0x2c, "IdleBehavior::Watcher2c offset");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x40`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(IdleBehavior) <= 0x40, "IdleBehavior must not exceed its retail size 0x40");
