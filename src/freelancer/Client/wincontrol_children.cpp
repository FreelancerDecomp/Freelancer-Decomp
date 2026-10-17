// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "wincontrol_base.h"

#include "nn_button_control.h"
#include "nn_static_control.h"
#include "nn_text_control.h"
#include "rich_text_win.h"
#include "ui_page_desc.h"
#include "ui_scroll.h"
#include "win_factory.h"
#include "wire_controls.h"

// Retail WinControl's child-construction helpers, 0x005a0c90..0x005a0ee5 and 0x005a10e0 /
// 0x005a1950. Every one has the same shape: FUN_0059db20(Name, <class name>, this, 0); on a
// non-null result `MOV ECX,ESI` and a direct call to that control class's Init with the
// helper's remaining arguments, pushed in the same order; the helper returns FUN_0059db20's
// result either way.
//
// StaticControl (nn_static_control), TextControl and EditControl (nn_text_control), ButtonControl
// (nn_button_control.h) and the wire controls (wire_controls.h) are registered through their
// WINFACTORY<T> statics at load, as in retail.

WinControl* WinControl::CreateButtonControl(char const* Name, char const* MeshPath,
                                            char const* String350, char const* SoundName,
                                            char const* KeyCommandName, float const* Position,
                                            char const* Hardpoint, float const* HalfExtents,
                                            unsigned char bFlag331, unsigned char bFlag330Bit3,
                                            unsigned char bFlag330Bit5, unsigned char bNoZEnable,
                                            int ToolTipIds, int Dword32c, int Dword34c)
{
    // FUN_005a0c90: every argument after the name goes to ButtonControl::Init FUN_0055ac30 in
    // order (0x005a0cc4..0x005a0cf4). "ButtonControl" is the WINFACTORY<NnButtonControl>
    // static (nn_button_control.cpp).
    Win3D* const Created = WinFactoryCreate(Name, "ButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        NnButtonControl* const Button = static_cast<NnButtonControl*>(Created);
        Button->Init(MeshPath, String350, SoundName, KeyCommandName, Position, Hardpoint,
                     HalfExtents, bFlag331, bFlag330Bit3, bFlag330Bit5, bNoZEnable, ToolTipIds,
                     Dword32c, Dword34c);
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateWireButtonControl(char const* Name, char const* SoundName,
                                                float const* Position, float const* HalfExtents,
                                                unsigned char bFlag3, unsigned char bNoZEnable,
                                                int ToolTipIds, int CommandId, int WireFlags)
{
    // FUN_005a0d00.
    Win3D* const Created = WinFactoryCreate(Name, "WireButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        WireButtonControl* const Wire = static_cast<WireButtonControl*>(Created);
        Wire->Init(SoundName, Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds, CommandId,
                   static_cast<unsigned>(WireFlags)); // FUN_005a3ee0
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateWireStaticControl(char const* Name, float const* Position,
                                                float const* HalfExtents, unsigned char bFlag3,
                                                unsigned char bNoZEnable, int ToolTipIds,
                                                int WireFlags)
{
    // FUN_005a0d60.
    Win3D* const Created = WinFactoryCreate(Name, "WireStaticControl", this, nullptr);
    if (Created != nullptr)
    {
        WireStaticControl* const Wire = static_cast<WireStaticControl*>(Created);
        Wire->Init(Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds,
                   static_cast<unsigned>(WireFlags)); // FUN_005a3360
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateStaticControl(char const* Name, char const* MeshPath,
                                            float const* Position, char const* Hardpoint,
                                            float const* HalfExtents, unsigned char bNoZEnable,
                                            unsigned char Byte34c, unsigned char Byte344,
                                            unsigned char Byte345, unsigned char Byte346,
                                            unsigned char Byte34b, unsigned char Byte34a,
                                            int Int418, int Int41c, unsigned char Byte349,
                                            float const* Vector338)
{
    // FUN_005a0db0: every argument after the name goes to StaticControl::Init FUN_00588540 in
    // order (0x005a0dd9..0x005a0e19). "StaticControl" is the WINFACTORY<NnStaticControl>
    // static (nn_static_control.cpp).
    Win3D* const Created = WinFactoryCreate(Name, "StaticControl", this, nullptr);
    if (Created != nullptr)
    {
        NnStaticControl* const Static = static_cast<NnStaticControl*>(Created);
        Static->Init(MeshPath, Position, Hardpoint, HalfExtents, bNoZEnable, Byte34c, Byte344,
                     Byte345, Byte346, Byte34b, Byte34a, Int418, Int41c, Byte349, Vector338);
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateTextControl(char const* Name, char const* StyleName,
                                          float const* Position, char const* Hardpoint,
                                          float Width, float Height, float Arg6, unsigned Ids,
                                          int Dword358, int Dword344, unsigned char Byte378)
{
    // FUN_005a0e30: FUN_0059db20(Name, "TextControl", this, 0) -- the WINFACTORY<TextControl>
    // static (nn_text_control.cpp) -- and on a non-null result FUN_0058a110 with every argument.
    Win3D* const Created = WinFactoryCreate(Name, "TextControl", this, nullptr);
    if (Created != nullptr)
    {
        // FUN_0058a110(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids, Dword358,
        // Dword344, Byte378) -- TextControl::Init with every argument as passed.
        NnTextControl* const Text = static_cast<NnTextControl*>(Created);
        Text->Init(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids, Dword358, Dword344,
                   Byte378);
    }
    return static_cast<WinControl*>(Created);
}

UiScroll* WinControl::CreateUIScroll(char const* Name, float const* Position, float Width,
                                     float Height, int Min, int Max, int Page,
                                     unsigned char bHorizontal)
{
    // FUN_005a17e0: the payload is built on the stack (0x005a17e3..0x005a1829) -- the two sizes
    // as passed, the three ints through FILD, a step of 1.0f and the byte -- and handed to
    // FUN_0059db20(Name, "UIScroll", this, &Payload).
    FUiPayloadScrollCycle Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.Extra0 = static_cast<float>(Min);
    Payload.Extra1 = static_cast<float>(Max);
    Payload.Extra2 = static_cast<float>(Page);
    Payload.Extra3 = 1.0f;
    Payload.Flag = bHorizontal;
    UiScroll* const Scroll = static_cast<UiScroll*>(WinFactoryCreate(Name, "UIScroll", this,
                                                                      &Payload));
    if (Scroll != nullptr)
    {
        Scroll->SetScrollOffset(Position);   // FUN_005972d0
        Scroll->SetValue(0.0f);              // FUN_00595db0
        Scroll->SetHorizontal(bHorizontal != 0); // FUN_00597550
    }
    return Scroll;
}

UiScroll* WinControl::CreateUICycle(char const* Name, float const* Position, float Width,
                                    float Height, int Min, int Max, int Page,
                                    unsigned char bHorizontal)
{
    // FUN_005a1870: FUN_005a17e0 instruction for instruction, the factory name "UICycle".
    FUiPayloadScrollCycle Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.Extra0 = static_cast<float>(Min);
    Payload.Extra1 = static_cast<float>(Max);
    Payload.Extra2 = static_cast<float>(Page);
    Payload.Extra3 = 1.0f;
    Payload.Flag = bHorizontal;
    UiScroll* const Cycle = static_cast<UiScroll*>(WinFactoryCreate(Name, "UICycle", this,
                                                                     &Payload));
    if (Cycle != nullptr)
    {
        Cycle->SetScrollOffset(Position);   // FUN_005972d0
        Cycle->SetValue(0.0f);              // FUN_00595db0
        Cycle->SetHorizontal(bHorizontal != 0); // FUN_00597550
    }
    return Cycle;
}

RichTextWin* WinControl::CreateRichTextWin(char const* Name, float Width, float Height,
                                           float const* AttachOffset, bool bAutoScrollbar)
{
    // FUN_005a10e0: no null test on the factory's result (005a10ff MOV ESI,EAX, then straight
    // into FUN_00579cc0).
    RichTextWin* const Text =
        static_cast<RichTextWin*>(WinFactoryCreate(Name, "RichTextWin", this, nullptr));
    Text->SetNormSize(Width, Height);          // FUN_00579cc0
    Text->SetAttachOffset(AttachOffset);       // FUN_00579c40
    Text->SetAutoScrollbar(bAutoScrollbar);    // FUN_005799f0
    return Text;
}

RichTextEditWin* WinControl::CreateUiTextEdit(char const* Name, float const* AttachOffset,
                                              float Width, float Height, wchar_t* Caption)
{
    // FUN_005a1900 (RET 0x14): a stack payload {Width, Height, Caption} (UITextEdit's
    // FUiPayloadText), FUN_0059db20(Name, "UITextEdit", this, &payload), and on a non-null
    // result FUN_00579c40 (SetAttachOffset) with the position.
    FUiPayloadText Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.CaptionUtf16 = Caption;
    RichTextEditWin* const Edit =
        static_cast<RichTextEditWin*>(WinFactoryCreate(Name, "UITextEdit", this, &Payload));
    if (Edit != nullptr)
        Edit->SetAttachOffset(AttachOffset);
    return Edit;
}

WinControl* WinControl::CreateRichTextWireButtonControl(char const* Name, char const* SoundName,
                                                        float const* Position,
                                                        float const* HalfExtents,
                                                        unsigned char bFlag3,
                                                        unsigned char bNoZEnable, int ToolTipIds,
                                                        unsigned CaptionIds, int CommandId,
                                                        int WireFlags)
{
    // FUN_005a1950.
    Win3D* const Created = WinFactoryCreate(Name, "RichTextWireButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        RichTextWireButtonControl* const Wire = static_cast<RichTextWireButtonControl*>(Created);
        Wire->Init(SoundName, Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds, CaptionIds,
                   CommandId, static_cast<unsigned>(WireFlags)); // FUN_005a4810
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateEditControl(char const* Name, char const* StyleName,
                                          float const* Position, char const* Hardpoint,
                                          float Width, float Height, float Arg6, unsigned Ids,
                                          int Dword358, int Dword344)
{
    // FUN_005a0e90: FUN_0059db20(Name, "EditControl", this, 0) -- WINFACTORY<EditControl>
    // DAT_0067d860, G_EditControlFactory in nn_text_control.cpp; on a non-null result
    // TextControl's Init FUN_0058a110(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids,
    // Dword358, Dword344, 0) on it.
    Win3D* const Created = WinFactoryCreate(Name, "EditControl", this, nullptr);
    if (Created != nullptr)
    {
        NnEditControl* const Edit = static_cast<NnEditControl*>(Created);
        Edit->Init(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids, Dword358, Dword344,
                   0); // FUN_0058a110
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateToggleButtonControl(char const* Name, char const* MeshPath,
                                                  char const* AltMeshPath, char const* SoundName,
                                                  char const* KeyCommandName,
                                                  float const* Position, char const* Hardpoint,
                                                  float const* HalfExtents,
                                                  unsigned char bFlag331,
                                                  unsigned char bFlag330Bit3,
                                                  unsigned char bFlag330Bit5,
                                                  unsigned char bNoZEnable, int ToolTipIds,
                                                  int Dword32c)
{
    // FUN_005a0fb0: FUN_0059db20(Name, "ToggleButtonControl", this, 0) -- the WINFACTORY
    // DAT_00679a80 (G_ToggleButtonControlFactory, nn_button_control.cpp); on a non-null result
    // ToggleButtonControl::Init FUN_0055c1c0 with the remaining thirteen arguments in order.
    Win3D* const Created = WinFactoryCreate(Name, "ToggleButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<NnToggleButtonControl*>(Created)->Init(
            MeshPath, AltMeshPath, SoundName, KeyCommandName, Position, Hardpoint, HalfExtents,
            bFlag331, bFlag330Bit3, bFlag330Bit5, bNoZEnable, ToolTipIds, Dword32c);
    }
    return static_cast<WinControl*>(Created);
}

bool WinControlHalfExtentsFromMesh(float* HalfExtents, FClientMeshHolder* Holder)
{
    // FUN_005a1340 (thiscall on HalfExtents, RET 4): FUN_0059f0d0 fills a six-float box on the
    // stack; when it answers 1, HalfExtents = {box[0], box[2], 0} and the result stays 1 (AL
    // from the call), otherwise 0 and HalfExtents is left alone.
    FClientMeshAabb Box;
    if (!ClientMeshQueryAabb(Holder, &Box))
        return false;
    HalfExtents[0] = Box.Values[0];
    HalfExtents[1] = Box.Values[2];
    HalfExtents[2] = 0.0f;
    return true;
}
