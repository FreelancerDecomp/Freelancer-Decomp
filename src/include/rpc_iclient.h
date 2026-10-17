// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_iclient.h - IClient / IChatClient (the RPC client-side interfaces).
//
// Ground truth: retail EXE/rpclocal.dll (ImageBase 0x06d80000).
//   * the abstract interface vtable is the exported symbol ??_7IClient@@6B@ at
//     .rdata:0x06db3250 - exactly 126 purecall entries (0x06db3250..0x06db3444);
//   * rpclocal also ships its own concrete IClient at .data:Client (0x06dbbcdc),
//     whose vtable at .rdata:0x06db3978 has the very same 126 entries.
//   Two independent counts agree: IClient has 126 slots, +0x000..+0x1f4.
//
// Who exports GetClientInterface (re-checked on the listing in wave 2):
//   * freelancer.exe exports it. IServerImpl::Startup (0x06d8f1e0) strncpy()s the
//     first 0x104 bytes of SStartupInfo (the caller module path) to .data:0x06dbbbc0,
//     and IClient::Startup (slot 10, 0x06d8eed0) LoadLibraryA()s exactly that path to
//     obtain the real client interface (cached in .data:0x06dbbcd8).
//   * rpclocal.dll exports it too (ordinal 111, 0x06d8eeb0 -> &Client), and that is
//     the copy retail Server.dll receives: Startup copies the whole 0x214-byte
//     SStartupInfo onto the stack and then OVERWRITES its first 0x104 bytes with
//     GetModuleFileNameA(rpclocal HMODULE) before calling Server::Startup (+0x24).
//   Hence SStartupInfo.ModulePath sits at offset +0x000, size 0x104 - not at +0x104.
//
// Almost every IClient slot is a packet sender: it allocates a CDAPacket through
//   FUN_06d96300(Serializer /* this+0x08 */, Channel, Opcode, &OutPacket),
// fills the payload from +0x20 on and hands the packet to PushToClient (slot 9, +0x24).
// The Channel/Opcode pair and the FLPACKET_* name in each comment below come from the
// retail packet registry FUN_06d960a0(Serializer, Channel << 16 | Opcode,
// "FLPACKET_...", Factory); "size" is that packet class's operator new() size.
// See src/rpclocal/rpc_packet_registry.cpp for the whole registry (protocol: docs/re/rpclocal.md).
//
// NAMES. Retail never decorated the IClient member names (rpclocal exports only
// ??0IClient@@QAE@XZ, ??4IClient@@QAEAAV0@ABV0@@Z and ??_7IClient@@6B@), so every name
// here is OURS, built by one rule (2026-10-16):
//   * a packet sender is `Send` + the channel word of its FLPACKET_* registry string
//     (COMMON = ch1, SERVER = ch2) + the rest of that string in PascalCase:
//     FLPACKET_COMMON_FIREWEAPON -> SendCommonFireWeapon, FLPACKET_SERVER_SET_CAMERA ->
//     SendServerSetCamera. Run-together words are split where English splits them; the
//     abbreviations retail itself spells out elsewhere are kept as retail spells them
//     (GF as in IServerImpl::GFGoodBuy, NN as in pub::Player::SendNNMessage, Msg, Obj,
//     Arch, Resp, Cfg). When several slots send ONE packet type (PLAYER_TRADE,
//     PLAYERLIST, MISCOBJUPDATE) the packet name gets a suffix: for PLAYER_TRADE the name
//     of the retail-DECORATED IServerImpl method that sends the same sub-type the other
//     way (?InitiateTrade@IServerImpl@@ writes 0x01, ... ?StopTradeRequest@ writes 0x80);
//     for the other two the field the slot carries.
//   * the few non-senders are named after what the retail bodies do, mirroring the
//     decorated IServerImpl name of the same role where one exists (PushToServer ->
//     PushToClient, Startup, Shutdown, Update, OnConnect, DumpPacketStats) and DALib's
//     decorated CDPClientProxy names that RemoteClient.dll's bodies call (GetSendQSize,
//     GetSendQBytes, GetLinkSaturation, Disconnect).
// Each slot's comment keeps its old mechanical name as "(was SlotNN)".
//
// TYPES. The argument COUNT is exact: each callee's own RET n / 4 (a by-value Costume
// takes 13 of those dwords). Every argument type was re-derived from rpclocal's sender
// body, freelancer.exe's receiver and Server.dll's caller; the whole derivation table is
// D:\Temp\claude_scratch\iclient\TABLE.md (phase 1 of the 2026-10-16 rename) and the
// tracker's interface card. Conventions:
//   * ClientId is the connection/session id the packet goes to. rpclocal's PushToClient
//     never reads it; RemoteClient's hands it to DALib. Twenty-one senders take it BY
//     REFERENCE (`const unsigned int&`): their retail body dereferences it before calling
//     slot 9 (e.g. 06d8b23f MOV ECX,[EDX] in SendServerSetCash), and rpclocal's receiver
//     passes the address of a stack/static 1 for exactly those.
//   * a struct the callee always dereferences and never null-tests is `const T&`; the
//     callee only reads it. Pointers remain where retail tests for null (Packet, Name),
//     where a retail caller passes null (LoadHint's Hint), or the argument is an array
//     (TextIds) or a byte block whose ownership moves.
//   * byte blocks: `const unsigned char* Data, unsigned int Size` when the sender copies
//     or borrows them; `unsigned char* Data` when the packet TAKES the caller's
//     operator-new block over and frees it in its destructor (ch1 0x13/0x17/0x19/0x1a/0x1c,
//     see FPayloadCommonBuffer in src/rpclocal/rpc_payloads.h).
#pragma once

#include <list>
#include <string>
#include <vector>

// Retail mangles it ?PushToServer@IServerImpl@@UAEXPAVCDAPacket@@@Z -- `V`, a class.
class CDAPacket;

// Argument types. Every one is complete elsewhere; this header only names them, so the
// class-key of each declaration matches its definition.
class CCharacterSet;                        // server_characterset.h
class Vector;                               // common_ini.h
struct BaseHint;                            // common_goods.h (COMMON_API)
struct CHAT_ID;                             // common_server_types.h (by value)
struct CollisionGroupDesc;                  // common_cobject.h (COMMON_API)
struct Costume;                             // common_reputation.h (by value, 13 dwords)
struct CSetMissionMessage;                  // rpc_iclient_args.h
struct CShipCreateInfo;                     // server_createinfo.h
struct CSolarCreateInfo;                    // server_createinfo.h
struct DamageList;                          // common_misc.h (COMMON_API)
struct EquipDesc;                           // common_goods.h (COMMON_API)
struct EquipDescVector;                     // common_goods.h (COMMON_API)
struct FCommonUpdateObjectInfo;             // rpc_iclient_args.h
struct FMineExplosion;                      // server_sysdb.h
struct FmtStr;                              // common_text.h
struct FPayloadServerActivateObject;        // rpc_iclient_args.h
struct FPayloadServerItemTractored;         // rpc_iclient_args.h
struct FPayloadServerSetDirective;          // rpc_iclient_args.h
struct FPayloadServerSystemSwitchIn;        // rpc_iclient_args.h
struct FPayloadServerUseItem;               // rpc_iclient_args.h
struct FServerBurnFuseInfo;                 // rpc_iclient_args.h
struct FServerCharSelectVerifiedInfo;       // rpc_iclient_args.h
struct FServerCreateGuidedInfo;             // server_createinfo.h
struct FServerCreateLootRequest;            // server_createinfo.h
struct FServerCreateProjectileInfo;         // server_createinfo.h
struct FServerLandInfo;                     // rpc_iclient_args.h
struct FServerLaunchInfo;                   // rpc_iclient_args.h
struct FServerMissionObjectiveStateInfo;    // rpc_iclient_args.h
struct FServerReplaceMissionObjectiveInfo;  // rpc_iclient_args.h
struct FServerRequestSpaceScriptInfo;       // rpc_iclient_args.h
struct FServerSetGroupFeelingsInfo;         // rpc_iclient_args.h
struct FServerSetMissionObjectivesInfo;     // rpc_iclient_args.h
struct FServerSetReputationInfo;            // rpc_iclient_args.h
struct FServerSystemSwitchOutInfo;          // rpc_iclient_args.h
struct XActivateCruise;                     // common_server_types.h
struct XActivateEquip;                      // common_server_types.h
struct XActivateThrusters;                  // common_server_types.h
struct XFireWeaponInfo;                     // common_server_types.h
struct XGoTradelane;                        // common_server_types.h
struct XJettisonCargo;                      // common_server_types.h
struct XSetTarget;                          // common_server_types.h
struct XTractorObjects;                     // common_server_types.h
// rpc_iclient_args.h (moved out of rpclocal's private rpc_payloads.h):
struct FPayloadServerAdjustAttitude;        // slot 67
struct FPayloadServerCfgInterfaceNotification; // slot 76
struct FPayloadServerSetCamera;             // slot 88
struct FServerDestroyObjectInfo;            // slot 55
struct FServerRepairObjectInfo;             // slot 84

// ============================================================================
// Export macros for rpclocal.dll (src/include/rpc_iserver.h uses both).
//
// RPC_ICLIENT_API -- IClient is exported, never imported. Retail EXE\rpclocal.dll
// (ordinals 1, 2, 5, 7), EXE\Server.dll (9, 10, 28, 39) and EXE\freelancer.exe
// (9, 10, 22, 32) EACH export their own ??0IClient@@QAE@XZ, ??0IClient@@QAE@ABV0@@Z,
// ??4IClient@@QAEAAV0@ABV0@@Z and ??_7IClient@@6B@, and no retail module imports any
// of them. rpclocal's ??_7IClient@@6B@ (.rdata:0x06db3250) and freelancer.exe's
// (.rdata:0x005c9034) are each 126 entries of the module's own purecall thunk
// (0x06db1dd0 / 0x005b8064) -- the abstract class itself, emitted because it was
// declared dllexport in every module that compiled it.
//
// So RPC_ICLIENT_API is dllexport in exactly those three modules -- RPCLOCAL_EXPORTS,
// SERVER_EXPORTS, FREELANCER_EXPORTS (each set by its own .vcxproj) -- and plain
// everywhere else (Common.dll reaches this header through server_sysdb.h /
// server_basedb.h and exports no IClient). Never dllimport: each of the three defines its
// own copies of the three special members (src/rpclocal/rpc_client_proxy.cpp,
// src/Server/IServerImpl.cpp, src/freelancer/Client/client_exports.cpp) and none links
// another's import library, so a dllimport would be C2491 on those definitions. The
// vftable is exported from the TU whose constructor stamps it.
//
// RPCLOCAL_API -- rpclocal's own exports (IServerImpl, Client, Server, Get*Interface).
// rpc_iserver.h declares the proxy used by rpclocal and RemoteServer. RemoteClient
// uses Server.dll's canonical server_iserverimpl.h instead. Import-library linkage
// and dynamic Get*Interface lookup are separate from this proxy export macro.
// ============================================================================
#if defined(RPCLOCAL_EXPORTS) || defined(SERVER_EXPORTS) || defined(FREELANCER_EXPORTS) || defined(REMOTECLIENT_EXPORTS)
#define RPC_ICLIENT_API __declspec(dllexport)
#else
#define RPC_ICLIENT_API
#endif

#ifdef RPCLOCAL_EXPORTS
#define RPCLOCAL_API __declspec(dllexport)
#else
#define RPCLOCAL_API __declspec(dllimport)
#endif

// ============================================================================
// IClient - 126 slots, +0x000 .. +0x1f4
// ============================================================================
class RPC_ICLIENT_API IClient {
public:
    // Special members retail spells out: rpclocal exports ??0IClient@@QAE@XZ,
    // ??0IClient@@QAE@ABV0@@Z and ??4IClient@@QAEAAV0@ABV0@@Z next to ??_7IClient@@6B@.
    // They are user-declared (so no move members are generated and exported) and DEFINED
    // once per module that links IClient: src/rpclocal/rpc_client_proxy.cpp,
    // src/Server/IServerImpl.cpp, src/freelancer/Client/client_exports.cpp. The class is
    // abstract and carries no data, so all three are trivial.
    IClient();
    IClient(const IClient& Other);
    IClient& operator=(const IClient& Other);

    // ---- slots 0..8: the channel-1 mirror of IServer slots 0..8 (same X* records,
    //      retail-decorated there as ?FireWeapon@IServerImpl@@UAEXIABUXFireWeaponInfo@@@Z ...)

    // +0x000  0x06d8b310  ch1 op 0x02  FLPACKET_COMMON_FIREWEAPON (size 0x40)  (was Slot00)
    virtual void SendCommonFireWeapon(unsigned int ClientId, const XFireWeaponInfo& Info) = 0;
    // +0x004  0x06d8bbb0  ch1 op 0x08  FLPACKET_COMMON_ACTIVATEEQUIP (size 0x28)  (was Slot01)
    virtual void SendCommonActivateEquip(unsigned int ClientId, const XActivateEquip& Info) = 0;
    // +0x008  0x06d8bc10  ch1 op 0x0e  FLPACKET_COMMON_ACTIVATECRUISE (size 0x28)  (was Slot02)
    virtual void SendCommonActivateCruise(unsigned int ClientId, const XActivateCruise& Info) = 0;
    // +0x00c  0x06d8bc60  ch1 op 0x15  FLPACKET_COMMON_ACTIVATETHRUSTERS (size 0x28)  (was Slot03)
    virtual void SendCommonActivateThrusters(unsigned int ClientId, const XActivateThrusters& Info) = 0;
    // +0x010  0x06d8bd50  ch1 op 0x04  FLPACKET_COMMON_SETTARGET (size 0x2c)  (was Slot04)
    virtual void SendCommonSetTarget(unsigned int ClientId, const XSetTarget& Info) = 0;
    // +0x014  0x06d8be10  ch1 op 0x1d  FLPACKET_COMMON_TRACTOROBJECTS (size 0x38)  (was Slot05)
    virtual void SendCommonTractorObjects(unsigned int ClientId, const XTractorObjects& Info) = 0;
    // +0x018  0x06d8bcb0  ch1 op 0x0f  FLPACKET_COMMON_GOTRADELANE (size 0x30)  (was Slot06)
    virtual void SendCommonGoTradeLane(unsigned int ClientId, const XGoTradelane& Info) = 0;
    // +0x01c  0x06d8bd10  ch1 op 0x10  FLPACKET_COMMON_STOPTRADELANE (size 0x2c)  (was Slot07)
    // The sender serializes only ShipId (06d8bd2d..06d8bd31: [ESP+0x10] -> packet+0x20); the
    // two ring ids are what IServer::StopTradelane sends the other way and what the exe's
    // LAB_0053c760 reads (it ignores the third), so they keep the IServer meaning.
    virtual void SendCommonStopTradeLane(unsigned int ClientId, unsigned int ShipId,
                                         unsigned int TradelaneRing1, unsigned int TradelaneRing2) = 0;
    // +0x020  0x06d8bdb0  ch1 op 0x14  FLPACKET_COMMON_JETTISONCARGO (size 0x2c)  (was Slot08)
    virtual void SendCommonJettisonCargo(unsigned int ClientId, const XJettisonCargo& Info) = 0;

    // ---- non-senders

    // +0x024  0x06d8f080  (was Slot09)
    // The one outbound path every sender ends in: this->vt[+0x24](ClientId, Packet).
    // rpclocal: FUN_06d85e00(CDAConnection, Packet), the id unread. RemoteClient.dll
    // (06b39650): CDPClientProxy::Send to the proxy of ClientId, or CDPClientProxy::
    // Disconnect when Packet is null -- Server.dll sends a null packet on purpose
    // (06d48d33..06d48d3b). Named after its retail-decorated mirror
    // ?PushToServer@IServerImpl@@UAEXPAVCDAPacket@@@Z.
    virtual void PushToClient(unsigned int ClientId, CDAPacket* Packet) = 0;
    // +0x028  0x06d8eed0  (was Slot10)
    // rpclocal: LoadLibraryA(the module path IServerImpl::Startup saved at .data:0x06dbbbc0)
    // -> GetClientInterface / GetChatClientInterface -> forwards both of its own arguments
    // to the real IClient's Startup, then builds the packet serializer into this+0x08.
    // RemoteClient.dll (06b39750): CDPServer ctor, BeginHosting(Port), SetMaxPlayers.
    // Server.dll passes SStartupInfo +0x208 / +0x20c.
    virtual bool Startup(unsigned int MaxPlayers, unsigned int Port) = 0;
    // +0x02c  0x06d8eae0  (was Slot11)
    // 0x06d8eae0 - shared bare "RET 4" stub. The same code backs three exported rpclocal
    // names: ?GetServerStats@ (public NON-virtual, not a vtable entry anywhere),
    // ?OnConnect@ (IServer slot 17) and ?SetActiveConnection@ (IServer slot 13); the
    // IClient role is IServer::OnConnect's (?OnConnect@IServerImpl@@UAEXI@Z), hence the
    // name. Incoming ch2 op 0x01 FLPACKET_SERVER_CONNECTRESPONSE is delivered to THIS slot
    // by IServerImpl::HandlePacket; freelancer.exe's LAB_005a81f0 forwards the id to
    // CLoginMgr::OnConnectResponse.
    virtual void OnConnect(unsigned int ClientId) = 0;

    // +0x030  0x06d883a0  ch2 op 0x02  FLPACKET_SERVER_LOGINRESPONSE (size 0x24)  (was Slot12)
    // 06d883bd MOV DL,byte ptr [ECX] -- the status byte read THROUGH the argument; Server.dll
    // passes &PlayerDB::login's result (?login@PlayerDB@@QAEEABUSLoginInfo@@I@Z returns E).
    virtual void SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status) = 0;
    // +0x034  0x06d89890  ch2 op 0x03  FLPACKET_SERVER_CHARACTERINFO (size 0x30)  (was Slot13)
    virtual void SendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters) = 0;
    // +0x038  0x06d8a030  ch2 op 0x08  FLPACKET_SERVER_CHARSELECTVERIFIED (size 0x30)  (was Slot14)
    virtual void SendServerCharSelectVerified(unsigned int ClientId, const FServerCharSelectVerifiedInfo& Info) = 0;

    // +0x03c  0x06d8ef80  (was Slot15)
    // rpclocal: the real client's Shutdown, then FreeLibrary()s that module and clears the
    // cached interface pointers. RemoteClient.dll: 06b39980. Mirrors ?Shutdown@IServerImpl@@.
    virtual void Shutdown() = 0;
    // +0x040  0x06d8efc0  (was Slot16)
    // client-side pump. Creates the CDAConnection (FUN_06d88280), emits ch2 op 0x01
    // CONNECTRESPONSE once, then drains the client->server queue via FUN_06d86580 /
    // FUN_06d86a70. IServerImpl::Update calls it as FUN_06d8efc0(&Client); RemoteClient.dll's
    // 06b399d0 is CDPServer::DispatchMsgs. Mirrors ?Update@IServerImpl@@UAEHXZ.
    // Returns 1 on every path: rpclocal 06d8f05b MOV EAX,0x1, and freelancer.exe's own slot 16
    // (IClientImpl vtable .rdata:005e6674 + 0x40 -> LAB_0051ca40) is MOV EAX,0x1 / RET. Both known
    // callers discard it (rpclocal 06d8f448, Server.dll 06d45148).
    virtual int Update() = 0;
    // +0x044  0x06d8f070  (was Slot17)
    // rpclocal: clears DAT_06dbbd18 / DAT_06dbbd19 (the connection-established flags), the
    // id unread. RemoteClient.dll (06b39a00): CDPClientProxy::Disconnect on ClientId's proxy.
    virtual void DisconnectClient(unsigned int ClientId) = 0;
    // +0x048  0x06d8f0a0  (was Slot18)
    // rpclocal: XOR EAX,EAX; RET 4 - always 0. RemoteClient.dll (06b39d60): ClientId in
    // 1..0x80 -> ?GetSendQSize@CDPClientProxy@@QAEIXZ, else 0.
    virtual unsigned int GetSendQSize(unsigned int ClientId) = 0;
    // +0x04c  0x06d8f0a0  (was Slot19)
    // rpclocal: literally the same code as slot 18. RemoteClient.dll (06b39d90):
    // ?GetSendQBytes@CDPClientProxy@@QAEIXZ.
    virtual unsigned int GetSendQBytes(unsigned int ClientId) = 0;
    // +0x050  0x06d8f0b0  (was Slot20)
    // rpclocal: FLD [0.0]; RET 4. RemoteClient.dll (06b39dc0):
    // ?GetLinkSaturation@CDPClientProxy@@QAENXZ, else the 0.0 at .rdata:06b6bef8.
    virtual double GetLinkSaturation(unsigned int ClientId) = 0;

    // ---- channel 2: player state

    // +0x054  0x06d883e0  ch2 op 0x23  FLPACKET_SERVER_SETSHIPARCH (size 0x24)  (was Slot21)
    virtual void SendServerSetShipArch(unsigned int ClientId, unsigned int ShipArchId) = 0;
    // +0x058  0x06d88420  ch2 op 0x49  FLPACKET_SERVER_SETHULLSTATUS (size 0x24)  (was Slot22)
    // 06d8843d FLD float ptr [ESP+0x10] / FSTP float ptr [ECX+0x20].
    virtual void SendServerSetHullStatus(unsigned int ClientId, float HullStatus) = 0;
    // +0x05c  0x06d88460  ch2 op 0x48  FLPACKET_SERVER_SETCOLLISIONGROUPS (size 0x2c)  (was Slot23)
    // Server.dll pushes &PlayerData::collision_groups (06d4f400, slot +0x26c); rpclocal's
    // receiver passes `LEA EAX,[packet+0x20]`, the list inside the packet; freelancer.exe's
    // LAB_00437530 hands it to FUN_004c3f30 (std::list operator= into DAT_0067296c). Nobody
    // tests it for null. The mirror of ?ReqCollisionGroups@IServerImpl@@ (a reference).
    virtual void SendServerSetCollisionGroups(unsigned int ClientId, const std::list<CollisionGroupDesc>& Groups) = 0;
    // +0x060  0x06d884a0  ch2 op 0x24  FLPACKET_SERVER_SETEQUIPMENT (size 0x24)  (was Slot24)
    virtual void SendServerSetEquipment(unsigned int ClientId, const EquipDescVector& Equipment) = 0;
    // +0x064  0x06d88530  ch2 op 0x25  FLPACKET_SERVER_SETCARGO (size 0x24)  (was Slot25)
    virtual void SendServerSetCargo(unsigned int ClientId, const EquipDescVector& Cargo) = 0;
    // +0x068  0x06d885c0  ch2 op 0x2e  FLPACKET_SERVER_SETADDITEM (size 0x40)  (was Slot26)
    // AddResult is EquipDescList::add_equipment_item's return (1 = folded into an existing
    // item, 0 = appended), 06d8861c..06d88627 a dword to packet+0x2c.
    virtual void SendServerSetAddItem(unsigned int ClientId, int AddResult, const EquipDesc& Item) = 0;
    // +0x06c  0x06d88670  ch2 op 0x2f  FLPACKET_SERVER_SETREMOVEITEM (size 0x2c)  (was Slot27)
    // RemoveResult is EquipDescList::remove_equipment_item's (-1 / 0 / 1); ItemId is read as a
    // word (06d88691 MOVZX EAX,word ptr [ESP+0x14]).
    virtual void SendServerSetRemoveItem(unsigned int ClientId, int RemoveResult, unsigned short ItemId, int Count) = 0;

    // ---- channel 2: the GF (base / room) mirror

    // +0x070  0x06d886d0  ch2 op 0x0d  FLPACKET_SERVER_SETSTARTROOM (size 0x28)  (was Slot28)
    virtual void SendServerSetStartRoom(unsigned int ClientId, unsigned int BaseId, unsigned int RoomId) = 0;
    // +0x074  0x06d88720  ch2 op 0x15  FLPACKET_SERVER_GFDESTROYCHARACTER (size 0x28)  (was Slot29)
    virtual void SendServerGFDestroyCharacter(unsigned int ClientId, unsigned int RoomId, unsigned int Key) = 0;
    // +0x078  0x06d88770  ch2 op 0x26  FLPACKET_SERVER_GFUPDATECHAR (size 0x28)  (was Slot30)
    // Data/Size: a MarshalBuffer image (CharacterProperties); FUN_06d97b00 copies it.
    virtual void SendServerGFUpdateChar(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x07c  0x06d887c0  ch2 op 0x0f  FLPACKET_SERVER_GFCOMPLETECHARLIST (size 0x24)  (was Slot31)
    virtual void SendServerGFCompleteCharList(unsigned int ClientId, unsigned int RoomId) = 0;
    // +0x080  0x06d88800  ch2 op 0x1a  FLPACKET_SERVER_GFSCRIPTBEHAVIOR (size 0x28)  (was Slot32)
    // Data/Size: a MarshalBuffer image (ScriptBehavior); FUN_06d97b00 copies it.
    virtual void SendServerGFScriptBehavior(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x084  0x06d88850  ch2 op 0x17  FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR (size 0x28)  (was Slot33)
    virtual void SendServerGFDestroyScriptBehavior(unsigned int ClientId, unsigned int RoomId, unsigned int BehaviorId) = 0;
    // +0x088  0x06d888a0  ch2 op 0x11  FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST (size 0x24)  (was Slot34)
    virtual void SendServerGFCompleteScriptBehaviorList(unsigned int ClientId, unsigned int RoomId) = 0;
    // +0x08c  0x06d888e0  ch2 op 0x1f  FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT (size 0x28)  (was Slot35)
    // Data/Size: a MarshalBuffer image (AmbientScriptProperties); FUN_06d90a00 copies it.
    virtual void SendServerGFUpdateAmbientScript(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x090  0x06d88930  ch2 op 0x19  FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT (size 0x28)  (was Slot36)
    virtual void SendServerGFDestroyAmbientScript(unsigned int ClientId, unsigned int RoomId, unsigned int Key) = 0;
    // +0x094  0x06d88980  ch2 op 0x13  FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST (size 0x24)  (was Slot37)
    virtual void SendServerGFCompleteAmbientScriptList(unsigned int ClientId, unsigned int RoomId) = 0;
    // +0x098  0x06d889c0  ch2 op 0x16  FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER (size 0x28)  (was Slot38)
    virtual void SendServerGFDestroyMissionComputer(unsigned int ClientId, unsigned int BaseId, unsigned int Key) = 0;
    // +0x09c  0x06d88a10  ch2 op 0x1d  FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER (size 0x28)  (was Slot39)
    // Data/Size: a MarshalBuffer image (MissionVendorProperties); FUN_06d97b00 copies it.
    virtual void SendServerGFUpdateMissionComputer(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x0a0  0x06d88a60  ch2 op 0x10  FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST (size 0x24)  (was Slot40)
    virtual void SendServerGFCompleteMissionComputerList(unsigned int ClientId, unsigned int BaseId) = 0;
    // +0x0a4  0x06d88aa0  ch2 op 0x20  FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE (size 0x28)  (was Slot41)
    // Data/Size: a MarshalBuffer image (MissionVendorAcceptance); FUN_06d97b00 copies it.
    virtual void SendServerGFMissionVendorAcceptance(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x0a8  0x06d88af0  ch2 op 0x5a  FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY (size 0x24)  (was Slot42)
    // Reason is pub::GF::ReportWhyMissionVendorEmpty's MVEmptyReason (common_server.h), which
    // has no fixed underlying type and so cannot be forward-declared here; the sender keeps
    // its low byte (06d88b0d MOV DL,byte ptr [ESP+0x10]).
    virtual void SendServerGFMissionVendorWhyEmpty(unsigned int ClientId, unsigned int Reason) = 0;
    // +0x0ac  0x06d88b30  ch2 op 0x14  FLPACKET_SERVER_GFDESTROYNEWSBROADCAST (size 0x28)  (was Slot43)
    virtual void SendServerGFDestroyNewsBroadcast(unsigned int ClientId, unsigned int BaseId, unsigned int Key) = 0;
    // +0x0b0  0x06d88b80  ch2 op 0x1e  FLPACKET_SERVER_GFUPDATENEWSBROADCAST (size 0x28)  (was Slot44)
    // Data/Size: a MarshalBuffer image (NewsBroadcastProperties); FUN_06d97b00 copies it.
    virtual void SendServerGFUpdateNewsBroadcast(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x0b4  0x06d88bd0  ch2 op 0x0e  FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST (size 0x24)  (was Slot45)
    virtual void SendServerGFCompleteNewsBroadcastList(unsigned int ClientId, unsigned int BaseId) = 0;

    // ---- channel 2: space objects

    // +0x0b8  0x06d89190  ch2 op 0x42  FLPACKET_SERVER_CREATESOLAR (size 0xb8)  (was Slot46)
    virtual void SendServerCreateSolar(unsigned int ClientId, const CSolarCreateInfo& Info) = 0;
    // +0x0bc  0x06d89410  ch2 op 0x04  FLPACKET_SERVER_CREATESHIP (size 0xec)  (was Slot47)
    virtual void SendServerCreateShip(unsigned int ClientId, const CShipCreateInfo& Info) = 0;
    // +0x0c0  0x06d8aae0  ch2 op 0x28  FLPACKET_SERVER_CREATELOOT (size 0x34)  (was Slot48)
    virtual void SendServerCreateLoot(unsigned int ClientId, const FServerCreateLootRequest& Info) = 0;
    // +0x0c4  0x06d8a180  ch2 op 0x2c  FLPACKET_SERVER_CREATEMINE (size 0x68)  (was Slot49)
    virtual void SendServerCreateMine(unsigned int ClientId, const FServerCreateProjectileInfo& Info) = 0;
    // +0x0c8  0x06d8a730  ch2 op 0x37  FLPACKET_SERVER_CREATEGUIDED (size 0x58)  (was Slot50)
    virtual void SendServerCreateGuided(const unsigned int& ClientId, const FServerCreateGuidedInfo& Info) = 0;
    // +0x0cc  0x06d8a270  ch2 op 0x2d  FLPACKET_SERVER_CREATECOUNTER (size 0x68)  (was Slot51)
    // ClientId by reference: 06d8a34a MOV ECX,[ESP+0xc] / 06d8a351 MOV EAX,[ECX].
    virtual void SendServerCreateCounter(const unsigned int& ClientId, const FServerCreateProjectileInfo& Info) = 0;
    // +0x0d0  0x06d8a360  ch2 op 0x31  FLPACKET_SERVER_EXPLODEASTEROIDMINE (size 0x30)  (was Slot52)
    virtual void SendServerExplodeAsteroidMine(unsigned int ClientId, const FMineExplosion& Info) = 0;
    // +0x0d4  0x06d8a800  ch2 op 0x4a  FLPACKET_SERVER_SETGUIDEDTARGET (size 0x2c)  (was Slot53)
    // Widths are proven (dword, dword, word at 06d8a833 MOV AX,word ptr [ESP+0x18]); the
    // names are from the packet name only -- no port caller, and the exe's FUN_0053d4f0 is a
    // bare RET 0x10.
    virtual void SendServerSetGuidedTarget(unsigned int ClientId, unsigned int GuidedId, unsigned int TargetId,
                                           unsigned short TargetSubObjId) = 0;
    // +0x0d8  0x06d897c0  ch1 op 0x01  FLPACKET_COMMON_UPDATEOBJECT (size 0x4c)  (was Slot54)
    virtual void SendCommonUpdateObject(unsigned int ClientId, const FCommonUpdateObjectInfo& Info) = 0;
    // +0x0dc  0x06d89140  ch2 op 0x06  FLPACKET_SERVER_DESTROYOBJECT (size 0x28)  (was Slot55)
    virtual void SendServerDestroyObject(unsigned int ClientId, const FServerDestroyObjectInfo& Info) = 0;
    // +0x0e0  0x06d89770  ch2 op 0x0a  FLPACKET_SERVER_ACTIVATEOBJECT (size 0x2c)  (was Slot56)
    virtual void SendServerActivateObject(unsigned int ClientId, const FPayloadServerActivateObject& Info) = 0;
    // +0x0e4  0x06d8a860  ch2 op 0x21  FLPACKET_SERVER_SYSTEM_SWITCH_OUT (size 0x28)  (was Slot57)
    virtual void SendServerSystemSwitchOut(unsigned int ClientId, const FServerSystemSwitchOutInfo& Info) = 0;
    // +0x0e8  0x06d8a8b0  ch2 op 0x22  FLPACKET_SERVER_SYSTEM_SWITCH_IN (size 0x44)  (was Slot58)
    virtual void SendServerSystemSwitchIn(unsigned int ClientId, const FPayloadServerSystemSwitchIn& Info) = 0;
    // +0x0ec  0x06d8a990  ch2 op 0x0b  FLPACKET_SERVER_LAND (size 0x2c)  (was Slot59)
    virtual void SendServerLand(unsigned int ClientId, const FServerLandInfo& Info) = 0;
    // +0x0f0  0x06d8a080  ch2 op 0x07  FLPACKET_SERVER_LAUNCH (size 0x48)  (was Slot60)
    virtual void SendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info) = 0;
    // +0x0f4  0x06d8a130  ch2 op 0x27  FLPACKET_SERVER_REQUESTCREATESHIPRESP (size 0x28)  (was Slot61)
    // bSuccess: 06d8a158 MOV CL,byte ptr [ESP+0x10] (Server.dll's IServerImpl::RequestCreateShip
    // computes it with SETNZ into a byte local, .text:06cefa5f / 06cefa62); ShipId the new
    // ship's id.
    virtual void SendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId) = 0;
    // +0x0f8  0x06d8aa40  ch2 op 0x32  FLPACKET_SERVER_REQUESTSPACESCRIPT (size 0x3c)  (was Slot62)
    virtual void SendServerRequestSpaceScript(unsigned int ClientId, const FServerRequestSpaceScriptInfo& Info) = 0;
    // +0x0fc  0x06d88c10  ch2 op 0x05  FLPACKET_SERVER_DAMAGEOBJECT (size 0x40)  (was Slot63)
    // Damage goes to FUN_06d90390, the packet's DamageList copy (06d88c3d).
    virtual void SendServerDamageObject(unsigned int ClientId, unsigned int ObjectId, const DamageList& Damage) = 0;
    // +0x100  0x06d88c60  ch2 op 0x38  FLPACKET_SERVER_ITEMTRACTORED (size 0x2c)  (was Slot64)
    virtual void SendServerItemTractored(unsigned int ClientId, const FPayloadServerItemTractored& Info) = 0;
    // +0x104  0x06d88cc0  ch2 op 0x51  FLPACKET_SERVER_USE_ITEM (size 0x28)  (was Slot65)
    virtual void SendServerUseItem(unsigned int ClientId, const FPayloadServerUseItem& Info) = 0;
    // +0x108  0x06d88d70  ch2 op 0x29  FLPACKET_SERVER_SETREPUTATION (size 0x68)  (was Slot66)
    virtual void SendServerSetReputation(unsigned int ClientId, const FServerSetReputationInfo& Info) = 0;
    // +0x10c  0x06d88f70  ch2 op 0x2a  FLPACKET_SERVER_ADJUSTATTITUDE (size 0x28)  (was Slot67)
    // 06d88f8d MOV ECX,[EAX] (a dword) and 06d88f96 FLD float ptr [EAX+0x4] through Info.
    virtual void SendServerAdjustAttitude(unsigned int ClientId, const FPayloadServerAdjustAttitude& Info) = 0;
    // +0x110  0x06d88fc0  ch2 op 0x4f  FLPACKET_SERVER_SENDCOMM (size 0x78)  (was Slot68)
    // RET 0x5c = 23 dwords: CommCostume is a Costume BY VALUE (13 dwords, copied at
    // 06d89007..06d89014; Server.dll REP MOVSDs it onto the stack at 06cf6b97); TextIds is an
    // array of TextCount dwords (06d8907e..06d890b2, count compared signed); Duration goes
    // through FLD/FSTP (06d890d4); the last argument is stored as a byte (06d890db) and
    // rpclocal's receiver always passes a literal 0 for it (06d83083). The scalar names follow
    // pub::SpaceObj::SendComm (server side); freelancer.exe's ClientSendComm calls CommType
    // "CaptionIds" and Mood "PermutationIds" -- TODO(name).
    virtual void SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                    unsigned int VoiceId, Costume CommCostume, unsigned int CommType,
                                    const unsigned int* TextIds, int TextCount, unsigned int Mood,
                                    float Duration, bool bReceiverIsOwnShip) = 0;
    // +0x114  0x06d89100  ch2 op 0x56  FLPACKET_SERVER_SENDNNMESSAGE (size 0x24)  (was Slot69)
    virtual void SendServerSendNNMessage(unsigned int ClientId, unsigned int MessageId) = 0;

    // ---- channel 2: missions (ClientId by reference -- the senders dereference it)

    // +0x118  0x06d8af60  ch2 op 0x45  FLPACKET_SERVER_SET_MISSION_MESSAGE (size 0x2c)  (was Slot70)
    virtual void SendServerSetMissionMessage(const unsigned int& ClientId, const CSetMissionMessage& Message) = 0;
    // +0x11c  0x06d8ad90  ch2 op 0x34  FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE (size 0x30)  (was Slot71)
    virtual void SendServerReplaceMissionObjective(const unsigned int& ClientId,
                                                   const FServerReplaceMissionObjectiveInfo& Info) = 0;
    // +0x120  0x06d8b020  ch2 op 0x35  FLPACKET_SERVER_SETMISSIONOBJECTIVES (size 0x48)  (was Slot72)
    virtual void SendServerSetMissionObjectives(const unsigned int& ClientId,
                                                const FServerSetMissionObjectivesInfo& Info) = 0;
    // +0x124  0x06d8ac40  ch2 op 0x33  FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE (size 0x2c)  (was Slot73)
    virtual void SendServerSetMissionObjectiveState(const unsigned int& ClientId,
                                                    const FServerMissionObjectiveStateInfo& Info) = 0;
    // +0x128  0x06d8aca0  ch2 op 0x5b  FLPACKET_SERVER_MISSIONSAVEA (size 0x24)  (was Slot74)
    // ClientId BY VALUE here. freelancer.exe's LAB_00437b30 tail-calls IServer::MissionSaveB
    // with both arguments in place, and retail decorates that one
    // ?MissionSaveB@IServerImpl@@UAEXIK@Z -- so MissionId is an unsigned long.
    virtual void SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId) = 0;
    // +0x12c  0x06d8ace0  ch2 op 0x46  FLPACKET_SERVER_MARKOBJ (size 0x28)  (was Slot75)
    // MarkType is pub::Player::MarkObj's int; the sender keeps its low byte (06d8ad08).
    virtual void SendServerMarkObj(const unsigned int& ClientId, unsigned int ObjectId, int MarkType) = 0;
    // +0x130  0x06d8ad30  ch2 op 0x47  FLPACKET_SERVER_CFGINTERFACENOTIFICATION (size 0x2c)  (was Slot76)
    virtual void SendServerCfgInterfaceNotification(const unsigned int& ClientId,
                                                    const FPayloadServerCfgInterfaceNotification& Info) = 0;

    // ---- channel 2: player statistics (ClientId by reference)

    // +0x134  0x06d8b210  ch2 op 0x30  FLPACKET_SERVER_SETCASH (size 0x24)  (was Slot77)
    virtual void SendServerSetCash(const unsigned int& ClientId, int Cash) = 0;
    // +0x138  0x06d8b250  ch2 op 0x3e  FLPACKET_SERVER_SETNUMKILLS (size 0x24)  (was Slot78)
    virtual void SendServerSetNumKills(const unsigned int& ClientId, int NumKills) = 0;
    // +0x13c  0x06d8b290  ch2 op 0x3f  FLPACKET_SERVER_SETMISSIONSUCCESSES (size 0x24)  (was Slot79)
    virtual void SendServerSetMissionSuccesses(const unsigned int& ClientId, int NumSuccesses) = 0;
    // +0x140  0x06d8b2d0  ch2 op 0x40  FLPACKET_SERVER_SETMISSIONFAILURES (size 0x24)  (was Slot80)
    virtual void SendServerSetMissionFailures(const unsigned int& ClientId, int NumFailures) = 0;
    // +0x144  0x06d8a3c0  ch2 op 0x4d  FLPACKET_SERVER_LOADHINT (size 0x2c)  (was Slot81)
    // Not const: the sender calls the hint's own Serialize (06d8a3ed CALL [EDX+0x4]) and
    // CleanUp (06d8a412 CALL [EDX+0xc]) virtuals on it. A pointer, not a reference: both
    // receivers (rpclocal caseD_4d 06d81c0a, remoteserver caseD_4d 06bd1cfa) push NULL for
    // any wire Kind but 1. No implementer null-tests it (senders 06d8a3dc / 06b35e9c, the
    // exe's LAB_0053de70 reads Kind at +4 first).
    virtual void SendServerLoadHint(const unsigned int& ClientId, BaseHint* Hint) = 0;
    // +0x148  0x06d8a5c0  ch2 op 0x2b  FLPACKET_SERVER_SETGROUPFEELINGS (size 0x34)  (was Slot82)
    virtual void SendServerSetGroupFeelings(unsigned int ClientId, const FServerSetGroupFeelingsInfo& Info) = 0;
    // +0x14c  0x06d8a9e0  ch2 op 0x44  FLPACKET_SERVER_REQUEST_RETURNED (size 0x28)  (was Slot83)
    // The sender keeps one byte of each of the last three (06d8aa08..06d8aa1e); both ends
    // treat them as ints (Server.dll's FUN_06d01e80 request_returned(int, int, int),
    // freelancer.exe's FUN_00546d00).
    virtual void SendServerRequestReturned(const unsigned int& ClientId, unsigned int ShipId, int RequestType,
                                           int Response, int Extra) = 0;
    // +0x150  0x06d8bfe0  ch2 op 0x3c  FLPACKET_SERVER_REPAIROBJECT (size 0x2c)  (was Slot84)
    virtual void SendServerRepairObject(unsigned int ClientId, const FServerRepairObjectInfo& Info) = 0;
    // +0x154  0x06d8c040  ch2 op 0x3d  FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE (size 0x2c)  (was Slot85)
    // Flag: one byte (06d8c072 MOV DL,byte ptr [param_4]); Server.dll passes 1, freelancer.exe's
    // FUN_0053deb0 never reads it -- TODO(name).
    virtual void SendServerRemoteObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                                   const EquipDescVector& Cargo, unsigned char Flag) = 0;
    // +0x158  0x06d8c0f0  ch2 op 0x55  FLPACKET_SERVER_OBJECTCARGOUPDATE (size 0x34)  (was Slot86)
    virtual void SendServerObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                             const EquipDesc& Item) = 0;
    // +0x15c  0x06d8c170  ch2 op 0x41  FLPACKET_SERVER_BURNFUSE (size 0x3c)  (was Slot87)
    virtual void SendServerBurnFuse(const unsigned int& ClientId, const FServerBurnFuseInfo& Info) = 0;
    // +0x160  0x06d8ae90  ch2 op 0x4b  FLPACKET_SERVER_SET_CAMERA (size 0x44)  (was Slot88)
    virtual void SendServerSetCamera(const unsigned int& ClientId, const FPayloadServerSetCamera& Info) = 0;
    // +0x164  0x06d8af20  ch2 op 0x4c  FLPACKET_SERVER_REVERT_CAMERA (size 0x20)  (was Slot89)
    virtual void SendServerRevertCamera(const unsigned int& ClientId) = 0;
    // +0x168  0x06d8ae50  ch2 op 0x43  FLPACKET_SERVER_SET_STORY_CUE (size 0x24)  (was Slot90)
    // Both by reference: 06d8ae69 MOV ECX,[ESP+0x10] / 06d8ae6d MOV EDX,[ECX] for the cue, and the
    // usual dereference of ClientId; pub::Player::SetStoryCue passes &cue (06d55dce).
    virtual void SendServerSetStoryCue(const unsigned int& ClientId, const unsigned int& CueId) = 0;

    // ---- channel 1: the byte-block family (see FPayloadCommonBuffer)

    // +0x16c  0x06d8b4f0  ch1 op 0x11  FLPACKET_COMMON_SET_WEAPON_GROUP (size 0x2c)  (was Slot91)
    // BORROWS Data: stored with the ownership byte left 0.
    virtual void SendCommonSetWeaponGroup(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x170  0x06d8b540  ch1 op 0x13  FLPACKET_COMMON_SET_VISITED_STATE (size 0x2c)  (was Slot92)
    // TAKES Data over: 06d8b573 MOV byte ptr [EDX+0x28],0x1 -- the packet frees it.
    virtual void SendCommonSetVisitedState(unsigned int ClientId, unsigned char* Data, unsigned int Size) = 0;
    // +0x174  0x06d8b590  ch1 op 0x16  FLPACKET_COMMON_REQUEST_BEST_PATH (size 0x2c)  (was Slot93)
    // COPIES Data (operator_new + REP MOVSD at 06d8b5b1..06d8b5d6).
    virtual void SendCommonRequestBestPath(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x178  0x06d8b610  ch1 op 0x18  FLPACKET_COMMON_REQUEST_PLAYER_STATS (size 0x2c)  (was Slot94)
    // COPIES Data, as slot 93.
    virtual void SendCommonRequestPlayerStats(unsigned int ClientId, const unsigned char* Data, unsigned int Size) = 0;
    // +0x17c  0x06d8b690  ch1 op 0x1b  FLPACKET_COMMON_POP_UP_DIALOG (size 0x34)  (was Slot95)
    // Both FmtStrs are flattened into the packet; Buttons is the dword at packet+0x30 (06d8b780).
    virtual void SendCommonPopUpDialog(unsigned int ClientId, const FmtStr& Title, const FmtStr& Text,
                                       unsigned int Buttons) = 0;
    // +0x180  0x06d8b7a0  ch1 op 0x17  FLPACKET_COMMON_REQUEST_GROUP_POSITIONS (size 0x2c)  (was Slot96)
    // TAKES Data over (06d8b7d3).
    virtual void SendCommonRequestGroupPositions(unsigned int ClientId, unsigned char* Data, unsigned int Size) = 0;
    // +0x184  0x06d8b7f0  ch1 op 0x19  FLPACKET_COMMON_SET_MISSION_LOG (size 0x2c)  (was Slot97)
    // TAKES Data over (06d8b823).
    virtual void SendCommonSetMissionLog(unsigned int ClientId, unsigned char* Data, unsigned int Size) = 0;
    // +0x188  0x06d8b840  ch1 op 0x1c  FLPACKET_COMMON_SET_INTERFACE_STATE (size 0x2c)  (was Slot98)
    // TAKES Data over (06d8b873).
    virtual void SendCommonSetInterfaceState(unsigned int ClientId, unsigned char* Data, unsigned int Size) = 0;
    // +0x18c  0x06d8b890  ch1 op 0x1a  FLPACKET_COMMON_REQUEST_RANK_LEVEL (size 0x2c)  (was Slot99)
    // TAKES Data over (06d8b8c3).
    virtual void SendCommonRequestRankLevel(unsigned int ClientId, unsigned char* Data, unsigned int Size) = 0;

    // +0x190  0x06d8a480  ch2 op 0x4e  FLPACKET_SERVER_SETDIRECTIVE (size 0xb4)  (was Slot100)
    // ClientId by reference: 06d8a5a2 MOV EDX,[ESP+0x14] / 06d8a5a9 MOV ECX,[EDX].
    virtual void SendServerSetDirective(const unsigned int& ClientId, const FPayloadServerSetDirective& Info) = 0;

    // ---- ch1 op 0x12 FLPACKET_COMMON_PLAYER_TRADE (size 0x44): eight senders, each writing
    //      its sub-type literal to packet+0x20; suffix = the retail-decorated IServerImpl
    //      method that writes the same literal (06d8e3e1 .. 06d8e641).

    // +0x194  0x06d8b8e0  sub-type 0x01 (?InitiateTrade@IServerImpl@@UAEXII@Z)  (was Slot101)
    virtual void SendCommonPlayerTradeInitiateTrade(unsigned int ClientId, unsigned int ShipId) = 0;
    // +0x198  0x06d8b980  sub-type 0x02 (?TerminateTrade@IServerImpl@@UAEXIH@Z)  (was Slot102)
    virtual void SendCommonPlayerTradeTerminateTrade(unsigned int ClientId, int Reason) = 0;
    // +0x19c  0x06d8b930  sub-type 0x40 (?AcceptTrade@IServerImpl@@UAEXI_N@Z)  (was Slot103)
    // bAccepted: 06d8b963 MOV DL,byte ptr [ESP+0x14].
    virtual void SendCommonPlayerTradeAcceptTrade(unsigned int ClientId, unsigned int ShipId, bool bAccepted) = 0;
    // +0x1a0  0x06d8b9d0  sub-type 0x04 (?SetTradeMoney@IServerImpl@@UAEXIK@Z)  (was Slot104)
    virtual void SendCommonPlayerTradeSetTradeMoney(unsigned int ClientId, unsigned int ShipId, unsigned long Money) = 0;
    // +0x1a4  0x06d8ba20  sub-type 0x08 (?AddTradeEquip@IServerImpl@@UAEXIABUEquipDesc@@@Z)  (was Slot105)
    virtual void SendCommonPlayerTradeAddTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) = 0;
    // +0x1a8  0x06d8baa0  sub-type 0x10 (?DelTradeEquip@IServerImpl@@UAEXIABUEquipDesc@@@Z)  (was Slot106)
    virtual void SendCommonPlayerTradeDelTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) = 0;
    // +0x1ac  0x06d8bb10  sub-type 0x20 (?RequestTrade@IServerImpl@@UAEXII@Z)  (was Slot107)
    virtual void SendCommonPlayerTradeRequestTrade(unsigned int ClientId, unsigned int ShipId) = 0;
    // +0x1b0  0x06d8bb60  sub-type 0x80 (?StopTradeRequest@IServerImpl@@UAEXI@Z)  (was Slot108)
    virtual void SendCommonPlayerTradeStopTradeRequest(unsigned int ClientId, unsigned int ShipId) = 0;

    // +0x1b4  0x06d8a430  ch2 op 0x39  FLPACKET_SERVER_SCANNOTIFY (size 0x28)  (was Slot109)
    virtual void SendServerScanNotify(unsigned int ClientId, unsigned int ScannerId, unsigned int GoodId) = 0;

    // ---- ch2 op 0x52 FLPACKET_SERVER_PLAYERLIST (size 0x30): three senders, the kind
    //      dword at packet+0x20 (1 add, 2 remove, 0 clear), then FUN_06d99950(Name, Value).

    // +0x1b8  0x06d8c500  kind 1  (was Slot110)
    // Name is _wcsdup'ed after a null test (FUN_06d99950, 06d99976); bAlreadyPresent is a byte
    // (06d8c51d) -- 1 when Server.dll lists an existing player to a newcomer, 0 when it
    // announces the newcomer (freelancer.exe's FUN_0046aa10 gets `== 0`).
    virtual void SendServerPlayerListAdd(unsigned int ClientId, const wchar_t* Name, unsigned int PlayerId,
                                         bool bAlreadyPresent) = 0;
    // +0x1bc  0x06d8c560  kind 2  (was Slot111)
    virtual void SendServerPlayerListRemove(unsigned int ClientId, unsigned int PlayerId) = 0;
    // +0x1c0  0x06d8c5b0  kind 0  (was Slot112)
    virtual void SendServerPlayerListClear(unsigned int ClientId) = 0;

    // ---- ch2 op 0x54 FLPACKET_SERVER_MISCOBJUPDATE (size 0x6c): eight senders, each
    //      setting its own bits of the flag word at packet+0x20 (rpc_payloads.h table).

    // +0x1c4  0x06d8c1f0  flags 0x0009  (was Slot113)
    // GroupId lands in the exe's CShip+0x1b4 (LAB_0053e2b0).
    virtual void SendServerMiscObjUpdateObjectGroup(unsigned int ClientId, unsigned int ObjectId, unsigned int GroupId) = 0;
    // +0x1c8  0x06d8c290  flags 0x0002  (was Slot114)
    // Server.dll: PlayerData group_mission_id / group_mission_set_by (pub::Player::SetMsnID).
    virtual void SendServerMiscObjUpdateMission(unsigned int ClientId, unsigned int MissionId, unsigned int MissionSetBy) = 0;
    // +0x1cc  0x06d8c430  flags 0x0010  (was Slot115)
    // The server's message of the day, by reference on every side: Server.dll pushes
    // &PlayerDB::m_MOTD (06d4509c) or &Message (PlayerDB::SetMOTD); rpclocal's sender hands
    // it to FUN_06d98b20 (string assign, untested) and its receiver passes `LEA EDX,[EBP+0x40]`,
    // the string inside the packet; freelancer.exe's LAB_0053e3a0 reads [A1+4] -- the VC6
    // basic_string<unsigned short> _Ptr, falling back to _Nullstr -- and gives it to
    // FUN_0046b0f0 as a wchar_t*. The client id is not read by the exe.
    virtual void SendServerMiscObjUpdateMotd(unsigned int ClientId, const std::wstring& Motd) = 0;
    // +0x1d0  0x06d8c240  flags 0x0028  (was Slot116)
    virtual void SendServerMiscObjUpdateAffiliation(unsigned int ClientId, unsigned int ObjectId,
                                                    unsigned int Affiliation) = 0;
    // +0x1d4  0x06d8c2e0  flags 0x0044  (was Slot117)
    // Rank travels as a word (FUN_06d98ce0); the exe's FUN_0053e200 compares PlayerId with
    // its own ClientId.
    virtual void SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank) = 0;
    // +0x1d8  0x06d8c390  flags 0x0005  (was Slot118)
    virtual void SendServerMiscObjUpdatePlayerGroup(unsigned int ClientId, unsigned int PlayerId, unsigned int GroupId) = 0;
    // +0x1dc  0x06d8c3e0  flags 0x0084  (was Slot119)
    virtual void SendServerMiscObjUpdateSystem(unsigned int ClientId, unsigned int PlayerId, unsigned int SystemId) = 0;
    // +0x1e0  0x06d8c330  flags 0x0100  (was Slot120)
    // A vector of dwords by reference: Server.dll's update_formation_state pushes a stack
    // vector (06d5d920 `LEA EBX,[ESP+0x14]` / 06d5d924 PUSH, and three more sites), rpclocal's
    // sender walks it element by element into FUN_06d98cf0 and its receiver passes `LEA
    // EDX,[EBP+0x58]`, the vector inside the packet; freelancer.exe's LAB_0053e270 reads
    // [A1+4] (_First) and [A1+8] (_Last) with `SAR EAX,0x2` -- 4-byte elements.
    virtual void SendServerMiscObjUpdateFormation(unsigned int ClientId, const std::vector<unsigned int>& MemberIds) = 0;

    // +0x1e4  0x06d8c600  ch2 op 0x53  FLPACKET_SERVER_FORMATION_UPDATE (size 0x30)  (was Slot121)
    // Offset: three dwords copied through the pointer (06d8c621..06d8c645), FLD/FSTP on the
    // receiver -- a Vector.
    virtual void SendServerFormationUpdate(unsigned int ClientId, unsigned int LeaderId, const Vector& Offset) = 0;
    // +0x1e8  0x06d8c660  ch2 op 0x57  FLPACKET_SERVER_SET_MUSIC (size 0x38)  (was Slot122)
    // pub::Audio::SetMusic's Tryptich unpacked: four dwords, a byte (06d8c6ad MOV CL,byte ptr
    // [ESP+0x20]), a float (06d8c67d FLD float ptr [ESP+0x24]). The music names follow the
    // exe's FUN_00553340 (StarSysSetMusicOverride); bFlag is TODO(name).
    virtual void SendServerSetMusic(unsigned int ClientId, unsigned int SpaceMusic, unsigned int DangerMusic,
                                    unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag,
                                    float Fade) = 0;
    // +0x1ec  0x06d8c6e0  ch2 op 0x58  FLPACKET_SERVER_CANCEL_MUSIC (size 0x20)  (was Slot123)
    virtual void SendServerCancelMusic(unsigned int ClientId) = 0;
    // +0x1f0  0x06d8c710  ch2 op 0x59  FLPACKET_SERVER_PLAY_SOUND_EFFECT (size 0x24)  (was Slot124)
    virtual void SendServerPlaySoundEffect(unsigned int ClientId, unsigned int SoundId) = 0;
    // +0x1f4: write local packet statistics to the named CSV file (slot 125, name unchanged;
    // the mirror of ?DumpPacketStats@IServerImpl@@UAEXPBD@Z).
    virtual void DumpPacketStats(const char* Name) = 0;
};

// ============================================================================
// IChatClient - 1 slot. rpclocal keeps it as a second base of the same object:
// GetChatClientInterface() returns Client + 4 (.data:0x06dbbce0); its vtable at
// .rdata:0x06db3970 holds a single entry, 0x06d8c470.
// ============================================================================
class IChatClient {
public:
    // +0x000  0x06d8c470  ch1 op 0x05  FLPACKET_COMMON_CHATMSG (size 0x30)  (was Slot00)
    // RET 0x10. TargetId -> packet+0x20 and SenderId -> packet+0x24 whole (06d8c4d4..06d8c4e3);
    // slot 9 gets TargetId's low word (06d8c4ed MOVZX EAX,AX) -- the client id inside the
    // CHAT_ID. Data is copied only when it and Size are both non-zero (06d8c499 / 06d8c4a1).
    // The order is IChatServer::SubmitChat's (?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@KPBX0H@Z):
    // CHAT_ID by value, the size an unsigned long, the data `const void*`.
    virtual void SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) = 0;
};

// ============================================================================
// Exported by BOTH rpclocal.dll (ordinal 111) and freelancer.exe.
// ============================================================================
extern "C" {
#if !defined(REMOTESERVER_EXPORTS)
    __declspec(dllexport) IClient* GetClientInterface();  // ordinal 111
#endif
}
