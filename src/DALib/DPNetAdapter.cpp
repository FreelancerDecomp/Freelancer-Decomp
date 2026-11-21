// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(dpnet): DPNet behind DirectPlay 8's COM interfaces (DPNetAdapter.h, docs/dpnet.md).
//
// DPNet's FServer has no socket, clock or thread of its own. This file supplies them: a UDP
// socket bound like DirectPlay's TCP/IP service provider, a network thread that feeds received
// datagrams to FServer and calls Tick every 10 ms, and GetTickCount as the clock. One lock
// guards FServer. The application's message handler is called with the lock held, on whichever
// thread is inside FServer (the network thread, or the caller of SendTo, DestroyClient or
// Close). DALib's handler only queues messages under its own locks and never calls back into
// the server, so that is safe for it.

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include "DPNetAdapter.h"
#include "dpnet_session.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    // DirectPlay's TCP/IP service provider takes the first free port of 2302..2400 when the
    // device address names none (DPNA_DPNSVR_PORT is 6073; the game range starts at 2302).
    constexpr unsigned short kFirstDefaultPort = 2302;
    constexpr unsigned short kLastDefaultPort = 2400;
    constexpr DWORD kTickMs = 10;
    // IDirectPlay8Server::Close waits for the players to be told and gone.
    constexpr DWORD kCloseWaitMs = 5000;
    // The largest reply an EnumQuery may get, as DirectPlay's TCP/IP SP reports it.
    constexpr DWORD kMaxEnumResponse = 1000;

    uint32_t NowMs()
    {
        return GetTickCount();
    }

    std::u16string ToU16(const WCHAR* Text)
    {
        if (Text == nullptr)
            return std::u16string();
        return std::u16string(reinterpret_cast<const char16_t*>(Text));
    }

    DPNet::FGuid ToGuid(const GUID& In)
    {
        DPNet::FGuid Out;
        Out.Data1 = In.Data1;
        Out.Data2 = In.Data2;
        Out.Data3 = In.Data3;
        std::memcpy(Out.Data4, In.Data4, sizeof(Out.Data4));
        return Out;
    }

    DPNet::FAppDesc ToAppDesc(const DPN_APPLICATION_DESC& In)
    {
        DPNet::FAppDesc Out;
        Out.Flags = In.dwFlags;
        Out.MaxPlayers = In.dwMaxPlayers;
        Out.SessionName = ToU16(In.pwszSessionName);
        Out.Password = ToU16(In.pwszPassword);
        Out.Application = ToGuid(In.guidApplication);
        Out.Instance = ToGuid(In.guidInstance);
        if (In.pvApplicationReservedData != nullptr && In.dwApplicationReservedDataSize != 0)
        {
            const BYTE* Data = static_cast<const BYTE*>(In.pvApplicationReservedData);
            Out.ApplicationReserved.assign(Data, Data + In.dwApplicationReservedDataSize);
        }
        return Out;
    }

    sockaddr_in ToSockaddr(const DPNet::FAddress& In)
    {
        sockaddr_in Out{};
        Out.sin_family = AF_INET;
        std::memcpy(&Out.sin_addr, In.Ip.data(), 4);
        Out.sin_port = htons(In.Port);
        return Out;
    }

    DPNet::FAddress FromSockaddr(const sockaddr_in& In)
    {
        DPNet::FAddress Out;
        Out.Family = 4;
        std::memcpy(Out.Ip.data(), &In.sin_addr, 4);
        Out.Port = ntohs(In.sin_port);
        return Out;
    }

    // ---------------------------------------------------------------------------------------
    // IDirectPlay8Address: a service provider, a device and named components, as
    // dpnet.dll keeps them. Enough for DALib: SetSP, AddComponent, GetComponentByName, GetURLA.

    class FAddressObject final : public IDirectPlay8Address
    {
    public:
        // IUnknown
        STDMETHOD(QueryInterface)(REFIID Iid, void** Out) override
        {
            if (Out == nullptr)
                return E_POINTER;
            if (Iid == IID_IUnknown || Iid == IID_IDirectPlay8Address)
            {
                AddRef();
                *Out = static_cast<IDirectPlay8Address*>(this);
                return S_OK;
            }
            *Out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&Refs); }
        STDMETHOD_(ULONG, Release)() override
        {
            const LONG Left = InterlockedDecrement(&Refs);
            if (Left == 0)
                delete this;
            return (ULONG)Left;
        }

        // IDirectPlay8Address
        STDMETHOD(BuildFromURLW)(WCHAR*) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(BuildFromURLA)(CHAR*) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(Duplicate)(PDIRECTPLAY8ADDRESS* Out) override
        {
            if (Out == nullptr)
                return DPNERR_INVALIDPOINTER;
            FAddressObject* Copy = new FAddressObject(*this);
            Copy->Refs = 1;
            *Out = Copy;
            return S_OK;
        }
        STDMETHOD(SetEqual)(PDIRECTPLAY8ADDRESS) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(IsEqual)(PDIRECTPLAY8ADDRESS) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(Clear)() override
        {
            bHasSp = false;
            bHasDevice = false;
            Components.clear();
            UserData.clear();
            return S_OK;
        }
        STDMETHOD(GetURLW)(WCHAR* Url, PDWORD NumChars) override
        {
            const std::string Text = BuildUrl();
            if (NumChars == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Need = (DWORD)Text.size() + 1;
            if (Url == nullptr || *NumChars < Need)
            {
                *NumChars = Need;
                return DPNERR_BUFFERTOOSMALL;
            }
            for (size_t Index = 0; Index < Text.size(); ++Index)
                Url[Index] = (WCHAR)(unsigned char)Text[Index];
            Url[Text.size()] = 0;
            *NumChars = Need;
            return S_OK;
        }
        STDMETHOD(GetURLA)(CHAR* Url, PDWORD NumChars) override
        {
            const std::string Text = BuildUrl();
            if (NumChars == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Need = (DWORD)Text.size() + 1;
            if (Url == nullptr || *NumChars < Need)
            {
                *NumChars = Need;
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Url, Text.c_str(), Need);
            *NumChars = Need;
            return S_OK;
        }
        STDMETHOD(GetSP)(GUID* Sp) override
        {
            if (Sp == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (!bHasSp)
                return DPNERR_DOESNOTEXIST;
            *Sp = SpGuid;
            return S_OK;
        }
        STDMETHOD(GetUserData)(LPVOID Buffer, PDWORD Size) override
        {
            if (Size == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (UserData.empty())
                return DPNERR_DOESNOTEXIST;
            if (Buffer == nullptr || *Size < UserData.size())
            {
                *Size = (DWORD)UserData.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Buffer, UserData.data(), UserData.size());
            *Size = (DWORD)UserData.size();
            return S_OK;
        }
        STDMETHOD(SetSP)(const GUID* Sp) override
        {
            if (Sp == nullptr)
                return DPNERR_INVALIDPOINTER;
            SpGuid = *Sp;
            bHasSp = true;
            return S_OK;
        }
        STDMETHOD(SetUserData)(const void* Data, DWORD Size) override
        {
            const BYTE* Bytes = static_cast<const BYTE*>(Data);
            if (Bytes == nullptr || Size == 0)
                UserData.clear();
            else
                UserData.assign(Bytes, Bytes + Size);
            return S_OK;
        }
        STDMETHOD(GetNumComponents)(PDWORD Count) override
        {
            if (Count == nullptr)
                return DPNERR_INVALIDPOINTER;
            *Count = (DWORD)Components.size();
            return S_OK;
        }
        STDMETHOD(GetComponentByName)(const WCHAR* Name, LPVOID Buffer, PDWORD Size, PDWORD Type) override
        {
            if (Name == nullptr || Size == nullptr || Type == nullptr)
                return DPNERR_INVALIDPOINTER;
            const FComponent* Found = Find(Name);
            if (Found == nullptr)
                return DPNERR_DOESNOTEXIST;
            *Type = Found->Type;
            if (Buffer == nullptr || *Size < Found->Data.size())
            {
                *Size = (DWORD)Found->Data.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Buffer, Found->Data.data(), Found->Data.size());
            *Size = (DWORD)Found->Data.size();
            return S_OK;
        }
        STDMETHOD(GetComponentByIndex)(DWORD Index, WCHAR* Name, PDWORD NameLen, void* Buffer, PDWORD Size,
                                       PDWORD Type) override
        {
            if (NameLen == nullptr || Size == nullptr || Type == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (Index >= Components.size())
                return DPNERR_DOESNOTEXIST;
            const FComponent& Found = Components[Index];
            const DWORD NeedName = (DWORD)Found.Name.size() + 1;
            if (Name == nullptr || *NameLen < NeedName || Buffer == nullptr || *Size < Found.Data.size())
            {
                *NameLen = NeedName;
                *Size = (DWORD)Found.Data.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Name, Found.Name.c_str(), NeedName * sizeof(WCHAR));
            std::memcpy(Buffer, Found.Data.data(), Found.Data.size());
            *NameLen = NeedName;
            *Size = (DWORD)Found.Data.size();
            *Type = Found.Type;
            return S_OK;
        }
        STDMETHOD(AddComponent)(const WCHAR* Name, const void* Data, DWORD Size, DWORD Type) override
        {
            if (Name == nullptr || (Data == nullptr && Size != 0))
                return DPNERR_INVALIDPOINTER;
            const BYTE* Bytes = static_cast<const BYTE*>(Data);
            FComponent* Existing = Find(Name);
            if (Existing == nullptr)
            {
                Components.push_back(FComponent());
                Existing = &Components.back();
                Existing->Name = Name;
            }
            Existing->Type = Type;
            Existing->Data.assign(Bytes, Bytes + Size);
            return S_OK;
        }
        STDMETHOD(GetDevice)(GUID* Device) override
        {
            if (Device == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (!bHasDevice)
                return DPNERR_DOESNOTEXIST;
            *Device = DeviceGuid;
            return S_OK;
        }
        STDMETHOD(SetDevice)(const GUID* Device) override
        {
            if (Device == nullptr)
                return DPNERR_INVALIDPOINTER;
            DeviceGuid = *Device;
            bHasDevice = true;
            return S_OK;
        }
        // BuildFromDirectPlay4Address in later SDKs.
        STDMETHOD(BuildFromDPADDRESS)(LPVOID, DWORD) override { return DPNERR_UNSUPPORTED; }

        // The hostname and port components of a UDP endpoint, on the TCP/IP service provider.
        static FAddressObject* ForEndpoint(const DPNet::FAddress& Endpoint)
        {
            FAddressObject* Address = new FAddressObject();
            Address->SetSP(&CLSID_DP8SP_TCPIP);
            const std::string Host = Endpoint.HostText();
            const std::wstring Wide(Host.begin(), Host.end());
            Address->AddComponent(DPNA_KEY_HOSTNAME, Wide.c_str(), (DWORD)((Wide.size() + 1) * sizeof(WCHAR)),
                                  DPNA_DATATYPE_STRING);
            const DWORD Port = Endpoint.Port;
            Address->AddComponent(DPNA_KEY_PORT, &Port, sizeof(Port), DPNA_DATATYPE_DWORD);
            return Address;
        }

        bool GetDword(const WCHAR* Name, DWORD& Out) const
        {
            const FComponent* Found = const_cast<FAddressObject*>(this)->Find(Name);
            if (Found == nullptr || Found->Type != DPNA_DATATYPE_DWORD || Found->Data.size() != sizeof(DWORD))
                return false;
            std::memcpy(&Out, Found->Data.data(), sizeof(DWORD));
            return true;
        }

    private:
        struct FComponent
        {
            std::wstring Name;
            DWORD Type = 0;
            std::vector<BYTE> Data;
        };

        FComponent* Find(const WCHAR* Name)
        {
            for (FComponent& Component : Components)
            {
                if (_wcsicmp(Component.Name.c_str(), Name) == 0)
                    return &Component;
            }
            return nullptr;
        }

        static std::string GuidText(const GUID& Guid)
        {
            char Text[64];
            std::snprintf(Text, sizeof(Text), "%%7B%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X%%7D",
                          (unsigned long)Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0], Guid.Data4[1],
                          Guid.Data4[2], Guid.Data4[3], Guid.Data4[4], Guid.Data4[5], Guid.Data4[6],
                          Guid.Data4[7]);
            return Text;
        }

        // DPNA_HEADER, then provider, device and the components as key=value pairs.
        std::string BuildUrl() const
        {
            std::string Url = "x-directplay:/";
            bool bFirst = true;
            auto Append = [&](const std::string& Key, const std::string& Value) {
                if (!bFirst)
                    Url += ';';
                bFirst = false;
                Url += Key + "=" + Value;
            };
            if (bHasSp)
                Append("provider", GuidText(SpGuid));
            if (bHasDevice)
                Append("device", GuidText(DeviceGuid));
            for (const FComponent& Component : Components)
            {
                const std::string Key(Component.Name.begin(), Component.Name.end());
                std::string Value;
                if (Component.Type == DPNA_DATATYPE_DWORD && Component.Data.size() == sizeof(DWORD))
                {
                    DWORD Number = 0;
                    std::memcpy(&Number, Component.Data.data(), sizeof(Number));
                    Value = std::to_string(Number);
                }
                else if (Component.Type == DPNA_DATATYPE_STRING)
                {
                    const WCHAR* Text = reinterpret_cast<const WCHAR*>(Component.Data.data());
                    for (size_t Index = 0; Index < Component.Data.size() / sizeof(WCHAR) && Text[Index] != 0; ++Index)
                        Value += (char)Text[Index];
                }
                else if (Component.Type == DPNA_DATATYPE_STRING_ANSI)
                {
                    Value.assign(reinterpret_cast<const char*>(Component.Data.data()), Component.Data.size());
                    Value = Value.c_str();
                }
                else if (Component.Type == DPNA_DATATYPE_GUID && Component.Data.size() == sizeof(GUID))
                {
                    GUID Guid;
                    std::memcpy(&Guid, Component.Data.data(), sizeof(Guid));
                    Value = GuidText(Guid);
                }
                Append(Key, Value);
            }
            return Url;
        }

        LONG Refs = 1;
        bool bHasSp = false;
        GUID SpGuid{};
        bool bHasDevice = false;
        GUID DeviceGuid{};
        std::vector<FComponent> Components;
        std::vector<BYTE> UserData;
    };

    // ---------------------------------------------------------------------------------------
    // IDirectPlay8Server on DPNet::FServer.

    class FServerObject final : public IDirectPlay8Server, private DPNet::ITransport, private DPNet::IServerEvents
    {
    public:
        FServerObject() = default;
        ~FServerObject() { Shutdown(); }

        // IUnknown
        STDMETHOD(QueryInterface)(REFIID Iid, void** Out) override
        {
            if (Out == nullptr)
                return E_POINTER;
            if (Iid == IID_IUnknown || Iid == IID_IDirectPlay8Server)
            {
                AddRef();
                *Out = static_cast<IDirectPlay8Server*>(this);
                return S_OK;
            }
            *Out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&Refs); }
        STDMETHOD_(ULONG, Release)() override
        {
            const LONG Left = InterlockedDecrement(&Refs);
            if (Left == 0)
                delete this;
            return (ULONG)Left;
        }

        // IDirectPlay8Server
        STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DWORD) override
        {
            if (Handler == nullptr)
                return DPNERR_INVALIDPARAM;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler != nullptr)
                return DPNERR_ALREADYINITIALIZED;
            WSADATA WsaData;
            if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
                return DPNERR_GENERIC;
            bWinsock = true;
            MessageHandler = Handler;
            HandlerContext = UserContext;
            return S_OK;
        }
        STDMETHOD(EnumServiceProviders)(const GUID*, const GUID*, DPN_SERVICE_PROVIDER_INFO*, PDWORD, PDWORD,
                                        DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(CancelAsyncOperation)(DPNHANDLE, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetSendQueueInfo)(DPNID Player, DWORD* NumMsgs, DWORD* NumBytes, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            uint32_t Messages = 0;
            uint32_t Bytes = 0;
            if (Server == nullptr || !Server->GetSendQueueInfo(Player, Messages, Bytes))
                return DPNERR_INVALIDPLAYER;
            if (NumMsgs != nullptr)
                *NumMsgs = Messages;
            if (NumBytes != nullptr)
                *NumBytes = Bytes;
            return S_OK;
        }
        STDMETHOD(GetApplicationDesc)(DPN_APPLICATION_DESC*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(SetServerInfo)(const DPN_PLAYER_INFO*, PVOID, DPNHANDLE*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(GetClientInfo)(DPNID, DPN_PLAYER_INFO*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetClientAddress)(DPNID Player, IDirectPlay8Address** Address, DWORD) override
        {
            if (Address == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            DPNet::FAddress Endpoint;
            if (Server == nullptr || !Server->GetPlayerAddress(Player, Endpoint))
                return DPNERR_INVALIDPLAYER;
            *Address = FAddressObject::ForEndpoint(Endpoint);
            return S_OK;
        }
        STDMETHOD(GetLocalHostAddresses)(IDirectPlay8Address** Addresses, DWORD* Count, DWORD) override
        {
            if (Count == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            if (Addresses == nullptr || *Count < 1)
            {
                *Count = 1;
                return DPNERR_BUFFERTOOSMALL;
            }
            Addresses[0] = FAddressObject::ForEndpoint(LocalEndpoint());
            *Count = 1;
            return S_OK;
        }
        STDMETHOD(SetApplicationDesc)(const DPN_APPLICATION_DESC* Desc, DWORD) override
        {
            if (Desc == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr)
                return DPNERR_UNINITIALIZED;
            if (Server != nullptr && Server->IsHosting())
                Server->SetApplicationDesc(ToAppDesc(*Desc));
            return S_OK;
        }
        STDMETHOD(Host)(const DPN_APPLICATION_DESC* Desc, IDirectPlay8Address** Devices, DWORD NumDevices,
                        const DPN_SECURITY_DESC*, const DPN_SECURITY_CREDENTIALS*, void*, DWORD) override
        {
            if (Desc == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr)
                return DPNERR_UNINITIALIZED;
            if (Server != nullptr)
                return DPNERR_ALREADYCONNECTED;

            // The device address may name a port; FAddressObject is the only kind there is.
            DWORD Port = 0;
            if (NumDevices != 0 && Devices != nullptr && Devices[0] != nullptr)
                static_cast<FAddressObject*>(Devices[0])->GetDword(DPNA_KEY_PORT, Port);
            if (!OpenSocket((unsigned short)Port))
                return DPNERR_INVALIDDEVICEADDRESS;

            LARGE_INTEGER Counter;
            QueryPerformanceCounter(&Counter);
            const uint64_t Seed = (uint64_t)Counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32);
            // The callback bases are private; convert here, where they are accessible.
            DPNet::ITransport& Transport = *this;
            DPNet::IServerEvents& Events = *this;
            Server = std::make_unique<DPNet::FServer>(Transport, Events);
            if (!Server->Host(ToAppDesc(*Desc), NowMs(), Seed))
            {
                Server.reset();
                CloseSocket();
                return DPNERR_GENERIC;
            }
            bStop = false;
            NetThread = std::thread([this] { NetLoop(); });
            return S_OK;
        }
        STDMETHOD(SendTo)(DPNID Player, const DPN_BUFFER_DESC* Buffers, DWORD NumBuffers, DWORD, void* AsyncContext,
                          DPNHANDLE* AsyncHandle, DWORD Flags) override
        {
            if (Buffers == nullptr && NumBuffers != 0)
                return DPNERR_INVALIDPOINTER;
            std::vector<uint8_t> Data;
            for (DWORD Index = 0; Index < NumBuffers; ++Index)
                Data.insert(Data.end(), Buffers[Index].pBufferData, Buffers[Index].pBufferData + Buffers[Index].dwBufferSize);
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            uint64_t Handle = 0;
            if (!Server->SendTo(Player, Data.data(), Data.size(), Flags, AsyncContext, &Handle, NowMs()))
                return DPNERR_INVALIDPLAYER;
            if (AsyncHandle != nullptr)
                *AsyncHandle = (DPNHANDLE)Handle;
            return (Flags & DPNSEND_NOCOMPLETE) != 0 ? S_OK : DPNSUCCESS_PENDING;
        }
        STDMETHOD(CreateGroup)(const DPN_GROUP_INFO*, void*, void*, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(DestroyGroup)(DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(AddPlayerToGroup)(DPNID, DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(RemovePlayerFromGroup)(DPNID, DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(SetGroupInfo)(DPNID, DPN_GROUP_INFO*, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetGroupInfo)(DPNID, DPN_GROUP_INFO*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(EnumPlayersAndGroups)(DPNID*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(EnumGroupMembers)(DPNID, DPNID*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(Close)(DWORD) override
        {
            {
                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (MessageHandler == nullptr)
                    return DPNERR_UNINITIALIZED;
                if (Server != nullptr && Server->IsHosting())
                    Server->Close(NowMs());
            }
            // The network thread carries the goodbyes out; wait for the last player to go.
            const DWORD Start = GetTickCount();
            for (;;)
            {
                {
                    std::lock_guard<std::recursive_mutex> Guard(Lock);
                    if (Server == nullptr || Server->IsIdle() || !NetThread.joinable())
                        break;
                }
                if (GetTickCount() - Start > kCloseWaitMs)
                    break;
                Sleep(kTickMs);
            }
            StopThread();
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            Server.reset();
            CloseSocket();
            // As in dpnet.dll, a closed object must be initialized again before it is used.
            MessageHandler = nullptr;
            HandlerContext = nullptr;
            if (bWinsock)
            {
                WSACleanup();
                bWinsock = false;
            }
            return S_OK;
        }
        STDMETHOD(DestroyClient)(DPNID Player, const void* Data, DWORD Size, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            return Server->DestroyClient(Player, static_cast<const uint8_t*>(Data), Size, NowMs()) ? S_OK
                                                                                                     : DPNERR_INVALIDPLAYER;
        }
        STDMETHOD(ReturnBuffer)(DPNHANDLE Handle, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            return ReceiveBuffers.erase(Handle) != 0 ? S_OK : DPNERR_INVALIDHANDLE;
        }
        STDMETHOD(GetPlayerContext)(DPNID, PVOID*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetGroupContext)(DPNID, PVOID*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetCaps)(DPN_CAPS* Caps, DWORD) override
        {
            if (Caps == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Size = Caps->dwSize;
            std::memset(Caps, 0, sizeof(*Caps));
            Caps->dwSize = Size;
            return S_OK;
        }
        // DPNet keeps its own timeouts and retry limits (docs/dpnet.md).
        STDMETHOD(SetCaps)(const DPN_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(SetSPCaps)(const GUID*, const DPN_SP_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(GetSPCaps)(const GUID*, DPN_SP_CAPS*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetConnectionInfo)(DPNID Player, DPN_CONNECTION_INFO* Info, DWORD) override
        {
            if (Info == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            const DPNet::FLinkStats* Stats = Server != nullptr ? Server->GetLinkStats(Player) : nullptr;
            if (Stats == nullptr)
                return DPNERR_INVALIDPLAYER;
            const DWORD Size = Info->dwSize;
            std::memset(Info, 0, sizeof(*Info));
            Info->dwSize = Size;
            Info->dwRoundTripLatencyMS = Stats->RttMs;
            Info->dwBytesSentGuaranteed = (DWORD)Stats->BytesSent;
            Info->dwPacketsSentGuaranteed = (DWORD)Stats->FramesSent;
            Info->dwBytesReceivedGuaranteed = (DWORD)Stats->BytesReceived;
            Info->dwPacketsReceivedGuaranteed = (DWORD)Stats->FramesReceived;
            Info->dwPacketsRetried = (DWORD)Stats->FramesRetried;
            Info->dwPacketsDropped = (DWORD)Stats->FramesDropped;
            Info->dwMessagesTransmittedNormalPriority = (DWORD)Stats->MessagesSent;
            Info->dwMessagesTimedOutNormalPriority = (DWORD)Stats->MessagesTimedOut;
            Info->dwMessagesReceived = (DWORD)Stats->MessagesReceived;
            return S_OK;
        }
        STDMETHOD(RegisterLobby)(DPNHANDLE, struct IDirectPlay8LobbiedApplication*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }

    private:
        // ITransport
        void SendTo(const DPNet::FAddress& To, const uint8_t* Data, size_t Size) override
        {
            if (Socket == INVALID_SOCKET || To.Family != 4)
                return;
            const sockaddr_in Target = ToSockaddr(To);
            sendto(Socket, reinterpret_cast<const char*>(Data), (int)Size, 0,
                   reinterpret_cast<const sockaddr*>(&Target), sizeof(Target));
        }

        // IServerEvents: each becomes the DPN_MSGID_ message dpnet.dll would send.
        HRESULT Notify(DWORD MessageId, void* Message)
        {
            return MessageHandler != nullptr ? MessageHandler(HandlerContext, MessageId, Message) : S_OK;
        }

        void ReturnAppBuffer(void* Buffer, void* Context)
        {
            if (Buffer == nullptr && Context == nullptr)
                return;
            DPNMSG_RETURN_BUFFER Message{};
            Message.dwSize = sizeof(Message);
            Message.hResultCode = S_OK;
            Message.pvBuffer = Buffer;
            Message.pvUserContext = Context;
            Notify(DPN_MSGID_RETURN_BUFFER, &Message);
        }

        bool ServerEnumQuery(const DPNet::FAddress& From, const DPNet::FBytes& QueryData, DPNet::FBytes& Response) override
        {
            FAddressObject* Sender = FAddressObject::ForEndpoint(From);
            FAddressObject* Device = FAddressObject::ForEndpoint(LocalEndpoint());
            DPNMSG_ENUM_HOSTS_QUERY Message{};
            Message.dwSize = sizeof(Message);
            Message.pAddressSender = Sender;
            Message.pAddressDevice = Device;
            Message.pvReceivedData = QueryData.empty() ? nullptr : const_cast<uint8_t*>(QueryData.data());
            Message.dwReceivedDataSize = (DWORD)QueryData.size();
            Message.dwMaxResponseDataSize = kMaxEnumResponse;
            const HRESULT Result = Notify(DPN_MSGID_ENUM_HOSTS_QUERY, &Message);
            Sender->Release();
            Device->Release();
            if (FAILED(Result))
                return false;
            const BYTE* Reply = static_cast<const BYTE*>(Message.pvResponseData);
            if (Reply != nullptr && Message.dwResponseDataSize != 0)
                Response.assign(Reply, Reply + Message.dwResponseDataSize);
            ReturnAppBuffer(Message.pvResponseData, Message.pvResponseContext);
            return true;
        }

        bool ServerIndicateConnect(const DPNet::FAddress& From, const DPNet::FPlayerConnectInfo& Info, void*& PlayerContext,
                                   DPNet::FBytes& Reply) override
        {
            FAddressObject* Player = FAddressObject::ForEndpoint(From);
            FAddressObject* Device = FAddressObject::ForEndpoint(LocalEndpoint());
            DPNMSG_INDICATE_CONNECT Message{};
            Message.dwSize = sizeof(Message);
            Message.pvUserConnectData = Info.ConnectData.empty() ? nullptr : const_cast<uint8_t*>(Info.ConnectData.data());
            Message.dwUserConnectDataSize = (DWORD)Info.ConnectData.size();
            Message.pAddressPlayer = Player;
            Message.pAddressDevice = Device;
            const HRESULT Result = Notify(DPN_MSGID_INDICATE_CONNECT, &Message);
            Player->Release();
            Device->Release();
            PlayerContext = Message.pvPlayerContext;
            const BYTE* Data = static_cast<const BYTE*>(Message.pvReplyData);
            if (Data != nullptr && Message.dwReplyDataSize != 0)
                Reply.assign(Data, Data + Message.dwReplyDataSize);
            ReturnAppBuffer(Message.pvReplyData, Message.pvReplyContext);
            return SUCCEEDED(Result);
        }

        void ServerIndicatedConnectAborted(void* PlayerContext) override
        {
            DPNMSG_INDICATED_CONNECT_ABORTED Message{};
            Message.dwSize = sizeof(Message);
            Message.pvPlayerContext = PlayerContext;
            Notify(DPN_MSGID_INDICATED_CONNECT_ABORTED, &Message);
        }

        void ServerCreatePlayer(uint32_t Dpnid, void* PlayerContext) override
        {
            DPNMSG_CREATE_PLAYER Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidPlayer = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Notify(DPN_MSGID_CREATE_PLAYER, &Message);
        }

        void ServerDestroyPlayer(uint32_t Dpnid, void* PlayerContext, uint32_t Reason) override
        {
            DPNMSG_DESTROY_PLAYER Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidPlayer = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Message.dwReason = Reason;
            Notify(DPN_MSGID_DESTROY_PLAYER, &Message);
        }

        // The data stays ours until the application returns it: DPNSUCCESS_PENDING from the
        // handler, then ReturnBuffer with the handle.
        void ServerReceive(uint32_t Dpnid, void* PlayerContext, DPNet::FBytes&& Data) override
        {
            DPNHANDLE Handle = NextBufferHandle++;
            if (Handle == 0)
                Handle = NextBufferHandle++;
            DPNet::FBytes& Stored = ReceiveBuffers[Handle];
            Stored = std::move(Data);
            DPNMSG_RECEIVE Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidSender = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Message.pReceiveData = Stored.data();
            Message.dwReceiveDataSize = (DWORD)Stored.size();
            Message.hBufferHandle = Handle;
            if (Notify(DPN_MSGID_RECEIVE, &Message) != DPNSUCCESS_PENDING)
                ReceiveBuffers.erase(Handle);
        }

        void ServerSendComplete(uint64_t Handle, void* UserContext, bool bOk, uint32_t SendTimeMs) override
        {
            DPNMSG_SEND_COMPLETE Message{};
            Message.dwSize = sizeof(Message);
            Message.hAsyncOp = (DPNHANDLE)Handle;
            Message.pvUserContext = UserContext;
            Message.hResultCode = bOk ? S_OK : DPNERR_CONNECTIONLOST;
            Message.dwSendTime = SendTimeMs;
            Notify(DPN_MSGID_SEND_COMPLETE, &Message);
        }

        // --- socket and thread ---

        // Bound like DirectPlay's TCP/IP SP: all interfaces, the given port, else the first free
        // one of 2302..2400.
        bool OpenSocket(unsigned short Port)
        {
            const unsigned short First = Port != 0 ? Port : kFirstDefaultPort;
            const unsigned short Last = Port != 0 ? Port : kLastDefaultPort;
            for (unsigned int Candidate = First; Candidate <= Last; ++Candidate)
            {
                SOCKET Created = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (Created == INVALID_SOCKET)
                    return false;
                BOOL Exclusive = TRUE;
                setsockopt(Created, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&Exclusive),
                           sizeof(Exclusive));
                sockaddr_in Local{};
                Local.sin_family = AF_INET;
                Local.sin_addr.s_addr = htonl(INADDR_ANY);
                Local.sin_port = htons((unsigned short)Candidate);
                if (bind(Created, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) == 0)
                {
                    // An ICMP port-unreachable from a vanished client must not fail the next
                    // recvfrom (Windows reports it as WSAECONNRESET on a UDP socket).
                    BOOL Report = FALSE;
                    DWORD Returned = 0;
                    WSAIoctl(Created, SIO_UDP_CONNRESET, &Report, sizeof(Report), nullptr, 0, &Returned, nullptr,
                             nullptr);
                    u_long NonBlocking = 1;
                    ioctlsocket(Created, FIONBIO, &NonBlocking);
                    Socket = Created;
                    BoundPort = (unsigned short)Candidate;
                    return true;
                }
                closesocket(Created);
            }
            return false;
        }

        void CloseSocket()
        {
            if (Socket != INVALID_SOCKET)
            {
                closesocket(Socket);
                Socket = INVALID_SOCKET;
            }
            BoundPort = 0;
        }

        // The address the clients reach: the first IPv4 address of this host, else loopback.
        DPNet::FAddress LocalEndpoint() const
        {
            DPNet::FAddress Endpoint = DPNet::FAddress::Ipv4(127, 0, 0, 1, BoundPort);
            char HostName[256] = {};
            if (gethostname(HostName, sizeof(HostName)) != 0)
                return Endpoint;
            addrinfo Hints{};
            Hints.ai_family = AF_INET;
            addrinfo* Found = nullptr;
            if (getaddrinfo(HostName, nullptr, &Hints, &Found) != 0 || Found == nullptr)
                return Endpoint;
            Endpoint = FromSockaddr(*reinterpret_cast<const sockaddr_in*>(Found->ai_addr));
            Endpoint.Port = BoundPort;
            freeaddrinfo(Found);
            return Endpoint;
        }

        void NetLoop()
        {
            std::vector<char> Buffer(64 * 1024);
            while (!bStop)
            {
                fd_set Readable;
                FD_ZERO(&Readable);
                FD_SET(Socket, &Readable);
                timeval Timeout{0, (long)(kTickMs * 1000)};
                select(0, &Readable, nullptr, nullptr, &Timeout);

                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (Server == nullptr)
                    break;
                for (;;)
                {
                    sockaddr_in From{};
                    int FromSize = sizeof(From);
                    const int Got = recvfrom(Socket, Buffer.data(), (int)Buffer.size(), 0,
                                             reinterpret_cast<sockaddr*>(&From), &FromSize);
                    if (Got < 0)
                        break;   // WSAEWOULDBLOCK: nothing more queued
                    if (From.sin_family != AF_INET)
                        continue;
                    Server->OnDatagram(FromSockaddr(From), reinterpret_cast<const uint8_t*>(Buffer.data()),
                                       (size_t)Got, NowMs());
                }
                Server->Tick(NowMs());
            }
        }

        void StopThread()
        {
            bStop = true;
            if (NetThread.joinable() && NetThread.get_id() != std::this_thread::get_id())
                NetThread.join();
            else if (NetThread.joinable())
                NetThread.detach();
        }

        void Shutdown()
        {
            if (MessageHandler != nullptr)
                Close(0);
            StopThread();
        }

        LONG Refs = 1;
        std::recursive_mutex Lock;
        PFNDPNMESSAGEHANDLER MessageHandler = nullptr;
        void* HandlerContext = nullptr;
        bool bWinsock = false;
        std::unique_ptr<DPNet::FServer> Server;
        SOCKET Socket = INVALID_SOCKET;
        unsigned short BoundPort = 0;
        std::thread NetThread;
        std::atomic<bool> bStop{false};
        std::map<DPNHANDLE, DPNet::FBytes> ReceiveBuffers;
        DPNHANDLE NextBufferHandle = 1;
    };
}

namespace DPNetAdapter
{
    HRESULT CreateServer(IDirectPlay8Server** Out)
    {
        if (Out == nullptr)
            return E_POINTER;
        *Out = new FServerObject();
        return S_OK;
    }

    HRESULT CreateAddress(IDirectPlay8Address** Out)
    {
        if (Out == nullptr)
            return E_POINTER;
        *Out = new FAddressObject();
        return S_OK;
    }
}
