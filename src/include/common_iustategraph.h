// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_aisteer.h"        // FAiTarget (the 0x88-byte AI-target cache record)
#include "common_iface.h"          // IObjInspect
#include "common_systemmanager.h"  // SystemManagerTimerTable / FAiTargetVector -- this
                                   // cluster's container IS that struct (see below).
                                   // common_systemmanager.h stays independent of THIS
                                   // header, so the dependency runs one way only.

#include <cstddef>

// ============================================================================
// The AI "hostile candidate" target list -- address range 0x062e7580-0x062e8400
// plus its vector mechanics at 0x062ed0d0-0x062ed460 (Common.dll.c / .txt).
//
// HISTORY, AND WHAT WAS WRONG (2026-09-17, claude:common-iustategraph, items
// 1800/1802/1805/1884). Everything in this file used to sit behind a
// "FULLPORT-DEFERRED[PUB] Wave-4 / IUStateGraph" disposition resting on three
// claims. All three are now disproved from the raw listing, so the narrative
// is replaced rather than annotated:
//
//  1. WRONG: "the vector's OWNING object is still unidentified... two
//     DIFFERENT owning objects at the same +0x34 sub-offset". There is one
//     owner. Every single one of FUN_062e7580's 8 call sites resolves to
//     `SystemManager* + 0x34`, and the SystemManager pointer is reached
//     through whatever field the particular caller keeps it in:
//       0x062d01be  `MOV ECX,[EBX+0x24]; ADD ECX,0x34`   (LargeShipBehavior)
//       0x062e782a  `MOV ECX,[EBX+0x04]; ADD ECX,0x34`   (item 1802, EBX is
//                   itself SystemManager+0x34, so +4 is its own Owner field)
//       0x062e82b8  `MOV ECX,EBX`                        (item 1805 IS a
//                   method of this same class -- no adjustment at all)
//       0x062e9d7a / 0x062eda09 / 0x062ee3xx  `MOV ECX,[reg+4]; ADD ECX,0x34`
//     and, decisively, the exported thunk
//     `IBehaviorManager::get_closest_trailing_ship` (0x062d4a30, item 12392)
//     is literally `MOV ECX,[ECX+8]; ADD ECX,0x34; JMP FUN_062e7820` --
//     IBehaviorManager+0x08 is SystemManager (common_iface.h documents that
//     field already). The "NavHelperPtr24" of the LargeShipBehavior note is a
//     SystemManager* too.
//  2. WRONG: "FUN_062e7580's body reads a value through [ESP+0x10] ... that
//     hand-tracing resolves to a LOCAL variable (Ghidra's own local_9c,
//     written to 0 at Common.dll.txt:243903) ... reading [0+0x800] would be a
//     null-pointer dereference". local_9c is not a plain local: it is an
//     OUT-PARAMETER. At 0x062e766e retail does `LEA EAX,[ESP+0x18]; PUSH EAX;
//     CALL [EDX+0x104]`, and with the two pushes already on the stack at that
//     point `[ESP+0x18]` IS local_9c. Slot +0x104 is
//     `IObjInspect::get_scanlist(ScanList const*& OutList, uint Type, bool
//     bFlag)` (common_iface.h slot 65), so local_9c comes back holding a
//     `ScanList const*` -- and `[ECX+0x800]` is ScanList::Count, which
//     common_iface.h already asserts at exactly +0x800. Nothing is hidden and
//     no WinDbg session was needed; Ghidra's XREF list simply does not
//     attribute writes made through a LEA'd address (the project's own
//     "QI out-param setter" lesson, applied to a non-QI getter).
//  3. WRONG: "the compaction block shifts stale entries via a block-move CALL
//     that Ghidra resolves to JustifyNode::Execute (0x0628f050) ... treat as a
//     generic memmove-shaped helper". It is an ICF fold, yes, but not with a
//     memmove: the `rep movsd` loop immediately above it is already the move,
//     and its own source pointer is initialised from `End` rather than
//     `Begin` (`MOV EAX,EDX` at 0x062e75a0, then `CMP EAX,EDX` -- always
//     equal, so the loop never runs). What 0x0628f050 is, is the
//     compiler-generated destroy-a-range helper of an inlined
//     `erase(begin(), end())`; for a trivially-destructible element type it
//     does nothing. Ported below as DestroyAiTargetRange so the call shape
//     survives without pretending a copy happens.
//
// WHAT THE CONTAINER ACTUALLY IS. `FAiTargetList` below is the object at
// SystemManager+0x34, and the list itself is a textbook MSVC-7
// `std::vector<FAiTarget>` embedded at its +0x0C (empty allocator member at
// +0x00 of the vector, then _First/_Last/_End). FUN_062e7580 returns a
// pointer TO THAT VECTOR (`LEA EAX,[ESI+0xc]` at 0x062e77cb), which is why
// every caller immediately reads `[EAX+4]` / `[EAX+8]` as its begin/end pair.
// It is not a begin/end-returning accessor.
//
// CROSS-FILE MERGE, DONE (2026-09-17, claude:common-crossfile-harvest -- the
// note that used to sit here recorded this as an unacted-on implication because
// common_systemmanager.h belonged to another lane at the time). SystemManager
// +0x34 is `SystemManagerTimerTable`, and FAiTargetList is not a second view of
// those bytes: it IS that struct. Its +0x00..+0x1F used to be the blind blobs
// `Unknown00[4]` / `Owner` / `Unknown08[0x18]`; those are now the real fields,
// pinned by static_assert in common_systemmanager.h, and FAiTargetList is a
// plain alias of the table (below). The clinching evidence is that table's own
// constructor FUN_062e6af0, which default-constructs an empty
// std::vector<FAiTarget> at +0x0C (.text:062e6b16-062e6b1f writes the 1-byte
// allocator then three null pointers) and clears bKeepCache at +0x1D; the full
// derivation lives on SystemManagerTimerTable's MERGE NOTE.
// ============================================================================

// sizeof(FAiTarget) == 0x88. Kept as a named constant because retail's own
// pointer arithmetic is written in raw bytes throughout this cluster.
inline constexpr std::size_t kElemStride = 0x88;

// ----------------------------------------------------------------------------
// The object at SystemManager+0x34. FAiTargetVector and every field this
// cluster uses (Owner, ScannerEquipId, Targets, bBuilt, bKeepCache) now live on
// SystemManagerTimerTable itself in common_systemmanager.h, with their offsets
// pinned there by static_assert. This alias exists so the cluster's own code
// and comments can keep calling the object by the role it plays here.
// ----------------------------------------------------------------------------
using FAiTargetList = SystemManagerTimerTable;

// The SystemManager+0x34 accessor every call site spells out by hand.
FAiTargetList* GetAiTargetList(SystemManager* Sys);

// ----------------------------------------------------------------------------
// Vector mechanics
// ----------------------------------------------------------------------------

// Common.dll.c 0x062ed0d0 (FUN_062ed0d0), item 1882. thiscall(this=the vector).
// `(End - Begin) / 0x88`, i.e. size(), with retail's null-Begin early-out.
// The IMUL-by-0x78787879/SAR-6/sign-correct sequence is the standard MSVC
// signed-divide-by-0x88 reciprocal idiom.
unsigned ElemVectorSize(void const* VectorHeader);

// Common.dll.c 0x062ed100 (FUN_062ed100), item 1883. __stdcall, 3 stack args
// (RET 0xc). Element-wise `rep movsd` copy of [SrcBegin, SrcEnd) into
// DestBegin at the 0x88 stride.
void CopyElemRange(void const* SrcBegin, void const* SrcEnd, void* DestBegin);

// The destroy-a-range helper at 0x0628f050 that Ghidra mislabels
// `JustifyNode::Execute` (ICF fold). thiscall(this=the vector) plus (First,
// Last). FAiTarget is trivially destructible, so retail's instance is an
// empty loop; kept as a real function so both call sites (the inlined
// erase(begin(),end()) and the post-reallocation cleanup) stay visible.
void DestroyAiTargetRange(FAiTargetVector* Self, FAiTarget* First, FAiTarget* Last);

// The inlined `erase(begin(), end())` at the head of FUN_062e7580
// (0x062e7597-0x062e75d0): a `rep movsd` shift loop whose source is seeded
// from End instead of Begin and therefore never runs, then
// DestroyAiTargetRange, then `End = Begin`.
void ClearAiTargetVector(FAiTargetVector* Self);

// Common.dll.c 0x062e77e0 (FUN_062e77e0), item 1801. __thiscall(this = the list), no
// arguments. `bBuilt = 0; Targets.End = Targets.Begin` -- the same inlined, never-iterating
// erase(begin(), end()) shape, but without ClearAiTargetVector's destroy-range call. Sole
// caller TargetManager::GetScanResponse (FUN_062eeb70, .text:062eeb89).
void ResetAiTargetList(FAiTargetList* Self);

// Common.dll.c 0x062ed140 (FUN_062ed140), item 1884. __thiscall(this=the
// vector) + 3 stack args (RET 0xc): MSVC 7.0's
// `std::vector<FAiTarget>::insert(iterator Where, size_type Count, const
// FAiTarget& Value)` / `_Insert_n`, all three branches. Growth, when needed,
// is `NewCapacity = size() + max(size(), Count)`, allocated with a raw
// `operator new` and released with `operator delete` (0x06391d5a) -- no
// element destructors run because the element type has none. The only live
// call site (FUN_062e7580) always uses Where == End and Count == 1, i.e.
// push_back, but the other two branches are ported too rather than stubbed.
void InsertAiTargets(FAiTargetVector* Self, FAiTarget* Where, unsigned Count, FAiTarget const& Value);

// ----------------------------------------------------------------------------
// The three real methods on FAiTargetList
// ----------------------------------------------------------------------------

// Common.dll.c 0x062e7580 (FUN_062e7580), item 1800. __thiscall, no stack
// args, returns a pointer to Self->Targets. Raw disasm
// Common.dll.txt:243851-244072 (194 insns), hand-derived instruction by
// instruction.
//
// Rebuilds the list of nearby objects the owner's scanner can see, unless a
// cached list is still considered good:
//   1. If bKeepCache is clear, ClearAiTargetVector first (0x062e7595).
//   2. If bBuilt AND bKeepCache are both set, return the cached vector
//      untouched (0x062e75d3-0x062e75df).
//   3. Set bBuilt, then recover the owner's own IObjRW from
//      SystemManager::Watcher (+0x08) by the "-8 Watchable adjustment": the
//      watcher stores the target's Watchable sub-object, which IObjRW places
//      at its own +0x08 (IObjRW : IObjInspect, IObjAffect, Watchable), so
//      subtracting 8 recovers the object. Bail out returning the vector
//      as-is if nothing is watched.
//   4. IObjInspect::get_equipment_status(Status, ScannerEquipId) -- bail if
//      Status.Flags has EquipStatusFlagDestroyed (0x02).
//   5. IObjInspect::get_scanlist(List, 0x7FFDFF, bKeepCache) -- bail unless
//      it returns 0. The 0x7FFDFF mask is the object-type set to scan for.
//   6. Reserve exactly List->Count elements if the CURRENT CAPACITY (not the
//      size) is smaller, by hand: operator new, CopyElemRange the live range
//      into the new buffer, DestroyAiTargetRange the old range, operator
//      delete the old buffer, then Cap/End/Begin.
//   7. For each of List->Count entries, take the watched Watchable, apply the
//      same -8 adjustment, build a fresh FAiTarget whose AttitudeRefIface is
//      the OWNER's object and whose Iface is the candidate, prime
//      CachedFlags via get_type, and append it if
//      `(CachedFlags & 0x7FFDFF) != 0`.
FAiTargetVector* BuildAiTargetList(FAiTargetList* Self);

// Common.dll.c 0x062e7820 (FUN_062e7820), item 1802. __thiscall, no stack
// args, returns float. Raw disasm Common.dll.txt:244122-244302 (163 insns).
// This IS the body of the exported `IBehaviorManager::get_closest_trailing_
// ship` (item 12392), which is nothing but
// `MOV ECX,[ECX+8]; ADD ECX,0x34; JMP` into here.
//
// Walks BuildAiTargetList's result and returns the distance to the nearest
// candidate that passes every gate, or FLT_MAX when none does:
//   * CachedFlags & 0x7F0000 (a ship-ish object type)
//   * !is_player()                                        (slot +0x0BC)
//   * get_target(Other) yields a non-null target          (slot +0x074)
//   * that target's get_id() equals the owner's get_id()  (slot +0x020)
//     -- i.e. the candidate is currently targeting US
//   * IsTargetCoAlignedAndAhead(Candidate, OwnerElem, 0.0f)  (item 1753)
//   * CachedAttitude <= -0.2f (.rdata:063a0794) -- hostile enough
// Distance uses retail's FastInvSqrt idiom (`FastInvSqrt(d2) * d2`), not a
// real sqrt, and both endpoints come from the cached-position idiom. The
// "owner element" is `SystemManager + 0x14FC`, the single embedded FAiTarget
// the Intercept-clearance cluster already documents.
float GetClosestTrailingShipDistance(FAiTargetList* Self);

// Common.dll.c 0x062e8280 (FUN_062e8280), item 1805. __thiscall + one float
// stack arg (RET 0x4), returns IObjInspect* or null. Raw disasm
// Common.dll.txt:244955-245102 (129 insns).
//
// Returns the interface of the physically nearest candidate that is hostile
// and (optionally) close enough, or null:
//   * CachedFlags & 0x7F0000
//   * CachedAttitude <= -0.6f (.rdata:063a0790)
//   * when MaxClearance >= 0.0f, also
//     GetInterceptClearance(OwnerElem, Candidate) < MaxClearance. A negative
//     MaxClearance skips this gate entirely (retail's own
//     `FCOMP 0.0f; JNP` at 0x062e835b), which is how callers ask for
//     "nearest hostile at any range".
//   * among the survivors, minimum Distance3(OwnerPosition, CachedPos),
//     seeded at 100000.0f (.rdata:0x47C35000) -- so a candidate farther than
//     100km is never returned even if it is the only one.
// OwnerPosition comes from GetIfacePositionOrZero (item 1771) on the owner's
// own object, resolved through the same -8 Watchable adjustment.
IObjInspect* FindClosestHostileTarget(FAiTargetList* Self, float MaxClearance);
