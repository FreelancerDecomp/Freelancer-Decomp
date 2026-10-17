// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_list_box.h"

#include "ui_page.h"
#include "ui_page_desc.h"
#include "win_factory.h"

UiListBox::UiListBox()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // FUN_0059c810 0x0059c84b..0x0059c850: the ListBox ctor, then the UIListBox vftable.
    // +0x8b0 / +0x8b4 are left unset by retail; the port zeroes them.
}

UiListBox::~UiListBox()
{
    // FUN_0059ce00: the ListBox dtor (thunk to FUN_005a1fe0) and nothing of its own.
}

bool UiListBox::Create(Win3D* NewParent, void* Payload)
{
    // FUN_0059a490 (RET 8): ListBox's resets and registration (LAB_005a2070 inlined), then
    // the payload's width / height into both the unprojected and the projected extent.
    SelectedIndex = -1;
    HoverIndex = -1;
    Field80 = -1;
    ItemCount = 0;
    VisibleRows = 0x10;
    FirstVisible = 0;
    bDrawFrame = 1;
    bDrawRowRules = 0;
    bSelectable = 1;
    Win3DReparent(this, NewParent); // FUN_0059dd00
    Win3DRegister(this);            // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

    if (Payload != nullptr)
    {
        FUiPayloadBounds const* Desc = static_cast<FUiPayloadBounds const*>(Payload);
        LocalWidth = Desc->Right;
        Width = Desc->Right;
        LocalHeight = Desc->Bottom;
        Height = Desc->Bottom;
    }
    return true;
}

void UiListBox::SetTransform(float const* Transform)
{
    // LAB_0059a530 (RET 4): slot 7 SetOrientation(Transform), slot 5 SetPosition(Transform + 9),
    // then the shared extent projection through DAT_0067dbc0 with the new Z (Transform[11]).
    SetOrientation(Transform);
    SetPosition(Transform + 9);
    UiWidgetProjectExtent(Orientation, Transform[11], LocalWidth, LocalHeight, &Width, &Height);
}

// Retail DAT_0067daec -- WINFACTORY<UIListBox> static (LAB_0059c270 -> FUN_0059c7a0; Create
// FUN_0059c810); atexit dtor FUN_00579380.
WINFACTORY<UiListBox> G_UIListBoxFactory("UIListBox");
