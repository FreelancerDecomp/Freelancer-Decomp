// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <windows.h>
#include "ClientImpl.h"
#include "../include/common_server_types.h"
#include "common_text.h"
#include "common_goods.h"
#include "rpc_iclient_args.h"
#include <windows.h>

#include "../include/rpc_packet.h"
#include "dalib_net.h"
#include "rpc_packet_codec.h"

CClientImpl Client;

CClientImpl::CClientImpl()
    : Serializer(nullptr), Host(nullptr), ClientProxies{}, NumClients(0),
      Field_0x214(0), bStarted(false) {}
CClientImpl::~CClientImpl() {}

IClient::IClient() {}
IClient::IClient(const IClient& Other) {}
IClient& IClient::operator=(const IClient& Other) { return *this; }


void CClientImpl::SendCommonFireWeapon(unsigned int ClientId, const XFireWeaponInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 2, &Packet) == 0)
    {
        FRpcFireWeaponPacket* TypedPacket = static_cast<FRpcFireWeaponPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetPos = Info.TargetPos;
        TypedPacket->Hardpoints = Info.Hardpoints;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateEquip(unsigned int ClientId, const XActivateEquip& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 8, &Packet) == 0)
    {
        FRpcActivateEquipPacket* TypedPacket = static_cast<FRpcActivateEquipPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->EquipId = Info.EquipId;
        TypedPacket->bActive = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateCruise(unsigned int ClientId, const XActivateCruise& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x0e, &Packet) == 0)
    {
        FRpcActivateCruisePacket* TypedPacket = static_cast<FRpcActivateCruisePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        TypedPacket->Field_0x05 = Info.Field_0x05;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateThrusters(unsigned int ClientId, const XActivateThrusters& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x15, &Packet) == 0)
    {
        FRpcActivateThrustersPacket* TypedPacket = static_cast<FRpcActivateThrustersPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetTarget(unsigned int ClientId, const XSetTarget& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 4, &Packet) == 0)
    {
        FRpcSetTargetPacket* TypedPacket = static_cast<FRpcSetTargetPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetId = Info.TargetObjectId;
        TypedPacket->SubObjId = Info.SubObjId;
        TypedPacket->TargetSubObjId = Info.TargetSubObjId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonTractorObjects(unsigned int ClientId, const XTractorObjects& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1d, &Packet) == 0)
    {
        FRpcTractorObjectsPacket* TypedPacket = static_cast<FRpcTractorObjectsPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TractorEquipId = Info.TractorEquipId;
        TypedPacket->TargetIds = Info.TargetIds;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonGoTradeLane(unsigned int ClientId, const XGoTradelane& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x0f, &Packet) == 0)
    {
        FRpcGoTradeLanePacket* TypedPacket = static_cast<FRpcGoTradeLanePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TradelaneRing1 = Info.TradelaneRing1;
        TypedPacket->TradelaneRing2 = Info.TradelaneRing2;
        TypedPacket->Field_0x0c = Info.Field_0x0c;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonStopTradeLane(unsigned int ClientId, unsigned int ShipId,
                                          unsigned int TradelaneRing1, unsigned int TradelaneRing2) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x10, &Packet) == 0)
    {
        // Retail ignores the last two arguments; the constructor zeroes both rings.
        static_cast<FRpcStopTradeLanePacket*>(Packet)->ShipId = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonJettisonCargo(unsigned int ClientId, const XJettisonCargo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x14, &Packet) == 0)
    {
        FRpcJettisonCargoPacket* TypedPacket = static_cast<FRpcJettisonCargoPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->ItemId = Info.ItemId;
        TypedPacket->Count = Info.Count;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::OnConnect(unsigned int ClientId) {
}

void CClientImpl::SendCommonUpdateObject(unsigned int ClientId, const FCommonUpdateObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 1, &Packet) == 0)
    {
        FRpcUpdateObjectPacket* TypedPacket = static_cast<FRpcUpdateObjectPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x28;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Position = Info.Position;
        TypedPacket->Orientation = Info.Orientation;
        memcpy(&TypedPacket->UpdateTime, &Info.Field_0x20, sizeof(float));
        memcpy(&TypedPacket->Field_0x28, &Info.Field_0x24, sizeof(float));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetWeaponGroup(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x11, &Packet) == 0)
    {
        FRpcSetWeaponGroupPacket* TypedPacket = static_cast<FRpcSetWeaponGroupPacket*>(Packet);
        // BorrowData keeps the caller's block with the ownership byte 0 (never writes or
        // frees it); its parameter is non-const only because the packet field is.
        TypedPacket->BorrowData(const_cast<unsigned char*>(Data), Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetVisitedState(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x13, &Packet) == 0)
    {
        FRpcSetVisitedStatePacket* TypedPacket = static_cast<FRpcSetVisitedStatePacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestBestPath(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x16, &Packet) == 0)
    {
        FRpcRequestBestPathPacket* TypedPacket = static_cast<FRpcRequestBestPathPacket*>(Packet);
        TypedPacket->CopyData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestPlayerStats(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x18, &Packet) == 0)
    {
        FRpcRequestPlayerStatsPacket* TypedPacket = static_cast<FRpcRequestPlayerStatsPacket*>(Packet);
        TypedPacket->CopyData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPopUpDialog(unsigned int ClientId, const FmtStr& Title, const FmtStr& Text,
                                        unsigned int Buttons) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1b, &Packet) == 0)
    {
        FRpcPopUpDialogPacket* TypedPacket = static_cast<FRpcPopUpDialogPacket*>(Packet);
        TypedPacket->SizeA = RpcGetFlattenedFmtStrSize(Title);
        TypedPacket->DataA = static_cast<unsigned char*>(::operator new(TypedPacket->SizeA, std::nothrow));
        Title.flatten(TypedPacket->DataA, TypedPacket->SizeA);
        TypedPacket->SizeB = RpcGetFlattenedFmtStrSize(Text);
        TypedPacket->DataB = static_cast<unsigned char*>(::operator new(TypedPacket->SizeB, std::nothrow));
        Text.flatten(TypedPacket->DataB, TypedPacket->SizeB);
        TypedPacket->Field_0x10 = Buttons;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestGroupPositions(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x17, &Packet) == 0)
    {
        FRpcRequestGroupPositionsPacket* TypedPacket = static_cast<FRpcRequestGroupPositionsPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetMissionLog(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x19, &Packet) == 0)
    {
        FRpcSetMissionLogPacket* TypedPacket = static_cast<FRpcSetMissionLogPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetInterfaceState(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1c, &Packet) == 0)
    {
        FRpcSetInterfaceStatePacket* TypedPacket = static_cast<FRpcSetInterfaceStatePacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestRankLevel(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1a, &Packet) == 0)
    {
        FRpcRequestRankLevelPacket* TypedPacket = static_cast<FRpcRequestRankLevelPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeInitiateTrade(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x1;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeTerminateTrade(unsigned int ClientId, int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x2;
        TypedPacket->Reason = Reason;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeAcceptTrade(unsigned int ClientId, unsigned int ShipId, bool bAccepted) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x40;
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->bAccepted = static_cast<unsigned char>(bAccepted);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeSetTradeMoney(unsigned int ClientId, unsigned int ShipId, unsigned long Money) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x4;
        TypedPacket->Money = static_cast<int>(Money);
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeAddTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x8;
        TypedPacket->EquipArchId = Item.ArchId;
        TypedPacket->EquipCount = Item.Count;
        const float* SourceHealth = &Item.Health;
        float* DestinationHealth = &TypedPacket->EquipHealth;
        PortableFloatLoad::Copy(DestinationHealth, SourceHealth);
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->EquipId = Item.Id;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeDelTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x10;
        TypedPacket->EquipArchId = Item.ArchId;
        TypedPacket->EquipCount = Item.Count;
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->EquipId = Item.Id;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeRequestTrade(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x20;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeStopTradeRequest(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x80;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::DumpPacketStats(const char* Name) {
    RpcGetRemotePacketSerializer()->DumpStats(Name);
}

void CClientImpl::SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 5, &Packet) == 0)
    {
        FRpcChatMsgPacket* TypedPacket = static_cast<FRpcChatMsgPacket*>(Packet);
        if (Data != nullptr && Size != 0)
        {
            TypedPacket->DataSize = Size;
            TypedPacket->Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
            memcpy(TypedPacket->Data, Data, Size);
        }
        else
        {
            TypedPacket->DataSize = 0;
            TypedPacket->Data = nullptr;
        }
        TypedPacket->TargetId = TargetId.Id;
        TypedPacket->Field_0x04 = SenderId.Id;
        // The client id is the CHAT_ID's low word (06d8c4ed MOVZX).
        PushToClient(static_cast<unsigned short>(TargetId.Id), Packet->AsRaw());
    }
}

