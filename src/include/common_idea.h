// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_csys.h"      // Matrix (FaceIdea::SetBasis's 9-dword REP MOVSD source)
#include "common_goods.h"     // BaseWatcher
#include "common_ini.h"       // Vector (MoveIdea::Position / MoveIdea::Direction)

#include <cstddef>

// Forward declaration only -- SplineMoveIdea::Spline is a POINTER, so the full
// definition (common_aisteer.h) is not needed here and including it would pull
// a much heavier header into every Idea consumer. COMMON_API is repeated so the
// declaration matches common_aisteer.h's own `struct COMMON_API ICRSplineSegment`
// exactly, whichever header a translation unit happens to see first.
struct COMMON_API ICRSplineSegment;
class SystemManager;    // BaseIdea::Handle (pointer only)

// ============================================================================
// BaseIdea family -- RTTI-identified AI "Idea" decision-object hierarchy.
// ============================================================================
//
// This is the real identity behind the two ubiquitous opaque sub-objects
// (informally "SubObjectA"/0x78 bytes and "SubObjectB"/0xc0 bytes) that every
// TractorArm-family Behavior subclass (DockBehavior, LaunchBehavior,
// LootBehavior, FaceBehavior, GuideBehavior, LargeShipBehavior, GotoBehavior,
// BuzzBehavior, ...) embeds via the two shared constructor helpers
// FUN_062d4e60 / FUN_062d6750, plus the smaller "SubObjectC"/"SubObjectD"
// (~0x14-0x18 bytes) some of them embed via FUN_062da500.
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes -- Ghidra's Common.dll.c has NO decompilation at all for most of this
// cluster, see "ground truth precedence" in this marathon's methodology):
//
//   FUN_062d4e60 installs vtable .rdata:PTR_LAB_063a042c at object+0. The
//   RTTICompleteObjectLocator pointer at (vtable-4) == .rdata:0x063a0428
//   points to DAT_063a9a00, which decodes as a genuine 32-bit COL:
//     pTypeDescriptor      -> .data:0x063ec588, name ".?AVMoveIdea@@"
//     pClassHierarchyDescriptor -> .rdata:DAT_063a99f0, numBaseClasses=2,
//       BaseClassArray = [ MoveIdea(self, numContained=1, mdisp=0),
//                           BaseIdea(numContained=0, mdisp=0) ]
//   => MoveIdea : public BaseIdea (single inheritance, no displacement).
//
//   FUN_062d6750 installs vtable .rdata:PTR_LAB_063a04ac at object+0. COL
//   pointer at (vtable-4) == 0x063a04a8 -> DAT_063a9a90, decodes to:
//     pTypeDescriptor -> .data:0x063ec5c4, name ".?AVFaceIdea@@"
//     pClassHierarchyDescriptor -> DAT_063a9a80, numBaseClasses=2,
//       BaseClassArray = [ FaceIdea(self, numContained=1, mdisp=0),
//                           BaseIdea(numContained=0, mdisp=0) ]
//   => FaceIdea : public BaseIdea (single inheritance, no displacement).
//
//   FUN_062da500 installs vtable .rdata:PTR_LAB_063a0584 at object+0. COL
//   pointer at (vtable-4) == 0x063a0580 -> DAT_063a9c60, decodes to:
//     pTypeDescriptor -> .data:0x063ec688 (region), name ".?AVAfterburnerIdea@@"
//     pClassHierarchyDescriptor -> DAT_063a9c50, numBaseClasses=2,
//       BaseClassArray = [ AfterburnerIdea(self, numContained=1, mdisp=0),
//                           BaseIdea(numContained=0, mdisp=0) ]
//   => AfterburnerIdea : public BaseIdea (single inheritance, no displacement).
//
// The SAME .rdata neighborhood (0x063a9900-0x063a9ce8) holds fully-formed
// RTTICompleteObjectLocator/ClassHierarchyDescriptor/BaseClassArray/
// TypeDescriptor chains for a MUCH larger sibling family sharing the common
// base BaseIdea: StopIdea, MoveIdea, MatchThrottle2Idea, FaceIdea,
// DirectionPicker, DodgeIdea, BreakAwayIdea, DodgeMissileIdea, IdleIdea,
// SplineMoveIdea, AfterburnerIdea (this list is NOT necessarily exhaustive --
// only the members whose ctors this pass actually needed were decoded).
//
// ADDENDUM 2026-09-17 (ABI pass 50, claude:abi-pass50) -- the whole vtable ->
// class map was walked end to end this pass by reading each table's COL at
// [-4] and that COL's +0x0c pTypeDescriptor straight out of the raw .rdata
// bytes. Every one of these eleven tables is exactly SIX slots long and has a
// real COL, so the BaseIdea slot shape below is confirmed family-wide:
//   .rdata:063a0410  COL 063a9970  BaseIdea           (abstract; slot 0 is the
//                                                      pure-call stub 06391fb0)
//   .rdata:063a042c  COL 063a9a00  MoveIdea           ctor FUN_062d4e60
//   .rdata:063a0448  COL 063a99b8  StopIdea           (also installed inline at
//                                                      MoveIdea+0x64)
//   .rdata:063a047c  COL 063a9a48  MatchThrottle2Idea ctor FUN_062d5aa0
//   .rdata:063a04ac  COL 063a9a90  FaceIdea           ctor FUN_062d6750
//   .rdata:063a04c8  COL 063a9af4  DodgeIdea          (+ DirectionPicker base)
//   .rdata:063a04f8  COL 063a9b40  BreakAwayIdea      (+ DirectionPicker base)
//   .rdata:063a0524  COL 063a9b88  DodgeMissileIdea
//   .rdata:063a054c  COL 063a9bd0  IdleIdea
//   .rdata:063a0568  COL 063a9c18  SplineMoveIdea     ctor FUN_062d9bf0
//   .rdata:063a0584  COL 063a9c60  AfterburnerIdea    ctor FUN_062da500
//
// The 063a047c class used to live in common_behavior.h under the invented name
// `FormationMoveFx`; since 2026-10-08 it is MatchThrottle2Idea below, a real
// BaseIdea sibling.
// This
// is retail's internal AI "Idea" decision-object system: each Behavior-family
// class picks/embeds one or more concrete Idea objects (a MoveIdea to drive
// toward a point, a FaceIdea to orient toward it, etc.) as its low-level
// execution primitive. DodgeIdea/BreakAwayIdea additionally multiply-inherit
// a second base, DirectionPicker (mdisp=0x14) -- not modeled here, out of
// scope for this pass (neither is embedded by FUN_062d4e60/062d6750/062da500).
//
// A prior investigation pass (decomp-tracker items 1601-1658, documented at
// length in common_aisteer.h) had already independently reverse-engineered
// nearly this entire cluster's FIELD LAYOUT and CONTROL FLOW by hand (no
// Ghidra decompile exists for any of these functions -- they are all huge,
// SEH-guarded, or simply outside Ghidra's frame-size limit) under the
// provisional guess "steering task" -- that investigation's mechanical
// findings (field offsets, the shared Bind() implementation, Update()'s
// control flow) are ALL independently confirmed correct by this pass's RTTI
// walk; only the NAME was wrong (it did not have RTTI decoded). Concretely:
//   - aisteer.h's "small-variant SteeringTask" (ctor FUN_062d6750, Bind
//     LAB_062d6820/item 1602, Update LAB_062d68b0/item 1603, ported as the
//     free function SteeringTaskSmall_Update) IS FaceIdea.
//   - aisteer.h's "PTR_LAB_063a0584 sibling" (ctor FUN_062da500) IS
//     AfterburnerIdea.
//   - aisteer.h's "5 vtables sharing one Bind() verbatim" (LAB_062d7920,
//     item 1617) are IdleIdea/AfterburnerIdea/DodgeIdea/BreakAwayIdea/
//     DodgeMissileIdea -- i.e. LAB_062d7920 is genuinely BaseIdea's own
//     default SetContext() implementation, inherited unchanged by 5 of the
//     (at least) 8 confirmed siblings; StopIdea/MoveIdea/FaceIdea each
//     override it with their own version (see each class below).
// This header does not re-derive any of that control-flow work -- see
// common_aisteer.h / decomp-tracker items 1601-1658 for the full writeup of
// Bind()/Update()'s internals. This header's own job is narrower: give the
// family a REAL C++ base/derived class layout (so DockBehavior/LaunchBehavior/
// LootBehavior/FaceBehavior/GuideBehavior/LargeShipBehavior/GotoBehavior/
// BuzzBehavior/... can eventually replace their raw `unsigned char
// SubObjectA[0x78]` / `SubObjectB[0xc0]` byte-blob members with real
// `MoveIdea`/`FaceIdea` members -- NOT attempted in this pass, see each of
// those headers' own SubObjectA/B comments, still FULLPORT-DEFERRED there).
//
// BaseIdea's own vtable slot shape (6 slots, confirmed identical layout
// across every sibling checked -- MoveIdea/StopIdea/FaceIdea all lay out
// their own vtables the same way, .rdata byte-extent confirmed by walking
// each vtable head to the next in sequence, not just get_vtable's own
// (known-undercounting, see this marathon's own "get_vtable unreliable"
// lesson) "observed slot" list):
//   slot 0 (0x00) SetContext(void*,void*,void*)->bool  -- has a real shared
//                 default (LAB_062d7920 / BaseIdea::SetContext below),
//                 overridden by StopIdea/MoveIdea/FaceIdea.
//   slot 1 (0x04) Update(float)->int                    -- always overridden.
//   slot 2 (0x08) RESERVED -- ICF-folded in EVERY confirmed sibling with the
//                 SAME unrelated retail method (MetaList<GoodDesc>::
//                 operator[]); never individually overridden anywhere in this
//                 family. No BaseIdea-specific semantics recovered.
//   slot 3 (0x0c) GetKind()->short                       -- always overridden;
//                 always a trivial `MOV AX,<constant>; RET` (MoveIdea=6,
//                 FaceIdea=0x10, StopIdea=2 -- confirmed from raw bytes).
//   slot 4 (0x10) Commit()                                -- always overridden
//                 with real per-subclass logic in MoveIdea/FaceIdea (confirmed
//                 THISCALL, ZERO explicit args -- bare `RET`, not `RET 0x4` --
//                 re-verified directly from LAB_062d56b0/LAB_062d74e0's own
//                 raw bytes; both route through the same still-blocked
//                 FUN_062e8ce0/FUN_062e89a0 pair FormationMoveFx::SetMode
//                 already defers on, items 1819/1821); ICF-folded with an
//                 unrelated class (Exclusion::AsteroidExclusionZone..) in
//                 StopIdea, i.e. StopIdea's own override is a functional
//                 no-op.
//   slot 5 (0x14) RESERVED -- same ICF-fold situation as slot 2 (unrelated
//                 target: CEquip::GetConnectionPosition).
//
// Retail's vtable has NO destructor slot anywhere in this family (all 6
// slots above are real behavior methods) -- these objects are always embedded
// by value inside a larger owner, never separately `delete`d. Matched here:
// BaseIdea deliberately declares NO virtual destructor (adding one would
// insert a 7th vtable slot that does not exist in retail and shift every
// other slot's index by one).
//
// Own-data layout: BaseIdea's own ctor-confirmed span is +0x04..+0x10
// (Object/Handle/Context/Attached) -- CONFIRMED IDENTICAL (same relative
// offsets, same zeroing code shape) across FUN_062d4e60 (MoveIdea),
// FUN_062d6750 (FaceIdea), and FUN_062da500 (AfterburnerIdea). MoveIdea and
// FaceIdea's own ctors ADDITIONALLY zero an 8-byte BaseWatcher-shaped region
// at +0x14 (also identical zeroing code shape in both); AfterburnerIdea's own
// ctor (FUN_062da500) does NOT touch +0x14 at all, and neither does the
// embedded-StopIdea sub-object inside MoveIdea's own ctor (see MoveIdea's
// EmbeddedStop field below) -- both confirmed to stop dead at +0x11. This is
// the evidence BaseWatcher-at-+0x14 is NOT part of BaseIdea itself, but is
// instead independently redeclared by MoveIdea and FaceIdea as their own
// first field, landing at the same relative offset only because BaseIdea's
// own size is fixed at 0x14. sizeof(BaseIdea) == 0x14, confirmed by
// AfterburnerIdea/StopIdea (both embedded and, per FUN_062da500's own item,
// standalone) never growing past it.
// ============================================================================

class BaseIdea
{
public:
    BaseIdea();

    // Slot 0 (offset 0x00). Common.dll.txt 219718-219735 (LAB_062d7920, item
    // 1617), fully traced, 15 raw instructions, no Ghidra decompile exists.
    // Rejects (returns false) if InObject or InHandle is null; else stores
    // InHandle -> Handle, InContext -> Context, InObject -> Object, sets
    // Attached = 1, returns true. This exact function is used VERBATIM
    // (byte-identical machine code, single shared entry point) as slot 0 of
    // FIVE confirmed sibling vtables (IdleIdea/AfterburnerIdea/DodgeIdea/
    // BreakAwayIdea/DodgeMissileIdea) -- i.e. this genuinely is BaseIdea's
    // own default implementation, not a guess. StopIdea/MoveIdea/FaceIdea
    // each override it below with their own version.
    virtual bool SetContext(void* InObject, void* InHandle, void* InContext);

    // Slot 1 (offset 0x04). Always overridden per-subclass in every sibling
    // checked; no shared/base implementation observed anywhere in the
    // family, unlike slot 0 above.
    virtual int Update(float DeltaTime) = 0;

    // Slot 2 (offset 0x08). Every sibling table except MatchThrottle2Idea's holds
    // .text:062ed730, a bare `RET 4`: ONE stack argument, no effect, no result
    // (corrected 2026-10-08, claude-code:cmn-ai5 -- it used to be declared with no
    // argument and an int result). The one caller found, TrailBehavior slot 8
    // (.text:062caa4a-062caa56), pushes the behavior's DeltaTime (+0x1c).
    // MatchThrottle2Idea overrides it (LAB_062d5bf0). Retail has no name for it.
    virtual void Slot2(float DeltaTime) { (void)DeltaTime; }

    // Slot 3 (offset 0x0c). "Kind" tag getter -- always overridden by a
    // trivial `MOV AX,<constant>; RET` in every confirmed sibling.
    virtual short GetKind() const = 0;

    // Slot 4 (offset 0x10). Always overridden with real per-subclass logic
    // (or, for StopIdea, an ICF-folded functional no-op) -- see class-level
    // comment. Confirmed zero explicit arguments.
    virtual void Commit() = 0;

    // Slot 5 (offset 0x14). Every one of the eleven tables (BaseIdea's own
    // included) holds .text:0632c0c0, `XOR AL,AL; RET 8`: two stack arguments,
    // returns false (corrected 2026-10-08, claude-code:cmn-ai5 -- it used to be
    // declared with no arguments and an int result). No caller found; retail has
    // no name for it.
    virtual bool Slot5(void* Arg1, void* Arg2) { (void)Arg1; (void)Arg2; return false; }

    // FUN_062d4e50 (item 1574), thiscall, RET 4, 5 instructions: `MOV EAX,[ECX+0xc];
    // MOV ECX,[EAX+0xc]; MOV [ECX+0x64],arg` -- Context is the IBehaviorManager, its +0x0C
    // the PathfindManager, whose Field_064 receives the id. No null tests. Called on the
    // MoveIdea members of DockBehavior (.text:062c3822/062c3834), BuzzBehavior
    // (FUN_062c6170) and StrafeBehavior (FUN_062ca310). Retail name unknown.
    void PublishTargetId(unsigned int Id);

    // ---- Confirmed own-data fields (+0x04..+0x10, span 0x14 with the
    // ---- leading vtable pointer). Ascending offset order. ----
    void* Object;              // +0x04. SetContext()'s 1st arg (ctor-zeroed).
    SystemManager* Handle;      // +0x08. SetContext()'s 2nd arg (ctor-zeroed): the owner's
                                // SystemManager, the behaviors' +0x24 NavHelper.
    void* Context;                // +0x0c. SetContext()'s 3rd arg. NOT
                                   // ctor-initialized in ANY confirmed sibling
                                   // (retail leaves it as stack garbage until
                                   // SetContext() is actually called) --
                                   // zero-initialized here anyway per this
                                   // marathon's documented "safe over-init"
                                   // convention (see e.g. DockBehavior's
                                   // Opaque388/Facing38c `{}` initializers).
    unsigned char Attached;         // +0x10. Ctor-zeroed; SetContext() sets 1
                                     // on success.
    unsigned char Pad11[3];          // +0x11, alignment only.
};
static_assert(offsetof(BaseIdea, Object) == 0x04, "BaseIdea::Object offset");
static_assert(offsetof(BaseIdea, Handle) == 0x08, "BaseIdea::Handle offset");
static_assert(offsetof(BaseIdea, Context) == 0x0c, "BaseIdea::Context offset");
static_assert(offsetof(BaseIdea, Attached) == 0x10, "BaseIdea::Attached offset");
static_assert(sizeof(BaseIdea) == 0x14, "BaseIdea size (own data ends at +0x14 -- see class comment)");

// ============================================================================
// StopIdea : public BaseIdea -- Common.dll RTTI name ".?AVStopIdea@@".
// ============================================================================
// Adds NO fields of its own beyond BaseIdea (confirmed twice independently:
// once as the embedded sub-object inside MoveIdea's own ctor at MoveIdea+0x64
// -- span exactly 0x14 bytes before the enclosing MoveIdea object ends at
// +0x78, see MoveIdea::EmbeddedStop below -- and once by FUN_062da500's own
// item notes, which ALSO only ever touch +0x04/+0x08/+0x10 before returning;
// FUN_062da500 turned out to be a different sibling (AfterburnerIdea, see
// below), but its identical "stop dead at +0x11" shape independently
// corroborates that this family's minimal/no-extra-fields leaf classes are
// genuinely just bare BaseIdea, not truncated versions of something bigger).
//
// Own vtable slot 0 (SetContext override): Common.dll.txt 219477-219478
// (LAB_062d76a0), 2 raw instructions -- `MOV AL,1; RET 0xc`. Unconditionally
// succeeds and touches NOTHING (ignores all 3 incoming pointers outright) --
// this exact function is independently already ported elsewhere in this
// marathon as NullBehavior::SetContext (common_nullbehavior.h/.cpp) via an
// ICF-fold with an entirely unrelated class; the decomp-tracker vtable global
// for StopIdea's own vtable (PTR_LAB_063a0448) still carries that stale
// ported_name from before this pass's RTTI identification -- left as-is
// (still byte-for-byte correct code, just also reachable under StopIdea's
// real name now) rather than duplicated.
//
// Own vtable slot 3 (GetKind): Common.dll.txt 223791-223793 (LAB_062da560)
// -- `MOV AX,0x2; RET`. Returns the constant 2.
//
// Own vtable slot 4 (Commit): ICF-folded with an unrelated class
// (Exclusion::AsteroidExclusionZone::..) -- i.e. StopIdea's own override is a
// functional no-op; not reproduced here (matches this marathon's established
// convention for ICF-fold slots, see e.g. DockBehavior's own vtable
// enumeration comment).
// ============================================================================
class StopIdea : public BaseIdea
{
public:
    StopIdea();

    // Own vtable slot 0. `MOV AL,1; RET 0xc` -- ignores all 3 parameters,
    // unconditionally returns true, touches no fields.
    bool SetContext(void* InObject, void* InHandle, void* InContext) override;

    // Own vtable slot 1 (LAB_0632b010). NOT traced this pass (address is far
    // outside this cluster's own 0x062d6000-0x062da600 range -- a distinct
    // investigation). FULLPORT-DEFERRED.
    int Update(float DeltaTime) override;

    // Own vtable slot 3. Always returns the constant 2.
    short GetKind() const override { return 2; }

    // Own vtable slot 4. ICF-folded with unrelated code -- functional no-op,
    // matching retail's own observable behavior for this slot on this class.
    void Commit() override {}
};

// ============================================================================
// AfterburnerIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVAfterburnerIdea@@". Ctor is FUN_062da500 (this marathon's "SubObjectC"
// / "SubObjectD" / "GotoDirectiveHandle ctor" in several sibling headers --
// see the class-level comment above and each of those headers' own
// ConstructSubObjectC/D comments for the historical naming).
// ============================================================================
// Adds NO fields of its own beyond BaseIdea -- FUN_062da500's own raw body
// (Common.dll.txt 223733-223744, 7 instructions, a thunk-shaped tiny ctor)
// zeroes only +0x04/+0x08 (both void*) and +0x10 (byte), i.e. exactly
// BaseIdea's own span, then installs PTR_LAB_063a0584 and returns -- no
// further bytes touched.
//
// NOTE for whoever next revisits DockBehavior's own GotoDirectiveHandle
// (common_dockbehavior.h): that struct's ctor is ALSO FUN_062da500 (i.e.
// retail really does construct an AfterburnerIdea there), but a later pass on
// that class (see its own 2026-09-11 follow-up comment) found the SAME
// storage's rel+0/rel+0x14 fields behaving as two independently-dereferenced
// raw pointers (GotoWatcherPtr/NavPilotPtr) rather than a single opaque
// object's own internals -- i.e. GotoDirectiveHandle most likely embeds an
// AfterburnerIdea* (or similar) POINTER pair, not an AfterburnerIdea BY
// VALUE, despite retail calling this same ctor helper on that storage. Not
// resolved further here; flagged since this pass's identification of
// FUN_062da500 bears directly on that still-open question.
//
// Own vtable slot 0 (SetContext): NOT overridden -- uses BaseIdea's own
// shared default (LAB_062d7920) verbatim, confirmed by direct XREF (see
// common_aisteer.h's own ADDENDUM: "PTR_LAB_063a0584 ... ctor FUN_062da500"
// is one of the five vtables LAB_062d7920 is XREF'd from).
//
// Own vtable slot 1 (Update): FUN_062da520 (item 1658). PORTED IN FULL
// 2026-09-17 (claude:common-idea-internals) -- Common.dll.txt 223762-223782, 13
// instructions, and the earlier "FX/sound-trigger style update" guess was only a
// shape guess. The whole body is:
//   if (Object == nullptr) return 3;                     // 062da523-062da530
//   Behavior_ConditionalNotify(Handle + 0x18, 1);        // 062da53b
//   Behavior_UpdateCruiseGate(Handle + 0x18, 1.0f, 0);   // 062da54d
//   return 0;                                            // 062da552
// i.e. it is the "hold the afterburner on" tick. Both helpers are real ports
// (common_behavior.h, items 1824 / 1819); the `1.0f`/`0` argument order is read
// off the `PUSH 0x3f800000` / `PUSH 0x0` pair, same convention as every other
// Behavior_UpdateCruiseGate call site in this header.
//
// Own vtable slot 3 (GetKind): RESOLVED 2026-09-17. The vtable's slot-3 entry
// (.rdata:063a0590) is LAB_062da560, `MOV AX,0x2; RET` -- so GetKind() returns 2,
// the same constant StopIdea returns and the same address (063a0454 is StopIdea's
// own slot 3, listed in that LAB's XREF header alongside 063a0590). Two siblings
// returning the same kind and being ICF-folded, not a coincidence.
//
// Own vtable slot 4 (Commit): LAB_062da570 (.rdata:063a0594), Common.dll.txt
// 223806-223810, 5 instructions: `Behavior_ConditionalNotify(Handle + 0x18, 0)`
// and nothing else -- the "let the afterburner off" counterpart of slot 1's
// `(..., 1)`. Retail does NOT null-check Handle here (it does in slot 1); the
// guard below is this port's documented convention, not a behaviour change.
// ============================================================================
class AfterburnerIdea : public BaseIdea
{
public:
    AfterburnerIdea();

    // Own vtable slot 1 (FUN_062da520, item 1658). Ported in full 2026-09-17 --
    // see the class-level comment for the three-line body and its citations.
    int Update(float DeltaTime) override;

    // Own vtable slot 3 (.rdata:063a0590 -> LAB_062da560, `MOV AX,0x2; RET`).
    // Returns the constant 2, ICF-folded with StopIdea's identical slot 3.
    short GetKind() const override { return 2; }

    // Own vtable slot 4 (.rdata:063a0594 -> LAB_062da570). Ported in full
    // 2026-09-17: a single Behavior_ConditionalNotify(Handle + 0x18, 0).
    void Commit() override;
};

// ============================================================================
// IdleIdea : public BaseIdea -- RTTI COL 063a9bd0 at [vtable-4] of .rdata:063a054c
// (see the table at the head of this header). Ctor FUN_062d9b10 (item 1644): zeroes
// BaseIdea's +0x04/+0x08/+0x10 and installs 063a054c -- no fields of its own (0x14 bytes).
// Embedded at IdleBehavior+0x2c and DelayBehavior+0x2c (FUN_062d0a10). Vtable:
//   0 LAB_062d7920 BaseIdea::SetContext      1 LAB_062d9b30 Update (below)
//   2 062ed730 `RET 4` (ReservedSlot2 shape)  3 LAB_062d9be0 `MOV AX,0x12; RET`
//   4 062a8920 bare `RET` (Commit)           5 0632c0c0 `XOR AL,AL; RET 8`
// Added 2026-10-08 (claude-code:cmn-ai2).
// ============================================================================
class IdleIdea : public BaseIdea
{
public:
    IdleIdea();

    // LAB_062d9b30 (item 1645), RET 4: Object null -> 3; else on Handle+0x18 (the
    // ThrustManager): FUN_062e89a0(0, 2), FUN_062e89a0(0, 0), FUN_062e8c20(&zero),
    // FUN_062e8ce0(&zero), FUN_062e9030(0), FUN_062e9120(0); returns 0.
    int Update(float DeltaTime) override;

    // Slot 3, LAB_062d9be0: `MOV AX,0x12; RET`.
    short GetKind() const override { return 0x12; }

    // Slot 4, .text:062a8920 (ICF-folded bare RET).
    void Commit() override {}
};
static_assert(sizeof(IdleIdea) == 0x14, "IdleIdea adds no fields to BaseIdea");

// ============================================================================
// MoveIdea : public BaseIdea -- Common.dll RTTI name ".?AVMoveIdea@@". Ctor
// is FUN_062d4e60 (this marathon's "SubObjectA", 0x78 bytes, across DockBehavior
// / LaunchBehavior / LootBehavior / FaceBehavior / GuideBehavior /
// LargeShipBehavior / GotoBehavior / BuzzBehavior / TrailBehavior /
// FollowBehavior and others -- see each of those headers' own
// ConstructSubObjectA comment).
// ============================================================================
// Full ctor raw disasm: Common.dll.txt 215579-215649 (53 instructions, no
// Ghidra decompile). Own-data span +0x14..+0x78 (0x64 bytes) on top of
// BaseIdea's own +0x04..+0x10:
//   +0x14 BaseWatcher Watcher     -- zeroed via the identical `LEA ECX,
//                                    [this+0x14]` + double-dword-zero +
//                                    conditional BaseWatcher::set_pointer(null)
//                                    pattern FaceIdea's own ctor also uses
//                                    (see FaceIdea::Watcher below) -- NOT part
//                                    of BaseIdea itself, see the class-level
//                                    comment's own-data-layout note above.
//   +0x1c..+0x30 (6 dwords)       -- zeroed. NAMED 2026-09-17: two Vectors,
//                                    Position (+0x1c) and Direction (+0x28),
//                                    proved by the two setters that write them
//                                    (see the setter block below).
//   +0x34 (1 dword)               -- zeroed. NAMED 2026-09-17: Mode, the 0/1/2
//                                    transition state SetMode() drives and
//                                    Commit() compares against 1.
//   +0x38 float PI    (0x40490fdb)
//   +0x3c float 200.0 (0x43480000)
//   +0x40 float 1.0   (0x3f800000)
//   +0x44, +0x48 (2 dwords)       -- zeroed. +0x48 NAMED 2026-09-17:
//                                    TargetKind48, the 0/1/2/3 tag the
//                                    SetWatchObject/SetPosition/SetDirection
//                                    setters stamp (none/watched-object/
//                                    fixed-point/direction-vector).
//   +0x4c, +0x50, +0x54 (3 dwords) -- zeroed.
//   +0x58 byte                    -- zeroed.
//   +0x59 byte = 1.
//   +0x5a..+0x5b                  -- alignment, unconfirmed.
//   +0x5c, +0x60 (2 dwords)       -- zeroed. RETYPED int -> float 2026-09-17:
//                                    SetContext() FSTPs an FPU result into each
//                                    (`FSTP float ptr [ESI+0x60]` at
//                                    .text:062d4f5b, `FSTP float ptr
//                                    [ESI+0x5c]` at .text:062d4f6e), so both
//                                    are genuinely floats, not ints. Zero has
//                                    the same bit pattern either way, which is
//                                    why the ctor alone could not tell.
//   +0x64 EmbeddedStop (StopIdea, 0x14 bytes, ends +0x78) -- installs
//         PTR_LAB_063a0448 (StopIdea's own vtable) directly at this+0x64,
//         zeroes rel+0x04/rel+0x08 (dwords) and rel+0x10 (byte); rel+0x0c is
//         left untouched (matches BaseIdea::Context's own "not
//         ctor-initialized" pattern) and rel+0x0c..+0x13 is otherwise
//         unconfirmed. This is a genuine embedded-by-value StopIdea object,
//         NOT a pointer -- confirmed by the vtable install happening inline
//         at a fixed positive offset with no separate allocation/CALL.
// Total confirmed span 0x00..0x78 == sizeof(MoveIdea) exactly.
//
// Own vtable slot 0 (SetContext): LAB_062d4f20 (item 1576), Common.dll.txt
// 215657-215704, 46 instructions, no Ghidra decompile. PORTED IN FULL
// 2026-09-17 (claude:common-idea-methods) -- every one of the 46 instructions
// is accounted for; see the .cpp for the instruction-address citations. Shape:
// the same `if (!Object || !Handle) return false` guard BaseIdea::SetContext
// uses, then Handle/Object/Context/Attached, then TWO queries on the incoming
// Object's own interface followed by a forward into the embedded StopIdea:
//   .text:062d4f55 CALL [Object->vtbl+0xf0] (0, 1.0f)  -> FSTP +0x60
//   .text:062d4f68 CALL [Object->vtbl+0xd8] (1.0f, 1)  -> FSTP +0x5c
//   .text:062d4f83 CALL [EmbeddedStop.vtbl+0x00] (Object, Handle, Context)
// Both slots are named in the rebuilt retail IObjInspect table
// (src/include/common_iface.h): 0xf0 is get_angular_speed_XY(float,float) and
// 0xd8 is get_speed(float, ThrustEquipType) -- so +0x60 caches the object's
// angular speed at unit throttle and +0x5c its linear speed at full engine
// throttle. Push order verified per call, they are NOT the same order:
// slot 0xf0 gets (0.0f, 1.0f) (PUSH 0x3f800000 then PUSH 0x0, so the zero is
// the argument closest to ESP) while slot 0xd8 gets (1.0f, 1).
//
// Own vtable slot 1 (Update): LAB_062d4fa0 (item 1577), 596 instructions, no
// Ghidra decompile. PORTED 2026-09-17 (claude:common-idea-internals) from the
// raw listing (Common.dll.txt 215717-216351) -- every branch traced, every
// stack slot's exact address recomputed across the mid-body `PUSH EBX`
// (.text:062d50e5) and `POP EBX` (.text:062d5325) that shift every later
// `[ESP+disp]` by four. See the .cpp for the instruction-address citations.
// Four things it settles that the header did not know before:
//
//   * BaseIdea::Handle is retail's NavHelper, and the nested block at
//     Handle+0x14fc carries a cached (radius, centre) pair refreshed at most
//     once per tick: `if (Handle[0x157a] == 0) { Handle[0x157a] = 1;
//     Owner->get_physical_radius(Nav[0x18], Nav[0x0c]); }` -- and 0x157a is
//     0x14fc + 0x7e, i.e. the gate byte and the byte the body sets are the SAME
//     byte (.text:062d4fc8 vs .text:062d4fed).
//
//   * Nav[+0x84] is an `IObjInspect*` -- the class three sibling headers still
//     call "the unidentified NavHelper-nested sub-object". Four independent slot
//     matches, all from this one function plus its immediate neighbour:
//     slot 0x2c takes (float&, Vector&) == get_physical_radius; slot 0x0c takes
//     nothing and returns a pointer used as a Matrix == get_transform (already
//     cross-confirmed by common_behavior.h's Behavior_InvokeNodeSlotC); slot
//     0x04 takes one hidden-return-buffer pointer and the caller immediately
//     takes sqrt(x*x+y*y+z*z) of it == `Vector get_velocity()` returned by
//     value; and FUN_062e4bb0, called from here on the same block, asks slot
//     0x88 for a dword it then tests against 0x30000/0x207 == get_type(uint&).
//     FUN_062e4bb0's own tail (`FLD [Elem+0x18]; FADD <other radius>; FSUBR
//     <centre distance>`) is radius arithmetic on exactly the pair slot 0x2c
//     filled, which is what makes the identification a chain rather than a
//     coincidence of argument counts.
//
//   * Field4c/Field50/Field54 are ONE Vector, not three ints: .text:062d5023
//     does `LEA EDX,[ESI+0x4c]` and passes it as the *Source* argument of the
//     3DMathEngine singleton's slot 0x2c, which x86math.h names
//     TransformMatrix(FVector& Dst, FMatrix const&, FVector const& Source).
//     Renamed `Offset4c` below.
//
//   * The return values are 0 (still steering), 1 (arrived -- Commit() was
//     called), 2 (the cruise gate refused) and 3 (no Object).
//
// Own vtable slot 3 (GetKind): LAB_062d6540 -- `MOV AX,0x6; RET`. Returns the
// constant 6. This exact function is ALSO reached from PTR_LAB_063a047c+0x0c
// (XREF at Common.dll.txt:217807 lists both 063a0438 and 063a0488).
// CORRECTED 2026-09-17 (claude:abi-pass50) -- the note that used to stand here
// called 063a047c "the UNRELATED FormationMoveFx vtable" and the fold "slot 4
// ... coincidental". Both halves are wrong:
//   * 0x063a0488 - 0x063a047c == 0x0c, i.e. slot 3 in BOTH tables, not slot 4.
//   * 063a047c's own COL at [-4] (0x063a0478 -> DAT_063a9a48) has
//     pTypeDescriptor .rdata:063a9a54 == `.data:MatchThrottle2Idea::
//     RTTI_Type_Descriptor`. 063a047c is MatchThrottle2Idea's vtable, i.e. a
//     SIBLING in this very family (it is also the class this port carries
//     under the invented name `FormationMoveFx`, common_behavior.h -- see the
//     naming note at the head of this header's SplineMoveIdea section's
//     sibling table and decomp-tracker item 1589).
// So this is two same-family siblings whose GetKind() both return 6 being
// folded at the SAME slot -- entirely expected, not a coincidence between
// unrelated classes.
//
// Own vtable slot 4 (Commit): LAB_062d56b0 (item 1578), 46 instructions,
// Common.dll.txt 216364-216411. PORTED IN FULL 2026-09-17
// (claude:common-idea-methods). The two helpers this method's own body depends
// on both turned out to be REAL PORTS already, under names a prior pass on this
// header did not cross-check (they were listed as "already-documented gaps"):
//   FUN_062e8ce0 == Behavior_SyncPredictedPosition(void*, Vector const*)
//   FUN_062e89a0 == Behavior_UpdateCruiseGate(void*, float, int)
// both declared in src/include/common_behavior.h and defined for real in
// common_behavior.cpp. (Behavior_SyncPredictedPosition is itself only ported
// for its front half -- see its own doc comment -- so that residual gap is
// inherited here rather than re-created here.) Body:
//   Behavior_SyncPredictedPosition(Handle+0x18, &ZeroVector)  -- unconditional
//   if (Mode != 1 && 0 <= StopStyle && StopStyle <= 1) {
//       Behavior_UpdateCruiseGate(Handle+0x18, 0.0f, 0);
//       Behavior_UpdateCruiseGate(Handle+0x18, 0.0f, 2); }
//   tail (unconditional): `[[Context+0xc]]+0x64 = 0` and
//   `[[Context+0xc]]+0x200..+0x208 = {0,0,0}`
// -- the SAME "external slot array" shape SetPosition/SetWatchObject below
// mirror into, independent corroboration of the family's "publish to the owning
// slot array" idiom. The StopStyle test is signed (`JL`/`JG` around 0 and 1).
//
// ============================================================================
// MoveIdea's NON-VIRTUAL setter family, and the /OPT:ICF trap around it
// ============================================================================
// Eight separate one-line setters and one small state machine configure a
// MoveIdea from outside. Retail calls every one of them NON-virtually, by raw
// address, and the linker folded most of them onto the address of a byte-
// identical method belonging to a COMPLETELY UNRELATED class -- in three cases
// a class whose own mangled export name survives, which is why Ghidra labels
// the call sites with names like `TextRenderContext::set_origin_h`. Those
// labels are real for the OTHER owner and meaningless for this one.
//
// A folded address proves NOTHING about the receiver's class (this marathon's
// lesson #44, and the exact error commit 4b0d528 had to correct). Ownership
// here is established the way that commit established it: from the ENCLOSING
// class's own constructor, which names each embedded sub-object with an
// explicit `LEA ECX,[reg+offset]` immediately before CALLing that sub-object's
// constructor, plus the RTTI Complete Object Locator at [vtable-4].
// Concretely, two independent enclosing classes:
//   GotoBehavior::FUN_062fd620  .text:062fd64b LEA ECX,[ESI+0x2c]  CALL FUN_062d4e60
//   TrailBehavior::FUN_062ca350 .text:062ca3xx LEA ECX,[EBP+0x148] CALL FUN_062d4e60
// and BOTH of those receivers are then handed to this same setter family --
// GotoBehavior's at .text:062fd953/062fd96d/062fda02/062fe3eb/062fe522/
// 062fe7d5/062fe7ea/062fe81f/062fe887, TrailBehavior's in one contiguous
// configuration block at .text:062cb03a-062cb068 that calls FIVE of them in a
// row on the single receiver EBP+0x148:
//   062cb03a FUN_062d5980(ESI, 0)                      -> Mode          (+0x34)
//   062cb046 CAsteroidCube::set_state(ESI, 1.0f)       -> Throttle (+0x40)
//   062cb04e FUN_062d5760(ESI, EBX)                    -> Watcher/TargetKind48
//   062cb05a TextRenderContext::set_origin_h(ESI, pi)  -> MaxThrustBearing (+0x38)
//   062cb068 DeviceTRC::bind_to_dc(ESI, [EBP+0x1e8])   -> ArrivalRange (+0x3c)
// Five consecutive folded "unrelated class" symbols landing on five
// consecutive MoveIdea fields, on a receiver whose constructor is MoveIdea's,
// is not a coincidence; it is one MoveIdea configuration block.
//
// The folds, with each address's OTHER (real, correctly-named, already-ported)
// owner named so nobody re-derives them as MoveIdea's:
//   .text:063275f0 `MOV [ECX+0x38],EAX; RET 4`  also TextRenderContext::set_origin_h
//                                              (tracker item 13878, ported in
//                                              common_textrender.cpp -- that port is
//                                              correct and stays)
//   .text:062a66b0 `MOV [ECX+0x3c],EAX; RET 4`  also DeviceTRC::bind_to_dc
//                                              (item 11443, common_textrender.cpp)
//                                              AND CAsteroidField::set_system
//   .text:062a33e0 `MOV [ECX+0x40],EAX; RET 4`  also CmnAsteroid::CAsteroidCube
//                                              ::set_state
//   .text:062d88b0 `MOV [ECX+0x44],EAX; RET 4`  item 1620, 21 call sites, no
//                                              single owner -- correctly left
//                                              `skipped` as linker glue
//   .text:062d5a10 `MOV [ECX+0x58],AL;  RET 4`  item 1584, also reachable as
//                                              FormationMoveFx::SetEnabled
// and the four non-trivial ones, which are NOT distinguishable from genuine
// single functions shared between two classes -- stated rather than guessed:
//   .text:062d5760 (73 insns) FUN_062d5760, also FormationMoveFx::SetWatchObject
//   .text:062d5840 (24 insns) FUN_062d5840, also FormationMoveFx::SetPosition
//   .text:062d5890 (37 insns) FUN_062d5890, also FormationMoveFx::SetDirection
//   .text:062d5980 (49 insns) FUN_062d5980, also FormationMoveFx::SetMode
// For these four the listing genuinely cannot say whether link.exe folded two
// identical source methods or whether retail wrote one and called it on both
// classes: MoveIdea and MatchThrottle2Idea (the class this port still carries
// under the invented name FormationMoveFx, see the naming note at the head of
// this header) have IDENTICAL layout from +0x00 through +0x48 -- BaseIdea's
// four fields, a BaseWatcher at +0x14, a Vector at +0x1c, a Vector at +0x28, a
// mode int at +0x34, three floats, and a kind tag at +0x48. RTTI settles that
// they are still two distinct classes with no intermediate base
// (MoveIdea's ClassHierarchyDescriptor has numBaseClasses=2: itself and
// BaseIdea), so the shared code canNOT be hoisted onto BaseIdea -- BaseIdea's
// own size is 0x14 and none of these fields exist in it. Duplicating the
// bodies onto both classes, which is what this header does, is therefore the
// faithful choice, not a shortcut.
// ============================================================================
class MoveIdea : public BaseIdea
{
public:
    MoveIdea();

    // Own vtable slot 0 -- LAB_062d4f20 (item 1576). Ported in full; see the
    // class-level comment's slot-0 section for the instruction citations.
    bool SetContext(void* InObject, void* InHandle, void* InContext) override;

    // Own vtable slot 1 (LAB_062d4fa0, item 1577, 596 instructions). Ported
    // 2026-09-17; see the class-level comment's slot-1 section and the .cpp.
    // Returns 0/1/2/3 (see that section). ONE residual gap, named at its own
    // call site in the .cpp: the TargetKind48 == 1 arm needs FUN_062e4bb0
    // (item 1749), the still-unported `Iface` twin of common_aisteer.h's
    // GetInterceptClearance.
    int Update(float DeltaTime) override;

    // Own vtable slot 3. Always returns the constant 6.
    short GetKind() const override { return 6; }

    // Own vtable slot 4 (LAB_062d56b0, item 1578). Ported in full; see the
    // class-level comment's slot-4 section.
    void Commit() override;

    // ---- Non-virtual setters, all called by raw address in retail. Each one
    // ---- carries its retail address; see the class-level comment for the
    // ---- /OPT:ICF fold table and how ownership was proved. ----

    // FUN_062d5840, RET 4. TargetKind48 = 2, Position = Pos, then mirrors
    // Position into the owning slot array at `[[Context+0xc]]+0x200..+0x208`.
    void SetPosition(Vector const& Pos);

    // FUN_062d5890, RET 4. TargetKind48 = 3, Direction = Dir, then renormalizes
    // Direction in place (no zero-length guard, matching retail).
    void SetDirection(Vector const& Dir);

    // FUN_062d58f0, RET 4. TargetKind48 = 3, Direction = -(Src+0x08, Src+0x14,
    // Src+0x20) -- a foreign object's basis-vector column -- then renormalizes.
    void SetDirectionFrom(void const* Src);

    // FUN_062d5760, RET 4. TargetKind48 = 1 and Watcher.set(Object+8) when
    // Object is non-null, else TargetKind48 = 0 and Watcher.set(nullptr). Then
    // mirrors the watched object's id into the slot array's +0x64 and its
    // position into +0x200..+0x208 (or zeroes both when cleared).
    void SetWatchObject(void const* Object);

    // FUN_062d5980, RET 4. The Mode 0/1/2 transition machine -- see the .cpp
    // for the exact branch table. Ends with Mode = NewMode on every path.
    void SetMode(int NewMode);

    // Folded at .text:063275f0 (= TextRenderContext::set_origin_h).
    // MaxThrustBearing = Value. Most call sites pass 0x40490fdb (pi, the ctor
    // default); DockBehavior passes 0x3eb2b8c3 at .text:062c4400.
    void SetAngle38(float Value);

    // Folded at .text:062a66b0 (= DeviceTRC::bind_to_dc). ArrivalRange =
    // Value. Retail passes a dword straight through, so callers holding the
    // value as raw bits must reinterpret rather than convert.
    void SetRange3c(float Value);

    // Folded at .text:062a33e0 (= CmnAsteroid::CAsteroidCube::set_state).
    // Throttle = Value.
    void SetScale40(float Value);

    // FUN_062d5a20 (item 1585), .text:062d5a20-062d5a62, 26 insns, RET 4.
    // The DERIVED sibling of SetScale40: instead of storing Value into
    // Throttle it stores `get_projected_throttle(Value, <Mode's thrust
    // type>)`. Exactly FaceIdea::SetThrottleScale's shape one class over, which
    // is why it carries the same name.
    //
    // Mode selects WHICH propulsion system is asked, and the ladder is its own
    // confirmation that this reading is right: 0 -> 1, 1 -> 3, 2 -> 4 are
    // ThrustEquipEngine / ThrustEquipTradelane / ThrustEquipCruise
    // (common_cequip.h). Three meaningful enumerators landing in order is not
    // coincidence.
    //
    // ITEM 1585's `blocked` REASON WAS STALE and is retired rather than re-filed.
    // It read "Field04's real interface/class is not identified anywhere in this
    // port -- calling through an unnamed vtable slot with an unknown calling
    // convention would be guessing". Field04 is BaseIdea::Object; slot 0xd4 is
    // IObjInspect::get_projected_throttle(float, ThrustEquipType) (common_iface.h,
    // identified 2026-09-16); and the trampoline FUN_062e5b70 is itself a complete
    // port, Behavior_InvokeManagerSlotD4 (item 1765).
    //
    // Retail's `default` arm forwards Value's raw FLOAT BIT PATTERN as the enum;
    // reproduced as written (Mode is not provably 0..2); see the .cpp.
    void SetThrottleScale(float Value);

    // Folded at .text:062d88b0 (tracker item 1620, correctly left `skipped`).
    // StopStyle = Value (formerly Field44). Observed arguments are the small ints
    // 0 and 2; the field is read back by Commit() as a signed int range-tested
    // against [0,1].
    void SetField44(int Value);

    // Folded at .text:062d5a10 (= FormationMoveFx::SetEnabled, item 1584).
    // Field58 = Value: the caller's byte is stored as it is (`MOV AL,[ESP+4]; MOV byte
    // [ECX+0x58],AL`), not normalised to 0/1 -- GotoBehavior::Slot10 passes op+0x59 raw.
    void SetEnabled(unsigned char Value);

    // .text:062d5a70 (item 1586), RET 4 -- `MOV AL,[ESP+4]; MOV byte
    // [ECX+0x59],AL; RET 4`, i.e. bKillEngineOffBearing = Value (formerly
    // Field59). The NEIGHBOURING byte to
    // SetEnabled()'s, and the only thing that separates the two addresses.
    // ONE call site in the whole module, .text:062c43f4, whose receiver is
    // `MOV ECX,EDI` with EDI set by `LEA EDI,[ESI+0x30]` at .text:062c4380 --
    // DockBehavior's SubObjectA, i.e. this class. Item 1586 used to record the
    // receiver as "FormationBehavior's this+0xc"; that predates the 2026-09-17
    // identification of FUN_062c3d10 as DockBehavior::EnterState and is wrong.
    void SetField59(unsigned char Value);

    // ---- Confirmed own-data fields, ascending offset order. ----
    //
    // RENAMED 2026-09-17 (claude:common-idea-methods), old name -> new, for any
    // lane holding a reference to the previous spelling:
    //   Field1c/Field20/Field24 -> Position (one Vector)
    //   Field28/Field2c/Field30 -> Direction (one Vector)
    //   Field34 -> Mode        Field48 -> TargetKind48
    //   Field5c -> MaxSpeed5c (int -> float)
    //   Field60 -> MaxAngularSpeedXY60 (int -> float)
    // RENAMED 2026-10-15 (claude-code:cmn-aif2), from Update()/Commit()'s own reads:
    //   AngleDefaultPi38 -> MaxThrustBearing   RangeDefault200_3c -> ArrivalRange
    //   ScaleDefault1_40 -> Throttle           Field44 -> StopStyle
    //   Field59 -> bKillEngineOffBearing
    // Field58 stays unnamed: nothing in MoveIdea's code (062d4e60-062d5a80) reads it.
    BaseWatcher Watcher;        // +0x14, ends +0x1c.
    Vector Position;              // +0x1c, ends +0x28. Written by SetPosition()
                                   // (which also stamps TargetKind48 = 2) and
                                   // mirrored into the owning slot array.
    Vector Direction;               // +0x28, ends +0x34. Written by
                                     // SetDirection()/SetDirectionFrom() (both
                                     // stamp TargetKind48 = 3), always kept
                                     // normalized by those setters.
    int Mode;                         // +0x34. Ctor-zeroed. SetMode()'s 0/1/2
                                       // transition state; Commit() compares it
                                       // against 1.
    // +0x38, formerly AngleDefaultPi38. Ctor default PI (0x40490fdb); SetAngle38().
    // Update() FCOMPs BearingAngleToPoint(Object, Goal) against it (062d5422,
    // 062d55fe): strictly below -> thrust toward Goal (SetThrottle at 062d543a /
    // 062d5667); otherwise cruise is dropped (062d544e) or the throttle is cut to 0
    // and the engine switched per bKillEngineOffBearing (062d5683-062d569f).
    float MaxThrustBearing;
    // +0x3c, formerly RangeDefault200_3c. Ctor default 200.0f; SetRange3c(). Update()
    // commits and returns 1 when the squared target distance is below its square
    // (062d50b4-062d50dc), and adds it to the stopping lookahead at 062d5340.
    float ArrivalRange;
    // +0x40, formerly ScaleDefault1_40. Ctor default 1.0f; SetScale40(), and
    // SetThrottleScale() stores get_projected_throttle into it. Update() passes it as
    // ThrustManager::SetThrottle's throttle (062d5435-062d543a cruise, 062d560b the
    // cap of the engine throttle) and scales the nudge direction by it (062d5511).
    float Throttle;
    // +0x44, formerly Field44. Ctor-zeroed; SetField44(). How Update() slows down
    // once the stopping lookahead reaches the target while still moving:
    // 0 = throttle cut (SetThrottle(0), lookahead FUN_062e5ab0), 1 = EmbeddedStop
    // (StopIdea) Update, lookahead FUN_062e58f0; 2 (any other) = no
    // lookahead, nothing done, and Mode 2 is not dropped near the target
    // (062d51e5). Commit() zeroes both throttles only for 0..1 (062d56e4).
    int StopStyle;
    int TargetKind48;                           // +0x48. Ctor-zeroed. 0 = nothing,
                                                 // 1 = watched object, 2 = fixed point,
                                                 // 3 = direction vector -- stamped by
                                                 // the four target setters above.
    Vector Offset4c;                              // +0x4c, ends +0x58. Ctor-zeroed
                                                   // (three dwords). RETYPED
                                                   // 2026-09-17: Update() hands
                                                   // `&this->Offset4c` to
                                                   // I3DMathEngine::TransformMatrix
                                                   // as its Source argument
                                                   // (.text:062d5023), so the three
                                                   // dwords are one Vector -- an
                                                   // offset in the owner's own frame
                                                   // that Update() rotates into world
                                                   // space and adds to the cached nav
                                                   // centre to get its lead point.
                                                   // Old names Field4c/Field50/Field54.
    unsigned char Field58;                              // +0x58. Ctor-zeroed byte.
                                                         // SetEnabled(). No reader in
                                                         // MoveIdea's own code.
    // +0x59, formerly Field59. Ctor default 1; SetField59(). Read only at 062d5692:
    // when the bearing is not below MaxThrustBearing, Update() calls
    // ThrustManager::SetEngineActive(bKillEngineOffBearing == 0).
    unsigned char bKillEngineOffBearing;
    unsigned char Pad5a[0x5c - 0x5a];                       // +0x5a, alignment only.
    float MaxSpeed5c;                                         // +0x5c. Ctor-zeroed;
                                                               // SetContext() fills it from
                                                               // Object->get_speed(1.0f, 1).
    float MaxAngularSpeedXY60;                                  // +0x60. Ctor-zeroed;
                                                                 // SetContext() fills it from
                                                                 // Object->
                                                                 // get_angular_speed_XY(0,1).
    StopIdea EmbeddedStop;         // +0x64, ends +0x78. Embedded-by-value StopIdea
                                    // sub-object -- see class-level comment.
};
static_assert(offsetof(MoveIdea, Watcher) == 0x14, "MoveIdea::Watcher offset");
static_assert(offsetof(MoveIdea, Position) == 0x1c, "MoveIdea::Position offset");
static_assert(offsetof(MoveIdea, Direction) == 0x28, "MoveIdea::Direction offset");
static_assert(offsetof(MoveIdea, Mode) == 0x34, "MoveIdea::Mode offset");
static_assert(offsetof(MoveIdea, MaxThrustBearing) == 0x38, "MoveIdea::MaxThrustBearing offset");
static_assert(offsetof(MoveIdea, ArrivalRange) == 0x3c, "MoveIdea::ArrivalRange offset");
static_assert(offsetof(MoveIdea, Throttle) == 0x40, "MoveIdea::Throttle offset");
static_assert(offsetof(MoveIdea, StopStyle) == 0x44, "MoveIdea::StopStyle offset");
static_assert(offsetof(MoveIdea, TargetKind48) == 0x48, "MoveIdea::TargetKind48 offset");
static_assert(offsetof(MoveIdea, Offset4c) == 0x4c, "MoveIdea::Offset4c offset");
static_assert(offsetof(MoveIdea, Field58) == 0x58, "MoveIdea::Field58 offset");
static_assert(offsetof(MoveIdea, bKillEngineOffBearing) == 0x59, "MoveIdea::bKillEngineOffBearing offset");
static_assert(offsetof(MoveIdea, MaxSpeed5c) == 0x5c, "MoveIdea::MaxSpeed5c offset");
static_assert(offsetof(MoveIdea, MaxAngularSpeedXY60) == 0x60, "MoveIdea::MaxAngularSpeedXY60 offset");
static_assert(offsetof(MoveIdea, EmbeddedStop) == 0x64, "MoveIdea::EmbeddedStop offset");
static_assert(sizeof(MoveIdea) == 0x78, "MoveIdea size (matches every sibling header's confirmed SubObjectA size)");

// ============================================================================
// MatchThrottle2Idea : public BaseIdea -- RTTI COL 063a9a48 at [vtable-4] of
// .rdata:063a047c names ".?AVMatchThrottle2Idea@@"; its ClassHierarchyDescriptor
// (063a9a38, numBaseClasses 2) is [MatchThrottle2Idea, BaseIdea]. Ctor FUN_062d5aa0,
// sizeof 0x5c. Embedded at TrailBehavior+0x2c and at +0x34 of the FUN_062cb2b0
// family (FollowBehavior, FormationBehavior).
//
// Re-ported from the listing 2026-10-08 (claude-code:cmn-ai5). Until then this
// port carried the class as `FormationMoveFx` in common_behavior.h, outside the
// Idea hierarchy, with MoveIdea's +0x28 Direction / +0x34 Mode / +0x48 kind tag
// projected onto it and MoveIdea's setters (FUN_062d5760/5840/5890/58f0/5980,
// all MoveIdea's own -- every listing call site's receiver is a MoveIdea)
// declared as its methods. None of that exists here: the ctor never writes
// +0x28, writes pi/4 as a FLOAT at +0x34, and nothing of this class reads +0x48.
//
// The class matches the watched ship's throttle (and optionally its cruise and
// nudge): Update measures the distance and closing speed to the watched object,
// converts them through the owner's thrust model (IObjInspect get_projected_throttle,
// get_speed, get_time_to_accelerate, get_distance_travelled) into a throttle and
// drives the ThrustManager with it.
//
// Vtable .rdata:063a047c, six slots:
//   0 LAB_062d5b50 SetContext   1 LAB_062d5ce0 Update   2 LAB_062d5bf0 Slot2
//   3 LAB_062d6540 GetKind (6, folded with MoveIdea's)  4 LAB_062d6550 Commit
//   5 .text:0632c0c0 BaseIdea::Slot5
// Non-virtual members: FUN_062d65f0 SetWatchAndPosition, FUN_062d6670
// SetWatchAndDistance, FUN_062d6700..062d6740 the five one-store setters, and
// BaseIdea::PublishTargetId (FUN_062d4e50, called on TrailBehavior+0x2c).
// ============================================================================
class MatchThrottle2Idea : public BaseIdea
{
public:
    // FUN_062d5aa0 (.text:062d5aa0-062d5b48).
    MatchThrottle2Idea();

    // Slot 0, LAB_062d5b50 (RET 0xc): rejects a null Object or Handle; otherwise
    // stores the three, Attached = 1, and seeds Speed4c / Speed50 from the
    // owner's thrust model (cruise and engine).
    bool SetContext(void* InObject, void* InHandle, void* InContext) override;

    // Slot 1, LAB_062d5ce0 (RET 4, 677 instructions).
    int Update(float DeltaTime) override;

    // Slot 2, LAB_062d5bf0 (RET 4, argument unread): engage or drop cruise to
    // follow the watched ship.
    void Slot2(float DeltaTime) override;

    // Slot 3, LAB_062d6540 `MOV AX,0x6; RET` -- the same address as MoveIdea's.
    short GetKind() const override { return 6; }

    // Slot 4, LAB_062d6550 (RET 0).
    void Commit() override;

    // FUN_062d65f0 (RET 8): watch Object, aim at Point, Distance28 = 0, and mirror
    // the watched object's position into PathfindManager::CachedTargetPosition.
    void SetWatchAndPosition(void const* Object, Vector const& Point);

    // FUN_062d6670 (RET 8): watch Object, Position = 0, Distance28 = Distance, and
    // the same mirror.
    void SetWatchAndDistance(void const* Object, float Distance);

    // FUN_062d6700 / 062d6710 / 062d6720 / 062d6730: `MOV AL,[ESP+4]; MOV
    // [ECX+0x38+n],AL; RET 4` -- the raw argument byte is stored, not normalised
    // (FollowBehavior::Slot27 passes Flag28c's byte through at 062cd205-062cd210).
    void SetFlag38(unsigned char Value) { Flag38 = Value; }
    void SetFlag39(unsigned char Value) { Flag39 = Value; }
    void SetFlag3a(unsigned char Value) { Flag3a = Value; }
    void SetFlag3b(unsigned char Value) { Flag3b = Value; }

    // FUN_062d6740: `MOV EAX,[ESP+4]; MOV [ECX+0x58],EAX; RET 4`.
    void SetCatchUpRange(float Range) { CatchUpRange58 = Range; }

    // .text:063275d0 `MOV [ECX+0x34],EAX; RET 4` (ICF-folded; one call site on this
    // class, TrailBehavior::SetContext at .text:062ca5cf with pi).
    void SetMaxBearing(float Angle) { MaxBearing34 = Angle; }

    // ---- own fields (+0x14..+0x5b) ----
    BaseWatcher Watcher;          // +0x14  the ship being matched
    Vector Position;              // +0x1c  Update: the watched centre; FUN_062d65f0: the point
    float Distance28;             // +0x28  standoff distance; NOT written by the ctor
    float Speed2c;                // +0x2c  ctor 0; Update: Speed4c / Speed50 or a travel distance
    float ThrottleBias30;         // +0x30  ctor 0; Update's throttle correction
    float MaxBearing34;           // +0x34  ctor pi/4 (0x3f490fdb)
    unsigned char Flag38;         // +0x38  ctor 1: query the watched ship's cruise state
    unsigned char Flag39;         // +0x39  ctor 0: Commit keeps cruise when set
    unsigned char Flag3a;         // +0x3a  ctor 0: match the watched ship's nudge
    unsigned char Flag3b;         // +0x3b  ctor 0: afterburner allowed
    unsigned char Flag3c;         // +0x3c  ctor 1: catch up beyond CatchUpRange58
    unsigned char Flag3d;         // +0x3d  catching up this tick (Update writes it)
    unsigned char Pad3e[2];
    float ClosingFloor40;         // +0x40  ctor -200.0f
    float ClosingBias44;          // +0x44  ctor -0.5f
    float Field48;                // +0x48  ctor 0.05f (0x3d4ccccd); no reader in this class's code
    float Speed4c;                // +0x4c  SetContext: cruise-type speed + 300
    float Speed50;                // +0x50  SetContext: engine-type speed + 50
    float ClosingWindow54;        // +0x54  ctor 5.0f
    float CatchUpRange58;         // +0x58  ctor 500.0f; FUN_062d6740
};
static_assert(offsetof(MatchThrottle2Idea, Watcher) == 0x14, "MatchThrottle2Idea::Watcher offset");
static_assert(offsetof(MatchThrottle2Idea, Position) == 0x1c, "MatchThrottle2Idea::Position offset");
static_assert(offsetof(MatchThrottle2Idea, Distance28) == 0x28, "MatchThrottle2Idea::Distance28 offset");
static_assert(offsetof(MatchThrottle2Idea, Speed2c) == 0x2c, "MatchThrottle2Idea::Speed2c offset");
static_assert(offsetof(MatchThrottle2Idea, ThrottleBias30) == 0x30, "MatchThrottle2Idea::ThrottleBias30 offset");
static_assert(offsetof(MatchThrottle2Idea, MaxBearing34) == 0x34, "MatchThrottle2Idea::MaxBearing34 offset");
static_assert(offsetof(MatchThrottle2Idea, Flag38) == 0x38, "MatchThrottle2Idea::Flag38 offset");
static_assert(offsetof(MatchThrottle2Idea, Flag3d) == 0x3d, "MatchThrottle2Idea::Flag3d offset");
static_assert(offsetof(MatchThrottle2Idea, ClosingFloor40) == 0x40, "MatchThrottle2Idea::ClosingFloor40 offset");
static_assert(offsetof(MatchThrottle2Idea, Field48) == 0x48, "MatchThrottle2Idea::Field48 offset");
static_assert(offsetof(MatchThrottle2Idea, Speed4c) == 0x4c, "MatchThrottle2Idea::Speed4c offset");
static_assert(offsetof(MatchThrottle2Idea, ClosingWindow54) == 0x54, "MatchThrottle2Idea::ClosingWindow54 offset");
static_assert(offsetof(MatchThrottle2Idea, CatchUpRange58) == 0x58, "MatchThrottle2Idea::CatchUpRange58 offset");
static_assert(sizeof(MatchThrottle2Idea) == 0x5c, "MatchThrottle2Idea size (operator-new-free embed; next member at +0x5c in every owner)");

// ============================================================================
// FaceIdea : public BaseIdea -- Common.dll RTTI name ".?AVFaceIdea@@". Ctor is
// FUN_062d6750 (this marathon's "SubObjectB", 0xc0 bytes, across the SAME
// sibling headers as MoveIdea above). This is also the class the prior
// aisteer.h investigation called the "small-variant SteeringTask" -- see this
// header's own class-level comment for the identity correction.
// ============================================================================
// Full ctor raw disasm: Common.dll.txt 218090-218143 (53 instructions, no
// Ghidra decompile). Own-data span +0x14..+0xc0 (0xac bytes):
//   +0x14 BaseWatcher Watcher     -- same zeroing pattern as MoveIdea::Watcher.
//   +0x1c..+0x27                 -- untouched by the ctor, unconfirmed.
//   +0x28, +0x2c, +0x30 (3 dwords) -- zeroed.
//   +0x34..+0x57 (9 dwords, REP STOSD) -- zeroed.
//   +0x58..+0x63                 -- untouched by the ctor, unconfirmed.
//   +0x64 byte                   -- zeroed. (NOT a nested vtable -- confirmed
//                                    by direct byte-width: MoveIdea's own
//                                    ctor installs a vtable pointer at its
//                                    +0x64, but FaceIdea's ctor here only
//                                    zeroes a single byte at the SAME
//                                    relative offset -- the two classes' own
//                                    data past +0x14 diverge completely past
//                                    that point despite the coincidental
//                                    +0x64 offset match.)
//   +0x65..+0x8b                 -- untouched by the ctor, unconfirmed.
//   +0x8c float 0.098175 (0x3dc90fdb)
//   +0x90 float               -- FSTP result of FUN_062e5ba0(Object, 1.0f)
//                                 (item pending -- FUN_062e5ba0 itself not
//                                 independently ported in this pass).
//                                 FULLPORT-DEFERRED: left 0.0f here.
//   +0x94 float 1.0 (0x3f800000)
//   +0x98 float 0.61 (0x3f1c28f6)
//   +0x9c float 0.19634955 == pi/16 (0x3e490fdb)
//   +0xa0 dword                -- zeroed.
//   +0xa4..+0xa7                 -- untouched by the ctor, unconfirmed.
//   +0xa8 float PI (0x40490fdb)
//   +0xac..+0xbb                 -- untouched by the ctor, unconfirmed.
//   +0xbc float 0.976563 (0x3f7ae148)
// Total confirmed span 0x00..0xc0 == sizeof(FaceIdea) exactly.
//
// Own vtable slot 0 (SetContext / "Bind"): LAB_062d6820 (item 1602), fully
// hand-disassembled by the prior aisteer.h pass. Takes 3 pointer-shaped
// arguments (Object/Handle/Context, matching BaseIdea::SetContext's own
// signature exactly); rejects if Object or Handle is null. Stores Handle at
// +0x08, the 3rd arg at +0x0c (later read as a POINTER by Update(), not a
// float, per that item's own 2026-09-11 correction), Object at +0x04, sets
// Attached=1. FULLPORT-DEFERRED here: the aisteer.h investigation intentionally
// left it un-ported to a C++ body (see item 1602's own notes) pending a wider
// review of its 2 forwarding trampolines; not second-guessed in this pass.
//
// Own vtable slot 1 (Update): LAB_062d68b0 (item 1603), already ported as the
// free function SteeringTaskSmall_Update(void* Self, float Dt) in
// common_aisteer.h/.cpp (962 instructions hand-disassembled, status
// `partial` -- see that function's own extensive doc comment for the full
// control-flow writeup and remaining gaps). Wired directly below.
//
// Own vtable slot 3 (GetKind): LAB_062d74d0 -- `MOV AX,0x10; RET`. Returns
// the constant 0x10 (16).
//
// Own vtable slot 4 (Commit): LAB_062d74e0, Common.dll.txt 219257-219267 (11
// instructions). PORTED 2026-09-17 (claude:common-idea-internals). The doubt that
// stood here -- "FUN_062e8c20 is independently confirmed ... as
// Behavior_RefreshCachedPositionIfMoved but that resolution came from a DIFFERENT
// call shape (this->0x18 directly, not Handle+0x18 through a stored pointer
// field)" -- is settled, and settled by this wave's own new material rather than
// by assertion: DodgeIdea::Commit (LAB_062d8830) and BreakAwayIdea::Commit
// (LAB_062d93e0), both ported this pass, call FUN_062e8c20 with EXACTLY this
// shape (`MOV ECX,[ESI+8]; ADD ECX,0x18`), and DodgeIdea::Commit calls
// FUN_062e8ce0 and FUN_062e9120 on the same receiver in the same breath -- both
// of which common_behavior.h already documents as taking a "ComponentThis" that
// IS Handle+0x18. The two shapes are one shape.
// Body: a 12-byte stack Vector is zeroed (062d74ee-062d74fe) and passed as
// `Behavior_RefreshCachedPositionIfMoved(Handle + 0x18, &Zero)`. Nothing else.
//
// ============================================================================
// FaceIdea's NON-VIRTUAL setter family, and what it proves about the layout
// ============================================================================
// ADDED 2026-09-17 (claude:common-idea-internals). Seven consecutive one-line
// functions at .text:062d7510-062d769a, plus two strays at .text:062d93c0 /
// .text:062d9490, are FaceIdea's configuration setters. They were catalogued in
// decomp-tracker as items 1606-1612/1632 under three different wrong owners
// (two carried `ported_name = "FormationBehavior::SetOffsetVector"`); a lane
// working BuzzBehavior::Update re-attributed them to FaceIdea and recorded the
// handoff on item 1309. That re-attribution is CONFIRMED here, and not by the
// six offset matches alone -- those would be exactly the lesson-#44 mistake,
// since MoveIdea, MatchThrottle2Idea, DodgeIdea and BreakAwayIdea all have
// fields at several of the same offsets. What settles it is the enclosing
// constructor plus the receiver register at the call site:
//   BuzzBehavior::BuzzBehavior (FUN_062c5af0) does `LEA ECX,[ESI+0xa4]; CALL
//   FUN_062d6750` at .text:062c5b26-062c5b31, and GotoBehavior's own ctor puts
//   its FaceIdea at the same +0xa4 -- so in BOTH classes "+0xa4" is a FaceIdea
//   by construction, and GotoBehavior::Update then calls three of these setters
//   on exactly that receiver:
//     .text:062fdd8b  LEA ECX,[ESI+0xa4]   CALL FUN_062d75e0
//     .text:062fdd9d  LEA ECX,[ESI+0xa4]   CALL FUN_062d7560
//     .text:062fe7fd  LEA ECX,[EBX+0xa4]   CALL FUN_062d7510
//     .text:062fe32e  ECX = ESI+0xa4       CALL FUN_062d7610
//     .text:062fe34f  ECX = ESI+0xa4       CALL FUN_062d9490
// The mechanical corroboration is still worth stating, because it is unusually
// tight: every one of the seven writes the SAME tag field, `[ECX+0xa0]`, with a
// different small constant, and FaceIdea's ctor is the class whose own +0xa0 is
// ctor-zeroed. The tags run 1,2,3,4,5 in address order -- i.e. +0xa0 is
// FaceIdea's exact analogue of MoveIdea::TargetKind48, and the setters are one
// "how am I aiming" switch:
//   FUN_062d7510  tag 1 / 0   watch an object      (Watcher at +0x14)
//   FUN_062d7560  tag 2       face a fixed point   (Vector at +0x1c)
//   FUN_062d7590  tag 3       face a direction     (Vector at +0x28)
//   FUN_062d75c0  tag 4       face along a basis   (9 dwords at +0x34)
//   FUN_062d75e0  tag 5       face an axis         (Vector at +0x58)
//   FUN_062d7640  (no tag)    optional up-vector   (flag +0xad, Vector +0xb0)
//   FUN_062d7610  (no tag)    throttle scale       (+0x90 raw, +0x94 derived)
// The +0x34 one is decisive on its own: `MOV ECX,0x9; REP MOVSD` into
// `[ECX+0x34]` (.text:062d75d0-062d75d8) is NINE dwords, which is exactly the
// extent of this class's own ctor-zeroed `REP STOSD` block at +0x34 and exactly
// a 3x3 Matrix. Nothing else in this family has a 9-dword block there.
//
// That closes three of the four items handed over on tracker item 1309: +0x1c
// and +0x58 were inside the header's `Opaque1c`/`Opaque58` spans and +0xad/+0xb0
// inside `OpaqueAc`; all four are named fields below now, and Field28/Field2c/
// Field30 are one Vector.
//
// Two of the nine addresses are NOT safe to treat as FaceIdea-only, and are
// declared here with that stated rather than hidden:
//   .text:062d93c0 `MOV [ECX+0x8c],EAX` and .text:062d9490 `MOV [ECX+0x94],EAX`
// are single-instruction stores, and DodgeIdea's ctor writes +0x8c while
// BreakAwayIdea's writes +0x8c, +0x94 and +0x98 (see both classes at the end of
// this header). So those two addresses are prime ICF-fold candidates shared with
// their siblings: the GotoBehavior call site above proves the +0x94 store is
// FaceIdea's TurnThrottle *there*, and says nothing about the call sites at
// .text:062c84dd/062c84ee/062c84ff, which cluster inside one function and are
// far more likely BreakAwayIdea's. Each caller must prove its own receiver.
// ============================================================================
class FaceIdea : public BaseIdea
{
public:
    FaceIdea();

    // Own vtable slot 0, LAB_062d6820 (item 1602): BaseIdea's binding, then AngleWeight98 from
    // the object's angular speed / distance (FUN_062e5b20 / FUN_062e5950), a discarded
    // FUN_062e5b50 query, AngleWeight9c = pi/16 and bUpValid = 0.
    bool SetContext(void* InObject, void* InHandle, void* InContext) override;

    // Own vtable slot 1, LAB_062d68b0 (item 1603, PTR_LAB_063a04ac slot 1). Defined in
    // common_faceidea.cpp; the steering helpers it shares with DodgeIdea::Update are in
    // common_aisteer.cpp.
    int Update(float DeltaTime) override;

    // Own vtable slot 3. Always returns the constant 0x10 (16).
    short GetKind() const override { return 0x10; }

    // Own vtable slot 4 (LAB_062d74e0). Ported in full 2026-09-17 -- see the
    // class-level comment's slot-4 section.
    void Commit() override;

    // ---- Non-virtual setters, all called by raw address in retail. See the
    // ---- "FaceIdea's NON-VIRTUAL setter family" block above for the receiver
    // ---- proofs and the tag table. Every one of them is RET 4. ----

    // FUN_062d7510 (item 1606), Common.dll.txt 219277-219297, 10 call sites.
    // TargetKind = 1 and Watcher.set(Object + 8) when Object is non-null, else
    // TargetKind = 0 and Watcher.set(nullptr) -- structurally the same setter as
    // MoveIdea::SetWatchObject minus that class's slot-array mirroring.
    void SetWatchObject(void const* Object);

    // FUN_062d7560 (item 1607), Common.dll.txt 219311-219320, 15 call sites.
    // TargetKind = 2, Position1c = Point (three plain dword copies).
    void SetPosition(Vector const& Point);

    // FUN_062d7590 (item 1608), Common.dll.txt 219340-219349, 5 call sites.
    // TargetKind = 3, Direction28 = Dir. Retail does NOT renormalize here (unlike
    // MoveIdea::SetDirection) -- the three dword copies are the whole body.
    void SetDirection(Vector const& Dir);

    // FUN_062d75c0 (item 1609), Common.dll.txt 219369-219378, 8 call sites.
    // TargetKind = 4, then `MOV ECX,0x9; REP MOVSD` from Basis into ZeroBlock34.
    // Nine dwords == a 3x3 Matrix; see the class comment.
    void SetBasis(Matrix const& Basis);

    // FUN_062d75e0 (item 1610), Common.dll.txt 219388-219397, 9 call sites.
    // TargetKind = 5, Vector58 = Axis.
    void SetAxis(Vector const& Axis);

    // FUN_062d7610 (item 1611), Common.dll.txt 219416-219428, 2 call sites.
    // Field90 = Value (raw dword) and then TurnThrottle =
    // Object->get_projected_axis_throttle_XY(Value), via FUN_062e5ba0.
    // NOTE the asymmetry with this class's own constructor, which is real and
    // reproduced rather than smoothed over: the ctor stores the DERIVED value in
    // +0x90 (`CALL FUN_062e5ba0; FSTP [ESI+0x90]`, .text:062d67df-062d67eb, which
    // yields 0.0f because Object is still null there) and the literal 1.0f in
    // +0x94, i.e. exactly the opposite assignment from this setter.
    void SetThrottleScale(float Value);

    // FUN_062d7640 (item 1612), Common.dll.txt 219445-219472, 4 call sites. An
    // all-or-nothing optional vector: if every component of Up compares EQUAL to
    // 0.0f then bUpValid = 0 and nothing else is written; otherwise bUpValid = 1
    // and UpB0 = Up. The three gates are `FCOMP DAT_06399408 (0.0f); TEST AH,0x44;
    // JP` -- MSVC's EQUALITY idiom, the same one item 1309 had to correct a
    // `< 0.0f` misreading of, so the branch really is "is this component zero".
    void SetUpVector(Vector const& Up);

    // .text:062d93c0 (item 1632), 4 call sites. `MOV [ECX+0x8c],EAX; RET 4` --
    // FacingTolerance = Value. POSSIBLE ICF FOLD: DodgeIdea and BreakAwayIdea both
    // have a +0x8c their own ctors write. See the class comment's last paragraph.
    void SetAngleWeight8c(float Value);

    // .text:062d9490, 11 call sites. `MOV [ECX+0x94],EAX; RET 4` --
    // TurnThrottle = Value. Receiver proved FaceIdea at .text:062fe34f
    // (GotoBehavior's this+0xa4, `PUSH 0x3f800000`). SAME possible-fold caveat.
    void SetScale94(float Value);

    // ---- Confirmed own-data fields, ascending offset order. Opaque* spans
    // ---- are bytes nothing in this port writes yet (unconfirmed, not guessed).
    // ----
    // RENAMED/RETYPED 2026-09-17 (claude:common-idea-internals), old -> new:
    //   Opaque1c (6 bytes)      -> Position1c (one Vector, FUN_062d7560)
    //   Field28/Field2c/Field30 -> Direction28 (one Vector, FUN_062d7590)
    //   Opaque58 (12 bytes)     -> Vector58 (one Vector, FUN_062d75e0)
    //   OpaqueAc (16 bytes)     -> PadAc / bUpValid / PadAe / UpB0 (FUN_062d7640)
    // RENAMED 2026-10-15 (claude-code:cmn-aif2), from Update()'s own reads:
    //   FieldA0 -> TargetKind        ScaleDefault1_94 -> TurnThrottle
    //   AngleDefaultPiA8 -> RemainingAngle   AngleWeight8c -> FacingTolerance
    // Field64 / Field90 stay unnamed (see their declarations).
    // `ZeroBlock34` keeps its name (eight external users) despite now
    // being known to be a 3x3 Matrix.
    BaseWatcher Watcher;             // +0x14, ends +0x1c.
    Vector Position1c;                 // +0x1c, ends +0x28. SetPosition(), tag 2.
    Vector Direction28;                  // +0x28, ends +0x34. Ctor-zeroed (three
                                          // dwords). SetDirection(), tag 3.
    int ZeroBlock34[9];                    // +0x34..+0x57 (9 dwords). Ctor-zeroed via
                                            // REP STOSD; a 3x3 Matrix written whole by
                                            // SetBasis(), tag 4. Kept as int[9] rather
                                            // than a Matrix so the eight existing
                                            // external users still compile.
    Vector Vector58;                          // +0x58, ends +0x64. SetAxis(), tag 5.
    // +0x64. Ctor-zeroed byte. Update() reads it twice: case 1 aims along the
    // watched node's up only when it is set (062d6959), case 2 takes the floats at
    // +0x6c/+0x78/+0x84 as the up hint when it is set (062d6c53). No writer other
    // than the ctor was found, so the meaning stays open.
    unsigned char Field64;
    unsigned char Opaque65[0x8c - 0x65];          // +0x65, unconfirmed.
    // +0x8c, formerly AngleWeight8c. Ctor default 0.098175f (0x3dc90fdb);
    // SetAngleWeight8c(). Update() returns 1 (facing reached) when RemainingAngle is
    // below it, 0 otherwise (062d7483-062d7499).
    float FacingTolerance;
    float Field90;                                         // +0x90. Ctor: FSTP of
                                                            // FUN_062e5ba0(Object, 1.0f)
                                                            // (0.0f, Object is null);
                                                            // SetThrottleScale() stores its
                                                            // raw argument. Never read by
                                                            // Update (062d68b0-062d74a9).
    // +0x94, formerly ScaleDefault1_94. Ctor default 1.0f; SetScale94(), and
    // SetThrottleScale() stores get_projected_axis_throttle_XY into it. Update() caps
    // the length of the axis-throttle vector it hands to ThrustManager::
    // SetAxisThrottle at this value (062d7385-062d7406).
    float TurnThrottle;
    float AngleWeight98;                                       // +0x98. Ctor default 0.61f
                                                                // (0x3f1c28f6).
    float AngleWeight9c;                                         // +0x9c. Ctor default
                                                                  // 0.19634955f == pi/16 (0x3e490fdb).
    // +0xa0, formerly FieldA0. Ctor-zeroed. The 0..5 aim-mode tag every setter
    // above stamps and Update() switches on (062d68db) -- FaceIdea's analogue of
    // MoveIdea::TargetKind48.
    int TargetKind;
    unsigned char OpaqueA4[0xa8 - 0xa4];                             // +0xa4, unconfirmed.
    // +0xa8, formerly AngleDefaultPiA8. Ctor default PI (0x40490fdb). Update() passes
    // its address as TurnHelper::get_angular_throttle's OutAngle (062d7083), i.e. the
    // rotation angle still between the current orientation and the target frame.
    float RemainingAngle;
    unsigned char PadAc;                                                 // +0xac, unconfirmed.
    unsigned char bUpValid;                                               // +0xad. SetUpVector()
                                                                           // writes 1 when Up is
                                                                           // any non-zero vector,
                                                                           // 0 otherwise. NOT
                                                                           // ctor-initialized in
                                                                           // retail.
    unsigned char PadAe[0xb0 - 0xae];                                       // +0xae, alignment.
    Vector UpB0;                                                              // +0xb0, ends +0xbc.
                                                                               // SetUpVector(), only
                                                                               // written when
                                                                               // bUpValid is set.
    float ThresholdBc;                                                     // +0xbc. Ctor
                                                                            // default 0.976563f
                                                                            // (0x3f7ae148).
};
static_assert(offsetof(FaceIdea, Watcher) == 0x14, "FaceIdea::Watcher offset");
static_assert(offsetof(FaceIdea, Position1c) == 0x1c, "FaceIdea::Position1c offset");
static_assert(offsetof(FaceIdea, Direction28) == 0x28, "FaceIdea::Direction28 offset");
static_assert(offsetof(FaceIdea, ZeroBlock34) == 0x34, "FaceIdea::ZeroBlock34 offset");
static_assert(offsetof(FaceIdea, Vector58) == 0x58, "FaceIdea::Vector58 offset");
static_assert(offsetof(FaceIdea, Field64) == 0x64, "FaceIdea::Field64 offset");
static_assert(offsetof(FaceIdea, FacingTolerance) == 0x8c, "FaceIdea::FacingTolerance offset");
static_assert(offsetof(FaceIdea, Field90) == 0x90, "FaceIdea::Field90 offset");
static_assert(offsetof(FaceIdea, TurnThrottle) == 0x94, "FaceIdea::TurnThrottle offset");
static_assert(offsetof(FaceIdea, AngleWeight98) == 0x98, "FaceIdea::AngleWeight98 offset");
static_assert(offsetof(FaceIdea, AngleWeight9c) == 0x9c, "FaceIdea::AngleWeight9c offset");
static_assert(offsetof(FaceIdea, TargetKind) == 0xa0, "FaceIdea::TargetKind offset");
static_assert(offsetof(FaceIdea, RemainingAngle) == 0xa8, "FaceIdea::RemainingAngle offset");
static_assert(offsetof(FaceIdea, bUpValid) == 0xad, "FaceIdea::bUpValid offset");
static_assert(offsetof(FaceIdea, UpB0) == 0xb0, "FaceIdea::UpB0 offset");
static_assert(offsetof(FaceIdea, ThresholdBc) == 0xbc, "FaceIdea::ThresholdBc offset");
static_assert(sizeof(FaceIdea) == 0xc0, "FaceIdea size (matches every sibling header's confirmed SubObjectB size)");

// ============================================================================
// SplineMoveIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVSplineMoveIdea@@". Ctor is FUN_062d9bf0 (decomp-tracker item 1647).
// Identified 2026-09-17, ABI pass 50 (claude:abi-pass50) -- previously the
// nameless "composite SteeringTask" of common_aisteer.h and the opaque
// `unsigned char SubObjectC[0x174]` of common_gotobehavior.h.
// ============================================================================
// RTTI walk (raw bytes, EXE/Disasm/Common.dll.txt -- no Ghidra decompile for
// most of this cluster), identical method to MoveIdea/FaceIdea above:
//   FUN_062d9bf0 installs vtable .rdata:PTR_FUN_063a0568 at object+0
//   (Common.dll.txt:222953, `.text:062d9c46 MOV dword ptr [ESI],
//   .rdata:PTR_FUN_063a0568`). The COL pointer at (vtable-4) == .rdata:
//   0x063a0564 -> DAT_063a9c18, whose +0x0c pTypeDescriptor
//   (.rdata:063a9c24) is `.data:SplineMoveIdea::RTTI_Type_Descriptor`.
//   => SplineMoveIdea : public BaseIdea, same single-inheritance shape as
//   every other confirmed sibling.
// Vtable extent: 063a0568..063a057c inclusive, SIX entries, ending where the
// next sibling's own COL pointer (063a0580 -> DAT_063a9c60, AfterburnerIdea)
// begins -- the same 6-slot BaseIdea shape, no destructor slot:
//   slot 0 (0x00) FUN_062d9c80 RET 0xc  SetContext(Object, Handle, Context)
//   slot 1 (0x04) LAB_062d9d00 RET 0x4  Update(float)
//   slot 2 (0x08) MetaList<GoodDesc>::operator[]      ICF fold, RESERVED
//   slot 3 (0x0c) LAB_062da300 RET      `MOV AX,0x16; RET` -> GetKind() == 0x16
//   slot 4 (0x10) LAB_062da310 RET      Commit()
//   slot 5 (0x14) CEquip::GetConnectionPosition        ICF fold, RESERVED
//
// Layout, from FUN_062d9bf0's own 33 instructions (Common.dll.txt 222923-
// 222962) in source order:
//   [ESI]          = PTR_LAB_063a0410 (BaseIdea's own vtable -- the base
//                    sub-object ctor, re-stamped at the end of the body)
//   [ESI+4]/[+8]   = 0, [ESI+0x10] byte = 0        -- BaseIdea's own span
//   ECX = ESI+0x14; CALL FUN_062d4e60              -- MoveIdea Move  @ +0x14
//   ECX = ESI+0x8c; CALL FUN_062d6750              -- FaceIdea Face  @ +0x8c
//   [ESI+0x150] byte = 0
//   [ESI+0x158]      = 0
//   [ESI]          = PTR_FUN_063a0568              -- final vtable install
//   [ESI+0x168]      = 0x41200000 (10.0f)
//   [ESI+0x16c]      = 0x42480000 (50.0f)
//   [ESI+0x154]      = 0x3f800000 (1.0f)
// sizeof == 0x174, confirmed TWICE independently from two different embedders,
// each time as an exact gap (not a lower bound):
//   * GotoBehavior's own ctor FUN_062fd620 puts it at +0x164 and the next
//     field it writes is +0x2d8 -- 0x164 + 0x174 == 0x2d8 exactly.
//   * FUN_062c2e60 puts it at +0x168 and constructs the next member
//     (AfterburnerIdea, FUN_062da500) at +0x2dc -- 0x168 + 0x174 == 0x2dc.
// A THIRD, independent structural confirmation of the two embedded members:
// GotoBehavior's own destructor (FUN_062fd7a0) unwatches BaseWatchers at
// this+0x204 and this+0x18c, i.e. SubObjectC+0xa0 and SubObjectC+0x28 --
// exactly Face.Watcher (0x8c+0x14) and Move.Watcher (0x14+0x14).
//
// The +0x1c/+0x20/+0x48/+0x4c/+0x54/+0x58/+0x5c offsets Update() writes are the embedded
// MoveIdea's (this+0x14+N): Move.Handle / Context / Mode / MaxThrustBearing (restored to
// its ctor default pi) / Throttle / StopStyle / TargetKind48.
// ============================================================================
class SplineMoveIdea : public BaseIdea
{
public:
    SplineMoveIdea();

    // Own vtable slot 0 -- FUN_062d9c80 (item 1648), Common.dll.txt
    // 222971-223025, RET 0xc. Fully ported below.
    bool SetContext(void* InObject, void* InHandle, void* InContext) override;

    // Own vtable slot 1 -- LAB_062d9d00 (item 1649), RET 0x4 (formerly the free
    // UpdateSplineSteeringTask). Follows the spline: steers the embedded Move to its P2
    // and the embedded Face to the far point (or the P2->P3 chord), and commits itself
    // once the end is reached. 3 = not ready, 0 = steering, 1 = done.
    int Update(float DeltaTime) override;

    // Own vtable slot 3 -- LAB_062da300, `MOV AX,0x16; RET`. Returns 0x16 (22).
    short GetKind() const override { return 0x16; }

    // FUN_062da3f0 (.text:062da3f0-062da3f6): `FLD float ptr [ECX+0x134]; RET` -- the
    // embedded FaceIdea's RemainingAngle (+0x8c + 0xa8). Only caller: DockBehavior's
    // approach tail at .text:062c4de2 on its SplineMoveIdea (+0x168).
    float GetFaceAngle() const { return Face.RemainingAngle; }

    // Own vtable slot 4 -- LAB_062da310, bare `RET`. Fully ported below as of
    // 2026-09-17 (its last deferred call, FUN_062e8ce0, turned out to be the
    // already-real Behavior_SyncPredictedPosition).
    void Commit() override;

    // Non-virtual, folded at .text:062da4d0 -- whose other (real, correctly-
    // named) owner is IBehaviorManager::set_camera_mode, hence Ghidra's label at
    // the call site. The body is `MOV [ECX+0x154],EAX; RET 4`, and +0x154 on
    // THIS class is BankBlendCap. Added 2026-09-17
    // (claude:common-idea-methods): the one observed call site,
    // .text:062fe148 inside GotoBehavior::Update, has ECX = this+0x164, which
    // GotoBehavior's constructor FUN_062fd620 initialises with FUN_062d9bf0
    // (this class's ctor) at .text:062fd667 -- so the receiver is proved the same
    // way MoveIdea's setters are, from the enclosing constructor's LEA.
    // NOTE the pointer is NOT an IBehaviorManager: GotoBehavior's real
    // IBehaviorManager lives at this+0x20 (common_iface.h).
    void SetBankBlendCap(float Value);

    // FUN_062da4e0 (item 1656), .text:062da4e0-062da4fd, 12 insns, RET 4. The
    // DERIVED sibling of SetBankBlendCap, the same way MoveIdea::SetThrottleScale
    // is SetScale40's: BankBlendCap = get_projected_throttle(Value,
    // ThrustEquipEngine) through Behavior_InvokeManagerSlotD4 on BaseIdea::Object.
    // Unlike MoveIdea's version there is no Mode ladder -- the thrust type is the
    // literal 1 at .text:062da4ea (`PUSH 0x1`).
    //
    // Previously carried only as an inline at GotoBehavior::Update's call site
    // (.text:062fe190) because common_idea.h belonged to another lane at the time;
    // the second call site, DockBehavior::AdvancePreamble at .text:062c48e6, made
    // the duplication worth retiring.
    void SetThrottleScale(float Value);

    // FUN_062da410, RET 4, Common.dll.txt 223630-223691 (~40 instructions).
    // Sole call site .text:062fe74b, on GotoBehavior's this+0x164. Ported
    // 2026-09-17 -- see the .cpp, where it collapses to two lines and, in doing
    // so, independently CONFIRMS that FUN_062d5980 is MoveIdea::SetMode: the
    // whole 40-instruction branch tree is byte-for-byte the SetMode transition
    // table operating on `Move` (its `[ESI+0x48]` is Move.Mode at 0x14+0x34 and
    // its `[ESI+0x1c]` is Move.Handle at 0x14+0x08), i.e. retail's source said
    // `Move.SetMode(...)` here and the compiler inlined it. The byte is stored raw
    // (`MOV AL,[ESP+4]; MOV [ESI+0x150],AL`); only the branch tests it (TEST AL,AL).
    void SetWideMode(unsigned char Value);

    // FUN_062da400 (item 1654), RET 4 -- `MOV [ECX+0x158],EAX`, i.e.
    // Field158 = Value. Two call sites, BOTH now receiver-proved to be this
    // class, so the "may be another fold" caveat that stood here is retired
    // (2026-09-17, claude:w21-stubsweep):
    //   .text:062fe89f  `LEA ECX,[EBX+0x164]` -- GotoBehavior's SplineMoveIdea,
    //                   the member its ctor FUN_062fd620 builds with FUN_062d9bf0
    //                   at .text:062fd667. Argument 0 or 2.
    //   .text:062c435e  `LEA ECX,[ESI+0x168]` at .text:062c4358 -- DockBehavior's
    //                   SubObjectC, which its ctor FUN_062c2e60 builds with
    //                   FUN_062d9bf0 at .text:062c2ea6. That receiver was the
    //                   "different object whose class this port has not
    //                   identified"; it was identified as SplineMoveIdea on
    //                   2026-09-17 and common_dockbehavior.h declares it so.
    // The stored value is an INTEGER literal (0 or 2) at both sites, which is
    // why Field158 is an int below rather than the float it used to be typed as
    // -- 0x00000002 read as a float is a denormal, not 2.0f.
    void SetField158(int Value);

    // FUN_062da380 (item 1652), RET 4, Common.dll.txt 223563-223589. Receiver
    // proved this class at .text:062fe73a (GotoBehavior's this+0x164, the member
    // its own ctor builds with FUN_062d9bf0); second call site .text:062c5a62.
    // PORTED IN FULL 2026-09-17 (claude:common-idea-internals). The blocker that
    // stood in this comment -- "only FUN_062e5ab0 is genuinely unknown ... whose
    // threshold constant .rdata:DAT_0639e6c8 this pass did not decode" -- was
    // already stale when it was written and did not survive a re-read of the
    // tracker: FUN_062e5ab0 is Behavior_QueryFixedFalloffEffect
    // (common_behavior.h, item 1761, closed 2026-09-11), and DAT_0639e6c8 is
    // decoded there as the family's 0.001f epsilon. Nothing was unknown.
    //
    // Retail's body, in this class's own field names (its `[ESI+0x50]` is
    // 0x14+0x3c == Move.ArrivalRange, its `[ESI+0x58]` is 0x14+0x44 ==
    // Move.StopStyle and its `[ESI+0x6d]` is 0x14+0x59 == Move.bKillEngineOffBearing):
    //   Move.ArrivalRange          = ExtrapolateBase;       // 062da387/062da397
    //   Spline                     = NewSpline;             // 062da39b
    //   Move.StopStyle             = 2;                     // 062da3a1
    //   Move.bKillEngineOffBearing = 0;                     // 062da3a8
    //   MaxSpeed15c  = Behavior_InvokeManagerSlotD8(Object, 1.0f, 1);  // 062da3ac
    //   LookaheadScale = Behavior_QueryFixedFalloffEffect(Object, MaxSpeed15c);
    //   SpeedNorm    = Spline->get_spline_length();         // 062da3d8
    // The middle pair is a genuine chain: retail FSTs the first result to a stack
    // slot at 062da3b1, reloads it as a raw dword at 062da3b5 and pushes THAT as
    // the second call's argument, so the two are not independent queries.
    void SetSpline(ICRSplineSegment* NewSpline);

    // ---- Confirmed own-data fields, ascending offset order. ----
    MoveIdea Move;               // +0x14, ends +0x8c. Embedded by value.
    FaceIdea Face;               // +0x8c, ends +0x14c. Embedded by value.
    ICRSplineSegment* Spline;    // +0x14c. NOT ctor-initialized in retail;
                                  // SetContext() nulls it. Type from
                                  // common_aisteer.h / item 1652.
    unsigned char WideMode;        // +0x150. Ctor-zeroed byte.
    unsigned char Pad151[0x154 - 0x151];
    float BankBlendCap;             // +0x154. Ctor default 1.0f.
    int Field158;                    // +0x158. Ctor-zeroed. RETYPED float -> int
                                      // 2026-09-17 (claude:w21-stubsweep): both
                                      // SetField158() call sites push an integer
                                      // literal (0 / 2), never a float bit
                                      // pattern, and nothing reads it as a float.
    float MaxSpeed15c;               // +0x15c. NOT ctor-initialized. RETYPED
                                      // 2026-09-17 from `unsigned char
                                      // Opaque15c[4]`: SetSpline() FSTPs
                                      // Behavior_InvokeManagerSlotD8(Object, 1.0f,
                                      // ThrustEquipEngine) into it
                                      // (.text:062da3b9), i.e. it caches
                                      // IObjInspect::get_speed at full engine
                                      // throttle -- the same quantity
                                      // MoveIdea::MaxSpeed5c holds.
    float LookaheadScale;           // +0x160. NOT ctor-initialized. Filled by
                                     // SetSpline() from
                                     // Behavior_QueryFixedFalloffEffect(Object,
                                     // MaxSpeed15c), i.e. get_distance_travelled --
                                     // a coast distance, not a scale. Name kept
                                     // (eleven external users) despite the
                                     // mismatch; TODO: rename with its users.
    float SpeedNorm;                 // +0x164. NOT ctor-initialized. Filled by
                                      // SetSpline() from
                                      // ICRSplineSegment::get_spline_length(), i.e.
                                      // it is the spline's arc length. Name kept
                                      // (nine external users); TODO: rename to
                                      // SplineLength with its users.
    float StepNumerator;              // +0x168. Ctor default 10.0f.
    float ExtrapolateBase;             // +0x16c. Ctor default 50.0f.
    float PersistedT;                   // +0x170. NOT ctor-initialized;
                                         // SetContext()/Commit() both zero it.
};
static_assert(offsetof(SplineMoveIdea, Move) == 0x14, "SplineMoveIdea::Move offset");
static_assert(offsetof(SplineMoveIdea, Face) == 0x8c, "SplineMoveIdea::Face offset");
static_assert(offsetof(SplineMoveIdea, Spline) == 0x14c, "SplineMoveIdea::Spline offset");
static_assert(offsetof(SplineMoveIdea, WideMode) == 0x150, "SplineMoveIdea::WideMode offset");
static_assert(offsetof(SplineMoveIdea, BankBlendCap) == 0x154, "SplineMoveIdea::BankBlendCap offset");
static_assert(offsetof(SplineMoveIdea, Field158) == 0x158, "SplineMoveIdea::Field158 offset");
static_assert(offsetof(SplineMoveIdea, MaxSpeed15c) == 0x15c, "SplineMoveIdea::MaxSpeed15c offset");
static_assert(offsetof(SplineMoveIdea, LookaheadScale) == 0x160, "SplineMoveIdea::LookaheadScale offset");
static_assert(offsetof(SplineMoveIdea, SpeedNorm) == 0x164, "SplineMoveIdea::SpeedNorm offset");
static_assert(offsetof(SplineMoveIdea, StepNumerator) == 0x168, "SplineMoveIdea::StepNumerator offset");
static_assert(offsetof(SplineMoveIdea, ExtrapolateBase) == 0x16c, "SplineMoveIdea::ExtrapolateBase offset");
static_assert(offsetof(SplineMoveIdea, PersistedT) == 0x170, "SplineMoveIdea::PersistedT offset");
static_assert(sizeof(SplineMoveIdea) == 0x174, "SplineMoveIdea size (0x164+0x174==0x2d8 in GotoBehavior, 0x168+0x174==0x2dc in FUN_062c2e60)");

// ============================================================================
// DirectionPicker -- the SECOND base of DodgeIdea and BreakAwayIdea. Published
// 2026-09-17 (claude:common-idea-internals).
// ============================================================================
// This header's own opening comment has recorded that "DodgeIdea/BreakAwayIdea
// additionally multiply-inherit a second base, DirectionPicker (mdisp=0x14) --
// not modeled here, out of scope for this pass". It is modeled now, and its
// layout came out of the two derived constructors rather than out of RTTI alone:
// the mdisp only says WHERE the sub-object starts, not how big it is.
//
// FUN_062d77d0 (DodgeIdea) and FUN_062d8970 (BreakAwayIdea) open with
// BYTE-IDENTICAL code for the region +0x14..+0x33, allowing for the one constant
// that differs:
//   [this+0x14] = 0                             062d780c / 062d89aa
//   [this+0x18] = [this+0x1c] = [this+0x20]
//             = [this+0x24] = 1.0f              062d7800-062d7806 / 062d899e-062d89a7
//   LEA ECX,[this+0x28]; two dwords zeroed;
//   then the `CMP [ECX],0` + BaseWatcher::
//   set_pointer(null) pattern every BaseWatcher
//   member in this family uses                  062d77fd-062d7860 / 062d899b-062d89c9
//   [this+0x30] byte = 0 (Dodge) / 1 (BreakAway) 062d7877 / 062d8a0e
// and then diverge completely: DodgeIdea's next write is +0x34 = 5.0f, and
// BreakAwayIdea's next write is +0x34 = 0. So the shared region ends at +0x34 and
// sizeof(DirectionPicker) == 0x20 exactly -- confirmed from two independent
// derived classes rather than assumed from one.
//
// No vtable is installed at +0x14 by either constructor (both write a plain zero
// there), so DirectionPicker has NO virtual functions of its own -- which is also
// why neither derived class needs a second vtable pointer. Modeled here as a
// plain `F`-prefixed struct MEMBER, not as a real second base class: making it a
// base would hand the layout to the compiler, and this port's rule is that
// offsets are pinned by static_assert rather than trusted to the ABI.
// TODO(DirectionPicker): the four 1.0f weights at rel +0x04..+0x10 and the dword
// at rel +0x00 have no recovered meaning -- offset names only.
//
// RTTI CONFIRMATION 2026-09-17 (claude:common-steering-close), obtained while
// settling DodgeIdea::Update's receiver: the name and the offset are now proved,
// not inferred. DodgeIdea's ClassHierarchyDescriptor (.rdata:063a9ae4) has
// numBaseClasses == 3 and BaseClassArray 063a9ad4 ==
// [DodgeIdea, BaseIdea, 063a9aa4], where 063a9aa4 is a BaseClassDescriptor whose
// pTypeDescriptor is .data:DirectionPicker::RTTI_Type_Descriptor and whose
// PMD.mdisp (063a9aac) is 0x14. So retail really does spell it `DirectionPicker`
// and really does place it at +0x14, as a genuine (non-virtual, single-
// inheritance-slot) BASE. The decision above to model it as a member stands --
// it changes nothing observable and keeps the offsets pinned -- but item 1616's
// note calling it an "embedded sub-object" constructed by a
// `LEA ECX,[ESI+0x14]; CALL` pair is wrong on the mechanism: DodgeIdea's ctor
// contains no such pair (its only one is `LEA ECX,[ESI+0x28]` for the
// BaseWatcher at +0x28), it initialises this region inline, exactly as modelled
// here. FaceIdea's own CHD (063a9a80, numBaseClasses == 2) does NOT list
// DirectionPicker.
// ============================================================================
struct FDirectionPicker
{
    // Retail has no separate function for this: both derived constructors inline
    // the same +0x14..+0x33 prologue. Factored out here so the two derived ctors
    // below read the way retail's source must have, and so the four 1.0f defaults
    // live in exactly one place.
    FDirectionPicker();

    // FUN_062d88e0 (Common.dll.txt .text:062d88e0-062d8904, RET 4). Retail's receiver is
    // the enclosing Idea (DodgeIdea at EvadeBehavior+0x30, BreakAwayIdea at +0x110 --
    // .text:062c8295/062c82b1), and it touches only the Idea's +0x28 and +0x30, i.e. this
    // picker's Watcher (rel +0x14) and Flag1c (rel +0x1c): Watcher.set(Object ? Object+8 :
    // null) with set()'s own dedup, then Flag1c = 1. Declared on the picker because both
    // receiving Idea classes hold it at +0x14 and the body reads nothing else.
    void SetWatchObject(void const* Object);

    // FUN_062d76b0 (.text:062d76b0-062d76cc, RET 0x10): the four weights, in argument
    // order. Called on <Idea>+0x14 (.text:062c840d `LEA ECX,[ESI+0x44]`, 062c8523
    // `LEA ECX,[ESI+0x124]`), so the receiver really is this sub-object. Every caller passes
    // the four <Block>DirectionWeight{Left,Right,Up,Down} keys in that order (EvadeDodge /
    // EvadeBreak at .text:062c83ee-062c840c / 062c8504-062c8522, BuzzDodge / BuzzBreak in
    // BuzzBehavior::RecomputeApproach), which names the four weights.
    void SetWeights(float Left, float Right, float Up, float Down);

    // rel +0x00, formerly Field00. Ctor-zeroed. GenericWeightedPick (FUN_062d76d0) zeroes the
    // weight of the direction stored here (1 Left, 2 Right, 3 Up, 4 Down), draws a new one and
    // stores it back -- so it is the last picked direction, never picked twice running.
    int LastDirection;
    float WeightLeft;          // rel +0x04, formerly Weight04. Ctor default 1.0f.
    float WeightRight;          // rel +0x08, formerly Weight08. Ctor default 1.0f.
    float WeightUp;             // rel +0x0c, formerly Weight0c. Ctor default 1.0f.
    float WeightDown;           // rel +0x10, formerly Weight10. Ctor default 1.0f.
    BaseWatcher Watcher;        // rel +0x14, ends rel +0x1c.
    unsigned char Flag1c;       // rel +0x1c. DodgeIdea's ctor writes 0,
                                 // BreakAwayIdea's writes 1.
    unsigned char Pad1d[3];     // rel +0x1d, alignment only.
};
static_assert(offsetof(FDirectionPicker, WeightLeft) == 0x04, "FDirectionPicker::WeightLeft offset");
static_assert(offsetof(FDirectionPicker, WeightDown) == 0x10, "FDirectionPicker::WeightDown offset");
static_assert(offsetof(FDirectionPicker, Watcher) == 0x14, "FDirectionPicker::Watcher offset");
static_assert(offsetof(FDirectionPicker, Flag1c) == 0x1c, "FDirectionPicker::Flag1c offset");
static_assert(sizeof(FDirectionPicker) == 0x20, "FDirectionPicker size (both derived ctors' shared region is +0x14..+0x34)");

// ============================================================================
// DodgeIdea : public BaseIdea (+ DirectionPicker at +0x14) -- Common.dll RTTI
// name ".?AVDodgeIdea@@". Ctor FUN_062d77d0 (decomp-tracker item 1616).
// Published 2026-09-17 (claude:common-idea-internals).
// ============================================================================
// THIS IS common_buzzbehavior.h's "WatcherD". That header has carried the right
// vtable address since 2026-09-11 ("WatcherD (+0x178, size 0xe0) <-> PTR_LAB_
// 063a04c8, ctor FUN_062d77d0") but kept the member as an opaque
// `unsigned char WatcherD[0xe0]` because no class name was attached to that
// vtable. This header's own RTTI walk had meanwhile decoded .rdata:063a04c8's COL
// (063a9af4) as DodgeIdea and recorded it in the sibling table at the top of this
// file -- the two facts were simply never put together. FUN_062d77d0's single
// `MOV dword ptr [ESI],.rdata:PTR_LAB_063a04c8` at .text:062d7819 is the link.
//
// sizeof == 0xe0, pinned from BOTH sides:
//   * upper bound: BuzzBehavior's own ctor does `LEA ECX,[ESI+0x178]; CALL
//     FUN_062d77d0` at .text:062c5b46-062c5b4c and then `LEA ECX,[ESI+0x258];
//     CALL FUN_062d8970` -- a 0xe0 gap.
//   * lower bound: this ctor's own highest write is the dword at +0xdc
//     (.text:062d78f5), so the class cannot be smaller than 0xe0.
//
// Vtable .rdata:063a04c8, the same six-slot BaseIdea shape:
//   slot 0 (0x00) LAB_062d7920  BaseIdea::SetContext, inherited unchanged
//   slot 1 (0x04) LAB_062d7950  Update -- decomp-tracker item 1618, the
//                               1163-instruction body, defined as a member in
//                               common_dodgeidea.cpp (2026-10-08; it was the
//                               raw-offset free function SteeringTaskLarge_Update).
//   slot 2 (0x08) MetaList<GoodDesc>::operator[]   ICF fold, RESERVED
//   slot 3 (0x0c) LAB_062d9be0  `MOV AX,0x12; RET` -> GetKind() == 0x12
//   slot 4 (0x10) LAB_062d8830  Commit, ported in the .cpp
//   slot 5 (0x14) CEquip::GetConnectionPosition    ICF fold, RESERVED
// ============================================================================
class DodgeIdea : public BaseIdea
{
public:
    DodgeIdea();

    // Own vtable slot 1 -- LAB_062d7950 (item 1618), defined in common_dodgeidea.cpp.
    int Update(float DeltaTime) override;

    // Own vtable slot 3 -- LAB_062d9be0, `MOV AX,0x12; RET`.
    short GetKind() const override { return 0x12; }

    // Own vtable slot 4 -- LAB_062d8830, .text:062d8830-062d88a8. Ported in full.
    void Commit() override;

    // FUN_062d88c0 (.text:062d88c0-062d88dd, RET 4): copies the three dwords of *Value
    // into +0x58..+0x60, then `MOV byte [ECX+0x64],1`. Receivers: EvadeBehavior+0x30
    // (.text:062c85c9) and BuzzBehavior's DodgeIdea.
    void SetField58(Vector const& Value);

    // ---- Confirmed own-data fields, ascending offset order. Every constant is
    // ---- the raw IEEE-754 dword the ctor stores, decoded from .text bytes. ----
    FDirectionPicker Picker;         // +0x14, ends +0x34. The second base; see
                                      // FDirectionPicker above. Flag1c gets 0.
    // Field names from +0x34 on follow the personality keys both owners copy in, raw dword
    // for raw dword: EvadeBehavior::Slot3 (.text:062c8308-062c83f7, [EvadeDodgeBlock]) and
    // BuzzBehavior::RecomputeApproach (.text:062c5fa7-062c605b, [BuzzHeadTowardBlock]) store
    // the same key of their block through the same setter address.
    float IntervalTime;                // +0x34, formerly Field34. Ctor default 5.0f
                                       // (0x40a00000). EvadeDodgeIntervalTime / BuzzDodgeIntervalTime /
                                       // BuzzSlideIntervalTime (.text:063275d0).
    float IntervalTimeVariancePercent; // +0x38, formerly Field38. Ctor-zeroed. Update:
                                       // IntervalTimeLeft = IntervalTime - IntervalTime *
                                       // this * r (.text:062d79b0-062d79d7). The ...IntervalTime-
                                       // VariancePercent keys (.text:063275f0).
    float ConeAngle;                   // +0x3c, formerly Angle3c. Ctor default 1.0471377f
                                       // (0x3f860a92) -- close to pi/3 but NOT equal to it (pi/3
                                       // is 0x3f860c48), so it is recorded as raw bits.
                                       // EvadeDodgeConeAngle / BuzzDodgeConeAngle (.text:062a66b0).
    float ConeAngleVariancePercent;    // +0x40, formerly Field40. Ctor-zeroed. `FMUL [EBP+0x40]`
                                       // at 062d7c0f/7e3f. ...DodgeConeAngleVariancePercent.
    float WaggleAxisConeAngle;         // +0x44, formerly Field44. Ctor-zeroed. `FMUL/FSUB
                                       // [EBP+0x44]` at 062d7e6d/7e72. ...DodgeWaggleAxisConeAngle.
    float RollAngle;                   // +0x48, formerly Field48. Ctor-zeroed. Update pushes it as
                                       // FUN_062e2630's bank angle (062d7df9). ...DodgeRollAngle.
    float DodgeDistance;               // +0x4c, formerly Field4c. Ctor default 150.0f; Evade's
                                       // SetContext seeds 50.0f. EvadeDodgeDistance
                                       // (.text:062a33d0); the limit in Update is Field50 * this.
    float Field50;                                 // +0x50. Ctor-zeroed; Commit()
                                                    // zeroes it again. RETYPED int ->
                                                    // float 2026-10-08: Update FSTPs a
                                                    // float into it (062d79fa) and FLDs
                                                    // it (062d829f).
    float Field54;                                   // +0x54. Ctor default 0.2f
                                                      // (0x3e4ccccd).
    Vector Field58;                                    // +0x58, untouched by the ctor. Written
                                                        // only by SetField58 (FUN_062d88c0).
    unsigned char Field64;                               // +0x64. Ctor-zeroed byte.
    unsigned char Pad65[0x68 - 0x65];                      // +0x65, alignment only.
    Matrix WorkingFrame;                                     // +0x68..+0x8b. Ctor-zeroed
                                                              // by `MOV ECX,9; REP STOSD`;
                                                              // Update's steering frame
                                                              // (every `LEA EDI,[EBP+0x68];
                                                              // MOV ECX,9; REP MOVSD`).
    float IntervalTimeLeft;            // +0x8c, formerly Field8c. Ctor-zeroed; Commit() zeroes
                                       // it again. Update counts it down by DeltaTime (062d7975)
                                       // and re-arms it from IntervalTime/-VariancePercent.
    float TurnThrottle;                // +0x90, formerly Field90. Ctor default 1.0f. Scales the
                                       // angular throttle (062d8682-062d86d0).
                                       // EvadeDodgeTurnThrottle / BuzzDodgeTurnThrottle (FUN_062d8910).
    float Throttle;                    // +0x94, formerly Field94. Untouched by the ctor. Update
                                       // feeds it to the throttle command (062d8216/062d82bf).
                                       // Writers (FUN_062d9490): EvadeBehavior::Slot3 passes the
                                       // owner's IObjInspect::get_throttle (062c82ed),
                                       // BuzzBehavior BuzzHeadTowardEngineThrottle.
    unsigned char Field98;                                           // +0x98. Ctor-zeroed byte.
    unsigned char Opaque99[0xb4 - 0x99];                               // +0x99, untouched.
    // +0xb4, formerly FieldB4. Ctor default 1. Update's two switches dispatch on it.
    // EvadeBehavior::PickDodgeStyle (FUN_062c8690) picks it from the EvadeDodgeStyleWeight*
    // keys: 0 = Waggle, 1 = WaggleRandom, 2 = Slide, 3 = Corkscrew. BuzzBehavior writes 2 with
    // the BuzzSlide* keys, 0 for Waggle and 3 when BuzzHeadTowardRollThrottle != 0.
    int DodgeStyle;
    Vector VectorB8;                                                      // +0xb8, ends +0xc4.
                                                                           // Ctor-zeroed.
    float FieldC4;                                                          // +0xc4. Ctor default
                                                                             // 30.0f (0x41f00000).
    float SlideThrottle;               // +0xc8, formerly FieldC8. Ctor default 1.0f. Scales the
                                       // slide nudge (062d86d0..). EvadeDodgeSlideThrottle /
                                       // BuzzSlideThrottle (FUN_062d8930).
    // +0xcc, ends +0xd8. Ctor default {0.1f, 0.0f, 1.0f}; Update transforms it by the own
    // orientation as one Vector. .x is EvadeDodgeCorkscrewTurnThrottle (FUN_062d8940),
    // .z is EvadeDodgeCorkscrewRollThrottle / BuzzHeadTowardRollThrottle (FUN_062d8950).
    Vector VectorCc;
    unsigned char bRollFlipDirection;  // +0xd8, formerly FlagD8. Ctor-zeroed. Set: each style-3 reroll
                                       // negates VectorCc.z (062d8160). bEvadeDodgeCorkscrewRollFlipDirection /
                                       // bBuzzHeadTowardRollFlipDirection (FUN_062d8960).
    unsigned char FlagD9;                                                          // +0xd9. Ctor-zeroed.
    unsigned char FlagDa;                                                           // +0xda. Ctor-zeroed.
    unsigned char PadDb;                                                             // +0xdb, alignment.
    float FieldDc;                                                                    // +0xdc. Ctor
                                                                                       // default 1.0f.
};
static_assert(offsetof(DodgeIdea, Picker) == 0x14, "DodgeIdea::Picker offset");
static_assert(offsetof(DodgeIdea, IntervalTime) == 0x34, "DodgeIdea::IntervalTime offset");
static_assert(offsetof(DodgeIdea, ConeAngle) == 0x3c, "DodgeIdea::ConeAngle offset");
static_assert(offsetof(DodgeIdea, DodgeDistance) == 0x4c, "DodgeIdea::DodgeDistance offset");
static_assert(offsetof(DodgeIdea, Field54) == 0x54, "DodgeIdea::Field54 offset");
static_assert(offsetof(DodgeIdea, Field64) == 0x64, "DodgeIdea::Field64 offset");
static_assert(offsetof(DodgeIdea, WorkingFrame) == 0x68, "DodgeIdea::WorkingFrame offset");
static_assert(offsetof(DodgeIdea, Field50) == 0x50, "DodgeIdea::Field50 offset");
static_assert(offsetof(DodgeIdea, IntervalTimeLeft) == 0x8c, "DodgeIdea::IntervalTimeLeft offset");
static_assert(offsetof(DodgeIdea, TurnThrottle) == 0x90, "DodgeIdea::TurnThrottle offset");
static_assert(offsetof(DodgeIdea, Throttle) == 0x94, "DodgeIdea::Throttle offset");
static_assert(offsetof(DodgeIdea, Field98) == 0x98, "DodgeIdea::Field98 offset");
static_assert(offsetof(DodgeIdea, DodgeStyle) == 0xb4, "DodgeIdea::DodgeStyle offset");
static_assert(offsetof(DodgeIdea, VectorB8) == 0xb8, "DodgeIdea::VectorB8 offset");
static_assert(offsetof(DodgeIdea, FieldC4) == 0xc4, "DodgeIdea::FieldC4 offset");
static_assert(offsetof(DodgeIdea, VectorCc) == 0xcc, "DodgeIdea::VectorCc offset");
static_assert(offsetof(DodgeIdea, SlideThrottle) == 0xc8, "DodgeIdea::SlideThrottle offset");
static_assert(offsetof(DodgeIdea, bRollFlipDirection) == 0xd8, "DodgeIdea::bRollFlipDirection offset");
static_assert(offsetof(DodgeIdea, FieldDc) == 0xdc, "DodgeIdea::FieldDc offset");
static_assert(sizeof(DodgeIdea) == 0xe0, "DodgeIdea size (BuzzBehavior's 0x178..0x258 ctor gap meets the ctor's own +0xdc write)");

// ============================================================================
// BreakAwayIdea : public BaseIdea (+ DirectionPicker at +0x14) -- Common.dll
// RTTI name ".?AVBreakAwayIdea@@". Ctor FUN_062d8970 (item 1628). Published
// 2026-09-17 (claude:common-idea-internals).
// ============================================================================
// THIS IS common_buzzbehavior.h's "WatcherE", by the same argument as DodgeIdea
// above: FUN_062d8970's one vtable install is `MOV dword ptr [ESI],
// .rdata:PTR_LAB_063a04f8` at .text:062d89b6, and this header's sibling table
// decodes 063a04f8's COL (063a9b40) as BreakAwayIdea.
//
// sizeof == 0x9c, which closes a gap common_buzzbehavior.h explicitly flagged as
// open ("WatcherE's true size is <= 0x2f4-0x258 = 0x9c; no independent lower
// bound"). The lower bound is this ctor: its highest write is the dword at +0x98
// (.text:062d89e0), so the class is at least 0x9c. Upper and lower bound meet, so
// 0x9c is exact, not an estimate.
//
// Vtable .rdata:063a04f8:
//   slot 0 (0x00) LAB_062d7920  BaseIdea::SetContext, inherited unchanged
//   slot 1 (0x04) LAB_062d8a50  Update -- PORTED IN FULL 2026-09-17
//                               (claude:common-idea-updates), all 634
//                               instructions; see the .cpp.
//   slot 2 (0x08) MetaList<GoodDesc>::operator[]   ICF fold, RESERVED
//   slot 3 (0x0c) LAB_062d93d0  `MOV AX,0x52; RET` -> GetKind() == 0x52
//   slot 4 (0x10) LAB_062d93e0  Commit, ported in the .cpp
//   slot 5 (0x14) CEquip::GetConnectionPosition    ICF fold, RESERVED
//
// Commit() is worth reading for a reason beyond this class: at .text:062d9434 it
// calls `[Handle+0x14fc][+0x84]->vtbl[0xbc]()` and SKIPS its notification when
// that returns true. Slot 0xbc of IObjInspect is is_player() (common_iface.h),
// and "do not force this notification on the human player's ship" is exactly the
// behaviour that call shape implies -- a fifth independent confirmation, alongside
// the four listed in MoveIdea::Update's comment above, that the NavHelper-nested
// +0x84 sub-object three sibling headers still call "unidentified" is simply an
// IObjInspect*.
//
// FUN_062d9450 (.text:062d9450) IS a method of THIS class -- SetWatchObject
// below. The question this comment used to leave open ("DodgeIdea has those two
// members at exactly the same offsets, so the address alone cannot say which
// sibling the single call site means") is answered by the one call site's own
// receiver, re-read from the raw listing 2026-09-17 (claude:w19-buzz-retype):
// .text:062c6c8e is `LEA EDI,[ESI+0x258]` with ESI == BuzzBehavior's `this`, and
// BuzzBehavior+0x258 is WatcherE, i.e. a BreakAwayIdea. The same EDI is then
// dispatched through its OWN vtable slot 1 at .text:062c6cbc, which is
// LAB_062d8a50 == BreakAwayIdea::Update -- so the receiver is confirmed twice
// over, by the LEA and by the vtable identity.
// ============================================================================
class BreakAwayIdea : public BaseIdea
{
public:
    BreakAwayIdea();

    // Own vtable slot 1 -- LAB_062d8a50, item 1629, Common.dll.txt 221255-221928
    // (634 instructions, RET 4). PORTED IN FULL 2026-09-17; see the .cpp for the
    // per-branch derivation. Returns 3 (no Object), 1 (the break-away is done)
    // or 0 (keep going).
    int Update(float DeltaTime) override;

    // FUN_062d9220, item 1630, Common.dll.txt 221938-222099 (138 instructions).
    // NON-VIRTUAL __thiscall helper, not a vtable slot: the address appears
    // nowhere in the listing except its own header and the single
    // `MOV ECX,EBP; CALL` pair at .text:062d8d9b-062d8d9d inside Update above,
    // whose EBP is Update's own `this` (`MOV EBP,ECX` at .text:062d8a59). That
    // is the whole proof that this runs on a BreakAwayIdea, and it is why the
    // fields it touches (+0x28 Picker.Watcher, +0x54 Field54, +0x5c Flag5c,
    // Handle's nav block) are declared on this class rather than left as the
    // anonymous "SteeringTask composite" the item's original note assumed.
    // Returns 2 (nothing watched / still closing), 1 (clear -- break off) or 0.
    int EvaluateBreakAwayClearance();

    // FUN_062d9450 (item 1635), Common.dll.txt 222186-222209, 16 instructions,
    // __thiscall RET 4. NON-VIRTUAL: the address appears nowhere in the listing
    // except its own header and the single call at .text:062c6caf, whose receiver
    // is proved a BreakAwayIdea in the class comment above. Body, instruction by
    // instruction:
    //   062d9450  EAX = Object                       (the one stack argument)
    //   062d9459  ECX = this + 0x28                  (== &Picker.Watcher)
    //   062d945c  EAX = Object ? Object + 8 : nullptr (the IObjRW -> Watchable
    //             adjustment, identical to FaceIdea::SetWatchObject's)
    //   062d9465  if (EAX != Watcher.Watched) BaseWatcher::set_pointer(EAX)
    //             -- exactly what BaseWatcher::set() does, dedup included
    //   062d946f  Picker.Flag1c = 0                  (`MOV byte [ESI+0x30],0`;
    //             0x14 + 0x1c == 0x30)
    // Note the tag byte is CLEARED here, where FaceIdea/MoveIdea's own
    // SetWatchObject stamps a non-zero tag -- the name is structural, not a claim
    // that the three do the same thing to their tag fields.
    void SetWatchObject(void const* Object);

    // Own vtable slot 3 -- LAB_062d93d0, `MOV AX,0x52; RET`.
    short GetKind() const override { return 0x52; }

    // Own vtable slot 4 -- LAB_062d93e0, Common.dll.txt 222156-222184. Ported in
    // full in the .cpp.
    void Commit() override;

    // ---- Confirmed own-data fields, ascending offset order. ----
    FDirectionPicker Picker;         // +0x14, ends +0x34. Flag1c gets 1 here (the
                                      // one constant that differs from DodgeIdea's
                                      // otherwise byte-identical prologue).
    // Names below follow the personality keys the owners copy in (EvadeBehavior::Slot3
    // .text:062c84aa-062c8568, [EvadeBreakBlock]; BuzzBehavior::Update .text:062c6c97,
    // [BuzzPassByBlock]; BaseFollowerBehavior .text:062cbe60, FormationExitRollOutrunThrottle).
    //
    // +0x34, formerly Field34. Ctor-zeroed. Update() dispatches on it (062d8d75). Evade picks it
    // from the EvadeBreakStyleWeight* keys (.text:062c843b-062c84a7): 0 = Outrun (the "drive
    // forward" arm; BaseFollowerBehavior's FormationExitRollOutrunThrottle path writes 0),
    // 1 = Sideways, 2 = Reverse (the arm that runs the AttemptReverseTime / ReverseDistance
    // checks; EvadeBehavior case 3 writes 2). BuzzBehavior writes 1.
    int BreakStyle;
    float Field38;                      // +0x38. Ctor default 350.0f (0x43af0000). The range
                                        // Update() hands IsWithinInterceptRangeOfIface
                                        // (062d90d4 / 062d91dc). Evade and Buzz never set it.
    unsigned char Flag3c;                // +0x3c. Ctor default 1.
    unsigned char Pad3d[3];               // +0x3d, alignment only.
    float Throttle;                     // +0x40, formerly Field40. Ctor default 1.0f. Update()
                                        // passes it to ThrustManager::SetThrottle (062d8e31,
                                        // 062d9182). Its one writer is EvadeBehavior::Slot3 with
                                        // the owner's IObjInspect::get_throttle (062c8303).
    unsigned char Flag44;                    // +0x44. Ctor-zeroed byte.
    unsigned char Pad45[3];                   // +0x45, alignment only.
    float AttemptReverseElapsed;        // +0x48, formerly Field48. Ctor-zeroed (integer 0, same
                                        // bits). BreakStyle 2: DeltaTime sum (062d8dbb) compared
                                        // against AttemptReverseTime (062d8d8e).
    float AttemptReverseTime;           // +0x4c, formerly Field4c. Ctor default 4.0f.
                                        // EvadeBreakAttemptReverseTime (.text:062a33d0).
    float Field50;                                  // +0x50. Ctor-zeroed. RETYPED
                                                     // int -> float for the same reason
                                                     // (`FADD [EBP+0x50]; FST; FCOMP
                                                     // [EBP+0x58]` at 062d8def).
    float ReverseDistance;              // +0x54, formerly Field54. Ctor default 100.0f. The
                                        // clearance threshold EvaluateBreakAwayClearance()
                                        // compares against (062d92f9). EvadeBreakReverseDistance
                                        // (FUN_062d93b0).
    float Field58;                                      // +0x58. Ctor default 6.0f -- the
                                                         // cap on Field50.
    unsigned char Flag5c;                                 // +0x5c. Ctor-zeroed byte.
    unsigned char Pad5d[0x60 - 0x5d];                       // +0x5d, alignment only.
    Matrix Frame60;                                           // +0x60, ends +0x84. NOT
                                                               // ctor-initialized in retail.
                                                               // CARVED OUT of the old
                                                               // `Opaque5d[0x84-0x5d]`
                                                               // 2026-09-17: Update()'s
                                                               // reroll REP MOVSDs nine
                                                               // dwords of
                                                               // BuildOrientationBasis's
                                                               // output into `[EBP+0x60]`
                                                               // (062d8c3b-062d8c47), then
                                                               // its four switch arms rotate
                                                               // the three rows in place, and
                                                               // the Field34 == 1 arm hands
                                                               // `LEA ESI,[EBP+0x60]` to
                                                               // TurnHelper::
                                                               // get_angular_throttle,
                                                               // ComputeBankedSteeringFrame
                                                               // and LimitAxisThrottleForTurn
                                                               // as a Matrix. 9 dwords ==
                                                               // sizeof(Matrix) exactly, and
                                                               // it ends flush against
                                                               // Field84.
    float IntervalElapsed;              // +0x84, formerly Field84. Ctor default 100000.0f
                                        // (0x47c35000); Commit() restores it. The reroll timer:
                                        // Update() adds DeltaTime and re-picks the break-away
                                        // direction once it reaches IntervalTime (062d8b9e).
    float IntervalTime;                 // +0x88, formerly Field88. Ctor default 100000.0f.
                                        // EvadeBreakIntervalTime (FUN_062d9480).
    float AfterburnerDelay;             // +0x8c, formerly Field8c. Ctor-zeroed. Evade:
                                        // EvadeBreakAfterburnerDelay minus a random share of
                                        // ...AfterburnerDelayVariancePercent (FUN_062d93c0).
    float AfterburnerElapsed;           // +0x90, formerly Field90. Ctor-zeroed; Commit() zeroes it.
                                        // DeltaTime sum; once it reaches AfterburnerDelay Update()
                                        // calls ThrustManager::SetThrustersActive(true)
                                        // (062d90a0-062d90c1, 062d9199-062d91ba).
    float TurnThrottle;                 // +0x94, formerly Field94. Ctor default 1.0f. Scales the
                                        // steering command (062d8fa7). EvadeBreakTurnThrottle /
                                        // BuzzBreakTurnThrottle (FUN_062d9490).
    float RollThrottle;                 // +0x98, formerly Field98. Ctor default 1.0f. The z of the
                                        // {0, 0, RollThrottle} command (062d9072, 062d9132).
                                        // EvadeBreakRollThrottle / FormationExitRollOutrunThrottle
                                        // (FUN_062d94a0).
};
static_assert(offsetof(BreakAwayIdea, Picker) == 0x14, "BreakAwayIdea::Picker offset");
static_assert(offsetof(BreakAwayIdea, BreakStyle) == 0x34, "BreakAwayIdea::BreakStyle offset");
static_assert(offsetof(BreakAwayIdea, Field38) == 0x38, "BreakAwayIdea::Field38 offset");
static_assert(offsetof(BreakAwayIdea, Flag3c) == 0x3c, "BreakAwayIdea::Flag3c offset");
static_assert(offsetof(BreakAwayIdea, Throttle) == 0x40, "BreakAwayIdea::Throttle offset");
static_assert(offsetof(BreakAwayIdea, Flag44) == 0x44, "BreakAwayIdea::Flag44 offset");
static_assert(offsetof(BreakAwayIdea, AttemptReverseTime) == 0x4c, "BreakAwayIdea::AttemptReverseTime offset");
static_assert(offsetof(BreakAwayIdea, ReverseDistance) == 0x54, "BreakAwayIdea::ReverseDistance offset");
static_assert(offsetof(BreakAwayIdea, Field58) == 0x58, "BreakAwayIdea::Field58 offset");
static_assert(offsetof(BreakAwayIdea, Flag5c) == 0x5c, "BreakAwayIdea::Flag5c offset");
static_assert(offsetof(BreakAwayIdea, Frame60) == 0x60, "BreakAwayIdea::Frame60 offset");
static_assert(offsetof(BreakAwayIdea, IntervalElapsed) == 0x84, "BreakAwayIdea::IntervalElapsed offset");
static_assert(offsetof(BreakAwayIdea, AfterburnerDelay) == 0x8c, "BreakAwayIdea::AfterburnerDelay offset");
static_assert(offsetof(BreakAwayIdea, TurnThrottle) == 0x94, "BreakAwayIdea::TurnThrottle offset");
static_assert(offsetof(BreakAwayIdea, RollThrottle) == 0x98, "BreakAwayIdea::RollThrottle offset");
static_assert(sizeof(BreakAwayIdea) == 0x9c, "BreakAwayIdea size (BuzzBehavior's 0x258..0x2f4 upper bound meets the ctor's own +0x98 write)");

// ============================================================================
// DodgeMissileIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVDodgeMissileIdea@@". Ctor FUN_062d94b0 (item 1639). Published and
// PORTED IN FULL 2026-09-17 (claude:w17-idea): all four live vtable slots plus
// the constructor. VERIFIED ADDRESS RANGES for this pass, stated explicitly:
//   .text:062d94b0-062d94ec (ctor, 17 insns)
//   .text:062d94f0-062d9a4e (Update, 428 insns -- every instruction, both the
//                            "avoid probe" arm and all three Mode1c arms)
//   .text:062d9a60-062d9a64 (GetKind, 2 insns)
//   .text:062d9a70-062d9ad7 (Commit, 26 insns)
//   .text:062c7790-062c7869 (the enclosing host ctor, for the size proof)
//   .rdata:063a0520-063a0538, .rdata:063a9b54-063a9b98, .data:063ec630 (RTTI)
// ============================================================================
// RTTI chain, re-walked from the raw .rdata/.data bytes this pass rather than
// inherited from the prior lane's tracker note (every address below was read
// out of the listing again, and all of it held):
//   * .rdata:063a0524 is the vtable. Its single install is the constructor's
//     own `MOV dword ptr [EAX],.rdata:PTR_LAB_063a0524` at .text:062d94dc.
//   * [vtable - 4] == .rdata:063a0520 holds 063a9b88 (Common.dll.txt:563925).
//   * COL 063a9b88: signature 0, offset 0, cdOffset 0, pTypeDescriptor
//     .data:063ec630, pClassHierarchyDescriptor 063a9b78
//     (Common.dll.txt:583961-583974).
//   * .data:063ec630's `name` field at +0x08 reads ".?AVDodgeMissileIdea@@"
//     verbatim (Common.dll.txt:634864).
//   * CHD 063a9b78: numBaseClasses == 2 (the dword `02 00 00 00` at
//     .rdata:063a9b80), pBaseClassArray == 063a9b6c ==
//     [063a9b54 (own BCD -> 063ec630, numContainedBases 1, mdisp 0, pdisp -1),
//      063a9940 (BaseIdea's own BCD)]. Single inheritance at offset 0, hence
//     `: public BaseIdea` with no adjustor.
//   * Every one of the FIVE return paths of Update (.text:062d950c, 062d9529,
//     062d9648, 062d97ee and the shared tail 062d9a4e) is `RET 0x4`, so the
//     slot takes exactly one explicit argument -- BaseIdea::Update(float).
//     (The prior note said "four return paths"; there are five. The extra one
//     is the Mode1c == 2 exit at .text:062d97ee.)
//
// sizeof == 0x20, and BOTH bounds are exact rather than estimated:
//   * LOWER: Update reads the dword at `this + 0x1c` (.text:062d964b), so the
//     class is at least 0x20 bytes.
//   * UPPER: this class is never heap-allocated -- there is no `operator new`
//     site for it anywhere, so there is no allocation-size argument to read.
//     Its constructor has exactly ONE call site and it is an EMBEDDED
//     sub-object: `LEA ECX,[ESI + 0x1ac]; CALL FUN_062d94b0` at
//     .text:062c77d6-062c77e1, inside FUN_062c7790. The very next member of
//     that same host is constructed at `LEA ECX,[ESI + 0x1cc]`
//     (.text:062c77e6), so this sub-object spans exactly 0x1ac..0x1cc == 0x20.
//   * CROSS-CHECK: the same host constructs a BreakAwayIdea at `LEA ECX,[ESI +
//     0x110]` (.text:062c77c6), and 0x110 + 0x9c == 0x1ac exactly -- i.e. this
//     reading of FUN_062c7790 independently reproduces BreakAwayIdea's
//     already-recorded 0x9c from a completely different direction.
//
// Vtable .rdata:063a0524 (Common.dll.txt:563929-563935):
//   slot 0 (0x00) LAB_062d7920  BaseIdea::SetContext, inherited unchanged
//   slot 1 (0x04) LAB_062d94f0  Update -- item 1640, 428 instructions, ported
//                               in full in the .cpp
//   slot 2 (0x08) MetaList<struct_GoodDesc>::operator[]  ICF fold, RESERVED
//   slot 3 (0x0c) LAB_062d9a60  `MOV AX,0x56; RET` -> GetKind() == 0x56
//   slot 4 (0x10) LAB_062d9a70  Commit, ported in the .cpp
//   slot 5 (0x14) CEquip::GetConnectionPosition          ICF fold, RESERVED
//
// What the whole class DOES, in one paragraph, because the name alone is
// misleading: this Idea does NOT own a missile-avoidance algorithm. It is the
// thin dispatcher that turns ONE watched incoming object (MissileWatcher) plus
// ONE mode selector (Mode1c) into the three commands the ship's nav component
// understands. The heavy lifting is all in shared helpers that other lanes
// already ported -- AiVolumeProbeApproachDirection, BuildOrientationBasis,
// BuildRandomSpreadFrame, TurnHelper::get_angular_throttle and
// ComputeBankedSteeringFrame. Its "dodge" geometry is one cross product:
// normalize(DirToMissile x Axis), where Axis is the ship's own up or forward
// column depending on how head-on the missile is. See the .cpp.
// ============================================================================
class DodgeMissileIdea : public BaseIdea
{
public:
    DodgeMissileIdea();

    // Own vtable slot 1 -- LAB_062d94f0, item 1640, Common.dll.txt
    // 222283-222728 (428 instructions, `RET 0x4` on all five exits). PORTED IN
    // FULL 2026-09-17; see the .cpp for the per-branch derivation. Returns 3
    // (no Object), 1 (nothing watched any more) or 0 (command issued).
    int Update(float DeltaTime) override;

    // Own vtable slot 3 -- LAB_062d9a60, Common.dll.txt 222744-222746:
    // `MOV AX,0x56; RET`. 0x56 == 86, and it is distinct from every sibling tag
    // recorded in this header (StopIdea 2, BreakAwayIdea 0x52).
    short GetKind() const override { return 0x56; }

    // Own vtable slot 4 -- LAB_062d9a70, Common.dll.txt 222758-222784
    // (26 instructions). Ported in full in the .cpp.
    void Commit() override;

    // FUN_062d9ae0 (.text:062d9ae0-062d9aff, RET 4): MissileWatcher.set(Object ? Object+8 :
    // null), retail's own `CMP EAX,[ECX]; JZ` dedup then a tail JMP into set_pointer.
    // No tag byte. Only caller: EvadeBehavior::Slot7 mode 4 (.text:062c7f30/7f62/7f75/7faa).
    void SetWatchObject(void const* Object);

    // ---- Confirmed own-data fields, ascending offset order. ----

    // +0x14, ends +0x1c. A BARE BaseWatcher, not an FDirectionPicker: the
    // constructor zeroes exactly two dwords here (`MOV [EAX+0x14],ECX` at
    // .text:062d94d2 and `MOV [EAX+0x18],ECX` at .text:062d94d5) and nothing
    // else, which is 8 bytes == sizeof(BaseWatcher), whereas both sibling
    // classes that DO embed an FDirectionPicker (DodgeIdea, BreakAwayIdea)
    // write its five weight/tag fields as well. Update resolves it with the
    // family's standard `Ptr ? Ptr - 8 : nullptr` Watchable->IObjRW downcast
    // at .text:062d950f-062d951b, 062d968f-062d969d and 062d9801-062d980f.
    BaseWatcher MissileWatcher;

    // +0x1c. The mode selector Update switches on at .text:062d964b-062d965f
    // (`SUB EAX,0x0 / JZ` then `DEC EAX / JZ` then `DEC EAX / JNZ`), so the
    // live values are 0, 1 and 2 and anything else is a no-op that returns 0.
    // Typed int because that ladder is a full 32-bit dword compare, not a byte
    // or a float (no FLD ever touches this offset).
    //
    // NOT constructor-initialized in retail -- the constructor stops at +0x18
    // -- so it starts as whatever the enclosing host object's storage held.
    // Zero-initialized here anyway, per this header's documented "safe
    // over-init" convention (see BaseIdea::Context), which also happens to be
    // the arm retail's own zeroed host storage would select.
    //
    // WRITER FOUND 2026-10-08 (claude-code:cmn-ai4): EvadeBehavior::Slot7 mode 4 calls
    // .text:0631d950 (`MOV [ECX+0x1c],EAX; RET 4`, ICF-labelled
    // CEquip::ConnectClientEquip) on `LEA EDI,[ESI+0x1ac]` == this object
    // (.text:062c7fb1-062c7fb3) with 1, 2 or 0. The earlier search looked for
    // host offset +0x1c8, but the host's sub-object starts at +0x1ac (+0x1ac+0x1c ==
    // +0x1c8 was right; the write goes through the setter's own ECX, not a host disp).
    int Mode1c;
};
static_assert(offsetof(DodgeMissileIdea, MissileWatcher) == 0x14, "DodgeMissileIdea::MissileWatcher offset");
static_assert(offsetof(DodgeMissileIdea, Mode1c) == 0x1c, "DodgeMissileIdea::Mode1c offset");
static_assert(sizeof(DodgeMissileIdea) == 0x20, "DodgeMissileIdea size (host FUN_062c7790's 0x1ac..0x1cc sub-object span meets Update's own +0x1c read)");
