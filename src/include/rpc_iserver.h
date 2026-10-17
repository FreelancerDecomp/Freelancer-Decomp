// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_iserver.h -- rpclocal.dll's public ABI: IServerImpl, the Client / Server data
// exports and the four Get*Interface factories.
//
// Retail EXE\rpclocal.dll exports 113 names (dumpbin -exports), all of them the shape
// `__declspec(dllexport)` on a class produces, plus five C names:
//   1-6     ??0 / ??4 of IClient (rpc_iclient.h) and IServerImpl
//   7-9     ??_7IClient@@6B@, ??_7IServerImpl@@6BIChatServer@@@, ??_7IServerImpl@@6BIServer@@@
//   10-107  98 IServerImpl members: the 92 IServer slots, PushToServer (IServerImpl's own
//           virtual, slot 92), SubmitChat (IChatServer), and four
//           non-virtual ones (GetServerStats, HandleCharacterInfo, HandleDamageObject,
//           HandlePacket)
//   108     Client  (data, .data:0x06dbbcdc)      109  GetChatClientInterface (0xeec0)
//   110     GetChatServerInterface (0xf1d0)       111  GetClientInterface     (0xeeb0)
//   112     GetServerInterface     (0xf1c0)       113  Server  (data, .data:0x06dbbd28)
// Without a .def the linker numbers exports by name in ASCII order, which is the order
// above, so matching the NAME SET matches the ordinals too.
//
// Every declaration below is written so that it mangles to retail's decorated name --
// that is what fixes `struct` here (?..@IServerImpl@@QAE@ABU0@@Z: `U`), CHAT_ID, ServerStats,
// `class` CDAPacket / EquipDescList / CDAPCharacterInfo / CDAPDamageSpaceObj.
//
// Server.dll exports a different IServerImpl (server_iserverimpl.h), also used by
// RemoteClient. This proxy declaration is used by rpclocal and RemoteServer;
// the two IServerImpl declarations must never meet in one translation unit.
#pragma once

#include "common_server.h"
#include "rpc_iclient.h"        // RPCLOCAL_API, IClient

// Packet classes named only by the two HandleX exports; the port's packets are FPacketBase
// (src/include/rpc_packet.h), so these stay incomplete.
class CDAPCharacterInfo;
class CDAPDamageSpaceObj;
// The packet-type registry (src/include/rpc_packet.h); only a pointer to it is part of
// IServerImpl's layout, so a forward declaration is enough here.
class FPacketSerializer;

// C4275: IServer / IChatServer are interfaces from common_server.h and are not exported
// themselves -- exactly as in retail, which exports no IServer / IChatServer symbol.
#pragma warning(push)
#pragma warning(disable : 4275)

// ===========================================================================
// IServerImpl -- retail .data:Server (0x06dbbd28): IServer vptr at +0x00
// (??_7IServerImpl@@6BIServer@@@, 0x06db3450), IChatServer vptr at +0x04
// (??_7IServerImpl@@6BIChatServer@@@, 0x06db3448), serializer pointer at +0x08 -- the
// static initializer LAB_06d8f1a0 writes exactly those three dwords.
// ===========================================================================
struct RPCLOCAL_API IServerImpl : public IServer, public IChatServer {
    // Retail exports ??0IServerImpl@@QAE@XZ (0x1110), ??0IServerImpl@@QAE@ABU0@@Z (0x1130)
    // and ??4IServerImpl@@QAEAAU0@ABU0@@Z (0x1160). User-declared so the compiler does not
    // also generate and export move members retail does not have.
    IServerImpl();
    IServerImpl(const IServerImpl& Other);
    IServerImpl& operator=(const IServerImpl& Other);

    // --- IServer, slot order (common_server.h) ---
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
    void SystemSwitchOutComplete(unsigned int GateObjectId, unsigned int SessionId) override;
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

    // --- IServerImpl's own virtual: slot 92 (+0x170, .rdata:06db35c0 -> 0x06d8f460) ---
    // IServer ends at slot 91 (Server.dll's IServer table .rdata:06d65d4c holds 92
    // _purecall entries), so this, the first virtual IServerImpl adds, lands at +0x170.
    virtual void PushToServer(CDAPacket* Packet);

    // --- IChatServer ---
    void SubmitChat(CHAT_ID SenderId, unsigned long MsgType, const void* MsgData, CHAT_ID TargetId, int SystemId) override;

    // --- non-virtual ("QAE") exports ---
    // ordinal 38, 0x06d8eae0: a bare RET 4 shared with OnConnect / SetActiveConnection.
    void GetServerStats(ServerStats& Stats);
    // ordinal 41, 0x06d84490: the FLPACKET_SERVER_CHARACTERINFO receiver; caseD_3 of
    // HandlePacket calls it at 06d81715 with (packet, real client, 1).
    bool HandleCharacterInfo(CDAPCharacterInfo* Packet, IClient* Client, unsigned int Arg);
    // ordinal 42, 0x06d84c60: the FLPACKET_SERVER_DAMAGEOBJECT receiver as a member.
    // HandlePacket's caseD_5 inlines the same logic instead of calling it.
    bool HandleDamageObject(CDAPDamageSpaceObj* Packet, IClient* Client, unsigned int Arg);
    // ordinal 43, 0x06d81170: the client-bound packet dispatcher.
    void HandlePacket(CDAPacket* Packet);

    // +0x08 -- the packet-type registry. Zeroed by every constructor (06d81119,
    // 06d8f1a0), filled by Startup from FUN_06d95e50 (06d8f2c9 MOV [EBP+0x8],EAX), read
    // untested by every serializing slot (e.g. 06d8d489 MOV ECX,[ESI+0x8] ahead of
    // CALL FUN_06d96300) and released by Shutdown (06d8f35d / 06d8f360). The copy
    // constructor and operator= copy the pointer as it is (06d81143 / 06d81166), no AddRef.
    FPacketSerializer* Serializer;
};

#pragma warning(pop)

// ===========================================================================
// Data exports. Retail's names are the undecorated C names `Client` and `Server`, so
// they are extern "C" -- which also means they cannot carry the G prefix.
//   Server (ordinal 113) -- the IServerImpl above; GetServerInterface (0x06d8f1c0) is
//          MOV EAX,.data:Server / RET, GetChatServerInterface (0x06d8f1d0) returns
//          .data:0x06dbbd2c == Server + 4.
//   Client (ordinal 108) -- rpclocal's IClient proxy (src/rpclocal/rpc_client_proxy.cpp);
//          its concrete class is private to that file, so it is declared there, not here.
// ===========================================================================
// RemoteServer defines its concrete network subclass in its owning translation unit.
// Other consumers need only the IServerImpl prefix of this C data export.
#if !defined(REMOTESERVER_INSTANCE_DEFINITION)
extern "C" RPCLOCAL_API IServerImpl Server;
#endif

// ===========================================================================
// Factories, __cdecl, undecorated. common_server.h already declares GetServerInterface /
// GetChatServerInterface as SERVER_API (Server.dll exports the same two names), which is
// dllimport inside rpclocal; redeclaring them dllexport here raises C4273 and the
// dllexport wins -- that is the intent, rpclocal defines its own.
// ===========================================================================
#pragma warning(push)
#pragma warning(disable : 4273)
extern "C" {
    RPCLOCAL_API IServer* __cdecl GetServerInterface();           // ordinal 112, 0x06d8f1c0
    RPCLOCAL_API void* __cdecl GetChatServerInterface();          // ordinal 110, 0x06d8f1d0
#if !defined(REMOTESERVER_EXPORTS)
    RPCLOCAL_API IClient* __cdecl GetClientInterface();           // ordinal 111, 0x06d8eeb0
    RPCLOCAL_API void* __cdecl GetChatClientInterface();          // ordinal 109, 0x06d8eec0
#endif
}
#pragma warning(pop)
