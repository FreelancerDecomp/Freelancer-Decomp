// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU22 (06ed3d90-06ed4220): the parsers and offer tests of the GF_NPC
// "bribe" / "know" / "rumor" records of missions\mbases.ini (types in BaseDB.h; their
// loader FUN_06ea7f60 is TU17, BaseDB.cpp). The TU opens with its own 16 static-ID
// initializers 06ed3d90-06ed3f70 (DAT_06fcf058..DAT_06fcf01c) = ContentStaticIds.h.

#include "ContentStaticIds.h"

#include "BaseDB.h"
#include "ContentPlayerState.h"
#include "ContentStageNames.h"
#include "common_ini.h"
#include "common_server.h"

#include <cstdint>
#include <cstring>

namespace
{
    // FUN_06ed4010 06ed4029-06ed4057 / 06ed4070-06ed409e: the value of the first
    // GContentStageNames entry whose name matches (all 43 entries, null names skipped);
    // Default when none does.
    int FindStageValue(char const* Name, int Default)
    {
        for (int Index = 0; Index < 43; ++Index)
        {
            if (GContentStageNames[Index].Name != nullptr &&
                _stricmp(Name, GContentStageNames[Index].Name) == 0)
            {
                return GContentStageNames[Index].Value;
            }
        }
        return Default;
    }
}

// DAT_06fce350 (read only by FUN_06ed3f90 06ed3f97 and FUN_06ed4100 06ed411d).
float const GBaseNpcRepThresholds[4] = { 0.0f, 0.2f, 0.4f, 0.6f };

// FUN_06ed41c0 (06ed41c0-06ed4215).
bool FBaseNpcBribe::Parse(INI_Reader* Reader)
{
    if (pub::Reputation::GetReputationGroup(Group, Reader->get_value_string(0)) != 0)
        return false;
    Cost = Reader->get_value_int(1);
    if (Cost == 0)
        return false;
    TextIds = Reader->get_value_int(2);
    return true;
}

// FUN_06ed4160 (06ed4160-06ed41ae). 06ed4187 calls IAT 06fb35e8 =
// ?GetReputation@Reputation@pub@@YAHABHABIAAM@Z (result ignored). FCOMP DAT_06fb60ac
// (0.6), TEST AH,5 / JP: not below (or unordered) -> 0; below -> 1.
bool FBaseNpcBribe::IsOffered(FContentPlayerStateBase const* Player) const
{
    float Feeling = 0.0f;
    int Reputation = Player->GetReputation();
    pub::Reputation::GetReputation(Reputation, Group, Feeling);
    return Feeling < 0.6f;
}

// FUN_06ed3f90 (06ed3f90-06ed3fc0). FCOMP threshold, TEST AH,1 / JNZ: below (or
// unordered) -> 0.
bool FBaseNpcKnow::IsOffered(FContentPlayerState const* Player, float Feeling) const
{
    if (!(Feeling >= GBaseNpcRepThresholds[RepLevel]))
        return false;
    return Player->FindRumor(static_cast<unsigned int>(Field_0x04)) == 0;
}

// FUN_06ed3fd0 (06ed3fd0-06ed400b).
bool FBaseNpcKnow::Parse(INI_Reader* Reader)
{
    Field_0x08 = Reader->get_value_int(0);
    Field_0x04 = Reader->get_value_int(1);
    Price = Reader->get_value_int(2);
    RepLevel = static_cast<unsigned int>(Reader->get_value_int(3));
    return RepLevel < 4;
}

// FUN_06ed4010 (06ed4010-06ed40f5).
bool FBaseNpcRumor::Parse(INI_Reader* Reader)
{
    // Retail keeps the stage in the stack slot of the Reader argument, never
    // initialised: a name missing from the table leaves the Reader pointer bits
    // (06ed4053), and a missing second name repeats the first result (06ed409e).
    int Stage = static_cast<int>(reinterpret_cast<intptr_t>(Reader));
    Stage = FindStageValue(Reader->get_value_string(0), Stage);
    FromStage = Stage;
    Stage = FindStageValue(Reader->get_value_string(1), Stage);
    ToStage = Stage;
    RepLevel = Reader->get_value_int(2);

    // 06ed40b4-06ed40ec: values 3.. into Ids until the first empty one. Retail has no
    // bound on the 8 slots; missions\mbases.ini never gives more than one id.
    unsigned int Index = 3;
    while (!Reader->is_value_empty(Index))
    {
        Ids[Index - 3] = Reader->get_value_int(Index);
        ++Index;
    }
    return true;
}

// FUN_06ed4100 (06ed4100-06ed415b). Stage bounds are signed (JL / JG); the rumor test is
// unsigned (CMP EAX,1 / JNC).
int FBaseNpcRumor::GetWeight(FContentPlayerState const* Player, float Feeling) const
{
    int Stage = static_cast<int>(Player->MissionNumber);
    if (Stage < FromStage || Stage > ToStage)
        return 0;
    if (!(Feeling >= GBaseNpcRepThresholds[RepLevel]))
        return 0;
    if (Player->FindRumor(static_cast<unsigned int>(Ids[0])) >= 1)
        return 0;
    if (FromStage > 0 && ToStage < 0x2a)
        return 0x40;
    return (RepLevel + 1) * 5;
}
