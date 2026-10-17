// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_cliobj.h"     // FlushMaterialCache (FUN_0041b150)
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "client_frame_tick.h"
#include "client_ime_focus.h" // ReleaseClientIMEFocus (FUN_005641d0)
#include "client_state.h"
#include "intro_connect.h"

#include "common_ini.h"
#include "common_id.h"
#include "common_reputation.h"
#include "common_async.h"        // Async::Shutdown
#include "common_dataconfig.h"   // DataConfig::shutdown
#include "common_errorhandler.h" // ErrorHandler::activate / deactivate / new_log
#include "common_misc.h"         // GetUserLocalDataPath
#include "common_text.h"
#include "character_info.h"   // ShutdownCharacterInfo (FUN_00435500)
#include "chat_init.h"        // ChatShutdown (FUN_00469b30)
#include "client_debris.h"    // ShutdownDebrisArchDb (FUN_00530fa0)
#include "connect_flow.h"
#include "effect_base.h"      // EffectSystemShutdown (FUN_004f7140)
#include "vis_effect_arch.h"  // EffectArchShutdown (FUN_0040ef30)
#include "font_imp.h"         // FontShutdown (FUN_004174a0)
#include "client_tick_profile.h" // Port-only diagnostics, released before fonts
#include "gameflow.h"
#include "globals.h"
#include "graph_library.h"
#include "dacom.h"
#include "DALib.h"
#include "dalib_net.h"
#include "ids_strings.h"
#include "loading_screen.h"   // LoadingScreenShutdown (FUN_005791c0)
#include "menu_keymap_config.h"
#include "menu_sound_arch.h"
#include "menu_ui_init.h"
#include "login_mgr.h"
#include "mission_briefing.h"  // GetMissionBriefingState (FUN_0043d560)
#include "nn_error.h"
#include "nn_modal.h"         // NnSystemShutdown (FUN_0045a140)
#include "nn_montage_draw.h"  // ClearButtonMontage (FUN_00559d20)
#include "nn_rollover.h"      // NnRolloverDestroyGlobal (FUN_004b6580)
#include "nn_tooltip.h"       // GetNnToolTipSingleton (DAT_0067244c)
#include "space_state.h"      // GetSpaceState (FUN_0054bd30)
#include "win3d.h"
#include "server_connect.h"
#include "texture_shapes.h"   // ClearEffectShapesFiles (FUN_004f73b0)
#include "ui_scroll.h"
#include "voice_profiles.h"
#ifdef FREELANCER_RP9
#include "render_ex.h"        // RenderEx::Shutdown (port-only Direct3D 9 extensions)
#endif

#include <ctype.h>
#include <float.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// DAT_0067ecc0 -- "+G"/"-G" (FUN_005b2cb0); read by CLoginMgr::Login (FUN_005aa510), hence
// outside the anonymous namespace (declared in login_mgr.h).
bool G_bCommandLineG = false;

namespace
{
    // DAT_0067e670 -- the application name FUN_005a5910 hands Dr. Watson (0x80 wchar, the last
    // one forced to zero at DAT_0067e76e).
    wchar_t G_CrashAppName[0x80];

    // DAT_0067de50 -- the '|'-separated list of files Dr. Watson attaches: FUN_005b2800 puts the
    // spew log path here, FUN_005a5390 appends the dxdiag report. 0x410 wchar, ending right
    // where G_CrashAppName begins in retail's .data.
    wchar_t G_CrashReportFiles[0x410];

    // FUN_005a5390 (.text:005a5390-005a54dd): run "dxdiag.exe <temp>\dxdiag.txt", wait up to 60 s
    // and, unless it timed out, append the report path to G_CrashReportFiles.
    void AppendDxDiagReport()
    {
        STARTUPINFOA StartupInfo;
        memset(&StartupInfo, 0, sizeof(StartupInfo));
        PROCESS_INFORMATION ProcessInfo;
        ProcessInfo.hProcess = nullptr;
        ProcessInfo.hThread = nullptr;
        ProcessInfo.dwProcessId = 0;
        StartupInfo.cb = 0x44;
        ProcessInfo.dwThreadId = 0;

        char ReportPath[0x104];
        GetTempPathA(0x104, ReportPath);
        strcat(ReportPath, "dxdiag.txt");

        char CommandLine[0x104];
        wsprintfA(CommandLine, "dxdiag.exe %s", ReportPath);
        if (CreateProcessA(nullptr, CommandLine, nullptr, nullptr, FALSE, 0x4000020, nullptr,
                           nullptr, &StartupInfo, &ProcessInfo) == 0)
            return;

        if (WaitForSingleObject(ProcessInfo.hProcess, 60000) != WAIT_TIMEOUT)
        {
            if (wcslen(G_CrashReportFiles) != 0)
                wcscat(G_CrashReportFiles, L"|");
            const size_t Length = wcslen(G_CrashReportFiles);
            MultiByteToWideChar(CP_UTF8, 0, ReportPath, -1, G_CrashReportFiles + Length,
                                static_cast<int>(0x410 - Length));
            // 0x005a54b8: retail's terminator lands 0x104 wchar past the old end, not after the
            // appended text.
            G_CrashReportFiles[Length + 0x104] = L'\0';
        }
        CloseHandle(ProcessInfo.hProcess);
        CloseHandle(ProcessInfo.hThread);
    }

    // FUN_005a54e0 (.text:005a54e0-005a5909), the unhandled-exception filter FUN_005a5910
    // installs. Breakpoint (0x80000003) and single-step (0x80000004) are left to the next handler.
    // Everything else: close a hosted DirectPlay session, then hand the crash to Dr. Watson
    // (dw15.exe, shipped next to freelancer.exe) through a 0x1c50-byte shared block and wait for
    // it. Returns EXCEPTION_EXECUTE_HANDLER unless dw15 says the process may continue.
    LONG WINAPI ClientCrashFilter(EXCEPTION_POINTERS* Info)
    {
        LONG Result = 1; // local_20
        const DWORD Code = Info->ExceptionRecord->ExceptionCode;
        if (Code == 0x80000003 || Code == 0x80000004)
            return 0;

        // 0x005a552d..0x005a5555: guarded by its own __try with a filter that returns 1.
        __try
        {
            CDPServer::CrashCleanup();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        SECURITY_ATTRIBUTES Security;
        Security.lpSecurityDescriptor = nullptr;
        Security.nLength = 0xc;
        Security.bInheritHandle = TRUE;
        HANDLE Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &Security, PAGE_READWRITE, 0,
                                            0x1c50, nullptr);
        if (Mapping == nullptr)
            return Result;

        unsigned char* const Block =
            static_cast<unsigned char*>(MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0,
                                                      0, 0));
        if (Block != nullptr)
        {
            memset(Block, 0, 0x714 * 4);
            HANDLE const EventDone = CreateEventA(&Security, FALSE, FALSE, nullptr); // local_34
            if (EventDone != nullptr)
            {
                HANDLE const EventAlive = CreateEventA(&Security, FALSE, FALSE, nullptr); // local_38
                if (EventAlive != nullptr)
                {
                    HANDLE const Mutex = CreateMutexA(&Security, FALSE, nullptr); // local_3c
                    if (Mutex != nullptr)
                    {
                        if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                                            GetCurrentProcess(),
                                            reinterpret_cast<HANDLE*>(Block + 0x24), 0x1f0fff,
                                            TRUE, 0) != 0)
                        {
                            DWORD* const Words = reinterpret_cast<DWORD*>(Block);
                            Words[1] = GetCurrentProcessId();
                            Words[2] = GetCurrentThreadId();
                            *reinterpret_cast<HANDLE*>(Block + 0x1c) = EventDone;
                            *reinterpret_cast<HANDLE*>(Block + 0x14) = EventAlive;
                            *reinterpret_cast<HANDLE*>(Block + 0x20) = Mutex;
                            Words[0] = 0x1c50;
                            *reinterpret_cast<EXCEPTION_POINTERS**>(Block + 0x10) = Info;
                            *reinterpret_cast<void**>(Block + 0xc) =
                                Info->ExceptionRecord->ExceptionAddress;
                            Words[0x34 / 4] = 1;
                            Words[0x3c / 4] = 0x10;
                            wcscpy(reinterpret_cast<wchar_t*>(Block + 0x48), G_CrashAppName);
                            strcpy(reinterpret_cast<char*>(Block + 0x1148),
                                   "Microsoft\\PCHealth\\ErrorReporting\\DW");
                            strcpy(reinterpret_cast<char*>(Block + 0x1040),
                                   "HKLM\\Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0\\"
                                   "DigitalProductID");

                            HKEY Key;
                            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                              "Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0",
                                              0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
                            {
                                DWORD LangId;
                                DWORD Size = 4;
                                if (RegQueryValueExA(Key, "LangID", nullptr, nullptr,
                                                     reinterpret_cast<BYTE*>(&LangId),
                                                     &Size) == ERROR_SUCCESS)
                                    Words[0x1144 / 4] = LangId + 0x400;
                                RegCloseKey(Key);
                            }

                            Words[0x28 / 4] = 0x100;
                            memcpy(Block + 0x1210, L"common.dll", 22);

                            char ModulePath[0x104];
                            GetModuleFileNameA(nullptr, ModulePath, 0x104);
                            MultiByteToWideChar(CP_UTF8, 0, ModulePath, -1,
                                                reinterpret_cast<wchar_t*>(Block + 0xb8), 0x104);

                            AppendDxDiagReport();
                            memcpy(Block + 0x1418, G_CrashReportFiles,
                                   wcslen(G_CrashReportFiles) * 2 + 2);

                            ErrorHandler::deactivate();

                            STARTUPINFOA StartupInfo;
                            memset(&StartupInfo, 0, sizeof(StartupInfo));
                            StartupInfo.cb = 0x44;
                            PROCESS_INFORMATION ProcessInfo;
                            ProcessInfo.hProcess = nullptr;
                            ProcessInfo.hThread = nullptr;
                            ProcessInfo.dwProcessId = 0;
                            ProcessInfo.dwThreadId = 0;

                            char Arguments[0x104];
                            wsprintfA(Arguments, " -x -s %u", reinterpret_cast<UINT_PTR>(Mapping));
                            if (CreateProcessA("dw15.exe", Arguments, nullptr, nullptr, TRUE,
                                               0x4000020, nullptr, nullptr, &StartupInfo,
                                               &ProcessInfo) == 0)
                            {
                                Result = 0;
                            }
                            else
                            {
                                // 0x005a5824..0x005a58a3.
                                bool bWaiting = true;
                                do
                                {
                                    if (WaitForSingleObject(EventDone, 20000) == WAIT_OBJECT_0)
                                    {
                                        if (WaitForSingleObject(EventAlive, 1) == WAIT_OBJECT_0)
                                        {
                                            bWaiting = false;
                                            if (Words[0x2c / 4] == 0x10)
                                                Result = 0;
                                        }
                                    }
                                    else
                                    {
                                        const DWORD MutexWait = WaitForSingleObject(Mutex, 20000);
                                        if (MutexWait == WAIT_TIMEOUT)
                                        {
                                            bWaiting = false;
                                        }
                                        else if (MutexWait == WAIT_ABANDONED)
                                        {
                                            bWaiting = false;
                                            ReleaseMutex(Mutex);
                                        }
                                        else
                                        {
                                            if (WaitForSingleObject(EventDone, 1) != WAIT_OBJECT_0)
                                            {
                                                SetEvent(EventAlive);
                                                bWaiting = false;
                                            }
                                            else if (WaitForSingleObject(EventAlive, 1) ==
                                                     WAIT_OBJECT_0)
                                            {
                                                bWaiting = false;
                                            }
                                            ReleaseMutex(Mutex);
                                        }
                                    }
                                } while (bWaiting);
                            }
                        }
                        CloseHandle(Mutex);
                    }
                    CloseHandle(EventAlive);
                }
                CloseHandle(EventDone);
            }
            UnmapViewOfFile(Block);
        }
        CloseHandle(Mapping);
        return Result;
    }

    // FUN_005a5910 (.text:005a5910-005a593c).
    void InstallClientCrashFilter(const wchar_t* AppName)
    {
        wcsncpy(G_CrashAppName, AppName, 0x80);
        G_CrashAppName[0x7f] = L'\0';
        SetUnhandledExceptionFilter(ClientCrashFilter);
    }

    // DAT_005b08a0 -- installed into DALib::FatalErrorFn by WinMain (0x005b30e8). Ghidra left it
    // as data; disassembled from the PE bytes. Loads caption 0xd1 and the message Id from the
    // resources, appends Text, drops the render device (FUN_004247c0(1)), forces the cursor
    // visible, shows the message box and exits.
    void __cdecl ClientFatalError(unsigned int Id, const char* Text)
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd1, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Fatal Error");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(Id, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered a fatal error and will now terminate.\n");

        if (Text != nullptr)
        {
            wcscat(Message, L"\n\n");
            const size_t Length = wcslen(Message);
            MultiByteToWideChar(CP_ACP, 0, Text, -1, Message + Length,
                                static_cast<int>(0x1000 - Length));
        }

        ShutdownRenderMgr(true);
        while (ShowCursor(TRUE) < 0)
        {
        }
        MessageBoxW(g_client.window, Message, Caption, 0x10030);
        exit(1);
    }

    // LAB_005b09b0 -- installed into DALib::CriticalWarningFn (0x005b30f4). Same text assembly
    // with caption 0xd2; a Yes/No box. "No" drops the render device and exits; "Yes" puts the
    // cursor count back and returns true.
    bool __cdecl ClientCriticalWarning(unsigned int Id, const char* Text)
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd2, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Critical Warning");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(Id, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered an unknown issue.\n");

        if (Text != nullptr)
        {
            wcscat(Message, L"\n\n");
            const size_t Length = wcslen(Message);
            MultiByteToWideChar(CP_ACP, 0, Text, -1, Message + Length,
                                static_cast<int>(0x1000 - Length));
        }

        int ExtraShows = 0;
        if (ShowCursor(TRUE) < 0)
        {
            do
            {
                ++ExtraShows;
            } while (ShowCursor(TRUE) < 0);
        }

        const bool bContinue = MessageBoxW(g_client.window, Message, Caption, 0x10024) == IDYES;
        if (!bContinue)
        {
            ShutdownRenderMgr(true);
            exit(1);
        }

        if (ExtraShows >= 0)
        {
            for (int Count = ExtraShows + 1; Count != 0; --Count)
                ShowCursor(FALSE);
        }
        return bContinue;
    }

    // .data:00615bb0 / .data:00615bbc -- writable char arrays in retail (only FUN_005b2d60
    // reads them), handed to EBUEula as-is.
    char G_EulaFile[] = "..\\EULA.rtf";
    char G_WarrantyFile[] = "..\\Warranty.rtf";

    // EBUEula.dll's "EBUEula" export as FUN_005b2d60 calls it: cdecl (ADD ESP,0x10 after the
    // call at 0x005b2f31), four arguments, an int result tested for non-zero. EBUEula.dll is the
    // retail third-party binary shipped in bin\ (not Digital Anvil code, not ported).
    typedef int(__cdecl* FEBUEulaFn)(const char* RegKey, char* EulaFile, char* WarrantyFile,
                                     int Arg4);

    // The failure tail FUN_005b2d60 carries twice (0x005b2d94..0x005b2e3b after both
    // LoadLibraryA calls fail, 0x005b2e5e..0x005b2f09 after GetProcAddress fails): caption ids
    // 0xd1 and message ids 0xd9 from the resources (FUN_004347e0 with DAT_0067eca8) with the
    // built-in fallbacks, the render device dropped (FUN_004247c0(1)), the cursor forced
    // visible, MessageBoxW(DAT_0067eca0, message, caption, 0x10030), exit(1). No text is
    // appended. GetProcAddress / MessageBoxW go through retail's lazy import thunks
    // FUN_005b74fe / FUN_005b75b7 (kernel32 / user32 by name) -- the same APIs.
    __declspec(noreturn) void EulaLoadFailureExit()
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd1, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Fatal Error");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(0xd9, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered a fatal error and will now terminate.\n");

        ShutdownRenderMgr(true);
        while (ShowCursor(TRUE) < 0)
        {
        }
        MessageBoxW(g_client.window, Message, Caption, 0x10030);
        exit(1);
    }

    // FUN_005b2d60 (.text:005b2d60-005b2f4e): the EULA gate WinMain runs after the ..\DATA
    // probe (0x005b30bc). False (EBUEula returned 0) makes WinMain return 1.
    bool RunEulaGate()
    {
        // 0x005b2d6c..0x005b2d8e: "..\EBUEula.dll", then "EBUEula.dll"; neither -> fatal box.
        HMODULE Eula = LoadLibraryA("..\\EBUEula.dll");
        if (Eula == nullptr)
        {
            Eula = LoadLibraryA("EBUEula.dll");
            if (Eula == nullptr)
                EulaLoadFailureExit();
        }

        // 0x005b2e41..0x005b2e58: GetProcAddress(module, "EBUEula"); a miss frees the module
        // first, then the same fatal box.
        FEBUEulaFn const EulaFn = reinterpret_cast<FEBUEulaFn>(GetProcAddress(Eula, "EBUEula"));
        if (EulaFn == nullptr)
        {
            FreeLibrary(Eula);
            EulaLoadFailureExit();
        }

        // 0x005b2f0f..0x005b2f26: GetFileAttributesA("..\Warranty.rtf"); INC / NEG / SBB / AND
        // -> the path when the attributes are not INVALID_FILE_ATTRIBUTES, else null.
        char* const Warranty =
            (GetFileAttributesA(G_WarrantyFile) != INVALID_FILE_ATTRIBUTES) ? G_WarrantyFile : nullptr;

        // 0x005b2f1f..0x005b2f31: EBUEula(.rdata:005e1f64 registry key, "..\EULA.rtf",
        // warranty-or-null, 1).
        int const Result =
            EulaFn("Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0", G_EulaFile, Warranty, 1);

        // 0x005b2f36..0x005b2f44: FreeLibrary, then SETNZ on the result.
        FreeLibrary(Eula);
        return Result != 0;
    }

    // FUN_005b0b00 -- the assert callback WinMain hands ErrorHandler::activate. In a fullscreen
    // session (DAT_00679be5) with a registered Win3D (FUN_0059da50) the message goes to the
    // NN_Error modal FUN_00473e00 and the result is 4 + (its bool) (005b0b1c..005b0b29);
    // otherwise MessageBoxA.
    int __cdecl ClientAssertHandler(const char* Message, const char* Caption, bool bOkCancel)
    {
        if (g_display.fullscreen != 0 && Win3DGetFirstRegistered() != nullptr)
            return NnErrorRunModalMessage(Message) ? 5 : 4;
        const UINT Flags = (bOkCancel ? 2u : 0x12u) | 2u;
        return MessageBoxA(nullptr, Message, Caption, Flags);
    }

    // FUN_005b2cb0 (.text:005b2cb0-005b2d55), cdecl(FClientCommandLine*): "+G"/"-G", "+P<port>"
    // and "+S<host:port>" in either case, the sign ignored. A token without a sign matches
    // nothing.
    void ApplyClientCommandLineFlags(const FClientCommandLine* CommandLine)
    {
        for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
        {
            const char* Token = CommandLine->Argv[Index];
            char Letter = 0;
            if (Token[0] == '+' || Token[0] == '-')
            {
                Letter = static_cast<char>(toupper(Token[1]));
                Token += 2;
            }

            if (Letter == 'G' || Letter == 'g')
            {
                G_bCommandLineG = true;
            }
            else if (Letter == 'P' || Letter == 'p')
            {
                const int Port = MsvcrtAtoi(Token);
                if (Port != 0)
                    CDPClient::SetSourcePort(static_cast<unsigned long>(Port));
            }
            else if (Letter == 'S' || Letter == 's')
            {
                // 0x005b2d16..0x005b2d1c: FUN_005ac710 on the connect context DAT_0067e7b8; its
                // answer is not read.
                G_LoginMgr.AddDirectServer(Token);
            }
        }
    }

    // WinMain's exception filter (.text:005b33a9-005b33c6): FUN_004247c0(1), ShowCursor(TRUE),
    // EXCEPTION_CONTINUE_SEARCH.
    int ClientWinMainFilter()
    {
        ShutdownRenderMgr(true);
        ShowCursor(TRUE);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // LAB_005b2190 -- registered with atexit right after CoInitialize (0x005b3101), so it runs
    // after WinMain has returned. Calls are in retail's order; a call the port has no retail
    // counterpart for yet is named in place.
    void __cdecl ClientAtExitTeardown()
    {
        CGunWrapper::Shutdown();
        // 0x005b2195: FUN_005791c0 -- delete the loading screen's Image (DAT_0067c3bc) and its
        // progress bar tree (DAT_0067c3c0), set DAT_0067c3db.
        LoadingScreenShutdown();
        Async::Shutdown();
        if (g_client.current_state != nullptr)
        {
            // 0x005b21a0..0x005b21b9: slot 13 (+0x34, Destroy) of the current state object,
            // then DAT_0067ecac = 0.
            g_client.current_state->Destroy();
            g_client.current_state = nullptr;
        }
        ShutdownServerConnection(); // FUN_005a8260
        G_LoginMgr.Reset();         // FUN_005a9300 on DAT_0067e7b8
        G_LoginMgr.DestroyGun();    // FUN_005abc60 on DAT_0067e7b8
        // Singleton slot 9 (+0x24, Shutdown) on FUN_00575430 / FUN_00437420 / FUN_0054bd30 /
        // FUN_0043d560 (0x005b21d2..0x005b21ff). IntroFocus's is 0x00401e90, a bare RET; GF's
        // LAB_00437290 -> FUN_00558870, the space state's FUN_0054ab20, MissionBriefing's
        // LAB_0043d260.
        GetIntroFocusState()->Shutdown();      // 0x005b21d2..0x005b21db
        GetGameflowState()->Shutdown();        // 0x005b21de..0x005b21e7
        GetSpaceState()->Shutdown();           // 0x005b21ea..0x005b21f3
        GetMissionBriefingState()->Shutdown(); // 0x005b21f6..0x005b21ff
        ShutdownCharacterInfo();    // FUN_00435500 (0x005b2202)
        // 0x005b2207..0x005b2224: the ToolTip singleton DAT_0067244c -- slot 10 (Destroy), then,
        // re-read and still set, slot 0 with 1 (the scalar deleting dtor, FUN_004bd690 clears
        // DAT_0067244c when it is this tooltip).
        if (GetNnToolTipSingleton() != nullptr)
        {
            GetNnToolTipSingleton()->Destroy();
            NnToolTipControl* const Tip = GetNnToolTipSingleton();
            if (Tip != nullptr)
                delete Tip;
        }
        NnRolloverDestroyGlobal();  // FUN_004b6580 (0x005b2228)
        ChatShutdown();            // FUN_00469b30 (0x005b222d; DAT_0066da60..88, FUN_00469d20)
        NnSystemShutdown();         // FUN_0045a140 (0x005b2232)
        ClearButtonMontage();       // FUN_00559d20 (0x005b2237)
        // 0x005b223c..0x005b226f: the DAT_0067ecb4 tree -- FUN_0043d2a0 inlined (null -> skip;
        // each child through FUN_0043d2a0 with the next sibling read first; slot 10; deleting
        // dtor). The root is always null in retail (globals.h).
        Win3DDeleteTree(G_WinDebugRoot);
        Win3DShutdownManager(); // FUN_0059d6d0 (0x005b2271)
        ShutdownUiSoundTable(); // FUN_00564540
        StopAllClientSounds();  // FUN_004284f0 (0x005b227b)
        DataConfig::shutdown();
        GraphLibraryShutdown(); // FUN_00419e50
        EffectSystemShutdown();   // FUN_004f7140 (0x005b228b)
        ClearEffectShapesFiles(); // FUN_004f73b0 (0x005b2290)
        UiScrollClearShapes(); // FUN_00595400
        ShutdownClientTickProfile(); // CLIENT-BETA-LABEL: release cached atlas owners first
        FontShutdown(); // FUN_004174a0 (0x005b229a)
        ShutdownRichText();
        ShutdownDebrisArchDb(); // FUN_00530fa0 (0x005b22a5)
        EffectArchShutdown();      // FUN_0040ef30 (0x005b22aa)
        ClearClientVoices();       // FUN_00431850
        ShutdownClientSoundArch(); // FUN_0042b4d0
        ShutdownRenderMgr(false);  // FUN_004247c0(0)
#ifdef FREELANCER_RP9
        // Port-only: the Direct3D 9 extension objects and interfaces go before the pipeline.
        RenderEx::Shutdown();
#endif
        // 0x005b22c3 FUN_005b4050: frees every heap option object and list node of the
        // PerfOptions std::list (DAT_0067ed04). The port's registry is the static G_Options
        // table (perf_options.cpp), so there is nothing to free; nothing after this point reads
        // the registry in retail either.
        if (Reputation::Vibe::Count() != 0)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %d REP_INSTANCE(s) orphaned\n",
                  "E:\\FL\\Scratch\\Source\\Client\\main.cpp", 0x4be, Reputation::Vibe::Count());
        }
        Archetype::FlushMaterialCache(); // FUN_0041b150 (0x005b22f6), result discarded
        AleLibShutdown();                // FUN_00401380 (ALELib::Shutdown, 0x005b22fb)
        DALib::CloseData();
        DALib::Shutdown(g_client.window);
        G_LoginMgr.Cleanup(); // CDPClient::Cleanup on DAT_0067e7bc
        FreeLibrary(g_client.resources);
        g_client.resources = nullptr;
        ErrorHandler::deactivate();
        CFLIDMaker::Shutdown();
        StringShutdown();
        ReleaseClientIMEFocus(); // FUN_005641d0 (0x005b2349): delete DAT_0067a4a4
        CoUninitialize();
    }
}

// FUN_005b33f0 (.text:005b33f0): `MOV dword ptr [ECX],0; RET`.
FClientCommandLine::FClientCommandLine()
{
    Count = 0;
}

// FUN_005b3400 (.text:005b3400-005b34aa), __thiscall, RET 4.
void FClientCommandLine::Parse(const char* CommandLine)
{
    static const char kDelimiters[] = " \t"; // .rdata:005e6dec

    Count = 0;
    strcpy(Buffer, CommandLine);

    char* Cursor = Buffer;
    for (;;)
    {
        Cursor += strspn(Cursor, kDelimiters);
        char* const Token = Cursor;
        unsigned Length = 0;
        char Ch = *Cursor;
        while (Ch != '\0')
        {
            ++Cursor;
            if (strchr(kDelimiters, Ch) != nullptr)
                break;
            if (Ch == '"')
            {
                // Copy up to the closing quote; a missing one ends the token (and the scan).
                char Inner = *Cursor;
                if (Inner == '\0')
                    break;
                bool bClosed = false;
                do
                {
                    ++Cursor;
                    if (Inner == '"')
                    {
                        bClosed = true;
                        break;
                    }
                    Token[Length] = Inner;
                    ++Length;
                    Inner = *Cursor;
                } while (Inner != '\0');
                if (!bClosed)
                    break;
            }
            else
            {
                Token[Length] = Ch;
                ++Length;
            }
            Ch = *Cursor;
        }

        Token[Length] = '\0';
        if (Token[0] == '\0')
            return;
        Argv[Count] = Token;
        ++Count;
    }
}

// FUN_005b27c0 -- developer FPU-exception-trap toggle, gated on "+X"/"-X" (G_bFpExceptionTrap).
// Enabling unmasks FP exceptions (crash on NaN/overflow/etc.); retail calls this every frame with
// Enable=false at frame-begin (FUN_00424f30) and Enable=true at frame-end (FUN_00425080), in
// addition to the one-time WinMain call.
void SetFpuExceptionTrap(bool Enable)
{
    if (Enable)
    {
        _clearfp();
        const unsigned int Prev = _controlfp(0, 0);
        _control87(Prev & 0xffffffe3u, 0x8001f);
    }
    else
    {
        _control87(0x9001f, 0x8001f);
    }
}

// FUN_005b2800 (.text:005b2800-005b2880).
void OpenSpewLog()
{
    char Path[0x104];
    if (!GetUserLocalDataPath(Path))
        return;
    strcat(Path, "\\FLSpew.txt");
    if (ErrorHandler::new_log(Path, -1, 7, false, false) != nullptr)
        MultiByteToWideChar(CP_UTF8, 0, Path, -1, G_CrashReportFiles, 0x104);
}

// FUN_005b2f50 -- Client/main.cpp WinMain.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE /*prev*/, LPSTR cmd, int /*show*/)
{
    int ExitCode = 1; // local_20

    // The whole body runs under a frame handler (scope table .rdata:005e6de0) whose filter
    // (0x005b33a9) drops the render device and shows the cursor, then lets the exception go on.
    __try
    {
        // 0x005b2f87..0x005b2fac: the retail single-instance guard (named mutex; null handle or
        // ERROR_ALREADY_EXISTS 0xb7 -> return 1). DEVIATION (deviations.md "Client: single-instance
        // guard behind FL_SINGLE_INSTANCE"): compiled only with FL_SINGLE_INSTANCE defined, so the
        // owner can run several clients on one machine for multiplayer tests.
#if defined(FL_SINGLE_INSTANCE)
        HANDLE mutex = CreateMutexA(nullptr, FALSE, "FreelancerClient");
        if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(mutex);
            return 1;
        }
#endif

        char ModulePath[MAX_PATH];
        GetModuleFileNameA(nullptr, ModulePath, MAX_PATH);
        char Drive[_MAX_DRIVE];
        char Dir[_MAX_DIR];
        _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);
        char Folder[MAX_PATH];
        _makepath(Folder, Drive, Dir, nullptr, nullptr);
        SetCurrentDirectoryA(Folder);

        // Retail probes ..\DATA after the chdir above (DebugASan runs from binasan\, a sibling of bin\).
        if (_access("..\\DATA\\CHARACTERS\\ANIMATIONS\\FACIALMALE.ANM", 0) != 0)
        {
            STARTUPINFOA StartupInfo;
            memset(&StartupInfo, 0, sizeof(StartupInfo));
            StartupInfo.cb = 0x44;
            PROCESS_INFORMATION ProcessInfo;
            memset(&ProcessInfo, 0, sizeof(ProcessInfo));
            if (CreateProcessA("FLServer.exe", nullptr, nullptr, nullptr, FALSE, 0x20, nullptr,
                               nullptr, &StartupInfo, &ProcessInfo))
            {
                CloseHandle(ProcessInfo.hProcess);
                CloseHandle(ProcessInfo.hThread);
            }
            return 1;
        }

        // 0x005b30bc..0x005b30df: FUN_005b2d60, the EULA gate; declined -> return EDI, which
        // still holds the 1 loaded at 0x005b2f79.
        if (!RunEulaGate())
            return 1;

        // Global DALib data exports ?FatalErrorFn@@3P6AXIPBD@ZA / ?CriticalWarningFn@@3P6A_NIPBD@ZA
        // (not in namespace DALib), stored at 0x005b30e2 / 0x005b30ee.
        ::FatalErrorFn = ClientFatalError;
        ::CriticalWarningFn = ClientCriticalWarning;
        CoInitialize(nullptr);
        atexit(ClientAtExitTeardown);
        g_client.instance = instance;
        g_client.resources = LoadLibraryA("resources.dll");
        RegisterIdsResourceModule(g_client.resources); // FUN_0057d800

        unsigned int Major;
        unsigned int Minor;
        unsigned int Build;
        if (DACOM_GetDllVersion(ModulePath, &Major, &Minor, &Build) == 0)
        {
            InstallClientCrashFilter(L"Freelancer");
        }
        else
        {
            // 0x005b316a.
            Major = 1;
            Minor = 0;
            Build = 0xb;
        }
        g_client.packed_version = (((Minor & 0xFFu) | (Major << 8)) << 16) | (Build & 0xFFFFu); // FUN_005b34b0

        FClientCommandLine CommandLine;
        CommandLine.Parse(cmd);
        ApplyClientCommandLineFlags(&CommandLine);
        LoadStartupConfig(&g_display, &CommandLine);
        ErrorHandler::activate(g_display.ini_path.c_str(), ClientAssertHandler);
        OpenSpewLog();
        // FUN_005a82d0(&CommandLine, ini path): [Freelancer] local_server / AppGUID, [ListServer]
        // hostname / port and [Login] name / pwd / char into the connect context. The command
        // line argument is not read.
        G_LoginMgr.LoadConfig(g_display.ini_path.c_str());
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(true);

        WNDCLASSA WindowClass;
        char ClassName[] = "Freelancer";
        WindowClass.style = CS_DBLCLKS;
        WindowClass.lpfnWndProc = ClientWndProc;
        WindowClass.cbClsExtra = 0;
        WindowClass.cbWndExtra = 0;
        WindowClass.hInstance = instance;
        WindowClass.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(0x67));
        WindowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        WindowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        WindowClass.lpszMenuName = nullptr;
        WindowClass.lpszClassName = ClassName;
        RegisterClassA(&WindowClass);

        g_client.window = CreateGameWindow(ClassName, &g_display, "Freelancer");
        ShowCursor(FALSE);

        if (InitializeEngine(&CommandLine))
        {
            ClientNoteEngineReady(); // DAT_0067ecc8 = 1; FUN_00471f30(0) if a close came early

            // 0x005b3324..0x005b3330: FUN_005b2a30(FUN_005b20a0(), nullptr) into DAT_0067ecac.
            g_client.current_state = EnterClientState(ComputeInitialEnvironment(), nullptr);

            RegisterAcceleratorGroup("IDR_ALWAYS_PRESENT"); // FUN_00577490

            for (;;)
            {
                UpdateClientTiming();                              // FUN_005b2360
                const double DeltaSeconds = GetClientFrameTimeCapped(); // FUN_0042d680
                if (!PumpMessages(&ExitCode))                      // FUN_005b0b60
                    break;
                if (ClientPresentAllowed())                        // DAT_00615ba8
                    TickClientFrame(DeltaSeconds);                 // FUN_005b2890
            }
        }
    }
    __except (ClientWinMainFilter())
    {
        // 0x005b33c7: unreachable, the filter never accepts.
    }

    // Retail WinMain returns straight away; the mutex is left to process exit and the teardown
    // runs from atexit (ClientAtExitTeardown).
    return ExitCode;
}
