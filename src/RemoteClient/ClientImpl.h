// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "../include/rpc_iclient.h"

class CClientImpl : public IClient, public IChatClient {
public:
    CClientImpl();
    ~CClientImpl();

    class FPacketSerializer* Serializer;
    class CDPServer* Host;
    class CDPClientProxy* ClientProxies[128];
    int NumClients;
    unsigned int Field_0x214;
    bool bStarted;

    CDPClientProxy* FindProxy(unsigned int ClientId) const;

    virtual void SendCommonFireWeapon(unsigned int ClientId, const XFireWeaponInfo& Info) override;
    virtual void SendCommonActivateEquip(unsigned int ClientId, const XActivateEquip& Info) override;
    virtual void SendCommonActivateCruise(unsigned int ClientId, const XActivateCruise& Info) override;
    virtual void SendCommonActivateThrusters(unsigned int ClientId, const XActivateThrusters& Info) override;
    virtual void SendCommonSetTarget(unsigned int ClientId, const XSetTarget& Info) override;
    virtual void SendCommonTractorObjects(unsigned int ClientId, const XTractorObjects& Info) override;
    virtual void SendCommonGoTradeLane(unsigned int ClientId, const XGoTradelane& Info) override;
    virtual void SendCommonStopTradeLane(unsigned int ClientId, unsigned int ShipId,
                                         unsigned int TradelaneRing1, unsigned int TradelaneRing2) override;
    virtual void SendCommonJettisonCargo(unsigned int ClientId, const XJettisonCargo& Info) override;
    virtual void PushToClient(unsigned int ClientId, CDAPacket* Packet) override;
    virtual bool Startup(unsigned int MaxPlayers, unsigned int Port) override;
    virtual void OnConnect(unsigned int ClientId) override;
    virtual void SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status) override;
    virtual void SendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters) override;
    virtual void SendServerCharSelectVerified(unsigned int ClientId, const FServerCharSelectVerifiedInfo& Info) override;
    virtual void Shutdown() override;
    virtual int Update() override;
    virtual void DisconnectClient(unsigned int ClientId) override;
    virtual unsigned int GetSendQSize(unsigned int ClientId) override;
    virtual unsigned int GetSendQBytes(unsigned int ClientId) override;
    virtual double GetLinkSaturation(unsigned int ClientId) override;
    virtual void SendServerSetShipArch(unsigned int ClientId, unsigned int ShipArchId) override;
    virtual void SendServerSetHullStatus(unsigned int ClientId, float HullStatus) override;
    virtual void SendServerSetCollisionGroups(unsigned int ClientId, const std::list<CollisionGroupDesc>& Groups) override;
    virtual void SendServerSetEquipment(unsigned int ClientId, const EquipDescVector& Equipment) override;
    virtual void SendServerSetCargo(unsigned int ClientId, const EquipDescVector& Cargo) override;
    virtual void SendServerSetAddItem(unsigned int ClientId, int AddResult, const EquipDesc& Item) override;
    virtual void SendServerSetRemoveItem(unsigned int ClientId, int RemoveResult, unsigned short ItemId, int Count) override;
    virtual void SendServerSetStartRoom(unsigned int ClientId, unsigned int BaseId, unsigned int RoomId) override;
    virtual void SendServerGFDestroyCharacter(unsigned int ClientId, unsigned int RoomId, unsigned int Key) override;
    virtual void SendServerGFUpdateChar(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFCompleteCharList(unsigned int ClientId, unsigned int RoomId) override;
    virtual void SendServerGFScriptBehavior(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFDestroyScriptBehavior(unsigned int ClientId, unsigned int RoomId, unsigned int BehaviorId) override;
    virtual void SendServerGFCompleteScriptBehaviorList(unsigned int ClientId, unsigned int RoomId) override;
    virtual void SendServerGFUpdateAmbientScript(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFDestroyAmbientScript(unsigned int ClientId, unsigned int RoomId, unsigned int Key) override;
    virtual void SendServerGFCompleteAmbientScriptList(unsigned int ClientId, unsigned int RoomId) override;
    virtual void SendServerGFDestroyMissionComputer(unsigned int ClientId, unsigned int BaseId, unsigned int Key) override;
    virtual void SendServerGFUpdateMissionComputer(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFCompleteMissionComputerList(unsigned int ClientId, unsigned int BaseId) override;
    virtual void SendServerGFMissionVendorAcceptance(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFMissionVendorWhyEmpty(unsigned int ClientId, unsigned int Reason) override;
    virtual void SendServerGFDestroyNewsBroadcast(unsigned int ClientId, unsigned int BaseId, unsigned int Key) override;
    virtual void SendServerGFUpdateNewsBroadcast(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendServerGFCompleteNewsBroadcastList(unsigned int ClientId, unsigned int BaseId) override;
    virtual void SendServerCreateSolar(unsigned int ClientId, const CSolarCreateInfo& Info) override;
    virtual void SendServerCreateShip(unsigned int ClientId, const CShipCreateInfo& Info) override;
    virtual void SendServerCreateLoot(unsigned int ClientId, const FServerCreateLootRequest& Info) override;
    virtual void SendServerCreateMine(unsigned int ClientId, const FServerCreateProjectileInfo& Info) override;
    virtual void SendServerCreateGuided(const unsigned int& ClientId, const FServerCreateGuidedInfo& Info) override;
    virtual void SendServerCreateCounter(const unsigned int& ClientId, const FServerCreateProjectileInfo& Info) override;
    virtual void SendServerExplodeAsteroidMine(unsigned int ClientId, const FMineExplosion& Info) override;
    virtual void SendServerSetGuidedTarget(unsigned int ClientId, unsigned int GuidedId, unsigned int TargetId,
                                           unsigned short TargetSubObjId) override;
    virtual void SendCommonUpdateObject(unsigned int ClientId, const FCommonUpdateObjectInfo& Info) override;
    virtual void SendServerDestroyObject(unsigned int ClientId, const FServerDestroyObjectInfo& Info) override;
    virtual void SendServerActivateObject(unsigned int ClientId, const FPayloadServerActivateObject& Info) override;
    virtual void SendServerSystemSwitchOut(unsigned int ClientId, const FServerSystemSwitchOutInfo& Info) override;
    virtual void SendServerSystemSwitchIn(unsigned int ClientId, const FPayloadServerSystemSwitchIn& Info) override;
    virtual void SendServerLand(unsigned int ClientId, const FServerLandInfo& Info) override;
    virtual void SendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info) override;
    virtual void SendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId) override;
    virtual void SendServerRequestSpaceScript(unsigned int ClientId, const FServerRequestSpaceScriptInfo& Info) override;
    virtual void SendServerDamageObject(unsigned int ClientId, unsigned int ObjectId, const DamageList& Damage) override;
    virtual void SendServerItemTractored(unsigned int ClientId, const FPayloadServerItemTractored& Info) override;
    virtual void SendServerUseItem(unsigned int ClientId, const FPayloadServerUseItem& Info) override;
    virtual void SendServerSetReputation(unsigned int ClientId, const FServerSetReputationInfo& Info) override;
    virtual void SendServerAdjustAttitude(unsigned int ClientId, const FPayloadServerAdjustAttitude& Info) override;
    virtual void SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                    unsigned int VoiceId, Costume CommCostume, unsigned int CommType,
                                    const unsigned int* TextIds, int TextCount, unsigned int Mood,
                                    float Duration, bool bReceiverIsOwnShip) override;
    virtual void SendServerSendNNMessage(unsigned int ClientId, unsigned int MessageId) override;
    virtual void SendServerSetMissionMessage(const unsigned int& ClientId, const CSetMissionMessage& Message) override;
    virtual void SendServerReplaceMissionObjective(const unsigned int& ClientId,
                                                   const FServerReplaceMissionObjectiveInfo& Info) override;
    virtual void SendServerSetMissionObjectives(const unsigned int& ClientId,
                                                const FServerSetMissionObjectivesInfo& Info) override;
    virtual void SendServerSetMissionObjectiveState(const unsigned int& ClientId,
                                                    const FServerMissionObjectiveStateInfo& Info) override;
    virtual void SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId) override;
    virtual void SendServerMarkObj(const unsigned int& ClientId, unsigned int ObjectId, int MarkType) override;
    virtual void SendServerCfgInterfaceNotification(const unsigned int& ClientId,
                                                    const FPayloadServerCfgInterfaceNotification& Info) override;
    virtual void SendServerSetCash(const unsigned int& ClientId, int Cash) override;
    virtual void SendServerSetNumKills(const unsigned int& ClientId, int NumKills) override;
    virtual void SendServerSetMissionSuccesses(const unsigned int& ClientId, int NumSuccesses) override;
    virtual void SendServerSetMissionFailures(const unsigned int& ClientId, int NumFailures) override;
    virtual void SendServerLoadHint(const unsigned int& ClientId, BaseHint* Hint) override;
    virtual void SendServerSetGroupFeelings(unsigned int ClientId, const FServerSetGroupFeelingsInfo& Info) override;
    virtual void SendServerRequestReturned(const unsigned int& ClientId, unsigned int ShipId, int RequestType,
                                           int Response, int Extra) override;
    virtual void SendServerRepairObject(unsigned int ClientId, const FServerRepairObjectInfo& Info) override;
    virtual void SendServerRemoteObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                                   const EquipDescVector& Cargo, unsigned char Flag) override;
    virtual void SendServerObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                             const EquipDesc& Item) override;
    virtual void SendServerBurnFuse(const unsigned int& ClientId, const FServerBurnFuseInfo& Info) override;
    virtual void SendServerSetCamera(const unsigned int& ClientId, const FPayloadServerSetCamera& Info) override;
    virtual void SendServerRevertCamera(const unsigned int& ClientId) override;
    virtual void SendServerSetStoryCue(const unsigned int& ClientId, const unsigned int& CueId) override;
    virtual void SendCommonSetWeaponGroup(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonSetVisitedState(unsigned int ClientId, unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonRequestBestPath(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonRequestPlayerStats(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonPopUpDialog(unsigned int ClientId, const FmtStr& Title, const FmtStr& Text,
                                       unsigned int Buttons) override;
    virtual void SendCommonRequestGroupPositions(unsigned int ClientId, unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonSetMissionLog(unsigned int ClientId, unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonSetInterfaceState(unsigned int ClientId, unsigned char* Data, unsigned int Size) override;
    virtual void SendCommonRequestRankLevel(unsigned int ClientId, unsigned char* Data, unsigned int Size) override;
    virtual void SendServerSetDirective(const unsigned int& ClientId, const FPayloadServerSetDirective& Info) override;
    virtual void SendCommonPlayerTradeInitiateTrade(unsigned int ClientId, unsigned int ShipId) override;
    virtual void SendCommonPlayerTradeTerminateTrade(unsigned int ClientId, int Reason) override;
    virtual void SendCommonPlayerTradeAcceptTrade(unsigned int ClientId, unsigned int ShipId, bool bAccepted) override;
    virtual void SendCommonPlayerTradeSetTradeMoney(unsigned int ClientId, unsigned int ShipId, unsigned long Money) override;
    virtual void SendCommonPlayerTradeAddTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) override;
    virtual void SendCommonPlayerTradeDelTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) override;
    virtual void SendCommonPlayerTradeRequestTrade(unsigned int ClientId, unsigned int ShipId) override;
    virtual void SendCommonPlayerTradeStopTradeRequest(unsigned int ClientId, unsigned int ShipId) override;
    virtual void SendServerScanNotify(unsigned int ClientId, unsigned int ScannerId, unsigned int GoodId) override;
    virtual void SendServerPlayerListAdd(unsigned int ClientId, const wchar_t* Name, unsigned int PlayerId,
                                         bool bAlreadyPresent) override;
    virtual void SendServerPlayerListRemove(unsigned int ClientId, unsigned int PlayerId) override;
    virtual void SendServerPlayerListClear(unsigned int ClientId) override;
    virtual void SendServerMiscObjUpdateObjectGroup(unsigned int ClientId, unsigned int ObjectId, unsigned int GroupId) override;
    virtual void SendServerMiscObjUpdateMission(unsigned int ClientId, unsigned int MissionId, unsigned int MissionSetBy) override;
    virtual void SendServerMiscObjUpdateMotd(unsigned int ClientId, const std::wstring& Motd) override;
    virtual void SendServerMiscObjUpdateAffiliation(unsigned int ClientId, unsigned int ObjectId,
                                                    unsigned int Affiliation) override;
    virtual void SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank) override;
    virtual void SendServerMiscObjUpdatePlayerGroup(unsigned int ClientId, unsigned int PlayerId, unsigned int GroupId) override;
    virtual void SendServerMiscObjUpdateSystem(unsigned int ClientId, unsigned int PlayerId, unsigned int SystemId) override;
    virtual void SendServerMiscObjUpdateFormation(unsigned int ClientId, const std::vector<unsigned int>& MemberIds) override;
    virtual void SendServerFormationUpdate(unsigned int ClientId, unsigned int LeaderId, const Vector& Offset) override;
    virtual void SendServerSetMusic(unsigned int ClientId, unsigned int SpaceMusic, unsigned int DangerMusic,
                                    unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag,
                                    float Fade) override;
    virtual void SendServerCancelMusic(unsigned int ClientId) override;
    virtual void SendServerPlaySoundEffect(unsigned int ClientId, unsigned int SoundId) override;
    virtual void DumpPacketStats(const char* Name) override;
    virtual void SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) override;
};

extern "C" __declspec(dllexport) CClientImpl Client;

CDPServer* CreateRemoteClientHost();
