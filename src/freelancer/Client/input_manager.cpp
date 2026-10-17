// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail E:\FL\Scratch\Source\Client\cmn\mouse.cpp (.text 0x0041d970..0x004207a0): the
// DirectInput8 mouse, its event queue, and the named software cursors. See input_manager.h for
// the three pieces of state and the flags byte.
//
// x87 note: retail evaluates every axis and quad formula on the x87 stack (64-bit mantissa) and
// rounds only where it stores to a float. The port evaluates the same expressions in double and
// rounds at the same stores; the two agree except in the last bits of a double.

#include "input_manager.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "game_options.h"
#include "globals.h"
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "hud_command_dispatch.h"
#include "menu_keymap_config.h"
#include "nn_modal.h" // NnMainButtonOnUnclaimedMouse (FUN_0045a270)
#include "texture_shapes.h"
#include "win3d_immediate.h"

#include "DALib.h"
#include "common_ini.h"
#include "common_tstring.h"
#include "dacom.h"
#include "readfile.h"
#include "render/IRenderPipeline.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <list>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DirectInput8Create, and IID_IDirectInput8A / GUID_SysMouse / c_dfDIMouse2 (retail links the
// same three statically: .rdata 0x005e752c, 0x005e74ec, 0x005e772c).
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

static_assert(sizeof(DIDEVICEOBJECTDATA) == 0x14, "FUN_0041fa10 walks 0x14-byte records");
static_assert(sizeof(DIMOUSESTATE2) == 0x14, "FUN_0041fd90 reads 0x14 bytes into +0x2c");

namespace
{
    char const kMouseCpp[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\mouse.cpp"; // 005c952c

    // The flags byte at +0x14 (DAT_006167cc).
    unsigned char const kMouseActive = 0x01;
    unsigned char const kMouseAbsolute = 0x02;
    unsigned char const kMouseAcquired = 0x04;
    unsigned char const kMouseExclusive = 0x08;
    unsigned char const kMouseClampToSurface = 0x10;
    unsigned char const kMouseNeedsPoll = 0x20;
    unsigned char const kMouseBuffered = 0x40;

    // FUN_00420060 / its inlined copies: the buffered-data size they ask for.
    unsigned const kMouseBufferEntries = 0x80;
    unsigned const kMouseButtonCount = 8;
    // FUN_0041f150 0x0041f2fb: the wheel is queued as "button" 8.
    unsigned const kMouseWheelButton = 8;
    // FUN_0041fa10 0x0041fb06 / 0x0041fb12: the double-click slop, in pixels, per axis.
    int const kDoubleClickSlop = 4;
    // FUN_0041df30 0x0041e099: at most 0x14 cursors are kept.
    unsigned const kMaxCursors = 0x14;

    // .rdata 0x005c9680 (double) and 0x005c967c (float, 1/120): the axis and wheel scales.
    double const kAxisScale = 0.00125;
    float const kWheelScale = 0.008333333767950535f;
    // .rdata 0x005c8d30 (float): the cursor quad's surface-width scale in FUN_0041d9c0.
    float const kCursorQuadScale = 0.0012499999720603228f;

    // The render states FUN_0041f150 saves and forces around the cursor quad.
    int const kRsZEnable = 7;
    int const kRsAlphaTestEnable = 0xf;
    int const kRsSrcBlend = 0x13;
    int const kRsDestBlend = 0x14;
    int const kRsCullMode = 0x16;
    int const kRsAlphaBlendEnable = 0x1b;

    // The mouse object at DAT_006167b8 (0x74 bytes). Retail constructs it on first use behind
    // DAT_0061682c bit 0 (FUN_0041e740, then atexit LAB_0041e880 -> FUN_004a2d30, which only
    // destroys the list). The constructor has no effect beyond these stores, so the port holds
    // it as a namespace-scope object: zero-initialised like retail's .bss, then the stores below.
    struct FMouseDevice
    {
        // +0x04 head / +0x08 size: 0x2c-byte nodes, the event at node+8 (FUN_004207b0 /
        // FUN_0041f850 / FUN_0041f8a0 insert before the sentinel; FUN_0041ef20 pops the front).
        std::list<FMouseEvent> Events;
        IDirectInput8A* DirectInput;   // +0x0c DAT_006167c4
        IDirectInputDevice8A* Device;  // +0x10 DAT_006167c8
        unsigned char Flags;           // +0x14 DAT_006167cc
        int AbsoluteBase[3];           // +0x18 / +0x1c / +0x20 -- FUN_0041f7b0, absolute mode only
        unsigned BufferSize;           // +0x24 DAT_006167dc
        DIDEVICEOBJECTDATA* Buffer;    // +0x28 DAT_006167e0
        DIMOUSESTATE2 State;           // +0x2c
        unsigned DoubleClickTime;      // +0x40 DAT_006167f8
        unsigned LastPressTime[kMouseButtonCount]; // +0x44
        float Remainder[3];            // +0x64 / +0x68 / +0x6c -- sub-pixel carry per axis
        bool bMoved;                   // +0x70 DAT_00616828 -- written only (0x0041f2a0 / 0x0041f2af)

        // FUN_0041e740: +0x0c, +0x10, the +0x14 dword, +0x68, +0x64, +0x24, +0x44..+0x63, +0x28
        // and the +0x70 byte. +0x6c, +0x18..+0x20, +0x2c..+0x40 are left as the .bss gave them.
        FMouseDevice()
            : DirectInput(nullptr)
            , Device(nullptr)
            , Flags(0)
            , BufferSize(0)
            , Buffer(nullptr)
            , bMoved(false)
        {
            Remainder[1] = 0.0f;
            Remainder[0] = 0.0f;
            unsigned Index = 0;
            for (; Index < kMouseButtonCount; ++Index)
                LastPressTime[Index] = 0;
        }
    };

    FMouseDevice G_Mouse;              // DAT_006167b8

    bool G_bDeviceCreated = false;     // DAT_00616854
    bool G_bCursorsReady = false;      // DAT_00616855
    bool G_bInputActive = false;       // DAT_0061685c
    int G_DispatchDepth = 0;           // DAT_00616860 -- FUN_0041ef20 nesting; written only

    int G_CursorX = 0;                 // DAT_00616840
    int G_CursorY = 0;                 // DAT_00616844
    int G_CursorZ = 0;                 // DAT_00616848
    int G_MouseWheel = 0;              // DAT_0061684c
    unsigned G_MouseButtons = 0;       // DAT_00616850

    // DAT_006107d0 -- the axis multiplier, .data 1.0f; written only by LAB_0041d970.
    float G_MouseSpeed = 1.0f;
    // DAT_006107d4 / DAT_006107d8 -- the cursor position at the last press, .data -777 / -777.
    int G_LastClickX = -777;
    int G_LastClickY = -777;
    // DAT_006107dc -- .data 01; FUN_0041dd90 / FUN_0041dda0.
    unsigned char G_MouseCursorDrawFlag = 1;

    // The cursor record FUN_0041df30 allocates (operator new 0x90) and FUN_0041e550 constructs.
    struct FMouseCursor
    {
        TString<32> Name;       // +0x00 "nickname" (FUN_00420750)
        FTexShape* Shape;       // +0x24 resolved by FUN_0041dae0 from ShapeName
        TString<24> ShapeName;  // +0x28 "shape" (FUN_00417c50, 0x18)
        float HotspotX;         // +0x44 "hotspot" [0]
        float HotspotY;         // +0x48 "hotspot" [1]
        unsigned char Color[4]; // +0x4c "color" R, G, B, A (each 0..0xff; A 0 reads as 0xff)
        int Blend;              // +0x50 "blend" -- the FUN_0041f150 blend-mode switch
        float Scale;            // +0x54 "scale"
        float Angle;            // +0x58 "angle", or Spin * time
        float Spin;             // +0x5c "spin"
        unsigned FrameCount;    // +0x60 "anim" [1]
        unsigned Frame;         // +0x64
        float Fps;              // +0x68 "anim" [2]
        TString<32> AnimName;   // +0x6c "anim" [0] -- a printf format taking the frame
    };
    static_assert(sizeof(FMouseCursor) == 0x90, "FUN_0041df30 allocates 0x90 bytes");

    // FUN_0041e550. Fps (+0x68) is not written by retail; operator new leaves it as it finds it.
    // The port zeroes it -- it is read only after "anim" has stored it (FUN_0041dae0 tests
    // AnimName first).
    void CursorConstruct(FMouseCursor* Cursor)
    {
        Cursor->Name.Length = 0;
        Cursor->Name.Data[0] = '\0';
        Cursor->ShapeName.Length = 0;
        Cursor->ShapeName.Data[0] = '\0';
        Cursor->AnimName.Length = 0;
        Cursor->AnimName.Data[0] = '\0';
        Cursor->Shape = nullptr;
        Cursor->HotspotX = 0.0f;
        Cursor->HotspotY = 0.0f;
        Cursor->Color[3] = 0xff;
        Cursor->Color[2] = 0xff;
        Cursor->Color[1] = 0xff;
        Cursor->Color[0] = 0xff;
        Cursor->Blend = 1;
        Cursor->Scale = 1.0f;
        Cursor->Spin = 0.0f;
        Cursor->Angle = 0.0f;
        Cursor->FrameCount = 0;
        Cursor->Frame = 0;
        Cursor->Fps = 0.0f;
    }

    FMouseCursor* G_Cursors[kMaxCursors] = {}; // DAT_00616744
    unsigned G_CursorCount = 0;                 // DAT_00616740 (LAB_0041dc10)
    FMouseCursor* G_CurrentCursor = nullptr;    // DAT_00616858
    TString<32> G_DefaultCursorName = {0, {0}}; // DAT_00616794 (LAB_0041dc00)

    // FUN_0041e5a0.
    int ClampColorByte(int Value)
    {
        if (Value < 0)
            return 0;
        if (Value > 0xff)
            return 0xff;
        return Value;
    }

    // FUN_00420620 -- vsprintf into a 0x18-byte stack buffer, then TString<24> assignment
    // (length clamped to 0x17). Retail's vsprintf is unbounded and a longer result overruns its
    // frame; the port bounds the format to the same buffer, which gives the same string for
    // every result retail survives.
    void FormatShapeName(TString<24>* Out, char const* Format, ...)
    {
        char Buffer[0x18];
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
        va_end(Args);
        Buffer[sizeof(Buffer) - 1] = '\0';
        *Out = Buffer;
    }

    // FUN_0041f8a0's / FUN_0041fd00's event: -1 - button in Key (0 for an index past the table,
    // which then leaves the three flag bytes unwritten), the flags, bMove 0, the index. Retail
    // builds it in an uninitialised stack slot; the fields no reader touches are zero here.
    void BuildButtonEvent(FMouseEvent* Event, unsigned ButtonIndex, bool bDown, bool bDouble)
    {
        memset(Event, 0, sizeof(*Event));
        Event->bMove = 0;
        Event->ButtonIndex = ButtonIndex;
        if (ButtonIndex >= 9)
        {
            Event->Record.Key = 0;
            return;
        }
        // The nine-entry table on the stack: -1, -2, ... -9.
        Event->Record.Key = static_cast<unsigned>(-1 - static_cast<int>(ButtonIndex));
        Event->Record.Byte18 = bDown ? 1 : 0;
        Event->Record.Byte19 = 0;
        Event->Record.Byte1A = bDouble ? 1 : 0;
    }

    // FUN_0041f8a0 (thiscall, RET 0xc): build and queue a button / wheel event.
    void QueueButtonEvent(unsigned ButtonIndex, bool bDown, bool bDouble)
    {
        FMouseEvent Event;
        BuildButtonEvent(&Event, ButtonIndex, bDown, bDouble);
        G_Mouse.Events.push_back(Event);
    }

    // FUN_0041f850: queue a move event -- only +0x1c is written.
    void QueueMoveEvent()
    {
        FMouseEvent Event;
        memset(&Event, 0, sizeof(Event));
        Event.bMove = 1;
        G_Mouse.Events.push_back(Event);
    }

    // Unacquire the device (when there is one), drop the acquired / exclusive bits and empty the
    // queue: the prologue FUN_0041e790, FUN_0041f720 and FUN_004201a0(0) share.
    void UnacquireAndFlush()
    {
        if (G_Mouse.Device != nullptr)
            G_Mouse.Device->Unacquire();
        G_Mouse.Flags = static_cast<unsigned char>(G_Mouse.Flags & 0xf3);
        G_Mouse.Events.clear();
    }

    // FUN_0041e790 (thiscall, RET 4): resize the DirectInput buffer. Nothing happens when the
    // size is already right. SetProperty is called on the device without a null test.
    bool SetBufferSize(unsigned Entries)
    {
        if (G_Mouse.BufferSize == Entries)
            return false;

        UnacquireAndFlush();

        DIPROPDWORD Property;
        Property.diph.dwSize = 0x14;
        Property.diph.dwHeaderSize = 0x10;
        Property.diph.dwObj = 0;
        Property.diph.dwHow = DIPH_DEVICE;
        Property.dwData = Entries;
        if (G_Mouse.Device->SetProperty(DIPROP_BUFFERSIZE, &Property.diph) < 0)
            return false;

        G_Mouse.BufferSize = Entries;
        operator delete(G_Mouse.Buffer);
        G_Mouse.Buffer = nullptr;
        if (G_Mouse.BufferSize != 0)
        {
            G_Mouse.Buffer = static_cast<DIDEVICEOBJECTDATA*>(
                operator new(G_Mouse.BufferSize * sizeof(DIDEVICEOBJECTDATA)));
        }
        return true;
    }

    // FUN_00420060 (thiscall, RET 8; the first argument, the window, is not read). No HRESULT is
    // tested: a failed DirectInput8Create leaves +0x0c null and the next call faults, as in
    // retail.
    void CreateDevice(bool bBuffered)
    {
        G_Mouse.DoubleClickTime = GetDoubleClickTime();
        DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                           reinterpret_cast<void**>(&G_Mouse.DirectInput), nullptr);
        G_Mouse.DirectInput->CreateDevice(GUID_SysMouse, &G_Mouse.Device, nullptr);
        G_Mouse.Device->SetDataFormat(&c_dfDIMouse2);

        unsigned char const Flags = static_cast<unsigned char>(G_Mouse.Flags | kMouseNeedsPoll);
        G_Mouse.Flags = static_cast<unsigned char>((Flags & ~kMouseBuffered) |
                                                   (bBuffered ? kMouseBuffered : 0));
        if ((G_Mouse.Flags & kMouseBuffered) != 0)
            SetBufferSize(kMouseBufferEntries);
    }

    // The `if (!DAT_00616854) { DAT_00616854 = 1; FUN_00420060(DAT_0067eca0, 1); }` block every
    // entry point opens with (inlined in most of them).
    void EnsureDevice()
    {
        if (G_bDeviceCreated)
            return;
        G_bDeviceCreated = true;
        CreateDevice(true);
    }

    // FUN_004201a0 (thiscall, RET 4).
    void SetActive(bool bActive)
    {
        if (bActive)
        {
            G_Mouse.Flags = static_cast<unsigned char>(G_Mouse.Flags | kMouseActive);
            G_Mouse.Remainder[1] = 0.0f;
            G_Mouse.Remainder[0] = 0.0f;

            // A windowed session picks the cursor up where the OS pointer is.
            POINT Point;
            if (g_display.width != 0 && g_display.height != 0 && g_display.fullscreen == 0 &&
                GetCursorPos(&Point) != 0 && ScreenToClient(g_client.window, &Point) != 0)
            {
                G_CursorX = Point.x;
                G_CursorY = Point.y;
                G_CursorZ = 0;
            }

            int const Width = static_cast<int>(G_RenderSurfaceWidth);
            int const Height = static_cast<int>(G_RenderSurfaceHeight);
            if (G_CursorX < 0)
                G_CursorX = 0;
            else if (G_CursorX >= Width)
                G_CursorX = Width - 1;
            if (G_CursorY < 0)
                G_CursorY = 0;
            else if (G_CursorY >= Height)
                G_CursorY = Height - 1;

            while (ShowCursor(FALSE) >= 0)
            {
            }
            return;
        }

        G_Mouse.Flags = static_cast<unsigned char>(G_Mouse.Flags & ~kMouseActive);
        UnacquireAndFlush();
        if (G_Mouse.Device != nullptr)
        {
            G_Mouse.Device->SetCooperativeLevel(g_client.window,
                                                DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            G_Mouse.Flags = static_cast<unsigned char>(G_Mouse.Flags & ~kMouseExclusive);
        }

        CURSORINFO Info;
        Info.cbSize = 0x14;
        GetCursorInfo(&Info);
        if ((Info.flags & CURSOR_SHOWING) != 0)
            return;

        POINT Point;
        Point.x = G_CursorX;
        Point.y = G_CursorY;
        if (ClientToScreen(g_client.window, &Point) != 0)
            SetCursorPos(Point.x, Point.y);
        while (ShowCursor(TRUE) <= 0)
        {
        }
    }

    // FUN_0041f720: take the device exclusive and foreground once. SetCooperativeLevel is called
    // without a null test on the device.
    bool SetExclusive()
    {
        if ((G_Mouse.Flags & kMouseExclusive) == 0)
        {
            UnacquireAndFlush();
            HRESULT const Result = G_Mouse.Device->SetCooperativeLevel(
                g_client.window, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
            G_Mouse.Flags = static_cast<unsigned char>((G_Mouse.Flags & ~kMouseExclusive) |
                                                       (Result >= 0 ? kMouseExclusive : 0));
        }
        return (G_Mouse.Flags & kMouseExclusive) != 0;
    }

    // FUN_0041f7b0: acquire while the game window is in front, retrying up to 100 times while
    // DirectInput answers DIERR_INPUTLOST; in absolute mode latch the axis base.
    bool Acquire()
    {
        if ((G_Mouse.Flags & kMouseAcquired) == 0 && GetForegroundWindow() == g_client.window)
        {
            HRESULT Result = 0;
            int Attempt = 0;
            do
            {
                Result = G_Mouse.Device->Acquire();
                if (Result != DIERR_INPUTLOST)
                    break;
                ++Attempt;
            } while (Attempt < 100);

            G_Mouse.Flags = static_cast<unsigned char>((G_Mouse.Flags & ~kMouseAcquired) |
                                                       (Result >= 0 ? kMouseAcquired : 0));
            if ((G_Mouse.Flags & kMouseAcquired) != 0 && (G_Mouse.Flags & kMouseAbsolute) != 0 &&
                G_Mouse.Device->GetDeviceState(0x14, &G_Mouse.State) >= 0)
            {
                G_Mouse.AbsoluteBase[0] = G_Mouse.State.lX - G_CursorX;
                G_Mouse.AbsoluteBase[1] = G_Mouse.State.lY - G_CursorY;
                G_Mouse.AbsoluteBase[2] = G_Mouse.State.lZ - G_CursorZ;
            }
        }
        return (G_Mouse.Flags & kMouseAcquired) != 0;
    }

    // FILD DAT_00610850 / FMUL double 0.00125 / FMUL float DAT_006107d0 -- kept unrounded.
    double AxisScale()
    {
        return static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * kAxisScale *
               static_cast<double>(G_MouseSpeed);
    }

    // One X / Y / Z step. Absolute mode: the position is (value - base) * scale, truncated.
    // Relative mode: value * scale plus the carried fraction; the whole part (ftol truncates)
    // moves the cursor and the rest is stored back as a float.
    void ApplyAxis(int Value, bool bAbsolute, int Base, double Scale, float* Remainder, int* Pos)
    {
        if (bAbsolute)
        {
            *Pos = static_cast<int>(static_cast<double>(Value - Base) * Scale);
            return;
        }
        double const Moved = Scale * static_cast<double>(Value) + static_cast<double>(*Remainder);
        int const Whole = static_cast<int>(Moved);
        *Remainder = static_cast<float>(Moved - static_cast<double>(Whole));
        *Pos += Whole;
    }

    // The press bookkeeping FUN_0041fa10 (0x0041fabe..0x0041fb1e) and FUN_0041fd90
    // (0x0041ff37..0x0041ffa2) inline: a press within the double-click time of the previous one
    // on this button, and within four pixels of the previous press on either axis, is a double
    // click; the button's time is then pulled back by one double-click time so a third press does
    // not pair with the second.
    bool DetectDoubleClick(unsigned ButtonIndex, unsigned Timestamp)
    {
        unsigned const Previous = G_Mouse.LastPressTime[ButtonIndex];
        unsigned const Elapsed = (Timestamp > Previous) ? Timestamp - Previous : 0;
        int const Dx = G_CursorX - G_LastClickX;
        int const Dy = G_CursorY - G_LastClickY;
        G_LastClickX = G_CursorX;
        G_LastClickY = G_CursorY;
        if (Elapsed < G_Mouse.DoubleClickTime && abs(Dx) <= kDoubleClickSlop &&
            abs(Dy) <= kDoubleClickSlop)
        {
            G_Mouse.LastPressTime[ButtonIndex] = Timestamp - G_Mouse.DoubleClickTime;
            return true;
        }
        G_Mouse.LastPressTime[ButtonIndex] = Timestamp;
        return false;
    }

    // A button changed state: keep DAT_00616850, detect the double click on a press, queue it.
    void OnButtonChange(unsigned ButtonIndex, bool bDown, unsigned Timestamp)
    {
        unsigned const Bit = 1u << ButtonIndex;
        if (bDown)
            G_MouseButtons |= Bit;
        else
            G_MouseButtons &= ~Bit;

        bool bDouble = false;
        if (bDown)
            bDouble = DetectDoubleClick(ButtonIndex, Timestamp);
        QueueButtonEvent(ButtonIndex, bDown, bDouble);
    }

    // FUN_0041fa10: drain the DirectInput buffer. Axis records are skipped in buffered mode
    // (FUN_0041fd90 reads the axes from the immediate state instead); buttons always count, with
    // the record's own timestamp.
    bool ReadBuffered()
    {
        DWORD Count = G_Mouse.BufferSize;
        HRESULT const Result = G_Mouse.Device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),
                                                             G_Mouse.Buffer, &Count, 0);
        bool const bOk = Result >= 0;
        if (!bOk || Count == 0)
            return bOk;

        DWORD Index = 0;
        for (; Index < Count; ++Index)
        {
            DIDEVICEOBJECTDATA const* const Entry = &G_Mouse.Buffer[Index];
            bool const bAbsolute = (G_Mouse.Flags & kMouseAbsolute) != 0;
            switch (Entry->dwOfs)
            {
            case DIMOFS_X:
                if ((G_Mouse.Flags & kMouseBuffered) == 0)
                {
                    ApplyAxis(static_cast<int>(Entry->dwData), bAbsolute, G_Mouse.AbsoluteBase[0],
                              AxisScale(), &G_Mouse.Remainder[0], &G_CursorX);
                }
                break;
            case DIMOFS_Y:
                if ((G_Mouse.Flags & kMouseBuffered) == 0)
                {
                    ApplyAxis(static_cast<int>(Entry->dwData), bAbsolute, G_Mouse.AbsoluteBase[1],
                              AxisScale(), &G_Mouse.Remainder[1], &G_CursorY);
                }
                break;
            case DIMOFS_Z:
                if ((G_Mouse.Flags & kMouseBuffered) == 0)
                {
                    ApplyAxis(static_cast<int>(Entry->dwData), bAbsolute, G_Mouse.AbsoluteBase[2],
                              static_cast<double>(kWheelScale), &G_Mouse.Remainder[2],
                              &G_CursorZ);
                }
                break;
            case DIMOFS_BUTTON0:
            case DIMOFS_BUTTON1:
            case DIMOFS_BUTTON2:
            case DIMOFS_BUTTON3:
            case DIMOFS_BUTTON4:
            case DIMOFS_BUTTON5:
            case DIMOFS_BUTTON6:
            case DIMOFS_BUTTON7:
                OnButtonChange(Entry->dwOfs - DIMOFS_BUTTON0, ((Entry->dwData >> 7) & 1) != 0,
                               Entry->dwTimeStamp);
                break;
            default:
                break;
            }
        }
        return bOk;
    }

    // FUN_0041fd90: the immediate state -- all three axes, then every button whose bit in
    // DAT_00616850 disagrees with the device, stamped with the frame time FUN_0042d670.
    bool ReadImmediate()
    {
        HRESULT const Result = G_Mouse.Device->GetDeviceState(0x14, &G_Mouse.State);
        bool const bOk = Result >= 0;
        if (!bOk)
            return false;

        ApplyAxis(G_Mouse.State.lX, (G_Mouse.Flags & kMouseAbsolute) != 0,
                  G_Mouse.AbsoluteBase[0], AxisScale(), &G_Mouse.Remainder[0], &G_CursorX);
        ApplyAxis(G_Mouse.State.lY, (G_Mouse.Flags & kMouseAbsolute) != 0,
                  G_Mouse.AbsoluteBase[1], AxisScale(), &G_Mouse.Remainder[1], &G_CursorY);
        ApplyAxis(G_Mouse.State.lZ, (G_Mouse.Flags & kMouseAbsolute) != 0,
                  G_Mouse.AbsoluteBase[2], static_cast<double>(kWheelScale),
                  &G_Mouse.Remainder[2], &G_CursorZ);

        unsigned const Timestamp = static_cast<unsigned>(GetClientFrameTimestampMs());
        unsigned Index = 0;
        for (; Index < kMouseButtonCount; ++Index)
        {
            bool const bDown = (G_Mouse.State.rgbButtons[Index] >> 7) != 0;
            bool const bHeld = (G_MouseButtons & (1u << Index)) != 0;
            if (bDown == bHeld)
                continue;
            OnButtonChange(Index, bDown, Timestamp);
        }
        return bOk;
    }

    // FUN_0041f970 (thiscall, RET 8): keep a moved cursor on the surface. Without flags bit 0x10
    // (a windowed session) leaving the surface asks the caller to release the mouse instead.
    bool ClampOrRelease(int Dx, int Dy)
    {
        bool const bMayRelease = (G_Mouse.Flags & kMouseClampToSurface) == 0;
        bool bRelease = false;
        if (Dx != 0)
        {
            if (G_CursorX < 0)
            {
                if (bMayRelease)
                    bRelease = true;
                else
                    G_CursorX = 0;
            }
            else if (G_CursorX >= static_cast<int>(G_RenderSurfaceWidth))
            {
                if (bMayRelease)
                    bRelease = true;
                else
                    G_CursorX = static_cast<int>(G_RenderSurfaceWidth) - 1;
            }
        }
        if (Dy != 0)
        {
            if (G_CursorY < 0)
            {
                if (bMayRelease)
                    bRelease = true;
                else
                    G_CursorY = 0;
            }
            else if (G_CursorY >= static_cast<int>(G_RenderSurfaceHeight))
            {
                if (bMayRelease)
                    bRelease = true;
                else
                    G_CursorY = static_cast<int>(G_RenderSurfaceHeight) - 1;
            }
        }
        return bRelease;
    }

    // FUN_0041dae0 (thiscall on the cursor, RET 8: ShapeDB, time). An animated cursor picks its
    // frame from the elapsed time and re-formats its shape name; the shape is looked up whenever
    // it is not resolved; a spinning cursor's angle follows the time.
    void CursorAnimate(FMouseCursor* Cursor, FShapeDB* ShapeDB, float Time)
    {
        if (Cursor->AnimName.Length != 0 &&
            (Cursor->FrameCount > 1 || Cursor->ShapeName.Length == 0))
        {
            // 0x0041dafe: FCOMP 0.0 / TEST AH,0x44 / JNP -- skipped only when Fps equals 0.
            if (!(Cursor->Fps == 0.0f))
            {
                unsigned const Tick =
                    static_cast<unsigned>(static_cast<int>(static_cast<double>(Time) *
                                                           static_cast<double>(Cursor->Fps)));
                Cursor->Frame = Tick % Cursor->FrameCount;
            }
            FormatShapeName(&Cursor->ShapeName, Cursor->AnimName.Data, Cursor->Frame);
            Cursor->Shape = nullptr;
        }

        if (Cursor->Shape == nullptr)
        {
            // The ShapeDB.h:0xf2 find, inlined (key FUN_004205c0, cache FUN_00420690, lookup
            // FUN_004206c0, warning with ShapeDB.h's full path).
            FTexShape* const Shape = ShapeDB->Find(Cursor->ShapeName.Data, kShapeDbHeaderFull);
            Cursor->Shape = Shape;
            if (Shape == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: missing cursor shape (%s)\n", kMouseCpp,
                      0x430, Cursor->ShapeName.Data);
            }
        }

        // 0x0041dbd7: the same equality test on Spin.
        if (!(Cursor->Spin == 0.0f))
            Cursor->Angle = Time * Cursor->Spin;
    }

    // FUN_0041d9c0 (thiscall on the cursor, RET 0xc: out[8], x, y): the four corners of the
    // cursor quad, rotated by Angle about the hotspot and scaled by Scale and the surface width.
    bool CursorComputeQuad(FMouseCursor const* Cursor, float* Out, int X, int Y)
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: the cursor keeps its size relative to the height-scaled interface.
        float const SurfaceScale = static_cast<float>(
            WidescreenEquivalentSurfaceWidth() * static_cast<double>(kCursorQuadScale));
#else
        float const SurfaceScale =
            static_cast<float>(static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) *
                               static_cast<double>(kCursorQuadScale));
#endif
        if (Cursor->Shape == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: missing shape (%s)\n", kMouseCpp, 0x401,
                  Cursor->Name.Data);
            return Cursor->Shape != nullptr;
        }

        double const Width = static_cast<double>(Cursor->Shape->PixelW);
        double const Height = static_cast<double>(Cursor->Shape->PixelH);
        // S stays on the x87 stack; C is stored to a float first.
        double const Sine = sin(static_cast<double>(Cursor->Angle)) *
                            static_cast<double>(Cursor->Scale) *
                            static_cast<double>(SurfaceScale);
        float const Cosine = static_cast<float>(cos(static_cast<double>(Cursor->Angle)) *
                                                static_cast<double>(Cursor->Scale) *
                                                static_cast<double>(SurfaceScale));

        float const Ax = static_cast<float>(static_cast<double>(X) -
                                            static_cast<double>(Cosine) * Cursor->HotspotX +
                                            Sine * Cursor->HotspotY);
        float const Ay = static_cast<float>(static_cast<double>(Y) - Sine * Cursor->HotspotX -
                                            static_cast<double>(Cosine) * Cursor->HotspotY);

        double const RightX = static_cast<double>(Ax) + static_cast<double>(Cosine) * Width;
        float const SineWidth = static_cast<float>(Sine * Width);   // [param_3]
        float const SineHeight = static_cast<float>(Sine * Height); // [param_1]
        double const CosineHeight = static_cast<double>(Cosine) * Height;

        Out[0] = Ax;
        Out[1] = Ay;
        Out[2] = static_cast<float>(RightX);
        Out[3] = static_cast<float>(Sine * Width + static_cast<double>(Ay));
        Out[4] = static_cast<float>(RightX - static_cast<double>(SineHeight));
        Out[5] = static_cast<float>(static_cast<double>(SineWidth) + CosineHeight +
                                    static_cast<double>(Ay));
        Out[6] = static_cast<float>(static_cast<double>(Ax) - static_cast<double>(SineHeight));
        Out[7] = static_cast<float>(CosineHeight + static_cast<double>(Ay));
        return Cursor->Shape != nullptr;
    }

    // FUN_00405840's object: {Pipeline, State, Saved}. Built in place (GetRenderState, slot
    // 0xf4) and restored by FUN_00405840 (SetRenderState, slot 0xf0).
    struct FRenderStateSave
    {
        IRenderPipeline* Pipeline;
        int State;
        int Saved;
    };

    // Build the saver from the current DALib::Pipeline, then force Value through the saver's own
    // pipeline and state (the MOV EAX,[ESP+..] / PUSH [ESP+..] pair after every GetRenderState).
    void RenderStateSaveAndSet(FRenderStateSave* Save, int State, int Value)
    {
        Save->Pipeline = DALib::Pipeline;
        Save->State = State;
        Save->Pipeline->GetRenderState((D3DRENDERSTATETYPE)State, &Save->Saved);
        Save->Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save->State, Value);
    }

    void RenderStateRestore(FRenderStateSave* Save)
    {
        Save->Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save->State, Save->Saved);
    }

    // FUN_0041f150 0x0041f30a..0x0041f6e4: animate and draw the cursor in use.
    void DrawCursor()
    {
        if (G_MouseCursorDrawFlag == 0)
            return;

        if (G_CurrentCursor != nullptr)
        {
            float const Time = static_cast<float>(GetClientElapsedTime()); // FUN_0042d660
            CursorAnimate(G_CurrentCursor, &G_MouseShapeDB, Time);
        }

        if (GetGameOption(6) == 0 || GetGameOption(0xe) == 0)
            return;
        if (G_CurrentCursor == nullptr)
            return;
        FTexShape* const Shape = G_CurrentCursor->Shape;
        if (Shape == nullptr)
            return;

        Win3DSetOrtho2D(); // FUN_004225e0

        FRenderStateSave ZEnable;
        RenderStateSaveAndSet(&ZEnable, kRsZEnable, 0);
        FRenderStateSave CullMode;
        RenderStateSaveAndSet(&CullMode, kRsCullMode, 1);
        FRenderStateSave AlphaTest;
        RenderStateSaveAndSet(&AlphaTest, kRsAlphaTestEnable, 0);

        Win3DBindTexture(TexShapeGetGpuHandle(Shape, 0)); // FUN_00425640 / FUN_004220a0
        unsigned char FilterToken = 0;                    // the ECX both filter calls get
        Win3DSetTextureFilterLinear(&FilterToken);         // FUN_004223d0
        Win3DSetTextureFilterPoint();                      // FUN_00422410
        Win3DColor4ubv(G_CurrentCursor->Color);            // FUN_00421ba0

        // 0x0041f47c..0x0041f4c2: the blend switch on +0x50 (table 0x0041f700).
        int SourceBlend = 2;
        int DestBlend = 1;
        switch (G_CurrentCursor->Blend)
        {
        case 1:
            SourceBlend = 2;
            DestBlend = 2;
            break;
        case 2:
            SourceBlend = 2;
            DestBlend = 4;
            break;
        case 3:
            SourceBlend = 5;
            DestBlend = 2;
            break;
        case 4:
            SourceBlend = 5;
            DestBlend = 6;
            break;
        case 5:
            SourceBlend = 5;
            DestBlend = 4;
            break;
        default:
            break;
        }

        FRenderStateSave SrcBlend;
        RenderStateSaveAndSet(&SrcBlend, kRsSrcBlend, SourceBlend);
        FRenderStateSave DstBlend;
        RenderStateSaveAndSet(&DstBlend, kRsDestBlend, DestBlend);
        FRenderStateSave AlphaBlend;
        RenderStateSaveAndSet(&AlphaBlend, kRsAlphaBlendEnable,
                              G_CurrentCursor->Blend != 0 ? 1 : 0);

        float Quad[8];
        if (CursorComputeQuad(G_CurrentCursor, Quad, G_CursorX, G_CursorY))
        {
            Win3DBegin(kWin3DQuads);
            Win3DTexCoord2f(Shape->U0, Shape->V0);
            Win3DVertex3f(Quad[0], Quad[1], 0.0f);
            Win3DTexCoord2f(Shape->U1, Shape->V0);
            Win3DVertex3f(Quad[2], Quad[3], 0.0f);
            Win3DTexCoord2f(Shape->U1, Shape->V1);
            Win3DVertex3f(Quad[4], Quad[5], 0.0f);
            Win3DTexCoord2f(Shape->U0, Shape->V1);
            Win3DVertex3f(Quad[6], Quad[7], 0.0f);
            Win3DEnd();
        }

        RenderStateRestore(&AlphaBlend);
        RenderStateRestore(&DstBlend);
        RenderStateRestore(&SrcBlend);
        RenderStateRestore(&AlphaTest);
        RenderStateRestore(&CullMode);
        RenderStateRestore(&ZEnable);
    }

    // FUN_0041df30(Path, BaseDir): every [Cursor] section with a nickname becomes a cursor record.
    void LoadCursors(char const* Path, IFileSystem* BaseDir)
    {
        INI_Reader Reader;

        char FullPath[0x104];
        if (BaseDir != nullptr)
        {
            char BaseDirPath[0x104];
            BaseDir->GetFileSysPath(BaseDirPath, 0x104);
            _makepath(FullPath, nullptr, BaseDirPath, Path, nullptr);
        }
        else
        {
            strcpy(FullPath, Path);
        }

        if (!Reader.open(FullPath, false) || !Reader.read_header())
            return;

        do
        {
            if (!Reader.is_header("Cursor") || !Reader.read_value())
                continue;

            // 0x0041e027: skip to the nickname; a section without one is dropped.
            bool bHasNickname = false;
            do
            {
                if (Reader.is_value("nickname"))
                {
                    bHasNickname = true;
                    break;
                }
            } while (Reader.read_value());
            if (!bHasNickname)
                continue;

            char const* const Nickname = Reader.get_value_string(0);
            FMouseCursor* const Cursor = static_cast<FMouseCursor*>(operator new(0x90));
            CursorConstruct(Cursor);
            Cursor->Name = Nickname; // FUN_00420750

            // 0x0041e099: past 0x14 cursors the record is still filled but never stored.
            if (G_CursorCount < kMaxCursors)
            {
                G_Cursors[G_CursorCount] = Cursor;
                ++G_CursorCount;
            }

            if (!Reader.read_value())
                continue;
            do
            {
                if (Reader.is_value("shape"))
                {
                    char const* const Shape = Reader.get_value_string(0);
                    if (Shape == nullptr)
                    {
                        Cursor->ShapeName.Length = 0;
                        Cursor->ShapeName.Data[0] = '\0';
                    }
                    else
                    {
                        Cursor->ShapeName.Length =
                            CopyBoundedString(Cursor->ShapeName.Data, 0x18, Shape);
                    }
                }
                else if (Reader.is_value("hotspot"))
                {
                    float const HotspotX = Reader.get_value_float(0);
                    Cursor->HotspotY = Reader.get_value_float(1);
                    Cursor->HotspotX = HotspotX;
                }
                else if (Reader.is_value("blend"))
                {
                    Cursor->Blend = static_cast<int>(Reader.get_value_float(0));
                }
                else if (Reader.is_value("color"))
                {
                    int const Red = static_cast<int>(Reader.get_value_float(0));
                    int const Green = static_cast<int>(Reader.get_value_float(1));
                    int const Blue = static_cast<int>(Reader.get_value_float(2));
                    int Alpha = static_cast<int>(Reader.get_value_float(3));
                    if (Alpha == 0)
                        Alpha = 0xff;
                    Cursor->Color[0] = static_cast<unsigned char>(ClampColorByte(Red));
                    Cursor->Color[1] = static_cast<unsigned char>(ClampColorByte(Green));
                    Cursor->Color[2] = static_cast<unsigned char>(ClampColorByte(Blue));
                    Cursor->Color[3] = static_cast<unsigned char>(ClampColorByte(Alpha));
                }
                else if (Reader.is_value("anim"))
                {
                    // A TString<0x80> on the stack, then the frame count and the rate.
                    TString<0x80> Format;
                    Format.Length = 0;
                    Format.Data[0] = '\0';
                    char const* const Text = Reader.get_value_string(0);
                    if (Text != nullptr)
                        Format.Length = CopyBoundedString(Format.Data, 0x80, Text);
                    double const Frames = static_cast<double>(Reader.get_value_float(1));
                    float const Fps = Reader.get_value_float(2);
                    Cursor->AnimName = Format.Data;
                    Cursor->FrameCount = static_cast<unsigned>(static_cast<int>(Frames));
                    Cursor->Fps = Fps;
                }
                else if (Reader.is_value("angle"))
                {
                    Cursor->Angle = Reader.get_value_float(0);
                }
                else if (Reader.is_value("spin"))
                {
                    Cursor->Spin = Reader.get_value_float(0);
                }
                else if (Reader.is_value("scale"))
                {
                    Cursor->Scale = Reader.get_value_float(0);
                }
            } while (Reader.read_value());
        } while (Reader.read_header());
    }
}

void ClientInputOnMouseMove()
{
    // FUN_0041e890: gated on DAT_0061685c; the device ensure and FUN_004201a0(1) inlined.
    if (!G_bInputActive)
        return;
    EnsureDevice();
    SetActive(true);
}

void ClientInputOnFocusGained()
{
    // FUN_0041eac0.
    EnsureDevice();
    SetActive(true);
    G_bInputActive = true;
}

void ClientInputOnFocusLost()
{
#ifdef FREELANCER_WALK_MODE
    BaseWalkFocusLost();
#endif
    // FUN_0041ecf0.
    EnsureDevice();
    SetActive(false);
    G_bInputActive = false;
}

void MouseSetEnabled(bool bEnable, int Width, int Height, bool bFullscreen)
{
    // FUN_00420350 (cdecl). Width and Height are pushed by both callers and never read.
    (void)Width;
    (void)Height;

    if (!bEnable)
    {
        // 0x00420360..0x004204d4: the device ensure, FUN_004201a0(0) inlined, DAT_0061685c = 0.
        EnsureDevice();
        SetActive(false);
        G_bInputActive = false;
        return;
    }

    // 0x004204d5: a windowed session only takes the mouse while the window has the focus.
    if (!bFullscreen && GetFocus() != g_client.window)
        return;

    EnsureDevice();
    G_Mouse.Flags = static_cast<unsigned char>((G_Mouse.Flags & ~kMouseClampToSurface) |
                                               (bFullscreen ? kMouseClampToSurface : 0));
    // 0x00420550..0x0042059e: retail repeats the object / device ensure; both are no-ops now.
    EnsureDevice();
    SetActive(true);
    G_bInputActive = true;
}

void MouseDispatchEvents()
{
    // FUN_0041ef20.
    EnsureDevice();
    ++G_DispatchDepth;
    while (!G_Mouse.Events.empty())
    {
        FMouseEvent Event = G_Mouse.Events.front();
        G_Mouse.Events.pop_front();

#ifdef FREELANCER_WALK_MODE
        if (BaseWalkConsumeMouseEvent(Event))
            continue;
#endif

        if (Event.bMove != 0)
        {
            HudPostCommand(5, 0); // FUN_005763b0(5, 0)
            continue;
        }
        if (Event.Record.Key == 0)
            continue;
        // 0x0041f106: an unclaimed left press goes to FUN_0045a270(WM_LBUTTONDOWN, 0, 0).
        if (!KeyMapRouteMouseEvent(&Event.Record) && Event.ButtonIndex == 0 &&
            Event.Record.Byte18 != 0)
        {
            NnMainButtonOnUnclaimedMouse(0x201, 0, 0);
        }
    }
    --G_DispatchDepth;
}

void MouseFrameUpdate()
{
    // FUN_0041f150 -- nothing at all until the cursors are loaded (DAT_00616855).
    if (!G_bCursorsReady)
        return;

    EnsureDevice();
    if (G_Mouse.Device != nullptr && (G_Mouse.Flags & kMouseActive) != 0 && SetExclusive() &&
        Acquire())
    {
        if ((G_Mouse.Flags & kMouseNeedsPoll) != 0)
            G_Mouse.Device->Poll();

        int const StartX = G_CursorX;
        int const StartY = G_CursorY;
        int const StartZ = G_CursorZ;

        // 0x0041f218..0x0041f26c: buffered mode reads the buffer (its result lands in bit 2 for
        // a moment) and then the immediate state; otherwise the buffer when there is one, else
        // the immediate state. The last read decides bit 2.
        bool bRead = false;
        if ((G_Mouse.Flags & kMouseBuffered) != 0)
        {
            bRead = ReadBuffered();
            G_Mouse.Flags = static_cast<unsigned char>((G_Mouse.Flags & ~kMouseAcquired) |
                                                       (bRead ? kMouseAcquired : 0));
            bRead = ReadImmediate();
        }
        else if (G_Mouse.BufferSize != 0)
        {
            bRead = ReadBuffered();
        }
        else
        {
            bRead = ReadImmediate();
        }
        G_Mouse.Flags = static_cast<unsigned char>((G_Mouse.Flags & ~kMouseAcquired) |
                                                   (bRead ? kMouseAcquired : 0));

        int const Dx = G_CursorX - StartX;
        int const Dy = G_CursorY - StartY;
        int Wheel = G_CursorZ - StartZ;
#ifdef FREELANCER_WALK_MODE
        bool const bWalkMouse = BaseWalkMouseFrame(bRead, Dx, Dy, StartX, StartY);
        if (bWalkMouse || (Dx == 0 && Dy == 0))
#else
        if (Dx == 0 && Dy == 0)
#endif
        {
            G_Mouse.bMoved = false;
        }
        else
        {
            G_Mouse.bMoved = true;
            if (ClampOrRelease(Dx, Dy))
                SetActive(false);
            QueueMoveEvent();
        }

        // 0x0041f2d5: the wheel delta is published and queued one notch at a time.
        G_MouseWheel = Wheel;
        if (Wheel < 0)
            Wheel = -Wheel;
        for (; Wheel > 0; --Wheel)
            QueueButtonEvent(kMouseWheelButton, G_MouseWheel < 0, false);
    }

    DrawCursor();
}

void MouseInitCursors()
{
    // FUN_0041e5c0.
    G_MouseShapeDB.Load("mouse.ini", DALib::Data); // FUN_00425b30
    LoadCursors("mouse.ini", DALib::Data);         // FUN_0041df30
    G_bCursorsReady = true;
    MouseSetCursor("DEFAULT", false);
}

void MouseShutdown()
{
    // FUN_0041e610. The device ensure runs first, so a session that never created the device
    // creates it here only to release it.
    EnsureDevice();
    if (G_Mouse.Device != nullptr)
    {
        G_Mouse.Device->Unacquire();
        if (G_Mouse.Device != nullptr)
        {
            G_Mouse.Device->Release();
            G_Mouse.Device = nullptr;
        }
    }
    if (G_Mouse.DirectInput != nullptr)
    {
        G_Mouse.DirectInput->Release();
        G_Mouse.DirectInput = nullptr;
    }

    G_CurrentCursor = nullptr;
    unsigned Index = 0;
    for (; Index < G_CursorCount; ++Index)
    {
        operator delete(G_Cursors[Index]);
        G_Cursors[Index] = nullptr;
    }
    G_CursorCount = 0;
    G_MouseShapeDB.Clear(); // FUN_00425a20
    G_bCursorsReady = false;
}

void MouseSetCursor(char const* Name, bool bSetDefault)
{
    // FUN_0041dde0.
    if (bSetDefault)
        G_DefaultCursorName = Name;

    if (!G_bCursorsReady)
        return;
    // 0x0041de4a: the cursor already in use is left alone. (A null Name reaches DACOM stricmp
    // here only while a cursor is set; no caller in the PE passes one.)
    if (G_CurrentCursor != nullptr && _stricmp(G_CurrentCursor->Name.Data, Name) == 0)
        return;

    FMouseCursor* Found = nullptr;
    if (Name != nullptr)
    {
        char const* Wanted = Name;
        if (_stricmp(Name, "DEFAULT") == 0)
            Wanted = G_DefaultCursorName.Data;
        unsigned Index = 0;
        for (; Index < G_CursorCount; ++Index)
        {
            if (_stricmp(G_Cursors[Index]->Name.Data, Wanted) == 0)
            {
                Found = G_Cursors[Index];
                break;
            }
        }
    }

    if (Found == nullptr && Name != nullptr && Name[0] != '\0' && _stricmp(Name, "DEFAULT") != 0)
        FDUMP(0x100002, "%s(%d) : *** WARNING: missing cursor (%s)\n", kMouseCpp, 0x4ad, Name);

    if (Found == G_CurrentCursor)
        return;
    G_CurrentCursor = Found;
    // 0x0041df0f: restart the animation (0 % FrameCount).
    if (Found != nullptr && Found->FrameCount != 0)
        Found->Frame = 0 % Found->FrameCount;
}

bool MouseIsArrowCursor()
{
    // FUN_0041ddb0.
    return G_CurrentCursor != nullptr && _stricmp(G_CurrentCursor->Name.Data, "Arrow") == 0;
}

bool ClientInputIsActive()
{
    return G_bInputActive;
}

int GetClientMouseWheel()
{
    return G_MouseWheel;
}

unsigned GetClientMouseButtons()
{
    return G_MouseButtons;
}

// LAB_0041d970 -- apply of the perf option "mouse_speed": clamp to [DAT_005c7474 = 0.0,
// DAT_005c75dc = 1.0] (0041d978 / 0041d993), then * DAT_005c94fc (1.75) + DAT_005c94f8 (0.25)
// into DAT_006107d0. The x87 FMUL/FADD round once, on the FSTP; the double arithmetic here is
// exact up to that same single rounding. A NaN fails both compares and passes through, as in
// retail.
void ApplyMouseSpeedOption(float Value)
{
    float Clamped = Value;
    if (Value < 0.0f)
        Clamped = 0.0f;
    else if (Value > 1.0f)
        Clamped = 1.0f;
    G_MouseSpeed = static_cast<float>(static_cast<double>(Clamped) * 1.75 + 0.25);
}

float GetClientMouseSpeed()
{
    return G_MouseSpeed;
}

void GetClientSoftwareCursor(int* OutX, int* OutY, int* OutZ)
{
    if (OutX != nullptr)
        *OutX = G_CursorX;
    if (OutY != nullptr)
        *OutY = G_CursorY;
    if (OutZ != nullptr)
        *OutZ = G_CursorZ;
}

void SetClientSoftwareCursor(int X, int Y)
{
    G_CursorX = X;
    G_CursorY = Y;
}

unsigned GetClientDoubleClickTime()
{
    // The object's +0x40, latched by the device creation.
    return G_Mouse.DoubleClickTime;
}

unsigned char GetClientMouseCursorDrawFlag()
{
    // FUN_0041dd90.
    return G_MouseCursorDrawFlag;
}

void SetClientMouseCursorDrawFlag(unsigned char Value)
{
    // FUN_0041dda0.
    G_MouseCursorDrawFlag = Value;
}
