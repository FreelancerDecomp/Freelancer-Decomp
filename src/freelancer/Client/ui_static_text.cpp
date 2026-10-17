// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_static_text.h"

#include "ui_page.h"
#include "ui_page_desc.h"
#include "win_factory.h"

UiStaticText::UiStaticText()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // FUN_0059c3d0 0059c40c..0059c4ab (inlined ctor): the Win3D and TextWin parts run in their
    // constructors. +0xec / +0xf0 are left to slot 9 (the zeros are only C++ initialisation).
}

UiStaticText::~UiStaticText()
{
    // FUN_0059cda0 -> the TextWin dtor FUN_005a1af0.
}

bool UiStaticText::Create(Win3D* NewParent, void* Payload)
{
    // FUN_00599ac0 (RET 8).
    Win3DReparent(this, NewParent);                                   // FUN_0059dd00
    Win3DRegister(this);                                              // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

    InitTextDefaults();                                               // FUN_005a1c50

    Flags = static_cast<unsigned char>(Flags & 0xfe);
    Width = 0.1f;                                                     // 0x3dcccccd
    Height = 0.1f;
    FlagE1 = 0;
    SetText(L"StaticText");                                           // vftable+0x70

    if (Payload != nullptr)
    {
        FUiPayloadText const* TextPayload = static_cast<FUiPayloadText const*>(Payload);
        LocalWidth = TextPayload->Right;
        Width = TextPayload->Right;
        LocalHeight = TextPayload->Bottom;
        Height = TextPayload->Bottom;
        SetText(TextPayload->CaptionUtf16);                           // vftable+0x70
        ColorD8 = 0xff000000u;                                        // bytes 0, 0, 0, 0xff
    }
    return true;
}

void UiStaticText::SetTransform(float const* Transform)
{
    // LAB_00599b70 (RET 4): slot 7 (Transform), slot 5 (Transform + 9), then the extent
    // projection through DAT_0067dbc0 with M = the argument block itself (EDI, not this+0x3c)
    // and Z = Transform[11]; +0xb4 / +0xb8 from +0xec / +0xf0.
    SetOrientation(Transform);
    SetPosition(Transform + 9);
    UiWidgetProjectExtent(Transform, Transform[11], LocalWidth, LocalHeight, &Width, &Height);
}

// Retail DAT_0067db70 -- WINFACTORY<UIStaticText> static (LAB_0059c1e0 -> FUN_0059c360; Create
// FUN_0059c3d0); atexit dtor FUN_00579380.
WINFACTORY<UiStaticText> G_UIStaticTextFactory("UIStaticText");
