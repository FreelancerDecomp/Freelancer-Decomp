// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#define RPCLOCAL_EXPORTS
#include "portable_rpc_item_fields.h"
#include <windows.h>
#include "../include/rpc_iserver.h"
#define GetChatClientInterface __NetworkGetChatClientInterface
#define GetChatServerInterface __NetworkGetChatServerInterface
#include "../include/network.h"
#undef GetChatClientInterface
#undef GetChatServerInterface
#include "../include/rpc_packet.h"
#include "../include/rpc_packet_codec.h"
#include "../include/rpc_server_channel_codec.h"
#include "../include/common_goods.h"
#include <string.h>
#include <cwchar>
#include <new>

namespace
{
    FPacketSerializer* GRemotePacketSerializer = nullptr;
}

// 06be12a0: access alone does not register any packet types or add a reference.
FPacketSerializer* RpcGetRemotePacketSerializer()
{
    if (GRemotePacketSerializer == nullptr) {
        GRemotePacketSerializer = new (std::nothrow) FPacketSerializer;
    }
    return GRemotePacketSerializer;
}

// Retail remoteserver.dll ??0IServerImpl@@QAE@XZ (.text:06bd11b0): the two vptrs and
// `MOV dword ptr [EAX + 0x8],0x0` (06bd11b9) -- the serializer starts out null.
IServerImpl::IServerImpl() : Serializer(nullptr) {}
IServerImpl::IServerImpl(const IServerImpl& Other) { Serializer = Other.Serializer; }
IServerImpl& IServerImpl::operator=(const IServerImpl& Other) { Serializer = Other.Serializer; return *this; }
void IServerImpl::GetServerStats(ServerStats& Stats) {}
void IServerImpl::HandlePacket(CDAPacket* Packet) {
    extern void HandlePacket(unsigned int, CDAPacket*);
    HandlePacket(1, Packet);
    reinterpret_cast<FPacketBase*>(Packet)->Release();
}

FRpcRefObject::~FRpcRefObject() {}
unsigned int FRpcRefObject::Release() {
    if (UsageCount != 0 && --UsageCount == 0) {
        delete this;
        return 0;
    }
    return UsageCount;
}
FPacketSerializer::FPacketSerializer()
{
    // 06be1357 is inside the construction EH region: dictionary exceptions
    // unwind the map/base and the new-expression frees the unpublished storage.
    RpcServerBuildHardpointDictionary();
}
FPacketSerializer::~FPacketSerializer() {
    for (std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.begin(); It != Types.end(); ++It) {
        delete It->second;
    }
}

void RpcDispatchMsgs(IServerImpl* Server, const unsigned char* Data, unsigned int Size) {
    const unsigned char* Cursor = Data;
    unsigned int Remaining = Size;
    
    while (Remaining >= 2) {
        unsigned short PacketId = *(const unsigned short*)Cursor;
        unsigned int Channel = PacketId >> 8;
        unsigned int Opcode = PacketId & 0xff;
        
        Cursor += 2;
        Remaining -= 2;
        
        FPacketBase* Packet = nullptr;
        if (Server->Serializer && Server->Serializer->Allocate(Channel, Opcode, &Packet) == 0) {
            unsigned int BytesRead = 0;
            if (Packet->Decode(Cursor, Remaining, &BytesRead) == 0) {
                Cursor += BytesRead;
                Remaining -= BytesRead;
                Server->HandlePacket(Packet->AsRaw());
            }
        }
    }
}


// Retail remoteserver.dll FUN_06be1750 -- the same body as rpclocal's FUN_06d96300
// (src/rpclocal/rpc_transport.cpp):
//   06be1755 TEST out / 06be175d MOV EAX,0x4 ; RET 0xc      -- null out pointer
//   06be1766..06be1774  id = (MOVZX Channel word << 16) | MOVZX Opcode word
//   06be1784..06be17b1  lower_bound + key compare = find; the head node -> 0x19
//   06be17b3 MOV EAX,[node+0x10] / 06be17b6 PUSH out / 06be17b7 CALL [EAX]
// The old stub here returned 0 without producing a packet, so every caller wrote
// through a null CDAPacket*. The base constructor leaves Serializer null; the
// concrete network Server's Startup creates it and installs the packet registries.
int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket)
{
    if (OutPacket == nullptr) {
        return kPacketAllocateNullOut;                  // 06be175d MOV EAX,0x4
    }
    const unsigned int PacketId = ((Channel & 0xffffu) << 16) | (Opcode & 0xffffu);
    std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.find(PacketId);
    if (It == Types.end()) {
        return kPacketAllocateUnknownType;              // 06be17c2 MOV EAX,0x19
    }
    return It->second->Factory(OutPacket);              // 06be17b7 CALL dword ptr [EAX]
}

// 06bd8140..06bd8312: object/position stores, deep vector<ushort> assignment,
// then virtual PushToServer. The packet owns its copy of all hardpoint IDs.
void IServerImpl::FireWeapon(unsigned int PlayerId, const XFireWeaponInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x02, &Packet) == 0) {
        FRpcFireWeaponPacket* TypedPacket = static_cast<FRpcFireWeaponPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetPos = Info.TargetPos;
        TypedPacket->Hardpoints = Info.Hardpoints;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ActivateEquip(unsigned int PlayerId, const XActivateEquip& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x08, &Packet) == 0) {
        FRpcActivateEquipPacket* TypedPacket = static_cast<FRpcActivateEquipPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->EquipId = Info.EquipId;
        TypedPacket->bActive = Info.bActivate;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ActivateCruise(unsigned int PlayerId, const XActivateCruise& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x0e, &Packet) == 0) {
        FRpcActivateCruisePacket* TypedPacket = static_cast<FRpcActivateCruisePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        TypedPacket->Field_0x05 = Info.Field_0x05;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ActivateThrusters(unsigned int PlayerId, const XActivateThrusters& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x15, &Packet) == 0) {
        FRpcActivateThrustersPacket* TypedPacket = static_cast<FRpcActivateThrustersPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetTarget(unsigned int PlayerId, const XSetTarget& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x04, &Packet) == 0) {
        FRpcSetTargetPacket* TypedPacket = static_cast<FRpcSetTargetPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetId = Info.TargetObjectId;
        TypedPacket->SubObjId = Info.SubObjId;
        TypedPacket->TargetSubObjId = Info.TargetSubObjId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::TractorObjects(unsigned int PlayerId, const XTractorObjects& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1d, &Packet) == 0) {
        FRpcTractorObjectsPacket* TypedPacket = static_cast<FRpcTractorObjectsPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TractorEquipId = Info.TractorEquipId;
        TypedPacket->TargetIds = Info.TargetIds;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GoTradelane(unsigned int PlayerId, const XGoTradelane& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x0f, &Packet) == 0) {
        FRpcGoTradeLanePacket* TypedPacket = static_cast<FRpcGoTradeLanePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TradelaneRing1 = Info.TradelaneRing1;
        TypedPacket->TradelaneRing2 = Info.TradelaneRing2;
        TypedPacket->Field_0x0c = Info.Field_0x0c;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::StopTradelane(unsigned int SessionId, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x10, &Packet) == 0) {
        FRpcStopTradeLanePacket* TypedPacket = static_cast<FRpcStopTradeLanePacket*>(Packet);
        TypedPacket->ShipId = Arg1;
        TypedPacket->TradeLaneRing1 = Arg2;
        TypedPacket->TradeLaneRing2 = Arg3;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::JettisonCargo(unsigned int PlayerId, const XJettisonCargo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x14, &Packet) == 0) {
        FRpcJettisonCargoPacket* TypedPacket = static_cast<FRpcJettisonCargoPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->ItemId = Info.ItemId;
        TypedPacket->Count = Info.Count;
        this->PushToServer(Packet->AsRaw());
    }
}

bool IServerImpl::Startup(const SStartupInfo& Info) {
    return false;
}

void IServerImpl::Shutdown() {
}

int IServerImpl::Update() {
    return 1;
}

void IServerImpl::ElapseTime(float Dt) {
}

void IServerImpl::SetActiveConnection(EFLConnection Conn) {
}

// 06bd9df0: the exported base answers true without reading either connection.
bool IServerImpl::SwapConnections(EFLConnection ConnA, EFLConnection ConnB) {
    return true;
}

void IServerImpl::Connect(const char* ServerName, unsigned short* Port) {
}

void IServerImpl::DisConnect(unsigned int SessionId, EFLConnection Type) {
}

void IServerImpl::OnConnect(unsigned int SessionId) {
}

void IServerImpl::Login(const SLoginInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, 0x01, &Packet) == 0) {
        FRpcClientLoginPacket* TypedPacket = static_cast<FRpcClientLoginPacket*>(Packet);
        std::wcscpy(TypedPacket->AccountName, Info.AccountName);
        std::wcscpy(TypedPacket->Field_0x220, Info.Field_0xA0);
        TypedPacket->CreateFlag = static_cast<unsigned char>(Info.CreateFlag);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CharacterInfoReq(unsigned int SessionId, bool bFullInfo) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTCHARINFO, &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CharacterSelect(const CHARACTER_ID& CharId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_SELECTCHARACTER, &Packet) == 0) {
        FRpcSelectCharacterPacket* TypedPacket = static_cast<FRpcSelectCharacterPacket*>(Packet);
        TypedPacket->CharacterId = CharId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::NewCharacterInfoReq(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTNEWCHARINFO, &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_CREATENEWCHAR, &Packet) == 0) {
        wchar_t Name[64];
        std::wcsncpy(Name, Info.Name, 64);
        const unsigned int Field_0x80 = Info.Field_0x30;
        const unsigned int Field_0x84 = Info.Field_0x34;
        const unsigned int Field_0x88 = Info.Field_0x38;
        const unsigned int Field_0x8c = Info.Field_0x3c;
        Name[63] = L'\0';
        FRpcCreateNewCharacterPacket* TypedPacket = static_cast<FRpcCreateNewCharacterPacket*>(Packet);
        memcpy(TypedPacket->Name, Name, sizeof(Name));
        TypedPacket->Field_0x80 = Field_0x80;
        TypedPacket->Field_0x84 = Field_0x84;
        TypedPacket->Field_0x88 = Field_0x88;
        TypedPacket->Field_0x8c = Field_0x8c;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::DestroyCharacter(const CHARACTER_ID& CharId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_DESTROYCHAR, &Packet) == 0) {
        FRpcDestroyCharacterPacket* TypedPacket = static_cast<FRpcDestroyCharacterPacket*>(Packet);
        TypedPacket->CharacterId = CharId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CharacterSkipAutosave(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_SKIP_AUTOSAVE, &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqShipArch(unsigned int ArchId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQSHIPARCH, &Packet) == 0) {
        FRpcRequestShipArchPacket* TypedPacket = static_cast<FRpcRequestShipArchPacket*>(Packet);
        TypedPacket->Value = ArchId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqHullStatus(float HullStatus, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQHULLSTATUS, &Packet) == 0) {
        FRpcRequestHullStatusPacket* TypedPacket = static_cast<FRpcRequestHullStatusPacket*>(Packet);
        float* Destination = &TypedPacket->Value;
        PortableFloatLoad::Copy(Destination, &HullStatus);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& Groups, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQCOLLISIONGROUPS, &Packet) == 0) {
        FRpcRequestCollisionGroupsPacket* TypedPacket = static_cast<FRpcRequestCollisionGroupsPacket*>(Packet);
        TypedPacket->Groups = Groups;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqEquipment(const EquipDescList& Equipment, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQEQUIPMENT, &Packet) == 0) {
        FRpcServerEquipHandle Temporary;
        const std::list<EquipDesc>::const_iterator End = Equipment.Items.end();
        for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != End; ++It) {
            const EquipDesc& Desc = *It;
            FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (Record != nullptr) {
                Record->ArchId = 0;
                Record->Count = 0;
                std::memset(&Record->Health, 0, 4u);
                Record->Id = 0;
                Record->bEquipped = 0;
                Record->Field_0x0f = 0;
                Record->HardpointSize = 0;
                Record->Hardpoint = nullptr;
            }
            Record->ArchId = Desc.ArchId;
            Record->Count = Desc.Count;
            std::memcpy(&Record->Health, &Desc.Health, 4u);
            Record->Id = Desc.Id;
            Record->bEquipped = Desc.bEquipped;
            if (!Desc.is_internal()) {
                const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc.Hardpoint)) + 1u;
                Record->HardpointSize = static_cast<unsigned short>(Length);
                Record->Hardpoint = static_cast<char*>(
                    ::operator new(Record->HardpointSize, std::nothrow));
                std::strcpy(Record->Hardpoint, Desc.Hardpoint);
            }
            Temporary.PushBack(Record);
        }
        FRpcRequestEquipmentPacket* TypedPacket = static_cast<FRpcRequestEquipmentPacket*>(Packet);
        TypedPacket->AssignEquipment(Temporary);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqCargo(const EquipDescList& Cargo, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQCARGO, &Packet) == 0) {
        FRpcServerEquipHandle Temporary;
        const std::list<EquipDesc>::const_iterator End = Cargo.Items.end();
        for (std::list<EquipDesc>::const_iterator It = Cargo.Items.begin(); It != End; ++It) {
            const EquipDesc& Desc = *It;
            FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (Record != nullptr) {
                Record->ArchId = 0;
                Record->Count = 0;
                std::memset(&Record->Health, 0, 4u);
                Record->Id = 0;
                Record->bEquipped = 0;
                Record->Field_0x0f = 0;
                Record->HardpointSize = 0;
                Record->Hardpoint = nullptr;
            }
            Record->ArchId = Desc.ArchId;
            Record->Count = Desc.Count;
            std::memcpy(&Record->Health, &Desc.Health, 4u);
            Record->Id = Desc.Id;
            Record->bEquipped = Desc.bEquipped;
            if (!Desc.is_internal()) {
                const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc.Hardpoint)) + 1u;
                Record->HardpointSize = static_cast<unsigned short>(Length);
                Record->Hardpoint = static_cast<char*>(
                    ::operator new(Record->HardpointSize, std::nothrow));
                std::strcpy(Record->Hardpoint, Desc.Hardpoint);
            }
            Temporary.PushBack(Record);
        }
        FRpcRequestCargoPacket* TypedPacket = static_cast<FRpcRequestCargoPacket*>(Packet);
        TypedPacket->AssignEquipment(Temporary);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqAddItem(unsigned int ArchId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQADDITEM, &Packet) == 0) {
        FRpcRequestAddItemPacket* TypedPacket = static_cast<FRpcRequestAddItemPacket*>(Packet);
        unsigned int* ArchDestination = &TypedPacket->ArchId;
        int* CountDestination = &TypedPacket->Count;
        float* HealthDestination = &TypedPacket->Health;
        unsigned char Mounted;
        // 06bd75bd..75db: FLD precedes both integer stores; FSTP follows them.
        PortableRpcItemFields::Store(ArchDestination, CountDestination, HealthDestination,
                                     &Mounted, ArchId, Count, &Health, bMounted);
        TypedPacket->bMounted = Mounted;
        TypedPacket->CopyHardpoint(Hardpoint, static_cast<unsigned int>(strlen(Hardpoint) + 1u));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqRemoveItem(unsigned short EquipId, int Count, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQREMOVEITEM, &Packet) == 0) {
        FRpcRequestRemoveItemPacket* TypedPacket = static_cast<FRpcRequestRemoveItemPacket*>(Packet);
        TypedPacket->EquipId = EquipId;
        TypedPacket->Count = Count;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqModifyItem(unsigned short EquipId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQMODIFYITEM, &Packet) == 0) {
        FRpcRequestModifyItemPacket* TypedPacket = static_cast<FRpcRequestModifyItemPacket*>(Packet);
        unsigned int* EquipDestination = &TypedPacket->EquipId;
        int* CountDestination = &TypedPacket->Count;
        float* HealthDestination = &TypedPacket->Health;
        unsigned char Mounted;
        // 06bd768d..76ad: retain MOVZX and the stores between FLD and FSTP.
        __asm {
            fld Health
            movzx eax, EquipId
            mov edx, EquipDestination
            mov dword ptr [edx], eax
            mov eax, Count
            mov edx, CountDestination
            mov dword ptr [edx], eax
            mov al, byte ptr bMounted
            mov Mounted, al
            mov edx, HealthDestination
            fstp dword ptr [edx]
        }
        TypedPacket->bMounted = Mounted;
        TypedPacket->CopyHardpoint(Hardpoint, static_cast<unsigned int>(strlen(Hardpoint) + 1u));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqSetCash(int Cash, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQSETCASH, &Packet) == 0) {
        FRpcRequestSetCashPacket* TypedPacket = static_cast<FRpcRequestSetCashPacket*>(Packet);
        TypedPacket->Value = Cash;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqChangeCash(int Delta, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQCHANGECASH, &Packet) == 0) {
        FRpcRequestChangeCashPacket* TypedPacket = static_cast<FRpcRequestChangeCashPacket*>(Packet);
        TypedPacket->Value = Delta;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseEnter(unsigned int BaseId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_ENTERBASE, &Packet) == 0) {
        FRpcEnterBasePacket* TypedPacket = static_cast<FRpcEnterBasePacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseExit(unsigned int BaseId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_EXITBASE, &Packet) == 0) {
        FRpcExitBasePacket* TypedPacket = static_cast<FRpcExitBasePacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LocationEnter(unsigned int LocationId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_ENTERLOCATION, &Packet) == 0) {
        FRpcEnterLocationPacket* TypedPacket = static_cast<FRpcEnterLocationPacket*>(Packet);
        TypedPacket->Value = LocationId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LocationExit(unsigned int LocationId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_EXITLOCATION, &Packet) == 0) {
        FRpcExitLocationPacket* TypedPacket = static_cast<FRpcExitLocationPacket*>(Packet);
        TypedPacket->Value = LocationId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseInfoRequest(unsigned int BaseId, unsigned int SessionId, bool bFull) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTBASEINFO, &Packet) == 0) {
        FRpcRequestBaseInfoPacket* TypedPacket = static_cast<FRpcRequestBaseInfoPacket*>(Packet);
        TypedPacket->Id = BaseId;
        memcpy(&TypedPacket->bFull, &bFull, 1u);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LocationInfoRequest(unsigned int LocationId, unsigned int SessionId, bool bFull) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTLOCATIONINFO, &Packet) == 0) {
        FRpcRequestLocationInfoPacket* TypedPacket = static_cast<FRpcRequestLocationInfoPacket*>(Packet);
        TypedPacket->Id = LocationId;
        memcpy(&TypedPacket->bFull, &bFull, 1u);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFObjSelect(unsigned int ObjectId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_GFSELECTOBJECT, &Packet) == 0) {
        FRpcGfSelectObjectPacket* TypedPacket = static_cast<FRpcGfSelectObjectPacket*>(Packet);
        TypedPacket->Value = ObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_GFGOODVAPORIZED, &Packet) == 0) {
        FRpcGfGoodVaporizedPacket* TypedPacket = static_cast<FRpcGfGoodVaporizedPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->Field_0x04 = Info.Field_0x04;
        TypedPacket->Field_0x08 = Info.Field_0x08;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::MissionResponse(unsigned int Arg0, unsigned long MissionId, unsigned char bAccepted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_MISSIONRESPONSE, &Packet) == 0) {
        FRpcMissionResponsePacket* TypedPacket = static_cast<FRpcMissionResponsePacket*>(Packet);
        TypedPacket->Field_0x00 = Arg0; // TODO: first argument's game meaning.
        TypedPacket->MissionId = MissionId;
        TypedPacket->bAccepted = bAccepted;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::TradeResponse(const unsigned char* Data, int Size, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_TRADERESPONSE, &Packet) == 0) {
        FRpcTradeResponsePacket* TypedPacket = static_cast<FRpcTradeResponsePacket*>(Packet);
        TypedPacket->CopyData(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_GFGOODBUY, &Packet) == 0) {
        FRpcGfGoodBuyPacket* TypedPacket = static_cast<FRpcGfGoodBuyPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->Field_0x04 = Info.Field_0x04;
        TypedPacket->Field_0x08 = Info.Field_0x08;
        TypedPacket->Field_0x0c = Info.Field_0x0c;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFGoodSell(const SGFGoodSellInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_GFGOODSELL, &Packet) == 0) {
        FRpcGfGoodSellPacket* TypedPacket = static_cast<FRpcGfGoodSellPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->Field_0x04 = Info.Field_0x04;
        TypedPacket->Field_0x08 = Info.Field_0x08;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SystemSwitchOutComplete(unsigned int ShipObjectId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE, &Packet) == 0) {
        FRpcSystemSwitchOutCompletePacket* TypedPacket = static_cast<FRpcSystemSwitchOutCompletePacket*>(Packet);
        TypedPacket->Value = ShipObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PlayerLaunch(unsigned int ShipId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTLAUNCH, &Packet) == 0) {
        FRpcRequestLaunchPacket* TypedPacket = static_cast<FRpcRequestLaunchPacket*>(Packet);
        TypedPacket->Value = ShipId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LaunchComplete(unsigned int Arg0, unsigned int ObjectId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_LAUNCHCOMPLETE, &Packet) == 0) {
        FRpcLaunchCompletePacket* TypedPacket = static_cast<FRpcLaunchCompletePacket*>(Packet);
        TypedPacket->Field_0x00 = Arg0;
        TypedPacket->Field_0x04 = ObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::JumpInComplete(unsigned int Arg0, unsigned int ObjectId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_JUMPINCOMPLETE, &Packet) == 0) {
        FRpcJumpInCompletePacket* TypedPacket = static_cast<FRpcJumpInCompletePacket*>(Packet);
        TypedPacket->Field_0x00 = Arg0;
        TypedPacket->Field_0x04 = ObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::Hail(unsigned int ObjectId, unsigned int Arg1, unsigned int Arg2) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_HAIL, &Packet) == 0) {
        FRpcHailPacket* TypedPacket = static_cast<FRpcHailPacket*>(Packet);
        TypedPacket->Field_0x00 = ObjectId;
        TypedPacket->Field_0x04 = Arg1;
        TypedPacket->Field_0x08 = Arg2;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x01, &Packet) == 0) {
        const double* TimeSource = &Info.Timestamp;
        float NarrowedTime;
        // 06bd7fc6/7fcc narrows before any packet payload store.
        __asm {
            mov eax, TimeSource
            fld qword ptr [eax]
            fstp dword ptr NarrowedTime
        }
        FRpcUpdateObjectPacket* TypedPacket = static_cast<FRpcUpdateObjectPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x2c;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Position = Info.Position;
        TypedPacket->Orientation = Info.Orientation;
        std::memcpy(&TypedPacket->UpdateTime, &NarrowedTime, sizeof(NarrowedTime));
        std::memcpy(&TypedPacket->Field_0x28, &Info.Field_0x28, sizeof(Info.Field_0x28));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_MUNCOLLISION, &Packet) == 0) {
        FRpcMunitionCollisionPacket* TypedPacket = static_cast<FRpcMunitionCollisionPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->Field_0x04 = Info.Field_0x04;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Field_0x0c = Info.Field_0x0c;
        std::memcpy(&TypedPacket->HitPosition.x, &Info.Field_0x10, 4u);
        std::memcpy(&TypedPacket->HitPosition.y, &Info.Field_0x14, 4u);
        std::memcpy(&TypedPacket->HitPosition.z, &Info.Field_0x18, 4u);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_BADLANDSOBJCOLLISION, &Packet) == 0) {
        FRpcBadlandsObjectCollisionPacket* TypedPacket = static_cast<FRpcBadlandsObjectCollisionPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Field_0x08 = Info.Field_0x08;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_OBJCOLLISION, &Packet) == 0) {
        FRpcObjectCollisionPacket* TypedPacket = static_cast<FRpcObjectCollisionPacket*>(Packet);
        TypedPacket->TargetObjectId = Info.TargetObjectId;
        TypedPacket->TargetSubObjId = Info.TargetSubObjId;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->SubObjId = Info.SubObjId;
        std::memcpy(&TypedPacket->Field_0x10, &Info.Field_0x10, 4u);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPRequestUseItem(const SSPUseItem& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUEST_USE_ITEM, &Packet) == 0) {
        FRpcRequestUseItemPacket* TypedPacket = static_cast<FRpcRequestUseItemPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->ItemId = Info.ItemId;
        TypedPacket->Count = Info.Count;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPRequestInvincibility(unsigned int ShipId, unsigned char bInvincible, InvincibilityReason Reason, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQINVINCIBILITY, &Packet) == 0) {
        FRpcRequestInvincibilityPacket* TypedPacket = static_cast<FRpcRequestInvincibilityPacket*>(Packet);
        TypedPacket->ShipId = ShipId;
        TypedPacket->Reason = static_cast<unsigned char>(Reason);
        TypedPacket->bInvincible = bInvincible;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SaveGame(const CHARACTER_ID& CharId, const wchar_t* Description, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_SAVEGAME, &Packet) == 0) {
        FRpcSaveGamePacket* TypedPacket = static_cast<FRpcSaveGamePacket*>(Packet);
        TypedPacket->CharacterId = CharId;
        const unsigned int DescriptionCount = static_cast<unsigned int>(std::wcslen(Description)) + 1u;
        TypedPacket->CopyDescription(Description, DescriptionCount);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::MissionSaveB(unsigned int SessionId, unsigned long MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_MISSIONSAVEB, &Packet) == 0) {
        FRpcMissionSaveBPacket* TypedPacket = static_cast<FRpcMissionSaveBPacket*>(Packet);
        TypedPacket->Value = MissionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestEvent(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned int Arg3, unsigned long Arg4, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUEST_EVENT, &Packet) == 0) {
        FRpcRequestEventPacket* TypedPacket = static_cast<FRpcRequestEventPacket*>(Packet);
        TypedPacket->EventType = static_cast<unsigned char>(EventType);
        TypedPacket->Arg1 = Arg1;
        TypedPacket->Arg2 = Arg2;
        TypedPacket->Arg3 = Arg3;
        TypedPacket->Field_0x10 = static_cast<unsigned char>(Arg4);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestCancel(int EventType, unsigned int Arg1, unsigned int Arg2, unsigned long Arg3, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUEST_CANCEL, &Packet) == 0) {
        FRpcRequestCancelPacket* TypedPacket = static_cast<FRpcRequestCancelPacket*>(Packet);
        TypedPacket->EventType = static_cast<unsigned char>(EventType);
        TypedPacket->Arg1 = Arg1;
        TypedPacket->Arg2 = Arg2;
        TypedPacket->Field_0x0c = static_cast<unsigned char>(Arg3);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::MineAsteroid(unsigned int SystemId, const Vector& Position, unsigned int LootId, unsigned int SecondaryLoot, unsigned int Quantity, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_MINEASTEROID, &Packet) == 0) {
        FRpcMineAsteroidPacket* TypedPacket = static_cast<FRpcMineAsteroidPacket*>(Packet);
        TypedPacket->SystemId = SystemId;
        std::memcpy(&TypedPacket->Position.x, &Position.x, 4u);
        std::memcpy(&TypedPacket->Position.y, &Position.y, 4u);
        std::memcpy(&TypedPacket->Position.z, &Position.z, 4u);
        TypedPacket->LootId = LootId;
        TypedPacket->SecondaryLoot = SecondaryLoot;
        TypedPacket->Quantity = Quantity;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CommComplete(unsigned int Arg0, unsigned int Arg1, unsigned int Arg2, CommResult Result) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_COMMCOMPLETE, &Packet) == 0) {
        FRpcCommCompletePacket* TypedPacket = static_cast<FRpcCommCompletePacket*>(Packet);
        TypedPacket->Field_0x00 = Arg0;
        TypedPacket->Field_0x04 = Arg1;
        TypedPacket->Field_0x08 = Arg2;
        TypedPacket->Field_0x0c = static_cast<unsigned int>(Result);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestCreateShip(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQUESTCREATESHIP, &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPScanCargo(const unsigned int& ScannerId, const unsigned int& TargetId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_CARGOSCAN, &Packet) == 0) {
        FRpcCargoScanPacket* TypedPacket = static_cast<FRpcCargoScanPacket*>(Packet);
        TypedPacket->Field_0x00 = ScannerId;
        TypedPacket->Field_0x04 = TargetId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetManeuver(unsigned int SessionId, const XSetManeuver& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_SETMANEUVER, &Packet) == 0) {
        FRpcSetManeuverPacket* TypedPacket = static_cast<FRpcSetManeuverPacket*>(Packet);
        TypedPacket->Field_0x00 = Info.Field_0x00;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Field_0x08 = Info.Field_0x08;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::InterfaceItemUsed(unsigned int SessionId, unsigned int ItemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_INTERFACEITEMUSED, &Packet) == 0) {
        FRpcInterfaceItemUsedPacket* TypedPacket = static_cast<FRpcInterfaceItemUsedPacket*>(Packet);
        TypedPacket->Value = ItemId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::AbortMission(unsigned int SessionId, unsigned int MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_ABORT_MISSION, &Packet) == 0) {
        FRpcAbortMissionPacket* TypedPacket = static_cast<FRpcAbortMissionPacket*>(Packet);
        TypedPacket->Value = MissionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RTCDone(unsigned int SessionId, unsigned int RtcId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_RTCDONE, &Packet) == 0) {
        FRpcRtcDonePacket* TypedPacket = static_cast<FRpcRtcDonePacket*>(Packet);
        TypedPacket->Value = RtcId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetWeaponGroup(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x11, &Packet) == 0) {
        FRpcSetWeaponGroupPacket* TypedPacket = static_cast<FRpcSetWeaponGroupPacket*>(Packet);
        TypedPacket->BorrowData(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetVisitedState(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x13, &Packet) == 0) {
        FRpcSetVisitedStatePacket* TypedPacket = static_cast<FRpcSetVisitedStatePacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestBestPath(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x16, &Packet) == 0) {
        FRpcRequestBestPathPacket* TypedPacket = static_cast<FRpcRequestBestPathPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestPlayerStats(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x18, &Packet) == 0) {
        FRpcRequestPlayerStatsPacket* TypedPacket = static_cast<FRpcRequestPlayerStatsPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PopUpDialog(unsigned int SessionId, unsigned int Arg1) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1b, &Packet) == 0) {
        FRpcPopUpDialogPacket* TypedPacket = static_cast<FRpcPopUpDialogPacket*>(Packet);
        TypedPacket->Field_0x10 = Arg1;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestGroupPositions(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x17, &Packet) == 0) {
        FRpcRequestGroupPositionsPacket* TypedPacket = static_cast<FRpcRequestGroupPositionsPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetMissionLog(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x19, &Packet) == 0) {
        FRpcSetMissionLogPacket* TypedPacket = static_cast<FRpcSetMissionLogPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetInterfaceState(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1c, &Packet) == 0) {
        FRpcSetInterfaceStatePacket* TypedPacket = static_cast<FRpcSetInterfaceStatePacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestRankLevel(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x1a, &Packet) == 0) {
        FRpcRequestRankLevelPacket* TypedPacket = static_cast<FRpcRequestRankLevelPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::InitiateTrade(unsigned int SessionId, unsigned int TargetSessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x01;
        TypedPacket->TargetPlayer = TargetSessionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::TerminateTrade(unsigned int SessionId, int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x02;
        TypedPacket->Reason = Reason;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::AcceptTrade(unsigned int SessionId, bool bAccepted) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x40;
        TypedPacket->bAccepted = static_cast<unsigned char>(bAccepted);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetTradeMoney(unsigned int SessionId, unsigned long Money) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x04;
        TypedPacket->Money = static_cast<int>(Money);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::AddTradeEquip(unsigned int SessionId, const EquipDesc& Equip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x08;
        TypedPacket->EquipArchId = Equip.get_arch_id();
        TypedPacket->EquipCount = Equip.get_count();
        const float* HealthSource = &Equip.Health;
        float* HealthDestination = &TypedPacket->EquipHealth;
        PortableFloatLoad::Copy(HealthDestination, HealthSource);
        TypedPacket->EquipId = Equip.get_id();
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::DelTradeEquip(unsigned int SessionId, const EquipDesc& Equip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x10;
        TypedPacket->EquipArchId = Equip.get_arch_id();
        TypedPacket->EquipCount = Equip.get_count();
        TypedPacket->EquipId = Equip.get_id();
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestTrade(unsigned int SessionId, unsigned int TargetSessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x20;
        TypedPacket->TargetPlayer = TargetSessionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::StopTradeRequest(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x12, &Packet) == 0) {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = 0x80;
        TypedPacket->TargetPlayer = SessionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqDifficultyScale(float Scale, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_REQDIFFICULTYSCALE, &Packet) == 0) {
        FRpcRequestDifficultyScalePacket* TypedPacket = static_cast<FRpcRequestDifficultyScalePacket*>(Packet);
        float* Destination = &TypedPacket->Value;
        PortableFloatLoad::Copy(Destination, &Scale);
        this->PushToServer(Packet->AsRaw());
    }
}

unsigned int IServerImpl::GetServerID() {
    return 0;
}

const char* IServerImpl::GetServerSig() {
    return "";
}

void IServerImpl::DumpPacketStats(const char* Name) {
    RpcGetRemotePacketSerializer()->DumpStats(Name);
}

void IServerImpl::Dock(const unsigned int& BaseId, const unsigned int& SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(3, FLPACKET_CLIENT_DOCK, &Packet) == 0) {
        FRpcDockPacket* TypedPacket = static_cast<FRpcDockPacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PushToServer(CDAPacket* Packet) {
    return;
}

void IServerImpl::SubmitChat(CHAT_ID SenderId, unsigned long Size, const void* MsgData, CHAT_ID TargetId, int SystemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(1, 0x05, &Packet) == 0) {
        FRpcChatMsgPacket* TypedPacket = static_cast<FRpcChatMsgPacket*>(Packet);
        if (MsgData == nullptr || Size == 0) {
            TypedPacket->DataSize = 0;
            TypedPacket->Data = nullptr;
        } else {
            TypedPacket->DataSize = Size;
            TypedPacket->Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
            std::memcpy(TypedPacket->Data, MsgData, Size);
        }
        TypedPacket->TargetId = TargetId.Id;
        this->PushToServer(Packet->AsRaw());
    }
}

