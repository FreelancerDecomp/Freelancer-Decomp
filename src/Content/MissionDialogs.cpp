// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU28 (06ef2070-06ef3630): mission dialogs. See MissionDialogs.h.

#include "MissionDialogs.h"

#include "ContentStaticIds.h"   // TU28's 16 static-init thunks 06ef2070-06ef2250
#include "ContentPlayerState.h" // FindContentPlayerState (FUN_06f49b00), ...ByShip (FUN_06f49bc0)
#include "SystemInfo.h"         // FSystemInfo (TU58), FindSystemInfo (FUN_06f75f30)
#include "common_ini.h"
#include "common_server.h"      // pub::GetSystemID, pub::GetNicknameId

#include <memory>
#include <utility>

// LAB_06ef2270: FUN_06ef2a70 on DAT_06fcf260, atexit FUN_06ef22a0 (the map teardown).
std::map<unsigned int, FMissionDialog> GMissionDialogs;

// FUN_06ef2380 (06ef2380-06ef2608, RET 4). The id of the previous line (06ef23af, stack
// +0x10) starts at 0 and carries over from speaker to speaker.
void FMissionDialog::Load(INI_Reader* Reader)
{
    unsigned int PrevLineId = 0;
    while (Reader->read_value())
    {
        if (Reader->is_value("line"))
        {
            // 06ef23df-06ef23f8: the speaker name (FUN_06ed0160 into an empty TString).
            TString<0x30> SpeakerName;
            SpeakerName.Length = 0;
            SpeakerName.Data[0] = '\0';
            SpeakerName = Reader->get_value_string(0);
            // 06ef23fd-06ef24df: find the speaker (FUN_06ef2db0); a new one is inserted with an
            // empty vector (pair FUN_06ef2e80, insert FUN_06ef2ae0) and found again.
            std::map<TString<0x30>, std::vector<FMissionDialogLine>, FMissionDialogSpeakerLess>::iterator
                It = Speakers.find(SpeakerName);
            if (It == Speakers.end())
            {
                std::vector<FMissionDialogLine> NoLines;
                Speakers.insert(std::pair<TString<0x30> const, std::vector<FMissionDialogLine>>(
                    SpeakerName, NoLines));
                It = Speakers.find(SpeakerName);
            }
            // 06ef24e3-06ef2585: the line: both names empty, +0x68/+0x69/+0x78 zero; the speaker
            // copied from the key (FUN_06ea9f40, max 0x30), the target from value 1 (empty when
            // absent); +0x6c the previous id, +0x70 GetNicknameId(value 2), +0x68 set when the
            // previous id has none of its low 30 bits (AND 0x3fffffff; NEG; SBB; INC).
            FMissionDialogLine Line;
            Line.Speaker.Length = 0;
            Line.Speaker.Data[0] = '\0';
            Line.Target.Length = 0;
            Line.Target.Data[0] = '\0';
            Line.Field_0x69 = 0;
            Line.bFirst = 0;
            Line.Comm.Field_0x0c = 0;
            Line.Speaker = SpeakerName.Data;
            Line.Target = Reader->get_value_string(1);
            Line.Comm.Key = PrevLineId;
            Line.Comm.TextId = pub::GetNicknameId(Reader->get_value_string(2));
            Line.Comm.TargetId = 0;   // retail leaves +0x74 unset until StartMissionDialog
            Line.bFirst = (PrevLineId & 0x3fffffff) == 0 ? 1 : 0;
            // 06ef258c-06ef259a: FUN_06ef3190 (insert 1 at the end).
            It->second.push_back(Line);
            PrevLineId = Line.Comm.TextId;
        }
        else if (Reader->is_value("system"))
        {
            // 06ef25bb-06ef25dd.
            unsigned int SystemId;
            pub::GetSystemID(SystemId, Reader->get_value_string(0));
            System = FindSystemInfo(&SystemId);
        }
    }
}

// FUN_06ef2610 (06ef2610-06ef29ff, RET 4). Locals: bResult (local_79, starts 1), Speaker
// (local_74, the speaker object of the last first line), TargetId / TextId (local_64 /
// local_58, that line's). The pending-comm vector (local_50) is the one EH state.
bool FMissionDialog::Start(unsigned int const* PlayerId)
{
    bool bResult = true;
    // 06ef2634-06ef2679: a player in another system than the dialog's -> false.
    FContentPlayerState* PlayerState = FindContentPlayerState(*PlayerId);
    if (PlayerState != nullptr)
    {
        FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
        if (PlayerSystem != nullptr && System != nullptr && PlayerSystem->SystemId != System->SystemId)
            return false;
    }
    if (System == nullptr)
        return bResult;

    FContentSpaceObj* Speaker = nullptr;
    unsigned int TargetId = 0;
    unsigned int TextId = 0;
    std::map<TString<0x30>, std::vector<FMissionDialogLine>, FMissionDialogSpeakerLess>::iterator
        It = Speakers.begin();
    for (; It != Speakers.end(); ++It)
    {
        std::vector<FSpaceObjPendingComm> Comms;
        std::vector<FMissionDialogLine>::iterator LineIt = It->second.begin();
        for (; LineIt != It->second.end(); ++LineIt)
        {
            FMissionDialogLine& Line = *LineIt;
            // 06ef26f0-06ef2775: the target. "Player" is the ship of the player whose ship has
            // the resolved id, else of the mission's player; any other name must be a ship or
            // solar of the system. A miss clears bResult and skips to the comm (the line keeps
            // its old target id and its first-line part is skipped).
            unsigned int LineTargetId = System->FindObjectIdByName(&Line.Target, ControllerId);
            bool bTargetFound = false;
            if (Line.Target == "Player")
            {
                FContentPlayerState* TargetPlayer = FindContentPlayerStateByShip(&LineTargetId);
                if (TargetPlayer == nullptr)
                    TargetPlayer = FindContentPlayerState(*PlayerId);
                if (TargetPlayer != nullptr && TargetPlayer->ShipObject != nullptr)
                {
                    LineTargetId = TargetPlayer->ShipObject->SpaceObjId;
                    bTargetFound = true;
                }
            }
            else
            {
                bTargetFound = System->FindShipOrSolar(LineTargetId) != nullptr;
            }

            if (!bTargetFound)
            {
                bResult = false;
            }
            else
            {
                Line.Comm.TargetId = LineTargetId;
                if (Line.bFirst != 0)
                {
                    // 06ef2787-06ef27a6: a TString copy of the speaker's chars is built and never
                    // read (FindObjectIdByName gets the line's own name) - not reproduced.
                    // 06ef27aa-06ef2841: the speaker. A missing player state keeps the previous
                    // Speaker and clears bResult; a player without a ship, or a missing object,
                    // sets Speaker null and clears bResult.
                    unsigned int SpeakerId = System->FindObjectIdByName(&Line.Speaker, ControllerId);
                    if (Line.Speaker == "Player")
                    {
                        FContentPlayerState* SpeakerPlayer = FindContentPlayerStateByShip(&SpeakerId);
                        if (SpeakerPlayer == nullptr)
                            SpeakerPlayer = FindContentPlayerState(*PlayerId);
                        if (SpeakerPlayer == nullptr)
                        {
                            // 06ef280e-06ef282e: Speaker kept; bResult cleared (again at
                            // 06ef282e when Speaker is null).
                            bResult = false;
                        }
                        else
                        {
                            Speaker = SpeakerPlayer->ShipObject;
                            if (Speaker == nullptr)
                                bResult = false;
                        }
                    }
                    else
                    {
                        Speaker = System->FindShipOrSolar(SpeakerId);
                        if (Speaker == nullptr)
                            bResult = false;
                    }
                    TargetId = Line.Comm.TargetId;
                    TextId = Line.Comm.TextId;
                }
            }
            // 06ef2848-06ef2857: FUN_06f69d50 (insert 1 at the end) of the line's +0x6c.
            Comms.push_back(Line.Comm);
        }

        // 06ef2880-06ef2903: the speaker object of this map entry (same resolution as the
        // target) gets the comms (FUN_06eb6ea0 = vector assignment on +0x108); a miss clears
        // bResult.
        unsigned int OwnerId = System->FindObjectIdByName(&It->first, ControllerId);
        FContentSpaceObj* Owner = nullptr;
        if (It->first == "Player")
        {
            FContentPlayerState* OwnerPlayer = FindContentPlayerStateByShip(&OwnerId);
            if (OwnerPlayer == nullptr)
                OwnerPlayer = FindContentPlayerState(*PlayerId);
            if (OwnerPlayer != nullptr && OwnerPlayer->ShipObject != nullptr)
                Owner = OwnerPlayer->ShipObject;
        }
        else
        {
            Owner = System->FindShipOrSolar(OwnerId);
        }
        if (Owner != nullptr)
            Owner->PendingComms = Comms;
        else
            bResult = false;
    }

    // 06ef2942-06ef29e5: the first line from its speaker; 1.0f when the target is a player's
    // ship (the first match in the player map DAT_06fd0bbc ends the walk), else 0.5f.
    if (Speaker == nullptr)
        return bResult;
    float Duration = 0.5f;
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players = GetContentPlayerStates();
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator PlayerIt = Players.begin();
    for (; PlayerIt != Players.end(); ++PlayerIt)
    {
        if (TargetId == PlayerIt->second->GetShipId())
        {
            Duration = 1.0f;
            break;
        }
    }
    System->SendComm(Speaker->SpaceObjId, TargetId, TextId, 0, 0, nullptr, Duration);
    return bResult;
}

// FUN_06ef2a10 (06ef2a10-06ef2a63, cdecl): find (FUN_06fa7200 lower bound + key test).
bool __cdecl StartMissionDialog(unsigned int const* DialogId, unsigned int const* PlayerId)
{
    std::map<unsigned int, FMissionDialog>::iterator It = GMissionDialogs.find(*DialogId);
    if (It == GMissionDialogs.end())
        return false;
    return It->second.Start(PlayerId);
}
