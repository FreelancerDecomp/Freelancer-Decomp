// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// Starts Server.dll the way the single-player client does, with no window, renderer, sound or
// network, and writes the resulting game state to a text file. Two runs of the same build must
// give identical files, and a change that keeps retail behaviour must not change them.
//
// Usage (from bin\, next to the game DLLs):
//     FLRegress.exe [--out <file>]
// Exit code: 0 on success, 1 on a startup failure, 2 on bad arguments.

#include <windows.h>
#include <objbase.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "common_ini.h"           // INI_Reader
#include "common_misc.h"          // SinglePlayer, SetMultiPlayer
#include "common_server_types.h"  // SStartupInfo
#include "dalib_core.h"           // DALib::Startup / OpenData / CloseData / Shutdown
#include "regress_client.h"
#include "regress_dump.h"
#include "server_iserverimpl.h"   // Server

namespace
{
    struct FOptions
    {
        const char* OutPath = "regress_state.txt";
    };

    bool ParseOptions(int Argc, char** Argv, FOptions& Options)
    {
        for (int i = 1; i < Argc; ++i)
        {
            if (strcmp(Argv[i], "--out") == 0 && i + 1 < Argc)
            {
                Options.OutPath = Argv[++i];
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

        if (!DALib::Startup(nullptr, "DACOMsrv.ini"))
        {
            fprintf(stderr, "FLRegress: DALib::Startup failed\n");
            return 1;
        }

        char IniPath[MAX_PATH];
        GetFullPathNameA("freelancer.ini", MAX_PATH, IniPath, nullptr);
        if (!OpenDataFromIni(IniPath))
        {
            fprintf(stderr, "FLRegress: no data path in %s\n", IniPath);
            DALib::Shutdown(nullptr);
            return 1;
        }

        // The fields the single-player client fills (server_connect.cpp StartServer). The
        // server loads its client from ModulePath, which is this executable.
        SStartupInfo Info;
        memset(&Info, 0, sizeof(Info));
        strcpy(Info.ModulePath, ModulePath);
        strcpy(Info.IniPath, IniPath);
        Info.MaxPlayers = 1;
        Info.Port = 0x36d9;
        Info.bAllowPvp = 1;
        Info.bAllowNewPlayers = 1;

        if (!Server.Startup(Info))
        {
            fprintf(stderr, "FLRegress: Server::Startup failed\n");
            DALib::CloseData();
            DALib::Shutdown(nullptr);
            return 1;
        }

        int Result = 0;
        FILE* File = fopen(Options.OutPath, "wb");
        if (File == nullptr)
        {
            fprintf(stderr, "FLRegress: cannot write %s\n", Options.OutPath);
            Result = 1;
        }
        else
        {
            FRegressWriter Out(File);
            Out.Line("flregress 1");
            Out.Line("startup single_player=%d", SinglePlayer() ? 1 : 0);
            for (const FRegressClient::FCall& Call : GetRegressClient().GetTrace())
            {
                Out.Line("client_call slot=%d name=%s", Call.Slot, Call.Name);
            }
            DumpUniverse(Out);
            fclose(File);
            printf("FLRegress: wrote %s\n", Options.OutPath);
        }

        Server.Shutdown();
        DALib::CloseData();
        DALib::Shutdown(nullptr);
        return Result;
    }
}

int main(int Argc, char** Argv)
{
    FOptions Options;
    if (!ParseOptions(Argc, Argv, Options))
    {
        return 2;
    }

    // FLServer runs Startup on a thread with COM initialised (StartupThreadProc).
    const HRESULT ComResult = CoInitialize(nullptr);
    const int Result = Run(Options);
    if (ComResult == S_OK || ComResult == S_FALSE)
    {
        CoUninitialize();
    }
    return Result;
}
