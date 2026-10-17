// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Scanner -- see nn_scanner.h for the class, its vftables and layout.

#include "nn_scanner.h"

#include "globals.h"         // IsClientServerTickPaused (FUN_0042d730)
#include "hud_target.h"      // HudIsMousePickBlocked (FUN_0045a740)
#include "menu_ui_init.h"    // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "nn_modal.h"        // NnMainButtonOnModalOpen, NnMainButtonHasActivePanel, G_bNnScreenUp
#include "player_behavior.h" // GetPlayerObjRW (FUN_0054baf0)
#include "space_letterbox.h" // IsLetterboxVisible (FUN_0041a3e0)
#include "win3d.h"           // Win3DFindByName (FUN_0059da10)
#include "win_factory.h"

// Retail WINFACTORY<NN_Scanner> DAT_00672294 "NN_Scanner" (static init LAB_004b6b30 -> ctor
// FUN_004b7200; atexit LAB_004b6b50 -> FUN_00579380; Create FUN_004b7270).
WINFACTORY<NnScanner> G_NnScannerFactory("NN_Scanner");

namespace
{
    // The gate FUN_004b6b60 (0x004b6b69..0x004b6bce) and Update (0x004b6e36..0x004b6e83) share,
    // in their order: a player ship, no "NeuroNetMissionMessage" / "NN_Trade" window, no
    // NN_MainButton panel, no NeuroNet screen, no held server tick, no letterbox.
    bool NnScannerMayStand()
    {
        if (GetPlayerObjRW() == nullptr)
            return false;
        if (Win3DFindByName("NeuroNetMissionMessage") != nullptr)
            return false;
        if (Win3DFindByName("NN_Trade") != nullptr)
            return false;
        if (NnMainButtonHasActivePanel())
            return false;
        if (G_bNnScreenUp)
            return false;
        if (IsClientServerTickPaused())
            return false;
        if (IsLetterboxVisible())
            return false;
        return true;
    }
}

NnScanner::NnScanner()
    : NnInventory()
{
    // FUN_004b7270 0x004b72af..0x004b7326. NN_Inventory's ctor (with its ResetFields) is the
    // only field initialisation; the list starts empty (0x004b72fb..0x004b7324 is the inlined
    // clear of a list that has no nodes yet).
    ScannedCargo.Items.clear();
}

NnScanner::~NnScanner()
{
    // FUN_004b6c80: 0x004b6c9f..0x004b6cfb free every node of +0x978 and its head -- the
    // member's destructor here; 0x004b6d07..0x004b6d11 FUN_00576010 on the +0x974 face and
    // 0x004b6d20 FUN_00477da0 are the base destructors, which C++ runs in that order.
}

bool NnScanner::Create(Win3D* NewParent, void* Data)
{
    // FUN_004b6d40 (RET 8).
    bool const bOk = NnInventory::Create(NewParent, Data);
    if (bOk)
    {
        // 0x004b6d61
        BindControls();

        // 0x004b6d66..0x004b6d7b: the title "PlayerTitle" gets ids 0xbcb.
        if (PlayerTitle != nullptr)
            PlayerTitle->SetProperty(0x1e, 0xbcb, 0);

        // 0x004b6d81..0x004b6df3: slot 42 (1, 1, 0) -- hide -- on each control that exists, in
        // this order; the full cargo bar pointer is dropped afterwards.
        if (JettisonButton != nullptr)
            JettisonButton->SetProperty(1, 1, 0);
        if (PlayerCash != nullptr)
            PlayerCash->SetProperty(1, 1, 0);
        if (Ship.Toggle != nullptr)
            Ship.Toggle->SetProperty(1, 1, 0);
        if (CargoLabel != nullptr)
            CargoLabel->SetProperty(1, 1, 0);
        if (Field4d0 != nullptr)
        {
            Field4d0->SetProperty(1, 1, 0);
            Field4d0 = nullptr;   // 0x004b6df9
        }

        // 0x004b6e03..0x004b6e09: FUN_0047f240(4, 1) -- the commodity filter.
        SetMode(4, true);
    }

    // 0x004b6e0e..0x004b6e16: FUN_005760d0(0xf) on the +0x974 face, whatever Create returned.
    RegisterEvent(0xf);
    return bOk;
}

void NnScanner::Update()
{
    // LAB_004b6e30.
    //
    // 0x004b6e36..0x004b6e92: the scan cannot stand -> slot 23 (0xef02, 0, 0), a virtual call
    // that lands in this class's Slot23 (sound 0xe and Close, which deletes this).
    if (!NnScannerMayStand())
    {
        Slot23(0xef02, 0, 0);
        return;
    }

    // 0x004b6e9a..0x004b6ebc: every child -- bit0 ? slot 11 : slot 12, the next sibling read
    // before the call.
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

    // 0x004b6ebe..0x004b6f2c: one FUN_0047c740 row per scanned item, in list order:
    // (&Counter, ArchId, FUN_0047e440(item), Count, 0, 0, 0, 0, 0, 1, 0, 0, 0). The first item
    // that answers true (the selected entry) is remembered.
    int Counter = 0;
    EquipDesc const* Selected = nullptr;
    for (std::list<EquipDesc>::const_iterator It = ScannedCargo.Items.begin();
         It != ScannedCargo.Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        bool const bIsSelected = FillItemRow(&Counter, Desc.ArchId, GetItemFilterMode(&Desc),
                                             Desc.Count, 0, 0, 0, 0, 0, 1, 0, 0, 0);
        if (Selected == nullptr && bIsSelected)
            Selected = &Desc;
    }

    // 0x004b6f2d..0x004b6f4f: FUN_0047ba90 -- +0x4e0 set: the scanned ship (+0x348); otherwise
    // the selected item's archetype, or 0.
    unsigned InfoArchId = 0;
    if (Field4e0 != 0)
        InfoArchId = Ship.ShipArchId;
    else if (Selected != nullptr)
        InfoArchId = Selected->ArchId;
    ShowInfoCard(InfoArchId, 0);

    // 0x004b6f54..0x004b6f5b: FUN_0047f3e0(Counter).
    UpdateRowVisibility(Counter);

    // 0x004b6f60..0x004b6f7d: `(+0x4e1 ? 0 : +0x4e2)` is pushed to 0x0053e4a0 on +0x330 -- a
    // bare `RET 4` (ICF-named IDPMsgHandler::OnAddHost). Nothing happens.

    // 0x004b6f82..0x004b7014: the list length changed.
    if (Counter != Field3c8)
    {
        SelectedIndex = -1;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Counter - 1));

        // 0x004b6fb9..0x004b7006 (unsigned compares): pull the first row back so the page ends
        // at the list's end.
        if (FirstRow != 0
            && static_cast<unsigned>(FirstRow + kNnInventoryRows) > static_cast<unsigned>(Counter))
        {
            if (static_cast<unsigned>(Counter) > static_cast<unsigned>(kNnInventoryRows))
                FirstRow = Counter - kNnInventoryRows;
            else
                FirstRow = 0;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow * 10), 0);
        }

        // 0x004b700a..0x004b7014: remember the length, then slot 11 again (this function).
        Field3c8 = Counter;
        Update();
    }

    // 0x004b7017..0x004b702d: clear bits 0..1 of the five damage bars (no null test).
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
        ItemDamage[Row]->Flags = static_cast<unsigned char>(ItemDamage[Row]->Flags & 0xfc);

    // 0x004b702f..0x004b7039
    if (SelectedIndex == -1)
        Field4e0 = 1;
}

bool NnScanner::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004b7180 (RET 0xc).
    if (Msg == 0xef02)
    {
        PlayOrRewindUiSoundSlot(0xe);
        Close();
        return true;
    }
    return NnInventory::Slot23(Msg, A, B);
}

intptr_t NnScanner::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004b7050 (RET 0xc).
    if (Msg == 0x26)
    {
        // 0x004b714e..0x004b7177: the close button -> slot 23 (0xef02, 0, 0), return 1.
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            Slot23(0xef02, 0, 0);
            return 1;
        }
    }
    else if (Msg == 0x49)
    {
        // 0x004b7090..0x004b7146: +0x348 = B; empty the list, then (when A is another list)
        // copy A's items into it; slot 11. The clear runs first, so A == &ScannedCargo leaves
        // the list empty, exactly as the listing does.
        Ship.ShipArchId = static_cast<unsigned>(B);
        ScannedCargo.Items.clear();
        EquipDescList const* const Source = reinterpret_cast<EquipDescList const*>(A);
        if (Source != &ScannedCargo)
            ScannedCargo.Items = Source->Items;
        Update();
    }
    else if (Msg == 0x4a)
    {
        // 0x004b706f
        Flags = static_cast<unsigned char>(Flags | 3);
    }

    // 0x004b7073..0x004b708d: FUN_00479d10.
    return NnInventory::SetProperty(Msg, A, B);
}

bool NnScanner::Slot46()
{
    // FUN_004b6c50: MOV AL,1 / RET.
    return true;
}

bool NnScanner::OnEvent(unsigned MsgId)
{
    // FUN_004b71c0 (RET 4).
    if (HudIsMousePickBlocked())
        return false;
    if (MsgId != 0xf)
        return false;
    PlayOrRewindUiSoundSlot(0xe);
    // 0x004b71e9: ECX = this - 0x974, FUN_0047ad20.
    Close();
    return true;
}

void OpenNnScanner(EquipDescList const* Cargo, unsigned ShipArchId)
{
    // FUN_004b6b60 (cdecl).
    // 0x004b6b64: FUN_0045a460, before any test.
    NnMainButtonOnModalOpen();

    // 0x004b6b69..0x004b6bce
    if (!NnScannerMayStand())
        return;

    // 0x004b6bd0..0x004b6bf5: FUN_0059db20("NN_Scanner", "NN_Scanner", 0, 0), slot 2.
    Win3D* const Created = WinFactoryCreate("NN_Scanner", "NN_Scanner", nullptr, nullptr);
    if (Created == nullptr)
        return;
    WinControl* const Window = Created->AsWinControl();
    if (Window == nullptr)
        return;

    // 0x004b6bf7..0x004b6c18: slot 5 {0, 0x3d9ba5e3 (0.076), 0xbfa4dd2f (-1.288)}.
    float const Position[3] = {0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f)};
    Window->SetPosition(Position);

    // 0x004b6c1b..0x004b6c3b: slot 42 (0x49, Cargo, ShipArchId), then (0x4a, 0, 0).
    Window->SetProperty(0x49, reinterpret_cast<uintptr_t>(Cargo), ShipArchId);
    Window->SetProperty(0x4a, 0, 0);

    // 0x004b6c41..0x004b6c43
    PlayOrRewindUiSoundSlot(0xd);
}
