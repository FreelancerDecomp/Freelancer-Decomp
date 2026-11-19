// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// Starts Server.dll as a multiplayer server, as FLServer.exe does, but with FLRegress itself as
// the client module instead of RemoteClient.dll: no window, renderer, sound or network. It
// writes the resulting game state to a text file. Two runs of the same build must give
// identical files, and a change that keeps retail behaviour must not change them.
//
// Stage A dumps the universe right after startup. Stage B then plays a scripted player (a new
// account and character, enter and leave the start base, launch into space), runs the server
// for --frames fixed 1/30 s frames on a virtual clock (regress_hooks.h), then dumps every game
// object and the calls the server made to the client. See docs/flregress.md.
//
// Usage (from bin\, next to the game DLLs):
//     FLRegress.exe [--out <file>] [--timeout <seconds>] [--frames <count>]
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
#include <cwchar>
#include <map>
#include <utility>

#include "common_ini.h"           // INI_Reader
#include "common_misc.h"          // SinglePlayer
#include "common_net.h"           // SetIsMPServer
#include "common_newchar.h"       // NewChar::TheDB, FNewCharFaction
#include "common_server_types.h"  // SStartupInfo, SLoginInfo, SCreateCharacterInfo
#include "common_timing.h"        // Timing::UpdateGlobalTime
#include "dacom.h"                // FDUMP
#include "dalib_core.h"           // DALib::Startup / OpenData / CloseData / Shutdown
#include "regress_client.h"
#include "regress_dump.h"
#include "regress_hooks.h"
#include "server_iserverimpl.h"   // Server

namespace
{
    struct FOptions
    {
        const char* OutPath = "regress_state.txt";
        unsigned int TimeoutSeconds = 300;
        unsigned int Frames = 900;    // stage B: 30 s of game time after the launch
    };

    // The scripted player's session (the single-player client's connection id).
    const unsigned int SessionId = 1;
    // FLServer's frame period (ServerThreadProc), as a fixed step.
    const double FrameSeconds = 1.0 / 30.0;
    // Frames run docked, between BaseEnter and BaseExit.
    const unsigned int BaseFrames = 30;
    // The sandbox user folder beside the executable (regress_hooks.h), emptied at every start.
    const char* const SandboxFolderName = "FLRegressUser";

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
            else if (strcmp(Argv[i], "--frames") == 0 && i + 1 < Argc)
            {
                Options.Frames = (unsigned int)strtoul(Argv[++i], nullptr, 10);
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

    // Deletes Path and everything under it. Only ever called on the sandbox folder.
    void DeleteTree(const char* Path)
    {
        char Pattern[MAX_PATH];
        snprintf(Pattern, sizeof(Pattern), "%s\\*", Path);
        WIN32_FIND_DATAA Found;
        HANDLE Find = FindFirstFileA(Pattern, &Found);
        if (Find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (strcmp(Found.cFileName, ".") == 0 || strcmp(Found.cFileName, "..") == 0)
                {
                    continue;
                }
                char Child[MAX_PATH];
                snprintf(Child, sizeof(Child), "%s\\%s", Path, Found.cFileName);
                if ((Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    DeleteTree(Child);
                }
                else
                {
                    SetFileAttributesA(Child, FILE_ATTRIBUTE_NORMAL);
                    DeleteFileA(Child);
                }
            } while (FindNextFileA(Find, &Found));
            FindClose(Find);
        }
        RemoveDirectoryA(Path);
    }

    // One server frame as FLServer's server thread runs it (Update, then the global time and
    // ElapseTime with the frame's elapsed time), on the virtual clock and without the run-
    // permission handshake: nothing else runs in this process.
    void RunFrame()
    {
        RegressHooks::Advance(FrameSeconds);
        Server.Update();
        Timing::UpdateGlobalTime(FrameSeconds);
        Server.ElapseTime((float)FrameSeconds);
    }

    void RunFrames(FRegressWriter& Out, unsigned int Count, unsigned int& Frame)
    {
        for (unsigned int i = 0; i < Count; ++i)
        {
            RunFrame();
            ++Frame;
            if (Frame % 30 == 0)
            {
                DumpObjectCounts(Out, Frame);
            }
        }
    }

    // How often the server called each IClient slot, by slot.
    void DumpCallCounts(FRegressWriter& Out)
    {
        std::map<int, std::pair<const char*, unsigned int>> Counts;
        for (const FRegressClient::FCall& Call : GetRegressClient().GetTrace())
        {
            std::pair<const char*, unsigned int>& Entry = Counts[Call.Slot];
            Entry.first = Call.Name;
            ++Entry.second;
        }
        for (const auto& Entry : Counts)
        {
            Out.Line("client_calls slot=%d name=%s count=%u", Entry.first, Entry.second.first, Entry.second.second);
        }
    }

    // The new character's choices: the first faction of the new-character database
    // (DATA\characters\newcharacter.ini, ascending nickname id) with its first base, package
    // and pilot -- what a multiplayer client offers when nothing is picked.
    struct FNewCharacterChoice
    {
        bool bFound = false;
        unsigned int Faction = 0;
        unsigned int Base = 0;
        unsigned int Package = 0;
        unsigned int Pilot = 0;
    };

    bool __cdecl PickFirstFaction(unsigned int NicknameId, const FNewCharFaction* Faction, void* Context)
    {
        FNewCharacterChoice& Choice = *static_cast<FNewCharacterChoice*>(Context);
        if (Faction == nullptr || Faction->Bases.empty() || Faction->Packages.empty() || Faction->Pilots.empty())
        {
            return true;   // keep looking
        }
        Choice.bFound = true;
        Choice.Faction = NicknameId;
        Choice.Base = *Faction->Bases.begin();
        Choice.Package = *Faction->Packages.begin();
        Choice.Pilot = *Faction->Pilots.begin();
        return false;      // stop
    }

    // Stage B's scripted player: the IServer calls a multiplayer client makes for a new account
    // (login with account creation, the character list, a new character, character select; the
    // base menu's BaseEnter / BaseExit; the space state's RequestCreateShip; the ship's
    // PlayerLaunch and LaunchComplete). Each step records what the server answered; a missing
    // answer ends the script there, and the frames and the dump still run so the file shows how
    // far it got.
    bool PlayScript(FRegressWriter& Out, unsigned int& Frame)
    {
        FRegressClient& Client = GetRegressClient();
        const FRegressClient::FPlayerState& Player = Client.GetPlayer();

        Phase("stage B: login");
        Server.OnConnect(SessionId);
        SLoginInfo Login;
        memset(&Login, 0, sizeof(Login));
        wcsncpy(Login.AccountName, L"FLRegress", 0x28);
        Login.AccountName[0x27] = L'\0';
        // The sandbox starts without accounts: PlayerDB::load_user_data answers 2 for a missing
        // account folder unless the login asks for the account to be created.
        Login.CreateFlag = 1;
        Server.Login(Login, SessionId);
        Out.Line("player login answered=%d status=%u", Player.bLoginResponse ? 1 : 0,
            (unsigned int)Player.LoginStatus);
        if (!Player.bLoginResponse || Player.LoginStatus != 3)
        {
            return false;
        }

        Phase("stage B: new character");
        Server.CharacterInfoReq(SessionId, false);
        FNewCharacterChoice Choice;
        if (NewChar::TheDB != nullptr)
        {
            NewChar::TheDB->ForEachFaction(&PickFirstFaction, &Choice);
        }
        Out.Line("player new_character found=%d faction=%08x base=%08x package=%08x pilot=%08x",
            Choice.bFound ? 1 : 0, Choice.Faction, Choice.Base, Choice.Package, Choice.Pilot);
        if (!Choice.bFound)
        {
            return false;
        }
        SCreateCharacterInfo Create;
        memset(&Create, 0, sizeof(Create));
        wcsncpy(Create.Name, L"FLRegress", 23);
        Create.Field_0x30 = Choice.Faction;
        Create.Field_0x34 = Choice.Base;
        Create.Field_0x38 = Choice.Package;
        Create.Field_0x3c = Choice.Pilot;
        // On success the server answers with the new character list (CharacterInfoReq).
        Server.CreateNewCharacter(Create, SessionId);
        Out.Line("player characters=%u first=%s system=%08x base=%08x", Player.CharacterCount,
            Player.bHaveCharacter ? Player.CharacterId.Buffer : "", Player.CharacterSystemId,
            Player.CharacterBaseId);
        if (!Player.bHaveCharacter)
        {
            return false;
        }

        Phase("stage B: select character");
        Server.CharacterSelect(Player.CharacterId, SessionId);

        Phase("stage B: base");
        Server.BaseEnter(Player.CharacterBaseId, SessionId);
        RunFrames(Out, BaseFrames, Frame);
        Server.BaseExit(Player.CharacterBaseId, SessionId);

        Phase("stage B: launch");
        Server.RequestCreateShip(SessionId);
        Out.Line("player create_ship answered=%d ok=%d ship=%08x", Player.bCreateShipResponse ? 1 : 0,
            Player.bCreateShipOk ? 1 : 0, Player.CreatedShipId);
        if (!Player.bCreateShipOk)
        {
            return false;
        }
        Server.PlayerLaunch(Player.CreatedShipId, SessionId);
        Out.Line("player launch answered=%d ship=%08x launch_object=%08x launch_kind=%d",
            Player.bLaunched ? 1 : 0, Player.LaunchShipId, Player.LaunchObjectId, (int)Player.LaunchKind);
        if (!Player.bLaunched)
        {
            return false;
        }
        Server.LaunchComplete(Player.LaunchObjectId, Player.CreatedShipId);
        return true;
    }

    void RunStageB(FRegressWriter& Out, unsigned int Frames)
    {
        // Content.dll and the other modules Startup loaded get their imports patched too.
        RegressHooks::Install();
        // A fixed seed for the simulation; every module shares the CRT's rand state.
        srand(1);
        GetRegressClient().ClearTrace();

        Out.Line("stage_b frames=%u base_frames=%u frame_seconds=1/30", Frames, BaseFrames);
        unsigned int Frame = 0;
        const bool bInSpace = PlayScript(Out, Frame);
        if (!bInSpace)
        {
            fprintf(stderr, "FLRegress: the scripted player did not reach space (see the dump)\n");
        }
        Out.Line("player in_space=%d", bInSpace ? 1 : 0);

        Phase("stage B: frames");
        RunFrames(Out, Frames, Frame);

        Phase("stage B: dump");
        Out.Line("stage_b_end frame=%u", Frame);
        DumpCallCounts(Out);
        DumpObjects(Out);
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

        // A fresh, private user folder for accounts and saves (regress_hooks.h), and the
        // import patches of the modules loaded with the executable.
        char Sandbox[MAX_PATH];
        GetFullPathNameA(SandboxFolderName, MAX_PATH, Sandbox, nullptr);
        DeleteTree(Sandbox);
        CreateDirectoryA(Sandbox, nullptr);
        RegressHooks::SetDocumentsFolder(Sandbox);
        RegressHooks::Install();

        // FLServer's server-thread FPU setup with exceptions masked (SetupServerFpu).
        _control87(0x9001f, 0xfffff);

        // A multiplayer server, as FLServer.exe and FLDedicated.exe start it. In single player the
        // server takes each system's solars from the client sharing its process
        // (StarSystem::load_solars), so a run without a client could not launch a ship; the
        // multiplayer server builds them from the system files itself.
        SetIsMPServer(true);

        Phase("DALib::Startup");
        if (!DALib::Startup(nullptr, "DACOMsrv.ini"))
        {
            fprintf(stderr, "FLRegress: DALib::Startup failed\n");
            return 1;
        }

        Phase("open data");
        char IniPath[MAX_PATH];
        GetFullPathNameA("freelancer.ini", MAX_PATH, IniPath, nullptr);
        if (!OpenDataFromIni(IniPath))
        {
            fprintf(stderr, "FLRegress: no data path in %s\n", IniPath);
            DALib::Shutdown(nullptr);
            return 1;
        }

        // The fields the single-player client fills (server_connect.cpp StartServer). The
        // server loads its client from ModulePath, which is this executable (FLServer names
        // RemoteClient.dll there).
        SStartupInfo Info;
        memset(&Info, 0, sizeof(Info));
        strcpy(Info.ModulePath, ModulePath);
        strcpy(Info.IniPath, IniPath);
        Info.MaxPlayers = 1;
        Info.Port = 0x36d9;
        Info.bAllowPvp = 1;
        Info.bAllowNewPlayers = 1;

        // DALib::Startup loaded the DACOM libraries since the first patch. From here on the game
        // modules run on the virtual clock: Startup seeds rand and creates the server's account
        // id from it, which then repeat as well.
        RegressHooks::Install();
        RegressHooks::EnableVirtualClock();

        Phase("Server::Startup");
        if (!Server.Startup(Info))
        {
            fprintf(stderr, "FLRegress: Server::Startup failed\n");
            DALib::CloseData();
            DALib::Shutdown(nullptr);
            return 1;
        }

        Phase("dump");
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
            Out.Line("flregress 2");
            Out.Line("startup single_player=%d", SinglePlayer() ? 1 : 0);
            for (const FRegressClient::FCall& Call : GetRegressClient().GetTrace())
            {
                Out.Line("client_call slot=%d name=%s", Call.Slot, Call.Name);
            }
            DumpUniverse(Out);
            fflush(File);

            RunStageB(Out, Options.Frames);
            fclose(File);
            printf("FLRegress: wrote %s\n", Options.OutPath);
        }

        Phase("shutdown");
        Server.Shutdown();
        DALib::CloseData();
        DALib::Shutdown(nullptr);
        Phase("done");
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

    // No Windows error boxes for missing DLLs or crashes either.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    FDUMP = RegressDump;
    if (Options.TimeoutSeconds != 0)
    {
        const DWORD Milliseconds = Options.TimeoutSeconds * 1000u;
        CloseHandle(CreateThread(nullptr, 0, WatchdogThread, (LPVOID)(uintptr_t)Milliseconds, 0, nullptr));
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
