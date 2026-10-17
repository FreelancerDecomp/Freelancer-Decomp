// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IClientImpl slots outside IClientS.cpp (see freelancer_client.h for the split):
//   the GF mirror forwards   .text:00437510..00437b30 (slots 21..45, 74),
//   the player-state block   .text:005a5a60..005a64f0 (slots 17, 66, 67, 70..80, 101..108,
//                            110, 111) and IChatClient slot 0 (.text:005a6250),
//   the login forwards       .text:005a81f0..005a8240 (slots 11..14),
//   three shared stubs       0052fba0 (slot 10), 00401e90 (slot 15), 0051ca40 (slot 16).
// Every slot's retail body is quoted by address.

#include "freelancer_client.h"

#include "best_path.h"                // objective map slots 71 / 72 / 73 (DAT_0067294c)
#include "character_select_equip.h"   // player-state setters (FUN_004c3e20 ..)
#include "chat_init.h"                // ChatDispatchIncoming (FUN_0046a150), FUN_0046b500/0046b520
#include "client_gameobj.h"           // FClientGameobj / FClientShipGameobj
#include "client_objdb.h"             // ClientObjDb_FindObject (FUN_005416c0)
#include "common_cobject.h"           // CEqObj
#include "common_goods.h"             // EquipDescList, the mirror property classes
#include "common_iface.h"             // ScriptBehavior
#include "common_id.h"                // MakeId
#include "common_net.h"               // MarshalBuffer
#include "common_reputation.h"        // Reputation::Vibe
#include "common_server.h"            // IServer (DAT_0067ecd0)
#include "common_misc.h"              // SinglePlayer
#include "dacom.h"
#include "hud_target.h"               // G_bTradeRequested (DAT_00674b00)
#include "login_mgr.h"                // CLoginMgr G_LoginMgr (DAT_0067e7b8)
#include "menu_universe_base.h"       // LocationController setters (FUN_0043a5a0 ..)
#include "misc_util.h"                // FmtStrSprintf (FUN_0041c970)
#include "nn_mission_vendor.h"        // NnMissionVendor, G_MissionVendorNoMissionsReason
#include "nn_modal.h"                 // NnMainButtonOnModalOpen (FUN_0045a460)
#include "nn_story_star.h"            // StoryStarOnMissionMessage (FUN_004b9c60, slot 70)
#include "nn_trade.h"                 // G_NnTrade (DAT_006724b4)
#include "player_behavior.h"          // SetUnknownFloat673568 / 67356c (FUN_004c83a0/b0)
#include "player_spawn.h"             // player ids (DAT_00673344 / DAT_00673378)
#include "rpc_iclient_args.h"         // FServerSetReputationInfo (slot 66)
#include "server_connect.h"           // GetLocalServerInterface (DAT_0067ecd0)
#include "win_factory.h"              // WinFactoryCreate (FUN_0059db20)

#include <list>
#include <map>
#include <stdint.h>
#include <string>
#include <string.h>
#include <utility>

namespace
{
    // DAT_006724b0 -- XREF[3], read and written only by slots 107 / 108 (0x005a61f2,
    // 0x005a6232, 0x005a623f): one more for every trade request slot 107 announces (the +0x18c
    // bit 2 / FUN_0046b500 AddTradeRequest path), one less for every one slot 108 withdraws.
    // Both slots change it even when the ship is not found. Nothing reads it otherwise.
    int G_PendingTradeRequestCount = 0;

    // DAT_0067e770 -- a std::map<unsigned, std::wstring> (player id -> name), constructed by the
    // static initialiser LAB_005a6270 (FUN_005a67c0, atexit LAB_005a62a0 -> the tree teardown
    // LAB_005a62b0). XREF[4]: the ctor, the dtor and slots 110 / 111 (FUN_005a70b0 insert).
    // Nothing else reads it.
    std::map<unsigned, std::wstring> G_PlayerListNames;

    IServer* LocalServer()
    {
        return static_cast<IServer*>(GetLocalServerInterface());
    }

    // The four-byte float every GF / player-state slot receives as a dword.
    float DwordAsFloat(unsigned int Bits)
    {
        float Value = 0.0f;
        memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }
}

// Retail .text:005b0840 (the static initializer of .data:Client): the two vptrs and +0x08 = 0.
IClientImpl::IClientImpl()
    : Field_0x08(0)
{
}

// ---------------------------------------------------------------------------------------------
// Shared stubs
// ---------------------------------------------------------------------------------------------

// LAB_0052fba0: `MOV AL,1; RET 8` -- neither argument is read.
bool IClientImpl::Startup(unsigned int /*MaxPlayers*/, unsigned int /*Port*/)
{
    return true;
}

// FUN_00401e90: a bare `RET` (thiscall, no arguments).
void IClientImpl::Shutdown()
{
}

// LAB_0051ca40: `MOV EAX,1; RET`.
int IClientImpl::Update()
{
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Login forwards (.text:005a81f0..005a824f) on CLoginMgr DAT_0067e7b8
// ---------------------------------------------------------------------------------------------

// LAB_005a81f0: DAT_0067ecbf = 1, then JMP CLoginMgr::OnConnectResponse (FUN_005a9c10) with the
// argument in place (`RET 4` there).
void IClientImpl::OnConnect(unsigned int ClientId)
{
    G_bServerConnected = true;
    G_LoginMgr.OnConnectResponse(ClientId);
}

// LAB_005a8210: JMP CLoginMgr::OnLoginResponse (FUN_005aa660, `RET 8`); the second argument
// points at the response, whose first byte is the login status.
void IClientImpl::SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status)
{
    G_LoginMgr.OnLoginResponse(ClientId, &Status);
}

// LAB_005a8220 (`RET 8`): FUN_005aa9c0 on the connect context with the second argument -- the
// CCharacterSet the server side built (ch2 0x03 FLPACKET_SERVER_CHARACTERINFO). The first
// argument is not read.
void IClientImpl::SendServerCharacterInfo(unsigned int /*ClientId*/, const CCharacterSet& Characters)
{
    CharacterSelect_ReceiveCharacterInfo(Characters);
}

// LAB_005a8240 (`RET 8`): CLoginMgr::OnCharacterSelectResponse (FUN_005ab380) on the connect
// context with the second argument (the response); the first is not read. The CLoginMgr chain
// (NotifyCharacterSelectComplete) still reads the record as dwords, hence the cast.
void IClientImpl::SendServerCharSelectVerified(unsigned int /*ClientId*/,
                                               const FServerCharSelectVerifiedInfo& Info)
{
    G_LoginMgr.OnCharacterSelectResponse(reinterpret_cast<unsigned int const*>(&Info));
}

// ---------------------------------------------------------------------------------------------
// Player state (.text:005a5a60..)
// ---------------------------------------------------------------------------------------------

// LAB_005a5a60 (`RET 4`): nothing in single player; otherwise DAT_00673344 = 0 and, when
// DAT_0067ecbf is set, DAT_0067ecbe = 1. ClientId is not read.
void IClientImpl::DisconnectClient(unsigned int /*ClientId*/)
{
    if (SinglePlayer())
    {
        return;
    }
    SetPlayerConnectionId(0);
    if (G_bServerConnected)
    {
        G_bServerDisconnected = true;
    }
}

// ---------------------------------------------------------------------------------------------
// GF mirror forwards (.text:00437510..00437b30). ECX = DAT_00668708 is the LocationController.
// ---------------------------------------------------------------------------------------------

// LAB_00437510 (RET 8): FUN_004c3e20(ShipArchId) -- ch2 0x23 SETSHIPARCH.
void IClientImpl::SendServerSetShipArch(unsigned int /*ClientId*/, unsigned int ShipArchId)
{
    SetPlayerShipArchetypeIdRetail(ShipArchId);
}

// LAB_00437520 (RET 8): FUN_004c4f80(HullStatus) -- ch2 0x49 SETHULLSTATUS.
void IClientImpl::SendServerSetHullStatus(unsigned int /*ClientId*/, float HullStatus)
{
    SetPlayerHealthScaleRetail(HullStatus);
}

// LAB_00437530 (RET 8): `PUSH [ESP+8] ; CALL FUN_004c3f30 ; POP ECX ; RET 8` -- ch2 0x48
// SETCOLLISIONGROUPS, the second argument the address of a std::list<CollisionGroupDesc>
// (Server.dll &PlayerData::collision_groups; rpclocal DeliverSetCollisionGroups hands over a
// real std::list since W4-C).
void IClientImpl::SendServerSetCollisionGroups(unsigned int /*ClientId*/,
                                               const std::list<CollisionGroupDesc>& Groups)
{
    SyncPlayerCollisionGroupListRetail(Groups);
}

// FUN_00437540 (RET 8) -- ch2 0x24 SETEQUIPMENT: EquipDescList built from the EquipDescVector
// (Common ??0EquipDescList@@QAE@ABUEquipDescVector@@@Z, 0x00437563), FUN_004c4070(&it), then its
// std::list torn down (0x00437584..0x004375bb, the EH state at +0x1c).
void IClientImpl::SendServerSetEquipment(unsigned int /*ClientId*/, const EquipDescVector& Equipment)
{
    EquipDescList const Received(Equipment);
    SyncPlayerEquipDescListRetail(Received);
}

// LAB_004375e0 (RET 0xc): FUN_004c4170(AddResult, &Item) -- ch2 0x2e SETADDITEM; the callee
// reads only its second argument, the EquipDesc (0x004c4177).
void IClientImpl::SendServerSetAddItem(unsigned int /*ClientId*/, int AddResult, const EquipDesc& Item)
{
    AddPlayerEquipRetail(static_cast<unsigned char>(AddResult), &Item);
}

// LAB_00437600 (RET 0x10): FUN_004c4320(RemoveResult, ItemId, Count) -- ch2 0x2f SETREMOVEITEM.
// The first argument is never read by the callee.
void IClientImpl::SendServerSetRemoveItem(unsigned int /*ClientId*/, int RemoveResult, unsigned short ItemId,
                                          int Count)
{
    RemovePlayerEquipRetail(static_cast<unsigned char>(RemoveResult), ItemId, Count);
}

// LAB_00437620 (RET 0xc): LocationController FUN_0043a5a0(BaseId, RoomId) -- ch2 0x0d
// SETSTARTROOM.
void IClientImpl::SendServerSetStartRoom(unsigned int /*ClientId*/, unsigned int BaseId, unsigned int RoomId)
{
    SetClientLocationBaseStartLocation(BaseId, RoomId);
}

// LAB_004376e0 (RET 0xc): LocationController `CALL 0x0053aaa0` (RoomId, Key) -- Ghidra labels the
// target IDPMsgHandler::OnReceive; it is the shared `RET 8` stub (0x004376ef E8 AC331000 ->
// 0x0053aaa0). ch2 0x15 GFDESTROYCHARACTER therefore does nothing.
void IClientImpl::SendServerGFDestroyCharacter(unsigned int /*ClientId*/, unsigned int /*RoomId*/,
                                               unsigned int /*Key*/)
{
}

// FUN_00437640 (RET 0xc) -- ch2 0x26 GFUPDATECHAR: MarshalBuffer over (Data, Size),
// CharacterProperties unmarshalled from it, LocationController FUN_0043abf0(&props), then
// ~CharacterProperties.
void IClientImpl::SendServerGFUpdateChar(unsigned int /*ClientId*/, const unsigned char* Data,
                                         unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    CharacterProperties Properties;
    Properties.unmarshal(&Buffer);
    UpsertClientLocationRoomCharacter(Properties);
}

// LAB_00437700 (RET 8): LocationController FUN_0043ace0(RoomId) -- ch2 0x0f GFCOMPLETECHARLIST.
void IClientImpl::SendServerGFCompleteCharList(unsigned int /*ClientId*/, unsigned int RoomId)
{
    SetClientLocationRoomCharactersComplete(RoomId);
}

// FUN_00437720 (RET 0xc) -- ch2 0x1a GFSCRIPTBEHAVIOR: as GFUPDATECHAR with ScriptBehavior and
// FUN_0043ade0.
void IClientImpl::SendServerGFScriptBehavior(unsigned int /*ClientId*/, const unsigned char* Data,
                                             unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    ScriptBehavior Behavior;
    Behavior.unmarshal(&Buffer);
    UpsertClientLocationRoomScriptBehavior(Behavior);
}

// LAB_004377c0 (RET 0xc): LocationController FUN_0043ae70(RoomId, BehaviorId) -- ch2 0x17
// GFDESTROYSCRIPTBEHAVIOR.
void IClientImpl::SendServerGFDestroyScriptBehavior(unsigned int /*ClientId*/, unsigned int RoomId,
                                                    unsigned int BehaviorId)
{
    EraseClientLocationRoomScriptBehavior(RoomId, BehaviorId);
}

// LAB_004377e0 (RET 8): LocationController FUN_0043af80(RoomId) -- ch2 0x11
// GFCOMPLETESCRIPTBEHAVIORLIST.
void IClientImpl::SendServerGFCompleteScriptBehaviorList(unsigned int /*ClientId*/, unsigned int RoomId)
{
    SetClientLocationRoomScriptBehaviorsComplete(RoomId);
}

// FUN_00437800 (RET 0xc) -- ch2 0x1f GFUPDATEAMBIENTSCRIPT: as GFUPDATECHAR with
// AmbientScriptProperties and FUN_0043b080.
void IClientImpl::SendServerGFUpdateAmbientScript(unsigned int /*ClientId*/, const unsigned char* Data,
                                                  unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    AmbientScriptProperties Properties;
    Properties.unmarshal(&Buffer);
    UpsertClientLocationRoomAmbientScript(Properties);
}

// LAB_00437890 (RET 0xc): LocationController FUN_0043b110(RoomId, Key) -- ch2 0x19
// GFDESTROYAMBIENTSCRIPT.
void IClientImpl::SendServerGFDestroyAmbientScript(unsigned int /*ClientId*/, unsigned int RoomId,
                                                   unsigned int Key)
{
    EraseClientLocationRoomAmbientScript(RoomId, Key);
}

// LAB_004378b0 (RET 8): LocationController FUN_0043b220(RoomId) -- ch2 0x13
// GFCOMPLETEAMBIENTSCRIPTLIST.
void IClientImpl::SendServerGFCompleteAmbientScriptList(unsigned int /*ClientId*/, unsigned int RoomId)
{
    SetClientLocationRoomAmbientScriptsComplete(RoomId);
}

// LAB_004378d0 (RET 0xc): LocationController FUN_0043a9f0(BaseId, Key) -- ch2 0x16
// GFDESTROYMISSIONCOMPUTER: the base runtime's MissionVendorProperties erase.
void IClientImpl::SendServerGFDestroyMissionComputer(unsigned int /*ClientId*/, unsigned int BaseId,
                                                     unsigned int Key)
{
    EraseClientLocationBaseMissionOffer(BaseId, Key);
}

// FUN_004378f0 (RET 0xc) -- ch2 0x1d GFUPDATEMISSIONCOMPUTER: as GFUPDATECHAR with
// MissionVendorProperties and FUN_0043a960 (the base runtime's +0x00 bucket upsert).
void IClientImpl::SendServerGFUpdateMissionComputer(unsigned int /*ClientId*/, const unsigned char* Data,
                                                    unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    MissionVendorProperties Properties;
    Properties.unmarshal(&Buffer);
    UpsertClientLocationBaseMissionOffer(Properties);
}

// LAB_00437990 (RET 8): LocationController FUN_0043aaf0(BaseId) -- ch2 0x10
// GFCOMPLETEMISSIONCOMPUTERLIST, the base runtime's +0x14 byte.
void IClientImpl::SendServerGFCompleteMissionComputerList(unsigned int /*ClientId*/, unsigned int BaseId)
{
    SetClientLocationBaseMissionListComplete(BaseId);
}

// FUN_004379b0 (RET 0xc) -- ch2 0x20 GFMISSIONVENDORACCEPTANCE: MissionVendorAcceptance
// unmarshalled from (Data, Size), FUN_0048d4a0(&it) (cdecl), then its destructor.
void IClientImpl::SendServerGFMissionVendorAcceptance(unsigned int /*ClientId*/, const unsigned char* Data,
                                                      unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    MissionVendorAcceptance Acceptance;
    Acceptance.unmarshal(&Buffer);
    NnMissionVendor::OnAcceptResult(Acceptance);
}

// LAB_00437a40 (RET 8): DAT_00672038 = Reason -- ch2 0x5a GFMISSIONVENDORWHYEMPTY.
void IClientImpl::SendServerGFMissionVendorWhyEmpty(unsigned int /*ClientId*/, unsigned int Reason)
{
    G_MissionVendorNoMissionsReason = static_cast<int>(Reason);
}

// LAB_00437a50 (RET 0xc): LocationController FUN_0043a750(BaseId, Key) -- ch2 0x14
// GFDESTROYNEWSBROADCAST: the base runtime's NewsBroadcastProperties erase.
void IClientImpl::SendServerGFDestroyNewsBroadcast(unsigned int /*ClientId*/, unsigned int BaseId,
                                                   unsigned int Key)
{
    EraseClientLocationBaseNews(BaseId, Key);
}

// FUN_00437a70 (RET 0xc) -- ch2 0x1e GFUPDATENEWSBROADCAST: as GFUPDATECHAR with
// NewsBroadcastProperties and FUN_0043a6c0 (the base runtime's +0x18 bucket upsert).
void IClientImpl::SendServerGFUpdateNewsBroadcast(unsigned int /*ClientId*/, const unsigned char* Data,
                                                  unsigned int Size)
{
    MarshalBuffer Buffer;
    Buffer.use_read_buffer(Data, static_cast<int>(Size));
    NewsBroadcastProperties Properties;
    Properties.unmarshal(&Buffer);
    UpsertClientLocationBaseNews(Properties);
}

// LAB_00437b10 (RET 8): LocationController FUN_0043a860(BaseId) -- ch2 0x0e
// GFCOMPLETENEWSBROADCASTLIST, the base runtime's +0x2c byte.
void IClientImpl::SendServerGFCompleteNewsBroadcastList(unsigned int /*ClientId*/, unsigned int BaseId)
{
    SetClientLocationBaseNewsListComplete(BaseId);
}

// LAB_00437b30: `MOV ECX,[DAT_0067ecd0]; MOV EAX,[ECX]; JMP [EAX+0xec]` -- a tail call into the
// server interface's slot 59 (MissionSaveB) with this slot's own two arguments in place.
void IClientImpl::SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId)
{
    LocalServer()->MissionSaveB(ClientId, MissionId);
}

// ---------------------------------------------------------------------------------------------
// Reputation / mission / stats (.text:005a5a90..005a5fb0)
// ---------------------------------------------------------------------------------------------

// LAB_005a5ac0 (RET 8) -- ch2 0x29 SETREPUTATION.
//   multiplayer (SinglePlayer() false): Vibe::EnsureExists(info), Vibe::SetAttitudeTowardsPlayer(
//     info->VibeHandle, info->Attitude), Vibe::SetAffiliation(info, info+0x08, false);
//   single player: the display name is +0x48, or -- when NameB carries a mad-lib id (+0x34) --
//     NameB printed into a 0x18-char stack buffer (FUN_0041c970); the rank is read into the Info
//     argument's own stack slot (Vibe::GetRank), then Vibe::Set(info, +0x08, rank, (byte)+0x14,
//     +0x10, &NameA, &NameB, name).
void IClientImpl::SendServerSetReputation(unsigned int /*ClientId*/, const FServerSetReputationInfo& Info)
{
    if (!SinglePlayer())
    {
        Reputation::Vibe::EnsureExists(Info.VibeHandle);
        Reputation::Vibe::SetAttitudeTowardsPlayer(Info.VibeHandle, Info.Attitude);
        Reputation::Vibe::SetAffiliation(Info.VibeHandle, Info.Field_0x08, false);
        return;
    }
    wchar_t const* DisplayName = Info.DisplayName;
    wchar_t Printed[0x18];
    if (Info.NameB.MadLibId != 0)
    {
        FmtStrSprintf(&Info.NameB, Printed, 0x18);
        DisplayName = Printed;
    }
    // Retail quirk: Vibe::GetRank writes into the stack slot that held the Info pointer, so the
    // rank's initial value (kept when GetRank leaves it alone) is the pointer's own bits.
    unsigned Rank = static_cast<unsigned>(reinterpret_cast<uintptr_t>(&Info));
    Reputation::Vibe::GetRank(Info.VibeHandle, Rank);
    Reputation::Vibe::Set(Info.VibeHandle, Info.Field_0x08, Rank, Info.RelationCount,
                          Info.Relations, Info.NameA, Info.NameB, DisplayName);
}

// LAB_005a5a90 (RET 8) -- ch2 0x2a ADJUSTATTITUDE, Info = {group, delta}: Vibe::AdjustAttitude(
// info->group, FUN_004c4670() (the player's vibe, DAT_0061226c, in a stack local), info->delta).
// The group goes by reference to the record's own +0x00 (retail pushes its address), hence the
// int view of that unsigned field.
void IClientImpl::SendServerAdjustAttitude(unsigned int /*ClientId*/, const FPayloadServerAdjustAttitude& Info)
{
    int const PlayerVibe = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670 -> DAT_0061226c
    Reputation::Vibe::AdjustAttitude(*reinterpret_cast<int const*>(&Info.Field_0x00), PlayerVibe,
                                     Info.Field_0x04);
}

// LAB_005a5ba0 (RET 8) -- ch2 0x45 SET_MISSION_MESSAGE: FUN_004b9c60(&Message.Text,
// Message.Type) -- the FmtStr at +0 and the message kind at +0x18 (0x005a5ba0..0x005a5ba9).
void IClientImpl::SendServerSetMissionMessage(const unsigned int& /*ClientId*/, const CSetMissionMessage& Message)
{
    StoryStarOnMissionMessage(&Message.Text, static_cast<int>(Message.Type));
}

// FUN_005a5b60 (RET 8) -- ch2 0x34 REPLACEMISSIONOBJECTIVE, body in best_path.cpp.
void IClientImpl::SendServerReplaceMissionObjective(const unsigned int& /*ClientId*/,
                                                    const FServerReplaceMissionObjectiveInfo& Info)
{
    BestPathReplaceObjective(&Info);
}

// LAB_005a5bc0 (RET 8) -- ch2 0x35 SETMISSIONOBJECTIVES, body in best_path.cpp.
void IClientImpl::SendServerSetMissionObjectives(const unsigned int& /*ClientId*/,
                                                 const FServerSetMissionObjectivesInfo& Info)
{
    BestPathSetObjectives(&Info);
}

// LAB_005a5e80 (RET 8) -- ch2 0x33 SETMISSIONOBJECTIVESTATE, body in best_path.cpp.
void IClientImpl::SendServerSetMissionObjectiveState(const unsigned int& /*ClientId*/,
                                                     const FServerMissionObjectiveStateInfo& Info)
{
    BestPathSetObjectiveState(&Info);
}

// FUN_005a5ed0 (RET 0xc) -- ch2 0x46 MARKOBJ: the object in bucket 1, else 2, else 7
// (FUN_005416c0); when found, FUN_00547550(DAT_00673344, MarkType) on it -- the watch-list flag.
void IClientImpl::SendServerMarkObj(const unsigned int& /*ClientId*/, unsigned int ObjectId, int MarkType)
{
    FClientGameobj* Found = ClientObjDb_FindObject(ObjectId, 1);
    if (Found == nullptr)
    {
        Found = ClientObjDb_FindObject(ObjectId, 2);
        if (Found == nullptr)
        {
            Found = ClientObjDb_FindObject(ObjectId, 7);
            if (Found == nullptr)
            {
                return;
            }
        }
    }
    static_cast<FClientSimpleGameobj*>(Found)->SetWatchListed(
        static_cast<int>(GetPlayerConnectionId()), MarkType);
}

// LAB_005a5f20 (RET 8) -- ch2 0x47 CFGINTERFACENOTIFICATION, Info = {id, float}: the id against
// MakeId("target_window") and MakeId("status_window") (each zero-extended from its word); the
// first sets .data:00673568 (FUN_004c83a0), the second .data:0067356c (FUN_004c83b0). The
// exe reads Field_0x04 as float bits (TODO(type) in rpc_iclient_args.h); Field_0x08 is unread.
void IClientImpl::SendServerCfgInterfaceNotification(const unsigned int& /*ClientId*/,
                                                     const FPayloadServerCfgInterfaceNotification& Info)
{
    unsigned int const TargetWindow = MakeId("target_window");
    unsigned int const StatusWindow = MakeId("status_window");
    if (Info.Field_0x00 == TargetWindow)
    {
        SetUnknownFloat673568(DwordAsFloat(Info.Field_0x04));
        return;
    }
    if (Info.Field_0x00 == StatusWindow)
    {
        SetUnknownFloat67356c(DwordAsFloat(Info.Field_0x04));
    }
}

// LAB_005a5f70 (RET 8): FUN_004c4cf0(Cash, 0) -- ch2 0x30 SETCASH.
void IClientImpl::SendServerSetCash(const unsigned int& /*ClientId*/, int Cash)
{
    SetPlayerCreditsRetail(static_cast<unsigned>(Cash), 0);
}

// LAB_005a5f90 (RET 8): FUN_004c4d70(NumKills) -- ch2 0x3e SETNUMKILLS.
void IClientImpl::SendServerSetNumKills(const unsigned int& /*ClientId*/, int NumKills)
{
    SetPlayerNumKillsRetail(static_cast<unsigned>(NumKills));
}

// LAB_005a5fa0 (RET 8): FUN_004c4d80(NumSuccesses) -- ch2 0x3f SETMISSIONSUCCESSES.
void IClientImpl::SendServerSetMissionSuccesses(const unsigned int& /*ClientId*/, int NumSuccesses)
{
    SetPlayerMissionSuccessesRetail(static_cast<unsigned>(NumSuccesses));
}

// LAB_005a5fb0 (RET 8): FUN_004c4d90(NumFailures) -- ch2 0x40 SETMISSIONFAILURES.
void IClientImpl::SendServerSetMissionFailures(const unsigned int& /*ClientId*/, int NumFailures)
{
    SetPlayerMissionFailuresRetail(static_cast<unsigned>(NumFailures));
}

// ---------------------------------------------------------------------------------------------
// Player trade (.text:005a5fc0..005a61b3), FLPACKET_COMMON_PLAYER_TRADE on NN_Trade
// ---------------------------------------------------------------------------------------------

// LAB_005a5fc0 (RET 8): the NeuroNet modal close (FUN_0045a460), the trade window through its
// factory (FUN_0059db20("NN_Trade", "NN_Trade", 0, 0); the result is not kept -- the ctor stored
// DAT_006724b4), placed at {0, 0.35, -1.4} (0x3eb33333 / 0xbfb33333, slot 5), shown
// (+0x6c |= 3), aimed at the other ship (FUN_004c2510), and the other ship's pending trade request
// dropped from NN_GroupControl (FUN_0046b520). The window pointer is not tested.
void IClientImpl::SendCommonPlayerTradeInitiateTrade(unsigned int /*ClientId*/, unsigned int ShipId)
{
    NnMainButtonOnModalOpen(); // FUN_0045a460
    WinFactoryCreate("NN_Trade", "NN_Trade", nullptr, nullptr);
    float const Where[3] = {0.0f, 0.35f, -1.4f};
    G_NnTrade->SetPosition(Where);
    G_NnTrade->Flags = static_cast<unsigned char>(G_NnTrade->Flags | 0x03);
    G_NnTrade->SetTarget(ShipId); // FUN_004c2510
    ChatGroupTradeRequestRemoved(ShipId); // FUN_0046b520
}

// LAB_005a6070 (RET 8). With the window open: a completed trade (Reason == 1) re-syncs the player's
// ship cargo with the player's equipment list -- the character's gameobj (bucket 1, narrowed to
// ManagerKind 1 with no null test after), its object (vt+0x150) narrowed to class 0x503
// (CEqObj, again not tested), CEqObj::sync_cargo(FUN_004c4060()); then the window closes itself
// (its 0xef02). Whether or not it was open, DAT_00674b00 = 0.
void IClientImpl::SendCommonPlayerTradeTerminateTrade(unsigned int /*ClientId*/, int Reason)
{
    if (G_NnTrade != nullptr)
    {
        if (Reason == 1)
        {
            FClientGameobj* const Found = ClientObjDb_FindObject(GetPlayerCharacterId(), 1);
            if (Found != nullptr)
            {
                FClientGameobj* const Ship = (Found->ManagerKind == 1) ? Found : nullptr;
                CObject* const Obj = Ship->GetObject();
                CEqObj* const Eq =
                    (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CEqObj*>(Obj)
                        : nullptr;
                Eq->sync_cargo(GetPlayerEquipDescListRetail());
            }
        }
        G_NnTrade->SetProperty(0xef02, 0, 0);
    }
    G_bTradeRequested = 0; // DAT_00674b00
}

// LAB_005a6040 (RET 0xc): the "accepted" text of whoever answered -- the other player's
// (FUN_004c2620) unless it is the local character (DAT_00673378), then the local player's
// (FUN_004c2690).
void IClientImpl::SendCommonPlayerTradeAcceptTrade(unsigned int /*ClientId*/, unsigned int ShipId, bool bAccepted)
{
    if (G_NnTrade == nullptr)
    {
        return;
    }
    if (ShipId != GetPlayerCharacterId())
    {
        G_NnTrade->SetTargetAccepted(static_cast<unsigned char>(bAccepted));
    }
    else
    {
        G_NnTrade->SetPlayerAccepted(static_cast<unsigned char>(bAccepted));
    }
}

// LAB_005a6100 (RET 0xc): the other player's money (FUN_004c2700) unless the change is the local
// character's own; either way both "accepted" texts drop back to red (FUN_004c2620(0),
// FUN_004c2690(0)).
void IClientImpl::SendCommonPlayerTradeSetTradeMoney(unsigned int /*ClientId*/, unsigned int ShipId,
                                                     unsigned long Money)
{
    if (G_NnTrade == nullptr)
    {
        return;
    }
    if (ShipId != GetPlayerCharacterId())
    {
        G_NnTrade->SetTargetMoney(static_cast<int>(Money));
    }
    G_NnTrade->SetTargetAccepted(0);
    G_NnTrade->SetPlayerAccepted(0);
}

// LAB_005a6140 (RET 0xc): as SetTradeMoney with FUN_004c2220 (the other player offered the item).
void IClientImpl::SendCommonPlayerTradeAddTradeEquip(unsigned int /*ClientId*/, unsigned int ShipId,
                                                     const EquipDesc& Item)
{
    if (G_NnTrade == nullptr)
    {
        return;
    }
    if (ShipId != GetPlayerCharacterId())
    {
        G_NnTrade->AddTargetItem(&Item);
    }
    G_NnTrade->SetTargetAccepted(0);
    G_NnTrade->SetPlayerAccepted(0);
}

// LAB_005a6180 (RET 0xc): as SetTradeMoney with FUN_004c2380 (the other player took the item
// back).
void IClientImpl::SendCommonPlayerTradeDelTradeEquip(unsigned int /*ClientId*/, unsigned int ShipId,
                                                     const EquipDesc& Item)
{
    if (G_NnTrade == nullptr)
    {
        return;
    }
    if (ShipId != GetPlayerCharacterId())
    {
        G_NnTrade->RemoveTargetItem(&Item);
    }
    G_NnTrade->SetTargetAccepted(0);
    G_NnTrade->SetPlayerAccepted(0);
}

// FUN_005a61c0 (RET 8): the ship ShipId (bucket 1) -- when there is one and its ManagerKind
// (+0x1c) is 1 -- gets +0x18c bit 2 and FUN_0046b500(ShipId), the trade request from that ship
// queued on
// NN_GroupControl (NnGroupControl::AddTradeRequest FUN_004632f0); then DAT_006724b0 is
// incremented either way. Since FUN_0046b500 is the trade-request path, bit 2 marks a ship with a
// pending trade request (the earlier "in the local player's group" reading is not supported by
// the callee).
void IClientImpl::SendCommonPlayerTradeRequestTrade(unsigned int /*ClientId*/, unsigned int ShipId)
{
    FClientGameobj* const Found = ClientObjDb_FindObject(ShipId, 1);
    if (Found != nullptr && Found->ManagerKind == 1)
    {
        FClientShipGameobj* const Ship = static_cast<FClientShipGameobj*>(Found);
        Ship->Field_0x18c = static_cast<unsigned char>(Ship->Field_0x18c | 0x04);
        ChatGroupTradeRequestReceived(ShipId); // FUN_0046b500
    }
    ++G_PendingTradeRequestCount;
}

// FUN_005a6200 (RET 8): the mirror of RequestTrade -- bit 2 cleared and FUN_0046b520(ShipId) (the
// ship's pending trade request dropped); then DAT_00674b00 = 0 and DAT_006724b0 decremented.
void IClientImpl::SendCommonPlayerTradeStopTradeRequest(unsigned int /*ClientId*/, unsigned int ShipId)
{
    FClientGameobj* const Found = ClientObjDb_FindObject(ShipId, 1);
    if (Found != nullptr && Found->ManagerKind == 1)
    {
        FClientShipGameobj* const Ship = static_cast<FClientShipGameobj*>(Found);
        Ship->Field_0x18c = static_cast<unsigned char>(Ship->Field_0x18c & 0xfb);
        ChatGroupTradeRequestRemoved(ShipId); // FUN_0046b520
    }
    G_bTradeRequested = 0; // DAT_00674b00
    --G_PendingTradeRequestCount;
}

// FUN_005a6380 (RET 0x10) -- ch2 0x52 PLAYERLIST (add): the name, the player id, a byte flag.
void IClientImpl::SendServerPlayerListAdd(unsigned int /*ClientId*/, const wchar_t* Name, unsigned int PlayerId,
                                          bool bAlreadyPresent)
{
    // 0x005a63b6..0x005a63e6: wcslen(Name), _Grow + FUN_0041d8a0 (wmemcpy) + _Eos -- a copy.
    std::wstring const NameCopy(Name, wcslen(Name));
    // 0x005a63ec..0x005a644c: {PlayerId, L""} through FUN_005a70b0 (map insert, an existing
    // entry kept) and FUN_0046d660 (the pair's iterator) -> the node.
    std::map<unsigned, std::wstring>::iterator const It =
        G_PlayerListNames.insert(std::make_pair(static_cast<unsigned>(PlayerId), std::wstring())).first;
    // 0x005a6471..0x005a6483: the node's +0x10 string assigned from the copy.
    It->second = NameCopy;
    // 0x005a6489..0x005a6493: FUN_0046aa10(Name, PlayerId, (byte)bAlreadyPresent == 0) with the
    // caller's pointer.
    ChatPlayerJoined(Name, PlayerId, static_cast<unsigned char>(bAlreadyPresent) == 0);
}

// FUN_005a64f0 (RET 8) -- ch2 0x52 PLAYERLIST (remove).
void IClientImpl::SendServerPlayerListRemove(unsigned int /*ClientId*/, unsigned int PlayerId)
{
    // 0x005a650f..0x005a6567: {PlayerId, L""} through FUN_005a70b0 / FUN_0046d660 -- an insert,
    // so an unknown id gets an empty entry; nothing ever erases from DAT_0067e770.
    std::map<unsigned, std::wstring>::iterator const It =
        G_PlayerListNames.insert(std::make_pair(static_cast<unsigned>(PlayerId), std::wstring())).first;
    // 0x005a658f..0x005a65b4: the node's name copied into a local that nothing reads before
    // its destruction at 0x005a65ca.
    std::wstring const Departing(It->second);
    (void)Departing;
    // 0x005a65ba..0x005a65c5: FUN_0046aeb0(PlayerId, 1).
    ChatPlayerLeft(PlayerId, true);
}

// ---------------------------------------------------------------------------------------------
// IChatClient
// ---------------------------------------------------------------------------------------------

// IChatClient slot 0, LAB_005a6250 (RET 0x10) -- ch1 CHATMSG: FUN_0046a150(SenderId, Data,
// Size), i.e. the channel key, the buffer and its size. TargetId is not read.
void IClientImpl::SendCommonChatMsg(CHAT_ID /*TargetId*/, CHAT_ID SenderId, unsigned long Size,
                                    const void* Data)
{
    ChatDispatchIncoming(SenderId, Data, static_cast<unsigned>(Size));
}
