// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIListBox -- PAGE_*.txt list widget (RTTI .?AUUIListBox@@ : ListBox : Win3D).
//
// Retail:
//   WINFACTORY<UIListBox> DAT_0067daec, static init LAB_0059c270 -> FUN_0059c7a0("UIListBox")
//   Create FUN_0059c810: operator_new(0x8b8) + ListBox ctor FUN_005a2100, then vftable
//   0x005e5cb4 -- UIListBox has no constructor body of its own. ListBox is 0x8b0 bytes (its
//   by-value copies in NN_LoadSave settle that, list_box.h), so the two floats at +0x8b0 /
//   +0x8b4 -- written only by slot 9 and read only by slot 8 -- are UIListBox's; retail's ctor
//   leaves them unset.
//   vftable 0x005e5cb4 overrides of ListBox 0x005e5f9c:
//     slot 0  FUN_0059ce00  scalar deleting destructor (ListBox dtor + operator_delete)
//     slot 8  LAB_0059a530  SetTransform + extent projection
//     slot 9  FUN_0059a490  Create from the page payload
//     slot 13 0x0059a6a0    a debug frame guarded by DAT_0067dbc4, then JMP ListBox::Draw
//                           (FUN_005a2360). DAT_0067dbc4 has four readers and no writer in
//                           the PE (no instruction and no pointer to it outside those reads),
//                           so the .bss byte stays 0 and slot 13 behaves as ListBox::Draw --
//                           the port does not override it.

#include "list_box.h"

class UiListBox : public ListBox
{
public:
    UiListBox();
    ~UiListBox() override;

    // slot 8 (+0x20) -- LAB_0059a530.
    void SetTransform(float const* Transform) override;
    // slot 9 (+0x24) -- FUN_0059a490.
    bool Create(Win3D* NewParent, void* Payload) override;

    float GetWidth() const { return Width; }
    float GetHeight() const { return Height; }

    float LocalWidth;                       // +0x8b0 -- unprojected width (slot 9 payload)
    float LocalHeight;                      // +0x8b4 -- unprojected height (slot 9 payload)

private:
    UiListBox(UiListBox const&) = delete;
    UiListBox& operator=(UiListBox const&) = delete;
};

static_assert(sizeof(UiListBox) == 0x8b8, "FUN_0059c810 allocates 0x8b8");
static_assert(offsetof(UiListBox, LocalWidth) == 0x8b0, "UIListBox +0x8b0");
