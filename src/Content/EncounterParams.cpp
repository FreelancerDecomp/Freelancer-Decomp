// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU29 (06ef3630-06ef8d40): encounter parameters and formations.
// Lane CNT-POP (docs/re/content.md, TU map).

#include "EncounterParams.h"

#include "Cargo.h"      // TU44 GetShipLineFactions / GetNpcArchFactions / GetFactionFeelingMask
#include "ContentStaticIds.h"   // TU29's 16 static-init thunks 06ef3630-06ef3810
#include "ContentInternal.h"    // GetContentGameTime
#include "ContentPlayerState.h" // FContentPlayerShip, FindContentPlayerStateByShip (TU46)
#include "DockQueues.h"         // CountNpcShipsNear, PlaceFormationBlocks (TU53)
#include "Encounters.h"         // FindFormation (TU31)
#include "FactionProps.h"       // GetFactionProps (TU48)
#include "Gcs.h"                // GetGcsHandler, FGcsDockHailHandler (TU25)
#include "Guides.h"
#include "NpcNames.h"           // TU42 name generation
#include "NpcShip.h"            // FContentNpcShip, FContentNpcShipParams (TU43)
#include "NpcShips.h"           // FindNpcShipArch, GNpcShipArchs (TU42)
#include "Populators.h"         // FSpawnRequest, SpacePopClampBlockAttitudes (TU52)
#include "SpaceSupport.h"       // PlaceSpawnFormation (TU53 FUN_06f65f10)
#include "SystemInfo.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_tstring.h"
#include "x86math.h"            // FTransform

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // The unique insert of FUN_06ef5ca0 / FUN_06ef39a0: a linear find, then insert(end);
    // retail re-finds the id afterwards and discards the result (FUN_06eb5f30 +
    // FUN_06fa6e80).
    void AddUniqueId(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        for (std::vector<unsigned int>::iterator It = Ids->begin(); It != Ids->end(); ++It)
        {
            if (*It == Id)
                return;
        }
        Ids->insert(Ids->end(), Id);                                        // FUN_06eae440
    }

    // FUN_06ef7430 (cdecl, hidden return): the ids of A that are also in B, each once, in
    // A's order; the result's capacity equals its size.
    std::vector<unsigned int> IntersectIds(std::vector<unsigned int> const& A,
                                           std::vector<unsigned int> const& B)
    {
        std::vector<unsigned int> Result;
        for (std::vector<unsigned int>::const_iterator It = A.begin(); It != A.end(); ++It)
        {
            bool bInB = false;
            for (std::vector<unsigned int>::const_iterator Other = B.begin(); Other != B.end(); ++Other)
            {
                if (*Other == *It)
                {
                    bInB = true;
                    break;
                }
            }
            if (!bInB)
                continue;
            bool bKnown = false;
            for (std::vector<unsigned int>::iterator Kept = Result.begin(); Kept != Result.end(); ++Kept)
            {
                if (*Kept == *It)
                {
                    bKnown = true;
                    break;
                }
            }
            if (!bKnown)
                Result.push_back(*It);                                      // FUN_06eb5770
        }
        return std::vector<unsigned int>(Result.begin(), Result.end());
    }
}

// The ship locals of FUN_06ef5ca0 (06ef5cf7-06ef5d3a and 06ef5e76-06ef5ecf): FUN_06ead6a0 /
// FUN_06eac620 empty vectors, both ids cleared, +0x14 = 0, min = max = 1, +0x34 = 0.
FEncounterShip::FEncounterShip()
    : Arch(nullptr)
    , MinCount(1)
    , MaxCount(1)
    , DifficultyOffset(0)
{
    NpcArch.clear();
    PilotJob.clear();
}

// FUN_06ef5b50 (06ef5b50-06ef5c9d, RET 4).
FEncounterFormation::FEncounterFormation(INI_Reader* Reader)
    : bFeelingToFormation(0)
    , RerollFrequency(2)
    , FeelingMask(7)
    , Field_0x0c(-1)
    , FeelingValue(-1)
    , Faction(0xffffffffu)
    , Behavior(0)
    , ZoneCreationDistance(1500.0)                  // 0x4097700000000000
    , TimesToCreate(-1)
    , bAllowSimultaneousCreation(true)
    , Field_0x78(0)
    , Longevity(0x18)
{
    Formation.clear();
    FormationByClass.clear();
    // 06ef5c09-06ef5c7d: resize(9, 1) of the empty byte vector.
    Arrival.resize(9, 1);
    ParseSection(Reader);                           // 06ef5c84; the result is dropped
}

// FUN_06ef5ca0 (06ef5ca0-06ef631f, RET 4).
bool FEncounterFormation::ParseSection(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("ship_by_class"))
        {
            // 06ef5cf7-06ef5e60
            FEncounterShip Ship;
            Ship.MinCount = Reader->get_value_int(0);
            Ship.MaxCount = Reader->get_value_int(1);
            AddUniqueId(&Ship.ShipClasses, CreateID(Reader->get_value_string(2)));
            if (!Reader->is_value_empty(3))
                Ship.DifficultyOffset = Reader->get_value_int(3);
            Ships.insert(Ships.end(), 1, Ship);                             // FUN_06ef6d60
        }
        else if (Reader->is_value("ship_by_npc_arch"))
        {
            // 06ef5e76-06ef5f7f
            FEncounterShip Ship;
            Ship.MinCount = Reader->get_value_int(0);
            Ship.MaxCount = Reader->get_value_int(1);
            Ship.NpcArch.Id = CreateID(Reader->get_value_string(2));
            Ships.insert(Ships.end(), 1, Ship);                             // FUN_06ef6d60
        }
        else if (Reader->is_value("make_class"))
        {
            // 06ef5f95-06ef6076: every value, into the last ship line.
            if (Ships.empty())
                continue;
            FEncounterShip& Ship = Ships.back();
            unsigned int Index = 0;
            if (Reader->is_value_empty(0))
                continue;
            do
            {
                AddUniqueId(&Ship.MakeClasses, CreateID(Reader->get_value_string(Index)));
                ++Index;
            } while (!Reader->is_value_empty(Index));
        }
        else if (Reader->is_value("pilot"))
        {
            // 06ef6086: recognised and ignored.
        }
        else if (Reader->is_value("pilot_job"))
        {
            // 06ef6099-06ef60eb: the IsEmpty call on the last line's id is discarded.
            if (Ships.empty())
                continue;
            Ships.back().PilotJob.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("formation"))
        {
            // 06ef60fd-06ef6146
            if (Formation.IsEmpty() && FormationByClass.IsEmpty())
                Formation.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("formation_by_class"))
        {
            // 06ef6122-06ef6146
            if (FormationByClass.IsEmpty() && Formation.IsEmpty())
                FormationByClass.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("behavior"))
        {
            Behavior = ParseGuideBehavior(Reader->get_value_string());       // FUN_06f07a80
        }
        else if (Reader->is_value("arrival"))
        {
            ParseArrival(Reader);                                           // FUN_06ef6360
        }
        else if (Reader->is_value("longevity"))
        {
            Longevity = Reader->get_value_int(0);
        }
        else if (Reader->is_value("allow_simultaneous_creation"))
        {
            // 06ef61b2-06ef6229: exact compares; any other value leaves the flag.
            if (strcmp(Reader->get_value_string(), "yes") == 0 ||
                strcmp(Reader->get_value_string(), "true") == 0)
                bAllowSimultaneousCreation = true;
            else if (strcmp(Reader->get_value_string(), "no") == 0 ||
                     strcmp(Reader->get_value_string(), "false") == 0)
                bAllowSimultaneousCreation = false;
        }
        else if (Reader->is_value("zone_creation_distance"))
        {
            // 06ef623b-06ef624b: FILD / FSTP double.
            ZoneCreationDistance = static_cast<double>(Reader->get_value_int(0));
        }
        else if (Reader->is_value("times_to_create"))
        {
            if (strcmp(Reader->get_value_string(), "infinite") == 0)
                TimesToCreate = -1;
            else
                TimesToCreate = Reader->get_value_int(0);
        }
        else if (Reader->is_value("feeling_to_local") || Reader->is_value("feeling_to_group") ||
                 Reader->is_value("feeling_to_formation") || Reader->is_value("explicit_group"))
        {
            // 06ef62bf-06ef62c7
            if (bFeelingToFormation == 0)
                ParseFeeling(Reader);                                       // FUN_06ef3cf0
        }
    }

    // 06ef62de-06ef6309
    return !Ships.empty();
}

// FUN_06ef3cf0 (06ef3cf0-06ef3e23, RET 4). Its only caller passes the four feeling_* /
// explicit_group keys, so the group_reroll_frequency branch is unreachable there; every
// call first resets FeelingValue.
void FEncounterFormation::ParseFeeling(INI_Reader* Reader)
{
    FeelingValue = -1;                                                      // 06ef3d07
    if (Reader->is_value("group_reroll_frequency"))
    {
        // 06ef3d16-06ef3d7f: a null value counts as "every"; an unknown one changes nothing.
        char const* Value = Reader->get_value_string(0);
        if (Value == nullptr)
            RerollFrequency = 2;
        else if (strcmp(Value, "synch") == 0)
            RerollFrequency = 1;
        else if (strcmp(Value, "every") == 0)
            RerollFrequency = 2;
        else if (strcmp(Value, "once") == 0)
            RerollFrequency = 0;
        return;
    }
    if (!Reader->is_value("feeling_to_formation"))
        return;

    // 06ef3d91-06ef3e1c
    bFeelingToFormation = 1;
    char const* Feeling = Reader->get_value_string(0);
    if (strcmp(Feeling, "friendly") == 0)
        FeelingMask = 1;
    else if (strcmp(Feeling, "hostile") == 0)
        FeelingMask = 4;
    else if (strcmp(Feeling, "same") == 0)
        FeelingMask = 8;
    else if (strcmp(Feeling, "any") == 0)
        FeelingMask = 7;
    RerollFrequency = 2;
    FeelingValue = Reader->get_value_int(1);
}

// FUN_06ef6360 (06ef6360-06ef6592, RET 4).
void FEncounterFormation::ParseArrival(INI_Reader* Reader)
{
    // 06ef6367-06ef63c7: erase everything, then resize(9, 0).
    Arrival.clear();
    Arrival.resize(9, 0);

    unsigned int Index = 0;
    if (Reader->is_value_empty(0))
        return;
    char Name[0x34];
    do
    {
        // 06ef63e0-06ef6434: a leading '-' clears the flags instead of setting them; the
        // rest is copied into a 50-byte local, at most 49 chars.
        char const* Value = Reader->get_value_string(Index);
        ++Index;
        unsigned char bSet = 1;
        if (Value[0] == '-')
        {
            bSet = 0;
            ++Value;
        }
        size_t Length = strlen(Value);
        if (Length >= 0x32)
            Length = 0x31;
        memcpy(Name, Value, Length);
        Name[Length] = '\0';

        // 06ef6439-06ef6575: DACOM stricmp.
        if (_stricmp(Name, "all") == 0)
        {
            Arrival[1] = bSet;
            Arrival[2] = bSet;
            Arrival[5] = bSet;
            Arrival[3] = bSet;
            Arrival[4] = bSet;
            Arrival[6] = bSet;
            Arrival[7] = bSet;
            Arrival[8] = bSet;
        }
        else if (_stricmp(Name, "tradelane") == 0)
        {
            Arrival[1] = bSet;
        }
        else if (_stricmp(Name, "object_all") == 0)
        {
            Arrival[5] = bSet;
            Arrival[3] = bSet;
            Arrival[4] = bSet;
            Arrival[6] = bSet;
        }
        else if (_stricmp(Name, "object_docking_ring") == 0)
        {
            Arrival[3] = bSet;
        }
        else if (_stricmp(Name, "object_capital") == 0)
        {
            Arrival[6] = bSet;
        }
        else if (_stricmp(Name, "object_jump_gate") == 0)
        {
            Arrival[4] = bSet;
        }
        else if (_stricmp(Name, "object_station") == 0)
        {
            Arrival[5] = bSet;
        }
        else if (_stricmp(Name, "cruise") == 0)
        {
            Arrival[7] = bSet;
        }
        else if (_stricmp(Name, "buzz") == 0)
        {
            Arrival[8] = bSet;
        }
    } while (!Reader->is_value_empty(Index));
}

// FUN_06ef3950 (06ef3950-06ef3990).
FEncounterParameters::FEncounterParameters()
    : SystemId(0)
    , ZoneId(0)
    , RerollInterval(0.0f)
    , LastRerollTime(0.0f)
    , Difficulty(0)
    , CurrentPermutation(nullptr)
{
    Nickname.clear();
}

// FUN_06ef3830 (06ef3830-06ef38d0, RET 0xc).
FEncounterParameters::FEncounterParameters(INI_Reader* Reader, unsigned int const* Id,
                                           unsigned int const* InSystemId)
    : SystemId(*InSystemId)
    , ZoneId(0)
    , RerollInterval(0.0f)
    , LastRerollTime(-1.0f)                         // 0xbf800000
    , Difficulty(-1)
    , CurrentPermutation(nullptr)                   // 06ef38b7: +0x28 = +0x20 (null)
{
    Nickname.Id = *Id;
    Load(Reader);                                   // 06ef38ba; the result is dropped
}

// FUN_06ef3ab0 (06ef3ab0-06ef3cdf, RET 4).
bool FEncounterParameters::Load(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("EncounterFormation"))
        {
            // 06ef3afd-06ef3b73: the local formation, inserted at the end (FUN_06ef69a0).
            FEncounterFormation Formation(Reader);
            Formations.insert(Formations.end(), Formation);
        }
        else if (Reader->is_header("Creation"))
        {
            ParseCreation(Reader);                                          // FUN_06ef3ef0
        }
    }

    // 06ef3b9c-06ef3bca: retail reads the first formation's ship vector through the begin
    // pointer without testing it, so a file with no [EncounterFormation] faults there;
    // the port's operator[] on the empty vector fails the same way.
    bool const bHasShips = !Formations[0].Ships.empty();

    // 06ef3bcf-06ef3cc0: no permutation -> one permutation {0, 1, ..., n-1}, weight 1.0.
    // EH map 06fc0dd4: state 4 is the local entry list, state 5 the permutation copied from
    // it; both are RAII locals in that order.
    if (Permutations.empty())
    {
        std::vector<FEncounterPermutationEntry> Entries;                    // 06ef3bf3, state 4
        for (unsigned int Index = 0; Index < Formations.size(); ++Index)
        {
            FEncounterPermutationEntry Entry;
            Entry.FormationIndex = static_cast<int>(Index);
            Entry.bStar = false;
            Entries.insert(Entries.end(), 1, Entry);                        // FUN_06f96770
        }
        FEncounterPermutation Permutation;                                  // 06ef3c47, state 5
        Permutation.Entries = Entries;                                      // FUN_06ed2700
        Permutation.Weight = 1.0f;                                          // 0x3f800000
        Permutations.insert(Permutations.end(), 1, Permutation);            // FUN_06ef70c0
    }
    return bHasShips;
}

// FUN_06ef3ef0 (06ef3ef0-06ef417a, RET 4).
void FEncounterParameters::ParseCreation(INI_Reader* Reader)
{
    std::vector<FEncounterPermutationEntry> Entries;
    while (Reader->read_value())
    {
        // 06ef3f40-06ef3f6b: the local list is emptied at the top of every line.
        Entries.clear();
        if (!Reader->is_value("permutation"))
            continue;

        // 06ef3f7d-06ef3fd5: skip leading spaces; the first number always goes in.
        char const* Text = Reader->get_value_string(0);
        int const Length = static_cast<int>(strlen(Text));
        int Position = 0;
        while (Position < Length && Text[Position] == ' ')
            ++Position;
        FEncounterPermutationEntry Entry;
        Entry.FormationIndex = static_cast<int>(ParseNumber(Text + Position));   // ftol
        Entry.bStar = false;
        Entries.insert(Entries.end(), 1, Entry);                            // FUN_06f96770

        // 06ef3fda-06ef40a4: skip the digits, then ' ' and '+'; '*' marks the last entry,
        // any other char starts the next number (its digits skipped after it).
        while (Position < Length && Text[Position] >= '0' && Text[Position] <= '9')
            ++Position;
        while (Position < Length)
        {
            while (Position < Length && (Text[Position] == ' ' || Text[Position] == '+'))
                ++Position;
            if (Position == Length)
                break;
            if (Text[Position] == '*')
            {
                Entries.back().bStar = true;
                ++Position;
                continue;
            }
            Entry.FormationIndex = static_cast<int>(ParseNumber(Text + Position));
            Entry.bStar = false;
            Entries.insert(Entries.end(), 1, Entry);
            while (Position < Length && Text[Position] >= '0' && Text[Position] <= '9')
                ++Position;
        }

        // 06ef4034-06ef4126: weight = (float) value 1; the copy goes at the end of +0x18.
        FEncounterPermutation Permutation;
        Permutation.Weight = static_cast<float>(Reader->get_value_int(1));
        Permutation.Entries = Entries;                                      // FUN_06ed2700
        Permutations.insert(Permutations.end(), 1, Permutation);            // FUN_06ef70c0

        // 06ef4116-06ef4144
        if (!Reader->is_value_empty(2))
            RerollInterval = static_cast<float>(Reader->get_value_int(2));
    }
}

// FUN_06ef39a0 (06ef39a0-06ef3aa1, RET 4).
void FEncounterParameters::ApplyDifficulty(int Level)
{
    if (Difficulty != -1)
        return;
    Difficulty = Level;
    for (std::vector<FEncounterFormation>::iterator Formation = Formations.begin();
         Formation != Formations.end(); ++Formation)
    {
        for (std::vector<FEncounterShip>::iterator Ship = Formation->Ships.begin();
             Ship != Formation->Ships.end(); ++Ship)
        {
            if (Ship->ShipClasses.empty())
                continue;
            // 06ef39ef-06ef3a2b: TString<10> sprintf (FUN_06ef66e0: vsprintf into a local,
            // then at most 9 chars kept).
            int Tag = Ship->DifficultyOffset + Difficulty;
            if (Tag < 1)
                Tag = 1;
            char Buffer[0x20];
            sprintf_s(Buffer, sizeof(Buffer), "diff%d", Tag);
            TString<0xa> Name;
            Name = Buffer;
            AddUniqueId(&Ship->ShipClasses, CreateID(Name.Data));
        }
    }
}

// FUN_06ef4180 (06ef4180-06ef44ac, RET 4).
bool FEncounterParameters::BuildFormationFactions(FEncounterFormation* Formation)
{
    // 06ef41a1-06ef41b4: erase(begin, end).
    Formation->Factions.Items.clear();

    std::vector<unsigned int> ClassFactions;        // local_4c
    std::vector<unsigned int> ArchFactions;         // local_2c
    std::vector<unsigned int> Allowed;              // local_6c
    std::vector<unsigned int> NpcArchs;             // local_5c
    bool bHaveClassFactions = false;                // BL
    bool bHaveNpcArch = false;                      // the reused argument byte

    // 06ef4230-06ef42e3: ship lines with an npc_arch are collected; the others narrow
    // ClassFactions to the factions that fit every such line.
    for (std::vector<FEncounterShip>::iterator Ship = Formation->Ships.begin();
         Ship != Formation->Ships.end(); ++Ship)
    {
        if (Ship->NpcArch.Id != 0)
        {
            NpcArchs.insert(NpcArchs.end(), Ship->NpcArch.Id);              // FUN_06eae440
            bHaveNpcArch = true;
            continue;
        }
        std::vector<unsigned int> ShipFactions;
        GetShipLineFactions(&*Ship, &ShipFactions);                         // FUN_06f35540
        if (!bHaveClassFactions)
        {
            ClassFactions = ShipFactions;                                   // FUN_06eb6560
            bHaveClassFactions = true;
        }
        else
        {
            ClassFactions = IntersectIds(ClassFactions, ShipFactions);      // FUN_06ef7430
        }
    }

    // 06ef42e9-06ef434f
    if (bHaveNpcArch)
    {
        GetNpcArchFactions(&NpcArchs, &ArchFactions);                       // FUN_06f35310
        if (bHaveClassFactions)
            Allowed = IntersectIds(ClassFactions, ArchFactions);
        else
            Allowed = ArchFactions;
    }
    else
    {
        Allowed = ClassFactions;
    }

    // 06ef4354-06ef444d
    if (Allowed.empty())
        return false;
    // 06ef4365-06ef441d: reserve(min(Allowed, Factions)) -- Factions' size when it is not
    // larger than Allowed's, 0 when it is empty.
    size_t Reserve = Factions.Items.size();
    if (!Factions.Items.empty() && Allowed.size() < Factions.Items.size())
        Reserve = Allowed.size();
    Formation->Factions.Items.reserve(Reserve);
    // 06ef4420-06ef4439: FUN_06ef75a0 over a by-value copy of Factions (weighted_vector copy
    // ctor): every pair whose faction is allowed, in order.
    weighted_vector<unsigned int> const Copy(Factions);
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Copy.Items.begin();
         It != Copy.Items.end(); ++It)
    {
        for (std::vector<unsigned int>::iterator Faction = Allowed.begin(); Faction != Allowed.end(); ++Faction)
        {
            if (*Faction == It->first)
            {
                Formation->Factions.Items.insert(Formation->Factions.Items.end(), 1, *It);  // FUN_06f96770
                break;
            }
        }
    }
    return !Formation->Factions.Items.empty();
}

// FUN_06ef44b0 (06ef44b0-06ef4774, RET 4). Both picks are weighted_vector::Pick inlined
// (sum; 0 -> none; rand() * 2^-15 * sum; first running total >= it).
bool FEncounterParameters::PickFaction(FEncounterFormation* Formation)
{
    if (Formation->Factions.Items.empty())
        return false;

    if (Formation->bFeelingToFormation != 1)
    {
        // 06ef46db-06ef4774
        std::pair<unsigned int, float>* Picked = Formation->Factions.Pick();
        if (Picked == Formation->Factions.Items.data() + Formation->Factions.Items.size())
            return false;
        Formation->Faction = Picked->first;
        return true;
    }

    // 06ef44f8-06ef4551: the faction picked for formation FeelingValue (read unchecked).
    // EH map 06fc0eac: the output list is state 0 (06ef44fd), the by-value copy of the
    // choices state 1 (06ef4526); the copy dies right after the filter (06ef45ab).
    weighted_vector<unsigned int> Matching;                                 // 06ef44fd
    unsigned int const OtherFaction = Formations.data()[Formation->FeelingValue].Faction;
    unsigned int const Mask = Formation->FeelingMask;
    {
        weighted_vector<unsigned int> const Choices(Formation->Factions);  // 06ef4526
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Choices.Items.begin();
             It != Choices.Items.end(); ++It)
        {
            bool bMatch;
            if (Mask == 8)
                bMatch = It->first == OtherFaction;                         // "same"
            else
                bMatch = (Mask & GetFactionFeelingMask(It->first, OtherFaction)) != 0;   // FUN_06f350f0
            if (bMatch)
                Matching.Items.insert(Matching.Items.end(), *It);           // FUN_06ef6bb0
        }
    }                                                                       // 06ef45ab
    if (Matching.Items.empty())
        return false;

    // 06ef45ef-06ef469b
    std::pair<unsigned int, float>* Picked = Matching.Pick();
    if (Picked == Matching.Items.data() + Matching.Items.size())
        return false;
    Formation->Faction = Picked->first;
    return true;
}

// FUN_06ef52d0 (06ef52d0-06ef54d4, RET 4).
bool FEncounterParameters::Refresh(float Time)
{
    // 06ef52d8-06ef5303: the -1.0f tests compare the raw bits (CMP EAX,0xbf800000). A reroll
    // is due unless Time - LastRerollTime < RerollInterval (TEST AH,1 / JZ: C0 clear).
    unsigned int LastBits;
    memcpy(&LastBits, &LastRerollTime, sizeof(LastBits));
    bool const bFirst = LastBits == 0xbf800000u;
    bool bReroll;
    if (bFirst)
        bReroll = true;
    else
        bReroll = (Time - LastRerollTime) >= RerollInterval;

    FSystemInfo* System = FindSystemInfo(&SystemId);                         // FUN_06f75f30

    // 06ef531a-06ef5339: the first call builds every formation's faction choices.
    if (bFirst)
    {
        for (std::vector<FEncounterFormation>::iterator Formation = Formations.begin();
             Formation != Formations.end(); ++Formation)
            BuildFormationFactions(&*Formation);                            // result dropped
    }

    // 06ef533b-06ef53e4
    for (std::vector<FEncounterFormation>::iterator Formation = Formations.begin();
         Formation != Formations.end(); ++Formation)
    {
        // 06ef5347-06ef5396: forget the created objects the system no longer knows.
        std::vector<unsigned int>::iterator Object = Formation->CreatedObjects.begin();
        while (Object != Formation->CreatedObjects.end())
        {
            if (System->FindShip(*Object) == nullptr)                       // FUN_06f73930
                Object = Formation->CreatedObjects.erase(Object);
            else
                ++Object;
        }
        if (bReroll)
            Formation->Field_0x78 = 0;

        bool bPick;
        switch (Formation->RerollFrequency)
        {
        case 0:
            bPick = bFirst;
            break;
        case 1:
            bPick = bReroll;
            break;
        case 2:
            bPick = true;
            break;
        default:
            bPick = false;
            break;
        }
        // 06ef53c4: formations tied to another formation's faction wait for the second pass.
        if (bPick && Formation->bFeelingToFormation != 1 && !PickFaction(&*Formation))
            return false;
    }

    // 06ef53ee-06ef5433
    for (std::vector<FEncounterFormation>::iterator Formation = Formations.begin();
         Formation != Formations.end(); ++Formation)
    {
        bool bPick;
        switch (Formation->RerollFrequency)
        {
        case 0:
            bPick = bFirst;
            break;
        case 1:
            bPick = bReroll;
            break;
        case 2:
            bPick = true;
            break;
        default:
            bPick = false;
            break;
        }
        if (bPick && Formation->bFeelingToFormation == 1 && !PickFaction(&*Formation))
            return false;
    }

    // 06ef5435-06ef54c1: the weighted pick of a permutation (weight +0x10); none -> end.
    if (bReroll)
    {
        FEncounterPermutation* const End = Permutations.data() + Permutations.size();
        FEncounterPermutation* Picked = End;
        float Sum = 0.0f;
        for (std::vector<FEncounterPermutation>::iterator It = Permutations.begin();
             It != Permutations.end(); ++It)
            Sum += It->Weight;
        if (!Permutations.empty() && Sum != 0.0f)
        {
            // FILD rand(), FMUL 2^-15 (DAT_06fb4448), FMUL Sum, FSTP float.
            float const Target = static_cast<float>(rand()) * 3.0517578125e-05f * Sum;
            float Running = 0.0f;
            for (std::vector<FEncounterPermutation>::iterator It = Permutations.begin();
                 It != Permutations.end(); ++It)
            {
                Running += It->Weight;
                // TEST AH,0x41 / JNP: Target < Running or equal.
                if (Target <= Running)
                {
                    Picked = &*It;
                    break;
                }
            }
        }
        CurrentPermutation = Picked;
    }

    LastRerollTime = Time;                                                  // 06ef54ca
    return true;
}

namespace
{
    // The linear finds of FUN_06ef5a20 / FUN_06ef4eb0 (FUN_06eb5f30): Id in Ids.
    bool ContainsId(std::vector<unsigned int> const* Ids, unsigned int Id)
    {
        for (std::vector<unsigned int>::const_iterator It = Ids->begin(); It != Ids->end(); ++It)
        {
            if (*It == Id)
                return true;
        }
        return false;
    }

    // FUN_06ef4eb0 06ef4faa-06ef4fba: REP MOVSD 0xe of the plan into the guide description's
    // +0x10..+0x47 block.
    void CopyPlanToGuideDesc(FSpawnPlan const* Plan, FGuideDesc* Desc)
    {
        Desc->Plan = *Plan;
    }
}

// FUN_06ef54e0 (06ef54e0-06ef5a15, RET 0xc).
int FEncounterParameters::Spawn(FSpawnRequest const* Request, bool const* bStarred,
                                std::list<FDensityRestriction> const* Restrictions)
{
    int Result = 0;                                                         // 06ef5514
    // CurrentPermutation is dereferenced as is (Refresh leaves it at the end of the list
    // only when every weight is 0).
    std::vector<FEncounterPermutationEntry> const& Entries = CurrentPermutation->Entries;
    for (std::vector<FEncounterPermutationEntry>::const_iterator Entry = Entries.begin();
         Entry != Entries.end(); ++Entry)
    {
        FEncounterFormation* Formation = &Formations[Entry->FormationIndex];
        FContentPlayerShip* PlayerShip = Request->PlayerShip;

        // 06ef554a-06ef5574: the player's standing with the formation's faction.
        int const PlayerRep = PlayerShip->ReputationHandle->GetReputation();  // FUN_06f33f80
        float Standing;
        pub::Reputation::GetReputation(PlayerRep, Formation->Faction, Standing);
        // 06ef5577-06ef5599: in single player a hostile faction (<= -0.6f, DAT_06fb7830)
        // needs the hostile clamp flag.
        if (pub::SinglePlayer() && Standing <= -0.6f && SpacePopGetHostileClamp() == 0)
            continue;
        // 06ef559f-06ef55f4: the player's flags: bit 0 refuses hostile factions, bit 1
        // refuses the others (both tests are skipped for a NaN standing).
        unsigned int const ShipId = PlayerShip->SpaceObjId;
        unsigned int const Flags = FindContentPlayerStateByShip(&ShipId)->GetFlags();   // FUN_06f49bc0
        if ((Flags & 1) != 0 && Standing <= -0.6f)
            continue;
        if ((Flags & 2) != 0 && Standing > -0.6f)
            continue;

        // 06ef55fa-06ef56c8: a ship in combat (slot 2, +0x11c) takes new ships only below the
        // zone's max_battle_size; otherwise relief_time must have passed since combat ended.
        if (PlayerShip->Slot08())
        {
            FSystemZone* Zone = FindSystemInfo(&SystemId)->FindZone(ZoneId);
            if (static_cast<unsigned int>(PlayerShip->GetHostileContactCount()) >=
                static_cast<unsigned int>(Zone->Population.MaxBattleSize))  // JNC
                continue;
        }
        else
        {
            double Elapsed = -1.0;                                          // DAT_06fb78f0
            if (PlayerShip->GetCalmSince() != -1.0)
                Elapsed = GetContentGameTime() - PlayerShip->GetCalmSince();   // FUN_06efec10
            float const ElapsedStored = static_cast<float>(Elapsed);
            if (Elapsed > 0.0)
            {
                FSystemZone* Zone = FindSystemInfo(&SystemId)->FindZone(ZoneId);
                // TEST AH,5 + JNP: only a stored time below relief_time refuses.
                if (ElapsedStored < Zone->Population.ReliefTime)
                    continue;
            }
            else if (PlayerShip->GetCalmSince() != -1.0)
            {
                continue;
            }
        }

        // 06ef56ce-06ef5721: the formation itself must be available.
        bool bSpawned = false;
        if (Entry->bStar == *bStarred &&
            (Formation->bAllowSimultaneousCreation || Formation->CreatedObjects.empty()) &&
            (Formation->TimesToCreate == -1 || Formation->Field_0x78 < Formation->TimesToCreate) &&
            Formation->Faction != 0xffffffffu && !Formation->Ships.empty())
        {
            // 06ef5727-06ef576a: every ship line needs an arch -- its npc arch, else a pick
            // for the faction (FUN_06f35890 with level -1).
            bool bArchs = true;
            for (std::vector<FEncounterShip>::iterator Ship = Formation->Ships.begin();
                 Ship != Formation->Ships.end(); ++Ship)
            {
                if (Ship->NpcArch.Id == 0)
                    Ship->Arch = PickFactionShipLineArch(Formation->Faction, &*Ship, -1);
                else
                    Ship->Arch = FindNpcShipArch(&Ship->NpcArch.Id, &GNpcShipArchs);   // FUN_06f32480
                if (Ship->Arch == nullptr)
                {
                    bArchs = false;
                    break;
                }
            }
            if (bArchs)
            {
                ++Formation->Field_0x78;                                    // 06ef576c
                // 06ef576f-06ef57a2: unstarred spawns must pass the density restrictions.
                if (*bStarred ||
                    CheckDensityRestrictions(&Request->Placement, Restrictions, Formation))
                    bSpawned = true;
            }
        }

        if (bSpawned)
        {
            // 06ef57a8-06ef5839: the blocks, the ships created from them and the plan.
            std::list<FContentNpcShipParams> Blocks;
            std::list<FContentNpcShip*> Ships;
            FSpawnPlan Plan;
            SpawnFormation(Request, &Plan, Formation, &Blocks);             // FUN_06ef4eb0
            if (!Blocks.empty())
            {
                FSystemInfo* System = FindSystemInfo(&SystemId);
                System->FindZone(ZoneId);                                   // 06ef585c, result unused
                if (Plan.Kind == 2)
                {
                    System->CreateNpcShips(&Blocks, Plan.DockTarget, &Ships);   // FUN_06f73810
                }
                else
                {
                    System->CreateNpcShips(&Blocks, 0, &Ships);
                    if (Request->bArrival)
                    {
                        // 06ef589c-06ef58e5: the dock hail handler keeps quiet about each
                        // new ship for 30 s.
                        int const HandlerType = 0;
                        void* const AnyPhantom = nullptr;                   // DAT_06fb7834
                        FGcsDockHailHandler* Handler =
                            static_cast<FGcsDockHailHandler*>(GetGcsHandler(&HandlerType));   // FUN_06ee93f0
                        for (std::list<FContentNpcShip*>::iterator It = Ships.begin(); It != Ships.end(); ++It)
                        {
                            unsigned int const NewShipId = (*It)->SpaceObjId;
                            Handler->SuppressHail(&NewShipId, &AnyPhantom, 0x1e);   // FUN_06ee9b70
                        }
                    }
                }
                // 06ef58eb-06ef5905: each ship remembers the encounter (+0x5ac).
                for (std::list<FContentNpcShip*>::iterator It = Ships.begin(); It != Ships.end(); ++It)
                    (*It)->EncounterParamsId.Id = Nickname.Id;
                // 06ef5907-06ef5934: the formation's created objects.
                for (std::list<FContentNpcShip*>::iterator It = Ships.begin(); It != Ships.end(); ++It)
                {
                    unsigned int const NewShipId = (*It)->SpaceObjId;
                    Formation->CreatedObjects.insert(Formation->CreatedObjects.end(), 1, NewShipId);   // FUN_06f937c0
                }
            }
            // 06ef593c-06ef5944: the result is the number of blocks (not of created ships).
            Result = static_cast<int>(Blocks.size());
        }

        // 06ef59e1-06ef59f2: an unstarred pass stops at the first spawn.
        if (Result != 0 && !*bStarred)
            return Result;
    }
    return Result;
}

// FUN_06ef5a20 (06ef5a20-06ef5b4a, RET 0xc).
bool FEncounterParameters::CheckDensityRestrictions(FSpaceObjPlacement const* Placement,
                                                    std::list<FDensityRestriction> const* Restrictions,
                                                    FEncounterFormation const* Formation)
{
    for (std::list<FDensityRestriction>::const_iterator Restriction = Restrictions->begin();
         Restriction != Restrictions->end(); ++Restriction)
    {
        unsigned int const Type = Restriction->Type.Id;
        int Count = 0;
        for (std::vector<FEncounterShip>::const_iterator Ship = Formation->Ships.begin();
             Ship != Formation->Ships.end(); ++Ship)
        {
            if (ContainsId(&Ship->ShipClasses, Type) || ContainsId(&Ship->MakeClasses, Type))
            {
                ++Count;
                continue;
            }
            if (Ship->NpcArch.Id == 0)
                continue;
            // 06ef5aae-06ef5adf: the arch record is not null-tested.
            FNpcShipArch const* Arch = FindNpcShipArch(&Ship->NpcArch.Id, &GNpcShipArchs);
            if (ContainsId(&Arch->NpcClasses, Type))
                ++Count;
        }
        // 06ef5af0-06ef5b24: counted for every restriction; JL / JG signed.
        int const Near = CountNpcShipsNear(FindSystemInfo(&SystemId), Placement,
                                           &GEncounterDensityRadiusSP, &GEncounterRestrictionCone,
                                           &Type);                           // FUN_06f67440
        if (Count >= 1 && Near + Count > Restriction->MaxCount)
            return false;
    }
    return true;
}

// FUN_06ef4eb0 (06ef4eb0-06ef52c1, RET 0x10).
void FEncounterParameters::SpawnFormation(FSpawnRequest const* Request, FSpawnPlan* Plan,
                                          FEncounterFormation* Formation,
                                          std::list<FContentNpcShipParams>* Blocks)
{
    FSystemInfo* System = FindSystemInfo(&SystemId);
    FSystemZone* Zone = System->FindZone(ZoneId);
    BuildFormationBlocks(Formation, Blocks, Zone, Request->ControllerId);   // FUN_06ef4780
    // 06ef4f05-06ef4f2d: arrivals in single player: the first block's +0x108 = 76.0f.
    if (pub::SinglePlayer() && Request->bArrival && !Blocks->empty())
        Blocks->front().Field_0x108 = 76.0f;

    // 06ef4f37-06ef4f73: TU53 places the blocks and fills the plan.
    bool const bPlaced = PlaceSpawnFormation(Request, System, Zone,
                                             &Formation->ZoneCreationDistance, Formation->Arrival,
                                             Plan, &Formation->Faction, Blocks);    // FUN_06f65f10
    if (!bPlaced)
    {
        // 06ef51f2-06ef52a9: the blocks are dropped.
        Blocks->clear();
        return;
    }

    NameFormationBlocks(Blocks, Formation);                                 // FUN_06ef4cf0
    SetLeaderGuideTargets(System, Plan, &Blocks->front());                  // 06ef4f8f FUN_06f63cc0
    FContentNpcShipParams& Leader = Blocks->front();
    // The encounter path always gives the leader an FGuideDesc (BuildFormationBlocks).
    FGuideDesc* Desc = static_cast<FGuideDesc*>(Leader.GuideDesc.Pointer);
    if (Desc != nullptr)
    {
        CopyPlanToGuideDesc(Plan, Desc);
        if (Desc->Type == 2)
        {
            // 06ef4fd1-06ef5109: a trade guide -- the leader's cargo pick, one unit fitted to
            // each freighter / transport of the formation BEHIND the leader. GenerateCargo has
            // already appended the leader's own fitted load to Leader.Cargo (+0x80, at
            // 06f36c1d-06f36c37), so the loop starts at the SECOND block: 06ef5028-06ef502d
            // is `MOV EAX,[EBX+4]` (head), `MOV ECX,[EAX]` (first node), `MOV EBP,[ECX]`
            // (second node), and 06ef50cb-06ef50d1 walks _Next until the head. Starting at
            // begin() gave the leader a second CargoDesc, which SpaceObj_BuildTransportLoadout
            // (Server 06d5f990) turned into a second pod + stack on every cargo hardpoint;
            // CECargo::Init then linked the second stacks to the FIRST pods, leaving the first
            // stacks pointing at pods that were later freed (FLServer crash in
            // CECargo::GetEquipDesc from EqObj::collect_droppable_equip).
            std::vector<FCargoPick> Cargo;
            GenerateCargo(System, &Leader, &Cargo);                         // FUN_06f36050
            if (!Cargo.empty())
            {
                std::list<FContentNpcShipParams>::iterator Block = Blocks->begin();
                for (++Block; Block != Blocks->end(); ++Block)
                {
                    if (!ContainsId(&Block->ClassIds, CreateID("class_freighter")) &&
                        !ContainsId(&Block->ClassIds, CreateID("class_transport")))
                        continue;
                    Cargo.front().Count = 1;
                    FitCargoToHold(Block->ShipArchId, &Cargo);             // FUN_06f35bf0
                    for (std::vector<FCargoPick>::const_iterator Pick = Cargo.begin(); Pick != Cargo.end(); ++Pick)
                    {
                        pub::SpaceObj::CargoDesc const Entry = {Pick->GoodId, Pick->Count};
                        Block->Cargo.insert(Block->Cargo.end(), 1, Entry); // FUN_06f96770
                    }
                }
            }
        }
    }

    // 06ef510d-06ef513c: the leader's placement (+0x90 orientation, +0xb4 position).
    FTransform Placement;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Placement.Rotation.D[Row][Column] = Leader.Orientation.E[Row][Column];
    }
    Placement.Translation.X = Leader.Position.x;
    Placement.Translation.Y = Leader.Position.y;
    Placement.Translation.Z = Leader.Position.z;

    // 06ef5140-06ef518a: formation_by_class through the faction's formations, else formation.
    ID_String FormationId;
    FormationId.clear();
    if (!Formation->FormationByClass.IsEmpty())
        GetFactionProps(&Formation->Faction)->GetFormation(&Formation->FormationByClass.Id,
                                                           &FormationId.Id);   // FUN_06f50650 / FUN_06f4f3d0
    else if (!Formation->Formation.IsEmpty())
        FormationId.Id = Formation->Formation.Id;
    if (!FormationId.IsEmpty() && Blocks->size() > 1)
        PlaceFormationBlocks(Blocks, FindFormation(&FormationId.Id), &Placement);   // FUN_06f66c80

    // 06ef51bd-06ef51eb.
    for (std::list<FContentNpcShipParams>::iterator Block = Blocks->begin(); Block != Blocks->end(); ++Block)
        SpacePopClampBlockAttitudes(Request->PlayerShip, &*Block);          // FUN_06f5a5d0
}

// FUN_06ef4780 (06ef4780-06ef4aa3, RET 0x10).
void BuildFormationBlocks(FEncounterFormation const* Formation,
                          std::list<FContentNpcShipParams>* Blocks, FSystemZone* Zone,
                          unsigned int ControllerId)
{
    int LineIndex = 0;
    for (std::vector<FEncounterShip>::const_iterator Line = Formation->Ships.begin();
         Line != Formation->Ships.end(); ++Line, ++LineIndex)
    {
        // 06ef47c7-06ef4840: a fresh block with a new guide description (held once).
        FContentNpcShipParams Params;                                       // FUN_06f33500
        Params.ControllerId = ControllerId;
        FGuideDesc* Desc = new FGuideDesc();                                // FUN_06f061b0
        Params.GuideDesc = Desc;
        FNpcShipArch* Arch = Line->Arch;
        Params.Level = Arch->Level;                                         // 06ef4845-06ef484b
        if (LineIndex == 0)
        {
            // 06ef4854-06ef486f: the leader's guide wanders in the zone with the behavior.
            Desc->WanderZone = &Zone->ZoneId;
            Desc->Type = Formation->Behavior;
        }
        else if (Params.GuideDesc != nullptr)
        {
            Params.GuideDesc->Release();
            Params.GuideDesc = nullptr;
        }
        // 06ef488f-06ef48a3: FILD longevity, FST, FADD ST0,ST0.
        Params.Field_0x104 = static_cast<float>(Formation->Longevity);
        Params.Field_0x108 = static_cast<float>(static_cast<double>(Formation->Longevity) * 2.0);

        // 06ef48aa-06ef48cc: min + rand() * (max - min + 1) / 32768 ships.
        int const Count = rand() * (Line->MaxCount - Line->MinCount + 1) / 0x8000 + Line->MinCount;
        for (int ShipIndex = 0; ShipIndex < Count; ++ShipIndex)
        {
            ApplyNpcShipArch(&Params, Arch, nullptr,
                             Line->PilotJob.IsEmpty() ? nullptr : &Line->PilotJob);   // FUN_06f348f0
            if (ShipIndex > 0 && Params.GuideDesc != nullptr)
            {
                Params.GuideDesc->Release();
                Params.GuideDesc = nullptr;
            }
            // 06ef4935-06ef49a9: make_class ids join the block's classes uniquely.
            for (std::vector<unsigned int>::const_iterator Class = Line->MakeClasses.begin();
                 Class != Line->MakeClasses.end(); ++Class)
            {
                if (!ContainsId(&Params.ClassIds, *Class))
                    Params.ClassIds.insert(Params.ClassIds.end(), *Class);  // FUN_06eae440
            }
            Blocks->push_back(Params);                                      // FUN_06ef6b80 + FUN_06ef7bd0
        }
    }
}

// FUN_06ef4cf0 (06ef4cf0-06ef4e50).
void __cdecl NameFormationBlocks(std::list<FContentNpcShipParams>* Blocks,
                                 FEncounterFormation const* Formation)
{
    // 06ef4d0d-06ef4d2b: the leader's guide type and the block count; the guide is not
    // null-tested.
    FNpcNameRequest Request(Blocks->front().GuideDesc->Type, static_cast<int>(Blocks->size()),
                            0, 0, 0);                                        // FUN_06f31670
    for (std::list<FContentNpcShipParams>::iterator Block = Blocks->begin(); Block != Blocks->end(); ++Block)
    {
        Request.RankLevel = Block->Level;                                   // +0x500
        Request.ShipArchId = Block->ShipArchId;                             // +0x74
        FNpcNameParams Name;
        PickNpcNameGender(Formation->Faction, &Name);                       // FUN_06f319b0
        if (BuildNpcName(Formation->Faction, &Request, &Name, true))        // FUN_06f319d0
        {
            ApplyNpcShipFaction(Formation->Faction, Name.Gender, &*Block);  // FUN_06f34a80
            if (Block->ReputationHandle != nullptr)
            {
                Block->ReputationHandle->Release();
                Block->ReputationHandle = nullptr;
            }
            CreateNpcNameReputation(&Name, &Block->ReputationHandle);       // FUN_06f31910
        }
        // 06ef4ddb-06ef4e0a: the leader's name fixes the designation of the others.
        if (Block == Blocks->begin())
        {
            GetFirstNameValue(&Name.First, &Name.Second, &Request.Desig);   // FUN_06f32350
            GetFirstNameLastValue(&Name.First, &Name.Second, &Request.DesigNumber);   // FUN_06f323c0
        }
        ++Request.EscortNumber;                                             // 06ef4e0d
    }
}
