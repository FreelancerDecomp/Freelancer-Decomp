// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payload_ch3.cpp -- the client -> server half of rpclocal: the channel-3 packet
// types AND the port of retail FUN_06d86a70, the client-side dispatcher.
//
// FUN_06d86a70 (0x06d86a70, 1518 instructions) is the exact mirror of
// IServerImpl::HandlePacket: it reads the same routing header at CDAPacket+0x0c,
// switches on the channel, and for channels 1 and 3 calls exactly one vtable slot of
// the REAL IServer that Server.dll handed back (.data:DAT_06dbbd24, which
// IServerImpl::Startup fills from DAT_06dbbd5c). Channel 2 never reaches it.
//
// Three readings stand behind every layout below, the same discipline channels 1 and 2
// were closed with:
//
//   1. the SENDER   -- the IServerImpl slot body in EXE/Disasm/rpclocal.dll.c / .txt
//                      that allocates through FUN_06d96300(Serializer, 3, Opcode, &Out)
//                      and writes the payload from packet+0x20;
//   2. the RECEIVER -- the matching case of this dispatcher, which reads the same bytes
//                      back and makes one IServer call;
//   3. the SIZE     -- the type's operator new() argument from the registry
//                      (rpc_packet_registry.cpp): payload == size - 0x20.
//
// Channel 3 also affords a FOURTH reading that channels 1 and 2 did not: the retail
// IServer vtable was recovered independently (decomp-tracker IServer card ->
// src/include/common_server.h), so every slot OFFSET this dispatcher calls comes with a
// parameter list recovered from a different source. All 27 channel-1 call sites and all
// 53 channel-3 call sites land on a slot whose declared arity and argument types match
// what the case actually pushes -- 80 of 80, no exceptions. Together those 80 cases reach
// 80 distinct slots of IServerImpl's 93-entry table; the 13 they never touch are the nine
// lifecycle/connection slots 9..17 plus GetServerID (88), GetServerSig (89),
// DumpPacketStats (90) and PushToServer (92, IServerImpl's own virtual -- IServer itself
// has 92 slots, 0..91), none of which is an RPC at all.
//
// WHAT IS NOT CLOSED, stated plainly (see docs/re/rpclocal.md):
//   * ch3 0x0a GFREQUESTSHIPINFO is registered (size 0x20, payload 0) but has NO sender
//     in retail and NO case in this dispatcher. It is a genuine retail dead entry.
//
// The three container types that used to stand here -- 0x16 REQEQUIPMENT, 0x17 REQCARGO
// and 0x36 REQCOLLISIONGROUPS -- are closed: the retail std::list wire layout is
// reproduced in rpc_containers.{h,cpp} and the codecs are at the bottom of this file.
// No module-private mirror stands in for them: rpclocal links Common.lib, so the real
// EquipDescList / std::list<CollisionGroupDesc> are handed to IServer (see the BRIDGE
// comment above MarshalReqEquipmentCommon, and the former-BRIDGE entry in
// docs/re/deviations.md).

#include "rpc_payloads.h"
#include "rpc_containers.h"
#include "common_server.h"
// EquipDesc and its setters, imported by retail rpclocal at .rdata:06db3004 and
// 06db3070..06db308c and used verbatim by the two PLAYER_TRADE equipment sub-types.
#include "common_goods.h"
// CollisionGroupDesc's COMPLETE definition, for ch3 0x36 REQCOLLISIONGROUPS. Retail's
// receiver needs no such include -- its std::list layout IS the payload layout, so
// FUN_06d8f7d0 hands the payload's own address to IServer+0x6c. The port has to build a
// real std::list<CollisionGroupDesc>, and common_archetype.h (reached through
// common_goods.h) only forward-declares the element. common_server.h and
// common_cobject.h already coexist in src/Server/server_pub.cpp.
#include "common_cobject.h"

#include <cstring>
#include <list>
#include <new>

// ---------------------------------------------------------------------------
// The real interfaces Server.dll handed back. Retail keeps them in .data:0x06dbbd24
// (IServer, aliased from 0x06dbbd5c) and .data:0x06dbbd20 (IChatServer, from
// 0x06dbbd60). IServerImpl::Startup publishes both; Shutdown clears them.
// ---------------------------------------------------------------------------
IServer* GRealServer = nullptr;
IChatServer* GRealChatServer = nullptr;

// ---------------------------------------------------------------------------
// Shared helpers. The three channel-3 block writers (FUN_06d9a160, FUN_06d9a5f0,
// FUN_06d9aab0) and the one channel-3 blob writer (FUN_06d97b00, shared with channel 2)
// are all the same four instructions: a bare operator new(N), REP MOVSD/MOVSB, store the
// block and its length in two payload fields. The packet owns the block from then on.
// The three block writers put `if (size == 0 || src == 0) return` in front
// (06d9a16b/06d9a173, 06d9a5fb/06d9a603, 06d9aaba/06d9aac2) -- their callers carry that
// test; FUN_06d97b00 has none (06d97b0b).
// ---------------------------------------------------------------------------
static void* RpcCh3CopyBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

// strlen / wcslen spelled out, the way retail's SCASB.REPNE loop (06d8d283) and MSVCRT
// wcslen (06d8db41) compute them -- neither tests the pointer.
static unsigned int RpcCh3StringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

static unsigned int RpcCh3WideLength(const wchar_t* Text) {
    unsigned int Length = 0;
    while (Text[Length] != 0) {
        ++Length;
    }
    return Length;
}

// ===========================================================================
// Generic marshallers. The argument index is the position in the retail IServer
// signature (src/include/common_server.h), NOT counting `this`.
// ===========================================================================

// payload+0x00 = Args[Index], by value.
template<unsigned int Index>
static bool MarshalCh3DwordAt(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDword Wire;
    Wire.Field_0x00 = Args[Index];
    return RpcPayloadWrite(Packet, 0, Wire);
}

// payload+0x00 = *(dword*)Args[Index]. Used where the method takes `uint const&`
// (Dock's first argument). Retail: MOV EAX,[ECX]; MOV [packet+0x20],EAX.
template<unsigned int Index>
static bool MarshalCh3DwordByRefAt(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDword Wire;
    Wire.Field_0x00 = *reinterpret_cast<const unsigned int*>(Args[Index]);
    return RpcPayloadWrite(Packet, 0, Wire);
}

// Nothing is serialized at all. ch3 0x05, 0x10, 0x38, 0x42 -- the whole call is the
// opcode plus the implicit session.
static bool MarshalCh3Empty(FPacketBase*, const unsigned int*, unsigned int) {
    return true;
}

// The first N dwords of the struct Args[0] points at, copied straight through.
template<unsigned int DwordCount>
static bool MarshalCh3StructHead(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const unsigned int* Src = reinterpret_cast<const unsigned int*>(Args[0]);
    for (unsigned int i = 0; i < DwordCount; ++i) {
        RpcPayloadWrite(Packet, i * 4u, Src[i]);
    }
    return true;
}

// ===========================================================================
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- sender IServerImpl::Login 0x06d8cc70,
// receiver caseD_1. IServer slot 18 (+0x48) Login(SLoginInfo const&, uint).
//
// Retail's receiver does NOT build the SLoginInfo on its stack: it fills a FILE-STATIC
// one at .data:0x06dbba38 and passes its address. Reproduced as found -- that is not
// re-entrant in retail either, and single player never has two logins in flight.
//
// The two wcsncpy caps (0x28 and 0x10 wide characters) and the two explicit NUL stores
// at .data:0x06dbba86 / 0x06dbbaf6 are retail's, and they are what pins the second
// string's offset inside SLoginInfo to +0xa0 and the flag's to +0xc0.
// ===========================================================================

// Byte-addressed so the field offsets stay the ones the listing proves, without
// depending on src/include/common_server_types.h's SLoginInfo field break -- which is
// wrong (it has one wchar_t AccountName[96] spanning +0x00..+0xbf and therefore no room
// for the string retail keeps at +0xa0). Reported, not patched: cross-module header.
static const unsigned int kLoginAccountOffset = 0x00;   // cap 0x28 wide chars
static const unsigned int kLoginPasswordOffset = 0xa0;  // cap 0x10 wide chars
static const unsigned int kLoginFlagOffset = 0xc0;
static unsigned char GStaticLoginInfo[sizeof(SLoginInfo)];   // retail .data:0x06dbba38

static bool MarshalLogin(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const unsigned char* Info = reinterpret_cast<const unsigned char*>(Args[0]);
    // IServerImpl::Login 0x06d8cc70: two UNBOUNDED wcscpy calls into a 0x404-byte stack
    // window (06d8cca3 from SLoginInfo+0, 06d8ccb4 from SLoginInfo+0xa0 to window+0x200),
    // the flag byte SLoginInfo+0xc0 to window+0x400 (06d8ccb6/06d8ccc0), then 0x402 bytes
    // blitted into the payload (06d8ccca..06d8ccd5). The window bytes neither wcscpy
    // reaches are uninitialised stack in retail -- there is no retail value to copy, so
    // they start at zero here (not a deviation: the ledger's uninitialised-stack rule).
    FPayloadClientLogin Wire;
    for (unsigned int i = 0; i < (unsigned int)sizeof(Wire); ++i) {
        reinterpret_cast<unsigned char*>(&Wire)[i] = 0;
    }
    wcscpy(Wire.AccountName, reinterpret_cast<const wchar_t*>(Info + kLoginAccountOffset));
    wcscpy(Wire.Password, reinterpret_cast<const wchar_t*>(Info + kLoginPasswordOffset));
    Wire.CreateFlag = Info[kLoginFlagOffset];
    // 06d8ccca MOV ECX,0x100 / 06d8ccd3 MOVSD.REP / 06d8ccd5 MOVSW: exactly 0x402 bytes
    // reach the payload; +0x402/+0x403 are left as the packet's constructor left them.
    memcpy(Packet->Payload(), &Wire, 0x402u);
    return true;
}

static bool DeliverLogin(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const unsigned char* Payload = Packet->Payload();
    // caseD_1 of FUN_06d86a70: MSVCRT wcsncpy into the file-static SLoginInfo -- 0x28
    // wide characters at 06d86ddc, 0x10 at 06d86ded -- which pads with NULs up to the
    // count, then the flag byte (06d86dfc). Retail never clears the static itself: every
    // byte it does not overwrite stays at its zero .data image, and nothing else writes it.
    wcsncpy(reinterpret_cast<wchar_t*>(GStaticLoginInfo + kLoginAccountOffset),
            reinterpret_cast<const wchar_t*>(Payload + 0x000), 0x28);
    wcsncpy(reinterpret_cast<wchar_t*>(GStaticLoginInfo + kLoginPasswordOffset),
            reinterpret_cast<const wchar_t*>(Payload + 0x200), 0x10);
    GStaticLoginInfo[kLoginFlagOffset] = Payload[0x400];
    // Retail's two explicit terminator stores, +0x4e and +0xbe.
    GStaticLoginInfo[0x4e] = 0;
    GStaticLoginInfo[0x4f] = 0;
    GStaticLoginInfo[0xbe] = 0;
    GStaticLoginInfo[0xbf] = 0;
    Server->Login(*reinterpret_cast<const SLoginInfo*>(GStaticLoginInfo), SessionId);
    return true;
}

// ===========================================================================
// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION -- sender 0x06d8d6e0, receiver caseD_3.
// IServer slot 53 (+0xd4) SPMunitionCollision(SSPMunitionCollisionInfo const&, uint).
// Identity mapping; the only narrow field is the word at +0x0c on both sides.
// ===========================================================================
static bool MarshalMunCollision(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const SSPMunitionCollisionInfo* Info = reinterpret_cast<const SSPMunitionCollisionInfo*>(Args[0]);
    FPayloadClientMunCollision Wire;
    Wire.Field_0x00 = Info->Field_0x00;
    Wire.Field_0x04 = Info->Field_0x04;
    Wire.ObjectId = Info->ObjectId;
    Wire.Field_0x0c = Info->Field_0x0c;
    Wire.Pad_0x0e = 0;
    Wire.Field_0x10 = Info->Field_0x10;
    Wire.Field_0x14 = Info->Field_0x14;
    Wire.Field_0x18 = Info->Field_0x18;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverMunCollision(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientMunCollision Wire = RpcPayloadRead<FPayloadClientMunCollision>(Packet, 0);
    SSPMunitionCollisionInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.ObjectId = Wire.ObjectId;
    Info.Field_0x0c = Wire.Field_0x0c;
    Info.Pad_0x0e = 0;
    Info.Field_0x10 = Wire.Field_0x10;
    Info.Field_0x14 = Wire.Field_0x14;
    Info.Field_0x18 = Wire.Field_0x18;
    Server->SPMunitionCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x04 REQUESTLAUNCH / 0x07 ENTERBASE / 0x0b SYSTEM_SWITCH_OUT_COMPLETE /
// 0x0d EXITBASE / 0x0e ENTERLOCATION / 0x0f EXITLOCATION / 0x13 GFSELECTOBJECT /
// 0x15 REQSHIPARCH -- one dword, the FIRST argument; the session is the second and is
// dropped. Receivers push (payload+0x00, SessionId) in that order.
// ===========================================================================
static bool DeliverPlayerLaunch(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->PlayerLaunch(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverBaseEnter(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->BaseEnter(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverBaseExit(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->BaseExit(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverLocationEnter(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->LocationEnter(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverLocationExit(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->LocationExit(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverSystemSwitchOutComplete(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->SystemSwitchOutComplete(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverGFObjSelect(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->GFObjSelect(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverReqShipArch(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->ReqShipArch(RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverReqSetCash(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->ReqSetCash((int)RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverReqChangeCash(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->ReqChangeCash((int)RpcPayloadRead<unsigned int>(Packet, 0), SessionId);
    return true;
}
static bool DeliverReqHullStatus(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->ReqHullStatus(RpcPayloadRead<float>(Packet, 0), SessionId);
    return true;
}
static bool DeliverReqDifficultyScale(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->ReqDifficultyScale(RpcPayloadRead<float>(Packet, 0), SessionId);
    return true;
}

// ch3 0x35 INTERFACEITEMUSED / 0x41 ABORT_MISSION / 0x45 MISSIONSAVEB / 0x47 RTCDONE --
// these four take the session FIRST, so retail serializes the SECOND argument and the
// receiver pushes (SessionId, payload+0x00).
static bool DeliverInterfaceItemUsed(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->InterfaceItemUsed(SessionId, RpcPayloadRead<unsigned int>(Packet, 0));
    return true;
}
static bool DeliverAbortMission(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->AbortMission(SessionId, RpcPayloadRead<unsigned int>(Packet, 0));
    return true;
}
static bool DeliverMissionSaveB(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->MissionSaveB(SessionId, RpcPayloadRead<unsigned int>(Packet, 0));
    return true;
}
static bool DeliverRTCDone(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->RTCDone(SessionId, RpcPayloadRead<unsigned int>(Packet, 0));
    return true;
}

// ===========================================================================
// ch3 0x05 REQUESTCHARINFO -- sender IServerImpl::CharacterInfoReq 0x06d8cd00 writes
// NOTHING, and the receiver pushes a literal 0 for the bool. So the bool argument is
// lost in transit on purpose: retail always asks for the short form over the wire.
// ch3 0x10 REQUESTCREATESHIP / 0x38 REQUESTNEWCHARINFO / 0x42 SKIP_AUTOSAVE take the
// session only.
// ===========================================================================
static bool DeliverCharacterInfoReq(const FPacketBase*, IServer* Server, unsigned int SessionId) {
    Server->CharacterInfoReq(SessionId, false);
    return true;
}
static bool DeliverRequestCreateShip(const FPacketBase*, IServer* Server, unsigned int SessionId) {
    Server->RequestCreateShip(SessionId);
    return true;
}
static bool DeliverNewCharacterInfoReq(const FPacketBase*, IServer* Server, unsigned int SessionId) {
    Server->NewCharacterInfoReq(SessionId);
    return true;
}
static bool DeliverCharacterSkipAutosave(const FPacketBase*, IServer* Server, unsigned int SessionId) {
    Server->CharacterSkipAutosave(SessionId);
    return true;
}

// ===========================================================================
// ch3 0x06 SELECTCHARACTER / 0x3a DESTROYCHAR -- the payload IS the CHARACTER_ID, and
// the receiver hands packet+0x20 straight to the slot.
//
// Senders 0x06d8cd60 / 0x06d8ce90 (and 0x06d8db10 for SAVEGAME): `LEA ECX,[pkt+0x20]` /
// `PUSH arg1` / `CALL [->COMMON.DLL::CHARACTER_ID::operator=]` (06d8cd85, 06d8ceb5,
// 06d8db36) -- the imported operator itself, no size test in front. Until 2026-09-24
// the port spelled the copy out with a 0x1ff cap, a forced terminator and a payload-size
// guard, on the stale ground that "rpclocal links only kernel32/user32"; it links
// Common.lib (see rpc_containers.cpp, which already calls this operator), so the retail
// call is used as is.
// ===========================================================================
static bool MarshalCharacterId(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    CHARACTER_ID* Dst = reinterpret_cast<CHARACTER_ID*>(Packet->Payload());
    *Dst = *reinterpret_cast<const CHARACTER_ID*>(Args[0]);
    return true;
}

static bool DeliverCharacterSelect(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->CharacterSelect(*reinterpret_cast<const CHARACTER_ID*>(Packet->Payload()), SessionId);
    return true;
}

static bool DeliverDestroyCharacter(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->DestroyCharacter(*reinterpret_cast<const CHARACTER_ID*>(Packet->Payload()), SessionId);
    return true;
}

// ===========================================================================
// ch3 0x08 REQUESTBASEINFO / 0x09 REQUESTLOCATIONINFO -- sender 0x06d8d500 / 0x06d8d550.
// IServer slots 39 (+0x9c) and 40 (+0xa0), both (uint, uint, bool).
// ===========================================================================
static bool MarshalInfoRequest(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientInfoRequest Wire;
    Wire.Id = Args[0];
    Wire.bFull = (unsigned char)(Args[2] & 0xffu);
    Wire.Pad_0x05[0] = 0;
    Wire.Pad_0x05[1] = 0;
    Wire.Pad_0x05[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverBaseInfoRequest(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientInfoRequest Wire = RpcPayloadRead<FPayloadClientInfoRequest>(Packet, 0);
    Server->BaseInfoRequest(Wire.Id, SessionId, Wire.bFull != 0);
    return true;
}

static bool DeliverLocationInfoRequest(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientInfoRequest Wire = RpcPayloadRead<FPayloadClientInfoRequest>(Packet, 0);
    Server->LocationInfoRequest(Wire.Id, SessionId, Wire.bFull != 0);
    return true;
}

// ===========================================================================
// ch3 0x0c OBJCOLLISION -- sender 0x06d8d7b0, receiver caseD_c. The halves of
// SSPObjCollisionInfo are swapped on the wire and swapped back on arrival.
// ===========================================================================
static bool MarshalObjCollision(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const SSPObjCollisionInfo* Info = reinterpret_cast<const SSPObjCollisionInfo*>(Args[0]);
    FPayloadClientObjCollision Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.Pad_0x06 = 0;
    Wire.TargetObjectId = Info->TargetObjectId;
    Wire.TargetSubObjId = Info->TargetSubObjId;
    Wire.Pad_0x0e = 0;
    Wire.Field_0x10 = Info->Field_0x10;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverObjCollision(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientObjCollision Wire = RpcPayloadRead<FPayloadClientObjCollision>(Packet, 0);
    SSPObjCollisionInfo Info;
    Info.TargetObjectId = Wire.TargetObjectId;
    Info.TargetSubObjId = Wire.TargetSubObjId;
    Info.Pad_0x06 = 0;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x0e = 0;
    Info.Field_0x10 = Wire.Field_0x10;
    Server->SPObjCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x3d BADLANDSOBJCOLLISION -- sender 0x06d8d760, receiver caseD_3d. Same idea,
// a three-field rotation.
// ===========================================================================
static bool MarshalBadLandsObjCollision(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const SSPBadLandsObjCollisionInfo* Info =
        reinterpret_cast<const SSPBadLandsObjCollisionInfo*>(Args[0]);
    FPayloadClientBadLandsObjCollision Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.Field_0x04 = Info->Field_0x08;
    Wire.Pad_0x06 = 0;
    Wire.Field_0x08 = Info->Field_0x00;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverBadLandsObjCollision(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientBadLandsObjCollision Wire =
        RpcPayloadRead<FPayloadClientBadLandsObjCollision>(Packet, 0);
    SSPBadLandsObjCollisionInfo Info;
    Info.Field_0x00 = Wire.Field_0x08;
    Info.ObjectId = Wire.ObjectId;
    Info.Field_0x08 = Wire.Field_0x04;
    Info.Pad_0x0a = 0;
    Server->SPBadLandsObjCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x11 GFGOODSELL / 0x12 GFGOODBUY / 0x3c GFGOODVAPORIZED -- straight struct-head
// copies of three, four and three dwords. IServer slots 46 (+0xb8), 45 (+0xb4) and
// 42 (+0xa8). SGFGoodSellInfo and SGFGoodVaporizedInfo carry no static_assert in the
// shared header because retail never proves their total size; only these three dwords
// are ever touched, and that is exactly what travels.
// ===========================================================================
static bool DeliverGFGoodSell(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordTriple Wire = RpcPayloadRead<FPayloadClientDwordTriple>(Packet, 0);
    SGFGoodSellInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Server->GFGoodSell(Info, SessionId);
    return true;
}

static bool DeliverGFGoodVaporized(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordTriple Wire = RpcPayloadRead<FPayloadClientDwordTriple>(Packet, 0);
    SGFGoodVaporizedInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Server->GFGoodVaporized(Info, SessionId);
    return true;
}

static bool DeliverGFGoodBuy(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordQuad Wire = RpcPayloadRead<FPayloadClientDwordQuad>(Packet, 0);
    SGFGoodBuyInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Info.Field_0x0c = Wire.Field_0x0c;
    Server->GFGoodBuy(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x14 MISSIONRESPONSE -- sender 0x06d8da80, IServer slot 43 (+0xac).
// ===========================================================================
static bool MarshalMissionResponse(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientMissionResponse Wire;
    Wire.Field_0x00 = Args[0];
    Wire.MissionId = Args[1];
    Wire.bAccepted = (unsigned char)(Args[2] & 0xffu);
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverMissionResponse(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientMissionResponse Wire =
        RpcPayloadRead<FPayloadClientMissionResponse>(Packet, 0);
    Server->MissionResponse(Wire.Field_0x00, Wire.MissionId, Wire.bAccepted, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x18 REQADDITEM / 0x1a REQMODIFYITEM -- senders 0x06d8d230 / 0x06d8d300 plus the
// string writers FUN_06d9a160 / FUN_06d9a5f0. IServer slots 30 (+0x78) and 32 (+0x80).
// The packet owns the hardpoint copy, so both are self-contained and get a destructor.
// ===========================================================================
static bool MarshalReqAddItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const char* Hardpoint = reinterpret_cast<const char*>(Args[1]);
    const unsigned int Length = RpcCh3StringLength(Hardpoint) + 1u;
    FPayloadClientReqAddItem Wire;
    Wire.ArchId = Args[0];
    Wire.Count = (int)Args[2];
    Wire.HardpointSize = 0u;
    Wire.Health = *reinterpret_cast<const float*>(&Args[3]);
    Wire.bMounted = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // FUN_06d9a160(hardpoint, strlen + 1) (06d8d28d): its own guard, then the copy and the
    // size store (06d9a17c / 06d9a194).
    Wire.Hardpoint = nullptr;
    if (Length != 0 && Hardpoint != nullptr) {
        Wire.Hardpoint = static_cast<char*>(RpcCh3CopyBlock(Hardpoint, Length));
        Wire.HardpointSize = Length;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverReqAddItem(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientReqAddItem Wire = RpcPayloadRead<FPayloadClientReqAddItem>(Packet, 0);
    Server->ReqAddItem(Wire.ArchId, Wire.Hardpoint, Wire.Count, Wire.Health,
                       Wire.bMounted, SessionId);
    return true;
}

static bool MarshalReqModifyItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const char* Hardpoint = reinterpret_cast<const char*>(Args[1]);
    const unsigned int Length = RpcCh3StringLength(Hardpoint) + 1u;
    FPayloadClientReqModifyItem Wire;
    Wire.Count = (int)Args[2];
    Wire.HardpointSize = 0u;
    Wire.EquipId = Args[0] & 0xffffu;
    Wire.Health = *reinterpret_cast<const float*>(&Args[3]);
    Wire.bMounted = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // FUN_06d9a5f0(hardpoint, strlen + 1): its own guard, then the copy and the size
    // store (06d9a60c / 06d9a624).
    Wire.Hardpoint = nullptr;
    if (Length != 0 && Hardpoint != nullptr) {
        Wire.Hardpoint = static_cast<char*>(RpcCh3CopyBlock(Hardpoint, Length));
        Wire.HardpointSize = Length;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverReqModifyItem(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientReqModifyItem Wire = RpcPayloadRead<FPayloadClientReqModifyItem>(Packet, 0);
    // Retail reads payload+0x08 with MOVZX -- a word, not the dword the sender wrote.
    Server->ReqModifyItem((unsigned short)(Wire.EquipId & 0xffffu), Wire.Hardpoint, Wire.Count,
                          Wire.Health, Wire.bMounted, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x19 REQREMOVEITEM -- sender 0x06d8d2b0, IServer slot 31 (+0x7c).
// The sender zero-extends the ushort into a full dword; the receiver reads it back
// as a word (MOVZX). Both halves reproduced.
// ===========================================================================
static bool MarshalReqRemoveItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordPair Wire;
    Wire.Field_0x00 = Args[0] & 0xffffu;
    Wire.Field_0x04 = Args[1];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverReqRemoveItem(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordPair Wire = RpcPayloadRead<FPayloadClientDwordPair>(Packet, 0);
    Server->ReqRemoveItem((unsigned short)(Wire.Field_0x00 & 0xffffu), (int)Wire.Field_0x04, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x1e SAVEGAME -- sender 0x06d8db10 plus the wide-string writer FUN_06d9aab0.
// IServer slot 58 (+0xe8). The packet owns the description copy.
// ===========================================================================
static bool MarshalSaveGame(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8db36: CHARACTER_ID::operator= into packet+0x20, no size test in front.
    MarshalCharacterId(Packet, Args, ArgCount);
    // 06d8db41 wcslen (no pointer test) / 06d8db4e INC EAX / 06d8db51 FUN_06d9aab0.
    const wchar_t* Description = reinterpret_cast<const wchar_t*>(Args[1]);
    const unsigned int Count = RpcCh3WideLength(Description) + 1u;
    // FUN_06d9aab0(description, wcslen + 1) (06d8db51): its own guard, then the copy
    // (06d9aacf) and the count store (06d9aaef).
    wchar_t* Block = nullptr;
    unsigned int StoredCount = 0u;
    if (Count != 0 && Description != nullptr) {
        Block = static_cast<wchar_t*>(RpcCh3CopyBlock(Description, Count * 2u));
        StoredCount = Count;
    }
    RpcPayloadWrite(Packet, 0x200, Block);
    return RpcPayloadWrite(Packet, 0x204, StoredCount);
}

static bool DeliverSaveGame(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const wchar_t* Description = RpcPayloadRead<wchar_t*>(Packet, 0x200);
    Server->SaveGame(*reinterpret_cast<const CHARACTER_ID*>(Packet->Payload()), Description, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x20 MINEASTEROID -- sender 0x06d8dcf0, IServer slot 62 (+0xf8). The receiver
// rebuilds the Vector on its own stack between the first and the last three dwords.
// ===========================================================================
static bool MarshalMineAsteroid(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const Vector* Position = reinterpret_cast<const Vector*>(Args[1]);
    FPayloadClientMineAsteroid Wire;
    Wire.SystemId = Args[0];
    Wire.PositionX = Position->x;
    Wire.PositionY = Position->y;
    Wire.PositionZ = Position->z;
    Wire.LootId = Args[2];
    Wire.SecondaryLoot = Args[3];
    Wire.Quantity = Args[4];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverMineAsteroid(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientMineAsteroid Wire = RpcPayloadRead<FPayloadClientMineAsteroid>(Packet, 0);
    Vector Position;
    Position.x = Wire.PositionX;
    Position.y = Wire.PositionY;
    Position.z = Wire.PositionZ;
    Server->MineAsteroid(Wire.SystemId, Position, Wire.LootId, Wire.SecondaryLoot,
                         Wire.Quantity, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x24 DOCK -- sender 0x06d8dbb0 dereferences its FIRST argument (uint const&).
// IServer slot 91 (+0x16c) Dock(uint const&, uint const&); the receiver passes
// packet+0x20 for the first and the ADDRESS of its own session local for the second.
// ===========================================================================
static bool DeliverDock(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    Server->Dock(*reinterpret_cast<const unsigned int*>(Packet->Payload()), SessionId);
    return true;
}

// ===========================================================================
// ch3 0x27 TRADERESPONSE -- sender 0x06d8dad0 via FUN_06d97b00 (length first, block
// second). IServer slot 44 (+0xb0) TradeResponse(uchar const*, int, uint).
// ===========================================================================
static bool MarshalTradeResponse(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientBlob Wire;
    Wire.Size = Args[1];
    // FUN_06d97b00: bare operator new(len) (06d97b0b) and the copy, no guard at all.
    Wire.Buffer = RpcCh3CopyBlock(reinterpret_cast<const void*>(Args[0]), Args[1]);
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverTradeResponse(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientBlob Wire = RpcPayloadRead<FPayloadClientBlob>(Packet, 0);
    Server->TradeResponse(static_cast<const unsigned char*>(Wire.Buffer), (int)Wire.Size, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x2b CARGOSCAN -- sender 0x06d8ebd0 dereferences both `uint const&` arguments.
// IServer slot 65 (+0x104); the receiver passes packet+0x20 and packet+0x24 as the two
// references, so the packet's own storage backs them.
// ===========================================================================
static bool MarshalSPScanCargo(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordPair Wire;
    Wire.Field_0x00 = *reinterpret_cast<const unsigned int*>(Args[0]);
    Wire.Field_0x04 = *reinterpret_cast<const unsigned int*>(Args[1]);
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSPScanCargo(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const unsigned int* Fields = reinterpret_cast<const unsigned int*>(Packet->Payload());
    Server->SPScanCargo(Fields[0], Fields[1], SessionId);
    return true;
}

// ===========================================================================
// ch3 0x2f SETMANEUVER -- sender 0x06d8e670, IServer slot 66 (+0x108)
// SetManeuver(uint Session, XSetManeuver const&): the session comes FIRST.
// ===========================================================================
static bool MarshalSetManeuver(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XSetManeuver* Info = reinterpret_cast<const XSetManeuver*>(Args[1]);
    FPayloadClientDwordTriple Wire;
    Wire.Field_0x00 = Info->Field_0x00;
    Wire.Field_0x04 = Info->ObjectId;
    Wire.Field_0x08 = Info->Field_0x08;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetManeuver(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordTriple Wire = RpcPayloadRead<FPayloadClientDwordTriple>(Packet, 0);
    XSetManeuver Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.ObjectId = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Server->SetManeuver(SessionId, Info);
    return true;
}

// ===========================================================================
// ch3 0x31 REQUEST_EVENT / 0x32 REQUEST_CANCEL -- senders 0x06d8eaf0 / 0x06d8eb60,
// IServer slots 60 (+0xf0) and 61 (+0xf4). Retail truncates the int event type and the
// ulong trailing argument to ONE BYTE each on the way out and sign/zero-extends them
// back on the way in. That loss is retail's, not the port's.
// ===========================================================================
static bool MarshalRequestEvent(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientRequestEvent Wire;
    Wire.EventType = (unsigned char)(Args[0] & 0xffu);
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.Field_0x04 = Args[1];
    Wire.Field_0x08 = Args[2];
    Wire.Field_0x0c = Args[3];
    Wire.Field_0x10 = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestEvent(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientRequestEvent Wire = RpcPayloadRead<FPayloadClientRequestEvent>(Packet, 0);
    Server->RequestEvent((int)Wire.EventType, Wire.Field_0x04, Wire.Field_0x08, Wire.Field_0x0c,
                         (unsigned long)Wire.Field_0x10, SessionId);
    return true;
}

static bool MarshalRequestCancel(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientRequestCancel Wire;
    Wire.EventType = (unsigned char)(Args[0] & 0xffu);
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.Field_0x04 = Args[1];
    Wire.Field_0x08 = Args[2];
    Wire.Field_0x0c = (unsigned char)(Args[3] & 0xffu);
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestCancel(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientRequestCancel Wire = RpcPayloadRead<FPayloadClientRequestCancel>(Packet, 0);
    Server->RequestCancel((int)Wire.EventType, Wire.Field_0x04, Wire.Field_0x08,
                          (unsigned long)Wire.Field_0x0c, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x37 COMMCOMPLETE -- sender 0x06d8dd70, IServer slot 63 (+0xfc). The ONLY
// channel-3 receiver that forwards no session id at all: the slot takes four payload
// values and nothing else.
// ===========================================================================
static bool MarshalCommComplete(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordQuad Wire;
    Wire.Field_0x00 = Args[0];
    Wire.Field_0x04 = Args[1];
    Wire.Field_0x08 = Args[2];
    Wire.Field_0x0c = Args[3];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCommComplete(const FPacketBase* Packet, IServer* Server, unsigned int) {
    const FPayloadClientDwordQuad Wire = RpcPayloadRead<FPayloadClientDwordQuad>(Packet, 0);
    Server->CommComplete(Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08,
                         (CommResult)Wire.Field_0x0c);
    return true;
}

// ===========================================================================
// ch3 0x39 CREATENEWCHAR -- sender 0x06d8cdf0, IServer slot 22 (+0x58). As with LOGIN,
// retail's receiver fills a FILE-STATIC SCreateCharacterInfo (.data:0x06dbbb10) and
// passes its address. Reproduced.
// ===========================================================================
static SCreateCharacterInfo GStaticCreateCharacterInfo;   // retail .data:0x06dbbb10

static bool MarshalCreateNewCharacter(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const SCreateCharacterInfo* Info = reinterpret_cast<const SCreateCharacterInfo*>(Args[0]);
    // IServerImpl::CreateNewCharacter 0x06d8cdf0: wcsncpy(window, Info, 0x40) at
    // 06d8ce1f -- 0x40 wide characters out of the 0x18-character Name, exactly as retail
    // asks; it stops reading at the first NUL and pads the rest of the 0x80 bytes -- then
    // the four dwords (06d8ce25..06d8ce4d), window+0x7e = 0 (06d8ce54), and 0x24 dwords
    // into the payload (06d8ce67). Every one of the 0x90 bytes is written.
    FPayloadClientCreateNewChar Wire;
    wcsncpy(Wire.Name, Info->Name, 0x40);
    Wire.Field_0x80 = Info->Field_0x30;
    Wire.Field_0x84 = Info->Field_0x34;
    Wire.Field_0x88 = Info->Field_0x38;
    Wire.Field_0x8c = Info->Field_0x3c;
    Wire.Name[0x3f] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateNewCharacter(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientCreateNewChar Wire = RpcPayloadRead<FPayloadClientCreateNewChar>(Packet, 0);
    // caseD_39 of FUN_06d86a70: an unbounded MSVCRT wcscpy straight into the static's
    // Name (06d86e9a), no clearing first, then the four dwords (06d86ea0..06d86ed3), which
    // land after the string and so overwrite whatever a long name spilled into them.
    wcscpy(GStaticCreateCharacterInfo.Name, Wire.Name);
    GStaticCreateCharacterInfo.Field_0x30 = Wire.Field_0x80;
    GStaticCreateCharacterInfo.Field_0x34 = Wire.Field_0x84;
    GStaticCreateCharacterInfo.Field_0x38 = Wire.Field_0x88;
    GStaticCreateCharacterInfo.Field_0x3c = Wire.Field_0x8c;
    Server->CreateNewCharacter(GStaticCreateCharacterInfo, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x3e LAUNCHCOMPLETE / 0x43 JUMPINCOMPLETE -- senders 0x06d8d8e0 / 0x06d8d930,
// IServer slots 49 (+0xc4) and 50 (+0xc8). BOTH arguments are serialized, so neither
// is the implicit session -- the receiver pushes only the two payload dwords.
// ===========================================================================
static bool MarshalDwordPair(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordPair Wire;
    Wire.Field_0x00 = Args[0];
    Wire.Field_0x04 = Args[1];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverLaunchComplete(const FPacketBase* Packet, IServer* Server, unsigned int) {
    const FPayloadClientDwordPair Wire = RpcPayloadRead<FPayloadClientDwordPair>(Packet, 0);
    Server->LaunchComplete(Wire.Field_0x00, Wire.Field_0x04);
    return true;
}

static bool DeliverJumpInComplete(const FPacketBase* Packet, IServer* Server, unsigned int) {
    const FPayloadClientDwordPair Wire = RpcPayloadRead<FPayloadClientDwordPair>(Packet, 0);
    Server->JumpInComplete(Wire.Field_0x00, Wire.Field_0x04);
    return true;
}

// ===========================================================================
// ch3 0x3f HAIL -- sender 0x06d8d980, IServer slot 51 (+0xcc). All three arguments
// travel; no session is appended.
// ===========================================================================
static bool MarshalDwordTriple(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordTriple Wire;
    Wire.Field_0x00 = Args[0];
    Wire.Field_0x04 = Args[1];
    Wire.Field_0x08 = Args[2];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverHail(const FPacketBase* Packet, IServer* Server, unsigned int) {
    const FPayloadClientDwordTriple Wire = RpcPayloadRead<FPayloadClientDwordTriple>(Packet, 0);
    Server->Hail(Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08);
    return true;
}

// ===========================================================================
// ch3 0x40 REQUEST_USE_ITEM -- sender 0x06d8ec20, IServer slot 56 (+0xe0). Identity
// mapping of SSPUseItem.
// ===========================================================================
static bool MarshalSPRequestUseItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const SSPUseItem* Info = reinterpret_cast<const SSPUseItem*>(Args[0]);
    FPayloadClientUseItem Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.ItemId = Info->ItemId;
    Wire.Count = Info->Count;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSPRequestUseItem(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientUseItem Wire = RpcPayloadRead<FPayloadClientUseItem>(Packet, 0);
    SSPUseItem Info;
    Info.ObjectId = Wire.ObjectId;
    Info.ItemId = Wire.ItemId;
    Info.Count = Wire.Count;
    Server->SPRequestUseItem(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x44 REQINVINCIBILITY -- sender 0x06d8ec80, IServer slot 57 (+0xe4). The sender
// crosses the two byte arguments (reason at +0x04, bool at +0x05) and the receiver
// crosses them back, which is what tells the two bytes apart.
// ===========================================================================
static bool MarshalSPRequestInvincibility(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientInvincibility Wire;
    Wire.ShipId = Args[0];
    Wire.Reason = (unsigned char)(Args[2] & 0xffu);
    Wire.bInvincible = (unsigned char)(Args[1] & 0xffu);
    Wire.Pad_0x06[0] = 0;
    Wire.Pad_0x06[1] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSPRequestInvincibility(const FPacketBase* Packet, IServer* Server, unsigned int SessionId) {
    const FPayloadClientInvincibility Wire = RpcPayloadRead<FPayloadClientInvincibility>(Packet, 0);
    Server->SPRequestInvincibility(Wire.ShipId, Wire.bInvincible,
                                   (InvincibilityReason)Wire.Reason, SessionId);
    return true;
}

// ===========================================================================
// The six debug-only channel-3 types. Retail registers them (so the registry has to
// list them) but the release build has no sender at all, and this dispatcher answers
// each one with an FDUMP naming the source file and line it was raised from. Those
// strings and line numbers are retail's own, copied verbatim out of the listing.
// ===========================================================================
static const char* const kServerPacketHandlerFile =
    "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp";

// Retail raises DACOM's FDUMP through the import slot .rdata:06db311c (06d86b0f,
// 06d86b40, 06d86b70, 06d86be0), severity 0x100001, format .rdata:06db389c -- no
// trailing newline -- and the file string .rdata:06db38b4.
static bool ReportDebugPacket(int Line, const char* Message) {
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kServerPacketHandlerFile, Line, Message);
    return false;
}

static bool DeliverDbgCreateShip(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x26a, "FLPACKET_CLIENT_DBGCREATESHIP encountered in release mode");
}
static bool DeliverDbgLoadSystem(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x217, "FLPACKET_CLIENT_DBGLOADSYSTEM encountered in release mode");
}
static bool DeliverDbgDestroyObject(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x1e4, "FLPACKET_CLIENT_DBGDESTROYOBJECT encountered in Final Release mode");
}
static bool DeliverDbgConsole(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x204, "FLPACKET_CLIENT_DBGCONSOLE encountered in release mode");
}
static bool DeliverDbgFreeSystem(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x227, "FLPACKET_CLIENT_DBGFREESYSTEM encountered in release mode");
}
static bool DeliverDbgRelocateShip(const FPacketBase*, IServer*, unsigned int) {
    return ReportDebugPacket(0x1f5, "FLPACKET_CLIENT_DBGRELOCATE_SHIP encountered in Final Release mode");
}

// ===========================================================================
// The three container types, closed by the serializer in rpc_containers.{h,cpp}.
//
// 0x16 REQEQUIPMENT  -- sender IServerImpl::ReqEquipment       (0x06d8cf90)
//                       receiver FUN_06d87ec0, IServer slot 28 (+0x70)
// 0x17 REQCARGO      -- sender IServerImpl::ReqCargo           (0x06d8d0e0)
//                       receiver FUN_06d880a0, IServer slot 29 (+0x74)
// 0x36 REQCOLLISIONGROUPS
//                    -- sender IServerImpl::ReqCollisionGroups (0x06d8cf50)
//                       receiver FUN_06d8f7d0, IServer slot 27 (+0x6c)
//
// The two REQ*EQUIPMENT senders are the same 107 instructions and differ only in the
// opcode they allocate; their two receivers are the same 153 instructions and differ
// only in the IServer slot they finish on (verified by diffing the two listing ranges:
// the single mismatched instruction is `CALL dword ptr [EDX + 0x70]` vs `+ 0x74`).
//
// Registry agreement, the third reading: REQEQUIPMENT and REQCARGO are 0x24 bytes
// (payload 0x04 = the handle pointer) and REQCOLLISIONGROUPS is 0x2c (payload 0x0c =
// the std::list header itself).
//
// Fourth reading, the one only channel 3 affords: the three slot offsets the dispatcher
// calls -- +0x6c, +0x70, +0x74 -- are slots 27, 28 and 29 of src/include/common_server.h,
// declared there as ReqCollisionGroups(std::list<CollisionGroupDesc> const&, uint),
// ReqEquipment(EquipDescList const&, uint) and ReqCargo(EquipDescList const&, uint).
// Arity and argument kind match what each case pushes: one container reference plus the
// dispatcher's own session id, pushed session-first so the reference is argument one.
//
// The BRIDGE that used to stand here is GONE, and it was not merely unproven -- it was
// a live defect. It cast the caller's container to a module-private mirror:
//
//   * REQEQUIPMENT/REQCARGO cast an `EquipDescList const&` to
//     `std::list<FRpcEquipDescMirror> const&` on the way out and cast a
//     `std::list<FRpcEquipDescMirror>` back to `EquipDescList const&` on the way in.
//     This port's EquipDescList was then `{ std::vector<EquipDesc> Items; unsigned short
//     NextId; }`, NOT a std::list, so both directions read a list header as a vector's
//     begin/end/capacity triple. Now that IClient slot 16 really routes through
//     RpcDispatchToServer, that is a wild pointer walk on the live single-player path,
//     not a silent mismatch.
//   * REQCOLLISIONGROUPS cast `std::list<CollisionGroupDesc>` to
//     `std::list<FRpcCollisionGroupDescMirror>`. Those two ARE layout-identical, so it
//     only ever worked by accident; it is gone too.
//
// The justification -- "rpclocal links no Common.dll" -- was never true of retail and
// has not been true of this port since rpclocal.vcxproj started linking Common.lib.
// Retail imports the EquipDesc ctor, all six setters, make_internal and is_internal BY
// NAME (.rdata:06db3004, 06db3070..06db308c, 06db30d8), and decorates its list import
// `std::list<struct EquipDesc,...>::_Buynode` (06db30ac). Both containers are now the
// real ones. See docs/re/deviations.md.
// ===========================================================================

// Retail marshals from an EquipDescList, whose node chain it walks itself. Args[0] is
// the address of that EquipDescList; since 2026-09-17 IServerImpl::ReqEquipment
// (0x06d8cf90) and ::ReqCargo (0x06d8d0e0) really do call this.
static bool MarshalReqEquipmentCommon(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const EquipDescList& Source = *reinterpret_cast<const EquipDescList*>(Args[0]);

    // 06d8cfcc: the local handle starts null; 06d8cff0..06d8d08a fill it; 06d8d09e
    // FUN_06d8fa90(packet, &handle) deep-copies it into the packet; 06d8d0be
    // FUN_06d91340 tears the local down -- its destructor, which Unwind@06db25f0 also
    // runs on a throw.
    FRpcEquipWireHandle Handle;
    RpcEquipHandleFromList(&Handle, Source);
    RpcPacketEquipAssign(Packet, &Handle);
    return true;
}

static bool DeliverReqEquipmentCommon(const FPacketBase* Packet, IServer* Server,
                                      unsigned int SessionId, bool bCargo) {
    // retail FUN_06d87ec0: build an empty EquipDescList, pull the wire list out of the
    // packet, refill the EquipDescList from it, make the one IServer call, then tear
    // both temporaries down. Both are objects with destructors (FuncInfo_06db6bf8):
    // state 0 is the list (Unwind@06db24d0, COMMON EquipDescList::~EquipDescList),
    // state 1 the handle (Unwind@06db24d9, FUN_06d91340).
    //
    // 06d87ee4/06d87eec: the EquipDescList is built first, empty.
    EquipDescList List;
    // 06d87efe: then the handle, null; 06d87f10 FUN_06d95d20 fills it.
    FRpcEquipWireHandle Handle;
    RpcPacketEquipExtract(Packet, &Handle);
    RpcEquipListFromHandle(List, &Handle);

    // retail 06d88015 `CALL dword ptr [EDX + 0x70]` / 06d881f5 `+ 0x74`, with the
    // list's address pushed as argument one and the session id as argument two.
    if (bCargo) {
        Server->ReqCargo(List, SessionId);
    } else {
        Server->ReqEquipment(List, SessionId);
    }

    // 06d88018..06d88021 the handle's destructor, then 06d88026..06d8807b the list's --
    // reverse order of construction, which is what the two members' destructors do.
    return true;
}

static bool DeliverReqEquipment(const FPacketBase* Packet, IServer* Server,
                                unsigned int SessionId) {
    return DeliverReqEquipmentCommon(Packet, Server, SessionId, false);
}

static bool DeliverReqCargo(const FPacketBase* Packet, IServer* Server,
                            unsigned int SessionId) {
    return DeliverReqEquipmentCommon(Packet, Server, SessionId, true);
}

// 0x36 REQCOLLISIONGROUPS. Retail's sender is three calls long -- allocate, splice the
// caller's list into the payload with FUN_06d8f710, push -- and its receiver is one
// instruction: `LEA EAX,[packet+0x20]`, because the payload IS the list object.
static bool MarshalReqCollisionGroups(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const std::list<CollisionGroupDesc>& Source =
        *reinterpret_cast<const std::list<CollisionGroupDesc>*>(Args[0]);

    // 06d8cf72 FUN_06d8f710(packet, Source): retail's three-phase list assignment walks
    // the caller's list directly, and so does the port -- RpcCollisionGroupListAssignRange
    // takes the caller's own iterators; each value travels as 8 raw bytes in two dword
    // moves (06d8f7a9..06d8f7b1), no field decode, no intermediate array.
    // The list is the one the packet constructor built (retail 0x06da10b0, 06da1109); no
    // re-initialisation here. FUN_06d8f710's self-assignment test (06d8f719 `CMP EBP,ECX`)
    // compares the packet's list with the caller's; here the caller's is a different type
    // (this toolchain's std::list), so the two can never be the same object.
    FRpcCollisionGroupList* List = RpcPacketCollisionGroups(Packet);
    RpcCollisionGroupListAssignRange(List, Source.begin(), Source.end());
    return true;
}

static bool DeliverReqCollisionGroups(const FPacketBase* Packet, IServer* Server,
                                      unsigned int SessionId) {
    // retail: `PUSH SessionId ; ECX = packet ; FUN_06d8f7d0 ; PUSH EAX ; CALL [ESI+0x6c]`
    // -- the payload's address goes straight in as the list reference, because retail's
    // std::list layout IS the payload layout. This port's std::list<CollisionGroupDesc>
    // is its own toolchain's container (the No-retail-ABI rule), so the node chain is
    // assigned into one first -- the same seam the create-object header uses
    // (RpcCollisionGroupListFromWire, retail's inline copy at 06d8505c).
    std::list<CollisionGroupDesc> Groups;
    RpcCollisionGroupListFromWire(Groups, RpcPacketCollisionGroups(Packet));
    Server->ReqCollisionGroups(Groups, SessionId);
    return true;
}

// ===========================================================================
// The channel-3 table. Ordered by opcode, not by the retail registrator's order.
// ===========================================================================
const FRpcServerCodec GChannel3Codecs[] = {
    { 3, 0x01, "FLPACKET_CLIENT_LOGIN",                      0x404,
      MarshalLogin,                   DeliverLogin },
    { 3, 0x03, "FLPACKET_CLIENT_MUNCOLLISION",               0x1c,
      MarshalMunCollision,            DeliverMunCollision },
    { 3, 0x04, "FLPACKET_CLIENT_REQUESTLAUNCH",              0x04,
      MarshalCh3DwordAt<0>,           DeliverPlayerLaunch },
    { 3, 0x05, "FLPACKET_CLIENT_REQUESTCHARINFO",            0x00,
      MarshalCh3Empty,                DeliverCharacterInfoReq },
    { 3, 0x06, "FLPACKET_CLIENT_SELECTCHARACTER",            0x200,
      MarshalCharacterId,             DeliverCharacterSelect },
    { 3, 0x07, "FLPACKET_CLIENT_ENTERBASE",                  0x04,
      MarshalCh3DwordAt<0>,           DeliverBaseEnter },
    { 3, 0x08, "FLPACKET_CLIENT_REQUESTBASEINFO",            0x08,
      MarshalInfoRequest,             DeliverBaseInfoRequest },
    { 3, 0x09, "FLPACKET_CLIENT_REQUESTLOCATIONINFO",        0x08,
      MarshalInfoRequest,             DeliverLocationInfoRequest },
    { 3, 0x0b, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE", 0x04,
      MarshalCh3DwordAt<0>,           DeliverSystemSwitchOutComplete },
    { 3, 0x0c, "FLPACKET_CLIENT_OBJCOLLISION",               0x14,
      MarshalObjCollision,            DeliverObjCollision },
    { 3, 0x0d, "FLPACKET_CLIENT_EXITBASE",                   0x04,
      MarshalCh3DwordAt<0>,           DeliverBaseExit },
    { 3, 0x0e, "FLPACKET_CLIENT_ENTERLOCATION",              0x04,
      MarshalCh3DwordAt<0>,           DeliverLocationEnter },
    { 3, 0x0f, "FLPACKET_CLIENT_EXITLOCATION",               0x04,
      MarshalCh3DwordAt<0>,           DeliverLocationExit },
    { 3, 0x10, "FLPACKET_CLIENT_REQUESTCREATESHIP",          0x00,
      MarshalCh3Empty,                DeliverRequestCreateShip },
    { 3, 0x11, "FLPACKET_CLIENT_GFGOODSELL",                 0x0c,
      MarshalCh3StructHead<3>,        DeliverGFGoodSell },
    { 3, 0x12, "FLPACKET_CLIENT_GFGOODBUY",                  0x10,
      MarshalCh3StructHead<4>,        DeliverGFGoodBuy },
    { 3, 0x13, "FLPACKET_CLIENT_GFSELECTOBJECT",             0x04,
      MarshalCh3DwordAt<0>,           DeliverGFObjSelect },
    { 3, 0x14, "FLPACKET_CLIENT_MISSIONRESPONSE",            0x0c,
      MarshalMissionResponse,         DeliverMissionResponse },
    { 3, 0x15, "FLPACKET_CLIENT_REQSHIPARCH",                0x04,
      MarshalCh3DwordAt<0>,           DeliverReqShipArch },
    { 3, 0x16, "FLPACKET_CLIENT_REQEQUIPMENT",               0x04,
      MarshalReqEquipmentCommon,      DeliverReqEquipment },
    { 3, 0x17, "FLPACKET_CLIENT_REQCARGO",                   0x04,
      MarshalReqEquipmentCommon,      DeliverReqCargo },
    { 3, 0x18, "FLPACKET_CLIENT_REQADDITEM",                 0x18,
      MarshalReqAddItem,              DeliverReqAddItem },
    { 3, 0x19, "FLPACKET_CLIENT_REQREMOVEITEM",              0x08,
      MarshalReqRemoveItem,           DeliverReqRemoveItem },
    { 3, 0x1a, "FLPACKET_CLIENT_REQMODIFYITEM",              0x18,
      MarshalReqModifyItem,           DeliverReqModifyItem },
    { 3, 0x1b, "FLPACKET_CLIENT_REQSETCASH",                 0x04,
      MarshalCh3DwordAt<0>,           DeliverReqSetCash },
    { 3, 0x1c, "FLPACKET_CLIENT_REQCHANGECASH",              0x04,
      MarshalCh3DwordAt<0>,           DeliverReqChangeCash },
    { 3, 0x1e, "FLPACKET_CLIENT_SAVEGAME",                   0x208,
      MarshalSaveGame,                DeliverSaveGame },
    { 3, 0x20, "FLPACKET_CLIENT_MINEASTEROID",               0x1c,
      MarshalMineAsteroid,            DeliverMineAsteroid },
    { 3, 0x22, "FLPACKET_CLIENT_DBGCREATESHIP",              0x70,
      nullptr,                    DeliverDbgCreateShip },
    { 3, 0x23, "FLPACKET_CLIENT_DBGLOADSYSTEM",              0x08,
      nullptr,                        DeliverDbgLoadSystem },
    { 3, 0x24, "FLPACKET_CLIENT_DOCK",                       0x04,
      MarshalCh3DwordByRefAt<0>,      DeliverDock },
    { 3, 0x25, "FLPACKET_CLIENT_DBGDESTROYOBJECT",           0x08,
      nullptr,                        DeliverDbgDestroyObject },
    { 3, 0x27, "FLPACKET_CLIENT_TRADERESPONSE",              0x08,
      MarshalTradeResponse,           DeliverTradeResponse },
    { 3, 0x2b, "FLPACKET_CLIENT_CARGOSCAN",                  0x08,
      MarshalSPScanCargo,             DeliverSPScanCargo },
    { 3, 0x2d, "FLPACKET_CLIENT_DBGCONSOLE",                 0x84,
      nullptr,                        DeliverDbgConsole },
    { 3, 0x2e, "FLPACKET_CLIENT_DBGFREESYSTEM",              0x00,
      nullptr,                        DeliverDbgFreeSystem },
    { 3, 0x2f, "FLPACKET_CLIENT_SETMANEUVER",                0x0c,
      MarshalSetManeuver,             DeliverSetManeuver },
    { 3, 0x30, "FLPACKET_CLIENT_DBGRELOCATE_SHIP",           0x10,
      nullptr,                        DeliverDbgRelocateShip },
    { 3, 0x31, "FLPACKET_CLIENT_REQUEST_EVENT",              0x14,
      MarshalRequestEvent,            DeliverRequestEvent },
    { 3, 0x32, "FLPACKET_CLIENT_REQUEST_CANCEL",             0x10,
      MarshalRequestCancel,           DeliverRequestCancel },
    { 3, 0x35, "FLPACKET_CLIENT_INTERFACEITEMUSED",          0x04,
      MarshalCh3DwordAt<1>,           DeliverInterfaceItemUsed },
    { 3, 0x36, "FLPACKET_CLIENT_REQCOLLISIONGROUPS",         0x0c,
      MarshalReqCollisionGroups,      DeliverReqCollisionGroups },
    { 3, 0x37, "FLPACKET_CLIENT_COMMCOMPLETE",               0x10,
      MarshalCommComplete,            DeliverCommComplete },
    { 3, 0x38, "FLPACKET_CLIENT_REQUESTNEWCHARINFO",         0x00,
      MarshalCh3Empty,                DeliverNewCharacterInfoReq },
    { 3, 0x39, "FLPACKET_CLIENT_CREATENEWCHAR",              0x90,
      MarshalCreateNewCharacter,      DeliverCreateNewCharacter },
    { 3, 0x3a, "FLPACKET_CLIENT_DESTROYCHAR",                0x200,
      MarshalCharacterId,             DeliverDestroyCharacter },
    { 3, 0x3b, "FLPACKET_CLIENT_REQHULLSTATUS",              0x04,
      MarshalCh3DwordAt<0>,           DeliverReqHullStatus },
    { 3, 0x3c, "FLPACKET_CLIENT_GFGOODVAPORIZED",            0x0c,
      MarshalCh3StructHead<3>,        DeliverGFGoodVaporized },
    { 3, 0x3d, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION",       0x0c,
      MarshalBadLandsObjCollision,    DeliverBadLandsObjCollision },
    { 3, 0x3e, "FLPACKET_CLIENT_LAUNCHCOMPLETE",             0x08,
      MarshalDwordPair,               DeliverLaunchComplete },
    { 3, 0x3f, "FLPACKET_CLIENT_HAIL",                       0x0c,
      MarshalDwordTriple,             DeliverHail },
    { 3, 0x40, "FLPACKET_CLIENT_REQUEST_USE_ITEM",           0x08,
      MarshalSPRequestUseItem,        DeliverSPRequestUseItem },
    { 3, 0x41, "FLPACKET_CLIENT_ABORT_MISSION",              0x04,
      MarshalCh3DwordAt<1>,           DeliverAbortMission },
    { 3, 0x42, "FLPACKET_CLIENT_SKIP_AUTOSAVE",              0x00,
      MarshalCh3Empty,                DeliverCharacterSkipAutosave },
    { 3, 0x43, "FLPACKET_CLIENT_JUMPINCOMPLETE",             0x08,
      MarshalDwordPair,               DeliverJumpInComplete },
    { 3, 0x44, "FLPACKET_CLIENT_REQINVINCIBILITY",           0x08,
      MarshalSPRequestInvincibility,  DeliverSPRequestInvincibility },
    { 3, 0x45, "FLPACKET_CLIENT_MISSIONSAVEB",               0x04,
      MarshalCh3DwordAt<1>,           DeliverMissionSaveB },
    { 3, 0x46, "FLPACKET_CLIENT_REQDIFFICULTYSCALE",         0x04,
      MarshalCh3DwordAt<0>,           DeliverReqDifficultyScale },
    { 3, 0x47, "FLPACKET_CLIENT_RTCDONE",                    0x04,
      MarshalCh3DwordAt<1>,           DeliverRTCDone },
};

const unsigned int GChannel3CodecCount =
    (unsigned int)(sizeof(GChannel3Codecs) / sizeof(GChannel3Codecs[0]));

const FRpcServerCodec* RpcFindServerCodec(unsigned int Channel, unsigned int Opcode) {
    if (Channel != 3u) {
        return nullptr;
    }
    for (unsigned int i = 0; i < GChannel3CodecCount; ++i) {
        if (GChannel3Codecs[i].Opcode == (unsigned short)Opcode) {
            return &GChannel3Codecs[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// AddTradeEquip / DelTradeEquip (IServer slots 83 and 84, +0x14c / +0x150) take
// `EquipDesc const&`.
//
// CORRECTED 2026-09-17: a hand-rolled vtable call used to stand here, justified by
// "rpclocal links only kernel32 and user32, so EquipDesc is not a complete type". That
// wall was our own build's, never retail's -- retail rpclocal imports COMMON.DLL and
// calls EquipDesc::EquipDesc / set_arch_id / set_count / set_status / set_id by name in
// exactly these two cases (06d878e7..06d87941 and 06d8794c..06d87998). The project links
// Common.lib now, so both are ordinary virtual calls taking a real EquipDesc, built with
// the same four (three, for Del) setters in retail's own order.
// ---------------------------------------------------------------------------

// ===========================================================================
// The channel-1 half of FUN_06d86a70: the same 21 packet types channel 1 already
// carries the OTHER way, delivered into IServer instead of IClient.
//
// Every wire struct here is the one rpc_payload_ch1.cpp already proved; only the
// receiving side is new. The slot each case calls was cross-checked against
// src/include/common_server.h -- all 27 call sites (19 plain cases plus the eight
// PLAYER_TRADE sub-types) hit a slot whose declared arity matches what is pushed.
//
// UPDATED 2026-09-17: these packets DO occur now. IServerImpl serializes every one of its
// 81 retail senders (src/rpclocal/rpclocal.cpp), so this branch is live. Five channel-1
// types turned out to need an IServer-direction marshaller of their own, because the two
// directions do not write the same payload -- see the block comment above IServerImpl.
// ===========================================================================
static bool DispatchChannel1ToServer(const FPacketBase* Packet, IServer* Server,
                                     IChatServer* ChatServer, unsigned int SessionId) {
    switch (Packet->GetOpcode()) {
    case 0x01: {   // UPDATEOBJECT -> slot 52 SPObjUpdate
        const FPayloadCommonUpdateObject Wire =
            RpcPayloadRead<FPayloadCommonUpdateObject>(Packet, 0);
        SSPObjUpdateInfo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.Orientation = Wire.Orientation;
        Info.Position = Wire.Position;
        // Retail widens the wire float into the struct's double with FLD/FSTP.
        Info.Timestamp = (double)Wire.Field_0x24;
        // Retail copies packet+0x48 as a plain dword into the struct's +0x28.
        Info.Field_0x28 = *reinterpret_cast<const unsigned int*>(&Wire.Field_0x28);
        Info.Field_0x2c = Wire.Field_0x00;
        Info.Pad_0x2d[0] = 0;
        Info.Pad_0x2d[1] = 0;
        Info.Pad_0x2d[2] = 0;
        Server->SPObjUpdate(Info, SessionId);
        return true;
    }
    case 0x02: {   // FIREWEAPON -> slot 0
        const FPayloadCommonFireWeapon Wire = RpcPayloadRead<FPayloadCommonFireWeapon>(Packet, 0);
        // Retail caseD_2 0x06d87583..0x06d87712: a local XFireWeaponInfo, +0x00..+0x0c from
        // payload +0x00..+0x0c, then the inlined vector operator= from payload +0x10
        // (0x06d875ad..0x06d876cb) -- a COPY of the elements, never the packet's block --
        // IServer slot 0 (0x06d876e4), and the local vector freed (0x06d876ff..0x06d8770a).
        XFireWeaponInfo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TargetPos = Wire.TargetPos;
        const unsigned int Count = Wire.Hardpoints.Count();
        if (Count != 0) {
            Info.Hardpoints.assign(Wire.Hardpoints.Begin, Wire.Hardpoints.Begin + Count);
        }
        Server->FireWeapon(SessionId, Info);
        return true;
    }
    case 0x04: {   // SETTARGET -> slot 4. The wire packs both ids first, XSetTarget interleaves.
        const FPayloadCommonSetTarget Wire = RpcPayloadRead<FPayloadCommonSetTarget>(Packet, 0);
        XSetTarget Info;
        Info.ObjectId = Wire.ObjectId;
        Info.SubObjId = Wire.SubObjId;
        Info.Pad_0x06 = 0;
        Info.TargetObjectId = Wire.TargetObjectId;
        Info.TargetSubObjId = Wire.TargetSubObjId;
        Info.Pad_0x0e = 0;
        Server->SetTarget(SessionId, Info);
        return true;
    }
    case 0x05: {   // CHATMSG -> IChatServer slot 0. Retail: SubmitChat(Session & 0xffff,
                   // payload+0x08, payload+0x0c, payload+0x00, -1).
        if (ChatServer == nullptr) {
            return false;
        }
        const FPayloadCommonChatMsg Wire = RpcPayloadRead<FPayloadCommonChatMsg>(Packet, 0);
        const CHAT_ID Sender(SessionId & 0xffffu);
        const CHAT_ID Target(Wire.Field_0x00);
        ChatServer->SubmitChat(Sender, Wire.Size, Wire.Buffer, Target, -1);
        return true;
    }
    case 0x08: {   // ACTIVATEEQUIP -> slot 1
        const FPayloadCommonActivateEquip Wire =
            RpcPayloadRead<FPayloadCommonActivateEquip>(Packet, 0);
        XActivateEquip Info;
        Info.ObjectId = Wire.ObjectId;
        Info.EquipId = Wire.EquipId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.Pad_0x07 = 0;
        Server->ActivateEquip(SessionId, Info);
        return true;
    }
    case 0x0e: {   // ACTIVATECRUISE -> slot 2
        const FPayloadCommonActivateCruise Wire =
            RpcPayloadRead<FPayloadCommonActivateCruise>(Packet, 0);
        XActivateCruise Info;
        Info.ObjectId = Wire.ObjectId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.Field_0x05 = (unsigned char)(Wire.Field_0x05 != 0 ? 1 : 0);
        Info.Pad_0x06[0] = 0;
        Info.Pad_0x06[1] = 0;
        Server->ActivateCruise(SessionId, Info);
        return true;
    }
    case 0x0f: {   // GOTRADELANE -> slot 6
        const FPayloadCommonGoTradelane Wire = RpcPayloadRead<FPayloadCommonGoTradelane>(Packet, 0);
        XGoTradelane Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TradelaneRing1 = Wire.TradelaneRing1;
        Info.TradelaneRing2 = Wire.TradelaneRing2;
        Info.Field_0x0c = (Wire.Field_0x0c != 0);
        Info.Pad_0x0d[0] = 0;
        Info.Pad_0x0d[1] = 0;
        Info.Pad_0x0d[2] = 0;
        Server->GoTradelane(SessionId, Info);
        return true;
    }
    case 0x10: {   // STOPTRADELANE -> slot 7: ship, entrance and exit from the
                   // IServerImpl sender 06d8e770; SessionId comes from this dispatcher.
        const FPayloadCommonStopTradelane Wire =
            RpcPayloadRead<FPayloadCommonStopTradelane>(Packet, 0);
        Server->StopTradelane(SessionId, Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08);
        return true;
    }
    case 0x11: {   // SET_WEAPON_GROUP -> slot 70
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->SetWeaponGroup(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x12: {   // PLAYER_TRADE -> the eight trade slots, picked by the bitmask at +0x00
        const FPayloadCommonPlayerTrade Wire =
            RpcPayloadRead<FPayloadCommonPlayerTrade>(Packet, 0);
        switch (Wire.SubType) {
        case 0x01:
            Server->InitiateTrade(SessionId, Wire.Field_0x08);
            return true;
        case 0x02:
            Server->TerminateTrade(SessionId, (int)Wire.Field_0x04);
            return true;
        case 0x04:
            Server->SetTradeMoney(SessionId, Wire.Field_0x0c);
            return true;
        case 0x08: {
            EquipDesc Desc;                                  // 06d878eb, the real ctor
            Desc.set_arch_id(Wire.EquipArchId);              // 06d878f9
            Desc.set_count((int)Wire.EquipCount);            // 06d87907
            Desc.set_status(Wire.EquipStatus);               // 06d87915
            Desc.set_id(Wire.EquipId);                       // 06d87926, MOVZX from a word
            Server->AddTradeEquip(SessionId, Desc);          // 06d87941, IServer+0x14c
            return true;
        }
        case 0x10: {
            // Retail's DelTradeEquip case never calls set_status -- the health stays at
            // the constructor default (06d8794c..06d8797d has only three setters).
            EquipDesc Desc;                                  // 06d87950
            Desc.set_arch_id(Wire.EquipArchId);              // 06d8795e
            Desc.set_count((int)Wire.EquipCount);            // 06d8796c
            Desc.set_id(Wire.EquipId);                       // 06d8797d
            Server->DelTradeEquip(SessionId, Desc);          // 06d87998, IServer+0x150
            return true;
        }
        case 0x20:
            Server->RequestTrade(SessionId, Wire.Field_0x08);
            return true;
        case 0x40:
            Server->AcceptTrade(SessionId, Wire.Field_0x10 != 0);
            return true;
        case 0x80:
            // Retail quirk, reproduced: this is the ONE sub-type that does not forward
            // the dispatcher's session id. StopTradeRequest gets payload+0x08 instead.
            Server->StopTradeRequest(Wire.Field_0x08);
            return true;
        default:
            return false;
        }
    }
    case 0x13: {   // SET_VISITED_STATE -> slot 71
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->SetVisitedState(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x14: {   // JETTISONCARGO -> slot 8
        const FPayloadCommonJettisonCargo Wire =
            RpcPayloadRead<FPayloadCommonJettisonCargo>(Packet, 0);
        XJettisonCargo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.ItemId = Wire.ItemId;
        Info.Pad_0x06 = 0;
        Info.Count = Wire.Count;
        Server->JettisonCargo(SessionId, Info);
        return true;
    }
    case 0x15: {   // ACTIVATETHRUSTERS -> slot 3
        const FPayloadCommonActivateThrusters Wire =
            RpcPayloadRead<FPayloadCommonActivateThrusters>(Packet, 0);
        XActivateThrusters Info;
        Info.ObjectId = Wire.ObjectId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.Pad_0x05[0] = 0;
        Info.Pad_0x05[1] = 0;
        Info.Pad_0x05[2] = 0;
        Server->ActivateThrusters(SessionId, Info);
        return true;
    }
    case 0x16: {   // REQUEST_BEST_PATH -> slot 72
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->RequestBestPath(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x17: {   // REQUEST_GROUP_POSITIONS -> slot 75
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->RequestGroupPositions(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x18: {   // REQUEST_PLAYER_STATS -> slot 73
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->RequestPlayerStats(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x19: {   // SET_MISSION_LOG -> slot 76
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->SetMissionLog(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1a: {   // REQUEST_RANK_LEVEL -> slot 78
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->RequestRankLevel(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1b: {   // POP_UP_DIALOG -> slot 74. Retail forwards ONLY payload+0x10; the two
                   // flattened FmtStr blocks are dropped on this side of the wire.
        const FPayloadCommonPopUpDialog Wire =
            RpcPayloadRead<FPayloadCommonPopUpDialog>(Packet, 0);
        Server->PopUpDialog(SessionId, Wire.Field_0x10);
        return true;
    }
    case 0x1c: {   // SET_INTERFACE_STATE -> slot 77
        const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
        Server->SetInterfaceState(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1d: {   // TRACTOROBJECTS -> slot 5
        const FPayloadCommonTractorObjects Wire =
            RpcPayloadRead<FPayloadCommonTractorObjects>(Packet, 0);
        XTractorObjects Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TractorEquipId = Wire.TractorEquipId;
        Info.Pad_0x06 = 0;
        const unsigned int Count = Wire.TargetIds.Count();
        if (Count != 0u) {
            Info.TargetIds.assign(Wire.TargetIds.Begin, Wire.TargetIds.End);
        }
        Server->TractorObjects(SessionId, Info);
        return true;
    }
    default:
        // Retail's default branch: the packet is dropped without a word.
        return false;
    }
}

// ===========================================================================
// RpcDispatchToServer -- the entry point, retail FUN_06d86a70(param_1, packet).
//
// Retail reads the routing header exactly as HandlePacket does, runs the matching
// switch, and then -- on EVERY path, including the ones that dropped the packet --
// releases it through FUN_06d8f4a0. The release stays with the caller here (the pump in
// rpc_client_proxy.cpp already owns it), so this function only routes.
// ===========================================================================
bool RpcDispatchToServer(FPacketBase* Packet, unsigned int SessionId) {
    if (Packet == nullptr) {
        return false;
    }
    IServer* Server = GRealServer;
    if (Server == nullptr) {
        return false;
    }

    const unsigned int Channel = Packet->GetChannel();
    if (Channel == 1u) {
        return DispatchChannel1ToServer(Packet, Server, GRealChatServer, SessionId);
    }
    if (Channel != 3u) {
        // Retail: channel 2 (and anything else) never reaches this dispatcher at all.
        return false;
    }

    const FRpcServerCodec* Codec = RpcFindServerCodec(3u, Packet->GetOpcode());
    if (Codec == nullptr || Codec->Deliver == nullptr) {
        // ch3 0x0a GFREQUESTSHIPINFO is registered but has no case in retail either.
        // It is now the only channel-3 opcode without a codec.
        return false;
    }
    return Codec->Deliver(Packet, Server, SessionId);
}
