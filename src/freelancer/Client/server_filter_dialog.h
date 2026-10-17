// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ServerFilterDialog (RTTI .?AVServerFilterDialog@@ : WinControl, COL 0x005efe30) -- the
// modal box PageServerSelect's "SetFilter" button opens over the server list (FUN_005637b0 from
// PageServerSelect slot 23, 0x00571517..0x00571606). It edits the page's ServerFilter in place
// and keeps a copy to put back on cancel.
//
// WINFACTORY<ServerFilterDialog> DAT_0067a444 "ServerFilterDialog": static init LAB_005626e0
// (FUN_00563890, atexit LAB_00562700 -> FUN_00579380), Create FUN_00563900 (operator_new(0x39c),
// WinControl ctor FUN_0059f860, the five rows through `eh vector constructor iterator`
// (FUN_005b7fba, ctor LAB_00562a40 zeroing the three pointers, dtor LAB_00562a50 zeroing them
// again), ServerFilter's ctor FUN_005628e0 inlined at +0x378, the vptrs, +0x374 / +0x32c /
// +0x330 / +0x334 = 0, +0x10 = the factory's name, slot 9 with the payload; on failure the
// scalar deleting dtor).
//
// vftable 0x005e20ec (45 slots) -- WinControl's with these overrides:
//   0 FUN_00562f40 (scalar deleting dtor around FUN_00562f60), 9 FUN_005631c0 Create,
//   19 FUN_005635a0 MouseEvent, 20 FUN_00563530 KeyEvent, 23 FUN_00563630 Slot23,
//   42 FUN_005636d0 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005e20e0 (WinControl's LAB_0053aaa0 / FUN_0053e4a0).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c "FilterOk" (RichTextWireButtonControl), +0x330 "FilterCancel" (ButtonControl),
//   +0x334 "FilterTitle" (RichTextWin)
//   +0x338 five FServerFilterDialogRow (0xc each): "FilterCheck%d" / "FilterLabel%d" /
//          "FilterData%d" (only row 0 has the edit box)
//   +0x374 ServerFilter* -- the caller's filter, edited live (ReadControls)
//   +0x378 ServerFilter  -- the copy Create takes, assigned back on every cancel path
//   size 0x39c

#include "server_filter.h"
#include "wincontrol_base.h"

#include <stddef.h>

class RichTextEditWin;
class RichTextWin;

// DAT_00614828 -- one 0x20-byte description per row, five rows up to DAT_006148c8. Row 0 is
// .data as linked except for the dwords LAB_00562750 writes; rows 1..4 are LAB_00562750's
// stores. The toggle is placed at {X, Y, Z} (read in the order +0x08, +0x00, +0x04), the label
// at {LabelX, Y + 0.015, Z}, the edit box at {DataX, Y + 0.015, Z}, each plus the dialog's
// +0xa8 offset.
struct FServerFilterRowDesc
{
    float Y;                  // +0x00
    float Z;                  // +0x04
    float X;                  // +0x08
    float LabelX;             // +0x0c
    float DataX;              // +0x10
    unsigned char bHasData;   // +0x14 -- build "FilterData%d"; the label is 0.28 wide, else 0.38
    unsigned LabelIds;        // +0x18
    int ToolTipIds;           // +0x1c -- the toggle's two tooltip ids and the label's IdsAlt
};

// +0x338 element (ctor LAB_00562a40 / dtor LAB_00562a50 zero all three).
struct FServerFilterDialogRow
{
    // FUN_00562a60 (thiscall on the row, RET 8) -- the three controls from Desc under Dialog
    // (placed relative to its +0xa8 offset), then the row counter DAT_006148c8 raised.
    void Build(WinControl* Dialog, FServerFilterRowDesc const& Desc);

    WinControl* Check = nullptr;      // +0x0 -- ToggleButtonControl
    RichTextWin* Label = nullptr;     // +0x4
    RichTextEditWin* Data = nullptr;  // +0x8 -- null unless the row description has one
};

class ServerFilterDialog : public WinControl
{
public:
    // Inlined into FUN_00563900: WinControl ctor, the rows zeroed, the ServerFilter ctor at
    // +0x378, the pointers zeroed.
    ServerFilterDialog();
    // FUN_00562f60 (scalar deleting FUN_00562f40) -- the rows zeroed, then WinControl's dtor.
    ~ServerFilterDialog() override;

    // slot 9 FUN_005631c0 (RET 8); Data is the ServerFilter* to edit.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 19 FUN_005635a0 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 20 FUN_00563530 (RET 8)
    bool KeyEvent(unsigned Key, unsigned char* Modifiers) override;
    // slot 23 FUN_00563630 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 FUN_005636d0 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    WinControl* OkButton;                 // +0x32c
    WinControl* CancelButton;             // +0x330
    RichTextWin* Title;                   // +0x334
    FServerFilterDialogRow Rows[5];       // +0x338
    ServerFilter* Target;                 // +0x374
    ServerFilter Backup;                  // +0x378

private:
    // FUN_00562fd0 -- the five toggles and the ping box into *Target.
    void ReadControls();
    // FUN_005630f0 -- *Target into the five toggles and the ping box.
    void WriteControls();
    // The close every exit path spells inline (0x00563555..0x00563587, 0x0056367f..0x005636b1,
    // 0x005636f4..0x00563724, 0x00563747..0x00563777): slot 33 ReleaseMouse, FUN_0043d2a0 on
    // every child (the next sibling read first), slot 10 Destroy, the scalar deleting dtor.
    void Close();

    ServerFilterDialog(ServerFilterDialog const&) = delete;
    ServerFilterDialog& operator=(ServerFilterDialog const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(ServerFilterDialog) == 0x39c,
              "retail ServerFilterDialog is 0x39c bytes (operator_new 0x00563919)");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Rows) == 0x338,
              "ServerFilterDialog +0x338");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Target) == 0x374,
              "ServerFilterDialog +0x374");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Backup) == 0x378,
              "ServerFilterDialog +0x378");

// FUN_005637b0 (cdecl) -- build a ServerFilterDialog under Parent for Filter, centre it (slot 42
// (0x42, &{0, 0, 0}, 0)), capture the mouse (slot 32) and pump frames while one is registered.
// False only when the message pump sees WM_QUIT (FUN_005b0b60); true otherwise, including when
// the factory failed. Caller: PageServerSelect slot 23 (0x0057157f).
bool ServerFilterDialogRun(Win3D* Parent, ServerFilter* Filter);
