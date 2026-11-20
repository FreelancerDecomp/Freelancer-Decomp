// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation (port-only, not part of retail).
//
// FLink is one [MC-DPL8R] connection: the CONNECT/CONNECTED handshake, sequenced data frames
// with acknowledgements, SACK and send masks, retries, coalescing, large-message reassembly,
// KeepAlives, and graceful (END_STREAM) and hard (HARD_DISCONNECT) disconnects.
//
// It has no clock, socket or thread of its own. The owner passes in every datagram from the
// partner's address (OnDatagram), calls Tick regularly (every 10-20 ms), and gets the
// datagrams to transmit and the events through ILinkEvents. Times are milliseconds from any
// wrapping 32-bit clock. Events are delivered at the end of the public call that caused them,
// when the link is consistent again, so a handler may call Send, Close or HardClose. The owner
// must not destroy the link from inside a handler.
//
// Packet signing is never requested and never accepted: Freelancer's sessions do not use it.

#pragma once

#include "dpnet_wire.h"

#include <array>
#include <deque>
#include <memory>

namespace DPNet
{
    // True once Now has reached Due on a wrapping millisecond clock.
    inline bool TimeReached(uint32_t Now, uint32_t Due) { return (int32_t)(Now - Due) >= 0; }

    struct FLinkConfig
    {
        uint32_t ProtocolVersion = kProtocolVersion;
        size_t MaxDatagram = 1472;           // the largest DFRAME sent (Ethernet MTU minus IP/UDP)
        size_t MaxMessage = 4 * 1024 * 1024; // the largest message accepted from the partner
        bool bCoalesce = true;               // combine small messages when the partner supports it

        // [MC-DPL8R] 3.1.2: the recommended timer values.
        uint32_t ConnectRetryMs = 200;      // doubles per retry...
        uint32_t ConnectRetryCapMs = 5000;  // ...up to this
        uint32_t ConnectRetries = 14;
        uint32_t DelayedAckMs = 100;
        uint32_t DelayedAckShortMs = 20;    // out-of-order or duplicate frames
        uint32_t DelayedSendMaskMs = 40;
        uint32_t RetryCapMs = 5000;
        uint32_t MaxRetries = 10;
        uint32_t KeepAliveMs = 25000;
        uint32_t InitialRttMs = 200;
        uint32_t HardDisconnects = 3;
        // After our END_STREAM is acknowledged, how long to wait for the partner's own.
        uint32_t CloseLingerMs = 10000;
    };

    struct FLinkStats
    {
        uint32_t RttMs = 0;
        uint64_t FramesSent = 0;
        uint64_t FramesRetried = 0;
        uint64_t FramesReceived = 0;
        uint64_t FramesDropped = 0;   // unreliable frames given up on
        uint64_t BytesSent = 0;
        uint64_t BytesReceived = 0;
        uint64_t MessagesSent = 0;
        uint64_t MessagesReceived = 0;
        uint64_t MessagesTimedOut = 0;
    };

    enum class ELinkState
    {
        Idle,
        Connecting,   // outbound: CONNECT sent
        Accepting,    // inbound: CONNECTED sent, waiting for the connector's CONNECTED
        Established,
        HardClosing,  // our HARD_DISCONNECTs are going out
        Closed,
    };

    enum class ECloseReason
    {
        None,
        Graceful,             // both END_STREAMs exchanged
        ConnectFailed,        // the handshake ran out of retries, or was abandoned
        TimedOut,             // a reliable frame ran out of retries
        HardDisconnectLocal,  // HardClose
        HardDisconnectRemote, // the partner sent HARD_DISCONNECT
        ProtocolError,        // the partner broke a rule we cannot recover from
    };

    // Flags of a sent or received message: the Command bits that travel per message.
    namespace MsgFlags
    {
        constexpr uint8_t Reliable = Command::Reliable;
        constexpr uint8_t Sequential = Command::Sequential;
        constexpr uint8_t User1 = Command::User1;
        constexpr uint8_t User2 = Command::User2;
        constexpr uint8_t Mask = Reliable | Sequential | User1 | User2;   // the bits that go on the wire
        // Local only (DPNSEND_COALESCE): the message may wait for the next Tick and share a
        // frame with other such messages. Without it, a message goes out at once, alone.
        constexpr uint8_t Coalesce = 0x01;
    }

    class ILinkEvents
    {
    public:
        virtual ~ILinkEvents() = default;
        // A datagram to transmit to the partner. Called at once, not deferred.
        virtual void LinkTransmit(const uint8_t* Data, size_t Size) = 0;
        virtual void LinkEstablished() {}
        // A complete message. UserFlags holds MsgFlags::User1/User2 as sent.
        virtual void LinkReceive(FBytes&& Message, uint8_t UserFlags) = 0;
        // A reliable message was acknowledged (bOk) or abandoned; an unreliable one was sent.
        virtual void LinkSendComplete(uint64_t /*Id*/, bool /*bOk*/) {}
        // The partner sent END_STREAM; the link closes once our own is acknowledged.
        virtual void LinkRemoteClosing() {}
        virtual void LinkClosed(ECloseReason Reason) = 0;
    };

    class FLink
    {
    public:
        FLink(ILinkEvents& InEvents, const FLinkConfig& InConfig);
        FLink(const FLink&) = delete;
        FLink& operator=(const FLink&) = delete;

        // Outbound: send CONNECT (SessId must not be zero).
        void Connect(uint32_t InSessId, uint32_t Now);
        // Inbound: answer a CONNECT that arrived from a new address. False (and the link stays
        // Idle) when the frame is not an acceptable CONNECT.
        bool Accept(const FConnectFrame& Connect, uint32_t Now);

        void OnDatagram(const uint8_t* Data, size_t Size, uint32_t Now);
        void Tick(uint32_t Now);

        // Queues a message. Flags: MsgFlags. Returns its id for LinkSendComplete, or 0 when
        // the link no longer sends (closing, closed) or the message is empty.
        uint64_t Send(FBytes Message, uint8_t Flags, uint32_t Now);
        // Graceful: END_STREAM after everything queued has gone out.
        void Close(uint32_t Now);
        // Abortive: drop everything, send HARD_DISCONNECTs.
        void HardClose(uint32_t Now);

        ELinkState GetState() const { return State; }
        bool IsClosed() const { return State == ELinkState::Closed; }
        bool IsClosing() const { return bCloseRequested || State == ELinkState::HardClosing; }
        ECloseReason GetCloseReason() const { return CloseReason; }
        uint32_t GetSessId() const { return SessId; }
        uint32_t GetRemoteVersion() const { return RemoteVersion; }
        const FLinkStats& GetStats() const { return Stats; }
        // The id the next Send will return.
        uint64_t PeekNextMessageId() const { return NextMessageId; }
        // Messages (and their bytes) queued or sent but not yet acknowledged.
        void GetQueueInfo(uint32_t& Messages, uint32_t& Bytes) const;

    private:
        struct FOutMessage
        {
            uint64_t Id = 0;
            FBytes Data;
            uint8_t Flags = 0;
            size_t Framed = 0;              // bytes already put into frames
            uint32_t FramesOutstanding = 0; // reliable frames carrying it, not yet acknowledged
            bool bDone = false;
        };
        struct FPart
        {
            std::shared_ptr<FOutMessage> Msg;
            size_t Offset = 0;
            size_t Size = 0;
        };
        struct FOutFrame
        {
            uint8_t Seq = 0;
            uint8_t Command = 0;   // without Poll
            uint8_t Control = 0;   // KeepAlive, Coalesce, EndStream
            std::vector<FPart> Parts;
            uint32_t Retries = 0;
            uint32_t FirstSend = 0;
            uint32_t LastSend = 0;
            uint32_t NextRetry = 0;
            bool bSacked = false;      // the partner holds it out of order: no early retry
            bool bEarlyRetry = false;  // NextRetry was brought forward by a SACK gap
            bool bEarlyRetried = false;
        };
        struct FInFrame
        {
            bool bHave = false;
            bool bSkipped = false;    // dropped by the partner's send mask
            bool bDelivered = false;  // non-sequential, already indicated
            FDataHeader Header;
            FBytes Payload;
        };
        struct FEvent
        {
            enum class EKind
            {
                Established,
                Receive,
                SendComplete,
                RemoteClosing,
                Closed,
            };
            EKind Kind = EKind::Receive;
            FBytes Data;
            uint8_t Flags = 0;
            uint64_t Id = 0;
            bool bOk = false;
            ECloseReason Reason = ECloseReason::None;
        };

        bool Reliable(const FOutFrame& F) const { return (F.Command & Command::Reliable) != 0; }
        bool HasDroppedOutstanding() const;
        bool PartnerCoalesces() const { return RemoteVersion >= kProtocolCoalesce && Config.ProtocolVersion >= kProtocolCoalesce; }

        void SendConnectFrame(uint8_t Op, bool bPoll, uint8_t RspId, uint32_t Now);
        void SendHardDisconnect(uint32_t Now);
        void SendSack(uint32_t Now);
        void Establish(uint32_t Now);

        void OnCommandFrame(const FCommandFrame& Frame, uint32_t Now);
        void OnDataFrame(const uint8_t* Data, size_t Size, uint32_t Now);
        void ProcessAcks(uint8_t NRcv, uint64_t SackMask, uint32_t Now);
        void ProcessSendMask(uint8_t Seq, uint64_t SendMask, uint32_t Now);
        void DrainReceive(uint32_t Now);
        void Deliver(FInFrame& Frame, uint32_t Now);
        void DeliverWhole(const FInFrame& Frame);
        void DeliverMessage(FBytes&& Message, uint8_t UserFlags);
        void ScheduleAck(uint32_t Now, uint32_t Delay);

        void Pump(uint32_t Now, bool bTick);
        bool BuildFrame(FOutFrame& Frame);
        void Transmit(FOutFrame& Frame, uint32_t Now, bool bRetry, bool bPoll);
        void FrameAcknowledged(FOutFrame& Frame, uint32_t Now, bool bSampleRtt);
        void CompleteMessage(FOutMessage& Msg, bool bOk);
        uint32_t RetryTimeout(uint32_t Retries) const;
        uint64_t ReceiveSackMask() const;
        uint64_t SendMaskFor(uint8_t Seq) const;
        void CheckCloseDone(uint32_t Now);
        void FailAllSends();
        void Finish(ECloseReason Reason, uint32_t Now);
        void Flush();

        ILinkEvents& Events;
        FLinkConfig Config;
        ELinkState State = ELinkState::Idle;
        ECloseReason CloseReason = ECloseReason::None;
        FLinkStats Stats;

        uint32_t SessId = 0;
        uint32_t RemoteVersion = 0;
        bool bConnector = false;

        // Handshake.
        uint8_t NextCommandMsgId = 0;
        uint8_t RemoteConnectMsgId = 0;
        uint32_t HandshakeRetries = 0;
        uint32_t HandshakeInterval = 0;
        uint32_t HandshakeDue = 0;
        uint32_t HandshakeSent = 0;

        // Sending.
        std::deque<std::shared_ptr<FOutMessage>> Queue;
        std::deque<FOutFrame> InFlight;
        uint64_t NextMessageId = 1;
        uint8_t NextSend = 0;
        uint8_t SendBase = 0;   // the partner's last bNRcv: oldest sequence not yet acknowledged
        std::array<bool, 256> Dropped{};
        uint32_t Cwnd = 2;
        uint32_t RttMs = 0;
        bool bHaveRtt = false;
        bool bKeepAlivePending = false;
        uint32_t LastKeepAlive = 0;
        uint32_t LastMaskSack = 0;

        // Receiving.
        std::array<FInFrame, 64> Slots;
        uint8_t NextReceive = 0;
        FBytes Partial;
        uint8_t PartialFlags = 0;
        bool bPartial = false;
        bool bDiscardUntilNew = false;
        bool bLastReceiveRetry = false;
        uint32_t LastReceive = 0;

        // Acknowledgements owed to the partner.
        bool bAckPending = false;
        uint32_t AckDue = 0;
        bool bSendMaskPending = false;
        uint32_t SendMaskDue = 0;

        // Closing.
        bool bCloseRequested = false;
        bool bEndStreamSent = false;
        bool bEndStreamAcked = false;
        uint8_t EndStreamSeq = 0;
        bool bRemoteEndStream = false;
        bool bRemoteClosingRaised = false;
        uint32_t EndStreamAckedAt = 0;
        uint32_t HardSent = 0;
        uint32_t HardDue = 0;

        std::vector<FEvent> Pending;
        bool bFlushing = false;
        uint32_t LastNow = 0;   // the time of the current public call, for Flush
    };
}
