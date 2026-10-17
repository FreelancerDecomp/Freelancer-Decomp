// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payload_ch1.cpp -- the 21 channel-1 packet types, marshalled and delivered the way
// retail does it.
//
// Channel 1 is the "common" channel: the same 21 packet types travel in BOTH directions,
// which is why every layout here has two independent readings in the listing --
// rpclocal's IClient slot (server -> client, the direction this file implements) and
// rpclocal's IServerImpl method (client -> server, written up in
// the decomp-tracker IServer card). Where a documented struct exists the two agree field for
// field, and that agreement is what promotes these layouts from "read once" to "checked".
//
// Every Deliver() below reproduces its retail HandlePacket case exactly, including the
// literal 1 retail pushes in place of the session id. Retail addresses are quoted per
// codec; the receive-side case labels are the ones Ghidra assigns inside the channel-1
// switch that starts at 0x06d83256.

#include "rpc_payloads.h"
#include "rpc_iclient.h"
#include "rpc_containers.h"
#include "common_goods.h"

#include <new>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Copies InSize bytes into a block the packet owns from now on. Retail does the same with
// a bare operator new (e.g. 0x06d8c4aa for CHATMSG, 0x06d8b5b1 for REQUEST_BEST_PATH,
// 0x06d8b631 for REQUEST_PLAYER_STATS) -- a zero size included, which still hands back a
// non-null zero-byte block. Where retail tests the source or the size first (CHATMSG,
// 06d8c499 / 06d8c4a1), the caller carries that test.
static void* RpcCopyIntoPacketBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

static void RpcFreePacketBlock(void* Block) {
    if (Block != nullptr) {
        ::operator delete(Block);
    }
}

// ===========================================================================
// ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT
// Sender 0x06d897c0 (IClient+0x0d8, RET 8), receiver caseD_1 at 0x06d83a4a.
//   payload+0x00 = byte  arg+0x28        payload+0x04 = dword arg+0x00
//   payload+0x08 = Vector arg+0x14 (FUN_06d8f510)
//   payload+0x14 = Quaternion arg+0x04 (FUN_06d8f4f0)
//   payload+0x24 = dword arg+0x20        payload+0x28 = dword arg+0x24
// ===========================================================================
static bool MarshalUpdateObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FCommonUpdateObjectInfo* Info =
        reinterpret_cast<const FCommonUpdateObjectInfo*>(Args[1]);
    FPayloadCommonUpdateObject Wire;
    Wire.Field_0x00 = Info->Field_0x28;
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ObjectId = Info->ObjectId;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    Wire.Field_0x24 = Info->Field_0x20;
    Wire.Field_0x28 = Info->Field_0x24;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverUpdateObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonUpdateObject Wire = RpcPayloadRead<FPayloadCommonUpdateObject>(Packet, 0);
    FCommonUpdateObjectInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.Orientation = Wire.Orientation;
    Info.Position = Wire.Position;
    Info.Field_0x20 = Wire.Field_0x24;
    Info.Field_0x24 = Wire.Field_0x28;
    Info.Field_0x28 = Wire.Field_0x00;
    Info.Pad_0x29[0] = 0;
    Info.Pad_0x29[1] = 0;
    Info.Pad_0x29[2] = 0;
    Client->SendCommonUpdateObject(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON
// Sender 0x06d8b310 (IClient+0x000, RET 8), receiver case 2.
// Argument struct = XFireWeaponInfo (see common_server_types.h).
// ===========================================================================
static bool MarshalFireWeapon(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XFireWeaponInfo* Info = reinterpret_cast<const XFireWeaponInfo*>(Args[1]);
    FPayloadCommonFireWeapon Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TargetPos = Info->TargetPos;
    Wire.Hardpoints.AllocPad = 0;
    Wire.Hardpoints.Begin = nullptr;
    Wire.Hardpoints.End = nullptr;
    Wire.Hardpoints.Cap = nullptr;

    // Retail's `(last - first) >> 1` element count, then a word-by-word copy into a block
    // the packet keeps. std::vector::size() is the same arithmetic, data() its _First.
    const unsigned int Count = static_cast<unsigned int>(Info->Hardpoints.size());
    if (Count != 0) {
        // The inlined vector operator= (06d8b370..06d8b4cb): an empty destination gets
        // _Allocate(size) -- a bare operator new -- and the elements copied in.
        unsigned short* Block = static_cast<unsigned short*>(
            RpcCopyIntoPacketBlock(Info->Hardpoints.data(), Count * 2u));
        Wire.Hardpoints.Begin = Block;
        Wire.Hardpoints.End = Block + Count;
        Wire.Hardpoints.Cap = Block + Count;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// caseD_2 0x06d833c9: a local XFireWeaponInfo at [ESP+0x50] -- COMMON's
// vector<unsigned short> ctor for +0x10 (833f0), +0x00..+0x0c copied from payload
// +0x00..+0x0c (833f6..83422), then the inlined vector operator= from payload+0x10
// (83426..8353c): an empty source leaves the local empty, otherwise one operator new of
// Count*2 bytes holds a COPY (8351d / 83536). IClient slot 0 (83553) gets (1, &local);
// the local vector's destructor frees the copy (83564).
static bool DeliverFireWeapon(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonFireWeapon Wire = RpcPayloadRead<FPayloadCommonFireWeapon>(Packet, 0);
    XFireWeaponInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TargetPos = Wire.TargetPos;
    // operator= from the payload's vector: empty stays empty, otherwise one block of
    // exactly Count elements holding a copy -- assign() over the wire range.
    const unsigned int Count = Wire.Hardpoints.Count();
    if (Count != 0) {
        Info.Hardpoints.assign(Wire.Hardpoints.Begin, Wire.Hardpoints.Begin + Count);
    }
    Client->SendCommonFireWeapon(1u, Info);
    // 83564: the local vector's destructor frees the copy -- ~XFireWeaponInfo.
    return true;
}

// ===========================================================================
// ch1 op 0x04 FLPACKET_COMMON_SETTARGET
// Sender 0x06d8bd50 (IClient+0x010, RET 8), receiver caseD_4 at 0x06d838c9.
// Argument struct = XSetTarget (В§3.5).
// ===========================================================================
static bool MarshalSetTarget(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XSetTarget* Info = reinterpret_cast<const XSetTarget*>(Args[1]);
    FPayloadCommonSetTarget Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TargetObjectId = Info->TargetObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.TargetSubObjId = Info->TargetSubObjId;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetTarget(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonSetTarget Wire = RpcPayloadRead<FPayloadCommonSetTarget>(Packet, 0);
    XSetTarget Info;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x06 = 0;
    Info.TargetObjectId = Wire.TargetObjectId;
    Info.TargetSubObjId = Wire.TargetSubObjId;
    Info.Pad_0x0e = 0;
    Client->SendCommonSetTarget(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x05 FLPACKET_COMMON_CHATMSG
// Sender 0x06d8c470 (IChatClient+0x000, RET 0x10), receiver caseD_5 at 0x06d83900.
// The only channel-1 type that goes to the chat client rather than to IClient.
// ===========================================================================
static bool MarshalChatMsg(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadCommonChatMsg Wire;
    Wire.Field_0x00 = Args[0];
    Wire.Field_0x04 = Args[1];
    // Retail: `if (Buffer != 0 && Size != 0) { new(Size); memcpy } else { Size = 0; Buffer = 0 }`.
    Wire.Size = 0;
    Wire.Buffer = nullptr;
    const unsigned int Size = Args[2];
    void* Source = reinterpret_cast<void*>(Args[3]);
    if (Source != nullptr && Size != 0) {                   // 06d8c499 / 06d8c4a1
        Wire.Size = Size;                                   // 06d8c4a8
        Wire.Buffer = RpcCopyIntoPacketBlock(Source, Size); // 06d8c4aa, bare
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// caseD_5 0x06d83900: DAT_06dbbcd4 is read untested (83906) -- no null test here either.
// The first argument is a 4-byte struct built on the stack, word 1 then two zero bytes
// (83916..83925), i.e. the dword 1.
static bool DeliverChatMsg(const FPacketBase* Packet, IClient*, IChatClient* ChatClient) {
    const FPayloadCommonChatMsg Wire = RpcPayloadRead<FPayloadCommonChatMsg>(Packet, 0);
    // Retail pushes the payload's +0x04, +0x08 and +0x0c and a literal 1 -- payload+0x00
    // is written by the sender and then never read.
    ChatClient->SendCommonChatMsg(CHAT_ID(1u), CHAT_ID(Wire.Field_0x04), Wire.Size, Wire.Buffer);
    return true;
}

// ===========================================================================
// ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP -- sender 0x06d8bbb0, receiver case 8.
// Argument struct = XActivateEquip (В§3.2).
// ===========================================================================
static bool MarshalActivateEquip(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XActivateEquip* Info = reinterpret_cast<const XActivateEquip*>(Args[1]);
    FPayloadCommonActivateEquip Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.Pad_0x05 = 0;
    Wire.EquipId = Info->EquipId;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverActivateEquip(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonActivateEquip Wire = RpcPayloadRead<FPayloadCommonActivateEquip>(Packet, 0);
    XActivateEquip Info;
    Info.ObjectId = Wire.ObjectId;
    Info.EquipId = Wire.EquipId;
    Info.bActivate = (Wire.bActivate != 0);   // retail: `param_1[0x24] != 0`
    Info.Pad_0x07 = 0;
    Client->SendCommonActivateEquip(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE -- sender 0x06d8bc10, receiver case 0xe.
// Argument struct = XActivateCruise (В§3.3).
// ===========================================================================
static bool MarshalActivateCruise(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XActivateCruise* Info = reinterpret_cast<const XActivateCruise*>(Args[1]);
    FPayloadCommonActivateCruise Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.Field_0x05 = Info->Field_0x05;
    Wire.Pad_0x06[0] = 0;
    Wire.Pad_0x06[1] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverActivateCruise(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonActivateCruise Wire = RpcPayloadRead<FPayloadCommonActivateCruise>(Packet, 0);
    XActivateCruise Info;
    Info.ObjectId = Wire.ObjectId;
    Info.bActivate = (Wire.bActivate != 0);
    Info.Field_0x05 = (Wire.Field_0x05 != 0) ? 1u : 0u;   // retail: `param_1[0x25] != 0`
    Info.Pad_0x06[0] = 0;
    Info.Pad_0x06[1] = 0;
    Client->SendCommonActivateCruise(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE -- sender 0x06d8bcb0, receiver case 0xf.
// Argument struct = XGoTradelane (В§3.7).
// ===========================================================================
static bool MarshalGoTradelane(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XGoTradelane* Info = reinterpret_cast<const XGoTradelane*>(Args[1]);
    FPayloadCommonGoTradelane Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TradelaneRing1 = Info->TradelaneRing1;
    Wire.TradelaneRing2 = Info->TradelaneRing2;
    Wire.Field_0x0c = Info->Field_0x0c ? 1u : 0u;
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverGoTradelane(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonGoTradelane Wire = RpcPayloadRead<FPayloadCommonGoTradelane>(Packet, 0);
    XGoTradelane Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TradelaneRing1 = Wire.TradelaneRing1;
    Info.TradelaneRing2 = Wire.TradelaneRing2;
    Info.Field_0x0c = (Wire.Field_0x0c != 0);
    Info.Pad_0x0d[0] = 0;
    Info.Pad_0x0d[1] = 0;
    Info.Pad_0x0d[2] = 0;
    Client->SendCommonGoTradeLane(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x10 FLPACKET_COMMON_STOPTRADELANE -- sender 0x06d8bd10, receiver caseD_10.
// Four arguments in, one dword out: see the note on FPayloadCommonStopTradelane.
// ===========================================================================
static bool MarshalStopTradelane(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadCommonStopTradelane Wire;
    Wire.Field_0x00 = Args[1];
    Wire.Field_0x04 = 0;   // retail never writes these two
    Wire.Field_0x08 = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverStopTradelane(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonStopTradelane Wire = RpcPayloadRead<FPayloadCommonStopTradelane>(Packet, 0);
    Client->SendCommonStopTradeLane(1u, Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08);
    return true;
}

// ===========================================================================
// The buffer family. One marshaller pair, parameterised by whether retail copies and
// whether the packet takes ownership (payload+0x08, see FPayloadCommonBuffer).
// ===========================================================================
static bool MarshalBufferCommon(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount, bool bCopy, unsigned char bOwns) {
    FPayloadCommonBuffer Wire;
    Wire.Buffer = reinterpret_cast<void*>(Args[1]);
    Wire.Size = Args[2];
    Wire.bOwnsBuffer = bOwns;
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    if (bCopy) {
        Wire.Buffer = RpcCopyIntoPacketBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// op 0x11 FLPACKET_COMMON_SET_WEAPON_GROUP -- sender 0x06d8b4f0, receiver caseD_11.
// The one sender in the family that leaves payload+0x08 at zero.
static bool MarshalSetWeaponGroup(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 0);
}
static bool DeliverSetWeaponGroup(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonSetWeaponGroup(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x13 FLPACKET_COMMON_SET_VISITED_STATE -- sender 0x06d8b540, receiver caseD_13.
// Stores the caller's pointer and sets the flag (06d8b561 / 06d8b56c / 06d8b573): the
// packet takes the caller's operator-new block over. Same for 0x17 (06d8b7d3), 0x19
// (06d8b823), 0x1c (06d8b873) and 0x1a (06d8b8c3).
static bool MarshalSetVisitedState(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 1);
}
static bool DeliverSetVisitedState(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonSetVisitedState(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x16 FLPACKET_COMMON_REQUEST_BEST_PATH -- sender 0x06d8b590, receiver caseD_16.
// operator_new(Size) + REP MOVSD/MOVSB at 0x06d8b5b1..0x06d8b5d6: the packet owns the bytes.
static bool MarshalRequestBestPath(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, true, 1);
}
static bool DeliverRequestBestPath(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonRequestBestPath(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x17 FLPACKET_COMMON_REQUEST_GROUP_POSITIONS -- sender 0x06d8b7a0, receiver caseD_17.
static bool MarshalRequestGroupPositions(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 1);
}
static bool DeliverRequestGroupPositions(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonRequestGroupPositions(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x18 FLPACKET_COMMON_REQUEST_PLAYER_STATS -- sender 0x06d8b610, receiver caseD_18.
// Same operator_new + copy as 0x16.
static bool MarshalRequestPlayerStats(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, true, 1);
}
static bool DeliverRequestPlayerStats(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonRequestPlayerStats(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x19 FLPACKET_COMMON_SET_MISSION_LOG -- sender 0x06d8b7f0, receiver caseD_19.
static bool MarshalSetMissionLog(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 1);
}
static bool DeliverSetMissionLog(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonSetMissionLog(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x1a FLPACKET_COMMON_REQUEST_RANK_LEVEL -- sender 0x06d8b890, receiver caseD_1a.
static bool MarshalRequestRankLevel(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 1);
}
static bool DeliverRequestRankLevel(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonRequestRankLevel(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x1c FLPACKET_COMMON_SET_INTERFACE_STATE -- sender 0x06d8b840, receiver caseD_1c.
static bool MarshalSetInterfaceState(FPacketBase* P, const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P, A, N, false, 1);
}
static bool DeliverSetInterfaceState(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer Wire = RpcPayloadRead<FPayloadCommonBuffer>(Packet, 0);
    Client->SendCommonSetInterfaceState(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// ===========================================================================
// ch1 op 0x12 FLPACKET_COMMON_PLAYER_TRADE -- eight senders, one sub-switch.
//
// The proxy hands us Args[0] = the sub-type mask it is sending under, then the slot's own
// arguments -- see FLocalClientProxy::SendTradePacket. That mirrors retail, where each of
// the eight slots writes its own literal into payload+0x00.
// ===========================================================================
static bool MarshalPlayerTrade(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadCommonPlayerTrade Wire;
    Wire.SubType = Args[0];
    Wire.Field_0x04 = 0;
    Wire.Field_0x08 = 0;
    Wire.Field_0x0c = 0;
    Wire.Field_0x10 = 0;
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // The rest is what the packet constructor FUN_06d9bad0 leaves for the fields a sub-type
    // does not write: +0x14 = 0xffffffff (06d9bafa), +0x18 / +0x1c = 0 (06d9bb01 / 06d9bb04).
    // So sub-types 1/2/4/0x20/0x40/0x80 ship EquipArchId -1, not 0. The word at +0x20 is not
    // seeded at all (operator new residue in retail); zero here.
    Wire.EquipArchId = 0xffffffffu;
    Wire.EquipCount = 0;
    Wire.EquipStatus = 0.0f;
    Wire.EquipId = 0;
    Wire.Pad_0x22 = 0;

    // Args[1] is every sender's 2nd explicit argument, Args[2] the 3rd where there is one.
    // Both senders always hand over a zero-filled array of at least three
    // (SendTradePacket's Marshalled[4], RpcSendTradeToServer's Args[3]).
    const unsigned int A1 = Args[1];
    const unsigned int A2 = Args[2];

    switch (Wire.SubType) {
    case 0x01:   // 0x06d8b8e0 InitiateTrade-shaped
    case 0x20:   // 0x06d8bb10
    case 0x80:   // 0x06d8bb60
        Wire.Field_0x08 = A1;
        break;
    case 0x02:   // 0x06d8b980
        Wire.Field_0x04 = A1;
        break;
    case 0x04:   // 0x06d8b9d0
        Wire.Field_0x08 = A1;
        Wire.Field_0x0c = A2;
        break;
    case 0x40:   // 0x06d8b930
        Wire.Field_0x08 = A1;
        Wire.Field_0x10 = (unsigned char)(A2 & 0xffu);
        break;
    case 0x08:   // 0x06d8ba20 -- EquipDesc, status included
    case 0x10: { // 0x06d8baa0 -- EquipDesc, status NOT written
        // 06d8ba48 / 06d8ba52 / 06d8ba5c / 06d8ba71: direct loads off the caller's
        // EquipDesc (+0x04, +0x14, float +0x10, word +0x02); 0x06d8baa0 skips the float.
        Wire.Field_0x08 = A1;
        const EquipDesc* Desc = reinterpret_cast<const EquipDesc*>(A2);
        Wire.EquipArchId = Desc->ArchId;
        Wire.EquipCount = (unsigned int)Desc->Count;
        Wire.EquipId = Desc->Id;
        if (Wire.SubType == 0x08) {
            Wire.EquipStatus = Desc->Health;
        }
        break;
    }
    default:
        return false;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverPlayerTrade(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonPlayerTrade Wire = RpcPayloadRead<FPayloadCommonPlayerTrade>(Packet, 0);
    switch (Wire.SubType) {
    case 0x01:
        Client->SendCommonPlayerTradeInitiateTrade(1u, Wire.Field_0x08);
        return true;
    case 0x02:
        Client->SendCommonPlayerTradeTerminateTrade(1u, static_cast<int>(Wire.Field_0x04));
        return true;
    case 0x04:
        Client->SendCommonPlayerTradeSetTradeMoney(1u, Wire.Field_0x08, Wire.Field_0x0c);
        return true;
    case 0x08: {
        // caseD_8 0x06d837df: COMMON's EquipDesc::EquipDesc() (06d837e3), then
        // set_arch_id(+0x34) 06d837f1, set_count(+0x38) 06d837ff, set_status(+0x3c)
        // 06d8380d, set_id(word +0x40) 06d8381e, then IClient+0x1a4 (06d83837).
        EquipDesc Desc;
        Desc.set_arch_id(Wire.EquipArchId);
        Desc.set_count((int)Wire.EquipCount);
        Desc.set_status(Wire.EquipStatus);
        Desc.set_id(Wire.EquipId);
        Client->SendCommonPlayerTradeAddTradeEquip(1u, Wire.Field_0x08, Desc);
        return true;
    }
    case 0x10: {
        // caseD_10 0x06d83842: the same without set_status, so Health keeps the ctor's
        // default; set_arch_id 06d83854, set_count 06d83862, set_id 06d83873,
        // IClient+0x1a8 (06d8388c).
        EquipDesc Desc;
        Desc.set_arch_id(Wire.EquipArchId);
        Desc.set_count((int)Wire.EquipCount);
        Desc.set_id(Wire.EquipId);
        Client->SendCommonPlayerTradeDelTradeEquip(1u, Wire.Field_0x08, Desc);
        return true;
    }
    case 0x20:
        Client->SendCommonPlayerTradeRequestTrade(1u, Wire.Field_0x08);
        return true;
    case 0x40:
        // The byte goes out zero-extended; the sender only ever stored a bool's 0/1.
        Client->SendCommonPlayerTradeAcceptTrade(1u, Wire.Field_0x08, Wire.Field_0x10 != 0);
        return true;
    case 0x80:
        Client->SendCommonPlayerTradeStopTradeRequest(1u, Wire.Field_0x08);
        return true;
    default:
        // Retail's sub-switch has no default: anything else falls out and the packet dies.
        return false;
    }
}

// ===========================================================================
// ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO -- sender 0x06d8bdb0, receiver caseD_14.
// Argument struct = XJettisonCargo (В§3.8).
// ===========================================================================
static bool MarshalJettisonCargo(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XJettisonCargo* Info = reinterpret_cast<const XJettisonCargo*>(Args[1]);
    FPayloadCommonJettisonCargo Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.ItemId = Info->ItemId;
    Wire.Pad_0x06 = 0;
    Wire.Count = Info->Count;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverJettisonCargo(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonJettisonCargo Wire = RpcPayloadRead<FPayloadCommonJettisonCargo>(Packet, 0);
    XJettisonCargo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.ItemId = Wire.ItemId;
    Info.Pad_0x06 = 0;
    Info.Count = Wire.Count;
    Client->SendCommonJettisonCargo(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS -- sender 0x06d8bc60, receiver caseD_15.
// Argument struct = XActivateThrusters (В§3.4).
// ===========================================================================
static bool MarshalActivateThrusters(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XActivateThrusters* Info = reinterpret_cast<const XActivateThrusters*>(Args[1]);
    FPayloadCommonActivateThrusters Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.Pad_0x05[0] = 0;
    Wire.Pad_0x05[1] = 0;
    Wire.Pad_0x05[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverActivateThrusters(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonActivateThrusters Wire =
        RpcPayloadRead<FPayloadCommonActivateThrusters>(Packet, 0);
    XActivateThrusters Info;
    Info.ObjectId = Wire.ObjectId;
    Info.bActivate = (Wire.bActivate != 0);   // retail: SETNZ on packet+0x24
    Info.Pad_0x05[0] = 0;
    Info.Pad_0x05[1] = 0;
    Info.Pad_0x05[2] = 0;
    Client->SendCommonActivateThrusters(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- sender 0x06d8b690 (IClient+0x17c,
// RET 0x10), receiver caseD_1b at 0x06d835e3 -> IClient vtable +0x17c.
//
// THE GAP THAT USED TO BE HERE IS CLOSED. The old note said neither FmtStr export was
// reachable because rpclocal.vcxproj links only kernel32 and user32. That was a property
// of the port, not of retail: retail rpclocal's import table names COMMON.DLL, and four
// FmtStr entry points in it. The project now links Common.lib, as retail does, and this
// type flattens and unflattens for real. The long argument is at the head of the
// flattened-FmtStr family section in rpc_payloads.h.
//
// (The old note also had the size table backwards -- it said tag 0x4e costs 7 and
// everything else 0x17. The loop at 0x06d8b6d0 does the opposite: `CMP EAX,0x4e ; SETZ CL
// ; DEC ECX ; AND ECX,0xfffffff0 ; ADD ECX,0x17` gives 0x17 for 0x4e and 7 for the rest.)
//
// The payload copies now, so the type is self-contained and queues like the rest.
// Arguments: Args[0] session id (never serialized), Args[1]/Args[2] FmtStr*, Args[3] the
// dword at packet+0x30 (06d8b780).
// ===========================================================================
static bool MarshalPopUpDialog(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FmtStr* SourceA = reinterpret_cast<const FmtStr*>(Args[1]);
    const FmtStr* SourceB = reinterpret_cast<const FmtStr*>(Args[2]);
    FPayloadCommonPopUpDialog Wire;
    Wire.SizeA = 0;
    Wire.SizeB = 0;
    Wire.BufferA = RpcFlattenFmtStr(*SourceA, Wire.SizeA);   // 06d8b6ff..06d8b719
    Wire.BufferB = RpcFlattenFmtStr(*SourceB, Wire.SizeB);   // 06d8b760..06d8b776
    Wire.Field_0x10 = Args[3];                               // 06d8b780
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverPopUpDialog(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonPopUpDialog Wire = RpcPayloadRead<FPayloadCommonPopUpDialog>(Packet, 0);
    FmtStr TextA;
    FmtStr TextB;
    RpcUnflattenFmtStr(TextA, Wire.BufferA, Wire.SizeA);     // 06d8368a
    RpcUnflattenFmtStr(TextB, Wire.BufferB, Wire.SizeB);     // 06d83698
    // 06d836a5..06d836b2: PUSH packet+0x30, PUSH &B, PUSH &A, PUSH 1.
    Client->SendCommonPopUpDialog(1u, TextA, TextB, Wire.Field_0x10);
    return true;
}

// ===========================================================================
// ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS -- sender 0x06d8be10, receiver caseD_1d.
// Argument struct = XTractorObjects (В§3.6).
// ===========================================================================
static bool MarshalTractorObjects(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const XTractorObjects* Info = reinterpret_cast<const XTractorObjects*>(Args[1]);
    FPayloadCommonTractorObjects Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TractorEquipId = Info->TractorEquipId;
    Wire.Pad_0x06 = 0;
    Wire.TargetIds.AllocPad = 0;
    Wire.TargetIds.Begin = nullptr;
    Wire.TargetIds.End = nullptr;
    Wire.TargetIds.Cap = nullptr;

    const unsigned int Count = static_cast<unsigned int>(Info->TargetIds.size());
    if (Count != 0) {
        // The inlined vector operator=: _Allocate(size), a bare operator new.
        unsigned int* Block = static_cast<unsigned int*>(
            RpcCopyIntoPacketBlock(Info->TargetIds.data(), Count * 4u));
        Wire.TargetIds.Begin = Block;
        Wire.TargetIds.End = Block + Count;
        Wire.TargetIds.Cap = Block + Count;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// caseD_1d 0x06d8326a: a local XTractorObjects at [ESP+0x18] -- the vector at +0x08 is
// zeroed (83270..8327c), +0x00 = payload+0x00 and the word +0x04 = payload+0x04
// (83280..8329b), then the inlined vector<uint> operator= from payload+0x08
// (832a0..83396): an empty source leaves it empty, otherwise one operator new of Count*4
// bytes holds a COPY (83378). IClient slot 5 (833ad) gets (1, &local); FUN_06d851d0, the
// vector destructor, frees the copy (833bf).
static bool DeliverTractorObjects(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadCommonTractorObjects Wire =
        RpcPayloadRead<FPayloadCommonTractorObjects>(Packet, 0);
    XTractorObjects Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TractorEquipId = Wire.TractorEquipId;
    Info.Pad_0x06 = 0;
    const unsigned int Count = Wire.TargetIds.Count();
    if (Count != 0) {
        Info.TargetIds.assign(Wire.TargetIds.Begin, Wire.TargetIds.End);
    }
    Client->SendCommonTractorObjects(1u, Info);
    return true;
}

// ===========================================================================
// The table. Order follows the opcode, not the retail registrator's order.
// ===========================================================================
const FRpcPayloadCodec GChannel1Codecs[] = {
    { 1, 0x01, "FLPACKET_COMMON_UPDATEOBJECT",             0x2c,
      MarshalUpdateObject,           DeliverUpdateObject },
    { 1, 0x02, "FLPACKET_COMMON_FIREWEAPON",               0x20,
      MarshalFireWeapon,             DeliverFireWeapon },
    { 1, 0x04, "FLPACKET_COMMON_SETTARGET",                0x0c,
      MarshalSetTarget,              DeliverSetTarget },
    { 1, 0x05, "FLPACKET_COMMON_CHATMSG",                  0x10,
      MarshalChatMsg,                DeliverChatMsg },
    { 1, 0x08, "FLPACKET_COMMON_ACTIVATEEQUIP",            0x08,
      MarshalActivateEquip,          DeliverActivateEquip },
    { 1, 0x0e, "FLPACKET_COMMON_ACTIVATECRUISE",           0x08,
      MarshalActivateCruise,         DeliverActivateCruise },
    { 1, 0x0f, "FLPACKET_COMMON_GOTRADELANE",              0x10,
      MarshalGoTradelane,            DeliverGoTradelane },
    { 1, 0x10, "FLPACKET_COMMON_STOPTRADELANE",            0x0c,
      MarshalStopTradelane,          DeliverStopTradelane },
    { 1, 0x11, "FLPACKET_COMMON_SET_WEAPON_GROUP",         0x0c,
      MarshalSetWeaponGroup,         DeliverSetWeaponGroup },
    { 1, 0x12, "FLPACKET_COMMON_PLAYER_TRADE",             0x24,
      MarshalPlayerTrade,            DeliverPlayerTrade },
    { 1, 0x13, "FLPACKET_COMMON_SET_VISITED_STATE",        0x0c,
      MarshalSetVisitedState,        DeliverSetVisitedState },
    { 1, 0x14, "FLPACKET_COMMON_JETTISONCARGO",            0x0c,
      MarshalJettisonCargo,          DeliverJettisonCargo },
    { 1, 0x15, "FLPACKET_COMMON_ACTIVATETHRUSTERS",        0x08,
      MarshalActivateThrusters,      DeliverActivateThrusters },
    { 1, 0x16, "FLPACKET_COMMON_REQUEST_BEST_PATH",        0x0c,
      MarshalRequestBestPath,        DeliverRequestBestPath },
    { 1, 0x17, "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS",  0x0c,
      MarshalRequestGroupPositions,  DeliverRequestGroupPositions },
    { 1, 0x18, "FLPACKET_COMMON_REQUEST_PLAYER_STATS",     0x0c,
      MarshalRequestPlayerStats,     DeliverRequestPlayerStats },
    { 1, 0x19, "FLPACKET_COMMON_SET_MISSION_LOG",          0x0c,
      MarshalSetMissionLog,          DeliverSetMissionLog },
    { 1, 0x1a, "FLPACKET_COMMON_REQUEST_RANK_LEVEL",       0x0c,
      MarshalRequestRankLevel,       DeliverRequestRankLevel },
    { 1, 0x1b, "FLPACKET_COMMON_POP_UP_DIALOG",            0x14,
      MarshalPopUpDialog,            DeliverPopUpDialog },
    { 1, 0x1c, "FLPACKET_COMMON_SET_INTERFACE_STATE",      0x0c,
      MarshalSetInterfaceState,      DeliverSetInterfaceState },
    { 1, 0x1d, "FLPACKET_COMMON_TRACTOROBJECTS",           0x18,
      MarshalTractorObjects,         DeliverTractorObjects },
};

const unsigned int GChannel1CodecCount =
    (unsigned int)(sizeof(GChannel1Codecs) / sizeof(GChannel1Codecs[0]));

extern "C" __declspec(dllexport) const FRpcPayloadCodec* RpcFindPayloadCodec(unsigned int Channel, unsigned int Opcode) {
    if (Channel == 1u) {
        for (unsigned int i = 0; i < GChannel1CodecCount; ++i) {
            if (GChannel1Codecs[i].Opcode == (unsigned short)Opcode) {
                return &GChannel1Codecs[i];
            }
        }
        return nullptr;
    }
    if (Channel == 2u) {
        // Recovered in src/rpclocal/rpc_payload_ch2.cpp; the types still missing there
        // fall through to the raw-argument-dword blit, as before.
        for (unsigned int i = 0; i < GChannel2CodecCount; ++i) {
            if (GChannel2Codecs[i].Opcode == (unsigned short)Opcode) {
                return &GChannel2Codecs[i];
            }
        }
        return nullptr;
    }
    // Channel 3 (client -> server) keeps its codecs in a table of its own, because its
    // deliver side calls IServer rather than IClient: see RpcFindServerCodec in
    // src/rpclocal/rpc_payload_ch3.cpp. Callers that need a channel-3 codec ask that
    // function; this one answers only for the two IClient-facing channels.
    return nullptr;
}
