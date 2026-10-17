// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "globals.h"
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "win_factory.h"   // WinFactoryCreate (FUN_0059db20)
#include "character_info.h"
#include "client_ime_focus.h"
#include "DALib.h"
#include "alchemy.h"
#include "client_cliobj.h"
#include "common_csys.h" // Acquire3DMathEngine (this module's own copy, defined below)
#include "common_id.h"
#include "common_ini.h"
#include "common_async.h"      // Async::Startup
#include "common_dataconfig.h" // DataConfig::startup, DataConfig::ProgressCB
#include "common_misc.h"       // GetFileSysPath, DAEngine_create_filesystem
#include "common_timing.h"     // Timing::init
#include "dacom.h"
#include "dataconfig_progress.h"
#include "font_atlas.h"
#include "font_string.h"   // FHudWideText, WideTextReserve / WideTextFree
#include "gameflow.h"
#include "client_state.h"  // GetIntroFocusState (FUN_00575430)
#include "space_state.h"   // GetSpaceState (FUN_0054bd30)
#include "ids_strings.h"
#include "imeui.h"
#include "input_manager.h"
#include "client_frame_tick.h" // BeginClientRenderFrame / EndClientRenderScene / Present...
#include "image.h"
#include "loading_screen.h"
#include "win3d_immediate.h"   // Win3DSetViewport / Win3DSetPipelineClearColor
#include "win3d_render.h"      // ClearWin3DBuffers
#include "menu_keymap_config.h" // KeyMapRouteInputMessage (FUN_00577850)
#include "menu_ui_init.h"
#include "chat_init.h"
#include "menu_ui_widgets.h"
#include "common_text.h"
#include "texture_shapes.h"
#include "menu_universe_base.h"
#include "mission_briefing.h"
#include "nn_preferences.h"
#include "perf_options.h"
#include "space_camera.h"
#include "win3d.h"                // Win3DStartupManager (FUN_0059d5a0)
#include "win3d_engine_submit.h"
#ifdef FREELANCER_RP9
#include "render_ex.h"            // RenderEx::Startup (port-only Direct3D 9 extensions)
#endif
#include "render/IRenderPipeline.h"

#include <float.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// RenderMgr.cpp .data (declared in globals.h).
int G_FpuPrecisionWanted = 0x35; // DAT_00610848
int G_FpuPrecisionFound = 0x18;  // DAT_0061084c

// DAT_0067ecb4 (.bss 0; declared in globals.h).
Win3D* G_WinDebugRoot = nullptr;

namespace
{
    const char kMainCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\main.cpp";
    const char kRenderMgrCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\RenderMgr.cpp";

    // The message loop FUN_005b1300 inlines after every stage (0x005b134a, 0x005b152d, ...):
    // the body of FUN_005b0b60 except that WM_QUIT only makes InitializeEngine return 0 -- the
    // exit code is not stored.
    bool PumpInitMessages()
    {
        // FUN_0041ef20 drains the DirectInput mouse queue first (0x005b134a, 0x005b152d, ...).
        MouseDispatchEvents();
        MSG Msg;
        while (PeekMessageA(&Msg, nullptr, 0, 0, PM_NOREMOVE))
        {
            if (!GetMessageA(&Msg, nullptr, 0, 0))
                return false;
            IMEFocus* const Ime = IMEFocus::GetSingleton();
            if ((Ime == nullptr || !Ime->IgnoreHotKey(&Msg)) &&
                !KeyMapRouteInputMessage(Msg.message, static_cast<unsigned>(Msg.wParam),
                                         static_cast<unsigned>(Msg.lParam)))
            {
                TranslateMessage(&Msg);
                DispatchMessageA(&Msg);
            }
        }
        return true;
    }

    // RTTI .?AUDataConfigPump@@ : DataConfig::ProgressCB, vftable 0x005e6ab8 = {LAB_005b1fe0}.
    // No data members; InitializeEngine keeps one on the stack for DataConfig::startup.
    struct DataConfigPump : public DataConfig::ProgressCB
    {
        // LAB_005b1fe0 (.text:005b1fe0-005b2096), thiscall, RET 4: the value string is not
        // read. The same loop as PumpInitMessages (MouseDispatchEvents FUN_0041ef20, then
        // Peek / Get / IMEFocus::IgnoreHotKey / FUN_00577850 / Translate / Dispatch); WM_QUIT
        // returns 1 (0x005b208b), an empty queue 0 (0x005b2080).
        int Slot00(char const* ValueString) override
        {
            (void)ValueString;
            return PumpInitMessages() ? 0 : 1;
        }
    };

    // 0x005b15ce..0x005b1684 and 0x005b17be..0x005b1872: caption ids 0xd1, message ids 0xd7
    // (pipeline startup) or 0xc9 (no display mode), both with built-in fallbacks; drop the
    // render device, force the cursor visible, show the box and exit.
    __declspec(noreturn) void ShowEngineFatalError(unsigned int MessageIds)
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd1, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Fatal Error");
        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(MessageIds, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered a fatal error and will now terminate.\n");
        ShutdownRenderMgr(true);
        while (ShowCursor(TRUE) < 0)
        {
        }
        MessageBoxW(g_client.window, Message, Caption, 0x10030);
        exit(1);
    }

    // FUN_004247a0 (.text:004247a0-004247bb): Pipeline->Startup(nullptr) >= 0, false without a
    // pipeline.
    bool StartupRenderPipeline()
    {
        IRenderPipeline* const Pipeline = DALib::Pipeline;
        if (Pipeline == nullptr)
            return false;
        return Pipeline->Startup(nullptr) >= 0;
    }

}

    // FUN_00424ce0 (.text:00424ce0-00424dc8), cdecl(hwnd, mode): try the mode; a windowed mode
    // with a non-zero options size then retries with Format 32/24/16 (WantedZ any); after that
    // the options object itself; failing all of it, lay the window out at 640x480 windowed.
    bool SetRenderModeWithFallback(HWND Hwnd, const DisplaySettings* Mode)
    {
        DisplaySettings Local;
        memcpy(&Local, Mode, 0x28);
        bool bOk = RenderMgrCreateBuffers(Hwnd, &Local, &g_display);
        if (bOk)
            return true;

        if (Local.fullscreen == 0)
        {
            if (g_display.width == 0)
            {
                ApplyGameWindowLayout(Hwnd, false, 0x280, 0x1e0);
                return bOk;
            }
            if (g_display.height != 0)
            {
                const int kFormats[3] = {0x20, 0x18, 0x10};
                for (unsigned Index = 0; Index < 3; ++Index)
                {
                    Local.color_bpp = kFormats[Index];
                    Local.depth_bpp = -1;
                    bOk = RenderMgrCreateBuffers(Hwnd, &Local, &g_display);
                    if (bOk)
                        return true;
                }
            }
        }

        // 0x00424d7f.
        if (g_display.width != 0 && g_display.height != 0)
            return RenderMgrCreateBuffers(Hwnd, &g_display, &g_display);
        ApplyGameWindowLayout(Hwnd, false, 0x280, 0x1e0);
        return bOk;
    }

namespace
{
    // FUN_00424dd0 (.text:00424dd0-00424f2a), cdecl(hwnd, mode, fullscreen). With a sized mode
    // the ladder is {mode size and Format, 800x600 at that Format, 800x600x16}; without one the
    // first rung is 800x600x16, or 1024x768x32 on an adapter reporting more than 16 MB of video
    // memory (QueryDeviceAbility 3, >> 20) and ability 4. WantedZ is always "any".
    bool SelectInitialRenderMode(HWND Hwnd, const DisplaySettings* Mode, char bFullscreen)
    {
        DisplaySettings Local;
        Local.mode_field00 = -1;
        Local.refresh_rate = 0x3c;
        Local.stencil_bpp = 0;
        Local.interval = 1;
        Local.buffer_count = 2;
        Local.border = 1;

        unsigned Width;
        unsigned Height;
        int Format;
        if (Mode->width != 0 && Mode->height != 0)
        {
            memcpy(&Local, Mode, 0x28);
            Width = static_cast<unsigned>(Local.width);
            Height = static_cast<unsigned>(Local.height);
            Format = Local.color_bpp;
        }
        else
        {
            // 0x00424e2e. Local.width/height/color_bpp stay unset: every rung below writes them.
            Local.fullscreen = bFullscreen;
            unsigned long VideoMemory = 0;
            DALib::Pipeline->QueryDeviceAbility(3, &VideoMemory);
            Width = 0x320;
            Height = 0x258;
            Format = 0x10;
            VideoMemory >>= 20;
            if (VideoMemory >= 0x11)
            {
                unsigned long Ability4 = 0;
                DALib::Pipeline->QueryDeviceAbility(4, &Ability4);
                Format = 0x20;
                if (Ability4 != 0)
                {
                    Width = 0x400;
                    Height = 0x300;
                }
            }
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            // Port-only: full screen with no saved [Display] size on a 32-bit capable adapter,
            // the first rung is the desktop's own size when it is at least 800x600 and at least
            // as wide as 4:3 -- a widescreen monitor starts native instead of stretching 1024x768.
            int DesktopWidth = 0;
            int DesktopHeight = 0;
            if (bFullscreen != 0 && Format == 0x20 && WidescreenGetDesktopSize(&DesktopWidth, &DesktopHeight) &&
                DesktopWidth >= 0x320 && DesktopHeight >= 0x258 &&
                DesktopWidth * 3 >= DesktopHeight * 4)
            {
                Width = static_cast<unsigned>(DesktopWidth);
                Height = static_cast<unsigned>(DesktopHeight);
            }
#endif
        }

        const unsigned Rungs[3][3] = {
            {Width, Height, static_cast<unsigned>(Format)},
            {0x320, 0x258, static_cast<unsigned>(Format)},
            {0x320, 0x258, 0x10},
        };
        Local.depth_bpp = -1;
        bool bOk = false;
        for (unsigned Index = 0; Index < 3; ++Index)
        {
            Local.width = static_cast<int>(Rungs[Index][0]);
            Local.height = static_cast<int>(Rungs[Index][1]);
            Local.color_bpp = static_cast<int>(Rungs[Index][2]);
            bOk = RenderMgrCreateBuffers(Hwnd, &Local, &g_display);
            if (bOk)
                break;
        }
        return bOk;
    }

    // FUN_005b1300's three [Startup] movies (0x005b18c0..0x005b19bc).
    // DEVIATION (existing): retail imports MOVIE.DLL statically; the port binds MediaPlayer by
    // GetProcAddress because src/movie publishes no header in src/include. A missing movie.dll
    // skips the movies instead of failing to start the process.
    bool PlayStartupMovies()
    {
        HMODULE MovieDll = LoadLibraryA("movie.dll");
        if (MovieDll == nullptr)
        {
            return true;
        }

        using FnSetApproach = void(__cdecl*)(int);
        using FnStartup = void(__cdecl*)();
        using FnPlayMovie = long(__cdecl*)(HWND, bool, const char*);
        using FnUpdateMovie = bool(__cdecl*)();
        using FnStopMovie = void(__cdecl*)();
        using FnShutdown = void(__cdecl*)();

        FnSetApproach SetApproach = reinterpret_cast<FnSetApproach>(
            GetProcAddress(MovieDll, "?SetApproach@MediaPlayer@@YAXW4Approach@1@@Z"));
        FnStartup Startup = reinterpret_cast<FnStartup>(
            GetProcAddress(MovieDll, "?Startup@MediaPlayer@@YAXXZ"));
        FnPlayMovie PlayMovie = reinterpret_cast<FnPlayMovie>(
            GetProcAddress(MovieDll, "?PlayMovie@MediaPlayer@@YAJPAUHWND__@@_NPBD@Z"));
        FnUpdateMovie UpdateMovie = reinterpret_cast<FnUpdateMovie>(
            GetProcAddress(MovieDll, "?UpdateMovie@MediaPlayer@@YA_NXZ"));
        FnStopMovie StopMovie = reinterpret_cast<FnStopMovie>(
            GetProcAddress(MovieDll, "?StopMovie@MediaPlayer@@YAXXZ"));
        FnShutdown Shutdown = reinterpret_cast<FnShutdown>(
            GetProcAddress(MovieDll, "?Shutdown@MediaPlayer@@YAXXZ"));

        if (SetApproach == nullptr || Startup == nullptr || PlayMovie == nullptr ||
            UpdateMovie == nullptr || StopMovie == nullptr || Shutdown == nullptr)
        {
            FreeLibrary(MovieDll);
            return true;
        }

        SetApproach(0); // DAT_0067eccc, zero .bss, never written
        Startup();

        for (unsigned Index = 0; Index < 3; ++Index)
        {
            const FClientPathString& Movie = g_display.movie_files[Index];
            if (Movie.Length == 0)
                continue;

            // FUN_00402180: _vsnprintf into a 0x2000-byte buffer, "%s\%s" (data path, movie).
            char Path[0x2000];
            _snprintf(Path, 0x2000, "%s\\%s", g_display.data_path.Text, Movie.Text);
            PlayMovie(g_client.window, g_display.fullscreen != 0, Path);
            if (UpdateMovie())
            {
                do
                {
                    if (!PumpInitMessages())
                    {
                        // 0x005b19e5: stop the movie and fail; MediaPlayer is not shut down.
                        StopMovie();
                        return false;
                    }
                } while (UpdateMovie());
            }
            StopMovie();
        }

        Shutdown();
        return true;
    }
}

namespace
{
    // The inlined "append a wide string" block FUN_005b0220 repeats after every string it
    // fetches (e.g. 0x005b0294..0x005b02e1): nothing for an empty Source, else grow the text to
    // old + len + 1 (FUN_004142a0), wcsncpy len chars after the old text, terminate.
    void LowMachineWarningAppend(FHudWideText* Text, wchar_t const* Source)
    {
        int const Length = static_cast<int>(wcslen(Source));
        if (Length <= 0)
            return;
        int const Old = Text->Text != nullptr ? static_cast<int>(wcslen(Text->Text)) : 0;
        int const Total = Old + Length;
        WideTextReserve(Text, static_cast<unsigned>(Total + 1));
        wcsncpy(Text->Text + Old, Source, static_cast<size_t>(Length));
        Text->Text[Total] = 0;
    }

    // One warning line: the ids string (L"" -- DAT_005c8ce0 -- when the lookup returns 0),
    // then L"\n\n" (DAT_005d0a08).
    void LowMachineWarningAppendIds(FHudWideText* Text, unsigned Ids)
    {
        wchar_t Line[0x100];
        if (GetIdsStringWWithRawFallback(Ids, Line, 0x100) == 0) // FUN_004347e0
            wcscpy(Line, L"");
        LowMachineWarningAppend(Text, Line);
        LowMachineWarningAppend(Text, L"\n\n");
    }

    // FUN_005b0220 (.text:005b0220-005b0591), cdecl(&text): the low-machine warning text for
    // InitializeEngine. True when the text is non-empty.
    bool BuildLowMachineWarning(FHudWideText* Text)
    {
        // 0x005b022a..0x005b0247: GlobalMemoryStatus on a local whose dwLength is not set;
        // total physical and available virtual memory in MB (SHR 0x14).
        MEMORYSTATUS Status;
        GlobalMemoryStatus(&Status);
        int const TotalPhysMb = static_cast<int>(Status.dwTotalPhys >> 20);
        int const AvailVirtualMb = static_cast<int>(Status.dwAvailVirtual >> 20);

        if (TotalPhysMb < 0x78)
        {
            // 0x005b0253..0x005b033b: under 120 MB of RAM. DAT_0067a439 (skip) ends it here.
            if (GetSkipMachineWarnings())
                goto Done;
            LowMachineWarningAppendIds(Text, 0xcb);
        }
        else if (AvailVirtualMb < 0x190)
        {
            // 0x005b0340..0x005b0421: under 400 MB of free virtual memory.
            LowMachineWarningAppendIds(Text, 0xd6);
        }

        // 0x005b042a..0x005b0552: skip set -> done; else the pipeline's ability 0xf (vt+0x20)
        // selects {none, 0xcd, 0xce, 0xcf} (a local table, indexed unchecked).
        if (GetSkipMachineWarnings())
            goto Done;
        {
            unsigned long Level = 0;
            DALib::Pipeline->QueryDeviceAbility(0xf, &Level);
            if (Level != 0)
            {
                unsigned const LevelIds[4] = {0, 0xcd, 0xce, 0xcf};
                LowMachineWarningAppendIds(Text, LevelIds[Level]);
            }
        }

    Done:
        // 0x005b0558..0x005b0588: non-null and non-empty.
        return Text->Text != nullptr && wcslen(Text->Text) != 0;
    }

    // FUN_005b05a0 (.text:005b05a0-005b07f8) -- the DLGPROC of dialog 0x82.
    INT_PTR CALLBACK LowMachineWarningDialogProc(HWND Dialog, UINT Message, WPARAM WParam,
                                                 LPARAM LParam)
    {
        if (Message == WM_INITDIALOG)
        {
            // 0x005b063d..0x005b07e7: the caption (ids 0xd2), the warning text (*lParam) in
            // 0x83, the check box 0x84 (ids 0x86) checked and enabled while warnings are not
            // skipped, the label 0x85 (ids 0x87) and the two buttons 6 / 7 (ids 0x4dc / 0x4dd).
            // Each missing string becomes L"".
            wchar_t Buffer[0x100];
            if (GetIdsStringWWithRawFallback(0xd2, Buffer, 0x100) == 0)
                wcscpy(Buffer, L"");
            SetWindowTextW(Dialog, Buffer);
            SetWindowTextW(GetDlgItem(Dialog, 0x83), *reinterpret_cast<wchar_t**>(LParam));

            HWND const CheckBox = GetDlgItem(Dialog, 0x84);
            if (GetIdsStringWWithRawFallback(0x86, Buffer, 0x100) == 0)
                wcscpy(Buffer, L"");
            SetWindowTextW(CheckBox, Buffer);
            SendMessageA(CheckBox, BM_SETCHECK, GetSkipMachineWarnings() ? 0 : 1, 0);
            EnableWindow(CheckBox, GetSkipMachineWarnings() ? FALSE : TRUE);

            HWND const Label = GetDlgItem(Dialog, 0x85);
            if (GetIdsStringWWithRawFallback(0x87, Buffer, 0x100) == 0)
                wcscpy(Buffer, L"");
            SetWindowTextW(Label, Buffer);

            HWND const YesButton = GetDlgItem(Dialog, 6);
            if (GetIdsStringWWithRawFallback(0x4dc, Buffer, 0x100) == 0)
                wcscpy(Buffer, L"");
            SetWindowTextW(YesButton, Buffer);

            HWND const NoButton = GetDlgItem(Dialog, 7);
            if (GetIdsStringWWithRawFallback(0x4dd, Buffer, 0x100) == 0)
                wcscpy(Buffer, L"");
            SetWindowTextW(NoButton, Buffer);
            return 1;
        }
        if (Message == WM_COMMAND)
        {
            // 0x005b05b9..0x005b05f0: BM_GETCHECK on 0x84 first, then the whole wParam:
            // 1 / 6 -> DAT_0067a439 = unchecked, EndDialog 1; 2 / 7 -> EndDialog 2.
            LRESULT const Checked = SendMessageA(GetDlgItem(Dialog, 0x84), BM_GETCHECK, 0, 0);
            switch (WParam)
            {
            case 1:
            case 6:
                SetSkipMachineWarnings(Checked == 0);
                EndDialog(Dialog, 1);
                return 1;
            case 2:
            case 7:
                EndDialog(Dialog, 2);
                return 1;
            default:
                return 0;
            }
        }
        return 0;
    }
}

float G_RenderGamma = 1.3f;  // DAT_00610858 (.data 0x3fa66666)
int G_bRenderDither = 1;     // DAT_0061085c (.data 1)
int G_RenderMipFilter = 1;   // DAT_00610860 (.data 1)

bool RenderMgrApplyGamma()
{
    // FUN_00424510 (.text:00424510-004245b4). A null pipeline returns false.
    bool bOk = false;
    IRenderPipeline* const Pipe = DALib::Pipeline;
    if (Pipe != nullptr)
    {
        // 0x00424534..0x00424557: QueryInterface (vt+0) for "1.11_IGammaControl" into a local
        // (zeroed first); a failure skips straight to the release test.
        IGammaControl* Gamma = nullptr;
        if (Pipe->QueryInterface(Iids::GammaControl, reinterpret_cast<void**>(&Gamma)) == 0)
        {
            // 0x00424559..0x00424569: slot 6 (+0x18) with the options object's fullscreen byte
            // (DAT_00679be5), result unread.
            Gamma->SetGammaEnable(static_cast<unsigned char>(g_display.fullscreen));
            // 0x0042456c..0x0042458a: slot 3 (+0x0c) (7, DAT_00610858, 0, 1.0f, 0); SETGE.
            bOk = Gamma->SetGammaFunction(7, G_RenderGamma, 0.0f, 1.0f, 0.0f) >= 0;
        }
        // 0x0042458d..0x004245a0: the local released (vt+8) when non-null.
        if (Gamma != nullptr)
            Gamma->Release();
    }
    return bOk;
}

void ApplyGammaRampOption(float Value)
{
    // LAB_004244e0 (RET 4): FCOMP 0.0f / TEST AH,1 / JNZ -- a value below 0 (or unordered)
    // does nothing; otherwise DAT_00610858 = v + 1.0 (double DAT_005c89b8) as a float and
    // FUN_00424510, its result dropped.
    if (!(Value >= 0.0f))
        return;
    G_RenderGamma = static_cast<float>(static_cast<double>(Value) + 1.0);
    RenderMgrApplyGamma();
}

void ApplyDitherControlOption(float Value)
{
    // LAB_00424630 (RET 4): DAT_0061085c = 1, then FCOMP double 0.3 (DAT_005c9858) / TEST AH,1 /
    // JZ -- below 0.3 (or unordered) stores 0.
    G_bRenderDither = 1;
    if (!(static_cast<double>(Value) >= 0.3))
        G_bRenderDither = 0;
}

void ApplyMipFilterControlOption(float Value)
{
    // LAB_004246d0 (RET 4): DAT_00610860 = 1 whatever the value.
    (void)Value;
    G_RenderMipFilter = 1;
}

// FUN_004249c0 (.text:004249c0-00424c5c), cdecl(hwnd, mode, out): create the device buffers for
// a mode and bring everything that depends on the surface size up to date.
bool RenderMgrCreateBuffers(HWND Hwnd, const DisplaySettings* Mode, DisplaySettings* Out)
{
    IRenderPipeline* const Pipeline = DALib::Pipeline;
    if (Pipeline == nullptr)
        return false;
    if (Mode->width == 0 || Mode->height == 0)
        return false;

    DisplaySettings Local;
    memcpy(&Local, Mode, 0x28);
    G_bRenderPostCreate = false; // DAT_00610864 (0x00424a09)

    if (Local.fullscreen == 0)
    {
        // 0x00424a12: windowed -- adapter format and depth, and never larger than the desktop.
        Local.color_bpp = -1;
        Local.depth_bpp = -1;
        RECT Desktop;
        GetWindowRect(GetDesktopWindow(), &Desktop);
        const unsigned DesktopRight = static_cast<unsigned>(Desktop.right);
        if (DesktopRight < static_cast<unsigned>(Local.width) ||
            static_cast<unsigned>(Desktop.bottom) < static_cast<unsigned>(Local.height))
        {
            Local.width = static_cast<int>(DesktopRight);
            Local.height = static_cast<int>((DesktopRight * 3) >> 2);
        }
    }

    Local.depth_bpp = -1;
    Local.mode_field00 = -1;
    bool bLaidOut = false;
    if (Local.fullscreen != 0)
    {
        ApplyGameWindowLayout(Hwnd, true, static_cast<unsigned>(Local.width),
                              static_cast<unsigned>(Local.height));
        bLaidOut = true;
    }

    if (Pipeline->CreateBuffers(Hwnd, reinterpret_cast<int*>(&Local), reinterpret_cast<int*>(Out)) !=
        0)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: RenderMgr - failed to create_buffers(%d,%d) %d,%d\n",
              kRenderMgrCppPath, 0x240, Local.width, Local.height, Local.color_bpp,
              Local.depth_bpp);
        return false;
    }

    // 0x00424ad4: FUN_00424510, the gamma from DAT_00610858; result unread.
    RenderMgrApplyGamma();

    if (DALib::VertexBuffer->VbMgrInit(reinterpret_cast<void*>(1), reinterpret_cast<void*>(0x252),
                                       reinterpret_cast<void*>(0xfc00), nullptr) != 0)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRenderMgrCppPath, 0x24b,
              "unable to initialize VertexBuffer manager\n");
        return false;
    }

    if (!bLaidOut)
        ApplyGameWindowLayout(Hwnd, Out->fullscreen != 0, static_cast<unsigned>(Out->width),
                              static_cast<unsigned>(Out->height));

    // 0x00424b41: restore the FPU precision the renderer wants if the device changed it.
    const unsigned int Control = _control87(0, 0);
    G_FpuPrecisionFound = ((Control & 0x30000) == 0x20000) ? 0x18 : 0x35;
    if (G_FpuPrecisionFound != G_FpuPrecisionWanted)
    {
        if (G_FpuPrecisionWanted == 0x18)
            _control87(0x20000, 0x30000);
        else if (G_FpuPrecisionWanted == 0x35)
            _control87(0x10000, 0x30000);
        else
            _control87(0, 0);
    }

    // 0x00424b9e..0x00424bba: FUN_00421620(0, 0, w, h, RenderMgr.cpp, 0x269), the full-surface
    // viewport (DAT_00610850 width, DAT_00610854 height).
    Win3DSetViewportRect(0, 0, static_cast<int>(G_RenderSurfaceWidth),
                         static_cast<int>(G_RenderSurfaceHeight),
                         "E:\\FL\\Scratch\\Source\\Client\\cmn\\RenderMgr.cpp", 0x269);
    InitializeRichText(static_cast<int>(G_RenderSurfaceHeight));
    ReinitializeRichTextFonts();
    // 0x00424bd1..0x00424bdf: FUN_004213d0(w, h) -- both arguments unread: render-state defaults.
    Win3DResetState();
    // 0x00424bf1 FUN_004180c0(w, h) -- cdecl, both arguments unread: font device reset (layout
    // generation bump, number-face glyphs dropped, GDI scratch rebuilt).
    FontDeviceReset();
    // FUN_0059dcc0(w, h): the UI scene camera viewport {0, 0, w - 1, h - 1}.
    {
        int ViewRect[4];
        ViewRect[0] = 0;
        ViewRect[1] = 0;
        ViewRect[2] = static_cast<int>(G_RenderSurfaceWidth) - 1;
        ViewRect[3] = static_cast<int>(G_RenderSurfaceHeight) - 1;
        SpaceCameraSetViewportEx(GetWin3DSceneCamera(), ViewRect, 1);
    }
    // 0x00424c08..0x00424c23: the current client state (DAT_0067ecac) non-null -> its slot 11
    // (+0x2c) Resize(DAT_00610850, DAT_00610854), thiscall RET 8: space LAB_0054a540
    // (space_state.cpp), GF and IntroFocus LAB_00437400 (gameflow.cpp / client_state.cpp),
    // MissionBriefing 0x0053aaa0 (mission_briefing.cpp).
    if (g_client.current_state != nullptr)
    {
        g_client.current_state->Resize(static_cast<int>(G_RenderSurfaceWidth),
                                       static_cast<int>(G_RenderSurfaceHeight));
    }
    // 0x00424c26..0x00424c45: FUN_00420350(DAT_00610865, w, h, DAT_006679f0).
    MouseSetEnabled(ClientAppIsActive(), static_cast<int>(G_RenderSurfaceWidth),
                    static_cast<int>(G_RenderSurfaceHeight), G_bRenderSurfaceFullscreen);
    G_bRenderPostCreate = true; // 0x00424c4f
    return true;
}

// FUN_004247c0 (.text:004247c0-00424865).
void ShutdownRenderMgr(bool bRestoreWindowedMode)
{
    if (G_bRenderMgrShuttingDown)
    {
        // 0x0042485a: a nested call only clears the latch.
        G_bRenderMgrShuttingDown = false;
        return;
    }

    HWND const Hwnd = G_RenderMgrWindow;
    G_bRenderMgrShuttingDown = true;
    ApplyGameWindowLayout(Hwnd, false, 10, 10);
    if (DALib::Pipeline != nullptr && bRestoreWindowedMode)
    {
        if (g_display.width != 0 && g_display.height != 0 && g_display.fullscreen != 0)
        {
            DisplaySettings Windowed;
            memcpy(&Windowed, &g_display, 0x28);
            Windowed.fullscreen = 0;
            SetRenderModeWithFallback(Hwnd, &Windowed);
        }
        DALib::VertexBuffer->VbMgrReleaseAll();  // +0x10
        DALib::Pipeline->DestroyGpuResources(); // +0x38
    }
    G_bRenderMgrShuttingDown = false;
}

// FUN_005b1300 -- Client/main.cpp InitializeEngine, cdecl(FClientCommandLine*).
bool InitializeEngine(const FClientCommandLine* CommandLine)
{
    if (!DALib::Startup(g_client.window, nullptr))
        return false;
    DALib::OpenData(g_display.data_path.c_str());
    if (!PumpInitMessages())
        return false;

    // 0x005b13e4..0x005b152d.
    if (DALib::Data == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: could not find DATA directory (%s)\n", kMainCppPath,
              0x2f8, g_display.data_path.Text);
        return false;
    }
    if (DALib::Hardpoint == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x2fd,
              "IHardpoint");
        return false;
    }
    if (DALib::Renderer == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x302,
              "IRenderer");
        return false;
    }
    if (DALib::TextureLib == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x307,
              "ITextureLibrary");
        return false;
    }
    if (DALib::MaterialLib == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x30c,
              "IMaterialLibrary");
        return false;
    }
    if (DALib::VertexBuffer == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x311,
              "IVertexBufferManager");
        return false;
    }
    if (DALib::Alchemy == nullptr)
    {
        // Reported, not fatal.
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x316,
              "IAlchemy");
    }
    if (DALib::MaterialBatcher == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing DA component (%s)\n", kMainCppPath, 0x31b,
              Iids::MaterialBatcher2);
        return false;
    }
    if (!PumpInitMessages())
        return false;

    Async::Startup();
    Timing::init();
    if (!StartupRenderPipeline())
        ShowEngineFatalError(0xd7);
#ifdef FREELANCER_RP9
    // Port-only: QueryInterface the Direct3D 9 extensions (RP9.dll) and read
    // DATA_EXTRA/VOLUMETRIC/volumetric.ini. With RP8 (DirectX8) the QI fails and nothing changes.
    RenderEx::Startup();
#endif

    // 0x005b1684: the local render mode PerfOptions.ini's [Display] may overwrite.
    DisplaySettings Mode;
    Mode.mode_field00 = -1;
    Mode.refresh_rate = 0x3c;
    Mode.width = 0;
    Mode.height = 0;
    Mode.color_bpp = -1;
    Mode.depth_bpp = -1;
    Mode.stencil_bpp = 0;
    Mode.interval = 1;
    Mode.buffer_count = 2;
    Mode.border = 1;
    Mode.fullscreen = 1;
    static_assert(offsetof(DisplaySettings, height) == offsetof(DisplaySettings, width) + 4 &&
                      offsetof(DisplaySettings, color_bpp) == offsetof(DisplaySettings, width) + 8 &&
                      offsetof(DisplaySettings, depth_bpp) == offsetof(DisplaySettings, width) + 12,
                  "FPerfDisplaySettings mirrors mode +0x08..+0x14");
    const bool bPerfOptionsCurrent =
        LoadPerfOptions("PerfOptions.ini", reinterpret_cast<FPerfDisplaySettings*>(&Mode.width));

    // 0x005b16da..0x005b1780: FUN_005b0220(&text) builds the low-machine warning into a
    // zeroed two-word text. False -> FUN_00444e20. True -> ShowCursor(TRUE) until the count is
    // >= 0 (the extra calls counted), DialogBoxParamA(resources DAT_0067eca8, 0x82, window
    // DAT_0067eca0, FUN_005b05a0, &text), ShowCursor(FALSE) extra + 1 times, free(text) (not
    // zeroed), and anything but 1 from the dialog fails InitializeEngine (LAB_005b1413). The
    // retail EH state around the text has no exception source between these calls.
    {
        FHudWideText WarningText;
        WarningText.Text = nullptr;
        WarningText.Capacity = 0;
        if (!BuildLowMachineWarning(&WarningText))
        {
            WideTextFree(&WarningText);
        }
        else
        {
            int ExtraShows = 0;
            if (ShowCursor(TRUE) < 0)
            {
                do
                {
                    ++ExtraShows;
                } while (ShowCursor(TRUE) < 0);
            }
            INT_PTR const Result =
                DialogBoxParamA(g_client.resources, MAKEINTRESOURCEA(0x82), g_client.window,
                                LowMachineWarningDialogProc,
                                reinterpret_cast<LPARAM>(&WarningText));
            if (ExtraShows >= 0)
            {
                ++ExtraShows;
                do
                {
                    ShowCursor(FALSE);
                    --ExtraShows;
                } while (ExtraShows != 0);
            }
            bool const bAccepted = Result == 1;
            if (WarningText.Text != nullptr)
                free(WarningText.Text);
            if (!bAccepted)
                return false;
        }
    }

    ApplyDisplayCmdlineOverrides(&Mode, CommandLine);
    if (!SelectInitialRenderMode(g_client.window, &Mode, Mode.fullscreen))
        ShowEngineFatalError(0xc9);

    if (!bPerfOptionsCurrent)
    {
        NnPreferences::SeedGraphicsDefaults(); // FUN_004a9380 (result not read, 0x005b1876)
        ApplyChangedPerfOptions(); // FUN_005b40f0
    }
    // 0x005b1885: FUN_005b3ec0 writes [Display] from the options object (DAT_00679bc8..bd4).
    SavePerfOptions("PerfOptions.ini");
    DALib::Alchemy->CreateVertexBuffer(); // +0x34, no null check (retail)

    if (!PlayStartupMovies())
        return false;

    // 0x005b19c8..0x005b1a06: the startup picture -- CMP [DAT_00679bc8],0x500 / JC (width,
    // unsigned) and CMP [DAT_00679bd0],0x20 (colour bits) pick one of four .rdata strings.
    char const* SplashPath = nullptr;
    if (static_cast<unsigned>(g_display.width) >= 0x500u)
    {
        if (g_display.color_bpp == 0x20)
            SplashPath = "../Data/Interface/Intro/images/startupscreen_1280.tga"; // 0x005e6b90
        else
            SplashPath = "../Data/Interface/Intro/images/startupscreen_1280_16.bmp"; // 0x005e6b54
    }
    else if (g_display.color_bpp == 0x20)
    {
        SplashPath = "../Data/Interface/Intro/images/startupscreen.tga"; // 0x005e6b20
    }
    else
    {
        SplashPath = "../Data/Interface/Intro/images/startupscreen_16.bmp"; // 0x005e6aec
    }

    // 0x005b1a06..0x005b1a11: the loading screen DAT_0067c3b8 is registered with Common.dll,
    // gets the same picture as its Image, and runs once.
    LoadingScreenRegister();              // FUN_00579180
    LoadingScreenSetImage(SplashPath);    // FUN_00579220
    LoadingScreenStart();                 // FUN_00579290

    // 0x005b1a16..0x005b1ab9: a stack Image (vptr 0x005e2718, +0x14 = 0) shows the picture for
    // two frames, then drops its BigImage. The object itself lives to the end of the function
    // (dtor FUN_00565010 at 0x005b1c07 / 0x005b1fd1).
    Image Splash;
    Win3DSetViewport(nullptr, kMainCppPath, 0x3a4);   // FUN_00422610(0, main.cpp, 0x3a4)
    Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 1.0f); // FUN_004214d0
    if (Splash.Load(SplashPath))                        // FUN_00565430
    {
        if (BeginClientRenderFrame()) // FUN_00424f30
        {
            ClearWin3DBuffers(0x4100); // FUN_004215e0
            Splash.Draw();             // FUN_00565a10
            EndClientRenderScene();    // FUN_00425080
            PresentClientRenderFrame(); // FUN_00425140
        }
        if (BeginClientRenderFrame())
        {
            ClearWin3DBuffers(0x4100);
            Splash.Draw();
            EndClientRenderScene();
            PresentClientRenderFrame();
        }
    }
    Splash.Release(); // FUN_005650d0

    RefreshLoadingProgress();
    if (!PumpInitMessages())
        return false;
    RefreshLoadingProgress();
    srand(timeGetTime());
    if (CreateID("Freelancer") == 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kMainCppPath, 0x3d1,
              "Failed to initialize global ID maker instance.\n");
        return false;
    }

    RefreshLoadingProgress();
    // 0x005b1baa..0x005b1bc1: a stack DataConfigPump (vptr 0x005e6ab8 stored at [ESP+0x2c])
    // pumps messages for every [Data] value DataConfig::startup reads.
    DataConfigPump Pump;
    const int ConfigResult = DataConfig::startup(g_display.ini_path.c_str(),
                                                 &Archetype::GCliObjFactory, &Pump);
    if (ConfigResult == 1)
        return false;
    if (ConfigResult != 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kMainCppPath, 0x3e1,
              "Config::startup() failed.\n");
        return false;
    }

    RefreshLoadingProgress();
    if (!PumpInitMessages())
        return false;

    // 0x005b1ca0 FUN_00468c20: the compiled-in chat text styles.
    ChatRegisterTextStyles();
    // 0x005b1ca5..0x005b1caf: FUN_005b0c30(DAT_00679d04) -- freelancer.ini's client [Data]
    // keys, its [Resources] DLLs and audio\SoundCFG.ini; the result is not read.
    LoadClientIniData(g_display.ini_path.c_str());
    if (!PumpInitMessages())
        return false;

    RefreshLoadingProgress();
    // 0x005b1d46..0x005b1d53: FUN_0059d5a0(DAT_00679bc8, DAT_00679bcc).
    Win3DStartupManager(g_display.width, g_display.height);
    InitUiSoundTable(); // 0x005b1d58: FUN_00564520
    // 0x005b1d5d..0x005b1d73: DAT_0067ecb4 = FUN_0059db20("WinDebug", "WinDebug", 0, 0). No
    // factory answers to "WinDebug", so this stores null (see globals.h).
    G_WinDebugRoot = WinFactoryCreate("WinDebug", "WinDebug", nullptr, nullptr);
    // 0x005b1d78: FUN_0041ef20 and the inlined pump follow the store directly.
    if (!PumpInitMessages())
        return false;

    // Slot 8 (+0x20, PreInit) of the four client state singletons, a message pump after the
    // first two. IntroFocus's and SpaceApp's are 0x00401e90, a bare RET.
    GetIntroFocusState()->PreInit(); // 0x005b1e04..0x005b1e0d: FUN_00575430 slot 8
    if (!PumpInitMessages())
        return false;
    GetSpaceState()->PreInit();      // 0x005b1e9c..0x005b1ea5: FUN_0054bd30 slot 8
    if (!PumpInitMessages())
        return false;
    // 0x005b1f47..0x005b1f50: FUN_00437420 slot 8 (LAB_00437270) -> FUN_00558850: FUN_004393b0
    // (the location maps), FUN_004354e0 (the gf\CharacterInfo.cpp script database), JMP
    // FUN_004355e0 (the "preload =" scripts), then GF +0x04 = +0x05 = 0.
    GetGameflowState()->PreInit();
    GetMissionBriefingState()->PreInit(); // 0x005b1f53..0x005b1f5c: FUN_0043d560 slot 8 -> LAB_0043d170

    EnsureClientIMEFocus();                                   // FUN_00564060
    InitializeRichText(static_cast<int>(G_RenderSurfaceHeight)); // 0x005b1f6a, DAT_00610854
    ReinitializeRichTextFonts();
    DeviceTRC::set_image_root(g_display.data_path.c_str());   // 0x005b1f7b, DAT_00679e0c
    ChatInit();                                               // FUN_004690c0
    MenuUiInitToolTipAndRollover(); // FUN_0059db20("ToolTip") + FUN_0059db20("Rollover")
    RefreshLoadingProgress();
    WarmUpEffectShapes(); // FUN_004f70d0
    RefreshLoadingProgress();

    // 0x005b1fb8: FUN_0042d770. Its Delta (DAT_00667cf8) is a CRT-constructed static (XC
    // 0x0060f18c -> Timing::Delta::Delta) that the first stop() starts by itself, and
    // DAT_00667d20 was seeded by the XC entry 0x0060f188 (timeGetTime) -- nothing to start here.
    TickClientFrameTimer(); // FUN_0042d770
    LoadingScreenReset(); // 0x005b1fbd: FUN_00579190

    return true;
}

// freelancer.exe FUN_005b73e0 (.text:005b73e0-005b7418), 21 callers. The exe's own copy of the
// 3DMathEngine acquire -- retail Common.dll has a separate, unexported one (FUN_06391cf0) and
// does not export it, so each module defines the function common_csys.h declares. Builds a
// 0xC-byte descriptor {size 0xC, "3DMathEngine", 0} on the stack and calls
// DACOM_Acquire()->CreateInstance (vtable +0x0C). No cache here: retail's callers keep their
// own DAT_ statics. No null check on the manager, as retail.
I3DMathEngine* __cdecl Acquire3DMathEngine()
{
    struct FMathEngineDesc
    {
        unsigned long Size;
        const char* Name;
        void* Implementation;
    };

    FMathEngineDesc Desc;
    Desc.Size = 0xC;
    Desc.Name = "3DMathEngine";
    Desc.Implementation = nullptr;

    void* Instance = nullptr;
    DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);
    return static_cast<I3DMathEngine*>(Instance);
}

// freelancer.exe's own port helper (Common.dll's former twin of it, declared in the dissolved
// common_stubs.h, no longer exists; retail Common exports no such function). Same logic as Common's:
// DAEngine_create_filesystem(RelPath); on a miss, resolve through GetFileSysPath against
// DALib::Data and retry when the resolved path differs. NOT retail's exe mechanism: retail's
// callers (e.g. texture_shapes' [Texture] file= at .text:00425d40-00425dbc) strncpy the path
// (FUN_00417c50) and CreateInstance a "FileSystem" DACOMDESC under DALib::Data inline.
IFileSystem* DAEngine_open_filesystem(char const* RelPath)
{
    IFileSystem* Filesys = nullptr;
    if (RelPath == nullptr || RelPath[0] == '\0')
        return nullptr;

    DAEngine_create_filesystem(RelPath, &Filesys, nullptr);
    if (Filesys != nullptr)
        return Filesys;

    char FullPath[0x104];
    FullPath[0] = '\0';
    GetFileSysPath(FullPath, RelPath, DALib::Data);
    if (FullPath[0] != '\0' && DACOM_CRC::CompareStringsI(FullPath, RelPath) != 0)
        DAEngine_create_filesystem(FullPath, &Filesys, nullptr);
    return Filesys;
}
