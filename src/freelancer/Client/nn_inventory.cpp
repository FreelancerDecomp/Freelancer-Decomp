// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Inventory -- see nn_inventory.h for the class, its vftable and layout.

#include "nn_inventory.h"

#include "cargo_bar_control.h"      // ToolTipHoverInit / SetCaption
#include "client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510), ClientMarketGetPrice
#include "commodity_by_base.h"      // AppendCommodityTradeReport (FUN_004704d0)
#include "character_select_equip.h" // GetPlayerShipArchetypeIdRetail (FUN_004c3e10),
                                    // GetPlayerEquipDescListRetail (FUN_004c4060),
                                    // GetPlayerBaseIdRetail (DAT_00673358)
#include "dealer_equip_stats.h"     // BuildDealerEquipmentStatText (FUN_00484090)
#include "game_options.h"           // GetGameOption (FUN_005792b0)
#include "gf_object_ship.h"         // GfObjectShip (FUN_00446be0), SetEquipment / RebuildModel
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "hud_weapon_groups.h"      // G_WeaponGroups (DAT_00674c0c)
#include "ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), AppendTextMaybeXmlRdl
                                    // (FUN_0057e2c0), RdlAppendNode
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nav_bar.h"                // NavBarRefreshRepairTab (FUN_004427e0), NavBarShowDealerTab (FUN_00442850)
#include "nn_dealer.h"
#include "nn_main_button.h"         // NN_MainButton::FreeScaledBackground (FUN_0045dc60)
#include "nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "nn_modal.h"              // NnMainButtonSetPanelOrigin (FUN_0045a780),
                                    // NnMainButtonOnModalOpen (FUN_0045a460)
#include "nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4), DAT_00679bac
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "tooltip_hover.h"          // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "rich_text_win.h"
#include "ship_view_control.h"
#include "star_sphere.h"            // Win3DBindModelview (FUN_00422690)
#include "ui_scroll.h"
#include "win3d.h"
#include "win3d_immediate.h"        // Win3DBegin / Win3DVertex3f / Win3DEnd / ... (OpenGL.cpp)
#include "win_factory.h"
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "DALib.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include "common_archetype.h"
#include "common_goods.h"
#include "common_id.h"              // CreateID
#include "common_iface.h"           // IObjRW / IObjAffect
#include "common_server.h"          // IServer
#include "common_text.h"            // RenderDisplayList, StyleNode
#include "common_ini.h"             // StringAlloc
#include "common_misc.h"            // SUBOBJ_ID_NONE

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <vector>

// Retail WINFACTORY<NN_Inventory> "NN_Inventory" (static init FUN_00485670, vftable 0x005d1d20,
// Create FUN_004856e0).
WINFACTORY<NnInventory> G_NnInventoryFactory("NN_Inventory");

// DAT_00671ce4 -- the filter mode the player's own inventory last showed. SetMode (FUN_0047f240)
// writes it when the window is neither a dealer nor docked next to one; BindControls
// (0x0047b9f6) reopens the list in it.
int G_NnInventoryLastMode = 0;

// ---------------------------------------------------------------------------------------------
// FInventoryShipModel (+0x330)
// ---------------------------------------------------------------------------------------------

FInventoryShipModel::FInventoryShipModel()
    : ShipView(nullptr)
    , NameText(nullptr)
    , DamageBar(nullptr)
    , Frame(nullptr)
    , Toggle(nullptr)
    , ShipArchId(0)
    , Loadout()
    , bFlag28(0)
{
    // FUN_004c6140: +0x1c allocator byte (an uninitialised stack byte), +0x20 head node,
    // +0x24 = 0, then +0x00/+0x04/+0x08/+0x0c/+0x18 = 0. +0x10 and +0x14 are not written;
    // the port zeroes them with the rest.
    InfoText = nullptr;

    // 004c618e..004c61c5: FUN_004c6250 inlined -- clear the (just created, so empty) list,
    // +0x28 = 0, and the view calls behind `if (+0x00)`, which was zeroed above.
    Clear();
}

FInventoryShipModel::~FInventoryShipModel()
{
    // FUN_004c6200: erase every node, free the head, +0x20 = +0x24 = 0. The EquipDescList
    // member's own destructor does the same.
}

void FInventoryShipModel::Clear()
{
    // FUN_004c6250: unlink and free every node (size counted down), +0x28 = 0, then
    // FUN_00585d90(0, 0) and FUN_00585bd0(-1, 0, 0) on the view if there is one.
    Loadout.Items.clear();
    bFlag28 = 0;
    if (ShipView != nullptr)
    {
        ShipView->SelectGood(0, false);
        ShipView->SelectInstance(-1, false, 0);
    }
}

void FInventoryShipModel::LoadShip(EquipDescList const* Source, unsigned char bFlag)
{
    // FUN_004c62b0 (RET 8).
    if (ShipView != nullptr)
        ShipView->Reset(); // FUN_00583b90
    if (ShipArchId == 0)
        return;
    Archetype::Ship* const ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return;

    // 004c62e6..004c6316: std::list::erase(begin, end) (the Common import), +0x28 = 0 and the
    // view's two select calls -- FUN_004c6250's body.
    Clear();

    // 004c631b..004c633b
    bFlag28 = bFlag;
    if (ShipView != nullptr)
    {
        ShipView->LoadShip(&ShipArch->Nickname, true, bFlag != 0); // FUN_00585a90(ship+8, 1, bFlag)
        ShipView->LoadEquipment(Source);                          // FUN_005850f0
    }

    // 004c6340..004c6344
    SortFrom(Source);

    // 004c6349..004c6373: every item that is a good gets this model as its owner.
    for (std::list<EquipDesc>::iterator It = Loadout.Items.begin(); It != Loadout.Items.end(); ++It)
    {
        EquipDesc& Item = *It;
        if (GoodList::find_by_archetype(Item.ArchId) != nullptr)
            Item.set_owner(static_cast<int>(reinterpret_cast<intptr_t>(this)));
    }

    // 004c6379..004c638b: the name label's slot 42 (0x1e, ids_name, 0).
    if (NameText != nullptr)
        NameText->SetProperty(0x1e, static_cast<uintptr_t>(ShipArch->IdsName), 0);

    // 004c6391..004c63c6: the damage bar's slot 42 (0x40, 100, ftol(health * 100.0f)), then
    // (3, 0x408, 0). The product is formed on the x87 stack; the double product of two floats
    // is exact, as the extended one is.
    if (DamageBar != nullptr)
    {
        int const Health = static_cast<int>(static_cast<double>(GetPlayerHealthScaleRetail()) *
                                            100.0); // FUN_004c4f70, DAT_005ca230
        DamageBar->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
        DamageBar->SetProperty(3, 0x408, 0);
    }
}

void FInventoryShipModel::GetCargoCapacityAndUsed(float* OutCapacity, float* OutUsed) const
{
    // FUN_004c63e0 (RET 8). The ship lookup is not null-tested.
    *OutCapacity = 0.0f;
    *OutUsed = 0.0f;
    if (ShipArchId == 0)
        return;
    *OutCapacity = Archetype::GetShip(ShipArchId)->HoldSize;
    *OutUsed = Loadout.get_cargo_space_occupied();
}

bool FInventoryShipModel::RemoveItem(EquipDesc const* Desc, int Count)
{
    // FUN_004c6430 (RET 8). Count 0 takes Desc's own count (read before the search).
    unsigned Take = static_cast<unsigned>(Count);
    if (Take == 0)
        Take = static_cast<unsigned>(Desc->Count);

    // 004c6444..004c645b: find the node whose value is Desc.
    std::list<EquipDesc>::iterator Found = Loadout.Items.begin();
    for (; Found != Loadout.Items.end(); ++Found)
    {
        if (&*Found == Desc)
            break;
    }
    if (Found == Loadout.Items.end())
        return false;

    // 004c6465..004c6490: all of it (unsigned compare) unlinks the node, otherwise the count
    // drops by Take.
    EquipDesc& Item = *Found;
    unsigned const Have = static_cast<unsigned>(Item.Count);
    if (Take >= Have)
        Loadout.Items.erase(Found);
    else
        Item.set_count(static_cast<int>(Have - Take));

    // 004c6496..004c64a0
    if (ShipView != nullptr)
        ShipView->LoadEquipment(&Loadout);
    return true;
}

bool FInventoryShipModel::AddItem(EquipDesc* Desc, int Count, bool bPlaySound)
{
    // FUN_004c64b0 (RET 0xc). Desc's count is swapped for Count while the copy is added.
    if (Desc == nullptr)
        return false;
    int Add = Count;
    if (Add == 0)
        Add = Desc->Count;
    int const Saved = Desc->Count;
    Desc->set_count(Add);
    Desc->set_equipped(false);
    Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
    if (bPlaySound)
        PlayOrRewindUiSoundSlot(0x10); // FUN_005645c0
    Desc->set_count(Saved);
    return true;
}

bool FInventoryShipModel::MountItem(EquipDesc* Desc, CacheString const* Hardpoint, int Count,
                                    bool bPlaySound)
{
    // FUN_004c6530 (RET 0x10).
    if (Desc == nullptr || ShipView == nullptr)
        return false;

    // 004c6564..004c65fd: does Count of it fit in the hold? The volume is taken for at least
    // one unit; both sides are truncated and compared unsigned.
    bool bNoRoom = false;
    int Mount = Count;
    if (Mount == 0)
        Mount = Desc->Count;
    float Volume = 0.0f;
    if (Desc->ArchId != 0)
    {
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ArchId);
        if (Arch != nullptr)
            Volume = Arch->Volume;
    }
    int const Units = Mount < 1 ? 1 : Mount;
    unsigned const Need =
        static_cast<unsigned>(static_cast<int>(static_cast<double>(Units) * Volume));
    float Capacity = 0.0f;
    double Used = 0.0;
    if (ShipArchId != 0)
    {
        Capacity = Archetype::GetShip(ShipArchId)->HoldSize; // not null-tested
        Used = Loadout.get_cargo_space_occupied();
    }
    unsigned const Free = static_cast<unsigned>(static_cast<int>(Capacity - Used));
    if (Need > Free)
        bNoRoom = true;

    // 004c6602..004c660c
    int const Saved = Desc->Count;
    Desc->set_count(Mount);

    if (Desc->is_internal() && Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
    {
        // 004c662e..004c6698: cargo into the cargo bay.
        if (bNoRoom)
        {
            Desc->set_count(Saved);
            if (bPlaySound)
            {
                // 004c664b..004c6653: FUN_00489e80(DAT_0067339c, 0) -- queue the
                // "insufficient_cargo_space" voice line on the NeuroNet voice queue.
                NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
            }
            return false;
        }
        Desc->set_equipped(false);
        Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
        if (bPlaySound)
            PlayOrRewindUiSoundSlot(0x10);
    }
    else
    {
        // 004c669d..004c66bd: the hardpoints of the ship instance the view shows.
        HardpointSummary Summary;
        Summary.analyze_instance(ShipView->Instance);

        if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        {
            // 004c66d1..004c672b: equipment into the cargo bay.
            if (bNoRoom)
            {
                Desc->set_count(Saved);
                if (bPlaySound)
                {
                    // 004c66ea..004c66f2: FUN_00489e80(DAT_0067339c, 0).
                    NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
                }
                return false;
            }
            Desc->make_internal();
            Desc->set_equipped(false);
        }
        else
        {
            // 004c672d..004c676e: onto a hardpoint the ship has.
            if (Summary.find_name(Hardpoint->Value) == -1)
            {
                Desc->set_count(Saved);
                if (bPlaySound)
                {
                    // 004c674e..004c6757: FUN_00489e80(DAT_0067339c, 0).
                    NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
                }
                return false;
            }
            Desc->set_hardpoint(*Hardpoint);
            if (Hardpoint->Value != EquipDesc::CARGO_BAY_HP_NAME.Value)
                Desc->set_equipped(true);
        }

        // 004c6776..004c67e8
        Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
        if (bPlaySound)
        {
            if (Desc->is_internal())
                PlayOrRewindUiSoundSlot(0x10);
            else
                PlayMountSound(Desc->ArchId); // the same switch, inlined in retail
        }
    }

    // 004c6802..004c6818
    ShipView->LoadEquipment(&Loadout);
    Desc->set_count(Saved);
    return true;
}

void FInventoryShipModel::PlayMountSound(unsigned ArchId)
{
    // FUN_004c6860 (RET 4): get_class_type (vt+0x10) - 6 through the byte table 0x004c68e8.
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr)
        return;
    switch (Arch->get_class_type())
    {
    case Archetype::AClassEquipment:       // 6
    case Archetype::AClassCommodity:       // 0xa
    case Archetype::AClassScanner:         // 0x14
    case Archetype::AClassLight:           // 0x15
    case Archetype::AClassTractor:         // 0x16
    case Archetype::AClassRepairDroid:     // 0x19
    case Archetype::AClassCloakingDevice:  // 0x1c
        PlayOrRewindUiSoundSlot(0x16);
        break;
    case Archetype::AClassPower:           // 0xb
    case Archetype::AClassShield:          // 0xd
    case Archetype::AClassShieldGenerator: // 0xe
        PlayOrRewindUiSoundSlot(0x15);
        break;
    case Archetype::AClassEngine:          // 0xc
    case Archetype::AClassThruster:        // 0xf
        PlayOrRewindUiSoundSlot(0x13);
        break;
    case Archetype::AClassGun:                   // 0x11
    case Archetype::AClassCounterMeasureDropper: // 0x13
    case Archetype::AClassCounterMeasure:        // 0x21
        PlayOrRewindUiSoundSlot(0x12);
        break;
    case Archetype::AClassMineDropper:     // 0x12
    case Archetype::AClassProjectile:      // 0x1e
    case Archetype::AClassMunition:        // 0x1f
    case Archetype::AClassMine:            // 0x20
        PlayOrRewindUiSoundSlot(0x14);
        break;
    default:
        break;
    }
}

void FInventoryShipModel::SetItemHardpoint(EquipDesc* Desc, char const* Hardpoint)
{
    // FUN_004c6910 (RET 8).
    if (Desc == nullptr || ShipView == nullptr)
        return;
    if (_stricmp(Desc->Hardpoint, Hardpoint) == 0) // DACOM stricmp
        return;
    CacheString NewHardpoint;
    NewHardpoint.Value = StringAlloc(Hardpoint, false);
    Desc->set_hardpoint(NewHardpoint);
    // 004c6961: the caller's pointer, not the allocated one, is compared.
    if (Hardpoint != EquipDesc::CARGO_BAY_HP_NAME.Value)
        Desc->set_equipped(true);
    ShipView->LoadEquipment(&Loadout);
    if (Desc->is_internal())
    {
        PlayOrRewindUiSoundSlot(0x10);
        return;
    }
    PlayMountSound(Desc->ArchId); // 004c699c..004c6a14, the same switch inlined
}

bool FInventoryShipModel::ShowItem(EquipDesc const* Item, bool bFlag, int Mode)
{
    // FUN_004c6a50 (RET 0xc).
    if (ShipView == nullptr)
        return false;
    if (Item == nullptr)
    {
        ShipView->SelectGood(0, false);
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    // 004c6a6b..004c6abb and 004c6ac0..004c6b13: the two branches are the same code.
    if ((Item->is_internal() && Mode == 0) || (Item->is_internal() && Mode == 2))
    {
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        int const HpClass = ShipViewEquipDescHpClass(Item, &Weapon, &Shield); // FUN_00583690
        ShipView->SelectHpClass(HpClass, Weapon, Shield, bFlag);             // FUN_00585cd0
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    if (Item->is_internal())
    {
        // 004c6b1e..004c6b64
        ShipView->SelectGood(0, bFlag);
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    // 004c6b27..004c6b46: FUN_00585bd0(FUN_00585e80(hp), 1, 0) -- the outer call's 1 and 0 are
    // pushed before the inner call.
    ShipView->SelectEquipDesc(Item, bFlag);
    ShipView->SelectInstance(ShipView->GetInstanceOnHardpoint(Item->Hardpoint), true, 0);
    return true;
}

namespace
{
    // FUN_004c6b80's 0x90-byte sort record.
    struct FInventorySortRecord
    {
        EquipDesc const* Desc;             // +0x00
        GoodInfo const* Good;              // +0x04
        Archetype::Equipment* Arch;        // +0x08
        unsigned char bUsable;             // +0x0c
        unsigned char bHasName;            // +0x0d
        wchar_t Name[0x40];                // +0x0e
    };
    static_assert(sizeof(FInventorySortRecord) == 0x90, "FUN_004c6b80 record stride 0x90");
}

void FInventoryShipModel::SortFrom(EquipDescList const* Source)
{
    // FUN_004c6b80 (RET 4).
    //
    // 004c6b86..004c6ba1: empty the loadout (std::list::erase(begin, end)).
    Loadout.Items.clear();

    // 004c6ba7..004c6cb1: one record per Source item.
    int const Count = static_cast<int>(Source->Items.size());
    std::vector<FInventorySortRecord> Records(static_cast<size_t>(Count < 0 ? 0 : Count));
    std::list<EquipDesc>::const_iterator SourceIt = Source->Items.begin();
    for (int Index = 0; Index < Count; ++Index, ++SourceIt)
    {
        EquipDesc const& Item = *SourceIt;
        FInventorySortRecord& Record = Records[static_cast<size_t>(Index)];
        GoodInfo const* const Good = GoodList::find_by_archetype(Item.ArchId);
        Record.Desc = &Item;
        Record.Good = Good;
        Record.Arch = Archetype::GetEquipment(Item.ArchId);
        Record.bUsable = 1;
        Record.bHasName = 0;
        int const Filter = NnItemFilterMode(Item.ArchId); // FUN_0047e460

        // 004c6c19..004c6c5e: can the ship use it? Weapons / equipment need a free-able
        // hardpoint, ammo a launcher in Source.
        if (Good != nullptr && ShipView != nullptr)
        {
            if (Filter == 0 || Filter == 2)
            {
                if (ShipView->FindHardpointForGood(Good->Id, true) == nullptr) // FUN_00585f20
                    Record.bUsable = 0;
            }
            else if (Filter == 1)
            {
                Record.bUsable = NnListHasLauncherFor(Source, Item.ArchId) ? 1 : 0;
            }
        }

        // 004c6c60..004c6c95: commodities sort by their name.
        if (ShipView != nullptr && Filter == 4 && Record.Arch != nullptr && !Item.bMission)
        {
            Record.bHasName = 1;
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Record.Arch->IdsName), Record.Name,
                                         0x40); // FUN_004347e0(DAT_0067eca8, ...)
        }
    }

    // 004c6cc0..004c6df1: bubble sort, swapping the whole record, until a pass swaps nothing.
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Count - 1 <= 0)
            break;
        for (int Index = 0; Index < Count - 1; ++Index)
        {
            FInventorySortRecord& A = Records[static_cast<size_t>(Index)];
            FInventorySortRecord& B = Records[static_cast<size_t>(Index) + 1];
            bool bSwap = false;
            if (A.bHasName != 0)
            {
                // 004c6cee..004c6d1d: a named record before an unnamed one; two named ones
                // by _wcsicmp.
                if (B.bHasName == 0)
                    bSwap = true;
                else if (_wcsicmp(A.Name, B.Name) > 0)
                    bSwap = true;
            }
            else if (B.bHasName == 0)
            {
                // 004c6d34..004c6da1: unusable ones last, then mission ones, then by class,
                // then by archetype id (unsigned).
                if (A.bUsable == 0 && B.bUsable != 0)
                    bSwap = true;
                else if (A.bUsable == B.bUsable)
                {
                    unsigned char const MissionA = A.Desc->bMission ? 1 : 0;
                    unsigned char const MissionB = B.Desc->bMission ? 1 : 0;
                    if (MissionA == 0 && MissionB != 0)
                        bSwap = true;
                    else if (MissionA == MissionB)
                    {
                        int ClassA = 0;
                        int ClassB = 0;
                        if (A.Arch != nullptr)
                            ClassA = static_cast<int>(A.Arch->get_class_type());
                        if (B.Arch != nullptr)
                            ClassB = static_cast<int>(B.Arch->get_class_type());
                        if (ClassA > ClassB)
                            bSwap = true;
                        else if (ClassA == ClassB && A.Desc->ArchId > B.Desc->ArchId)
                            bSwap = true;
                    }
                }
            }
            if (bSwap)
            {
                FInventorySortRecord const Swapped = A;
                A = B;
                B = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // 004c6df7..004c6e54: append a copy of each item in the sorted order (operator_new(0x28)
    // node, FUN_004b7390 member copy, size + 1), then free the records.
    for (int Index = 0; Index < Count; ++Index)
        Loadout.Items.push_back(*Records[static_cast<size_t>(Index)].Desc);
}

void FInventoryShipModel::RefreshView()
{
    // FUN_004c6e70
    if (ShipView != nullptr)
        ShipView->LoadEquipment(&Loadout);
}

// ---------------------------------------------------------------------------------------------
// Inventory helpers
// ---------------------------------------------------------------------------------------------

// Static initializer LAB_004c6120.
unsigned G_NnInsufficientCargoSpaceId = CreateID("insufficient_cargo_space");

// Static initializers LAB_004da100 / LAB_004da120.
unsigned G_NanobotArchId = CreateID("ge_s_repair_01");
unsigned G_ShieldBatteryArchId = CreateID("ge_s_battery_01");

// Static initializers LAB_004778c0 / 004778e0 / 00477900 / 00477920 / 00477940 / 00477960 /
// 00477980: CreateID of each voice line name.
unsigned G_NnVoiceInsufficientCargoSpaceId = CreateID("insufficient_cargo_space");
unsigned G_NnVoiceNotEnoughMoneyId = CreateID("not_enough_money");
unsigned G_NnVoiceMountedId = CreateID("mounted");
unsigned G_NnVoiceLoadedIntoCargoHoldId = CreateID("loaded_into_cargo_hold");
unsigned G_NnVoiceNoPlaceToMountId = CreateID("no_place_to_mount");
unsigned G_NnVoiceCargoJettisonedId = CreateID("cargo_jettisoned");
unsigned G_NnVoiceShipPurchaseCompleteId = CreateID("ship_purchase_complete");

// ---------------------------------------------------------------------------------------------
// The player's loadout requests (0x004c4xxx; FUN_004c43d0 / FUN_004c4f90 are in
// character_select_equip.cpp with the list and its counter)
// ---------------------------------------------------------------------------------------------

bool IsPlayerDockedRetail()
{
    // FUN_004c4810: AND 0x3fffffff / NEG / SBB / NEG.
    return (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0;
}

void ReqPlayerEquipmentRetail(EquipDescList const* List)
{
    // FUN_004c4040 (cdecl): G_Server slot 28 (+0x70); G_Server is not null-tested.
    G_Server->ReqEquipment(*List, GetPlayerConnectionId());
}

bool ReqModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                                float Health, unsigned char bEquipped)
{
    // FUN_004c4540 (cdecl): G_Server slot 32 (+0x80), then the local copy.
    G_Server->ReqModifyItem(Id, Hardpoint->Value, Count, Health, bEquipped,
                            GetPlayerConnectionId());
    return ModifyPlayerEquipRetail(Id, Hardpoint, Count, Health, bEquipped);
}

void ReqPlayerShipArchRetail(unsigned ArchId)
{
    // FUN_004c3df0 (cdecl): G_Server slot 25 (+0x64); G_Server is not null-tested.
    G_Server->ReqShipArch(ArchId, GetPlayerConnectionId());
}

void ReqPlayerHullStatusRetail(float Health)
{
    // FUN_004c4f50 (cdecl): G_Server slot 26 (+0x68); the float goes through as a dword.
    G_Server->ReqHullStatus(Health, GetPlayerConnectionId());
}

void ReqPlayerCollisionGroupsRetail(std::list<CollisionGroupDesc> const* Groups)
{
    // FUN_004c3f10 (cdecl): G_Server slot 27 (+0x6c) with the list by reference.
    G_Server->ReqCollisionGroups(*Groups, GetPlayerConnectionId());
}

void ReqPlayerCashRetail(int Cash)
{
    // FUN_004c4cc0 (cdecl): G_Server slot 33 (+0x84).
    G_Server->ReqSetCash(Cash, GetPlayerConnectionId());
}

void ReqPlayerSpendCashRetail(int Amount)
{
    // FUN_004c4d50 (cdecl): G_Server slot 34 (+0x88) with the amount negated.
    G_Server->ReqChangeCash(-Amount, GetPlayerConnectionId());
}

void ReqPlayerGainCashRetail(int Amount)
{
    // FUN_004c4d30 (cdecl): G_Server slot 34 (+0x88) with the amount as given.
    G_Server->ReqChangeCash(Amount, GetPlayerConnectionId());
}

void ReqAddPlayerItemOnHardpointRetail(unsigned ArchId, CacheString const* Hardpoint, float Health)
{
    // FUN_004c41d0 (cdecl): G_Server slot 30 (+0x78) -- (ArchId, *Hardpoint, 1, Health, 1,
    // DAT_00673344).
    G_Server->ReqAddItem(ArchId, Hardpoint->Value, 1, Health, true, GetPlayerConnectionId());
}

void ReqAddPlayerMountedCargoRetail(unsigned ArchId, float Health)
{
    // FUN_004c4200 (cdecl): slot 30 -- (ArchId, CARGO_BAY_HP_NAME, 1, Health, 1, DAT_00673344).
    G_Server->ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME.Value, 1, Health, true,
                         GetPlayerConnectionId());
}

void ReqAddPlayerCargoRetail(unsigned ArchId, int Count, float Health)
{
    // FUN_004c4230 (cdecl): slot 30 -- (ArchId, CARGO_BAY_HP_NAME, Count, Health, 0,
    // DAT_00673344).
    G_Server->ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME.Value, Count, Health, false,
                         GetPlayerConnectionId());
}

bool ReqRemovePlayerEquipRetail(unsigned short Id, int Count)
{
    // FUN_004c4260 (cdecl). 004c4260..004c427f: G_Server slot 31 (+0x7c) -- (Id, Count,
    // DAT_00673344).
    G_Server->ReqRemoveItem(Id, Count, GetPlayerConnectionId());
    // 004c4282..004c431b: byte for byte FUN_004c4320's body (find_equipment_item, the copy, the
    // DAT_00673384 bump, remove_equipment_item(Id, Count), the ObjectShip notification); its
    // result is this one's.
    return RemovePlayerEquipRetail(0, Id, Count);
}

int NnLoadoutResaleValue(EquipDescList const* List, FInventoryShipModel const* Owner,
                         EquipDescList* Saved)
{
    // FUN_004c6f70 (cdecl). 004c6f70..004c6f7f: not docked (DAT_0066873c == 0) -- 0.
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId == 0)
        return 0;

    // 004c6f80..004c6f96: FUN_0043a510(DAT_0066873c) on DAT_00668708; the null test never fails
    // (the market is runtime + 0x30).
    FClientMarket const* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return 0;

    // 004c6f97..004c707b
    int Total = 0;
    unsigned const OwnerTag = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Owner));
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end();
         ++It)
    {
        EquipDesc const& Item = *It;
        GoodInfo const* const Good = GoodList::find_by_archetype(Item.ArchId); // node +0xc
        if (Good == nullptr)
            continue;
        // 004c6fcb..004c6fd6: only the Owner's items when an owner is given (node +0x24).
        if (Owner != nullptr && Item.Owner != OwnerTag)
            continue;

        // 004c6fdc..004c6ff7: held -- no Saved list, or Saved has the item's id (node +0xa).
        bool bHeld = true;
        if (Saved != nullptr && Saved->find_equipment_item(Item.Id) == nullptr)
            bHeld = false;

        if (NnItemFilterMode(Item.ArchId) != 4 && !Good->bCombinable && bHeld) // +0x70
        {
            // 004c7012..004c7043: used goods -- ftol(price * 0.3 + 0.4999990) (DAT_005d6d3c /
            // DAT_005d1914), then ftol(Count * Health * that).
            int const Unit = static_cast<int>(
                static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) * 0.30000001192092896 +
                0.4999989867210388);
            Total += static_cast<int>(static_cast<double>(Item.Count) *
                                      static_cast<double>(Item.Health) * static_cast<double>(Unit));
        }
        else
        {
            // 004c7049..004c7061: ftol(price) * Count.
            Total += static_cast<int>(ClientMarketGetPrice(Market, Good->Id)) * Item.Count;
        }
    }
    return Total;
}

int NnShipResaleValue(unsigned ShipArchId, bool bApplyCondition)
{
    // FUN_004c6e90 (cdecl). 004c6e93..004c6ebb: no ship, not docked, or no market -- 0.
    if (ShipArchId == 0)
        return 0;
    unsigned const BaseId = GetClientLocationCurrentBase(); // DAT_0066873c
    if (BaseId == 0)
        return 0;
    FClientMarket const* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return 0;

    // 004c6ec1..004c6ee3: the ship's good and its market price.
    GoodInfo const* const Good = GoodList_get()->find_by_ship_arch(ShipArchId);
    if (Good == nullptr)
        return 0;
    int const Price = static_cast<int>(ClientMarketGetPrice(Market, Good->Id)); // +0x48
    if (!bApplyCondition)
        return Price;

    // 004c6ef4..004c6f33: the hull scale (FUN_004c4f70) and the mean health of the player's
    // collision groups (FUN_004c3f00, node +0xc), 1.0 without any.
    double const HullScale = static_cast<double>(GetPlayerHealthScaleRetail());
    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    double Mean = 0.0;
    int Count = 0;
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end();
         ++It)
    {
        Mean += static_cast<double>(It->Health);
        ++Count;
    }
    if (Count > 0)
        Mean /= static_cast<double>(Count);
    else
        Mean = 1.0;

    // 004c6f33..004c6f57: ftol(ftol(Price * Mean * HullScale) * 0.8) (DAT_005d6d38).
    int const Conditioned = static_cast<int>(static_cast<double>(Price) * Mean * HullScale);
    return static_cast<int>(static_cast<double>(Conditioned) * 0.800000011920929);
}

int NnItemFilterMode(unsigned ArchId)
{
    // FUN_0047e460 (cdecl).
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr || Arch->IdsName == 0)
        return 5;

    // 0047e487..0047e4c0: when docked (DAT_0066873c, the LocationController's current base),
    // FUN_0043a510 finds that base's market (null-tested) and FUN_00438060 prices
    // Arch2Good(ArchId) in it; FCOMP 0.0, TEST AH,0x44 + JNP: a price of exactly 0.0 returns 5
    // (unordered goes on).
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId != 0)
    {
        FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
        if (Market != nullptr &&
            static_cast<double>(ClientMarketGetPrice(Market, Arch2Good(ArchId))) == 0.0)
            return 5;
    }

    // 0047e4c2..0047e4d1
    if (GoodList::find_by_id(Arch2Good(ArchId)) == nullptr)
        return 5;

    // 0047e4d3..0047e4ea: get_hp_type (vt+0x18) - 2 through the byte table 0x0047e540.
    int const HpType = static_cast<int>(Arch->get_hp_type());
    if (HpType == 2)
        return 3;
    if (HpType == 3)
        return 1;
    if (HpType >= 5 && HpType <= 9)
        return 2;
    if (HpType == 0xe)
        return 4;
    if (HpType >= 0x25 && HpType <= 0x42)
        return 2;
    if (HpType == 4 || (HpType >= 0xa && HpType <= 0xd) || (HpType >= 0xf && HpType <= 0x24))
        return 0;
    return 4; // below 2 or above 0x42: the default case, 0x0047e516
}

int NnInventory::GetItemFilterMode(EquipDesc const* Desc)
{
    // FUN_0047e440 (RET 4; ECX is not read).
    if (Desc == nullptr)
        return 5;
    return NnItemFilterMode(Desc->ArchId); // FUN_0047e460
}

bool NnListHasLauncherFor(EquipDescList const* List, unsigned AmmoArchId)
{
    // FUN_00482d80 (cdecl).
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        Archetype::Equipment* const Arch = Archetype::GetEquipment(It->ArchId);
        if (Arch == nullptr)
            continue;
        Archetype::AClassType const Class = Arch->get_class_type();
        if (Class != Archetype::AClassGun && Class != Archetype::AClassMineDropper &&
            Class != Archetype::AClassCounterMeasureDropper)
            continue;
        Archetype::Launcher const* const Launcher = static_cast<Archetype::Launcher const*>(Arch);
        if (Launcher->ProjectileArchetype.IsEmpty())
            continue;
        Archetype::Equipment* const Ammo = Archetype::GetEquipmentByName(Launcher->ProjectileArchetype);
        if (Ammo != nullptr && Ammo->Nickname.Id == AmmoArchId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// NnInventory
// ---------------------------------------------------------------------------------------------

NnInventory::NnInventory()
    : Mode(0)
{
    // FUN_00477cf0: FUN_0059f860 (WinControl ctor), FUN_004c6140 on +0x330, the CacheString
    // ctor on +0x394, the +0x3a8 list head, EquipDesc::EquipDesc on +0x94c, the two vptrs,
    // then FUN_00477ee0. +0x32c is not written by the ctor (it is set by SetMode); the port
    // starts it at 0.
    Field394.clear();
    ResetFields();
}

void NnInventory::ResetFields()
{
    // FUN_00477ee0, in the order the stores are made.
    // 00477ee7..00477f6b
    FirstRow = 0;              // +0x3c4
    Field3c8 = -1;             // +0x3c8
    SelectedIndex = -1;        // +0x3a4
    HoverIndex = -1;           // +0x3cc
    SelectedItem = nullptr;    // +0x4f4
    InventoryScroll = nullptr; // +0x3d0
    PlayerCash = nullptr;      // +0x3b4
    FilterWeapons = nullptr;   // +0x3d4
    FilterCargoBay = nullptr;  // +0x3d8
    FilterEquipment = nullptr; // +0x3dc
    FilterCommodity = nullptr; // +0x3e0
    FilterAmmo = nullptr;      // +0x3e4
    JettisonButton = nullptr;     // +0x4b0
    InfoCardBackground = nullptr; // +0x4b4
    InfoCardName = nullptr;       // +0x4b8
    InfoCardInfo = nullptr;       // +0x4bc
    InfoCardClose = nullptr;      // +0x4c0
    InfoCardView = nullptr;       // +0x4c4
    InfoCardScroll = nullptr;     // +0x4c8
    Field4cc = nullptr;           // +0x4cc
    Field4d4 = nullptr;           // +0x4d4
    Field4d0 = nullptr;           // +0x4d0
    CloseButton = nullptr;        // +0x4d8

    // 00477f71..00477fa7: five passes over the ten per-row arrays.
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        ItemName[Row] = nullptr;         // EDX - 0x14 (+0x3e8)
        ItemView[Row] = nullptr;         // EDX        (+0x3fc)
        ItemPrice[Row] = nullptr;        // EDX + 0x14 (+0x410)
        ItemQuantity[Row] = nullptr;     // EDX + 0x28 (+0x424)
        ItemButton[Row] = nullptr;       // EDX + 0x3c (+0x438)
        ItemMesh[Row] = nullptr;         // EDX + 0x50 (+0x44c)
        ItemGoodDeal[Row] = nullptr;     // EDX + 0x64 (+0x460)
        CargoMissionText[Row] = nullptr; // EDX + 0xa0 (+0x49c)
        ItemDamage[Row] = nullptr;       // EDX + 0x78 (+0x474)
        EquipToggle[Row] = nullptr;      // EDX + 0x8c (+0x488)
    }

    // 00477fae..004780eb
    Field908 = -1;
    Field90c = -1;
    Field4e1 = 0;
    Field4e2 = 0;
    Field4e3 = 0;
    Field4e4 = 0;
    Result4e5 = 0;
    Field4e0 = 0;
    Field4e6 = 0;
    bNeedBind = 0;
    bDealerMode = 0;
    Field4f0 = 0;
    Dealer = nullptr;
    Field904 = 0;
    ResultFn = nullptr;
    TransferItemButton = nullptr;
    TransferItemView = nullptr;
    TransferItemMesh = nullptr;
    TransferItemName = nullptr;
    TransferItemDamage = nullptr;
    TransferItemPrice = nullptr;
    TransferItemGoodDeal = nullptr;
    Field388 = 0;
    Field38c = 0;
    Field910 = 0;
    Field390 = 0;
    CargoLabel = nullptr;
    Field914 = 0.0f;
    Field918 = 0;
    InfoCardFacts = nullptr;
    InfoCardStats = nullptr;
    InfoCardValues = nullptr;
    for (int Index = 0; Index < 8; ++Index)
        QuantityControl[Index] = nullptr; // +0x91c..+0x938
    Field93c = 1;
    Field940 = 1;
    Field944 = nullptr;
    Field948 = SUBOBJ_ID_NONE;
    Field3b9 = 0;
    Field3b8 = 0;
    Field3c0 = 0;
    Field3c1 = 0;
    Field4e8 = 0;
    Field4ec = 0;
    TradeRouteButton = nullptr;
    TradeRouteText = nullptr;
}

NnInventory::~NnInventory()
{
    // FUN_00477da0.
    //
    // 00477dcf..00477e29: a dealer window still attached to this one is unlinked (FUN_0059dd00
    // with a null parent), its subtree deleted (FUN_0043d2a0 per child), Destroy (slot 10) and
    // the scalar deleting dtor run on it, +0x360 = 0, then FUN_00442850 (the NavBar's dealer
    // tab back in place of the desk tab).
    if (Dealer != nullptr)
    {
        Win3DReparent(Dealer, nullptr);
        NnDealer* const Attached = Dealer;
        if (Attached != nullptr)
        {
            Win3D* Child = Attached->ChildHead;
            while (Child != nullptr)
            {
                Win3D* const Next = Child->NextSibling;
                Win3DDeleteTree(Child);
                Child = Next;
            }
            Attached->Destroy();
            delete Attached;
        }
        Dealer = nullptr;
        NavBarShowDealerTab(); // FUN_00442850
    }

    // 00477e2e..00477e4d: FUN_0045a780(0x66, {0, 0x3d9ba5e3, 0xbfa4dd2f}) -- hand the
    // NN_MainButton the origin of panel 0x66.
    float const PanelOrigin[3] = {0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f)};
    NnMainButtonSetPanelOrigin(0x66, PanelOrigin);

    // 00477e52..00477ec3: the +0x3a8 list, FUN_004c6200 on +0x330 and FUN_0059fa50 run as the
    // member and base destructors.
}

void NnInventory::SetPosition(float const* NewPosition)
{
    // FUN_0047fbb0 (RET 4): WinControl's FUN_005a0880, then the attached dealer's slot 5 with
    // the same position.
    WinControl::SetPosition(NewPosition);
    if (Dealer != nullptr)
        Dealer->SetPosition(NewPosition);
}

bool NnInventory::Create(Win3D* NewParent, void* Data)
{
    // FUN_0047b3e0 (RET 8): FUN_0059dd00(this, NewParent), FUN_0059dbf0(this), flags &= 0xf8,
    // +0x4e7 = 1, return true. Data is not read.
    (void)Data;
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>(Flags & 0xf8);
    bNeedBind = 1;
    return true;
}

void NnInventory::Update()
{
    // FUN_0047b310.
    //
    // 0047b316..0047b339: every child -- bit0 ? slot 11 : slot 12, the next sibling read before
    // the call.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
        Child = Next;
    }

    // 0047b33c..0047b353
    if (bNeedBind != 0)
    {
        BindControls();
        // 0047b34d: MOV ECX,[DAT_0066d3f0] / CALL FUN_0045dc60 -- NN_MainButton drops its
        // "ScaledBackground". No null test on the singleton.
        G_NnMainButton->FreeScaledBackground();
    }

    // 0047b358..0047b365: FUN_0047d2f0(&+0x34c, 0, 1).
    RefreshItemList(&Ship.Loadout, 0, 1);

    // 0047b36a..0047b383: no player ship -> Inventory_Scroll slot 42 (0x37, 0, 0).
    if (GetPlayerShipArchetypeIdRetail() == 0 && InventoryScroll != nullptr)
        InventoryScroll->SetProperty(0x37, 0, 0);

    // 0047b389..0047b3c2: count +0x914 down by the capped frame time, clamping at 0.
    if (Field914 > 0.0f)
    {
        double const Remaining = static_cast<double>(Field914) - GetClientFrameTimeCapped();
        Field914 = static_cast<float>(Remaining);
        if (Remaining < 0.0)
            Field914 = 0.0f;
    }
}

void NnInventory::Close()
{
    // FUN_0047ad20. 0047ad23..0047ad4d: hide the attached dealer and reset its trade mode.
    if (Dealer != nullptr && (Dealer->Flags & kWin3DVisible) != 0)
    {
        Dealer->Flags = static_cast<unsigned char>(Dealer->Flags & 0xfc);
        if (Dealer->TradeMode != 0)
            Dealer->TradeMode = 0;
    }

    // 0047ad57..0047ad65: FUN_004c62b0(player's EquipDescList, 1).
    Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);

    // 0047ad6a..0047ad90
    Field4e3 = 0;
    if (ResultFn != nullptr)
    {
        ResultFn(Result4e5, ResultArg);
        ResultFn = nullptr;
    }

    // 0047ad9a..0047add0: unlink, delete the subtree, Destroy, delete this.
    Win3DReparent(this, nullptr);
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

bool NnInventory::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0047ade0 (RET 0xc).
    if (Msg == 0xef02)
    {
        // 0047adf2..0047aea2: the listing inlines FUN_0047ad20's body instruction for
        // instruction (0047adf2..0047aea0 == 0047ad23..0047add0), then returns 1.
        Close();
        return true;
    }

    if (Msg == 0xef01)
    {
        // 0047aeaf..0047af0c
        WinControl::Slot23(0xef01, A, B);
        if (Dealer != nullptr)
            Dealer->Flags = static_cast<unsigned char>(Dealer->Flags | 3);
        Update();
        if (Dealer != nullptr)
        {
            Dealer->Update();
            if (Dealer->TradeMode == 2)
            {
                Dealer->RestorePlayerShipView();
                Update();
                Dealer->Update();
            }
        }
        return true;
    }

    // 0047af15..0047af22
    return WinControl::Slot23(Msg, A, B);
}

void NnInventory::ShowQuantityControls(bool bShow)
{
    // FUN_004795f0 (RET 4). No test on the eight pointers themselves -- only +0x91c is checked.
    if (QuantityControl[0] == nullptr)
        return;
    if (bShow)
    {
        for (int Index = 0; Index < 8; ++Index)
            QuantityControl[Index]->Flags = static_cast<unsigned char>(QuantityControl[Index]->Flags | 3);
        if (InfoCardInfo == nullptr || Field944 == nullptr)
            return;
        InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);
        Field944->Flags = static_cast<unsigned char>(Field944->Flags & 0xfc);
        return;
    }
    for (int Index = 0; Index < 8; ++Index)
        QuantityControl[Index]->Flags = static_cast<unsigned char>(QuantityControl[Index]->Flags & 0xfc);
    if (InfoCardInfo == nullptr || Field944 == nullptr)
        return;
    InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
    Field944->Flags = static_cast<unsigned char>(Field944->Flags | 3);
}

bool NnInventory::Slot46()
{
    // FUN_0044fd00: XOR AL,AL / RET.
    return false;
}

void NnInventory::ClearSelection()
{
    // The sequence the listing inlines after most list changes (e.g. 0x0047f2a3..0x0047f2e2,
    // 0x0047d432..0x0047d46f, 0x0047a33f..0x0047a37c): +0x3a4 = -1, +0x4f4 = 0,
    // FUN_004c6a50(0, 0, 5) on +0x330, FUN_004795f0(0), and the attached dealer's +0x4e8 /
    // +0x4ec = 0.
    SelectedIndex = -1;
    SelectedItem = nullptr;
    Ship.ShowItem(nullptr, 0, 5);
    ShowQuantityControls(false);
    if (Dealer != nullptr)
    {
        Dealer->Field4e8 = 0;
        Dealer->Field4ec = 0;
    }
}

void NnInventory::UpdateFilterButtons()
{
    // FUN_0047e590: slot 42 (0xc, Mode == N, 0) on each filter button that exists.
    if (FilterCargoBay != nullptr)
        FilterCargoBay->SetProperty(0xc, Mode == 3 ? 1 : 0, 0);
    if (FilterWeapons != nullptr)
        FilterWeapons->SetProperty(0xc, Mode == 0 ? 1 : 0, 0);
    if (FilterEquipment != nullptr)
        FilterEquipment->SetProperty(0xc, Mode == 2 ? 1 : 0, 0);
    if (FilterCommodity != nullptr)
        FilterCommodity->SetProperty(0xc, Mode == 4 ? 1 : 0, 0);
    if (FilterAmmo != nullptr)
        FilterAmmo->SetProperty(0xc, Mode == 1 ? 1 : 0, 0);
}

void NnInventory::SetMode(int NewMode, bool bPropagate)
{
    // FUN_0047f240 (RET 8).
    Mode = NewMode;
    if (Dealer == nullptr && bDealerMode == 0)
        G_NnInventoryLastMode = NewMode;

    FirstRow = 0;
    SelectedIndex = -1;
    UpdateFilterButtons();
    ShowQuantityControls(false);
    if (InventoryScroll != nullptr)
        InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow), 0);

    // 0047f2a2..0047f2e2
    ClearSelection();

    if (InventoryScroll != nullptr)
        InventoryScroll->SetProperty(0x38, 0, 0);

    // 0047f2fe..0047f368: mirror the mode onto the dealer unless it is in trade mode 1.
    if (Dealer != nullptr && bPropagate && Dealer->TradeMode != 1)
    {
        Dealer->SetMode(Mode, true);
        Dealer->ClearSelection();
    }

    // 0047f36e..0047f3b5: the ship view's slot 42 (0x4c, n, 0) -- 0 for weapons, 1 for
    // equipment, 2 for ammo / cargo bay / commodity and the unused 5; nothing above 5. The
    // switch reads the argument (param_1), not +0x32c.
    if (Ship.ShipView == nullptr)
        return;
    switch (NewMode)
    {
    case 0:
        Ship.ShipView->SetProperty(0x4c, 0, 0);
        break;
    case 2:
        Ship.ShipView->SetProperty(0x4c, 1, 0);
        break;
    case 1:
    case 3:
    case 4:
    case 5:
        Ship.ShipView->SetProperty(0x4c, 2, 0);
        break;
    default:
        break;
    }
}

namespace
{
    // FUN_0047af30's 8-byte record: the item's EquipDesc id and its sort price.
    struct FTransferRecord
    {
        unsigned short Id; // +0
        float Price;       // +4
    };
}

void NnInventory::TransferEquipmentFromDealer()
{
    // FUN_0047af30.
    if (Dealer == nullptr)
        return;
    Archetype::Ship* const ShipArch = Archetype::GetShip(Ship.ShipArchId);
    if (ShipArch == nullptr)
        return;

    // 0047af67..0047af7b: the dealer's model shows the player's list.
    Dealer->Ship.LoadShip(&GetPlayerEquipDescListRetail(), 0);

    // 0047af80..0047afcc: one record per dealer item.
    EquipDescList& Source = Dealer->Ship.Loadout;
    int const Count = static_cast<int>(Source.Items.size());
    std::vector<FTransferRecord> Records(static_cast<size_t>(Count < 0 ? 0 : Count));
    // 0047afa9..0047afcc: when docked (DAT_0066873c, JBE) FUN_0043a510 returns the base's
    // market; otherwise none.
    FClientMarket* Market = nullptr; // [ESP+0x14]
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId != 0)
        Market = GetClientLocationBaseMarket(BaseId);

    // 0047afd0..0047b074: per dealer item, the record {Id, price}. Without a market the price is
    // DAT_005d15b8 = 1e8. With one: Arch2Good 0, or an engine (0xc) / scanner (0x14) / tractor
    // (0x16) / power plant (0xb) (GetEquipment's result is not null-tested) -> record {0, 0}
    // (0x0047b14a); otherwise FUN_00438060's price, 1e8 when it is exactly 0.0 (FCOM 0, TEST
    // AH,0x44 + JP keeps any other price, unordered included).
    std::list<EquipDesc>::const_iterator SourceIt = Source.Items.begin();
    for (int Index = 0; Index < Count; ++Index, ++SourceIt)
    {
        FTransferRecord& Record = Records[static_cast<size_t>(Index)];
        Record.Id = SourceIt->Id;
        if (Market == nullptr)
        {
            Record.Price = 100000000.0f; // DAT_005d15b8
            continue;
        }
        unsigned const GoodId = Arch2Good(SourceIt->ArchId);
        int const Class =
            static_cast<int>(Archetype::GetEquipment(SourceIt->ArchId)->get_class_type()); // vt+0x10
        if (GoodId == 0 || Class == Archetype::AClassEngine || Class == Archetype::AClassScanner ||
            Class == Archetype::AClassTractor || Class == Archetype::AClassPower)
        {
            Record.Id = 0;
            Record.Price = 0.0f;
            continue;
        }
        float const Price = ClientMarketGetPrice(Market, GoodId);
        Record.Price = static_cast<double>(Price) == 0.0 ? 100000000.0f : Price;
    }

    // 0047b07e..0047b0c0: bubble sort, most expensive first (swap when the left price is below
    // the right one, TEST AH,5 / JP skipping >= and unordered).
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
            break;
        for (int Index = 0; Index < Last; ++Index)
        {
            FTransferRecord& A = Records[static_cast<size_t>(Index)];
            FTransferRecord& B = Records[static_cast<size_t>(Index) + 1];
            if (A.Price < B.Price)
            {
                FTransferRecord const Swapped = A;
                A = B;
                B = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // 0047b0c2..0047b24f: mount each onto this ship where it fits.
    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned short const Id = Records[static_cast<size_t>(Index)].Id;
        if (Id == 0)
            continue;
        EquipDesc* const Desc = Source.find_equipment_item(Id);
        if (Desc == nullptr)
            continue;

        // 0047b0fa..0047b12c: the hardpoint the view picks; an empty name means the cargo bay.
        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(Ship.ShipView->FindHardpointForEquipDesc(Desc, false), false);
        if (Hardpoint.Value[0] == '\0')
            Hardpoint.Value = EquipDesc::CARGO_BAY_HP_NAME.Value;

        // 0047b130..0047b176: nanobots and shield batteries are capped at the hull's limit
        // (a negative limit caps nothing).
        int Mount = Desc->Count;
        int Limit = 0;
        bool bLimited = false;
        if (Desc->ArchId == G_NanobotArchId)
        {
            Limit = ShipArch->NanobotLimit; // +0x140
            bLimited = true;
        }
        else if (Desc->ArchId == G_ShieldBatteryArchId)
        {
            Limit = ShipArch->ShieldBatteryLimit; // +0x144
            bLimited = true;
        }
        if (bLimited && Mount > Limit && Limit >= 0)
            Mount = Limit;

        // 0047b178..0047b1a3
        if (Ship.MountItem(Desc, &Hardpoint, Mount, false))
        {
            Dealer->Ship.RemoveItem(Desc, Mount);
            continue;
        }

        // 0047b1a8..0047b238: a stack that did not fit moves as many units as the hold takes.
        if (Desc->Count <= 1)
            continue;
        float Capacity = 0.0f;
        float Used = 0.0f;
        Ship.GetCargoCapacityAndUsed(&Capacity, &Used);
        float const Free = Capacity - Used;
        if (Desc->ArchId == 0)
            continue;
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ArchId);
        if (Arch == nullptr)
            continue;
        if (!(Arch->Volume > 0.0f))
            continue;
        int Fit = static_cast<int>(static_cast<double>(Free) / Arch->Volume);
        if (Fit > Mount)
            Fit = Mount;
        if (Fit <= 0)
            continue;
        if (Ship.MountItem(Desc, &Hardpoint, Fit, false))
            Dealer->Ship.RemoveItem(Desc, Fit);
    }

    // 0047b255..0047b2f6: free the records; the dealer's +0x4f4 = 0, then the same selection
    // reset on this window and on the dealer.
    NnDealer* const Partner = Dealer;
    Partner->SelectedItem = nullptr;
    ClearSelection();
    Partner->ClearSelection();
}

void NnInventory::TakeDealerSelection()
{
    // FUN_0047e920. Caller: NN_Dealer's SetProperty 0x00482755.
    if (Dealer == nullptr)
        return;
    EquipDesc* const Item = Dealer->GetSelectedItem(); // FUN_0047e790 on the dealer
    if (Item == nullptr)
        return;
    int const Count = Dealer->Field988;
    if (Count == 0)
        return;

    // 0047e953..0047e99d: into the ship's own list as an unmounted, internal item; the hardpoint
    // is the one the ship view offers for it (FUN_00585ef0), else the cargo bay. The local
    // CacheString is not released afterwards (retail has no clear on it either).
    Item->make_internal();
    Item->set_equipped(false);
    char const* Hardpoint = Ship.ShipView->FindHardpointForEquipDesc(Item, false);
    if (Hardpoint == nullptr)
        Hardpoint = EquipDesc::CARGO_BAY_HP_NAME.Value;
    CacheString HardpointName;
    HardpointName.Value = StringAlloc(Hardpoint, false);

    // 0047e9a1..0047e9b9: FUN_004c6530; when it took the item, FUN_004c6430 on the dealer's model.
    if (Ship.MountItem(Item, &HardpointName, Count, true))
        Dealer->Ship.RemoveItem(Item, Count);

    // 0047e9be..0047e9cc
    SelectedItem = nullptr;         // +0x4f4
    Dealer->SelectedItem = nullptr; // dealer +0x4f4
}

void NnInventory::BuyFromDealer(int Count)
{
    // FUN_0047eab0 (RET 4). Caller: NN_Dealer's SetProperty 0x00482833.
    if (Count == 0)
        return;
    if (Dealer == nullptr)
        return;
    unsigned const GoodId = Dealer->GetSelectedMarketGood(); // FUN_00483580
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    if (Good == nullptr)
        return;

    // 0047eaf6..0047eb25: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested) and its
    // entry FUN_00437d90(GoodId); no entry, or none in stock (+0x1c <= 0) -- straight to the
    // tail (0x0047ee4c).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    FClientMarketGood* const Entry = ClientMarketFindEntry(Market, GoodId);
    if (Entry != nullptr && Entry->Stock > 0)
    {
        // 0047eb2b..0047eb4b: Count = min(Count, stock) (JNC: unsigned), the market loses it and
        // the server is asked to sell it (FUN_00438220(base, good, Count)).
        if (static_cast<unsigned>(Entry->Stock) < static_cast<unsigned>(Count))
            Count = Entry->Stock;
        Entry->Stock -= Count;
        ClientMarketRequestBuy(Market, GetClientLocationCurrentBase(), GoodId, Count);

        // 0047eb50..0047eb63: the last one gone with the info card up -- clear the card.
        if (Entry->Stock == 0 && InfoCardFacts != nullptr) // +0x398
            ClearInfoCard(); // FUN_0047e9e0

        // 0047eb68..0047ecc7: where the good goes. [ESP+0x13] -- whether the good takes hold
        // space -- starts 1.
        bool bInHold = true;
        if (Dealer->TradeMode == 3)
        {
            if (Field394.Value[0] != '\0')
            {
                // 0047eb95..0047ec2e: onto the hint hardpoint, one, at full health; it takes no
                // hold space. A gun whose projectile (+0x98 by name) does not require ammo
                // (Projectile +0x78 == 0) joins weapon group 0 (FUN_004f6200); voice "mounted";
                // the mount sound (FUN_004c6860, this +0x330).
                ReqAddPlayerItemOnHardpointRetail(Good->ArchId, &Field394, 1.0f); // FUN_004c41d0
                char const* const Hardpoint = Field394.Value;
                bInHold = false;
                Archetype::Equipment const* const Equip = Archetype::GetEquipment(Good->ArchId);
                if (Equip != nullptr && Equip->get_class_type() == Archetype::AClassGun)
                {
                    ID_String const& ProjectileName =
                        static_cast<Archetype::Launcher const*>(Equip)->ProjectileArchetype;
                    if (!ProjectileName.IsEmpty())
                    {
                        Archetype::Equipment const* const Projectile =
                            Archetype::GetEquipmentByName(ProjectileName);
                        if (Projectile != nullptr &&
                            !static_cast<Archetype::Projectile const*>(Projectile)->bRequiresAmmo)
                            G_WeaponGroups.AddHardpoint(0, Hardpoint); // FUN_004f6200
                    }
                }
                NnVoiceEnqueue(G_NnVoiceMountedId, 0); // FUN_00489e80(DAT_00671c70, 0)
                FInventoryShipModel::PlayMountSound(Good->ArchId);
            }
            else if (NnItemFilterMode(Good->ArchId) == 3 && Count == 1 &&
                     Good->ArchId != G_NanobotArchId && Good->ArchId != G_ShieldBatteryArchId &&
                     !HasEquippedOfClass(Good->ArchId))
            {
                // 0047ec93..0047ecb6: a single cargo-bay item with nothing of its class equipped
                // goes into the bay mounted; voice "mounted".
                ReqAddPlayerMountedCargoRetail(Good->ArchId, 1.0f); // FUN_004c4200
                NnVoiceEnqueue(G_NnVoiceMountedId, 0);
            }
            else
            {
                // 0047ec6a..0047ec91: into the hold; voice "loaded into cargo hold".
                ReqAddPlayerCargoRetail(Good->ArchId, Count, 1.0f); // FUN_004c4230
                NnVoiceEnqueue(G_NnVoiceLoadedIntoCargoHoldId, 0); // DAT_00671c6c
            }
        }
        else
        {
            // 0047ecb8..0047ecc7: the other dealers -- into the hold, no voice.
            ReqAddPlayerCargoRetail(Good->ArchId, Count, 1.0f);
        }

        // 0047ecca..0047ece4: pay the package price per unit (FUN_004c4d50); +0x4e5 = 0,
        // +0x3b8 = 1.
        ReqPlayerSpendCashRetail(GetMarketPackagePrice(Entry->Info.GoodId) * Count);
        Result4e5 = 0;
        Field3b8 = 1;

        // 0047eceb..0047ee48: a good with an ammo package (GoodInfo +0xb8 / +0xbc > 0).
        if (Good->FreeAmmoArchId != 0 && Good->FreeAmmoCount > 0)
        {
            // 0047ed0a..0047ed3c: the gun's volume, counted only while it went into the hold.
            float GunVolume = 0.0f;
            Archetype::Equipment const* const Gun = Archetype::GetEquipment(Good->ArchId);
            if (Gun != nullptr)
                GunVolume = Gun->Volume;
            if (!bInHold)
                GunVolume = 0.0f;

            // 0047ed3c..0047eda6: the ammo bought from the market -- only when the ammo has
            // equipment: as much of its stock as the package holds (JGE keeps the package
            // count), the market loses it, FUN_00438220; no entry or no stock -- none. Without
            // equipment the package count stands and nothing is asked of the market.
            float AmmoVolume = 0.0f;
            int AmmoCount = Good->FreeAmmoCount;
            Archetype::Equipment const* const Ammo = Archetype::GetEquipment(Good->FreeAmmoArchId);
            if (Ammo != nullptr)
            {
                AmmoVolume = Ammo->Volume;
                unsigned const AmmoGood = Arch2Good(Good->FreeAmmoArchId);
                FClientMarketGood* const AmmoEntry = ClientMarketFindEntry(Market, AmmoGood);
                if (AmmoEntry != nullptr && AmmoEntry->Stock > 0)
                {
                    if (AmmoEntry->Stock < AmmoCount)
                        AmmoCount = AmmoEntry->Stock;
                    AmmoEntry->Stock -= AmmoCount;
                    ClientMarketRequestBuy(Market, GetClientLocationCurrentBase(), AmmoGood,
                                           AmmoCount);
                }
                else
                    AmmoCount = 0;
            }

            // 0047eda8..0047edf8: what fits -- (hold size - used - Count (FILD qword: unsigned)
            // * gun volume) / ammo volume, ftol, unclamped, then at most the ammo bought; an
            // ammo volume of exactly 0.0 (TEST AH,0x44 + JNP) leaves the ammo bought.
            float const HoldSize = GetPlayerShipHoldSizeRetail();                               // FUN_004c4610
            float const Used = GetPlayerEquipDescListRetail().get_cargo_space_occupied();       // FUN_004c4640
            double const Free = static_cast<double>(HoldSize) - static_cast<double>(Used) -
                                static_cast<double>(static_cast<unsigned>(Count)) *
                                    static_cast<double>(GunVolume);
            int Take = AmmoCount;
            if (static_cast<double>(AmmoVolume) != 0.0)
            {
                Take = static_cast<int>(Free / static_cast<double>(AmmoVolume)); // ftol
                if (Take > AmmoCount)
                    Take = AmmoCount;
            }

            // 0047edfc..0047ee45: at most MAX_PLAYER_AMMO less the ammo carried
            // (find_matching_cargo(ammo, 0, 1.0f) on +0x34c); a positive count goes into the
            // hold (FUN_004c4230).
            int Room = MAX_PLAYER_AMMO;
            EquipDesc const* const Carried =
                Ship.Loadout.find_matching_cargo(Good->FreeAmmoArchId, 0, 1.0f);
            if (Carried != nullptr)
                Room -= Carried->Count;
            if (Take > Room)
                Take = Room;
            if (Take > 0)
                ReqAddPlayerCargoRetail(Good->FreeAmmoArchId, Take, 1.0f);
        }
    }

    // 0047ee4c..0047ee85: remember the good for the next list refresh (not for ships).
    if (Dealer->TradeMode == 2)
        return;
    Field3bc = GoodId; // +0x3bc
    if (Mode == 0 || Mode == 2)
        Field3b9 = 1;  // +0x3b9
    else
        Field3c0 = 1;  // +0x3c0
}

void NnInventory::SellSelected(int Count)
{
    // FUN_0047eea0 (RET 4). Caller: NN_Dealer's SetProperty 0x00482859.
    if (Count == 0)
        return;
    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
    if (Item == nullptr)
        return;
    if (Mode == 0) // +0x32c, weapons
        G_WeaponGroups.RemoveHardpoint(Item->Hardpoint); // FUN_004f6380

    // 0047eedf..0047ef13: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested), entry =
    // FUN_00437d90(Arch2Good(Item->ArchId)); no entry -- straight to the NavBar refresh.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    unsigned const GoodId = Arch2Good(Item->ArchId); // [ESP+0x14]
    FClientMarketGood* const Entry = ClientMarketFindEntry(Market, GoodId);
    if (Entry != nullptr)
    {
        // 0047ef19..0047ef26: Count = min(Count, the item's count) (JNC: unsigned); the market
        // gains it.
        int const ItemCount = Item->Count; // [ESP+0x18]
        if (static_cast<unsigned>(ItemCount) < static_cast<unsigned>(Count))
            Count = ItemCount;
        Entry->Stock += Count;

        // 0047ef29..0047efbe: the payment. A used item -- not a commodity (filter mode != 4),
        // not combinable, and in the saved loadout +0x3a8 (find_equipment_item(Id)) -- fetches
        // ftol(Health * Count (FIMUL) * (price * 0.3 + 0.4999990)) (DAT_005d0e2c /
        // DAT_005d1914, all on the x87 stack); anything else ftol(price * Count).
        int Cash = 0;
        if (NnItemFilterMode(Item->ArchId) != 4 && !arch_is_combinable(Item->ArchId) &&
            SavedLoadout.find_equipment_item(Item->Id) != nullptr)
        {
            double const Unit =
                static_cast<double>(ClientMarketGetPrice(Market, Entry->Info.GoodId)) *
                    0.30000001192092896 +
                0.4999989867210388;
            Cash = static_cast<int>(static_cast<double>(Item->Health) *
                                    static_cast<double>(Count) * Unit);
        }
        else
        {
            Cash = static_cast<int>(
                static_cast<double>(ClientMarketGetPrice(Market, Entry->Info.GoodId)) *
                static_cast<double>(Count));
        }
        ReqPlayerGainCashRetail(Cash); // FUN_004c4d30

        // 0047efc9..0047f011: +0x3b8 = 1; the server buys it (FUN_00438270(base, good, Count));
        // the item leaves the player's loadout (FUN_004c4260(Id, Count)); the ship model shows
        // the loadout again (FUN_004c62b0(FUN_004c4060(), 1)); +0x4e5 = 0.
        unsigned short const ItemId = Item->Id;
        Field3b8 = 1;
        ClientMarketRequestSell(Market, GetClientLocationCurrentBase(), GoodId, Count);
        ReqRemovePlayerEquipRetail(ItemId, Count);
        Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);
        Result4e5 = 0;

        // 0047f00d..0047f026: the whole stack sold with the info card up -- clear the card.
        if (Count == ItemCount && InfoCardFacts != nullptr) // +0x398
            ClearInfoCard(); // FUN_0047e9e0
    }

    // 0047f02b..0047f032: FUN_004427e0(0) -- the NavBar's repair tab refreshed.
    NavBarRefreshRepairTab(false);
}

int NnInventory::GetMarketPackagePrice(unsigned GoodId)
{
    // FUN_0047f040 (RET 4). 0047f043..0047f062: FUN_0043a510(DAT_0066873c) on DAT_00668708 --
    // no market at the current base returns 0 (EAX is the null market).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    if (Market == nullptr)
        return 0;

    // 0047f065..0047f0a6: the good's own price; a good with an ammo package (GoodInfo +0xb8
    // archetype, +0xbc count > 0) goes on.
    int Price = static_cast<int>(ClientMarketGetPrice(Market, GoodId)); // ftol
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    if (Good == nullptr || Good->FreeAmmoArchId == 0 || Good->FreeAmmoCount <= 0)
        return Price;

    // 0047f0ac..0047f0f7: the ammo good's price; n = the package count capped by the market's
    // stock of it (entry +0x1c; 0 without an entry or a positive stock).
    unsigned const AmmoGood = Arch2Good(Good->FreeAmmoArchId);
    int const AmmoPrice = static_cast<int>(ClientMarketGetPrice(Market, AmmoGood)); // ftol
    int Count = Good->FreeAmmoCount;
    FClientMarketGood const* const Entry = ClientMarketFindEntry(Market, AmmoGood); // FUN_00437d90
    if (Entry == nullptr || Entry->Stock <= 0)
        Count = 0;
    else if (Entry->Stock < Count)
        Count = Entry->Stock;

    // 0047f0f7..0047f151: the gun's and the ammo's volume (Equipment +0x5c; 0 without one).
    float GunVolume = 0.0f;
    unsigned const GunArch = Good2Arch(GoodId);
    if (GunArch != 0)
    {
        Archetype::Equipment const* const Gun = Archetype::GetEquipment(GunArch);
        if (Gun != nullptr)
            GunVolume = Gun->Volume;
    }
    float AmmoVolume = 0.0f;
    unsigned const AmmoArch = Good2Arch(AmmoGood);
    if (AmmoArch != 0)
    {
        Archetype::Equipment const* const Ammo = Archetype::GetEquipment(AmmoArch);
        if (Ammo != nullptr)
            AmmoVolume = Ammo->Volume;
    }

    // The ship whose hold and loadout count: the partner's on a dealer (+0x35c), else this one's.
    NnInventory* const Owner = bDealerMode != 0 ? static_cast<NnDealer*>(this)->Inventory : this;

    // 0047f151..0047f1d3: both volumes above 0 (FCOMP 0, TEST AH,0x41 + JNZ skips <= and
    // unordered): n capped by ftol((capacity - used - gun volume) / ammo volume), at least 0.
    if (static_cast<double>(GunVolume) > 0.0 && static_cast<double>(AmmoVolume) > 0.0)
    {
        float Capacity = 0.0f;
        float Used = 0.0f;
        Owner->Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
        int Fit = static_cast<int>((static_cast<double>(Capacity) - static_cast<double>(Used) -
                                    static_cast<double>(GunVolume)) /
                                   static_cast<double>(AmmoVolume)); // ftol
        if (Fit < 0)
            Fit = 0;
        if (Count > Fit)
            Count = Fit;
    }

    // 0047f1d9..0047f22c: n capped by MAX_PLAYER_AMMO less the ammo already carried
    // (find_matching_cargo(ammo archetype, 0, 1.0f) on the owner's loadout), then the ammo's
    // price n times.
    int Room = MAX_PLAYER_AMMO;
    EquipDesc const* const Carried =
        Owner->Ship.Loadout.find_matching_cargo(Good->FreeAmmoArchId, 0, 1.0f);
    if (Carried != nullptr)
        Room -= Carried->Count;
    if (Count > Room)
        Count = Room;
    Price += Count * AmmoPrice;
    return Price;
}

void NnInventory::Slot45()
{
    // FUN_00478100 -- build the window's controls. Every position is a float[3] in the window's
    // space; the retail code builds them on the stack (FUN_00422b80 / FUN_00423b30), the port
    // with the same arithmetic. Where x87 keeps an intermediate at extended precision across
    // two operations, the port carries it in double.
    unsigned char bShowFilters = 1;    // [ESP+0x27]
    unsigned char bShowCommodity = 1;  // [ESP+0x1f]
    float Base[3] = {0.0f, 0.0f, 0.0f};         // [ESP+0x10] -- the item list's origin
    float FilterBase[3] = {0.0f, 0.0f, 0.0f};   // [ESP+0x50] -- the filter row's origin
    unsigned char bOwnWindow = 0;      // [ESP+0x1e] -- the player's own inventory
    float PriceColumn = 0.025f;        // [ESP+0x20] (0x3ccccccd)

    if (Dealer != nullptr)
    {
        // 0047815e..00478207: the half next to a dealer.
        SetMesh("Interface\\NeuroNet\\Inventory\\INV_biginforcard.cmp", 0); // slot 38
        HalfExtents[0] = 0.66f;  // +0xb4
        HalfExtents[1] = 0.397f;
        HalfExtents[2] = 0.0f;
        HitOffset[0] = 0.64f;    // +0xc0
        HitOffset[1] = -0.398f;
        HitOffset[2] = 0.0f;
        if (Dealer->TradeMode == 1) // +0x978
        {
            bShowFilters = 0;
            bShowCommodity = 0;
            PriceColumn = 0.0f;
        }
        else if (Dealer->TradeMode == 3)
        {
            bShowCommodity = 0;
        }
    }
    else if (bDealerMode == 0)
    {
        // 0047821a..00478316: the player's own inventory window.
        SetMesh("Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp", 0); // slot 38
        float const ClosePosition[3] = {0.401f, 0.237f, 0.0f};
        float const CloseHalfExtents[3] = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("Inventory_Close", // FUN_005a0c90
                                          "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
                                          "ui_select_item", nullptr, ClosePosition, nullptr,
                                          CloseHalfExtents, 0, 1, 0, 0, 0x409, 0, 0);
        WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder); // FUN_005a1340
        Base[0] = -0.43f;
        Base[1] = 0.258f;
        Base[2] = 0.0f;
        FilterBase[0] = -0.43f;
        FilterBase[1] = 0.244f;
        FilterBase[2] = 0.0f;
        bOwnWindow = 1;
    }

    // 0047831b..00478333: no ship and no dealer -- no filter buttons at all.
    if (GetPlayerShipArchetypeIdRetail() == 0 && Dealer == nullptr) // FUN_004c3e10
    {
        bShowFilters = 0;
        bShowCommodity = 0;
    }

    // 00478338..004783d2: "PlayerTitle" (FUN_005a0e30); the result is not kept here.
    if (bOwnWindow != 0)
    {
        float const TitlePosition[3] = {-0.42f, 0.261f, 0.0f};
        CreateTextControl("PlayerTitle", "PageHeader", TitlePosition, nullptr, 0.839f, 0.03f,
                          1.65f, 0x213f, 1, 0, 1);
    }
    else
    {
        float const Delta[3] = {0.219f, -0.039f, 0.0f};
        float const TitlePosition[3] = {Base[0] + Delta[0], Base[1] + Delta[1],
                                        Base[2] + Delta[2]}; // FUN_00423b30
        CreateTextControl("PlayerTitle", "PageHeader", TitlePosition, nullptr, 0.394f, 0.03f,
                          1.65f, 0x213f, 1, 0, 0);
    }

    // 004783da..00478433: "PlayerCash".
    {
        float const Delta[3] = {0.219f, -0.072f, 0.0f};
        float const CashPosition[3] = {Base[0] + Delta[0], Base[1] + Delta[1],
                                       Base[2] + Delta[2]}; // FUN_00423b30
        CreateTextControl("PlayerCash", "SubHeader", CashPosition, nullptr, 0.394f, 0.03f, 1.4f,
                          0, 1, 0, 0);
    }

    // 00478438..0047844e: the price column's x.
    float const PriceX = PriceColumn + 0.293f; // DAT_005d1554

    // 0047844a..00478a85: the five item rows. Names count from 1, the row offset from 0.
    char Name[0x100];
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        int const Number = Row + 1;

        // 00478461..004784fe: "ItemButton%d". The product is exact in double; x87 subtracts
        // the double 0.131 (DAT_005d1538) from it before the float stores.
        sprintf(Name, "ItemButton%d", Number);
        double const RowOffset = static_cast<double>(Row) * static_cast<double>(-0.068f); // DAT_005d1540
        float const RowY = static_cast<float>(RowOffset);                 // [ESP+0x20]
        float const ButtonY = static_cast<float>(RowOffset - 0.131);      // [ESP+0x4c]
        {
            float const ButtonPosition[3] = {Base[0] + 0.067f, Base[1] + ButtonY,
                                             Base[2] + 0.0f}; // FUN_00423b30
            float const ButtonHalfExtents[3] = {0.035f, 0.0335f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ButtonPosition, ButtonHalfExtents,
                                    0, 0, 0, 0, 0); // FUN_005a0d00
        }

        // 00478503..0047859b: "ItemView0%d".
        sprintf(Name, "ItemView0%d", Number);
        {
            float const ViewPosition[3] = {Base[0] + 0.248f, Base[1] + ButtonY,
                                           Base[2] + 0.0f}; // FUN_00423b30
            float const ViewHalfExtents[3] = {0.138f, 0.034f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ViewPosition, ViewHalfExtents, 0, 0,
                                    0, 0, 0); // FUN_005a0d00
        }

        // 004785a0..00478670: "ItemMesh%d" -- a static mesh placeholder.
        sprintf(Name, "ItemMesh%d", Number);
        double const MeshOffset = static_cast<double>(RowY) - static_cast<double>(0.131f); // DAT_005d150c
        float const MeshRowY = static_cast<float>(MeshOffset); // [ESP+0x68]
        {
            float const MeshPosition[3] = {Base[0] + 0.066999994f, // DAT_005d1508
                                           static_cast<float>(static_cast<double>(Base[1]) +
                                                              MeshOffset),
                                           Base[2]};
            float const MeshHalfExtents[3] = {0.036f, 0.041f, 0.0f};
            float const MeshVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                MeshPosition, nullptr, MeshHalfExtents, 0, 0, 0, 1, 1, 1, 1, 0x50,
                                0, 0, MeshVector338); // FUN_005a0db0
        }

        // 00478675..004786eb: "ItemName0%d".
        sprintf(Name, "ItemName0%d", Number);
        {
            float const NamePosition[3] = {
                Base[0] + 0.256f, // DAT_005d14c4
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.118f) +
                                   static_cast<double>(Base[1])), // DAT_005d14c8
                Base[2]};
            CreateTextControl(Name, "ItemName", NamePosition, nullptr, 0.27f, 0.04f, 1.5f, 0, 0,
                              0, 0); // FUN_005a0e30
        }

        // 004786f0..0047876d: "ItemQuantity0%d". The row y it computes is kept for the "good
        // deal" marker ([ESP+0x4c]).
        sprintf(Name, "ItemQuantity0%d", Number);
        double const QuantityOffset = static_cast<double>(RowY) - static_cast<double>(0.148f); // DAT_005d14b0
        float const QuantityRowY = static_cast<float>(QuantityOffset);
        {
            float const QuantityPosition[3] = {
                Base[0] + 0.171f, // DAT_005d14ac
                static_cast<float>(QuantityOffset + static_cast<double>(Base[1])), Base[2]};
            CreateTextControl(Name, "ItemName", QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0,
                              0, 0xbcf, 0); // FUN_005a0e30
        }

        // 00478772..00478818: "EquipToggle%d" -- mount / unmount toggle (FUN_005a0fb0).
        sprintf(Name, "EquipToggle%d", Number);
        {
            float const TogglePosition[3] = {
                Base[0] + 0.047000002f, // DAT_005d1490
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.11f) +
                                   static_cast<double>(Base[1])), // DAT_005d1498
                Base[2] + 0.001f};                               // DAT_005d1494
            float const ToggleHalfExtents[3] = {0.012f, 0.012f, 0.0f};
            CreateToggleButtonControl(Name, "Interface\\neuronet\\inventory\\unmount.3db",
                                      "Interface\\neuronet\\inventory\\mount.3db", nullptr,
                                      nullptr, TogglePosition, nullptr, ToggleHalfExtents, 0, 0,
                                      0, 0, 0x426, 0x425);
        }

        // 0047881d..004788b6: "ItemDamage%d" -- a horizontal cargo bar (FUN_005a1080) with the
        // empty mesh path DAT_005c865c and the colour DAT_00679bac for both states.
        sprintf(Name, "ItemDamage%d", Number);
        {
            float const DamagePosition[3] = {
                Base[0] + 0.066999994f, // DAT_005d1508
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.158f) +
                                   static_cast<double>(Base[1])), // DAT_005d1428
                Base[2] + 0.001f};                               // DAT_005d1494
            float const DamageHalfExtents[3] = {0.03f, 0.003f, 0.0f};
            CreateCargoBarControl(Name, "", DamagePosition, nullptr, DamageHalfExtents, 1, 0x43c,
                                  G_NnPaletteArgb_679bac, G_NnPaletteArgb_679bac);
        }

        // 004788bb..00478938: "ItemPrice0%d".
        sprintf(Name, "ItemPrice0%d", Number);
        {
            float const PricePosition[3] = {
                PriceX + Base[0],
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.152f) +
                                   static_cast<double>(Base[1])), // DAT_005d1414
                Base[2]};
            CreateTextControl(Name, "ItemName", PricePosition, nullptr, 0.12f, 0.04f, 1.5f, 0xbd4,
                              2, 0xbd4, 0); // FUN_005a0e30
        }

        // 0047893d..00478a02: "ItemGoodDeal0%d" -- only next to a dealer in trade mode 1.
        if (Dealer != nullptr && Dealer->TradeMode == 1)
        {
            sprintf(Name, "ItemGoodDeal0%d", Number);
            float const DealPosition[3] = {Base[0] + 0.365f, QuantityRowY + Base[1], // DAT_005d1400
                                           Base[2] + 0.001f};                        // DAT_005d1494
            float const DealHalfExtents[3] = {0.012f, 0.012f, 0.0f};
            float const DealVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
                                DealPosition, nullptr, DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                0, DealVector338); // FUN_005a0db0
        }

        // 00478a07..00478a7b: "CargoMissionText%d", stored at +0x49c.
        sprintf(Name, "CargoMissionText%d", Number);
        {
            float const MissionPosition[3] = {Base[0] + 0.183f, // DAT_005d13e8
                                              MeshRowY + Base[1], Base[2]};
            CargoMissionText[Row] = CreateTextControl(Name, "ItemName", MissionPosition, nullptr,
                                                      0.17f, 0.04f, 1.5f, 0, 2, 0, 1);
        }
    }

    // 00478a8b..00478ac3: the info card text of the player's own window (FUN_005a10e0).
    if (bOwnWindow != 0)
    {
        float const InfoOffset[3] = {0.027f, 0.151f, 0.0f};
        CreateRichTextWin("InfoCard_Info", 0.36f, 0.31f, InfoOffset, true);
    }

    // 00478ac8..00478b12: "Inventory_Scroll" (FUN_005a17e0).
    {
        float const ScrollPosition[3] = {Base[0] + 0.3989f, Base[1] - 0.099f, // DAT_005d13d0 / 13d4
                                         Base[2]};
        CreateUIScroll("Inventory_Scroll", ScrollPosition, 0.019f, 0.335f, 0, 5, 5, 0);
    }

    // 00478b17..00478cda: the four filter buttons (FUN_005a0c90).
    float const FilterHalfExtents[3] = {0.017f, 0.017f, 0.0f};
    if (bShowFilters != 0)
    {
        float const FilterY = FilterBase[1] - 0.463f; // DAT_005d13b8
        float const WeaponsPosition[3] = {FilterBase[0] + 0.226f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Weapons", "Interface\\NeuroNet\\Inventory\\weaponsicon.3db",
                            nullptr, "ui_filter_select", nullptr, WeaponsPosition, nullptr,
                            FilterHalfExtents, 0, 1, 0, 0, 0x2135, 0, 0);
        float const AmmoPosition[3] = {FilterBase[0] + 0.26f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Ammo", "Interface\\NeuroNet\\Inventory\\ammo.3db", nullptr,
                            "ui_filter_select", nullptr, AmmoPosition, nullptr, FilterHalfExtents,
                            0, 1, 0, 0, 0x214a, 0, 0);
        float const EquipmentPosition[3] = {FilterBase[0] + 0.294f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Equipment",
                            "Interface\\NeuroNet\\Inventory\\equipmenticon.3db", nullptr,
                            "ui_filter_select", nullptr, EquipmentPosition, nullptr,
                            FilterHalfExtents, 0, 1, 0, 0, 0x2142, 0, 0);
        float const CargoBayPosition[3] = {FilterBase[0] + 0.33f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_CargoBay", "Interface\\NeuroNet\\Inventory\\cargobay.3db",
                            nullptr, "ui_filter_select", nullptr, CargoBayPosition, nullptr,
                            FilterHalfExtents, 0, 1, 0, 0, 0x2141, 0, 0);
    }

    // 00478cdf..00478d5d: the commodity filter.
    if (bShowCommodity != 0)
    {
        float const CommodityPosition[3] = {FilterBase[0] + 0.364f, FilterBase[1] - 0.463f,
                                            FilterBase[2]};
        CreateButtonControl("Filter_Commodity", "Interface\\NeuroNet\\Inventory\\commodities.3db",
                            nullptr, "ui_filter_select", nullptr, CommodityPosition, nullptr,
                            FilterHalfExtents, 0, 1, 0, 0, 0x2138, 0, 0);
    }

    // 00478d62..00478e3a: the trade route button of the player's own window.
    if (bOwnWindow != 0)
    {
        float const RoutePosition[3] = {Base[0] + 0.641f, Base[1] - 0.455f, Base[2]};
        float const RouteHalfExtents[3] = {0.146f, 0.026f, 0.0f};
        TradeRouteButton = CreateWireButtonControl("TradeRouteButton", "ui_item_select",
                                                   RoutePosition, RouteHalfExtents, 0, 0, 0x3d4,
                                                   0, 0); // +0x970
        float const RouteTextPosition[3] = {Base[0] + 0.54f, Base[1] - 0.433f, Base[2]};
        TradeRouteText = CreateTextControl("TradeRouteText", "ButtonText", RouteTextPosition,
                                           nullptr, 0.201f, 0.06f, 1.6f, 0x3dc, 1, 0,
                                           1); // +0x96c
    }

    // 00478e40..00479074: the cargo bars and their label, when the player has a ship.
    if (GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        float const BarPosition[3] = {FilterBase[0] + 0.0984f, FilterBase[1] - 0.453f,
                                      FilterBase[2]};
        float const FullHalfExtents[3] = {0.06f, 0.014f, 0.0f};
        float const ZeroVector[3] = {0.0f, 0.0f, 0.0f};
        Field4d0 = CreateStaticControl("Full_Cargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargofull.3db",
                                       BarPosition, nullptr, FullHalfExtents, 0, 0, 0, 1, 0, 0, 1,
                                       0, 0, 0, ZeroVector); // +0x4d0
        Field4d0->ToolTipIds = 0x43b;                        // +0xcc, no null test
        Field4cc = CreateStaticControl("Empty_Cargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargoempty.3db",
                                       BarPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                       0, ZeroVector); // +0x4cc
        Field4cc->SetProperty(1, 1, 0);
        Field4d4 = CreateStaticControl("SelectedCargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargoselected.3db",
                                       BarPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                       0, ZeroVector); // +0x4d4
        Field4d4->SetProperty(1, 1, 0);
        float const LabelPosition[3] = {FilterBase[0] + 0.166f, FilterBase[1] - 0.489f,
                                        FilterBase[2]};
        CargoLabel = CreateTextControl("CargoLabel", "Normal", LabelPosition, nullptr, 0.26f,
                                       0.06f, 1.5f, 0x43b, 0, 0, 0); // +0x4dc
    }

    // 0047907a..0047913c: "ShipView_Control" (FUN_005a1020), hidden (flags bits 0..1 cleared).
    {
        float const ViewPosition[3] = {Base[0] + 0.25f, Base[1] - 0.245f, Base[2]}; // DAT_005c94f8
        float const RotSpeed[3] = {0.0f, 0.0f, 0.0f};
        float const Ypr[3] = {0.0f, 0.0f, 0.0f};
        float const ViewSize[3] = {0.13f, -0.147f, 0.0f};
        unsigned const WireColor = 0xffffffffu; // four 0xff bytes at [ESP+0x20]
        WinControl* const View = CreateShipViewControl(
            "ShipView_Control", RotSpeed, ViewPosition, Ypr, ViewSize, nullptr, false, false,
            reinterpret_cast<unsigned const*>(G_HudChromeColorRgba), &WireColor);
        View->Flags = static_cast<unsigned char>(View->Flags & 0xfc);
    }

    // 00479140..00479218
    if (GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        if (bOwnWindow != 0)
        {
            float const TitlePosition[3] = {0.027f, 0.202f, 0.0f};
            Field944 = CreateTextControl("Infocard_Title", "SubHeader", TitlePosition, nullptr,
                                         0.364f, 0.016f, 1.4f, 0x387, 1, 0, 1); // +0x944
        }
        else if (Dealer != nullptr && Dealer->TradeMode != 1)
        {
            float const InfoPosition[3] = {0.478f, -0.424f, 0.388f};
            float const InfoHalfExtents[3] = {0.012f, 0.012f, 0.0f};
            CreateButtonControl("ShipView_Info", "Interface\\NeuroNet\\Inventory\\question.3db",
                                nullptr, "ui_open_infocard", nullptr, InfoPosition, nullptr,
                                InfoHalfExtents, 0, 1, 0, 0, 0xbc4, 0, 0);
        }
    }

    // 0047921d..00479227: docked (DAT_0066873c, the LocationController's current base) -- no
    // jettison controls.
    if (GetClientLocationCurrentBase() != 0)
        return;

    // 0047922d..0047929b: "JettisonButton" (FUN_005a0c90); the result is not kept here.
    {
        float const JettisonPosition[3] = {Base[0] + 0.42f, Base[1] - 0.076f, Base[2]};
        float const JettisonHalfExtents[3] = {0.014f, 0.014f, 0.0f};
        CreateButtonControl("JettisonButton", "Interface\\NeuroNet\\Inventory\\INV_dump.3db",
                            nullptr, "ui_item_select", nullptr, JettisonPosition, nullptr,
                            JettisonHalfExtents, 0, 1, 0, 0, 0xbc6, 0, 0);
    }

    // 004792a0..004795d5: the jettison quantity panel, +0x91c..+0x938.
    {
        float const TitlePosition[3] = {Base[0] + 0.62f, Base[1] - 0.121f, Base[2]};
        QuantityControl[0] = CreateTextControl("Inv_Jettison_Title", "SubHeader", TitlePosition,
                                               nullptr, 0.26f, 0.016f, 1.4f, 0x638, 1, 0,
                                               0); // +0x91c
    }
    {
        float const SliderPosition[3] = {Base[0] + 0.494f, Base[1] - 0.182f, Base[2]};
        UiScroll* const Slider = CreateUIScroll("JettisonQuantitySlider", SliderPosition, 0.253f,
                                                0.019f, 0, 5, 1, 1);
        QuantityControl[1] = Slider; // +0x920
        Slider->SetStepMode(true);   // +0x3b4 = 1, no null test
        Slider->SetPageField(1.0f);  // +0x3cc = 1.0f
    }
    {
        float const TextPosition[3] = {Base[0] + 0.706f, Base[1] - 0.258f, Base[2]};
        QuantityControl[2] = CreateTextControl("JettisonQuantityText", "Normal", TextPosition,
                                               nullptr, 0.384f, 0.06f, 1.6f, 0x47a, 0, 0,
                                               0); // +0x924
    }
    {
        float const EditPosition[3] = {Base[0] + 0.864f, Base[1] - 0.258f, Base[2]};
        QuantityControl[3] = CreateEditControl("JettisonQuantityEdit", "Normal", EditPosition,
                                               nullptr, 0.384f, 0.06f, 1.6f, 0, 0,
                                               0); // FUN_005a0e90, +0x928
        QuantityControl[3]->SetProperty(0x43, 1, 8);
        QuantityControl[3]->SetProperty(0x44, 1, 0);
    }
    float const JettisonButtonHalfExtents[3] = {0.104f, 0.026f, 0.0f};
    {
        float const CancelPosition[3] = {Base[0] + 0.621f, Base[1] - 0.319f, Base[2]};
        QuantityControl[4] = CreateWireButtonControl("JettisonCancelButton", "ui_item_select",
                                                     CancelPosition, JettisonButtonHalfExtents, 0,
                                                     0, 0x639, 0, 0); // +0x92c
        float const CancelTextPosition[3] = {Base[0] + 0.52f, Base[1] - 0.297f, Base[2]};
        QuantityControl[5] = CreateTextControl("JettisonCancelText", "ButtonText",
                                               CancelTextPosition, nullptr, 0.201f, 0.06f, 1.6f,
                                               0x2148, 1, 0, 1); // +0x930
    }
    {
        float const AcceptPosition[3] = {Base[0] + 0.621f, Base[1] - 0.391f, Base[2]};
        QuantityControl[6] = CreateWireButtonControl("JettisonAcceptButton", "ui_item_select",
                                                     AcceptPosition, JettisonButtonHalfExtents, 0,
                                                     0, 0x63a, 0, 0); // +0x934
        float const AcceptTextPosition[3] = {Base[0] + 0.52f, Base[1] - 0.369f, Base[2]};
        QuantityControl[7] = CreateTextControl("JettisonAcceptText", "ButtonText",
                                               AcceptTextPosition, nullptr, 0.201f, 0.06f, 1.6f,
                                               0x2146, 1, 0, 1); // +0x938
    }
    Field93c = 1;
    Field940 = 100;
    ShowQuantityControls(false); // FUN_004795f0(0)
}

// --- Not ported yet (temporary stubs so the class links; each is `simplified` in the tracker
// with the retail behaviour described there). ---

// The quad block NN_Inventory's Draw runs twice (0x00479a64..0x00479b3e and
// 0x00479bd5..0x00479cb8): the modelview from Bar's orientation (Win3D +0x3c) and its position
// plus offset (+0x60 + +0xa8), texturing off, blending on with (2, 4), colour (0, 0, 0, 0x0a),
// and a quad from x = X to x = -HalfX, y = +-HalfY at z = 0.001.
static void NnInventoryDrawBarQuad(WinControl const* Bar, float X, float HalfX, float HalfY)
{
    float Transform[12];
    for (int Index = 0; Index < 9; ++Index)
        Transform[Index] = Bar->Orientation[Index]; // REP MOVSD, 9 dwords
    Transform[9] = Bar->Offset[0] + Bar->Position[0];
    Transform[10] = Bar->Offset[1] + Bar->Position[1];
    Transform[11] = Bar->Offset[2] + Bar->Position[2];
    Win3DBindModelview(nullptr, Transform); // FUN_00422690(0, &Transform)
    Win3DDisable(kWin3DTexture2D);          // FUN_00421ed0(0xde1)
    Win3DEnable(kWin3DBlend);               // FUN_00421dd0(0xbe2)
    Win3DSetBlendFunc(2, 4);                // FUN_004220c0
    Win3DColor4ub(0, 0, 0, 0x0a);           // FUN_00421b70
    Win3DBegin(kWin3DQuads);                // FUN_00421670(8)
    Win3DVertex3f(X, HalfY, 0.001f);        // FUN_00421cc0
    Win3DVertex3f(X, -HalfY, 0.001f);
    Win3DVertex3f(-HalfX, -HalfY, 0.001f);
    Win3DVertex3f(-HalfX, HalfY, 0.001f);
    Win3DEnd();                             // FUN_00421690
}

void NnInventory::Draw()
{
    // FUN_00479700 (slot 13).
    if (GetGameOption(kGameOptHud) == 0) // FUN_005792b0(10)
        return;
    if (bNeedBind != 0)
        return;

    // 0047972c..00479767: the hovered and the selected row on screen, -1 when off the page.
    int HoverRow = -1;
    if (HoverIndex >= 0)
    {
        HoverRow = HoverIndex - FirstRow;
        if (HoverRow < 0 || HoverRow >= kNnInventoryRows)
            HoverRow = -1;
    }
    int SelectedRow = -1;
    if (SelectedIndex >= 0)
    {
        SelectedRow = SelectedIndex - FirstRow;
        if (SelectedRow < 0 || SelectedRow >= kNnInventoryRows)
            SelectedRow = -1;
    }

    // 0047976a..004797ff: every child's draw layer (+0x70): 1 for the hovered row's button /
    // view / damage bar / equip toggle / mesh, 2 for the selected row's, else 0.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        Child->DrawLayer = 0;
        if (HoverRow >= 0)
        {
            WinControl* const AsControl = Child->AsWinControl(); // slot 2
            if (AsControl == ItemButton[HoverRow] || AsControl == ItemView[HoverRow]
                || AsControl == ItemDamage[HoverRow] || AsControl == EquipToggle[HoverRow]
                || AsControl == ItemMesh[HoverRow])
            {
                Child->DrawLayer = 1;
            }
        }
        if (SelectedRow >= 0)
        {
            WinControl* const AsControl = Child->AsWinControl(); // slot 2
            if (AsControl == ItemButton[SelectedRow] || AsControl == ItemView[SelectedRow]
                || AsControl == ItemDamage[SelectedRow] || AsControl == EquipToggle[SelectedRow]
                || AsControl == ItemMesh[SelectedRow])
            {
                Child->DrawLayer = 2;
            }
        }
    }

    // 00479805..00479826
    WireControlsJoinColumn(ItemButton, kNnInventoryRows); // FUN_005a4b50
    WireControlsJoinColumn(ItemView, kNnInventoryRows);
    WinControl::Draw(); // FUN_005a0760

    // 0047982b..0047984d: the cargo bar needs all three bar controls.
    if (Field4cc == nullptr || Field4d0 == nullptr || Field4d4 == nullptr)
        return;

    // 00479853..0047991f: the volume the dealer's offer (+0x390 on the dealer, times its
    // +0x988 count) adds (+0x910) or, with nothing selected, the volume it would take.
    int OfferVolume = 0; // [ESP+0x1c]
    if (Field910 == 0 && Dealer != nullptr && Dealer->Field390 != 0)
    {
        Archetype::Equipment* const Offer = Archetype::GetEquipment(Dealer->Field390);
        if (Offer != nullptr)
        {
            if (SelectedIndex != -1)
            {
                // ftol of the extended product; the port forms it in double.
                Field910 = static_cast<int>(static_cast<double>(Dealer->Field988) *
                                            static_cast<double>(Offer->Volume));
                if (Mode == 0 || Mode == 2)
                {
                    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
                    if (bDealerMode != 0 || (Item != nullptr && Item->bEquipped))
                        Field910 = 0;
                }
            }
            else
            {
                Field910 = 0;
                if (Field394.Value[0] == '\0')
                {
                    OfferVolume = static_cast<int>(static_cast<double>(Dealer->Field988) *
                                                   static_cast<double>(Offer->Volume));
                }
                if (Mode == 0 || Mode == 2)
                    OfferVolume = 0;
            }
        }
    }

    // 00479923..00479991: the hold (FUN_004c63e0) as fractions of its capacity. The free
    // fraction stays on the x87 stack for the next block (0.0 when the capacity is not
    // positive).
    float Capacity = 0.0f;
    float Used = 0.0f;
    Ship.GetCargoCapacityAndUsed(&Capacity, &Used);
    float Fill = 0.0f;     // [ESP+0x18]
    float Offered = 0.0f;  // [ESP+0x30]
    double Free = 0.0;
    if (Capacity > 0.0f) // TEST AH,0x41 / JNZ: skipped at <= 0 or unordered
    {
        float const Inverse = 1.0f / Capacity;
        Free = (static_cast<double>(Capacity) - static_cast<double>(Used)) *
               static_cast<double>(Inverse);
        Fill = static_cast<float>(static_cast<double>(Field910) * static_cast<double>(Inverse));
        Offered = static_cast<float>(static_cast<double>(OfferVolume) *
                                     static_cast<double>(Inverse));
    }

    // 00479995..004799cd: the full bar's extents; its right end moves left by the free part.
    WinControl* Bar = Field4d0;
    float const HalfX = Bar->HalfExtents[0];
    float const HalfY = Bar->HalfExtents[1];
    float const Width = HalfX + HalfX;
    float Right = static_cast<float>(static_cast<double>(HalfX) -
                                     static_cast<double>(Width) * Free); // [ESP+0x14]
    float Split = 0.0f;                                                  // [ESP+0x18]

    bool bDrawFirst = true;
    if (Field910 > 0)
    {
        // 004799e3..00479a08: at least 0.025 of the bar.
        if (Fill < 0.025f) // DAT_005d15b4; TEST AH,5 / JP keeps it at >= or unordered
            Fill = 0.025f;
        Split = static_cast<float>(static_cast<double>(Right) -
                                   static_cast<double>(Width) * static_cast<double>(Fill));
    }
    else if (OfferVolume <= 0)
    {
        bDrawFirst = false; // 004799d9 -> 00479b97
    }
    else
    {
        // 00479a0e..00479a22
        Split = Right;
        Right = static_cast<float>(static_cast<double>(Width) * static_cast<double>(Offered) +
                                   static_cast<double>(Right));
    }

    IRenderPipeline* const Pipe = DALib::Pipeline;
    if (bDrawFirst)
    {
        // 00479a26..00479b94: the quad from the left end to Split in the full bar's frame, then
        // the selected bar's mesh.
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, 8);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        NnInventoryDrawBarQuad(Bar, Split, HalfX, HalfY);
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        Bar = Field4d4; // ESI now holds the selected bar
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Bar->MeshHolder.EngineInstance, 1.0f, 0, nullptr,
                               nullptr); // IRenderer2 +0x1c, &DAT_0067dbf8
    }

    // 00479b97..00479cf9: the quad from the left end to Right in the frame of whichever bar ESI
    // holds (the selected one when the first quad was drawn, else the full one), then the empty
    // bar's mesh.
    Pipe->SetRenderState(D3DRS_ZENABLE, 1);
    Pipe->SetRenderState(D3DRS_ZFUNC, 8);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    NnInventoryDrawBarQuad(Bar, Right, HalfX, HalfY);
    Pipe->SetRenderState(D3DRS_ZFUNC, 4);
    DALib::Renderer->RenderInstance(Win3DGetCamera(), Field4cc->MeshHolder.EngineInstance, 1.0f, 0,
                           nullptr, nullptr);
}

// The byte several NN_Inventory methods compute before FUN_004c6a50 (e.g. 0x0047e112..0x0047e135,
// 0x0047f945..0x0047f969): true unless the window sits next to a dealer in trade mode 1.
static bool NnInventoryShowFlag(NnDealer const* Dealer)
{
    return Dealer == nullptr || Dealer->TradeMode == 2 || Dealer->TradeMode == 3;
}

intptr_t NnInventory::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_00479d10 (RET 0xc). A switch over 0x13..0x4f (byte table 0x0047acdc); WinControl's
    // slot 42 is never called. Every case that does not return by itself falls into the
    // dealer block after the switch (caseD_14, 0x0047abce), which returns 0.
    switch (Msg)
    {
    case 0x4f:
    {
        // 00479d52: a child asks for its tooltip.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == Field4d0)
        {
            // 00479d65..00479dbf: "<IDS 0x49b>" formatted with the free and total hold.
            float Capacity = 0.0f;
            float Used = 0.0f;
            Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
            GetIdsStringWWithRawFallback(0x49b, ToolTipFormat, 0xff);
            int const Free = static_cast<int>(static_cast<double>(Capacity) - Used); // ftol
            int const Total = static_cast<int>(Capacity);                             // ftol
            swprintf(ToolTipText, 0xff, ToolTipFormat, Free, Total);

            // 00479dc4..00479f7e: the hover record, keyed 0x43b, at the bar's instance.
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, ToolTipText);
            Info.IdsAlt = 0x43b;
            Info.Ids = 0x43b;
            // IEngine slot 40 (+0xa0) -- the instance's 3x4 transform; floats 9..11 are the
            // position.
            float const* const Transform =
                DALib::Engine->GetInstanceTransform(Field4d0->MeshHolder.EngineInstance);
            Info.WorldPos[0] = Transform[9];
            Info.WorldPos[1] = Transform[10];
            Info.WorldPos[2] = Transform[11];
            Info.HalfExtents[0] = Field4d0->HalfExtents[0];
            Info.HalfExtents[1] = Field4d0->HalfExtents[1];
            Info.HalfExtents[2] = Field4d0->HalfExtents[2];
            Info.bLive = 1;
            NnToolTipFeedHover(&Info); // FUN_004be370
            return 1; // ~FToolTipHoverInfo
        }

        // 00479f83..00479f9d: an item button; anything else returns 0 without the dealer block.
        int Row = 0;
        for (; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row])
                break;
        }
        if (Row == kNnInventoryRows)
            return 0;
        WinControl* const Toggle = EquipToggle[Row];
        if (Toggle == nullptr || (Toggle->Flags & 2) == 0)
            return 0;
        if (Control->ToolTipIds == 0)
            return 0;

        // 00479fc9..0047a04c: the button's text followed by "equipped" (0x52a) or "not
        // equipped" (0x52b), after the toggle's state. Retail does not initialise the state
        // local before the toggle's slot 42 0x2f writes it.
        int State = 0;
        Toggle->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
        GetIdsStringWWithRawFallback(static_cast<unsigned>(Control->ToolTipIds), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        GetIdsStringWWithRawFallback(State != 0 ? 0x52a : 0x52b, G_NnTextScratchA,
                                     G_NnTextScratchCapacity);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);

        // 0047a04c..0047a1fb: the hover record, keyed by the button, at the button's wire
        // centre (+0x350 of the 12 dwords copied from +0x32c).
        FToolTipHoverInfo Info;
        ToolTipHoverInit(&Info);
        ToolTipHoverSetCaption(&Info, G_NnTextScratchB);
        Info.IdsAlt = static_cast<int>(reinterpret_cast<intptr_t>(Control));
        Info.Ids = 0;
        WireStaticControl const* const Wire = static_cast<WireStaticControl const*>(ItemButton[Row]);
        Info.WorldPos[0] = Wire->WireCenter[0];
        Info.WorldPos[1] = Wire->WireCenter[1];
        Info.WorldPos[2] = Wire->WireCenter[2];
        Info.HalfExtents[0] = ItemButton[Row]->HalfExtents[0];
        Info.HalfExtents[1] = ItemButton[Row]->HalfExtents[1];
        Info.HalfExtents[2] = ItemButton[Row]->HalfExtents[2];
        Info.bLive = 1;
        NnToolTipFeedHover(&Info); // FUN_004be370
        return 1; // ~FToolTipHoverInfo
    }

    case 0x2c:
    case 0x2d:
    {
        // 0047a200 / 0047a28e: the mouse enters (0x2c) or leaves (0x2d) a control of a row; the
        // row's button and view (other than the sender) get the same message.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        int Row = 0;
        for (; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row] || Control == ItemView[Row] ||
                Control == ItemGoodDeal[Row] || Control == ItemDamage[Row] ||
                Control == EquipToggle[Row])
                break;
        }
        if (Row == kNnInventoryRows)
            break;
        if (Msg == 0x2d)
        {
            if (HoverIndex == FirstRow + Row)
                HoverIndex = -1;
        }
        else
        {
            HoverIndex = FirstRow + Row;
        }
        if (Control != ItemButton[Row])
            ItemButton[Row]->SetProperty(Msg, 0, 0);
        if (Control != ItemView[Row])
            ItemView[Row]->SetProperty(Msg, 0, 0);
        break;
    }

    case 0x15:
        // 0047a314: the ship view picked an instance -- select the row of its hardpoint and
        // clear the dealer's selection.
        SelectHardpoint(Ship.ShipView->GetHardpointOfInstance(static_cast<long>(A))); // FUN_00585e20
        if (Dealer != nullptr)
            Dealer->ClearSelection();
        break;

    case 0x16:
    {
        // 0047a387: an item dropped on hardpoint A (docked only).
        if (!IsPlayerDockedRetail()) // FUN_004c4810
            break;
        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(reinterpret_cast<char const*>(A), false);
        if (Dealer != nullptr && Dealer->GetSelectedItem() != nullptr)
        {
            // 0047a3c6..0047a4cd: the dealer's selected item, mounted straight from the dealer.
            EquipDesc* const Item = Dealer->GetSelectedItem();
            if (Item == nullptr)
                break;
            Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
            int const Filter = NnItemFilterMode(Item->ArchId);
            int Group = 0;
            if (Filter == 0)
                Group = 0;
            else if (Filter == 2)
                Group = 1;
            else
                break;
            if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
                break;
            if (Ship.MountItem(Item, &Hardpoint, 0, true)) // FUN_004c6530
                Dealer->Ship.RemoveItem(Item, 0);          // FUN_004c6430
            ClearSelection();
            Dealer->ClearSelection();
            break;
        }

        // 0047a4d2..0047a5ce: this window's selected item moves to the hardpoint.
        if (GetSelectedItem() == nullptr)
            break;
        EquipDesc* const Item = GetSelectedItem();
        if (Item == nullptr)
            break;
        Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
        int const Filter = NnItemFilterMode(Item->ArchId);
        int Group = 0;
        if (Filter == 0)
            Group = 0;
        else if (Filter == 2)
            Group = 1;
        else
            break;
        if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
            break;
        Ship.SetItemHardpoint(Item, Hardpoint.Value); // FUN_004c6910
        if (!(Dealer != nullptr && Dealer->TradeMode == 2) && IsPlayerDockedRetail())
        {
            ReqPlayerEquipmentRetail(&Ship.Loadout); // FUN_004c4040
            // 0047a56f..0047a584: FUN_00448540(&Ship.Loadout) and FUN_00447210() on the GF
            // ObjectShip singleton (FUN_00446be0) when it exists.
            FObjectShip* const GfShip = GfObjectShip();
            if (GfShip != nullptr)
            {
                GfShip->SetEquipment(&Ship.Loadout);
                GfShip->RebuildModel();
            }
        }
        ClearSelection();
        break;
    }

    case 0x13:
        // 0047a5d3: the ship dealer shows ship package A.
        if (Dealer != nullptr)
            Dealer->ShowShipPackage(static_cast<unsigned>(A)); // FUN_00483d40
        break;

    case 0x47:
        // 0047a5f3: the dealer's trade mode.
        if (Dealer != nullptr)
        {
            Dealer->TradeMode = static_cast<int>(A);
            Dealer->Field980 = 1;
        }
        break;

    case 0x48:
    {
        // 0047a623: open the window: A / B are the result callback's argument and function.
        int const TradeMode = (Dealer != nullptr) ? Dealer->TradeMode : 0;
        if (Dealer != nullptr)
        {
            Field4e4 = 1;
            Dealer->TradeMode = TradeMode;
            Dealer->Slot23(0xef01, 0, 0);
        }
        Result4e5 = 1;
        ResultArg = reinterpret_cast<void*>(A);
        ResultFn = reinterpret_cast<FNnInventoryResultFn>(B);
        if (TradeMode == 1)
            SetMode(4, true);
        else if (TradeMode == 2 || TradeMode == 3)
            SetMode(0, true);

        // 0047a697..0047a76a: +0x3a8 = the player's list (cleared first, then assigned unless
        // it is the same list).
        SavedLoadout.Items.clear();
        EquipDescList& PlayerList = GetPlayerEquipDescListRetail(); // FUN_004c4060
        if (&SavedLoadout != &PlayerList)
            SavedLoadout = PlayerList;

        // 0047a770..0047a78b
        Ship.ShipArchId = GetPlayerShipArchetypeIdRetail(); // FUN_004c3e10
        Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);   // FUN_004c62b0
        // 0047a790..0047a79b: FUN_0053e4a0(+0x4e2) on +0x330 -- `RET 4`, nothing.

        // 0047a7a0..0047a7f8: the dealer's +0x364..+0x37c.
        if (Dealer != nullptr)
        {
            TransferItemButton = Dealer->TransferItemButton;
            TransferItemView = Dealer->TransferItemView;
            TransferItemMesh = Dealer->TransferItemMesh;
            TransferItemName = Dealer->TransferItemName;
            TransferItemDamage = Dealer->TransferItemDamage;
            TransferItemPrice = Dealer->TransferItemPrice;
            TransferItemGoodDeal = Dealer->TransferItemGoodDeal;
        }
        RefreshItemList(&Ship.Loadout, 0, 1); // FUN_0047d2f0
        if (GetParent() != nullptr)
            HudPostCommand(0x66, 0); // FUN_005763b0
        break;
    }

    case 0x45:
    {
        // 0047a82c: the quantity edit's text changed.
        wchar_t const* const Text = reinterpret_cast<wchar_t const*>(A);
        if (Text == nullptr)
        {
            QuantityControl[1]->SetProperty(0x38, 1, 0);
            break;
        }
        int Value = 0;
        swscanf(Text, L"%d", &Value);
        if (Value < 1)
            Field93c = 1;
        else if (Value > Field940)
            Field93c = Field940;
        else
            Field93c = Value;
        swprintf(ToolTipText, 0xff, L"%d", Field93c); // PTR_DAT_005d0ec4
        if (_wcsicmp(Text, ToolTipText) != 0)
            QuantityControl[3]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        QuantityControl[1]->SetProperty(0x38, static_cast<uintptr_t>(Field93c), 0);
        break;
    }

    case 0x3e:
        // 0047a8f1: a slider moved -- the list scroll or the quantity slider.
        if (reinterpret_cast<WinControl*>(A) == InventoryScroll)
        {
            FirstRow = static_cast<int>(B);
            break;
        }
        if (reinterpret_cast<WinControl*>(A) == QuantityControl[1])
        {
            Field93c = static_cast<int>(B);
            if (Field93c < 0)
                Field93c = 0;
            swprintf(ToolTipText, 0xff, L"%d", Field93c);
            QuantityControl[3]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        }
        break;

    case 0x1a:
    case 0x26:
    {
        // 0047a96d: a click.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == CloseButton)
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            return 1;
        }
        if (Control == TradeRouteButton)
        {
            // 0047a993..0047a9d2: the trade routes of the selected commodity.
            if (Mode == 4 && SelectedIndex != -1)
            {
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr)
                {
                    unsigned const ArchId = Item->ArchId;
                    NnMainButtonOnModalOpen(); // FUN_0045a460
                    WinFactoryCreate("NN_CommodityByBase", "NN_CommodityByBase", nullptr,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(ArchId)));
                }
            }
            return 1;
        }
        if (Control == QuantityControl[4])
        {
            ShowQuantityControls(false);
            return 1;
        }
        if (Control == QuantityControl[6])
        {
            // 0047a9fa..0047aa6b: jettison the chosen quantity. The item is not null-tested.
            ShowQuantityControls(false);
            if (SelectedIndex != -1 && Field4e3 == 0)
            {
                EquipDesc* const Item = GetSelectedItem();
                if (!Item->bEquipped)
                {
                    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
                    if (Player != nullptr)
                    {
                        static_cast<IObjAffect*>(Player)->slot_38(Item->Id, Field93c,
                                                                  static_cast<int>(GetPlayerConnectionId()));
                        Field914 = 1.0f;
                        // 0047aa56..0047aa63: FUN_00489e80(G_NnVoiceCargoJettisonedId, 0).
                        NnVoiceEnqueue(G_NnVoiceCargoJettisonedId, 0);
                    }
                }
            }
            return 1;
        }

        // 0047aa70..0047ab35: a control of a row.
        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row] || Control == ItemView[Row] ||
                Control == ItemGoodDeal[Row])
            {
                SelectRow(Row, 1);
                Field4e0 = 0;
                if (Dealer != nullptr)
                    Dealer->ClearSelection();
                break;
            }
            if (Control == EquipToggle[Row])
            {
                SelectRow(Row, 1);
                int State = 0; // retail: uninitialised until slot 42 0x2f writes it
                EquipToggle[Row]->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr)
                    ToggleEquip(Item, State); // FUN_0047f720
                break;
            }
        }

        // 0047ab3b..0047abc7: the filter buttons, the jettison button, the ship view's info.
        if (Control == FilterCargoBay)
            SetMode(3, true);
        else if (Control == FilterWeapons)
            SetMode(0, true);
        else if (Control == FilterEquipment)
            SetMode(2, true);
        else if (Control == FilterCommodity)
            SetMode(4, true);
        else if (Control == FilterAmmo)
            SetMode(1, true);
        else if (Control == JettisonButton)
            JettisonSelected(); // FUN_0047e650
        else if (Control == Ship.InfoText)
        {
            SelectRow(-1, 1);
            Field4e0 = 1;
        }
        break;
    }

    default:
        break;
    }

    // 0047abce..0047ac8a: a ship dealer (trade mode 2) with a ship view shows this window's
    // selected item on its own ship too.
    if (Dealer != nullptr && Dealer->TradeMode == 2 && Dealer->Ship.ShipView != nullptr &&
        GetSelectedItem() != nullptr)
    {
        EquipDesc* const Item = GetSelectedItem();
        Dealer->ClearSelection();
        Dealer->Ship.ShipView->SelectEquipDesc(Item, true); // FUN_00585d20
        Dealer->Ship.ShipView->SelectInstance(Ship.ShipView->SelInstance, false,
                                              static_cast<int>(Item->ArchId)); // FUN_00585bd0
    }
    return 0;
}

void NnInventory::SelectRow(int Row, int Unused)
{
    // FUN_0047e0e0 (RET 8).
    (void)Unused;
    ShowQuantityControls(false);
    if (Row == -1)
    {
        FirstRow = 0;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(0x38, 0, 0);
    }
    bool const bFlag = NnInventoryShowFlag(Dealer);
    SelectedItem = nullptr;
    SelectedIndex = FirstRow + Row;

    if (Dealer != nullptr)
    {
        // 0047e14d..0047e1c5: the dealer's quantity controls follow the row's count.
        int Count = 1;
        EquipDesc* const Selected = GetSelectedItem();
        if (Selected != nullptr)
            Count = Selected->Count;
        NnDealer* const Partner = Dealer;
        if (Count > 1)
            Partner->QuantitySlider->SetProperty(0x37, 1, static_cast<uintptr_t>(Count));
        Partner->Field988 = Count;
        Partner->QuantitySlider->SetProperty(0x38, static_cast<uintptr_t>(Count), 0);
        swprintf(Partner->ToolTipText, 0xff, L"%d", Partner->Field988); // PTR_DAT_005d0ec4
        Partner->QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Partner->ToolTipText), 0);
    }

    // 0047e1c6..0047e1f3
    EquipDesc* const Item = GetSelectedItem();
    Ship.ShowItem(Item, bFlag, Item != nullptr ? NnItemFilterMode(Item->ArchId) : 5);

    // 0047e1f8..0047e25d: the dealer's selection is cleared, then its +0x4e8 / +0x4ec again.
    if (Dealer != nullptr)
    {
        Dealer->ClearSelection();
        Dealer->Field4e8 = 0;
        Dealer->Field4ec = 0;
    }
}

void NnInventory::SelectHardpoint(char const* Name)
{
    // FUN_0047e270 (RET 4).
    if (Name == nullptr)
        return;
    if (Dealer != nullptr && Dealer->TradeMode == 1)
        return;

    // 0047e29e..0047e31a: the first row already holds it.
    SelectedIndex = 0;
    SelectedItem = nullptr;
    ShowQuantityControls(false);
    EquipDesc* const First = GetSelectedItem();
    if (First != nullptr && _stricmp(First->Hardpoint, Name) == 0) // DACOM stricmp
    {
        Ship.ShowItem(First, NnInventoryShowFlag(Dealer), NnItemFilterMode(First->ArchId));
        return;
    }

    // 0047e31d..0047e38a: remember the selection, switch to the hardpoint type's filter.
    int const SavedFirstRow = FirstRow;
    int const SavedIndex = SelectedIndex;
    EquipDesc* const SavedItem = First;
    int const SavedMode = Mode;
    Ship.ShipView->CollectHpTypes(Name); // FUN_00586560
    if (Ship.ShipView->HasHpTypeGroup(0)) // FUN_00586800
        SetMode(0, true);
    else if (Ship.ShipView->HasHpTypeGroup(1))
        SetMode(2, true);
    else if (Ship.ShipView->HasHpType(1)) // FUN_00586510
        SetMode(3, true);

    // 0047e38f..0047e3e0: walk the list page by page until a row's item is on Name; stop after
    // a page whose last row is empty.
    SelectRow(-1, 1);
    FirstRow = 0;
    EquipDesc* Found = nullptr;
    do
    {
        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            SelectRow(Row, 1);
            Found = GetSelectedItem();
            if (Found != nullptr && _stricmp(Found->Hardpoint, Name) == 0)
            {
                // 0047e414..0047e438
                if (InventoryScroll != nullptr)
                    InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow), 0);
                return;
            }
        }
        ++FirstRow;
    } while (Found != nullptr);

    // 0047e3e2..0047e404
    FirstRow = SavedFirstRow;
    SelectedIndex = SavedIndex;
    SelectedItem = SavedItem;
    Mode = SavedMode;
}

void NnInventory::JettisonSelected()
{
    // FUN_0047e650. The selected item is not null-tested.
    if (SelectedIndex == -1)
        return;
    if (Field4e3 != 0)
        return;
    EquipDesc* Item = GetSelectedItem();
    if (Item->bEquipped)
        return;
    Field93c = 1;
    Field940 = Item->Count;
    if (Field940 != 1)
    {
        // 0047e719..0047e77b: ask for the quantity.
        ShowQuantityControls(true);
        QuantityControl[1]->SetProperty(0x37, 1, static_cast<uintptr_t>(Field940));
        QuantityControl[1]->SetProperty(0x38, static_cast<uintptr_t>(Field93c), 0);
        swprintf(ToolTipText, 0xff, L"%d", Field93c); // PTR_DAT_005d0ec4
        QuantityControl[3]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        return;
    }

    // 0047e69d..0047e713: a single item goes at once (the conditions are tested again).
    ShowQuantityControls(false);
    if (SelectedIndex == -1)
        return;
    if (Field4e3 != 0)
        return;
    Item = GetSelectedItem();
    if (Item->bEquipped)
        return;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
        return;
    static_cast<IObjAffect*>(Player)->slot_38(Item->Id, Field93c,
                                              static_cast<int>(GetPlayerConnectionId()));
    Field914 = 1.0f;
    // 0047e706..0047e70e: FUN_00489e80(G_NnVoiceCargoJettisonedId, 0).
    NnVoiceEnqueue(G_NnVoiceCargoJettisonedId, 0);
}

void NnInventory::ToggleEquip(EquipDesc* Item, int State)
{
    // FUN_0047f720 (RET 8).
    int Filter = 0; // EDI -- only read on the hardpoint path
    bool bHardpointItem = false;
    if (Item != nullptr)
    {
        Filter = NnItemFilterMode(Item->ArchId);
        bHardpointItem = (Filter == 0 || Filter == 2);
    }

    if (!bHardpointItem)
    {
        // 0047f750..0047f875: an item without a hardpoint (Item is not null-tested here). In a
        // dealer's trade mode 3 every change goes through the server (FUN_004c4540).
        bool const bThroughServer = Dealer != nullptr && Dealer->TradeMode == 3;
        if (State != 0)
        {
            if (bThroughServer)
            {
                ReqModifyPlayerEquipRetail(Item->Id, &Item->get_hardpoint(), Item->Count,
                                           Item->Health, false);
                return;
            }
            Item->set_equipped(false);
            return;
        }

        // 0047f75c..0047f7fa: equip Item alone -- every other item of its class comes off.
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr)
        {
            Archetype::AClassType const Class = Arch->get_class_type();
            for (std::list<EquipDesc>::iterator It = Ship.Loadout.Items.begin();
                 It != Ship.Loadout.Items.end(); ++It)
            {
                EquipDesc* const Other = &*It;
                if (Other == Item)
                    continue;
                Archetype::Equipment* const OtherArch = Archetype::GetEquipment(Other->ArchId);
                if (OtherArch == nullptr)
                    continue;
                if (OtherArch->get_class_type() != Class)
                    continue;
                if (Dealer != nullptr && Dealer->TradeMode == 3)
                    ReqModifyPlayerEquipRetail(Other->Id, &Other->get_hardpoint(), Other->Count,
                                               Other->Health, false);
                else
                    Other->set_equipped(false);
            }
        }
        if (Dealer != nullptr && Dealer->TradeMode == 3)
        {
            ReqModifyPlayerEquipRetail(Item->Id, &Item->get_hardpoint(), Item->Count, Item->Health,
                                       true);
            return;
        }
        Item->set_equipped(true);
        return;
    }

    // 0047f878: weapons and equipment.
    Field948 = Item->Id;
    if (State != 0)
    {
        // 0047f88f..0047f914: into the cargo bay, when the hold has room for it.
        float Volume = 0.0f;
        if (Item->ArchId != 0)
        {
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
            if (Arch != nullptr)
                Volume = Arch->Volume; // +0x5c
        }
        int Count = Item->Count;
        if (Count < 1)
            Count = 1;
        float Capacity = 0.0f;
        float Used = 0.0f;
        Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
        // FILD / FMUL / ftol and FLD / FSUB / ftol, compared unsigned (JBE).
        unsigned const Need =
            static_cast<unsigned>(static_cast<long long>(static_cast<double>(Count) * Volume));
        unsigned const Free = static_cast<unsigned>(
            static_cast<long long>(static_cast<double>(Capacity) - Used));
        if (Need > Free)
        {
            // 0047f8fc..0047f914: FUN_00489e80(G_NnVoiceInsufficientCargoSpaceId, 0).
            NnVoiceEnqueue(G_NnVoiceInsufficientCargoSpaceId, 0);
            return;
        }

        // 0047f917..0047f984
        PlayOrRewindUiSoundSlot(0x11); // FUN_005645c0
        if (Filter == 0)
            G_WeaponGroups.RemoveHardpoint(Item->Hardpoint); // FUN_004f6380
        Item->make_internal();
        Item->set_equipped(false);
        Ship.ShowItem(Item, NnInventoryShowFlag(Dealer), NnItemFilterMode(Item->ArchId));

        // 0047f989..0047f9da: no dealer -- a dealer window itself refreshes in trade mode 2,
        // the player's own window syncs the server.
        if (Dealer == nullptr)
        {
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != 2)
                    return;
                Ship.RefreshView(); // FUN_004c6e70
                return;
            }
        }
        else if (Dealer->TradeMode != 3)
        {
            Ship.RefreshView(); // FUN_004c6e70
            return;
        }
    }
    else
    {
        // 0047f9dd..0047fafe: onto the first free hardpoint that takes it.
        char const* const Hardpoint = Ship.ShipView->FindHardpointForEquipDesc(Item, false); // FUN_00585ef0
        if (Hardpoint == nullptr)
        {
            // 0047f9f9..0047fa0f: FUN_00489e80(G_NnVoiceNoPlaceToMountId, 0).
            NnVoiceEnqueue(G_NnVoiceNoPlaceToMountId, 0);
            return;
        }
        CacheString NewHardpoint;
        NewHardpoint.Value = StringAlloc(Hardpoint, false);
        Item->set_hardpoint(NewHardpoint);
        Item->set_equipped(true);
        // 0047fa37..0047fa3f: FUN_00489e80(G_NnVoiceMountedId, 0).
        NnVoiceEnqueue(G_NnVoiceMountedId, 0);

        // 0047fa44..0047fa54: FUN_004c6860 is handed the GOOD id of the archetype.
        FInventoryShipModel::PlayMountSound(Arch2Good(Item->ArchId));

        // 0047fa59..0047faad: a gun whose projectile needs no ammo joins weapon group 0.
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr && Arch->get_class_type() == Archetype::AClassGun)
        {
            ID_String const& ProjectileName = static_cast<Archetype::Launcher*>(Arch)->ProjectileArchetype;
            if (!ProjectileName.IsEmpty())
            {
                Archetype::Equipment* const AmmoArch = Archetype::GetEquipmentByName(ProjectileName);
                if (AmmoArch != nullptr &&
                    !static_cast<Archetype::Projectile*>(AmmoArch)->bRequiresAmmo) // +0x78
                    G_WeaponGroups.AddHardpoint(0, Hardpoint); // FUN_004f6200
            }
        }

        // 0047fab2..0047fafe
        Ship.ShowItem(Item, NnInventoryShowFlag(Dealer), NnItemFilterMode(Item->ArchId));
        if (Dealer != nullptr && Dealer->TradeMode != 3)
        {
            Ship.RefreshView(); // FUN_004c6e70
            return;
        }
    }

    // 0047fb04..0047fb31: docked -- send the loadout to the server.
    if (IsPlayerDockedRetail()) // FUN_004c4810
    {
        ReqPlayerEquipmentRetail(&Ship.Loadout); // FUN_004c4040
        // 0047fb1c..0047fb31: FUN_00448540(&Ship.Loadout) and FUN_00447210() on the GF
        // ObjectShip singleton (FUN_00446be0) when it exists.
        FObjectShip* const GfShip = GfObjectShip();
        if (GfShip != nullptr)
        {
            GfShip->SetEquipment(&Ship.Loadout);
            GfShip->RebuildModel();
        }
    }
}

// PTR_s_Interface_NeuroNet_Inventory_INV_green_circle_00611a4c: the good-deal icons FUN_0047c740
// indexes with the market verdict (0..2 selling to the dealer, 3..5 buying from it).
static char const* const G_NnGoodDealIcons[6] = {
    "Interface\\NeuroNet\\Inventory\\INV_green_circle.3db",   // 005d0c7c
    "Interface\\NeuroNet\\Inventory\\INV_red_circle.3db",     // 005d0c4c
    "Interface\\NeuroNet\\Inventory\\INV_yellow_circle.3db",  // 005d0c18
    "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",   // 005d0be4
    "Interface\\NeuroNet\\Inventory\\INV_red_square.3db",     // 005d0bb4
    "Interface\\NeuroNet\\Inventory\\INV_yellow_square.3db",  // 005d0b80
};

// 0x0047c939..0x0047c950 with the byte table 0x0047d2bc: the class number (1..10) a hardpoint
// type carries in the item name -- hp_gun_special_1..10 (0xf..0x18), hp_turret_special_1..10
// (0x19..0x22) and the three shield-special runs (0x25..0x42); 0 for every other type (the
// table's entry 10, which also covers 0x23 / 0x24, and the JA default).
static int NnHpTypeClassNumber(int HpType)
{
    unsigned const Index = static_cast<unsigned>(HpType - 0xf);
    if (Index > 0x33)
        return 0;
    if (HpType == 0x23 || HpType == 0x24)
        return 0;
    if (HpType <= 0x22)
        return (HpType - 0xf) % 10 + 1;
    return (HpType - 0x25) % 10 + 1;
}

bool NnInventory::FillItemRow(int* Counter, unsigned ArchId, int FilterMode, int Count, int Price,
                              int Health, unsigned char bShowToggle, unsigned char bEquipped,
                              unsigned char bMission, unsigned char bUsable, int EmptyIds,
                              int HpClass, unsigned char bAlwaysShowCount)
{
    // FUN_0047c740 (RET 0x34).
    (void)HpClass; // the twelfth argument is never read
    if (ArchId == 0 && EmptyIds == 0)
        return false;

    // 0047c765..0047c7a4: the dealer's transfer panel mirrors the selected entry when all of
    // it exists.
    unsigned char bTransfer = 0;
    if ((bDealerMode != 0 || Dealer != nullptr) && TransferItemMesh != nullptr &&
        TransferItemName != nullptr && TransferItemDamage != nullptr &&
        TransferItemPrice != nullptr && TransferItemGoodDeal != nullptr)
        bTransfer = 1;

    // 0047c7a9..0047c7bc
    if (FilterMode != Mode && FilterMode != -1)
        return false;

    // 0047c7c2..0047c851: the selected entry records itself on the dealer (or on this window).
    unsigned char const bSelected = (*Counter == SelectedIndex) ? 1 : 0;
    if (bSelected == 0)
        bTransfer = 0;
    if (ArchId == 0)
    {
        bTransfer = 0;
        if (bSelected != 0)
        {
            if (Dealer == nullptr)
                Field390 = 0;
            else
                Dealer->Field390 = 0;
        }
    }
    else if (bTransfer != 0)
    {
        if (Dealer == nullptr)
        {
            Field38c = Count;
            Field390 = ArchId;
            Field388 = Price;
        }
        else
        {
            Dealer->Field390 = ArchId;
            Dealer->Field38c = Count;
            Dealer->Field388 = Price;
        }
    }

    // 0047c855..0047c87e
    GoodInfo const* Good = nullptr;
    if (ArchId != 0)
        Good = GoodList::find_by_archetype(ArchId);
    if (Good == nullptr && EmptyIds == 0)
        return false;

    // 0047c881..0047c88d: entries off the page are only counted.
    unsigned const Row = static_cast<unsigned>(*Counter - FirstRow);
    if (Row < kNnInventoryRows)
    {
        // 0047c893..0047cae6: the name -- in modes 0 / 2 a special hardpoint type adds its class
        // number ("<name> <IDS 0x3df><n><IDS 0x3e0>").
        WinControl* const NameText = ItemName[Row];
        if (NameText != nullptr)
        {
            if (ArchId == 0)
            {
                if (EmptyIds != 0)
                    NameText->SetProperty(0x1e, 0x38a, 0);
            }
            else
            {
                Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
                if (Arch == nullptr)
                {
                    NameText->SetProperty(0x1e, 0, 0);
                }
                else
                {
                    int ClassNumber = 0;
                    if (Mode == 0 || Mode == 2)
                    {
                        int HpType = static_cast<int>(Arch->get_hp_type()); // vt+0x18
                        if (Arch->get_class_type() == Archetype::AClassGun)
                        {
                            Archetype::Gun const* const Gun = static_cast<Archetype::Gun const*>(Arch);
                            int const TypeCount = Gun->get_number_of_hp_types();
                            for (int Index = 0; Index < TypeCount; ++Index)
                            {
                                if (static_cast<int>(Gun->get_hp_type_by_index(Index)) > HpType)
                                    HpType = static_cast<int>(Gun->get_hp_type_by_index(Index));
                            }
                        }
                        ClassNumber = NnHpTypeClassNumber(HpType);
                    }
                    if (ClassNumber != 0)
                    {
                        // 0047c95c..0047ca2c
                        GetIdsStringWWithRawFallback(static_cast<unsigned>(Arch->IdsName), G_NnTextScratchB,
                                                     G_NnTextScratchCapacity);
                        GetIdsStringWWithRawFallback(0x3df, G_NnTextScratchA, G_NnTextScratchCapacity);
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        swprintf(G_NnTextScratchA, G_NnTextScratchCapacity, L"%d", ClassNumber); // PTR_DAT_005d0ec4
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        GetIdsStringWWithRawFallback(0x3e0, G_NnTextScratchA, G_NnTextScratchCapacity);
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        NameText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
                        if (bTransfer != 0)
                            TransferItemName->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
                    }
                    else
                    {
                        // 0047ca8b..0047cac7
                        NameText->SetProperty(0x1e, static_cast<uintptr_t>(Arch->IdsName), 0);
                        if (bTransfer != 0)
                            TransferItemName->SetProperty(0x1e, static_cast<uintptr_t>(Arch->IdsName), 0);
                    }
                }
            }
            NameText->SetProperty(0xc, bSelected, 0);
        }

        // 0047caea..0047cc39: the quantity. A dealer outside trade mode 2, or a ship dealer's
        // non-combinable good, shows no count unless bAlwaysShowCount.
        WinControl* const QuantityText = ItemQuantity[Row];
        if (QuantityText != nullptr)
        {
            int Shown = Count;
            bool bForceZero = false;
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != 2)
                    bForceZero = true;
                else if (Good != nullptr && !Good->bCombinable) // +0x70
                    bForceZero = true;
            }
            bool bShowCount = false;
            if (bForceZero)
            {
                Shown = 0;
                bShowCount = bAlwaysShowCount != 0;
            }
            else
            {
                bShowCount = Shown > 0 || bAlwaysShowCount != 0;
            }
            if (bShowCount)
            {
                swprintf(ToolTipText, 0xff, L"%d", Shown); // PTR_DAT_005d0ec4
                QuantityText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                QuantityText->SetProperty(0xc, bSelected, 0);
                QuantityText->SetProperty(1, 0, 0);
            }
            else
            {
                QuantityText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(L""), 0); // DAT_005c8ce0
            }
            if (Mode == 3)
            {
                // 0047cb92..0047cbce: nanobots, shield batteries and every dealer row hide it.
                unsigned char bOn = 1;
                if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId || bDealerMode != 0)
                    bOn = 0;
                QuantityText->SetProperty(1, bOn, 0);
            }
            if (EmptyIds != 0)
            {
                QuantityText->SetProperty(0x1e, static_cast<uintptr_t>(EmptyIds), 0);
                QuantityText->SetProperty(1, 0, 0);
                QuantityText->SetProperty(0xc, bSelected, 0);
            }
            else if (bDealerMode == 0 && (Mode == 0 || Mode == 2))
            {
                QuantityText->SetProperty(1, 1, 0);
            }
        }

        // 0047cc3f..0047cd75: the price (none for a mission item) and the good-deal marker's
        // visibility.
        WinControl* const PriceText = ItemPrice[Row];
        if (PriceText != nullptr)
        {
            if (Price > 0 && bMission == 0)
            {
                FormatGroupedNumber(ToolTipText, Price, -1); // FUN_004779a0
                PriceText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                PriceText->SetProperty(0xc, bSelected, 0);
                if (bTransfer != 0)
                {
                    TransferItemPrice->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                    TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags | 3);
                    bool bShowDeal = true;
                    if (bDealerMode != 0)
                        bShowDeal = static_cast<NnDealer*>(this)->TradeMode == 1;
                    else if (Dealer != nullptr)
                        bShowDeal = Dealer->TradeMode == 1;
                    if (bShowDeal)
                        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags | 3);
                    else
                        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags & 0xfc);
                }
                if (ItemGoodDeal[Row] != nullptr)
                    ItemGoodDeal[Row]->SetProperty(2, 0, 0);
            }
            else
            {
                PriceText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(L""), 0); // DAT_005c8ce0
                if (ItemGoodDeal[Row] != nullptr)
                    ItemGoodDeal[Row]->SetProperty(2, 1, 0);
                if (bTransfer != 0)
                {
                    TransferItemPrice->SetProperty(0x1d, reinterpret_cast<uintptr_t>(L""), 0);
                    TransferItemGoodDeal->SetProperty(2, 1, 0);
                }
            }
        }

        // 0047cd7b..0047cda6: the mission marker text.
        if (CargoMissionText[Row] != nullptr)
        {
            if (bMission != 0)
                CargoMissionText[Row]->SetProperty(0x1e, 0x576, 0);
            else
                CargoMissionText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(L""), 0);
        }

        // 0047cdac..0047ce4c: the damage bar, green from 70, yellow from 30, red below.
        if (ItemDamage[Row] != nullptr)
        {
            ItemDamage[Row]->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
            unsigned Colour = 0xff2626ffu; // bytes ff 26 26 ff
            if (Health >= 70)
                Colour = 0xff30c24bu;      // bytes 4b c2 30 ff
            else if (Health >= 30)
                Colour = 0xff26eaf5u;      // bytes f5 ea 26 ff
            ItemDamage[Row]->SetProperty(0x3f, Colour, 0);
            if (bTransfer != 0)
            {
                TransferItemDamage->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
                TransferItemDamage->SetProperty(0x3f, Colour, 0);
            }
        }

        // 0047ce52..0047cef9: the equip toggle -- never for nanobots / shield batteries; in modes
        // 0 / 2 an unequipped good needs a hardpoint of the ship that takes it.
        if (EquipToggle[Row] != nullptr)
        {
            unsigned char bToggle = bShowToggle;
            if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId)
            {
                bToggle = 0;
            }
            else if (bEquipped == 0 && (Mode == 0 || Mode == 2))
            {
                if (Good == nullptr)
                    bToggle = 0;
                else if (Ship.ShipView->FindHardpointForGood(Good->Id, false) == nullptr) // FUN_00585f20
                    bToggle = 0;
            }
            EquipToggle[Row]->SetProperty(1, bToggle == 0 ? 1 : 0, 0);
            if (bToggle != 0)
                EquipToggle[Row]->SetProperty(0x30, bEquipped != 0 ? 0 : 1, 0);
        }

        // 0047ceff..0047d03a: the good-deal marker. Out of a base (DAT_0066873c, JBE) icon 0 and
        // IDS 0. Docked: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested) and the
        // market price P of the good.
        if (ItemGoodDeal[Row] != nullptr && Good != nullptr)
        {
            int IconIndex = 0; // EBX
            unsigned Ids = 0;  // EBP
            unsigned const BaseId = GetClientLocationCurrentBase();
            if (BaseId != 0)
            {
                FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
                if (Dealer != nullptr)
                {
                    // 0047cf42..0047cf86: a dealer's list -- P = FUN_00438060(Good->Id) against
                    // good_sell_price (+0x64: FCOM, TEST AH,1 + JNE: not less -> 0x41a, icon 0)
                    // and bad_sell_price (+0x68: FCOMP, TEST AH,0x41 + JNE: less / equal /
                    // unordered -> 0x41b, icon 1; greater -> 0x41c, icon 2).
                    double const MarketPrice =
                        static_cast<double>(ClientMarketGetPrice(Market, Good->Id));
                    if (MarketPrice >= static_cast<double>(Good->GoodSellPrice)) // unordered: false
                    {
                        IconIndex = 0;
                        Ids = 0x41a;
                    }
                    else if (MarketPrice > static_cast<double>(Good->BadSellPrice))
                    {
                        IconIndex = 2;
                        Ids = 0x41c;
                    }
                    else
                    {
                        IconIndex = 1;
                        Ids = 0x41b;
                    }
                }
                else
                {
                    // 0047cf88..0047cfd7: the player's own list -- P = FUN_00438060(Arch2Good(
                    // ArchId)) against good_buy_price (+0x5c: FCOMP, TEST AH,0x41 + JP: less or
                    // equal -> 0x417, icon 3) and bad_buy_price (+0x60: FCOMP, TEST AH,5 + JP:
                    // less -> 0x419, icon 5; greater / equal / unordered -> 0x418, icon 4).
                    double const MarketPrice =
                        static_cast<double>(ClientMarketGetPrice(Market, Arch2Good(ArchId)));
                    if (MarketPrice <= static_cast<double>(Good->GoodBuyPrice))
                    {
                        IconIndex = 3;
                        Ids = 0x417;
                    }
                    else if (MarketPrice < static_cast<double>(Good->BadBuyPrice))
                    {
                        IconIndex = 5;
                        Ids = 0x419;
                    }
                    else
                    {
                        IconIndex = 4;
                        Ids = 0x418;
                    }
                }
            }
            ItemGoodDeal[Row]->SetProperty(4, reinterpret_cast<uintptr_t>(G_NnGoodDealIcons[IconIndex]), 0);
            ItemGoodDeal[Row]->SetProperty(3, Ids, 0);
            if (bTransfer != 0)
            {
                TransferItemGoodDeal->SetProperty(3, Ids, 0);
                TransferItemGoodDeal->SetProperty(4, reinterpret_cast<uintptr_t>(G_NnGoodDealIcons[IconIndex]), 0);
            }
        }

        // 0047d040..0047d057
        if (ItemView[Row] != nullptr)
            ItemView[Row]->SetProperty(0xc, bSelected, 0);

        // 0047d05d..0047d161: the item's icon mesh and the button's name IDS.
        WinControl* const Mesh = ItemMesh[Row];
        if (Mesh != nullptr && ItemButton[Row] != nullptr)
        {
            int NameIds = 0;
            if (Good != nullptr)
            {
                Mesh->Field2ec = static_cast<int>(Row);
                char const* const Icon = Good->ItemIcon.Value; // +0x78
                if (Icon != nullptr)
                {
                    Mesh->SetProperty(4, reinterpret_cast<uintptr_t>(Icon), 0);
                    if (bTransfer != 0)
                        TransferItemMesh->SetProperty(4, reinterpret_cast<uintptr_t>(Icon), 0);
                }
                NameIds = Good->IdsName; // +0x88
            }
            else
            {
                // 0047d0d3..0047d0f1: both EmptyIds branches issue the same call.
                Mesh->SetProperty(0x17, static_cast<uintptr_t>(-1), 0);
            }
            Mesh->SetProperty(0x19, 0x10000, 0x10000);
            if (InfoCardView != nullptr)
                InfoCardView->SetProperty(0x19, 0, 0);
            Mesh->SetProperty(0xc, bSelected, 0);
            ItemButton[Row]->SetProperty(0xc, bSelected, 0);
            ItemButton[Row]->SetProperty(3, static_cast<uintptr_t>(NameIds), 0);
        }

        // 0047d167..0047d275: grey an unusable, unselected row.
        if (ItemButton[Row] != nullptr && ItemView[Row] != nullptr && ItemName[Row] != nullptr &&
            ItemPrice[Row] != nullptr && ItemQuantity[Row] != nullptr)
        {
            unsigned char bGrey = (bUsable == 0 && bSelected == 0) ? 1 : 0;
            unsigned char Dark[4] = {0x80, 0x80, 0x80, 0xff};
            ItemButton[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(Dark), bGrey);
            ItemView[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(Dark), bGrey);
            unsigned char Light[4] = {0xc0, 0xc0, 0xc0, 0xff};
            ItemPrice[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(Light), bGrey);
            if (ArchId == 0)
                bGrey = 1;
            ItemName[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(Light), bGrey);
            if (EmptyIds != 0)
                bGrey = 1;
            ItemQuantity[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(Light), bGrey);
        }
    }

    // 0047d27b..0047d28c
    ++*Counter;
    return bSelected != 0;
}

void NnInventory::BindControls()
{
    // FUN_0047b410 -- every lookup below is slot 37 (+0x94, FindChild) on this window.
    bNeedBind = 0;
    Slot45(); // +0xb4

    InventoryScroll = FindChild("Inventory_Scroll");
    PlayerCash = FindChild("PlayerCash");
    ItemName[0] = FindChild("ItemName01");
    ItemName[1] = FindChild("ItemName02");
    ItemName[2] = FindChild("ItemName03");
    ItemName[3] = FindChild("ItemName04");
    ItemName[4] = FindChild("ItemName05");
    ItemView[0] = FindChild("ItemView01");
    ItemView[1] = FindChild("ItemView02");
    ItemView[2] = FindChild("ItemView03");
    ItemView[3] = FindChild("ItemView04");
    ItemView[4] = FindChild("ItemView05");
    ItemPrice[0] = FindChild("ItemPrice01");
    ItemPrice[1] = FindChild("ItemPrice02");
    ItemPrice[2] = FindChild("ItemPrice03");
    ItemPrice[3] = FindChild("ItemPrice04");
    ItemPrice[4] = FindChild("ItemPrice05");
    ItemQuantity[0] = FindChild("ItemQuantity01");
    ItemQuantity[1] = FindChild("ItemQuantity02");
    ItemQuantity[2] = FindChild("ItemQuantity03");
    ItemQuantity[3] = FindChild("ItemQuantity04");
    ItemQuantity[4] = FindChild("ItemQuantity05");
    ItemButton[0] = FindChild("ItemButton1");
    ItemButton[1] = FindChild("ItemButton2");
    ItemButton[2] = FindChild("ItemButton3");
    ItemButton[3] = FindChild("ItemButton4");
    ItemButton[4] = FindChild("ItemButton5");
    ItemMesh[0] = FindChild("ItemMesh1");
    ItemMesh[1] = FindChild("ItemMesh2");
    ItemMesh[2] = FindChild("ItemMesh3");
    ItemMesh[3] = FindChild("ItemMesh4");
    ItemMesh[4] = FindChild("ItemMesh5");
    ItemGoodDeal[0] = FindChild("ItemGoodDeal01");
    ItemGoodDeal[1] = FindChild("ItemGoodDeal02");
    ItemGoodDeal[2] = FindChild("ItemGoodDeal03");
    ItemGoodDeal[3] = FindChild("ItemGoodDeal04");
    ItemGoodDeal[4] = FindChild("ItemGoodDeal05");
    ItemDamage[0] = FindChild("ItemDamage1");
    ItemDamage[1] = FindChild("ItemDamage2");
    ItemDamage[2] = FindChild("ItemDamage3");
    ItemDamage[3] = FindChild("ItemDamage4");
    ItemDamage[4] = FindChild("ItemDamage5");
    EquipToggle[0] = FindChild("EquipToggle1");
    EquipToggle[1] = FindChild("EquipToggle2");
    EquipToggle[2] = FindChild("EquipToggle3");
    EquipToggle[3] = FindChild("EquipToggle4");
    EquipToggle[4] = FindChild("EquipToggle5");

    // 0047b800..0047b86f: per row -- hit priority 100 on the good-deal mark, the mission text
    // (set by slot 45) and the equip toggle; damage bar slot 42 (0x19, 0x10000, 0); toggle
    // (0x34, 1, 0); button (0x35, 1, 0).
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        if (ItemGoodDeal[Row] != nullptr)
            ItemGoodDeal[Row]->HitPriority = 100;
        if (CargoMissionText[Row] != nullptr)
            CargoMissionText[Row]->HitPriority = 100;
        if (ItemDamage[Row] != nullptr)
            ItemDamage[Row]->SetProperty(0x19, 0x10000, 0);
        if (EquipToggle[Row] != nullptr)
        {
            EquipToggle[Row]->HitPriority = 100;
            EquipToggle[Row]->SetProperty(0x34, 1, 0);
        }
        if (ItemButton[Row] != nullptr)
            ItemButton[Row]->SetProperty(0x35, 1, 0);
    }

    // 0047b871..0047b90d
    InfoCardBackground = FindChild("InfoCard_Background");
    InfoCardName = FindChild("InfoCard_Name");
    InfoCardInfo = static_cast<RichTextWin*>(FindChild("InfoCard_Info"));
    InfoCardClose = FindChild("InfoCard_Close");
    InfoCardView = FindChild("InfoCard_View");
    InfoCardScroll = FindChild("InfoCard_Scroll");
    if (InfoCardClose != nullptr)
        InfoCardClose->HitPriority = 100;
    if (InfoCardView != nullptr)
    {
        // 0047b906: AND byte [view+0x518],0x7f -- the view is a ShipViewControl.
        ShipViewControl* const View = static_cast<ShipViewControl*>(InfoCardView);
        View->ViewFlags = static_cast<unsigned char>(View->ViewFlags & 0x7f);
    }

    // 0047b90d..0047b970
    FilterWeapons = FindChild("Filter_Weapons");
    FilterCargoBay = FindChild("Filter_CargoBay");
    FilterEquipment = FindChild("Filter_Equipment");
    FilterCommodity = FindChild("Filter_Commodity");
    FilterAmmo = FindChild("Filter_Ammo");

    // 0047b976..0047b9f4: the ship model's controls.
    Ship.InfoText = FindChild("ShipView_Info");
    Ship.Toggle = FindChild("ShipView_Toggle");
    Ship.DamageBar = FindChild("ShipView_DamageBar");
    Ship.NameText = FindChild("ShipView_Name");
    Ship.Frame = FindChild("Shipview_Frame");
    Ship.ShipView = static_cast<ShipViewControl*>(FindChild("ShipView_Control"));

    // 0047b9f6..0047ba00
    SetMode(G_NnInventoryLastMode, true);

    // 0047ba05..0047ba52
    JettisonButton = FindChild("JettisonButton");
    Ship.ShipArchId = GetPlayerShipArchetypeIdRetail(); // FUN_004c3e10
    Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);  // FUN_004c4060
    if (bDealerMode == 0)
        Ship.ShipView->BuildHardpointList(); // FUN_00586b70
    RefreshItemList(&Ship.Loadout, 0, 1);

    // 0047ba57..0047ba80
    PlayerTitle = FindChild("PlayerTitle");
    if (bDealerMode == 0 && Dealer == nullptr)
        Field4e0 = 1;
}

void NnInventory::ClearInfoCard()
{
    // FUN_0047e9e0. 0047e9e4..0047ea05: this window's card only on a dealer.
    if (InfoCardInfo != nullptr && bDealerMode != 0)
    {
        InfoCardInfo->SetScrollOrigin(0, 0); // FUN_005798b0
        InfoCardInfo->ClearContent();        // FUN_0057b370
    }

    // 0047ea0a..0047ea2b: the partner dealer's card.
    if (Dealer != nullptr && Dealer->InfoCardInfo != nullptr)
    {
        Dealer->InfoCardInfo->SetScrollOrigin(0, 0);
        Dealer->InfoCardInfo->ClearContent();
    }

    // 0047ea30..0047ea77: the three dealer panes; only the first is null-tested.
    if (InfoCardFacts != nullptr)
    {
        InfoCardFacts->SetScrollOrigin(0, 0);
        InfoCardFacts->ClearContent();
        InfoCardStats->SetScrollOrigin(0, 0);
        InfoCardStats->ClearContent();
        InfoCardValues->SetScrollOrigin(0, 0);
        InfoCardValues->ClearContent();
    }

    // 0047ea7c..0047eaa0: the partner dealer's +0x4e8 is stored twice (the listing reloads
    // +0x360 between the two stores).
    Field4e8 = 0;
    if (Dealer != nullptr)
    {
        Dealer->Field4e8 = 0;
        Dealer->Field4e8 = 0;
    }
}

void NnInventory::ShowInfoCard(unsigned ArchId, unsigned Ids)
{
    // FUN_0047ba90 (RET 8). 0047baad..0047babe: not while the quantity controls show.
    if (QuantityControl[0] != nullptr && (QuantityControl[0]->Flags & 2) != 0)
        return;

    // 0047bac4..0047badc: borrow the partner dealer's card when this window has none.
    if (InfoCardInfo == nullptr && Dealer != nullptr)
        InfoCardInfo = Dealer->InfoCardInfo;

    if (ArchId == 0)
    {
        // 0047baee..0047bb00
        Field4e8 = 0;
        Field4e1 = 0;
        if (Ids == 0)
        {
            // 0047bc6f..0047bcaf: nothing to show; forget the text and reset the partners.
            Field4ec = 0;
            if (Dealer != nullptr)
            {
                Dealer->Field4e8 = 0;
                Dealer->Field4ec = 0;
            }
            if (bDealerMode != 0)
            {
                NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
                Partner->Field4e8 = 0;
                Partner->Field4ec = 0;
            }
            return;
        }

        // 0047bb0c..0047bb3c
        if (Dealer != nullptr)
        {
            Dealer->Field4e8 = 0;
            Dealer->Field4ec = 0;
        }
        if (bDealerMode != 0)
        {
            NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
            Partner->Field4e8 = 0;
            Partner->Field4ec = 0;
        }

        // 0047bb42..0047bb53: the same text again changes nothing.
        if (Field4ec == Ids)
            return;
        Field4ec = Ids;

        // 0047bb4e..0047bbe3: a style node, then the text; FUN_0057da40's result is not read.
        RenderDisplayList Text;
        {
            StyleNode Style(0x9ca4);
            RdlAppendNode(Text, Style.Clone());
        }
        LoadIdsIntoRdl(Field4ec, Text);

        // 0047bbe8..0047bc31: the dealer panes give way to the single card; a window without
        // them relabels its title (+0x944, IDS 0x388).
        if (InfoCardFacts != nullptr)
        {
            InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
            InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
            InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
            InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
        }
        else if (Field944 != nullptr)
        {
            Field944->SetProperty(0x1e, 0x388, 0);
        }

        // 0047bc37..0047bc65: the card is not null-tested here.
        InfoCardInfo->SetScrollOrigin(0, 0); // FUN_005798b0
        InfoCardInfo->ClearContent();        // FUN_0057b370
        InfoCardInfo->AppendContent(Text);   // FUN_0057b3e0
        return;                              // FUN_0046ba90: the list goes with Text
    }

    // 0047bcb4..0047bd02: the same archetype again changes nothing.
    if (Field4e8 == ArchId)
        return;
    Field4e8 = ArchId;
    Field4ec = 0;
    if (Dealer != nullptr)
    {
        Dealer->Field4e8 = 0;
        Dealer->Field4ec = 0;
    }
    if (bDealerMode != 0)
    {
        NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
        Partner->Field4e8 = 0;
        Partner->Field4ec = 0;
    }

    // 0047bd08..0047bd1a: the name, the two info texts and the trade-text flag.
    int IdsName = 0;   // [ESP+0x10]
    int IdsFirst = 0;  // [ESP+0x14]
    int IdsSecond = 0; // [ESP+0x20]
    bool bTradeText = false; // the second argument's byte, reused

    if (Field4e0 != 0)
    {
        // 0047bd25..0047bd52: ship mode.
        Archetype::Ship const* const ShipArch = Archetype::GetShip(ArchId);
        if (ShipArch != nullptr)
        {
            IdsFirst = ShipArch->IdsInfo1;
            IdsName = ShipArch->IdsName;
            IdsSecond = ShipArch->IdsInfo;

            // 0047bd56..0047bd6a: the three panes need a dealer (this one or the partner) and
            // the panes themselves.
            if ((bDealerMode != 0 || Dealer != nullptr) && InfoCardFacts != nullptr)
            {
                // 0047bd70..0047be04: pane 1, IdsInfo1 (+0xec).
                RenderDisplayList Facts;
                {
                    StyleNode Style(0x9ca4);
                    RdlAppendNode(Facts, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo1), Facts);

                // 0047be09..0047be36: show the panes, hide the single card (not null-tested).
                InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags | 3);
                InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags | 3);
                InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags | 3);
                InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);

                // 0047be39..0047be56
                InfoCardFacts->SetScrollOrigin(0, 0);
                InfoCardFacts->ClearContent();
                InfoCardFacts->AppendContent(Facts);

                // 0047be5b..0047bf11: pane 2, IdsInfo2 (+0xf0).
                RenderDisplayList Stats;
                {
                    StyleNode Style(0x9ca4);
                    RdlAppendNode(Stats, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo2), Stats);
                InfoCardStats->SetScrollOrigin(0, 0);
                InfoCardStats->ClearContent();
                InfoCardStats->AppendContent(Stats);

                // 0047bf16..0047bfcc: pane 3, IdsInfo3 (+0xf4).
                RenderDisplayList Values;
                {
                    StyleNode Style(0x9ca4);
                    RdlAppendNode(Values, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo3), Values);
                InfoCardValues->SetScrollOrigin(0, 0);
                InfoCardValues->ClearContent();
                InfoCardValues->AppendContent(Values);

                // 0047bfd1..0047c087: the three lists are freed; the name label is not set.
                return;
            }
        }
    }
    else
    {
        // 0047c08c..0047c0ef: item mode -- the good's texts, else the archetype's.
        Archetype::Equipment const* const Equip = Archetype::GetEquipment(ArchId);
        GoodInfo const* const Good = GoodList::find_by_archetype(ArchId);
        if (Good != nullptr)
        {
            // 0047c0a6..0047c0d7: FUN_005835f0 == 1 (an item without a hp type) asks for the
            // trade text; its two out-values land in slots the listing overwrites next.
            Archetype::Equipment const* OutWeapon = nullptr;
            Archetype::Equipment const* OutShield = nullptr;
            bTradeText = ShipViewEquipmentHpClass(ArchId, &OutWeapon, &OutShield) == 1;
            IdsName = Good->IdsName;
            IdsFirst = Good->IdsInfo;
        }
        else if (Equip != nullptr)
        {
            IdsName = Equip->IdsName;
            IdsFirst = Equip->IdsInfo;
        }
    }

    // 0047c0f3..0047c107: the name label.
    if (InfoCardName != nullptr)
        InfoCardName->SetProperty(0x1e, static_cast<uintptr_t>(IdsName), 0);

    // 0047c10d..0047c113
    if (InfoCardInfo == nullptr)
        return;

    // 0047c119..0047c1e7: the card's text -- a style node and the (non-zero) info texts.
    RenderDisplayList Card;
    {
        StyleNode Style(0x9ca4);
        RdlAppendNode(Card, Style.Clone());
    }
    if (IdsFirst != 0)
        LoadIdsIntoRdl(static_cast<unsigned>(IdsFirst), Card);
    if (IdsSecond != 0)
        LoadIdsIntoRdl(static_cast<unsigned>(IdsSecond), Card);

    // 0047c1ea..0047c232: items with a stat table get the three panes (switch table
    // 0x0047c72c over class types 0xe..0x21: ShieldGenerator, Thruster, Gun, MineDropper,
    // CounterMeasureDropper, RepairKit, ShieldBattery, Munition, Mine, CounterMeasure).
    if (Field4e0 == 0 && InfoCardFacts != nullptr)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr)
        {
            int const Class = static_cast<int>(Equip->get_class_type()); // vt+0x10
            bool bTwoColumn = false;
            switch (Class)
            {
            case Archetype::AClassShieldGenerator:
            case Archetype::AClassThruster:
            case Archetype::AClassGun:
            case Archetype::AClassMineDropper:
            case Archetype::AClassCounterMeasureDropper:
            case Archetype::AClassRepairKit:
            case Archetype::AClassShieldBattery:
            case Archetype::AClassMunition:
            case Archetype::AClassMine:
            case Archetype::AClassCounterMeasure:
                bTwoColumn = true;
                break;
            default:
                break;
            }

            if (bTwoColumn)
            {
                // 0047c239..0047c27e: panes on, card off; the info text goes to pane 1.
                InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags | 3);
                InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags | 3);
                InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags | 3);
                InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);
                InfoCardFacts->SetScrollOrigin(0, 0);
                InfoCardFacts->ClearContent();
                InfoCardFacts->AppendContent(Card);

                // 0047c283..0047c438: pane 2 -- header style 0xf64, IDS 0x6e1, a line break
                // (DAT_005d0254 L"\n"), body style 0x9ca4, then the stat labels.
                RenderDisplayList Labels;
                {
                    StyleNode Style(0xf64);
                    RdlAppendNode(Labels, Style.Clone());
                }
                LoadIdsIntoRdl(0x6e1, Labels);
                AppendTextMaybeXmlRdl(L"\n", static_cast<unsigned>(wcslen(L"\n")), Labels, false);
                {
                    StyleNode Style(0x9ca4);
                    RdlAppendNode(Labels, Style.Clone());
                }
                BuildDealerEquipmentStatText(Labels, ArchId, true, false); // FUN_00484090(,,1,0)
                InfoCardStats->SetScrollOrigin(0, 0);
                InfoCardStats->ClearContent();
                InfoCardStats->AppendContent(Labels);

                // 0047c43d..0047c54f: pane 3 -- body style, a line break, the stat values.
                RenderDisplayList Values;
                {
                    StyleNode Style(0x9ca4);
                    RdlAppendNode(Values, Style.Clone());
                }
                AppendTextMaybeXmlRdl(L"\n", static_cast<unsigned>(wcslen(L"\n")), Values, false);
                BuildDealerEquipmentStatText(Values, ArchId, false, true); // FUN_00484090(,,0,1)
                InfoCardValues->SetScrollOrigin(0, 0);
                InfoCardValues->ClearContent();
                InfoCardValues->AppendContent(Values);

                // 0047c554..0047c5eb: the lists are freed.
                return;
            }
        }
    }

    // 0047c5f4..0047c64f: the single card; a window without panes relabels its title
    // (+0x944, not null-tested): IDS 0x387 in ship mode, 0x388 otherwise.
    if (InfoCardFacts != nullptr)
    {
        InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
        InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
        InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
        InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
    }
    else if (Field4e0 != 0)
    {
        Field944->SetProperty(0x1e, 0x387, 0);
    }
    else
    {
        Field944->SetProperty(0x1e, 0x388, 0);
    }

    // 0047c655..0047c692
    if (bTradeText && (bDealerMode != 0 || Dealer != nullptr))
    {
        // 0047c66b..0047c676: FUN_004704d0(&Card, ArchId) -- the commodity's price at every
        // known base.
        AppendCommodityTradeReport(&Card, ArchId);
    }
    else if (Field4e0 == 0)
    {
        BuildDealerEquipmentStatText(Card, ArchId, true, true); // FUN_00484090(,,1,1)
    }

    // 0047c695..0047c6b2
    InfoCardInfo->SetScrollOrigin(0, 0);
    InfoCardInfo->ClearContent();
    InfoCardInfo->AppendContent(Card);
}

void NnInventory::UpdateRowVisibility(int Count)
{
    // FUN_0047f3e0 (RET 4). 0047f3e7..0047f402: a dealer with +0x98c set hides every row.
    bool bHideAll = false;
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->Field98c != 0)
        bHideAll = true;

    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        // 0047f413..0047f43a: past the end of the list (unsigned compare) or all hidden.
        bool const bHide =
            static_cast<unsigned>(FirstRow + Row) >= static_cast<unsigned>(Count) || bHideAll;

        // 0047f43f..0047f457: the price columns also hide while the partner dealer is hidden.
        bool bHidePrice = bHide;
        if (Dealer != nullptr && (Dealer->Flags & 2) == 0)
            bHidePrice = true;

        // 0047f45c..0047f499: ...and when no price applies: an own inventory next to no dealer,
        // a commodity trader (1) outside the commodity filter, an equipment dealer (3) on the
        // commodity filter. A ship dealer (2) and a dealer window itself always price.
        bool bNoPrice = true;
        if (Dealer != nullptr)
        {
            int const TradeMode = Dealer->TradeMode;
            if (TradeMode == 3)
            {
                if (Mode != 4)
                    bNoPrice = false;
            }
            else if (TradeMode == 1)
            {
                if (Mode == 4)
                    bNoPrice = false;
            }
            if (TradeMode == 2)
                bNoPrice = false;
        }
        if (bDealerMode != 0)
            bNoPrice = false;

        uintptr_t const HidePriceValue = (bHidePrice || bNoPrice) ? 1 : 0;

        // 0047f49b..0047f551
        if (ItemName[Row] != nullptr)
            ItemName[Row]->SetProperty(1, bHide ? 1 : 0, 0);
        if (ItemPrice[Row] != nullptr)
            ItemPrice[Row]->SetProperty(1, HidePriceValue, 0);
        if (ItemQuantity[Row] != nullptr && bHide)
            ItemQuantity[Row]->SetProperty(1, 1, 0);
        if (ItemGoodDeal[Row] != nullptr)
            ItemGoodDeal[Row]->SetProperty(1, HidePriceValue, 0);
        if (ItemDamage[Row] != nullptr)
            ItemDamage[Row]->SetProperty(1, bHide ? 1 : 0, 0);
        if (CargoMissionText[Row] != nullptr)
            CargoMissionText[Row]->SetProperty(1, bHide ? 1 : 0, 0);

        // 0047f557..0047f5be: the equip toggle -- hidden on empty rows and on the ammo /
        // commodity filters; disabled on a dealer other than the ship dealer, and on an own
        // inventory while the player is in space.
        if (EquipToggle[Row] != nullptr)
        {
            if (bHide || Mode == 4 || Mode == 1)
                EquipToggle[Row]->SetProperty(1, 1, 0);
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != 2)
                    EquipToggle[Row]->SetProperty(2, 1, 0);
            }
            else if (!IsPlayerDockedRetail()) // FUN_004c4810
            {
                EquipToggle[Row]->SetProperty(2, 1, 0);
            }
        }

        // 0047f5c4..0047f657: the view / button / mesh trio.
        if (ItemView[Row] != nullptr && ItemButton[Row] != nullptr && ItemMesh[Row] != nullptr)
        {
            uintptr_t const HideValue = bHide ? 1 : 0;
            ItemButton[Row]->SetProperty(1, 0, 0);
            ItemMesh[Row]->SetProperty(1, HideValue, 0);
            ItemView[Row]->SetProperty(1, 0, 0);
            ItemView[Row]->SetProperty(2, HideValue, 0);
            ItemButton[Row]->SetProperty(2, HideValue, 0);
            if (bHide)
            {
                ItemView[Row]->SetProperty(3, 0, 0);
                ItemButton[Row]->SetProperty(3, 0, 0);
            }
        }
    }

    // 0047f672..0047f688
    if (TradeRouteButton == nullptr || TradeRouteText == nullptr)
        return;

    // 0047f68e..0047f6a2
    bool bMission = false;
    EquipDesc const* const Selected = GetSelectedItem(); // FUN_0047e790
    if (Selected != nullptr && Selected->bMission)
        bMission = true;

    // 0047f6a4..0047f6f9: the trade-route pair shows for a selected, non-mission commodity,
    // unless slot 46 says otherwise (NN_Scanner) or the quantity controls are up.
    if (Mode == 4 && SelectedIndex != -1 && !bMission && !Slot46() &&
        (QuantityControl[0] == nullptr || (QuantityControl[0]->Flags & 2) == 0))
    {
        TradeRouteButton->Flags = static_cast<unsigned char>(TradeRouteButton->Flags | 3);
        TradeRouteText->Flags = static_cast<unsigned char>(TradeRouteText->Flags | 3);
        return;
    }

    // 0047f6fc..0047f712
    TradeRouteButton->Flags = static_cast<unsigned char>(TradeRouteButton->Flags & 0xfc);
    TradeRouteText->Flags = static_cast<unsigned char>(TradeRouteText->Flags & 0xfc);
}

// DAT_00611a24 -- the info-card text of an empty hardpoint row, by FShipViewHpEntry::Class
// (read only at 0x0047e03e).
static unsigned const G_NnHpClassInfoIds[10] = {
    0x38b, 0x38c, 0x394, 0x38f, 0x38d, 0x38e, 0x390, 0x391, 0x392, 0x393,
};

void NnInventory::RefreshItemList(EquipDescList const* List, int Arg2, unsigned char bArg3)
{
    // FUN_0047d2f0 (RET 0xc). Arg2 is only handed on to the recursive call.

    // 0047d30e..0047d326: the partner dealer's trade mode (0 without one).
    int const DealerTradeMode = (Dealer != nullptr) ? Dealer->TradeMode : 0;

    // 0047d32a..0047d340: resync with the player's loadout when asked to and the player has a
    // ship.
    if (bArg3 != 0 && GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        // 0047d346..0047d395: the loadout changed since the last look.
        if (Field908 != static_cast<int>(GetPlayerEquipRevisionRetail())) // FUN_004c4f90
        {
            Field3b8 = 0;
            Field908 = static_cast<int>(GetPlayerEquipRevisionRetail());
            if (Ship.ShipView != nullptr)
                Ship.ShipView->LoadEquipment(&GetPlayerEquipDescListRetail()); // FUN_005850f0
            Ship.SortFrom(&GetPlayerEquipDescListRetail()); // FUN_004c6b80
            SelectedItem = nullptr;                          // +0x4f4

            // 0047d39b..0047d3fe: next to an equipment dealer, the partner's +0x394 names the
            // hardpoint the dealer's selected good would go on (weapons / equipment filters).
            NnDealer* const Partner = Dealer;
            if (Partner != nullptr && Partner->TradeMode == 3 && Partner->Inventory != nullptr)
            {
                Partner->Inventory->Field394.clear();
                unsigned const GoodId = Partner->GetSelectedMarketGood(); // FUN_00483580
                if (GoodId != 0 && (Partner->Mode == 0 || Partner->Mode == 2))
                {
                    char const* const Hardpoint =
                        Partner->Inventory->Ship.ShipView->FindHardpointForGood(GoodId, false); // FUN_00585f20
                    if (Hardpoint != nullptr)
                        Partner->Inventory->Field394.Value = StringAlloc(Hardpoint, false);
                }
            }

            // 0047d404..0047d46f: a pending good (+0x3bc) that no longer fits drops the partner
            // dealer's selection. The ship view is not null-tested here.
            if (Field3b9 != 0)
            {
                unsigned const PendingGood = Field3bc;
                Field3b9 = 0;
                if (Ship.ShipView->FindHardpointForGood(PendingGood, false) == nullptr &&
                    Dealer != nullptr)
                    Dealer->ClearSelection();
            }

            // 0047d475..0047d486
            if (Field3c0 != 0)
            {
                Field3c0 = 0;
                Field3c1 = 1;
            }
        }

        // 0047d48d..0047d4ec: the ship's collision groups changed: reload the hull.
        if (Field90c != static_cast<int>(GetPlayerCollisionRevisionRetail())) // FUN_004c4fa0
        {
            Field90c = static_cast<int>(GetPlayerCollisionRevisionRetail());
            Archetype::Ship* const ShipArch = Archetype::GetShip(Ship.ShipArchId); // +0x348
            if (ShipArch != nullptr && Ship.ShipView != nullptr)
            {
                Ship.ShipView->LoadEquipment(nullptr);                 // FUN_005850f0(0)
                Ship.ShipView->SetInstance(-1, true);                  // FUN_00585380(-1, 1)
                Ship.ShipView->LoadShip(&ShipArch->Nickname, true, true); // FUN_00585a90(+8, 1, 1)
            }

            // 0047d4f1..0047d516
            if (Ship.ShipView != nullptr)
                Ship.ShipView->LoadEquipment(&GetPlayerEquipDescListRetail());
            Ship.SortFrom(&GetPlayerEquipDescListRetail());

            // 0047d51b..0047d53d: next to an equipment dealer, FUN_00446be0 (the GF ObjectShip)
            // gets the new loadout (FUN_00448540(&+0x34c)) and FUN_00447210.
            if (DealerTradeMode == 3)
            {
                FObjectShip* const GfShip = GfObjectShip();
                if (GfShip != nullptr)
                {
                    GfShip->SetEquipment(&Ship.Loadout);
                    GfShip->RebuildModel();
                }
            }
        }
    }

    // 0047d542..0047d570: the player's credits into the cash label.
    if (PlayerCash != nullptr)
    {
        FormatGroupedNumber(ToolTipText, static_cast<int>(GetPlayerCreditsRetail()), 0x476); // FUN_004779a0(,FUN_004c4ce0(),)
        PlayerCash->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    // 0047d576..0047d59e: docked at a base (DAT_0066873c > 0, JBE), FUN_0043a510 on
    // DAT_00668708 finds the base's market ([ESP+0x14]); otherwise none. Every use below tests
    // it for null.
    FClientMarket* Market = nullptr;
    {
        unsigned const BaseId = GetClientLocationCurrentBase();
        if (BaseId != 0)
            Market = GetClientLocationBaseMarket(BaseId);
    }
    int Counter = 0;                       // V_18
    EquipDesc const* FirstSelected = nullptr; // V_1c
    GoodInfo const* FirstGood = nullptr;   // V_30
    EquipDesc const* Hovered = nullptr;    // V_2c

    // 0047d5a2..0047d5c8: a ship dealer lists every filter at once.
    bool bAllModes = false; // V_11
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == 2)
        bAllModes = true;
    int FocusCounter = -1;  // V_40
    int SelectedHpClass = -1; // V_50

    // 0047d5cc..0047d7f1: an own inventory lists the ship's hardpoints first (empty ones too).
    if (bDealerMode == 0)
    {
        int const HardpointCount = Ship.ShipView->GetHardpointListCount(); // FUN_00587050
        for (int Index = 0; Index < HardpointCount; ++Index)
        {
            FShipViewHpEntry const* const Entry = Ship.ShipView->GetHardpointListEntry(Index); // FUN_00587070
            if (Entry == nullptr)
                continue;

            unsigned ArchId = 0;           // V_20
            int Price = 0;                 // V_28
            int Health = 0;                // V_44
            unsigned char bShowToggle = 0; // V_34
            unsigned char bEquipped = 0;   // V_38
            unsigned char bMission = 0;    // V_3c
            int const RowMode = (Entry->Group == 1) ? 2 : 0; // V_24

            // 0047d639..0047d661: the item on this hardpoint.
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Entry->Name.c_str(), false);
            EquipDesc const* const Item = List->find_equipment_item(Hardpoint);
            GoodInfo const* Good = nullptr;
            if (Item != nullptr)
            {
                // 0047d673..0047d69e
                bMission = Item->bMission ? 1 : 0;
                ArchId = Item->ArchId;
                bShowToggle = 1;
                bEquipped = 1;
                Health = static_cast<int>(static_cast<double>(Item->Health) * 100.0); // DAT_005ca230
                Good = GoodList::find_by_archetype(ArchId);

                // 0047d6a6..0047d6cf: the row under the mouse.
                if ((RowMode == Mode || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
                    Hovered = Item;

                // 0047d6d3..0047d757: with a good and a market whose transaction type for it
                // (FUN_00438100) is 0 or 1: ftol(FUN_00438060(Good->Id) * Item->Health); at type
                // 0, for a non-combinable good (+0x70) whose item is in the saved loadout +0x3a8,
                // ftol(price (FILD) * 0.3f (DAT_005d0e2c) + 0.49999f (DAT_005d1910)).
                Price = 0;
                if (Good != nullptr && Market != nullptr)
                {
                    int const Type = ClientMarketGetTransactionType(Market, Good->Id);
                    if (Type == 0 || Type == 1)
                    {
                        Price = static_cast<int>(
                            static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) *
                            static_cast<double>(Item->Health));
                        if (Type == 0 && !Good->bCombinable &&
                            SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                            Price = static_cast<int>(static_cast<double>(Price) *
                                                         0.30000001192092896 +
                                                     0.49998998641967773);
                    }
                }

                // 0047d75b..0047d777: the item to keep in view.
                if (Field948 != SUBOBJ_ID_NONE && Item->Id == Field948)
                    FocusCounter = Counter;
            }

            // 0047d77b..0047d7b6
            bool const bSelected = FillItemRow(&Counter, ArchId, RowMode, 1, Price, Health,
                                               bShowToggle, bEquipped, bMission, 1, Entry->Ids,
                                               Entry->Class, 0);

            // 0047d7bb..0047d7de: the first selected row; an empty hardpoint keeps its class.
            if (FirstSelected == nullptr && bSelected)
            {
                FirstSelected = Item;
                FirstGood = Good;
                if (ArchId == 0)
                    SelectedHpClass = Entry->Class;
            }
        }
    }

    // 0047d7f7..0047da9d: every item of the list.
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const* const Item = &*It;
        GoodInfo const* const Good = GoodList::find_by_archetype(Item->ArchId);
        int Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460, V_20

        // 0047d838..0047d921: the price in the base's market, with a good and a market whose
        // transaction type for it (FUN_00438100) is 0 or 1: a commodity (filter 4)
        // ftol(FUN_00438060(Good->Id)), anything else ftol(that * Item->Health). Then the
        // used-goods rate ftol(price (FILD) * 0.3f + 0.49999f) for a non-commodity,
        // non-combinable good (+0x70) whose item is in this window's saved loadout +0x3a8 (type
        // 0 only), or -- any type -- on a ship dealer (+0x35c, trade mode 2) in its partner's
        // +0x3a8 (the partner null-tested).
        int Price = 0; // V_24
        if (Good != nullptr && Market != nullptr)
        {
            int const Type = ClientMarketGetTransactionType(Market, Good->Id);
            if (Type == 0 || Type == 1)
            {
                if (Filter == 4)
                    Price = static_cast<int>(ClientMarketGetPrice(Market, Good->Id));
                else
                    Price = static_cast<int>(
                        static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) *
                        static_cast<double>(Item->Health));

                bool bUsed = false;
                if (Type == 0 && Filter != 4 && !Good->bCombinable &&
                    SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                    bUsed = true;
                else if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == 2 &&
                         Filter != 4 && !Good->bCombinable)
                {
                    NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
                    if (Partner != nullptr &&
                        Partner->SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                        bUsed = true;
                }
                if (bUsed)
                    Price = static_cast<int>(static_cast<double>(Price) * 0.30000001192092896 +
                                             0.49998998641967773);
            }
        }

        // 0047d925..0047d961
        int const Health = static_cast<int>(static_cast<double>(Item->Health) * 100.0);
        unsigned char bShowToggle = 0; // V_38
        unsigned char bEquipped = 0;   // V_34
        if (Good != nullptr && Filter != 1 && Filter != 4)
        {
            bEquipped = Item->bEquipped ? 1 : 0;
            bShowToggle = 1;
        }

        // 0047d965..0047d970
        if (bAllModes)
            Filter = -1;

        // 0047d974..0047d981: mounted weapons and equipment are already on the hardpoint rows.
        bool bSelected = false;
        if (!((Filter == 0 || Filter == 2) && bEquipped != 0))
        {
            // 0047d987..0047d9a3
            if (Field948 != SUBOBJ_ID_NONE && Item->Id == Field948)
                FocusCounter = Counter;

            // 0047d9a7..0047da06: whether the item can be used: a free hardpoint for an
            // unmounted weapon / equipment of an own inventory, a launcher for ammo.
            bool bUnusable = false;
            if (bEquipped == 0 && bDealerMode == 0 && Good != nullptr && (Mode == 0 || Mode == 2))
            {
                if (Ship.ShipView->FindHardpointForGood(Good->Id, true) == nullptr) // FUN_00585f20
                    bUnusable = true;
            }
            else if (Mode == 1)
            {
                bUnusable = !NnListHasLauncherFor(List, Item->ArchId); // FUN_00482d80
            }

            // 0047da08..0047da30
            if ((Filter == Mode || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
                Hovered = Item;

            // 0047da34..0047da74
            bSelected = FillItemRow(&Counter, Item->ArchId, Filter, Item->Count, Price, Health,
                                    bShowToggle, bEquipped, Item->bMission ? 1 : 0,
                                    bUnusable ? 0 : 1, 0, 0, 0);
        }

        // 0047da79..0047da89
        if (FirstSelected == nullptr && bSelected)
        {
            FirstSelected = Item;
            FirstGood = Good;
        }
    }

    // 0047daa3..0047dd2f: ammo the list lacks for its launchers, once per ammo type.
    std::vector<unsigned> ListedAmmo; // V_60
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const* const Item = &*It;

        // 0047dad8..0047db26: a gun / mine dropper / countermeasure dropper's projectile.
        unsigned AmmoId = 0; // V_24
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr)
        {
            int const Class = static_cast<int>(Arch->get_class_type()); // vt+0x10
            if (Class == Archetype::AClassGun || Class == Archetype::AClassMineDropper ||
                Class == Archetype::AClassCounterMeasureDropper)
            {
                ID_String const& Projectile = static_cast<Archetype::Launcher*>(Arch)->ProjectileArchetype; // +0x98
                if (!Projectile.IsEmpty())
                    AmmoId = Projectile.Id;
            }
        }
        if (AmmoId == 0)
            continue;

        // 0047db30..0047db47: the list holds some already.
        if (List->find_matching_cargo(AmmoId, 0, 1.0f) != nullptr)
            continue;

        // 0047db4d..0047db7c: listed already.
        bool bListed = false;
        for (size_t Seen = 0; Seen < ListedAmmo.size(); ++Seen)
        {
            if (ListedAmmo[Seen] == AmmoId)
            {
                bListed = true;
                break;
            }
        }
        if (bListed)
            continue;

        // 0047db7e..0047dba9: the hovered row is the launcher's.
        if ((Mode == 1 || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
            Hovered = Item;

        // 0047dbad..0047dbd2: an empty ammo row (ammo filter, count shown, health 100).
        bool const bSelected = FillItemRow(&Counter, AmmoId, 1, 0, 0, 100, 0, 0, 0, 0, 0, 0, 1);

        // 0047dbce..0047dce4
        ListedAmmo.push_back(AmmoId);

        // 0047dce8..0047dd1d: selecting it selects a stand-in item of that ammo (+0x94c).
        if (FirstSelected == nullptr && bSelected)
        {
            Field94c.set_arch_id(AmmoId);
            FirstSelected = &Field94c;
            FirstGood = GoodList::find_by_archetype(AmmoId);
        }
    }

    // 0047dd35..0047dd6b: +0x4e6 = the market takes the selected unmounted item: a first
    // selected item that is not mounted (+0xc), a market, and FUN_00438100(FirstGood->Id) 0 or 1
    // (FirstGood is not null-tested).
    Field4e6 = 0;
    if (FirstSelected != nullptr && !FirstSelected->bEquipped && Market != nullptr)
    {
        int const Type = ClientMarketGetTransactionType(Market, FirstGood->Id);
        if (Type == 0 || Type == 1)
            Field4e6 = 1;
    }

    // 0047dd72..0047ddb3: the cargo space of the hovered item; mounted weapons / equipment and
    // everything on a dealer take none.
    Field910 = 0;
    if (Hovered != nullptr)
    {
        Field910 = static_cast<int>(Hovered->get_cargo_space_occupied());
        if ((Mode == 0 || Mode == 2) && (bDealerMode != 0 || Hovered->bEquipped))
            Field910 = 0;
    }

    // 0047ddb9..0047ddc0
    UpdateRowVisibility(Counter); // FUN_0047f3e0

    // 0047ddc5..0047dde2: LAB_0053e4a0 (`RET 4`) on &+0x330 with (+0x4e1 ? 0 : +0x4e2) -- a
    // no-op.

    // 0047dde7..0047de77: the jettison button needs a selected, unmounted, lootable item with
    // no mission flag and no pending transfer (+0x914), and not the ammo stand-in.
    if (JettisonButton != nullptr)
    {
        unsigned char bMission = 0;  // V_12
        unsigned char bLootable = 0; // V_13
        unsigned char bEquipped = 0; // BL
        if (FirstSelected != nullptr)
        {
            bMission = FirstSelected->bMission ? 1 : 0;
            bEquipped = FirstSelected->bEquipped ? 1 : 0;
            Archetype::Equipment* const Arch = Archetype::GetEquipment(FirstSelected->ArchId);
            if (Arch != nullptr)
                bLootable = Arch->bLootable ? 1 : 0; // +0x6c
        }
        // 0047de34..0047de45: TEST AH,0x41 / JZ -- disabled while +0x914 > 0.
        bool bDisabled = true;
        if (SelectedIndex != -1 && bEquipped == 0 && !(Field914 > 0.0f) && bMission == 0 &&
            bLootable != 0 && FirstSelected != &Field94c)
            bDisabled = false;
        JettisonButton->SetProperty(2, bDisabled ? 1 : 0, 0);
    }

    // 0047de7d..0047dea7: the ship's damage bar.
    if (Ship.DamageBar != nullptr)
    {
        int const ShipHealth = static_cast<int>(static_cast<double>(GetPlayerHealthScaleRetail()) *
                                                100.0); // FUN_004c4f70, DAT_005ca230
        Ship.DamageBar->SetProperty(0x40, 100, static_cast<uintptr_t>(ShipHealth));
    }

    // 0047dead..0047df94: the row count changed: select the focused item, rescale the scroll
    // bar, keep the focus on the page, then build the list again.
    if (Counter != Field3c8)
    {
        SelectedIndex = -1;
        if (FocusCounter != -1)
            SelectedIndex = FocusCounter;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Counter - 1));

        // 0047dee7..0047df26 (unsigned compares)
        if (FocusCounter != -1)
        {
            if (static_cast<unsigned>(FocusCounter) > static_cast<unsigned>(FirstRow + 4))
                FirstRow = FocusCounter - 4;
            if (static_cast<unsigned>(FirstRow) > static_cast<unsigned>(FocusCounter))
                FirstRow = FocusCounter;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow), 0);
        }

        // 0047df2c..0047df6e: no gap below the last row.
        if (FirstRow != 0 &&
            static_cast<unsigned>(FirstRow + 5) > static_cast<unsigned>(Counter))
        {
            if (static_cast<unsigned>(Counter) > 5)
                FirstRow = Counter - 5;
            else
                FirstRow = 0;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow), 0);
        }

        // 0047df74..0047df94
        Field3c8 = Counter;
        RefreshItemList(List, Arg2, bArg3);
    }

    // 0047df99..0047dffe: +0x4e0 (the card shows the ship) holds on an own inventory with no
    // dealer and no selection; any selection here or on the partner dealer clears it on all
    // three windows.
    if (SelectedIndex != -1 || (Dealer != nullptr && Dealer->SelectedIndex != -1))
    {
        Field4e0 = 0;
        if (Dealer != nullptr)
            Dealer->Field4e0 = 0;
        if (bDealerMode != 0)
            static_cast<NnDealer*>(this)->Inventory->Field4e0 = 0;
    }
    if (bDealerMode == 0 && Dealer == nullptr && SelectedIndex == -1)
        Field4e0 = 1;

    // 0047e005..0047e0a4: the info card.
    if (Field4e0 != 0)
    {
        ShowInfoCard(Ship.ShipArchId, 0);
    }
    else if (!bAllModes && SelectedIndex != -1)
    {
        if (SelectedHpClass != -1)
            ShowInfoCard(0, G_NnHpClassInfoIds[SelectedHpClass]); // DAT_00611a24
        else if (FirstSelected != nullptr)
            ShowInfoCard(FirstSelected->ArchId, 0);
        else
            ShowInfoCard(0, 0);
    }
    else if (SelectedIndex == -1 && Dealer != nullptr && Dealer->SelectedIndex == -1)
    {
        if (Dealer->TradeMode == 2)
            ShowInfoCard(0, 0x487);
        else
            ClearInfoCard(); // FUN_0047e9e0
    }

    // 0047e0a9..0047e0be: the focus request is spent; the ammo list goes with ListedAmmo.
    Field948 = SUBOBJ_ID_NONE;
}

bool NnInventory::HasEquippedOfClass(unsigned ArchId) const
{
    // FUN_0047fb40 (RET 4).
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr)
        return false;
    Archetype::AClassType const Class = Arch->get_class_type();
    for (std::list<EquipDesc>::const_iterator It = Ship.Loadout.Items.begin();
         It != Ship.Loadout.Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        Archetype::Equipment* const ItemArch = Archetype::GetEquipment(Item.ArchId);
        if (ItemArch == nullptr)
            continue;
        if (ItemArch->get_class_type() == Class && Item.bEquipped)
            return true;
    }
    return false;
}

EquipDesc* NnInventory::GetSelectedItem()
{
    // FUN_0047e790.
    if (SelectedIndex == -1)
    {
        SelectedItem = nullptr;
        return nullptr;
    }
    if (SelectedItem != nullptr)
        return SelectedItem;

    // 0047e7c6..0047e7e4: a dealer (+0x35c) in trade mode 2 (+0x978) lists every mode.
    unsigned char bAllModes = 0;
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == 2)
        bAllModes = 1;

    int Counter = 0; // [ESP+0x14]
    if (bDealerMode == 0)
    {
        // 0047e7f4..0047e88f: the ship view's hardpoint rows first -- group 1 (+0x18) belongs
        // to mode 2, every other group to mode 0. The row's item is looked up by hardpoint
        // name through a StringAlloc'd CacheString.
        int const Count = Ship.ShipView->GetHardpointListCount(); // FUN_00587050
        for (int Index = 0; Index < Count; ++Index)
        {
            FShipViewHpEntry const* const Entry = Ship.ShipView->GetHardpointListEntry(Index); // FUN_00587070
            if (Entry == nullptr)
                continue;
            int const RowMode = (Entry->Group == 1) ? 2 : 0;
            if (bAllModes == 0 && RowMode != Mode)
                continue;
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Entry->Name.c_str(), false);
            EquipDesc* const Found = Ship.Loadout.find_equipment_item(Hardpoint);
            if (bAllModes != 0 || RowMode == Mode)
            {
                if (Counter == SelectedIndex)
                {
                    SelectedItem = Found;
                    return Found;
                }
            }
            ++Counter;
        }
    }

    // 0047e895..0047e8fe: then the loadout's items of the mode (filter 5 = none never counts);
    // a mounted item counts only outside modes 0 and 2, whose mounted rows came from the
    // hardpoint list above.
    for (std::list<EquipDesc>::iterator It = Ship.Loadout.Items.begin();
         It != Ship.Loadout.Items.end(); ++It)
    {
        EquipDesc* const Item = &*It;
        int const Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460
        if (Filter == 5)
            continue;
        if (bAllModes == 0 && Filter != Mode)
            continue;
        if (Item->bEquipped && (Mode == 0 || Mode == 2))
            continue;
        if (Counter == SelectedIndex)
        {
            SelectedItem = Item;
            return Item;
        }
        ++Counter;
    }
    return SelectedItem;
}

void NnInventoryOpenDealer(int TradeMode, FNnInventoryResultFn ResultFn, void* ResultArg,
                           unsigned ShipPackage)
{
    // FUN_00477ab0 (cdecl).

    // 0x00477ab5..0x00477ac4: an NN_Dealer window is already up -> nothing.
    if (Win3DFindByName("NN_Dealer") != nullptr)
        return;

    // 0x00477aca..0x00477b12: the NN_Inventory window; made through the WinFactory (parent
    // FUN_0045a7a0(0x66), no payload) when there is none or it carries flag 4, and then shown
    // hidden (flags &= 0xfc). A failed create returns.
    Win3D* Window = Win3DFindByName("NN_Inventory");
    bool bCreated = false;
    if (Window == nullptr || (Window->Flags & 4) != 0)
    {
        Window = WinFactoryCreate("NN_Inventory", "NN_Inventory", NnMainButtonGetPanelButton(0x66),
                                  nullptr);
        if (Window == nullptr)
            return;
        Window->Flags = static_cast<unsigned char>(Window->Flags & 0xfc);
        bCreated = true;
    }

    // 0x00477b14..0x00477b33: FUN_0045a780(0x66, {-0.64, 0.33, -1.288}).
    float const PanelOrigin[3] = {NnFloatBits(0xbf23d70a), NnFloatBits(0x3ea8f5c3),
                                  NnFloatBits(0xbfa4dd2f)};
    NnMainButtonSetPanelOrigin(0x66, PanelOrigin);
    // 0x00477b38..0x00477b41: FUN_0045dcb0 on DAT_0066d3f0 (not null-tested).
    G_NnMainButton->SetInfoCardLayout();

    // 0x00477b46..0x00477b4d: slot 2 of the window.
    NnInventory* const Inventory = static_cast<NnInventory*>(Window->AsWinControl());

    // 0x00477b4f..0x00477b69: an NN_Dealer under the inventory's parent.
    Win3D* const DealerWindow =
        WinFactoryCreate("NN_Dealer", "NN_Dealer", Inventory->Parent, nullptr);
    if (DealerWindow != nullptr)
    {
        // 0x00477b6b..0x00477b89: hidden, linked as the inventory's +0x360, then the trade mode.
        DealerWindow->Flags = static_cast<unsigned char>(DealerWindow->Flags & 0xfc);
        Inventory->Dealer = static_cast<NnDealer*>(DealerWindow->AsWinControl());
        Inventory->SetProperty(0x47, static_cast<uintptr_t>(TradeMode), 0);
        // 0x00477b8f..0x00477ba0: FUN_0047b410 on the dealer (not null-tested), and on the
        // inventory when it was made here.
        Inventory->Dealer->BindControls();
        if (bCreated)
            Inventory->BindControls();
        // 0x00477ba5..0x00477bbb: the dealer's +0x974 = the inventory, the dealer hidden again.
        if (Inventory->Dealer != nullptr)
        {
            Inventory->Dealer->Inventory = Inventory;
            Inventory->Dealer->Flags = static_cast<unsigned char>(Inventory->Dealer->Flags & 0xfc);
        }
    }

    // 0x00477bbf..0x00477be2: open with the result callback, then the ship package.
    Inventory->SetProperty(0x48, reinterpret_cast<uintptr_t>(ResultArg),
                           reinterpret_cast<uintptr_t>(ResultFn));
    Inventory->SetProperty(0x13, static_cast<uintptr_t>(ShipPackage), 0);
}

void NnInventoryOpenShipDealer(int Index, FNnInventoryResultFn ResultFn, void* ResultArg)
{
    // FUN_00477bf0 (cdecl).

    // 0x00477c08..0x00477c4b: the current base's market (FUN_0043a510(DAT_0066873c) on
    // DAT_00668708), its category-3 goods collected into a local list (FUN_00437f70).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, 3);

    // 0x00477c50..0x00477c77 / 0x00477ce8: the Index-th good, else the first; an empty list
    // opens nothing.
    if (Goods.empty())
        return;
    FClientMarketGood const* Good = Goods.front();
    int Position = 0;
    for (std::list<FClientMarketGood const*>::const_iterator It = Goods.begin();
         It != Goods.end(); ++It)
    {
        if (Position == Index)
        {
            Good = *It;
            break;
        }
        ++Position;
    }

    // 0x00477c7a..0x00477c89: FUN_00477ab0(2, ResultFn, ResultArg, good id). The list is freed
    // on the way out (0x00477c95..0x00477cce).
    NnInventoryOpenDealer(2, ResultFn, ResultArg, Good->Info.GoodId);
}
