// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include <map>
#include "common_ini.h"
#include "common_physys.h"

int RegisterChannel1Packets(FPacketSerializer* Serializer) {
    return RpcRegisterCommonScalarPackets(Serializer);
}

// Implement Serializer methods missing from rpclocal exports

int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory) {
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.lower_bound(PacketId);
    if (It != Types.end() && It->first == PacketId) {
        return 0x42; // kPacketRegisterDuplicate
    }
    FPacketTypeInfo* Info = new FPacketTypeInfo;
    Info->Factory = Factory;
    Info->Name = Name;
    Types.insert(It, std::map<unsigned int, FPacketTypeInfo*>::value_type(PacketId, Info));
    return 0;
}

FPacketBase::FPacketBase(unsigned int InPacketId) : PacketId(InPacketId) {
    MarshalCursor = 0;
    MarshalError = 0;
    Field_0x08 = nullptr;
}
FPacketBase::~FPacketBase() {}
void FPacketBase::Dump(std::ostream& Out) const {}
unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // 06bebec0: inline entry, cached header commit, Error=0, OutLength, stats, 0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

using FLPACKET_CLIENT_REQCHANGECASH = FRpcRequestChangeCashPacket;

using FLPACKET_CLIENT_REQSETCASH = FRpcRequestSetCashPacket;

// FUN_06be9250
using FLPACKET_CLIENT_REQREMOVEITEM = FRpcRequestRemoveItemPacket;

using FLPACKET_CLIENT_REQMODIFYITEM = FRpcRequestModifyItemPacket;

using FLPACKET_CLIENT_REQCARGO = FRpcRequestCargoPacket;

using FLPACKET_CLIENT_REQADDITEM = FRpcRequestAddItemPacket;

using FLPACKET_CLIENT_REQSHIPARCH = FRpcRequestShipArchPacket;


using FLPACKET_CLIENT_REQEQUIPMENT = FRpcRequestEquipmentPacket;

using FLPACKET_CLIENT_MUNCOLLISION = FRpcMunitionCollisionPacket;

using FLPACKET_CLIENT_BADLANDSOBJCOLLISION = FRpcBadlandsObjectCollisionPacket;

using FLPACKET_CLIENT_OBJCOLLISION = FRpcObjectCollisionPacket;

using FLPACKET_CLIENT_REQUEST_EVENT = FRpcRequestEventPacket;

using FLPACKET_CLIENT_REQUEST_CANCEL = FRpcRequestCancelPacket;

using FLPACKET_CLIENT_CARGOSCAN = FRpcCargoScanPacket;

using FLPACKET_CLIENT_REQUESTLAUNCH = FRpcRequestLaunchPacket;

using FLPACKET_CLIENT_REQUESTCHARINFO = FRpcRequestCharInfoPacket;

using FLPACKET_CLIENT_SKIP_AUTOSAVE = FRpcSkipAutosavePacket;

using FLPACKET_CLIENT_SELECTCHARACTER = FRpcSelectCharacterPacket;

using FLPACKET_CLIENT_COMMCOMPLETE = FRpcCommCompletePacket;

using FLPACKET_CLIENT_REQUESTNEWCHARINFO = FRpcRequestNewCharInfoPacket;

using FLPACKET_CLIENT_CREATENEWCHAR = FRpcCreateNewCharacterPacket;

using FLPACKET_CLIENT_DESTROYCHAR = FRpcDestroyCharacterPacket;

using FLPACKET_CLIENT_LOGIN = FRpcClientLoginPacket;

using FLPACKET_CLIENT_ENTERBASE = FRpcEnterBasePacket;

using FLPACKET_CLIENT_REQUESTBASEINFO = FRpcRequestBaseInfoPacket;

using FLPACKET_CLIENT_REQUESTLOCATIONINFO = FRpcRequestLocationInfoPacket;

using FLPACKET_CLIENT_GFREQUESTSHIPINFO = FRpcGfRequestShipInfoPacket;

using FLPACKET_CLIENT_SYSTEM_SWITCH_OUT = FRpcSystemSwitchOutCompletePacket;

using FLPACKET_CLIENT_EXITBASE = FRpcExitBasePacket;

using FLPACKET_CLIENT_ENTERLOCATION = FRpcEnterLocationPacket;

using FLPACKET_CLIENT_EXITLOCATION = FRpcExitLocationPacket;

using FLPACKET_CLIENT_REQUESTCREATESHIP = FRpcRequestCreateShipPacket;

using FLPACKET_CLIENT_INTERFACEITEMUSED = FRpcInterfaceItemUsedPacket;

using FLPACKET_CLIENT_RTCDONE = FRpcRtcDonePacket;

using FLPACKET_CLIENT_ABORT_MISSION = FRpcAbortMissionPacket;

using FLPACKET_CLIENT_LAUNCHCOMPLETE = FRpcLaunchCompletePacket;

using FLPACKET_CLIENT_JUMPINCOMPLETE = FRpcJumpInCompletePacket;

using FLPACKET_CLIENT_HAIL = FRpcHailPacket;

using FLPACKET_CLIENT_GFGOODBUY = FRpcGfGoodBuyPacket;

using FLPACKET_CLIENT_GFGOODSELL = FRpcGfGoodSellPacket;

using FLPACKET_CLIENT_GFGOODVAPORIZED = FRpcGfGoodVaporizedPacket;

using FLPACKET_CLIENT_GFSELECTOBJECT = FRpcGfSelectObjectPacket;

using FLPACKET_CLIENT_SAVEGAME = FRpcSaveGamePacket;

using FLPACKET_CLIENT_MINEASTEROID = FRpcMineAsteroidPacket;

using FLPACKET_CLIENT_MISSIONRESPONSE = FRpcMissionResponsePacket;

using FLPACKET_CLIENT_TRADERESPONSE = FRpcTradeResponsePacket;

using FLPACKET_CLIENT_DBGCREATESHIP = FRpcDebugCreateShipPacket;

using FLPACKET_CLIENT_DBGLOADSYSTEM = FRpcDebugLoadSystemPacket;

using FLPACKET_CLIENT_DBGFREESYSTEM = FRpcDebugFreeSystemPacket;

using FLPACKET_CLIENT_DOCK = FRpcDockPacket;

using FLPACKET_CLIENT_DBGCONSOLE = FRpcDebugConsolePacket;

using FLPACKET_CLIENT_DBGDESTROYOBJECT = FRpcDebugDestroyObjectPacket;

using FLPACKET_CLIENT_DBGRELOCATE_SHIP = FRpcDebugRelocateShipPacket;

using FLPACKET_CLIENT_SETMANEUVER = FRpcSetManeuverPacket;

using FLPACKET_CLIENT_REQCOLLISIONGROUPS = FRpcRequestCollisionGroupsPacket;

using FLPACKET_CLIENT_REQHULLSTATUS = FRpcRequestHullStatusPacket;

using FLPACKET_CLIENT_REQUEST_USE_ITEM = FRpcRequestUseItemPacket;

using FLPACKET_CLIENT_REQINVINCIBILITY = FRpcRequestInvincibilityPacket;

using FLPACKET_CLIENT_MISSIONSAVEB = FRpcMissionSaveBPacket;

using FLPACKET_CLIENT_REQDIFFICULTYSCALE = FRpcRequestDifficultyScalePacket;

int RegisterChannel3Packets(FPacketSerializer* Serializer) {
    return RpcRegisterClientScalarPackets(Serializer);
}


int RegisterChannel2Packets(FPacketSerializer* Serializer) {
    return RpcRegisterRemoteServerServerPackets(Serializer);
}
