// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_text_msg_button.h"

#include "client_mesh_load.h"
#include "ids_rdl.h"
#include "menu_ui_init.h"   // PlayUiRolloverSound (FUN_00564700)
#include "nn_ui_pulse.h"
#include "ui_page.h"      // UiWidgetMathEngine (DAT_0067dbc0)
#include "ui_page_desc.h"
#include "win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <string.h>

namespace
{
    // DAT_0067dbac, built by the static initializer LAB_00599da0: byte +0 = 8, the dword at +1 =
    // (old & 0xff000000) | 0x00e0c387 over zeroed .bss -- bytes 08 87 C3 E0. The idle caption
    // attributes (the hover colour is computed per frame).
    TextRenderAttributes const G_UiTextMsgButtonIdleAttrs = {0xe0c38708ul};

    // DAT_0067dba4, LAB_00599dc0: bytes 08 80 80 80 -- the caption of a non-interactive button.
    TextRenderAttributes const G_UiTextMsgButtonInactiveAttrs = {0x80808008ul};

    // .rdata 0x005d4f58, the "<default>" style mesh.
    char const kUiTextMsgButtonDefaultStyle[] = "Interface\\NeuroNet\\Inventory\\FRONT_button.cmp";

}

UiTextMsgButton::UiTextMsgButton()
    : bInteractive(0)
    , LocalWidth(0.0f)
    , LocalHeight(0.0f)
    , MessageArg(0)
    , Field4c4(0)
{
    // FUN_0059c580 inline: RichTextWin ctor FUN_0057b1b0, then the empty RDLs at +0x4a8 and
    // +0x4b4 and the two vftables. +0x498..+0x4a4 and +0x4c0 are left to Create; the zeros here
    // are only C++ initialisation.
    CaptionAttrs.Packed = 0;
    Pad499[0] = 0;
    Pad499[1] = 0;
    Pad499[2] = 0;
}

UiTextMsgButton::~UiTextMsgButton()
{
    // FUN_0059ce50: the +0x4b4 and +0x4a8 lists (the members' own destructors), then
    // RichTextWin's (FUN_0057b260).
}

void UiTextMsgButton::SetPosition(float const* NewPosition)
{
    // LAB_00599eb0 (RET 4). The style mesh sits at the button's centre: half the payload size
    // along the control's axes (DAT_005c75e0 = 0.5f, DAT_005cef60 = -0.5f), scaled onto the
    // z = -1 plane (DAT_005c7500 = -1.0f) and written straight into the WinControl offset
    // +0xa8..+0xb0. Then BaseRichTextWin::SetPosition (FUN_0057b0a0).
    // 0x00599eb3..0x00599eec: DAT_0067dbc0 -- the UI_PAGE widgets' shared cache
    // (UiWidgetMathEngine), tested and filled from FUN_005b73e0.
    I3DMathEngine* const Math = UiWidgetMathEngine();
    FVector Half;
    Half.X = LocalWidth * 0.5f;
    Half.Y = LocalHeight * -0.5f;
    Half.Z = 0.0f;
    FVector Centre;
    Math->TransformMatrix(Centre, *reinterpret_cast<FMatrix const*>(Orientation), Half);
    double const SumX = static_cast<double>(Centre.X) + NewPosition[0];
    double const SumY = static_cast<double>(Centre.Y) + NewPosition[1];
    double const Scale = -1.0 / NewPosition[2];
    Offset[0] = static_cast<float>(SumX * Scale - NewPosition[0]);
    Offset[1] = static_cast<float>(SumY * Scale - NewPosition[1]);
    Offset[2] = -1.0f - NewPosition[2];
    BaseRichTextWin::SetPosition(NewPosition);
}

void UiTextMsgButton::SetOrientation(float const* NewOrientation)
{
    // LAB_00599e00 (RET 4): WinControl's SetOrientation (FUN_005a0950), then, with a style mesh
    // bound (FUN_00420d60), the instance is reset to the identity orientation (Engine +0x94) and
    // updated (Engine +0x6c).
    WinControl::SetOrientation(NewOrientation);
    if (!ClientMeshHolderHasInstance(&MeshHolder))
        return;
    float Identity[9];
    Identity[0] = 1.0f;
    Identity[1] = 0.0f;
    Identity[2] = 0.0f;
    Identity[3] = 0.0f;
    Identity[4] = 1.0f;
    Identity[5] = 0.0f;
    Identity[6] = 0.0f;
    Identity[7] = 0.0f;
    Identity[8] = 1.0f;
    DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, Identity);
    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
}

void UiTextMsgButton::ApplyCaptionLayout()
{
    // FUN_0059a110: FUN_0057b370 clear, then FUN_0057b470 (clone + push) of each node.
    ClearContent();

    JustifyNode Justify(TextJustifyCenter);                          // 1
    AppendNode(Justify);

    RDLRefNode CaptionRef(CaptionBodyList);                          // +0x4a8
    AppendNode(CaptionRef);

    StyleNode Style(0x828b);
    AppendNode(Style);

    TRANode Tra(CaptionAttrs, 0xffffff00u, 0u);                      // +0x4c0
    AppendNode(Tra);

    RDLRefNode TextRef(TextBodyList);                                // +0x4b4
    AppendNode(TextRef);
}

bool UiTextMsgButton::Create(Win3D* NewParent, void* Data)
{
    // FUN_00599f60 (RET 8).
    bool const bResult = RichTextWin::Create(NewParent, nullptr);   // FUN_0057b5a0
    bInteractive = 1;                                                 // +0x498
    CaptionAttrs = G_UiTextMsgButtonIdleAttrs;                        // +0x4c0

    FUiPayloadTextMsgButton const* const Payload =
        static_cast<FUiPayloadTextMsgButton const*>(Data);
    if (Payload != nullptr)
    {
        LocalWidth = Payload->Right;                                  // +0x49c
        NormWidth = Payload->Right;                                   // +0x330
        LocalHeight = Payload->Bottom;                                // +0x4a0
        NormHeight = Payload->Bottom;                                 // +0x334

        if (Payload->ExtraId != 0)
        {
            // FUN_0057da40(id, this+0x4b4) -- appends, no trailing-paragraph trim here.
            LoadIdsIntoRdl(static_cast<unsigned>(Payload->ExtraId), TextBodyList);
        }
        else
        {
            // TextNode(caption, -1) (import 0x005c62b0); its vt+0x1c clone goes into the list.
            TextNode Text(Payload->CaptionUtf16, -1);
            RdlAppendNode(TextBodyList, Text.Clone());
        }
        MessageArg = Payload->ExtraInt;                               // +0x4a4
    }

    SetAlignMode(2);                                                  // FUN_00579a60

    // 0x0059a098: MOV EAX,[EDI+0x14] runs on both paths -- retail reads the payload here even
    // when it was null above.
    char const* const StyleName = Payload->StyleName;
    if (StyleName != nullptr && strcmp(StyleName, "<none>") != 0)
    {
        if (strcmp(StyleName, "<default>") == 0)
            SetMesh(kUiTextMsgButtonDefaultStyle, 0);                 // vt+0x98
        else
            SetMesh(StyleName, 0);
        Field2d2 = 1;                                                 // +0x2d2
    }

    ApplyCaptionLayout();                                             // FUN_0059a110
    return bResult;
}

void UiTextMsgButton::Draw()
{
    // LAB_0059a260. The caption follows three states: hovered (the pulse around the amber
    // DAT_00679ba4, FUN_00561f20), idle (DAT_0067dbac) and non-interactive (DAT_0067dba4). Only
    // the colour bytes +0x4c1..+0x4c3 are compared; a change rebuilds the content.
    bool const bHovered = Win3DGetHover() == this;
    unsigned long PulseArgb = 0;
    NnUiPulseBlendFromArgb(&PulseArgb, G_NnPaletteArgb_679ba4);
    unsigned char* const Attrs = reinterpret_cast<unsigned char*>(&CaptionAttrs.Packed);

    bool bRelayout = false;
    if (bInteractive != 0)
    {
        if (bHovered)
        {
            unsigned char const R = static_cast<unsigned char>(PulseArgb & 0xff);
            unsigned char const G = static_cast<unsigned char>((PulseArgb >> 8) & 0xff);
            unsigned char const B = static_cast<unsigned char>((PulseArgb >> 16) & 0xff);
            if (Attrs[1] != R || Attrs[2] != G || Attrs[3] != B)
            {
                Attrs[1] = R;
                Attrs[2] = G;
                Attrs[3] = B;
                bRelayout = true;
            }
        }
        else if (((CaptionAttrs.Packed ^ G_UiTextMsgButtonIdleAttrs.Packed) & 0xffffff00u) != 0)
        {
            CaptionAttrs = G_UiTextMsgButtonIdleAttrs;
            bRelayout = true;
        }
    }
    else if (((CaptionAttrs.Packed ^ G_UiTextMsgButtonInactiveAttrs.Packed) & 0xffffff00u) != 0)
    {
        CaptionAttrs = G_UiTextMsgButtonInactiveAttrs;
        bRelayout = true;
    }
    if (bRelayout)
        ApplyCaptionLayout();

    // 0x0059a310..0x0059a3b5: a red debug outline gated on DAT_0067dbc4, which has four readers
    // and no writer anywhere in the PE -- never drawn.

    BaseRichTextWin::Draw();                                          // FUN_0057ab10
}

void UiTextMsgButton::MouseEnter(float const* /*Ray*/)
{
    // LAB_0059a240 (RET 4): 0059a240..0059a24a -- byte +0x498 set -> FUN_00564700.
    if (bInteractive != 0)
        PlayUiRolloverSound();
}

bool UiTextMsgButton::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0059a3d0 (RET 4): a release (Field1 == 2) of button -1 on an interactive button tells
    // the parent (slot 23: 0x3004, +0x4a4, 0) and is consumed.
    if (Event->Field1 != 2)
        return false;
    if (Event->Field0 != -1)
        return false;
    if (bInteractive == 0)
        return false;
    if (Parent != nullptr)
        Parent->Slot23(0x3004u, static_cast<unsigned>(MessageArg), 0u);
    return true;
}

// Retail DAT_0067db44 -- WINFACTORY<UITextMsgButton> static (LAB_0059c210 -> FUN_0059c510;
// Create FUN_0059c580); atexit dtor FUN_00579380.
WINFACTORY<UiTextMsgButton> G_UITextMsgButtonFactory("UITextMsgButton");
