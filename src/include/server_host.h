// Freelancer Decomp: the server host shared by FLServer.exe and FLDedicated.exe.
//
// The parts of FLServer's host that do not need MFC: the FPU setup, the startup and shutdown
// thread bodies, the 30 Hz server thread, the loading screen, the exe version and the
// FLServer.cfg file. src/ServerHost/server_host.cpp is compiled into both programs (as
// src/RemoteCommon is into both RemoteClient and RemoteServer), so each has its own copy.
// The addresses are FLServer.exe's: FLServer is the retail reference and its behaviour
// does not change. Each program keeps what differs between them (the log, the critical
// warning, the UI around startup and shutdown, its own ReadFreelancerIni) and passes the rest
// in through FServerThreadHooks. Builds for Win32 and x64 (FLDedicated); no MFC here.
#pragma once

#include <windows.h>

#include <string>

#include "common_server_types.h"   // SStartupInfo

class ILoadingScreen;

// The FPU setup of InitInstance (.text:0040bae3) and the server thread (.text:0040cc6c):
// "unmask FPU exceptions" clears the zero-divide / overflow / invalid masks of the current
// control word, otherwise every exception is masked with 53-bit precision.
void SetupServerFpu(bool bUnmaskExceptions);

// .text:0040be90 -- the body of the startup thread: IServerImpl::Startup with COM initialised.
// Returns 1 when it succeeded (the thread's exit code), 0 otherwise.
unsigned int RunServerStartup(const SStartupInfo& Info);

// .text:0040b710 -- the body of the shutdown thread: IServerImpl::Shutdown with COM
// initialised. Returns 1.
unsigned int RunServerShutdown();

// What the server thread does differently in each program. Every hook is called on the server
// thread; OnFrameRun may be null.
struct FServerThreadHooks
{
    // The loop runs while this returns true (FLServer's GServerRunning).
    bool (*IsRunning)();
    // Receives motd.dat's text; ApplyMotd is then called to hand it to PlayerDB.
    std::wstring* Motd;
    void (*ApplyMotd)();
    // Called once before the loop (the FPU setup and the "running" log line). Its result is
    // handed to OnFrameDone.
    void* (*OnLoopStart)();
    // Take and release the server mutex around IServerImpl::Update / ElapseTime.
    void (*LockServer)();
    void (*UnlockServer)();
    // After the frame's second Timing::read, before GiveClientRunPermission.
    void (*OnFrameRun)();
    // After GiveClientRunPermission, with the timeGetTime values around the frame.
    void (*OnFrameDone)(void* Context, DWORD FrameStart, DWORD FrameEnd);
};

// .text:0040cb40 -- the server thread: loads motd.dat, then runs IServerImpl::Update /
// ElapseTime at 30 Hz under the server mutex, handing the run permission back and forth with
// the client side. Returns the thread's exit code (0).
DWORD RunServerThread(const FServerThreadHooks& Hooks);

// .data:004278a0 -- the loading screen InitInstance registers with Common (does nothing).
ILoadingScreen* GetServerLoadingScreen();

// InitInstance's version (.text:0040b99d): DACOM_GetDllVersion of the exe, 1.0.11 when that
// fails, packed as (major&0xff | minor<<8) << 16 | build&0xffff.
unsigned int ReadServerVersion(const char* ModulePath);

// The end of OnServerStartupFinished (.text:0040bf50; SetRemoteClientPassword at .text:0040c080,
// the response data at .text:0040c09a..0040c0cf): hands the session name, the password
// and the host discovery response data to RemoteClient.dll.
void PublishServerSession(const wchar_t* Name, const wchar_t* Password, bool bAllowPvp,
    bool bAllowNewPlayers, unsigned int ServerVersion, const wchar_t* Description);

// FLServer.cfg in the local user directory -- written and read field by field in this order
// (binary, "wb" / "rb") by CServerConfigDlg::OnInitDialog / OnOK (.text:004067f0 / 00406b70).
// Version 2 files lack the trailing bAllowPvp byte. Retail keeps every field as a separate
// local of OnInitDialog / OnOK.
struct FServerConfigFile
{
    int Version;                   // 3 when written
    wchar_t ServerName[0x21];      // 0x42 bytes
    wchar_t Description[0x81];     // 0x102 bytes
    wchar_t Password[0x11];        // 0x22 bytes
    bool bAllowNewPlayers;         // 1 byte
    bool bInternetAccessible;      // 1 byte
    int MaxPlayers;                // 4 bytes
    bool bAllowPvp;                // 1 byte, version 3 only
};

extern const char* const ServerConfigFileName;   // "FLServer.cfg"

// OnInitDialog's read: puts FLServer.cfg's path in Path and, when the file opens, reads a
// version 2 or 3 file over Config (other versions leave all but Version alone). Returns
// whether the file opened.
bool ReadServerConfigFile(FServerConfigFile& Config, char (&Path)[0x400]);

// OnOK's write (after PlayerDB::BuildLocalUserDir, which the caller does): puts FLServer.cfg's
// path in Path and writes Config as version Config.Version. Returns whether the file opened.
bool WriteServerConfigFile(const FServerConfigFile& Config, char (&Path)[0x400]);
