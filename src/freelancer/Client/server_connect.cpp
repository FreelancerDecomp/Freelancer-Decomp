// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "server_connect.h"

#include "chat_init.h"          // ChatResetForDisconnect (FUN_0046b170)
#include "common_ini.h"         // RefreshLoadingProgress
#include "common_server.h"      // IServer
#include "common_server_types.h" // SStartupInfo
#include "common_misc.h"        // SinglePlayer, SetMultiPlayer, CleanUpDatabases
#include "connect_lists.h"      // TeardownConnectLists (FUN_004c4bc0)
#include "dacom.h"              // FDUMP
#include "globals.h"            // ShouldSkipLocalServerTick (FUN_0042d740)
#include "login_mgr.h"
#include "menu_enter_state.h"   // MenuNavGetIntroModeRequest (DAT_0067c200)
#include "player_spawn.h"       // SetPlayerConnectionId (DAT_00673344)

#include <string.h>
#include <windows.h>

IServer* G_Server = nullptr;        // DAT_0067ecd0
void* G_ChatServer = nullptr;       // DAT_0067ecd4

// .data:00615aec -- "Server.dll", overwritten (at most 31 characters) by [Freelancer]
// local_server in FUN_005a82d0.
char G_LocalServerDll[32] = "Server.dll";

namespace
{
    HMODULE G_ServerModule = nullptr;       // DAT_0067ed54
    IServer* G_LoadedServer = nullptr;      // DAT_0067ed58
    void* G_LoadedChatServer = nullptr;     // DAT_0067ed5c

    typedef IServer*(__cdecl* FGetServerInterface)();
    typedef void*(__cdecl* FGetChatServerInterface)();

    // FUN_00564b00: DAT_0067c200 != 0.
    bool IsIntroModeRequested()
    {
        return MenuNavGetIntroModeRequest() != 0;
    }
}

// FUN_005b6f40 (cdecl). Neither GetProcAddress result is tested.
IServer* LoadServerModule(char const* DllName)
{
    G_ServerModule = LoadLibraryA(DllName);
    if (G_ServerModule == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Connect - failed to LoadLibrary(%s)\n",
              "E:\\FL\\Scratch\\Source\\network\\ServerConnect.cpp", 0x1f, DllName);
        return G_LoadedServer;
    }
    G_LoadedServer = reinterpret_cast<FGetServerInterface>(
        GetProcAddress(G_ServerModule, "GetServerInterface"))();
    G_LoadedChatServer = reinterpret_cast<FGetChatServerInterface>(
        GetProcAddress(G_ServerModule, "GetChatServerInterface"))();
    return G_LoadedServer;
}

// FUN_005b6fb0
IServer* GetLoadedServer()
{
    return G_LoadedServer;
}

// FUN_005b6fc0
void* GetLoadedChatServer()
{
    return G_LoadedChatServer;
}

// FUN_005b6fd0 (the pushed argument is not read).
void FreeServerModule()
{
    if (G_ServerModule != nullptr)
    {
        FreeLibrary(G_ServerModule);
        G_ServerModule = nullptr;
        G_LoadedChatServer = nullptr;
        G_LoadedServer = nullptr;
    }
}

// FUN_005a8260. The same block is inlined at the head of FUN_005a8080 and in the failure
// tails of FUN_005a8770 / FUN_005a8840.
void ShutdownServerConnection()
{
    if (!IsIntroModeRequested())
    {
        ChatResetForDisconnect(); // .text:005a8269 FUN_0046b170 (returns at once in SP)
        if (G_Server != nullptr)
        {
            G_LoginMgr.Logout();        // FUN_005aa440
            G_Server->Shutdown();       // +0x28
            FreeServerModule();
            G_Server = nullptr;
            G_ChatServer = nullptr;
        }
        SetPlayerConnectionId(0);
    }
    TeardownConnectLists();             // FUN_004c4bc0
    CleanUpDatabases();
}

// FUN_005a8080 (cdecl).
bool StartServer(char const* IniPath)
{
    RefreshLoadingProgress();
    // .text:005a808f-005a80ed: ShutdownServerConnection's body, inlined.
    ShutdownServerConnection();
    RefreshLoadingProgress();

    if (SinglePlayer())
    {
        LoadServerModule(G_LocalServerDll);
        SetMultiPlayer(false);
    }
    else
    {
        LoadServerModule("RemoteServer.dll");
        SetMultiPlayer(true);
    }
    RefreshLoadingProgress();

    G_Server = GetLoadedServer();
    G_ChatServer = GetLoadedChatServer();
    if (G_Server == nullptr)
    {
        return false;
    }

    // Retail fills only the fields below of an uninitialised stack object.
    SStartupInfo Info;
    memset(&Info, 0, sizeof(Info));
    // DAT_0067eca4 is the exe's own module handle; nullptr names the same module.
    GetModuleFileNameA(nullptr, Info.ModulePath, 0x104);
    strcpy(Info.IniPath, IniPath);      // .text:005a8183 byte-copy loop
    Info.MaxPlayers = 1;
    Info.Port = 0x36d9;
    Info.bAllowPvp = 1;
    Info.bAllowNewPlayers = 1;
    return G_Server->Startup(Info);     // +0x24
}

void* GetLocalServerInterface()
{
    return G_Server;
}

void* GetLocalChatServerInterface()
{
    return G_ChatServer;
}

void TickLocalServer(float DeltaSeconds)
{
    if (G_Server == nullptr || ShouldSkipLocalServerTick())
    {
        return;
    }
    G_Server->Update();                 // +0x2c
    G_Server->ElapseTime(DeltaSeconds); // +0x30
}
