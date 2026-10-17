// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dalib_net.h"

#include <list>

CDPServer* CDPServer::m_pServer = nullptr;
// .data:065ca254 is seeded at DLL load by the static initializer LAB_065c4630 (__xc table entry
// .data:065ca008): CALL timeGetTime; MOV [065ca254],EAX.
unsigned long CDPServer::m_dwLastMsgReceivedTime = timeGetTime();

namespace
{
    enum EMsgType : unsigned long
    {
        kMsgReceive = 0,
        kMsgPlayerCreated = 1,
        kMsgConnectAborted = 2,
        kMsgPlayerDestroyed = 3,
    };

    // DPN_APPLICATION_DESC::dwFlags bit SetPassword toggles (DPNSESSION_REQUIREPASSWORD).
    const unsigned long kFlagRequirePassword = 0x80;

    // Module-wide registry; critical section is destroyed before the list.
    std::list<CDPClientProxy*> g_ClientProxies;
    CRITICAL_SECTION g_ClientProxiesCs;
    struct FClientProxyRegistryInit
    {
        FClientProxyRegistryInit() { InitializeCriticalSection(&g_ClientProxiesCs); }
        ~FClientProxyRegistryInit() { DeleteCriticalSection(&g_ClientProxiesCs); }
    } g_ClientProxyRegistryInit;
}

// Retail DALib.dll.c CDPServer::CDPServer(void) @ 0x4bf0.
CDPServer::CDPServer()
{
    m_pServer = this;

    const HRESULT Hr = CoCreateInstance(CLSID_DirectPlay8Server, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_IDirectPlay8Server, (void**)(&Server));
    if (FAILED(Hr))
    {
        (*CriticalWarningFn)(0xd4, nullptr);
        return;
    }

    Server->Initialize(this, &HandleDPServerMsg, 0);

    AppDesc = DPN_APPLICATION_DESC{};
    AppDesc.dwSize = sizeof(AppDesc);
    AppDesc.dwFlags = 1;
    AppDesc.dwMaxPlayers = 0x10;
    AppDesc.pwszSessionName = SessionName;
    wcscpy(SessionName, L"DPServer Generic");
    Server->SetApplicationDesc(&AppDesc, 0);
    AppDesc.pwszPassword = nullptr;
    AppDesc.dwFlags &= ~kFlagRequirePassword;
    Server->SetApplicationDesc(&AppDesc, 0);

    // Retail default guidApplication (hardcoded in the ctor, DALib.dll.c @ 0x4bf0 lines ~4348-4351).
    AppDesc.guidApplication = {0xA690F026, 0x26F0, 0x4E57,
                                {0xAC, 0xA0, 0xEC, 0xF8, 0x68, 0xE4, 0x8D, 0x21}};

    DPN_CAPS Caps{};
    Caps.dwSize = sizeof(Caps);
    Server->SetCaps(&Caps, 0);
}

// Retail DALib.dll.c CDPServer::~CDPServer(void) @ 0x4d70.
CDPServer::~CDPServer()
{
    bHosting = false;
    IncomingMsgs.SetEmptyEvent();
    ReleaseHostAddresses();

    if (Server != nullptr)
    {
        Server->Close(0);
        Server->Release();
        Server = nullptr;
    }
    m_pServer = nullptr;
}

// Copy ctor @ 0x3bc0 / operator= @ 0x3d20 are the compiler's memberwise members -- implicit in
// src/include/dalib_net.h, not defined here.
// Retail DALib.dll.c CDPServer::UpdateDescription @ 0x4e30.
void CDPServer::UpdateDescription()
{
    if (Server != nullptr)
        Server->SetApplicationDesc(&AppDesc, 0);
}

// Retail DALib.dll.c CDPServer::SetGUID @ 0x4e50.
void CDPServer::SetGUID(GUID& Guid)
{
    AppDesc.guidApplication = Guid;
}

// Retail .text:065c4e70: unbounded wcscpy into +0x50, then the SetApplicationDesc that
// UpdateDescription (.text:065c4e30) performs -- inlined in retail.
void CDPServer::SetSessionName(const wchar_t* Name)
{
    wcscpy(SessionName, Name);
    UpdateDescription();
}

// Retail DALib.dll.c CDPServer::SetMaxPlayers @ 0x4ea0 (retail stores param+1 -- reserving one
// slot for the server's own local player).
void CDPServer::SetMaxPlayers(int MaxPlayers)
{
    AppDesc.dwMaxPlayers = MaxPlayers + 1;
    UpdateDescription();
}

// Retail .text:065c4ec0: `CMP word ptr [EAX],0` with no null check, unbounded wcscpy into +0x258,
// then SetApplicationDesc (inlined UpdateDescription).
void CDPServer::SetPassword(const wchar_t* NewPassword)
{
    if (NewPassword[0] == L'\0')
    {
        AppDesc.pwszPassword = nullptr;
        AppDesc.dwFlags &= ~kFlagRequirePassword;
    }
    else
    {
        wcscpy(Password, NewPassword);
        AppDesc.pwszPassword = Password;
        AppDesc.dwFlags |= kFlagRequirePassword;
    }
    UpdateDescription();
}

// Retail DALib.dll.c CDPServer::GetHostAddresses @ 0x4f20 (protected) -- caches
// GetLocalHostAddresses() results and logs each as a URL via OutputDebugStringA (retail debug
// spew, "HOSTING URL: ...").
void CDPServer::GetHostAddresses()
{
    if (Server == nullptr || NumHostAddresses != 0)
        return;

    NumHostAddresses = 64;
    if (FAILED(Server->GetLocalHostAddresses(HostAddresses, &NumHostAddresses, 0)))
    {
        NumHostAddresses = 0;
        return;
    }

    for (DWORD i = 0; i < NumHostAddresses; ++i)
    {
        char Url[0x400];
        DWORD UrlLen = sizeof(Url);
        if (SUCCEEDED(HostAddresses[i]->GetURLA(Url, &UrlLen)))
        {
            OutputDebugStringA("\n");
            OutputDebugStringA("HOSTING URL: ");
            OutputDebugStringA(Url);
            OutputDebugStringA("\n\n");
        }
    }
}

// Retail DALib.dll.c CDPServer::ReleaseHostAddresses @ 0x4fd0 (protected).
void CDPServer::ReleaseHostAddresses()
{
    for (DWORD i = 0; i < NumHostAddresses; ++i)
    {
        if (HostAddresses[i] != nullptr)
        {
            HostAddresses[i]->Release();
            HostAddresses[i] = nullptr;
        }
    }
    NumHostAddresses = 0;
}

// Retail DALib.dll.c CDPServer::CreateClientProxy @ 0x5020 (virtual factory hook).
CDPClientProxy* CDPServer::CreateClientProxy()
{
    return new CDPClientProxy();
}

// Retail DALib.dll.c CDPServer::BeginHosting @ 0x5080 -- builds a bare TCPIP-SP device address
// (with an optional "port" component) and calls IDirectPlay8Server::Host once.
bool CDPServer::BeginHosting(unsigned int Port)
{
    if (Server == nullptr)
        return false;
    if (bHosting)
        return true;

    IDirectPlay8Address* DeviceAddr = nullptr;
    CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address,
                      (void**)(&DeviceAddr));
    if (DeviceAddr == nullptr)
        return false;
    DeviceAddr->SetSP(&CLSID_DP8SP_TCPIP);
    if (Port != 0)
        DeviceAddr->AddComponent(L"port", &Port, sizeof(Port), 2 /*DPNA_DATATYPE_DWORD*/);

    IDirectPlay8Address* Devices[1] = {DeviceAddr};
    const HRESULT Hr = Server->Host(&AppDesc, Devices, 1, nullptr, nullptr, nullptr, 0);
    DeviceAddr->Release();
    if (FAILED(Hr))
        return false;

    bHosting = true;
    GetHostAddresses();
    return true;
}

// Retail DALib.dll.c CDPServer::StopHosting @ 0x5140.
void CDPServer::StopHosting()
{
    if (Server != nullptr)
        Server->Close(0);
}

// A failed port lookup still appends its seeded zero value.
void CDPServer::GetPort(std::vector<unsigned long>& Ports)
{
    for (DWORD i = 0; i < NumHostAddresses; ++i)
    {
        unsigned long Port = 0;
        DWORD Size = 4;
        DWORD Type = 2;
        HostAddresses[i]->GetComponentByName(L"port", &Port, &Size, &Type);
        Ports.push_back(Port);
    }
}

// Retail DALib.dll.c CDPServer::SendTo @ 0x5360.
bool CDPServer::SendTo(CDPClientProxy* Proxy, void* Data, unsigned long Size)
{
    if (Server == nullptr || Proxy == nullptr)
        return false;
    DPN_BUFFER_DESC BufferDesc{Size, (BYTE*)Data};
    DPNHANDLE AsyncHandle = 0;
    const HRESULT Hr = Server->SendTo(Proxy->PlayerId, &BufferDesc, 1, 0, Proxy, &AsyncHandle, 0x108);
    return Hr == S_OK || Hr == DPNSUCCESS_PENDING;
}

// Retail DALib.dll.c CDPServer::DisconnectClient @ 0x53d0.
void CDPServer::DisconnectClient(unsigned long PlayerId)
{
    if (Server != nullptr)
        Server->DestroyClient(PlayerId, nullptr, 0, 0);
}

// Retail .text:065c53f0 CDPServer::GetConnectionStats(CDPClientProxy*, ...): dwSize = 0x5c,
// GetConnectionInfo (slot 0x80) on the proxy's dpnid (+0x8); `NEG; SBB AL,AL; INC AL`
// (.text:065c5421) returns true only for exactly S_OK, not for any SUCCEEDED code.
bool CDPServer::GetConnectionStats(CDPClientProxy* Proxy, DPN_CONNECTION_INFO* Info)
{
    if (Server == nullptr || Proxy == nullptr || Info == nullptr)
        return false;
    Info->dwSize = sizeof(DPN_CONNECTION_INFO);
    return Server->GetConnectionInfo(Proxy->PlayerId, Info, 0) == S_OK;
}

// Retail .text:065c5440 CDPServer::GetConnectionStats(DPN_CONNECTION_INFO*) -- own
// (server-local) connection stats via OwnPlayerId (+0x568); same `== S_OK` result
// (.text:065c546c).
bool CDPServer::GetConnectionStats(DPN_CONNECTION_INFO* Info)
{
    if (Server == nullptr || Info == nullptr)
        return false;
    Info->dwSize = sizeof(DPN_CONNECTION_INFO);
    return Server->GetConnectionInfo(OwnPlayerId, Info, 0) == S_OK;
}

// Return the message count written by GetSendQueueInfo.
unsigned int CDPServer::GetSendQSize(CDPClientProxy* Proxy)
{
    if (Server == nullptr || Proxy == nullptr)
        return 0;
    DWORD NumMsgs = 0, NumBytes = 0;
    Server->GetSendQueueInfo(Proxy->PlayerId, &NumMsgs, &NumBytes, 1);
    return NumMsgs;
}

// Retail DALib.dll.c CDPServer::GetSendQBytes @ 0x54c0 -- same decompiler-artifact return value as
// GetSendQSize; ported as the real (byte count) behavior.
unsigned int CDPServer::GetSendQBytes(CDPClientProxy* Proxy)
{
    if (Server == nullptr || Proxy == nullptr)
        return 0;
    DWORD NumMsgs = 0, NumBytes = 0;
    Server->GetSendQueueInfo(Proxy->PlayerId, &NumMsgs, &NumBytes, 1);
    return NumBytes;
}

// Retail DALib.dll.c CDPServer::SetEnumResponse @ 0x5500.
bool CDPServer::SetEnumResponse(void* Data, unsigned long Size)
{
    if (Size > sizeof(EnumResponseData))
        return false;
    if (Data != nullptr)
    {
        memcpy(EnumResponseData, Data, Size);
        EnumResponseSize = Size;
    }
    else
    {
        EnumResponseSize = 0;
    }
    return true;
}

// Retail .text:065c5560: MOV EAX,[m_dwLastMsgReceivedTime]; RET.
unsigned long CDPServer::GetLastMsgTimestamp()
{
    return m_dwLastMsgReceivedTime;
}

// Retail DALib.dll.c CDPServer::CrashCleanup @ 0x5570 -- static emergency shutdown via the single
// global server instance.
void CDPServer::CrashCleanup()
{
    if (m_pServer != nullptr && m_pServer->Server != nullptr)
        m_pServer->Server->Close(0);
}


// Retail DALib.dll.c CDPServer::HandleDPServerMsg @ 0x4670 -- DirectPlay8 IO-thread callback.
long __stdcall CDPServer::HandleDPServerMsg(void* UserContext, unsigned long MessageId, void* MsgData)
{
    CDPServer* Self = (CDPServer*)UserContext;
    long Result = 0;

    switch (MessageId)
    {
    case DPN_MSGID_CREATE_PLAYER:
    {
        const DPNMSG_CREATE_PLAYER* const Msg = (DPNMSG_CREATE_PLAYER*)MsgData;
        if (Msg->pvPlayerContext == nullptr)
        {
            // Server's own local player being created.
            Self->OwnPlayerId = Msg->dpnidPlayer;
            return 0;
        }
        CDPClientProxy* const Proxy = (CDPClientProxy*)(Msg->pvPlayerContext);
        Proxy->PlayerId = Msg->dpnidPlayer;
        CDPMessage* const Message = new CDPMessage();
        Message->Type = kMsgPlayerCreated;
        Message->Owner = Proxy;
        Self->IncomingMsgs.Add(Message);
        break;
    }
    case DPN_MSGID_DESTROY_PLAYER:
    {
        const DPNMSG_DESTROY_PLAYER* const Msg = (DPNMSG_DESTROY_PLAYER*)MsgData;
        if (Msg->pvPlayerContext == nullptr)
        {
            Self->OwnPlayerId = 0;
            return 0;
        }
        CDPMessage* const Message = new CDPMessage();
        Message->Type = kMsgPlayerDestroyed;
        Message->Owner = Msg->pvPlayerContext;
        Self->IncomingMsgs.Add(Message);
        break;
    }
    case DPN_MSGID_ENUM_HOSTS_QUERY:
    {
        DPNMSG_ENUM_HOSTS_QUERY* const Msg = (DPNMSG_ENUM_HOSTS_QUERY*)MsgData;
        Msg->pvResponseData = Self->EnumResponseSize != 0 ? Self->EnumResponseData : nullptr;
        Msg->dwResponseDataSize = Self->EnumResponseSize;
        break;
    }
    case DPN_MSGID_INDICATE_CONNECT:
    {
        DPNMSG_INDICATE_CONNECT* const Msg = (DPNMSG_INDICATE_CONNECT*)MsgData;
        CDPClientProxy* Proxy = Self->CreateClientProxy();
        Proxy->Owner = Self;
        Msg->pvPlayerContext = Proxy;

        EnterCriticalSection(&g_ClientProxiesCs);
        g_ClientProxies.push_back(Proxy);
        LeaveCriticalSection(&g_ClientProxiesCs);
        break;
    }
    case DPN_MSGID_INDICATED_CONNECT_ABORTED:
    {
        const DPNMSG_INDICATED_CONNECT_ABORTED* const Msg = (DPNMSG_INDICATED_CONNECT_ABORTED*)MsgData;
        if (Msg->pvPlayerContext != nullptr)
        {
            CDPMessage* const Message = new CDPMessage();
            Message->Type = kMsgConnectAborted;
            Message->Owner = Msg->pvPlayerContext;
            Self->IncomingMsgs.Add(Message);
        }
        break;
    }
    case DPN_MSGID_SEND_COMPLETE:
    {
        // .text:065c48da: the send's user context is the CDPClientProxy SendTo passed.
        const DPNMSG_SEND_COMPLETE* const Msg = (DPNMSG_SEND_COMPLETE*)MsgData;
        CDPClientProxy* const Proxy = (CDPClientProxy*)(Msg->pvUserContext);
        if (Proxy != nullptr)
            Proxy->OnMsgSent(Msg->dwSendTime);
        break;
    }
    case DPN_MSGID_RECEIVE:
    {
        const DPNMSG_RECEIVE* const Msg = (DPNMSG_RECEIVE*)MsgData;
        if (Msg->pvPlayerContext != nullptr)
        {
            CDPMessage* const Message = new CDPMessage();
            Message->SenderId = Msg->dpnidSender;
            Message->Size = Msg->dwReceiveDataSize;
            Message->Data = Msg->pReceiveData;
            Message->ReceiveBufferHandle = Msg->hBufferHandle;
            Message->Owner = Msg->pvPlayerContext;
            Self->IncomingMsgs.Add(Message);
            Result = DPNSUCCESS_PENDING;
            Self->m_dwLastMsgReceivedTime = timeGetTime();
        }
        break;
    }
    default:
        break;
    }

    return Result;
}

// Retail DALib.dll.c CDPServer::DispatchMsgs @ 0x4940.
void CDPServer::DispatchMsgs()
{
    if (!bHosting)
        return;

    CDPMsgList Working;
    Working.Lock();
    Working.CopyList(IncomingMsgs);
    for (CDPMessage* Msg = Working.GetNextMsg(); Msg != nullptr; Msg = Working.GetNextMsg())
    {
        CDPClientProxy* const Proxy = (CDPClientProxy*)(Msg->Owner);
        switch (Msg->Type)
        {
        case kMsgReceive:
        {
            if (Msg->Size == 1)
            {
                // Ping-quality response: echo back a byte derived from the proxy's link
                // saturation.
                const unsigned char Quality =
                    (unsigned char)(-1 - (long)(Proxy->GetLinkSaturation()));
                DPN_BUFFER_DESC BufferDesc{1, (unsigned char*)(&Quality)};
                DPNHANDLE AsyncHandle = 0;
                if (Server != nullptr && Proxy != nullptr)
                    Server->SendTo(Proxy->PlayerId, &BufferDesc, 1, 0, Proxy, &AsyncHandle, 0x108);
            }
            else if (Proxy != nullptr)
            {
                Proxy->OnReceive(Msg->Data, Msg->Size);
            }
            if (Msg->ReceiveBufferHandle != 0 && Server != nullptr)
                Server->ReturnBuffer(Msg->ReceiveBufferHandle, 0);
            break;
        }
        case kMsgPlayerCreated:
            if (Proxy != nullptr)
                Proxy->OnConnect(0);
            break;
        case kMsgConnectAborted:
        {
            EnterCriticalSection(&g_ClientProxiesCs);
            g_ClientProxies.remove(Proxy);
            LeaveCriticalSection(&g_ClientProxiesCs);
            delete Proxy;
            break;
        }
        case kMsgPlayerDestroyed:
        {
            if (Proxy != nullptr)
                Proxy->OnDisconnect();
            EnterCriticalSection(&g_ClientProxiesCs);
            g_ClientProxies.remove(Proxy);
            LeaveCriticalSection(&g_ClientProxiesCs);
            delete Proxy;
            break;
        }
        default:
            break;
        }
        delete Msg;
    }
    Working.Unlock();

    EnterCriticalSection(&g_ClientProxiesCs);
    for (std::list<CDPClientProxy*>::iterator It = g_ClientProxies.begin(); It != g_ClientProxies.end(); ++It)
        (*It)->UpdateQueueHistory();
    LeaveCriticalSection(&g_ClientProxiesCs);
}
