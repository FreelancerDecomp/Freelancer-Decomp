// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail CargoBarControl -- see cargo_bar_control.h for the class, its vftable and layout.

#include "cargo_bar_control.h"

#include "ids_strings.h"      // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "nn_ui_pulse.h"      // G_HudChromeColorRgba (DAT_00679bb4)
#include "star_sphere.h"      // Win3DBindModelview (FUN_00422690)
#include "tooltip_hover.h"    // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "win3d_engine_submit.h" // Win3DRayHitsAabbHalfExtents (FUN_005a1130)
#include "win3d_immediate.h"
#include "win_factory.h"

#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_ini.h"
#include "x86math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Retail WINFACTORY<CargoBarControl> "CargoBarControl" (ctor FUN_0055fcb0 from the static
// initializer at 0x0055e90a; Create FUN_0055fd20).
WINFACTORY<CargoBarControl> G_CargoBarControlFactory("CargoBarControl");

namespace
{
    // DAT_00679b4c -- SetOrientation's lazily acquired 3DMathEngine (FUN_005b73e0, 0x0055f612).
    I3DMathEngine* G_CargoBarMath = nullptr;

    void SetIdentity9(float* M)
    {
        M[0] = 1.0f;
        M[1] = 0.0f;
        M[2] = 0.0f;
        M[3] = 0.0f;
        M[4] = 1.0f;
        M[5] = 0.0f;
        M[6] = 0.0f;
        M[7] = 0.0f;
        M[8] = 1.0f;
    }
} // namespace

// The hover record Update builds on its stack (0x0055f703..0x0055f738 and the two
// siblings): caption empty and Font::String constructed (FUN_004144b0) -- both done by the
// FToolTipHoverInfo constructor -- then key -1, IdsAlt 0, FUN_00413df0(null, -1) on the
// caption (a no-op on an empty caption), +0x44 = 0, +0x60 = 0. Retail leaves +0x48..+0x5f as
// stack garbage; the port's constructor zeroes them.
// NN_Inventory's SetProperty 0x4f builds the same record (0x00479dc4..0x00479dfd).
void ToolTipHoverInit(FToolTipHoverInfo* Info)
{
    Info->Ids = -1;
    Info->IdsAlt = 0;
    Info->AuxField = 0;
    Info->bLive = 0;
}

// FUN_004142a0(Length + 1) then wcsncpy + NUL (0x0055f75f..0x0055f788), the same as
// FUN_00413df0(Text, -1) (0x0055f943) on the empty caption.
void ToolTipHoverSetCaption(FToolTipHoverInfo* Info, wchar_t const* Text)
{
    unsigned const Length = static_cast<unsigned>(wcslen(Text));
    unsigned const Need = Length + 1;
    if (Need > Info->CaptionCapacity)
    {
        if (Info->CaptionW != nullptr)
        {
            Info->CaptionW = static_cast<wchar_t*>(realloc(Info->CaptionW, Need * sizeof(wchar_t)));
        }
        else
        {
            Info->CaptionW = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
            Info->CaptionW[0] = L'\0';
        }
        Info->CaptionCapacity = Need;
    }
    wcsncpy(Info->CaptionW, Text, Length);
    Info->CaptionW[Length] = L'\0';
}

// FUN_00444ee0 (the record's destructor, 0x00444ee0..0x00444f8f; inlined at
// 0x0055f7bd..0x0055f7ee / 0x0055f985..0x0055f9d5): members in reverse -- the Font::String at
// +0x10 (FUN_00444e50 inlined: FUN_004144f0, the child vector, the text buffer), then the
// caption buffer at +0x08 (FUN_00444e20 inlined: free, +0x08 / +0x0c zeroed). Runs as
// ~FToolTipHoverInfo (tooltip_hover.h), so the stack records below need no teardown call.
void ToolTipHoverDestroy(FToolTipHoverInfo* Info)
{
    FontStringDestroy(&Info->Text);
    if (Info->CaptionW != nullptr)
    {
        free(Info->CaptionW);
        Info->CaptionW = nullptr;
        Info->CaptionCapacity = 0;
    }
}

namespace
{

    // One filled quad in the current colour: (X0, Y0) .. (X0 + Width, Y0 + Height), z = 0, the
    // vertex order of every quad in Draw (FUN_00421670(8), four FUN_00421cc0, FUN_00421690).
    void DrawBarQuad(float X0, float Y0, float Width, float Height)
    {
        Win3DBegin(kWin3DQuads);
        float const X1 = X0 + Width;
        float const Y1 = Y0 + Height;
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd();
    }
}

CargoBarControl::CargoBarControl()
{
    // FUN_0055e930, in store order.
    Color[3] = 0;
    Color[2] = 0;
    Color[1] = 0;
    Color[0] = 0;
    HighlightColor[3] = 0;
    HighlightColor[2] = 0;
    HighlightColor[1] = 0;
    HighlightColor[0] = 0;
    Capacity = 0.0f;
    Used = 0.0f;
    TailSegment = 0.0f;
    HeadSegment = 0.0f;
    bHorizontal = 0;
    bHovered = 0;
    Field34b = 0;
    bToolTip = 0;
    ToolTip2Ids = 0;
    bBorder = 1;
    bShow = 1;
    // 0055e9bb..0055e9cf: after the vptrs, the segment colour becomes 0xff,0xff,0,0xff.
    HighlightColor[2] = 0;
    HighlightColor[0] = 0xff;
    HighlightColor[1] = 0xff;
    HighlightColor[3] = 0xff;
    Pad34e[0] = 0;
    Pad34e[1] = 0;
}

int CargoBarControl::GetHitPriority()
{
    // LAB_0055e9e0: MOV EAX,0x64 / RET.
    return 0x64;
}

void CargoBarControl::RefreshAttachment()
{
    // Slot 35 is FUN_00401e90 (RET): the bar does not re-attach to a hardpoint.
}

void CargoBarControl::GetAttachOrientation(float* OutOrientation9)
{
    // LAB_0055ea20 (RET 4): the attach orientation +0x2f8 while the parent is a WinControl,
    // else identity. The out pointer is also the return value (EAX); the port's slot is void.
    float Identity[9];
    SetIdentity9(Identity);
    float const* Source = Identity;
    if (Parent != nullptr && Parent->AsWinControl() != nullptr)
        Source = AttachOrientation;
    memcpy(OutOrientation9, Source, 9 * sizeof(float));
}

void CargoBarControl::GetAttachPosition(float* OutPosition3)
{
    // FUN_0055eaf0 (RET 4): +0x31c while the parent is a WinControl, else zero.
    if (Parent != nullptr && Parent->AsWinControl() != nullptr)
    {
        OutPosition3[0] = AttachPosition[0];
        OutPosition3[1] = AttachPosition[1];
        OutPosition3[2] = AttachPosition[2];
        return;
    }
    OutPosition3[0] = 0.0f;
    OutPosition3[1] = 0.0f;
    OutPosition3[2] = 0.0f;
}

bool CargoBarControl::Create(Win3D* NewParent, void* Data)
{
    // LAB_0055eb40 (RET 8): Win3D's Create (FUN_004b5060(NewParent, 0)), then the INI keys.
    bool const bResult = Win3D::Create(NewParent, nullptr);
    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini == nullptr || !bResult)
        return bResult;

    // 0055eb6e / 0055ee54: read_value at the top and at the bottom of the loop.
    while (Ini->read_value())
    {
        if (Ini->is_value("mesh"))
        {
            // 0055eb93: slot 38 (get_value_string(), 0).
            SetMesh(Ini->get_value_string(), 0);
        }
        // 0055eba9: "offset" is tested even after "mesh" matched.
        if (Ini->is_value("offset"))
        {
            float const Y = static_cast<float>(Ini->get_value_float(1));
            float const X = static_cast<float>(Ini->get_value_float(0));
            float const Z = static_cast<float>(Ini->get_value_float(2));
            Offset[0] = X;
            Offset[1] = Y;
            Offset[2] = Z;
            continue;
        }
        if (Ini->is_value("hardpoint"))
        {
            // 0055ec12..0055ec5b: identity attach orientation (FUN_004215b0), zero attach
            // position, then the NEW parent's slot 36 (value, &+0x2f8). Neither +0x1d0 nor
            // +0x328 is written.
            SetIdentity9(AttachOrientation);
            AttachPosition[2] = 0.0f;
            AttachPosition[1] = 0.0f;
            AttachPosition[0] = 0.0f;
            if (NewParent == nullptr)
                continue;
            WinControl* const ParentControl = NewParent->AsWinControl();
            if (ParentControl == nullptr)
                continue;
            ParentControl->GetHardpointTransform(Ini->get_value_string(), AttachOrientation);
            continue;
        }
        if (Ini->is_value("mouse_size"))
        {
            float const Y = static_cast<float>(Ini->get_value_float(1));
            float const X = static_cast<float>(Ini->get_value_float(0));
            float const Z = static_cast<float>(Ini->get_value_float(2));
            HalfExtents[0] = X;
            HalfExtents[1] = Y;
            HalfExtents[2] = Z;
            continue;
        }
        if (Ini->is_value("horizontal"))
        {
            // 0055ecd9: FCOMP 0.0 (DAT_005c8ba8), TEST AH,0x44 / JNP -> equal gives 0.
            bHorizontal = (Ini->get_value_float(0) != 0.0) ? 1 : 0;
            continue;
        }
        if (Ini->is_value("tooltip"))
        {
            bToolTip = (Ini->get_value_float(0) != 0.0) ? 1 : 0;
            continue;
        }
        if (Ini->is_value("tooltip2"))
        {
            ToolTip2Ids = static_cast<int>(Ini->get_value_float(0)); // ftol
            bToolTip = 1;
            continue;
        }
        if (Ini->is_value("color"))
        {
            // 0055ed7b..0055eddf: components 2, 1, 0 are read first, then 3; each is ftol'd to
            // a byte of the dword stored at +0x340.
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char Rgba[4];
            Rgba[0] = static_cast<unsigned char>(static_cast<int>(V0));
            Rgba[1] = static_cast<unsigned char>(static_cast<int>(V1));
            Rgba[2] = static_cast<unsigned char>(static_cast<int>(V2));
            Rgba[3] = static_cast<unsigned char>(static_cast<int>(Ini->get_value_float(3)));
            memcpy(Color, Rgba, sizeof(Color));
            continue;
        }
        if (Ini->is_value("color_highlight"))
        {
            // 0055ede1..0055ee4c: the same into +0x344.
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char Rgba[4];
            Rgba[0] = static_cast<unsigned char>(static_cast<int>(V0));
            Rgba[1] = static_cast<unsigned char>(static_cast<int>(V1));
            Rgba[2] = static_cast<unsigned char>(static_cast<int>(V2));
            Rgba[3] = static_cast<unsigned char>(static_cast<int>(Ini->get_value_float(3)));
            memcpy(HighlightColor, Rgba, sizeof(HighlightColor));
        }
    }
    return bResult;
}

void CargoBarControl::Init(char const* MeshPath, float const* NewOffset, char const* Hardpoint,
                           float const* NewHalfExtents, unsigned char bNewHorizontal,
                           int NewToolTip2Ids, unsigned NewColor, unsigned NewHighlightColor)
{
    // FUN_0055ee80 (RET 0x20).
    SetMesh(MeshPath, 0);               // slot 38 (MeshPath, 0)
    Offset[0] = NewOffset[0];           // +0xa8
    Offset[1] = NewOffset[1];
    Offset[2] = NewOffset[2];
    if (Hardpoint != nullptr)
    {
        // 0055eeba..0055eefe: identity + zero attach, then the parent's slot 36.
        SetIdentity9(AttachOrientation);
        AttachPosition[2] = 0.0f;
        AttachPosition[1] = 0.0f;
        AttachPosition[0] = 0.0f;
        if (Parent != nullptr)
        {
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl != nullptr)
                ParentControl->GetHardpointTransform(Hardpoint, AttachOrientation);
        }
    }
    HalfExtents[0] = NewHalfExtents[0]; // +0xb4
    HalfExtents[1] = NewHalfExtents[1];
    HalfExtents[2] = NewHalfExtents[2];
    bHorizontal = bNewHorizontal;       // +0x348
    ToolTip2Ids = NewToolTip2Ids;       // +0x32c
    bToolTip = (NewToolTip2Ids != 0) ? 1 : 0; // +0x34a
    memcpy(Color, &NewColor, sizeof(Color));                    // +0x340
    memcpy(HighlightColor, &NewHighlightColor, sizeof(HighlightColor)); // +0x344
}

void CargoBarControl::Draw()
{
    // LAB_0055ef60.
    if (bShow == 0)
        return;

    WinControl::Draw(); // FUN_005a0760

    // 0055ef7c..0055efd0: model-view = orientation with position + offset.
    float Matrix[12];
    memcpy(Matrix, Orientation, 9 * sizeof(float));
    Matrix[9] = Position[0] + Offset[0];
    Matrix[10] = Position[1] + Offset[1];
    Matrix[11] = Position[2] + Offset[2];
    Win3DBindModelview(nullptr, Matrix);
    Win3DDisable(kWin3DTexture2D); // FUN_00421ed0(0xde1)
    Win3DDisable(kWin3DDepthTest); // FUN_00421ed0(0xb71)

    unsigned char Chrome[4];
    memcpy(Chrome, G_HudChromeColorRgba, sizeof(Chrome)); // DAT_00679bb4
    unsigned char Segment[4];
    memcpy(Segment, HighlightColor, sizeof(Segment));

    // 0055efe9: FCOMP 0.0, TEST AH,0x44 / JNP -> nothing but the border when Capacity == 0.
    if (Capacity != 0.0f)
    {
        float const Width2 = HalfExtents[0] + HalfExtents[0];  // +0xb4 doubled
        float const Height2 = HalfExtents[1] + HalfExtents[1]; // +0xb8 doubled

        if (bHorizontal != 0)
        {
            if (Width2 != 0.0f)
            {
                // 0055f052..0055f10c: the head segment from the left edge.
                float HeadLength = 0.0f;
                if (HeadSegment != 0.0f)
                {
                    HeadLength = HeadSegment / Capacity * Width2;
                    Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                    DrawBarQuad(-HalfExtents[0], -HalfExtents[1], HeadLength, Height2);
                }

                // 0055f111..0055f1e3: Used and the tail segment, which ends at Used.
                double const Inverse = 1.0 / Capacity;
                float const UsedLength = static_cast<float>(Inverse * Used * Width2);
                float TailLength = 0.0f;
                if (TailSegment != 0.0f)
                {
                    TailLength = static_cast<float>(Inverse * TailSegment * Width2);
                    Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                    float const TailStart = UsedLength - HalfExtents[0] - TailLength;
                    DrawBarQuad(TailStart, -HalfExtents[1], TailLength, Height2);
                }

                // 0055f1ec..0055f4d1: the fill between the two segments in +0x340.
                float const FillLength = UsedLength - HeadLength - TailLength;
                Win3DColor4ub(Color[0], Color[1], Color[2], Color[3]);
                DrawBarQuad(HeadLength - HalfExtents[0], -HalfExtents[1], FillLength, Height2);
            }
        }
        else if (Height2 != 0.0f)
        {
            // 0055f283..0055f4d1: the same bottom-up along y, full width across.
            float HeadLength = 0.0f;
            if (HeadSegment != 0.0f)
            {
                HeadLength = HeadSegment / Capacity * Height2;
                Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                DrawBarQuad(-HalfExtents[0], -HalfExtents[1], Width2, HeadLength);
            }

            double const Inverse = 1.0 / Capacity;
            float const UsedLength = static_cast<float>(Inverse * Used * Height2);
            float TailLength = 0.0f;
            if (TailSegment != 0.0f)
            {
                TailLength = static_cast<float>(Inverse * TailSegment * Height2);
                Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                float const TailStart = UsedLength - HalfExtents[1] - TailLength;
                DrawBarQuad(-HalfExtents[0], TailStart, Width2, TailLength);
            }

            float const FillLength = UsedLength - HeadLength - TailLength;
            Win3DColor4ub(Color[0], Color[1], Color[2], Color[3]);
            DrawBarQuad(-HalfExtents[0], HeadLength - HalfExtents[1], Width2, FillLength);
        }
    }

    // 0055f4d6..0055f57d: the border, a line loop in the HUD chrome colour.
    if (bBorder != 0)
    {
        Win3DColor4ub(Chrome[0], Chrome[1], Chrome[2], Chrome[3]);
        Win3DBegin(kWin3DLineLoop);
        Win3DVertex3f(-HalfExtents[0], -HalfExtents[1], 0.0f);
        Win3DVertex3f(HalfExtents[0], -HalfExtents[1], 0.0f);
        Win3DVertex3f(HalfExtents[0], HalfExtents[1], 0.0f);
        Win3DVertex3f(-HalfExtents[0], HalfExtents[1], 0.0f);
        Win3DEnd();
    }

    Slot39(); // 0055f586 CALL [EDX + 0x9c]
}

void CargoBarControl::SetPosition(float const* NewPosition)
{
    // LAB_0055f5a0 (RET 4): WinControl's FUN_005a0880 with the attach position added.
    float Attach[3];
    GetAttachPosition(Attach); // slot 41
    float Placed[3];
    Placed[0] = Attach[0] + NewPosition[0];
    Placed[1] = Attach[1] + NewPosition[1];
    Placed[2] = Attach[2] + NewPosition[2];
    WinControl::SetPosition(Placed);
}

void CargoBarControl::SetOrientation(float const* NewOrientation)
{
    // LAB_0055f5f0 (RET 4): MulMatrix(&Local, NewOrientation, attach orientation) through the
    // cached 3DMathEngine, then WinControl's FUN_005a0950 on a copy.
    float Attach[9];
    GetAttachOrientation(Attach); // slot 40
    if (G_CargoBarMath == nullptr)
        G_CargoBarMath = Acquire3DMathEngine();
    FMatrix Product;
    G_CargoBarMath->MulMatrix(Product, *reinterpret_cast<FMatrix const*>(NewOrientation),
                              *reinterpret_cast<FMatrix const*>(Attach));
    float Copy[9];
    memcpy(Copy, &Product, sizeof(Copy));
    WinControl::SetOrientation(Copy);
}

void CargoBarControl::Update()
{
    // LAB_0055f660.
    if (bToolTip != 0 && bHovered != 0)
    {
        if (ToolTip2Ids != 0)
        {
            // 0055f6a9..0055f7c2: "<IDS> used/capacity", keyed by this control.
            int const UsedCount = static_cast<int>(Used);      // ftol +0x334
            int const CapacityCount = static_cast<int>(Capacity); // ftol +0x330
            wchar_t Name[0x100];
            GetIdsStringWWithRawFallback(static_cast<unsigned>(ToolTip2Ids), Name, 0xff);
            wchar_t Text[0x100];
            swprintf(Text, 0x100, L"%s %d/%d", Name, UsedCount, CapacityCount);
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, Text);
            Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
            Info.IdsAlt = 0;
            NnToolTipFeedHover(&Info);
        }
        else if (ToolTipIds != 0)
        {
            // 0055f7f8..0055f891: the plain tooltip id, which is also the record's key.
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            Info.IdsAlt = ToolTipIds;
            Info.Ids = ToolTipIds;
            NnToolTipFeedHover(&Info);
        }
        else
        {
            // 0055f89e..0055f9d5: "used/capacity", keyed by this control.
            int const UsedCount = static_cast<int>(Used);
            int const CapacityCount = static_cast<int>(Capacity);
            wchar_t Text[0x100];
            swprintf(Text, 0x100, L"%d/%d", UsedCount, CapacityCount);
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, Text);
            Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
            Info.IdsAlt = 0;
            NnToolTipFeedHover(&Info);
        }
    }

    Win3D::Update(); // FUN_005a17b0
}

void CargoBarControl::MouseEnter(float const* Ray)
{
    // FUN_0055fa00 (RET 4): +0x349 = 1, then the parent WinControl hears 0x2c with this.
    (void)Ray;
    bHovered = 1;
    if (Parent == nullptr)
        return;
    WinControl* const ParentControl = Parent->AsWinControl();
    if (ParentControl == nullptr)
        return;
    ParentControl->SetProperty(0x2c, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
}

void CargoBarControl::MouseLeave(float const* Ray)
{
    // FUN_0055fa40 (RET 4): +0x349 = 0, then 0x2d to the parent.
    (void)Ray;
    bHovered = 0;
    if (Parent == nullptr)
        return;
    WinControl* const ParentControl = Parent->AsWinControl();
    if (ParentControl == nullptr)
        return;
    ParentControl->SetProperty(0x2d, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
}

bool CargoBarControl::HitTest(float const* Ray)
{
    // LAB_0055fa80 (RET 4): the +0xb4 box at orientation / position + offset (FUN_005a1130).
    // Unlike WinControl's FUN_005a0340 there is no instance transform, no hit offset and no
    // parent notification.
    float Matrix[12];
    memcpy(Matrix, Orientation, 9 * sizeof(float));
    Matrix[9] = Position[0] + Offset[0];
    Matrix[10] = Position[1] + Offset[1];
    Matrix[11] = Position[2] + Offset[2];
    return Win3DRayHitsAabbHalfExtents(HalfExtents, Matrix, Ray);
}

intptr_t CargoBarControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0055fb00 (RET 0xc): every case returns 0; nothing is forwarded to WinControl.
    switch (Msg)
    {
    case 1:
        // caseD_1: A ? hide : show.
        if (A != 0)
            Flags = static_cast<unsigned char>(Flags & 0xfc);
        else
            Flags = static_cast<unsigned char>(Flags | 3);
        break;
    case 2:
        bShow = (A == 0) ? 1 : 0;
        break;
    case 3:
        ToolTipIds = static_cast<int>(A);
        bToolTip = (A != 0) ? 1 : 0;
        break;
    case 0x19:
        bBorder = ((A & 0xffff0000u) != 0) ? 0 : 1;
        break;
    case 0x2c:
        Field34b = 1;
        break;
    case 0x2d:
        Field34b = 0;
        break;
    case 0x3f:
        memcpy(Color, &A, sizeof(Color));
        break;
    case 0x40:
        // FILD qword with a zero high dword: the arguments are unsigned.
        Capacity = static_cast<float>(static_cast<unsigned>(A));
        Used = static_cast<float>(static_cast<unsigned>(B));
        break;
    case 0x41:
        TailSegment = static_cast<float>(static_cast<unsigned>(A));
        HeadSegment = static_cast<float>(static_cast<unsigned>(B));
        break;
    default:
        break;
    }
    return 0;
}

WinControl* WinControl::CreateCargoBarControl(char const* Name, char const* MeshPath,
                                              float const* NewOffset, char const* Hardpoint,
                                              float const* NewHalfExtents,
                                              unsigned char bHorizontal, int ToolTip2Ids,
                                              unsigned Color, unsigned HighlightColor)
{
    // FUN_005a1080 (RET 0x24): FUN_0059db20(Name, "CargoBarControl", this, 0), then Init.
    Win3D* const Created = WinFactoryCreate(Name, "CargoBarControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<CargoBarControl*>(Created)->Init(MeshPath, NewOffset, Hardpoint,
                                                     NewHalfExtents, bHorizontal, ToolTip2Ids,
                                                     Color, HighlightColor);
    }
    return static_cast<WinControl*>(Created);
}
