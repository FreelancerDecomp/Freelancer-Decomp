// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_iface.h"     // IObjAffect::RequestCB
#include "common_pub.h"       // pub::AI::DirectiveDockOp, pub::AI::DirectiveGotoOp
#include "common_aisteer.h"   // ICRSplineSegment
#include "common_goods.h"     // BaseWatcher
#include "common_csys.h"      // Matrix (LookMatrixYup output, see Orient3d8 below)
#include "common_idea.h"      // MoveIdea, FaceIdea -- real classes behind SubObjectA/B (see below)

#include <cstddef>

// ============================================================================
// DockBehavior -- AI docking-maneuver state machine (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes, NOT from Ghidra's .c decompilation -- see "ground truth precedence"
// in this marathon's methodology):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a8c4c (offset field = 0, i.e. the
//   primary vtable installs at object offset 0)
//     -> pTypeDescriptor  @ .data:0x063ebe04, name ".?AVDockBehavior@@"
//     -> pClassHierarchyDescriptor @ .rdata:0x063a8cb8, numBaseClasses = 3,
//        BaseClassArray @ 0x063a8ca8 = [ DockBehavior(self, numContained=2),
//                                        Behavior(numContained=0, mdisp=0),
//                                        IObjAffect::RequestCB(numContained=0,
//                                                               mdisp=0x2c) ]
//
// The array holds TWO SEPARATE, UNNESTED base-class entries after self (both
// with numContainedBases=0, i.e. two independent root bases, not a chain) --
// this is a genuine MULTIPLE-INHERITANCE layout, not the single-chain
// Behavior -> BaseFollowerBehavior -> DockBehavior the task brief guessed.
// DockBehavior does NOT derive from BaseFollowerBehavior at all.
//
// A SECOND RTTICompleteObjectLocator exists at .rdata:0x063a8cc8 (signature=0,
// offset field = 0x2C, same pTypeDescriptor/pClassHierarchyDescriptor as the
// primary COL) -- this is the secondary-vtable COL required by the MSVC ABI
// for the second polymorphic base (IObjAffect::RequestCB) in a
// multiple-inheritance layout, confirming mdisp=0x2C independently of the
// RTTIBaseClassDescriptor above.
//
// Ground truth for the mdisp=0x2C secondary base is further confirmed by
// DockBehavior's own constructor (Common.dll.txt 187332-187388,
// FUN_062c2e60): `MOV dword ptr [ESI+0x2c], PTR_LAB_0639f498` installs the
// secondary (RequestCB) vtable pointer at exactly object+0x2C, right after
// `MOV dword ptr [ESI], PTR_FUN_0639f4a4` installs the primary vtable at
// object+0.
//
// The exact same Behavior + IObjAffect::RequestCB(mdisp=0x2C) pattern is
// independently confirmed on a SECOND, unrelated class in this module,
// InstantTradeLaneBehavior (RTTIBaseClassDescriptor array @ 0x063a9144,
// entries @ 0x063a9164/063a9180/063a9184 -- Behavior mdisp=0, RequestCB
// mdisp=0x2C) -- i.e. this is a recurring, deliberate retail pattern ("AI
// behaviors that need to receive an affect-request completion callback"),
// not a one-off.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062c2e60 (Common.dll.txt 187332-187388). This address was
// ALREADY tracked (decomp-tracker item 1261) under the "TractorArm dispatch
// table" investigation before this pass connected it to the DockBehavior RTTI
// name -- item 1261's ported_name "TractorArm::Dock (goto-directive mode)"
// and this class are the SAME constructor/class; the dispatch-table row named
// it "Dock", RTTI names the class DockBehavior, both refer to one retail type.
// Calls the shared TractorArm-family base-init helper FUN_062d2020 (item 1529,
// "TractorArm::ModeBase", already a REAL field-initialising base ctor -- it writes +0x04..+0x29 (see Behavior in common_behavior.h); the long-recorded "no-op stub" reading was wrong) before constructing DockBehavior's own sub-objects.
//
// ----------------------------------------------------------------------------
// SETTLED AND ACTED ON 2026-09-17 (claude:common-dockmove). READ THIS BEFORE THE
// TWO HISTORICAL BLOCKS BELOW, which are kept for their evidence but whose
// conclusions are superseded.
//
// FUN_062c4900 and FUN_062c3d10 are DockBehavior's own non-virtual members. They
// are NOT shared with FormationBehavior, there is no mixin and no template, and
// the compiler did not fold anything. What actually happened is that an early
// pass, working outward from a FormationBehavior hypothesis, modelled these two
// bodies on that class, and every DockBehavior field they touch then got
// re-derived a second time on FormationBehavior at the same absolute offsets.
// The "striking cross-class layout match" the blocks below celebrate was two
// copies of one class's layout, read off the same two functions.
//
// The decisive evidence is the object sizes, from the two adjacent factories:
//     .text:062d1a07  PUSH 0x470  ; then .text:062d1a2c CALL FUN_062c2e60  == DockBehavior
//     .text:062d1b67  PUSH 0x2b8  ; then .text:062d1b8c CALL FUN_062cbfe0  == FormationBehavior
// FormationBehavior's whole object is 0x2b8 bytes. These two bodies write as far
// as +0x46c. They cannot be its methods. They are this class's, and both now live
// in common_dockbehavior.cpp: FUN_062c4900 as UpdateApproachTail() (wired into
// Update()'s tail the way .text:062c3cea wires it) and FUN_062c3d10 as the full
// ten-case EnterState(). The whole >= 0x2b8 field block came with them.
//
// ----------------------------------------------------------------------------
// HISTORICAL (2026-09-11) -- the observation was right, the "shared mixin"
// conclusion at the end of it is superseded by the block above:
//
// DockBehavior's own vtable slot 7 (Update, LAB_062c3b70, item 1278) tail-
// calls `FUN_062c4900(this)` with `this` UNCHANGED (raw disasm: `MOV ESI,ECX`
// at function entry, `MOV ECX,ESI` immediately before `CALL FUN_062c4900` at
// the end -- verified byte-for-byte, no adjustment). DockBehavior's
// IObjAffect::RequestCB callback slot (LAB_062c53f0, item 1288) also calls
// `FUN_062c3d10(outer_this, state)` where outer_this = ESI-0x2C, i.e. the
// SAME DockBehavior instance (the -0x2C undoes exactly the +0x2C `this`
// adjustment the secondary-vtable thunk applied on entry). Field offsets that
// LAB_062c53f0 touches on that pointer -- +0x2C+0x2F8=+0x324 (matches
// DockBehavior's own known +0x324 pointer field), +0x2C+0x300=+0x32C tested
// against mask 0x840 (matches FormationBehavior's documented StateFlags@0x32C
// bit meanings 0x40/0x80/0x800 EXACTLY), +0x2C+0x3DC=+0x408 set to 16.0f
// (matches FormationBehavior's Field0x408@0x408) -- all read/write DockBehavior
// itself, not a nested sub-object.
//
// [SUPERSEDED CONCLUSION, kept so the reasoning is auditable: this block used to
// end "retail's FormationBehavior and DockBehavior were built from a shared C++
// template or copy-pasted mixin ... and the compiler emitted ONE shared
// implementation". There is no sharing. FormationBehavior's object is 0x2b8
// bytes and cannot hold a +0x320 state field at all -- the matching offsets were
// two models of THIS class. See the 2026-09-17 block at the top.]
//
// ----------------------------------------------------------------------------
// FOLLOW-UP (this pass, 2026-09-11): THREE MORE independent DockBehavior vtable
// slots -- item 1276 (LAB_062c3720, slot 3), item 1289 (LAB_062c54b0, slot 22),
// and (indirectly, via the shared field layout it exercises) item 1277
// (LAB_062c3910, slot 4) -- were read from the raw disassembly this pass and
// ALSO directly `CALL FUN_062c3d10` with `this` unchanged (5 confirmed call
// sites total across the class now, not 2). Reading these bodies also turned up
// many MORE offsets that read/write byte-for-byte identically to
// FormationBehavior's own documented fields, well beyond the original 3:
// DockBehavior's ThreatSourcePtr14/Field20/Field24 (+0x14/+0x20/+0x24) are
// populated by the exact same shared initializer FUN_062d2090 (see item 1271
// below) that other Behavior-family siblings' own +0x14/+0x20/+0x24-shaped
// fields go through elsewhere in this module (15 call sites total, spanning
// the whole 0x062cXXXX Behavior neighborhood) -- i.e. these are NOT
// DockBehavior-private "Field20/Field24" ints as this pass's own predecessor
// assumed; they are genuinely BaseFollowerBehavior::BehaviorMgrPtr/NavHelper
// -shaped pointers (FUN_062d2090 literally writes `P1`/`P2`/`P3` raw pointer
// arguments into them). Also newly confirmed identical to FormationBehavior:
// +0x2dc/+0x2f0 (Goto-watcher/NavPilot pointer pair, previously mis-modeled as
// part of one opaque "SubObjectD" -- see GotoDirectiveHandle below), +0x330
// (int, -1 sentinel), +0x334 (ApproachClearedFlag-shaped byte), +0x344
// (CombatFlag-shaped byte), +0x398/+0x399/+0x39a (three more flag bytes
// alongside the already-known +0x39b), +0x440 (SubStateValue-shaped int).
// IBehaviorManager::lock_maneuvers is even called BY NAME (not just by vtable
// slot number) from item 1277 on the +0x20 field, which is about as direct a
// confirmation as this kind of reverse engineering gets that DockBehavior's
// +0x20 field really is an IBehaviorManager*.
//
// The 2026-09-11 pass read that depth of "sharing" as a reason to be cautious,
// and reproduced only the pre-switch release block plus cases 0xc/0xd of
// FUN_062c3d10 here, leaving the full ten-case switch on FormationBehavior. It
// also flagged a "size mismatch" -- that FormationBehavior::EnterState() writes
// +0x46c..+0x470, four bytes past DockBehavior's then-confirmed 0x46c span.
//
// RESOLVED 2026-09-17 (claude:common-dockmove): the size mismatch was the clue,
// not the obstacle. DockBehavior really is 0x470 bytes (.text:062d1a07
// `PUSH 0x470`), so the +0x46c write lands exactly on its last field, with
// nothing left over -- which is what you expect when a body writes its OWN
// class's last member, and is not what you get by coincidence. The two bodies
// are this class's alone, the full ten-case EnterState() and the whole of
// FUN_062c4900 (as UpdateApproachTail()) are ported here, and there is no second
// copy anywhere: FormationBehavior has neither method any more.
// ============================================================================

// Intermediate base holding DockBehavior's own +0x04..+0x2c fields. MSVC lays
// out multiple-inheritance bases in declaration order, THEN the derived
// class's own members -- it does NOT interleave a derived class's own fields
// between two of its own direct bases. To get retail's real layout (own data
// at +0x04..+0x2c, THEN the IObjAffect::RequestCB secondary vtable pointer at
// +0x2c, THEN the sub-objects starting at +0x30) this "own data" block has to
// live on its own intermediate base, exactly like BaseFollowerBehavior does
// for FormationBehavior above -- DockBehavior then multiply-inherits from
// THIS class (primary) and RequestCB (secondary), with its own members
// (SubObjectA onward) naturally landing after both, at +0x30.
class DockBehaviorFields : public Behavior
{
public:
    explicit DockBehaviorFields(int InTypeId);

    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.       // +0x08. Set by item 1271 (LAB_062c31c0, own vtable slot 1) from
                                 // `ThreatSourcePtr14->vtbl[0x20]()`'s return value, via the shared
                                 // Behavior-family initializer FUN_062d2090 (15 xrefs across this
                                 // module's whole 0x062cXXXX Behavior neighborhood -- see the
                                 // class-level comment above). Not otherwise read by anything this
                                 // pass ported.
    int ModeSelector0xc;        // +0x0c. Written by SetModeFromFlags() (values 1/2/3); not
                                 // otherwise read by anything this pass ported.
    int UpdateResult10;         // +0x10. Update()'s own tri-state result/early-out code (1/2/3),
                                 // analogous in spirit (not offset) to FormationBehavior's
                                 // BaseFollowerBehavior::UpdateResult.
    void* ThreatSourcePtr14;    // +0x14. Update() null-checks then dereferences this as a
                                 // vtable-having pointer (slots 0x40/0x6c called on *its*
                                 // vtable, not DockBehavior's own). CONFIRMED (this pass) to be
                                 // the same structural field as FormationBehavior/
                                 // BaseFollowerBehavior::OwnerContext@+0x14 -- both are populated by
                                 // the identical shared initializer FUN_062d2090 at the identical
                                 // offset (see item 1271 / the class comment above).
    unsigned char Opaque18[0x20 - 0x18]; // +0x18, never touched by anything read this pass
    void* Field20;                // +0x20. CONFIRMED (this pass, item 1277/LAB_062c3910) to be an
                                   // `IBehaviorManager*` -- that function calls
                                   // `IBehaviorManager::lock_maneuvers` on it BY LINKED SYMBOL NAME,
                                   // matching FormationBehavior::BaseFollowerBehavior::BehaviorMgrPtr
                                   // at the identical offset exactly. Previously typed as a plain
                                   // `int`; retyped to `void*` this pass (still stored/read as a raw
                                   // pointer by every confirmed caller; not yet cast to
                                   // `IBehaviorManager*` directly since that header isn't included
                                   // here).
    void* Field24;                 // +0x24. CONFIRMED (this pass) structurally identical to
                                    // FormationBehavior/BaseFollowerBehavior::NavHelper@+0x24 (same
                                    // shared FUN_062d2090 initializer, same `+0x14fc`-offset nested
                                    // vtable-having sub-object access pattern seen in items
                                    // 1271/1277/1289). Update() also passes it as an argument to
                                    // BOTH FUN_062c2af0 and FUN_062c2d90 (items 1257/1259).
                                    // Previously typed as a plain `int`; retyped to `void*` this pass.
    unsigned char Flags28;       // +0x28. Update() tests bit 0x1 (early-exit to state 2);
                                  // SetModeFromFlags() tests bit 0x2.
    unsigned char Flag29;        // +0x29. SetModeFromFlags() tests it (nonzero => mode 3).
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only
};
static_assert(offsetof(DockBehaviorFields, CachedHandle08) == 0x08, "DockBehaviorFields::CachedHandle08 offset");
static_assert(offsetof(DockBehaviorFields, ModeSelector0xc) == 0x0c, "DockBehaviorFields::ModeSelector0xc offset");
static_assert(offsetof(DockBehaviorFields, UpdateResult10) == 0x10, "DockBehaviorFields::UpdateResult10 offset");
static_assert(offsetof(DockBehaviorFields, ThreatSourcePtr14) == 0x14, "DockBehaviorFields::ThreatSourcePtr14 offset");
static_assert(offsetof(DockBehaviorFields, Field20) == 0x20, "DockBehaviorFields::Field20 offset");
static_assert(offsetof(DockBehaviorFields, Field24) == 0x24, "DockBehaviorFields::Field24 offset");
static_assert(offsetof(DockBehaviorFields, Flags28) == 0x28, "DockBehaviorFields::Flags28 offset");
static_assert(offsetof(DockBehaviorFields, Flag29) == 0x29, "DockBehaviorFields::Flag29 offset");
static_assert(sizeof(DockBehaviorFields) == 0x2c, "DockBehaviorFields size (own data ends right before the RequestCB secondary vtable ptr at +0x2c)");

class DockBehavior : public DockBehaviorFields, public IObjAffect::RequestCB
{
public:
    // Retail's ctor argument is the behavior type id (7, "Dock"); it is forwarded to DockBehaviorFields.
    // See Behavior::GetTypeId() in common_behavior.h.
    explicit DockBehavior(int InTypeId);

    // ------------------------------------------------------------------
    // Primary vtable (.rdata:0x0639f4a4, 27 slots, 0x0639f4a4-0x0639f50c
    // inclusive -- byte distance to the next vtable head at 0x0639f564
    // confirms this bound). Slot -> retail address -> decomp-tracker item.
    // Several slots are confirmed MSVC ICF (identical code folding) artifacts
    // shared with unrelated classes (CEquip::ConnectServerEquip,
    // Exclusion::AsteroidExclusionZone::.., IObjInspectImpl::cobject,
    // MetaList<GoodDesc>::operator[], Archetype::AttachedEquipment::i..,
    // CEShield::GetEquipDesc) -- see item 1257's blocker_reason for the full
    // ICF proof; they carry no DockBehavior-specific logic despite the name.
    //
    //   0  (0x00) FUN_062c3020        item 1267  scalar-deleting-dtor thunk (MSVC boilerplate,
    //                                            real dtor is FUN_062c30d0 below)
    //   1  (0x04) LAB_062c31c0        item 1271  ported (outer logic): SetContext()
    //   2  (0x08) CEquip::ConnectServerEquip      ICF fold, not DockBehavior-specific
    //   3  (0x0c) LAB_062c3720        item 1276  ported (outer logic): RecomputeApproach()
    //   4  (0x10) LAB_062c3910        item 1277  ported (outer logic): Disengage()
    //   5  (0x14) LAB_062c35c0        item 1274  ported for real: SetModeFromFlags()
    //   6  (0x18) LAB_062c35f0        item 1275  ported for real: CheckClearanceProgress()
    //   7  (0x1c) LAB_062c3b70        item 1278  ported (outer logic): Update()
    //   8  (0x20) Exclusion::AsteroidExclusionZone..  ICF fold
    //   9  (0x24) IObjInspectImpl::cobject          ICF fold
    //  10  (0x28) FUN_062c3370        item 1272  ported 2026-10-08: Slot10() (apply DirectiveDockOp)
    //  11  (0x2c) LAB_062c3000        item 1266  ported for real: IsClearanceState()
    //  12  (0x30) LAB_062c5540        item 1290  ported (outer logic): SubmitGotoDirective()
    //  13  (0x34) LAB_062c5650        item 1291  documented, not ported this pass
    //  14  (0x38) LAB_062c5670        item 1292  documented, not ported this pass
    //  15  (0x3c) MetaList<GoodDesc>::operator[]    ICF fold
    //  16  (0x40) LAB_062c5690        item 1293  documented, not ported this pass
    //  17  (0x44) LAB_062c56b0        item 1294  documented, not ported this pass
    //  18  (0x48) MetaList<GoodDesc>::operator[]    ICF fold (same target as slot 15)
    //  19  (0x4c) Archetype::AttachedEquipment::i.. ICF fold
    //  20  (0x50) LAB_062c2f40        item 1262  trivial (2 insns), skipped
    //  21  (0x54) FUN_062c2f60        item 1264  ported 2026-10-08: Slot21() (drop watchers on id)
    //  22  (0x58) LAB_062c54b0        item 1289  ported (outer logic): OnWatchedObjectDestroyed()
    //  23  (0x5c) LAB_062c2f50        item 1263  trivial (2 insns), skipped
    //  24  (0x60) LAB_062c2fe0        item 1265  ported for real: GetWatchedRawBlock()
    //  25  (0x64) CEShield::GetEquipDesc            ICF fold
    //  26  (0x68) LAB_062c2fe0        item 1265  same target as slot 24 (shared slot)
    //
    // Secondary vtable (.rdata:0x0639f498, IObjAffect::RequestCB base,
    // mdisp=0x2C): exactly 1 slot.
    //   0  (0x00) LAB_062c53f0        item 1288  ported (outer logic): OnResult() override
    // ------------------------------------------------------------------

    // FUN_062c30d0 (item 1270): member teardown in reverse declaration order (six
    // BaseWatcher destructors) and FUN_062d2080 (~Behavior) -- all compiler-generated.
    ~DockBehavior() override;

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 188111-188123
    // (LAB_062c35c0). Fully self-contained 3-state mode selector:
    //   byte flag1 = Flags28 & 0x2; byte flag2 = Flags29;
    //   int mode = flag1 ? 1 : 2; if (flag2 != 0) mode = 3;
    //   ModeSelector0xc = mode; return mode;   (RET 4, returns EAX -- returned void until
    //   2026-10-08)
    int SetModeFromFlags();

    // Own vtable slot 11 (offset 0x2c). Common.dll.txt 187523-187537
    // (LAB_062c3000). `int mode = ModeState320; return mode!=6 && mode!=7 &&
    // mode!=8 && mode!=10;` -- reads the +0x320 state field (NOT the +0xc
    // ModeSelector SetModeFromFlags writes; those are two distinct fields,
    // confirmed by reading both raw disassemblies directly -- easy to
    // conflate since both are informally "mode"-shaped ints).
    bool IsClearanceState() const;

    // Own vtable slot 24 AND 26 (offsets 0x60/0x68, same target function).
    // Common.dll.txt 187499-187507 (LAB_062c2fe0). `EAX = Watcher324.Watched;
    // return EAX ? (EAX-8) : 0;` -- classic "payload pointer back to owning
    // 8-byte-header allocation block" adjustment.
    void* GetWatchedRawBlock() const;

    // Own vtable slot 7 (offset 0x1c), Update/dispatch entry point, RET 0.
    // Common.dll.txt 188633-188787 (LAB_062c3b70), 141 instructions: early outs 2/3,
    // the player-owner docking-target health latch (Flag39b, IObjAffect slot_6c),
    // release of both Ideas while the target moves faster than sqrt(0.3), else the
    // +0x450 proximity watcher (FUN_062c2af0 / FUN_062c2d90) and UpdateApproachTail.
    int Update();

    // FUN_062c4900, decomp-tracker item 1285. Common.dll.txt 189892-190605 /
    // Common.dll.c:101167, 659 instructions. NOT a vtable slot: the address appears
    // in no data section anywhere in the module (`grep -c 00492c06 Common.dll.txt`
    // == 0), and it has exactly ONE caller in the whole DLL, .text:062c3cea inside
    // Update() above, which passes its own unadjusted `this`. So it is a plain
    // non-virtual member -- the tail half of Update(), split out by the compiler or
    // by the original source.
    //
    // MOVED HERE 2026-09-17 (claude:common-dockmove) from common_behavior.cpp, where
    // it had been hosted on FormationBehavior as `FormationBehavior::Update()` since
    // the class was first rebuilt. That was wrong four ways over (RTTI, vtable
    // membership, call sites, and object size -- FormationBehavior is 0x2b8 bytes and
    // this body writes as far as +0x46c); see common_behavior.h's FormationBehavior
    // class comment for the full trail. The body itself is unchanged by the move
    // apart from field renames: it was already re-derived from the raw listing on
    // 2026-09-17 by claude:common-formation and already addressed DockBehavior's
    // sub-object offsets.
    //
    // Returns the same tri-state retail leaves in UpdateResult10 (0=done/idle,
    // 1=continue, 2=give-up states).
    int UpdateApproachTail();

    // Retail's per-tick entry point is vtable slot 7 (+0x1c), not a non-virtual member:
    // IBehaviorManager::UpdateBehaviorTick writes the accumulated tick delta and then
    // dispatches through it -- Common.dll.txt .text:062d335d-062d336a,
    //     FLD float ptr [ESI + 0xd4]   /   FSTP float ptr [EDI + 0x1c]
    //     CALL dword ptr [EAX + 0x1c]  ; vtable +0x1c IS Slot7
    // EDI there is a bare Behavior* out of the dispatch table, so Update() below was
    // unreachable through a Behavior* until this override existed. Base Behavior::Slot7 is
    // _purecall in retail; see common_behavior.h. Same wiring as TrailBehavior.
    int Slot7() override { return Update(); }

    // The other slots retail's DockBehavior vftable (.rdata:0639f4a4) overrides, wired
    // 2026-10-08 (claude-code:cmn-ai2) -- until then the methods below were plain members and
    // the manager's Behavior* dispatch reached the base bodies. Slots 10 (FUN_062c3370) and
    // 21 (FUN_062c2f60) were ported 2026-10-08 (claude-code:cmn-ai5).
    bool Slot10(void* DirectiveOp) override;                              // 10 FUN_062c3370 (RET 4)
    void Slot21(void* Arg) override;                                      // 21 FUN_062c2f60 (RET 4)
    void Slot3() override { RecomputeApproach(); }                       // 3  LAB_062c3720
    void Slot4() override { Disengage(); }                               // 4  LAB_062c3910
    int Slot5(float) override { return SetModeFromFlags(); }             // 5  LAB_062c35c0 (RET 4)
    EvaluateResult evaluate(unsigned int) override                       // 6  LAB_062c35f0 (RET 4)
    { return static_cast<EvaluateResult>(CheckClearanceProgress()); }
    bool Slot11() override { return IsClearanceState(); }                // 11 LAB_062c3000
    void set_direction(Vector const& Dir) override { SubmitGotoDirective(Dir); } // 12 LAB_062c5540
    // 13 LAB_062c5650 / 14 LAB_062c5670: `ECX = [this+0x2f0]; if (ECX) JMP [[ECX]+0x34/+0x38]`
    // with the argument passed through -- forwarded to the Goto pilot when there is one.
    void set_throttle(float Throttle) override;
    void set_cruise(bool bOn) override;
    // 16 LAB_062c5690 / 17 LAB_062c56b0: `if ([this+0x2f0]) JMP FUN_062d2110 / FUN_062d21b0`
    // with this unchanged -- Behavior's own bodies, only while a pilot is attached.
    void set_afterburner(bool bOn) override;
    void slide_strafe_burst(StrafeDir Dir) override;
    void Slot22(void* Message) override { OnWatchedObjectDestroyed(Message); } // 22 LAB_062c54b0

    // FUN_062c3590 (item 1273), `RET 4`, sole caller FleeBehavior::Perform (.text:062c7403):
    // `REP MOVSD` of 0xb dwords from this+0x2f4 (DockOp) into *Out, then
    // `(Flags28 >> 1) & 1`.
    bool CopyDockOp(pub::AI::DirectiveDockOp* Out) const;
    // 24 / 26: both LAB_062c2fe0 -- Watcher324's object (target - 8) or null.
    IObjRW const* get_target() override { return static_cast<IObjRW const*>(GetWatchedRawBlock()); }
    void* Slot26() override { return GetWatchedRawBlock(); }

    // IObjAffect::RequestCB override -- own single secondary-vtable slot.
    // Common.dll.txt 190855-190928 (LAB_062c53f0), 66 instructions, fully
    // translated below except the FUN_062c3d10/FUN_062c50e0 delegations
    // (same shared-tail caveat as Update() above).
    bool OnResult(int Param1, int Param2, int Param3) override;

    // ------------------------------------------------------------------
    // Newly ported this pass (see the class-level comment's 2026-09-11
    // follow-up for the multi-site EnterState/shared-layout evidence these
    // rely on).
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 187726-187871
    // (LAB_062c31c0), 141 instructions. First calls the shared Behavior-family
    // context initializer FUN_062d2090(this, P1, P2, P3) (see
    // DockBehaviorFields::ThreatSourcePtr14/Field20/Field24 above); if it
    // returns false (P1/P3 null -- NOT P1/P2; confirmed directly from
    // FUN_062d2090's own raw disassembly), returns immediately. Otherwise resets
    // AbortFlag398/Flag39b, calls SubObjectA/B/C's own vtable slot 0, clears
    // SubStateValue440/Field330/Flags28 bit 0x2 [CORRECTED 2026-09-11
    // d285:common-port: re-reading Common.dll.txt 187783-187789 byte-for-byte
    // shows this call site actually zeroes the 3 unconfirmed dwords at
    // +0x444/+0x448/+0x44c (Opaque444), NOT SubStateValue440(+0x440)/
    // Field330(+0x330) -- no instruction anywhere in this function touches
    // either of those two fields; the prior pass's claim was wrong, fixed in
    // the .cpp], and (if a "hpmount" hardpoint lookup on ThreatSourcePtr14
    // fails) recomputes OrientMatrix3d8 from NavHelper's nested +0x14fc/+0x84
    // sub-object via FUN_06391cf0/its own vtbl+0x38 -- that recompute branch
    // stays FULLPORT-DEFERRED (deeply coupled to NavHelper's still-
    // unidentified nested class).
    //
    // SubObjectA/B's own vtable slot 0 calls are now WIRED FOR REAL (2026-09-11
    // d285:common-port), now that they're identified as MoveIdea/FaceIdea:
    // re-read from the raw bytes, both calls pass the identical 3 arguments
    // `(ThreatSourcePtr14, Field24, <this function's own raw Param2>)` -- NOT
    // `Field20` (which also holds the same value, since FUN_062d2090 sets
    // Field20=Param2, but retail's own EDI register here is never reloaded
    // from +0x20, it's the still-live raw incoming Param2 captured before the
    // FUN_062d2090 call). SubObjectC's own slot-0 call is wired for real
    // too as of 2026-09-17 (claude:abi-pass50), with the same three
    // arguments, now that it is a SplineMoveIdea. Two calls in that same run
    // are still omitted -- see the .cpp's TODO(FUN_062c31c0).
    // Returns the FUN_062d2090 gate result: retail saves AL in [ESP+0x30] after the gate and
    // returns it on both exits (.text:062c333e / .text:062c3351), and the failure exit
    // (.text:062c3367) leaves AL == 0. Override of Behavior slot 1 (SetContext).
    bool SetContext(void* Param1, void* Param2, void* Param3) override;

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 188246-188420
    // (LAB_062c3720), 163 instructions. Early-outs to `EnterState(0xd)` if
    // Watcher324's watched block is gone; otherwise resets several per-tick
    // fields (Field43c=-1, CombatFlag344=0, Field33c via FUN_062e5af0/
    // FUN_062e5ab0*3.0, SetCruiseLock(1,3) via FUN_062d21d0), then branches on
    // StateFlags32c's sign bit: if clear, calls ThreatSourcePtr14's vtbl+0x138
    // "get transform" (writes Opaque388/Facing38c/an intercept-point out-param
    // -- see the field comments below for the 2026-09-11 width correction to
    // this span), builds OrientMatrix3d8 via LookMatrixYup, and updates
    // SubObjectA via FUN_062d4e50 with the watched block's vtbl+0x20 result;
    // if set and bit 0x40 set, tail-calls FUN_062c50e0 (FULLPORT-DEFERRED).
    // Finally checks ThreatSourcePtr14's vtbl+0xbc "is threatening" bool and a
    // distance-squared-vs-DAT_063fc518 test on the watched block's position to
    // decide `EnterState(0xd)` vs `EnterState(1)`, then a FUN_062d5a80 call on
    // SubObjectA (FULLPORT-DEFERRED, same "unidentified sub-object vtable" gap
    // as the vtbl+0 calls above) and a fixed dword write to a scratch field
    // this pass doesn't separately type out (retail: this+0x46c, PI-shaped
    // constant -- outside this struct's currently-confirmed 0x46c size, so not
    // reproduced).
    void RecomputeApproach();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 188421-188627
    // (LAB_062c3910), 187 instructions. DockBehavior's own "give up the
    // maneuver" routine.
    //
    // CORRECTED 2026-09-11 (d285:common-port), full re-read of the raw bytes
    // top to bottom while wiring the SubObjectA/B Commit() calls below --
    // the PRIOR pass's port had THREE structural bugs, all fixed in the .cpp:
    //   1. Retail gates the ENTIRE lock_maneuvers/NavPilot-release block
    //      behind `if (ModeState320 != 0)`; the prior port ran it
    //      unconditionally. When ModeState320==0, retail instead takes a
    //      completely separate short tail (LAB_062c3afc) that the prior port
    //      didn't reproduce AT ALL (an early-out on ApproachClearedFlag334==0,
    //      else resets SubStateValue440/Field330/ApproachClearedFlag334 and a
    //      still-unidentified "get watched position, feed ThreatSourcePtr14's
    //      own +4 nested vtbl+0x60" call). Now reproduced (position/vtbl part
    //      still FULLPORT-DEFERRED, nested class unidentified).
    //   2. The prior port had the StateFlags32c-sign-bit branches INVERTED:
    //      it treated the sign-bit-CLEAR case as "SetAlertLevel(SubObjectA,2)
    //      then return early" and the sign-bit-SET case as "do the resets".
    //      Retail is the opposite AND neither branch returns early -- both
    //      fall into one shared tail (LAB_062c3a9e). Sign-bit SET (`bClearance
    //      Requested`): `FUN_062d88b0(&SubObjectA, 2)` (hardcoded level),
    //      then -- only if Flags28 bit 0 is clear -- the SubStateValue440/
    //      Field330/ApproachClearedFlag334 resets plus the AbortFlag398/
    //      Flag399-gated CShip::request_exit_tradelane call and the
    //      independent Flag39a-gated "position" call. Sign-bit CLEAR: level
    //      is CONDITIONAL (2 only if ThreatSourcePtr14's nested +0x14fc+0x84
    //      sub-object's own vtbl+0xbc call is true AND ModeState320==1, else
    //      0) -- that nested condition is still FULLPORT-DEFERRED (unidentified
    //      class), so this port conservatively always passes 0 on this branch
    //      rather than guessing the condition's outcome.
    //   3. The shared tail (LAB_062c3a9e) -- reached from BOTH branches above,
    //      not just one -- clears Flag39a/AbortFlag398, calls (in this exact
    //      order) `GotoDirective->vtbl[0x10]()` (FULLPORT-DEFERRED, GotoDirective's
    //      real identity is still an open question -- see GotoDirectiveHandle's
    //      own header comment), then `SubObjectB.Commit()`, then
    //      `SubObjectA.Commit()` (own vtable slot 4, retail `vtbl+0x10` --
    //      WIRED FOR REAL this pass now that MoveIdea/FaceIdea are identified),
    //      `SetCruiseLock(1,3)`, `FUN_062e9a60(Field24+0x12c, 1)` -- WIRED FOR
    //      REAL 2026-09-17, now Field24->GunGroups.SetShieldActive(1); it
    //      had been FULLPORT-DEFERRED under the guessed name "ResumeUserInput"
    //      for a function that was fully ported on 2026-09-11 -- and a
    //      still-FULLPORT-DEFERRED `FUN_062d20f0(this)`, and finally
    //      `ModeState320 = 0`. The prior
    //      port's incorrect `GotoDirective.GotoWatcherPtr = nullptr;` write
    //      after the (deferred) vtbl+0x10 call is REMOVED -- no instruction in
    //      this function's raw bytes zeroes that field; only NavPilotPtr gets
    //      zeroed (earlier, in the top-level release block).
    void Disengage();

    // Own vtable slot 6 (offset 0x18). Common.dll.txt 188138-188244
    // (LAB_062c35f0), 100 instructions. Retail returns a literal 1 or 2 (NOT
    // a bool -- both values are non-zero), so this is typed `int` to preserve
    // that. Returns 1 ("already clear"/early-out) if AbortFlag398 is set.
    // Otherwise calls ThreatSourcePtr14's vtbl+0x74 to get a "candidate"
    // object; if null, returns 2. Else calls the candidate's own vtbl+0x88 to
    // get a status dword and tests it against several bit patterns
    // (0x600de0/0x8c0/0x120, or a fallback vtbl+0x8c + FUN_062e7400(Field24+
    // 0x34, candidate) path) to decide whether to proceed to a
    // distance/facing check: candidate's vtbl+0x40 (float getter, compared to
    // DAT_06399408) then vtbl+0x4 (3-float getter, dotted against itself and
    // compared to DAT_0639f484) -- returns 1 if both checks pass, 2
    // otherwise. Everything past the AbortFlag398 check is FULLPORT-DEFERRED
    // (candidate's real class isn't identified, so it can't even be fetched
    // here) -- this port returns retail's own safe default (2, "not clear
    // yet") for every case past that point rather than guessing.
    int CheckClearanceProgress();

    // Own vtable slot 12 (offset 0x30). Common.dll.txt 191005-191094
    // (LAB_062c5540), 81 instructions. No-ops if NavPilotPtr (GotoDirective)
    // is null. Otherwise computes the squared magnitude of a caller-supplied
    // Vector*; if the FCOM against DAT_06399408 is unordered (JP), tail-calls
    // NavPilotPtr's own vtbl+0x30 with the raw parameter instead (a distinct,
    // not-separately-modeled dispatch path -- FULLPORT-DEFERRED). Otherwise
    // builds a `pub::AI::DirectiveGotoOp` (Field_50 from the same
    // DAT_063ebdb4 tunable FormationBehavior::EnterState(1) uses,
    // ArrivalRadius-shaped fields from ProxWatcher450.Unknown08/Unknown0c,
    // Destination from either an intercept-point Vector at +0x3cc -- not
    // separately typed out this pass -- when StateFlags32c bits 0xc0 are set,
    // or from the watched-block's own vtbl+0x20 "position" getter otherwise)
    // and submits it via `NavPilotPtr->vtbl[0x28](&Op)` -- the exact same
    // submit convention as FormationBehavior::EnterState(1)'s own
    // `Slot28(NavPilotPtr, &Op)` call.
    void SubmitGotoDirective(Vector const& ReferencePoint);

    // Own vtable slot 22 (offset 0x58). Common.dll.txt 190940-190996
    // (LAB_062c54b0), 51 instructions. A notification-message handler:
    // `param->TypeCode` (+0) selects between two "is this about MY watched
    // object" identity checks against `param->Payload` (+4) -- type 4 checks
    // a flag byte plus two vtbl+0x20 "get id" calls (ThreatSourcePtr14's and
    // Watcher324's watched-block's) against Payload+0xc/+0x10; type 3 checks
    // NavHelper's nested +0x14fc/+0x84 sub-object's own vtbl+0x20 result
    // against Payload+8. On a match, sets Flag399=1 and calls
    // `EnterState(0xc)` -- this class's THIRD confirmed direct call site into
    // the shared FUN_062c3d10 (see the class-level comment's 2026-09-11
    // follow-up). Any other TypeCode, or a non-matching identity check, is a
    // silent no-op (matching retail's fall-through-to-RET behavior).
    void OnWatchedObjectDestroyed(void* Message);

    // FUN_062c3d10, decomp-tracker item 1279. Common.dll.txt 188802-189470 /
    // Common.dll.c:100735. Like UpdateApproachTail(), not a vtable slot anywhere
    // (`grep -c 103d2c06 Common.dll.txt` == 0) -- a plain non-virtual member whose
    // non-self callers are all DockBehavior methods with an unadjusted `this`:
    // .text:062c375b and .text:062c38c4 (inside LAB_062c3720 == slot 3,
    // RecomputeApproach) and .text:062c5440..062c552e (inside LAB_062c53f0, the
    // RequestCB secondary-vtable body, OnResult), plus OnWatchedObjectDestroyed().
    //
    // NOW THE FULL TEN-CASE SWITCH, 2026-09-17 (claude:common-dockmove). It used to
    // reproduce only the pre-switch release block and cases 0xc/0xd, because the
    // full translation lived on FormationBehavior in common_behavior.cpp and the
    // fields the other cases write were (wrongly) declared over there. Both the body
    // and the fields are here now, so the "extend it to cases 1/2/4/6/7/8/9/0xb" TODO
    // that stood on this declaration is discharged. What remains deferred inside it
    // is the unported-callee set, not the control flow -- see the .cpp.
    void EnterState(int NewState);

private:
    // ---- Not-yet-ported dependencies of UpdateApproachTail()/EnterState(), called
    // ---- faithfully from the right state/branch but with a safe stand-in body.
    // ---- MOVED HERE 2026-09-17 with the two bodies (FULLPORT-DEFERRED). ----
    bool CheckCanProceed();                                  // FUN_062c4530, ported 2026-10-08
    // FUN_062c56d0 (263 insns): builds the final-approach spline (+0x348) and hands it to
    // SubObjectC. Was the empty static stub "EnterFinalApproachVisuals"; the name was a guess
    // and the body is pure geometry. Ported 2026-10-08.
    void BuildFinalApproachSpline();

    // FUN_062c47f0 (item 1284), .text:062c47f0-062c48f0, 76 instructions. PORTED
    // FOR REAL 2026-09-17; it was a `static void` empty body, and `static` was
    // itself the bug -- retail's very first instruction is `MOV ESI,ECX`, and the
    // body reads seven of this class's fields. A receiverless declaration cannot
    // express it, so the emptiness was structurally locked in.
    //
    // It is NOT "the timeout countdown" the old stub comment claimed. It computes
    // this tick's LINEAR and ANGULAR speed caps and pushes them into the three
    // Idea sub-objects. See the .cpp for the derivation.
    void AdvancePreamble();
    // The "refresh NavHelper's cached lead point unless it was already recomputed
    // this frame" idiom, verbatim from retail and used at three sites inside these
    // two bodies (Common.dll.txt 062c4a72, 062c4b0e and 062c3e97, all identical
    // byte-for-byte): read the flag at Field24+0x157a, and if it is clear, set the
    // byte at Field24+0x14fc+0x7e and dispatch slot 0x2c of the sub-object pointer
    // at Field24+0x14fc+0x84 with (Field24+0x14fc+0x18, Field24+0x14fc+0xc). Same
    // shape FormationBehavior::DisengageSelf() documents and decomp-tracker item
    // 1996 independently confirmed.
    void NavHelperRefreshLeadPoint();
    // EvaluateWatcher/ResetWatcher REMOVED 2026-09-17 (claude:w21-stubsweep).
    // They stood for the `CALL [<vtbl>+0x4]` and `CALL [<vtbl>+0x10]` retail makes
    // on SubObjectA/SubObjectB/SubObjectC, and the reason given for keeping them
    // untyped ("the three sub-objects are different classes and only the offsets
    // are shared") was the wrong conclusion: all three derive from BaseIdea, where
    // offset 0x4 is slot 1 == Update(float) and offset 0x10 is slot 4 == Commit().
    // Both are pure virtuals with real overrides on all three classes, so a direct
    // member call on the typed member dispatches exactly where retail dispatches.
    // .rdata:DAT_0639f518, the double 0x3fe3333333333333 == 0.6, the only multiplier
    // EnterState()'s case 9 applies to Field408 (.text:062c439f).
    static double const kEnterState9RadiusScale;   // DAT_0639f518, a double

    // ---- The five "shared sub-object setter" stubs are GONE ----------------
    // REMOVED 2026-09-17 (claude:w21-stubsweep). Their stated reason -- "whose
    // destination FIELD offset inside that sub-object is not recovered
    // (MoveIdea/FaceIdea/SplineMoveIdea live in common_idea.h)" -- was stale:
    // common_idea.h declares every one of those fields AND ports every one of
    // these setters. Five names covered eight distinct retail functions:
    //   SetOffsetVector   FUN_062d5840 -> MoveIdea::SetPosition
    //                     FUN_062d5a80 -> MoveIdea::Offset4c (3-dword store, +0x4c)
    //                     FUN_062d7560 -> FaceIdea::SetPosition
    //                     FUN_062d7590 -> FaceIdea::SetDirection
    //                     FUN_062d75c0 -> FaceIdea::SetBasis (9 dwords, a Matrix)
    //   SetMoveIdeaMode   FUN_062d5980 -> MoveIdea::SetMode
    //   SetMoveIdeaRadius .text:062a66b0 (bind_to_dc fold) -> MoveIdea::SetRange3c
    //   SetMoveIdeaAngle  .text:063275f0 (set_origin_h fold) -> MoveIdea::SetAngle38
    //   SetMoveIdeaEnabled FUN_062d5a70 -> MoveIdea::SetField59, NOT SetEnabled:
    //                     .text:062d5a70 writes byte +0x59, while SetEnabled's own
    //                     address .text:062d5a10 writes byte +0x58. The guessed
    //                     name had picked the wrong one of two adjacent bytes.
    // Likewise PlayApproachWarning -> FaceIdea::SetAngleWeight8c (item 1632) and
    // RequestBehaviorSwitch -> SplineMoveIdea::SetField158 (item 1654), both below.
    static float QueryHandleRange(void* HandleField, int Mode);            // FUN_062c2ad0
    // (RunCollisionProbe, an always-true stub for FUN_062e53e0, is gone 2026-10-08: its one
    // call site, Update case 9 at .text:062c4edd, now calls IsPointOutsideHeadingCone.)

    // Raw-offset reach for the two spans inside this class that retail addresses but
    // whose width it never proves -- Opaque3a8 (+0x3a8), Opaque40c's +0x40c and
    // +0x430. Retail only ever takes their address, so no typed member is declared;
    // see each field's own comment. Keeping the reach in one named helper makes the
    // remaining raw offsets greppable.
    void* RawAt(unsigned Offset) { return reinterpret_cast<unsigned char*>(this) + Offset; }
    // ---- Not-yet-identified sub-object constructors (FULLPORT-DEFERRED). ----
    // SubObjectA/B (FUN_062d4e60/FUN_062d6750) are IDENTIFIED as real
    // MoveIdea/FaceIdea (see the field declarations below) -- their real
    // constructors now run via the member-initializer list instead of a
    // deferred no-op, so no Construct* stand-in remains for them here.
    // SubObjectC is IDENTIFIED too as of 2026-09-17 (claude:abi-pass50): its
    // ctor FUN_062d9bf0 installs vtable .rdata:063a0568, whose RTTI COL at
    // [-4] names SplineMoveIdea (common_idea.h) -- so its Construct* stand-in
    // is gone as well and it constructs for real. Only GotoDirective's owning
    // class is still unidentified; it stays a partially-typed struct rather
    // than a guessed type, matching this marathon's placeholder-field policy.
    // UPDATE 2026-10-08: GotoDirective's embedded object is an AfterburnerIdea too
    // (GotoDirectiveHandle::Idea), constructed as a member; ConstructSubObjectD is gone.

    // ---- FULLPORT-DEFERRED stand-ins for cross-cutting / not-yet-ported
    // ---- calls the newly-ported methods above route through by name. ----
    // ReleaseGotoDirective (FUN_062d21c0) is GONE 2026-09-17: it is
    // Behavior_ReleaseDirectiveTarget(NavPilotPtr) in common_behavior.h, a real
    // five-instruction port. It was declared `static` with no receiver, which is
    // what kept the body empty -- retail's ECX is NavPilotPtr's value, not `this`.
    // SetCruiseLock (FUN_062d21d0) is inherited: Behavior::SetCruiseLock (common_behavior.h).
    // SetAlertLevel REMOVED 2026-09-17 (claude:w21-stubsweep): a guessed name for
    // FUN_062d88b0, which is MoveIdea::SetField44(int) (item 1620's first attributed
    // caller family). Every one of this class's eleven call sites has receiver
    // `LEA EDI,[ESI+0x30]` == SubObjectA, verified individually against the listing
    // (.text:062c3964/3a30/3c7e/4188/41a0/420d/42a2/43d0/4409/4434/49bf/4f58).
    // Disengage's three owner IObjAffect slot-0x60 sites (RequestType, &Watcher324 id, 0).
    void CancelOwnerRequest(int RequestType);
    // FUN_062c50e0 (item 1287), thiscall, RET 0: re-reads the dock hardpoints of Watcher324's
    // object for Field43c into the approach fields and sets CombatFlag344 / Facing38c.
    // Called from RecomputeApproach() and OnResult()'s Param1==0 path.
    void RefreshApproachState();

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. Struct sized to EXACTLY 0x470, which is retail's own
    // allocation size for this class (.text:062d1a07 `PUSH 0x470` immediately
    // before .text:062d1a2c `CALL FUN_062c2e60`, the constructor). UPDATED
    // 2026-09-17 (claude:common-dockmove): the four bytes at +0x46c used to be
    // a speculative `TailPad46c` reserved "so a future EnterState() case has
    // somewhere to write"; they are MaxApproachSpeed46c, a real float
    // EnterState() writes on every single call. See that field, and the whole
    // +0x2b8..+0x470 block below, which arrived from FormationBehavior this pass.
    // ------------------------------------------------------------------

    // +0x04..+0x2c: DockBehavior's own data (0x28 bytes) -- lives on the
    // DockBehaviorFields intermediate base above, so it isn't redeclared here.

    // +0x2c: IObjAffect::RequestCB secondary vtable pointer (base-class
    // subobject, placed here implicitly by the compiler -- no explicit field
    // needed; `static_cast<RequestCB*>(this)` gives the adjusted pointer).
    // SubObjectA below starts right after it (0x2c + sizeof(vfptr) == 0x30).

    // +0x30. IDENTIFIED 2026-09-11 (d285:common-port): this is a real, already-
    // ported MoveIdea (common_idea.h) -- ctor FUN_062d4e60 matches MoveIdea's
    // own confirmed ctor address exactly, and sizeof(MoveIdea)==0x78 matches
    // this field's long-confirmed span. SetContext() now calls
    // MoveIdea::SetContext() for real (own vtable slot 0); Disengage()'s tail
    // now calls MoveIdea::Commit() for real (own vtable slot 4, retail
    // `vtbl+0x10`). RecomputeApproach()'s own two SubObjectA touches
    // (`FUN_062d4e50(&SubObjectA, ...)` and `FUN_062d5a80(&SubObjectA)`) are
    // calls to OTHER, still-unidentified free functions that operate on
    // SubObjectA's fields directly (not virtual calls through its own
    // vtable) -- left FULLPORT-DEFERRED, unrelated to this identification.
    // Update()'s own SubObjectA.Commit() call (retail's post-threat-check
    // tail) stays FULLPORT-DEFERRED too: it's gated behind a still-unported
    // "is threatening"/distance check on ThreatSourcePtr14's own vtable, so
    // wiring just the now-known Commit() call in isolation would run it
    // unconditionally every Update() tick, which retail does not do.
    MoveIdea SubObjectA;    // +0x30.
    // +0xa8. IDENTIFIED 2026-09-11 (d285:common-port): real, already-ported
    // FaceIdea (common_idea.h) -- ctor FUN_062d6750 matches FaceIdea's own
    // confirmed ctor address, sizeof(FaceIdea)==0xc0 matches this field's
    // span. SetContext()/Disengage() wired the same way as SubObjectA above.
    FaceIdea SubObjectB;    // +0xa8.
    SplineMoveIdea SubObjectC;         // +0x168, ends +0x2dc. IDENTIFIED 2026-09-17
                                       // (claude:abi-pass50): ctor FUN_062d9bf0, vtable
                                       // .rdata:063a0568, COL at [-4] == 063a0564 ->
                                       // 063a9c18 -> SplineMoveIdea::RTTI_Type_Descriptor
                                       // (common_idea.h). sizeof(SplineMoveIdea) == 0x174
                                       // matches this field's long-confirmed span exactly,
                                       // and 0x168 + 0x174 == 0x2dc lands precisely on
                                       // GotoDirective below. Its own two embedded Idea
                                       // members explain two of the destructor's six
                                       // watcher clears: this+0x190 == SubObjectC.Move.Watcher
                                       // (0x168+0x14+0x14) and this+0x208 ==
                                       // SubObjectC.Face.Watcher (0x168+0x8c+0x14).

    // +0x2dc (ends +0x2f4). REPLACES the prior pass's single opaque
    // `SubObjectD[0x18]` byte buffer: items 1276/1277/1289/1290 show the far end
    // of this span is a directly-dereferenced raw pointer -- `NavPilotPtr` at
    // rel+0x14 (absolute +0x2f0; null-checked, its own vtbl+0x10/+0x1c/+0x28/+0xc
    // called). The still-constructed ctor call (FUN_062da500) is preserved
    // (ConstructSubObjectD below) since retail does call it as an 0x18-byte
    // default-constructor-shaped helper; the 0x10 bytes between the two pointers
    // (rel+0x4..+0x14) remain unconfirmed.
    //
    // rel+0 CORRECTED 2026-09-17 (claude:common-dockmove). It was named
    // `GotoWatcherPtr` and described as a raw pointer "called via its own
    // vtbl+0x10". It is not a pointer to something else -- it is the VTABLE
    // POINTER OF AN EMBEDDED SUB-OBJECT that starts right here. EnterState()'s
    // pre-switch release block reads it once and then passes the address of the
    // field itself as the receiver:
    //   .text:062c3d5e  MOV EDX,dword ptr [ESI + 0x2dc]   ; vtable
    //   .text:062c3d64  LEA ECX,[ESI + 0x2dc]             ; receiver == this+0x2dc
    //   .text:062c3d6a  CALL dword ptr [EDX + 0x10]
    // One deref for the vtable, `this+0x2dc` itself as ECX -- the same "embedded
    // object, first field IS the vtable pointer" pattern Disengage()'s own note
    // below already describes for this field, and consistent with retail
    // CONSTRUCTING something here (FUN_062da500) rather than storing a borrowed
    // pointer. Nothing anywhere zeroes it, which a borrowed pointer would need.
    //
    // IDENTIFIED 2026-10-08 (claude-code:cmn-ai2): the embedded object is an AfterburnerIdea
    // by value. Retail constructs it with FUN_062da500 (AfterburnerIdea::AfterburnerIdea,
    // RTTI-proven, item 1657 -- the same ctor BuzzBehavior::WatcherC gets), sizeof 0x14 ends
    // exactly at NavPilotPtr, and the three receiver calls are BaseIdea slots: slot 0
    // SetContext (Dock SetContext .text:062c3229) and slot 4 Commit (EnterState 062c3d6a,
    // Disengage 062c3ab6).
    struct GotoDirectiveHandle
    {
        AfterburnerIdea Idea;               // +0x00 (absolute +0x2dc)
        void* NavPilotPtr;                  // +0x14 (absolute +0x2f0), the manager's Goto behavior
    };
    GotoDirectiveHandle GotoDirective; // +0x2dc

    pub::AI::DirectiveDockOp DockOp;   // +0x2f4. Real, already-ported type (common_pub.h). Ends at +0x320.

    int ModeState320;          // +0x320. Zeroed by ctor. CONFIRMED (this pass) structurally
                                // identical to FormationBehavior::CurrentState@0x320: EnterState()
                                // (new, this pass) writes it exactly the way FUN_062c3d10 writes
                                // FormationBehavior's own CurrentState.
    BaseWatcher Watcher324;    // +0x324 (8 bytes, ends +0x32c). Ctor zeroes both dwords making up
                                // BaseWatcher (+0x324 Watched, +0x328 Next) in one instruction pair;
                                // the real dtor (FUN_062c30d0) clears it via
                                // BaseWatcher::set_pointer(nullptr). GetWatchedRawBlock() (vtable
                                // slot 24/26) reads its raw Watched pointer directly.
    unsigned StateFlags32c;    // +0x32c. Zeroed by ctor. OnResult() tests it against mask 0x840
                                // (== FormationBehavior's documented StateFlags bits 0x40|0x800).
    int Field330;               // +0x330. Set to -1 (sentinel) by RecomputeApproach()/Disengage().
    unsigned char ApproachClearedFlag334; // +0x334. Cleared by RecomputeApproach()/Disengage() --
                                           // CONFIRMED (this pass) same offset/role as
                                           // FormationBehavior::ApproachClearedFlag@0x334.
    unsigned char Pad335[0x338 - 0x335];
    float Field338;              // +0x338. EnterState(2) (.text:062c4148, FSTP dword): the
                                  // watched object's FUN_062e6010 attribute times 0.5f
                                  // (StateFlags32c bit 0x80) or 1/3 (bit 0x40). Retail's ctor
                                  // (062c2e60) leaves it unwritten, and so does the port.
    float Field33c;              // +0x33c. Written by RecomputeApproach() from
                                  // FUN_062e5af0/FUN_062e5ab0 (both FULLPORT-DEFERRED, not yet
                                  // individually processed).
    // +0x340. RETYPED 2026-09-17 (claude:common-dockmove) from `Opaque340[4]`: this is the
    // Cooldown-state elapsed-seconds accumulator UpdateApproachTail()'s case 0xb runs and
    // EnterState()'s case 0xb clears. A float, from the instructions themselves --
    // .text:062c5019 `FADD float ptr [ESI + 0x340]`, 062c501f `FSTP float ptr [ESI + 0x340]`,
    // 062c5049 `FCOMP float ptr [ESI + 0x340]`, and 062c44ac `MOV dword ptr [ESI+0x340],EBP`
    // with EBP == 0. Held (wrongly) on FormationBehavior as `StateTimer` until this pass.
    float StateTimer340;
    unsigned char CombatFlag344; // +0x344. Cleared by RecomputeApproach() -- CONFIRMED (this pass)
                                  // same offset/role as FormationBehavior::CombatFlag@0x344.
    unsigned char Pad345[0x348 - 0x345];
    ICRSplineSegment Spline;   // +0x348. Real, already-ported type (common_aisteer.h). Ends at +0x388.

    // CORRECTED 2026-09-11 (d270:common-port): the prior pass's `Vector Position388`
    // (claimed to span +0x388..+0x394) is disproved by re-reading RecomputeApproach's
    // raw disassembly (Common.dll.txt 188304-188317, LAB_062c3720/item 1276) byte-for-
    // byte: `ThreatSourcePtr14->vtbl[0x138]` takes THREE separate out-param addresses,
    // `LEA EDI,[EBP+0x388]` / `LEA EDI,[EBP+0x3cc]` / `LEA ESI,[EBP+0x38c]` -- and that
    // third one (+0x38c) is independently proven to be its OWN full 3-float Vector: the
    // exact same ESI pointer is immediately re-pushed as the sole argument to
    // `LookMatrixYup(Vector*)` (which reads 3 floats) right after the vtbl+0x138 call
    // returns. +0x38c being a real 12-byte Vector means the field at +0x388 can be AT
    // MOST 4 bytes wide (it would otherwise overlap +0x38c..+0x394) -- so the old
    // `Vector Position388` (0xC bytes) was wrong; the true width here is confirmed
    // exactly 4 bytes, primitive type still unconfirmed (could be a pointer/float/int
    // "position"-shaped out-param -- not guessed further here, see the placeholder-field
    // marathon convention). This correction removes the previously-unaccounted-for
    // Opaque394 gap entirely: +0x388 (4B) + +0x38c (12B Vector) lands EXACTLY on
    // AbortFlag398 @ +0x398 with zero slack, which is itself independent corroboration
    // this new split is the right one (the old layout left a spurious 4-byte gap here).
    // +0x388 (4 bytes). Width was confirmed 2026-09-11 (see the comment above); the
    // PRIMITIVE TYPE IS SETTLED NOW, 2026-09-17 (claude:common-dockmove), by
    // UpdateApproachTail()'s own reads of the same offset:
    //   .text:062c4a2c  CMP dword ptr [ESI + 0x388],EBP        ; null test, EBP == 0
    //   .text:062c4c8b  MOV ECX,dword ptr [ESI + 0x388]        ; then CALL [vtbl+0x20] on it
    //   .text:062c4f2e  MOV EBP,dword ptr [ESI + 0x388]        ; pushed as a call argument
    // A value loaded into ECX and immediately used as a thiscall receiver whose vtable is
    // read from [it] is a pointer to a polymorphic object, not a float or an int. The
    // pointee's class is still not identified (only its slot 0x20 "get id"-shaped getter is
    // exercised), so the name stays descriptive of the role UpdateApproachTail() gives it
    // rather than naming a type. Written by RecomputeApproach()'s (FULLPORT-DEFERRED)
    // "get transform" call, out-param 1. Held (wrongly) on FormationBehavior as
    // `TargetShipPtr` until this pass.
    void* TargetShipPtr388;
    Vector Facing38c;          // +0x38c (ends +0x398). CONFIRMED (this pass) same absolute
                                // offset AND same "3-float Vector consumed by a look/orient
                                // helper" role as FormationBehavior::Facing@0x38c -- a
                                // genuine cross-class layout match, not a guess. Written by
                                // RecomputeApproach()'s (FULLPORT-DEFERRED) "get transform"
                                // call, out-param 3; also the direct input to the
                                // (FULLPORT-DEFERRED) LookMatrixYup(&Facing38c) call that
                                // builds OrientMatrix3d8.
    unsigned char AbortFlag398; // +0x398. CONFIRMED (this pass, item 1275/1277) same offset/role
                                 // as FormationBehavior::AbortFlag@0x398.
    unsigned char Flag399;      // +0x399. Cleared by RecomputeApproach()/Disengage(); set to 1 by
                                 // OnWatchedObjectDestroyed()'s type-4 match branch.
    unsigned char Flag39a;      // +0x39a. Read/cleared by RecomputeApproach()/Disengage().
    unsigned char Flag39b;     // +0x39b. Zeroed by ctor.
    // +0x39c (ends +0x3a8). CARVED OUT OF Opaque39c 2026-09-17 (claude:common-dockmove).
    // EnterState()'s cases 2 and 6 hand `LEA ECX,[ESI+0x39c]` to the MoveIdea and FaceIdea
    // offset setters (.text:062c416a / 062c4244 / 062c4028), and the block at 062c4028 is
    // the tail of a three-FLD/FSTP computation that lands its result there -- i.e. a
    // computed lead/intercept point, three floats wide. Held (wrongly) on FormationBehavior
    // as `InterceptPoint` until this pass. The math that fills it is FULLPORT-DEFERRED (it
    // needs the unported AISTEER transforms), so in this port it keeps whatever the
    // previous state left.
    Vector InterceptPoint39c;
    // +0x3a8 (ends +0x3cc). A Matrix: RefreshApproachState (FUN_062c50e0) copies the third
    // dock-hardpoint transform's rotation here with `LEA EDI,[EBX+0x3a8]; MOV ECX,9;
    // MOVSD.REP` (.text:062c51d4-062c51e6) and reads its column z (+0x3b0/+0x3bc/+0x3c8).
    // EnterState case 6 hands its address to the FaceIdea basis setter FUN_062d75c0.
    // Retyped from `unsigned char Opaque3a8[0x24]` 2026-10-08.
    Matrix Orient3a8;
    // +0x3cc (ends +0x3d8). CARVED OUT OF Opaque39c 2026-09-17: a genuine three-float
    // block, proven by the dword-by-dword copy at .text:062c4c6f-062c4c88 that moves
    // [ESI+0x3cc][0..2] into [ESI+0x3fc][0..2], and by .text:062c3ecb
    // `FSUB float ptr [ESI + 0x3cc]` / 062c40a2 `FADD float ptr [ESI + 0x3cc]`. It is the
    // destination point EnterState()'s case 1 copies into DirectiveGotoOp::Destination and
    // cases 6/7 feed to the Move/Face offset setters. Name is the role, not a claimed
    // retail identifier.
    Vector DestPoint3cc;
    Matrix OrientMatrix3d8;    // +0x3d8 (ends +0x3fc). Built by RecomputeApproach() via
                                // LookMatrixYup(Facing-equivalent) and copied in (9-dword MOVSD.REP).
    // +0x3fc (ends +0x408). RETYPED 2026-09-17 from `Opaque3fc[12]`: the destination of the
    // same three-dword copy cited on DestPoint3cc above (.text:062c4c77 `LEA ECX,[ESI+0x3fc]`,
    // three `MOV [ECX+n]` stores), and read back a float at a time at .text:062c4ebb
    // `FADD float ptr [ESI + 0x3fc]` when case 9 builds its retreat point. Held (wrongly) on
    // FormationBehavior as `SavedPosition` until this pass.
    Vector SavedPosition3fc;
    float Field408;            // +0x408. Set to 16.0f by OnResult() under one branch (retail:
                                // `MOV dword ptr [outer+0x408],0x41800000`). Also the float*
                                // out-param of get_dock_hardpoints in RefreshApproachState.
    // +0x40c (ends +0x430). A Matrix: RefreshApproachState copies the first dock-hardpoint
    // transform's rotation here (9-dword MOVSD at .text:062c5175-062c5184) and reads its
    // column z (+0x414/+0x420/+0x42c). Split out of `Opaque40c[0x30]` 2026-10-08.
    Matrix Orient40c;
    unsigned char Opaque430[0x43c - 0x430]; // +0x430: EnterState reads a Vector here (RawAt)
    int Field43c;              // +0x43c. Set by OnResult() to its own 3rd int parameter; also
                                // reset to -1 by RecomputeApproach().
    int SubStateValue440;       // +0x440. Cleared by RecomputeApproach()/Disengage() -- CONFIRMED
                                 // (this pass) same offset/role as
                                 // FormationBehavior::SubStateValue@0x440.
    // +0x444: a Vector offset from DestPoint3cc. SetContext zeroes its three dwords;
    // BuildFinalApproachSpline (FUN_062c56d0, .text:062c57a4-062c57b7) adds it to
    // DestPoint3cc through FUN_062887d0 (Vector add) for the spline's P2. Typed 2026-10-08.
    Vector Field444;

    // formerly FUN_062c50c0 (`MOV EAX,[param]; MOV [this+0x444],EAX` x3, RET 4): Field444 =
    // Offset. Sole caller BaseFollowerBehavior (.text:062cbc3e) on its DockDelegate
    // (BehaviorDispatchTable[7], the Dock behaviour) with the Vector its Slot28 returned.
    // Defined in common_dockbehavior.cpp.
    void SetDestPointOffset(Vector const& Offset);

    // +0x450 (ends +0x46c). Small embedded object items 1257/1259
    // (FUN_062c2af0/FUN_062c2d90) operate on directly (ECX = &this->ProxWatcher450,
    // i.e. LEA this+0x450). First member is a BaseWatcher (ctor zeroes both its
    // dwords, +0x450/+0x454, in one instruction pair; the real dtor clears it via
    // BaseWatcher::set_pointer(nullptr) -- matching item 1270's note). The
    // remaining fields (relative +0x08/+0xc/+0x10/+0x14/+0x18, absolute
    // +0x458/+0x45c/+0x460/+0x464/+0x468) are read/written by items 1257/1259 as
    // a float triple + two flag dwords; item 1272 independently lists
    // +0x458/+0x45c/+0x460/+0x464/+0x468 as fields re-derived when a dock
    // directive is applied, corroborating this exact span. Not fully understood
    // (see items 1257/1259's own notes) -- kept as a named-but-opaque-tail
    // struct rather than raw offsets, so DockBehavior's own size is accurate.
    struct DockProximityWatcher
    {
        BaseWatcher Watcher;   // +0x00 (absolute +0x450)
        float Unknown08;       // +0x08 (absolute +0x458). Ctor zeroes as a raw dword.
        float Unknown0c;       // +0x0c (absolute +0x45c). Ctor zeroes as a raw dword.
        float Unknown10;       // +0x10 (absolute +0x460). Slot10 (FUN_062c3370) writes it:
                                // the op's +0x28 when > 0, else own get_speed(1, 4) over the
                                // docking target's get_speed(1, 1), clamped up to 1.0f
                                // (FCOMP float at 062c3547). Not ctor-zeroed.
        unsigned Flags14;      // +0x14 (absolute +0x464). Items 1257/1259 flags accumulator.
        unsigned SavedFlags18; // +0x18 (absolute +0x468). Items 1257/1259 previous-flags snapshot.
    };
    DockProximityWatcher ProxWatcher450; // +0x450, ends +0x46c

    // +0x46c (ends +0x470, the end of the object). PROMOTED FROM PADDING 2026-09-17
    // (claude:common-dockmove). It was `unsigned char TailPad46c[4]`, reserved on the
    // hypothesis that "a future EnterState() case" might need it. That case is here:
    // EnterState() writes this field unconditionally on entry and then again per state --
    //   .text:062c3d71  MOV EAX,[.rdata:DAT_0639f47c]        ; == 447A0000h == 1000.0f
    //   .text:062c3d76  MOV dword ptr [ESI + 0x46c],EAX
    //   .text:062c4213  MOV dword ptr [ESI + 0x46c],0x41200000   ; == 10.0f, case 6
    //   .text:062c4270 / 062c4333 / 062c4486  MOV [ESI+0x46c],<DAT_063ebdb8 == 18.0f>
    // -- and .text:062c47fc `MOV EAX,dword ptr [ESI + 0x46c]` (inside FUN_062c47f0, the
    // per-tick preamble) reads it back. Four bytes holding 1000.0f/10.0f/18.0f is a float.
    // It is also what makes this object 0x470 bytes rather than 0x46c, matching retail's
    // own `PUSH 0x470` exactly instead of padding up to it. Held (wrongly) on
    // FormationBehavior as `PendingTimeoutSeconds` until 2026-09-17.
    //
    // RENAMED 2026-09-17 (claude:orch-dockpreamble), PendingTimeoutSeconds46c ->
    // MaxApproachSpeed46c. "Seconds" was inherited from the FormationBehavior
    // misattribution and survived the field's promotion because nothing had yet
    // read the value. AdvancePreamble, ported this pass, is the reader, and it
    // does not count anything down with it: it min-clamps the value against
    // FormationFollowerEnvelope::Speed and against ProxWatcherComputeSpeedCap's
    // return, then feeds the result to MoveIdea::SetThrottleScale and
    // SplineMoveIdea::SetThrottleScale as a SPEED. Its three literals read as
    // speeds and not as durations: 1000.0f is this module's standing "no limit"
    // sentinel (the same DAT_0639f47c the angular cap starts at), and 18.0f/10.0f
    // are the slow-approach caps for the docking states that set them.
    float MaxApproachSpeed46c;
};

static_assert(offsetof(DockBehavior, SubObjectA) == 0x30, "DockBehavior::SubObjectA offset");
static_assert(offsetof(DockBehavior, SubObjectB) == 0xa8, "DockBehavior::SubObjectB offset");
static_assert(offsetof(DockBehavior, SubObjectC) == 0x168, "DockBehavior::SubObjectC offset");
static_assert(offsetof(DockBehavior, GotoDirective) == 0x2dc, "DockBehavior::GotoDirective offset");
static_assert(sizeof(AfterburnerIdea) == 0x14, "GotoDirectiveHandle::Idea is an AfterburnerIdea (0x14 bytes, ends at NavPilotPtr)");
static_assert(offsetof(DockBehavior::GotoDirectiveHandle, NavPilotPtr) == 0x14, "GotoDirectiveHandle::NavPilotPtr offset (absolute +0x2f0)");
static_assert(sizeof(DockBehavior::GotoDirectiveHandle) == 0x18, "GotoDirectiveHandle size");
static_assert(offsetof(DockBehavior, DockOp) == 0x2f4, "DockBehavior::DockOp offset");
static_assert(offsetof(DockBehavior, ModeState320) == 0x320, "DockBehavior::ModeState320 offset");
static_assert(offsetof(DockBehavior, Watcher324) == 0x324, "DockBehavior::Watcher324 offset");
static_assert(offsetof(DockBehavior, StateFlags32c) == 0x32c, "DockBehavior::StateFlags32c offset");
static_assert(offsetof(DockBehavior, Field330) == 0x330, "DockBehavior::Field330 offset");
static_assert(offsetof(DockBehavior, ApproachClearedFlag334) == 0x334, "DockBehavior::ApproachClearedFlag334 offset");
static_assert(offsetof(DockBehavior, Field33c) == 0x33c, "DockBehavior::Field33c offset");
static_assert(offsetof(DockBehavior, StateTimer340) == 0x340, "DockBehavior::StateTimer340 offset (Common.dll.txt 062c5019/062c501f/062c5049)");
static_assert(offsetof(DockBehavior, CombatFlag344) == 0x344, "DockBehavior::CombatFlag344 offset");
static_assert(offsetof(DockBehavior, Spline) == 0x348, "DockBehavior::Spline offset");
static_assert(offsetof(DockBehavior, TargetShipPtr388) == 0x388, "DockBehavior::TargetShipPtr388 offset (Common.dll.txt 062c4a2c/062c4c8b/062c4f2e)");
static_assert(offsetof(DockBehavior, Facing38c) == 0x38c, "DockBehavior::Facing38c offset");
static_assert(offsetof(DockBehavior, AbortFlag398) == 0x398, "DockBehavior::AbortFlag398 offset");
static_assert(offsetof(DockBehavior, Flag399) == 0x399, "DockBehavior::Flag399 offset");
static_assert(offsetof(DockBehavior, Flag39a) == 0x39a, "DockBehavior::Flag39a offset");
static_assert(offsetof(DockBehavior, Flag39b) == 0x39b, "DockBehavior::Flag39b offset");
static_assert(offsetof(DockBehavior, InterceptPoint39c) == 0x39c, "DockBehavior::InterceptPoint39c offset (Common.dll.txt 062c4028/062c416a/062c4244)");
static_assert(offsetof(DockBehavior, DestPoint3cc) == 0x3cc, "DockBehavior::DestPoint3cc offset (Common.dll.txt 062c4c6f-062c4c88, the 3-dword copy into SavedPosition3fc)");
static_assert(offsetof(DockBehavior, OrientMatrix3d8) == 0x3d8, "DockBehavior::OrientMatrix3d8 offset");
static_assert(offsetof(DockBehavior, SavedPosition3fc) == 0x3fc, "DockBehavior::SavedPosition3fc offset (Common.dll.txt 062c4c77/062c4ebb)");
static_assert(offsetof(DockBehavior, Field408) == 0x408, "DockBehavior::Field408 offset");
static_assert(offsetof(DockBehavior, Orient3a8) == 0x3a8, "DockBehavior::Orient3a8 offset (.text:062c51d4)");
static_assert(offsetof(DockBehavior, Orient40c) == 0x40c, "DockBehavior::Orient40c offset (.text:062c5175)");
static_assert(offsetof(DockBehavior, Field43c) == 0x43c, "DockBehavior::Field43c offset");
static_assert(offsetof(DockBehavior, SubStateValue440) == 0x440, "DockBehavior::SubStateValue440 offset");
static_assert(offsetof(DockBehavior, ProxWatcher450) == 0x450, "DockBehavior::ProxWatcher450 offset");
static_assert(sizeof(DockBehavior::DockProximityWatcher) == 0x1c, "DockProximityWatcher size (0x450..0x46c span confirmed by items 1257/1259/1272)");
static_assert(offsetof(DockBehavior, MaxApproachSpeed46c) == 0x46c, "DockBehavior::MaxApproachSpeed46c offset (Common.dll.txt 062c3d76/062c4213/062c4270/062c47fc)");
// SIZE TIGHTENED 2026-09-17 (claude:common-dockmove): was `>= 0x470`, justified as "padded
// 4 bytes past the previously-confirmed 0x46c ... so the shared tail cannot write past the
// end of the object". Retail settles it outright -- the factory that allocates this class
// does .text:062d1a07 `PUSH 0x470` immediately before .text:062d1a2c `CALL FUN_062c2e60`,
// which is this class's constructor. 0x470 is the exact retail size, and the trailing four
// bytes are not padding at all: they are MaxApproachSpeed46c, a real float EnterState()
// writes on every call. So the assert is now exact.
static_assert(sizeof(DockBehavior) == 0x470, "DockBehavior size == retail operator_new argument at .text:062d1a07");
