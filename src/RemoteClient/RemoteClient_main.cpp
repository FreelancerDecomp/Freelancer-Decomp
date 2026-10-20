// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <windows.h>
#include "ClientImpl.h"
#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "dacom.h"
#include "common_server_types.h"
#include "common_server.h"
#include "common_goods.h"

static SLoginInfo GStaticLoginInfo = {};
static SCreateCharacterInfo GStaticCreateCharacterInfo = {};

IServer* RemoteClientGetServer();
IChatServer* RemoteClientGetChatServer();

static void DispatchClientPacket(unsigned int ClientId, CDAPacket* Packet)
{
    IServer* Server = RemoteClientGetServer();

    unsigned int Channel = Packet->GetChannel();
    unsigned int Opcode = Packet->GetOpcode();

    if (Channel == 1) {
        switch (Opcode) {
        case 0x01: // FLPACKET_COMMON_UPDATEOBJECT
        {
            const FRpcUpdateObjectPacket* Wire =
            static_cast<const FRpcUpdateObjectPacket*>(
            reinterpret_cast<const FPacketBase*>(Packet));

            SSPObjUpdateInfo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.Orientation = Wire->Orientation;
            Info.Position = Wire->Position;
            Info.Field_0x2c = Wire->Field_0x00;

            // Original x87 conversions.
            Info.Timestamp = static_cast<double>(Wire->UpdateTime);

            float field = Wire->Field_0x28;
            static_assert(sizeof(float) == sizeof(unsigned int));

            std::memcpy(&Info.Field_0x28, &field, sizeof(field));

            Server->SPObjUpdate(Info, ClientId);
            break;
        }
        case 0x02: // FLPACKET_COMMON_FIREWEAPON
        {
            const FRpcFireWeaponPacket* Wire = static_cast<const FRpcFireWeaponPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XFireWeaponInfo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TargetPos = Wire->TargetPos;
            Info.Hardpoints = Wire->Hardpoints;
            Server->FireWeapon(ClientId, Info);
            break;
        }
        case 0x04: // FLPACKET_COMMON_SETTARGET
        {
            const FRpcSetTargetPacket* Wire = static_cast<const FRpcSetTargetPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XSetTarget Info;
            Info.ObjectId = Wire->ObjectId;
            Info.SubObjId = Wire->SubObjId;
            Info.TargetObjectId = Wire->TargetId;
            Info.TargetSubObjId = Wire->TargetSubObjId;
            Server->SetTarget(ClientId, Info);
            break;
        }
        case 0x05: // FLPACKET_COMMON_CHATMSG
        {
            const FRpcChatMsgPacket* Wire = static_cast<const FRpcChatMsgPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            RemoteClientGetChatServer()->SubmitChat(
                CHAT_ID(static_cast<unsigned short>(ClientId)), Wire->DataSize,
                Wire->Data, CHAT_ID(Wire->TargetId), -1);
            break;
        }
        case 0x08: // FLPACKET_COMMON_ACTIVATEEQUIP
        {
            const FRpcActivateEquipPacket* Wire = static_cast<const FRpcActivateEquipPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XActivateEquip Info;
            Info.ObjectId = Wire->ObjectId;
            Info.EquipId = Wire->EquipId;
            Info.bActivate = (Wire->bActive != 0);
            Server->ActivateEquip(ClientId, Info);
            break;
        }
        case 0x0E: // FLPACKET_COMMON_ACTIVATECRUISE
        {
            const FRpcActivateCruisePacket* Wire = static_cast<const FRpcActivateCruisePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XActivateCruise Info;
            Info.ObjectId = Wire->ObjectId;
            Info.bActivate = (Wire->bActive != 0);
            Info.Field_0x05 = (Wire->Field_0x05 != 0) ? 1u : 0u;
            Server->ActivateCruise(ClientId, Info);
            break;
        }
        case 0x0F: // FLPACKET_COMMON_GOTRADELANE
        {
            const FRpcGoTradeLanePacket* Wire = static_cast<const FRpcGoTradeLanePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XGoTradelane Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TradelaneRing1 = Wire->TradelaneRing1;
            Info.TradelaneRing2 = Wire->TradelaneRing2;
            Info.Field_0x0c = (Wire->Field_0x0c != 0);
            Server->GoTradelane(ClientId, Info);
            break;
        }
        case 0x10: // FLPACKET_COMMON_STOPTRADELANE
        {
            const FRpcStopTradeLanePacket* Wire = static_cast<const FRpcStopTradeLanePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->StopTradelane(ClientId, Wire->ShipId, Wire->TradeLaneRing1, Wire->TradeLaneRing2);
            break;
        }
        case 0x11: // FLPACKET_COMMON_SET_WEAPON_GROUP
        {
            const FRpcSetWeaponGroupPacket* Wire = static_cast<const FRpcSetWeaponGroupPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SetWeaponGroup(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x12: // FLPACKET_COMMON_PLAYER_TRADE
        {
            const FRpcPlayerTradePacket* Wire = static_cast<const FRpcPlayerTradePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            switch (Wire->Action)
            {
            case 0x01:
                Server->InitiateTrade(ClientId, Wire->TargetPlayer);
                break;
            case 0x02:
                Server->TerminateTrade(ClientId, Wire->Reason);
                break;
            case 0x04:
                Server->SetTradeMoney(ClientId, static_cast<unsigned long>(Wire->Money));
                break;
            case 0x08:
            {
                EquipDesc Equip;
                Equip.set_arch_id(Wire->EquipArchId);
                Equip.set_count(Wire->EquipCount);
                Equip.set_status(Wire->EquipHealth);
                Equip.set_id(Wire->EquipId);
                Server->AddTradeEquip(ClientId, Equip);
                break;
            }
            case 0x10:
            {
                EquipDesc Equip;
                Equip.set_arch_id(Wire->EquipArchId);
                Equip.set_count(Wire->EquipCount);
                Equip.set_id(Wire->EquipId);
                Server->DelTradeEquip(ClientId, Equip);
                break;
            }
            case 0x20:
                Server->RequestTrade(ClientId, Wire->TargetPlayer);
                break;
            case 0x40:
                // The native decoder already produces a canonical 0/1 byte.
                Server->AcceptTrade(ClientId, Wire->bAccepted != 0);
                break;
            case 0x80:
                Server->StopTradeRequest(Wire->TargetPlayer);
                break;
            default:
                break;
            }
            break;
        }
        case 0x13: // FLPACKET_COMMON_SET_VISITED_STATE
        {
            const FRpcSetVisitedStatePacket* Wire = static_cast<const FRpcSetVisitedStatePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SetVisitedState(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x14: // FLPACKET_COMMON_JETTISONCARGO
        {
            const FRpcJettisonCargoPacket* Wire = static_cast<const FRpcJettisonCargoPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XJettisonCargo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.ItemId = Wire->ItemId;
            Info.Count = Wire->Count;
            Server->JettisonCargo(ClientId, Info);
            break;
        }
        case 0x15: // FLPACKET_COMMON_ACTIVATETHRUSTERS
        {
            const FRpcActivateThrustersPacket* Wire = static_cast<const FRpcActivateThrustersPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XActivateThrusters Info;
            Info.ObjectId = Wire->ObjectId;
            Info.bActivate = (Wire->bActive != 0);
            Server->ActivateThrusters(ClientId, Info);
            break;
        }
        case 0x16: // FLPACKET_COMMON_REQUEST_BEST_PATH
        {
            const FRpcRequestBestPathPacket* Wire = static_cast<const FRpcRequestBestPathPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->RequestBestPath(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x17: // FLPACKET_COMMON_REQUEST_GROUP_POSITIONS
        {
            const FRpcRequestGroupPositionsPacket* Wire = static_cast<const FRpcRequestGroupPositionsPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->RequestGroupPositions(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x18: // FLPACKET_COMMON_REQUEST_PLAYER_STATS
        {
            const FRpcRequestPlayerStatsPacket* Wire = static_cast<const FRpcRequestPlayerStatsPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->RequestPlayerStats(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x19: // FLPACKET_COMMON_SET_MISSION_LOG
        {
            const FRpcSetMissionLogPacket* Wire = static_cast<const FRpcSetMissionLogPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SetMissionLog(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x1A: // FLPACKET_COMMON_REQUEST_RANK_LEVEL
        {
            const FRpcRequestRankLevelPacket* Wire = static_cast<const FRpcRequestRankLevelPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->RequestRankLevel(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x1B: // FLPACKET_COMMON_POP_UP_DIALOG
        {
            const FRpcPopUpDialogPacket* Wire = static_cast<const FRpcPopUpDialogPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->PopUpDialog(ClientId, Wire->Field_0x10);
            break;
        }
        case 0x1C: // FLPACKET_COMMON_SET_INTERFACE_STATE
        {
            const FRpcSetInterfaceStatePacket* Wire = static_cast<const FRpcSetInterfaceStatePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SetInterfaceState(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case 0x1D: // FLPACKET_COMMON_TRACTOROBJECTS
        {
            const FRpcTractorObjectsPacket* Wire = static_cast<const FRpcTractorObjectsPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XTractorObjects Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TractorEquipId = Wire->TractorEquipId;
            Info.TargetIds = Wire->TargetIds;
            Server->TractorObjects(ClientId, Info);
            break;
        }
        default:
            break;
        }
    } else if (Channel == 3) {
        switch (Opcode) {
        case 1:
            // Slot 18 - Login
            {
                const FRpcClientLoginPacket* Wire = static_cast<const FRpcClientLoginPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
                wchar_t AccountName[256];
                wchar_t SecondString[256];
                memcpy(AccountName, Wire->AccountName, sizeof(AccountName));
                memcpy(SecondString, Wire->Field_0x220, sizeof(SecondString));
                const unsigned char CreateFlag = Wire->CreateFlag;
                wcsncpy(GStaticLoginInfo.AccountName, AccountName, 0x28);
                wcsncpy(GStaticLoginInfo.Field_0xA0, SecondString, 0x10);
                *reinterpret_cast<unsigned char*>(&GStaticLoginInfo.CreateFlag) = CreateFlag;
                GStaticLoginInfo.AccountName[39] = L'\0';
                GStaticLoginInfo.Field_0xA0[15] = L'\0';
                Server->Login(GStaticLoginInfo, ClientId);
            }
            break;
        case 3:
            // Slot 53 - SPMunitionCollision
            {
                const FRpcMunitionCollisionPacket* Wire = static_cast<const FRpcMunitionCollisionPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
                SSPMunitionCollisionInfo Info;
                Info.Field_0x00 = Wire->Field_0x00;
                Info.Field_0x04 = Wire->Field_0x04;
                Info.ObjectId = Wire->ObjectId;
                Info.Field_0x0c = Wire->Field_0x0c;
                std::memcpy(&Info.Field_0x10, &Wire->HitPosition.x, 4u);
                std::memcpy(&Info.Field_0x14, &Wire->HitPosition.y, 4u);
                std::memcpy(&Info.Field_0x18, &Wire->HitPosition.z, 4u);
                Server->SPMunitionCollision(Info, ClientId);
            }
            break;
        case 4:
            // Slot 48 - PlayerLaunch
        {
            const FRpcRequestLaunchPacket* Wire = static_cast<const FRpcRequestLaunchPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->PlayerLaunch(Wire->Value, ClientId);
            break;
        }
        case 5:
            // Slot 19 - CharacterInfoReq
            Server->CharacterInfoReq(ClientId, false);
            break;
        case 6:
            // Slot 20 - CharacterSelect
        {
            const FRpcSelectCharacterPacket* Wire = static_cast<const FRpcSelectCharacterPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->CharacterSelect(Wire->CharacterId, ClientId);
            break;
        }
        case 7:
            // Slot 35 - BaseEnter
        {
            const FRpcEnterBasePacket* Wire = static_cast<const FRpcEnterBasePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->BaseEnter(Wire->Value, ClientId);
            break;
        }
        case 8:
            // Slot 39 - BaseInfoRequest
        {
            const FRpcRequestBaseInfoPacket* Wire = static_cast<const FRpcRequestBaseInfoPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            // The Server/BaseDB endpoint observes only zero versus nonzero.
            Server->BaseInfoRequest(Wire->Id, ClientId, Wire->bFull != 0);
            break;
        }
        case 9:
            // Slot 40 - LocationInfoRequest
        {
            const FRpcRequestLocationInfoPacket* Wire = static_cast<const FRpcRequestLocationInfoPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->LocationInfoRequest(Wire->Id, ClientId, Wire->bFull != 0);
            break;
        }
        case 0x0A: // FLPACKET_CLIENT_GFREQUESTSHIPINFO
            break;
        case 11:
            // Slot 47 - SystemSwitchOutComplete
        {
            const FRpcSystemSwitchOutCompletePacket* Wire = static_cast<const FRpcSystemSwitchOutCompletePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SystemSwitchOutComplete(Wire->Value, ClientId);
            break;
        }
        case 12:
            // Slot 55 - SPObjCollision
            {
                const FRpcObjectCollisionPacket* Wire = static_cast<const FRpcObjectCollisionPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
                SSPObjCollisionInfo Info;
                Info.TargetObjectId = Wire->TargetObjectId;
                Info.TargetSubObjId = Wire->TargetSubObjId;
                Info.Pad_0x06 = 0;
                Info.ObjectId = Wire->ObjectId;
                Info.SubObjId = Wire->SubObjId;
                std::memcpy(&Info.Field_0x10, &Wire->Field_0x10, 4u);
                Server->SPObjCollision(Info, ClientId);
            }
            break;
        case 13:
            // Slot 36 - BaseExit
        {
            const FRpcExitBasePacket* Wire = static_cast<const FRpcExitBasePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->BaseExit(Wire->Value, ClientId);
            break;
        }
        case 14:
            // Slot 37 - LocationEnter
        {
            const FRpcEnterLocationPacket* Wire = static_cast<const FRpcEnterLocationPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->LocationEnter(Wire->Value, ClientId);
            break;
        }
        case 15:
            // Slot 38 - LocationExit
        {
            const FRpcExitLocationPacket* Wire = static_cast<const FRpcExitLocationPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->LocationExit(Wire->Value, ClientId);
            break;
        }
        
        case 0x10: // FLPACKET_CLIENT_REQUESTCREATESHIP
            Server->RequestCreateShip(ClientId);
            break;
        case 0x11: // FLPACKET_CLIENT_GFGOODSELL
        {
            const FRpcGfGoodSellPacket* Wire = static_cast<const FRpcGfGoodSellPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            SGFGoodSellInfo Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.Field_0x04 = Wire->Field_0x04;
            Info.Field_0x08 = Wire->Field_0x08;
            Server->GFGoodSell(Info, ClientId);
            break;
        }
        case 0x12: // FLPACKET_CLIENT_GFGOODBUY
        {
            const FRpcGfGoodBuyPacket* Wire = static_cast<const FRpcGfGoodBuyPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            SGFGoodBuyInfo Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.Field_0x04 = Wire->Field_0x04;
            Info.Field_0x08 = Wire->Field_0x08;
            Info.Field_0x0c = Wire->Field_0x0c;
            Server->GFGoodBuy(Info, ClientId);
            break;
        }
        case 0x13: // FLPACKET_CLIENT_GFSELECTOBJECT
        {
            const FRpcGfSelectObjectPacket* Wire = static_cast<const FRpcGfSelectObjectPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->GFObjSelect(Wire->Value, ClientId);
            break;
        }
        case 0x14: // FLPACKET_CLIENT_MISSIONRESPONSE
        {
            const FRpcMissionResponsePacket* Wire = static_cast<const FRpcMissionResponsePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->MissionResponse(Wire->Field_0x00, Wire->MissionId, Wire->bAccepted, ClientId);
            break;
        }
        case 0x15: // FLPACKET_CLIENT_REQSHIPARCH
        {
            const FRpcRequestShipArchPacket* Wire = static_cast<const FRpcRequestShipArchPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqShipArch(Wire->Value, ClientId);
            break;
        }
        case 0x16: // FLPACKET_CLIENT_REQEQUIPMENT
        {
            EquipDescList List;
            FRpcServerEquipHandle Snapshot;
            const FRpcRequestEquipmentPacket* Wire = static_cast<const FRpcRequestEquipmentPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Wire->ExtractEquipment(&Snapshot);
            if (Snapshot.Records != nullptr)
            {
                for (std::list<FRpcServerEquipRecord*>::const_iterator It = Snapshot.Records->begin();
                     It != Snapshot.Records->end(); ++It)
                {
                    const FRpcServerEquipRecord* Record = *It;
                    const EquipDesc Default;
                    List.Items.push_back(Default);
                    EquipDesc& Desc = List.Items.back();
                    Desc.set_arch_id(Record->ArchId);
                    Desc.set_count(Record->Count);
                    Desc.set_status(Record->Health);
                    Desc.set_id(Record->Id);
                    Desc.set_equipped(Record->bEquipped);
                    // Native ignores the record's raw Field_0x0f byte here.
                    if (Record->HardpointSize != 0)
                    {
                        CacheString Hardpoint;
                        Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
                        Desc.set_hardpoint(Hardpoint);
                    }
                    else
                    {
                        Desc.make_internal();
                    }
                }
            }
            Server->ReqEquipment(List, ClientId);
            break;
        }
        case 0x17: // FLPACKET_CLIENT_REQCARGO
        {
            EquipDescList List;
            FRpcServerEquipHandle Snapshot;
            const FRpcRequestCargoPacket* Wire = static_cast<const FRpcRequestCargoPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Wire->ExtractEquipment(&Snapshot);
            if (Snapshot.Records != nullptr)
            {
                for (std::list<FRpcServerEquipRecord*>::const_iterator It = Snapshot.Records->begin();
                     It != Snapshot.Records->end(); ++It)
                {
                    const FRpcServerEquipRecord* Record = *It;
                    const EquipDesc Default;
                    List.Items.push_back(Default);
                    EquipDesc& Desc = List.Items.back();
                    Desc.set_arch_id(Record->ArchId);
                    Desc.set_count(Record->Count);
                    Desc.set_status(Record->Health);
                    Desc.set_id(Record->Id);
                    Desc.set_equipped(Record->bEquipped);
                    // Native ignores the record's raw Field_0x0f byte here.
                    if (Record->HardpointSize != 0)
                    {
                        CacheString Hardpoint;
                        Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
                        Desc.set_hardpoint(Hardpoint);
                    }
                    else
                    {
                        Desc.make_internal();
                    }
                }
            }
            Server->ReqCargo(List, ClientId);
            break;
        }
        case 0x18: // FLPACKET_CLIENT_REQADDITEM
        {
            const FRpcRequestAddItemPacket* Wire = static_cast<const FRpcRequestAddItemPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqAddItem(Wire->ArchId, Wire->Hardpoint, Wire->Count, Wire->Health,
                               Wire->bMounted, ClientId);
            break;
        }
        case 0x19: // FLPACKET_CLIENT_REQREMOVEITEM
        {
            const FRpcRequestRemoveItemPacket* Wire = static_cast<const FRpcRequestRemoveItemPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqRemoveItem(static_cast<unsigned short>(Wire->EquipId), Wire->Count, ClientId);
            break;
        }
        case 0x1A: // FLPACKET_CLIENT_REQMODIFYITEM
        {
            const FRpcRequestModifyItemPacket* Wire = static_cast<const FRpcRequestModifyItemPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqModifyItem(static_cast<unsigned short>(Wire->EquipId), Wire->Hardpoint, Wire->Count,
                                  Wire->Health, Wire->bMounted, ClientId);
            break;
        }
        case 0x1B: // FLPACKET_CLIENT_REQSETCASH
        {
            const FRpcRequestSetCashPacket* Wire = static_cast<const FRpcRequestSetCashPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqSetCash(Wire->Value, ClientId);
            break;
        }
        case 0x1C: // FLPACKET_CLIENT_REQCHANGECASH
        {
            const FRpcRequestChangeCashPacket* Wire = static_cast<const FRpcRequestChangeCashPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqChangeCash(Wire->Value, ClientId);
            break;
        }
        case 0x1E: // FLPACKET_CLIENT_SAVEGAME
        {
            const FRpcSaveGamePacket* Wire = static_cast<const FRpcSaveGamePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SaveGame(Wire->CharacterId, Wire->Description, ClientId);
            break;
        }
        case 0x20: // FLPACKET_CLIENT_MINEASTEROID
        {
            const FRpcMineAsteroidPacket* Wire = static_cast<const FRpcMineAsteroidPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Vector Position;
            std::memcpy(&Position.x, &Wire->Position.x, 4u);
            std::memcpy(&Position.y, &Wire->Position.y, 4u);
            std::memcpy(&Position.z, &Wire->Position.z, 4u);
            Server->MineAsteroid(Wire->SystemId, Position, Wire->LootId, Wire->SecondaryLoot,
                                 Wire->Quantity, ClientId);
            break;
        }
        case 0x22: // FLPACKET_CLIENT_DBGCREATESHIP
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x26a,
                  "FLPACKET_CLIENT_DBGCREATESHIP encountered in release mode");
            break;
        case 0x23: // FLPACKET_CLIENT_DBGLOADSYSTEM
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x217,
                  "FLPACKET_CLIENT_DBGLOADSYSTEM encountered in release mode");
            break;
        case 0x24: // FLPACKET_CLIENT_DOCK
        {
            const FRpcDockPacket* Wire = static_cast<const FRpcDockPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->Dock(Wire->Value, ClientId);
            break;
        }
        case 0x25: // FLPACKET_CLIENT_DBGDESTROYOBJECT
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x1e4,
                  "FLPACKET_CLIENT_DBGDESTROYOBJECT encountered in Final Release mode");
            break;
        case 0x27: // FLPACKET_CLIENT_TRADERESPONSE
        {
            const FRpcTradeResponsePacket* Wire = static_cast<const FRpcTradeResponsePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->TradeResponse(Wire->Data, static_cast<int>(Wire->DataSize), ClientId);
            break;
        }
        case 0x2B: // FLPACKET_CLIENT_CARGOSCAN
        {
            const FRpcCargoScanPacket* Wire = static_cast<const FRpcCargoScanPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SPScanCargo(Wire->Field_0x00, Wire->Field_0x04, ClientId);
            break;
        }
        case 0x2D: // FLPACKET_CLIENT_DBGCONSOLE
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x204,
                  "FLPACKET_CLIENT_DBGCONSOLE encountered in release mode");
            break;
        case 0x2E: // FLPACKET_CLIENT_DBGFREESYSTEM
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x227,
                  "FLPACKET_CLIENT_DBGFREESYSTEM encountered in release mode");
            break;
        case 0x2F: // FLPACKET_CLIENT_SETMANEUVER
        {
            const FRpcSetManeuverPacket* Wire = static_cast<const FRpcSetManeuverPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            XSetManeuver Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.ObjectId = Wire->ObjectId;
            Info.Field_0x08 = Wire->Field_0x08;
            Server->SetManeuver(ClientId, Info);
            break;
        }
        case 0x30: // FLPACKET_CLIENT_DBGRELOCATE_SHIP
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x1f5,
                  "FLPACKET_CLIENT_DBGRELOCATE_SHIP encountered in Final Release mode");
            break;
        case 0x31: // FLPACKET_CLIENT_REQUEST_EVENT
        {
            const FRpcRequestEventPacket* Wire = static_cast<const FRpcRequestEventPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            const int Tail = static_cast<signed char>(Wire->Field_0x10);
            Server->RequestEvent(static_cast<int>(Wire->EventType), Wire->Arg1, Wire->Arg2, Wire->Arg3,
                                 static_cast<unsigned long>(Tail), ClientId);
            break;
        }
        case 0x32: // FLPACKET_CLIENT_REQUEST_CANCEL
        {
            const FRpcRequestCancelPacket* Wire = static_cast<const FRpcRequestCancelPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            const int Tail = static_cast<signed char>(Wire->Field_0x0c);
            Server->RequestCancel(static_cast<int>(Wire->EventType), Wire->Arg1, Wire->Arg2,
                                  static_cast<unsigned long>(Tail), ClientId);
            break;
        }
        case 0x35: // FLPACKET_CLIENT_INTERFACEITEMUSED
        {
            const FRpcInterfaceItemUsedPacket* Wire = static_cast<const FRpcInterfaceItemUsedPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->InterfaceItemUsed(ClientId, Wire->Value);
            break;
        }
        case 0x36: // FLPACKET_CLIENT_REQCOLLISIONGROUPS
        {
            const FRpcRequestCollisionGroupsPacket* Wire = static_cast<const FRpcRequestCollisionGroupsPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqCollisionGroups(Wire->Groups, ClientId);
            break;
        }
        case 0x37: // FLPACKET_CLIENT_COMMCOMPLETE
        {
            const FRpcCommCompletePacket* Wire = static_cast<const FRpcCommCompletePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->CommComplete(Wire->Field_0x00, Wire->Field_0x04, Wire->Field_0x08,
                                 static_cast<CommResult>(Wire->Field_0x0c));
            break;
        }
        case 0x38: // FLPACKET_CLIENT_REQUESTNEWCHARINFO
            Server->NewCharacterInfoReq(ClientId);
            break;
        case 0x39: // FLPACKET_CLIENT_CREATENEWCHAR
        {
            const FRpcCreateNewCharacterPacket* Wire = static_cast<const FRpcCreateNewCharacterPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            wcscpy(GStaticCreateCharacterInfo.Name, Wire->Name);
            GStaticCreateCharacterInfo.Field_0x30 = Wire->Field_0x80;
            GStaticCreateCharacterInfo.Field_0x34 = Wire->Field_0x84;
            GStaticCreateCharacterInfo.Field_0x38 = Wire->Field_0x88;
            GStaticCreateCharacterInfo.Field_0x3c = Wire->Field_0x8c;
            Server->CreateNewCharacter(GStaticCreateCharacterInfo, ClientId);
            break;
        }
        case 0x3A: // FLPACKET_CLIENT_DESTROYCHAR
        {
            const FRpcDestroyCharacterPacket* Wire = static_cast<const FRpcDestroyCharacterPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->DestroyCharacter(Wire->CharacterId, ClientId);
            break;
        }
        case 0x3B: // FLPACKET_CLIENT_REQHULLSTATUS
        {
            const FRpcRequestHullStatusPacket* Wire = static_cast<const FRpcRequestHullStatusPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqHullStatus(Wire->Value, ClientId);
            break;
        }
        case 0x3C: // FLPACKET_CLIENT_GFGOODVAPORIZED
        {
            const FRpcGfGoodVaporizedPacket* Wire = static_cast<const FRpcGfGoodVaporizedPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            SGFGoodVaporizedInfo Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.Field_0x04 = Wire->Field_0x04;
            Info.Field_0x08 = Wire->Field_0x08;
            Server->GFGoodVaporized(Info, ClientId);
            break;
        }
        case 0x3D: // FLPACKET_CLIENT_BADLANDSOBJCOLLISION
        {
            const FRpcBadlandsObjectCollisionPacket* Wire = static_cast<const FRpcBadlandsObjectCollisionPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            SSPBadLandsObjCollisionInfo Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.ObjectId = Wire->ObjectId;
            Info.Field_0x08 = Wire->Field_0x08;
            Server->SPBadLandsObjCollision(Info, ClientId);
        }
            break;
        case 0x3E: // FLPACKET_CLIENT_LAUNCHCOMPLETE
        {
            const FRpcLaunchCompletePacket* Wire = static_cast<const FRpcLaunchCompletePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->LaunchComplete(Wire->Field_0x00, Wire->Field_0x04);
            break;
        }
        case 0x3F: // FLPACKET_CLIENT_HAIL
        {
            const FRpcHailPacket* Wire = static_cast<const FRpcHailPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->Hail(Wire->Field_0x00, Wire->Field_0x04, Wire->Field_0x08);
            break;
        }
        
        case 0x40: // FLPACKET_CLIENT_REQUEST_USE_ITEM
        {
            const FRpcRequestUseItemPacket* Wire = static_cast<const FRpcRequestUseItemPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            SSPUseItem Info;
            Info.ObjectId = Wire->ObjectId;
            Info.ItemId = Wire->ItemId;
            Info.Count = Wire->Count;
            Server->SPRequestUseItem(Info, ClientId);
            break;
        }
        case 0x41: // FLPACKET_CLIENT_ABORT_MISSION
        {
            const FRpcAbortMissionPacket* Wire = static_cast<const FRpcAbortMissionPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->AbortMission(ClientId, Wire->Value);
            break;
        }
        case 0x42: // FLPACKET_CLIENT_SKIP_AUTOSAVE
            Server->CharacterSkipAutosave(ClientId);
            break;
        case 0x43: // FLPACKET_CLIENT_JUMPINCOMPLETE
        {
            const FRpcJumpInCompletePacket* Wire = static_cast<const FRpcJumpInCompletePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->JumpInComplete(Wire->Field_0x00, Wire->Field_0x04);
            break;
        }
        case 0x44: // FLPACKET_CLIENT_REQINVINCIBILITY
        {
            const FRpcRequestInvincibilityPacket* Wire = static_cast<const FRpcRequestInvincibilityPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->SPRequestInvincibility(Wire->ShipId, Wire->bInvincible,
                                           static_cast<InvincibilityReason>(Wire->Reason), ClientId);
            break;
        }
        case 0x45: // FLPACKET_CLIENT_MISSIONSAVEB
        {
            const FRpcMissionSaveBPacket* Wire = static_cast<const FRpcMissionSaveBPacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->MissionSaveB(ClientId, Wire->Value);
            break;
        }
        case 0x46: // FLPACKET_CLIENT_REQDIFFICULTYSCALE
        {
            const FRpcRequestDifficultyScalePacket* Wire = static_cast<const FRpcRequestDifficultyScalePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->ReqDifficultyScale(Wire->Value, ClientId);
            break;
        }
        case 0x47: // FLPACKET_CLIENT_RTCDONE
        {
            const FRpcRtcDonePacket* Wire = static_cast<const FRpcRtcDonePacket*>(reinterpret_cast<const FPacketBase*>(Packet));
            Server->RTCDone(ClientId, Wire->Value);
            break;
        }
        default:
            break;
        }
    }
}

void HandleClientPacket(unsigned int ClientId, CDAPacket* Packet)
{
    DispatchClientPacket(ClientId, Packet);
    reinterpret_cast<FPacketBase*>(Packet)->Release();
}
