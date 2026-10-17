// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail UITable (RTTI .?AUUITable@@ : Win3D, vftable 0x005e5744) -- a PAGE_*.txt grid: row and
// column edges the mouse can drag, one optional BaseRichTextWin per cell laid out on the grid,
// a selectable / hoverable row highlight and a line frame.
//
// Built by WINFACTORY<UITable> DAT_0067da00 (static init LAB_00599220 -> FUN_00599250 "UITable",
// atexit LAB_00599240 -> FUN_00579380; vftable 0x005e57b8, Create FUN_005992c0 =
// operator_new(0x13c)). Constructor FUN_00597cd0 (Win3D ctor inlined), destructor FUN_00597ed0
// (scalar deleting FUN_00597eb0).
//
// vftable 0x005e5744 overrides (the rest are Win3D's):
//   0 FUN_00597eb0 dtor, 4 LAB_00599160 HitTest, 5 LAB_00598850 SetPosition,
//   8 LAB_005987e0 SetTransform, 9 LAB_00598490 Create, 11 LAB_005988c0 Update,
//   13 FUN_00598c40 Draw, 19 LAB_00599090 MouseEvent
//
// Retail layout (0x13c; the port keeps the members, not the VC6 vector layout):
//   +0x07c Width, +0x080 Height (payload [0], [1])
//   +0x084 vector<BaseRichTextWin*> cells, Rows * Columns, index Columns * Row + Col
//   +0x094 vector<float> row edges (payload +0x08 entries; y, 0 at the top, going negative)
//   +0x0a4 vector<float> column edges (payload +0x0c entries; x, 0 at the left)
//   +0x0b4 vector<FUiTableRow> per-row flags + payload handle (payload +0x08 entries)
//   +0x0c4 byte = 1 (never read by UITable), +0x0c8 / +0x0cc never written
//   +0x0d0 bSelected, +0x0d4 selected row, +0x0d8 bHovered, +0x0dc hovered row
//   +0x0e0 frame RGBA, +0x0e4 selected-row RGBA, +0x0e8 hovered-row RGBA
//   +0x0ec edge grab distance 0.004f
//   +0x0f0 bColumnsResizable, +0x0f1 bRowsResizable (ctor 1, nothing clears them)
//   +0x0f4 inverse of the +0x3c transform (12 floats)
//   +0x124 IDC_SIZENS cursor, +0x128 IDC_SIZEWE cursor, +0x12c the cursor at construction
//   +0x130 bDragArmed, +0x131 bDragging, +0x132 bDragColumn, +0x134 drag edge, +0x138 drag offset

#include "ui_fast_table.h"
#include "win3d.h"

#include <windows.h>

#include <vector>

class BaseRichTextWin;

// One +0x0b4 element (8 bytes). The copy constructor is retail's FUN_005999d0: it copies byte 1
// and the handle but NOT byte 0, so an element built by copy (every element vector::resize adds,
// and every element moved by a reallocation) keeps whatever byte 0 the allocator returned. The
// copy assignment (inlined in FUN_00599610 / FUN_005998f0) copies both bytes and the handle.
struct FUiTableRow
{
    // Create's prototype row (0x00598625..0x0059862a): {1, 1, null}.
    FUiTableRow(unsigned char InHoverable, unsigned char InSelectable)
        : bHoverable(InHoverable)
        , bSelectable(InSelectable)
    {
    }

    FUiTableRow(FUiTableRow const& Other)
        : bSelectable(Other.bSelectable)
        , Payload(Other.Payload)
    {
        // FUN_005999d0: byte 0 is left untouched.
    }

    FUiTableRow& operator=(FUiTableRow const& Other)
    {
        bHoverable = Other.bHoverable;
        bSelectable = Other.bSelectable;
        Payload = Other.Payload;
        return *this;
    }

    // FUN_005987a0 -- the handle's release.
    ~FUiTableRow()
    {
    }

    unsigned char bHoverable;  // +0x00 -- Update only hovers rows with this set
    unsigned char bSelectable; // +0x01 -- MouseEvent selects only rows with this set
    UiFastTableRefPtr Payload; // +0x04
};

class UiTable : public Win3D
{
public:
    // FUN_00597cd0
    UiTable();
    // FUN_00597ed0 (scalar deleting FUN_00597eb0)
    ~UiTable() override;

    // slot 4 LAB_00599160
    bool HitTest(float const* Ray) override;
    // slot 5 LAB_00598850
    void SetPosition(float const* NewPosition) override;
    // slot 8 LAB_005987e0
    void SetTransform(float const* Transform) override;
    // slot 9 LAB_00598490 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_005988c0
    void Update() override;
    // slot 13 FUN_00598c40
    void Draw() override;
    // slot 19 LAB_00599090 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_00597ff0 (RET 8) -- move column edge Index (1..count-1) to X, clamped between its
    // neighbours; false for edge 0 or an index out of range.
    bool SetColumnEdge(unsigned Index, float X);
    // FUN_00598100 (RET 8) -- the same for row edge Index and Y.
    bool SetRowEdge(unsigned Index, float Y);
    // FUN_00598210 (RET 0x10) -- place and size the cells of rows [RowStart, RowStart + RowCount)
    // and columns [ColStart, ColStart + ColCount); a zero count means "to the end".
    void Layout(unsigned RowStart, unsigned RowCount, unsigned ColStart, unsigned ColCount);

    float Width;                          // +0x07c
    float Height;                         // +0x080
    std::vector<BaseRichTextWin*> Cells;  // +0x084
    std::vector<float> RowEdges;          // +0x094
    std::vector<float> ColumnEdges;       // +0x0a4
    std::vector<FUiTableRow> Rows;        // +0x0b4
    unsigned char FieldC4;                // +0x0c4 -- ctor 1, not read by UITable
    unsigned char bSelected;              // +0x0d0
    unsigned SelectedRow;                 // +0x0d4
    unsigned char bHovered;               // +0x0d8
    unsigned HoveredRow;                  // +0x0dc
    unsigned char FrameRgba[4];           // +0x0e0
    unsigned char SelectedRgba[4];        // +0x0e4
    unsigned char HoveredRgba[4];         // +0x0e8
    float GrabDistance;                   // +0x0ec
    unsigned char bColumnsResizable;      // +0x0f0
    unsigned char bRowsResizable;         // +0x0f1
    float InverseTransform[12];           // +0x0f4
    HCURSOR RowResizeCursor;              // +0x124
    HCURSOR ColumnResizeCursor;           // +0x128
    HCURSOR DefaultCursor;                // +0x12c
    unsigned char bDragArmed;             // +0x130
    unsigned char bDragging;              // +0x131
    unsigned char bDragColumn;            // +0x132
    unsigned DragEdge;                    // +0x134
    float DragOffset;                     // +0x138

private:
    UiTable(UiTable const&) = delete;
    UiTable& operator=(UiTable const&) = delete;
};
