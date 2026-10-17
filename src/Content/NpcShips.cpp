// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU42 (06f31190-06f33300): NPC ship archetypes, NPC character
// descriptions, mission [NPC] blocks, space_costume.
//
// Listing: DLLS\BIN\Content.dll.txt. The std::map template instantiations
// retail emits for the two tables (insert FUN_06f32d50, _Insert FUN_06f32f60,
// _Buynode FUN_06f331b0, iterator step FUN_06f331d0, pair copy FUN_06f33230,
// map ctor FUN_06f32ce0, _Tidy LAB_06f32c20) are the std::map objects below.

#include "NpcShips.h"

#include "ContentStaticIds.h"
#include "common_ini.h"
#include "common_newchar.h"
#include "common_server.h"
#include "common_text.h"
#include "msvcrt_compat.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

FNpcShipArchMap GNpcShipArchs;
FNpcCharacterDescMap GSpecificNpcs;

FNpcShipArch::FNpcShipArch()
    : ShipArchetype(0)
    , Loadout(0)
    , StateGraph(-1)
    , Level(0)
{
    Nickname.clear();
    Pilot.clear();
}

int FNpcShipArch::GetLevel() const
{
    return Level;
}

// FUN_06f32510.
void __cdecl LoadNpcShipArchs(INI_Reader* Reader, FNpcShipArchMap* Map)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("NPCShipArch"))
            continue;

        // 06f32565-06f325a4: locals [ESP+0x30..0x54] and the key [ESP+0x18].
        FNpcShipArch Arch;
        ID_String Key;
        Key.clear();

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Key.Id = CreateID(Reader->get_value_string(0));
                Arch.Nickname.Id = Key.Id;
            }
            else if (Reader->is_value("ship_archetype"))
            {
                pub::GetShipID(Arch.ShipArchetype, Reader->get_value_string(0));
            }
            else if (Reader->is_value("pilot"))
            {
                Arch.Pilot.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("loadout"))
            {
                pub::GetLoadoutID(Arch.Loadout, Reader->get_value_string(0));
            }
            else if (Reader->is_value("state_graph"))
            {
                Arch.StateGraph = pub::StateGraph::get_state_graph(Reader->get_value_string(0),
                                                                   pub::StateGraph::LEADER);
            }
            else if (Reader->is_value("npc_class"))
            {
                // 06f32697-06f32728: every value, unique ids appended at the end.
                for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                {
                    unsigned int ClassId = CreateID(Reader->get_value_string(Index));
                    std::vector<unsigned int>::iterator Found =
                        std::find(Arch.NpcClasses.begin(), Arch.NpcClasses.end(), ClassId);
                    if (Found == Arch.NpcClasses.end())
                        Arch.NpcClasses.push_back(ClassId);
                }
            }
            else if (Reader->is_value("level"))
            {
                // 06f32820-06f32842: skip to the first digit (or the end), atoi.
                char const* Text = Reader->get_value_string(0);
                while ((*Text < '0' || *Text > '9') && *Text != '\0')
                    ++Text;
                Arch.Level = MsvcrtAtoi(Text);
            }
        }

        // 06f32740-06f327a2: pair{Key, Arch} built, Map->insert(pair).
        Map->insert(FNpcShipArchMap::value_type(Key.Id, Arch));
    }
}

// FUN_06f32850.
void __cdecl ClearNpcShipArchs()
{
    GNpcShipArchs.erase(GNpcShipArchs.begin(), GNpcShipArchs.end());
}

// FUN_06f32480.
FNpcShipArch* __cdecl FindNpcShipArch(unsigned int const* Id, FNpcShipArchMap* Map)
{
    FNpcShipArchMap::iterator It = Map->find(*Id);
    if (It == Map->end())
        return nullptr;
    return &It->second;
}

// FUN_06f324d0.
FNpcShipArch* __cdecl FindNpcShipArchByShip(unsigned int ShipArchetype, FNpcShipArchMap* Map)
{
    for (FNpcShipArchMap::iterator It = Map->begin(); It != Map->end(); ++It)
    {
        if (It->second.ShipArchetype == ShipArchetype)
            return &It->second;
    }
    return nullptr;
}

// FUN_06f31390.
bool __cdecl ParseSpaceCostume(Costume* OutCostume, INI_Reader* Reader)
{
    OutCostume->PartIds[0] = pub::GF::GetBodyPart(Reader->get_value_string(0), 1);
    OutCostume->PartIds[1] = pub::GF::GetBodyPart(Reader->get_value_string(1), 0);
    for (unsigned int Index = 2; !Reader->is_value_empty(Index); ++Index)
    {
        OutCostume->AccessoryIds[OutCostume->AccessoryCount] =
            pub::GF::GetAccessory(Reader->get_value_string(Index));
        ++OutCostume->AccessoryCount;
    }
    return OutCostume->PartIds[0] != 0 && OutCostume->PartIds[1] != 0;
}

// FUN_06f32920.
FNpcCharacterDesc::FNpcCharacterDesc()
{
    Appearance.PartIds[0] = 0;
    Appearance.PartIds[1] = 0;
    Appearance.PartIds[2] = 0;
    Appearance.PartIds[3] = 0;
    Appearance.AccessoryCount = 0;
    Voice = 0;
    IndividualName = 0;
    Affiliation = 0xFFFFFFFFu;
    Nickname.clear();
}

// FUN_06f32950.
bool FNpcCharacterDesc::ParseValue(INI_Reader* Reader, ID_String* OutNickname)
{
    if (Reader->is_value("base_appr"))
    {
        // 06f32974-06f329a8: the costume is copied only when the id resolved.
        int CostumeId;
        if (pub::GetCostumeID(CostumeId, Reader->get_value_string()) == 0)
            pub::GF::GetCostumeByID(CostumeId, Appearance);
        return true;
    }
    if (Reader->is_value("head"))
    {
        Appearance.PartIds[0] = pub::GF::GetBodyPart(Reader->get_value_string(), 1);
        return true;
    }
    if (Reader->is_value("body"))
    {
        Appearance.PartIds[1] = pub::GF::GetBodyPart(Reader->get_value_string(), 0);
        return true;
    }
    if (Reader->is_value("lefthand"))
    {
        Appearance.PartIds[2] = pub::GF::GetBodyPart(Reader->get_value_string(), 2);
        return true;
    }
    if (Reader->is_value("righthand"))
    {
        Appearance.PartIds[3] = pub::GF::GetBodyPart(Reader->get_value_string(), 3);
        return true;
    }
    if (Reader->is_value("accessory"))
    {
        Appearance.AccessoryIds[Appearance.AccessoryCount] = pub::GF::GetAccessory(Reader->get_value_string());
        ++Appearance.AccessoryCount;
        return true;
    }
    if (Reader->is_value("nickname"))
    {
        unsigned int Id = CreateID(Reader->get_value_string());
        OutNickname->Id = Id;
        Nickname.Id = Id;
        return true;
    }
    if (Reader->is_value("voice"))
    {
        pub::GetVoiceID(Voice, Reader->get_value_string());
        return true;
    }
    if (Reader->is_value("individual_name"))
    {
        IndividualName = Reader->get_value_int(0);
        return true;
    }
    if (Reader->is_value("affiliation"))
    {
        pub::Reputation::GetReputationGroup(Affiliation, Reader->get_value_string());
        return true;
    }
    return false;
}

// FUN_06f32b50.
bool FNpcCharacterDesc::IsComplete() const
{
    return Appearance.PartIds[1] != 0 && Voice != 0 && IndividualName != 0;
}

// FUN_06f32b70.
bool __cdecl LoadNpcCharacterDesc(ID_String* OutNickname, FNpcCharacterDesc* Desc, INI_Reader* Reader)
{
    while (Reader->read_value())
        Desc->ParseValue(Reader, OutNickname);

    // 06f32ba5-06f32bc6: FUN_06f32b50 inlined.
    return !OutNickname->IsEmpty() && Desc->IsComplete();
}

namespace
{
    // FmtStr::Val::Value of a value node read as the dword retail loads ([node+4]).
    int NameValue(FmtStr::Val const* Node)
    {
        return (int)Node->Value;
    }
}

// FUN_06f322a0.
bool __cdecl GetSecondNameValues(FmtStr const* /*First*/, FmtStr const* Second, int* OutA, int* OutB)
{
    *OutB = 1;
    *OutA = 1;
    switch (Second->MadLibId)
    {
    case 0x3f18:
    case 0x3f21:
    case 0x3f22:
    {
        // 06f322d5: node2, node3.
        FmtStr::Val const* Node = Second->Head->Next;
        *OutA = NameValue(Node);
        *OutB = NameValue(Node->Next);
        return true;
    }
    case 0x3f23:
    {
        // 06f322d7: node1, node2.
        FmtStr::Val const* Node = Second->Head;
        *OutA = NameValue(Node);
        *OutB = NameValue(Node->Next);
        return true;
    }
    case 0x3f1e:
        // 06f322e9: node4.
        *OutB = NameValue(Second->Head->Next->Next->Next);
        return true;
    default:
        return false;
    }
}

// FUN_06f32320.
bool __cdecl GetSecondNameThirdValue(FmtStr const* /*First*/, FmtStr const* Second, int* Out)
{
    *Out = 1;
    if (Second->MadLibId != 0x3f1a && Second->MadLibId != 0x3f1c)
        return false;
    *Out = NameValue(Second->Head->Next->Next);
    return true;
}

// FUN_06f32350.
bool __cdecl GetFirstNameValue(FmtStr const* First, FmtStr const* /*Second*/, int* Out)
{
    *Out = 1;
    switch (First->MadLibId)
    {
    case 0x3f17:
    case 0x3f19:
    case 0x3f1b:
    case 0x3f1d:
    case 0x3f20:
        *Out = NameValue(First->Head->Next->Next);
        return true;
    case 0x3f24:
        *Out = NameValue(First->Head->Next);
        return true;
    default:
        return false;
    }
}

// FUN_06f323c0.
bool __cdecl GetFirstNameLastValue(FmtStr const* First, FmtStr const* /*Second*/, int* Out)
{
    *Out = -1;
    switch (First->MadLibId)
    {
    case 0x3f17:
    case 0x3f19:
    case 0x3f1b:
    case 0x3f1d:
    case 0x3f20:
        *Out = NameValue(First->Head->Next->Next->Next);
        return true;
    case 0x3f24:
        *Out = NameValue(First->Head->Next->Next);
        return true;
    default:
        return false;
    }
}

// FUN_06f32420.
bool __cdecl RollNpcNameValue(FmtStr const* /*First*/, FmtStr const* /*Second*/, int* Out)
{
    *Out = rand() * 5 / 32768 + 1;
    return false;
}

// FUN_06f31430.
FMsnNpc::FMsnNpc()
{
    Affiliation = 0xFFFFFFFFu;
    IndividualName = 0;
    NpcShipArch.clear();
    Field_0x0C.clear();
    Appearance.PartIds[0] = 0;
    Appearance.PartIds[1] = 0;
    Appearance.PartIds[2] = 0;
    Appearance.PartIds[3] = 0;
    Appearance.AccessoryCount = 0;
    Voice = 0;
    Nickname.clear();
}

// FUN_06f31470.
bool __cdecl LoadMsnNpc(ID_String* OutNickname, FMsnNpc* Npc, INI_Reader* Reader)
{
    Npc->Voice = 0;

    // [ESP+0x10]/[ESP+0x14]: a length + char[0x50] stack string; the voice
    // name is copied into it truncated to 0x4f characters.
    char VoiceName[0x50];
    VoiceName[0] = '\0';

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            unsigned int Id = CreateID(Reader->get_value_string());
            OutNickname->Id = Id;
            Npc->Nickname.Id = Id;
        }
        else if (Reader->is_value("space_costume"))
        {
            // 06f314e3-06f31556: FUN_06f31390 inlined without its result.
            Npc->Appearance.PartIds[0] = pub::GF::GetBodyPart(Reader->get_value_string(0), 1);
            Npc->Appearance.PartIds[1] = pub::GF::GetBodyPart(Reader->get_value_string(1), 0);
            for (unsigned int Index = 2; !Reader->is_value_empty(Index); ++Index)
            {
                Npc->Appearance.AccessoryIds[Npc->Appearance.AccessoryCount] =
                    pub::GF::GetAccessory(Reader->get_value_string(Index));
                ++Npc->Appearance.AccessoryCount;
            }
        }
        else if (Reader->is_value("voice"))
        {
            char const* Value = Reader->get_value_string();
            size_t Length = 0;
            if (Value != nullptr)
            {
                Length = strlen(Value);
                if (Length >= 0x50)
                    Length = 0x4f;
                memcpy(VoiceName, Value, Length);
            }
            VoiceName[Length] = '\0';
            pub::GetVoiceID(Npc->Voice, VoiceName);
        }
        else if (Reader->is_value("npc_ship_arch"))
        {
            Npc->NpcShipArch.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("affiliation"))
        {
            pub::Reputation::GetReputationGroup(Npc->Affiliation, Reader->get_value_string());
        }
        else if (Reader->is_value("individual_name"))
        {
            Npc->IndividualName = Reader->get_value_int(0);
        }
    }

    return !OutNickname->IsEmpty();
}
