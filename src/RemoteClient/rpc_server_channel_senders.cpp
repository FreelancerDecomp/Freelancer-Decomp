// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ClientImpl.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "rpc_iclient_args.h"        // kPlayerListKind* (0x52 PLAYERLIST)
#include "common_goods.h"            // BaseHint (SendServerLoadHint), EquipDescVector
#include "server_characterset.h" // RS-P1: CCharacterSet (SendServerCharacterInfo)
#include "server_createinfo.h"   // RS-P1: CShipCreateInfo (SendServerCreateShip)
#include "server_sysdb.h"        // FMineExplosion (SendServerExplodeAsteroidMine)
#include "common_reputation.h"   // RS-P1: Reputation::Vibe (SendServerCreateShip)
#include "common_text.h"         // RS-P1: FmtStr (SendServerCreateShip)
#include <cstdint>
#include <cstring>
#include <cwchar>

void CClientImpl::SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 2, &Packet) == 0)
    {
        static_cast<FRpcLoginResponsePacket*>(Packet)->Value = Status;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendServerSetShipArch(unsigned int ClientId, unsigned int ShipArchId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x23, &Packet) == 0)
    {
        static_cast<FRpcSetShipArchPacket*>(Packet)->Value = ShipArchId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}








// ===== SERVER 0x01..0x1f (RS-P1) =====
// Native shape of every sender: Serializer->Allocate(2, op, &Packet) (06b3b480); on
// success fill the payload from the arguments and hand the packet to this->PushToClient.
// An allocation failure sends nothing. The client id is never put in the payload.

// CHARACTERINFO record build for SendServerCharacterInfo, the first hop (RC 06b353cf..06b35906). The
// id block from an inline Costume (06b355f8..06b356e5 for +0x274 <- src+0x274,
// 06b3569a.. for +0x278 <- src+0x2a8): operator new(0x18) whose test only skips the
// zero stores (a null block is written through, reproduced on purpose), four part ids
// in the order [1],[0],[2],[3], the count, then for an unsigned non-zero count a bare
// operator new(count * 4) and a copy that is not bounded by the eight slots.
static FRpcServerCharacterIdBlock* RpcServerCharacterIdBlockFromCostume(const Costume& Source) {
    FRpcServerCharacterIdBlock* Block = static_cast<FRpcServerCharacterIdBlock*>(
        ::operator new(sizeof(FRpcServerCharacterIdBlock), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->Field_0x0c = 0;
        Block->Field_0x08 = 0;
        Block->Field_0x04 = 0;
        Block->Field_0x00 = 0;
    }
    Block->Field_0x04 = Source.PartIds[1];
    Block->Field_0x00 = Source.PartIds[0];
    Block->Field_0x08 = Source.PartIds[2];
    Block->Field_0x0c = Source.PartIds[3];
    Block->Count = static_cast<int>(Source.AccessoryCount);
    if (static_cast<unsigned int>(Block->Count) != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Block->Count) << 2, std::nothrow));
        const unsigned int* SourceIds = &Source.AccessoryIds[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            Block->Ids[Index] = SourceIds[Index];
        }
    }
    return Block;
}

// One wire record from one CCharacterSet::CharacterInfo. Retail builds it twice: hop 1
// here into a stack list, hop 2 (06b42690, = rpclocal 06d955c0) deep-copies every non-null
// value into the packet and derives +0x260 from the equipment list's size. Every hop
// re-allocates, so one hop plus that postscript yields the same record.
static void RpcServerCharacterRecordFromInfo(FRpcServerCharacterRecord* Record,
    const CCharacterSet::CharacterInfo* Source) {
    Record->Name = Source->Id;                                    // 06b354af
    Record->Field_0x200 = Source->bIsRestart;                     // 06b354bf raw byte
    Record->Field_0x201 = Source->bIsAutoSave;                    // 06b354cf raw byte
    Record->TextA = Source->Name;                                 // 06b354ef assign
    Record->TextB = Source->Description;                          // 06b35510 assign
    Record->Field_0x224 = Source->DescripStrId;
    Record->Field_0x228 = Source->TimestampLow;
    Record->Field_0x22c = Source->TimestampHigh;
    Record->Field_0x230 = Source->ShipArchetype;
    Record->Field_0x234 = static_cast<unsigned int>(Source->Money);
    Record->Field_0x238 = Source->SystemId;                       // 06b35570 <- +0x260
    Record->Field_0x244 = Source->VoiceId;                        // 06b35580 <- +0x26c
    Record->Field_0x248 = Source->Rank;                           // 06b35590 <- +0x270
    Record->Field_0x23c = Source->BaseId;                         // 06b355a0 <- +0x264
    Record->Field_0x240 = Source->LocationId;                     // 06b355b0 <- +0x268
    std::memcpy(&Record->Field_0x250, &Source->HullStatus, 4u);   // 06b355c0 integer move
    Record->Field_0x254 = static_cast<unsigned int>(Source->NumKills);             // <- +0x238
    Record->Field_0x258 = static_cast<unsigned int>(Source->NumMissionFailures);   // <- +0x240
    Record->Field_0x25c = static_cast<unsigned int>(Source->NumMissionSuccesses);  // <- +0x23c
    Record->BlockA = RpcServerCharacterIdBlockFromCostume(Source->ComCostume);
    Record->BlockB = RpcServerCharacterIdBlockFromCostume(Source->BaseCostume);
    // 06b357d4..06b3588c: one 0x18 record per EquipDesc, no filter.
    for (std::list<EquipDesc>::const_iterator It = Source->Equipment.Items.begin();
         It != Source->Equipment.Items.end(); ++It) {
        // 06b357ea: the new-expression test only skips the zero stores; a null record
        // is written through, reproduced on purpose.
        FRpcServerEquipRecord* E = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (E != nullptr) {
            E->ArchId = 0;
            E->Count = 0;
            E->Health = 0.0f;
            E->Id = 0;
            E->HardpointSize = 0;
            E->Hardpoint = nullptr;
            E->bEquipped = 0;
            E->Field_0x0f = 0;
        }
        E->ArchId = It->ArchId;                                   // 06b35816
        E->Count = It->Count;                                     // 06b3581b
        RpcServerX87CopyFloat(&E->Health, &It->Health);           // 06b3581e FLD/FSTP
        E->Id = It->Id;                                           // 06b35828
        E->bEquipped = It->bEquipped;                             // 06b3582f raw byte
        // 06b35832 / 06b35835: EquipDesc +0x18 (bMission) copied as a raw byte.
        std::memcpy(&E->Field_0x0f, &It->bMission, 1u);
        // 06b35842..06b3586d: strlen + 1 bytes allocated (bare), strcpy, and the
        // size stored as strlen + 1 truncated to a word.
        const unsigned int Length = static_cast<unsigned int>(std::strlen(It->Hardpoint)) + 1u;
        E->Hardpoint = static_cast<char*>(::operator new(Length, std::nothrow));
        const char* Text = It->Hardpoint;
        char* Out = E->Hardpoint;
        char Value;
        do {                                                      // 06b35857..06b3585f
            Value = *Text++;
            *Out++ = Value;
        } while (Value != '\0');
        E->HardpointSize = static_cast<unsigned short>(Length);
        Record->Equipment.PushBack(E);                            // 06b3587c
    }
    // Hop 2's postscript: +0x260 = low byte of the equipment list size, or 0.
    Record->Field_0x260 = (Record->Equipment.Records != nullptr)
        ? static_cast<unsigned char>(Record->Equipment.Records->size() & 0xffu) : 0u;
    // 06b358e2..06b35906: list<CollisionGroupDesc>::operator= from src+0x254.
    Record->CollisionGroups = Source->CollisionGroups;
}

// RC 06b35350, IClient +0x34, RET 8: CHARACTERINFO (= rpclocal 06d89890). Characters is
// the caller's CCharacterSet. Its NULL-terminated MetaList is walked (06b353b0..06b353c9);
// every record is converted and appended to the packet's list (hops 1 and 2 folded).
// A null entry jumps to the push-back tail (06b353d4 -> 06b3590c) and appends the
// PREVIOUS entry's copy again, so hop 2 ships that record twice; the port converts the
// previous source again (same bytes). Before the first record that copy is an
// uninitialised stack slot; the port reads it as null, which hop 2 skips: a
// nondeterministic retail read made deterministic. Then +0x20 = SaveFailureCount
// (06b35968) and PushToClient (06b35979). The stack list teardown follows the send.
void CClientImpl::SendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 3, &Packet) != 0) return;
    FRpcServerCharacterInfoPacket* Typed = static_cast<FRpcServerCharacterInfoPacket*>(Packet);
    const CCharacterSet* Source = &Characters;
    const CCharacterSet::CharacterInfo* Previous = nullptr;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Source->Characters.get_head();
         Node != nullptr; Node = Node->Next) {
        const CCharacterSet::CharacterInfo* Info = Node->Data;
        if (Info == nullptr) {
            Info = Previous;
            if (Info == nullptr) continue;
        }
        // 06b353df operator new(0x27c); the test (06b353ed / 06b353f4) only skips the
        // ctor, the conversion runs on the result either way: reproduced on purpose.
        FRpcServerCharacterRecord* Record = new (std::nothrow) FRpcServerCharacterRecord();
        RpcServerCharacterRecordFromInfo(Record, Info);
        Typed->Records.push_back(Record);
        Previous = Info;
    }
    Typed->SaveFailureCount = Source->SaveFailureCount;
    PushToClient(ClientId, Packet->AsRaw());
}

// RC 06b34660, IClient +0xfc, RET 0xc: DAMAGEOBJECT (= rpclocal 06d88c10). +0x20 = ObjectId,
// then 06b3e690(packet, &Damage): DamageList::operator= of the caller's DamageList into the
// packet's (list overwrite/insert/erase, then bDestroyed, Cause, InflictorId,
// InflictorOwner).
void CClientImpl::SendServerDamageObject(unsigned int ClientId, unsigned int ObjectId, const DamageList& Damage) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 5, &Packet) == 0) {
        FRpcServerDamageObjectPacket* Typed = static_cast<FRpcServerDamageObjectPacket*>(Packet);
        Typed->ObjectId = ObjectId;                                            // 06b34685
        Typed->Damage = Damage;                                                // 06b3468d
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35af0, IClient +0x38, RET 8: CHARSELECTVERIFIED. The double is copied as
// two integer dwords (06b35b16..06b35b27), not through the FPU.
void CClientImpl::SendServerCharSelectVerified(unsigned int ClientId, const FServerCharSelectVerifiedInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 8, &Packet) == 0) {
        FRpcServerCharSelectVerifiedPacket* Typed =
            static_cast<FRpcServerCharSelectVerifiedPacket*>(Packet);
        Typed->RepInstance = static_cast<unsigned int>(Info.VibeHandle);
        std::memcpy(&Typed->Time, &Info.ServerTime, sizeof(Typed->Time));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34120, IClient +0x70, RET 0xc: SETSTARTROOM, both values by value.
void CClientImpl::SendServerSetStartRoom(unsigned int ClientId, unsigned int BaseId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xd, &Packet) == 0) {
        FRpcServerSetStartRoomPacket* Typed = static_cast<FRpcServerSetStartRoomPacket*>(Packet);
        Typed->Field_0x00 = BaseId;
        Typed->Field_0x04 = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34c00, IClient +0xdc, RET 8: DESTROYOBJECT from {dword, byte}.
void CClientImpl::SendServerDestroyObject(unsigned int ClientId, const FServerDestroyObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 6, &Packet) == 0) {
        FRpcServerDestroyObjectPacket* Typed = static_cast<FRpcServerDestroyObjectPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->Field_0x04 = Info.Field_0x04;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35230, IClient +0xe0, RET 8: ACTIVATEOBJECT; native store order +0x20,
// +0x28, then the raw byte +0x24.
void CClientImpl::SendServerActivateObject(unsigned int ClientId, const FPayloadServerActivateObject& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xa, &Packet) == 0) {
        FRpcServerActivateObjectPacket* Typed = static_cast<FRpcServerActivateObjectPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->Field_0x08 = Info.Field_0x08;
        Typed->bActivate = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36450, IClient +0xec, RET 8: LAND; native store order [0], [8], [4].
void CClientImpl::SendServerLand(unsigned int ClientId, const FServerLandInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xb, &Packet) == 0) {
        FRpcServerLandPacket* Typed = static_cast<FRpcServerLandPacket*>(Packet);
        Typed->Field_0x00 = Info.ShipId;
        Typed->Field_0x08 = Info.BaseId;
        Typed->Field_0x04 = Info.DockId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35b40, IClient +0xf0, RET 8: LAUNCH. Argument struct {3 dwords,
// Orientation +0x0c, Position +0x1c}; both blocks move as integer dwords
// (06b3a160 / 06b39ec0), Position first.
void CClientImpl::SendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 7, &Packet) == 0) {
        FRpcServerLaunchPacket* Typed = static_cast<FRpcServerLaunchPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->Field_0x04 = Info.Field_0x04;
        Typed->Field_0x08 = Info.Field_0x08;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// GF list senders 0x0e..0x1f. Every body: Allocate(2, op, &Packet) (06b3b480);
// on success store the arguments, then this->PushToClient(ClientId, Packet).

// RC 06b34170, IClient +0x74, RET 0xc: GFDESTROYCHARACTER 0x15; +0x20 = RoomId, +0x24 = Key.
void CClientImpl::SendServerGFDestroyCharacter(unsigned int ClientId, unsigned int RoomId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x15, &Packet) == 0) {
        FRpcServerGfDestroyCharacterPacket* Typed =
            static_cast<FRpcServerGfDestroyCharacterPacket*>(Packet);
        Typed->Field_0x00 = RoomId;
        Typed->Field_0x04 = Key;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34210, IClient +0x7c, RET 8: GFCOMPLETECHARLIST 0x0f; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteCharList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xf, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteCharListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34250, IClient +0x80, RET 0xc: GFSCRIPTBEHAVIOR 0x1a; set helper 06b3d130
// (Data = new(Size), copy from Data, Size = Size).
void CClientImpl::SendServerGFScriptBehavior(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1a, &Packet) == 0) {
        static_cast<FRpcServerGfScriptBehaviorPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b342a0, IClient +0x84, RET 0xc: GFDESTROYSCRIPTBEHAVIOR 0x17; +0x20 = RoomId,
// +0x24 = BehaviorId.
void CClientImpl::SendServerGFDestroyScriptBehavior(unsigned int ClientId, unsigned int RoomId, unsigned int BehaviorId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x17, &Packet) == 0) {
        FRpcServerGfDestroyScriptBehaviorPacket* Typed =
            static_cast<FRpcServerGfDestroyScriptBehaviorPacket*>(Packet);
        Typed->Field_0x00 = RoomId;
        Typed->Field_0x04 = BehaviorId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b342f0, IClient +0x88, RET 8: GFCOMPLETESCRIPTBEHAVIORLIST 0x11; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteScriptBehaviorList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x11, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteScriptBehaviorListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34330, IClient +0x8c, RET 0xc: GFUPDATEAMBIENTSCRIPT 0x1f; set helper 06b3d490
// (Data +0x20 = new(Size), copy from Data, Size +0x24 = Size).
void CClientImpl::SendServerGFUpdateAmbientScript(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1f, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateAmbientScriptPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34380, IClient +0x90, RET 0xc: GFDESTROYAMBIENTSCRIPT 0x19; native store
// order +0x24 = Key, then +0x20 = RoomId.
void CClientImpl::SendServerGFDestroyAmbientScript(unsigned int ClientId, unsigned int RoomId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x19, &Packet) == 0) {
        FRpcServerGfDestroyAmbientScriptPacket* Typed =
            static_cast<FRpcServerGfDestroyAmbientScriptPacket*>(Packet);
        Typed->Field_0x04 = Key;
        Typed->Field_0x00 = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b343d0, IClient +0x94, RET 8: GFCOMPLETEAMBIENTSCRIPTLIST 0x13; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteAmbientScriptList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x13, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteAmbientScriptListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34410, IClient +0x98, RET 0xc: GFDESTROYMISSIONCOMPUTER 0x16; native store
// order +0x24 = Key, then +0x20 = BaseId.
void CClientImpl::SendServerGFDestroyMissionComputer(unsigned int ClientId, unsigned int BaseId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x16, &Packet) == 0) {
        FRpcServerGfDestroyMissionComputerPacket* Typed =
            static_cast<FRpcServerGfDestroyMissionComputerPacket*>(Packet);
        Typed->Field_0x04 = Key;
        Typed->Field_0x00 = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34460, IClient +0x9c, RET 0xc: GFUPDATEMISSIONCOMPUTER 0x1d; set helper 06b3d9b0.
void CClientImpl::SendServerGFUpdateMissionComputer(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1d, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateMissionComputerPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b344b0, IClient +0xa0, RET 8: GFCOMPLETEMISSIONCOMPUTERLIST 0x10; +0x20 = BaseId.
void CClientImpl::SendServerGFCompleteMissionComputerList(unsigned int ClientId, unsigned int BaseId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x10, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteMissionComputerListPacket*>(Packet)->Value = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34580, IClient +0xac, RET 0xc: GFDESTROYNEWSBROADCAST 0x14; +0x20 = BaseId, +0x24 = Key.
void CClientImpl::SendServerGFDestroyNewsBroadcast(unsigned int ClientId, unsigned int BaseId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x14, &Packet) == 0) {
        FRpcServerGfDestroyNewsBroadcastPacket* Typed =
            static_cast<FRpcServerGfDestroyNewsBroadcastPacket*>(Packet);
        Typed->Field_0x00 = BaseId;
        Typed->Field_0x04 = Key;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b345d0, IClient +0xb0, RET 0xc: GFUPDATENEWSBROADCAST 0x1e; set helper 06b3e200.
void CClientImpl::SendServerGFUpdateNewsBroadcast(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1e, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateNewsBroadcastPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34620, IClient +0xb4, RET 8: GFCOMPLETENEWSBROADCASTLIST 0x0e; +0x20 = BaseId.
void CClientImpl::SendServerGFCompleteNewsBroadcastList(unsigned int ClientId, unsigned int BaseId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xe, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteNewsBroadcastListPacket*>(Packet)->Value = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b3f970 (thiscall on the CREATESHIP packet, RET 4): a null name changes nothing;
// otherwise +0xd8 = wcslen, +0xdc = operator new(2 * length + 2) (untested), wcscpy.
static void RpcServerCreateShipSetDisplayName(FRpcServerCreateShipPacket* Packet,
    const wchar_t* Name) {
    if (Name == nullptr) return;
    Packet->DisplayNameLength = static_cast<int>(std::wcslen(Name));
    Packet->DisplayName = static_cast<wchar_t*>(::operator new(
        static_cast<unsigned int>(Packet->DisplayNameLength) * 2u + 2u, std::nothrow));
    std::wcscpy(Packet->DisplayName, Name);
}

// RC 06b34ed0, IClient +0xbc, RET 8: CREATESHIP; Info is the CShipCreateInfo.
// Every store is an integer move. In MP the vibe's display name ships when it is non-empty,
// else its two FmtStr names flattened (each sized like RC 06b347e0, operator new'd
// untested, flatten's return ignored); then the vibe's affiliation and its attitude
// towards Info's second group. The two FmtStr locals die B then A (EH funclets).
void CClientImpl::SendServerCreateShip(unsigned int ClientId, const CShipCreateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 4, &Packet) != 0) return;                     // 06b34efb
    FRpcServerCreateShipPacket* Typed = static_cast<FRpcServerCreateShipPacket*>(Packet);
    RpcServerCreateObjectFill(&Info, &Typed->Object);                        // 06b34f13
    Typed->bUsingTradelane = Info.bUsingTradelane;                           // 06b34f1c
    Typed->OwnerPlayer = Info.OwnerPlayer;                                   // 06b34f2c
    Typed->Field_0xc0 = static_cast<unsigned short>(Info.Field_0xb0 & 0xffffu);        // 06b34f38
    Typed->ReputationRank = static_cast<unsigned char>(Info.ReputationRank & 0xffu);   // 06b34f4a
    std::memcpy(&Typed->Throttle, &Info.Throttle, 4u);                       // 06b34f5a
    std::memcpy(&Typed->Velocity, &Info.Velocity, 12u);                      // 06b34f6a..
    std::memcpy(&Typed->AngularVelocity, &Info.AngularVelocity, 12u);        // ..06b34fc4
    Typed->bMarkedBy = Info.bMarkedBy;                                       // 06b34fca
    Typed->Field_0xc4 = static_cast<unsigned short>(Info.Field_0xd8 & 0xffffu);        // 06b34fda
    if (!SinglePlayer()) {                                                   // 06b34fef
        FmtStr NameA;                                                        // 06b35000
        FmtStr NameB;                                                        // 06b35044
        const wchar_t* DisplayName = nullptr;                                // 06b350a0
        Reputation::Vibe::GetName(Typed->Object.VibeGroupId, NameA, NameB, DisplayName);
        if (DisplayName != nullptr && std::wcslen(DisplayName) != 0) {      // 06b350b1 / 06b350bf
            RpcServerCreateShipSetDisplayName(Typed, DisplayName);           // 06b350cc
        } else {
            Typed->NameASize = RpcGetFlattenedFmtStrSize(NameA);             // 06b350d6..06b35120
            Typed->NameA = static_cast<unsigned char*>(
                ::operator new(Typed->NameASize, std::nothrow));             // 06b35123
            NameA.flatten(Typed->NameA, Typed->NameASize);                   // 06b35137
            Typed->NameBSize = RpcGetFlattenedFmtStrSize(NameB);             // 06b3513d..06b35184
            Typed->NameB = static_cast<unsigned char*>(
                ::operator new(Typed->NameBSize, std::nothrow));             // 06b35187
            NameB.flatten(Typed->NameB, Typed->NameBSize);                   // 06b3519b
        }
        Reputation::Vibe::GetAffiliation(Typed->Object.VibeGroupId, Typed->Affiliation,
            false);                                                          // 06b351ba
        Reputation::Vibe::AttitudeTowards(Typed->Object.VibeGroupId, Info.VibeOtherGroupId,
            Typed->Attitude);                                                // 06b351d6
    }
    PushToClient(ClientId, Packet->AsRaw());                                 // 06b3520c
}

// ===== end SERVER 0x01..0x1f (RS-P1) =====

// ===== SERVER 0x20..0x3f (RS-P2) =====
#include "server_createinfo.h" // FServerCreateProjectileInfo / FServerCreateGuidedInfo (0x2d, 0x37)
#include <cwchar> // wcslen (0x29)
#include <list>
#include <new>
// Native senders: allocate through the serializer at this+0x08 (06b3b480), fill the
// typed packet, hand it to PushToClient, which takes ownership. An allocation failure
// sends nothing. Where retail dereferences the first argument (the client id passed BY
// REFERENCE) the body says so.

// RC 06b33ef0, IClient +0x60, RET 8: SETEQUIPMENT. Equipment is an EquipDescVector. The
// stack handle is constructed only after a successful allocation (06b33f20) and is
// destroyed by 06b3a450 on every exit, the EH funclet included.
void CClientImpl::SendServerSetEquipment(unsigned int ClientId, const EquipDescVector& Equipment) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x24, &Packet) == 0) {
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Equipment, &Handle, true);
        static_cast<FRpcServerSetEquipmentPacket*>(Packet)->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b33f80, IClient +0x64, RET 8: SETCARGO. Same body as SendServerSetEquipment, also
// with bAll = 1 (06b33fb8); only the packet setter's address differs (06b3c770).
void CClientImpl::SendServerSetCargo(unsigned int ClientId, const EquipDescVector& Cargo) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x25, &Packet) == 0) {
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Cargo, &Handle, true);
        static_cast<FRpcServerSetCargoPacket*>(Packet)->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34010, IClient +0x68, RET 0xc: SETADDITEM 0x2e. Item is an EquipDesc. Native
// order: +0x20 = ArchId, +0x30 = MOVZX Id, +0x34 = Health through FLD/FSTP, +0x24 =
// Count, the two raw bytes +0x38 / +0x39, +0x2c = AddResult; then, unless is_internal(),
// 06b3cd50(hardpoint, strlen(hardpoint)).
void CClientImpl::SendServerSetAddItem(unsigned int ClientId, int AddResult, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2e, &Packet) == 0) {
        FRpcServerSetAddItemPacket* Typed = static_cast<FRpcServerSetAddItemPacket*>(Packet);
        Typed->ArchId = Item.ArchId;
        Typed->Id = Item.Id;
        RpcServerX87CopyFloat(&Typed->Health, &Item.Health);
        Typed->Count = Item.Count;
        Typed->bEquipped = Item.bEquipped;
        std::memcpy(&Typed->bMission, &Item.bMission, 1u); // raw byte, not a bool test
        Typed->Field_0x0c = static_cast<unsigned int>(AddResult);
        if (!Item.is_internal()) {
            Typed->SetHardpoint(Item.Hardpoint,
                static_cast<unsigned int>(std::strlen(Item.Hardpoint)));
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36700, IClient +0x124, RET 8: SETMISSIONOBJECTIVESTATE 0x33. Info is a
// FServerMissionObjectiveStateInfo: +0x20 = [Info], +0x24 = MOVZX byte [Info+4], word +0x28
// = word [Info+8]. Client id by reference: PushToClient gets the referenced id (06b36748).
void CClientImpl::SendServerSetMissionObjectiveState(const unsigned int& ClientId,
                                                     const FServerMissionObjectiveStateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x33, &Packet) == 0) {
        FRpcServerSetMissionObjectiveStatePacket* Typed =
            static_cast<FRpcServerSetMissionObjectiveStatePacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->Field_0x04 = Info.Field_0x04;
        Typed->Field_0x08 = static_cast<unsigned short>(Info.Field_0x08);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36500, IClient +0xf8, RET 8: REQUESTSPACESCRIPT 0x32. Info is a
// FServerRequestSpaceScriptInfo: +0x20 = [Info], +0x28..+0x30 = [Info+0x108..+0x110];
// [Info+0x114] == 1 -> Mode = 1, == 2 -> Mode = 0, else the ctor's 1 stays; then
// 06b44240(Info+4, strlen(Info+4)). PushToClient(ClientId by value).
void CClientImpl::SendServerRequestSpaceScript(unsigned int ClientId, const FServerRequestSpaceScriptInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x32, &Packet) == 0) {
        FRpcServerRequestSpaceScriptPacket* Typed =
            static_cast<FRpcServerRequestSpaceScriptPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x000;
        Typed->Position[0] = Info.Field_0x108;
        Typed->Position[1] = Info.Field_0x10c;
        Typed->Position[2] = Info.Field_0x110;
        if (Info.Field_0x114 == 1u) {
            Typed->Mode = 1;
        } else if (Info.Field_0x114 == 2u) {
            Typed->Mode = 0;
        }
        Typed->SetName(Info.Name, static_cast<unsigned int>(std::strlen(Info.Name)));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36850, IClient +0x11c, RET 8: REPLACEMISSIONOBJECTIVE 0x34. Info is a
// FServerReplaceMissionObjectiveInfo: +0x20 = [Info], +0x24 = MOVZX byte [Info+0x20];
// TextSize = the flattened size of the FmtStr at Info+4, TextData = operator new(size)
// with no null test, FmtStr::flatten into it. Client id by reference: PushToClient gets
// the referenced id.
void CClientImpl::SendServerReplaceMissionObjective(const unsigned int& ClientId,
                                                    const FServerReplaceMissionObjectiveInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x34, &Packet) == 0) {
        FRpcServerReplaceMissionObjectivePacket* Typed =
            static_cast<FRpcServerReplaceMissionObjectivePacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->State = Info.State;
        Typed->TextSize = RpcGetFlattenedFmtStrSize(Info.Text);
        Typed->TextData = static_cast<unsigned char*>(::operator new(Typed->TextSize, std::nothrow));
        Info.Text.flatten(Typed->TextData, Typed->TextSize);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36ae0, IClient +0x120, RET 8: SETMISSIONOBJECTIVES 0x35. Info is a
// FServerSetMissionObjectivesInfo: +0x20 = [Info], +0x24 = MOVZX byte [Info+0x3c]; TextA /
// TextB = the flattened FmtStrs at Info+0x0c / Info+0x24 (size, operator new, flatten);
// then per entry i < [Info+8]: record = operator new(0xc) (Size/Data zeroed when it is
// non-null), State = entry byte +0x18, the entry FmtStr flattened into the record, and
// 06b44e20 appends it (Count++). Client id by reference: PushToClient gets the referenced id.
void CClientImpl::SendServerSetMissionObjectives(const unsigned int& ClientId,
                                                 const FServerSetMissionObjectivesInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x35, &Packet) == 0) {
        FRpcServerSetMissionObjectivesPacket* Typed =
            static_cast<FRpcServerSetMissionObjectivesPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->State = Info.Field_0x3c;
        Typed->TextA.Size = RpcGetFlattenedFmtStrSize(Info.TextA);
        Typed->TextA.Data = static_cast<unsigned char*>(::operator new(Typed->TextA.Size, std::nothrow));
        Info.TextA.flatten(Typed->TextA.Data, Typed->TextA.Size);
        Typed->TextB.Size = RpcGetFlattenedFmtStrSize(Info.TextB);
        Typed->TextB.Data = static_cast<unsigned char*>(::operator new(Typed->TextB.Size, std::nothrow));
        Info.TextB.flatten(Typed->TextB.Data, Typed->TextB.Size);
        for (unsigned int Index = 0; Index < Info.Count; ++Index) {
            FRpcServerMissionObjectiveRecord* Record = static_cast<FRpcServerMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcServerMissionObjectiveRecord), std::nothrow));
            if (Record != nullptr) {
                Record->Size = 0;
                Record->Data = nullptr;
            }
            const FServerMissionObjectiveEntry& Entry = Info.Entries[Index];
            Record->State = static_cast<unsigned char>(Entry.State);
            Record->Size = RpcGetFlattenedFmtStrSize(Entry.Text);
            Record->Data = static_cast<unsigned char*>(::operator new(Record->Size, std::nothrow));
            Entry.Text.flatten(Record->Data, Record->Size);
            Typed->PushObjective(Record);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b340c0, IClient +0x6c, RET 0x10: SETREMOVEITEM. +0x28 = RemoveResult, +0x20 =
// ItemId (the low word zero-extended), +0x24 = Count.
void CClientImpl::SendServerSetRemoveItem(unsigned int ClientId, int RemoveResult, unsigned short ItemId, int Count) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2f, &Packet) == 0) {
        FRpcServerSetRemoveItemPacket* Typed = static_cast<FRpcServerSetRemoveItemPacket*>(Packet);
        Typed->Field_0x08 = static_cast<unsigned int>(RemoveResult);
        Typed->Field_0x00 = ItemId;
        Typed->Field_0x04 = static_cast<unsigned int>(Count);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b341c0, IClient +0x78, RET 0xc: GFUPDATECHAR 0x26; set helper 06b3cdd0 (Data =
// new(Size), copy from Data, Size = Size). Same body as SendServerGFScriptBehavior except
// the opcode.
void CClientImpl::SendServerGFUpdateChar(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x26, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateCharPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b344f0, IClient +0xa4, RET 0xc: GFMISSIONVENDORACCEPTANCE 0x20; set helper
// 06b3dcc0. Same body as SendServerGFScriptBehavior except the opcode.
void CClientImpl::SendServerGFMissionVendorAcceptance(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x20, &Packet) == 0) {
        static_cast<FRpcServerGfMissionVendorAcceptancePacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35e20, IClient +0xd0, RET 8: EXPLODEASTEROIDMINE 0x31. Info is the four-dword
// FMineExplosion: +0x20..+0x2c = its dwords 0..3, integer moves.
void CClientImpl::SendServerExplodeAsteroidMine(unsigned int ClientId, const FMineExplosion& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x31, &Packet) == 0) {
        FRpcServerExplodeAsteroidMinePacket* Typed =
            static_cast<FRpcServerExplodeAsteroidMinePacket*>(Packet);
        Typed->Field_0x00 = Info.MineId;
        std::memcpy(&Typed->Position.x, &Info.Offset.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Offset.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Offset.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36370, IClient +0xe8, RET 8: SYSTEM_SWITCH_IN 0x22. Info is a
// FPayloadServerSystemSwitchIn. Retail first stages Position/Orientation in stack
// temps (initialised to zero / identity before the allocation) and copies them with
// 06b3a160 / 06b39ec0; every move is an integer one, so the direct copies are the same.
void CClientImpl::SendServerSystemSwitchIn(unsigned int ClientId, const FPayloadServerSystemSwitchIn& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x22, &Packet) == 0) {
        FRpcServerSystemSwitchInPacket* Typed = static_cast<FRpcServerSystemSwitchInPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->Field_0x04 = Info.Field_0x04;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b346b0, IClient +0x100, RET 8: ITEMTRACTORED 0x38. Info is a
// FPayloadServerItemTractored: two dwords, two words.
void CClientImpl::SendServerItemTractored(unsigned int ClientId, const FPayloadServerItemTractored& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x38, &Packet) == 0) {
        FRpcServerItemTractoredPacket* Typed = static_cast<FRpcServerItemTractoredPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        Typed->Field_0x04 = Info.Field_0x04;
        Typed->Field_0x08 = Info.Field_0x08;
        Typed->Field_0x0a = Info.Field_0x0a;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37aa0, IClient +0x150, RET 8: REPAIROBJECT 0x3c. Info is a
// FServerRepairObjectInfo; native store order +0x24 = MOVZX word [Info+4], +0x20 =
// [Info], +0x28 = [Info+8].
void CClientImpl::SendServerRepairObject(unsigned int ClientId, const FServerRepairObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3c, &Packet) == 0) {
        FRpcServerRepairObjectPacket* Typed = static_cast<FRpcServerRepairObjectPacket*>(Packet);
        Typed->EquipId = Info.Field_0x04;
        Typed->ObjectId = Info.Field_0x00;
        std::memcpy(&Typed->Points, &Info.Field_0x08, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36320, IClient +0xe4, RET 8: SYSTEM_SWITCH_OUT. Info is two dwords.
void CClientImpl::SendServerSystemSwitchOut(unsigned int ClientId, const FServerSystemSwitchOutInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x21, &Packet) == 0) {
        FRpcServerSystemSwitchOutPacket* Typed = static_cast<FRpcServerSystemSwitchOutPacket*>(Packet);
        Typed->Field_0x00 = Info.ShipId;
        Typed->Field_0x04 = Info.GateId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35bf0, IClient +0xf4, RET 0xc: REQUESTCREATESHIPRESP. +0x24 = ShipId, then the
// low byte of bSuccess into +0x20 (raw).
void CClientImpl::SendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x27, &Packet) == 0) {
        FRpcServerRequestCreateShipRespPacket* Typed =
            static_cast<FRpcServerRequestCreateShipRespPacket*>(Packet);
        Typed->Field_0x04 = ShipId;
        Typed->Field_0x00 = static_cast<unsigned char>(bSuccess);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34a30, IClient +0x10c, RET 8: ADJUSTATTITUDE. Info is {dword, float};
// the float moves through the x87 stack (06b34a56 / 06b34a61).
void CClientImpl::SendServerAdjustAttitude(unsigned int ClientId, const FPayloadServerAdjustAttitude& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2a, &Packet) == 0) {
        FRpcServerAdjustAttitudePacket* Typed = static_cast<FRpcServerAdjustAttitudePacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x00;
        RpcServerX87CopyFloat(&Typed->Field_0x04, &Info.Field_0x04);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b361f0, IClient +0xc8, RET 8: CREATEGUIDED 0x37. Info is a FServerCreateGuidedInfo;
// every field is an integer dword/word copy (no x87), in retail order. Client id by
// reference: PushToClient gets the referenced id (06b362ab).
void CClientImpl::SendServerCreateGuided(const unsigned int& ClientId, const FServerCreateGuidedInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x37, &Packet) == 0) {
        FRpcServerCreateGuidedPacket* Typed = static_cast<FRpcServerCreateGuidedPacket*>(Packet);
        Typed->Field_0x00 = Info.Field_0x2c;
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        Typed->TargetId = Info.TargetId;
        Typed->TargetSubObjId = Info.TargetSubObjId;
        Typed->ObjId = Info.ObjId;
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orientation.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orientation.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orientation.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orientation.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35d30, IClient +0xcc, RET 8: CREATECOUNTER 0x2d. Info is a
// FServerCreateProjectileInfo; integer copies in retail order (Position before
// Orientation). Client id by reference: PushToClient gets the referenced id (06b35e11).
void CClientImpl::SendServerCreateCounter(const unsigned int& ClientId, const FServerCreateProjectileInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2d, &Packet) == 0) {
        FRpcServerCreateCounterPacket* Typed = static_cast<FRpcServerCreateCounterPacket*>(Packet);
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->ObjId = Info.ObjId;
        Typed->Field_0x08 = Info.Field_0x44;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orientation.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orientation.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orientation.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orientation.z, 4u);
        std::memcpy(&Typed->Velocity.x, &Info.Velocity.x, 4u);
        std::memcpy(&Typed->Velocity.y, &Info.Velocity.y, 4u);
        std::memcpy(&Typed->Velocity.z, &Info.Velocity.z, 4u);
        std::memcpy(&Typed->Field_0x3c.x, &Info.Field_0x38.x, 4u);
        std::memcpy(&Typed->Field_0x3c.y, &Info.Field_0x38.y, 4u);
        std::memcpy(&Typed->Field_0x3c.z, &Info.Field_0x38.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36cd0, IClient +0x134, RET 8: SETCASH. The client id is passed by
// reference: PushToClient gets the referenced id (06b36cff).
void CClientImpl::SendServerSetCash(const unsigned int& ClientId, int Cash) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x30, &Packet) == 0) {
        static_cast<FRpcServerSetCashPacket*>(Packet)->Value = static_cast<unsigned int>(Cash);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d10, IClient +0x138, RET 8: SETNUMKILLS. Client id by reference (06b36d3f).
void CClientImpl::SendServerSetNumKills(const unsigned int& ClientId, int NumKills) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3e, &Packet) == 0) {
        static_cast<FRpcServerSetNumKillsPacket*>(Packet)->Value = static_cast<unsigned int>(NumKills);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d50, IClient +0x13c, RET 8: SETMISSIONSUCCESSES. Client id by reference
// (06b36d7f).
void CClientImpl::SendServerSetMissionSuccesses(const unsigned int& ClientId, int NumSuccesses) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3f, &Packet) == 0) {
        static_cast<FRpcServerSetMissionSuccessesPacket*>(Packet)->Value =
            static_cast<unsigned int>(NumSuccesses);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35ef0, IClient +0x1b4, RET 0xc: SCANNOTIFY. +0x20 = ScannerId, +0x24 = GoodId.
void CClientImpl::SendServerScanNotify(unsigned int ClientId, unsigned int ScannerId, unsigned int GoodId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x39, &Packet) == 0) {
        FRpcServerScanNotifyPacket* Typed = static_cast<FRpcServerScanNotifyPacket*>(Packet);
        Typed->Field_0x00 = ScannerId;
        Typed->Field_0x04 = GoodId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b365a0, IClient +0xc0, RET 8: CREATELOOT 0x28. Info is a FServerCreateLootRequest:
// +0x20 = [Info], +0x24 = byte [Info+4]; then per item (byte count [Info+0x10], stride 0x48
// from [Info+0xc]) a stack record filled by integer moves and appended (06b44b40). The
// record's Orientation takes the item's w, y, x, z in that order: x and y are swapped
// (06b3669c..06b366b4), reproduced on purpose. PushToClient(ClientId by value).
void CClientImpl::SendServerCreateLoot(unsigned int ClientId, const FServerCreateLootRequest& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x28, &Packet) == 0) {
        const FServerCreateLootRequest* Request = &Info;
        FRpcServerCreateLootPacket* Typed = static_cast<FRpcServerCreateLootPacket*>(Packet);
        Typed->OwnerId = Request->OwnerId;
        Typed->Kind = static_cast<unsigned char>(Request->Kind);
        const FServerLootDesc* Desc = Request->Items;
        FRpcServerLootRecord Record;
        std::memset(&Record, 0, sizeof(Record));
        Record.Orientation.w = 1.0f;
        for (unsigned int Left = Request->Count; Left != 0; --Left, ++Desc) {
            Record.LootId = Desc->LootId;
            Record.ContentsNickname = Desc->ContentsNickname;
            std::memcpy(&Record.ContentsHitPts, &Desc->ContentsHitPts, 4u);
            Record.Units = static_cast<unsigned short>(Desc->Units);
            Record.ContainerNickname = Desc->ContainerNickname;
            std::memcpy(&Record.HitPts, &Desc->HitPts, 4u);
            Record.NameOverride = Desc->NameOverride;
            std::memcpy(&Record.Position, &Desc->Position, sizeof(Record.Position));
            std::memcpy(&Record.Velocity, &Desc->Velocity, sizeof(Record.Velocity));
            std::memcpy(&Record.Orientation.w, &Desc->Orientation.w, 4u);
            std::memcpy(&Record.Orientation.x, &Desc->Orientation.y, 4u);
            std::memcpy(&Record.Orientation.y, &Desc->Orientation.x, 4u);
            std::memcpy(&Record.Orientation.z, &Desc->Orientation.z, 4u);
            Record.bMarkedBy = Desc->bMarkedBy;
            Record.bTemporary = Desc->bTemporary;
            Typed->AddLoot(Record);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35c40, IClient +0xc4, RET 8: CREATEMINE 0x2c. Info is a FServerCreateProjectileInfo;
// every field an integer move: +0x20..+0x28 = info +0x00..+0x08, word +0x2c = info
// +0x0c, Position = info +0x10, Orientation = info +0x1c, Velocity = info +0x2c,
// Field_0x38 = info +0x38, +0x64 = info +0x44. PushToClient(ClientId by value).
void CClientImpl::SendServerCreateMine(unsigned int ClientId, const FServerCreateProjectileInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2c, &Packet) == 0) {
        FRpcServerCreateMinePacket* Typed = static_cast<FRpcServerCreateMinePacket*>(Packet);
        Typed->ObjId = Info.ObjId;
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        std::memcpy(&Typed->Velocity, &Info.Velocity, sizeof(Typed->Velocity));
        std::memcpy(&Typed->Field_0x38, &Info.Field_0x38, sizeof(Typed->Field_0x38));
        Typed->Field_0x44 = Info.Field_0x44;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34830, IClient +0x108, RET 8: SETREPUTATION 0x29. Info is a
// FServerSetReputationInfo: Target = [Info], Affiliation = [Info+8]; NameA / NameB = the
// flattened FmtStrs at Info+0x18 / Info+0x30 (size, operator new with no null test,
// flatten); 06b3f670(Info+0x48, wcslen); a stack relation list (operator new(8) per
// relation, GroupId and Feeling as integers) deep-copied into the packet (06b3f6c0);
// Attitude = [Info+4] as an integer; PushToClient(ClientId by value); then the stack list's
// relations and nodes are freed.
void CClientImpl::SendServerSetReputation(unsigned int ClientId, const FServerSetReputationInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x29, &Packet) == 0) {
        FRpcServerSetReputationPacket* Typed = static_cast<FRpcServerSetReputationPacket*>(Packet);
        Typed->Target = static_cast<unsigned int>(Info.VibeHandle);
        Typed->Affiliation = Info.Field_0x08;
        Typed->NameA.Size = RpcGetFlattenedFmtStrSize(Info.NameA);
        Typed->NameA.Data = static_cast<unsigned char*>(::operator new(Typed->NameA.Size, std::nothrow));
        Info.NameA.flatten(Typed->NameA.Data, Typed->NameA.Size);
        Typed->NameB.Size = RpcGetFlattenedFmtStrSize(Info.NameB);
        Typed->NameB.Data = static_cast<unsigned char*>(::operator new(Typed->NameB.Size, std::nothrow));
        Info.NameB.flatten(Typed->NameB.Data, Typed->NameB.Size);
        Typed->SetDisplayName(Info.DisplayName,
            static_cast<unsigned int>(std::wcslen(Info.DisplayName)));
        std::list< ::Reputation::Relation*> Local;
        for (unsigned short Index = 0; Index < Info.RelationCount; ++Index) {
            ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
                ::operator new(sizeof(::Reputation::Relation), std::nothrow));
            Copy->GroupId = Info.Relations[Index].GroupId;
            std::memcpy(&Copy->Feeling, &Info.Relations[Index].Feeling, 4u);
            Local.push_back(Copy);
        }
        RpcServerRelationListAppendCopy(Typed->Relations, Local);
        std::memcpy(&Typed->Attitude, &Info.Attitude, 4u);
        PushToClient(ClientId, Packet->AsRaw());
        for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
            if (*It != nullptr) ::operator delete(*It);
        }
    }
}

// RC 06b36080, IClient +0x148, RET 8: SETGROUPFEELINGS 0x2b (channel 2, id 0x2b). Info is
// a FServerSetGroupFeelingsInfo: 06b43920(name, strlen + 1); a stack relation list built
// as in SendServerSetReputation (count byte [Info+0x14], array [Info+0x10]) and deep-copied
// into the packet (06b43da0); PushToClient(ClientId by value); then the stack list's
// relations and nodes are freed.
void CClientImpl::SendServerSetGroupFeelings(unsigned int ClientId, const FServerSetGroupFeelingsInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2b, &Packet) == 0) {
        FRpcServerSetGroupFeelingsPacket* Typed = static_cast<FRpcServerSetGroupFeelingsPacket*>(Packet);
        Typed->SetName(Info.Name, static_cast<unsigned int>(std::strlen(Info.Name)) + 1u);
        std::list< ::Reputation::Relation*> Local;
        for (unsigned short Index = 0; Index < Info.RelationCount; ++Index) {
            ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
                ::operator new(sizeof(::Reputation::Relation), std::nothrow));
            Copy->GroupId = Info.Relations[Index].GroupId;
            std::memcpy(&Copy->Feeling, &Info.Relations[Index].Feeling, 4u);
            Local.push_back(Copy);
        }
        RpcServerRelationListAppendCopy(Typed->Relations, Local);
        PushToClient(ClientId, Packet->AsRaw());
        for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
            if (*It != nullptr) ::operator delete(*It);
        }
    }
}

// RC 06b37b00, IClient +0x154, RET 0x10: REMOTEOBJECTCARGOUPDATE 0x3d. Flag = byte Flag,
// ObjectId = ObjectId; a stack equipment handle built after the allocation from the
// EquipDescVector Cargo with bAll = 0 (06b37b4a), only unequipped items, then assigned
// (06b45710); PushToClient(the client id BY REFERENCE); the handle is destroyed by
// 06b3a450 on every exit, the EH funclet included.
void CClientImpl::SendServerRemoteObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                                    const EquipDescVector& Cargo, unsigned char Flag) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3d, &Packet) == 0) {
        FRpcServerRemoteObjectCargoUpdatePacket* Typed =
            static_cast<FRpcServerRemoteObjectCargoUpdatePacket*>(Packet);
        Typed->Flag = Flag;
        Typed->ObjectId = ObjectId;
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Cargo, &Handle, false);
        Typed->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// ===== end SERVER 0x20..0x3f (RS-P2) =====

// ===== SERVER 0x40..0x5b (RS-P3) =====
// Native sender shape (e.g. RC 06b364a0): Serializer->Allocate(2, Opcode, &Packet); on
// success the payload is filled and handed to the virtual PushToClient. A sender marked BY
// REF takes the client id by reference and passes the referenced id to PushToClient; the
// others pass it by value.
#include "common_goods.h"
#include "common_text.h"
#include <new>

// RC 06b33e70, 0x49 SETHULLSTATUS, by value: FLD [ESP+0x10] / FSTP [pkt+0x20].
void CClientImpl::SendServerSetHullStatus(unsigned int ClientId, float HullStatus) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x49, &Packet) == 0) {
        RpcServerX87CopyFloat(&static_cast<FRpcSetHullStatusPacket*>(Packet)->Value, &HullStatus);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34540, 0x5a GFMISSIONVENDORWHYEMPTY, by value: the low byte of Reason.
void CClientImpl::SendServerGFMissionVendorWhyEmpty(unsigned int ClientId, unsigned int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x5a, &Packet) == 0) {
        static_cast<FRpcServerGfMissionVendorWhyEmptyPacket*>(Packet)->Value =
            static_cast<unsigned char>(Reason);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b362c0, 0x4a SETGUIDEDTARGET, by value: GuidedId, TargetId, low word of the third.
void CClientImpl::SendServerSetGuidedTarget(unsigned int ClientId, unsigned int GuidedId, unsigned int TargetId,
                                            unsigned short TargetSubObjId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4a, &Packet) == 0) {
        FRpcServerSetGuidedTargetPacket* Typed = static_cast<FRpcServerSetGuidedTargetPacket*>(Packet);
        Typed->Field_0x20 = GuidedId;
        Typed->Field_0x24 = TargetId;
        Typed->Field_0x28 = TargetSubObjId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34710, 0x51 USE_ITEM, by value: Info is a FPayloadServerUseItem.
void CClientImpl::SendServerUseItem(unsigned int ClientId, const FPayloadServerUseItem& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x51, &Packet) == 0) {
        FRpcServerUseItemPacket* Typed = static_cast<FRpcServerUseItemPacket*>(Packet);
        Typed->ObjectId = Info.Field_0x00;
        Typed->EquipId = Info.Field_0x04;
        Typed->Count = Info.Field_0x06;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34bc0, 0x56 SENDNNMESSAGE, by value.
void CClientImpl::SendServerSendNNMessage(unsigned int ClientId, unsigned int MessageId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x56, &Packet) == 0) {
        static_cast<FRpcServerSendNNMessagePacket*>(Packet)->Value = MessageId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36a20, 0x45 SET_MISSION_MESSAGE, BY REF: Message is a CSetMissionMessage. Size from
// the inlined 06b347e0 walk, operator new (no null test), FmtStr::flatten, then Type.
void CClientImpl::SendServerSetMissionMessage(const unsigned int& ClientId, const CSetMissionMessage& Message) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x45, &Packet) == 0) {
        FRpcServerSetMissionMessagePacket* Typed = static_cast<FRpcServerSetMissionMessagePacket*>(Packet);
        Typed->TextSize = RpcGetFlattenedFmtStrSize(Message.Text);
        Typed->TextData = static_cast<unsigned char*>(::operator new(Typed->TextSize, std::nothrow));
        Message.Text.flatten(Typed->TextData, Typed->TextSize);
        Typed->Type = Message.Type;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36760, 0x5b MISSIONSAVEA, by value.
void CClientImpl::SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x5b, &Packet) == 0) {
        static_cast<FRpcServerMissionSaveAPacket*>(Packet)->Value = MissionId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b367a0, 0x46 MARKOBJ, BY REF: ObjectId, low byte of MarkType.
void CClientImpl::SendServerMarkObj(const unsigned int& ClientId, unsigned int ObjectId, int MarkType) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x46, &Packet) == 0) {
        FRpcServerMarkObjPacket* Typed = static_cast<FRpcServerMarkObjPacket*>(Packet);
        Typed->ObjectId = ObjectId;
        Typed->MarkType = static_cast<unsigned char>(MarkType);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b367f0, 0x47 CFGINTERFACENOTIFICATION, BY REF: two dwords and the raw byte of Info.
void CClientImpl::SendServerCfgInterfaceNotification(const unsigned int& ClientId,
                                                     const FPayloadServerCfgInterfaceNotification& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x47, &Packet) == 0) {
        FRpcServerCfgInterfaceNotificationPacket* Typed =
            static_cast<FRpcServerCfgInterfaceNotificationPacket*>(Packet);
        Typed->NotifType = Info.Field_0x00;
        Typed->Param = Info.Field_0x04;
        Typed->bEnable = Info.Field_0x08;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d90, 0x40 SETMISSIONFAILURES, BY REF.
void CClientImpl::SendServerSetMissionFailures(const unsigned int& ClientId, int NumFailures) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x40, &Packet) == 0) {
        static_cast<FRpcServerSetMissionFailuresPacket*>(Packet)->Value =
            static_cast<unsigned int>(NumFailures);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b364a0, 0x44 REQUEST_RETURNED, BY REF: ShipId and the low bytes of the last three.
void CClientImpl::SendServerRequestReturned(const unsigned int& ClientId, unsigned int ShipId, int RequestType,
                                            int Response, int Extra) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x44, &Packet) == 0) {
        FRpcServerRequestReturnedPacket* Typed = static_cast<FRpcServerRequestReturnedPacket*>(Packet);
        Typed->ObjectId = ShipId;
        Typed->RequestType = static_cast<unsigned char>(RequestType);
        Typed->Response = static_cast<unsigned char>(Response);
        Typed->Field_0x26 = static_cast<unsigned char>(Extra);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37bb0, 0x55 OBJECTCARGOUPDATE, BY REF: ObjectId, then Item (EquipDesc) word +0x02,
// dword +0x04, word +0x14, float +0x10 (FLD/FSTP), raw byte +0x18.
void CClientImpl::SendServerObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                              const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x55, &Packet) == 0) {
        FRpcServerObjectCargoUpdatePacket* Typed = static_cast<FRpcServerObjectCargoUpdatePacket*>(Packet);
        Typed->ObjectId = ObjectId;
        Typed->EquipId = Item.Id;
        Typed->ArchId = Item.ArchId;
        Typed->Count = static_cast<unsigned short>(Item.Count);
        RpcServerX87CopyFloat(&Typed->Status, &Item.Health);
        Typed->bMission = *reinterpret_cast<const unsigned char*>(&Item.bMission);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37c30, 0x41 BURNFUSE, BY REF: Info is FServerBurnFuseInfo, every field an
// integer move (the two floats included).
void CClientImpl::SendServerBurnFuse(const unsigned int& ClientId, const FServerBurnFuseInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x41, &Packet) == 0) {
        FRpcServerBurnFusePacket* Typed = static_cast<FRpcServerBurnFusePacket*>(Packet);
        Typed->SourceId = Info.SourceId;
        Typed->ObjectId = Info.ObjectId;
        Typed->FuseId = Info.FuseId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->AtT, &Info.AtT, 4u);
        std::memcpy(&Typed->Lifetime, &Info.Lifetime, 4u);
        Typed->bLit = Info.bLit;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36950, 0x4b SET_CAMERA, BY REF: the nine dwords of Info by integer moves.
void CClientImpl::SendServerSetCamera(const unsigned int& ClientId, const FPayloadServerSetCamera& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4b, &Packet) == 0) {
        FRpcServerSetCameraPacket* Typed = static_cast<FRpcServerSetCameraPacket*>(Packet);
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orient.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orient.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orient.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orient.z, 4u);
        std::memcpy(&Typed->Fov, &Info.Fov, 4u);
        std::memcpy(&Typed->Aspect, &Info.Aspect, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b369e0, 0x4c REVERT_CAMERA, BY REF: header only.
void CClientImpl::SendServerRevertCamera(const unsigned int& ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4c, &Packet) == 0) {
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36910, 0x43 SET_STORY_CUE, BY REF: the cue id is read through CueId too.
void CClientImpl::SendServerSetStoryCue(const unsigned int& ClientId, const unsigned int& CueId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x43, &Packet) == 0) {
        static_cast<FRpcServerSetStoryCuePacket*>(Packet)->Value = CueId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b380c0, 0x53 FORMATION_UPDATE, by value: LeaderId, then Offset (Vector) by integer moves.
void CClientImpl::SendServerFormationUpdate(unsigned int ClientId, unsigned int LeaderId, const Vector& Offset) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x53, &Packet) == 0) {
        FRpcServerFormationUpdatePacket* Typed = static_cast<FRpcServerFormationUpdatePacket*>(Packet);
        Typed->LeaderId = LeaderId;
        std::memcpy(&Typed->Offset.x, &Offset.x, 4u);
        std::memcpy(&Typed->Offset.y, &Offset.y, 4u);
        std::memcpy(&Typed->Offset.z, &Offset.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b38120, 0x57 SET_MUSIC, by value: the four music ids, low byte of bFlag, Fade
// through FLD/FSTP.
void CClientImpl::SendServerSetMusic(unsigned int ClientId, unsigned int SpaceMusic, unsigned int DangerMusic,
                                     unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag,
                                     float Fade) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x57, &Packet) == 0) {
        FRpcServerSetMusicPacket* Typed = static_cast<FRpcServerSetMusicPacket*>(Packet);
        Typed->Field_0x20 = SpaceMusic;
        Typed->Field_0x24 = DangerMusic;
        Typed->Field_0x28 = BattleMusic;
        Typed->Field_0x2c = OverrideMusic;
        Typed->Field_0x30 = static_cast<unsigned char>(bFlag);
        RpcServerX87CopyFloat(&Typed->Field_0x34, &Fade);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b381a0, 0x58 CANCEL_MUSIC, by value: header only.
void CClientImpl::SendServerCancelMusic(unsigned int ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x58, &Packet) == 0) {
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b381d0, 0x59 PLAY_SOUND_EFFECT, by value.
void CClientImpl::SendServerPlaySoundEffect(unsigned int ClientId, unsigned int SoundId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x59, &Packet) == 0) {
        static_cast<FRpcServerPlaySoundEffectPacket*>(Packet)->Value = SoundId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b35e80, 0x4d LOADHINT: Hint is a BaseHint pointer, used with no test
// (06b35e9c MOV ESI,[ESP+0x18] ; 06b35ea0 MOV EDX,[ESI]).
// The buffer pointer starts at 0, Serialize (vtable +4) fills it and returns the size,
// the setter 06b42f40 copies the bytes into the packet, +0x28 takes BaseHint::Kind, and
// CleanUp (vtable +0xc) releases the serializer's buffer before the send.
void CClientImpl::SendServerLoadHint(const unsigned int& ClientId, BaseHint* Hint) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4d, &Packet) == 0) {
        FRpcServerLoadHintPacket* Typed = static_cast<FRpcServerLoadHintPacket*>(Packet);
        unsigned char* Serialized = nullptr;
        const unsigned int SerializedSize = Hint->Serialize(&Serialized);
        Typed->SetData(Serialized, SerializedSize);
        Typed->Kind = Hint->Kind;
        Hint->CleanUp(Serialized);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b35f40, 0x4e SETDIRECTIVE, BY REF: Info is FPayloadServerSetDirective,
// copied field by field into the packet (the padding bytes are not copied).
void CClientImpl::SendServerSetDirective(const unsigned int& ClientId, const FPayloadServerSetDirective& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4e, &Packet) == 0) {
        static_cast<FRpcServerSetDirectivePacket*>(Packet)->SetDirective(Info);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b37fc0, 0x52 PLAYERLIST Kind 1, by value: Kind = 1, the low byte of
// bAlreadyPresent, then the setter 06b466f0 with (Name, PlayerId).
void CClientImpl::SendServerPlayerListAdd(unsigned int ClientId, const wchar_t* Name, unsigned int PlayerId,
                                          bool bAlreadyPresent) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindEntry;
        Typed->Field_0x2c = static_cast<unsigned char>(bAlreadyPresent);
        Typed->SetEntry(Name, PlayerId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b38020, 0x52 PLAYERLIST Kind 2, by value: Kind = 2, setter (null, PlayerId).
void CClientImpl::SendServerPlayerListRemove(unsigned int ClientId, unsigned int PlayerId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindDrop;
        Typed->SetEntry(nullptr, PlayerId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b38070, 0x52 PLAYERLIST Kind 0, by value: Kind = 0, setter (null, 0).
void CClientImpl::SendServerPlayerListClear(unsigned int ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindClear;
        Typed->SetEntry(nullptr, 0u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P4: RC 06b33eb0, 0x48 SETCOLLISIONGROUPS, by value: the list's operator= 06b3a360
// from Groups (no null test), then the send.
void CClientImpl::SendServerSetCollisionGroups(unsigned int ClientId, const std::list<CollisionGroupDesc>& Groups) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x48, &Packet) == 0) {
        static_cast<FRpcServerSetCollisionGroupsPacket*>(Packet)->SetGroups(Groups);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P9: RC 06b34a80, 0x4f SENDCOMM, by value (RET 0x5c = 23 dwords). CommCostume is a
// Costume passed by value (13 dwords); retail copies it to a stack local (REP MOVSD) and the
// accessory loop reads that local: Ids[i] = local[4 + i] for i < the count (stored to +0x5c
// first and re-read each pass, signed). Past the 13-dword local lies retail's own frame --
// the return address, then the 23 argument dwords -- so Frame reproduces that memory for
// counts up to 33: i = 8 rewrites the count with itself, i = 9 writes the return-address slot
// into +0x60 (overwritten by CommType below, so its value is unobservable and 0 here), i = 10
// puts ClientId into +0x64, which is then handed to operator delete, as retail does. Counts
// above 33 read past Frame as retail reads past its caller's arguments (undefined; writes past
// the 0x78-byte packet already start at i = 15 in both). Then: +0x60 = CommType; a held array
// is deleted; operator new(TextCount * 4) with no null test; the low byte of TextCount;
// TextCount dwords copied from TextIds (signed bound, not re-read); +0x6c = Mood; Duration
// through FLD/FSTP; the low byte of bReceiverIsOwnShip.
void CClientImpl::SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                     unsigned int VoiceId, Costume CommCostume, unsigned int CommType,
                                     const unsigned int* TextIds, int TextCount, unsigned int Mood,
                                     float Duration, bool bReceiverIsOwnShip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4f, &Packet) == 0) {
        FRpcServerSendCommPacket* Typed = static_cast<FRpcServerSendCommPacket*>(Packet);
        Typed->Field_0x20 = SenderId;
        Typed->Field_0x24 = ReceiverId;
        Typed->Field_0x28 = VoiceId;
        // The retail frame image as raw dwords: the costume local, the return-address slot,
        // then the 23 argument dwords (the costume a second time among them). The pointer,
        // float and bool arguments enter as their stack-slot bits; the bool's slot holds 0/1
        // here where retail's upper three bytes are whatever its caller pushed (Frame[36] is
        // read only at a count of 33).
        static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
        unsigned int Frame[37];
        std::memcpy(&Frame[0], &CommCostume, sizeof(Costume));
        Frame[13] = 0u;
        Frame[14] = ClientId;
        Frame[15] = SenderId;
        Frame[16] = ReceiverId;
        Frame[17] = VoiceId;
        std::memcpy(&Frame[18], &CommCostume, sizeof(Costume));
        Frame[31] = CommType;
        Frame[32] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(TextIds));
        Frame[33] = static_cast<unsigned int>(TextCount);
        Frame[34] = Mood;
        std::memcpy(&Frame[35], &Duration, sizeof(float));
        Frame[36] = static_cast<unsigned int>(bReceiverIsOwnShip);
        Typed->Body.PartIds[0] = CommCostume.PartIds[0];
        Typed->Body.PartIds[1] = CommCostume.PartIds[1];
        Typed->Body.PartIds[2] = CommCostume.PartIds[2];
        Typed->Body.PartIds[3] = CommCostume.PartIds[3];
        Typed->Body.AccessoryCount = CommCostume.AccessoryCount;
        unsigned int* Ids = &Typed->Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Typed->Body.AccessoryCount); ++Index) {
            Ids[Index] = Frame[4 + Index];
        }
        Typed->Field_0x60 = CommType;
        if (Typed->Field_0x64 != nullptr) ::operator delete(Typed->Field_0x64);
        Typed->Field_0x64 = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(TextCount) * 4u, std::nothrow));
        Typed->Field_0x68 = static_cast<signed char>(TextCount);
        for (int Index = 0; Index < TextCount; ++Index) {
            Typed->Field_0x64[Index] = TextIds[Index];
        }
        Typed->Field_0x6c = Mood;
        RpcServerX87CopyFloat(&Typed->Field_0x70, &Duration);
        Typed->Field_0x74 = static_cast<unsigned char>(bReceiverIsOwnShip);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P9: the eight MISCOBJUPDATE senders (0x54), all by value: Allocate, one or two setters
// (RC 06b45830..06b45a60), PushToClient(ClientId).
// RC 06b37cb0: +0x28 (0x08) = ObjectId, +0x34 (0x01) = GroupId.
void CClientImpl::SendServerMiscObjUpdateObjectGroup(unsigned int ClientId, unsigned int ObjectId, unsigned int GroupId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetField0x28(ObjectId);
        Typed->SetField0x34(GroupId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37d50: +0x38 = MissionId, +0x3c = MissionSetBy (0x02).
void CClientImpl::SendServerMiscObjUpdateMission(unsigned int ClientId, unsigned int MissionId, unsigned int MissionSetBy) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        static_cast<FRpcServerMiscObjUpdatePacket*>(Packet)->SetFields0x38(MissionId, MissionSetBy);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37ef0: the text (0x10) assigned from Motd, no null test.
void CClientImpl::SendServerMiscObjUpdateMotd(unsigned int ClientId, const std::wstring& Motd) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        static_cast<FRpcServerMiscObjUpdatePacket*>(Packet)->SetText(Motd);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37d00: +0x28 (0x08) = ObjectId, +0x54 (0x20) = Affiliation.
void CClientImpl::SendServerMiscObjUpdateAffiliation(unsigned int ClientId, unsigned int ObjectId,
                                                     unsigned int Affiliation) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetField0x28(ObjectId);
        Typed->SetField0x54(Affiliation);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37da0: +0x24 (0x04) = PlayerId, the low word of Rank into +0x2c (0x40).
void CClientImpl::SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetField0x24(PlayerId);
        Typed->SetField0x2c(static_cast<unsigned short>(Rank));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37e50: +0x24 (0x04) = PlayerId, +0x34 (0x01) = GroupId.
void CClientImpl::SendServerMiscObjUpdatePlayerGroup(unsigned int ClientId, unsigned int PlayerId, unsigned int GroupId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetField0x24(PlayerId);
        Typed->SetField0x34(GroupId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37ea0: +0x24 (0x04) = PlayerId, +0x30 (0x80) = SystemId.
void CClientImpl::SendServerMiscObjUpdateSystem(unsigned int ClientId, unsigned int PlayerId, unsigned int SystemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetField0x24(PlayerId);
        Typed->SetField0x30(SystemId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37df0: every element of MemberIds, first to last (the end re-read each pass), through
// the id setter 06b45a60; no null test.
void CClientImpl::SendServerMiscObjUpdateFormation(unsigned int ClientId, const std::vector<unsigned int>& MemberIds) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        for (std::vector<unsigned int>::const_iterator It = MemberIds.begin(); It != MemberIds.end(); ++It) {
            Typed->AddId(*It);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RS-P9: RC 06b34c50, 0x42 CREATESOLAR, by value (RET 8). Info is a CSolarCreateInfo, used
// with no null test: the block fill 06b382f0, +0x9c = Info+0xad, +0x9d = Info+0xa8 (raw
// bytes); outside single player Reputation::Vibe::GetAffiliation(vibe, +0xb0, false),
// AttitudeTowards(vibe, Info.VibeOtherGroupId, +0xb4), then two FmtStr locals (EH states 1
// and 2) filled by GetName(vibe, A, B, DisplayName) and flattened into +0xa0/+0xa4 (A) and
// +0xa8/+0xac (B): the inlined flattened-size walk, operator new with no null test,
// FmtStr::flatten. B is destroyed before A. The Common calls' returns are discarded.
void CClientImpl::SendServerCreateSolar(unsigned int ClientId, const CSolarCreateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x42, &Packet) == 0) {
        FRpcServerCreateSolarPacket* Typed = static_cast<FRpcServerCreateSolarPacket*>(Packet);
        RpcServerCreateObjectFill(&Info, &Typed->Fields);
        Typed->Field_0x9c = Info.Field_0xad;
        Typed->bMarkedBy = Info.bMarkedBy;
        if (!SinglePlayer()) {
            Reputation::Vibe::GetAffiliation(Typed->Fields.VibeGroupId, Typed->Affiliation, false);
            Reputation::Vibe::AttitudeTowards(Typed->Fields.VibeGroupId, Info.VibeOtherGroupId,
                Typed->Attitude);
            FmtStr NameA;
            FmtStr NameB;
            const wchar_t* DisplayName = nullptr;
            Reputation::Vibe::GetName(Typed->Fields.VibeGroupId, NameA, NameB, DisplayName);
            Typed->NameASize = RpcGetFlattenedFmtStrSize(NameA);
            Typed->NameA = static_cast<unsigned char*>(::operator new(Typed->NameASize, std::nothrow));
            NameA.flatten(Typed->NameA, Typed->NameASize);
            Typed->NameBSize = RpcGetFlattenedFmtStrSize(NameB);
            Typed->NameB = static_cast<unsigned char*>(::operator new(Typed->NameBSize, std::nothrow));
            NameB.flatten(Typed->NameB, Typed->NameBSize);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// ===== end SERVER 0x40..0x5b (RS-P3) =====
