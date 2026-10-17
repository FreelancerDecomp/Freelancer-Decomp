// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ScanList, CEScanner -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::GetBucket / GetNext (ScanList::add_in_range)
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect)
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_cequip_internal.h"

using namespace CmnCequip;

// ============================================================
// CEScanner — scanner equipment (listing 0x3f4e0..0x3fbb4)
// ============================================================

namespace
{
    const float kScannerOne = 1.0f;

    // IObjRW's Watchable base lives at +8: every ScanList member converts with
    // `Obj ? Obj + 8 : 0` before BaseWatcher::set_pointer (.text:062c26a0-062c26a7 in add,
    // 062c26f0-062c26f9 in remove, 062c2808-062c2817 in add_in_range).
    Watchable const* ScannerWatchableOf(IObjRW* Obj)
    {
        return Obj;   // the IObjRW -> Watchable base conversion keeps null as null
    }
}

// ============================================================
// ScanList
// ============================================================

// .text:062c2610: every BaseWatcher built with the null-target ctor (FUN_062c2920(0) through
// the array-ctor helper), then Count = 0, Capacity = 0x100.
ScanList::ScanList()
    : Count(0)
    , Capacity(0x100)
{
}

// .text:062948b0: for each of the 256 entries, zero it and, when the source watches something
// (and that something is not the -8 sentinel), set_pointer to it -- the BaseWatcher copy
// constructor, inlined; then Count and Capacity.
ScanList::ScanList(ScanList const& Other)
    : Count(Other.Count)
    , Capacity(Other.Capacity)
{
    // .text:062948e6-062948fc: source target taken only when non-null and not the 8
    // sentinel (a null IObjRW + 8).
    for (int Index = 0; Index < 256; ++Index)
    {
        Watchable const* SrcWatched = Other.Entries[Index].target();
        if (SrcWatched != nullptr && SrcWatched != (Watchable const*)8)
            Entries[Index].set(SrcWatched);
    }
}

// .text:06294960: per entry, the source target (null when null or the 8 sentinel,
// .text:06294973-06294986), set_pointer only when it differs from the current one
// (.text:06294988), then Count and Capacity. Written out rather than through
// BaseWatcher::operator=, which retail does not call here: that export (.text:06280ee0)
// is a raw two-dword copy with no watcher-chain maintenance.
ScanList& ScanList::operator=(ScanList const& Other)
{
    for (int Index = 0; Index < 256; ++Index)
    {
        Watchable const* SrcWatched = Other.Entries[Index].target();
        if (SrcWatched == (Watchable const*)8)
            SrcWatched = nullptr;
        Entries[Index].set(SrcWatched);
    }
    Count = Other.Count;
    Capacity = Other.Capacity;
    return *this;
}

// .text:062c2660: only the bookkeeping. The entries keep their watch links -- a slot beyond
// the new Count is overwritten by the next add, and a destroyed target unlinks itself through
// its own Watchable chain whatever Count says.
void ScanList::reset()
{
    Count = 0;
    Capacity = 0x100;
}

// .text:062c2680: when Count < Capacity, take slot Count, bump Count, point it at Obj
// (set_pointer skipped when unchanged) and return true; else false.
bool ScanList::add(IObjRW* Obj)
{
    if (Count >= Capacity)
        return false;
    int Index = Count;
    ++Count;
    Entries[Index].set(ScannerWatchableOf(Obj));
    return true;
}

// .text:062c26c0: erase-remove compaction over [0, Count). An entry whose watched IObjRW
// (Watched - 8) differs from Obj is written to the write cursor, which only advances on a
// keep; Count becomes the number of survivors. Entries beyond it keep their old links.
void ScanList::remove(IObjRW* Obj)
{
    int WriteIndex = 0;
    for (int ReadIndex = 0; ReadIndex < Count; ++ReadIndex)
    {
        Watchable const* Watched = Entries[ReadIndex].target();
        IObjRW* WatchedOwner = (Watched != nullptr)
            ? const_cast<IObjRW*>(static_cast<IObjRW const*>(Watched))
            : nullptr;
        if (WatchedOwner != Obj)
        {
            // .text:062c26f0-062c26f9: the kept entry is rebuilt from the owner, so a watcher
            // on the 8 sentinel (owner null) is written back as null.
            Entries[WriteIndex].set(ScannerWatchableOf(WatchedOwner));
            ++WriteIndex;
        }
    }
    Count = WriteIndex;
}

// .text:062c2720-062c2916 (Ghidra's decompilation is wrecked by the __chkstk probe for the
// 0x2030-byte frame; this follows the listing).
//
// Two-phase enumeration against Db, each candidate gated through Filt->add_obj (slot 0,
// called unconditionally -- no null check on Filt) before it is appended:
//   Phase 1 (TypeMask & 0xfdff): Db slot 3 (+0x0c) with 0xfdff returns a bucket; the bucket's
//     slot 7 (+0x1c) fills a caller-owned {Vector pos; float range; int capacity; int count;
//     void* data[2048];} with up to `capacity` range-limited candidates (0x800*4 + header =
//     the 0x2030-byte frame).
//   Phase 2 (TypeMask & 0xffff0200): Db slot 0 get-first/get-next walk; distance checked here
//     against Range^2 (FCOMP / TEST AH,5 / JP skip -- i.e. strictly closer than Range).
// Phase 1 stops at a full list and falls through to phase 2 (.text:062c27ec -> 062c2842);
// phase 2 returns at a full list (.text:062c28bd -> 062c2908). Returns the number appended.
int ScanList::add_in_range(Vector const& Pos, float Range, IObjDB* Db, unsigned TypeMask,
                           Filter* Filt)
{
    float RangeSq = Range * Range;
    int Added = 0;

    // .text:062c276a: IObjDB::GetBucket (+0x0c) with 0xfdff; .text:062c27a9: the bucket's
    // ISpatialPartition::QuerySphere (+0x1c); .text:062c2859/062c28fa: IObjDB::GetNext (+0x00).
    if (TypeMask & 0xfdffu)
    {
        void* Bucket = Db->GetBucket(0xfdffu);
        if (Bucket != nullptr)
        {
            // The 0x2030-byte frame: the {Pos, Range} sphere, then the {0x800, 0, items} result.
            Geometry::Sphere Sphere;
            Sphere.Center = Pos;
            Sphere.Radius = Range;
            FSpatialQueryResult Query;
            Query.Capacity = 0x800;
            Query.Count = 0;

            int Result = ((ISpatialPartition*)Bucket)->QuerySphere(&Sphere, &Query);
            if (Result != 0 && Query.Count != 0)
            {
                for (unsigned Index = 0; Index < Query.Count; ++Index)
                {
                    IObjRW* Candidate = (IObjRW*)Query.Items[Index];
                    if (Count >= Capacity)
                        break;
                    if (Filt->add_obj(Candidate))
                    {
                        add(Candidate);
                        ++Added;
                    }
                }
            }
        }
    }

    struct FIterState
    {
        unsigned Unused0;
        unsigned Unused1;
        unsigned Mask;
    } IterState = { 0, 0, TypeMask & 0xffff0200u };

    IObjRW* Candidate = Db->GetNext(&IterState);
    while (Candidate != nullptr)
    {
        Vector const* CandPos = &Candidate->get_position();

        // .text:062c2879-062c28af: the deltas and the sum stay on the FPU stack, summed
        // (x^2 + y^2) + z^2, then FCOMP against the float RangeSq; TEST AH,5 + JP skips the
        // candidate for >= or unordered, so only an ordered "closer" falls through.
        double Dx = (double)CandPos->x - Pos.x;
        double Dy = (double)CandPos->y - Pos.y;
        double Dz = (double)CandPos->z - Pos.z;
        double DistSq = (Dx * Dx + Dy * Dy) + Dz * Dz;
        if (DistSq < RangeSq)
        {
            if (Count >= Capacity)
                return Added;
            if (Filt->add_obj(Candidate))
            {
                add(Candidate);
                ++Added;
            }
        }

        Candidate = Db->GetNext(&IterState);
    }

    return Added;
}

namespace
{
    // .text:0629f64d..0629f66d / 0629f742..0629f75f: a static CSolar gets the
    // restricted mask. Class 0x303 and byte +0x1a9 are CSolar and bDynamic.
    bool OwnerIsStaticSolar(CEqObj const* Owner)
    {
        if (Owner == nullptr)
            return false;
        if ((Owner->ObjectClass & 0x303u) != 0x303u)
            return false;
        return !((CSolar const*)Owner)->bDynamic;
    }
}

// .text:0629f4e0: CEquip(TypeScanner=0x80000, ...), ScanList::ScanList at +0x24,
// bMissionFlag at +0x82c, cargo vector and cached mask zeroed, both tables stamped
// (.text:0629f55d/0629f563), CEquip::Activate(true).
CEScanner::CEScanner(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Scanner const* ArchObj,
                     bool bMissionFlag)
    : CInternalEquip(EquipTypeScanner, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    this->bMissionFlag = bMissionFlag;   // 0629f532: the argument byte
    CargoVecBegin = nullptr;
    CargoVecEnd = nullptr;
    CargoVecCapacityEnd = nullptr;
    CachedTypeMask = 0;
    CEquip::Activate(true);
}

// .text:06294be0: the CEquip fields copied member by member (+0x04..+0x1c, the implicit copy
// constructor), the ScanList copy (its per-entry set_pointer loop inlined at 06294c60), the
// mission byte, the cargo vector copy, the cached mask.
CEScanner::CEScanner(CEScanner const& Other)
    : CInternalEquip(Other)
    , ScanList::Filter(Other)
    , Scans(Other.Scans)
    , bMissionFlag(Other.bMissionFlag)
{
    // .text:06294cc8-06294d5a: the VC6 vector<EquipDesc> copy. The block is sized exactly
    // (a negative size clamps to 0) and allocated even for an empty source; End and the
    // capacity end both land after the last copied element.
    int SrcSize = (Other.CargoVecBegin == nullptr) ? 0 : (int)(Other.CargoVecEnd - Other.CargoVecBegin);
    if (SrcSize < 0)
        SrcSize = 0;
    CargoVecBegin = new EquipDesc[(unsigned)SrcSize];
    EquipDesc* Dest = CargoVecBegin;
    for (EquipDesc const* Src = Other.CargoVecBegin; Src != Other.CargoVecEnd; ++Src, ++Dest)
        *Dest = *Src;
    CargoVecEnd = Dest;
    CargoVecCapacityEnd = Dest;

    CachedTypeMask = Other.CachedTypeMask;
}

// Listing 0x3f590: Destroy(), free cargo vector, reset ScanList, ~CEquip.
CEScanner::~CEScanner()
{
    CEquip::Destroy();

    // Free cargo results vector (matching delete[] at 0x3f5ce).
    delete[] CargoVecBegin;
    CargoVecBegin = nullptr;
    CargoVecEnd = nullptr;
    CargoVecCapacityEnd = nullptr;

    // Retail destroys the BaseWatcher[256] array here (0x3f5dd) -- ~ScanList, which the
    // compiler runs on the Scans member after this body.

    // Base ~CEquip called implicitly via CInternalEquip dtor chain.
}

// .text:0629f630..0629f6c2: functioning/database checks, static-solar mask,
// uncached types, zone-adjusted range query, then cache OR.
void CEScanner::scan_for_types(unsigned TypeMask)
{
    // The raw vtable+0xc call here resolves to CEquip::IsFunctioning (IsActive() &&
    // !IsDisabled() && !IsDestroyed()), not plain IsActive() -- confirmed against
    // CEquip's own vtable layout (rdata 0x639bdcc). A destroyed-but-still-flagged-active
    // scanner must not keep re-scanning.
    if (!IsFunctioning())
        return;
    if (Owner->ObjectDb == nullptr)
        return;

    unsigned EffectiveMask = TypeMask;
    if (OwnerIsStaticSolar(Owner))
        EffectiveMask = TypeMask & 0xdfff0200u;

    // NOT the cached mask, AND with effective mask → types we haven't scanned yet.
    unsigned const NewTypes = EffectiveMask & ~CachedTypeMask;

    // Compute range: (1.0 - get_scanner_interference()) * Arch->RadarRange.
    // 0629f695..0629f6a7: the subtraction/product stay on the FPU until
    // the range argument is stored as a float. Even a zero type mask queries Db.
    double const Interference = Owner->get_scanner_interference();
    float const Range = (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);

    // Get owner position for range query.
    Vector const& OwnerPos = Owner->WorldPosition;

    Scans.add_in_range(OwnerPos, Range, Owner->ObjectDb, NewTypes, this);

    CachedTypeMask |= NewTypes;
}

// .text:0629f6d0: ScanList::reset on +0x24, zero cached mask.
void CEScanner::clear_cache()
{
    Scans.reset();
    CachedTypeMask = 0;
}

// Listing 0x3f6f0 (raw .txt 119924-120000): reset scan list, check IsFunctioning
// (vtable+0xc), check caller-supplied Flags bit 0x4 (skip scanning this tick when set --
// e.g. a paused/frozen-simulation update pass), check IsFunctioning again, check
// Owner+0xac, compute interference-adjusted range, call add_in_range, OR cached mask,
// then CEquip::Update.
bool CEScanner::Update(float Dt, unsigned Flags)
{
    // Reset scan state each frame.
    Scans.reset();
    CachedTypeMask = 0;

    // The two vtable+0xc calls below resolve to CEquip::IsFunctioning, not IsActive() --
    // see scan_for_types' own comment above for the vtable-layout evidence.
    if (!IsFunctioning())
        goto Done;

    // Raw asm (119943-119945): TEST dword ptr [rdata constant == 4], Flags; JNZ Done.
    // I.e. skip the whole scan this tick when Flags has bit 0x4 set.
    if ((Flags & 4u) != 0)
        goto Done;

    if (!IsFunctioning())
        goto Done;

    if (Owner->ObjectDb == nullptr)
        goto Done;

    {
        unsigned EffectiveMask = 0x357ffdffu;
        if (OwnerIsStaticSolar(Owner))
            EffectiveMask = 0x157f0000u;

        unsigned NewTypes = EffectiveMask & ~CachedTypeMask;

        // 0629f77e..0629f790: actual zone interference, one final range spill.
        double const Interference = Owner->get_scanner_interference();
        float const Range = (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);

        Vector const& OwnerPos = Owner->WorldPosition;

        Scans.add_in_range(OwnerPos, Range, Owner->ObjectDb, NewTypes, this);

        CachedTypeMask |= NewTypes;
    }

Done:
    return CEquip::Update(Dt, Flags);
}

// .text:0629f7c0: slot 0 of CEScanner's ScanList::Filter table. Retail's `this` is the +0x20
// Filter sub-object and the body recovers the CEScanner with this-0x20; the compiler does that
// adjustment for us now that Filter is a real base. Self/untargetable objects are rejected;
// distance needs another check only when the target has stronger interference than the owner.
bool CEScanner::add_obj(IObjRW* Obj)
{
    if (Obj->get_id() == Owner->Id)
        return false;
    if (!Obj->is_targetable())
        return false;

    float const TargetInterference = Obj->get_scanner_interference();
    double const OwnerInterference = Owner->get_scanner_interference();
    // 0629f80c..0629f815: FCOMP owner/target; TEST AH,5 + JP returns true
    // for owner >= target or unordered. add_in_range already applied the owner's range.
    if (!(OwnerInterference < TargetInterference))
        return true;

    // 0629f817..0629f831: use target interference, then FSTP float range.
    float const Range = (float)(((double)(kScannerOne) - TargetInterference) *
                                          ScannerArch()->RadarRange);
    Vector const& ObjPos = Obj->get_position();
    Vector const& OwnerPos = Owner->WorldPosition;
    double const Dx = (double)(ObjPos.x) - OwnerPos.x;
    double const Dy = (double)(ObjPos.y) - OwnerPos.y;
    double const Dz = (double)(ObjPos.z) - OwnerPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    // 0629f85f..0629f86c: FCOMPP range^2/distance^2, TEST AH,5 + JP
    // accepts >= or unordered; only the ordered-less fall-through rejects.
    return !((double)(Range) * Range < DistSq);
}

// .text:0629f880..0629f896: zone-adjusted interference, FSUBR one, FMUL radar range.
float CEScanner::GetRadarRange() const
{
    double const Interference = Owner->get_scanner_interference();
    return (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);
}

// .text:0629f8a0..0629f8b6: same zone-adjusted range, archetype cargo range at +0x78.
float CEScanner::GetCargoScanRange() const
{
    double const Interference = Owner->get_scanner_interference();
    return (float)((kScannerOne - Interference) * ScannerArch()->CargoScanRange);
}

// .text:0629f8c0..0629fb83: clear the previous result, check range/power, then
// append each target equipment item's virtual GetEquipDesc output verbatim.
CEScanner::ScanResult CEScanner::ScanCargo(CEqObj* Target)
{
    // 0629f8c8..0629f8f3: copy(End, End, Begin) returns Begin; the ICF-folded
    // _Destroy at 0628f050 is empty for EquipDesc. Capacity is retained even on failure.
    CargoVecEnd = CargoVecBegin;
    if (Target == nullptr)
        return ScanNoTarget;

    // 0629f90e..0629f932: target interference is FSTP float; owner stays on the
    // FPU stack. FCOMP target/owner, TEST AH,0x41 + JNZ keeps owner when target
    // is <= owner or unordered; the greater fall-through replaces it with target.
    float const TargetInterference = Target->get_scanner_interference();
    double Interference = (double)(Owner->get_scanner_interference());
    if (TargetInterference > Interference)
        Interference = TargetInterference;

    Archetype::Scanner const* const Scanner = ScannerArch();
    double const Range = (kScannerOne - Interference) * Scanner->CargoScanRange;
    Vector const& TargetPos = Target->WorldPosition;
    Vector const& OwnerPos = Owner->WorldPosition;
    double const Dx = (double)(TargetPos.x) - OwnerPos.x;
    double const Dy = (double)(TargetPos.y) - OwnerPos.y;
    double const Dz = (double)(TargetPos.z) - OwnerPos.z;
    double const DistSq = (Dy * Dy + Dz * Dz) + Dx * Dx;
    // 0629f967..0629f980: FCOMPP range^2/distance^2; TEST AH,0x05 + JP
    // continues for >= or unordered. Only the ordered-less fall-through returns 0.
    if (Range * Range < DistSq)
        return ScanTooFar;

    // 0629f98e..0629f995: FCOMP threshold/power; TEST AH,0x41 + JP returns 1
    // for greater or unordered; ordered <= falls through to the subtraction.
    float const Threshold = Scanner->PowerThreshold;
    if (!(Threshold <= Owner->Power))
        return ScanNotEnoughPower;
    Owner->Power = (float)((double)(Owner->Power) - Threshold);

    CEquipTraverser Walk(-1);
    CEquip* Equip = Target->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        // 0629f9d4..0629f9e7: vtable+0x28 is GetEquipDesc(&Desc), returning bool.
        // CECargo supplies its real stack count; mounted equipment supplies count 1.
        EquipDesc Desc;
        if (!Equip->GetEquipDesc(Desc))
        {
            Equip = Target->EquipManager.Traverse(Walk);
            continue;
        }

        // The insertion position is always End, so the inlined vector::insert
        // tail-copy ranges are empty. Growth at 0629fa03..0629fa24 is size+max(size,1).
        if (CargoVecEnd == CargoVecCapacityEnd)
        {
            unsigned const CurSize = CargoVecBegin == nullptr ? 0 :
                (unsigned)(CargoVecEnd - CargoVecBegin);
            unsigned const NewCapacity = CurSize + (CurSize > 1 ? CurSize : 1);
            EquipDesc* const NewBuf = new EquipDesc[NewCapacity];
            for (unsigned Index = 0; Index < CurSize; ++Index)
                NewBuf[Index] = CargoVecBegin[Index];
            NewBuf[CurSize] = Desc;
            delete[] CargoVecBegin;
            CargoVecBegin = NewBuf;
            CargoVecEnd = NewBuf + CurSize + 1;
            CargoVecCapacityEnd = NewBuf + NewCapacity;
        }
        else
        {
            *CargoVecEnd = Desc;
            ++CargoVecEnd;
        }

        Equip = Target->EquipManager.Traverse(Walk);
    }

    return ScanComplete;
}

// Listing 0x3fb90: ICF-folded with many other *Arch methods. Returns this->Arch.
Archetype::Scanner const* CEScanner::ScannerArch() const
{
    return (Archetype::Scanner const*)(Arch);
}

// Listing 0x3fba0: type == 0x80000 → static_cast. Both const and non-const overloads
// share the same entry point in retail.
CEScanner* __cdecl CEScanner::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeScanner)
        return nullptr;
    return (CEScanner*)(Equip);
}

CEScanner const* __cdecl CEScanner::cast(CEquip const* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeScanner)
        return nullptr;
    return (CEScanner const*)(Equip);
}
