// Freelancer Decomp: FLDedicated, a console dedicated server -- PORT(dedicated), not part of retail.
//
// Runs the multiplayer server the way FLServer.exe does (Server.dll with RemoteClient.dll as its
// client module), without MFC, windows or dialogs. It loads the same DLLs and follows the same
// startup, frame loop and shutdown as FLServer (src/FLServer/FLServer.cpp): the parts that do
// not need MFC are shared with it (src/ServerHost/server_host.cpp); the settings
// FLServer asks for in its Server Configuration dialog come from FLServer.cfg and the command
// line instead. See docs/dedicated_server.md.
//
// Usage (from bin\, next to the game DLLs):
//     FLDedicated.exe [--name <text>] [--description <text>] [--password <text>]
//                     [--max-players <1..128>] [--port <n>] [--pvp | --no-pvp]
//                     [--new-players | --no-new-players] [--save-config]
//                     [--log <file>] [--log-level <1..7>] [--log-category <n>]
//                     [--unmask-fpu] [--run-for <seconds>]
// Console commands: status, quit. Ctrl+C or closing the console also stops the server.
// Exit code: 0 after a clean stop, 1 on a startup failure, 2 on bad arguments.

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "common_ini.h"           // INI_Reader, ILoadingScreen, RegisterLoadingScreen
#include "common_net.h"           // SetIsMPServer
#include "common_server_types.h"  // SStartupInfo, ServerStats
#include "dacom.h"                // FDUMP
#include "dalib_core.h"           // DALib::Startup / OpenData / CloseData / Shutdown, CriticalWarningFn
#include "remoteclient.h"         // GetNumClients, SetServerLogFunction
#include "server_host.h"          // the host shared with FLServer (src/ServerHost)
#include "server_iserverimpl.h"   // Server
#include "server_playerdb.h"      // Players
#include "x64_high_memory.h"      // ReserveLowAddressSpaceIfRequested

namespace
{
    // The limits of FLServer's Server Configuration dialog (EM_EXLIMITTEXT, UDM_SETRANGE).
    const size_t MaxNameLength = 0x20;
    const size_t MaxDescriptionLength = 0x80;
    const size_t MaxPasswordLength = 0x10;
    const int MaxPlayersLimit = 0x80;

    // A command-line value that may be absent.
    template <typename T>
    struct FOptional
    {
        bool bSet = false;
        T Value = T();

        void Set(const T& NewValue)
        {
            bSet = true;
            Value = NewValue;
        }
    };

    struct FOptions
    {
        FOptional<std::wstring> Name;
        FOptional<std::wstring> Description;
        FOptional<std::wstring> Password;
        FOptional<int> MaxPlayers;
        FOptional<unsigned int> Port;
        FOptional<bool> bAllowPvp;
        FOptional<bool> bAllowNewPlayers;
        bool bSaveConfig = false;
        std::string LogPath;
        int LogLevel = 4;
        std::vector<int> LogCategories = { 0x10000 };
        bool bUnmaskFpuExceptions = false;
        unsigned int RunForSeconds = 0;
    };

    FOptions GOptions;

    // The server's settings, as FServerControl holds them in FLServer.
    SStartupInfo GStartupInfo;
    std::wstring GServerName;
    std::wstring GServerDescription;
    std::wstring GServerPassword;
    unsigned int GServerVersion = 0;

    HANDLE GLogMutex = nullptr;
    FILE* GLogFile = nullptr;
    char GLogBuffer[0x400 + 1];

    // FLServer holds the named mutex for the process lifetime (.data:0042788c).
    HANDLE GCoopMutex = nullptr;
    HANDLE GServerMutex = nullptr;
    volatile LONG GServerRunning = 0;
    HANDLE GServerThread = nullptr;

    HANDLE GStopEvent = nullptr;   // set by Ctrl+C, the console's close button and "quit"
    HANDLE GDoneEvent = nullptr;   // set once shutdown has finished

    // What the server thread last measured, for the "status" command.
    CRITICAL_SECTION GStatsLock;
    ServerStats GLastStats = {};
    unsigned int GLastLoadMs = 0;
    unsigned int GFrameCount = 0;

    std::wstring GMotd;

    std::string Narrow(const std::wstring& Text)
    {
        if (Text.empty())
        {
            return std::string();
        }
        const int Size = WideCharToMultiByte(CP_ACP, 0, Text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string Result((size_t)Size, '\0');
        WideCharToMultiByte(CP_ACP, 0, Text.c_str(), -1, &Result[0], Size, nullptr, nullptr);
        Result.resize((size_t)Size - 1);
        return Result;
    }

    void WriteLogLine(const char* Line, size_t Length)
    {
        OutputDebugStringA(Line);
        fwrite(Line, 1, Length, stdout);
        fflush(stdout);
        if (GLogFile != nullptr)
        {
            fwrite(Line, 1, Length, GLogFile);
            fflush(GLogFile);
        }
    }

    // FLServer's ServerLogf (.text:0040e330) with the console view replaced by stdout: the same
    // level and category filter, the same 0x400-byte line. The c:\fl-server-errors.log copy
    // of level 0/1 lines is left out.
    int __cdecl ServerLog(ErrorCode Code, const char* Format, ...)
    {
        WaitForSingleObject(GLogMutex, INFINITE);
        const int Level = ((int)(Code.Value << 28)) >> 28;
        if (Level > GOptions.LogLevel)
        {
            ReleaseMutex(GLogMutex);
            return 0;
        }
        if (Level != 0)
        {
            const int Category = (int)Code.Value >> 4;
            const std::vector<int>& Categories = GOptions.LogCategories;
            if (std::find(Categories.begin(), Categories.end(), Category) == Categories.end())
            {
                ReleaseMutex(GLogMutex);
                return 0;
            }
        }

        va_list Args;
        va_start(Args, Format);
        _vsnprintf(GLogBuffer, 0x400, Format, Args);
        va_end(Args);
        GLogBuffer[0x400] = '\0';
        size_t Length = strlen(GLogBuffer);
        if ((Length == 0 || GLogBuffer[Length - 1] != '\n') && Length < 0x400)
        {
            GLogBuffer[Length] = '\n';
            ++Length;
            GLogBuffer[Length] = '\0';
        }
        WriteLogLine(GLogBuffer, Length);
        ReleaseMutex(GLogMutex);
        return 0;
    }

    // FLServer's own progress lines, which it logs with the log level raised to 4 for the
    // call: they show at every level, so they bypass the filter here.
    void LogStatus(const char* Text)
    {
        WaitForSingleObject(GLogMutex, INFINITE);
        std::string Line(Text);
        Line += '\n';
        WriteLogLine(Line.c_str(), Line.size());
        ReleaseMutex(GLogMutex);
    }

    // DALib's critical warning. FLServer asks Yes/No and exits on No; nobody can answer here,
    // so the warning is logged and the server carries on, as after Yes.
    bool __cdecl CriticalWarning(unsigned int Code, const char* Text)
    {
        ServerLog(ErrorCode{ 0x100001 }, "Critical warning %u: %s", Code, Text != nullptr ? Text : "");
        return true;
    }

    void PrintUsage()
    {
        fputs(
            "Usage: FLDedicated.exe [options]\n"
            "  --name <text>           server name (up to 32 characters)\n"
            "  --description <text>    server description (up to 128 characters)\n"
            "  --password <text>       join password (up to 16 characters)\n"
            "  --max-players <n>       1..128\n"
            "  --port <n>              DirectPlay host port (0: DirectPlay's default)\n"
            "  --pvp, --no-pvp         allow players to harm other players\n"
            "  --new-players, --no-new-players\n"
            "                          allow new players to join\n"
            "  --save-config           write the resulting settings to FLServer.cfg\n"
            "  --log <file>            also write the log to a file\n"
            "  --log-level <1..7>      most verbose level shown (default 4)\n"
            "  --log-category <n>      also show this log category\n"
            "  --unmask-fpu            unmask FPU zero-divide/overflow/invalid exceptions\n"
            "  --run-for <seconds>     stop after this many seconds\n"
            "Settings not given on the command line come from FLServer.cfg, as saved by\n"
            "FLServer's Server Configuration dialog.\n",
            stdout);
    }

    bool ParseInt(const wchar_t* Text, long long& Out)
    {
        wchar_t* End = nullptr;
        Out = _wcstoi64(Text, &End, 10);
        return End != Text && *End == L'\0';
    }

    bool ParseOptions(int Argc, wchar_t** Argv, FOptions& Options)
    {
        for (int i = 1; i < Argc; ++i)
        {
            const wchar_t* Arg = Argv[i];
            const wchar_t* Value = i + 1 < Argc ? Argv[i + 1] : nullptr;
            long long Number = 0;

            if (wcscmp(Arg, L"--help") == 0 || wcscmp(Arg, L"-h") == 0 || wcscmp(Arg, L"/?") == 0)
            {
                PrintUsage();
                exit(0);
            }
            else if (wcscmp(Arg, L"--pvp") == 0)
            {
                Options.bAllowPvp.Set(true);
            }
            else if (wcscmp(Arg, L"--no-pvp") == 0)
            {
                Options.bAllowPvp.Set(false);
            }
            else if (wcscmp(Arg, L"--new-players") == 0)
            {
                Options.bAllowNewPlayers.Set(true);
            }
            else if (wcscmp(Arg, L"--no-new-players") == 0)
            {
                Options.bAllowNewPlayers.Set(false);
            }
            else if (wcscmp(Arg, L"--save-config") == 0)
            {
                Options.bSaveConfig = true;
            }
            else if (wcscmp(Arg, L"--unmask-fpu") == 0)
            {
                Options.bUnmaskFpuExceptions = true;
            }
            else if (Value == nullptr)
            {
                fprintf(stderr, "FLDedicated: unknown argument or missing value: %ls\n", Arg);
                return false;
            }
            else
            {
                ++i;
                if (wcscmp(Arg, L"--name") == 0)
                {
                    Options.Name.Set(Value);
                }
                else if (wcscmp(Arg, L"--description") == 0)
                {
                    Options.Description.Set(Value);
                }
                else if (wcscmp(Arg, L"--password") == 0)
                {
                    Options.Password.Set(Value);
                }
                else if (wcscmp(Arg, L"--max-players") == 0 && ParseInt(Value, Number))
                {
                    // Out-of-range values are rejected by ConfigureServer, as by the dialog.
                    Options.MaxPlayers.Set(Number < 0 || Number > 0x7fffffffLL ? -1 : (int)Number);
                }
                else if (wcscmp(Arg, L"--port") == 0 && ParseInt(Value, Number) && Number >= 0 && Number <= 0xffff)
                {
                    Options.Port.Set((unsigned int)Number);
                }
                else if (wcscmp(Arg, L"--log") == 0)
                {
                    Options.LogPath = Narrow(Value);
                }
                else if (wcscmp(Arg, L"--log-level") == 0 && ParseInt(Value, Number) && Number >= 1 && Number <= 7)
                {
                    Options.LogLevel = (int)Number;
                }
                else if (wcscmp(Arg, L"--log-category") == 0 && ParseInt(Value, Number))
                {
                    Options.LogCategories.push_back((int)Number);
                }
                else if (wcscmp(Arg, L"--run-for") == 0 && ParseInt(Value, Number) && Number > 0 && Number <= 0x7fffffffLL / 1000)
                {
                    Options.RunForSeconds = (unsigned int)Number;
                }
                else
                {
                    fprintf(stderr, "FLDedicated: bad argument: %ls %ls\n", Arg, Value);
                    return false;
                }
            }
        }
        return true;
    }

    // The game resolves DATA, DACOM libraries and INI files relative to bin\.
    bool EnterModuleDirectory(char (&ModulePath)[MAX_PATH])
    {
        if (GetModuleFileNameA(nullptr, ModulePath, MAX_PATH) == 0)
        {
            return false;
        }
        char Directory[MAX_PATH];
        strcpy(Directory, ModulePath);
        char* Slash = strrchr(Directory, '\\');
        if (Slash == nullptr)
        {
            return false;
        }
        *Slash = '\0';
        return SetCurrentDirectoryA(Directory) != 0;
    }

    // FLServer's ReadFreelancerIni (.text:0040c190), [Freelancer] "Data path" only: AppGUID and
    // [ListServer] feed the list-server thread and [Resources] FLServer's own UI strings, which
    // this program does not have.
    bool ReadFreelancerIni(const char* Path)
    {
        INI_Reader Reader;
        bool bDataPath = false;
        if (Reader.open(Path, false))
        {
            while (Reader.read_header())
            {
                if (!Reader.is_header("Freelancer"))
                {
                    continue;
                }
                while (Reader.read_value())
                {
                    if (Reader.is_value("Data path"))
                    {
                        DALib::OpenData(Reader.get_value_string());
                        bDataPath = true;
                    }
                }
            }
        }
        return bDataPath;
    }

    // FServerControl::SetIniPath (.text:004018e0): freelancer.ini and SrvLocal-freelancer.ini
    // in the directory of Path.
    void MakeIniPaths(const char* Path, char (&IniPath)[0x104], char (&LocalIniPath)[0x104])
    {
        char Drive[3] = {};
        char Directory[0x100] = {};
        _splitpath(Path, Drive, Directory, nullptr, nullptr);
        _makepath(IniPath, Drive, Directory, "freelancer.ini", nullptr);
        _makepath(LocalIniPath, Drive, Directory, "SrvLocal-freelancer.ini", nullptr);
    }

    bool CopyLimited(const std::wstring& Value, wchar_t* Out, size_t Capacity, const char* What, size_t Limit)
    {
        if (Value.size() > Limit)
        {
            fprintf(stderr, "FLDedicated: the %s is longer than %u characters\n", What, (unsigned int)Limit);
            return false;
        }
        memset(Out, 0, Capacity * sizeof(wchar_t));
        wcsncpy(Out, Value.c_str(), Capacity - 1);
        return true;
    }

    // CServerConfigDlg::OnInitDialog / OnOK (.text:004067f0 / 00406b70) without the dialog:
    // FLServer.cfg's values, then the command line on top, then the dialog's checks.
    bool ConfigureServer()
    {
        FServerConfigFile Config = {};
        Config.Version = 3;
        Config.bAllowNewPlayers = true;
        Config.bInternetAccessible = true;
        Config.MaxPlayers = 0x10;
        Config.bAllowPvp = false;

        char Path[0x400] = "";
        if (ReadServerConfigFile(Config, Path))
        {
            Config.ServerName[0x20] = L'\0';
            Config.Description[0x80] = L'\0';
            Config.Password[0x10] = L'\0';
            ServerLog(ErrorCode{ 0x100004 }, "Read the server settings from %s", Path);
        }
        Config.Version = 3;

        const FOptions& Options = GOptions;
        if (Options.Name.bSet && !CopyLimited(Options.Name.Value, Config.ServerName, 0x21, "server name", MaxNameLength))
        {
            return false;
        }
        if (Options.Description.bSet
            && !CopyLimited(Options.Description.Value, Config.Description, 0x81, "description", MaxDescriptionLength))
        {
            return false;
        }
        if (Options.Password.bSet && !CopyLimited(Options.Password.Value, Config.Password, 0x11, "password", MaxPasswordLength))
        {
            return false;
        }
        if (Options.MaxPlayers.bSet)
        {
            Config.MaxPlayers = Options.MaxPlayers.Value;
        }
        if (Options.bAllowPvp.bSet)
        {
            Config.bAllowPvp = Options.bAllowPvp.Value;
        }
        if (Options.bAllowNewPlayers.bSet)
        {
            Config.bAllowNewPlayers = Options.bAllowNewPlayers.Value;
        }

        if (Config.MaxPlayers < 1 || Config.MaxPlayers > MaxPlayersLimit)
        {
            fprintf(stderr, "FLDedicated: the maximum number of players must be between 1 and 128\n");
            return false;
        }
        if (wcslen(Config.ServerName) == 0)
        {
            fprintf(stderr, "FLDedicated: the server needs a name: pass --name, or save one with FLServer\n");
            return false;
        }

        GStartupInfo.MaxPlayers = (unsigned int)Config.MaxPlayers;
        GServerDescription = Config.Description;
        GServerName = Config.ServerName;
        GStartupInfo.bAllowNewPlayers = Config.bAllowNewPlayers ? 1 : 0;
        GServerPassword = Config.Password;
        GStartupInfo.bAllowPvp = Config.bAllowPvp ? 1 : 0;
        if (Options.Port.bSet)
        {
            GStartupInfo.Port = Options.Port.Value;
        }

        Players.BuildLocalUserDir();
        if (Options.bSaveConfig)
        {
            if (!WriteServerConfigFile(Config, Path))
            {
                fprintf(stderr, "FLDedicated: cannot write %s\n", Path);
                return false;
            }
            ServerLog(ErrorCode{ 0x100004 }, "Saved the server settings to %s", Path);
        }
        return true;
    }

    // FLServer's StartupThreadProc (.text:0040be90): IServerImpl::Startup on a thread of its
    // own with COM initialised; exit code 1 when it succeeded.
    DWORD WINAPI StartupThreadProc(LPVOID)
    {
        return RunServerStartup(GStartupInfo);
    }

    // FLServer's ShutdownThreadProc (.text:0040b710).
    DWORD WINAPI ShutdownThreadProc(LPVOID)
    {
        return RunServerShutdown();
    }

    // The server thread's FLDedicated parts; the loop itself is RunServerThread
    // (src/ServerHost/server_host.cpp).
    bool IsServerRunning()
    {
        return GServerRunning != 0;
    }

    // ApplyServerNews (.text:00413780).
    void ApplyMotd()
    {
        WaitForSingleObject(GServerMutex, INFINITE);
        Players.SetMOTD(GMotd);
        ReleaseMutex(GServerMutex);
    }

    void* OnServerLoopStart()
    {
        SetupServerFpu(GOptions.bUnmaskFpuExceptions);
        LogStatus("Server running, ready for log in.");
        return nullptr;
    }

    void LockServer()
    {
        WaitForSingleObject(GServerMutex, INFINITE);
    }

    void UnlockServer()
    {
        ReleaseMutex(GServerMutex);
    }

    // The frame time and ServerStats go to the "status" command instead of the summary form.
    void OnServerFrameDone(void*, DWORD FrameStart, DWORD FrameEnd)
    {
        ServerStats Stats;
        Server.GetServerStats(Stats);
        EnterCriticalSection(&GStatsLock);
        GLastStats = Stats;
        GLastLoadMs = (unsigned int)(FrameEnd - FrameStart);
        ++GFrameCount;
        LeaveCriticalSection(&GStatsLock);
    }

    // FLServer's ServerThreadProc (.text:0040cb40): loads motd.dat, then runs
    // IServerImpl::Update / ElapseTime at 30 Hz under the server mutex, handing the run
    // permission back and forth with the client side.
    DWORD WINAPI ServerThreadProc(LPVOID)
    {
        FServerThreadHooks Hooks;
        Hooks.IsRunning = IsServerRunning;
        Hooks.Motd = &GMotd;
        Hooks.ApplyMotd = ApplyMotd;
        Hooks.OnLoopStart = OnServerLoopStart;
        Hooks.LockServer = LockServer;
        Hooks.UnlockServer = UnlockServer;
        Hooks.OnFrameRun = nullptr;
        Hooks.OnFrameDone = OnServerFrameDone;
        return RunServerThread(Hooks);
    }

    void PrintStatus()
    {
        EnterCriticalSection(&GStatsLock);
        const ServerStats Stats = GLastStats;
        const unsigned int LoadMs = GLastLoadMs;
        const unsigned int Frames = GFrameCount;
        LeaveCriticalSection(&GStatsLock);

        char Line[0x200];
        _snprintf(Line, sizeof(Line) - 1,
            "Status: %u players (of %u), %d connections, frame %u ms, %u frames, "
            "%u logins, %u failed logins, %u logouts\n",
            Stats.PlayerCount, GStartupInfo.MaxPlayers, GetNumClients(), LoadMs, Frames,
            Stats.LoginSuccessCount, Stats.LoginFailCount, Stats.LogoutCount);
        Line[sizeof(Line) - 1] = '\0';
        WaitForSingleObject(GLogMutex, INFINITE);
        WriteLogLine(Line, strlen(Line));
        ReleaseMutex(GLogMutex);
    }

    // Reads console commands from stdin. End of input (a closed or redirected stdin) only ends
    // this thread: the server then runs until Ctrl+C or --run-for.
    DWORD WINAPI CommandThreadProc(LPVOID)
    {
        char Line[0x100];
        while (fgets(Line, sizeof(Line), stdin) != nullptr)
        {
            char* Command = Line;
            while (*Command == ' ' || *Command == '\t')
            {
                ++Command;
            }
            size_t Length = strlen(Command);
            while (Length > 0 && (Command[Length - 1] == '\n' || Command[Length - 1] == '\r'
                || Command[Length - 1] == ' ' || Command[Length - 1] == '\t'))
            {
                Command[--Length] = '\0';
            }
            if (Length == 0)
            {
                continue;
            }
            if (_stricmp(Command, "quit") == 0 || _stricmp(Command, "exit") == 0 || _stricmp(Command, "stop") == 0)
            {
                SetEvent(GStopEvent);
                return 0;
            }
            if (_stricmp(Command, "status") == 0)
            {
                PrintStatus();
            }
            else
            {
                LogStatus("Commands: status, quit");
            }
        }
        return 0;
    }

    BOOL WINAPI ConsoleCtrlHandler(DWORD Type)
    {
        SetEvent(GStopEvent);
        if (Type == CTRL_CLOSE_EVENT || Type == CTRL_LOGOFF_EVENT || Type == CTRL_SHUTDOWN_EVENT)
        {
            // Windows ends the process when this handler returns: give the shutdown (which
            // saves the players) the few seconds Windows allows.
            WaitForSingleObject(GDoneEvent, 4500);
        }
        return TRUE;
    }

    // FLServer's BeginShutdown (.text:0040b510) without the UI pumping: stop the server thread
    // (killed after 600 waits of 100 ms), then IServerImpl::Shutdown on its own thread.
    void StopServer()
    {
        if (GServerRunning != 0)
        {
            InterlockedExchange(&GServerRunning, 0);
            if (GServerThread != nullptr)
            {
                if (WaitForSingleObject(GServerThread, 600 * 100) == WAIT_TIMEOUT)
                {
                    LogStatus("The server thread does not stop; ending it.");
                    TerminateThread(GServerThread, 0);
                }
                CloseHandle(GServerThread);
                GServerThread = nullptr;
            }
        }

        LogStatus("Shutting down...");
        HANDLE ShutdownThread = CreateThread(nullptr, 0, ShutdownThreadProc, nullptr, CREATE_SUSPENDED, nullptr);
        if (ShutdownThread != nullptr)
        {
            SetThreadPriority(ShutdownThread, THREAD_PRIORITY_HIGHEST);
            ResumeThread(ShutdownThread);
            WaitForSingleObject(ShutdownThread, INFINITE);
            CloseHandle(ShutdownThread);
        }
        else
        {
            Server.Shutdown();
        }
    }

    int Run()
    {
        char ModulePath[MAX_PATH] = {};
        if (!EnterModuleDirectory(ModulePath))
        {
            fprintf(stderr, "FLDedicated: cannot enter the executable's directory\n");
            return 1;
        }

        // CFLServerApp::InitInstance (.text:0040b840), in its order, minus the EULA, the
        // windows, the crash reporter and the configuration dialog.
        GServerVersion = ReadServerVersion(ModulePath);

        SetIsMPServer(true);
        RegisterLoadingScreen(GetServerLoadingScreen());

        // FServerControl's ctor, then InitInstance's SetServerPort(0) / SetMaxPlayers(16).
        memset(&GStartupInfo, 0, sizeof(GStartupInfo));
        GStartupInfo.bAllowNewPlayers = 1;
        GStartupInfo.Port = 0;
        GStartupInfo.MaxPlayers = 0x10;

        SetupServerFpu(GOptions.bUnmaskFpuExceptions);

        char IniPath[0x104];
        char LocalIniPath[0x104];
        MakeIniPaths(ModulePath, IniPath, LocalIniPath);
        strcpy(GStartupInfo.IniPath, IniPath);
        strcpy(GStartupInfo.ModulePath, "RemoteClient.dll");

        ServerLog(ErrorCode{ 0x100004 }, "FLDedicated %u.%u.%u", (GServerVersion >> 24) & 0xff,
            (GServerVersion >> 16) & 0xff, GServerVersion & 0xffff);
        LogStatus("Loading initial data...");

        if (!DALib::Startup(nullptr, "DACOMsrv.ini"))
        {
            fprintf(stderr, "FLDedicated: DALib::Startup failed\n");
            return 1;
        }
        const bool bDataPath = ReadFreelancerIni(IniPath);
        const bool bLocalDataPath = ReadFreelancerIni(LocalIniPath);
        if (!bDataPath && !bLocalDataPath)
        {
            ServerLog(ErrorCode{ 0x100002 }, "No [Freelancer] data path in %s", IniPath);
        }

        if (!ConfigureServer())
        {
            DALib::CloseData();
            DALib::Shutdown(nullptr);
            return 2;
        }

        SetServerLogFunction(ServerLog);

        HANDLE StartupThread = CreateThread(nullptr, 0, StartupThreadProc, nullptr, 0, nullptr);
        if (StartupThread == nullptr)
        {
            DALib::CloseData();
            DALib::Shutdown(nullptr);
            return 1;
        }
        WaitForSingleObject(StartupThread, INFINITE);
        DWORD ExitCode = 0;
        GetExitCodeThread(StartupThread, &ExitCode);
        CloseHandle(StartupThread);

        // CFLServerApp::OnServerStartupFinished (.text:0040bf50).
        int Result = 0;
        if (ExitCode == 1)
        {
            InterlockedExchange(&GServerRunning, 1);
            LogStatus("Starting logic thread...");
            GServerThread = CreateThread(nullptr, 0, ServerThreadProc, nullptr, 0, nullptr);
            if (GServerThread == nullptr)
            {
                InterlockedExchange(&GServerRunning, 0);
                Result = 1;
            }
        }
        else
        {
            fprintf(stderr, "FLDedicated: IServerImpl::Startup failed\n");
            Result = 1;
        }

        if (Result == 0)
        {
            PublishServerSession(GServerName.c_str(), GServerPassword.c_str(), GStartupInfo.bAllowPvp != 0,
                GStartupInfo.bAllowNewPlayers != 0, GServerVersion, GServerDescription.c_str());

            ServerLog(ErrorCode{ 0x100004 }, "Server \"%s\": %u players, port %u, PvP %s, new players %s%s",
                Narrow(GServerName).c_str(), GStartupInfo.MaxPlayers, GStartupInfo.Port,
                GStartupInfo.bAllowPvp ? "on" : "off", GStartupInfo.bAllowNewPlayers ? "on" : "off",
                GServerPassword.empty() ? "" : ", password set");

            CloseHandle(CreateThread(nullptr, 0, CommandThreadProc, nullptr, 0, nullptr));
            const DWORD Timeout = GOptions.RunForSeconds != 0 ? GOptions.RunForSeconds * 1000u : INFINITE;
            if (WaitForSingleObject(GStopEvent, Timeout) == WAIT_TIMEOUT)
            {
                LogStatus("Run time is over.");
                PrintStatus();
            }
        }

        StopServer();
        DALib::CloseData();
        DALib::Shutdown(nullptr);
        LogStatus(Result == 0 ? "Server stopped." : "Server failed to start.");
        return Result;
    }
}

int wmain(int Argc, wchar_t** Argv)
{
    ReserveLowAddressSpaceIfRequested();

    if (!ParseOptions(Argc, Argv, GOptions))
    {
        PrintUsage();
        return 2;
    }

    // No Windows error boxes for missing DLLs or crashes.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    GLogMutex = CreateMutexA(nullptr, FALSE, nullptr);
    GServerMutex = CreateMutexA(nullptr, FALSE, nullptr);
    GCoopMutex = CreateMutexA(nullptr, FALSE, "FreelancerCoopMutex");
    GStopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    GDoneEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    InitializeCriticalSection(&GStatsLock);

    if (!GOptions.LogPath.empty())
    {
        GLogFile = fopen(GOptions.LogPath.c_str(), "w");
        if (GLogFile == nullptr)
        {
            fprintf(stderr, "FLDedicated: cannot write the log %s\n", GOptions.LogPath.c_str());
            return 2;
        }
    }

    // FLServer's console view routes DACOM's FDUMP into ServerLogf (CFLServerView ctor).
    FDUMP = (void(__cdecl*)(int, const char*, ...))ServerLog;
    CriticalWarningFn = CriticalWarning;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // AfxOleInit in InitInstance.
    const HRESULT ComResult = CoInitialize(nullptr);
    const int Result = Run();
    if (ComResult == S_OK || ComResult == S_FALSE)
    {
        CoUninitialize();
    }

    if (GLogFile != nullptr)
    {
        fclose(GLogFile);
        GLogFile = nullptr;
    }
    SetEvent(GDoneEvent);
    return Result;
}
