// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ContentInternal.h"
// TU0-14 static IDs (06ea1000..06ea58c0: 16 ids + "default" per retail TU; the port merges
// the 15 mission TUs into this file, so one copy stands for all 15).
#include "ContentStaticIds.h"
#include "ContentDefaultId.h"
#include "BaseDB.h"
#include "ContentPlayerState.h"
#include "MissionScript.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "server_cobject.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <string.h>

// Mission_01a..Mission_13 / GenericMission (TU0-TU14) over the TU21 script mission
// FMissionScript (MissionScript.h); the TU38 base is FMissionObject (MissionObject.h).

namespace
{


    // Save message parms (msg 4) and load message parms (msg 5) of the mission overrides.
    struct FMissionSaveParms
    {
        unsigned int PlayerId;
        FILE* File;
    };

    struct FMissionLoadParms
    {
        unsigned int PlayerId;
        INI_Reader* Reader;
    };

    // Retail DAT_06fce968 / DAT_06fce964: base ids looked up by the Mission_01a ctor.
    unsigned int GLi0101BaseId = 0;
    unsigned int GLi0102BaseId = 0;

    // GenericMission: 0x32c bytes, vtable 06fb37d0 (TU21 table, own dtor slot only).
    class FGenericMission : public FMissionScript
    {
    public:
        // FUN_06ea1220 (06ea1220-06ea1449, RET 8). The ini path is the char array that
        // follows the CreateParms (parms+0xc, FStory::OnLoad lays it out); no validation.
        FGenericMission(unsigned int const* InControllerId, FStoryMissionCreateParms const* Parms)
            : FMissionScript(InControllerId, Parms)
        {
            LoadMissionIni(reinterpret_cast<char const*>(Parms) + 0xc);
        }
    };

    // Mission_01a: 0x440 bytes, vtable 06fb38a8 (slot 21 = 06ea1b60).
    class FMission01a : public FMissionScript
    {
    public:
        // FUN_06ea1880 (06ea1880-06ea1b3b, RET 8).
        FMission01a(unsigned int const* InControllerId, FStoryMissionCreateParms const* Parms);
        // LAB_06ea1b60 (06ea1b60-06ea1d25).
        void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;

    private:
        // +0x32c once-flag: set when the player first enters Li01_01_Base (msg 1, 06ea1d12);
        // gates the [Mission01aSave] block (06ea1ca5).
        unsigned char bBaseEnterHandled;
        // Write-only in retail: only this ctor stores them (06ea1921-06ea1939). Vtable
        // 06fb38a8 differs from the TU21 table 06fb4710 in slots 1 (06ea1b40) and 21
        // (06ea1b60) only, and neither reads +0x334..+0x43d. +0x334 and +0x3b8 are 0x84
        // apart (the first dword of two 0x84-byte blocks ending at +0x43c).
        // TODO: meaning (no reader exists to derive it from).
        int Field_0x334;                              // +0x334 (ctor -1)
        int Field_0x3b8;                              // +0x3b8 (ctor -1)
        unsigned char Field_0x43c;                    // +0x43c (ctor 0)
        unsigned char Field_0x43d;                    // +0x43d (ctor 0)
    };

    // 06ea18c0-06ea1991: chapter 1/2, 0x5528/0x7d00, heartbeat 0.0333 again (+0x14 and
    // SetHeartbeatInterval), +0x2ac = 3, +0x43c/+0x43d/+0x32c = 0, +0x334/+0x3b8 = -1,
    // then the ini, then pub::GetBaseID into DAT_06fce968 / DAT_06fce964.
    FMission01a::FMission01a(unsigned int const* InControllerId,
                             FStoryMissionCreateParms const* Parms)
        : FMissionScript(InControllerId, Parms)
        , bBaseEnterHandled(0)
        , Field_0x334(-1)
        , Field_0x3b8(-1)
        , Field_0x43c(0)
        , Field_0x43d(0)
    {
        SetChapter(1, 2, 0x5528, 0x7d00);
        HeartbeatInterval = 0.0333333351f;
        pub::Controller::SetHeartbeatInterval(ControllerId, HeartbeatInterval);
        MissionState = 3;
        LoadMissionIni("Missions\\M01a\\m01a.ini");
        pub::GetBaseID(GLi0101BaseId, "Li01_01_Base");
        pub::GetBaseID(GLi0102BaseId, "Li01_02_Base");
    }

    void FMission01a::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                           void const* MessageParms)
    {
        using pub::Controller::EMessage;
        if (MessageId == EMessage::BaseEnter)
        {
            // 06ea1cde-06ea1d19: {BaseId, PlayerId}.
            FMissionScript::HandleMissionMessage(EMessage::BaseEnter, MessageParms);
            unsigned int const* BaseParms = static_cast<unsigned int const*>(MessageParms);
            if (BaseParms[1] != PlayerState->PlayerId)
                return;
            if (BaseParms[0] != GLi0101BaseId)
                return;
            if (bBaseEnterHandled != 0)
                return;
            bBaseEnterHandled = 1;
            // 06ea1d19: CALL [vtbl+0x78] = slot 30 FUN_06eb9aa0.
            ApplyDesiredMissionState();
            return;
        }
        if (MessageId == EMessage::PlayerSave)
        {
            // 06ea1c78-06ea1cdb.
            FMissionSaveParms const* Save = static_cast<FMissionSaveParms const*>(MessageParms);
            if (Save->PlayerId != PlayerState->PlayerId)
                return;
            if (!pub::SinglePlayer())
                return;
            FMissionScript::HandleMissionMessage(EMessage::PlayerSave, MessageParms);
            if (bBaseEnterHandled == 0)
                return;
            fprintf(Save->File, "\n[Mission01aSave]\n");
            fprintf(Save->File, "MissionStateNum = %d\n\n", PreviousMissionState);
            return;
        }
        if (MessageId == EMessage::PlayerLoad)
        {
            // 06ea1ba3-06ea1c75.
            FMissionLoadParms const* Load = static_cast<FMissionLoadParms const*>(MessageParms);
            if (Load->PlayerId != PlayerState->PlayerId)
                return;
            FMissionScript::HandleMissionMessage(EMessage::PlayerLoad, MessageParms);
            INI_Reader* Reader = Load->Reader;
            INI_Reader::State SavedState;
            Reader->get_state(SavedState);
            Reader->reset();
            while (Reader->read_header())
            {
                if (!Reader->is_header("Mission01aSave"))
                    continue;
                while (Reader->read_value())
                {
                    if (!Reader->is_value("MissionStateNum"))
                        continue;
                    int Value = Reader->get_value_int(0);
                    PreviousMissionState = Value;
                    MissionState = Value;
                    // 06ea1c27-06ea1c3a: not 3 -> FUN_06f1b610 (ECX = this).
                    if (Value != 3)
                        RenumberAllObjectives();
                }
            }
            Reader->set_state(SavedState);
            return;
        }
        FMissionScript::HandleMissionMessage(MessageId, MessageParms);
    }

    // Mission_01b: 0x330 bytes, vtable 06fb3998 (slot 21 = 06ea21c0).
    class FMission01b : public FMissionScript
    {
    public:
        // FUN_06ea1f50 (06ea1f50-06ea21b5, RET 8).
        FMission01b(unsigned int const* InControllerId, FStoryMissionCreateParms const* Parms);
        // LAB_06ea21c0 (06ea21c0-06ea2406).
        void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;

    private:
        // FUN_06ea2410 (06ea2410-06ea252e, RET 4).
        void LoadSuccessState(INI_Reader* Reader);

        // +0x32c once-flag: set by msg 1 after the group join (06ea235b), by msg 5
        // (06ea225c) and by LoadSuccessState (06ea241f); tested at 06ea22eb.
        unsigned char bBaseEnterHandled;
    };

    // 06ea1f8f-06ea2000: +0x2a8 = +0x2ac = +0x2b4 = +0x2b8 = 4, +0x32c = 0,
    // 0x55f0/0x7d64, then the ini.
    FMission01b::FMission01b(unsigned int const* InControllerId,
                             FStoryMissionCreateParms const* Parms)
        : FMissionScript(InControllerId, Parms)
        , bBaseEnterHandled(0)
    {
        SetChapter(4, 4, 0x55f0, 0x7d64);
        PreviousMissionState = 4;
        MissionState = 4;
        LoadMissionIni("Missions\\M01b\\m01b.ini");
    }

    void FMission01b::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                           void const* MessageParms)
    {
        using pub::Controller::EMessage;
        if (MessageId == EMessage::BaseEnter)
        {
            // 06ea22cb-06ea23f6: {BaseId, PlayerId}.
            FMissionScript::HandleMissionMessage(EMessage::BaseEnter, MessageParms);
            unsigned int const* BaseParms = static_cast<unsigned int const*>(MessageParms);
            if (PlayerState->PlayerId != BaseParms[1])
                return;
            if (bBaseEnterHandled != 0)
                return;
            unsigned int BaseId = 0;
            pub::GetBaseID(BaseId, "Li01_02_base");
            // 06ea230e-06ea2327: a cleared ID_String, then FUN_06f488b0(PlayerState, ControllerId,
            // 0, &IdString, 0, 0) (TU46; the arguments in retail's push order).
            ID_String Vnpc;
            Vnpc.clear();
            PlayerState->JoinMissionAsGroup(ControllerId, 0, &Vnpc, false, 0);
            StoryEvents->OnMissionEvent(1, static_cast<unsigned int>(CampaignSlotSucceed),
                                        ControllerId);
            SpacePopSetAttackClamp(1);                // DAT_06fd1c78 = 1
            bBaseEnterHandled = 1;
            FStoryMissionListNode_0x2f4 Node;
            // .rdata:06fb3a14 (raw PE bytes).
            char const* Path = "missions\\m01a\\M001a_s006x_Li01_02_nrml.ini";
            size_t Length = strlen(Path);
            if (Length >= 0x80)
                Length = 0x7f;
            memcpy(Node.Path, Path, Length);
            Node.Path[Length] = '\0';
            Node.PathLength = static_cast<unsigned int>(Length);
            // 06ea23af-06ea23b6: FUN_06eb8a20(this, &Node) (TU21 RTC script start), then the
            // node is copied to the end of +0x2f0 (06ea23bb-06ea23f6).
            StartRtcScript(&Node);
            List_0x2f4.push_back(Node);
            return;
        }
        if (MessageId == EMessage::PlayerSave)
        {
            // 06ea226e-06ea22c8.
            FMissionSaveParms const* Save = static_cast<FMissionSaveParms const*>(MessageParms);
            if (Save->PlayerId != PlayerState->PlayerId)
                return;
            if (!pub::SinglePlayer())
                return;
            FMissionScript::HandleMissionMessage(EMessage::PlayerSave, MessageParms);
            fprintf(Save->File, "\n[Mission01bSave]\n");
            fprintf(Save->File, "MissionStateNum = %d\n\n", PreviousMissionState);
            return;
        }
        if (MessageId == EMessage::PlayerLoad)
        {
            // 06ea2205-06ea226b.
            FMissionLoadParms const* Load = static_cast<FMissionLoadParms const*>(MessageParms);
            if (Load->PlayerId != PlayerState->PlayerId)
                return;
            FMissionScript::HandleMissionMessage(EMessage::PlayerLoad, MessageParms);
            INI_Reader::State SavedState;
            Load->Reader->get_state(SavedState);
            Load->Reader->reset();
            LoadSuccessState(Load->Reader);
            Load->Reader->set_state(SavedState);
            bBaseEnterHandled = 1;
            return;
        }
        FMissionScript::HandleMissionMessage(MessageId, MessageParms);
    }

    void FMission01b::LoadSuccessState(INI_Reader* Reader)
    {
        bBaseEnterHandled = 1;
        // 06ea241b-06ea243e: a cleared ID_String, then FUN_06f488b0(PlayerState, ControllerId,
        // 0, &IdString, 0, 0) (TU46; the arguments in retail's push order).
        ID_String Vnpc;
        Vnpc.clear();
        PlayerState->JoinMissionAsGroup(ControllerId, 0, &Vnpc, false, 0);
        StoryEvents->OnMissionEvent(1, static_cast<unsigned int>(CampaignSlotSucceed),
                                    ControllerId);
        SpacePopSetAttackClamp(1);                    // DAT_06fd1c78 = 1
        // 06ea245a-06ea24ac: FUN_06eba430(this, {len, "missions\\m01a\\rtc_success.ini"}) (the
        // TString<0x80> built on the stack: strlen, clamp 0x7f, copy, NUL).
        TString<0x80> RtcPath;
        RtcPath = "missions\\m01a\\rtc_success.ini";
        RemoveMissionRtc(this, &RtcPath);
        while (Reader->read_header())
        {
            if (!Reader->is_header("Mission01bSave"))
                continue;
            while (Reader->read_value())
            {
                if (Reader->is_value("MissionStateNum"))
                    MissionState = Reader->get_value_int(0);
            }
        }
    }

    // Mission_02 .. Mission_12: vtable = the TU21 table with only the dtor slot changed;
    // the ctors (FUN_06ea2760, 06ea2be0, 06ea3060, 06ea34e0, 06ea3960, 06ea3de0, 06ea4260,
    // 06ea46e0, 06ea4b60, 06ea4fe0, 06ea5460) differ only in the four stores and the ini.
    class FMission02 : public FMissionScript
    {
    public:
        FMission02(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(7, 8, 0x56b8, 0x7dc8);
            LoadMissionIni("Missions\\M02\\m02.ini");
        }
    };

    class FMission03 : public FMissionScript
    {
    public:
        FMission03(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(10, 11, 0x5780, 0x7e2c);
            LoadMissionIni("Missions\\M03\\m03.ini");
        }
    };

    class FMission04 : public FMissionScript
    {
    public:
        FMission04(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(13, 14, 0x5848, 0x7e90);
            LoadMissionIni("Missions\\M04\\m04.ini");
        }
    };

    class FMission05 : public FMissionScript
    {
    public:
        FMission05(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(16, 17, 0x5910, 0x7ef4);
            LoadMissionIni("Missions\\M05\\m05.ini");
        }
    };

    class FMission06 : public FMissionScript
    {
    public:
        FMission06(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(19, 20, 0x59d8, 0x7f58);
            LoadMissionIni("Missions\\M06\\m06.ini");
        }
    };

    class FMission07 : public FMissionScript
    {
    public:
        FMission07(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(22, 23, 0x5aa0, 0x7fbc);
            LoadMissionIni("Missions\\M07\\m07.ini");
        }
    };

    class FMission08 : public FMissionScript
    {
    public:
        FMission08(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(25, 26, 0x5b68, 0x8020);
            LoadMissionIni("Missions\\M08\\m08.ini");
        }
    };

    class FMission09 : public FMissionScript
    {
    public:
        FMission09(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(28, 29, 0x5c30, 0x8084);
            LoadMissionIni("Missions\\M09\\m09.ini");
        }
    };

    class FMission10 : public FMissionScript
    {
    public:
        FMission10(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(31, 32, 0x5cf8, 0x80e8);
            LoadMissionIni("Missions\\M10\\m10.ini");
        }
    };

    class FMission11 : public FMissionScript
    {
    public:
        FMission11(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(34, 35, 0x5dc0, 0x814c);
            LoadMissionIni("Missions\\M11\\m11.ini");
        }
    };

    class FMission12 : public FMissionScript
    {
    public:
        FMission12(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(37, 38, 0x5e88, 0x81b0);
            LoadMissionIni("Missions\\M12\\m12.ini");
        }
    };

    // Mission_13: vtable 06fb4198, slot 23 overridden by 06ea5b50.
    class FMission13 : public FMissionScript
    {
    public:
        // FUN_06ea58e0.
        FMission13(unsigned int const* Id, FStoryMissionCreateParms const* Parms)
            : FMissionScript(Id, Parms)
        {
            SetChapter(40, 41, 0x5f50, 0x8214);
            LoadMissionIni("Missions\\M13\\m13.ini");
        }

        // FUN_06ea5b50 (06ea5b50-06ea5c0a): the base SUCCEED display (direct CALL
        // FUN_06eb9760), then madlib 0x5faa shown to +0xc with type 9, broadcast true.
        void OnMissionSucceed() override
        {
            unsigned int const TargetPlayerId = PlayerId;
            FMissionScript::OnMissionSucceed();
            FmtStr MissionMsg;
            MissionMsg.begin_mad_lib(0x5faa);
            MissionMsg.end_mad_lib();
            pub::Player::DisplayMissionMessage(TargetPlayerId, MissionMsg,
                                               static_cast<MissionMessageType>(9), true);
        }
    };

    // FUN_06eeec50: operator new(size) then the class ctor with (&ControllerId, Parms);
    // a failed new yields 0.
    template <class TMission>
    IDLL* NewMission(unsigned int ControllerId, void const* Parms)
    {
        return new (std::nothrow)
            TMission(&ControllerId, static_cast<FStoryMissionCreateParms const*>(Parms));
    }
}

IDLL* CreateGenericMission(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FGenericMission>(ControllerId, Parms);
}

IDLL* CreateMission01a(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission01a>(ControllerId, Parms);
}

IDLL* CreateMission01b(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission01b>(ControllerId, Parms);
}

IDLL* CreateMission02(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission02>(ControllerId, Parms);
}

IDLL* CreateMission03(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission03>(ControllerId, Parms);
}

IDLL* CreateMission04(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission04>(ControllerId, Parms);
}

IDLL* CreateMission05(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission05>(ControllerId, Parms);
}

IDLL* CreateMission06(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission06>(ControllerId, Parms);
}

IDLL* CreateMission07(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission07>(ControllerId, Parms);
}

IDLL* CreateMission08(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission08>(ControllerId, Parms);
}

IDLL* CreateMission09(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission09>(ControllerId, Parms);
}

IDLL* CreateMission10(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission10>(ControllerId, Parms);
}

IDLL* CreateMission11(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission11>(ControllerId, Parms);
}

IDLL* CreateMission12(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission12>(ControllerId, Parms);
}

IDLL* CreateMission13(char const*, unsigned int ControllerId, void const* Parms)
{
    return NewMission<FMission13>(ControllerId, Parms);
}
