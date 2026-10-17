// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_iface.h"     // IObjAffect::RequestCB
#include "common_ini.h"       // Vector

#include <cstddef>

// ============================================================================
// InstantTradeLaneBehavior -- AI trade-lane state machine (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology, and common_dockbehavior.h's header comment
// for the worked example this pass reused). This is the SECOND class flagged
// by that DockBehavior pass as sharing the identical
// `Behavior + IObjAffect::RequestCB(mdisp=0x2C)` multiple-inheritance shape --
// this pass follows through on it.
//
//   RTTICompleteObjectLocator @ .rdata:0x063a9138 (offset field = 0, i.e. the
//   primary vtable installs at object offset 0)
//     -> pTypeDescriptor  @ .data:0x063ec00c, name ".?AVInstantTradeLaneBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a918c, numBaseClasses = 3,
//        BaseClassArray @ 0x063a917c = [ InstantTradeLaneBehavior(self,
//                                          numContained=2, mdisp=0)
//                                        @0x063a9164,
//                                        Behavior(numContained=0, mdisp=0)
//                                        @0x063a8c78 -- BYTE-IDENTICAL,
//                                          linker-shared RTTIBaseClassDescriptor
//                                          with DockBehavior's own Behavior
//                                          base entry (same address, 7-way
//                                          XREF'd, confirmed COMDAT-folded --
//                                          not evidence of any relationship
//                                          beyond "both derive from Behavior"),
//                                        IObjAffect::RequestCB(numContained=0,
//                                          mdisp=0x2c, attributes=9)
//                                        @0x063a914c -- NOT shared with
//                                          DockBehavior's own RequestCB
//                                          descriptor (different address,
//                                          different attributes field: 9 here
//                                          vs 0 there) ]
//
// A SECOND RTTICompleteObjectLocator exists at .rdata:0x063a919c (signature=0,
// offset field = 0x2C, same pTypeDescriptor/pClassHierarchyDescriptor as the
// primary COL) -- the secondary-vtable COL required by the MSVC ABI for the
// second polymorphic base (IObjAffect::RequestCB), confirming mdisp=0x2C
// independently of the RTTIBaseClassDescriptor above.
//
// Ground truth for mdisp=0x2C is further confirmed by this class's own
// constructor (Common.dll.txt 205207-205221, FUN_062cee50):
// `MOV dword ptr [ESI+0x2c],PTR_LAB_0639fe38` installs the secondary
// (RequestCB) vtable pointer at exactly object+0x2C, right after
// `MOV dword ptr [ESI],PTR_FUN_0639fe44` installs the primary vtable at
// object+0 -- byte-for-byte the same pattern DockBehavior's ctor uses.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062cee50 (Common.dll.txt 205207-205221). This address was
// ALREADY tracked (decomp-tracker item 1442) under the "TractorArm dispatch
// table" investigation before this pass connected it to the RTTI class name --
// item 1442's ported_name "TractorArm::InstantTradeLane (goto-directive mode;
// ctor)" and this class are the SAME constructor/class; the dispatch-table row
// named it "InstantTradeLane", RTTI names the class InstantTradeLaneBehavior,
// both refer to one retail type. Sole factory caller is FUN_062d1af0 (item
// 1514), which does `operator_new(0x5c)` then this ctor -- CONFIRMS the
// object's total retail size is exactly 0x5c (92) bytes, independently of the
// field-by-field trace below.
//
// The ctor itself is tiny: it calls the shared TractorArm-family base-init
// helper FUN_062d2020 (item 1529, "TractorArm::ModeBase", already a
// REAL field-initialising base ctor -- it writes +0x04..+0x29 (see Behavior in common_behavior.h); the long-recorded "no-op stub" reading was wrong -- same call DockBehavior's
// ctor makes), installs both vtable pointers, and does NOTHING else -- no
// direct field writes at all. Every field below (even the ones inside the
// +0x04..+0x2c "own fields" span that DockBehaviorFields also models) is
// therefore only ever touched by the vtable-slot methods, never zeroed by the
// ctor itself in retail; this port zero-initializes everything via the
// member-initializer list anyway (documented gap vs. retail, consistent with
// this marathon's "fully-defined port object" precedent).
//
// ----------------------------------------------------------------------------
// Destructor: the scalar-deleting-destructor thunk at vtable slot 0
// (FUN_062c7090, decomp-tracker item 1320) is SHARED verbatim (identical
// machine code, confirmed ICF fold) across 7 unrelated vtables in this
// module -- already correctly tracked `skipped`, not this class's own logic.
// Unlike DockBehavior (whose slot-0 thunk calls a SEPARATE real destructor
// that clears 6 BaseWatcher-shaped fields first), THIS class's thunk body
// calls `thunk_FUN_062d2080` DIRECTLY with no class-specific cleanup at all
// (Common.dll.txt 193351-193362) -- consistent with this class having no
// BaseWatcher-shaped owned sub-objects to tear down. FUN_062d2080 (decomp-
// tracker item 1531, `blocked`) is the same shared/ICF-ambiguous family base-
// destructor DockBehavior's own real destructor also tail-calls; left
// FULLPORT-DEFERRED here for the identical reason.
//
// ----------------------------------------------------------------------------
// Vtable slot map (see the class body below for per-slot ported/deferred
// status). Primary vtable .rdata:0x0639fe44, 27 slots (0x0639fe44-0x0639feac
// inclusive -- byte distance to the next vtable head at 0x0639feb4 confirms
// this bound, exactly matching DockBehavior's own 27-slot primary vtable
// shape). Several slots are confirmed MSVC ICF (identical code folding)
// artifacts shared with unrelated, already-ported classes (CEquip::
// ConnectServerEquip, CEquip::DisableController, Exclusion::
// AsteroidExclusionZone.., IObjInspectImpl::cobject, MetaList<GoodDesc>::
// operator[], Archetype::AttachedEquipment::i.., CEShield::GetEquipDesc,
// RDLNode::operator_class_AttribStackNode) -- these carry no
// InstantTradeLaneBehavior-specific logic despite the name, matching the
// exact same ICF pattern already documented on DockBehavior's vtable.
//
//   0  (0x00) FUN_062c7090        item 1320  scalar-deleting-dtor thunk (ICF-shared
//                                            MSVC boilerplate; real dtor is
//                                            thunk_FUN_062d2080, FULLPORT-DEFERRED)
//   1  (0x04) LAB_062cee70        item 1443  ported for real: SetContext() -- renamed and
//                                            re-signed 2026-09-17 from the mistaken
//                                            `void ResetIfTargetQueryFailed(void*, void*)`;
//                                            retail's RET 0xc proves three parameters
//   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not this class's own logic
//   3  (0x0c) FUN_062cef10        item 1445  ported for real: TurnOff()
//   4  (0x10) FUN_062cef40        item 1446  ported for real: RequestExitTradeLane()
//   5  (0x14) LAB_062ceeb0        item 1444  ported for real: UpdateMode()
//   6  (0x18) LAB_062cf5e0        item 1460  ICF-shared trivial "return 2" thunk (blocked
//                                            per its own tracker item, not attributable)
//   7  (0x1c) LAB_062cefb0        item 1447  ported (outer guards + state dispatch
//                                            skeleton): Update()
//   8  (0x20) Exclusion::AsteroidExclusionZone..  ICF fold
//   9  (0x24) IObjInspectImpl::cobject          ICF fold
//  10  (0x28) LAB_062cf0f0        item 1448  ported (outer logic): AttachDirective()
//  11  (0x2c) CEquip::DisableController          ICF fold (real, unrelated, ready_verified
//                                            elsewhere -- common_cequip.cpp)
//  12  (0x30) MetaList<GoodDesc>::operator[]     ICF fold
//  13  (0x34) MetaList<GoodDesc>::operator[]     ICF fold (same target as slot 12)
//  14  (0x38) MetaList<GoodDesc>::operator[]     ICF fold (same target as slot 12)
//  15  (0x3c) MetaList<GoodDesc>::operator[]     ICF fold (same target as slot 12)
//  16  (0x40) FUN_062d2110        item 1535  shared/unprocessed family helper (see
//                                            DockBehavior's own slot 16 for the same
//                                            target); not this class's own logic
//  17  (0x44) FUN_062d21b0        item 1536  MSVC MI adjustor thunk (skipped, shared
//                                            across 20 unrelated vtable slots)
//  18  (0x48) MetaList<GoodDesc>::operator[]     ICF fold (same target as slot 12)
//  19  (0x4c) Archetype::AttachedEquipment::i.. ICF fold
//  20  (0x50) LAB_062c2f40        item 1262  trivial (2 insns), ICF-shared with
//                                            DockBehavior's own slot 20 -- already tracked
//  21  (0x54) MetaList<GoodDesc>::operator[]     ICF fold (same target as slot 12)
//  22  (0x58) LAB_062cf1d0        item 1450  ported (outer logic): HandleTargetEvent()
//  23  (0x5c) LAB_062c2f50        item 1263  trivial (2 insns), ICF-shared with
//                                            DockBehavior's own slot 23 -- already tracked
//  24  (0x60) RDLNode::operator_class_AttribStackNode  ICF fold (real, ready_unverified
//                                            elsewhere -- common_text.h/.cpp)
//  25  (0x64) CEShield::GetEquipDesc            ICF fold (real, ready_verified elsewhere)
//  26  (0x68) RDLNode::operator_class_AttribStackNode  ICF fold, same target as slot 24
//
// Secondary vtable (.rdata:0x0639fe38, IObjAffect::RequestCB base, mdisp=0x2C):
// exactly 1 slot, matching DockBehavior's own secondary-vtable shape.
//   0  (0x00) LAB_062cf150        item (see .cpp) ported (outer logic): OnResult() override
// ============================================================================

// Intermediate base holding InstantTradeLaneBehavior's own +0x04..+0x2c
// fields. MSVC lays out multiple-inheritance bases in declaration order,
// THEN the derived class's own members -- it does NOT interleave a derived
// class's own fields between two of its own direct bases (see
// common_dockbehavior.h's DockBehaviorFields for the full explanation of this
// gotcha, which this class hits identically). To get retail's real layout
// (own data at +0x04..+0x2c, THEN the IObjAffect::RequestCB secondary vtable
// pointer at +0x2c, THEN this class's remaining own members starting at
// +0x30) this "own data" block has to live on its own intermediate base --
// InstantTradeLaneBehavior then multiply-inherits from THIS class (primary)
// and RequestCB (secondary), with its own remaining members (State30 onward)
// naturally landing after both, at +0x30.
//
// Several of these fields (ModeSelector0xc/TargetPtr14/Flags28/Flag29) sit at
// the EXACT SAME offsets, with the EXACT SAME confirmed semantics, as
// DockBehaviorFields' own fields of the same name -- independently re-derived
// here from this class's own vtable-slot bodies (LAB_062ceeb0/FUN_062cef40),
// not copied from DockBehavior. This is strong evidence the retail compiler
// built both classes from a shared "TractorArm mode base fields" template/
// mixin (matching FUN_062d2020's role as the shared base-init helper both
// ctors call), not that one derives from the other -- RTTI already rules out
// any inheritance relationship between the two (see the header comment above:
// both are independent, unrelated direct children of Behavior).
class InstantTradeLaneBehaviorFields : public Behavior
{
public:
    // +0x04. RECLASSIFIED 2026-09-17 (claude:common-largeship-itl) from `void* Field04Ptr`
    // back to opaque padding, because the one piece of evidence for it was a MISREAD. The old
    // note said it is "dereferenced exactly once, by Update()'s state-3 case (`MOV EDX,[ECX+0x4]`
    // then `CALL [EDX+0x64]`)". At that instruction ECX is NOT `this`: Update() loads
    // `ECX = [ESI+0x14]` (TargetPtr14) at .text:062cefcc, before the switch, and never reloads
    // it. So `[ECX+0x4]` is TargetPtr14+4 -- the IObjAffect sub-object of an IObjRW -- and the
    // `ADD ECX,0x4` that follows is the standard base-adjustment for calling it, not an
    // adjustment onto this object's own +0x04. Nothing anywhere in this class touches +0x04.
    //
    // IDENTIFIED 2026-09-17 (claude:common-factory). "Nothing in this class touches it" was
    // right and is not in conflict with what follows: +0x04 is written by the SHARED base
    // constructor FUN_062d2020, not by anything this class declares, and nothing in
    // Common.dll reads it back.
    explicit InstantTradeLaneBehaviorFields(int InTypeId);

    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.

    // +0x08. NEWLY IDENTIFIED 2026-09-17 (claude:common-largeship-itl): the old layout hid
    // this inside opaque padding because nothing in this class was known to write it. It is
    // written -- by this class's own vtable slot 1, which turns out to be the family's
    // SetContext (see the method below): the shared initializer FUN_062d2090 stores
    // IObjInspect::get_id()'s result here. Same field, same name, same type as every sibling
    // in this family (LargeShipBehavior, DockBehavior, ...).
    unsigned CachedHandle08;

    int ModeSelector0xc;        // +0x0c. Written by UpdateMode() (values 1/2/3); not
                                 // otherwise read by anything this pass ported.
    int UpdateResult10;         // +0x10. Update()'s own tri-state result/early-out code
                                 // (values 0/1/2/3 observed), analogous in spirit (not
                                 // offset) to DockBehavior's UpdateResult10/FormationBehavior's
                                 // BaseFollowerBehavior::UpdateResult.

    // +0x14. TYPED 2026-09-17: this is an IObjRW*, not an unidentified pointer. Proof is
    // structural, not by shape-matching: Update()'s state-0 case calls its vtbl+0x138 with
    // exactly four arguments in exactly the order of IObjInspect::get_lane_start(IObjInspect
    // const* Self, IObjInspect const*& OutLane, Vector& OutStart, Vector& OutEnd) -- slot 78
    // of the retail-order table rebuilt in common_iface.h -- and its state-3/OnResult/
    // HandleTargetEvent cases call vtbl+0x64 and vtbl+0x60 through the sub-object at +0x4,
    // which is where IObjRW's RTTI puts its IObjAffect base. IObjInspect therefore sits at
    // offset 0, which is what the other three call sites (vtbl+0x20 == get_id, vtbl+0x150 ==
    // cobject) independently require. A trade-lane state machine asking its target for
    // get_lane_start is also exactly the semantic this class's RTTI name predicts.
    IObjRW* TargetPtr14;
    unsigned char Opaque18[0x20 - 0x18]; // +0x18, never touched by anything read this pass

    // +0x20. RENAMED 2026-09-17 from `HelperPtr20`. This is the Behavior family's ordinary
    // IBehaviorManager back-pointer: the shared context initializer FUN_062d2090 (item 1532,
    // ready_unverified) writes Param2 to +0x20 and Param3 to +0x24 on EVERY class in this
    // family, and all of this class's own uses of +0x20 are IBehaviorManager operations
    // (FUN_062d4520 == IBehaviorManager::QueueNotification, item 1561; FUN_062d29a0's two
    // field writes at manager+0x118/+0x11c). Same field, same name, as LargeShipBehavior's.
    IBehaviorManager* BehaviorMgrPtr20;

    // +0x24. RENAMED 2026-09-17 from `ControllerPtr24`, same reasoning as above: +0x24 is the
    // family's NavHelper pointer (FUN_062d2090's Param3). AttachDirective() passes
    // (NavHelperPtr24 + 0x34) to FUN_062e74a0 -- the identical `NavHelper+0x34` lookup
    // LaunchBehavior/TrailBehavior/FaceBehavior all perform. TurnOff()'s own use of it as a
    // ThrustManager base is unchanged and still deferred.
    SystemManager* NavHelperPtr24;
    unsigned char Flags28;       // +0x28. RequestExitTradeLane() ORs bit 0x1;
                                 // Update()'s very first check tests bit 0x1 (early-exit,
                                 // matching DockBehavior's Flags28 bit 0x1 early-exit shape);
                                 // UpdateMode() tests bit 0x2.
    unsigned char Flag29;       // +0x29. UpdateMode() tests it (nonzero => mode 3) -- exact
                                 // same semantics as DockBehaviorFields::Flag29.
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only
};
static_assert(offsetof(InstantTradeLaneBehaviorFields, CachedHandle08) == 0x08, "InstantTradeLaneBehaviorFields::CachedHandle08 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, ModeSelector0xc) == 0x0c, "InstantTradeLaneBehaviorFields::ModeSelector0xc offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, UpdateResult10) == 0x10, "InstantTradeLaneBehaviorFields::UpdateResult10 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, TargetPtr14) == 0x14, "InstantTradeLaneBehaviorFields::TargetPtr14 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, BehaviorMgrPtr20) == 0x20, "InstantTradeLaneBehaviorFields::BehaviorMgrPtr20 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, NavHelperPtr24) == 0x24, "InstantTradeLaneBehaviorFields::NavHelperPtr24 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, Flags28) == 0x28, "InstantTradeLaneBehaviorFields::Flags28 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, Flag29) == 0x29, "InstantTradeLaneBehaviorFields::Flag29 offset");
static_assert(sizeof(InstantTradeLaneBehaviorFields) == 0x2c, "InstantTradeLaneBehaviorFields size (own data ends right before the RequestCB secondary vtable ptr at +0x2c)");

class InstantTradeLaneBehavior : public InstantTradeLaneBehaviorFields, public IObjAffect::RequestCB
{
public:
    // Retail's ctor argument is the behavior type id (9, "InstantTradeLane"); it is forwarded to InstantTradeLaneBehaviorFields.
    // See Behavior::GetTypeId() in common_behavior.h.
    explicit InstantTradeLaneBehavior(int InTypeId);

    // Scalar-deleting-destructor thunk (FUN_062c7090, item 1320) calls
    // thunk_FUN_062d2080 directly with no class-specific cleanup -- this
    // class owns no BaseWatcher-shaped sub-objects to tear down, unlike
    // DockBehavior. FUN_062d2080 is ~Behavior (item 1531), run implicitly after this body.
    ~InstantTradeLaneBehavior() override;

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 205222-205244 (LAB_062cee70).
    //
    // RENAMED AND RE-SIGNED 2026-09-17 (claude:common-largeship-itl). This was declared
    // `void ResetIfTargetQueryFailed(void* Param1, void* Param2)` -- wrong on all three
    // counts, and the arity error is exactly the stack-corrupting class of bug this
    // codebase has a documented history of: retail ends this method with `RET 0xc`
    // (.text:062ceea2), i.e. THREE stack parameters, so a two-parameter declaration leaves
    // four bytes of caller stack unpopped on every call. It also returns a bool in AL, via
    // FUN_062d2090's own `MOV AL,1` / `XOR AL,AL` tail, which the void return discarded.
    //
    // It is not a "reset if the target query failed" helper at all: it is this family's
    // ordinary SetContext override. Retail forwards all three parameters unchanged to
    // FUN_062d2090 (item 1532, ready_unverified -- ALREADY PORTED, contrary to the stale
    // "unprocessed" note this carried; the same initializer LargeShipBehavior::SetContext
    // inlines), and clears the cached lane state only when that install SUCCEEDS
    // (`CMP AL,CL; JZ` skips the clears, so they run on the nonzero/true path).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 205303-205322
    // (FUN_062cef10, decomp-tracker item 1445 -- already independently
    // documented as "TurnOff/cruise-disengage" before this pass connected it
    // to this class's RTTI name). "Turn off / release cruise lock": mirrors
    // FormationBehavior::SetCruiseLock/ReleaseGotoDirective's existing
    // FULLPORT-DEFERRED stub precedent (formerly in common_behavior.cpp; Behavior::SetCruiseLock has since been ported there) for the
    // exact same still-unported ThrustManager/cruise-lock subsystem.
    void TurnOff();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 205326-205368
    // (FUN_062cef40). CLOSED 2026-09-17 (claude:common-largeship-itl): the
    // CShip resolution this used to defer is now real code, and the old
    // reason ("TargetPtr14's real interface is not identified") is disproved.
    // Retail, .text:062cef66-062cef87:
    //   CObject const* Obj = TargetPtr14->cobject();        // vtbl+0x150,
    //                                                       // IObjInspect slot 84
    //   CShip* Ship = (Obj && (Obj->ObjectClass & 0x503) == 0x503)
    //                     ? (CShip*)Obj : nullptr;          // +0x4c IS CObject::
    //                                                       // ObjectClass, common_cobject.h
    //   Ship->request_exit_tradelane();                     // called UNCONDITIONALLY
    // The `0x4c` in the old note was never a vtable slot on TargetPtr14 -- it is a
    // plain field offset on the CObject the 0x150 call returns, and this port has
    // had that field typed all along.
    void RequestExitTradeLane();

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 205256-205297
    // (LAB_062ceeb0). 3-state mode selector, same overall shape as
    // DockBehavior::SetModeFromFlags() but with extra gating logic, all
    // nested under a single TargetPtr14 guard:
    //   mode = 2;
    //   if (TargetPtr14) {
    //       if (Flags54) mode = 1;
    //       if (SubObjPtr34) {
    //           uint Type = 0; SubObjPtr34->get_type(Type);   // vtbl+0x88, slot 34
    //           if (Type & 0x80) mode = 1;                     // low byte's sign bit
    //       }
    //   }
    //   if (Flag29) mode = 3;
    //   ModeSelector0xc = mode;
    // CLOSED 2026-09-17: the vcall is IObjInspect::get_type(uint&). Retail reads only
    // the out-parameter's LOW BYTE and branches on JNS (.text:062ceee1-062ceee7), so
    // the real predicate is `Type & 0x80`, not a signed comparison on the whole dword.
    int UpdateMode();   // returns the mode in EAX (`MOV EAX,EDI` / `MOV EAX,3`, RET 4)

    // Own vtable slot 7 (offset 0x1c), Update/dispatch entry point.
    // Common.dll.txt 205382-205523 (LAB_062cefb0), 124 instructions across a
    // 7-way (State30 0-6) switch.
    //
    // THE SWITCH IS NOW REAL CODE (2026-09-17, claude:common-largeship-itl). The whole
    // body used to be one FULLPORT-DEFERRED block whose stated reason -- "a call through
    // `*this`'s own vtable at byte offset 0x138, which exceeds this class's own 27-slot
    // vtable, so its real target is not resolvable from this function alone" -- rested on
    // a register misread and is now disproved. ECX is loaded with TargetPtr14 at
    // .text:062cefcc, BEFORE the switch, and is never reloaded; so 0x138 is a slot on
    // TargetPtr14's vtable, not on this object's, and there is nothing paradoxical about
    // its size. Everything follows from that one correction:
    //   state 0: `TargetPtr14->get_lane_start(SubObjPtr34, OutLane, LaneStart3c, LaneEnd48)`
    //            (IObjInspect slot 78). Nonzero return -> UpdateResult10 = 3, return 3.
    //            Zero -> LanePtr38 = OutLane, then State30 = 3, Flag56 = 1, Flag55 = 0,
    //            UpdateResult10 = 1, return 1. NOTE the polarity: an older summary had
    //            "on vcall failure (result == 0)" -- backwards; `JZ` takes the SUCCESS path.
    //            Skipped entirely when LanePtr38 is already set.
    //   state 3: IObjAffect(TargetPtr14 + 4)'s slot 0x64, args (SubObjPtr34, LanePtr38, 1,
    //            Field58). Zero -> BehaviorMgrPtr20->QueueNotification(TargetPtr14, 0, 0.0f)
    //            (FUN_062d4520 == item 1561, ported), UpdateResult10 = 0, Flags54 = 1,
    //            State30 = 4, return 0. Nonzero -> State30 = 6, UpdateResult10 = 1,
    //            return 1. (An older summary said the failure path returns 0; it does not --
    //            EDI is still 1 there, it is only zeroed on the success path.)
    //   states 5/6: Flags28 |= 1, then this->RequestExitTradeLane() (own vtable slot 4),
    //            then the manager-field write FUN_062d29a0 (item 1548) with
    //            mgr[0x11c] = 9 and mgr[0x118] = (state 6 ? 2 : 0); UpdateResult10 = 2.
    //   states 1/2/4 and above 6: UpdateResult10 = 1 (EDI from 062cefe5), no other effect.
    // The ONE thing still deferred is IObjAffect's slot 0x64 signature: common_iface.h
    // currently declares slots 0x60/0x64 with no parameters, so this file calls it through
    // a documented raw dispatch. See the .cpp.
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

    // The other overrides retail's vftable .rdata:0639fe44 holds (raw .rdata dwords,
    // 2026-10-08). Until then slots 3/4/5/10/22 reached Behavior's base bodies.
    void Slot3() override { TurnOff(); }                                   // 3  FUN_062cef10
    void Slot4() override { RequestExitTradeLane(); }                      // 4  FUN_062cef40
    int Slot5(float) override { return UpdateMode(); }                     // 5  LAB_062ceeb0 (RET 4)
    bool Slot10(void* DirectiveOp) override { return AttachDirective(DirectiveOp); } // 10 LAB_062cf0f0
    void Slot22(void* Event) override { HandleTargetEvent(Event); }        // 22 LAB_062cf1d0

    // Own vtable slot 10 (offset 0x28). Common.dll.txt 205524-205562
    // (LAB_062cf0f0). CLOSED 2026-09-17 (claude:common-largeship-itl) -- every one of the
    // 37 instructions is now real code. The GAP:AI-MANEUVER-EXECUTOR tag this carried was
    // stale: FUN_062e74a0 (item 1798) is NOT "still-unprocessed", it was closed on
    // 2026-09-11 and exported for real (SystemManagerTimerTable::FindVerifiedObject,
    // common_sysmgr_timertable.cpp). It is now called twice, as
    // retail does, against (NavHelperPtr24 + 0x34).
    bool AttachDirective(void* Directive);

    // Own vtable slot 22 (offset 0x58). Common.dll.txt 205627-205661
    // (LAB_062cf1d0). Tagged-event handler: if the caller-supplied event's
    // tag == 4, its first payload byte == 0, and TargetPtr14's vtable slot
    // 0x20 result matches the event payload's own +0xc field, transitions
    // State30 to 5 UNCONDITIONALLY (not gated on SubObjPtr34 -- confirmed via
    // raw disasm ordering), then, only if SubObjPtr34 is set, re-validates it
    // through a vcall chain against TargetPtr14's own +4 embedded sub-object.
    // 2026-09-17: both owning interfaces ARE now identified -- vtbl+0x20 on
    // either pointer is IObjInspect::get_id(), and TargetPtr14+4 is the
    // IObjAffect sub-object. The id-match guard is real code now; only
    // IObjAffect::slot_60(2, &SavedId, 0) remains a raw dispatch, for want of
    // a declared signature in common_iface.h.
    void HandleTargetEvent(void* Event);

    // IObjAffect::RequestCB override -- own single secondary-vtable slot.
    // Common.dll.txt 205566-205611 (LAB_062cf150), 47 instructions. Entered
    // with `this` already adjusted to the RequestCB sub-object (+0x2c);
    // retail recovers the outer InstantTradeLaneBehavior* via a further
    // -0x18 adjustment from THAT pointer (ESI-0x18 relative to the adjusted
    // `this`, i.e. outer+0x14 -- confirmed by cross-referencing against the
    // exact same TargetPtr14 field UpdateMode()/Update() read directly).
    // Sets State30 to 3 or 6 depending on Param3, then (only on the ==-1
    // path) does a vcall-driven re-validation through SubObjPtr34/TargetPtr14
    // -- FULLPORT-DEFERRED, see .cpp.
    bool OnResult(int Param1, int Param2, int Param3) override;

    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. Struct sized to exactly 0x5c (92) bytes -- CONFIRMED
    // (not just "largest field referenced") by the factory's own
    // `operator_new(0x5c)` call (FUN_062d1af0, item 1514).
    // ------------------------------------------------------------------

    // +0x04..+0x2c: InstantTradeLaneBehavior's own data (0x28 bytes) -- lives
    // on the InstantTradeLaneBehaviorFields intermediate base above, so it
    // isn't redeclared here.

    // +0x2c: IObjAffect::RequestCB secondary vtable pointer (base-class
    // subobject, placed here implicitly by the compiler -- no explicit field
    // needed). State30 below starts right after it (0x2c + sizeof(vfptr) ==
    // 0x30).

    int State30;                // +0x30. Zeroed by ctor (documented gap -- retail's ctor
                                 // never actually zeroes it, see the class-level comment
                                 // above). Set to 3/4/5/6 by Update()/OnResult()/TurnOff();
                                 // Update()'s own dispatch switch reads it.

    // +0x34. TYPED 2026-09-17: an IObjInspect const*. Proof: Update()'s state-0 case passes
    // this field, unchanged, as the `Self` argument of TargetPtr14->get_lane_start(IObjInspect
    // const* Self, ...) -- see TargetPtr14's own note above. Every other read of it agrees:
    // UpdateMode() calls its vtbl+0x88 (IObjInspect::get_type) and HandleTargetEvent()/
    // OnResult() call its vtbl+0x20 (IObjInspect::get_id). Resolved by AttachDirective() via
    // FUN_062e74a0(NavHelperPtr24 + 0x34, Directive + 0xc). Name left at its offset spelling:
    // the interface is proven, the object's ROLE (which trade-lane-adjacent entity it is)
    // is not, and this marathon does not permit an invented semantic name for that.
    IObjInspect const* SubObjPtr34;

    // +0x38. RENAMED 2026-09-17 from `Field38`. This is the out-parameter of
    // TargetPtr14->get_lane_start(...), i.e. the resolved trade lane, written at
    // .text:062cf02f-062cf033; Update()'s state-0 case null-checks it as an "already
    // resolved" gate, and state 3 forwards it to IObjAffect's slot 0x64. AttachDirective()
    // also seeds it from FUN_062e74a0(NavHelperPtr24 + 0x34, Directive + 0x10).
    IObjInspect const* LanePtr38;

    // +0x3c / +0x48. RENAMED 2026-09-17 from PosVector3c/DirVector48. These are
    // get_lane_start's OutStart and OutEnd out-parameters -- the lane's two endpoints, not a
    // position/direction pair. Order settled from the raw push order at .text:062ceffe-062cf005:
    // &(+0x48) is pushed FIRST and &(+0x3c) SECOND, so +0x3c is argument 3 (OutStart) and
    // +0x48 is argument 4 (OutEnd). (The old comment had the pair the other way round, reading
    // "LEA'd first" as "logically first"; on a right-to-left stdcall push, it is the opposite.)
    Vector LaneStart3c;
    Vector LaneEnd48;
    unsigned char Flags54;      // +0x54. SetContext()/RequestExitTradeLane()/
                                 // UpdateMode() all read or clear this bit; AttachDirective()
                                 // sets it.
    unsigned char Flag55;       // +0x55. Cleared alongside Flags54/Flag56 by
                                 // SetContext()/RequestExitTradeLane();
                                 // read/cleared by OnResult().
    unsigned char Flag56;       // +0x56. Same clearing group as Flag55; OnResult() sets it
                                 // to 1 on one path.
    unsigned char Pad57;        // +0x57, alignment only.
    float Field58;              // +0x58. Set by AttachDirective() from the directive
                                 // struct's own +0x14 field. Ends the object at +0x5c.
                                 // A float: it is only ever passed on as the fourth
                                 // argument of IObjAffect::go_tradelane, which Server's
                                 // Ship body (.text:06d028c0) forwards to
                                 // CShip::go_tradelane's `float Speed`.
};

static_assert(offsetof(InstantTradeLaneBehavior, State30) == 0x30, "InstantTradeLaneBehavior::State30 offset");
static_assert(offsetof(InstantTradeLaneBehavior, SubObjPtr34) == 0x34, "InstantTradeLaneBehavior::SubObjPtr34 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LanePtr38) == 0x38, "InstantTradeLaneBehavior::LanePtr38 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneStart3c) == 0x3c, "InstantTradeLaneBehavior::LaneStart3c offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneEnd48) == 0x48, "InstantTradeLaneBehavior::LaneEnd48 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flags54) == 0x54, "InstantTradeLaneBehavior::Flags54 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag55) == 0x55, "InstantTradeLaneBehavior::Flag55 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag56) == 0x56, "InstantTradeLaneBehavior::Flag56 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Field58) == 0x58, "InstantTradeLaneBehavior::Field58 offset");
static_assert(sizeof(InstantTradeLaneBehavior) == 0x5c, "InstantTradeLaneBehavior size -- CONFIRMED via operator_new(0x5c) in the factory (item 1514), not just the largest field referenced");
