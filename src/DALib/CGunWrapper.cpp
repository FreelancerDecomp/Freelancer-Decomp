// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// GUN COM wrapper; worker threads release references and Shutdown joins them.

#include "dalib_net.h"

#include <list>
#include <stdlib.h>
#include <string.h>

namespace
{
    // gundll.dll's class id and interface ids -- retail .data:065ca034 / 065ca024 / 065ca054 /
    // 065ca044 (bytes read from EXE\DALib.dll).
    const GUID GCLSID_GunConnection = {0x76633332, 0x52a2, 0x4460, {0xb0, 0x89, 0x35, 0x41, 0xa8, 0x44, 0x3c, 0x9a}};
    const GUID GIID_GunConnection = {0xeeeeee21, 0x85ea, 0x40f7, {0x81, 0x57, 0xcd, 0x07, 0xa1, 0x38, 0xb4, 0x9f}};
    const GUID GIID_GunHost = {0xeeeeee26, 0x85ea, 0x40f7, {0x81, 0x57, 0xcd, 0x07, 0xa1, 0x38, 0xb4, 0x9f}};
    const GUID GIID_GunBrowser = {0xeeeeee27, 0x85ea, 0x40f7, {0x81, 0x57, 0xcd, 0x07, 0xa1, 0x38, 0xb4, 0x9f}};
    // Retail .rdata:065c7610 {00000001-0000-0000-C000-000000000046}.
    const GUID GIID_ClassFactory = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

    // Retail .rdata:065c7558 -- the cd-key used when the caller passes none: L"0".
    const wchar_t GDefaultCdKey[] = L"0";
    // Retail .rdata:065c7548 -- u"CD-KEY".
    const wchar_t GCdKeyName[] = L"CD-KEY";

    typedef HRESULT(__stdcall* FDllGetClassObject)(REFCLSID, REFIID, void**);

    // The 0x14-byte block the destructor allocates (.text:065c5928 `PUSH 0x14`) and hands to the
    // worker thread: the wrapper's module and its three references, plus an "empty" flag that the
    // allocation sets to 1 and the hand-over clears.
    struct FGunReleaseContext
    {
        HMODULE Module;             // +0x00
        IGunConnection* Connection; // +0x04
        IGunHost* Host;             // +0x08
        IGunBrowser* Browser;       // +0x0c
        bool bEmpty;                // +0x10
    };

    // Worker handles have module lifetime and are joined by Shutdown.
    std::list<HANDLE> GGunThreads;

    // The release sequence retail inlines twice -- in the destructor's CreateThread-failed path
    // (.text:065c59a1..065c5a00) and in the worker (.text:065c5ab1..065c5b10). Nothing is released
    // when the module no longer exports DllGetClassObject; the block is emptied either way.
    void ReleaseGunContext(FGunReleaseContext* Context)
    {
        if (GetProcAddress(Context->Module, "DllGetClassObject") != nullptr)
        {
            if (Context->Host != nullptr)
            {
                Context->Host->Release();
                Context->Host = nullptr;
            }
            if (Context->Browser != nullptr)
            {
                Context->Browser->Release();
                Context->Browser = nullptr;
            }
            if (Context->Connection != nullptr)
            {
                Context->Connection->Disconnect();
                Context->Connection->Release();
                Context->Connection = nullptr;
            }
            FreeLibrary(Context->Module);
        }
        Context->Module = nullptr;
        Context->Connection = nullptr;
        Context->Host = nullptr;
        Context->Browser = nullptr;
        Context->bEmpty = true;
    }

    // Retail FUN_065c5a90 -- the worker thread. Releases only when COM initialised (S_OK or
    // S_FALSE); frees the block in every case.
    DWORD __stdcall GunReleaseThread(void* Parameter)
    {
        FGunReleaseContext* const Context = (FGunReleaseContext*)Parameter;
        const HRESULT Hr = CoInitialize(nullptr);
        if (Hr == S_OK || Hr == S_FALSE)
        {
            if (Context != nullptr && !Context->bEmpty)
                ReleaseGunContext(Context);
            CoUninitialize();
        }
        if (Context != nullptr)
            delete Context;
        return 0;
    }
}

// Retail .text:065c5840.
CGunWrapper::CGunWrapper(const char* DllPath, GUID& Guid, const char* ServerNameIn, unsigned long PortIn,
                         wchar_t* CdKey)
{
    Module = nullptr;
    Connection = nullptr;
    Host = nullptr;
    Browser = nullptr;
    Field_0x08 = 0;
    ServerName = _strdup(ServerNameIn);
    Port = PortIn;
    Status = (IGunConnection::ConnectStatus)0;
    RetryTime = 0;

    Module = LoadLibraryA(DllPath);
    if (Module == nullptr)
        return;
    FDllGetClassObject GetClassObject =
        (FDllGetClassObject)(GetProcAddress(Module, "DllGetClassObject"));
    if (GetClassObject == nullptr)
        return;

    IClassFactory* Factory = nullptr;
    if (GetClassObject(GCLSID_GunConnection, GIID_ClassFactory, (void**)(&Factory)) != S_OK)
        return;
    // Retail leaves the factory unreleased when CreateInstance fails (.text:065c58cb).
    if (Factory->CreateInstance(nullptr, GIID_GunConnection, (void**)(&Connection)) != S_OK)
        return;
    Factory->Release();

    if (Connection->InitializeConnection(&Guid, CdKey != nullptr ? CdKey : (wchar_t*)GDefaultCdKey, 0,
                           GCdKeyName) != 0)
        return;
    // .text:065c5907: the caller's server name (not the _strdup copy) and the full 32-bit port.
    Connection->Connect(ServerNameIn, (unsigned short)PortIn);
}

// Retail .text:065c5b30: XOR EAX,EAX; RET 8.
long __stdcall CGunWrapper::Read(Gun2::GUNQueueMessage* Message)
{
    (void)Message;
    return 0;
}

// Retail .text:065c5920.
CGunWrapper::~CGunWrapper()
{
    FGunReleaseContext* const Context = new FGunReleaseContext();
    Context->Module = nullptr;
    Context->Connection = nullptr;
    Context->Host = nullptr;
    Context->Browser = nullptr;
    Context->bEmpty = true;
    if (Context->bEmpty)
    {
        Context->bEmpty = false;
        Context->Module = Module;
        Context->Connection = Connection;
        Context->Host = Host;
        Context->Browser = Browser;
    }
    Module = nullptr;
    Connection = nullptr;
    Host = nullptr;
    Browser = nullptr;

    DWORD ThreadId = 0;
    const HANDLE Thread = CreateThread(nullptr, 0, &GunReleaseThread, Context, CREATE_SUSPENDED, &ThreadId);
    if (Thread == nullptr)
    {
        if (!Context->bEmpty)
            ReleaseGunContext(Context);
        delete Context;
    }
    else
    {
        GGunThreads.push_back(Thread);
        ResumeThread(Thread);
    }

    if (ServerName != nullptr)
    {
        free(ServerName);
        ServerName = nullptr;
    }
    Field_0x08 = 0;
}

// Retail .text:065c5b40: MOV EAX,[ECX+0x18]; RET.
IGunConnection::ConnectStatus CGunWrapper::GetStatus()
{
    return Status;
}

// Retail .text:065c5b50.
void CGunWrapper::Update()
{
    if (Connection == nullptr)
        return;
    IGunConnection::ConnectStatus Current = (IGunConnection::ConnectStatus)0;
    Connection->GetConnectionStatus(&Current);
    const int CurrentValue = (int)Current;
    if (CurrentValue != 4 && Status == Current)
        return;
    Status = Current;

    if (CurrentValue == 2)
    {
        if (Host == nullptr)
            Connection->QueryInterface(GIID_GunHost, (void**)(&Host));
        if (Browser == nullptr)
            Connection->QueryInterface(GIID_GunBrowser, (void**)(&Browser));
        return;
    }
    // Signed compares (.text:065c5b8d JLE / 065c5b96 JG): only 3 and 4 reconnect.
    if (CurrentValue < 2 || CurrentValue > 4)
        return;
    // Unsigned compare (.text:065c5ba3 JBE).
    if (timeGetTime() <= RetryTime)
        return;

    Connection->Connect(ServerName, (unsigned short)Port);
    Connection->GetConnectionStatus(&Current);
    if ((int)Current == 4)
        RetryTime = timeGetTime() + 10000;
    else
        RetryTime = 0;
}

// Retail .text:065c57d0: join and close every worker thread, then empty the list.
void CGunWrapper::Shutdown()
{
    for (std::list<HANDLE>::iterator It = GGunThreads.begin(); It != GGunThreads.end(); ++It)
    {
        const HANDLE Thread = *It;
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
    }
    GGunThreads.clear();
}
