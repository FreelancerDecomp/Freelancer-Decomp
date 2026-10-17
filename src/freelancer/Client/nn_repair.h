// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The repair window of a base (retail NN_Repair, freelancer.exe 0x004b2b70..0x004b4ec0,
// "E:\FL\Scratch\Source\Client\nn\NN_Repair.cpp").
//
// RTTI .?AVNN_Repair@@ : WinControl, Event::Handler. operator_new(0x494) (0x004b48ba).
// WINFACTORY<NN_Repair> DAT_00672210 "NN_Repair": static init LAB_004b2ba0 (ctor FUN_004b4830,
// atexit LAB_004b2bc0 -> FUN_00579380); Create FUN_004b48a0 (operator_new, WinControl's ctor
// FUN_0059f860, the members and vptrs inlined, the class-name pointer, slot 9; a failed Create
// is followed by the scalar deleting destructor). FUN_004b4830 is the WINFACTORY ctor inlined
// for this type (name clamped to 31, FUN_0059dab0, vftable 0x005d5434).
//
// vftable 0x005d51e4 -- WinControl's with these overrides (raw dword dump):
//   0 FUN_004b2bf0 scalar deleting dtor (FUN_004b2c10), 9 FUN_004b2c90 Create, 11 LAB_004b4670
//   Update, 13 FUN_004b43b0 Draw, 42 LAB_004b3530 SetProperty.
// IDataViewOwner face +0x7c: vftable 0x005d51d4 (WinControl's).
// Event::Handler face +0x32c: vftable 0x005d51cc, slot 0 LAB_004b47c0.
//
// Nine rows over a list of what the player can have repaired: the hull, the damaged collision
// groups and the damaged equipment, each with a check box, an icon, its name, its price and a
// health bar. The checked rows' total is charged by the OK button.
//
// Layout: retail's, except that the std::vector at +0x330 is the current CRT's (retail's VC6
// vector is 0x10 bytes: allocator byte, first, last, end); every later member sits
// RepairVectorDelta bytes away. The comments give retail offsets.

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

class UiScroll;

// One row of the list, 0x20 bytes (the vector's stride; PopulateList 0x004b3a5c..0x004b3a9c,
// 0x004b3b0f..0x004b3b54, 0x004b3cd1..0x004b3d2a builds it on the stack).
struct FRepairItem
{
    int Kind;                   // +0x00 0 hull, 1 collision group, 2 equipment
    unsigned short Id;          // +0x04 CollisionGroupDesc::Id / EquipDesc::Id (0 for the hull)
    unsigned short Pad06;       //       not written
    int NameIds;                // +0x08
    int Cost;                   // +0x0c credits
    int HealthPercent;          // +0x10 ftol(health * 100)
    char const* IconMesh;       // +0x14 equipment only
    char const* IconMaterial;   // +0x18 equipment only
    unsigned char bSelected;    // +0x1c
    unsigned char Pad1d[3];     //       not written
};

static_assert(sizeof(FRepairItem) == 0x20, "NN_Repair list element stride 0x20");
static_assert(offsetof(FRepairItem, Cost) == 0x0c, "FRepairItem +0x0c");
static_assert(offsetof(FRepairItem, bSelected) == 0x1c, "FRepairItem +0x1c");

class NnRepair : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_004b48a0 (0x004b48dc..0x004b4944).
    NnRepair();
    // FUN_004b2c10 (FUN_004b2bf0 the scalar deleting wrapper).
    ~NnRepair() override;

    // slot 9 FUN_004b2c90 (RET 8) -- every control, then Win3D's Create. The list is filled by
    // the first Update (the revisions start at -1).
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_004b4670 -- closes without a base; otherwise Win3D's Update and, when the
    // player's equipment, collision groups or hull moved, the list rebuilt.
    void Update() override;
    // slot 13 FUN_004b43b0 -- the OK button against the credits, the row layers and the three
    // frame columns, then WinControl's Draw.
    void Draw() override;
    // slot 42 LAB_004b3530 (RET 0xc) -- clicks (0x26), hover (0x2c / 0x2d), a check box (0x30),
    // scroll (0x3e), a move (0x42), close (0x4e); returns 0.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face LAB_004b47c0 (RET 4) -- USER_CANCEL (0x10) closes the window.
    bool OnEvent(unsigned MsgId) override;

    // FUN_004b4510 (cdecl) -- whether anything of the player's ship is damaged: the hull, a
    // collision group, or an item (other than hp class 1) whose health is not 1. Callers: the
    // unported 0x0043e050 (0x0043e0c8), 0x0043e1b0 and 0x0043e570 (0x0043e59d).
    static bool PlayerNeedsRepair();

    std::vector<FRepairItem> Items;      // +0x330 (retail: allocator byte, +0x334 first,
                                         //         +0x338 last, +0x33c end)
    int ScrollOffset;                    // +0x340 first item in row 0
    WinControl* OkButton;                // +0x344 "RepairOkButton"
    WinControl* CancelButton;            // +0x348 "RepairCancelButton"
    WinControl* CheckBoxes[9];           // +0x34c "RepairCheckBox%d"
    WinControl* CheckBoxFrames[9];       // +0x370 "RepairCheckBoxFrame%d"
    WinControl* IconFrames[9];           // +0x394 "RepairIconFrames%d"
    WinControl* SmallIcons[9];           // +0x3b8 "RepairSmallIcons%d"
    WinControl* TextAreas[9];            // +0x3dc "RepairTextArea%d"
    WinControl* ItemNames[9];            // +0x400 "RepairItemName%d"
    WinControl* ItemPrices[9];           // +0x424 "RepairItemPrice%d"
    WinControl* ItemDamages[9];          // +0x448 "RepairItemDamage%d"
    UiScroll* Scroll;                    // +0x46c "RepairScroll"
    WinControl* ToggleChecks;            // +0x470 "RepairToggleChecks"
    WinControl* TotalCost;               // +0x474 "RepairTotalCost"
    WinControl* RepairText;              // +0x478 "RepairText"
    WinControl* PlayerCash;              // +0x47c "RepairPlayerCash"
    WinControl* NotEnoughCash;           // +0x480 "RepairNotEnoughCash"
    int Hovered;                         // +0x484 row under the mouse, -1 none
    unsigned EquipRevision;              // +0x488 FUN_004c4f90 when the list was built
    unsigned CollisionRevision;          // +0x48c FUN_004c4fa0 when the list was built
    float HealthScale;                   // +0x490 FUN_004c4f70 when the list was built

private:
    // The tail every close shares (e.g. 0x004b362b..0x004b3656): FUN_0043d2a0 on every child
    // (the next sibling read first), slot 10, then slot 0 with 1.
    void DeleteSelf();
    // FUN_004b39b0 -- the list rebuilt from the player's hull, collision groups and equipment,
    // and the scroll range.
    void PopulateList();
    // FUN_004b3dd0 -- the nine rows filled from ScrollOffset.
    void RefreshRows();
    // FUN_004b40d0 -- pay for the checked items and ask the server to repair them.
    void DoRepair();
    // The checked items' cost, inlined at 0x004b36c3, 0x004b38eb, 0x004b40f1, 0x004b43c1 and
    // 0x004b46ec (the loop walks `index < size` and adds +0x0c where +0x1c is set).
    int SelectedTotal() const;
    // swprintf(L"%d", SelectedTotal()) into TotalCost with 0x1d (0x004b3921..0x004b3948,
    // 0x004b4722..0x004b4747).
    void ShowTotalCost();
    // FormatGroupedNumber(credits, 0x476) into PlayerCash with 0x1d (0x004b34c2..0x004b34f1,
    // 0x004b42b7..0x004b42e0, 0x004b474d..0x004b4776).
    void ShowPlayerCash();

    NnRepair(NnRepair const&) = delete;
    NnRepair& operator=(NnRepair const&) = delete;
};

// Retail's VC6 std::vector is 0x10 bytes; everything after +0x330 moves by the difference
// (negative when the current CRT's vector is smaller).
constexpr ptrdiff_t RepairVectorDelta = static_cast<ptrdiff_t>(sizeof(std::vector<FRepairItem>)) - 0x10;

static_assert(offsetof(NnRepair, Items) == 0x330, "NN_Repair +0x330");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, ScrollOffset)) == 0x340 + RepairVectorDelta, "NN_Repair +0x340");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, CheckBoxes)) == 0x34c + RepairVectorDelta, "NN_Repair +0x34c");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, ItemDamages)) == 0x448 + RepairVectorDelta, "NN_Repair +0x448");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, Scroll)) == 0x46c + RepairVectorDelta, "NN_Repair +0x46c");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, Hovered)) == 0x484 + RepairVectorDelta, "NN_Repair +0x484");
static_assert(static_cast<ptrdiff_t>(offsetof(NnRepair, HealthScale)) == 0x490 + RepairVectorDelta, "NN_Repair +0x490");
static_assert(static_cast<ptrdiff_t>(sizeof(NnRepair)) == 0x494 + RepairVectorDelta, "retail NN_Repair is 0x494 bytes (operator_new 0x004b48ba)");
