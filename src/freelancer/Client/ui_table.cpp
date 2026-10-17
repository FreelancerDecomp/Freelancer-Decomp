// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_table.h"

#include "rich_text_win.h"
#include "star_sphere.h"
#include "win3d_immediate.h"
#include "win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "x86math.h"

#include <math.h>

namespace
{
    // DAT_0067da38 -- this TU's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_UiTableMath = nullptr;

    I3DMathEngine* UiTableMath()
    {
        if (G_UiTableMath == nullptr)
            G_UiTableMath = Acquire3DMathEngine();
        return G_UiTableMath;
    }

    // .data DAT_006156a8 / DAT_006156ac / DAT_006156b0, read only by the constructor
    // (0x00597e28..0x00597e45; no writer anywhere in the listing). R, G, B, A byte order, the
    // order FUN_00421ba0 reads them.
    unsigned char const kUiTableFrameRgba[4] = {0x00, 0xff, 0x00, 0xff};    // FF00FF00h
    unsigned char const kUiTableSelectedRgba[4] = {0xa0, 0x00, 0x00, 0xa0}; // A00000A0h
    unsigned char const kUiTableHoveredRgba[4] = {0x80, 0x80, 0x80, 0x80};  // 80808080h

    // The seven render states both halves of Draw set (0x00598c4f..0x00598ce1 and
    // 0x00598ee4..0x00598f77): pipeline slot +0xf0.
    void UiTableSetDrawStates()
    {
        DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
        DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
        DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 5);
        DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 6);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
    }

    // The mouse ray scaled onto the table's z plane (Position.z / Ray.z, the quotient kept on the
    // x87 stack: 0x0059890f..0x00598938, 0x005989db..0x00598a04, 0x0059916c..0x005991a4), then
    // taken into table space through the +0xf4 inverse (3DMathEngine +0x28 TransformXform).
    FVector UiTableRayToLocal(UiTable const* Table, float const* Ray)
    {
        double const Scale = static_cast<double>(Table->Position[2]) / Ray[2];
        FVector Scaled;
        Scaled.X = static_cast<float>(Ray[0] * Scale);
        Scaled.Y = static_cast<float>(Ray[1] * Scale);
        Scaled.Z = static_cast<float>(Ray[2] * Scale);
        FVector Local;
        UiTableMath()->TransformXform(
            Local, *reinterpret_cast<FTransform const*>(Table->InverseTransform), Scaled);
        return Local;
    }

    // Draw's highlight quad for row Row (0x00598cf5..0x00598dd9, 0x00598dec..0x00598ed0).
    void UiTableDrawRowQuad(UiTable const* Table, unsigned char const* Rgba, unsigned Row)
    {
        Win3DColor4ubv(Rgba);
        Win3DBegin(kWin3DQuads);
        Win3DVertex3f(Table->ColumnEdges[0], Table->RowEdges[Row], 0.0f);
        Win3DVertex3f(Table->Width, Table->RowEdges[Row], 0.0f);
        if (Row + 1 < Table->RowEdges.size())
        {
            Win3DVertex3f(Table->Width, Table->RowEdges[Row + 1], 0.0f);
            Win3DVertex3f(Table->ColumnEdges[0], Table->RowEdges[Row + 1], 0.0f);
        }
        else
        {
            Win3DVertex3f(Table->Width, -Table->Height, 0.0f);
            Win3DVertex3f(Table->ColumnEdges[0], -Table->Height, 0.0f);
        }
        Win3DEnd();
    }
}

// Retail DAT_0067da00 -- WINFACTORY<UITable> static (LAB_00599220 -> FUN_00599250; Create
// FUN_005992c0 = operator_new(0x13c) + FUN_00597cd0 + slot 9, scalar delete on false).
WINFACTORY<UiTable> G_UITableFactory("UITable");

UiTable::UiTable()
    : Width(0.0f)
    , Height(0.0f)
    , FieldC4(1)
    , bSelected(0)
    , SelectedRow(0)
    , bHovered(0)
    , HoveredRow(0)
    , GrabDistance(0.004f)
    , bColumnsResizable(1)
    , bRowsResizable(1)
    , bDragArmed(0)
    , bDragging(0)
    , bDragColumn(0)
    , DragEdge(0)
    , DragOffset(0.0f)
{
    // FUN_00597cd0 after the inlined Win3D ctor: empty vectors, +0xf4 identity with zero
    // translation, the flags and colours, then the two resize cursors (LoadImageA(0, 32645 /
    // 32644, IMAGE_CURSOR, 0, 0, LR_SHARED)) and GetCursor(). +0x138 is written as the integer 0.
    Win3DIdentityRotation3x3(InverseTransform);
    InverseTransform[9] = 0.0f;
    InverseTransform[10] = 0.0f;
    InverseTransform[11] = 0.0f;
    for (int I = 0; I < 4; ++I)
    {
        FrameRgba[I] = kUiTableFrameRgba[I];
        HoveredRgba[I] = kUiTableHoveredRgba[I];
        SelectedRgba[I] = kUiTableSelectedRgba[I];
    }
    RowResizeCursor = static_cast<HCURSOR>(
        LoadImageA(nullptr, MAKEINTRESOURCEA(32645), IMAGE_CURSOR, 0, 0, LR_SHARED));
    ColumnResizeCursor = static_cast<HCURSOR>(
        LoadImageA(nullptr, MAKEINTRESOURCEA(32644), IMAGE_CURSOR, 0, 0, LR_SHARED));
    DefaultCursor = GetCursor();
}

UiTable::~UiTable()
{
    // FUN_00597ed0: release every row handle and free +0xb4, then free +0xa4, +0x94, +0x84 --
    // the members' own destructors in reverse declaration order -- then Win3D's (inlined).
}

bool UiTable::SetColumnEdge(unsigned Index, float X)
{
    // FUN_00597ff0
    if (Index == 0 || ColumnEdges.empty() || Index >= ColumnEdges.size())
        return false;
    unsigned const Prev = Index - 1;
    unsigned Count = 2;
    if (Prev + 2 > ColumnEdges.size())
        Count = static_cast<unsigned>(ColumnEdges.size()) - Prev;
    float const Gap2 = GrabDistance + GrabDistance;
    float const Lo = Gap2 + ColumnEdges[Prev];
    float const Next = Index + 1 == ColumnEdges.size() ? Width : ColumnEdges[Index + 1];
    float const Hi = Next - Gap2;
    // 0x005980a9 TEST AH,5 / JP: below Lo -> Lo; 0x005980bc TEST AH,0x41 / JNZ: above Hi -> Hi.
    if (X < Lo)
        X = Lo;
    else if (X > Hi)
        X = Hi;
    // 0x005980cd TEST AH,0x44 / JNP skips the store only when equal.
    if (X != ColumnEdges[Index])
    {
        ColumnEdges[Index] = X;
        Layout(0, 0, Prev, Count);
    }
    return true;
}

bool UiTable::SetRowEdge(unsigned Index, float Y)
{
    // FUN_00598100
    if (Index == 0 || RowEdges.empty() || Index >= RowEdges.size())
        return false;
    unsigned const Prev = Index - 1;
    unsigned Count = 2;
    if (Prev + 2 > RowEdges.size())
        Count = static_cast<unsigned>(RowEdges.size()) - Prev;
    float const Gap2 = GrabDistance + GrabDistance;
    float const Lo = RowEdges[Prev] - Gap2;
    // The lower limit stays on the x87 stack (0x005981ae FADDP, compared at 0x005981b5). For the
    // last edge retail adds +Height, not -Height: the limit is then above every edge, the first
    // test always clamps to it and the second clamps to Lo, so the last row edge only ever lands
    // on Lo. Kept as retail has it.
    double const Hi = static_cast<double>(Index + 1 == RowEdges.size() ? Height
                                                                        : RowEdges[Index + 1])
                      + Gap2;
    if (Y < Hi)
        Y = static_cast<float>(Hi);
    else if (Y > Lo)
        Y = Lo;
    if (RowEdges[Index] != Y)
    {
        RowEdges[Index] = Y;
        Layout(Prev, Count, 0, 0);
    }
    return true;
}

void UiTable::Layout(unsigned RowStart, unsigned RowCount, unsigned ColStart, unsigned ColCount)
{
    // FUN_00598210
    if (RowEdges.empty() || RowStart >= RowEdges.size())
        return;
    if (ColumnEdges.empty() || ColStart >= ColumnEdges.size())
        return;
    if (RowCount == 0)
        RowCount = static_cast<unsigned>(RowEdges.size()) - RowStart;
    if (ColCount == 0)
        ColCount = static_cast<unsigned>(ColumnEdges.size()) - ColStart;
    unsigned const RowEnd = RowStart + RowCount;
    unsigned const ColEnd = ColStart + ColCount;
    for (unsigned Row = RowStart; Row < RowEnd; ++Row)
    {
        // Height of the row: to the next edge, the last one to -Height.
        float const RowHeight = Row < static_cast<unsigned>(RowEdges.size()) - 1
                                    ? RowEdges[Row] - RowEdges[Row + 1]
                                    : RowEdges[Row] + Height;
        for (unsigned Col = ColStart; Col < ColEnd; ++Col)
        {
            // 0x00598335..0x0059837d: the element address, or 0 when either index or the flat
            // index is out of range -- and the element is then read through that address
            // without a test (0x0059837d CMP [ESI],0), as retail does.
            BaseRichTextWin** Cell = nullptr;
            if (Row < RowEdges.size() && Col < ColumnEdges.size())
            {
                unsigned const Flat = static_cast<unsigned>(ColumnEdges.size()) * Row + Col;
                if (Flat < Cells.size())
                    Cell = &Cells[Flat];
            }
            if (*Cell == nullptr)
                continue;

            // Cell transform = this transform * (identity, (column edge, row edge, 0)).
            FTransform Offset;
            Win3DIdentityRotation3x3(&Offset.Rotation.D[0][0]);
            Offset.Translation.X = ColumnEdges[Col];
            Offset.Translation.Y = RowEdges[Row];
            Offset.Translation.Z = 0.0f;
            FTransform World;
            UiTableMath()->MulTransform(World, *reinterpret_cast<FTransform const*>(Orientation),
                                        Offset);
            FTransform const Copy = World; // FUN_00406880
            (*Cell)->SetTransform(reinterpret_cast<float const*>(&Copy));

            float const ColumnWidth = Col < static_cast<unsigned>(ColumnEdges.size()) - 1
                                          ? ColumnEdges[Col + 1] - ColumnEdges[Col]
                                          : Width - ColumnEdges[Col];
            (*Cell)->SetNormSize(ColumnWidth, RowHeight);
            (*Cell)->EnsureSurface();
        }
    }
}

bool UiTable::Create(Win3D* NewParent, void* Data)
{
    // LAB_00598490
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | 3);
    if (Data != nullptr)
    {
        // UITable payload (ui_page_desc.h FUiPayloadTable): {Right, Bottom, rows, columns}.
        float const* const Size = static_cast<float const*>(Data);
        int const* const Counts = static_cast<int const*>(Data);
        Width = Size[0];
        Height = Size[1];
        RowEdges.resize(static_cast<unsigned>(Counts[2]), 0.0f);
        ColumnEdges.resize(static_cast<unsigned>(Counts[3]), 0.0f);
        Cells.resize(static_cast<unsigned>(Counts[3]) * static_cast<unsigned>(Counts[2]),
                     nullptr);
        FUiTableRow const Prototype(1, 1);
        Rows.resize(static_cast<unsigned>(Counts[2]), Prototype);
    }

    // Evenly spaced edges: rows going down from 0, columns going right from 0. No Layout here.
    if (ColumnEdges.size() > 0 && !RowEdges.empty() && RowEdges.size() > 0)
    {
        float const RowStep = static_cast<float>(
            Height / static_cast<double>(static_cast<unsigned>(RowEdges.size())));
        float const ColumnStep = static_cast<float>(
            Width / static_cast<double>(static_cast<unsigned>(ColumnEdges.size())));
        for (unsigned I = 0; !RowEdges.empty() && I < RowEdges.size(); ++I)
            RowEdges[I] = static_cast<float>(-(static_cast<double>(I) * RowStep));
        for (unsigned I = 0; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
            ColumnEdges[I] = static_cast<float>(static_cast<double>(I) * ColumnStep);
    }
    return true;
}

void UiTable::SetTransform(float const* Transform)
{
    // LAB_005987e0: slot 7, slot 5 (both virtual), +0xf4 = GeneralInverse(+0x3c) (the W
    // out-parameter lands in the argument slot), Layout of everything.
    SetOrientation(Transform);
    SetPosition(Transform + 9);
    float W = 0.0f;
    FTransform Inverse;
    UiTableMath()->GeneralInverse(Inverse, W, *reinterpret_cast<FTransform const*>(Orientation));
    *reinterpret_cast<FTransform*>(InverseTransform) = Inverse;
    Layout(0, 0, 0, 0);
}

void UiTable::SetPosition(float const* NewPosition)
{
    // LAB_00598850: +0x60 = NewPosition, then the same inverse and Layout as SetTransform.
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];
    float W = 0.0f;
    FTransform Inverse;
    UiTableMath()->GeneralInverse(Inverse, W, *reinterpret_cast<FTransform const*>(Orientation));
    *reinterpret_cast<FTransform*>(InverseTransform) = Inverse;
    Layout(0, 0, 0, 0);
}

void UiTable::Update()
{
    // LAB_005988c0 -- Win3D's child update (FUN_005a17b0) inlined first.
    Win3D::Update();

    if (Win3DGetCapture() == this && bDragging != 0)
    {
        // Dragging an edge: follow the mouse with the grab offset kept.
        float Ray[3];
        Win3DGetMouseRay(Ray);
        FVector const Local = UiTableRayToLocal(this, Ray);
        if (bDragColumn != 0)
        {
            SetColumnEdge(DragEdge, Local.X + DragOffset);
            SetCursor(ColumnResizeCursor);
        }
        else
        {
            SetRowEdge(DragEdge, Local.Y + DragOffset);
            SetCursor(RowResizeCursor);
        }
        return;
    }
    if (Win3DGetCapture() != nullptr)
        return;

    float Ray[3];
    Win3DGetMouseRay(Ray);
    FVector const Local = UiTableRayToLocal(this, Ray);
    bHovered = 0;
    bDragArmed = 0;
    bDragging = 0;
    // 0x00598a2b..0x00598a92: 0 <= x < Width and -Height < y <= 0, else nothing more (no
    // SetCursor either).
    if (!(Local.X >= 0.0f))
        return;
    if (!(Local.X < Width))
        return;
    if (!(Local.Y <= 0.0f))
        return;
    float const Bottom = -Height;
    if (!(Local.Y > Bottom))
        return;

    if (bColumnsResizable != 0)
    {
        for (unsigned I = 1; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
        {
            float const Edge = ColumnEdges[I];
            if (GrabDistance > fabs(static_cast<double>(Local.X) - Edge))
            {
                SetCursor(ColumnResizeCursor);
                DragEdge = I;
                bDragArmed = 1;
                DragOffset = Edge - Local.X;
                bDragColumn = 1;
                return;
            }
        }
    }

    unsigned I = 0;
    while (!RowEdges.empty() && I < RowEdges.size())
    {
        float const Edge = RowEdges[I];
        float const Next = I + 1 < RowEdges.size() ? RowEdges[I + 1] : Bottom;
        if (bRowsResizable != 0 && I > 0
            && GrabDistance > fabs(static_cast<double>(Local.Y) - Edge))
        {
            SetCursor(RowResizeCursor);
            DragEdge = I;
            bDragColumn = 0;
            DragOffset = Edge - Local.Y;
            bDragArmed = 1;
            return;
        }
        // 0x00598b97 TEST AH,0x41 / JP: above this edge (or unordered) -> next row;
        // 0x00598ba6 TEST AH,0x41 / JNZ: at or below the next edge (or unordered) -> next row.
        if (!(Local.Y <= Edge) || !(Local.Y > Next))
        {
            ++I;
            continue;
        }
        if (!RowEdges.empty() && I < RowEdges.size() && Rows[I].bHoverable != 0)
        {
            HoveredRow = I;
            bHovered = 1;
        }
        break;
    }
    SetCursor(DefaultCursor);
}

void UiTable::Draw()
{
    // FUN_00598c40
    Win3DBindModelview(nullptr, Orientation);
    UiTableSetDrawStates();
    if (bSelected != 0)
        UiTableDrawRowQuad(this, SelectedRgba, SelectedRow);
    if (bHovered != 0)
        UiTableDrawRowQuad(this, HoveredRgba, HoveredRow);

    Win3DDrawChildren(this);

    Win3DBindModelview(nullptr, Orientation);
    UiTableSetDrawStates();
    Win3DColor4ubv(FrameRgba);
    Win3DBegin(kWin3DLineLoop);
    Win3DVertex3f(0.0f, 0.0f, 0.0f);
    Win3DVertex3f(Width, 0.0f, 0.0f);
    Win3DVertex3f(Width, -Height, 0.0f);
    Win3DVertex3f(0.0f, -Height, 0.0f);
    Win3DEnd();
    Win3DBegin(kWin3DLines);
    for (unsigned I = 0; !RowEdges.empty() && I < RowEdges.size(); ++I)
    {
        Win3DVertex3f(0.0f, RowEdges[I], 0.0f);
        Win3DVertex3f(Width, RowEdges[I], 0.0f);
    }
    for (unsigned I = 0; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
    {
        Win3DVertex3f(ColumnEdges[I], 0.0f, 0.0f);
        Win3DVertex3f(ColumnEdges[I], -Height, 0.0f);
    }
    Win3DEnd();
}

bool UiTable::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00599090
    if (Event->Field0 == -1)
    {
        if (Event->Field1 == 2)
        {
            if (bHovered != 0)
            {
                // Press on the hovered row: select it when it is both selectable and hoverable.
                unsigned const Row = HoveredRow;
                if (!RowEdges.empty() && Row < RowEdges.size() && Rows[Row].bSelectable != 0
                    && Rows[Row].bHoverable != 0)
                {
                    SelectedRow = Row;
                    bSelected = 1;
                }
                return true;
            }
            if (bDragArmed != 0)
            {
                // Press on an edge: take the capture and start dragging.
                Win3DPushCapture(this);
                bDragging = 1;
                return true;
            }
        }
        else if (Win3DGetCapture() == this && bDragging != 0)
        {
            bDragging = 0;
            Win3DRemoveCapture(this);
            return true;
        }
    }
    // 0x00599145: bubble to the parent's slot 19, false at the root.
    if (Parent != nullptr)
        return Parent->MouseEvent(Event);
    return false;
}

bool UiTable::HitTest(float const* Ray)
{
    // LAB_00599160: the given ray on the table plane, in table space, inside
    // [0, Width) x (-Height, 0].
    FVector const Local = UiTableRayToLocal(this, Ray);
    if (!(Local.X >= 0.0f))
        return false;
    if (!(Local.X < Width))
        return false;
    if (!(Local.Y <= 0.0f))
        return false;
    if (!(-Height < Local.Y))
        return false;
    return true;
}
