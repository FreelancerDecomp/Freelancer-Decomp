// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_reputation.h"
#include "common_reputation_internal.h"

#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "server_savefile.h"  // ISave (declared once, Server implements it)
#include "dacom.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using CmnReputation::EnsureStoryFactionIds;
using CmnReputation::FGroup;
using CmnReputation::FindGroupConst;
using CmnReputation::GStoryFactionIds;

namespace
{
    const char kReputationSource[] = "E:\\FL\\Scratch\\Source\\Common\\Reputation.cpp";
    const unsigned kMissingGroup = 0xFFFFFFFFu;

    char const* const kStoryFactionNames[] = {
        "fc_ln_grp",
        "fc_kn_grp",
        "fc_rn_grp",
        "fc_n_grp",
        "fc_or_grp",
        "fc_f_grp",
        "fc_q_grp",
        "fc_ouk_grp",
        "fc_uk_grp",
        nullptr,
    };

    std::map<unsigned, FGroup> GGroups;
}

namespace CmnReputation
{
    std::vector<unsigned> GStoryFactionIds;

    FGroup const* FindGroupConst(unsigned GroupId)
    {
        std::map<unsigned, FGroup>::const_iterator It = GGroups.find(GroupId);
        if (It == GGroups.end())
            return nullptr;
        return &It->second;
    }

    void EnsureStoryFactionIds()
    {
        if (!GStoryFactionIds.empty())
            return;

        unsigned Index = 0;
        while (kStoryFactionNames[Index] != nullptr)
        {
            unsigned short Id = MakeId(kStoryFactionNames[Index]);
            GStoryFactionIds.push_back(Id);
            Index += 1;
        }
    }
}

namespace
{
    void ClearTString(TString<16>& Dest)
    {
        Dest.Length = 0;
        Dest.Data[0] = '\0';
        unsigned Index = 1;
        for (; Index < 16; ++Index)
            Dest.Data[Index] = '\0';
    }

    void CopyTString(TString<16>& Dest, char const* Src)
    {
        ClearTString(Dest);
        if (Src == nullptr)
            return;

        unsigned Len = 0;
        while (Src[Len] != '\0' && Len < 15)
        {
            Dest.Data[Len] = Src[Len];
            Len += 1;
        }
        Dest.Data[Len] = '\0';
        Dest.Length = Len;
    }

    FGroup* FindGroup(unsigned GroupId)
    {
        std::map<unsigned, FGroup>::iterator It = GGroups.find(GroupId);
        if (It == GGroups.end())
            return nullptr;
        return &It->second;
    }


    void ReadRepValue(FGroup& Group, INI_Reader& Reader)
    {
        // 0631f0a0-0631f108: the pair {MakeId(value 1), value 0} is INSERTED (FUN_063222f0, the
        // unique path of VC6 _Tree::insert): a group already in the map keeps its first value.
        TString<16> OtherName;
        CopyTString(OtherName, Reader.get_value_string(1));
        unsigned OtherId = (unsigned)MakeId(OtherName.Data);
        float Feeling = Reader.get_value_float(0);
        Group.Feelings.insert(std::pair<unsigned const, float>(OtherId, Feeling));
    }

    void ReadGroupBlock(INI_Reader& Reader)
    {
        TString<16> Name;
        ClearTString(Name);
        Reader.read_value();
        CopyTString(Name, Reader.get_value_string());
        unsigned Id = MakeId(Name.Data);

        // 0631ea7d-0631eaee: an empty record {Length 0, Data[0] 0, feelings map} is inserted
        // (FUN_063225b0, unique -- an existing group keeps its record), then 0631eaf3-0631eb11
        // the nickname is copied into the node. Retail UB: the inserted stack record's
        // IdsName/IdsInfo/IdsShortName are never written, but all three are overwritten below
        // on every path, so nothing of the garbage survives; operator[] value-initialises.
        FGroup& Group = GGroups[Id];
        Group.Nickname = Name;

        Reader.read_value();
        Group.IdsName = (int)Reader.get_value_float(0);

        Reader.read_value();
        if (Reader.is_value("ids_info"))
            Group.IdsInfo = (int)Reader.get_value_float(0);
        else
            Group.IdsInfo = 0;

        Reader.read_value();
        if (Reader.is_value("ids_short_name"))
            Group.IdsShortName = (int)Reader.get_value_float(0);
        else
            Group.IdsShortName = 0;

        // 0631eba4: straight back to read_header -- Startup reads no `rep` values. Group
        // feelings come from Reputation::LoadFeelings (the server's load of the initial world /
        // save game) and Reputation::set.
    }
}

bool __cdecl Reputation::Startup(char const* Path)
{
    // .text:0631e9a0-0631ed69: open(Path, false) with no null check (0631e9db-0631e9eb); false
    // when it fails (0631e9f2 skips the 0631ebb8 `bResult = 1`), true after the header loop.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return false;

    while (Reader.read_header())
    {
        if (Reader.is_header("Group"))
            ReadGroupBlock(Reader);
    }
    return true;
}

void __cdecl Reputation::Shutdown()
{
    // .text:0631e8e0-0631e998: erase(begin, end) on the group registry DAT_064018ec only; the
    // story-faction id list DAT_064018dc is left as it is.
    GGroups.clear();
}

void __cdecl Reputation::Save(ISave* Writer)
{
    // Common.dll.txt:0631eef0. No null check on Writer: retail loads it at .text:0631ef13 and
    // calls through it straight away. WriteSection is vtbl+0x04 (.text:0631ef22), WriteString
    // vtbl+0x14 (.text:0631ef32, 0631efa8).
    std::map<unsigned, FGroup>::const_iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
    {
        FGroup const& Group = It->second;
        Writer->WriteSection("Group");
        Writer->WriteString("nickname", Group.Nickname.Data);

        std::map<unsigned, float>::const_iterator FeelIt = Group.Feelings.begin();
        for (; FeelIt != Group.Feelings.end(); ++FeelIt)
        {
            // 0631ef43-0631ef79: retail looks the target group back up by id (lower_bound) to
            // print its nickname; if the id no longer names a real group (a stale relation) it
            // prints head-node+0x14 instead. Retail UB: the registry's head node is a raw
            // operator new block whose value area is never initialised, so that text is heap
            // garbage. The port prints an empty string there and still writes the line.
            FGroup const* Other = FindGroupConst(FeelIt->first);
            char const* OtherNick = (Other != nullptr) ? Other->Nickname.Data : "";
            char Buffer[260];
            _snprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, "%g, %s",
                        (double)FeelIt->second, OtherNick);
            Writer->WriteString("rep", Buffer);
        }
    }
}

void __cdecl Reputation::LoadFeelings(INI_Reader& Reader)
{
    while (Reader.read_header())
    {
        if (!Reader.is_header("Group"))
            continue;

        TString<16> Name;
        ClearTString(Name);
        Reader.read_value();
        CopyTString(Name, Reader.get_value_string());
        unsigned Id = MakeId(Name.Data);
        // 0631f075-0631f080: retail takes find()'s node+0x10 without testing for end().
        // Retail UB: for an unknown group that is the map head node, whose value area
        // operator new never initialised, and the `rep` lines are inserted into a garbage
        // map. The port cannot reproduce that and skips the lines instead.
        FGroup* Group = FindGroup(Id);
        unsigned RepCount = 0;
        while (Reader.read_value())
        {
            if (!Reader.is_value("rep"))
                continue;
            RepCount += 1;
            if (Group != nullptr)
                ReadRepValue(*Group, Reader);
        }

        if (RepCount == 0)
        {
            // 0631f120-0631f13f: the DACOM FDUMP pointer is called directly, no null test, and
            // the name printed is the found record's nickname (EBX = node+0x10, +4 = Data), which
            // differs from Name when two nicknames share a 16-bit MakeId. Name.Data only stands
            // in for the UB head-node case above.
            char const* GroupNick = (Group != nullptr) ? Group->Nickname.Data : Name.Data;
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: FACTION: save game has corrupted reputation data for group "
                  "%s, please re-create the savegame",
                  kReputationSource, 0x10e, GroupNick);
        }
    }
}

void __cdecl Reputation::FreeFeelings()
{
    std::map<unsigned, FGroup>::iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
        It->second.Feelings.clear();
}

unsigned __cdecl Reputation::get_id(TString<16> const& Name)
{
    if (Name.Length == 0)
        return kMissingGroup;
    unsigned Id = MakeId(Name.Data);
    if (FindGroupConst(Id) == nullptr)
        return kMissingGroup;
    return Id;
}

int __cdecl Reputation::get_nickname(TString<16>& Out, unsigned const& GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return -1;
    // 0631f49e-0631f4ed: the record's Data is re-measured (strlen, capped at 15) and copied
    // with its terminator; Out's bytes past the terminator are left as they were.
    char const* Src = Group->Nickname.Data;
    unsigned Len = (unsigned)strlen(Src);
    if (Len >= 16)
        Len = 15;
    memcpy(Out.Data, Src, Len);
    Out.Data[Len] = '\0';
    Out.Length = Len;
    return 0;
}

unsigned __cdecl Reputation::get_name(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsName;
}

unsigned __cdecl Reputation::get_short_name(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsShortName;
}

unsigned __cdecl Reputation::get_info_card(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsInfo;
}

unsigned __cdecl Reputation::group_count()
{
    return (unsigned)GGroups.size();
}

void __cdecl Reputation::enumerate(RepGroupCB* Callback)
{
    // 0631f600-0631f619: no null test of Callback; an empty registry never calls it.
    std::map<unsigned, FGroup>::const_iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
    {
        unsigned const& Id = It->first;
        if (Callback->Execute(&Id) == 0)
            return;
    }
}

int __cdecl Reputation::get_feelings_towards(unsigned FromId, unsigned ToId, float& Out)
{
    if (FromId == kMissingGroup || ToId == kMissingGroup)
    {
        Out = 0.0f;
        return 0;
    }

    FGroup const* From = FindGroupConst(FromId);
    if (From == nullptr)
        return -1;

    FGroup const* To = FindGroupConst(ToId);
    if (To == nullptr)
        return -2;

    Out = 0.0f;
    if (FromId == ToId)
    {
        Out = 1.0f;
        return 0;
    }

    std::map<unsigned, float>::const_iterator It = From->Feelings.find(ToId);
    if (It != From->Feelings.end())
        Out = It->second;
    return 0;
}

int __cdecl Reputation::affect_relation(unsigned const& FromId, unsigned const& ToId, float Delta)
{
    FGroup* From = FindGroup(FromId);
    if (From == nullptr)
        return -1;

    // Common.dll.txt:0631f7a0. Retail checks for an existing Feelings entry FIRST and only
    // validates that ToId still names a real group when no entry exists yet -- a relation left
    // over for a group id that has since disappeared from GGroups is still updated in place,
    // not rejected with -2. Validating ToId unconditionally (as an earlier port draft did)
    // would wrongly refuse that update.
    std::map<unsigned, float>::iterator It = From->Feelings.find(ToId);
    if (It != From->Feelings.end())
    {
        // 0631f873-0631f8ab: Delta + old stays on the FPU stack for the clamp. FCOM 1.0f /
        // TEST AH,0x5 / JNP keeps the sum only when it is ordered-less than 1.0, so a NaN
        // becomes 1.0; FLD -1.0f / FCOMP / TEST AH,0x41 / JNZ keeps it unless -1.0 > sum.
        double Value = (double)Delta + (double)It->second;
        if (!(Value < 1.0))
            Value = 1.0;
        if (-1.0 > Value)
            Value = -1.0;
        It->second = (float)Value;
        return 0;
    }

    if (FindGroupConst(ToId) == nullptr)
        return -2;

    // 0631f81b-0631f849: the same clamp on Delta (TEST AH,0x5 / JP -> 1.0 for >= 1 or NaN),
    // then the pair is inserted (FUN_063222f0, 0631f865).
    float Value = Delta;
    if (!(Value < 1.0f))
        Value = 1.0f;
    if (-1.0f > Value)
        Value = -1.0f;
    From->Feelings.insert(std::pair<unsigned const, float>(ToId, Value));
    return 0;
}

bool __cdecl Reputation::IsStoryFaction(unsigned GroupId)
{
    EnsureStoryFactionIds();
    unsigned Index = 0;
    for (; Index < GStoryFactionIds.size(); ++Index)
    {
        if (GStoryFactionIds[Index] == GroupId)
            return true;
    }
    return false;
}

void __cdecl Reputation::set(TString<16> const& Name, Relation* const Rels, unsigned Count)
{
    // .text:0631f260-0631f3ea. Id = MakeId(Name.Data), zero-extended (0631f289-0631f290), looked
    // up in the group registry DAT_064018ec (FUN_06301640).
    unsigned const Id = MakeId(Name.Data);
    std::map<unsigned, FGroup>::iterator It = GGroups.find(Id);
    if (It != GGroups.end())
    {
        // 0631f2be-0631f2d0: an existing group's feelings map is erased(begin, end).
        It->second.Feelings.clear();
    }
    else
    {
        // 0631f2da-0631f394: a new record is built on the stack -- only its nickname's Length
        // (local_80 = 0) and first character (local_7c = 0) are written, the feelings map is
        // constructed (FUN_06322280) -- copied into the pair {Id, record} (FUN_06322070) and
        // inserted (FUN_063225b0). The new group therefore has an EMPTY nickname, not Name.
        // Retail UB: IdsName/IdsInfo/IdsShortName (+0x14/+0x18/+0x1c of the stack record) are
        // never written and carry stack garbage into the registry; the port value-initialises
        // the record, so they are 0.
        FGroup Record = FGroup();
        Record.Nickname.Length = 0;
        Record.Nickname.Data[0] = '\0';
        It = GGroups.insert(std::pair<unsigned const, FGroup>(Id, Record)).first;
    }

    // 0631f397-0631f3d1: each relation is INSERTED (FUN_063222f0, a unique insert), so for a
    // repeated GroupId in Rels the first value wins.
    // Retail UB: the loop at 0631f3b0 has no exit -- after the `Count == 0` test (0631f39e JBE)
    // it advances ESI by 8 and jumps back unconditionally (0631f3d1 `eb dd`), reading past
    // Rels until the read faults. The port stops after Count entries, which is what the
    // `Count` parameter and the 0631f39e test describe.
    if (Count == 0)
        return;
    std::map<unsigned, float>& Feelings = It->second.Feelings;
    unsigned Index = 0;
    for (; Index < Count; ++Index)
        Feelings.insert(std::pair<unsigned const, float>(Rels[Index].GroupId, Rels[Index].Feeling));
}
