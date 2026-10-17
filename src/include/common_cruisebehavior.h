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
// CruiseBehavior -- AI "engage cruise engine" state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a90dc (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor @ .data:0x063ebfd0, name ".?AVCruiseBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a90cc, numBaseClasses = 2,
//        BaseClassArray @ 0x063a90c0 = [ CruiseBehavior(self @0x063a90a8,
//                                        numContained=1),
//                                        Behavior(numContained=0, mdisp=0,
//                                        @0x063a8c78, the SAME shared
//                                        Behavior BaseClassDescriptor every
//                                        other sibling in this family
//                                        reuses) ]
//
// Single-inheritance direct child of Behavior, same shape as
// IdleBehavior/DockBehaviorFields -- no secondary vtable, no multiple
// inheritance. Full byte-level trace (self-consistency of COL -> CHD ->
// BaseClassArray -> both BaseClassDescriptors) re-verified this pass.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062ce360 (Common.dll.txt 204036-204047), 9 instructions --
// the SMALLEST constructor in the whole TractorArm-family so far: it calls
// the shared base-init helper FUN_062d2020(this) (it writes +0x04..+0x29, see Behavior in common_behavior.h) (item 1529 precedent) and
// installs the vtable. NO field is zeroed and NO sub-object is constructed --
// CruiseBehavior owns no embedded sub-objects at all; every field it reads is
// populated later, by SetContext(). This matches the task brief's own hint
// that some siblings in this family are much simpler than
// Dock/Formation/Follow.
//
// Destructor: slot 0 is FUN_062c7090, the SAME scalar-deleting-dtor thunk
// address already documented in common_idlebehavior.h/.cpp as shared
// verbatim across 7 unrelated vtables (decomp-tracker item 1320) -- pure MSVC
// boilerplate (tail-calls thunk_FUN_062d2080, conditionally frees), NO
// CruiseBehavior-specific cleanup exists anywhere in it.
//
// ----------------------------------------------------------------------------
// Vtable-slot findings of note for OTHER lanes:
//
//  - Slot 1 (SetContext, .rdata:0x0639fd50 -> LAB_062cf3e0) is a PURE
//    `JMP FUN_062d2090` tail-call with NO wrapper logic at all -- i.e.
//    CruiseBehavior::SetContext(P1,P2,P3) IS, byte-for-byte, the shared
//    Behavior-family context initializer already documented in
//    common_dockbehavior.h/.cpp (item 1532). This is the cleanest possible
//    confirmation of that shared helper's signature/effect: no extra
//    class-specific logic layered on top at all. LAB_062cf3e0 is ALSO used,
//    via the SAME literal address, by three other sibling vtables:
//    0x0639f650, 0x0639f8f0 (DrasticEvadeBehavior's own slot 1), and
//    0x0639feb8 -- confirming (independently of the task brief's own note)
//    that 0639f650 and 0639feb8 are genuine still-unidentified siblings in
//    this family whose own slot-1 SetContext is this exact shared thunk too.
//
//  - Slots 3/4 (Engage/Disengage) call the real, ALREADY-NAMED retail symbol
//    `ThrustManager::engage_cruise(ThrustManager*, bool)` directly (not an
//    anonymous FUN_ address) -- strong independent corroboration of the
//    CruiseBehavior identity: a class literally named "cruise" toggles the
//    engine's cruise-engage flag as its whole job. ThrustManager itself is
//    not yet a ported class in this codebase (see common_itlbehavior.h:212-215
//    for the identical situation on InstantTradeLaneBehavior's own
//    TurnOff()), so the call is represented here as a FULLPORT-DEFERRED
//    private helper (EngageCruiseThruster) rather than invented as a bare
//    extern call into an unported type.
//
//  - Slot 5 (SetModeFromFlags, .rdata:0x0639fd60 -> LAB_062ce3c0) is the
//    SAME literal address as IdleBehavior::SetModeFromFlags
//    (common_idlebehavior.h/.cpp) -- confirmed linker ICF, byte-identical
//    body (`ModeSelector0xc = Flag29 ? 3 : 1; return ModeSelector0xc;`).
//    Reproduced here as CruiseBehavior's own method (not a cast to the
//    unrelated IdleBehavior type), matching this marathon's established
//    policy on ICF folds between sibling classes with matching field layout.
//    A THIRD sibling vtable slot at 0x0639ffb0 shares the same address too
//    (not investigated this pass).
//
//  - Slot 6 (.rdata:0x0639fd64 -> LAB_062d1730) is a trivial 2-instruction
//    `return 1;` stub, ICF-shared with TWO other still-unidentified sibling
//    slots (0x0639f5f4, 0x063a0258) -- documented, not given its own method
//    (matches the DockBehavior precedent of skipping 2-instruction ICF
//    trivia).
// ============================================================================

class CruiseBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (12, "Cruise"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit CruiseBehavior(int InTypeId);
    ~CruiseBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fd4c, 27 slots, 0x0639fd4c-0x0639fdb4 inclusive --
    // byte distance to the next COL/vtable head at 0x0639fdb8/0x0639fdbc
    // confirms this bound).
    //
    //   0  (0x00) FUN_062c7090        scalar-deleting-dtor thunk, ICF-shared (see class comment)
    //   1  (0x04) LAB_062cf3e0        ported for real: SetContext() (pure tail-call to FUN_062d2090)
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not CruiseBehavior-specific
    //   3  (0x0c) FUN_062ce380        ported for real: Engage()
    //   4  (0x10) FUN_062ce3a0        ported for real: Disengage()
    //   5  (0x14) LAB_062ce3c0        ported for real: SetModeFromFlags() (ICF-shared w/ IdleBehavior)
    //   6  (0x18) LAB_062d1730        trivial `return 1;` stub, ICF-shared (see class comment), skipped
    //   7  (0x1c) LAB_062ce3e0        ported for real: Update()
    //   8  (0x20) Exclusion::AsteroidExclusionZon..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) LAB_062c2f40        trivial (2 insns), skipped (same as Dock/Idle's own trivial slots)
    //  11  (0x2c) CEquip::DisableController        ICF fold
    //  12-15 (0x30-0x3c) MetaList<GoodDesc>::operator[]  ICF fold (x4)
    //  16  (0x40) FUN_062d2110        family-shared "audio cue" helper, not reachable from any
    //                                 confirmed CruiseBehavior call site this pass traced -- FULLPORT-DEFERRED
    //  17  (0x44) FUN_062d21b0        ditto
    //  18  (0x48) MetaList<GoodDesc>::operator[]     ICF fold
    //  19  (0x4c) Archetype::AttachedEquipment::i..  ICF fold
    //  20  (0x50) LAB_062c2f40        trivial, same target as slot 10
    //  21-22 (0x54-0x58) MetaList<GoodDesc>::operator[]  ICF fold (x2)
    //  23  (0x5c) LAB_062c2f50        trivial (2 insns), skipped
    //  24  (0x60) RDLNode::operator_class_AttribS...ICF fold
    //  25  (0x64) CEShield::GetEquipDesc            ICF fold
    //  26  (0x68) RDLNode::operator_class_AttribS...ICF fold (same target as slot 24)
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 205851-205852
    // (LAB_062cf3e0), a PURE `JMP FUN_062d2090` -- see class comment. Retail
    // effect (matching the already-documented shared helper exactly):
    //   if (Param1 == nullptr || Param3 == nullptr) return false;
    //   OwnerContextPtr14 = Param1;
    //   CachedHandle08 = Param1->vtbl[0x20](Param1);
    //   NavHelperPtr24 = Param3;
    //   BehaviorMgrPtr20 = Param2;
    //   return true;
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 204058-204068
    // (FUN_062ce380), 9 instructions. Tail-calls the shared FUN_062d20d0
    // reset helper first (already-documented effect, see
    // common_idlebehavior.h's ResetTickState: `ModeSelector0xc=1;
    // UpdateResult10=1; Flags28&=~1; DeltaTime1c=0;` -- CruiseBehavior has
    // no DeltaTime1c field of its own since nothing else in this class
    // reads/writes +0x1c, so only the three confirmed-relevant effects are
    // reproduced), then calls the real named retail symbol
    // `ThrustManager::engage_cruise(NavHelperPtr24+0x18, true)`.
    void Engage();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 204081-204091
    // (FUN_062ce3a0), 10 instructions, fully self-contained. Calls
    // `ThrustManager::engage_cruise(NavHelperPtr24+0x18, false)` then
    // tail-calls the shared FUN_062d20f0(this) (already-documented effect,
    // see common_idlebehavior.h's Disengage(): dereferences
    // BehaviorMgrPtr20->+0xc and, if non-null, zeroes THAT target's +0x64
    // field, then clears Flag29). STILL FULLPORT-DEFERRED, but the REASON is
    // narrower than it was: 2026-09-17 (claude:common-behavior-fields) searched
    // every instruction of this class for a +0x20 dereference and found none,
    // so BehaviorMgrPtr20's own type is still open HERE (see the field comment).
    // For the four siblings whose +0x20 was typed this pass -- FaceBehavior,
    // GotoBehavior, GuideBehavior, DelayBehavior -- this exact shared tail is
    // now ported in full, so the moment CruiseBehavior's field is typed this
    // becomes a copy-paste, not an investigation. TODO: type the field, then
    // port this tail the way common_delaybehavior.cpp's Disengage() now does.
    void Disengage();

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 204100-204108
    // (LAB_062ce3c0), 7 instructions -- ICF-shared with IdleBehavior's own
    // slot 5 (see class comment). Fully self-contained.
    int SetModeFromFlags();   // LAB_062ce3c0, returns the mode, RET 4

    // Own vtable slot 7 (offset 0x1c). Common.dll.txt 204118-204126
    // (LAB_062ce3e0), 8 instructions, fully self-contained -- the simplest
    // Update() anywhere in this family so far: no candidate-search, no
    // state machine, just an OwnerContextPtr14 presence check.
    //   UpdateResult10 = (OwnerContextPtr14 != nullptr) ? 1 : 3;
    //   return UpdateResult10;
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

    // Slots 3/4/5/6 of retail's CruiseBehavior vftable (.rdata:0639fd4c), wired 2026-10-08
    // (claude-code:cmn-ai2). Slot 6 is LAB_062d1730 `MOV EAX,1; RET 4` (shared with
    // FreeFlightBehavior slot 6 and NullBehavior slot 5).
    void Slot3() override { Engage(); }                              // 3 FUN_062ce380
    void Slot4() override { Disengage(); }                           // 4 FUN_062ce3a0
    int Slot5(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    EvaluateResult evaluate(unsigned int) override { return static_cast<EvaluateResult>(1); } // 6

    // The ThrustManager::engage_cruise stand-in (EngageCruiseThruster) is gone 2026-10-08:
    // ThrustManager is ported (common_systemmanager.h, SystemManager::Thrust at +0x18).

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. CruiseBehavior owns
    // NO sub-objects (see class comment) -- every field here is part of the
    // shared Behavior-family "context" prefix already documented identically
    // on DockBehaviorFields/IdleBehavior at the SAME absolute offsets. Struct
    // sized only to the smallest confirmed bound (0x2a) -- nothing past
    // Flag29 is read or written by any traced CruiseBehavior call site.
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.      // +0x08. Set by SetContext() (== FUN_062d2090) from
                               // `Param1->vtbl[0x20](Param1)`. Same role/offset as
                               // DockBehaviorFields::CachedHandle08 / IdleBehavior::CachedHandle08.
    int ModeSelector0xc;       // +0x0c. Written by Engage() (=1, via the shared
                               // FUN_062d20d0 effect) and SetModeFromFlags() (1 or 3).
    int UpdateResult10;        // +0x10. Update()'s own tri-state result (1=active/3=idle)
                               // and Engage()'s own write (=1, via FUN_062d20d0).
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1); null-checked by Update().
    unsigned char Opaque18[0x20 - 0x18];
    // +0x20. Set by SetContext() (Param2); read by Disengage()'s shared FUN_062d20f0 tail-call.
    //
    // LEFT `void*` DELIBERATELY. SEARCHED AND NOT FOUND, 2026-09-17
    // (claude:common-behavior-fields) -- this is a recorded negative result, not an
    // unexamined field.
    //
    // The lead is real: the same +0x20 on TrailBehavior IS proven to be an `IBehaviorManager*`
    // (.text:062cb094 `MOV ECX,dword ptr [EBP + 0x20]` immediately followed by
    // `CALL IBehaviorManager::get_camera_level_status`, a mangled retail symbol), and
    // FaceBehavior / GotoBehavior / GuideBehavior / DelayBehavior were all typed this same pass
    // from their own equivalent sites. The shared writer FUN_062d2090 still does NOT carry it
    // across: fourteen separate SetContext() bodies call it, so Param2's type is supplied from
    // outside and must be established per class.
    //
    // WHAT WAS SEARCHED: every instruction of CruiseBehavior's own five methods
    // (.text:062ce360 ctor, 062ce380 Engage, 062ce3a0 Disengage, 062ce3c0 SetModeFromFlags,
    // 062ce3e0 Update -- .text:062ce360..062ce3f0 inclusive) was scanned for any operand of the
    // form `[reg + 0x20]`. There is not one, of any shape: no `MOV reg,[this+0x20]`, no
    // `PUSH dword ptr [this+0x20]`, no `[[this+0x20]+disp]` chain. Retail writes this field in
    // SetContext() and the only thing that ever reads it back is the SHARED helper
    // FUN_062d20f0, which is called by a dozen unrelated classes and therefore cannot type it
    // for CruiseBehavior any more than FUN_062d2090 can.
    //
    // WHAT WOULD CLOSE IT: a site -- here or in a not-yet-ported module that calls into
    // CruiseBehavior -- that loads +0x20 into ECX ahead of a call, or walks it to one of
    // IBehaviorManager's own confirmed offsets (+0x08 Sys, +0xc0 CurrentBehaviorIndex,
    // +0xc4 directive, +0x110 ContentCbPtr, +0x118/+0x11c, +0x164 TurnSensitivity,
    // +0x174 BehaviorDispatchTable). Until then the type stays open.
    void* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3). Engage()/Disengage()
                               // pass `(char*)NavHelperPtr24 + 0x18` as the ThrustManager*
                               // to ThrustManager::engage_cruise.
    unsigned char Flags28;     // +0x28. Cleared bit 0x1 by Engage()'s shared FUN_062d20d0 effect.
                               // Not otherwise read by anything this pass ported.
    unsigned char Flag29;      // +0x29. Read by SetModeFromFlags(); cleared by Disengage()'s
                               // shared FUN_062d20f0 effect.
};

static_assert(offsetof(CruiseBehavior, CachedHandle08) == 0x08, "CruiseBehavior::CachedHandle08 offset");
static_assert(offsetof(CruiseBehavior, ModeSelector0xc) == 0x0c, "CruiseBehavior::ModeSelector0xc offset");
static_assert(offsetof(CruiseBehavior, UpdateResult10) == 0x10, "CruiseBehavior::UpdateResult10 offset");
static_assert(offsetof(CruiseBehavior, OwnerContextPtr14) == 0x14, "CruiseBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(CruiseBehavior, BehaviorMgrPtr20) == 0x20, "CruiseBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(CruiseBehavior, NavHelperPtr24) == 0x24, "CruiseBehavior::NavHelperPtr24 offset");
static_assert(offsetof(CruiseBehavior, Flags28) == 0x28, "CruiseBehavior::Flags28 offset");
static_assert(offsetof(CruiseBehavior, Flag29) == 0x29, "CruiseBehavior::Flag29 offset");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x2c`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(CruiseBehavior) <= 0x2c, "CruiseBehavior must not exceed its retail size 0x2c");
