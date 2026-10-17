// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail E:\FL\Scratch\Source\Client\cmn\mouse.cpp -- the DirectInput mouse, its event queue and
// the software cursor.
//
// Three pieces of retail state:
//
//   * The mouse object at DAT_006167b8 (0x74 bytes, constructed lazily behind DAT_0061682c bit 0
//     by FUN_0041e740, destroyed through the atexit thunk LAB_0041e880). It owns the DirectInput
//     interface (+0x0c, DAT_006167c4), the SysMouse device (+0x10, DAT_006167c8), a flags byte
//     (+0x14, DAT_006167cc), the buffered-data array (+0x24 count / +0x28 data) and a std::list of
//     0x24-byte input events (+0x04 head / +0x08 size) that the per-frame poll fills and the
//     message pump drains. The device is created once, behind DAT_00616854.
//   * The software cursor position DAT_00616840 / DAT_00616844 / DAT_00616848 (x, y, wheel
//     accumulator) and the button bits DAT_00616850. While the object is active (flags bit 0)
//     the position is driven by DirectInput deltas scaled by the surface width, not by
//     GetCursorPos; GetCursorPos is only read when the object (re)activates.
//   * The named cursors: up to 0x14 records from mouse.ini [Cursor] (DAT_00616740 count,
//     DAT_00616744 array), the one in use (DAT_00616858), the "DEFAULT" alias (TString<32> at
//     DAT_00616794) and the cursor shapes in the ShapeDB DAT_00616728 (G_MouseShapeDB). The
//     cursor is drawn at the very end of FUN_0059e300 by FUN_0041f150.
//
// Flags byte (+0x14):
//   0x01 active (FUN_004201a0(1)) -- the device is polled and owns the cursor
//   0x02 absolute axis mode (base captured at acquire); never set in the PE, branches kept
//   0x04 acquired and the last read succeeded
//   0x08 cooperative level set to DISCL_EXCLUSIVE | DISCL_FOREGROUND
//   0x10 clamp the cursor to the surface instead of releasing the mouse when it leaves it
//   0x20 the device needs Poll
//   0x40 buffered mode (GetDeviceData into the +0x28 array)

#include "hud_command_dispatch.h" // FHudCmdRecord (the event layout's first 0x1c bytes)

#include <stddef.h>

// One queued input event (a 0x2c-byte list node minus the two links). The first 0x1c bytes are
// exactly what FUN_00577680 copies into the key-map record DAT_0067c278.
struct FMouseEvent
{
    FHudCmdRecord Record;       // +0x00  Key +0x04 (-1 - button, -9 wheel), Byte18 down,
                                //        Byte19 repeat (always 0), Byte1A double click
    unsigned char bMove;        // +0x1c  a cursor-move event (FUN_0041f850)
    unsigned char Pad1d[3];
    unsigned ButtonIndex;       // +0x20  0..7 buttons, 8 wheel
};

static_assert(sizeof(FHudCmdRecord) == 0x1c, "FUN_00577680 copies seven dwords");
static_assert(offsetof(FMouseEvent, bMove) == 0x1c, "event +0x1c move flag");
static_assert(offsetof(FMouseEvent, ButtonIndex) == 0x20, "event +0x20 button index");
static_assert(sizeof(FMouseEvent) == 0x24, "FUN_0041ef20 copies nine dwords");

// FUN_0041e890 -- WM_MOUSEMOVE: with input focus (DAT_0061685c), make sure the device exists and
// re-activate the object (FUN_004201a0(1) inlined): the cursor jumps to the OS pointer.
void ClientInputOnMouseMove();
// FUN_0041eac0 -- WM_SETFOCUS: the same, unconditionally, then DAT_0061685c = 1.
void ClientInputOnFocusGained();
// FUN_0041ecf0 -- WM_KILLFOCUS: deactivate (FUN_004201a0(0) inlined) and DAT_0061685c = 0.
void ClientInputOnFocusLost();

// FUN_00420350(bEnable, Width, Height, bFullscreen) -- called by FUN_004249c0 (device buffers
// created) and FUN_00424c60 (WM_ACTIVATEAPP). Enable needs bFullscreen or the window to hold the
// focus; bFullscreen becomes flags bit 0x10 (clamp instead of release). Width and Height are
// not read.
void MouseSetEnabled(bool bEnable, int Width, int Height, bool bFullscreen);

// FUN_0041ef20 -- drain the event queue: move events post key-map command 5 (FUN_005763b0),
// button / wheel events go through FUN_00577680, and an unclaimed left press reaches
// FUN_0045a270(0x201, 0, 0). Called first in every message-pump loop.
void MouseDispatchEvents();

// FUN_0041f150 -- the per-frame mouse update and cursor draw (gated by DAT_00616855): poll the
// device, move the cursor, queue move / button / wheel events, then draw the current cursor.
// The last call of FUN_0059e300 (Win3DDrawAll) and of the NN_Error modal loop.
void MouseFrameUpdate();

// FUN_0041e5c0 -- load the mouse.ini shapes (FUN_00425b30) and cursors (FUN_0041df30), set
// DAT_00616855 and select "DEFAULT". Called by FUN_0059d5a0.
void MouseInitCursors();
// FUN_0041e610 -- release the device and the DirectInput interface, free the cursors, clear the
// shape DB and DAT_00616855. Called by FUN_0059d6d0.
void MouseShutdown();

// FUN_0041dde0(Name, bSetDefault) -- select the cursor called Name ("DEFAULT" is the name stored
// by the last bSetDefault call). With bSetDefault the name (or its absence) is stored first.
// Returns retail's EAX, which no caller reads.
void MouseSetCursor(char const* Name, bool bSetDefault);
// FUN_0041ddb0 -- whether the cursor in use is "Arrow".
bool MouseIsArrowCursor();

// DAT_0061685c.
bool ClientInputIsActive();
// DAT_00616840 / DAT_00616844 / DAT_00616848.
void GetClientSoftwareCursor(int* OutX, int* OutY, int* OutZ);
void SetClientSoftwareCursor(int X, int Y);
// DAT_0061684c -- the wheel delta of the last FUN_0041f150; read by FUN_0059d140 (event 4) and
// 0x004d8354.
int GetClientMouseWheel();
// DAT_00616850 -- button bits (bit n = button n held), kept by FUN_0041fa10 / FUN_0041fd90;
// read by 0x004957f8, 0x004b9331, 0x004bde7e, FUN_004c7cb0, 0x0051a84c and 0x00578545.
unsigned GetClientMouseButtons();
// LAB_0041d970 -- perf option "mouse_speed": DAT_006107d0 = clamp(Value, 0, 1) * 1.75 + 0.25.
void ApplyMouseSpeedOption(float Value);
// DAT_006107d0 -- the axis multiplier FUN_0041fa10 / FUN_0041fd90 apply.
float GetClientMouseSpeed();
// Mouse object +0x40 (DAT_006167f8) -- GetDoubleClickTime() latched when the device is created.
unsigned GetClientDoubleClickTime();

// DAT_006107dc / FUN_0041dd90 / FUN_0041dda0 -- the software-cursor draw latch.
unsigned char GetClientMouseCursorDrawFlag();
void SetClientMouseCursorDrawFlag(unsigned char Value);
