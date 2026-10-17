// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher, Watchable
#include "common_pub.h"       // pub::AI::DirectiveGotoOp, pub::AI::DirectiveFollowOp, pub::AI::DirectiveGuideOp

#include <cstddef>

// ============================================================================
// GuideBehavior -- AI "guide/escort a point-to-point course" state (Behavior family)
// ============================================================================
//
// RTTI identity (hand-decoded directly from EXE/Disasm/Common.dll.txt raw bytes, NOT from
// Ghidra's .c decompilation -- see "ground truth precedence" in this marathon's
// methodology). Already on file as decomp-tracker item 6609 (address 0x0639feb4):
//
//   RTTICompleteObjectLocator @ .rdata:0x063a91e4 (offset field = 0, i.e. the primary --
//   and only -- vtable installs at object offset 0)
//     -> pTypeDescriptor, name ".?AVGuideBehavior@@"
//     -> pClassHierarchyDescriptor, numBaseClasses = 2, adjacent CHD sibling is
//        LargeShipBehavior (see common_largeshipbehavior.h)
//
// Single-inheritance direct child of Behavior -- same shape as FleeBehavior/CruiseBehavior,
// no secondary vtable.
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062cf230 (Common.dll.txt 205669-205723). Calls the shared base-init
// field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h), zeroes WatcherA (+0x2c/+0x30, a BaseWatcher -- confirmed by the real
// dtor FUN_062cf360 treating +0x2c as a BaseWatcher via BaseWatcher::set_pointer, see below),
// default-constructs a REAL, already-ported `pub::AI::DirectiveGotoOp` (common_pub.h) at
// +0x68 and a REAL, already-ported `pub::AI::DirectiveFollowOp` (common_pub.h) at +0xd8
// (0xd8-0x68 == sizeof(DirectiveGotoOp) == 0x70, confirming back-to-back layout, same
// pattern FleeBehavior uses for its own GotoOp/DockOp pair), zeroes WatcherB (+0xfc/+0x100,
// a second BaseWatcher), installs the vtable, then sets THREE float defaults --
// +0x11c=0x3ec90fdb (0.39269909f, pi/8), +0x114=-10.0f, +0x118=10.0f -- followed by more
// field writes a prior pass's summary did NOT capture (re-verified this pass by reading the
// raw bytes end to end): zeroes +0x108/+0x44/+0x34, sets +0x104=10.0f, +0x38=1.
//
// CONFIRMED DEAD CODE (matching this marathon's LootBehavior ctor precedent, common_lootbehavior.h):
// right after installing the vtable and the three float defaults, retail emits
// `CMP dword ptr [this+0xfc],0; JZ skip; CALL BaseWatcher::set_pointer(this+0xfc, nullptr)`.
// The compared dword was JUST zeroed by this SAME ctor a few instructions earlier (WatcherB's
// own zero-init) and nothing in between writes it, so the branch always takes the
// "already null" path -- the BaseWatcher::set_pointer call is unreachable. Not reproduced.
// ============================================================================

class GuideBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (14, "Guide"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit GuideBehavior(int InTypeId);
    ~GuideBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639feb4, 27 slots, 0x0639feb4-0x0639ff1c inclusive).
    // CORRECTED this pass: the tracker item's byte_size=112 (112/4=28) is NOT the real slot
    // count -- the 28th dword at 0x0639ff20 is LargeShipBehavior's own leading RTTI COL
    // pointer (DAT_063a922c, the hidden [-4] slot preceding PTR_FUN_0639ff24), not a
    // GuideBehavior vtable entry. Re-verified directly against the raw .rdata listing
    // (Common.dll.txt 563239-563267): GuideBehavior's real vtable is the standard
    // family-wide 27-slot shape (matches FleeBehavior/LaunchBehavior/LootBehavior).
    //
    //   0  FUN_062cf340  scalar-deleting dtor around FUN_062cf360
    //   1  LAB_062cf3e0  SetContext (`JMP FUN_062d2090`)
    //   3  LAB_062cf3f0  Slot3 (engage)
    //   4  FUN_062cf4f0  Slot4 (disengage)
    //   5  FUN_062cf530  Slot5
    //   6  LAB_062cf5e0  Behavior's own evaluate body (`MOV EAX,2; RET 4`), inherited
    //   7  LAB_062cf5f0  Slot7 -> Update()
    //  10  LAB_062cfc70  Slot10 (apply a DirectiveGuideOp)
    //  21  LAB_062cf300  Slot21 (shared with BaseFollowerBehavior)
    //  24  LAB_062cf2f0  get_target
    // Every other slot is Behavior's own entry.
    // ------------------------------------------------------------------

    // RE-PORTED 2026-10-08 (claude-code:cmn-ai5): slots 1/3/4/5/10/21/24 are overrides now.
    // Until then slots 3 and 10 were unported and slots 1/4/5/21/24 were plain members that
    // the manager's Behavior* dispatch never reached.

    // Slot 1, LAB_062cf3e0: `JMP FUN_062d2090` (BindContext), shared with Flee/Cruise/
    // DrasticEvade.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, LAB_062cf3f0 (Engage): re-arm, capture the manager's Goto (+0x17c) and Follow
    // (+0x1b8) behaviors, hand them GotoOp/FollowOp and engage both.
    void Slot3() override;

    // Slot 4, FUN_062cf4f0: disengage both siblings and release their directive bit.
    void Slot4() override;

    // Slot 5, FUN_062cf530 (RET 4, argument unread).
    int Slot5(float Value) override;

    // Slot 10, LAB_062cfc70 (RET 4): take a pub::AI::DirectiveGuideOp.
    bool Slot10(void* DirectiveOp) override;

    // ------------------------------------------------------------------
    // Own vtable slot 7 (offset 0x1c). LAB_062cf5f0, Common.dll.txt 206092-206730, 575
    // instructions. PORTED IN FULL 2026-09-17 (claude:common-guide); body lives in
    // src/Common/common_guidebehavior.cpp. It sat in src/Common/common_stubs.cpp until
    // 2026-09-17 (claude:w16-behavior) because FUN_062ec520 was file-static there; that
    // function is now non-static and declared at the bottom of this header, so the body is
    // back in its own class's translation unit. Derived instruction-by-instruction from the
    // raw listing.
    //
    // WHAT IT IS. This is the per-tick brain of the "guide/escort" behavior: an NPC leads
    // the watched object (WatcherA, in practice the player) along a course, hands the actual
    // flying to ONE OF TWO SIBLING Behavior objects owned by the manager, and emits a
    // numbered "guide event" to the manager's notification callback whenever the escort's
    // progress crosses a threshold.
    //
    // Structure, in retail order:
    //   1. 062cf5f0-062cf62e  Guards. No WatcherA -> clear Flags28 bit 0x2 and return
    //      UpdateResult10 = 3. No OwnerContextPtr14 (or, re-tested, no WatcherA) -> return 3
    //      WITHOUT touching Flags28. Retail re-reads +0x2c twice more; nothing in between can
    //      change it, so the port evaluates it once.
    //   2. 062cf630-062cf6bd  Refresh the guide destination Field48 from GotoOp.Field_0C
    //      (retail reads it at raw offset +0x74 == GotoOp+0x0C):
    //        0 -> WatcherB's world position via GetIfacePositionOrZero; if WatcherB is unset,
    //             clear Flags28 bit 0x2, NotifyBehaviorDone(0xe, 1) and return 2.
    //        1 -> a straight copy of GotoOp.Destination (raw +0x78 == GotoOp+0x10).
    //        anything else -> Field48 is left at whatever the last tick put there.
    //   3. 062cf6c0-062cf729  Field54 = GotoOp.Field_50 (raw +0xb8); then three steering
    //      queries on the NavHelperPtr24+0x14fc element: Clearance (to WatcherA),
    //      RadialClearance (to Field48) and Dist (WatcherA position to Field48).
    //   4. 062cf74e-062cf7ef  Two independent "is the escorted object actually mine" tests,
    //      either of which clears Flag120: (a) WatcherA's get_behavior_id() == 10 and its
    //      get_formation_leader() has the same object id as the NavHelper element's own +0x84
    //      ship -- this one also latches bLeaderIsSelf; (b) WatcherA's id appears in the
    //      +0x1364 sub-object's formation-followers list (FUN_062ec520).
    //   5. 062cf7f1-062cf8b2  Field108 accumulates DeltaTime1c until it passes Field104
    //      (ctor default 10.0f). On overflow it samples: Field58 = previous Field10c - Dist
    //      (distance closed since the last sample, positive == closing), Field5c =
    //      acos(clamped dot of the escorted ship's negated forward axis with the normalized
    //      Field48-minus-its-own-position delta) == how far off-course it is pointing, then
    //      Field10c = Dist and Field108 = 0. Only on a sampling tick does step 6 run.
    //   6. 062cf8b4-062cfba5  The event table. Two mirror-image halves selected by Flag120:
    //        Flag120 set  (following): 8 = fell behind hard (Field58 < FieldLow114),
    //                                  6 = caught up hard but pointing away (Field58 >
    //                                      FieldHigh118 and Field5c > AngleThreshold11c),
    //                                  5 = caught up hard and pointing the right way,
    //                                  7 = neither, but Clearance dropped below Field44.
    //                                  8/6/7 also clear Flag120; 5 does not.
    //        Flag120 clear (leading):  3 = fell behind hard, 1 = caught up and on course and
    //                                  inside Field3c, 2 = steady but outside Field3c,
    //                                  4 = the "leader is me" shortcut. 1 and 2 set Flag120.
    //      The Dist < RadialClearance byte gates them unevenly, and the asymmetry is retail's,
    //      not a transcription slip: 8/6/7/3/2 are SUPPRESSED when it is set, 4's second entry
    //      (via 062cfb2d) REQUIRES it, and 1, 5 and 4's bLeaderIsSelf entry ignore it entirely.
    //      Each event is a NotifyManagerGuideEvent(SelfId, WatchedId, Code, Clearance, Field48).
    //   7. 062cfa15 / 062cfba7  Hand off to a sibling: Flag120 set -> CachedPtr64 drives and
    //      CachedPtr60 is parked; Flag120 clear -> the reverse. A transition disengages the
    //      outgoing one (slot 4), pushes this class's matching directive struct into the
    //      incoming one (slot 10) and engages it (slot 3); no transition just writes
    //      DeltaTime1c into the active one's own +0x1c and ticks it (slot 7).
    //   8. 062cfbc7-062cfc66  Termination test: if the radial clearance to Field48 exceeds
    //      Field54, or the intercept clearance to WatcherA exceeds GotoOp.Field_60 (raw
    //      +0xc8), the directive is still running -> UpdateResult10 = 1. Otherwise clear
    //      Flags28 bit 0x2, NotifyBehaviorDone(0xe, 0) and finish with UpdateResult10 = 2.
    //      (CachedPtr60's slot-7 result == 2 skips straight to the intercept test.)
    //
    // FP-idiom care taken (this marathon's own standing warning): every FCOMP here was decoded
    // from its mask AND its jump, not from the mask alone. `TEST AH,0x41` + `JNZ` guards a
    // strictly-greater test (fall-through is greater); `TEST AH,0x41` + `JP` JUMPS on greater;
    // `TEST AH,0x5` + `JP` jumps on NOT-less. The pair at 062cfab4-062cfad0 interleaves TWO
    // compares through one AH: the first FNSTSW feeds the `TEST AH,0x41`/`JNZ`, the second
    // (which does not touch EFLAGS) feeds the following `TEST AH,0x5`/`JP` and, at 062cfb3d,
    // a `TEST AH,0x1` reading C0 of that same second compare.
    //
    // Two retail oddities reproduced/recorded rather than "fixed": the `TEST ECX,ECX; JNZ`
    // at 062cf66d is dead (ECX was already proven zero at 062cf635), and event 5's arm is the
    // only one that skips the Dist<RadialClearance suppression AND leaves Flag120 alone.
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
    // ------------------------------------------------------------------

    // Slot 21, LAB_062cf300 (RET 4), the same body as BaseFollowerBehavior's slot 21 on this
    // class's own +0x2c: release WatcherA when its object id equals the argument.
    void Slot21(void* Arg) override;

    // Slot 24, LAB_062cf2f0: WatcherA's object (target - 8) or null.
    IObjRW const* get_target() override;

private:
    // FULLPORT-DEFERRED stand-ins for not-yet-ported cross-cutting retail helpers this
    // class's ported methods route through by name/role (shared with several other siblings
    // in this family -- see the DockBehavior/IdleBehavior/LaunchBehavior precedents cited in
    // the method comments above for each one's confirmed effect elsewhere).
    // ReleaseCachedDirectivePoint (FUN_062d21c0) is GONE 2026-09-17: it is
    // Behavior_ReleaseDirectiveTarget in common_behavior.h, shared with the two
    // DockBehavior sites that had the same body under a different guessed name.


    // FUN_062d2a70, decomp-tracker item 1551, Common.dll.txt 211770-211808 (28 insns),
    // `__thiscall bool(IBehaviorManager*, uint, uint, int, float, Vector const*)`, RET 0x14 --
    // the lead-event sibling of IBehaviorManager::NotifyFollowEvent (062d29c0) and
    // NotifyTetherEvent (062d2a10). ALL FOUR of its XREFs (062cf9b3/062cfa09/062cfb1f/062cfba2)
    // are inside Update() above. Retail: false unless Sys->Targeting.AttackState (+0x1434) is
    // nonzero and the current behavior index (+0xc0) is 0xe (Guide's own dispatch slot); then
    // ContentCbPtr (+0x110) slot 2 lead_object_event with the five arguments and true, or, with
    // no callback, Sys->ExternalIface->is_player() (+0xbc) for effect and false.
    static bool NotifyManagerGuideEvent(IBehaviorManager* BehaviorMgr, unsigned SelfId, unsigned WatchedId,
                                        int EventCode, float Clearance, Vector const* Point);

    // The eight guide-event call sites in Update() are byte-identical apart from the event
    // code, so they are factored into this one helper. It reproduces retail's own evaluation
    // ORDER as well as its arguments: the NavHelper element's own +0x84 ship is asked for its
    // id FIRST (retail parks it in EBX), then the escorted object, then the manager call --
    // which matters because both are virtual calls on live game objects, not pure getters the
    // compiler may reorder.
    void FireGuideEvent(IObjRW* Watched, FAiTarget* NavElem, int EventCode, float Clearance);

public:
    // ------------------------------------------------------------------
    // Confirmed fields, ascending absolute-offset order. Same shared context-prefix shape as
    // every other sibling (own data starts right after the inherited vfptr @ +0x00), plus two
    // real embedded pub::AI::Directive*Op sub-objects and a handful of GuideBehavior-own
    // fields the ctor touches directly. Not sizeof()-asserted.
    //
    // 2026-09-17 (claude:common-guide): the padding runs this list used to carry at +0x18,
    // +0x39 and +0x48 are now typed out, because Update() reads and writes every one of the
    // fields inside them. The old note "+0x48/+0x54/+0x74/+0x78/+0x120 ... not individually
    // typed" no longer applies: +0x74 and +0x78 were never GuideBehavior fields at all (they
    // are GotoOp.Field_0C and GotoOp.Destination, reached through the embed), and the rest
    // are named below.
    // ------------------------------------------------------------------
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here, taken
                 // from this class's own ctor argument -- see Behavior::GetTypeId() (common_behavior.h)
                 // for the twenty-one-factory evidence.
    unsigned CachedHandle08;  // retyped 2026-09-16: this is IObjInspect::get_id()'s result,
                              // an object id, not a handle. Retail slot 8 is `?get_id@IObjInspectImpl@@UBE?BIXZ`.      // +0x08. Set by SetContext() via the shared FUN_062d2090 effect.
    int ModeSelector0xc;       // +0x0c. Written by the GAP'd Engage() (=1) and SetModeFromFlags() (0/2/3).
    int UpdateResult10;        // +0x10. The GAP'd Update()'s own tri-state result.
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1).
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime1c;         // +0x1c. Zeroed by the shared FUN_062d20d0 reset effect inside the
                                // GAP'd Engage(). Update() accumulates it into Field108 and copies
                                // it into whichever of CachedPtr60/64 is driving, at that object's
                                // OWN +0x1c -- the same per-tick delta slot BaseFollowerBehavior
                                // already names DeltaTime at +0x1c (common_behavior.h), which is
                                // what makes the float typing here evidence rather than a guess.
    // +0x20. Set by SetContext() (Param2). TYPED 2026-09-17 (claude:common-behavior-fields)
    // from GuideBehavior's OWN call sites, not by carrying TrailBehavior's proof across:
    //   * .text:062cf9ae/062cfa04/062cfb1a/062cfb9d -- `MOV ECX,dword ptr [ESI + 0x20]`
    //     immediately followed by `CALL FUN_062d2a70`, i.e. the field goes straight into ECX as
    //     a __thiscall receiver. FUN_062d2a70's own body opens `MOV EAX,[ECX+0x8];
    //     MOV EDX,[EAX+0x1434]` and then `CMP dword ptr [ECX+0xc0],0xe` -- +0x08 is the field
    //     the NAMED retail symbol `IBehaviorManager::get_scan_response` dereferences the same
    //     way (`MOV ECX,[ECX+0x8]; ADD ECX,0x142c`), and +0xc0 is the field the NAMED symbols
    //     `update_current_behavior_throttle` / `reset_current_behavior_direction` /
    //     `update_current_behavior_cruise` all read as the current-behavior index. Two
    //     independent confirmed IBehaviorManager offsets in one eight-instruction prologue.
    //   * .text:062cf56a/062cf64b/062cfc35 -- same `MOV ECX,[ESI+0x20]` shape into
    //     `CALL FUN_062d29a0`, which writes +0x118/+0x11c: exactly the pair the big
    //     IBehaviorManager tick function writes on its OWN `this` at .text:062d2ea9-062d2eb3.
    //   * .text:062cf3fc -- `MOV EAX,[ESI+0x20]; MOV ECX,[EAX+0x17c]; MOV EDX,[EAX+0x1b8]`,
    //     entries 2 and 18 of IBehaviorManager::BehaviorDispatchTable (+0x174, common_iface.h).
    // The shared writer FUN_062d2090 was deliberately NOT used as evidence (fourteen callers,
    // so Param2's type has to come from each class's own dereferences, as here).
    IBehaviorManager* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3).
    unsigned char Flags28;     // +0x28. Tested by SetModeFromFlags().
    unsigned char Flag29;      // +0x29. Tested by SetModeFromFlags().
    unsigned char Pad2a[0x2c - 0x2a];

    BaseWatcher WatcherA;       // +0x2c (8 bytes, ends +0x34). Zeroed by ctor; cleared by dtor;
                                 // read by SetModeFromFlags()/ReleaseWatcherAIfIdMatches()/GetRawBlockA().
    float Field34;               // +0x34. Ctor 0. Slot10 copies DirectiveGuideOp+0xb8; Slot5 hands
                                  // it to FUN_062e4500 as the float range (062cf586), hence float.
    unsigned char Flag38;        // +0x38. Set to 1 by ctor. The GAP'd ApplyDirective() overwrites it
                                  // from DirectiveGuideOp::Field_A0 (retail default 1). Also read
                                  // (copied to +0x120) by the GAP'd Engage()/Update().
    unsigned char Pad39[0x3c - 0x39];
    float Field3c;                // +0x3c. Set by the GAP'd ApplyDirective() from
                                   // DirectiveGuideOp::Field_6C (raw dword copy at 062cfd62). Update()
                                   // FCOMPs the intercept clearance against it at 062cfac3 -- that
                                   // read is what types it float.
    unsigned Field40;             // +0x40. Set by the GAP'd ApplyDirective() from
                                   // DirectiveGuideOp::Field_70 (062cfd68). Never read by any ported
                                   // code; width is all that is proven. TODO: semantic role unknown.
    float Field44;                // +0x44. Zeroed by ctor. Conditionally written by the GAP'd
                                   // Engage()/ApplyDirective() from an FPU vector-magnitude result.
                                   // Update() FCOMPs the intercept clearance against it (062cf9be).
    Vector Field48;               // +0x48..+0x54. The live "guide destination" point. Update()
                                   // refreshes it each tick (062cf637-062cf6bd) either from WatcherB's
                                   // world position or from GotoOp.Destination, then uses it as the
                                   // reference point for every clearance/distance query and passes
                                   // its address as the 5th argument of every guide event.
    float Field54;                // +0x54. Update() reloads it from GotoOp.Field_50 every tick
                                   // (062cf6c0, a raw dword copy from +0xb8) and compares the radial
                                   // clearance against it to decide whether the directive is done.
    float Field58;                // +0x58. Written only on a sampling tick (062cf81a): the previous
                                   // Field10c minus the current distance, i.e. how much ground was
                                   // closed since the last sample. Drives the guide event table.
    float Field5c;                // +0x5c. Written only on a sampling tick (062cf894): the acos of
                                   // the clamped dot between the escorted ship's negated forward axis
                                   // and the normalized delta to Field48 -- an angle in radians,
                                   // compared against AngleThreshold11c.
    // +0x60/+0x64. Set by the GAP'd Engage() from BehaviorMgrPtr20's own +0x17c/+0x1b8.
    // IDENTIFIED 2026-09-17 (claude:common-guide), previously "vtable-having, real class not
    // identified": IBehaviorManager's own +0x174 is BehaviorDispatchTable[21] (common_iface.h),
    // so +0x17c is entry 2 (Goto) and +0x1b8 is entry 17 (Follow; (0x1b8 - 0x174) / 4 == 17,
    // corrected 2026-10-08 from "18") -- both are plain sibling `Behavior*`.
    // Confirmed by every slot Update() touches lining up with the Behavior vtable this class
    // itself implements: 0x0c == slot 3 (engage/reset), 0x10 == slot 4 (disengage), 0x1c ==
    // slot 7 (Update), 0x28 == slot 10 (apply directive) -- plus each object's own +0x1c
    // per-tick delta float. What settles WHICH siblings they are is the argument Update()
    // hands slot 10: CachedPtr60 always receives &GotoOp and CachedPtr64 always receives
    // &FollowOp, and the two are engaged in strict alternation by Flag120.
    Behavior* CachedPtr60;
    Behavior* CachedPtr64;

    pub::AI::DirectiveGotoOp GotoOp;    // +0x68. Real, already-ported type (common_pub.h).
                                          // Ends at +0xd8 (sizeof(DirectiveGotoOp) == 0x70).
    pub::AI::DirectiveFollowOp FollowOp; // +0xd8. Real, already-ported type (common_pub.h).
                                          // Ends at +0xfc (sizeof(DirectiveFollowOp) == 0x24).

    BaseWatcher WatcherB;        // +0xfc (8 bytes, ends +0x104). Zeroed by ctor; cleared by dtor.
    float Field104;               // +0x104. Set to 10.0f by ctor. The GAP'd ApplyDirective()
                                   // overwrites it from DirectiveGuideOp::Field_A4 (retail default 10.0).
    float Field108;                // +0x108. Zeroed by ctor. RETYPED int -> float 2026-09-17: the
                                    // ctor's integer zero store is just `0.0f`'s bit pattern, and
                                    // Update() genuinely does `FLD [+0x1c]; FADD [+0x108]; FSTP
                                    // [+0x108]` (062cf8a5) on it. A sampling-period accumulator.
    float Field10c;                 // +0x10c. Not touched by ctor. Update()'s cached "distance to
                                     // Field48 as of the last sampling tick".
    float Field110;                  // +0x110. Not touched by ctor. The GAP'd ApplyDirective() sets
                                      // it from `(float)DirectiveGuideOp::Field_AC` (retail default 3).
    float FieldLow114;                // +0x114. Set to -10.0f by ctor. The GAP'd ApplyDirective()
                                       // overwrites it from DirectiveGuideOp::Field_B4 (retail default -10.0).
    float FieldHigh118;                // +0x118. Set to 10.0f by ctor. The GAP'd ApplyDirective()
                                        // overwrites it from DirectiveGuideOp::Field_B0 (retail default 10.0).
    float AngleThreshold11c;            // +0x11c. Set to 0.39269909f (pi/8) by ctor. The GAP'd
                                         // ApplyDirective() overwrites it from DirectiveGuideOp::Field_A8
                                         // (retail default 0.3927, the SAME angle).
    unsigned char Flag120;               // +0x120. ADDED 2026-09-17 -- the class used to end at +0x120
                                          // while Update() reads and writes this byte eleven times
                                          // (062cf7b8/062cf7ea/062cf8b4/062cfa0e/062cfa15/062cfb24/
                                          // 062cfba7 ...). It is the "escort is following me" latch
                                          // that selects which of CachedPtr60/CachedPtr64 drives.
                                          // Seeded from Flag38 by the GAP'd Engage().
};

static_assert(offsetof(GuideBehavior, CachedHandle08) == 0x08, "GuideBehavior::CachedHandle08 offset");
static_assert(offsetof(GuideBehavior, ModeSelector0xc) == 0x0c, "GuideBehavior::ModeSelector0xc offset");
static_assert(offsetof(GuideBehavior, UpdateResult10) == 0x10, "GuideBehavior::UpdateResult10 offset");
static_assert(offsetof(GuideBehavior, OwnerContextPtr14) == 0x14, "GuideBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(GuideBehavior, DeltaTime1c) == 0x1c, "GuideBehavior::DeltaTime1c offset");
static_assert(offsetof(GuideBehavior, BehaviorMgrPtr20) == 0x20, "GuideBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(GuideBehavior, NavHelperPtr24) == 0x24, "GuideBehavior::NavHelperPtr24 offset");
static_assert(offsetof(GuideBehavior, Flags28) == 0x28, "GuideBehavior::Flags28 offset");
static_assert(offsetof(GuideBehavior, Flag29) == 0x29, "GuideBehavior::Flag29 offset");
static_assert(offsetof(GuideBehavior, WatcherA) == 0x2c, "GuideBehavior::WatcherA offset");
static_assert(offsetof(GuideBehavior, Field34) == 0x34, "GuideBehavior::Field34 offset");
static_assert(offsetof(GuideBehavior, Flag38) == 0x38, "GuideBehavior::Flag38 offset");
static_assert(offsetof(GuideBehavior, Field3c) == 0x3c, "GuideBehavior::Field3c offset");
static_assert(offsetof(GuideBehavior, Field40) == 0x40, "GuideBehavior::Field40 offset");
static_assert(offsetof(GuideBehavior, Field44) == 0x44, "GuideBehavior::Field44 offset");
static_assert(offsetof(GuideBehavior, Field48) == 0x48, "GuideBehavior::Field48 offset");
static_assert(offsetof(GuideBehavior, Field54) == 0x54, "GuideBehavior::Field54 offset");
static_assert(offsetof(GuideBehavior, Field58) == 0x58, "GuideBehavior::Field58 offset");
static_assert(offsetof(GuideBehavior, Field5c) == 0x5c, "GuideBehavior::Field5c offset");
static_assert(offsetof(GuideBehavior, CachedPtr60) == 0x60, "GuideBehavior::CachedPtr60 offset");
static_assert(offsetof(GuideBehavior, CachedPtr64) == 0x64, "GuideBehavior::CachedPtr64 offset");
static_assert(offsetof(GuideBehavior, GotoOp) == 0x68, "GuideBehavior::GotoOp offset");
static_assert(offsetof(GuideBehavior, FollowOp) == 0xd8, "GuideBehavior::FollowOp offset");
static_assert(offsetof(GuideBehavior, WatcherB) == 0xfc, "GuideBehavior::WatcherB offset");
static_assert(offsetof(GuideBehavior, Field104) == 0x104, "GuideBehavior::Field104 offset");
static_assert(offsetof(GuideBehavior, Field108) == 0x108, "GuideBehavior::Field108 offset");
static_assert(offsetof(GuideBehavior, Field10c) == 0x10c, "GuideBehavior::Field10c offset");
static_assert(offsetof(GuideBehavior, Field110) == 0x110, "GuideBehavior::Field110 offset");
static_assert(offsetof(GuideBehavior, FieldLow114) == 0x114, "GuideBehavior::FieldLow114 offset");
static_assert(offsetof(GuideBehavior, FieldHigh118) == 0x118, "GuideBehavior::FieldHigh118 offset");
static_assert(offsetof(GuideBehavior, AngleThreshold11c) == 0x11c, "GuideBehavior::AngleThreshold11c offset");
static_assert(offsetof(GuideBehavior, Flag120) == 0x120, "GuideBehavior::Flag120 offset");

// Retail object size, read off the factory body that allocates this class (`PUSH 0x124`
// immediately before `CALL operator_new`). This is an UPPER bound on purpose: the existing
// `>=` asserts in this family prove the model reaches its furthest known field, but they
// cannot catch the opposite error -- a field declared PAST the end of the retail object.
// That is exactly what happened to FormationBehavior, which carried a block of DockBehavior
// fields up to 0x470 behind a `>= 0x470` assert while retail allocates 0x2b8. An upper bound
// cannot fail on an incomplete model, so it is safe to assert even where the port is short.
static_assert(sizeof(GuideBehavior) == 0x124, "GuideBehavior size == retail operator_new(0x124)");

