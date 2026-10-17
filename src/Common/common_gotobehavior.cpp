// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_gotobehavior.h"
#include "common_iface.h"   // IObjInspect / IObjRW, ObjInspectOf, ObjRwFromWatchable
#include "common_csys.h"          // Transform, Matrix, Acquire3DMathEngine
#include "common_systemmanager.h" // SystemManager: CachedPhysicalCenter/Radius, Flags1578, ExternalIface
#include "x86math.h"              // I3DMathEngine::TransformMatrix, FVector, FMatrix
#include "common_pathfind.h"      // PathfindManager::IsCruiseUnobstructed (FUN_062de160)

#include <cmath>
#include <cstdint>
#include <cstring> // std::memcpy -- the GotoOp copy in Slot10 (LAB_062fe670)

// GotoBehavior -- see common_gotobehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// FUN_062fd620 (Common.dll.txt 280834-280915) -- constructor. Calls the
// shared base-init helper FUN_062d2020 first (item 1529 -- NOT the
// REAL field-initialising base ctor: it writes +0x04..+0x29, see Behavior in common_behavior.h), default-constructs a
// MoveIdea, a FaceIdea and a SplineMoveIdea for real (SubObjectA/SubObjectB/
// SubObjectC, all three RTTI-identified, common_idea.h -- SubObjectC's class
// was resolved 2026-09-17 and its former FULLPORT-DEFERRED
// ConstructSubObjectC stand-in is gone), then the two REAL
// already-ported sub-objects (ICRSplineSegment, DirectiveGotoOp) plus the
// small FUN_062c29b0 POD (its leading BaseWatcher modeled for real, see
// Watcher370), then explicitly zeroes/sets every remaining field retail
// touches directly, and finally (implicitly, via this being a real C++
// object) installs the vtable -- retail's own
// `MOV dword ptr [ESI], PTR_FUN_063a2284` needs no manual equivalent.
// (The block above documents GotoBehavior::GotoBehavior, defined just below this base ctor.)

// FUN_062fd620 forwards its own type-id argument to FUN_062d2020 at .text:062fd644.
GotoBehaviorFields::GotoBehaviorFields(int InTypeId)
    : TypeId(InTypeId)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

GotoBehavior::GotoBehavior(int InTypeId)
    : GotoBehaviorFields(InTypeId)
    , SubObjectA()
    , SubObjectB()
    , SubObjectC()
    , Field2d8(0)
    , Field2dc(0)
    , Field2e0(0)
    , Field2e4(0)
    , Flag2e8(0)
    , Flag2e9(1)
    , Flag2ea(0)
    , Flag2eb(0)
    , Flag2ec(0)
    , Pad2ed{}
    , Field2f0(1.5f)
    , Field2f4(3)
    , Field2f8{}    // not written by retail's ctor; value-initialised (Slot10/Update write it first)
    , Field304{}    // 062fd6fe-062fd70a: three zero dwords
    , Watcher310()
    , Spline()
    , Field358(200.0f)
    , Field35c(-1.0f)
    , Field360(1.0f)
    , Flag364(0)    // not written by retail's ctor; Update writes it before any read
    , Flag365(1)
    , Flag366(0)
    , Flag367(0)    // not written by retail's ctor; SetContext/Slot10 set it before any read
    , Flag368(0)
    , Flag369(0)
    , Flag36a(0)
    , Flag36b(0)
    , Flag36c(1)
    , Pad36d{}
    , Watcher370()
    , Field378(0.0f)
    , Field37c(0.0f)
    // Field380/Flags384/SavedFlags388 are the recovered tail of the shared
    // 0x1c-byte proximity-watcher struct (common_gotobehavior.h). The shared
    // ctor FUN_062c29b0 zeroes only the FIRST FOUR DWORDS of it -- Watcher370's
    // two words plus Field378/Field37c -- and stops. Retail therefore leaves
    // these three uninitialized at construction, exactly as DockBehavior's own
    // ctor does at +0x460/+0x464/+0x468. They are zeroed here anyway: reading
    // an uninitialized member is UB in C++ (unlike x86, which just reads
    // whatever was in the block), and GotoBehavior::Update DOES read Flags384
    // (.text:062fe1c4) on a path reachable before FUN_062c2af0 has necessarily
    // written it. Deliberate, documented divergence from retail, not an
    // oversight.
    , Field380(0.0f)
    , Flags384(0)
    , SavedFlags388(0)
    , GotoOp()
{
    // Retail default-constructs SubObjectA/SubObjectB/SubObjectC here via
    // FUN_062d4e60/FUN_062d6750/FUN_062d9bf0 (ECX = &SubObjectA/&SubObjectB/
    // &SubObjectC) -- ALL THREE are now real MoveIdea()/FaceIdea()/
    // SplineMoveIdea() constructor calls via the member-initializer list above
    // (common_idea.h). The FUN_062d9bf0 gap closed 2026-09-17 when that ctor's
    // class was RTTI-identified as SplineMoveIdea; nothing is deferred here
    // any more.

    // Every other field write above (Field2d8.. through Flag36c, plus the
    // Watcher310/Watcher370 zero-init and the Field2f0/Field358/Field35c/
    // Field360/Flag365/Flag36c non-zero defaults) is retail's own explicit
    // ctor body, fully reproduced via the member-initializer list -- nothing
    // further to do here.
}

// FUN_062fd7a0 (Common.dll.txt 280941-281018) -- the real (non-thunk)
// destructor; FUN_062fd780 (item 1989, MSVC's scalar-deleting-destructor
// boilerplate) just calls this then conditionally frees the block.
GotoBehavior::~GotoBehavior()
{
    // Retail unwatches six BaseWatcher-shaped pointer fields via
    // BaseWatcher::set_pointer(nullptr), in this exact order: this+0x370,
    // this+0x310, this+0x204, this+0x18c, this+0xb8, this+0x40. ALL SIX are
    // now named and cleared for real as of 2026-09-17, since SubObjectC is a
    // real SplineMoveIdea (common_idea.h) rather than an opaque buffer:
    //   this+0x204 == SubObjectC(+0x164) + 0xa0 == SubObjectC.Face.Watcher
    //   this+0x18c == SubObjectC(+0x164) + 0x28 == SubObjectC.Move.Watcher
    //   this+0xb8  == SubObjectB(+0xa4)  + 0x14 == SubObjectB.Watcher
    //   this+0x40  == SubObjectA(+0x2c)  + 0x14 == SubObjectA.Watcher
    // The two SubObjectC offsets landing exactly on its embedded MoveIdea's
    // and FaceIdea's own Watcher members is independent structural
    // confirmation of SplineMoveIdea's layout, not an assumption.
    Watcher370.set(nullptr);
    Watcher310.set(nullptr);
    SubObjectC.Face.Watcher.set(nullptr);
    SubObjectC.Move.Watcher.set(nullptr);
    SubObjectB.Watcher.set(nullptr);
    SubObjectA.Watcher.set(nullptr);

    // 062fd7a0's tail FUN_062d2080 is ~Behavior, which the compiler chains after this body.
    // (The empty static ReleaseSharedState stand-in that stood here is gone, 2026-10-08.)
}

// ----------------------------------------------------------------------------
// Every own slot of GotoBehavior's vtable is a real override as of 2026-10-08
// (claude-code:cmn-ai5): see the slot list in common_gotobehavior.h. The extern "C" free
// functions that used to stand in for slots 0/1/3/4/5/6/12/14 over a raw `unsigned char*`
// (FUN_062fd780 / FUN_062fd7a0 / LAB_062fd890 / FUN_062fd920 / FUN_062fd9c0 / LAB_062fda20 /
// LAB_062fda60 / FUN_062fe970 / LAB_062feb00) are gone -- nothing dispatched to them.
// ----------------------------------------------------------------------------

// (The FULLPORT-DEFERRED `ConstructSubObjectC` stand-in that used to live here
// is gone as of 2026-09-17: FUN_062d9bf0's class is RTTI-identified as
// SplineMoveIdea, so all three sub-objects now construct for real from the
// ctor's member-initializer list. DockBehavior/LootBehavior/LaunchBehavior
// carry a stand-in of the same NAME, but for a DIFFERENT retail helper
// (FUN_062da500 == AfterburnerIdea) -- those are a separate, still-open gap.)


// ============================================================================
// The shared 0x1c-byte proximity-watcher struct -- decomp-tracker items
// 1257 / 1258 / 1259, PORTED IN FULL 2026-09-17 (claude:common-attack-goto).
//
// All three were on record as "DO-NOT-TOUCH DockBehavior vtable cluster", then
// (2026-09-17) as "untranslated FP geometry leaning on the still-unprocessed
// FUN_062e4ea0 / FUN_062e4bb0". Both reasons are dead:
//   * the DO-NOT-TOUCH scope was lifted 2026-09-11 and the enclosing address
//     range retired 2026-09-17 (item 1896's census);
//   * FUN_062e4bb0 is item 1749, GetInterceptClearanceOfIface, and
//     FUN_062e4ea0 is item 1751, GetSignedRadialClearanceToPoint -- BOTH
//     ready_unverified (now in common_aiintercept.cpp) since 2026-09-17. Checked, not
//     assumed: `list_items` over 0x062e4b00-0x062e5000 returns both with a
//     ported_file.
// What was left was plain translation, done here from the raw listing
// (EXE/Disasm/Common.dll.txt 186949-187298); Ghidra's Common.dll.c was not
// used. Every FPU comparison below is annotated with the exact FNSTSW/TEST/Jcc
// pair it came from, because the polarity is not recoverable from the
// decompilation.
//
// TWO SLOT IDENTIFICATIONS that make these readable, both from the retail
// IObjInspect table rebuilt in common_iface.h:
//   vtable +0x00 == get_position()          (slot 0)   -- FUN_062c2af0
//   vtable +0x60 == is_cruise_active(bool&) (slot 24)  -- FUN_062c2af0
//   vtable +0x20 == get_id()                (slot 8)   -- FUN_062c2d90
// The +0x60 call is why the struct's +0x08/+0x0c thresholds get multiplied by
// its +0x10 field on some ticks and not others: +0x10 is a cruise multiplier,
// applied exactly while the watched object reports cruise engaged.
// ============================================================================

// FUN_062c29f0 (.text:062c29f0-062c2aa5, RET 0x14, item 1254). Ported 2026-10-08
// (claude-code:cmn-ai5); its "blocked on the qhull cluster" note was stale -- the struct is
// the shared proximity watcher and its only caller is GotoBehavior::Slot10.
void ProxWatcherInit(unsigned char* Watcher, void* NavHelper, void* Target, float Near, float Far,
                     float CruiseRatio)
{
    BaseWatcher* const W = reinterpret_cast<BaseWatcher*>(Watcher);
    *reinterpret_cast<float*>(Watcher + 0x08) = Near;               // 062c29fb
    *reinterpret_cast<float*>(Watcher + 0x0c) = Far;                // 062c2a05
    // 062c29fe-062c2a18: Target + 8 (or 0), set_pointer only when it differs.
    IObjRW* const TargetObj = static_cast<IObjRW*>(Target);
    W->set(TargetObj != nullptr ? static_cast<Watchable const*>(TargetObj) : nullptr);
    *reinterpret_cast<unsigned*>(Watcher + 0x14) = 0;               // 062c2a21
    *reinterpret_cast<unsigned*>(Watcher + 0x18) = 0;               // 062c2a28
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(W->target()));
    if (Watched == nullptr)
        return;                                                     // 062c2a2f / 062c2a36
    float* const Ratio = reinterpret_cast<float*>(Watcher + 0x10);
    // 062c2a38-062c2a47: CruiseRatio against double 0.0 (DAT_0639c430); TEST AH,0x41 + JNZ ->
    // not-greater or NaN takes the speed quotient.
    if (CruiseRatio > 0.0)
    {
        *Ratio = CruiseRatio;                                       // 062c2a4d
    }
    else
    {
        // LAB_062c2a52: own ship (NavHelper's +0x08 Watcher target - 8) get_speed(1.0f, 4),
        // FSTP float; FDIVR by the watched object's get_speed(1.0f, 1); FSTP float.
        SystemManager* const Sys = static_cast<SystemManager*>(NavHelper);
        IObjRW* const Self = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
        float const OwnSpeed = Behavior_InvokeManagerSlotD8(
            ObjInspectOf(Self), 1.0f, IObject::ThrustEquipCruise);
        double const WatchedSpeed = Behavior_InvokeManagerSlotD8(
            ObjInspectOf(Watched), 1.0f, IObject::ThrustEquipEngine);
        *Ratio = static_cast<float>(OwnSpeed / WatchedSpeed);
    }
    // LAB_062c2a8c: TEST AH,0x5 + JP keeps >= 1.0f or unordered; strictly less -> 1.0f.
    if (*Ratio < 1.0f)
        *Ratio = 1.0f;
}

// FUN_062c2af0 (Common.dll.txt 186949-187101, 135/135 insns, item 1257).
void ProxWatcherRecomputeZoneFlags(unsigned char* Watcher, void* NavHelper, Vector const& RefPoint)
{
    // .text:062c2af6-062c2b05. Retail's second guard (`LEA ECX,[EAX-8]; TEST
    // ECX,ECX; JZ`) can only fire if the watched pointer is literally 8;
    // ObjRwFromWatchable's null return covers the first and, in practice, both.
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return;

    unsigned* const ZoneFlags = reinterpret_cast<unsigned*>(Watcher + 0x14);
    unsigned* const SavedFlags = reinterpret_cast<unsigned*>(Watcher + 0x18);

    // .text:062c2b0b-062c2b0e -- snapshot last tick's zones first; the
    // hysteresis bands below OR that snapshot back in.
    *SavedFlags = *ZoneFlags;

    // .text:062c2b11-062c2b42. Slot 0 is IObjInspect::get_position().
    Vector const& Pos = ObjInspectOf(Obj)->get_position();
    float const Dx = Pos.x - RefPoint.x;
    float const Dy = Pos.y - RefPoint.y;
    float const Dz = Pos.z - RefPoint.z;

    FAiTarget* const SteerElem = &((SystemManager*)NavHelper)->SelfTarget;
    // The ST0 return stays on the x87 stack until the FCOMPP below.
    double const RadialClearance = GetSignedRadialClearanceToPoint(SteerElem, RefPoint); // FUN_062e4ea0, item 1751

    // .text:062c2b47-062c2b7a. Retail accumulates (z*z + x*x) + y*y from the
    // float-stored differences, then FSQRT, all unrounded, then `FXCH;
    // FCOMPP` -- so the comparison is RadialClearance against Distance, and
    // `TEST AH,0x41 / JNZ` SKIPS the write when C0|C3 is set, i.e. when
    // RadialClearance <= Distance or the compare is unordered.
    *ZoneFlags = 0;
    double const Distance = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dx) * Dx) +
                                      static_cast<double>(Dy) * Dy);
    if (RadialClearance > Distance)
        *ZoneFlags = 1;

    // .text:062c2b81-062c2b95. Retail re-reads the watcher slot before each of
    // the next two calls (separately-inlined accessors) and re-guards neither:
    // a null head gives a null receiver, as here.
    IObjRW* const ClearanceObj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    // 062c2b95: `FSTP float` (into the RefPoint argument's home slot) rounds the ST0 return.
    float const InterceptClearance = static_cast<float>(GetInterceptClearanceOfIface(SteerElem, ClearanceObj)); // FUN_062e4bb0, item 1749

    // .text:062c2b99-062c2bb3. Slot +0x60 is is_cruise_active(bool&).
    IObjRW* const CruiseObj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    bool bCruiseActive = false;
    ObjInspectOf(CruiseObj)->is_cruise_active(bCruiseActive);

    // Each limit is re-derived on the x87 stack before each compare and never stored:
    // FLD [+0x08/+0x0c], FMUL [+0x10] while cruising -- unrounded.
    float const CruiseScale = *reinterpret_cast<float*>(Watcher + 0x10);
    double const NearLimit = bCruiseActive
        ? static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x08)) * CruiseScale
        : static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x08));
    double const FarLimit = bCruiseActive
        ? static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x0c)) * CruiseScale
        : static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x0c));

    // .rdata:DAT_0639e6d4 == 0x42480000 == 50.0f -- the half-width of the
    // hysteresis band around each limit (FSUBR / FADD on the stack).
    double const HysteresisBand = 50.0;
    double const Clearance = InterceptClearance;

    // .text:062c2bc2-062c2c0a. Each compare is `FCOMPP; FNSTSW AX; TEST
    // AH,0x41; JP <skip>`, so the FALLTHROUGH is "clearance <= limit" and the
    // jump covers greater-or-unordered. Inside the band the previous tick's
    // zone bits are OR'd back in, which is what makes this hysteresis rather
    // than a plain threshold.
    if (Clearance <= NearLimit - HysteresisBand)
        *ZoneFlags |= 2;
    else if (Clearance <= NearLimit + HysteresisBand)
        *ZoneFlags |= *SavedFlags;

    // .text:062c2c0d-062c2c69. Same shape on the far limit -- note retail
    // RETURNS immediately after setting bit 0x4 rather than falling into the
    // band test.
    if (Clearance <= FarLimit - HysteresisBand)
    {
        *ZoneFlags |= 4;
        return;
    }
    if (Clearance <= FarLimit + HysteresisBand)
        *ZoneFlags |= *SavedFlags;
}

// FUN_062c2c70 (Common.dll.txt 187107-187226, 99/99 insns, item 1258).
double ProxWatcherComputeSpeedCap(unsigned char* Watcher, void* OwnerContext, void* NavHelper,
                                  int Selector, float SelectorValue)
{
    // .text:062c2c71-062c2c8d. `local_4` is pre-loaded with
    // .rdata:DAT_06399408 (0x00000000 == 0.0f) purely so the two early-return
    // paths have something on the FPU stack.
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return 0.0f;

    ThrustEquipType const ThrustType = static_cast<ThrustEquipType>(Selector);

    // .text:062c2c96-062c2cc1. Retail reuses the param_3 home slot as float
    // scratch here (which is why Ghidra's decompile of this function is a
    // mess): the Value argument is SelectorValue when Selector == 1 and the
    // literal 1.0f otherwise, while the Selector passed on is the ORIGINAL
    // integer, still live in EDI.
    float const QueryValue = (Selector == 1) ? SelectorValue : 1.0f;
    // 062c2cc1: FSTP float into the SelectorValue argument's home slot.
    float const OwnerSpeed =
        Behavior_InvokeManagerSlotD8(ObjInspectOf(OwnerContext), QueryValue, ThrustType); // FUN_062e5af0, item 1762

    unsigned const ZoneFlags = *reinterpret_cast<unsigned*>(Watcher + 0x14);
    double ZoneSpeed;
    if ((ZoneFlags & 3) != 0)
    {
        // .text:062c2d47-062c2d72. Two byte-identical arms MSVC failed to CSE
        // (one re-reads the watcher, one passes a hard zero); collapsed,
        // because Behavior_InvokeManagerSlotD8 itself returns 0.0f on a null
        // receiver -- item 1762's own confirmed null-fallback.
        ZoneSpeed = Behavior_InvokeManagerSlotD8(
            ObjInspectOf(ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00))), 1.0f, ThrustType);
    }
    else if ((ZoneFlags & 4) == 0)
    {
        // LAB_062c2d74 -- no zone at all, so the cap is the pre-loaded 0.0f.
        ZoneSpeed = 0.0f;
    }
    else
    {
        // .text:062c2cd7-062c2d45. Linear blend of the clearance across the
        // struct's own two thresholds -- UNSCALED here, unlike
        // ProxWatcherRecomputeZoneFlags, which cruise-scales the same pair.
        float const NearThreshold = *reinterpret_cast<float*>(Watcher + 0x08);
        float const FarThreshold = *reinterpret_cast<float*>(Watcher + 0x0c);
        FAiTarget* const SteerElem = &((SystemManager*)NavHelper)->SelfTarget;
        // 062c2cf4-062c2d0c: FSUB / FLD / FSUB / FDIVP / FSUBR all run on the x87
        // stack straight off the ST0 return; the only rounding is the final
        // `FSTP float` of Blend. Hence double for the clearance and the
        // threshold difference.
        double const Clearance = GetInterceptClearanceOfIface(SteerElem, Obj); // FUN_062e4bb0, item 1749
        float const Blend = static_cast<float>(
            1.0 - ((Clearance - NearThreshold) / (static_cast<double>(FarThreshold) - NearThreshold))); // FSUBR against DAT_0639a1d0 (1.0f)

        // Retail multiplies the full-throttle speed by Blend TWICE, at
        // .text:062c2d25 and .text:062c2d2c -- both reads resolve to the same
        // stack slot (ESP+0x24 before `ADD ESP,0xc`, ESP+0x18 after). This is
        // a square, not a transcription slip; re-checked against the raw bytes.
        // Both FMULs run on the stack (no rounding until the caller's store).
        float const FullSpeed = Behavior_InvokeManagerSlotD8(
            ObjInspectOf(ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00))), 1.0f, ThrustType);
        ZoneSpeed = (static_cast<double>(FullSpeed) * Blend) * Blend;

        // .text:062c2d30-062c2d45: `FCOM DAT_0639e884; FNSTSW AX; TEST AH,0x5;
        // JP` -- the jump KEEPS the value and is taken on greater-or-equal or
        // unordered, so only a strictly-below-10.0f result is floored to zero.
        // DAT_0639e884 == 0x41200000 == 10.0f (hand-decoded, listing line
        // 561713; it sits immediately before the " AUHH index not 0" string).
        if (ZoneSpeed < 10.0)
            ZoneSpeed = 0.0;
    }

    // .text:062c2d78-062c2d86: `FCOM <OwnerSpeed>; TEST AH,0x41; JNZ` returns
    // the zone speed (still in ST0) when C0|C3 is set (zone <= owner, or
    // unordered) and the owner's rounded speed otherwise. Written as `!(a > b)`
    // rather than `a <= b` so the NaN case keeps retail's answer.
    return !(ZoneSpeed > static_cast<double>(OwnerSpeed)) ? ZoneSpeed : static_cast<double>(OwnerSpeed);
}

// FUN_062c2d90 (Common.dll.txt 187229-187298, 60/60 insns, item 1259).
void ProxWatcherNotifyZoneChange(unsigned char* Watcher, void* NavHelper, void* BehaviorMgr,
                                 Vector const& RefPoint)
{
    // .text:062c2d90-062c2da1
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return;

    // .text:062c2da7-062c2e19. Three recognised transitions between last
    // tick's snapshot (+0x18) and this tick's zones (+0x14); anything else
    // returns without notifying. Retail's third arm is reached by falling out
    // of the first, which is why the (Saved & 4) test appears twice.
    unsigned const Saved = *reinterpret_cast<unsigned*>(Watcher + 0x18);
    unsigned const ZoneFlags = *reinterpret_cast<unsigned*>(Watcher + 0x14);

    int Code;
    if ((Saved & 4) != 0 && (Saved & 2) == 0 && (ZoneFlags & 2) != 0)
        Code = 1;
    else if ((Saved & 4) == 0 && (ZoneFlags & 4) != 0)
        Code = 2;
    else if ((Saved & 4) != 0 && (ZoneFlags & 4) == 0)
        Code = 3;
    else
        return;

    // .text:062c2dbb-062c2dc9 (and its two verbatim copies). Slot +0x20 is
    // IObjInspect::get_id(); `NavHelper + 0x1580` is SelfTarget.Iface, the own ship.
    unsigned const OwnerId = ((SystemManager*)NavHelper)->SelfTarget.Iface->get_id();

    // .text:062c2e1b-062c2e1f: the same slot on the watched object itself.
    unsigned const WatchedId = ObjInspectOf(Obj)->get_id();

    // .text:062c2e22-062c2e28. The CALL at .text:062c2e1f does NOT clean the
    // two dwords pushed just before it -- that is what proves those two are
    // FUN_062d2a10's arguments and that slot +0x20 takes none. FUN_062d2a10 is
    // IBehaviorManager::NotifyTetherEvent (common_behaviormanager.cpp); the result is dropped.
    (void)static_cast<IBehaviorManager*>(BehaviorMgr)->NotifyTetherEvent(
        OwnerId, WatchedId, static_cast<pub::AI::ContentCallback::TetherStatusType>(Code), RefPoint);
}

// ============================================================================
// FUN_062ebf50 -- ComputeFormationFollowerEnvelope, decomp-tracker item 1855.
// 471/471 instructions, Common.dll.txt 251151-251678. PORTED IN FULL
// 2026-09-17 (claude:common-goto), from the raw listing only; Ghidra's
// Common.dll.c body for this function is the `unaff_EBX`/`unaff_retaddr` mess
// that got it filed as `blocked` in the first place and was not consulted.
// See common_gotobehavior.h for the call shape, the receiver's identity, the
// out-struct field table, and the point-by-point retirement of the three
// stale blocker claims.
//
// FRAME MAP (needed to read the annotations below, and the whole reason the
// decompile is unusable). On entry ESP == E, [E] = return address, [E+4] = the
// single `Envelope*` argument. `SUB ESP,0xa4` then `PUSH EBX/EBP/ESI` puts the
// frame base at B == E-0xb0, so every `[ESP+k]` in the listing is B+(k-adjust)
// where `adjust` is however many argument dwords are live at that instruction.
// Resolved slots, all verified against every read AND every write:
//   B+0x0f  bool   bCruise            B+0x10  float MinFollowerSpeed
//   B+0x14  int    ThrustEquipType    B+0x18  float scratch (|Delta|, then Speed)
//   B+0x1c  float  MinFollowerAngularSpeed        B+0x20  float MaxProjection
//   B+0x24  float  MinAlignCos        B+0x28  Vector Delta
//   B+0x34  IObjRW** follower cursor  B+0x38  unsigned remaining count
//   B+0x3c  float  MaxDistance (WRITE-ONLY, see below)
//   B+0x40  Transform const* hoisted owner transform
//   B+0x44  Vector DesiredPos         B+0x50  Vector RotatedOffset
//   B+0x5c  Vector OwnerBack          B+0x74  Vector FormationOffset
//   B+0x80  Vector OwnerFacing        B+0x8c  Vector FollowerPos
//   B+0x98  Vector DeltaDir           B+0xa4  Vector FollowerFacing
// MSVC reuses three of them after their last read: B+0x10 becomes the angular
// target at .text:062ec3c0, B+0x14 becomes the acos() angle at .text:062ec3af,
// and B+0x18 switches from the per-follower distance to the final speed at
// .text:062ec354. That reuse is exactly what Ghidra could not follow.
//
// Every x87 comparison below carries the FNSTSW/TEST/Jcc it came from, because
// the polarity is NOT recoverable from the decompilation:
//   `TEST AH,0x05; JP <skip>`  -> the jump is taken on "NOT less" (and on
//                                 unordered), so the fall-through is `a < b`.
//   `TEST AH,0x41; JNZ <skip>` -> the jump is taken on C0|C3, i.e. `a <= b` or
//                                 unordered, so the fall-through is `a > b`.
// Written as plain `<` / `>` on purpose: C++ gives false for either on a NaN
// operand, which is the same branch retail takes.
//
// RETAIL CONSTANTS, all hand-decoded from the .rdata bytes (Ghidra prints no
// value for any of them):
//   DAT_0639fe30 = 0x43960000 = 300.0f   non-cruise projection divisor
//   DAT_063a0608 = 0x447a0000 = 1000.0f  cruise projection divisor
//   DAT_063a0800 = 0xc1a00000 = -20.0f   "formation has any cohesion" gate
//   DAT_0639b21c = 0x41a00000 = 20.0f    squared-blend / tightness threshold
//   DAT_0639e6b8 = 0x3c23d70a = 0.01f    blend floor-to-zero epsilon
//   DAT_06399408 = 0x00000000 = 0.0f     the family's null-receiver fallback
//   DAT_0639df88 = 0x3fe0000000000000 = 0.5  (double) speed/angular floor
//   DAT_0639fa50 = 0x3fc999999999999a = 0.2  (double) wide-angle scale
//   DAT_063a07f8 = 0x3fefd70a3d70a3d7 = 0.99 (double) alignment threshold
// The three doubles are kept as double literals rather than float ones: retail
// really does widen the float operand and compare/multiply in the wider type,
// and 0.2 and 0.99 are not representable in float.
// ============================================================================

namespace
{
    // The receiver's +0x08 back-pointer to its owning SystemManager, then that
    // SystemManager's own +0x08 BaseWatcher, then the standard `-8` Watchable
    // downcast. Retail re-walks this chain from scratch at all seven sites that
    // need the owner (.text:062ebf5a, 062ebfa4, 062ebfcb, 062ec010, 062ec37d,
    // 062ec442, 062ec494), so it is a function here rather than a hoisted
    // local. Retail's second guard at .text:062ebf69-062ebf6e (`ADD EAX,-8;
    // TEST EAX,EAX; JZ`) can only fire when the Watchable pointer is literally
    // 8; ObjRwFromWatchable's null return covers the reachable case, and the
    // two checks are collapsed here exactly as item 1856's port collapsed the
    // identical triple-check.
    IObjRW* OwnerObjOf(SystemManager* Sys)
    {
        return ObjRwFromWatchable(*reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(Sys) + 0x8));
    }

    // .text:062ec05c-062ec083, repeated verbatim at .text:062ec154-062ec17b.
    // SystemManager's once-per-tick cache of
    // `ExternalIface->get_physical_radius(float& Radius, Vector& Center)`
    // (IObjInspect slot +0x2C), latched by Flags1578[2] (== +0x157a, spelled
    // `[Sys+0x14fc + 0x7e]` in the listing). Argument order is (Radius,
    // Center): retail pushes `&[Elem+0xc]` first and `&[Elem+0x18]` second, so
    // Elem+0x18 == SystemManager+0x1514 == CachedPhysicalRadius is the FIRST
    // argument -- the same ordering common_aisteer.cpp's
    // RefreshCachedIfacePosition had to be corrected to on 2026-09-16.
    //
    // Retail sets the latch and then dereferences ExternalIface with no null
    // check; both are reproduced as written.
    void RefreshCachedPhysicalExtents(SystemManager* Sys)
    {
        if (Sys->SelfTarget.bPosValid != 0)
            return;
        Sys->SelfTarget.bPosValid = 1;
        Sys->SelfTarget.Iface
            ->get_physical_radius(Sys->SelfTarget.CachedRadius, Sys->SelfTarget.CachedPos);
    }
}

bool FGunConvergenceStateBlock::ComputeFormationFollowerEnvelope(FormationFollowerEnvelope* Out)
{
    // .text:062ebf5a-062ebf6e
    SystemManager* const Sys = Owner;
    if (OwnerObjOf(Sys) == nullptr)
        return false;

    // .text:062ebf74-062ebf9d. Slot +0x60 is the one-argument
    // is_cruise_active(bool&) overload, not the (bool&,bool&) one at +0x5C.
    bool bCruise = false;
    ObjInspectOf(OwnerObjOf(Sys))->is_cruise_active(bCruise);

    // .text:062ebf7e -- B+0x1c is pre-seeded with 0x7f7fffff (FLT_MAX) so the
    // per-follower minimum below has a sentinel; it is NOT a float constant
    // from .rdata, it is an immediate store.
    float MinFollowerAngularSpeed = 3.402823466e+38f; // 0x7f7fffff

    // .text:062ebfa4-062ebfe8. The two arms differ only in the selector they
    // pass and latch into B+0x14: 1 == ThrustEquipEngine when not cruising,
    // 4 == ThrustEquipCruise when cruising (common_cequip.h's IObject enum).
    ThrustEquipType const ThrustType = bCruise ? ThrustEquipCruise : ThrustEquipEngine;
    float MinFollowerSpeed =
        Behavior_InvokeManagerSlotD8(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, ThrustType); // FUN_062e5af0, item 1762

    // .text:062ebff0-062ec00c. Retail populates four of the five out fields
    // HERE, before the follower-count early-out below -- so a `false` return
    // from the count check still leaves the caller with this partial result.
    // That ordering is load-bearing and is reproduced exactly.
    Out->Speed = MinFollowerSpeed;
    Out->Throttle = 1.0f;
    Out->AngularThrottle = 1.0f;
    Out->Flag10 = 1;

    // .text:062ec010-062ec04d. `cobject()` is IObjInspect slot +0x150 (84).
    // The 0x503 mask is this codebase's established "is really a CShip" test
    // (common_iobjinspectimpl.cpp's own bitmask table). Retail then calls
    // CShip::get_formation_followers NON-virtually with whatever EAX holds --
    // 0 on a mask miss (.text:062ec043) -- and the port does the same.
    CObject const* const Obj = ObjInspectOf(OwnerObjOf(Sys))->cobject();
    CShip* const Ship = (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
        ? static_cast<CShip*>(const_cast<CObject*>(Obj))
        : nullptr;

    // +0x1c..+0x9c is the receiver's own embedded 32-entry scratch array -- the same one
    // item 1856's port fills.
    IObjRW** const Followers = FollowerScratch;
    unsigned const Count = static_cast<unsigned>(Ship->get_formation_followers(Followers, 0x20));

    // .text:062ec052-062ec056: `TEST ESI,ESI; JBE` -- after a TEST, CF is
    // always clear, so JBE degenerates to JZ. Only an exactly-zero count is
    // rejected, which is why the count is modelled unsigned here (the same
    // retail quirk item 1856's port preserved).
    if (Count == 0)
        return false;

    // .text:062ec05c-062ec083
    RefreshCachedPhysicalExtents(Sys);

    // .text:062ec086-062ec09b. Slot +0x0C is get_transform(); retail calls it
    // ONCE here and keeps the returned pointer in B+0x40 for the whole loop,
    // re-reading through it each iteration. Reproduced as a hoisted pointer,
    // not as a copy, so a mutation mid-loop would be seen the same way.
    IObjInspect const* const Ext = Sys->SelfTarget.Iface;
    Transform const* const HoistedXform = &Ext->get_transform();

    // .text:062ec09f-062ec0c0. FUN_062e6150 (item 1772).
    Vector OwnerFacing = { 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&OwnerFacing, OwnerObjOf(Sys));

    // .text:062ec0c5-062ec0d5. -10000.0f (0xc61c4000) twice, 1.0f once.
    float MaxProjection = -10000.0f;
    float MaxDistance = -10000.0f;
    float MinAlignCos = 1.0f;

    // .text:062ec0f0-062ec2ec
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        IObjRW* const Follower = Followers[Index];
        if (Follower == nullptr)
            continue; // .text:062ec0f2-062ec0f4 jumps straight to the increment

        // .text:062ec0fa-062ec11d. Slot +0xD8 == get_speed(Throttle, Type);
        // the pushes are (0x3f800000, selector), so Throttle is the 1.0f.
        // `FCOM; TEST AH,0x5; JP` keeps the smaller: a running MINIMUM seeded
        // from the owner's own speed above.
        float const FollowerSpeed = ObjInspectOf(Follower)->get_speed(1.0f, ThrustType);
        if (FollowerSpeed < MinFollowerSpeed)
            MinFollowerSpeed = FollowerSpeed;

        // .text:062ec121-062ec143. Slot +0xF0 == get_angular_speed_XY(Speed,
        // MaxSpeed), inlined here rather than routed through FUN_062e5b20
        // (retail devirtualises it because the null case cannot happen inside
        // the loop). Pushes are (0x0, 0x3f800000) -> (1.0f, 0.0f). Another
        // running minimum, against the FLT_MAX sentinel.
        float const FollowerAngularSpeed = ObjInspectOf(Follower)->get_angular_speed_XY(1.0f, 0.0f);
        if (FollowerAngularSpeed < MinFollowerAngularSpeed)
            MinFollowerAngularSpeed = FollowerAngularSpeed;

        // .text:062ec145-062ec14e. Slot +0xAC == get_formation_offset(Vector&)
        // -- the follower's OWN desired station, in the leader's local frame.
        Vector FormationOffset = { 0.0f, 0.0f, 0.0f };
        ObjInspectOf(Follower)->get_formation_offset(FormationOffset);

        // .text:062ec154-062ec17b -- the same cache refresh again, per follower.
        RefreshCachedPhysicalExtents(Sys);

        // .text:062ec17e-062ec1a3. DAT_063fca58 / FUN_06391cf0 is the shared
        // 3DMathEngine singleton (Acquire3DMathEngine, common_csys.h); slot
        // +0x2C is TransformMatrix(FVector& Dst, FMatrix const&, FVector const&)
        // per src/include/x86math.h. Retail hands it the Transform pointer
        // where an FMatrix is expected, which is exactly "rotate by the
        // orientation, ignore the translation" -- a Transform opens with its
        // Matrix. The `this` pointer is pushed as the first stack argument
        // (PUSH EAX last), the DACOM calling convention.
        Vector RotatedOffset;
        Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(RotatedOffset),
                                               reinterpret_cast<FMatrix const&>(HoistedXform->Rotation),
                                               reinterpret_cast<FVector const&>(FormationOffset));

        // .text:062ec1a6-062ec1cc. The three FADDs are against
        // [SteerElem+0xc/+0x10/+0x14] == SystemManager+0x1508..+0x1510 ==
        // CachedPhysicalCenter, the pair the refresh above maintains.
        Vector const DesiredPos = { RotatedOffset.x + Sys->SelfTarget.CachedPos.x,
                                    RotatedOffset.y + Sys->SelfTarget.CachedPos.y,
                                    RotatedOffset.z + Sys->SelfTarget.CachedPos.z };

        // .text:062ec1d0-062ec1fc. FUN_062e60e0 (item 1771) returns its own
        // out-pointer in EAX, which is what the three FSUBs dereference.
        Vector FollowerPos = { 0.0f, 0.0f, 0.0f };
        GetIfacePositionOrZero(&FollowerPos, Follower);
        Vector const Delta = { DesiredPos.x - FollowerPos.x,
                               DesiredPos.y - FollowerPos.y,
                               DesiredPos.z - FollowerPos.z };

        // .text:062ec200-062ec243. A SECOND, fresh get_transform() call (retail
        // does not reuse B+0x40 here), then the NEGATED third column
        // [+0x08, +0x14, +0x20] == E[0][2], E[1][2], E[2][2] -- the owner's
        // backward axis. Three separate FCHS instructions, one per component.
        Matrix const& R = Ext->get_transform().Rotation;
        Vector const OwnerBack = { -R.E[0][2], -R.E[1][2], -R.E[2][2] };

        // .text:062ec247-062ec27a. FUN_062e1680 (item 1728) == VecNormalize3,
        // whose port spells its parameters (Src, Out) where retail pushes
        // (Out, Src); FUN_062e1630 (item 1727) == ClampedDot3. Retail leaves
        // &OwnerBack on the stack from the normalize call and reuses that slot
        // as ClampedDot3's second argument -- the `ADD ESP,0x8` at
        // .text:062ec24c cleans only two of the three pushed dwords, which is
        // the proof that VecNormalize3 takes two arguments and not three.
        Vector DeltaDir;
        VecNormalize3(&Delta, &DeltaDir);
        double const Cosine = ClampedDot3(DeltaDir, OwnerBack);

        // .text:062ec255-062ec27a: (z*z + y*y) + x*x, FSQRT, FST to [B+0x18] (the
        // rounded distance), FMULP with the unrounded cosine -- the projection
        // stays on the stack.
        double const Distance = std::sqrt((static_cast<double>(Delta.z) * Delta.z +
                                           static_cast<double>(Delta.y) * Delta.y) +
                                          static_cast<double>(Delta.x) * Delta.x);
        float const DistanceRounded = static_cast<float>(Distance);
        double const Projection = Cosine * Distance;

        // .text:062ec27c-062ec28b: FCOM the unrounded projection; `TEST AH,0x41;
        // JNZ` skips the store unless strictly greater -- a running MAXIMUM of the
        // signed longitudinal offset (positive == the follower is behind its station).
        if (Projection > static_cast<double>(MaxProjection))
            MaxProjection = static_cast<float>(Projection);

        // .text:062ec28f-062ec2a2, same polarity, on the rounded distance.
        if (DistanceRounded > MaxDistance)
            MaxDistance = DistanceRounded;

        // .text:062ec2a6-062ec2d4. FUN_062e6150 again, this time on the
        // follower, then FUN_062e1630(OwnerFacing, FollowerFacing) FCOM'd unrounded
        // (`TEST AH,0x5; JP`): a running MINIMUM of how well the formation is aligned.
        Vector FollowerFacing;
        GetNegatedIfaceDirection(&FollowerFacing, Follower);
        double const AlignCos = ClampedDot3(OwnerFacing, FollowerFacing);
        if (AlignCos < static_cast<double>(MinAlignCos))
            MinAlignCos = static_cast<float>(AlignCos);
    }

    // B+0x3c is written every iteration and never read again anywhere in the
    // function -- retail computes the widest follower distance and throws it
    // away. Kept (and explicitly discarded) rather than deleted, because
    // dropping it would hide a real, if dead, retail computation.
    (void)MaxDistance;

    // .text:062ec2f2-062ec318. The divisor is picked off the SAME cruise flag
    // sampled at the top, not re-queried.
    float const ProjectionDivisor = bCruise ? 1000.0f : 300.0f; // DAT_063a0608 / DAT_0639fe30

    // `FCOMP DAT_063a0800; TEST AH,0x41; JNZ LAB_062ec469` -- the fallback
    // path is taken when MaxProjection <= -20.0f (or unordered), i.e. when
    // every follower is already well AHEAD of its station and there is nothing
    // for the leader to wait for.
    if (MaxProjection > -20.0f)
    {
        // .text:062ec31e-062ec346. The blend stays on the x87 stack throughout.
        double Blend = static_cast<double>(MaxProjection) / ProjectionDivisor;
        // `FCOMP DAT_0639b21c; TEST AH,0x5; JP LAB_062ec34c` -- the SQUARING
        // arm is the fall-through, so it runs only while MaxProjection is
        // below 20.0f; at or above it the linear ratio is used as-is.
        if (MaxProjection < 20.0f)
        {
            Blend = Blend * Blend;
            // `FCOM DAT_0639e6b8; TEST AH,0x5; JP` -- anything under 0.01f is
            // flattened to 0.0f (FSTP ST0 / FLD DAT_06399408).
            if (Blend < static_cast<double>(0.01f))
                Blend = 0.0;
        }

        // .text:062ec34c-062ec37b. `FMUL; FSUBR` against the same slot, so this
        // is Speed - Blend*Speed, FSTP'd to [B+0x18]; then floored at half the
        // speed (`FMUL double 0.5`, FLD [B+0x18], FCOMP, `TEST AH,0x5; JP`: only a
        // rounded target below the unrounded floor takes the floor).
        float TargetSpeed = static_cast<float>(static_cast<double>(MinFollowerSpeed) -
                                               Blend * MinFollowerSpeed);
        double const SpeedFloor = static_cast<double>(MinFollowerSpeed) * 0.5;
        if (static_cast<double>(TargetSpeed) < SpeedFloor)
            TargetSpeed = static_cast<float>(SpeedFloor);
        Out->Speed = TargetSpeed;

        // .text:062ec37d-062ec3a0. FUN_062e5b70 (item 1765) == slot +0xD4 ==
        // get_projected_throttle(Speed, Type).
        Out->Throttle =
            Behavior_InvokeManagerSlotD4(ObjInspectOf(OwnerObjOf(Sys)), TargetSpeed, ThrustType);

        // .text:062ec3a3-062ec3b3. _CIacos on the worst alignment cosine; FST keeps
        // a rounded copy in [B+0x14] while the first compare uses the unrounded one.
        double const AngleExact = std::acos(static_cast<double>(MinAlignCos));
        float const Angle = static_cast<float>(AngleExact);

        // +0x9c / +0xa0 are the receiver's own two angle thresholds; the ctor (item 1853)
        // seeds them with 0.1f and 0.2f radians.
        float const NarrowAngle = FormationNarrowAngle;
        float const WideAngle = FormationWideAngle;

        float AngularTarget;
        if (AngleExact < static_cast<double>(NarrowAngle))
        {
            // .text:062ec3be-062ec3c8: the fall-through of `TEST AH,0x5; JP`,
            // i.e. strictly-below. Stores the 0x7f7fffff sentinel, which the
            // clamp below immediately turns into "no angular limit at all".
            AngularTarget = 3.402823466e+38f;
        }
        else if (Angle < WideAngle)
        {
            // .text:062ec3db-062ec410. (WideAngle - Angle) * (MinAngular /
            // WideAngle), floored at half the minimum angular speed by the same
            // `FMUL double 0.5` + keep-larger shape used for the speed above.
            // NOTE the floor is not decorative and not a transcription slip:
            // this ramp PEAKS at exactly half the minimum (at Angle ==
            // NarrowAngle, with retail's own 0.1/0.2 seeds) and falls from
            // there, so with those defaults the whole band collapses to the
            // constant floor. Reproduced literally; the thresholds are fields,
            // not constants, and other seeds make the ramp live.
            // On the stack: (Wide - rounded Angle) * (MinAngular / Wide), FSTP'd.
            AngularTarget = static_cast<float>(
                (static_cast<double>(WideAngle) - Angle) *
                (static_cast<double>(MinFollowerAngularSpeed) / WideAngle));
            double const AngularFloor = static_cast<double>(MinFollowerAngularSpeed) * 0.5;
            if (static_cast<double>(AngularTarget) < AngularFloor)
                AngularTarget = static_cast<float>(AngularFloor);
        }
        else
        {
            // LAB_062ec412: `FMUL double 0.2` (DAT_0639fa50).
            AngularTarget = static_cast<float>(static_cast<double>(MinFollowerAngularSpeed) * 0.2);
        }

        // .text:062ec424-062ec43f: `TEST AH,0x41; JNZ` skips the store unless
        // strictly greater, i.e. clamp DOWN to the slowest follower's turn rate.
        if (AngularTarget > MinFollowerAngularSpeed)
            AngularTarget = MinFollowerAngularSpeed;
        Out->AngularSpeed = AngularTarget;

        // .text:062ec442-062ec461. FUN_062e5ba0 (item 1766) == slot +0xF8.
        Out->AngularThrottle = Behavior_InvokeManagerSlotF8(ObjInspectOf(OwnerObjOf(Sys)), AngularTarget);
    }
    else
    {
        // LAB_062ec469-.text:062ec4b9. Nothing to hold back for: run the
        // owner's own full-throttle speed and turn rate straight into the
        // envelope. ESI is loaded with 0x3f800000 once and reused as both the
        // 1.0f literals AND the two stored 1.0f fields.
        Out->Speed = Behavior_InvokeManagerSlotD8(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, ThrustType);
        Out->Throttle = 1.0f;
        // .text:062ec4ab-062ec4b3: FUN_062e5b20 (item 1763) for real, with
        // BOTH arguments 1.0f -- unlike the in-loop site, which passes
        // (1.0f, 0.0f).
        Out->AngularSpeed = Behavior_InvokeManagerSlotF0(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, 1.0f);
        Out->AngularThrottle = 1.0f;
    }

    // .text:062ec4bc-062ec507. While cruising the flag keeps the 1 stored back
    // at .text:062ec00c and nothing else is tested. Otherwise it becomes
    // "the formation is tight": `FCOMP DAT_0639b21c; TEST AH,0x5; JP` demands
    // MaxProjection strictly below 20.0f, and `FCOMP DAT_063a07f8; TEST
    // AH,0x41; JNZ` demands MinAlignCos strictly above 0.99. Either failing --
    // including either being NaN -- writes 0.
    if (!bCruise)
        Out->Flag10 = (MaxProjection < 20.0f && static_cast<double>(MinAlignCos) > 0.99) ? 1 : 0;

    // Both non-early-out tails (.text:062ec4f5 / .text:062ec507) return AL = 1;
    // only the guard at the top and the zero-follower check return 0
    // (LAB_062ec50a).
    return true;
}

// ============================================================================
// MOVED HERE 2026-09-17 (claude:w16-behavior). Everything from this banner to the end
// of the file used to sit in src/Common/common_stubs.cpp, lines 8412-10865, as one
// contiguous GotoBehavior cluster: the vtable slot bodies (FUN_062fd780 / FUN_062fd7a0 /
// FUN_062fd920 / FUN_062fd9c0 / LAB_062fd890 / LAB_062fda20 / LAB_062fda60 / LAB_062feb00 /
// FUN_062fe940 / FUN_062fe970), GotoBehavior::Update (LAB_062fdb80, item 1996),
// GotoBehavior::EnterState (LAB_062fe670, item 2000), and the twelve file-static helpers
// they share (GotoMoveIdea/GotoFaceIdea/GotoSplineMoveIdea, the four FollowSibling_* and
// the six Follower* ones).
//
// The split was verified to be exact before it was made, in both directions, by scanning
// the range's identifiers against common_stubs.cpp's own out-of-range definitions:
//   * the ONLY symbol this cluster needed from the rest of common_stubs.cpp was the
//     file-static IsIdAmongOwnerFormationFollowers (FUN_062ec520), which is now non-static
//     and lived in common_behavior.cpp (declared in common_behavior.h; since CMN-GC an
//     FGunConvergenceStateBlock member in common_gunconvergence.cpp) -- its real home,
//     since both its call sites load the same SystemManager+0x1364 receiver and it is a
//     member of neither behavior class;
//   * NOTHING outside the range referenced anything defined inside it.
// Only one new include was needed here: <cstring> for the std::memcpy state-block copies
// (common_behavior.h already arrives through common_gotobehavior.h). Not a single line of the bodies
// below was edited by the move -- they are byte-identical to what common_stubs.cpp held,
// so every "Common.dll.txt NNNNNN" citation in them still stands unaltered.
//
// TODO (out of the moving lane's scope): the extern "C" free functions below (no longer
// exported -- retail exports none of them) are GotoBehavior vtable slots that still carry their raw LAB_/FUN_ names
// and `unsigned char* param_1` receivers. Now that they share a translation unit with the
// real class they should become real GotoBehavior members, the way LAB_062fdb80 already
// became GotoBehavior::Update().
// ----------------------------------------------------------------------------
//
// Predecessor cluster to LAB_062fdb80 -- Common.dll.c/.txt 0x062fd780-0x062fda59,
// decomp-tracker items 1989/1990/1991/1992/1993/1994.
//
// OWNING CLASS AND ITS THREE SUB-OBJECTS -- RESOLVED 2026-09-17, ABI pass 50
// (claude:abi-pass50). Everything below used to be written as "the concrete
// owning class is still not independently named/RTTI-confirmed ... most likely
// FollowBehavior", with the +0x2c sub-object called FormationMoveFx and the
// +0xa4/+0x164 ones left raw. All four of those claims were wrong, and the
// correction comes from one constructor, read straight out of the raw listing:
//
//   FUN_062fd620 (Common.dll.txt 280834-280915) is GotoBehavior's constructor
//   -- it is the function that installs .rdata:PTR_FUN_063a2284 at
//   `.text:062fd6ad MOV dword ptr [ESI],.rdata:PTR_FUN_063a2284`, and every
//   item in this cluster is a slot of THAT table (1991 = 063a2288/slot 1,
//   1992 = 063a2290/slot 3, 1993 = 063a2294/slot 4, 1994 = 063a2298/slot 5).
//   The table has a real RTTI COL at [-4] (063a2280 -> 063a9f94); it is a
//   genuine C++ vtable, not one of the "0639f5dc-ish/063a2284-ish
//   function-pointer table" dead ends this file's older comments describe.
//   GotoBehavior is already fully modelled in common_gotobehavior.h.
//
//   The same constructor names all three embedded sub-objects by calling their
//   own constructors with an explicit LEA:
//     .text:062fd64b LEA ECX,[ESI+0x2c];   CALL FUN_062d4e60  -> MoveIdea       (0x78, ends +0xa4)
//     .text:062fd657 LEA ECX,[ESI+0xa4];   CALL FUN_062d6750  -> FaceIdea       (0xc0, ends +0x164)
//     .text:062fd667 LEA ECX,[ESI+0x164];  CALL FUN_062d9bf0  -> SplineMoveIdea (0x174, ends +0x2d8)
//   All three are RTTI-identified BaseIdea siblings (common_idea.h); the sizes
//   chain exactly, and the next field FUN_062fd620 writes is +0x2d8.
//
//   CORRECTION, stated rather than silently overwritten: the +0x2c sub-object
//   is NOT FormationMoveFx. FormationMoveFx (this port's invented name for the
//   class whose ctor is FUN_062d5aa0 / vtable .rdata:063a047c, retail RTTI name
//   MatchThrottle2Idea) is a DIFFERENT BaseIdea sibling; FUN_062d5aa0 has
//   exactly two call sites in the whole module (062ca381 and 062cb2ec) and
//   neither is this class. The reason the older reading looked convincing is
//   that every "evidence" call it cited is an /OPT:ICF fold of a one-line
//   setter shared by both siblings:
//     FUN_062d5a10 = `MOV byte [ECX+0x58],AL; RET 4`  (MoveIdea::Field58)
//     FUN_062d88b0 = `MOV dword [ECX+0x44],EAX; RET 4` (MoveIdea::Field44)
//     TextRenderContext::set_origin_h = `MOV [ECX+0x38],EAX; RET 4`
//   Identical code, so the linker gave them one address; that says nothing
//   about which class the receiver belongs to. Only the constructor does.
//
// The three sub-objects' dispatch sites in this cluster are now typed calls on
// MoveIdea/FaceIdea/SplineMoveIdea instead of `Vtable[N]` indexing. Slot 0 is
// BaseIdea::SetContext(Object, Handle, Context) and slot 0x10 is
// BaseIdea::Commit() in all three, so the old raw sites were at least reading
// in-range slots -- but see item 1991 below for an argument-order defect the
// typing exposed.
//
// These remain free functions taking `unsigned char* param_1` rather than
// GotoBehavior methods: GotoBehavior's own class (common_gotobehavior.h)
// deliberately does not reproduce vtable slots 3/4/5 (its GAP:AI-GOTO-STATE-
// MACHINE note), and moving them is a separate change. The sub-object casts
// below are exact, though -- offsets straight from GotoBehavior's own
// static_asserts.
// ============================================================================

// Common.dll.c 0x062fd3e0 (FUN_062fd3e0) is decomp-tracker item 1984 -- see
// common_archetype.h: that one is UNRELATED to this class (it is ArchDB's own
// per-node value destructor, called only from item 1942). Two of THIS
// cluster's own items reuse the exact same "destroy, then maybe free(this)"
// wrapper SHAPE for a real owned game object instead -- traced individually,
// not assumed identical just because the shape matches.

// Slot 1, LAB_062fd890 (.text:062fd890-062fd910, RET 0xc). BL = FUN_062d2090(Owner, Manager,
// Sys); only when it succeeds: Flag367 = 1, then each Idea's slot 0 with (+0x14, +0x24,
// Manager) -- MoveIdea, MoveIdea::SetEnabled(Flag367) (FUN_062d5a10), FaceIdea,
// SplineMoveIdea. Returns BL. (The old free function hard-wired the gate to false.)
bool GotoBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bBound = BindContext(Owner, Manager, Sys);           // 062fd8a6
    if (bBound)
    {
        Flag367 = 1;                                                // 062fd8c3
        SubObjectA.SetContext(OwnerContextPtr14, NavHelperPtr24, Manager);   // 062fd8ca
        SubObjectA.SetEnabled(Flag367);                             // 062fd8d7 (raw byte)
        SubObjectB.SetContext(OwnerContextPtr14, NavHelperPtr24, Manager);   // 062fd8f1
        SubObjectC.SetContext(OwnerContextPtr14, NavHelperPtr24, Manager);   // 062fd908
    }
    return bBound;
}

// Slot 3, FUN_062fd920 (.text:062fd920-062fd9be).
void GotoBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062fd924 FUN_062d20d0
    Flag369 = 0;                                                    // 062fd92c
    // 062fd933-062fd93c: OwnerContext->is_cruise_active(Flag368) (+0x60). Retail does not
    // clear Flag368 first (the old port did).
    int const CruiseResult =
        ObjInspectOf(OwnerContextPtr14)->is_cruise_active(reinterpret_cast<bool&>(Flag368));
    // 062fd93f-062fd953: mode 2 only when the flag came back set AND the call returned 0.
    SubObjectA.SetMode((Flag368 != 0 && CruiseResult == 0) ? 2 : 0);    // FUN_062d5980
    Flag365 = 1;                                                    // 062fd95f
    Flag36c = 1;                                                    // 062fd966
    SubObjectA.SetAngle38(3.14159274f);                             // 062fd96d, 0x40490fdb
    Field2e4 = Field360;                                            // 062fd97a
    Flag2ea = 0;                                                    // 062fd984
    Flag2ec = 0;                                                    // 062fd98b
    SetCruiseLock(1, 3);                                            // 062fd992 FUN_062d21d0
    // 062fd997-062fd9b9: SystemManager::ExternalIface (+0x14fc+0x84) is_player() -> this
    // object's own slot 14 (vtable +0x38) with true.
    SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);
    if (Sys->SelfTarget.Iface->is_player())
        set_cruise(true);
}

// Slot 4, FUN_062fd9c0 (.text:062fd9c0-062fda1b).
void GotoBehavior::Slot4()
{
    SubObjectB.Commit();                                            // 062fd9cf vtbl+0x10
    // 062fd9d2-062fd9fd: ExternalIface->is_player() and goto kind 0 -> MoveIdea::SetField44(2)
    // (FUN_062d88b0).
    SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);
    if (Sys->SelfTarget.Iface->is_player() && Field2f4 == 0)
        SubObjectA.SetField44(2);
    SubObjectA.Commit();                                            // 062fda08 vtbl+0x10
    // 062fda0b-062fda13: FUN_062e9030(NavHelper + 0x18, 0). The old port dropped this call.
    ((SystemManager*)NavHelperPtr24)->Thrust.SetStrafeDir(StrafeNone);
    Behavior::Slot4();                                              // 062fda1b JMP FUN_062d20f0
}

// Slot 5, LAB_062fda20 (.text:062fda20-062fda52, RET 4, argument unread). EAX = 2; Flags28
// bit 0x2 set -> (Flag29 ? 3 : 1), clear -> (Flag29 ? 3 : 2); stored to +0xc and returned
// (the old free function returned nothing).
int GotoBehavior::Slot5(float Value)
{
    (void)Value;
    int Result = 2;                                                 // 062fda23
    if ((Flags28 & 0x2) != 0)
        Result = (Flag29 != 0) ? 3 : 1;                             // LAB_062fda3e
    else if (Flag29 != 0)
        Result = 3;                                                 // 062fda33
    ModeSelector0xc = Result;
    return Result;
}

// Slot 6, LAB_062fda60 (.text:062fda60-062fdb74, RET 4, argument unread).
EvaluateResult GotoBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    IObjInspect const* const Owner = ObjInspectOf(OwnerContextPtr14);
    IObjRW* Target = nullptr;                                       // 062fda6d
    Owner->get_target(Target);                                      // 062fda78 +0x74
    if (Target == nullptr)
        return EvalBlocked;                                         // -> LAB_062fdb6b (1)

    // 062fda87-062fdac7: is_using_tradelane (+0x134) on the owner, then on the target, into
    // ONE byte retail zeroes only before the first call.
    bool bInLane = false;
    Owner->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;                                            // 2
    Target->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;

    // 062fdad5-062fdb15: get_type (+0x88). Only a ZERO return with a 0x7f0000 type bit reaches
    // get_behavior_id (+0x148); ids 0xe / 0x11 / 0x8 -> 2. (The old port took the id branch on
    // a NONZERO return too: `JNZ LAB_062fdb23` skips it.)
    unsigned int Type = 0;
    int const TypeResult = Target->get_type(Type);
    if (TypeResult == 0 && (Type & 0x7f0000) != 0)
    {
        int const BehaviorId = Target->get_behavior_id();
        if (BehaviorId == 0xe || BehaviorId == 0x11 || BehaviorId == 0x8)
            return EvalSkip;
    }
    if ((Type & 0x10000000) != 0)
        return EvalBlocked;                                         // 062fdb2b -> 1

    // 062fdb2d-062fdb69: |velocity|^2 on the FPU stack, ((x*x + y*y) + z*z), FCOMP float 25.0f
    // (DAT_063a227c); TEST AH,0x41 + JZ -> strictly greater keeps 2, otherwise 1.
    Vector const Velocity = Target->get_velocity();                 // +0x04
    double const SpeedSq = static_cast<double>(Velocity.x) * Velocity.x
                         + static_cast<double>(Velocity.y) * Velocity.y
                         + static_cast<double>(Velocity.z) * Velocity.z;
    return (SpeedSq > 25.0) ? EvalSkip : EvalBlocked;
}

// Slot 21, FUN_062c6d30 (.text:062c6d30-062c6da5, RET 4): drop Watcher310 and then Watcher370
// when their object's get_id() (+0x20) equals the argument.
void GotoBehavior::Slot21(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Dest = ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
    if (Dest != nullptr && Dest->get_id() == LeftId)
        Watcher310.set(nullptr);
    IObjRW* const Prox = ObjRwFromWatchable(const_cast<Watchable*>(Watcher370.target()));
    if (Prox != nullptr && Prox->get_id() == LeftId)
        Watcher370.set(nullptr);
}

// Slot 24, LAB_062c6db0 (.text:062c6db0-062c6dc0): Watcher310's object (target - 8) or null.
IObjRW const* GotoBehavior::get_target()
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
}

// ============================================================================
// GotoBehavior::Update -- vtable slot 7, LAB_062fdb80 (Common.dll.txt 281323-282292, 796
// instructions plus the PTR_LAB_062fe658 jump table). Re-ported 2026-10-08 (cmn-ai7) from the
// listing onto the modelled fields; the previous body (raw `param_1` offsets) dropped the
// FUN_062e4ea0 / FUN_062e4bb0 results the later compares read, invented a 0.0f "no prior
// write" placeholder for them, built case 0's direction with a 0.0f x, and returned 2 instead
// of 3 on case 0's velocity exit.
//
// Frame: `SUB ESP,0x54` + EBX/EBP/ESI/EDI, E = ESP after the pushes:
//   E+0x11 byte  bCruise        -- the "fly in cruise mode" decision of the 0/1 tail
//   E+0x12 byte  bTargetCruise  -- the watched object's is_cruise_active
//   E+0x13 byte  owner is_cruise_active out-param (never read back)
//   E+0x14 int   Result         -- EBP = 3 at entry
//   E+0x18 float Throttle       -- Field360, overridden from Field2e4 / 1.0f for the player
//   E+0x1c float Clearance      -- 0 at entry; case 1 / case 0 store the FUN_062e4ea0 /
//                                  FUN_062e4bb0 result here, the tail stores the
//                                  FUN_062c2c70 speed cap here.
// Both `switch` statements dispatch on Field2f4 (0 watch an object, 1 a point, 2 a spline).
// ============================================================================

namespace
{
    // `raw = [W]; raw ? raw - 8 : 0` -- the IObjRW behind a BaseWatcher. Retail's tests of the
    // result (`ADD EAX,-8; TEST EAX,EAX`) also treat a raw value of 8 as "nothing watched",
    // which ObjRwFromWatchable's null-or-minus-8 reproduces.
    IObjRW* GotoWatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The negated third column of a frame's rotation: FLD [+8] / [+0x14] / [+0x20], each FCHS'd
    // and `FSTP float` (.text:062fdcca-062fdcf4 and .text:062fe007-062fe035).
    Vector NegatedColumn2(Matrix const& Rotation)
    {
        return Vector{ -Rotation.E[0][2], -Rotation.E[1][2], -Rotation.E[2][2] };
    }

    // (x*x + y*y) + z*z on the x87 stack: FLD [+8], FLD [+4], FLD [+0]; FLD ST0 / FMUL ST1;
    // FLD ST2 / FMUL ST3; FADDP; FLD ST3 / FMUL ST4; FADDP (.text:062fdd5f-062fdd75 and
    // .text:062fdf1a-062fdf30). Never stored, so it stays double.
    double LengthSquaredXyz(Vector const& V)
    {
        return (static_cast<double>(V.x) * V.x + static_cast<double>(V.y) * V.y) +
               static_cast<double>(V.z) * V.z;
    }
}

int GotoBehavior::Update()
{
    // 062fdb88-062fdb9f: a pending disengage request finishes the tick with 2.
    if ((Flags28 & 0x1) != 0)
    {
        UpdateResult10 = 2;
        return 2;
    }

    // 062fdba0-062fdbad -> LAB_062fe646: no owner, or not engaged, finishes with 3.
    if (OwnerContextPtr14 == nullptr || (Flags28 & 0x2) == 0)
    {
        UpdateResult10 = 3;
        return 3;
    }

    IObjInspect const* const Owner = ObjInspectOf(OwnerContextPtr14);
    SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);
    FAiTarget* const Elem = &Sys->SelfTarget;

    // 062fdbb3-062fdbc8: Owner->is_cruise_active (slot +0x60) into E+0x13, which nothing reads.
    bool bOwnerCruise = false;
    int Result = 3;   // E+0x14, EBP
    Owner->is_cruise_active(bOwnerCruise);

    // 062fdbcb-062fdbf2: the steering element's PosValid refill (flag set before the call).
    if (Elem->bPosValid == 0)
    {
        Elem->bPosValid = 1;
        Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
    }

    // 062fdbf5-062fdc13: the lead point starts as our own cached position.
    Field2f8 = Elem->CachedPos;

    // 062fdc0d-062fdc66: Throttle = Field360; Clearance = 0; for the player ship Field2e4, or
    // 1.0f while Flag2e8 is set and Flag36b clear.
    float Throttle = Field360;
    float Clearance = 0.0f;
    if (Elem->Iface->is_player())
    {
        if (Flag2e8 != 0 && Flag36b == 0)
            Throttle = 1.0f;
        else
            Throttle = Field2e4;
    }

    // 062fdc66-062fdc85: `SUB 0 / JZ`, `DEC / JZ`, `SUB 2 / JNZ`; value 3 stores EBP (3) into
    // Result, which already holds 3. Everything but 0 and 1 goes straight to the tail.
    if (Field2f4 == 1)
    {
        // LAB_062fdc8a: Clearance = FUN_062e4ea0(Elem, &Field304), `FSTP float`.
        Clearance = static_cast<float>(GetSignedRadialClearanceToPoint(Elem, Field304));

        if (Flag2ea != 0)
        {
            // 062fdcb5-062fdcf4: Dir (E+0x20) = -column 2 of our own frame (slot +0x0c).
            Vector const Dir = NegatedColumn2(Elem->Iface->get_transform().Rotation);

            // 062fdcf8-062fdd13: PosValid refill again.
            if (Elem->bPosValid == 0)
            {
                Elem->bPosValid = 1;
                Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
            }

            // 062fdd16-062fdd5b: Ahead (E+0x38) = CachedPos + Dir * 1750.0f (DAT_063a22f0 =
            // 00 c0 da 44). x and y round the product (`FSTP float` to E+0x2c / E+0x30) before
            // the FADD; z adds the unrounded product.
            float const ScaledX = Dir.x * 1750.0f;
            float const ScaledY = Dir.y * 1750.0f;
            Vector const Ahead{
                ScaledX + Elem->CachedPos.x,
                ScaledY + Elem->CachedPos.y,
                static_cast<float>(static_cast<double>(Dir.z) * 1750.0f + Elem->CachedPos.z) };

            // 062fdd5f-062fdda3: `FCOMP 0.0f; TEST AH,0x41; JNZ` -- a non-zero (strictly
            // positive) steering triple Field2d8 makes the FaceIdea face that axis
            // (FUN_062d75e0), otherwise it faces the point ahead (FUN_062d7560).
            Vector const& Steer = *reinterpret_cast<Vector const*>(&Field2d8);
            if (LengthSquaredXyz(Steer) > 0.0)
                SubObjectB.SetAxis(Steer);
            else
                SubObjectB.SetPosition(Ahead);

            // LAB_062fdda8: FUN_062d88b0(2), FUN_062d5890(&Dir) on the MoveIdea.
            SubObjectA.SetField44(2);
            SubObjectA.SetDirection(Dir);

            // 062fddc0-062fddd6: `FLD Clearance; FCOMP Field358`, Flag366 = 1 stored between,
            // `TEST AH,0x5; JP LAB_062fde2d` -- only strictly-less falls through.
            Flag366 = 1;
            if (Clearance < Field358)
            {
                // 062fddd8-062fde17: arrived.
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                Slot4();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgrPtr20->SetDirectiveResult(2, 0);
                UpdateResult10 = 2;
                return 2;
            }
        }
        else
        {
            // LAB_062fde18: FaceIdea faces the destination (FUN_062d7560), MoveIdea moves to it
            // (FUN_062d5840).
            SubObjectB.SetPosition(Field304);
            SubObjectA.SetPosition(Field304);
        }

        // LAB_062fde2d: the lead point is the destination.
        Field2f8 = Field304;
    }
    else if (Field2f4 == 0)
    {
        // LAB_062fde43.
        if (GotoWatchedObject(Watcher310) == nullptr)
        {
            // LAB_062fde58: the watched object is gone.
            Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
            Slot4();
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgrPtr20->SetDirectiveResult(2, 2);

            // 062fde7e-062fdea5: for the player, IObjAffect slot +0x6c (3, 0) on the owner.
            if (Elem->Iface->is_player())
                ObjAffectOf(OwnerContextPtr14)->slot_6c(3, 0);

            // LAB_062fdea8.
            Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
            Flag368 = 0;
            Slot4();
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgrPtr20->SetDirectiveResult(2, 2);
            UpdateResult10 = Result;
            return Result;
        }

        // LAB_062fdedf: for the player, a watched object moving faster than 5 m/s ends the
        // goto. 062fdf10-062fdf43: get_velocity (slot +0x04, hidden return buffer E+0x44), its
        // (x*x+y*y)+z*z against 25.0f (DAT_063a227c = 00 00 c8 41), `TEST AH,0x41; JNZ` --
        // strictly greater falls through.
        if (Elem->Iface->is_player())
        {
            Vector const Velocity = GotoWatchedObject(Watcher310)->get_velocity();
            if (LengthSquaredXyz(Velocity) > 25.0)
            {
                ObjAffectOf(OwnerContextPtr14)->slot_6c(2, 0);
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                Slot4();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgrPtr20->SetDirectiveResult(2, 2);
                // 062fdf80-062fdf8c: `MOV [ESI+0x10],EBP; MOV EAX,EBP` -- 3.
                UpdateResult10 = Result;
                return Result;
            }
        }

        // LAB_062fdf8d: the lead point is the watched object's position (FUN_062e60e0), and
        // Clearance = FUN_062e4bb0(Elem, watched), `FSTP float`.
        Vector WatchedPos;
        GetIfacePositionOrZero(&WatchedPos, GotoWatchedObject(Watcher310));
        Field2f8 = WatchedPos;
        Clearance = static_cast<float>(GetInterceptClearanceOfIface(Elem, GotoWatchedObject(Watcher310)));

        if (Flag2ea != 0)
        {
            // 062fdff2-062fe046: Dir (E+0x38) = -column 2 of our own frame; the FaceIdea faces
            // the steering triple unconditionally (FUN_062d75e0), the MoveIdea flies along Dir.
            Vector const Dir = NegatedColumn2(Elem->Iface->get_transform().Rotation);
            SubObjectB.SetAxis(*reinterpret_cast<Vector const*>(&Field2d8));
            SubObjectA.SetDirection(Dir);

            // 062fe04b-062fe05a: `FCOMP Field358; TEST AH,0x5; JP LAB_062fe0d3`.
            if (Clearance < Field358)
            {
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                Slot4();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgrPtr20->SetDirectiveResult(2, 0);
                UpdateResult10 = 2;
                return 2;
            }
        }
        else
        {
            // LAB_062fe09c: both Ideas watch the object (FUN_062d5760, FUN_062d7510).
            SubObjectA.SetWatchObject(GotoWatchedObject(Watcher310));
            SubObjectB.SetWatchObject(GotoWatchedObject(Watcher310));
        }
    }

    // LAB_062fe0d3: FUN_062c2af0 / FUN_062c2d90 on the proximity watcher at +0x370.
    unsigned char* const ProxWatcher = reinterpret_cast<unsigned char*>(&Watcher370);
    ProxWatcherRecomputeZoneFlags(ProxWatcher, NavHelperPtr24, Field2f8);
    ProxWatcherNotifyZoneChange(ProxWatcher, NavHelperPtr24, BehaviorMgrPtr20, Field2f8);

    if (Field2f4 == 2)
    {
        // 062fe11b-062fe148: the lead point is the spline's end point (Spline.P2 at +0x330);
        // FUN_062da4d0 (SplineMoveIdea::SetBankBlendCap) with Throttle.
        Field2f8 = Spline.P2;
        SubObjectC.SetBankBlendCap(Throttle);

        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            // 062fe15a-062fe197: Clearance = FUN_062c2c70(Owner, Sys, 1, Field360) (`FSTP
            // float`); when Owner's get_speed(Throttle, engine) (FUN_062e5af0) is strictly
            // greater (`TEST AH,0x41; JNZ`), FUN_062da4e0 with that cap.
            Clearance = static_cast<float>(
                ProxWatcherComputeSpeedCap(ProxWatcher, OwnerContextPtr14, NavHelperPtr24, 1, Field360));
            if (Behavior_InvokeManagerSlotD8(Owner, Throttle, ThrustEquipEngine) > Clearance)
                SubObjectC.SetThrottleScale(Clearance);
        }

        // LAB_062fe19c: SplineMoveIdea::Update (slot 1) with the tick delta.
        int const SplineResult = SubObjectC.Update(DeltaTime1c);
        if (SplineResult == 0)
        {
            // LAB_062fe544.
            Result = 1;
            UpdateResult10 = Result;
            return Result;
        }
        if (SplineResult != 1)
        {
            Result = 3;   // LAB_062fe5e2
        }
        else
        {
            // 062fe1b7-062fe1ed: done with the spline -- keep going (1) while the watcher still
            // has an object, is outside zone 2 and Flag366 is clear, else LAB_062fe1ee (2).
            if (GotoWatchedObject(Watcher370) != nullptr && (Flags384 & 0x2) == 0 && Flag366 == 0)
            {
                Result = 1;
                UpdateResult10 = Result;
                return Result;
            }
            Result = 2;
        }
    }
    else if (Field2f4 == 0 || Field2f4 == 1)
    {
        // LAB_062fe1fb: FUN_062d88b0(Flag366 ? 2 : 0) -- `NEG DL; SBB EDX,EDX; AND EDX,2`.
        SubObjectA.SetField44(Flag366 != 0 ? 2 : 0);

        // 062fe213-062fe23d: once per goto (Flag365), Flag364 = Clearance > 1750.0f
        // (`TEST AH,0x41; JNZ` -> 0 on not-greater or unordered).
        if (Flag365 != 0)
        {
            Flag364 = (Clearance > 1750.0f) ? 1 : 0;
            Flag365 = 0;
        }

        // 062fe244-062fe2b8: bCruise. For the player it is Flag364 only while Flag2e8 is set
        // (else Flag2e8 itself, i.e. 0); for AI ships Flag364. A non-zero value is then
        // confirmed by the pathfinder when there is one (FUN_062de160). Flag36a forces it on,
        // Flag36b forces it off.
        unsigned char bCruise;
        if (Elem->Iface->is_player())
        {
            bCruise = Flag2e8;
            if (Flag2e8 != 0)
                bCruise = Flag364;
        }
        else
        {
            bCruise = Flag364;
        }
        if (bCruise != 0 && BehaviorMgrPtr20->Pathfind != nullptr)
            bCruise = BehaviorMgrPtr20->Pathfind->IsCruiseUnobstructed() ? 1 : 0;
        if (Flag36a != 0)
            bCruise = 1;
        if (Flag36b != 0)
            bCruise = 0;

        // 062fe2bd-062fe351: the formation envelope (FUN_062ebf50 on Sys+0x1364, out-struct
        // E+0x50). Retail zeroes only its first four dwords; the flag byte is value-initialised
        // here as well (the callee writes it on every path that returns true).
        FormationFollowerEnvelope Envelope{};
        if (Sys->GunConvergenceStateBytes.ComputeFormationFollowerEnvelope(&Envelope))
        {
            // 062fe2f4-062fe31e: MoveIdea angle pi (`PUSH 0x40490fdb`), Throttle clamped down to
            // the envelope's (`TEST AH,0x5; JP` -> assign only when strictly less), MoveIdea
            // scale = Throttle.
            SubObjectA.SetAngle38(3.14159274f);
            if (Envelope.Throttle < Throttle)
                Throttle = Envelope.Throttle;
            SubObjectA.SetScale40(Throttle);

            // 062fe323-062fe342: FaceIdea throttle scale from the envelope's angular speed
            // (FUN_062d7610); a clear envelope flag cancels cruise.
            SubObjectB.SetThrottleScale(Envelope.AngularSpeed);
            if (Envelope.Flag10 == 0)
                bCruise = 0;
        }
        else
        {
            // LAB_062fe344: FUN_062d9490(1.0f) on the FaceIdea.
            SubObjectB.SetScale94(1.0f);
        }

        // LAB_062fe356: bTargetCruise = the watched object's is_cruise_active (slot +0x60).
        unsigned char bTargetCruise = 0;
        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            bool bWatchedCruise = false;
            GotoWatchedObject(Watcher370)->is_cruise_active(bWatchedCruise);
            bTargetCruise = bWatchedCruise ? 1 : 0;
        }

        // LAB_062fe37f: unless forced, a watched object that is not cruising and is not one of
        // our formation followers (FUN_062c2ab0, FUN_062ec520 on Sys+0x1364) cancels cruise.
        if (Flag36a == 0 && GotoWatchedObject(Watcher370) != nullptr && bTargetCruise == 0)
        {
            if (!Sys->GunConvergenceStateBytes.IsIdAmongOwnerFormationFollowers(Watcher370.get_watched_id()))
                bCruise = 0;
        }

        // LAB_062fe3bc: a change of decision switches the MoveIdea mode (FUN_062d5980).
        if (bCruise != Flag368)
        {
            if (bCruise != 0)
            {
                SubObjectA.SetMode(2);
                Flag368 = 1;
            }
            else
            {
                // 062fe3df-062fe3f9: DeviceTRC::bind_to_dc fold == MoveIdea::SetRange3c.
                SubObjectA.SetRange3c(Field358);
                SubObjectA.SetMode(0);
                Flag368 = 0;
            }
        }

        // LAB_062fe400: speed cap from the proximity watcher (FUN_062c2c70, selector 4 in
        // cruise else 1 -- `NEG AL; SBB EAX,EAX; AND EAX,3; INC EAX`), stored to Clearance.
        // When Owner's get_speed(Throttle, type) is strictly greater (`TEST AH,0x41; JNZ`),
        // Throttle = get_projected_throttle(cap, type) (FUN_062e5b70). The query type is
        // cruise only while both we and the watched object cruise.
        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            Clearance = static_cast<float>(ProxWatcherComputeSpeedCap(
                ProxWatcher, OwnerContextPtr14, NavHelperPtr24, Flag368 != 0 ? 4 : 1, Field360));
            ThrustEquipType const QueryType =
                (Flag368 != 0 && bTargetCruise != 0) ? ThrustEquipCruise : ThrustEquipEngine;
            if (Behavior_InvokeManagerSlotD8(Owner, Throttle, QueryType) > Clearance)
                Throttle = Behavior_InvokeManagerSlotD4(Owner, Clearance, QueryType);
        }

        // LAB_062fe4b0: FaceIdea::Update (slot 1) with the tick delta, kept in EBX.
        int const FaceResult = SubObjectB.Update(DeltaTime1c);

        // 062fe4bd-062fe53d: for the player, Flag2ec engages the cruise gate with -1.0f
        // (FUN_062e89a0) and Flag2e9 clear drops the registration flag (FUN_062e91d0) -- both
        // dispatch with index 0. Otherwise the MoveIdea gets the throttle (CAsteroidCube::
        // set_state fold == MoveIdea::SetScale40) and its own Update picks the index.
        unsigned int Index = 0;
        bool bRunMove = true;
        if (Elem->Iface->is_player())
        {
            if (Flag2ec != 0)
            {
                Sys->Thrust.SetThrottle(-1.0f, 0);
                bRunMove = false;
            }
            else if (Flag2e9 == 0)
            {
                Sys->Thrust.SetEngineActive(false);
                bRunMove = false;
            }
        }
        if (bRunMove)
        {
            // LAB_062fe51d.
            SubObjectA.SetScale40(Throttle);
            Index = static_cast<unsigned int>(SubObjectA.Update(DeltaTime1c));
        }

        // 062fe534-062fe53d: `CMP EDI,3; JA LAB_062fe5ea` (Result still 3), then the
        // PTR_LAB_062fe658 table {062fe544, 062fe55b, 062fe5e2, 062fe5e2}.
        if (Index == 0)
        {
            // LAB_062fe544.
            Result = 1;
            UpdateResult10 = Result;
            return Result;
        }
        if (Index == 1)
        {
            // LAB_062fe55b: BL = (FaceResult == 1), forced on while Flag36c is clear. The player
            // keeps going (1) when neither cruise nor the face is pending.
            bool bFaceDone = (FaceResult == 1);
            if (Flag36c == 0)
                bFaceDone = true;
            if (Owner->is_player() && bCruise == 0 && !bFaceDone)
            {
                Result = 1;
                UpdateResult10 = Result;
                return Result;
            }

            // LAB_062fe59d: same keep-going test as the spline arm, else LAB_062fe1ee (2).
            if (GotoWatchedObject(Watcher370) != nullptr && (Flags384 & 0x2) == 0 && Flag366 == 0)
            {
                Result = 1;
                UpdateResult10 = Result;
                return Result;
            }
            Result = 2;
        }
        else if (Index <= 3)
        {
            Result = 3;   // LAB_062fe5e2
        }
    }

    // LAB_062fe5ea: finish -- request disengage, Slot4 (vtable +0x10), report to the manager:
    // SetDirectiveResult(2, 0) when Result is 2, (2, 2) otherwise.
    Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
    Flag368 = 0;
    Slot4();
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    if (Result == 2)
        BehaviorMgrPtr20->SetDirectiveResult(2, 0);
    else
        BehaviorMgrPtr20->SetDirectiveResult(2, 2);
    UpdateResult10 = Result;
    return Result;
}

// LAB_062fe544 / LAB_062fe55b / LAB_062fe5e2 (tracker items 1997-1999) are internal labels of
// Update above -- jump-table targets of PTR_LAB_062fe658 sharing its frame -- not functions.

// FUN_062e74a0 (item 1798): the checked id lookup through the IObjDB at SystemManager+0x34,
// SystemManagerTimerTable::FindVerifiedObject (common_sysmgr_timertable.cpp).

// Slot 10, LAB_062fe670 (.text:062fe670-062fe938, RET 4): apply a pub::AI::DirectiveGotoOp.
// Re-ported 2026-10-08 (claude-code:cmn-ai5) from the listing onto the modelled fields; the
// previous body (EnterState, raw offsets) stubbed FUN_062e74a0 to null, skipped
// FUN_062da380 / FUN_062d7510 / FUN_062c29f0, swapped the DAT_0639f418 arms and routed
// FUN_062da400 / FUN_062d21d0 into empty stubs.
bool GotoBehavior::Slot10(void* DirectiveOp)
{
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfe);           // 062fe680 / 062fe691
    // 062fe682-062fe68f: REP MOVSD of 0x1c dwords, the whole op (vptr included) -> GotoOp.
    std::memcpy(static_cast<void*>(&GotoOp), DirectiveOp, sizeof(GotoOp));
    pub::AI::DirectiveGotoOp const& Op = *static_cast<pub::AI::DirectiveGotoOp const*>(DirectiveOp);
    SystemManagerTimerTable const* const ObjDb = &static_cast<SystemManager*>(NavHelperPtr24)->Timers;
    Field2f4 = Op.Field_0C;                                         // 062fe699

    switch (Op.Field_0C)
    {
    case 0:
    {
        // LAB_062fe79d: watch the object Op.Field_1C names.
        IObjRW* const Dest = static_cast<IObjRW*>(
            ObjDb->FindVerifiedObject((unsigned const*)&GotoOp.Field_1C));
        Watcher310.set(Dest != nullptr ? static_cast<Watchable const*>(Dest) : nullptr);
        SubObjectA.SetRange3c(Field358);                            // 062fe7d5 (bind_to_dc fold)
        IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
        SubObjectA.SetWatchObject(Watched);                         // 062fe7ea FUN_062d5760
        SubObjectB.SetWatchObject(Watched);                         // 062fe803 FUN_062d7510
        break;
    }
    case 1:
        // LAB_062fe76b: the destination point, into both Ideas.
        Field304 = Op.Destination;
        SubObjectA.SetPosition(Field304);                           // 062fe78a FUN_062d5840
        SubObjectB.SetPosition(Field304);                           // 062fe796 FUN_062d7560
        break;
    case 2:
        // LAB_062fe6c4: the spline's four control points from op+0x38/+0x20/+0x2c/+0x44.
        Spline.P0 = *reinterpret_cast<Vector const*>(&Op.Field_38);
        Spline.P1 = *reinterpret_cast<Vector const*>(&Op.Field_20);
        Spline.P2 = *reinterpret_cast<Vector const*>(&Op.Field_2C);
        Spline.P3 = *reinterpret_cast<Vector const*>(&Op.Field_44);
        Spline.calculate_arclength();                               // 062fe72e
        SubObjectC.SetSpline(&Spline);                              // 062fe73a FUN_062da380
        SubObjectC.SetWideMode(Op.Field_5A);                        // 062fe74b FUN_062da410 (raw byte)
        Field2f8 = Spline.P2;                                       // 062fe750-062fe763
        break;
    default:
        // 062fe6b2-062fe6c1.
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        return false;
    }

    // LAB_062fe808: shared tail.
    Field358 = Op.Field_50;                                         // 062fe816
    SubObjectA.SetRange3c(Field358);                                // 062fe81f
    Field35c = Op.Field_54;                                         // 062fe82b
    // 062fe831-062fe83c: FCOMP double DAT_0639f418 (1.0e-5, bytes f1 68 e3 88 b5 f8 e4 3e);
    // TEST AH,0x41 + JP is taken on greater OR unordered -> owner's get_projected_throttle
    // (FUN_062e5b70, slot 0xd4) of that speed; less-or-equal -> 1.0f. (The old port had the
    // two arms swapped and never made the call.)
    if (!(static_cast<double>(Op.Field_54) <= 1.0e-5))
        Field360 = Behavior_InvokeManagerSlotD4(ObjInspectOf(OwnerContextPtr14), Op.Field_54,
                                                IObject::ThrustEquipEngine);   // PUSH 0x1
    else
        Field360 = 1.0f;                                            // 062fe83e
    Flag367 = Op.Field_59;                                          // 062fe86a
    SubObjectA.SetEnabled(Flag367);                                 // 062fe871 FUN_062d5a10 (raw byte)
    Flag366 = Op.Field_58;                                          // 062fe87b
    SubObjectA.SetField44(Op.Field_58 != 0 ? 2 : 0);                // 062fe887/062fe892
    SubObjectC.SetField158(Op.Field_58 != 0 ? 2 : 0);               // 062fe89f FUN_062da400
    SetCruiseLock(Op.Field_6C == 0 ? 1 : 0, 3);                     // 062fe8b1 (SETZ)
    Flag36a = Op.Field_5A;                                          // 062fe8b9
    Flag36b = Op.Field_5B;                                          // 062fe8c2

    // 062fe8c8-062fe91b: the proximity watcher. Only a nonzero op+0x5c is looked up, and only
    // then are op+0x60/+0x64/+0x68 passed; otherwise all four arguments are zero.
    void* ProxTarget = nullptr;
    float ProxNear = 0.0f;
    float ProxFar = 0.0f;
    float ProxRatio = 0.0f;
    if (Op.Field_5C != 0)
    {
        ProxTarget = ObjDb->FindVerifiedObject((unsigned const*)&GotoOp.Field_5C);
        ProxNear = Op.Field_60;
        ProxFar = Op.Field_64;
        ProxRatio = *reinterpret_cast<float const*>(&Op.Field_68);
    }
    ProxWatcherInit(reinterpret_cast<unsigned char*>(&Watcher370), NavHelperPtr24, ProxTarget,
                    ProxNear, ProxFar, ProxRatio);                  // 062fe91b FUN_062c29f0

    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);            // 062fe925
    Flag2ea = 0;                                                    // 062fe92b
    return true;
}

// Common.dll.txt 282558-282576 / Common.dll.c 136404-136417 (decomp-tracker item 2001,
// FUN_062fe940). Tiny getter/predicate pair for the same sibling class as LAB_062fda60/
// LAB_062fe670/LAB_062fdb80 above: copies the 0x70-byte state block back OUT of
// this+0x38c (the exact inverse of LAB_062fe670's own IN-copy above) into a caller-
// supplied buffer, and returns bit 1 of Flags28 (this+0x28) as a bool.
// thiscall(this, void* OutBuffer).
extern "C" bool __fastcall FUN_062fe940(unsigned char* param_1, void* outBuffer)
{
    std::memcpy(outBuffer, param_1 + 0x38c, 0x70);
    return (param_1[0x28] & 0x2) != 0;
}

// Retail's two-sided clamp to [-1, 1] at .text:062fea1a-062fea69 (and again for +0x2dc):
// FCOMP -1.0f with TEST AH,0x41 + JNZ sends not-greater AND unordered to -1.0f; then FCOMP
// 1.0f with TEST AH,0x5 + JP sends >= to 1.0f; otherwise the value stays.
static float GotoClampUnit(float Value)
{
    if (!(Value > -1.0f))
        return -1.0f;
    if (!(Value < 1.0f))
        return 1.0f;
    return Value;
}

// Slot 12, FUN_062fe970 (.text:062fe970-062fead1, RET 4).
void GotoBehavior::set_direction(Vector const& Dir)
{
    // 062fe973-062fe9b2: goto kind 1 always proceeds; kind 0 only when |Dir|^2 (FPU, ((x*x +
    // y*y) + z*z)) is strictly greater than 0.0f (TEST AH,0x41 + JZ). Anything else clears
    // Flag2ea. (The old port proceeded on the NOT-greater side.)
    bool bProceed = false;
    if (Field2f4 == 1)
    {
        bProceed = true;
    }
    else if (Field2f4 == 0)
    {
        double const LengthSq = static_cast<double>(Dir.x) * Dir.x
                              + static_cast<double>(Dir.y) * Dir.y
                              + static_cast<double>(Dir.z) * Dir.z;
        bProceed = LengthSq > 0.0;
    }
    if (!bProceed)
    {
        Flag2ea = 0;                                                // LAB_062fe9b4
        return;
    }

    Flag2ea = 1;                                                    // 062fe9bf
    Field2d8 = -Dir.y;                                              // 062fe9c6-062fe9cb
    Field2e0 = 0.0f;                                                // 062fe9d3 (ECX == 0)
    Field2dc = -Dir.x;                                              // 062fe9d1-062fe9de
    // 062fe9e4-062fea16: factor kept on the FPU; FCOM double 0.0 with TEST AH,0x41 + JNZ ->
    // only a strictly positive factor scales; FDIVR gives 1/factor, each product FSTP dword.
    double const Factor = BehaviorMgrPtr20->get_turn_sensitivity_factor();
    if (Factor > 0.0)
    {
        double const InvFactor = 1.0 / Factor;
        Field2d8 = static_cast<float>(InvFactor * Field2d8);
        Field2dc = static_cast<float>(InvFactor * Field2dc);
    }
    Field2d8 = GotoClampUnit(Field2d8);                             // LAB_062fea1a-062fea69
    Field2dc = GotoClampUnit(Field2dc);                             // 062fea6f-062feabe
    Field2e0 = -Dir.z;                                              // 062feac4-062feaca
}

// Slot 13, LAB_062feae0 (.text:062feae0-062feaf8, RET 4).
void GotoBehavior::set_throttle(float Throttle)
{
    Flag2eb = 1;
    Field2e4 = Throttle;
    Flag2e9 = 1;
}

// Slot 14, LAB_062feb00 (.text:062feb00-062feb5c, RET 4).
void GotoBehavior::set_cruise(bool bOn)
{
    if (bOn)
    {
        Flag2e8 = 1;                                                // 062feb12 (BL)
        Field2e4 = Field360;                                        // 062feb18
        Flag2e9 = 1;                                                // 062feb1e
        return;
    }
    if (Flag2e8 != 0)
        Field2e4 = Field360;                                        // 062feb3a
    // 062feb40-062feb48: ThrustManager::engage_cruise(NavHelper + 0x18, false). The old port
    // dropped this call as "ThrustManager not ported"; it is (common_systemmanager.h).
    static_cast<SystemManager*>(NavHelperPtr24)->Thrust.engage_cruise(false);
    Flag2e8 = 0;                                                    // 062feb4d
    Flag2e9 = 1;                                                    // 062feb53
}

// Slot 18, LAB_062feb60 (.text:062feb60-062feb6a, RET 4).
void GotoBehavior::brake_reverse(bool bOn)
{
    Flag2ec = bOn ? 1 : 0;
}

