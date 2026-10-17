// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll Exclusion: AsteroidExclusionZone / NebulaExclusionZone and their managers, with the
// managers' VC6-vector and sort helpers. Split out of common_net.cpp.

#include "common_net.h"
#include "common_id.h" // CreateID for NebulaExclusionZone::set_zone_shell_name

#include "dacom.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// Exclusion namespace
// ===========================================================================

namespace Exclusion
{

// --- AsteroidExclusionZone ---

AsteroidExclusionZone::AsteroidExclusionZone()
    : ZonePtr(nullptr)
    , MaxDistSq(10000000.0f)   // 0x4b189680 == 1e7 (was mis-decoded as 100000.0f previously)
    , bExcludeBillboards(0)
    , bExcludeDynamicAsteroids(0)
    , EmptyCubeFrequency(-1.0f)       // 0xbf800000
    , BillboardCount(-1.0f)
{
}

void AsteroidExclusionZone::update_dist(const Vector& Pos)
{
    // .text:062bc4a0 (Common.dll.c 0x5c4a0).
    if (ZonePtr == nullptr)
        return;

    // .text:062bc4b0-062bc4d2: vt+0x18 (shape) and a copy of vt+0x10 (position).
    unsigned const Shape = ZonePtr->GetShape();
    Vector const ZonePos = ZonePtr->GetPos();

    float const Dx = Pos.x - ZonePos.x;
    float const Dy = Pos.y - ZonePos.y;
    float const Dz = Pos.z - ZonePos.z;
    float Len = 0.0f;
    double Radius = 0.0;

    if (Shape == Universe::ZoneShapeSphere)
    {
        // LAB_062bc584: float(sqrt((dz^2 + dy^2) + dx^2)), then vt+0x30.
        Len = (float)(sqrt(((double)(Dz) * Dz + (double)(Dy) * Dy) +
                                      (double)(Dx) * Dx));
        Radius = ZonePtr->GetRadius();
    }
    else if (Shape == Universe::ZoneShapeEllipsoid)
    {
        // .text:062bc4ee-062bc57f: float(sqrt((dz^2 + dx^2) + dy^2)); the unit direction is
        // each delta times 1.0f / that float (on the stack), stored as floats, then vt+0x40.
        // There is no zero-length test.
        Len = (float)(sqrt(((double)(Dz) * Dz + (double)(Dx) * Dx) +
                                      (double)(Dy) * Dy));
        double const Inv = 1.0f / (double)(Len);
        Vector Dir;
        Dir.x = (float)(Dx * Inv);
        Dir.y = (float)(Dy * Inv);
        Dir.z = (float)(Dz * Inv);
        Radius = ZonePtr->GetRadiusAlong(Dir);
    }
    else
    {
        // .text:062bc4e0: every other shape.
        MaxDistSq = 0.0f;
        return;
    }

    // LAB_062bc5e9: Dist = Len - Radius on the stack; FCOM 0.0f / TEST 5 / JP -> the square
    // when >= 0 or unordered, else 0.
    double const Dist = Len - Radius;
    if (Dist < 0.0)
        MaxDistSq = 0.0f;
    else
        MaxDistSq = (float)(Dist * Dist);
}

AsteroidExclusionZone::~AsteroidExclusionZone()
{
    // .text:062a8920: RET.
}

Universe::IZone const* AsteroidExclusionZone::get_zone()
{
    // .text:06261d90: MOV EAX,[this]; RET.
    return ZonePtr;
}

bool AsteroidExclusionZone::set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bc620: ZonePtr = Zone; no zone -> false. Otherwise Parent's immersion (vt+0x00,
    // Parent not tested) at the zone's vt+0x10 position; FCOMP 0.0 (double .rdata:0639c430) /
    // TEST 0x41 / JNZ -> false when <= 0 or unordered.
    ZonePtr = Zone;
    if (Zone == nullptr)
        return false;
    return Parent->GetImmersion(Zone->GetPos()) > 0.0f;
}

// --- NebulaExclusionZone ---

NebulaExclusionZone::NebulaExclusionZone()
    : ZonePtr(nullptr)
    // .text:062bcbc0: +0x04 = 0x4b189680 (1e7), the same "far away" sentinel as
    // AsteroidExclusionZone's; update_dist() overwrites it with the current
    // shell-distance-squared (or 0 if inside).
    , MaxDistSq(10000000.0f)
    , FogFar(-1.0f)          // +0x08 = 0xbf800000
{
    // Retail calls ID_String::clear on +0x0c twice (before and after the stores).
    ZoneShellName.clear();
}

NebulaExclusionZone::~NebulaExclusionZone()
{
    // .text:062a8920: RET.
}

Universe::IZone const* NebulaExclusionZone::get_zone()
{
    // .text:06261d90: MOV EAX,[this]; RET.
    return ZonePtr;
}

float NebulaExclusionZone::get_fog_far()
{
    // .text:06261da0: FLD [this+0x8]; RET.
    return FogFar;
}

void NebulaExclusionZone::set_fog_far(float Far)
{
    // .text:0631d940: MOV [this+0x8],arg; RET 4.
    FogFar = Far;
}

void NebulaExclusionZone::update_dist(const Vector& Pos)
{
    // .text:062bcbf0. No zone -> nothing. The deltas to the zone's vt+0x10 position stay on
    // the stack; Length = sqrt((dz^2 + dx^2) + dy^2) stored as float; Dist = Length - vt+0x30
    // (the radius); FST float into MaxDistSq, then FCOM 0.0f / TEST 5 / JP -> MaxDistSq =
    // float(Dist^2) when >= 0 or unordered, else 0.
    if (ZonePtr == nullptr)
        return;

    Vector const ZonePos = ZonePtr->GetPos();
    double const Dx = (double)(Pos.x) - ZonePos.x;
    double const Dy = (double)(Pos.y) - ZonePos.y;
    double const Dz = (double)(Pos.z) - ZonePos.z;
    float const Length = (float)(sqrt((Dz * Dz + Dx * Dx) + Dy * Dy));
    double const Dist = Length - (double)(ZonePtr->GetRadius());
    if (Dist < 0.0)
        MaxDistSq = 0.0f;
    else
        MaxDistSq = (float)(Dist * Dist);
}

bool NebulaExclusionZone::set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // Common.dll.txt:062bcc90 (RET 8). Neither zone is null-checked.
    ZonePtr = Zone;

    // 062bcc9f-062bccc9: shape (vt+0x18) other than 1 (sphere) -> the error with the zone's
    // id (vt+0x04) as %u.
    if (Zone->GetShape() != Universe::ZoneShapeSphere)
    {
        FDUMP(0x100001,
            "%s(%d) : *** ERROR: DATA ERROR: nebula exclusion zones may only be SPHERE shaped\n %u is not a sphere zone",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x1e9, Zone->GetId());
    }

    // 062bccce-062bcce6: the parent's immersion (vt+0x00) at the zone's position (vt+0x10),
    // kept as a float.
    // 062bcce2: FST float [ESP+0x14] keeps a float copy for the return test; the 1.0 test
    // below runs on the unrounded value still on the x87 stack.
    double const Immersion = Parent->GetImmersion(Zone->GetPos());
    float const Inside = (float)Immersion;

    // 062bcce6-062bccf2: FCOMP float 1.0, TEST AH,0x01 / JZ -> the error when below 1.0 or
    // unordered (C0 set).
    if (!(Immersion >= 1.0))
    {
        // Retail passes the two zone ids (vt+0x04, a uint each) for the two %s, which makes
        // vsprintf read a string at those addresses -- undefined behaviour. The port prints
        // the same ids as ID_String text ("0x...") instead.
        ID_String ParentId;
        ParentId.Id = Parent->GetId();
        ID_String ZoneId;
        ZoneId.Id = Zone->GetId();
        char const* const ParentText = ParentId.get_string();
        char const* const ZoneText = ZoneId.get_string();
        FDUMP(0x100001,
            "%s(%d) : *** ERROR: DATA ERROR: nebula exclusion zone (%s) is not fully inside nebula zone (%s)\n",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x1f5, ZoneText, ParentText);
    }

    // 062bcd23-062bcd41: TEST AH,0x41 / JNZ -> false when <= 0.0 or unordered.
    return Inside > 0.0f;
}

ID_String NebulaExclusionZone::get_zone_shell_name()
{
    // .text:062a2db0: copies the dword at +0x0C into the hidden return slot.
    return ZoneShellName;
}

void NebulaExclusionZone::set_zone_shell_name(const char* Name)
{
    // .text:062bcd50: [this+0x0C] = CreateID(Name).
    ZoneShellName.Id = CreateID(Name);
}

// --- The managers' zone arrays ---
//
// Both managers hold a VC6 std::vector<Zone*> at +0x04 (allocator byte +0x04, first +0x08,
// last +0x0c, end +0x10). The helpers below are the vector members retail calls or inlines,
// on operator new / operator delete as VC6's allocator does.
namespace
{
    // vector<T*>::insert(where, count, value) at .text:0628e250 (ICF-shared by every pointer
    // vector) as both push_back members call it: where = last, count = 1. With no spare slot
    // (end - last < 1, unsigned) it allocates size + max(size, 1) slots -- just 1 while first
    // is null -- through operator new (a negative total clamps to 0), copies the old pointers
    // and the new one, frees the old block (operator delete) and resets first / last / end.
    // Otherwise the value goes into the next slot. (The general count / where paths of that
    // function are not reached from here.)
    template <typename TZone>
    void ZoneArrayPushBack(TZone**& First, TZone**& Last, TZone**& End, TZone* Value)
    {
        if ((unsigned)(End - Last) < 1u)
        {
            unsigned const Size = (First == nullptr) ? 0u : (unsigned)(Last - First);
            unsigned const Grow = (First == nullptr || !(1u < Size)) ? 1u : Size;
            int Capacity = (int)(Size + Grow);
            if (Capacity < 0)
                Capacity = 0;
            TZone** const Block = (TZone**)(operator new((size_t)(Capacity) * sizeof(TZone*)));
            TZone** Out = Block;
            for (TZone** In = First; In != Last; ++In, ++Out)
                *Out = *In;
            *Out = Value;
            operator delete(First);
            End = Block + Capacity;
            Last = Block + (Size + 1u);
            First = Block;
            return;
        }
        *Last = Value;
        ++Last;
    }

    // The VC6 vector<T*> copy constructor both manager copy constructors inline (.text:062a2b9e
    // / 062a2dce): the allocator byte, then size slots from operator new (a negative size
    // clamps to 0, so an empty source still allocates) and a plain pointer copy; last = end.
    template <typename TZone>
    void ZoneArrayCopyConstruct(TZone**& First, TZone**& Last, TZone**& End, TZone** OtherFirst, TZone** OtherLast)
    {
        int Count = (OtherFirst == nullptr) ? 0 : (int)(OtherLast - OtherFirst);
        if (Count < 0)
            Count = 0;
        First = (TZone**)(operator new((size_t)(Count) * sizeof(TZone*)));
        TZone** Out = First;
        for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
            *Out = *In;
        Last = Out;
        End = Out;
    }

    // The VC6 vector<T*>::operator= both manager assignments inline (.text:062a2c2b-062a2d84 /
    // 062a2e4b-062a2fd8). Self-assignment skips it. Not longer than this: copy and cut.
    // Longer but within capacity: copy over the live part, then construct the rest
    // (FUN_0630cd00). Otherwise: free the block, allocate exactly the other size (negative
    // clamps to 0) and copy (_Ucopy). Only the pointers are copied; the zones are shared.
    template <typename TZone>
    void ZoneArrayAssign(TZone**& First, TZone**& Last, TZone**& End, TZone** OtherFirst, TZone** OtherLast)
    {
        unsigned const OtherSize = (OtherFirst == nullptr) ? 0u : (unsigned)(OtherLast - OtherFirst);
        unsigned const Size = (First == nullptr) ? 0u : (unsigned)(Last - First);
        if (!(OtherSize > Size))
        {
            TZone** Out = First;
            for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
                *Out = *In;
            Last = First + OtherSize;
            return;
        }

        unsigned const Capacity = (First == nullptr) ? 0u : (unsigned)(End - First);
        if (!(OtherSize > Capacity))
        {
            TZone** Out = First;
            TZone** In = OtherFirst;
            for (unsigned Index = 0; Index < Size; ++Index, ++In, ++Out)
                *Out = *In;
            for (; In != OtherLast; ++In, ++Out)
                *Out = *In;
            Last = First + OtherSize;
            return;
        }

        operator delete(First);
        int Count = (OtherFirst == nullptr) ? 0 : (int)(OtherLast - OtherFirst);
        if (Count < 0)
            Count = 0;
        First = (TZone**)(operator new((size_t)(Count) * sizeof(TZone*)));
        TZone** Out = First;
        for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
            *Out = *In;
        Last = Out;
        End = Out;
    }

    // The managers' destructors (.text:062bbd70 / 062bc6b0): every non-null zone goes to
    // operator delete (the zone destructor is the empty .text:062a8920), the erase copy loop
    // runs on an empty range, the block is freed and the three pointers cleared.
    template <typename TZone>
    void ZoneArrayDestroy(TZone**& First, TZone**& Last, TZone**& End)
    {
        for (TZone** It = First; It != Last; ++It)
        {
            if (*It != nullptr)
                delete *It;
        }
        Last = First;
        operator delete(First);
        First = nullptr;
        Last = nullptr;
        End = nullptr;
    }

    // The comparison every sort step makes: FLD [a+4] / FCOMP [b+4] / TEST AH,5 / JNP -> a
    // sorts before b when its distance is smaller (unordered compares false).
    template <typename TZone>
    bool ZoneCloser(TZone const* A, TZone const* B)
    {
        return A->MaxDistSq < B->MaxDistSq;
    }

    // VC6 std::sort on the zone arrays, inlined into both sort members with these out-of-line
    // pieces: _Unguarded_insert FUN_062bcf70, _Unguarded_partition FUN_062bcfa0 and the
    // recursive _Sort FUN_062bcd70 / FUN_062bce70 (one per zone type).
    template <typename TZone>
    void ZoneUnguardedInsert(TZone** Last, TZone* Value)
    {
        // FUN_062bcf70: shift right while Value sorts before the previous element.
        for (TZone** Prev = Last; ZoneCloser(Value, *--Prev); Last = Prev)
            *Last = *Prev;
        *Last = Value;
    }

    template <typename TZone>
    void ZoneInsertionSort(TZone** First, TZone** Last)
    {
        if (First == Last)
            return;
        for (TZone** It = First; ++It != Last;)
        {
            TZone* const Value = *It;
            if (ZoneCloser(Value, *First))
            {
                for (TZone** Move = It; Move != First; --Move)
                    *Move = *(Move - 1);
                *First = Value;
            }
            else
            {
                ZoneUnguardedInsert(It, Value);
            }
        }
    }

    template <typename TZone>
    TZone* ZoneMedian(TZone* X, TZone* Y, TZone* Z)
    {
        if (ZoneCloser(X, Y))
            return ZoneCloser(Y, Z) ? Y : (ZoneCloser(X, Z) ? Z : X);
        return ZoneCloser(X, Z) ? X : (ZoneCloser(Y, Z) ? Z : Y);
    }

    template <typename TZone>
    TZone** ZoneUnguardedPartition(TZone** First, TZone** Last, TZone* Pivot)
    {
        // FUN_062bcfa0.
        for (;; ++First)
        {
            for (; ZoneCloser(*First, Pivot); ++First)
            {
            }
            for (; ZoneCloser(Pivot, *--Last);)
            {
            }
            if (!(Last > First))
                return First;
            TZone* const Swap = *First;
            *First = *Last;
            *Last = Swap;
        }
    }

    template <typename TZone>
    void ZoneSortRange(TZone** First, TZone** Last)
    {
        // FUN_062bcd70 / FUN_062bce70: while more than 16 elements, partition around the
        // median of first / middle / last-1 and recurse into the side that is not longer.
        while ((Last - First) > 16)
        {
            TZone** const Mid = ZoneUnguardedPartition(
                First, Last, ZoneMedian(*First, *(First + (Last - First) / 2), *(Last - 1)));
            if ((Last - Mid) <= (Mid - First))
            {
                ZoneSortRange(Mid, Last);
                Last = Mid;
            }
            else
            {
                ZoneSortRange(First, Mid);
                First = Mid;
            }
        }
    }

    template <typename TZone>
    void ZoneSort(TZone** First, TZone** Last)
    {
        if ((Last - First) <= 16)
        {
            ZoneInsertionSort(First, Last);
            return;
        }
        ZoneSortRange(First, Last);
        ZoneInsertionSort(First, First + 16);
        for (TZone** It = First + 16; It != Last; ++It)
            ZoneUnguardedInsert(It, *It);
    }
}

// --- AsteroidExclusionZoneMgr ---

AsteroidExclusionZoneMgr::AsteroidExclusionZoneMgr()
    : ZonesAllocator(0)
    , Begin(nullptr)
    , End(nullptr)
    , CapacityEnd(nullptr)
    // Retail ctor (Common.dll.c 93536-93553) stores these as squared-distance
    // literals directly: this+0x14 = 0x4a742400 = 4000000.0f (2000^2),
    // this+0x18 = 0x48742400 = 250000.0f (500^2). Confirmed by the setters
    // (Common.dll.txt 126483-126506) which do `FLD/FMUL param_1,param_1;
    // FSTP [this+0x14 or +0x18]` -- i.e. they square their float argument
    // before storing, so these fields always hold squared distances.
    , MaxCliDist(4000000.0f)
    , MaxDynDist(250000.0f)
{
}

AsteroidExclusionZoneMgr::AsteroidExclusionZoneMgr(const AsteroidExclusionZoneMgr& Other)
    : ZonesAllocator(Other.ZonesAllocator)
{
    // .text:062a2b90: the base vftable (0639dcf0) first, the allocator byte, the inlined
    // vector copy (shared zone pointers), +0x14 / +0x18, then this vftable.
    ZoneArrayCopyConstruct(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxCliDist = Other.MaxCliDist;
    MaxDynDist = Other.MaxDynDist;
}

AsteroidExclusionZoneMgr& AsteroidExclusionZoneMgr::operator=(const AsteroidExclusionZoneMgr& Other)
{
    // .text:062a2c20: the inlined vector assignment (skipped for self-assignment; the zones
    // are shared, not copied, and the old ones are not freed), then +0x14 / +0x18 always.
    if (&ZonesAllocator != &Other.ZonesAllocator)
        ZoneArrayAssign(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxCliDist = Other.MaxCliDist;
    MaxDynDist = Other.MaxDynDist;
    return *this;
}

AsteroidExclusionZoneMgr::~AsteroidExclusionZoneMgr()
{
    // .text:062bbd70.
    ZoneArrayDestroy(Begin, End, CapacityEnd);
}

bool AsteroidExclusionZoneMgr::is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue)
{
    // .text:062bbe70. Types above 3 (CMP 3 / JA, unsigned) return false. Cases 0, 1 and 3 are
    // inlined copies of is_cliasteroid_excluded / is_cmnasteroid_excluded /
    // is_dynamic_asteroid_excluded (identical control flow); case 2 calls
    // is_billboard_excluded.
    switch ((unsigned)(Type))
    {
    case 0:
        return is_cliasteroid_excluded(Pos, OutValue);
    case 1:
        return is_cmnasteroid_excluded(Pos, OutValue);
    case 2:
        return is_billboard_excluded(Pos, OutValue);
    case 3:
        return is_dynamic_asteroid_excluded(Pos);
    default:
        return false;
    }
}

void AsteroidExclusionZoneMgr::push_back(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bbde0: operator new(0x14) with the constructor's values inlined (a null
    // allocation is stored as a null zone), vector insert at the end, then set_zone inlined
    // on back() with its result dropped (FSTP ST0): ZonePtr = Zone and, for a zone,
    // Parent's vt+0x00 at the zone's vt+0x10 position.
    AsteroidExclusionZone* const NewZone = new AsteroidExclusionZone();
    ZoneArrayPushBack(Begin, End, CapacityEnd, NewZone);
    back()->set_zone(Zone, Parent);
}

void AsteroidExclusionZoneMgr::sort(Vector const* Observer)
{
    // .text:062bbfe0: update_dist on every zone (no null tests), then the inlined VC6 sort by
    // MaxDistSq.
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
        (*It)->update_dist(*Observer);
    ZoneSort(Begin, End);
}

AsteroidExclusionZone* AsteroidExclusionZoneMgr::back()
{
    // .text:062bbe60 (shared with NebulaExclusionZoneMgr::back): MOV EAX,[this+0xc] /
    // MOV EAX,[EAX-4]. No empty test.
    return *(End - 1);
}

int AsteroidExclusionZoneMgr::get_max_billboard_count()
{
    // .text:062bc3d0: the largest ftol(BillboardCount) above 0 (FILD / FCOMP 0.0f / TEST 0x41 /
    // JNZ skips <= 0), -1 when none.
    int MaxCount = -1;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        int const Count = (int)((long long)((*It)->BillboardCount));
        if ((float)(Count) > 0.0f && Count > MaxCount)
            MaxCount = Count;
    }
    return MaxCount;
}

void AsteroidExclusionZoneMgr::normalize_billboard_density(int TotalCount)
{
    // .text:062bc420: Inverse = 1.0f / TotalCount stored as float (no zero test); every zone
    // whose ftol(BillboardCount) is above 0 gets that count times Inverse.
    float const Inverse = (float)(1.0 / (double)(TotalCount));
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        int const Count = (int)((long long)(Zone->BillboardCount));
        if ((double)(Count) > 0.0)
            Zone->BillboardCount = (float)((double)(Count) * Inverse);
    }
}

void AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance(float Dist)
{
    // Retail squares the argument before storing (Common.dll.txt 126483-126492:
    // FLD/FMUL param_1,param_1; FSTP [this+0x14]) -- MaxCliDist is compared
    // directly against Zone->MaxDistSq elsewhere, so it must hold squared units.
    MaxCliDist = Dist * Dist;
}

void AsteroidExclusionZoneMgr::set_max_dynamicasteroid_exclusion_distance(float Dist)
{
    // Same squaring as set_max_cliasteroid_exclusion_distance (Common.dll.txt 126498-126505).
    MaxDynDist = Dist * Dist;
}

bool AsteroidExclusionZoneMgr::is_cliasteroid_excluded(const Vector& Pos, float* OutDist)
{
    // .text:062bc200 (and is_excluded case 0, .text:062bbe8c). *OutDist = -1.0f first, no
    // null tests. The zones are sorted by distance: the scan stops (false) at the first zone
    // whose MaxDistSq is above MaxCliDist (FCOMP / TEST 0x41 / JZ). A zone whose immersion
    // (vt+0x00) is non-zero (TEST 0x44 / JNP skips 0) ends the scan with true when its
    // EmptyCubeFrequency is below 0 or unordered (TEST CL=1,AH -- C0); otherwise its EmptyCubeFrequency
    // goes to *OutDist and the scan goes on.
    *OutDist = -1.0f;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->MaxDistSq > MaxCliDist)
            return false;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            if (!(Zone->EmptyCubeFrequency >= 0.0f))
                return true;
            *OutDist = Zone->EmptyCubeFrequency;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_cmnasteroid_excluded(const Vector& Pos, float* OutDist)
{
    // .text:062bc270 (and is_excluded case 1, .text:062bbef5). As is_cliasteroid_excluded
    // without the distance cut-off.
    *OutDist = -1.0f;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            if (!(Zone->EmptyCubeFrequency >= 0.0f))
                return true;
            *OutDist = Zone->EmptyCubeFrequency;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_billboard_excluded(const Vector& Pos, float* OutDist)
{
    // .text:062bc2d0. A zone with bExcludeBillboards returns true when its immersion is
    // non-zero and is skipped otherwise. Any other zone, until one has been found: when its
    // BillboardCount is above 0 (TEST 0x41 / JNZ skips <= 0 or unordered) and the immersion is
    // non-zero, BillboardCount goes to *OutDist. The end of the scan returns false in every case
    // (XOR AL,AL); the found flag only stops further writes.
    bool bFound = false;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->bExcludeBillboards)
        {
            if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
                return true;
            continue;
        }
        float const Density = Zone->BillboardCount;
        if (bFound || !(Density > 0.0f))
            continue;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            bFound = true;
            *OutDist = Density;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_dynamic_asteroid_excluded(const Vector& Pos)
{
    // .text:062bc370 (and is_excluded case 3, .text:062bbf71): stops (false) at the first zone
    // whose MaxDistSq is above MaxDynDist; a zone with bExcludeDynamicAsteroids and a non-zero
    // immersion returns true; other zones are skipped.
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->MaxDistSq > MaxDynDist)
            return false;
        if (Zone->bExcludeDynamicAsteroids && Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
            return true;
    }
    return false;
}

// --- NebulaExclusionZoneMgr ---

NebulaExclusionZoneMgr::NebulaExclusionZoneMgr()
    : ZonesAllocator(0)
    , Begin(nullptr)
    , End(nullptr)
    , CapacityEnd(nullptr)
    // .text:062bc660 never writes +0x14: retail leaves MaxDist indeterminate until
    // set_max_distance. Named UB, kept deterministic here (owner's UB rule).
    , MaxDist(0.0f)
{
    // The allocator byte at +0x04 is copied from an uninitialised stack byte in retail; the
    // value is never read.
}

NebulaExclusionZoneMgr::NebulaExclusionZoneMgr(const NebulaExclusionZoneMgr& Other)
    : ZonesAllocator(Other.ZonesAllocator)
{
    // .text:062a2dc0: the base vftable (0639dcf0) first, the allocator byte, the inlined
    // vector copy (shared zone pointers), +0x14, then this vftable.
    ZoneArrayCopyConstruct(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxDist = Other.MaxDist;
}

NebulaExclusionZoneMgr& NebulaExclusionZoneMgr::operator=(const NebulaExclusionZoneMgr& Other)
{
    // .text:062a2e40: the inlined vector assignment (skipped for self-assignment; the zones
    // are shared, not copied, and the old ones are not freed), then +0x14 always.
    if (&ZonesAllocator != &Other.ZonesAllocator)
        ZoneArrayAssign(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxDist = Other.MaxDist;
    return *this;
}

NebulaExclusionZoneMgr::~NebulaExclusionZoneMgr()
{
    // .text:062bc6b0.
    ZoneArrayDestroy(Begin, End, CapacityEnd);
}

bool NebulaExclusionZoneMgr::is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue)
{
    // .text:062bc840: only type 4; true at the first zone whose immersion (vt+0x00) is
    // non-zero (TEST 0x44 / JNP). OutValue is not read; no null tests.
    (void)OutValue;
    if ((int)(Type) != 4)
        return false;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        if ((*It)->ZonePtr->GetImmersion(Pos) != 0.0f)
            return true;
    }
    return false;
}

void NebulaExclusionZoneMgr::push_back(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bc720: operator new(0x10) with the constructor inlined (ID_String::clear twice,
    // +0 = 0, +4 = 1e7, +8 = -1.0f), vector insert at the end, then back()->set_zone. Zone's
    // edge fraction (vt+0x50) at or below 0.0f (FCOMP / TEST 0x41 / JP skips > 0 or
    // unordered) reports line 0x170; a false set_zone reports line 0x176 -- both with the
    // zone id (vt+0x04).
    NebulaExclusionZone* const NewZone = new NebulaExclusionZone();
    ZoneArrayPushBack(Begin, End, CapacityEnd, NewZone);
    bool const bInside = back()->set_zone(Zone, Parent);
    if (!(Zone->GetEdgeFraction() > 0.0f))
    {
        FDUMP(0x100001,
            "%s(%d) : *** ERROR: DATA ERROR: edge fraction must be added to exclusion zone %u inorder for transitions to work",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x170, Zone->GetId());
    }
    if (!bInside)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: DATA ERROR: exclusion zone %u not inside nebula",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x176, Zone->GetId());
    }
}

void NebulaExclusionZoneMgr::sort(Vector const* Observer)
{
    // .text:062bc890. update_dist is inlined with its own float stores: for every zone with a
    // ZonePtr, the delta to the zone's vt+0x10 position is built through the Vector ctor
    // (FUN_0628b030, floats), Length = float(sqrt((dy^2 + dz^2) + dx^2)), Dist = Length -
    // vt+0x30; FST float, FCOM 0.0f / TEST 5 / JP -> float(Dist^2) when >= 0 or unordered,
    // else 0. Observer is not tested. Then the inlined VC6 sort by MaxDistSq.
    Vector const& ObserverPos = *Observer;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        NebulaExclusionZone* const Zone = *It;
        if (Zone->ZonePtr == nullptr)
            continue;
        Vector const ZonePos = Zone->ZonePtr->GetPos();
        float const Dx = ObserverPos.x - ZonePos.x;
        float const Dy = ObserverPos.y - ZonePos.y;
        float const Dz = ObserverPos.z - ZonePos.z;
        float const Length = (float)(sqrt(((double)(Dy) * Dy + (double)(Dz) * Dz) +
                                                     (double)(Dx) * Dx));
        double const Dist = Length - (double)(Zone->ZonePtr->GetRadius());
        if (Dist < 0.0)
            Zone->MaxDistSq = 0.0f;
        else
            Zone->MaxDistSq = (float)(Dist * Dist);
    }
    ZoneSort(Begin, End);
}

NebulaExclusionZone* NebulaExclusionZoneMgr::back()
{
    // .text:062bbe60 (shared with AsteroidExclusionZoneMgr::back): no empty test.
    return *(End - 1);
}

bool NebulaExclusionZoneMgr::is_in_exclusion_zone(const Vector& Pos,
                                                NebulaExclusionZone** OutZone,
                                                float* OutDist, bool Flag)
{
    // .text:062bcb40: *OutZone = 0 and *OutDist = 0 first (no null tests). With Flag, the scan
    // stops (false) at the first zone whose MaxDistSq is above 10.0f (FCOMP float 0639e884 /
    // TEST 0x41 / JZ) -- the zones are sorted by distance. A zone whose immersion is above
    // 1e-5f (FCOM float 0639b984 / TEST 0x41 / JZ) is the answer: the zone and that value.
    *OutZone = nullptr;
    *OutDist = 0.0f;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        NebulaExclusionZone* const Zone = *It;
        if (Flag && Zone->MaxDistSq > 10.0f)
            return false;
        // 062bcb8a-062bcb97: FCOM float 1e-5 on the unrounded vt+0x00 result.
        double const Inside = Zone->ZonePtr->GetImmersion(Pos);
        if (Inside > 9.99999975e-06f)
        {
            *OutZone = Zone;
            *OutDist = (float)Inside;
            return true;
        }
    }
    return false;
}

void NebulaExclusionZoneMgr::set_max_distance(float Dist)
{
    // ICF-shared code with AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance
    // (both are literally the same machine code at 0x42b70, Common.dll.txt 126483-126492):
    // squares the argument before storing (this+0x14), matching the squared-distance
    // convention used everywhere else in this namespace.
    MaxDist = Dist * Dist;
}

} // namespace Exclusion
