// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// DPNetCheck.exe (PORT(dpnet), docs/dpnet.md): joins a running Freelancer server through
// Microsoft's DirectPlay 8 client, dpnet.dll, which is what a retail Freelancer client uses, and
// checks the three steps a client takes:
//
//   1. enumeration: an EnumHosts to the server's port is answered (session name, reply data);
//   2. connect: Connect completes with S_OK;
//   3. one guaranteed message each way: DALib's CDPServer answers a 1-byte message with a
//      1-byte link-quality reply (CDPServer::DispatchMsgs), so the byte crosses the server's
//      network layer and its game loop and comes back.
//
// It does not log in: that is the game protocol on top, which RpcCodecTest covers. Run against
// FLDedicated on Microsoft's DirectPlay (Win32) and on DPNet (x64), it shows whether a retail
// client's DirectPlay can join both.
//
//     DPNetCheck.exe [--host 127.0.0.1] [--port 2302] [--timeout 10000]
//
// Exit code 0 when every step passes.

#define INITGUID
#include <windows.h>
#include <dplay8.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{
    HANDLE GEnumDone = nullptr;
    HANDLE GReplyReceived = nullptr;
    CRITICAL_SECTION GLock;

    bool bEnumAnswered = false;
    wchar_t GSessionName[256] = L"";
    GUID GApplication{};
    GUID GInstance{};
    DWORD GEnumReplySize = 0;
    DWORD GEnumPlayers = 0;
    DWORD GEnumMaxPlayers = 0;
    IDirectPlay8Address* GHostAddress = nullptr;

    DWORD GMessagesReceived = 0;
    bool bReplyReceived = false;
    BYTE GReply = 0;
    bool bTerminated = false;

    HRESULT WINAPI HandleMessage(PVOID, DWORD MessageId, PVOID Message)
    {
        EnterCriticalSection(&GLock);
        switch (MessageId)
        {
        case DPN_MSGID_ENUM_HOSTS_RESPONSE:
        {
            const DPNMSG_ENUM_HOSTS_RESPONSE* Response = static_cast<const DPNMSG_ENUM_HOSTS_RESPONSE*>(Message);
            if (!bEnumAnswered && Response->pApplicationDescription != nullptr)
            {
                const DPN_APPLICATION_DESC* Desc = Response->pApplicationDescription;
                if (Desc->pwszSessionName != nullptr)
                    wcsncpy_s(GSessionName, Desc->pwszSessionName, _TRUNCATE);
                GApplication = Desc->guidApplication;
                GInstance = Desc->guidInstance;
                GEnumPlayers = Desc->dwCurrentPlayers;
                GEnumMaxPlayers = Desc->dwMaxPlayers;
                GEnumReplySize = Response->dwResponseDataSize;
                Response->pAddressSender->Duplicate(&GHostAddress);
                bEnumAnswered = true;
                SetEvent(GEnumDone);
            }
            break;
        }
        case DPN_MSGID_RECEIVE:
        {
            const DPNMSG_RECEIVE* Receive = static_cast<const DPNMSG_RECEIVE*>(Message);
            ++GMessagesReceived;
            if (Receive->dwReceiveDataSize == 1 && !bReplyReceived)
            {
                GReply = Receive->pReceiveData[0];
                bReplyReceived = true;
                SetEvent(GReplyReceived);
            }
            break;
        }
        case DPN_MSGID_TERMINATE_SESSION:
            bTerminated = true;
            break;
        default:
            break;
        }
        LeaveCriticalSection(&GLock);
        return S_OK;
    }

    IDirectPlay8Address* NewAddress()
    {
        IDirectPlay8Address* Address = nullptr;
        if (FAILED(CoCreateInstance(CLSID_DirectPlay8Address, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Address,
                                    reinterpret_cast<void**>(&Address))))
            return nullptr;
        Address->SetSP(&CLSID_DP8SP_TCPIP);
        return Address;
    }

    int Fail(const char* Step, HRESULT Result)
    {
        std::printf("FAIL %s (0x%08lx)\n", Step, (unsigned long)Result);
        return 1;
    }
}

int main(int argc, char** argv)
{
    const char* Host = "127.0.0.1";
    DWORD Port = 2302;
    DWORD TimeoutMs = 10000;
    for (int Index = 1; Index + 1 < argc; Index += 2)
    {
        if (std::strcmp(argv[Index], "--host") == 0)
            Host = argv[Index + 1];
        else if (std::strcmp(argv[Index], "--port") == 0)
            Port = (DWORD)std::strtoul(argv[Index + 1], nullptr, 10);
        else if (std::strcmp(argv[Index], "--timeout") == 0)
            TimeoutMs = (DWORD)std::strtoul(argv[Index + 1], nullptr, 10);
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("DPNetCheck: %s:%lu, Microsoft DirectPlay 8 client\n", Host, (unsigned long)Port);

    InitializeCriticalSection(&GLock);
    GEnumDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    GReplyReceived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IDirectPlay8Client* Client = nullptr;
    HRESULT Result = CoCreateInstance(CLSID_DirectPlay8Client, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectPlay8Client,
                                      reinterpret_cast<void**>(&Client));
    if (FAILED(Result))
        return Fail("create the DirectPlay 8 client (is DirectPlay installed?)", Result);
    Result = Client->Initialize(nullptr, HandleMessage, 0);
    if (FAILED(Result))
        return Fail("initialize", Result);

    IDirectPlay8Address* HostAddress = NewAddress();
    IDirectPlay8Address* DeviceAddress = NewAddress();
    if (HostAddress == nullptr || DeviceAddress == nullptr)
        return Fail("create addresses", E_FAIL);
    wchar_t WideHost[256];
    std::swprintf(WideHost, 256, L"%hs", Host);
    HostAddress->AddComponent(DPNA_KEY_HOSTNAME, WideHost, (DWORD)((wcslen(WideHost) + 1) * sizeof(wchar_t)),
                              DPNA_DATATYPE_STRING);
    HostAddress->AddComponent(DPNA_KEY_PORT, &Port, sizeof(Port), DPNA_DATATYPE_DWORD);

    // 1. Enumeration, as the game's server list does it: any application, a few tries.
    DPN_APPLICATION_DESC EnumDesc{};
    EnumDesc.dwSize = sizeof(EnumDesc);
    DPNHANDLE EnumHandle = 0;
    Result = Client->EnumHosts(&EnumDesc, HostAddress, DeviceAddress, nullptr, 0, 5, 500, 3000, nullptr,
                               &EnumHandle, 0);
    if (FAILED(Result))
        return Fail("EnumHosts", Result);
    if (WaitForSingleObject(GEnumDone, TimeoutMs) != WAIT_OBJECT_0)
        return Fail("enumeration: no answer", DPNERR_TIMEDOUT);
    Client->CancelAsyncOperation(EnumHandle, 0);
    std::printf("PASS enumeration: session \"%ls\", players %lu/%lu, reply data %lu bytes\n", GSessionName,
                (unsigned long)GEnumPlayers, (unsigned long)GEnumMaxPlayers, (unsigned long)GEnumReplySize);

    // 2. Connect to the instance that answered.
    DPN_APPLICATION_DESC ConnectDesc{};
    ConnectDesc.dwSize = sizeof(ConnectDesc);
    ConnectDesc.guidApplication = GApplication;
    ConnectDesc.guidInstance = GInstance;
    Result = Client->Connect(&ConnectDesc, GHostAddress != nullptr ? GHostAddress : HostAddress, DeviceAddress,
                             nullptr, nullptr, nullptr, 0, nullptr, nullptr, DPNCONNECT_SYNC);
    if (FAILED(Result))
        return Fail("connect", Result);
    std::printf("PASS connect\n");

    // 3. One guaranteed byte; CDPServer answers it with one byte.
    BYTE Ping = 0;
    DPN_BUFFER_DESC Buffer{1, &Ping};
    DPNHANDLE SendHandle = 0;
    Result = Client->Send(&Buffer, 1, 0, nullptr, &SendHandle, DPNSEND_GUARANTEED);
    if (FAILED(Result))
        return Fail("send", Result);
    if (WaitForSingleObject(GReplyReceived, TimeoutMs) != WAIT_OBJECT_0)
        return Fail("no reply to the 1-byte message", DPNERR_TIMEDOUT);
    std::printf("PASS message round trip: reply 0x%02x, %lu message(s) received\n", GReply,
                (unsigned long)GMessagesReceived);

    Client->Close(0);
    if (GHostAddress != nullptr)
        GHostAddress->Release();
    HostAddress->Release();
    DeviceAddress->Release();
    Client->Release();
    CoUninitialize();
    std::printf("DPNetCheck: all passed\n");
    return 0;
}
