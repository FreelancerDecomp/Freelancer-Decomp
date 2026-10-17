// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// One TU must provide storage for the well-known DirectPlay8 GUIDs pulled in via dplay8.h's
// DEFINE_GUID macros.
#define INITGUID

#include "dalib_net.h"
#include "CDPNetCodec.h"

// .data:065ca120 is seeded at DLL load by the static initializer LAB_065c19c0 (__xc table entry
// .data:065ca004): CALL timeGetTime; MOV [065ca120],EAX.
unsigned long CDPClient::m_dwLastMsgReceivedTime = timeGetTime();
unsigned long CDPClient::m_dwPingSendTime = 0;
unsigned long CDPClient::m_dwPingReceiveTime = 0;
unsigned long CDPClient::m_dwPingSendCount = 0;
unsigned long CDPClient::m_dwPingReceiveCount = 0;
unsigned long CDPClient::m_dwPingLastTime = 0;
unsigned long CDPClient::m_dwRoundTripTime = 0;
unsigned long CDPClient::m_dwSourcePort = 0;
unsigned long CDPClient::m_dwLinkQuality = 0xFF; // .data:065ca020 initialised to 0xFF
namespace
{
    // Retail message Type values used internally by CDPMessage (arbitrary, DALib-internal).
    enum EMsgType : unsigned long
    {
        kMsgReceive = 0,
        kMsgConnectComplete = 1,
        kMsgDisconnected = 3,
        kMsgEnumHostsResponse = 4,
    };

    // Retail DAT_065ca1c8 -- the live CDPClient (set by the ctor, read by GetSendQueueSize).
    // A module global, not a class member: retail exports no such name.
    CDPClient* GActiveClient = nullptr;
}

// Retail DALib.dll.c CDPClient::CDPClient(void) @ 0x2090.
CDPClient::CDPClient()
{
    GActiveClient = this;

    CoInitialize(nullptr);
    bCoInitialized = true;

    const HRESULT Hr = CoCreateInstance(CLSID_DirectPlay8Client, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_IDirectPlay8Client, (void**)(&Client));
    if (FAILED(Hr))
    {
        (*CriticalWarningFn)(0xd4, nullptr);
        return;
    }

    Client->Initialize(this, &HandleClientMsg, 0);

    DPN_CAPS Caps{};
    Caps.dwSize = sizeof(Caps);
    Client->SetCaps(&Caps, 0);
}

// Retail DALib.dll.c CDPClient::~CDPClient(void) @ 0x21c0.
CDPClient::~CDPClient()
{
    if (GActiveClient == this)
        GActiveClient = nullptr;

    if (Client != nullptr)
    {
        Client->CancelAsyncOperation(0, 0x8000);
        Client->Close(0);
        Client->Release();
        Client = nullptr;
    }

    if (bCoInitialized)
    {
        CoUninitialize();
        bCoInitialized = false;
    }

    PendingConnectAttempts.clear();
}

// Copy ctor @ 0x2ec0 / operator= @ 0x3030 are the compiler's memberwise members -- implicit in
// src/include/dalib_net.h, not defined here.

// Retail .text:065c22e0: MOV EAX,[ECX+0x4]; RET.
IDirectPlay8Client* CDPClient::GetClient()
{
    return Client;
}

// Retail DALib.dll.c CDPClient::Cleanup(void) @ 0x22f0 — same teardown as the dtor minus draining
// PendingConnectAttempts / clearing GActiveClient (retail leaves those to ~CDPClient).
void CDPClient::Cleanup()
{
    if (Client != nullptr)
    {
        Client->CancelAsyncOperation(0, 0x8000);
        Client->Close(0);
        Client->Release();
        Client = nullptr;
    }
    if (bCoInitialized)
    {
        CoUninitialize();
        bCoInitialized = false;
    }
}

// Host and Port are stored verbatim; DirectPlay consumes wchar_t URLs.
bool CDPClient::AddConnectAttempt(const wchar_t* Host, wchar_t* Port)
{
    if (bConnected)
        return false;
    PendingConnectAttempts.emplace_back(Host != nullptr ? Host : L"",
                                         Port != nullptr ? Port : L"2000");
    return true;
}

// Retail DALib.dll.c CDPClient::ProcessConnectAttempt @ 0x2580.
bool CDPClient::ProcessConnectAttempt()
{
    if (PendingConnectAttempts.empty())
        return false;
    const std::pair<std::wstring, std::wstring> Attempt = PendingConnectAttempts.front();
    PendingConnectAttempts.pop_front();
    return ConnectToServer(Attempt.first.c_str(), Attempt.second.c_str());
}

// Host is a full DirectPlay URL; Port is retained for the API and is unused here.
bool CDPClient::ConnectToServer(const wchar_t* Host, const wchar_t* /*Port*/)
{
    m_dwPingSendTime = timeGetTime();
    m_dwPingReceiveTime = 0;
    m_dwPingSendCount = 0;
    m_dwPingReceiveCount = 0;
    m_dwPingLastTime = 0;

    IDirectPlay8Address* HostAddr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_IDirectPlay8Address, (void**)(&HostAddr))))
        return false;

    IDirectPlay8Address* DeviceAddr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_IDirectPlay8Address, (void**)(&DeviceAddr))))
    {
        HostAddr->Release();
        return false;
    }

    HostAddr->BuildFromURLW((WCHAR*)Host);
    HostAddr->SetSP(&CLSID_DP8SP_TCPIP);
    DeviceAddr->SetSP(&CLSID_DP8SP_TCPIP);
    if (m_dwSourcePort != 0)
        HostAddr->AddComponent(L"port", &m_dwSourcePort, sizeof(m_dwSourcePort), 2 /*DPNA_DATATYPE_DWORD*/);

    DPN_APPLICATION_DESC AppDesc{};
    AppDesc.dwSize = sizeof(AppDesc);
    AppDesc.guidApplication = ApplicationGuid;

    ConnectResult = DPNSUCCESS_PENDING;
    const HRESULT Hr = Client->Connect(&AppDesc, HostAddr, DeviceAddr, nullptr, nullptr, nullptr, 0,
                                        nullptr, &PendingConnectHandle, 0);
    HostAddr->Release();
    DeviceAddr->Release();
    return Hr == S_OK || Hr == DPNSUCCESS_PENDING;
}

// Retail DALib.dll.c CDPClient::Close @ 0x2790.
void CDPClient::Close()
{
    if (Client != nullptr)
    {
        Client->CancelAsyncOperation(0, 0x8000);
        Client->Close(0);
    }
}

// Retail DALib.dll.c CDPClient::CancelEnums @ 0x27c0.
void CDPClient::CancelEnums()
{
    if (Client != nullptr)
        Client->CancelAsyncOperation(0, 2 /*DPNCANCEL_ENUM*/);
}

// Retail DALib.dll.c CDPClient::Send @ 0x27e0.
bool CDPClient::Send(unsigned char* Data, unsigned long Size)
{
    if (!bConnected)
        return false;

    DPN_BUFFER_DESC BufferDesc{Size, Data};
    DPNHANDLE AsyncHandle = 0;
    const HRESULT Hr = Client->Send(&BufferDesc, 1, 0, nullptr, &AsyncHandle, 0x108);
    return Hr == S_OK || Hr == DPNSUCCESS_PENDING;
}

// Retail DALib.dll.c CDPClient::SetGUID @ 0x2850.
void CDPClient::SetGUID(GUID& Guid)
{
    ApplicationGuid = Guid;
}

// Bare TCP/IP addresses use provider-default broadcast and retry timing.
bool CDPClient::EnumerateHosts()
{
    if (Client == nullptr)
        return false;

    // Retail passes CLSCTX 7 = CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER|CLSCTX_LOCAL_SERVER here
    // (vs. 1 = CLSCTX_INPROC_SERVER alone in ConnectToServer/EnumerateHost).
    constexpr DWORD kEnumHostsClsCtx = CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER;

    IDirectPlay8Address* HostAddr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectPlay8Address, nullptr, kEnumHostsClsCtx,
                                 IID_IDirectPlay8Address, (void**)(&HostAddr))))
        return false;

    IDirectPlay8Address* DeviceAddr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectPlay8Address, nullptr, kEnumHostsClsCtx,
                                 IID_IDirectPlay8Address, (void**)(&DeviceAddr))))
    {
        HostAddr->Release();
        return false;
    }

    if (FAILED(HostAddr->SetSP(&CLSID_DP8SP_TCPIP)))
    {
        DeviceAddr->Release();
        HostAddr->Release();
        return false;
    }
    if (FAILED(DeviceAddr->SetSP(&CLSID_DP8SP_TCPIP)))
    {
        DeviceAddr->Release();
        HostAddr->Release();
        return false;
    }

    DPN_APPLICATION_DESC AppDesc{};
    AppDesc.dwSize = sizeof(AppDesc);
    AppDesc.guidApplication = ApplicationGuid;

    const HRESULT Hr = Client->EnumHosts(&AppDesc, HostAddr, DeviceAddr, nullptr, 0, 0xffffffff, 0, 0,
                                          this, &PendingConnectHandle, 0);
    DeviceAddr->Release();
    HostAddr->Release();
    return Hr == S_OK || Hr == DPNSUCCESS_PENDING;
}

// Failure after address creation retains the addresses; this API does not unwind them.
bool CDPClient::EnumerateHost(const wchar_t* HostAddress, unsigned long Port, unsigned long EnumCount,
                               unsigned long RetryInterval, unsigned long TimeOut)
{
    if (Client == nullptr)
        return false;

    DPN_APPLICATION_DESC AppDesc{}; // 0x12 dwords zeroed, .text:065c29ee
    AppDesc.dwSize = sizeof(AppDesc);
    AppDesc.guidApplication = ApplicationGuid; // this+0x68
    DPNHANDLE AsyncHandle = 0;
    IDirectPlay8Address* HostAddr = nullptr;
    IDirectPlay8Address* DeviceAddr = nullptr;

    if (CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address,
                         (void**)(&HostAddr)) != S_OK)
        return false;
    if (CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address,
                         (void**)(&DeviceAddr)) != S_OK)
        return false;

    wchar_t Url[0x400];
    _swprintf(Url, L"x-directplay:/hostname=%s", HostAddress);
    if (HostAddr->BuildFromURLW(Url) != S_OK)
        return false;
    if (HostAddr->SetSP(&CLSID_DP8SP_TCPIP) != S_OK)
        return false;
    if (DeviceAddr->SetSP(&CLSID_DP8SP_TCPIP) != S_OK)
        return false;
    if (Port != 0 && HostAddr->AddComponent(L"port", &Port, 4, 2 /*DPNA_DATATYPE_DWORD*/) != S_OK)
        return false;

    const HRESULT Hr = Client->EnumHosts(&AppDesc, HostAddr, DeviceAddr, nullptr, 0, EnumCount, RetryInterval,
                                          TimeOut, this, &AsyncHandle, 2);
    if (Hr != S_OK && Hr != DPNSUCCESS_PENDING)
        return false;
    HostAddr->Release();
    DeviceAddr->Release();
    return true;
}

// Retail .text:065c2bc0: MOV EAX,[m_dwLastMsgReceivedTime]; RET.
unsigned long CDPClient::GetLastMsgTimestamp()
{
    return m_dwLastMsgReceivedTime;
}

// Retail .text:065c2c40: CMP ReceiveCount,SendCount; SBB; NEG -- ReceiveCount < SendCount.
bool CDPClient::IsPingOutstanding()
{
    return m_dwPingReceiveCount < m_dwPingSendCount;
}

// Retail .text:065c2c50: MOV [m_dwSourcePort],[ESP+4]; RET.
void CDPClient::SetSourcePort(unsigned long Port)
{
    m_dwSourcePort = Port;
}

// Retail .text:065c2c60: MOV EAX,[m_dwLinkQuality]; RET.
unsigned long CDPClient::GetLinkQuality()
{
    return m_dwLinkQuality;
}

// Retail DALib.dll.c CDPClient::GetPingDelay @ 0x2bd0.
unsigned long CDPClient::GetPingDelay()
{
    if (m_dwPingSendCount == 0)
        return 0;
    if (m_dwPingSendCount == m_dwPingReceiveCount)
    {
        m_dwPingLastTime = m_dwPingReceiveTime - m_dwPingSendTime;
        return m_dwPingReceiveTime - m_dwPingSendTime;
    }
    const DWORD Now = timeGetTime();
    unsigned long Elapsed = Now - m_dwPingSendTime;
    if (Elapsed <= m_dwPingLastTime)
        Elapsed = m_dwPingLastTime;
    return Elapsed;
}

// Retail DALib.dll.c CDPClient::GetSendQueueSize @ 0x2c10 — see header comment: the decompiled C
// return value (the client pointer) is a decompiler artifact; ported as the real disassembled
// behavior (the message count from GetSendQueueInfo).
unsigned long CDPClient::GetSendQueueSize()
{
    if (GActiveClient == nullptr || GActiveClient->Client == nullptr)
        return 0;
    DWORD NumMsgs = 0;
    GActiveClient->Client->GetSendQueueInfo(&NumMsgs, nullptr, 1);
    return NumMsgs;
}

// Retail DALib.dll.c CDPClient::HandleClientMsg @ 0x19d0 — DirectPlay8 IO-thread callback. Only
// queues into IncomingMsgs / updates ping globals; all higher-level dispatch happens on the game
// thread in DispatchMsgs.
long __stdcall CDPClient::HandleClientMsg(void* UserContext, unsigned long MessageId, void* MsgData)
{
    CDPClient* Self = (CDPClient*)UserContext;
    long Result = 0;

    switch (MessageId)
    {
    case DPN_MSGID_ASYNC_OP_COMPLETE:
    {
        // .text:065c1b33
        if (Self == nullptr)
            break;
        const DPNMSG_ASYNC_OP_COMPLETE* const Msg = (DPNMSG_ASYNC_OP_COMPLETE*)MsgData;
        if (Msg->hAsyncOp == Self->PendingConnectHandle)
            Self->PendingConnectHandle = 0;
        break;
    }
    case DPN_MSGID_CONNECT_COMPLETE:
    {
        // .text:065c1acd
        if (Self == nullptr)
            break;
        m_dwLastMsgReceivedTime = timeGetTime();
        const DPNMSG_CONNECT_COMPLETE* const Msg = (DPNMSG_CONNECT_COMPLETE*)MsgData;
        CDPMessage* const Message = new CDPMessage();
        Message->Owner = Self;
        Message->Type = kMsgConnectComplete;
        Message->ConnectResult = Msg->hResultCode;
        Self->IncomingMsgs.Add(Message);
        break;
    }
    case DPN_MSGID_ENUM_HOSTS_RESPONSE:
    {
        // Queue owns the response/descriptor copies and references released by DispatchMsgs.
        if (Self == nullptr)
            break;

        // DEVIATION (deviations.md "DALib: IPv6 host enumeration answers dropped"): the 2003
        // DirectPlay 8.1 TCP/IP provider was IPv4-only. The current dpnet.dll also answers over
        // IPv6, and a link-local fe80:: sender (no scope id) in the server list can be neither
        // parsed (inet_addr) nor connected to. Such answers are not queued; the IPv4 answer of
        // the same host arrives on the next enumeration retry.
        {
            const DPNMSG_ENUM_HOSTS_RESPONSE* const Peek = (DPNMSG_ENUM_HOSTS_RESPONSE*)MsgData;
            wchar_t SenderHost[0x400];
            DWORD SenderHostSize = sizeof(SenderHost);
            DWORD SenderHostType = 1; // DPNA_DATATYPE_STRING
            if (Peek->pAddressSender != nullptr &&
                SUCCEEDED(Peek->pAddressSender->GetComponentByName(L"hostname", SenderHost,
                                                                   &SenderHostSize, &SenderHostType)) &&
                wcschr(SenderHost, L':') != nullptr)
                break;
        }

        CDPMessage* const Message = new CDPMessage();
        Message->Owner = Self;
        Message->Type = kMsgEnumHostsResponse;

        const DPNMSG_ENUM_HOSTS_RESPONSE* const Msg = (DPNMSG_ENUM_HOSTS_RESPONSE*)MsgData;
        Msg->pAddressSender->AddRef();

        DPNMSG_ENUM_HOSTS_RESPONSE* const Copy =
            (DPNMSG_ENUM_HOSTS_RESPONSE*)(malloc(sizeof(DPNMSG_ENUM_HOSTS_RESPONSE)));
        Copy->dwSize = sizeof(DPNMSG_ENUM_HOSTS_RESPONSE);
        Copy->pAddressSender = Msg->pAddressSender;
        Copy->pAddressDevice = nullptr;
        if (Msg->pvResponseData != nullptr)
        {
            Copy->pvResponseData = malloc(Msg->dwResponseDataSize);
            Copy->dwResponseDataSize = Msg->dwResponseDataSize;
            memcpy(Copy->pvResponseData, Msg->pvResponseData, Copy->dwResponseDataSize);
        }
        else
        {
            Copy->pvResponseData = nullptr;
            Copy->dwResponseDataSize = 0;
        }
        Copy->pvUserContext = Msg->pvUserContext;
        Copy->dwRoundTripLatencyMS = Msg->dwRoundTripLatencyMS;

        const DPN_APPLICATION_DESC* const Source = Msg->pApplicationDescription;
        DPN_APPLICATION_DESC* const Desc = (DPN_APPLICATION_DESC*)(malloc(sizeof(DPN_APPLICATION_DESC)));
        Desc->dwSize = sizeof(DPN_APPLICATION_DESC);
        Desc->dwFlags = Source->dwFlags;
        Desc->guidInstance = Source->guidInstance;
        Desc->guidApplication = Source->guidApplication;
        Desc->dwMaxPlayers = Source->dwMaxPlayers;
        Desc->dwCurrentPlayers = Source->dwCurrentPlayers;
        const size_t NameBytes = wcslen(Source->pwszSessionName) * sizeof(WCHAR) + sizeof(WCHAR);
        Desc->pwszSessionName = (WCHAR*)(malloc(NameBytes));
        memcpy(Desc->pwszSessionName, Source->pwszSessionName, NameBytes);
        Desc->pwszPassword = nullptr;
        Desc->pvReservedData = nullptr;
        Desc->dwReservedDataSize = 0;
        Desc->pvApplicationReservedData = nullptr;
        Desc->dwApplicationReservedDataSize = 0;
        Copy->pApplicationDescription = Desc;

        Message->Data = (unsigned char*)Copy;
        Message->Size = sizeof(DPNMSG_ENUM_HOSTS_RESPONSE);
        Self->IncomingMsgs.Add(Message);
        break;
    }
    case DPN_MSGID_RECEIVE:
    {
        // .text:065c1a11: retail reads Self->Client without a null check.
        DPN_CONNECTION_INFO ConnInfo;
        ConnInfo.dwSize = sizeof(DPN_CONNECTION_INFO);
        Self->Client->GetConnectionInfo(&ConnInfo, 0);
        const DWORD Now = timeGetTime();
        m_dwLastMsgReceivedTime = Now;

        const DPNMSG_RECEIVE* const Msg = (DPNMSG_RECEIVE*)MsgData;
        if (Msg->dwReceiveDataSize > 1)
        {
            CDPMessage* const Message = new CDPMessage();
            Message->SenderId = Msg->dpnidSender;
            Message->Size = Msg->dwReceiveDataSize;
            Message->Data = Msg->pReceiveData;
            Message->ReceiveBufferHandle = Msg->hBufferHandle;
            Message->Owner = Self;
            Self->IncomingMsgs.Add(Message);
            Result = DPNSUCCESS_PENDING;
            m_dwRoundTripTime = ConnInfo.dwRoundTripLatencyMS;
        }
        else
        {
            m_dwPingReceiveTime = Now;
            m_dwLinkQuality = Msg->pReceiveData[0];
            m_dwPingReceiveCount++;
            m_dwRoundTripTime = ConnInfo.dwRoundTripLatencyMS;
        }
        break;
    }
    case DPN_MSGID_TERMINATE_SESSION:
    {
        // .text:065c1b5a
        if (Self == nullptr)
            break;
        m_dwLastMsgReceivedTime = timeGetTime();
        CDPMessage* const Message = new CDPMessage();
        Message->Owner = Self;
        Message->Type = kMsgDisconnected;
        Self->IncomingMsgs.Add(Message);
        break;
    }    default:
        break;
    }

    return Result;
}

// Codec lifetime is lazy and ends at process teardown.
void CDPClient::DispatchMsgs(unsigned long TimeoutMs)
{
    static CDPNetCodec Codec;

    if (bConnected && m_dwPingSendCount == m_dwPingReceiveCount)
    {
        const DWORD Now = timeGetTime();
        if (Now - m_dwPingSendTime > 999)
        {
            m_dwPingSendTime = timeGetTime();
            m_dwPingSendCount++;
            if (bConnected)
            {
                // .text:065c1de2: the ping payload is the single byte 1.
                unsigned char PingByte = 1;
                DPN_BUFFER_DESC BufferDesc{1, &PingByte};
                DPNHANDLE AsyncHandle = 0;
                Client->Send(&BufferDesc, 1, 0, nullptr, &AsyncHandle, 0x108);
            }
        }
    }

    WorkingMsgs.Lock();
    WorkingMsgs.CopyList(IncomingMsgs);
    for (CDPMessage* Msg = WorkingMsgs.GetNextMsg(); Msg != nullptr; Msg = WorkingMsgs.GetNextMsg())
    {
        switch (Msg->Type)
        {
        case kMsgReceive:
        {
            // .text:065c1e5a: retail decompresses into a 0x8000 stack buffer (the 0x8010 frame).
            unsigned char DecompressBuffer[0x8000];
            if (Codec.Decompress(Msg->Data, Msg->Size, DecompressBuffer, 0x7fff))
                OnReceive(DecompressBuffer, Codec.GetLastDecompressedSize());
            else
                OnReceive(Msg->Data, Msg->Size);
            // .text:065c1ebf: IDirectPlay8Client::ReturnBuffer (+0x3c), no null check on Client.
            if (Msg->ReceiveBufferHandle != 0)
                Client->ReturnBuffer(Msg->ReceiveBufferHandle, 0);
            break;
        }
        case kMsgConnectComplete:
        {
            // .text:065c1eda
            if (Msg->ConnectResult == 0)
            {
                bConnected = true;
                PendingConnectAttempts.clear();
            }
            if (!PendingConnectAttempts.empty())
            {
                const std::pair<std::wstring, std::wstring> Attempt = PendingConnectAttempts.front();
                const bool bStarted = ConnectToServer(Attempt.first.c_str(), Attempt.second.c_str());
                PendingConnectAttempts.pop_front();
                if (bStarted)
                    break;
            }
            ConnectResult = Msg->ConnectResult;
            OnConnect(Msg->ConnectResult);
            break;
        }
        case kMsgDisconnected:
            // .text:065c1fc8
            m_dwPingSendTime = timeGetTime();
            m_dwPingReceiveTime = 0;
            m_dwPingSendCount = 0;
            m_dwPingReceiveCount = 0;
            m_dwPingLastTime = 0;
            OnDisconnect();
            bConnected = false;
            break;
        case kMsgEnumHostsResponse:
        {
            // .text:065c1ff8: OnAddHost, then Release the AddRef'd sender address and free the
            // session name, the application desc, the response data (if any) and the copy.
            DPNMSG_ENUM_HOSTS_RESPONSE* const Response = (DPNMSG_ENUM_HOSTS_RESPONSE*)(Msg->Data);
            OnAddHost(Response);
            Response->pAddressSender->Release();
            // SDK exposes a const descriptor; this queue owns its malloc allocation.
            free(Response->pApplicationDescription->pwszSessionName);
            free((DPN_APPLICATION_DESC*)(Response->pApplicationDescription));
            if (Response->pvResponseData != nullptr)
                free(Response->pvResponseData);
            free(Response);
            break;
        }        default:
            break;
        }
        delete Msg;
    }
    WorkingMsgs.Unlock();

    (void)TimeoutMs; // retail's DispatchMsgs(param_1) never consumes its own timeout parameter either
}
