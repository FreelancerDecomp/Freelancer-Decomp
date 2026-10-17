// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher, Watchable
#include "common_csys.h"      // Matrix
#include "common_ini.h"       // Vector
#include "common_misc.h"      // PlayerTrailAvailable
#include "common_idea.h"      // MoveIdea, FaceIdea -- real classes behind SubObjectA/B (see below)

#include <cstddef>

// Forward declaration only -- NavHelperPtr24 is a SystemManager* (see that field's comment for
// the four independent proofs). The full definition is pulled in by common_facebehavior.cpp,
// which is the only translation unit that dereferences it; keeping it out of this header avoids
// dragging common_systemmanager.h into every consumer.
class SystemManager;

// ============================================================================
// FaceBehavior -- AI "point at target" state machine (Behavior family)
// ============================================================================
//
// RTTI identity, from decomp-tracker item 6586's own prior-pass writeup
// (module Common.dll, address 0x0639f95c -- re-verified this pass against the
// raw disassembly, see the constructor trace below):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a8e94 (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor, name ".?AVFaceBehavior@@"
//     -> pClassHierarchyDescriptor, numBaseClasses = 2, i.e. a direct
//        single-inheritance child of Behavior -- same shape as
//        FleeBehavior/LootBehavior/IdleBehavior, NOT a multiple-inheritance
//        layout like DockBehavior (no secondary vtable, no mdisp=0x2C base).
//        The sibling entry at the same ClassHierarchyDescriptor level is
//        StrafeBehavior (TypeDescriptor immediately adjacent) -- not ported
//        by this pass.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062c88e0 (Common.dll.txt 195675-195737), re-read in full
// this pass. Calls the shared base-init field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) (item 1529
// precedent, nothing to reproduce), then constructs THREE sub-objects using
// the SAME shared helper functions DockBehavior/LaunchBehavior/LootBehavior
// already use:
//   +0x68 via FUN_062d4e60 ("SubObjectA", 0x78 bytes, ends +0xe0) --
//          IDENTIFIED 2026-09-11 (d285:common-port) as real MoveIdea (common_idea.h)
//   +0xe0 via FUN_062d6750 ("SubObjectB", 0xc0 bytes, ends +0x1a0) --
//          IDENTIFIED 2026-09-11 (d285:common-port) as real FaceIdea (common_idea.h)
//   +0x1a0 via FUN_062da500 ("SubObjectC", 0x14 bytes, ends +0x1b4) --
//          IDENTIFIED 2026-09-17 (claude:w18-behavior) as real AfterburnerIdea
//          (common_idea.h). The older reading of this line ("DockBehavior's own
//          GotoDirectiveHandle ctor, 0x18 bytes, still unidentified") was stale
//          on both the size and the identity -- see the SETTLED paragraph below.
// -- but UNLIKE DockBehavior, FaceBehavior's own ctor also directly zeroes
// TargetKind2c/Watcher30 (own fields, BEFORE the three CALLs above -- see the
// `LEA EDI,[ESI+0x30]` block) and a further block of own fields AFTER the
// three CALLs (+0x1c5/+0x1c8/+0x1cc/+0x1b4/+0x1c4=1/+0x1d0=0.9f/+0x1b8/+0x1bc/
// +0x1c0/+0x1d4/+0x1d8 -- see the field list below). Installs the vtable
// (`MOV [ESI],PTR_FUN_0639f95c`) between the sub-object construction and that
// trailing field-init block. The dead-code `CMP [this+0x30],0; JZ` guard
// right after vtable install (always taken, since +0x30 was just zeroed two
// instructions earlier) is the SAME defensive-clear idiom already documented
// on LootBehavior's own ctor -- not modeled as a real ctor effect here.
//
// NOTE on the +0x1a0 sub-object: unlike DockBehavior's own GotoDirectiveHandle
// (where the shape at relative +0x14 is a confirmed full pointer, NavPilotPtr,
// dereferenced directly by several DockBehavior methods), FaceBehavior's own
// ctor writes only a single BYTE at that same relative offset (absolute
// +0x1b4), and several of FaceBehavior's OWN vtable-slot methods (Update(),
// SetTargetOffset()) read/write +0x1b4 directly as a plain flag byte -- never
// through the sub-object's own vtable. This pass therefore models only the
// first 0x14 bytes of the nominal 0x18-byte FUN_062da500 span as the
// SubObjectC object, and treats the trailing byte (+0x1b4) as FaceBehavior's
// own Flag1b4 field instead of a sub-object-internal pointer -- see the field
// list below.
//
// SETTLED 2026-09-17 (claude:w18-behavior): the "nominal 0x18-byte span" was
// never FUN_062da500's own size -- it was DockBehavior's reading of ITS use of
// the same ctor. FUN_062da500 constructs an AfterburnerIdea (RTTI
// ".?AVAfterburnerIdea@@" via .rdata:PTR_LAB_063a0584), which adds no fields
// beyond BaseIdea and is therefore exactly 0x14 bytes. So the split this
// paragraph argued for on behavioural grounds is also the literal retail
// layout, and SubObjectC is now a real typed member rather than a byte buffer.
// ============================================================================

class FaceBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (15, "Face"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit FaceBehavior(int InTypeId);

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639f95c, 27 slots, 0x0639f95c-0x0639f9c4 inclusive).
    // Slot -> retail address -> notes. ICF-fold slots (shared byte-identical
    // code with unrelated classes -- CEquip::ConnectServerEquip,
    // Exclusion::AsteroidExclusionZone::.., IObjInspectImpl::cobject,
    // CEquip::DisableController, MetaList<GoodDesc>::operator[],
    // Archetype::AttachedEquipment::i.., CEShield::GetEquipDesc,
    // RDLNode::operator_class_AttribS..) carry no FaceBehavior-specific logic
    // and are not touched by this port, matching every sibling class's own
    // documented precedent for this exact fold set.
    //
    //   0  (0x00) FUN_062c8a10        scalar-deleting-dtor thunk -> real dtor FUN_062c8a30
    //   1  (0x04) LAB_062c8ac0        ported (outer logic): SetContext()
    //   2  (0x08) CEquip::ConnectServerEquip          ICF fold, skip
    //   3  (0x0c) FUN_062c9320        ported for real: ResetApproachState()
    //   4  (0x10) FUN_062c9380        ported for real: Disengage()
    //   5  (0x14) LAB_062c8c10        ported (outer logic + safe-default tail): SetModeFromFlags()
    //   6  (0x18) LAB_062c8d80        ported (outer logic): RefreshPlayerTrailState()
    //   7  (0x1c) LAB_062c8de0        PORTED IN FULL 2026-09-17 (all 410 insns): Update()
    //   8  (0x20) Exclusion::AsteroidExclusionZone..  ICF fold, skip
    //   9  (0x24) IObjInspectImpl::cobject            ICF fold, skip
    //  10  (0x28) LAB_062c8b30        ported for real: ApplyTargetMessage()
    //  11  (0x2c) CEquip::DisableController           ICF fold, skip
    //  12  (0x30) FUN_062c93c0        ported (outer logic + GAP tail): SetTargetOffset()
    //  13  (0x34) LAB_062c9530        ported for real (trivial): RequestAbort()
    //  14  (0x38) MetaList<GoodDesc>::operator[]      ICF fold, skip
    //  15  (0x3c) MetaList<GoodDesc>::operator[]      ICF fold, skip
    //  16  (0x40) MetaList<GoodDesc>::operator[]      ICF fold, skip
    //  17  (0x44) FUN_062d21b0        family-shared "audio cue" helper, no confirmed FaceBehavior
    //                                 call site this pass traced -- FULLPORT-DEFERRED
    //  18  (0x48) MetaList<GoodDesc>::operator[]      ICF fold, skip
    //  19  (0x4c) Archetype::AttachedEquipment::i..   ICF fold, skip
    //  20  (0x50) LAB_062c2f40        trivial (2 insns), skip (same target every sibling shares)
    //  21  (0x54) LAB_062c89c0        ported for real: OnWatchedObjectDestroyed()
    //  22  (0x58) MetaList<GoodDesc>::operator[]      ICF fold, skip
    //  23  (0x5c) LAB_062c2f50        trivial (2 insns), skip
    //  24  (0x60) LAB_062c8a00        ported for real: GetWatchedRawBlock()
    //  25  (0x64) CEShield::GetEquipDesc              ICF fold, skip
    //  26  (0x68) RDLNode::operator_class_AttribS..   ICF fold, skip
    // ------------------------------------------------------------------

    // Real (non-thunk) destructor. FUN_062c8a10 (item's slot 0) is the
    // scalar-deleting-dtor thunk that calls FUN_062c8a30 (Common.dll.txt
    // 195815-195860) then conditionally frees. FUN_062c8a30 clears THREE
    // BaseWatcher-shaped fields via BaseWatcher::set_pointer(nullptr), in this
    // order: this+0xf4, this+0x7c, this+0x30. WIRED FOR REAL 2026-09-11
    // (d285:common-port): +0xf4/+0x7c are EXACTLY SubObjectB::Watcher/
    // SubObjectA::Watcher (FaceIdea/MoveIdea's own +0x14 BaseWatcher field, at
    // absolute 0xe0+0x14=0xf4 / 0x68+0x14=0x7c) -- independent corroboration
    // of the MoveIdea/FaceIdea identification, same finding as LaunchBehavior/
    // LootBehavior's own dtors. this+0x30 (Watcher30) was already modeled for
    // real. Retail then tail-calls the shared FUN_062d2080(this,-1) cleanup
    // (FULLPORT-DEFERRED, same shared helper DockBehavior's own dtor calls).
    ~FaceBehavior() override;

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 195870-195913
    // (LAB_062c8ac0), 68 instructions. Calls the shared Behavior-family
    // context initializer FUN_062d2090(this, Param1, Param2, Param3) (same
    // body as DockBehavior::SetContext / FleeBehavior::SetContext); if it
    // returns false, returns false immediately. Otherwise calls SubObjectB's
    // and SubObjectC's own vtable slot 0 -- SubObjectB's call WIRED FOR REAL
    // 2026-09-11 (d285:common-port), now identified as FaceIdea; arg shape
    // `(Param1, Param3, Param2)` re-verified from Common.dll.txt 195886-195899
    // byte-for-byte (same order as every sibling's own SetContext calls, see
    // e.g. DockBehavior::SetContext). SubObjectC's call is ALSO WIRED FOR REAL
    // as of 2026-09-17 (claude:w18-behavior) -- it is an AfterburnerIdea and
    // slot 0 is BaseIdea::SetContext, same three-push shape. NOTE: unlike
    // DockBehavior's SetContext, FaceBehavior's own retail body does NOT also
    // call SubObjectA's slot 0 here (re-confirmed this pass -- SubObjectA's
    // own vtable pointer at +0x68 is simply never read in this function).
    // Then computes `Field1cc = FUN_062e5b20(ThreatSourcePtr14, 0, 1.0f)`,
    // PORTED 2026-09-17 (claude:w18-behavior) as Behavior_InvokeManagerSlotF0
    // (item 1763, common_behavior.h) -- the recorded "not yet ported" reason
    // was stale -- and returns true. NOTHING in this method is deferred now.
    // WIRED 2026-10-08 (claude-code:cmn-ai4): an override of Behavior slot 1; the binder is
    // Behavior::BindContext (it was inlined by hand before).
    bool SetContext(void* Param1, void* Param2, void* Param3) override;

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 196670-196695
    // (FUN_062c9320), 34 instructions, fully traced (two deferred shared
    // helper calls). Resets several per-approach fields:
    // Flag1c5/Field1c8/Flag1b4/Field1b8/Field1bc/Field1c0/Field1d8 all -> 0,
    // plus `SetCruiseLock(0, 3)` (FUN_062d21d0, FULLPORT-DEFERRED, shared
    // name/role with DockBehavior/FormationBehavior's own deferred
    // SetCruiseLock) and `Behavior_NotifyRegistrationFlag(NavHelperPtr24+0x18,
    // Flag1c4==0)` (FUN_062e91d0 -- already a documented FULLPORT-DEFERRED
    // no-op in common_behavior.cpp, reused directly here).
    // CORRECTED 2026-10-08 (claude-code:cmn-ai4): retail starts with Behavior::Slot3
    // (FUN_062d20d0) and calls the real Behavior::SetCruiseLock(0, 3); both were stubs.
    void Slot3() override;

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 196706-196727
    // (FUN_062c9380), 20 instructions, fully traced. If Flags28 bit 0x2 is
    // clear and Watcher30 is non-null, clears Watcher30. Unconditionally
    // calls SubObjectB's and SubObjectC's own vtable slot 0x10 ("release"-
    // shaped, same convention DockBehavior::Disengage() uses) -- SubObjectB's
    // call WIRED FOR REAL 2026-09-11 (d285:common-port) as FaceIdea::Commit();
    // SubObjectC's call WIRED FOR REAL 2026-09-17 (claude:w18-behavior) as
    // AfterburnerIdea::Commit(), i.e. "afterburner off" --
    // then tail-calls the shared FUN_062d20f0(this) (PORTED IN FULL 2026-09-17,
    // claude:common-behavior-fields -- it needed BehaviorMgrPtr20's type and nothing else;
    // see the .cpp. What is left there is only IBehaviorManager+0x0c's own POINTEE class,
    // whose +0x64 is therefore written as a plain dword at a confirmed offset. Same
    // shared tail DockBehavior::Disengage() calls).
    void Slot4() override;

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 196016-196152
    // (LAB_062c8c10), 150+ instructions. Fast path (fully ported): if
    // Flags28 bit 0x2 is set, TargetKind2c==1, and Watcher30 is valid (raw
    // target non-null and its -8 adjust non-null), returns Flag29 ? 3 : 1
    // (also written into ModeSelector0xc). GAP:AI-FACE-STATE-MACHINE for the
    // "main" fallback branch (Common.dll.txt 196051-196152): re-derives
    // Watcher30 from a NavHelperPtr24-relative candidate lookup
    // (FUN_062e74a0), several NavHelperPtr24-nested gate checks (+0x1364 via
    // FUN_062ecb20, BehaviorMgrPtr20->+0xc->+0x260/+0x264, +0x16e4 via
    // FUN_062e8280), and a candidate-distance FPU computation against a
    // nested +0x14fc/+0x84 sub-object (magnitude-squared vs DAT_0639f9c8,
    // then a FUN_062e8830 bool check). PRECISION 2026-09-17
    // (claude:common-behavior-fields): "BehaviorMgrPtr20->+0xc" is no longer
    // one of the unknowns in that list -- BehaviorMgrPtr20 is an
    // IBehaviorManager* (see the field) and +0x0c is that class's own
    // Pathfind (PathfindManager*) pointer, loaded the same way by the NAMED retail symbol
    // `IBehaviorManager::update_current_behavior_cruise` at .text:062d3bfa.
    // What is still unidentified is the class that pointer points AT, whose
    // +0x260/+0x264 this branch reads. The rest of the list is unchanged and
    // still not reproduced. Retail's own safe default reaching the
    // tail without any of those resolving is 2 ("not ready"), which this
    // port reproduces directly (further overridden to 3 if Flag29 is set,
    // matching retail's own tail logic exactly).
    // PORTED IN FULL 2026-10-08 (claude-code:cmn-ai4) -- the paragraph above describes the
    // old gap; see the .cpp for the trace. RET 4, the argument is unread.
    int Slot5(float Unused) override;

    // Own vtable slot 6 (offset 0x18). Common.dll.txt 196164-196193
    // (LAB_062c8d80), 26 instructions, fully traced (one deferred helper
    // call). If the global PlayerTrailAvailable is false, returns 2.
    // Otherwise queries `ThreatSourcePtr14->vtbl[0x74](&out)` (FULLPORT-
    // DEFERRED, ThreatSourcePtr14's real class unidentified) and calls
    // FUN_062ee940(NavHelperPtr24+0x142c, out, SUBOBJ_ID_NONE) (FULLPORT-
    // DEFERRED), returning 1.
    // PORTED IN FULL 2026-10-08 (claude-code:cmn-ai4): get_target (+0x74) and
    // TargetManager::RetargetOwner (FUN_062ee940) are real calls now. RET 4.
    EvaluateResult evaluate(unsigned int Context) override;

    // Own vtable slot 7 (offset 0x1c), Update/dispatch entry point.
    // Common.dll.txt 196208-196651 (LAB_062c8de0), 410 instructions.
    //
    // PORTED IN FULL 2026-09-17 (claude:w18-behavior), replacing the
    // entry-guard-only stub and its GAP:AI-FACE-STATE-MACHINE note. Every
    // address in .text:062c8de0-062c9307 was hand-traced from the raw bytes
    // this pass; see the .cpp for the per-branch derivation and for the five
    // stale premises that note rested on (every single named "unported FUN_"
    // in it had already been ported by another lane, and the "unidentified
    // NavHelperPtr24-nested sub-object" is SystemManager::ExternalIface).
    //
    // NOTHING in this function is deferred any more. The last hold-out was
    // SubObjectC's two per-tick virtual calls, and SubObjectC turned out to be
    // an already-ported AfterburnerIdea -- see that field's comment.
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

    // Own vtable slot 10 (offset 0x28). Common.dll.txt 195918-196015
    // (LAB_062c8b30), ~100 instructions, fully traced (one deferred helper
    // call in case 1). A notification/message-apply handler: reads a message
    // struct (Message->+0x44 byte -> Flag1c4, Message->+0xc int -> a 1..4
    // "kind" selector -> TargetKind2c) and dispatches: kind 1 re-derives
    // Watcher30 from a NavHelperPtr24-relative lookup (FUN_062e74a0,
    // FULLPORT-DEFERRED -- this port always takes retail's own "candidate not
    // found" fallback, re-validating Watcher30 as-is); kind 2 copies a Vector
    // from Message->+0x14 into TargetPosition5c; kind 3 copies a 9-float
    // Matrix from Message->+0x20 into OrientMatrix38; kind 4 is a pure flag
    // set. Every reached case sets Flags28 bit 0x2 and returns true; a null
    // Message or an out-of-range kind clears the bit and returns false
    // (retail's own shared default path).
    // CORRECTED 2026-10-08 (claude-code:cmn-ai4): kind 1 now calls FUN_062e74a0 and re-points
    // Watcher30 at the result, as retail does.
    bool Slot10(void* DirectiveOp) override;

    // Own vtable slot 12 (offset 0x30). Common.dll.txt 196731-196841
    // (FUN_062c93c0), ~110 instructions. FULLY PORTED 2026-09-17
    // (claude:common-behavior-fields) -- BOTH of the GAP:AI-FACE-STATE-MACHINE
    // holes this comment used to describe are closed, and the old text is
    // replaced rather than annotated, because each rested on a false premise:
    //
    //  (1) "a magnitude-squared-vs-DAT_06399408 FCOM ... that (not-yet-resolved)
    //      threshold". DAT_06399408 is not unresolved: its eight raw .rdata
    //      bytes at 06399408 are all zero, so it is 0.0f. The gate is simply
    //      `Dot(Offset, Offset) > 0.0f`, i.e. "the offset is not the zero
    //      vector" -- reproduced exactly. (`TEST AH,0x41; JZ` at 062c93f8-
    //      062c93fd takes the set path only when both C0 and C3 are clear,
    //      which for FCOMP is strictly-greater.)
    //
    //  (2) "FUN_062d4b20 isn't ported". It is, and under its real name:
    //      FUN_062d4b20 is `IBehaviorManager::get_turn_sensitivity_factor()`
    //      (`FLD [ECX+0x164]; RET` -- +0x164 is IBehaviorManager::TurnSensitivity,
    //      and common_behaviormanager.cpp already carries the body, citing this very
    //      address). It is called at .text:062c9432 on ECX loaded from
    //      `[ESI+0x20]` four instructions earlier -- which is also this class's
    //      own proof that BehaviorMgrPtr20 is an IBehaviorManager*, see the
    //      field below.
    //
    // Full retail body now reproduced: on the set path Flag1b4=1,
    // Field1b8=-Offset.y, Field1bc=-Offset.x, Field1c0=Offset.z; then, when
    // get_turn_sensitivity_factor() is strictly greater than 0.0 (DAT_0639c430
    // is an all-zero double), both Field1b8 and Field1bc are multiplied by
    // 1.0f/Sensitivity (DAT_0639a1d0 == 1.0f); then each is clamped into
    // [-1.0f, +1.0f] (DAT_06399404 == -1.0f, DAT_0639a1d0 == +1.0f).
    void set_direction(Vector const& Offset) override;

    // Own vtable slot 13 (offset 0x34). Common.dll.txt 196853-196855
    // (LAB_062c9530), trivial one-instruction body: `Flag1d4 = 1`. Update()'s
    // own entry guard (slot 7) consumes this flag as an abort-request signal.
    // `MOV byte [ECX+0x1d4],1; RET 4` -- the throttle argument is unread.
    void set_throttle(float Throttle) override;

    // Slot 16 (0x40) is 062ed730, an empty `RET 4` -- FaceBehavior REPLACES Behavior's own
    // set_afterburner (FUN_062d2110) with nothing.
    void set_afterburner(bool bOn) override { (void)bOn; }

    // Own vtable slot 21 (offset 0x54). Common.dll.txt 195748-195776
    // (LAB_062c89c0), fully traced. If Watcher30 is valid (raw target and its
    // -8 adjust both non-null) and the adjusted block's own vtbl[0x20]()
    // ("get id") result matches Id, and Watcher30 is still non-null, clears
    // Watcher30.
    void Slot21(void* Arg) override;

    // Own vtable slot 24 (offset 0x60). Common.dll.txt 195781-195789
    // (LAB_062c8a00), fully traced: `EAX = Watcher30.target(); return EAX ?
    // (EAX-8) : 0;` -- same "payload pointer back to owning 8-byte-header
    // allocation block" convention as DockBehavior::GetWatchedRawBlock().
    IObjRW const* get_target() override;

    // The same `Ptr ? Ptr - 8 : 0` the slot-24 body computes, kept as the non-virtual helper
    // Update() uses (retail inlines it there).
    IObjRW* GetWatchedRawBlock() const;

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. The +0x04..+0x2c span is the shared Behavior-family
    // context prefix (same shape/roles as every sibling: CachedHandle08 is
    // set by SetContext() via a getter vcall on Param1; ThreatSourcePtr14/
    // BehaviorMgrPtr20/NavHelperPtr24 are Param1/Param2/Param3 respectively;
    // Flags28/Flag29 are the usual bit-flag pair). +0x2c onward is
    // FaceBehavior's own data, confirmed directly from the ctor and the
    // vtable-slot bodies ported above.
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.        // +0x08. Set by SetContext() via Param1's own vtbl+0x20 getter.
    int ModeSelector0xc;         // +0x0c. Shared context-prefix slot; not directly written by
                                  // anything this pass ported (SetModeFromFlags() writes it, see
                                  // the ModeSelector0xc assignments inside that method).
    int UpdateResult10;          // +0x10. Update()'s own tri-state result.
    void* ThreatSourcePtr14;     // +0x14. Set by SetContext() (Param1); read by
                                  // RefreshPlayerTrailState() and Update()'s GAP body.
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime1c;            // +0x1c. Per-tick delta time; read by Update()'s GAP body (feeds
                                   // Field1d8 and the SubObjectB/SubObjectC per-tick vcalls, all now real). Not
                                   // written by anything this pass ported (set by the owning
                                   // behavior-manager tick call, same convention as every sibling).
    // +0x20. Set by SetContext() (Param2); read by SetModeFromFlags()'s GAP branch and by
    // SetTargetOffset(), which is where the type comes from.
    // TYPED 2026-09-17 (claude:common-behavior-fields) from FaceBehavior's OWN call site, not
    // by carrying a sibling's proof across: .text:062c9429 `MOV ECX,dword ptr [ESI + 0x20]`
    // (ESI is `this`, set at .text:062c93c1) then .text:062c9432 `CALL FUN_062d4b20`, with
    // nothing touching ECX in between -- the field IS the __thiscall receiver. FUN_062d4b20's
    // entire body is `FLD float ptr [ECX + 0x164]; RET`, and +0x164 is IBehaviorManager::
    // TurnSensitivity; common_behaviormanager.cpp already ports it as
    // `IBehaviorManager::get_turn_sensitivity_factor()` and names 0x062d4b20 in its comment.
    // The receiver class of that folded two-instruction getter is settled independently of
    // FaceBehavior: (a) it sits immediately before the NAMED retail symbol
    // `IBehaviorManager::get_docking_port` (.text:062d4b40) in the contiguous IBehaviorManager
    // method run, so the surviving /OPT:ICF copy is IBehaviorManager's; and (b) two of its four
    // XREFs call it on a +0x20 field already proven to be an IBehaviorManager* -- .text:062cc9c7
    // (BaseFollowerBehavior, whose BehaviorMgrPtr common_behavior.h types from
    // `IBehaviorManager::lock_maneuvers`) and .text:062cb173.
    // Corroborating, from this class's OTHER dereference: .text:062c8c77
    // `MOV EAX,[ESI+0x20]; MOV EAX,[EAX+0xc]` walks IBehaviorManager+0x0c, the same pointer
    // field the NAMED symbol `IBehaviorManager::update_current_behavior_cruise` loads at
    // .text:062d3bfa.
    // The shared writer FUN_062d2090 was deliberately NOT used as evidence (fourteen callers).
    IBehaviorManager* BehaviorMgrPtr20;
    // +0x24. Set by SetContext() (Param3).
    //
    // TYPED 2026-09-17 (claude:w18-behavior): this is a `SystemManager*` (common_systemmanager.h),
    // and every "nested sub-object at a large raw offset" note attached to this field was really a
    // named SystemManager member all along. Proof, all from Update()'s own body:
    //   * `.text:062c8e0a ADD EAX,0x14fc; MOV EAX,[EAX+0x84]` is a SINGLE load of +0x1580 --
    //     NOT the double indirection prior notes described. +0x1580 is SystemManager::ExternalIface,
    //     the only field at that offset in the module (common_systemmanager.h's own FUN_062e92e0
    //     writeup makes exactly this argument), and the vtable slots this function calls on it
    //     (+0x0c, +0x2c, +0xbc) are IObjInspect::get_transform / get_physical_radius / is_player
    //     in common_iface.h's RTTI-verified table.
    //   * `.text:062c91b1 MOV ECX,[EBP+0x24]; ADD ECX,0x18; CALL FUN_062e92e0` passes Sys+0x18,
    //     which is SystemManager::Thrust -- CheckSystemManagerSteeringGate's own receiver.
    //   * `[+0x14fc+0xc/+0x18/+0x7e]` == CachedPhysicalCenter / CachedPhysicalRadius /
    //     Flags1578[2], and Update() inlines FUN_062e6d40's own refresh of exactly that triple.
    //   * +0x16d4/+0x16d8/+0x16dc/+0x16e0/+0x16e4 are the FIVE consecutive floats of
    //     PersonalityData.EngineKillParams (Personality+0x150 == absolute 0x16d4,
    //     pub::AI::Personality::EngineKillUseStruct, five fields, exact fit). That is what
    //     this whole class is: the engine-kill / coast-and-face manoeuvre, tuned per personality.
    SystemManager* NavHelperPtr24;
    unsigned char Flags28;       // +0x28. Bit 0x2 = "have a valid target" (set/cleared by
                                  // ApplyTargetMessage(), tested by SetModeFromFlags()/Disengage()).
    unsigned char Flag29;        // +0x29. Tested by SetModeFromFlags() (forces mode 3 either path).
    unsigned char Pad2a[0x2c - 0x2a];

    int TargetKind2c;             // +0x2c. Zeroed by ctor. "Target kind" selector (1=watched-object
                                   // candidate, 2=fixed Vector, 3=fixed Matrix, 4=flag-only) --
                                   // written by ApplyTargetMessage() from Message->+0xc, read by
                                   // SetModeFromFlags()/SetTargetOffset()/Update()'s GAP body.
    BaseWatcher Watcher30;        // +0x30 (8 bytes, ends +0x38). Zeroed by ctor (both dwords in one
                                   // instruction pair); real dtor clears it via
                                   // BaseWatcher::set_pointer(nullptr). GetWatchedRawBlock()/
                                   // OnWatchedObjectDestroyed()/SetModeFromFlags() all read it
                                   // directly; ApplyTargetMessage()'s kind-1 case re-validates it.
    Matrix OrientMatrix38;        // +0x38 (0x24 bytes, ends +0x5c). CONFIRMED this pass: a 9-dword
                                   // MOVSD.REP copy from Message->+0x20 in ApplyTargetMessage()'s
                                   // kind-3 case (Common.dll.txt 195966-195979) -- same "9-float
                                   // Matrix" shape/copy idiom DockBehavior::OrientMatrix3d8 uses.
    Vector TargetPosition5c;      // +0x5c (0xc bytes, ends +0x68). CONFIRMED this pass: a 3-dword
                                   // copy from Message->+0x14 in ApplyTargetMessage()'s kind-2 case
                                   // (Common.dll.txt 195980-195993).

    // +0x68. IDENTIFIED 2026-09-11 (d285:common-port): real, already-ported
    // MoveIdea (common_idea.h) -- ctor FUN_062d4e60 matches MoveIdea's own
    // confirmed ctor address; sizeof(MoveIdea)==0x78 matches this field's
    // span. UNLIKE every other sibling (DockBehavior/LaunchBehavior/
    // LootBehavior), FaceBehavior's retail SetContext()/Disengage() do NOT
    // call through SubObjectA's own vtable at all -- re-confirmed this pass
    // (Common.dll.txt 195870-195913 for SetContext, 196706-196727 for
    // Disengage) -- so there is no vtable call here to wire despite the
    // identification; the destructor's own +0x7c clear (SubObjectA::Watcher,
    // MoveIdea+0x14==0x68+0x14==0x7c) IS wired for real, see ~FaceBehavior().
    MoveIdea SubObjectA;    // +0x68.
    // +0xe0. IDENTIFIED 2026-09-11 (d285:common-port): real, already-ported
    // FaceIdea (common_idea.h) -- ctor FUN_062d6750 matches FaceIdea's own
    // confirmed ctor address; sizeof(FaceIdea)==0xc0 matches this field's
    // span. SetContext() now calls FaceIdea::SetContext() for real (own
    // vtable slot 0); Disengage() now calls FaceIdea::Commit() for real (own
    // vtable slot 4, retail `vtbl+0x10`). Its internal BaseWatcher (own
    // relative +0x14, absolute +0xf4) is cleared by the real dtor for real
    // too now (see ~FaceBehavior()). Update()'s GAP body still routes its own
    // SubObjectB vtbl+4 poke through a named FULLPORT-DEFERRED stub (the
    // surrounding GAP logic gating it is itself unported).
    FaceIdea SubObjectB;    // +0xe0.
    // +0x1a0. IDENTIFIED AND WIRED FOR REAL 2026-09-17 (claude:w18-behavior), replacing the
    // `unsigned char[0x14]` placeholder and the ConstructSubObjectC stand-in.
    //
    // The premise behind the placeholder -- "FUN_062da500's owning class is unidentified" -- was
    // STALE. common_idea.h has carried the answer since 2026-09-17: FUN_062da500 installs
    // .rdata:PTR_LAB_063a0584, whose RTTI Complete Object Locator names ".?AVAfterburnerIdea@@",
    // single inheritance from BaseIdea. That header even names "this marathon's SubObjectC" as the
    // call site. FaceBehavior's own ctor confirms the receiver: `.text:062c8934 LEA ECX,[ESI+0x1a0];
    // CALL FUN_062da500`.
    //
    // The size lines up exactly, which is the independent check: AfterburnerIdea adds no fields of
    // its own beyond BaseIdea, sizeof(BaseIdea) == 0x14 (static_asserted in common_idea.h), and
    // FaceBehavior's next confirmed field Flag1b4 sits at 0x1b4 == 0x1a0 + 0x14. The old comment's
    // worry about "nominally 0x18 bytes" came from DockBehavior's GotoDirectiveHandle, which
    // common_idea.h's own note already explains as a probably-different (pointer-pair) use of the
    // same ctor helper -- it never applied here.
    //
    // All four of retail's dispatches through this object are now real virtual calls rather than
    // stand-ins: slot 0 in SetContext (.text:062c8b08), slot 4 in Disengage (.text:062c93b5) and
    // Update (.text:062c8f16), slot 1 in Update (.text:062c8ec5). AfterburnerIdea::Update and
    // ::Commit are themselves complete ports (item 1658 and .rdata:063a0594).
    AfterburnerIdea SubObjectC;
    unsigned char Flag1b4;          // +0x1b4. Zeroed by ctor. Read/written directly by Update()
                                     // (now fully ported) and SetTargetOffset() (set to 1 on the "set" path,
                                     // cleared by ResetApproachState()/the "clear" path) -- NOT
                                     // accessed through SubObjectC's own vtable anywhere this pass
                                     // traced (see the class-level comment for the DockBehavior
                                     // GotoDirectiveHandle::NavPilotPtr offset coincidence this byte
                                     // sits at).
    unsigned char Pad1b5[0x1b8 - 0x1b5];
    float Field1b8;                  // +0x1b8. Zeroed by ctor/ResetApproachState(). SetTargetOffset()
                                      // writes -Offset.y (pre-clamp) on the "set" path.
    float Field1bc;                  // +0x1bc. Zeroed by ctor/ResetApproachState(). SetTargetOffset()
                                      // writes -Offset.x (pre-clamp) on the "set" path.
    float Field1c0;                  // +0x1c0. Zeroed by ctor/ResetApproachState(). SetTargetOffset()
                                      // writes Offset.z (raw, unclamped/unnegated) on the "set" path.
    unsigned char Flag1c4;           // +0x1c4. Set to 1 by ctor. ApplyTargetMessage() overwrites it
                                      // from Message->+0x44; ResetApproachState() reads it (feeds
                                      // Behavior_NotifyRegistrationFlag's bool argument).
    unsigned char Flag1c5;           // +0x1c5. Zeroed by ctor/ResetApproachState(). Read/written by
                                      // Update()'s GAP body only (a "resolved this tick" flag).
    unsigned char Pad1c6[0x1c8 - 0x1c6];
    // RETYPED int -> float 2026-09-17 (claude:w18-behavior). Update()'s body reaches this field
    // ONLY through the x87 unit, which settles the type: `.text:062c8f26 FLD float ptr [EBP+0x1c8];
    // FCOMP float ptr [EAX+0x16d8]`, `.text:062c8fd8 FADD float ptr [EBP+0x1c8]; FSTP float ptr
    // [EBP+0x1c8]` and `.text:062c8fbe FLD float ptr [EBP+0x1c8]; FCOMP float ptr [EDX+0x16d4]`.
    // The two `MOV dword ptr [EBP+0x1c8],EDI` stores (062c8f3d / 062c8ff3) write EDI==0, whose bit
    // pattern is 0.0f either way, so nothing contradicted the old `int` until the FPU reads were
    // traced. It is a seconds-valued "how long has this state been held" accumulator, advanced by
    // DeltaTime1c and compared against Personality::EngineKillParams.EngineKillSearchTime/EngineKillFaceTime.
    float Field1c8;                  // +0x1c8. Zeroed by ctor/ResetApproachState().
    float Field1cc;                  // +0x1cc. Zeroed by ctor. SetContext() computes it via the
                                      // FULLPORT-DEFERRED FUN_062e5b20(ThreatSourcePtr14, 0, 1.0f)
                                      // call (not reproduced -- field stays at its ctor default).
    float Field1d0;                  // +0x1d0. Set to 0.9f by ctor. Not otherwise read/written by
                                      // anything this pass ported.
    unsigned char Flag1d4;            // +0x1d4. Zeroed by ctor. Update()'s own entry guard checks and
                                       // clears it (early-out -> UpdateResult10=2); RequestAbort()
                                       // (slot 13) is the confirmed setter.
    unsigned char Pad1d5[0x1d8 - 0x1d5];
    // RETYPED int -> float 2026-09-17 (claude:w18-behavior), same reasoning as Field1c8 above and
    // for the same reason it was missed: the only integer-looking access is the zero store.
    // `.text:062c8ea6 FLD float ptr [EBP+0x1d8]; FADD float ptr [EBP+0x1c]; FSTP float ptr
    // [EBP+0x1d8]` and `.text:062c8e68 FLD float ptr [EBP+0x1d8]; FCOMP float ptr [ECX+0x16e0]`.
    // A SECOND seconds accumulator, independent of Field1c8: this one measures how long the ship
    // has been turning toward the target while the bearing is still wide, and is capped by
    // Personality::EngineKillParams.EngineKillAfterburnerTime.
    float Field1d8;                   // +0x1d8. Zeroed by ctor/ResetApproachState().
};

static_assert(offsetof(FaceBehavior, CachedHandle08) == 0x08, "FaceBehavior::CachedHandle08 offset");
static_assert(offsetof(FaceBehavior, ModeSelector0xc) == 0x0c, "FaceBehavior::ModeSelector0xc offset");
static_assert(offsetof(FaceBehavior, UpdateResult10) == 0x10, "FaceBehavior::UpdateResult10 offset");
static_assert(offsetof(FaceBehavior, ThreatSourcePtr14) == 0x14, "FaceBehavior::ThreatSourcePtr14 offset");
static_assert(offsetof(FaceBehavior, DeltaTime1c) == 0x1c, "FaceBehavior::DeltaTime1c offset");
static_assert(offsetof(FaceBehavior, BehaviorMgrPtr20) == 0x20, "FaceBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(FaceBehavior, NavHelperPtr24) == 0x24, "FaceBehavior::NavHelperPtr24 offset");
static_assert(offsetof(FaceBehavior, Flags28) == 0x28, "FaceBehavior::Flags28 offset");
static_assert(offsetof(FaceBehavior, Flag29) == 0x29, "FaceBehavior::Flag29 offset");
static_assert(offsetof(FaceBehavior, TargetKind2c) == 0x2c, "FaceBehavior::TargetKind2c offset");
static_assert(offsetof(FaceBehavior, Watcher30) == 0x30, "FaceBehavior::Watcher30 offset");
static_assert(offsetof(FaceBehavior, OrientMatrix38) == 0x38, "FaceBehavior::OrientMatrix38 offset");
static_assert(offsetof(FaceBehavior, TargetPosition5c) == 0x5c, "FaceBehavior::TargetPosition5c offset");
static_assert(offsetof(FaceBehavior, SubObjectA) == 0x68, "FaceBehavior::SubObjectA offset");
static_assert(offsetof(FaceBehavior, SubObjectB) == 0xe0, "FaceBehavior::SubObjectB offset");
static_assert(offsetof(FaceBehavior, SubObjectC) == 0x1a0, "FaceBehavior::SubObjectC offset");
static_assert(offsetof(FaceBehavior, Flag1b4) == 0x1b4, "FaceBehavior::Flag1b4 offset");
static_assert(offsetof(FaceBehavior, Field1b8) == 0x1b8, "FaceBehavior::Field1b8 offset");
static_assert(offsetof(FaceBehavior, Field1bc) == 0x1bc, "FaceBehavior::Field1bc offset");
static_assert(offsetof(FaceBehavior, Field1c0) == 0x1c0, "FaceBehavior::Field1c0 offset");
static_assert(offsetof(FaceBehavior, Flag1c4) == 0x1c4, "FaceBehavior::Flag1c4 offset");
static_assert(offsetof(FaceBehavior, Flag1c5) == 0x1c5, "FaceBehavior::Flag1c5 offset");
static_assert(offsetof(FaceBehavior, Field1c8) == 0x1c8, "FaceBehavior::Field1c8 offset");
static_assert(offsetof(FaceBehavior, Field1cc) == 0x1cc, "FaceBehavior::Field1cc offset");
static_assert(offsetof(FaceBehavior, Field1d0) == 0x1d0, "FaceBehavior::Field1d0 offset");
static_assert(offsetof(FaceBehavior, Flag1d4) == 0x1d4, "FaceBehavior::Flag1d4 offset");
static_assert(offsetof(FaceBehavior, Field1d8) == 0x1d8, "FaceBehavior::Field1d8 offset");
static_assert(sizeof(FaceBehavior) >= 0x1dc, "FaceBehavior minimum size (largest field actually read/written this pass)");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x1dc`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(FaceBehavior) <= 0x1dc, "FaceBehavior must not exceed its retail size 0x1dc");
