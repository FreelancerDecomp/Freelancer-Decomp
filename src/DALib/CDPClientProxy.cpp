// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dalib_net.h"
#include "CDPNetCodec.h"

// Retail DALib.dll.c CDPClientProxy::CDPClientProxy(void) @ 0x36b0.
CDPClientProxy::CDPClientProxy()
{
    LastActivityTime = timeGetTime();
    LastHistoryUpdateTime = LastActivityTime;
}

// Retail DALib.dll.c CDPClientProxy::~CDPClientProxy(void) @ 0x3710.
CDPClientProxy::~CDPClientProxy()
{
    Owner = nullptr;
    PlayerId = 0;
}

// Copy ctor @ 0x3f30 / operator= @ 0x4030 are the compiler's memberwise members -- implicit in
// src/include/dalib_net.h, not defined here.

// Retail DALib.dll.c CDPClientProxy::Send @ 0x3a00 -- queue a copy of Data for later coalesced
// transmission by SendMsgs.
bool CDPClientProxy::Send(void* Data, unsigned long Size)
{
    CDPMessage* const Message = new CDPMessage();
    Message->SenderId = PlayerId;
    Message->Size = Size;
    unsigned char* const Copy = (unsigned char*)(malloc(Size));
    memcpy(Copy, Data, Size);
    Message->Data = Copy;

    SendQueue.Lock();
    QueuedCount++;
    QueuedBytes += (int)Size;
    SendQueue.Add(Message);
    SendQueue.Unlock();

    // .text:065c3aa3..3aaf: SendMsgs' result is discarded; Send always returns true.
    SendMsgs();
    return true;
}

// Coalescing keeps the existing incomplete huge-message/state-machine coverage.
bool CDPClientProxy::SendMsgs()
{
    static CDPNetCodec Codec;

    if (!bConnected)
        return false;

    SendQueue.Lock();
    if (SendQueue.PeekNextMsg() == nullptr)
    {
        SendQueue.Unlock();
        return true;
    }

    LastActivityTime = timeGetTime();

    unsigned char Batch[0x400];
    unsigned char Compressed[0x800];
    unsigned int BatchSize = 0;

    auto Flush = [&]() -> bool
    {
        if (BatchSize == 0)
            return true;
        const void* SendData = Batch;
        unsigned long SendSize = BatchSize;
        if (Codec.Compress(Batch, BatchSize, Compressed, sizeof(Compressed)) &&
            Codec.GetLastCompressedSize() <= BatchSize)
        {
            SendData = Compressed;
            SendSize = Codec.GetLastCompressedSize();
        }
        BatchSize = 0;
        bConnected = Owner->SendTo(this, (void*)SendData, SendSize);
        return bConnected;
    };

    for (CDPMessage* Msg = SendQueue.PeekNextMsg(); Msg != nullptr && bConnected;
         Msg = SendQueue.PeekNextMsg())
    {
        if (BatchSize + Msg->Size > sizeof(Batch))
        {
            if (!Flush())
                break;
            continue;
        }
        memcpy(Batch + BatchSize, Msg->Data, Msg->Size);
        BatchSize += (unsigned int)(Msg->Size);
        QueuedBytes -= (int)(Msg->Size);
        QueuedCount--;
        SendQueue.GetNextMsg();
        free(Msg->Data);
        delete Msg;
    }
    if (bConnected)
        Flush();

    SendQueue.Unlock();
    return true;
}

// Retail DALib.dll.c CDPClientProxy::Disconnect @ 0x3ac0.
bool CDPClientProxy::Disconnect()
{
    Owner->DisconnectClient(PlayerId);
    return true;
}

// Retail DALib.dll.c CDPClientProxy::GetConnectionStats @ 0x3ad0.
bool CDPClientProxy::GetConnectionStats(DPN_CONNECTION_INFO* Info)
{
    if (Owner == nullptr)
        return false;
    return Owner->GetConnectionStats(this, Info);
}

// Retail .text:065c3b00: FLD qword ptr [ECX+0x58]; RET.
double CDPClientProxy::GetLinkSaturation()
{
    return LinkSaturation;
}

// Retail .text:065c3af0: RET 4 -- an empty body (ICF-shared with IDPMsgHandler::OnConnect /
// OnAddHost). Called for DPN_MSGID_SEND_COMPLETE by CDPServer::HandleDPServerMsg.
void CDPClientProxy::OnMsgSent(unsigned long SendTime)
{
    (void)SendTime;
}

// Retail DALib.dll.c CDPClientProxy::UpdateQueueHistory @ 0x3b10 -- 3-slot rolling history of
// queued bytes (own + CDPServer-side, via GetSendQBytes), refreshed at most once/second;
// LinkSaturation = average(history) / 3 * 0.0005.
void CDPClientProxy::UpdateQueueHistory()
{
    const DWORD Now = timeGetTime();
    if (Now - LastHistoryUpdateTime <= 1000)
        return;
    LastHistoryUpdateTime = Now;

    const unsigned int ServerSideBytes = Owner != nullptr ? Owner->GetSendQBytes(this) : 0;
    const unsigned int CurrentBytes = ServerSideBytes + (unsigned int)QueuedBytes;

    QueueHistory[2] = QueueHistory[1];
    QueueHistory[1] = QueueHistory[0];
    QueueHistory[0] = CurrentBytes;

    const unsigned long Sum = QueueHistory[0] + QueueHistory[1] + QueueHistory[2];
    LinkSaturation = (double)(Sum / 3) * 0.0005;
}

// Retail DALib.dll.c CDPClientProxy::GetSendQSize @ 0x3b80.
unsigned int CDPClientProxy::GetSendQSize()
{
    const unsigned int ServerSide = Owner != nullptr ? Owner->GetSendQSize(this) : 0;
    return ServerSide + (unsigned int)QueuedCount;
}

// Retail DALib.dll.c CDPClientProxy::GetSendQBytes @ 0x3ba0.
unsigned int CDPClientProxy::GetSendQBytes()
{
    const unsigned int ServerSide = Owner != nullptr ? Owner->GetSendQBytes(this) : 0;
    return ServerSide + (unsigned int)QueuedBytes;
}
