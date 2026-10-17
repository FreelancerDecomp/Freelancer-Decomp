// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior, FormationMoveFx
#include "common_idea.h"      // MoveIdea, FaceIdea -- see the WatcherA/B identity note below
#include "common_iface.h"     // IObjRW, IObjInspect, ObjInspectOf, ObjRwFromWatchable
#include "common_aisteer.h"   // FAiTarget + the three intercept probes Update() calls

#include <cstddef>

// ============================================================================
// BuzzBehavior -- AI "buzz" maneuver state (Behavior family, TractorArm named-
// mode dispatch table row "Buzz")
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw
// bytes -- see decomp-tracker item 1309 and this marathon's "ground truth
// precedence" rule):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a8d10 (offset field = 0, i.e. the
//   primary -- and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor, name ".?AVBuzzBehavior@@"
//     -> single base = root Behavior (numBaseClasses = 2: self + Behavior) --
//        same direct, single-inheritance shape as IdleBehavior
//        (common_idlebehavior.h), NOT the BaseFollowerBehavior/
//        DockBehaviorFields multiple-inheritance shape.
//
// Own vtable .rdata:0x0639f564 (dtor thunk FUN_062c5c00 at slot 0). Ctor =
// FUN_062c5af0 (decomp-tracker item 1300, already tracked project-wide under
// the descriptive alias "TractorArm::Buzz (goto-directive mode)" before this
// pass connected it to the BuzzBehavior RTTI name -- same relationship as
// DockBehavior's ctor/item 1261 and IdleBehavior's ctor/item 1410).
//
// ----------------------------------------------------------------------------
// Field layout, by direct analogy with IdleBehavior (common_idlebehavior.h):
// BuzzBehavior's own vtable slot 3 (LAB_062c5dd0, RecomputeApproach() below)
// opens with an UNCONDITIONAL `CALL FUN_062d20d0` (this unchanged) -- the
// SAME shared retail function IdleBehavior::ResetTickState() tail-calls, and
// whose body is independently fully understood for this exact call shape:
// `this[0xc]=1; this[0x10]=1; this[0x28] &= ~1; this[0x1c]=0`. This directly
// CONFIRMS BuzzBehavior converges on the identical +0x08/+0x0c/+0x10/+0x14/
// +0x1c/+0x20/+0x24/+0x28/+0x29 "context + flags" prefix IdleBehavior/
// DockBehaviorFields document at those same absolute offsets, even though
// (like IdleBehavior) BuzzBehavior does not literally inherit
// BaseFollowerBehavior -- see common_idlebehavior.h's own class comment for
// the fuller discussion of this "shared field shape, not shared base class"
// convention across the whole TractorArm family. BuzzBehavior's own
// SetContext (LAB_062c5cf0, below) additionally confirms this by calling the
// SAME shared context initializer FUN_062d2090(this, P1, P2, P3) that
// DockBehavior::SetContext/IdleBehavior::SetContext also call, then reading
// back this[0x14]/this[0x24] immediately afterward exactly as those two
// classes' own ported bodies do.
//
// Five embedded sub-objects follow at +0x2c, ascending, sizes/ctor-functions
// confirmed directly from FUN_062c5af0's own raw disassembly (Common.dll.txt
// 191534-191594, LEA/CALL pairs -- gaps between successive LEA targets give
// exact sizes):
//   +0x2c  WatcherA   size 0x78   ctor FUN_062d4e60
//   +0xa4  WatcherB   size 0xc0   ctor FUN_062d6750
//   +0x164 WatcherC   size 0x14   ctor FUN_062da500
//   +0x178 WatcherD   size 0xe0   ctor FUN_062d77d0
//   +0x258 WatcherE   size 0x9c   ctor FUN_062d8970
//
// WatcherA/WatcherB are the SAME two sub-object classes DockBehavior/
// LaunchBehavior/LootBehavior/FaceBehavior embed as their own SubObjectA/
// SubObjectB -- identical ctor addresses (FUN_062d4e60/FUN_062d6750) AND
// identical sizes (0x78/0xc0) -- so this pass reuses the "WatcherA/WatcherB"
// naming convention rather than "SubObjectA/SubObjectB", to make the family
// relationship obvious, but they are now typed for real as `MoveIdea`/
// `FaceIdea` (common_idea.h), matching those sibling classes.
//
// WatcherC IDENTITY, RESOLVED 2026-09-17 (claude:common-buzzbehavior): WatcherC
// is an `AfterburnerIdea` (common_idea.h), typed for real below. An earlier
// pass on this file kept it an opaque byte buffer because its confirmed size
// here (0x14, from the 0x164..0x178 ctor-LEA gap) differed from DockBehavior's
// own FUN_062da500-constructed storage (0x18) and that was read as "the two
// classes reserve different amounts of space". That reasoning is now
// disproved: common_idea.h has since decoded FUN_062da500's RTTI chain to
// ".?AVAfterburnerIdea@@" and established that the class adds NO fields of its
// own beyond BaseIdea, whose `static_assert(sizeof(BaseIdea) == 0x14)` makes
// sizeof(AfterburnerIdea) EXACTLY 0x14 -- i.e. WatcherC's measured size is an
// exact match, not a mismatch, and it is DockBehavior's 0x18 that is the
// outlier (common_idea.h's own AfterburnerIdea note already flags that storage
// as probably holding a POINTER pair rather than the object by value). Typing
// WatcherC for real unblocks eight call sites in Update()/Disengage() that had
// been FULLPORT-DEFERRED purely for want of its class: its vtable slot 0
// (SetContext) is BaseIdea's shared default, slot 1 (Update(float)) and slot 4
// (Commit()) are AfterburnerIdea's own overrides. Both of the latter still
// have FULLPORT-DEFERRED BODIES inside common_idea.cpp (a gap owned by that
// file, not by this one), so the calls are currently inert -- but they are now
// retail's real call sites instead of comments.
//
// WatcherD/WatcherE IDENTITY (2026-09-11, d295:common-port; CLOSED 2026-09-17,
// claude:w19-buzz-retype): their ctors (FUN_062d77d0/FUN_062d8970) are NOT
// unseen elsewhere -- they are the EXACT same ctor addresses common_aisteer.h's
// own "steering task" polymorphic family documents:
//   WatcherD (+0x178, size 0xe0) <-> PTR_LAB_063a04c8, ctor FUN_062d77d0,
//     Update LAB_062d7950. common_idea.h publishes this class for real as
//     `DodgeIdea`, with a real constructor; DodgeIdea::Update() is defined in
//     common_dodgeidea.cpp.
//   WatcherE (+0x258, size 0x9c) <-> PTR_LAB_063a04f8, ctor FUN_062d8970,
//     Update LAB_062d8a50. common_idea.h publishes this class for real as
//     `BreakAwayIdea`, constructor and all six vtable slots included --
//     LAB_062d8a50 was ported in full (all 634 instructions, tracker item 1629,
//     ready_unverified) by claude:common-idea-updates.
// Both identifications rest on the SAME "two different vtable-pointer
// literals can never be byte-identical ctors" argument the WatcherA/MoveIdea
// paragraph below already established -- not a size/offset coincidence.
// BOTH MEMBERS ARE TYPED FOR REAL BELOW as of 2026-09-17: they are constructed
// by their own real constructors, they carry real vtable pointers, and every
// retail call on them -- slot 0 (SetContext), slot 1 (Update), slot 4 (Commit),
// the non-virtual BreakAwayIdea::SetWatchObject and all fourteen field-setter
// folds -- is a real call on a real object. Nothing about these two is deferred
// any more.
//
// WatcherA IDENTITY CONFLICT, RESOLVED 2026-09-11 (d285:common-port): an
// earlier pass on this file claimed WatcherA is CONFIRMED to be
// FormationMoveFx (common_behavior.h), reasoning that BuzzBehavior::SetContext
// calls `FUN_062d5980` directly on it (Common.dll.txt 062c5d51-062c5d53) and
// FUN_062d5980 is independently ported as FormationMoveFx::SetMode(int). That
// reasoning is superseded here in favor of the STRONGER, RTTI-based signal:
// WatcherA's own CONSTRUCTOR is FUN_062d4e60 (re-verified directly against
// Common.dll.txt 191558, inside BuzzBehavior::BuzzBehavior/FUN_062c5af0),
// which common_idea.h hand-decodes a genuine MSVC RTTICompleteObjectLocator/
// TypeDescriptor chain for, giving the class name ".?AVMoveIdea@@" -- i.e.
// WatcherA really is a MoveIdea instance, full stop (a constructor cannot be
// MSVC-ICF-folded across two different classes: it writes a class-specific
// vtable-pointer LITERAL, so two ctors installing different vtables can never
// be byte-identical). By contrast, common_behavior.h's own FormationMoveFx
// class comment explicitly notes its leading vtable pointer "has no recovered
// RTTI name of its own" -- that identification rests on SIZE (0x78) +
// FIELD-OFFSET convergence with FormationBehavior's own two embeds, not on a
// decoded ctor/RTTI chain, and FormationBehavior's own ctor (FUN_062cbfe0,
// Common.dll.txt 200676-200693) does NOT call FUN_062d4e60 (or any other
// shared sub-object ctor) at all for its own +0x30/+0xa8 members -- so the
// "FormationMoveFx" name is weaker evidence than MoveIdea's own ctor-RTTI
// decode wherever the two conflict. FUN_062d5980 being callable on a genuine
// MoveIdea's `&WatcherA` is not actually surprising: it is invoked at a raw
// address (`CALL FUN_062d5980`), never through either class's vtable, and
// MoveIdea::Commit() (common_idea.h, LAB_062d56b0) independently documents
// its OWN control flow reading MoveIdea::Field34 (mode-like, compared against
// 1) and routing through the SAME two registration helpers
// (FUN_062e8ce0/FUN_062e89a0) FormationMoveFx::SetMode also calls -- i.e.
// FUN_062d5980 most likely is a genuinely shared, non-virtual "Mode
// transition" helper the two sibling classes' authors both call directly by
// address on their own compatible +0x34-ish sub-layout, not a FormationMoveFx
// member being called on a foreign object. Given that, this pass does NOT
// reinterpret_cast WatcherA to FormationMoveFx* anywhere (doing so on a real
// MoveIdea instance would be undefined behavior if the two classes' actual
// field layouts past +0x14 diverge, which common_idea.h's own MoveIdea layout
// confirms they do) -- every FUN_062d5980/FUN_062d58f0/FUN_062d5890 call on
// WatcherA is left FULLPORT-DEFERRED instead, matching
// common_launchbehavior.cpp's own established precedent for the identical
// `FUN_062d5980(&SubObjectA, 0)` call shape on its own confirmed-MoveIdea
// SubObjectA (ReleaseApproachStep() there).
//
// RESOLVED 2026-09-11 (d286:common-port), closing the "FLAGGED FOR THE
// ORCHESTRATOR" note this paragraph used to end on: the worry that
// FormationBehavior's own "FormationMoveFx" embeds rested on the SAME weak
// call-target-attribution method as WatcherA's original (wrong) guess here
// is now moot -- FormationBehavior's own ctor chain WAS independently pinned
// down since this note was written. The shared base-init helper
// FUN_062cb2b0(this), which BOTH FormationBehavior's and FollowBehavior's own
// ctors call, was directly re-traced from the raw disassembly and DOES call
// real, distinct constructors for each of its five sub-objects: FUN_062d5aa0
// at +0x34 (FormationMoveFx's own real, separate ctor -- confirmed distinct
// from MoveIdea's FUN_062d4e60 by the same "two different vtable-pointer
// literals" argument this file's own paragraph above already established for
// WatcherA/MoveIdea) and FUN_062d4e60 at +0x150 (MoveIdea, same reasoning).
// See common_behavior.h's own FormationMoveFx class comment for the full
// writeup -- FormationBehavior's OLD "+0x30/+0xA8, two FormationMoveFx
// instances" model (the one this paragraph's "in question" flag referred to)
// was independently found to be factually wrong and has been replaced with
// the ctor-confirmed real layout (ONE FormationMoveFx, at +0x34). This also
// resolved decomp-tracker item 1583's own flagged identity conflict.
//
// ----------------------------------------------------------------------------
// THE /OPT:ICF FOLD TABLE FOR THIS FILE, RESOLVED 2026-09-17
// (claude:common-buzzbehavior). Several calls in Update()/RecomputeApproach()/
// SetContext() carry Ghidra symbol names (`CmnAsteroid::CAsteroidCube::
// set_state`, `CSimple::SetOwnerPlayer`, `CAttachedEquip::SetFate`,
// `TextRenderContext::set_origin_h`, `DeviceTRC::bind_to_dc`) that earlier
// passes on this file correctly SUSPECTED were MSVC identical-code-folding
// artifacts, but then treated as permanently unresolvable and left
// FULLPORT-DEFERRED. That is no longer true of a single one of them: each
// address was disassembled directly this pass and every one is a one-line
// field setter whose EFFECT is unambiguous once the receiver's class is known
// (and the receiver's class is proved by the enclosing ctor's own LEA/CALL
// pairs, per this marathon's lesson #44 -- a folded address proves nothing
// about the receiver, but the ctor does):
//
//   .text:062a33e0  `MOV [ECX+0x40],EAX; RET 4`  "CAsteroidCube::set_state"
//   .text:062a66b0  `MOV [ECX+0x3c],EAX; RET 4`  "DeviceTRC::bind_to_dc"
//   .text:063275f0  `MOV [ECX+0x38],EAX; RET 4`  "TextRenderContext::set_origin_h"
//   .text:063275d0  `MOV [ECX+0x34],EAX; RET 4`  "CAttachedEquip::SetFate"
//   .text:062d8920  `MOV [ECX+0xb4],EAX; RET 4`  "CSimple::SetOwnerPlayer"
//   .text:062d88b0  `MOV [ECX+0x44],EAX; RET 4`  FUN_062d88b0
//   .text:062d9490  `MOV [ECX+0x94],EAX; RET 4`  FUN_062d9490
//   .text:062d93c0  `MOV [ECX+0x8c],EAX; RET 4`  FUN_062d93c0
//
// On a MoveIdea receiver (&WatcherA) the first three and FUN_062d88b0 are
// exactly MoveIdea::SetScale40 / SetRange3c / SetAngle38 / SetField44, which
// common_idea.h now declares and defines for real -- so every &WatcherA call
// site in this file is wired for real and the "not safe to call through yet"
// wording those sites used to carry is deleted, not merely softened. On a
// FaceIdea receiver (&WatcherB) FUN_062d9490/FUN_062d93c0 are
// FaceIdea::ScaleDefault1_94 / AngleWeight8c, also named fields. On a DodgeIdea
// receiver (&WatcherD) and a BreakAwayIdea receiver (&WatcherE) the same
// addresses are writes into THOSE classes' own named fields, which common_idea.h
// declares and this file's .cpp now assigns directly -- see the
// "FOURTEEN RETAIL SETTER ADDRESSES" block below the fields for the full map and
// for the two that still land inside an Opaque span.
//
// This port of course never calls the real `CmnAsteroid::CAsteroidCube::
// set_state` (that would touch an unrelated Asteroid-field object); it
// reproduces the one-line EFFECT on the correct receiver instead.
// ----------------------------------------------------------------------------
//
// NavHelper (+0x24) is read throughout Update()/RecomputeApproach() at many
// large, raw (non-vtable) offsets (+0x142c, +0x1364, +0x14fc, +0x1580,
// +0x1618..+0x16a8, +0x17ec..+0x17fc, +0x168c) -- consistent with
// common_behavior.h's own note that NavHelper "owns a cached 'target lead
// point' (its own +0x1508/+0x150c/+0x1510)" (note +0x14fc+0xc=+0x1508,
// +0x14fc+0x10=+0x150c, +0x14fc+0x14=+0x1510 -- an EXACT match, confirming
// +0x14fc is the head of that same nested sub-object) "plus a recomputed-
// this-frame flag at +0x157a" (matches this pass's own +0x24+0x157a read)
// "and a nested vtable-having sub-object at +0x1580" (also independently
// touched here). NavHelper's real class is still not identified anywhere in
// this port; every raw read/vcall through it is either reproduced as a plain
// memory read (safe, no ambiguity) or left FULLPORT-DEFERRED (vcalls/whole
// blocks whose real effect depends on NavHelper's unidentified shape) -- this
// is the exact "still-unidentified NavHelper/OwnerContext-shaped sub-object
// dependency" decomp-tracker item 1309's own notes flagged in advance.
// ============================================================================

class BuzzBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (1, "Buzz"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit BuzzBehavior(int InTypeId);
    ~BuzzBehavior();

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639f564). Slot -> retail address -> decomp-tracker item.
    //   0  (0x00) FUN_062c5c00   item 1301  scalar-deleting-dtor thunk (MSVC boilerplate;
    //                                       real dtor is FUN_062c5c40 below)
    //   1  (0x04) LAB_062c5cf0   item 1304  ported (outer logic): SetContext()
    //   3  (0x0c) LAB_062c5dd0   item 1305  ported (outer logic): RecomputeApproach()
    //   4  (0x10) FUN_062c60a0   item 1306  ported for real: Disengage()
    //   5  (0x14) FUN_062c60f0   item 1307  ported for real (3 sub-calls deferred): EvaluateReadiness()
    //   7  (0x1c) LAB_062c62a0   item 1309  ported (state-machine shape + confirmed
    //                                       field arithmetic; most per-case bodies
    //                                       FULLPORT-DEFERRED, see Update() below): Update()
    // Slots 2/6 and others not listed are not confirmed for this pass (likely
    // more ICF-shared generic-setter folds, matching the DockBehavior/
    // IdleBehavior vtable-shape precedent) -- not investigated here.
    // ------------------------------------------------------------------

    // Own vtable slot 1 (offset 0x04). Common.dll.txt 191719-191797
    // (LAB_062c5cf0), 77 instructions. Forwards to the shared context
    // initializer, then dispatches SetContext-shaped calls to all five
    // Watcher sub-objects, interleaved with a five-call MoveIdea
    // configuration block on WatcherA (SetField44(2) / SetAngle38(PI) /
    // SetMode(0) / SetRange3c(0.0f)). All of that is REAL as of 2026-09-17:
    // the configuration block's four addresses are the /OPT:ICF folds the
    // header's fold table resolves, and common_idea.h now has real bodies for
    // all four -- the "not safe to call through yet" note this file used to
    // carry on them is deleted, not softened. WatcherD's and WatcherE's own
    // slot-0 calls joined them 2026-09-17: both are DodgeIdea/BreakAwayIdea now
    // and both inherit BaseIdea::SetContext unchanged, so all five sub-objects'
    // slot-0 dispatches are real. Nothing in this function is deferred.
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Own vtable slot 3 (offset 0x0c). Common.dll.txt 191808-191999
    // (LAB_062c5dd0), 186 instructions. Resets the tick-state prefix (shared
    // FUN_062d20d0, same as IdleBehavior::ResetTickState), re-rolls
    // WatcherA/B/C/D/E's per-tick tunables from NavHelper's large tunable
    // block, and re-derives HeadTowardStyle/PassByStyle/MinDistanceToHeadToward/Field344 via
    // RandomVector()/rand()-driven weighted picks.
    void RecomputeApproach();

    // Own vtable slot 4 (offset 0x10). Common.dll.txt 192001-192024
    // (FUN_062c60a0), 20 instructions. Calls all five Watcher sub-objects'
    // own vtbl slot 0x10, in order WatcherB/A/C/D/E -- BaseIdea's own slot
    // 4/0x10 is Commit(), so WatcherB/A's calls are wired for real (see
    // .cpp); WatcherC joined them 2026-09-17 with its AfterburnerIdea
    // identification, and WatcherD/WatcherE joined the same day with the
    // DodgeIdea/BreakAwayIdea retype -- all five Commit()s are real calls.
    // Then tail-calls the shared FUN_062d20f0(this) -- see
    // IdleBehavior::Disengage() for why only
    // the unconditional Flag29 clear from that shared tail is reproduced.
    void Disengage();

    // Own vtable slot 5 (offset 0x14). Common.dll.txt 192026-192072
    // (FUN_062c60f0), 40 instructions. Real, mostly self-contained control
    // flow: if the StateTimer (HeadAwayElapsed) hasn't reached MaxTimeToHeadAway yet, checks
    // two NavHelper-nested-handle conditions (FUN_062ecb20/FUN_062ed5a0/
    // FUN_062e6200, all FULLPORT-DEFERRED -- unidentified classes) to decide
    // whether a candidate is ready (mode=1) or not (mode=2, retail's own safe
    // default while those checks can't be reproduced); else resets the timer.
    // Flag29 forces mode=3 unconditionally, matching the family's
    // "SetModeFromFlags" naming convention.
    int EvaluateReadiness();

    // ------------------------------------------------------------------
    // Own vtable slot 7 (offset 0x1c), Update/dispatch entry point.
    // Common.dll.txt 192187-192974 (LAB_062c62a0), 741 instructions, NO
    // Ghidra decompile -- hand-disassembled from the raw listing only.
    //
    // REWRITTEN 2026-09-17 (claude:common-buzzbehavior). The prior pass's
    // description of this function (kept below for its still-correct parts)
    // contained four outright control-flow errors that this pass found by
    // re-tracing the raw bytes end to end, and they are all fixed in the .cpp:
    //
    //  (1) THE TIMER SECTION IS NOT UNCONDITIONAL. LAB_062c6705's
    //      `HeadAwayElapsed += DeltaTime` block is reached only by falling out of the
    //      FIRST HeadTowardStyle switch. The two BuzzState-dispatch paths that skip it
    //      (`JNZ LAB_062c672b` at 062c646f when BuzzState != 0, and the
    //      `JZ/JMP LAB_062c672b` pair at 062c64fa/062c650b that ends the
    //      BuzzState 0 -> 1 head-toward commit) jump PAST it to 062c672b. The
    //      prior port ran it every tick -- a real, observable behavioral bug
    //      (the state timer advanced on ticks retail leaves it alone, so
    //      Result flipped to 2 early).
    //
    //  (2) THE FIRST SWITCH'S CASE-0 GATE HAD INVERTED MEANING. The idiom at
    //      062c659b is `TEST AH,0x44; JP`, which is MSVC's EQUALITY test (only
    //      the "equal" FCOMP outcome leaves AH & 0x44 == 0x40, the single set
    //      bit that clears PF) -- so the branch is `NavHelper[0x1638] != 0.0f`,
    //      not the `< 0.0f` the prior port coded. Independently cross-checked
    //      against the SECOND switch's case 0, which tests the same field with
    //      the opposite mnemonic (`JNP` at 062c6b07) and must therefore reach
    //      the same "fate = 3 finalize" body on the same condition -- it does,
    //      which would be impossible under the `< 0.0f` reading.
    //
    //  (3) THE "IMMEDIATE DIRECTION" FAST PATH IS NOT AN EARLY RETURN. Both of
    //      its exits (062c6415/062c6425) jump to LAB_062c6cda, the SHARED tail
    //      -- so the NavHelper timeout check still runs on that path. The prior
    //      comment claimed it "skips everything below, including the tail".
    //
    //  (4) PassByStyle case 1's second vector argument is `{0, 0, PassByRollThrottle}`, not
    //      `{PassByRollThrottle, 0, 0}` -- the FSTP at 062c6bed lands on the THIRD float
    //      of the stack temporary (the two preceding dword stores zero the
    //      first two), Common.dll.txt 192862-192868.
    //
    // Also newly wired for real this pass, each having previously been a
    // FULLPORT-DEFERRED comment: `IObjAffect::get_formation_follower_count()` (slot 8) on the owner's +4
    // sub-object (062c6475-062c647e -- common_iface.h declares the slot);
    // every WatcherC tick/commit (AfterburnerIdea, see the class comment);
    // MoveIdea::SetScale40/SetDirection/SetDirectionFrom on WatcherA; the
    // WatcherD/WatcherE "fate" field writes; FaceIdea's own
    // AngleWeight8c/ScaleDefault1_94/FieldA0/Field28 writes on WatcherB; the
    // three NavHelper byte-flag probes (FUN_062e8810/8840/8850 all turn out to
    // be single-instruction `MOV AL,[ECX+disp]; RET` getters, so they are now
    // exact); and the candidate-angle metric itself, which is
    // common_ini.h's already-ported AngleToBasisZColumn3 (FUN_062e1e20).
    //
    // What the prior pass got RIGHT and this pass re-confirmed unchanged (see
    // class comment above and the marathon's "ground truth precedence" rule).
    // This is BY FAR the largest function in the BuzzBehavior cluster. The
    // outer STATE-MACHINE SHAPE (entry guard; the BuzzState 0/1/2 dispatch;
    // the "immediate direction" fast path gate; the two HeadTowardStyle 3-way
    // sub-dispatches and their own entry gates; the timer section; the
    // PassByStyle 3-way sub-dispatch; every timer-vs-threshold comparison that
    // reads only BuzzBehavior's own fields) is reproduced faithfully as real,
    // compiled control flow -- these conditions were decoded directly from
    // the x87 FCOM/FNSTSW/TEST-AH flag-mask idioms used at each site
    // (verified bit-by-bit against the C0/C2/C3 -> AH mapping, not guessed).
    // Every case body of both HeadTowardStyle switches, and of the PassByStyle switch,
    // is now REAL compiled code rather than prose -- see the .cpp. Correct as
    // before: no case body of either HeadTowardStyle switch writes a BuzzBehavior-own
    // field; every effect lands on WatcherA/B/C/D/E or on NavHelper.
    //
    // WHAT IS STILL GENUINELY OPEN, with its CURRENT reason (the older reasons
    // were disproved above and are deleted rather than restated):
    // UPDATE 2026-09-17 (claude:common-idea-internals): the first two entries
    // below rested on one shared premise -- "NavHelper[+0x14fc]'s own +0x84
    // sub-object, whose class is still unidentified anywhere in this port". That
    // premise is false. It is an `IObjInspect*`, proved from five independent
    // slot matches (see common_idea.h's MoveIdea::Update slot-1 comment for all
    // five, including the one that lands here: slot 0x2c takes (float&, Vector&)
    // and its Vector out-param is NavHelper+0x1508, which is this very header's
    // own "cached lead point" -- 0x14fc + 0xc == 0x1508). What survives of those
    // two entries is written out below; two of the four stubs are now real.
    //
    //   * ALL FOUR PROBES ARE NOW REAL CALLS (2026-09-17, claude:common-buzz-
    //     largeship), and the TODO above ("wiring ReadLeadDistance and
    //     ProbeCandidateInRadius to them is a mechanical change that wants the
    //     NavHelper `Elem` argument typed first") is discharged. The `Elem`
    //     type it was waiting on is FAiTarget (common_aisteer.h), and BOTH of
    //     this function's records are that type: NavHelper+0x14fc is one
    //     (item 1745 proved SystemManager+0x14FC is exactly this record) and
    //     retail builds the other on its own stack at [ESP+0x2c], inlining
    //     FUN_062d09b0 (item 1470) at .text:062c62b8-062c62e4 to do it.
    //     ProbeLeadAvailable/ReadLeadDistance/ProbeCandidateInRadius are
    //     deleted; Update() calls IsTargetCoAlignedAndAhead (item 1753),
    //     GetInterceptClearance (item 1750) and IsWithinInterceptRange (item
    //     1745) directly, passing the two records positionally exactly as
    //     retail pushes them. FetchNavHandle is deleted too -- FUN_062ed5a0 is
    //     item 1891, 23 instructions, TargetManager::GetTarget.
    //   * The two lazy position refreshes (NavHelper's own record at +0x14fc and
    //     the stack record) are retail's inlined FUN_062c47c0, i.e.
    //     FAiTarget::RefreshPos, guard byte +0x7E included (2026-10-08 cmn-ai10:
    //     the former RefreshNavLeadPoint helper dropped that guard).
    //     `FetchNavCandidateBasis` is `&Owner->get_transform().Rotation`.
    //     `FetchHandleBasis` is real as well now -- its stated
    //     reason ("its receiver comes from FetchNavHandle's own
    //     (NavHelper+0x142c) chain, which has nothing non-null to call yet")
    //     died with FetchNavHandle; the receiver is the stack record's own
    //     Iface, reloaded at .text:062c6354.
    //   * FaceIdea's +0x58 Vector, +0xad flag and +0xb0 Vector (retail
    //     FUN_062d75e0/FUN_062d7640) are CLOSED: common_idea.h names them
    //     Vector58 / bUpValid / UpB0 and exposes FaceIdea::SetAxis and
    //     FaceIdea::SetUpVector, and this file's Update() calls the latter for
    //     real instead of leaving a FULLPORT-DEFERRED marker. The whole
    //     FUN_062d7510/7560/7590/75c0/75e0/7610/7640 family is declared there
    //     with receiver proofs.
    //   * WatcherD and WatcherE are CLOSED (2026-09-17, claude:w19-buzz-retype).
    //     Both members are typed for real as DodgeIdea / BreakAwayIdea, so every
    //     call retail makes on them here is a real call: WatcherD's vtbl[4] tick
    //     is DodgeIdea::Update (LAB_062d7950),
    //     WatcherE's vtbl[4] tick at .text:062c6cbc is BreakAwayIdea::Update
    //     (LAB_062d8a50, ported in full as tracker item 1629 -- the "it is only
    //     `simplified`, no C++ body exists" reason this file carried was stale),
    //     and FUN_062d9450 at .text:062c6caf is BreakAwayIdea::SetWatchObject.
    //     All fourteen field setters write named fields (see the list with the
    //     private helpers below).
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

    // Slots 3/4/5 of retail's BuzzBehavior vftable (.rdata:0639f564), wired 2026-10-08
    // (claude-code:cmn-ai2): until then these three were plain members and IBehaviorManager's
    // Behavior* dispatch reached the base bodies. Slot 5 is `RET 4` (float timer unused).
    void Slot3() override { RecomputeApproach(); }             // 3 +0x0c LAB_062c5dd0
    void Slot4() override { Disengage(); }                     // 4 +0x10 FUN_062c60a0
    int Slot5(float) override { return EvaluateReadiness(); }  // 5 +0x14 FUN_062c60f0

private:
    // SetCruiseLock (FUN_062d21d0) is Behavior::SetCruiseLock now (common_behavior.h).
    // NOTE 2026-09-17: the two other stand-ins that used to live here,
    // `SetOffsetVector` (FUN_062d75c0) and `PlayApproachWarning`
    // (FUN_062d93c0), are gone -- both were disassembled this pass and turned
    // out to be trivial writes into named FaceIdea fields (a 9-dword basis
    // copy into ZeroBlock34 with FieldA0 = 4, and `AngleWeight8c = Value`
    // respectively). Their call sites now do the real thing; see the .cpp.
    // "PlayApproachWarning" was in any case an invented semantic name for a
    // one-instruction field store and is deliberately not carried forward.

    // NavHelper+0x1584.. is the SystemManager's pub::AI::Personality: the former
    // ReadNavFloat / ReadNavDword raw readers are gone, the call sites read
    // PersonalityData.BuzzHeadParams / BuzzPassParams / FormationParams by name.

    // LAB_062c66cb/LAB_062c66f2 -- retail's own shared "commit a fate to
    // WatcherD" epilogue, reached from five call sites across both HeadTowardStyle
    // switches. Writes +0xb4/+0x34/+0x38 then ticks WatcherD. Interval / IntervalVariance
    // are the personality's BuzzDodgeIntervalTime pair (+0x1664) or BuzzSlideIntervalTime
    // pair (+0x1670) -- formerly passed as a raw NavHelper offset.
    void ApplySteeringFate(int Fate, float Interval, float IntervalVariance);

    // Common.dll.txt 192075-192181 (FUN_062c6170), 100 instructions, called
    // twice from Update() (062c682d, 062c6b49). Points WatcherB (face) and WatcherA
    // (move) at the current target (TargetManager::GetTarget), ticks WatcherB, ticks or
    // commits WatcherC by IsAlignedAndOutsideHeadingCone, and steers WatcherA along the
    // own ship's -z axis. Writes no BuzzBehavior-own field.
    void AcquireLockEffects();

    // The three NavHelper+0x34 byte probes: the timer table's IsTimer0/5/4Active
    // (FUN_062e8810 / 062e8840 / 062e8850, each `MOV AL,byte ptr [ECX+disp]; RET`).
    static bool CheckNavHelperTimeout(SystemManager* NavHelper);    // FUN_062e8810(NavHelper+0x34)
    static bool CheckNavHelperFlagA(SystemManager* NavHelper);      // FUN_062e8840(NavHelper+0x34)
    static bool CheckNavHelperFlagB(SystemManager* NavHelper);      // FUN_062e8850(NavHelper+0x34)

    // The two "fetch a 3x3 basis" vcalls -- slot 3, get_transform(), whose
    // Transform begins with its 3x3 Matrix. Retail reads the result at +0x8,
    // +0x14 and +0x20 (column 2) and also block-copies all nine floats.
    static Matrix const& FetchHandleBasis(IObjInspect const* Handle);           // Record.Iface->vtbl[0xc]()
    static Matrix const& FetchNavCandidateBasis(SystemManager* NavHelper);      // NavHelper->SelfTarget.Iface->vtbl[0xc]()

    // ---- THE FOURTEEN RETAIL SETTER ADDRESSES ON WatcherD/WatcherE. Between
    // ---- Update() and RecomputeApproach() this file routes through fourteen
    // ---- distinct retail addresses on these two objects; every one is a bare
    // ---- store at a fixed offset (`MOV [ECX+disp],EAX; RET 4` for the dwords,
    // ---- `MOV [ECX+0xd8],AL; RET 4` for the one byte, four consecutive dword
    // ---- stores for FUN_062d76b0, and a Vector-plus-flag for FUN_062d88c0).
    // ---- ALL FOURTEEN were re-disassembled 2026-09-17 (claude:w19-buzz-retype)
    // ---- and cross-checked against common_idea.h's field map; TWELVE land on a
    // ---- declared field and are now written as plain assignments at the call
    // ---- sites, with no helper at all:
    // ----
    // ----   .text:062a66b0  [+0x3c]  -> DodgeIdea::ConeAngle
    // ----   .text:062a33e0  [+0x40]  -> DodgeIdea::ConeAngleVariancePercent
    // ----   FUN_062d88b0    [+0x44]  -> DodgeIdea::WaggleAxisConeAngle
    // ----   .text:062a33c0  [+0x48]  -> DodgeIdea::RollAngle
    // ----   FUN_062d8910    [+0x90]  -> DodgeIdea::TurnThrottle
    // ----   FUN_062d8950    [+0xd4]  -> DodgeIdea::VectorCc.z   (0xcc + 8)
    // ----   FUN_062d8960    [+0xd8]  -> DodgeIdea::bRollFlipDirection (the byte)
    // ----   FUN_062d8930    [+0xc8]  -> DodgeIdea::SlideThrottle
    // ----   .text:062d8920  [+0xb4]  -> DodgeIdea::DodgeStyle
    // ----   .text:063275d0  [+0x34]  -> DodgeIdea::IntervalTime / BreakAwayIdea::BreakStyle
    // ----   .text:063275f0  [+0x38]  -> DodgeIdea::IntervalTimeVariancePercent
    // ----   FUN_062d76b0    [+0x4/8/c/10 of the passed sub-object, which is
    // ----                    <Idea>+0x14] -> FDirectionPicker::WeightLeft/Right/Up/Down
    // ----                    on WatcherD's and on WatcherE's own Picker.
    // ----
    // ---- The remaining TWO (2026-10-08, claude-code:cmn-ai4) are now named too:
    // ----   FUN_062d9490    [+0x94]  -> DodgeIdea::Throttle / BreakAwayIdea::TurnThrottle
    // ----   FUN_062d88c0    [+0x58]  -> DodgeIdea::SetField58 (Vector + Field64 = 1)

    // Candidate-relative angular metric, .text:062c68bf-062c68de and its two
    // twins: retail's `FUN_062e1e20(Basis, &NavRecord.CachedPos, &CandidatePos)`
    // is common_ini.h's AngleToBasisZColumn3; Basis is NavRecord.Iface->
    // get_transform(), fetched after both pointer pushes. Returns the unrounded
    // ST0 value the three FCOMP sites compare.
    static double ReadCandidateMetric(SystemManager* NavHelper, Vector const& CandidatePos);

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Everything else is
    // Opaque padding. See the class comment above for the +0x08..+0x29
    // "context + flags" prefix derivation (by analogy with IdleBehavior,
    // confirmed via RecomputeApproach()'s FUN_062d20d0 tail-call) and the
    // five Watcher sub-object derivations. Struct sized to the largest
    // confirmed field (HeadTowardTurnThrottle @ +0x350, ends +0x354).
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.      // +0x08. Same role/offset as IdleBehavior::CachedHandle08 /
                                // DockBehaviorFields::CachedHandle08 (shared FUN_062d2090 initializer).
    int ModeSelector0xc;        // +0x0c. FUN_062d20d0 sets =1; EvaluateReadiness() also writes it.
    int UpdateResult10;         // +0x10. Update()'s own tri-state result (EDI at return); FUN_062d20d0 sets =1.
    void* OwnerContext;         // +0x14. Set by SetContext() (Param1); null-checked by Update().
    IObjDB* ObjectDb;           // +0x18, formerly Opaque18. Written only by Behavior::Slot2
                                // (.text:0629afd0, the manager's IObjDB; see common_behavior.h).
    float DeltaTime;            // +0x1c. Zeroed by FUN_062d20d0; read constantly by Update().
    // +0x20. Same role/offset as IdleBehavior::BehaviorMgrPtr20 (shared initializer).
    //
    // LEFT `void*` DELIBERATELY. SEARCHED AND NOT FOUND, 2026-09-17
    // (claude:common-behavior-fields) -- a recorded negative result, not an unexamined field.
    // The lead is real: TrailBehavior's own +0x20 is PROVEN `IBehaviorManager*`
    // (.text:062cb094 `MOV ECX,dword ptr [EBP + 0x20]` into
    // `CALL IBehaviorManager::get_camera_level_status`), and FaceBehavior, GotoBehavior,
    // GuideBehavior and DelayBehavior were typed this same pass from their own such sites. The
    // shared writer FUN_062d2090 does NOT carry it across -- fourteen SetContext() bodies call
    // it, so Param2's type comes from outside and must be established per class.
    // WHAT WAS SEARCHED: every instruction of BuzzBehavior's own methods (.text:062c5af0 ctor,
    // 062c5c00/062c5c40 dtors, 062c5cf0 SetContext, 062c5dd0 RecomputeApproach, 062c60a0
    // Disengage, 062c60f0 EvaluateReadiness, 062c6170 AcquireLockEffects, 062c62a0 Update --
    // .text:062c5af0..062c6d30) scanned for `[reg + 0x20]`. Every hit is one of three things,
    // none of them this field: ESP-relative stack scratch; a vtable dispatch on a DIFFERENT
    // object (`CALL dword ptr [EAX + 0x20]` at .text:062c6192, `CALL dword ptr [EDX + 0x20]` at
    // .text:062c6238 and .text:062c647e -- slot 8 on an owner/IObjAffect, not a field read);
    // or `FLD float ptr [EAX + 0x20]` on a NavHelper-derived block (.text:062c6260,
    // .text:062c65d7, .text:062c6c47). There is no `MOV reg,[this+0x20]` and no
    // `[[this+0x20]+disp]` chain anywhere in the class -- Buzz reads its manager back only
    // through the shared FUN_062d20f0 Disengage() tail, which a dozen unrelated classes share.
    // WHAT WOULD CLOSE IT: a site that loads +0x20 into ECX ahead of a call, or walks it to a
    // confirmed IBehaviorManager offset (+0x08 Sys, +0xc0 CurrentBehaviorIndex, +0xc4,
    // +0x110 ContentCbPtr, +0x118/+0x11c, +0x164 TurnSensitivity, +0x174 dispatch table).
    void* BehaviorMgrPtr;
    SystemManager* NavHelper;   // +0x24. Set by SetContext() (Param3). See class comment -- read at
                                 // many raw offsets throughout Update()/RecomputeApproach().
    unsigned char Flags28;      // +0x28. Bit 0x1 cleared by FUN_062d20d0.
    unsigned char Flag29;       // +0x29. Forces EvaluateReadiness() mode=3 when set.
    unsigned char Pad2a[0x2c - 0x2a];

    MoveIdea WatcherA;  // +0x2c.  Same class as DockBehavior/LaunchBehavior/LootBehavior/
                        // FaceBehavior's own SubObjectA -- see the class-comment's
                        // "WatcherA IDENTITY CONFLICT, RESOLVED" note above. Ctor FUN_062d4e60.
    FaceIdea WatcherB;  // +0xa4.  Same class as those siblings' own SubObjectB. Ctor FUN_062d6750.
    AfterburnerIdea WatcherC;  // +0x164. Ctor FUN_062da500. Typed for real 2026-09-17 --
                                // sizeof(AfterburnerIdea) == sizeof(BaseIdea) == 0x14 matches
                                // the measured 0x164..0x178 gap exactly; see the class
                                // comment's "WatcherC IDENTITY, RESOLVED" note.
    // +0x178, size 0xe0. Ctor FUN_062d77d0.
    //
    // IDENTIFIED 2026-09-17 (claude:common-idea-internals): this is a DodgeIdea,
    // published with its full layout and a real constructor in common_idea.h. The
    // vtable address this header has recorded since 2026-09-11 (PTR_LAB_063a04c8)
    // is the one whose RTTI Complete Object Locator, 063a9af4, names DodgeIdea in
    // that header's own sibling table -- the two facts just had not been put
    // together. Its Update() (LAB_062d7950) is DodgeIdea::Update in
    // common_dodgeidea.cpp, and its DirectionPicker second base at
    // +0x14 is published too, as FDirectionPicker.
    //
    // TYPED FOR REAL 2026-09-17 (claude:w19-buzz-retype). sizeof(DodgeIdea) is
    // asserted 0xe0 in common_idea.h and the member span here is 0x178..0x258,
    // also 0xe0 -- so the retype introduces no padding and every offsetof assert
    // below still holds.
    DodgeIdea WatcherD;

    // +0x258, size 0x9c. Ctor FUN_062d8970.
    //
    // IDENTIFIED 2026-09-17 (claude:common-idea-internals): this is a
    // BreakAwayIdea (vtable PTR_LAB_063a04f8, COL 063a9b40), also published in
    // common_idea.h with a real constructor, a real Commit() and the same
    // FDirectionPicker base.
    //
    // The size note that stood here -- "confirmed only as an UPPER BOUND of 0x9c
    // ... no independent lower bound is confirmed" -- is CLOSED. The lower bound
    // is FUN_062d8970's own highest write, the dword at +0x98 (.text:062d89e0),
    // so the class is at least 0x9c; RecomputeApproach()'s +0x2f4 field puts the
    // upper bound at 0x9c as well. The two meet, so 0x9c is exact.
    //
    // TYPED FOR REAL 2026-09-17 (claude:w19-buzz-retype), same as WatcherD:
    // sizeof(BreakAwayIdea) is asserted 0x9c and the span 0x258..0x2f4 is 0x9c.
    BreakAwayIdea WatcherE;

    // +0x2f4 (ends +0x300). RandomVector(1.0f) result, written by
    // RecomputeApproach(). Not read by anything else this pass traced.
    Vector Field2f4;
    float Field300;             // +0x300. Set to 300.0f by SetContext().
    unsigned char Field304;     // +0x304. Set to 0 by both SetContext() and RecomputeApproach().
    unsigned char Pad305[0x308 - 0x305];
    float Field308;              // +0x308. Ctor-uninitialized (first real write is RecomputeApproach()'s
                                  // reset to 0); per-tick accumulator compared against Field30c in Update().
    float Field30c;               // +0x30c. Set to 5.0f by RecomputeApproach(). Threshold for Field308.
    float Field310;                // +0x310. Reset to 0 by RecomputeApproach(); accumulator compared
                                    // against Field314 in Update().

    float Field314;             // +0x314. Ctor 1.5f. Threshold for Field310.

    // Names below +0x314 come from the personality keys RecomputeApproach() (.text:062c5dd0)
    // copies in: NavHelper+0x1584 is the SystemManager's pub::AI::Personality, so +0x1618.. is
    // its [BuzzHeadTowardBlock] (BuzzHeadParams) and +0x1678.. its [BuzzPassByBlock]
    // (BuzzPassParams) -- the key names are Content's loader keys (common_pub.h).

    // +0x318, formerly Field318. The buzz stage, 0/1/2. Ctor 0; RecomputeApproach() and the
    // pass-by timeout reset it to 0. Proof of the three meanings (Update(), .text:062c62a0):
    //   0 = head away: its timer (HeadAwayElapsed) runs against MaxTimeToHeadAway and it is left
    //       when IsWithinInterceptRange(MinDistanceToHeadToward) fails;
    //   1 = head toward: entered at .text:062c64cd, where the formation notifications are gated
    //       by bBreakApartFormationOnBuzzHeadToward / bRegroupFormationOnBuzzHeadToward;
    //   2 = pass by: entered at .text:062c679f under the ...OnBuzzPassBy flags and
    //       bBuzzDropBombOnPassBy; its timer (PassByElapsed) runs against PassByTime.
    int BuzzState;
    // +0x31c, formerly Field31c. Weighted pick (.text:062c5e50-062c5eb6) over the
    // BuzzHeadTowardStyleWeight* keys: 0 = StraightTo, 1 = Slide, 2 = Waggle. Case 1 uses the
    // BuzzSlideIntervalTime pair, cases 0/2 the BuzzDodgeIntervalTime pair.
    int HeadTowardStyle;
    // +0x320, formerly Field320. Weighted pick (.text:062c5ebc-062c5f1b) over the
    // BuzzPassByStyleWeight* keys: 0 = BreakAway (ticks the BreakAwayIdea WatcherE),
    // 1 = StraightBy, 2 = EngineKill (ThrustManager::SetEngineActive(false)).
    int PassByStyle;
    // +0x324, formerly Field324. Ctor 500.0f; RecomputeApproach(): BuzzMinDistanceToHeadToward
    // scaled by (1 + r * BuzzMinDistanceToHeadTowardVariancePercent), r in [-1, 1).
    float MinDistanceToHeadToward;
    float Field328;                  // +0x328. Ctor 1.5f. No reader in .text:062c5af0..062c6d30.
    float DistanceToPassBy;          // +0x32c, formerly Field32c. Ctor 100.0f; BuzzDistanceToPassBy.
    float Field330;                  // +0x330. Ctor 5.0f. No reader in .text:062c5af0..062c6d30.
    float PassByTime;                // +0x334, formerly Field334. Ctor 3.0f; BuzzPassByTime.
    float PassByElapsed;             // +0x338, formerly Field338. Ctor 0. DeltaTime sum in stage 2,
                                     // compared against PassByTime (.text:062c6b5d-062c6b6c).
    float MaxTimeToHeadAway;         // +0x33c, formerly Field33c. Ctor 6.0f; BuzzMaxTimeToHeadAway.
    float HeadAwayElapsed;           // +0x340, formerly Field340. Ctor 0. DeltaTime sum in stage 0,
                                     // compared against MaxTimeToHeadAway (.text:062c670d,
                                     // EvaluateReadiness .text:062c60f3).
    float Field344;                  // +0x344. Set to 300.0f by RecomputeApproach(). The fast path runs
                                     // only while GetInterceptClearance(...) < Field344 (.text:062c6340).
    float PassByRollThrottle;        // +0x348, formerly Field348. Ctor 0; BuzzPassByRollThrottle (the
                                     // z of FaceIdea::SetAxis in PassByStyle 1).
    float HeadTowardEngineThrottle;  // +0x34c, formerly Field34c. Ctor 1.0f; BuzzHeadTowardEngineThrottle
                                     // (MoveIdea::SetScale40 in AcquireLockEffects()).
    float HeadTowardTurnThrottle;    // +0x350, formerly Field350. Ctor 1.0f; BuzzHeadTowardTurnThrottle
                                     // (FaceIdea::SetScale94 in AcquireLockEffects()).
};

static_assert(offsetof(BuzzBehavior, CachedHandle08) == 0x08, "BuzzBehavior::CachedHandle08 offset");
static_assert(offsetof(BuzzBehavior, ModeSelector0xc) == 0x0c, "BuzzBehavior::ModeSelector0xc offset");
static_assert(offsetof(BuzzBehavior, UpdateResult10) == 0x10, "BuzzBehavior::UpdateResult10 offset");
static_assert(offsetof(BuzzBehavior, OwnerContext) == 0x14, "BuzzBehavior::OwnerContext offset");
static_assert(offsetof(BuzzBehavior, ObjectDb) == 0x18, "BuzzBehavior::ObjectDb offset");
static_assert(offsetof(BuzzBehavior, DeltaTime) == 0x1c, "BuzzBehavior::DeltaTime offset");
static_assert(offsetof(BuzzBehavior, BehaviorMgrPtr) == 0x20, "BuzzBehavior::BehaviorMgrPtr offset");
static_assert(offsetof(BuzzBehavior, NavHelper) == 0x24, "BuzzBehavior::NavHelper offset");
static_assert(offsetof(BuzzBehavior, Flags28) == 0x28, "BuzzBehavior::Flags28 offset");
static_assert(offsetof(BuzzBehavior, Flag29) == 0x29, "BuzzBehavior::Flag29 offset");
static_assert(offsetof(BuzzBehavior, WatcherA) == 0x2c, "BuzzBehavior::WatcherA offset");
static_assert(offsetof(BuzzBehavior, WatcherB) == 0xa4, "BuzzBehavior::WatcherB offset");
static_assert(offsetof(BuzzBehavior, WatcherC) == 0x164, "BuzzBehavior::WatcherC offset");
static_assert(offsetof(BuzzBehavior, WatcherD) == 0x178, "BuzzBehavior::WatcherD offset");
static_assert(offsetof(BuzzBehavior, WatcherE) == 0x258, "BuzzBehavior::WatcherE offset");
static_assert(offsetof(BuzzBehavior, Field2f4) == 0x2f4, "BuzzBehavior::Field2f4 offset");
static_assert(offsetof(BuzzBehavior, Field300) == 0x300, "BuzzBehavior::Field300 offset");
static_assert(offsetof(BuzzBehavior, Field304) == 0x304, "BuzzBehavior::Field304 offset");
static_assert(offsetof(BuzzBehavior, Field308) == 0x308, "BuzzBehavior::Field308 offset");
static_assert(offsetof(BuzzBehavior, Field30c) == 0x30c, "BuzzBehavior::Field30c offset");
static_assert(offsetof(BuzzBehavior, Field310) == 0x310, "BuzzBehavior::Field310 offset");
static_assert(offsetof(BuzzBehavior, Field314) == 0x314, "BuzzBehavior::Field314 offset");
static_assert(offsetof(BuzzBehavior, BuzzState) == 0x318, "BuzzBehavior::BuzzState offset");
static_assert(offsetof(BuzzBehavior, HeadTowardStyle) == 0x31c, "BuzzBehavior::HeadTowardStyle offset");
static_assert(offsetof(BuzzBehavior, PassByStyle) == 0x320, "BuzzBehavior::PassByStyle offset");
static_assert(offsetof(BuzzBehavior, MinDistanceToHeadToward) == 0x324, "BuzzBehavior::MinDistanceToHeadToward offset");
static_assert(offsetof(BuzzBehavior, Field328) == 0x328, "BuzzBehavior::Field328 offset");
static_assert(offsetof(BuzzBehavior, DistanceToPassBy) == 0x32c, "BuzzBehavior::DistanceToPassBy offset");
static_assert(offsetof(BuzzBehavior, Field330) == 0x330, "BuzzBehavior::Field330 offset");
static_assert(offsetof(BuzzBehavior, PassByTime) == 0x334, "BuzzBehavior::PassByTime offset");
static_assert(offsetof(BuzzBehavior, PassByElapsed) == 0x338, "BuzzBehavior::PassByElapsed offset");
static_assert(offsetof(BuzzBehavior, MaxTimeToHeadAway) == 0x33c, "BuzzBehavior::MaxTimeToHeadAway offset");
static_assert(offsetof(BuzzBehavior, HeadAwayElapsed) == 0x340, "BuzzBehavior::HeadAwayElapsed offset");
static_assert(offsetof(BuzzBehavior, Field344) == 0x344, "BuzzBehavior::Field344 offset");
static_assert(offsetof(BuzzBehavior, PassByRollThrottle) == 0x348, "BuzzBehavior::PassByRollThrottle offset");
static_assert(offsetof(BuzzBehavior, HeadTowardEngineThrottle) == 0x34c, "BuzzBehavior::HeadTowardEngineThrottle offset");
static_assert(offsetof(BuzzBehavior, HeadTowardTurnThrottle) == 0x350, "BuzzBehavior::HeadTowardTurnThrottle offset");
static_assert(sizeof(BuzzBehavior) >= 0x354, "BuzzBehavior minimum size (largest field actually read/written)");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x354`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(BuzzBehavior) <= 0x354, "BuzzBehavior must not exceed its retail size 0x354");
