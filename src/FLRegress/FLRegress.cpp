// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// Starts Server.dll the way the single-player client does, with no window, renderer, sound or
// network, and writes the resulting game state to a text file. Two runs of the same build must
// give identical files, and a change that keeps retail behaviour must not change them.
//
// Usage (from bin\, next to the game DLLs):
//     FLRegress.exe [--out <file>] [--timeout <seconds>]
// Exit code: 0 on success, 1 on a startup failure, 2 on bad arguments, 3 on a timeout.
//
// Nobody can answer a dialog on a build machine, so engine messages (FDUMP) go to stderr
// instead of DACOM's default Abort/Retry/Ignore box, and a watchdog ends the process with the
// text of any open window when the run takes longer than --timeout.

#include <windows.h>
#include <objbase.h>

#include <cfloat>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "common_ini.h"           // INI_Reader
#include "common_misc.h"          // SinglePlayer, SetMultiPlayer
#include "common_server_types.h"  // SStartupInfo
#include "dacom.h"                // FDUMP
#include "dalib_core.h"           // DALib::Startup / OpenData / CloseData / Shutdown
#include "regress_client.h"
#include "regress_dump.h"
#include "server_iserverimpl.h"   // Server

namespace
{
    struct FOptions
    {
        const char* OutPath = "regress_state.txt";
        unsigned int TimeoutSeconds = 300;
    };

    // Runs a cleanup when the scope ends, however it ends, so each startup step sits next to
    // its own teardown and every early return undoes exactly what was done before it.
    template <typename FCleanup>
    class TScopeExit
    {
    public:
        explicit TScopeExit(FCleanup InCleanup) : Cleanup(InCleanup) {}
        ~TScopeExit() { Cleanup(); }
        TScopeExit(const TScopeExit&) = delete;
        TScopeExit& operator=(const TScopeExit&) = delete;

    private:
        FCleanup Cleanup;
    };

    volatile const char* GPhase = "init";

    void Phase(const char* Name)
    {
        GPhase = Name;
        fprintf(stderr, "FLRegress: %s\n", Name);
        fflush(stderr);
    }

    // Replaces DACOM's DefaultDump, which shows a modal box for errors and warnings.
    void __cdecl RegressDump(int Flags, const char* Format, ...)
    {
        char Text[0x1000];
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Text, sizeof(Text) - 1, Format, Args);
        va_end(Args);
        Text[sizeof(Text) - 1] = '\0';
        const int Severity = (int)((unsigned int)Flags << 28) >> 28;
        fprintf(stderr, "fdump[%d] %s\n", Severity, Text);
        fflush(stderr);
    }

    BOOL CALLBACK PrintChildText(HWND Window, LPARAM)
    {
        char Text[512];
        if (GetWindowTextA(Window, Text, sizeof(Text)) > 0)
        {
            fprintf(stderr, "    %s\n", Text);
        }
        return TRUE;
    }

    BOOL CALLBACK PrintProcessWindow(HWND Window, LPARAM)
    {
        DWORD ProcessId = 0;
        GetWindowThreadProcessId(Window, &ProcessId);
        if (ProcessId != GetCurrentProcessId())
        {
            return TRUE;
        }
        char Title[256] = "";
        GetWindowTextA(Window, Title, sizeof(Title));
        fprintf(stderr, "  window \"%s\"%s\n", Title, IsWindowVisible(Window) ? "" : " (hidden)");
        EnumChildWindows(Window, PrintChildText, 0);
        return TRUE;
    }

    DWORD WINAPI WatchdogThread(LPVOID Parameter)
    {
        const DWORD Milliseconds = (DWORD)(uintptr_t)Parameter;
        Sleep(Milliseconds);
        fprintf(stderr, "FLRegress: timed out after %lu s in phase '%s'; open windows:\n",
            Milliseconds / 1000, (const char*)GPhase);
        EnumWindows(PrintProcessWindow, 0);
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 3);
        return 0;
    }

    bool ParseOptions(int Argc, char** Argv, FOptions& Options)
    {
        for (int i = 1; i < Argc; ++i)
        {
            if (strcmp(Argv[i], "--out") == 0 && i + 1 < Argc)
            {
                Options.OutPath = Argv[++i];
            }
            else if (strcmp(Argv[i], "--timeout") == 0 && i + 1 < Argc)
            {
                Options.TimeoutSeconds = (unsigned int)strtoul(Argv[++i], nullptr, 10);
            }
            else
            {
                fprintf(stderr, "FLRegress: unknown argument '%s'\n", Argv[i]);
                return false;
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

    // [Freelancer] "data path" in freelancer.ini, as FLServer's ReadFreelancerIni reads it.
    bool OpenDataFromIni(const char* IniPath)
    {
        INI_Reader Reader;
        if (!Reader.open(IniPath, false))
        {
            return false;
        }
        bool bDataPath = false;
        while (Reader.read_header())
        {
            if (!Reader.is_header("Freelancer"))
            {
                continue;
            }
            while (Reader.read_value())
            {
                if (Reader.is_value("data path"))
                {
                    DALib::OpenData(Reader.get_value_string());
                    bDataPath = true;
                }
            }
        }
        return bDataPath;
    }

    bool WriteDump(const char* Path)
    {
        const std::unique_ptr<FILE, int (*)(FILE*)> File(fopen(Path, "wb"), fclose);
        if (!File)
        {
            fprintf(stderr, "FLRegress: cannot write %s\n", Path);
            return false;
        }
        FRegressWriter Out(File.get());
        Out.Line("flregress 1");
        Out.Line("startup single_player=%d", SinglePlayer() ? 1 : 0);
        for (const FRegressClient::FCall& Call : GetRegressClient().GetTrace())
        {
            Out.Line("client_call slot=%d name=%s", Call.Slot, Call.Name);
        }
        DumpUniverse(Out);
        printf("FLRegress: wrote %s\n", Path);
        return true;
    }

    // Each guard tears down its step when Run returns; they run in reverse order, so the
    // server shuts down before the data closes and DALib shuts down last.
    int Run(const FOptions& Options)
    {
        char ModulePath[MAX_PATH] = {};
        if (!EnterModuleDirectory(ModulePath))
        {
            fprintf(stderr, "FLRegress: cannot enter the executable's directory\n");
            return 1;
        }

        // FLServer's server-thread FPU setup with exceptions masked (SetupServerFpu).
        _control87(0x9001f, 0xfffff);

        // Single player: the default of Common's flag, set explicitly so the mode is visible.
        SetMultiPlayer(false);

        Phase("DALib::Startup");
        if (!DALib::Startup(nullptr, "DACOMsrv.ini"))
        {
            fprintf(stderr, "FLRegress: DALib::Startup failed\n");
            return 1;
        }
        const TScopeExit ShutdownDALib([] { DALib::Shutdown(nullptr); });

        Phase("open data");
        char IniPath[MAX_PATH];
        GetFullPathNameA("freelancer.ini", MAX_PATH, IniPath, nullptr);
        if (!OpenDataFromIni(IniPath))
        {
            fprintf(stderr, "FLRegress: no data path in %s\n", IniPath);
            return 1;
        }
        const TScopeExit CloseData([] { DALib::CloseData(); });

        // The fields the single-player client fills (server_connect.cpp StartServer). The
        // server loads its client from ModulePath, which is this executable.
        SStartupInfo Info = {};
        strcpy(Info.ModulePath, ModulePath);
        strcpy(Info.IniPath, IniPath);
        Info.MaxPlayers = 1;
        Info.Port = 0x36d9;
        Info.bAllowPvp = 1;
        Info.bAllowNewPlayers = 1;

        Phase("Server::Startup");
        if (!Server.Startup(Info))
        {
            fprintf(stderr, "FLRegress: Server::Startup failed\n");
            return 1;
        }
        const TScopeExit ShutdownServer([] {
            Phase("shutdown");
            Server.Shutdown();
        });

        Phase("dump");
        return WriteDump(Options.OutPath) ? 0 : 1;
    }
}

int main(int Argc, char** Argv)
{
    FOptions Options;
    if (!ParseOptions(Argc, Argv, Options))
    {
        return 2;
    }

    // No Windows error boxes for missing DLLs or crashes either.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    FDUMP = RegressDump;
    if (Options.TimeoutSeconds != 0)
    {
        const DWORD Milliseconds = Options.TimeoutSeconds * 1000u;
        CloseHandle(CreateThread(nullptr, 0, WatchdogThread, (LPVOID)(uintptr_t)Milliseconds, 0, nullptr));
    }

    // FLServer runs Startup on a thread with COM initialised (StartupThreadProc).
    const bool bComInitialised = SUCCEEDED(CoInitialize(nullptr));
    const TScopeExit UninitialiseCom([bComInitialised] {
        if (bComInitialised)
        {
            CoUninitialize();
        }
    });

    const int Result = Run(Options);
    Phase("done");
    return Result;
}
