// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payloads.h -- per-packet-type payload layouts (the retail wire format).
//
// Ground truth for every layout in this file and in rpc_payload_ch1.cpp is the raw
// listing EXE/Disasm/rpclocal.dll.txt, read from BOTH ends of each packet type:
//
//   * the sender -- the body of the IClient slot that allocates the packet through
//     FUN_06d96300(Serializer, Channel, Opcode, &OutPacket) and then writes the payload
//     from packet+0x20 on;
//   * the receiver -- the matching case inside IServerImpl::HandlePacket (0x06d81170),
//     which reads those same bytes back, rebuilds the argument struct on its own stack
//     and calls exactly one slot of the real IClient (.data:0x06dbbcd8) or of the chat
//     client (.data:0x06dbbcd4).
//
// A third, independent check on every layout is the packet class's own operator new()
// size, already written out in the registry (rpc_packet_registry.cpp): payload size ==
// that size minus the 0x20-byte CDAPacket base. All 21 channel-1 types agree on all
// three readings; the static_asserts below encode that agreement.
//
// What retail does NOT give us: the names of the IClient slots. rpclocal decorates none
// of the 126 methods; src/include/rpc_iclient.h names and types each of them from the
// sender, receiver and caller bodies (2026-10-16). A sender that does `MOV EAX,[A1];
// MOV [pkt+0x20],EAX` proves that its second argument is a pointer and that the first
// dword behind it lands at payload+0x00. The Marshal side of the codecs below reads the
// sender's arguments as that retail stack image -- one dword per argument slot, a
// reference or pointer as its address -- which FLocalClientProxy (rpc_client_proxy.cpp)
// builds explicitly from the typed parameters.
//
// Retail's own first argument (a session/client id) is never forwarded: every receive
// case pushes the literal 1 in its place. That is reproduced verbatim.
#pragma once

#include "rpc_transport.h"
#include "common_server_types.h"
// Retail rpclocal.dll imports FmtStr::flatten / FmtStr::unflatten / ~FmtStr /
// FmtStr::Val::get_flattened_size from COMMON.DLL (import table at .rdata:06db3008,
// 06db3094, 06db30dc, 06db30e0). The port does the same -- see the family note further
// down, next to FPayloadServerSetReputation.
#include "common_text.h"
#include "rpc_iclient_args.h"   // FServerSetReputationInfo, Reputation::Relation
#include "common_misc.h"        // DamageList, the DAMAGEOBJECT payload member

#include <cstddef>
#include <string>
#include <vector>

class IClient;
class IChatClient;

// ===========================================================================
// Codec table entry
// ===========================================================================

// Packs the sender's raw argument dwords into the retail payload. Returns false when the
// arguments cannot be marshalled (a null struct pointer, a payload that is too small).
typedef bool (*FRpcMarshalFn)(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount);

// Rebuilds the retail argument list out of the payload and makes the one call retail's
// HandlePacket case makes. Returns false when the packet cannot be delivered.
typedef bool (*FRpcDeliverFn)(const FPacketBase* Packet, IClient* Client, IChatClient* ChatClient);

struct FRpcPayloadCodec {
    unsigned short Channel;
    unsigned short Opcode;
    const char* Name;              // the retail FLPACKET_* string
    unsigned short PayloadSize;    // registry size minus the 0x20 base
    FRpcMarshalFn Marshal;
    FRpcDeliverFn Deliver;
};

// nullptr when this type's layout is not recovered yet; the caller then falls back to the
// raw-argument-dword blit and reports the gap once.
extern "C" __declspec(dllexport) const FRpcPayloadCodec* RpcFindPayloadCodec(unsigned int Channel, unsigned int Opcode);

// ===========================================================================
// Typed payload access. Unchecked, as retail's own field stores are: the packet is the
// concrete class the registry built, whose size is retail's operator new argument.
// ===========================================================================
template<typename T>
inline bool RpcPayloadWrite(FPacketBase* Packet, unsigned int Offset, const T& Value) {
    unsigned char* Base = Packet->Payload() + Offset;
    const unsigned char* Src = reinterpret_cast<const unsigned char*>(&Value);
    for (unsigned int i = 0; i < (unsigned int)sizeof(T); ++i) {
        Base[i] = Src[i];
    }
    return true;
}

template<typename T>
inline T RpcPayloadRead(const FPacketBase* Packet, unsigned int Offset) {
    T Value;
    unsigned char* Dst = reinterpret_cast<unsigned char*>(&Value);
    const unsigned char* Base = Packet->Payload() + Offset;
    for (unsigned int i = 0; i < (unsigned int)sizeof(T); ++i) {
        Dst[i] = Base[i];
    }
    return Value;
}

// ===========================================================================
// Channel-1 payload structures.
//
// These are the bytes ON THE WIRE, not the argument structs -- for several types the two
// differ (FLPACKET_COMMON_UPDATEOBJECT reorders its fields, FLPACKET_COMMON_SETTARGET
// packs its two 16-bit ids behind the two 32-bit ids). Each one carries the sender and
// receiver addresses it was read from.
// ===========================================================================

// ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT, packet size 0x4c -> payload 0x2c.
// Sender 0x06d897c0 (IClient+0x0d8), receiver caseD_1 at 0x06d83a4a.
struct FPayloadCommonUpdateObject {
    // TODO: semantics unproven. Sender: `MOV this+0x3,byte [A1+0x28]` -> payload+0x00.
    unsigned char Field_0x00;
    unsigned char Pad_0x01[3];
    unsigned int ObjectId;       // +0x04  <- argument struct +0x00
    Vector Position;             // +0x08  <- FUN_06d8f510 (3-dword copier), struct +0x14
    Quaternion Orientation;      // +0x14  <- FUN_06d8f4f0 (4-dword copier), struct +0x04
    // TODO: semantics unproven. The receiver reads both with FLD/FST, so both are floats.
    // The IServer-direction mirror of this struct (SSPObjUpdateInfo in
    // common_server_types.h) narrows a double `Timestamp` into the first of the two and carries an
    // unidentified dword in the second -- but that is a DIFFERENT struct (0x30 bytes, with
    // the double still at +0x20), so the correspondence is a lead, not a proof.
    float Field_0x24;            // +0x24  <- argument struct +0x20
    float Field_0x28;            // +0x28  <- argument struct +0x24
};
static_assert(sizeof(FPayloadCommonUpdateObject) == 0x2c, "UPDATEOBJECT payload 0x2c");

// The struct the sender reads and the receiver rebuilds (0x2c bytes, same fields in a
// different order), FCommonUpdateObjectInfo, is declared once in
// src/include/rpc_iclient_args.h: Server.dll builds it in StarSystem::sp_obj_update
// (FUN_06d0bbc0) from the IServer-side SSPObjUpdateInfo.

// ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON, packet size 0x40 -> payload 0x20.
// Sender 0x06d8b310 (IClient+0x000), receiver case 2 of the channel-1 switch.
// Field for field retail's XFireWeaponInfo layout (the packet class embeds the same VC6
// vector at payload +0x10). The port's XFireWeaponInfo holds a std::vector, so this wire
// mirror keeps the retail triple itself; the hardpoint ELEMENTS are always copied into a
// block the packet owns (freed by the packet dtor FUN_06d9b020), and the receivers copy
// them back out into the argument struct's own std::vector.
struct FPayloadCommonFireWeapon {
    unsigned int ObjectId;                    // +0x00
    Vector TargetPos;                         // +0x04
    FRetailVector<unsigned short> Hardpoints; // +0x10  element stride 2
};
static_assert(sizeof(FPayloadCommonFireWeapon) == 0x20, "FIREWEAPON payload 0x20");

// ch1 op 0x04 FLPACKET_COMMON_SETTARGET, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bd50 (IClient+0x010), receiver caseD_4 at 0x06d838c9.
// NOTE the reorder: the wire puts both 32-bit ids first and the two 16-bit sub-object ids
// behind them, while XSetTarget interleaves them.
struct FPayloadCommonSetTarget {
    unsigned int ObjectId;          // +0x00  <- XSetTarget +0x00
    unsigned int TargetObjectId;    // +0x04  <- XSetTarget +0x08
    unsigned short SubObjId;        // +0x08  <- XSetTarget +0x04
    unsigned short TargetSubObjId;  // +0x0a  <- XSetTarget +0x0c
};
static_assert(sizeof(FPayloadCommonSetTarget) == 0x0c, "SETTARGET payload 0x0c");

// ch1 op 0x05 FLPACKET_COMMON_CHATMSG, packet size 0x30 -> payload 0x10.
// Sender 0x06d8c470 (IChatClient+0x000), receiver caseD_5 at 0x06d83900.
// The sender operator_new()s Size bytes and memcpy()s the caller's buffer into them, so
// the packet owns the message body outright.
struct FPayloadCommonChatMsg {
    // TODO: semantics unproven. Written from the sender's 1st explicit argument; the
    // receiver does NOT forward it (it pushes the literal 1 in its place), so nothing
    // downstream reads it.
    unsigned int Field_0x00;   // +0x00
    // TODO: semantics unproven. 2nd argument; forwarded as the chat slot's 2nd argument.
    unsigned int Field_0x04;   // +0x04
    unsigned int Size;         // +0x08  operator new() argument and memcpy length
    void* Buffer;              // +0x0c  owned by the packet
};
static_assert(sizeof(FPayloadCommonChatMsg) == 0x10, "CHATMSG payload 0x10");

// ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bbb0 (IClient+0x004), receiver case 8. Mirrors XActivateEquip with the
// bool and the 16-bit id swapped round.
struct FPayloadCommonActivateEquip {
    unsigned int ObjectId;     // +0x00  <- XActivateEquip +0x00
    unsigned char bActivate;   // +0x04  <- XActivateEquip +0x06
    unsigned char Pad_0x05;    // +0x05
    unsigned short EquipId;    // +0x06  <- XActivateEquip +0x04
};
static_assert(sizeof(FPayloadCommonActivateEquip) == 0x08, "ACTIVATEEQUIP payload 0x08");

// ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bc10 (IClient+0x008), receiver case 0xe. Same order as XActivateCruise.
struct FPayloadCommonActivateCruise {
    unsigned int ObjectId;     // +0x00
    unsigned char bActivate;   // +0x04
    unsigned char Field_0x05;  // +0x05  TODO: semantics unproven (XActivateCruise +0x05)
    unsigned char Pad_0x06[2]; // +0x06
};
static_assert(sizeof(FPayloadCommonActivateCruise) == 0x08, "ACTIVATECRUISE payload 0x08");

// ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE, packet size 0x30 -> payload 0x10.
// Sender 0x06d8bcb0 (IClient+0x018), receiver case 0xf. Same order as XGoTradelane.
struct FPayloadCommonGoTradelane {
    unsigned int ObjectId;       // +0x00
    unsigned int TradelaneRing1; // +0x04
    unsigned int TradelaneRing2; // +0x08
    unsigned char Field_0x0c;    // +0x0c  TODO: semantics unproven (XGoTradelane +0x0c)
    unsigned char Pad_0x0d[3];   // +0x0d
};
static_assert(sizeof(FPayloadCommonGoTradelane) == 0x10, "GOTRADELANE payload 0x10");

// ch1 op 0x10 FLPACKET_COMMON_STOPTRADELANE, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bd10 (IClient+0x01c), receiver caseD_10 at 0x06d839fe.
// IClient's four-argument slot only serializes the ship id; its two ring fields stay zero.
// The opposite IServerImpl sender 06d8e770 writes all three payload dwords at
// 06d8e791 / 06d8e79c / 06d8e7a7. Both receivers forward all three fields.
struct FPayloadCommonStopTradelane {
    unsigned int Field_0x00;   // +0x00  <- the sender's 2nd argument
    unsigned int Field_0x04;   // +0x04  entrance ring (IServer); zero (IClient)
    unsigned int Field_0x08;   // +0x08  exit ring (IServer); zero (IClient)
};
static_assert(sizeof(FPayloadCommonStopTradelane) == 0x0c, "STOPTRADELANE payload 0x0c");

// The buffer family -- ch1 ops 0x11, 0x13, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1c, packet
// size 0x2c -> payload 0x0c. Senders 0x06d8b4f0 / 0x06d8b540 / 0x06d8b590 / 0x06d8b7a0 /
// 0x06d8b610 / 0x06d8b7f0 / 0x06d8b890 / 0x06d8b840. Every receiver is the same four
// instructions: RealClient->vt[slot](1, payload+0x00, payload+0x04).
//
// Ownership is carried by payload+0x08, and the one reader of it is the class destructor,
// identical in all eight classes (0x13's is FUN_06d9b1a0, the others its siblings at
// 06d9b0e0 / 06d9b260 / 06d9b320 / 06d9b4c0 / 06d9b580 / 06d9b640 / 06d9b700):
//   06d9b1a3  MOV  AL,[ESI + 0x28] / TEST AL,AL / JZ   ; not owned -> leave it
//   06d9b1b0  MOV  EAX,[ESI + 0x20] / TEST EAX,EAX / JZ
//   06d9b1b8  CALL FUN_06db1dc2 (= free)              ; owned -> free the block
//   06d9b1c0  MOV  [ESI + 0x20],0
// The constructors (e.g. 06d9b150) zero both +0x20 and +0x28. The IClient senders:
//   * 0x16 REQUEST_BEST_PATH and 0x18 REQUEST_PLAYER_STATS operator_new(Size), copy the
//     caller's bytes in and set the flag -- the packet owns its copy;
//   * 0x13 / 0x17 / 0x19 / 0x1a / 0x1c store the CALLER's pointer and set the flag
//     (e.g. 06d8b573 `MOV byte ptr [EDX+0x28],1`) -- the packet takes the caller's block
//     over and frees it. Every retail caller in Server.dll hands them a fresh
//     operator_new block and never touches it again;
//   * 0x11 SET_WEAPON_GROUP stores the caller's pointer and leaves the flag at zero --
//     the one member that borrows, so it alone must be delivered before the call returns.
// The wire decoders (0x06da62d0, 0x06da5bc0) set the flag on the block they allocate.
struct FPayloadCommonBuffer {
    void* Buffer;              // +0x00  sender's 2nd argument
    unsigned int Size;         // +0x04  sender's 3rd argument
    unsigned char bOwnsBuffer; // +0x08  the destructor frees Buffer when set
    unsigned char Pad_0x09[3]; // +0x09
};
static_assert(sizeof(FPayloadCommonBuffer) == 0x0c, "buffer-family payload 0x0c");

// ch1 op 0x12 FLPACKET_COMMON_PLAYER_TRADE, packet size 0x44 -> payload 0x24.
// Eight senders, 0x06d8b8e0 / 0x06d8b980 / 0x06d8b930 / 0x06d8b9d0 / 0x06d8ba20 /
// 0x06d8baa0 / 0x06d8bb10 / 0x06d8bb60, all writing the same packet type with a different
// bitmask in payload+0x00; receiver sub-switch at 0x06d8375d. Each sub-type writes only
// the fields it needs and leaves the rest at the constructor's zeros.
struct FPayloadCommonPlayerTrade {
    unsigned int SubType;       // +0x00  1, 2, 4, 8, 0x10, 0x20, 0x40 or 0x80
    unsigned int Field_0x04;    // +0x04  sub-type 2 only. TODO: semantics unproven.
    unsigned int Field_0x08;    // +0x08  every sub-type except 2. TODO: semantics unproven.
    unsigned int Field_0x0c;    // +0x0c  sub-type 4 only. TODO: semantics unproven.
    unsigned char Field_0x10;   // +0x10  sub-type 0x40 only. TODO: semantics unproven.
    unsigned char Pad_0x11[3];  // +0x11
    unsigned int EquipArchId;   // +0x14  sub-types 8 / 0x10  <- EquipDesc +0x04
    unsigned int EquipCount;    // +0x18  sub-types 8 / 0x10  <- EquipDesc +0x14
    float EquipStatus;          // +0x1c  sub-type 8 only     <- EquipDesc +0x10
    unsigned short EquipId;     // +0x20  sub-types 8 / 0x10  <- EquipDesc +0x02
    unsigned short Pad_0x22;    // +0x22
};
static_assert(sizeof(FPayloadCommonPlayerTrade) == 0x24, "PLAYER_TRADE payload 0x24");

// ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bdb0 (IClient+0x020), receiver caseD_14 at 0x06d83a1c. Same order as
// XJettisonCargo.
struct FPayloadCommonJettisonCargo {
    unsigned int ObjectId;    // +0x00
    unsigned short ItemId;    // +0x04
    unsigned short Pad_0x06;  // +0x06
    unsigned int Count;       // +0x08
};
static_assert(sizeof(FPayloadCommonJettisonCargo) == 0x0c, "JETTISONCARGO payload 0x0c");

// ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bc60 (IClient+0x00c), receiver caseD_15 at 0x06d8399c.
struct FPayloadCommonActivateThrusters {
    unsigned int ObjectId;      // +0x00
    unsigned char bActivate;    // +0x04
    unsigned char Pad_0x05[3];  // +0x05
};
static_assert(sizeof(FPayloadCommonActivateThrusters) == 0x08, "ACTIVATETHRUSTERS payload 0x08");

// ch1 op 0x1b FLPACKET_COMMON_POP_UP_DIALOG, packet size 0x34 -> payload 0x14.
// Sender 0x06d8b690 (IClient+0x17c), receiver caseD_1b at 0x06d835e3.
// Retail walks each FmtStr's node list, sizes it, operator_new()s that many bytes and
// calls COMMON.DLL::FmtStr::flatten into them; the receiver calls FmtStr::unflatten back
// out. See rpc_payload_ch1.cpp for why the port cannot do that yet.
struct FPayloadCommonPopUpDialog {
    unsigned int SizeA;     // +0x00  flattened size of the 1st FmtStr
    void* BufferA;          // +0x04  flattened bytes of the 1st FmtStr
    unsigned int SizeB;     // +0x08  flattened size of the 2nd FmtStr
    void* BufferB;          // +0x0c  flattened bytes of the 2nd FmtStr
    unsigned int Field_0x10;// +0x10  the sender's 4th argument. TODO: semantics unproven.
};
static_assert(sizeof(FPayloadCommonPopUpDialog) == 0x14, "POP_UP_DIALOG payload 0x14");

// ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS, packet size 0x38 -> payload 0x18.
// Sender 0x06d8be10 (IClient+0x014), receiver caseD_1d at 0x06d8326a. Same shape as
// XTractorObjects; the id vector's ELEMENTS are copied into the packet.
struct FPayloadCommonTractorObjects {
    unsigned int ObjectId;                  // +0x00
    unsigned short TractorEquipId;          // +0x04
    unsigned short Pad_0x06;                // +0x06
    FRetailVector<unsigned int> TargetIds;  // +0x08  element stride 4
};
static_assert(sizeof(FPayloadCommonTractorObjects) == 0x18, "TRACTOROBJECTS payload 0x18");

// payload 0x0c: ch2 0x3d FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE. The first dword is
// the equipment handle FUN_06d8fa90 hangs off packet+0x20 (see rpc_containers.h); the
// other two fields are written straight by the sender at 0x06d8c076 (a single byte at
// +0x04) and 0x06d8c081 (a dword at +0x08), and read back by caseD_3d at 0x06d82a64
// and 0x06d82a7d. Registry size 0x2c.
struct FPayloadServerRemoteObjectCargoUpdate {
    unsigned int EquipHandle;    // +0x00, owned by the packet
    unsigned char Field_0x04;    // +0x04  TODO: semantics unproven (a mode/flag byte)
    unsigned char Pad_0x05[3];   // +0x05
    unsigned int Field_0x08;     // +0x08  TODO: semantics unproven (an object id)
};
static_assert(sizeof(FPayloadServerRemoteObjectCargoUpdate) == 0x0c,
              "ch2 0x3d payload 0x0c");

// The three channel-1 codecs live in src/rpclocal/rpc_payload_ch1.cpp.
extern const FRpcPayloadCodec GChannel1Codecs[];
extern const unsigned int GChannel1CodecCount;

// ===========================================================================
// Channel-2 payload structures (server -> client).
//
// Recovered exactly like channel 1, from three independent readings:
//   * the sender -- the body of the IClient slot in rpclocal that allocates the packet
//     with FUN_06d96300(Serializer, 2, Opcode, &OutPacket) and writes from packet+0x20;
//   * the receiver -- the matching case of the channel-2 switch inside
//     IServerImpl::HandlePacket (jump table at .text:0x06d83b44, EBP = the packet), which
//     reads those same bytes back and calls exactly one slot of the real IClient
//     (.data:0x06dbbcd8);
//   * the registry size -- payload == the type's operator new() size minus the 0x20 base.
//
// Two channel-2 conventions differ from channel 1 and are reproduced verbatim:
//
//   1. Retail replaces the sender's first argument with a literal 1 on the way out, as on
//      channel 1 -- but for one family of types it passes that 1 BY REFERENCE
//      (LEA EAX,[local] ; MOV [local],1 ; PUSH EAX), because the slot takes uint const&.
//      Those same senders dereference their own first argument before handing it to
//      slot 9 (MOV EAX,[ECX]). Each codec below notes which convention its type uses.
//   2. Four types (0x02, 0x06, 0x0a, 0x21) do not rebuild the argument struct on the
//      receiver's stack at all: retail copies the fields into FILE-STATIC storage in
//      .data and passes the address of that. Reproduced as found -- it is not re-entrant
//      in retail either.
//
// Field names: where the packet carries exactly one value and the retail FLPACKET_* name
// says what it is, the field is named after the packet (SETSHIPARCH -> ShipArchId). Every
// other field keeps Field_0xNN plus its proven primitive width; rpclocal decorates none of
// the IClient methods, so nothing stronger is available.
// ===========================================================================

// --- shared shapes -------------------------------------------------------

// payload 0x04: one dword written by value. Registry size 0x24.
struct FPayloadServerDword {
    unsigned int Field_0x00;
};
static_assert(sizeof(FPayloadServerDword) == 0x04, "ch2 dword payload 0x04");

// payload 0x04: one float (sender FLD/FSTP). Registry size 0x24.
struct FPayloadServerFloat {
    float Field_0x00;
};
static_assert(sizeof(FPayloadServerFloat) == 0x04, "ch2 float payload 0x04");

// payload 0x04: one byte; the packet constructor zeroes packet+0x20 and nothing writes the
// other three. Registry size 0x24.
struct FPayloadServerByte {
    unsigned char Field_0x00;
    unsigned char Pad_0x01[3];
};
static_assert(sizeof(FPayloadServerByte) == 0x04, "ch2 byte payload 0x04");

// payload 0x08: two dwords by value. Registry size 0x28.
struct FPayloadServerDwordPair {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};
static_assert(sizeof(FPayloadServerDwordPair) == 0x08, "ch2 dword-pair payload 0x08");

// payload 0x08: a sized blob the packet OWNS. Written by FUN_06d97b00, which does
// operator new(Size) + REP MOVSD/MOVSB, stores the block at packet+0x24 and the length at
// packet+0x20. The matching packet destructor (e.g. FUN_06d974c0) frees packet+0x24.
struct FPayloadServerBlob {
    unsigned int Size;   // +0x00
    void* Buffer;        // +0x04  owned by the packet
};
static_assert(sizeof(FPayloadServerBlob) == 0x08, "ch2 blob payload 0x08");

// payload 0x08: the same blob with the two fields the other way round. Written by
// FUN_06d90a00 -- byte for byte the mirror of FUN_06d97b00, block at packet+0x20 and
// length at packet+0x24. Only FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT uses it, and its
// receiver reads the fields in the matching (reversed) order, so the difference is real.
struct FPayloadServerBlobReversed {
    void* Buffer;        // +0x00  owned by the packet
    unsigned int Size;   // +0x04
};
static_assert(sizeof(FPayloadServerBlobReversed) == 0x08, "ch2 reversed-blob payload 0x08");

// --- per-type shapes -----------------------------------------------------

// ch2 0x06 FLPACKET_SERVER_DESTROYOBJECT, size 0x28 -> payload 0x08.
// Sender 0x06d89140 (IClient+0x0dc), receiver caseD_6 at 0x06d81f6a.
// The receiver stores into .data:0x06dbb824/0x06dbb828 and passes that address; it writes
// the second field as a zero-extended DWORD, which is byte-identical to writing the byte
// and clearing the three padding bytes.
struct FPayloadServerDestroyObject {
    unsigned int ObjectId;      // +0x00  <- arg struct +0x00
    unsigned char Field_0x04;   // +0x04  <- arg struct +0x04. TODO: semantics unproven.
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadServerDestroyObject) == 0x08, "DESTROYOBJECT payload 0x08");

// FServerDestroyObjectInfo (the argument struct) lives in src/include/rpc_iclient_args.h
// (moved 2026-10-16: Server.dll builds it).

// ch2 0x07 FLPACKET_SERVER_LAUNCH, size 0x48 -> payload 0x28.
// Sender 0x06d8a080 (IClient+0x0f0), receiver caseD_7 at 0x06d81951.
// The wire REORDERS: three leading dwords, then the Vector from arg+0x1c (copied by
// FUN_06d8f510, the 3-dword copier), then the Quaternion from arg+0x0c (FUN_06d8f4f0, the
// 4-dword copier). The receiver puts all ten dwords back exactly where they came from --
// the two mappings are strict inverses, which is what pins this layout down.
struct FPayloadServerLaunch {
    unsigned int Field_0x00;    // +0x00  <- arg +0x00. TODO: semantics unproven.
    unsigned int Field_0x04;    // +0x04  <- arg +0x04. TODO: semantics unproven.
    unsigned int Field_0x08;    // +0x08  <- arg +0x08. TODO: semantics unproven.
    Vector Position;            // +0x0c  <- arg +0x1c (FUN_06d8f510)
    Quaternion Orientation;     // +0x18  <- arg +0x0c (FUN_06d8f4f0)
};
static_assert(sizeof(FPayloadServerLaunch) == 0x28, "LAUNCH payload 0x28");

// FServerLaunchInfo (the IClient +0x0f0 argument struct) lives in
// src/include/rpc_iclient_args.h: Server.dll's IServerImpl::PlayerLaunch builds it.

// ch2 0x0a FLPACKET_SERVER_ACTIVATEOBJECT, size 0x2c -> payload 0x0c.
// Sender 0x06d89770 (IClient+0x0e0), receiver caseD_a at 0x06d823ab (statics
// .data:0x06dbb8f4 / 0x06dbb8f8 / 0x06dbb8fc). The payload IS the argument
// struct Server.dll builds, so FPayloadServerActivateObject is declared once,
// in src/include/rpc_iclient_args.h (2026-09-24, claude:srv-sys).

// ch2 0x0b FLPACKET_SERVER_LAND, size 0x2c -> payload 0x0c. Sender 0x06d8a990
// (IClient+0x0ec), receiver caseD_b at 0x06d8237c. Three dwords, order preserved.
struct FPayloadServerThreeDwords {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};
static_assert(sizeof(FPayloadServerThreeDwords) == 0x0c, "three-dword payload 0x0c");

// ch2 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP, size 0x28 -> payload 0x08.
// Sender 0x06d8a130 (IClient+0x0f4, three arguments), receiver caseD_27 at 0x06d819b1.
struct FPayloadServerRequestCreateShipResp {
    unsigned char Field_0x00;   // +0x00  <- 2nd argument, truncated to a byte
    unsigned char Pad_0x01[3];
    unsigned int Field_0x04;    // +0x04  <- 3rd argument
};
static_assert(sizeof(FPayloadServerRequestCreateShipResp) == 0x08, "REQUESTCREATESHIPRESP payload 0x08");

// ch2 0x2c FLPACKET_SERVER_CREATEMINE, size 0x68 -> payload 0x48.
// Sender 0x06d8a180 (IClient+0x0c4), receiver caseD_2c at 0x06d81ac1.
// Eighteen fields; the wire swaps the two four-dword groups at arg+0x10 and arg+0x1c, and
// carries arg+0x0c as a WORD (the receiver reads a word back and leaves the upper half of
// its local untouched -- retail's own gap, reproduced).
struct FPayloadServerCreateMine {
    unsigned int Field_0x00;    // +0x00  <- arg +0x00
    unsigned int Field_0x04;    // +0x04  <- arg +0x04
    unsigned int Field_0x08;    // +0x08  <- arg +0x08
    unsigned short Field_0x0c;  // +0x0c  <- arg +0x0c (word only)
    unsigned short Pad_0x0e;
    unsigned int Field_0x10;    // +0x10  <- arg +0x1c
    unsigned int Field_0x14;    // +0x14  <- arg +0x20
    unsigned int Field_0x18;    // +0x18  <- arg +0x24
    unsigned int Field_0x1c;    // +0x1c  <- arg +0x28
    unsigned int Field_0x20;    // +0x20  <- arg +0x10
    unsigned int Field_0x24;    // +0x24  <- arg +0x14
    unsigned int Field_0x28;    // +0x28  <- arg +0x18
    unsigned int Field_0x2c;    // +0x2c  <- arg +0x2c
    unsigned int Field_0x30;    // +0x30  <- arg +0x30
    unsigned int Field_0x34;    // +0x34  <- arg +0x34
    unsigned int Field_0x38;    // +0x38  <- arg +0x38
    unsigned int Field_0x3c;    // +0x3c  <- arg +0x3c
    unsigned int Field_0x40;    // +0x40  <- arg +0x40
    unsigned int Field_0x44;    // +0x44  <- arg +0x44
};
static_assert(sizeof(FPayloadServerCreateMine) == 0x48, "CREATEMINE payload 0x48");

// The argument struct is Server.dll's FServerCreateProjectileInfo (server_createinfo.h,
// built by FUN_06d07c80); the private mirror FServerCreateMineInfo of the same 0x48 bytes
// was folded onto it 2026-10-16. Its +0x0e half never crosses the wire.

// ch2 0x2e FLPACKET_SERVER_SETADDITEM, size 0x40 -> payload 0x20.
// Sender 0x06d885c0 (IClient+0x068) under a SEH frame, receiver caseD_2e at 0x06d819ed.
// The sender fans a Common.dll EquipDesc out into scalars and, unless
// EquipDesc::is_internal(), copies the hardpoint STRING into a block the packet owns
// (FUN_06d97470: operator new(len+1), packet+0x3c = block, packet+0x28 = len+1).
// The receiver rebuilds the EquipDesc with the matching setters and calls
// StringAlloc(hardpoint, 0) before set_hardpoint.
struct FPayloadServerSetAddItem {
    unsigned int ArchId;            // +0x00  <- EquipDesc +0x04
    int Count;                      // +0x04  <- EquipDesc +0x14
    unsigned int HardpointSize;     // +0x08  <- strlen(EquipDesc.Hardpoint) + 1, or 0
    unsigned int Field_0x0c;        // +0x0c  <- the slot's 2nd argument. TODO: unproven.
    unsigned int Id;                // +0x10  <- EquipDesc +0x02, zero-extended from a word
    float Health;                   // +0x14  <- EquipDesc +0x10
    unsigned char bEquipped;        // +0x18  <- EquipDesc +0x0c
    unsigned char bMission;         // +0x19  <- EquipDesc +0x18
    unsigned char Pad_0x1a[2];
    char* Hardpoint;                // +0x1c  owned by the packet, nullptr when internal
};
static_assert(sizeof(FPayloadServerSetAddItem) == 0x20, "SETADDITEM payload 0x20");

// ch2 0x2f FLPACKET_SERVER_SETREMOVEITEM, size 0x2c -> payload 0x0c.
// Sender 0x06d88670 (IClient+0x06c, four arguments), receiver caseD_2f at 0x06d81aa0.
// The wire order is NOT the argument order: A2 (a word) lands first, then A3, then A1.
struct FPayloadServerSetRemoveItem {
    unsigned int Field_0x00;    // +0x00  <- 3rd argument, zero-extended from a word
    unsigned int Field_0x04;    // +0x04  <- 4th argument
    unsigned int Field_0x08;    // +0x08  <- 2nd argument
};
static_assert(sizeof(FPayloadServerSetRemoveItem) == 0x0c, "SETREMOVEITEM payload 0x0c");

// ch2 0x31 FLPACKET_SERVER_EXPLODEASTEROIDMINE, size 0x30 -> payload 0x10.
// Sender 0x06d8a360 (IClient+0x0d0), receiver caseD_31 at 0x06d81b6d. Four dwords, order
// preserved both ways.
struct FPayloadServerFourDwords {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
};
static_assert(sizeof(FPayloadServerFourDwords) == 0x10, "four-dword payload 0x10");

// ch2 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE, size 0x2c -> payload 0x0c.
// Sender 0x06d8ac40 (IClient+0x124), receiver caseD_33 at 0x06d823df.
// Width asymmetry, reproduced: the sender zero-extends arg+0x04 (a byte) into a whole
// payload dword, and carries arg+0x08 as a word; the receiver writes the byte back as a
// byte and the word back as a zero-extended dword.
struct FPayloadServerSetMissionObjectiveState {
    unsigned int Field_0x00;    // +0x00  <- arg +0x00
    unsigned int Field_0x04;    // +0x04  <- arg +0x04, zero-extended from a byte
    unsigned short Field_0x08;  // +0x08  <- arg +0x08 (word)
    unsigned short Pad_0x0a;
};
static_assert(sizeof(FPayloadServerSetMissionObjectiveState) == 0x0c, "SETMISSIONOBJECTIVESTATE payload 0x0c");

// FServerMissionObjectiveStateInfo: declared once in src/include/rpc_iclient_args.h
// (Server.dll builds it in pub::Player::SetMissionObjectiveState).

// ch2 0x38 FLPACKET_SERVER_ITEMTRACTORED, size 0x2c -> payload 0x0c:
// FPayloadServerItemTractored is declared once in src/include/rpc_iclient_args.h
// (Server.dll builds it in StarSystem::tractor_loot, FUN_06d0ed40).

// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT, size 0x2c -> payload 0x0c.
// Sender 0x06d8bfe0 (IClient+0x150), receiver caseD_3c at 0x06d82b55.
// The middle field travels as a zero-extended dword and comes back as a word.
struct FPayloadServerRepairObject {
    unsigned int Field_0x00;    // +0x00  <- arg +0x00
    unsigned int Field_0x04;    // +0x04  <- arg +0x04, zero-extended from a word
    unsigned int Field_0x08;    // +0x08  <- arg +0x08
};
static_assert(sizeof(FPayloadServerRepairObject) == 0x0c, "REPAIROBJECT payload 0x0c");

// FServerRepairObjectInfo (the argument struct) lives in src/include/rpc_iclient_args.h
// (moved 2026-10-16: freelancer.exe / RemoteClient.dll read the same record).

// ch2 0x41 FLPACKET_SERVER_BURNFUSE, size 0x3c -> payload 0x1c.
// Sender 0x06d8c170 (IClient+0x15c), receiver caseD_41 at 0x06d82c04. Straight copy both
// ways, mixed widths.
struct FPayloadServerBurnFuse {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned short Field_0x0c;
    unsigned short Pad_0x0e;
    unsigned int Field_0x10;
    unsigned int Field_0x14;
    unsigned char Field_0x18;
    unsigned char Pad_0x19[3];
};
static_assert(sizeof(FPayloadServerBurnFuse) == 0x1c, "BURNFUSE payload 0x1c");

// ch2 0x44 FLPACKET_SERVER_REQUEST_RETURNED, size 0x28 -> payload 0x08.
// Sender 0x06d8a9e0 (IClient+0x14c, five arguments), receiver caseD_44 at 0x06d81918.
// The receiver sign-extends the last two bytes and zero-extends the first -- so the
// three trailing arguments are (unsigned char, signed char, signed char).
struct FPayloadServerRequestReturned {
    unsigned int Field_0x00;    // +0x00  <- 2nd argument
    unsigned char Field_0x04;   // +0x04  <- 3rd argument, delivered zero-extended
    signed char Field_0x05;     // +0x05  <- 4th argument, delivered sign-extended
    signed char Field_0x06;     // +0x06  <- 5th argument, delivered sign-extended
    unsigned char Pad_0x07;
};
static_assert(sizeof(FPayloadServerRequestReturned) == 0x08, "REQUEST_RETURNED payload 0x08");

// ch2 0x46 FLPACKET_SERVER_MARKOBJ, size 0x28 -> payload 0x08.
// Sender 0x06d8ace0 (IClient+0x12c), receiver caseD_46 at 0x06d82420.
struct FPayloadServerDwordByte {
    unsigned int Field_0x00;
    unsigned char Field_0x04;
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadServerDwordByte) == 0x08, "dword+byte payload 0x08");

// ch2 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION, size 0x2c -> payload 0x0c.
// Sender 0x06d8ad30 (IClient+0x130), receiver caseD_47 at 0x06d8244f. The payload IS the
// argument struct Server.dll builds, so FPayloadServerCfgInterfaceNotification is declared
// once, in src/include/rpc_iclient_args.h (moved 2026-10-16).

// ch2 0x4a FLPACKET_SERVER_SETGUIDEDTARGET, size 0x2c -> payload 0x0c.
// Sender 0x06d8a800 (IClient+0x0d4, four arguments), receiver caseD_4a at 0x06d83048.
struct FPayloadServerSetGuidedTarget {
    unsigned int Field_0x00;    // +0x00  <- 2nd argument
    unsigned int Field_0x04;    // +0x04  <- 3rd argument
    unsigned short Field_0x08;  // +0x08  <- 4th argument, truncated to a word
    unsigned short Pad_0x0a;
};
static_assert(sizeof(FPayloadServerSetGuidedTarget) == 0x0c, "SETGUIDEDTARGET payload 0x0c");

// ch2 0x4b FLPACKET_SERVER_SET_CAMERA, size 0x44 -> payload 0x24.
// Sender 0x06d8ae90 (IClient+0x160), receiver caseD_4b at 0x06d82573. The payload IS the
// argument struct Server.dll's pub::Player::SetCamera builds, so FPayloadServerSetCamera
// {Vector Position; Quaternion Orient; float Fov, Aspect} is declared once, in
// src/include/rpc_iclient_args.h (moved 2026-10-16).

// ch2 0x51 FLPACKET_SERVER_USE_ITEM, size 0x28 -> payload 0x08:
// FPayloadServerUseItem is declared once in src/include/rpc_iclient_args.h
// (Server.dll builds it in StarSystem::use_item, FUN_06d0efe0).

// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE, size 0x24 -> payload 0x04.
// Sender 0x06d8eff0 -- NOT an IClient slot: retail sends this one from inside the pump
// FUN_06d8efc0 the first time it runs, and writes a literal 1 into the only field
// (`MOV dword [ECX+0x20],1`), then calls slot 9 with a literal 1 as well.
// Receiver caseD_1 at 0x06d816d8 is the ONE channel-2 case that does not push a leading
// 1: it calls `RealClient->vt[+0x2c](payload+0x00)` with a single argument.
struct FPayloadServerConnectResponse {
    unsigned int Field_0x00;    // +0x00  always the literal 1
};
static_assert(sizeof(FPayloadServerConnectResponse) == 0x04, "CONNECTRESPONSE payload 0x04");

// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED, size 0x30 -> payload 0x10.
// Sender 0x06d8a030 (IClient+0x38), receiver caseD_8 at 0x06d81828. A straight copy of a
// 0x10-byte argument struct with one hole: neither side touches +0x04, so retail delivers
// whatever was on the receiver's stack there. The port zeroes it -- the same harmless
// deviation already recorded for CREATEMINE and REPAIROBJECT.
struct FPayloadServerCharSelectVerified {
    unsigned int Field_0x00;    // +0x00
    unsigned int Unused_0x04;   // +0x04  never written, never read
    unsigned int Field_0x08;    // +0x08
    unsigned int Field_0x0c;    // +0x0c
};
static_assert(sizeof(FPayloadServerCharSelectVerified) == 0x10, "CHARSELECTVERIFIED payload 0x10");

// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN, size 0x44 -> payload 0x24.
// Sender 0x06d8a8b0 (IClient+0xe8), receiver caseD_22 at 0x06d822c6 (file-static
// .data:0x06dbb91c). The payload IS the argument struct Server.dll builds
// (FUN_06d0cc00), so FPayloadServerSystemSwitchIn is declared once, in
// src/include/rpc_iclient_args.h (2026-09-24, claude-code:w5-server-06d0).

// ch2 0x2a FLPACKET_SERVER_ADJUSTATTITUDE, size 0x28 -> payload 0x08.
// Sender 0x06d88f70 (IClient+0x10c), receiver caseD_2a at 0x06d81317. The payload IS the
// argument struct, so FPayloadServerAdjustAttitude is declared once, in
// src/include/rpc_iclient_args.h (moved 2026-10-16).

// ch2 0x53 FLPACKET_SERVER_FORMATION_UPDATE, size 0x30 -> payload 0x10.
// Sender 0x06d8c600 (IClient+0x1e4) writes its 2nd argument and then the three dwords
// behind its 3rd; receiver caseD_53 at 0x06d8133f reads the same three back with
// FLD/FSTP, so they are floats -- a Vector, rebuilt on the receiver's stack and passed
// by address as the slot's second argument.
struct FPayloadServerFormationUpdate {
    unsigned int Field_0x00;    // +0x00  <- 2nd argument, delivered by value
    Vector Offset;              // +0x04  <- the 3rd argument's first three floats
};
static_assert(sizeof(FPayloadServerFormationUpdate) == 0x10, "FORMATION_UPDATE payload 0x10");

// ch2 0x57 FLPACKET_SERVER_SET_MUSIC, size 0x38 -> payload 0x18.
// Sender 0x06d8c660 (IClient+0x1e8) takes seven arguments and serializes six of them --
// the session id is dropped as always. Receiver caseD_57 at 0x06d81372 pushes them back
// in reverse, zero-extends the byte (`XOR EAX,EAX; MOV AL,[EBP+0x30]`) and leaves the
// last one alone, because it is a float the slot takes by value.
struct FPayloadServerSetMusic {
    unsigned int Field_0x00;    // +0x00
    unsigned int Field_0x04;    // +0x04
    unsigned int Field_0x08;    // +0x08
    unsigned int Field_0x0c;    // +0x0c
    unsigned char Field_0x10;   // +0x10  delivered zero-extended
    unsigned char Pad_0x11[3];
    float Field_0x14;           // +0x14  FLD [ESP+0x24] / FSTP [EDX+0x34]
};
static_assert(sizeof(FPayloadServerSetMusic) == 0x18, "SET_MUSIC payload 0x18");

// ===========================================================================
// The two multi-slot channel-2 opcodes. Retail fans each of them out over several
// IClient slots and picks the slot from a field of the payload itself, exactly the way
// channel 1's PLAYER_TRADE does -- the discriminator is in the packet, not in the
// routing header.
// ===========================================================================

// ch2 0x52 FLPACKET_SERVER_PLAYERLIST, size 0x30 -> payload 0x10.
//
// Discriminator: the dword at payload+0x00, read by the receiver at .text:0x06d83102
// (`MOV EAX,[EBP+0x20]`) and compared against 0, 1 and 2 in that order. The packet's own
// constructor (0x06d9ec60) seeds it with 3, i.e. an unfilled PLAYERLIST falls through to
// retail's default branch and delivers nothing -- so 3 is "no message", not a fourth kind.
//
//   Kind 0 (sender 0x06d8c5b0, IClient+0x1c0): slot 112, no payload at all.
//   Kind 1 (sender 0x06d8c500, IClient+0x1b8): slot 110, (1, Name, Field_0x08, bool).
//   Kind 2 (sender 0x06d8c560, IClient+0x1bc): slot 111, (1, Field_0x08).
//
// Name is a WIDE string the packet OWNS: all three senders reach it through the same
// setter FUN_06d99950, which does `free(old)` + `_wcsdup(param_1)` into packet+0x24 and
// stores param_2 into packet+0x28. Kind 2 passes a null first parameter (it pushes the
// zero left in EAX by the allocation's own success test), kind 0 passes two nulls --
// which is why only kind 1 ever carries a string, and why the receiver only reads
// payload+0x04 on kind 1.
struct FPayloadServerPlayerList {
    unsigned int Kind;          // +0x00  0, 1 or 2; the ctor's 3 means "nothing to send"
    wchar_t* Name;              // +0x04  malloc'd copy, freed by the packet
    unsigned int Field_0x08;    // +0x08
    unsigned char Field_0x0c;   // +0x0c  delivered as bool: SETNZ against zero
    unsigned char Pad_0x0d[3];
};
static_assert(sizeof(FPayloadServerPlayerList) == 0x10, "PLAYERLIST payload 0x10");

// The kind values (kPlayerListKind*) are public, in rpc_iclient_args.h, because
// RemoteClient / RemoteServer encode and decode the same discriminator.

// The two containers MISCOBJUPDATE carries are the real standard ones.
//
// Text is retail's VC6 basic_string<unsigned short> -- WIDE, not narrow. Proof, all from
// rpclocal's own import table and the codec bytes:
//   * the sender FUN_06d98b20 calls ?_Split@?$basic_string@GU?$char_traits@G@std@@...
//     (06d98b52 -> [06db315c]), ?_Grow@?$basic_string@G...@ (06d98b8a -> [06db316c])
//     and ?_Eos@?$basic_string@G...@ (06d98b97 -> [06db3158]) -- `@G` is
//     `unsigned short`, not `@D`;
//   * the encoder multiplies the character count by two before both the bounds check
//     and the copy (06d98ef5 `SHL EAX,0x1`), and falls back to
//     ?_Nullstr@?$basic_string@G...@ (06d98f17 -> [06db3190]);
//   * the decoder allocates `len * 2 + 2` (06d9926c `LEA EDX,[EDI + EDI*0x1 + 0x2]`),
//     terminates with a WORD (06d992c9) and measures the result with MSVCRT wcslen
//     (06d992d2).
// The port holds std::wstring (project rule: modern std types, wchar_t for wide text).
//
// Ids is retail's VC6 std::vector<unsigned int>: the ctor zeroes packet+0x58..+0x64 and
// FUN_06d98cf0 reaches the insertion point through `MOV ECX,[ESI+0x60]` (= _Last) while
// passing `ESI+0x58` as the container, which is vector::insert(_Last, 1, value) --
// push_back. The port holds std::vector<unsigned int>.
//
// Both are members of FPacket_FLPACKET_SERVER_MISCOBJUPDATE (rpc_packet_registry.cpp),
// built by its constructor (retail 0x06d9edc0) and torn down by its destructor.

// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE, size 0x6c -> payload 0x4c.
//
// Discriminator: a BITMASK at payload+0x00, two bytes wide. The receiver (caseD_54 at
// .text:0x06d8316d) tests it seven times in a fixed order and may therefore fire more
// than one slot from a single packet; each of the eight senders sets exactly the bits its
// own fields need, through nine one-line setters that all do `OR byte [this+0x20], bit`
// plus one store:
//
//   FUN_06d98af0  0x0001 -> +0x14 dword        FUN_06d98b20  0x0010 -> +0x20 std::string
//   FUN_06d98b00  0x0002 -> +0x18/+0x1c pair   FUN_06d98ae0  0x0020 -> +0x34 dword
//   FUN_06d98ad0  0x0004 -> +0x04 dword        FUN_06d98ce0  0x0040 -> +0x0c word
//   FUN_06d98ac0  0x0008 -> +0x08 dword        FUN_06d98cd0  0x0080 -> +0x10 dword
//   FUN_06d98cf0  0x0100 -> +0x38 vector push_back (it ORs byte [this+0x21], 0x01)
//
// Bit 0x0008 is never tested by the receiver: it only marks that +0x08 was filled. The
// slot choice for bit 0x0001 depends on bit 0x0004 -- that pair is the one real fork:
//
//   0x0009 (sender 0x06d8c1f0) -> slot 113 (1, Field_0x08, Field_0x14)
//   0x0005 (sender 0x06d8c390) -> slot 118 (1, Field_0x04, Field_0x14)
//
// Full sender -> mask -> slot map, all eight confirmed against their receiver branch:
//   0x06d8c1f0 IClient+0x1c4  mask 0x0009  slot 113
//   0x06d8c290 IClient+0x1c8  mask 0x0002  slot 114
//   0x06d8c430 IClient+0x1cc  mask 0x0010  slot 115 (const basic_string<G>&)
//   0x06d8c240 IClient+0x1d0  mask 0x0028  slot 116
//   0x06d8c2e0 IClient+0x1d4  mask 0x0044  slot 117 (the word is zero-extended)
//   0x06d8c390 IClient+0x1d8  mask 0x0005  slot 118
//   0x06d8c3e0 IClient+0x1dc  mask 0x0084  slot 119
//   0x06d8c330 IClient+0x1e0  mask 0x0100  slot 120 (const std::vector<uint>&)
struct FPayloadServerMiscObjUpdate {
    unsigned short Flags;             // +0x00  ctor zeroes this word and nothing more
    unsigned short Pad_0x02;          // +0x02  retail leaves these two bytes uninitialised
    unsigned int Field_0x04;          // +0x04  flag 0x0004
    unsigned int Field_0x08;          // +0x08  flag 0x0008
    unsigned short Field_0x0c;        // +0x0c  flag 0x0040
    unsigned short Pad_0x0e;          // +0x0e
    unsigned int Field_0x10;          // +0x10  flag 0x0080
    unsigned int Field_0x14;          // +0x14  flag 0x0001
    unsigned int Field_0x18;          // +0x18  flag 0x0002
    unsigned int Field_0x1c;          // +0x1c  flag 0x0002
    // From here on the offsets in the NAMES are retail's; the port's members after Text
    // sit wherever std::wstring's size puts them.
    std::wstring Text;                // +0x20  flag 0x0010

    unsigned int Field_0x30;          // +0x30  zeroed by the ctor, no sender ever writes it
    unsigned int Field_0x34;          // +0x34  flag 0x0020; the ctor seeds it with -1
    std::vector<unsigned int> Ids;    // +0x38  flag 0x0100
    // +0x48 is the Ids ELEMENT COUNT, and the earlier "no sender writes it" here was
    // wrong -- it is written on a second hop the port had collapsed away. FUN_06d98cf0,
    // the vector push_back helper, ends with
    //     06d98d1e  MOV EAX,[EDI + 0x8]     ; End
    //     06d98d21  SUB EAX,param_1         ; - Begin
    //     06d98d23  SAR EAX,0x2             ; / 4
    //     06d98d27  MOV byte ptr [ESI + 0x68],AL
    // (and 06d98d17 stores a plain 0 when Begin is null). The codec slots depend on it:
    // the encoder writes this byte before the elements (06d99017) and the decoder uses
    // it as the loop bound (06d9960d `MOVZX ECX,byte ptr [EBP+0x68]`), so a zero here
    // means the whole array is dropped on the wire.
    unsigned char Field_0x48;         // +0x48
    unsigned char Pad_0x49[3];        // +0x49
};
// Retail payload 0x4c (registry 0x6c). The port's is larger (std::wstring / std::vector
// are not VC6's 0x10 bytes each); FPacket_FLPACKET_SERVER_MISCOBJUPDATE holds it as a
// typed member, so the class is 0x20 + sizeof(this struct), not operator new(0x6c).

const unsigned short kMiscObjUpdateFlagField_0x14 = 0x0001;
const unsigned short kMiscObjUpdateFlagPair       = 0x0002;
const unsigned short kMiscObjUpdateFlagField_0x04 = 0x0004;
const unsigned short kMiscObjUpdateFlagField_0x08 = 0x0008;
const unsigned short kMiscObjUpdateFlagText       = 0x0010;
const unsigned short kMiscObjUpdateFlagField_0x34 = 0x0020;
const unsigned short kMiscObjUpdateFlagField_0x0c = 0x0040;
const unsigned short kMiscObjUpdateFlagField_0x10 = 0x0080;
const unsigned short kMiscObjUpdateFlagIds        = 0x0100;

// The sender variant, prepended to the argument dwords the way SendTradePacket prepends
// PLAYER_TRADE's sub-type. One value per retail sender slot (113..120), in slot order,
// named after the IClient::SendServerMiscObjUpdate* suffix.
const unsigned int kMiscObjUpdateVariant_ObjectGroup = 0u;   // slot 113
const unsigned int kMiscObjUpdateVariant_Mission     = 1u;   // slot 114
const unsigned int kMiscObjUpdateVariant_Motd        = 2u;   // slot 115
const unsigned int kMiscObjUpdateVariant_Affiliation = 3u;   // slot 116
const unsigned int kMiscObjUpdateVariant_Rank        = 4u;   // slot 117
const unsigned int kMiscObjUpdateVariant_PlayerGroup = 5u;   // slot 118
const unsigned int kMiscObjUpdateVariant_System      = 6u;   // slot 119
const unsigned int kMiscObjUpdateVariant_Formation   = 7u;   // slot 120

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT -- retail registry 0x40, payload 0x20 (the port
// sizes it from FPayloadServerDamageObject, see there).
//
// Sender 0x06d88c10 (IClient+0x0fc, RET 0xc -- three arguments after this), receiver
// caseD_5 at 0x06d81ed4. The payload is { dword, a std::list, a four-field tail }:
//
//   06d88c2d  MOV EDX,[ESP+0x10]   ; A1
//   06d88c35  MOV [ECX+0x20],EDX   ; payload+0x00 = A1
//   06d88c31  MOV EAX,[ESP+0x14]   ; A2
//   06d88c3d  CALL FUN_06d90390    ; FUN_06d90390(packet, A2)
//
// and FUN_06d90390 opens with "LEA EBP,[this+0x24]", i.e. it assigns into payload+0x04.
// Its body is the retail three-phase std::list assignment; the node it allocates is
// "PUSH 0x14 ; CALL operator_new" (06d90413), and the value copy right after it moves
// exactly three dwords (06d90437..06d90445: [EBX+0x8], [EBX+0xc], [EBX+0x10]). So the
// node is 0x14 = 8 bytes of links + a 0x0c-byte value. Its tail, 06d90459..06d90474,
// copies one BYTE from source+0x0c and three dwords from source+0x10/+0x14/+0x18.
//
// The receiver FUN_06d90290 is the byte-for-byte mirror (same 0x14 node, same
// +0x0c/+0x10/+0x14/+0x18 tail), which closes the three-way check together with the
// registry size: 0x04 + 0x0c + 0x10 == 0x20 == 0x40 - 0x20.
//
// NOTE the tail is copied SHALLOW in both directions -- retail never deep-copies or
// frees it, so it is four plain fields and NOT a std::vector, whatever its
// { byte, ptr, ptr, ptr } shape suggests.

// The 0x0c-byte list element. Three dwords.
//
// UPDATED 2026-09-17 by the CREATESHIP/CREATESOLAR header codec FUN_06da4020, the first
// thing in rpclocal that CONSTRUCTS one instead of copying it. 06da4883..06da487f build
// the default value before the stream overwrites it:
//   +0x00 <- COMMON.DLL::SUBOBJ_ID_NONE, loaded through the data import at [06db3100]
//            and stored as a WORD (06da4888 `MOV CX,word ptr [EAX]`), so the field is a
//            SubObjId in its low half;
//   +0x04 <- 0xbf800000 (06da4877), i.e. -1.0f -- a float, and -1 is DamageEntry's
//            "untouched" health, matching common_goods.h's DamageEntry;
//   +0x08 <- 0 (06da487f, from an EBX that 06da4856 had just zeroed).
// The encoder FUN_06da3720 ships them as word(+0x00), dword(+0x08), FLOAT(+0x04) --
// 06da3f9d / 06da3fc6 / 06da3fee -- so only the low half of +0x00 travels.
// The types stay `unsigned int` because every other user moves all three as raw dwords;
// the codec bit-casts. TODO: retype +0x04 to float and +0x00 to a ushort pair once the
// remaining movers are ported and can be changed in one pass.
struct FRpcDamageEntryMirror {
    unsigned int Field_0x00;        // +0x00  low half == SubObjId, SUBOBJ_ID_NONE default
    unsigned int Field_0x04;        // +0x04  float health, -1.0f default, bit pattern
    unsigned int Field_0x08;        // +0x08
};
static_assert(sizeof(FRpcDamageEntryMirror) == 0x0c, "damage entry is 0x0c (node 0x14 - 8 links)");

// operator new(0x14) at 06d90413 / 06d90324.
struct FRpcDamageEntryNode {
    FRpcDamageEntryNode* Next;      // +0x00
    FRpcDamageEntryNode* Prev;      // +0x04
    FRpcDamageEntryMirror Value;    // +0x08
};
static_assert(sizeof(FRpcDamageEntryNode) == 0x14, "retail damage list node is operator new(0x14)");

// Same 0x0c retail std::list header as the collision-group one in rpc_containers.h.
struct FRpcDamageEntryList {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcDamageEntryNode* Head;      // +0x04  dummy node
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcDamageEntryList) == 0x0c, "retail std::list header is 0x0c");

// The 0x1c-byte retail image of Common.dll's DamageList. Nothing references it any more:
// DAMAGEOBJECT's payload holds the real DamageList (below), and the CREATESHIP /
// CREATESOLAR create-info record (src/include/server_createinfo.h) holds the real one too.
// It is kept only as the documented wire image of the retail layout.
//
// IDENTIFIED 2026-09-18: this is Common.dll's DamageList, not a rpclocal-local shape.
// CREATESHIP's constructor builds one at packet+0x80 by calling the export itself --
// 06d9ccf3 `CALL dword ptr [->COMMON.DLL::DamageList::...]`, .rdata:06db310c, whose
// XREF header reads `06d9ccf3(R),06d9e6c8(R)` and whose undecorated name in the same
// listing block is `??0DamageList@@QAE@XZ` -- and the next store in that constructor is
// 06d9ccfd `MOV byte ptr [ESI + 0x9c],BL`, which fixes the size at 0x1c. Matching it
// against common_misc.h's published DamageList gives the four tail fields their
// meanings: +0x0c bDestroyed (hence the BYTE copy), +0x10 DamageCause,
// +0x14 InflictorId, +0x18 InflictorOwner.
//
// The NAMES below stay Field_0xNN on purpose. This struct is the WIRE image: its list is
// the retail 0x0c header with hand-laid 0x14 nodes, where the port's own DamageList
// holds a std::list<DamageEntry> plus a separate Count and has no fixed size. Retyping
// the four would invite someone to substitute the real class, which does not have this
// layout. TODO: rename once a converter to and from common_misc.h's DamageList exists.
struct FRpcDamageListMirror {
    FRpcDamageEntryList Entries;    // +0x00  std::list<DamageEntry>, _Size at +0x08
    unsigned char Field_0x0c;       // +0x0c  copied as a byte (06d9045d / 06d9036e)
                                    //        DamageList::is_destroyed / ::set_destroyed
    unsigned char Pad_0x0d[3];      // +0x0d
    unsigned int Field_0x10;        // +0x10  DamageList::get_cause / ::set_cause
    unsigned int Field_0x14;        // +0x14  DamageList::get_inflictor_id
    unsigned int Field_0x18;        // +0x18  DamageList::get_inflictor_owner_player
};
static_assert(sizeof(FRpcDamageListMirror) == 0x1c, "DAMAGEOBJECT argument struct 0x1c");

// ch2 0x05 DAMAGEOBJECT holds the real Common.dll DamageList (common_misc.h), declared
// once there. Retail's packet ctor 06d9c8d0 builds exactly that object in place at
// packet+0x24 (06d9c90f..06d9c935: the list's operator new(0x14) dummy, _Size 0, the byte
// at +0x30 and the three dwords behind it), its sender assigns the caller's DamageList
// into it (FUN_06d90290) and the receiver assigns it into a stack DamageList built by
// COMMON.DLL's own ctor (06d81ee3). The port's DamageList is modern-std shaped (no retail
// ABI), so its size follows the build's std::list: 0x1c in Release, 0x20 in Debug and
// DebugASan (the iterator-debugging container proxy). Retail's class is `PUSH 0x40`
// (payload 0x20); the port sizes the class as the 0x20 packet header plus this struct
// (rpc_packet_registry.cpp), so the block always holds it. It is the class's typed
// member, built and destroyed with it.
struct FPayloadServerDamageObject {
    unsigned int Field_0x00;        // +0x00  the 2nd argument, by value
    DamageList Damage;              // +0x04
};
// The offset contract, not retail's byte count: the payload starts right after the
// 0x20-byte packet header, and the list object follows the one argument dword.
static_assert(sizeof(FPacketBase) == 0x20, "payload starts at packet+0x20");
static_assert(offsetof(FPayloadServerDamageObject, Damage) == sizeof(unsigned int),
              "DAMAGEOBJECT: the DamageList follows the by-value argument dword");

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT -- registry 0x34, payload 0x14.
//
// Sender 0x06d8aae0 (IClient+0x0c0, RET 8), receiver caseD_28 at 0x06d811c8.
// The payload is { dword, byte, a std::list of 0x48-byte records }; the list header
// sits at payload+0x08 because the accessor FUN_06d931b0 is "LEA EAX,[this+0x28]", and
// push_back FUN_06d93160 allocates "PUSH 0x50 ; operator_new" per node and bumps the
// count at packet+0x30 (== header+0x08). 0x04 + 0x04 + 0x0c == 0x14 == 0x34 - 0x20.
//
// Sender and receiver both walk an ARRAY of 0x48-byte records on the caller's side
// ("ADD ESI,0x48" at 06d8ac14, "ADD EDI,0x48" at 06d81237) and a std::list of the same
// records on the wire.
//
// RETAIL TRANSPOSITION, reproduced here on purpose (see docs/re/deviations.md):
// the two directions do NOT agree on one adjacent field pair.
//   sender   06d8abe3 "MOV EDX,[ESI+0x28]" (record+0x30) -> 06d8abe6 "MOV [ESP+0x48]" (wire+0x38)
//   sender   06d8abea "MOV EAX,[ESI+0x24]" (record+0x2c) -> 06d8abed "MOV [ESP+0x4c]" (wire+0x3c)
//   receiver 06d812c7 "MOV EDX,[EAX+0x40]" (wire+0x38)   -> 06d812ca "MOV [ECX+0x2c]" (record+0x2c)
//   receiver 06d812cd "MOV EDX,[EAX+0x44]" (wire+0x3c)   -> 06d812d0 "MOV [ECX+0x30]" (record+0x30)
// Every one of the other sixteen field moves is a strict inverse; only this pair swaps.

// The record as the CALLER holds it is Server.dll's FServerLootDesc (server_createinfo.h),
// 0x48 bytes, default-constructed on the receiving side by FUN_06d840d0 (which zeroes
// +0x00..+0x1b and sets +0x44 = 1, +0x45 = +0x46 = 0). Its +0x0c is moved as a word by
// both sides, and its +0x44 never travels -- the wire has no slot for it. (The private
// mirror FRpcLootDescMirror was folded onto it 2026-10-16.)

// The record as it travels: FUN_06d93280 copies 0x46 bytes of it, the node is
// operator new(0x50) so the value is padded back out to 0x48.
struct FRpcLootWireRecord {
    unsigned int Field_0x00;        // +0x00 <- record+0x00
    unsigned int Field_0x04;        // +0x04 <- record+0x04
    unsigned int Field_0x08;        // +0x08 <- record+0x08
    unsigned short Field_0x0c;      // +0x0c <- record+0x0c
    unsigned short Pad_0x0e;        // +0x0e
    unsigned int Field_0x10;        // +0x10 <- record+0x10
    unsigned int Field_0x14;        // +0x14 <- record+0x14
    unsigned int Field_0x18;        // +0x18 <- record+0x18
    unsigned int Field_0x1c;        // +0x1c <- record+0x1c
    unsigned int Field_0x20;        // +0x20 <- record+0x20
    unsigned int Field_0x24;        // +0x24 <- record+0x24
    unsigned int Field_0x28;        // +0x28 <- record+0x38
    unsigned int Field_0x2c;        // +0x2c <- record+0x3c
    unsigned int Field_0x30;        // +0x30 <- record+0x40
    unsigned int Field_0x34;        // +0x34 <- record+0x28  (sender seeds it with 1.0f)
    unsigned int Field_0x38;        // +0x38 <- record+0x30  (receiver puts it back at +0x2c)
    unsigned int Field_0x3c;        // +0x3c <- record+0x2c  (receiver puts it back at +0x30)
    unsigned int Field_0x40;        // +0x40 <- record+0x34
    unsigned char Field_0x44;       // +0x44 <- record+0x46
    unsigned char Field_0x45;       // +0x45 <- record+0x45
    unsigned char Pad_0x46[2];      // +0x46  inside the 0x50 node, never written
};
static_assert(sizeof(FRpcLootWireRecord) == 0x48, "CREATELOOT wire record is 0x50 - 8 links");

struct FRpcLootWireNode {
    FRpcLootWireNode* Next;         // +0x00
    FRpcLootWireNode* Prev;         // +0x04
    FRpcLootWireRecord Value;       // +0x08
};
static_assert(sizeof(FRpcLootWireNode) == 0x50, "retail loot list node is operator new(0x50)");

struct FRpcLootWireList {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcLootWireNode* Head;         // +0x04
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcLootWireList) == 0x0c, "retail std::list header is 0x0c");

struct FPayloadServerCreateLoot {
    unsigned int Field_0x00;        // +0x00 <- argument+0x00 (06d8ab03/06d8ab09)
    unsigned char Field_0x04;       // +0x04 <- LOW BYTE of argument+0x04 (06d8ab0c MOV CL,[EAX+0x4])
    unsigned char Pad_0x05[3];      // +0x05
    FRpcLootWireList Items;         // +0x08
};
static_assert(sizeof(FPayloadServerCreateLoot) == 0x14, "CREATELOOT payload 0x14");

// The argument struct behind IClient::SendServerCreateLoot is Server.dll's
// FServerCreateLootRequest (server_createinfo.h; the private FServerCreateLootInfo was
// folded onto it 2026-10-16). The receiver stores the payload byte back as a full dword
// at +0x04 (06d811e2 MOVZX EDX,byte [EBP+0x24] then 06d81207 a dword store) -- Kind is
// four bytes wide while the sender only ships its low byte -- zeroes +0x08 / +0x09
// (06d811f7 / 06d811fb), and both sides read the count at +0x10 as a byte.

// ===========================================================================
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER -- registry 0x68, payload 0x48.
//
// Sender 0x06d8a270 (IClient+0x0cc, RET 8), receiver caseD_2d at 0x06d81ce9. Eighteen
// field moves each way and the receiver is the strict inverse of the sender on all
// eighteen, which is what promotes this layout from "read" to "checked". The argument
// struct is 0x48 as well; only the ORDER differs.
//
// The receiver passes the literal 1 by REFERENCE (06d81d8d MOV [ESP+0x1e4],1 then
// LEA EDX,[ESP+0x1e4] ; PUSH EDX), and so does the sender, which dereferences its own
// first argument (06d8a34a MOV ECX,[ESP+0xc] ; MOV EAX,[ECX] ; PUSH EAX).
struct FPayloadServerCreateCounter {
    unsigned int Field_0x00;        // +0x00 <- argument+0x04
    unsigned int Field_0x04;        // +0x04 <- argument+0x00
    unsigned int Field_0x08;        // +0x08 <- argument+0x44
    unsigned int Field_0x0c;        // +0x0c <- argument+0x08
    unsigned short Field_0x10;      // +0x10 <- argument+0x0c, a word on both sides
    unsigned short Pad_0x12;        // +0x12
    unsigned int Field_0x14;        // +0x14 <- argument+0x1c
    unsigned int Field_0x18;        // +0x18 <- argument+0x20
    unsigned int Field_0x1c;        // +0x1c <- argument+0x24
    unsigned int Field_0x20;        // +0x20 <- argument+0x28
    unsigned int Field_0x24;        // +0x24 <- argument+0x10
    unsigned int Field_0x28;        // +0x28 <- argument+0x14
    unsigned int Field_0x2c;        // +0x2c <- argument+0x18
    unsigned int Field_0x30;        // +0x30 <- argument+0x2c
    unsigned int Field_0x34;        // +0x34 <- argument+0x30
    unsigned int Field_0x38;        // +0x38 <- argument+0x34
    unsigned int Field_0x3c;        // +0x3c <- argument+0x38
    unsigned int Field_0x40;        // +0x40 <- argument+0x3c
    unsigned int Field_0x44;        // +0x44 <- argument+0x40
};
static_assert(sizeof(FPayloadServerCreateCounter) == 0x48, "CREATECOUNTER payload 0x48");

// The argument struct is Server.dll's FServerCreateProjectileInfo (server_createinfo.h),
// the record CREATEMINE sends too; the private FServerCreateCounterInfo was folded onto it
// 2026-10-16.

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED -- registry 0x58, payload 0x38.
//
// Sender 0x06d8a730 (IClient+0x0c8, RET 8), receiver caseD_37 at 0x06d81da6. Fourteen
// moves each way, strict inverses. The permutation is a clean rotation: the argument's
// first eight dwords go to payload+0x18.., and its tail +0x20..+0x34 comes back at
// payload+0x00..+0x14. The literal 1 travels by reference on both sides.
struct FPayloadServerCreateGuided {
    unsigned int Field_0x00;        // +0x00 <- argument+0x2c
    unsigned int Field_0x04;        // +0x04 <- argument+0x28
    unsigned int Field_0x08;        // +0x08 <- argument+0x30
    unsigned short Field_0x0c;      // +0x0c <- argument+0x34, a word on both sides
    unsigned short Pad_0x0e;        // +0x0e
    unsigned int Field_0x10;        // +0x10 <- argument+0x20
    unsigned short Field_0x14;      // +0x14 <- argument+0x24, a word on both sides
    unsigned short Pad_0x16;        // +0x16
    unsigned int Field_0x18;        // +0x18 <- argument+0x00
    unsigned int Field_0x1c;        // +0x1c <- argument+0x04
    unsigned int Field_0x20;        // +0x20 <- argument+0x08
    unsigned int Field_0x24;        // +0x24 <- argument+0x0c
    unsigned int Field_0x28;        // +0x28 <- argument+0x10
    unsigned int Field_0x2c;        // +0x2c <- argument+0x14
    unsigned int Field_0x30;        // +0x30 <- argument+0x18
    unsigned int Field_0x34;        // +0x34 <- argument+0x1c
};
static_assert(sizeof(FPayloadServerCreateGuided) == 0x38, "CREATEGUIDED payload 0x38");

// The argument struct is FServerCreateGuidedInfo in src/include/server_createinfo.h -- the
// record Server.dll's FUN_06d07d40 builds and this module's sender 0x06d8a730 reads. It
// is declared once, there; rpc_containers.h pulls that header in.

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT -- registry 0x3c, payload 0x1c.
//
// Sender 0x06d8aa40 (IClient+0x0f8, RET 8), receiver caseD_32 at 0x06d81c7d. Unlike its
// neighbours this one passes the literal 1 by VALUE (06d81cdc PUSH 0x1), matching the
// sender, which pushes its own first argument unchanged (06d8aac3/06d8aaca).
//
// The string: the sender strlen()s argument+0x04 with the usual SCASB.REPNE and hands
// (buffer, strlen) to FUN_06d989b0, which does operator_new(len+1), copies len+1
// bytes, stores the POINTER at packet+0x38 (payload+0x18) and the COUNT len+1 at
// packet+0x24 (payload+0x04). The receiver copies it back into a 0x104-byte field of
// the argument struct (06d81cbf..06d81ccd, a byte loop that stops on NUL).
//
// The field at payload+0x14 is a two-value ENUM REMAP, and the two sides agree:
//   sender   06d8aa84  argument+0x114 == 1 -> payload+0x14 = 1
//            06d8aa98  argument+0x114 == 2 -> payload+0x14 = 0
//            (anything else leaves the byte UNINITIALISED in retail; this port zeroes it)
//   receiver 06d81ca4  payload+0x14 == 0 -> argument+0x114 = 2, else 1
//                      (CMP AL,BL ; SETZ DL ; INC EDX)
struct FPayloadServerRequestSpaceScript {
    unsigned int Field_0x00;        // +0x00 <- argument+0x000
    unsigned int NameSize;          // +0x04 = strlen(argument+0x004) + 1
    unsigned int Field_0x08;        // +0x08 <- argument+0x108
    unsigned int Field_0x0c;        // +0x0c <- argument+0x10c
    unsigned int Field_0x10;        // +0x10 <- argument+0x110
    unsigned char Field_0x14;       // +0x14 <- the enum remap above
    unsigned char Pad_0x15[3];      // +0x15
    char* Name;                     // +0x18  owned by the packet
};
static_assert(sizeof(FPayloadServerRequestSpaceScript) == 0x1c, "REQUESTSPACESCRIPT payload 0x1c");

// The argument record (FServerRequestSpaceScriptInfo, 0x118) and kRequestSpaceScriptNameSize
// are declared once, in src/include/rpc_iclient_args.h (included above): Server.dll builds the
// record and Freelancer.exe's LAB_0053d9d0 reads it.

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE -- registry 0xb4, payload 0x94.
//
// The payload is IClient slot 100's argument struct verbatim (receiver caseD_4e at
// 0x06d81c3b: `MOV ECX,0x25 ; REP MOVSD` from packet+0x20). Server.dll builds that struct
// in pub::AI::SubmitDirective, so FPayloadServerSetDirective is declared once, in
// src/include/rpc_iclient_args.h (included above), with the field types Server.dll's
// builder proves.

// ===========================================================================
// The flattened-FmtStr family: ch2 0x29, 0x2b (no FmtStr, but the same relation list),
// 0x34, 0x35 and 0x45.
//
// THE "BLOCKED ON FmtStr::unflatten" NOTE WAS STALE. Retail rpclocal.dll imports exactly
// four FmtStr entry points from COMMON.DLL and no more -- read off the import table in
// EXE/Disasm/rpclocal.dll.txt:
//
//   .rdata:06db30dc  ?flatten@FmtStr@@QBEHPAXI@Z              (listing line 103363)
//   .rdata:06db3008  ?unflatten@FmtStr@@QAEHPAXI@Z            (listing line 103066)
//   .rdata:06db3094  ??1FmtStr@@QAE@XZ                        (listing line 103262)
//   .rdata:06db30e0  ?get_flattened_size@Val@FmtStr@@QBEIXZ   (listing line 103368)
//
// All four already exist in the port with signatures that demangle to exactly those
// prototypes (src/include/common_text.h lines 168, 209, 238, 239). What actually blocked
// these types was rpclocal.vcxproj linking only kernel32 and user32 -- a property of the
// port, never of retail: retail rpclocal's own import table names COMMON.DLL, DACOM.DLL,
// KERNEL32.DLL, MSVCP60.DLL and MSVCRT.DLL. So the project now links Common.lib (which
// src/Server/Server.vcxproj already did) and these codecs call the real FmtStr.
//
// Independent check on the port's FmtStr::Val::get_flattened_size: the senders that do
// NOT call the export inline the identical arithmetic, six instructions, five separate
// copies (0x06d8ade2 for 0x34, 0x06d8afa0 for 0x45, 0x06d8b070 and 0x06d8b170 for 0x35,
// 0x06d8b6d0 for ch1 0x1b):
//
//   MOVZX EAX,word ptr [ESI+0x8] ; CMP EAX,0x21 ; JZ recurse
//   XOR EDX,EDX ; CMP EAX,0x4e ; SETZ DL ; DEC EDX ; AND EDX,0xfffffff0 ; ADD EDX,0x17
//
// tag == 0x4e -> DL=1 -> EDX=0 -> 0 -> 0x17; anything else -> EDX=-1 -> 0xfffffff0 ->
// 7; tag == 0x21 -> the recursive walk plus 3. That is byte for byte what
// FmtStr::Val::get_flattened_size in src/Common/common_fmtstr.cpp computes. (The tracker note on item 202115 stated the
// two constants the other way round -- 0x4e -> 7, else -> 0x17. It is wrong; corrected.)
//
// Per-FmtStr base is 6: the mad-lib id dword plus the ushort node count, the two fields
// FmtStr::unflatten reads back first (src/Common/common_fmtstr.cpp).

// The 8-byte element the reputation types carry. Server.dll's caller allocates the whole
// array as operator new(Reputation::group_count() * 8) (Server.dll.c:89953) and fills it
// through Reputation::Vibe::Get; both ends of the wire copy exactly two dwords per
// element and nothing in rpclocal ever interprets them.
// It is the WIRE view of Common.dll's Reputation::Relation {GroupId, Feeling} -- both
// dwords as raw bits, which is how the codecs move them. Server.dll hands the array
// over as the Relation* of Reputation::Vibe::Get
// (?Get@Vibe@Reputation@@YAHABHAAI1AAEQAURelation@2@AAUFmtStr@@4AAPBG@Z), typed so in
// FServerSetReputationInfo (src/include/rpc_iclient_args.h).
struct FRpcRelation {
    unsigned int Field_0x00;        // +0x00  Reputation::Relation::GroupId
    unsigned int Field_0x04;        // +0x04  Reputation::Relation::Feeling, as float bits
};
static_assert(sizeof(FRpcRelation) == 0x08, "reputation relation element is 8 bytes");
static_assert(sizeof(FRpcRelation) == sizeof(::Reputation::Relation),
              "FRpcRelation is the wire view of Reputation::Relation");

// Retail's list node is operator new(0xc) and its value is a POINTER to a separately
// operator new(8)'d element -- std::list<Relation*>, not std::list<Relation>. Sender
// 0x06d88e81..0x06d88ec0 allocates the 8 bytes, copies the pair in and links the node;
// receiver 0x06d814c8..0x06d814f3 copies the pair out and frees the value straight away.
struct FRpcRelationNode {
    FRpcRelationNode* Next;         // +0x00
    FRpcRelationNode* Prev;         // +0x04
    FRpcRelation* Value;            // +0x08
};
static_assert(sizeof(FRpcRelationNode) == 0x0c,
              "retail relation list node is operator new(0xc)");

struct FRpcRelationList {
    unsigned char Allocator;        // +0x00  VC6 allocator<T>: one byte, never read back
    unsigned char Pad_0x01[3];      // +0x01
    FRpcRelationNode* Head;         // +0x04  dummy node; Next == Prev == itself when empty
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcRelationList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- registry 0x68, payload 0x48.
//
// Sender 0x06d88d70 (IClient+0x108, RET 8), receiver caseD_29 at 0x06d813cf, which calls
// IClient vtable +0x108 -- the same slot, so this is a straight relay and the argument
// struct is identical on both sides. Verified field by field in both directions.
// ===========================================================================
struct FPayloadServerSetReputation {
    unsigned int NameASize;         // +0x00  06d88de5  flattened size of arg+0x18
    void* NameABuffer;              // +0x04  06d88df6
    unsigned int NameBSize;         // +0x08  06d88e23  flattened size of arg+0x30
    void* NameBBuffer;              // +0x0c  06d88e36
    float Attitude;                 // +0x10  06d88ee9 <- arg+0x04 ; ctor seeds 0
    // +0x14..+0x24 are the codec's FIVE PRESENCE-FLAG constants, not five payload
    // fields. The constructor seeds them 1, 2, 4, 8, 0x10 -- and the earlier claim
    // here that "+0x18 the ctor does not even seed this one" was WRONG: 06d9d2dc
    // `MOV EAX,0x2` / 06d9d2e1 `MOV dword ptr [ESI+0x38],EAX` is the seed, and
    // packet+0x38 is payload+0x18. Both codec slots (LAB_06d91ed0 / LAB_06d92350,
    // ported in rpc_wire_ch2.cpp) read the VALUES out of these five and test them
    // against the mask byte at +0x34; nothing ever uses a literal.
    unsigned int Field_0x14;        // +0x14  ctor seeds 1     06d9d2ef
    unsigned int Field_0x18;        // +0x18  ctor seeds 2     06d9d2e1
    unsigned int Field_0x1c;        // +0x1c  ctor seeds 4     06d9d2f2
    unsigned int Field_0x20;        // +0x20  ctor seeds 8     06d9d2f9
    unsigned int Field_0x24;        // +0x24  ctor seeds 0x10  06d9d300
    FRpcRelationList Relations;     // +0x28  packet+0x48..+0x53 (FUN_06d92760 / FUN_06d927e0)
    unsigned char Field_0x34;       // +0x34  the presence MASK; ctor clears it (06d9d323)
                                    //        and the ENCODER rebuilds it (06d91f1c..)
    unsigned char Pad_0x35[3];      // +0x35
    unsigned int VibeHandle;        // +0x38  06d88db2 <- arg+0x00
    unsigned int Field_0x3c;        // +0x3c  06d88dbc <- arg+0x08 ; ctor seeds 0xffffffff
    unsigned int DisplayNameLength; // +0x40  FUN_06d92710: wcslen(arg+0x48) + 1, in wchar_t
    wchar_t* DisplayName;           // +0x44  operator new((len+1)*4) -- see the note below
};
static_assert(sizeof(FPayloadServerSetReputation) == 0x48, "SETREPUTATION payload 0x48");

// The struct Server.dll hands to IClient slot 66. Recovered from Server.dll's own two
// call sites (Server.dll.c:89944-89970 and 89989-90015): both build it on the stack,
// both agree on every offset, and the trailing wide name measures 0x30 bytes in both
// frames (local_3c..local_c, and local_b4..local_84).
//
// Two retail quirks, reproduced: the single-player branch never fills +0x04, and the
// other branch never fills +0x08/+0x0c -- each path only sets what its own producer
// (Reputation::Vibe::Get vs Reputation::Vibe::AttitudeTowards) hands back, and the
// sender then serializes +0x00, +0x04 and +0x08 regardless. +0x0c is filled by Get and
// is NEVER serialized by anything.
//
// The struct itself (FServerSetReputationInfo) crosses into Server.dll, which builds
// it, so it is declared once in src/include/rpc_iclient_args.h (2026-09-24,
// claude:srv-pdb).

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS -- registry 0x34, payload 0x14.
//
// Sender 0x06d8a5c0 (IClient+0x148, RET 8), receiver caseD_2b at 0x06d815bb, which calls
// IClient vtable +0x148 -- again the same slot, so again a straight relay. No FmtStr on
// this one: the name travels as a NARROW string through FUN_06d91850 (operator new(n) +
// strncpy, size at packet+0x24), and the relation list is the same std::list<Relation*>.
// ===========================================================================
struct FPayloadServerSetGroupFeelings {
    char* Name;                     // +0x00  FUN_06d91850: operator new(strlen+1) + strncpy
    unsigned int NameSize;          // +0x04  strlen(arg+0x00) + 1, from the REPNE SCASB
    FRpcRelationList Relations;     // +0x08  packet+0x28..+0x33 (FUN_06d91ca0 / FUN_06d91d20)
};
static_assert(sizeof(FPayloadServerSetGroupFeelings) == 0x14,
              "SETGROUPFEELINGS payload 0x14");

// FServerSetGroupFeelingsInfo (the IClient slot 82 argument the receiver builds) is declared
// once in src/include/rpc_iclient_args.h (moved 2026-09-26, claude-code:exe-w3a, W4-C).

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- registry 0x30, payload 0x10.
//
// Sender 0x06d8ad90 (IClient+0x11c, RET 8), receiver caseD_34 at 0x06d82494 -> IClient
// vtable +0x11c. The packet destructor (FUN_06d9d7d0) frees packet+0x2c and nothing else.
// ===========================================================================
struct FPayloadServerReplaceMissionObjective {
    unsigned int Field_0x00;        // +0x00  06d8adba <- arg+0x00
    unsigned int State;             // +0x04  06d8adbd MOVZX from the BYTE at arg+0x20
    unsigned int TextSize;          // +0x08  06d8ae12
    void* TextBuffer;               // +0x0c  06d8ae22
};
static_assert(sizeof(FPayloadServerReplaceMissionObjective) == 0x10,
              "REPLACEMISSIONOBJECTIVE payload 0x10");

// FServerReplaceMissionObjectiveInfo: declared once in src/include/rpc_iclient_args.h
// (Server.dll builds it in pub::Player::ReplaceMissionObjective).

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE -- registry 0x2c, payload 0x0c.
//
// Sender 0x06d8af60 (IClient+0x118, RET 8), receiver caseD_45 at 0x06d82623 -> IClient
// vtable +0x118. The packet destructor (FUN_06d9d9e0) frees packet+0x24.
// ===========================================================================
struct FPayloadServerSetMissionMessage {
    unsigned int TextSize;          // +0x00  06d8afd0
    void* TextBuffer;               // +0x04  06d8afe0
    unsigned int Field_0x08;        // +0x08  06d8aff4 <- arg+0x18
};
static_assert(sizeof(FPayloadServerSetMissionMessage) == 0x0c,
              "SET_MISSION_MESSAGE payload 0x0c");

// FServerSetMissionMessageInfo == CSetMissionMessage: declared once in
// src/include/rpc_iclient_args.h (Server.dll builds it in DisplayMissionMessage).

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES -- registry 0x48, payload 0x28.
//
// Sender 0x06d8b020 (IClient+0x120, RET 8), receiver caseD_35 at 0x06d826c9 -> IClient
// vtable +0x120. Two top-level FmtStrs plus a list of per-objective records, each record
// its own operator new(0xc) holding one more flattened FmtStr and a state byte.
// ===========================================================================
struct FRpcMissionObjectiveRecord {
    unsigned char State;            // +0x00  06d8b15d
    unsigned char Pad_0x01[3];      // +0x01  retail news 0xc and writes only byte 0 here
    unsigned int TextSize;          // +0x04  06d8b1a0
    void* TextBuffer;               // +0x08  06d8b1b2
};
static_assert(sizeof(FRpcMissionObjectiveRecord) == 0x0c,
              "per-objective record is operator new(0xc)");

struct FRpcMissionObjectiveNode {
    FRpcMissionObjectiveNode* Next; // +0x00
    FRpcMissionObjectiveNode* Prev; // +0x04
    FRpcMissionObjectiveRecord* Value;  // +0x08  again a POINTER, not an inline value
};
static_assert(sizeof(FRpcMissionObjectiveNode) == 0x0c,
              "retail objective list node is operator new(0xc)");

struct FRpcMissionObjectiveList {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcMissionObjectiveNode* Head; // +0x04
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcMissionObjectiveList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

struct FPayloadServerSetMissionObjectives {
    unsigned int Field_0x00;        // +0x00  06d8b04b <- arg+0x00
    unsigned int Field_0x04;        // +0x04  06d8b058 MOVZX from the BYTE at arg+0x3c
    unsigned int TextASize;         // +0x08  06d8b0a0  flattened size of arg+0x0c
    void* TextABuffer;              // +0x0c  06d8b0b3
    unsigned int TextBSize;         // +0x10  06d8b100  flattened size of arg+0x24
    void* TextBBuffer;              // +0x14  06d8b111
    unsigned int Count;             // +0x18  NOT written directly: FUN_06d8fc10 increments
                                    //        packet+0x38 on every push_back, in lockstep
                                    //        with the list size at packet+0x44
    FRpcMissionObjectiveList Objectives;  // +0x1c  packet+0x3c..+0x47
};
static_assert(sizeof(FPayloadServerSetMissionObjectives) == 0x28,
              "SETMISSIONOBJECTIVES payload 0x28");

// FServerMissionObjectiveEntry / FServerSetMissionObjectivesInfo (the slot 72
// argument) moved to src/include/rpc_iclient_args.h: Server.dll builds them
// (CPlayerGroup::DelMember / SetMissionObjectives, pub::Player::SetMissionObjectives)
// and this module reads them, so they are declared once there.

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT -- registry 0x2c, payload 0x0c.
//
// THE "BLOCKED ON CostumeHint" NOTE WAS STALE TOO. The port already has the whole class:
// src/include/common_goods.h (BaseHint 0x08 with Kind at +0x04, CostumeHint 0x3c
// with Data[13] at +0x08) and src/Common/common_baseproperties.cpp (Serialize,
// UnSerialize, CleanUp). The virtual ORDER matches retail exactly: rpclocal substitutes
// its own four-slot vtable at .rdata:06db3740 and its three non-destructor slots are
// import thunks -- +0x04 -> CostumeHint::Serialize, +0x08 -> CostumeHint::UnSerialize,
// +0x0c -> CostumeHint::CleanUp -- which is the order common_goods.h declares them in.
//
// Sender 0x06d8a3c0 (IClient+0x144, RET 8): calls the hint's own vtable+0x04 (Serialize)
// into a stack pointer, FUN_06d90a00 copies the returned bytes into a packet-owned block
// (operator new(Size) at packet+0x20, Size at packet+0x24), packet+0x28 takes
// BaseHint::Kind, then vtable+0x0c (CleanUp) releases the serializer's buffer.
//
// Receiver caseD_4d at 0x06d81ba3: `MOV EAX,[EBP+0x28] ; DEC EAX ; JNZ` -- only Kind == 1
// builds anything. Then operator new(0x3c), CostumeHint::CostumeHint(), the vtable swap,
// UnSerialize(packet+0x20, packet+0x24), IClient+0x144 with the literal 1 BY REFERENCE,
// and the deleting destructor. Kind != 1 still calls the slot, with a null hint.
//
// The vtable swap (06d81bd4) is reproduced by FRpcLocalCostumeHint in rpc_payload_ch2.cpp:
// a member-less derived class local to rpclocal, whose ctor stores this module's vftable
// after COMMON's ctor and whose deleting dtor frees with this module's operator delete.
struct FPayloadServerLoadHint {
    void* Buffer;                   // +0x00  06d90a00: operator new(Size) + the copy
    unsigned int Size;              // +0x04  06d90a2e `MOV dword ptr [EBX + 0x24],EBP`
    unsigned int Kind;              // +0x08  06d8a406 <- BaseHint::Kind at hint+0x04
};
static_assert(sizeof(FPayloadServerLoadHint) == 0x0c, "LOADHINT payload 0x0c");

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE -- registry 0x34, payload 0x14.
//
// THE "BLOCKED ON FIVE COMMON.DLL SETTERS" NOTE WAS STALE AS WELL, with one real
// residual that has now been fixed. The receiver (caseD_55, 0x06d82abd) uses SEVEN
// EquipDesc entry points, all imported by name:
//
//   .rdata:06db3004 ??0EquipDesc@@QAE@XZ          -> common_goods.h EquipDesc()
//   .rdata:06db3088 ?set_id@EquipDesc@@QAEXG@Z    -> set_id
//   .rdata:06db308c ?set_arch_id@...QAEXI@Z       -> set_arch_id
//   .rdata:06db3084 ?set_count@...QAEXH@Z         -> set_count  <-- WAS MISSING, added
//   .rdata:06db3080 ?set_status@...QAEXM@Z        -> set_status
//   .rdata:06db3070 ?make_internal@...QAEXXZ      -> make_internal
//   .rdata:06db307c ?set_equipped@...QAEX_N@Z     -> set_equipped
//
// Six of the seven were already in the port; only EquipDesc::set_count(int) was absent.
// It is retail RVA 0xdbad0, one instruction (`MOV [this+0x14],arg`), and is now declared
// in src/include/common_goods.h and defined in src/Common/common_equipdesc.cpp.
//
// Sender 0x06d8c0f0 (IClient+0x158, RET 0xc -> three dword args after this) reads its
// EquipDesc argument field by field, and the offsets it reads are EXACTLY the port's
// EquipDesc layout: Id at +0x02 (06d8c118), ArchId at +0x04 (06d8c124), Count at +0x14
// (06d8c12e, truncated to a word on the wire), Health at +0x10 (06d8c13a), bMission at
// +0x18 (06d8c144). A fourth, independent confirmation of common_goods.h's EquipDesc.
//
// The session id is passed BY REFERENCE and dereferenced by the sender (06d8c14b
// `MOV ECX,[ESP+0xc]` then 06d8c159 `MOV EAX,[ECX] ; PUSH EAX`), and the receiver puts
// the literal 1 back BY REFERENCE (06d82b35).
//
// Three fields of the EquipDesc never reach the wire and are re-created from constants:
// make_internal() (the hardpoint), set_equipped(false) and whatever the ctor leaves.
struct FPayloadServerObjectCargoUpdate {
    unsigned int ObjectId;          // +0x00  06d8c115 <- the sender's 2nd dword argument
    unsigned int ArchId;            // +0x04  06d8c124 <- EquipDesc+0x04
    unsigned short EquipId;         // +0x08  06d8c118 <- EquipDesc+0x02
    unsigned short Count;           // +0x0a  06d8c12e <- EquipDesc+0x14, WORD-truncated
    float Status;                   // +0x0c  06d8c13a <- EquipDesc+0x10
    unsigned char bMission;         // +0x10  06d8c144 <- EquipDesc+0x18
    unsigned char Pad_0x11[3];      // +0x11  neither end touches these
};
static_assert(sizeof(FPayloadServerObjectCargoUpdate) == 0x14,
              "OBJECTCARGOUPDATE payload 0x14");

// ===========================================================================
// The 0x34 "four dwords, eight ids, a count" block
//
// Two unrelated-looking places carry the same 13-dword struct, which is what fixes its
// shape:
//
//   * ch2 0x4f SENDCOMM passes it BY VALUE. The sender copies 13 dwords off its own
//     argument list into a stack scratch buffer (06d89007 "MOV ECX,0xd" / 06d89014
//     "REP MOVSD") and lays the four leading dwords at packet+0x2c..+0x38, the array at
//     packet+0x3c and the count at packet+0x5c (06d8903f "MOV [EAX+0x30],EDX" with EAX
//     == packet+0x2c). The receiver's accessor FUN_06d841c0 is literally
//     "LEA ESI,[this+0x2c] ; MOV ECX,0xd ; REP MOVSD" back out again.
//   * ch2 0x03 CHARACTERINFO holds TWO of them inline in its 0x2dc record, at record
//     +0x274 and +0x2a8: four dwords, then the array at +0x284 / +0x2b8, then the count
//     at +0x2a4 / +0x2d8 (06d84782..06d847b7 and 06d84807..06d84837). 0x274 + 0x34 ==
//     0x2a8 and 0x2a8 + 0x34 == 0x2dc, i.e. the record ends exactly on the second one.
//
// The array capacity is 8 in both: (count offset - array offset) / 4.
// ===========================================================================
struct FRpcIdArrayBlock {
    unsigned int Field_0x00;        // +0x00
    unsigned int Field_0x04;        // +0x04
    unsigned int Field_0x08;        // +0x08
    unsigned int Field_0x0c;        // +0x0c
    unsigned int Ids[8];            // +0x10 .. +0x2f
    int Count;                      // +0x30
};
static_assert(sizeof(FRpcIdArrayBlock) == 0x34,
              "the id block is 13 dwords: four fields, eight ids and a count");

// ch2 0x4f FLPACKET_SERVER_SENDCOMM -- registry 0x78, payload 0x58.
//
// Sender 0x06d88fc0 (IClient+0x110), "RET 0x5c" == 23 dword arguments after `this`;
// receiver caseD_4f at 0x06d8306c. Argument numbering below is A0..A22 with A0 the
// session id, which as always never travels: the receiver pushes the literal 1 for it
// (06d830dc) and the sender reads it back out of its own frame only to hand it to the
// send slot (06d890e2 "MOV ECX,[ESP+0x4c]").
//
// The four-way reading is unusually clean here because the receiver reaches every field
// through a one-instruction accessor:
//
//   A1  packet+0x20  FUN_06d841a0  "MOV EAX,[ECX+0x20] ; RET"
//   A2  packet+0x24  FUN_06d90a40  "MOV EAX,[ECX+0x24] ; RET"
//   A3  packet+0x28  FUN_06d841b0  "MOV EAX,[ECX+0x28] ; RET"
//   A4..A16          FUN_06d841c0  the 13-dword block at packet+0x2c
//   A17 packet+0x60  FUN_06d841e0  "MOV EAX,[ECX+0x60] ; RET"
//   A18 packet+0x64  FUN_06d841f0  "MOV EAX,[ECX+0x64] ; RET"
//   A19 packet+0x68  FUN_06d84200  "MOVSX EAX,byte ptr [ECX+0x68] ; RET"
//   A20 packet+0x6c  FUN_06d84210  "MOV EAX,[ECX+0x6c] ; RET"
//   A21 packet+0x70  FUN_06d84220  "FLD dword ptr [ECX+0x70] ; RET"
//   A22              not read at all -- caseD_4f pushes 0 (06d83083)
//
// Two retail facts worth spelling out, both proven by the bytes above:
//   * BufferCount is a BYTE on the wire even though the sender takes it as a dword.
//     06d8909b is "MOV byte ptr [EDI+0x68],BL", and the reader sign-extends it with
//     MOVSX. A count of 0x80..0xff therefore arrives NEGATIVE at the slot. The port
//     reproduces the truncation and the sign extension exactly.
//   * Field_0x54 is written (06d890db "MOV [EAX+0x74],CL") and the DELIVERY path never
//     reads it back -- caseD_4f pushes 0 in its place (06d83083).
//
//     CORRECTED 2026-09-17: an earlier wave concluded from that alone that it is "a dead
//     field of the retail wire format". IT IS NOT DEAD. The wire codec pair
//     (rpc_wire_ch2.cpp) uses it as the discriminator for the entire id block: the
//     encoder emits it at 06dad0e2, re-reads it at 06dad0ea and jumps over
//     payload+0x0c..+0x3c when it is zero (06dad0ef "JZ LAB_06dad1b6"); the decoder
//     writes it at 06dad40a and branches the same way at 06dad40d, zeroing the block
//     when it is clear (LAB_06dad4dd). The constructor FUN_06d9eab0 even DEFAULTS it to
//     1 (06d9eab7 "MOV EDX,0x1" / 06d9eae4 "MOV byte ptr [EAX+0x74],DL"). "The receiver
//     does not read it" was true of one of the three readers.
struct FPayloadServerSendComm {
    unsigned int Field_0x00;        // +0x00 <- A1
    unsigned int Field_0x04;        // +0x04 <- A2
    unsigned int Field_0x08;        // +0x08 <- A3
    FRpcIdArrayBlock Body;          // +0x0c .. +0x3f <- A4..A16
    unsigned int Field_0x40;        // +0x40 <- A17
    unsigned int* Buffer;           // +0x44    operator new(A19 << 2), copied from A18
    unsigned char BufferCount;      // +0x48 <- LOW BYTE of A19
    unsigned char Pad_0x49[3];      // +0x49
    unsigned int Field_0x4c;        // +0x4c <- A20
    // The float travels as its four raw bytes. Retail moves it with FLD/FSTP on both
    // sides and the IClient slot takes it as a stack dword, so bits are the whole story.
    unsigned int Field_0x50Bits;    // +0x50 <- A21
    unsigned char Field_0x54;       // +0x54 <- LOW BYTE of A22; never read back
    unsigned char Pad_0x55[3];      // +0x55
};
static_assert(sizeof(FPayloadServerSendComm) == 0x58, "SENDCOMM payload 0x58");

// The channel-2 codecs live in src/rpclocal/rpc_payload_ch2.cpp.
// Three more channel-2 payloads -- 0x03 CHARACTERINFO, 0x04 CREATESHIP and 0x42
// CREATESOLAR -- are declared in rpc_containers.h instead, because they are built out of
// the container mirrors that live there.
extern const FRpcPayloadCodec GChannel2Codecs[];
extern const unsigned int GChannel2CodecCount;

// ===========================================================================
// Channel-3 payload structures (client -> server, the IServer direction).
//
// Ground truth, three readings per type, exactly as for channels 1 and 2:
//
//   1. the SENDER -- the body of the IServerImpl slot in EXE/Disasm/rpclocal.dll.c /
//      .txt that calls FUN_06d96300(Serializer, 3, Opcode, &OutPacket) and then writes
//      the payload from packet+0x20 on;
//   2. the RECEIVER -- the matching case of the channel-3 switch inside FUN_06d86a70
//      (the client-side dispatcher, 1518 instructions), which reads those same bytes
//      back and calls exactly one vtable slot of the real IServer inside Server.dll
//      (.data:DAT_06dbbd24 == DAT_06dbbd5c);
//   3. the SIZE -- the packet class's own operator new() argument from the registry
//      (rpc_packet_registry.cpp): payload == that size minus the 0x20-byte base.
//
// Where the sender permutes its argument struct (OBJCOLLISION, BADLANDSOBJCOLLISION,
// REQMODIFYITEM, REQINVINCIBILITY, CREATENEWCHAR) the receiver's permutation is the
// strict inverse -- the hardest of the three checks, and it holds for all five.
//
// A fourth reading is available on this channel and was used: the retail IServer
// vtable in src/include/common_server.h (IServer card in decomp-tracker) gives the slot
// offset AND the parameter list of every call the receiver makes, so the recovered
// field order has to agree with a signature recovered independently. All 50 closed
// types agree.
//
// ARGUMENT ORDER NOTE. On channels 1 and 2 the first argument is the session/client id
// and the receiver replaces it with a literal 1. Channel 3 is the other way round: the
// session id is usually the LAST argument of the IServer method, it is never
// serialized, and the receiver puts its own param_1 (the connection id the pump passes
// in) back in that place. A handful of methods take the session FIRST instead
// (InterfaceItemUsed, RTCDone, AbortMission, MissionSaveB, SetManeuver) -- for those
// retail serializes the SECOND argument. Every codec below says which.
//
// Marshal() argument convention for channel 3: Args[i] is the i-th dword argument
// after this, in the order src/include/common_server.h declares it.
// ===========================================================================

// --- generic shapes, reused by many opcodes -------------------------------

// payload 0x04. ch3 0x04, 0x07, 0x0b, 0x0d, 0x0e, 0x0f, 0x13, 0x15, 0x1b, 0x1c, 0x24,
// 0x35, 0x3b, 0x41, 0x45, 0x46, 0x47.
struct FPayloadClientDword {
    unsigned int Field_0x00;
};
static_assert(sizeof(FPayloadClientDword) == 0x04, "ch3 single-dword payload 0x04");

// payload 0x08. ch3 0x19, 0x2b, 0x3e, 0x43.
struct FPayloadClientDwordPair {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};
static_assert(sizeof(FPayloadClientDwordPair) == 0x08, "ch3 dword-pair payload 0x08");

// payload 0x0c. ch3 0x11 GFGOODSELL, 0x3c GFGOODVAPORIZED, 0x2f SETMANEUVER, 0x3f HAIL.
struct FPayloadClientDwordTriple {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};
static_assert(sizeof(FPayloadClientDwordTriple) == 0x0c, "ch3 dword-triple payload 0x0c");

// payload 0x10. ch3 0x12 GFGOODBUY, 0x37 COMMCOMPLETE.
struct FPayloadClientDwordQuad {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
};
static_assert(sizeof(FPayloadClientDwordQuad) == 0x10, "ch3 dword-quad payload 0x10");

// --- per-opcode shapes ----------------------------------------------------

// ch3 0x01 FLPACKET_CLIENT_LOGIN, packet size 0x424 -> payload 0x404.
// Sender IServerImpl::Login 0x06d8cc70, receiver caseD_1 of FUN_06d86a70.
//
// The sender wcscpy's the account out of SLoginInfo+0x00 into a wchar_t[256] stack
// buffer, wcscpy's a SECOND wide string out of SLoginInfo+0xa0 into the next
// wchar_t[256], puts the byte at SLoginInfo+0xc0 behind them and blits 0x402 bytes
// (0x100 dwords plus one trailing word) into the packet. The receiver copies the same
// 0x402 bytes back out, then wcsncpy's the first string into a file-static SLoginInfo
// at .data:0x06dbba38 with a cap of 0x28 wide characters, the second into that same
// struct at +0xa0 with a cap of 0x10, NUL-terminates both (.data:0x06dbba86 and
// 0x06dbbaf6, i.e. +0x4e and +0xbe) and stores the flag byte at +0xc0.
//
// That is two independent readings of the SAME retail struct, and they disagree with
// src/include/common_server_types.h, which declares SLoginInfo as a single
// wchar_t AccountName[96] covering +0x00..+0xbf. The second wide string at +0xa0 is
// real -- both the sender and the receiver touch it -- so AccountName cannot be longer
// than 0x50 wide characters. The overall 0xc8 size in that header is right; the field
// break is not. Reported rather than changed: it is a cross-module header.
//
// DEVIATION: retail blits the whole 0x402-byte stack window, so everything past each
// string's NUL is uninitialised stack. The marshal below zeroes the payload first.
struct FPayloadClientLogin {
    wchar_t AccountName[0x100];   // +0x000  wcscpy'd, receiver re-caps it at 0x28 wchars
    wchar_t Password[0x100];      // +0x200  wcscpy'd from SLoginInfo+0xa0, re-capped at 0x10
    unsigned char CreateFlag;     // +0x400  <- SLoginInfo+0xc0
    unsigned char Pad_0x401[3];
};
static_assert(sizeof(FPayloadClientLogin) == 0x404, "LOGIN payload 0x404");

// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION, packet size 0x3c -> payload 0x1c.
// Sender IServerImpl::SPMunitionCollision 0x06d8d6e0, receiver caseD_3.
// Identity mapping of SSPMunitionCollisionInfo; the receiver rebuilds the struct
// field by field on its own stack and calls IServer+0xd4 (slot 53) with it.
struct FPayloadClientMunCollision {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int ObjectId;        // +0x08
    unsigned short Field_0x0c;    // +0x0c  the only 2-byte write on either side
    unsigned short Pad_0x0e;
    unsigned int Field_0x10;
    unsigned int Field_0x14;
    unsigned int Field_0x18;
};
static_assert(sizeof(FPayloadClientMunCollision) == 0x1c, "MUNCOLLISION payload 0x1c");

// ch3 0x06 FLPACKET_CLIENT_SELECTCHARACTER, size 0x220 -> payload 0x200, and
// ch3 0x3a FLPACKET_CLIENT_DESTROYCHAR, size 0x220 -> payload 0x200.
// Senders 0x06d8cd60 / 0x06d8ce90 do CHARACTER_ID::operator=(packet+0x20, arg); the
// receiver passes packet+0x20 straight to IServer+0x50 / +0x5c as a CHARACTER_ID
// const&. The payload IS a CHARACTER_ID, so no separate wire struct is declared --
// see the 0x200 static_assert on CHARACTER_ID in common_server_types.h.

// ch3 0x08 FLPACKET_CLIENT_REQUESTBASEINFO and 0x09 REQUESTLOCATIONINFO,
// size 0x28 -> payload 0x08. Senders 0x06d8d500 / 0x06d8d550.
// The session id (2nd argument) is dropped; the bool 3rd argument travels as a byte.
struct FPayloadClientInfoRequest {
    unsigned int Id;              // +0x00  base / location id
    unsigned char bFull;          // +0x04
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadClientInfoRequest) == 0x08, "REQUEST*INFO payload 0x08");

// ch3 0x0c FLPACKET_CLIENT_OBJCOLLISION, size 0x34 -> payload 0x14.
// Sender IServerImpl::SPObjCollision 0x06d8d7b0, receiver caseD_c.
// The sender swaps the two halves of SSPObjCollisionInfo -- arg+0x00 goes to wire+0x08,
// arg+0x08 goes to wire+0x00 -- so the object's OWN id leads on the wire. The receiver
// swaps them back, exactly.
struct FPayloadClientObjCollision {
    unsigned int ObjectId;          // +0x00  <- SSPObjCollisionInfo+0x08
    unsigned short SubObjId;        // +0x04  <- +0x0c
    unsigned short Pad_0x06;
    unsigned int TargetObjectId;    // +0x08  <- +0x00
    unsigned short TargetSubObjId;  // +0x0c  <- +0x04
    unsigned short Pad_0x0e;
    unsigned int Field_0x10;        // +0x10  <- +0x10
};
static_assert(sizeof(FPayloadClientObjCollision) == 0x14, "OBJCOLLISION payload 0x14");

// ch3 0x14 FLPACKET_CLIENT_MISSIONRESPONSE, size 0x2c -> payload 0x0c.
// Sender IServerImpl::MissionResponse 0x06d8da80, receiver caseD_14.
struct FPayloadClientMissionResponse {
    unsigned int Field_0x00;      // +0x00  <- 1st argument
    unsigned int MissionId;       // +0x04  <- 2nd argument (ulong)
    unsigned char bAccepted;      // +0x08  <- 3rd argument
    unsigned char Pad_0x09[3];
};
static_assert(sizeof(FPayloadClientMissionResponse) == 0x0c, "MISSIONRESPONSE payload 0x0c");

// ch3 0x18 FLPACKET_CLIENT_REQADDITEM, size 0x38 -> payload 0x18.
// Sender IServerImpl::ReqAddItem 0x06d8d230. The hardpoint string goes through the
// block writer FUN_06d9a160, which does operator new(len), REP MOVSD/MOVSB, then
// [packet+0x34] = block and [packet+0x28] = len -- i.e. payload+0x14 and payload+0x08.
// The packet OWNS that copy, so this type is self-contained.
// Receiver caseD_18 calls IServer+0x78 (slot 30) as
//   ReqAddItem(payload+0x00, payload+0x14, payload+0x04, payload+0x0c, payload+0x10, Session)
// which is exactly the inverse.
struct FPayloadClientReqAddItem {
    unsigned int ArchId;          // +0x00
    int Count;                    // +0x04
    unsigned int HardpointSize;   // +0x08  written by FUN_06d9a160
    float Health;                 // +0x0c
    unsigned char bMounted;       // +0x10
    unsigned char Pad_0x11[3];
    char* Hardpoint;              // +0x14  packet-owned copy
};
static_assert(sizeof(FPayloadClientReqAddItem) == 0x18, "REQADDITEM payload 0x18");

// ch3 0x1a FLPACKET_CLIENT_REQMODIFYITEM, size 0x38 -> payload 0x18.
// Sender IServerImpl::ReqModifyItem 0x06d8d300, block writer FUN_06d9a5f0 (same code as
// FUN_06d9a160 except the length lands at [packet+0x24] = payload+0x04). Note that the
// first three fields are in a DIFFERENT order from REQADDITEM: Count leads, the equip id
// sits at +0x08. Receiver caseD_1a reads them back in that order.
struct FPayloadClientReqModifyItem {
    int Count;                    // +0x00
    unsigned int HardpointSize;   // +0x04  written by FUN_06d9a5f0
    unsigned int EquipId;         // +0x08  zero-extended from a ushort, read back as a word
    float Health;                 // +0x0c
    unsigned char bMounted;       // +0x10
    unsigned char Pad_0x11[3];
    char* Hardpoint;              // +0x14  packet-owned copy
};
static_assert(sizeof(FPayloadClientReqModifyItem) == 0x18, "REQMODIFYITEM payload 0x18");

// ch3 0x1e FLPACKET_CLIENT_SAVEGAME, size 0x228 -> payload 0x208.
// Sender IServerImpl::SaveGame 0x06d8db10: CHARACTER_ID::operator= into packet+0x20,
// then FUN_06d9aab0(packet, description, wcslen+1) -- that writer allocates
// (count * 2) bytes (LEA EBX,[EAX + EAX*0x1]), copies them, and stores the block at
// [packet+0x220] and the WIDE-CHARACTER count at [packet+0x224]. Receiver caseD_1e
// calls IServer+0xe8 (slot 58) with (packet+0x20, *(packet+0x220), Session) -- it never
// reads the count, but the sender writes it, so it is part of the layout.
struct FPayloadClientSaveGame {
    unsigned char CharId[0x200];      // +0x000  a CHARACTER_ID
    wchar_t* Description;             // +0x200  packet-owned wide string copy
    unsigned int DescriptionCount;    // +0x204  in wide characters, including the NUL
};
static_assert(sizeof(FPayloadClientSaveGame) == 0x208, "SAVEGAME payload 0x208");

// ch3 0x20 FLPACKET_CLIENT_MINEASTEROID, size 0x3c -> payload 0x1c.
// Sender IServerImpl::MineAsteroid 0x06d8dcf0 flattens the Vector inline; receiver
// caseD_20 rebuilds it on its own stack and calls IServer+0xf8 (slot 62).
struct FPayloadClientMineAsteroid {
    unsigned int SystemId;        // +0x00
    float PositionX;              // +0x04  <- Vector+0x00
    float PositionY;              // +0x08
    float PositionZ;              // +0x0c
    unsigned int LootId;          // +0x10
    unsigned int SecondaryLoot;   // +0x14
    unsigned int Quantity;        // +0x18
};
static_assert(sizeof(FPayloadClientMineAsteroid) == 0x1c, "MINEASTEROID payload 0x1c");

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP, size 0x90 -> payload 0x70.
//
// SIZE CORRECTION, 2026-09-17, on the bytes. The tracker note that identified this
// type's two codec slots said "size 0x70 -> payload 0x50". It is not: the factory
// FUN_06d9f680 does `06d9f685 PUSH 0x90 ; 06d9f690 CALL operator_new` before handing
// the block to the ctor FUN_06da0e20, so the object is 0x90 and the payload is 0x70.
// rpc_packet_registry.cpp already had 0x90 from the same instruction and was right.
// The layout below then closes the loop from the other end: the highest packet offset
// either codec touches is +0x8c, a dword, i.e. the payload's last four bytes at
// +0x6c..+0x6f. Every one of the 0x70 bytes is accounted for and none spills over.
//
// WHERE THE LAYOUT COMES FROM -- four independent readings, no guessing:
//   1. the ENCODER LAB_06db0560 (vtable +0x04), field by field;
//   2. the DECODER LAB_06db0b30 (vtable +0x08), which is its strict inverse;
//   3. the CTOR FUN_06da0e20, for which fields have defaults;
//   4. the DTOR FUN_06db0500, which frees exactly four pointers -- packet+0x60,
//      +0x88, +0x8c and +0x84 (06db0503 / 06db0519 / 06db052c / 06db053f, each
//      null-checked and passed to FUN_06db1dc2 = operator delete). Those four and only
//      those four are the packet-owned blocks, which is what pins the pointer fields
//      apart from the plain dwords.
//
// CTOR DEFAULTS, and a correction to the same tracker note. It claimed the ctor
// "zeroes +0x20..+0x2c"; it does not touch +0x20, +0x24, +0x28, +0x2c or +0x30 at all.
// What FUN_06da0e20 actually writes is: +0x04 = 1, +0x08 = +0x10 = +0x1c = 0 (base
// fields), then +0x34..+0x48 = 0, +0x4c = 0x3f800000, +0x50..+0x6c = 0 and
// +0x70..+0x8c = 0. So payload+0x00..+0x10 are left UNINITIALIZED by retail, and the
// single non-zero default is the 1.0f at payload+0x2c -- the w of the orientation
// quaternion, i.e. the identity rotation. FPacket_FLPACKET_CLIENT_DBGCREATESHIP in
// rpc_packet_registry.cpp reproduces it.
//
// THE COPIERS settle three of the fields' types without a sender to read:
//   * payload+0x14 and +0x20 go through FUN_06d8f510, the three-dword copier this port
//     already maps to Vector (06db06b7 / 06db070c out, 06db0c6f / 06db0ca1 back);
//   * payload+0x2c goes through FUN_06d8f4f0, the four-dword copier mapped to
//     Quaternion (06db0766 out, 06db0cd3 back). The ctor's 1.0f lands on its first
//     dword, which is Quaternion's w -- an identity default, and the one cross-check
//     that this really is a quaternion and not four loose dwords.
//
// FOUR VARIABLE-LENGTH BLOCKS, each a dword count followed by that many ELEMENTS. The
// element width is read off the encoder's cursor arithmetic, not assumed:
//   * payload+0x44 count -> payload+0x40 block, 06db07e9 `ADD EAX,EAX` and 06db0814
//     `SHL EAX,0x1` -- TWO bytes per element, so a wide string, count in characters.
//     The decoder allocates count*2 (06db0d3c `ADD EAX,EAX` before operator_new).
//   * payload+0x48 count -> payload+0x68 block, 06db0878 `ADD ESI,EAX` -- ONE byte per
//     element; decoder allocates count (06db0dcb PUSH EAX).
//   * payload+0x4c count -> payload+0x6c block, 06db0906 / 06db0e5a -- ONE byte.
//   * payload+0x60 count -> payload+0x64 block, 06db0a6a `SHL EAX,0x2` and 06db0fb3 --
//     FOUR bytes per element, a dword array.
// Count and pointer are NOT adjacent for three of the four; the counts sit in the
// middle of the record and their pointers are parked at the end. That is retail's own
// field order, which is why this is not a {Size, Buffer} pair family like
// FPayloadClientBlob and cannot be folded into one.
//
// The five leading dwords and the four at +0x50..+0x5c carry no sender and no receiver
// anywhere in retail -- FLPACKET_CLIENT_DBGCREATESHIP is registered but never built in
// the release image, and the ch3 dispatcher answers it with an FDUMP. Only the codecs'
// widths are proven, so they stay Field_0xNN with the width they are read at.
struct FPayloadClientDbgCreateShip {
    unsigned int Field_0x00;        // +0x00  dword, 06db05c9 / 06db0ba0
    unsigned int Field_0x04;        // +0x04  dword, 06db05f3 / 06db0bca
    unsigned int Field_0x08;        // +0x08  dword, 06db061d / 06db0bf4
    unsigned int Field_0x0c;        // +0x0c  dword, 06db0647 / 06db0c1e
    unsigned int Field_0x10;        // +0x10  dword, 06db0671 / 06db0c48
    Vector Field_0x14;              // +0x14  FUN_06d8f510
    Vector Field_0x20;              // +0x20  FUN_06d8f510
    Quaternion Orientation;         // +0x2c  FUN_06d8f4f0, ctor default {1,0,0,0}
    unsigned int Field_0x3c;        // +0x3c  dword, 06db07a1 / 06db0d09
    wchar_t* NameChars;             // +0x40  packet-owned, freed at 06db0503
    unsigned int NameCharCount;     // +0x44  in wide characters
    unsigned int BlockASize;        // +0x48  in bytes
    unsigned int BlockBSize;        // +0x4c  in bytes
    unsigned int Field_0x50;        // +0x50  dword, 06db097d / 06db0ed8
    unsigned int Field_0x54;        // +0x54  dword, 06db09b1 / 06db0f04
    unsigned int Field_0x58;        // +0x58  dword, 06db09e5 / 06db0f30
    unsigned int Field_0x5c;        // +0x5c  dword, 06db0a19 / 06db0f5c
    unsigned int DwordArrayCount;   // +0x60  element count, not a byte count
    unsigned int* DwordArray;       // +0x64  packet-owned, freed at 06db053f
    void* BlockA;                   // +0x68  packet-owned, freed at 06db0519
    void* BlockB;                   // +0x6c  packet-owned, freed at 06db052c
};
static_assert(sizeof(FPayloadClientDbgCreateShip) == 0x70, "DBGCREATESHIP payload 0x70");

// ch3 0x25 FLPACKET_CLIENT_DBGDESTROYOBJECT, size 0x28 -> payload 0x08.
// No sender anywhere in the release image: the only instruction in rpclocal that writes
// 0x30025 is the type's own constructor FUN_06da0f90 at 06da0faa, and the ch3 dispatcher
// answers the opcode with an FDUMP. The layout therefore comes from the codec pair alone
// (LAB_06db12c0 / LAB_06db13e0), which touches a dword at payload+0x00 (06db1330 /
// 06db1444) and a word at payload+0x04 (06db1365 / 06db146e) and nothing else. Widths
// are proven; meanings are not, so the fields keep their offsets for names.
struct FPayloadClientDbgDestroyObject {
    unsigned int Field_0x00;      // +0x00  dword
    unsigned short Field_0x04;    // +0x04  word
    unsigned short Pad_0x06;      // +0x06  neither direction touches it
};
static_assert(sizeof(FPayloadClientDbgDestroyObject) == 0x08, "DBGDESTROYOBJECT payload 0x08");

// ch3 0x27 FLPACKET_CLIENT_TRADERESPONSE, size 0x28 -> payload 0x08.
// Sender IServerImpl::TradeResponse 0x06d8dad0 uses FUN_06d97b00, the very same block
// writer channel 2 uses for its blob family: length at payload+0x00, owned copy at
// payload+0x04. Receiver caseD_27 calls IServer+0xb0 (slot 44) as
// TradeResponse(payload+0x04, payload+0x00, Session).
struct FPayloadClientBlob {
    unsigned int Size;            // +0x00
    void* Buffer;                 // +0x04  packet-owned copy
};
static_assert(sizeof(FPayloadClientBlob) == 0x08, "TRADERESPONSE payload 0x08");

// ch3 0x2d FLPACKET_CLIENT_DBGCONSOLE, size 0xa4 -> payload 0x84.
// Encoder LAB_06db1670, decoder LAB_06db17a0, both transcribed in rpc_wire_fields.cpp.
// Only two fields, and the split is fixed by the two reservations the encoder makes:
// 06db16be `ADD EDX,0x4` for the leading dword and 06db16f4 `ADD EDX,0x80` for the run
// that follows, which 06db1720 `MOV ECX,0x20` + 06db1728 `MOVSD.REP ES:EDI,ESI` copies
// from 06db1725 `LEA ESI,[EBP + 0x24]` == payload+0x04. 4 + 128 == 0x84 exactly, so
// there is no third field and no tail padding.
//
// The 128-byte run is copied WHOLE, not as a NUL-terminated string: there is no SCASB
// and no length prefix anywhere in either direction, unlike ch3 0x06/0x3a. So the
// trailing bytes past the terminator go out on the wire as they sit in the packet.
// Kept as a char array rather than narrowed to a string for that reason.
//
// Field_0x00 keeps its placeholder name: the type is debug-only -- DeliverDbgConsole in
// rpc_payload_ch3.cpp reports "encountered in release mode" -- so no retail sender in
// this module writes either field, and nothing fixes the dword's meaning.
struct FPayloadClientDbgConsole {
    unsigned int Field_0x00;      // +0x00  TODO: name unknown, no sender in rpclocal
    char Text_0x04[128];          // +0x04  fixed 128-byte run, sent in full
};
static_assert(sizeof(FPayloadClientDbgConsole) == 0x84, "DBGCONSOLE payload 0x84");

// ch3 0x31 FLPACKET_CLIENT_REQUEST_EVENT, size 0x34 -> payload 0x14.
// Sender IServerImpl::RequestEvent 0x06d8eaf0. Retail TRUNCATES both the int first
// argument and the ulong fifth argument to a single byte on the wire, and the receiver
// reads them back as bytes -- a real retail information loss, reproduced here.
struct FPayloadClientRequestEvent {
    unsigned char EventType;      // +0x00  <- int, truncated
    unsigned char Pad_0x01[3];
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
    unsigned char Field_0x10;     // +0x10  <- ulong, truncated
    unsigned char Pad_0x11[3];
};
static_assert(sizeof(FPayloadClientRequestEvent) == 0x14, "REQUEST_EVENT payload 0x14");

// ch3 0x32 FLPACKET_CLIENT_REQUEST_CANCEL, size 0x30 -> payload 0x10.
// Sender IServerImpl::RequestCancel 0x06d8eb60, same truncation habit.
struct FPayloadClientRequestCancel {
    unsigned char EventType;      // +0x00  <- int, truncated
    unsigned char Pad_0x01[3];
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned char Field_0x0c;     // +0x0c  <- ulong, truncated
    unsigned char Pad_0x0d[3];
};
static_assert(sizeof(FPayloadClientRequestCancel) == 0x10, "REQUEST_CANCEL payload 0x10");

// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR, size 0xb0 -> payload 0x90.
// Sender IServerImpl::CreateNewCharacter 0x06d8cdf0 does
// wcsncpy(scratch, (wchar_t*)Info, 0x40) -- 0x80 bytes -- zeroes the last wide character,
// copies the four dwords out of SCreateCharacterInfo+0x30..+0x3c into scratch+0x80..+0x8c
// and blits 0x24 dwords into the packet. Receiver caseD_39 wcscpy's the name into a
// file-static SCreateCharacterInfo at .data:0x06dbbb10 and puts wire+0x80..+0x8c back at
// struct +0x30..+0x3c, i.e. the strict inverse of the sender's field moves.
//
// The name window on the WIRE is 0x80 bytes while SCreateCharacterInfo::Name is only
// 0x30 (both facts are retail: the wcsncpy count over-reads a NUL-terminated field into
// a 0x80-byte stack buffer). The marshal below copies only the 0x30 bytes the struct
// actually owns and zeroes the rest, which is the one place we are tidier than retail.
struct FPayloadClientCreateNewChar {
    wchar_t Name[0x40];           // +0x00 .. +0x7f
    unsigned int Field_0x80;      // +0x80  <- SCreateCharacterInfo+0x30
    unsigned int Field_0x84;      // +0x84  <- +0x34
    unsigned int Field_0x88;      // +0x88  <- +0x38
    unsigned int Field_0x8c;      // +0x8c  <- +0x3c
};
static_assert(sizeof(FPayloadClientCreateNewChar) == 0x90, "CREATENEWCHAR payload 0x90");

// ch3 0x3d FLPACKET_CLIENT_BADLANDSOBJCOLLISION, size 0x2c -> payload 0x0c.
// Sender IServerImpl::SPBadLandsObjCollision 0x06d8d760 rotates the three fields of
// SSPBadLandsObjCollisionInfo (arg+0x00 -> wire+0x08, arg+0x04 -> wire+0x00,
// arg+0x08 -> wire+0x04); receiver caseD_3d rotates them back.
struct FPayloadClientBadLandsObjCollision {
    unsigned int ObjectId;        // +0x00  <- SSPBadLandsObjCollisionInfo+0x04
    unsigned short Field_0x04;    // +0x04  <- +0x08
    unsigned short Pad_0x06;
    unsigned int Field_0x08;      // +0x08  <- +0x00
};
static_assert(sizeof(FPayloadClientBadLandsObjCollision) == 0x0c, "BADLANDSOBJCOLLISION payload 0x0c");

// ch3 0x40 FLPACKET_CLIENT_REQUEST_USE_ITEM, size 0x28 -> payload 0x08.
// Sender IServerImpl::SPRequestUseItem 0x06d8ec20, identity mapping of SSPUseItem.
struct FPayloadClientUseItem {
    unsigned int ObjectId;        // +0x00
    unsigned short ItemId;        // +0x04
    unsigned short Count;         // +0x06
};
static_assert(sizeof(FPayloadClientUseItem) == 0x08, "REQUEST_USE_ITEM payload 0x08");

// ch3 0x44 FLPACKET_CLIENT_REQINVINCIBILITY, size 0x28 -> payload 0x08.
// Sender IServerImpl::SPRequestInvincibility 0x06d8ec80 writes the REASON at +0x04 and
// the BOOL at +0x05 -- the reverse of the declared argument order -- and receiver
// caseD_44 reads payload+0x05 as the bool and payload+0x04 as the reason, which is the
// inverse. That crossing is what proves the two byte fields apart.
struct FPayloadClientInvincibility {
    unsigned int ShipId;          // +0x00
    unsigned char Reason;         // +0x04  InvincibilityReason, one byte on the wire
    unsigned char bInvincible;    // +0x05
    unsigned char Pad_0x06[2];
};
static_assert(sizeof(FPayloadClientInvincibility) == 0x08, "REQINVINCIBILITY payload 0x08");

// ===========================================================================
// Channel-3 codec table. The deliver side of this channel does NOT call IClient --
// it calls the real IServer inside Server.dll, so it needs its own signature.
// ===========================================================================
struct IServer;
class IChatServer;

// The interfaces Server.dll handed back, cached exactly where retail caches them:
// .data:0x06dbbd24 (IServer, an alias of 0x06dbbd5c) and .data:0x06dbbd20
// (IChatServer, from 0x06dbbd60). IServerImpl::Startup publishes both, Shutdown clears
// them, and FUN_06d86a70 is their only reader. Defined in rpc_payload_ch3.cpp.
extern IServer* GRealServer;
extern IChatServer* GRealChatServer;

// Rebuilds the retail argument list out of the payload and makes the single IServer
// call that FUN_06d86a70's matching case makes. SessionId is the dispatcher's own
// param_1, which retail substitutes wherever the session id was dropped.
typedef bool (*FRpcServerDeliverFn)(const FPacketBase* Packet, IServer* Server, unsigned int SessionId);

struct FRpcServerCodec {
    unsigned short Channel;
    unsigned short Opcode;
    const char* Name;              // the retail FLPACKET_* string
    unsigned short PayloadSize;    // registry size minus the 0x20 base
    FRpcMarshalFn Marshal;         // nullptr for the debug types retail never sends
    FRpcServerDeliverFn Deliver;
};

// The channel-3 codecs live in src/rpclocal/rpc_payload_ch3.cpp.
extern const FRpcServerCodec GChannel3Codecs[];
extern const unsigned int GChannel3CodecCount;

// nullptr when this type's channel-3 layout is not recovered yet.
const FRpcServerCodec* RpcFindServerCodec(unsigned int Channel, unsigned int Opcode);

// The port of retail FUN_06d86a70: the client-side dispatcher for channels 1 and 3.
// Returns true when the packet was routed into a real IServer slot.
bool RpcDispatchToServer(FPacketBase* Packet, unsigned int SessionId);

// Bodies of the exported non-virtual members IServerImpl::HandleCharacterInfo
// (0x06d84490, ch2 0x03) and IServerImpl::HandleDamageObject (0x06d84c60, ch2 0x05).
// Arg is the value both pass as the IClient slot's first argument (06d84a9b / 06d84cd7).
// Both live in src/rpclocal/rpc_payload_ch2.cpp beside their codecs.
__declspec(dllexport) bool RpcHandleCharacterInfo(const FPacketBase* Packet, IClient* Client, unsigned int Arg);
__declspec(dllexport) bool RpcHandleDamageObject(const FPacketBase* Packet, IClient* Client, unsigned int Arg);
