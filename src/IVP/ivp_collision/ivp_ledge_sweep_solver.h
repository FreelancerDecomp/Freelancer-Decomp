// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// D-cluster-0635cbd0 pass (2026-09-10): investigation of the Common.dll
// address range 0x0635cbd0-0x0635e1d0 (decomp-tracker items 3348-3374),
// the last safe cluster before the DO-NOT-TOUCH range starting at
// ~0x06360000.
//
// This range turned out to hold TWO unrelated subsystems, not one:
//
//   1. 0x0635cbd0-0x0635d360 (items 3348-3354): the "IVP Broadphase" grid/
//      octree insertion, query and removal logic already covered by a full
//      behavioral (non-1:1) rewrite in src/Common/common_physys.cpp -- see
//      that file's own "IVP Broadphase" comment header and the per-item
//      decomp-tracker notes for the exact retail formulas recovered this
//      pass. Marked `skipped` there, not ported here.
//
//   2. 0x0635d5b0-0x0635e1d0 (items 3362-3374, this file): an entirely
//      separate coordinate-frame (retail calls the class "Csys::Csys" per
//      Ghidra's own demangled constructor symbol at Common.dll.c:216960/
//      216963/217113/217121 -- NOT the same class as this project's own
//      src/include/common_csys.h Csys, which is a different, 0x30-byte
//      Pos+Matrix pair; the retail symbol here names an unrelated,
//      0x40-byte-stride, rows[3]+translation object matching this port's
//      own IVP_U_Matrix exactly, most likely a genuine retail name
//      collision between two distinct "Csys" classes in different retail
//      translation units) sweep/distance-test solver operating on
//      IVP_Compact_Ledge-style compressed vertex data. Documented below.
//
// ============================================================================
// The Csys sweep-test family (items 3362-3374)
// ============================================================================
//
// Two generic numeric solver drivers (FUN_06369720, FUN_063694a0) are each
// called from 4 large driver
// functions in this cluster (FUN_0635da70/item 3369, FUN_0635df00/item
// 3371, FUN_0635e1d0/item 3374, and FUN_0635f180/item 3378 -- the fourth
// member, added to this list 2026-09-17 by lane claude:w16-ivp, which
// re-measured it: its 494 instructions contain exactly one
// `CALL FUN_06369720` at Common.dll.txt:0635f431 and two `CALL FUN_063694a0`,
// alongside two `CALL FUN_0635de90` seeds and two vector-ctor-iterator
// calls building its two 21-frame matrix arrays -- i.e. the family shape
// verbatim.)
//
// [2026-09-17, lane claude:w19-csys-sweep] CORRECTION, and the reason the
// paragraph above no longer says "still unported". This file used to claim
// that "both solver addresses sit inside the 0x06363000-0x0636e000
// LCP/Simplex range this marathon excludes by design", and items
// 3369/3371/3374/3378 all rested their `simplified` status on that claim.
// The claim does not survive reading the two bodies end to end
// (Common.dll.txt:468899-469141 and 469148-469420, 207 + 243 insns,
// re-read instruction-by-instruction this pass). Neither function contains
// a permutation array, a ratio test, a pivot step or a matrix: they are a
// conservative-advancement time-of-contact search over two lazily-filled
// 21-slot IVP_U_Matrix caches, plus a regula-falsi refinement
// (FUN_06369280). They are in the excluded ADDRESS range and are not part
// of the excluded SUBSYSTEM. All three are now ported, in
// ivp_ledge_sweep_solver.cpp next to this header. Each is 109-494 insns with ~3KB stack frames
// that construct up to 21 stack-local IVP_U_Matrix-shaped "Csys" frames --
// Common.dll.c:216960-216964/217112-217128/217113-217127) with a pointer
// to one of 7 small "evaluator" objects as an extra argument. Each
// evaluator is its OWN distinct concrete class (7 different one-slot
// vtables at .rdata:063a56d8/56dc/56e0/56ec/56f4/56fc/5700, each pointing
// at exactly one of the 7 LAB_ functions below -- confirmed via
// Common.dll.txt:569198-569224 -- NOT one shared 7-slot vtable, despite
// decomp-tracker's `vtable_method` classification implying a single
// table). All 7 share the same call shape: `this` = the evaluator object
// (ECX), 2 explicit stack args = pointers to two IVP_U_Matrix-shaped
// frames (the two Csys candidates currently under test), return value in
// ST0 (a float, via the x87 `RET 0x8` convention already established
// throughout this port for float-returning thiscalls).
//
// Confirmed shared callees (all already independently ported elsewhere in
// this codebase, cross-checked instruction-by-instruction this pass):
//
//   FUN_063484e0 == IVP_U_Matrix::vmult4(p_in, p_out)       [ivp_math.h]
//     *** CORRECTED 2026-09-17, lane claude:w21-csys-chain. This line used to
//     read "== IVP_U_Matrix::vimult4", and that claim is FALSE. Read the whole
//     body, Common.dll.txt:412790-412843 (0x063484e0-0x06348573): it never
//     subtracts the translation, and the three accumulators are
//       out.x = M[0x00]*px + M[0x04]*py + M[0x08]*pz + M[0x30]
//       out.y = M[0x10]*px + M[0x14]*py + M[0x18]*pz + M[0x34]
//       out.z = M[0x20]*px + M[0x24]*py + M[0x28]*pz + M[0x38]
//     i.e. rows[i] DOT p + vv[i] -- that is vmult4 term-for-term (compare
//     ivp_math.h's own vmult4), not vimult4, which would be
//     rows-transposed times (p - vv). The two differ in BOTH the transpose
//     and the sign of the translation, so the error was not cosmetic: every
//     evaluator below would have transformed its fixed local direction by the
//     wrong matrix. Note the misidentification never reached code, because
//     the only two evaluators ported before this pass (items 3363/3368) do
//     not call FUN_063484e0 at all -- they use FUN_06348580 and FUN_06366010.
//     The real vimult4 leaves in this module are FUN_06348470 (see
//     ivp_math.h's own note) and, for the rotation-only variant,
//     FUN_06366010.
//
//   FUN_06348580 == IVP_U_Matrix3::vmult(p_in, p_out)       [item 2895]
//     Re-verified this pass, Common.dll.txt:412860-412890: rows at
//     [ECX+0x00/0x10/0x20], no translation term. Correct as it stood.
//   FUN_06366010 == IVP_U_Matrix3::vimult(p_in, p_out)      [item 3524]
//   FUN_06347df0(x) == 1.0f / sqrtf(x)                      [confirmed
//     this pass: Common.dll.txt:411875-411885, FLD/FSQRT/FDIVR 1.0]
//   FUN_06347ef0(this=IVP_U_Point) == normize()-equivalent  [confirmed
//     this pass: Common.dll.txt:412034-412074, same 1e-10 epsilon guard
//     and per-axis scale as IVP_U_Point::normize() in ivp_math.h, but
//     ALSO returns a bool (0 if too small to normalize) -- retail's
//     normize() and this port's are not quite the same signature]
//   FUN_0628bed0(this=out, a, b) == the alias-safe x87 cross product,
//     ported as IVP_CalcCrossProduct (ivp_math.h)
//   FUN_06347e60 == real_length()-equivalent (VectorLength, per this
//     port's own ivp_math.h cross-reference comment at line ~198)
//
// [2026-09-17, lane claude:w21-csys-chain] ALL NINE EVALUATORS ARE NOW
// PORTED. The paragraph immediately below is kept verbatim because it is the
// record of what was true before this pass, but its conclusion ("left
// `simplified` rather than guessed") no longer applies to any member of the
// family. Every one of the seven that was still an Evaluate_Deferred stub
// returning 0.0f -- items 3362, 3364, 3365, 3366, 3367, 3372, 3373 -- was
// hand-traced this pass to the same standard as the two below: the FPU stack
// and the ESP-relative offsets were simulated instruction by instruction from
// each function's first byte to its RET, with every POP ESI / POP EDI /
// PUSH-before-CALL re-basing of ESP accounted for. That re-basing is exactly
// where a casual read goes wrong: item 3362, for instance, stores its two
// mid-function results through [ESP+0x14]/[ESP+0x18] and reads them back as
// [ESP+0x10]/[ESP+0x14], because a `POP ESI` sits between the writes and the
// reads (:0635d683). Reading the offsets without tracking that pop makes the
// operands look mismatched and the function look unportable.
//
// The nine share one shape, which is what finally made them legible: each
// takes two candidate frames, pushes a small fixed set of its own local
// points through those frames (vmult4 for positions, vmult for directions),
// and reduces the result to one scalar. The reductions divide cleanly into
// the three cases a ledge sweep needs:
//   * point-vs-plane separation   (3362, 3365, 3363, 3368)
//   * point-vs-point distance, clamped to a projection (3364, 3367)
//   * edge-vs-edge separation and its degeneracy guard (3366, 3372, 3373)
// Their mutual consistency is itself evidence: 3362 and 3365 compute the same
// quantity with the two frames' roles exchanged, and 3373 computes exactly the
// squared cross-product length whose reciprocal square root 3372 needs.
//
// (Historical paragraph, superseded by the one above:)
// Only 2 of the 7 evaluators were fully hand-traced (FPU-stack-simulated
// instruction-by-instruction, including exact ESP-relative offsets across
// each CALL) with enough confidence to port as real, buildable code --
// Evaluate_0635d6d0 and Evaluate_0635da20 below. Both confirmed to read
// the SAME two evaluator-object fields (an IVP_U_Point at +0x14, another
// at +0x24 -- stride 0x10 matching IVP_U_Point's own layout exactly), and
// both compute a plain dot product of two matrix-transformed copies of
// those points -- strong, consistent evidence for the family's general
// shape (a set of constraint/Jacobian-style "project this fixed local
// direction through these two candidate frames and dot the results"
// evaluators, geometrically similar to the still-unidentified
// Baraff-style impact/constraint solver machinery already flagged
// elsewhere in this port -- see ivp_mindist_limiter.cpp's own DoImpact
// notes). The remaining 5 (items 3362/3364/3365/3366/3367/3372/3373 --
// see each item's own decomp-tracker notes for its individually-confirmed
// callee sequence and touched field offsets) additionally involve
// cross products, sqrt/invsqrt normalization and, in 2 cases, comparisons
// against a fixed DAT_063a5668 scale constant -- genuinely different
// per-evaluator formulas, not a mechanical copy of the two below, and
// were NOT independently re-derived with the same rigor this pass. Left
// `simplified` rather than guessed, per this project's own established
// precedent for cross-cutting numerical-solver clusters (see the Baraff
// LCP-solver family, decomp-tracker items 3540-3598).
//
// The 3 large drivers (items 3369/3371/3374) additionally walk a doubly-
// linked structure via a `(uint)ptr & 0xc` -> DAT_063ee4e0/DAT_063ee4f0
// indexed-jump-table pointer-arithmetic idiom (Common.dll.c:217015-217047)
// whose purpose (most likely a compact edge/twin traversal, given the
// 16-bit-index * 0x10 + base vertex-fetch pattern alongside it -- see
// e.g. Common.dll.c:216969/217129, `(*param & 0xffff) * 0x10 + *param_3`)
// was not resolved this pass.
//
// [2026-09-17, lane claude:w22-csys-drivers] RESOLVED, with the bytes. It is
// the compact-mesh next-edge / previous-edge step, and it is not a "doubly
// linked structure" at all -- there are no links, only two four-entry
// constant tables indexed by the low nibble of the edge pointer itself.
// Read straight out of .data (Common.dll.txt:642753-642794):
//
//     .data:063ee4e0  00 00 00 00   04 00 00 00   04 00 00 00   f8 ff ff ff
//                     ->  { 0, +4, +4, -8 }
//     .data:063ee4f0  00 00 00 00   08 00 00 00   fc ff ff ff   fc ff ff ff
//                     ->  { 0, +8, -4, -4 }
//
// An IVP_Compact_Triangle is 16 bytes and 16-byte aligned: a 4-byte header
// followed by its three 4-byte IVP_Compact_Edges, so a live edge pointer
// always has `ptr & 0xc` equal to 4, 8 or 0xc -- that value is the edge's
// own byte offset inside its triangle, and it is the table index (the tables
// are indexed in BYTES, which is why every use spells it `[reg + DAT_...]`
// with reg already equal to `ptr & 0xc` and no scaling). Adding the entry
// walks around the triangle:
//     next: 4 -> 8 (+4),  8 -> c (+4),  c -> 4 (-8)
//     prev: 4 -> c (+8),  8 -> 4 (-4),  c -> 8 (-4)
// Index 0 would be the triangle header, never a valid edge; both tables hold
// 0 there, a harmless identity.
//
// Two independent confirmations that this is what the code means:
//   * FUN_0635b8d0 (Common.dll.txt:445590-445631) applies both tables to the
//     same edge and turns all three results into vertex addresses through the
//     identical `(dword & 0xffff) * 0x10 + Ledge + Ledge->CPointOffset`
//     expression -- i.e. three vertices of one triangle, which is only
//     coherent if the two steps are next and prev.
//   * this project ALREADY ported both tables, byte-for-byte, as
//     G_IVP_Compact_Edge_Next_Lut / G_IVP_Compact_Edge_Prev_Lut behind
//     IVP_Compact_Edge::GetNextEdge() / GetPrevEdge()
//     (ivp_compact_builder/ivp_compact_ledge.h:23-101). The values there
//     match the .data bytes above exactly. So the "unresolved idiom" this
//     header and four decomp-tracker items have carried since 2026-09-10 was
//     in fact resolved elsewhere in the tree the whole time; it was a
//     bookkeeping gap, not a research gap. Use the header's accessors.
//
// [2026-09-17, lane claude:w19-csys-sweep] The second half of that reason
// ("porting the drivers without first resolving FUN_06369720/FUN_063694a0's
// own calling convention would mean guessing the wiring") is now DISCHARGED:
// the calling convention of both solvers is fully recovered and written out
// below as IVP_Ledge_Sweep_Evaluator's three methods. What still blocks the
// 4 drivers is the OTHER half, which remains true and is a TODO, not an
// accepted state:
//   (a) 5 of the 7 evaluator functors are still Evaluate_Deferred stubs
//       returning 0.0f. FUN_0635da70 alone installs PTR_LAB_0635d5b0
//       (Common.dll.txt:0635db19) -- one of the 5 -- as its first
//       evaluator, so porting the driver against a 0.0f stub would produce
//       a driver that always reports contact at t0.
//   (b) the `(uint)ptr & 0xc` -> DAT_063ee4e0/DAT_063ee4f0 traversal idiom
//       above is still unresolved.
// Both are ordinary unfinished porting work inside this cluster.
//
// [2026-09-17, lane claude:w21-csys-chain] STATUS OF (a) AND (b), AND WHAT IS
// LEFT.
//   (a) DISCHARGED. There are no Evaluate_Deferred stubs left; all nine
//       evaluators are real bodies below, and Evaluate_Deferred itself has
//       been deleted rather than left behind as a trap for the next lane.
//   (a') ALSO DISCHARGED: item 3370 / FUN_0635de90, the IVP_Sweep_Frame_Cache
//       constructor that all four drivers seed both of their caches through,
//       is ported as IVP_Sweep_Frame_Cache::Init in the .cpp next to this
//       header.
//   (b) DISCHARGED 2026-09-17 by lane claude:w22-csys-drivers -- see the
//       byte-level resolution of the two tables further up this file. It is
//       IVP_Compact_Edge::GetNextEdge()/GetPrevEdge(), already in
//       ivp_compact_builder/ivp_compact_ledge.h.
//
// [2026-09-17, lane claude:w23-csys-drivers] ALL FOUR PRIMARY DRIVERS ARE NOW
// PORTED. Items 3374 (FUN_0635e1d0) and 3378 (FUN_0635f180) landed this pass as
// EstimateLedgeEdgeVsLedgeEdgeContact and EstimateLedgeVertexVsLedgeEdgeContact
// in the .cpp next to this header, joining 3369 and 3371. Together the four
// cover the three feature pairings a ledge sweep needs -- point-vs-face
// (3371), vertex-vs-face (3369), edge-vs-edge (3374) and vertex-vs-edge
// (3378) -- which is itself a consistency check on all four readings.
//
// TWO CORRECTIONS THIS PASS, both to claims this header carried:
//
//   * FUN_0634d8d0 / FUN_0634d9d0 / FUN_0634d970, read in full from the
//     listing (Common.dll.txt:422762-422810, 422890-422917 and 422826-422856):
//     they are vmult4, vmult and vimult respectively -- the w16-ivp census was
//     right about the operation, and the FUN_063484e0 fiasco does NOT repeat
//     here. But all three read their matrix at `this + 0x1c`, not at `this`,
//     so the receiver is the enclosing cache-source object and not a matrix.
//     Item 3378's four call sites pass Side->Source. The census did not say
//     this and a transcription that hands them a matrix reads 0x1c bytes early.
//     Incidental: that these three take the object rather than the matrix is
//     real evidence for the IVP_Cache_Object reading of
//     FIVP_Sweep_Cache_Source below, which this header declined to assert.
//     Still not asserted -- recorded as evidence.
//
//   * the "shared scalar block" (InnerThreshold = Radius*0.5 + ToleranceBase;
//     KnownDistance = Radius + SeparationGap; Threshold = Ramp + Radius) is
//     NOT shared by all four. Items 3369 and 3371 use it. Item 3374 uses none
//     of it -- it never reads CombinedRadius or SeparationGap at all, and all
//     six of its solver calls pass a NULL known distance. Item 3378 uses the
//     Threshold line but multiplies its radius by 0.9f (DAT_063a53fc, raw
//     66 66 66 3f) rather than 0.5f, and it too passes a NULL known distance
//     everywhere. Treat the block as a pattern that recurs, not an invariant.
//
// Item 3377 (FUN_0635f020, 96 insns) also landed this pass, as
// EstimateBallVsBallContact -- the family's base case, two objects treated as
// points at their own frame origins, with no ledge, edge or point array
// touched anywhere in its body. It takes its two FIVP_Sweep_Cache_Source
// arguments DIRECTLY rather than through FIVP_Sweep_Ledge_Side, and it is
// reached from a THIRD dispatcher, FUN_0635fe30 (item 3381, :0635fed5).
//
// WORTH KNOWING BEFORE TOUCHING ITS THREE REMAINING SIBLINGS: item 3377 needed
// no new research at all. Its `simplified` status rested on three stated
// blockers -- FUN_0635de90 "blocked, unresolved 21-slot struct", FUN_06369720
// "the unported Simplex/LCP entry point", and "`Ctx`'s owning class
// unidentified" -- and all three had been closed elsewhere in this file
// between 2026-09-10 and now. That is the second time in this cluster a
// `simplified` status outlived its reason (items 3374 and 3378 were the same,
// corrected by lane w22). Re-check items 3375/3376/3379's stated blockers
// against the current tree before spending any research budget on them.
//
// [2026-09-17, lane claude:w24-sweep-rest] THE WHOLE CLUSTER IS NOW PORTED --
// the three remaining drivers AND the four dispatch handlers above them.
//
// THE RE-CHECK PAID OFF A THIRD TIME. Items 3375, 3376 and 3379 were each held
// `simplified` on three stated blockers apiece, and every one of the three was
// already closed in this file before this pass started:
//   * "FUN_0635de90 (item 3370, still `blocked`)" -- item 3370 is
//     `ready_unverified`, ported as IVP_Sweep_Frame_Cache::Init below since
//     2026-09-17 17:19.
//   * "FUN_06369720 / FUN_063694a0 (items 3598/3597, `partial`/unported),
//     entry points into the deliberately-declined-to-port Simplex/LCP solver
//     (items 3540-3598)" -- both are `ready_unverified` since 2026-09-17 14:16,
//     and the LCP attribution itself was refuted by lane w19-lcp-audit.
//   * "`Ctx`/`this`'s owning class unidentified, no struct declared" -- it is
//     IVP_Sweep_Estimator_Context below, and each of the three touches exactly
//     that field set.
// That is now FIVE items in this one cluster (3374, 3378, 3377, and these
// three) whose `simplified` status outlived its reason. No new research budget
// was needed to discharge any of the nine recorded blockers; the work was
// reading the nine hundred instructions.
//
// THE PLACEHOLDER ARGUMENT TYPE IS ALSO RESOLVED, and not by guessing. Item
// 3379 is what settles it: it calls FUN_0634d8d0, FUN_0634d9d0 and
// FUN_0634d970 with `ECX = SideB->Source` (:0635fa2f, :0635fa44, :0635fa93 --
// and :0635fa93 is in FUN_0634d970's own XREF list in ivp_real_object.h). Those
// three are already ported in THIS TREE as IVP_TransformSlotToWorldSpace,
// IVP_RotateSlotToWorldSpace and IVP_RotateWorldToSlotSpace, whose receiver is
// `IVP_NarrowPhaseCacheSlot*` (ivp_real_object.h:811-873, item 3076/3077 plus
// the item-3514/3518 pass). Lay the two declarations side by side:
//       IVP_NarrowPhaseCacheSlot      FIVP_Sweep_Cache_Source
//         +0x00 LastRefreshTick         +0x00 Field_0x00 -> cache's +0x10
//         +0x04 RefCount                +0x04 (never read by Init)
//         +0x08 Owner  IVP_Real_Object* +0x08 Object IVP_Real_Object*
//         +0x1c WorldTransform (matrix) +0x1c Frame (matrix)
// Same type, derived twice from opposite directions. That also explains the
// three helpers' `this + 0x1c` matrix base, which lane w23 flagged as a trap:
// they take the SLOT, not the matrix, because the slot is what the sweep
// drivers carry. FIVP_Sweep_Cache_Source is deliberately NOT deleted in favour
// of the other name here -- IVP_NarrowPhaseCacheSlot is defined inside
// ivp_real_object.cpp, not its header, so it is not reachable from this file,
// and hoisting it is a cross-file edit. OPEN TODO, recorded not accepted:
// hoist IVP_NarrowPhaseCacheSlot into ivp_real_object.h and make
// FIVP_Sweep_Cache_Source an alias of it. Until then the two are layout-
// compatible by construction and this file uses `Source->Frame.<op>()` where
// retail calls the three helpers -- identical code, one indirection fewer.
//
// STILL OPEN in this cluster (nothing accepted as permanent):
//   * (closed 2026-10-09 by CMN-PHY8: FUN_0634f320 now builds the single
//     IVP_Sweep_Estimator_Context declared below; the former private
//     duplicate in ivp_mindist_limiter.cpp is gone.)
//   * item 3117's own `simplified` status: the 330-instruction tail that was
//     unreachable for want of this layer is now reachable. See this file's
//     .cpp for the verdict.

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_math.h"

class IVP_Real_Object;
class IVP_Core;
class IVP_Time;

// ============================================================================
// IVP_Sweep_Frame_Cache -- PORT-LOCAL NAME (retail's own class name for this
// object is not recovered; Ghidra only ever sees it as a 0x5a8-byte stack
// local).
//
// Layout proved from FUN_0635de90 (Common.dll.txt:449713-449752), the
// constructor all 4 drivers call on it, cross-checked against the two
// solvers' own indexing:
//   +0x00  Object          `MOV [ECX],EAX` where EAX = CtorArg->[0x08]; the
//                          receiver of every FUN_06345850 call the solvers
//                          make (== IVP_Real_Object::get_m_world_f_object_at_transform,
//                          decomp-tracker item 2815).
//   +0x04  Core            `MOV [ECX+4],[Object+0x98]`; +0x98 is
//                          IVP_Real_Object::core throughout this port.
//   +0x08/+0x0c            never touched by the ctor or by either solver.
//   +0x10                  `MOV [ECX+0x10],[CtorArg]`.
//   +0x14  FrameAtSlot[21] the lazily-filled pointer table. The ctor fills
//                          it downward from +0x64: 21 entries when
//                          `(Object->[0x78] << 0x18) >= 0x8000000` (all
//                          pointing at CtorArg+0x1c, i.e. one fixed frame
//                          for a non-moving object), otherwise 20 zeroes
//                          plus an explicit `[ECX+0x14] = CtorArg+0x1c`.
//                          Either way slot 0 is always valid on entry,
//                          which is why both solvers read FrameAtSlot[0]
//                          without a null check.
//   +0x68  FrameStorage[21] 21 * 0x40 bytes. Retail builds these with the
//                          vector-ctor-iterator FUN_06273bc0(ptr, 0x40,
//                          0x15, Csys::Csys) immediately before calling
//                          FUN_0635de90 on the 0x68 bytes just below them
//                          (Common.dll.txt:0635da84-0635daa4), and the two
//                          stack locals are exactly 0x68 apart -- i.e. one
//                          object, not two. 0x40 == sizeof(IVP_U_Matrix).
// Total 0x68 + 21*0x40 = 0x5a8, matching local_5a8 in every driver.
// ============================================================================
// ----------------------------------------------------------------------------
// FIVP_Sweep_Cache_Source -- PORT-LOCAL PLACEHOLDER for the single argument
// IVP_Sweep_Frame_Cache::Init takes. Retail's own type for it is NOT
// recovered and is deliberately NOT guessed here.
//
// What IS proved, from FUN_0635de90's 36 instructions plus its call site in
// FUN_0635da70 (Common.dll.txt:0635da78-0635daa4):
//   * the drivers obtain it as `param_3->[0x08]` / `param_4->[0x08]`
//     (:0635da81 `MOV EDI,[EBP+8]`, :0635dab0 `MOV EDI,[EBX+8]`), i.e. it is
//     itself reached one indirection below each of the two objects the driver
//     is testing;
//   * +0x00 is a pointer, copied verbatim into the cache's own +0x10;
//   * +0x08 is an IVP_Real_Object* -- proved, not assumed, because the very
//     next instruction dereferences it at +0x98 and stores the result as the
//     cache's Core, and +0x98 is IVP_Real_Object::core throughout this port
//     (ivp_real_object.h:570), while +0x78, read two instructions later, is
//     IVP_Real_Object::flags (ivp_real_object.h:518);
//   * +0x1c is a ready-made IVP_U_Matrix: `LEA ESI,[EDI+0x1c]` is what every
//     one of the 21 slot pointers is set to.
// Nothing between +0x0c and +0x18 is read by this function, so those are
// placeholders in the strict sense -- size and meaning both unknown. The
// shape (a real object, plus a prepared world transform) is consistent with
// IVP's IVP_Cache_Object, but this port has no such class and the match was
// NOT established, so the name is not borrowed.
// ----------------------------------------------------------------------------
struct FIVP_Sweep_Cache_Source
{
    const void *Field_0x00;             // +0x00 -- copied into the cache's +0x10
    int Field_0x04;                     // +0x04 -- never read by Init
    IVP_Real_Object *Object;            // +0x08
    int Field_0x0c;                     // +0x0c -- never read by Init
    int Field_0x10;                     // +0x10 -- never read by Init
    int Field_0x14;                     // +0x14 -- never read by Init
    int Field_0x18;                     // +0x18 -- never read by Init
    IVP_U_Matrix Frame;                 // +0x1c
};
static_assert(offsetof(FIVP_Sweep_Cache_Source, Object) == 0x08, "FIVP_Sweep_Cache_Source::Object");
static_assert(offsetof(FIVP_Sweep_Cache_Source, Frame) == 0x1c, "FIVP_Sweep_Cache_Source::Frame");

struct IVP_Sweep_Frame_Cache
{
    IVP_Real_Object *Object;            // +0x00
    IVP_Core *Core;                     // +0x04
    int Field_0x08;                     // +0x08 -- purpose unknown, never written by the ctor
    int Field_0x0c;                     // +0x0c -- purpose unknown, never written by the ctor
    const void *Field_0x10;             // +0x10 -- ctor: `= *CtorArg`
    IVP_U_Matrix *FrameAtSlot[21];      // +0x14
    IVP_U_Matrix FrameStorage[21];      // +0x68

    // FUN_0635de90 (Common.dll.txt:449711-449757, i.e. the byte range
    // 0x0635de90-0x0635def0, 36 insns; decomp-tracker item 3370). thiscall,
    // one stack argument, `RET 0x4`. It is a pure output-struct initializer:
    // it never reads any pre-existing field of `this`, which is why the
    // drivers can hand it raw stack memory. Note it does NOT touch
    // FrameStorage -- retail runs the MSVC vector-constructor-iterator over
    // that array first, with a constructor (Csys::Csys) whose entire retail
    // body is `MOV EAX,ECX; RET`, so that step is a runtime no-op and is not
    // reproduced here.
    // Source is deliberately NOT const-qualified: retail stores
    // `Source + 0x1c` straight into FrameAtSlot[], which the solvers then
    // hand out as a live frame pointer, so the cache genuinely aliases the
    // source's own storage rather than copying it.
    void Init(FIVP_Sweep_Cache_Source *Source);
};
static_assert(sizeof(IVP_Sweep_Frame_Cache) == 0x5a8, "IVP_Sweep_Frame_Cache size");
static_assert(offsetof(IVP_Sweep_Frame_Cache, FrameAtSlot) == 0x14, "IVP_Sweep_Frame_Cache::FrameAtSlot");
static_assert(offsetof(IVP_Sweep_Frame_Cache, FrameStorage) == 0x68, "IVP_Sweep_Frame_Cache::FrameStorage");

// ============================================================================
// IVP_Ledge_Sweep_Evaluator -- PORT-LOCAL NAME for the abstract base of the 7
// one-slot evaluator classes documented above.
//
// Field layout proved from FUN_0635da70's construction of its first
// evaluator (the stack local Ghidra calls local_ba4):
//   +0x00  vfptr           set to PTR_LAB_0635d5b0 at Common.dll.txt:0635db19.
//   +0x04  MaxRelativeSpeed    `FLD [ESI+8]; FSTP [local_ba0]` (:0635db9d).
//   +0x08  IMaxRelativeSpeed   `FLD 1.0; FDIV [local_ba0]; FSTP [local_b9c]`
//                          (:0635dba4-:0635dbba) -- i.e. exactly
//                          1.0f / MaxRelativeSpeed. This is what makes the
//                          two solvers agree: FUN_06369720 divides by
//                          `[ESI+4]` at :063697a8 while FUN_063694a0
//                          multiplies by `[EBP+8]` at :06369523, and both
//                          therefore compute the same (distance / speed)
//                          step.
//   +0x0c/+0x10            unidentified; the drivers' own derived classes
//                          put an IVP_U_Point at +0x14 and another at
//                          +0x24 (see Evaluate_0635d6d0 / Evaluate_0635da20
//                          below), so 2 dwords must sit here. Kept as
//                          named placeholders so any future derived class
//                          lands on the right offsets.
//
// The single virtual slot takes the two cached frames in the order
// (frame of the param_7 cache, frame of the param_8 cache) -- proved four
// separate times, at :063694ce/:063694cf, :06369664/:06369669,
// :06369745/:06369746 and :06369905/:0636990a, in each case by tracking
// which PUSH lands at [ESP+0] (the first argument) rather than by offset
// resemblance.
// ============================================================================
class IVP_Ledge_Sweep_Evaluator
{
public:
    // NOTE: deliberately NO virtual destructor. Retail's 7 evaluator vtables
    // (.rdata:063a56d8/56dc/56e0/56ec/56f4/56fc/5700) are one slot wide and
    // that slot is Evaluate; adding a destructor slot would move Evaluate off
    // offset 0 and break every `MOV EDX,[ECX]; CALL [EDX]` this file models.
    // The evaluators are stack locals in their drivers and are never deleted
    // through a base pointer.

    // Retail's one and only vtable slot (thiscall, RET 0x8). Every one of the
    // nine bodies leaves its result UNROUNDED in ST0, and the solvers compare
    // it unrounded (063693f8, 0636966c FST + FCOMP, 0636990d) -- hence double
    // (cmn-ivp3f 2026-10-09; was float).
    virtual double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const = 0;

    // FUN_063694a0 (Common.dll.txt:468899-469141, 207 insns).
    int SearchContactTime(float Threshold, float CurTimeBase, float CurTimeOffset,
                          float EndTimeBase, float EndTimeOffset, int StartSlot,
                          IVP_Sweep_Frame_Cache *CacheA, IVP_Sweep_Frame_Cache *CacheB,
                          const float *KnownDistance, IVP_Time *OutTime);

    // FUN_06369720 (Common.dll.txt:469148-469420, 243 insns).
    int SearchContactTimeFromContact(float Threshold, float InnerThreshold,
                                     float CurTimeBase, float CurTimeOffset,
                                     float EndTimeBase, float EndTimeOffset,
                                     IVP_Sweep_Frame_Cache *CacheA, IVP_Sweep_Frame_Cache *CacheB,
                                     const float *KnownDistance, IVP_Time *OutTime);

    // FUN_06369280 (Common.dll.txt:468718-468884, 136 insns).
    void RefineContactTime(IVP_Time *OutTime, float LoTimeBase, float LoTimeOffset,
                           float HiTimeBase, float HiTimeOffset, float Threshold,
                           float LoDistance, float HiDistance,
                           IVP_Real_Object *ObjectA, IVP_Real_Object *ObjectB);

    float MaxRelativeSpeed;   // +0x04
    float IMaxRelativeSpeed;  // +0x08
    int Field_0x0c;           // +0x0c -- unidentified, see comment above
    int Field_0x10;           // +0x10 -- unidentified, see comment above
};

namespace IVP_LedgeSweepEvaluatorsDeferred
{
    // Common.dll.txt:448988-449017 (decomp-tracker item 3363, 26 insns).
    // Fully hand-traced this pass (FPU stack + ESP-relative offsets
    // simulated instruction-by-instruction across both CALLs):
    //   Local1 = FrameA->vmult(EvaluatorThis->P0)   // P0 at +0x14
    //   Local2 = FrameB->vmult(EvaluatorThis->P1)   // P1 at +0x24
    //   return dot(Local1, Local2)
    // cmn-ivp3f 2026-10-09 (.text:0635d6d0-0635d71a): both transforms are
    // FUN_06348580 calls; the dot is (L1.z*L2.z + L1.y*L2.y) + L2.x*L1.x,
    // returned unrounded.
    inline double Evaluate_0635d6d0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                     const IVP_U_Matrix *FrameB)
    {
        const IVP_U_Point *P0 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x14);
        const IVP_U_Point *P1 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x24);
        IVP_U_Point Local1, Local2;
        FrameA->vmult(P0, &Local1);
        FrameB->vmult(P1, &Local2);
        return ((double)(Local1.k[2]) * Local2.k[2] + (double)(Local1.k[1]) * Local2.k[1]) +
               (double)(Local2.k[0]) * Local1.k[0];
    }

    // Common.dll.txt:449308-449334 (decomp-tracker item 3368, 26 insns).
    // Fully hand-traced this pass, same method as Evaluate_0635d6d0 above.
    // Notably chains vmult (rotate INTO FrameB) with vimult (rotate back
    // OUT via FrameA's inverse) before the final dot -- i.e. re-expresses
    // EvaluatorThis->P1 (transformed by FrameB) in FrameA's local space,
    // then projects it onto the fixed local direction EvaluatorThis->P0:
    //   Local1 = FrameB->vmult(EvaluatorThis->P1)    // P1 at +0x24
    //   Local2 = FrameA->vimult(Local1)
    //   return dot(Local2, EvaluatorThis->P0)         // P0 at +0x14
    // cmn-ivp3f 2026-10-09 (.text:0635da20-0635da68): dot is
    // (L2.y*P0.y + L2.z*P0.z) + L2.x*P0.x, returned unrounded.
    inline double Evaluate_0635da20(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                     const IVP_U_Matrix *FrameB)
    {
        const IVP_U_Point *P0 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x14);
        const IVP_U_Point *P1 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x24);
        IVP_U_Point Local1, Local2;
        FrameB->vmult(P1, &Local1);
        FrameA->vimult(&Local1, &Local2);
        return ((double)(Local2.k[1]) * P0->k[1] + (double)(Local2.k[2]) * P0->k[2]) +
               (double)(Local2.k[0]) * P0->k[0];
    }

    // ------------------------------------------------------------------------
    // The seven evaluators ported 2026-09-17 by lane claude:w21-csys-chain.
    //
    // Each was read from its first byte to its RET; the byte range is given on
    // each function. They are written as free functions taking the evaluator
    // as `const void *` for the same reason the two above are: the nine are
    // nine DISTINCT retail classes with nine different field layouts sharing
    // one one-slot vtable shape, and item 3367 in particular proves they are
    // not one class -- it puts a float at +0x14 and starts its points at
    // +0x18, where every other member starts its points at +0x14. Inventing a
    // class hierarchy to hold that would be guessing; the offsets below are
    // measured.
    //
    // Helper: fetch the evaluator's own local point at a measured offset.
    // ------------------------------------------------------------------------
    inline const IVP_U_Point *EvaluatorPoint(const void *EvaluatorThis, int Offset)
    {
        return reinterpret_cast<const IVP_U_Point *>(
            static_cast<const char *>(EvaluatorThis) + Offset);
    }

    inline float EvaluatorFloat(const void *EvaluatorThis, int Offset)
    {
        return *reinterpret_cast<const float *>(
            static_cast<const char *>(EvaluatorThis) + Offset);
    }

    // Common.dll.txt:448882-448975, bytes 0x0635d5b0-0x0635d6c1 (item 3362,
    // 93 insns -- the largest of the nine, and the one every driver but item
    // 3374 installs).
    //
    // Only the FIRST of its three transforms is an actual CALL; retail
    // inlined the other two. The inlined pair is what makes the body long,
    // and reading it is what proves FUN_063484e0 is vmult4 (below, the
    // inlined FrameB transform of P34 adds [EAX+0x30/0x34/0x38] -- the
    // translation -- to a plain rows-dot-point, and the called transform of
    // P14 must be the same operation for the subtraction that follows to
    // mean anything).
    //
    //   Position = FrameA->vmult4(P14)              // CALL FUN_063484e0
    //   Anchor   = FrameB->vmult4(P34)              // inlined, :0635d5c8-:0635d63a
    //   Normal   = FrameB->vmult (P24)              // inlined, :0635d63c-:0635d686
    //   return dot(Position - Anchor, Normal)
    //
    // i.e. the signed distance of a point on object A from a plane carried by
    // object B. NOTE the retail body never materializes Anchor.z or the third
    // component of the difference in memory -- Anchor.z lives on the x87
    // stack from :0635d63a all the way to the FSUBR at :0635d6a2. That is
    // also why the field offsets here are 0x14 / 0x24 / 0x34: three
    // IVP_U_Points at the natural 0x10 stride. (Item 3362's pre-existing
    // decomp-tracker note claimed "a tightly-packed, 0xC-stride 2-row partial
    // matrix, NOT IVP_U_Point's usual 0x10 stride". That is wrong -- the note
    // was reading the two inlined transforms' 0xC-spaced FLD operands, which
    // are three matrix ROWS, as if they were the evaluator's own fields.)
    // cmn-ivp3f 2026-10-09, x87-exact (.text:0635d5b0-0635d6c1). The inlined
    // Anchor transform is NOT FUN_063484e0's rounding: x/y are spilled as
    // floats after the first two terms only, then float((r.z*p.z + t) + vv);
    // Anchor.z stays extended to the final FSUBR. The inlined Normal is
    // x = (r0.x*p.x + r0.z*p.z) + r0.y*p.y, y likewise with r1,
    // z = (r2.y*p.y + r2.x*p.x) + r2.z*p.z, each FSTP dword.
    // Result ((Dz*Nz) + Dy*Ny) + Dx*Nx with Dx/Dy float, Dz extended.
    inline double Evaluate_0635d5b0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point Position;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &Position); // 0635d5c3

        const IVP_U_Point *P34 = EvaluatorPoint(EvaluatorThis, 0x34);
        const double Px = P34->k[0];
        const double Py = P34->k[1];
        const double Pz = P34->k[2];
        float Tx = (float)(FrameB->rows[0].k[0] * Px);
        float Ty = (float)(FrameB->rows[1].k[0] * Px);
        double Ez = Px * FrameB->rows[2].k[0];
        Tx = (float)(FrameB->rows[0].k[1] * Py + Tx);
        Ty = (float)(FrameB->rows[1].k[1] * Py + Ty);
        Ez = Ez + FrameB->rows[2].k[1] * Py;
        const float AnchorX = (float)((FrameB->rows[0].k[2] * Pz + Tx) + FrameB->vv.k[0]);
        const float AnchorY = (float)((FrameB->rows[1].k[2] * Pz + Ty) + FrameB->vv.k[1]);
        const double AnchorZ = (Ez + FrameB->rows[2].k[2] * Pz) + FrameB->vv.k[2];

        const IVP_U_Point *P24 = EvaluatorPoint(EvaluatorThis, 0x24);
        const float NormalX = (float)(((double)(FrameB->rows[0].k[0]) * P24->k[0] +
                                                  (double)(FrameB->rows[0].k[2]) * P24->k[2]) +
                                                 (double)(FrameB->rows[0].k[1]) * P24->k[1]);
        const float NormalY = (float)(((double)(FrameB->rows[1].k[0]) * P24->k[0] +
                                                  (double)(FrameB->rows[1].k[2]) * P24->k[2]) +
                                                 (double)(FrameB->rows[1].k[1]) * P24->k[1]);
        const float NormalZ = (float)(((double)(FrameB->rows[2].k[1]) * P24->k[1] +
                                                  (double)(FrameB->rows[2].k[0]) * P24->k[0]) +
                                                 (double)(FrameB->rows[2].k[2]) * P24->k[2]);

        const float DeltaX = Position.k[0] - AnchorX;                         // 0635d68a-0635d692
        const float DeltaY = Position.k[1] - AnchorY;                         // 0635d696-0635d69e
        const double DeltaZ = (double)(Position.k[2]) - AnchorZ;   // 0635d6a2 FSUBR
        return (DeltaZ * NormalZ + (double)(DeltaY) * NormalY) + (double)(DeltaX) * NormalX;
    }

    // Common.dll.txt:449018-449071, bytes 0x0635d720-0x0635d7ac (item 3364,
    // 52 insns).
    //
    //   PositionA = FrameA->vmult4(P14)
    //   PositionB = FrameB->vmult4(P24)
    //   Delta     = PositionB - PositionA
    //   Projected = dot(Delta, P34) * 1.2f        // P34 read RAW, untransformed
    //   return (fabsf(Projected) * Projected < Delta.quad_length())
    //              ? Projected : sqrtf(Delta.quad_length());
    //
    // The final test is retail's, transcribed literally rather than
    // simplified. Its effect is `min(Projected, |Delta|)` for positive
    // Projected -- `fabsf(s)*s` is s*s with s's sign, so a negative Projected
    // always compares below a non-negative quad_length and is returned
    // unchanged, which is how a clamp is written without a branch on the
    // sign. Branch polarity proved from the MASK AND THE JUMP together:
    // :0635d796 FCOMP [ESP+0x24]; FNSTSW AX; TEST AH,0x1 (C0 alone); JNZ ->
    // the jump (returning Projected) is taken when C0 is SET, i.e. when
    // ST0 < mem, i.e. strictly-less. The fall-through is >= (or unordered),
    // and it is the fall-through that replaces the value with the square
    // root.
    //
    // The 1.2f multiplier is DAT_063a5668, read as raw bytes:
    // 9a 99 99 3f == 0x3F99999A == 1.2f. Ghidra's own listing comment says
    // "= 9Ah", which is only the first byte and means nothing.
    // cmn-ivp3f 2026-10-09, x87-exact: D = B - A kept extended; Q =
    // float((Dy*Dy + Dz*Dz) + Dx*Dx) (FSTP 0635d772); Proj = ((Dy*Py + Dz*Pz)
    // + Dx*Px) * 1.2f extended; |Proj|*Proj < Q -> Proj, else sqrt(float Q).
    inline double Evaluate_0635d720(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x24), &PositionB);

        const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
        const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
        const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
        const float QuadLength = (float)((DeltaY * DeltaY + DeltaZ * DeltaZ) + DeltaX * DeltaX);

        const IVP_U_Point *Direction = EvaluatorPoint(EvaluatorThis, 0x34);
        const double Projected = ((DeltaY * Direction->k[1] + DeltaZ * Direction->k[2]) + DeltaX * Direction->k[0]) *
                                 (double)(1.2f); // DAT_063a5668
        if (std::fabs(Projected) * Projected < QuadLength)
        {
            return Projected;
        }
        return std::sqrt((double)(QuadLength));
    }

    // Common.dll.txt:449073-449113, bytes 0x0635d7b0-0x0635d81e (item 3365,
    // 40 insns).
    //
    //   PositionA = FrameA->vmult4(P14)
    //   PositionB = FrameB->vmult4(P24)
    //   Normal    = FrameB->vmult (P34)
    //   return dot(PositionB - PositionA, Normal)
    //
    // Same quantity as Evaluate_0635d5b0 with the two frames' roles
    // exchanged (there the plane is B's and the point is A's and the
    // difference runs A-minus-B; here the difference runs B-minus-A and the
    // plane normal is still B's). The two bodies agreeing to that degree,
    // derived independently, is the main cross-check on both.
    // cmn-ivp3f 2026-10-09, x87-exact: D = B - A extended, result
    // (Dz*Nz + Dy*Ny) + Dx*Nx unrounded.
    inline double Evaluate_0635d7b0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        IVP_U_Point Normal;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x24), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x34), &Normal);

        const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
        const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
        const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
        return (DeltaZ * Normal.k[2] + DeltaY * Normal.k[1]) + DeltaX * Normal.k[0];
    }

    // Common.dll.txt:449129-449218, bytes 0x0635d830-0x0635d941 (item 3366,
    // 89 insns).
    //
    //   PositionA = FrameA->vmult4(P14)
    //   DirectionA= FrameA->vmult (P24)
    //   PositionB = FrameB->vmult4(P34)
    //   DirectionB= FrameB->vmult (P44)
    //   Delta     = PositionA - PositionB
    //   Perp      = normize(cross(cross(DirectionB, Delta), DirectionB))
    //   return dot(DirectionA, Perp)
    //
    // The doubled cross product is the standard "component of Delta
    // perpendicular to DirectionB"; normalizing it yields the unit direction
    // from B's edge towards A's, and the result is A's own edge direction
    // projected onto it. Both crosses are INLINE in retail (:0635d897-
    // :0635d917), not calls, and their operand order was taken term by term
    // rather than assumed: the first is cross(DirectionB, Delta) --
    // :0635d899 computes dz*B2.y - dy*B2.z, which is +(DirectionB x Delta).x,
    // not (Delta x DirectionB).x.
    //
    // The normalize IS a call, FUN_06347ef0 at :0635d91b, with ECX loaded
    // eight instructions earlier at :0635d8ff (`LEA ECX,[ESP+0x18]`) and
    // untouched in between. It normalizes in place and returns 0/1 in EAX;
    // retail ignores the return, so a below-epsilon Perp is left unchanged
    // rather than zeroed -- ivp_math.h's normize() has that same behaviour,
    // so it is used directly instead of being reimplemented.
    //
    // Field offsets 0x14 / 0x24 / 0x34 / 0x44: four IVP_U_Points. Note ESI is
    // advanced by 0x44 at :0635d874 and is never used as `this` again.
    // cmn-ivp3f 2026-10-09, x87-exact (.text:0635d87f-0635d93c):
    //   D = PosA - PosB extended (B = DirectionB);
    //   C.x = float(Dz*By - Bz*Dy), C.y = float(Bz*Dx - Dz*Bx), C.z = Dy*Bx - By*Dx extended;
    //   T1 = float(By*C.x), T2 = float(C.y*Bx);
    //   Perp.x = float(C.y*Bz - C.z*By), Perp.y = float(C.z*Bx - Bz*C.x),
    //   Perp.z = float(T1 - T2); FUN_06347ef0 normize;
    //   result (A.z*Perp.z + A.y*Perp.y) + Perp.x*A.x unrounded (A = DirectionA).
    inline double Evaluate_0635d830(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point DirectionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x34), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x44), &DirectionB);

        const double Dx = (double)(PositionA.k[0]) - PositionB.k[0];
        const double Dy = (double)(PositionA.k[1]) - PositionB.k[1];
        const double Dz = (double)(PositionA.k[2]) - PositionB.k[2];
        const double Bx = DirectionB.k[0];
        const double By = DirectionB.k[1];
        const double Bz = DirectionB.k[2];

        const float Cx = (float)(Dz * By - Bz * Dy);
        const float Cy = (float)(Bz * Dx - Dz * Bx);
        const double Cz = Dy * Bx - By * Dx;

        const float T1 = (float)(By * Cx);
        const float T2 = (float)((double)(Cy) * Bx);

        IVP_U_Point Perpendicular;
        Perpendicular.k[0] = (float)((double)(Cy) * Bz - Cz * By);
        Perpendicular.k[1] = (float)(Cz * Bx - Bz * Cx);
        Perpendicular.k[2] = (float)((double)(T1) - T2);
        Perpendicular.normize(); // 0635d91b

        return ((double)(DirectionA.k[2]) * Perpendicular.k[2] +
                (double)(DirectionA.k[1]) * Perpendicular.k[1]) +
               (double)(Perpendicular.k[0]) * DirectionA.k[0];
    }

    // Common.dll.txt:449231-449298, bytes 0x0635d950-0x0635da14 (item 3367,
    // 66 insns).
    //
    //   PositionA  = FrameA->vmult4(P18)
    //   PositionB  = FrameB->vmult4(P28)
    //   DirectionB = FrameB->vmult (P38)
    //   MeasureB   = FrameB->vmult (P48)
    //   Delta      = PositionB - PositionA
    //   Moment     = cross(Delta, DirectionB)       // CALL FUN_0628bed0
    //   Limit      = Moment.real_length()           // CALL FUN_06347e60
    //   Value      = dot(Moment, MeasureB) + Float14
    //   return (Value < Limit) ? Value : Limit;
    //
    // THE ODD ONE OUT of the nine: its points start at +0x18, not +0x14,
    // because +0x14 is a float bias added to the result (:0635d9fa
    // `FADD [ESI+0x14]`). Any attempt to give the nine a common base class
    // has to account for that.
    //
    // Argument order of the cross product was proved, not assumed. Retail
    // pushes &DirectionB first (:0635d9a9) and &Delta second (:0635d9b6), so
    // &Delta is the one sitting at [ESP+0] at the CALL, which makes it
    // FUN_0628bed0's param_1; and FUN_0628bed0 (Common.dll.txt:83869-83891,
    // read in full this pass) computes ECX = param_1 x param_2. Hence
    // cross(Delta, DirectionB) and not the reverse.
    //
    // Branch polarity, mask and jump together: :0635da02 FCOMP ST(1);
    // FNSTSW AX; TEST AH,0x1 (C0); JZ -> the jump is taken when C0 is CLEAR,
    // i.e. NOT (Value < Limit), and the jumped-to path returns Limit (which
    // FCOMP left in ST0). The fall-through, taken when Value < Limit, pops
    // Limit and reloads Value. So it is a minimum, with Value winning ties.
    inline double Evaluate_0635d950(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        IVP_U_Point MeasureB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x18), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x28), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x38), &DirectionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x48), &MeasureB);

        IVP_U_Point Delta;
        Delta.subtract(&PositionB, &PositionA);

        // cmn-ivp3e 2026-10-08: x87 precision. FUN_0628bed0 is
        // IVP_CalcCrossProduct (products extended, each component rounded at
        // its FSTP); FUN_06347e60's length stays unrounded in ST1 (Limit);
        // Value = ((M.z*Me.z + M.y*Me.y) + Me.x*M.x) + bias stays extended for
        // the `FCOMP ST(1)` and only its FST copy (:0635d9fe) is returned.
        IVP_U_Point Moment;
        IVP_CalcCrossProduct(&Moment, &Delta, &DirectionB);

        const double Limit = Moment.real_length();
        const double Value = (((double)(Moment.k[2]) * MeasureB.k[2] +
                               (double)(Moment.k[1]) * MeasureB.k[1]) +
                              (double)(MeasureB.k[0]) * Moment.k[0]) +
                             EvaluatorFloat(EvaluatorThis, 0x14);
        if (Value < Limit)
        {
            return (float)(Value); // 0635d9fe FST dword, reloaded at 0635da0d
        }
        return Limit; // left unrounded in ST0
    }

    // Common.dll.txt:449919-449991, bytes 0x0635e080-0x0635e154 (item 3372,
    // 72 insns).
    //
    //   PositionA  = FrameA->vmult4(P14)
    //   DirectionA = FrameA->vmult (P24)
    //   PositionB  = FrameB->vmult4(P34)
    //   DirectionB = FrameB->vmult (P44)
    //   Axis       = cross(DirectionA, DirectionB)    // CALL FUN_0628bed0
    //   return (dot(Axis, PositionA) - dot(Axis, PositionB))
    //              * (1.0f / sqrtf(Axis.quad_length()))
    //              * Float54;
    //
    // The canonical edge-edge separation: the signed distance between two
    // skew lines, measured along their common perpendicular, times a stored
    // scale at +0x54. Retail reaches the reciprocal square root through
    // FUN_06347df0, whose whole body is FLD/FSQRT/FDIVR DAT_063a53d0, and
    // DAT_063a53d0 is raw bytes 00 00 80 3f == 1.0f -- so it is exactly
    // 1.0f/sqrtf(x) with no rsqrt approximation to reproduce.
    //
    // Cross-product argument order: retail pushes &DirectionB first
    // (:0635e0cf) then &DirectionA (:0635e0d8), so DirectionA is param_1 and
    // the product is cross(DirectionA, DirectionB).
    //
    // Retail computes the two dot products against the SAME Axis and
    // subtracts them (:0635e11b FSUBP) rather than dotting Axis with the
    // difference. Kept that way: with floats the two are not identical.
    // cmn-ivp3f 2026-10-09, x87-exact (.text:0635e0cf-0635e14d): Axis =
    // FUN_0628bed0 = IVP_CalcCrossProduct(DirectionA, DirectionB);
    // d1 = (Ax.z*PA.z + Ax.y*PA.y) + PA.x*Ax.x, d2 likewise with PB;
    // Num = float(d1 - d2); Q = float((Ax.y^2 + Ax.z^2) + Ax.x^2);
    // result (FUN_06347df0(Q) * Num) * Float54 unrounded.
    inline double Evaluate_0635e080(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point DirectionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionA);   // 0635e096
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);   // 0635e0a6
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x44), &DirectionB);   // 0635e0ba
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x34), &PositionB);   // 0635e0ca

        IVP_U_Point Axis;
        IVP_CalcCrossProduct(&Axis, &DirectionA, &DirectionB);              // 0635e0dd

        const double DotA = ((double)(Axis.k[2]) * PositionA.k[2] +
                             (double)(Axis.k[1]) * PositionA.k[1]) +
                            (double)(PositionA.k[0]) * Axis.k[0];
        const double DotB = ((double)(Axis.k[2]) * PositionB.k[2] +
                             (double)(Axis.k[1]) * PositionB.k[1]) +
                            (double)(PositionB.k[0]) * Axis.k[0];
        const float Numerator = (float)(DotA - DotB);           // 0635e11d
        const float QuadLength = (float)(
            ((double)(Axis.k[1]) * Axis.k[1] + (double)(Axis.k[2]) * Axis.k[2]) +
            (double)(Axis.k[0]) * Axis.k[0]);                    // 0635e13d
        return (IVP_InvSqrtFloat(QuadLength) * Numerator) * EvaluatorFloat(EvaluatorThis, 0x54);
    }

    // Common.dll.txt:450001-450037, bytes 0x0635e160-0x0635e1c1 (item 3373,
    // 36 insns).
    //
    //   DirectionA = FrameA->vmult(P14)
    //   DirectionB = FrameB->vmult(P24)
    //   return cross(DirectionA, DirectionB).quad_length();
    //
    // The degeneracy guard for Evaluate_0635e080 above: it is precisely the
    // quantity whose reciprocal square root that evaluator divides by, so a
    // driver installing both is asking "are these two edges parallel enough
    // that the edge-edge distance is meaningless". Note NEITHER transform is
    // vmult4 -- both are rotation-only, which is correct for directions and
    // is also why this is the only one of the nine that never calls
    // FUN_063484e0.
    //
    // Cross-product argument order: &DirectionB pushed first (:0635e18e),
    // &DirectionA second (:0635e193), so DirectionA is param_1.
    // cmn-ivp3f 2026-10-09, x87-exact: Axis = FUN_0628bed0 =
    // IVP_CalcCrossProduct(DirectionA, DirectionB); result
    // (z*z + y*y) + x*x unrounded.
    inline double Evaluate_0635e160(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point DirectionA;
        IVP_U_Point DirectionB;
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x14), &DirectionA);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionB);

        IVP_U_Point Axis;
        IVP_CalcCrossProduct(&Axis, &DirectionA, &DirectionB);
        return ((double)(Axis.k[2]) * Axis.k[2] + (double)(Axis.k[1]) * Axis.k[1]) +
               (double)(Axis.k[0]) * Axis.k[0];
    }
}

// ============================================================================
// The driver layer (decomp-tracker items 3369 / 3371 / 3374 / 3378 and their
// four `simplified` siblings 3375 / 3376 / 3377 / 3379).
//
// [2026-09-17, lane claude:w22-csys-drivers] The two argument shapes below and
// the receiver were derived from the ONE dispatcher that reaches item 3371,
// LAB_0635fd10 (Common.dll.txt:452286-452385, read in full), not guessed from
// the driver bodies alone.
// ============================================================================

// ----------------------------------------------------------------------------
// FIVP_Sweep_Ledge_Side -- PORT-LOCAL NAME. The 0x10-byte "resolved side"
// record the dispatcher builds on its own frame and passes to the driver by
// address (`LEA EAX,[ESP+0x18]; PUSH EAX`, :0635fdce-:0635fdd2).
//
// Every field is written in LAB_0635fd10 and read in FUN_0635df00, so both
// ends agree:
//   +0x00 Points -- `MOV EDX,[EDI]; ADD EDX,EDI; MOV [ESP+0x18],EDX`
//                   (:0635fda0-:0635fda4) with EDI == the ledge, i.e. this is
//                   `Ledge + Ledge->CPointOffset` ALREADY RESOLVED to an
//                   absolute pointer. That is why the driver's own vertex
//                   fetch is a bare `index*0x10 + Points` with no second add
//                   (:0635df79-:0635df81).
//   +0x04 Ledge  -- `[ESP+0x1c] = EDI` (:0635fd8b). EDI is the containing
//                   compact ledge, recovered from the feature edge by
//                   `EAX = Edge & ~0xf; ECX = (*EAX & 0xfff) + 1; EAX -= ECX*16`
//                   (:0635fd74-:0635fd87) -- exactly
//                   IVP_Compact_Triangle::GetCompactLedge(), since triangle n
//                   sits at Ledge+0x10+n*0x10.
//   +0x08 Source -- `[ESP+0x20] = EAX` (:0635fd98), the return value of
//                   FUN_06353510(this = ObjectB). This is what the driver
//                   feeds to IVP_Sweep_Frame_Cache::Init, and Init's own use
//                   of +0x08/+0x1c is what types it as
//                   FIVP_Sweep_Cache_Source.
//   +0x0c Object -- `[ESP+0x24] = EBP` (:0635fd9c), Mindist->ObjectB. Never
//                   read by item 3371; kept because the dispatcher writes it
//                   and a larger sibling driver may.
// ----------------------------------------------------------------------------
struct FIVP_Sweep_Ledge_Side
{
    const void *Points;                 // +0x00 -- IVP_U_Float_Point[], 0x10 stride
    const void *Ledge;                  // +0x04 -- IVP_Compact_Ledge *
    FIVP_Sweep_Cache_Source *Source;    // +0x08
    IVP_Real_Object *Object;            // +0x0c
};
static_assert(offsetof(FIVP_Sweep_Ledge_Side, Source) == 0x08, "FIVP_Sweep_Ledge_Side::Source");
static_assert(offsetof(FIVP_Sweep_Ledge_Side, Object) == 0x0c, "FIVP_Sweep_Ledge_Side::Object");

// ----------------------------------------------------------------------------
// IVP_Sweep_Estimator_Context -- the driver's `this`.
//
// RECEIVER PROOF, not an offset resemblance: in LAB_0635fd10, ECX is loaded
// once at :0635fd94 (`MOV ECX,[ESP+0x30]`, the handler's own single stack
// argument), used at :0635fda8 to zero +0x28, and is NOT written again before
// the `CALL FUN_0635df00` at :0635fdd9 -- the intervening instructions touch
// only EAX/EDX/EBX/EBP/ESI/EDI. So the driver's ECX is the handler's argument.
//
// That argument is retail's 0x34-byte collision-time-estimator context,
// built by FUN_0634f320 at BASE+0x20 and dispatched through
// `CALL [idx*4 + DAT_0658af9c]`; FUN_0634f320 constructs this very struct.
// Every offset item 3371 touches:
//     +0x08 ClosingBound  -> the evaluator's MaxRelativeSpeed (:0635dfd8)
//     +0x10 Mindist       -> :0635dfe5, then +0x14/+0x50/+0x54 through it
//     +0x18/+0x1c Now     -> the interval start (:0635e043/:0635e03f)
//     +0x20/+0x24 Next    -> the interval end   (:0635e039/:0635e031)
//     +0x28 OutCode       -> `MOV [ESI+0x28],0x20` on a hit (:0635e061)
//     +0x2c/+0x30 OutEvent-> `LEA EDX,[ESI+0x2c]` passed as OutTime (:0635dfec)
// ----------------------------------------------------------------------------
struct IVP_Sweep_Estimator_Context
{
    float RadiusSum;            // +0x00
    float RelativeSpeed;        // +0x04
    float ClosingBound;         // +0x08
    float MarginSpan;           // +0x0c
    void *Mindist;              // +0x10 -- IVP_Ledge_Mindist *
    void *Env;                  // +0x14 -- IVP_Environment *
    float NowBase;              // +0x18
    float NowOffset;            // +0x1c
    float NextBase;             // +0x20
    float NextOffset;           // +0x24
    int32_t OutCode;            // +0x28
    float OutEventBase;         // +0x2c
    float OutEventOffset;       // +0x30

    // FUN_0635df00 (Common.dll.txt:449772-449907, bytes 0x0635df00-0x0635e072,
    // 109 insns; decomp-tracker item 3371). thiscall, three stack arguments,
    // `RET 0xc`. Reached only from LAB_0635fd10's ShapeTypeB == 2 arm.
    void EstimateBallVsLedgeFaceContact(const void *FeatureEdge,
                                        FIVP_Sweep_Cache_Source *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635da70 (Common.dll.txt:449341-449699, bytes 0x0635da70-0x0635de83,
    // 299 insns; decomp-tracker item 3369). thiscall, four stack arguments,
    // `RET 0x10`. Reached from LAB_0635fef0 (:0636002a).
    void EstimateLedgeVertexVsLedgeFaceContact(const void *FeatureEdgeA,
                                               const void *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635e1d0 (Common.dll.txt:450051-450495, bytes 0x0635e1d0 through the
    // `RET 0x10` at 0x0635e77e, 361 insns; decomp-tracker item 3374). thiscall,
    // four stack arguments, `RET 0x10`. Its one XREF is :0635fff4, i.e. the
    // dispatcher LAB_0635fef0, the same one that reaches item 3369.
    //
    // The argument shape is item 3369's exactly, proved the same way: ESI is
    // loaded from the param_3 slot at :0635e1d9 and EDI from the param_4 slot
    // at :0635e214, and both are immediately dereferenced at +0x08 to feed
    // IVP_Sweep_Frame_Cache::Init, which is what types them as
    // FIVP_Sweep_Ledge_Side. param_1 and param_2 are read at :0635e289 and
    // :0635e2ce and are dereferenced as raw tagged compact edges.
    void EstimateLedgeEdgeVsLedgeEdgeContact(const void *FeatureEdgeA,
                                             const void *FeatureEdgeB,
                                             FIVP_Sweep_Ledge_Side *SideA,
                                             FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f180 (Common.dll.txt:451361-451925, bytes 0x0635f180 through the
    // `RET 0x10` at 0x0635f8bb, 494 insns; decomp-tracker item 3378 -- the
    // largest member of the family). thiscall, four stack arguments,
    // `RET 0x10`. Its one XREF is :06360045, again dispatcher LAB_0635fef0.
    //
    // RECEIVER PROOF, from this body rather than borrowed: ECX is copied to ESI
    // at :0635f1a5 before anything else touches it, and ESI is thereafter read
    // at +0x08, +0x0c, +0x10, +0x18, +0x1c, +0x20, +0x24 and written at +0x28,
    // +0x2c, +0x30 -- the IVP_Sweep_Estimator_Context field set exactly, with
    // +0x0c (MarginSpan) additionally exercised here, which items 3369, 3371
    // and 3374 never touch.
    void EstimateLedgeVertexVsLedgeEdgeContact(const void *FeatureEdgeA,
                                               const void *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f020 (Common.dll.txt:451231-451353, bytes 0x0635f020 through the
    // `RET 0x8` at 0x0635f177, 96 insns; decomp-tracker item 3377 -- the
    // smallest of the family). thiscall, TWO stack arguments, `RET 0x8`.
    //
    // Unlike the four ledge drivers it takes the two cache sources DIRECTLY
    // rather than through FIVP_Sweep_Ledge_Side: :0635f045 and :0635f072 read
    // the parameter slots and hand them straight to IVP_Sweep_Frame_Cache::Init
    // (:0635f054, :0635f07e), and the only other thing done with them is
    // reading +0x4c/+0x50/+0x54, which is Source->Frame's translation
    // (Frame at +0x1c, IVP_U_Matrix::vv at +0x30). No ledge, no edge, no
    // point array is touched anywhere in the body.
    //
    // Its one XREF is :0635fed5, inside FUN_0635fe30 (item 3381) -- a THIRD
    // dispatcher, distinct from LAB_0635fd10 (which reaches item 3371) and
    // LAB_0635fef0 (which reaches items 3369, 3374 and 3378).
    void EstimateBallVsBallContact(FIVP_Sweep_Cache_Source *SourceA,
                                   FIVP_Sweep_Cache_Source *SourceB);

    // FUN_0635e790 (Common.dll.txt:450564-450914, bytes 0x0635e790 through the
    // `RET 0x10` at 0x0635e880 inclusive, 340 insns; decomp-tracker item 3375).
    // thiscall, four stack arguments, `RET 0x10`. Its one XREF is :06360060,
    // i.e. dispatcher LAB_0635fef0's (ShapeTypeA, ShapeTypeB) == (0, 0) arm --
    // which is what names it: shape tag 0 is a ledge VERTEX on both sides.
    void EstimateLedgeVertexVsLedgeVertexContact(const void *FeatureEdgeA,
                                                 const void *FeatureEdgeB,
                                                 FIVP_Sweep_Ledge_Side *SideA,
                                                 FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635ec90 (Common.dll.txt:450963-451226, bytes 0x0635ec90 through the
    // `RET 0x10` at 0x0635f01a inclusive, 259 insns; decomp-tracker item 3376).
    // thiscall, four stack arguments, `RET 0x10`. Its one XREF is :0635fe07,
    // dispatcher LAB_0635fd10's ShapeTypeB == 0 arm.
    //
    // NOTE THE ARGUMENT SHAPE, which is neither of the two already in this
    // file: param_1 is a bare IVP_Real_Object* (read only at +0x98, :0635ecf0-
    // :0635ecf7), param_3 is a cache source passed DIRECTLY (:0635ecb2, the
    // ball side), and only param_4 is a FIVP_Sweep_Ledge_Side. Item 3379 takes
    // the identical four, and the dispatcher pushes the same four for both
    // (:0635fdef-:0635fe07 and :0635fde8-:0635fdf0).
    void EstimateBallVsLedgeVertexContact(IVP_Real_Object *ObjectA,
                                          const void *FeatureEdgeB,
                                          FIVP_Sweep_Cache_Source *SourceA,
                                          FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f8c0 (Common.dll.txt:451980-452276, bytes 0x0635f8c0 through the
    // `RET 0x10` at 0x0635fd04 inclusive, 293 insns; decomp-tracker item 3379).
    // thiscall, four stack arguments, `RET 0x10`. Its one XREF is :0635fdf0,
    // dispatcher LAB_0635fd10's ShapeTypeB == 1 arm (a ledge EDGE).
    //
    // ObjectA IS GENUINELY UNUSED here, and that is measured, not assumed:
    // Ghidra's own frame analysis leaves param_1 `<UNASSIGNED>` (no stack
    // location could be attached to it) and the function's local list has no
    // entry at frame+0xc1c. The body reads Stack[0x8], Stack[0xc] and
    // Stack[0x10] only. It still takes four arguments and still returns
    // `RET 0x10`, because the dispatcher pushes the same four for this arm as
    // for item 3376's -- so the parameter is kept rather than dropped.
    void EstimateBallVsLedgeEdgeContact(IVP_Real_Object *ObjectA,
                                        const void *FeatureEdgeB,
                                        FIVP_Sweep_Cache_Source *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);
};
static_assert(sizeof(IVP_Sweep_Estimator_Context) == 0x34, "IVP_Sweep_Estimator_Context size");
static_assert(offsetof(IVP_Sweep_Estimator_Context, ClosingBound) == 0x08, "IVP_Sweep_Estimator_Context::ClosingBound");
static_assert(offsetof(IVP_Sweep_Estimator_Context, Mindist) == 0x10, "IVP_Sweep_Estimator_Context::Mindist");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NowBase) == 0x18, "IVP_Sweep_Estimator_Context::NowBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NextBase) == 0x20, "IVP_Sweep_Estimator_Context::NextBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutCode) == 0x28, "IVP_Sweep_Estimator_Context::OutCode");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutEventBase) == 0x2c, "IVP_Sweep_Estimator_Context::OutEventBase");

// ============================================================================
// The dispatch layer (decomp-tracker items 3380 / 3381 / 3382 / 3383, plus the
// table initializer item 3384) -- ported 2026-09-17 by lane
// claude:w24-sweep-rest.
//
// This is what sits between IVP_RescheduleCollisionEvent (item 3117,
// ivp_mindist_limiter.cpp) and the eight drivers above it. Item 3117 builds
// the 0x34-byte context, computes `Index = ShapeType[j] + ShapeType[i] * 4`
// and does `CALL dword ptr [Index*4 + DAT_0658af9c]` (:0634f5b0-:0634f5ea).
//
// All four handlers are __cdecl with ONE stack argument and a plain `RET` (no
// callee cleanup) -- item 3117 does its own `ADD ESP,4` equivalent. They are
// NOT thiscalls, despite dispatching into thiscall drivers: each loads ECX
// from its own stack argument first.
//
// THE TABLE, read out of FUN_06360090 (Common.dll.txt:452636-452661):
//     MOV EAX,LAB_06360080; MOV ECX,0x10; MOV EDI,DAT_0658af9c; REP STOSD
// fills all sixteen slots with the trap, and then eleven `MOV [slot],reg`
// stores follow, EIGHT OF WHICH RE-STORE THE TRAP THAT IS ALREADY THERE
// (:063600a7, :063600ac, :063600b1, :063600b6, :063600bb, :063600c0,
// :063600c5, :063600ca). Those eight are reproduced anyway -- they are what a
// literal source-level array initializer compiles to, and leaving them out
// would hide that the array is written exhaustively.
//     slots 0, 1, 2, 5    -> LAB_0635fef0   (ledge-vs-ledge)
//     slots 12, 13, 14    -> LAB_0635fd10   (ball-vs-ledge)
//     slot 15             -> FUN_0635fe30   (ball-vs-ball)
//     the other eight     -> LAB_06360080   (the trap)
// With Index = ShapeB + ShapeA*4 that reads: ShapeA in {0,1} (ledge vertex or
// ledge edge) paired with ShapeB in {0,1,2} goes to the ledge-vs-ledge
// handler; ShapeA == 3 (ball) with ShapeB in {0,1,2} goes to ball-vs-ledge;
// (3,3) goes to ball-vs-ball. Slots 3, 7, 11 are "ShapeB == 3 with a non-ball
// A", which retail never generates because it normalizes the ball onto side A.
//
// ivp_environment.cpp already declares IVP_InitSimUnitDispatchTable() for
// FUN_06360090 and filled a local array of nullptr with the retail targets in
// comments. It now forwards to IVP_InitSweepEstimatorTable() below instead, so
// there is one table rather than two.
//
// That placeholder sized the array at seventeen, on the reading that the last
// store at :063600fb writes "one past" the sixteen the REP STOSD covers. That
// is wrong: 0x0658afd8 - 0x0658af9c = 0x3c = 15 * 4, so it is slot 15, the
// last of the sixteen. The table is sixteen dwords, 0658af9c..0658afd8.
// ============================================================================

typedef void (*FIVP_Sweep_Estimator_Handler)(IVP_Sweep_Estimator_Context *Ctx);

// LAB_0635fd10 (Common.dll.txt:452287-452389, bytes 0x0635fd10 through the
// `RET` at 0x0635fe25, 97 insns; decomp-tracker item 3380).
void IVP_SweepEstimate_BallVsLedge(IVP_Sweep_Estimator_Context *Ctx);

// FUN_0635fe30 (Common.dll.txt:452406-452469, bytes 0x0635fe30 through the
// `RET` at 0x0635fee3, 60 insns; decomp-tracker item 3381).
void IVP_SweepEstimate_BallVsBall(IVP_Sweep_Estimator_Context *Ctx);

// LAB_0635fef0 (Common.dll.txt:452483-452621, bytes 0x0635fef0 through the
// `RET` at 0x0636007e, 131 insns; decomp-tracker item 3382).
void IVP_SweepEstimate_LedgeVsLedge(IVP_Sweep_Estimator_Context *Ctx);

// LAB_06360080 (Common.dll.txt:452624-452625, bytes 0x06360080-0x0636008a,
// 2 insns; decomp-tracker item 3383). Retail's body is
// `MOV dword ptr [0x00000000],0x0; RET` -- a deliberate null store, i.e. an
// assert that this shape-pair index can never be dispatched.
void IVP_SweepEstimate_Unreachable(IVP_Sweep_Estimator_Context *Ctx);

// FUN_06360090 (decomp-tracker item 3384). Fills the 16-slot table.
void IVP_InitSweepEstimatorTable();

// `CALL dword ptr [Index*4 + DAT_0658af9c]`, item 3117 at :0634f5ea.
void IVP_InvokeSweepEstimator(int TableIndex, IVP_Sweep_Estimator_Context *Ctx);
