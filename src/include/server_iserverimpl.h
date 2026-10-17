// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_iserverimpl.h -- Server.dll's IServerImpl and its exported instance `Server`.
//
// Retail Server.dll exports the whole class (`??0IServerImpl@@`, `??4`, the two `??_7`
// vftables, every IServer override, PushToServer, SubmitChat, the non-virtual GetServerStats)
// and the object itself as the undecorated data export `Server`. FLServer.exe calls the
// members directly through its import table with ECX = &Server (Startup / Shutdown / Update /
// ElapseTime / GetServerStats / GetServerID / GetServerSig) or ECX = &Server + 4, the
// IChatServer subobject (SubmitChat from the chat dialogs).
//
// rpclocal.dll exports a DIFFERENT struct of the same name and layout prefix
// (src/include/rpc_iserver.h). The two never meet in one translation unit: this header is for
// Server.dll and its direct importers (FLServer.exe / RemoteClient.dll), rpc_iserver.h
// for the rpclocal / RemoteServer proxies.
// That is also why common_server.h does not include this header.
//
// Closure: server_iserver.h (IServer, IChatServer, SERVER_API) and common_server_types.h
// (ServerStats, the parameter structs). It must never reach common_server.h /
// common_cobject.h, so MFC translation units can include it.
#pragma once

#include "server_iserver.h"
#include "common_server_types.h"
#include <list>

// RemoteClient emits the inline members and tables; their virtual bodies still
// come from Server.dll. Data and factory exports keep the separate SERVER_API.
#if defined(SERVER_EXPORTS) || defined(REMOTECLIENT_EXPORTS)
#define SERVER_ISERVERIMPL_API __declspec(dllexport)
#else
#define SERVER_ISERVERIMPL_API __declspec(dllimport)
#endif

// C4275: IServer / IChatServer are not exported themselves -- as in retail, which exports no
// IServer / IChatServer symbol.
#pragma warning(push)
#pragma warning(disable : 4275)

// ===========================================================================
// IServerImpl -- retail .data:Server (06d8facc), TWELVE bytes: IServer vptr at +0x00
// (??_7IServerImpl@@6BIServer@@@, .rdata:06d65bcc, 93 entries -- IServer's 92 plus
// PushToServer), IChatServer vptr at +0x04 (??_7IServerImpl@@6BIChatServer@@@, one entry,
// SubmitChat), one dword at +0x08. Bodies: src/Server/IServerImpl.cpp.
// ===========================================================================
struct SERVER_ISERVERIMPL_API IServerImpl : public IServer, public IChatServer {
public:
    // --- IServer, slot order (server_iserver.h) ---
    void FireWeapon(unsigned int PlayerId, const XFireWeaponInfo& Info) override;
    void ActivateEquip(unsigned int PlayerId, const XActivateEquip& Info) override;
    void ActivateCruise(unsigned int PlayerId, const XActivateCruise& Info) override;
    void ActivateThrusters(unsigned int PlayerId, const XActivateThrusters& Info) override;
    void SetTarget(unsigned int PlayerId, const XSetTarget& Info) override;
    void TractorObjects(unsigned int PlayerId, const XTractorObjects& Info) override;
    void GoTradelane(unsigned int PlayerId, const XGoTradelane& Info) override;
    void StopTradelane(unsigned int SessionId, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3) override;
    void JettisonCargo(unsigned int PlayerId, const XJettisonCargo& Info) override;
    bool Startup(const SStartupInfo& Info) override;
    void Shutdown() override;
    int Update() override;
    void ElapseTime(float Dt) override;
    void SetActiveConnection(EFLConnection Conn) override;
    bool SwapConnections(EFLConnection ConnA, EFLConnection ConnB) override;
    void Connect(const char* ServerName, unsigned short* Port) override;
    void DisConnect(unsigned int SessionId, EFLConnection Type) override;
    void OnConnect(unsigned int SessionId) override;
    void Login(const SLoginInfo& Info, unsigned int SessionId) override;
    void CharacterInfoReq(unsigned int SessionId, bool bFullInfo) override;
    void CharacterSelect(const CHARACTER_ID& CharId, unsigned int SessionId) override;
    void NewCharacterInfoReq(unsigned int SessionId) override;
    void CreateNewCharacter(const SCreateCharacterInfo& Info, unsigned int SessionId) override;
    void DestroyCharacter(const CHARACTER_ID& CharId, unsigned int SessionId) override;
    void CharacterSkipAutosave(unsigned int SessionId) override;
    void ReqShipArch(unsigned int ArchId, unsigned int SessionId) override;
    void ReqHullStatus(float HullStatus, unsigned int SessionId) override;
    void ReqCollisionGroups(const std::list<CollisionGroupDesc>& Groups, unsigned int SessionId) override;
    void ReqEquipment(const EquipDescList& Equipment, unsigned int SessionId) override;
    void ReqCargo(const EquipDescList& Cargo, unsigned int SessionId) override;
    void ReqAddItem(unsigned int ArchId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) override;
    void ReqRemoveItem(unsigned short EquipId, int Count, unsigned int SessionId) override;
    void ReqModifyItem(unsigned short EquipId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) override;
    void ReqSetCash(int Cash, unsigned int SessionId) override;
    void ReqChangeCash(int Delta, unsigned int SessionId) override;
    void BaseEnter(unsigned int BaseId, unsigned int SessionId) override;
    void BaseExit(unsigned int BaseId, unsigned int SessionId) override;
    void LocationEnter(unsigned int LocationId, unsigned int SessionId) override;
    void LocationExit(unsigned int LocationId, unsigned int SessionId) override;
    void BaseInfoRequest(unsigned int BaseId, unsigned int SessionId, bool bFull) override;
    void LocationInfoRequest(unsigned int LocationId, unsigned int SessionId, bool bFull) override;
    void GFObjSelect(unsigned int ObjectId, unsigned int SessionId) override;
    void GFGoodVaporized(const SGFGoodVaporizedInfo& Info, unsigned int SessionId) override;
    void MissionResponse(unsigned int Arg0, unsigned long MissionId, unsigned char bAccepted, unsigned int SessionId) override;
    void TradeResponse(const unsigned char* Data, int Size, unsigned int SessionId) override;
    void GFGoodBuy(const SGFGoodBuyInfo& Info, unsigned int SessionId) override;
    void GFGoodSell(const SGFGoodSellInfo& Info, unsigned int SessionId) override;
    void SystemSwitchOutComplete(unsigned int ShipObjectId, unsigned int SessionId) override;
    void PlayerLaunch(unsigned int ShipId, unsigned int SessionId) override;
    void LaunchComplete(unsigned int Arg0, unsigned int ObjectId) override;
    void JumpInComplete(unsigned int Arg0, unsigned int ObjectId) override;
    void Hail(unsigned int ObjectId, unsigned int Arg1, unsigned int Arg2) override;
    void SPObjUpdate(const SSPObjUpdateInfo& Info, unsigned int SessionId) override;
    void SPMunitionCollision(const SSPMunitionCollisionInfo& Info, unsigned int SessionId) override;
    void SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& Info, unsigned int SessionId) override;
    void SPObjCollision(const SSPObjCollisionInfo& Info, unsigned int SessionId) override;
    void SPRequestUseItem(const SSPUseItem& Info, unsigned int SessionId) override;
    void SPRequestInvincibility(unsigned int ShipId, unsigned char bInvincible, InvincibilityReason Reason, unsigned int SessionId) override;
    void SaveGame(const CHARACTER_ID& CharId, const wchar_t* Description, unsigned int SessionId) override;
    void MissionSaveB(unsigned int SessionId, unsigned long MissionId) override;
    void RequestEvent(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3, unsigned long Arg4, unsigned int SessionId) override;
    void RequestCancel(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned long Arg3, unsigned int SessionId) override;
    void MineAsteroid(unsigned int SystemId, const Vector& Position, unsigned int LootId, unsigned int SecondaryLoot, unsigned int Quantity, unsigned int SessionId) override;
    void CommComplete(unsigned int Arg0, unsigned int Arg1, unsigned int Arg2, CommResult Result) override;
    void RequestCreateShip(unsigned int SessionId) override;
    void SPScanCargo(const unsigned int& ScannerId, const unsigned int& TargetId, unsigned int SessionId) override;
    void SetManeuver(unsigned int SessionId, const XSetManeuver& Info) override;
    void InterfaceItemUsed(unsigned int SessionId, unsigned int ItemId) override;
    void AbortMission(unsigned int SessionId, unsigned int MissionId) override;
    void RTCDone(unsigned int SessionId, unsigned int RtcId) override;
    void SetWeaponGroup(unsigned int SessionId, unsigned char* Data, int Size) override;
    void SetVisitedState(unsigned int SessionId, unsigned char* Data, int Size) override;
    void RequestBestPath(unsigned int SessionId, unsigned char* Data, int Size) override;
    void RequestPlayerStats(unsigned int SessionId, unsigned char* Data, int Size) override;
    void PopUpDialog(unsigned int SessionId, unsigned int Arg1) override;
    void RequestGroupPositions(unsigned int SessionId, unsigned char* Data, int Size) override;
    void SetMissionLog(unsigned int SessionId, unsigned char* Data, int Size) override;
    void SetInterfaceState(unsigned int SessionId, unsigned char* Data, int Size) override;
    void RequestRankLevel(unsigned int SessionId, unsigned char* Data, int Size) override;
    void InitiateTrade(unsigned int SessionId, unsigned int TargetSessionId) override;
    void TerminateTrade(unsigned int SessionId, int Reason) override;
    void AcceptTrade(unsigned int SessionId, bool bAccepted) override;
    void SetTradeMoney(unsigned int SessionId, unsigned long Money) override;
    void AddTradeEquip(unsigned int SessionId, const EquipDesc& Equip) override;
    void DelTradeEquip(unsigned int SessionId, const EquipDesc& Equip) override;
    void RequestTrade(unsigned int SessionId, unsigned int TargetSessionId) override;
    void StopTradeRequest(unsigned int SessionId) override;
    void ReqDifficultyScale(float Scale, unsigned int SessionId) override;
    unsigned int GetServerID() override;
    const char* GetServerSig() override;
    void DumpPacketStats(const char* Name) override;
    void Dock(const unsigned int& BaseId, const unsigned int& SessionId) override;

    // Slot 92 (+0x170, .rdata:06d65d3c): IServerImpl's OWN virtual, not an IServer override.
    virtual void PushToServer(CDAPacket* Packet);

    // --- IChatServer --- ?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@KPBX0H@Z (0x06d191b0, RET 0x14)
    void SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId) override;

    // ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z -- public NON-virtual
    // (.text:06d45af0..06d45b36).
    void GetServerStats(ServerStats& OutStats);

    // ??0IServerImpl@@QAE@XZ (0x06cef560), ??0IServerImpl@@QAE@ABU0@@Z (0x06cef580),
    // ??4IServerImpl@@QAEAAU0@ABU0@@Z (0x06cef5b0). User-declared so the compiler does not
    // also generate and export move members retail does not have.
    IServerImpl() : Field_0x8(0) {}
    IServerImpl(const IServerImpl& Other)
        : IServer(Other), IChatServer(Other), Field_0x8(Other.Field_0x8) {}
    IServerImpl& operator=(const IServerImpl& Other)
    {
        Field_0x8 = Other.Field_0x8;
        return *this;
    }

private:
    // The sole data member: default/global construction writes zero; the copy
    // constructor and assignment preserve its DWORD without interpreting it.
    // TODO: identify its game meaning; the special members do not establish it.
    unsigned int Field_0x8;
};

#pragma warning(pop)

// Server.dll's data export `Server` (undecorated, hence extern "C" and no G prefix).
extern "C" SERVER_API IServerImpl Server;
