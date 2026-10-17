// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail UIFastTable (RTTI .?AUUIFastTable@@ : BaseRichTextWin, vftable 0x005e511c + the
// IDataViewOwner face 0x005e5110) and UIScrollTable (.?AUUIScrollTable@@ : UIFastTable, vftable
// 0x005e524c + face 0x005e5240), plus the nested reference-counted row payload
// UIFastTable::RefCounter. Source path in the listing: E:\FL\Scratch\Source\Client\Interface\
// UIFastTable.cpp.
//
// A UIFastTable is ONE rich-text surface (the BaseRichTextWin image) laid out as a grid: every
// cell keeps a source RenderDisplayList, and Relayout (slot 45) lays each cell's list into the
// cell's pixel rectangle and chains all of them into the base class's LayoutList. Row and column
// edges are draggable, rows can be selected / hovered, and tool tips can be keyed per column, row
// or cell. UITable (ui_table.h) is the older sibling built from one BaseRichTextWin per cell.
//
// Factories: WINFACTORY<UIFastTable> DAT_0067d924 (static init LAB_0058f940 -> FUN_0058fac0
// "UIFastTable", atexit LAB_0058f960; Create FUN_0058fb30 = operator_new(0x578) + FUN_0058c6e0 +
// slot 9) and WINFACTORY<UIScrollTable> DAT_0067d8f8 (LAB_0058f970 -> FUN_0058fbd0
// "UIScrollTable", atexit LAB_0058f990; Create FUN_0058fc40 = operator_new(0x58c) + FUN_0058c6e0
// + the UIScrollTable vftables and five zeroed fields, then slot 9).
//
// UIFastTable vftable 0x005e511c overrides of BaseRichTextWin 0x005e459c:
//   0 FUN_0058c9e0 (dtor FUN_0058ca00), 4 LAB_0058f3e0 HitTest, 5 LAB_0058e510 SetPosition,
//   8 LAB_0058e470 SetTransform, 9 FUN_0058e000 Create, 11 FUN_0058e5a0 Update,
//   13 LAB_0058eb30 Draw, 16 LAB_0058f210 MouseLeave, 19 LAB_0058f230 MouseEvent,
//   45 FUN_0058d9d0 Relayout, 46 LAB_0058d8f0 GetToolTipHoverInfo.
// UIScrollTable vftable 0x005e524c: 0 FUN_0058f4b0 (dtor FUN_0058f4d0 = the UIFastTable dtor
//   inlined), 9 FUN_0058f7a0 Create, 23 LAB_0058f8b0 Slot23; every other slot is UIFastTable's.
//
// Layout (after BaseRichTextWin's 0x488; the port keeps the members, not the VC6 containers):
//   +0x488 Width, +0x48c Height (payload [0], [1])
//   +0x490 vector<FUiFastTableCell> (rows * columns, index columns * row + column)
//   +0x4a0 vector<float> row edges (y, 0 at the top, going negative)
//   +0x4b0 vector<float> column edges (x, 0 at the left)
//   +0x4c0 vector<FUiFastTableRow> (one per row)
//   +0x4d0..+0x4d7 not touched by any UIFastTable function
//   +0x4d8 bSelected, +0x4dc selected row, +0x4e0 hovered column, +0x4e4 hovered row (the cell
//          under the mouse, -1 when none), +0x4e8 bHovered, +0x4ec hovered (highlighted) row
//   +0x4f0 frame RGBA (DAT_00615550), +0x4f4 selected-row RGBA (DAT_00615554), +0x4f8
//          hovered-row RGBA (DAT_00615558)
//   +0x4fc edge grab distance 0.004f, +0x500 bColumnsResizable, +0x501 bRowsResizable
//   +0x504 inverse of the +0x3c transform (12 floats); +0x534..+0x53f not touched
//   +0x540 bDragArmed, +0x541 bDragging, +0x542 bDragColumn, +0x544 drag edge, +0x548 offset
//   +0x54c LayoutRichText's bool argument, +0x54d row lines, +0x54e column lines, +0x54f border,
//   +0x550..+0x553 border top / bottom / left / right, +0x554 row highlight quads
//   +0x558 header row count, +0x55c tool tip mode, +0x560 vector<FUiFastTableTip>
//   +0x570 horizontal cell margin, +0x574 vertical cell margin (pixels)
//   size 0x578
// UIScrollTable adds +0x578 callback, +0x57c item count, +0x580 fixed rows, +0x584 callback
// context, +0x588 the vertical UIScroll; size 0x58c.

#include "rich_text_win.h"
#include "tooltip_hover.h"

#include "common_text.h"
#include "x86math.h"

#include <vector>

class UiScroll;

// Retail UIFastTable::RefCounter (RTTI .?AVRefCounter@UIFastTable@@). vftable 0x005cfd94 has one
// slot, the scalar deleting destructor LAB_00467ef0; the destructor body FUN_00467ee0 only
// restores the vftable. Layout: +0x00 vftable, +0x04 reference count. The constructor is always
// inlined (0x005712a2: vftable, then +0x04 = 1).
class UiFastTableRefCounter
{
public:
    UiFastTableRefCounter()
        : RefCount(1)
    {
    }

    // slot 0 -- LAB_00467ef0 / FUN_00467ee0
    virtual ~UiFastTableRefCounter();

    // Inlined in every pointer copy (0x0059043a, 0x00599a49): INC [obj+4].
    void AddRef()
    {
        ++RefCount;
    }

    // FUN_00599980 (thiscall): if the count is non-zero decrement it; if it is then zero, set it
    // back to 1 and call vftable slot 0 with 1 (delete this) and return 0, else return the count.
    unsigned Release();

    unsigned RefCount; // +0x04

private:
    UiFastTableRefCounter(UiFastTableRefCounter const&) = delete;
    UiFastTableRefCounter& operator=(UiFastTableRefCounter const&) = delete;
};

// The one-pointer handle rows keep to a RefCounter. Retail has no class name for it in RTTI
// (it is not polymorphic); its members are the functions below.
class UiFastTableRefPtr
{
public:
    UiFastTableRefPtr()
        : Object(nullptr)
    {
    }

    // The pointer half of FUN_005999d0 / FUN_00590ab0: zero, then the inlined assignment (the
    // release of the just-zeroed pointer is dead), i.e. copy the pointer and AddRef it.
    UiFastTableRefPtr(UiFastTableRefPtr const& Other);

    // FUN_00599a60 / the pointer half of FUN_005987a0 and FUN_0058e430 -- Reset().
    ~UiFastTableRefPtr();

    // FUN_00590400 (RET 4): release the held object first (the Release() body inline, then
    // pointer = 0), then take Other's pointer and AddRef it. No self-assignment test: assigning a
    // pointer to itself while it holds the last reference deletes the object first, as in retail.
    UiFastTableRefPtr& operator=(UiFastTableRefPtr const& Other);

    // FUN_00575520 (RET 4) -- nothing when Raw is already held; otherwise the held object
    // released (Release() inline, pointer = 0), Raw taken and AddRef'd when not null. Returns
    // the held pointer. Callers: 0x00569648, 0x0056ecf6 (Raw = 0), 0x005711e1.
    UiFastTableRefCounter* Assign(UiFastTableRefCounter* Raw);

    // FUN_00599a60 (thiscall): held object -> Release(), pointer = 0.
    void Reset();

    UiFastTableRefCounter* Object; // +0x00
};

// One +0x490 element (0x1c bytes). Copy construction is FUN_005909c0 (two list copies and the
// byte), assignment FUN_00591150, destruction FUN_0058e360 (the +0x0c list first, then +0x00):
// exactly the implicit members over these three fields.
struct FUiFastTableCell
{
    // Create's prototype cell (0x0058e120..0x0058e13c): two empty lists, byte 0.
    FUiFastTableCell()
        : bSpanToEnd(0)
    {
    }

    RenderDisplayList Source;   // +0x00 -- what the owner puts in the cell (GetCell / ClearCell)
    RenderDisplayList Laid;     // +0x0c -- Relayout's output: a ClipNode, then the laid text
    unsigned char bSpanToEnd;   // +0x18 -- FUN_0058ce10; Relayout clips to the last column edge
};

// One +0x4c0 element (12 bytes).
struct FUiFastTableRow
{
    // Create's prototype row (0x0058e1c4..0x0058e1d9): {1, 1, 1, 0, null}.
    FUiFastTableRow()
        : bHoverable(1)
        , bSelectable(1)
        , bRowLine(1)
        , GroupId(0)
    {
    }

    // FUN_00590ab0 (construct-in-place): the three bytes and the handle are copied, GroupId is
    // NOT -- it is written 0 (0x00590af2 MOV [ESI+4],EBX with EBX = 0). So every row a resize adds,
    // and every row a reallocation moves, starts with GroupId 0.
    FUiFastTableRow(FUiFastTableRow const& Other)
        : bHoverable(Other.bHoverable)
        , bSelectable(Other.bSelectable)
        , bRowLine(Other.bRowLine)
        , GroupId(0)
        , Payload(Other.Payload)
    {
    }

    // FUN_00590d10: the three bytes, GroupId, then the handle (FUN_00590400 inline).
    FUiFastTableRow& operator=(FUiFastTableRow const& Other)
    {
        bHoverable = Other.bHoverable;
        bSelectable = Other.bSelectable;
        bRowLine = Other.bRowLine;
        GroupId = Other.GroupId;
        Payload = Other.Payload;
        return *this;
    }

    // FUN_0058e430 -- the handle's release (Release() inline, pointer = 0).
    ~FUiFastTableRow()
    {
    }

    unsigned char bHoverable;  // +0x00 -- Update hovers only rows with this set
    unsigned char bSelectable; // +0x01 -- selection needs this and +0x00
    unsigned char bRowLine;    // +0x02 -- Draw's row separator above this row (+0x54d)
    unsigned GroupId;          // +0x04 -- Draw outlines the hovered row's run of equal ids
    UiFastTableRefPtr Payload; // +0x08
};

// Retail FTableEdges: what GetEdges (FUN_0058d0f0) fills and SetEdges (FUN_0058d250) reads --
// the row edges at +0x00 and the column edges at +0x10 (two VC6 vector<float>).
struct FRegKeyStack;

struct FUiFastTableEdges
{
    // FUN_0058c370 (RET 4) -- the column edges as REG_BINARY "Columns" and the row edges as
    // "Rows" under Stack's top key (either skipped when its vector is empty). Always true.
    // Callers: PageCharacterSelect (0x00567445), PageServerSelect FUN_0056daa0 (0x0056db95).
    bool SaveToRegistry(FRegKeyStack* Stack) const;
    // FUN_0058c470 (RET 4) -- read both values back when the second query returns the size the
    // first one reported (a failed query counts as size -1 both times); each vector is emptied,
    // resized to size / 4 and filled. Always true. Callers: 0x00567b0b, 0x0056ea37.
    bool LoadFromRegistry(FRegKeyStack* Stack);

    std::vector<float> Rows;    // +0x00
    std::vector<float> Cols;    // +0x10
};

// One +0x560 element: a ToolTip hover record (0x64 bytes, tooltip_hover.h) with the copy and
// teardown the vector instantiation gives it.
struct FUiFastTableTip
{
    // SetTipMode's prototype (0x0058d647..0x0058d677 and the two sibling blocks): caption buffer
    // empty, the Font::String (FUN_004144b0), then FUN_004963a0 (key -1, IdsAlt 0, AuxField 0,
    // live 0). Retail leaves +0x48..+0x5f (WorldPos, HalfExtents) as stack garbage and every
    // copy inherits it; the port zeroes them.
    FUiFastTableTip();

    // FUN_00590b40 (construct-in-place): key and IdsAlt, a deep copy of the caption (wcslen,
    // malloc, wcsncpy), the Font::String copied member by member (its text deep-copied through
    // FUN_004142a0, its child slots copied with an AddRef each through FUN_00591420 /
    // FUN_00418f10), then AuxField, WorldPos, HalfExtents and the live byte.
    FUiFastTableTip(FUiFastTableTip const& Other);

    // FUN_00444ee0: the Font::String teardown (FUN_004144f0, the child releases, the child
    // vector and text freed), then the caption freed.
    ~FUiFastTableTip();

    FToolTipHoverInfo Info;

private:
    // Retail's assignment (the inlined record copy around FUN_00590f30 in FUN_005908a0 /
    // FUN_00590500) only runs when elements shift inside the vector; UIFastTable only ever
    // erases the whole vector and grows it from empty (FUN_0058d5e0), so no shift happens.
    FUiFastTableTip& operator=(FUiFastTableTip const&) = delete;
};

class UiFastTable : public BaseRichTextWin
{
public:
    // FUN_0058c6e0
    UiFastTable();
    // FUN_0058ca00 (scalar deleting FUN_0058c9e0)
    ~UiFastTable() override;

    // slot 4 LAB_0058f3e0 (RET 4)
    bool HitTest(float const* Ray) override;
    // slot 5 LAB_0058e510 (RET 4)
    void SetPosition(float const* NewPosition) override;
    // slot 8 LAB_0058e470 (RET 4)
    void SetTransform(float const* Transform) override;
    // slot 9 FUN_0058e000 (RET 8); Data = FUiPayloadTable {Width, Height, rows, columns}.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_0058e5a0
    void Update() override;
    // slot 13 LAB_0058eb30
    void Draw() override;
    // slot 16 LAB_0058f210 (RET 4)
    void MouseLeave(float const* Ray) override;
    // slot 19 LAB_0058f230 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 45 FUN_0058d9d0
    bool Relayout() override;
    // slot 46 LAB_0058d8f0 -- &+0x40c in tip mode 0, else null.
    FToolTipHoverInfo* GetToolTipHoverInfo() override;

    // FUN_0058cb70 (RET 4) -- +0x54e.
    void SetDrawColumnLines(bool bDraw);
    // FUN_0058cb80 (RET 0x10) -- +0x552 = Left, +0x553 = Right, +0x550 = Top, +0x551 = Bottom.
    void SetBorders(bool bLeft, bool bRight, bool bTop, bool bBottom);
    // FUN_0058cbb0 (RET 4) -- +0x558 = Rows when Rows < row count.
    void SetHeaderRows(unsigned Rows);
    // FUN_0058cbe0 (RET 4) -- +0x554.
    void SetRowHighlights(bool bQuads);
    // FUN_0058cbf0 (RET 8) -- empty the cell's source list and dirty the layout.
    bool ClearCell(unsigned Row, unsigned Column);
    // FUN_0058cd00 (RET 0xc)
    bool GetCell(unsigned Row, unsigned Column, FUiFastTableCell** OutCell);
    // FUN_0058cdb0 (RET 8) -- dirty the layout when (Row, Column) is inside the grid.
    void InvalidateCell(unsigned Row, unsigned Column);
    // FUN_0058ce10 (RET 0xc) -- +0x18 of the cell.
    void SetCellSpanToEnd(unsigned Row, unsigned Column, bool bSpan);
    // FUN_0058ceb0 (RET 8) -- move column edge Index (1..count-1) to X, clamped between its
    // neighbours; false for edge 0 or an index out of range.
    bool SetColumnEdge(unsigned Index, float X);
    // FUN_0058cfb0 (RET 8) -- the same for row edge Index and Y.
    bool SetRowEdge(unsigned Index, float Y);
    // FUN_0058d0b0 (RET 4) -- &Rows[Row], or null out of range.
    FUiFastTableRow* GetRow(unsigned Row);
    // FUN_0058d0f0 (RET 4)
    void GetEdges(FUiFastTableEdges* Out);
    // FUN_0058d250 (RET 4) -- FDUMP notice and no change when the shape or the first column
    // edge differs.
    void SetEdges(FUiFastTableEdges const* In);
    // FUN_0058d4a0
    void ClearSelection();
    // FUN_0058d4b0 (RET 4)
    bool SelectRow(unsigned Row);
    // FUN_0058d540 (RET 4)
    bool GetSelectedRow(unsigned* OutRow);
    // FUN_0058d560 (RET 4) -- +0x4f0.
    void SetFrameColor(unsigned long const* Rgba);
    // FUN_0058d570 (RET 4) -- only while the current distance is not negative.
    void SetGrabDistance(float Distance);
    // FUN_0058d590 (RET 8) -- +0x501 = bRows, +0x500 = bColumns.
    void SetResizable(bool bRows, bool bColumns);
    // FUN_0058d5b0 (RET 8) -- +0x570 = X, +0x574 = Y.
    void SetCellMargins(int X, int Y);
    // FUN_0058d5d0 (RET 4) -- +0x54c.
    void SetLayoutFlag(bool bFlag);
    // FUN_0058d5e0 (RET 4) -- 0 none (the base record), 1 per column, 2 per row, 3 per cell.
    void SetTipMode(int Mode);
    // FUN_0058d910 (RET 4) -- mode 2 only.
    FToolTipHoverInfo* GetRowTip(unsigned Row);
    // FUN_0058d960 (RET 8) -- mode 3 only.
    FToolTipHoverInfo* GetCellTip(unsigned Row, unsigned Column);

    float Width;                            // +0x488
    float Height;                           // +0x48c
    std::vector<FUiFastTableCell> Cells;    // +0x490
    std::vector<float> RowEdges;            // +0x4a0
    std::vector<float> ColEdges;            // +0x4b0
    std::vector<FUiFastTableRow> Rows;      // +0x4c0
    unsigned char bSelected;                // +0x4d8
    unsigned SelectedRow;                   // +0x4dc
    unsigned HoverColumn;                   // +0x4e0
    unsigned HoverRow;                      // +0x4e4
    unsigned char bHovered;                 // +0x4e8
    unsigned HoveredRow;                    // +0x4ec
    unsigned long FrameRgba;                // +0x4f0
    unsigned long SelectedRgba;             // +0x4f4
    unsigned long HoveredRgba;              // +0x4f8
    float GrabDistance;                     // +0x4fc
    unsigned char bColumnsResizable;        // +0x500
    unsigned char bRowsResizable;           // +0x501
    float InverseTransform[12];             // +0x504
    unsigned char bDragArmed;               // +0x540
    unsigned char bDragging;                // +0x541
    unsigned char bDragColumn;              // +0x542
    unsigned DragEdge;                      // +0x544
    float DragOffset;                       // +0x548
    unsigned char bLayoutFlag;              // +0x54c
    unsigned char bDrawRowLines;            // +0x54d
    unsigned char bDrawColumnLines;         // +0x54e
    unsigned char bDrawBorder;              // +0x54f
    unsigned char bBorderTop;               // +0x550
    unsigned char bBorderBottom;            // +0x551
    unsigned char bBorderLeft;              // +0x552
    unsigned char bBorderRight;             // +0x553
    unsigned char bRowHighlights;           // +0x554
    unsigned HeaderRows;                    // +0x558
    int TipMode;                            // +0x55c
    std::vector<FUiFastTableTip> Tips;      // +0x560
    int CellMarginX;                        // +0x570
    int CellMarginY;                        // +0x574

protected:
    // The mouse ray scaled onto the table's z plane, then into table space through +0x504
    // (0x0058e5d1..0x0058e61e, 0x0058e69f..0x0058e6ec, 0x0058f3ec..0x0058f448).
    FVector RayToLocal(float const* Ray) const;
    // Draw's row highlight quad (0x0058ebf5..0x0058ecdf / 0x0058ecf2..0x0058eddc).
    void DrawRowQuad(unsigned long const* Rgba, unsigned Row) const;

private:
    UiFastTable(UiFastTable const&) = delete;
    UiFastTable& operator=(UiFastTable const&) = delete;
};

// +0x578: called (cdecl) with the context, ftol(scrollbar value) and the visible row count
// (rows - fixed rows) whenever the scrollbar moves (Slot23) and once when it is installed
// (FUN_0058f740). The return value is not read.
typedef void (*FUiScrollTableCallback)(void* Context, int FirstItem, unsigned VisibleRows);

class UiScrollTable : public UiFastTable
{
public:
    // FUN_0058fc40's inline construction after FUN_0058c6e0: +0x578, +0x584, +0x57c, +0x580,
    // +0x588 = 0.
    UiScrollTable();
    // FUN_0058f4d0 -- nothing of its own; the UIFastTable teardown.
    ~UiScrollTable() override;

    // slot 9 FUN_0058f7a0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 23 LAB_0058f8b0 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;

    // FUN_0058f640 (RET 8) -- the scrollbar's range for ItemCount items with FixedRows rows
    // not scrolled; hidden (value 0) when everything fits.
    void SetItemCount(unsigned ItemCount, unsigned FixedRows);
    // FUN_0058f730 -- ftol(scrollbar value).
    int GetFirstItem();
    // FUN_0058f740 (RET 8) -- install the callback and call it once when it is not null.
    void SetCallback(FUiScrollTableCallback Callback, void* Context);
    // FUN_0058f810 (RET 4) -- +0x3fc, then re-place the scrollbar beside the table.
    void SetScrollBarGap(float Gap);

    FUiScrollTableCallback Callback;    // +0x578
    unsigned ItemCount;                 // +0x57c
    unsigned FixedRows;                 // +0x580
    void* CallbackContext;              // +0x584
    UiScroll* ScrollBar;                // +0x588

private:
    UiScrollTable(UiScrollTable const&) = delete;
    UiScrollTable& operator=(UiScrollTable const&) = delete;
};
