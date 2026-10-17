// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

// ============================================================================
// DamageEntry
// ============================================================================

DamageEntry::DamageEntry()
    : SubObjId(0)
    , HitPts(-1.0f)
    , Fate(SubObjFate::None)
{
    // Common.dll.c 0x20f00: zeroes SubObjId, sets HitPts to -1.0f (0xbf800000), zeroes Fate.
}

bool DamageEntry::operator==(DamageEntry const& Other) const
{
    // Common.dll.c 0x5bb20: compares all 3 real fields.
    return SubObjId == Other.SubObjId
        && HitPts == Other.HitPts
        && Fate == Other.Fate;
}

bool DamageEntry::operator!=(DamageEntry const& Other) const
{
    // Common.dll.c 0x5bb50
    return !(*this == Other);
}

bool DamageEntry::operator<(DamageEntry const& Other) const
{
    // Common.dll.c 0x5bb90: body is literally `Other.SubObjId < this->SubObjId` -- i.e.
    // retail's operator< implements what would normally be operator>'s logic. Preserved
    // as-is (mirrors operator> below) to match retail sort/compare behavior exactly.
    return Other.SubObjId < SubObjId;
}

bool DamageEntry::operator>(DamageEntry const& Other) const
{
    // Common.dll.c 0x5bbb0: body is literally `this->SubObjId < Other.SubObjId` -- the
    // mirrored swap of operator< above.
    return SubObjId < Other.SubObjId;
}

char const* DamageEntry::FateToString(SubObjFate Fate)
{
    // .text:062bbce0 (`MOV EAX,.rdata:0639ed24; RET`), the /OPT:ICF fold shared with
    // DamageList::DmgCauseToString: .rdata:0639ed24 is "???" (3f 3f 3f 00). The switch
    // that stood here ("none" / "destroyed" / ...) was fabricated (removed CMN-S2 AIS).
    (void)Fate;
    return "???";
}

// ============================================================================
// DamageList
// ============================================================================

DamageList::DamageList()
    : Count(0)
    , bDestroyed(false)
    , Cause(DamageCause::Unknown)
    , InflictorId(0)
    , InflictorOwner(0)
{
    // Common.dll.c 0x20f70: allocates the VC6 std::list sentinel/proxy node (ported as
    // an empty std::list<DamageEntry>), zeroes Count/bDestroyed/Cause/InflictorId/
    // InflictorOwner (+0x08/+0x0c/+0x10/+0x14/+0x18).
}

DamageList::DamageList(DamageList const& Other)
    : Entries(Other.Entries)
    , Count(Other.Count)
    , bDestroyed(Other.bDestroyed)
    , Cause(Other.Cause)
    , InflictorId(Other.InflictorId)
    , InflictorOwner(Other.InflictorOwner)
{
    // Common.dll.c 0x20fc0 — deep copy of the (real, unbounded) list plus scalar fields.
}

DamageList& DamageList::operator=(DamageList const& Other)
{
    // Common.dll.c 0x21070 — assign
    if (this != &Other)
    {
        Entries = Other.Entries;
        Count = Other.Count;
        bDestroyed = Other.bDestroyed;
        Cause = Other.Cause;
        InflictorId = Other.InflictorId;
        InflictorOwner = Other.InflictorOwner;
    }
    return *this;
}

DamageList::~DamageList()
{
    // Common.dll.c 0x??? — retail frees the list nodes; std::list's own dtor does that here.
}

void DamageList::add_damage_entry(unsigned short SubObjId, float HitPts, DamageEntry::SubObjFate Fate)
{
    // Common.dll.c 0x5bc00: dedups by SubObjId instead of unconditionally appending, and
    // the list is unbounded (no 8-entry cap -- that cap was fabricated in a prior port
    // revision). An existing entry for SubObjId gets HitPts replaced by min(old, new) and
    // Fate replaced by max(old, new) (i.e. keep the more severe fate); only a genuinely new
    // SubObjId allocates a new node.
    bool Found = false;
    for (DamageEntry& Entry : Entries)
    {
        if (Entry.SubObjId == SubObjId)
        {
            // .text:062bbcaa-062bbcc4: `FLD old; FCOMP new; TEST AH,0x5; JP` -- the old value
            // is kept only when strictly less; equal or unordered stores the new one.
            Entry.HitPts = (Entry.HitPts < HitPts) ? Entry.HitPts : HitPts;
            if (static_cast<int>(Fate) > static_cast<int>(Entry.Fate))
                Entry.Fate = Fate;
            Found = true;
            break;
        }
    }
    if (!Found)
    {
        DamageEntry NewEntry;
        NewEntry.SubObjId = SubObjId;
        NewEntry.HitPts = HitPts;
        NewEntry.Fate = Fate;
        Entries.push_back(NewEntry);
        ++Count;
    }

    // Common.dll.c LAB_062bbc83: SubObjId 1 is the root/hull entry; if it just took its
    // hit points to zero or below, flag the whole object destroyed.
    if (SubObjId == 1 && HitPts <= 0.0f)
        bDestroyed = true;
}

float DamageList::get_hit_pts_left(unsigned short SubObjId) const
{
    // Common.dll.c 0x5bbd0: returns the first (only, entries are deduped by SubObjId)
    // matching entry's HitPts, or -1.0f (DamageEntry's own default) if not present --
    // NOT a sum across entries, and NOT 0.0f as a prior port revision had it.
    for (DamageEntry const& Entry : Entries)
    {
        if (Entry.SubObjId == SubObjId)
            return Entry.HitPts;
    }
    return -1.0f;
}

DamageCause DamageList::get_cause() const
{
    // Common.dll.c 0x3a70 (ICF-folded with IObjInspectImpl::cobject-family)
    return Cause;
}

void DamageList::set_cause(DamageCause NewCause)
{
    // Common.dll.c 0xd2f50 (ICF-folded with TRANode::set_default_mask,
    // StateMachineMessage::set_delivery_state, EquipDesc::set_status)
    Cause = NewCause;
}

unsigned int DamageList::get_inflictor_id() const
{
    // Common.dll.c RVA 0x???
    return InflictorId;
}

void DamageList::set_inflictor_id(unsigned int Id)
{
    // Common.dll.c RVA 0xdbad0
    InflictorId = Id;
}

unsigned int DamageList::get_inflictor_owner_player() const
{
    // Common.dll.c RVA 0x???
    return InflictorOwner;
}

void DamageList::set_inflictor_owner_player(unsigned int Player)
{
    // Common.dll.c RVA 0x3afd0
    InflictorOwner = Player;
}

bool DamageList::is_destroyed() const
{
    // Common.dll.c RVA 0x???
    return bDestroyed;
}

void DamageList::set_destroyed(bool Destroyed)
{
    // Common.dll.c RVA 0xa9b50
    bDestroyed = Destroyed;
}

bool DamageList::is_inflictor_a_player() const
{
    // Common.dll.c RVA 0x20fb0 — check if inflictor owner is non-zero player ID
    return InflictorOwner != 0;
}

char const* DamageList::DmgCauseToString(DamageCause Cause)
{
    // CORRECTED 2026-09-17 (server lane, wave 15). The case list that stood here
    // was a fabrication. Retail's body is TWO instructions:
    //   .text:062bbce0  b824ed3906  MOV EAX,.rdata:DAT_0639ed24
    //   .text:062bbce5  c3          RET
    // and .rdata:0639ed24 reads 3f 3f 3f 00, i.e. the literal string "???".
    // There is no switch, no table and no per-value string anywhere in
    // Common.dll. The listing labels that address with BOTH
    //   ?DmgCauseToString@DamageList@@SAPBDW4DamageCause@@@Z          (0x5bce0)
    //   ?FateToString@DamageEntry@@SAPBDW4SubObjFate@1@@Z             (0x5bce0)
    // which is the /OPT:ICF fold that made the old reading look plausible --
    // but folding two functions that both `return "???"` proves nothing about
    // either enum.
    (void)Cause;
    return "???";
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 55
// members of its VC6 std::list<DamageEntry> (DamageList's storage). The port exports
// the modern instantiation; the importing declaration is in common_misc.h.
// ===========================================================================
template class COMMON_API std::list<DamageEntry>;
