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
// DelayBehavior -- AI "wait N seconds then notify" timer state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a9274 (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor @ .data:0x063ec54c (approx, ".?AVDelayBehavior@@"
//        name confirmed via the pTypeDescriptor pointer at .rdata:0x063a9280)
//     -> pClassHierarchyDescriptor @ .rdata:0x063a9264, numBaseClasses = 2,
//        BaseClassArray @ 0x063a9258 = [ DelayBehavior(self, numContained=1),
//                                        Behavior(numContained=0, mdisp=0,
//                                        the SAME shared Behavior
//                                        BaseClassDescriptor every other
//                                        sibling in this family reuses) ]
//
// Single-inheritance direct child of Behavior -- same shape as
// IdleBehavior/CruiseBehavior/NullBehavior, no secondary vtable.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062d0a10 (Common.dll.txt 207935-207956). Calls the shared
// base-init field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) (item 1529 precedent), default-constructs a
// still-unidentified sub-object at +0x2c via FUN_062d9b10 -- the EXACT SAME
// ctor function IdleBehavior's own IdleWatcher sub-object uses
// (common_idlebehavior.h/.cpp) -- then installs the vtable. Structurally
// this is IdleBehavior's own shape (context prefix + one embedded Watcher at
// +0x2c) with two extra float fields (+0x40/+0x44, see below) appended.
// ============================================================================

class DelayBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (20, "Delay"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit DelayBehavior(int InTypeId);
    ~DelayBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639ff9c, 27 slots, 0x0639ff9c-0x063a0004 inclusive --
    // byte distance to the next label (s_BEHAVIOR_ID_NONE_063a0008) confirms
    // this bound).
    //
    //   0  (0x00) FUN_062c7090        scalar-deleting-dtor thunk, ICF-shared across 7
    //                                 unrelated vtables (decomp-tracker item 1320) -- no
    //                                 DelayBehavior-specific cleanup exists anywhere in it
    //   1  (0x04) LAB_062d0a70        ported (outer logic): SetContext()
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not DelayBehavior-specific
    //   3  (0x0c) FUN_062d0ac0        ported for real: ResetTickState()
    //   4  (0x10) FUN_062cd5e0        ICF-shared, SAME retail address as IdleBehavior's
    //                                 own Disengage() (common_idlebehavior.h/.cpp) -- ported
    //                                 here as DelayBehavior's own method, same body
    //   5  (0x14) LAB_062ce3c0        ICF-shared, SAME retail address as Cruise/Idle's own
    //                                 SetModeFromFlags() -- ported here as DelayBehavior's
    //                                 own method, same body
    //   6  (0x18) LAB_062cf5e0        trivial `MOV EAX,2; RET 4` stub, ICF-shared 14-way
    //                                 fold (same one documented in common_idlebehavior.h's
    //                                 own slot 6), skipped
    //   7  (0x1c) FUN_062d0ae0        ported for real: Update()
    //   8  (0x20) Exclusion::AsteroidExclusionZon..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) LAB_062d0b40        ported for real: ApplyDirective()
    //  11  (0x2c) CEquip::DisableController        ICF fold
    //  12-15 (0x30-0x3c) MetaList<GoodDesc>::operator[]  ICF fold (x4)
    //  16  (0x40) FUN_062d2110        family-shared "audio cue" helper, not reachable from any
    //                                 confirmed DelayBehavior call site this pass traced -- FULLPORT-DEFERRED
    //  17  (0x44) FUN_062d21b0        ditto
    //  18  (0x48) MetaList<GoodDesc>::operator[]     ICF fold
    //  19  (0x4c) Archetype::AttachedEquipment::i..  ICF fold
    //  20  (0x50) LAB_062c2f40        trivial (2 insns), skipped
    //  21-22 (0x54-0x58) MetaList<GoodDesc>::operator[]  ICF fold (x2)
    //  23  (0x5c) LAB_062c2f50        trivial (2 insns), skipped
    //  24  (0x60) RDLNode::operator_class_AttribS...ICF fold
    //  25  (0x64) CEShield::GetEquipDesc            ICF fold
    //  26  (0x68) RDLNode::operator_class_AttribS...ICF fold (same target as slot 24)
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 207972-208004
    // (LAB_062d0a70), 31 instructions. Calls the shared Behavior-family
    // context initializer FUN_062d2090(this, P1, P2, P3) (see
    // common_cruisebehavior.cpp's SetContext for the fully-traced shared
    // body: null-checks P1/P3, writes CachedHandle08/OwnerContextPtr14/
    // NavHelperPtr24/BehaviorMgrPtr20). If it fails, returns false
    // immediately. Otherwise calls Watcher2c's own vtable slot 0 with args
    // (OwnerContextPtr14, NavHelperPtr24, Param2) -- the EXACT SAME call
    // shape IdleBehavior::SetContext() documents (FULLPORT-DEFERRED, the
    // Watcher sub-object's real class isn't identified) -- then zeroes
    // Field40 and Field44 (the delay-timer pair, see field comments below).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 208024-208029
    // (FUN_062d0ac0), 6 instructions. Tail-calls the shared FUN_062d20d0
    // reset-tick-state effect (`ModeSelector0xc=1; UpdateResult10=1;
    // Flags28&=~1;` -- see common_idlebehavior.cpp's ResetTickState() for
    // the fully-traced shared body) then additionally zeroes Field44 (the
    // delay accumulator).
    void ResetTickState();

    // Own vtable slot 4 (offset 0x10). ICF-shared, literal same retail
    // address as IdleBehavior::Disengage() (common_idlebehavior.cpp) --
    // reproduced here as DelayBehavior's own method with the identical body
    // (calls Watcher2c's own vtable slot 4 / `Reset()`-shaped, still
    // FULLPORT-DEFERRED -- Watcher2c's class really is unidentified).
    // 2026-09-17 (claude:common-behavior-fields): the shared FUN_062d20f0(this)
    // tail is now ported IN FULL, not reduced to its Flag29 clear. Retail
    // (Common.dll.txt 210752-210761) reads BehaviorMgrPtr20->+0x0c and, when
    // non-null, zeroes THAT object's +0x64 before clearing Flag29. The old
    // "BehaviorMgrPtr20's pointee class isn't identified" excuse is dead: it is
    // an IBehaviorManager (see the field), and +0x0c is that class's own
    // Pathfind (PathfindManager*) pointer -- the NAMED symbol
    // `IBehaviorManager::update_current_behavior_cruise` loads it at
    // .text:062d3bfa. What is STILL unidentified is the class +0x0c points AT,
    // so its +0x64 is written as a plain dword at a confirmed offset.
    void Disengage();

    // Own vtable slot 5 (offset 0x14). ICF-shared, literal same retail
    // address as CruiseBehavior/IdleBehavior's own SetModeFromFlags()
    // (common_cruisebehavior.cpp/common_idlebehavior.cpp) -- reproduced here
    // as DelayBehavior's own method with the identical body.
    int SetModeFromFlags();   // LAB_062ce3c0: returns the mode, RET 4

    // Own vtable slot 7 (offset 0x1c). Common.dll.txt 208050-208081
    // (FUN_062d0ae0), 30 instructions, fully self-contained except one
    // deferred Watcher call.
    //   if (OwnerContextPtr14 == nullptr) { UpdateResult10 = 3; return 3; }
    //   Watcher2c.vtbl[1](DeltaTime1c);   // Evaluate-shaped, FULLPORT-DEFERRED
    //   Field44 += DeltaTime1c;
    //   // FCOMP Field44 (post-add) against Field40; the notify branch below
    //   // is taken when Field44 is STRICTLY GREATER than Field40 (raw
    //   // FNSTSW/TEST AH,0x41 polarity, re-verified this pass)
    //   if (Field44 > Field40) {
    //       Field44 = <float bit-pattern 0xccbebc20>;  // fixed sentinel/reset value
    //       NotifyBehaviorDone(BehaviorMgrPtr20, 0x14, 0); // FUN_062d29a0, FULLPORT-DEFERRED,
    //                                                       // same shared helper LaunchBehavior::
    //                                                       // Disengage() documents (common_launchbehavior.h)
    //   }
    //   UpdateResult10 = 1;
    //   return 1;
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

    // Own vtable slot 10 (offset 0x28). Common.dll.txt 208098-208106
    // (LAB_062d0b40), 10 instructions, 1 parameter (a caller-owned
    // directive-shaped struct pointer). Sets Field40 = Param->Field_0c AND
    // Field44 = Param->Field_0c (the SAME incoming float value written to
    // both fields -- confirmed exactly as traced, not simplified; the
    // asymmetry with SetContext()/ResetTickState()'s own Field44=0 writes
    // is retail's, not a porting artifact), sets Flags28 bit 0x2, and always
    // returns true.
    bool ApplyDirective(void const* Param);

    // Slots 3/4/5/10 of retail's DelayBehavior vftable (.rdata:0639ff9c), wired 2026-10-08
    // (claude-code:cmn-ai2). Slot 4 is FUN_062cd5e0 (shared with IdleBehavior), slot 5
    // LAB_062ce3c0 (shared), slot 10 LAB_062d0b40; 5 and 10 are `RET 4`.
    void Slot3() override { ResetTickState(); }                      // 3 FUN_062d0ac0
    void Slot4() override { Disengage(); }                           // 4 FUN_062cd5e0
    int Slot5(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    bool Slot10(void* DirectiveOp) override { return ApplyDirective(DirectiveOp); } // 10

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Same shared
    // context-prefix shape as IdleBehavior (own data starts right after the
    // inherited vfptr @ +0x00 -- DelayBehavior derives directly from
    // Behavior, single inheritance, confirmed via RTTI above), plus one
    // embedded Watcher sub-object and two DelayBehavior-specific float
    // fields past it. Struct sized only to the smallest confirmed lower
    // bound (0x2c + 0x14 + 0x08 = 0x48) -- NOT asserted via sizeof() since
    // Watcher2c's own true size is not independently confirmed (see
    // IdleBehavior's identical caveat).
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.      // +0x08. Set by SetContext() from `Param1->vtbl[0x20](Param1)`
                               // (via the shared FUN_062d2090) -- same role/offset as
                               // IdleBehavior::CachedHandle08.
    int ModeSelector0xc;       // +0x0c. Written by ResetTickState() (=1) and
                               // SetModeFromFlags() (1 or 3).
    int UpdateResult10;        // +0x10. Update()'s own tri-state result (1=active/3=idle)
                               // and ResetTickState()'s own write (=1).
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1); null-checked by Update().
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime1c;         // +0x1c. Passed to Watcher2c's Evaluate(dt) call and
                               // accumulated into Field44 by Update().
    // +0x20. Set by SetContext() (Param2); read by Disengage()'s shared FUN_062d20f0 tail-call
    // and by Update()'s NotifyBehaviorDone() call.
    // TYPED 2026-09-17 (claude:common-behavior-fields) from DelayBehavior's OWN call site, not
    // by carrying a sibling's proof across: Update() at .text:062d0b14 does
    // `MOV ECX,dword ptr [ESI + 0x20]` (ESI is `this`, set at .text:062d0ae1) and four
    // instructions later `CALL FUN_062d29a0` -- the field goes straight into ECX as a
    // __thiscall receiver. FUN_062d29a0's whole body writes +0x118 and +0x11c, which is
    // exactly the pair IBehaviorManager writes on its OWN `this` inside its per-tick behavior
    // switch (.text:062d2ea9 `MOV dword ptr [ESI+0x118],0x2` / .text:062d2eb3
    // `MOV dword ptr [ESI+0x11c],EAX`, in a function whose ESI is proved IBehaviorManager by
    // its reads of +0xc0, +0xc4 and the +0x174 dispatch table -- all three named by exported
    // IBehaviorManager symbols). FUN_062d29a0 also sits inside the contiguous run of named
    // IBehaviorManager methods (between `set_state_graph` @062d28b0 and `get_scan_response`
    // @062d2b00), so the surviving /OPT:ICF copy is the IBehaviorManager one.
    // Cross-checked: GuideBehavior reaches the SAME FUN_062d29a0 off its own +0x20 and ALSO
    // reaches FUN_062d2a70 off it, whose prologue reads +0x08 and +0xc0 the way the named
    // symbols `get_scan_response` / `update_current_behavior_throttle` do.
    // The shared writer FUN_062d2090 was deliberately NOT used as evidence (fourteen callers).
    IBehaviorManager* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3).
    unsigned char Flags28;     // +0x28. Bit 0x1 cleared by ResetTickState(); bit 0x2 set by
                               // ApplyDirective(). Not otherwise read by anything this pass ported.
    unsigned char Flag29;      // +0x29. Read by SetModeFromFlags(); cleared by Disengage()'s
                               // shared FUN_062d20f0 effect.
    unsigned char Pad2a[0x2c - 0x2a];

    IdleIdea Watcher2c;        // +0x2c. IdleIdea (common_idea.h; ctor FUN_062d9b10 at
                               // .text:062d0a42), identified 2026-10-08, 0x14 bytes.
    float Field40;             // +0x40. Zeroed by SetContext(); set to Param->Field_0c by
                               // ApplyDirective(); compared (FCOMP) against Field44 in Update().
    float Field44;             // +0x44. Zeroed by SetContext()/ResetTickState(); ALSO set to
                               // Param->Field_0c by ApplyDirective() (see ApplyDirective()'s
                               // own comment); accumulated (+= DeltaTime1c) by Update(), and
                               // reset to a fixed sentinel bit-pattern (0xccbebc20) once it
                               // exceeds Field40.
};

static_assert(offsetof(DelayBehavior, CachedHandle08) == 0x08, "DelayBehavior::CachedHandle08 offset");
static_assert(offsetof(DelayBehavior, ModeSelector0xc) == 0x0c, "DelayBehavior::ModeSelector0xc offset");
static_assert(offsetof(DelayBehavior, UpdateResult10) == 0x10, "DelayBehavior::UpdateResult10 offset");
static_assert(offsetof(DelayBehavior, OwnerContextPtr14) == 0x14, "DelayBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(DelayBehavior, DeltaTime1c) == 0x1c, "DelayBehavior::DeltaTime1c offset");
static_assert(offsetof(DelayBehavior, BehaviorMgrPtr20) == 0x20, "DelayBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(DelayBehavior, NavHelperPtr24) == 0x24, "DelayBehavior::NavHelperPtr24 offset");
static_assert(offsetof(DelayBehavior, Flags28) == 0x28, "DelayBehavior::Flags28 offset");
static_assert(offsetof(DelayBehavior, Flag29) == 0x29, "DelayBehavior::Flag29 offset");
static_assert(offsetof(DelayBehavior, Watcher2c) == 0x2c, "DelayBehavior::Watcher2c offset");
static_assert(offsetof(DelayBehavior, Field40) == 0x40, "DelayBehavior::Field40 offset");
static_assert(offsetof(DelayBehavior, Field44) == 0x44, "DelayBehavior::Field44 offset");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x48`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(DelayBehavior) <= 0x48, "DelayBehavior must not exceed its retail size 0x48");
