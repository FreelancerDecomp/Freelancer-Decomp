// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU48 (06f4ed80-06f55e20): faction properties, faction name tables, faction
// goods and reputation empathy. Lane CNT-GCS (docs/re/content.md, TU map).
//
// Listing: DLLS\BIN\Content.dll.txt. The std::map / std::vector instantiations retail emits
// for the four tables (map ctors FUN_06f528b0 / FUN_06f52930 / FUN_06f529b0, _Tidy
// LAB_06f4f1f0 / LAB_06f4f300 / LAB_06f507b0, insert FUN_06f53110 / FUN_06f53350 /
// FUN_06f53590, _Erase FUN_06f53f90 / FUN_06f546a0 / FUN_06f54d40, erase FUN_06f53b40 /
// FUN_06f54250 / FUN_06f54960, node helpers 06f53960-06f55310, vector insert FUN_06f537a0,
// find FUN_06f52e90) are the std containers of FactionProps.h.

#include "FactionProps.h"

#include "ContentInternal.h"    // OpenContentDataIni, GContentNextUniqueId
#include "ContentSort.h"        // ContentSort (VC6 std::sort)
#include "ContentStaticIds.h"
#include "NpcShips.h"           // ParseSpaceCostume
#include "RandomMissionGen.h"   // FindVoiceProperties, FVoiceProperties
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"   // pub::SetTimer, pub::Controller::TimerExpired

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

// 06f4ef80: FUN_06eac620 (vector ctor) + atexit FUN_06f4efa0 (vector dtor FUN_06eec8d0).
std::vector<unsigned int> GFactionGroups;
// 06f4f1b0: map ctor FUN_06f528b0 + atexit FUN_06f4f1e0.
FFactionPropsMap GFactionProps;
// 06f4f2c0: map ctor FUN_06f52930 + atexit FUN_06f4f2f0.
FFactionNamesMap GFactionNames;
// 06f50770: map ctor FUN_06f529b0 + atexit FUN_06f507a0.
FFactionEmpathyMap GFactionEmpathy;

namespace
{
    // 06f4efc0 / 06f4efe0 / 06f4f000 (_initterm, in this order): three timer message ids
    // drawn from the module id generator DAT_06fce76c, stored in DAT_06fd0cd4 (pilot
    // names, sender FUN_06f51ee0) / DAT_06fd0cd0 (large ship names, FUN_06f51fe0) /
    // DAT_06fd0ccc (formation designations, FUN_06f520d0); all read by FUN_06f513c0.
    unsigned int const GFactionNameMsgPilot = GContentNextUniqueId++;
    unsigned int const GFactionNameMsgLargeShip = GContentNextUniqueId++;
    unsigned int const GFactionNameMsgFormation = GContentNextUniqueId++;

    // The random draw of TU48: rand() * Count, then the signed division by 32768 of the
    // CDQ / AND 0x7fff / ADD / SAR 0xf idiom.
    int FactionRandIndex(int Count)
    {
        return (rand() * Count) / 32768;
    }

    // FUN_06f53ab0 / the inlined FUN_06f55310 loops: find by the pair only.
    std::vector<FFactionNamePairUse>::iterator FindPairUse(std::vector<FFactionNamePairUse>* Uses,
                                                           int First, int Second)
    {
        std::vector<FFactionNamePairUse>::iterator It = Uses->begin();
        while (It != Uses->end() && !(It->First == First && It->Second == Second))
            ++It;
        return It;
    }

    // FUN_06f52f40 / the inlined name loops: find by the name.
    std::vector<FFactionNameUse>::iterator FindNameUse(std::vector<FFactionNameUse>* Uses, int Name)
    {
        std::vector<FFactionNameUse>::iterator It = Uses->begin();
        while (It != Uses->end() && It->Name != Name)
            ++It;
        return It;
    }

    // FUN_06f52720 / FUN_06f52ff0 / FUN_06f51db0 bodies: one more use of the pair, or a
    // new {First, Second, 1} at the end (FUN_06f38170 / FUN_06f6f3e0).
    void AddPairUse(std::vector<FFactionNamePairUse>* Uses, int First, int Second)
    {
        std::vector<FFactionNamePairUse>::iterator It = FindPairUse(Uses, First, Second);
        if (It != Uses->end())
        {
            ++It->UseCount;
            return;
        }
        FFactionNamePairUse Use;
        Use.First = First;
        Use.Second = Second;
        Use.UseCount = 1;
        Uses->insert(Uses->end(), 1, Use);
    }

    // FUN_06f52ec0 / FUN_06ef6bb0 paths of FUN_06f51b80: one more use, or {Name, 1} at the
    // end (FUN_06f96770).
    void AddNameUse(std::vector<FFactionNameUse>* Uses, int Name)
    {
        std::vector<FFactionNameUse>::iterator It = FindNameUse(Uses, Name);
        if (It != Uses->end())
        {
            ++It->UseCount;
            return;
        }
        FFactionNameUse Use;
        Use.Name = Name;
        Use.UseCount = 1;
        Uses->insert(Uses->end(), 1, Use);
    }

    // FUN_06f52810 (thiscall, RET 8) and the immediate paths of FUN_06f51ee0 /
    // FUN_06f520d0: one use less, *OutCount = the new count when given, the entry erased
    // at zero (FUN_06f53a60); a miss sets *OutCount = 0 and answers false.
    bool ReleasePairUse(std::vector<FFactionNamePairUse>* Uses, int First, int Second,
                        int* OutCount)
    {
        std::vector<FFactionNamePairUse>::iterator It = FindPairUse(Uses, First, Second);
        if (It == Uses->end())
        {
            if (OutCount != nullptr)
                *OutCount = 0;
            return false;
        }
        --It->UseCount;
        if (OutCount != nullptr)
            *OutCount = It->UseCount;
        if (It->UseCount == 0)
            Uses->erase(It);
        return true;
    }

    // FUN_06f526a0 (thiscall, RET 8) and the immediate path of FUN_06f51fe0: the same on
    // a single name.
    bool ReleaseNameUse(std::vector<FFactionNameUse>* Uses, int Name, int* OutCount)
    {
        std::vector<FFactionNameUse>::iterator It = FindNameUse(Uses, Name);
        if (It == Uses->end())
        {
            if (OutCount != nullptr)
                *OutCount = 0;
            return false;
        }
        --It->UseCount;
        if (OutCount != nullptr)
            *OutCount = It->UseCount;
        if (It->UseCount == 0)
            Uses->erase(It);
        return true;
    }

    // The predicate of the empathy-rate sort FUN_06f55a00 / FUN_06f55cd0 (FLD a.rate,
    // FCOMP b.rate, TEST AH,5: a.Rate < b.Rate).
    bool EmpathyRateLess(FFactionEmpathyRate const& A, FFactionEmpathyRate const& B)
    {
        return A.Rate < B.Rate;
    }

    // The three release timers: pub::SetTimer(&SpaceSupport id DAT_06fd1c74,
    // {MessageId, Group}, 30.0f) (DAT_06fbb78c / DAT_06fbb790 / DAT_06fbb794).
    void SetFactionNameTimer(unsigned int MessageId, unsigned int Group)
    {
        pub::Controller::TimerExpired Timer;
        Timer.Param1 = MessageId;
        Timer.Param2 = Group;
        pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    }

    // LAB_06f50160 (vtable 06fbb7a4, RET 4): appends GroupId to GFactionGroups when it is
    // not there yet; the second linear find that follows (06f50196-06f501b9) has no
    // effect. Always answers 1.
    struct FFactionGroupCollector : public pub::Reputation::Enumerator
    {
        char Execute(unsigned int GroupId) override
        {
            if (std::find(GFactionGroups.begin(), GFactionGroups.end(), GroupId) ==
                GFactionGroups.end())
            {
                GFactionGroups.insert(GFactionGroups.end(), 1, GroupId);   // FUN_06f937c0
            }
            return 1;
        }
    };

    // LAB_06f50c40 (vtable 06fbb928, RET 4): ++Count, answers 1.
    struct FFactionGroupCounter : public pub::Reputation::Enumerator
    {
        int Count = 0;              // +0x04

        char Execute(unsigned int GroupId) override
        {
            (void)GroupId;
            ++Count;
            return 1;
        }
    };

    // The reputation clamp of FUN_06f50c50: min(Value, 1.0f - 0.1) first, then the
    // lower bound -1.0f + 0.1 (x87, DAT_06fbb788 / DAT_06fbb784 are float, DAT_06fbb930
    // is double 0.1). Sum is the unrounded x87 sum, SumF the float the listing stores.
    float ClampFactionReputation(double Sum)
    {
        double const Upper = static_cast<double>(1.0f) - 0.1;
        double const Lower = static_cast<double>(-1.0f) + 0.1;
        float const SumF = static_cast<float>(Sum);

        // 06f50cb1-06f50cc4: Upper < Sum (unrounded) -> Upper, else the stored float.
        double const Candidate = (Upper < Sum) ? Upper : static_cast<double>(SumF);
        // 06f50cd4-06f50cdd (TEST AH,0x41 + JNZ: Candidate <= Lower -> Lower).
        if (Candidate <= Lower)
            return static_cast<float>(Lower);
        // 06f50cdf-06f50cf4: Upper < SumF -> Upper, else SumF.
        if (Upper < static_cast<double>(SumF))
            return static_cast<float>(Upper);
        return SumF;
    }
}

// ---------------------------------------------------------------------------------------
// FFactionProps
// ---------------------------------------------------------------------------------------

// FUN_06f4f060 (06f4f060-06f4f1a6).
FFactionProps::FFactionProps()
    : CommReferrable()
    , JumpPreference(FactionJumpAny)
    , ScanChance(1.0f)
    , bScanAnnounce(true)
    , Legality(FactionLegalityUnset)
    , bSingularNickname(true)
    , McCostume(0)
{
}

// FUN_06f4f3d0 (06f4f3d0-06f4f429).
bool FFactionProps::GetFormation(unsigned int const* Key, unsigned int* Out) const
{
    std::map<unsigned int, unsigned int>::const_iterator It = Formations.find(*Key);
    if (It == Formations.end())
        return false;
    *Out = It->second;
    return true;
}

// FUN_06f4f430 (06f4f430-06f4f611).
bool FFactionProps::PickSpaceCostume(int Gender, Costume* Out) const
{
    // 06f4f456-06f4f4fa: the index list is reserved to the costume count first.
    std::vector<unsigned int> Matches;
    if (!SpaceCostumes.empty())
        Matches.reserve(SpaceCostumes.size());

    // 06f4f504-06f4f573: the out-gender slot is the argument's own stack slot in retail.
    for (unsigned int Index = 0; Index < SpaceCostumes.size(); ++Index)
    {
        int SkeletonGender = Gender;
        pub::GF::GetCostumeSkeletonGender(SpaceCostumes[Index], SkeletonGender);
        if (Gender == SkeletonGender)
            Matches.insert(Matches.end(), 1, Index);                    // FUN_06f937c0
    }

    // 06f4f575-06f4f5fd.
    if (Matches.empty())
        return false;

    int const MatchCount = static_cast<int>(Matches.size());
    int const Pick = (rand() * MatchCount) / 32768;
    Costume const& Source = SpaceCostumes[Matches[Pick]];
    Out->PartIds[0] = Source.PartIds[0];
    Out->PartIds[1] = Source.PartIds[1];
    Out->PartIds[2] = Source.PartIds[2];
    Out->PartIds[3] = Source.PartIds[3];
    Out->AccessoryCount = Source.AccessoryCount;
    for (int Accessory = 0; Accessory < static_cast<int>(Out->AccessoryCount); ++Accessory)
        Out->AccessoryIds[Accessory] = Source.AccessoryIds[Accessory];
    return true;
}

// FUN_06f4f620 (06f4f620-06f4f7b7).
bool FFactionProps::PickVoice(unsigned int Roles, unsigned int* Out) const
{
    // 06f4f63c-06f4f6a6: a copy of the voice list.
    std::vector<unsigned int> Candidates(Voices.begin(), Voices.end());

    while (!Candidates.empty())
    {
        // 06f4f6d5-06f4f720: one candidate draws index 0 without calling rand().
        int const Count = static_cast<int>(Candidates.size());
        int Pick = 0;
        if (Count != 1)
            Pick = (rand() * Count) / 32768;

        FVoiceProperties* Props = FindVoiceProperties(&Candidates[Pick]);   // FUN_06fa6e50
        if (Props != nullptr && Props->SupportsRoles(Roles))                // FUN_06fa6e30
        {
            *Out = Candidates[Pick];
            return true;
        }
        // 06f4f73c-06f4f767: erase the candidate.
        Candidates.erase(Candidates.begin() + Pick);
    }
    return false;
}

// FUN_06f4f7c0 (06f4f7c0-06f4f860).
bool FFactionProps::PickGender(int* Out) const
{
    unsigned int MaleCount = 0;
    for (std::vector<unsigned int>::const_iterator It = Voices.begin(); It != Voices.end(); ++It)
    {
        FVoiceProperties* Props = FindVoiceProperties(&*It);
        if (Props != nullptr && Props->SupportsRoles(1))
            ++MaleCount;
    }

    if (Voices.empty())
        return false;

    // 06f4f839-06f4f85b: CMP + SBB: an unsigned draw below MaleCount -> 1, else 2.
    int const Count = static_cast<int>(Voices.size());
    unsigned int const Draw = static_cast<unsigned int>((rand() * Count) / 32768);
    *Out = (Draw < MaleCount) ? 1 : 2;
    return true;
}

// ---------------------------------------------------------------------------------------
// FFactionNames
// ---------------------------------------------------------------------------------------

FFactionNameList::FFactionNameList()
    : Count(0)
{
}

void FFactionNameList::AddRange(INI_Reader* Reader)
{
    FFactionNameRange Range;
    Range.First = Reader->get_value_int(0);
    if (Reader->is_value_empty(1))
        Range.Last = Range.First;
    else
        Range.Last = Reader->get_value_int(1);
    Ranges.insert(Ranges.end(), 1, Range);                                // FUN_06f96770
    Count += Range.Last - Range.First + 1;
}

// FUN_06f4f020 (06f4f020-06f4f04b): unsigned compares (JA / JBE).
int FFactionNameList::GetName(unsigned int Index) const
{
    std::vector<FFactionNameRange>::const_iterator Range = Ranges.begin();
    unsigned int Size = static_cast<unsigned int>(Range->Last - Range->First + 1);
    while (Size <= Index)
    {
        Index -= Size;
        ++Range;
        Size = static_cast<unsigned int>(Range->Last - Range->First + 1);
    }
    return Range->First + static_cast<int>(Index);
}

// FUN_06f4f050 (06f4f050-06f4f059).
bool FFactionNameList::IsEmpty() const
{
    return Count == 0;
}

// FUN_06f50ff0 (06f50ff0-06f5115c). Group (+0x00) is not written.
FFactionNames::FFactionNames()
    : LargeShipDesig(0)
    , ShortGroupName(0)
{
    RankDesig[0] = 0;
    RankDesig[1] = 0;
    RankDesig[2] = 0;
    for (int Index = 0; Index < 8; ++Index)
        NameIds[Index] = 0;
    RankThreshold[0] = 0;
    RankThreshold[1] = 0;
}

// FUN_06f52230 (06f52230-06f5269d).
bool FFactionNames::ReadIniValue(INI_Reader* Reader)
{
    if (Reader->is_value("affiliation"))
    {
        pub::Reputation::GetReputationGroup(Group, Reader->get_value_string());
        pub::Reputation::GetShortGroupName(Group, ShortGroupName);
        return false;
    }
    if (Reader->is_value("firstname"))
        return false;
    if (Reader->is_value("firstname_male"))
    {
        MaleFirstNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("firstname_female"))
    {
        FemaleFirstNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("lastname"))
    {
        LastNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("formation_desig"))
    {
        FormationDesigs.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("large_ship_names"))
    {
        // 06f5248d-06f52604: the same append, inlined as vector::insert(end, 1, range).
        LargeShipNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("large_ship_desig"))
    {
        LargeShipDesig = Reader->get_value_int(0);
        return false;
    }
    if (Reader->is_value("rank_desig"))
    {
        for (int Index = 0; Index < 3; ++Index)
            RankDesig[Index] = static_cast<unsigned int>(Reader->get_value_int(Index));
        RankThreshold[0] = Reader->get_value_int(3);
        RankThreshold[1] = Reader->get_value_int(4);
    }
    return false;
}

// FUN_06f521d0.
int FFactionNames::GetLargeShipDesig() const
{
    return LargeShipDesig;
}

// FUN_06f521e0.
unsigned int FFactionNames::GetShortGroupName() const
{
    return ShortGroupName;
}

// FUN_06f521f0.
unsigned int FFactionNames::GetNameId(int Index) const
{
    return NameIds[Index];
}

// FUN_06f52200 (signed compares, JGE).
unsigned int FFactionNames::GetRankDesig(int const* Level) const
{
    if (*Level < RankThreshold[0])
        return RankDesig[0];
    if (*Level < RankThreshold[1])
        return RankDesig[1];
    return RankDesig[2];
}

// FUN_06f51520 (06f51520-06f51919).
bool FFactionNames::PickPilotName(int* OutFirst, int* OutLast, int Gender, bool bReserve)
{
    FFactionNameList const* FirstNames = (Gender == 1) ? &MaleFirstNames : &FemaleFirstNames;
    int const FirstCount = FirstNames->Count;
    int const LastCount = LastNames.Count;
    int First = 0;                                                  // EBX
    int Last = 0;                                                   // EBP
    bool bResult;

    // 06f5154f-06f5158c: unsigned compare of the use count with the pair count.
    if (static_cast<unsigned int>(PilotNameUses.size()) <
        static_cast<unsigned int>(LastCount * FirstCount))
    {
        if (OutFirst != nullptr && OutLast != nullptr)
        {
            // 06f515ac-06f51687. The inner walk compares the counter before its
            // increment, so a first name whose last names are all taken ends with the
            // counter at LastCount + 1 and is accepted like a free pair.
            int Outer = 0;
            int const FirstStart = FactionRandIndex(FirstCount);
            if (FirstCount > 0)
            {
                for (;;)
                {
                    First = FirstNames->GetName(
                        static_cast<unsigned int>((FirstStart + Outer) % FirstCount));  // FUN_06f4f020
                    int Inner = 0;
                    int const LastStart = FactionRandIndex(LastCount);
                    for (;;)
                    {
                        Last = LastNames.GetName(
                            static_cast<unsigned int>((LastStart + Inner) % LastCount));
                        if (FindPairUse(&PilotNameUses, First, Last) == PilotNameUses.end())
                            break;
                        int const Previous = Inner;
                        ++Inner;
                        if (!(Previous < LastCount))
                            break;
                    }
                    if (Inner != LastCount)
                        break;
                    ++Outer;
                    if (!(Outer < FirstCount))
                        break;
                }
            }
            bResult = Outer < FirstCount;
        }
        else if (OutFirst == nullptr && OutLast != nullptr)
        {
            // 06f51699-06f51733: last name only, key {1, last}.
            First = 1;
            int Inner = 0;
            int const Start = FactionRandIndex(LastCount);
            for (;;)
            {
                Last = LastNames.GetName(static_cast<unsigned int>((Start + Inner) % LastCount));
                if (FindPairUse(&PilotNameUses, 1, Last) == PilotNameUses.end())
                    break;
                int const Previous = Inner;
                ++Inner;
                if (!(Previous < LastCount))
                    break;
            }
            bResult = Inner != LastCount;
        }
        else
        {
            // 06f5173d-06f517d3: first name only, key {first, 1}; the draw and the modulus
            // use the last-name count.
            Last = 1;
            int Inner = 0;
            int const Start = FactionRandIndex(LastCount);
            for (;;)
            {
                First = FirstNames->GetName(
                    static_cast<unsigned int>((Start + Inner) % LastCount));
                if (FindPairUse(&PilotNameUses, First, 1) == PilotNameUses.end())
                    break;
                int const Previous = Inner;
                ++Inner;
                if (!(Previous < LastCount))
                    break;
            }
            bResult = Inner != LastCount;
        }
    }
    else
    {
        // 06f517dd-06f5187d: every pair is in use.
        if (OutFirst != nullptr)
            First = FirstNames->GetName(static_cast<unsigned int>(FactionRandIndex(FirstCount)));
        else
            First = 1;
        if (OutLast != nullptr)
            Last = LastNames.GetName(static_cast<unsigned int>(FactionRandIndex(LastCount)));
        else
            First = 1;                                      // 06f51878 writes EBX, not EBP
        bResult = false;
    }

    // 06f51882-06f518f5.
    if (bReserve)
        AddPairUse(&PilotNameUses, First, Last);
    if (OutFirst != nullptr)
        *OutFirst = First;
    if (OutLast != nullptr)
        *OutLast = Last;
    return bResult;
}

// FUN_06f51920 (06f51920-06f51b75).
bool FFactionNames::PickFormationDesig(int* OutDesig, int* OutNumber, bool bReserve)
{
    int const Count = FormationDesigs.Count;

    if (static_cast<unsigned int>(FormationDesigUses.size()) < static_cast<unsigned int>(Count * 20))
    {
        // 06f5196b-06f51a46: same walk as the pilot names, 20 numbers per designation.
        int Outer = 0;
        int const Start = FactionRandIndex(Count);
        if (Count > 0)
        {
            for (;;)
            {
                *OutDesig = FormationDesigs.GetName(static_cast<unsigned int>((Start + Outer) % Count));
                int Inner = 0;
                int const NumberStart = (rand() * 20) / 32768;
                for (;;)
                {
                    int const Number = (NumberStart + Inner) % 20;
                    *OutNumber = Number;
                    if (FindPairUse(&FormationDesigUses, *OutDesig, Number) == FormationDesigUses.end())
                        break;
                    int const Previous = Inner;
                    ++Inner;
                    if (!(Previous < 20))
                        break;
                }
                if (Inner != 20)
                    break;
                ++Outer;
                if (!(Outer < Count))
                    break;
            }
        }
        // 06f51a4c-06f51a74: FUN_06f52720.
        if (bReserve)
            AddPairUse(&FormationDesigUses, *OutDesig, *OutNumber);
        return true;
    }

    // 06f51a85-06f51b67.
    *OutDesig = FormationDesigs.GetName(static_cast<unsigned int>(FactionRandIndex(Count)));
    int const Number = (rand() * 20) / 32768;
    *OutNumber = Number;
    if (bReserve)
        AddPairUse(&FormationDesigUses, *OutDesig, Number);                 // FUN_06f52ff0
    return false;
}

// FUN_06f51b80 (06f51b80-06f51da2).
bool FFactionNames::PickLargeShipName(int* OutName, bool bReserve)
{
    int const Count = LargeShipNames.Count;
    int Name;

    if (static_cast<unsigned int>(LargeShipNameUses.size()) < static_cast<unsigned int>(Count))
    {
        // 06f51bb4-06f51c1f: the first free name from a random start.
        int Draw = FactionRandIndex(Count);
        for (;;)
        {
            Name = LargeShipNames.GetName(static_cast<unsigned int>(Draw % Count));
            ++Draw;
            if (FindNameUse(&LargeShipNameUses, Name) == LargeShipNameUses.end())
                break;
        }
        if (bReserve)
            AddNameUse(&LargeShipNameUses, Name);                           // FUN_06f52ec0
        *OutName = Name;
        return true;
    }

    // 06f51ca9-06f51d9c.
    Name = LargeShipNames.GetName(static_cast<unsigned int>(FactionRandIndex(Count)));
    if (bReserve)
        AddNameUse(&LargeShipNameUses, Name);
    *OutName = Name;
    return false;
}

// FUN_06f51db0 (06f51db0-06f51ed5).
void FFactionNames::AddFormationDesigUse(int Desig, int Number)
{
    AddPairUse(&FormationDesigUses, Desig, Number);
}

// FUN_06f51ee0 (06f51ee0-06f51fd0).
void FFactionNames::ReleasePilotName(int First, int Last, bool bImmediate)
{
    if (bImmediate)
    {
        ReleasePairUse(&PilotNameUses, First, Last, nullptr);
        return;
    }
    FFactionNamePair Pending;
    Pending.First = First;
    Pending.Second = Last;
    PendingPilotNames.push_back(Pending);
    SetFactionNameTimer(GFactionNameMsgPilot, Group);
}

// FUN_06f51fe0 (06f51fe0-06f520be).
void FFactionNames::ReleaseLargeShipName(int Name, bool bImmediate)
{
    if (bImmediate)
    {
        ReleaseNameUse(&LargeShipNameUses, Name, nullptr);
        return;
    }
    PendingLargeShipNames.push_back(Name);                                  // FUN_06eb6740
    SetFactionNameTimer(GFactionNameMsgLargeShip, Group);
}

// FUN_06f520d0 (06f520d0-06f521c0).
void FFactionNames::ReleaseFormationDesig(int Desig, int Number, bool bImmediate)
{
    if (bImmediate)
    {
        ReleasePairUse(&FormationDesigUses, Desig, Number, nullptr);
        return;
    }
    FFactionNamePair Pending;
    Pending.First = Desig;
    Pending.Second = Number;
    PendingFormationDesigs.push_back(Pending);
    SetFactionNameTimer(GFactionNameMsgFormation, Group);
}

// FUN_06f513c0 (06f513c0-06f5151c).
void __cdecl HandleFactionNameTimer(pub::Controller::TimerExpired const* Timer)
{
    unsigned int const Group = Timer->Param2;
    if (Timer->Param1 == GFactionNameMsgPilot)
    {
        FFactionNames* Names = GetFactionNames(&Group);                     // FUN_06f7b0f0
        FFactionNamePair const Front = Names->PendingPilotNames.front();
        Names->ReleasePilotName(Front.First, Front.Second, true);
        Names->PendingPilotNames.pop_front();                               // FUN_06ecfe80
    }
    else if (Timer->Param1 == GFactionNameMsgLargeShip)
    {
        FFactionNames* Names = GetFactionNames(&Group);
        int const Front = Names->PendingLargeShipNames.front();
        ReleaseNameUse(&Names->LargeShipNameUses, Front, nullptr);         // FUN_06f526a0
        Names->PendingLargeShipNames.pop_front();                           // FUN_06f68e40
    }
    else if (Timer->Param1 == GFactionNameMsgFormation)
    {
        FFactionNames* Names = GetFactionNames(&Group);
        FFactionNamePair const Front = Names->PendingFormationDesigs.front();
        ReleasePairUse(&Names->FormationDesigUses, Front.First, Front.Second, nullptr);  // FUN_06f52810
        Names->PendingFormationDesigs.pop_front();                          // FUN_06f68e40
    }
}

// ---------------------------------------------------------------------------------------
// Loaders
// ---------------------------------------------------------------------------------------

// FUN_06f4f870 (06f4f870-06f50133).
void __cdecl LoadFactionProps(INI_Reader* Reader)
{
    // Frame locals that outlive a section: the affiliation [ESP+0x20], the good id
    // [ESP+0x3c] and the voice id [ESP+0x2c]. Retail never initialises them; the port
    // starts them at 0.
    unsigned int Group = 0;
    unsigned int Good = 0;
    unsigned int Voice = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("FactionProps"))
            continue;

        // 06f4f8bd-06f4f8db: [ESP+0x48] props, [ESP+0x270] names.
        FFactionProps Props;
        FFactionNames Names;

        while (Reader->read_value())
        {
            if (Reader->is_value("affiliation"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string());
                Names.ReadIniValue(Reader);                              // result ignored
            }
            else if (Reader->is_value("scan_announce"))
            {
                Props.bScanAnnounce = Reader->get_value_bool(0);
            }
            else if (Reader->is_value("formation"))
            {
                // 06f4f95a-06f4f9c8: inserted only when the key is new.
                unsigned int const Key = CreateID(Reader->get_value_string(0));
                unsigned int const Value = CreateID(Reader->get_value_string(1));
                if (Props.Formations.find(Key) == Props.Formations.end())    // FUN_06f7b0f0
                    Props.Formations.insert(std::make_pair(Key, Value));     // FUN_06f003e0
            }
            else if (Reader->is_value("scan_chance"))
            {
                Props.ScanChance = Reader->get_value_float(0);
            }
            else if (Reader->is_value("npc_ship"))
            {
                unsigned int const Arch = CreateID(Reader->get_value_string());
                Props.NpcShips.insert(Props.NpcShips.end(), 1, Arch);         // FUN_06f937c0
            }
            else if (Reader->is_value("scan_for_cargo"))
            {
                // 06f4fa42-06f4fb39: {archetype, level}; appended when not present.
                FFactionScanCargo Cargo;
                Cargo.Archetype = 0;
                Cargo.Level = 0;
                pub::GetGoodID(Good, Reader->get_value_string(0));
                pub::SpaceObj::GetArchetypeID(Good, Cargo.Archetype);
                int const Level = Reader->get_value_int(1);
                if (Level == 0)
                    Cargo.Level = 0;
                else if (Level == 1)
                    Cargo.Level = 1;
                else if (Level == 2)
                    Cargo.Level = 2;

                std::vector<FFactionScanCargo>::iterator It = Props.ScanForCargo.begin();
                while (It != Props.ScanForCargo.end() &&
                       !(It->Archetype == Cargo.Archetype && It->Level == Cargo.Level))  // FUN_06f50140
                {
                    ++It;
                }
                if (It == Props.ScanForCargo.end())
                    Props.ScanForCargo.insert(Props.ScanForCargo.end(), Cargo);       // FUN_06f537a0
            }
            else if (Props.CommReferrable::read_comm_ini_data(*Reader))
            {
                // 06f4fb3e-06f4fb4b: msg_id_prefix, a direct (non-virtual) import call.
            }
            else if (Reader->is_value("mc_costume"))
            {
                pub::GetCostumeID(Props.McCostume, Reader->get_value_string(0));
            }
            else if (Reader->is_value("space_costume"))
            {
                // 06f4fb89-06f4fbd6: head, body, both hands and the count cleared; the
                // accessory slots are not.
                Costume Appearance;
                Appearance.PartIds[0] = 0;
                Appearance.PartIds[1] = 0;
                Appearance.PartIds[2] = 0;
                Appearance.PartIds[3] = 0;
                Appearance.AccessoryCount = 0;
                ParseSpaceCostume(&Appearance, Reader);                   // FUN_06f31390
                Props.SpaceCostumes.insert(Props.SpaceCostumes.end(), 1, Appearance);  // FUN_06ed23d0
            }
            else if (Reader->is_value("voice"))
            {
                pub::GetVoiceID(Voice, Reader->get_value_string());
                Props.Voices.insert(Props.Voices.end(), 1, Voice);          // FUN_06f937c0
            }
            else if (Names.ReadIniValue(Reader))
            {
                // 06f4fc24-06f4fc33.
            }
            else if (Reader->is_value("jump_preference"))
            {
                // 06f4fc4c-06f4fce1: the value is copied into a TString<0x20> (null ->
                // empty, FUN_06ea9f40) and compared byte-wise (REPE CMPSB, 9 bytes) with
                // "jumpgate", then "jumphole"; anything else is 0. A copy truncated to 31
                // characters can never equal either word, so comparing the value itself
                // is the same test.
                char const* Value = Reader->get_value_string(0);
                if (Value != nullptr && strcmp(Value, "jumpgate") == 0)
                    Props.JumpPreference = FactionJumpGate;
                else if (Value != nullptr && strcmp(Value, "jumphole") == 0)
                    Props.JumpPreference = FactionJumpHole;
                else
                    Props.JumpPreference = FactionJumpAny;
            }
            else if (Reader->is_value("assassinate_target"))
            {
                unsigned int const Target = CreateID(Reader->get_value_string(0));
                Props.AssassinateTargets.insert(Props.AssassinateTargets.end(), 1, Target);
            }
            else if (Reader->is_value("Legality"))
            {
                if (_stricmp(Reader->get_value_string(0), "lawful") == 0)
                    Props.Legality = FactionLegalityLawful;
                else if (_stricmp(Reader->get_value_string(0), "unlawful") == 0)
                    Props.Legality = FactionLegalityUnlawful;
            }
            else if (Reader->is_value("nickname_plurality"))
            {
                if (_stricmp(Reader->get_value_string(0), "singular") == 0)
                    Props.bSingularNickname = true;
                else if (_stricmp(Reader->get_value_string(0), "plural") == 0)
                    Props.bSingularNickname = false;
            }
        }

        // 06f4fde1-06f4fe13: legality name ids.
        if (Props.Legality == FactionLegalityLawful || Props.Legality == FactionLegalityUnlawful)
        {
            unsigned int const LegalityId =
                (Props.Legality == FactionLegalityLawful) ? 0x4074u : 0x4075u;
            Names.NameIds[7] = LegalityId;
            Names.NameIds[3] = LegalityId;
            Names.NameIds[2] = 0x4076u;
            Names.NameIds[4] = LegalityId;
        }

        // 06f4fe1a-06f4fea8: both tables keyed by the affiliation; an existing key keeps
        // its entry (_Tree::insert).
        GFactionNames.insert(std::make_pair(Group, Names));                  // FUN_06f53350
        GFactionProps.insert(std::make_pair(Group, Props));                  // FUN_06f53110
    }

    // 06f50108-06f50115.
    FFactionGroupCollector Collector;
    pub::Reputation::EnumerateGroups(Collector);
}

// FUN_06f503d0 (06f503d0-06f5064d).
void __cdecl LoadFactionGoods(INI_Reader* Reader)
{
    // [ESP+0x10]: the good id outlives the section; retail never initialises it.
    unsigned int Good = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("FactionGood"))
            continue;

        FFactionProps* Props = nullptr;
        unsigned int Group = 0xFFFFFFFFu;
        while (Reader->read_value())
        {
            if (Reader->is_value("faction"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string(0));
                FFactionPropsMap::iterator It = GFactionProps.find(Group);      // FUN_06f7b0f0
                if (It != GFactionProps.end())
                    Props = &It->second;
                else
                    Props = nullptr;
            }
            else if (Reader->is_value("MarketGood"))
            {
                // 06f50499-06f50623: no null check on the props (retail faults without a
                // faction line).
                pub::GetGoodID(Good, Reader->get_value_string(0));
                Props->MarketGoods.insert(Props->MarketGoods.end(), 1, Good);
            }
        }
    }
}

// FUN_06f50880 (06f50880-06f50c3a).
void __cdecl LoadFactionEmpathy(char const* RelativePath)
{
    INI_Reader Reader;
    if (!OpenContentDataIni(RelativePath, &Reader))                         // FUN_06f24280
        return;

    // 06f508d0-06f508e1: the group count reserves every section's rate list.
    FFactionGroupCounter Counter;
    pub::Reputation::EnumerateGroups(Counter);

    // Frame locals of the empathy_rate line; retail never initialises them.
    FFactionEmpathyRate Rate;
    Rate.Group = 0;
    Rate.Rate = 0.0f;

    while (Reader.read_header())
    {
        if (!Reader.is_header("RepChangeEffects"))
            continue;

        FFactionEmpathy Empathy;
        Empathy.EventRates[0] = 0.0f;
        Empathy.EventRates[1] = 0.0f;
        Empathy.EventRates[2] = 0.0f;
        Empathy.EventRates[3] = 0.0f;
        Empathy.EmpathyRates.reserve(static_cast<unsigned int>(Counter.Count));
        unsigned int Group = 0xFFFFFFFFu;

        while (Reader.read_value())
        {
            if (Reader.is_value("empathy_rate"))
            {
                pub::Reputation::GetReputationGroup(Rate.Group, Reader.get_value_string(0));
                Rate.Rate = Reader.get_value_float(1);
                Empathy.EmpathyRates.insert(Empathy.EmpathyRates.end(), 1, Rate); // FUN_06f96770
            }
            else if (Reader.is_value("group"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader.get_value_string());
            }
            else if (Reader.is_value("event"))
            {
                char const* Event = Reader.get_value_string(0);
                int Index;
                if (_stricmp(Event, "object_destruction") == 0)
                    Index = FactionEventObjectDestruction;
                else if (_stricmp(Event, "random_mission_success") == 0)
                    Index = FactionEventRandomMissionSuccess;
                else if (_stricmp(Event, "random_mission_failure") == 0)
                    Index = FactionEventRandomMissionFailure;
                else if (_stricmp(Event, "random_mission_abortion") == 0)
                    Index = FactionEventRandomMissionAbortion;
                else
                    continue;
                Empathy.EventRates[Index] = Reader.get_value_float(1);
            }
        }

        // 06f50aec-06f50af7: FUN_06f55a00 sorts the rates ascending by Rate. Retail's
        // algorithm is kept because it fixes the order of equal rates, which
        // GetFactionHostileGroups exposes.
        if (!Empathy.EmpathyRates.empty())
        {
            FFactionEmpathyRate* First = &Empathy.EmpathyRates[0];
            ContentSort(First, First + Empathy.EmpathyRates.size(), &EmpathyRateLess);
        }

        GFactionEmpathy.insert(std::make_pair(Group, Empathy));                // FUN_06f53590
    }
}

// FUN_06f506a0 (06f506a0-06f50758): _Tidy of the props map (clear() or a node-wise erase
// of [begin, end), the same result).
void __cdecl UnloadFactionProps()
{
    GFactionProps.clear();
}

// FUN_06f50ed0 (06f50ed0-06f50f88).
void __cdecl UnloadFactionEmpathy()
{
    GFactionEmpathy.clear();
}

// ---------------------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------------------

// FUN_06f50650 (06f50650-06f50693).
FFactionProps* __cdecl GetFactionProps(unsigned int const* Group)
{
    FFactionPropsMap::iterator It = GFactionProps.find(*Group);
    if (It == GFactionProps.end())
        return nullptr;
    return &It->second;
}

// FUN_06f51370 (06f51370-06f513b3).
FFactionNames* __cdecl GetFactionNames(unsigned int const* Group)
{
    FFactionNamesMap::iterator It = GFactionNames.find(*Group);
    if (It == GFactionNames.end())
        return nullptr;
    return &It->second;
}

// FUN_06f50760.
std::vector<unsigned int>* __cdecl GetFactionGroups()
{
    return &GFactionGroups;
}

// ---------------------------------------------------------------------------------------
// Reputation empathy
// ---------------------------------------------------------------------------------------

// FUN_06f50c50 (06f50c50-06f50e0a).
void __cdecl ApplyFactionReputationChange(int VibeHandle, float Delta, unsigned int Group,
                                          bool bEmpathy)
{
    // 06f50c50-06f50c66 (TEST AH,0x44 + JNP: equal -> out).
    if (Delta == 0.0f)
        return;
    if (Group == 0xFFFFFFFFu)
        return;

    // 06f50c7d-06f50d10. The out value is zeroed once; later reads reuse it.
    float Reputation = 0.0f;
    pub::Reputation::GetReputation(VibeHandle, Group, Reputation);
    float Clamped = ClampFactionReputation(static_cast<double>(Reputation) +
                                           static_cast<double>(Delta));
    pub::Reputation::SetReputation(VibeHandle, Group, Clamped);

    if (!bEmpathy)
        return;

    // 06f50d21-06f50dfd: every rate of Group's empathy list.
    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);        // FUN_06f2e220
    if (Entry == GFactionEmpathy.end())
        return;
    for (std::vector<FFactionEmpathyRate>::iterator It = Entry->second.EmpathyRates.begin();
         It != Entry->second.EmpathyRates.end(); ++It)
    {
        pub::Reputation::GetReputation(VibeHandle, It->Group, Reputation);
        // FLD Delta, FMUL rate, FADD rep.
        double const Sum = static_cast<double>(Delta) * static_cast<double>(It->Rate) +
                           static_cast<double>(Reputation);
        Clamped = ClampFactionReputation(Sum);
        pub::Reputation::SetReputation(VibeHandle, It->Group, Clamped);
    }
}

// FUN_06f50e10 (06f50e10-06f50e97).
void __cdecl GetFactionHostileGroups(unsigned int Group, unsigned int* Out, unsigned int Count)
{
    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);        // FUN_06f2e220
    if (Entry == GFactionEmpathy.end())
        return;

    // 06f50e66-06f50e8f: the list position only moves past a negative rate, so the first
    // rate at or above zero ends the run.
    std::vector<FFactionEmpathyRate>::iterator It = Entry->second.EmpathyRates.begin();
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        if (It != Entry->second.EmpathyRates.end() && It->Rate < 0.0f)
        {
            Out[Index] = It->Group;
            ++It;
        }
        else
        {
            Out[Index] = 0xFFFFFFFFu;
        }
    }
}

// FUN_06f50f90 (06f50f90-06f50fe2).
float __cdecl GetFactionEventRate(unsigned int Group, int Event)
{
    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);        // FUN_06fa7200
    if (Entry == GFactionEmpathy.end())
        return 0.0f;
    return Entry->second.EventRates[Event];
}
