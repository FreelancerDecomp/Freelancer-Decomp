// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_iserver.h -- IServer (the 92-slot server interface), IChatServer, Server.dll's
// two C factories and its three chat-hook data exports. Split out of common_server.h (which includes this header at the spot the
// declarations used to occupy) so that a translation unit which must not see common_cobject.h
// -- FLServer.exe's MFC code, whose CObject clashes with Freelancer's -- can still name IServer.
// Closure: common_server_types.h (parameter structs, CHAT_ID, CHARACTER_ID, the EFLConnection /
// CommResult / InvincibilityReason enums, forward declarations of EquipDesc / EquipDescList /
// CollisionGroupDesc) and server_exports.h (SERVER_API).
#pragma once

#include "server_exports.h"
#include "common_server_types.h"
#include <list>

// ===========================================================================
// IServer — exactly 92 slots (+0x000 .. +0x16c).
//
// Ground truth (retail listings; IServer card in decomp-tracker):
//   * IServer's own abstract table, Server.dll .rdata:06d65d4c .. 06d65eb8 (COL at
//     06d65d48 -> 06d6c49c, TD IServer; stored by IServerImpl's copy ctor at 06cef586), holds
//     (0x16c / 4) + 1 = 92 entries, every one _purecall (LAB_06d5fff0).
//   * IServerImpl's primary table is one entry longer: Server.dll .rdata:06d65bcc ..
//     06d65d3c (COLs 06d65bc8 / 06d65d40 -> 93 entries), rpclocal.dll .rdata:06db3450 ..
//     06db35c0, byte-for-byte the same extent. The 93rd entry (+0x170) is
//     PushToServer, IServerImpl's OWN virtual -- it is not declared here, but by each
//     IServerImpl (src/include/server_iserverimpl.h, src/include/rpc_iserver.h).
//   * every name and parameter list comes from the module's own mangled export
//     comment; every arity was re-checked against the callee's RET n (n / 4 =
//     dword arguments after `this`), and all agree.
//
// The old slots 93..101 (UnknownSlot93, GetServerStats, SwapConnections,
// ReqCargo, SetActiveConnection, PushToServer, TradeAbort, GetServerStats2,
// NewCharacterInfoReq) do not exist in retail: they were duplicates of slots
// 13/14/21/29 and of IServerImpl's own PushToServer, invented while /OPT:ICF folding
// hid those five bodies.
// GetServerStats is not virtual either — ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z
// carries "QAE" (public, non-virtual); it is exported but absent from both vtables.
//
// ARGUMENT ORDER is taken from the retail bodies, not guessed. Retail's habit is
// "payload first, session id last", but it is NOT universal (InterfaceItemUsed,
// RTCDone, AbortMission, PopUpDialog, InitiateTrade, RequestTrade, StopTradelane,
// MissionSaveB, SetWeaponGroup take the session FIRST). Each such case is settled
// by which argument rpclocal's serializer drops — the session id is never
// serialized, since in single player it is implicit.
// ===========================================================================
struct IServer {
    // Slot 0 (+0x000) — RET 8
    virtual void FireWeapon(unsigned int PlayerId, const XFireWeaponInfo& Info) = 0;

    // Slot 1 (+0x004) — RET 8
    virtual void ActivateEquip(unsigned int PlayerId, const XActivateEquip& Info) = 0;

    // Slot 2 (+0x008) — RET 8
    virtual void ActivateCruise(unsigned int PlayerId, const XActivateCruise& Info) = 0;

    // Slot 3 (+0x00c) — RET 8
    virtual void ActivateThrusters(unsigned int PlayerId, const XActivateThrusters& Info) = 0;

    // Slot 4 (+0x010) — RET 8
    virtual void SetTarget(unsigned int PlayerId, const XSetTarget& Info) = 0;

    // Slot 5 (+0x014) — RET 8
    virtual void TractorObjects(unsigned int PlayerId, const XTractorObjects& Info) = 0;

    // Slot 6 (+0x018) — RET 8
    virtual void GoTradelane(unsigned int PlayerId, const XGoTradelane& Info) = 0;

    // Slot 7 (+0x01c) — RET 16. rpclocal 0x06d8e770 serializes Arg1/Arg2/Arg3 only,
    // so SessionId is the FIRST argument here.
    // TODO: Arg1..Arg3 semantics unproven (tradelane ring pair plus a third dword).
    virtual void StopTradelane(unsigned int SessionId, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3) = 0;

    // Slot 8 (+0x020) — RET 8
    virtual void JettisonCargo(unsigned int PlayerId, const XJettisonCargo& Info) = 0;

    // Slot 9 (+0x024) — RET 4
    virtual bool Startup(const SStartupInfo& Info) = 0;

    // Slot 10 (+0x028) — RET 0
    virtual void Shutdown() = 0;

    // Slot 11 (+0x02c) — RET 0
    virtual int Update() = 0;

    // Slot 12 (+0x030) — RET 4
    virtual void ElapseTime(float Dt) = 0;

    // Slot 13 (+0x034) — RET 4. Was "AppendCharacterNames"; resolved by intersecting
    // the two modules' /OPT:ICF fold groups (Server 0x06ce1050 x rpclocal 0x06d8eae0).
    // Both bodies are a bare RET — a genuine retail no-op.
    virtual void SetActiveConnection(EFLConnection Conn) = 0;

    // Slot 14 (+0x038) — RET 8. Was "GetCharactersForAccount".
    // rpclocal 0x06d8f3f0 is MOV AL,1 / RET 8 — always true.
    virtual bool SwapConnections(EFLConnection ConnA, EFLConnection ConnB) = 0;

    // Slot 15 (+0x03c) — RET 8
    virtual void Connect(const char* ServerName, unsigned short* Port) = 0;

    // Slot 16 (+0x040) — RET 8
    virtual void DisConnect(unsigned int SessionId, EFLConnection Type) = 0;

    // Slot 17 (+0x044) — RET 4
    virtual void OnConnect(unsigned int SessionId) = 0;

    // Slot 18 (+0x048) — RET 8
    virtual void Login(const SLoginInfo& Info, unsigned int SessionId) = 0;

    // Slot 19 (+0x04c) — RET 8
    virtual void CharacterInfoReq(unsigned int SessionId, bool bFullInfo) = 0;

    // Slot 20 (+0x050) — RET 8
    virtual void CharacterSelect(const CHARACTER_ID& CharId, unsigned int SessionId) = 0;

    // Slot 21 (+0x054) — RET 4. Was "AppendCharacterNames2".
    virtual void NewCharacterInfoReq(unsigned int SessionId) = 0;

    // Slot 22 (+0x058) — RET 8
    virtual void CreateNewCharacter(const SCreateCharacterInfo& Info, unsigned int SessionId) = 0;

    // Slot 23 (+0x05c) — RET 8
    virtual void DestroyCharacter(const CHARACTER_ID& CharId, unsigned int SessionId) = 0;

    // Slot 24 (+0x060) — RET 4
    virtual void CharacterSkipAutosave(unsigned int SessionId) = 0;

    // Slot 25 (+0x064) — RET 8
    virtual void ReqShipArch(unsigned int ArchId, unsigned int SessionId) = 0;

    // Slot 26 (+0x068) — RET 8
    virtual void ReqHullStatus(float HullStatus, unsigned int SessionId) = 0;

    // Slot 27 (+0x06c) — RET 8
    virtual void ReqCollisionGroups(const std::list<CollisionGroupDesc>& Groups, unsigned int SessionId) = 0;

    // Slot 28 (+0x070) — RET 8
    virtual void ReqEquipment(const EquipDescList& Equipment, unsigned int SessionId) = 0;

    // Slot 29 (+0x074) — RET 8. Was "Connect2" (a fabricated duplicate of Connect).
    virtual void ReqCargo(const EquipDescList& Cargo, unsigned int SessionId) = 0;

    // Slot 30 (+0x078) — RET 24
    virtual void ReqAddItem(unsigned int ArchId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) = 0;

    // Slot 31 (+0x07c) — RET 12 (the ushort still occupies a full stack dword)
    virtual void ReqRemoveItem(unsigned short EquipId, int Count, unsigned int SessionId) = 0;

    // Slot 32 (+0x080) — RET 24
    virtual void ReqModifyItem(unsigned short EquipId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) = 0;

    // Slot 33 (+0x084) — RET 8. Cash is an int, not a float.
    virtual void ReqSetCash(int Cash, unsigned int SessionId) = 0;

    // Slot 34 (+0x088) — RET 8
    virtual void ReqChangeCash(int Delta, unsigned int SessionId) = 0;

    // Slot 35 (+0x08c) — RET 8. Server.dll 0x37630 validates param_2 as the session.
    virtual void BaseEnter(unsigned int BaseId, unsigned int SessionId) = 0;

    // Slot 36 (+0x090) — RET 8
    virtual void BaseExit(unsigned int BaseId, unsigned int SessionId) = 0;

    // Slot 37 (+0x094) — RET 8
    virtual void LocationEnter(unsigned int LocationId, unsigned int SessionId) = 0;

    // Slot 38 (+0x098) — RET 8
    virtual void LocationExit(unsigned int LocationId, unsigned int SessionId) = 0;

    // Slot 39 (+0x09c) — RET 12
    virtual void BaseInfoRequest(unsigned int BaseId, unsigned int SessionId, bool bFull) = 0;

    // Slot 40 (+0x0a0) — RET 12
    virtual void LocationInfoRequest(unsigned int LocationId, unsigned int SessionId, bool bFull) = 0;

    // Slot 41 (+0x0a4) — RET 8
    virtual void GFObjSelect(unsigned int ObjectId, unsigned int SessionId) = 0;

    // Slot 42 (+0x0a8) — RET 8
    virtual void GFGoodVaporized(const SGFGoodVaporizedInfo& Info, unsigned int SessionId) = 0;

    // Slot 43 (+0x0ac) — RET 16
    // TODO: Arg0 semantics unproven; Arg1 is the mission id, Arg2 the accept flag.
    virtual void MissionResponse(unsigned int Arg0, unsigned long MissionId, unsigned char bAccepted, unsigned int SessionId) = 0;

    // Slot 44 (+0x0b0) — RET 12. PBE => `unsigned char const *`.
    virtual void TradeResponse(const unsigned char* Data, int Size, unsigned int SessionId) = 0;

    // Slot 45 (+0x0b4) — RET 8
    virtual void GFGoodBuy(const SGFGoodBuyInfo& Info, unsigned int SessionId) = 0;

    // Slot 46 (+0x0b8) — RET 8
    virtual void GFGoodSell(const SGFGoodSellInfo& Info, unsigned int SessionId) = 0;

    // Slot 47 (+0x0bc) — RET 8. Arg0 is the player's SHIP, not the gate: Server.dll
    // .text:06cef67e looks it up and .text:06cef6b2 requires its CSimple+0xb4 owner to be
    // SessionId; the gate is the ship's +0x90 (.text:06cef6be).
    virtual void SystemSwitchOutComplete(unsigned int ShipObjectId, unsigned int SessionId) = 0;

    // Slot 48 (+0x0c0) — RET 8. Server.dll 0x0fb20 validates param_2 as the session.
    virtual void PlayerLaunch(unsigned int ShipId, unsigned int SessionId) = 0;

    // Slot 49 (+0x0c4) — RET 8. BOTH arguments are serialized (ch3 op 0x3e), so
    // neither is the implicit session id: Server.dll resolves the object from Arg1
    // and passes Arg0 to that object's sub-object slot +0x70.
    // TODO: Arg0 semantics unproven.
    virtual void LaunchComplete(unsigned int Arg0, unsigned int ObjectId) = 0;

    // Slot 50 (+0x0c8) — RET 8. Same shape as LaunchComplete (ch3 op 0x43, sub-object +0x74).
    // TODO: Arg0 semantics unproven.
    virtual void JumpInComplete(unsigned int Arg0, unsigned int ObjectId) = 0;

    // Slot 51 (+0x0cc) — RET 12. All three arguments are serialized (ch3 op 0x3f);
    // Server.dll resolves the object from Arg0.
    // TODO: Arg1/Arg2 semantics unproven.
    virtual void Hail(unsigned int ObjectId, unsigned int Arg1, unsigned int Arg2) = 0;

    // Slot 52 (+0x0d0) — RET 8
    virtual void SPObjUpdate(const SSPObjUpdateInfo& Info, unsigned int SessionId) = 0;

    // Slot 53 (+0x0d4) — RET 8
    virtual void SPMunitionCollision(const SSPMunitionCollisionInfo& Info, unsigned int SessionId) = 0;

    // Slot 54 (+0x0d8) — RET 8
    virtual void SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& Info, unsigned int SessionId) = 0;

    // Slot 55 (+0x0dc) — RET 8
    virtual void SPObjCollision(const SSPObjCollisionInfo& Info, unsigned int SessionId) = 0;

    // Slot 56 (+0x0e0) — RET 8
    virtual void SPRequestUseItem(const SSPUseItem& Info, unsigned int SessionId) = 0;

    // Slot 57 (+0x0e4) — RET 16
    virtual void SPRequestInvincibility(unsigned int ShipId, unsigned char bInvincible, InvincibilityReason Reason, unsigned int SessionId) = 0;

    // Slot 58 (+0x0e8) — RET 12. Retail PBG = VC6's `unsigned short const *`, a wide string.
    virtual void SaveGame(const CHARACTER_ID& CharId, const wchar_t* Description, unsigned int SessionId) = 0;

    // Slot 59 (+0x0ec) — RET 8. rpclocal 0x06d8db70 serializes only Arg1, so the
    // session id is the FIRST argument.
    virtual void MissionSaveB(unsigned int SessionId, unsigned long MissionId) = 0;

    // Slot 60 (+0x0f0) — RET 24
    virtual void RequestEvent(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3, unsigned long Arg4, unsigned int SessionId) = 0;

    // Slot 61 (+0x0f4) — RET 20
    virtual void RequestCancel(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned long Arg3, unsigned int SessionId) = 0;

    // Slot 62 (+0x0f8) — RET 24
    virtual void MineAsteroid(unsigned int SystemId, const Vector& Position, unsigned int LootId, unsigned int SecondaryLoot, unsigned int Quantity, unsigned int SessionId) = 0;

    // Slot 63 (+0x0fc) — RET 16
    virtual void CommComplete(unsigned int Arg0, unsigned int Arg1, unsigned int Arg2, CommResult Result) = 0;

    // Slot 64 (+0x100) — RET 4
    virtual void RequestCreateShip(unsigned int SessionId) = 0;

    // Slot 65 (+0x104) — RET 12. ABI0 = back-reference: (uint const&, uint const&, uint).
    virtual void SPScanCargo(const unsigned int& ScannerId, const unsigned int& TargetId, unsigned int SessionId) = 0;

    // Slot 66 (+0x108) — RET 8
    virtual void SetManeuver(unsigned int SessionId, const XSetManeuver& Info) = 0;

    // Slot 67 (+0x10c) — RET 8. rpclocal serializes Arg1 only => session is first.
    virtual void InterfaceItemUsed(unsigned int SessionId, unsigned int ItemId) = 0;

    // Slot 68 (+0x110) — RET 8. rpclocal serializes Arg1 only => session is first.
    virtual void AbortMission(unsigned int SessionId, unsigned int MissionId) = 0;

    // Slot 69 (+0x114) — RET 8. rpclocal serializes Arg1 only => session is first.
    virtual void RTCDone(unsigned int SessionId, unsigned int RtcId) = 0;

    // Slot 70 (+0x118) — RET 12
    virtual void SetWeaponGroup(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 71 (+0x11c) — RET 12
    virtual void SetVisitedState(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 72 (+0x120) — RET 12
    virtual void RequestBestPath(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 73 (+0x124) — RET 12
    virtual void RequestPlayerStats(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 74 (+0x128) — RET 8. rpclocal serializes Arg1 only => session is first.
    // Arg1 = the button the player answered a mission pop-up with: 1 yes, 2 no, 4 later,
    // 8 close/ok (freelancer.exe NeuroNetMissionMessage 0x0048affc / 0x0048b043 / 0x0048b08c /
    // 0x0048b0f0, and 4 when another panel takes the pop-up down, 0x0048ae2c) -- the same bits
    // Content's Cnd_PopUpDialog tests (YES 1 / NO 2 / LATER 4 / CLOSE 8).
    virtual void PopUpDialog(unsigned int SessionId, unsigned int Arg1) = 0;

    // Slot 75 (+0x12c) — RET 12
    virtual void RequestGroupPositions(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 76 (+0x130) — RET 12
    virtual void SetMissionLog(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 77 (+0x134) — RET 12
    virtual void SetInterfaceState(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 78 (+0x138) — RET 12
    virtual void RequestRankLevel(unsigned int SessionId, unsigned char* Data, int Size) = 0;

    // Slot 79 (+0x13c) — RET 8. rpclocal serializes Arg1 only => session is first.
    virtual void InitiateTrade(unsigned int SessionId, unsigned int TargetSessionId) = 0;

    // Slot 80 (+0x140) — RET 8
    virtual void TerminateTrade(unsigned int SessionId, int Reason) = 0;

    // Slot 81 (+0x144) — RET 8
    virtual void AcceptTrade(unsigned int SessionId, bool bAccepted) = 0;

    // Slot 82 (+0x148) — RET 8
    virtual void SetTradeMoney(unsigned int SessionId, unsigned long Money) = 0;

    // Slot 83 (+0x14c) — RET 8
    virtual void AddTradeEquip(unsigned int SessionId, const EquipDesc& Equip) = 0;

    // Slot 84 (+0x150) — RET 8
    virtual void DelTradeEquip(unsigned int SessionId, const EquipDesc& Equip) = 0;

    // Slot 85 (+0x154) — RET 8. rpclocal serializes Arg1 only => session is first.
    virtual void RequestTrade(unsigned int SessionId, unsigned int TargetSessionId) = 0;

    // Slot 86 (+0x158) — RET 4
    virtual void StopTradeRequest(unsigned int SessionId) = 0;

    // Slot 87 (+0x15c) — RET 8
    virtual void ReqDifficultyScale(float Scale, unsigned int SessionId) = 0;

    // Slot 88 (+0x160) — RET 0
    virtual unsigned int GetServerID() = 0;

    // Slot 89 (+0x164) — RET 0
    virtual const char* GetServerSig() = 0;

    // Slot 90 (+0x168) — RET 4. Takes the stats dump name, not a session id.
    virtual void DumpPacketStats(const char* Name) = 0;

    // Slot 91 (+0x16c) — RET 8. ABI0 = back-reference: (uint const&, uint const&).
    // rpclocal serializes *Arg0 only (ch3 op 0x24) => Arg1 is the session id.
    virtual void Dock(const unsigned int& BaseId, const unsigned int& SessionId) = 0;

    // No slot 92 here: IServer's table (.rdata:06d65d4c) ends at +0x16c. +0x170,
    // PushToServer(CDAPacket*) RET 4, is declared by each IServerImpl as its own virtual.

    // NOT a vtable slot: ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z is
    // public NON-virtual ("QAE"). It is an IServerImpl export only; declaring it
    // here would shift every following slot.
};

// IChatServer — chat interface (dual inheritance with IServer in IServerImpl)
// Retail vtable at ??_7IServerImpl@@6BIChatServer@@@
class IChatServer {
public:
    // Retail ?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@KPBX0H@Z (Server 0x06d191b0,
    // RET 0x14). The `unsigned long` is the BYTE SIZE of Data, a BinaryRDL buffer
    // (SubmitChat hands both to BinaryRDLReader::extract_text_from_buffer), not a
    // message type. SystemId is -1 when the caller has none (rpclocal's CHATMSG).
    virtual void SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId) = 0;
};

// Retail Server.dll exports ordinals 485-487, zero-initialised .data words
// (06d8f608 / 06d8f60c / 06d8f604) that nothing inside Server.dll ever writes: a
// host installs them. IServerImpl::SubmitChat calls them, cdecl, with the decoded
// chat text and a player or system id -- see src/Server/server_chat.cpp.
// FLServer.exe's chat dialogs install them (universe: .text:00418249..0041825a).
// Retail mangles the text as `unsigned short*` (VC6 wchar); the port uses wchar_t.
extern SERVER_API void (__cdecl* g_pPrivateChatHook)(wchar_t* Text, int PlayerId);
extern SERVER_API void (__cdecl* g_pSystemChatHook)(wchar_t* Text, int SystemId);
extern SERVER_API void (__cdecl* g_pUniverseChatHook)(wchar_t* Text, int PlayerId);

// Export functions (C API)
extern "C" {
    SERVER_API IServer* __cdecl GetServerInterface();
    SERVER_API void* __cdecl GetChatServerInterface();
}
