// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_repair.h"

#include "character_select_equip.h" // GetPlayerCreditsRetail, GetPlayerHealthScaleRetail, the
                                    // revisions, the collision-group and equipment lists,
                                    // GetPlayerShipArchetypeIdRetail
#include "client_base_market.h"     // ClientMarketGetPrice (FUN_00438060),
                                    // GetClientLocationBaseMarket (FUN_0043a510),
                                    // GetClientCurrentLocationBaseData (FUN_0043b630)
#include "client_cliobj.h"          // Archetype::CliObjMaterialPaths (companion +0x20)
#include "hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740),
                                    // HudCollisionGroupTypeIds (FUN_0045a7c0)
#include "menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "nn_inventory.h"           // ReqPlayerSpendCashRetail, ReqPlayerHullStatusRetail,
                                    // ReqPlayerCollisionGroupsRetail, ReqPlayerEquipmentRetail
#include "nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "nn_ui_pulse.h"            // G_NnPaletteArgb_679bac
#include "ship_view_control.h"      // ShipViewEquipmentHpClass (FUN_005835f0)
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DReparent, Win3DFindByName, Win3DDeleteTree
#include "win_factory.h"
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "common_archetype.h"
#include "common_goods.h"           // EquipDesc, EquipDescList, GoodInfo, BaseData, Arch2Good
#include "common_id.h"              // CreateID

#include <list>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00672210 -- WINFACTORY<NN_Repair> static (LAB_004b2ba0 -> FUN_004b4830; Create
// FUN_004b48a0; atexit LAB_004b2bc0 -> FUN_00579380).
WINFACTORY<NnRepair> G_NnRepairFactory("NN_Repair");

namespace
{
    char const kBackgroundMesh[] = "Interface\\NeuroNet\\Inventory\\repair_bakgrnd.cmp";
    char const kCloseMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db";
    char const kCheckMesh[] = "Interface\\NeuroNet\\Inventory\\check.3db";
    char const kBlackBoxMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";
    char const kCheckToggleMesh[] = "Interface\\NeuroNet\\Inventory\\checktoggle.3db";
    char const kSelectSound[] = "ui_select_item";
    // PTR_DAT_005d0ec4 -- L"%d".
    wchar_t const kCountFormat[] = L"%d";

    // LAB_004b2bd0 -- DAT_0067220c = CreateID("repair_complete"); read once, by the OK button
    // (0x004b37b5).
    unsigned const G_NnVoiceRepairCompleteId = CreateID("repair_complete");

    // LAB_004b2b70 / LAB_004b2b80 / LAB_004b2b90 -- three float statics of this translation unit
    // (DAT_00672244 = DAT_00672240 = [0x005d51a8] -0.6, DAT_0067223c = [0x005d51ac] 0.6). Each
    // has one XREF, its initializer's write; nothing reads them.
    float G_RepairStatic672244 = -0.6f;
    float G_RepairStatic672240 = -0.6f;
    float G_RepairStatic67223c = 0.6f;

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // The collision-group tests compare the health's bits against 0x3f800000 (CMP dword,
    // 0x004b3ab8 / 0x004b4564), not the value.
    bool IsOneBits(float Value)
    {
        uint32_t Bits = 0;
        memcpy(&Bits, &Value, sizeof(Bits));
        return Bits == 0x3f800000u;
    }

    // The first material path of an equipment archetype's companion, 0 when it has none
    // (0x004b3c77..0x004b3c96 / 0x004b3c9f..0x004b3cbb).
    char const* FirstMaterialPath(Archetype::Equipment const* Equipment)
    {
        Archetype::ICliObj* const Companion = Equipment->Companion;
        if (Companion == nullptr)
            return nullptr;
        Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> const* const Paths =
            Archetype::CliObjMaterialPaths(Companion);
        // SAR 3 / TEST / JBE: an empty vector has no path.
        if (Paths->Begin == nullptr || Paths->End - Paths->Begin == 0)
            return nullptr;
        return Paths->Begin->Path;
    }
}

NnRepair::NnRepair()
    : ScrollOffset(0)
    , OkButton(nullptr)
    , CancelButton(nullptr)
    , Hovered(-1)
    , EquipRevision(0xffffffffu)
    , CollisionRevision(0xffffffffu)
    , HealthScale(-1.0f)
{
    // FUN_004b48a0 0x004b48e3..0x004b4944: the vector's three pointers zeroed (its allocator
    // byte gets the caller's garbage), the vptrs, +0x344 / +0x348 / +0x340 = 0, +0x484 /
    // +0x488 / +0x48c = -1, +0x490 = -1.0f. The control arrays are not written.
}

NnRepair::~NnRepair()
{
    // FUN_004b2c10: the vector's buffer freed and its pointers zeroed (the member's own
    // destructor here), then FUN_00576010 (Event::Handler) and FUN_0059fa50 (WinControl).
}

void NnRepair::DeleteSelf()
{
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child); // FUN_0043d2a0
        Child = Next;
    }
    Destroy();   // slot 10
    delete this; // slot 0 with 1
}

int NnRepair::SelectedTotal() const
{
    int Total = 0;
    for (size_t Index = 0; Index < Items.size(); ++Index)
    {
        if (Items[Index].bSelected != 0)
            Total += Items[Index].Cost;
    }
    return Total;
}

void NnRepair::ShowTotalCost()
{
    wchar_t Buffer[0x100];
    swprintf(Buffer, 0x100, kCountFormat, SelectedTotal());
    TotalCost->SetProperty(0x1d, PointerArg(Buffer), 0);
}

void NnRepair::ShowPlayerCash()
{
    wchar_t Buffer[0x100];
    FormatGroupedNumber(Buffer, static_cast<int>(GetPlayerCreditsRetail()), 0x476);
    PlayerCash->SetProperty(0x1d, PointerArg(Buffer), 0);
}

bool NnRepair::Create(Win3D* NewParent, void* Data)
{
    // FUN_004b2c90 (RET 8). 0x004b2c9c..0x004b2cb8: the background mesh (slot 38) and the
    // half extents from it.
    SetMesh(kBackgroundMesh, 0);
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder); // FUN_005a1340

    // 0x004b2cbd..0x004b2d25
    {
        float const Position[3] = {0.0f, -0.204f, 0.0f};
        float const Half[3] = {0.133f, 0.026f, 0.0f};
        OkButton = CreateWireButtonControl("RepairOkButton", "ui_execute_transaction", Position,
                                           Half, 0, 0, 0x438, 0, 0);
        OkButton->SetProperty(1, 1, 0);
    }
    // 0x004b2d2b..0x004b2d85
    {
        float const Position[3] = {0.0f, -0.216f, 0.0f};
        RepairText = CreateTextControl("RepairText", "ButtonText", Position, nullptr, 0.265f, 0.06f,
                                       1.5f, 0x427, 1, 0, 0);
        RepairText->SetProperty(1, 1, 0);
    }
    // 0x004b2d8b..0x004b2dfa
    {
        float const Position[3] = {0.22f, 0.231f, 0.0f};
        float const Half[3] = {0.02f, 0.02f, 0.0f};
        CancelButton = CreateButtonControl("RepairCancelButton", kCloseMesh, nullptr, kSelectSound,
                                           nullptr, Position, nullptr, Half, 0, 0, 0, 0, 0x2148, 0,
                                           0);
    }

    // 0x004b2e10..0x004b327d: nine rows, 0.0282 apart. Step is FILD Index, FMUL 0.0282f, stored
    // as float; each row coordinate is a float constant minus Step, stored once as float (the
    // item-name row's FSUBR takes the qword 0x005d5370, which is 0.141f widened, so it is the
    // same value).
    char Name[0x100];
    for (int Index = 0; Index < 9; ++Index)
    {
        float const Step = static_cast<float>(Index) * 0.0282f;
        float const CheckY = 0.14f - Step;
        float const RowY = 0.141f - Step;
        float const DamageY = 0.13f - Step;
        float const Small[3] = {0.014f, 0.014f, 0.0f};

        sprintf(Name, "RepairCheckBox%d", Index);
        {
            float const Position[3] = {-0.215f, CheckY, 0.0f};
            CheckBoxes[Index] = CreateToggleButtonControl(Name, kCheckMesh, kBlackBoxMesh,
                                                          kSelectSound, nullptr, Position, nullptr,
                                                          Small, 0, 0, 1, 0, 0x43a, 0);
        }
        sprintf(Name, "RepairCheckBoxFrame%d", Index);
        {
            float const Position[3] = {-0.215f, CheckY, 0.0f};
            CheckBoxFrames[Index] = CreateWireButtonControl(Name, kSelectSound, Position, Small, 0, 0,
                                                            0, 0, 0);
        }
        sprintf(Name, "RepairIconFrames%d", Index);
        {
            float const Position[3] = {-0.176f, RowY, 0.0f};
            IconFrames[Index] = CreateWireButtonControl(Name, kSelectSound, Position, Small, 0, 0, 0,
                                                        0, 0);
        }
        sprintf(Name, "RepairSmallIcons%d", Index);
        {
            float const Position[3] = {-0.176f, RowY, -1.1f};
            float const Zero[3] = {0.0f, 0.0f, 0.0f};
            SmallIcons[Index] = CreateStaticControl(Name, kBlackBoxMesh, Position, nullptr, Small, 0,
                                                    0, 0, 1, 0, 1, 1, 0x6e, 0, 0, Zero);
            // 0x004b306b..0x004b3072: not pickable.
            SmallIcons[Index]->Flags = static_cast<unsigned char>(SmallIcons[Index]->Flags & 0xf7);
        }
        sprintf(Name, "RepairTextArea%d", Index);
        {
            float const Position[3] = {0.02f, RowY, 0.0f};
            float const Half[3] = {0.171f, 0.014f, 0.0f};
            TextAreas[Index] = CreateWireButtonControl(Name, kSelectSound, Position, Half, 0, 0, 0, 0,
                                                       0);
        }
        sprintf(Name, "RepairItemName%d", Index);
        {
            float const Position[3] = {-0.01f, RowY, 0.0f};
            ItemNames[Index] = CreateTextControl(Name, "ItemName", Position, nullptr, 0.27f, 0.03f,
                                                 1.2f, 0x3f6, 0, 0, 0);
        }
        sprintf(Name, "RepairItemPrice%d", Index);
        {
            float const Position[3] = {0.136f, RowY, 0.0f};
            ItemPrices[Index] = CreateTextControl(Name, "ItemName", Position, nullptr, 0.105f, 0.03f,
                                                  1.2f, 0x40e, 2, 0, 0);
        }
        sprintf(Name, "RepairItemDamage%d", Index);
        {
            float const Position[3] = {-0.176f, DamageY, 0.001f};
            float const Half[3] = {0.014f, 0.002f, 0.0f};
            ItemDamages[Index] = CreateCargoBarControl(Name, "", Position, nullptr, Half, 1, 0x43c,
                                                       G_NnPaletteArgb_679bac,
                                                       G_NnPaletteArgb_679bac);
            ItemDamages[Index]->SetProperty(0x19, 0x10000, 0);
        }
    }

    // 0x004b3283..0x004b32eb: the scroll bar (0..5, page 9).
    {
        float const Position[3] = {0.19f, 0.14f, -1.0f};
        Scroll = CreateUIScroll("RepairScroll", Position, 0.018f, 0.23f, 0, 5, 9, 0);
    }
    // 0x004b32be..0x004b3309
    {
        float const Position[3] = {0.01f, 0.233f, 0.0f};
        CreateTextControl("RepairList", "PageHeader", Position, nullptr, 0.483f, 0.03f, 1.7f, 0x4a8,
                          1, 0, 0);
    }
    // 0x004b330e..0x004b33a2
    {
        float const Position[3] = {-0.215f, -0.13f, 0.0f};
        float const Half[3] = {0.02f, 0.02f, 0.0f};
        ToggleChecks = CreateButtonControl("RepairToggleChecks", kCheckToggleMesh, nullptr,
                                           kSelectSound, nullptr, Position, nullptr, Half, 0, 0, 0,
                                           0, 0x439, 0, 0);
    }
    // 0x004b3378..0x004b33c7
    {
        float const Position[3] = {0.005f, 0.192f, 0.0f};
        PlayerCash = CreateTextControl("RepairPlayerCash", "Normal", Position, nullptr, 0.488f, 0.03f,
                                       1.4f, 0, 1, 0, 0);
    }
    // 0x004b33cd..0x004b3410
    {
        float const Position[3] = {-0.067f, -0.13f, 0.0f};
        CreateTextControl("RepairTotalCostText", "Normal", Position, nullptr, 0.15f, 0.03f, 1.2f,
                          0x4a9, 0, 0, 0);
    }
    // 0x004b3415..0x004b3489
    {
        float const Position[3] = {0.12f, -0.13f, 0.0f};
        TotalCost = CreateTextControl("RepairTotalCost", "Normal", Position, nullptr, 0.15f, 0.03f,
                                      1.2f, 0, 2, 0, 0);
    }
    // 0x004b345c..0x004b34bc
    {
        float const Position[3] = {0.005f, -0.195f, 0.0f};
        NotEnoughCash = CreateTextControl("RepairNotEnoughCash", "Normal", Position, nullptr, 0.488f,
                                          0.03f, 1.2f, 0x4aa, 1, 0, 0);
        NotEnoughCash->SetProperty(1, 1, 0);
    }

    // 0x004b34c2..0x004b351d
    ShowPlayerCash();
    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    return Win3D::Create(NewParent, Data);      // FUN_004b5060
}

intptr_t NnRepair::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004b3530 (RET 0xc): byte table 0x004b397c over Msg - 0x26 (0..0x28) into the seven
    // cases at 0x004b3960. Every path returns 0.
    switch (Msg)
    {
    case 0x2c:
    case 0x2d:
    {
        // 0x004b355d..0x004b3616: the row of the control (a frame, an icon frame, a text area
        // or a check box). No match returns at once.
        int Row = 0;
        for (; Row < 9; ++Row)
        {
            if (A == PointerArg(CheckBoxFrames[Row]) || A == PointerArg(IconFrames[Row]) ||
                A == PointerArg(TextAreas[Row]) || A == PointerArg(CheckBoxes[Row]))
                break;
        }
        if (Row == 9)
            return 0;
        if (Msg == 0x2c)
            Hovered = Row;
        else if (Hovered == Row)
            Hovered = -1;
        // The row's other controls get the same message.
        if (A != PointerArg(CheckBoxFrames[Row]))
            CheckBoxFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(CheckBoxes[Row]))
            CheckBoxes[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(TextAreas[Row]))
            TextAreas[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case 0x4e:
    {
        // 0x004b361b..0x004b3656: closed from outside -- no NavBar.
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        Win3DReparent(this, nullptr); // FUN_0059dd00
        DeleteSelf();
        return 0;
    }
    case 0x30:
    {
        // 0x004b3665..0x004b36c1: a check box changed; B == 0 checks its item. Every row is
        // tested (no break); an item index past the list is ignored (unsigned compare).
        for (int Row = 0; Row < 9; ++Row)
        {
            if (A != PointerArg(CheckBoxes[Row]))
                continue;
            unsigned const Index = static_cast<unsigned>(ScrollOffset + Row);
            if (Index < Items.size())
                Items[Index].bSelected = (B == 0) ? 1 : 0;
        }
        // 0x004b36c3..0x004b3948
        ShowTotalCost();
        return 0;
    }
    case 0x3e:
    {
        // 0x004b3702..0x004b3724: the scroll bar moved.
        if (A != PointerArg(Scroll))
            return 0;
        ScrollOffset = static_cast<int>(B);
        RefreshRows();
        return 0;
    }
    case 0x42:
    {
        // 0x004b3738..0x004b3770: moved to *A -- slot 31 with the three floats, slot 5 with the
        // position, then UI sound slot 2.
        float const* const Target = reinterpret_cast<float const*>(A);
        SetOffset(Target[0], Target[1], Target[2]);
        SetPosition(Position);
        PlayOrRewindUiSoundSlot(2); // FUN_005645c0
        return 0;
    }
    case 0x26:
    {
        // 0x004b377f..0x004b37a0: an icon frame ([+0x3dc - 0x48]) or a text area toggles its
        // row's check box -- its state read (0x2f), written back (0x30), and this window told
        // (0x30, 1 when the state read was 0). Toggle 0x30 flips on its argument (SETZ), so the
        // write-back is the toggle.
        for (int Row = 0; Row < 9; ++Row)
        {
            if (A != PointerArg(IconFrames[Row]) && A != PointerArg(TextAreas[Row]))
                continue;
            int State = 0;
            CheckBoxes[Row]->SetProperty(0x2f, PointerArg(&State), 0);
            CheckBoxes[Row]->SetProperty(0x30, static_cast<uintptr_t>(State), 0);
            SetProperty(0x30, PointerArg(CheckBoxes[Row]), (State != 0) ? 0u : 1u);
            return 0;
        }
        if (A == PointerArg(OkButton))
        {
            // 0x004b37ae..0x004b37c6
            DoRepair(); // FUN_004b40d0
            NnVoiceEnqueue(G_NnVoiceRepairCompleteId, 0); // FUN_00489e80
            return 0;
        }
        if (A == PointerArg(CancelButton))
        {
            // 0x004b384c..0x004b3897
            Flags = static_cast<unsigned char>(Flags & 0xfc);
            Win3DReparent(this, nullptr); // FUN_0059dd00
            NavBarCloseService(false);    // FUN_004424f0(0)
            DeleteSelf();
            return 0;
        }
        if (A == PointerArg(ToggleChecks))
        {
            // 0x004b38b2..0x004b3948: every item's check flipped, the rows and the total.
            for (size_t Index = 0; Index < Items.size(); ++Index)
                Items[Index].bSelected = (Items[Index].bSelected == 0) ? 1 : 0;
            RefreshRows();
            ShowTotalCost();
        }
        return 0;
    }
    default:
        return 0;
    }
}

void NnRepair::PopulateList()
{
    // FUN_004b39b0. 0x004b39c8..0x004b39e2: the list emptied (FUN_004b4b50 erase(begin, end)).
    Items.clear();

    // 0x004b39e7..0x004b3a3a: the base's repair scale (BaseData +0x0c) and the hull's cost,
    // ftol((1 - health) * Ship::HitPts * scale) -- 0 without a ship archetype. The FPU keeps the
    // product at extended precision; the port's double product truncates the same way for
    // every realistic hit-point and scale value.
    float const RepairScale = GetClientCurrentLocationBaseData()->ShipRepairCost;
    float const Health = GetPlayerHealthScaleRetail();
    Archetype::Ship const* Ship = nullptr;
    int HullCost = 0;
    unsigned const ShipArchId = GetPlayerShipArchetypeIdRetail();
    if (ShipArchId != 0)
    {
        Ship = Archetype::GetShip(ShipArchId);
        if (Ship != nullptr)
            HullCost = static_cast<int>((1.0 - static_cast<double>(Health)) *
                                        static_cast<double>(Ship->HitPts) *
                                        static_cast<double>(RepairScale));
    }

    // 0x004b3a3c..0x004b3a9c: TEST AH,0x44 + JNP -- equal to 1.0 jumps. A damaged hull is
    // listed with at least 1 (a negative cost is dropped); an undamaged one only for a positive
    // cost.
    bool bAddHull = false;
    if (Health != 1.0f)
    {
        if (HullCost == 0)
        {
            HullCost = 1;
            bAddHull = true;
        }
        else
        {
            bAddHull = HullCost > 0;
        }
    }
    else
    {
        bAddHull = HullCost > 0;
    }
    if (bAddHull)
    {
        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = 0;
        Item.Id = 0;
        Item.NameIds = 0x428;
        Item.Cost = HullCost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Health) * 100.0);
        Item.IconMesh = nullptr;
        Item.IconMaterial = nullptr;
        Item.bSelected = 1;
        Items.push_back(Item); // FUN_004b49a0(end, &Item)
    }

    // 0x004b3aa1..0x004b3b63: every collision group whose health is not exactly 1.0. With the
    // ship archetype: its name from the group's type and ftol((1 - health) * HitPts * scale),
    // at least 1 (the group is not null-checked, as in retail). Without it: ids 0x429, cost 1.
    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end(); ++It)
    {
        CollisionGroupDesc const& Desc = *It;
        if (IsOneBits(Desc.Health))
            continue;
        int NameIds = 0x429;
        int Cost = 0;
        if (Ship != nullptr)
        {
            Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(Desc.Id);
            NameIds = static_cast<int>(HudCollisionGroupTypeIds(Group->Type)); // FUN_0045a7c0
            Cost = static_cast<int>((1.0 - static_cast<double>(Desc.Health)) *
                                    static_cast<double>(Group->HitPts) *
                                    static_cast<double>(RepairScale));
        }
        if (Cost == 0)
            Cost = 1;

        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = 1;
        Item.Id = Desc.Id;
        Item.NameIds = NameIds;
        Item.Cost = Cost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Desc.Health) * 100.0);
        Item.IconMesh = nullptr;
        Item.IconMaterial = nullptr;
        Item.bSelected = 1;
        Items.push_back(Item); // FUN_004b49a0(end, &Item)
    }

    // 0x004b3b69..0x004b3d37: a copy of the player's equipment, priced at the market of the
    // base the player is in.
    EquipDescList const Equipment(GetPlayerEquipDescListRetail());
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != Equipment.Items.end();
         ++It)
    {
        EquipDesc const& Desc = *It;

        // 0x004b3ba6..0x004b3bc7: items of hp class 1 are never repaired.
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        if (ShipViewEquipmentHpClass(Desc.ArchId, &Weapon, &Shield) == 1) // FUN_005835f0
            continue;

        // 0x004b3bcd..0x004b3c20: 30% of the market price, scaled by the damage and the count.
        // Price1 goes back to the FPU as an unsigned qword (FILD with a zero high dword), FMUL
        // 0.3f (DAT_005d51b4 = 0x3e99999a) on the stack, ftol. Price2 is reloaded the same way,
        // times (1 - health), FIMUL the count, stored as float.
        unsigned const GoodId = Arch2Good(Desc.ArchId);
        int const Price1 = static_cast<int>(ClientMarketGetPrice(Market, GoodId)); // FUN_00438060
        int const Price2 = static_cast<int>(
            static_cast<double>(static_cast<uint32_t>(Price1)) * static_cast<double>(0.3f));
        float const CostF = static_cast<float>(
            static_cast<double>(static_cast<uint32_t>(Price2)) *
            (1.0 - static_cast<double>(Desc.Health)) * static_cast<double>(Desc.Count));

        // 0x004b3c24..0x004b3c54: an unknown archetype, one without a name or an undamaged
        // item is skipped (TEST AH,0x44 + JNP: equal to 1.0 skips).
        Archetype::Equipment const* const Arch = Archetype::GetEquipment(Desc.ArchId);
        if (Arch == nullptr)
            continue;
        int const IdsName = Arch->IdsName;
        if (IdsName == 0)
            continue;
        if (Desc.Health == 1.0f)
            continue;

        // 0x004b3c5a..0x004b3cbb: the icon -- the good's item icon; else its shop model with the
        // companion's first material; without a good the archetype's model with that material.
        char const* Mesh = nullptr;
        char const* Material = nullptr;
        GoodInfo const* const Good = GoodList::find_by_id(GoodId);
        if (Good != nullptr)
        {
            Mesh = Good->ItemIcon.Value; // +0x78
            if (Mesh == nullptr)
            {
                Mesh = Good->ShopArchetype.Value; // +0x74
                Material = FirstMaterialPath(Arch);
            }
        }
        else
        {
            Mesh = Arch->DaArchetype.Value; // +0x0c
            Material = FirstMaterialPath(Arch);
        }

        // 0x004b3cbf..0x004b3d2a
        int Cost = static_cast<int>(CostF);
        if (Cost == 0)
            Cost = 1;
        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = 2;
        Item.Id = Desc.Id;
        Item.NameIds = IdsName;
        Item.Cost = Cost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Desc.Health) * 100.0);
        Item.IconMesh = Mesh;
        Item.IconMaterial = Material;
        Item.bSelected = 1;
        Items.insert(Items.end(), 1, Item); // FUN_004b4ba0(end, 1, &Item)
    }

    // 0x004b3d3d..0x004b3d62: the scroll range (-1 for an empty list, as in retail).
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(static_cast<int>(Items.size()) - 1));
}

void NnRepair::RefreshRows()
{
    // FUN_004b3dd0.
    wchar_t Buffer[0x100];
    int Row = 0;
    for (int Index = ScrollOffset; static_cast<unsigned>(Index) < Items.size(); ++Index)
    {
        FRepairItem const& Item = Items[static_cast<unsigned>(Index)];

        // 0x004b3e1c..0x004b3ea7: the row shown.
        ItemNames[Row]->SetProperty(2, 0, 0);
        ItemPrices[Row]->SetProperty(2, 0, 0);
        CheckBoxes[Row]->SetProperty(2, 0, 0);
        CheckBoxFrames[Row]->SetProperty(2, 0, 0);
        IconFrames[Row]->SetProperty(2, 0, 0);
        SmallIcons[Row]->SetProperty(1, 0, 0);
        TextAreas[Row]->SetProperty(2, 0, 0);
        ItemDamages[Row]->SetProperty(1, 0, 0);

        // 0x004b3ead..0x004b3efc: the name, the price, the check.
        ItemNames[Row]->SetProperty(0x1e, static_cast<uintptr_t>(Item.NameIds), 0);
        swprintf(Buffer, 0x100, kCountFormat, Item.Cost);
        ItemPrices[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);
        CheckBoxes[Row]->SetProperty(0x30, Item.bSelected, 0);

        // 0x004b3f02..0x004b3f2b: the icon, the black box without one.
        if (Item.IconMesh == nullptr)
            SmallIcons[Row]->SetProperty(4, PointerArg(kBlackBoxMesh), 0);
        else
            SmallIcons[Row]->SetProperty(4, PointerArg(Item.IconMesh), PointerArg(Item.IconMaterial));

        // 0x004b3f2f..0x004b3fa8: the health bar and its colour (>= 70, >= 30, below).
        int const Percent = Item.HealthPercent;
        ItemDamages[Row]->SetProperty(0x40, 100, static_cast<uintptr_t>(Percent));
        unsigned Colour = 0;
        if (Percent >= 0x46)
            Colour = 0xff30c24bu; // bytes 4b c2 30 ff
        else if (Percent >= 0x1e)
            Colour = 0xff26eaf5u; // bytes f5 ea 26 ff
        else
            Colour = 0xff2626ffu; // bytes ff 26 26 ff
        ItemDamages[Row]->SetProperty(0x3f, Colour, 0);

        // 0x004b3fae..0x004b3fbd: nine rows filled ends the refresh.
        ++Row;
        if (Row >= 9)
            return;
    }

    // 0x004b3fd8..0x004b40b9: the rows past the list hidden and cleared.
    for (; Row < 9; ++Row)
    {
        ItemNames[Row]->SetProperty(0x1e, 0, 0);
        ItemNames[Row]->SetProperty(2, 1, 0);
        ItemPrices[Row]->SetProperty(0x1e, 0, 0);
        ItemPrices[Row]->SetProperty(2, 1, 0);
        CheckBoxes[Row]->SetProperty(2, 1, 0);
        CheckBoxFrames[Row]->SetProperty(2, 1, 0);
        IconFrames[Row]->SetProperty(2, 1, 0);
        TextAreas[Row]->SetProperty(2, 1, 0);
        ItemDamages[Row]->SetProperty(1, 1, 0);
        CheckBoxes[Row]->SetProperty(0x30, 0, 0);
        SmallIcons[Row]->SetProperty(1, 1, 0);
    }
}

void NnRepair::DoRepair()
{
    // FUN_004b40d0. 0x004b40f1..0x004b4134: nothing happens when the checked items cost more
    // than the player has (signed compare).
    int const Total = SelectedTotal();
    if (static_cast<int>(GetPlayerCreditsRetail()) < Total)
        return;

    // 0x004b413a..0x004b41a7: pay, then copies of the collision groups and the equipment.
    ReqPlayerSpendCashRetail(Total); // FUN_004c4d50
    std::list<CollisionGroupDesc> Groups(GetPlayerCollisionGroupListRetail());
    EquipDescList Equipment(GetPlayerEquipDescListRetail());

    // 0x004b41ad..0x004b428f: every checked item repaired -- the hull through the server at
    // once; every group / item of the item's id set to 1.0 in the copies (no break).
    bool bGroupsChanged = false;
    bool bEquipmentChanged = false;
    for (size_t Index = 0; Index < Items.size(); ++Index)
    {
        FRepairItem const& Item = Items[Index];
        if (Item.bSelected == 0)
            continue;
        switch (Item.Kind)
        {
        case 0:
            ReqPlayerHullStatusRetail(1.0f); // FUN_004c4f50
            break;
        case 1:
            for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It)
            {
                if (It->Id == Item.Id)
                {
                    It->Health = 1.0f;
                    bGroupsChanged = true;
                }
            }
            break;
        case 2:
            for (std::list<EquipDesc>::iterator It = Equipment.Items.begin();
                 It != Equipment.Items.end(); ++It)
            {
                if (It->Id == Item.Id)
                {
                    It->set_status(1.0f);
                    bEquipmentChanged = true;
                }
            }
            break;
        default:
            break;
        }
    }

    // 0x004b4291..0x004b42e0: the changed lists sent, the credits shown again.
    if (bGroupsChanged)
        ReqPlayerCollisionGroupsRetail(&Groups); // FUN_004c3f10
    if (bEquipmentChanged)
        ReqPlayerEquipmentRetail(&Equipment);    // FUN_004c4040
    ShowPlayerCash();
}

void NnRepair::Draw()
{
    // FUN_004b43b0. 0x004b43b3..0x004b43ba: nothing is drawn while the HUD blocks the mouse.
    if (HudIsMousePickBlocked()) // FUN_0045a740
        return;

    // 0x004b43c1..0x004b4473: with nothing checked the OK button, its caption and the
    // "not enough cash" line are hidden; otherwise that line shows exactly when the total is
    // over the credits (signed SETG), and the button and caption when it is not.
    int const Total = SelectedTotal();
    if (Total == 0)
    {
        NotEnoughCash->SetProperty(1, 1, 0);
        OkButton->SetProperty(1, 1, 0);
        RepairText->SetProperty(1, 1, 0);
    }
    else
    {
        bool const bOver = Total > static_cast<int>(GetPlayerCreditsRetail());
        NotEnoughCash->SetProperty(1, bOver ? 0u : 1u, 0);
        OkButton->SetProperty(1, bOver ? 1u : 0u, 0);
        RepairText->SetProperty(1, bOver ? 1u : 0u, 0);
    }

    // 0x004b4479..0x004b44c6: every child at 0x64, the hovered row's frames at 0x65.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        Child->DrawLayer = 0x64;
        if (Hovered < 0)
            continue;
        WinControl* const Control = Child->AsWinControl(); // slot 2
        if (Control == CheckBoxFrames[Hovered] || Control == IconFrames[Hovered] ||
            Control == TextAreas[Hovered])
            Child->DrawLayer = 0x65;
    }

    // 0x004b44c8..0x004b44fa
    WireControlsJoinColumn(IconFrames, 9);     // FUN_005a4b50
    WireControlsJoinColumn(TextAreas, 9);
    WireControlsJoinColumn(CheckBoxFrames, 9);
    WinControl::Draw(); // FUN_005a0760 (tail jump)
}

bool NnRepair::PlayerNeedsRepair()
{
    // FUN_004b4510. 0x004b452b..0x004b4550: a damaged hull (TEST AH,0x44 + JNP: equal to 1.0
    // goes on).
    if (GetPlayerHealthScaleRetail() != 1.0f)
        return true;

    // 0x004b4551..0x004b4571: a collision group whose health is not exactly 1.0.
    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end(); ++It)
    {
        if (!IsOneBits(It->Health))
            return true;
    }

    // 0x004b4573..0x004b45d0: in a copy of the equipment, a damaged item of any hp class but 1.
    EquipDescList const Equipment(GetPlayerEquipDescListRetail());
    for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != Equipment.Items.end();
         ++It)
    {
        if (It->Health == 1.0f)
            continue;
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        if (ShipViewEquipmentHpClass(It->ArchId, &Weapon, &Shield) != 1) // FUN_005835f0
            return true;
    }
    return false;
}

void NnRepair::Update()
{
    // LAB_004b4670. 0x004b4670..0x004b4681 / 0x004b4786..0x004b47b0: out of the base, the
    // window takes itself apart (no NavBar; retail tests `this` first).
    if (GetClientLocationCurrentBase() == 0)
    {
        DeleteSelf();
        return;
    }

    // 0x004b4687..0x004b46b6: Win3D's Update; nothing more while the equipment and collision
    // revisions and the hull health are the ones the list was built from (TEST AH,0x44 + JNP:
    // equal returns).
    Win3D::Update(); // FUN_005a17b0
    if (EquipRevision == GetPlayerEquipRevisionRetail() &&
        CollisionRevision == GetPlayerCollisionRevisionRetail() &&
        GetPlayerHealthScaleRetail() == HealthScale)
        return;

    // 0x004b46bc..0x004b4776: remember them, rebuild, refill, the total and the credits.
    EquipRevision = GetPlayerEquipRevisionRetail();
    CollisionRevision = GetPlayerCollisionRevisionRetail();
    HealthScale = GetPlayerHealthScaleRetail();
    PopulateList();
    RefreshRows();
    ShowTotalCost();
    ShowPlayerCash();
}

bool NnRepair::OnEvent(unsigned MsgId)
{
    // LAB_004b47c0 (RET 4), on the +0x32c face: USER_CANCEL (0x10) closes the window like the
    // cancel button. (Retail tests the adjusted `this` for null.)
    if (MsgId != 0x10)
        return false;
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    Win3DReparent(this, nullptr); // FUN_0059dd00
    NavBarCloseService(false);    // FUN_004424f0(0)
    DeleteSelf();
    return true;
}
