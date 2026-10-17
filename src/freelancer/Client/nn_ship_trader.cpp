// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_ship_trader.h"

#include "character_select_equip.h" // GetPlayerShipArchetypeIdRetail, GetPlayerCreditsRetail,
                                    // GetPlayerEquipDescListRetail, GetPlayerRankLevelRetail
#include "client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510) and the market
#include "hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"          // GetClientMouseButtons (DAT_00616850)
#include "menu_ui_widgets.h"        // HudMessageQueueRequestRankRefresh (FUN_004c55f0)
#include "menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c),
                                    // GetClientCurrentBaseStridName (FUN_0043bbe0)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nn_dealer.h"              // NnDealer::GetBaseFactionFeeling (FUN_00483df0)
#include "nav_bar.h"                // NavBarCloseService (FUN_004424f0), NavBarShowDealerTab
                                    // (FUN_00442850), NavBarOpenDealer (FUN_004421a0)
#include "nn_inventory.h"           // resale values
#include "nn_text_control.h"        // NnTextControl::GetFontString (+0x334)
#include "nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "rich_text_win.h"
#include "ship_view_control.h"
#include "win3d.h"                  // Win3DFindByName, Win3DDeleteTree, Win3DGetMouseRay
#include "win_factory.h"
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "common_archetype.h"
#include "common_goods.h"
#include "common_misc.h"            // SinglePlayer
#include "common_text.h"            // RenderDisplayList, StyleNode

#include <list>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_006722d0 -- WINFACTORY<NN_ShipTrader> static (LAB_004b7410 -> FUN_004b96b0; Create
// FUN_004b9720; atexit LAB_004b7430 -> FUN_00579380).
WINFACTORY<NnShipTrader> G_NnShipTraderFactory("NN_ShipTrader");

namespace
{
    char const kShipTraderMesh[] = "Interface\\NeuroNet\\Inventory\\INV_biginforcard.cmp";
    char const kShipTraderCloseMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db";
    char const kWelcomeLineMesh[] = "Interface\\NeuroNet\\Inventory\\partitionline.3db";

    // DAT_00611f78 -- the class captions of Archetype::Ship::ShipClass 0..3 (read only by this
    // window: 0x004b79ca, 0x004b808d, 0x004b8be7, 0x004b8fd2, 0x004b95b1).
    int G_ShipTraderClassIds[4] = {0x39b, 0x39c, 0x39d, 0x39e};

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // The TextControl idiom after WelcomeText / WelcomeHelp / ErrorHelp (0x004b8208, 0x004b82ee,
    // 0x004b8430): the label's Font::String (+0x334) goes multi-line and drops its layout, unless
    // it already is.
    void MakeMultiLine(WinControl* Control)
    {
        FFontString* const Text = static_cast<NnTextControl*>(Control)->GetFontString();
        if (Text->bMultiLine != 1)
        {
            Text->bMultiLine = 1; // +0x30
            Text->LayoutGen = 0;  // +0x28
        }
    }

    // A style node (0x9ca4) and the ids text -- the three info panes of SelectRow.
    void BuildInfoText(RenderDisplayList& Text, int Ids)
    {
        {
            StyleNode Style(0x9ca4);
            RdlAppendNode(Text, Style.Clone());
        }
        LoadIdsIntoRdl(static_cast<unsigned>(Ids), Text); // result not read
    }
}

NnShipTrader::NnShipTrader()
{
    // FUN_004b9720 004b9759..004b9777: FUN_0059f860 and the vptrs. Nothing else is written
    // before Create.
}

NnShipTrader::~NnShipTrader()
{
    // FUN_004b9800: FUN_00576010 on the +0x32c face, then FUN_0059fa50 -- the Event::Handler and
    // WinControl destructors, which C++ runs in that order.
}

int NnShipTrader::RowDword(size_t ArrayOffset, int Index) const
{
    int Value = 0;
    unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(this);
    memcpy(&Value, Bytes + ArrayOffset + static_cast<ptrdiff_t>(Index) * 4, sizeof(Value));
    return Value;
}

void NnShipTrader::Close()
{
    // e.g. 0x004b8883..0x004b8890: FUN_0043d2a0 on every child (the next sibling read first),
    // slot 10, then slot 0 with 1.
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

bool NnShipTrader::Create(Win3D* NewParent, void* Data)
{
    // FUN_004b7440 (RET 8).
    //
    // 004b745b..004b74b3: the market of the base the player is at and its ships (category 3).
    FClientMarket const* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Ships;
    RowCount = ClientMarketCollectCategory(Market, &Ships, 3);
    if (RowCount == 0)
        return false; // 004b74bb..004b74eb: the list erased and freed

    // 004b74f0..004b7576
    Hovered = -1;
    Selected = -1;
    bDragStart = 1;
    SetMesh(kShipTraderMesh, 0); // slot 38
    HalfExtents[0] = 0.66f;      // +0xb4
    HalfExtents[1] = 0.64f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.64f;        // +0xc0
    HitOffset[1] = -0.27f;
    HitOffset[2] = 0.0f;

    // 004b7582..004b75e4: the close button.
    {
        float const Position[3] = {1.25f, -0.04f, 0.0f};
        float const Half[3] = {0.02f, 0.02f, 0.0f};
        CancelButton = CreateButtonControl("ShipTraderCancelButton", kShipTraderCloseMesh, nullptr,
                                           "ui_select_item", nullptr, Position, nullptr, Half, 0,
                                           0, 0, 0, 0x2148, 0, 0);
    }

    // 004b75ea..004b75f2: at most three rows.
    if (RowCount > 3)
        RowCount = 3;

    // 004b75f8..004b7ad5: one row per market ship.
    char Name[64];
    wchar_t PriceText[0x100];
    std::list<FClientMarketGood const*>::const_iterator Node = Ships.begin();
    for (int Row = 0; Row < RowCount; ++Row)
    {
        int NameIds = 0;   // [ESP+0x24]
        int Price = 0;     // [ESP+0x20]
        unsigned const GoodId = (*Node)->Info.GoodId; // the entry's +0x00
        RowGoodIds[Row] = GoodId;
        GoodInfo const* const Package = GoodList::find_by_id(GoodId);
        RowPackages[Row] = Package;
        RowShipClasses[Row] = 0;
        if (Package != nullptr)
        {
            // 004b7657..004b7685: the hull good (not null-tested) and its ship (not null-tested).
            GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId); // +0x90
            Archetype::Ship const* const Ship = Archetype::GetShip(Hull->ShipArchId); // +0x54
            RowShipClasses[Row] = Ship->ShipClass; // +0xf8
            if (RowShipClasses[Row] < 0 || RowShipClasses[Row] >= 4)
                RowShipClasses[Row] = 0;

            // 004b768e..004b76b5: the ship's name and the hull's market price.
            NameIds = Ship->IdsName; // +0x14
            Price = static_cast<int>(ClientMarketGetPrice(Market, Hull->Id));

            // 004b76ab..004b7724: plus every addon of the package (GoodInfo +0x94, node +0xc arch,
            // +0x18 health, +0x1c count) that is a good: ftol(price * health) * count.
            for (std::vector<EquipDesc>::const_iterator It = Package->AddonLow.begin();
                 It != Package->AddonLow.end(); ++It)
            {
                unsigned const AddonGood = Arch2Good(It->ArchId);
                if ((AddonGood & 0x3fffffffu) == 0)
                    continue;
                int const Unit = static_cast<int>(
                    static_cast<double>(ClientMarketGetPrice(Market, AddonGood)) *
                    static_cast<double>(It->Health));
                Price += It->Count * Unit;
            }

            // 004b7726..004b7816: the ship good's icon, when there is one.
            GoodInfoList const* const Goods = GoodList_get();
            GoodInfo const* ShipGood = nullptr;
            if (Goods != nullptr)
                ShipGood = Goods->find_by_ship_arch(Hull->ShipArchId);
            if (ShipGood != nullptr)
            {
                sprintf(Name, "ShipTraderShipMesh%d", Row);
                float const Position[3] = {
                    0.277f,
                    static_cast<float>(0.13f - static_cast<double>(Row) * 0.068f), // DAT_005d0434 / 005d4958
                    -1.288f};
                float const Half[3] = {0.036f, 0.041f, 0.0f};
                float const Zero[3] = {0.0f, 0.0f, 0.0f};
                ShipMeshes[Row] = CreateStaticControl(Name, ShipGood->ItemIcon.Value, Position, nullptr,
                                                      Half, 0, 0, 0, 1, 1, 1, 1, 0x50, 0, 0, Zero);
                ShipMeshes[Row]->Flags = static_cast<unsigned char>(ShipMeshes[Row]->Flags & 0xf7);
                RowMeshPaths[Row] = ShipGood->ItemIcon.Value; // +0x78
            }
        }

        // 004b7819..004b78c2: the icon frame. The row offset is stored as a float first.
        float const RowStep = static_cast<float>(static_cast<double>(Row) * 0.068f);
        float const FrameY = static_cast<float>(-0.2f - static_cast<double>(RowStep)); // DAT_005d58c8
        {
            sprintf(Name, "ShipTraderShipNameIconFrame%d", Row);
            float const Position[3] = {0.916f, FrameY, 0.0f};
            float const Half[3] = {0.035f, 0.0335f, 0.0f};
            NameIconFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", Position, Half, 0,
                                                          0, 0, 0, 0);
        }

        // 004b78b4..004b793b: the name frame.
        {
            sprintf(Name, "ShipTraderShipNameFrame%d", Row);
            float const Position[3] = {1.098f, FrameY, 0.0f};
            float const Half[3] = {0.138f, 0.034f, 0.0f};
            NameFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", Position, Half, 0, 0,
                                                      0, 0, 0);
        }

        // 004b793a..004b79b8: the name.
        {
            sprintf(Name, "ShipTraderShipName%d", Row);
            float const Position[3] = {
                0.97f, static_cast<float>(-0.167f - static_cast<double>(RowStep)), 0.0f}; // DAT_005d5890
            ShipNames[Row] = CreateTextControl(Name, "ItemName", Position, nullptr, 0.264f, 0.03f,
                                               1.6f, static_cast<unsigned>(NameIds), 0, 0, 1);
            RowNameIds[Row] = NameIds;
        }

        // 004b79a7..004b7a2b: the class. Its y is kept for the price.
        float const ClassY = static_cast<float>(-0.195f - static_cast<double>(RowStep)); // DAT_005d5874
        {
            sprintf(Name, "ShipTraderShipClass%d", Row);
            float const Position[3] = {0.97f, ClassY, 0.0f};
            RowClassTexts[Row] = CreateTextControl(
                Name, "ItemName", Position, nullptr, 0.15f, 0.03f, 1.6f,
                static_cast<unsigned>(G_ShipTraderClassIds[RowShipClasses[Row]]), 0, 0, 1);
        }

        // 004b7a1d..004b7ab4: the price -- created with the name's ids (retail pushes [ESP+0x24]),
        // then given the formatted price.
        {
            sprintf(Name, "ShipTraderShipPrice%d", Row);
            float const Position[3] = {1.078f, ClassY, 0.0f};
            ShipPrices[Row] = CreateTextControl(Name, "ItemName", Position, nullptr, 0.153f, 0.03f,
                                                1.6f, static_cast<unsigned>(NameIds), 2, 0, 1);
            FormatGroupedNumber(PriceText, Price, -1); // FUN_004779a0
            ShipPrices[Row]->SetProperty(0x1d, PointerArg(PriceText), 0);
        }

        // 004b7ab4..004b7ad5
        RowPrices[Row] = Price;
        ++Node;
    }

    // 004b7add..004b7bca: the 3D view.
    {
        float const RotSpeed[3] = {0.0f, 0.0f, 0.0f};
        float const ViewOffset[3] = {0.005f, 0.078f, -1.288f};
        float const Ypr[3] = {0.0f, 0.0f, 0.0f};
        float const ViewSize[3] = {0.18f, -0.162f, 0.0f};
        unsigned const RectColor = 0xffffdc0cu; // bytes 0c dc ff ff
        unsigned const WireColor = 0xffffffffu;
        ShipView = static_cast<ShipViewControl*>(CreateShipViewControl(
            "ShipTrader3DShip", RotSpeed, ViewOffset, Ypr, ViewSize, nullptr, false, false,
            &RectColor, &WireColor));
    }

    // 004b7ba4..004b7c36: the view's name and class.
    {
        float const Position[3] = {0.647f, -0.031f, 0.0f};
        ViewName = CreateTextControl("ShipTraderViewName", "PageHeader", Position, nullptr, 0.367f,
                                     0.03f, 1.6f, 0, 1, 0, 0);
    }
    {
        float const Position[3] = {0.647f, -0.07f, 0.0f};
        ViewClass = CreateTextControl("ShipTraderViewClass", "SubHeader", Position, nullptr,
                                      0.367f, 0.03f, 1.6f, 0, 1, 0, 0);
    }

    // 004b7c3c..004b7cdf: the buy button and its caption.
    {
        float const Position[3] = {0.642f, -0.472f, 0.0f};
        float const Half[3] = {0.15f, 0.026f, 0.0f};
        BuyButton = CreateWireButtonControl("ShipTraderBuyButton", "ui_select_item", Position, Half,
                                            0, 0, 0x325, 0, 0);
    }
    {
        float const Position[3] = {0.64f, -0.465f, 0.0f};
        BuyText = CreateTextControl("ShipTraderBuytext", "ButtonText", Position, nullptr, 0.198f,
                                    0.03f, 1.6f, 0x39a, 1, 0, 0);
    }

    // 004b7cdd..004b7da2: the three info panes.
    {
        float const Offset[3] = {-0.42f, -0.164f, -1.0f};
        InfoFacts = CreateRichTextWin("ShipInfoCard_Facts", 0.4f, 0.2f, Offset, true);
    }
    {
        float const Offset[3] = {0.01f, -0.164f, -1.0f};
        InfoStats = CreateRichTextWin("ShipInfoCard_Stats", 0.24f, 0.2f, Offset, true);
    }
    {
        float const Offset[3] = {0.27f, -0.164f, -1.0f};
        InfoValues = CreateRichTextWin("ShipInfoCard_Values", 0.14f, 0.2f, Offset, true);
    }

    // 004b7d8e..004b7e11: the player's cash.
    {
        float const Position[3] = {0.23f, -0.137f, 0.0f};
        PlayerCash = CreateTextControl("PlayerCash", "Normal", Position, nullptr, 0.413f, 0.03f,
                                       1.6f, 0, 1, 0, 0);
        wchar_t CashText[0x100];
        FormatGroupedNumber(CashText, static_cast<int>(GetPlayerCreditsRetail()), 0x476); // FUN_004c4ce0
        PlayerCash->SetProperty(0x1d, PointerArg(CashText), 0);
    }

    // 004b7e11..004b7e53: the title (not kept).
    {
        float const Position[3] = {1.07f, -0.085f, 0.0f};
        CreateTextControl("ShipTraderTitle", "PageHeader", Position, nullptr, 0.422f, 0.03f, 1.6f,
                          0x460, 1, 0, 0);
    }

    // 004b7e58..004b814d: the player's current ship.
    CurrentShipButton = nullptr;
    CurrentShipFrame = nullptr;
    unsigned const PlayerShipArchId = GetPlayerShipArchetypeIdRetail(); // FUN_004c3e10
    if (PlayerShipArchId != 0)
    {
        {
            float const Position[3] = {0.235f, -0.085f, 0.0f};
            CreateTextControl("ShipTraderCurrentShipText", "PageHeader", Position, nullptr, 0.41f,
                              0.03f, 1.6f, 0x62e, 1, 0, 0);
        }
        Archetype::Ship const* const Ship = Archetype::GetShip(PlayerShipArchId);
        if (Ship != nullptr)
        {
            // 004b7ece..004b7f76: the ship good's icon, when there is one.
            GoodInfoList const* const Goods = GoodList_get();
            if (Goods != nullptr)
            {
                GoodInfo const* const ShipGood = Goods->find_by_ship_arch(PlayerShipArchId);
                if (ShipGood != nullptr)
                {
                    float const Position[3] = {-0.557f, 0.13f, -1.288f};
                    float const Half[3] = {0.035f, 0.041f, 0.0f};
                    float const Zero[3] = {0.0f, 0.0f, 0.0f};
                    CurrentShipMesh = CreateStaticControl("ShipTraderCurrentShipMesh",
                                                          ShipGood->ItemIcon.Value, Position, nullptr,
                                                          Half, 0, 0, 0, 1, 1, 1, 1, 0x50, 0, 0,
                                                          Zero);
                    CurrentShipMesh->Flags = static_cast<unsigned char>(CurrentShipMesh->Flags & 0xf7);
                }
            }

            // 004b7f7a..004b802a: its icon frame and name frame.
            {
                float const Position[3] = {0.082f, -0.2f, 0.0f};
                float const Half[3] = {0.035f, 0.0335f, 0.0f};
                CurrentShipButton = CreateWireButtonControl("ShipTraderCurrentShip",
                                                            "ui_item_select", Position, Half, 0, 0,
                                                            0, 0, 0);
            }
            {
                float const Position[3] = {0.264f, -0.2f, 0.0f};
                float const Half[3] = {0.138f, 0.034f, 0.0f};
                CurrentShipFrame = CreateWireButtonControl("ShipTraderCurrentShipFrame",
                                                           "ui_item_select", Position, Half, 0, 0,
                                                           0, 0, 0);
            }

            // 004b802a..004b8067: the name (not kept).
            {
                float const Position[3] = {0.135f, -0.167f, 0.0f};
                CreateTextControl("ShipTraderCurrentShipName", "ItemName", Position, nullptr, 0.26f,
                                  0.03f, 1.6f, static_cast<unsigned>(Ship->IdsName), 0, 0, 1);
            }

            // 004b806c..004b80d2: the class (not kept).
            CurrentShipClass = Ship->ShipClass; // +0xf8
            if (CurrentShipClass < 0 || CurrentShipClass >= 4)
                CurrentShipClass = 0;
            {
                float const Position[3] = {0.135f, -0.198f, 0.0f};
                CreateTextControl("ShipTraderCurrentShipClass", "ItemName", Position, nullptr,
                                  0.146f, 0.03f, 1.6f,
                                  static_cast<unsigned>(G_ShipTraderClassIds[CurrentShipClass]), 0,
                                  0, 1);
            }

            // 004b80d7..004b814d: what the market pays for it (FUN_004c6e90(arch, 1)).
            int const ShipValue = NnShipResaleValue(PlayerShipArchId, true);
            float const Position[3] = {0.258f, -0.198f, 0.0f};
            WinControl* const PriceControl = CreateTextControl(
                "ShipTraderCurrentShipPrice", "ItemName", Position, nullptr, 0.14f, 0.03f, 1.6f, 1,
                2, 0, 1);
            wchar_t ValueText[0x100];
            FormatGroupedNumber(ValueText, ShipValue, -1);
            PriceControl->SetProperty(0x1d, PointerArg(ValueText), 0);
        }
    }

    // 004b8153..004b81ba: the view and the buy controls start hidden. 004b81b2..004b81ba:
    // FUN_0053e4a0 (`RET 4`) on the view with 1 -- nothing.
    ShipView->SetProperty(1, 1, 0);
    BuyButton->SetProperty(1, 1, 0);
    BuyText->SetProperty(1, 1, 0);
    ViewName->SetProperty(1, 1, 0);
    ViewClass->SetProperty(1, 1, 0);

    // 004b81bf..004b821b: the welcome title.
    {
        float const Position[3] = {0.645f, -0.061f, 0.0f};
        WelcomeText = CreateTextControl("WelcomeText", "SubHeader", Position, nullptr, 0.37f,
                                        0.06f, 1.6f, 0, 1, 0, 0);
        MakeMultiLine(WelcomeText);
    }

    // 004b821b..004b82c5: the partition line.
    {
        float const Position[3] = {0.0f, 0.159f, -1.288f};
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        WelcomeLine = CreateStaticControl("WelcomeLine", kWelcomeLineMesh, Position, nullptr, Zero,
                                          0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    }

    // 004b829c..004b8301: the welcome help.
    {
        float const Position[3] = {0.657f, -0.217f, 0.0f};
        WelcomeHelp = CreateTextControl("WelcomeHelp", "Normal", Position, nullptr, 0.35f, 0.06f,
                                        1.4f, 0x485, 0, 0, 0);
        MakeMultiLine(WelcomeHelp);
    }

    // 004b8301..004b83c9: with a base name, the title is swprintf(ids 0x477, base name,
    // ids 0x48d) upper-cased (FUN_005b74d9, the delay-loaded CharUpperW).
    int const BaseNameIds = GetClientCurrentBaseStridName(); // FUN_0043bbe0
    if (BaseNameIds != 0)
    {
        wchar_t Dealer[0x40];     // [ESP+0x240]
        wchar_t BaseName[0x40];   // [ESP+0x1c0]
        wchar_t Format[0x40] = {}; // [ESP+0x2c0] -- retail does not test this load
        wchar_t Title[0x100];     // [ESP+0x340]
        if (GetIdsStringWWithRawFallback(0x48d, Dealer, 0x40) == 0)
            Dealer[0] = 0;
        if (GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseNameIds), BaseName, 0x40) == 0)
            BaseName[0] = 0;
        GetIdsStringWWithRawFallback(0x477, Format, 0x3f);
        swprintf(Title, 0x100, Format, BaseName, Dealer);
        CharUpperW(Title);
        WelcomeText->SetProperty(0x1d, PointerArg(Title), 0);
    }

    // 004b83cf..004b846e: the error line, hidden, multi-line, white.
    {
        float const Position[3] = {0.657f, -0.438f, 0.0f};
        ErrorHelp = CreateTextControl("ErrorHelp", "Normal", Position, nullptr, 0.37f, 0.06f, 1.4f,
                                      0x49a, 0, 0, 0);
        ErrorHelp->SetProperty(1, 1, 0);
        MakeMultiLine(ErrorHelp);
        unsigned char const White[4] = {0xff, 0xff, 0xff, 0xff};
        ErrorHelp->SetProperty(0x33, PointerArg(White), 1);
    }

    // 004b846e..004b84af: what the player is worth -- cash, loadout and ship at this market.
    PlayerWorth = static_cast<int>(GetPlayerCreditsRetail());
    PlayerWorth += NnLoadoutResaleValue(&GetPlayerEquipDescListRetail(), nullptr, nullptr); // FUN_004c6f70
    PlayerWorth += NnShipResaleValue(GetPlayerShipArchetypeIdRetail(), true);              // FUN_004c6e90

    // 004b84b5..004b84f7: the draw layer, the offset (slot 31) and the position (slot 5).
    DrawLayer = 100;
    SetOffset(-0.64f, 0.33f, -1.288f);
    SetPosition(Position);

    // 004b84fa..004b8516
    BaseFeeling = NnDealer::GetBaseFactionFeeling(); // FUN_00483df0
    if (SinglePlayer())
        HudMessageQueueRequestRankRefresh(); // FUN_004c55f0
    RefreshRows();

    // 004b851b..004b8539: the cancel handler, then Win3D's Create; the ship list goes either way.
    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    return Win3D::Create(NewParent, Data);      // FUN_004b5060
}

void NnShipTrader::RefreshRows()
{
    // FUN_004b93f0. 004b93fc..004b941b: the rank level and the market.
    int const Level = GetPlayerRankLevelRetail(); // FUN_004c55d0
    RankLevel = Level;
    FClientMarket const* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());

    // 004b941f..004b94b6: every row's status.
    for (int Row = 0; Row < RowCount; ++Row)
    {
        unsigned const GoodId = RowGoodIds[Row];
        RowIndices[Row] = Row;
        RowStatus[Row] = 0;
        float const MinLevel = ClientMarketGetMinLevel(Market, GoodId); // FUN_00438160
        float const MinRep = ClientMarketGetMinRep(Market, GoodId);     // FUN_004381c0
        RowRequiredLevels[Row] = static_cast<int>(MinLevel);

        // 004b9465..004b9495: a known level (0..0x3e6) below the requirement (FILD / FCOMP,
        // TEST AH,5 / JP: no jump is "below") -- in single player the row is locked by rank, and
        // the reputation test is skipped.
        if (Level >= 0 && Level < 0x3e7 && static_cast<double>(Level) < static_cast<double>(MinLevel))
        {
            if (SinglePlayer())
                RowStatus[Row] = 1;
            continue;
        }

        // 004b9497..004b94a4: FCOMP BaseFeeling; TEST AH,0x41 / JNZ keeps a requirement that is
        // not above the feeling; above it the row is locked by reputation.
        if (MinRep > BaseFeeling)
            RowStatus[Row] = 2;
    }

    // 004b94c0..004b9561: bubble sort by status (a swap sets DL = 0 and runs another pass).
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Row = 0; Row < RowCount - 1; ++Row)
        {
            if (RowStatus[Row] <= RowStatus[Row + 1])
                continue;
            int const StatusTemp = RowStatus[Row];
            RowStatus[Row] = RowStatus[Row + 1];
            RowStatus[Row + 1] = StatusTemp;
            int const LevelTemp = RowRequiredLevels[Row];
            RowRequiredLevels[Row] = RowRequiredLevels[Row + 1];
            RowRequiredLevels[Row + 1] = LevelTemp;
            int const ClassTemp = RowShipClasses[Row];
            RowShipClasses[Row] = RowShipClasses[Row + 1];
            RowShipClasses[Row + 1] = ClassTemp;
            int const PriceTemp = RowPrices[Row];
            RowPrices[Row] = RowPrices[Row + 1];
            RowPrices[Row + 1] = PriceTemp;
            GoodInfo const* const PackageTemp = RowPackages[Row];
            RowPackages[Row] = RowPackages[Row + 1];
            RowPackages[Row + 1] = PackageTemp;
            unsigned const GoodTemp = RowGoodIds[Row];
            RowGoodIds[Row] = RowGoodIds[Row + 1];
            RowGoodIds[Row + 1] = GoodTemp;
            char const* const MeshTemp = RowMeshPaths[Row];
            RowMeshPaths[Row] = RowMeshPaths[Row + 1];
            RowMeshPaths[Row + 1] = MeshTemp;
            int const NameTemp = RowNameIds[Row];
            RowNameIds[Row] = RowNameIds[Row + 1];
            RowNameIds[Row + 1] = NameTemp;
            int const IndexTemp = RowIndices[Row];
            RowIndices[Row] = RowIndices[Row + 1];
            RowIndices[Row + 1] = IndexTemp;
            bSorted = false;
        }
    }

    // 004b9567..004b9699: the row controls. A locked row is drawn dimmed (property 0x33's B).
    wchar_t PriceText[0x100]; // [ESP+0x1c]
    for (int Row = 0; Row < RowCount; ++Row)
    {
        ShipMeshes[Row]->SetProperty(4, PointerArg(RowMeshPaths[Row]), 0);
        ShipNames[Row]->SetProperty(0x1e, static_cast<uintptr_t>(RowNameIds[Row]), 0);
        RowClassTexts[Row]->SetProperty(
            0x1e, static_cast<uintptr_t>(G_ShipTraderClassIds[RowShipClasses[Row]]), 0);
        FormatGroupedNumber(PriceText, RowPrices[Row], -1); // FUN_004779a0
        ShipPrices[Row]->SetProperty(0x1d, PointerArg(PriceText), 0);

        uintptr_t const bLocked = RowStatus[Row] != 0 ? 1 : 0;
        unsigned char const FrameColor[4] = {0x80, 0x80, 0x80, 0xff};
        NameFrames[Row]->SetProperty(0x33, PointerArg(FrameColor), bLocked);
        NameIconFrames[Row]->SetProperty(0x33, PointerArg(FrameColor), bLocked);
        unsigned char const TextColor[4] = {0xc0, 0xc0, 0xc0, 0xff};
        ShipNames[Row]->SetProperty(0x33, PointerArg(TextColor), bLocked);
        RowClassTexts[Row]->SetProperty(0x33, PointerArg(TextColor), bLocked);
        ShipPrices[Row]->SetProperty(0x33, PointerArg(TextColor), bLocked);
    }
}

void NnShipTrader::Update()
{
    // FUN_004b93d0: the children (FUN_005a17b0), then a refresh when the rank level moved.
    Win3D::Update();
    if (GetPlayerRankLevelRetail() != RankLevel) // FUN_004c55d0
        RefreshRows();
}

void NnShipTrader::Draw()
{
    // LAB_004b9260. 004b9267..004b92dc: every child's layer -- 1 for the hovered row's frames,
    // 2 for the selected row's (the second test wins), 0 otherwise.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        Child->DrawLayer = 0;
        if (Hovered >= 0)
        {
            WinControl* const Control = Child->AsWinControl(); // slot 2
            if (Control == NameFrames[Hovered] || Control == NameIconFrames[Hovered])
                Child->DrawLayer = 1;
        }
        if (Selected >= 0)
        {
            WinControl* const Control = Child->AsWinControl();
            if (Control == NameFrames[Selected] || Control == NameIconFrames[Selected])
                Child->DrawLayer = 2;
        }
    }

    // 004b92de..004b9309: the two frame columns, then WinControl's Draw.
    WireControlsJoinColumn(NameFrames, RowCount);     // FUN_005a4b50
    WireControlsJoinColumn(NameIconFrames, RowCount);
    WinControl::Draw(); // FUN_005a0760

    // 004b930e..004b9362: under the mouse, a held button (DAT_00616850 & 3) drags the ship round,
    // the first frame of a drag starting it; otherwise the next drag starts afresh.
    float Ray[3];
    if (ShipView->HitTest(Win3DGetMouseRay(Ray))) // FUN_0059da70, slot 4
    {
        if ((GetClientMouseButtons() & 3) != 0)
        {
            ShipView->DragRotate(bDragStart != 0); // FUN_00584c80
            bDragStart = 0;
        }
        else
        {
            bDragStart = 1;
        }
    }
}

bool NnShipTrader::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // LAB_004b89a0 (RET 8): a key press (bit 1) of Escape closes the window. Retail tests `this`
    // (0x004b89c2) before the close, which is never null here.
    if ((*Modifiers & 2) != 0 && Key == 0x1b)
    {
        NavBarCloseService(false); // FUN_004424f0(0)
        NavBarShowDealerTab();     // FUN_00442850
        Close();
    }
    return false;
}

intptr_t NnShipTrader::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004b85f0 (RET 0xc): jump table 0x004b8958 over Msg - 0x26 (bytes at 0x004b8970). B is
    // not read; nothing goes to WinControl's SetProperty.
    (void)B;
    WinControl* const Sender = reinterpret_cast<WinControl*>(A);
    switch (Msg)
    {
    case 0x26:
    {
        // caseD_26: a click.
        if (Sender == CancelButton)
        {
            NavBarCloseService(false);
            NavBarShowDealerTab();
            Close();
            return 1;
        }
        if (Sender == BuyButton)
        {
            // 0x004b88a6..0x004b88e7: the selected row's market order, the window closed, the
            // NavBar opens the dealer with it.
            int const Index = RowIndices[Selected];
            Close();
            NavBarOpenDealer(Index); // FUN_004421a0
            return 1;
        }
        for (int Row = 0; Row < RowCount; ++Row)
        {
            if (NameFrames[Row] == Sender || NameIconFrames[Row] == Sender)
            {
                SelectRow(Row);
                return 1;
            }
        }
        if (Sender == CurrentShipButton || Sender == CurrentShipFrame)
        {
            SelectRow(-2);
            return 1;
        }
        return 0;
    }
    case 0x27:
    {
        // caseD_27: a double click buys a row the player can afford and is not locked out of.
        int Row = 0;
        for (; Row < RowCount; ++Row)
        {
            if (NameFrames[Row] == Sender || NameIconFrames[Row] == Sender)
                break;
        }
        if (Row >= RowCount)
            return 0;
        if (RowPrices[Row] > PlayerWorth)
            return 0;
        if (RowStatus[Row] != 0)
            return 0;
        int const Index = RowIndices[Row];
        NavBarCloseService(false);
        NavBarShowDealerTab();
        Close();
        NavBarOpenDealer(Index);
        return 1;
    }
    case 0x2c:
    case 0x2d:
    {
        // caseD_2c / caseD_2d: hover in / out. The row's other frame (or the current ship's
        // other frame) gets the same message; a row updates Hovered (row / -1).
        int const Property = Msg;
        if (Sender == CurrentShipButton)
        {
            CurrentShipFrame->SetProperty(Property, 0, 0);
            return 0;
        }
        if (Sender == CurrentShipFrame)
        {
            CurrentShipButton->SetProperty(Property, 0, 0);
            return 0;
        }
        for (int Row = 0; Row < RowCount; ++Row)
        {
            WinControl* const Frame = NameFrames[Row];
            if (Sender != Frame && Sender != NameIconFrames[Row])
                continue;
            if (Sender != Frame)
                Frame->SetProperty(Property, 0, 0);
            if (Sender != NameIconFrames[Row])
                NameIconFrames[Row]->SetProperty(Property, 0, 0);
            Hovered = Msg == 0x2c ? Row : -1;
        }
        return 0;
    }
    case 0x4e:
        // caseD_4e: close. Retail tests `this` (0x004b8816), never null here.
        NavBarCloseService(false);
        NavBarShowDealerTab();
        Close();
        return 0;
    default:
        return 0;
    }
}

bool NnShipTrader::OnEvent(unsigned MsgId)
{
    // LAB_004b9370 (RET 4), the Event::Handler face (this - 0x32c): USER_CANCEL closes the window.
    if (MsgId != 0x10)
        return false;
    NavBarCloseService(false);
    NavBarShowDealerTab();
    Close();
    return true;
}

void NnShipTrader::SelectRow(int Row)
{
    // FUN_004b8a00 (RET 4).
    Selected = Row;

    // 004b8a27..004b8a7e: the rows' frames checked (property 0xc) only for Row.
    for (int Index = 0; Index < RowCount; ++Index)
    {
        uintptr_t const bChecked = Index == Row ? 1 : 0;
        NameFrames[Index]->SetProperty(0xc, bChecked, 0);
        NameIconFrames[Index]->SetProperty(0xc, bChecked, 0);
    }

    // 004b8a80..004b8ac3: the current ship's frames (not tested for null on -2).
    if (Selected == -2)
    {
        CurrentShipButton->SetProperty(0xc, 1, 0);
        CurrentShipFrame->SetProperty(0xc, 1, 0);
    }
    else if (CurrentShipButton != nullptr)
    {
        CurrentShipButton->SetProperty(0xc, 0, 0);
        CurrentShipFrame->SetProperty(0xc, 0, 0);
    }

    // 004b8ac9..004b8b56: the view shown, the welcome hidden.
    ShipView->SetProperty(1, 0, 0);
    BuyButton->SetProperty(1, 0, 0);
    BuyText->SetProperty(1, 0, 0);
    ViewName->SetProperty(1, 0, 0);
    ViewClass->SetProperty(1, 0, 0);
    WelcomeText->SetProperty(1, 1, 0);
    WelcomeLine->SetProperty(1, 1, 0);
    WelcomeHelp->SetProperty(1, 1, 0);

    // 004b8b5c..004b8c16 / 004b8f83..004b9005: the ship in the view and its three info ids.
    int FactsIds = 0;  // EBP
    int StatsIds = 0;  // [ESP+0x10]
    int ValuesIds = 0; // [ESP+0x14]
    if (Selected != -2)
    {
        GoodInfo const* const Package = RowPackages[Row];
        if (Package != nullptr)
        {
            GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId); // +0x90
            Archetype::Ship const* const Ship = Archetype::GetShip(Hull->ShipArchId);
            ShipView->LoadShip(&Ship->Nickname, true, false); // FUN_00585a90(ship + 8, 1, 0)
            // FUN_005850f0(package + 0x94): the port's GoodInfo keeps the addons in a vector
            // (common_goods.h), so they go through an EquipDescList; LoadEquipment copies it.
            EquipDescList Addons;
            Addons.Items.assign(Package->AddonLow.begin(), Package->AddonLow.end());
            ShipView->LoadEquipment(&Addons);
            ViewName->SetProperty(0x1e, static_cast<uintptr_t>(Ship->IdsName), 0);
            ViewClass->SetProperty(
                0x1e, static_cast<uintptr_t>(G_ShipTraderClassIds[RowShipClasses[Row]]), 0);
            FactsIds = Ship->IdsInfo1;  // +0xec
            StatsIds = Ship->IdsInfo2;  // +0xf0
            ValuesIds = Ship->IdsInfo3; // +0xf4
        }
    }
    else
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
        ShipView->LoadShip(&Ship->Nickname, true, false);
        ShipView->LoadEquipment(&GetPlayerEquipDescListRetail()); // FUN_004c4060
        ViewName->SetProperty(0x1e, static_cast<uintptr_t>(Ship->IdsName), 0);
        ViewClass->SetProperty(0x1e, static_cast<uintptr_t>(G_ShipTraderClassIds[CurrentShipClass]),
                               0);
        FactsIds = Ship->IdsInfo1;
        StatsIds = Ship->IdsInfo2;
        ValuesIds = Ship->IdsInfo3;
    }

    // 004b8c1c..004b8ce4: the facts pane.
    {
        RenderDisplayList Text;
        BuildInfoText(Text, FactsIds);
        InfoFacts->SetScrollOrigin(0, 0); // FUN_005798b0
        InfoFacts->ClearContent();        // FUN_0057b370
        InfoFacts->AppendContent(Text);   // FUN_0057b3e0
    }
    // 004b8cf7..004b8dec: the stats pane, no auto scroll bar.
    {
        RenderDisplayList Text;
        BuildInfoText(Text, StatsIds);
        InfoStats->SetScrollOrigin(0, 0);
        InfoStats->ClearContent();
        InfoStats->AppendContent(Text);
        InfoStats->SetAutoScrollbar(false); // FUN_005799f0(0)
    }
    // 004b8df1..004b8ee6: the values pane, no auto scroll bar.
    {
        RenderDisplayList Text;
        BuildInfoText(Text, ValuesIds);
        InfoValues->SetScrollOrigin(0, 0);
        InfoValues->ClearContent();
        InfoValues->AppendContent(Text);
        InfoValues->SetAutoScrollbar(false);
    }

    // 004b8eeb..004b8f1e: the view's angle, for the object and for the drag.
    {
        float const Ypr[3] = {162.581f, -71.6442f, 0.0f};
        ShipView->SetObjectYpr(Ypr); // FUN_00586900
        ShipView->SetDragYpr(Ypr);   // FUN_00585070
    }

    // 004b8f23..004b90d5: why the row cannot be bought, or the buy controls. Retail indexes the
    // row arrays with Selected even when it is -2 (the dwords 8 bytes before each array); the
    // -2 case hides everything again below.
    int const Status = RowDword(offsetof(NnShipTrader, RowStatus), Selected);
    if (Status == 1)
    {
        // 004b8f39..004b8f7e: ids 0x61b with the required rank level.
        GetIdsStringWWithRawFallback(0x61b, G_NnTextScratchA, G_NnTextScratchCapacity); // FUN_004347e0
        swprintf(G_NnTextScratchB, 0x1000, G_NnTextScratchA,
                 RowDword(offsetof(NnShipTrader, RowRequiredLevels), Selected));
        ErrorHelp->SetProperty(0x1d, PointerArg(G_NnTextScratchB), 0);
        ErrorHelp->SetProperty(1, 0, 0);
        BuyButton->SetProperty(1, 1, 0);
        BuyText->SetProperty(1, 1, 0);
    }
    else if (Status == 2)
    {
        // 004b900a..004b904d: reputation (ids 0x61c).
        ErrorHelp->SetProperty(0x1e, 0x61c, 0);
        ErrorHelp->SetProperty(1, 0, 0);
        BuyButton->SetProperty(1, 1, 0);
        BuyText->SetProperty(1, 1, 0);
    }
    else if (RowDword(offsetof(NnShipTrader, RowPrices), Selected) > PlayerWorth)
    {
        // 004b904f..004b90ab: too expensive (ids 0x49a).
        ErrorHelp->SetProperty(0x1e, 0x49a, 0);
        ErrorHelp->SetProperty(1, 0, 0);
        BuyButton->SetProperty(1, 1, 0);
        BuyText->SetProperty(1, 1, 0);
    }
    else
    {
        // 004b90ad..004b90d5: buyable.
        ErrorHelp->SetProperty(1, 1, 0);
        BuyButton->SetProperty(1, 0, 0);
        BuyText->SetProperty(1, 0, 0);
    }

    // 004b90db..004b9119: the current ship cannot be bought.
    if (Selected == -2)
    {
        BuyButton->SetProperty(1, 1, 0);
        BuyText->SetProperty(1, 1, 0);
        ErrorHelp->SetProperty(1, 1, 0);
    }
}
