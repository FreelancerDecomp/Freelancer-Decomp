// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "globals.h"
#include "imeui.h"
#include "input_manager.h"
#include "nn_error.h"
#include "nn_exit.h"
#include "nn_network_operation.h"

#include "DALib.h"
#include "common_misc.h" // SinglePlayer
#include "render/IRenderPipeline.h"

#include <windows.h>

DisplaySettings g_display = {};
ClientGlobals g_client = {};

// RenderMgr.cpp state (retail .data/.bss next to the functions below).
HWND G_RenderMgrWindow = nullptr;          // DAT_006679f4, written by FUN_004246e0
unsigned G_RenderSurfaceWidth = 0x280;     // DAT_00610850 (initialised .data)
unsigned G_RenderSurfaceHeight = 0x1e0;    // DAT_00610854 (initialised .data)
bool G_bRenderSurfaceFullscreen = false;   // DAT_006679f0
bool G_bRenderMgrShuttingDown = false;     // DAT_006679f9, FUN_004247c0's re-entry latch
bool G_bRenderPostCreate = true;           // DAT_00610864 (initialised .data 01h)
bool G_bRenderFrameBegun = false;          // DAT_006679f8 (.data, no initialiser)

// FUN_004246e0 (.text:004246e0-00424791), cdecl(ClassName, Display, Title). A zero width or height
// in the options object is replaced by 640/480 locally only -- retail never writes it back. The
// window handle is also stored in RenderMgr's DAT_006679f4 (0x00424788).
HWND CreateGameWindow(const char* class_name, DisplaySettings* display, const char* title)
{
    const HINSTANCE instance = GetModuleHandleA(nullptr);
    int width = display->width;
    if (width == 0)
        width = 0x280;
    int height = display->height;
    if (height == 0)
        height = 0x1e0;

    int x = 0;
    int y = 0;
    if (display->fullscreen == 0)
    {
        RECT rc = {0, 0, width, height};
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
        if (AdjustWindowRect(&rc, 0x2CA0000, FALSE))
            width = rc.right - rc.left;
        // 0x00424758: the height is taken from the rect whether or not the call succeeded.
        height = rc.bottom - rc.top;
    }

    G_RenderMgrWindow = CreateWindowExA(0, class_name, title, 0x2CA0000, x, y, width, height,
                                        nullptr, nullptr, instance, nullptr);
    return G_RenderMgrWindow;
}

// FUN_00424870 (.text:00424870-004249b8), cdecl(hwnd, fullscreen, width, height): records the
// surface size and mode, then restyles and resizes the window for it.
void ApplyGameWindowLayout(HWND Hwnd, bool bFullscreen, unsigned Width, unsigned Height)
{
    G_RenderSurfaceWidth = Width;
    G_RenderSurfaceHeight = Height;
    G_bRenderSurfaceFullscreen = bFullscreen;

    if (bFullscreen)
    {
        // 0x00424898: never smaller than the desktop (unsigned compares, JAE).
        RECT Desktop;
        GetWindowRect(GetDesktopWindow(), &Desktop);
        if (Width < static_cast<unsigned>(Desktop.right))
            Width = static_cast<unsigned>(Desktop.right);
        if (Height < static_cast<unsigned>(Desktop.bottom))
            Height = static_cast<unsigned>(Desktop.bottom);

        SetWindowLongA(Hwnd, GWL_STYLE, static_cast<LONG>(0x80000000)); // WS_POPUP
        SetWindowPos(Hwnd, nullptr, 0, 0, 0, 0, 0x27);
        SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, static_cast<int>(Width), static_cast<int>(Height),
                     0x100);
        ShowWindow(Hwnd, SW_SHOWNORMAL);
        return;
    }

    // 0x00424906.
    SetWindowLongA(Hwnd, GWL_STYLE, 0x2ca0000);
    SetWindowPos(Hwnd, nullptr, 0, 0, 0, 0, 0x27);
    RECT Frame;
    Frame.left = 0;
    Frame.top = 0;
    Frame.right = static_cast<LONG>(Width);
    Frame.bottom = static_cast<LONG>(Height);
    // Argument evaluation order as retail: ex-style, menu, style.
    const DWORD ExStyle = static_cast<DWORD>(GetWindowLongA(Hwnd, GWL_EXSTYLE));
    const BOOL bMenu = GetMenu(Hwnd) != nullptr ? TRUE : FALSE;
    const DWORD Style = static_cast<DWORD>(GetWindowLongA(Hwnd, GWL_STYLE));
    AdjustWindowRectEx(&Frame, Style, bMenu, ExStyle);
    SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, Frame.right - Frame.left, Frame.bottom - Frame.top,
                 0x102);
    ShowWindow(Hwnd, G_bRenderMgrShuttingDown ? SW_HIDE : SW_SHOWNORMAL);
    InvalidateRect(nullptr, nullptr, TRUE);
}

namespace
{
    void ReleaseRenderDeviceForDeactivate()
    {
        // DALib::Pipeline slot 14 (+0x38) — what LAB_005b26b0 calls before handing the
        // deactivation on to DefWindowProc.
        if (DALib::Pipeline == nullptr)
            return;
        DALib::Pipeline->DestroyGpuResources();
    }

    // LAB_005b2570 state.
    //
    // DAT_00615ba8 — the WinMain loop's present gate: `if (DAT_00615ba8) FUN_005b2890(dt);`
    // (0x5b0fe8). WM_ACTIVATEAPP is its only writer.
    bool G_bPresentAllowed = true;
    // DAT_00610865 — "application is active", latched by FUN_00424c60.
    bool G_bAppActive = true;
    // DAT_0067ecc8 / DAT_0067ecc9 — WinMain sets the first once InitializeEngine returns, and
    // SC_CLOSE sets the second when it arrives before that.
    bool G_bQuitDialogReady = false;
    bool G_bQuitDialogPending = false;

    // DAT_0067a438 is read exactly once in the whole PE (005b2667) and never written, so the
    // WM_ACTIVATEAPP branch it guards always takes the zero path. Kept as a named constant
    // rather than silently folded away.
    const bool kActivateOverrideByte = false;

    bool IsSinglePlayerSession()
    {
        // COMMON.DLL::SinglePlayer — retail default is true.
        return SinglePlayer();
    }

    void OnActivateApp(bool bActive)
    {
        // LAB_005b264c. DAT_00615ba9 is the local "active" copy; the derived gate is
        // DAT_00615ba8, which stays 1 whenever the app is not a fullscreen single-player
        // session (there is nothing to release then).
        const bool bFullscreen = g_display.fullscreen != 0;
        if (!IsSinglePlayerSession() || (kActivateOverrideByte && !bFullscreen))
            G_bPresentAllowed = true;
        else
            G_bPresentAllowed = bActive;

        // FUN_00424c60: a fullscreen surface (DAT_006679f0, 0x00424c60) re-activated after it
        // was inactive clears the post-create flag DAT_00610864; then DAT_00610865 latches the
        // new state and FUN_00420350(bActive, w, h, fullscreen) takes or releases the mouse.
        if (G_bRenderSurfaceFullscreen && !G_bAppActive && bActive)
            G_bRenderPostCreate = false;
        G_bAppActive = bActive;
        MouseSetEnabled(bActive, static_cast<int>(G_RenderSurfaceWidth),
                        static_cast<int>(G_RenderSurfaceHeight), G_bRenderSurfaceFullscreen);
    }

    bool OnSysCommandClose()
    {
        // LAB_005b2727: FUN_00424cb0 gates on app-active + post-create + buffers-ready.
        // Then TEST EAX after FUN_00473ca0 (body = FUN_0059d9c0("NN_Error") — nonzero
        // swallows) and FUN_00473be0 ("NN_NetworkOperation"). Then FUN_00471f30 NN_Exit.
        if (!RenderMgrFrameReady())
            return false;
        if (NnErrorFind() != nullptr)      // FUN_00473ca0
            return false;
        if (NnNetworkOperationIsOpen())    // FUN_00473be0
            return false;
        if (!G_bQuitDialogReady)
        {
            G_bQuitDialogPending = true;
            return true;
        }
        RequestClientQuitDialog();
        return true;
    }
}

bool ClientPresentAllowed()
{
    return G_bPresentAllowed;
}

bool ClientAppIsActive()
{
    return G_bAppActive;
}

// FUN_00424cb0 (.text:00424cb0-00424cd3): three byte tests, 1 or 0.
bool RenderMgrFrameReady()
{
    return G_bAppActive && G_bRenderPostCreate && G_bRenderFrameBegun;
}

void ClientNoteEngineReady()
{
    // WinMain 0x5b0fbe: DAT_0067ecc8 = 1; if (DAT_0067ecc9) FUN_00471f30(0);
    G_bQuitDialogReady = true;
    if (G_bQuitDialogPending)
    {
        G_bQuitDialogPending = false;
        RequestClientQuitDialog();
    }
}

void RequestClientQuitDialog()
{
    // 005b274b / 005b0fc8: FUN_00471f30(0) -- raise the NN_Exit confirmation. Its confirm paths
    // call DestroyWindow on the game window themselves.
    NnExitOpen(false);
}

LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    // LAB_005b2570 head. The IME gets first refusal on every message; a true return means it
    // consumed it, and the LRESULT comes back through the out parameter. Retail hands WndProc
    // the GLOBAL window handle (DAT_0067eca0) rather than the hwnd it was called with, and
    // reuses the lparam argument slot as that out parameter (005b258f / 005b25ac).
    IMEFocus* const Ime = IMEFocus::GetSingleton();
    if (Ime != nullptr)
    {
        int Handled = 0;
        if (Ime->WndProc(g_client.window, msg, wparam, lparam, &Handled))
            return Handled;
    }

    switch (msg)
    {
    case WM_SETFOCUS:
        ClientInputOnFocusGained(); // caseD_7 — FUN_0041eac0
        break;
    case WM_KILLFOCUS:
        ClientInputOnFocusLost(); // caseD_8 — FUN_0041ecf0
        break;
    case WM_ERASEBKGND:
        // caseD_14 — FUN_00425160 returns DAT_00610864; non-zero means "the engine owns the
        // surface, do not let GDI erase it".
        if (G_bRenderPostCreate)
            return 1;
        break;
    // WM_PAINT (0xF): retail switch byte at 0x005b27b9 = 5 → caseD_3 → DefWindowProcA
    // (LAB_005b277e). No BeginPaint/EndPaint / ValidateRect here — those were a port scaffold.
    case WM_ACTIVATEAPP:
        OnActivateApp(wparam == 1);
        if (!G_bAppActive && g_display.fullscreen != 0)
        {
            // DALib::Pipeline slot 14 (+0x38) — release the device while minimised.
            ReleaseRenderDeviceForDeactivate();
        }
        break;
    case WM_NCHITTEST:
        // LAB_005b26fb — a fullscreen window claims every pixel as client area.
        if (g_display.fullscreen != 0)
            return HTCLIENT;
        break;
    case WM_SYSKEYUP:
        // 0x105 — swallowed so Alt releases never reach DefWindowProc's menu handling.
        return 0;
    case WM_SYSCOMMAND:
        if (wparam == SC_CLOSE)
        {
            OnSysCommandClose();
            return 0;
        }
        if (wparam == SC_SCREENSAVE)
            return 0;
        break;
    case WM_MOUSEMOVE:
        // 0x005b276c..0x005b2776: CMP ESI,0x200 -> FUN_0041e890, then caseD_3 -> DefWindowProcA.
        // Retail's WndProc has no WM_LBUTTONDOWN / WM_LBUTTONUP case: buttons arrive through
        // the DirectInput queue (MouseDispatchEvents, FUN_0041ef20) and the Win3D handler.
        ClientInputOnMouseMove();
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}
