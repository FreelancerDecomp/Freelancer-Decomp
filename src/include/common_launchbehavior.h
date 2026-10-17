// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher
#include "common_idea.h"      // MoveIdea, FaceIdea -- real classes behind SubObjectA/B (see below)

#include <cstddef>

// ============================================================================
// LaunchBehavior -- AI "launch/catapult" state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a9094 (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor @ .data:0x063ebfb0, name ".?AVLaunchBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a9084, numBaseClasses = 2,
//        BaseClassArray @ 0x063a9078 = [ LaunchBehavior(self @0x063a9060,
//                                        numContained=1),
//                                        Behavior(numContained=0, mdisp=0,
//                                        @0x063a8c78, the SAME shared
//                                        Behavior BaseClassDescriptor every
//                                        other sibling in this family
//                                        reuses) ]
//
// Single-inheritance direct child of Behavior -- same shape as
// IdleBehavior/CruiseBehavior, no secondary vtable. Full byte-level trace
// (self-consistency of COL -> CHD -> BaseClassArray -> both
// BaseClassDescriptors) re-verified this pass.
//
// Independently corroborated by decomp-tracker item 1416's own pre-existing
// notes (a prior pass, before this RTTI identification): the ctor
// (FUN_062cd640) was already flagged as installing the "<empty-string> name,
// dispatch table slot 0x063a00e8-0x063a00f4" row of the 21-row TractorArm
// named-mode dispatch table (.rdata 0x063a002c-0x063a0238) -- i.e. retail's
// own mode-name string table has NO name for this mode (Ghidra found zero
// bytes, no label at all, unlike every other row), which is consistent with
// "Launch" being a mode retail never surfaces through the named-directive
// path players/mission scripts see (a ship launches from a base/carrier
// automatically, not via a player-issued named directive).
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062cd640 (Common.dll.txt 202851-202895). Calls the shared
// base-init field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) (item 1529 precedent), default-constructs
// SubObjectA @+0x2c via FUN_062d4e60 and SubObjectB @+0xa4 via FUN_062d6750
// -- the SAME TWO ctor functions DockBehavior's own SubObjectA/SubObjectB
// use, at offsets shifted down by exactly 4 bytes since LaunchBehavior has no
// secondary-vtable pointer to make room for. IDENTIFIED 2026-09-11
// (d285:common-port) as real MoveIdea/FaceIdea (common_idea.h) -- independent
// confirmation both classes share this sub-object pair's retail types, now
// constructed for real via the member-initializer list. Ctor also zeroes
// several fields and sets two float thresholds (Field180=3.5f, Field22c=15.0f).
// ============================================================================

class LaunchBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (8, "Launch"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit LaunchBehavior(int InTypeId);
    ~LaunchBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fccc, 27 slots, 0x0639fccc-0x0639fd34 inclusive --
    // byte distance to the next COL/vtable head at 0x0639fd38/0x0639fd48
    // confirms this bound). Slot -> retail address.
    //
    //   0  (0x00) FUN_062cd6e0        real (non-thunk) dtor wrapper -> FUN_062cd700; ported for real
    //   1  (0x04) LAB_062cd790        ported (outer logic): SetContext()
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not LaunchBehavior-specific
    //   3  (0x0c) FUN_062cd830        ported (outer logic): Engage()
    //   4  (0x10) FUN_062cd8b0        ported (outer logic): Disengage()
    //   5  (0x14) LAB_062cd910        ported for real: SetModeFromFlags()
    //   6  (0x18) LAB_062cf5e0        trivial ICF-shared "return 2" stub (same 14-way fold
    //                                 already documented in common_idlebehavior.h's own slot 6), skipped
    //   7  (0x1c) LAB_062cd950        GAP:AI-LAUNCH-STATE-MACHINE -- 457 instructions. [2026-09-11]
    //                                 The FULL control-flow skeleton (every guard check, the
    //                                 CachedResultPtr170 refresh-detect, the Field188-vs-Field18c
    //                                 gate, the DeltaTime1c accumulation, and the real 6-way
    //                                 ModeState164 dispatch with every mode transition/return value)
    //                                 is now hand-traced and ported for real below -- see Update()'s
    //                                 own .cpp comment. UPDATED [2026-09-11, d312:common-port]: 6 of
    //                                 the per-case sub-object "payload" helpers are ALSO now wired for
    //                                 real -- ReleaseApproachStep/SetApproachOffset (both literal
    //                                 addresses already independently ported elsewhere in this file's
    //                                 own family as FormationMoveFx::SetMode/SetPosition, a confirmed
    //                                 shared class-agnostic offset-convention helper -- see each
    //                                 method's own header comment), PushApproachBasisToSubObjectB
    //                                 (re-read from raw bytes this pass -- a SETTER copying the
    //                                 caller's own approach-basis vectors into FaceIdea's confirmed
    //                                 ZeroBlock34/FieldA0 fields, corrected from this method's prior
    //                                 "fetch" framing), and SetApproachMode/SetHardpointCone (both
    //                                 confirmed-trivial `*(this+offset)=param` setters landing exactly
    //                                 on MoveIdea::Field44 / FaceIdea::AngleWeight8c) and
    //                                 AdjustNavHelperThrottle (ThrustManager::SetThrottle).
    //                                 What remains genuinely NOT ported: case 2's own combined
    //                                 approach-transform computation (an object-transform vtbl+0xc8
    //                                 call on Watcher168's raw block, a "hpmount" hardpoint lookup, and
    //                                 an unidentified NavHelper-nested-singleton distance calc -- kept
    //                                 as ONE opaque step, several calls share overlapping stack
    //                                 buffers), case 3's StartLaunchSequence (FUN_062cdfa0, decomp-
    //                                 tracker item 1425's own "whole AI directive/behavior subsystem"
    //                                 cross-cutting gap), SetApproachSpeedParam (ICF-mislabeled
    //                                 `DeviceTRC::bind_to_dc`, unresolved anywhere else in this port
    //                                 either -- common_buzzbehavior.h's own precedent), and
    //                                 SetApproachThrottleScale (decomp-tracker item 1585, `blocked`: Field04's
    //                                 own interface genuinely unidentified). The one pure-math
    //                                 exception (no external call at all) is case 5's fixed-angle (pi
    //                                 radians, DAT_0639e2d0) 2D (x,z) rotation of the 3-Vector
    //                                 LaunchBasis array, and its preceding dot-product-vs-zero
    //                                 (DAT_06399408) gate -- both already ported for real.
    //   8  (0x20) Exclusion::AsteroidExclusionZon..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) FUN_062cdf30        ported (outer logic): ApplyDirective()
    //  11  (0x2c) CEquip::DisableController        ICF fold
    //  12-15 (0x30-0x3c) MetaList<GoodDesc>::operator[]  ICF fold (x4)
    //  16  (0x40) FUN_062d2110        family-shared "audio cue" helper, not reachable from any
    //                                 confirmed LaunchBehavior call site this pass traced -- FULLPORT-DEFERRED
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

    // Real (non-thunk) destructor path. FUN_062cd6e0 (Common.dll.txt
    // 202899-202916) is the scalar-deleting-dtor thunk: it calls FUN_062cd700
    // (the REAL cleanup body, 202920-202966) then conditionally frees. Unlike
    // IdleBehavior/CruiseBehavior's own slot-0 thunks (which share a
    // universally-empty helper), LaunchBehavior's own FUN_062cd700 genuinely
    // clears three BaseWatcher-shaped fields (+0x168, +0xb8, +0x40 -- same
    // three-watcher shape already documented on DockBehavior's real dtor,
    // FUN_062c30d0) before tail-calling the shared FUN_062d2080(this,-1).
    // WIRED FOR REAL 2026-09-11 (d285:common-port): +0xb8/+0x40 are EXACTLY
    // SubObjectB::Watcher/SubObjectA::Watcher (FaceIdea/MoveIdea's own +0x14
    // BaseWatcher field, at absolute 0xa4+0x14=0xb8 / 0x2c+0x14=0x40) --
    // independent corroboration of the MoveIdea/FaceIdea identification.

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 202969-203032
    // (LAB_062cd790), 45 instructions. Calls the shared Behavior-family
    // context initializer FUN_062d2090(this, P1, P2, P3) (see
    // common_dockbehavior.cpp's DockBehavior::SetContext for the fully-traced
    // shared body); if it returns false, returns false immediately (no other
    // side effect). Otherwise:
    //   - calls SubObjectA's own vtbl slot 0 with args (ThreatSourcePtr14,
    //     NavHelperPtr24, <this function's own raw Param2>) -- WIRED FOR REAL
    //     2026-09-11 (d285:common-port), now that SubObjectA is identified as
    //     MoveIdea (see field declaration below). Argument order re-verified
    //     directly from Common.dll.txt 202985-202994 byte-for-byte (push
    //     order: ThreatSourcePtr14 pushed last = 1st param, matching
    //     SetContext(InObject, InHandle, InContext)); Param2 is captured into
    //     a register BEFORE the FUN_062d2090 call above and never reloaded
    //     from a field afterward, same pattern as DockBehavior's own
    //     SetContext (see common_dockbehavior.h).
    //   - calls the shared FUN_062d5980(&SubObjectA, 0) helper -- FULLPORT-DEFERRED
    //     (a plain free-function call, not a vtable call through SubObjectA);
    //   - calls SubObjectB's own vtbl slot 0 with the same arg shape -- WIRED
    //     FOR REAL, same evidence (Common.dll.txt 202998-203005);
    //   - reads NavHelperPtr24's nested +0x14fc/+0x84 sub-object (the SAME
    //     "NavHelper nested sub-object" access pattern already documented on
    //     DockBehavior) and calls ITS OWN vtbl+0xbc "is threatening"-shaped
    //     bool getter -- FULLPORT-DEFERRED (nested sub-object's class isn't
    //     identified either);
    //   - sets Field18c to 15.0f if that bool was true, 60.0f otherwise
    //     (0x41700000 / 0x42700000, Common.dll.txt 203016 / 203021).
    // CORRECTED 2026-09-17 (claude:w16-behavior): the prior port skipped the
    // Field18c store altogether because the deciding bool is deferred. That was
    // wrong in kind -- retail writes Field18c on every successful SetContext and
    // there is no path where it keeps its old value. The store is now ported,
    // with the deferred predicate behind NestedNavQuery0xbc() (currently false,
    // selecting the 60.0f arm). TODO: identify the +0x14fc/+0x84 pointee's class
    // so the predicate becomes real.
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 203040-203079
    // (FUN_062cd830), 30 instructions. Tail-calls the shared FUN_062d20d0
    // reset-tick-state effect first (`ModeSelector0xc=1; UpdateResult10=1;
    // Flags28&=~1; DeltaTime1c=0`), caches the current system id
    // (FetchSystemId) into CachedSystemId170, caches GetObjectTypeOrZero
    // (FUN_062e6200) of the watched target (Watcher168 target - 8, or null)
    // into TargetType174, calls FUN_062e9a60(NavHelperPtr24+0x12c, 0), then
    // sets ModeState164=1, Flags17c=0, Field184=0.0f, Field188=0.0f, and
    // calls `SetCruiseLock(0, 3)`-equivalent via FUN_062d21d0
    // (FULLPORT-DEFERRED, same shared name/role as FormationBehavior/
    // DockBehavior's own SetCruiseLock).
    void Engage();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 203083-203112
    // (FUN_062cd8b0), 30 instructions. Calls SubObjectA's and SubObjectB's
    // own vtbl slot 4 (offset 0x10, "release"-shaped -- WIRED FOR REAL
    // 2026-09-11 d285:common-port as MoveIdea::Commit()/FaceIdea::Commit(),
    // in that order, re-verified from Common.dll.txt 203085-203092), tail-calls the shared
    // FUN_062d20f0(this) effect (BehaviorMgrPtr20->+0xc deref-and-clear,
    // FULLPORT-DEFERRED per common_idlebehavior.cpp's own precedent for this
    // exact helper, plus the unconditional Flag29 clear it performs), calls
    // a `ResumeUserInput(true)`-equivalent via FUN_062e9a60(NavHelperPtr24+
    // 0x12c, 1) (FULLPORT-DEFERRED), calls `SetCruiseLock(1, 3)`-equivalent
    // via FUN_062d21d0 (FULLPORT-DEFERRED), sets Flags28 bit 0x2 clear / bit
    // 0x1 set, and calls `NotifyBehaviorDone`-equivalent via
    // FUN_062d29a0(BehaviorMgrPtr20, 8, 0) (FULLPORT-DEFERRED, same
    // FUN_062d29a0-shaped helper role DockBehavior documents, but here
    // confirmed called with `this` = BehaviorMgrPtr20 rather than the raw
    // ints DockBehavior's own precedent comment describes -- a genuinely
    // different call site of a same-named retail function).
    void Disengage();

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 203124-203147
    // (LAB_062cd910), 34 instructions, fully self-contained -- no deferred
    // sub-object dependency at all.
    //   int mode = (Flags28 & 0x2) ? 0 : 2;
    //   if (UpdateResult10 == 2 || UpdateResult10 == 3) mode = 2;
    //   if (Flag29 != 0) { ModeSelector0xc = 3; return 3; }
    //   ModeSelector0xc = mode;
    //   return mode;
    int SetModeFromFlags();

    // Own vtable slot 10 (offset 0x28). Common.dll.txt 203650-203700
    // (FUN_062cdf30), 1 parameter (a caller-owned directive-shaped struct
    // pointer), 46 instructions. Calls `FUN_062e74a0(NavHelperPtr24+0x34,
    // Param+0xc)` (FULLPORT-DEFERRED, not-yet-ported helper -- looks like a
    // hardpoint/launch-point lookup by the calling shape) and adjusts its
    // result by the same "-8 node adjustor" idiom; if that differs from the
    // current Watcher168.Watched, re-points Watcher168 via
    // `BaseWatcher::set_pointer` (already-ported, common_goods.h); copies
    // Param->+0x10 into DirectiveField178; then re-derives the (possibly
    // just-updated) raw block from Watcher168 and, if non-null, sets Flags28
    // bit 0x2 and UpdateResult10=1 and returns true, otherwise clears Flags28
    // bit 0x2 and returns false.
    bool ApplyDirective(void const* Param);

    // Own vtable slot 7 (offset 0x1c). Common.dll.txt 203151-203638
    // (LAB_062cd950), 457 instructions. GAP:AI-LAUNCH-STATE-MACHINE -- see the
    // vtable-slot-table comment above and this method's own .cpp comment for
    // the full raw-bytes trace. The guard checks, mode-transition control
    // flow, and return-value contract are ALL ported for real. UPDATED
    // 2026-09-11 (d312:common-port): 6 of the 10 payload helpers below are now
    // ALSO wired for real (ReleaseApproachStep/SetApproachOffset/
    // PushApproachBasisToSubObjectB/SetApproachMode/SetHardpointCone/
    // AdjustNavHelperThrottle -- see each one's own header comment for the
    // byte-exact evidence); ComputeLaunchApproachTransform/StartLaunchSequence/
    // SetApproachSpeedParam/SetApproachThrottleScale remain genuine FULLPORT-DEFERRED
    // gaps (unidentified sub-object classes / cross-cutting subsystem, see each
    // one's own comment). Case 5's fixed pi-radian rotation of LaunchBasis and
    // its preceding dot-product gate are pure self-contained math and ARE
    // ported for real.
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

    // The other slots retail's LaunchBehavior vftable (.rdata:0639fccc) overrides, wired
    // 2026-10-08 (claude-code:cmn-ai2): until then Engage/Disengage/SetModeFromFlags/
    // ApplyDirective were plain members and IBehaviorManager's Behavior* dispatch
    // (common_behaviormanager.cpp Incoming->Slot3() / Outgoing->Slot4() / Slot5 / Slot10) reached the
    // base bodies instead. Slot 5 is `RET 4`: the float argument is ignored.
    void Slot3() override { Engage(); }                        // 3 +0x0c FUN_062cd830
    void Slot4() override { Disengage(); }                     // 4 +0x10 FUN_062cd8b0
    int Slot5(float) override { return SetModeFromFlags(); }   // 5 +0x14 LAB_062cd910
    bool Slot10(void* DirectiveOp) override { return ApplyDirective(DirectiveOp); } // 10 +0x28 FUN_062cdf30

private:
    // FULLPORT-DEFERRED stand-ins for not-yet-ported cross-cutting retail
    // helpers this class's ported methods route through by name/role (shared
    // with several other siblings in this family -- see the DockBehavior/
    // IdleBehavior/CruiseBehavior precedents cited in the method comments
    // above for each one's confirmed effect elsewhere).
    // .text:062cd838-062cd846 / 062cd980-062cd98c: `ECX=[this+0x18]; CALL [[ECX]+8]` (IObjDB::
    // GetZoneDb, a Universe::ISystem*), then `CALL [[EAX]+0]` (ISystem slot 0, the system id).
    unsigned FetchSystemId() const;
    static void* FindLaunchHardpoint(void* NavHelper, void const* Param); // FUN_062e74a0, ApplyDirective()'s own lookup

    // ---- Update() (vtable slot 7) payload helpers, GAP:AI-LAUNCH-STATE-MACHINE. ----
    // See Update()'s own .cpp comment for the exact retail FUN_ address /
    // vtable slot each one stands in for.
    //
    // [2026-09-11 d312:common-port] Six of these are now WIRED FOR REAL by
    // reading raw bytes directly against MoveIdea/FaceIdea's own confirmed
    // field layouts (common_idea.h) and this marathon's already-ported
    // FormationMoveFx class (common_behavior.h) -- see each one's own comment
    // below for the byte-exact evidence.
    //
    // [2026-09-17 claude:w18-behavior] FOUR MORE ARE NOW WIRED FOR REAL, and every
    // reason the paragraph above gave for deferring them measured STALE. The
    // paragraph used to name two "genuine external blockers" and three
    // "cross-cutting gaps"; here is what each one actually was:
    //   * SetApproachThrottleScale -- "item 1585 `blocked`, Field04's own interface
    //     unidentified". Field04 is BaseIdea::Object and slot 0xd4 is
    //     IObjInspect::get_projected_throttle (identified 2026-09-16), with the
    //     trampoline already ported as Behavior_InvokeManagerSlotD4 (item 1765),
    //     whose own doc comment names this very function. PORTED.
    //   * SetApproachSpeedParam -- "ICF-mislabeled `DeviceTRC::bind_to_dc`, target
    //     address not independently resolved anywhere else in this port either".
    //     It is resolved: common_idea.h declares MoveIdea::SetRange3c(float) and
    //     records the fold address ".text:062a66b0 (= DeviceTRC::bind_to_dc)"
    //     explicitly. The body is a two-instruction dword store. PORTED.
    //   * NestedNavQuery0xbc -- "unidentified nested NavHelper sub-object". There
    //     is no nested sub-object; the chain is ONE load of +0x1580 ==
    //     SystemManager::ExternalIface, and slot 0xbc is IObjInspect::is_player().
    //     PORTED -- see the .cpp for the instruction-level disproof.
    //   * EnsureNestedNavPositionCached -- same phantom sub-object. The block is
    //     FUN_062e6d40's (item 1794) own cache refresh, inlined. PORTED.
    // NavHelperPtr24 is a SystemManager* throughout; that is what dissolved the
    // last three at once.
    //
    // StartLaunchSequence (item 1425) and ComputeLaunchApproachTransform are both ported
    // now (2026-10-08 / 2026-09-17); see their definitions.
    bool NestedNavQuery0xbc();

    // case 2 payload, .text:062cda2f-062cdbdd. PORTED FOR REAL 2026-09-17
    // (claude:w20-updates) -- see the .cpp for the full stack-offset derivation that
    // the note below correctly said was the only work left. The three slot
    // identifications below stand; the ONE claim in them that did not survive is the
    // last bullet's "still unnamed ... contributes one distance scalar":
    //   * FUN_06391cf0 is not unnamed. Its body is DACOM_Acquire("3DMathEngine",0xc,0)
    //     -- the same acquire already ported as Acquire3DMathEngine() (common_csys.h),
    //     which AnimDB::Update uses at .text:0627e162 for its own cache DAT_063fc018.
    //   * Its vtbl+0x38 is I3DMathEngine::TransposeTransform(FVector&, FMatrix const&,
    //     FVector const&) (x86math.h, slot 14). It yields the rotated Field190 VECTOR,
    //     not a distance scalar -- .text:062cdbbe-062cdbd1 reads all three dwords of
    //     its out-buffer straight back into Field190.
    // Original (still accurate) slot writeup:
    //   * "rawblock vtbl+0xc8 get transform" -- slot +0x0C8 is IObjInspect slot 50,
    //     `get_dock_hardpoints(int Index, TERMINAL_TYPE* Type, Transform* XformA,
    //     Transform* XformB, Transform* XformC, float* Distance)`, recovered from its
    //     mangled name in common_iface.h. The call site matches exactly: six pushes at
    //     .text:062cda42-062cda5f, last push first, giving
    //     (Index = DirectiveField178, Type = null, XformA = ESP+0x38, XformB = ESP+0x94,
    //     XformC = ESP+0x60, Distance = null). Six-for-six.
    //   * "ThreatSourcePtr14 vtbl+0xc0 hpmount lookup" -- slot +0x0C0 is IObjInspect
    //     slot 48, `get_hardpoint(char const* Name, Vector* Pos, Matrix* Orient)`, and
    //     the pushed string at .text:062cdb0d IS "hpmount" (s_hpmount_0639e5b8).
    //   * "NavHelper nested +0x14fc/+0x84 ... unidentified singleton" -- the
    //     +0x14fc/+0x84 half is the same phantom disproven above (one load of
    //     SystemManager::ExternalIface). Only DAT_063fc538's own cached singleton
    //     (FUN_06391cf0, its vtbl+0x38) is still unnamed, and it contributes one
    //     distance scalar, not the transform.
    // The stack-depth bookkeeping the previous note flagged is now done, in the .cpp,
    // with every ESP value derived from the function's own prologue. Populates
    // Field1fc/ApproachBasis3 from XformA, Field1cc/ApproachBasis2 from XformB and
    // Field19c/LaunchBasis from XformC, then (only when get_hardpoint FAILS) Field190
    // and MoveIdea::Offset4c via FUN_062d5a80.
    void ComputeLaunchApproachTransform();
    // case 3: FUN_062cdfa0(this), item 1425 -- re-reads the dock hardpoints, sets Flag17c
    // from three alignment dot products and special-cases the terminal type (Flag230,
    // Flag231, pi rotation of the three bases). Returns retail's AL (false only when the
    // watcher target is gone); the only caller ignores it.
    bool StartLaunchSequence();
    void SetApproachSpeedParam(float Value);   // ICF-mislabeled `DeviceTRC::bind_to_dc(SubObjectA,
                                                // Value)` -- see common_buzzbehavior.h's own
                                                // documented ICF-mislabel precedent for this exact
                                                // retail symbol; real callee/class unidentified.
    // FUN_062d5980(SubObjectA, Flag). WIRED FOR REAL 2026-09-11 (d312:common-port):
    // re-verified from raw bytes (Common.dll.txt 062cdc6a-062cdc6e /
    // 062cdcc9-062cdccd, both plain non-virtual CALLs on SubObjectA, not a
    // vtable dispatch) that this is the SAME literal address already ported
    // as FormationMoveFx::SetMode(int) (decomp-tracker item 1583,
    // common_behavior.h/.cpp) -- that item's own notes independently confirm
    // FUN_062d5980 is a shared, class-agnostic, offset-convention-only helper
    // (reads/writes only the generic +0x08/+0x34 offsets both FormationMoveFx
    // and MoveIdea happen to share) ALSO called directly on other confirmed
    // MoveIdea instances elsewhere in this port (e.g. common_trailbehavior.cpp's
    // `reinterpret_cast<FormationMoveFx*>(&SubObjectC)->SetMode(1);`, the exact
    // precedent this implementation follows).
    void ReleaseApproachStep(int Flag);
    // FUN_062d5840(SubObjectA, &Vector). WIRED FOR REAL 2026-09-11
    // (d312:common-port): same non-virtual-shared-helper situation as
    // ReleaseApproachStep() above -- this is the literal address already
    // ported as FormationMoveFx::SetPosition(Vector const&) (item 1580).
    // FormationMoveFx::Position sits at +0x1c (0xc bytes), landing exactly on
    // MoveIdea's own ctor-zeroed Field1c/Field20/Field24 span -- the SAME
    // "shared offset convention" relationship item 1583 already documents for
    // Mode@+0x34; wired via the same reinterpret_cast<FormationMoveFx*> idiom.
    void SetApproachOffset(void const* OffsetVector);
    // FUN_062d75c0(SubObjectB, &SrcVectorBlock). CORRECTED and WIRED FOR REAL
    // 2026-09-11 (d312:common-port): this pass re-read the function's own raw
    // bytes directly (Common.dll.txt 219363-219378, only 10 instructions) --
    // it is a SETTER, not a getter: `this->+0xa0 = 4; memcpy(this->+0x34,
    // *param1, 36)` (a 9-dword REP MOVSD from the CALLER-supplied buffer INTO
    // `this`). This is the OPPOSITE data-flow direction from this method's own
    // prior name/doc comment ("FetchSubObjectPosition... reads position OUT of
    // SubObjectB") -- that framing was never wrong in its ported EFFECT (the
    // prior pass correctly left it as a no-op stub either way), only in its
    // documented direction. `this->+0x34` (9 dwords) and `this->+0xa0` (1
    // dword) land exactly on FaceIdea's own confirmed ZeroBlock34[9]/FieldA0
    // fields (common_idea.h) -- i.e. retail is pushing the caller's own
    // approach-basis vectors (LaunchBasis, populated by case 2's transform and
    // case 5's own pi-radian rotation) INTO the FaceIdea sub-object's internal
    // cache and tagging it "have basis" -- a sensible "tell the face-idea what
    // to face toward" operation. Renamed from FetchSubObjectPosition to
    // PushApproachBasisToSubObjectB to match the corrected direction; always
    // called with SubObjectB as `this` at all 3 call sites (case 4/5/6), so
    // the redundant SubObjectPtr parameter is dropped.
    void PushApproachBasisToSubObjectB(void const* SrcVectorBlock);
    // FUN_062d5a20(SubObjectA, Field22c), decomp-tracker item 1585. CLOSED
    // 2026-09-17 (claude:orch-dockpreamble): this is now a one-line forward to
    // MoveIdea::SetThrottleScale, which is where the function actually belongs --
    // a second call site (DockBehavior::AdvancePreamble, .text:062c48ca) proved it
    // is a MoveIdea method and not a LaunchBehavior one.
    //
    // RENAMED with it, from SetApproachRadius. The argument is a throttle scale
    // handed to IObjInspect::get_projected_throttle, not a radius; the old name
    // came from the call site's shape rather than from the callee.
    //
    // The recorded blocker ("Field04's own interface/class is unidentified
    // anywhere in this port -- genuine external blocker") is retired, not
    // re-filed: Field04 is BaseIdea::Object and slot 0xd4 has been
    // get_projected_throttle since 2026-09-16.
    void SetApproachThrottleScale(float Scale);
    // FUN_062d88b0(SubObjectA, Mode). WIRED FOR REAL 2026-09-11 (d312:common-port):
    // decomp-tracker item 1620 (`skipped`, ICF-folded 21-way trivial setter)
    // already confirms this function's ENTIRE body is `*(uint*)(this+0x44) =
    // param; RET 4` -- a raw field store, not meaningful as a named shared
    // method. Offset +0x44 lands exactly on MoveIdea's own confirmed Field44
    // (common_idea.h, also independently read by MoveIdea::Commit()) -- safe
    // to reproduce as a direct field write on SubObjectA, matching item 1620's
    // own stated convention ("each of the 21 call sites represents a
    // DIFFERENT real setter on a DIFFERENT class... captured individually
    // wherever their own class's tracker item lives").
    void SetApproachMode(int Mode);

    // SubObjectA/B's own vtbl slot 1 (offset 0x4) / slot 4 (offset 0x10).
    // WIRED FOR REAL 2026-09-11 (d285:common-port): SubObjectA/B are
    // IDENTIFIED as real MoveIdea/FaceIdea (common_idea.h), both derived from
    // BaseIdea, whose slot 1/slot 4 are exactly `Update(float)->int` /
    // `Commit()` -- taking `BaseIdea*` here lets the same helper serve either
    // sub-object with real virtual dispatch (no per-class duplication needed).
    int TickSubObject(BaseIdea* SubObject, float DeltaTime); // "advance by DeltaTime1c"
    void ReleaseSubObject(BaseIdea* SubObject); // same "release"-shaped call as Disengage() above.
    void EnsureNestedNavPositionCached();      // NavHelperPtr24+0x14fc's own +0x84 pointee vtbl+0x2c,
                                                // gated by NavHelperPtr24+0x14fc+0x7e (case 5's own
                                                // occurrence of the same pattern ComputeLaunchApproachTransform()
                                                // uses internally for case 2/3). Still FULLPORT-DEFERRED --
                                                // same unidentified "NavHelper nested sub-object" class
                                                // this whole family's SetContext()/Update() methods share.
    // FUN_062d93c0(SubObjectB, Angle) -- case 6, pi/8. WIRED FOR REAL
    // 2026-09-11 (d312:common-port): decomp-tracker item 1632 (`blocked` for
    // its OTHER, unrelated call sites in a still-unported weapon/equipment
    // factory switch) already confirms this function's entire body is
    // `*(uint*)(this+0x8c) = param` -- a raw field store. Offset +0x8c lands
    // exactly on FaceIdea's own confirmed AngleWeight8c (common_idea.h) --
    // safe to reproduce as a direct field write on SubObjectB for THIS call
    // site (the item's own genuine blocker is about a DIFFERENT owning class
    // at the other 4 XREFs, not this one).
    void SetHardpointCone(float Angle);
    // FUN_062e89a0(NavHelperPtr24+0x18, Value, 0) -- case 5, gated by Flag230:
    // ThrustManager::SetThrottle(Value, 0) on SystemManager::Thrust.
    void AdjustNavHelperThrottle(float Value);


public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. Update()'s own large state machine (slot 7, control
    // flow ported for real, several sub-object payload calls still
    // FULLPORT-DEFERRED -- see the vtable-slot-table comment above) touches
    // every field up through +0x232 (Flag231); this struct is therefore NOT
    // sizeof()-asserted the way DockBehavior is -- only offsets this pass's
    // ported methods actually touch are asserted.
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.       // +0x08. Set by SetContext() via the shared FUN_062d2090 effect.
    int ModeSelector0xc;        // +0x0c. Written by Engage() (=1) and SetModeFromFlags() (0/2/3).
    int UpdateResult10;         // +0x10. Update()'s own tri-state result (documented, not ported).
    void* ThreatSourcePtr14;    // +0x14. Set by SetContext() (Param1).
    IObjDB* Field18;            // +0x18. The manager's object database, stored by Behavior's
                                 // own vtable slot 2 (Behavior::Slot2, 0629afd0 `MOV [ECX+0x18],
                                 // EAX`) from ConnectBehaviorManagerObjectDb (FUN_062d2830).
                                 // Engage()/Update() read Field18->GetZoneDb()->GetId().
    float DeltaTime1c;           // +0x1c. Per-tick delta time; read (never written) by Update()'s
                                  // own state machine (Field184/Field188 accumulation, and passed
                                  // as the "advance by DeltaTime1c" argument to SubObjectA/B's own
                                  // vtbl slot 1 calls). Same offset/role LootBehavior's own
                                  // Update() reads too (common_lootbehavior.h).
    void* BehaviorMgrPtr20;     // +0x20. Set by SetContext() (Param2); read by Disengage().
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3).
    unsigned char Flags28;      // +0x28. Bit 0x2 tested by SetModeFromFlags()/ApplyDirective();
                                 // bit 0x1 set, bit 0x2 cleared by Disengage().
    unsigned char Flag29;       // +0x29. Read by SetModeFromFlags().
    unsigned char Pad2a[0x2c - 0x2a];

    // +0x2c. IDENTIFIED 2026-09-11 (d285:common-port): real, already-ported
    // MoveIdea (common_idea.h) -- ctor FUN_062d4e60 (same ctor function as
    // DockBehavior::SubObjectA, common_dockbehavior.h) matches MoveIdea's own
    // confirmed ctor address; sizeof(MoveIdea)==0x78 matches this field's
    // long-confirmed span. SetContext() now calls MoveIdea::SetContext() for
    // real (own vtable slot 0); Disengage() and Update()'s case-5/case-4
    // payload now call MoveIdea::Update()/Commit() for real via
    // TickSubObject()/ReleaseSubObject() above.
    MoveIdea SubObjectA;    // +0x2c.
    // +0xa4. IDENTIFIED 2026-09-11 (d285:common-port): real, already-ported
    // FaceIdea (common_idea.h) -- ctor FUN_062d6750 (same ctor function as
    // DockBehavior::SubObjectB) matches FaceIdea's own confirmed ctor
    // address; sizeof(FaceIdea)==0xc0 matches this field's span. Wired the
    // same way as SubObjectA above.
    FaceIdea SubObjectB;    // +0xa4.

    int ModeState164;           // +0x164. Zeroed by ctor; set to 1 by Engage().
    BaseWatcher Watcher168;     // +0x168 (8 bytes, ends +0x170). Zeroed by ctor (both dwords in
                                 // one instruction pair, matching DockBehavior::Watcher324's own
                                 // ctor pattern); dtor clears it via BaseWatcher::set_pointer(nullptr).
    unsigned CachedSystemId170; // +0x170. Engage(): Field18->GetZoneDb()->GetId() (the current
                                 // system's id); Update() aborts with 2 when it changes.
    unsigned TargetType174;     // +0x174. Engage(): FUN_062e6200 == GetObjectTypeOrZero on the
                                 // watched launch target (target - 8).
    int DirectiveField178;      // +0x178. Zeroed by ctor; copied from ApplyDirective()'s Param+0x10.
    unsigned char Flag17c;      // +0x17c. Cleared by Engage().
    unsigned char Pad17d[0x180 - 0x17d];
    float Field180;             // +0x180. Set to 3.5f by ctor (MOV [ESI+0x180],0x40600000 at
                                 // Common.dll.txt 202888). Mode-1 dwell threshold: Update()'s case 1
                                 // leaves mode 1 once Field184 > Field180.
    float Field184;             // +0x184. Mode-1 dwell accumulator. Cleared by Engage() (NOT by the
                                 // ctor -- FUN_062cd640 never touches +0x184). `Field184 += DeltaTime1c`
                                 // every mode-1 tick.
    float Field188;             // +0x188. Whole-launch elapsed-time accumulator. Cleared by ctor
                                 // (202883) AND by Engage(). `Field188 += DeltaTime1c` every tick,
                                 // and Update() aborts with result 3 once Field188 > Field18c.
    float Field18c;             // +0x18c. Launch timeout. Set by SetContext() to 15.0f or 60.0f
                                 // (never left alone -- see SetContext()'s own comment). NOT
                                 // ctor-initialized in retail.
    // ------------------------------------------------------------------
    // +0x190..+0x22c (156 bytes): Update()'s own case-2/3/5 vector workspace,
    // confirmed this pass by reading LAB_062cd950's raw bytes directly (the
    // 9-dword MOVSD.REP struct-copy destinations the header used to cite
    // generically as "+0x1fc/+0x1cc/+0x208" are now individually typed
    // below). Populated exclusively by ComputeLaunchApproachTransform()'s
    // still-FULLPORT-DEFERRED payload; LaunchBasis is additionally rotated
    // in place by Update()'s own case 5 (ported for real, pure FPU math).
    // ------------------------------------------------------------------
    Vector Field190;             // +0x190 (ends +0x19c). Delta vector computed by
                                  // ComputeLaunchApproachTransform() (hardpoint pos minus
                                  // NavHelper nested-object pos).
    Vector Field19c;             // +0x19c (ends +0x1a8). Result vector from
                                  // ComputeLaunchApproachTransform()'s DAT_063fc538 singleton call;
                                  // also written by Update()'s case 5 nested-object query.
    Vector LaunchBasis[3];       // +0x1a8 (ends +0x1cc). 3-Vector "launch orientation/direction
                                  // basis" -- populated by ComputeLaunchApproachTransform()'s first
                                  // 9-dword MOVSD.REP copy (from the rawblock vtbl+0xc8 transform
                                  // call's out-buffer), then rotated in place by pi radians around
                                  // the (x,z) plane (DAT_0639e2d0) by Update()'s own case 5 --
                                  // CONFIRMED, ported for real (see Update()'s .cpp comment).
    Vector Field1cc;             // +0x1cc (ends +0x1d8). Discrete-store vector from
                                  // ComputeLaunchApproachTransform()'s rawblock vtbl+0xc8 out-params.
    Vector ApproachBasis2[3];    // +0x1d8 (ends +0x1fc). Second 9-dword MOVSD.REP copy destination
                                  // from the same rawblock vtbl+0xc8 call.
    Vector Field1fc;             // +0x1fc (ends +0x208). Discrete-store vector from the SAME call.
    Vector ApproachBasis3[3];    // +0x208 (ends +0x22c). Third 9-dword MOVSD.REP copy destination;
                                  // ends exactly at Field22c below (confirmed contiguous).
    float Field22c;              // +0x22c. Set to 15.0f by ctor. Read by case 4 (passed as
                                  // SetApproachThrottleScale()'s argument, FULLPORT-DEFERRED).
    unsigned char Flag230;       // +0x230. Zeroed by ctor.
    unsigned char Flag231;       // +0x231. Zeroed by ctor.
};

static_assert(offsetof(LaunchBehavior, CachedHandle08) == 0x08, "LaunchBehavior::CachedHandle08 offset");
static_assert(offsetof(LaunchBehavior, ModeSelector0xc) == 0x0c, "LaunchBehavior::ModeSelector0xc offset");
static_assert(offsetof(LaunchBehavior, UpdateResult10) == 0x10, "LaunchBehavior::UpdateResult10 offset");
static_assert(offsetof(LaunchBehavior, ThreatSourcePtr14) == 0x14, "LaunchBehavior::ThreatSourcePtr14 offset");
static_assert(offsetof(LaunchBehavior, Field18) == 0x18, "LaunchBehavior::Field18 offset");
static_assert(offsetof(LaunchBehavior, BehaviorMgrPtr20) == 0x20, "LaunchBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(LaunchBehavior, NavHelperPtr24) == 0x24, "LaunchBehavior::NavHelperPtr24 offset");
static_assert(offsetof(LaunchBehavior, Flags28) == 0x28, "LaunchBehavior::Flags28 offset");
static_assert(offsetof(LaunchBehavior, Flag29) == 0x29, "LaunchBehavior::Flag29 offset");
static_assert(offsetof(LaunchBehavior, SubObjectA) == 0x2c, "LaunchBehavior::SubObjectA offset");
static_assert(offsetof(LaunchBehavior, SubObjectB) == 0xa4, "LaunchBehavior::SubObjectB offset");
static_assert(offsetof(LaunchBehavior, ModeState164) == 0x164, "LaunchBehavior::ModeState164 offset");
static_assert(offsetof(LaunchBehavior, Watcher168) == 0x168, "LaunchBehavior::Watcher168 offset");
static_assert(offsetof(LaunchBehavior, CachedSystemId170) == 0x170, "LaunchBehavior::CachedSystemId170 offset");
static_assert(offsetof(LaunchBehavior, TargetType174) == 0x174, "LaunchBehavior::TargetType174 offset");
static_assert(offsetof(LaunchBehavior, DirectiveField178) == 0x178, "LaunchBehavior::DirectiveField178 offset");
static_assert(offsetof(LaunchBehavior, Flag17c) == 0x17c, "LaunchBehavior::Flag17c offset");
static_assert(offsetof(LaunchBehavior, Field180) == 0x180, "LaunchBehavior::Field180 offset");
static_assert(offsetof(LaunchBehavior, Field184) == 0x184, "LaunchBehavior::Field184 offset");
static_assert(offsetof(LaunchBehavior, Field188) == 0x188, "LaunchBehavior::Field188 offset");
static_assert(offsetof(LaunchBehavior, Field18c) == 0x18c, "LaunchBehavior::Field18c offset");
static_assert(offsetof(LaunchBehavior, Field190) == 0x190, "LaunchBehavior::Field190 offset");
static_assert(offsetof(LaunchBehavior, Field19c) == 0x19c, "LaunchBehavior::Field19c offset");
static_assert(offsetof(LaunchBehavior, LaunchBasis) == 0x1a8, "LaunchBehavior::LaunchBasis offset");
static_assert(offsetof(LaunchBehavior, Field1cc) == 0x1cc, "LaunchBehavior::Field1cc offset");
static_assert(offsetof(LaunchBehavior, ApproachBasis2) == 0x1d8, "LaunchBehavior::ApproachBasis2 offset");
static_assert(offsetof(LaunchBehavior, Field1fc) == 0x1fc, "LaunchBehavior::Field1fc offset");
static_assert(offsetof(LaunchBehavior, ApproachBasis3) == 0x208, "LaunchBehavior::ApproachBasis3 offset");
static_assert(offsetof(LaunchBehavior, Field22c) == 0x22c, "LaunchBehavior::Field22c offset");
static_assert(offsetof(LaunchBehavior, Flag230) == 0x230, "LaunchBehavior::Flag230 offset");
static_assert(offsetof(LaunchBehavior, Flag231) == 0x231, "LaunchBehavior::Flag231 offset");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x234`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(LaunchBehavior) <= 0x234, "LaunchBehavior must not exceed its retail size 0x234");
