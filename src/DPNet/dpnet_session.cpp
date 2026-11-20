// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation (port-only, not part of retail).
// The [MC-DPL8CS] session layer, client/server mode; see dpnet_session.h. Section numbers refer
// to [MC-DPL8CS] unless noted.

#include "dpnet_session.h"

#include <cstdio>

namespace DPNet
{
    // ---------------------------------------------------------------------------------------
    // FAddress

    FAddress FAddress::Ipv4(uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint16_t InPort)
    {
        FAddress Out;
        Out.Family = 4;
        Out.Ip[0] = A;
        Out.Ip[1] = B;
        Out.Ip[2] = C;
        Out.Ip[3] = D;
        Out.Port = InPort;
        return Out;
    }

    std::string FAddress::HostText() const
    {
        char Text[64];
        if (Family == 4)
        {
            std::snprintf(Text, sizeof(Text), "%u.%u.%u.%u", Ip[0], Ip[1], Ip[2], Ip[3]);
        }
        else
        {
            std::string Out;
            for (int i = 0; i < 16; i += 2)
            {
                std::snprintf(Text, sizeof(Text), "%s%x", i == 0 ? "" : ":", (unsigned)(Ip[i] << 8 | Ip[i + 1]));
                Out += Text;
            }
            return Out;
        }
        return Text;
    }

    std::string FAddress::Url() const
    {
        std::string Guid = kProviderTcpIp.ToString();
        // "{...}" is written %7B...%7D in the URL.
        Guid = "%7B" + Guid.substr(1, Guid.size() - 2) + "%7D";
        return "x-directplay:/provider=" + Guid + ";hostname=" + HostText() + ";port=" + std::to_string(Port);
    }

    bool FAddress::operator==(const FAddress& Other) const
    {
        return Family == Other.Family && Ip == Other.Ip && Port == Other.Port;
    }

    bool FAddress::operator<(const FAddress& Other) const
    {
        if (Family != Other.Family)
            return Family < Other.Family;
        if (Ip != Other.Ip)
            return Ip < Other.Ip;
        return Port < Other.Port;
    }

    static FBytes CoreMessage(uint32_t Type)
    {
        FBytes Out;
        FWriter(Out).U32(Type);
        return Out;
    }

    static uint32_t CoreType(const FBytes& Message)
    {
        if (Message.size() < 4)
            return 0;
        FReader R(Message.data(), Message.size());
        return R.U32();
    }

    static constexpr uint8_t kCoreFlags = MsgFlags::Reliable | MsgFlags::Sequential | MsgFlags::User1;

    static uint8_t LinkFlags(uint32_t SendFlags)
    {
        uint8_t Flags = 0;
        if ((SendFlags & (SendFlags::Guaranteed | SendFlags::CompleteOnProcess)) != 0)
            Flags |= MsgFlags::Reliable;
        if ((SendFlags & SendFlags::NonSequential) == 0)
            Flags |= MsgFlags::Sequential;
        if ((SendFlags & SendFlags::Coalesce) != 0)
            Flags |= MsgFlags::Coalesce;
        return Flags;
    }

    // ---------------------------------------------------------------------------------------
    // FServer::FPeer: one remote address and its link.

    struct FServer::FPeer : public ILinkEvents
    {
        enum class EState
        {
            Linking,          // [MC-DPL8R] handshake, or waiting for the player connect message
            AwaitingAck,      // DN_SEND_CONNECT_INFO sent, player indicated
            Player,           // created
            Gone,             // refused or closed; reaped soon
        };

        struct FSendRecord
        {
            uint64_t Handle = 0;
            void* UserContext = nullptr;
            uint32_t Start = 0;
            bool bNotify = true;
        };

        FPeer(FServer& InServer, const FAddress& InAddress)
            : Server(InServer), Address(InAddress), Link(*this, InServer.Config), Since(InServer.Now)
        {
        }

        void LinkTransmit(const uint8_t* Data, size_t Size) override { Server.Transport.SendTo(Address, Data, Size); }

        void LinkReceive(FBytes&& Message, uint8_t UserFlags) override
        {
            if ((UserFlags & MsgFlags::User1) != 0)
            {
                switch (CoreType(Message))
                {
                case CoreMsg::PlayerConnectInfo:
                    if (State == EState::Linking)
                        Server.HandleConnectInfo(*this, Message);
                    break;
                case CoreMsg::AckConnectInfo:
                    if (State == EState::AwaitingAck)
                    {
                        State = EState::Player;
                        Server.Players[Dpnid] = this;
                        Server.Events.ServerCreatePlayer(Dpnid, Context);
                    }
                    break;
                case CoreMsg::ReqProcessCompletion:
                    // 2.2.3.2: deliver the payload, then confirm it.
                    if (State == EState::Player && Message.size() >= 8)
                    {
                        FBytes Reply = CoreMessage(CoreMsg::ProcessCompletion);
                        Reply.insert(Reply.end(), Message.begin() + 4, Message.begin() + 8);
                        FBytes Payload(Message.begin() + 8, Message.end());
                        Server.Events.ServerReceive(Dpnid, Context, std::move(Payload));
                        Link.Send(std::move(Reply), kCoreFlags, Server.Now);
                    }
                    break;
                default:
                    break;
                }
                return;
            }
            if ((UserFlags & MsgFlags::User2) != 0)
                return;   // DirectPlay Voice
            if (State == EState::Player)
                Server.Events.ServerReceive(Dpnid, Context, std::move(Message));
        }

        void LinkSendComplete(uint64_t Id, bool bOk) override
        {
            const auto It = Sends.find(Id);
            if (It == Sends.end())
                return;
            const FSendRecord Record = It->second;
            Sends.erase(It);
            if (Record.bNotify)
                Server.Events.ServerSendComplete(Record.Handle, Record.UserContext, bOk, Server.Now - Record.Start);
        }

        void LinkRemoteClosing() override
        {
            if (LeaveReason == DestroyReason::ConnectionLost)
                LeaveReason = DestroyReason::Normal;
        }

        void LinkClosed(ECloseReason Reason) override { Server.PeerClosed(*this, Reason); }

        FServer& Server;
        FAddress Address;
        FLink Link;
        EState State = EState::Linking;
        uint32_t Dpnid = 0;
        void* Context = nullptr;
        uint32_t LeaveReason = DestroyReason::ConnectionLost;   // DestroyReason for ServerDestroyPlayer
        uint32_t Since = 0;   // when the CONNECT arrived
        std::map<uint64_t, FSendRecord> Sends;
    };

    // ---------------------------------------------------------------------------------------
    // FServer

    FServer::FServer(ITransport& InTransport, IServerEvents& InEvents, const FLinkConfig& InConfig)
        : Transport(InTransport), Events(InEvents), Config(InConfig)
    {
    }

    FServer::~FServer() = default;

    static uint64_t SplitMix(uint64_t& State)
    {
        uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
        Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
        Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
        return Z ^ (Z >> 31);
    }

    bool FServer::Host(const FAppDesc& InDesc, uint32_t InNow, uint64_t Seed)
    {
        if (bHosting || !Peers.empty())
            return false;
        Now = InNow;
        Desc = InDesc;
        if (Desc.Instance.IsZero())
        {
            // A random (version 4) GUID.
            const uint64_t A = SplitMix(Seed);
            const uint64_t B = SplitMix(Seed);
            Desc.Instance.Data1 = (uint32_t)A;
            Desc.Instance.Data2 = (uint16_t)(A >> 32);
            Desc.Instance.Data3 = (uint16_t)(((A >> 48) & 0x0FFF) | 0x4000);
            for (int i = 0; i < 8; ++i)
                Desc.Instance.Data4[i] = (uint8_t)(B >> (8 * i));
            Desc.Instance.Data4[0] = (uint8_t)((Desc.Instance.Data4[0] & 0x3F) | 0x80);
        }
        bHosting = true;
        bClosing = false;
        // Name table: the all-players group is entry 1 (version 1), the server's player entry 2.
        NameTableVersion = 2;
        NextIndex = 3;
        LocalDpnid = MakeDpnid(2, 2, Desc.Instance);
        ++Depth;
        Events.ServerCreatePlayer(LocalDpnid, nullptr);
        --Depth;
        return true;
    }

    void FServer::SetApplicationDesc(const FAppDesc& InDesc)
    {
        const FGuid Application = Desc.Application;
        const FGuid Instance = Desc.Instance;
        Desc = InDesc;
        if (bHosting)
        {
            Desc.Application = Application;
            Desc.Instance = Instance;
        }
    }

    uint32_t FServer::GetPlayerCount() const
    {
        uint32_t Count = bHosting ? 1 : 0;
        for (const auto& Entry : Peers)
            if (Entry.second->State == FPeer::EState::AwaitingAck || Entry.second->State == FPeer::EState::Player)
                ++Count;
        return Count;
    }

    void FServer::OnDatagram(const FAddress& From, const uint8_t* Data, size_t Size, uint32_t InNow)
    {
        if (Size == 0)
            return;
        Now = InNow;
        ++Depth;
        if (Data[0] == 0)
        {
            // [MC-DPLHP]: a zero lead byte is host enumeration.
            HandleEnumQuery(From, Data, Size);
        }
        else if (const auto It = Peers.find(From); It != Peers.end())
        {
            It->second->Link.OnDatagram(Data, Size, Now);
        }
        else if (bHosting && !bClosing && Peers.size() - Players.size() < MaxJoining)
        {
            // [MC-DPL8R] 3.1.5.1.1: a CONNECT from an unknown address is a new connection.
            FCommandFrame Frame;
            if (DecodeCommandFrame(Data, Size, Frame) && Frame.Op == ExtOp::Connect)
            {
                auto Peer = std::make_unique<FPeer>(*this, From);
                if (Peer->Link.Accept(Frame.Connect, Now))
                    Peers.emplace(From, std::move(Peer));
            }
        }
        --Depth;
        Reap();
    }

    void FServer::Tick(uint32_t InNow)
    {
        Now = InNow;
        ++Depth;
        for (auto& Entry : Peers)
        {
            FPeer& Peer = *Entry.second;
            Peer.Link.Tick(Now);
            const bool bJoining = Peer.State == FPeer::EState::Linking || Peer.State == FPeer::EState::AwaitingAck;
            if (bJoining && !Peer.Link.IsClosed() && !Peer.Link.IsClosing() && Now - Peer.Since >= JoinTimeoutMs)
                Peer.Link.HardClose(Now);
        }
        --Depth;
        Reap();
    }

    void FServer::HandleEnumQuery(const FAddress& From, const uint8_t* Data, size_t Size)
    {
        if (!bHosting || bClosing || (Desc.Flags & SessionFlags::NoEnums) != 0)
            return;
        FEnumQuery Query;
        if (!FEnumQuery::Read(Data, Size, Query))
            return;
        if (Query.bHasApplication && Query.Application != Desc.Application)
            return;
        FEnumResponse Response;
        if (!Events.ServerEnumQuery(From, Query.UserData, Response.UserData))
            return;
        Response.EnumPayload = Query.EnumPayload;
        Response.Flags = Desc.Flags;
        Response.MaxPlayers = Desc.MaxPlayers;
        Response.CurrentPlayers = GetPlayerCount();
        Response.SessionName = Desc.SessionName;
        Response.ApplicationReserved = Desc.ApplicationReserved;
        Response.Instance = Desc.Instance;
        Response.Application = Desc.Application;
        FBytes Out;
        Response.Write(Out);
        Transport.SendTo(From, Out.data(), Out.size());
    }

    // 3.1.5.1: validate the player connect message, indicate the player, answer with
    // DN_SEND_CONNECT_INFO (or DN_CONNECT_FAILED).
    void FServer::HandleConnectInfo(FPeer& Peer, const FBytes& Message)
    {
        FPlayerConnectInfo Info;
        if (!FPlayerConnectInfo::Read(Message.data(), Message.size(), Info))
            return;

        uint32_t Failure = Result::Ok;
        if (!bHosting || bClosing)
            Failure = Result::AlreadyClosing;
        else if ((Info.Flags & ObjectType::Client) == 0)
            Failure = Result::InvalidInterface;
        else if (!Info.Instance.IsZero() && Info.Instance != Desc.Instance)
            Failure = Result::InvalidInstance;
        else if (Info.Application != Desc.Application)
            Failure = Result::InvalidApplication;
        else if ((Desc.Flags & SessionFlags::RequirePassword) != 0 && (!Info.bHasPassword || Info.Password != Desc.Password))
            Failure = Result::InvalidPassword;
        else if (Desc.MaxPlayers != 0 && GetPlayerCount() >= Desc.MaxPlayers)
            Failure = Result::SessionFull;

        FBytes Reply;
        if (Failure == Result::Ok)
        {
            void* Context = nullptr;
            if (Events.ServerIndicateConnect(Peer.Address, Info, Context, Reply))
            {
                Peer.Context = Context;
                Peer.State = FPeer::EState::AwaitingAck;
            }
            else
            {
                Failure = Result::HostRejectedConnection;
            }
        }
        if (Failure != Result::Ok)
        {
            FConnectFailed Failed;
            Failed.ResultCode = Failure;
            Failed.Reply = Reply;
            FBytes Out;
            Failed.Write(Out);
            Peer.State = FPeer::EState::Gone;
            Peer.Link.Send(std::move(Out), kCoreFlags, Now);
            Peer.Link.Close(Now);
            return;
        }

        ++NameTableVersion;
        Peer.Dpnid = MakeDpnid(NextIndex++, NameTableVersion, Desc.Instance);

        FSendConnectInfo Connect;
        Connect.Reply = Reply;
        Connect.Flags = Desc.Flags;
        Connect.MaxPlayers = Desc.MaxPlayers;
        Connect.CurrentPlayers = GetPlayerCount();
        Connect.bHasSessionName = !Desc.SessionName.empty();
        Connect.SessionName = Desc.SessionName;
        if ((Desc.Flags & SessionFlags::RequirePassword) != 0)
        {
            Connect.bHasPassword = true;
            Connect.Password = Desc.Password;
        }
        Connect.ApplicationReserved = Desc.ApplicationReserved;
        Connect.Instance = Desc.Instance;
        Connect.Application = Desc.Application;
        Connect.Dpnid = Peer.Dpnid;
        Connect.NameTableVersion = NameTableVersion;

        // Client/server: only the server's entry and the new client's (2.2.1.4).
        FNameTableEntry ServerEntry;
        ServerEntry.Dpnid = LocalDpnid;
        ServerEntry.Flags = EntryFlags::Server | EntryFlags::Host;
        ServerEntry.Version = 2;
        ServerEntry.DnetVersion = 8;
        FNameTableEntry ClientEntry;
        ClientEntry.Dpnid = Peer.Dpnid;
        ClientEntry.Flags = EntryFlags::Client;
        ClientEntry.Version = NameTableVersion;
        ClientEntry.DnetVersion = Info.DnetVersion;
        ClientEntry.bHasName = !Info.Name.empty();
        ClientEntry.Name = Info.Name;
        ClientEntry.Data = Info.Data;
        ClientEntry.Url = Peer.Address.Url();
        Connect.Entries.push_back(ServerEntry);
        Connect.Entries.push_back(ClientEntry);

        FBytes Out;
        Connect.Write(Out);
        Peer.Link.Send(std::move(Out), kCoreFlags, Now);
    }

    bool FServer::SendTo(uint32_t Dpnid, const uint8_t* Data, size_t Size, uint32_t Flags, void* UserContext,
                         uint64_t* OutHandle, uint32_t InNow)
    {
        Now = InNow;
        const auto It = Players.find(Dpnid);
        if (It == Players.end() || It->second->State != FPeer::EState::Player || Size == 0)
            return false;
        FPeer& Peer = *It->second;
        const uint64_t Id = Peer.Link.PeekNextMessageId();
        FPeer::FSendRecord& Record = Peer.Sends[Id];
        Record.Handle = NextHandle++;
        Record.UserContext = UserContext;
        Record.Start = Now;
        Record.bNotify = (Flags & SendFlags::NoComplete) == 0;
        const uint64_t Handle = Record.Handle;
        ++Depth;
        const uint64_t Sent = Peer.Link.Send(FBytes(Data, Data + Size), LinkFlags(Flags), Now);
        --Depth;
        if (Sent == 0)
        {
            Peer.Sends.erase(Id);
            Reap();
            return false;
        }
        if (OutHandle != nullptr)
            *OutHandle = Handle;
        Reap();
        return true;
    }

    bool FServer::DestroyClient(uint32_t Dpnid, const uint8_t* Data, size_t Size, uint32_t InNow)
    {
        Now = InNow;
        const auto It = Players.find(Dpnid);
        if (It == Players.end() || It->second->State != FPeer::EState::Player || It->second->Link.IsClosing())
            return false;
        FPeer& Peer = *It->second;
        Peer.LeaveReason = DestroyReason::HostDestroyedPlayer;
        FTerminateSession Terminate;
        if (Data != nullptr)
            Terminate.Data.assign(Data, Data + Size);
        FBytes Out;
        Terminate.Write(Out);
        ++Depth;
        Peer.Link.Send(std::move(Out), kCoreFlags, Now);
        Peer.Link.Close(Now);
        --Depth;
        Reap();
        return true;
    }

    void FServer::Close(uint32_t InNow)
    {
        if (!bHosting || bClosing)
            return;
        Now = InNow;
        bClosing = true;
        ++Depth;
        for (auto& Entry : Peers)
        {
            FPeer& Peer = *Entry.second;
            if (Peer.State == FPeer::EState::Player || Peer.State == FPeer::EState::AwaitingAck)
            {
                Peer.LeaveReason = DestroyReason::SessionTerminated;
                FBytes Out;
                FTerminateSession().Write(Out);
                Peer.Link.Send(std::move(Out), kCoreFlags, Now);
            }
            Peer.Link.Close(Now);
        }
        --Depth;
        Reap();
    }

    void FServer::PeerClosed(FPeer& Peer, ECloseReason Reason)
    {
        const FPeer::EState Was = Peer.State;
        Peer.State = FPeer::EState::Gone;
        if (Was == FPeer::EState::Player)
        {
            Players.erase(Peer.Dpnid);
            ++NameTableVersion;
            uint32_t DestroyReasonCode = Peer.LeaveReason;
            if (DestroyReasonCode == DestroyReason::Normal && Reason != ECloseReason::Graceful)
                DestroyReasonCode = DestroyReason::ConnectionLost;
            Events.ServerDestroyPlayer(Peer.Dpnid, Peer.Context, DestroyReasonCode);
        }
        else if (Was == FPeer::EState::AwaitingAck)
        {
            Events.ServerIndicatedConnectAborted(Peer.Context);
        }
    }

    void FServer::Reap()
    {
        if (Depth != 0)
            return;
        for (auto It = Peers.begin(); It != Peers.end();)
        {
            if (It->second->Link.IsClosed())
            {
                if (It->second->State != FPeer::EState::Gone)
                    PeerClosed(*It->second, It->second->Link.GetCloseReason());
                It = Peers.erase(It);
            }
            else
            {
                ++It;
            }
        }
        if (bClosing && Peers.empty())
            FinishClose();
    }

    void FServer::FinishClose()
    {
        bClosing = false;
        bHosting = false;
        const uint32_t Local = LocalDpnid;
        LocalDpnid = 0;
        Events.ServerDestroyPlayer(Local, nullptr, DestroyReason::SessionTerminated);
    }

    bool FServer::GetPlayerAddress(uint32_t Dpnid, FAddress& Out) const
    {
        const auto It = Players.find(Dpnid);
        if (It == Players.end())
            return false;
        Out = It->second->Address;
        return true;
    }

    const FLinkStats* FServer::GetLinkStats(uint32_t Dpnid) const
    {
        const auto It = Players.find(Dpnid);
        return It == Players.end() ? nullptr : &It->second->Link.GetStats();
    }

    bool FServer::GetSendQueueInfo(uint32_t Dpnid, uint32_t& Messages, uint32_t& Bytes) const
    {
        const auto It = Players.find(Dpnid);
        if (It == Players.end())
            return false;
        It->second->Link.GetQueueInfo(Messages, Bytes);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // FClient

    FClient::FClient(ITransport& InTransport, IClientEvents& InEvents, const FLinkConfig& InConfig)
        : Transport(InTransport), Events(InEvents), Config(InConfig)
    {
    }

    void FClient::EnumHosts(const FAddress& To, const FGuid* Application, const FBytes& UserData, uint32_t InNow)
    {
        Now = InNow;
        FEnumQuery Query;
        Query.EnumPayload = NextEnumPayload++;
        Query.bHasApplication = Application != nullptr;
        if (Application != nullptr)
            Query.Application = *Application;
        Query.UserData = UserData;
        EnumSent[Query.EnumPayload] = Now;
        if (EnumSent.size() > 64)
            EnumSent.erase(EnumSent.begin());
        FBytes Out;
        Query.Write(Out);
        Transport.SendTo(To, Out.data(), Out.size());
    }

    bool FClient::Connect(const FAddress& InHost, const FConnectRequest& InRequest, uint32_t SessId, uint32_t InNow)
    {
        if (State != EState::Idle || SessId == 0)
            return false;
        Now = InNow;
        Host = InHost;
        Request = InRequest;
        State = EState::Linking;
        Link = std::make_unique<FLink>(static_cast<ILinkEvents&>(*this), Config);
        Link->Connect(SessId, Now);
        return true;
    }

    bool FClient::Send(const uint8_t* Data, size_t Size, uint32_t Flags, void* UserContext, uint64_t* OutHandle, uint32_t InNow)
    {
        Now = InNow;
        if (State != EState::Connected || Size == 0)
            return false;
        const uint64_t Id = Link->PeekNextMessageId();
        FSendRecord& Record = Sends[Id];
        Record.Handle = NextHandle++;
        Record.UserContext = UserContext;
        Record.Start = Now;
        Record.bNotify = (Flags & SendFlags::NoComplete) == 0;
        const uint64_t Handle = Record.Handle;
        if (Link->Send(FBytes(Data, Data + Size), LinkFlags(Flags), Now) == 0)
        {
            Sends.erase(Id);
            return false;
        }
        if (OutHandle != nullptr)
            *OutHandle = Handle;
        return true;
    }

    void FClient::Close(uint32_t InNow)
    {
        Now = InNow;
        if (Link)
            Link->Close(Now);
    }

    void FClient::OnDatagram(const FAddress& From, const uint8_t* Data, size_t Size, uint32_t InNow)
    {
        Now = InNow;
        if (Size == 0)
            return;
        if (Data[0] == 0)
        {
            FEnumResponse Response;
            if (Size >= 2 && Data[1] == kEnumResponseCommand && FEnumResponse::Read(Data, Size, Response))
            {
                const auto It = EnumSent.find(Response.EnumPayload);
                const uint32_t Rtt = It != EnumSent.end() ? Now - It->second : 0;
                Events.ClientEnumResponse(From, Response, Rtt);
            }
            return;
        }
        if (Link && From == Host)
            Link->OnDatagram(Data, Size, Now);
    }

    void FClient::Tick(uint32_t InNow)
    {
        Now = InNow;
        if (Link)
            Link->Tick(Now);
    }

    void FClient::LinkTransmit(const uint8_t* Data, size_t Size) { Transport.SendTo(Host, Data, Size); }

    void FClient::LinkEstablished()
    {
        if (State != EState::Linking)
            return;
        // 3.1.5.1 step 2: the player connect message.
        FPlayerConnectInfo Info;
        Info.Flags = ObjectType::Client;
        Info.DnetVersion = Request.DnetVersion;
        Info.Name = Request.Name;
        Info.Data = Request.PlayerData;
        Info.bHasPassword = Request.bHasPassword;
        Info.Password = Request.Password;
        Info.ConnectData = Request.ConnectData;
        Info.Instance = Request.Instance;
        Info.Application = Request.Application;
        FBytes Out;
        Info.Write(Out);
        State = EState::Joining;
        Link->Send(std::move(Out), kCoreFlags, Now);
    }

    void FClient::LinkReceive(FBytes&& Message, uint8_t UserFlags)
    {
        if ((UserFlags & MsgFlags::User1) != 0)
        {
            switch (CoreType(Message))
            {
            case CoreMsg::SendConnectInfo:
                if (State == EState::Joining)
                {
                    if (!FSendConnectInfo::Read(Message.data(), Message.size(), Session))
                    {
                        Link->Close(Now);
                        break;
                    }
                    State = EState::Connected;
                    Link->Send(CoreMessage(CoreMsg::AckConnectInfo), kCoreFlags, Now);
                    Events.ClientConnectComplete(Result::Ok, Session.Reply);
                }
                break;
            case CoreMsg::ConnectFailed:
                if (State == EState::Joining)
                {
                    FConnectFailed Failed;
                    if (!FConnectFailed::Read(Message.data(), Message.size(), Failed))
                        Failed.ResultCode = Result::Generic;
                    State = EState::Closed;
                    Events.ClientConnectComplete(Failed.ResultCode, Failed.Reply);
                    Link->Close(Now);
                }
                break;
            case CoreMsg::TerminateSession:
                if (State == EState::Connected)
                {
                    FTerminateSession Terminate;
                    FTerminateSession::Read(Message.data(), Message.size(), Terminate);
                    bTerminatedByHost = true;
                    TerminateData = Terminate.Data;
                    Link->Close(Now);
                }
                break;
            case CoreMsg::ReqProcessCompletion:
                if (State == EState::Connected && Message.size() >= 8)
                {
                    FBytes Reply = CoreMessage(CoreMsg::ProcessCompletion);
                    Reply.insert(Reply.end(), Message.begin() + 4, Message.begin() + 8);
                    Events.ClientReceive(FBytes(Message.begin() + 8, Message.end()));
                    Link->Send(std::move(Reply), kCoreFlags, Now);
                }
                break;
            default:
                break;
            }
            return;
        }
        if ((UserFlags & MsgFlags::User2) == 0 && State == EState::Connected)
            Events.ClientReceive(std::move(Message));
    }

    void FClient::LinkSendComplete(uint64_t Id, bool bOk)
    {
        const auto It = Sends.find(Id);
        if (It == Sends.end())
            return;
        const FSendRecord Record = It->second;
        Sends.erase(It);
        if (Record.bNotify)
            Events.ClientSendComplete(Record.Handle, Record.UserContext, bOk, Now - Record.Start);
    }

    void FClient::LinkClosed(ECloseReason Reason)
    {
        const EState Was = State;
        State = EState::Closed;
        if (Was == EState::Linking || Was == EState::Joining)
            Events.ClientConnectComplete(Result::TimedOut, FBytes());
        else if (Was == EState::Connected)
            Events.ClientTerminated(bTerminatedByHost, TerminateData, Reason);
    }
}
