// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TU28 (06ef2070-06ef3630): mission dialogs. A mission ini's [Dialog] section (read by the
// mission object's LoadMission, FUN_06f172f0 06f185bc-06f18818) becomes one FMissionDialog in
// the global map GMissionDialogs (DAT_06fcf260) under CreateID of its nickname. Act_StartDialog
// (06ece8df) starts one through StartMissionDialog: every speaker object gets its lines as
// pending comms (FContentSpaceObj +0x108, one sent when the line before it ends), and the first
// line is sent at once. The TU21 script mission's EndMission (06eb8157) empties the map.
// No RTTI in Content; the names are the port's.

#include "NpcBase.h"            // FSpaceObjPendingComm (the +0x6c part of a line)
#include "common_tstring.h"

#include <cstring>
#include <map>
#include <vector>

class INI_Reader;
struct FSystemInfo;

// One "line = speaker, target, line nickname" value of a [Dialog] section: the 0x7c-byte
// element of a speaker's vector (appended by FUN_06ef3190 in the loader FUN_06ef2380).
struct FMissionDialogLine
{
    TString<0x30> Speaker;                        // +0x00 value 0 (the speaker's map key)
    TString<0x30> Target;                         // +0x34 value 1 (empty when absent)
    // +0x68: the first line of the section (the id before it, masked with 0x3fffffff, is 0);
    // StartMissionDialog sends this line at once.
    unsigned char bFirst;
    unsigned char Field_0x69;                     // +0x69 TODO: meaning (loader 0, no reader found)
    // +0x6c: the pending comm the speaker gets: Key = the id of the line before this one in the
    // section (0 for the first), TextId = pub::GetNicknameId of value 2, TargetId = the target's
    // SpaceObj id (written by StartMissionDialog), Field_0x0c = 0.
    FSpaceObjPendingComm Comm;
};

// The speaker map's order: the key chars compared byte by byte, unsigned (FUN_06ef2db0 /
// FUN_06ef2ae0: CMP / SBB / SBB -1, then SETL) = strcmp < 0.
struct FMissionDialogSpeakerLess
{
    bool operator()(TString<0x30> const& A, TString<0x30> const& B) const
    {
        return strcmp(A.Data, B.Data) < 0;
    }
};

// The value of a GMissionDialogs node (0x1c bytes at node +0x10).
struct FMissionDialog
{
    // FUN_06ef2380 (RET 4): the rest of a [Dialog] section. "line" values go to the speaker's
    // vector (the speaker added on first use), each one keyed on the id of the line before it;
    // "system" sets System (FindSystemInfo of pub::GetSystemID); other values are skipped.
    void Load(INI_Reader* Reader);
    // FUN_06ef2610 (RET 4): false when the player is in another system than the dialog's.
    // Without a system nothing happens (true). Otherwise every speaker (by name: a mission
    // object, or "Player") gets its lines as pending comms, each line's target is resolved,
    // and the first line is sent from its speaker (duration 1.0f when it goes to a player's
    // ship, else 0.5f). False when any speaker or target was not found.
    bool Start(unsigned int const* PlayerId);

    unsigned int ControllerId;                    // +0x00 the mission's controller (FindObjectIdByName)
    // +0x04: speaker name -> its lines in section order (ctor FUN_06f20af0 / copy FUN_06f209f0).
    std::map<TString<0x30>, std::vector<FMissionDialogLine>, FMissionDialogSpeakerLess> Speakers;
    FSystemInfo* System;                          // +0x18 "system" (null when absent)
};

// DAT_06fcf260: dialog nickname id -> dialog (ctor FUN_06ef2a70 from the TU28 initializer
// LAB_06ef2270, atexit FUN_06ef22a0). Inserted by LoadMission (FUN_06f1f2a0, unique: a duplicate
// id keeps the first dialog); erased whole by the TU21 EndMission (06eb8157-06eb817a).
extern std::map<unsigned int, FMissionDialog> GMissionDialogs;

// FUN_06ef2a10 (cdecl; caller Act_StartDialog 06ece8df with the act's dialog id and the
// mission's player id): Start of the dialog *DialogId, false when there is none.
bool __cdecl StartMissionDialog(unsigned int const* DialogId, unsigned int const* PlayerId);
