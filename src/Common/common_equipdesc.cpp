// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll EquipDesc, EquipDescVector and EquipDescList, EquipDesc::CARGO_BAY_HP_NAME, and the
// exported std::list / std::vector<EquipDesc> and weighted_vector<unsigned int> instantiations.
// Split out of common_goods.cpp.
//
// Static-initialisation order: CARGO_BAY_HP_NAME is StringAlloc'd by a dynamic initialiser, so
// common_stringcache.cpp (GStringCacheOwner) must precede this TU; among the goods TUs this one
// comes first (then common_constants.cpp, common_loadout.cpp, common_goodinfo.cpp).

#include "common_goods.h"
#include "common_ini.h"  // StringAlloc
#include "common_misc.h" // SUBOBJ_ID_NONE

#include <cstddef>
#include <list>
#include <new>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

// ---------------------------------------------------------------------------
// EquipDesc
// ---------------------------------------------------------------------------

// .text:06309bc0 (static-init table entry .data:063ea484): PUSH 0 / PUSH "BAY"
// (.rdata:063a347c) / CALL StringAlloc / MOV [CARGO_BAY_HP_NAME],EAX.
CacheString const EquipDesc::CARGO_BAY_HP_NAME = { StringAlloc("BAY", false) };

// 0x06309ae0: +0x00 byte 0, +0x02 = SUBOBJ_ID_NONE, +0x04 = 0, +0x08 =
// CARGO_BAY_HP_NAME.Value, +0x0c byte 0, +0x10 = 1.0f, +0x14 = 1, +0x18 byte 0, +0x1c = 0.
EquipDesc::EquipDesc()
    : Cached(0)
    , Id(SUBOBJ_ID_NONE)
    , ArchId(0)
    , Hardpoint(CARGO_BAY_HP_NAME.Value)
    , bEquipped(false)
    , Health(1.0f)
    , Count(1)
    , bMission(false)
    , Owner(0)
{
}

// 06263c60 -- member-by-member copy, in declaration order.
EquipDesc::EquipDesc(EquipDesc const& Other)
{
    Cached = Other.Cached;
    Id = Other.Id;
    ArchId = Other.ArchId;
    Hardpoint = Other.Hardpoint;
    bEquipped = Other.bEquipped;
    Health = Other.Health;
    Count = Other.Count;
    bMission = Other.bMission;
    Owner = Other.Owner;
}

// ADDED 2026-09-17 (wave 14). These three were DECLARED in common_goods.h but
// never defined anywhere in the tree -- a latent unresolved-external that only
// stayed quiet because nothing called them. StarSystem_DamageObject (Server.dll)
// is the first caller.
//
//   ?get_id@EquipDesc@@QBEGXZ        0x3b70  `MOV AX,[this+0x2]`
//   ?get_hardpoint@EquipDesc@@QBEABUCacheString@@XZ
//                                    0x3b80  `LEA EAX,[this+0x8]`
//   ?get_status@EquipDesc@@QBEMXZ    0x3ba0  `FLD [this+0x10]`
unsigned short EquipDesc::get_id() const
{
    return Id;
}

CacheString const& EquipDesc::get_hardpoint() const
{
    // Retail returns the ADDRESS of the field, which is a CacheString because a
    // CacheString is exactly the one `char const*` that lives there.
    return *(CacheString const*)&Hardpoint;
}

float EquipDesc::get_status() const
{
    return Health;
}

void EquipDesc::set_id(unsigned short NewId)
{
    Id = NewId;
}

void EquipDesc::set_arch_id(unsigned Arch)
{
    ArchId = Arch;
}

void EquipDesc::set_count(int NewCount)
{
    // 0xdbad0 -- one instruction: `MOV [this+0x14],arg`.
    Count = NewCount;
}

void EquipDesc::set_hardpoint(CacheString const& Hp)
{
    Hardpoint = Hp.Value;
}

void EquipDesc::set_equipped(unsigned char bOn)
{
    bEquipped = bOn;
}

void EquipDesc::set_status(float HealthValue)
{
    Health = HealthValue;
}

// 0x06309b80: MOV EAX,[CARGO_BAY_HP_NAME] / MOV [this+8],EAX.
void EquipDesc::make_internal()
{
    Hardpoint = CARGO_BAY_HP_NAME.Value;
}

bool EquipDesc::is_internal() const
{
    return Hardpoint == CARGO_BAY_HP_NAME.Value;
}

bool EquipDesc::get_temporary() const
{
    return bMission;
}

// The six accessors added 2026-09-17 (wave 14). Offsets in the header.
unsigned EquipDesc::get_arch_id() const
{
    return ArchId;
}

unsigned char EquipDesc::is_equipped() const
{
    return bEquipped;
}

int EquipDesc::get_count() const
{
    return Count;
}

void EquipDesc::set_temporary(bool bOn)
{
    bMission = bOn;
}

int EquipDesc::get_owner() const
{
    return (int)Owner;
}

void EquipDesc::set_owner(int NewOwner)
{
    Owner = (unsigned)NewOwner;
}

// .text:06309b90. 0.0 unless the item sits on CARGO_BAY_HP_NAME with a nonzero ArchId;
// otherwise FILD Count * FMUL Volume, left in ST0 (hence double). GetEquipment's result is
// not tested.
double EquipDesc::get_cargo_space_occupied() const
{
    if (Hardpoint != CARGO_BAY_HP_NAME.Value || ArchId == 0)
        return 0.0;
    Archetype::Equipment* Arch = Archetype::GetEquipment(ArchId);
    return (double)Count * Arch->Volume;
}

// 0x06266310 ??0EquipDescVector@@QAE@XZ -- zero the allocator byte and the three
// pointers. Retail's linker folded this onto the byte-identical default ctor of
// weighted_vector<unsigned int> (both are "zero four dwords"); see GetSumOfWeights
// below for the other half of that folded pair.
EquipDescVector::EquipDescVector()
    : AllocPad(0)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
}

// 0x06263cf0 ??0EquipDescVector@@QAE@ABU0@@Z: the allocator byte copied, then a buffer of
// exactly Other.size() elements is allocated -- also when that is 0 (operator new(0)) -- every
// element copy-constructed into it, and End = Cap = past the last one.
EquipDescVector::EquipDescVector(EquipDescVector const& Other)
    : AllocPad(Other.AllocPad)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
    int Count = (Other.Begin != nullptr) ? (int)(Other.End - Other.Begin) : 0; // 06263cfd..06263d0e
    if (Count < 0) // 06263d13 JGE: the VC6 allocator's own clamp
        Count = 0;
    Begin = (EquipDesc*)::operator new(Count * sizeof(EquipDesc)); // 06263d1b
    EquipDesc* Dst = Begin;
    for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst) // 06263d32..06263d74
        new (Dst) EquipDesc(*Src);
    End = Dst; // 06263d78
    Cap = Dst; // 06263d7b
}

// 0x06269110 ??1EquipDescVector@@QAE@XZ: .text:06269113 PUSH [this+4] / CALL
// FUN_06391d5a (operator delete), then .text:0626911c..06269127 zero +4/+8/+0xc.
// EquipDesc has a trivial destructor, so no element destructor runs (retail has none).
EquipDescVector::~EquipDescVector()
{
    ::operator delete(Begin);
    Begin = nullptr;
    End = nullptr;
    Cap = nullptr;
}

// 0x06263d90 ??4EquipDescVector@@QAEAAU0@ABU0@@Z -- the VC6 vector assignment. AllocPad
// (+0) is never touched. Three branches (sizes are unsigned compares, JA):
//   1. Other.size() <= size(): assign every element in place, End = Begin + Other.size()
//      (buffer and capacity kept; an empty source just sets End = Begin).
//   2. Other.size() <= capacity(): assign the first size() elements, copy-construct the
//      rest after End (FUN_0626c1e0), End = Begin + Other.size().
//   3. otherwise: delete Begin, allocate Other.size() elements, copy-construct all
//      (_Ucopy), End = Cap = past the last one.
EquipDescVector& EquipDescVector::operator=(EquipDescVector const& Other)
{
    if (this == &Other) // 06263d9a
        return *this;

    unsigned OtherCount = (Other.Begin != nullptr) ? (unsigned)(Other.End - Other.Begin) : 0; // 06263da2..06263db2
    unsigned OwnCount = (Begin != nullptr) ? (unsigned)(End - Begin) : 0; // 06263db5..06263dc5
    if (OtherCount <= OwnCount) // 06263dc8 CMP / JA
    {
        // 06263dcc..06263de8: REP MOVSD of 8 dwords per element.
        EquipDesc* Dst = Begin;
        for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst)
            *Dst = *Src;
        // 06263dee..06263e1c: End = Begin + Other.size(), size re-read from Other.
        End = Begin + ((Other.Begin != nullptr) ? (Other.End - Other.Begin) : 0);
        return *this;
    }

    unsigned OwnCapacity = (Begin != nullptr) ? (unsigned)(Cap - Begin) : 0; // 06263e37..06263e44
    if (OtherCount <= OwnCapacity) // 06263e47 CMP / JA
    {
        // 06263e4f..06263e83: assign Other[0 .. size()) over the live elements.
        EquipDesc const* Mid = Other.Begin + ((Begin != nullptr) ? (End - Begin) : 0);
        EquipDesc* Dst = Begin;
        for (EquipDesc const* Src = Other.Begin; Src != Mid; ++Src, ++Dst)
            *Dst = *Src;
        // 06263e89..06263ea7: FUN_0626c1e0 copy-constructs Other[size() .. ) after End.
        Dst = End;
        for (EquipDesc const* Src = Mid; Src != Other.End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        // 06263ea9..06263edb: End = Begin + Other.size().
        End = Begin + ((Other.Begin != nullptr) ? (Other.End - Other.Begin) : 0);
        return *this;
    }

    // 06263ee6..06263f2a
    ::operator delete(Begin);
    int NewCount = (Other.Begin != nullptr) ? (int)(Other.End - Other.Begin) : 0;
    if (NewCount < 0) // 06263f02 JGE (the VC6 allocator's own clamp)
        NewCount = 0;
    Begin = (EquipDesc*)::operator new(NewCount * sizeof(EquipDesc));
    EquipDesc* Dst = Begin;
    for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst)
        new (Dst) EquipDesc(*Src);
    Cap = Dst; // 06263f27
    End = Dst; // 06263f2a
    return *this;
}

namespace
{
    // Shared growth helper for EquipDescVector's hand-rolled VC6-vector push_back
    // path (0xa9be0 append / 0xa9e40 ctor-from-list / 0xaa0b0 add_equipment_item
    // all reallocate-and-copy the same way once they run out of capacity). Retail's
    // exact growth-factor arithmetic (roughly doubling) is an internal performance
    // detail with no observable effect through this class's public surface (no
    // capacity() accessor exists anywhere), so this grows to exactly the size
    // needed rather than reproducing retail's doubling heuristic.
    void EnsureEquipCapacity(EquipDescVector& Vec, unsigned ExtraCount)
    {
        const unsigned OldCount = (unsigned)(Vec.End - Vec.Begin);
        const unsigned OldCap = (unsigned)(Vec.Cap - Vec.Begin);
        const unsigned NewCount = OldCount + ExtraCount;
        if (OldCap >= NewCount)
            return;

        EquipDesc* NewBuf = (EquipDesc*)::operator new(NewCount * sizeof(EquipDesc));
        for (unsigned Index = 0; Index < OldCount; ++Index)
            new (&NewBuf[Index]) EquipDesc(Vec.Begin[Index]);
        ::operator delete(Vec.Begin);
        Vec.Begin = NewBuf;
        Vec.End = NewBuf + OldCount;
        Vec.Cap = NewBuf + NewCount;
    }

    void EquipVectorPushBack(EquipDescVector& Vec, EquipDesc const& Item)
    {
        EnsureEquipCapacity(Vec, 1);
        new (Vec.End) EquipDesc(Item);
        ++Vec.End;
    }
}

// 0xa9e40 ??0EquipDescVector@@QAE@ABVEquipDescList@@@Z -- copies every item out of
// the list's storage, in the list's own order, into a freshly allocated buffer.
EquipDescVector::EquipDescVector(EquipDescList const& Other)
    : AllocPad(0)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
    const unsigned Count = (unsigned)Other.Items.size();
    if (Count == 0)
        return;

    Begin = (EquipDesc*)::operator new(Count * sizeof(EquipDesc));
    unsigned Index = 0;
    for (std::list<EquipDesc>::const_iterator It = Other.Items.begin(); It != Other.Items.end();
         ++It, ++Index)
        new (&Begin[Index]) EquipDesc(*It);
    End = Begin + Count;
    Cap = Begin + Count;
}

// 0xa9be0 ?append@EquipDescVector@@QAEXABU1@@Z
void EquipDescVector::append(EquipDescVector const& Other)
{
    const unsigned AddCount = (unsigned)(Other.End - Other.Begin);
    if (AddCount == 0)
        return;

    EnsureEquipCapacity(*this, AddCount);
    for (unsigned Index = 0; Index < AddCount; ++Index)
        new (End + Index) EquipDesc(Other.Begin[Index]);
    End += AddCount;
}

// 0xaa0b0 ?add_equipment_item@EquipDescVector@@QAEHABUEquipDesc@@_N@Z
int EquipDescVector::add_equipment_item(EquipDesc const& Desc, bool bCombinable)
{
    if (bCombinable)
    {
        // Retail's raw disassembly (0630a0d0..0630a122) scans for the first
        // element whose ArchId matches, then checks bMission/Owner/Hardpoint;
        // on any of those three failing, it jumps back to the ArchId scan's own
        // entry label -- a genuine restart-from-Begin, not a decompiler
        // artifact (confirmed against Common.dll.txt, not just the .c
        // pseudocode). Reproduced faithfully, hazard included: if the first
        // ArchId match's other fields never match, this does not terminate.
        // Called by Loadout::Load (0631184b), whose port works on a std::vector and
        // uses the identical AddEquipmentItem helper above.
        // Hardpoints compared with DACOM stricmp (0630a11b). The health tests are
        // FCOMP 1.0f + TEST AH,0x5 + JP: "not below 1.0", NaN included.
        for (;;)
        {
            EquipDesc* Match = nullptr;
            for (EquipDesc* It = Begin; It != End; ++It)
            {
                if (It->ArchId == Desc.ArchId)
                {
                    Match = It;
                    break;
                }
            }
            if (Match == nullptr)
                break; // no ArchId match anywhere -- fall through to plain append

            if (Match->bMission != Desc.bMission || Match->Owner != Desc.Owner ||
                CallDacomStricmp(Match->Hardpoint, Desc.Hardpoint) != 0)
                continue; // retail: restart the scan from Begin

            // Full match -- health-aware combine (Health >= 1.0 means "full/new").
            if (!(Match->Health < 1.0f))
            {
                // Full-health existing stack: just absorb the incoming count and
                // take the incoming item's health (retail overwrites, doesn't sum).
                Match->Count += Desc.Count;
                Match->Health = Desc.Health;
            }
            else if (!(Desc.Health < 1.0f))
            {
                Match->Count += Desc.Count;
            }
            else
            {
                // 0630a147-0630a181: the sum stays on the x87 stack (exact in a double).
                double const NewHealth = (double)Match->Health + Desc.Health;
                const int NewCount = Desc.Count + Match->Count;
                if (!(NewHealth < 1.0))
                {
                    Match->Count = NewCount;
                    Match->Health = (float)(NewHealth - 1.0);
                }
                else
                {
                    Match->Health = (float)NewHealth;
                    Match->Count = NewCount - 1;
                }
            }
            return 1;
        }
    }

    EquipVectorPushBack(*this, Desc);
    return 0;
}

// 0xaa350 ?traverse_equipment_type@EquipDescVector@@QAEPAUEquipDesc@@IPBU2@@Z
EquipDesc* EquipDescVector::traverse_equipment_type(unsigned Type, EquipDesc const* Prev)
{
    for (EquipDesc* It = Begin; It != End; ++It)
    {
        if (It->ArchId == Type)
        {
            if (Prev == nullptr)
                return It;
            if (Prev == It)
                Prev = nullptr;
        }
    }
    return nullptr;
}

static_assert(offsetof(EquipDesc, Id) == 2, "EquipDesc+2 id");
static_assert(offsetof(EquipDesc, ArchId) == 4, "EquipDesc+4 arch");
static_assert(offsetof(EquipDesc, Hardpoint) == 8, "EquipDesc+8 hp");
static_assert(offsetof(EquipDesc, bEquipped) == 0x0C, "EquipDesc+0xC equipped");
static_assert(offsetof(EquipDesc, Health) == 0x10, "EquipDesc+0x10 health");
static_assert(offsetof(EquipDesc, Count) == 0x14, "EquipDesc+0x14 count");
static_assert(offsetof(EquipDesc, bMission) == 0x18, "EquipDesc+0x18 mission");
static_assert(offsetof(EquipDesc, Owner) == 0x1C, "EquipDesc+0x1C owner");

// ---------------------------------------------------------------------------
// EquipDescList — see common_goods.h. Minimal real port of retail Common.dll's
// class: freelancer.exe (client) holds one static instance (DAT_00672960) and
// forwards to it via add_equipment_item/find_equipment_item; the surrounding
// gather/notify wrappers (FUN_004c4170/FUN_004c41d0/.../FUN_004c4230) remain
// blocked on other unported pieces (see decomp-tracker notes on those items).
// ---------------------------------------------------------------------------
EquipDescList::EquipDescList()
    : Items()
{
}

EquipDescList::~EquipDescList()
{
}

// 0xaa4d0 ?add_equipment_item@EquipDescList@@QAEHABUEquipDesc@@_N@Z -- corrected
// from an earlier, unverified guess that returned an auto-assigned Id and merged
// via the simpler MergeCombinable() (plain Count += Count, no Health awareness).
// Retail's actual return type is int (0/1, confirmed by Ghidra's recovered
// mangled name ending in "H" not "G"), it never assigns Desc a fresh Id, and its
// combinable merge is the same Health-aware rule as
// EquipDescVector::add_equipment_item -- including that function's raw-
// disassembly-confirmed "restart the scan from begin() whenever an ArchId match
// fails the bMission/Owner/Hardpoint check" quirk (0630a4d0..0630a51d parallels
// 0630a0d0..0630a122 exactly).
int EquipDescList::add_equipment_item(EquipDesc const& Desc, bool bCombinable)
{
    if (bCombinable)
    {
        for (;;)
        {
            EquipDesc* Match = nullptr;
            for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
            {
                if (It->ArchId == Desc.ArchId)
                {
                    Match = &*It;
                    break;
                }
            }
            if (Match == nullptr)
                break; // no ArchId match anywhere -- fall through to plain append

            // 0630a565: DACOM stricmp; health tests as in EquipDescVector's
            // ("not below 1.0", NaN included), sum kept exact in a double.
            if (Match->bMission != Desc.bMission || Match->Owner != Desc.Owner ||
                CallDacomStricmp(Match->Hardpoint, Desc.Hardpoint) != 0)
                continue; // retail: restart the scan from begin()

            if (!(Match->Health < 1.0f))
            {
                Match->Count += Desc.Count;
                Match->Health = Desc.Health;
            }
            else if (!(Desc.Health < 1.0f))
            {
                Match->Count += Desc.Count;
            }
            else
            {
                double const NewHealth = (double)Desc.Health + Match->Health;
                const int NewCount = Desc.Count + Match->Count;
                if (!(NewHealth < 1.0))
                {
                    Match->Count = NewCount;
                    Match->Health = (float)(NewHealth - 1.0);
                }
                else
                {
                    Match->Health = (float)NewHealth;
                    Match->Count = NewCount - 1;
                }
            }
            return 1;
        }
    }

    Items.push_back(Desc);
    return 0;
}

// 0xaa460 ?append@EquipDescList@@QAEXABV1@@Z
void EquipDescList::append(EquipDescList const& Other)
{
    for (std::list<EquipDesc>::const_iterator It = Other.Items.begin(); It != Other.Items.end(); ++It)
        Items.push_back(*It);
}

// 0xaa380 ??0EquipDescList@@QAE@ABUEquipDescVector@@@Z
EquipDescList::EquipDescList(EquipDescVector const& Other)
    : Items()
{
    const unsigned Count = (Other.Begin != nullptr)
        ? (unsigned)(Other.End - Other.Begin)
        : 0;
    for (unsigned Index = 0; Index < Count; ++Index)
        Items.push_back(Other.Begin[Index]);
}

// 0xaa660 ?remove_equipment_item@EquipDescList@@QAEHGH@Z -- CountToRemove == -1
// means "remove all of it"; otherwise Count -= CountToRemove and Health resets to
// a full 1.0 (removing part of a stack leaves the remainder treated as fresh).
// Returns -1 if Id isn't found, 0 if the item's Count reached 0 (erased), 1 if it
// still has Count left.
int EquipDescList::remove_equipment_item(unsigned short Id, int CountToRemove)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        EquipDesc& Item = *It;
        if (Item.Id != Id)
            continue;

        if (CountToRemove == -1)
        {
            Item.Count = 0;
        }
        else
        {
            Item.Count -= CountToRemove;
            Item.Health = 1.0f;
        }

        if (Item.Count == 0)
        {
            Items.erase(It);
            return 0;
        }
        return 1;
    }
    return -1;
}

EquipDesc* EquipDescList::find_equipment_item(unsigned short Id)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        if (It->Id == Id)
            return &*It;
    }
    return nullptr;
}

EquipDesc const* EquipDescList::find_equipment_item(unsigned short Id) const
{
    return const_cast<EquipDescList*>(this)->find_equipment_item(Id);
}

// 0xaa6e0 -- linear search by Hardpoint pointer identity (see header note).
EquipDesc* EquipDescList::find_equipment_item(CacheString const& Hp)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        if (It->Hardpoint == Hp.Value)
            return &*It;
    }
    return nullptr;
}

EquipDesc const* EquipDescList::find_equipment_item(CacheString const& Hp) const
{
    return const_cast<EquipDescList*>(this)->find_equipment_item(Hp);
}

// 0xaa710 / 0xaa750 ?traverse_equipment_type@EquipDescList@@... -- same "first
// match, or next after Prev" semantics as EquipDescVector's sibling (see header).
EquipDesc* EquipDescList::traverse_equipment_type(unsigned Type, EquipDesc const* Prev)
{
    for (std::list<EquipDesc>::iterator Node = Items.begin(); Node != Items.end(); ++Node)
    {
        EquipDesc* It = &*Node;
        if (It->ArchId == Type)
        {
            if (Prev == nullptr)
                return It;
            if (Prev == It)
                Prev = nullptr;
        }
    }
    return nullptr;
}

EquipDesc const* EquipDescList::traverse_equipment_type(unsigned Type, EquipDesc const* Prev) const
{
    return const_cast<EquipDescList*>(this)->traverse_equipment_type(Type, Prev);
}

// 0xaa790 ?find_matching_cargo@EquipDescList@@QBEPBUEquipDesc@@IHM@Z. Raw
// disassembly (0630a790-0630a8ac) rather than the Ghidra .c here: the .c's
// pseudocode reuses stack slots across VC6 std::list iterator plumbing badly
// enough that it mis-renders the non-combinable branch's running-best threshold
// as `(float)param_2` (an int-to-float conversion of 0x40000000) when the raw
// asm shows it is actually a plain float memory compare seeded with the bit
// pattern of 2.0f. Both branches are also rewritten here as direct scans over
// Items instead of reproducing retail's "restart the whole list scan every time
// a candidate is rejected" traversal trick (a mechanical consequence of only
// having forward list iterators in retail) -- the externally observable result
// (first eligible match in list order, or global best) is identical.
EquipDesc const* EquipDescList::find_matching_cargo(unsigned Type, int NeedCount, float TargetHealth) const
{
    if (arch_is_combinable(Type))
    {
        for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
        {
            EquipDesc const& Item = *It;
            if (Item.ArchId == Type && !Item.bMission && Item.Count >= NeedCount)
                return &Item;
        }
        return nullptr;
    }

    EquipDesc const* Best = nullptr;
    float BestDiff = 2.0f; // retail's literal seed (bit pattern of 2.0f, not (float)0x40000000)
    for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        if (Item.ArchId != Type || Item.bMission || Item.bEquipped)
            continue;

        // 0630a880-0630a892: FLD Health / FSUBR TargetHealth stays on the x87 stack and is
        // compared with the float BestDiff (FCOM, TEST AH,0x5, JP = not below or unordered
        // keeps the old best); only the winner is rounded to float (FSTP).
        double const Diff = (double)TargetHealth - Item.Health;
        if (Diff < BestDiff)
        {
            BestDiff = (float)Diff;
            Best = &Item;
        }
    }
    return Best;
}

// .text:0630a8e0: EquipDesc::get_cargo_space_occupied inlined per item; each unrounded
// product is added to the float running total (FADD float) and stored back as float.
float EquipDescList::get_cargo_space_occupied() const
{
    float Total = 0.0f;
    for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
        Total = (float)(It->get_cargo_space_occupied() + Total);
    return Total;
}

// ===========================================================================
// Explicit STL instantiations exported like retail's. Retail Common.dll exports every
// member of its VC6 std::list<EquipDesc> (55) and std::vector<EquipDesc> (47);
// FreeLancer.exe, Server.dll and rpclocal.dll import them by name. The port exports the
// modern instantiations; the importing declarations are in common_goods.h.
// ===========================================================================
template class COMMON_API std::list<EquipDesc>;
template class COMMON_API std::vector<EquipDesc>;

// weighted_vector<unsigned int>: retail Common.dll exports its nine members (ctors
// .text:06266310 / 06261100 / 06266330, dtor 06269110, operator= 062665c0 / 06266490,
// GetSumOfWeights 062666e0, Pick const/non-const 06266700). The importing declaration is
// in common_goods.h.
template class COMMON_API weighted_vector<unsigned int>;
