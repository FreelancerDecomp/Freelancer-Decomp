// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ClientImpl.h"
#include "remoteclient.h"
#include "dalib_net.h"
#include "common_server.h"
#include "server_iserverimpl.h"
#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "msvcrt_compat.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

namespace
{
    IServer* GLocalServer = static_cast<IServer*>(&Server);
    IChatServer* GLocalChatServer = static_cast<IChatServer*>(&Server);
    // Byte 0 is the native zero byte preceding the 0x200-byte log buffer.
    char GServerLogStorage[0x201] = {};

    int __cdecl DefaultServerLog(ErrorCode Code, const char* Format, ...)
    {
        va_list Arguments;
        va_start(Arguments, Format);
        char* Buffer = GServerLogStorage + 1;
        _vsnprintf(Buffer, 0x200, Format, Arguments);
        va_end(Arguments);
        size_t Length = strlen(Buffer);
        if (GServerLogStorage[Length] != '\n' && Length < 0x1ff)
        {
            Buffer[Length] = '\n';
            Buffer[Length + 1] = 0;
        }
        OutputDebugStringA(Buffer);
        return 0;
    }

    FRemoteClientLogFunction GServerLog = &DefaultServerLog;

    class CRemoteClientProxy : public CDPClientProxy
    {
    public:
        ~CRemoteClientProxy() override;
        void OnReceive(unsigned char* Data, unsigned long Size) override;
        void OnConnect(long Result) override;
        void OnDisconnect() override;

        unsigned int ClientId = 0;
    };

    class CRemoteClientHost : public CDPServer
    {
    public:
        CDPClientProxy* CreateClientProxy() override;
    };
}

void HandleClientPacket(unsigned int ClientId, CDAPacket* Packet);
IServer* RemoteClientGetServer();

double CClientImpl::GetLinkSaturation(unsigned int ClientId)
{
    CDPClientProxy* Proxy = FindProxy(ClientId);
    return Proxy != nullptr ? Proxy->GetLinkSaturation() : 0.0;
}

unsigned int CClientImpl::GetSendQBytes(unsigned int ClientId)
{
    CDPClientProxy* Proxy = FindProxy(ClientId);
    return Proxy != nullptr ? Proxy->GetSendQBytes() : 0u;
}

unsigned int CClientImpl::GetSendQSize(unsigned int ClientId)
{
    CDPClientProxy* Proxy = FindProxy(ClientId);
    return Proxy != nullptr ? Proxy->GetSendQSize() : 0u;
}

void CClientImpl::DisconnectClient(unsigned int ClientId)
{
    --NumClients;
    CDPClientProxy* Proxy = FindProxy(ClientId);
    if (Proxy != nullptr)
        Proxy->Disconnect();
    RemoteClientGetServer()->DisConnect(ClientId, static_cast<EFLConnection>(0));
    if (ClientId != 0 && ClientId <= 128)
        ClientProxies[ClientId - 1] = nullptr;
}

int CClientImpl::Update()
{
    Host->DispatchMsgs();
    return 1;
}

bool CClientImpl::Startup(unsigned int MaxPlayers, unsigned int Port)
{
    NumClients = 0;
    Host = CreateRemoteClientHost();
    Host->BeginHosting(Port);
    Host->SetSessionName(L"Freelancer Default Session Name");
    Host->SetPassword(L"");
    Host->SetMaxPlayers(static_cast<int>(MaxPlayers));
    Serializer = RpcGetRemotePacketSerializer();

    const char* FailureMessage;
    if (RpcRegisterCommonScalarPackets(Serializer) != 0)
        FailureMessage = "CStartup: failed to register common packets";
    else if (RpcRegisterRemoteClientServerPackets(Serializer) != 0)
        FailureMessage = "CStartup: failed to register server packets";
    else if (RpcRegisterClientScalarPackets(Serializer) != 0)
        FailureMessage = "CStartup: failed to register client packets";
    else
    {
        bStarted = true;
        return true;
    }

    GServerLog(ErrorCode{0x100004}, FailureMessage);
    GServerLog(ErrorCode{0x100004}, "CStartup: Could not initialize server sockets.");
    return false;
}

void CClientImpl::Shutdown()
{
    GServerLog(ErrorCode{0x100004}, "CShutdown: shutdown remoteclient transmit layer");
    bStarted = false;
    if (Serializer != nullptr)
    {
        Serializer->Release();
        Serializer = nullptr;
    }
    delete Host;
    Host = nullptr;
}

void CClientImpl::PushToClient(unsigned int ClientId, CDAPacket* Packet)
{
    FPacketBase* WirePacket = reinterpret_cast<FPacketBase*>(Packet);
    CDPClientProxy* Proxy = FindProxy(ClientId);
    if (Proxy != nullptr)
    {
        if (WirePacket == nullptr)
        {
            Proxy->Disconnect();
            return;
        }

        unsigned char StackBuffer[0x2800];
        unsigned char* Buffer = StackBuffer;
        unsigned int Written = 0;
        if (WirePacket->Encode(&Buffer, sizeof(StackBuffer), &Written) == 0)
        {
            Proxy->Send(StackBuffer, Written);
            WirePacket->Release();
            return;
        }

        Buffer = static_cast<unsigned char*>(malloc(0x19000));
        if (Buffer != nullptr)
        {
            if (WirePacket->Encode(&Buffer, 0x19000, &Written) == 0)
                Proxy->Send(Buffer, Written);
            free(Buffer);
        }
    }
    WirePacket->Release();
}

void SetRemoteClientResponseData(bool bAllowPvp, bool bAllowNewPlayers,
    int ServerVersion, const wchar_t* Description, unsigned int ServerId,
    const char* ServerSignature)
{
    char Buffer[0x400];
    sprintf(Buffer, "%d:%d:%d:%d:%s:", static_cast<unsigned int>(bAllowPvp),
        static_cast<unsigned int>(bAllowNewPlayers), ServerVersion, ServerId, ServerSignature);
    unsigned int PrefixLength = static_cast<unsigned int>(strlen(Buffer));
    wcscpy(reinterpret_cast<wchar_t*>(Buffer + PrefixLength), Description);
    unsigned int DescriptionLength = static_cast<unsigned int>(wcslen(Description));
    Client.Host->SetEnumResponse(Buffer, PrefixLength + DescriptionLength * 2 + 2);
}

void SetServerLogFunction(FRemoteClientLogFunction Function)
{
    if (Function != nullptr)
        GServerLog = Function;
}

void ForceClientLogout(unsigned int ClientId)
{
    CDPClientProxy* Proxy = Client.FindProxy(ClientId);
    if (Proxy != nullptr)
        Proxy->Disconnect();
}

void GetRemoteClientPort(std::vector<unsigned long>& Ports)
{
    Client.Host->GetPort(Ports);
}

void SetRemoteClientPassword(const wchar_t* Password)
{
    if (Client.Host != nullptr)
        Client.Host->SetPassword(Password);
}

void SetRemoteClientSessionName(const wchar_t* Name)
{
    if (Client.Host != nullptr)
        Client.Host->SetSessionName(Name);
}

IServer* RemoteClientGetServer()
{
    return GLocalServer;
}

IChatServer* RemoteClientGetChatServer()
{
    return GLocalChatServer;
}

CDPClientProxy* CClientImpl::FindProxy(unsigned int ClientId) const
{
    // Raw PE 06b39630: JBE/JA reject 0 and IDs above 128.
    return ClientId != 0 && ClientId <= 128 ? ClientProxies[ClientId - 1] : nullptr;
}

extern "C" REMOTECLIENT_API IClient* GetClientInterface()
{
    return &Client;
}

extern "C" REMOTECLIENT_API IChatClient* GetChatClientInterface()
{
    return static_cast<IChatClient*>(&Client);
}

int GetNumClients()
{
    return Client.NumClients;
}

CDPServer* CreateRemoteClientHost()
{
    return new (std::nothrow) CRemoteClientHost();
}

CDPClientProxy* CRemoteClientHost::CreateClientProxy()
{
    return new (std::nothrow) CRemoteClientProxy();
}

CRemoteClientProxy::~CRemoteClientProxy()
{
    if (ClientId != 0 && ClientId <= 128)
        Client.ClientProxies[ClientId - 1] = nullptr;
}

void CRemoteClientProxy::OnDisconnect()
{
    Client.DisconnectClient(ClientId);
}

void CRemoteClientProxy::OnConnect(long Result)
{
    if (ClientId == 0)
    {
        for (unsigned int Index = 0; Index < 128; ++Index)
        {
            if (Client.ClientProxies[Index] == nullptr)
            {
                Client.ClientProxies[Index] = this;
                ClientId = Index + 1;
                break;
            }
        }
    }
    ++Client.NumClients;
    FPacketBase* Packet = nullptr;
    if (Client.Serializer->Allocate(2, 1, &Packet) == 0)
    {
        static_cast<FRpcConnectResponsePacket*>(Packet)->Value = ClientId;
        Client.PushToClient(ClientId, Packet->AsRaw());
    }
    RemoteClientGetServer()->OnConnect(ClientId);
}

void CRemoteClientProxy::OnReceive(unsigned char* Data, unsigned long Size)
{
    int Remaining = static_cast<int>(Size);
    if (Data == nullptr || Remaining <= 0)
        return;

    unsigned char* Cursor = Data;
    do
    {
        unsigned short Header;
        memcpy(&Header, Cursor, sizeof(Header));
        unsigned int Channel = Header >> 8;
        unsigned int Opcode = Header & 0xff;
        Cursor += 2;
        Remaining -= 2;

        FPacketBase* Packet = nullptr;
        unsigned int DecodedSize = 0;
        Client.Serializer->Allocate(Channel, Opcode, &Packet);
        if (Packet == nullptr || Packet->Decode(Cursor, Remaining, &DecodedSize) != 0)
        {
            Disconnect();
            return;
        }

        HandleClientPacket(ClientId, Packet->AsRaw());
        Remaining -= static_cast<int>(DecodedSize);
        // 06b3932d subtracts DecodedSize from ESI only. Unlike remoteserver's
        // decoder loop, retail does not advance EDI past the decoded body.
    } while (Remaining > 0);
}

int GetClientStats(FClientStats* Stats, int* Count)
{
    int Written = 0;
    for (unsigned int Index = 0; Index < 128; ++Index)
    {
        CDPClientProxy* Proxy = Client.ClientProxies[Index];
        if (Proxy != nullptr)
        {
            DPN_CONNECTION_INFO Info;
            if (Proxy->GetConnectionStats(&Info))
            {
                Stats[Written].BytesSentGuaranteed = Info.dwBytesSentGuaranteed;
                Stats[Written].BytesReceivedGuaranteed = Info.dwBytesReceivedGuaranteed;
                Stats[Written].PacketsSentGuaranteed = Info.dwPacketsSentGuaranteed;
                Stats[Written].PacketsReceivedGuaranteed = Info.dwPacketsReceivedGuaranteed;
                Stats[Written].ThroughputBps = Info.dwThroughputBPS;
                ++Written;
            }
        }
    }
    *Count = Written;
    return Written;
}
