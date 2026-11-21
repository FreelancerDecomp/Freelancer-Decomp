// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation -- PORT(dpnet), not part of retail.
//
// The [MC-DPL8CS] session layer, client/server mode only (Freelancer never uses peer-to-peer):
// FServer is what IDirectPlay8Server does for FLServer, FClient what IDirectPlay8Client does
// for the game. Both answer or send [MC-DPLHP] host enumeration.
//
// Like FLink, neither has a socket, clock or thread. The owner feeds datagrams in with the
// sender's address, calls Tick every 10-20 ms, and transmits through ITransport. Events are
// delivered from inside OnDatagram / Tick / the call that caused them; a handler may call back
// into the same object (SendTo, DestroyClient, Close). One thread at a time.

#pragma once

#include "dpnet_link.h"

#include <array>
#include <map>
#include <memory>
#include <string>

namespace DPNet
{
    // A UDP endpoint. IPv4 addresses use the first four bytes of Ip, in network order.
    struct FAddress
    {
        uint8_t Family = 4;   // 4 or 6
        std::array<uint8_t, 16> Ip{};
        uint16_t Port = 0;    // host byte order

        static FAddress Ipv4(uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint16_t Port);
        std::string HostText() const;
        // The DN_ADDRESSING_URL (2.2.8) DirectPlay puts in the name table for this address.
        std::string Url() const;
        bool operator==(const FAddress& Other) const;
        bool operator<(const FAddress& Other) const;
    };

    class ITransport
    {
    public:
        virtual ~ITransport() = default;
        virtual void SendTo(const FAddress& To, const uint8_t* Data, size_t Size) = 0;
    };

    // DPNSEND_* flags of IDirectPlay8Server::SendTo / IDirectPlay8Client::Send.
    namespace SendFlags
    {
        constexpr uint32_t NoCopy = 0x0001;
        constexpr uint32_t NoComplete = 0x0002;
        constexpr uint32_t CompleteOnProcess = 0x0004;   // sent as guaranteed; completes on delivery
        constexpr uint32_t Guaranteed = 0x0008;
        constexpr uint32_t NonSequential = 0x0010;
        constexpr uint32_t NoLoopback = 0x0020;
        constexpr uint32_t PriorityLow = 0x0040;         // accepted, not prioritised
        constexpr uint32_t PriorityHigh = 0x0080;        // accepted, not prioritised
        constexpr uint32_t Coalesce = 0x0100;            // may wait for the next Tick and share a frame
    }

    // DPN_APPLICATION_DESC, the parts that go on the wire.
    struct FAppDesc
    {
        uint32_t Flags = SessionFlags::ClientServer;
        uint32_t MaxPlayers = 0;   // 0: no limit. Counts the server's own player, as DirectPlay does.
        std::u16string SessionName;
        std::u16string Password;   // checked when Flags has SessionFlags::RequirePassword
        FGuid Application;
        FGuid Instance;            // Host makes one up when this is zero
        FBytes ApplicationReserved;
    };

    class IServerEvents
    {
    public:
        virtual ~IServerEvents() = default;
        // DPN_MSGID_ENUM_HOSTS_QUERY. Response is the reply's application data. Return false
        // to leave the query unanswered.
        virtual bool ServerEnumQuery(const FAddress& /*From*/, const FBytes& /*QueryData*/, FBytes& /*Response*/) { return true; }
        // DPN_MSGID_INDICATE_CONNECT. Set PlayerContext; return false to refuse the player
        // (DPNERR_HOSTREJECTEDCONNECTION). Reply goes back to the client either way.
        virtual bool ServerIndicateConnect(const FAddress& /*From*/, const FPlayerConnectInfo& /*Info*/, void*& /*PlayerContext*/,
                                           FBytes& /*Reply*/)
        {
            return true;
        }
        // DPN_MSGID_INDICATED_CONNECT_ABORTED: an indicated player left before it was created.
        virtual void ServerIndicatedConnectAborted(void* /*PlayerContext*/) {}
        // DPN_MSGID_CREATE_PLAYER. The server's own player has a null context.
        virtual void ServerCreatePlayer(uint32_t /*Dpnid*/, void* /*PlayerContext*/) {}
        // DPN_MSGID_DESTROY_PLAYER. Reason: DestroyReason.
        virtual void ServerDestroyPlayer(uint32_t /*Dpnid*/, void* /*PlayerContext*/, uint32_t /*Reason*/) {}
        // DPN_MSGID_RECEIVE.
        virtual void ServerReceive(uint32_t Dpnid, void* PlayerContext, FBytes&& Data) = 0;
        // DPN_MSGID_SEND_COMPLETE (not for DPNSEND_NOCOMPLETE sends).
        virtual void ServerSendComplete(uint64_t /*Handle*/, void* /*UserContext*/, bool /*bOk*/, uint32_t /*SendTimeMs*/) {}
    };

    class FServer
    {
    public:
        FServer(ITransport& InTransport, IServerEvents& InEvents, const FLinkConfig& InConfig = FLinkConfig());
        ~FServer();
        FServer(const FServer&) = delete;
        FServer& operator=(const FServer&) = delete;

        // Starts the session and creates the server's own player. Seed feeds the instance GUID
        // when Desc.Instance is zero.
        bool Host(const FAppDesc& Desc, uint32_t Now, uint64_t Seed);
        // IDirectPlay8Server::SetApplicationDesc: name, players, password, reserved data.
        // The application and instance GUIDs of a running session do not change.
        void SetApplicationDesc(const FAppDesc& Desc);
        const FAppDesc& GetApplicationDesc() const { return Desc; }

        void OnDatagram(const FAddress& From, const uint8_t* Data, size_t Size, uint32_t Now);
        void Tick(uint32_t Now);

        // DPNSEND_* Flags. False for a player that does not exist (any more).
        bool SendTo(uint32_t Dpnid, const uint8_t* Data, size_t Size, uint32_t Flags, void* UserContext,
                     uint64_t* OutHandle, uint32_t Now);
        // Sends DN_TERMINATE_SESSION with Data and disconnects the player.
        bool DestroyClient(uint32_t Dpnid, const uint8_t* Data, size_t Size, uint32_t Now);
        // Ends the session: every player is told and disconnected, then destroyed (as the links
        // close, on later Ticks), the server's own player last. IsIdle() once all are gone.
        void Close(uint32_t Now);

        bool IsHosting() const { return bHosting; }
        bool IsIdle() const { return !bHosting && Peers.empty(); }
        uint32_t GetLocalDpnid() const { return LocalDpnid; }
        // Players in the session, the server's own included, plus the ones still joining.
        uint32_t GetPlayerCount() const;
        bool GetPlayerAddress(uint32_t Dpnid, FAddress& Out) const;
        const FLinkStats* GetLinkStats(uint32_t Dpnid) const;
        bool GetSendQueueInfo(uint32_t Dpnid, uint32_t& Messages, uint32_t& Bytes) const;

        // A connection that has not become a player within TimeoutMs is dropped, and at most
        // MaxJoining connections may be on their way in at once (further CONNECTs are ignored).
        void SetJoinLimits(uint32_t TimeoutMs, uint32_t InMaxJoining)
        {
            JoinTimeoutMs = TimeoutMs;
            MaxJoining = InMaxJoining;
        }

    private:
        struct FPeer;
        friend struct FPeer;

        void HandleEnumQuery(const FAddress& From, const uint8_t* Data, size_t Size);
        void HandleConnectInfo(FPeer& Peer, const FBytes& Message);
        void PeerClosed(FPeer& Peer, ECloseReason Reason);
        void Reap();
        void FinishClose();

        ITransport& Transport;
        IServerEvents& Events;
        FLinkConfig Config;
        FAppDesc Desc;
        bool bHosting = false;
        bool bClosing = false;
        uint32_t Now = 0;
        uint32_t LocalDpnid = 0;
        uint32_t NameTableVersion = 0;
        uint32_t NextIndex = 0;
        uint64_t NextHandle = 1;
        uint32_t JoinTimeoutMs = 30000;
        uint32_t MaxJoining = 64;
        int Depth = 0;   // nesting of public calls; peers are reaped at the outermost
        std::map<FAddress, std::unique_ptr<FPeer>> Peers;
        std::map<uint32_t, FPeer*> Players;
    };

    // ---------------------------------------------------------------------------------------

    struct FConnectRequest
    {
        FGuid Application;
        FGuid Instance;   // zero: any instance
        std::u16string Name;
        std::u16string Password;
        bool bHasPassword = false;
        FBytes PlayerData;
        FBytes ConnectData;
        uint32_t DnetVersion = 8;
    };

    class IClientEvents
    {
    public:
        virtual ~IClientEvents() = default;
        virtual void ClientEnumResponse(const FAddress& /*From*/, const FEnumResponse& /*Response*/, uint32_t /*RoundTripMs*/) {}
        // DPN_MSGID_CONNECT_COMPLETE. Result 0 on success, else the DPNERR_ code; Reply is the
        // server application's reply data.
        virtual void ClientConnectComplete(uint32_t /*Result*/, const FBytes& /*Reply*/) {}
        virtual void ClientReceive(FBytes&& Data) = 0;
        virtual void ClientSendComplete(uint64_t /*Handle*/, void* /*UserContext*/, bool /*bOk*/, uint32_t /*SendTimeMs*/) {}
        // DPN_MSGID_TERMINATE_SESSION: bByHost when the server sent DN_TERMINATE_SESSION (with
        // its Data), otherwise the connection was lost or closed.
        virtual void ClientTerminated(bool /*bByHost*/, const FBytes& /*Data*/, ECloseReason /*Reason*/) {}
    };

    class FClient : private ILinkEvents
    {
    public:
        enum class EState
        {
            Idle,
            Linking,    // [MC-DPL8R] handshake
            Joining,    // player connect message sent
            Connected,
            Closed,
        };

        FClient(ITransport& InTransport, IClientEvents& InEvents, const FLinkConfig& InConfig = FLinkConfig());

        // Sends one EnumQuery; repeat it as long as you want answers.
        void EnumHosts(const FAddress& To, const FGuid* Application, const FBytes& UserData, uint32_t Now);
        bool Connect(const FAddress& Host, const FConnectRequest& Request, uint32_t SessId, uint32_t Now);
        bool Send(const uint8_t* Data, size_t Size, uint32_t Flags, void* UserContext, uint64_t* OutHandle, uint32_t Now);
        void Close(uint32_t Now);

        void OnDatagram(const FAddress& From, const uint8_t* Data, size_t Size, uint32_t Now);
        void Tick(uint32_t Now);

        EState GetState() const { return State; }
        const FSendConnectInfo& GetSessionInfo() const { return Session; }
        const FLinkStats& GetLinkStats() const { return Link->GetStats(); }

    private:
        void LinkTransmit(const uint8_t* Data, size_t Size) override;
        void LinkEstablished() override;
        void LinkReceive(FBytes&& Message, uint8_t UserFlags) override;
        void LinkSendComplete(uint64_t Id, bool bOk) override;
        void LinkClosed(ECloseReason Reason) override;

        struct FSendRecord
        {
            uint64_t Handle = 0;
            void* UserContext = nullptr;
            uint32_t Start = 0;
            bool bNotify = true;
        };

        ITransport& Transport;
        IClientEvents& Events;
        FLinkConfig Config;
        std::unique_ptr<FLink> Link;
        EState State = EState::Idle;
        FAddress Host;
        FConnectRequest Request;
        FSendConnectInfo Session;
        bool bTerminatedByHost = false;
        FBytes TerminateData;
        uint32_t Now = 0;
        uint64_t NextHandle = 1;
        uint16_t NextEnumPayload = 1;
        std::map<uint16_t, uint32_t> EnumSent;
        std::map<uint64_t, FSendRecord> Sends;
    };
}
