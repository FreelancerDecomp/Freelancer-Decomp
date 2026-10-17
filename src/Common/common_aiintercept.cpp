// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI intercept / clearance queries (retail 0x062e4350-0x062e6200, 0x062e6a80,
// 0x062e7400): intercept range and clearance, heading cones, cached range, shape-biased
// avoid direction, turn-circle approach cost, docking lookup, GetIfacePositionOrZero.
// See src/include/common_aisteer.h for the "Elem" raw-layout note.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_aisteer.cpp, code unchanged.

#include "common_aisteer.h"
#include "common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix, Transform
#include "common_cobjdb.h"        // IObjDB::GetNext (FindObjectDockingAtBase)

#include <cmath>

using namespace CmnAiSteer;

// ============================================================================
// Intercept-clearance cluster (items 1745-1751). See common_aisteer.h for the
// shared "Elem" raw-layout note.
// ============================================================================

namespace
{
    // Shared body of FUN_062e4350 (item 1745) / FUN_062e4d00 (item 1750) for the
    // case where both Elem and Target's CachedFlags already have bit 0x30000 set:
    // distance between cached positions minus the sum of their cached radii,
    // clamped to >= 0. 062e43b5-062e44c8 (1745) / 062e4d62-062e4e7b (1750),
    // byte-identical: the result stays in ST0 (double) for the caller.
    // Every `if (!bPosValid) { bPosValid = 1; Iface->get_physical_radius(...); }` below is
    // FAiTarget::RefreshPos inlined by retail, including the re-checks.
    double ComputeCachedRadialClearance(FAiTarget* Elem, FAiTarget* Target)
    {
        Target->RefreshPos();
        Vector const TargetPos = Target->CachedPos;
        // Retail re-checks the guard a second time here (already true); matched as-is.
        Target->RefreshPos();
        float const TargetRadius = Target->CachedRadius;

        Elem->RefreshPos();
        Vector const& ElemPos = Elem->CachedPos;

        // 062e4429-062e4458: the three differences (Elem - Target) stay on the
        // x87 stack; the sum is (z*z + y*y) + x*x.
        double const Dx = (double)ElemPos.x - TargetPos.x;
        double const Dy = (double)ElemPos.y - TargetPos.y;
        double const Dz = (double)ElemPos.z - TargetPos.z;
        double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        // 062e445c-062e448d: 0x5f3759df guess from the float image, one Newton
        // step (0.5f DAT_0639c13c, 1.5f DAT_0639e6d0), times DistSq; the only
        // rounding is the `FSTP float` of the distance.
        float const Dist = (float)InlineFastLength(DistSq);

        // 062e4470 / 062e4493-062e44a5: Elem's 0x7e byte tested a second time,
        // interleaved into the sequence above, with the same refill when clear.
        Elem->RefreshPos();
        // 062e44a8-062e44c8: Dist - (ElemRadius + TargetRadius) unrounded;
        // `FCOM 0.0f; TEST AH,0x5; JP` keeps it unless strictly less (NaN kept),
        // else `FSTP ST0; FLD 0.0f`.
        double const Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
        if (Clearance < 0.0)
            return 0.0;
        return Clearance;
    }
}

// FUN_062e4660, item 1747, Common.dll.txt 239080-239315 (225 instructions).
// __cdecl (Elem, TargetIface), bare `RET`, result in ST0 (double). Frame:
// `SUB ESP,0x6c` + EBP/ESI/EDI, E = ESP after the pushes; both parameter home
// slots (E+0x7c, E+0x80) are reused as scratch later on.
double EstimateLeadInterceptDistance(FAiTarget* Elem, IObjInspect const* TargetIface)
{
    FAiTarget* const Self = Elem;
    IObjInspect const* const Target = TargetIface;

    // 062e4669-062e46a9: the bounding-box corner cache, guarded by +0x7f (set
    // before the calls). get_archetype_extents(Min = E+0x54, Max = E+0x3c),
    // then FUN_062e4910 (TransformBoundingBoxCorners) on the iface's slot +0x10
    // frame into Elem+0x1c. Slot +0x10 (get_orientation) is ICF-folded with
    // slot +0x0c (get_transform) in retail and the callee reads a whole
    // Transform out of it, so the port calls get_transform() (same precedent as
    // FUN_062e5810 in this file).
    if (Self->bCornersValid == 0)
    {
        Self->bCornersValid = 1;
        Vector Min;
        Vector Max;
        Self->Iface->get_archetype_extents(Min, Max);
        TransformBoundingBoxCorners(Self->Iface->get_transform(), Min, Max, Self->CachedCorners);
    }

    // 062e46ac-062e46c6: EDI = &Elem->Iface frame +0x24 (the translation, held
    // by reference), then Target->get_position() (slot 0).
    Vector const& ElemPos = Self->Iface->get_transform().Translation;
    Vector const& TargetPos = Target->get_position();

    // 062e46c8-062e46ea: Dir = TargetPos - ElemPos, each component `FSTP float`
    // into FUN_0628b030's argument slots (E+0xc).
    Vector Dir{ TargetPos.x - ElemPos.x, TargetPos.y - ElemPos.y, TargetPos.z - ElemPos.z };

    // 062e46ef-062e4736: normalise in place. Sum (z*z + y*y) + x*x, FSQRT,
    // `FDIVR 1.0f` (DAT_0639a1d0), and each component multiplied by the
    // unrounded reciprocal, `FSTP float` back.
    double const InvLen = 1.0f / std::sqrt((static_cast<double>(Dir.z) * Dir.z +
                                            static_cast<double>(Dir.y) * Dir.y) +
                                           static_cast<double>(Dir.x) * Dir.x);
    Dir.x = static_cast<float>(Dir.x * InvLen);
    Dir.y = static_cast<float>(Dir.y * InvLen);
    Dir.z = static_cast<float>(Dir.z * InvLen);

    // 062e46f3 / 062e4738-062e474e: Elem's PosValid cache (+0x7e) refill.
    Self->RefreshPos();

    // 062e4751-062e477a: Offset = Dir * CachedRadius, `FSTP float` per
    // component (E+0x54); 062e477f-062e478a: FUN_062887d0, RayOrigin (E+0x18)
    // = ElemPos + Offset, `FADD` + `FSTP float` per component.
    float const Radius = Self->CachedRadius;
    Vector const Offset{ Dir.x * Radius, Dir.y * Radius, Dir.z * Radius };
    Vector const RayOrigin{ ElemPos.x + Offset.x, ElemPos.y + Offset.y, ElemPos.z + Offset.z };

    // 062e478f-062e47a1: Target->get_archetype_extents(Min = E+0x24, Max =
    // E+0x30); 062e47a4-062e47d8: de-interleaved into the {MinX, MaxX, MinY,
    // MaxY, MinZ, MaxZ} layout FUN_062dffd0 takes (E+0x60).
    Vector BoxMin;
    Vector BoxMax;
    Target->get_archetype_extents(BoxMin, BoxMax);
    float const Aabb[6] = { BoxMin.x, BoxMax.x, BoxMin.y, BoxMax.y, BoxMin.z, BoxMax.z };

    // 062e47cc-062e4809: OutStartsInside is param_1's home byte (E+0x7c),
    // cleared at 062e47e8; OutPoint is E+0x48; the frame is Target's slot
    // +0x10 (see above for get_transform). `CMP EAX,0x1; JZ` takes the hit arm.
    bool bStartsInside = false;
    Vector HitPoint;
    if (!RayTransformedBoxNearestHit(RayOrigin, Dir, Target->get_transform(), Aabb, &HitPoint, &bStartsInside))
    {
        // 062e480b-062e4881: miss. Target->get_physical_radius(param_2's home
        // slot, E+0x3c); the radius is fetched and never read. Returns the
        // fast length of (Centre - ElemPos), differences unrounded, sum
        // (z*z + y*y) + x*x, through the same 0x5f3759df / 0.5f / 1.5f Newton
        // step InlineFastLength reproduces.
        float UnusedRadius;
        Vector Centre;
        Target->get_physical_radius(UnusedRadius, Centre);
        double const Dx = static_cast<double>(Centre.x) - ElemPos.x;
        double const Dy = static_cast<double>(Centre.y) - ElemPos.y;
        double const Dz = static_cast<double>(Centre.z) - ElemPos.z;
        return InlineFastLength((Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    // LAB_062e4882: hit. The fast length of (HitPoint - RayOrigin) is set up
    // first (differences unrounded, same sum order); then `TEST AL,AL` on
    // bStartsInside returns 0.0f (DAT_06399408) when the ray origin is already
    // inside the box, else finishes the Newton step.
    double const Dx = static_cast<double>(HitPoint.x) - RayOrigin.x;
    double const Dy = static_cast<double>(HitPoint.y) - RayOrigin.y;
    double const Dz = static_cast<double>(HitPoint.z) - RayOrigin.z;
    double const HitDistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
    if (bStartsInside)
        return 0.0;
    return InlineFastLength(HitDistSq);
}

// Common.dll.c 0x062e4350 (FUN_062e4350), item 1745. Ported directly, cross-
// checked against Common.dll.txt:238769-238927.
bool IsWithinInterceptRange(FAiTarget* Elem, FAiTarget* Target, float Threshold)
{
    // FAiTarget::GetFlags inlined twice (the guard raised after the get_type call); the
    // target's flags are read only when Elem's have 0x30000.
    bool HaveBoth = false;
    if ((Elem->GetFlags() & 0x30000) != 0)
        HaveBoth = (Target->GetFlags() & 0x30000) != 0;

    // Both arms leave the clearance in ST0 (double). LAB_062e44da: `FCOMP
    // Threshold; TEST AH,0x41; JP` returns 0 on greater or unordered, so the
    // result is 1 for less OR EQUAL.
    double const Clearance = HaveBoth
        ? ComputeCachedRadialClearance(Elem, Target)
        : EstimateLeadInterceptDistance(Elem, Target->Iface);
    return Clearance <= Threshold;
}

// Common.dll.c 0x062e4d00 (FUN_062e4d00), item 1750. Same body as
// IsWithinInterceptRange above minus the final threshold compare. Ported
// directly, cross-checked against Common.dll.txt:239710-239861.
double GetInterceptClearance(FAiTarget* Elem, FAiTarget* Target)
{
    bool HaveBoth = false;
    if ((Elem->GetFlags() & 0x30000) != 0)
        HaveBoth = (Target->GetFlags() & 0x30000) != 0;

    return HaveBoth
        ? ComputeCachedRadialClearance(Elem, Target)
        : EstimateLeadInterceptDistance(Elem, Target->Iface);
}

// Common.dll.c 0x062e4500 (FUN_062e4500), item 1746. RE-VERIFIED 2026-09-11
// (d264:common-port): the prior pass's "unaff_ESI/unaff_retaddr -- same two
// Elem/Target pointers as the clean twins, not worth reconstructing" call was
// a FALSE POSITIVE, same root cause as the rest of this cluster's "unaff_"
// claims -- Ghidra guessed the wrong parameter count for this function (its
// own signature comment marks BOTH params storage class UNASSIGNED), so a
// genuine THIRD stack parameter and a lazily-reloaded first parameter both
// get mis-rendered as "unaffected incoming registers" instead of recognized
// reads. Byte-exact SUB/PUSH depth tracking through the whole 114-insn body
// (Common.dll.txt:238939-239069) resolves all three real parameters and
// shows this is NOT a duplicate of IsWithinInterceptRange/item 1745 -- it is
// a genuine sibling overload for callers that only have the Target's
// already-resolved interface pointer, not a full Target record:
//   - 2nd param (EDI, entry_ESP+8) = TargetIface directly (what 1745 would
//     compute as `*(pT+0x84)`). Its very first action is an UNCONDITIONAL
//     call through TargetIface's own vtable slot 0x88 (IObjInspect::get_type)
//     to fetch a fresh flags int into a local -- unconditional because there
//     is no backing Target struct to cache a "already refreshed" byte into,
//     unlike Elem's own 0x7d guard.
//   - 1st param (ESI, entry_ESP+4) = Elem, loaded lazily right after that
//     call; refreshed/cached exactly like 1745's own pE (0x7d flags-refresh,
//     then 0x7e pos/radius-refresh through Elem's own +0x84 iface).
//   - 3rd param (entry_ESP+0xC, float Threshold) is compared against the
//     final clearance with the byte-identical FCOMP/FNSTSW/TEST AH,0x41/JP
//     tail idiom 1745 uses.
// The gate condition is NOT identical to 1745's: since there is no Target-
// side cached-flags byte to lean on, retail takes the "compute directly"
// path whenever EITHER (Elem's own 0x30000 bit is set AND TargetIface's
// fresh flags also have 0x30000 set) OR TargetIface's fresh flags have any
// of bits 0x207 set outright (regardless of Elem's own bit) -- confirmed via
// the extra TEST EAX,0x207/JZ at Common.dll.txt:239051-239055 with no
// equivalent in 1745's body; the 0x207 branch reaches the SAME compute path
// (LAB_062e4560) via fallthrough, not a separate call. Target's own pos/
// radius are fetched fresh into locals on every call (no caching possible
// without a backing struct) through TargetIface's own vtable slot 0x2c,
// same (radius-arg-first, pos-arg-second) order 1745 uses for its own
// pT+0x18/pT+0xc pair. Elem's pos/radius still cache through its usual 0x7e
// byte. Falls back to EstimateLeadInterceptDistance exactly like 1745/1750.
bool IsWithinInterceptRangeOfIface(FAiTarget* Elem, IObjInspect const* TargetIface, float Threshold)
{
    // Unconditional -- TargetIface has no cache byte of its own to guard this.
    unsigned TargetFlags = 0;
    TargetIface->get_type(TargetFlags);

    bool const ElemHasFlag = (Elem->GetFlags() & 0x30000) != 0;

    // Common.dll.txt:238969-238977/239051-239055: NOT a simple "both flags
    // set" AND like 1745 -- TargetFlags&0x207 alone is also accepted,
    // independent of ElemHasFlag.
    bool const UseCached = (ElemHasFlag && (TargetFlags & 0x30000) != 0) || (TargetFlags & 0x207) != 0;

    // The clearance lives on the x87 stack from here to the final compare (double).
    double Clearance;
    if (UseCached)
    {
        // 062e4560-062e458c: TargetIface slot 11 get_physical_radius(param_1's home slot,
        // a local Vector), then Elem's 0x7e-cached refill (flag set before the call). Retail
        // leaves both uninitialised; the zeroes only matter if the callee writes nothing.
        float TargetRadius = 0.0f;
        Vector TargetPos{};
        TargetIface->get_physical_radius(TargetRadius, TargetPos);

        Elem->RefreshPos();
        Vector const& ElemPos = Elem->CachedPos;

        // 062e458f-062e45c0: the three differences stay unrounded; (z*z + y*y) + x*x.
        double const Dx = (double)ElemPos.x - TargetPos.x;
        double const Dy = (double)ElemPos.y - TargetPos.y;
        double const Dz = (double)ElemPos.z - TargetPos.z;
        double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        // 062e45c2-062e45f3: 0x5f3759df guess from the float image, one Newton step (0.5f
        // DAT_0639c13c, 1.5f DAT_0639e6d0), times DistSq; `FSTP float` to param_2's slot.
        float const Dist = (float)InlineFastLength(DistSq);

        // 062e45d6 / 062e45f9-062e460c: Elem's 0x7e byte is tested a second time, interleaved
        // into the sequence above, with the same refill when clear.
        Elem->RefreshPos();

        // 062e460f-062e462f: Dist - (ElemRadius + TargetRadius); `FCOM 0.0f; TEST AH,0x5; JP`
        // keeps it unless strictly less (NaN kept), else FLD 0.0f.
        Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
        if (Clearance < 0.0)
            Clearance = 0.0;
    }
    else
    {
        // LAB_062e4631: FUN_062e4660(Elem, TargetIface), its ST0 used as is.
        Clearance = EstimateLeadInterceptDistance(Elem, TargetIface);
    }

    // LAB_062e463b: `FCOMP Threshold; TEST AH,0x41; JP` -> 0 on greater or unordered, so the
    // result is 1 for less OR EQUAL.
    return Clearance <= Threshold;
}

// Common.dll.c 0x062e4bb0 (FUN_062e4bb0), item 1749. PORTED 2026-09-17
// (claude:common-steering-close), closing the last residual on MoveIdea::Update
// (item 1577). Ghidra could not recover either parameter ("<UNASSIGNED>" for
// both) and the item sat `simplified` on that basis; a full 109-instruction hand
// trace of Common.dll.txt:239580-239697 with byte-exact stack-depth accounting
// resolves it completely, and the "register corruption" was never real:
//   * `RET` with no immediate (062e4ce4/062e4cf4) => __cdecl, two stack params.
//   * param_1 (E+4) = Elem, param_2 (E+8) = TargetIface. EDI is loaded from
//     E+8 immediately (062e4bb5); ESI is loaded from E+4 only AFTER the first
//     vtable call (062e4bd0), which is what Ghidra mistook for an unaffected
//     incoming register.
//   * BOTH parameter home slots are then reused as float scratch: E+4 holds
//     TargetRadius (062e4c17 `LEA ECX,[ESP+0x20]` at depth E-0x1c) and E+8
//     holds the running distance for the FastInvSqrt bit-trick (062e4c72 `FST
//     [ESP+0x20]` / 062e4c7e `MOV [ESP+0x20],EDX` at depth E-0x18). That slot
//     reuse is the whole reason their storage class came back UNASSIGNED.
//
// Body: identical to IsWithinInterceptRangeOfIface above, up to and excluding
// the final threshold compare -- same unconditional TargetIface vtable+0x88
// flags fetch into a zeroed local, same Elem 0x7d flags cache, the same
// asymmetric gate `(Elem&0x30000 && Target&0x30000) || (Target&0x207)`
// (062e4bf1-062e4c0a, the identical extra `TEST EAX,0x207`), the same
// vtable+0x2c pos/radius fetches with Elem's 0x7e cache, the same
// FastInvSqrt(DistSq)*DistSq distance and the same `Dist - (ElemRadius +
// TargetRadius)` clearance. It returns that clearance clamped at zero
// (062e4cca-062e4ce4: `FCOM 0.0f; TEST AH,0x5; JP` keeps the value when it is
// >= 0 and loads 0.0f otherwise -- DAT_06399408 is 0.0f) instead of comparing
// it, and falls back to EstimateLeadInterceptDistance(Elem, TargetIface) on the
// gate's else path (062e4ce5, `PUSH EDI; PUSH ESI`), returning its result
// unclamped -- exactly the relationship GetInterceptClearance/item 1750 has to
// IsWithinInterceptRange/item 1745.
double GetInterceptClearanceOfIface(FAiTarget* Elem, IObjInspect const* TargetIface)
{
    // Unconditional -- TargetIface has no cache byte of its own to guard this.
    unsigned TargetFlags = 0;
    TargetIface->get_type(TargetFlags);

    bool const ElemHasFlag = (Elem->GetFlags() & 0x30000) != 0;
    bool const UseCached = (ElemHasFlag && (TargetFlags & 0x30000) != 0) || (TargetFlags & 0x207) != 0;

    if (!UseCached)
    {
        // Retail does NOT clamp this branch's result (LAB_062e4ce5 falls
        // straight through to the shared RET at LAB_062e4cef, bypassing the
        // FCOM against 0.0f).
        return EstimateLeadInterceptDistance(Elem, TargetIface);
    }

    float TargetRadius = 0.0f;
    Vector TargetPos{};
    TargetIface->get_physical_radius(TargetRadius, TargetPos);

    Elem->RefreshPos();
    Vector const& ElemPos = Elem->CachedPos;

    // 062e4c3f-062e4c70: unrounded differences, (z*z + y*y) + x*x on the x87 stack.
    double const Dx = (double)ElemPos.x - TargetPos.x;
    double const Dy = (double)ElemPos.y - TargetPos.y;
    double const Dz = (double)ElemPos.z - TargetPos.z;
    double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
    // 062e4c72-062e4ca3: 0x5f3759df guess from the float image, one Newton step (0.5f
    // DAT_0639c13c, 1.5f DAT_0639e6d0), times DistSq; `FSTP float` to param_2's home slot.
    float const Dist = (float)InlineFastLength(DistSq);

    // 062e4c86 / 062e4ca9-062e4cbc: Elem's 0x7e byte tested again, interleaved into the
    // sequence above, with the same refill when clear.
    Elem->RefreshPos();

    // 062e4cbf-062e4ce4: Dist - (ElemRadius + TargetRadius) stays on the x87 stack; `FCOM
    // 0.0f; TEST AH,0x5; JP LAB_062e4cef` returns it unless strictly less (NaN returned as
    // is), else FLD 0.0f.
    double const Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
    if (Clearance < 0.0)
        return 0.0;
    return Clearance;
}

// FUN_062e53e0, item 1755, Common.dll.txt:240396-240560 (141 instructions).
// PORTED IN FULL 2026-09-17 (claude:common-idea-updates). See common_aisteer.h
// for the predicate's meaning and for the retirement of this item's old
// "the two branches disagree on which stack slot holds Z" claim.
//
// Frame map, because every slot reference below depends on it: `SUB ESP,0x24`
// plus one `PUSH ESI`, so with B == ESP after the prologue the arguments are
// param_1 at B+0x2c, param_2 at B+0x30 and param_3 at B+0x34. The tail reads
// param_3 as `[ESP+0x30]` because `POP ESI` at 062e5552 has already moved ESP
// to B+4 by then -- the classic depth shift this marathon keeps tripping over.
// The function ends on a bare `RET` (062e55a5/062e55ab), so it is __cdecl with
// three arguments, which is exactly what BreakAwayIdea's own two call sites
// clean (`ADD ESP,0xc` at 062d926c and 062d9324).
bool IsPointOutsideHeadingCone(Vector const& Point, IObjInspect const* Iface,
                                          float CosThreshold)
{
    // 062e53e8-062e5438. Retail passes `&param_2` itself as the scratch
    // `float& Radius` (062e53f3, Ghidra annotates it `=>param_2`) and B+0x10 as
    // the `Vector& Centre`.
    Vector Centre{ 0.0f, 0.0f, 0.0f };
    if (Iface != nullptr)
    {
        float Radius = 0.0f;
        if (Iface->get_physical_radius(Radius, Centre) != 0)
        {
            // 062e5423: on the NON-zero return retail calls slot 0x20
            // (get_id()), throws the result away, and falls into the very
            // same "centre is the origin" path a null Iface takes. Only the
            // zero return uses the filled centre. Reproduced, not tidied.
            (void)Iface->get_id();
            Centre = Vector{ 0.0f, 0.0f, 0.0f };
        }
    }

    // 062e543e-062e5470. Direction points FROM the iface's centre TO Point; each
    // difference is FSTP'd to float.
    Vector Dir{ Point.x - Centre.x, Point.y - Centre.y, Point.z - Centre.z };

    // 062e5472-062e54a3: (z*z + y*y) + x*x, FSQRT and the `FCOM 0.01f`
    // (DAT_0639e6b8 == 0x3c23d70a; TEST AH,0x41 / JNZ: not-greater zeroes) all on
    // the x87 stack, unrounded.
    double const Length = std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
    if (Length > 0.01f)
    {
        // 062e54a5-062e54c5: `FDIVR [DAT_0639a1d0]` (1.0f) keeps the reciprocal on
        // the stack; only the three products are FSTP'd.
        double const Inv = 1.0 / Length;
        Dir.x = (float)(Dir.x * Inv);
        Dir.y = (float)(Dir.y * Inv);
        Dir.z = (float)(Dir.z * Inv);
    }
    else
    {
        Dir = Vector{ 0.0f, 0.0f, 0.0f }; // 062e54cb-062e54db
    }

    // 062e54e3-062e554c. Retail builds the raw column 2 with one Vec3Assign and
    // then negates it into a second Vec3Assign; only the negated copy is used.
    Vector Heading{ 0.0f, 0.0f, 0.0f };
    if (Iface != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Iface->get_transform().Rotation);
        Vec3Assign(&Heading, -Col2.x, -Col2.y, -Col2.z);
    }

    // 062e554e-062e5567: (z*z' + y*y') + x*x', unrounded.
    // 062e5569-062e5593: clamp into [-1, 1] (DAT_0639a1d0 == 1.0f,
    // DAT_06399404 == 0xbf800000 == -1.0f).
    double const Dot = ClampCos11(DotZyx(Dir, Heading));

    // 062e5595-062e55ab: `FCOMP threshold; TEST AH,0x5; JP` -- only strictly
    // less returns true.
    return Dot < CosThreshold;
}

// FUN_062e5190, item 1754, Common.dll.txt:240181-240392 (191 instructions).
// PORTED IN FULL 2026-09-17 (claude:common-idea-updates). It is the two-object
// sibling of IsPointOutsideHeadingCone above: same clamp, same final compare,
// but the point is Elem's own cached centre and the cone belongs to TargetIface.
//
// The item's old note ("algorithm understood, not ported verbatim to avoid
// guessing the operand roles wrong ... three-level fallback chain") is retired.
// The operand roles are pinned by the frame, not guessed. With B == ESP after
// `SUB ESP,0x30; PUSH EDI`, the arguments are at B+0x38 / B+0x3c / B+0x40, and
// retail reuses ALL THREE argument slots as scratch after reading them into
// registers: B+0x38 holds Dot1 from 062e5228, B+0x3c holds the scratch Radius at
// 062e5235 and then the distance at 062e52f6, and only B+0x40 -- the third
// argument Ghidra never listed -- survives to the final compare at 062e53c5.
// That is also the proof there IS a third argument: Ghidra's header shows only
// param_1/param_2, but the body reads B+0x40 and every call site cleans 0xc
// (e.g. 062d9384 in BreakAwayIdea::EvaluateBreakAwayClearance).
//
// Three depth shifts to keep straight while reading the listing against this:
// `PUSH ESI` at 062e51d7 moves the frame by 4 for the whole middle section,
// `POP ESI` at 062e52c7 moves it back, and `POP EDI` at 062e538d moves it again
// before the tail -- which is why the same argument is spelled `[ESP+0x3c]` at
// 062e52f6 (the distance slot, B+0x3c) and `[ESP+0x3c]` at 062e53c5 (the
// threshold slot, B+0x40). Two identical-looking texts, two different addresses.
//
// Leaf identifications, all already-ported primitives:
//   FUN_0628b030 / FUN_0636cf40 = Vec3Assign (two ICF-distinct instances of the
//                                 identical `[ECX]=a; [ECX+4]=b; [ECX+8]=c` body)
//   FUN_062c5a70               = VecNegate3
//   FUN_062dc410               = VecNormalizeInPlaceByLength3
//   FUN_062c47a0               = VecDot3 (Dot3 here, see the file-local note)
bool IsAlignedAndOutsideHeadingCone(FAiTarget* Elem, IObjInspect const* TargetIface, float CosThreshold)
{
    IObjInspect const* const Target = TargetIface;

    // 062e5198-062e51d6: TargetIface's heading, or zero when it is null.
    Vector TargetHeading{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Target->get_transform().Rotation);
        VecNegate3(&Col2, &TargetHeading);
    }

    // 062e51d7-062e5209: Elem's owner's heading, same shape.
    Vector OwnerHeading{ 0.0f, 0.0f, 0.0f };
    {
        Vector const Col2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        VecNegate3(&Col2, &OwnerHeading);
    }

    // 062e520a-062e5228: (z*z' + y*y') + x*x' on the x87 stack, FSTP'd once to
    // float into the param_1 slot.
    float const HeadingAlignment = (float)DotZyx(TargetHeading, OwnerHeading);

    // 062e522c-062e527b: TargetIface's own centre. Exactly the same inverted
    // convention IsPointOutsideHeadingCone uses -- the filled centre is taken
    // only on the ZERO return, and the non-zero return calls slot 0x20
    // (get_id()), discards it, and falls into the zero-centre path.
    Vector TargetCentre{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        float Radius = 0.0f;
        Vector Centre{ 0.0f, 0.0f, 0.0f };
        if (Target->get_physical_radius(Radius, Centre) == 0)
        {
            TargetCentre = Centre;
        }
        else
        {
            (void)Target->get_id();
        }
    }

    // LAB_062e527c, 062e527c-062e5299: the family's refresh-once nav cache, the
    // same `Elem[0x7e]` byte and `(Elem+0x18, Elem+0xc)` pair every sibling in
    // this file uses (FAiTarget::RefreshPos inlined).
    Elem->RefreshPos();
    Vector const& ElemCentre = Elem->CachedPos;

    // LAB_062e529a, 062e529a-062e5311. Direction points FROM the target TO Elem.
    Vector Dir{ ElemCentre.x - TargetCentre.x,
                ElemCentre.y - TargetCentre.y,
                ElemCentre.z - TargetCentre.z };
    // 062e52d0-062e5305: (z*z + y*y) + x*x and FSQRT unrounded; `FST float` keeps a
    // rounded copy in the param_2 slot for FUN_062dc410, while `FCOMP 0.01f`
    // (DAT_0639e6b8; TEST AH,0x41 / JNZ: not-greater zeroes) tests the unrounded one.
    double const Distance = std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
    if (Distance > 0.01f)
    {
        VecNormalizeInPlaceByLength3(&Dir, (float)Distance);
    }
    else
    {
        Vec3Assign(&Dir, 0.0f, 0.0f, 0.0f);
    }

    // LAB_062e531e-062e5374: retail rebuilds TargetIface's heading a SECOND
    // time here rather than reusing the copy it already has. Reproduced, since
    // get_transform() is a live accessor and the re-fetch is really in the code.
    Vector Heading{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Target->get_transform().Rotation);
        VecNegate3(&Col2, &Heading);
    }
    else
    {
        Vec3Assign(&Heading, 0.0f, 0.0f, 0.0f);
    }

    // LAB_062e5375-062e53b3: FUN_062c47a0(Dir, Heading) left in ST0, clamped unrounded.
    double const Dot = ClampCos11(DotZyx(Dir, Heading));

    // 062e53b4-062e53dd. `TEST AH,0x41; JNZ` on the first compare takes the
    // not-greater case away, and `TEST AH,0x5` with `JP` on the second leaves
    // only strictly-less falling through to `MOV AL,1`.
    if (!(HeadingAlignment > 0.0f))
    {
        return false;
    }
    return Dot < CosThreshold;
}

// Common.dll.c 0x062e4ea0 (FUN_062e4ea0), item 1751. Ported directly, cross-
// checked against Common.dll.txt:239876-239932.
double GetSignedRadialClearanceToPoint(FAiTarget* Elem, Vector const& Point)
{
    Elem->RefreshPos();
    // 062e4ec3-062e4ef2: the three differences stay on the x87 stack; the sum
    // is (dx*dx + dz*dz) + dy*dy (FLD ST2/FMUL ST3, FLD ST1/FMUL ST2, FADDP,
    // FLD ST2/FMUL ST3, FADDP) and only the FSQRT result is rounded, by
    // `FSTP float` into the dead param_1 stack slot.
    Vector const& ElemPos = Elem->CachedPos;
    double const Dx = (double)ElemPos.x - Point.x;
    double const Dy = (double)ElemPos.y - Point.y;
    double const Dz = (double)ElemPos.z - Point.z;
    float const Dist = (float)std::sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);

    // Retail re-checks the guard a second time here (already true); matched as-is.
    Elem->RefreshPos();
    // 062e4f12-062e4f1a: `FLD [Elem+0x18]; FSUBR Dist; RET` -- returned in ST0.
    return (double)Dist - Elem->CachedRadius;
}

// Common.dll.c 0x062e4f60 (FUN_062e4f60), item 1753. Hand-derived from
// Common.dll.txt:239984-240173; see common_aisteer.h's declaration for the
// operand-order proof and for why retail's param_2 stack slot doubles as a
// scratch local. Argument names below follow the CALL-SITE order, not
// retail's ESI/EDI assignment.
bool IsTargetCoAlignedAndAhead(FAiTarget* Target, FAiTarget* Reference, float CosThreshold)
{
    // Step 1: dot of the two objects' forward axes (Matrix column 2 of each
    // object's own transform). Retail negates both vectors through VecNegate3
    // before dotting them (0x062e4f92 / 0x062e4fc4); negation is exact and
    // (-a)*(-b) == a*b bit for bit, so the port drops the pair of negations.
    // 0x062e4fc9-0x062e4fea: (z*z' + y*y') + x*x' on the x87 stack, FSTP'd once to
    // float into the param_2 slot.
    Vector const RefForward = MatrixColumn2(Reference->Iface->get_transform().Rotation);
    Vector const TargetForward = MatrixColumn2(Target->Iface->get_transform().Rotation);
    float const HeadingDot = (float)DotZyx(RefForward, TargetForward);

    // Step 2: refresh both cached positions (same guard idiom as every other
    // accessor in this cluster; Reference first, then Target, matching
    // 0x062e4fee / 0x062e500c).
    if (Reference->bPosValid == 0)
    {
        Reference->bPosValid = 1;
        Reference->Iface->get_physical_radius(Reference->CachedRadius, Reference->CachedPos);
    }
    if (Target->bPosValid == 0)
    {
        Target->bPosValid = 1;
        Target->Iface->get_physical_radius(Target->CachedRadius, Target->CachedPos);
    }

    Vector Direction;
    Direction.x = Target->CachedPos.x - Reference->CachedPos.x;
    Direction.y = Target->CachedPos.y - Reference->CachedPos.y;
    Direction.z = Target->CachedPos.z - Reference->CachedPos.z;

    // Retail (0x062e5053-0x062e50a6): (z*z + y*y) + x*x, a real FSQRT, the compare
    // against 0.01f (.rdata:0639e6b8; TEST AH,0x41 / JNZ: not-greater zeroes) and
    // `FDIVR 1.0f`, all unrounded on the x87 stack -- NOT the FastInvSqrt idiom the
    // 1745/1750 pair use. Only the three normalised components are FSTP'd.
    double const Length = std::sqrt(((double)Direction.z * Direction.z +
                                     (double)Direction.y * Direction.y) +
                                    (double)Direction.x * Direction.x);
    if (Length > 0.01f)
    {
        double const InvLength = 1.0 / Length;
        Direction.x = (float)(Direction.x * InvLength);
        Direction.y = (float)(Direction.y * InvLength);
        Direction.z = (float)(Direction.z * InvLength);
    }
    else
    {
        Direction.x = 0.0f;
        Direction.y = 0.0f;
        Direction.z = 0.0f;
    }

    // Retail re-fetches the reference's forward axis here (0x062e50cf-
    // 0x062e50f2) instead of reusing the one from step 1, then negates it
    // inline with three FCHS. Reproduced as a real second call because the
    // interface is free to report a different transform mid-function.
    // 0x062e512a: FUN_062c47a0(Direction, -RefForward), left unrounded in ST0 through
    // the clamp and the threshold compare.
    Vector const RefForwardAgain = MatrixColumn2(Reference->Iface->get_transform().Rotation);
    Vector const RefHeading{ -RefForwardAgain.x, -RefForwardAgain.y, -RefForwardAgain.z };
    double const Cosine = ClampCos11(DotZyx(Direction, RefHeading));

    // 0x062e5164 compares the heading dot against .rdata:06399408 == 0.0f
    // with `TEST AH,0x41; JNZ <return false>`, i.e. "greater than", strictly.
    if (!(HeadingDot > 0.0f))
    {
        return false;
    }
    return Cosine < CosThreshold;
}

// Common.dll.c 0x062e6a80 (FUN_062e6a80), item 1791. Ported directly, hand-
// disassembled from Common.dll.txt:242775-242823 (39 insns, no Ghidra
// decompile issues -- single straight-line path with one early-out branch).
// Catalogued under the separate IUStateGraph cluster (see common_aisteer.h's
// "CROSS-CLUSTER IDENTITY" note above the Intercept-clearance block), but
// mechanically it is just another lazily-cached accessor on the same Elem
// record layout, so it lives here beside Elem's other accessors.
void GetCachedRange(FAiTarget* Elem, float* OutLow, float* OutHigh)
{
    if (Elem->bAttitudeValid == 0)
    {
        IObjInspect* const RefIface = Elem->AttitudeRefIface;
        IObjInspect* const OtherIface = Elem->Iface;

        // Retail unconditionally zeroes both cached values up front (Common.dll.txt:
        // 242793-242794); only left non-zero below if the query genuinely succeeds.
        Elem->CachedAttitude = 0.0f;
        Elem->CachedReverseAttitude = 0.0f;

        if (RefIface != nullptr && OtherIface != nullptr)
        {
            // Slot 27, get_attitude_towards_symmetrical(float& OutAtt, IObjInspect const*
            // Other, float& OutReverse); its int return is 0 on success.
            int const Result = RefIface->get_attitude_towards_symmetrical(
                Elem->CachedAttitude, OtherIface, Elem->CachedReverseAttitude);
            if (Result != 0)
            {
                // Failure -- retail re-zeroes both fields (redundant with the
                // pre-zero above but matched as-is for byte-exact fidelity).
                Elem->CachedAttitude = 0.0f;
                Elem->CachedReverseAttitude = 0.0f;
            }
        }
        // A null interface falls through with both already zeroed (LAB_062e6ac3).

        Elem->bAttitudeValid = 1; // regardless of success/failure
    }

    *OutLow = Elem->CachedAttitude;
    *OutHigh = Elem->CachedReverseAttitude;
}

// Common.dll.c 0x062e6150 (FUN_062e6150), item 1772. Ported directly, cross-
// checked against raw disasm Common.dll.txt (~241275-241320). Small, cleanly
// decompiled, no register corruption. Fetches a direction/basis-row vector
// through an unidentified interface's vtable slot 0xc (same "iVar=vtable(); Out
// from iVar+8/+0x14/+0x20" shape used by EstimateLeadInterceptDistance/item
// 1747 for the math-engine's own row fetch) and negates it; zero-fills on null.
void GetNegatedIfaceDirection(Vector* OutDir, IObjInspect const* Iface)
{
    if (Iface != nullptr)
    {
        // Slot 3 is get_transform(). Offsets 8 / 0x14 / 0x20 into the returned 3x3 are
        // E[0][2] / E[1][2] / E[2][2] -- column 2, the forward basis vector.
        Matrix const& Basis = Iface->get_transform().Rotation;
        OutDir->x = -Basis.E[0][2];
        OutDir->y = -Basis.E[1][2];
        OutDir->z = -Basis.E[2][2];
        return;
    }
    *OutDir = Vector{ 0.0f, 0.0f, 0.0f };
}

// FUN_062e61a0, item 1773, Common.dll.txt 241797-241832 (all 28 insns).
double GetIfaceVectorMagnitude(IObjInspect const* Iface)
{
    // 062e61a7-062e61a9 / LAB_062e61d9: FLD 0.0f (DAT_06399408) on a null Iface.
    if (Iface == nullptr)
        return 0.0f;

    // 062e61ab-062e61b2: slot 1 (+0x04) is get_velocity(), returned through the hidden buffer
    // at [ESP]. 062e61b5-062e61cd: (x*x + y*y) + z*z and FSQRT, all on the x87 stack; the
    // result is returned in ST0 without a float store, so it stays double here.
    Vector const V = Iface->get_velocity();
    double const X = V.x;
    double const Y = V.y;
    double const Z = V.z;
    return std::sqrt((X * X + Y * Y) + Z * Z);
}

// Common.dll.c 0x062e4910 (FUN_062e4910), item 1748. PORTED FOR REAL 2026-09-17
// (claude:common-unaff-rederive) from a complete 223/223-instruction hand trace
// of Common.dll.txt:239332-239562. Both claimed corruptions were parameters
// Ghidra never counted -- see common_aisteer.h.
//
// Retail is eight copies of the same nine-instruction block: stage one corner of
// the [Min, Max] box into a scratch Vector, re-acquire the 3DMathEngine
// singleton, call TransformXform (slot +0x28), and copy the three result dwords
// into OutCorners[Index]. The corner order is a plain 3-bit count with X as the
// high bit and Z as the low one, which the listing confirms block by block:
//   0 (Min.x,Min.y,Min.z) 239340/239342/239344 -> Out+0x00
//   1 (Min.x,Min.y,Max.z) 239368/239372/239374 -> Out+0x0c
//   2 (Min.x,Max.y,Min.z) 239395/239399/239401 -> Out+0x18
//   3 (Min.x,Max.y,Max.z) 239422/239426/239428 -> Out+0x24
//   4 (Max.x,Min.y,Min.z) 239449/239453/239455 -> Out+0x30
//   5 (Max.x,Min.y,Max.z) 239476/239480/239482 -> Out+0x3c
//   6 (Max.x,Max.y,Min.z) 239503/239507/239509 -> Out+0x48
//   7 (Max.x,Max.y,Max.z) 239530/239534/239536 -> Out+0x54
// Retail's eight separate lazy-init checks of DAT_063fca48 are reproduced by
// re-calling Acquire3DMathEngine() inside the loop rather than hoisting it.
void TransformBoundingBoxCorners(Transform const& Xform, Vector const& Min,
                                            Vector const& Max, Vector* OutCorners)
{
    for (int Index = 0; Index < 8; ++Index)
    {
        Vector const Corner{ ((Index & 4) != 0) ? Max.x : Min.x,
                             ((Index & 2) != 0) ? Max.y : Min.y,
                             ((Index & 1) != 0) ? Max.z : Min.z };

        Vector Transformed;
        Acquire3DMathEngine()->TransformXform(reinterpret_cast<FVector&>(Transformed),
                                              reinterpret_cast<FTransform const&>(Xform),
                                              reinterpret_cast<FVector const&>(Corner));
        OutCorners[Index] = Transformed;
    }
}

// Common.dll.c 0x062e55b0 (FUN_062e55b0), item 1756. PORTED FOR REAL 2026-09-17
// (claude:common-unaff-rederive) from a complete 212/212-instruction hand trace
// of Common.dll.txt:240578-240800. All three claimed corruptions were false
// positives -- see common_aisteer.h.
//
// Frame map, relative to the entry ESP `E` (SUB 0x4c + 2 pushes => a base depth
// of E-0x54, with one extra PUSH EDI live across the middle section):
//   E+0x04 Out   E+0x08 Elem   E+0x0C Target   (E+0x0C doubles as FP scratch!)
//   E-0x4c Radius (get_physical_radius out-param -- WRITTEN AND NEVER READ)
//   E-0x48 LocalX copy   E-0x44 LocalY   E-0x40 LocalZ
//   E-0x3c InverseTransform out (LocalPos), later Rotate's out
//   E-0x30 the local-space direction, copied to *Out at the tail
//   E-0x24 ExtentsMax     E-0x18 ExtentsMin     E-0x0c TargetCentre
Vector* ComputeShapeBiasedAvoidDirection(Vector* Out, FAiTarget* Elem, IObjInspect* Target)
{
    // 240583-240585: the three direction slots are zeroed before the null test.
    Vector Direction{ 0.0f, 0.0f, 0.0f };

    // 240582/240586-240598: no Target at all -> a zero direction.
    if (Target == nullptr)
    {
        *Out = Direction;
        return Out;
    }

    // 062e55f5-062e5604: get_physical_radius (+0x2c) into S+0x8 (never read again) and the
    // centre S+0x48; the radius slot is not pre-zeroed.
    float Radius;
    Vector TargetCentre;
    Target->get_physical_radius(Radius, TargetCentre);

    // 062e5607-062e5639: LocalPos (S+0x18) = InverseTransform (+0x3c) of the centre by the
    // Elem's transform (slot +0x10), engine created lazily with no null test.
    Vector LocalPos;
    Acquire3DMathEngine()->InverseTransform(reinterpret_cast<FVector&>(LocalPos),
                                            reinterpret_cast<FTransform const&>(Elem->Iface->get_transform()),
                                            reinterpret_cast<FVector const&>(TargetCentre));

    // 062e563c-062e56ce: the working copy W (S+0xc..S+0x14) = LocalPos; the inlined fast
    // length of the unrounded (x*x + y*y) + z*z FCOMP double 1e-5 (DAT_0639f418), TEST AH,0x5
    // + JP: only "less" answers (1, 0, 0).
    float const WorkX = LocalPos.x;
    float WorkY = LocalPos.y;
    float WorkZ = LocalPos.z;
    double const LocalLenSq = (static_cast<double>(LocalPos.x) * LocalPos.x +
                               static_cast<double>(LocalPos.y) * LocalPos.y) +
                              static_cast<double>(LocalPos.z) * LocalPos.z;
    if (InlineFastLength(LocalLenSq) < 1e-5)
    {
        Out->x = 1.0f;
        Out->y = 0.0f;
        Out->z = 0.0f;
        return Out;
    }

    // 062e56cf-062e572e: get_archetype_extents (+0x28) into Min (S+0x3c) / Max (S+0x30);
    // H = ((Max.z - Min.z) + (Max.x - Min.x)) * 0.5 and V = Max.y - Min.y on the FPU;
    // flat = H / V > double 2.0 (DAT_0639c420), tall = V / H > 2.0f (DAT_063a0498), each a
    // TEST AH,0x41 "greater" test.
    Vector ExtentsMin;
    Vector ExtentsMax;
    Target->get_archetype_extents(ExtentsMin, ExtentsMax);
    double const HorizontalHalf = ((static_cast<double>(ExtentsMax.z) - ExtentsMin.z) +
                                   (static_cast<double>(ExtentsMax.x) - ExtentsMin.x)) * 0.5;
    double const VerticalExtent = static_cast<double>(ExtentsMax.y) - ExtentsMin.y;
    bool const bFlatObstacle = (HorizontalHalf / VerticalExtent) > 2.0;
    bool const bTallObstacle = (VerticalExtent / HorizontalHalf) > 2.0;

    // 062e5732-062e5780: flat: A = LocalPos.x * 0.3f (DAT_063a0788), Inv = 1 / sqrt((z*z +
    // y*y) + A*A) on the FPU, Axial = A * Inv stays unrounded, W.y / W.z = component * float(Inv).
    // Otherwise Axial = W.x.
    double Axial;
    if (bFlatObstacle)
    {
        double const Scaled = static_cast<double>(LocalPos.x) * 0.3f;
        double const Inv = 1.0 / std::sqrt((static_cast<double>(WorkZ) * WorkZ + static_cast<double>(WorkY) * WorkY) +
                                           Scaled * Scaled);
        float const InvF = static_cast<float>(Inv);
        Axial = Scaled * Inv;
        WorkY = static_cast<float>(static_cast<double>(InvF) * WorkY);
        WorkZ = static_cast<float>(static_cast<double>(WorkZ) * InvF);
    }
    else
    {
        Axial = WorkX;
    }

    // 062e5784-062e57b4: tall: B = W.y * 0.3f, Inv = 1 / sqrt((z*z + B*B) + Axial*Axial),
    // Axial *= Inv (unrounded), W.y = B * Inv FSTP float; W.z is not written.
    if (bTallObstacle)
    {
        double const Scaled = static_cast<double>(WorkY) * 0.3f;
        double const Inv = 1.0 / std::sqrt((static_cast<double>(WorkZ) * WorkZ + Scaled * Scaled) + Axial * Axial);
        Axial = Axial * Inv;
        WorkY = static_cast<float>(Inv * Scaled);
    }

    // 062e57b6-062e57f4: the 2D dodge direction (S+0x24): Inv2 = 1 / sqrt(y*y + a*a) of
    // (-W.y, -Axial); x = -Axial * Inv2 (unrounded), y = -W.y * float(Inv2), z =
    // float(Inv2) * 0.0f (DAT_06399408), each FSTP float.
    double const NegAxial = -Axial;
    double const NegY = -static_cast<double>(WorkY);
    double const Inv2 = 1.0 / std::sqrt(NegY * NegY + NegAxial * NegAxial);
    float const Inv2F = static_cast<float>(Inv2);
    Direction.x = static_cast<float>(Inv2 * NegAxial);
    Direction.y = static_cast<float>(NegY * Inv2F);
    Direction.z = static_cast<float>(static_cast<double>(Inv2F) * 0.0f);

    // 062e57f8-062e583b: Rotate (+0x30) by the Elem's transform (slot +0x10) into S+0x18,
    // copied to *Out.
    Vector World;
    Acquire3DMathEngine()->Rotate(reinterpret_cast<FVector&>(World),
                                  reinterpret_cast<FTransform const&>(Elem->Iface->get_transform()),
                                  reinterpret_cast<FVector const&>(Direction));
    *Out = World;
    return Out;
}

// Common.dll.c 0x062e5c00 (FUN_062e5c00), item 1768. PORTED FOR REAL 2026-09-17
// (claude:common-unaff-rederive) from a complete 287/287-instruction hand trace
// of Common.dll.txt:241288-241584 with byte-exact stack-depth tracking. Every
// one of this item's five claimed decompiler corruptions was a false positive --
// see common_aisteer.h for the per-artifact resolution.
//
// Frame map, relative to the entry ESP `E` (SUB 0x80 + 3 pushes => a constant
// working depth of E-0x8c):
//   E+0x04 Iface   E+0x08 Point   E+0x0C AngularSpeed   E+0x10 LinearSpeed
//   E-0x7c DefaultResult (1.0f, written once, read only by the four early-outs)
//   E-0x78 Radius     E-0x74 LenSq / DistToRight
//   E-0x70 Throttle (get_throttle's out-param -- WRITTEN AND NEVER READ)
//   E-0x6c ToPoint (3 floats)            E-0x60 Center (3 floats)
//   E-0x54 TurnOffset (3 floats)         E-0x48 LeftOffset (3 floats)
//   E-0x3c RightOffset (3 floats)        E-0x30 Frame (Transform, 12 dwords)
//   E-0x0c Frame.Translation, overwritten with Center right after the copy
//   E-0x80 scratch (LenSq bit pattern for the invsqrt seed, then TurnRadius)
double EstimateTurnCircleApproachCost(IObjInspect* Iface, Vector const& Point,
                                      float AngularSpeed, float LinearSpeed)
{
    // Re-ported 2026-10-08 (cmn-ai9) from a full trace of 062e5c00-062e600d; S = ESP after
    // the three pushes. The result is left in ST0 unrounded (the MoveIdea caller compares it
    // before its FSTP), hence double.

    // 062e5c12-062e5c29: S+0x10 = 1.0f (the early-exit result), get_throttle (+0x4c) into a
    // 1.0f-seeded S+0x1c that is never read again.
    float const DefaultResult = 1.0f;
    float Throttle = 1.0f;
    Iface->get_throttle(Throttle);

    // 062e5c2c-062e5c42: get_physical_radius (+0x2c) into a zeroed radius (S+0x14) and the
    // centre (S+0x2c).
    float Radius = 0.0f;
    Vector Center;
    Iface->get_physical_radius(Radius, Center);

    // 062e5c45-062e5c8f: Frame (S+0x5c) = the 12-dword transform from slot +0x10 (folded
    // with get_transform), translation replaced by the centre; ToPoint (S+0x20) = Point -
    // centre through FUN_06288800 (float subtracts).
    Transform Frame = Iface->get_transform();
    Frame.Translation = Center;
    Vector ToPoint;
    ToPoint.x = Point.x - Frame.Translation.x;
    ToPoint.y = Point.y - Frame.Translation.y;
    ToPoint.z = Point.z - Frame.Translation.z;

    // 062e5c94-062e5ce6: LenSq = (z*z + y*y) + x*x, FST float (S+0x18), and the inlined fast
    // length of the unrounded sum.
    double const LenSq = (static_cast<double>(ToPoint.z) * ToPoint.z + static_cast<double>(ToPoint.y) * ToPoint.y) +
                         static_cast<double>(ToPoint.x) * ToPoint.x;
    float const LenSqF = static_cast<float>(LenSq);
    double const Dist = InlineFastLength(LenSq);

    // 062e5ce8-062e5d25: MaxRange = Radius * 5.0f on the FPU, replaced by 100.0f unless greater
    // (TEST AH,0x41 + JZ); Dist must be greater than 0.01f (TEST AH,0x41 + JNZ) and less than
    // MaxRange (TEST AH,0x5 + JP), else 1.0f.
    double MaxRange = static_cast<double>(Radius) * 5.0f;
    if (!(MaxRange > 100.0))
        MaxRange = 100.0;
    if (!(Dist > static_cast<double>(0.01f)))
        return DefaultResult;
    if (!(Dist < MaxRange))
        return DefaultResult;

    // 062e5d2b-062e5d55: Inv = 1 / sqrt(float LenSq) kept on the FPU; ToPoint *= Inv, each
    // FSTP float.
    double const Inv = 1.0 / std::sqrt(static_cast<double>(LenSqF));
    ToPoint.x = static_cast<float>(ToPoint.x * Inv);
    ToPoint.y = static_cast<float>(ToPoint.y * Inv);
    ToPoint.z = static_cast<float>(ToPoint.z * Inv);

    // 062e5d57-062e5dac: a speed argument equal to 0.0f (TEST AH,0x44 + JP: only "equal"
    // fetches) is refilled from get_angular_speed_XY(0.0f, 1.0f) (+0xf0) / get_speed(1.0f,
    // engine) (+0xd8) and FSTP over its own argument slot.
    if (AngularSpeed == 0.0f)
        AngularSpeed = Iface->get_angular_speed_XY(0.0f, 1.0f);
    if (LinearSpeed == 0.0f)
        LinearSpeed = Iface->get_speed(1.0f, ThrustEquipEngine);

    // 062e5daf-062e5dcc: TurnRadius = Linear / Angular, FST float (S+0xc); the unrounded
    // quotient must be greater than 0.01f.
    double const TurnRadiusD = static_cast<double>(LinearSpeed) / AngularSpeed;
    float const TurnRadius = static_cast<float>(TurnRadiusD);
    if (!(TurnRadiusD > static_cast<double>(0.01f)))
        return DefaultResult;

    // 062e5dd2-062e5e51: the two turn-circle centres: TransformXform (+0x28) by Frame of
    // (TurnRadius, 0, 0) into S+0x50 and of (-TurnRadius, 0, 0) into S+0x44, the engine
    // created lazily with no null test.
    Vector const OffsetRight{ TurnRadius, 0.0f, 0.0f };
    Vector RightCentre;
    Acquire3DMathEngine()->TransformXform(reinterpret_cast<FVector&>(RightCentre),
                                          reinterpret_cast<FTransform const&>(Frame),
                                          reinterpret_cast<FVector const&>(OffsetRight));
    Vector const OffsetLeft{ -TurnRadius, 0.0f, 0.0f };
    Vector LeftCentre;
    Acquire3DMathEngine()->TransformXform(reinterpret_cast<FVector&>(LeftCentre),
                                          reinterpret_cast<FTransform const&>(Frame),
                                          reinterpret_cast<FVector const&>(OffsetLeft));

    // 062e5e54-062e5ef3: Point - RightCentre (float FSUBs), the fast length of (z*z + y*y) +
    // x*x unrounded, FSTP float (S+0x18).
    Vector ToRight;
    ToRight.x = Point.x - RightCentre.x;
    ToRight.y = Point.y - RightCentre.y;
    ToRight.z = Point.z - RightCentre.z;
    float const DistRight = static_cast<float>(InlineFastLength(
        (static_cast<double>(ToRight.z) * ToRight.z + static_cast<double>(ToRight.y) * ToRight.y) +
        static_cast<double>(ToRight.x) * ToRight.x));

    // 062e5ef5-062e5f48: Point - LeftCentre through FUN_06288800, its fast length left on the
    // FPU unrounded.
    Vector ToLeft;
    ToLeft.x = Point.x - LeftCentre.x;
    ToLeft.y = Point.y - LeftCentre.y;
    ToLeft.z = Point.z - LeftCentre.z;
    double const DistLeft = InlineFastLength(
        (static_cast<double>(ToLeft.z) * ToLeft.z + static_cast<double>(ToLeft.y) * ToLeft.y) +
        static_cast<double>(ToLeft.x) * ToLeft.x);

    // 062e5f4a-062e5f66: the float right distance replaces the left one only when it is
    // smaller (TEST AH,0x5 + JP keeps the left on greater, equal or unordered); the minimum
    // must be less than the float TurnRadius (TEST AH,0x5 + JP), else 1.0f.
    double const MinCentreDist = (static_cast<double>(DistRight) < DistLeft) ? DistRight : DistLeft;
    if (!(MinCentreDist < static_cast<double>(TurnRadius)))
        return DefaultResult;

    // 062e5f6c-062e5f7a: Scaled = MinCentreDist * AngularSpeed, FSTP float (S+0x1c).
    float const Scaled = static_cast<float>(MinCentreDist * AngularSpeed);

    // 062e5f7e-062e5fd8: Forward (S+0x50) = -(Frame column 2); Facing = (T.z*F.z + T.y*F.y) +
    // F.x*T.x FST float, replaced by 0.0f only when less than 0.0f (TEST AH,0x5 + JP).
    Vector Forward;
    Forward.x = -Frame.Rotation.E[0][2];
    Forward.y = -Frame.Rotation.E[1][2];
    Forward.z = -Frame.Rotation.E[2][2];
    double const FacingD = (static_cast<double>(ToPoint.z) * Forward.z + static_cast<double>(ToPoint.y) * Forward.y) +
                           static_cast<double>(Forward.x) * ToPoint.x;
    float Facing = static_cast<float>(FacingD);
    if (FacingD < 0.0)
        Facing = 0.0f;

    // 062e5fd8-062e5ffb: _CIpow(Facing, double 2.0 at DAT_0639c420) * Scaled / LinearSpeed,
    // returned in ST0.
    return std::pow(static_cast<double>(Facing), 2.0) * Scaled / LinearSpeed;
}

// Common.dll.c 0x062e6200 (FUN_062e6200), item 1775. Hand-derived from the raw
// listing (Common.dll.txt:241871-241884, all 13 instructions) -- see
// common_aisteer.h for why this item's `unaff_retaddr` blocker was a false
// positive and how retail's reuse of its own argument slot as the out-param
// buffer produced it.
//
// Retail dereferences Iface's vtable unconditionally only AFTER its own null
// check, so the null guard below is retail's, not an added deviation. Retail
// also does not pre-zero the out slot -- it holds the caller's argument, which
// get_type overwrites on success and (per every other IObjInspect getter)
// leaves alone on failure; the failure path here returns a hard 0 regardless,
// so the port's explicit zero-init is behaviourally identical.
unsigned GetObjectTypeOrZero(IObjInspect const* Iface)
{
    if (Iface == nullptr)
        return 0;

    unsigned Type = 0;
    if (Iface->get_type(Type) != 0) // vtable +0x88, nonzero == failure
        return 0;
    return Type;
}

// Common.dll.c 0x062e7400 (FUN_062e7400), item 1797. Hand-derived from the raw
// listing (Common.dll.txt:243653-243715, all 60 instructions) -- see
// common_aisteer.h for the `unaff_EDI`/collapsed-stack-slot writeup and for
// the identification of `this` as "&NavHelper->ObjDb" rather than a class
// instance.
//
// Calling convention: retail is __thiscall with ONE stack parameter (`RET 0x4`
// at 243708/243715, `MOV EDI,ECX` at 243660). ECX is SystemManager+0x34, the timer
// table, whose first dword is its IObjDB* (`MOV ECX,[EDI]`).
IObjRW* SystemManagerTimerTable::FindObjectDockingAtBase(IObjInspect const* BaseIface) const
{
    if (BaseIface == nullptr)
        return nullptr;

    // 243665 `TEST AL,0x3`: only the low byte is tested, and the mask fits in
    // it, so this is (Type & OBJ_MOON|OBJ_PLANET) != 0.
    if ((GetObjectTypeOrZero(BaseIface) & 0x3u) == 0)
        return nullptr;

    unsigned BaseId = 0;
    BaseIface->get_base(BaseId); // vtable +0x8c; retail ignores the result

    // The 3-dword {cursor, cursor, TypeMask} walk state IObjDB's slot 0 owns
    // and updates in place. Identical layout to ScanList::add_in_range's own
    // FIterState (common_cescanner.cpp) and CAsteroidSystem::update's.
    struct FObjDbIterState
    {
        unsigned Cursor0;
        unsigned Cursor1;
        unsigned TypeMask;
    } IterState = { 0, 0, 0x20u }; // OBJ_DOCKING_RING

    // 243673/243694 `MOV ECX,[EDI]; CALL [EDX]`: IObjDB::GetNext (slot 0).
    IObjDB* const ObjDb = ObjectDb;

    IObjRW* Candidate = ObjDb->GetNext(&IterState);
    while (Candidate != nullptr)
    {
        // Slot +0x90 off the candidate's first vtable pointer is
        // IObjInspect::get_dock_target (IObjInspect is IObjRW's first base).
        unsigned DockTarget = 0; // retail re-zeroes this slot before every call
        Candidate->get_dock_target(DockTarget);
        if (DockTarget == BaseId)
            return Candidate;

        Candidate = ObjDb->GetNext(&IterState);
    }
    return nullptr;
}

// Common.dll.c 0x062e60e0 (FUN_062e60e0). Item 1771. See common_aisteer.h for
// the "unaff_ESI" Ghidra-decompile-corruption note and the full raw-disasm
// derivation. Ported directly from Common.dll.txt:241693-241744.
void GetIfacePositionOrZero(Vector* OutPos, IObjInspect const* Iface)
{
    if (Iface != nullptr)
    {
        // Slot 0x2c is get_physical_radius(float& Radius, Vector& Center), so what this
        // writes into OutPos is the object's CENTRE, not its position -- the function's own
        // name predates the slot being identified. The first out-parameter really is
        // discarded, exactly as retail does.
        float UnusedRadius = 0.0f;
        int const Status = Iface->get_physical_radius(UnusedRadius, *OutPos);
        if (Status == 0)
            return;
        (void)Iface->get_id();
    }
    *OutPos = Vector{ 0.0f, 0.0f, 0.0f };
}
