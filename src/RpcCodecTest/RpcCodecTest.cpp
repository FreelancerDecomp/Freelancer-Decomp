// Freelancer Decomp: RpcCodecTest, the network packet codec test (port-only, not part of retail).
//
// Compiles the packet codecs of RemoteClient.dll (the server's network side: rpc_client_packets.cpp
// and src/RemoteCommon) with RemoteClient's own defines, registers every packet type the way
// RemoteClient does, and runs each type's Encode and Decode on a fixed set of inputs. The results
// (result codes, lengths and the encoded bytes) go to a text file. CI runs the test twice and
// requires identical files, then compares the file with the one the base commit produces: a change
// that keeps the wire format must not change it, and an x64 build must produce the same bytes as
// the Win32 build. See docs/testing.md.
//
// Usage (from bin\):  RpcCodecTest.exe [--out <file>]
// Exit code: 0 when the test ran, 1 when the packet types could not be registered, 2 on bad
// arguments. Codec results never fail the run by themselves: the file comparison does.

#include <windows.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "common_misc.h"   // SetMultiPlayer
#include "common_net.h"    // SetIsMPServer

// -------------------------------------------------------------------------------------------------
// Allocation. Packet constructors leave some fields unset on purpose (as retail does), and a
// failed Decode leaves others unset; Encode may then read them. To keep the output
// deterministic, every allocation in this program's own code (the codecs included) is zero
// filled. calloc/free share the CRT heap with the DLLs' operator new/delete (/MD), so memory
// may cross between them. Requests above 64 MiB fail, as a garbage length would otherwise
// commit gigabytes.
// -------------------------------------------------------------------------------------------------
namespace
{
    const size_t MaxAllocation = (size_t)64 << 20;

    void* ZeroedAllocation(size_t Size)
    {
        if (Size > MaxAllocation)
        {
            return nullptr;
        }
        return calloc(1, Size != 0 ? Size : 1);
    }
}

void* operator new(size_t Size)
{
    void* Memory = ZeroedAllocation(Size);
    if (Memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return Memory;
}

void* operator new[](size_t Size)
{
    return operator new(Size);
}

void* operator new(size_t Size, const std::nothrow_t&) noexcept
{
    return ZeroedAllocation(Size);
}

void* operator new[](size_t Size, const std::nothrow_t&) noexcept
{
    return ZeroedAllocation(Size);
}

void operator delete(void* Memory) noexcept { free(Memory); }
void operator delete[](void* Memory) noexcept { free(Memory); }
void operator delete(void* Memory, size_t) noexcept { free(Memory); }
void operator delete[](void* Memory, size_t) noexcept { free(Memory); }
void operator delete(void* Memory, const std::nothrow_t&) noexcept { free(Memory); }
void operator delete[](void* Memory, const std::nothrow_t&) noexcept { free(Memory); }

namespace
{
    // The transport's encode buffer (RemoteClient encodes into 0x2800 bytes first).
    const unsigned int EncodeCapacity = 0x2800;
    // Encoded bytes printed in full up to this length; longer output is printed in part.
    const unsigned int MaxPrintedBytes = 256;
    // Decode is also run on every prefix of the "small" input up to this length.
    const unsigned int MaxTruncatedLength = 48;

    const unsigned int NoException = 0;
    const unsigned int ResultException = 0xffffffffu;

    struct FInput
    {
        const char* Name;
        std::vector<unsigned char> Bytes;
    };

    struct FCodecResult
    {
        unsigned int Code = 0;
        unsigned int Length = 0;
        unsigned int Exception = NoException;
    };

    // ---------------------------------------------------------------------------------------------
    // Structured-exception guards. A codec may fault on input no real peer sends (a null pointer
    // left by a failed Decode, say); the fault code is recorded and the test carries on. These
    // functions hold no objects with destructors, as __try requires.
    // ---------------------------------------------------------------------------------------------
    int RecordException(unsigned int Code, unsigned int* Out)
    {
        *Out = Code;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    unsigned int GuardedAllocate(FPacketFactoryFn Factory, FPacketBase** Out, unsigned int* Exception)
    {
        __try
        {
            return (unsigned int)Factory(Out);
        }
        __except (RecordException(GetExceptionCode(), Exception))
        {
            *Out = nullptr;
            return ResultException;
        }
    }

    unsigned int GuardedDecode(FPacketBase* Packet, const unsigned char* Buffer, unsigned int Size,
        unsigned int* Length, unsigned int* Exception)
    {
        __try
        {
            return Packet->Decode(Buffer, Size, Length);
        }
        __except (RecordException(GetExceptionCode(), Exception))
        {
            return ResultException;
        }
    }

    unsigned int GuardedEncode(FPacketBase* Packet, unsigned char** Buffer, unsigned int Capacity,
        unsigned int* Length, unsigned int* Exception)
    {
        __try
        {
            return Packet->Encode(Buffer, Capacity, Length);
        }
        __except (RecordException(GetExceptionCode(), Exception))
        {
            return ResultException;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Inputs
    // ---------------------------------------------------------------------------------------------
    std::vector<unsigned char> FilledBytes(unsigned int Size, unsigned char Value)
    {
        return std::vector<unsigned char>(Size, Value);
    }

    // Little-endian DWORDs 1, 2, 3, 4, 5, 1, 2, ...: every count or length field reads a small
    // value, so variable-length bodies decode instead of failing on their first count.
    std::vector<unsigned char> SmallDwords(unsigned int Size)
    {
        std::vector<unsigned char> Bytes(Size, 0);
        for (unsigned int i = 0; i + 4 <= Size; i += 4)
        {
            Bytes[i] = (unsigned char)((i / 4) % 5 + 1);
        }
        return Bytes;
    }

    std::vector<unsigned char> AscendingBytes(unsigned int Size)
    {
        std::vector<unsigned char> Bytes(Size);
        for (unsigned int i = 0; i < Size; ++i)
        {
            Bytes[i] = (unsigned char)i;
        }
        return Bytes;
    }

    // A fixed linear congruential generator, so every build sees the same bytes.
    std::vector<unsigned char> RandomBytes(unsigned int Size, unsigned int Seed)
    {
        std::vector<unsigned char> Bytes(Size);
        unsigned int State = Seed;
        for (unsigned int i = 0; i < Size; ++i)
        {
            State = State * 1103515245u + 12345u;
            Bytes[i] = (unsigned char)(State >> 16);
        }
        return Bytes;
    }

    std::vector<FInput> MakeInputs()
    {
        std::vector<FInput> Inputs;
        Inputs.push_back({ "empty", std::vector<unsigned char>() });
        Inputs.push_back({ "zeros", FilledBytes(256, 0x00) });
        Inputs.push_back({ "small", SmallDwords(256) });
        Inputs.push_back({ "ascending", AscendingBytes(256) });
        Inputs.push_back({ "ones", FilledBytes(256, 0xff) });
        Inputs.push_back({ "random1", RandomBytes(256, 0x0badf00du) });
        Inputs.push_back({ "random2", RandomBytes(256, 0x12345678u) });
        Inputs.push_back({ "random3", RandomBytes(64, 0xdeadbeefu) });
        Inputs.push_back({ "random4", RandomBytes(1024, 0x00c0ffeeu) });
        return Inputs;
    }

    // ---------------------------------------------------------------------------------------------
    // Output
    // ---------------------------------------------------------------------------------------------
    unsigned int Fnv1a(const unsigned char* Bytes, unsigned int Size)
    {
        unsigned int Hash = 2166136261u;
        for (unsigned int i = 0; i < Size; ++i)
        {
            Hash ^= Bytes[i];
            Hash *= 16777619u;
        }
        return Hash;
    }

    std::string FormatResult(const FCodecResult& Result)
    {
        char Text[64];
        if (Result.Code == ResultException)
        {
            sprintf(Text, "exception:%08x", Result.Exception);
        }
        else
        {
            sprintf(Text, "%u", Result.Code);
        }
        return Text;
    }

    std::string FormatBytes(const unsigned char* Bytes, unsigned int Size)
    {
        std::string Text;
        char Hex[4];
        const unsigned int Printed = Size < MaxPrintedBytes ? Size : MaxPrintedBytes;
        for (unsigned int i = 0; i < Printed; ++i)
        {
            sprintf(Hex, "%02x", Bytes[i]);
            Text += Hex;
        }
        if (Printed < Size)
        {
            char Tail[64];
            sprintf(Tail, "...(%u bytes, fnv %08x)", Size, Fnv1a(Bytes, Size));
            Text += Tail;
        }
        return Text;
    }

    struct FCounts
    {
        unsigned int Cases = 0;
        unsigned int Decoded = 0;
        unsigned int Encoded = 0;
        unsigned int RoundTrips = 0;
        unsigned int Unstable = 0;
        unsigned int Exceptions = 0;
    };

    // Encodes Packet into a fresh zeroed buffer. Out receives the encoded bytes on success.
    FCodecResult Encode(FPacketBase* Packet, std::vector<unsigned char>& Out)
    {
        FCodecResult Result;
        std::vector<unsigned char> Buffer(EncodeCapacity + 16, 0);
        unsigned char* Pointer = Buffer.data();
        Result.Code = GuardedEncode(Packet, &Pointer, EncodeCapacity, &Result.Length, &Result.Exception);
        Out.clear();
        if (Result.Code == 0 && Result.Length <= EncodeCapacity)
        {
            Out.assign(Buffer.begin(), Buffer.begin() + Result.Length);
        }
        return Result;
    }

    // Packets are never destroyed: a failed Decode can leave owned pointers unset, and the
    // destructors would free them. The test leaks every packet instead.
    FPacketBase* NewPacket(const FPacketTypeInfo& Info, FCodecResult& Result)
    {
        FPacketBase* Packet = nullptr;
        Result.Code = GuardedAllocate(Info.Factory, &Packet, &Result.Exception);
        return Result.Code == 0 ? Packet : nullptr;
    }

    // One input: Decode, then Encode the decoded packet, then check the round trip (does the
    // payload equal the bytes Decode consumed?) and stability (does decoding and re-encoding
    // the output give the same bytes again?).
    void RunInput(FILE* Out, const FPacketTypeInfo& Info, const FInput& Input, FCounts& Counts)
    {
        ++Counts.Cases;
        FCodecResult Allocation;
        FPacketBase* Packet = NewPacket(Info, Allocation);
        if (Packet == nullptr)
        {
            fprintf(Out, "  %-10s alloc=%s\n", Input.Name, FormatResult(Allocation).c_str());
            return;
        }

        FCodecResult Decode;
        Decode.Code = GuardedDecode(Packet, Input.Bytes.data(), (unsigned int)Input.Bytes.size(),
            &Decode.Length, &Decode.Exception);
        if (Decode.Code == ResultException)
        {
            ++Counts.Exceptions;
        }
        if (Decode.Code != 0)
        {
            fprintf(Out, "  %-10s dec=%s\n", Input.Name, FormatResult(Decode).c_str());
            return;
        }
        ++Counts.Decoded;

        std::vector<unsigned char> Encoded;
        const FCodecResult EncodeResult = Encode(Packet, Encoded);
        if (EncodeResult.Code == ResultException)
        {
            ++Counts.Exceptions;
        }
        if (EncodeResult.Code != 0)
        {
            fprintf(Out, "  %-10s dec=0 used=%u enc=%s\n", Input.Name, Decode.Length,
                FormatResult(EncodeResult).c_str());
            return;
        }
        ++Counts.Encoded;

        // The encoded output starts with the two header bytes Decode never sees.
        const char* RoundTrip = "diff";
        if (Encoded.size() >= 2 && Encoded.size() - 2 == Decode.Length
            && (Decode.Length == 0 || memcmp(Encoded.data() + 2, Input.Bytes.data(), Decode.Length) == 0))
        {
            RoundTrip = "same";
            ++Counts.RoundTrips;
        }

        const char* Stable = "n/a";
        if (Encoded.size() >= 2)
        {
            FCodecResult Allocation2;
            FPacketBase* Second = NewPacket(Info, Allocation2);
            FCodecResult Decode2;
            Stable = "no";
            if (Second != nullptr)
            {
                Decode2.Code = GuardedDecode(Second, Encoded.data() + 2, (unsigned int)Encoded.size() - 2,
                    &Decode2.Length, &Decode2.Exception);
                if (Decode2.Code == 0)
                {
                    std::vector<unsigned char> Reencoded;
                    const FCodecResult Encode2 = Encode(Second, Reencoded);
                    if (Encode2.Code == 0 && Reencoded == Encoded)
                    {
                        Stable = "yes";
                    }
                }
            }
            if (strcmp(Stable, "yes") != 0)
            {
                ++Counts.Unstable;
            }
        }

        fprintf(Out, "  %-10s dec=0 used=%u enc=0 len=%u rt=%s stable=%s bytes=%s\n", Input.Name,
            Decode.Length, EncodeResult.Length, RoundTrip, Stable,
            FormatBytes(Encoded.data(), (unsigned int)Encoded.size()).c_str());
    }

    void RunType(FILE* Out, unsigned int PacketId, const FPacketTypeInfo& Info,
        const std::vector<FInput>& Inputs, FCounts& Counts)
    {
        fprintf(Out, "type %05x %s\n", PacketId, Info.Name.c_str());

        // A freshly constructed packet, encoded as is.
        {
            ++Counts.Cases;
            FCodecResult Allocation;
            FPacketBase* Packet = NewPacket(Info, Allocation);
            if (Packet == nullptr)
            {
                fprintf(Out, "  %-10s alloc=%s\n", "fresh", FormatResult(Allocation).c_str());
            }
            else
            {
                std::vector<unsigned char> Encoded;
                const FCodecResult Result = Encode(Packet, Encoded);
                if (Result.Code == ResultException)
                {
                    ++Counts.Exceptions;
                }
                if (Result.Code == 0)
                {
                    ++Counts.Encoded;
                    fprintf(Out, "  %-10s enc=0 len=%u bytes=%s\n", "fresh", Result.Length,
                        FormatBytes(Encoded.data(), (unsigned int)Encoded.size()).c_str());
                }
                else
                {
                    fprintf(Out, "  %-10s enc=%s\n", "fresh", FormatResult(Result).c_str());
                }
            }
        }

        for (const FInput& Input : Inputs)
        {
            RunInput(Out, Info, Input, Counts);
        }

        // Decode of every prefix of the "small" input: where each codec stops and which error
        // it reports for a short packet.
        const std::vector<unsigned char> Small = SmallDwords(MaxTruncatedLength);
        std::string Line;
        for (unsigned int Length = 0; Length <= MaxTruncatedLength; ++Length)
        {
            ++Counts.Cases;
            FCodecResult Allocation;
            FPacketBase* Packet = NewPacket(Info, Allocation);
            FCodecResult Decode;
            if (Packet == nullptr)
            {
                Decode = Allocation;
            }
            else
            {
                Decode.Code = GuardedDecode(Packet, Small.data(), Length, &Decode.Length, &Decode.Exception);
            }
            if (Decode.Code == ResultException)
            {
                ++Counts.Exceptions;
            }
            if (Length != 0)
            {
                Line += ',';
            }
            Line += FormatResult(Decode);
            if (Decode.Code == 0)
            {
                char Used[16];
                sprintf(Used, "/%u", Decode.Length);
                Line += Used;
            }
        }
        fprintf(Out, "  %-10s %s\n", "prefixes", Line.c_str());
    }

    // RemoteClient_transport.cpp's registration, in its order.
    int RegisterTypes(FPacketSerializer& Serializer)
    {
        int Result = RpcRegisterCommonScalarPackets(&Serializer);
        if (Result != 0)
        {
            return Result;
        }
        Result = RpcRegisterRemoteClientServerPackets(&Serializer);
        if (Result != 0)
        {
            return Result;
        }
        return RpcRegisterClientScalarPackets(&Serializer);
    }

    void RunMode(FILE* Out, const char* Name, bool bMultiPlayer, const FPacketSerializer& Serializer,
        const std::vector<FInput>& Inputs)
    {
        // FIREWEAPON's format follows SinglePlayer(), PLAYER_TRADE's trailer IsMPServer().
        SetMultiPlayer(bMultiPlayer);
        SetIsMPServer(bMultiPlayer);

        FCounts Counts;
        fprintf(Out, "mode %s\n", Name);
        for (const auto& Entry : Serializer.GetTypes())
        {
            RunType(Out, Entry.first, *Entry.second, Inputs, Counts);
        }
        fprintf(Out, "summary %s cases=%u decoded=%u encoded=%u round_trips=%u unstable=%u exceptions=%u\n",
            Name, Counts.Cases, Counts.Decoded, Counts.Encoded, Counts.RoundTrips, Counts.Unstable,
            Counts.Exceptions);
        printf("RpcCodecTest: %s: %u types, %u cases, %u decoded, %u encoded, %u round trips, "
            "%u unstable, %u exceptions\n", Name, (unsigned int)Serializer.GetTypes().size(), Counts.Cases,
            Counts.Decoded, Counts.Encoded, Counts.RoundTrips, Counts.Unstable, Counts.Exceptions);
    }
}

int main(int Argc, char** Argv)
{
    const char* OutPath = "rpc_codecs.txt";
    for (int i = 1; i < Argc; ++i)
    {
        if (strcmp(Argv[i], "--out") == 0 && i + 1 < Argc)
        {
            OutPath = Argv[++i];
        }
        else
        {
            fprintf(stderr, "RpcCodecTest: unknown argument '%s'\nUsage: RpcCodecTest.exe [--out <file>]\n", Argv[i]);
            return 2;
        }
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // The server thread's FPU setup (FLServer SetupServerFpu): 53-bit precision, all masked.
    _control87(0x9001f, 0xfffff);

    // The constructor builds the hardpoint dictionary, as in RemoteClient.
    FPacketSerializer* Serializer = new FPacketSerializer;
    const int Registered = RegisterTypes(*Serializer);
    if (Registered != 0 || Serializer->GetTypes().empty())
    {
        fprintf(stderr, "RpcCodecTest: registering the packet types failed (%d)\n", Registered);
        return 1;
    }

    FILE* Out = fopen(OutPath, "wb");
    if (Out == nullptr)
    {
        fprintf(stderr, "RpcCodecTest: cannot write %s\n", OutPath);
        return 1;
    }
    const std::vector<FInput> Inputs = MakeInputs();
    fprintf(Out, "rpccodectest 1 module=RemoteClient types=%u\n", (unsigned int)Serializer->GetTypes().size());
    RunMode(Out, "multiplayer", true, *Serializer, Inputs);
    RunMode(Out, "singleplayer", false, *Serializer, Inputs);
    fclose(Out);
    printf("RpcCodecTest: wrote %s\n", OutPath);
    return 0;
}
