// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rich_text_win.h"

#include "fl_ime_input_box.h"
#include "globals.h"
#include "hud_command_dispatch.h"
#include "ids_rdl.h"
#include "pixel_format.h"   // PixelFormatFromD3D / PixelFormatFind (FUN_005b7010 / 005b7050)
#include "screen_quad_state.h"   // ScreenQuadDrawBigImageTile (FUN_00404720)
#include "star_sphere.h"
#include "ui_page_desc.h"
#include "ui_scroll.h"
#include "win3d_immediate.h"
#include "win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "render/IBigImage2.h"
#include "render/IRenderPipeline.h"
#include "x86math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

namespace
{
    // "E:\FL\Scratch\Source\Client\Interface\RichDisplay.cpp" (.rdata 0x005e46a8).
    char const* const kRichDisplayCpp = "E:\\FL\\Scratch\\Source\\Client\\Interface\\RichDisplay.cpp";

    // DAT_0067c480 -- the lazily acquired 3DMathEngine (FUN_005b73e0) every RichDisplay function
    // tests and fills in place.
    I3DMathEngine* G_RichTextMathEngine = nullptr;

    I3DMathEngine* RichTextMathEngine()
    {
        if (G_RichTextMathEngine == nullptr)
            G_RichTextMathEngine = Acquire3DMathEngine();
        return G_RichTextMathEngine;
    }

    // DAT_00615174 -- "ScrollV_%d" serial, .data initial value 1.
    int G_ScrollVSerial = 1;

    // FUN_00579390 (thiscall on +0x3d4). The dtor (0x005796ed..0x00579748) and EnsureSurface
    // (0x0057a320..0x0057a36b) inline the same body.
    void ReleaseRichTextSurface(FRichTextSurface* Surface)
    {
        if (Surface->Dc != nullptr)
        {
            if (Surface->PreviousObject != nullptr)
                SelectObject(Surface->Dc, Surface->PreviousObject);
            if (Surface->Bitmap != nullptr)
                DeleteObject(Surface->Bitmap);
            DeleteDC(Surface->Dc);
        }
        Surface->Height = -1;
        Surface->Width = -1;
        Surface->PreviousObject = nullptr;
        Surface->Bitmap = nullptr;
        Surface->Dc = nullptr;
    }

    // 0x0057a274..0x0057a2f8, spelled out twice (width, then height): a negative size becomes 1,
    // zero stays 0, a power of two stays itself, anything else rounds up to the next one.
    int RoundSurfaceSize(int Size)
    {
        if (Size < 0)
            return 1;
        int Result = 1;
        int Rest = Size;
        if ((Size & (Size - 1)) == 0)
        {
            while (Rest != 0)
            {
                Rest >>= 1;
                Result <<= 1;
            }
            return Result >> 1;
        }
        while (Rest != 0)
        {
            Rest >>= 1;
            Result <<= 1;
        }
        return Result;
    }
}

// ---------------------------------------------------------------------------------------------
// BaseRichTextWin
// ---------------------------------------------------------------------------------------------

BaseRichTextWin::BaseRichTextWin()
{
    // FUN_005793e0. The WinControl part (FUN_0059f860), the two lists (FUN_0042a7e0 sentinels)
    // and the DeviceTRC (import 0x005c6c20) are the members' own constructors.
    Surface.Height = -1;
    Surface.Width = -1;
    Surface.Dc = nullptr;
    Surface.Bitmap = nullptr;
    Surface.PreviousObject = nullptr;
    Image = nullptr;

    // The hover record: caption buffer empty and the Font::String (FUN_004144b0) -- the member's
    // own ctor -- then key -1, FUN_00413df0(null, -1) on the caption (a no-op with no buffer)
    // and the live flag.
    HoverInfo.IdsAlt = 0;
    HoverInfo.Ids = -1;
    HoverInfo.AuxField = 0;
    HoverInfo.bLive = 0;

    DebugWin = nullptr;
    bDrawFrame = 0;
    bSkipUpload = 0;
    // DAT_00615170: 00 ff 00 ff.
    FrameColor[0] = 0x00;
    FrameColor[1] = 0xff;
    FrameColor[2] = 0x00;
    FrameColor[3] = 0xff;
    ScrollY = 0;
    ScrollX = 0;
    Trc.set_origin_v(0);
    Trc.set_origin_h(0);
    ScrollBarH = nullptr;
    ScrollBarV = nullptr;
    bAutoScrollbar = 0;
    ScrollBarWidth = 0.02f;   // 0x3ca3d70a
    ScrollBarGap = 0.002f;    // 0x3b03126f
    LayoutHeight = 0;
    Pitch = 0;
    AlignMode = 0;
    bNeverHit = 0;
    bToolTipEnabled = 0;

    // The hover record again (0x00579554..0x0057956f): key -1, FUN_00414280 empties the
    // caption (none yet), then the key becomes this control.
    HoverInfo.IdsAlt = 0;
    HoverInfo.Ids = -1;
    HoverInfo.AuxField = 0;
    if (HoverInfo.CaptionW != nullptr && HoverInfo.CaptionW[0] != L'\0')
        HoverInfo.CaptionW[0] = L'\0';
    HoverInfo.bLive = 0;
    HoverInfo.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
    HoverInfo.IdsAlt = 0;

    bKeepScrolledToEnd = 0;
    NormHeight = 0.0f;
    NormWidth = 0.0f;
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    Alpha = 1.0f;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

BaseRichTextWin::~BaseRichTextWin()
{
    // FUN_00579620. The DebugWindow branch (0x00579655..0x0057967f: DestroyWindow, delete) is
    // not ported: +0x32c is only ever written null (see the header), so it never runs.

    // 0x0057969d..0x005796cf: the +0x40c record's member dtor (FUN_00444ee0 inlined), the first
    // member to go. Called here because the port tears the members below it (+0x3ec Image, the
    // +0x3d4 surface) down by hand in this body; ~FToolTipHoverInfo then finds it empty.
    ToolTipHoverDestroy(&HoverInfo);

    if (Image != nullptr)
    {
        Image->Release();
        Image = nullptr;
    }

    ReleaseRichTextSurface(&Surface);

    // ~DeviceTRC (import 0x005c6c14), then the +0x374 and +0x368 lists, then WinControl
    // (FUN_0059fa50): the members' and the base's own destructors, in that order.
}

bool BaseRichTextWin::HitTest(float const* Ray)
{
    // LAB_0057aa80 (RET 4). DAT_005c7474 is 0.0f. The y of the ray's point is rounded to a float
    // (FSTP at 0x0057aaa0); the x difference stays on the FPU stack.
    if (bNeverHit != 0)
        return false;
    double const T = static_cast<double>(Position[2]) / Ray[2];
    float const PointY = static_cast<float>(T * Ray[1]);
    double const DeltaX = T * Ray[0] - Position[0];
    if (DeltaX < 0.0)
        return false;
    if (!(DeltaX <= static_cast<double>(WorldRect[2]) - WorldRect[0]))
        return false;
    double const DeltaY = static_cast<double>(Position[1]) - PointY;
    if (DeltaY < 0.0)
        return false;
    if (!(DeltaY <= static_cast<double>(WorldRect[1]) - WorldRect[3]))
        return false;
    return true;
}

void BaseRichTextWin::SetPosition(float const* NewPosition)
{
    // FUN_0057b0a0 (RET 4): the attach offset added, then WinControl's SetPosition
    // (FUN_005a0880) called directly.
    float Moved[3];
    Moved[0] = AttachOffset[0] + NewPosition[0];
    Moved[1] = AttachOffset[1] + NewPosition[1];
    Moved[2] = AttachOffset[2] + NewPosition[2];
    WinControl::SetPosition(Moved);
}

bool BaseRichTextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0057a810 (RET 8). Win3D's Create (FUN_004b5060) always gets a null payload.
    bool const bResult = Win3D::Create(NewParent, nullptr);
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
    NormWidth = 0.4f;   // 0x3ecccccd
    NormHeight = 0.3f;  // 0x3e99999a
    EnsureSurface();
    // 0x0057a89f..0x0057a9ad: the DebugWindow window (class "RichDisplayDebugWindow",
    // wndproc LAB_0057a9c0) -- dead, +0x32c is always null.
    return bResult;
}

void BaseRichTextWin::FeedToolTipIfHovered()
{
    // 0x0057b0e6..0x0057b122 (and the same bytes in 0x0057b146.., 0x0057b551..).
    if (bToolTipEnabled == 0)
        return;
    FToolTipHoverInfo* const Info = GetToolTipHoverInfo();
    if (Info == nullptr)
        return;
    float Ray[3];
    Win3DGetMouseRay(Ray);
    if (HitTest(Ray))
        NnToolTipFeedHover(Info);
}

void BaseRichTextWin::Update()
{
    // FUN_0057b0e0: the ToolTip feed, then Win3D's child walk (FUN_005a17b0).
    FeedToolTipIfHovered();
    Win3D::Update();
}

void BaseRichTextWin::UpdateInactive()
{
    // LAB_0057b140: the ToolTip feed, then slot 12 on every child whose bit 0 is clear. The next
    // sibling is read after the call.
    FeedToolTipIfHovered();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        if ((Child->Flags & 1) == 0)
            Child->UpdateInactive();
        Child = Child->NextSibling;
    }
}

void BaseRichTextWin::Draw()
{
    // FUN_0057ab10.
    if ((Flags & 2) == 0)
        return;

    EnsureSurface();

    if ((bSurfaceDirty != 0 || bLayoutDirty != 0) && PixelWidth > 0 && PixelHeight > 0)
    {
        SelectClipRgn(Surface.Dc, nullptr);
        RECT Fill;
        Fill.left = 0;
        Fill.top = 0;
        Fill.right = Surface.Width;
        Fill.bottom = Surface.Height;
        HBRUSH const Brush = CreateSolidBrush(0);
        FillRect(Surface.Dc, &Fill, Brush);
        if (Brush != nullptr)
            DeleteObject(Brush);

        if (ScrollBarV != nullptr)
        {
            ScrollBarV->SetPage(static_cast<float>(PixelHeight));
            ScrollBarV->SetStep(static_cast<float>(PixelHeight / 10));
        }

        RebuildLayoutIfDirty();

        switch (AlignMode)
        {
        case 1:
            Trc.set_origin_v(0);
            Trc.set_origin_h(-ScrollX);
            break;
        case 2:
            Trc.set_origin_v((PixelHeight - LayoutHeight) / 2);
            Trc.set_origin_h(-ScrollX);
            break;
        case 3:
            Trc.set_origin_v(PixelHeight - LayoutHeight);
            Trc.set_origin_h(-ScrollX);
            break;
        default:
            break;
        }

        Trc.reset_to_default();
        Trc.set_pos_v(0);
        Trc.set_pos_h(0);
        LayoutList.execute(Trc, true);

        // Read the 16-bit DIB back out of the bitmap: deselect it, GetDIBits, reselect it.
        unsigned int const BufferBytes = static_cast<unsigned int>(Pitch * Surface.Height);
        unsigned char* const Buffer = static_cast<unsigned char*>(operator new(BufferBytes));
        struct FDibInfo
        {
            BITMAPINFOHEADER Header;
            RGBQUAD Colors[1];
        };
        FDibInfo Info;
        memset(&Info, 0, sizeof(Info));
        Info.Header.biSize = 0x28;
        Info.Header.biWidth = Surface.Width;
        Info.Header.biHeight = -Surface.Height;
        Info.Header.biPlanes = 1;
        Info.Header.biBitCount = 0x10;
        Info.Header.biSizeImage = BufferBytes;
        SelectObject(Surface.Dc, Surface.PreviousObject);
        int const Lines = GetDIBits(Surface.Dc, Surface.Bitmap, 0,
                                    static_cast<UINT>(Surface.Height), Buffer,
                                    reinterpret_cast<BITMAPINFO*>(&Info), 0);
        SelectObject(Surface.Dc, Surface.Bitmap);

        // Every drawn (non-black) pixel gets the A1 bit.
        if (Lines != 0)
        {
            unsigned char* Row = Buffer;
            for (int Y = 0; Y < Surface.Height; ++Y)
            {
                unsigned short* const Pixels = reinterpret_cast<unsigned short*>(Row);
                for (int X = 0; X < Surface.Width; ++X)
                {
                    if (Pixels[X] != 0)
                        Pixels[X] = static_cast<unsigned short>(Pixels[X] | 0x8000);
                }
                Row += Pitch;
            }
        }

        if (bSkipUpload == 0 && Image != nullptr)
        {
            // 0x0057ad9a..0x0057adb6: FUN_005b7010(0) seeds the id, FUN_005b7050 (thiscall on it)
            // looks up 16 bpp, 5:5:5 colour, 1-bit alpha (A1R5G5B5); the result is not tested.
            unsigned long Format = static_cast<unsigned long>(PixelFormatFromD3D(0));
            PixelFormatFind(&Format, 0x10, 5, 5, 5, 1);
            GENRESULT const Result = Image->SetFromBuffer(
                Buffer, &Format, static_cast<unsigned long>(Surface.Width),
                static_cast<unsigned long>(Surface.Height), static_cast<unsigned long>(Pitch));
            if (Result == 0)
                bSurfaceDirty = 0;
            else
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x5a1,
                      "Failed to set BigImage from a buffer.\n");
        }
        operator delete(Buffer);
    }

    DrawStaticMesh();

    if (Image != nullptr)
    {
        Image->SetAlpha(Alpha);
        float const White[3] = {1.0f, 1.0f, 1.0f};
        Image->SetColor(White);
        RECT Src;
        Src.left = 0;
        Src.top = 0;
        Src.right = ScreenRect[2] - ScreenRect[0] + 1;
        Src.bottom = ScreenRect[3] - ScreenRect[1] + 1;
        RECT Dest;
        Dest.left = ScreenRect[0];
        Dest.top = ScreenRect[1];
        Dest.right = ScreenRect[2] + 1;
        Dest.bottom = ScreenRect[3] + 1;
        Image->Draw(&Src, &Dest);

        if (bDrawFrame != 0)
        {
            Win3DDisable(kWin3DDepthTest);
            Win3DDisable(kWin3DCullFace);
            DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
            DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
            Win3DBindModelview(nullptr, Orientation);
            Win3DColor4ubv(FrameColor);
            Win3DDisable(kWin3DTexture2D);
            Win3DBegin(kWin3DLineLoop);
            Win3DVertex3f(0.0f, 0.0f, 0.0f);
            Win3DVertex3f(NormWidth, 0.0f, 0.0f);
            Win3DVertex3f(NormWidth, -NormHeight, 0.0f);
            Win3DVertex3f(0.0f, -NormHeight, 0.0f);
            Win3DEnd();
            Win3DEnable(kWin3DDepthTest);
            Win3DSetDepthWrite(1);
        }
    }

    Win3DDrawChildren(this);
    Slot39();
}

bool BaseRichTextWin::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0057afd0 (RET 0xc). 0xf005 is a scrollbar's value change; A is the sender.
    if (Msg == 0xf005)
    {
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBarV)))
        {
            int const OriginX = ScrollX;
            SetScrollOrigin(OriginX, static_cast<int>(ScrollBarV->GetValue()));
            return true;
        }
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBarH)))
        {
            int OriginY;
            switch (AlignMode)
            {
            case 0:
                OriginY = ScrollY;
                break;
            case 1:
                OriginY = 0;
                break;
            case 2:
                OriginY = -((PixelHeight - LayoutHeight) / 2);
                break;
            case 3:
                OriginY = LayoutHeight - PixelHeight;
                break;
            default:
                // 0x0057b05c: the message id itself.
                OriginY = static_cast<int>(Msg);
                break;
            }
            SetScrollOrigin(static_cast<int>(ScrollBarH->GetValue()), OriginY);
            return true;
        }
    }
    return WinControl::Slot23(Msg, A, B);
}

intptr_t BaseRichTextWin::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0057ebb0 (RET 0xc): property 6 is SetAttachOffset (FUN_00579c40) inlined, result 1.
    if (Msg != 6)
        return WinControl::SetProperty(Msg, A, B);
    SetAttachOffset(reinterpret_cast<float const*>(A));
    return 1;
}

bool BaseRichTextWin::Relayout()
{
    // FUN_004b6c50: MOV AL,1 / RET.
    return true;
}

FToolTipHoverInfo* BaseRichTextWin::GetToolTipHoverInfo()
{
    // LAB_005798a0.
    return &HoverInfo;
}

void BaseRichTextWin::SetDrawFrame(bool bDraw)
{
    bDrawFrame = bDraw ? 1 : 0;
}

void BaseRichTextWin::SetFrameColor(unsigned long const* Rgba)
{
    memcpy(FrameColor, Rgba, 4);
}

void BaseRichTextWin::SetNeverHit(bool bNever)
{
    bNeverHit = bNever ? 1 : 0;
}

void BaseRichTextWin::SetToolTipEnabled(bool bEnabled)
{
    bToolTipEnabled = bEnabled ? 1 : 0;
}

void BaseRichTextWin::SetScrollOrigin(int NewScrollX, int NewScrollY)
{
    // FUN_005798b0 (RET 8): a coordinate moves only to a new, non-negative value.
    bool bChanged = false;
    if (NewScrollX != ScrollX && NewScrollX >= 0)
    {
        ScrollX = NewScrollX;
        bChanged = true;
    }
    if (NewScrollY != ScrollY && NewScrollY >= 0)
        ScrollY = NewScrollY;
    else if (!bChanged)
        return;

    Trc.set_origin_v(-ScrollY);
    Trc.set_origin_h(-ScrollX);
    bLayoutDirty = bLayoutDirty != 0 ? 1 : 0;
    bSurfaceDirty = 1;
    if (ScrollBarV != nullptr)
        ScrollBarV->SetValue(static_cast<float>(ScrollY));
    if (ScrollBarH != nullptr)
        ScrollBarH->SetValue(static_cast<float>(ScrollX));
}

void BaseRichTextWin::GetScrollOrigin(int* OutScrollX, int* OutScrollY)
{
    // FUN_00579970 (RET 8): y as the alignment reports it; mode > 3 leaves it untouched.
    *OutScrollX = ScrollX;
    switch (AlignMode)
    {
    case 0:
        *OutScrollY = ScrollY;
        break;
    case 1:
        *OutScrollY = 0;
        break;
    case 2:
        *OutScrollY = -((PixelHeight - LayoutHeight) / 2);
        break;
    case 3:
        *OutScrollY = LayoutHeight - PixelHeight;
        break;
    default:
        break;
    }
}

void BaseRichTextWin::SetAutoScrollbar(bool bAuto)
{
    // FUN_005799f0.
    bAutoScrollbar = bAuto ? 1 : 0;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void BaseRichTextWin::ScrollToEnd()
{
    // FUN_00579a10.
    if (LayoutHeight > PixelHeight)
        SetScrollOrigin(ScrollX, LayoutHeight - PixelHeight);
    else
        SetScrollOrigin(ScrollX, 0);
}

void BaseRichTextWin::ScrollToTop()
{
    // FUN_00579a40.
    SetScrollOrigin(ScrollX, 0);
}

void BaseRichTextWin::SetKeepScrolledToEnd(bool bKeep)
{
    // FUN_00579a50.
    bKeepScrolledToEnd = bKeep ? 1 : 0;
}

void BaseRichTextWin::SetAlignMode(int Mode)
{
    // FUN_00579a60: the layout flag is only normalised, the surface flag raised.
    AlignMode = Mode;
    bLayoutDirty = bLayoutDirty != 0 ? 1 : 0;
    bSurfaceDirty = 1;
}

void BaseRichTextWin::SetSkipUpload(bool bSkip)
{
    // FUN_00579a90.
    bSkipUpload = bSkip ? 1 : 0;
}

UiScroll* BaseRichTextWin::CreateScrollBarV()
{
    // FUN_00579aa0. The payload is the UIScroll descriptor (width, height, min, max, page, step,
    // horizontal flag).
    FUiPayloadScrollCycle Payload;
    Payload.Right = ScrollBarWidth;
    Payload.Bottom = NormHeight;
    Payload.Extra0 = 0.0f;
    int const Range = LayoutHeight > PixelHeight ? LayoutHeight : PixelHeight;
    Payload.Extra1 = static_cast<float>(Range);
    Payload.Extra2 = static_cast<float>(PixelHeight);
    Payload.Extra3 = static_cast<float>(PixelHeight / 10);
    Payload.Flag = 0;

    char Name[0x2000];
    int const Serial = G_ScrollVSerial;
    G_ScrollVSerial = Serial + 1;
    sprintf(Name, "ScrollV_%d", Serial);

    UiScroll* const Created =
        static_cast<UiScroll*>(WinFactoryCreate(Name, "UIScroll", this, &Payload));
    if (Created != nullptr)
    {
        // Beside the text: the gap plus the width along the control's x axis.
        FVector Along;
        Along.X = ScrollBarGap + NormWidth;
        Along.Y = 0.0f;
        Along.Z = 0.0f;
        FVector Offset;
        RichTextMathEngine()->TransformMatrix(Offset, *reinterpret_cast<FMatrix const*>(Orientation),
                                              Along);
        Created->SetOffset(Offset.X, Offset.Y, Offset.Z);
        Created->SetTransform(Orientation);
    }
    return Created;
}

void BaseRichTextWin::ShowScrollBarV(bool bShow)
{
    // FUN_00579bd0 (RET 4).
    if (bShow)
    {
        if (ScrollBarV != nullptr)
            return;
        ScrollBarV = CreateScrollBarV();
        if (ScrollBarV != nullptr)
            ScrollBarV->SetValue(static_cast<float>(ScrollY));
        return;
    }
    if (ScrollBarV != nullptr)
    {
        delete ScrollBarV;
        ScrollBarV = nullptr;
    }
}

float BaseRichTextWin::GetScrollBarGap()
{
    // FUN_00579c30.
    return ScrollBarGap;
}

void BaseRichTextWin::SetAttachOffset(float const* NewOffset)
{
    // FUN_00579c40 (RET 4): move by the change of the offset. x and y stay on the FPU stack
    // between the subtraction and the addition; z is rounded to a float in between.
    float const Old[3] = {AttachOffset[0], AttachOffset[1], AttachOffset[2]};
    AttachOffset[0] = NewOffset[0];
    AttachOffset[1] = NewOffset[1];
    AttachOffset[2] = NewOffset[2];
    float Moved[3];
    float const DeltaZ = Position[2] - Old[2];
    Moved[0] = static_cast<float>(static_cast<double>(Position[0]) - Old[0] + AttachOffset[0]);
    Moved[1] = static_cast<float>(static_cast<double>(Position[1]) - Old[1] + AttachOffset[1]);
    Moved[2] = DeltaZ + AttachOffset[2];
    WinControl::SetPosition(Moved);
}

void BaseRichTextWin::SetNormSize(float Width, float Height)
{
    // FUN_00579cc0 (RET 8). No dirty flags: the next Draw's EnsureSurface notices the new size.
    NormHeight = Height;
    NormWidth = Width;
    if (ScrollBarV != nullptr)
    {
        ScrollBarV->SetSize(ScrollBarWidth, Height);
        FTransform Beside;
        memset(&Beside, 0, sizeof(Beside));
        Beside.Rotation.D[0][0] = 1.0f;
        Beside.Rotation.D[1][1] = 1.0f;
        Beside.Rotation.D[2][2] = 1.0f;
        Beside.Translation.X = NormWidth;
        FTransform Placed;
        RichTextMathEngine()->MulTransform(Placed, *reinterpret_cast<FTransform const*>(Orientation),
                                           Beside);
        ScrollBarV->SetTransform(reinterpret_cast<float const*>(&Placed));
    }
    if (ScrollBarH != nullptr)
    {
        ScrollBarH->SetSize(NormWidth, ScrollBarWidth);
        FTransform Below;
        memset(&Below, 0, sizeof(Below));
        Below.Rotation.D[0][0] = 1.0f;
        Below.Rotation.D[1][1] = 1.0f;
        Below.Rotation.D[2][2] = 1.0f;
        Below.Translation.Y = NormHeight;
        FTransform Placed;
        RichTextMathEngine()->MulTransform(Placed, *reinterpret_cast<FTransform const*>(Orientation),
                                           Below);
        ScrollBarH->SetTransform(reinterpret_cast<float const*>(&Placed));
    }
}

void BaseRichTextWin::EnsureSurface()
{
    // FUN_00579e90.
    int ViewX = 0;
    int ViewY = 0;
    int ViewW = 0;
    int ViewH = 0;
    DALib::Pipeline->GetViewportRect(&ViewX, &ViewY, &ViewW, &ViewH);
    float M[16];
    DALib::Pipeline->GetProjection(M);

    FTransform const& Xform = *reinterpret_cast<FTransform const*>(Orientation);

    // The top-left corner (0, 0, 0) into world space, then through the projection, rows summed
    // z, y, x, then the constant (0x00579f4f..0x00579fce).
    FVector Corner0;
    Corner0.X = 0.0f;
    Corner0.Y = 0.0f;
    Corner0.Z = 0.0f;
    FVector World0;
    RichTextMathEngine()->TransformXform(World0, Xform, Corner0);
    float Clip0[4];
    for (int Row = 0; Row < 4; ++Row)
    {
        double Sum = static_cast<double>(M[Row * 4 + 2]) * World0.Z;
        Sum += static_cast<double>(M[Row * 4 + 1]) * World0.Y;
        Sum += static_cast<double>(M[Row * 4 + 0]) * World0.X;
        Sum += M[Row * 4 + 3];
        Clip0[Row] = static_cast<float>(Sum);
    }
    if (Clip0[3] == 0.0f)
        return;
    double const InvW0 = 1.0 / Clip0[3];
    float const NdcX0 = static_cast<float>(Clip0[0] * InvW0);
    float const NdcY0 = static_cast<float>(InvW0 * Clip0[1]);

    // The bottom-right corner (w, -h, 0). The w row sums y, x, z here (0x0057a086..0x0057a0a5);
    // the other rows z, y, x as before.
    FVector Corner1;
    Corner1.X = NormWidth;
    Corner1.Y = -NormHeight;
    Corner1.Z = 0.0f;
    FVector World1;
    RichTextMathEngine()->TransformXform(World1, Xform, Corner1);
    float Clip1[4];
    for (int Row = 0; Row < 3; ++Row)
    {
        double Sum = static_cast<double>(M[Row * 4 + 2]) * World1.Z;
        Sum += static_cast<double>(M[Row * 4 + 1]) * World1.Y;
        Sum += static_cast<double>(M[Row * 4 + 0]) * World1.X;
        Sum += M[Row * 4 + 3];
        Clip1[Row] = static_cast<float>(Sum);
    }
    {
        double Sum = static_cast<double>(M[13]) * World1.Y;
        Sum += static_cast<double>(M[12]) * World1.X;
        Sum += static_cast<double>(M[14]) * World1.Z;
        Sum += M[15];
        Clip1[3] = static_cast<float>(Sum);
    }
    if (Clip1[3] == 0.0f)
        return;
    double const InvW1 = 1.0 / Clip1[3];

    WorldRect[0] = World0.X;
    WorldRect[1] = World0.Y;
    WorldRect[2] = World1.X;
    WorldRect[3] = World1.Y;

    double const NdcX1 = Clip1[0] * InvW1;
    float const NdcY1 = static_cast<float>(InvW1 * Clip1[1]);

    // DAT_005c75dc = 1.0f, DAT_005c75e0 = 0.5f. MSVCRT ftol truncates.
    double const WidthD = static_cast<double>(ViewW);
    double const XD = static_cast<double>(ViewX);
    float const HeightF = static_cast<float>(ViewH);
    float const YF = static_cast<float>(ViewY);
    int const Left = static_cast<int>((1.0 - NdcX0) * WidthD * 0.5 + XD);
    ScreenRect[0] = Left;
    int const Top = static_cast<int>((static_cast<double>(NdcY0) + 1.0) * HeightF * 0.5 + YF);
    ScreenRect[1] = Top;
    int const Right = static_cast<int>((1.0 - NdcX1) * WidthD * 0.5 + XD);
    ScreenRect[2] = Right;
    int const Bottom = static_cast<int>((static_cast<double>(NdcY1) + 1.0) * HeightF * 0.5 + YF);

    int const OldPixelWidth = PixelWidth;
    int const OldPixelHeight = PixelHeight;
    ScreenRect[3] = Bottom;
    PixelWidth = Right - Left;
    PixelHeight = Bottom - Top;

    int const SurfaceW = RoundSurfaceSize(PixelWidth);
    int const SurfaceH = RoundSurfaceSize(PixelHeight);
    if (SurfaceH > 0 && SurfaceW > 0 && (Surface.Height != SurfaceH || Surface.Width != SurfaceW))
    {
        // The FUN_00579390 body inline, then FUN_00579390 itself once the size differs from the
        // -1 it just wrote (always, since both sizes are positive).
        ReleaseRichTextSurface(&Surface);
        if (SurfaceW != -1 || SurfaceH != -1)
        {
            ReleaseRichTextSurface(&Surface);
            Surface.Width = SurfaceW;
            Surface.Height = SurfaceH;
            HDC const ScreenDc = GetDC(nullptr);
            Surface.Dc = CreateCompatibleDC(ScreenDc);
            if (Surface.Dc != nullptr)
            {
                Surface.Bitmap = CreateCompatibleBitmap(ScreenDc, Surface.Width, Surface.Height);
                if (Surface.Bitmap != nullptr)
                    Surface.PreviousObject = SelectObject(Surface.Dc, Surface.Bitmap);
            }
            ReleaseDC(nullptr, ScreenDc);
        }

        Pitch = Surface.Width * 2;
        bSurfaceDirty = 1;
        if ((Pitch & 3) != 0)
            Pitch = Pitch - (Pitch & 3) + 4;

        unsigned int const BufferBytes = static_cast<unsigned int>(Pitch * Surface.Height);
        unsigned char* const Buffer = static_cast<unsigned char*>(operator new(BufferBytes));
        memset(Buffer, 0, BufferBytes);

        // 0x0057a415..0x0057a44f: both ids seeded with FUN_005b7010(0), then FUN_005b7050 (thiscall
        // on each) with 16 bpp, 5:5:5 colour, 1-bit alpha (A1R5G5B5); results not tested.
        unsigned long DstFormat = static_cast<unsigned long>(PixelFormatFromD3D(0)); // [esp+0x34]
        unsigned long SrcFormat = static_cast<unsigned long>(PixelFormatFromD3D(0)); // [esp+0x38]
        PixelFormatFind(&DstFormat, 0x10, 5, 5, 5, 1);
        PixelFormatFind(&SrcFormat, 0x10, 5, 5, 5, 1);
        FBigImageDesc Desc;
        Desc.Size = 0x2c;
        Desc.Name = "BigImage";
        Desc.Pipeline = DALib::Pipeline;
        Desc.ImageSource = nullptr;
        Desc.SrcFormat = &SrcFormat;
        Desc.DstFormat = &DstFormat;
        Desc.Pixels = Buffer;
        Desc.Width = static_cast<unsigned long>(Surface.Width);
        Desc.Height = static_cast<unsigned long>(Surface.Height);
        Desc.Pitch = static_cast<unsigned long>(Pitch);
        Desc.DrawTile = &ScreenQuadDrawBigImageTile;

        void* Instance = nullptr;
        if (DALib::Dacom->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance) == 0)
        {
            if (Image != nullptr)
            {
                Image->Release();
                Image = nullptr;
            }
            if (static_cast<IDAComponent*>(Instance)->QueryInterface(
                    Iids::BigImage2, reinterpret_cast<void**>(&Image)) != 0)
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x3c7,
                      "Failed to query IBigImage2 interface.\n");
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x3cc,
                  "Failed to create BigImage instance.\n");
        }
        operator delete(Buffer);
        if (Instance != nullptr)
        {
            static_cast<IDAComponent*>(Instance)->Release();
            Instance = nullptr;
        }
    }

    if (OldPixelWidth != PixelWidth || OldPixelHeight != PixelHeight)
    {
        bLayoutDirty = 1;
        bSurfaceDirty = 1;
    }
    Trc.bind_to_dc(Surface.Dc);
}

DeviceTRC* BaseRichTextWin::GetDeviceTRC()
{
    // FUN_0057a610.
    return &Trc;
}

void BaseRichTextWin::MarkDirty(bool bSurface, bool bLayout)
{
    // FUN_0057a620 (RET 8): a dirty layout always dirties the surface.
    bLayoutDirty = (bLayoutDirty != 0 || bLayout) ? 1 : 0;
    if (bLayoutDirty != 0)
    {
        bSurfaceDirty = 1;
        return;
    }
    bSurfaceDirty = (bSurfaceDirty != 0 || bSurface) ? 1 : 0;
}

void BaseRichTextWin::RebuildLayoutIfDirty()
{
    // FUN_0057a680.
    if (Surface.Dc == nullptr || bLayoutDirty == 0)
        return;

    Trc.reset_to_default();
    Trc.set_pos_v(0);
    Trc.set_pos_h(0);
    RdlClear(LayoutList);
    LayoutHeight = PixelHeight;
    Relayout();

    if (bAutoScrollbar != 0)
    {
        if (ScrollBarV != nullptr)
        {
            if (LayoutHeight <= PixelHeight)
            {
                delete ScrollBarV;
                ScrollBarV = nullptr;
            }
        }
        else if (LayoutHeight > PixelHeight)
        {
            ScrollBarV = CreateScrollBarV();
            if (ScrollBarV != nullptr)
                ScrollBarV->SetValue(static_cast<float>(ScrollY));
        }
    }

    if (ScrollBarV != nullptr)
    {
        int const Range = LayoutHeight > PixelHeight ? LayoutHeight : PixelHeight;
        ScrollBarV->SetRange(0.0f, static_cast<float>(Range));
    }

    if (bKeepScrolledToEnd != 0)
    {
        // DAT_005d3e4c = 60.0f: follow the end while the view is within 60 pixels of it.
        int const Overflow = LayoutHeight - PixelHeight;
        if (static_cast<double>(Overflow) - ScrollY <= 60.0)
        {
            if (LayoutHeight > PixelHeight)
                SetScrollOrigin(ScrollX, Overflow);
            else
                SetScrollOrigin(ScrollX, 0);
        }
    }

    bLayoutDirty = 0;
}

// ---------------------------------------------------------------------------------------------
// RichTextWin
// ---------------------------------------------------------------------------------------------

RichTextWin::RichTextWin()
{
    // FUN_0057b1b0: the base, the Content list (sentinel allocated by its ctor), both dirty
    // flags cleared, the layout flag 1.
    bSurfaceDirty = 0;
    bLayoutDirty = 0;
    LayoutFlag = 1;
}

RichTextWin::~RichTextWin()
{
    // FUN_0057b260: erase every Content node and free the sentinel -- the member's own dtor --
    // then BaseRichTextWin's (FUN_00579620).
}

bool RichTextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0057b5a0 (RET 8): BaseRichTextWin::Create (FUN_0057a810) inlined, then the Content
    // emptied without touching the dirty flags (0x0057b6a1..0x0057b6e6).
    bool const bResult = Win3D::Create(NewParent, nullptr);
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
    NormWidth = 0.4f;
    NormHeight = 0.3f;
    EnsureSurface();
    // 0x0057b62e..0x0057b69b: the DebugWindow (FUN_0057b700 registers its class) -- dead.
    RdlClear(Content);
    return bResult;
}

void RichTextWin::Update()
{
    // FUN_0057b530: animate the source list by the capped frame time (FUN_0042d680, rounded to
    // a float), then the ToolTip feed and Win3D's child walk.
    Content.update(static_cast<float>(GetClientFrameTimeCapped()));
    FeedToolTipIfHovered();
    Win3D::Update();
}

bool RichTextWin::Relayout()
{
    // LAB_0057b320.
    LayoutRichText(Trc, Content, PixelWidth - 4, LayoutList, 2, 0, false, &LayoutHeight,
                   LayoutFlag != 0);
    return true;
}

void RichTextWin::CreateWithText(Win3D* NewParent, wchar_t const* Text, float Width,
                                 float Height)
{
    // FUN_0057b7b0 (RET 0x10). No dirty flags beyond what Create sets.
    Create(NewParent, nullptr);
    NormWidth = Width;
    NormHeight = Height;
    TextNode Empty;
    RDLNode* const Clone = Empty.Clone();
    RdlAppendNode(Content, Clone->Clone());
    delete Clone;
    // 0057b868-0057b87b: the tail cell's node (sentinel[1][2]) gets set_text(Text, -1).
    void** const Sentinel = (void**)Content.NodeList;
    RDLNode* const Back = static_cast<RDLNode*>(static_cast<void**>(Sentinel[1])[2]);
    static_cast<TextNode*>(Back)->set_text(Text, -1);
}

void RichTextWin::ClearContent()
{
    // FUN_0057b370: only a non-empty list raises the dirty flags.
    if (Content.Count == 0)
        return;
    RdlClear(Content);
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::AppendContent(RenderDisplayList const& Source)
{
    // FUN_0057b3e0 (RET 4): one clone per node (the list element's copy constructor
    // FUN_0046ea00).
    if (Source.Count == 0)
        return;
    void** const Sentinel = (void**)Source.NodeList;
    for (void** Cell = static_cast<void**>(Sentinel[0]); Cell != Sentinel;
         Cell = static_cast<void**>(Cell[0]))
    {
        RdlAppendNode(Content, static_cast<RDLNode*>(Cell[2])->Clone());
    }
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::AppendNode(RDLNode const& Node)
{
    // FUN_0057b470 (RET 4): the node cloned (vtable +0x1c), the clone cloned again into the new
    // element (FUN_0046ea00), the first clone deleted.
    RDLNode* const Temp = Node.Clone();
    RdlAppendNode(Content, Temp->Clone());
    delete Temp;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::SetLayoutFlag(bool bFlag)
{
    // FUN_0057b520.
    LayoutFlag = bFlag ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// RichTextEditWin
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_0067c40c -- the default text attributes (static initializer LAB_0057b8b0: byte 0 = 0,
    // bytes 1..3 = 0xff).
    TextRenderAttributes const G_RichEditDefaultAttr = {0xffffff00u};

    // The clone-into-list idiom every RDL append here spells out (FUN_0046c800 / FUN_0046bb00,
    // or the same steps inline): a clone of the node is made, the list's element clones it
    // again (FUN_0046ea00), and the first clone is deleted.
    void AppendNodeClone(RenderDisplayList& List, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(List, Temp->Clone());
        delete Temp;
    }
}

RichTextEditWin::RichTextEditWin()
    // FUN_0057b8d0: RichTextWin's constructor inline (0x0057b8f1..0x0057b925), the containers
    // (the cell vector, the three lists, the FUN_0057f7c0 set), then the constants.
    : Caret(0)
    , Field_0x4a0(0)
    , Field_0x4a4(0)
    , Field_0x4a8(4)
    , bCaretVisible(1)
    , CaretTimer(0.5f)                       // DAT_005e4564
    , CaretPeriod(0.5f)                      // DAT_005e4564
    , CaretHeightScale(1.0f)                 // DAT_005e4568
    , bEditable(1)
    , CurrentAttr(G_RichEditDefaultAttr)     // 0x0057ba6d
    , FormatMask(-1)
    , bFixedStyle(0)
    , bPasswordMode(0)
    , CaseMode(0)
    , MaxLength(0)
    , ReplacementChar(L'_')                  // 0x5f
    , bFilterIsAllowList(0)
    , bImeEnabled(1)
    , ImeBox(nullptr)
{
    // 0x0057ba63: +0x4b8 = -1 (the four bytes zeroed first at 0x0057b942..0x0057b954).
    CaretColor[0] = 0xff;
    CaretColor[1] = 0xff;
    CaretColor[2] = 0xff;
    CaretColor[3] = 0xff;

    // 0x0057bac3..0x0057baf3: an IME box only while an IMEFocus singleton exists.
    if (IMEFocus::GetSingleton() != nullptr)
        ImeBox = new FLImeInputBox(this);
}

RichTextEditWin::~RichTextEditWin()
{
    // FUN_0057bbf0: the IME box deleted (slot 0 with 1); then the set, the three lists and the
    // cell vector (member destructors, in that order), Event::Handler's FUN_00576010 and
    // RichTextWin's destructor (base destructors).
    if (ImeBox != nullptr)
        delete ImeBox;
}

void RichTextEditWin::Update()
{
    // FUN_0057c360: the source list animated by the capped frame time rounded to a float, the
    // ToolTip feed (0x0057c381..0x0057c3be) and Win3D's child walk (FUN_005a17b0).
    Content.update(static_cast<float>(GetClientFrameTimeCapped()));
    FeedToolTipIfHovered();
    Win3D::Update();

    // 0x0057c3c8..0x0057c43c: the caret blink. The timer less a second read of the frame time
    // is stored (FST float) and tested on the FPU value: FCOM 0.0 / TEST AH,0x41 / JP leaves
    // when greater or unordered, so only <= 0 goes on. Then the shown flag flips; a value still
    // below zero (TEST AH,0x05 / JP) gets the period added until it is not.
    double Remaining = static_cast<double>(CaretTimer) - GetClientFrameTimeCapped();
    CaretTimer = static_cast<float>(Remaining);
    if (!(Remaining <= 0.0))
        return;
    bCaretVisible = (bCaretVisible == 0) ? 1 : 0;
    if (!(Remaining < 0.0))
        return;
    double const Period = static_cast<double>(CaretPeriod);
    do
    {
        Remaining += Period;
    } while (Remaining < 0.0);
    CaretTimer = static_cast<float>(Remaining);
}

void RichTextEditWin::Draw()
{
    // FUN_0057c440: BaseRichTextWin's draw (FUN_0057ab10).
    BaseRichTextWin::Draw();

    // 0x0057c44b..0x0057c4a0: the IME box gets the input size (the max length, or 0x50 when it
    // is not positive) and the screen rectangle (slot 3).
    if (ImeBox != nullptr)
    {
        RECT Rect;
        Rect.left = ScreenRect[0];
        Rect.top = ScreenRect[1];
        Rect.right = ScreenRect[2];
        Rect.bottom = ScreenRect[3];
        int BufferSize = MaxLength;
        if (!(BufferSize > 0))
            BufferSize = 0x50;
        ImeBox->SetInputBufferSize(static_cast<DWORD>(BufferSize));
        ImeBox->Resize(&Rect);
    }

    // 0x0057c4a3..0x0057c4c8: the caret only while shown, holding the input focus and in its
    // visible blink phase.
    if ((Flags & 2) == 0)
        return;
    if (HudGetInputFocus() != AsHudCmdHandler())
        return;
    if (bCaretVisible == 0)
        return;

    // 0x0057c4ce..0x0057c506: depth writes, depth test, culling and blending off; the matrix
    // mode call (0x1701) is the empty RET at 0x00401e90; the projection saved and made 2D.
    Win3DSetDepthWrite(0);           // FUN_00422190(0)
    Win3DDisable(kWin3DDepthTest);   // FUN_00421ed0(0xb71)
    Win3DDisable(kWin3DCullFace);    // FUN_00421ed0(0xb44)
    Win3DDisable(kWin3DBlend);       // FUN_00421ed0(0xbe2)
    Win3DPushProjection();           // FUN_00422a70
    Win3DSetOrtho2D();               // FUN_004225e0

    // 0x0057c50b..0x0057c55f: the text context reset, and the laid-out position of the element
    // after the prefix list and Caret characters.
    Trc.reset_to_default();
    Trc.set_pos_v(0);
    Trc.set_pos_h(0);
    float PosH = 0.0f;
    float PosV = 0.0f;
    float Depth = 0.0f;
    int const Index = Prefix.get_element_count() + Caret;
    if (LayoutList.get_element_pos(Trc, Index, PosH, PosV, Depth))
    {
        // 0x0057c565..0x0057c5be: the vertical offset for the alignment mode. For any other mode
        // retail leaves the stack local unwritten and reads its residue (SetAlignMode, FUN_00579a60,
        // does not range-check); the port reads 0 there -- a named gap.
        int OffsetV = 0;
        switch (AlignMode)
        {
        case 0:
            OffsetV = ScrollY;
            break;
        case 1:
            OffsetV = 0;
            break;
        case 2:
            OffsetV = -((PixelHeight - LayoutHeight) / 2);
            break;
        case 3:
            OffsetV = LayoutHeight - PixelHeight;
            break;
        default:
            break;
        }

        // 0x0057c5c0..0x0057c5f2: H -= scroll x; the height scaled less 4 (2.0 + 2.0); V less
        // the offset plus 2. Each is stored back as a float.
        PosH = static_cast<float>(static_cast<double>(PosH) - static_cast<double>(ScrollX));
        Depth = static_cast<float>(static_cast<double>(Depth) * static_cast<double>(CaretHeightScale) -
                                   (2.0 + 2.0));
        PosV = static_cast<float>(static_cast<double>(PosV) - static_cast<double>(OffsetV) + 2.0);

        // 0x0057c5f6..0x0057c64c: inside the window -- H >= 0 (TEST AH,1 / JNZ skips less and
        // unordered), width > H (TEST AH,0x41 / JNZ skips not-greater), V < height and -D < V
        // (TEST AH,5 / JP skips not-less and unordered).
        float const HeightF = static_cast<float>(PixelHeight);
        if (PosH >= 0.0f && static_cast<double>(PixelWidth) > static_cast<double>(PosH) &&
            PosV < HeightF && -Depth < PosV)
        {
            // 0x0057c652..0x0057c688: clip the line to the window. Above the top: the length
            // shrinks by the overhang (D = V + D) and V = 0. Otherwise a line reaching past the
            // bottom (V + D not below the height) is cut to height - V.
            if (PosV < 0.0f)
            {
                Depth = static_cast<float>(static_cast<double>(PosV) + static_cast<double>(Depth));
                PosV = 0.0f;
            }
            else if (!(static_cast<double>(PosV) + static_cast<double>(Depth) < static_cast<double>(HeightF)))
            {
                Depth = static_cast<float>(static_cast<double>(HeightF) - static_cast<double>(PosV));
            }

            // 0x0057c68c..0x0057c6ab: to screen pixels.
            PosH = static_cast<float>(static_cast<double>(ScreenRect[0]) + static_cast<double>(PosH));
            PosV = static_cast<float>(static_cast<double>(ScreenRect[1]) + static_cast<double>(PosV));

            // 0x0057c6af..0x0057c704: one line in the caret colour, texturing off.
            Win3DColor4ubv(CaretColor);        // FUN_00421ba0(&+0x4b8)
            Win3DDisable(kWin3DTexture2D);     // FUN_00421ed0(0xde1)
            Win3DBegin(kWin3DLines);           // FUN_00421670(2)
            Win3DVertex3f(PosH, PosV, 0.0f);
            Win3DVertex3f(PosH, static_cast<float>(static_cast<double>(PosV) + static_cast<double>(Depth)),
                          0.0f);
            Win3DEnd();                        // FUN_00421690
        }
    }

    // 0x0057c709..0x0057c729: the matrix mode no-op, the projection back, depth test and depth
    // writes on.
    Win3DPopProjection();            // FUN_00422ab0
    Win3DEnable(kWin3DDepthTest);    // FUN_00421dd0(0xb71)
    Win3DSetDepthWrite(1);           // FUN_00422190(1)
}

bool RichTextEditWin::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0057d1d0: with a parent, tail-jump to the parent's slot 19 (+0x4c) with the same
    // argument; without one, JMP FUN_005a0650 (WinControl's slot 19).
    Win3D* const Up = Parent;
    if (Up != nullptr)
        return Up->MouseEvent(Event);
    return WinControl::MouseEvent(Event);
}

void RichTextEditWin::CreateSized(Win3D* NewParent, float Width, float Height)
{
    // FUN_0057c330 (RET 0xc): slot 9 with (Parent, 0), then +0x330 / +0x334.
    Create(NewParent, nullptr);
    NormWidth = Width;
    NormHeight = Height;
}

bool RichTextEditWin::OnKeyDown(FHudCmdRecord const* Record)
{
    // FUN_0057c9b0 (RET 4).
    if ((Flags & 1) == 0)
        return false;

    // 0x0057c9c6..0x0057c9d4: a key-up leaves at once, without the IME forward.
    bool bResult = false;
    if (Record->Byte18 == 0)
        return bResult;

    // 0x0057c9da..0x0057c9e6: nothing is edited unless +0x4a0 == +0x4a4.
    if (Field_0x4a0 == Field_0x4a4)
    {
        unsigned const Key = Record->Key;
        bResult = true;
        // 0x0057c9ec..0x0057ca06: switch on Key - 8 through the byte table at 0x0057ce40.
        switch (Key)
        {
        case 0x08:
            // Backspace (0x0057cbb7): one to the left, then the cell there erased.
            if (Caret > 0)
            {
                SetCaret(Caret - 1);
                int const At = ClampedCellIndex(Caret);
                Cells.erase(Cells.begin() + At);
                RebuildDisplay();
            }
            break;
        case 0x09:
        case 0x2d:
            // Tab / Insert (0x0057cc5e): false; with an IME box it returns before the forward.
            if (ImeBox != nullptr)
                return false;
            bResult = false;
            break;
        case 0x0d:
            // Enter (0x0057cda5): the IME box sees the key, the parent gets slot 23 0xf004.
            if (ImeBox != nullptr)
                ImeBox->HandleKey(Key);
            if (Parent != nullptr)
                Parent->Slot23(0xf004u, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), 0u);
            return true;
        case 0x1b:
            // Escape (0x0057cd75): the same with 0xf003.
            if (ImeBox != nullptr)
                ImeBox->HandleKey(Key);
            if (Parent != nullptr)
                Parent->Slot23(0xf003u, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), 0u);
            return true;
        case 0x23:
            // End (0x0057cc90).
            if (ImeBox != nullptr)
                return false;
            SetCaret(static_cast<int>(Cells.size()));
            break;
        case 0x24:
            // Home (0x0057cc78).
            if (ImeBox != nullptr)
                return false;
            SetCaret(0);
            break;
        case 0x25:
            // Left (0x0057cb73).
            if (ImeBox != nullptr)
                return false;
            SetCaret(Caret - 1);
            break;
        case 0x27:
            // Right (0x0057cb95).
            if (ImeBox != nullptr)
                return false;
            SetCaret(Caret + 1);
            break;
        case 0x2e:
            // Delete (0x0057ccd8): the cell at the caret erased, the caret re-applied.
            if (!Cells.empty())
            {
                int const At = ClampedCellIndex(Caret);
                if (At != static_cast<int>(Cells.size()))
                {
                    Cells.erase(Cells.begin() + At);
                    SetCaret(Caret);
                    RebuildDisplay();
                }
            }
            break;
        case 0x42:
            // Ctrl+B (0x0057ca0d): bit 0 of the attributes toggled when allowed.
            if (bFixedStyle == 0 && (FormatMask & 1) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x01u;
            else
                bResult = false;
            break;
        case 0x49:
            // Ctrl+I (0x0057ca4c): bit 1.
            if (bFixedStyle == 0 && (FormatMask & 2) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x02u;
            else
                bResult = false;
            break;
        case 0x55:
            // Ctrl+U (0x0057ca91): bit 2.
            if (bFixedStyle == 0 && (FormatMask & 4) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x04u;
            else
                bResult = false;
            break;
        case 0x6b:
            // Ctrl+numpad plus (0x0057cad8): the size field (bits 3..7) up one, capped at 0xf8.
            if (bFixedStyle == 0 && (FormatMask & 0xf8) != 0 && (Record->Modifiers & 0xc) != 0)
            {
                unsigned char const Low = static_cast<unsigned char>(CurrentAttr.Packed & 0xffu);
                if ((Low & 0xf8u) < 0xf8u)
                {
                    unsigned char const NewLow =
                        static_cast<unsigned char>((((Low & 0xf8u) + 8u) & 0xffu) ^ (Low & 0x07u));
                    CurrentAttr.Packed = (CurrentAttr.Packed & ~0xffu) | NewLow;
                }
            }
            else
            {
                bResult = false;
            }
            break;
        case 0x6d:
            // Ctrl+numpad minus (0x0057cb28): the size field down one, stopping at 0.
            if (bFixedStyle == 0 && (FormatMask & 0xf8) != 0 && (Record->Modifiers & 0xc) != 0)
            {
                unsigned char const Low = static_cast<unsigned char>(CurrentAttr.Packed & 0xffu);
                if ((Low & 0xf8u) != 0)
                {
                    unsigned char const NewLow = static_cast<unsigned char>(
                        (((Low & 0xf8u) - 1u) & 0xffu) ^ (static_cast<unsigned>(~Low) & 0x07u));
                    CurrentAttr.Packed = (CurrentAttr.Packed & ~0xffu) | NewLow;
                }
            }
            else
            {
                bResult = false;
            }
            break;
        default:
            // 0x0057cdd5
            bResult = false;
            break;
        }
    }

    // 0x0057cdda..0x0057cdec: the IME box sees every key-down that reaches here.
    if (ImeBox != nullptr)
        ImeBox->HandleKey(Record->Key);
    return bResult;
}

bool RichTextEditWin::OnChar(FHudCmdRecord const* Record)
{
    // LAB_0057ceb0 (RET 4).
    if ((Flags & 1) == 0)
        return false;

    // 0x0057cec5..0x0057cedd: the character (the record's wParam word); a control character is
    // consumed and nothing else happens.
    unsigned Ch = static_cast<unsigned short>(Record->Arg10);
    if (iswcntrl(static_cast<wint_t>(Ch)))
        return true;

    // 0x0057cee3..0x0057cf0e: 1 upper case, 2 lower case, anything else as typed.
    if (CaseMode == 1)
        Ch = towupper(static_cast<wint_t>(Ch));
    else if (CaseMode == 2)
        Ch = towlower(static_cast<wint_t>(Ch));

    // 0x0057cf12..0x0057cf8f: the filter -- an allow list replaces what it does not hold, a
    // deny list what it does. Only the low word is replaced.
    wchar_t const Lookup = static_cast<wchar_t>(Ch & 0xffffu);
    bool const bInSet = FilterChars.find(Lookup) != FilterChars.end();
    if (bFilterIsAllowList != 0 ? !bInSet : bInSet)
        Ch = (Ch & 0xffff0000u) | static_cast<unsigned short>(ReplacementChar);

    // 0x0057cf94..0x0057d1ad: inserted at the caret with the current attributes, unless
    // +0x4a0 != +0x4a4 or the text is at its (non-zero) maximum length.
    if (Field_0x4a0 == Field_0x4a4 &&
        !(MaxLength != 0 && static_cast<unsigned>(Cells.size()) >= static_cast<unsigned>(MaxLength)))
    {
        FRichEditCell Cell;
        Cell.Ch = static_cast<wchar_t>(Ch & 0xffffu);
        Cell.Attr = CurrentAttr;
        int const At = ClampedCellIndex(Caret);
        Cells.insert(Cells.begin() + At, Cell);
        SetCaret(Caret + 1);
        RebuildDisplay();
    }

    // 0x0057d1ae..0x0057d1c7: the IME box sees the character (its low byte).
    if (ImeBox != nullptr)
        ImeBox->HandleMultiByteChar(static_cast<char>(Ch & 0xffu));
    return true;
}

void RichTextEditWin::OnActivated()
{
    // FUN_00401e90: RET.
}

void RichTextEditWin::OnDeactivating()
{
    // FUN_00401e90: RET.
}

bool RichTextEditWin::OnEvent(unsigned MsgId)
{
    // FUN_0057c930 (RET 4, this = the +0x498 face).
    if (MsgId == 1)
        return OnKeyDown(HudCommandRecord());     // slot 49
    if (MsgId == 2)
        return OnChar(HudCommandRecord());        // slot 50
    if (MsgId == 0xf)
    {
        // 0x0057c941..0x0057c95d: with a raw window message in the record, USER_CANCEL is posted
        // again (FUN_005763b0(0xf, 0)) and the event consumed.
        if (HudCommandRecord()->Field0C != 0)
        {
            HudPostCommand(0xf, 0);
            return true;
        }
    }
    return false;
}

void RichTextEditWin::SetMaxLength(int MaxChars)
{
    // FUN_0057bdb0 (RET 4)
    if (MaxChars >= 0)
        MaxLength = MaxChars;
}

void RichTextEditWin::SetFixedStyle(bool bFixed)
{
    // FUN_0057bdd0 (RET 4)
    unsigned char const Value = bFixed ? 1 : 0;
    bool const bChanged = Value != bFixedStyle;
    bFixedStyle = Value;
    if (bChanged)
        RebuildDisplay();
}

void RichTextEditWin::SetPasswordMode(bool bPassword)
{
    // FUN_0057be00 (RET 4)
    unsigned char const Value = bPassword ? 1 : 0;
    bool const bChanged = Value != bPasswordMode;
    bPasswordMode = Value;
    if (bChanged)
        RebuildDisplay();
}

void RichTextEditWin::SetImeEnabled(bool bEnabled)
{
    // FUN_0057be30 (RET 4).
    bImeEnabled = bEnabled ? 1 : 0;
    if (bEnabled)
    {
        // 0x0057be5d..0x0057bf5f: a box is built only when there is none and an IMEFocus
        // singleton exists; then FUN_0057bff0's body inline -- which rebuilds the box once more.
        if (ImeBox != nullptr)
            return;
        if (IMEFocus::GetSingleton() == nullptr)
            return;
        ImeBox = new FLImeInputBox(this);
        ClearText();
        return;
    }
    // 0x0057bf6b..0x0057bf7b
    if (ImeBox != nullptr)
    {
        delete ImeBox;
        ImeBox = nullptr;
    }
}

void RichTextEditWin::SetEditable(bool bNewEditable)
{
    // FUN_0057bfa0 (RET 4): unchanged -> return; +0x4c0 = the flag; then, only while this
    // object's +0x498 face is the input focus (FUN_00577d50): editable -> FUN_005760f0(0xf),
    // else FUN_005760d0(0xf).
    unsigned char const Value = bNewEditable ? 1 : 0;
    if (bEditable == Value)
        return;
    bEditable = Value;
    if (HudGetInputFocus() != AsHudCmdHandler())
        return;
    if (bEditable != 0)
        UnregisterEvent(0xf);
    else
        RegisterEvent(0xf);
}

void RichTextEditWin::ClearText()
{
    // FUN_0057bff0: erase(begin, end) (the copy loop runs over an empty range), the three
    // position fields 0, the display rebuilt; an existing IME box is deleted and built again.
    Cells.clear();
    Field_0x4a4 = 0;
    Field_0x4a0 = 0;
    Caret = 0;
    RebuildDisplay();
    if (ImeBox != nullptr)
    {
        delete ImeBox;
        ImeBox = new FLImeInputBox(this);
    }
}

void RichTextEditWin::SetText(wchar_t const* Text, TextRenderAttributes const* Attr)
{
    // FUN_0057c0e0 (RET 8): the cells cleared (FUN_0057fb50 copy + destroy of an empty range).
    Cells.clear();
    TextRenderAttributes const RunAttr = (Attr != nullptr) ? *Attr : CurrentAttr;

    // 0x0057c131..0x0057c272: one cell per character until the terminator or, with a non-zero
    // maximum, until the count reaches it (unsigned compare). Case mode 0 keeps the character,
    // 1 upper-cases it, anything else lower-cases it; then the filter (see OnChar).
    wchar_t const* P = Text;
    while (*P != L'\0')
    {
        if (MaxLength != 0 && static_cast<unsigned>(Cells.size()) >= static_cast<unsigned>(MaxLength))
            break;
        wchar_t Ch;
        if (CaseMode == 0)
            Ch = *P;
        else if (CaseMode == 1)
            Ch = static_cast<wchar_t>(towupper(static_cast<wint_t>(*P)));
        else
            Ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(*P)));
        bool const bInSet = FilterChars.find(Ch) != FilterChars.end();
        if (bFilterIsAllowList != 0 ? !bInSet : bInSet)
            Ch = ReplacementChar;
        FRichEditCell Cell;
        Cell.Ch = Ch;
        Cell.Attr = RunAttr;
        Cells.push_back(Cell);   // FUN_0057eec0(end, 1, &cell)
        ++P;
    }

    // 0x0057c278..0x0057c2ac: the three position fields = the count, then the display.
    int const Count = static_cast<int>(Cells.size());
    Field_0x4a4 = Count;
    Field_0x4a0 = Count;
    Caret = Count;
    RebuildDisplay();
}

int RichTextEditWin::GetText(wchar_t* Buffer, int MaxChars)
{
    // FUN_0057c2c0 (RET 8). 0057c2c0..0057c2e2: count = begin ? (end - begin) / 0xc : 0.
    int const Count = static_cast<int>(Cells.size());

    // 0057c2e4..0057c2ea: MaxChars <= 0 returns the count untouched.
    if (MaxChars <= 0)
        return Count;

    // 0057c2ed..0057c323: at most MaxChars - 1 characters, then the terminator; EAX = index + 1.
    int Limit = Count;
    if (Limit > MaxChars - 1)
        Limit = MaxChars - 1;
    int Index = 0;
    while (Index < Limit)
    {
        Buffer[Index] = Cells[Index].Ch;
        ++Index;
    }
    Buffer[Index] = L'\0';
    return Index + 1;
}

void RichTextEditWin::Activate()
{
    // FUN_0057c750: shown (Flags | 3); the face pushed as the input focus (FUN_00577b40) and
    // registered for 1 and 2; 0xf dropped while editable, registered while read-only; the IME
    // box focused (IMEFocus slot 11 with (box, false)); then slot 51 (tail jump).
    Flags = static_cast<unsigned char>(Flags | 3);
    HudPushInputFocus(AsHudCmdHandler());
    RegisterEvent(1);
    RegisterEvent(2);
    if (bEditable != 0)
        UnregisterEvent(0xf);
    else
        RegisterEvent(0xf);
    if (ImeBox != nullptr)
    {
        IMEFocus* const Focus = IMEFocus::GetSingleton();
        if (Focus != nullptr)
            Focus->SetFocus(ImeBox, false);
    }
    OnActivated();
}

void RichTextEditWin::Deactivate(bool bHide)
{
    // FUN_0057c7d0 (RET 4): slot 52 first; 1, 2 and 0xf unregistered; the face dropped from
    // the input focus (FUN_00577c30); hidden on request (Flags & 0xfc); the IME focus cleared
    // when the IME is enabled.
    OnDeactivating();
    UnregisterEvent(1);
    UnregisterEvent(2);
    UnregisterEvent(0xf);
    HudPopInputFocus(AsHudCmdHandler());
    if (bHide)
        Flags = static_cast<unsigned char>(Flags & 0xfc);
    if (bImeEnabled != 0)
    {
        IMEFocus* const Focus = IMEFocus::GetSingleton();
        if (Focus != nullptr)
            Focus->SetFocus(nullptr, false);
    }
}

void RichTextEditWin::SetCaseMode(int Mode)
{
    // FUN_0057c840 (RET 4)
    CaseMode = Mode;
}

void RichTextEditWin::SetFilterIsAllowList(bool bAllowList)
{
    // FUN_0057c850 (RET 4)
    bFilterIsAllowList = bAllowList ? 1 : 0;
}

void RichTextEditWin::AddFilterChars(wchar_t const* Chars, int Count)
{
    // FUN_0057c860 (RET 8): nothing for a null pointer or a zero count; otherwise from the last
    // character back, each inserted (FUN_0057f820 on the hint the lower-bound walk found). The
    // loop is DEC / JNZ on the count.
    if (Chars == nullptr || Count == 0)
        return;
    wchar_t const* P = Chars + Count;
    int Remaining = Count;
    do
    {
        --P;
        FilterChars.insert(*P);
        --Remaining;
    } while (Remaining != 0);
}

void RichTextEditWin::SetReplacementChar(wchar_t Ch)
{
    // FUN_0057c920 (RET 4)
    ReplacementChar = Ch;
}

void RichTextEditWin::RebuildDisplay()
{
    // FUN_0057d330.
    // 0x0057d34d..0x0057d369: a non-empty source list is emptied (FUN_0046bb50) and marked.
    if (Content.Count != 0)
    {
        RdlClear(Content);
        MarkDirty(true, true);
    }

    // 0x0057d36e..0x0057d3e2: a reference to the prefix list appended, marked.
    {
        RDLRefNode const Ref(Prefix);
        AppendNodeClone(Content, Ref);
        MarkDirty(true, true);
    }
    // 0x0057d3e4..0x0057d443: a reference to the text list appended, marked.
    {
        RDLRefNode const Ref(TextDisplay);
        AppendNodeClone(Content, Ref);
        MarkDirty(true, true);
    }

    // 0x0057d441..0x0057d44f: the text list emptied; nothing more without cells.
    RdlClear(TextDisplay);
    if (Cells.empty())
        return;

    // 0x0057d484..0x0057d545: the first cell's attributes open the run -- as a reference to the
    // fixed-style list, or as a TRANode(attr, -1, 0).
    TextRenderAttributes RunAttr = Cells[0].Attr;
    if (bFixedStyle != 0)
    {
        RDLRefNode const Ref(FixedStyle);
        AppendNodeClone(TextDisplay, Ref);
    }
    else
    {
        TRANode const Node(RunAttr, 0xffffffffu, 0u);
        AppendNodeClone(TextDisplay, Node);
    }

    // 0x0057d547..0x0057d625: without a fixed style, every change of attributes closes the run
    // before it and opens a new TRANode. The count is re-read every pass.
    int Start = 0;
    if (bFixedStyle == 0)
    {
        int Index = 0;
        while (!Cells.empty() && static_cast<unsigned>(Index) < static_cast<unsigned>(Cells.size()))
        {
            if (Cells[Index].Attr.Packed != RunAttr.Packed)
            {
                AppendTextRun(Start, Index - Start);
                RunAttr = Cells[Index].Attr;
                TRANode const Node(RunAttr, 0xffffffffu, 0u);
                AppendNodeClone(TextDisplay, Node);
                Start = Index;
            }
            ++Index;
        }
    }

    // 0x0057d62a..0x0057d67c: the last run, while Start is below the count (unsigned).
    if (!Cells.empty() && static_cast<unsigned>(Start) < static_cast<unsigned>(Cells.size()))
        AppendTextRun(Start, static_cast<int>(Cells.size()) - Start);
}

void RichTextEditWin::SetCaret(int Position)
{
    // FUN_0057d6a0 (RET 4): stored, then clamped -- below 0 to 0, above the count (unsigned
    // compare) to the count.
    Caret = Position;
    if (Position < 0)
    {
        Caret = 0;
    }
    else if (static_cast<unsigned>(Position) > static_cast<unsigned>(Cells.size()))
    {
        Caret = static_cast<int>(Cells.size());
    }

    // 0x0057d711..0x0057d7a9: with cells, the current attributes follow the cell before the
    // caret -- the last cell at the end, the first cell at 0.
    if (Cells.empty())
        return;
    int const Count = static_cast<int>(Cells.size());
    if (Caret == Count)
        CurrentAttr = Cells[Count - 1].Attr;
    else if (Caret > 0)
        CurrentAttr = Cells[Caret - 1].Attr;
    else
        CurrentAttr = Cells[0].Attr;
}

void RichTextEditWin::AppendTextRun(int Start, int Count)
{
    // FUN_0057d1f0 (RET 8): nothing for Count <= 0. The characters go into a stack buffer
    // (_alloca) -- '*' each in password mode -- then a TextNode(buffer, -1) is cloned onto the
    // text list (the push inline, +0x4f0 incremented) and the clone deleted.
    if (Count <= 0)
        return;
    std::vector<wchar_t> Buffer(static_cast<size_t>(Count) + 1);
    if (bPasswordMode != 0)
    {
        int Index = 0;
        while (Index < Count)
        {
            Buffer[Index] = L'*';
            ++Index;
        }
    }
    else
    {
        int Index = 0;
        while (Index < Count)
        {
            Buffer[Index] = Cells[Start + Index].Ch;
            ++Index;
        }
    }
    Buffer[Count] = L'\0';
    TextNode const Node(Buffer.data(), -1);
    AppendNodeClone(TextDisplay, Node);
}

int RichTextEditWin::ClampedCellIndex(int Position) const
{
    // begin() advanced while (it != end && n > 0).
    int const Count = static_cast<int>(Cells.size());
    int Index = 0;
    int Remaining = Position;
    while (Index != Count && Remaining > 0)
    {
        ++Index;
        --Remaining;
    }
    return Index;
}

// Retail DAT_0067c440 -- WINFACTORY<RichTextWin> static (LAB_00579320 -> FUN_0057ec50("RichTextWin");
// atexit LAB_00579340 -> FUN_00579380). Create FUN_0057ecc0: operator_new(0x498), FUN_0057b1b0
// inlined (BaseRichTextWin ctor, Content sentinel, +0x401 / +0x400 = 0, +0x494 = 1), +0x10 =
// &factory+0xc, slot 9; false -> scalar deleting dtor, return 0. The same shape as every
// WINFACTORY<T>::Create (win_factory.h).
WINFACTORY<RichTextWin> G_RichTextWinFactory("RichTextWin");

// Retail DAT_0067c414 -- WINFACTORY<RichTextEditWin> static (LAB_00579350 ->
// FUN_0057edb0("RichTextEditWin"); atexit LAB_00579370 -> FUN_00579380). Create FUN_0057ee20:
// operator_new(0x530), FUN_0057b8d0, +0x10 = &factory+0xc, slot 9 (RichTextWin's FUN_0057b5a0);
// false -> scalar deleting dtor, return 0.
WINFACTORY<RichTextEditWin> G_RichTextEditWinFactory("RichTextEditWin");
