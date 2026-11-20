// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation (port-only, not part of retail).
// One [MC-DPL8R] connection; see dpnet_link.h. Section numbers refer to [MC-DPL8R].

#include "dpnet_link.h"

#include <algorithm>

namespace DPNet
{
    FLink::FLink(ILinkEvents& InEvents, const FLinkConfig& InConfig) : Events(InEvents), Config(InConfig)
    {
        RttMs = Config.InitialRttMs;
        Stats.RttMs = RttMs;
    }

    // ---------------------------------------------------------------------------------------
    // Handshake (3.1.4.2, 3.1.5.1.1, 3.1.5.1.2)

    void FLink::Connect(uint32_t InSessId, uint32_t Now)
    {
        LastNow = Now;
        if (State != ELinkState::Idle)
            return;
        bConnector = true;
        SessId = InSessId;
        State = ELinkState::Connecting;
        HandshakeInterval = Config.ConnectRetryMs;
        SendConnectFrame(ExtOp::Connect, true, 0, Now);
        HandshakeDue = Now + HandshakeInterval;
        Flush();
    }

    bool FLink::Accept(const FConnectFrame& Connect, uint32_t Now)
    {
        LastNow = Now;
        if (State != ELinkState::Idle || Connect.Op != ExtOp::Connect)
            return false;
        if ((Connect.Version & kProtocolMajorMask) != kProtocolMajor)
            return false;
        bConnector = false;
        SessId = Connect.SessId;
        RemoteVersion = Connect.Version;
        RemoteConnectMsgId = Connect.MsgId;
        State = ELinkState::Accepting;
        HandshakeInterval = Config.ConnectRetryMs;
        SendConnectFrame(ExtOp::Connected, true, Connect.MsgId, Now);
        HandshakeDue = Now + HandshakeInterval;
        Flush();
        return true;
    }

    void FLink::SendConnectFrame(uint8_t Op, bool bPoll, uint8_t RspId, uint32_t Now)
    {
        FConnectFrame F;
        F.Command = (uint8_t)(Command::CFrame | (bPoll ? Command::Poll : 0));
        F.Op = Op;
        F.MsgId = NextCommandMsgId++;
        F.RspId = RspId;
        F.Version = Config.ProtocolVersion;
        F.SessId = SessId;
        F.Timestamp = Now;
        FBytes Out;
        F.Write(Out);
        HandshakeSent = Now;
        Events.LinkTransmit(Out.data(), Out.size());
    }

    void FLink::SendHardDisconnect(uint32_t Now)
    {
        FConnectFrame F;
        F.Command = Command::CFrame;
        F.Op = ExtOp::HardDisconnect;
        F.MsgId = NextCommandMsgId++;
        F.Version = Config.ProtocolVersion;
        F.SessId = SessId;
        F.Timestamp = Now;
        FBytes Out;
        F.Write(Out);
        ++HardSent;
        Events.LinkTransmit(Out.data(), Out.size());
    }

    void FLink::Establish(uint32_t Now)
    {
        if (!bHaveRtt)
        {
            RttMs = std::max<uint32_t>(1, Now - HandshakeSent);
            bHaveRtt = true;
            Stats.RttMs = RttMs;
        }
        State = ELinkState::Established;
        LastReceive = Now;
        LastKeepAlive = Now;
        // Both partners open with a KeepAlive (4.1): sequence 0, and a first RTT sample.
        bKeepAlivePending = true;
        FEvent Ev;
        Ev.Kind = FEvent::EKind::Established;
        Pending.push_back(std::move(Ev));
        Pump(Now, true);
    }

    // ---------------------------------------------------------------------------------------
    // Incoming datagrams (3.1.5)

    void FLink::OnDatagram(const uint8_t* Data, size_t Size, uint32_t Now)
    {
        LastNow = Now;
        if (State == ELinkState::Idle || State == ELinkState::Closed)
            return;
        if (IsDataFrame(Data, Size))
        {
            OnDataFrame(Data, Size, Now);
        }
        else
        {
            FCommandFrame Frame;
            if (DecodeCommandFrame(Data, Size, Frame))
                OnCommandFrame(Frame, Now);
        }
        if (State == ELinkState::Established)
        {
            Pump(Now, false);
            if (bAckPending && TimeReached(Now, AckDue))
                SendSack(Now);
        }
        Flush();
    }

    void FLink::OnCommandFrame(const FCommandFrame& Frame, uint32_t Now)
    {
        const FConnectFrame& C = Frame.Connect;
        switch (Frame.Op)
        {
        case ExtOp::Connect:
            // Our CONNECTED was lost: answer the retried CONNECT at once.
            if (State == ELinkState::Accepting && C.SessId == SessId)
            {
                RemoteConnectMsgId = C.MsgId;
                SendConnectFrame(ExtOp::Connected, true, C.MsgId, Now);
            }
            break;

        case ExtOp::Connected:
        {
            if ((C.Version & kProtocolMajorMask) != kProtocolMajor || C.SessId != SessId)
                break;
            const bool bPoll = (C.Command & Command::Poll) != 0;
            if (State == ELinkState::Connecting && bPoll)
            {
                RemoteVersion = C.Version;
                RemoteConnectMsgId = C.MsgId;
                const uint32_t Sent = HandshakeSent;
                SendConnectFrame(ExtOp::Connected, false, C.MsgId, Now);
                HandshakeSent = Sent;
                Establish(Now);
            }
            else if (State == ELinkState::Accepting && !bPoll)
            {
                Establish(Now);
            }
            else if (State == ELinkState::Established && bConnector && bPoll)
            {
                // The listener did not get our CONNECTED: send it again.
                SendConnectFrame(ExtOp::Connected, false, C.MsgId, Now);
            }
            break;
        }

        case ExtOp::HardDisconnect:
            if (C.SessId != SessId)
                break;
            if (State == ELinkState::HardClosing)
            {
                Finish(ECloseReason::HardDisconnectLocal, Now);
            }
            else if (State == ELinkState::Established)
            {
                FailAllSends();
                for (uint32_t i = 0; i < Config.HardDisconnects; ++i)
                    SendHardDisconnect(Now);
                Finish(ECloseReason::HardDisconnectRemote, Now);
            }
            break;

        case ExtOp::Sack:
            if (State != ELinkState::Established)
                break;
            LastReceive = Now;
            ProcessAcks(Frame.Sack.NRcv, Frame.Sack.SackMask, Now);
            if (State != ELinkState::Established)
                break;
            ProcessSendMask(Frame.Sack.NSeq, Frame.Sack.SendMask, Now);
            DrainReceive(Now);
            break;

        default:
            // CONNECTED_SIGNED: we never sign.
            break;
        }
    }

    void FLink::OnDataFrame(const uint8_t* Data, size_t Size, uint32_t Now)
    {
        // 3.1.5.2: only an established connection takes data frames.
        if (State != ELinkState::Established)
            return;
        FDataHeader H;
        const size_t Offset = FDataHeader::Read(Data, Size, H);
        if (Offset == 0)
            return;
        const uint8_t* Payload = Data + Offset;
        const size_t PayloadSize = Size - Offset;

        const bool bKeepAlive = (H.Control & Control::KeepAlive) != 0 && RemoteVersion >= kProtocolCoalesce;
        if (bKeepAlive)
        {
            if (PayloadSize < 4)
                return;
            FReader R(Payload, PayloadSize);
            if (R.U32() != SessId)
                return;
        }
        if ((H.Control & Control::Coalesce) != 0 &&
            (H.Command & (Command::NewMsg | Command::EndMsg)) != (Command::NewMsg | Command::EndMsg))
            return;

        ++Stats.FramesReceived;
        Stats.BytesReceived += Size;
        LastReceive = Now;

        ProcessAcks(H.NRcv, H.SackMask, Now);
        if (State != ELinkState::Established)
            return;
        ProcessSendMask(H.Seq, H.SendMask, Now);

        const uint8_t Distance = (uint8_t)(H.Seq - NextReceive);
        // Before protocol 1.5, KEEPALIVE_OR_CORRELATE asks for a dedicated acknowledgement (2.2.2).
        const bool bPoll = (H.Command & Command::Poll) != 0 ||
                           ((H.Control & Control::KeepAlive) != 0 && RemoteVersion < kProtocolCoalesce);
        bLastReceiveRetry = (H.Control & Control::Retry) != 0;
        FInFrame& Slot = Slots[H.Seq & 63];
        // 3.1.5.2.1: outside the window, a duplicate, or past the partner's END_STREAM.
        if (Distance >= 64 || Slot.bHave || Slot.bSkipped || bRemoteEndStream)
        {
            ScheduleAck(Now, bPoll ? 0 : Config.DelayedAckShortMs);
            DrainReceive(Now);
            return;
        }
        ScheduleAck(Now, bPoll ? 0 : (Distance != 0 ? Config.DelayedAckShortMs : Config.DelayedAckMs));
        Slot = FInFrame();
        Slot.bHave = true;
        Slot.Header = H;
        Slot.Payload.assign(Payload, Payload + PayloadSize);
        // A complete non-sequential message need not wait for the gap before it.
        if (Distance != 0 && (H.Command & Command::Sequential) == 0 &&
            (H.Command & (Command::NewMsg | Command::EndMsg)) == (Command::NewMsg | Command::EndMsg) &&
            (H.Control & Control::EndStream) == 0)
        {
            DeliverWhole(Slot);
            Slot.bDelivered = true;
        }
        DrainReceive(Now);
    }

    // 3.1.5.2.2 and 3.1.5.2.3: bNRcv acknowledges everything before it; the SACK mask the
    // frames after it that arrived out of order.
    void FLink::ProcessAcks(uint8_t NRcv, uint64_t SackMask, uint32_t Now)
    {
        const uint8_t Span = (uint8_t)(NextSend - SendBase);
        const uint8_t Acked = (uint8_t)(NRcv - SendBase);
        if (Acked > Span)
            return;
        // A round-trip sample comes only from the newest frame an acknowledgement covers, and
        // only when no frame it covers was resent or already known to have arrived: otherwise
        // the acknowledgement waited for a retry, or answers an older one that was lost.
        bool bAny = false;
        bool bCleanSample = true;
        while (!InFlight.empty() && (uint8_t)(InFlight.front().Seq - SendBase) < Acked)
        {
            FOutFrame Frame = std::move(InFlight.front());
            InFlight.pop_front();
            if (Frame.Retries != 0 || Frame.bSacked)
                bCleanSample = false;
            const bool bNewest = InFlight.empty() || (uint8_t)(InFlight.front().Seq - SendBase) >= Acked;
            FrameAcknowledged(Frame, Now, bNewest && bCleanSample);
            bAny = true;
        }
        for (uint8_t S = SendBase; S != NRcv; ++S)
            Dropped[S] = false;
        SendBase = NRcv;
        if (bAny)
            Cwnd = std::min<uint32_t>(64, Cwnd + 1);

        if (SackMask != 0)
        {
            for (FOutFrame& F : InFlight)
            {
                const uint8_t Bit = (uint8_t)(F.Seq - NRcv - 1);
                if (Bit < 64 && ((SackMask >> Bit) & 1) != 0)
                    F.bSacked = true;
            }
            // The partner is missing the first frame of the window: retry it soon (3.1.5.2.3).
            // Only once three later frames are in and it has been out for a round trip and a
            // half, or a frame merely overtaken by others would be sent twice.
            uint32_t Later = 0;
            for (uint64_t M = SackMask; M != 0; M &= M - 1)
                ++Later;
            FOutFrame* First = InFlight.empty() ? nullptr : &InFlight.front();
            if (First != nullptr && First->Seq == NRcv && !First->bSacked && Later >= 3 &&
                !First->bEarlyRetried && Now - First->LastSend >= RttMs + RttMs / 2 &&
                (int32_t)(First->NextRetry - (Now + 10)) > 0)
            {
                First->NextRetry = Now + 10;
                First->bEarlyRetry = true;
            }
        }
        CheckCloseDone(Now);
    }

    // 3.1.5.2.4: bit i of the mask is the unreliable frame Seq - 1 - i the partner gave up on.
    void FLink::ProcessSendMask(uint8_t Seq, uint64_t SendMask, uint32_t Now)
    {
        if (SendMask == 0)
            return;
        for (uint32_t i = 0; i < 64; ++i)
        {
            if (((SendMask >> i) & 1) == 0)
                continue;
            const uint8_t S = (uint8_t)(Seq - 1 - i);
            if ((uint8_t)(S - NextReceive) >= 64)
            {
                // We are past that frame already, so the partner has not heard our
                // acknowledgement: send it again, or its window may stay full for good.
                if ((uint8_t)(NextReceive - 1 - S) < 64)
                    ScheduleAck(Now, Config.DelayedAckShortMs);
                continue;
            }
            FInFrame& Slot = Slots[S & 63];
            if (!Slot.bHave)
                Slot.bSkipped = true;
        }
    }

    void FLink::DrainReceive(uint32_t Now)
    {
        while (State == ELinkState::Established && !bRemoteEndStream)
        {
            FInFrame& Slot = Slots[NextReceive & 63];
            if (!Slot.bHave && !Slot.bSkipped)
                break;
            if (Slot.bHave)
            {
                if (!Slot.bDelivered)
                    Deliver(Slot, Now);
                if ((Slot.Header.Control & Control::EndStream) != 0)
                    bRemoteEndStream = true;
            }
            else if (bPartial)
            {
                // A lost unreliable piece of a large message: the message cannot be completed.
                bPartial = false;
                Partial.clear();
                bDiscardUntilNew = true;
            }
            Slot = FInFrame();
            ++NextReceive;
        }
        if (State != ELinkState::Established)
            return;
        if (bRemoteEndStream && !bRemoteClosingRaised)
        {
            // 3.1.4.3: answer with our own END_STREAM once our queue has drained. That starts
            // when the event is dispatched (Flush), after the messages before the END_STREAM,
            // so their handlers can still reply.
            bRemoteClosingRaised = true;
            FEvent Ev;
            Ev.Kind = FEvent::EKind::RemoteClosing;
            Pending.push_back(std::move(Ev));
        }
        CheckCloseDone(Now);
    }

    // A complete frame (single message or coalesced) delivered ahead of the gap before it:
    // the reassembly state belongs to the frames in sequence and is left alone.
    void FLink::DeliverWhole(const FInFrame& Frame)
    {
        const FDataHeader& H = Frame.Header;
        if ((H.Control & Control::KeepAlive) != 0 && RemoteVersion >= kProtocolCoalesce)
            return;
        if ((H.Control & Control::Coalesce) != 0)
        {
            std::vector<FSubPayload> Subs;
            if (!DecodeCoalesced(Frame.Payload.data(), Frame.Payload.size(), Subs))
                return;
            for (const FSubPayload& Sub : Subs)
                if (Sub.Size != 0)
                    DeliverMessage(FBytes(Sub.Data, Sub.Data + Sub.Size), (uint8_t)(Sub.Flags & Command::UserMask));
            return;
        }
        if (!Frame.Payload.empty())
            DeliverMessage(FBytes(Frame.Payload), (uint8_t)(H.Command & Command::UserMask));
    }

    void FLink::Deliver(FInFrame& Frame, uint32_t Now)
    {
        const FDataHeader& H = Frame.Header;
        if ((H.Control & Control::KeepAlive) != 0 && RemoteVersion >= kProtocolCoalesce)
            return;
        if ((H.Control & Control::Coalesce) != 0)
        {
            // 3.1.5.2.5
            std::vector<FSubPayload> Subs;
            if (!DecodeCoalesced(Frame.Payload.data(), Frame.Payload.size(), Subs))
                return;
            bPartial = false;
            Partial.clear();
            bDiscardUntilNew = false;
            for (const FSubPayload& Sub : Subs)
                if (Sub.Size != 0)
                    DeliverMessage(FBytes(Sub.Data, Sub.Data + Sub.Size), (uint8_t)(Sub.Flags & Command::UserMask));
            return;
        }
        // 3.1.5.2.6
        if ((H.Command & Command::NewMsg) != 0)
        {
            Partial.clear();
            bPartial = true;
            bDiscardUntilNew = false;
            PartialFlags = (uint8_t)(H.Command & Command::UserMask);
        }
        else if (bDiscardUntilNew)
        {
            return;
        }
        else if (!bPartial)
        {
            // A continuation without a start: treat it as the start of a message.
            Partial.clear();
            bPartial = true;
            PartialFlags = (uint8_t)(H.Command & Command::UserMask);
        }
        Partial.insert(Partial.end(), Frame.Payload.begin(), Frame.Payload.end());
        if (Partial.size() > Config.MaxMessage)
        {
            Partial.clear();
            bPartial = false;
            FailAllSends();
            for (uint32_t i = 0; i < Config.HardDisconnects; ++i)
                SendHardDisconnect(Now);
            Finish(ECloseReason::ProtocolError, Now);
            return;
        }
        if ((H.Command & Command::EndMsg) != 0)
        {
            bPartial = false;
            if (!Partial.empty())
                DeliverMessage(std::move(Partial), PartialFlags);
            Partial.clear();
        }
    }

    void FLink::DeliverMessage(FBytes&& Message, uint8_t UserFlags)
    {
        ++Stats.MessagesReceived;
        FEvent Ev;
        Ev.Kind = FEvent::EKind::Receive;
        Ev.Data = std::move(Message);
        Ev.Flags = UserFlags;
        Pending.push_back(std::move(Ev));
    }

    void FLink::ScheduleAck(uint32_t Now, uint32_t Delay)
    {
        const uint32_t Due = Now + Delay;
        if (!bAckPending || (int32_t)(Due - AckDue) < 0)
            AckDue = Due;
        bAckPending = true;
    }

    // ---------------------------------------------------------------------------------------
    // Timers (3.1.6)

    void FLink::Tick(uint32_t Now)
    {
        LastNow = Now;
        switch (State)
        {
        case ELinkState::Connecting:
        case ELinkState::Accepting:
            if (TimeReached(Now, HandshakeDue))
            {
                if (HandshakeRetries >= Config.ConnectRetries)
                {
                    Finish(ECloseReason::ConnectFailed, Now);
                    break;
                }
                ++HandshakeRetries;
                HandshakeInterval = std::min(HandshakeInterval * 2, Config.ConnectRetryCapMs);
                if (State == ELinkState::Connecting)
                    SendConnectFrame(ExtOp::Connect, true, 0, Now);
                else
                    SendConnectFrame(ExtOp::Connected, true, RemoteConnectMsgId, Now);
                HandshakeDue = Now + HandshakeInterval;
            }
            break;

        case ELinkState::HardClosing:
            if (TimeReached(Now, HardDue))
            {
                if (HardSent >= Config.HardDisconnects)
                {
                    Finish(ECloseReason::HardDisconnectLocal, Now);
                    break;
                }
                SendHardDisconnect(Now);
                HardDue = Now + std::clamp<uint32_t>(RttMs / 2, 10, 500);
            }
            break;

        case ELinkState::Established:
        {
            bool bProbed = false;
            for (size_t i = 0; i < InFlight.size();)
            {
                FOutFrame& F = InFlight[i];
                if (!TimeReached(Now, F.NextRetry))
                {
                    ++i;
                    continue;
                }
                // A SACKed frame whose timer runs out has lost its acknowledgement. Resending
                // one of them is enough to make the partner acknowledge again; the others wait
                // another period.
                if (F.bSacked && Reliable(F) && bProbed)
                {
                    F.NextRetry = Now + RetryTimeout(F.Retries);
                    ++i;
                    continue;
                }
                if (F.bSacked)
                    bProbed = true;
                if (Reliable(F))
                {
                    if (F.bEarlyRetry)
                    {
                        // Brought forward by a SACK gap: does not count against the retries
                        // and keeps the backoff where it was. Once per frame.
                        F.bEarlyRetry = false;
                        F.bEarlyRetried = true;
                        ++Stats.FramesRetried;
                        const uint32_t Due = F.LastSend + RetryTimeout(F.Retries);
                        Transmit(F, Now, true, true);
                        F.NextRetry = TimeReached(Now, Due) ? Now + RetryTimeout(F.Retries) : Due;
                        ++i;
                        continue;
                    }
                    if (F.Retries >= Config.MaxRetries)
                    {
                        Finish(ECloseReason::TimedOut, Now);
                        break;
                    }
                    ++F.Retries;
                    ++Stats.FramesRetried;
                    Cwnd = std::max<uint32_t>(2, Cwnd / 2);
                    Transmit(F, Now, true, true);
                    ++i;
                }
                else
                {
                    // An unreliable frame is never resent: tell the partner to skip it.
                    Dropped[F.Seq] = true;
                    ++Stats.FramesDropped;
                    InFlight.erase(InFlight.begin() + (ptrdiff_t)i);
                    if (!bSendMaskPending)
                    {
                        bSendMaskPending = true;
                        SendMaskDue = Now + Config.DelayedSendMaskMs;
                    }
                }
            }
            if (State != ELinkState::Established)
                break;

            if (!bKeepAlivePending && !bCloseRequested && InFlight.empty() && Queue.empty() &&
                Now - LastReceive >= Config.KeepAliveMs && Now - LastKeepAlive >= Config.KeepAliveMs)
            {
                bKeepAlivePending = true;
                LastKeepAlive = Now;
            }
            if (bEndStreamAcked && !bRemoteEndStream && TimeReached(Now, EndStreamAckedAt + Config.CloseLingerMs))
            {
                Finish(ECloseReason::Graceful, Now);
                break;
            }
            Pump(Now, true);
            // A send mask whose SACK was lost would otherwise wait for our next new frame.
            if (!bSendMaskPending && HasDroppedOutstanding() && Now - LastMaskSack >= std::max<uint32_t>(RttMs * 2, 100))
            {
                bSendMaskPending = true;
                SendMaskDue = Now;
            }
            if ((bAckPending && TimeReached(Now, AckDue)) || (bSendMaskPending && TimeReached(Now, SendMaskDue)))
                SendSack(Now);
            break;
        }

        default:
            break;
        }
        Flush();
    }

    // ---------------------------------------------------------------------------------------
    // Sending (3.1.4.4)

    uint64_t FLink::Send(FBytes Message, uint8_t Flags, uint32_t Now)
    {
        LastNow = Now;
        if (Message.empty() || bCloseRequested || State == ELinkState::Idle || State == ELinkState::HardClosing ||
            State == ELinkState::Closed)
            return 0;
        auto Msg = std::make_shared<FOutMessage>();
        Msg->Id = NextMessageId++;
        Msg->Data = std::move(Message);
        Msg->Flags = (uint8_t)(Flags & (MsgFlags::Mask | MsgFlags::Coalesce));
        Queue.push_back(Msg);
        // DPNSEND_COALESCE messages wait for the next Tick to share frames.
        if ((Msg->Flags & MsgFlags::Coalesce) == 0)
            Pump(Now, false);
        Flush();
        return Msg->Id;
    }

    void FLink::Close(uint32_t Now)
    {
        LastNow = Now;
        if (State == ELinkState::Connecting || State == ELinkState::Accepting)
        {
            Finish(ECloseReason::ConnectFailed, Now);
        }
        else if (State == ELinkState::Idle)
        {
            State = ELinkState::Closed;
            CloseReason = ECloseReason::ConnectFailed;
        }
        else if (State == ELinkState::Established && !bCloseRequested)
        {
            bCloseRequested = true;
            Pump(Now, true);
        }
        Flush();
    }

    void FLink::HardClose(uint32_t Now)
    {
        LastNow = Now;
        if (State == ELinkState::Connecting || State == ELinkState::Accepting)
        {
            Finish(ECloseReason::ConnectFailed, Now);
        }
        else if (State == ELinkState::Idle)
        {
            State = ELinkState::Closed;
            CloseReason = ECloseReason::ConnectFailed;
        }
        else if (State == ELinkState::Established)
        {
            FailAllSends();
            State = ELinkState::HardClosing;
            HardSent = 0;
            SendHardDisconnect(Now);
            HardDue = Now + std::clamp<uint32_t>(RttMs / 2, 10, 500);
        }
        Flush();
    }

    // bTick: also send the DPNSEND_COALESCE messages waiting for a Tick. Outside a Tick, only
    // what is ahead of the first such message goes out, to keep the order.
    void FLink::Pump(uint32_t Now, bool bTick)
    {
        if (State != ELinkState::Established)
            return;
        if (!bTick && !Queue.empty() && (Queue.front()->Flags & MsgFlags::Coalesce) != 0 && Queue.front()->Framed == 0)
            return;
        const uint32_t Window = std::min<uint32_t>(Cwnd, 64);
        while ((uint8_t)(NextSend - SendBase) < Window)
        {
            FOutFrame Frame;
            if (!BuildFrame(Frame))
                break;
            Frame.Seq = NextSend++;
            if ((Frame.Control & Control::EndStream) != 0)
                EndStreamSeq = Frame.Seq;
            // Ask for an immediate acknowledgement only when the window is now full, and on
            // KeepAlives and END_STREAM. Otherwise the partner's delayed acknowledgement, or one
            // riding on its own traffic, will do.
            const bool bPoll = (uint8_t)(NextSend - SendBase) >= Window ||
                               (Frame.Control & (Control::KeepAlive | Control::EndStream)) != 0 || Frame.Parts.empty();
            Transmit(Frame, Now, false, bPoll);
            if (!Reliable(Frame))
            {
                for (const FPart& P : Frame.Parts)
                    if (P.Offset + P.Size == P.Msg->Data.size())
                        CompleteMessage(*P.Msg, true);
            }
            InFlight.push_back(std::move(Frame));
        }
    }

    bool FLink::BuildFrame(FOutFrame& Frame)
    {
        const size_t Budget = Config.MaxDatagram - kMaxDataHeaderSize;
        const bool bMidMessage = !Queue.empty() && Queue.front()->Framed != 0;

        if (bKeepAlivePending && !bMidMessage)
        {
            bKeepAlivePending = false;
            Frame.Command = Command::Data | Command::Reliable | Command::Sequential | Command::NewMsg | Command::EndMsg;
            Frame.Control = PartnerCoalesces() ? Control::KeepAlive : 0;
            return true;
        }

        if (!Queue.empty())
        {
            const std::shared_ptr<FOutMessage> Msg = Queue.front();
            const size_t Remaining = Msg->Data.size() - Msg->Framed;
            const uint8_t MsgCommand = (uint8_t)(Msg->Flags & MsgFlags::Mask);

            if (Msg->Framed == 0 && Remaining <= Budget)
            {
                // 2.2.3: several whole small messages of the same reliability in one frame.
                if (Config.bCoalesce && PartnerCoalesces() && Queue.size() > 1 && Remaining <= kMaxCoalescedSize &&
                    (Msg->Flags & MsgFlags::Coalesce) != 0)
                {
                    std::vector<size_t> Sizes = {Remaining};
                    size_t Count = 1;
                    while (Count < Queue.size() && Count < kMaxCoalesced)
                    {
                        const FOutMessage& Next = *Queue[Count];
                        if (Next.Data.size() > kMaxCoalescedSize || (Next.Flags & MsgFlags::Coalesce) == 0 ||
                            ((Next.Flags ^ Msg->Flags) & MsgFlags::Reliable) != 0)
                            break;
                        Sizes.push_back(Next.Data.size());
                        if (CoalescedSize(Sizes) > Budget)
                        {
                            Sizes.pop_back();
                            break;
                        }
                        ++Count;
                    }
                    if (Count >= 2)
                    {
                        Frame.Command = Command::Data | Command::NewMsg | Command::EndMsg;
                        Frame.Control = Control::Coalesce;
                        for (size_t i = 0; i < Count; ++i)
                        {
                            const std::shared_ptr<FOutMessage> Part = Queue.front();
                            Queue.pop_front();
                            Frame.Command |= (uint8_t)(Part->Flags & (MsgFlags::Reliable | MsgFlags::Sequential));
                            Part->Framed = Part->Data.size();
                            ++Part->FramesOutstanding;
                            Frame.Parts.push_back({Part, 0, Part->Data.size()});
                        }
                        return true;
                    }
                }
                Frame.Command = (uint8_t)(Command::Data | Command::NewMsg | Command::EndMsg | MsgCommand);
                Msg->Framed = Msg->Data.size();
                ++Msg->FramesOutstanding;
                Frame.Parts.push_back({Msg, 0, Remaining});
                Queue.pop_front();
                return true;
            }

            // A large message: consecutive frames, nothing else in between.
            const size_t Size = std::min(Remaining, Budget);
            Frame.Command = (uint8_t)(Command::Data | MsgCommand);
            if (Msg->Framed == 0)
                Frame.Command |= Command::NewMsg;
            if (Msg->Framed + Size == Msg->Data.size())
                Frame.Command |= Command::EndMsg;
            Frame.Parts.push_back({Msg, Msg->Framed, Size});
            Msg->Framed += Size;
            ++Msg->FramesOutstanding;
            if (Msg->Framed == Msg->Data.size())
                Queue.pop_front();
            return true;
        }

        if (bCloseRequested && !bEndStreamSent && !bKeepAlivePending)
        {
            bEndStreamSent = true;
            Frame.Command = Command::Data | Command::Reliable | Command::Sequential | Command::NewMsg | Command::EndMsg;
            Frame.Control = Control::EndStream;
            return true;
        }
        return false;
    }

    // Retries carry the current acknowledgement state (3.1.6.5); only Seq is the original.
    void FLink::Transmit(FOutFrame& Frame, uint32_t Now, bool bRetry, bool bPoll)
    {
        FDataHeader H;
        H.Command = (uint8_t)(Frame.Command | (bPoll ? Command::Poll : 0));
        H.Control = (uint8_t)(Frame.Control | (bRetry ? Control::Retry : 0));
        H.Seq = Frame.Seq;
        H.NRcv = NextReceive;
        H.SackMask = ReceiveSackMask();
        H.SendMask = SendMaskFor(Frame.Seq);

        FBytes Out;
        Out.reserve(Config.MaxDatagram);
        H.Write(Out);
        if ((Frame.Control & Control::KeepAlive) != 0)
        {
            FWriter(Out).U32(SessId);
        }
        else if ((Frame.Control & Control::Coalesce) != 0)
        {
            std::vector<FSubPayload> Subs;
            for (const FPart& P : Frame.Parts)
            {
                FSubPayload Sub;
                Sub.Flags = (uint8_t)(P.Msg->Flags & MsgFlags::Mask);
                Sub.Data = P.Msg->Data.data() + P.Offset;
                Sub.Size = P.Size;
                Subs.push_back(Sub);
            }
            EncodeCoalesced(Subs, Out);
        }
        else
        {
            for (const FPart& P : Frame.Parts)
                Out.insert(Out.end(), P.Msg->Data.begin() + (ptrdiff_t)P.Offset,
                           P.Msg->Data.begin() + (ptrdiff_t)(P.Offset + P.Size));
        }

        ++Stats.FramesSent;
        Stats.BytesSent += Out.size();
        Frame.LastSend = Now;
        if (!bRetry)
            Frame.FirstSend = Now;
        Frame.NextRetry = Now + RetryTimeout(Frame.Retries);
        bAckPending = false;
        if (!bRetry)
            bSendMaskPending = false;
        if (H.SendMask != 0)
            LastMaskSack = Now;
        Events.LinkTransmit(Out.data(), Out.size());
    }

    void FLink::SendSack(uint32_t Now)
    {
        FSackFrame S;
        S.Flags = SackFlags::Response;
        S.Retry = bLastReceiveRetry ? 1 : 0;
        S.NSeq = NextSend;
        S.NRcv = NextReceive;
        S.Timestamp = Now;
        S.SackMask = ReceiveSackMask();
        S.SendMask = SendMaskFor(NextSend);
        if (S.SendMask != 0)
            LastMaskSack = Now;
        FBytes Out;
        S.Write(Out);
        bAckPending = false;
        bSendMaskPending = false;
        Events.LinkTransmit(Out.data(), Out.size());
    }

    void FLink::FrameAcknowledged(FOutFrame& Frame, uint32_t Now, bool bSampleRtt)
    {
        if (bSampleRtt && Frame.Retries == 0)
        {
            const uint32_t Sample = std::max<uint32_t>(1, Now - Frame.FirstSend);
            RttMs = bHaveRtt ? (RttMs * 7 + Sample) / 8 : Sample;
            bHaveRtt = true;
            Stats.RttMs = RttMs;
        }
        if ((Frame.Control & Control::EndStream) != 0)
        {
            bEndStreamAcked = true;
            EndStreamAckedAt = Now;
        }
        if (!Reliable(Frame))
            return;
        for (FPart& P : Frame.Parts)
        {
            FOutMessage& Msg = *P.Msg;
            if (Msg.FramesOutstanding > 0)
                --Msg.FramesOutstanding;
            if (Msg.Framed == Msg.Data.size() && Msg.FramesOutstanding == 0)
                CompleteMessage(Msg, true);
        }
    }

    void FLink::CompleteMessage(FOutMessage& Msg, bool bOk)
    {
        if (Msg.bDone)
            return;
        Msg.bDone = true;
        if (bOk)
            ++Stats.MessagesSent;
        else
            ++Stats.MessagesTimedOut;
        FEvent Ev;
        Ev.Kind = FEvent::EKind::SendComplete;
        Ev.Id = Msg.Id;
        Ev.bOk = bOk;
        Pending.push_back(std::move(Ev));
    }

    // 3.1.2.5: 2.5 RTT plus the delayed-ACK time, linear backoff for the second and third
    // retries, exponential after that, capped.
    uint32_t FLink::RetryTimeout(uint32_t Retries) const
    {
        const uint64_t Base = (uint64_t)RttMs * 5 / 2 + Config.DelayedAckMs;
        uint64_t Timeout = Base;
        if (Retries == 1)
            Timeout = Base * 2;
        else if (Retries == 2)
            Timeout = Base * 3;
        else if (Retries >= 3)
            Timeout = Base * 3 << std::min<uint32_t>(Retries - 2, 16);
        return (uint32_t)std::min<uint64_t>(Timeout, Config.RetryCapMs);
    }

    bool FLink::HasDroppedOutstanding() const
    {
        for (uint8_t S = SendBase; S != NextSend; ++S)
            if (Dropped[S])
                return true;
        return false;
    }

    uint64_t FLink::ReceiveSackMask() const
    {
        uint64_t Mask = 0;
        for (uint32_t i = 0; i < 63; ++i)
        {
            const uint8_t S = (uint8_t)(NextReceive + 1 + i);
            if (Slots[S & 63].bHave)
                Mask |= (uint64_t)1 << i;
        }
        return Mask;
    }

    uint64_t FLink::SendMaskFor(uint8_t Seq) const
    {
        uint64_t Mask = 0;
        const uint8_t Span = (uint8_t)(Seq - SendBase);
        for (uint32_t i = 0; i < 64; ++i)
        {
            const uint8_t S = (uint8_t)(Seq - 1 - i);
            if ((uint8_t)(S - SendBase) >= Span)
                break;
            if (Dropped[S])
                Mask |= (uint64_t)1 << i;
        }
        return Mask;
    }

    void FLink::GetQueueInfo(uint32_t& Messages, uint32_t& Bytes) const
    {
        Messages = 0;
        Bytes = 0;
        for (const std::shared_ptr<FOutMessage>& Msg : Queue)
        {
            ++Messages;
            Bytes += (uint32_t)Msg->Data.size();
        }
        for (const FOutFrame& F : InFlight)
            for (const FPart& P : F.Parts)
                if (P.Offset + P.Size == P.Msg->Data.size() && !P.Msg->bDone && P.Msg->Framed == P.Msg->Data.size() &&
                    (Queue.empty() || Queue.front() != P.Msg))
                {
                    ++Messages;
                    Bytes += (uint32_t)P.Msg->Data.size();
                }
    }

    // ---------------------------------------------------------------------------------------
    // Closing (3.1.4.3, 3.1.4.5)

    void FLink::CheckCloseDone(uint32_t Now)
    {
        if (State == ELinkState::Established && bEndStreamSent && bEndStreamAcked && bRemoteEndStream)
            Finish(ECloseReason::Graceful, Now);
    }

    void FLink::FailAllSends()
    {
        for (FOutFrame& F : InFlight)
            for (FPart& P : F.Parts)
                CompleteMessage(*P.Msg, false);
        for (std::shared_ptr<FOutMessage>& Msg : Queue)
            CompleteMessage(*Msg, false);
        InFlight.clear();
        Queue.clear();
        bKeepAlivePending = false;
    }

    void FLink::Finish(ECloseReason Reason, uint32_t Now)
    {
        if (State == ELinkState::Closed)
            return;
        // Both END_STREAMs went out and the partner stopped answering: its last acknowledgement
        // was lost, or it left as soon as ours was in. Still a graceful end.
        if (Reason == ECloseReason::TimedOut && bEndStreamSent && bRemoteEndStream)
            Reason = ECloseReason::Graceful;
        // The partner's END_STREAM must be acknowledged before we go.
        if (Reason == ECloseReason::Graceful && bRemoteEndStream && State == ELinkState::Established)
            SendSack(Now);
        FailAllSends();
        State = ELinkState::Closed;
        CloseReason = Reason;
        FEvent Ev;
        Ev.Kind = FEvent::EKind::Closed;
        Ev.Reason = Reason;
        Pending.push_back(std::move(Ev));
    }

    void FLink::Flush()
    {
        if (bFlushing)
            return;
        bFlushing = true;
        for (size_t i = 0; i < Pending.size(); ++i)
        {
            FEvent Ev = std::move(Pending[i]);
            switch (Ev.Kind)
            {
            case FEvent::EKind::Established:
                Events.LinkEstablished();
                break;
            case FEvent::EKind::Receive:
                Events.LinkReceive(std::move(Ev.Data), Ev.Flags);
                break;
            case FEvent::EKind::SendComplete:
                Events.LinkSendComplete(Ev.Id, Ev.bOk);
                break;
            case FEvent::EKind::RemoteClosing:
                Events.LinkRemoteClosing();
                if (State == ELinkState::Established && !bCloseRequested)
                {
                    bCloseRequested = true;
                    Pump(LastNow, true);
                    CheckCloseDone(LastNow);
                }
                break;
            case FEvent::EKind::Closed:
                Events.LinkClosed(Ev.Reason);
                break;
            }
        }
        Pending.clear();
        bFlushing = false;
    }
}
