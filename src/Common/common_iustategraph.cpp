// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_iustategraph.h"

#include "common_systemmanager.h"   // SystemManager (Watcher at +0x08, owner FAiTarget at +0x14FC)
#include "common_misc.h"
#include "common_aisteer_internal.h" // CmnAiSteer::InlineFastLength
#include "common_net.h"             // Watchable

#include <cfloat>
#include <cstring>
#include <new>

// See common_iustategraph.h for the full cluster writeup: what the container
// is, which of the previous disposition's three claims were wrong, and the
// per-function derivations.

namespace
{
    // The "-8 Watchable adjustment" this whole cluster is built on. A
    // BaseWatcher stores its target's Watchable sub-object, and IObjRW places
    // Watchable at its own +0x08 (`struct IObjRW : IObjInspect, IObjAffect,
    // Watchable`, common_iface.h), so subtracting 8 recovers the object.
    // IObjInspect is IObjRW's first base, which is why retail can read the
    // primary vtable straight out of the adjusted pointer (`MOV EDX,[ECX]`).
    //
    // Retail spells this out at every use as `if (p) p - 8 else 0`, then
    // re-tests the result against 0 -- a dead second test, since the only way
    // `p - 8` is null is a literal 0x8 sentinel (which BaseWatcher's
    // `Watchable* const*` constructor does guard against, see
    // common_goods.h). Reproduced as one guard.
    IObjInspect* RecoverObjInspect(Watchable const* Watched)
    {
        if (Watched == nullptr)
        {
            return nullptr;
        }
        unsigned char* const Raw = reinterpret_cast<unsigned char*>(const_cast<Watchable*>(Watched)) - 8;
        if (Raw == nullptr)
        {
            return nullptr;
        }
        return static_cast<IObjInspect*>(reinterpret_cast<IObjRW*>(Raw));
    }

    // SystemManager+0x14FC: the owner's own FAiTarget record (SystemManager::SelfTarget).
    FAiTarget* OwnerElementOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    // Common.dll.txt:244179-244205 (item 1802) and 244993-245012 (item 1805)
    // inline this identically; FUN_062e6a80 / GetCachedRange (item 1791) is
    // the out-of-line variant that also hands the pair back to a caller.
    // Slot +0x6C is IObjInspect::get_attitude_towards_symmetrical and returns
    // 0 on success; on any failure, or when either interface is null, both
    // cached values are forced to 0.0f.
    void RefreshCachedAttitude(FAiTarget* Elem)
    {
        if (Elem->bAttitudeValid != 0)
        {
            return;
        }

        IObjInspect* const RefIface = Elem->AttitudeRefIface;
        IObjInspect* const Other = Elem->Iface;

        Elem->CachedAttitude = 0.0f;
        Elem->CachedReverseAttitude = 0.0f;

        if (RefIface != nullptr && Other != nullptr)
        {
            int const Result = RefIface->get_attitude_towards_symmetrical(
                Elem->CachedAttitude, Other, Elem->CachedReverseAttitude);
            if (Result != 0)
            {
                Elem->CachedAttitude = 0.0f;
                Elem->CachedReverseAttitude = 0.0f;
            }
        }
        else
        {
            Elem->CachedAttitude = 0.0f;
            Elem->CachedReverseAttitude = 0.0f;
        }

        Elem->bAttitudeValid = 1;
    }

    // The cached-position idiom, guarded by bPosValid. One
    // get_physical_radius call fills both CachedRadius (+0x18, the first
    // pushed out-param) and CachedPos (+0x0C).
    void RefreshCachedPosition(FAiTarget* Elem)
    {
        if (Elem->bPosValid == 0)
        {
            Elem->bPosValid = 1;
            Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
        }
    }

    // The cached object-type flags, guarded by bFlagsValid. Slot +0x88.
    void RefreshCachedFlags(FAiTarget* Elem)
    {
        if (Elem->bFlagsValid == 0)
        {
            Elem->Iface->get_type(Elem->CachedFlags);
            Elem->bFlagsValid = 1;
        }
    }

    // .rdata:063a0790 == 0xBF19999A.
    float const kHostileAttitudeStrict = -0.6f;
    // .rdata:063a0794 == 0xBE4CCCCD.
    float const kHostileAttitudeLoose = -0.2f;
    // The object-type mask BuildAiTargetList scans and filters with
    // (0x7FFFFF minus bit 0x200).
    unsigned const kScanTypeMask = 0x7FFDFFu;
    // The narrower "ship-ish" mask both scan loops re-test each candidate against.
    unsigned const kShipTypeMask = 0x7F0000u;
    // .rdata:0x47C35000 -- FindClosestHostileTarget's seed distance.
    float const kMaxConsideredDistance = 100000.0f;
}

FAiTargetList* GetAiTargetList(SystemManager* Sys)
{
    // Every retail call site spells this out as `ADD reg,0x34`. Now that
    // FAiTargetList IS SystemManagerTimerTable, the named member replaces the
    // hand-rolled cast and common_systemmanager.h's
    // `offsetof(SystemManager, Timers) == 0x34` is what keeps it honest.
    return &Sys->Timers;
}

// Common.dll.c 0x062ed0d0 (FUN_062ed0d0), item 1882. Unchanged from the
// earlier pass apart from the doc comment.
unsigned ElemVectorSize(void const* VectorHeader)
{
    unsigned char const* const H = static_cast<unsigned char const*>(VectorHeader);

    int const Begin = *reinterpret_cast<int const*>(H + 0x4);
    if (Begin == 0)
        return 0;

    int const Used = *reinterpret_cast<int const*>(H + 0x8);
    return static_cast<unsigned>((Used - Begin) / static_cast<int>(kElemStride));
}

// Common.dll.c 0x062ed100 (FUN_062ed100), item 1883. Retail's
// `TEST EAX,EAX; JZ` (skip the byte copy but still advance both pointers)
// only matters if DestBegin starts null -- reproduced as-is.
void CopyElemRange(void const* SrcBegin, void const* SrcEnd, void* DestBegin)
{
    unsigned char const* Src = static_cast<unsigned char const*>(SrcBegin);
    unsigned char const* const End = static_cast<unsigned char const*>(SrcEnd);
    unsigned char* Dest = static_cast<unsigned char*>(DestBegin);

    while (Src != End)
    {
        if (Dest != nullptr)
            std::memcpy(Dest, Src, kElemStride);
        Src += kElemStride;
        Dest += kElemStride; // unconditional in retail, even on the null-Dest iteration
    }
}

// 0x0628f050, the compiler-generated destroy-a-range helper Ghidra mislabels
// `JustifyNode::Execute` (an ICF fold -- the two functions compiled to
// identical code). FAiTarget has no destructor, so retail's instance walks
// the range and does nothing per element. Kept as a real call so the two
// sites that use it stay legible rather than vanishing.
void DestroyAiTargetRange(FAiTargetVector* Self, FAiTarget* First, FAiTarget* Last)
{
    (void)Self;
    (void)First;
    (void)Last;
}

// The inlined `erase(begin(), end())` at Common.dll.txt:243866-243888. The
// `rep movsd` shift loop there is real code but provably dead: its source
// cursor is seeded from End (`MOV EAX,EDX` at 0x062e75a0, where EDX already
// holds End) and immediately compared against End, so it exits before the
// first iteration. What survives is the destroy-range call and `End = Begin`.
void ClearAiTargetVector(FAiTargetVector* Self)
{
    FAiTarget* const First = Self->Begin;
    DestroyAiTargetRange(Self, First, Self->End);
    Self->End = First;
}

// Common.dll.c 0x062e77e0 (FUN_062e77e0), item 1801, Common.dll.txt 244086-244112, all 25
// instructions. `MOV byte [EBP+0x1c],0` is bBuilt; EBX/EDX load End (+0x14) and Begin
// (+0x10); the 0x22-dword `rep movsd` loop is seeded with EAX = EBX and compared against EBX,
// so it never runs; `MOV [EBP+0x14],EDX` stores End = Begin. Unlike ClearAiTargetVector
// above, retail makes no DestroyAiTargetRange call here.
void ResetAiTargetList(FAiTargetList* Self)
{
    Self->bBuilt = 0;
    Self->Targets.End = Self->Targets.Begin;
}

// Common.dll.c 0x062ed140 (FUN_062ed140), item 1884. MSVC 7.0's
// `std::vector<FAiTarget>::insert(Where, Count, Value)` / `_Insert_n`, all
// three branches, derived from Common.dll.txt:253207-253536.
//
// Every element move in retail is a `rep movsd` of 0x22 dwords guarded by a
// `TEST <dest>,<dest>; JZ` that skips the copy but still advances the cursor
// -- the same null-destination quirk CopyElemRange has. Since no branch here
// can reach a null cursor once the allocation succeeded, the guards are not
// reproduced individually; the memcpy calls below are unconditional.
void InsertAiTargets(FAiTargetVector* Self, FAiTarget* Where, unsigned Count, FAiTarget const& Value)
{
    // Count is retail's size_type: every compare against it is unsigned (JNC / JC / JBE).
    int const CapacityRoom = (int)(Self->Cap - Self->End);

    if ((unsigned)CapacityRoom < Count)                        // 0x062ed166 CMP EDX,ESI; JNC
    {
        // --- Branch 1 (0x062ed172): reallocate. ---
        unsigned Size = 0;
        unsigned Growth = Count;
        if (Self->Begin != nullptr)
        {
            Size = (unsigned)(Self->End - Self->Begin);
            // 0x062ed18e `CMP ESI,EDX; MOV ECX,EDX; JC` -- keep Size when
            // Count < Size, else take Count. i.e. max(Size, Count).
            if (Count < Size)
            {
                Growth = Size;
            }
        }

        int const NewCapacity = (int)(Size + Growth);
        int const AllocCount = (NewCapacity >= 0) ? NewCapacity : 0;
        FAiTarget* const NewBuf = static_cast<FAiTarget*>(
            ::operator new(static_cast<std::size_t>(AllocCount) * kElemStride));

        // [Begin, Where) -> NewBuf
        FAiTarget* Dest = NewBuf;
        for (FAiTarget* Src = Self->Begin; Src != Where; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }
        // Count copies of Value
        for (unsigned Filled = 0; Filled < Count; ++Filled, ++Dest)
        {
            std::memcpy(Dest, &Value, kElemStride);
        }
        // [Where, End) -> after the inserted block
        for (FAiTarget* Src = Where; Src != Self->End; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }

        // 0x062ed279: raw operator delete (0x06391d5a), no element
        // destructors -- retail does not call the destroy-range helper here.
        ::operator delete(Self->Begin);

        Self->Cap = NewBuf + NewCapacity;
        // 0x062ed295: recomputed from the OLD Begin/End, which are still in
        // place at this point.
        unsigned OldSize = 0;
        if (Self->Begin != nullptr)
        {
            OldSize = (unsigned)(Self->End - Self->Begin);
        }
        Self->Begin = NewBuf;
        Self->End = NewBuf + (OldSize + Count);
        return;
    }

    unsigned const TailLength = (unsigned)(Self->End - Where);

    if (TailLength < Count)                                     // 0x062ed2ec CMP EDX,ESI; JNC
    {
        // --- Branch 2 (0x062ed2d3): in place, but more new elements than
        // existing tail elements. ---
        FAiTarget* Dest = Where + Count;
        for (FAiTarget* Src = Where; Src != Self->End; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }

        // 0x062ed336: fill the (Count - TailLength) slots that land past the
        // old End, then 0x062ed374: fill [Where, old End).
        FAiTarget* Fill = Self->End;
        for (unsigned Extra = Count - TailLength; Extra != 0; --Extra, ++Fill)
        {
            std::memcpy(Fill, &Value, kElemStride);
        }
        for (FAiTarget* Slot = Where; Slot != Self->End; ++Slot)
        {
            std::memcpy(Slot, &Value, kElemStride);
        }

        Self->End += Count;
        return;
    }

    // --- Branch 3 (0x062ed3ae): in place, enough existing tail elements. ---
    if (Count == 0)
    {
        return; // retail's `TEST ESI,ESI; JBE` straight to the epilogue
    }

    // Move the last Count elements past End, then shift the remainder of the
    // tail right (backwards, to stay overlap-safe), then fill the hole.
    FAiTarget* Dest = Self->End;
    for (FAiTarget* Src = Self->End - Count; Src != Self->End; ++Src, ++Dest)
    {
        std::memcpy(Dest, Src, kElemStride);
    }

    FAiTarget* Back = Self->End;
    FAiTarget* Src = Self->End - Count;
    while (Src != Where)
    {
        --Src;
        --Back;
        std::memcpy(Back, Src, kElemStride);
    }

    for (FAiTarget* Slot = Where; Slot != Where + Count; ++Slot)
    {
        std::memcpy(Slot, &Value, kElemStride);
    }

    Self->End += Count;
}

// Common.dll.c 0x062e7580 (FUN_062e7580), item 1800. See the declaration in
// common_iustategraph.h for the step-by-step derivation and for the two
// disproved blockers this replaces.
FAiTargetVector* BuildAiTargetList(FAiTargetList* Self)
{
    FAiTargetVector* const Vec = &Self->Targets;

    // 0x062e7595: a non-persistent list is thrown away before anything else.
    if (Self->bKeepCache == 0)
    {
        ClearAiTargetVector(Vec);
    }

    // 0x062e75d3: a persistent list that has already been built stands.
    if (Self->bBuilt != 0 && Self->bKeepCache != 0)
    {
        return Vec;
    }

    SystemManager* const Sys = Self->Owner;
    Self->bBuilt = 1;

    // Retail's local_9c, zeroed at 0x062e75ec and then filled as
    // get_scanlist's out-parameter at 0x062e7673. (Ghidra's XREF list shows
    // only the zeroing because the write goes through a LEA'd address -- that
    // omission is what the old "null-pointer dereference" blocker was
    // actually looking at.)
    ScanList const* List = nullptr;

    IObjInspect* const OwnerObj = RecoverObjInspect(Sys->Watcher.target());
    if (OwnerObj == nullptr)
    {
        return Vec;
    }

    // 0x062e760a-0x062e761f: retail masks an UNINITIALISED stack local
    // (`Flags &= 0xF8`, `EquipFlags &= 0xE0000000`) rather than zeroing it --
    // the inlined EquipStatus default constructor clearing its bitfields. The
    // only bit anyone reads here is 0x02, which that mask clears, so a full
    // zero-init is equivalent and avoids reading indeterminate bytes.
    EquipStatus Status;
    Status.Flags = 0;
    Status.Pad01 = 0;
    Status.Pad02 = 0;
    Status.Pad03 = 0;
    Status.EquipFlags = 0;
    OwnerObj->get_equipment_status(Status, Self->ScannerEquipId);
    if ((Status.Flags & EquipStatusFlagDestroyed) != 0)
    {
        return Vec;
    }

    // 0x062e764d-0x062e7673: retail re-reads the watcher and, unlike the first read, calls
    // get_scanlist through the adjusted pointer without testing it.
    IObjInspect* const OwnerObjAgain = RecoverObjInspect(Sys->Watcher.target());
    if (OwnerObjAgain->get_scanlist(List, kScanTypeMask, Self->bKeepCache != 0) != 0)
    {
        return Vec;
    }

    // 0x062e7689-0x062e771c: reserve exactly List->Count elements when the
    // current CAPACITY (not the size) falls short. Hand-rolled, not a
    // vector::reserve call.
    int const Wanted = List->Count;
    int Capacity = 0;
    if (Vec->Begin != nullptr)
    {
        Capacity = static_cast<int>(Vec->Cap - Vec->Begin);
    }
    // 0x062e76b7 `CMP EDX,EDI; JNC` -- an unsigned compare.
    if ((unsigned)Capacity < (unsigned)Wanted)
    {
        int const AllocCount = (Wanted >= 0) ? Wanted : 0;
        FAiTarget* const NewBuf = static_cast<FAiTarget*>(
            ::operator new(static_cast<std::size_t>(AllocCount) * kElemStride));

        CopyElemRange(Vec->Begin, Vec->End, NewBuf);
        DestroyAiTargetRange(Vec, Vec->Begin, Vec->End);
        ::operator delete(Vec->Begin);

        Vec->Cap = NewBuf + Wanted;
        // 0x062e770c: ElemVectorSize still sees the OLD Begin/End here.
        unsigned const OldSize = ElemVectorSize(Vec);
        Vec->End = NewBuf + OldSize;
        Vec->Begin = NewBuf;
    }

    // 0x062e771f-0x062e77c0: one pass over the scanner's result table.
    // List->Count is re-read every iteration in retail (0x062e77b3), so it is
    // re-read here too.
    for (int Index = 0; Index < List->Count; ++Index)
    {
        IObjInspect* const Candidate = RecoverObjInspect(List->Entries[Index].target());
        if (Candidate == nullptr)
        {
            continue;
        }

        // Retail builds this record on its own stack and leaves everything
        // except the six fields below indeterminate; the four guard bytes
        // make that harmless, but the port zero-fills to stay defined.
        FAiTarget Local;
        std::memset(&Local, 0, sizeof(Local));
        Local.AttitudeRefIface = RecoverObjInspect(Self->Owner->Watcher.target());
        Local.Iface = Candidate;
        Local.bAttitudeValid = 0;
        Local.bFlagsValid = 0;
        Local.bPosValid = 0;
        Local.bCornersValid = 0;

        Candidate->get_type(Local.CachedFlags);
        Local.bFlagsValid = 1;   // set before the filter branch in retail, not after
        if ((Local.CachedFlags & kScanTypeMask) == 0)
        {
            continue;
        }

        InsertAiTargets(Vec, Vec->End, 1, Local);
    }

    return Vec;
}

// Common.dll.c 0x062e7820 (FUN_062e7820), item 1802 -- the real body of
// IBehaviorManager::get_closest_trailing_ship (item 12392). Derived from
// Common.dll.txt:244122-244302.
float GetClosestTrailingShipDistance(FAiTargetList* Self)
{
    // 0x062e783c: seeded with 0x7F7FFFFF == FLT_MAX and returned unchanged
    // when nothing qualifies.
    float Best = FLT_MAX;

    // 0x062e7826-0x062e782d: `MOV ECX,[EBX+4]; ADD ECX,0x34` -- the list is reached through
    // the owner, not through `this`.
    FAiTargetVector* const Vec = BuildAiTargetList(&Self->Owner->Timers);
    SystemManager* const Sys = Self->Owner;
    FAiTarget* const OwnerElem = OwnerElementOf(Sys);

    for (FAiTarget* Elem = Vec->Begin; Elem != Vec->End; ++Elem)
    {
        RefreshCachedFlags(Elem);
        if ((Elem->CachedFlags & kShipTypeMask) == 0)
        {
            continue;
        }

        // 0x062e7882, slot +0x0BC.
        if (Elem->Iface->is_player())
        {
            continue;
        }

        // 0x062e789d, slot +0x074 = get_target. This is the "unidentified
        // 'resolve target interface' out-param getter" the old blocker_reason
        // listed; it is a plain IObjInspect slot.
        IObjRW* CandidateTarget = nullptr;
        Elem->Iface->get_target(CandidateTarget);
        if (CandidateTarget == nullptr)
        {
            continue;
        }

        // 0x062e78be / 0x062e78c7, slot +0x020 = get_id, called on the
        // owner's own interface and on the candidate's target. Equal ids mean
        // the candidate is currently targeting us.
        unsigned const OwnerId = OwnerElem->Iface->get_id();
        unsigned const CandidateTargetId = static_cast<IObjInspect*>(CandidateTarget)->get_id();
        if (CandidateTargetId != OwnerId)
        {
            continue;
        }

        // 0x062e78df, cdecl (Candidate, OwnerElement, 0.0f).
        if (!IsTargetCoAlignedAndAhead(Elem, OwnerElem, 0.0f))
        {
            continue;
        }

        RefreshCachedAttitude(Elem);
        if (!(Elem->CachedAttitude <= kHostileAttitudeLoose))
        {
            continue;
        }

        // 0x062e7953 reads SystemManager+0x157A as the guard -- that is
        // exactly OwnerElem->bPosValid (0x14FC + 0x7E).
        RefreshCachedPosition(OwnerElem);
        RefreshCachedPosition(Elem);

        // 0x062e7998-0x062e79ef: retail's inlined 0x5F3759DF Newton step. The deltas, the
        // (dz^2 + dy^2) + dx^2 sum, the Newton step and the final product stay on the x87
        // stack; only the bit-hack input is stored as float (FST [ESP+0x18]).
        double const Dx = (double)Elem->CachedPos.x - OwnerElem->CachedPos.x;
        double const Dy = (double)Elem->CachedPos.y - OwnerElem->CachedPos.y;
        double const Dz = (double)Elem->CachedPos.z - OwnerElem->CachedPos.z;
        double const DistanceSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        // 0x5f3759df - (bits SAR 1) on the float image, then ((Y*Y) * d2) * 0.5f
        // (DAT_0639c13c), 1.5f (DAT_0639e6d0) minus that, * Y, * d2 -- all unrounded.
        double const Distance = CmnAiSteer::InlineFastLength(DistanceSq);

        // 0x062e79f1 `FCOM Best; TEST AH,5; JP` -- only strictly less is taken.
        if (Distance < Best)
        {
            Best = (float)Distance;
        }
    }

    return Best;
}

// Common.dll.c 0x062e8280 (FUN_062e8280), item 1805. Derived from
// Common.dll.txt:244955-245102. Note this one IS a method of FAiTargetList
// itself -- its call site passes `ECX` through unchanged (0x062e82b8
// `MOV ECX,EBX`), no +0x34 adjustment.
IObjInspect* FindClosestHostileTarget(FAiTargetList* Self, float MaxClearance)
{
    IObjInspect* BestIface = nullptr;
    // 0x062e8299 seeds this with 0x47C35000 == 100000.0f, so anything farther
    // is never returned.
    float BestDistance = kMaxConsideredDistance;

    // 0x062e8286-0x062e82b5: the owner's own world position, fetched before
    // the list is (re)built.
    IObjInspect* const OwnerObj = RecoverObjInspect(Self->Owner->Watcher.target());
    Vector OwnerPosition;
    GetIfacePositionOrZero(&OwnerPosition, OwnerObj);

    FAiTargetVector* const Vec = BuildAiTargetList(Self);
    FAiTarget* const OwnerElem = OwnerElementOf(Self->Owner);

    for (FAiTarget* Elem = Vec->Begin; Elem != Vec->End; ++Elem)
    {
        RefreshCachedFlags(Elem);
        if ((Elem->CachedFlags & kShipTypeMask) == 0)
        {
            continue;
        }

        RefreshCachedAttitude(Elem);
        if (!(Elem->CachedAttitude <= kHostileAttitudeStrict))
        {
            continue;
        }

        // 0x062e835b: a negative MaxClearance skips the clearance gate
        // entirely ("nearest hostile at any range").
        if (!(MaxClearance < 0.0f))
        {
            if (!(GetInterceptClearance(OwnerElem, Elem) < MaxClearance))
            {
                continue;
            }
        }

        RefreshCachedPosition(Elem);

        // 0x062e83ac: Distance3 (FUN_062e4f20, item 1752) -- which DOES have
        // a tracker item, contrary to this item's old blocker_reason. 062e83b1-062e83c5:
        // the UNROUNDED ST0 is FCOM'd against the float best (TEST AH,0x5 / JP: only
        // strictly less takes it) and FSTP'd to float only then.
        double const Distance = Distance3(&OwnerPosition, &Elem->CachedPos);
        if (Distance < BestDistance)
        {
            BestDistance = (float)Distance;
            BestIface = Elem->Iface;
        }
    }

    return BestIface;
}
