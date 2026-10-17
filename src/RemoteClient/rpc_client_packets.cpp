// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "common_misc.h" // SinglePlayer
#include <map>
#include <string>
#include <vector>
#include <list>
#include <iostream>

namespace {
// Native raw cache: Shutdown does not clear it or add another owning reference.
FPacketSerializer* GRemotePacketSerializer = nullptr;
}

// =================================================================================================
// Base Classes (local stubs to avoid rpclocal dependency)
// =================================================================================================

FRpcRefObject::~FRpcRefObject() {}

unsigned int FRpcRefObject::Release() {
    if (UsageCount == 0) return 0;
    if (--UsageCount == 0) {
        delete this;
        return 0;
    }
    return UsageCount;
}

FPacketBase::FPacketBase(unsigned int InPacketId) {
    Field_0x08 = nullptr;
    PacketId = InPacketId;
    MarshalCursor = 0;
    MarshalError = 0;
}

FPacketBase::~FPacketBase() {}

unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // 06b4e7b0: inline entry, cached header commit, Error=0, OutLength, stats, 0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

void FPacketBase::Dump(std::ostream& Out) const {}

// =================================================================================================
// Packet Classes Implementation
// =================================================================================================

#if 0 // Unfinished COMMON packet classes remain disabled.

struct FPacketCommonFireWeapon : public FPacketCommon
{
    unsigned char Field_20;
    unsigned int Field_24;
    Vector Field_28;
    float Field_38;
    float Field_3C;
    float Field_40;
    float Field_34;
    float Field_48;

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_20);
        Stream.Read(Field_24);
        Stream.Read(Field_28);

        unsigned char b1, b2, b3, b4, b5;
        Stream.Read(b1);
        Stream.Read(b2);
        Stream.Read(b3);
        Stream.Read(b4);
        Stream.Read(b5);

        if (Stream.GetError()) return 0x17;

        Field_38 = (float)(signed char)b1 * 0.0078125f;
        Field_3C = (float)(signed char)b2 * 0.0078125f;
        Field_40 = (float)(signed char)b3 * 0.0078125f;
        Field_34 = (float)(signed char)b4 * 0.0078125f;
        Field_48 = (float)(signed char)b5 * 0.0078125f;

        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_20);
        Stream.Write(Field_24);
        Stream.Write(Field_28);

        unsigned char b1 = (unsigned char)(int)(Field_38 * 128.0f);
        unsigned char b2 = (unsigned char)(int)(Field_3C * 128.0f);
        unsigned char b3 = (unsigned char)(int)(Field_40 * 128.0f);
        unsigned char b4 = (unsigned char)(int)(Field_34 * 128.0f);
        unsigned char b5 = (unsigned char)(int)(Field_48 * 128.0f);

        Stream.Write(b1);
        Stream.Write(b2);
        Stream.Write(b3);
        Stream.Write(b4);
        Stream.Write(b5);

        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};

struct FPacketCommonUpdateObject : public FPacketCommon
{
    unsigned int Field_28;
    char* Field_2C;
    unsigned int Field_20;
    unsigned int Field_24;

    FPacketCommonUpdateObject()
    {
        Field_28 = 0;
        Field_2C = nullptr;
        Field_20 = 0;
        Field_24 = 0;
    }

    virtual ~FPacketCommonUpdateObject()
    {
        if (Field_2C)
        {
            delete[] Field_2C;
            Field_2C = nullptr;
        }
    }

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_28);

        if (Field_28 > 0 && !Stream.GetError())
        {
            Field_2C = new char[Field_28];
            Stream.Read(Field_2C, Field_28);
        }

        Stream.Read(Field_20);
        Stream.Read(Field_24);

        if (Stream.GetError()) return 0x17;
        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_28);
        if (Field_28 > 0 && Field_2C)
        {
            Stream.Write(Field_2C, Field_28);
        }
        Stream.Write(Field_20);
        Stream.Write(Field_24);
        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};

struct FPacketCommonChatMsg : public FPacketCommon
{
    unsigned int Field_20;
    unsigned short Field_26;
    unsigned char Field_24;

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_20);
        Stream.Read(Field_26);
        Stream.Read(Field_24);
        if (Stream.GetError()) return 0x17;
        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_20);
        Stream.Write(Field_26);
        Stream.Write(Field_24);
        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};

struct FPacketCommonActivateEquip : public FPacketCommon
{
    unsigned int Field_20;
    unsigned int Field_24;
    unsigned short Field_28;
    unsigned short Field_2A;

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_20);
        Stream.Read(Field_24);
        Stream.Read(Field_28);
        Stream.Read(Field_2A);
        if (Stream.GetError()) return 0x17;
        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_20);
        Stream.Write(Field_24);
        Stream.Write(Field_28);
        Stream.Write(Field_2A);
        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};

struct FPacketCommonSetTarget : public FPacketCommon
{
    unsigned int Field_20;
    unsigned char Field_24;
    unsigned char Field_25;

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_20);
        Stream.Read(Field_24);
        Stream.Read(Field_25);
        if (Stream.GetError()) return 0x17;
        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_20);
        Stream.Write(Field_24);
        Stream.Write(Field_25);
        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};

struct FPacketCommonActivateCruise : public FPacketCommon
{
    unsigned int Field_20;
    unsigned char Field_24;

    virtual int Decode(const void* InBuffer, unsigned int InLength, unsigned int* OutLength) override
    {
        CDataStream Stream(InBuffer, InLength);
        Stream.Read(Field_20);
        Stream.Read(Field_24);
        if (Stream.GetError()) return 0x17;
        if (OutLength) *OutLength = Stream.GetCursor();
        return 0;
    }

    virtual int Encode(void** OutBuffer, unsigned int* OutLength) override
    {
        CDataStream Stream;
        Stream.Write(Field_20);
        Stream.Write(Field_24);
        return Stream.GenerateBuffer(OutBuffer, OutLength);
    }
};
#endif

FPacketSerializer* RpcGetRemotePacketSerializer() {
    if (GRemotePacketSerializer == nullptr) {
        GRemotePacketSerializer = new (std::nothrow) FPacketSerializer;
    }
    return GRemotePacketSerializer;
}

// =================================================================================================
// FPacketSerializer Implementation
// =================================================================================================

FPacketSerializer::FPacketSerializer() {
    // Native's dictionary call is inside construction: a throw unwinds the
    // already-constructed Types map, refcount base, then new-expression storage.
    RpcServerBuildHardpointDictionary();
}

FPacketSerializer::~FPacketSerializer() {
    for (auto& Pair : Types) {
        delete Pair.second;
    }
    Types.clear();
}

int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory) {
    if (Types.find(PacketId) != Types.end()) {
        return kPacketRegisterDuplicate;
    }

    FPacketTypeInfo* Info = new FPacketTypeInfo();
    Info->Factory = Factory;
    Info->Name = Name ? Name : "";

    Types[PacketId] = Info;
    return 0;
}

int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket) {
    if (!OutPacket) return kPacketAllocateNullOut;

    const unsigned int PacketId = ((Channel & 0xffffu) << 16) | (Opcode & 0xffffu);
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.find(PacketId);
    if (It == Types.end()) {
        return kPacketAllocateUnknownType;
    }

    return It->second->Factory(OutPacket);
}
