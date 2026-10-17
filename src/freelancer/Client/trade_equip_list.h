// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The item lists of the multiplayer player-to-player trade window (NN_Trade, 0x004bfb60..).
//
// CTradeEquipListLine (RTTI .?AVCTradeEquipListLine@@, no base; operator_new(0x2c)) -- one row:
// an item-view frame with its icon mesh, an info frame, an optional "?" info-card button, the
// item's name and count texts and a health bar. vftable 0x005d6028 holds only the scalar
// deleting destructor LAB_004be7c0 (the widgets belong to the parent's Win3D tree and are not
// destroyed with the row). The constructor is inlined at every allocation site (0x004bf257,
// 0x004bf2f2, NN_Trade 0x004c0d91): vftable, +0x04 owner list, +0x24 = 0.
//
// CTradeEquipList (RTTI .?AVCTradeEquipList@@, no base; operator_new(0x68)) -- a fixed number
// of rows over an EquipDescList, scrolled by a ScrollControl. vftable 0x005d628c holds only the
// scalar deleting destructor FUN_004bf520. Built by NN_Trade's Create (0x004c18b9, 0x004c1911,
// 0x004c1970). NN_Trade keeps three of them (+0x330 / +0x334 / +0x338) and drives everything
// else: the scroll value (0x004c3775..0x004c378c writes +0x2c and refreshes), the selected item
// +0x58, the select mode +0x5d and the cargo volume +0x60 (0x004c2220, 0x004c2380).
//
// Container note: +0x20 is an EquipDescList, a std::list<EquipDesc> as in retail, so element
// addresses never move. The rows (+0x24) and NN_Trade's selection (+0x58, NN_Trade +0x428) keep
// raw pointers into it across inserts of other items, exactly as retail does; only erasing the
// element itself invalidates such a pointer.

#include "wincontrol_base.h"

#include "common_archetype.h"
#include "common_goods.h"

#include <stddef.h>
#include <vector>

class CTradeEquipList;
class CargoBarControl;
class NnButtonControl;

// DAT_006724b8 -- the "%s-%06d" widget-name buffer (0x400 bytes) and DAT_006728b8, the counter
// that makes each name unique. Shared by CTradeEquipListLine::Init, the CTradeEquipList ctor
// ("ScrollBar") and NN_Trade's Create (0x004c0655..0x004c1836).
extern char G_TradeWidgetName[0x400];
extern int G_TradeWidgetNameCounter;

// The inlined sprintf(G_TradeWidgetName, "%s-%06d", Base, G_TradeWidgetNameCounter++) that
// names every widget of the trade window (s_%s-%06d_005d601c).
void FormatTradeWidgetName(char const* Base);

// FUN_004be6c0 (cdecl) -- the icon mesh, its material library and the two resource ids of an
// item. Both strings are cleared first. With a good (GoodList::find_by_archetype):
//   mesh = item_icon (+0x78) when it is set and bShopArchetype is false, and then the material
//   string is left cleared; otherwise mesh = shop_archetype (+0x74) and material =
//   material_library (+0x7c), or when that is null the first material path of the equipment's
//   client companion; ids = the good's ids_name / ids_info (+0x88 / +0x8c).
// Without a good: the equipment archetype's DA archetype, the companion's first material path
// and the archetype's ids -- or nothing at all (not even the ids) when there is no archetype.
// Callers: CTradeEquipListLine::SetItem (0x004bef9e, bShopArchetype 0), NN_Trade 0x004c2856.
void GetTradeItemVisuals(EquipDesc const* Item, CacheString* OutMesh, CacheString* OutMaterial,
                         int* OutIdsName, int* OutIdsInfo, bool bShopArchetype);

class CTradeEquipListLine
{
public:
    explicit CTradeEquipListLine(CTradeEquipList* NewOwner);
    // LAB_004be7c0 -- nothing beyond the vftable reset.
    virtual ~CTradeEquipListLine();

    // FUN_004be7e0 (RET 8) -- build the row's widgets under Parent around Position.
    void Init(WinControl* Parent, float const* Position);
    // FUN_004bed40 -- SetProperty(1, 1, 0) (hide) on every widget; the info card when it exists.
    void Hide();
    // FUN_004bedc0 -- show and enable the frame and icon, show the other widgets, hide the card.
    void Show();
    // FUN_004bee70 (RET 4) -- bits 2 and 3 of +0x331 of both frames = bOn.
    void SetHighlight(unsigned char bOn);
    // FUN_004bef00 (RET 4) -- show Item in this row (a null archetype is the credits entry).
    void SetItem(EquipDesc* NewItem);

    CTradeEquipList* Owner;       // +0x04
    NnButtonControl* IconFrame;   // +0x08 "IconFrame" -- TRADE_item_view
    NnButtonControl* InfoFrame;   // +0x0c "InfoFrame" -- TRADE_plyr_item_info
    NnButtonControl* InfoCard;    // +0x10 "InfoCard" -- TRADE_question; null unless the owner has one
    WinControl* IconMesh;         // +0x14 "IconMesh" -- StaticControl
    WinControl* ItemName;         // +0x18 "ItemName" -- TextControl
    WinControl* ItemCount;        // +0x1c "ItemCount" -- TextControl
    CargoBarControl* HealthBar;   // +0x20 "CargoBar" -- CargoBarControl, factory-built, no Init
    EquipDesc* Item;              // +0x24
    unsigned char bInUse;         // +0x28 SetItem sets it, Refresh clears it; not set by the ctor
    unsigned char Pad29[3];

private:
    CTradeEquipListLine(CTradeEquipListLine const&) = delete;
    CTradeEquipListLine& operator=(CTradeEquipListLine const&) = delete;
};

class CTradeEquipList
{
public:
    // FUN_004bf120 (RET 0x28). RowsHp is the parent hardpoint of the first row; the others step
    // down by the info frame's height. The five scroll-bar hardpoints go to CreateScrollControl
    // as the back plate, the up (prev) and down (next) arrows and the thumb's two ends.
    CTradeEquipList(WinControl* NewParent, char const* RowsHp, char const* ScrollBackHp,
                    char const* ScrollUpHp, char const* ScrollDownHp, char const* ThumbTopHp,
                    char const* ThumbBottomHp, int NewVisibleRows, unsigned char bNewInfoCard,
                    unsigned char bNewHighlight);
    // FUN_004bf540 (scalar deleting FUN_004bf520).
    virtual ~CTradeEquipList();

    // FUN_004bf620 (RET 4) -- the credits entry (the item with archetype 0) takes Count; with
    // Count 0 it is removed, with no entry one is added from CreditsTemplate. Returns Count.
    int SetMoney(int Count);
    // FUN_004bf720 -- the list becomes the player's credits (when non-zero) and every item of
    // the player's EquipDescList that is neither mounted nor a mission item; the scroll range
    // follows.
    void Rebuild();
    // FUN_004bf850 (RET 4) -- Index < 0: every row from the scroll position (+0x2c) on, unused
    // rows blanked; otherwise only the row showing list item Index (when it is on screen).
    void Refresh(int Index);

    unsigned Field04;             // +0x04 -- never written by the ctor; TODO: meaning
    WinControl* ScrollBar;        // +0x08 "ScrollBar" -- ScrollControl, Field2f0 = this
    WinControl* Parent;           // +0x0c
    std::vector<CTradeEquipListLine*> Lines; // +0x10 VC6 vector (allocator byte + 3 pointers)
    EquipDescList Items;          // +0x20 std::list<EquipDesc> (see note above)
    int TopIndex;                 // +0x2c -- the first list item shown; NN_Trade sets it
    int VisibleRows;              // +0x30
    unsigned Field34;             // +0x34 -- never written by the ctor; TODO: meaning
    EquipDesc CreditsTemplate;    // +0x38 -- the credits entry SetMoney adds (count = credits)
    EquipDesc* SelectedItem;      // +0x58 -- NN_Trade's selection (it clears the byte at +0)
    unsigned char bInfoCard;      // +0x5c -- rows get an InfoCard button
    unsigned char bSelectMode;    // +0x5d -- NN_Trade sets it; Refresh shows only frame/icon/bar
    unsigned char bHighlight;     // +0x5e -- SetHighlight value for every row outside select mode
    unsigned char Pad5f;
    double CargoVolume;           // +0x60 -- NN_Trade (0x004c22a1 get_cargo_space_occupied)

private:
    CTradeEquipList(CTradeEquipList const&) = delete;
    CTradeEquipList& operator=(CTradeEquipList const&) = delete;
};
