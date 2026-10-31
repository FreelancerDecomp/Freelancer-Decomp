// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "rpc_packet_codec.h"
#include "common_misc.h"
#include <cstring>
#include <iostream>
#include <list>
#include <string>
#include <vector>

// Internal SERVER-channel seam, compiled separately into the two remote DLLs.
// Legacy packet bodies remain module-specific until their native audit replaces
// them. This extraction does not verify or repair their existing wire behavior.
int RpcRegisterRemoteClientServerPackets(FPacketSerializer* Serializer);
int RpcRegisterRemoteServerServerPackets(FPacketSerializer* Serializer);

// ===== shared SERVER helpers =====
struct FmtStr;

// Preserve the retail SERVER float-field load/store behavior in portable C++:
// bit-exact except that a signaling NaN is quietened.
inline void RpcServerX87CopyFloat(float* Destination, const float* Source) {
    PortableFloatLoad::Copy(Destination, Source);
}

// The length-prefixed block decoder (RC 06b4eb30 / RS 06bec240) and the FmtStr flattened
// size (RC 06b347e0) are rpc_packet_codec.h's RpcWireDecodeOwnedBlock and
// RpcGetFlattenedFmtStrSize.

// Attitude component quantizer: FLD float, FMUL double 127.0, MSVCRT _ftol (truncating
// 64-bit conversion), low byte. Expand is MOVSX byte, FILD dword, FMUL double 1/127,
// FSTP float. Used by 0x37 CREATEGUIDED (RS-P2); same constants as COMMON UPDATEOBJECT.
unsigned char RpcServerQuantize127(const float* Value);
void RpcServerExpand127(unsigned char Encoded, float* Value);

// --- create-object block: 0x04 CREATESHIP and 0x42 CREATESOLAR (RS-P10) ---
// Both packets carry the same block at packet+0x20..+0x9b, built by the inlined base
// constructor (RS 06be5340 for 0x04: +0x20..+0x2c, the +0x30 costume (06be5480), +0x48..
// +0x54, the +0x58 quaternion (06bde8d0), +0x68/+0x6c/+0x70, then Common's
// list<CollisionGroupDesc> ctor at +0x74 and DamageList() at +0x80) and torn down by the
// base part of the destructor (RS 06bf0c79..06bf0d36): the DamageList, the collision
// list, the equipment handle (06bdc790), then the costume's id array.
#include "rpc_equipment_codec.h" // FRpcServerEquipHandle (+0x70)
#include "common_archetype.h"    // CollisionGroupDesc (+0x74)

struct CEqObjCreateInfo;

// packet+0x30..+0x47, the costume sub-object (EH state 1 of the packet constructor).
// RS 06be5480 zeroes the six dwords; the destructor frees a non-null id array
// (RS 06bf0d27..06bf0d31).
struct FRpcServerCreateObjectCostume {
    FRpcServerCreateObjectCostume()
        : Head(0), Body(0), LeftHand(0), RightHand(0), Count(0), Ids(nullptr) {}
    ~FRpcServerCreateObjectCostume() {
        if (Ids != nullptr) ::operator delete(Ids);
    }
    FRpcServerCreateObjectCostume(const FRpcServerCreateObjectCostume&) = delete;
    FRpcServerCreateObjectCostume& operator=(const FRpcServerCreateObjectCostume&) = delete;

    unsigned int Head;      // +0x30 <- CEqObjCreateInfo::CostumeHead (+0x14)
    unsigned int Body;      // +0x34 <- CostumeBody (+0x18)
    unsigned int LeftHand;  // +0x38 <- CostumeLeftHand (+0x1c); never on the wire
    unsigned int RightHand; // +0x3c <- CostumeRightHand (+0x20); never on the wire
    int Count;              // +0x40 <- CostumeAccessoryCount (+0x44)
    unsigned int* Ids;      // +0x44 operator new(Count * 4), CostumeAccessory copies
};

// packet+0x20..+0x9b. Member order is retail's; the containers are this toolchain's
// (no retail ABI).
struct FRpcServerCreateObjectFields {
    FRpcServerCreateObjectFields();
    FRpcServerCreateObjectFields(const FRpcServerCreateObjectFields&) = delete;
    FRpcServerCreateObjectFields& operator=(const FRpcServerCreateObjectFields&) = delete;

    unsigned int ObjId;                 // +0x20 <- CEqObjCreateInfo::ObjId (+0x04)
    unsigned int ArchetypeId;           // +0x24 <- ArchetypeNickname (+0x08)
    unsigned int DockingBaseId;         // +0x28 <- DockingBaseId (+0x0c)
    int VibeGroupId;                    // +0x2c <- VibeGroupId (+0x10)
    FRpcServerCreateObjectCostume Costume; // +0x30
    unsigned int VoiceId;               // +0x48 <- VoiceId (+0x48)
    Vector Position;                    // +0x4c <- Position (+0x4c)
    Quaternion Orientation;             // +0x58 <- Orientation (+0x58), w first
    int HitPts;                         // +0x68 <- HitPts (+0x68), FILD on encode
    unsigned int Field_0x6c;            // +0x6c <- Field_0x6c; travels only in SP
    FRpcServerEquipHandle Equipment;    // +0x70 <- Equipment (+0x70) as wire records
    std::list<CollisionGroupDesc> CollisionGroups; // +0x74 <- CollisionGroups (+0x80)
    DamageList Damage;                  // +0x80 <- Damage (+0x8c); list travels only in SP
};

// RC 06b50e50 / RS 06c08640 (instruction-identical) and RC 06b51750 / RS 06c08f40
// (the RC decoder checks the id array and a hardpoint text run before it allocates
// them, and returns 5 when that allocation fails). Called by each packet's Encode /
// Decode right after its header on the packet's own marshal scratch; retail ignores
// the return and lets the sticky error stop the packet's next field. Encode returns
// 0 or 0x16, Decode 0 or the stream error (5 on the RC allocation failures).
unsigned int RpcServerCreateObjectEncode(FPacketBase& Packet, FRpcWireStream& Stream,
    const FRpcServerCreateObjectFields& Fields);
unsigned int RpcServerCreateObjectDecode(FPacketBase& Packet, FRpcWireStream& Stream,
    FRpcServerCreateObjectFields& Fields);
// RC 06b382f0 (__cdecl, Info then packet): the senders' copy of the create info into
// the block (SendServerCreateSolar 06b34c93, SendServerCreateShip 06b34f13). RemoteClient only.
void RpcServerCreateObjectFill(const CEqObjCreateInfo* Info,
    FRpcServerCreateObjectFields* Fields);
// RS 06bd52a0 (__cdecl, packet then Info): the receivers' copy of the block into the
// case's static create info (case 04 06bd211f, case 42 06bd2e7b). RemoteServer only.
void RpcServerCreateObjectRead(const FRpcServerCreateObjectFields* Fields,
    CEqObjCreateInfo* Info);
// RC 06b4fee0 / RS 06bed590: fills the hardpoint dictionary the create-object codec
// uses (names -> codes 1..42). Retail calls it once, at the end of the packet
// serializer's construction (RC 06b3b087, RS 06be1357).
void RpcServerBuildHardpointDictionary();
// ===== end shared SERVER helpers =====

#if defined(REMOTECLIENT_EXPORTS)

// These two aliases and their registered factories use exactly the types consumed
// by OnConnect and CClientImpl::SendServerLoginResponse; the former lookalike classes were distinct.
using FPacketServerConnectResponse = FRpcConnectResponsePacket;

using FPacketServerLoginResponse = FRpcLoginResponsePacket;

struct FPacketServerRequestReturned : public FPacketBase {
    unsigned int Field_00;
    unsigned char Field_04;
    unsigned char Field_05;
    unsigned char Field_06;

    FPacketServerRequestReturned() : FPacketBase(0x20044), Field_00(0), Field_04(0), Field_05(0), Field_06(0) {}

    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (Capacity < 7) return 0x16;
        unsigned char* Buffer = *BufferHolder;
        *reinterpret_cast<unsigned int*>(Buffer) = Field_00;
        *(Buffer + 4) = Field_04;
        *(Buffer + 5) = Field_05;
        *(Buffer + 6) = Field_06;
        *BufferHolder = Buffer + 7;
        if (OutLength) *OutLength = 7;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        if (Size < 7) return 0x17;
        Field_00 = *reinterpret_cast<const unsigned int*>(Buffer);
        Field_04 = *(Buffer + 4);
        Field_05 = *(Buffer + 5);
        Field_06 = *(Buffer + 6);
        if (OutLength) *OutLength = 7;
        return 0;
    }
};


// Local factory for RemoteClient instead of missing RpcGetPacketSerializer

struct FPacketServerMissionSaveAvailable : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned char* Field_24 = nullptr;

    FPacketServerMissionSaveAvailable() : FPacketBase(0x2005b) {}
    ~FPacketServerMissionSaveAvailable() {
        if (Field_24) delete[] Field_24;
    }

    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (Field_20 > 0) {
            Field_24 = new unsigned char[Field_20];
            if (!ReadBytes(Field_24, Field_20)) return 0x17;
        }

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerGFMissionVendorWhyEmpty : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned char* Field_24 = nullptr;

    FPacketServerGFMissionVendorWhyEmpty() : FPacketBase(0x2005a) {}
    ~FPacketServerGFMissionVendorWhyEmpty() {
        if (Field_24) delete[] Field_24;
    }

    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (Field_20 > 0) {
            Field_24 = new unsigned char[Field_20];
            if (!ReadBytes(Field_24, Field_20)) return 0x17;
        }

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerCfgInterfaceNotification : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned char Field_24 = 0;

    FPacketServerCfgInterfaceNotification() : FPacketBase(0x20047) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (!ReadBytes(&Field_24, 1)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerMarkObj : public FPacketBase {


    FPacketServerMarkObj() : FPacketBase(0x20046) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerSetStoryCue : public FPacketBase {
    unsigned char Field_20[12] = {0};
    unsigned char Field_2C[16] = {0};
    unsigned int Field_3C = 0;
    unsigned int Field_40 = 0;

    FPacketServerSetStoryCue() : FPacketBase(0x20043) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(Field_20, 12)) return 0x17;
        if (!ReadBytes(Field_2C, 16)) return 0x17;
        if (!ReadBytes(&Field_3C, 4)) return 0x17;
        if (!ReadBytes(&Field_40, 4)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};


struct FPacketServerSetCamera : public FPacketBase {


    FPacketServerSetCamera() : FPacketBase(0x2004b) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerRevertCamera : public FPacketBase {
    unsigned int Field_28 = 0;
    unsigned int Field_20 = 0;
    unsigned char* Field_24 = nullptr;

    FPacketServerRevertCamera() : FPacketBase(0x2004c) {}
    ~FPacketServerRevertCamera() { if (Field_24) delete[] Field_24; }

    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_28, 4)) return 0x17;
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (Field_20 > 0) {
            Field_24 = new unsigned char[Field_20];
            if (!ReadBytes(Field_24, Field_20)) return 0x17;
        }
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerSetMissionMessage : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned int Field_24 = 0;
    unsigned int Field_28 = 0;

    FPacketServerSetMissionMessage() : FPacketBase(0x20045) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (!ReadBytes(&Field_24, 4)) return 0x17;
        if (!ReadBytes(&Field_28, 4)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};


struct FPacketServerSetMissionFailures : public FPacketBase {


    FPacketServerSetMissionFailures() : FPacketBase(0x20040) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerBurnFuse : public FPacketBase {


    FPacketServerBurnFuse() : FPacketBase(0x20041) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };

        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};



using FPacketServerSetHullStatus = FRpcSetHullStatusPacket;

struct FPacketServerSetGuidedTarget : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned int Field_24 = 0;
    unsigned int Field_28 = 0;
    unsigned char Field_74 = 0;
    unsigned int Field_2C = 0;
    unsigned int Field_30 = 0;
    unsigned int Field_34 = 0;
    unsigned int Field_38 = 0;
    unsigned int Field_5C = 0;
    unsigned int Field_3C[8] = {0};
    unsigned int Field_60 = 0;
    unsigned char Field_68 = 0;
    unsigned int* Field_64 = nullptr;

    FPacketServerSetGuidedTarget() : FPacketBase(0x2004a) {}
    ~FPacketServerSetGuidedTarget() { if (Field_64) delete[] Field_64; }

    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (!ReadBytes(&Field_24, 4)) return 0x17;
        if (!ReadBytes(&Field_28, 4)) return 0x17;
        if (!ReadBytes(&Field_74, 1)) return 0x17;
        if (Field_74 != 0) {
            if (!ReadBytes(&Field_2C, 4)) return 0x17;
            if (!ReadBytes(&Field_30, 4)) return 0x17;
            Field_34 = 0;
            Field_38 = 0;
            unsigned char count1 = 0;
            if (!ReadBytes(&count1, 1)) return 0x17;
            Field_5C = count1;
            for (unsigned int i = 0; i < Field_5C && i < 8; ++i) {
                if (!ReadBytes(&Field_3C[i], 4)) return 0x17;
            }
        } else {
            Field_2C = 0;
            Field_30 = 0;
            Field_34 = 0;
            Field_38 = 0;
            Field_5C = 0;
        }
        if (!ReadBytes(&Field_60, 4)) return 0x17;
        if (!ReadBytes(&Field_68, 1)) return 0x17;
        if (Field_68 > 0) {
            Field_64 = new unsigned int[Field_68];
            for (unsigned int i = 0; i < Field_68; ++i) {
                if (!ReadBytes(&Field_64[i], 4)) return 0x17;
            }
        }
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerUseItem : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned int Field_24 = 0;

    FPacketServerUseItem() : FPacketBase(0x20051) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (!ReadBytes(&Field_24, 4)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerFormationUpdate : public FPacketBase {
    unsigned int Field_20 = 0;
    unsigned int Field_24 = 0;
    unsigned short Field_28 = 0;
    unsigned short Field_2A = 0;
    unsigned int Field_2C = 0;
    unsigned char Field_30 = 0;

    FPacketServerFormationUpdate() : FPacketBase(0x20053) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (!ReadBytes(&Field_24, 4)) return 0x17;
        if (!ReadBytes(&Field_28, 2)) return 0x17;
        if (!ReadBytes(&Field_2A, 2)) return 0x17;
        if (!ReadBytes(&Field_2C, 4)) return 0x17;
        if (!ReadBytes(&Field_30, 1)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};

struct FPacketServerObjectCargoUpdate : public FPacketBase {
    unsigned int Field_20 = 0;

    FPacketServerObjectCargoUpdate() : FPacketBase(0x20055) {}


    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) override {
        if (OutLength) *OutLength = 0;
        return 0;
    }

    unsigned int Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) override {
        unsigned int Cursor = 0;
        auto ReadBytes = [&](void* Dest, unsigned int Count) -> bool {
            if (Cursor + Count > Size) return false;
            if (Dest) memcpy(Dest, Buffer + Cursor, Count);
            Cursor += Count;
            return true;
        };
        if (!ReadBytes(&Field_20, 4)) return 0x17;
        if (OutLength) *OutLength = Cursor;
        return 0;
    }
};
#endif

// ===== SERVER 0x01..0x1f (RS-P1) =====
// Native SERVER classes shared by both remote DLLs; RC and RS bodies are identical
// (RS-S1 contracts 01..1f). Field names are payload-relative where the meaning is
// not proven. Constructors initialize exactly what the native constructors write.

// 0x05 FLPACKET_SERVER_DAMAGEOBJECT. RC factory 06b4a350 / ctor 06b4a390 / dtor
// 06b3ecb0, RS 06be4e50 / 06be4e90 / 06bdb980 (body 06bdb200); operator new(0x40).
// The ctor leaves +0x20 unset and builds Common's DamageList in place at +0x24 (list
// dummy, size, byte +0x30, dwords +0x34/+0x38/+0x3c zeroed): DamageList(). The
// destructor tears that list down: ~DamageList(). Codecs RC 06b3e780 / 06b3e9c0 =
// RS 06bdb450 / 06bdb690 (instruction-identical to rpclocal 06d90480 / 06d906c0).
// Wire: dword ObjectId, byte bDestroyed, dword Cause, dword InflictorId, word entry
// count, then per entry word SubObjId, byte Fate, float HitPts. InflictorOwner
// (+0x3c) never travels.
class FRpcServerDamageObjectPacket : public FPacketBase {
public:
    FRpcServerDamageObjectPacket() : FPacketBase(0x20005u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerDamageObjectPacket>(OutPacket);
    }
    // RC 06b3e690 (sender helper, packet <- Source) and RS 06bdb260 (receiver helper,
    // Dest <- packet, returns 0): both are DamageList::operator= inlined (list
    // overwrite/insert/erase, then the byte and three dwords of the tail).
    unsigned int ObjectId; // +0x20, IClient::SendServerDamageObject's ObjectId
    DamageList Damage;     // +0x24
};

// 0x06 FLPACKET_SERVER_DESTROYOBJECT. RC factory 06b4a610 / ctor 06b4a650,
// RS 06be5110 / 06be5150; both ctors zero the two fields. Wire: dword, byte.
class FRpcServerDestroyObjectPacket : public FPacketBase {
public:
    FRpcServerDestroyObjectPacket() : FPacketBase(0x20006u), ObjectId(0), Field_0x04(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerDestroyObjectPacket>(OutPacket);
    }
    unsigned int ObjectId;
    unsigned char Field_0x04; // TODO: semantics unproven; kept raw on both sides.
};

// 0x07 FLPACKET_SERVER_LAUNCH. RC 06b4b2a0 / ctor 06b4b2e0, RS 06be5dc0 / 06be5e00.
// The ctor leaves the three leading dwords unset, zeroes Position and seeds the
// identity quaternion {w=1,0,0,0}. Wire: three dwords, Position (one 12-byte
// reservation), Orientation (one 16-byte reservation), all integer copies.
class FRpcServerLaunchPacket : public FPacketBase {
public:
    FRpcServerLaunchPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerLaunchPacket>(OutPacket);
    }
    unsigned int Field_0x00; // TODO: ship id per rpc_iclient_args.h; not proven here.
    unsigned int Field_0x04; // TODO: launch object id; not proven here.
    unsigned int Field_0x08; // TODO: launch position index; not proven here.
    Vector Position;
    Quaternion Orientation;
};

// 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED. RC 06b4b9d0 / ctor 06b4ba10, RS 06be6510 /
// 06be6550; the ctor writes no payload field. Wire: dword, then the double moved
// through the x87 stack on encode (FLD/FSTP qword) and by two integer dwords on decode.
class FRpcServerCharSelectVerifiedPacket : public FPacketBase {
public:
    FRpcServerCharSelectVerifiedPacket() : FPacketBase(0x20008u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCharSelectVerifiedPacket>(OutPacket);
    }
    unsigned int RepInstance; // Dump label "RepInstance"
    double Time;              // Dump label "time"
};

// 0x0a FLPACKET_SERVER_ACTIVATEOBJECT. RC 06b4a570 / ctor 06b4a5b0, RS 06be5070 /
// 06be50b0; the ctor writes no payload field. Wire: dword, raw byte, dword (9 bytes).
class FRpcServerActivateObjectPacket : public FPacketBase {
public:
    FRpcServerActivateObjectPacket() : FPacketBase(0x2000au) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerActivateObjectPacket>(OutPacket);
    }
    unsigned int ObjectId;
    unsigned char bActivate; // Raw native byte, never normalized.
    unsigned int Field_0x08; // Activate-state index (rpc_iclient_args.h).
};

// 0x0b FLPACKET_SERVER_LAND: RC 06b4b140 / ctor 06b4b180, RS 06be5c60 / 06be5ca0;
// three dwords, ctor writes none of them.
using FRpcServerLandPacket = FRpcClientDwordTriplePacket<0x2000bu>;
// 0x0d FLPACKET_SERVER_SETSTARTROOM: RC 06b4be00 / ctor 06b4be40, RS 06be6960 /
// 06be69a0; two dwords, ctor writes neither.
using FRpcServerSetStartRoomPacket = FRpcClientDwordPairPacket<0x2000du>;

// Single-dword lists: the ctor writes no payload field; wire is one dword.
// 0x0e GFCOMPLETENEWSBROADCASTLIST RC 06b4bc20/06b4bc60, RS 06be6780/06be67c0.
using FRpcServerGfCompleteNewsBroadcastListPacket = FRpcScalarPacket<0x2000eu, unsigned int, false>;
// 0x0f GFCOMPLETECHARLIST RC 06b4bae0/06b4bb20, RS 06be6640/06be6680.
using FRpcServerGfCompleteCharListPacket = FRpcScalarPacket<0x2000fu, unsigned int, false>;
// 0x10 GFCOMPLETEMISSIONCOMPUTERLIST RC 06b4bb80/06b4bbc0, RS 06be66e0/06be6720.
using FRpcServerGfCompleteMissionComputerListPacket = FRpcScalarPacket<0x20010u, unsigned int, false>;
// 0x11 GFCOMPLETESCRIPTBEHAVIORLIST RC 06b4bcc0/06b4bd00, RS 06be6820/06be6860.
using FRpcServerGfCompleteScriptBehaviorListPacket = FRpcScalarPacket<0x20011u, unsigned int, false>;
// 0x13 GFCOMPLETEAMBIENTSCRIPTLIST RC 06b4bd60/06b4bda0, RS 06be68c0/06be6900.
using FRpcServerGfCompleteAmbientScriptListPacket = FRpcScalarPacket<0x20013u, unsigned int, false>;

// Dword pairs: the ctor writes no payload field; wire is two dwords.
// 0x14 GFDESTROYNEWSBROADCAST RC 06b4bfe0/06b4c020, RS 06be6b40/06be6b80.
using FRpcServerGfDestroyNewsBroadcastPacket = FRpcClientDwordPairPacket<0x20014u>;
// 0x15 GFDESTROYCHARACTER RC 06b4bea0/06b4bee0, RS 06be6a00/06be6a40.
using FRpcServerGfDestroyCharacterPacket = FRpcClientDwordPairPacket<0x20015u>;
// 0x16 GFDESTROYMISSIONCOMPUTER RC 06b4bf40/06b4bf80, RS 06be6aa0/06be6ae0.
using FRpcServerGfDestroyMissionComputerPacket = FRpcClientDwordPairPacket<0x20016u>;
// 0x17 GFDESTROYSCRIPTBEHAVIOR RC 06b4c080/06b4c0c0, RS 06be6be0/06be6c20.
using FRpcServerGfDestroyScriptBehaviorPacket = FRpcClientDwordPairPacket<0x20017u>;
// 0x19 GFDESTROYAMBIENTSCRIPT RC 06b4c120/06b4c160, RS 06be6c80/06be6cc0.
using FRpcServerGfDestroyAmbientScriptPacket = FRpcClientDwordPairPacket<0x20019u>;

// Owned byte block {Size, Data} (1a/1d/1e: Size +0x20, Data +0x24; 1f: Data +0x20,
// Size +0x24, as its native ctor 06b4c350, set helper 06b3d490 and codecs 06b3d4d0 /
// 06b3d610 address them). The ctor zeroes both; the destructor frees a non-null Data
// (RC 06b3d100). Wire: dword Size, Size bytes. Decode differs between the DLLs (see
// remote_server_channel_codecs.cpp).
unsigned int RpcServerBlobEncode(FPacketBase& Packet, const unsigned int& Size,
    const unsigned char* Data, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength);
unsigned int RpcServerBlobDecode(FPacketBase& Packet, unsigned int& Size,
    unsigned char*& Data, const unsigned char* Buffer, unsigned int InSize,
    unsigned int* OutLength);

// Member order of the block; laid out at +0x20 right after FPacketBase.
template<bool bDataFirst>
struct FRpcServerBlobFields {
    unsigned int Size;
    unsigned char* Data;
};
template<>
struct FRpcServerBlobFields<true> {
    unsigned char* Data;
    unsigned int Size;
};

template<unsigned int PacketTypeId, bool bDataFirst = false>
class FRpcServerBlobPacket : public FPacketBase, public FRpcServerBlobFields<bDataFirst> {
public:
    FRpcServerBlobPacket() : FPacketBase(PacketTypeId) {
        this->Size = 0;
        this->Data = nullptr;
    }
    ~FRpcServerBlobPacket() override {
        if (this->Data != nullptr) ::operator delete(this->Data);
    }
    // RC 06b3d130 / 06b3d490 / 06b3d9b0 / 06b3e200 (sender helpers): Data =
    // operator new(InSize) without a null test, copy, then Size = InSize. A previous
    // block is not released.
    void SetData(const void* InData, unsigned int InSize) {
        this->Data = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
        std::memcpy(this->Data, InData, InSize);
        this->Size = InSize;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        return RpcServerBlobEncode(*this, this->Size, this->Data, BufferHolder, Capacity,
            OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int InSize,
        unsigned int* OutLength) override {
        return RpcServerBlobDecode(*this, this->Size, this->Data, Buffer, InSize, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerBlobPacket>(OutPacket);
    }
};

// 0x1a GFSCRIPTBEHAVIOR RC 06b4c1c0/06b4c200 (E 06b3d170, D 06b3d2b0), RS 06be6d20/06be6d60.
using FRpcServerGfScriptBehaviorPacket = FRpcServerBlobPacket<0x2001au>;
// 0x1d GFUPDATEMISSIONCOMPUTER RC 06b4c230/06b4c270, RS 06be6db0/06be6df0.
using FRpcServerGfUpdateMissionComputerPacket = FRpcServerBlobPacket<0x2001du>;
// 0x1e GFUPDATENEWSBROADCAST RC 06b4c2a0/06b4c2e0, RS 06be6e40/06be6e80.
using FRpcServerGfUpdateNewsBroadcastPacket = FRpcServerBlobPacket<0x2001eu>;
// 0x1f GFUPDATEAMBIENTSCRIPT RC 06b4c310/06b4c350, RS 06be6ed0/06be6f10.
using FRpcServerGfUpdateAmbientScriptPacket = FRpcServerBlobPacket<0x2001fu, true>;
static_assert(sizeof(FRpcServerGfScriptBehaviorPacket) == 0x28, "SERVER 0x1a size 0x28");
static_assert(sizeof(FRpcServerGfUpdateAmbientScriptPacket) == 0x28, "SERVER 0x1f size 0x28");

// 0x04 FLPACKET_SERVER_CREATESHIP. RC factory 06b4a800 / ctor 06b4a840 / dtor 06b40320
// (body 06b3f820), RS 06be5300 / 06be5340 / 06be54a0 (body 06bf0c10), operator
// new(0xec); every pair is instruction-identical. Codecs RC 06b3f9b0 / 06b3fee0, RS
// 06bf0da0 / 06bf12d0 (the RC decoder checks the display-name run before it allocates
// it and returns 5 when that fails). Dump is the base no-op (RC 06b4ecc0 / RS 06bec3a0).
// The create-object block is followed by CShipCreateInfo's own tail.
class FRpcServerCreateShipPacket : public FPacketBase {
public:
    // RS 06be5340: the block, then +0x9c..+0xe8 zeroed.
    FRpcServerCreateShipPacket();
    // RS 06bf0c10: +0xdc (then cleared), +0xd4, +0xcc freed when non-null, then the
    // block's members.
    ~FRpcServerCreateShipPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateShipPacket>(OutPacket);
    }
    FRpcServerCreateObjectFields Object; // +0x20..+0x9b
    unsigned char bUsingTradelane;       // +0x9c <- CShipCreateInfo::bUsingTradelane (+0xdc)
    Vector Velocity;                     // +0xa0 <- Velocity (+0xb8)
    Vector AngularVelocity;              // +0xac <- AngularVelocity (+0xc4); never on the wire
    float Throttle;                      // +0xb8 <- Throttle (+0xd0), 127-scale byte
    unsigned int OwnerPlayer;            // +0xbc <- OwnerPlayer (+0xac); low byte travels
    unsigned short Field_0xc0;           // +0xc0 <- Field_0xb0 (+0xb0), low word
    unsigned char ReputationRank;        // +0xc2 <- ReputationRank (+0xb4), low byte
    unsigned short Field_0xc4;           // +0xc4 <- Field_0xd8 (+0xd8), low word
    unsigned int NameASize;              // +0xc8 flattened FmtStr (Vibe::GetName name 1)
    unsigned char* NameA;                // +0xcc
    unsigned int NameBSize;              // +0xd0 flattened FmtStr (name 2)
    unsigned char* NameB;                // +0xd4
    int DisplayNameLength;               // +0xd8 wide characters, without the terminator
    wchar_t* DisplayName;                // +0xdc operator new(2 * length + 2)
    unsigned int Affiliation;            // +0xe0 Vibe::GetAffiliation
    float Attitude;                      // +0xe4 Vibe::AttitudeTowards, 127-scale byte
    unsigned char bMarkedBy;             // +0xe8 <- bMarkedBy (+0xa8)
};

// ===== end SERVER 0x01..0x1f (RS-P1) =====

// ===== SERVER 0x40..0x5b (RS-P3) =====
// Native SERVER classes of 0x40..0x5b shared by both remote DLLs. RC and RS build the
// same layouts, constructors and wire bodies for every ID here (RS-S2 contracts 40..5b);
// a body whose RC and RS listings differ says so at its definition. Constructors write
// exactly what the native constructors write; the rest stays as retail leaves it.

// One-scalar and empty bodies. Encode: header, one reserved field, end, stats.
// 0x40 SETMISSIONFAILURES  RC 06b4c670/06b4c6b0, RS 06be7270/06be72b0; ctor leaves +0x20.
using FRpcServerSetMissionFailuresPacket = FRpcScalarPacket<0x20040u, unsigned int, false>;
// 0x43 SET_STORY_CUE       RC 06b4b5e0/06b4b620, RS 06be6100/06be6140; ctor leaves +0x20.
using FRpcServerSetStoryCuePacket = FRpcScalarPacket<0x20043u, unsigned int, false>;
// 0x56 SENDNNMESSAGE       RC 06b4d1c0/06b4d200, RS 06be7e00/06be7e40; ctor zeroes +0x20.
using FRpcServerSendNNMessagePacket = FRpcScalarPacket<0x20056u, unsigned int, true>;
// 0x59 PLAY_SOUND_EFFECT   RC 06b4d3a0/06b4d3e0, RS 06be7fe0/06be8020; ctor leaves +0x20.
using FRpcServerPlaySoundEffectPacket = FRpcScalarPacket<0x20059u, unsigned int, false>;
// 0x5a GFMISSIONVENDORWHYEMPTY  RC 06b4c3f0/06b4c430, RS 06be6ff0/06be7030; one byte, unset.
using FRpcServerGfMissionVendorWhyEmptyPacket = FRpcScalarPacket<0x2005au, unsigned char, false>;
// 0x5b MISSIONSAVEA        RC 06b4d440/06b4d480, RS 06be8080/06be80c0; ctor leaves +0x20.
using FRpcServerMissionSaveAPacket = FRpcScalarPacket<0x2005bu, unsigned int, false>;
// 0x4c REVERT_CAMERA       RC 06b4b730/06b4b770, RS 06be6250/06be6290; header only.
using FRpcServerRevertCameraPacket = FRpcEmptyPacket<0x2004cu>;
// 0x58 CANCEL_MUSIC        RC 06b4d300/06b4d340, RS 06be7f40/06be7f80; header only.
using FRpcServerCancelMusicPacket = FRpcEmptyPacket<0x20058u>;

// 0x41 FLPACKET_SERVER_BURNFUSE. RC 06b4c710 / ctor 06b4c750, RS 06be7310 / 06be7350.
// The argument is FServerBurnFuseInfo (rpc_iclient_args.h). Wire: three dwords, word,
// two floats (x87 on encode, raw on decode), byte.
class FRpcServerBurnFusePacket : public FPacketBase {
public:
    FRpcServerBurnFusePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerBurnFusePacket>(OutPacket);
    }
    unsigned int SourceId;      // +0x20
    unsigned int ObjectId;      // +0x24
    unsigned int FuseId;        // +0x28
    unsigned short SubObjId;    // +0x2c  ctor: SUBOBJ_ID_NONE
    float AtT;                  // +0x30
    float Lifetime;             // +0x34  ctor: -1.0f
    unsigned char bLit;         // +0x38  raw byte
};

// 0x44 FLPACKET_SERVER_REQUEST_RETURNED. RC 06b4b0a0 / ctor 06b4b0e0, RS 06be5bc0 /
// 06be5c00; the ctor writes no payload field. Wire: dword and three bytes (7 bytes).
class FRpcServerRequestReturnedPacket : public FPacketBase {
public:
    FRpcServerRequestReturnedPacket() : FPacketBase(0x20044u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerRequestReturnedPacket>(OutPacket);
    }
    unsigned int ObjectId;        // +0x20  SendServerRequestReturned ShipId (Server: the requesting object's id)
    unsigned char RequestType;    // +0x24  A2 low byte; the receiver zero-extends it
    unsigned char Response;       // +0x25  A3 low byte; the receiver sign-extends it
    unsigned char Field_0x26;     // +0x26  A4 low byte; sign-extended. TODO: meaning
};

// 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE. RC 06b4b7d0 / ctor 06b4b810 / dtor
// 06b4b860, RS 06be62f0 / 06be6330 / 06be6360. The ctor zeroes the block; the
// destructor releases it with operator delete. Wire: Type, then the flattened
// CSetMissionMessage::Text as a length-prefixed block.
class FRpcServerSetMissionMessagePacket : public FPacketBase {
public:
    FRpcServerSetMissionMessagePacket() : FPacketBase(0x20045u), TextSize(0), TextData(nullptr) {}
    ~FRpcServerSetMissionMessagePacket() override;
    FRpcServerSetMissionMessagePacket(const FRpcServerSetMissionMessagePacket&) = delete;
    FRpcServerSetMissionMessagePacket& operator=(const FRpcServerSetMissionMessagePacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetMissionMessagePacket>(OutPacket);
    }
    unsigned int TextSize;        // +0x20
    unsigned char* TextData;      // +0x24  owned, operator new / operator delete
    unsigned int Type;            // +0x28  CSetMissionMessage::Type; ctor leaves it
};

// 0x46 FLPACKET_SERVER_MARKOBJ. RC 06b4b3f0 / ctor 06b4b430, RS 06be5f10 / 06be5f50;
// the ctor writes no payload field. Wire: dword, byte.
class FRpcServerMarkObjPacket : public FPacketBase {
public:
    FRpcServerMarkObjPacket() : FPacketBase(0x20046u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerMarkObjPacket>(OutPacket);
    }
    unsigned int ObjectId;        // +0x20  pub::Player::MarkObj obj_id
    unsigned char MarkType;       // +0x24  low byte of mark_type
};

// 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION. RC 06b4b490 / ctor 06b4b4d0, RS
// 06be5fb0 / 06be5ff0; the ctor writes no payload field. Wire: +0x20, the byte +0x28,
// then +0x24. The sender copies the byte raw; only the receiver normalizes it.
class FRpcServerCfgInterfaceNotificationPacket : public FPacketBase {
public:
    FRpcServerCfgInterfaceNotificationPacket() : FPacketBase(0x20047u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCfgInterfaceNotificationPacket>(OutPacket);
    }
    unsigned int NotifType;       // +0x20
    unsigned int Param;           // +0x24
    unsigned char bEnable;        // +0x28  raw byte
};

// 0x4a FLPACKET_SERVER_SETGUIDEDTARGET. RC 06b4cc30 / ctor 06b4cc70, RS 06be7830 /
// 06be7870; the ctor zeroes all three fields. Wire: dword, dword, word.
class FRpcServerSetGuidedTargetPacket : public FPacketBase {
public:
    FRpcServerSetGuidedTargetPacket()
        : FPacketBase(0x2004au), Field_0x20(0), Field_0x24(0), Field_0x28(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetGuidedTargetPacket>(OutPacket);
    }
    // TODO: semantic names; SendServerSetGuidedTarget's GuidedId, TargetId and
    // TargetSubObjId (a word), in this order.
    unsigned int Field_0x20;
    unsigned int Field_0x24;
    unsigned short Field_0x28;
};

// 0x4b FLPACKET_SERVER_SET_CAMERA. RC 06b4b680 / ctor 06b4b6c0, RS 06be61a0 / 06be61e0.
// The ctor zeroes Position, seeds the identity quaternion (w = 1.0f) and leaves the two
// trailing floats. Wire: Position (one 12-byte reservation), Orientation (one 16-byte
// reservation), both raw; then the two floats (x87 on encode, raw on decode).
class FRpcServerSetCameraPacket : public FPacketBase {
public:
    FRpcServerSetCameraPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetCameraPacket>(OutPacket);
    }
    Vector Position;              // +0x20
    Quaternion Orientation;       // +0x2c
    float Fov;                    // +0x3c  pub::Player::SetCamera fov
    float Aspect;                 // +0x40  pub::Player::SetCamera aspect
};

// 0x51 FLPACKET_SERVER_USE_ITEM. RC 06b4cda0 / ctor 06b4cde0, RS 06be79a0 / 06be79e0;
// the ctor zeroes all three fields. The argument is FPayloadServerUseItem
// (rpc_iclient_args.h). Wire: dword, word, word.
class FRpcServerUseItemPacket : public FPacketBase {
public:
    FRpcServerUseItemPacket() : FPacketBase(0x20051u), ObjectId(0), EquipId(0), Count(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerUseItemPacket>(OutPacket);
    }
    unsigned int ObjectId;        // +0x20
    unsigned short EquipId;       // +0x24
    unsigned short Count;         // +0x26
};

// 0x53 FLPACKET_SERVER_FORMATION_UPDATE. RC 06b4cfd0 / ctor 06b4d010, RS 06be7bf0 /
// 06be7c30; the ctor zeroes all four dwords. Wire: dword, then Offset as one raw
// 12-byte reservation.
class FRpcServerFormationUpdatePacket : public FPacketBase {
public:
    FRpcServerFormationUpdatePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerFormationUpdatePacket>(OutPacket);
    }
    unsigned int LeaderId;        // +0x20  SendServerFormationUpdate LeaderId
    Vector Offset;                // +0x24  SendServerFormationUpdate Offset
};

// 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE. RC 06b4d120 / ctor 06b4d160, RS 06be7d60 /
// 06be7da0. Carries one EquipDesc: ctor +0x20/+0x24/+0x28 = 0, Count = 1, Status =
// 1.0f, bMission = 0. Wire: dword, dword, word, word, float (x87 on encode, raw on
// decode), byte.
class FRpcServerObjectCargoUpdatePacket : public FPacketBase {
public:
    FRpcServerObjectCargoUpdatePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerObjectCargoUpdatePacket>(OutPacket);
    }
    unsigned int ObjectId;        // +0x20  SendServerObjectCargoUpdate ObjectId
    unsigned int ArchId;          // +0x24  EquipDesc::ArchId
    unsigned short EquipId;       // +0x28  EquipDesc::Id
    unsigned short Count;         // +0x2a  low word of EquipDesc::Count
    float Status;                 // +0x2c  EquipDesc::Health
    unsigned char bMission;       // +0x30  EquipDesc::bMission, raw byte
};

// 0x57 FLPACKET_SERVER_SET_MUSIC. RC 06b4d260 / ctor 06b4d2a0, RS 06be7ea0 / 06be7ee0;
// the ctor writes no payload field. Wire: four dwords, byte, float (x87 on encode, raw
// on decode). The six values are SendServerSetMusic's arguments after ClientId (Server: the
// Tryptich fields).
class FRpcServerSetMusicPacket : public FPacketBase {
public:
    FRpcServerSetMusicPacket() : FPacketBase(0x20057u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetMusicPacket>(OutPacket);
    }
    // TODO: semantic names of the Tryptich fields.
    unsigned int Field_0x20;
    unsigned int Field_0x24;
    unsigned int Field_0x28;
    unsigned int Field_0x2c;
    unsigned char Field_0x30;
    float Field_0x34;
};

// 0x4d FLPACKET_SERVER_LOADHINT (RS-P4). RC 06b4abf0 / ctor 06b4ac30 / deleting dtor
// 06b43820 (body 06b42f00), RS 06be5710 / 06be5750 / 06bdc510 (body 06bdbc10). Carries
// one serialized BaseHint: the ctor zeroes all three fields, the destructor frees a
// non-null block and clears the pointer. Wire: Size, Kind, then Size bytes.
class FRpcServerLoadHintPacket : public FPacketBase {
public:
    FRpcServerLoadHintPacket() : FPacketBase(0x2004du), Data(nullptr), Size(0), Kind(0) {}
    ~FRpcServerLoadHintPacket() override;
    FRpcServerLoadHintPacket(const FRpcServerLoadHintPacket&) = delete;
    FRpcServerLoadHintPacket& operator=(const FRpcServerLoadHintPacket&) = delete;
    void SetData(const unsigned char* InData, unsigned int InSize);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int InSize,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerLoadHintPacket>(OutPacket);
    }
    unsigned char* Data;          // +0x20  owned, operator new / operator delete
    unsigned int Size;            // +0x24
    unsigned int Kind;            // +0x28  BaseHint::Kind
};

#include "rpc_iclient_args.h" // FPayloadServerSetDirective (0x4e)

// 0x4e FLPACKET_SERVER_SETDIRECTIVE (RS-P4). RC 06b4ac60 / ctor 06b4aca0 / deleting dtor
// 06b4ad70, RS 06be5780 / 06be57c0 / 06be5890. Carries one FPayloadServerSetDirective at
// +0x20. The ctor seeds it as Server's default construction does (+0x18 = -1, +0x40 = 3,
// every other field 0) and leaves +0x00 and the padding bytes. Wire: the 0x94-byte record
// as one reservation, written and read field by field, so the padding bytes +0x09..+0x0b
// and +0x91..+0x93 are never touched on either side.
class FRpcServerSetDirectivePacket : public FPacketBase {
public:
    FRpcServerSetDirectivePacket();
    void SetDirective(const FPayloadServerSetDirective& Source);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetDirectivePacket>(OutPacket);
    }
    FPayloadServerSetDirective Directive; // +0x20
};

// 0x52 FLPACKET_SERVER_PLAYERLIST (RS-P4). RC 06b4cee0 / ctor 06b4cf20 / deleting dtor
// 06b466d0 (body 06b466a0), RS 06be7ae0 / 06be7b20 / 06be7b60 (body 06bf9f40). One
// packet type for IClient slots 110 (Kind 1), 111 (Kind 2) and 112 (Kind 0); the ctor
// seeds Kind 3, which the receiver ignores. Name is a CRT heap string (_wcsdup by the
// setter, malloc by the decoder, free by the setter and the destructor). Wire: Kind,
// PlayerId, the byte +0x2c, a byte count of wide characters (terminator included, 0 for
// no name), then those characters.
class FRpcServerPlayerListPacket : public FPacketBase {
public:
    FRpcServerPlayerListPacket()
        : FPacketBase(0x20052u), Kind(kPlayerListKindNone), Name(nullptr), PlayerId(0), Field_0x2c(0) {}
    ~FRpcServerPlayerListPacket() override;
    FRpcServerPlayerListPacket(const FRpcServerPlayerListPacket&) = delete;
    FRpcServerPlayerListPacket& operator=(const FRpcServerPlayerListPacket&) = delete;
    void SetEntry(const wchar_t* InName, unsigned int InPlayerId);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerPlayerListPacket>(OutPacket);
    }
    unsigned int Kind;            // +0x20  kPlayerListKind* (rpc_iclient_args.h): 0 clear
                                  //        (SendServerPlayerListClear), 1 entry
                                  //        (SendServerPlayerListAdd), 2 drop (SendServerPlayerListRemove)
    wchar_t* Name;                // +0x24  owned, CRT heap
    unsigned int PlayerId;        // +0x28
    // TODO: meaning. SendServerPlayerListAdd's bAlreadyPresent byte; Server passes 1 for players already present and 0
    // for the newcomer, the client tests it against zero. Raw byte here, SETNZ on receive.
    unsigned char Field_0x2c;     // +0x2c
};

#include "common_archetype.h" // CollisionGroupDesc (0x48)

// 0x48 FLPACKET_SERVER_SETCOLLISIONGROUPS (RS-P4). RC 06b4ca50 / ctor 06b4ca90 / deleting
// dtor 06b4cb10 (body 06b4cb30), RS 06be7650 / 06be7690 / 06be7710. Holds a
// std::list<CollisionGroupDesc> at +0x20 (VC6: allocator byte +0x20, head +0x24, size
// +0x28; the port's list has its own layout). Wire (helpers RC 06b3a180 / 06b3a230, RS
// 06bda4e0 / 06bda590): the low word of the size, then per node the u16 Id and the float
// Health (x87 on encode, raw on decode).
class FRpcServerSetCollisionGroupsPacket : public FPacketBase {
public:
    FRpcServerSetCollisionGroupsPacket() : FPacketBase(0x20048u) {}
    void SetGroups(const std::list<CollisionGroupDesc>& Source);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetCollisionGroupsPacket>(OutPacket);
    }
    std::list<CollisionGroupDesc> Groups; // +0x20
};

#include "common_reputation.h" // Costume (0x4f)

// 0x4f FLPACKET_SERVER_SENDCOMM (RS-P9). RC factory 06b4ccd0 / ctor 06b4cd10 / deleting dtor
// 06b4cd50 (body 06b4cd70), RS 06be78d0 / 06be7910 / 06be7950 (body 06be7970); RC and RS are
// identical except where Decode says so. Size 0x78, every field at its retail offset: the
// accessory loops of the sender and the decoder run past Body.AccessoryIds when the count
// exceeds 8 and land on the members that follow, as retail's do. The ctor zeroes the four
// part ids, the accessory count and the owned array pointer and sets +0x74 to 1; the rest is
// left as operator new leaves it. Wire: three dwords, the byte +0x74; when it is not 0 the
// part ids 0 and 1, the accessory count as a byte and that many accessory ids; then +0x60,
// the signed byte count +0x68 and that many dwords of the owned array, the low word of
// +0x6c, and +0x70 quantized to one byte (x0.1f x255, truncated; expanded x2/51).
class FRpcServerSendCommPacket : public FPacketBase {
public:
    FRpcServerSendCommPacket();
    ~FRpcServerSendCommPacket() override;
    FRpcServerSendCommPacket(const FRpcServerSendCommPacket&) = delete;
    FRpcServerSendCommPacket& operator=(const FRpcServerSendCommPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSendCommPacket>(OutPacket);
    }
    // TODO: semantic names. SendServerSendComm's SenderId, ReceiverId, VoiceId in this order.
    unsigned int Field_0x20;      // +0x20
    unsigned int Field_0x24;      // +0x24
    unsigned int Field_0x28;      // +0x28
    Costume Body;                 // +0x2c  SendServerSendComm CommCostume (13 dwords by value)
    unsigned int Field_0x60;      // +0x60  SendServerSendComm CommType. TODO: meaning
    unsigned int* Field_0x64;     // +0x64  owned array (operator new / operator delete), TextIds
    signed char Field_0x68;       // +0x68  its count, signed (MOVSX everywhere), TextCount
    unsigned int Field_0x6c;      // +0x6c  Mood; the wire carries the low word
    float Field_0x70;             // +0x70  Duration; one quantized byte on the wire
    // TODO: meaning. bReceiverIsOwnShip's byte, raw; the codec sends the costume only when it is not 0.
    unsigned char Field_0x74;     // +0x74
};
static_assert(sizeof(FRpcServerSendCommPacket) == 0x78, "SERVER 0x4f size 0x78");

// 0x54 FLPACKET_SERVER_MISCOBJUPDATE (RS-P9). RC factory 06b4d070 / ctor 06b4d0b0 / deleting
// dtor 06b45810 (body 06b457a0), RS 06be7c90 / 06be7cd0 / 06be7d40 (body 06bfa600). One packet
// type for the eight IClient slots 113..120; each sender sets one or two flag bits through the
// setters below (RC 06b45830..06b45a60). The ctor zeroes Flags and every scalar, sets
// Field_0x54 to -1 and leaves the string and the vector empty. VC6 layout: wstring +0x40
// (_Ptr +0x44, _Len +0x48), +0x50 (ctor 0, never coded), vector +0x58 (_First +0x5c), byte
// +0x68; the port's std containers have their own layout, so only the scalar offsets before
// +0x40 are retail's. Wire: Flags, then per set bit in this order: 0x08 +0x28, 0x04 +0x24,
// 0x01 +0x34, 0x02 +0x38 and +0x3c, 0x10 the text (low word of its length, then that many
// wide characters), 0x20 +0x54, 0x40 the word +0x2c, 0x80 +0x30, 0x100 the byte +0x68 then
// every element of the vector.
class FRpcServerMiscObjUpdatePacket : public FPacketBase {
public:
    FRpcServerMiscObjUpdatePacket();
    void SetField0x28(unsigned int Value);
    void SetField0x24(unsigned int Value);
    void SetField0x54(unsigned int Value);
    void SetField0x34(unsigned int Value);
    void SetFields0x38(unsigned int Value0x38, unsigned int Value0x3c);
    void SetText(const std::wstring& Source);
    void SetField0x30(unsigned int Value);
    void SetField0x2c(unsigned short Value);
    void AddId(unsigned int Id);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerMiscObjUpdatePacket>(OutPacket);
    }
    unsigned short Flags;         // +0x20  (0x100 is bit 0 of the byte +0x21)
    // TODO: semantic names; the flag bit that carries each field is proven.
    unsigned int Field_0x24;      // +0x24  0x04 (Rank/PlayerGroup/System PlayerId)
    unsigned int Field_0x28;      // +0x28  0x08 (ObjectGroup/Affiliation ObjectId)
    unsigned short Field_0x2c;    // +0x2c  0x40 (Rank Rank)
    unsigned int Field_0x30;      // +0x30  0x80 (System SystemId)
    unsigned int Field_0x34;      // +0x34  0x01 (ObjectGroup/PlayerGroup GroupId)
    unsigned int Field_0x38;      // +0x38  0x02 (Mission MissionId)
    unsigned int Field_0x3c;      // +0x3c  0x02 (Mission MissionSetBy)
    std::wstring Text;            // VC6 +0x40, 0x10 (Motd)
    unsigned int Field_0x50;      // VC6 +0x50, ctor 0; no coder or setter touches it
    unsigned int Field_0x54;      // VC6 +0x54, 0x20 (Affiliation Affiliation); ctor -1
    std::vector<unsigned int> Ids; // VC6 +0x58, 0x100 (Formation MemberIds)
    // (names: IClient::SendServerMiscObjUpdate<Name> and its parameter)
    unsigned char IdCount;        // VC6 +0x68, the low byte of the vector size
};

// 0x42 FLPACKET_SERVER_CREATESOLAR (RS-P9). RC factory 06b4c7d0 / ctor 06b4c810 / deleting
// dtor 06b4c900 (body 06b4c920), RS 06be73d0 / 06be7410 / 06be7500 (body 06be7520); RC and
// RS are instruction-identical apart from helper and constant addresses. The create-object
// block +0x20..+0x9b is shared with 0x04 CREATESHIP (RS-P10's FRpcServerCreateObjectFields).
// The ctor builds the block, zeroes the two bytes and the two name blocks and leaves +0xb0 /
// +0xb4; the destructor frees NameB, then NameA, then the block's members. Wire: the block,
// the two bytes, and outside single player Affiliation, Attitude as one 127-scaled byte, and
// the two flattened FmtStr names as length-prefixed blocks. Both ends test SinglePlayer() of
// their own process.
class FRpcServerCreateSolarPacket : public FPacketBase {
public:
    FRpcServerCreateSolarPacket();
    ~FRpcServerCreateSolarPacket() override;
    FRpcServerCreateSolarPacket(const FRpcServerCreateSolarPacket&) = delete;
    FRpcServerCreateSolarPacket& operator=(const FRpcServerCreateSolarPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateSolarPacket>(OutPacket);
    }
    FRpcServerCreateObjectFields Fields; // +0x20..+0x9b
    unsigned char Field_0x9c;     // +0x9c  <- CSolarCreateInfo::Field_0xad
    unsigned char bMarkedBy;      // +0x9d  <- CEqObjCreateInfo::bMarkedBy (+0xa8), raw byte
    unsigned int NameASize;       // +0xa0  Reputation::Vibe::GetName's first FmtStr, flattened
    unsigned char* NameA;         // +0xa4  owned, operator new / operator delete
    unsigned int NameBSize;       // +0xa8  its second FmtStr, flattened
    unsigned char* NameB;         // +0xac  owned
    unsigned int Affiliation;     // +0xb0  Reputation::Vibe::GetAffiliation out-parameter
    float Attitude;               // +0xb4  Reputation::Vibe::AttitudeTowards out-parameter
};

// ===== end SERVER 0x40..0x5b (RS-P3) =====

// ===== SERVER 0x20..0x3f (RS-P2) =====
// Native SERVER classes 0x20..0x3f, one class per id compiled into both remote DLLs.
// The RC and RS constructors and codecs of every class below differ only in their
// addresses (RS-S1/RS-S2 dossiers); comments name the RC address first, then RS.
// Constructors write exactly the payload fields the native constructors write.

#include "common_reputation.h" // Reputation::Relation (0x29, 0x2b)

struct EquipDescVector;

// --- equipment list: 0x24 SETEQUIPMENT, 0x25 SETCARGO ---

// FRpcServerEquipRecord, FRpcServerEquipHandle and FRpcServerEquipListPacketBase are
// declared once in rpc_equipment_codec.h; their bodies stay in
// remote_server_channel_codecs.cpp.
#include "rpc_equipment_codec.h"

// RC 06b38210 (__cdecl, the senders' builder): one record per EquipDesc of Source;
// bAll false keeps only the items whose bEquipped byte is 0.
void RpcServerEquipHandleFromVector(const EquipDescVector* Source,
    FRpcServerEquipHandle* Handle, bool bAll);
// RS 06bd5130 (__cdecl, the receivers' converter): records -> EquipDesc elements.
void RpcServerEquipVectorFromHandle(const FRpcServerEquipHandle* Handle,
    EquipDescVector* Dest);

template<unsigned int PacketTypeId>
class FRpcServerEquipListPacket : public FRpcServerEquipListPacketBase {
public:
    FRpcServerEquipListPacket() : FRpcServerEquipListPacketBase(PacketTypeId) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerEquipListPacket>(OutPacket);
    }
};

// 0x24: RC factory 06b4a090 / ctor 06b4a0d0, RS 06be4b90 / 06be4bd0.
using FRpcServerSetEquipmentPacket = FRpcServerEquipListPacket<0x20024u>;
// 0x25: RC factory 06b4a100 / ctor 06b4a140, RS 06be4c00 / 06be4c40.
using FRpcServerSetCargoPacket = FRpcServerEquipListPacket<0x20025u>;

// 0x21 FLPACKET_SERVER_SYSTEM_SWITCH_OUT: RC 06b4a6b0 / ctor 06b4a6f0, RS 06be51b0 /
// 06be51f0; two dwords, ctor writes neither.
using FRpcServerSystemSwitchOutPacket = FRpcClientDwordPairPacket<0x20021u>;

// 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP: RC 06b49ff0 / ctor 06b4a030, RS
// 06be4af0 / 06be4b30; ctor writes no payload field. Wire: raw byte, dword.
class FRpcServerRequestCreateShipRespPacket : public FPacketBase {
public:
    FRpcServerRequestCreateShipRespPacket() : FPacketBase(0x20027u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerRequestCreateShipRespPacket>(OutPacket);
    }
    unsigned char Field_0x00; // +0x20 TODO: response byte semantics; kept raw
    unsigned int Field_0x04;  // +0x24 TODO: semantics unproven
};

// 0x2a FLPACKET_SERVER_ADJUSTATTITUDE: RC 06b4b000 / ctor 06b4b040, RS 06be5b20 /
// 06be5b60; ctor writes no payload field. Wire: dword, float (FLD/FSTP on encode,
// integer MOV on decode).
class FRpcServerAdjustAttitudePacket : public FPacketBase {
public:
    FRpcServerAdjustAttitudePacket() : FPacketBase(0x2002au) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerAdjustAttitudePacket>(OutPacket);
    }
    unsigned int Field_0x00; // +0x20 TODO: group/vibe id semantics unproven
    float Field_0x04;        // +0x24 TODO: attitude delta semantics unproven
};

// 0x2f FLPACKET_SERVER_SETREMOVEITEM: RC 06b49e90 / ctor 06b49ed0, RS 06be4970 /
// 06be49b0; the ctor writes +0x20 = 0, +0x24 = +0x28 = -1. Wire: three dwords.
class FRpcServerSetRemoveItemPacket : public FRpcClientDwordTriplePacket<0x2002fu> {
public:
    FRpcServerSetRemoveItemPacket() {
        Field_0x00 = 0;
        Field_0x04 = 0xffffffffu;
        Field_0x08 = 0xffffffffu;
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetRemoveItemPacket>(OutPacket);
    }
};

// 0x30 SETCASH (RC 06b49d80 / 06b49dc0, RS 06be4840 / 06be4880), 0x3e SETNUMKILLS
// (RC 06b4c530 / 06b4c570, RS 06be7130 / 06be7170), 0x3f SETMISSIONSUCCESSES (RC
// 06b4c5d0 / 06b4c610, RS 06be71d0 / 06be7210): one dword the ctor leaves unset.
using FRpcServerSetCashPacket = FRpcScalarPacket<0x20030u, unsigned int, false>;
using FRpcServerSetNumKillsPacket = FRpcScalarPacket<0x2003eu, unsigned int, false>;
using FRpcServerSetMissionSuccessesPacket = FRpcScalarPacket<0x2003fu, unsigned int, false>;

// 0x39 FLPACKET_SERVER_SCANNOTIFY: RC 06b4ce40 / ctor 06b4ce80, RS 06be7a40 /
// 06be7a80; two dwords, ctor writes neither.
using FRpcServerScanNotifyPacket = FRpcClientDwordPairPacket<0x20039u>;

// 0x20 GFMISSIONVENDORACCEPTANCE (RC factory 06b4c380 / ctor 06b4c3c0, E 06b3dd00,
// D 06b3de40, dtor 06b3e1b0, set helper 06b3dcc0; RS 06be6f60 / 06be6fa0) and 0x26
// GFUPDATECHAR (RC 06b4ba70 / 06b4bab0, E 06b3ce10, D 06b3cf50, dtor 06b3d0e0, set
// helper 06b3cdd0; RS 06be65b0 / 06be65f0): instruction-identical to 0x1a in both DLLs
// (address-masked compare); the ctors differ only in store order. Size 0x28.
using FRpcServerGfMissionVendorAcceptancePacket = FRpcServerBlobPacket<0x20020u>;
using FRpcServerGfUpdateCharPacket = FRpcServerBlobPacket<0x20026u>;

// 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN: RC factory 06b4a750 / ctor 06b4a790, RS
// 06be5250 / 06be5290 (size 0x44). The ctor zeroes Position and seeds Orientation
// {w=1,0,0,0}; it leaves the two dwords. Wire: dword, dword, Position (one 12-byte
// reservation), Orientation (one 16-byte reservation), all integer copies (RC
// 06b3a160 / 06b39ec0). The IClient argument is FPayloadServerSystemSwitchIn.
class FRpcServerSystemSwitchInPacket : public FPacketBase {
public:
    FRpcServerSystemSwitchInPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSystemSwitchInPacket>(OutPacket);
    }
    unsigned int Field_0x00; // +0x20 object id (argument +0x00)
    unsigned int Field_0x04; // +0x24 jump object type bits (argument +0x04)
    Vector Position;         // +0x28
    Quaternion Orientation;  // +0x34
};

// 0x31 FLPACKET_SERVER_EXPLODEASTEROIDMINE: RC factory 06b4ab50 / ctor 06b4ab90, RS
// 06be5670 / 06be56b0 (size 0x30). The ctor zeroes all four dwords. Wire: dword, then
// the Vector as one 12-byte reservation (integer copy).
class FRpcServerExplodeAsteroidMinePacket : public FPacketBase {
public:
    FRpcServerExplodeAsteroidMinePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerExplodeAsteroidMinePacket>(OutPacket);
    }
    unsigned int Field_0x00; // +0x20 TODO: object id per contract; semantics unproven
    Vector Position;         // +0x24 TODO: explosion position; semantics unproven
};

// 0x38 FLPACKET_SERVER_ITEMTRACTORED: RC factory 06b4a4d0 / ctor 06b4a510, RS
// 06be4fd0 / 06be5010 (size 0x2c). The ctor zeroes every field. Wire: dword, dword,
// word, word. The IClient argument is FPayloadServerItemTractored.
class FRpcServerItemTractoredPacket : public FPacketBase {
public:
    FRpcServerItemTractoredPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerItemTractoredPacket>(OutPacket);
    }
    unsigned int Field_0x00;   // +0x20 loot object id
    unsigned int Field_0x04;   // +0x24 tractoring ship id
    unsigned short Field_0x08; // +0x28 tractor equipment id
    unsigned short Field_0x0a; // +0x2a units taken
};

// 0x2e FLPACKET_SERVER_SETADDITEM: RC factory 06b49e20 / ctor 06b49e60 / dtor 06b3c800,
// RS 06be48e0 / 06be4920 / 06bee710 (size 0x40). The ctor writes only HardpointSize =
// 0, the +0x39 byte = 0 and Hardpoint = null; the dtor frees a non-null Hardpoint.
// Wire: ArchId, Id, Count, Health (FLD/FSTP on encode, MOV on decode), raw byte
// bEquipped, raw byte bMission, Field_0x0c, HardpointSize, then HardpointSize bytes
// only when it is non-zero. Decode differs between the DLLs (see the .cpp).
class FRpcServerSetAddItemPacket : public FPacketBase {
public:
    FRpcServerSetAddItemPacket();
    ~FRpcServerSetAddItemPacket() override;
    FRpcServerSetAddItemPacket(const FRpcServerSetAddItemPacket&) = delete;
    FRpcServerSetAddItemPacket& operator=(const FRpcServerSetAddItemPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetAddItemPacket>(OutPacket);
    }
    // RC 06b3cd50 (thiscall, RET 8): when Length > 0 and Source is non-null, Hardpoint =
    // operator new(Length + 1) with no null test, copy Length + 1 bytes, HardpointSize =
    // Length + 1. A previous block is not released.
    void SetHardpoint(const char* Source, unsigned int Length);
    unsigned int ArchId;         // +0x20 EquipDesc +0x04
    int Count;                   // +0x24 EquipDesc +0x14
    unsigned int HardpointSize;  // +0x28 strlen + 1, 0 for an internal item
    unsigned int Field_0x0c;     // +0x2c IClient::SendServerSetAddItem's AddResult
    unsigned int Id;             // +0x30 EquipDesc +0x02, zero-extended
    float Health;                // +0x34 EquipDesc +0x10
    unsigned char bEquipped;     // +0x38 EquipDesc +0x0c, raw byte
    unsigned char bMission;      // +0x39 EquipDesc +0x18, raw byte
    char* Hardpoint;             // +0x3c owned, operator new(HardpointSize)
};

// 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT: RC factory 06b4b950 / ctor 06b4b990 / dtor
// 06b43ed0, RS 06be6470 / 06be64b0 / (own dtor via 06be64f0) (size 0x3c). The ctor zeroes
// +0x20, +0x28..+0x30 and Name, sets Mode = 1 and leaves NameSize. The dtor frees a
// non-null Name. Wire: dword, NameSize, then NameSize bytes when it is non-zero;
// +0x28..+0x34 never cross the wire. RS Decode copies the name into Name WITHOUT
// allocating it (Name is null from the ctor): a retail write through null, reproduced on
// purpose (owner's UB ruling).
class FRpcServerRequestSpaceScriptPacket : public FPacketBase {
public:
    FRpcServerRequestSpaceScriptPacket();
    ~FRpcServerRequestSpaceScriptPacket() override;
    FRpcServerRequestSpaceScriptPacket(const FRpcServerRequestSpaceScriptPacket&) = delete;
    FRpcServerRequestSpaceScriptPacket& operator=(const FRpcServerRequestSpaceScriptPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerRequestSpaceScriptPacket>(OutPacket);
    }
    // RC 06b44240 (thiscall, RET 8): when Length > 0 and Source is non-null, free the old
    // Name, Name = operator new(Length + 1) with no null test, copy Length + 1 bytes,
    // NameSize = Length + 1.
    void SetName(const char* Source, unsigned int Length);
    unsigned int Field_0x00;   // +0x20 object id (argument +0x000)
    unsigned int NameSize;     // +0x24 strlen + 1
    unsigned int Position[3];  // +0x28 argument +0x108..+0x110, float bits; not on the wire
    unsigned char Mode;        // +0x34 1 unless the argument's +0x114 is 2; not on the wire
    char* Name;                // +0x38 owned
};

// 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE: RC factory 06b4b350 / ctor 06b4b390, RS
// 06be5e70 / 06be5eb0 (size 0x2c); the ctor writes no payload field. Wire: dword,
// dword (the sender zero-extends a byte), word. The IClient argument is
// FServerMissionObjectiveStateInfo.
class FRpcServerSetMissionObjectiveStatePacket : public FPacketBase {
public:
    FRpcServerSetMissionObjectiveStatePacket() : FPacketBase(0x20033u) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetMissionObjectiveStatePacket>(OutPacket);
    }
    unsigned int Field_0x00;   // +0x20 argument +0x00
    unsigned int Field_0x04;   // +0x24 argument byte +0x04, zero-extended
    unsigned short Field_0x08; // +0x28 argument word +0x08
};

// 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE: RC factory 06b4b530 / ctor 06b4b570 / dtor
// 06b4b5c0, RS 06be6050 / 06be6090 / 06be60e0 (size 0x30). The ctor zeroes TextSize and
// TextData only; the dtor frees a non-null TextData. Wire: dword, dword (the sender
// zero-extends a byte), TextSize, TextSize bytes of a flattened FmtStr. The IClient
// argument is FServerReplaceMissionObjectiveInfo.
class FRpcServerReplaceMissionObjectivePacket : public FPacketBase {
public:
    FRpcServerReplaceMissionObjectivePacket()
        : FPacketBase(0x20034u), TextSize(0), TextData(nullptr) {}
    ~FRpcServerReplaceMissionObjectivePacket() override;
    FRpcServerReplaceMissionObjectivePacket(const FRpcServerReplaceMissionObjectivePacket&) = delete;
    FRpcServerReplaceMissionObjectivePacket& operator=(
        const FRpcServerReplaceMissionObjectivePacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerReplaceMissionObjectivePacket>(OutPacket);
    }
    unsigned int Field_0x00;  // +0x20 mission id (argument +0x00)
    unsigned int State;       // +0x24 argument byte +0x20, zero-extended
    unsigned int TextSize;    // +0x28
    unsigned char* TextData;  // +0x2c owned, flattened argument +0x04 FmtStr
};

// The {Size, Data} member block (a flattened FmtStr) of 0x35 SETMISSIONOBJECTIVES (+0x28,
// +0x30) and 0x29 SETREPUTATION (+0x20, +0x28). The packet ctors zero both dwords inline
// (RS 06be641f..06be6428 / 06be5a90..06be5a99), each block an EH state of its own. The
// destructor RS 06bdabb0 / RC 06b3ede0 frees a non-null Data; the packet dtors run it
// inline for B then A after their list (RS 06bdab6a..06bdab8b / 06bdd579..06bdd598), and
// the unwind funclets of both ctors and both dtors call it (RS 06c0a428/433, 06c0a4b8/4c3,
// 06c0a728/733, 06c0a778/783; RC calls at 06b6a79e/7a9, 06b6a95e/969, 06b6aa4e/a59,
// 06b6aa9e/aa9).
struct FRpcServerFlatTextBlock {
    FRpcServerFlatTextBlock() : Size(0), Data(nullptr) {}
    ~FRpcServerFlatTextBlock() {
        if (Data != nullptr) ::operator delete(Data);
    }
    FRpcServerFlatTextBlock(const FRpcServerFlatTextBlock&) = delete;
    FRpcServerFlatTextBlock& operator=(const FRpcServerFlatTextBlock&) = delete;
    unsigned int Size;   // +0x00
    unsigned char* Data; // +0x04 owned
};

// One objective of 0x35: operator new(0xc) in the sender (06b36c00) and the decoder
// (06b452f0); State is not initialised by either before it is written.
struct FRpcServerMissionObjectiveRecord {
    unsigned char State;  // +0x00 entry +0x18, low byte
    unsigned int Size;    // +0x04 flattened FmtStr size
    unsigned char* Data;  // +0x08 owned, flattened entry FmtStr
};

// 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES: RC factory 06b4b880 / ctor 06b4b8c0 / dtor
// 06b44d20, RS 06be63a0 / 06be63e0 / 06bdaad0 (retail size 0x48). The ctor zeroes the two
// blocks and Count and builds the record list; the dtor frees every record's Data and
// the record, then the members go: the list, TextB, TextA (block RAII). Wire: Field_0x00, Count, State, TextA
// block, TextB block, then, only when Count is non-zero, every list record as byte
// State, dword Size, Size bytes. Retail keeps a VC6 std::list<record*> inline (+0x3c
// allocator byte, +0x40 head, +0x44 size); the port keeps this toolchain's std::list of
// the same record pointers, so sizeof differs from retail (not a cross-module type).
class FRpcServerSetMissionObjectivesPacket : public FPacketBase {
public:
    FRpcServerSetMissionObjectivesPacket();
    ~FRpcServerSetMissionObjectivesPacket() override;
    FRpcServerSetMissionObjectivesPacket(const FRpcServerSetMissionObjectivesPacket&) = delete;
    FRpcServerSetMissionObjectivesPacket& operator=(
        const FRpcServerSetMissionObjectivesPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetMissionObjectivesPacket>(OutPacket);
    }
    // RC 06b44e20 (thiscall, RET 4): a null Record is ignored; otherwise append it and
    // increment Count.
    void PushObjective(FRpcServerMissionObjectiveRecord* Record);
    // RS 06bdac30 (thiscall, RET 4): the Index-th record, or null past the end.
    FRpcServerMissionObjectiveRecord* ObjectiveAt(unsigned int Index) const;
    unsigned int Field_0x00;  // +0x20 mission id (argument +0x00)
    unsigned int State;       // +0x24 argument byte +0x3c, zero-extended
    FRpcServerFlatTextBlock TextA; // +0x28 flattened argument +0x0c FmtStr
    FRpcServerFlatTextBlock TextB; // +0x30 flattened argument +0x24 FmtStr
    unsigned int Count;       // +0x38
    std::list<FRpcServerMissionObjectiveRecord*> Objectives; // retail +0x3c..+0x44
};

// 0x3c FLPACKET_SERVER_REPAIROBJECT: RC factory 06b4c490 / ctor 06b4c4d0, RS 06be7090 /
// 06be70d0 (size 0x2c). The ctor zeroes the three dwords. Wire: three dwords. One of
// the few classes with its own Dump slot (RC 06b61bf0 / RS 06bf7dd0).
class FRpcServerRepairObjectPacket : public FPacketBase {
public:
    FRpcServerRepairObjectPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerRepairObjectPacket>(OutPacket);
    }
    unsigned int ObjectId; // +0x20 Dump label "Object ID"
    unsigned int EquipId;  // +0x24 Dump label "Equip ID"; the sender zero-extends a word
    int Points;            // +0x28 Dump label "Points", printed signed
};

// 0x2d FLPACKET_SERVER_CREATECOUNTER: RC factory 06b4aa70 / ctor 06b4aab0, RS
// 06be5590 / 06be55d0 (size 0x68). The ctor zeroes every dword, seeds SubObjId with
// Common's SUBOBJ_ID_NONE and Orientation with {w=1,0,0,0}. Wire: four dwords, word,
// Orientation (one 16-byte reservation), three Vectors (one 12-byte reservation
// each), all integer copies (RC 06b39ec0 / 06b3a160). The sender's argument is
// FServerCreateProjectileInfo (server_createinfo.h); member comments name its field.
class FRpcServerCreateCounterPacket : public FPacketBase {
public:
    FRpcServerCreateCounterPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateCounterPacket>(OutPacket);
    }
    unsigned int ArchetypeNickname; // +0x20 info +0x04
    unsigned int ObjId;             // +0x24 info +0x00
    unsigned int Field_0x08;        // +0x28 info +0x44 (TODO: semantics unproven)
    unsigned int OwnerId;           // +0x2c info +0x08
    unsigned short SubObjId;        // +0x30 info +0x0c
    Quaternion Orientation;         // +0x34 info +0x1c
    Vector Position;                // +0x44 info +0x10
    Vector Velocity;                // +0x50 info +0x2c
    Vector Field_0x3c;              // +0x5c info +0x38 (TODO: semantics unproven)
};

// 0x37 FLPACKET_SERVER_CREATEGUIDED: RC factory 06b4a420 / ctor 06b4a460, RS
// 06be4f20 / 06be4f60 (size 0x58). The ctor writes only Position = 0 and
// Orientation = {w=1,0,0,0}. Wire: u16 Archetype::LargeIDToSmallID(nickname),
// OwnerId, u16 SubObjId, TargetId, u16 TargetSubObjId, ObjId, Position (12 bytes),
// then x, y, z, w each as (s8)ftol(f * 127.0). Field_0x00 never crosses the wire:
// the decoder zeroes it. The argument struct is FServerCreateGuidedInfo.
class FRpcServerCreateGuidedPacket : public FPacketBase {
public:
    FRpcServerCreateGuidedPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateGuidedPacket>(OutPacket);
    }
    unsigned int Field_0x00;        // +0x20 info +0x2c, not on the wire
    unsigned int ArchetypeNickname; // +0x24 info +0x28
    unsigned int OwnerId;           // +0x28 info +0x30
    unsigned short SubObjId;        // +0x2c info +0x34
    unsigned int TargetId;          // +0x30 info +0x20
    unsigned short TargetSubObjId;  // +0x34 info +0x24
    unsigned int ObjId;             // +0x38 info +0x00
    Vector Position;                // +0x3c info +0x04
    Quaternion Orientation;         // +0x48 info +0x10
};

// --- 0x28 CREATELOOT ---

// One loot record, the value of a VC6 std::list node (operator new(0x50): next, prev,
// then these 0x48 bytes; RC 06b44b40 / 06b44bd0, RS 06bde900). The sender fills it from
// a FServerLootDesc (server_createinfo.h), the RS receiver copies it back into one.
struct FRpcServerLootRecord {
    unsigned int LootId;            // +0x00 desc +0x00
    unsigned int ContentsNickname;  // +0x04 desc +0x04; a small id (u16) on the wire
    float ContentsHitPts;           // +0x08 desc +0x08
    unsigned short Units;           // +0x0c desc +0x0c, low word
    unsigned short Pad0e;           // +0x0e
    unsigned int ContainerNickname; // +0x10 desc +0x10; a small id (u16) on the wire
    float HitPts;                   // +0x14 desc +0x14
    unsigned int NameOverride;      // +0x18 desc +0x18
    Vector Position;                // +0x1c desc +0x1c
    Vector Velocity;                // +0x28 desc +0x38
    Quaternion Orientation;         // +0x34 desc +0x28, x and y swapped by the sender
    unsigned char bMarkedBy;        // +0x44 desc +0x46
    unsigned char bTemporary;       // +0x45 desc +0x45
    unsigned char Pad46[2];         // +0x46
};
static_assert(sizeof(FRpcServerLootRecord) == 0x48, "CREATELOOT list record 0x48");

// 0x28 FLPACKET_SERVER_CREATELOOT: RC factory 06b4ada0 / ctor 06b4ade0 / dtor 06b4ae90,
// RS 06be58c0 / 06be5900 (retail size 0x34). The ctor zeroes OwnerId and Kind and builds
// the list. Wire: OwnerId, byte Kind, byte list size, then per record LootId, u16 small
// id of ContentsNickname, ContentsHitPts (FLD/FSTP), u16 Units, u16 small id of
// ContainerNickname, HitPts (FLD/FSTP), NameOverride, Position, and ONLY in single player
// Velocity and Orientation, then the two bytes. Retail keeps a VC6 std::list inline
// (+0x28 allocator byte, +0x2c head, +0x30 size); the port keeps this toolchain's
// std::list of the same records, so sizeof differs from retail (not a cross-module type).
class FRpcServerCreateLootPacket : public FPacketBase {
public:
    FRpcServerCreateLootPacket() : FPacketBase(0x20028u), OwnerId(0), Kind(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateLootPacket>(OutPacket);
    }
    // RC 06b44b40 (thiscall, RET 4): append a copy of Record.
    void AddLoot(const FRpcServerLootRecord& Record);
    unsigned int OwnerId; // +0x20 request +0x00
    unsigned char Kind;   // +0x24 request +0x04, low byte
    std::list<FRpcServerLootRecord> Loot; // retail +0x28..+0x30
};

// --- relation lists: 0x29 SETREPUTATION, 0x2b SETGROUPFEELINGS ---

// RC 06b3f6c0 / 06b43da0, RS 06bddef0 / 06bdd430 (one body, the list offset differs):
// for every non-null relation of Source, operator new(8) with no null test, Feeling then
// GroupId copied as integers, appended to Dest.
void RpcServerRelationListAppendCopy(std::list< ::Reputation::Relation*>& Dest,
    const std::list< ::Reputation::Relation*>& Source);

// 0x29 FLPACKET_SERVER_SETREPUTATION: RC factory 06b4aef0 / ctor 06b4af30 / dtor 06b3ed00,
// RS 06be5a10 / 06be5a50 / 06bdd4e0 (retail size 0x68), own Dump (RC 06b3f7c0 / RS
// 06bddf70). The five presence masks are packet fields the ctor seeds with 1, 2, 4, 8 and
// 0x10. Encode recomputes Flags, then: Flags, Target, Affiliation when present, the
// display name (byte length, length * 2 bytes) and the two flattened FmtStr blocks when
// present, then on an MP server the Attitude float (FLD/FSTP), otherwise the relation
// count (byte or word) and per relation Feeling (FLD/FSTP), GroupId. The decoder reads
// the attitude when NOT in single player, the relations when in single player. Retail
// keeps a VC6 std::list inline (+0x48 allocator byte, +0x4c head, +0x50 size); the port
// keeps this toolchain's std::list of the same pointers, so sizeof differs from retail.
class FRpcServerSetReputationPacket : public FPacketBase {
public:
    FRpcServerSetReputationPacket();
    ~FRpcServerSetReputationPacket() override;
    FRpcServerSetReputationPacket(const FRpcServerSetReputationPacket&) = delete;
    FRpcServerSetReputationPacket& operator=(const FRpcServerSetReputationPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetReputationPacket>(OutPacket);
    }
    // RC 06b3f670 (thiscall, RET 8): when Source is non-null and Length > 0, DisplayName =
    // operator new((Length + 1) * 4) with no null test, (Length + 1) * 2 bytes copied,
    // DisplayNameLength = Length + 1. A previous block is not released.
    void SetDisplayName(const wchar_t* Source, unsigned int Length);
    FRpcServerFlatTextBlock NameA;  // +0x20 flattened argument +0x18 FmtStr
    FRpcServerFlatTextBlock NameB;  // +0x28 flattened argument +0x30 FmtStr
    float Attitude;                 // +0x30 argument +0x04
    unsigned int MaskAffiliation;   // +0x34 ctor 1
    unsigned int MaskNames;         // +0x38 ctor 2
    unsigned int MaskDisplayName;   // +0x3c ctor 4
    unsigned int MaskRelationsByte; // +0x40 ctor 8
    unsigned int MaskRelationsWord; // +0x44 ctor 0x10
    std::list< ::Reputation::Relation*> Relations; // retail +0x48..+0x50, owned values
    unsigned char Flags;            // +0x54
    unsigned int Target;            // +0x58 argument +0x00, Dump label "Target"
    unsigned int Affiliation;       // +0x5c argument +0x08, Dump label "Affiliation"; ctor -1
    int DisplayNameLength;          // +0x60 wide characters, terminator included
    wchar_t* DisplayName;           // +0x64 owned
};

// 0x2b FLPACKET_SERVER_SETGROUPFEELINGS: RC factory 06b49f20 / ctor 06b49f60 / dtor
// 06b43870, RS 06be4a00 / 06be4a40 / (06bdcf40) (retail size 0x34). The ctor zeroes Name
// and NameSize and builds the list. Wire (encoder): NameSize, NameSize bytes when it is
// non-zero, then, when the low word of the list size is non-zero, per non-null relation
// Feeling (FLD/FSTP), GroupId. The encoder never writes a count, but the decoder reads a
// u16 count after the name: retail's two sides disagree, reproduced on purpose. Retail
// keeps a VC6 std::list inline (+0x28 allocator byte, +0x2c head, +0x30 size).
class FRpcServerSetGroupFeelingsPacket : public FPacketBase {
public:
    FRpcServerSetGroupFeelingsPacket() : FPacketBase(0x2002bu), Name(nullptr), NameSize(0) {}
    ~FRpcServerSetGroupFeelingsPacket() override;
    FRpcServerSetGroupFeelingsPacket(const FRpcServerSetGroupFeelingsPacket&) = delete;
    FRpcServerSetGroupFeelingsPacket& operator=(const FRpcServerSetGroupFeelingsPacket&) = delete;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerSetGroupFeelingsPacket>(OutPacket);
    }
    // RC 06b43920 (thiscall, RET 8): free the old Name, Name = operator new(Size) with no
    // null test, strncpy(Name, Source, Size), NameSize = Size.
    void SetName(const char* Source, unsigned int Size);
    char* Name;              // +0x20 owned
    unsigned int NameSize;   // +0x24 strlen + 1
    std::list< ::Reputation::Relation*> Relations; // retail +0x28..+0x30, owned values
};

// 0x2c FLPACKET_SERVER_CREATEMINE: RC factory 06b4a9a0 / ctor 06b4a9e0, RS 06be54c0 /
// 06be5500 (size 0x68). The ctor zeroes every field and seeds Orientation {w=1,0,0,0};
// unlike 0x2d it leaves SubObjId 0. Wire: ObjId, u16 small id of ArchetypeNickname,
// OwnerId, u16 SubObjId, Position, and ONLY in single player Orientation, Velocity,
// Field_0x38, all integer copies (RC 06b3a160 / 06b39ec0). Field_0x44 never crosses the
// wire; the decoder zeroes it. The argument is FServerCreateProjectileInfo.
class FRpcServerCreateMinePacket : public FPacketBase {
public:
    FRpcServerCreateMinePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCreateMinePacket>(OutPacket);
    }
    unsigned int ObjId;             // +0x20 info +0x00
    unsigned int ArchetypeNickname; // +0x24 info +0x04
    unsigned int OwnerId;           // +0x28 info +0x08
    unsigned short SubObjId;        // +0x2c info +0x0c
    Quaternion Orientation;         // +0x30 info +0x1c
    Vector Position;                // +0x40 info +0x10
    Vector Velocity;                // +0x4c info +0x2c
    Vector Field_0x38;              // +0x58 info +0x38 (TODO: semantics unproven)
    unsigned int Field_0x44;        // +0x64 info +0x44, not on the wire
};

// 0x3d FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE: RC factory 06b4cf60 / ctor 06b4cfa0 /
// dtor 06b45490 (sets its vtable, then the equipment base dtor 06b4f860), RS 06be7b80 /
// 06be7bc0 / 06bda7c0 (size 0x2c). The ctor zeroes the handle, Flag and ObjectId. Wire:
// ObjectId, raw byte Flag, then the equipment list (06b4f8d0 / 06b4fb10).
class FRpcServerRemoteObjectCargoUpdatePacket : public FRpcServerEquipListPacketBase {
public:
    FRpcServerRemoteObjectCargoUpdatePacket()
        : FRpcServerEquipListPacketBase(0x2003du), Flag(0), ObjectId(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerRemoteObjectCargoUpdatePacket>(OutPacket);
    }
    unsigned char Flag;    // +0x24 IClient::SendServerRemoteObjectCargoUpdate Flag, raw byte
    unsigned int ObjectId; // +0x28 IClient::SendServerRemoteObjectCargoUpdate ObjectId
};

// ===== end SERVER 0x20..0x3f (RS-P2) =====

// ===== SERVER 0x01..0x1f (RS-P1) part 2 =====
// 0x03 CHARACTERINFO sits here, after RS-P2's block, because its wire record holds
// RS-P2's FRpcServerEquipHandle by value.
#include "common_archetype.h" // CollisionGroupDesc

// The id block a wire record points to: operator new(0x18), four dwords, a count and
// an operator new(Count * 4) array. Built from a Costume by the sender (RC 06b355f8 /
// 06b3569a), read back into one by HandleCharacterInfo (RS 06bd49f4..06bd4af7).
struct FRpcServerCharacterIdBlock {
    unsigned int Field_0x00; // +0x00 Costume::PartIds[0]
    unsigned int Field_0x04; // +0x04 Costume::PartIds[1]
    unsigned int Field_0x08; // +0x08 Costume::PartIds[2]
    unsigned int Field_0x0c; // +0x0c Costume::PartIds[3]
    int Count;               // +0x10 Costume::AccessoryCount
    unsigned int* Ids;       // +0x14 Costume::AccessoryIds, Count entries
};

// RS 06bd4ed0 (scalar deleting dtor, = rpclocal 06d84c10) and the packet dtor's
// inline pair in RS 06bdea60: a non-null Ids array is freed, then the block.
void RpcServerCharacterIdBlockFree(FRpcServerCharacterIdBlock* Block);

// The wire record, operator new(0x27c) in retail (RC sender 06b353df, decoder RS
// 06bdec90, receiver copy RS 06bdfbc0). Member order is the record's. The two strings
// are retail MSVCP60 basic_string<unsigned short>; here std::wstring (no retail ABI).
struct FRpcServerCharacterRecord {
    // The decoder's / copy's inline ctor (RS 06bdecb3..06bded56): name byte, both flag
    // bytes, the strings, +0x224, +0x228, +0x22c, +0x244..+0x25c, +0x260, the handle,
    // the collision list and both block pointers. +0x230..+0x240 are left alone in
    // retail and zeroed here; every path overwrites them before they are read.
    FRpcServerCharacterRecord();
    // The member teardown every owner runs (packet dtor RS 06bdea60 pass 1, receiver
    // RS 06bd4cbe..06bd4d33): BlockA, BlockB, then the members in reverse order: the
    // collision list, the equipment handle (RS 06bdc790), TextB, TextA.
    ~FRpcServerCharacterRecord();
    FRpcServerCharacterRecord(const FRpcServerCharacterRecord&) = delete;
    FRpcServerCharacterRecord& operator=(const FRpcServerCharacterRecord&) = delete;

    CHARACTER_ID Name;               // +0x000
    unsigned char Field_0x200;       // +0x200 CharacterInfo::bIsRestart, raw on the wire
    unsigned char Field_0x201;       // +0x201 CharacterInfo::bIsAutoSave, raw on the wire
    std::wstring TextA;              // +0x204 CharacterInfo::Name
    std::wstring TextB;              // +0x214 CharacterInfo::Description
    unsigned int Field_0x224;        // +0x224 <- CharacterInfo +0x224 DescripStrId
    unsigned int Field_0x228;        // +0x228 <- +0x228 TimestampLow
    unsigned int Field_0x22c;        // +0x22c <- +0x22c TimestampHigh
    unsigned int Field_0x230;        // +0x230 <- +0x230 ShipArchetype
    unsigned int Field_0x234;        // +0x234 <- +0x234 Money
    unsigned int Field_0x238;        // +0x238 <- +0x260 SystemId
    unsigned int Field_0x23c;        // +0x23c <- +0x264 BaseId
    unsigned int Field_0x240;        // +0x240 <- +0x268 LocationId
    unsigned int Field_0x244;        // +0x244 <- +0x26c VoiceId
    unsigned int Field_0x248;        // +0x248 <- +0x270 Rank
    unsigned int Field_0x24c;        // +0x24c TODO: no writer but the ctors' zero; travels
    unsigned int Field_0x250;        // +0x250 <- +0x244 HullStatus bits (encoder FLD/FSTP)
    unsigned int Field_0x254;        // +0x254 <- +0x238 NumKills
    unsigned int Field_0x258;        // +0x258 <- +0x240 NumMissionFailures
    unsigned int Field_0x25c;        // +0x25c <- +0x23c NumMissionSuccesses
    unsigned char Field_0x260;       // +0x260 equipment element count (low byte of size)
    FRpcServerEquipHandle Equipment; // +0x264 RS-P2's 4-byte handle, inline
    std::list<CollisionGroupDesc> CollisionGroups; // +0x268 Common.dll's list
    FRpcServerCharacterIdBlock* BlockA; // +0x274 <- CharacterInfo::ComCostume
    FRpcServerCharacterIdBlock* BlockB; // +0x278 <- CharacterInfo::BaseCostume
};

// 0x03 FLPACKET_SERVER_CHARACTERINFO (CDAPCharacterInfo). RC factory 06b4b1e0 / ctor
// 06b4b220 / dtor 06b42c70, RS 06be5d00 / 06be5d40 / 06be1280 (body 06bdea60);
// operator new(0x30). The ctor builds only the record list (+0x24 allocator byte,
// +0x28 dummy, +0x2c size 0); +0x20 is left unset. Codecs RC 06b41aa0 / 06b40500,
// RS 06be00e0 / 06bdebe0 (encoders and RS decoder instruction-identical to rpclocal
// 06d949d0 / 06d934d0; the RC decoder bounds the name and checks byte runs before it
// allocates them). Dump RC 06b40340 / RS 06bdea20.
class FRpcServerCharacterInfoPacket : public FPacketBase {
public:
    FRpcServerCharacterInfoPacket() : FPacketBase(0x20003u) {}
    // RS 06bdea60: pass 1 deletes every non-null record, pass 2 frees the nodes.
    ~FRpcServerCharacterInfoPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcServerCharacterInfoPacket>(OutPacket);
    }
    unsigned int SaveFailureCount;                   // +0x20 CCharacterSet +0x14
    std::list<FRpcServerCharacterRecord*> Records;   // +0x24 retail VC6 list<record*>
};

// ===== end SERVER 0x01..0x1f (RS-P1) part 2 =====
