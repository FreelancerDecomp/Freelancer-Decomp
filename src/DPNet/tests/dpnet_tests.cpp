// Freelancer Decomp: DPNet tests -- PORT(dpnet), not part of retail.
//
// 1. The byte-level examples of [MC-DPL8R] section 4 and [MC-DPL8CS] section 4: our encoders
//    must produce, and our decoders accept, exactly what Windows put on the wire.
// 2. FServer and FClient over a simulated network with latency, jitter (reordering), loss and
//    duplication: enumeration, joining, refusals, ordered delivery of large and small
//    messages both ways, unreliable sends, KeepAlives, and every way a session ends.
//
// Exit code 0 when every check passes.

#include "../dpnet_session.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

using namespace DPNet;

static int Failures = 0;
static int Checks = 0;

#define CHECK(Cond)                                                                    \
    do                                                                                 \
    {                                                                                  \
        ++Checks;                                                                      \
        if (!(Cond))                                                                   \
        {                                                                              \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #Cond);            \
            ++Failures;                                                                \
        }                                                                              \
    } while (0)

static FBytes Hex(const char* Text)
{
    FBytes Out;
    int Nibble = -1;
    for (; *Text != 0; ++Text)
    {
        int V = -1;
        if (*Text >= '0' && *Text <= '9')
            V = *Text - '0';
        else if (*Text >= 'A' && *Text <= 'F')
            V = *Text - 'A' + 10;
        else if (*Text >= 'a' && *Text <= 'f')
            V = *Text - 'a' + 10;
        if (V < 0)
            continue;
        if (Nibble < 0)
        {
            Nibble = V;
        }
        else
        {
            Out.push_back((uint8_t)(Nibble << 4 | V));
            Nibble = -1;
        }
    }
    return Out;
}

static std::string Ascii(const std::u16string& Text)
{
    std::string Out;
    for (char16_t C : Text)
        Out.push_back((char)C);
    return Out;
}

// ---------------------------------------------------------------------------------------------
// 1. Specification examples

static void TestSpecReliable()
{
    std::printf("[MC-DPL8R] 4.1, 4.2 examples\n");
    // 4.1 frame 1: CONNECT, poll, msg 0, version 1.6, session 0x79C9AEC6.
    FConnectFrame Connect;
    Connect.Command = Command::CFrame | Command::Poll;
    Connect.Op = ExtOp::Connect;
    Connect.SessId = 0x79C9AEC6;
    Connect.Timestamp = 0x2367369D;
    FBytes Out;
    Connect.Write(Out);
    CHECK(Out == Hex("88 01 00 00 06 00 01 00 C6 AE C9 79 9D 36 67 23"));

    // Frame 2: the listener's CONNECTED.
    FConnectFrame Connected = Connect;
    Connected.Op = ExtOp::Connected;
    Connected.Timestamp = 0x0004DFE1;
    Out.clear();
    Connected.Write(Out);
    CHECK(Out == Hex("88 02 00 00 06 00 01 00 C6 AE C9 79 E1 DF 04 00"));

    // Frame 3: the connector's CONNECTED, no poll, msg 1.
    FConnectFrame Confirm = Connect;
    Confirm.Command = Command::CFrame;
    Confirm.Op = ExtOp::Connected;
    Confirm.MsgId = 1;
    Out.clear();
    Confirm.Write(Out);
    CHECK(Out == Hex("80 02 01 00 06 00 01 00 C6 AE C9 79 9D 36 67 23"));

    FCommandFrame Decoded;
    CHECK(DecodeCommandFrame(Out.data(), Out.size(), Decoded));
    CHECK(Decoded.Op == ExtOp::Connected && Decoded.Connect.MsgId == 1 && Decoded.Connect.SessId == 0x79C9AEC6);

    // Frames 4/5: the opening KeepAlive, sequence 0.
    FDataHeader KeepAlive;
    KeepAlive.Command = Command::Data | Command::Reliable | Command::Sequential | Command::Poll | Command::NewMsg | Command::EndMsg;
    KeepAlive.Control = Control::KeepAlive;
    Out.clear();
    KeepAlive.Write(Out);
    FWriter(Out).U32(0x79C9AEC6);
    CHECK(Out == Hex("3F 02 00 00 C6 AE C9 79"));

    // 4.2 frame 1: unreliable, sequential, poll, sequence 5, next receive 3. (The dump has one
    // payload byte more than its caption says.)
    const FBytes Data = Hex("3D 00 05 03 01 41 42 43 44 45");
    FDataHeader H;
    const size_t Offset = FDataHeader::Read(Data.data(), Data.size(), H);
    CHECK(Offset == 4);
    CHECK(H.Seq == 5 && H.NRcv == 3 && (H.Command & Command::Reliable) == 0 && (H.Command & Command::Poll) != 0);

    // Frame 2: the SACK. The dump says next receive 6, which is right after sequence 5.
    FSackFrame Sack;
    Sack.NSeq = 3;
    Sack.NRcv = 6;
    Sack.Timestamp = 0x00115D07;
    Out.clear();
    Sack.Write(Out);
    CHECK(Out == Hex("80 06 01 00 03 06 00 00 07 5D 11 00"));

    // Masks are present only when their half is non-zero.
    FDataHeader Masked;
    Masked.SackMask = 0x0000000500000000ull;
    Masked.SendMask = 0x3;
    Out.clear();
    Masked.Write(Out);
    CHECK(Out.size() == 12);
    FDataHeader Back;
    CHECK(FDataHeader::Read(Out.data(), Out.size(), Back) == 12);
    CHECK(Back.SackMask == Masked.SackMask && Back.SendMask == Masked.SendMask);
    CHECK((Back.Control & (Control::Sack1 | Control::Sack2 | Control::Send1 | Control::Send2)) == (Control::Sack2 | Control::Send1));
}

static void TestCoalesce()
{
    std::printf("[MC-DPL8R] 2.2.3 coalesced payloads\n");
    const FBytes A = Hex("01 02 03");
    const FBytes B = Hex("04 05 06 07 08");
    FBytes C(0x5A3, 0xCC);   // needs the size bits in bCommand
    std::vector<FSubPayload> Parts = {
        {Command::Reliable | Command::Sequential, A.data(), A.size()},
        {Command::Reliable | Command::User1, B.data(), B.size()},
        {Command::Reliable, C.data(), C.size()},
    };
    FBytes Out;
    EncodeCoalesced(Parts, Out);
    CHECK(Out.size() == CoalescedSize({A.size(), B.size(), C.size()}));
    // Three headers plus padding to a dword, 3 bytes + 1 pad, 5 bytes + 3 pad, then C.
    CHECK(Out.size() == 8 + 4 + 8 + C.size());
    CHECK(Out[0] == 3 && Out[1] == (Command::Reliable | Command::Sequential));
    CHECK(Out[4] == 0xA3 && Out[5] == (Command::Reliable | (5 << 3) | Command::EndCoalesce));
    std::vector<FSubPayload> Decoded;
    CHECK(DecodeCoalesced(Out.data(), Out.size(), Decoded));
    CHECK(Decoded.size() == 3);
    if (Decoded.size() == 3)
    {
        CHECK(FBytes(Decoded[0].Data, Decoded[0].Data + Decoded[0].Size) == A);
        CHECK(FBytes(Decoded[1].Data, Decoded[1].Data + Decoded[1].Size) == B);
        CHECK(FBytes(Decoded[2].Data, Decoded[2].Data + Decoded[2].Size) == C);
        CHECK(Decoded[1].Flags == (Command::Reliable | Command::User1));
    }
    // Truncated: refused.
    CHECK(!DecodeCoalesced(Out.data(), Out.size() - 1, Decoded));
}

// [MC-DPL8CS] section 4: Ethernet frames; the UDP payload starts at byte 0x2A.
static const char* const kSpecConnectInfoFrame =
    "00 0A E4 03 27 73 00 0B DB 5C 3F 45 08 00 45 00"
    "00 98 3A 4C 00 00 80 11 9F B1 41 34 EF 3D 41 34"
    "EE B1 08 FE 08 FE 00 84 C2 BF 7F 00 01 00 C1 00"
    "00 00 04 00 00 00 08 00 00 00 60 00 00 00 14 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 00 23 81 BE 94 AB A1 FB 48 A2 E7 23 85 9E 65"
    "89 36 DA 80 EF 61 1B 69 47 42 9A DD 1C 7B ED 2B"
    "C1 3E 58 00 00 00 08 00 00 00 07 02 08 FE 41 34"
    "EF 3D 54 00 65 00 73 00 74 00 20 00 55 00 73 00"
    "65 00 72 00 00 00";

static const char* const kSpecSendConnectInfoFrame =
    "00 0B DB 5C 3F 45 00 0A E4 03 27 73 08 00 45 00"
    "01 94 06 95 00 00 80 11 D2 6C 41 34 EE B1 41 34"
    "EF 3D 08 FE 08 FE 01 80 0D 9F 7F 00 01 02 C2 00"
    "00 00 00 00 00 00 00 00 00 00 50 00 00 00 04 00"
    "00 00 00 00 00 00 02 00 00 00 56 01 00 00 1A 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 23 81 BE 94 AB A1"
    "FB 48 A2 E7 23 85 9E 65 89 36 DA 80 EF 61 1B 69"
    "47 42 9A DD 1C 7B ED 2B C1 3E 20 81 8E 94 03 00"
    "00 00 00 00 00 00 02 00 00 00 00 00 00 00 21 81"
    "9E 94 00 00 00 00 02 01 00 00 02 00 00 00 00 00"
    "00 00 07 00 00 00 42 01 00 00 14 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 20 81"
    "8E 94 00 00 00 00 00 01 00 00 03 00 00 00 00 00"
    "00 00 08 00 00 00 2E 01 00 00 14 00 00 00 00 00"
    "00 00 00 00 00 00 CC 00 00 00 62 00 00 00 78 2D"
    "64 69 72 65 63 74 70 6C 61 79 3A 2F 70 72 6F 76"
    "69 64 65 72 3D 25 37 42 45 42 46 45 37 42 41 30"
    "2D 36 32 38 44 2D 31 31 44 32 2D 41 45 30 46 2D"
    "30 30 36 30 39 37 42 30 31 34 31 31 25 37 44 3B"
    "68 6F 73 74 6E 61 6D 65 3D 36 35 2E 35 32 2E 32"
    "33 39 2E 36 31 3B 70 6F 72 74 3D 32 33 30 32 00"
    "54 00 65 00 73 00 74 00 20 00 55 00 73 00 65 00"
    "72 00 00 00 54 00 65 00 73 00 74 00 20 00 55 00"
    "73 00 65 00 72 00 00 00 54 00 65 00 73 00 74 00"
    "20 00 53 00 65 00 73 00 73 00 69 00 6F 00 6E 00"
    "00 00";

static void TestSpecCore()
{
    std::printf("[MC-DPL8CS] 4 examples\n");
    const FGuid Instance = {0x94BE8123, 0xA1AB, 0x48FB, {0xA2, 0xE7, 0x23, 0x85, 0x9E, 0x65, 0x89, 0x36}};
    const FGuid Application = {0x61EF80DA, 0x691B, 0x4247, {0x9A, 0xDD, 0x1C, 0x7B, 0xED, 0x2B, 0xC1, 0x3E}};

    // The player connect message (_EX form, DirectPlay 9).
    {
        const FBytes Frame = Hex(kSpecConnectInfoFrame);
        const FBytes Udp(Frame.begin() + 0x2A, Frame.end());
        FDataHeader H;
        const size_t Offset = FDataHeader::Read(Udp.data(), Udp.size(), H);
        CHECK(Offset == 4);
        CHECK((H.Command & Command::User1) != 0 && H.Seq == 1 && H.NRcv == 0);
        FPlayerConnectInfo Info;
        CHECK(FPlayerConnectInfo::Read(Udp.data() + Offset, Udp.size() - Offset, Info));
        CHECK(Info.Flags == ObjectType::Peer);
        CHECK(Info.DnetVersion == 8);
        CHECK(Ascii(Info.Name) == "Test User");
        CHECK(Info.Instance == Instance);
        CHECK(Info.Application == Application);
        CHECK(Info.AlternateAddresses == Hex("07 02 08 FE 41 34 EF 3D"));
        CHECK(!Info.bHasPassword && Info.Url.empty());

        // Our encoder lays it out the same way.
        FBytes Out;
        Info.Write(Out);
        CHECK(Out == FBytes(Udp.begin() + (ptrdiff_t)Offset, Udp.end()));
    }

    // The host's answer.
    {
        const FBytes Frame = Hex(kSpecSendConnectInfoFrame);
        const FBytes Udp(Frame.begin() + 0x2A, Frame.end());
        FDataHeader H;
        const size_t Offset = FDataHeader::Read(Udp.data(), Udp.size(), H);
        CHECK(Offset == 4 && H.Seq == 1 && H.NRcv == 2);
        FSendConnectInfo Info;
        CHECK(FSendConnectInfo::Read(Udp.data() + Offset, Udp.size() - Offset, Info));
        CHECK(Info.Flags == SessionFlags::MigrateHost);
        CHECK(Info.CurrentPlayers == 2 && Info.MaxPlayers == 0);
        CHECK(Ascii(Info.SessionName) == "Test Session");
        CHECK(Info.Instance == Instance && Info.Application == Application);
        CHECK(Info.Dpnid == 0x948E8120 && Info.NameTableVersion == 3);
        CHECK(Info.Entries.size() == 2 && Info.Memberships.empty());
        if (Info.Entries.size() == 2)
        {
            CHECK(Info.Entries[0].Dpnid == 0x949E8121);
            CHECK(Info.Entries[0].Flags == (EntryFlags::Peer | EntryFlags::Host));
            CHECK(Info.Entries[0].DnetVersion == 7 && Ascii(Info.Entries[0].Name) == "Test User");
            CHECK(Info.Entries[1].Dpnid == Info.Dpnid && Info.Entries[1].Flags == EntryFlags::Peer);
            CHECK(Info.Entries[1].Url ==
                  "x-directplay:/provider=%7BEBFE7BA0-628D-11D2-AE0F-006097B01411%7D;hostname=65.52.239.61;port=2302");
        }
        // 2.2.7: DPNID = (version << 20 | index) ^ instance.Data1.
        CHECK(MakeDpnid(3, 3, Instance) == 0x948E8120);
        CHECK(MakeDpnid(2, 2, Instance) == 0x949E8121);
        // Windows' tail-packed layout, byte for byte.
        FBytes Out;
        Info.Write(Out);
        CHECK(Out == FBytes(Udp.begin() + (ptrdiff_t)Offset, Udp.end()));
        CHECK(FAddress::Ipv4(65, 52, 239, 61, 2302).Url() == Info.Entries[1].Url);
    }

    // Enumeration round trip.
    {
        FEnumResponse R;
        R.EnumPayload = 0x1234;
        R.Flags = SessionFlags::ClientServer | SessionFlags::RequirePassword;
        R.MaxPlayers = 17;
        R.CurrentPlayers = 3;
        R.SessionName = WideFromAscii("Freelancer");
        R.UserData = Hex("AA BB CC");
        R.Instance = Instance;
        R.Application = Application;
        FBytes Out;
        R.Write(Out);
        CHECK(Out[0] == 0 && Out[1] == kEnumResponseCommand);
        FEnumResponse Back;
        CHECK(FEnumResponse::Read(Out.data(), Out.size(), Back));
        CHECK(Back.EnumPayload == 0x1234 && Back.MaxPlayers == 17 && Back.CurrentPlayers == 3);
        CHECK(Ascii(Back.SessionName) == "Freelancer" && Back.UserData == R.UserData && Back.Instance == Instance);

        FEnumQuery Q;
        Q.EnumPayload = 7;
        Q.bHasApplication = true;
        Q.Application = Application;
        Q.UserData = Hex("01");
        Out.clear();
        Q.Write(Out);
        CHECK(Out.size() == 5 + 16 + 1);
        FEnumQuery QBack;
        CHECK(FEnumQuery::Read(Out.data(), Out.size(), QBack));
        CHECK(QBack.bHasApplication && QBack.Application == Application && QBack.UserData == Q.UserData);
    }
}

// ---------------------------------------------------------------------------------------------
// 2. Simulated network

struct FRandom
{
    uint64_t State;
    explicit FRandom(uint64_t Seed) : State(Seed * 2654435761ull + 1) {}
    uint32_t Next()
    {
        State ^= State << 13;
        State ^= State >> 7;
        State ^= State << 17;
        return (uint32_t)(State >> 16);
    }
    bool Chance(uint32_t Percent) { return Next() % 1000 < Percent * 10; }
};

class FNetwork
{
public:
    struct FPacket
    {
        FAddress From;
        FAddress To;
        FBytes Data;
        uint32_t Due;
    };
    using FReceiver = std::function<void(const FAddress&, const uint8_t*, size_t)>;

    class FEndpoint : public ITransport
    {
    public:
        FEndpoint(FNetwork& InNet, const FAddress& InSelf) : Net(InNet), Self(InSelf) {}
        void SendTo(const FAddress& To, const uint8_t* Data, size_t Size) override { Net.Push(Self, To, Data, Size); }
        FNetwork& Net;
        FAddress Self;
    };

    explicit FNetwork(uint64_t Seed) : Random(Seed) {}

    void Attach(const FAddress& Address, FReceiver Receiver) { Receivers[Address] = std::move(Receiver); }

    void Push(const FAddress& From, const FAddress& To, const uint8_t* Data, size_t Size)
    {
        ++Sent;
        if (Recorded.size() < 2000)
            Recorded.push_back({From, To, FBytes(Data, Data + Size), 0});
        if (bCut || Random.Chance(LossPercent))
            return;
        const int Copies = Random.Chance(DuplicatePercent) ? 2 : 1;
        for (int i = 0; i < Copies; ++i)
        {
            const uint32_t Delay = LatencyMs + (JitterMs != 0 ? Random.Next() % JitterMs : 0);
            Queue.push_back({From, To, FBytes(Data, Data + Size), Now + Delay});
        }
    }

    void Deliver(uint32_t InNow)
    {
        Now = InNow;
        std::vector<FPacket> Due;
        for (size_t i = 0; i < Queue.size();)
        {
            if (TimeReached(Now, Queue[i].Due))
            {
                Due.push_back(std::move(Queue[i]));
                Queue.erase(Queue.begin() + (ptrdiff_t)i);
            }
            else
            {
                ++i;
            }
        }
        for (FPacket& P : Due)
        {
            const auto It = Receivers.find(P.To);
            if (It != Receivers.end() && !bCut)
                It->second(P.From, P.Data.data(), P.Data.size());
        }
    }

    FRandom Random;
    uint32_t Now = 0;
    uint32_t LatencyMs = 20;
    uint32_t JitterMs = 0;
    uint32_t LossPercent = 0;
    uint32_t DuplicatePercent = 0;
    bool bCut = false;
    uint64_t Sent = 0;
    std::vector<FPacket> Queue;
    std::vector<FPacket> Recorded;   // the first datagrams sent, for the fuzz test
    std::map<FAddress, FReceiver> Receivers;
};

static const FGuid kFreelancerApp = {0xA690F026, 0x26F0, 0x4E57, {0xAC, 0xA0, 0xEC, 0xF8, 0x68, 0xE4, 0x8D, 0x21}};

struct FTestServer : public IServerEvents
{
    struct FPlayer
    {
        uint32_t Dpnid = 0;
        int Tag = 0;
        bool bAlive = false;
        uint32_t DestroyReason = 0;
        std::vector<FBytes> Received;
    };

    FTestServer(FNetwork& Net, const FAddress& Address, const FLinkConfig& Config = FLinkConfig())
        : Endpoint(Net, Address), Server(Endpoint, *this, Config)
    {
        Net.Attach(Address, [this](const FAddress& From, const uint8_t* Data, size_t Size) {
            Server.OnDatagram(From, Data, Size, Endpoint.Net.Now);
        });
    }

    bool ServerEnumQuery(const FAddress&, const FBytes& Query, FBytes& Response) override
    {
        LastQuery = Query;
        Response = Hex("46 4C");   // "FL"
        return true;
    }
    bool ServerIndicateConnect(const FAddress&, const FPlayerConnectInfo& Info, void*& Context, FBytes& Reply) override
    {
        ++Indicated;
        if (bRefuse)
        {
            Reply = Hex("0B AD");
            return false;
        }
        Players.push_back(std::make_unique<FPlayer>());
        Players.back()->Tag = (int)Players.size();
        Context = Players.back().get();
        LastName = Ascii(Info.Name);
        Reply = Hex("60 0D");
        return true;
    }
    void ServerIndicatedConnectAborted(void*) override { ++Aborted; }
    void ServerCreatePlayer(uint32_t Dpnid, void* Context) override
    {
        if (Context == nullptr)
        {
            OwnDpnid = Dpnid;
            return;
        }
        FPlayer* P = (FPlayer*)Context;
        P->Dpnid = Dpnid;
        P->bAlive = true;
    }
    void ServerDestroyPlayer(uint32_t Dpnid, void* Context, uint32_t Reason) override
    {
        if (Context == nullptr)
        {
            OwnDestroyed = Dpnid;
            return;
        }
        FPlayer* P = (FPlayer*)Context;
        P->bAlive = false;
        P->DestroyReason = Reason;
    }
    void ServerReceive(uint32_t, void* Context, FBytes&& Data) override { ((FPlayer*)Context)->Received.push_back(std::move(Data)); }
    void ServerSendComplete(uint64_t, void*, bool bOk, uint32_t) override { bOk ? ++SendsOk : ++SendsFailed; }

    FNetwork::FEndpoint Endpoint;
    FServer Server;
    std::vector<std::unique_ptr<FPlayer>> Players;
    FBytes LastQuery;
    std::string LastName;
    bool bRefuse = false;
    int Indicated = 0;
    int Aborted = 0;
    uint32_t OwnDpnid = 0;
    uint32_t OwnDestroyed = 0;
    int SendsOk = 0;
    int SendsFailed = 0;
};

struct FTestClient : public IClientEvents
{
    FTestClient(FNetwork& Net, const FAddress& Address, const FLinkConfig& Config = FLinkConfig())
        : Endpoint(Net, Address), Client(Endpoint, *this, Config)
    {
        Net.Attach(Address, [this](const FAddress& From, const uint8_t* Data, size_t Size) {
            Client.OnDatagram(From, Data, Size, Endpoint.Net.Now);
        });
    }

    void ClientEnumResponse(const FAddress&, const FEnumResponse& R, uint32_t) override
    {
        ++EnumResponses;
        LastEnum = R;
    }
    void ClientConnectComplete(uint32_t InResult, const FBytes& InReply) override
    {
        bConnectDone = true;
        Result = InResult;
        Reply = InReply;
    }
    void ClientReceive(FBytes&& Data) override { Received.push_back(std::move(Data)); }
    void ClientSendComplete(uint64_t, void*, bool bOk, uint32_t) override { bOk ? ++SendsOk : ++SendsFailed; }
    void ClientTerminated(bool bByHost, const FBytes& Data, ECloseReason Reason) override
    {
        bTerminated = true;
        bTerminatedByHost = bByHost;
        TerminateData = Data;
        TerminateReason = Reason;
    }

    FNetwork::FEndpoint Endpoint;
    FClient Client;
    int EnumResponses = 0;
    FEnumResponse LastEnum;
    bool bConnectDone = false;
    uint32_t Result = 0xFFFFFFFF;
    FBytes Reply;
    std::vector<FBytes> Received;
    int SendsOk = 0;
    int SendsFailed = 0;
    bool bTerminated = false;
    bool bTerminatedByHost = false;
    FBytes TerminateData;
    ECloseReason TerminateReason = ECloseReason::None;
};

static const FAddress kServerAddress = FAddress::Ipv4(10, 0, 0, 1, 2302);

static FAppDesc MakeDesc()
{
    FAppDesc Desc;
    Desc.Flags = SessionFlags::ClientServer;
    Desc.MaxPlayers = 17;
    Desc.SessionName = WideFromAscii("Test server");
    Desc.Application = kFreelancerApp;
    return Desc;
}

struct FWorld
{
    explicit FWorld(uint64_t Seed) : Net(Seed) {}

    // Advances the clock in 5 ms steps until Done() or the time runs out.
    bool RunUntil(const std::function<bool()>& Done, uint32_t ForMs)
    {
        const uint32_t End = Net.Now + ForMs;
        while (!TimeReached(Net.Now, End))
        {
            if (Done())
                return true;
            Step();
        }
        return Done();
    }
    void Run(uint32_t ForMs)
    {
        RunUntil([] { return false; }, ForMs);
    }
    void Step()
    {
        Net.Now += 5;
        Net.Deliver(Net.Now);
        for (FTestServer* S : Servers)
            S->Server.Tick(Net.Now);
        for (FTestClient* C : Clients)
            C->Client.Tick(Net.Now);
    }

    FNetwork Net;
    std::vector<FTestServer*> Servers;
    std::vector<FTestClient*> Clients;
};

static FBytes Pattern(uint32_t Index, size_t Size)
{
    FBytes Out(Size);
    for (size_t i = 0; i < Size; ++i)
        Out[i] = (uint8_t)(Index * 31 + i * 7);
    return Out;
}

static bool ConnectClient(FWorld& World, FTestClient& Client, const FConnectRequest& Request, uint32_t SessId)
{
    Client.Client.Connect(kServerAddress, Request, SessId, World.Net.Now);
    return World.RunUntil([&] { return Client.bConnectDone; }, 60000);
}

static FConnectRequest Request(const char* Name)
{
    FConnectRequest R;
    R.Application = kFreelancerApp;
    R.Name = WideFromAscii(Name);
    return R;
}

static void TestEnumAndRefusals()
{
    std::printf("Enumeration, refusals\n");
    FWorld World(1);
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    FAppDesc Desc = MakeDesc();
    Desc.MaxPlayers = 2;   // the server's own player and one client
    Desc.Flags |= SessionFlags::RequirePassword;
    Desc.Password = WideFromAscii("secret");
    CHECK(Server.Server.Host(Desc, World.Net.Now, 42));
    CHECK(Server.OwnDpnid == Server.Server.GetLocalDpnid() && Server.OwnDpnid != 0);

    FTestClient A(World.Net, FAddress::Ipv4(10, 0, 0, 2, 2302));
    World.Clients.push_back(&A);
    A.Client.EnumHosts(kServerAddress, &kFreelancerApp, Hex("51"), World.Net.Now);
    World.Run(100);
    CHECK(A.EnumResponses == 1);
    CHECK(Ascii(A.LastEnum.SessionName) == "Test server");
    CHECK(A.LastEnum.UserData == Hex("46 4C") && Server.LastQuery == Hex("51"));
    CHECK(A.LastEnum.CurrentPlayers == 1 && A.LastEnum.MaxPlayers == 2);
    CHECK((A.LastEnum.Flags & SessionFlags::RequirePassword) != 0);
    CHECK(A.LastEnum.Instance == Server.Server.GetApplicationDesc().Instance);
    // Another application's query goes unanswered.
    const FGuid Other = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
    A.Client.EnumHosts(kServerAddress, &Other, FBytes(), World.Net.Now);
    World.Run(100);
    CHECK(A.EnumResponses == 1);

    // Wrong password.
    FConnectRequest Bad = Request("A");
    Bad.bHasPassword = true;
    Bad.Password = WideFromAscii("guess");
    CHECK(ConnectClient(World, A, Bad, 0x1111));
    CHECK(A.Result == Result::InvalidPassword && Server.Indicated == 0);

    // Right password.
    FTestClient B(World.Net, FAddress::Ipv4(10, 0, 0, 3, 2302));
    World.Clients.push_back(&B);
    FConnectRequest Good = Request("B");
    Good.bHasPassword = true;
    Good.Password = WideFromAscii("secret");
    CHECK(ConnectClient(World, B, Good, 0x2222));
    CHECK(B.Result == Result::Ok && B.Reply == Hex("60 0D"));
    World.Run(200);
    CHECK(Server.Players.size() == 1 && Server.Players[0]->bAlive);
    CHECK(Server.LastName == "B");
    CHECK(B.Client.GetSessionInfo().Dpnid == Server.Players[0]->Dpnid);
    CHECK(B.Client.GetSessionInfo().Entries.size() == 2);

    // Full.
    FTestClient C(World.Net, FAddress::Ipv4(10, 0, 0, 4, 2302));
    World.Clients.push_back(&C);
    CHECK(ConnectClient(World, C, Good, 0x3333));
    CHECK(C.Result == Result::SessionFull);

    // Refused by the application, with its reply.
    Server.Server.SetApplicationDesc([&] {
        FAppDesc D = Desc;
        D.MaxPlayers = 0;
        return D;
    }());
    Server.bRefuse = true;
    FTestClient D(World.Net, FAddress::Ipv4(10, 0, 0, 5, 2302));
    World.Clients.push_back(&D);
    CHECK(ConnectClient(World, D, Good, 0x4444));
    CHECK(D.Result == Result::HostRejectedConnection && D.Reply == Hex("0B AD"));

    // Wrong application.
    Server.bRefuse = false;
    FTestClient E(World.Net, FAddress::Ipv4(10, 0, 0, 6, 2302));
    World.Clients.push_back(&E);
    FConnectRequest Wrong = Good;
    Wrong.Application = Other;
    CHECK(ConnectClient(World, E, Wrong, 0x5555));
    CHECK(E.Result == Result::InvalidApplication);

    // Nobody home.
    FTestClient F(World.Net, FAddress::Ipv4(10, 0, 0, 7, 2302));
    World.Clients.push_back(&F);
    F.Client.Connect(FAddress::Ipv4(10, 0, 0, 99, 2302), Good, 0x6666, World.Net.Now);
    CHECK(World.RunUntil([&] { return F.bConnectDone; }, 120000));
    CHECK(F.Result == Result::TimedOut);
}

// Many messages of every size both ways over a bad network; everything arrives once, in order.
static void TestTraffic(uint64_t Seed, uint32_t Loss, uint32_t ClientVersion)
{
    std::printf("Traffic: seed %llu, %u%% loss, client protocol %08X\n", (unsigned long long)Seed, Loss, ClientVersion);
    FWorld World(Seed);
    World.Net.LatencyMs = 30;
    World.Net.JitterMs = 40;   // reorders
    World.Net.LossPercent = Loss;
    World.Net.DuplicatePercent = 2;
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    CHECK(Server.Server.Host(MakeDesc(), World.Net.Now, Seed));
    FLinkConfig ClientConfig;
    ClientConfig.ProtocolVersion = ClientVersion;
    FTestClient Client(World.Net, FAddress::Ipv4(192, 168, 1, 20, 2302), ClientConfig);
    World.Clients.push_back(&Client);
    CHECK(ConnectClient(World, Client, Request("Trader"), 0xC0FFEE00u + (uint32_t)Seed));
    CHECK(Client.Result == Result::Ok);
    CHECK(World.RunUntil([&] { return !Server.Players.empty() && Server.Players[0]->bAlive; }, 10000));
    if (Server.Players.empty())
        return;
    FTestServer::FPlayer& Player = *Server.Players[0];

    // Sizes cross the coalescing limit, the frame size and several frames.
    const size_t Sizes[] = {1, 2, 3, 17, 200, 600, 1200, 1452, 1453, 2047, 2048, 3000, 9000, 40000};
    std::vector<FBytes> ToServer, ToClient;
    uint32_t Index = 0;
    for (int Round = 0; Round < 6; ++Round)
    {
        for (size_t Size : Sizes)
        {
            ToServer.push_back(Pattern(Index++, Size));
            ToClient.push_back(Pattern(Index++, Size));
            const uint32_t Flags = SendFlags::Guaranteed | SendFlags::Coalesce;   // DALib's 0x108
            CHECK(Client.Client.Send(ToServer.back().data(), ToServer.back().size(), Flags, nullptr, nullptr, World.Net.Now));
            CHECK(Server.Server.SendTo(Player.Dpnid, ToClient.back().data(), ToClient.back().size(), Flags, nullptr, nullptr,
                                       World.Net.Now));
        }
        // A burst of tiny messages in one go: coalesced when both sides support it.
        for (int i = 0; i < 40; ++i)
        {
            ToServer.push_back(Pattern(Index++, 1 + i % 9));
            CHECK(Client.Client.Send(ToServer.back().data(), ToServer.back().size(), SendFlags::Guaranteed | SendFlags::Coalesce,
                                     nullptr, nullptr, World.Net.Now));
        }
        World.Run(50);
    }
    const bool bDone = World.RunUntil(
        [&] { return Player.Received.size() >= ToServer.size() && Client.Received.size() >= ToClient.size(); }, 300000);
    CHECK(bDone);
    CHECK(Player.Received == ToServer);
    CHECK(Client.Received == ToClient);
    World.RunUntil([&] { return Client.SendsOk == (int)ToServer.size() && Server.SendsOk == (int)ToClient.size(); }, 60000);
    CHECK(Client.SendsOk == (int)ToServer.size() && Client.SendsFailed == 0);
    CHECK(Server.SendsOk == (int)ToClient.size() && Server.SendsFailed == 0);
    const FLinkStats& Stats = Client.Client.GetLinkStats();
    if (Loss >= 5)
        CHECK(Stats.FramesRetried > 0);
    std::printf("  client: %llu frames sent, %llu retried, rtt %u ms\n", (unsigned long long)Stats.FramesSent,
                (unsigned long long)Stats.FramesRetried, Stats.RttMs);

    // The client leaves.
    Client.Client.Close(World.Net.Now);
    CHECK(World.RunUntil([&] { return !Player.bAlive && Client.bTerminated; }, 60000));
    CHECK(Player.DestroyReason == DestroyReason::Normal || (Loss != 0 && Player.DestroyReason == DestroyReason::ConnectionLost));
    CHECK(!Client.bTerminatedByHost);
}

static void TestUnreliable()
{
    std::printf("Unreliable sends\n");
    FWorld World(7);
    World.Net.LatencyMs = 25;
    World.Net.JitterMs = 30;
    World.Net.LossPercent = 15;
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    Server.Server.Host(MakeDesc(), World.Net.Now, 7);
    FTestClient Client(World.Net, FAddress::Ipv4(10, 1, 1, 1, 4000));
    World.Clients.push_back(&Client);
    CHECK(ConnectClient(World, Client, Request("U"), 0x77));
    World.RunUntil([&] { return !Server.Players.empty() && Server.Players[0]->bAlive; }, 10000);
    if (Server.Players.empty())
        return;
    FTestServer::FPlayer& Player = *Server.Players[0];
    for (uint32_t i = 0; i < 300; ++i)
    {
        FBytes Data(4);
        Data[0] = (uint8_t)i;
        Data[1] = (uint8_t)(i >> 8);
        Client.Client.Send(Data.data(), Data.size(), 0, nullptr, nullptr, World.Net.Now);
        if (i % 10 == 0)
            World.Run(20);
    }
    const FBytes Last = Hex("FF FF FF FF FF");
    Client.Client.Send(Last.data(), Last.size(), SendFlags::Guaranteed, nullptr, nullptr, World.Net.Now);
    CHECK(World.RunUntil([&] { return !Player.Received.empty() && Player.Received.back() == Last; }, 120000));
    // Some were lost, none repeated, none out of order.
    int Previous = -1;
    bool bOrdered = true;
    for (size_t i = 0; i + 1 < Player.Received.size(); ++i)
    {
        const int V = Player.Received[i][0] | Player.Received[i][1] << 8;
        if (V <= Previous)
            bOrdered = false;
        Previous = V;
    }
    CHECK(bOrdered);
    CHECK(Player.Received.size() < 301 && Player.Received.size() > 150);
    std::printf("  %zu of 300 unreliable messages arrived\n", Player.Received.size() - 1);
}

static void TestEndings()
{
    std::printf("KeepAlive, DestroyClient, lost connection, server close\n");
    FWorld World(3);
    World.Net.LatencyMs = 40;
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    Server.Server.Host(MakeDesc(), World.Net.Now, 3);

    FTestClient A(World.Net, FAddress::Ipv4(10, 0, 1, 1, 2302));
    FTestClient B(World.Net, FAddress::Ipv4(10, 0, 1, 2, 2302));
    FTestClient C(World.Net, FAddress::Ipv4(10, 0, 1, 3, 2302));
    World.Clients = {&A, &B, &C};
    CHECK(ConnectClient(World, A, Request("A"), 0xA));
    CHECK(ConnectClient(World, B, Request("B"), 0xB));
    CHECK(ConnectClient(World, C, Request("C"), 0xC));
    World.Run(500);
    CHECK(Server.Players.size() == 3);
    if (Server.Players.size() != 3)
        return;
    CHECK(Server.Server.GetPlayerCount() == 4);
    // Distinct DPNIDs.
    CHECK(Server.Players[0]->Dpnid != Server.Players[1]->Dpnid && Server.Players[1]->Dpnid != Server.Players[2]->Dpnid);

    // Two idle minutes: KeepAlives hold every connection.
    World.Run(120000);
    CHECK(Server.Players[0]->bAlive && Server.Players[1]->bAlive && Server.Players[2]->bAlive);
    CHECK(!A.bTerminated && !B.bTerminated && !C.bTerminated);

    // Kicked, with a reason.
    CHECK(Server.Server.DestroyClient(Server.Players[0]->Dpnid, Hex("4B 49 43 4B").data(), 4, World.Net.Now));
    CHECK(World.RunUntil([&] { return !Server.Players[0]->bAlive && A.bTerminated; }, 30000));
    CHECK(Server.Players[0]->DestroyReason == DestroyReason::HostDestroyedPlayer);
    CHECK(A.bTerminatedByHost && A.TerminateData == Hex("4B 49 43 4B"));
    CHECK(!Server.Server.SendTo(Server.Players[0]->Dpnid, Hex("01").data(), 1, SendFlags::Guaranteed, nullptr, nullptr,
                                World.Net.Now));

    // B's machine vanishes: the server notices through retries or KeepAlives.
    World.Clients = {&C};
    World.Net.Receivers.erase(B.Endpoint.Self);
    const FBytes Ping = Hex("01");
    Server.Server.SendTo(Server.Players[1]->Dpnid, Ping.data(), 1, SendFlags::Guaranteed, nullptr, nullptr, World.Net.Now);
    CHECK(World.RunUntil([&] { return !Server.Players[1]->bAlive; }, 120000));
    CHECK(Server.Players[1]->DestroyReason == DestroyReason::ConnectionLost);
    CHECK(Server.SendsFailed == 1);
    CHECK(C.Received.empty() && Server.Players[2]->bAlive);

    // The session ends: C is told, then the server's own player goes.
    Server.Server.Close(World.Net.Now);
    CHECK(World.RunUntil([&] { return Server.Server.IsIdle() && C.bTerminated; }, 60000));
    CHECK(C.bTerminated && C.bTerminatedByHost);
    CHECK(Server.Players[2]->DestroyReason == DestroyReason::SessionTerminated);
    CHECK(Server.OwnDestroyed == Server.OwnDpnid);
    CHECK(Server.Aborted == 0);
}

static void TestHardDisconnect()
{
    std::printf("Hard disconnect\n");
    struct FSink : public ILinkEvents
    {
        void LinkTransmit(const uint8_t* Data, size_t Size) override { Out.emplace_back(Data, Data + Size); }
        void LinkReceive(FBytes&&, uint8_t) override {}
        void LinkClosed(ECloseReason R) override { Reason = R; }
        std::vector<FBytes> Out;
        ECloseReason Reason = ECloseReason::None;
    };
    FSink SA, SB;
    FLink A(SA, FLinkConfig());
    FLink B(SB, FLinkConfig());
    uint32_t Now = 1000;
    auto Exchange = [&] {
        for (int Round = 0; Round < 8; ++Round)
        {
            std::vector<FBytes> ToB = std::move(SA.Out), ToA = std::move(SB.Out);
            SA.Out.clear();
            SB.Out.clear();
            for (const FBytes& D : ToB)
            {
                FCommandFrame F;
                if (B.GetState() == ELinkState::Idle && DecodeCommandFrame(D.data(), D.size(), F))
                    B.Accept(F.Connect, Now);
                else
                    B.OnDatagram(D.data(), D.size(), Now);
            }
            for (const FBytes& D : ToA)
                A.OnDatagram(D.data(), D.size(), Now);
            Now += 5;
            A.Tick(Now);
            B.Tick(Now);
        }
    };
    A.Connect(0x1234, Now);
    Exchange();
    CHECK(A.GetState() == ELinkState::Established && B.GetState() == ELinkState::Established);
    A.HardClose(Now);
    Exchange();
    CHECK(B.IsClosed() && SB.Reason == ECloseReason::HardDisconnectRemote);
    CHECK(A.IsClosed() && SA.Reason == ECloseReason::HardDisconnectLocal);
}

// Garbage and corrupted copies of real traffic, from strangers and from a connected client's
// own address. Nothing may crash (run under AddressSanitizer), and a stranger's garbage must
// not disturb a connected client.
static void TestFuzz()
{
    std::printf("Malformed datagrams\n");
    FWorld World(11);
    World.Net.LatencyMs = 10;
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    Server.Server.Host(MakeDesc(), World.Net.Now, 11);
    FTestClient Victim(World.Net, FAddress::Ipv4(10, 9, 9, 9, 2302));
    FTestClient Bystander(World.Net, FAddress::Ipv4(10, 9, 9, 8, 2302));
    World.Clients = {&Victim, &Bystander};
    CHECK(ConnectClient(World, Victim, Request("V"), 0x51));
    CHECK(ConnectClient(World, Bystander, Request("B"), 0x52));
    Victim.Client.EnumHosts(kServerAddress, nullptr, Hex("01 02"), World.Net.Now);
    for (int i = 0; i < 20; ++i)
    {
        const FBytes Data = Pattern((uint32_t)i, 50 + i * 300);
        Victim.Client.Send(Data.data(), Data.size(), SendFlags::Guaranteed, nullptr, nullptr, World.Net.Now);
        Server.Server.SendTo(Server.Players[0]->Dpnid, Data.data(), Data.size(), SendFlags::Guaranteed, nullptr, nullptr,
                             World.Net.Now);
    }
    World.Run(2000);
    const std::vector<FNetwork::FPacket> Real = World.Net.Recorded;
    CHECK(Real.size() > 50);

    FRandom R(5150);
    const uint8_t Leads[] = {0x00, 0x80, 0x88, 0x01, 0x3F, 0x7F, 0x07, 0xFF};
    for (int i = 0; i < 30000; ++i)
    {
        FBytes Data;
        if (R.Next() % 2 == 0 && !Real.empty())
        {
            Data = Real[R.Next() % Real.size()].Data;
            const int Flips = 1 + (int)(R.Next() % 4);
            for (int f = 0; f < Flips && !Data.empty(); ++f)
                Data[R.Next() % Data.size()] ^= (uint8_t)(1 + R.Next() % 255);
            if (R.Next() % 4 == 0)
                Data.resize(R.Next() % (Data.size() + 1));
        }
        else
        {
            Data.resize(R.Next() % 120);
            for (uint8_t& B : Data)
                B = (uint8_t)R.Next();
            if (!Data.empty())
                Data[0] = Leads[R.Next() % sizeof(Leads)];
        }
        const bool bFromVictim = R.Next() % 3 == 0;
        const FAddress From = bFromVictim ? Victim.Endpoint.Self : FAddress::Ipv4(172, 16, (uint8_t)R.Next(), (uint8_t)R.Next(), 2302);
        Server.Server.OnDatagram(From, Data.data(), Data.size(), World.Net.Now);
        Victim.Client.OnDatagram(kServerAddress, Data.data(), Data.size(), World.Net.Now);
        if (i % 50 == 0)
            World.Step();
    }
    World.Run(30000);
    // The bystander never saw a corrupted packet from its own address.
    const FBytes Hello = Hex("48 69");
    CHECK(Bystander.Client.Send(Hello.data(), Hello.size(), SendFlags::Guaranteed, nullptr, nullptr, World.Net.Now));
    CHECK(World.RunUntil([&] { return !Server.Players[1]->Received.empty() && Server.Players[1]->Received.back() == Hello; },
                         20000));
}

// A complete non-sequential message that overtakes the middle of a large one must not disturb
// the large one's reassembly.
static void TestEarlyDelivery()
{
    std::printf("Non-sequential message inside a large one\n");
    struct FSink : public ILinkEvents
    {
        void LinkTransmit(const uint8_t* Data, size_t Size) override { Out.emplace_back(Data, Data + Size); }
        void LinkReceive(FBytes&& M, uint8_t) override { In.push_back(std::move(M)); }
        void LinkClosed(ECloseReason) override {}
        std::vector<FBytes> Out;
        std::vector<FBytes> In;
    };
    FSink SA, SB;
    FLinkConfig Config;
    FLink A(SA, Config);
    FLink B(SB, Config);
    uint32_t Now = 5000;
    auto Exchange = [&] {
        for (int Round = 0; Round < 6; ++Round)
        {
            std::vector<FBytes> ToB = std::move(SA.Out), ToA = std::move(SB.Out);
            SA.Out.clear();
            SB.Out.clear();
            for (const FBytes& D : ToB)
            {
                FCommandFrame F;
                if (B.GetState() == ELinkState::Idle && DecodeCommandFrame(D.data(), D.size(), F))
                    B.Accept(F.Connect, Now);
                else
                    B.OnDatagram(D.data(), D.size(), Now);
            }
            for (const FBytes& D : ToA)
                A.OnDatagram(D.data(), D.size(), Now);
            Now += 50;
            A.Tick(Now);
            B.Tick(Now);
        }
    };
    A.Connect(0x99, Now);
    Exchange();
    CHECK(A.GetState() == ELinkState::Established && B.GetState() == ELinkState::Established);
    SA.Out.clear();
    // Open the window so all four frames go out at once.
    for (int i = 0; i < 6; ++i)
    {
        const FBytes Small = Pattern(1000 + (uint32_t)i, 10);
        A.Send(Small, MsgFlags::Reliable | MsgFlags::Sequential, Now);
        Exchange();
    }
    SB.In.clear();
    SA.Out.clear();
    const FBytes Large = Pattern(7, 3000);   // three frames
    const FBytes Quick = Pattern(8, 20);
    A.Send(Large, MsgFlags::Reliable | MsgFlags::Sequential, Now);
    A.Send(Quick, MsgFlags::Reliable, Now);  // non-sequential
    CHECK(SA.Out.size() == 4);
    if (SA.Out.size() == 4)
    {
        const int Order[] = {0, 3, 1, 2};
        for (int i : Order)
            B.OnDatagram(SA.Out[(size_t)i].data(), SA.Out[(size_t)i].size(), Now);
    }
    CHECK(SB.In.size() == 2);
    if (SB.In.size() == 2)
    {
        CHECK(SB.In[0] == Quick);
        CHECK(SB.In[1] == Large);
    }
}

// A connection that never becomes a player is dropped after the join timeout.
static void TestJoinTimeout()
{
    std::printf("Join timeout\n");
    FWorld World(21);
    FTestServer Server(World.Net, kServerAddress);
    World.Servers.push_back(&Server);
    Server.Server.Host(MakeDesc(), World.Net.Now, 21);
    Server.Server.SetJoinLimits(5000, 64);
    struct FSilent : public ILinkEvents
    {
        FNetwork::FEndpoint* Endpoint = nullptr;
        void LinkTransmit(const uint8_t* Data, size_t Size) override { Endpoint->SendTo(kServerAddress, Data, Size); }
        void LinkReceive(FBytes&&, uint8_t) override {}
        void LinkClosed(ECloseReason R) override { Reason = R; }
        ECloseReason Reason = ECloseReason::None;
    };
    const FAddress Self = FAddress::Ipv4(10, 5, 5, 5, 2302);
    FNetwork::FEndpoint Endpoint(World.Net, Self);
    FSilent Silent;
    Silent.Endpoint = &Endpoint;
    FLink Link(Silent, FLinkConfig());
    World.Net.Attach(Self, [&](const FAddress&, const uint8_t* Data, size_t Size) { Link.OnDatagram(Data, Size, World.Net.Now); });
    Link.Connect(0x5151, World.Net.Now);
    // Linked, but never sends the player connect message.
    CHECK(World.RunUntil([&] { Link.Tick(World.Net.Now); return Link.GetState() == ELinkState::Established; }, 5000));
    CHECK(World.RunUntil([&] { Link.Tick(World.Net.Now); return Link.IsClosed(); }, 20000));
    CHECK(Silent.Reason == ECloseReason::HardDisconnectRemote);
    CHECK(Server.Indicated == 0 && Server.Server.GetPlayerCount() == 1);
}

int main(int argc, char** argv)
{
    TestSpecReliable();
    TestCoalesce();
    TestSpecCore();
    TestEnumAndRefusals();
    TestTraffic(1, 0, kProtocolVersion);
    TestTraffic(2, 10, kProtocolVersion);
    TestTraffic(3, 25, kProtocolVersion);
    TestTraffic(4, 10, 0x00010004);   // a partner without coalescing or KeepAlive payloads
    TestUnreliable();
    TestEndings();
    TestHardDisconnect();
    TestFuzz();
    TestEarlyDelivery();
    TestJoinTimeout();
    // --stress N: N more traffic runs with assorted seeds and loss rates. Up to 15% each way:
    // above that a frame failing eleven times in a row ends the connection now and then, as
    // the specification's ten retries intend.
    if (argc == 3 && std::strcmp(argv[1], "--stress") == 0)
        for (int Seed = 100; Seed < 100 + std::atoi(argv[2]); ++Seed)
            TestTraffic((uint64_t)Seed, (uint32_t)(Seed * 7 % 16), Seed % 3 == 0 ? 0x00010004 : kProtocolVersion);
    std::printf("%d checks, %d failed\n", Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
