// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The commodity-by-base price table of the NeuroNet inventory (the "NN_CommodityByBase" window,
// WINFACTORY DAT_0066dacc, ctor FUN_0046ef80) and the inventory info card's trade report
// (FUN_004704d0), which read the same three tables:
//
//   DAT_0066dabc  std::vector of 0xc-byte rows (static init LAB_0046eda0, dtor LAB_0046edd0)
//   DAT_0066daa4  std::map space-object id -> base name   (static init LAB_0046ee10 via
//                 FUN_004706b0, dtor LAB_0046ee40)
//   DAT_0066da90  std::map space-object id -> system name (static init LAB_0046ef30 via
//                 FUN_004706b0, dtor LAB_0046ef60)
//
// The rows are keyed by the base's space object (Universe::IBase::SpaceObjectId, retail BASE
// vt+0x5c), the id the player's visit map is keyed by too. Retail's map value is a two-dword
// wide string {wchar_t* Ptr; unsigned Capacity} assigned with FUN_00413df0 (malloc / realloc
// + wcsncpy) and freed with free() (FUN_0046fa50); the port keeps std::wstring.
//
// NN_Inventory opens the window with the selected commodity's archetype (its "trade routes"
// button, 0x0047a9bb); a row click closes it and hands the row's space object to the nav map
// (FUN_004a2aa0).

#include "common_text.h" // RenderDisplayList
#include "event_handler.h"
#include "wincontrol_base.h"

#include <map>
#include <stddef.h>
#include <string>
#include <vector>

class UiScroll;

// One row of DAT_0066dabc (0xc bytes).
struct FCommodityByBaseRow
{
    unsigned SpaceObjectId; // +0x00 the base's space object (IBase vt+0x5c)
    int Price;              // +0x04 ftol of the market price, or of GoodInfo::Price
    bool bBaseSells;        // +0x08 ids 0x3e5 " - Base Sells this Item"
};

extern std::vector<FCommodityByBaseRow> G_CommodityByBaseRows;         // DAT_0066dabc
extern std::map<unsigned, std::wstring> G_CommodityByBaseBaseNames;    // DAT_0066daa4
extern std::map<unsigned, std::wstring> G_CommodityByBaseSystemNames;  // DAT_0066da90

// FUN_004a2310 (cdecl) -- false for ids 0 and for the ten base-name ids 0x3fc27, 0x3fc2e,
// 0x3fc28, 0xeb41, 0xeb3f, 0x300ea, 0x30052, 0x3009c, 0x3009d, 0x300bf; true otherwise.
bool IsCommodityByBaseListedName(int Ids);

// FUN_0046f6d0 (cdecl) -- empty the three tables, then one row for every base whose space
// object the player knows (visit flag 4), with the price of ArchId's good there.
void BuildCommodityByBaseRows(unsigned ArchId);

// FUN_00470040 (cdecl) -- bubble-sort the rows by Column: 1 price (high first) then base
// name, 2 base name, 3 system name then price then base name, 4 base-sells first then price;
// any other value leaves the rows as they are.
void SortCommodityByBaseRows(int Column);

// FUN_004704d0 (cdecl) -- the info card's trade report for ArchId: rebuild the rows, sort
// them by price, and append one "\n<price> <base>, <system>[ sells][ best]" line per row.
void AppendCommodityTradeReport(RenderDisplayList* Card, unsigned ArchId);

// One visible line of the window: four TextControls ("ItemName" style), 0x10 bytes.
struct FCommodityByBaseRowControls
{
    WinControl* Price;   // +0x0 "CommodityByBasePrice%d"
    WinControl* BuySell; // +0x4 "CommodityByBuySellPrice%d"
    WinControl* Base;    // +0x8 "CommodityByBaseBase%d"
    WinControl* System;  // +0xc "CommodityByBaseSystem%d"
};

// Retail NN_CommodityByBase (RTTI .?AVNN_CommodityByBase@@ : WinControl, Event::Handler).
//
// WINFACTORY<NN_CommodityByBase> DAT_0066dacc "NN_CommodityByBase": static init LAB_0046ed70
// (FUN_00470720 + atexit LAB_0046ed90 -> FUN_00579380), Create FUN_00470790
// (operator_new(0x424), WinControl ctor FUN_0059f860, the vptrs, no field written, slot 9).
//
// vftable 0x005d027c (45 slots) -- WinControl's with these overrides:
//   0 FUN_00470850 (scalar deleting dtor around FUN_00470870: FUN_00576010 on +0x32c, then
//     FUN_0059fa50 -- the two base destructors), 9 FUN_0046ef80 Create, 11 LAB_0046fd20 Update,
//   42 LAB_0046f550 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005d026c; Event::Handler face +0x32c vftable 0x005d0264
// (FUN_0046ffe0).
class NnCommodityByBase : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_00470790: WinControl's ctor and the vptrs; no member is written before
    // Create, so none is here.
    NnCommodityByBase();
    // FUN_00470870 -- only the base destructors.
    ~NnCommodityByBase() override;

    // slot 9 FUN_0046ef80 (RET 8) -- build the table for the archetype in Data.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0046fd20 -- close when the window may not stand, else update the children
    // and colour the column titles and the rows under the mouse.
    void Update() override;
    // slot 42 LAB_0046f550 (RET 0xc) -- the scroll bar (0x3e) and the clicks (0x26 / 0x3e).
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face FUN_0046ffe0 (RET 4) -- event 0xf closes the window.
    bool OnEvent(unsigned MsgId) override;

    // FUN_0046fa80 -- fill the 13 visible lines from FirstRow and set up the scroll bar.
    void RefreshRows();

    unsigned ArchId;                      // +0x330 Create's Data
    WinControl* CloseButton;              // +0x334 "CommodityByBaseCloseButton"
    WinControl* PriceTitle;               // +0x338 "CommodityByBasePriceTitle"   (column 1)
    WinControl* BaseTitle;                // +0x33c "CommodityByBaseBaseTitle"    (column 2)
    WinControl* SystemTitle;              // +0x340 "CommodityByBaseSystemTitle"  (column 3)
    WinControl* BuySellTitle;             // +0x344 "CommodityByBaseBuySellTitle" (column 4)
    UiScroll* Scroll;                     // +0x348 "CommodityByBaseScroll"
    int SortColumn;                       // +0x34c
    int FirstRow;                         // +0x350
    FCommodityByBaseRowControls Rows[13]; // +0x354

private:
    // The close sequence of the close button, a row click, Update and OnEvent: every child tree
    // (FUN_0043d2a0), slot 10 Destroy, then the scalar deleting destructor.
    void Close();

    NnCommodityByBase(NnCommodityByBase const&) = delete;
    NnCommodityByBase& operator=(NnCommodityByBase const&) = delete;
};

static_assert(sizeof(NnCommodityByBase) == 0x424,
              "retail NN_CommodityByBase is 0x424 bytes (operator_new 0x004707a8)");
static_assert(offsetof(NnCommodityByBase, ArchId) == 0x330, "NN_CommodityByBase +0x330");
static_assert(offsetof(NnCommodityByBase, Scroll) == 0x348, "NN_CommodityByBase +0x348");
static_assert(offsetof(NnCommodityByBase, Rows) == 0x354, "NN_CommodityByBase +0x354");
