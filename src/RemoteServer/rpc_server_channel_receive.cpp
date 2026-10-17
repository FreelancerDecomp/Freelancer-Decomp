// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#define RPCLOCAL_EXPORTS
#include <windows.h>
#include "../include/rpc_iserver.h"
#include "../include/common_server.h"
#include "../include/rpc_iclient.h"
#include "../include/common_goods.h"        // CostumeHint (0x4d LOADHINT), EquipDescVector
#include "../include/server_createinfo.h"   // CShipCreateInfo / CSolarCreateInfo / loot / guided
#include "../include/server_characterset.h" // CCharacterSet::CharacterInfo (0x03)
#include "../include/common_misc.h"
#include "../include/common_archetype.h"
#include "../include/rpc_packet.h"
#include "../include/rpc_iclient_args.h"
#include "../include/server_sysdb.h" // FMineExplosion (0x31)
#include "rpc_server_channel_receive_private.h"
#include "../include/rpc_server_channel_codec.h"

#include <list>
#include <new>
#include <cstring>
#include <cwchar>

struct FRpcLocalCostumeHint final : public CostumeHint
{
};

// ----- RS-P1: 0x03 CHARACTERINFO receiver -----

// RS 06bdfdae..06bdfe7e (+0x274; +0x278 likewise): only when the source has a block;
// new(0x18) whose test only skips the zero stores (a null block is written through,
// reproduced on purpose); +4, +0, +0xc, +8, Count; for an unsigned non-zero count a
// bare new(count * 4) and a copy bounded by the DESTINATION's count.
static void RpcServerCharacterIdBlockCopy(FRpcServerCharacterIdBlock** Out,
    const FRpcServerCharacterIdBlock* Source) {
    if (Source == nullptr) return;
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
    *Out = Block;
    Block->Field_0x04 = Source->Field_0x04;
    Block->Field_0x00 = Source->Field_0x00;
    Block->Field_0x0c = Source->Field_0x0c;
    Block->Field_0x08 = Source->Field_0x08;
    Block->Count = Source->Count;
    if (static_cast<unsigned int>(Block->Count) != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Block->Count) << 2, std::nothrow));
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            Block->Ids[Index] = Source->Ids[Index];
        }
    }
}

// RS 06bdfb70 (packet method, = rpclocal 06d94460): one fresh record per non-null
// record of the packet's list, appended to Dest. The new-expression test only skips the
// ctor (06bdfbd4); the copy runs on the result either way (reproduced on purpose).
static void RpcServerCharacterRecordListCopy(std::list<FRpcServerCharacterRecord*>& Dest,
    const std::list<FRpcServerCharacterRecord*>& Source) {
    for (std::list<FRpcServerCharacterRecord*>::const_iterator It = Source.begin();
         It != Source.end(); ++It) {
        const FRpcServerCharacterRecord* From = *It;
        if (From == nullptr) continue;                                      // 06bdfbb5
        FRpcServerCharacterRecord* To = new (std::nothrow) FRpcServerCharacterRecord();
        To->Name = From->Name;                                              // 06bdfc98
        To->Field_0x200 = From->Field_0x200;
        To->Field_0x201 = From->Field_0x201;
        To->TextA = From->TextA;                                            // 06bdfccc
        To->TextB = From->TextB;                                            // 06bdfce8
        To->Field_0x224 = From->Field_0x224;
        To->Field_0x228 = From->Field_0x228;
        To->Field_0x22c = From->Field_0x22c;
        To->Field_0x230 = From->Field_0x230;
        To->Field_0x234 = From->Field_0x234;
        To->Field_0x238 = From->Field_0x238;
        To->Field_0x23c = From->Field_0x23c;
        To->Field_0x240 = From->Field_0x240;
        To->Field_0x244 = From->Field_0x244;
        To->Field_0x248 = From->Field_0x248;
        To->Field_0x24c = From->Field_0x24c;
        To->Field_0x250 = From->Field_0x250;
        To->Field_0x254 = From->Field_0x254;
        To->Field_0x258 = From->Field_0x258;
        To->Field_0x25c = From->Field_0x25c;                                // 06bdfd9c
        RpcServerCharacterIdBlockCopy(&To->BlockA, From->BlockA);
        RpcServerCharacterIdBlockCopy(&To->BlockB, From->BlockB);
        To->Equipment.AppendCopy(From->Equipment);                          // 06bdff6f
        // 06bdff76..06bdff8a: +0x260 = low byte of the copied list's size, or 0.
        To->Field_0x260 = (To->Equipment.Records != nullptr)
            ? static_cast<unsigned char>(To->Equipment.Records->size() & 0xffu) : 0u;
        To->CollisionGroups = From->CollisionGroups;                        // 06bdff90..
        Dest.push_back(To);
    }
}

// HandleCharacterInfo 06bd48e7..06bd4c6b: one CCharacterSet::CharacterInfo from one wire
// record (the inverse of the sender's permutation).
static void RpcServerCharacterInfoFromRecord(CCharacterSet::CharacterInfo* Dest,
    const FRpcServerCharacterRecord* Source) {
    Dest->Id = Source->Name;                                                // 06bd48ea
    Dest->bIsRestart = (Source->Field_0x200 != 0) ? 1u : 0u;                // 06bd48f8 SETNE
    Dest->bIsAutoSave = (Source->Field_0x201 != 0) ? 1u : 0u;               // 06bd4909 SETNE
    Dest->Name = Source->TextA;                                             // 06bd4929
    Dest->Description = Source->TextB;                                      // 06bd4946
    Dest->DescripStrId = Source->Field_0x224;
    Dest->TimestampLow = Source->Field_0x228;
    Dest->TimestampHigh = Source->Field_0x22c;
    Dest->ShipArchetype = Source->Field_0x230;
    Dest->Money = static_cast<int>(Source->Field_0x234);
    Dest->VoiceId = Source->Field_0x244;                                    // 06bd498e -> +0x26c
    Dest->Rank = Source->Field_0x248;                                       // 06bd499a -> +0x270
    Dest->BaseId = Source->Field_0x23c;                                     // 06bd49a6 -> +0x264
    Dest->LocationId = Source->Field_0x240;                                 // 06bd49b2 -> +0x268
    Dest->SystemId = Source->Field_0x238;                                   // 06bd49be -> +0x260
    std::memcpy(&Dest->HullStatus, &Source->Field_0x250, 4u);               // 06bd49ca -> +0x244
    Dest->NumKills = static_cast<int>(Source->Field_0x254);                 // -> +0x238
    Dest->NumMissionFailures = static_cast<int>(Source->Field_0x258);       // -> +0x240
    Dest->NumMissionSuccesses = static_cast<int>(Source->Field_0x25c);      // -> +0x23c
    // 06bd49f4..06bd4af7: a null block leaves the Costume as constructed; otherwise
    // [1],[0],[3],[2], the count, and an unsigned copy NOT bounded by the eight slots
    // (reproduced on purpose), the bound re-read from the block every round.
    const FRpcServerCharacterIdBlock* Blocks[2] = { Source->BlockA, Source->BlockB };
    Costume* Costumes[2] = { &Dest->ComCostume, &Dest->BaseCostume };
    for (int Which = 0; Which < 2; ++Which) {
        const FRpcServerCharacterIdBlock* Block = Blocks[Which];
        if (Block == nullptr) continue;
        Costume& Out = *Costumes[Which];
        Out.PartIds[1] = Block->Field_0x04;
        Out.PartIds[0] = Block->Field_0x00;
        Out.PartIds[3] = Block->Field_0x0c;
        Out.PartIds[2] = Block->Field_0x08;
        Out.AccessoryCount = static_cast<unsigned int>(Block->Count);
        unsigned int* OutIds = &Out.AccessoryIds[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            OutIds[Index] = Block->Ids[Index];
        }
    }
    // 06bd4af9..06bd4bea: only the handle's list header is tested. Each element is a
    // default EquipDesc appended to +0x248, then the setters on the appended node.
    if (Source->Equipment.Records != nullptr) {
        for (std::list<FRpcServerEquipRecord*>::const_iterator It =
                 Source->Equipment.Records->begin();
             It != Source->Equipment.Records->end(); ++It) {
            const FRpcServerEquipRecord* E = *It;
            Dest->Equipment.Items.push_back(EquipDesc());                   // 06bd4b27..06bd4b68
            EquipDesc& Item = Dest->Equipment.Items.back();
            Item.set_arch_id(E->ArchId);                                    // 06bd4b7f
            Item.set_count(E->Count);                                       // 06bd4b8b
            Item.set_id(E->Id);                                             // 06bd4b9a
            Item.set_equipped(E->bEquipped);                                // 06bd4ba8 raw byte
            Item.set_status(E->Health);                                     // 06bd4bb4
            // 06bd4bba / 06bd4bbd: the wire byte stored raw into EquipDesc +0x18.
            std::memcpy(&Item.bMission, &E->Field_0x0f, 1u);
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(E->Hardpoint, false);             // 06bd4bc6
            Item.set_hardpoint(Hardpoint);                                  // 06bd4bda
        }
    }
    // 06bd4bf4..06bd4c65: list<CollisionGroupDesc>::operator= onto +0x254.
    Dest->CollisionGroups = Source->CollisionGroups;
}

// RS 06bd4750 (exported; instruction-identical to rpclocal 06d84490). No packet test:
// the packet is dereferenced untested. A local CCharacterSet (06bd476e, MetaList(false)
// + OwnerList vptr) and a local record list (06bd4786..06bd47a1) are built; the
// packet's list is deep-copied into it (06bd47b6). Per copy: new(0x2dc) record; null
// skips the conversion (06bd47f5 -> 06bd48d8) but the node is still appended with a
// null value (MetaList::append inline, 06bd4c6d..06bd4cab); the copy is then freed on
// the spot (06bd4cb3..06bd4d33) while its node keeps the dangling pointer.
// SaveFailureCount <- +0x20 (06bd4d53), Client->vt[+0x34](Arg, &Set) (06bd4d70).
// Retail then destroys the Set (records, then the MetaList), then the local list's
// nodes; here the local list goes first (reverse declaration order): only frees,
// unobservable. Returns true (06bd4e96). `this` is never read, so case 03 and the
// exported member share this body.
static bool RpcServerHandleCharacterInfo(const FPacketBase* Packet, IClient* Client,
    unsigned int Arg) {
    const FRpcServerCharacterInfoPacket* Typed =
        static_cast<const FRpcServerCharacterInfoPacket*>(Packet);
    CCharacterSet Set;
    std::list<FRpcServerCharacterRecord*> Local;
    RpcServerCharacterRecordListCopy(Local, Typed->Records);
    for (std::list<FRpcServerCharacterRecord*>::iterator It = Local.begin(); It != Local.end();
         ++It) {
        const FRpcServerCharacterRecord* Source = *It;                      // 06bd47d4, untested
        CCharacterSet::CharacterInfo* Info = new (std::nothrow) CCharacterSet::CharacterInfo();
        if (Info != nullptr) {
            RpcServerCharacterInfoFromRecord(Info, Source);
        }
        Set.Characters.append(Info);
        if (*It != nullptr) {
            delete *It;                                                     // 06bd4cbe..06bd4d33
        }
    }
    Set.SaveFailureCount = Typed->SaveFailureCount;
    Client->SendServerCharacterInfo(Arg, Set);
    return true;
}

bool IServerImpl::HandleCharacterInfo(CDAPCharacterInfo* Packet, IClient* Client, unsigned int Arg) {
    return RpcServerHandleCharacterInfo(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}
// ----- end RS-P1: 0x03 CHARACTERINFO receiver -----
// RS 06bd5030 (exported; instruction-identical to rpclocal 06d84c60; no caller inside
// remoteserver). `this` is unused. A null packet returns false (06bd5050). Otherwise a
// stack DamageList is built inline (list dummy operator new(0x14), size 0, byte and
// three dwords 0: DamageList()), filled by the packet's 06bdb260 (DamageList::operator=
// from packet+0x24), handed to Client->vt[+0xfc](Arg, [packet+0x20], &list), then torn
// down node by node and the dummy freed (06bd50b2..06bd50ee); returns true.
bool IServerImpl::HandleDamageObject(CDAPDamageSpaceObj* Packet, IClient* Client, unsigned int Arg) {
    if (Packet == nullptr) {
        return false;
    }
    const FRpcServerDamageObjectPacket* Typed =
        reinterpret_cast<const FRpcServerDamageObjectPacket*>(Packet);
    DamageList Local;
    Local = Typed->Damage;                                                     // 06bd5093
    Client->SendServerDamageObject(Arg, Typed->ObjectId, Local);              // 06bd50ac
    return true;
}

// ----- RS-P2: 0x29 / 0x2b relation array (inline in cases 06bd159c and 06bd1706) -----
// operator new(list size * 8) with no null test; every non-null relation of the stack
// list is copied by integer moves (GroupId, then Feeling) at a 16-bit index and then
// freed. The list keeps its (now dangling) values; only its nodes are freed later.
static ::Reputation::Relation* RpcServerRelationArrayFromList(
    std::list< ::Reputation::Relation*>& Local) {
    ::Reputation::Relation* Array = static_cast< ::Reputation::Relation*>(::operator new(
        static_cast<unsigned int>(Local.size()) * sizeof(::Reputation::Relation), std::nothrow));
    unsigned short At = 0;
    for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
        ::Reputation::Relation* Relation = *It;
        if (Relation == nullptr) continue;
        Array[At].GroupId = Relation->GroupId;
        std::memcpy(&Array[At].Feeling, &Relation->Feeling, 4u);
        ++At;
        ::operator delete(Relation);
    }
    return Array;
}
// ----- end RS-P2 relation array -----

void RpcReceiveServerChannel(unsigned int ClientId, FPacketBase* Packet, IClient* Client)
{
    unsigned int Opcode = Packet->GetOpcode();
    const unsigned char* Payload = reinterpret_cast<const unsigned char*>(Packet) + 0x20;

        switch (Opcode)
        {
            case 0x01: // FLPACKET_SERVER_CONNECTRESPONSE, RS case 06bd17c8
            {
                // vt+0x2c = OnConnect([+0x20]): the only SERVER call without a client id.
                Client->OnConnect(static_cast<const FRpcConnectResponsePacket*>(Packet)->Value);
                break;
            }
            case 0x02: // FLPACKET_SERVER_LOGINRESPONSE, RS case 06bd17dc
            {
                // The byte goes to module global 06c138b0 (06bd17df), then
                // vt+0x30 = SendServerLoginResponse(1, &global).
                static unsigned char GLoginResponseStatic = 0;
                GLoginResponseStatic = static_cast<const FRpcLoginResponsePacket*>(Packet)->Value;
                Client->SendServerLoginResponse(1u, GLoginResponseStatic);
                break;
            }
            case 0x03: // FLPACKET_SERVER_CHARACTERINFO, RS case 06bd17fc
            {
                // HandleCharacterInfo(packet, GClient, 1) (06bd1805); `this` is unused by
                // the member, so the shared body is called directly.
                RpcServerHandleCharacterInfo(Packet, Client, 1u);
                break;
            }
            case 0x04: // FLPACKET_SERVER_CREATESHIP, RS case 06bd20b5..06bd23b1
            {
                const FRpcServerCreateShipPacket* Typed =
                    static_cast<const FRpcServerCreateShipPacket*>(Packet);
                // .data:06c138f0, built on first use (guard bit 2 of 06c138b8: base ctor
                // 06bd3f20 + the derived stores) and destroyed by the atexit 06bd4390.
                static CShipCreateInfo GCreateShipInfo;
                RpcServerCreateObjectRead(&Typed->Object, &GCreateShipInfo);        // 06bd211f
                GCreateShipInfo.bUsingTradelane = Typed->bUsingTradelane;           // 06bd212a
                GCreateShipInfo.OwnerPlayer = Typed->OwnerPlayer;                   // 06bd2135
                GCreateShipInfo.Field_0xd8 = static_cast<unsigned int>(Typed->Field_0xc4);       // MOVZX
                GCreateShipInfo.Field_0xb0 = static_cast<unsigned int>(Typed->Field_0xc0);       // MOVZX
                GCreateShipInfo.ReputationRank = static_cast<unsigned int>(Typed->ReputationRank); // MOVZX
                std::memcpy(&GCreateShipInfo.Throttle, &Typed->Throttle, 4u);      // 06bd2167
                // 06bd216d..06bd21b1: x by integer move, y and z through the x87 stack
                // (retail loads z then y and stores y then z).
                std::memcpy(&GCreateShipInfo.AngularVelocity.x, &Typed->AngularVelocity.x, 4u);
                RpcServerX87CopyFloat(&GCreateShipInfo.AngularVelocity.y, &Typed->AngularVelocity.y);
                RpcServerX87CopyFloat(&GCreateShipInfo.AngularVelocity.z, &Typed->AngularVelocity.z);
                std::memcpy(&GCreateShipInfo.Velocity.x, &Typed->Velocity.x, 4u);
                RpcServerX87CopyFloat(&GCreateShipInfo.Velocity.y, &Typed->Velocity.y);
                RpcServerX87CopyFloat(&GCreateShipInfo.Velocity.z, &Typed->Velocity.z);
                GCreateShipInfo.bMarkedBy = Typed->bMarkedBy;                       // 06bd21bd
                if (!SinglePlayer()) {                                              // 06bd21c3
                    // 06bd21d1..06bd22f5: two stack FmtStr (EH states 0x10 / 0x11, B
                    // destroyed first); the vibe is the PACKET's +0x2c.
                    FmtStr NameA;
                    FmtStr NameB;
                    NameA.unflatten(Typed->NameA, Typed->NameASize);                // 06bd227c
                    NameB.unflatten(Typed->NameB, Typed->NameBSize);                // 06bd2290
                    const int& Group = Typed->Object.VibeGroupId;
                    Reputation::Vibe::EnsureExists(Group);                          // 06bd2296
                    Reputation::Vibe::SetName(Group, NameA, NameB, Typed->DisplayName); // 06bd22ae
                    Reputation::Vibe::SetAffiliation(Group, Typed->Affiliation, false); // 06bd22bd
                    Reputation::Vibe::SetAttitudeTowardsPlayer(Group, Typed->Attitude); // 06bd22cd
                }
                Client->SendServerCreateShip(1u, GCreateShipInfo);                  // 06bd2306
                // 06bd230c..06bd23ab: CShipCreateInfo::clear() inlined (base fields, the
                // equipment vector erase, both lists, then the derived tail).
                GCreateShipInfo.clear();
                break;
            }
            case 0x05: // FLPACKET_SERVER_DAMAGEOBJECT, RS case 06bd1fc4
            {
                // HandleDamageObject's body inlined, client id literal 1: a stack
                // DamageList (Common list ctor, byte +0xc and dwords +0x10..+0x18 zeroed,
                // EH state 0xf), filled by 06bdb260, SendServerDamageObject(1, +0x20, &list),
                // then erase(begin, end) and the dummy freed (06bd2017..06bd204d).
                const FRpcServerDamageObjectPacket* Typed =
                    static_cast<const FRpcServerDamageObjectPacket*>(Packet);
                DamageList Local;
                Local = Typed->Damage;                                         // 06bd1ffd
                Client->SendServerDamageObject(1u, Typed->ObjectId, Local);
                break;
            }
            case 0x06: // FLPACKET_SERVER_DESTROYOBJECT, RS case 06bd205a
            {
                // Function-local static (guard bit 1 of 06c138b8, zeroed once, empty
                // atexit dtor 06bd43a0). The byte is zero-extended into the dword.
                const FRpcServerDestroyObjectPacket* Typed =
                    static_cast<const FRpcServerDestroyObjectPacket*>(Packet);
                static FServerDestroyObjectInfo GDestroyObjectStatic;
                GDestroyObjectStatic.ObjectId = Typed->ObjectId;
                const unsigned int Widened = Typed->Field_0x04;
                std::memcpy(&GDestroyObjectStatic.Field_0x04, &Widened, sizeof(Widened));
                Client->SendServerDestroyObject(1u, GDestroyObjectStatic);
                break;
            }
            case 0x07: // FLPACKET_SERVER_LAUNCH, RS case 06bd1a41
            {
                // Stack struct, every dword copied by integer moves.
                const FRpcServerLaunchPacket* Typed = static_cast<const FRpcServerLaunchPacket*>(Packet);
                FServerLaunchInfo Info;
                Info.Field_0x00 = Typed->Field_0x00;
                Info.Field_0x04 = Typed->Field_0x04;
                Info.Field_0x08 = Typed->Field_0x08;
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                Client->SendServerLaunch(1u, Info);
                break;
            }
            case 0x08: // FLPACKET_SERVER_CHARSELECTVERIFIED, RS case 06bd1918
            {
                // Stores +0x00, +0x08, +0x0c only (06bd1918..06bd1943); +0x04 stays
                // unwritten as in retail -- the game reader (005ab380) never reads it.
                // The double moves as two integer dwords.
                const FRpcServerCharSelectVerifiedPacket* Typed =
                    static_cast<const FRpcServerCharSelectVerifiedPacket*>(Packet);
                FServerCharSelectVerifiedInfo Info;
                Info.VibeHandle = static_cast<int>(Typed->RepInstance);
                std::memcpy(&Info.ServerTime, &Typed->Time, sizeof(Info.ServerTime));
                Client->SendServerCharSelectVerified(1u, Info);
                break;
            }
            case 0x0a: // FLPACKET_SERVER_ACTIVATEOBJECT, RS case 06bd249b
            {
                // Module globals 06c1389c/06c138a0/06c138a4; only the byte of +0x04 is
                // written, its three pad bytes stay zero.
                const FRpcServerActivateObjectPacket* Typed =
                    static_cast<const FRpcServerActivateObjectPacket*>(Packet);
                static FPayloadServerActivateObject GActivateObjectStatic;
                GActivateObjectStatic.ObjectId = Typed->ObjectId;
                GActivateObjectStatic.bActivate = Typed->bActivate;
                GActivateObjectStatic.Field_0x08 = Typed->Field_0x08;
                Client->SendServerActivateObject(1u, GActivateObjectStatic);
                break;
            }
            case 0x0b: // FLPACKET_SERVER_LAND, RS case 06bd246c
            {
                const FRpcServerLandPacket* Typed = static_cast<const FRpcServerLandPacket*>(Packet);
                FServerLandInfo Info;
                Info.ShipId = Typed->Field_0x00;
                Info.DockId = Typed->Field_0x04;
                Info.BaseId = Typed->Field_0x08;
                Client->SendServerLand(1u, Info);
                break;
            }
            case 0x0d: // FLPACKET_SERVER_SETSTARTROOM, RS case 06bd1970
            {
                const FRpcServerSetStartRoomPacket* Typed =
                    static_cast<const FRpcServerSetStartRoomPacket*>(Packet);
                Client->SendServerSetStartRoom(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            // GF list cases 0x0e..0x1f: every IClient argument is read straight off the
            // packet, client id literal 1; the packet stays owned by the caller (its
            // destructor frees a blob's Data).
            case 0x0e: // GFCOMPLETENEWSBROADCASTLIST, RS case 06bd19d6
                Client->SendServerGFCompleteNewsBroadcastList(1u,
                    static_cast<FRpcServerGfCompleteNewsBroadcastListPacket*>(Packet)->Value);
                break;
            case 0x0f: // GFCOMPLETECHARLIST, RS case 06bd195a
                Client->SendServerGFCompleteCharList(1u,
                    static_cast<FRpcServerGfCompleteCharListPacket*>(Packet)->Value);
                break;
            case 0x10: // GFCOMPLETEMISSIONCOMPUTERLIST, RS case 06bd19a4
                Client->SendServerGFCompleteMissionComputerList(1u,
                    static_cast<FRpcServerGfCompleteMissionComputerListPacket*>(Packet)->Value);
                break;
            case 0x11: // GFCOMPLETESCRIPTBEHAVIORLIST, RS case 06bd19ef
                Client->SendServerGFCompleteScriptBehaviorList(1u,
                    static_cast<FRpcServerGfCompleteScriptBehaviorListPacket*>(Packet)->Value);
                break;
            case 0x13: // GFCOMPLETEAMBIENTSCRIPTLIST, RS case 06bd19bd
                Client->SendServerGFCompleteAmbientScriptList(1u,
                    static_cast<FRpcServerGfCompleteAmbientScriptListPacket*>(Packet)->Value);
                break;
            case 0x14: // GFDESTROYNEWSBROADCAST, RS case 06bd29e9
            {
                const FRpcServerGfDestroyNewsBroadcastPacket* Typed =
                    static_cast<const FRpcServerGfDestroyNewsBroadcastPacket*>(Packet);
                Client->SendServerGFDestroyNewsBroadcast(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x15: // GFDESTROYCHARACTER, RS case 06bd198a
            {
                const FRpcServerGfDestroyCharacterPacket* Typed =
                    static_cast<const FRpcServerGfDestroyCharacterPacket*>(Packet);
                Client->SendServerGFDestroyCharacter(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x16: // GFDESTROYMISSIONCOMPUTER, RS case 06bd2ab0
            {
                const FRpcServerGfDestroyMissionComputerPacket* Typed =
                    static_cast<const FRpcServerGfDestroyMissionComputerPacket*>(Packet);
                Client->SendServerGFDestroyMissionComputer(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x17: // GFDESTROYSCRIPTBEHAVIOR, RS case 06bd2a76
            {
                const FRpcServerGfDestroyScriptBehaviorPacket* Typed =
                    static_cast<const FRpcServerGfDestroyScriptBehaviorPacket*>(Packet);
                Client->SendServerGFDestroyScriptBehavior(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x19: // GFDESTROYAMBIENTSCRIPT, RS case 06bd2a23
            {
                const FRpcServerGfDestroyAmbientScriptPacket* Typed =
                    static_cast<const FRpcServerGfDestroyAmbientScriptPacket*>(Packet);
                Client->SendServerGFDestroyAmbientScript(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x1a: // GFSCRIPTBEHAVIOR, RS case 06bd2a59: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfScriptBehaviorPacket* Typed =
                    static_cast<const FRpcServerGfScriptBehaviorPacket*>(Packet);
                Client->SendServerGFScriptBehavior(1u, Typed->Data, Typed->Size);
                break;
            }
            case 0x1d: // GFUPDATEMISSIONCOMPUTER, RS case 06bd2a93: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateMissionComputerPacket* Typed =
                    static_cast<const FRpcServerGfUpdateMissionComputerPacket*>(Packet);
                Client->SendServerGFUpdateMissionComputer(1u, Typed->Data, Typed->Size);
                break;
            }
            case 0x1e: // GFUPDATENEWSBROADCAST, RS case 06bd29cc: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateNewsBroadcastPacket* Typed =
                    static_cast<const FRpcServerGfUpdateNewsBroadcastPacket*>(Packet);
                Client->SendServerGFUpdateNewsBroadcast(1u, Typed->Data, Typed->Size);
                break;
            }
            case 0x1f: // GFUPDATEAMBIENTSCRIPT, RS case 06bd2a06: (1, Data +0x20, Size +0x24)
            {
                const FRpcServerGfUpdateAmbientScriptPacket* Typed =
                    static_cast<const FRpcServerGfUpdateAmbientScriptPacket*>(Packet);
                Client->SendServerGFUpdateAmbientScript(1u, Typed->Data, Typed->Size);
                break;
            }
            // ===== SERVER 0x20..0x3f (RS-P2) =====
            case 0x20: // GFMISSIONVENDORACCEPTANCE, RS case 06bd2acd: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfMissionVendorAcceptancePacket* Typed =
                    static_cast<const FRpcServerGfMissionVendorAcceptancePacket*>(Packet);
                Client->SendServerGFMissionVendorAcceptance(1u, Typed->Data, Typed->Size);
                break;
            }
            case 0x21: // FLPACKET_SERVER_SYSTEM_SWITCH_OUT, RS 06bd2441
            {
                // Retail copies both dwords into a module global (06c137d4 / 06c137d8)
                // and passes its address; a static FServerSystemSwitchOutInfo (two dwords)
                // has the same storage.
                static FServerSystemSwitchOutInfo GSystemSwitchOut;
                const FRpcServerSystemSwitchOutPacket* Typed =
                    static_cast<const FRpcServerSystemSwitchOutPacket*>(Packet);
                GSystemSwitchOut.ShipId = Typed->Field_0x00;
                GSystemSwitchOut.GateId = Typed->Field_0x04;
                Client->SendServerSystemSwitchOut(1u, GSystemSwitchOut);
                break;
            }
            case 0x22: // FLPACKET_SERVER_SYSTEM_SWITCH_IN, RS 06bd23b6
            {
                // Function-local static 06c138c8 (guard bit 4 of 06c138b8, empty atexit
                // dtor 06bd4380): nine dwords copied by integer moves,
                // SendServerSystemSwitchIn(1, &static).
                const FRpcServerSystemSwitchInPacket* Typed =
                    static_cast<const FRpcServerSystemSwitchInPacket*>(Packet);
                static FPayloadServerSystemSwitchIn GSystemSwitchInStatic;
                std::memcpy(&GSystemSwitchInStatic.Position, &Typed->Position,
                    sizeof(GSystemSwitchInStatic.Position));
                std::memcpy(&GSystemSwitchInStatic.Orientation, &Typed->Orientation,
                    sizeof(GSystemSwitchInStatic.Orientation));
                GSystemSwitchInStatic.Field_0x00 = Typed->Field_0x00;
                GSystemSwitchInStatic.Field_0x04 = Typed->Field_0x04;
                Client->SendServerSystemSwitchIn(1u, GSystemSwitchInStatic);
                break;
            }
            case 0x23: // FLPACKET_SERVER_SETSHIPARCH, RS 06bd1944
            {
                Client->SendServerSetShipArch(1u, static_cast<const FRpcSetShipArchPacket*>(Packet)->Value);
                break;
            }
            case 0x24: // FLPACKET_SERVER_SETEQUIPMENT, RS 06bd18ae
            {
                // The vector is built first (EH state 0xc), the handle second (0xd);
                // the handle is destroyed first (06bdc790), then the vector.
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                static_cast<const FRpcServerSetEquipmentPacket*>(Packet)->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                Client->SendServerSetEquipment(1u, Vector);
                break;
            }
            case 0x25: // FLPACKET_SERVER_SETCARGO, RS 06bd180f
            {
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                static_cast<const FRpcServerSetCargoPacket*>(Packet)->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                Client->SendServerSetCargo(1u, Vector);
                break;
            }
            case 0x26: // GFUPDATECHAR, RS case 06bd29b2: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateCharPacket* Typed =
                    static_cast<const FRpcServerGfUpdateCharPacket*>(Packet);
                Client->SendServerGFUpdateChar(1u, Typed->Data, Typed->Size);
                break;
            }
            case 0x27: // FLPACKET_SERVER_REQUESTCREATESHIPRESP, RS 06bd1aa1
            {
                const FRpcServerRequestCreateShipRespPacket* Typed =
                    static_cast<const FRpcServerRequestCreateShipRespPacket*>(Packet);
                // The wire byte as the bool: the callee keeps one byte (rpclocal 06d8a158),
                // so only values above 1 would differ.
                Client->SendServerRequestCreateShipResp(1u, Typed->Field_0x00 != 0, Typed->Field_0x04);
                break;
            }
            case 0x28: // FLPACKET_SERVER_CREATELOOT, RS 06bd12b8
            {
                // An empty list sends nothing. Otherwise a FServerCreateLootRequest with
                // OwnerId = +0x20, Kind = MOVZX byte +0x24, both bytes 0, Count = the low
                // byte of the list size, and an array operator new(Count * 0x48) (no null
                // test: a failed allocation is written through, reproduced on purpose)
                // whose elements get the inlined ctor 06bd4200; the list records are
                // copied in by integer moves at a byte index; SendServerCreateLoot(1, &request), then
                // the array is freed. Retail leaves the request's pad bytes and each
                // element's +0x47 uninitialised (nondeterministic); the port zeroes them.
                const FRpcServerCreateLootPacket* Typed =
                    static_cast<const FRpcServerCreateLootPacket*>(Packet);
                const std::list<FRpcServerLootRecord>& Loot = Typed->Loot;
                if (Loot.size() == 0) break;
                const unsigned char Count = static_cast<unsigned char>(Loot.size());
                FServerCreateLootRequest Request;
                Request.Count = Count;
                Request.bMarkedBy = 0;
                Request.bTemporary = 0;
                Request.Pad0a[0] = 0;
                Request.Pad0a[1] = 0;
                Request.Items = nullptr;
                Request.OwnerId = Typed->OwnerId;
                Request.Kind = Typed->Kind;
                Request.Pad11[0] = 0;
                Request.Pad11[1] = 0;
                Request.Pad11[2] = 0;
                FServerLootDesc* Items = static_cast<FServerLootDesc*>(
                    ::operator new(static_cast<unsigned int>(Count) * 0x48u, std::nothrow));
                if (Items != nullptr) {
                    for (unsigned int Index = 0; Index < Count; ++Index) {
                        FServerLootDesc& Desc = Items[Index];
                        Desc.LootId = 0;
                        Desc.ContentsNickname = 0;
                        std::memset(&Desc.ContentsHitPts, 0, 4u);
                        Desc.Units = 0;
                        Desc.ContainerNickname = 0;
                        std::memset(&Desc.HitPts, 0, 4u);
                        Desc.NameOverride = 0;
                        Desc.Field_0x44 = 1;
                        Desc.bTemporary = 0;
                        Desc.bMarkedBy = 0;
                        Desc.Pad47 = 0;
                    }
                }
                Request.Items = Items;
                unsigned char At = 0;
                for (std::list<FRpcServerLootRecord>::const_iterator It = Loot.begin();
                    It != Loot.end(); ++It, ++At) {
                    const FRpcServerLootRecord& Record = *It;
                    FServerLootDesc& Desc = Items[At];
                    Desc.LootId = Record.LootId;
                    Desc.ContentsNickname = Record.ContentsNickname;
                    std::memcpy(&Desc.ContentsHitPts, &Record.ContentsHitPts, 4u);
                    Desc.Units = Record.Units;
                    Desc.ContainerNickname = Record.ContainerNickname;
                    std::memcpy(&Desc.HitPts, &Record.HitPts, 4u);
                    Desc.NameOverride = Record.NameOverride;
                    std::memcpy(&Desc.Position, &Record.Position, sizeof(Desc.Position));
                    std::memcpy(&Desc.Velocity, &Record.Velocity, sizeof(Desc.Velocity));
                    std::memcpy(&Desc.Orientation, &Record.Orientation, sizeof(Desc.Orientation));
                    Desc.bMarkedBy = Record.bMarkedBy;
                    Desc.bTemporary = Record.bTemporary;
                }
                Client->SendServerCreateLoot(1u, Request);
                ::operator delete(Items);
                break;
            }
            case 0x29: // FLPACKET_SERVER_SETREPUTATION, RS 06bd14bf
            {
                // FServerSetReputationInfo with both FmtStrs zeroed inline (EH states 5/6),
                // Relations = 0, the count byte 0, DisplayName[0] = 0; a stack relation
                // list (state 7). VibeHandle = +0x58, Affiliation = +0x5c, Attitude = +0x30
                // (integer moves); FmtStr::unflatten of both blocks; wcscpy of the display
                // name when there is one; the packet's relations deep-copied into the
                // stack list (06bddef0); SendServerSetReputation(1, &info); the array freed when the count
                // byte is non-zero; the stack list's nodes, then NameB, NameA. Retail
                // leaves +0x0c, the pad bytes and the rest of DisplayName uninitialised
                // (nondeterministic); the port zeroes them.
                const FRpcServerSetReputationPacket* Typed =
                    static_cast<const FRpcServerSetReputationPacket*>(Packet);
                FServerSetReputationInfo Info;
                Info.Relations = nullptr;
                Info.RelationCount = 0;
                Info.Field_0x0c = 0;
                Info.Pad_0x15[0] = 0;
                Info.Pad_0x15[1] = 0;
                Info.Pad_0x15[2] = 0;
                for (unsigned int Index = 0; Index < 24u; ++Index) {
                    Info.DisplayName[Index] = L'\0';
                }
                std::list< ::Reputation::Relation*> Local;
                Info.VibeHandle = static_cast<int>(Typed->Target);
                Info.Field_0x08 = Typed->Affiliation;
                std::memcpy(&Info.Attitude, &Typed->Attitude, 4u);
                Info.NameA.unflatten(Typed->NameA.Data, Typed->NameA.Size);
                Info.NameB.unflatten(Typed->NameB.Data, Typed->NameB.Size);
                if (Typed->DisplayName != nullptr) {
                    std::wcscpy(Info.DisplayName, Typed->DisplayName);
                } else {
                    Info.DisplayName[0] = L'\0';
                }
                RpcServerRelationListAppendCopy(Local, Typed->Relations);
                Info.RelationCount = static_cast<unsigned char>(Local.size());
                if (Local.size() > 0) {
                    Info.Relations = RpcServerRelationArrayFromList(Local);
                } else {
                    Info.Relations = nullptr;
                }
                Client->SendServerSetReputation(1u, Info);
                if (Info.RelationCount > 0) {
                    ::operator delete(Info.Relations);
                }
                break;
            }
            case 0x2a: // FLPACKET_SERVER_ADJUSTATTITUDE, RS 06bd1407
            {
                // Stack copy {dword, float}; the float moves through FLD/FSTP.
                const FRpcServerAdjustAttitudePacket* Typed =
                    static_cast<const FRpcServerAdjustAttitudePacket*>(Packet);
                FPayloadServerAdjustAttitude Local;
                Local.Field_0x00 = Typed->Field_0x00;
                RpcServerX87CopyFloat(&Local.Field_0x04, &Typed->Field_0x04);
                Client->SendServerAdjustAttitude(1u, Local);
                break;
            }
            case 0x2b: // FLPACKET_SERVER_SETGROUPFEELINGS, RS 06bd16ab
            {
                // A stack relation list (EH state 9); strncpy(info name, +0x20, +0x24)
                // with no bound to the 0x10-byte field (reproduced on purpose); the
                // packet's relations deep-copied into the list (06bdd430); the count byte
                // is the list size; for a non-zero size the array as in 0x29 (06bd1706);
                // SendServerSetGroupFeelings(1, &info); the list's nodes freed. The array is never freed
                // after the call (a retail leak, reproduced on purpose). Retail leaves
                // Relations uninitialised for an empty list and the pad bytes always
                // (nondeterministic); the port zeroes them.
                const FRpcServerSetGroupFeelingsPacket* Typed =
                    static_cast<const FRpcServerSetGroupFeelingsPacket*>(Packet);
                FServerSetGroupFeelingsInfo Info;
                std::list< ::Reputation::Relation*> Local;
                std::memset(Info.Name, 0, sizeof(Info.Name)); // bytes past the copy: retail garbage
                std::strncpy(Info.Name, Typed->Name, Typed->NameSize);
                RpcServerRelationListAppendCopy(Local, Typed->Relations);
                Info.RelationCount = static_cast<unsigned char>(Local.size());
                Info.Relations = nullptr;
                Info.Pad_0x15[0] = 0;
                Info.Pad_0x15[1] = 0;
                Info.Pad_0x15[2] = 0;
                if (Local.size() > 0) {
                    Info.Relations = RpcServerRelationArrayFromList(Local);
                }
                Client->SendServerSetGroupFeelings(1u, Info);
                break;
            }
            case 0x2c: // FLPACKET_SERVER_CREATEMINE, RS 06bd1bb1
            {
                // Stack FServerCreateProjectileInfo, every field an integer move off the
                // packet: info +0x00..+0x08 = +0x20..+0x28, word +0x0c = +0x2c, Position
                // = +0x40, Orientation = +0x30, Velocity = +0x4c, +0x38 = +0x58, +0x44 =
                // +0x64. Retail leaves Pad0e uninitialised (nondeterministic); the port
                // zeroes it. The 1 goes BY VALUE (PUSH 1, 06bd1c50).
                const FRpcServerCreateMinePacket* Typed =
                    static_cast<const FRpcServerCreateMinePacket*>(Packet);
                FServerCreateProjectileInfo Info;
                Info.ObjId = Typed->ObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                Info.Pad0e = 0;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Velocity, &Typed->Velocity, sizeof(Info.Velocity));
                std::memcpy(&Info.Field_0x38, &Typed->Field_0x38, sizeof(Info.Field_0x38));
                Info.Field_0x44 = Typed->Field_0x44;
                Client->SendServerCreateMine(1u, Info);
                break;
            }
            case 0x2d: // FLPACKET_SERVER_CREATECOUNTER, RS 06bd1dd9
            {
                // Stack FServerCreateProjectileInfo, every field an integer move off the
                // packet. Pad0e (+0x0e..+0x0f) stays unwritten as in retail (06bd1dd9..
                // 06bd1e95); the game reader (0053cd70) never reads it. Client id 1 goes
                // BY REFERENCE (stack dword, 06bd1e7d).
                const FRpcServerCreateCounterPacket* Typed =
                    static_cast<const FRpcServerCreateCounterPacket*>(Packet);
                FServerCreateProjectileInfo Info;
                Info.ObjId = Typed->ObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Velocity, &Typed->Velocity, sizeof(Info.Velocity));
                std::memcpy(&Info.Field_0x38, &Typed->Field_0x3c, sizeof(Info.Field_0x38));
                Info.Field_0x44 = Typed->Field_0x08;
                unsigned int One = 1u;
                Client->SendServerCreateCounter(One, Info);
                break;
            }
            case 0x2e: // FLPACKET_SERVER_SETADDITEM, RS 06bd1add
            {
                // Common EquipDesc ctor, then the setters in native order. The Mission
                // byte is a raw MOV into EquipDesc +0x18 (06bd1b32/35), not a bool test.
                const FRpcServerSetAddItemPacket* Typed =
                    static_cast<const FRpcServerSetAddItemPacket*>(Packet);
                EquipDesc Desc;
                Desc.set_arch_id(Typed->ArchId);
                Desc.set_id(static_cast<unsigned short>(Typed->Id));
                Desc.set_count(Typed->Count);
                Desc.set_status(Typed->Health);
                Desc.set_equipped(Typed->bEquipped);
                std::memcpy(&Desc.bMission, &Typed->bMission, 1u);
                if (Typed->HardpointSize != 0) {
                    CacheString Interned;
                    Interned.Value = StringAlloc(Typed->Hardpoint, false);
                    Desc.set_hardpoint(Interned);
                } else {
                    Desc.make_internal();
                }
                Client->SendServerSetAddItem(1u, static_cast<int>(Typed->Field_0x0c), Desc);
                break;
            }
            case 0x2f: // FLPACKET_SERVER_SETREMOVEITEM, RS 06bd1b90
            {
                const FRpcServerSetRemoveItemPacket* Typed =
                    static_cast<const FRpcServerSetRemoveItemPacket*>(Packet);
                Client->SendServerSetRemoveItem(1u, static_cast<int>(Typed->Field_0x08),
                    static_cast<unsigned short>(Typed->Field_0x00), static_cast<int>(Typed->Field_0x04));
                break;
            }
            case 0x30: // FLPACKET_SERVER_SETCASH, RS 06bd1f9a
            {
                // The client id goes BY REFERENCE: a stack dword 1 (06bd1fab).
                unsigned int One = 1u;
                Client->SendServerSetCash(One,
                    static_cast<int>(static_cast<const FRpcServerSetCashPacket*>(Packet)->Value));
                break;
            }
            case 0x31: // FLPACKET_SERVER_EXPLODEASTEROIDMINE, RS 06bd1c5d
            {
                // Stack copy of the four dwords (integer moves),
                // SendServerExplodeAsteroidMine(1, &local).
                const FRpcServerExplodeAsteroidMinePacket* Typed =
                    static_cast<const FRpcServerExplodeAsteroidMinePacket*>(Packet);
                FMineExplosion Info;
                Info.MineId = Typed->Field_0x00;
                std::memcpy(&Info.Offset.x, &Typed->Position.x, 4u);
                std::memcpy(&Info.Offset.y, &Typed->Position.y, 4u);
                std::memcpy(&Info.Offset.z, &Typed->Position.z, 4u);
                Client->SendServerExplodeAsteroidMine(1u, Info);
                break;
            }
            case 0x32: // FLPACKET_SERVER_REQUESTSPACESCRIPT, RS 06bd1d6d
            {
                // Stack record: +0x000 = [+0x20], +0x108..+0x110 = [+0x28..+0x30], +0x114 =
                // ([+0x34] == 0) + 1 (SETE, INC), then a byte loop copies Name including
                // its NUL when Name is non-null (06bd1dad JZ, 06bd1db5..06bd1dbd). The rest
                // of the name buffer, and all of it when Name is null, stays unwritten as in
                // retail. On this receiver Name is always null (the RS decoder 06bf48f0
                // never allocates it), so the game's SendServerRequestSpaceScript (0053d9d0) hands the uninitialised
                // buffer to the space-script camera: nondeterministic, reproduced on purpose.
                const FRpcServerRequestSpaceScriptPacket* Typed =
                    static_cast<const FRpcServerRequestSpaceScriptPacket*>(Packet);
                FServerRequestSpaceScriptInfo Info;
                Info.Field_0x000 = Typed->Field_0x00;
                Info.Field_0x108 = Typed->Position[0];
                Info.Field_0x10c = Typed->Position[1];
                Info.Field_0x110 = Typed->Position[2];
                Info.Field_0x114 = (Typed->Mode == 0) ? 2u : 1u;
                if (Typed->Name != nullptr) {
                    unsigned int i = 0;
                    do {
                        Info.Name[i] = Typed->Name[i];
                    } while (Typed->Name[i++] != '\0');
                }
                Client->SendServerRequestSpaceScript(1u, Info);
                break;
            }
            case 0x33: // FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE, RS 06bd24cf
            {
                // Stack {dword +0x20, byte of +0x24, MOVZX word +0x28 as a dword}. Retail
                // leaves bytes +5..+7 uninitialised (nondeterministic); the port zeroes
                // them. Client id 1 BY REFERENCE (stack dword, 06bd24f7).
                const FRpcServerSetMissionObjectiveStatePacket* Typed =
                    static_cast<const FRpcServerSetMissionObjectiveStatePacket*>(Packet);
                FServerMissionObjectiveStateInfo Info;
                Info.Field_0x00 = Typed->Field_0x00;
                Info.Field_0x04 = static_cast<unsigned char>(Typed->Field_0x04);
                Info.Pad_0x05[0] = 0;
                Info.Pad_0x05[1] = 0;
                Info.Pad_0x05[2] = 0;
                Info.Field_0x08 = Typed->Field_0x08;
                unsigned int One = 1u;
                Client->SendServerSetMissionObjectiveState(One, Info);
                break;
            }
            case 0x34: // FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE, RS 06bd2584
            {
                // Stack record: +0x00 = [+0x20], the FmtStr at +0x04 zeroed inline, byte
                // +0x20 = [+0x24]; FmtStr::unflatten(TextData, TextSize) under EH state
                // 0x12 (the FmtStr is RAII here); stack One = 1 BY REFERENCE; ~FmtStr.
                // Retail leaves +0x1c and the pad bytes uninitialised (nondeterministic);
                // the port zeroes them.
                const FRpcServerReplaceMissionObjectivePacket* Typed =
                    static_cast<const FRpcServerReplaceMissionObjectivePacket*>(Packet);
                FServerReplaceMissionObjectiveInfo Info;
                Info.Field_0x00 = Typed->Field_0x00;
                Info.ObjectiveType = 0;
                Info.State = static_cast<unsigned char>(Typed->State);
                Info.Pad_0x21[0] = 0;
                Info.Pad_0x21[1] = 0;
                Info.Pad_0x21[2] = 0;
                Info.Text.unflatten(Typed->TextData, Typed->TextSize);
                unsigned int One = 1u;
                Client->SendServerReplaceMissionObjective(One, Info);
                break;
            }
            case 0x35: // FLPACKET_SERVER_SETMISSIONOBJECTIVES, RS 06bd27b9
            {
                // Stack record with both FmtStrs zeroed inline and unflattened (A, then B)
                // under EH state 0x14; Count = [+0x38], byte +0x3c = [+0x24]. When Count
                // is non-zero: vector new of Count 0x1c-byte entries (FmtStr ctor per
                // element), then per entry the packet's i-th record (06bdac30, no null
                // test): State = MOVZX byte, FmtStr::unflatten(Data, Size).
                // SendServerSetMissionObjectives(&One, &record); then, when Count and Entries
                // are non-zero, vector delete.
                // Retail leaves Entries uninitialised when Count is 0; the port uses null.
                const FRpcServerSetMissionObjectivesPacket* Typed =
                    static_cast<const FRpcServerSetMissionObjectivesPacket*>(Packet);
                FServerSetMissionObjectivesInfo Info;
                Info.Field_0x00 = Typed->Field_0x00;
                Info.Entries = nullptr;
                Info.TextA.unflatten(Typed->TextA.Data, Typed->TextA.Size);
                Info.TextB.unflatten(Typed->TextB.Data, Typed->TextB.Size);
                Info.Field_0x3c = static_cast<unsigned char>(Typed->State);
                Info.Pad_0x3d[0] = 0;
                Info.Pad_0x3d[1] = 0;
                Info.Pad_0x3d[2] = 0;
                Info.Count = Typed->Count;
                if (Info.Count != 0) {
                    Info.Entries = new (std::nothrow) FServerMissionObjectiveEntry[Info.Count];
                    for (unsigned int Index = 0; Index < Info.Count; ++Index) {
                        const FRpcServerMissionObjectiveRecord* Record = Typed->ObjectiveAt(Index);
                        Info.Entries[Index].State = Record->State;
                        Info.Entries[Index].Text.unflatten(Record->Data, Record->Size);
                    }
                }
                unsigned int One = 1u;
                Client->SendServerSetMissionObjectives(One, Info);
                if (Info.Count != 0 && Info.Entries != nullptr) {
                    delete[] Info.Entries;
                }
                break;
            }
            case 0x37: // FLPACKET_SERVER_CREATEGUIDED, RS 06bd1e96
            {
                // Stack FServerCreateGuidedInfo, every field an integer move off the
                // packet. Pad26/Pad36 (+0x26..+0x27, +0x36..+0x37) stay unwritten as in
                // retail (06bd1e96..06bd1f2c); the game reader (0053d080) never reads them.
                // Field_0x2c comes from +0x20, which never crosses the wire (the decoder
                // zeroes it): always 0, reproduced on purpose.
                const FRpcServerCreateGuidedPacket* Typed =
                    static_cast<const FRpcServerCreateGuidedPacket*>(Packet);
                FServerCreateGuidedInfo Info;
                Info.ObjId = Typed->ObjId;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                Info.TargetId = Typed->TargetId;
                Info.TargetSubObjId = Typed->TargetSubObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.Field_0x2c = Typed->Field_0x00;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                unsigned int One = 1u; // stack dword passed by reference (06bd1f14)
                Client->SendServerCreateGuided(One, Info);
                break;
            }
            case 0x38: // FLPACKET_SERVER_ITEMTRACTORED, RS 06bd1f2d
            {
                // Stack {dword, dword, word, word}, SendServerItemTractored(1, &local).
                const FRpcServerItemTractoredPacket* Typed =
                    static_cast<const FRpcServerItemTractoredPacket*>(Packet);
                FPayloadServerItemTractored Info;
                Info.Field_0x00 = Typed->Field_0x00;
                Info.Field_0x04 = Typed->Field_0x04;
                Info.Field_0x08 = Typed->Field_0x08;
                Info.Field_0x0a = Typed->Field_0x0a;
                Client->SendServerItemTractored(1u, Info);
                break;
            }
            case 0x39: // FLPACKET_SERVER_SCANNOTIFY, RS 06bd1ac0
            {
                const FRpcServerScanNotifyPacket* Typed = static_cast<const FRpcServerScanNotifyPacket*>(Packet);
                Client->SendServerScanNotify(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case 0x3c: // FLPACKET_SERVER_REPAIROBJECT, RS 06bd2c45
            {
                // Stack {dword +0x20, word of +0x24, dword +0x28}, SendServerRepairObject(1, &local).
                // Retail leaves the pad word at +0x06 uninitialised (nondeterministic);
                // the port zeroes it.
                const FRpcServerRepairObjectPacket* Typed =
                    static_cast<const FRpcServerRepairObjectPacket*>(Packet);
                FServerRepairObjectInfo Info;
                Info.Field_0x00 = Typed->ObjectId;
                Info.Field_0x04 = static_cast<unsigned short>(Typed->EquipId);
                Info.Pad_0x06 = 0;
                std::memcpy(&Info.Field_0x08, &Typed->Points, 4u);
                Client->SendServerRepairObject(1u, Info);
                break;
            }
            case 0x3d: // FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE, RS 06bd2b04
            {
                // The vector is built first (EH state 0x17), the handle second (0x18);
                // 06bdaa80 extracts, 06bd5130 converts; stack One = 1 BY REFERENCE,
                // ObjectId +0x28, &vector, MOVZX byte +0x24; the handle is destroyed first
                // (06bdc790), then the vector's block.
                const FRpcServerRemoteObjectCargoUpdatePacket* Typed =
                    static_cast<const FRpcServerRemoteObjectCargoUpdatePacket*>(Packet);
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                Typed->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                unsigned int One = 1u;
                Client->SendServerRemoteObjectCargoUpdate(One, Typed->ObjectId, Vector, Typed->Flag);
                break;
            }
            case 0x3e: // FLPACKET_SERVER_SETNUMKILLS, RS 06bd2c76
            {
                unsigned int One = 1u; // stack dword passed by reference (06bd2c87)
                Client->SendServerSetNumKills(One,
                    static_cast<int>(static_cast<const FRpcServerSetNumKillsPacket*>(Packet)->Value));
                break;
            }
            case 0x3f: // FLPACKET_SERVER_SETMISSIONSUCCESSES, RS 06bd2ca0
            {
                unsigned int One = 1u; // stack dword passed by reference (06bd2cb1)
                Client->SendServerSetMissionSuccesses(One,
                    static_cast<int>(static_cast<const FRpcServerSetMissionSuccessesPacket*>(Packet)->Value));
                break;
            }
            // ===== end SERVER 0x20..0x3f (RS-P2) =====
            // ===== SERVER 0x40..0x5b (RS-P3) =====
            // Cases of HandlePacket 06bd1260 (switch 06bd12b1). Where retail passes &one,
            // the 1 is a stack local written right before the call, as in the listing.
            case 0x40: // SETMISSIONFAILURES, caseD_40 06bd2cca
            {
                unsigned int One = 1u;
                Client->SendServerSetMissionFailures(One,
                    static_cast<int>(static_cast<FRpcServerSetMissionFailuresPacket*>(Packet)->Value));
                break;
            }
            case 0x41: // BURNFUSE, caseD_41 06bd2cf4: integer copies into a stack record
            {
                const FRpcServerBurnFusePacket* Typed = static_cast<FRpcServerBurnFusePacket*>(Packet);
                FServerBurnFuseInfo Info = {};
                Info.SourceId = Typed->SourceId;
                Info.ObjectId = Typed->ObjectId;
                Info.FuseId = Typed->FuseId;
                Info.SubObjId = Typed->SubObjId;
                std::memcpy(&Info.AtT, &Typed->AtT, 4u);
                std::memcpy(&Info.Lifetime, &Typed->Lifetime, 4u);
                Info.bLit = Typed->bLit;
                unsigned int One = 1u;
                Client->SendServerBurnFuse(One, Info);
                break;
            }
            case 0x42: // CREATESOLAR, caseD_42 06bd2d52
            {
                // A function-local static CSolarCreateInfo (guard bit 8 of 06c138b8, inline
                // ctor, atexit destructor 06bd4370) filled by the block reader 06bd52a0, then
                // the two bytes. Outside single player two FmtStr locals (A, then B; B is
                // destroyed first) are unflattened from the packet's blocks and the vibe is
                // created, named, affiliated and given its attitude (the float pushed as is).
                // SendServerCreateSolar(1, &static), then the inlined CEqObjCreateInfo::clear()
                // (06bd3050..).
                FRpcServerCreateSolarPacket* Typed = static_cast<FRpcServerCreateSolarPacket*>(Packet);
                static CSolarCreateInfo Static;
                RpcServerCreateObjectRead(&Typed->Fields, &Static);
                Static.Field_0xad = Typed->Field_0x9c;
                Static.bMarkedBy = Typed->bMarkedBy;
                if (!SinglePlayer()) {
                    FmtStr NameA;
                    FmtStr NameB;
                    NameA.unflatten(Typed->NameA, Typed->NameASize);
                    NameB.unflatten(Typed->NameB, Typed->NameBSize);
                    Reputation::Vibe::EnsureExists(Typed->Fields.VibeGroupId);
                    Reputation::Vibe::SetName(Typed->Fields.VibeGroupId, NameA, NameB, nullptr);
                    Reputation::Vibe::SetAffiliation(Typed->Fields.VibeGroupId, Typed->Affiliation, false);
                    Reputation::Vibe::SetAttitudeTowardsPlayer(Typed->Fields.VibeGroupId, Typed->Attitude);
                }
                Client->SendServerCreateSolar(1u, Static);
                Static.CEqObjCreateInfo::clear();
                break;
            }
            case 0x43: // SET_STORY_CUE, caseD_43 06bd2631: the cue id by address
            {
                unsigned int CueId = static_cast<FRpcServerSetStoryCuePacket*>(Packet)->Value;
                unsigned int One = 1u;
                Client->SendServerSetStoryCue(One, CueId);
                break;
            }
            case 0x44: // REQUEST_RETURNED, caseD_44 06bd1a08: MOVZX +0x24, MOVSX +0x25/+0x26
            {
                const FRpcServerRequestReturnedPacket* Typed =
                    static_cast<FRpcServerRequestReturnedPacket*>(Packet);
                unsigned int One = 1u;
                Client->SendServerRequestReturned(One, Typed->ObjectId,
                    static_cast<int>(Typed->RequestType),
                    static_cast<int>(static_cast<signed char>(Typed->Response)),
                    static_cast<int>(static_cast<signed char>(Typed->Field_0x26)));
                break;
            }
            case 0x45: // SET_MISSION_MESSAGE, caseD_45 06bd2713 (EH state 0x13 owns the FmtStr)
            {
                FRpcServerSetMissionMessagePacket* Typed = static_cast<FRpcServerSetMissionMessagePacket*>(Packet);
                CSetMissionMessage Info;
                Info.Text.unflatten(Typed->TextData, Typed->TextSize);
                Info.Type = Typed->Type;
                unsigned int One = 1u;
                Client->SendServerSetMissionMessage(One, Info);
                break;
            }
            case 0x46: // MARKOBJ, caseD_46 06bd2510: MOVZX of the mark byte
            {
                const FRpcServerMarkObjPacket* Typed = static_cast<FRpcServerMarkObjPacket*>(Packet);
                unsigned int One = 1u;
                Client->SendServerMarkObj(One, Typed->ObjectId, static_cast<int>(Typed->MarkType));
                break;
            }
            case 0x47: // CFGINTERFACENOTIFICATION, caseD_47 06bd253f: TEST/SETNZ of the byte
            {
                const FRpcServerCfgInterfaceNotificationPacket* Typed =
                    static_cast<FRpcServerCfgInterfaceNotificationPacket*>(Packet);
                FPayloadServerCfgInterfaceNotification Info = {};
                Info.Field_0x00 = Typed->NotifType;
                Info.Field_0x04 = Typed->Param;
                Info.Field_0x08 = static_cast<unsigned char>(Typed->bEnable != 0 ? 1 : 0);
                unsigned int One = 1u;
                Client->SendServerCfgInterfaceNotification(One, Info);
                break;
            }
                        case 0x48:
            {
                // SETCOLLISIONGROUPS, caseD_48 06bd3102: the packet's own list (accessor
                // 06bda780 = &packet+0x20), no copy; SendServerSetCollisionGroups(1, &list).
                FRpcServerSetCollisionGroupsPacket* Typed =
                    static_cast<FRpcServerSetCollisionGroupsPacket*>(Packet);
                Client->SendServerSetCollisionGroups(1u, Typed->Groups);
                break;
            }
            case 0x49: // SETHULLSTATUS: the float's dword pushed as is
            {
                // Bit copy, not an x87 load: retail pushes the packet's dword unchanged.
                float HullStatus;
                std::memcpy(&HullStatus, &static_cast<FRpcSetHullStatusPacket*>(Packet)->Value, 4u);
                Client->SendServerSetHullStatus(1u, HullStatus);
                break;
            }
            case 0x4a: // SETGUIDEDTARGET, caseD_4a 06bd313e: the word zero-extended
            {
                const FRpcServerSetGuidedTargetPacket* Typed =
                    static_cast<FRpcServerSetGuidedTargetPacket*>(Packet);
                Client->SendServerSetGuidedTarget(1u, Typed->Field_0x20, Typed->Field_0x24,
                    Typed->Field_0x28);
                break;
            }
            case 0x4b: // SET_CAMERA, caseD_4b 06bd2663: the quaternion through FLD/FSTP
            {
                const FRpcServerSetCameraPacket* Typed = static_cast<FRpcServerSetCameraPacket*>(Packet);
                FPayloadServerSetCamera Info;
                std::memcpy(&Info.Position.x, &Typed->Position.x, 4u);
                std::memcpy(&Info.Position.y, &Typed->Position.y, 4u);
                std::memcpy(&Info.Position.z, &Typed->Position.z, 4u);
                RpcServerX87CopyFloat(&Info.Orient.w, &Typed->Orientation.w);
                RpcServerX87CopyFloat(&Info.Orient.x, &Typed->Orientation.x);
                RpcServerX87CopyFloat(&Info.Orient.y, &Typed->Orientation.y);
                RpcServerX87CopyFloat(&Info.Orient.z, &Typed->Orientation.z);
                std::memcpy(&Info.Fov, &Typed->Fov, 4u);
                std::memcpy(&Info.Aspect, &Typed->Aspect, 4u);
                unsigned int One = 1u;
                Client->SendServerSetCamera(One, Info);
                break;
            }
            case 0x4c: // REVERT_CAMERA, caseD_4c 06bd26f3
            {
                unsigned int One = 1u;
                Client->SendServerRevertCamera(One);
                break;
            }
            case 0x4d: // LOADHINT, caseD_4d 06bd1c93 (EH state 0xe while the hint is built)
            {
                // Only Kind 1 builds a hint: operator new(0x3c), COMMON's CostumeHint ctor and
                // this module's vftable 06c0b790 (FRpcLocalCostumeHint), then UnSerialize
                // through the vtable with no null test (reproduced on purpose).
                FRpcServerLoadHintPacket* Typed = static_cast<FRpcServerLoadHintPacket*>(Packet);
                CostumeHint* Hint = nullptr;
                if (Typed->Kind == 1) {
                    Hint = new (std::nothrow) FRpcLocalCostumeHint;
                    Hint->UnSerialize(Typed->Data, Typed->Size);
                }
                // 06bd1cfa PUSH ESI: the hint pointer goes out even when it is null (any Kind
                // but 1), the literal 1 by reference (06bd1cfb..06bd1d0f). No implementer
                // null-tests it; the game's LAB_0053de70 reads Kind through it first.
                unsigned int One = 1u;
                Client->SendServerLoadHint(One, Hint);
                if (Hint != nullptr) {
                    delete Hint;
                }
                break;
            }
            case 0x4e: // SETDIRECTIVE, caseD_4e 06bd1d2b
            {
                // The local is seeded by 06bd4230 (+0x18 = -1, +0x40 = 3, the rest 0) and then
                // wholly overwritten by REP MOVSD of 0x25 dwords from packet +0x20, padding
                // included; the seeding has no observable effect and is not repeated here.
                FRpcServerSetDirectivePacket* Typed = static_cast<FRpcServerSetDirectivePacket*>(Packet);
                FPayloadServerSetDirective Info;
                std::memcpy(&Info, &Typed->Directive, 0x94u);
                unsigned int One = 1u;
                Client->SendServerSetDirective(One, Info);
                break;
            }
            case 0x4f: // SENDCOMM, caseD_4f 06bd315c
            {
                // The costume goes through a stack copy (accessor 06bd4300, REP MOVSD of 13
                // dwords) and a second REP MOVSD onto the argument stack; the other values come
                // from one-instruction accessors 06bd42d0..06bd4350: the array pointer as is,
                // the count by MOVSX. The float leaves 06bd4360 on the x87 stack and is stored
                // by FSTP dword; bReceiverIsOwnShip is the literal 0.
                FRpcServerSendCommPacket* Typed = static_cast<FRpcServerSendCommPacket*>(Packet);
                Costume Local;
                std::memcpy(&Local, &Typed->Body, sizeof(Costume));
                float Level;
                RpcServerX87CopyFloat(&Level, &Typed->Field_0x70);
                Client->SendServerSendComm(1u, Typed->Field_0x20, Typed->Field_0x24, Typed->Field_0x28,
                    Local, Typed->Field_0x60, Typed->Field_0x64,
                    static_cast<int>(Typed->Field_0x68), Typed->Field_0x6c, Level, false);
                break;
            }
            case 0x51: // USE_ITEM, caseD_51 06bd1f67: a stack FPayloadServerUseItem
            {
                const FRpcServerUseItemPacket* Typed = static_cast<FRpcServerUseItemPacket*>(Packet);
                FPayloadServerUseItem Info;
                Info.Field_0x00 = Typed->ObjectId;
                Info.Field_0x04 = Typed->EquipId;
                Info.Field_0x06 = Typed->Count;
                Client->SendServerUseItem(1u, Info);
                break;
            }
                        case 0x52:
            {
                // PLAYERLIST, caseD_52 06bd31f2: Kind 0 -> SendServerPlayerListClear(1);
                // 1 -> SendServerPlayerListAdd(1, Name, PlayerId, SETNZ byte +0x2c);
                // 2 -> SendServerPlayerListRemove(1, PlayerId); anything else nothing.
                const FRpcServerPlayerListPacket* Typed =
                    static_cast<FRpcServerPlayerListPacket*>(Packet);
                if (Typed->Kind == kPlayerListKindClear) {
                    Client->SendServerPlayerListClear(1u);
                    break;
                }
                if (Typed->Kind == kPlayerListKindEntry) {
                    Client->SendServerPlayerListAdd(1u, Typed->Name, Typed->PlayerId,
                        Typed->Field_0x2c != 0);
                    break;
                }
                if (Typed->Kind == kPlayerListKindDrop) {
                    Client->SendServerPlayerListRemove(1u, Typed->PlayerId);
                    break;
                }
                break;
            }
            case 0x53: // FORMATION_UPDATE, caseD_53 06bd142f: Offset through FLD/FSTP
            {
                const FRpcServerFormationUpdatePacket* Typed =
                    static_cast<FRpcServerFormationUpdatePacket*>(Packet);
                Vector Local;
                RpcServerX87CopyFloat(&Local.x, &Typed->Offset.x);
                RpcServerX87CopyFloat(&Local.y, &Typed->Offset.y);
                RpcServerX87CopyFloat(&Local.z, &Typed->Offset.z);
                Client->SendServerFormationUpdate(1u, Typed->LeaderId, Local);
                break;
            }
            case 0x54: // MISCOBJUPDATE, caseD_54 06bd325d
            {
                // Flags is re-read from the packet for every test except the 0x01 / 0x04 pair,
                // which tests one snapshot. The text and the vector go by address into the
                // packet; the word +0x2c by MOVZX.
                FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
                if ((Typed->Flags & 0x20u) != 0) {
                    Client->SendServerMiscObjUpdateAffiliation(1u, Typed->Field_0x28, Typed->Field_0x54);
                }
                const unsigned short Snapshot = Typed->Flags;
                if ((Snapshot & 0x01u) != 0) {
                    if ((Snapshot & 0x04u) != 0) {
                        Client->SendServerMiscObjUpdatePlayerGroup(1u, Typed->Field_0x24, Typed->Field_0x34);
                    } else {
                        Client->SendServerMiscObjUpdateObjectGroup(1u, Typed->Field_0x28, Typed->Field_0x34);
                    }
                }
                if ((Typed->Flags & 0x02u) != 0) {
                    Client->SendServerMiscObjUpdateMission(1u, Typed->Field_0x38, Typed->Field_0x3c);
                }
                if ((Typed->Flags & 0x10u) != 0) {
                    Client->SendServerMiscObjUpdateMotd(1u, Typed->Text);
                }
                if ((Typed->Flags & 0x40u) != 0) {
                    Client->SendServerMiscObjUpdateRank(1u, Typed->Field_0x24, static_cast<int>(Typed->Field_0x2c));
                }
                if ((Typed->Flags & 0x80u) != 0) {
                    Client->SendServerMiscObjUpdateSystem(1u, Typed->Field_0x24, Typed->Field_0x30);
                }
                if ((Typed->Flags & 0x100u) != 0) {
                    Client->SendServerMiscObjUpdateFormation(1u, Typed->Ids);
                }
                break;
            }
            case 0x55: // OBJECTCARGOUPDATE, caseD_55 06bd2bad: Common EquipDesc setters
            {
                const FRpcServerObjectCargoUpdatePacket* Typed =
                    static_cast<FRpcServerObjectCargoUpdatePacket*>(Packet);
                EquipDesc Desc;
                Desc.set_id(Typed->EquipId);
                Desc.set_arch_id(Typed->ArchId);
                Desc.set_count(static_cast<int>(Typed->Count));
                Desc.set_status(Typed->Status);
                Desc.make_internal();
                Desc.set_equipped(0);
                Desc.bMission = (Typed->bMission != 0);
                unsigned int One = 1u;
                Client->SendServerObjectCargoUpdate(One, Typed->ObjectId, Desc);
                break;
            }
            case 0x56: // SENDNNMESSAGE
            {
                Client->SendServerSendNNMessage(1u, static_cast<FRpcServerSendNNMessagePacket*>(Packet)->Value);
                break;
            }
            case 0x57: // SET_MUSIC, caseD_57 06bd1462: MOVZX of the byte, the float's dword as is
            {
                const FRpcServerSetMusicPacket* Typed = static_cast<FRpcServerSetMusicPacket*>(Packet);
                // Bit copy, not an x87 load: retail pushes the packet's dword unchanged.
                float Fade;
                std::memcpy(&Fade, &Typed->Field_0x34, 4u);
                // The wire byte as the bool: the callee keeps one byte (rpclocal 06d8c6ad),
                // so only values above 1 would differ.
                Client->SendServerSetMusic(1u, Typed->Field_0x20, Typed->Field_0x24, Typed->Field_0x28,
                    Typed->Field_0x2c, Typed->Field_0x30 != 0, Fade);
                break;
            }
            case 0x58: // CANCEL_MUSIC
            {
                Client->SendServerCancelMusic(1u);
                break;
            }
            case 0x59: // PLAY_SOUND_EFFECT
            {
                Client->SendServerPlaySoundEffect(1u, static_cast<FRpcServerPlaySoundEffectPacket*>(Packet)->Value);
                break;
            }
            case 0x5a: // GFMISSIONVENDORWHYEMPTY, caseD_5a 06bd2aea: MOVZX of the byte
            {
                Client->SendServerGFMissionVendorWhyEmpty(1u, static_cast<unsigned int>(
                    static_cast<FRpcServerGfMissionVendorWhyEmptyPacket*>(Packet)->Value));
                break;
            }
            case 0x5b: // MISSIONSAVEA, caseD_5b 06bd2a40
            {
                Client->SendServerMissionSaveA(1u, static_cast<FRpcServerMissionSaveAPacket*>(Packet)->Value);
                break;
            }
            // ===== end SERVER 0x40..0x5b (RS-P3) =====
            // Opcodes outside 1..0x5b and the jump-table holes go to caseD_9 06bd3c0f, which only
            // releases the packet (06bd3c11); the release is the caller's (IServerImpl::HandlePacket).
            default:
                break;
        }

}
