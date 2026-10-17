// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h"    // Vector
#include "common_goods.h"  // BaseWatcher
#include "common_iface.h"  // IBehaviorManager
#include "common_pub.h"    // pub::AI::DirectiveGotoOp
#include "common_idea.h"   // FaceIdea, MoveIdea -- FormationBehavior's own real embedded sub-objects

#include <cstddef>

struct IObjDB; // common_cobjdb.h -- Behavior::Slot2's argument (pointer-only use here)

// Common.dll AI/formation-flying state machine family (FormationBehavior and friends).
//
// A concurrent decomp-tracker pass identified the owning class hierarchy for this
// address neighborhood (0x062c2000-0x062e8000ish) by hand-parsing the MSVC RTTI
// (Complete Object Locator / TypeDescriptor / ClassHierarchyDescriptor) at each
// vtable's [-4] slot rather than trusting Ghidra's FUN_/LAB_ labels or address
// proximity -- see decomp-tracker items 1279/1285/1397/1415/1583/1586/1653 and this
// repo's Common.dll decomp-tracker marathon project memory. Confirmed hierarchy:
// Behavior (root, RTTI_Type_Descriptor @ .data:063ebdec)
// +-- BaseFollowerBehavior   RTTI_Type_Descriptor @ .data:063ebf30, the immediate
// |                          base shared by a 2-member sibling group (COL
// |                          base-class-array entries @ 063a8fb8/9004 resolve
// |                          through it -- IdleBehavior does NOT, see below,
// |                          correcting this comment's own earlier claim that
// |                          it was a 3-member group).
// +-- FormationBehavior      COL @ 063a8fb8, vtable .rdata:0639fb64, ctor
// |                          FUN_062cbfe0. THE big state machine this file
// |                          ports (see below) -- despite the "Formation" name,
// |                          most of what it does reads as approach/rejoin/
// |                          break-off maneuvering relative to another ship,
// |                          which IS what formation-flying AI needs (fly to a
// |                          relative slot, watch for the leader/obstacle
// |                          getting too close, break off and retreat, rejoin).
// +-- FollowBehavior         COL @ 063a9004, vtable .rdata:0639fbe4, ctor
//                            FUN_062ccaf0. Ported in
//                            common_followbehavior.h/.cpp (2026-09-11).
//
// IdleBehavior (COL @ 063a904c, vtable .rdata:0639fc5c, ctor FUN_062cd530) is
// NOT a BaseFollowerBehavior subclass despite the address proximity and shared
// vtable shape suggesting otherwise -- its own ClassHierarchyDescriptor
// (independently re-verified 2026-09-11, decomp-tracker item 6599) has
// numBaseClasses=2 (self + Behavior only), a direct single-inheritance chain.
// It structurally converges on the same +0x08/0x0c/0x10/0x14/0x1c/0x20/0x24/
// 0x28/0x29 field shape as BaseFollowerBehavior anyway (confirmed via its own
// SetContext calling the same shared FUN_062d2090 initializer), so treat that
// convergence as a shared FIELD LAYOUT convention across the whole family, not
// evidence of a shared BASE CLASS. Ported in common_idlebehavior.h/.cpp.
//
// A prior pass mistook this object for something "far bigger than any CEquip
// subclass" living somewhere near a DIFFERENT class (an unrelated, unexplored
// `DockBehavior`, RTTI COL @ 063a8c4c, whose own Update trampoline
// `LAB_062c3b70` at .rdata:0639f4c0 happens to CALL into a FormationBehavior
// instance) -- that address-proximity signal was misleading, exactly the kind
// of trap already documented project-wide for this module.
//
// AND THEN THE CORRECTION ITSELF WENT THE WRONG WAY, fixed 2026-09-17
// (claude:common-dockmove). The paragraph that stood here argued that
// "the vtable-slot addresses actually called from inside FUN_062c4900/
// FUN_062c3d10 (`*this + 0xc` / `*this + 0x10`) are a byte-for-byte match for
// FormationBehavior's OWN vtable slots (FUN_062cc3b0 / LAB_062cb6d0 at
// .rdata:0639fb70/0639fb74)" and used that to claim the two bodies for this
// class. It does not follow: an own-vtable slot-0xc/0x10 dispatch through
// `[[this]+0xc]` names no class at all, because every Behavior subclass has a
// slot there. LAB_062c3b70 at .rdata:0639f4c0 IS DockBehavior's slot 7, it
// calls FUN_062c4900 at .text:062c3cea with `this` unchanged, and that is
// FUN_062c4900's ONLY caller in the module. Both bodies are DockBehavior's and
// both now live in common_dockbehavior.{h,cpp}. The original "far bigger"
// observation was right about the size and wrong only about which class it
// belonged to.
//
// FormationBehavior/FollowBehavior share one ~29-slot vtable shape and both
// ctors call a common `FUN_062cb2b0(this)` base ctor before installing their
// own vtable; IdleBehavior shares the vtable SHAPE and the same
// `FUN_062d2090` context-initializer convention (see SetContext on each
// class) but is a separate single-inheritance class, not a third caller of
// FUN_062cb2b0 -- see the class-hierarchy comment above. Only FormationBehavior's own layout is
// resolved below, and it is EXACTLY 0x2b8 bytes.
//
// CORRECTED 2026-09-17 (claude:common-dockmove). This comment used to say
// ">= 0x470 bytes -- its ctor only explicitly touches the first ~0x2b8 bytes,
// the rest is written lazily by Update()/EnterState()". That had it exactly
// backwards in both halves. 0x2b8 is the WHOLE object, and 0x470 is a DIFFERENT
// class's size. Retail says so in the two adjacent factory bodies that allocate
// them:
//     .text:062d1b67  PUSH 0x2b8 ; CALL MSVCRT.DLL::operator_new
//     .text:062d1b8c  CALL FUN_062cbfe0        ; == FormationBehavior::ctor
//     .text:062d1a07  PUSH 0x470 ; CALL MSVCRT.DLL::operator_new
//     .text:062d1a2c  CALL FUN_062c2e60        ; == DockBehavior::ctor
// and the "written lazily by Update()/EnterState()" rationalisation was circular:
// those two bodies (FUN_062c4900/FUN_062c3d10) are DockBehavior's, not this
// class's, which is why everything they touch fits in 0x470 and not in 0x2b8.
// Both have MOVED to src/include/common_dockbehavior.h this pass, together with
// the whole >= 0x2b8 field block they addressed. See FormationBehavior's own
// class comment below and decomp-tracker items 1279/1285.

// The class this header used to carry as `FormationMoveFx` (ctor FUN_062d5aa0,
// vtable .rdata:063a047c) is retail's MatchThrottle2Idea, a BaseIdea sibling; it
// lives in common_idea.h since 2026-10-08 (claude-code:cmn-ai5).

// ============================================================================
// Small generic vtable-dispatch helpers used by FormationMoveFx::Update's own
// cruise-transition decision and its still-FULLPORT-DEFERRED shared tail (see
// its doc comment above), decomp-tracker items 1757/1758/1762/1765/1769. All
// operate on interfaces whose owning class/name is not otherwise identified
// in this port -- ported as faithful mechanical forwards (raw vtable-slot
// dispatch, same convention this file already uses pervasively for
// Behavior_NotifyRegistrationFlag/GetIfacePositionOrZero/etc.), not as
// fabricated semantic names.
//
// UPDATE 2026-09-11 (d301:common-port): items 1762/1757/1758 were previously
// left `partial` with the reasoning "would require inventing a vtable layout
// blind" -- re-investigated this pass because FormationMoveFx::Update's own
// still-deferred tail (Common.dll.txt 217486-217555) calls all three of them
// with `this`/param_1 = Field04 or Field04's raw pointer value, i.e. the SAME
// already-identified-by-convention interface Behavior_InvokeManagerSlotD4
// (item 1765, slot 0xd4) already operates on -- NOT a separate, unrelated
// "per-weapon tunable-parameter bridge" as the family comment speculated.
// Slots 0xcc/0xd0/0xd8 are simply more siblings of the same slot-0xd4
// trampoline family on that one interface. Hand-re-verified all three
// call target bodies directly against the raw bytes (not just trusting the
// prior items' own notes) -- see each declaration below.
// ============================================================================

// FUN_062e5b70 (decomp-tracker item 1765). Generic null-guarded vtable-slot
// trampoline: part of a small family of near-identical trampolines at
// 0x062e5af0/b20/b50/b70/ba0 (see FUN_062e5af0's own decomp-tracker notes,
// item 1762) each invoking a different fixed vtable offset -- this one calls
// slot 0xd4. Hand-verified against raw disasm Common.dll.txt:241196-241208
// (12 insns): if Obj is null, returns DAT_06399408 (0.0f, the SAME zero-
// fallback sentinel item 1762's own notes already confirm for its sibling);
// else returns (float10 FPU-register return, modeled here as a plain float)
// Obj->vtable[0xd4](Value, Selector) via __thiscall (Obj as `this`, Value
// pushed as the callee's first stack arg, Selector as its second -- push-
// order-verified: `param_2`/Value is pushed last/closest to ESP, `param_3`/
// Selector first/farthest, matching declared-parameter order).
//
// Two confirmed call sites: (1) FormationMoveFx's own unported "recompute
// Field40" method FUN_062d5a20 calls this on Field04 (that METHOD itself is
// still not ported -- see this class's earlier doc comment); (2)
// FormationMoveFx::Update's own direct call site (Common.dll.txt 217149-
// 217233), wired in the .cpp -- Obj=Field04, Value=a locally re-derived
// FastInvSqrt seed bit-pattern, Selector=4 or 1 gated by a two-bool query
// issued immediately before it.
// IDENTIFIED 2026-09-16 (cross-DLL ABI pass 30): slot 0xd4 is
// IObjInspect::get_projected_throttle(float, ThrustEquipType) -- slot 53 of the retail table
// rebuilt in src/include/common_iface.h. `Selector` is that enum, and retail pushes it as an
// INTEGER (`MOV EDI,0x4` / `MOV EDI,0x1` at Common.dll.txt 217629/217646, pushed verbatim).
float Behavior_InvokeManagerSlotD4(IObjInspect const* Obj, float Value, ThrustEquipType Type);

// FUN_062d21c0 (item 1537), .text:062d21c0-062d21ca, 5 instructions. The whole
// body is `Target->Flags28 = (Target->Flags28 & ~0x02) | 0x01`.
//
// THE RECEIVER IS THE POINTED-TO OBJECT, NOT THE BEHAVIOR. Item 1537's note read
// it as `this->flags_0x28` on the behavior class itself, which is what kept it
// parked as "trivial to port once the owning class exists". At all five retail
// call sites ECX is the VALUE of a pointer field, never `this`:
//   .text:062c393d / .text:062c3d3f   ECX = [ESI+0x2f0]  DockBehavior's NavPilotPtr
//   .text:062c7695                    ECX = [ESI+0x40]
//   .text:062cf4ff / .text:062cf513   ECX = [ESI+0x60] and [ESI+0x64]
// and every one of the five is preceded by a `CALL [vtable+0x10]` on that SAME
// pointer -- a release/teardown pair, which is what the name records.
//
// WHAT THE TARGET IS. `[vtable+0x10]` is BYTE offset 0x10, i.e. vtable index 4 --
// Behavior::Slot4, the family's own disengage. GuideBehavior already types its two
// targets (CachedPtr60/CachedPtr64) as `Behavior*` and calls `->Slot4()` there for
// real, so the target is a Behavior-family object and +0x28 is that family's
// Flags28 byte -- the same byte BaseFollowerBehavior_DefaultRequestFlag
// (LAB_062cd630) sets bit 0x2 of, which this function then clears.
//
// The parameter stays `void*` rather than `Behavior*` for one honest reason: Flags28
// is declared per derived class (BaseFollowerBehavior, DockBehavior, ...) and not on
// the `Behavior` base, so there is no member to name yet. TODO: hoist Flags28 onto
// Behavior and retype this, once one lane owns the whole family's layout.
//
// TWO OTHER CLAIMS IN THAT NOTE MEASURED WRONG. It says "only 2 callers, confirmed
// via list_references"; a grep of the listing for `CALL FUN_062d21c0` finds five
// (the recurring XREF-completeness trap). And it says both callers are "inside this
// same unbuilt class ... ported once in a different, now-lost agent worktree" --
// DockBehavior and GuideBehavior are both real, built classes today.
void Behavior_ReleaseDirectiveTarget(void* Target);

// FUN_062e5af0 (decomp-tracker item 1762). PORTED FOR REAL 2026-09-11
// (d301:common-port). Sibling of Behavior_InvokeManagerSlotD4 immediately
// above -- byte-identical shape, calls slot 0xd8 instead of 0xd4.
// Hand-verified against raw disasm Common.dll.txt:241092-241117 (12 insns,
// this item's own prior notes already had this exactly right: "if (this==0)
// return 0.0; return (*this->vftable[0xd8/4])(param_2,param_3)"; this pass
// only added the confirmation that its FormationMoveFx::Update call sites
// (Common.dll.txt 217486-217506, both still FULLPORT-DEFERRED -- see that
// method's own doc comment) pass Obj=Field04, the SAME interface slot 0xd4
// already operates on, Value=1.0f literal, Selector=4 or 1).
// IDENTIFIED 2026-09-16 (pass 30): slot 0xd8 is IObjInspect::get_speed(float, ThrustEquipType).
float Behavior_InvokeManagerSlotD8(IObjInspect const* Obj, float Value, ThrustEquipType Type);

// FUN_062e5950 (item 1759), cdecl, Common.dll.txt 240922-240962. Null Obj -> 0.0f;
// MaxSpeed < 0 or == 0 -> 0.0f (`TEST AH,0x41; JP` lets only > and NaN through);
// Dt < 0.001 (double DAT_0639c9f0) -> Dt = 0; then tail-returns
// Obj->get_angular_distance_travelled(From, To, Dt, MaxSpeed) (slot 59, +0xec).
float Behavior_GetAngularDistanceTravelled(IObjInspect const* Obj, float From, float To, float Dt,
                                           float MaxSpeed);

// FUN_062e6010 (decomp-tracker item 1769). Takes a single interface pointer
// -- FormationMoveFx::Update's own call site below passes the SAME
// "-8 outer adjustor" WatchedOuter pointer common_aisteer.h's
// GetIfacePositionOrZero (item 1771) also operates on. Hand-verified against
// raw disasm Common.dll.txt:241597-241627 (29 insns); unlike its 1757-1767
// siblings it has NO null-check of its own -- dereferences Iface
// unconditionally, matching every confirmed call site this port has found
// (all pre-validate non-null). Calls Iface->vtable[0x2c](this, &V1[init
// 1.0f], &Unused) -- the SAME slot GetIfacePositionOrZero calls, with the
// same "meaningful output pushed closer to ESP, unused scratch pushed
// farther" shape, push-order-verified to put V1 first/Unused second in
// declared-parameter order -- then Iface->vtable[0x140](this, &V2[init
// 0.0f]) (a second, distinct slot), and returns whichever of V2/V1 is
// strictly larger, V1 on a tie or on any unordered/NaN compare:
// `(V2 > V1) ? V2 : V1`. Neither vtable slot's own semantic meaning (what
// V1/V2 actually represent) is identified in this port -- ported as a
// faithful mechanical forward, not a fabricated semantic name.
// IDENTIFIED 2026-09-16 (pass 30). The two slots are
// IObjInspect::get_physical_radius(float&, Vector&) at 0x2c and
// IObjInspect::get_atmosphere_range(float&) at 0x140, so this returns whichever is larger of
// the object's atmosphere range and its physical radius. The paragraph above said "neither
// vtable slot's own semantic meaning is identified" -- that is no longer true.
//
// It also hid a defect: the port declared slot 0x2c's second parameter `float*` and passed a
// four-byte local, where retail writes a twelve-byte Vector. Eight bytes of the caller's frame
// were overwritten on every call.
float Behavior_GetIfaceMaxScalarAttribute(IObjInspect const* Iface);

// FUN_062e5850 (decomp-tracker item 1757). PORTED FOR REAL 2026-09-11
// (d301:common-port), superseding this item's own prior `partial` notes.
// Hand-re-verified directly against raw disasm Common.dll.txt:240814-240875
// (46 insns) rather than trusting the prior pass's summary. `Obj` (param_1)
// is null-guarded exactly like the slot-0xd4/0xd8 trampolines above (returns
// 0.0f). Radius (param_3) is clamped to >=0.0 (any NaN-or-negative compare
// clamps it), Falloff (param_4) likewise clamped to >=0.0; if Radius==0.0
// AND Unused2 (param_2) ==0.0 (both exactly, ordered compares), returns
// 0.0f early WITHOUT calling the vtable slot at all (matches this item's own
// prior note: "early-out if both Radius and param_2 are 0" -- confirmed
// exactly by the raw TEST AH,0x44/JP idiom at Common.dll.txt 240845-240856).
// Otherwise Radius is additionally snapped to 0.0 if below 0.001
// (DAT_0639c9f0, confirmed 0.001f via the same clamp idiom), then calls
// Obj->vtable[0xcc](Unused2, Radius, Falloff, Selector) via __thiscall,
// preserving the caller's own declared argument order (push-order-verified:
// Selector pushed first/farthest, Unused2 pushed last/closest to ESP).
//
// FormationMoveFx::Update's own call site (Common.dll.txt 217539-217546,
// still FULLPORT-DEFERRED) passes Obj=Field04 (the SAME interface as the
// slot-0xd4/0xd8 helpers), Unused2=0.0f literal, Radius=a value re-derived
// from an earlier FUN_062e58f0/vtable-0xcc chain not independently retraced
// this pass, Falloff=a stale copy of the InvSqrtSeed local (EBP was never
// reassigned between the earlier Behavior_InvokeManagerSlotD4 call site and
// here -- a genuine register-reuse-for-register-pressure artifact, not a
// semantic relationship to InvSqrtSeed), Selector=4 or 1 (the same EDI
// case value Behavior_InvokeManagerSlotD8's own call sites set).
// IDENTIFIED 2026-09-16 (pass 30): slot 0xcc is
// IObjInspect::get_time_to_accelerate(float From, float To, float Dt, ThrustEquipType). The
// parameter names below are the port's pre-identification guesses and are kept so the
// decomp-tracker item and its callers still read the same; `Selector` is the enum, and it used
// to be declared `float`, so `static_cast<float>(1)` reached the callee as 0x3F800000 instead
// of 1. The retail call site pushes the integer (Common.dll.txt 217629-217646).
float Behavior_QueryRadiusEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type);

// FUN_062e58f0 (decomp-tracker item 1758). PORTED FOR REAL 2026-09-11
// (d301:common-port), superseding this item's own prior `partial` notes.
// Hand-re-verified directly against raw disasm Common.dll.txt:240876-240917
// (29 insns). Sibling of Behavior_QueryRadiusEffect immediately above, same
// Obj null-guard, but calls vtable slot 0xd0 and has a SINGLE clamp gate:
// if Falloff (param_4) is <0.0 or NaN, returns 0.0f early without calling
// the vtable slot at all (NOT a "clamp to 0 and continue" like the two
// clamps in the 0xcc sibling -- confirmed by the raw control flow: the
// early-return FLD/RET at Common.dll.txt 240897-240898 is reached directly
// by the JP-not-taken fallthrough, there is no follow-up call after it).
// Otherwise Radius (param_3) is snapped to 0.0 if below 0.001
// (DAT_0639c9f0, same constant as the 0xcc sibling), then calls
// Obj->vtable[0xd0](Unused2, Radius, Falloff, Selector), same argument-order
// convention as Behavior_QueryRadiusEffect.
//
// FormationMoveFx::Update's own call site (Common.dll.txt 217549-217555,
// still FULLPORT-DEFERRED) passes Obj=Field04, Unused2=0.0f literal,
// Radius=the SAME Radius value just passed to Behavior_QueryRadiusEffect
// above (EBP, reloaded fresh at Common.dll.txt 217542 and reused verbatim),
// Falloff=Behavior_QueryRadiusEffect's own return value (a genuine chained
// dependency -- item 1757's own family comment already flagged
// "1760/FUN_062e59b0 chains slot 0xcc then 0xd0" as a sibling doing the same
// thing, independent corroboration), Selector=the same EDI case value.
// IDENTIFIED 2026-09-16 (pass 30): slot 0xd0 is
// IObjInspect::get_distance_travelled(float Speed, float Dt, float Accel, ThrustEquipType),
// with the same float-for-enum defect as its 0xcc sibling above.
float Behavior_QueryFalloffEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type);

// FUN_062e59b0 (decomp-tracker item 1760). CLOSED 2026-09-11 (this pass),
// superseding the prior `partial` verdict. Chains Behavior_InvokeManagerSlotD8
// with the Behavior_QueryRadiusEffect/QueryFalloffEffect pair above, all on
// the SAME Field04 interface family. Hand-verified against raw disasm
// Common.dll.txt:240967-241059 (79 insns) and Ghidra's own clean decompile
// (Common.dll.c 116419-116456, which already carries the correct literal
// constants 1.0/0.0/0.001 for DAT_06399410/DAT_0639c430/DAT_0639c9f0 -- no
// register corruption in the decompile itself). The prior `partial` note's
// "unaff_retaddr register-corruption artifact for one of its float params"
// is resolved: unaff_retaddr is simply this function's own genuine, real 2nd
// stack parameter (Radius, entry_ESP+8) -- confirmed by hand-computing the
// ESP-relative address of the `FCOM float ptr [ESP+0x10]` read at
// Common.dll.txt:240998 (ESP is entry_ESP-8 there, no stack imbalance across
// the preceding vtable call since it returns via a clean, self-cleaning
// indirect call), which resolves to exactly entry_ESP+8 -- Ghidra's own
// 3-param signature guess just failed to correlate its declared param_2 slot
// with this later read (same class of artifact as item 1738's unaff_EBX/
// unaff_retaddr, not a genuine corruption).
float Behavior_QueryChainedRadiusEffect(void* Obj, float Radius, int Selector);

// FUN_062e5ab0 (decomp-tracker item 1761). CLOSED 2026-09-11 (this pass),
// superseding the prior `partial` verdict. Hand-verified against raw disasm
// Common.dll.txt:241064-241090 (21 insns) and Ghidra's clean decompile
// (Common.dll.c 116460-116473, no register corruption). Null-guards Obj,
// then returns 0.0f early if Radius < 0.001f (DAT_0639e6c8, the same epsilon
// used elsewhere in this family); otherwise forwards straight into
// Behavior_QueryFalloffEffect's own vtable slot (0xd0) with a fixed
// Falloff=1000.0f (DAT_063a0760 = 0x447a0000) and Selector=1 literal --
// reusing that helper directly is safe here since Behavior_QueryFalloffEffect's
// own internal "Radius<0.001 -> 0" snap can never fire (this function's own
// gate already guarantees Radius>=0.001f on every path that reaches the call).
float Behavior_QueryFixedFalloffEffect(void* Obj, float Radius);

// FUN_062e5bc0 (decomp-tracker item 1767). CLOSED 2026-09-11 (this pass),
// superseding the prior `partial` verdict. Hand-verified against raw disasm
// Common.dll.txt:241247-241270 (18 insns) and Ghidra's clean decompile
// (Common.dll.c 116547-116560, no register corruption). New sibling vtable
// slot (0xdc) on the same Field04-family interface, not previously ported by
// any sibling in this cluster. Null-guards Obj, returns 0.0f early if Value
// is exactly 0.0f, else calls Obj->vtable[0xdc](Value, 1) via __thiscall
// (Selector fixed to the literal 1, matching the raw `PUSH 0x1` at
// Common.dll.txt:241267).
// IDENTIFIED 2026-09-16 (pass 30): slot 0xdc is
// IObjInspect::get_initial_speed_to_coast_distance(float Speed, bool bStop); the `PUSH 0x1` at
// Common.dll.txt 241267 is that bool.
float Behavior_InvokeManagerSlotDC(IObjInspect const* Obj, float Value);

// FUN_062e5b20 (decomp-tracker item 1763, 12 insns, Common.dll.txt
// 241132-241151). Third member of the same trampoline family: null-guarded
// forward to IObjInspect slot +0xF0 == get_angular_speed_XY(float Speed,
// float MaxSpeed); the null arm loads .rdata:DAT_06399408 (0.0f). Argument
// order is the push order at .text:062e5b35-062e5b3a -- param_3 is pushed
// first, so param_2 is the callee's FIRST argument.
//
// HOISTED HERE 2026-09-17 (claude:common-formation) at the GotoBehavior lane's
// request: it lived file-local in common_gotobehavior.cpp only because that
// lane could not write this file. Two further call sites want it,
// .text:062c8b15 and Bind()'s at .text:062d684e. The gotobehavior copy can be
// removed in a later pass by whichever lane owns that file.
float Behavior_InvokeManagerSlotF0(IObjInspect const* Obj, float Speed, float MaxSpeed);

// FUN_062e5b50 (item 1764): null Obj -> 0.0f, else Obj->get_angular_speed_Z(Speed) (slot +0xf4).
// Sole caller FaceIdea::SetContext (.text:062d6881), which discards the result.
float Behavior_InvokeManagerSlotF4(IObjInspect const* Obj, float Speed);

// FUN_062e5ba0 (item 1766): null Obj -> 0.0f, else Obj->get_projected_axis_throttle_XY(Value)
// (slot +0xf8). Callers: FaceIdea's ctor / SetThrottleScale, ComputeFormationFollowerEnvelope.
float Behavior_InvokeManagerSlotF8(IObjInspect const* Obj, float Value);

// ============================================================================
// FUN_062e89a0 / 062e8c20 / 062e8ce0 / 062e8e60 / 062e9030 / 062e9120 / 062e91d0 (items
// 1819-1825) were free Behavior_* functions taking a `void* ComponentThis`; every retail
// caller passes `SystemManager + 0x18`, and they are ThrustManager members now
// (common_systemmanager.h, common_thrustmanager.cpp; CMN-S2 AI4 2026-10-14).
// ============================================================================

// FUN_062e61f0 (item 1774). Trivial 1-stack-arg tail-thunk: `Node->vtbl[0xc](Node)`.
// The raw bytes (Common.dll.txt 241853-241855): `MOV ECX,[ESP+4]; MOV EAX,[ECX];
// JMP [EAX+0xc]`. No null test, as retail. Slot 0xc is IObjInspect::get_transform().
// Sole caller ThrustManager::SetNudgeDirection (FUN_062e8ce0).
Transform const* Behavior_InvokeNodeSlotC(void* Node);

// FUN_062ecb20 (FollowerDirectiveGateA), FUN_062ecc60 / FUN_062ecce0 (NotifyFormationLeader1 /
// 2) and FUN_062ecc50 (SetFlagC3, formerly FollowerDirectiveRecord_SetFlagC3) are thiscall on
// the 0xC8-byte block at SystemManager+0x1364: FGunConvergenceStateBlock members
// (common_systemmanager.h, common_gunconvergence.cpp; CMN-GC 2026-10-14). Notify callers:
// EvadeBehavior::Slot7 (.text:062c7e80/062c7ebc, 062c7e98/062c7ed4) and BuzzBehavior::Update
// (.text:062c64ec/062c67be, 062c6506/062c67d4); SetFlagC3's: IBehaviorManager::QueueNotification
// cases 1 / 2 (.text:062d4949 / 062d496c).

// The SystemManagerTimerTable (SystemManager+0x34) byte getters FUN_062e8810..8860 are
// SystemManagerTimerTable::IsTimer0Active..IsTimer5Active now (common_systemmanager.h,
// common_sysmgr_timertable.cpp; CMN-SPLIT 2026-10-14).

// FUN_062e8910 / FUN_062e8940 / FUN_062e8970 (.text:062e8910-062e899e, RET 0 each), three
// byte-identical bodies on the SystemManagerTimerTable (SystemManager+0x34) differing only in
// the threshold: BestValueSoFar (+0xf4) > 0.0f (`TEST AH,0x41; JNZ false`) and BestValueSoFar
// <= Owner->PersonalityData.MissileReaction.EvadeBreak / EvadeSlide / EvadeAfterburn
// MissileReactionTime (Sys+0x17b4 /
// +0x17b8 / +0x17bc; `TEST AH,0x41; JP false`, so equality is true). Owner (+4) is
// dereferenced unconditionally. Only caller: EvadeBehavior::Slot7 mode 4. Retail names
// unknown; until 2026-10-08 they were EvadeBehavior::ExternalNavHelperCheck3/4/5.
struct SystemManagerTimerTable;
bool TimerTableMissileWithinReaction00(SystemManagerTimerTable const* Self);   // FUN_062e8910
bool TimerTableMissileWithinReaction04(SystemManagerTimerTable const* Self);   // FUN_062e8940
bool TimerTableMissileWithinReaction08(SystemManagerTimerTable const* Self);   // FUN_062e8970

// ============================================================================
// Behavior / BaseFollowerBehavior -- root and shared immediate base of
// Formation/Follow/Idle. Recovered via the RTTI trail documented at the top
// of this file (a different, worktree-isolated agent pass than the
// FormationMoveFx port above; reconciled here since both independently landed
// on the identical +0x34/+0x59 offsets for FormationMoveFx, which is the
// evidence the two passes are describing the same retail object graph).
// ============================================================================
// The interface IBehaviorManager fans its per-tick ship commands out over. Every slot is
// __thiscall -- retail reaches each one by loading the behavior into ECX and TAIL-JUMPING
// into it, so the behavior inherits the manager method's own stack frame:
//
//   062d43e0  MOV ECX,[this + EAX*0x4 + 0x174]   ; allow_head_tracking
//   062d43f2  MOV EAX,[ECX]
//   062d43f4  JMP dword ptr [EAX + 0x4c]
//
// Until 2026-09-16 IBehaviorManager (common_behaviormanager.cpp) reached all thirteen of them through raw
// `Vtable[0x4c / 4]` casts, eleven of which declared __stdcall with `this` pushed on the
// stack. That is the wrong receiver AND four bytes of the caller's stack per call. The two
// that did say __thiscall (slots 6 and 24) were right, which is what made the file's own
// inconsistency visible.
//
// The named slots are the ones the manager drives, with signatures taken from the exported
// manager methods that tail-jump into them (the mangled names carry the arguments). The
// unnamed ones are placeholders at the right width so the named ones land on the right index.
//
// Slot 0 IS the scalar-deleting destructor -- CONFIRMED 2026-09-17 (claude:common-mixin-owner,
// decomp-tracker items 1531/1524ff), no longer an assumption:
//
//   .rdata:063a02c0  = 0x063a98e8               <- [vtable-4], the RTTI COL
//   COL +0x0C        -> .rdata:063a98f4         -> .data:Behavior::RTTI_Type_Descriptor
//   COL +0x10        -> .rdata:063a98f8         -> ClassHierarchyDescriptor, numBases = 1
//   PTR_LAB_063a02c4 = Behavior's vftable, slot 0 = LAB_062d2060
//
// LAB_062d2060 is the scalar-deleting destructor (`TEST byte [ESP+4],1` delete flag, resets
// the vptr, conditionally calls operator delete), FUN_062d2080 is the plain `~Behavior()` it
// calls (`MOV dword [ECX],&vftable; RET` -- nothing to destroy, just the vptr reset), and
// FUN_062d2020 is `Behavior::Behavior(void*)`, which every one of the nineteen derived
// behavior constructors calls first (TrailBehavior 062ca373, GotoBehavior 062fd644, ...).
//
// FUN_062d2020 writes +0x04, +0x08, +0x0c = 2, +0x10 = 1, +0x14, +0x18, +0x1c, +0x24,
// +0x28 (bits 0-1 cleared) and +0x29, so sizeof(Behavior) >= 0x2c and ALL of those fields
// belong to Behavior itself -- not to BaseFollowerBehavior, where single inheritance
// forced this port to declare them (see that class's own comment). Behaviors that do NOT
// derive from BaseFollowerBehavior (TrailBehavior's 2-entry BaseClassArray is literally
// [TrailBehavior, Behavior]) still have them, and indeed re-declare them locally today.
// Re-homing that prefix onto Behavior is still a separate wave; several slots below are
// FULLPORT-DEFERRED only because of it, and each says so.
//
// ============================================================================
// THE TABLE IS 27 SLOTS, NOT 25 -- measured and reconciled 2026-09-17
// (claude:common-behavior-vtable). Until this pass the class declared 25.
// ============================================================================
// Extent, byte by byte in EXE\Disasm\Common.dll.txt (563659-563686): entries run
// .rdata:063a02c4 through .rdata:063a032c inclusive = 0x6c bytes = 27 dwords. Three
// independent checks that 27 is right and not the classic off-by-one:
//
//  (a) The dword AFTER the last entry, DAT_063a0330, is 0xBF19999A -- the float -0.6f,
//      the same constant that precedes every table in this .rdata run. This is the
//      pitfall a sibling pass hit at scale this week: the apparent trailing slot is
//      usually the NEXT class's RTTICompleteObjectLocator sitting at [next_vtable-4].
//      Checked here explicitly -- 0xBF19999A is not even inside the image (base
//      0x06260000), so it cannot be a COL pointer, and no +0x0C TypeDescriptor walk
//      is possible from it. The real next COL is two dwords further on, at
//      .rdata:063a0338, in front of the next table.
//  (b) decomp-tracker's own item 6619 (the vtable global) independently records
//      byte_size = 108 = 0x6c = 27 * 4.
//  (c) NullBehavior's table, PTR_FUN_063a0244 (COL .rdata:063a0240 -> 063a98bc,
//      +0x0C -> NullBehavior::RTTI_Type_Descriptor), has the IDENTICAL 0x6c extent
//      and is slot-for-slot equal to Behavior's from slot 8 onward. A derived class
//      cannot have a shorter table than its base, and it does not have a longer one.
//
// FULL SLOT TABLE. "folds" = how many vtable slots DLL-wide point at that same address
// (`grep '^\.rdata:.* <bytes>        addr ' Common.dll.txt | wc -l`); anything in the
// double digits is /OPT:ICF collapsing a trivial body, so Ghidra's label on it names
// SOME class that happens to share the code, never the receiver.
//
//  #  off   target                              retail body                                RET n  folds
//  0  0x00  LAB_062d2060                        scalar-deleting dtor                       RET 4      1
//  1  0x04  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  2  0x08  0629afd0 "CEquip::ConnectServerEquip" MOV EAX,[ESP+4]; MOV [ECX+0x18],EAX       RET 4     23
//  3  0x0c  FUN_062d20d0                        +0xc=1; +0x10=1; +0x28&=~1; +0x1c=0         RET 0      1
//  4  0x10  FUN_062d20f0                        t=[[ECX+0x20]+0xc]; if(t)[t+0x64]=0;
//                                               [ECX+0x29]=0                                RET 0      1
//  5  0x14  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  6  0x18  LAB_062cf5e0                        MOV EAX,2                                   RET 4     14
//  7  0x1c  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  8  0x20  062a8920 "Exclusion::AsteroidExclusionZone..."  bare RET                        RET 0     50
//  9  0x24  06263a70 "IObjInspectImpl::cobject" MOV EAX,[ECX+0x10]                          RET 0     25
// 10  0x28  LAB_062c2f40                        MOV AL,1                                    RET 4     21
// 11  0x2c  0629ba00 "CEquip::DisableController" XOR AL,AL                                  RET 0    130
// 12  0x30  062ed730 "MetaList<GoodDesc>::op"   empty                                       RET 4    214
// 13  0x34  062ed730                            empty                                       RET 4    214
// 14  0x38  062ed730                            empty                                       RET 4    214
// 15  0x3c  062ed730                            empty                                       RET 4    214
// 16  0x40  FUN_062d2110                        real body; slot-0x5c query on [+0x24]-8,
//                                               then FUN_062e9120                           RET 4     18
// 17  0x44  FUN_062d21b0 -> FUN_062e9030        real body on [+0x24]+0x18                   RET 4     19
// 18  0x48  062ed730                            empty                                       RET 4    214
// 19  0x4c  0627a710 "Archetype::AttachedEquipment::i..." MOV AL,1                          RET 0     36
// 20  0x50  LAB_062c2f40                        MOV AL,1                                    RET 4     21
// 21  0x54  062ed730                            empty                                       RET 4    214
// 22  0x58  062ed730                            empty                                       RET 4    214
// 23  0x5c  LAB_062c2f50                        AND byte [ECX+0x28],0xfd                    RET 0     21
// 24  0x60  06325390 "RDLNode::operator..."     XOR EAX,EAX                                 RET 0    374
// 25  0x64  0629ba10 "CEShield::GetEquipDesc"   XOR AL,AL                                   RET 4     25   <- WAS MISSING
// 26  0x68  06325390 "RDLNode::operator..."     XOR EAX,EAX                                 RET 0    374   <- WAS MISSING
//
// Only slots 0, 3, 4 have a fold count of 1, i.e. only those three bodies are Behavior's
// and nobody else's. Slots 16/17 (18 and 19 refs) are shared inside the behavior family
// itself. EVERY other label above is an ICF fold and was named accordingly -- by position
// and `RET n`, never by Ghidra's symbol. Three of them are nonetheless provably Behavior's
// own semantics because of the field they touch: slot 2 writes +0x18, slot 9 reads +0x10,
// slots 10/23 set and clear bit 0x2 of +0x28 -- all fields Behavior's own constructor
// FUN_062d2020 initialises, and +0x28 bit 0x2 is exactly the bit
// BaseFollowerBehavior_DefaultRequestFlag (LAB_062cd630, a derived override of slot 10)
// sets. That pairing is what tells slots 10 and 23 apart from the noise.
//
// PURE VIRTUALS (slots 1, 5, 7): LAB_06391fb0 is
// `JMP dword ptr [->MSVCRT.DLL::purecall]`, so retail's Behavior is ABSTRACT at those three
// indices. NullBehavior supplies all three and their shapes read straight off its overrides:
// slot 1 = LAB_062d76a0 `MOV AL,1; RET 0xc` (bool, three dword arguments), slot 5 =
// LAB_062d1730 `MOV EAX,1; RET 4` (int, one dword argument), slot 7 = LAB_062d1740
// `MOV EAX,2; RET 0` (int, no arguments).
//
// SLOT 7 IS NOW `= 0` (2026-09-17, claude:common-follower) -- every concrete class in the
// family supplies a body; see that slot's own comment for the evidence and for the three
// intermediate classes it makes abstract. SLOTS 1 AND 5 ARE STILL NOT: no subclass in this
// port overrides either, so `= 0` there would make all twenty-four behavior classes
// uninstantiable. That half of the debt stays FULLPORT-DEFERRED for the subclass wave.
//
// Slots 3, 4, 8, 9, 11, 21, 22, 25, 26 keep their SlotN placeholder names -- retail gives no
// name evidence for them -- but their signatures are now the real ones from `RET n` and the
// return register the body leaves set.
class Behavior
{
public:
    // 0 +0x00. LAB_062d2060, the scalar-deleting destructor; it calls FUN_062d2080,
    // the plain `~Behavior()` (vptr reset, nothing to destroy). MSVC emits exactly this
    // pair for `virtual ~Behavior() {}`, so slot 0 is correct as declared -- unlike the
    // FormationMoveFx case (commit d178e12) this port did NOT invent a destructor here.
    virtual ~Behavior() {}
    // 1 +0x04. PURE VIRTUAL in retail (_purecall). SetContext(Owner, Manager, Sys): the one
    // caller is IBehaviorManager's ctor (.text:062d2220), which runs it on each of the 21
    // CREATE_BEHAVIOR results with (owner IObjRW*, the manager, its SystemManager). Every
    // retail override starts with the family's binder FUN_062d2090 (Owner && Sys required;
    // +0x14 = Owner, +0x08 = Owner->get_id(), +0x24 = Sys, +0x20 = Manager) and returns its
    // result. Renamed from `Slot1` 2026-09-24 so the subclasses' existing SetContext members
    // become the overrides retail's vtables hold (NullBehavior LAB_062d76a0 is the only one
    // that skips the binder: `MOV AL,1; RET 0xc`).
    // Still declared with a body, not `= 0`: GotoBehavior/FreeFlightBehavior (LAB_062fd890)
    // and BaseFollowerBehavior (LAB_062cb470, Follow/Formation) have no override yet, so for
    // those the binder does not run -- item 1991 / the BaseFollower slot-1 item stay
    // `simplified`. (StrafeBehavior's LAB_062c9660 is ported since 2026-10-08.)
    // FULLPORT-DEFERRED: make `= 0` once those three supply an override.
    virtual bool SetContext(void* Owner, void* Manager, void* Sys)
    { (void)Owner; (void)Manager; (void)Sys; return true; }
    // 2 +0x08. `MOV EAX,[ESP+4]; MOV [ECX+0x18],EAX; RET 4` (0x0629afd0, an ICF fold Ghidra
    // labels CEquip::ConnectServerEquip) -- a plain setter for Behavior's own +0x18, one of the
    // nine fields FUN_062d2020 zeroes. Its one caller is ConnectBehaviorManagerObjectDb
    // (FUN_062d2830, .text:062d2846-062d286a), which hands every dispatch-table entry the
    // manager's IObjDB -- so +0x18 is the behavior's object database (LaunchBehavior already
    // reads its +0x18 as `->vtbl[8]()`, IObjDB::GetZoneDb). WIRED 2026-09-24: until then this
    // body was empty and no behavior ever received the database. The store goes through the
    // same raw-offset device as SetTickDelta, for the same reason (the +0x04..+0x29 block is
    // still declared on the subclasses, see below). No subclass overrides this slot.
    virtual void Slot2(IObjDB* ObjectDb);
    // 3 +0x0c. FUN_062d20d0, decomp-tracker item 1533, fold count 1 -- Behavior's own.
    // Retail: `+0xc = 1; +0x10 = 1; +0x28 &= 0xfe; +0x1c = 0`, i.e. it re-arms the same
    // fields the constructor set (the constructor leaves +0xc = 2), then RET with no
    // arguments. Written through the same raw-offset device as Slot2 (the +0x04..+0x29
    // block is still declared on the subclasses). Derived slot-3 bodies that begin with
    // `CALL FUN_062d20d0` call it as Behavior::Slot3().
    virtual void Slot3();
    // 4 +0x10. FUN_062d20f0, decomp-tracker item 1534, fold count 1 -- Behavior's own.
    // Retail: `P = [[this+0x20]+0xc]; if (P) [P+0x64] = 0; byte [this+0x29] = 0` --
    // this+0x20 is the IBehaviorManager, +0x0C its PathfindManager, whose Field_064 is
    // cleared. Derived slot-4 bodies that end in `CALL/JMP FUN_062d20f0` call it as
    // Behavior::Slot4().
    virtual void Slot4();
    // 5 +0x14. PURE VIRTUAL in retail (_purecall); NullBehavior's override returns int 1
    // and pops one dword. FULLPORT-DEFERRED: make `= 0` once every subclass overrides it.
    //
    // THE ARGUMENT IS A FLOAT, corrected 2026-09-17. It was declared `void* Arg` because
    // both call sites in IBehaviorManager::UpdateBehaviorTick push a plain dword
    // (.text:062d2e7f `MOV EAX,[ESP+0x14]; PUSH EAX` and .text:062d31b8
    // `MOV EAX,[ESI+0xdc]; PUSH EAX`), which on x86 looks identical for a float already
    // sitting in memory -- MSVC needs no FLD/FSTP to pass it. What settles it is what
    // those slots hold, and the manager itself says so 50 instructions earlier in the
    // same function: .text:062d2df2 `FADD float ptr [ESI + 0xdc]` followed by
    // .text:062d2dfc `FSTP float ptr [ESI + 0xdc]`. Manager+0xdc is a float accumulator,
    // and it is passed here verbatim. The other site's [ESP+0x14] is the same
    // accumulated per-tick timer.
    virtual int Slot5(float Value) { (void)Value; return 1; }
    // 6 +0x18 -- ?external_player_evaluate@IBehaviorManager@@QAE?AW4EvaluateResult@@H@Z
    // tail-jumps here. NOTE: the older claim that "the manager passes its own +0xdc
    // through as the context word" was written when +0xdc was thought to be an opaque
    // context dword; it is a float (see Slot5 above), and it is Slot5 that receives it,
    // not this slot.
    // Retail's base body is LAB_062cf5e0 `MOV EAX,2; RET 4`, and EvalSkip == 2, so the
    // default below is retail's own answer, not a placeholder.
    virtual EvaluateResult evaluate(unsigned int Context) { (void)Context; return EvalSkip; }
    // 7 +0x1c. PURE VIRTUAL in retail (_purecall); NullBehavior's override returns int 2
    // with no arguments.
    //
    // NOW `= 0` -- 2026-09-17 (claude:common-follower). This was the file's own explicit
    // FULLPORT-DEFERRED debt ("make `= 0` once every subclass overrides it") and the
    // condition is met: every CONCRETE class in the Behavior family now supplies a slot-7
    // body. The last four holes closed this pass were BaseFollowerBehavior (which covers
    // FormationBehavior and FollowBehavior too, since retail's slot 7 for all three is the
    // single shared LAB_062cb700 -- see BaseFollowerBehavior::Slot7 below), EvadeBehavior
    // (its already-ported Perform() was simply never wired to the slot) and FleeBehavior
    // (LAB_062c72c0, ported this pass).
    //
    // Making it pure makes `Behavior` abstract, which is what retail is: slots 1, 5 and 7
    // are all _purecall in .rdata:063a02c4. It also makes the three intermediate "Fields"
    // classes abstract -- DockBehaviorFields, GotoBehaviorFields and
    // InstantTradeLaneBehaviorFields all carry their slot-7 override on the DERIVED class
    // rather than on themselves. Checked before making the change: none of the three is
    // ever constructed on its own (their only appearances in src/*.cpp are as base-class
    // mem-initialisers, and their static_asserts use sizeof, which is legal on an abstract
    // type), and nothing anywhere constructs a bare `Behavior`.
    //
    // SetContext (slot 1) and Slot5 stay non-pure. They are pure in retail too, but Goto,
    // Strafe and BaseFollower do not override SetContext yet and no subclass overrides Slot5,
    // so `= 0` there would make classes uninstantiable. That half of the debt is still open.
    virtual int Slot7() = 0;
    // 8 +0x20. Bare `RET` -- a real, empty, zero-argument base implementation.
    virtual void Slot8() {}
    // 9 +0x24. `MOV EAX,[ECX+0x10]; RET` (ICF fold Ghidra labels IObjInspectImpl::cobject) --
    // a getter for Behavior's own +0x10 (the subclasses' UpdateResult10). Read through the
    // raw-offset device; until 2026-10-08 this returned a constant 0.
    virtual int Slot9();
    // 10 +0x28. `MOV AL,1; RET 4` -- returns true, pops one dword the body never reads.
    // The argument width comes from `RET 4`, and from the confirmed derived override
    // BaseFollowerBehavior_DefaultRequestFlag (LAB_062cd630), which has the same shape and
    // additionally sets bit 0x2 of +0x28. Declared `void Slot10()` until 2026-09-17, which
    // was both the wrong return type and four bytes of caller stack left unpopped.
    //
    // The parameter is NOT unused by callers, only by this base body -- renamed from
    // `Unused` 2026-09-17 because that name was steering readers wrong. At
    // IBehaviorManager::SetCurrentBehavior's call site the caller loads [ESP+0x28],
    // null-tests it (.text:062d3560 `TEST EAX,EAX; JZ`), skips the call entirely when it
    // is null, and consumes the bool result (.text:062d356c `MOV BL,AL`).
    //
    // TYPE PROMOTED int -> void* 2026-09-17 (claude:common-follower). The old comment here
    // said "do not promote it to `void*` without a deref site"; this pass found two, both
    // inside BaseFollowerBehavior::Slot7 (LAB_062cb700, item 1390), and they agree:
    //   * .text:062cba08 `PUSH EDX; CALL [EAX+0x28]` where EDX == `LEA EDX,[ESP+0x70]`, a
    //     stack-local the two preceding blocks construct as a `pub::AI::DirectiveGotoOp`
    //     (its real ctor is called at 062cb9ce, then +0x0c/+0x1c/+0x50 are filled in).
    //   * .text:062cbc28 `PUSH EAX; CALL [EDX+0x28]` where EAX == `LEA EAX,[ESP+0x38]`, a
    //     stack-local built the same way as a `pub::AI::DirectiveDockOp` (ctor at
    //     062cbc06, then +0x0c/+0x10 filled in).
    // The already-ported derived override FUN_062cd3d0 (FollowBehavior::RequestFlag) then
    // reads its own parameter's +0x10/+0x14/+0x18/+0x1c/+0x20 -- i.e. it derefs a
    // pub::AI::DirectiveFollowOp. So this slot is "hand the behavior a directive op"; the
    // pointee type varies per subclass, hence `void*` and not one concrete op type.
    virtual bool Slot10(void* DirectiveOp) { (void)DirectiveOp; return true; }
    // 11 +0x2c. `XOR AL,AL; RET` -- returns false, no arguments.
    virtual bool Slot11() { return false; }
    virtual void set_direction(Vector const& Dir) { (void)Dir; }             // 12 +0x30
    virtual void set_throttle(float Throttle) { (void)Throttle; }            // 13 +0x34
    virtual void set_cruise(bool bOn) { (void)bOn; }                        // 14 +0x38
    virtual void engage_engine(bool bOn) { (void)bOn; }                     // 15 +0x3C
    // 16 +0x40. FUN_062d2110, decomp-tracker item 1535, 54 instructions,
    // Common.dll.txt 210791-210860. NO LONGER `{}` -- 2026-09-17 (claude:w23-stubsweep2).
    //
    // Eighteen vtable slots DLL-wide hold this address, all of them behavior-family tables
    // inheriting Behavior's own entry, and IBehaviorManager::update_current_behavior_
    // set_afterburner (common_behaviormanager.cpp) dispatches into it every time the manager is told
    // to toggle the afterburner. Until this pass every one of those calls landed on an
    // empty body.
    //
    // Item 1535's own note left it unported on the grounds that it "dispatches through
    // this+0x24's unidentified pointee". That is stale on both halves. The object at
    // this+0x24 is the NavHelper every subclass in this port already names, and the two
    // things retail does with it are both already-ported generic helpers:
    //   * `[this+0x24] + 0x8` -> list head -> `head - 8` -> vtable slot 0x5c, which
    //     common_iface.h and Behavior_UpdateCruiseGate both name
    //     IObjInspect::is_cruise_active(bool& OutCruise, bool& OutTradeLane);
    //   * `[this+0x24] + 0x18` is the module-wide "ComponentThis" convention (the same
    //     +0x18 slot 17 below forwards into, and the same one FormationMoveFx::SetMode and
    //     GotoBehavior already hand to Behavior_UpdateCruiseGate).
    // See common_behavior.cpp for the branch-by-branch derivation.
    virtual void set_afterburner(bool bOn);                                 // 16 +0x40
    // 17 +0x44. FUN_062d21b0, decomp-tracker item 1536, THREE instructions:
    //     MOV ECX,[ECX+0x24]   ; the NavHelper pointer field
    //     ADD ECX,0x18         ; the ComponentThis sub-object
    //     JMP FUN_062e9030     ; == Behavior_RefreshCachedValueIfChanged, already ported
    // NO LONGER `{}` -- 2026-09-17 (claude:w23-stubsweep2). The whole body is a forward
    // into a function this port has had at `ready_unverified` for days, and the empty stub
    // meant the twenty vtable slots that hold this address -- plus the PLAYER's own strafe
    // burst, which reaches here through IBehaviorManager::update_current_behavior_slide_
    // strafe_burst from src/freelancer/Client/player_behavior.cpp -- all did nothing.
    virtual void slide_strafe_burst(StrafeDir Dir);                         // 17 +0x44
    virtual void brake_reverse(bool bOn) { (void)bOn; }                     // 18 +0x48
    // 19 +0x4C. Retail's base body is `MOV AL,1; RET` -- it answers TRUE. The old comment
    // here justified `return false` from IBehaviorManager::allow_head_tracking()'s
    // no-current-behavior path, but that is the MANAGER's null guard, a different thing
    // from the base class's own implementation. Corrected 2026-09-17.
    virtual bool allow_head_tracking() { return true; }
    // 20 +0x50. The argument was DROPPED until 2026-09-16: the port declared this slot
    // zero-argument while 062d43c0 tail-jumps into it, which hands the behavior the
    // CancelRequestType the manager itself was called with. Retail's base body
    // (LAB_062c2f40, `MOV AL,1; RET 4`) does answer true.
    virtual bool cancel_behavior(CancelRequestType Request) { (void)Request; return true; }
    // 21 +0x54, 22 +0x58. Empty `RET 4` bodies -- one dword argument each, not zero as
    // declared until 2026-09-17.
    virtual void Slot21(void* Arg) { (void)Arg; }
    virtual void Slot22(void* Arg) { (void)Arg; }
    // 23 +0x5C. LAB_062c2f50, `AND byte ptr [ECX+0x28],0xfd; RET` -- clears exactly the
    // +0x28 bit slot 10's derived override sets. All 21 Behavior vtables point here
    // (LAB_062c2f50's XREF list); no subclass overrides it. Written with the same raw-offset
    // device as SetTickDelta/Slot2, since +0x28 is declared on the subclasses.
    virtual void cancel_notify();
    virtual IObjRW const* get_target() { return nullptr; }                   // 24 +0x60
    // 25 +0x64. ADDED 2026-09-17 -- previously missing, which left every Behavior object
    // in this port carrying a 25-entry table while retail's is 27. Nothing in src/ indexes
    // a behavior vtable by number today (checked: the only raw
    // `*reinterpret_cast<void***>` dispatches in common_behavior.cpp are on NavPilotPtr /
    // EngineObj / NavHelper / GotoWatcherPtr / Field04Sub, none of them Behavior-family,
    // and all at indices below 25), so this was a latent hazard rather than a live
    // out-of-bounds call -- but it is the same hazard commit d178e12 found live in
    // common_stubs.cpp on FormationMoveFx, so it is fixed the same way.
    // Retail body 0629ba10, `XOR AL,AL; RET 4`: returns false, pops one dword.
    virtual bool Slot25(void* Arg) { (void)Arg; return false; }
    // 26 +0x68. ADDED 2026-09-17, same reason. Retail body 06325390, `XOR EAX,EAX; RET`:
    // returns a zero dword, no arguments. Declared as a pointer return to match slot 24,
    // which shares the identical ICF-folded body; `int` would be equally consistent with
    // the asm and nothing observed distinguishes them, so this one is deliberately typed
    // after its neighbour rather than guessed at semantically.
    virtual void* Slot26() { return nullptr; }

    // ---- Behavior's OWN data block is +0x04..+0x29 ---------------------------------------
    // RECOVERED 2026-09-17 (claude:common-behavior-layout) from the constructor itself,
    // FUN_062d2020 (.text:062d2020-062d205a, 19 XREFs -- every behavior class in the module
    // runs it as its first act). It writes, in program order:
    //     +0x04 = its one argument   +0x08 = 0   +0x14 = 0   +0x24 = 0   +0x18 = 0
    //     +0x1c = 0                  +0x29 = 0   +0x28 &= 0xfc
    //     +0x00 = &Behavior::`vftable' (.rdata:063a02c4)   +0x0c = 2   +0x10 = 1
    // and the shared context binder FUN_062d2090 (15 XREFs, likewise spread across the whole
    // family -- including IdleBehavior and GotoBehavior, which do NOT derive from
    // BaseFollowerBehavior) additionally writes +0x08, +0x14, +0x20 and +0x24. So every field
    // from +0x04 through +0x29 belongs to Behavior itself: not to BaseFollowerBehavior, and
    // not to any individual leaf behavior.
    //
    // THE PORT DOES NOT MODEL IT THAT WAY, and this pass deliberately did not change that.
    // COUNT UPDATED 2026-09-17 (claude:common-factory): it is now TWENTY-ONE, not nineteen --
    // NullBehavior gained the block (it had none, against a 0x2c retail object, which would
    // have made Behavior::SetTickDelta write past the end of a four-byte object the moment
    // the factory could build one) and the newly created StrafeBehavior declares it too.
    // Twenty-one classes across src/include/common_*behavior.h each re-declare the same block on
    // THEMSELVES -- BuzzBehavior::DeltaTime, IdleBehavior::DeltaTime1c, EvadeBehavior::
    // DeltaTime1c, DockBehaviorFields::..., BaseFollowerBehavior::DeltaTime, and so on, each
    // one pinned by its own offsetof static_assert. Those asserts pass only because
    // `sizeof(Behavior) == 4` today. Hoisting the block up into this class would move every
    // one of those twenty-one classes' members 0x28 bytes at a stroke, so the migration has to
    // retire all twenty-one duplicates in the same commit. It cannot be done from this file
    // alone and it is NOT done here. Open TODO, not an accepted state.
    //
    // What IS closed here is the one call site that had to reach into the base by raw offset
    // regardless of which class declares the storage. IBehaviorManager::UpdateBehaviorTick
    // hands the active behavior its accumulated per-tick delta immediately before calling
    // Slot7(): Common.dll.txt .text:062d335d-062d336a,
    //     FLD float ptr [ESI + 0xd4]      ; the manager's own float accumulator
    //     FSTP float ptr [EDI + 0x1c]     ; EDI is a bare Behavior* out of the dispatch table
    //     CALL dword ptr [EAX + 0x1c]     ; vtable +0x1c IS Slot7
    // An `FSTP float` is a four-byte float store, which is independent confirmation of the
    // constructor's own dword zero at the same offset. The two accessors below own that
    // offset so common_behaviormanager.cpp does not have to open-code it. They are deliberately NOT
    // backed by a data member: adding one here is precisely the 0x28-byte shift above.
    float GetTickDelta() const;
    void SetTickDelta(float Delta);

    // +0x24, THE NAVHELPER POINTER, reached the same way and for the same reason
    // GetTickDelta reaches +0x1c: the shared context binder FUN_062d2090 writes this field
    // on every behavior in the family, but the storage is declared twenty-one times over on
    // the subclasses (DrasticEvadeBehavior::NavHelperPtr24, FormationBehavior::NavHelper,
    // LargeShipBehavior::NavHelperPtr24, ...) rather than here, because `sizeof(Behavior)`
    // has to stay 4 until the whole +0x04..+0x29 prefix can be hoisted in one commit.
    // ADDED 2026-09-17 (claude:w23-stubsweep2) so slots 16 and 17 above can be real bodies
    // on this class instead of empty stubs waiting on that migration. Deliberately NOT
    // backed by a data member, for exactly the reason the block above gives.
    void* GetNavHelper() const;

    // FUN_062d2020 (item 1529), Behavior's constructor body, RET 4: +0x04 = TypeId,
    // +0x08/+0x14/+0x24/+0x18/+0x1c = 0, byte +0x29 = 0, byte +0x28 &= 0xfc, vptr,
    // +0x0c = 2, +0x10 = 1. Every derived constructor calls it first; the port's derived
    // constructors call this from their bodies (the +0x04..+0x29 block is still declared on
    // the subclasses, so it cannot run from Behavior's own constructor before their member
    // initialisers). Added 2026-10-08: until then most subclasses started with
    // +0x0c = 0 / +0x10 = 0 instead of retail's 2 / 1.
    void ConstructBase(int TypeId);

    // FUN_062d2090 (item 1532), non-virtual, thiscall, RET 0xc, 15 call sites (the slot-1
    // SetContext overrides): Owner && Sys required, else false; then +0x14 = Owner,
    // +0x08 = Owner->get_id(), +0x24 = Sys, +0x20 = Manager, true. Raw-offset device.
    bool BindContext(void* Owner, void* Manager, void* Sys);

    // FUN_062d21d0 (item 1538), non-virtual, thiscall, 20 call sites across the family:
    // `MOV EDX,[ESP+4]; MOV EAX,[ECX+0x20]; MOV ECX,[EAX+0xc]; MOV [ESP+4],EDX;
    // JMP FUN_062dd3d0` -- a forwarder (not an MI adjustor thunk) to the manager's
    // PathfindManager::SetCruiseLock with both stack arguments passed through. +0x20 is read
    // through the raw-offset device, for the reason GetNavHelper gives.
    void SetCruiseLock(int Value, int Mask);

    // ---- +0x04 IS THE TYPE ID, and every behavior constructor takes it -------------------
    // RECOVERED 2026-09-17 (claude:common-factory). The constructor above, FUN_062d2020,
    // writes its ONE argument to +0x04 and nothing else reads that argument. Where the
    // argument comes from is the twenty-one factory bodies at .text:062d16d0..062d1f90, one
    // per registered behavior type, all the same six-instruction shape:
    //     PUSH <retail object size>   ; CALL operator_new
    //     TEST EAX,EAX / JZ <return null>
    //     MOV  ECX,dword ptr [ESI + 0x4]   ; ESI is the BehaviorTypeDesc; +0x04 is its Id
    //     PUSH ECX
    //     MOV  ECX,EAX                      ; the fresh object
    //     CALL <that type's constructor>
    // e.g. .text:062d1796-062d179c for Buzz and .text:062d1a06-062d1a2c for Dock. So the
    // descriptor's own type id is handed to the concrete constructor, which forwards it (via
    // whatever intermediate bases it has) to FUN_062d2020, which parks it at +0x04. Every one
    // of the twenty-one constructors in this port therefore takes `int TypeId`, and passes it
    // down; the storage lives on each derived class, at +0x04, for exactly the reason the
    // comment above gives (`sizeof(Behavior)` has to stay 4 until the whole +0x04..+0x29 block
    // can be hoisted in one commit).
    //
    // NOTHING in retail reads +0x04 back except Behavior's own vtable slot 0 equivalent on the
    // DESCRIPTOR class (BehaviorTypeDesc::get_type_id, a different object) -- this field is
    // written once and, so far as this pass could find, never consumed inside Common.dll. It
    // is stored anyway because retail stores it, and because an out-of-module caller can read
    // it off a Behavior* at a fixed offset.
    int GetTypeId() const;
};

// ============================================================================
// BaseFollowerBehavior -- RTTI ".?AVBaseFollowerBehavior@@" (TypeDescriptor .data:063ebf30),
// the shared single-inheritance base of FormationBehavior (vftable 0639fb64) and
// FollowBehavior (vftable 0639fbe4). Own vftable .rdata:0639fae4, 29 slots (0639fae4..
// 0639fb54; 0639fb58 is the float constant DAT_0639fb58 and 0639fb60 the next COL).
// Constructor FUN_062cb2b0, destructor FUN_062cb3b0 (scalar-deleting 062cb390).
//
// RE-PORTED 2026-10-08 (claude-code:cmn-ai5). The whole +0x2c..+0x2a0 block is declared here,
// where retail builds it: FUN_062cb2b0 constructs it for both subclasses with `this`
// unmodified, and the base's own slots 3/4/7/8/21/22 read and write it. Until then each
// subclass declared its own copy under different names and the base reached it through raw
// offset accessors. sizeof is 0x2a0: base slot 4 (062cb5ee) writes +0x29c, so the Vector at
// +0x290 that both subclasses use is a base member too.
//
// Vtable 0639fae4, overrides of Behavior's 27 plus the two this class adds:
//    0  062cb390  scalar-deleting dtor around FUN_062cb3b0
//    1  062cb470  SetContext
//    3  062cb540  Slot3
//    4  062cb5e0  Slot4
//    5  06391fb0  _purecall
//    6  06391fb0  _purecall (evaluate)
//    7  062cb700  Slot7, the subtree's per-tick body (no subclass overrides it)
//    8  062cb6d0  Slot8
//   10  06391fb0  _purecall -- Behavior's concrete slot 10 is re-pured here
//   16  062ed730  set_afterburner: bare `RET 4`
//   17  062cbfc0  slide_strafe_burst: FUN_062e9030(Sys+0x18, 0), the argument replaced by 0
//   19  0629ba00  allow_head_tracking: `XOR AL,AL; RET`
//   21  062cf300  Slot21
//   22  062cbea0  Slot22
//   27  06391fb0  _purecall, Slot27 (void, no arguments: the one call site .text:062cbcbd
//                 pushes nothing and discards EAX)
//   28  06391fb0  _purecall, Slot28 (Vector by value: the `RET 4` pops MSVC's hidden
//                 return-buffer pointer -- .text:062cbc2d pushes a bare local and hands the
//                 returned EAX to FUN_062c50c0, which copies three dwords out of it)
// All four _purecall slots are `= 0` below; both subclasses override every one of them.
// ============================================================================
class SystemManager;

class BaseFollowerBehavior : public Behavior
{
public:
    // FUN_062cb2b0 (RET 4): FUN_062d2020(TypeId); LeadWatcher zeroed; the five Ideas at
    // +0x34/+0x90/+0x150/+0x1c8/+0x264 (EH states 2..6); bytes +0x280..+0x285 and +0x28c and
    // dwords +0x278/+0x27c zeroed; vptr 0639fae4. +0x288/+0x290/+0x29c are not written.
    explicit BaseFollowerBehavior(int InTypeId);
    // FUN_062cb3b0: guarded set_pointer(0) on +0x1f0, +0x164, +0xa4, +0x48, +0x2c (the
    // members' own BaseWatcher destructors, in reverse order), then FUN_062d2080.
    ~BaseFollowerBehavior() override;

    BaseFollowerBehavior(BaseFollowerBehavior const&) = delete;
    BaseFollowerBehavior& operator=(BaseFollowerBehavior const&) = delete;

    // Slot 1, LAB_062cb470 (RET 0xc).
    bool SetContext(void* Owner, void* Manager, void* Sys) override;
    // Slot 3, FUN_062cb540.
    void Slot3() override;
    // Slot 4, FUN_062cb5e0.
    void Slot4() override;
    // Slots 5, 6 and 10 are _purecall in this table.
    int Slot5(float Value) override = 0;
    EvaluateResult evaluate(unsigned int Context) override = 0;
    // Slot 7, LAB_062cb700 (488 instructions).
    int Slot7() override;
    // Slot 8, LAB_062cb6d0.
    void Slot8() override;
    bool Slot10(void* DirectiveOp) override = 0;
    // Slot 16, 062ed730: a bare `RET 4`.
    void set_afterburner(bool bOn) override { (void)bOn; }
    // Slot 17, LAB_062cbfc0.
    void slide_strafe_burst(StrafeDir Dir) override;
    // Slot 19, 0629ba00: `XOR AL,AL; RET`.
    bool allow_head_tracking() override { return false; }
    // Slot 21, LAB_062cf300 (RET 4). The argument is an object id (the manager's loop at
    // .text:062d405c hands every entry the dword at [Data+8]).
    void Slot21(void* Arg) override;
    // Slot 22, FUN_062cbea0 (RET 4). The argument is the manager's FBehaviorManagerEvent.
    void Slot22(void* Arg) override;
    // Slots 27 and 28, _purecall here.
    virtual void Slot27() = 0;
    virtual Vector Slot28() = 0;

protected:
    // FUN_062cbcf0 (RET 4), Slot7's "final leg" payload, called at .text:062cbca4 with
    // DeltaTime. A switch on SystemManager+0x1810 (0..3; anything above 3 does nothing).
    void UpdateTickPayload(float Delta);

public:
    // ---- Behavior's own +0x04..+0x2b block, still declared per class ----
    int TypeId;                     // +0x04, FUN_062d2020's argument
    unsigned CachedHandle08;        // +0x08, the binder's Owner->get_id()
    int ModeSelector0xc;            // +0x0c, Slot5's result
    int UpdateResult;               // +0x10, Slot7's result
    void* OwnerContext;             // +0x14, the owner IObjRW* (binder)
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime;                // +0x1c, stored by the manager before Slot7
    IBehaviorManager* BehaviorMgrPtr; // +0x20 (binder)
    SystemManager* NavHelper;       // +0x24 (binder)
    unsigned char Flags28;          // +0x28, bit 1 set/cleared by slot 10
    unsigned char Flag29;           // +0x29
    unsigned char Pad2a[0x2c - 0x2a];

    // ---- BaseFollowerBehavior's own block, built by FUN_062cb2b0 ----
    // +0x2c: the ship being followed. Slot7 re-points it every tick from the SystemManager's
    // +0x1364 directive record (FUN_062ecaf0); FormationBehavior's slots 5/27 from
    // FUN_062ecab0 / FUN_062ecad0, FollowBehavior's slot 10 from the directive op.
    BaseWatcher LeadWatcher;        // +0x2c
    MatchThrottle2Idea MatchThrottle; // +0x34 (ctor FUN_062d5aa0)
    FaceIdea Face;                  // +0x90 (ctor FUN_062d6750)
    MoveIdea Move;                  // +0x150 (ctor FUN_062d4e60)
    BreakAwayIdea BreakAway;        // +0x1c8 (ctor FUN_062d8970)
    AfterburnerIdea Afterburner;    // +0x264 (ctor FUN_062da500)
    // +0x278 / +0x27c: the manager's own Dock (BehaviorDispatchTable[7], Manager+0x190) and
    // Goto (BehaviorDispatchTable[2], Manager+0x17c) behaviors, captured by Slot3
    // (062cb58f / 062cb5c7). Slot7 drives them as child behaviors: it stores its DeltaTime
    // to their +0x1c and dispatches slots 3/4/7/10, the manager's own protocol.
    Behavior* DockDelegate;         // +0x278
    Behavior* GotoDelegate;         // +0x27c
    unsigned char Flag280;          // +0x280: Slot22 sets it when the owner left a trade lane
    unsigned char Flag281;          // +0x281: a trade-lane order was handed to go_tradelane
    unsigned char Flag282;          // +0x282: DockDelegate is running
    unsigned char Flag283;          // +0x283: Slot3 latches the owner's is_player()
    unsigned char Flag284;          // +0x284: GotoDelegate is running
    unsigned char Flag285;          // +0x285: the record's +0xb0 order was consumed
    unsigned char Pad286[2];
    int Field288;                   // +0x288: Slot3 zeroes it; no reader found in this class
    unsigned char Flag28c;          // +0x28c: FormationBehavior::Slot3 sets it from is_player()
    unsigned char Pad28d[3];
    // +0x290: FormationBehavior: the owner's get_formation_offset (Slot3, Slot27);
    // FollowBehavior: the follow offset its slot 10 copies from the directive op.
    Vector Offset290;               // +0x290
    int Field29c;                   // +0x29c: Slot4 zeroes it; no reader found in this class
};
static_assert(offsetof(BaseFollowerBehavior, TypeId) == 0x04, "BaseFollowerBehavior::TypeId offset");
static_assert(offsetof(BaseFollowerBehavior, ModeSelector0xc) == 0x0c, "BaseFollowerBehavior::ModeSelector0xc offset");
static_assert(offsetof(BaseFollowerBehavior, UpdateResult) == 0x10, "BaseFollowerBehavior::UpdateResult offset");
static_assert(offsetof(BaseFollowerBehavior, OwnerContext) == 0x14, "BaseFollowerBehavior::OwnerContext offset");
static_assert(offsetof(BaseFollowerBehavior, DeltaTime) == 0x1c, "BaseFollowerBehavior::DeltaTime offset");
static_assert(offsetof(BaseFollowerBehavior, BehaviorMgrPtr) == 0x20, "BaseFollowerBehavior::BehaviorMgrPtr offset");
static_assert(offsetof(BaseFollowerBehavior, NavHelper) == 0x24, "BaseFollowerBehavior::NavHelper offset");
static_assert(offsetof(BaseFollowerBehavior, Flags28) == 0x28, "BaseFollowerBehavior::Flags28 offset");
static_assert(offsetof(BaseFollowerBehavior, Flag29) == 0x29, "BaseFollowerBehavior::Flag29 offset");
static_assert(offsetof(BaseFollowerBehavior, LeadWatcher) == 0x2c, "BaseFollowerBehavior::LeadWatcher offset");
static_assert(offsetof(BaseFollowerBehavior, MatchThrottle) == 0x34, "BaseFollowerBehavior::MatchThrottle offset");
static_assert(offsetof(BaseFollowerBehavior, Face) == 0x90, "BaseFollowerBehavior::Face offset");
static_assert(offsetof(BaseFollowerBehavior, Move) == 0x150, "BaseFollowerBehavior::Move offset");
static_assert(offsetof(BaseFollowerBehavior, BreakAway) == 0x1c8, "BaseFollowerBehavior::BreakAway offset");
static_assert(offsetof(BaseFollowerBehavior, Afterburner) == 0x264, "BaseFollowerBehavior::Afterburner offset");
static_assert(offsetof(BaseFollowerBehavior, DockDelegate) == 0x278, "BaseFollowerBehavior::DockDelegate offset");
static_assert(offsetof(BaseFollowerBehavior, GotoDelegate) == 0x27c, "BaseFollowerBehavior::GotoDelegate offset");
static_assert(offsetof(BaseFollowerBehavior, Flag280) == 0x280, "BaseFollowerBehavior::Flag280 offset");
static_assert(offsetof(BaseFollowerBehavior, Flag285) == 0x285, "BaseFollowerBehavior::Flag285 offset");
static_assert(offsetof(BaseFollowerBehavior, Field288) == 0x288, "BaseFollowerBehavior::Field288 offset");
static_assert(offsetof(BaseFollowerBehavior, Flag28c) == 0x28c, "BaseFollowerBehavior::Flag28c offset");
static_assert(offsetof(BaseFollowerBehavior, Offset290) == 0x290, "BaseFollowerBehavior::Offset290 offset");
static_assert(offsetof(BaseFollowerBehavior, Field29c) == 0x29c, "BaseFollowerBehavior::Field29c offset");
static_assert(sizeof(BaseFollowerBehavior) == 0x2a0, "BaseFollowerBehavior size (base slot 4 writes +0x29c)");

// ============================================================================
// FormationBehavior -- RTTI ".?AVFormationBehavior@@" (COL 0639fb60 -> 063a8fb8), behavior
// type 10 ("Formation"). vftable .rdata:0639fb64, 29 slots; factory .text:062d1b67
// `PUSH 0x2b8` before the ctor FUN_062cbfe0, so sizeof is 0x2b8.
//
// The state machine that used to be documented here (FUN_062c4900 / FUN_062c3d10) is
// DockBehavior's and lives in common_dockbehavior.cpp (items 1279/1285).
//
// Own slots (the rest are BaseFollowerBehavior's):
//    3  FUN_062cc3b0  Slot3          4  LAB_062cc410  Slot4
//    5  FUN_062cc030  Slot5          6  LAB_062cc0d0  evaluate
//   10  LAB_062cd630  Slot10        12  LAB_062cc990  set_direction
//   27  LAB_062cc470  Slot27        28  LAB_062cc8f0  Slot28
// Re-ported 2026-10-08 (claude-code:cmn-ai5): slots 3/4/5/6/12 were unbound or empty.
// ============================================================================
class FormationBehavior : public BaseFollowerBehavior
{
public:
    // FUN_062cbfe0 (RET 4): FUN_062cb2b0(TypeId); +0x280 = +0x281 = +0x2a8 = 0; vptr;
    // +0x2a0 = 200.0f; +0x2a4 = 500.0f.
    explicit FormationBehavior(int InTypeId);

    // Slot 3, FUN_062cc3b0.
    void Slot3() override;
    // Slot 4, LAB_062cc410.
    void Slot4() override;
    // Slot 5, FUN_062cc030 (RET 4, argument unread).
    int Slot5(float Value) override;
    // Slot 6, LAB_062cc0d0 (RET 4, argument unread): 1 when the owner's target is a valid
    // formation candidate, else 2.
    EvaluateResult evaluate(unsigned int Context) override;
    // Slot 10, LAB_062cd630: `OR byte [ECX+0x28],0x2; MOV AL,1; RET 4` (shared with
    // IdleBehavior's slot 10).
    bool Slot10(void* DirectiveOp) override;
    // Slot 12, LAB_062cc990 (RET 4).
    void set_direction(Vector const& Dir) override;

    // Slot 27, LAB_062cc470 (362 instructions): place the ship in the leader's formation slot.
    void Slot27() override;
    // Slot 28, LAB_062cc8f0 (`RET 4` = hidden return buffer): the owner's formation offset
    // rotated into the leader's frame.
    Vector Slot28() override;

    // ---- FormationBehavior's own fields (+0x2a0..+0x2b8) ----
    // +0x2a0 / +0x2a4: the standoff Slot27 adds to the own physical radius -- +0x2a4 when
    // cruising outside a trade lane, +0x2a0 otherwise. Ctor 200.0f / 500.0f (062cc007/062cc011).
    float StandoffDistance2a0;       // +0x2a0
    float CruiseStandoffDistance2a4; // +0x2a4
    // +0x2a8: set_direction writes 1 (non-zero direction) or 0; Slot3 clears it; Slot27
    // steers along FaceAxis2ac while it is set. Ctor 0.
    unsigned char bDirectionSet2a8;  // +0x2a8
    unsigned char Pad2a9[3];
    // +0x2ac: set_direction's (-y*k, -x*k, -z), the first two clamped to [-1, 1]; Slot27 hands
    // it to FaceIdea::SetAxis. Not ctor-initialised in retail.
    Vector FaceAxis2ac;              // +0x2ac
};

static_assert(offsetof(FormationBehavior, StandoffDistance2a0) == 0x2a0, "FormationBehavior::StandoffDistance2a0 offset");
static_assert(offsetof(FormationBehavior, CruiseStandoffDistance2a4) == 0x2a4, "FormationBehavior::CruiseStandoffDistance2a4 offset");
static_assert(offsetof(FormationBehavior, bDirectionSet2a8) == 0x2a8, "FormationBehavior::bDirectionSet2a8 offset");
static_assert(offsetof(FormationBehavior, FaceAxis2ac) == 0x2ac, "FormationBehavior::FaceAxis2ac offset");
static_assert(sizeof(FormationBehavior) == 0x2b8, "FormationBehavior size == retail operator_new argument at .text:062d1b67");

// ============================================================================
// FUN_062ec520 (Common.dll.txt 251690-251755), "is WantedId one of the ids in the owner's own
// 32-entry formation-follower scratch array", is a member of neither behavior class that calls
// it: both call sites load the receiver identically -- `MOV ECX,[ESI+0x24]; ADD ECX,0x1364` at
// Common.dll.txt:282027-282029 (GotoBehavior::Update) and 206292-206294 (GuideBehavior::Update).
// It is FGunConvergenceStateBlock::IsIdAmongOwnerFormationFollowers (common_systemmanager.h,
// common_gunconvergence.cpp; CMN-GC 2026-10-14).
// ============================================================================
