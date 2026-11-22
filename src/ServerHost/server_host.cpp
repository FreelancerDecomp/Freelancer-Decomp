// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The server host shared by FLServer.exe and FLDedicated.exe (see server_host.h). Compiled
// into both programs; the addresses are FLServer.exe's.

#include "server_host.h"

#include <objbase.h>
#include <mmsystem.h>

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "common_ini.h"           // ILoadingScreen
#include "common_misc.h"          // WaitForServerRunPermission, GiveClientRunPermission
#include "common_timing.h"        // Timing
#include "dacom.h"                // DACOM_GetDllVersion
#include "remoteclient.h"         // SetRemoteClient*
#include "server_iserverimpl.h"   // Server
#include "server_playerdb.h"      // Players

namespace
{
    // .data:00425548 -- the server thread's frame period in seconds (1/30). Retail keeps it in
    // writable .data but nothing writes it (only the two reads at .text:0040cda0 / 0040cdb4).
    float GServerFramePeriod = 0.0333333351f;

    // .data:004278a0 (vtable .rdata:0041d030, set by the static initializer .text:0040b340) --
    // the loading screen InitInstance registers with Common: both slots are a bare RET
    // (.text:0040b350 / 0040b360), so Common's progress refreshes do nothing in the server.
    class FServerLoadingScreen : public ILoadingScreen
    {
    public:
        virtual void Refresh() override {}   // .text:0040b350
        virtual void Slot01() override {}    // .text:0040b360 -- ILoadingScreen +0x04, never called
    };
    FServerLoadingScreen GServerLoadingScreen;
}

const char* const ServerConfigFileName = "FLServer.cfg";

void SetupServerFpu(bool bUnmaskExceptions)
{
    if (bUnmaskExceptions)
    {
        _clearfp();
        _control87(_controlfp(0, 0) & ~(_EM_ZERODIVIDE | _EM_OVERFLOW | _EM_INVALID), 0x8001f);
    }
    else
    {
        _control87(0x9001f, 0xfffff);
    }
}

// .text:0040be90
unsigned int RunServerStartup(const SStartupInfo& Info)
{
    HRESULT Result = CoInitialize(NULL);
    unsigned int bStarted = Server.Startup(Info) ? 1 : 0;
    if (Result == S_OK || Result == S_FALSE)
    {
        CoUninitialize();
    }
    return bStarted;
}

// .text:0040b710
unsigned int RunServerShutdown()
{
    HRESULT Result = CoInitialize(NULL);
    Server.Shutdown();
    if (Result == S_OK || Result == S_FALSE)
    {
        CoUninitialize();
    }
    return 1;
}

// .text:0040cb40
DWORD RunServerThread(const FServerThreadHooks& Hooks)
{
    HRESULT Result = CoInitialize(NULL);

    char MotdPath[0x400] = "";
    Players.MakeLocalUserPath(MotdPath, "motd.dat");
    FILE* MotdFile = fopen(MotdPath, "rb");
    if (MotdFile != NULL)
    {
        wchar_t MotdText[0x1400] = L"";
        fread(MotdText, 0x2800, 1, MotdFile);
        MotdText[0x13ff] = L'\0';
        fclose(MotdFile);
        size_t Length = wcslen(MotdText);
        Hooks.Motd->assign(MotdText, Length);
        // Retail pushes &GMotd (.text:0040cc4e), which ApplyServerNews ignores.
        Hooks.ApplyMotd();
    }

    void* Context = Hooks.OnLoopStart();

    long long LastTicks = Timing::read_ticks();
    srand(timeGetTime());
    while (Hooks.IsRunning())
    {
        long long Ticks = Timing::read_ticks();
        long long DeltaTicks = Ticks - LastTicks;
        double Elapsed = Timing::seconds(DeltaTicks);
        // x87: FLD period / FCOMP Elapsed / TEST AH,0x41 / JNZ skips the sleep when
        // period <= Elapsed; the fall-through (period > Elapsed) sleeps the remainder.
        if ((double)GServerFramePeriod > Elapsed)
        {
            SleepEx((DWORD)(FlLong)(((double)GServerFramePeriod - Elapsed) * 1000.0), FALSE);
        }
        DWORD FrameStart = timeGetTime();
        WaitForServerRunPermission();
        LastTicks = Ticks;
        Timing::read();
        Hooks.LockServer();
        Server.Update();
        Timing::UpdateGlobalTime(Elapsed);
        Server.ElapseTime((float)Elapsed);
        Hooks.UnlockServer();
        Timing::read();
        if (Hooks.OnFrameRun != NULL)
        {
            Hooks.OnFrameRun();
        }
        GiveClientRunPermission();
        DWORD FrameEnd = timeGetTime();
        Hooks.OnFrameDone(Context, FrameStart, FrameEnd);
    }
    if (Result == S_OK || Result == S_FALSE)
    {
        CoUninitialize();
    }
    return 0;
}

ILoadingScreen* GetServerLoadingScreen()
{
    return &GServerLoadingScreen;
}

// Inside InitInstance (.text:0040b840; the GServerVersion store is .text:0040b99d).
unsigned int ReadServerVersion(const char* ModulePath)
{
    unsigned int Major;
    unsigned int Minor;
    unsigned int Build;
    if (DACOM_GetDllVersion(ModulePath, &Major, &Minor, &Build) != 0)
    {
        Major = 1;
        Minor = 0;
        Build = 0xb;
    }
    return (((Minor & 0xff) | (Major << 8)) << 16) | (Build & 0xffff);
}

void PublishServerSession(const wchar_t* Name, const wchar_t* Password, bool bAllowPvp,
    bool bAllowNewPlayers, unsigned int ServerVersion, const wchar_t* Description)
{
    SetRemoteClientSessionName(Name);
    SetRemoteClientPassword(Password);
    // .text:0040c09a..0040c0cf: (AllowPvp, AllowNewPlayers, exe version, description,
    // server id, server signature); remoteclient.h's parameter names lag (see FLS-E handoff).
    SetRemoteClientResponseData(bAllowPvp, bAllowNewPlayers, (int)ServerVersion, Description,
        Server.GetServerID(), Server.GetServerSig());
}

bool ReadServerConfigFile(FServerConfigFile& Config, char (&Path)[0x400])
{
    Players.MakeLocalUserPath(Path, ServerConfigFileName);
    FILE* File = fopen(Path, "rb");
    if (File == nullptr)
    {
        return false;
    }
    fread(&Config.Version, 4, 1, File);
    if (Config.Version == 2 || Config.Version == 3)
    {
        fread(Config.ServerName, sizeof(Config.ServerName), 1, File);
        fread(Config.Description, sizeof(Config.Description), 1, File);
        fread(Config.Password, sizeof(Config.Password), 1, File);
        fread(&Config.bAllowNewPlayers, 1, 1, File);
        fread(&Config.bInternetAccessible, 1, 1, File);
        fread(&Config.MaxPlayers, 4, 1, File);
        if (Config.Version == 3)
        {
            fread(&Config.bAllowPvp, 1, 1, File);
        }
    }
    fclose(File);
    return true;
}

bool WriteServerConfigFile(const FServerConfigFile& Config, char (&Path)[0x400])
{
    Players.MakeLocalUserPath(Path, ServerConfigFileName);
    FILE* File = fopen(Path, "wb");
    if (File == nullptr)
    {
        return false;
    }
    fwrite(&Config.Version, 4, 1, File);
    fwrite(Config.ServerName, sizeof(Config.ServerName), 1, File);
    fwrite(Config.Description, sizeof(Config.Description), 1, File);
    fwrite(Config.Password, sizeof(Config.Password), 1, File);
    fwrite(&Config.bAllowNewPlayers, 1, 1, File);
    fwrite(&Config.bInternetAccessible, 1, 1, File);
    fwrite(&Config.MaxPlayers, 4, 1, File);
    fwrite(&Config.bAllowPvp, 1, 1, File);
    fclose(File);
    return true;
}
