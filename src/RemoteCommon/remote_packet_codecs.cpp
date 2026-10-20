// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_rpc_quantization.h"
#include "rpc_packet_codec.h"
#include "common_misc.h" // SinglePlayer selects retail's two FIREWEAPON formats.
#include "common_net.h" // PLAYER_TRADE counts its trailer by IsMPServer direction.
#include "common_text.h" // Canonical FmtStr::Val size calculation.

#include <cfloat>
#include <cwchar>
#include <cstdio>
#include <ostream>

namespace {
// A distinct private global in each Remote DLL, as in both retail images.
std::map<unsigned int, FRpcWireStats> GRemoteWireStats;
}

std::map<unsigned int, FRpcWireStats>& RpcRemoteWireStats() {
    return GRemoteWireStats;
}

FRpcWireStats* RpcFindRemoteWireStats(unsigned int PacketId) {
    return &GRemoteWireStats[PacketId];
}

void FPacketSerializer::DumpStats(const char* Name) const {
    FILE* File = std::fopen(Name, "w");
    if (File == nullptr) return;
    std::fprintf(File, "Packet Name,In Count,In Max Size,In Avg Size,In Total Bytes,Exec Time Total,Exec Time Avg,Out Count,Out Max Size,Out Avg Size,Out Total Bytes\n\n");
    for (std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.begin();
        It != Types.end(); ++It) {
        FRpcWireStats* Stats = RpcFindRemoteWireStats(It->first);
        if (Stats->EncodedCount == 0 && Stats->DecodedCount == 0) continue;
        std::fprintf(File, "%s,", It->second->Name.c_str());
        std::fprintf(File, "%u,%u,%u,%u,%f,%f,", Stats->DecodedCount,
            Stats->DecodedMaximum, Stats->DecodedAverage, Stats->DecodedBytes,
            Stats->ExecTimeTotal, Stats->ExecTimeAvg);
        std::fprintf(File, "%u,%u,%u,%u", Stats->EncodedCount,
            Stats->EncodedMaximum, Stats->EncodedAverage, Stats->EncodedBytes);
        std::fprintf(File, "\n");
    }
    std::fclose(File);
}

FRpcSaveGamePacket::FRpcSaveGamePacket() : FPacketBase(0x3001eu) {
    // Native writes only CharacterId.Buffer[0]; canonical CHARACTER_ID also
    // initializes its unused tail. Description and Count stay uninitialized.
}

FRpcSaveGamePacket::~FRpcSaveGamePacket() {
    if (Description != nullptr) ::operator delete(Description);
}

void FRpcSaveGamePacket::CopyDescription(const wchar_t* Source, unsigned int Count) {
    if (Count == 0 || Source == nullptr) return;
    const unsigned int ByteCount = Count * 2u;
    Description = static_cast<wchar_t*>(::operator new(ByteCount, std::nothrow));
    if (ByteCount != 0) std::memcpy(Description, Source, ByteCount);
    DescriptionCount = Count;
}

unsigned int FRpcSaveGamePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b55690 / RS06be3920.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DescriptionCount, 4u)) return 0x16u;
    if (DescriptionCount != 0) {
        const unsigned int ByteCount = DescriptionCount * 2u;
        if (!Stream.Reserve(ByteCount, 0x16u)) return 0x16u;
        // RC06b55748..06b55772: cached destination, REP copy, Ptr=cached+bytes.
        Stream.PutReserved(Description, ByteCount);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcSaveGamePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned int Count;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    DescriptionCount = Count;
    if (DescriptionCount != 0) {
        const unsigned int ByteCount = DescriptionCount * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        // RC preflights without committing cursor; RS allocates before bounds.
        if (MarshalCursor + ByteCount > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        Description = static_cast<wchar_t*>(::operator new(ByteCount, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        if (Description == nullptr) return 5u;
#endif
        if (!Stream.Get(Description, ByteCount)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

FRpcTradeResponsePacket::FRpcTradeResponsePacket()
    : FPacketBase(0x30027u), DataSize(0), Data(nullptr) {}

FRpcTradeResponsePacket::~FRpcTradeResponsePacket() {
    ::operator delete(Data);
    Data = nullptr;
}

void FRpcTradeResponsePacket::CopyData(const unsigned char* Source, unsigned int Size) {
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Size != 0) std::memcpy(Data, Source, Size);
    DataSize = Size;
}

unsigned int FRpcTradeResponsePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54510 / RS06be3660.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return 0x16u;
    // Literal native wire pointer DWORD, not the pointed-to buffer's bytes
    // (RC06b5458c..06b545bf: reserve, cached destination, load +0x24, commit).
    if (!Stream.Put(&Data, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcTradeResponsePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
#if defined(REMOTECLIENT_EXPORTS)
    unsigned int Count;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    DataSize = Count;
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = 0x17u;
        return MarshalError;
    }
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
    if (!Stream.Get(Data, DataSize)) return MarshalError;
#else
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
    if (!Stream.Reserve(4u, 0x17u)) return MarshalError;
    unsigned int PointerBits;
    std::memcpy(&PointerBits, reinterpret_cast<const void*>(MarshalPtr), 4u);
    // Native overwrites and leaks the fresh allocation with the wire DWORD.
    Data = reinterpret_cast<unsigned char*>(PointerBits);
    Stream.CommitReadPointer(4u);
#endif
    return Stream.Finish(OutLength, false);
}

void FRpcConnectResponsePacket::Dump(std::ostream& Out) const {
    Out << "   Client ID: " << Value << "\n";
}

void FRpcLoginResponsePacket::Dump(std::ostream& Out) const {
    // Retail selects the unsigned-character inserter, not a decimal integer.
    Out << "   Result   : " << Value << "\n";
}

unsigned int FRpcMissionResponsePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54220 / RS06c02750.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&MissionId, 4u)) return 0x16u;
    if (!Stream.Put(&bAccepted, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMissionResponsePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned int First;
    if (!Stream.Get(&First, 4u)) return MarshalError;
    Field_0x00 = First;
    unsigned long Second;
    if (!Stream.Get(&Second, 4u)) return MarshalError;
    MissionId = Second;
    if (!Stream.Get(&bAccepted, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int RpcGetFlattenedFmtStrSize(const FmtStr& Text) {
    unsigned int Size = 6u;
    for (const FmtStr::Val* Node = Text.Head; Node != nullptr; Node = Node->Next) {
        Size += Node->get_flattened_size();
    }
    return Size;
}

// RC06b3a180 / RS06bda4e0: operate on the caller's existing marshal state.
unsigned int RpcWireEncodeCollisionGroups(FRpcWireStream& Stream,
    const std::list<CollisionGroupDesc>& Groups) {
    const unsigned short Count = static_cast<unsigned short>(Groups.size());
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    if (Count != 0) {
        for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin();
            It != Groups.end(); ++It) {
            if (!Stream.Put(&It->Id, 2u)) return 0x16u;
            if (!Stream.PutFloat(It->Health)) return 0x16u;
        }
    }
    return 0;
}

// RC06b3a230 / RS06bda590: clear before even checking the count prefix.
unsigned int RpcWireDecodeCollisionGroups(FPacketBase& Packet, FRpcWireStream& Stream,
    std::list<CollisionGroupDesc>& Groups) {
    Groups.clear();
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return Packet.MarshalError;
    for (unsigned int Index = 0; Index < Count; ++Index) {
        // Native appends a copy of two uninitialized stack DWORDs before bounds.
        // The canonical modern node is value-initialized instead; retain this
        // explicit partial-node/padding and allocator/EH representation gap.
        Groups.emplace_back();
        CollisionGroupDesc& Group = Groups.back();
        if (!Stream.Get(&Group.Id, 2u)) return Packet.MarshalError;
        if (!Stream.Get(&Group.Health, 4u)) return Packet.MarshalError;
    }
    return 0;
}

namespace {
template<typename T>
bool RpcWireGetThenStore(FRpcWireStream& Stream, T& Field) {
    T Value;
    if (!Stream.Get(&Value, sizeof(T))) return false;
    Field = Value;
    return true;
}

bool RpcWirePutVector(FRpcWireStream& Stream, const Vector& Value) {
    if (!Stream.Reserve(12u, 0x16u)) return false;
    // CLIENT03 RC06b5340c..06b5343a: native snapshots all three DWORDs and the
    // destination once, MOV-copies them (06b3a160), then commits Ptr=cached+12.
    unsigned int Bits[3];
    std::memcpy(&Bits[0], &Value.x, 4u);
    std::memcpy(&Bits[1], &Value.y, 4u);
    std::memcpy(&Bits[2], &Value.z, 4u);
    Stream.PutReserved(Bits, 12u);
    return true;
}

bool RpcWireGetVector(FPacketBase& Packet, FRpcWireStream& Stream, Vector& Value) {
    if (!Stream.Reserve(12u, 0x17u)) return false;
    // RC06b3a160 / RS06bda4c0: MOV DWORD load/store pairs, no x87 operations.
    std::memcpy(&Value.x, reinterpret_cast<const void*>(Packet.MarshalPtr), 4u);
    std::memcpy(&Value.y, reinterpret_cast<const void*>(Packet.MarshalPtr + 4u), 4u);
    std::memcpy(&Value.z, reinterpret_cast<const void*>(Packet.MarshalPtr + 8u), 4u);
    Stream.CommitReadPointer(12u);
    return true;
}
}

unsigned int FRpcRequestCollisionGroupsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5a830 / RS06c078c0: a nonzero helper result is returned unchanged.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = RpcWireEncodeCollisionGroups(Stream, Groups);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestCollisionGroupsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = RpcWireDecodeCollisionGroups(*this, Stream, Groups);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

FRpcDebugCreateShipPacket::FRpcDebugCreateShipPacket()
    : FPacketBase(0x30022u), Field_0x14{0.0f, 0.0f, 0.0f},
      Field_0x20{0.0f, 0.0f, 0.0f}, Field_0x2c{1.0f, 0.0f, 0.0f, 0.0f},
      Field_0x3c(0), WideData(nullptr), WideCount(0), DataA(nullptr), DataSizeA(0),
      DataB(nullptr), DataSizeB(0), Field_0x50(0), Field_0x54(0), Field_0x58(0),
      Field_0x5c(0), ArrayCount(0), ArrayData(nullptr) {}

FRpcDebugCreateShipPacket::~FRpcDebugCreateShipPacket() {
    if (WideData != nullptr) ::operator delete(WideData);
    if (DataA != nullptr) ::operator delete(DataA);
    if (DataB != nullptr) ::operator delete(DataB);
    if (ArrayData != nullptr) ::operator delete(ArrayData);
}

unsigned int FRpcDebugCreateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b58290 / RS06c05c80: every failure returns 0x16 (literal or ECX=0x16).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x0c, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x10, 4u)) return 0x16u;
    if (!RpcWirePutVector(Stream, Field_0x14)) return 0x16u;
    if (!RpcWirePutVector(Stream, Field_0x20)) return 0x16u;
    if (!Stream.Reserve(16u, 0x16u)) return 0x16u;
    // RC06b58473..06b584a2 / RS06c05e63..5e92: snapshot W/X/Y/Z as DWORDs and the
    // destination, MOV-copy all four (06b39ec0), then commit Ptr=cached+16.
    unsigned int QuaternionBits[4];
    std::memcpy(&QuaternionBits[0], &Field_0x2c.w, 4u);
    std::memcpy(&QuaternionBits[1], &Field_0x2c.x, 4u);
    std::memcpy(&QuaternionBits[2], &Field_0x2c.y, 4u);
    std::memcpy(&QuaternionBits[3], &Field_0x2c.z, 4u);
    Stream.PutReserved(QuaternionBits, 16u);
    if (!Stream.Put(&Field_0x3c, 4u)) return 0x16u;
    if (!Stream.Put(&WideCount, 4u)) return 0x16u;
    if (WideCount != 0) {
        const unsigned int Width = WideCount * 2u;
        if (!Stream.Reserve(Width, 0x16u)) return 0x16u;
        // RC06b5853b..06b58560. Native unsigned multiplication may wrap to zero;
        // REP then does no access and the pointer commit adds zero.
        Stream.PutReserved(WideData, Width);
    }
    if (!Stream.Put(&DataSizeA, 4u)) return 0x16u;
    if (DataSizeA != 0) {
        // RC06b585a3..06b585ee: reserve, cached REP copy, Ptr=cached+size.
        if (!Stream.PutBlock(DataA, DataSizeA)) return 0x16u;
    }
    if (!Stream.Put(&DataSizeB, 4u)) return 0x16u;
    if (DataSizeB != 0) {
        // RC06b58631..06b5867f.
        if (!Stream.PutBlock(DataB, DataSizeB)) return 0x16u;
    }
    if (!Stream.Put(&Field_0x50, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x54, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x58, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x5c, 4u)) return 0x16u;
    if (!Stream.Put(&ArrayCount, 4u)) return 0x16u;
    if (ArrayCount != 0) {
        const unsigned int Width = ArrayCount * 4u;
        if (!Stream.Reserve(Width, 0x16u)) return 0x16u;
        // RC06b587bd..06b587e9.
        Stream.PutReserved(ArrayData, Width);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugCreateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, Field_0x00)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x04)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x08)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x0c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x10)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Field_0x14)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Field_0x20)) return MarshalError;
    if (!Stream.Reserve(16u, 0x17u)) return MarshalError;
    std::memcpy(&Field_0x2c.w, reinterpret_cast<const void*>(MarshalPtr), 4u);
    std::memcpy(&Field_0x2c.x, reinterpret_cast<const void*>(MarshalPtr + 4u), 4u);
    std::memcpy(&Field_0x2c.y, reinterpret_cast<const void*>(MarshalPtr + 8u), 4u);
    std::memcpy(&Field_0x2c.z, reinterpret_cast<const void*>(MarshalPtr + 12u), 4u);
    Stream.CommitReadPointer(16u);
    if (!RpcWireGetThenStore(Stream, Field_0x3c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, WideCount)) return MarshalError;
    if (WideCount != 0) {
        const unsigned int Width = WideCount * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        // RC has a noncommitting preflight before allocation; RS allocates first.
        if (MarshalCursor + Width > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        WideData = static_cast<wchar_t*>(::operator new(Width, std::nothrow));
        if (!Stream.Reserve(Width, 0x17u)) return MarshalError;
        if (Width != 0) std::memcpy(WideData, reinterpret_cast<const void*>(MarshalPtr), Width);
        Stream.CommitReadPointer(Width);
    }
    if (!RpcWireGetThenStore(Stream, DataSizeA)) return MarshalError;
    if (DataSizeA != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + DataSizeA > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        DataA = static_cast<unsigned char*>(::operator new(DataSizeA, std::nothrow));
        if (!Stream.Reserve(DataSizeA, 0x17u)) return MarshalError;
        std::memcpy(DataA, reinterpret_cast<const void*>(MarshalPtr), DataSizeA);
        Stream.CommitReadPointer(DataSizeA);
    }
    if (!RpcWireGetThenStore(Stream, DataSizeB)) return MarshalError;
    if (DataSizeB != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + DataSizeB > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        DataB = static_cast<unsigned char*>(::operator new(DataSizeB, std::nothrow));
        if (!Stream.Reserve(DataSizeB, 0x17u)) return MarshalError;
        std::memcpy(DataB, reinterpret_cast<const void*>(MarshalPtr), DataSizeB);
        Stream.CommitReadPointer(DataSizeB);
    }
    // Native alternates store-before-pointer and pointer-before-store here.
    if (!Stream.Get(&Field_0x50, 4u)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x54)) return MarshalError;
    if (!Stream.Get(&Field_0x58, 4u)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x5c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ArrayCount)) return MarshalError;
    if (ArrayCount != 0) {
        const unsigned int Width = ArrayCount * 4u;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Width > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        ArrayData = static_cast<unsigned int*>(::operator new(Width, std::nothrow));
        if (!Stream.Reserve(Width, 0x17u)) return MarshalError;
        if (Width != 0) std::memcpy(ArrayData, reinterpret_cast<const void*>(MarshalPtr), Width);
        Stream.CommitReadPointer(Width);
    }
    return Stream.Finish(OutLength, false);
}

FRpcDebugLoadSystemPacket::FRpcDebugLoadSystemPacket()
    : FPacketBase(0x30023u), Data(nullptr), DataSize(0) {}

FRpcDebugLoadSystemPacket::~FRpcDebugLoadSystemPacket() {
    if (Data != nullptr) ::operator delete(Data);
}

unsigned int FRpcDebugLoadSystemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b58e80 / RS06c06800.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return 0x16u;
    // RC06b58efa rereads the size after the prefix store; the body is reserved
    // unconditionally, then RC06b58f2b..06b58f55 copies to the cached destination
    // (a zero REP count performs no access) and commits Ptr=cached+size.
    if (!Stream.Reserve(DataSize, 0x16u)) return 0x16u;
    Stream.PutReserved(Data, DataSize);
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugLoadSystemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, DataSize)) return MarshalError;
    if (DataSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        // RC06b59036..5904b checks the body without committing its cursor.
        // RS06c069b6 allocates first. Neither decoder has an OOM-return guard.
        if (MarshalCursor + DataSize > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        Data = static_cast<char*>(::operator new(DataSize, std::nothrow));
        if (!Stream.Reserve(DataSize, 0x17u)) return MarshalError;
        std::memcpy(Data, reinterpret_cast<const void*>(MarshalPtr), DataSize);
        Stream.CommitReadPointer(DataSize);
    }
    return Stream.Finish(OutLength, false);
}

void FRpcDebugLoadSystemPacket::CopyString(const char* Text) {
    if (Text != nullptr) {
        const unsigned int Length = static_cast<unsigned int>(std::strlen(Text)) + 1u;
        Data = static_cast<char*>(::operator new(Length, std::nothrow));
        std::strcpy(Data, Text);
        DataSize = Length;
    }
}

unsigned int FRpcDebugDestroyObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59530 / RS06c06e90.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugDestroyObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, Field_0x00)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugConsolePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59a00 / RS06c07360: the 0x80-byte command is copied by REP MOVSD into
    // the cached destination (RC06b59aac..06b59acb), then Ptr=cached+0x80.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.PutBlock(Command, sizeof(Command))) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugConsolePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(Command, sizeof(Command))) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugRelocateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59760 / RS06c070c0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!RpcWirePutVector(Stream, Position)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugRelocateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Position)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugFreeSystemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b591b0 / RS06c06b10: header, then the shared trailer tail.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugFreeSystemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

FRpcMunitionCollisionPacket::FRpcMunitionCollisionPacket()
    : FPacketBase(0x30003u), Field_0x00(0), Field_0x04(0), ObjectId(0),
      Field_0x0c(SUBOBJ_ID_NONE), HitPosition{0.0f, 0.0f, 0.0f} {}

FRpcObjectCollisionPacket::FRpcObjectCollisionPacket()
    : FPacketBase(0x3000cu), TargetObjectId(0), TargetSubObjId(0), ObjectId(0),
      SubObjId(0), Field_0x10(0.0f) {}

FRpcBadlandsObjectCollisionPacket::FRpcBadlandsObjectCollisionPacket()
    : FPacketBase(0x3003du), Field_0x00(0), Field_0x08(0), ObjectId(0) {}

FRpcMineAsteroidPacket::FRpcMineAsteroidPacket()
    : FPacketBase(0x30020u), Position{0.0f, 0.0f, 0.0f} {}

FRpcSetManeuverPacket::FRpcSetManeuverPacket()
    : FPacketBase(0x3002fu), Field_0x00(0), ObjectId(0), Field_0x08(0) {}

FRpcRequestEventPacket::FRpcRequestEventPacket() : FPacketBase(0x30031u) {}
FRpcRequestCancelPacket::FRpcRequestCancelPacket() : FPacketBase(0x30032u) {}

unsigned int FRpcMunitionCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b532f0 / RS06c01820: every failure exits through EAX=0x16 (06b5334d).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x0c, 2u)) return 0x16u;
    if (!RpcWirePutVector(Stream, HitPosition)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMunitionCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, Field_0x00)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x04)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x0c)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, HitPosition)) return MarshalError;
    return Stream.Finish(OutLength, false);
}


unsigned int FRpcObjectCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53910 / RS06c01e40: every failure returns EBP=0x16 (06b53967).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&SubObjId, 2u)) return 0x16u;
    if (!Stream.Put(&TargetObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&TargetSubObjId, 2u)) return 0x16u;
    if (!Stream.PutFloat(Field_0x10)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcObjectCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, SubObjId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetSubObjId)) return MarshalError;
    if (!Stream.Get(&Field_0x10, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}


unsigned int FRpcMineAsteroidPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53c60 / RS06c02190: every failure returns 0x16 (literal or EBP).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SystemId, 4u)) return 0x16u;
    if (!RpcWirePutVector(Stream, Position)) return 0x16u;
    if (!Stream.Put(&LootId, 4u)) return 0x16u;
    if (!Stream.Put(&SecondaryLoot, 4u)) return 0x16u;
    if (!Stream.Put(&Quantity, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMineAsteroidPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SystemId, 4u)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Position)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, LootId)) return MarshalError;
    if (!Stream.Get(&SecondaryLoot, 4u)) return MarshalError;
    if (!Stream.Get(&Quantity, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}


unsigned int FRpcSetManeuverPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    const unsigned int* Fields[3] = { &Field_0x00, &ObjectId, &Field_0x08 };
    return RpcWireEncodeDwords(*this, Fields, 3u, BufferHolder, Capacity, OutLength);
}

unsigned int FRpcSetManeuverPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    unsigned int* Fields[3] = { &Field_0x00, &ObjectId, &Field_0x08 };
    return RpcWireDecodeDwords(*this, Fields, 3u, Buffer, Size, OutLength);
}

unsigned int FRpcRequestEventPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59f60 / RS06bf2440: every failure returns EBP=0x16 (06b59fb5).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EventType, 1u)) return 0x16u;
    if (!Stream.Put(&Arg1, 4u)) return 0x16u;
    if (!Stream.Put(&Arg2, 4u)) return 0x16u;
    if (!Stream.Put(&Arg3, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x10, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestEventPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, EventType)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Arg1)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Arg2)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Arg3)) return MarshalError;
    if (!Stream.Get(&Field_0x10, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestCancelPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5a280 / RS06bf2760: every failure returns EBP=0x16 (06b5a2d5).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EventType, 1u)) return 0x16u;
    if (!Stream.Put(&Arg1, 4u)) return 0x16u;
    if (!Stream.Put(&Arg2, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x0c, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestCancelPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, EventType)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Arg1)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Arg2)) return MarshalError;
    if (!Stream.Get(&Field_0x0c, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcBadlandsObjectCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53690 / RS06c01bc0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 2u)) return 0x16u;
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcBadlandsObjectCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x08)) return MarshalError;
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}


unsigned int RpcWireEncodeDwords(FPacketBase& Packet, const unsigned int* const* Fields,
    unsigned int FieldCount, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength) {
    // DWORD-record encoders (e.g. CLIENT2b RC06b3ada0, CLIENT37 RC06b559c0,
    // CLIENT2f RC06b59cb0): each failed reservation returns 0x16.
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    for (unsigned int Index = 0; Index < FieldCount; ++Index) {
        if (!Stream.Put(Fields[Index], 4u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int RpcWireDecodeDwords(FPacketBase& Packet, unsigned int* const* Fields,
    unsigned int FieldCount, const unsigned char* Buffer, unsigned int Size,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    for (unsigned int Index = 0; Index < FieldCount; ++Index) {
        if (Index + 1u < FieldCount) {
            // Native commits the moving pointer before storing every non-final
            // payload field; the final field is stored before pointer commit.
            unsigned int Value;
            if (!Stream.Get(&Value, 4u)) return Packet.MarshalError;
            *Fields[Index] = Value;
        } else {
            if (!Stream.Get(Fields[Index], 4u)) return Packet.MarshalError;
        }
    }
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestInvincibilityPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    if (!Stream.Get(&Reason, 1u)) return MarshalError;
    if (!Stream.Get(&bInvincible, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestInvincibilityPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5b150 / RS06c081e0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ShipId, 4u)) return 0x16u;
    if (!Stream.Put(&Reason, 1u)) return 0x16u;
    if (!Stream.Put(&bInvincible, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestUseItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ItemId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestUseItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5ae70 / RS06c07f00.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&ItemId, 2u)) return 0x16u;
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestRemoveItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&EquipId, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestRemoveItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b52590 / RS06c01400: failure returns literal 0x16 without rewriting the
    // sticky Error; success stores Error=0 before OutLength (RC06b52590 tail).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EquipId, 4u)) return 0x16u;
    if (!Stream.Put(&Count, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

FRpcRequestInvincibilityPacket::FRpcRequestInvincibilityPacket()
    : FPacketBase(0x30044u), ShipId(0), Reason(0), bInvincible(0) {}

FRpcRequestUseItemPacket::FRpcRequestUseItemPacket()
    : FPacketBase(0x30040u), ObjectId(0), ItemId(0), Count(0) {}

FRpcRequestRemoveItemPacket::FRpcRequestRemoveItemPacket()
    : FPacketBase(0x30019u), EquipId(0), Count(-1) {}

FRpcItemChangePacketBase::FRpcItemChangePacketBase(unsigned int PacketTypeId)
    : FPacketBase(PacketTypeId), Count(0), Health(0.0f), bMounted(0),
      HardpointSize(0), Hardpoint(nullptr) {}

FRpcItemChangePacketBase::~FRpcItemChangePacketBase() {
    if (Hardpoint != nullptr) ::operator delete(Hardpoint);
}

void FRpcItemChangePacketBase::CopyHardpoint(const void* Data, unsigned int Size) {
    if (Size != 0 && Data != nullptr) {
        // Replacing an existing pointer without freeing it is native behavior.
        Hardpoint = static_cast<char*>(::operator new(Size, std::nothrow));
        std::memcpy(Hardpoint, Data, Size);
        HardpointSize = Size;
    }
}

unsigned int FRpcItemChangePacketBase::EncodeFields(const unsigned int& Selector,
    unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // RC06b52820 / RS06be2d50 (ADDITEM) and RC06b52cc0 / RS06be31e0 (MODIFYITEM):
    // every failure returns EDI=0x16; the hardpoint body is a cached REP copy
    // (RC06b5297f..06b529a1).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Selector, 4u)) return 0x16u;
    if (!Stream.Put(&Count, 4u)) return 0x16u;
    if (!Stream.PutFloat(Health)) return 0x16u;
    if (!Stream.Put(&bMounted, 1u)) return 0x16u;
    if (!Stream.Put(&HardpointSize, 4u)) return 0x16u;
    if (HardpointSize != 0 && !Stream.PutBlock(Hardpoint, HardpointSize)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcItemChangePacketBase::DecodeFields(unsigned int& Selector,
    const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Selector, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&Health, 4u)) return MarshalError;
    if (!Stream.Get(&bMounted, 1u)) return MarshalError;
    if (!Stream.Get(&HardpointSize, 4u)) return MarshalError;
    if (HardpointSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        // RC's preflight does not commit the body cursor. RS allocates before
        // that bounds check, and has no separate allocation-failure return.
        if (MarshalCursor + HardpointSize > MarshalLimit) {
            MarshalError = 0x17u;
            return MarshalError;
        }
#endif
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        if (Hardpoint == nullptr) return 5u;
#endif
        if (!Stream.Get(Hardpoint, HardpointSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

FRpcClientInfoRequestPacketBase::FRpcClientInfoRequestPacketBase(unsigned int PacketTypeId)
    : FPacketBase(PacketTypeId) {}

unsigned int FRpcClientInfoRequestPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54f70 / RS06c03190 (BASEINFO) and RC06b551d0 / RS06c033f0 (LOCATIONINFO).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Id, 4u)) return 0x16u;
    if (!Stream.Put(&bFull, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcClientInfoRequestPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Id, 4u)) return MarshalError;
    if (!Stream.Get(&bFull, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

void FRpcRequestLaunchPacket::Dump(std::ostream& Out) const {
    Out << "   Object ID: " << Value << "\n";
}

FRpcCreateNewCharacterPacket::FRpcCreateNewCharacterPacket()
    : FPacketBase(0x30039u) {}

FRpcCharacterIdPacketBase::FRpcCharacterIdPacketBase(unsigned int Id)
    : FPacketBase(Id) {
    // Native inlines only Buffer[0]=0. Canonical CHARACTER_ID also initializes
    // the unused tail; the packet's strlen-based wire body stops at its NUL.
    CharacterId.Buffer[0] = '\0';
}

unsigned int FRpcCharacterIdPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b56560 / RS06c03f80 (SELECTCHARACTER) and RC06b562a0 / RS06c03cd0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned short Length = static_cast<unsigned short>(
        std::strlen(CharacterId.Buffer));
    if (!Stream.Put(&Length, 2u)) return 0x16u;
    // RC06b56616..06b5663c: cached REP copy, Ptr=cached+length.
    if (!Stream.PutBlock(CharacterId.Buffer, Length)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCharacterIdPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned short Length;
    if (!Stream.Get(&Length, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    // Only the RC body has this explicit error; the prefix pointer is committed
    // already, MarshalError remains zero and the body length is not reserved.
    if (Length >= 0x200u) return 0x4au;
#endif
    if (!Stream.Get(CharacterId.Buffer, Length)) return MarshalError;
    CharacterId.Buffer[Length] = '\0';
    return Stream.Finish(OutLength, false);
}

// These string helpers return an error without writing MarshalError. Their
// strict capacity comparison and delayed pointer commit differ from scalar IO.
static unsigned int RpcEncodeWideString(FPacketBase& Packet, const wchar_t* Value) {
    unsigned char* Destination = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    Packet.MarshalCursor += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return 0x16u;
    const unsigned short Length = static_cast<unsigned short>(std::wcslen(Value));
    std::memcpy(Destination, &Length, 2u);
    const unsigned int ByteCount = static_cast<unsigned int>(Length) * 2u;
    Packet.MarshalCursor += ByteCount;
    Destination += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return 0x16u;
    std::memcpy(Destination, Value, ByteCount);
    Packet.MarshalPtr = reinterpret_cast<unsigned int>(Destination + ByteCount);
    return 0;
}

static unsigned int RpcDecodeWideString(FPacketBase& Packet, wchar_t** Value) {
    const unsigned char* Source = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    Packet.MarshalCursor += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return 0x17u;
    unsigned short Length;
    std::memcpy(&Length, Source, 2u);
    Source += 2u;
    const unsigned int ByteCount = static_cast<unsigned int>(Length) * 2u;
    Packet.MarshalCursor += ByteCount;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return 0x17u;
    *Value = nullptr;
    if (Length != 0) {
        *Value = static_cast<wchar_t*>(::operator new(ByteCount + 2u, std::nothrow));
        // Native has no OOM branch. A zero-length value instead leaves both
        // the packet name and the moving input pointer completely untouched.
        std::memcpy(*Value, Source, ByteCount);
        (*Value)[Length] = L'\0';
        Packet.MarshalPtr = reinterpret_cast<unsigned int>(Source + ByteCount);
    }
    return 0;
}

unsigned int FRpcCreateNewCharacterPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b55eb0 / RS06c038b0: every failure returns 0x16 (literal or EBP).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (RpcEncodeWideString(*this, Name) != 0) return 0x16u;
    if (!Stream.Put(&Field_0x80, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x84, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x88, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x8c, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCreateNewCharacterPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    wchar_t* Temporary;
    if (RpcDecodeWideString(*this, &Temporary) != 0) return 0x17u;
    if (Temporary != nullptr) {
#if defined(REMOTECLIENT_EXPORTS)
        std::wcsncpy(Name, Temporary, 24u);
        Name[23] = L'\0';
#else
        // RS uses wcscpy, not the bounded RC operation or a synthetic 64 cap.
        std::wcscpy(Name, Temporary);
#endif
        ::operator delete(Temporary);
    }
    if (!Stream.Get(&Field_0x80, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x84, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x88, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x8c, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

void FRpcCharacterIdPacketBase::Dump(std::ostream& Out) const {
    Out << "   Character: " << CharacterId.Buffer << "\n";
}

void FRpcCreateNewCharacterPacket::Dump(std::ostream& Out) const {
    // Both native IAT entries select ostream::operator<<(const void*), not a
    // wchar_t text conversion. Keep the address insertion into the char stream.
    Out << "   Name: " << static_cast<const void*>(Name)
        << "   Fac: " << Field_0x80 << "   Base: " << Field_0x84
        << "   Pkg: " << Field_0x88 << "   Pilot: " << Field_0x8c << "\n";
}

FRpcUpdateObjectPacket::FRpcUpdateObjectPacket() : FPacketBase(0x10001u) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

FRpcPlayerTradePacket::FRpcPlayerTradePacket()
    : FPacketBase(0x10012u), Action(0), Reason(0), TargetPlayer(0), Money(0),
      bAccepted(0), EquipArchId(0xffffffffu), EquipCount(0), EquipHealth(0.0f) {}

unsigned int FRpcPlayerTradePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b68c90 / RS06bfff80: every failure returns 0x16 (literal or ECX).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned char ActionByte = static_cast<unsigned char>(Action);
    if (!Stream.Put(&ActionByte, 1u)) return 0x16u;
    const bool bIsMPServer = IsMPServer();
    bool bCountTarget = bIsMPServer;
    bool bNeedTarget = true;
    // The selector is the full DWORD, even though only its low byte was emitted.
    switch (Action) {
    case 0x01u:
    case 0x20u:
    case 0x80u:
        bCountTarget = true;
        break;
    case 0x02u: {
        const unsigned char ReasonByte = static_cast<unsigned char>(Reason);
        if (!Stream.Put(&ReasonByte, 1u)) return 0x16u;
        bNeedTarget = false;
        break;
    }
    case 0x04u:
        if (!Stream.Put(&Money, 4u)) return 0x16u;
        break;
    case 0x08u:
        if (!Stream.Put(&EquipArchId, 4u)) return 0x16u;
        if (!Stream.Put(&EquipCount, 4u)) return 0x16u;
        if (!Stream.PutFloat(EquipHealth)) return 0x16u;
        if (!Stream.Put(&EquipId, 2u)) return 0x16u;
        break;
    case 0x10u:
        if (!Stream.Put(&EquipArchId, 4u)) return 0x16u;
        if (!Stream.Put(&EquipCount, 4u)) return 0x16u;
        if (!Stream.Put(&EquipId, 2u)) return 0x16u;
        break;
    case 0x40u:
        if (!Stream.Put(&bAccepted, 1u)) return 0x16u;
        break;
    default:
        bNeedTarget = false;
        break;
    }
    if (bNeedTarget) {
        // RC06b68e06/06b68d66/06b68ee9: the DWORD is counted only on this
        // direction, but the sticky Error is tested on every path (06b68e1d,
        // 06b68d7d, 06b68efc) before the target DWORD is written to the cached
        // destination and Ptr=cached+4 is committed (06b68f7a..06b68f85).
        if (bCountTarget) Stream.Reserve(4u, 0x16u);
        if (MarshalError != 0) return 0x16u;
        Stream.PutReserved(&TargetPlayer, 4u);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcPlayerTradePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned char ActionByte;
    if (!Stream.Get(&ActionByte, 1u)) return MarshalError;
    Action = ActionByte;
    const bool bCountDirectedTarget = !IsMPServer();
    bool bCountTarget = bCountDirectedTarget;
    bool bNeedTarget = true;
    switch (Action) {
    case 0x01u:
    case 0x20u:
    case 0x80u:
        bCountTarget = true;
        break;
    case 0x02u: {
        signed char ReasonByte;
        if (!Stream.Get(&ReasonByte, 1u)) return MarshalError;
        Reason = ReasonByte;
        bNeedTarget = false;
        break;
    }
    case 0x04u:
        if (!Stream.Get(&Money, 4u)) return MarshalError;
        break;
    case 0x08u:
        if (!Stream.Get(&EquipArchId, 4u)) return MarshalError;
        if (!Stream.Get(&EquipCount, 4u)) return MarshalError;
        if (!Stream.Get(&EquipHealth, 4u)) return MarshalError; // Native integer bit-copy.
        if (!Stream.Get(&EquipId, 2u)) return MarshalError;
        break;
    case 0x10u:
        if (!Stream.Get(&EquipArchId, 4u)) return MarshalError;
        if (!Stream.Get(&EquipCount, 4u)) return MarshalError;
        if (!Stream.Get(&EquipId, 2u)) return MarshalError;
        break;
    case 0x40u: {
        unsigned char Accepted;
        if (!Stream.Get(&Accepted, 1u)) return MarshalError;
        bAccepted = static_cast<unsigned char>(Accepted != 0);
        if (bCountDirectedTarget && !Stream.Reserve(4u, 0x16u)) return MarshalError;
        // Native decoder bug: writes existing packet TargetPlayer INTO INPUT,
        // rather than reading it (RS06c004ab..4b1 / RC06b691bb..1c1).
        // The packet target remains unchanged; the input pointer still advances.
        Stream.PutUncounted(&TargetPlayer, 4u);
        bNeedTarget = false;
        break;
    }
    default:
        bNeedTarget = false;
        break;
    }
    if (bNeedTarget) {
        if (bCountTarget && !Stream.Reserve(4u, 0x17u)) return MarshalError;
        Stream.GetUncounted(&TargetPlayer, 4u);
    }
    return Stream.Finish(OutLength, false);
}

FRpcChatMsgPacket::FRpcChatMsgPacket()
    : FPacketBase(0x10005u), TargetId(0), Field_0x04(0), DataSize(0), Data(nullptr) {}

FRpcChatMsgPacket::~FRpcChatMsgPacket() {
    if (Data != nullptr) ::operator delete(Data);
}

unsigned int FRpcChatMsgPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b678c0 / RS06bfebd0: size, reserve size, cached REP body copy
    // (RC06b6796b..06b67990), two DWORDs; every failure returns 0x16.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return 0x16u;
    if (!Stream.PutBlock(Data, DataSize)) return 0x16u;
    if (!Stream.Put(&TargetId, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcChatMsgPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = 0x17u;
        return MarshalError;
    }
#endif
    // Both native decoders replace the pointer unconditionally. Re-decoding an
    // existing packet does not release its previous allocation in retail.
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (Data == nullptr) return 5u; // MarshalError and the decoded size remain intact.
#endif
    if (!Stream.Get(Data, DataSize)) return MarshalError;
    if (!Stream.Get(&TargetId, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

FRpcPopUpDialogPacket::FRpcPopUpDialogPacket()
    : FPacketBase(0x1001bu), SizeA(0), DataA(nullptr), SizeB(0), DataB(nullptr), Field_0x10(0) {}

FRpcPopUpDialogPacket::~FRpcPopUpDialogPacket() {
    if (DataB != nullptr) ::operator delete(DataB);
    if (DataA != nullptr) ::operator delete(DataA);
}

unsigned int FRpcPopUpDialogPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b66a10 / RS06bfdde0: both bodies are cached REP copies
    // (RC06b66abb..06b66ae0, 06b66b44..); every failure returns 0x16.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SizeA, 4u)) return 0x16u;
    if (!Stream.PutBlock(DataA, SizeA)) return 0x16u;
    if (!Stream.Put(&SizeB, 4u)) return 0x16u;
    // Native announces SizeB but reserves and copies SizeA from the second block.
    if (!Stream.PutBlock(DataB, SizeA)) return 0x16u;
    if (!Stream.Put(&Field_0x10, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

FRpcCommonBufferPacketBase::FRpcCommonBufferPacketBase(unsigned int Id,
    bool bInNullTerminate)
    : FPacketBase(Id), Data(nullptr), bOwnsData(false), bNullTerminate(bInNullTerminate) {}

FRpcCommonBufferPacketBase::~FRpcCommonBufferPacketBase() {
    if (bOwnsData && Data != nullptr) {
        ::operator delete(Data);
        Data = nullptr;
    }
}

void FRpcCommonBufferPacketBase::BorrowData(unsigned char* InData, unsigned int Size) {
    Data = InData;
    DataSize = Size;
}

void FRpcCommonBufferPacketBase::AdoptData(unsigned char* InData, unsigned int Size) {
    Data = InData;
    DataSize = Size;
    bOwnsData = true;
}

void FRpcCommonBufferPacketBase::CopyData(const void* InData, unsigned int Size) {
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    std::memcpy(Data, InData, Size);
    DataSize = Size;
    bOwnsData = true;
}

void FRpcCommonBufferPacketBase::CopyDataIfNull(const void* InData, unsigned int Size) {
    if (Data == nullptr) {
        Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
        bOwnsData = true;
    }
    std::memcpy(Data, InData, Size);
    DataSize = Size;
}

unsigned int FRpcCommonBufferPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Eight COMMON bodies, e.g. COMMON11 RC06b65e50 / RS06bfd2d0: size prefix
    // (RC06b65ec3..06b65ed0, size reread after the prefix store), unconditional
    // reserve of that size, cached REP copy and commit (06b65efb..06b65f25);
    // every failure returns literal 0x16 without rewriting the sticky Error.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return 0x16u;
    if (!Stream.PutBlock(Data, DataSize)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCommonBufferPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    // RC 06b66006..601a preflights without committing the cursor. The RS
    // counterpart 06bfd480..4bd instead allocates before its only body check.
    // This is native module behavior; the shared packet class layout is identical.
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = 0x17u;
        return MarshalError;
    }
#endif
    if (Data == nullptr) {
        // Only SET_WEAPON_GROUP reserves a terminator. Allocation precedes the
        // payload bounds check, and an existing pointer is never resized.
        const unsigned int AllocationSize = DataSize + (bNullTerminate ? 1u : 0u);
        Data = static_cast<unsigned char*>(::operator new(AllocationSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        // RC returns allocation error 5 with MarshalError still zero and without
        // changing ownership; RS has no allocation-failure branch here.
        if (Data == nullptr) return 5u;
#endif
        bOwnsData = true;
    }
    if (!Stream.Get(Data, DataSize)) return MarshalError;
    if (bNullTerminate) Data[DataSize] = 0;
    return Stream.Finish(OutLength, false);
}

unsigned int RpcWireDecodeOwnedBlock(FPacketBase& Packet, FRpcWireStream& Stream,
    unsigned int& BlockSize, unsigned char*& Data) {
    if (!Stream.Get(&BlockSize, 4u)) return Packet.MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (Packet.MarshalCursor + BlockSize > Packet.MarshalLimit) {
        Packet.MarshalError = 0x17u;
        return Packet.MarshalError;
    }
#endif
    Data = static_cast<unsigned char*>(::operator new(BlockSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (Data == nullptr) {
        BlockSize = 0;
        return 5u; // MarshalError remains zero; each caller handles this return.
    }
#endif
    if (!Stream.Reserve(BlockSize, 0x17u)) return Packet.MarshalError;
    // Native REP with count zero does not access either pointer, including
    // RS's null result from operator new(0). Allocation is still unconditional.
    if (BlockSize != 0)
        std::memcpy(Data, reinterpret_cast<const void*>(Packet.MarshalPtr), BlockSize);
    Stream.CommitReadPointer(BlockSize);
    return 0;
}

namespace {
const double GFireSmallLimit = 64.0;             // PE 06c0e338
const double GFireMediumLimit = 8192.0;          // PE 06c0e330
const float GFireSmallPreScale = 0.015625f;       // PE 06c0e32c: 0000803c
const float GFireMediumPreScale = 0.0001220703125f; // PE 06c0e328: 00000039
const double GFireSmallScale = 127.0;            // PE 06c0df40
const double GFireMediumScale = 32767.0;          // PE 06c0e320
const double GFireSmallInverse = 64.0 / 127.0;    // PE 06c0e348: 080402814020e03f
const double GFireMediumInverse = 8192.0 / 32767.0; // PE 06c0e340: 800040002000d03f
const double GUpdateInverseScale = 1.0 / 127.0;   // PE 06c0df48: 080402814020803f

// UPDATEOBJECT performs FLD/FMUL before committing the position or W pointer,
// and only then calls _ftol. Keep that ordering around an unmasked FP exception.
// The commit stores the destination cached before the preceding payload write
// plus its width (RC06b64d58/06b64d5b, 06b64e3d/06b64e3e); it never rereads Ptr.
unsigned char RpcUpdateQuantize(const float& Value, unsigned int* PointerToCommit,
    unsigned int CommittedPointer) {
    const float* Source = &Value;
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Source
        fld dword ptr [ecx]
        fmul qword ptr GFireSmallScale
        mov ecx, PointerToCommit
        test ecx, ecx
        jz UpdateConversion
        mov eax, CommittedPointer
        mov dword ptr [ecx], eax
    UpdateConversion:
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

void RpcUpdateExpand(signed char Encoded, float& Value) {
    const int Quantized = Encoded;
    float* Destination = &Value;
    PortableRpcQuantization::ExpandInteger(Quantized, GUpdateInverseScale, Destination);
}

// TEST AH,5 + JP takes the equal/greater/unordered side, while fall-through
// selects the strictly-less tier. Z has only the small comparison in retail.
unsigned char RpcFirePickTier(const float& Value, bool bHasMediumTier) {
    const float* Source = &Value;
    unsigned char Tier;
    if (bHasMediumTier) {
        __asm {
            mov ecx, Source
            xor edx, edx
            fld dword ptr [ecx]
            fabs
            fcom qword ptr GFireSmallLimit
            fnstsw ax
            test ah, 5
            jp FireTryMedium
            fstp st(0)
            mov dl, 1
            jmp FireTierDone
        FireTryMedium:
            fcomp qword ptr GFireMediumLimit
            fnstsw ax
            test ah, 5
            jp FireTierDone
            mov dl, 2
        FireTierDone:
            mov Tier, dl
        }
    } else {
        __asm {
            mov ecx, Source
            xor edx, edx
            fld dword ptr [ecx]
            fabs
            fcomp qword ptr GFireSmallLimit
            fnstsw ax
            test ah, 5
            jp FireSmallDone
            mov dl, 1
        FireSmallDone:
            mov Tier, dl
        }
    }
    return Tier;
}

// Native FMUL float followed by FMUL double, then _ftol's truncating signed
// integer conversion. Tier selection bounds the finite result to +/-32767.
unsigned int RpcFireQuantize(const float& Value, bool bMedium) {
    const float* Source = &Value;
    const float* PreScale = bMedium ? &GFireMediumPreScale : &GFireSmallPreScale;
    const double* Scale = bMedium ? &GFireMediumScale : &GFireSmallScale;
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Source
        mov eax, PreScale
        mov edx, Scale
        fld dword ptr [ecx]
        fmul dword ptr [eax]
        fmul qword ptr [edx]
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned int>(Quantized);
}

bool RpcFirePutComponent(FRpcWireStream& Stream, const float& Value,
    unsigned char Flags, unsigned char SmallBit, unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        const unsigned char Quantized = static_cast<unsigned char>(RpcFireQuantize(Value, false));
        return Stream.Put(&Quantized, 1u);
    }
    if ((Flags & MediumBit) != 0) {
        const unsigned short Quantized = static_cast<unsigned short>(RpcFireQuantize(Value, true));
        return Stream.Put(&Quantized, 2u);
    }
    return Stream.PutFloat(Value);
}

bool RpcFireGetComponent(FRpcWireStream& Stream, float& Value,
    unsigned char Flags, unsigned char SmallBit, unsigned char MediumBit,
    bool bSmallCommitBefore, bool bMediumCommitBefore) {
    int Quantized;
    unsigned int Width;
    bool bCommitBefore;
    const double* Scale;
    if ((Flags & SmallBit) != 0) {
        signed char Encoded;
        if (!Stream.GetBeforePointerCommit(&Encoded, 1u)) return false;
        Quantized = Encoded;
        Width = 1u;
        bCommitBefore = bSmallCommitBefore;
        Scale = &GFireSmallInverse;
    } else if ((Flags & MediumBit) != 0) {
        short Encoded;
        if (!Stream.GetBeforePointerCommit(&Encoded, 2u)) return false;
        Quantized = Encoded;
        Width = 2u;
        bCommitBefore = bMediumCommitBefore;
        Scale = &GFireMediumInverse;
    } else {
        // Uncompressed decoding copies integer bits, unlike float encoding.
        return Stream.Get(&Value, 4u);
    }
    if (bCommitBefore) Stream.CommitReadPointer(Width);
    float* Destination = &Value;
    PortableRpcQuantization::ExpandInteger(Quantized, *Scale, Destination);
    if (!bCommitBefore) Stream.CommitReadPointer(Width);
    return true;
}
} // namespace

unsigned int FRpcUpdateObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b64c70 / RS06bfc0f0: every failure returns EAX=0x16.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 1u)) return 0x16u;
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Reserve(12u, 0x16u)) return 0x16u;
    // RC06b64d2b..06b64d4a: snapshot X/Y/Z and the destination, MOV-copy via the
    // Vector assignment helper; the pointer commit is delayed past the next FMUL.
    unsigned int PositionBits[3];
    std::memcpy(&PositionBits[0], &Position.x, 4u);
    std::memcpy(&PositionBits[1], &Position.y, 4u);
    std::memcpy(&PositionBits[2], &Position.z, 4u);
    const unsigned int PositionDestination = MarshalPtr;
    std::memcpy(reinterpret_cast<void*>(PositionDestination), PositionBits, 12u);
    // All four conversions precede the first quaternion-byte capacity check.
    const unsigned char X = RpcUpdateQuantize(Orientation.x, &MarshalPtr,
        PositionDestination + 12u);
    const unsigned char Y = RpcUpdateQuantize(Orientation.y, nullptr, 0u);
    const unsigned char Z = RpcUpdateQuantize(Orientation.z, nullptr, 0u);
    const unsigned char W = RpcUpdateQuantize(Orientation.w, nullptr, 0u);
    if (!Stream.Put(&X, 1u)) return 0x16u;
    if (!Stream.Put(&Y, 1u)) return 0x16u;
    if (!Stream.Put(&Z, 1u)) return 0x16u;
    // RC06b64e2f..06b64e3e: W is stored at the cached destination, the extra
    // value's FLD/FMUL follows, then Ptr=cached+1, then _ftol.
    if (!Stream.Reserve(1u, 0x16u)) return 0x16u;
    const unsigned int WDestination = MarshalPtr;
    std::memcpy(reinterpret_cast<void*>(WDestination), &W, 1u);
    const unsigned char Extra = RpcUpdateQuantize(Field_0x28, &MarshalPtr, WDestination + 1u);
    if (!Stream.Put(&Extra, 1u)) return 0x16u;
    if (!Stream.PutFloat(UpdateTime)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcUpdateObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 1u)) return MarshalError;
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    signed char X;
    signed char Y;
    signed char Z;
    signed char W;
    // Native reads and commits all four bytes before modifying the quaternion.
    if (!Stream.Get(&X, 1u)) return MarshalError;
    if (!Stream.Get(&Y, 1u)) return MarshalError;
    if (!Stream.Get(&Z, 1u)) return MarshalError;
    if (!Stream.Get(&W, 1u)) return MarshalError;
    RpcUpdateExpand(X, Orientation.x);
    RpcUpdateExpand(Y, Orientation.y);
    RpcUpdateExpand(Z, Orientation.z);
    RpcUpdateExpand(W, Orientation.w);
    signed char Extra;
    if (!Stream.Get(&Extra, 1u)) return MarshalError;
    RpcUpdateExpand(Extra, Field_0x28);
    if (!Stream.Get(&UpdateTime, 4u)) return MarshalError; // Integer bit-copy, no x87.
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcPopUpDialogPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // Retail ignores both helper results, including RC's allocation error 5.
    // A sticky marshal error still advances each later checked field's cursor.
    RpcWireDecodeOwnedBlock(*this, Stream, SizeA, DataA);
    RpcWireDecodeOwnedBlock(*this, Stream, SizeB, DataB);
    if (!Stream.Get(&Field_0x10, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// CLIENT_LOGIN: native packet constructor leaves both string buffers and the
// creation byte for the sender/decoder. It does not add synthetic defaults.
FRpcClientLoginPacket::FRpcClientLoginPacket() : FPacketBase(0x30001u) {}

unsigned int FRpcClientLoginPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b56850 / RS06c04260: every failure returns 0x16; both string bodies are
    // cached REP copies (RC06b56996..06b569b6, 06b569ed..06b56a10).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&CreateFlag, 1u)) return 0x16u;
    const unsigned char AccountLength = static_cast<unsigned char>(std::wcslen(AccountName));
    const unsigned char SecondLength = static_cast<unsigned char>(std::wcslen(Field_0x220));
    if (!Stream.Put(&AccountLength, 1u)) return 0x16u;
    if (!Stream.Put(&SecondLength, 1u)) return 0x16u;
    if (AccountLength != 0) {
        // This mutation precedes the account-buffer capacity check in retail.
        _wcslwr(AccountName);
        if (!Stream.PutBlock(AccountName, static_cast<unsigned int>(AccountLength) * 2u)) return 0x16u;
    }
    if (SecondLength != 0) {
        if (!Stream.PutBlock(Field_0x220, static_cast<unsigned int>(SecondLength) * 2u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcClientLoginPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // Native clears exactly 0x402 bytes, including CreateFlag and one pad byte.
    std::memset(AccountName, 0, sizeof(AccountName));
    std::memset(Field_0x220, 0, sizeof(Field_0x220));
    CreateFlag = 0;
    Padding[0] = 0;
    if (!Stream.Get(&CreateFlag, 1u)) return MarshalError;
    unsigned char AccountLength;
    unsigned char SecondLength;
    if (!Stream.Get(&AccountLength, 1u)) return MarshalError;
    if (!Stream.Get(&SecondLength, 1u)) return MarshalError;
    if (AccountLength != 0) {
        if (!Stream.Get(AccountName, static_cast<unsigned int>(AccountLength) * 2u)) return MarshalError;
        AccountName[AccountLength] = L'\0';
    }
    if (SecondLength != 0) {
        if (!Stream.Get(Field_0x220, static_cast<unsigned int>(SecondLength) * 2u)) return MarshalError;
        Field_0x220[SecondLength] = L'\0';
    }
    return Stream.Finish(OutLength, false);
}

void FRpcClientLoginPacket::Dump(std::ostream& Out) const {
    Out << "   Account: " << static_cast<const void*>(AccountName) << "\n";
}

FRpcFireWeaponPacket::FRpcFireWeaponPacket() : FPacketBase(0x10002u), ObjectId(0) {
    TargetPos.x = FLT_MAX;
    TargetPos.y = FLT_MAX;
    TargetPos.z = FLT_MAX;
}

unsigned int FRpcFireWeaponPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b651f0 / RS06bfc670: every failure returns 0x16 (literal or EDX).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (SinglePlayer()) {
        // RC06b65288..06b652dc: reserve 12, snapshot X/Y/Z, MOV-copy, Ptr=cached+12.
        if (!RpcWirePutVector(Stream, TargetPos)) return 0x16u;
        const unsigned char Count = static_cast<unsigned char>(Hardpoints.size());
        if (!Stream.Put(&Count, 1u)) return 0x16u;
    } else {
        unsigned char Flags = RpcFirePickTier(TargetPos.x, true);
        Flags |= static_cast<unsigned char>(RpcFirePickTier(TargetPos.y, true) << 2);
        Flags |= static_cast<unsigned char>(RpcFirePickTier(TargetPos.z, false) << 4);
        if (Hardpoints.size() == 1u) Flags |= 0x40u;
        if (!Stream.Put(&Flags, 1u)) return 0x16u;
        if (!RpcFirePutComponent(Stream, TargetPos.x, Flags, 0x01u, 0x02u)) return 0x16u;
        if (!RpcFirePutComponent(Stream, TargetPos.y, Flags, 0x04u, 0x08u)) return 0x16u;
        if (!RpcFirePutComponent(Stream, TargetPos.z, Flags, 0x10u, 0x20u)) return 0x16u;
        const unsigned char Count = static_cast<unsigned char>(Hardpoints.size());
        // The full-size one-element flag and this low-byte test differ at 257.
        if (Count != 1u && !Stream.Put(&Count, 1u)) return 0x16u;
    }
    for (std::vector<unsigned short>::const_iterator It = Hardpoints.begin(); It != Hardpoints.end(); ++It) {
        if (!Stream.Put(&*It, 2u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcFireWeaponPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    unsigned char Count;
    if (SinglePlayer()) {
        if (!Stream.Get(&TargetPos, 12u)) return MarshalError;
        if (!Stream.Get(&Count, 1u)) return MarshalError;
    } else {
        unsigned char Flags;
        if (!Stream.Get(&Flags, 1u)) return MarshalError;
        // Native commits its stream pointer on different sides of FSTP for
        // these branches; preserve that ordering even for an FP exception.
        if (!RpcFireGetComponent(Stream, TargetPos.x, Flags, 0x01u, 0x02u, false, true)) return MarshalError;
        if (!RpcFireGetComponent(Stream, TargetPos.y, Flags, 0x04u, 0x08u, true, false)) return MarshalError;
        if (!RpcFireGetComponent(Stream, TargetPos.z, Flags, 0x10u, 0x20u, true, false)) return MarshalError;
        if ((Flags & 0x40u) != 0) Count = 1u;
        else if (!Stream.Get(&Count, 1u)) return MarshalError;
    }
    for (unsigned int Index = 0; Index < Count; ++Index) {
        unsigned short Hardpoint;
        if (!Stream.Get(&Hardpoint, 2u)) return MarshalError;
        Hardpoints.push_back(Hardpoint);
    }
    return Stream.Finish(OutLength, false);
}

FRpcTractorObjectsPacket::FRpcTractorObjectsPacket()
    : FPacketBase(0x1001du), ObjectId(0), TractorEquipId(0) {}

unsigned int FRpcTractorObjectsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b697b0 / RS06c00aa0: every failure returns EBP=0x16 (06b69807).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&TractorEquipId, 2u)) return 0x16u;
    const unsigned short Count = static_cast<unsigned short>(TargetIds.size());
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    // Retail truncates the count to a WORD but emits every element, independently.
    for (std::vector<unsigned int>::const_iterator It = TargetIds.begin(); It != TargetIds.end(); ++It) {
        if (!Stream.Put(&*It, 4u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcTractorObjectsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&TractorEquipId, 2u)) return MarshalError;
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    for (unsigned int Index = 0; Index < Count; ++Index) {
        unsigned int TargetId;
        if (!Stream.Get(&TargetId, 4u)) return MarshalError;
        TargetIds.push_back(TargetId); // Decode appends to existing contents.
    }
    return Stream.Finish(OutLength, false);
}

FRpcGoTradeLanePacket::FRpcGoTradeLanePacket() : FPacketBase(0x1000fu) {}

unsigned int FRpcGoTradeLanePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b68180 / RS06bff470: every failure returns EBP=0x16 (06b681d7).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&TradelaneRing1, 4u)) return 0x16u;
    if (!Stream.Put(&TradelaneRing2, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x0c, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcGoTradeLanePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&TradelaneRing1, 4u)) return MarshalError;
    if (!Stream.Get(&TradelaneRing2, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x0c, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

FRpcJettisonCargoPacket::FRpcJettisonCargoPacket()
    : FPacketBase(0x10014u), ObjectId(0), ItemId(0), Count(0) {}

unsigned int FRpcJettisonCargoPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b69530 / RS06c00820.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&ItemId, 2u)) return 0x16u;
    if (!Stream.Put(&Count, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcJettisonCargoPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ItemId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}
