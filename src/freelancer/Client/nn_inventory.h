// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Inventory (RTTI .?AVNN_Inventory@@ : WinControl) -- the NeuroNet inventory window:
// the player's ship in a ShipViewControl, the five-row item list (name / view / price /
// quantity / button / mesh / "good deal" / damage / equip toggle / mission text), the filter
// buttons, the info card and the jettison button. NN_Dealer (nn_dealer.h) and NN_Scanner
// (HUD, W3-D) derive from it.
//
// WINFACTORY<NN_Inventory> "NN_Inventory": static init FUN_00485670 (WinFactory head) with
// vftable 0x005d1d20; Create FUN_004856e0 (operator_new(0x974), the ctor inlined, then slot 9).
//
// vftable 0x005d0eec (47 slots) -- WinControl's 45 with these overrides:
//    0 FUN_0047fd30 scalar deleting dtor (dtor FUN_00477da0)
//    5 FUN_0047fbb0 SetPosition     9 FUN_0047b3e0 Create       11 FUN_0047b310 Update
//   13 FUN_00479700 Draw           23 FUN_0047ade0 Slot23       42 FUN_00479d10 SetProperty
// and two slots of its own:
//   45 (+0xb4) FUN_00478100 -- builds the window's controls (called by BindControls on the
//      first Update after Create); NN_Dealer overrides it with FUN_00480d10
//   46 (+0xb8) FUN_0044fd00 -- `XOR AL,AL / RET`; NN_Scanner overrides it with FUN_004b6c50
//      (`MOV AL,1 / RET`). Read at 0x0047f6bf.
// IDataViewOwner face +0x7c vftable 0x005d0ee0 (LAB_0053aaa0 / FUN_0053e4a0, as WinControl).
//
// Retail layout (size 0x974). The port keeps the members in retail order; the std::list
// members are modern containers, so offsets past +0x3a8 no longer match retail and only the
// WinControl base is asserted. Retail offsets stay in the comments because every listing
// reference is by offset.

#include "wincontrol_base.h"

#include "common_goods.h"

#include <stddef.h>
#include <stdint.h>

class ShipViewControl;
class NnDealer;
class RichTextWin;

// The member at NN_Inventory+0x330 (ctor FUN_004c6140, dtor FUN_004c6200). No vftable and no
// RTTI; its methods sit at 0x004c6140..0x004c6f70. It pairs the ShipViewControl that shows the
// ship with the ship archetype and the loadout being shown (the player's, copied in by
// FUN_004c62b0), and the five ship-view labels BindControls looks up.
//
// Retail layout (0x2c):
//   +0x00 ShipViewControl* ("ShipView_Control")
//   +0x04 "ShipView_Name"  +0x08 "ShipView_DamageBar"  +0x0c "Shipview_Frame"
//   +0x10 "ShipView_Toggle"  +0x14 "ShipView_Info"      (all WinControl*)
//   +0x18 ship archetype id
//   +0x1c EquipDescList (VC6 std::list<EquipDesc>: allocator byte, head, size)
//   +0x28 byte -- FUN_004c62b0's second argument
struct FInventoryShipModel
{
    // FUN_004c6140
    FInventoryShipModel();
    // FUN_004c6200 (the list clear + node free; the byte and the view are left alone)
    ~FInventoryShipModel();

    // FUN_004c6250 -- empty the loadout, +0x28 = 0, and clear the view's selection.
    void Clear();
    // FUN_004c62b0 (RET 8) -- show the ship ShipArchId with Source's loadout: reset the view,
    // store bFlag, load the hull and Source into the view, rebuild the loadout sorted, stamp
    // each good's owner, refresh the name label and the damage bar.
    void LoadShip(EquipDescList const* Source, unsigned char bFlag);
    // FUN_004c63e0 (RET 8) -- the ship's hold size and the space the loadout occupies.
    void GetCargoCapacityAndUsed(float* OutCapacity, float* OutUsed) const;
    // FUN_004c6430 (RET 8) -- take Count (0: all) of the loadout item Desc points at; false
    // when Desc is not an item of the loadout.
    bool RemoveItem(EquipDesc const* Desc, int Count);
    // FUN_004c64b0 (RET 0xc) -- add Count (0: Desc's) of Desc to the loadout, unequipped.
    bool AddItem(EquipDesc* Desc, int Count, bool bPlaySound);
    // FUN_004c6530 (RET 0x10) -- mount Count (0: Desc's) of Desc on Hardpoint (the cargo bay or
    // one of the ship's hardpoints); false when it does not fit or the hardpoint is unknown.
    bool MountItem(EquipDesc* Desc, CacheString const* Hardpoint, int Count, bool bPlaySound);
    // FUN_004c6860 (RET 4, this unused) -- the UI sound of an equipment class.
    static void PlayMountSound(unsigned ArchId);
    // FUN_004c6910 (RET 8) -- move the loadout item Desc to the hardpoint named Hardpoint.
    void SetItemHardpoint(EquipDesc* Desc, char const* Hardpoint);
    // FUN_004c6a50 (RET 0xc) -- point the view at Item (null clears it). An internal item is
    // shown by its hardpoint class when Mode is 0 or 2, otherwise by good; a mounted one by its
    // archetype and the instance on its hardpoint. bFlag goes to the view's select calls.
    bool ShowItem(EquipDesc const* Item, bool bFlag, int Mode);
    // FUN_004c6b80 (RET 4) -- replace the loadout with Source's items in display order.
    void SortFrom(EquipDescList const* Source);
    // FUN_004c6e70 -- hand the loadout to the view again.
    void RefreshView();
    ShipViewControl* ShipView;  // +0x00
    WinControl* NameText;       // +0x04
    WinControl* DamageBar;      // +0x08
    WinControl* Frame;          // +0x0c
    WinControl* Toggle;         // +0x10
    WinControl* InfoText;       // +0x14
    unsigned ShipArchId;        // +0x18
    EquipDescList Loadout;      // +0x1c
    unsigned char bFlag28;      // +0x28

private:
    FInventoryShipModel(FInventoryShipModel const&) = delete;
    FInventoryShipModel& operator=(FInventoryShipModel const&) = delete;
};

// Result callback at +0x500 (cdecl): called by Slot23 0xef02 with (byte +0x4e5, +0x504).
typedef void (*FNnInventoryResultFn)(unsigned char Result, void* Arg);

// Number of item rows the window shows at once (every per-row array below is [5]).
enum
{
    kNnInventoryRows = 5
};

class NnInventory : public WinControl
{
public:
    // FUN_00477cf0 -- WinControl ctor, +0x330 FUN_004c6140, +0x394 CacheString ctor,
    // +0x3a8 list, +0x94c EquipDesc ctor, vptrs, ResetFields.
    NnInventory();
    // FUN_00477da0
    ~NnInventory() override;

    // --- Win3D / WinControl overrides ---
    // slot 5 FUN_0047fbb0
    void SetPosition(float const* NewPosition) override;
    // slot 9 FUN_0047b3e0
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_0047b310
    void Update() override;
    // slot 13 FUN_00479700
    void Draw() override;
    // slot 23 FUN_0047ade0 -- 0xef02 closes the window, 0xef01 shows it
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 FUN_00479d10
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // --- NN_Inventory's own slots ---
    // slot 45 (+0xb4) FUN_00478100 -- build the controls
    virtual void Slot45();
    // slot 46 (+0xb8) FUN_0044fd00 -- false
    virtual bool Slot46();

    // FUN_00477ee0 -- the field reset every ctor runs (NN_Dealer's twice).
    void ResetFields();
    // FUN_0047ad20 -- close the window: hide the partner dealer (trade mode 0), show the
    // player's loadout again, run the result callback once, then unlink, delete the subtree,
    // Destroy and delete this. Slot23 0xef02 runs the same body (inlined in retail); NN_Scanner
    // calls it at 0x004b719a / 0x004b71ef.
    void Close();
    // FUN_0047b410 -- first Update after Create: slot 45, then look every control up by name.
    void BindControls();
    // FUN_004795f0 (RET 4) -- show (bShow) or hide the eight +0x91c..+0x938 controls and hide
    // (bShow) or show the info card pair +0x4bc / +0x944.
    void ShowQuantityControls(bool bShow);
    // The sequence the listing inlines after list changes: +0x3a4 = -1, +0x4f4 = 0,
    // FUN_004c6a50(0, 0, 5) on +0x330, FUN_004795f0(0), the dealer's +0x4e8 / +0x4ec = 0.
    void ClearSelection();
    // FUN_0047e590 -- the five filter buttons' slot 42 (0xc, Mode == n, 0).
    void UpdateFilterButtons();
    // FUN_0047f240 (RET 8) -- switch the list filter: 0 weapons, 1 ammo, 2 equipment,
    // 3 cargo bay, 4 commodity.
    void SetMode(int NewMode, bool bPropagate);
    // FUN_0047d2f0 -- rebuild the item rows from List.
    void RefreshItemList(EquipDescList const* List, int Arg2, unsigned char bArg3);
    // FUN_0047af30 -- after a ship purchase: load the player's list into the dealer's model,
    // then move each of its items (most valuable first) onto this window's ship, as many as
    // fit; clear both windows' selection.
    void TransferEquipmentFromDealer();
    // FUN_0047e920 -- ship dealer: move the dealer's selected item (the dealer's +0x988 of it)
    // onto this window's ship model -- on the hardpoint the ship view offers, else the cargo
    // bay -- and remove it from the dealer's model when it fitted; both selections reset.
    void TakeDealerSelection();
    // FUN_0047eab0 (RET 4) -- buy Count of the dealer's selected market good: the market
    // (DAT_00668708) block, then remember the good (+0x3bc) and flag the list for refresh.
    void BuyFromDealer(int Count);
    // FUN_0047eea0 (RET 4) -- sell Count of the selected item to the base market, then the
    // NavBar refresh FUN_004427e0(0).
    void SellSelected(int Count);
    // FUN_0047f040 (RET 4) -- the market price of GoodId plus, for a gun with an ammo package
    // (GoodInfo +0xb8 / +0xbc), the price of the ammo that would come with it; 0 without a
    // market at the current base.
    int GetMarketPackagePrice(unsigned GoodId);
    // FUN_0047fb40 (RET 4) -- whether the ship model holds an equipped item of ArchId's class.
    bool HasEquippedOfClass(unsigned ArchId) const;
    // FUN_0047e790 -- the loadout item the selected row (+0x3a4) shows, cached in +0x4f4; null
    // when nothing is selected. Own window: the ship view's hardpoint rows of the current mode
    // first, then the unmounted items of the mode; a dealer in trade mode 2 counts every mode.
    EquipDesc* GetSelectedItem();
    // FUN_0047e0e0 (RET 8) -- select row Row of the page (-1: rewind the list to its top and
    // select nothing), point the ship view at the row's item and reset the partner window's
    // selection. The second argument is never read (every caller pushes 1).
    void SelectRow(int Row, int Unused);
    // FUN_0047e270 (RET 4) -- select the list row whose item sits on hardpoint Name, switching
    // to the filter mode of that hardpoint's type first; the previous selection is restored
    // when no row matches.
    void SelectHardpoint(char const* Name);
    // FUN_0047e650 -- the jettison button: drop the selected unmounted item at once when it is a
    // single one, otherwise open the quantity controls.
    void JettisonSelected();
    // FUN_0047f720 (RET 8) -- an equip toggle clicked on Item. State is the toggle's state
    // (slot 42 0x2f): for items on hardpoints (weapons / equipment) nonzero moves the item into
    // the cargo bay, zero mounts it on a free hardpoint; for the rest nonzero unequips it and
    // zero equips it, unequipping every other item of its class.
    void ToggleEquip(EquipDesc* Item, int State);
    // FUN_0047e440 (RET 4, this unused) -- NnItemFilterMode of Desc's archetype, 5 for no Desc.
    static int GetItemFilterMode(EquipDesc const* Desc);
    // FUN_0047e9e0 -- empty the info card: this window's +0x4bc (only on a dealer), the partner
    // dealer's +0x4bc and the three dealer panes +0x398..+0x3a0 go back to the top and lose
    // their text; +0x4e8 here and on the partner dealer = 0.
    void ClearInfoCard();
    // FUN_0047ba90 (RET 8) -- show archetype ArchId in the info card: the name label (+0x4b8),
    // then a ship's three info texts in the dealer panes, or an item's info with its stat table
    // (two columns for weapons / ammo / shields / thrusters etc.) or trade text. ArchId 0 shows
    // the plain text Ids instead (0: nothing). Nothing happens while the quantity controls show.
    void ShowInfoCard(unsigned ArchId, unsigned Ids);
    // FUN_0047f3e0 (RET 4) -- show or hide each row's controls for a list of Count entries (the
    // price / good-deal columns only on a dealer that trades them), and the trade-route pair.
    void UpdateRowVisibility(int Count);
    // FUN_0047c740 (RET 0x34) -- list entry *Counter of RefreshItemList: skipped (false, no
    // count) when FilterMode is neither the window's mode nor -1; otherwise, when the entry
    // falls on the page, fill that row's name / quantity / price / mission / damage / toggle /
    // good-deal / view / mesh / colours (and the dealer's transfer panel for the selected
    // entry), then ++*Counter and return whether the entry is the selected one. EmptyIds is the
    // caption of an empty hardpoint row (ArchId 0); HpClass is never read.
    bool FillItemRow(int* Counter, unsigned ArchId, int FilterMode, int Count, int Price,
                     int Health, unsigned char bShowToggle, unsigned char bEquipped,
                     unsigned char bMission, unsigned char bUsable, int EmptyIds, int HpClass,
                     unsigned char bAlwaysShowCount);

    // --- retail members, in order (retail offsets) ---
    int Mode;                         // +0x32c -- 0 / 2 / 4 (Draw, 0x0047f6a4)
    FInventoryShipModel Ship;         // +0x330
    unsigned char bDealerMode;        // +0x35c -- NN_Dealer's ctor sets 1
    unsigned char Pad35d[3];
    NnDealer* Dealer;                 // +0x360 -- the dealer window this inventory sits next to
    // +0x364..+0x37c: the dealer's "transfer item" panel -- NN_Dealer's slot 45 FUN_00480d10
    // builds them (0x004817ff..0x00481a8e); SetProperty 0x48 copies the dealer's pointers into
    // the partner inventory (0x0047a7a0..0x0047a7f8); FUN_0047c740 fills them for the selected
    // row when +0x36c..+0x37c are all set.
    WinControl* TransferItemButton;   // +0x364 "TransferItemButton"
    WinControl* TransferItemView;     // +0x368 "TransferItemView"
    WinControl* TransferItemMesh;     // +0x36c "TransferItemMesh"
    WinControl* TransferItemName;     // +0x370 "TransferItemName"
    WinControl* TransferItemDamage;   // +0x374 "TransferItemDamage" (cargo bar)
    WinControl* TransferItemPrice;    // +0x378 "TransferItemPrice"
    WinControl* TransferItemGoodDeal; // +0x37c "TransferItemGoodDeal"
    WinControl* TransferPriceText;    // +0x380 "TransferPriceText" -- NN_Dealer's slot 45 stores
                                      // it on the dealer (0x00481cc0); not written by the ctor
    WinControl* PlayerTitle;          // +0x384 "PlayerTitle"
    int Field388;                     // +0x388
    int Field38c;                     // +0x38c
    unsigned Field390;                // +0x390 -- an archetype id (Draw 0x00479871)
    CacheString Field394;             // +0x394
    // +0x398..+0x3a0: the dealer's three-pane info card -- NN_Dealer's slot 45 creates them with
    // FUN_005a10e0 (0x004811d5 / 0x00481218 / 0x0048125c, each hidden at once); NN_Dealer's
    // Update copies them into the partner inventory (0x0047ff1c..0x0047ff46). ShowInfoCard and
    // ClearInfoCard drive them through RichTextWin methods.
    RichTextWin* InfoCardFacts;       // +0x398 "InfoCard_Facts"
    RichTextWin* InfoCardStats;       // +0x39c "InfoCard_Stats"
    RichTextWin* InfoCardValues;      // +0x3a0 "InfoCard_Values"
    int SelectedIndex;                // +0x3a4 -- -1 = none
    EquipDescList SavedLoadout;       // +0x3a8 -- EquipDescList (VC6 list: +0x3ac head, +0x3b0
                                      // size); SetProperty 0x0047a697 assigns the player's list
    WinControl* PlayerCash;           // +0x3b4 "PlayerCash"
    unsigned char Field3b8;           // +0x3b8
    unsigned char Field3b9;           // +0x3b9
    unsigned char Pad3ba[2];
    unsigned Field3bc;                // +0x3bc -- a good id (written by FUN_0047eab0 0x0047ee67,
                                      // not by the ctor); RefreshItemList reads it after +0x3b9
    unsigned char Field3c0;           // +0x3c0
    unsigned char Field3c1;           // +0x3c1
    unsigned char Pad3c2[2];
    int FirstRow;                     // +0x3c4 -- list index shown in row 0
    int Field3c8;                     // +0x3c8 -- -1
    int HoverIndex;                   // +0x3cc -- -1 = none
    WinControl* InventoryScroll;      // +0x3d0 "Inventory_Scroll"
    WinControl* FilterWeapons;        // +0x3d4 "Filter_Weapons"
    WinControl* FilterCargoBay;       // +0x3d8 "Filter_CargoBay"
    WinControl* FilterEquipment;      // +0x3dc "Filter_Equipment"
    WinControl* FilterCommodity;      // +0x3e0 "Filter_Commodity"
    WinControl* FilterAmmo;           // +0x3e4 "Filter_Ammo"
    WinControl* ItemName[kNnInventoryRows];      // +0x3e8 "ItemName0%d"
    WinControl* ItemView[kNnInventoryRows];      // +0x3fc "ItemView0%d"
    WinControl* ItemPrice[kNnInventoryRows];     // +0x410 "ItemPrice0%d"
    WinControl* ItemQuantity[kNnInventoryRows];  // +0x424 "ItemQuantity0%d"
    WinControl* ItemButton[kNnInventoryRows];    // +0x438 "ItemButton%d"
    WinControl* ItemMesh[kNnInventoryRows];      // +0x44c "ItemMesh%d"
    WinControl* ItemGoodDeal[kNnInventoryRows];  // +0x460 "ItemGoodDeal0%d"
    WinControl* ItemDamage[kNnInventoryRows];    // +0x474 "ItemDamage%d"
    WinControl* EquipToggle[kNnInventoryRows];   // +0x488 "EquipToggle%d"
    WinControl* CargoMissionText[kNnInventoryRows]; // +0x49c "CargoMissionText%d" (slot 45 stores it)
    WinControl* JettisonButton;       // +0x4b0 "JettisonButton"
    WinControl* InfoCardBackground;   // +0x4b4 "InfoCard_Background"
    WinControl* InfoCardName;         // +0x4b8 "InfoCard_Name"
    RichTextWin* InfoCardInfo;        // +0x4bc "InfoCard_Info"
    WinControl* InfoCardClose;        // +0x4c0 "InfoCard_Close"
    WinControl* InfoCardView;         // +0x4c4 "InfoCard_View"
    WinControl* InfoCardScroll;       // +0x4c8 "InfoCard_Scroll"
    WinControl* Field4cc;             // +0x4cc "Empty_Cargobar" -- Draw's bar controls (+0x84 instance)
    WinControl* Field4d0;             // +0x4d0 "Full_Cargobar"
    WinControl* Field4d4;             // +0x4d4 "SelectedCargobar"
    WinControl* CloseButton;          // +0x4d8 "Inventory_Close" (slot 45)
    WinControl* CargoLabel;           // +0x4dc "CargoLabel" (slot 45 stores it)
    unsigned char Field4e0;           // +0x4e0
    unsigned char Field4e1;           // +0x4e1
    unsigned char Field4e2;           // +0x4e2
    unsigned char Field4e3;           // +0x4e3
    unsigned char Field4e4;           // +0x4e4
    unsigned char Result4e5;          // +0x4e5 -- passed to the result callback
    unsigned char Field4e6;           // +0x4e6
    unsigned char bNeedBind;          // +0x4e7 -- Create sets it; BindControls clears it
    unsigned Field4e8;                // +0x4e8
    unsigned Field4ec;                // +0x4ec
    int Field4f0;                     // +0x4f0
    EquipDesc* SelectedItem;          // +0x4f4 -- GetSelectedItem's cache (FUN_0047e790)
    int Field4f8;                     // +0x4f8 (not written by the ctor)
    int Field4fc;                     // +0x4fc (not written by the ctor)
    FNnInventoryResultFn ResultFn;    // +0x500
    void* ResultArg;                  // +0x504
    wchar_t ToolTipText[0xff];        // +0x508 -- SetProperty 0x4f swprintf target
    wchar_t ToolTipFormat[0xff];      // +0x706 -- IDS 0x49b
    int Field904;                     // +0x904
    int Field908;                     // +0x908 -- -1
    int Field90c;                     // +0x90c -- -1
    int Field910;                     // +0x910
    float Field914;                   // +0x914 -- counted down by Update
    int Field918;                     // +0x918
    WinControl* QuantityControl[8];   // +0x91c..+0x938 -- ShowQuantityControls
    int Field93c;                     // +0x93c -- 1
    int Field940;                     // +0x940 -- 1 (slot 45: 100)
    WinControl* Field944;             // +0x944 "Infocard_Title" -- paired with InfoCardInfo in
                                      // ShowQuantityControls
    unsigned short Field948;          // +0x948 -- SUBOBJ_ID_NONE
    unsigned char Pad94a[2];
    EquipDesc Field94c;               // +0x94c
    WinControl* TradeRouteText;       // +0x96c "TradeRouteText" (slot 45 stores it)
    WinControl* TradeRouteButton;     // +0x970 "TradeRouteButton" (slot 45 stores it)

private:
    NnInventory(NnInventory const&) = delete;
    NnInventory& operator=(NnInventory const&) = delete;
};

static_assert(offsetof(NnInventory, Mode) == 0x32c, "NN_Inventory members start at +0x32c");

// DAT_00671ce4 -- the filter mode the player's own inventory last used (SetMode / BindControls).
extern int G_NnInventoryLastMode;

// DAT_0067339c -- CreateID("insufficient_cargo_space") (static init LAB_004c6120).
extern unsigned G_NnInsufficientCargoSpaceId;

// DAT_00674a8c -- CreateID("ge_s_repair_01"), the nanobot archetype (static init LAB_004da100).
// DAT_00674a88 -- CreateID("ge_s_battery_01"), the shield battery (static init LAB_004da120).
// Both initializers sit in the HUD status file's range; the readers are the dealer / trade
// code and the HUD status bars. Declared here so every reader uses one definition.
extern unsigned G_NanobotArchId;
extern unsigned G_ShieldBatteryArchId;

// The NeuroNet voice line ids of the inventory and dealer windows (static initializers
// LAB_004778c0 .. LAB_00477980). Their readers hand them to the voice queue FUN_00489e80,
// which the port does not have yet.
extern unsigned G_NnVoiceInsufficientCargoSpaceId; // DAT_00671c78 "insufficient_cargo_space"
extern unsigned G_NnVoiceNotEnoughMoneyId;         // DAT_00671c74 "not_enough_money"
extern unsigned G_NnVoiceMountedId;                // DAT_00671c70 "mounted"
extern unsigned G_NnVoiceLoadedIntoCargoHoldId;    // DAT_00671c6c "loaded_into_cargo_hold"
extern unsigned G_NnVoiceNoPlaceToMountId;         // DAT_00671c68 "no_place_to_mount"
extern unsigned G_NnVoiceCargoJettisonedId;        // DAT_00671c64 "cargo_jettisoned"
extern unsigned G_NnVoiceShipPurchaseCompleteId;   // DAT_00671c60 "ship_purchase_complete"

// --- The player's loadout requests at 0x004c4xxx ------------------------------------------
// They sit in the block of player-state accessors that character_select_equip.cpp holds
// (FUN_004c43d0 / FUN_004c4f90 / FUN_004c4fa0 are there, next to the counters they touch).
// These three call the server or read one global only; they are defined in nn_inventory.cpp
// because the inventory is their first ported caller.

// FUN_004c4810 (cdecl) -- the player is docked: (DAT_00673358 & 0x3fffffff) != 0.
bool IsPlayerDockedRetail();
// FUN_004c4040 (cdecl) -- G_Server->ReqEquipment(*List, DAT_00673344).
void ReqPlayerEquipmentRetail(EquipDescList const* List);
// FUN_004c4540 (cdecl) -- G_Server->ReqModifyItem with the same arguments, then
// ModifyPlayerEquipRetail (FUN_004c43d0), whose result it returns.
bool ReqModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                                float Health, unsigned char bEquipped);
// FUN_004c3df0 (cdecl) -- G_Server->ReqShipArch(ArchId, DAT_00673344) (slot 25, +0x64).
void ReqPlayerShipArchRetail(unsigned ArchId);
// FUN_004c4f50 (cdecl) -- G_Server->ReqHullStatus(Health, DAT_00673344) (slot 26, +0x68).
void ReqPlayerHullStatusRetail(float Health);
// FUN_004c3f10 (cdecl) -- G_Server->ReqCollisionGroups(*Groups, DAT_00673344) (slot 27, +0x6c).
void ReqPlayerCollisionGroupsRetail(std::list<CollisionGroupDesc> const* Groups);
// FUN_004c4cc0 (cdecl) -- G_Server->ReqSetCash(Cash, DAT_00673344) (slot 33, +0x84).
void ReqPlayerCashRetail(int Cash);
// FUN_004c4d50 (cdecl) -- G_Server->ReqChangeCash(-Amount, DAT_00673344) (slot 34, +0x88).
// Callers: NN_Repair 0x004b413b, the dealer purchase 0x0047ecd8 (BuyFromDealer).
void ReqPlayerSpendCashRetail(int Amount);
// FUN_004c4d30 (cdecl) -- G_Server->ReqChangeCash(Amount, DAT_00673344) (slot 34, +0x88).
// Caller: the sale 0x0047efc4 (SellSelected).
void ReqPlayerGainCashRetail(int Amount);
// FUN_004c41d0 (cdecl) -- G_Server->ReqAddItem(ArchId, Hardpoint->Value, 1, Health, true,
// DAT_00673344) (slot 30, +0x78): one item mounted on Hardpoint.
void ReqAddPlayerItemOnHardpointRetail(unsigned ArchId, CacheString const* Hardpoint, float Health);
// FUN_004c4200 (cdecl) -- ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME, 1, Health, true,
// DAT_00673344): one item into the cargo bay, mounted.
void ReqAddPlayerMountedCargoRetail(unsigned ArchId, float Health);
// FUN_004c4230 (cdecl) -- ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME, Count, Health, false,
// DAT_00673344): Count into the cargo bay, not mounted.
void ReqAddPlayerCargoRetail(unsigned ArchId, int Count, float Health);
// FUN_004c4260 (cdecl) -- G_Server->ReqRemoveItem(Id, Count, DAT_00673344) (slot 31, +0x7c), then
// the local removal FUN_004c4320's body (inlined in retail; RemovePlayerEquipRetail here).
bool ReqRemovePlayerEquipRetail(unsigned short Id, int Count);

// FUN_004c6f70 (cdecl) -- what the base market pays for the items of List (only those whose
// owner +0x24 is Owner, when Owner is set); 0 when the player is not docked or the base has
// no market. Saved is the loadout that decides the used-goods discount.
int NnLoadoutResaleValue(EquipDescList const* List, FInventoryShipModel const* Owner,
                         EquipDescList* Saved);
// FUN_004c6e90 (cdecl) -- what the base market pays for ShipArchId's hull: ftol(price); with
// bApplyCondition, ftol(ftol(price * mean collision-group health * hull scale) * 0.8). 0 when
// there is no ship, the player is not docked or the ship has no good.
int NnShipResaleValue(unsigned ShipArchId, bool bApplyCondition);

// FUN_0047e460 (cdecl) -- the inventory filter an equipment archetype belongs to: 0 weapons,
// 1 ammo, 2 equipment, 3 cargo bay, 4 commodity, 5 none.
int NnItemFilterMode(unsigned ArchId);

// FUN_00482d80 (cdecl) -- whether List holds a gun / mine dropper / countermeasure dropper
// whose projectile archetype is AmmoArchId.
bool NnListHasLauncherFor(EquipDescList const* List, unsigned AmmoArchId);

// FUN_00477ab0 (cdecl) -- open the dealer: nothing while an NN_Dealer window exists; else the
// NN_Inventory window (made under NN_MainButton panel 0x66's button when missing or flagged 4)
// and an NN_Dealer beside it, then the inventory's slot 42 with 0x47 (TradeMode), 0x48
// (ResultArg, ResultFn) and 0x13 (ShipPackage). Callers: NavBar 0x0044107a (1, commodity) /
// 0x00441235 (3, equipment) and FUN_00477bf0 (2, ship).
void NnInventoryOpenDealer(int TradeMode, FNnInventoryResultFn ResultFn, void* ResultArg,
                           unsigned ShipPackage);

// FUN_00477bf0 (cdecl) -- open the ship dealer on the Index-th ship package the current base's
// market sells (market category 3; the first one when Index is out of range; nothing when the
// market sells none). Caller: NavBarOpenDealer FUN_004421a0 (0x004421f0).
void NnInventoryOpenShipDealer(int Index, FNnInventoryResultFn ResultFn, void* ResultArg);
