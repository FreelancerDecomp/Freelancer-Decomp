// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The player / target information window (retail NN_Help, freelancer.exe 0x00474b60..0x004778b0,
// shares NN_Inventory's CHAR_infowindow.cmp mesh). Opened by the NeuroNet "info" main button
// (nn_main_button.cpp); the HUD target panel's view change (FUN_004e4690, 0x004e4708) refills it
// with the viewed object when it is open.
//
// RTTI .?AVNN_Help@@ : WinControl. operator_new(0x33c) (0x004770f8); the ctor is inlined in the
// factory's Create: WinControl's FUN_0059f860, vftable 0x005d0acc, the IDataViewOwner face +0x7c
// its own copy (0x005d0abc) of WinControl's two slots.
// WINFACTORY<NN_Help> DAT_0066dc18 "NN_Help": static init LAB_00474b60 (ctor FUN_00477070 --
// the WINFACTORY ctor inlined for this type: WinFactory head, FUN_0059dab0, vftable 0x005d0ab4 --
// then atexit LAB_00474b80 -> FUN_00579380); Create FUN_004770e0 (operator_new, the ctor, the
// class-name pointer at +0x10, slot 9, a failed Create followed by slot 0 with 1).
//
// vftable 0x005d0acc -- WinControl's (0x005e5e44) with these overrides (raw dword diff):
//   0 FUN_0055e9f0 scalar deleting dtor (shared by ICF; NN_Help has no destructor body of its
//   own), 9 FUN_00474b90 Create, 23 LAB_00475430 Slot23, 42 LAB_00475400 SetProperty.
//
// Layout: retail's -- WinControl (0x32c) and four dwords.

#include "wincontrol_base.h"

#include "client_base_market.h" // FClientMarketGood

#include <list>
#include <stddef.h>
#include <stdint.h>

class RenderDisplayList;
class RichTextWin;
struct IObjRW;

class NnHelp : public WinControl
{
public:
    NnHelp() = default;

    // slot 9 FUN_00474b90 (RET 8) -- the mesh, the close button, the title and the text; then
    // the text: the ship's current target when there is one (and the player is in space), else
    // the base the player is docked at (its system, its infocard, the base goods sections) or,
    // in space, the system, the sector and the zone the player is in. Win3D's Create last.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 23 LAB_00475430 (RET 0xc) -- 0xef02 removes the window from its parent and deletes it
    // (every child first); anything else is WinControl's Slot23.
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_00475400 (RET 0xc) -- a click (0x26) on the close button closes the NeuroNet
    // screen (FUN_0045a460) and returns 1; everything else returns 0 (WinControl's SetProperty
    // is not called).
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_004754a0 (thiscall, RET 4) -- fill the title and the text with what the player knows
    // of Object: its name (or, never visited, its type and ids 0xd6ef), pilot, attitude, hull,
    // shield, group, affiliation, waypoint number, the base goods sections of its dock base and
    // the infocards. Nothing at all for a null Object, one without a CObject, or a non-CSimple.
    // Callers: Create 0x00474d39, HUD_Target 0x004e4708.
    void ShowObjectInfo(IObjRW* Object);

    WinControl* CloseButton;  // +0x32c "PlayerInfoCloseButton"
    RichTextWin* Text;        // +0x330 "PlayerInfoText"
    WinControl* Title;        // +0x334 "PlayerInfoTitle"
    unsigned Field_0x338;     // +0x338 TODO: not written or read by any NN_Help code (the
                              //        ctor leaves it unwritten)

private:
    NnHelp(NnHelp const&) = delete;
    NnHelp& operator=(NnHelp const&) = delete;
};

static_assert(offsetof(NnHelp, CloseButton) == 0x32c, "NN_Help +0x32c");
static_assert(offsetof(NnHelp, Text) == 0x330, "NN_Help +0x330");
static_assert(offsetof(NnHelp, Title) == 0x334, "NN_Help +0x334");
static_assert(sizeof(NnHelp) == 0x33c, "retail NN_Help is 0x33c bytes (operator_new 0x004770f8)");

// FUN_00476080 (cdecl) -- the base goods sections of an info card, appended to Card for BaseId
// (nothing, not even the style, when no flag is set): a StyleNode 0x9ca4, then
//   bCommodities: title 0x668 -- every stocked commodity sold at or below its good_buy price,
//                 with the price; title 0x667 -- every good of GoodList whose price here is at or
//                 above its good_sell price, with the price;
//   bEquipment:   title 0x66a -- every stocked piece of equipment except ammunition (hp type 3);
//   bShips:       title 0x669 -- every ship for sale (ships first in the card).
// Each list ends with ids 0x67b ("none") when it named nothing. The prices come from a market
// filled for the occasion with the base's price variance (a random draw, FUN_00437de0).
// Callers: NN_Help::Create 0x0047514a, NN_Help::ShowObjectInfo 0x00475c96, the nav map's info
// card FUN_00497970 (0x00497bb3).
void NnHelpAppendBaseGoods(RenderDisplayList* Card, unsigned BaseId, unsigned char bCommodities,
                           unsigned char bEquipment, unsigned char bShips);

// FUN_00475dd0 (cdecl) -- order Goods by the equipment name of each good (_wcsicmp, bubble
// passes until one swaps nothing); a good without equipment sorts as an empty name. The nodes
// are rebuilt in the new order. Callers: FUN_00476080 0x004764d8 / 0x00476aca.
void NnHelpSortGoodsByName(std::list<FClientMarketGood const*>* Goods);
