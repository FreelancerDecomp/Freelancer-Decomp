// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Dealer -- see nn_dealer.h for the class, its vftable and layout.

#include "nn_dealer.h"

#include "client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510), the market
                                    // queries FUN_00438060 / 00438160 / 004381c0 / 00438220
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830),
                                    // GetPlayerBaseIdRetail (DAT_00673358 via FUN_004c4900),
                                    // GetPlayerSpawnSystemIdRetail (FUN_004c4670)
#include "hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "hud_weapon_groups.h"      // G_WeaponGroups (DAT_00674c0c)
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "menu_ui_widgets.h"        // HudMessageQueueRequestRankRefresh (FUN_004c55f0)
#include "menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nav_bar.h"                // NavBarOpenButton (FUN_00442200), NavBarRefreshRepairTab (FUN_004427e0)
#include "nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "nn_modal.h"               // NnFloatBits
#include "nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "nn_text_control.h"        // NnTextControl::GetFontString (Font::String +0x334)
#include "nn_ui_pulse.h"            // G_NnPaletteArgb_679bac (DAT_00679bac)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "ship_view_control.h"
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DFindByName (FUN_0059da10)
#include "win_factory.h"
#include "wire_controls.h"          // WireStaticControl::SoundSlot (+0x380)

#include "common_archetype.h"       // Archetype::Gun / Munition (the equipment sort)
#include "common_goods.h"           // Good2Arch / Arch2Good / GoodList
#include "common_ini.h"             // StringAlloc
#include "common_reputation.h"
#include "common_server.h"
#include "common_misc.h"            // SinglePlayer
#include "common_tstring.h"
#include "common_universe.h"

#include <list>
#include <stdio.h>
#include <vector>
#include <wchar.h>
#include <windows.h> // CharUpperW (FUN_005b74d9)

// Retail WINFACTORY<NN_Dealer> "NN_Dealer" (static init FUN_004857f0, vftable 0x005d1d28,
// Create FUN_00485860).
WINFACTORY<NnDealer> G_NnDealerFactory("NN_Dealer");

NnDealer::NnDealer()
    : NnInventory()
{
    // FUN_0047fbe0. 0047fc00..0047fc6c is NN_Inventory's ctor inlined; the dealer vptrs go in
    // and FUN_00477ee0 runs a second time.
    ResetFields();

    // 0047fc8b..0047fcc6. The control pointers +0x994..+0x9f0, +0x97c and +0x9fc are not
    // written here in retail (operator_new memory); the port starts them at zero.
    Field97c = 0;
    WelcomeText = nullptr;
    WelcomeLine = nullptr;
    WelcomeHelp = nullptr;
    BuyText = nullptr;
    BuyArrow = nullptr;
    SellText = nullptr;
    SellArrow = nullptr;
    BuySellTradeButton = nullptr;
    QuantityPrompt = nullptr;
    QuantitySlider = nullptr;
    QuantityText = nullptr;
    QuantityPrice = nullptr;
    QuantityTotal = nullptr;
    QuantityTotalText = nullptr;
    QuantityEdit = nullptr;
    TransferItemQuantity = nullptr;
    ErrorHelp = nullptr;
    PlusMinus = nullptr;
    Field9dc = 0;
    ContinueButton = nullptr;
    ContinueText = nullptr;
    ShipBuyProblem = nullptr;
    BackToShipDealer = nullptr;
    ShipDealerShipPrice = nullptr;
    Field9fc = 0;

    TradeMode = 1;      // +0x978
    bDealerMode = 1;    // +0x35c
    Inventory = nullptr; // +0x974
    Field980 = 1;
    Field984 = 0;
    Field988 = 1;
    Field9f4 = 1;
    Field98c = 0;
    Field9f8 = 0;
    Field4e8 = 0;

    // 0047fccc..0047fcd1
    BaseFeeling = GetBaseFactionFeeling(); // FUN_00483df0

    // 0047fcd7..0047fd12: single player asks for the rank level (FUN_004c55f0), multiplayer
    // for the player stats (IServer slot 73, +0x124) with a zeroed 4-byte block.
    if (SinglePlayer())
    {
        HudMessageQueueRequestRankRefresh();
        return;
    }
    unsigned char Data[4] = {0, 0, 0, 0};
    G_Server->RequestPlayerStats(GetPlayerConnectionId(), Data, 4);
}

// The TextControl idiom retail inlines after several creations (0x00481339..0x0048134d and the
// same shape at 0x004814ed / 0x00481d37 / 0x00481ffa): the label's Font::String (+0x334) goes
// multi-line and drops its layout, unless it already is. The factory's result is not tested.
static void NnDealerMakeMultiLine(WinControl* Control)
{
    FFontString* const Text = static_cast<NnTextControl*>(Control)->GetFontString();
    if (Text->bMultiLine != 1)
    {
        Text->bMultiLine = 1; // +0x30
        Text->LayoutGen = 0;  // +0x28
    }
}

void NnDealer::Slot45()
{
    // FUN_00480d10 -- every position is absolute (the dealer half of the screen); where x87 keeps
    // an intermediate at extended precision across two operations, the port carries it in
    // double. The results of the row controls are not kept: NN_Inventory's BindControls finds
    // them by name.
    float PriceColumn = 0.025f; // [ESP+0x2c] (0x3ccccccd)
    if (TradeMode == 1)         // +0x978
        PriceColumn = 0.0f;

    // 00480d3a..00480d7f: "PlayerTitle"; the result is not kept.
    {
        float const TitlePosition[3] = {NnFloatBits(0x3f870a3e), -0.039f, 0.0f}; // 1.055
        CreateTextControl("PlayerTitle", "PageHeader", TitlePosition, nullptr, 0.394f, 0.03f,
                          1.65f, 0x2143, 1, 0, 0); // FUN_005a0e30
    }

    // 00480d84..00480d9a: the price column's x.
    float const PriceX = PriceColumn + NnFloatBits(0x3f908313); // DAT_005d1cec, 1.129 (not 1.129f)

    // 00480da0..00481161: the five item rows. Names count from 1, the row offset from 0.
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        int const Number = Row + 1;

        // 00480da0..00480e27: "ItemButton%d". The product is exact in double; x87 subtracts the
        // double 0.131 (DAT_005d1538) from it before the float stores.
        sprintf(Name, "ItemButton%d", Number);
        double const RowOffset = static_cast<double>(Row) * static_cast<double>(-0.068f); // DAT_005d1540
        float const RowY = static_cast<float>(RowOffset);            // [ESP+0x24]
        float const ButtonY = static_cast<float>(RowOffset - 0.131); // [ESP+0x3c] / [ESP+0x70]
        {
            float const ButtonPosition[3] = {0.903f, ButtonY, 0.0f};
            float const ButtonHalfExtents[3] = {0.035f, 0.0335f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ButtonPosition, ButtonHalfExtents, 0,
                                    0, 0, 0, 0); // FUN_005a0d00
        }

        // 00480e2c..00480ea7: "ItemView0%d".
        sprintf(Name, "ItemView0%d", Number);
        {
            float const ViewPosition[3] = {1.084f, ButtonY, 0.0f};
            float const ViewHalfExtents[3] = {0.138f, 0.034f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ViewPosition, ViewHalfExtents, 0, 0,
                                    0, 0, 0); // FUN_005a0d00
        }

        // 00480eac..00480f5d: "ItemMesh%d" -- a static mesh placeholder.
        sprintf(Name, "ItemMesh%d", Number);
        {
            float const MeshPosition[3] = {
                0.903f,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.131f)), // DAT_005d150c
                0.0f};
            float const MeshHalfExtents[3] = {0.036f, 0.041f, 0.0f};
            float const MeshVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                MeshPosition, nullptr, MeshHalfExtents, 0, 0, 0, 1, 1, 1, 1, 0x50,
                                0, 0, MeshVector338); // FUN_005a0db0
        }

        // 00480f62..00480fcc: "ItemName0%d".
        sprintf(Name, "ItemName0%d", Number);
        {
            float const NamePosition[3] = {
                1.092f,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.118f)), // DAT_005d14c8
                0.0f};
            CreateTextControl(Name, "ItemName", NamePosition, nullptr, 0.27f, 0.04f, 1.5f, 0, 0, 0,
                              0); // FUN_005a0e30
        }

        // 00480fd1..00481042: "ItemQuantity0%d". Its y is kept for the "good deal" marker
        // ([ESP+0x3c]).
        sprintf(Name, "ItemQuantity0%d", Number);
        float const QuantityY =
            static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.148f)); // DAT_005d14b0
        {
            float const QuantityPosition[3] = {1.01f, QuantityY, 0.0f};
            CreateTextControl(Name, "ItemName", QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0, 0,
                              0xbcf, 0); // FUN_005a0e30
        }

        // 00481047..004810b7: "ItemPrice0%d".
        sprintf(Name, "ItemPrice0%d", Number);
        {
            float const PricePosition[3] = {
                PriceX,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.152f)), // DAT_005d1414
                0.0f};
            CreateTextControl(Name, "ItemName", PricePosition, nullptr, 0.12f, 0.04f, 1.5f, 0xbd4, 2,
                              0xbd4, 0); // FUN_005a0e30
        }

        // 004810bc..00481155: "ItemGoodDeal0%d" -- the commodity dealer (trade mode 1) only.
        if (TradeMode == 1)
        {
            sprintf(Name, "ItemGoodDeal0%d", Number);
            float const DealPosition[3] = {1.201f, QuantityY, 0.0001f}; // 0x38d1b717
            float const DealHalfExtents[3] = {0.012f, 0.012f, 0.0f};
            float const DealVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
                                DealPosition, nullptr, DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                0, DealVector338); // FUN_005a0db0
        }
    }

    // 00481167..0048126e: the info card (FUN_005a10e0). "InfoCard_Info" is not kept; the three
    // panes start hidden (flags bits 0..1 cleared). FUN_005a10e0's result is not tested.
    {
        float const InfoOffset[3] = {0.09f, -0.537f, 0.0f};
        CreateRichTextWin("InfoCard_Info", 1.09f, 0.255f, InfoOffset, true);
        InfoCardFacts = CreateRichTextWin("InfoCard_Facts", 0.5f, 0.265f, InfoOffset, true); // +0x398
        InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
        float const StatsOffset[3] = {0.69f, -0.537f, 0.0f};
        InfoCardStats = CreateRichTextWin("InfoCard_Stats", 0.29f, 0.265f, StatsOffset, true); // +0x39c
        InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
        float const ValuesOffset[3] = {0.99f, -0.537f, 0.0f};
        InfoCardValues = CreateRichTextWin("InfoCard_Values", 0.17f, 0.265f, ValuesOffset, true); // +0x3a0
        InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
    }

    // 00481271..0048129a: "Inventory_Scroll" (FUN_005a17e0); the result is not kept.
    {
        float const ScrollPosition[3] = {1.2349f, -0.099f, 0.0f};
        CreateUIScroll("Inventory_Scroll", ScrollPosition, 0.019f, 0.335f, 0, 5, 5, 0);
    }

    // 0048129f..00481309: "Inventory_Close" (FUN_005a0c90) -> +0x4d8.
    {
        float const ClosePosition[3] = {1.251f, -0.04f, 0.0f};
        float const CloseHalfExtents[3] = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("Inventory_Close", "Interface\\NeuroNet\\Inventory\\x.3db",
                                          nullptr, "ui_select_item", nullptr, ClosePosition,
                                          nullptr, CloseHalfExtents, 0, 1, 0, 0, 0x409, 0, 0);
    }

    // 0048130f..0048134d: "WelcomeText" -> +0x994, multi-line.
    {
        float const WelcomePosition[3] = {0.645f, -0.05f, 0.0f};
        WelcomeText = CreateTextControl("WelcomeText", "SubHeader", WelcomePosition, nullptr,
                                        0.37f, 0.06f, 1.8f, 0, 1, 0, 0);
        NnDealerMakeMultiLine(WelcomeText);
    }

    // 00481350..0048137d: the ship dealer's welcome block sits higher.
    float HelpY = -0.2f;   // [ESP+0x24] (0xbe4ccccd)
    float LineY = -0.143f; // DAT_005d1cac
    if (TradeMode == 2)
    {
        HelpY = -0.131f;   // 0xbe0624dd
        LineY = -0.072f;   // DAT_005d1ca8
    }

    // 0048137d..004813f2: "WelcomeLine" -> +0x998.
    {
        float const LinePosition[3] = {0.643f, LineY, 0.0f};
        float const LineHalfExtents[3] = {0.0f, 0.0f, 0.0f};
        float const LineVector338[3] = {0.0f, 0.0f, 0.0f};
        WelcomeLine = CreateStaticControl("WelcomeLine",
                                          "Interface\\NeuroNet\\Inventory\\partitionline.3db",
                                          LinePosition, nullptr, LineHalfExtents, 0, 0, 0, 1, 0, 0,
                                          1, 0, 0, 0, LineVector338); // FUN_005a0db0
    }

    // 004813f8..004814a6: the dealer's title into [ESP+0xc0] (64 wchars) and the help ids.
    // FUN_004347e0 failures leave an empty title.
    wchar_t Title[0x40]; // [ESP+0xc0]
    Title[0] = 0;
    unsigned HelpIds = 0;
    if (TradeMode == 1)
    {
        if (GetIdsStringWWithRawFallback(0x48b, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = 0x478;
    }
    else if (TradeMode == 2)
    {
        if (GetIdsStringWWithRawFallback(0x486, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = GetPlayerShipArchetypeIdRetail() != 0 ? 0x489 : 0x499; // FUN_004c3e10
    }
    else if (TradeMode == 3)
    {
        if (GetIdsStringWWithRawFallback(0x48c, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = 0x47d;
    }

    // 004814ab..00481501: "WelcomeHelp" -> +0x99c, multi-line.
    {
        float const HelpPosition[3] = {0.657f, HelpY, 0.0f};
        WelcomeHelp = CreateTextControl("WelcomeHelp", "Normal", HelpPosition, nullptr, 0.35f,
                                        0.06f, 1.4f, HelpIds, 0, 0, 0);
        NnDealerMakeMultiLine(WelcomeHelp);
    }

    // 00481504..004815b6: FUN_0043bbe0 on DAT_00668708 -- the strid_name of the base the player
    // is docked at; zero skips the block (0x0048150e). With one, WelcomeText's text (property
    // 0x1d, vt+0xa8) is the title alone for the ship dealer (TradeMode 2, EBP); otherwise the
    // base name (64 wchars, empty when FUN_004347e0 fails) and the title go through ids 0x477
    // (63 wchars, the result not tested) with swprintf into [ESP+0x344] (256 wchars), upper-cased
    // by FUN_005b74d9 (the delay-loaded CharUpperW).
    {
        int const BaseIds = GetClientCurrentBaseStridName();
        if (BaseIds != 0)
        {
            wchar_t const* WelcomeString = Title;
            wchar_t BaseName[0x40];      // [ESP+0x244]
            wchar_t Format[0x3f];        // [ESP+0x2c4]
            wchar_t Welcome[0x100];      // [ESP+0x344]
            if (TradeMode != 2)
            {
                if (GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseIds), BaseName, 0x40) ==
                    0)
                    BaseName[0] = 0;
                GetIdsStringWWithRawFallback(0x477, Format, 0x3f);
                _swprintf(Welcome, Format, BaseName, Title);
                CharUpperW(Welcome);
                WelcomeString = Welcome;
            }
            WelcomeText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(WelcomeString), 0);
        }
    }

    // 004815bc..004815fd: "BuyText" -> +0x9a0.
    float const ZeroVector[3] = {0.0f, 0.0f, 0.0f};
    {
        float const BuyTextPosition[3] = {0.683f, -0.081f, 0.0f};
        BuyText = CreateTextControl("BuyText", "ButtonText", BuyTextPosition, nullptr, 0.164f,
                                    0.06f, 2.0f, 0xbc8, 1, 0, 0);
    }

    // 00481602..00481676: "BuyArrow" -> +0x9a4.
    {
        float const ArrowPosition[3] = {0.563f, -0.074f, 0.0f};
        BuyArrow = CreateStaticControl("BuyArrow", "Interface\\NeuroNet\\Inventory\\INV_buyarrow.3db",
                                       ArrowPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0,
                                       0, 0, ZeroVector); // FUN_005a0db0
    }

    // 0048167b..004816c2: "SellText" -> +0x9a8.
    {
        float const SellTextPosition[3] = {0.601f, -0.081f, 0.0f};
        SellText = CreateTextControl("SellText", "ButtonText", SellTextPosition, nullptr, 0.164f,
                                     0.06f, 2.0f, 0xbc9, 1, 0, 0);
    }

    // 004816c7..0048173b: "SellArrow" -> +0x9ac.
    {
        float const ArrowPosition[3] = {0.726f, -0.073f, 0.0f};
        SellArrow = CreateStaticControl("SellArrow",
                                        "Interface\\NeuroNet\\Inventory\\INV_sellarrow.3db",
                                        ArrowPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0,
                                        0, 0, ZeroVector); // FUN_005a0db0
    }

    // 00481740..00481795: "BuySellTradeButton" -> +0x9b0.
    {
        float const ButtonPosition[3] = {0.645f, -0.073f, 0.0f};
        float const ButtonHalfExtents[3] = {0.134f, 0.026f, 0.0f};
        BuySellTradeButton = CreateWireButtonControl("BuySellTradeButton", "ui_item_select",
                                                     ButtonPosition, ButtonHalfExtents, 0, 0,
                                                     0xbc8, 0, 0); // FUN_005a0d00
    }

    // 0048179a..00481a8e: the partner inventory's transfer panel, kept on this window
    // (+0x364..+0x37c); NN_Inventory's SetProperty 0x48 copies them to the partner.
    {
        float const ButtonPosition[3] = {0.502f, -0.171f, 0.0f};
        float const ButtonHalfExtents[3] = {0.035f, 0.0335f, 0.0f};
        TransferItemButton = CreateWireStaticControl("TransferItemButton", ButtonPosition,
                                                     ButtonHalfExtents, 0, 0, 0, 0); // FUN_005a0d60
    }
    {
        float const ViewPosition[3] = {0.683f, -0.171f, 0.0f};
        float const ViewHalfExtents[3] = {0.138f, 0.034f, 0.0f};
        TransferItemView = CreateWireStaticControl("TransferItemView", ViewPosition,
                                                   ViewHalfExtents, 0, 0, 0, 0); // FUN_005a0d60
    }
    {
        float const MeshPosition[3] = {0.502f, -0.171f, 0.0f};
        float const MeshHalfExtents[3] = {0.036f, 0.041f, 0.0f};
        TransferItemMesh = CreateStaticControl("TransferItemMesh",
                                               "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                               MeshPosition, nullptr, MeshHalfExtents, 0, 0, 0, 1,
                                               1, 1, 1, 0x50, 0, 0, ZeroVector); // FUN_005a0db0
    }
    {
        float const NamePosition[3] = {0.691f, NnFloatBits(0xbe21cac0), 0.0f}; // -0.158
        TransferItemName = CreateTextControl("TransferItemName", "ItemName", NamePosition, nullptr,
                                             0.27f, 0.04f, 1.5f, 0, 0, 0, 0);
    }
    {
        float const QuantityPosition[3] = {NnFloatBits(0x3f1be76c), -0.188f, 0.0f}; // 0.609
        TransferItemQuantity = CreateTextControl("TransferItemQuantity", "ItemName",
                                                 QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0,
                                                 0, 0xbcf, 0); // +0x9d0
    }
    {
        // 0048194e..004819b9: a horizontal cargo bar with the empty mesh path DAT_005c865c and the
        // colour DAT_00679bac for both states, then property 0x19 (0x10000, 0).
        float const DamagePosition[3] = {0.502f, NnFloatBits(0xbe4ac084), 0.001f}; // -0.198
        float const DamageHalfExtents[3] = {0.03f, 0.003f, 0.0f};
        TransferItemDamage = CreateCargoBarControl("TransferItemDamage", "", DamagePosition,
                                                   nullptr, DamageHalfExtents, 1, 0x43c,
                                                   G_NnPaletteArgb_679bac,
                                                   G_NnPaletteArgb_679bac); // FUN_005a1080
        TransferItemDamage->SetProperty(0x19, 0x10000, 0);
    }
    {
        // 004819bf..00481a0a: x = the price column + 0.728 (the float at 0x005d1b40).
        float const PricePosition[3] = {PriceColumn + NnFloatBits(0x3f3a5e36), -0.192f, 0.0f};
        TransferItemPrice = CreateTextControl("TransferItemPrice", "ItemName", PricePosition,
                                              nullptr, 0.12f, 0.04f, 1.5f, 0xbd4, 2, 0xbd4, 0);
    }
    {
        float const DealPosition[3] = {0.8f, -0.188f, 0.001f};
        float const DealHalfExtents[3] = {0.012f, 0.012f, 0.0f};
        TransferItemGoodDeal = CreateStaticControl(
            "TransferItemGoodDeal", "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
            DealPosition, nullptr, DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
            ZeroVector); // FUN_005a0db0
        TransferItemGoodDeal->HitPriority = 100; // +0x74, no null test
    }

    // 00481a88..00481ae3: the quantity panel's rows; the ship dealer's sit higher.
    float PromptY = NnFloatBits(0xbe851eb8); // DAT_005d1b10, -0.26
    float TotalY = -0.461f;                  // [ESP+0x28]
    float TextY = -0.359f;                   // [ESP+0x24]
    float SliderY = -0.28f;                  // [ESP+0x3c]
    float PriceY = -0.41f;                   // [ESP+0x2c]
    if (TradeMode == 2)
    {
        TotalY = -0.424f;
        PromptY = NnFloatBits(0xbe7ced91); // DAT_005d1b0c, -0.247
        TextY = -0.337f;
        SliderY = -0.263f;
        PriceY = -0.382f;
    }

    // 00481aeb..00481b28: "QuantityPrompt" -> +0x9b4.
    {
        float const PromptPosition[3] = {0.648f, PromptY, 0.0f};
        QuantityPrompt = CreateTextControl("QuantityPrompt", "Normal", PromptPosition, nullptr,
                                           0.384f, 0.06f, 1.8f, 0x479, 1, 0, 0);
    }

    // 00481b2d..00481ba8: "QuantitySlider" -> +0x9b8 (no null test before the two stores).
    {
        float const SliderPosition[3] = {0.494f, SliderY, 0.0f};
        QuantitySlider = CreateUIScroll("QuantitySlider", SliderPosition, 0.314f, 0.019f, 0, 5, 1,
                                        1);
        QuantitySlider->SetStepMode(true);  // +0x3b4 = 1
        QuantitySlider->SetPageField(1.0f); // +0x3cc = 1.0f
    }

    // 00481b6b..00481c5a: "QuantityText" +0x9bc, "QuantityPrice" +0x9c0, "QuantityTotal" +0x9c4.
    {
        float const TextPosition[3] = {0.706f, TextY, 0.0f};
        QuantityText = CreateTextControl("QuantityText", "Normal", TextPosition, nullptr, 0.384f,
                                         0.06f, 1.6f, 0x47a, 0, 0, 0);
        float const PricePosition[3] = {0.706f, PriceY, 0.0f};
        QuantityPrice = CreateTextControl("QuantityPrice", "Normal", PricePosition, nullptr,
                                          0.384f, 0.06f, 1.6f, 0x47b, 0, 0, 0);
        float const TotalPosition[3] = {0.706f, TotalY, 0.0f};
        QuantityTotal = CreateTextControl("QuantityTotal", "Normal", TotalPosition, nullptr,
                                          0.384f, 0.06f, 1.6f, 0x47c, 0, 0, 0);
    }

    // 00481c5f..00481cea: "TransferPriceText" (+0x380) and "QuantityTotalText" (+0x9c8).
    {
        float const PricePosition[3] = {0.864f, PriceY, 0.0f};
        TransferPriceText = CreateTextControl("TransferPriceText", "Normal", PricePosition,
                                              nullptr, 0.384f, 0.06f, 1.6f, 0, 0, 0, 0);
        float const TotalPosition[3] = {0.864f, TotalY, 0.0f};
        QuantityTotalText = CreateTextControl("QuantityTotalText", "Normal", TotalPosition,
                                              nullptr, 0.384f, 0.06f, 1.6f, 0, 0, 0, 0);
    }

    // 00481cef..00481d70: "ErrorHelp" -> +0x9d4, multi-line, then property 0x33 with the colour
    // {0xff, 0xff, 0xff, 0xff} ([ESP+0x40]) and 1.
    unsigned char White[4] = {0xff, 0xff, 0xff, 0xff}; // [ESP+0x40]
    {
        float const ErrorPosition[3] = {0.645f, -0.072f, 0.0f};
        ErrorHelp = CreateTextControl("ErrorHelp", "Normal", ErrorPosition, nullptr, 0.352f,
                                      0.06f, 1.4f, 0, 0, 0, 0);
        NnDealerMakeMultiLine(ErrorHelp);
        ErrorHelp->SetProperty(0x33, reinterpret_cast<uintptr_t>(White), 1);
    }

    // 00481d76..00481db7: "PlusMinus" -> +0x9d8.
    {
        float const PlusMinusPosition[3] = {0.677f, TotalY, 0.0f};
        PlusMinus = CreateTextControl("PlusMinus", "Normal", PlusMinusPosition, nullptr, 0.05f,
                                      0.06f, 1.8f, 0x481, 0, 0, 0);
    }

    // 00481dbc..00481e24: "QuantityEdit" (FUN_005a0e90) -> +0x9cc, then properties 0x43 (1, 8)
    // and 0x44 (1, 0).
    {
        float const EditPosition[3] = {0.864f, TextY, 0.0f};
        QuantityEdit = CreateEditControl("QuantityEdit", "Normal", EditPosition, nullptr, 0.384f,
                                         0.06f, 1.6f, 0, 0, 0);
        QuantityEdit->SetProperty(0x43, 1, 8);
        QuantityEdit->SetProperty(0x44, 1, 0);
    }

    // 00481e2a..00481e42
    ContinueButton = nullptr;   // +0x9e0
    ContinueText = nullptr;     // +0x9e4
    ShipBuyProblem = nullptr;   // +0x9e8
    BackToShipDealer = nullptr; // +0x9ec
    if (TradeMode != 2)
        return;

    // 00481e4f..00481ef5: the ship dealer's continue button.
    Field98c = 1;
    {
        float const ButtonPosition[3] = {0.645f, -0.473f, 0.0f};
        float const ButtonHalfExtents[3] = {0.134f, 0.026f, 0.0f};
        ContinueButton = CreateWireButtonControl("ContinueButton", "ui_item_select",
                                                 ButtonPosition, ButtonHalfExtents, 0, 0, 0x32c, 0,
                                                 0); // FUN_005a0d00
        float const TextPosition[3] = {0.643f, -0.4816f, 0.0f};
        ContinueText = CreateTextControl("ContinueText", "ButtonText", TextPosition, nullptr,
                                         0.265f, 0.06f, 1.8f, 0x48e, 1, 0, 0);
    }

    // 00481efb..00481f02: the rest only when the player owns a ship.
    if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
        return;

    // 00481f08..00481f61: "BackToShipDealer" (FUN_005a0c90) -> +0x9ec.
    {
        float const BackPosition[3] = {1.21f, -0.04f, 0.0f};
        float const BackHalfExtents[3] = {0.02f, 0.02f, 0.0f};
        BackToShipDealer = CreateButtonControl(
            "BackToShipDealer", "Interface\\neuronet\\inventory\\INV_goback.3db", nullptr,
            "ui_select_item", nullptr, BackPosition, nullptr, BackHalfExtents, 0, 1, 0, 0, 0x39f, 0,
            0);
    }

    // 00481f66..00481fad: "ShipDealerShipPrice" -> +0x9f0.
    {
        float const PricePosition[3] = {1.051f, -0.072f, 0.0f};
        ShipDealerShipPrice = CreateTextControl("ShipDealerShipPrice", "SubHeader", PricePosition,
                                                nullptr, 0.403f, 0.03f, 1.4f, 0x3c9, 1, 0, 0);
    }

    // 00481fb2..00482021: "ShipBuyProblem" -> +0x9e8, multi-line, property 0x33 with the same
    // white colour.
    {
        float const ProblemPosition[3] = {0.474f, -0.431f, 0.0f};
        ShipBuyProblem = CreateTextControl("ShipBuyProblem", "SubHeader", ProblemPosition, nullptr,
                                           0.36f, 0.06f, 1.6f, 0, 0, 0, 1);
        NnDealerMakeMultiLine(ShipBuyProblem);
        ShipBuyProblem->SetProperty(0x33, reinterpret_cast<uintptr_t>(White), 1);
    }
}

void NnDealer::Update()
{
    // FUN_0047fef0 (slot 11). 0047fef7..0047ff46: the partner's visibility bit (flags bit 1) and,
    // while it has none, this window's three info card panes.
    if (Inventory != nullptr)
    {
        Flags = static_cast<unsigned char>((Flags & ~2u) | (Inventory->Flags & 2u));
        if (Inventory->InfoCardFacts == nullptr)
        {
            Inventory->InfoCardFacts = InfoCardFacts;   // +0x398
            Inventory->InfoCardStats = InfoCardStats;   // +0x39c
            Inventory->InfoCardValues = InfoCardValues; // +0x3a0
        }
    }

    // 0047ff4c..0047ff6c: every child -- bit0 ? slot 11 : slot 12, the next sibling read before
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

    // 0047ff6e..0047ff72: the rest only while active.
    if ((Flags & kWin3DActive) == 0)
        return;

    // 0047ff78..0047ffef: the list.
    if (TradeMode == 1 || TradeMode == 3)
    {
        RefreshMarketList(); // FUN_00482a90
    }
    else if (TradeMode == 2)
    {
        RefreshItemList(&Ship.Loadout, 0, 0); // FUN_0047d2f0
        EquipDesc* const Item = GetSelectedItem();
        if (Item != nullptr)
            ShowInfoCard(Item->ArchId, 0); // FUN_0047ba90
        Field4e2 = 0;
    }

    // 0047ff8e..0047ffa3
    ErrorHelp->SetProperty(1, 1, 0);
    Field4e2 = 0;

    // 0047ffa9..00480045: a selection here ([ESP+0x12]) and on the partner ([ESP+0x13]); a
    // window in dealer mode (+0x35c) tests its row, otherwise its item. The partner is not
    // null-tested. The ship purchase step (+0x98c) counts as no selection.
    bool const bOwnSelection =
        bDealerMode != 0 ? SelectedIndex != -1 : GetSelectedItem() != nullptr;
    bool bPartnerSelection = Inventory->bDealerMode != 0 ? Inventory->SelectedIndex != -1
                                                         : Inventory->GetSelectedItem() != nullptr;
    bool bAnySelection = bOwnSelection || bPartnerSelection; // BL
    if (Field98c != 0)
    {
        bAnySelection = false;
        bPartnerSelection = false;
    }

    // 00480049..0048018d: property 1 (hide) on the welcome block with a selection, on the buy
    // side without one here, on the sell side without one on the partner, on the transfer panel
    // without any.
    uintptr_t const HideWelcome = bAnySelection ? 1 : 0;
    uintptr_t const HideBuy = bOwnSelection ? 0 : 1;
    uintptr_t const HideSell = bPartnerSelection ? 0 : 1;
    uintptr_t const HideTransfer = bAnySelection ? 0 : 1;
    WelcomeText->SetProperty(1, HideWelcome, 0);
    WelcomeLine->SetProperty(1, HideWelcome, 0);
    WelcomeHelp->SetProperty(1, HideWelcome, 0);
    BuyText->SetProperty(1, HideBuy, 0);
    BuyArrow->SetProperty(1, HideBuy, 0);
    SellText->SetProperty(1, HideSell, 0);
    SellArrow->SetProperty(1, HideSell, 0);
    BuySellTradeButton->SetProperty(1, HideTransfer, 0);
    TransferItemButton->SetProperty(1, HideTransfer, 0);   // +0x364
    TransferItemView->SetProperty(1, HideTransfer, 0);     // +0x368
    TransferItemMesh->SetProperty(1, HideTransfer, 0);     // +0x36c
    TransferItemName->SetProperty(1, HideTransfer, 0);     // +0x370
    TransferItemQuantity->SetProperty(1, HideTransfer, 0); // +0x9d0
    TransferItemDamage->SetProperty(1, HideSell, 0);       // +0x374
    TransferItemPrice->SetProperty(1, HideTransfer, 0);    // +0x378

    // 0048018d..004801cf: the good-deal marker only for the commodity dealer, whose trade button
    // plays UI sound 0x25 on a buy (WireStaticControl +0x380).
    if (TradeMode != 1)
    {
        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags & 0xfc);
    }
    else
    {
        TransferItemGoodDeal->SetProperty(1, HideTransfer, 0);
        static_cast<WireStaticControl*>(BuySellTradeButton)->SoundSlot = bOwnSelection ? 0x25 : 0;
    }

    // 004801d9..0048027e: the quantity panel.
    QuantityPrompt->SetProperty(1, HideTransfer, 0);
    QuantitySlider->SetProperty(1, HideTransfer, 0);
    QuantityText->SetProperty(1, HideTransfer, 0);
    QuantityPrice->SetProperty(1, HideTransfer, 0);
    QuantityTotal->SetProperty(1, HideTransfer, 0);
    TransferPriceText->SetProperty(1, HideTransfer, 0); // +0x380
    QuantityTotalText->SetProperty(1, HideTransfer, 0);
    QuantityEdit->SetProperty(1, HideTransfer, 0);
    PlusMinus->SetProperty(1, HideTransfer, 0);

    // 00480284..004802fe: the button's caption (property 3) and the plus / minus sign.
    if (TradeMode == 2)
        BuySellTradeButton->SetProperty(3, 0x32d, 0);
    else
        BuySellTradeButton->SetProperty(3, bOwnSelection ? 0x32e : 0x32f, 0);
    PlusMinus->SetProperty(0x1e, bOwnSelection ? 0x482 : 0x481, 0);

    // 00480304..00480380: the transfer panel appeared or went -- the quantity starts at the
    // partner's selected stack (1 without one). The partner is not null-tested.
    if (Field9f4 != static_cast<unsigned char>(HideTransfer))
    {
        Field9f4 = static_cast<unsigned char>(HideTransfer);
        Field988 = 1;
        EquipDesc* const PartnerItem = Inventory->GetSelectedItem();
        if (PartnerItem != nullptr)
            Field988 = PartnerItem->Count;
        QuantitySlider->SetProperty(0x38, static_cast<uintptr_t>(Field988), 0);
        swprintf(ToolTipText, 0xff, L"%d", Field988); // PTR_DAT_005d0ec4
        QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    int MaxCount = 0; // [ESP+0x14]
    if (bAnySelection)
    {
        // 00480396..004803ef: the unit price, and a quantity of at least 1.
        FormatGroupedNumber(ToolTipText, Field388, -1); // FUN_004779a0
        TransferPriceText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        if (Field988 == 0)
        {
            Field988 = 1;
            swprintf(ToolTipText, 0xff, L"%d", 1);
            QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        }

        if (SelectedIndex != -1)
        {
            // 00480402..004806d0: how many can be bought / taken.
            MaxCount = Field38c;
            float RankRequired = 0.0f; // [ESP+0x1c]
            // [ESP+0x18]: GetMaxBuyCount writes it; on the ship dealer's path retail leaves it
            // uninitialised unless a cap below sets it -- the port starts it at 0 (no message).
            int Reason = 0;
            if (TradeMode != 2)
            {
                unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
                MaxCount = GetMaxBuyCount(GoodId, &Reason);      // FUN_004837a0
                if (Reason == 0 || Reason == 2)
                {
                    // 0048044e..0048046a: docked (DAT_0066873c, JBE) at a base with a market
                    // (FUN_0043a510 on DAT_00668708, null-tested).
                    unsigned const BaseId = GetClientLocationCurrentBase();
                    FClientMarket* const Market =
                        BaseId != 0 ? GetClientLocationBaseMarket(BaseId) : nullptr;
                    if (Market != nullptr)
                    {
                        // 00480470..0048048d: the good's required rank ([ESP+0x1c]), the
                        // player's rank, the good's required feeling ([ESP+0x24]).
                        RankRequired = ClientMarketGetMinLevel(Market, GoodId); // FUN_00438160
                        int const Rank = GetPlayerRankLevelRetail();            // FUN_004c55d0
                        float const FeelingRequired = ClientMarketGetMinRep(Market, GoodId); // FUN_004381c0

                        // 00480491..004804de: in single player, a rank in 0..0x3e6 below the
                        // requirement (FILD / FCOMP, TEST AH,5 + JP: only "less" is taken) --
                        // reason 5; otherwise the base's feeling (+0x990) below the good's --
                        // reason 6. Either leaves nothing to buy.
                        if (SinglePlayer() && Rank >= 0 && Rank < 0x3e7 &&
                            static_cast<double>(Rank) < static_cast<double>(RankRequired))
                        {
                            Reason = 5;
                            MaxCount = 0;
                        }
                        else if (static_cast<double>(BaseFeeling) <
                                 static_cast<double>(FeelingRequired))
                        {
                            Reason = 6;
                            MaxCount = 0;
                        }

                        // 004804e2..00480552: the partner's weapons / equipment filters (+0x32c
                        // 0 or 2; the partner is not null-tested): no hardpoint for the good at
                        // all (FUN_00585f20(GoodId, 1)) -- reason 7; with something to buy, no
                        // free one (FUN_00585f20(GoodId, 0)) -- reason 8.
                        int const Filter = Inventory->Mode;
                        if (Filter == 0 || Filter == 2)
                        {
                            ShipViewControl* const View = Inventory->Ship.ShipView; // +0x330
                            if (View->FindHardpointForGood(GoodId, true) == nullptr)
                            {
                                MaxCount = 0;
                                Reason = 7;
                            }
                            else if (MaxCount > 0 &&
                                     View->FindHardpointForGood(GoodId, false) == nullptr)
                            {
                                MaxCount = 0;
                                Reason = 8;
                            }
                        }
                    }
                }
            }
            else
            {
                // 00480557..004806cc: the ship dealer -- the selected item fits the partner's
                // hold, and nanobots / shield batteries the new ship's limits.
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr && Inventory != nullptr)
                {
                    if (Item->ArchId != 0)
                    {
                        Archetype::Equipment* const Equipment = Archetype::GetEquipment(Item->ArchId);
                        if (Equipment != nullptr)
                        {
                            float const Volume = Equipment->Volume; // +0x5c
                            if (Volume > 0.0f) // TEST AH,0x41 / JNZ
                            {
                                float Capacity = 0.0f; // [ESP+0x24]
                                float Used = 0.0f;     // [ESP+0x28]
                                Inventory->Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
                                int const Fit = static_cast<int>(
                                    (static_cast<double>(Capacity) - static_cast<double>(Used)) /
                                    static_cast<double>(Volume)); // ftol
                                if (MaxCount >= Fit)
                                    MaxCount = Fit;
                                if (MaxCount == 0)
                                    Reason = 4;
                            }
                        }
                    }

                    // 004805df..004806cc
                    unsigned const ArchId = Item->ArchId;
                    if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId)
                    {
                        Archetype::Ship const* const ShipArch =
                            Archetype::GetShip(Inventory->Ship.ShipArchId); // +0x348
                        if (ShipArch != nullptr)
                        {
                            bool const bNanobots = ArchId == G_NanobotArchId;
                            EquipDesc const* const Carried =
                                Inventory->Ship.Loadout.find_matching_cargo(ArchId, 0, 1.0f);
                            int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                            int const Limit = bNanobots ? ShipArch->NanobotLimit       // +0x140
                                                        : ShipArch->ShieldBatteryLimit; // +0x144
                            int const Room = Limit - CarriedCount;
                            if (MaxCount > Room && Limit >= 0)
                                MaxCount = Room;
                            if (MaxCount <= 0)
                            {
                                MaxCount = 0;
                                Reason = bNanobots ? 9 : 10;
                            }
                        }
                    }
                }
            }

            if (MaxCount != 0)
            {
                // 004807c0..0048081d: the slider's range, and the quantity capped by it.
                QuantitySlider->SetProperty(0x37, 1, static_cast<uintptr_t>(MaxCount));
                if (Field988 > MaxCount)
                {
                    Field988 = MaxCount;
                    QuantitySlider->SetProperty(0x38, static_cast<uintptr_t>(MaxCount), 0);
                    swprintf(ToolTipText, 0xff, L"%d", Field988);
                    QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                }
            }
            else
            {
                // 004806d6..004807be: why nothing can be bought (jump table 0x00480ce0).
                switch (Reason)
                {
                case 1: ShowErrorText(0x47e, 0); break;
                case 2: ShowErrorText(0x47f, 0); break;
                case 3: ShowErrorText(0x480, 0); break;
                case 4: ShowErrorText(0x490, 0); break;
                case 5: ShowErrorText(0x61b, static_cast<int>(RankRequired)); break; // ftol
                case 6: ShowErrorText(0x61c, 0); break;
                case 7: ShowErrorText(0x637, 0); break;
                case 8: ShowErrorText(0x63f, 0); break;
                case 9: ShowErrorText(0x3a4, 0); break;
                case 10: ShowErrorText(0x3a5, 0); break;
                case 11: ShowErrorText(0x3cf, 0); break;
                default: break;
                }
            }

            // 00480823..004808c9: the maximum, the edit box's limit, the prompt, and an engine
            // cannot be traded in at the ship dealer.
            swprintf(ToolTipText, 0xff, L"%d", MaxCount);
            TransferItemQuantity->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            if (MaxCount > 1)
            {
                QuantityEdit->SetProperty(0x43, 1, 8);
                QuantityPrompt->SetProperty(1, 0, 0);
            }
            else
            {
                QuantityEdit->SetProperty(0x43, 0, 0);
                QuantityPrompt->SetProperty(1, 1, 0);
            }
            Archetype::Equipment* const Shown = Archetype::GetEquipment(Field390);
            if (Shown != nullptr && Shown->get_class_type() == Archetype::AClassEngine &&
                TradeMode == 2)
                ShowErrorText(0x52d, 0);
        }
        else
        {
            // 004808d3..004809ae: the partner's item is sold -- up to its whole stack.
            QuantitySlider->SetProperty(0x37, 1, static_cast<uintptr_t>(Field38c));
            if ((TradeMode == 1 || TradeMode == 3) && Inventory != nullptr)
            {
                EquipDesc* const PartnerItem = Inventory->GetSelectedItem();
                if (PartnerItem != nullptr && PartnerItem->bMission) // +0x18
                    ShowErrorText(0x575, 0);
            }
            Archetype::Equipment* const Shown = Archetype::GetEquipment(Field390);
            if (Shown != nullptr && Shown->get_class_type() == Archetype::AClassEngine)
                ShowErrorText(TradeMode == 2 ? 0x52d : 0x52c, 0);
            swprintf(ToolTipText, 0xff, L"%d", Field38c);
            TransferItemQuantity->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            if (Field38c > 1)
                QuantityEdit->SetProperty(0x43, 1, 8);
            else
                QuantityEdit->SetProperty(0x43, 0, 0);
        }

        // 004809b4..004809e0: the total.
        FormatGroupedNumber(ToolTipText, Field988 * Field388, -1);
        QuantityTotalText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    // 004809e6..00480b93: the ship dealer's help, price and "not enough credits" line. The
    // partner, +0x9f0 and (on the second path) +0x9e8 are not null-tested.
    if (TradeMode == 2)
    {
        if (SelectedIndex == -1 && Inventory->SelectedIndex == -1)
            WelcomeHelp->Flags = static_cast<unsigned char>(WelcomeHelp->Flags | 3);
        else
            WelcomeHelp->Flags = static_cast<unsigned char>(WelcomeHelp->Flags & 0xfc);

        int const OwnResale = NnLoadoutResaleValue(&Ship.Loadout, &Ship,
                                                   &Inventory->SavedLoadout); // FUN_004c6f70
        int const PartnerResale = NnLoadoutResaleValue(&Inventory->Ship.Loadout, &Inventory->Ship,
                                                       &Inventory->SavedLoadout);
        int ShipPrice = Field9f8 - OwnResale + PartnerResale;
        if (ShipPrice < 0) // JNS
            ShipPrice = 0;
        FormatGroupedNumber(ToolTipText, ShipPrice, 0x3c9);
        ShipDealerShipPrice->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);

        int const Cost = Field9f8 - Field9fc - OwnResale + PartnerResale;
        if (ShipBuyProblem != nullptr && Cost > static_cast<int>(GetPlayerCreditsRetail())) // JLE
        {
            ContinueButton->SetProperty(1, 1, 0);
            ContinueText->SetProperty(1, 1, 0);
            FormatGroupedNumber(ToolTipText, Cost - static_cast<int>(GetPlayerCreditsRetail()), -1);
            GetIdsStringWWithRawFallback(0x630, G_NnTextScratchA, G_NnTextScratchCapacity);
            swprintf(G_NnTextScratchB, 0x1000, G_NnTextScratchA, ToolTipText);
            ShipBuyProblem->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        }
        else
        {
            ContinueButton->SetProperty(1, 0, 0);
            ContinueText->SetProperty(1, 0, 0);
            ShipBuyProblem->SetProperty(0x1e, 0, 0);
        }
    }

    // 00480b99..00480c05: the partner just traded (+0x3b8) -- the buy / sell controls hide.
    if (Inventory->Field3b8 != 0)
    {
        BuySellTradeButton->SetProperty(1, 1, 0);
        BuyText->SetProperty(1, 1, 0);
        BuyArrow->SetProperty(1, 1, 0);
        SellText->SetProperty(1, 1, 0);
        SellArrow->SetProperty(1, 1, 0);
    }

    // 00480c0b..00480c31
    if (Inventory->Field3c0 != 0 || Inventory->Field3b9 != 0)
        ErrorHelp->SetProperty(0x1e, 0, 0);

    // 00480c37..00480cd0: the partner's pending good no longer fits -- drop the selection when
    // it is the one selected here and nothing can be bought.
    if (Inventory->Field3c1 == 0)
        return;
    Inventory->Field3c1 = 0;
    if (SelectedIndex == -1 || TradeMode == 2)
        return;
    unsigned const SelectedGood = GetSelectedMarketGood(); // FUN_00483580
    if (MaxCount != 0 || SelectedGood != Inventory->Field3bc)
        return;
    ClearSelection();
    ErrorHelp->SetProperty(0x1e, 0, 0);
}

intptr_t NnDealer::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004822a0 (RET 0xc).
    switch (Msg)
    {
    case 0x2c:
    case 0x2d:
        // 004822c4: the row hover goes to NN_Inventory (FUN_00479d10).
        return NnInventory::SetProperty(Msg, A, B);

    case 0x15:
        // 004822dd: the ship view picked an instance -- select the row of its hardpoint.
        SelectHardpoint(Ship.ShipView->GetHardpointOfInstance(static_cast<long>(A))); // FUN_00585e20, FUN_0047e270
        return 0;

    case 0x16:
    {
        // 004822fe: an item dropped on hardpoint A. The name goes into the argument's slot.
        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(reinterpret_cast<char const*>(A), false);
        if (Inventory != nullptr && Inventory->GetSelectedItem() != nullptr)
        {
            // 0048232e..00482446: the partner's selected item, mounted on this window's ship.
            EquipDesc* const Item = Inventory->GetSelectedItem();
            if (Item == nullptr)
                return 0;
            Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
            int const Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460
            int Group = 0;
            if (Filter == 0)
                Group = 0;
            else if (Filter == 2)
                Group = 1;
            else
                return 0;
            if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
                return 0;
            if (Ship.MountItem(Item, &Hardpoint, 0, true)) // FUN_004c6530
                Inventory->Ship.RemoveItem(Item, 0);       // FUN_004c6430
            ClearSelection();
            Inventory->ClearSelection();
            return 0;
        }

        // 00482449..00482553: this window's selected item moves to the hardpoint.
        if (GetSelectedItem() == nullptr)
            return 0;
        EquipDesc* const Item = GetSelectedItem();
        if (Item == nullptr)
            return 0;
        Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
        int const Filter = NnItemFilterMode(Item->ArchId);
        int Group = 0;
        if (Filter == 0)
            Group = 0;
        else if (Filter == 2)
            Group = 1;
        else
            return 0;
        if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
            return 0;
        Ship.SetItemHardpoint(Item, Hardpoint.Value); // FUN_004c6910
        ClearSelection();
        if (Inventory == nullptr)
            return 0;
        Inventory->ClearSelection();
        return 0;
    }

    case 0x3e:
        // 00482559: a slider moved -- the list scroll or the quantity slider.
        if (reinterpret_cast<WinControl*>(A) == InventoryScroll) // +0x3d0
        {
            FirstRow = static_cast<int>(B);
            return 0;
        }
        if (reinterpret_cast<WinControl*>(A) != QuantitySlider) // +0x9b8
            return 0;
        Field988 = static_cast<int>(B);
        if (Field988 < 0)
            Field988 = 0;
        swprintf(ToolTipText, 0xff, L"%d", Field988); // PTR_DAT_005d0ec4
        QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        return 0;

    case 0x45:
    {
        // 004825d6: the quantity edit's text changed.
        wchar_t const* const Text = reinterpret_cast<wchar_t const*>(A);
        if (Text == nullptr)
        {
            QuantitySlider->SetProperty(0x38, 1, 0);
            return 0;
        }
        int Value = 1; // the argument's slot, 1 before swscanf
        swscanf(Text, L"%d", &Value);
        if (Value < 1)
            Field988 = 1;
        else if (Value > Field38c)
            Field988 = Field38c;
        else
            Field988 = Value;
        swprintf(ToolTipText, 0xff, L"%d", Field988);
        if (_wcsicmp(Text, ToolTipText) != 0)
            QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        QuantitySlider->SetProperty(0x38, static_cast<uintptr_t>(Field988), 0);
        return 0;
    }

    case 0x1a:
    case 0x26:
    {
        // 0048269e: a click.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == CloseButton) // +0x4d8
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            return 1;
        }
        if (Control == BackToShipDealer) // +0x9ec
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            // 004826c8..004826cf: FUN_00442200(7) -- the NavBar activates its ship dealer tab.
            NavBarOpenButton(7);
            return 1;
        }
        if (Control == ContinueButton && Inventory != nullptr) // +0x9e0
        {
            if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
            {
                NnMainButtonOnModalOpen();
                return 1;
            }
            if (Field98c != 0)
                return 1;
            BuyShip(); // FUN_00483ae0
            return 1;
        }
        if (Control == BuySellTradeButton) // +0x9b0
        {
            if (TradeMode == 2)
            {
                if (SelectedIndex != -1)
                {
                    // 0048274f: the partner is not null-tested.
                    Inventory->TakeDealerSelection(); // FUN_0047e920
                }
                else
                {
                    // 0048275f..0048281f: the partner's selected item goes back into this
                    // (the partner's dealer's) model.
                    NnInventory* const Partner = Inventory;
                    EquipDesc* const Item = Partner->GetSelectedItem();
                    if (Item != nullptr && Partner->Dealer != nullptr &&
                        Partner->Dealer->Field988 != 0)
                    {
                        int const Count = Partner->Dealer->Field988;
                        Item->set_equipped(false);
                        if (Partner->Dealer->Ship.AddItem(Item, Count, true)) // FUN_004c64b0
                            Partner->Ship.RemoveItem(Item, Count);             // FUN_004c6430
                        Partner->ClearSelection();
                        Partner->SelectedItem = nullptr;         // +0x4f4
                        Partner->Dealer->SelectedItem = nullptr; // dealer +0x4f4
                    }
                }
            }
            else if (SelectedIndex != -1)
            {
                // 00482826..0048284a: buy.
                Inventory->BuyFromDealer(Field988); // FUN_0047eab0
                Inventory->Field394.clear();
            }
            else
            {
                // 0048284c..00482859: sell.
                Inventory->SellSelected(Field988); // FUN_0047eea0
            }

            // 0048285e..004828a5: the quantity goes back to 1.
            Field988 = 1;
            QuantitySlider->SetProperty(0x38, 1, 0);
            swprintf(ToolTipText, 0xff, L"%d", Field988);
            QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            return 1;
        }

        // 004828b7..00482954: a control of a row (every row is tested; retail does not stop at
        // the first match). A click on the row's button also clears +0x984.
        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            if (Control != ItemButton[Row] && Control != ItemView[Row] &&
                Control != ItemGoodDeal[Row])
                continue;
            if (Control == ItemButton[Row])
                Field984 = 0;
            SelectDealerRow(Row, 1); // FUN_00482120
            Field4e0 = 0;
            if (Inventory != nullptr)
                Inventory->ClearSelection();
        }

        // 0048295a..00482a30: the filter buttons (the partner follows this window's mode) and
        // the ship view's info button.
        int NewMode = -1;
        if (Control == FilterCargoBay)
            NewMode = 3;
        else if (Control == FilterWeapons)
            NewMode = 0;
        else if (Control == FilterEquipment)
            NewMode = 2;
        else if (Control == FilterCommodity)
            NewMode = 4;
        else if (Control == FilterAmmo)
            NewMode = 1;
        if (NewMode != -1)
        {
            SetMode(NewMode, true); // FUN_0047f240
            if (Inventory != nullptr)
                Inventory->SetMode(Mode, true);
            return 0;
        }
        if (Control == Ship.InfoText) // +0x344 "ShipView_Info"
        {
            SelectDealerRow(-1, 1);
            Field4e0 = 1;
        }
        return 0;
    }

    default:
        return 0;
    }
}

void NnDealer::ShowErrorText(unsigned Ids, int Arg)
{
    // FUN_0047fd50 (RET 8). No control pointer is null-tested.
    ErrorHelp->SetProperty(1, 0, 0); // +0x9d4 shown

    // 0047fd68..0047fe7a: hide the rest (property 1, 1).
    BuyText->SetProperty(1, 1, 0);            // +0x9a0
    BuyArrow->SetProperty(1, 1, 0);           // +0x9a4
    SellText->SetProperty(1, 1, 0);           // +0x9a8
    SellArrow->SetProperty(1, 1, 0);          // +0x9ac
    BuySellTradeButton->SetProperty(1, 1, 0); // +0x9b0
    QuantityPrompt->SetProperty(1, 1, 0);     // +0x9b4
    QuantitySlider->SetProperty(1, 1, 0);     // +0x9b8
    QuantityText->SetProperty(1, 1, 0);       // +0x9bc
    QuantityPrice->SetProperty(1, 1, 0);      // +0x9c0
    QuantityTotal->SetProperty(1, 1, 0);      // +0x9c4
    TransferPriceText->SetProperty(1, 1, 0);  // +0x380
    QuantityTotalText->SetProperty(1, 1, 0);  // +0x9c8
    QuantityEdit->SetProperty(1, 1, 0);       // +0x9cc
    PlusMinus->SetProperty(1, 1, 0);          // +0x9d8

    // 0047fe80..0047fea0: no argument -- the ids goes to the control as is (property 0x1e).
    if (Arg == 0)
    {
        ErrorHelp->SetProperty(0x1e, Ids, 0);
        return;
    }

    // 0047fea3..0047fee4: the ids' string (FUN_004347e0, DAT_006119f8 wchars into
    // DAT_0066dc60) is the format of swprintf into DAT_0066fc60 (property 0x1d).
    GetIdsStringWWithRawFallback(Ids, G_NnTextScratchA, G_NnTextScratchCapacity);
    swprintf(G_NnTextScratchB, 0x1000, G_NnTextScratchA, Arg);
    ErrorHelp->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
}

void NnDealer::SelectDealerRow(int Row, int /*Unused*/)
{
    // FUN_00482120 (RET 8). The second argument is never read.
    SelectRow(Row, 1); // FUN_0047e0e0

    // 00482131..00482178: quantity 1 on the slider and the edit box.
    Field988 = 1;
    QuantitySlider->SetProperty(0x38, 1, 0);
    swprintf(ToolTipText, 0xff, L"%d", Field988); // PTR_DAT_005d0ec4, into +0x508
    QuantityEdit->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ToolTipText), 0);

    // 0048217e..004821c4: the row's item on the ship model; the flag byte reuses the first
    // argument's slot ([ESP+0x10]).
    bool const bFlag = TradeMode == 2 || TradeMode == 3;
    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
    Ship.ShowItem(Item, bFlag, Item != nullptr ? NnItemFilterMode(Item->ArchId) : 5); // FUN_004c6a50

    // 004821c9..004821e8: a commodity dealer puts its partner on the cargo filter.
    if (Inventory != nullptr && TradeMode == 1 && Inventory->Mode != 4)
        Inventory->SetMode(4, false); // FUN_0047f240

    // 004821ed..00482245: the partner's +0x394 names the hardpoint the selected good would go
    // on (this dealer's weapons / equipment filters).
    if (Inventory != nullptr)
    {
        Inventory->Field394.clear();
        unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
        if (GoodId != 0 && (Mode == 0 || Mode == 2))
        {
            char const* const Hardpoint =
                Inventory->Ship.ShipView->FindHardpointForGood(GoodId, false); // FUN_00585f20
            if (Hardpoint != nullptr)
                Inventory->Field394.Value = StringAlloc(Hardpoint, false);
        }
    }

    // 0048224b..00482292: the partner's selection is cleared (the inlined sequence, including
    // its dealer's +0x4e8 / +0x4ec).
    if (Inventory != nullptr)
        Inventory->ClearSelection();
}

void NnDealer::BuyShip()
{
    // FUN_00483ae0. The cost: the package's price less the old ship's trade-in, less what the
    // market pays for this window's loadout, plus what it pays for the partner's.
    int const OwnResale = NnLoadoutResaleValue(&Ship.Loadout, &Ship,
                                               &Inventory->SavedLoadout); // FUN_004c6f70
    int const PartnerResale = NnLoadoutResaleValue(&Inventory->Ship.Loadout, &Inventory->Ship,
                                                   &Inventory->SavedLoadout);
    int const Cost = Field9f8 - Field9fc - OwnResale + PartnerResale;
    if (static_cast<int>(GetPlayerCreditsRetail()) < Cost) // FUN_004c4ce0, signed (JGE)
    {
        // 00483b5a..00483b67: FUN_00489e80(DAT_00671c74, 0) -- the "not_enough_money" voice.
        NnVoiceEnqueue(G_NnVoiceNotEnoughMoneyId, 0);
        return;
    }

    // 00483b7c..00483b9a: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested), then
    // FUN_00438220(base, +0x97c, 1) -- the server is asked to sell the player the package.
    {
        unsigned const BaseId = GetClientLocationCurrentBase();
        ClientMarketRequestBuy(GetClientLocationBaseMarket(BaseId), BaseId,
                               static_cast<unsigned>(Field97c), 1);
    }

    // 00483b9f..00483bc1: the new loadout is the partner's (it shows the new ship); the ship
    // archetype is this window's.
    EquipDescList NewLoadout = Inventory->Ship.Loadout; // std::list copy ctor
    unsigned const ShipArchId = Ship.ShipArchId;      // +0x348

    // 00483bc5..00483c43: the player's collision groups, every one at full health.
    std::list<CollisionGroupDesc> Groups;
    std::list<CollisionGroupDesc> const& PlayerGroups = GetPlayerCollisionGroupListRetail(); // FUN_004c3f00
    Groups.insert(Groups.begin(), PlayerGroups.begin(), PlayerGroups.end());
    for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It)
        It->Health = 1.0f; // +0xc of the node
    ReqPlayerCollisionGroupsRetail(&Groups); // FUN_004c3f10

    // 00483c46..00483c5d: both ship models empty.
    Ship.Clear();             // FUN_004c6250
    Inventory->Ship.Clear();

    // 00483c62..00483c84: cash, loadout, ship, hull.
    ReqPlayerCashRetail(static_cast<int>(GetPlayerCreditsRetail()) - Cost); // FUN_004c4cc0
    ReqPlayerEquipmentRetail(&NewLoadout);  // FUN_004c4040
    ReqPlayerShipArchRetail(ShipArchId);    // FUN_004c3df0
    ReqPlayerHullStatusRetail(1.0f);        // FUN_004c4f50

    // 00483c89..00483c8b: FUN_004427e0(1) -- the NavBar's repair tab disabled (the new ship
    // is undamaged).
    NavBarRefreshRepairTab(true);

    // 00483c93..00483ca2
    G_WeaponGroups.Clear(); // FUN_004f6190
    HudPostCommand(0x6b, 0); // FUN_005763b0

    // 00483ca7..00483cae: FUN_00489e80(DAT_00671c60, 0) -- the "ship_purchase_complete" voice.
    NnVoiceEnqueue(G_NnVoiceShipPurchaseCompleteId, 0);
}

void NnDealer::RefreshMarketList()
{
    // FUN_00482a90. 00482ab3..00482ab9: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not
    // null-tested: FUN_00437f70 runs on it as it is).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());

    // 00482ac0..00482ae7: the commodity dealer lists the cargo filter and collects category 0,
    // the equipment dealer category 1. Any other mode passes an uninitialised local
    // ([ESP+0x24]); the only callers are Update (0x0047ff89, trade modes 1 and 3 only) and this
    // function itself (0x00482cdc, same mode), so that path is never taken.
    unsigned Category = 0;
    if (TradeMode == 1)
        Mode = 4; // +0x32c
    else if (TradeMode == 3)
        Category = 1;

    // 00482aeb..00482b44: the market's goods of that category (FUN_00437f70) into a local list,
    // then FUN_00483230 (mode 3) or FUN_00482f10 orders it.
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, Category);
    if (TradeMode == 3)
        SortEquipmentMarketGoods(&Goods); // FUN_00483230
    else
        SortMarketGoodsByName(&Goods);    // FUN_00482f10

    // 00482b44..00482c37: one row per entry in stock (+0x1c > 0; the JGE clamp at 0x00482b6c
    // is then always taken).
    int Counter = 0;                           // [ESP+0x14]
    FClientMarketGood const* Selected = nullptr; // [ESP+0x1c]
    for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end(); ++It)
    {
        FClientMarketGood const* const Entry = *It;
        if (Entry->Stock <= 0)
            continue;
        int const Stock = Entry->Stock;

        // 00482b76..00482bde: the filter -- the archetype's own in mode 3, otherwise 4, or 5
        // while docked at a base whose market prices the good at exactly 0.0 (FCOMP 0, TEST
        // AH,0x44 + JP: only "equal" falls through to the 5).
        unsigned const ArchId = Good2Arch(Entry->Info.GoodId);
        int Filter = 4;
        if (TradeMode == 3)
            Filter = NnItemFilterMode(ArchId); // FUN_0047e460
        else
        {
            unsigned const BaseId = GetClientLocationCurrentBase();
            if (BaseId != 0)
            {
                FClientMarket* const BaseMarket = GetClientLocationBaseMarket(BaseId);
                if (BaseMarket != nullptr &&
                    static_cast<double>(ClientMarketGetPrice(BaseMarket, Arch2Good(ArchId))) ==
                        0.0)
                    Filter = 5;
            }
        }

        // 00482be3..00482c10: FillItemRow(&Counter, arch, filter, stock, package price, 0, 0,
        // 0, 0, the CanBuyGood byte, 0, 0, 0).
        int const Price = GetMarketPackagePrice(Entry->Info.GoodId); // FUN_0047f040
        unsigned char const bCanBuy = static_cast<unsigned char>(Entry->Field24 & 0xffu);
        bool const bRowSelected =
            FillItemRow(&Counter, ArchId, Filter, Stock, Price, 0, 0, 0, 0, bCanBuy, 0, 0, 0);

        // 00482c15..00482c21: the first entry FillItemRow reports as the selected row.
        if (Selected == nullptr && bRowSelected)
            Selected = Entry;
    }

    // 00482c3d..00482c56: that entry's archetype on the info card.
    if (Selected != nullptr)
        ShowInfoCard(Good2Arch(Selected->Info.GoodId), 0); // FUN_0047ba90

    // 00482c5b..00482c65
    Field4e2 = 0;
    UpdateRowVisibility(Counter); // FUN_0047f3e0

    // 00482c6a..00482cdc: the row count changed -- rescale the scroll bar, keep no gap below the
    // last row, build the list again.
    if (Counter != Field3c8)
    {
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Counter - 1));
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
        Field3c8 = Counter;
        RefreshMarketList();
    }

    // 00482ce1..00482cf6 (unsigned compare)
    if (Counter == 0)
        SelectedIndex = -1;
    if (static_cast<unsigned>(SelectedIndex) >= static_cast<unsigned>(Counter))
        SelectedIndex = -1;

    // 00482cfc..00482d23: the equipment dealer points the partner's ship view at the good.
    if (TradeMode == 3)
    {
        unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
        if (GoodId != 0 && Inventory != nullptr)
            Inventory->Ship.ShipView->SelectGood(GoodId, true); // FUN_00585d90
    }
}

bool NnDealer::CanBuyGood(unsigned GoodId)
{
    // FUN_00482e00 (RET 4). Callers: the market list sorts FUN_00482f10 (0x00483049) and
    // FUN_00483230 (0x0048330b).
    if (TradeMode != 3) // +0x978
        return true;

    // 00482e14..00482e35: not docked (DAT_0066873c, JBE), or no market (FUN_0043a510 on
    // DAT_00668708, null-tested) -- false.
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId == 0)
        return false;
    FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return false;

    // 00482e38..00482e6f: the good's required rank ([ESP+0x10]), the player's rank (kept in the
    // argument slot), then the required feeling FCOMP +0x990: TEST AH,0x41 + JNZ goes on when the
    // requirement is below or equal (or unordered); above the base's feeling -- false.
    float const RankRequired = ClientMarketGetMinLevel(Market, GoodId); // FUN_00438160
    int const Rank = GetPlayerRankLevelRetail();                        // FUN_004c55d0
    float const FeelingRequired = ClientMarketGetMinRep(Market, GoodId); // FUN_004381c0
    if (static_cast<double>(FeelingRequired) > static_cast<double>(BaseFeeling))
        return false;

    // 00482e72..00482e9e: a rank in 0..0x3e6 below the requirement (FILD / FCOMP, TEST AH,5 +
    // JP: only "less"), in single player (tested last) -- false.
    if (Rank >= 0 && Rank < 0x3e7 &&
        static_cast<double>(Rank) < static_cast<double>(RankRequired) && SinglePlayer())
        return false;

    // 00482ea1..00482ecf: the partner's weapons / equipment filters (+0x32c 0 or 2; the partner
    // is not null-tested): no hardpoint for the good at all (FUN_00585f20(GoodId, 1)) -- false.
    int const Filter = Inventory->Mode;
    if (Filter == 0 || Filter == 2)
    {
        if (Inventory->Ship.ShipView->FindHardpointForGood(GoodId, true) == nullptr)
            return false;
    }

    // 00482ed2..00482f03: the ammo filter needs a launcher for it in the partner's loadout
    // (FUN_00482d80 on +0x34c with Good2Arch(GoodId)); every other case -- true.
    if (Inventory->Mode == 1)
        return NnListHasLauncherFor(&Inventory->Ship.Loadout, Good2Arch(GoodId));
    return true;
}

// The CanBuyGood byte the two sorts keep in the low byte of the market entry's +0x24 (byte
// stores 0x0048304e / 0x00483310, byte reads 0x004830d5 / 0x00483406 / 0x00482beb). Retail
// writes through the list's entry pointer into the market itself; the port's list holds const
// pointers (ClientMarketCollectCategory), so the store casts the const away.
static void NnDealerSetCanBuyByte(FClientMarketGood const* Entry, bool bCanBuy)
{
    FClientMarketGood* const Writable = const_cast<FClientMarketGood*>(Entry);
    Writable->Field24 = (Writable->Field24 & ~0xffu) | (bCanBuy ? 1u : 0u);
}

static unsigned char NnDealerCanBuyByte(FClientMarketGood const* Entry)
{
    return static_cast<unsigned char>(Entry->Field24 & 0xffu);
}

// FUN_00482f10's 8-byte record: the entry and its index into the name buffer.
struct FNnDealerNameSortRecord
{
    FClientMarketGood const* Entry; // +0
    int NameIndex;                  // +4
};

void NnDealer::SortMarketGoodsByName(std::list<FClientMarketGood const*>* Goods)
{
    // FUN_00482f10 (RET 4). 00482f38..0048301c: a vector of Count 8-byte records and a vector of
    // Count 0x40-wchar names, Count = the list's size (+8).
    int const Count = static_cast<int>(Goods->size());
    std::vector<FNnDealerNameSortRecord> Records(static_cast<size_t>(Count));
    std::vector<wchar_t> Names(static_cast<size_t>(Count) * 0x40);

    // 0048301c..004830b1: per node, in list order: the record {entry, i}, the entry's CanBuyGood
    // byte (FUN_00482e00), and the name -- FUN_004347e0(DAT_0067eca8, Equipment +0x14, name,
    // 0x40) of GetEquipment(Good2Arch(good)), or an empty string without one.
    int Index = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods->begin(); It != Goods->end();
         ++It, ++Index)
    {
        FClientMarketGood const* const Entry = *It;
        Records[Index].Entry = Entry;
        Records[Index].NameIndex = Index;
        NnDealerSetCanBuyByte(Entry, CanBuyGood(Entry->Info.GoodId));
        Archetype::Equipment const* const Equip =
            Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
        wchar_t* const Name = &Names[static_cast<size_t>(Index) * 0x40];
        if (Equip == nullptr)
            Name[0] = L'\0';
        else
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Equip->IdsName), Name, 0x40);
    }

    // 004830bd..00483150: bubble passes over the Count - 1 neighbour pairs until one makes no
    // swap. A pair swaps when the first cannot be bought and the second can (0x004830e2 /
    // 0x004830eb), or both bytes are equal and _wcsicmp(first name, second name) > 0.
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < Count - 1; ++Pair)
        {
            FNnDealerNameSortRecord& A = Records[Pair];
            FNnDealerNameSortRecord& B = Records[Pair + 1];
            unsigned char const CanBuyA = NnDealerCanBuyByte(A.Entry);
            unsigned char const CanBuyB = NnDealerCanBuyByte(B.Entry);
            bool bSwap = false;
            if (CanBuyA == 0 && CanBuyB != 0)
                bSwap = true;
            else if (CanBuyA == CanBuyB &&
                     _wcsicmp(&Names[static_cast<size_t>(A.NameIndex) * 0x40],
                              &Names[static_cast<size_t>(B.NameIndex) * 0x40]) > 0)
                bSwap = true;
            if (bSwap)
            {
                FNnDealerNameSortRecord const Saved = A;
                A = B;
                B = Saved;
                bSorted = false;
            }
        }
    }

    // 00483156..004831e2: every node erased (FUN_00516f70), then one node per record appended
    // in order; the two buffers freed.
    Goods->clear();
    for (int Out = 0; Out < Count; ++Out)
        Goods->push_back(Records[Out].Entry);
}

// FUN_00483230's 0x10-byte record.
struct FNnDealerEquipSortRecord
{
    FClientMarketGood const* Entry; // +0x0
    int Class;                      // +0x4 Equipment vt+0x10 (get_class_type), 0 without one
    int bSpecial;                   // +0x8 the byte flag, zero-extended (0x004833b1)
    float Price;                    // +0xc
};

void NnDealer::SortEquipmentMarketGoods(std::list<FClientMarketGood const*>* Goods)
{
    // FUN_00483230 (RET 4). 00483253..004832c1: a vector of Count 0x10-byte records.
    int const Count = static_cast<int>(Goods->size());
    std::vector<FNnDealerEquipSortRecord> Records(static_cast<size_t>(Count));

    // 004832c1..004833db: per node, in list order.
    int Index = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods->begin(); It != Goods->end();
         ++It, ++Index)
    {
        FClientMarketGood const* const Entry = *It;
        bool bSpecial = false; // [ESP+0x13], cleared per node (0x004832e8)
        int Class = 0;         // EBX, 0 per node (0x004832d8)

        // 004832ed..00483307: FILD qword {package price, 0} -- the price as an unsigned
        // 32-bit value -- stored as a float.
        int const PackagePrice = GetMarketPackagePrice(Entry->Info.GoodId); // FUN_0047f040
        float const Price = static_cast<float>(static_cast<unsigned>(PackagePrice));

        // 0048330b..00483310
        NnDealerSetCanBuyByte(Entry, CanBuyGood(Entry->Info.GoodId));

        // 00483313..00483399: the class and the flag.
        Archetype::Equipment const* const Equip =
            Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
        if (Equip != nullptr)
        {
            Class = static_cast<int>(Equip->get_class_type()); // vt+0x10
            if (Class == Archetype::AClassGun) // 0x11
            {
                // A gun is special when it is an auto turret (+0xba), or when its projectile
                // (+0x98, not IsEmpty, GetEquipmentByName) is a munition (0x1f) with a seeker
                // (+0x90 != 0).
                Archetype::Gun const* const Gun = static_cast<Archetype::Gun const*>(Equip);
                if (Gun->bAutoTurret)
                    bSpecial = true;
                else if (!Gun->ProjectileArchetype.IsEmpty())
                {
                    Archetype::Equipment const* const Projectile =
                        Archetype::GetEquipmentByName(Gun->ProjectileArchetype);
                    if (Projectile != nullptr &&
                        Projectile->get_class_type() == Archetype::AClassMunition &&
                        static_cast<Archetype::Munition const*>(Projectile)->Seeker != 0)
                        bSpecial = true;
                }
            }
            else if (Class == Archetype::AClassMunition) // 0x1f
            {
                // A munition is special when its hp type (+0xa8) is 0xa or 0x23.
                unsigned const HpType = static_cast<Archetype::Munition const*>(Equip)->HpType;
                if (HpType == 0xa || HpType == 0x23)
                    bSpecial = true;
            }
        }

        // 0048339e..004833cd
        Records[Index].Entry = Entry;
        Records[Index].Class = Class;
        Records[Index].bSpecial = bSpecial ? 1 : 0;
        Records[Index].Price = Price;
    }

    // 004833e5..004834a8: bubble passes over the Count - 1 neighbour pairs until one makes no
    // swap. A pair swaps when the first cannot be bought and the second can; with equal bytes,
    // when the first's class is greater (JG), or with equal classes its flag is greater (JG),
    // or with equal flags its price is greater (FCOMP, TEST AH,0x41 + JNZ keeps less, equal and
    // unordered).
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < Count - 1; ++Pair)
        {
            FNnDealerEquipSortRecord& A = Records[Pair];
            FNnDealerEquipSortRecord& B = Records[Pair + 1];
            unsigned char const CanBuyA = NnDealerCanBuyByte(A.Entry);
            unsigned char const CanBuyB = NnDealerCanBuyByte(B.Entry);
            bool bSwap = false;
            if (CanBuyA == 0 && CanBuyB != 0)
                bSwap = true;
            else if (CanBuyA == CanBuyB)
            {
                if (A.Class > B.Class)
                    bSwap = true;
                else if (A.Class == B.Class)
                {
                    if (A.bSpecial > B.bSpecial)
                        bSwap = true;
                    else if (A.bSpecial == B.bSpecial &&
                             static_cast<double>(A.Price) > static_cast<double>(B.Price))
                        bSwap = true;
                }
            }
            if (bSwap)
            {
                FNnDealerEquipSortRecord const Saved = A;
                A = B;
                B = Saved;
                bSorted = false;
            }
        }
    }

    // 004834ae..00483559: every node unlinked and freed, one node per record appended in order;
    // the record buffer freed.
    Goods->clear();
    for (int Out = 0; Out < Count; ++Out)
        Goods->push_back(Records[Out].Entry);
}

int NnDealer::GetMaxBuyCount(unsigned GoodId, int* OutReason)
{
    // FUN_004837a0 (RET 8). Caller: Update 0x00480432.
    *OutReason = 0;
    if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
    {
        *OutReason = 3;
        return 0;
    }
    if (GoodId == 0)
        return 0;

    // 004837da..00483801: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested) and its
    // entry FUN_00437d90(GoodId); no entry, or none in stock (+0x1c <= 0) -- 0.
    FClientMarketGood const* const Entry =
        ClientMarketFindEntry(GetClientLocationBaseMarket(GetClientLocationCurrentBase()), GoodId);
    if (Entry == nullptr || Entry->Stock <= 0)
        return 0;

    // 00483807..00483832: n = credits / the package price (XOR EDX + DIV: unsigned; a zero
    // price faults as in retail); n <= 0 (signed JG) -- reason 2.
    unsigned const PackagePrice =
        static_cast<unsigned>(GetMarketPackagePrice(Entry->Info.GoodId)); // FUN_0047f040
    int Count = static_cast<int>(GetPlayerCreditsRetail() / PackagePrice); // FUN_004c4ce0
    if (Count <= 0)
    {
        *OutReason = 2;
        return 0;
    }

    // 00483835..004838cf: with a partner, refresh its hardpoint hint (+0x394) when it is empty:
    // clear it, then for this window's weapons / equipment filters (+0x32c 0 or 2) the
    // hardpoint FUN_00585f20(selected good, 0) finds. A named hardpoint means one at a time:
    // min(n, min(stock, 1)).
    if (Inventory != nullptr)
    {
        if (Inventory->Field394.Value[0] == '\0')
        {
            Inventory->Field394.clear();
            unsigned const Selected = GetSelectedMarketGood(); // FUN_00483580
            if (Selected != 0 && (Mode == 0 || Mode == 2))
            {
                char const* const Hardpoint =
                    Inventory->Ship.ShipView->FindHardpointForGood(Selected, false);
                if (Hardpoint != nullptr)
                    Inventory->Field394.Value = StringAlloc(Hardpoint, false);
            }
        }
        if (Inventory->Field394.Value[0] != '\0')
        {
            int Cap = Entry->Stock;
            if (Cap > 1)
                Cap = 1;
            return Count >= Cap ? Cap : Count;
        }
    }

    // 004838d2..0048391e: the hold cap -- (hold size FUN_004c4610 - used FUN_004c4640) / the
    // good's volume (Equipment +0x5c) when Good2Arch and GetEquipment answer and the volume is
    // not 0.0 (FCOMP 0, TEST AH,0x44 + JNP takes "equal" to the stock branch); otherwise the
    // stock (FILD). ftol either way.
    unsigned const ArchId = Good2Arch(Entry->Info.GoodId); // [ESP+0x1c]
    double HoldValue = static_cast<double>(Entry->Stock);
    if (ArchId != 0)
    {
        Archetype::Equipment const* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr && static_cast<double>(Equip->Volume) != 0.0)
        {
            float const HoldSize = GetPlayerShipHoldSizeRetail(); // FUN_004c4610
            float const Used =
                GetPlayerEquipDescListRetail().get_cargo_space_occupied(); // FUN_004c4640
            HoldValue = (static_cast<double>(HoldSize) - static_cast<double>(Used)) /
                        static_cast<double>(Equip->Volume);
        }
    }
    int const HoldCap = static_cast<int>(HoldValue); // ftol, [ESP+0x14]

    // 00483928..0048393f: n = min(n, hold cap, stock), at least 0.
    if (Count >= HoldCap)
        Count = HoldCap;
    if (Count >= Entry->Stock)
        Count = Entry->Stock;
    if (Count < 0)
        Count = 0;

    // 00483941..00483a54: the partner's weapons / equipment / cargo filters (+0x32c 0, 2, 3;
    // the partner is not null-tested from here on) take one at a time, except the nanobots
    // (DAT_00674a8c) and shield batteries (DAT_00674a88) under the cargo filter, which are
    // capped by the player ship's limit (Ship +0x140 / +0x144, a negative limit caps nothing)
    // less the count already carried (find_matching_cargo(arch, 0, 1.0f) on the partner's
    // loadout); none left -- reason 9 / 10 and n = 0.
    int const PartnerMode = Inventory->Mode;
    if (PartnerMode == 0 || PartnerMode == 2 || PartnerMode == 3)
    {
        bool const bPackedGood =
            PartnerMode == 3 && (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId);
        if (!bPackedGood && Count > 1)
            Count = 1;

        if (bPackedGood)
        {
            Archetype::Ship const* const PlayerShipArch =
                Archetype::GetShip(GetPlayerShipArchetypeIdRetail()); // FUN_004c3e10
            if (PlayerShipArch != nullptr)
            {
                if (ArchId == G_NanobotArchId)
                {
                    EquipDesc const* const Carried =
                        Inventory->Ship.Loadout.find_matching_cargo(G_NanobotArchId, 0, 1.0f);
                    int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                    int const Limit = PlayerShipArch->NanobotLimit; // retail +0x140
                    int const Room = Limit - CarriedCount;
                    if (Count > Room && Limit >= 0)
                        Count = Room;
                    if (Count <= 0)
                    {
                        *OutReason = 9;
                        Count = 0;
                    }
                }
                else if (ArchId == G_ShieldBatteryArchId)
                {
                    EquipDesc const* const Carried =
                        Inventory->Ship.Loadout.find_matching_cargo(G_ShieldBatteryArchId, 0, 1.0f);
                    int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                    int const Limit = PlayerShipArch->ShieldBatteryLimit; // retail +0x144
                    int const Room = Limit - CarriedCount;
                    if (Count > Room && Limit >= 0)
                        Count = Room;
                    if (Count <= 0)
                    {
                        *OutReason = 10;
                        Count = 0;
                    }
                }
            }
        }
    }

    // 00483a56..00483ab2: the ammo filter (partner +0x32c == 1), with no reason yet: capped by
    // MAX_PLAYER_AMMO less the count carried; none left -- reason 11, 0. A reason already set
    // returns n as it stands.
    if (Inventory->Mode == 1)
    {
        if (*OutReason != 0)
            return Count;
        int Room = MAX_PLAYER_AMMO;
        EquipDesc const* const Carried = Inventory->Ship.Loadout.find_matching_cargo(ArchId, 0, 1.0f);
        if (Carried != nullptr)
            Room -= Carried->Count;
        if (Count > Room)
            Count = Room;
        if (Count <= 0)
        {
            *OutReason = 11;
            return 0;
        }
    }

    // 00483ab5..00483add: a reason, or a positive hold cap, returns n; otherwise reason 1, 0.
    if (*OutReason != 0)
        return Count;
    if (HoldCap > 0)
        return Count;
    *OutReason = 1;
    return 0;
}

void NnDealer::RestorePlayerShipView()
{
    // FUN_00482040.
    Field98c = 0;
    if (Inventory == nullptr)
        return;

    // 00482058..004820ae
    ContinueText->SetProperty(0x1e, 0x461, 0);
    ContinueButton->SetProperty(3, 0x330, 0);
    BuyText->SetProperty(0x1e, 0x48a, 0);
    SellText->SetProperty(0x1e, 0x48a, 0); // +0x9a8

    // 004820b4..004820d1: +0x9fc = FUN_004c6e90(partner ship arch (+0x348), 1), the value of
    // the partner's ship at this base.
    Field9fc = NnShipResaleValue(Inventory->Ship.ShipArchId, true);

    // 004820d7..00482109: the partner shows this dealer's ship and loadout.
    Inventory->Ship.ShipArchId = Ship.ShipArchId;
    Inventory->Ship.LoadShip(&Ship.Loadout, 1);
    Inventory->Ship.ShipView->BuildHardpointList(); // FUN_00586b70

    // 0048210e..00482115: tail jump to FUN_0047af30 on the partner.
    Inventory->TransferEquipmentFromDealer();
}

void NnDealer::ShowShipPackage(unsigned GoodId)
{
    // FUN_00483d40 (RET 4). Only the ship dealer (trade mode 2) shows packages.
    if (TradeMode != 2)
        return;

    // 00483d52..00483d73: no ship until the package resolves; the partner's filter mode.
    Ship.ShipArchId = 0;
    Mode = Inventory->Mode;
    GoodInfo const* const Package = GoodList::find_by_id(GoodId);
    if (Package == nullptr)
        return;

    // 00483d84..00483db3: remember the package, show its hull (the hull good is not
    // null-tested) with the package's first addon list (GoodInfo +0x94).
    Field97c = static_cast<int>(Package->Id);
    GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId);
    Ship.ShipArchId = Hull->ShipArchId;
    // Retail hands the GoodInfo's list itself to FUN_004c62b0; the port's GoodInfo keeps the
    // addons in a vector (common_goods.h), so they are copied into an EquipDescList first.
    EquipDescList Addons;
    Addons.Items.assign(Package->AddonLow.begin(), Package->AddonLow.end());
    Ship.LoadShip(&Addons, 0);

    // 00483db8..00483ddc: +0x9f8 = ftol(the hull's price in the market of the base the player
    // is at): FUN_0043a510(DAT_0066873c) on DAT_00668708 (null-tested), then
    // FUN_00438060(Hull->Id).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    if (Market != nullptr)
        Field9f8 = static_cast<int>(ClientMarketGetPrice(Market, Hull->Id));
}

unsigned NnDealer::GetSelectedMarketGood()
{
    // FUN_00483580. 0048359d..004835b9: a selection, and a commodity or equipment dealer.
    if (SelectedIndex == -1)
        return 0;
    if (TradeMode != 1 && TradeMode != 3)
        return 0;

    // 004835bf..004835c6: no room loaded (DAT_00668740) -- 0.
    if (GetClientLocationCurrentRoom() == 0)
        return 0;

    // 004835cc..00483645: FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested); its
    // goods of category TradeMode == 3 (FUN_00437f70) into a local list, ordered by
    // FUN_00483230 (trade mode 3) or FUN_00482f10 -- the same list RefreshMarketList shows.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, TradeMode == 3 ? 1u : 0u);
    if (TradeMode == 3)
        SortEquipmentMarketGoods(&Goods); // FUN_00483230
    else
        SortMarketGoodsByName(&Goods);    // FUN_00482f10

    // 00483645..004836a5: count the entries in stock (+0x1c > 0) -- in trade mode 3 only those
    // whose NnItemFilterMode(Good2Arch(good)) is this window's filter (+0x32c) -- and return
    // the good id (entry +0) of the SelectedIndex-th (0x004836f0 / 0x0048374f); 0 when the list
    // runs out (0x004836a7).
    int Row = 0; // EBP
    for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end(); ++It)
    {
        FClientMarketGood const* const Entry = *It;
        if (Entry->Stock <= 0)
            continue;
        if (TradeMode == 3 && NnItemFilterMode(Good2Arch(Entry->Info.GoodId)) != Mode)
            continue;
        if (Row == SelectedIndex)
            return Entry->Info.GoodId;
        ++Row;
    }
    return 0;
}

float NnDealer::GetBaseFactionFeeling()
{
    // FUN_00483df0 (cdecl).
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System == nullptr)
        return 1.0f; // DAT_005c75dc

    // 00483e10..00483e85: the system object the player's base docks with.
    unsigned const BaseId = GetPlayerBaseIdRetail(); // *FUN_004c4900 = DAT_00673358
    Universe::FSystemObject Object;
    unsigned const Count = (unsigned)System->GetObjectCount(); // vt+0x3c
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (!System->GetSystemObject(Index, &Object)) // vt+0x40
            continue;
        if (Object.DockWith != BaseId)
            continue;

        // 00483e97..00483f13: the feeling of the player's group towards the object's
        // reputation group; a non-zero result means none, 1.0.
        int const PlayerGroup = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670, DAT_0061226c
        TString<16> GroupName;
        GroupName = Object.Reputation.Data;
        unsigned const Group = Reputation::get_id(GroupName);
        float Feeling = 0.0f;
        if (Reputation::Vibe::GetGroupFeelingsTowards(PlayerGroup, Group, Feeling) != 0)
            return 1.0f;
        return Feeling;
    }
    return 1.0f;
}

void NnDealer::ApplyPlayerStatsFeeling(int Count, FPlayerStatsRepEntry const* Table)
{
    // FUN_00483f30 (cdecl, 2 args; the only caller is FUN_004a8e40 0x004a8e6c).
    // 00483f39..00483f4e: FUN_0059da10("NN_Dealer"), kept in [ESP+0xc].
    NnDealer* const Dealer = static_cast<NnDealer*>(Win3DFindByName("NN_Dealer"));
    if (Dealer == nullptr)
        return;
    // 00483f54..00483f67
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System == nullptr)
        return;

    // 00483f6d..00483fe5: the first system object whose dock_with (+0x80, [ESP+0xac]) is the
    // player's base; an index Slot16 refuses is passed over. The count is unsigned (JBE / JC).
    unsigned const BaseId = GetPlayerBaseIdRetail(); // *FUN_004c4900
    Universe::FSystemObject Object;
    unsigned const ObjectCount = (unsigned)System->GetObjectCount(); // vt+0x3c
    unsigned Index = 0;
    for (;;)
    {
        if (Index >= ObjectCount)
            return;
        if (System->GetSystemObject(Index, &Object) && Object.DockWith == BaseId) // vt+0x40
            break;
        ++Index;
    }

    // 00483ff1: FUN_004c4670 is called and its result dropped (EAX is cleared by the XOR at
    // 00483ff9); it only reads DAT_0061226c, so the call has no effect.
    // 00483ff6..00484036: the object's reputation nickname (+0x60) into a TString<16>, the
    // length clamped to 15.
    TString<16> GroupName;
    GroupName = Object.Reputation.Data;
    // 0048403f: Reputation::get_id; -1 ends the call.
    unsigned const Group = Reputation::get_id(GroupName);
    if (Group == 0xffffffffu)
        return;

    // 0048404d..0048407e: the first entry with that group id (signed count, JLE / JL) hands
    // its value to the dealer's +0x990.
    for (int Entry = 0; Entry < Count; ++Entry)
    {
        if (Table[Entry].GroupId == Group)
        {
            Dealer->BaseFeeling = Table[Entry].Feeling;
            return;
        }
    }
}
