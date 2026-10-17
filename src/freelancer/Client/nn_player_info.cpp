// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_player_info.h"

#include "character_select_equip.h" // GetPlayerSpawnSystemIdRetail (FUN_004c4670, the player's
                                    // vibe DAT_0061226c), GetPlayerVisitFlagsRetail
                                    // (FUN_004c4da0), SetPlayerVisitFlagsRetail (FUN_004c4e00)
#include "game_options.h"           // GetGameOption (FUN_005792b0)
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_target.h"             // HudReputationBand (FUN_0045a6e0)
#include "ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nn_dealer.h"              // NnDealer::ApplyPlayerStatsFeeling (FUN_00483f30)
#include "nn_modal.h"               // NnMainButtonOnModalOpen (FUN_0045a460)
#include "nn_text_scratch.h"        // G_NnTextScratchB (DAT_0066fc60), G_NnTextScratchCapacity
#include "nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4),
                                    // NnReputationColorArgb (FUN_0045a650),
                                    // NnUiPulseBlendFromArgb (FUN_00561f20)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "star_sphere.h"            // Win3DBindModelview (FUN_00422690)
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DReparent (FUN_0059dd00), Win3DDeleteTree
                                    // (FUN_0043d2a0), Win3DGetCamera (DAT_0067dbf8)
#include "win3d_immediate.h"        // Win3DBegin / Win3DVertex3f / Win3DEnd / ...
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "DALib.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include "common_archetype.h"       // Archetype::GetShip
#include "common_reputation.h"      // Reputation::get_id / get_name / get_info_card /
                                    // IsStoryFaction, Reputation::Vibe::GetAffiliation
#include "common_server.h"          // IServer::RequestPlayerStats
#include "common_misc.h"            // SinglePlayer
#include "common_text.h"            // RenderDisplayList, StyleNode
#include "common_tstring.h"         // TString

#include <stdio.h>
#include <string.h>
#include <wchar.h>

// Retail DAT_00672180 -- WINFACTORY<NN_PlayerInfo> static (LAB_004a7820 -> FUN_004a9090; Create
// FUN_004a9100; atexit LAB_004a7840 -> FUN_00579380).
NnPlayerInfoFactory G_NnPlayerInfoFactory("NN_PlayerInfo");

// DAT_006721bc
NnPlayerInfo* G_NnPlayerInfo = nullptr;

// DAT_006721b0 / DAT_006721ac (-0.6f / 0.6f, static inits LAB_004a7800 / LAB_004a7810) are this
// TU's copies of a header float constant; nothing in the PE reads them (raw scan: each address
// occurs once, in its initializer's store), so the port has no counterpart.

namespace
{
    char const kInfoWindowMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 0x005ce994
    char const kCloseButtonMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";            // 0x005d01d0
    char const kUnfriendlyBarMesh[] =
        "Interface\\NeuroNet\\Inventory\\PSTAT_grandienthate.3db"; // 0x005d48e0
    char const kFriendlyBarMesh[] =
        "Interface\\NeuroNet\\Inventory\\PSTAT_grandientlove.3db"; // 0x005d4890

    wchar_t const kIntFormat[] = L"%d";                  // PTR_DAT_005d0ec4
    wchar_t const kTimeFormat[] = L"%.02d:%.02d:%.02d"; // 0x005d49b4

    // Create's heading table (0x004a7e5a..0x004a7efe, a stack array): the ids of each
    // statistic, 1 for a gap of 0.015, 0 ends it.
    unsigned const kHeadingIds[16] = {0x61d, 0x61e, 0x61f, 1,     0x64b, 0x641, 0x646, 0x647,
                                      0x648, 0x649, 1,     0x64a, 0x64c, 0x64d, 0x64e, 0};

    // Not a retail function: the "%d" line every statistic of FUN_004a8820 spells out -- swprintf
    // into the second scratch buffer, then the control's SetProperty(0x1d, text, 0).
    void SetScratchText(WinControl* Control)
    {
        Control->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    }

    void SetIntText(WinControl* Control, int Value)
    {
        _swprintf(G_NnTextScratchB, kIntFormat, Value);
        SetScratchText(Control);
    }

    // The player's affiliation as FUN_004a8750 (0x004a8758..0x004a8785) and the Draw
    // (0x004a821f..0x004a824c) read it: -1 in single player, else Vibe::GetAffiliation of the
    // player's vibe (its result is not tested; Out stays -1 when it writes nothing).
    unsigned PlayerAffiliation()
    {
        unsigned Affiliation = 0xffffffffu;
        if (!SinglePlayer())
        {
            int const PlayerVibe = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670
            Reputation::Vibe::GetAffiliation(PlayerVibe, Affiliation, false);
        }
        return Affiliation;
    }

    // The quad block of the Draw (0x004a83cc..0x004a8519 and 0x004a852d..0x004a8687): the
    // modelview from Bar's orientation (Win3D +0x3c) and its offset plus position (+0xa8 +
    // +0x60), texturing off, blending on with (2, 4), colour (0, 0, 0, 0x0a), then a quad at
    // z = 0.001 (0x3a83126f) through the four corners in the order the caller gives.
    void BindBarFrame(WinControl const* Bar)
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
    }
}

Win3D* NnPlayerInfoFactory::Create(Win3D* Parent, void* Payload)
{
    // FUN_004a9100: operator_new(0x428), the ctor (inline), +0x10 = the factory's name, slot 9;
    // on failure slot 0 with 1 and null.
    NnPlayerInfo* const Window =
        new NnPlayerInfo(static_cast<unsigned char>(reinterpret_cast<uintptr_t>(Payload)));
    Window->ClassName = ClassName;
    if (!Window->Create(Parent, Payload))
    {
        delete Window;
        return nullptr;
    }
    return Window;
}

NnPlayerInfo::NnPlayerInfo(unsigned char PayloadByte)
    : CloseButton(nullptr)
    , PlayerStatsTitle(nullptr)
    , Scroll(nullptr)
    , FactionInfoCard(nullptr)
    , CloseInfocardButton(nullptr)
    , CloseInfocardText(nullptr)
    , Field_0x40c(PayloadByte)
    , FactionCount(0)
    , FirstRow(0)
    , StatsRequestTimer(0.0f)
{
    // FUN_004a9100 0x004a9143..0x004a91d6. Retail leaves the control pointers, +0x41c and
    // +0x424 unwritten until Create; the port zeroes them.
    for (int Row = 0; Row < 6; ++Row)
    {
        FactionFrames[Row] = nullptr;
        FactionNames[Row] = nullptr;
        UnfriendlyBars[Row] = nullptr;
        FriendlyBars[Row] = nullptr;
    }
    for (int Stat = 0; Stat < 13; ++Stat)
    {
        StatValues[Stat] = nullptr;
        StatHeadings[Stat] = nullptr;
    }

    // 0x004a9169..0x004a91d2: the "li_p_grp" group (a TString<16> on the stack) gets visit flags
    // 0x41, told to the server.
    TString<16> GroupName;
    GroupName = "li_p_grp"; // 0x005d4848
    SetPlayerVisitFlagsRetail(Reputation::get_id(GroupName), 0x41, true); // FUN_004c4e00

    G_NnPlayerInfo = this; // 0x004a91d6
}

NnPlayerInfo::~NnPlayerInfo()
{
    // FUN_004a7870: DAT_006721bc = 0, then the vector (member dtor) and WinControl's dtor.
    G_NnPlayerInfo = nullptr;
}

bool NnPlayerInfo::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a78c0 (RET 8). 0x004a78cc..0x004a78e8: the window mesh (slot 38) and the hit box.
    SetMesh(kInfoWindowMesh, 0);
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder); // FUN_005a1340

    // 0x004a78ed..0x004a794f
    {
        float const Position[3] = {0.405f, 0.239f, 0.0f}; // 0x3ecf5c29, 0x3e74bc6a
        float const Half[3] = {0.018f, 0.018f, 0.0f};     // 0x3c9374bc
        CloseButton = CreateButtonControl("PlayerInfoCloseButton", kCloseButtonMesh, nullptr,
                                          "ui_select_item", nullptr, Position, nullptr, Half, 0, 0,
                                          0, 0, 0x409, 0, 0);
    }
    // 0x004a7954..0x004a799f: ids 0x3a3 "PLAYER STATUS"
    {
        float const Position[3] = {-0.418f, 0.257f, 0.0f}; // 0xbed60419, 0x3e839581
        CreateTextControl("PlayerInfoMainTitle", "PageHeader", Position, nullptr, 0.839f, 0.03f,
                          1.65f, 0x3a3, 1, 0, 1);
    }
    // 0x004a79a4..0x004a79e9: ids 0x632 "REPUTATION"
    {
        float const Position[3] = {-0.405f, 0.21f, 0.0f}; // 0xbecf5c29, 0x3e570a3d
        CreateTextControl("PlayerInfoTitle", "SubHeader", Position, nullptr, 0.35f, 0.03f, 1.65f,
                          0x632, 1, 0, 1);
    }
    // 0x004a79ee..0x004a7a50: ids 0x640 "STATS"
    {
        float const Position[3] = {0.009f, 0.21f, 0.0f}; // 0x3c1374bc
        PlayerStatsTitle = CreateTextControl("PlayerStatsTitle", "SubHeader", Position, nullptr,
                                             0.355f, 0.03f, 1.65f, 0x640, 1, 0, 1);
    }
    // 0x004a7a38..0x004a7a8c: the infocard, invisible (Flags &= 0xfd).
    {
        float const Position[3] = {0.01f, 0.172f, 0.0f}; // 0x3c23d70a, 0x3e3020c5
        FactionInfoCard = CreateRichTextWin("FactionInfoCard", 0.384f, 0.34f, Position, true);
        FactionInfoCard->Flags = static_cast<unsigned char>(FactionInfoCard->Flags & 0xfd);
    }
    // 0x004a7a73..0x004a7ad5: ids 0x32b "Close the Infocard."
    {
        float const Position[3] = {0.2f, -0.21f, 0.0f};   // 0x3e4ccccd, 0xbe570a3d
        float const Half[3] = {0.133f, 0.026f, 0.0f};     // 0x3e083127, 0x3cd4fdf4
        CloseInfocardButton = CreateWireButtonControl("NavMapCloseInfocardButton",
                                                      "ui_select_item", Position, Half, 0, 0,
                                                      0x32b, 0, 0);
    }
    // 0x004a7ada..0x004a7b4e: ids 0x3a1 "CLOSE"; both hidden (Flags &= 0xfc).
    {
        float const Position[3] = {0.07f, -0.19f, 0.0f}; // 0x3d8f5c29, 0xbe428f5c
        CloseInfocardText = CreateTextControl("NavMapCloseInfocardText", "ButtonText", Position,
                                              nullptr, 0.262f, 0.03f, 1.9f, 0x3a1, 1, 0, 1);
    }
    CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags & 0xfc);
    CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags & 0xfc);

    // 0x004a7b60..0x004a7e01: six faction rows.
    char ControlName[0x80]; // [ESP+0xd0]
    for (int Row = 0; Row < 6; ++Row)
    {
        // FILD Row, FMUL 0.068f (DAT_005d4958), FSUBR -0.01f (DAT_005d4954): the row's base Y,
        // kept extended for the frame and stored as a float for the rest.
        double const RowY = static_cast<double>(-0.01f) -
                            static_cast<double>(Row) * static_cast<double>(0.068f);
        float const RowYf = static_cast<float>(RowY); // FST [ESP+0x30]

        sprintf(ControlName,"PlayerInfoFactionFrame%d", Row);
        {
            float const Position[3] = {-0.231f, // 0xbe6c8b44
                                       static_cast<float>(RowY + static_cast<double>(0.149f)),
                                       0.0f}; // DAT_005d2c10
            float const Half[3] = {0.173f, 0.034f, 0.0f}; // 0x3e3126e9, 0x3d0b4396
            FactionFrames[Row] = CreateWireButtonControl(ControlName,"ui_item_select", Position, Half,
                                                         0, 0, 0, 0, 0);
        }

        sprintf(ControlName,"PlayerInfoFactionName%d", Row);
        {
            float const Position[3] = {
                NnFloatBits(0xbe666667), // -0.225 plus one ulp
                static_cast<float>(static_cast<double>(RowYf) + static_cast<double>(0.16f)),
                0.0f}; // DAT_005d4938
            FactionNames[Row] = CreateTextControl(ControlName,"ItemName", Position, nullptr, 0.358f,
                                                  0.04f, 1.5f, 0, 1, 0, 0);
        }

        // 0x004a7c60..0x004a7c78: the bars' Y, RowYf + 0.134f (0x005d4918), shared by both.
        float const BarY =
            static_cast<float>(static_cast<double>(RowYf) + static_cast<double>(0.134f));
        float const Half[3] = {0.061f, 0.014f, 0.0f}; // 0x3d79db23, 0x3c656042
        float const Zero[3] = {0.0f, 0.0f, 0.0f};

        sprintf(ControlName,"PlayerInfoUnfriendlyBar%d", Row);
        {
            float const Position[3] = {-0.292f, BarY, 0.0f}; // 0xbe958106
            UnfriendlyBars[Row] = CreateStaticControl(ControlName,kUnfriendlyBarMesh, Position, nullptr,
                                                      Half, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, Zero);
        }
        // 0x004a7d02..0x004a7d31: the HUD chrome colour with a third of its alpha (the byte
        // divided by 3, IMUL 0x55555556), as both of property 0x18's arguments.
        unsigned char Colour[4];
        memcpy(Colour, G_HudChromeColorRgba, sizeof(Colour)); // DAT_00679bb4
        Colour[3] = static_cast<unsigned char>(Colour[3] / 3);
        UnfriendlyBars[Row]->SetProperty(0x18, reinterpret_cast<uintptr_t>(Colour),
                                         reinterpret_cast<uintptr_t>(Colour));

        sprintf(ControlName,"PlayerInfofriendlyBar%d", Row);
        {
            float const Position[3] = {NnFloatBits(0xbe2e7d57), BarY, 0.0f}; // -0.1704 + 1 ulp
            FriendlyBars[Row] = CreateStaticControl(ControlName,kFriendlyBarMesh, Position, nullptr,
                                                    Half, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, Zero);
        }
        FriendlyBars[Row]->SetProperty(0x18, reinterpret_cast<uintptr_t>(Colour),
                                       reinterpret_cast<uintptr_t>(Colour));
    }

    // 0x004a7e07..0x004a7e42
    {
        float const Position[3] = {-0.045f, 0.176f, 0.0f}; // 0xbd3851ec, 0x3e343958
        Scroll = CreateUIScroll("PlayerInfoScroll", Position, 0.019f, 0.412f, 0, 5, 6, 0);
    }

    // 0x004a7e48..0x004a805f: the heading / value pairs, top down from 0.19; a 1 in the table
    // is a 0.015 gap. Outside single player the worth and next-level pairs (table entries 1 and
    // 2) are hidden and take no room.
    float Y = 0.0f;          // [ESP+0x10]
    int Pair = 0;            // [ESP+0x24]
    unsigned TableIndex = 0; // [ESP+0x20]
    unsigned Ids = kHeadingIds[0];
    while (Ids != 0)
    {
        if (Ids == 1)
        {
            Y = static_cast<float>(static_cast<double>(Y) + static_cast<double>(0.015f));
        }
        else
        {
            // FLD 0.19f (DAT_005d4248), FSUB Y, FSUB 0.03f (DAT_005d011c), stored as a float.
            float const PairY = static_cast<float>(
                static_cast<double>(0.19f) - static_cast<double>(Y) - static_cast<double>(0.03f));

            sprintf(ControlName,"PlayerInfoHeading%d", Pair);
            float const HeadingPosition[3] = {0.19f, PairY, 0.0f}; // 0x3e428f5c
            WinControl* const Heading = CreateTextControl(ControlName,"ItemName", HeadingPosition,
                                                          nullptr, 0.358f, 0.04f, 1.5f, Ids, 0,
                                                          0, 0);
            StatHeadings[Pair] = Heading;

            sprintf(ControlName,"PlayerInfoStat%d", Pair);
            float const StatPosition[3] = {0.38f, PairY, 0.0f}; // 0x3ec28f5c
            StatValues[Pair] = CreateTextControl(ControlName,"ItemName", StatPosition, nullptr, 0.188f,
                                                 0.04f, 1.5f, 0, 0, 0, 0);

            if (!SinglePlayer() && (TableIndex == 1 || TableIndex == 2))
            {
                Heading->Flags = static_cast<unsigned char>(Heading->Flags & 0xfc);
                StatValues[Pair]->Flags = static_cast<unsigned char>(StatValues[Pair]->Flags & 0xfc);
            }
            else
            {
                Y = static_cast<float>(static_cast<double>(Y) + static_cast<double>(0.03f));
            }
            ++Pair;
        }
        ++TableIndex;
        Ids = kHeadingIds[TableIndex];
    }

    // 0x004a8065..0x004a80c7
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(-1));
    FactionCount = 0;
    SetOffset(0.0f, 0.0f, 0.0f); // slot 31
    SetPosition(Position);       // slot 5
    StatsRequestTimer = 1.0f;
    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

intptr_t NnPlayerInfo::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a80e0 (RET 0xc).
    if (Msg == 0x3e)
    {
        // 0x004a80f6..0x004a8117: the scroll moved.
        if (A != reinterpret_cast<uintptr_t>(Scroll))
            return 0;
        FirstRow = static_cast<int>(B);
        return 1;
    }
    if (Msg != 0x26)
        return 0;

    if (A == reinterpret_cast<uintptr_t>(CloseButton))
    {
        NnMainButtonOnModalOpen(); // FUN_0045a460
        return 1;
    }

    if (A == reinterpret_cast<uintptr_t>(CloseInfocardButton))
    {
        // 0x004a8141..0x004a81c3: the card invisible, its close button and text hidden, every
        // statistic shown again (outside single player not the worth and next-level pairs), the
        // title back to ids 0x640.
        FactionInfoCard->Flags = static_cast<unsigned char>(FactionInfoCard->Flags & 0xfd);
        CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags & 0xfc);
        CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags & 0xfc);
        for (int Stat = 0; Stat < 13; ++Stat)
        {
            if (SinglePlayer() || (Stat != 1 && Stat != 2))
            {
                StatValues[Stat]->Flags = static_cast<unsigned char>(StatValues[Stat]->Flags | 3);
                StatHeadings[Stat]->Flags =
                    static_cast<unsigned char>(StatHeadings[Stat]->Flags | 3);
            }
        }
        PlayerStatsTitle->SetProperty(0x1e, 0x640, 0);
        return 1;
    }

    // 0x004a81c6..0x004a81fb: a faction frame.
    for (int Row = 0; Row < 6; ++Row)
    {
        if (A == reinterpret_cast<uintptr_t>(FactionFrames[Row]))
        {
            ShowFactionInfocard(FirstRow + Row);
            return 1;
        }
    }
    return 0;
}

bool NnPlayerInfo::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_00475430 (RET 0xc) -- the body NN_Help's slot 23 (nn_help.cpp) is folded with.
    if (Msg != 0xef02)
        return WinControl::Slot23(Msg, A, B); // FUN_005a0840

    // 0x0047543e..0x00475477: out of the parent (no parent test), then every child deleted (the
    // next sibling read first), slot 10 and slot 0 with 1.
    Win3DReparent(this, nullptr); // FUN_0059dd00
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child); // FUN_0043d2a0
        Child = Next;
    }
    Destroy();
    delete this;
    return true;
}

void NnPlayerInfo::Draw()
{
    // LAB_004a8200 (slot 13).
    if (GetGameOption(kGameOptHud) == 0) // FUN_005792b0(10)
        return;

    unsigned const Affiliation = PlayerAffiliation(); // [ESP+0x10]

    // 0x004a8251..0x004a8368: the six rows from FirstRow; a row past the count hides its frame,
    // name and bars.
    for (int Row = 0; Row < 6; ++Row)
    {
        int const Index = FirstRow + Row;
        if (Index >= FactionCount)
        {
            FactionNames[Row]->Flags = static_cast<unsigned char>(FactionNames[Row]->Flags & 0xfc);
            FactionFrames[Row]->Flags =
                static_cast<unsigned char>(FactionFrames[Row]->Flags & 0xfc);
            UnfriendlyBars[Row]->Flags =
                static_cast<unsigned char>(UnfriendlyBars[Row]->Flags & 0xfc);
            FriendlyBars[Row]->Flags = static_cast<unsigned char>(FriendlyBars[Row]->Flags & 0xfc);
            continue;
        }
        FactionNames[Row]->Flags = static_cast<unsigned char>(FactionNames[Row]->Flags | 3);
        FactionFrames[Row]->Flags = static_cast<unsigned char>(FactionFrames[Row]->Flags | 3);
        UnfriendlyBars[Row]->Flags = static_cast<unsigned char>(UnfriendlyBars[Row]->Flags | 3);
        FriendlyBars[Row]->Flags = static_cast<unsigned char>(FriendlyBars[Row]->Flags | 3);

        FNnPlayerInfoFaction const& Faction = Factions.data()[Index];

        // 0x004a82a9..0x004a82ba: the name, ids of Reputation::get_name.
        FactionNames[Row]->SetProperty(0x1e, Reputation::get_name(Faction.GroupId), 0);

        // 0x004a82c0..0x004a8300: its colour by attitude, pulsed for the player's own group.
        unsigned long Colour = 0; // [ESP+0x14]
        NnReputationColorArgb(&Colour, Faction.Attitude); // FUN_0045a650
        if (Affiliation == Faction.GroupId)
        {
            unsigned long Pulsed = 0; // [ESP+0x10]
            NnUiPulseBlendFromArgb(&Pulsed, Colour); // FUN_00561f20
            Colour = Pulsed;
        }
        FactionNames[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);

        // 0x004a8306..0x004a833a: the frame's caption, ids 0x634 "FRIENDLY" for band 4, 0x636
        // "HOSTILE" for band 0, otherwise 0x635 "NEUTRAL".
        int const Band = HudReputationBand(Faction.Attitude); // FUN_0045a6e0 at 0x004a830a
        unsigned CaptionIds = 0x635;
        if (Band == 4)
            CaptionIds = 0x634;
        else if (Band == 0)
            CaptionIds = 0x636;
        FactionFrames[Row]->SetProperty(3, CaptionIds, 0);
    }

    // 0x004a836e..0x004a8381
    WireControlsJoinColumn(FactionFrames, 6); // FUN_005a4b50
    WinControl::Draw();                       // FUN_005a0760

    // 0x004a8386..0x004a86e1: each visible row's bar is darkened past its attitude.
    IRenderPipeline* const Pipe = DALib::Pipeline;
    for (int Row = 0; Row < 6; ++Row)
    {
        int const Index = FirstRow + Row;
        if (Index >= FactionCount)
            continue;
        FNnPlayerInfoFaction const& Faction = Factions.data()[Index];

        // 0x004a83b0..0x004a83c6: FCOMP 0.0f (DAT_005c7474), TEST AH,5, JP -- the unfriendly
        // bar only when the attitude is less than 0 (C0 alone); at or above 0, or unordered,
        // the friendly one.
        WinControl* Bar = nullptr;
        if (Faction.Attitude < 0.0f)
        {
            // 0x004a83cc..0x004a8528: X = HalfX - (-(2 HalfX * Attitude)); the quad from X to
            // the left end.
            Bar = UnfriendlyBars[Row];
            float const HalfX = Bar->HalfExtents[0];
            float const HalfY = Bar->HalfExtents[1];
            float const X = static_cast<float>(
                static_cast<double>(HalfX) +
                static_cast<double>(HalfX + HalfX) * static_cast<double>(Faction.Attitude));
            Pipe->SetRenderState(D3DRS_ZENABLE, 1);
            Pipe->SetRenderState(D3DRS_ZFUNC, 8);
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
            BindBarFrame(Bar);
            Win3DVertex3f(X, HalfY, 0.001f); // FUN_00421cc0
            Win3DVertex3f(X, -HalfY, 0.001f);
            Win3DVertex3f(-HalfX, -HalfY, 0.001f);
            Win3DVertex3f(-HalfX, HalfY, 0.001f);
            Win3DEnd(); // FUN_00421690
        }
        else
        {
            // 0x004a852d..0x004a868c: X = HalfX - 2 HalfX * Attitude; the quad from -X to the
            // right end.
            Bar = FriendlyBars[Row];
            float const HalfX = Bar->HalfExtents[0];
            float const HalfY = Bar->HalfExtents[1];
            float const X = static_cast<float>(
                static_cast<double>(HalfX) -
                static_cast<double>(HalfX + HalfX) * static_cast<double>(Faction.Attitude));
            Pipe->SetRenderState(D3DRS_ZENABLE, 1);
            Pipe->SetRenderState(D3DRS_ZFUNC, 8);
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
            BindBarFrame(Bar);
            Win3DVertex3f(-X, HalfY, 0.001f);
            Win3DVertex3f(-X, -HalfY, 0.001f);
            Win3DVertex3f(HalfX, -HalfY, 0.001f);
            Win3DVertex3f(HalfX, HalfY, 0.001f);
            Win3DEnd();
        }

        // 0x004a8694..0x004a86c3: the bar's mesh over the quad.
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Bar->MeshHolder.EngineInstance, 1.0f, 0,
                               nullptr, nullptr); // IRenderer2 +0x1c, &DAT_0067dbf8
    }

    // 0x004a86e7..0x004a8732: the timer gets the frame time (the sum is stored as a float, the
    // compare uses the extended sum); past 1.0 it loses 1.0 and the server is asked for the
    // stats (IServer slot 73 with a zero dword, 4 bytes).
    double const Timer =
        GetClientFrameTimeCapped() + static_cast<double>(StatsRequestTimer); // FUN_0042d680
    StatsRequestTimer = static_cast<float>(Timer);
    if (Timer > 1.0) // TEST AH,0x41 / JNZ: skipped when not greater
    {
        StatsRequestTimer = static_cast<float>(Timer - 1.0);
        unsigned Request = 0; // [ESP+0x4]
        G_Server->RequestPlayerStats(GetPlayerConnectionId(),
                                     reinterpret_cast<unsigned char*>(&Request), 4);
    }
}

void NnPlayerInfo::SortFactions()
{
    // FUN_004a8750 (thiscall).
    unsigned const Affiliation = PlayerAffiliation(); // [ESP+0xc]

    // 0x004a8790..0x004a8810: passes over the FactionCount - 1 neighbour pairs (none when that
    // is <= 0) until one swaps nothing. Each attitude, 10.0f (DAT_005d49b0) higher for the
    // affiliation, compared on the x87 stack: FCOMP, TEST AH,0x41, JNZ -- the pair swaps only
    // when the first is greater (not when equal, less or unordered).
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < FactionCount - 1; ++Pair)
        {
            FNnPlayerInfoFaction* const First = &Factions.data()[Pair];
            FNnPlayerInfoFaction* const Second = &Factions.data()[Pair + 1];
            double FirstKey = static_cast<double>(First->Attitude);
            double SecondKey = static_cast<double>(Second->Attitude);
            if (First->GroupId == Affiliation)
                FirstKey += static_cast<double>(10.0f);
            if (Second->GroupId == Affiliation)
                SecondKey += static_cast<double>(10.0f);
            if (FirstKey > SecondKey)
            {
                FNnPlayerInfoFaction const Saved = *First;
                *First = *Second;
                *Second = Saved;
                bSorted = false;
            }
        }
    }
}

void NnPlayerInfo::OnPlayerStats(FNnPlayerStatsBlock const* Stats,
                                 FNnPlayerStatsShipKills const* ShipKills,
                                 FPlayerStatsRepEntry const* Reputations)
{
    // FUN_004a8820 (thiscall, RET 0xc). 0x004a8834..0x004a88ae: systems, bases, jump holes.
    SetIntText(StatValues[6], Stats->SystemsVisited);  // +0x3b0
    SetIntText(StatValues[7], Stats->BasesVisited);    // +0x3b4
    SetIntText(StatValues[8], Stats->JumpHolesFound);  // +0x3b8

    // 0x004a88b1..0x004a88f1: the play time, ftol of the seconds, as hours:minutes:seconds.
    {
        int const TotalSeconds = static_cast<int>(Stats->TotalTimeSeconds);
        int const Seconds = TotalSeconds % 60;
        int const TotalMinutes = TotalSeconds / 60;
        int const Minutes = TotalMinutes % 60;
        int const Hours = TotalMinutes / 60;
        _swprintf(G_NnTextScratchB, kTimeFormat, Hours, Minutes, Seconds);
        SetScratchText(StatValues[5]); // +0x3ac
    }

    // 0x004a88f4..0x004a898d: kills by the ship's type bits (Archetype +0x10); a ship
    // archetype that does not resolve counts nowhere.
    int Freighters = 0;   // [ESP+0x18], type & 0x20000
    int Transports = 0;   // [ESP+0x10], type & 0x100000
    int Battleships = 0;  // [ESP+0x24], type & 0x280000
    int Fighters = 0;     // EBP, every other type
    for (int Entry = 0; Entry < Stats->ShipKillCount; ++Entry)
    {
        FNnPlayerStatsShipKills const& Kills = ShipKills[Entry];
        Archetype::Ship const* const ShipArch = Archetype::GetShip(Kills.ShipArchId);
        if (ShipArch == nullptr)
            continue;
        unsigned const Type = ShipArch->ObjectType;
        if ((Type & 0x20000u) != 0)
            Freighters += Kills.Count;
        else if ((Type & 0x100000u) != 0)
            Transports += Kills.Count;
        else if ((Type & 0x280000u) != 0)
            Battleships += Kills.Count;
        else
            Fighters += Kills.Count;
    }

    // 0x004a898f..0x004a8a6a
    SetIntText(StatValues[9], Fighters);     // +0x3bc
    SetIntText(StatValues[10], Freighters);  // +0x3c0
    SetIntText(StatValues[11], Transports);  // +0x3c4
    SetIntText(StatValues[12], Battleships); // +0x3c8
    SetIntText(StatValues[4], Battleships + Transports + Freighters + Fighters); // +0x3a8

    // 0x004a8a70..0x004a8ae7: missions, level, worth (grouped digits).
    SetIntText(StatValues[3], Stats->TotalMissions); // +0x3a4
    SetIntText(StatValues[0], Stats->Level);         // +0x398
    FormatGroupedNumber(G_NnTextScratchB, Stats->Worth, -1); // FUN_004779a0
    SetScratchText(StatValues[1]);                   // +0x39c

    // 0x004a8aed..0x004a8b56: the next level -- ids 0x3e4 "Max Level" for 0x7fffffff, the
    // grouped number when positive, otherwise ids 0x620 "Mission".
    int const Requirement = Stats->NextLevelRequirement;
    if (Requirement == 0x7fffffff)
        GetIdsStringWWithRawFallback(0x3e4, G_NnTextScratchB, G_NnTextScratchCapacity);
    else if (Requirement > 0)
        FormatGroupedNumber(G_NnTextScratchB, Requirement, -1);
    else
        GetIdsStringWWithRawFallback(0x620, G_NnTextScratchB, G_NnTextScratchCapacity);
    SetScratchText(StatValues[2]); // +0x3a0

    // 0x004a8b5c..0x004a8e0d: the faction list rebuilt -- each group with an entry in the visit
    // map (FUN_004c4da0 with (0, 1)) whose flags have bit 0, and that is no story faction.
    Factions.clear(); // the tail copied onto the head, end = begin
    FactionCount = 0;
    for (int Entry = 0; Entry < Stats->ReputationCount; ++Entry)
    {
        FPlayerStatsRepEntry const& Rep = Reputations[Entry];
        unsigned char VisitFlags = 0; // [ESP+0x28]
        if (!GetPlayerVisitFlagsRetail(Rep.GroupId, &VisitFlags, 0, 1))
            continue;
        if ((VisitFlags & 1) == 0)
            continue;
        if (Reputation::IsStoryFaction(Rep.GroupId))
            continue;
        FNnPlayerInfoFaction Faction;
        Faction.GroupId = Rep.GroupId;
        Faction.Attitude = Rep.Feeling;
        Factions.push_back(Faction); // the vector insert FUN_004e5fc0 / operator_new inline
        ++FactionCount;
    }

    // 0x004a8e13..0x004a8e2e
    SortFactions();
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(FactionCount - 1));
}

void NnPlayerInfo::ShowFactionInfocard(int Row)
{
    // FUN_004a8e80 (thiscall, RET 4). 0x004a8e9d..0x004a8eda: nothing without rows (a null or
    // empty vector) or when the group has no infocard.
    if (Factions.empty())
        return;
    // Row is not tested against the count (0x004a8ec1..0x004a8ec7).
    unsigned const GroupId = Factions.data()[Row].GroupId;
    unsigned const InfoCard = Reputation::get_info_card(GroupId);
    if (InfoCard == 0)
        return;

    // 0x004a8ee0..0x004a8f07: the title becomes the group's name.
    PlayerStatsTitle->SetProperty(0x1e, Reputation::get_name(GroupId), 0);

    // 0x004a8f0d..0x004a8f57: every statistic hidden, the card visible, its close button and
    // text shown.
    for (int Stat = 0; Stat < 13; ++Stat)
    {
        StatValues[Stat]->Flags = static_cast<unsigned char>(StatValues[Stat]->Flags & 0xfc);
        StatHeadings[Stat]->Flags = static_cast<unsigned char>(StatHeadings[Stat]->Flags & 0xfc);
    }
    FactionInfoCard->Flags = static_cast<unsigned char>(FactionInfoCard->Flags | 2);
    CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags | 3);
    CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags | 3);

    // 0x004a8f5a..0x004a9077: a card of style 0x9ca4 and the infocard's ids, put into the text
    // (cleared first) and scrolled to the origin; the card destroyed.
    RenderDisplayList Card; // [ESP+0x1c]
    {
        StyleNode Style(0x9ca4);
        RdlAppendNode(Card, Style.Clone()); // vt+0x1c, FUN_0046ea00
    }
    LoadIdsIntoRdl(InfoCard, Card);        // FUN_0057da40
    FactionInfoCard->ClearContent();       // FUN_0057b370
    FactionInfoCard->AppendContent(Card);  // FUN_0057b3e0
    FactionInfoCard->SetScrollOrigin(0, 0); // FUN_005798b0
}

void NnPlayerInfoOnPlayerStats(unsigned char const* Stats, void const* ShipKills,
                               void const* Reputations)
{
    // FUN_004a8e40.
    FNnPlayerStatsBlock const* const Block = reinterpret_cast<FNnPlayerStatsBlock const*>(Stats);
    FPlayerStatsRepEntry const* const RepTable =
        static_cast<FPlayerStatsRepEntry const*>(Reputations);

    // 004a8e40..004a8e5e: an open NN_PlayerInfo gets all three through FUN_004a8820.
    if (G_NnPlayerInfo != nullptr)
    {
        G_NnPlayerInfo->OnPlayerStats(Block,
                                      static_cast<FNnPlayerStatsShipKills const*>(ShipKills),
                                      RepTable);
        return;
    }
    // 004a8e5f..004a8e71: FUN_00483f30([Stats+0x30], the reputation table -- the third
    // argument, [ESP+0xc]).
    NnDealer::ApplyPlayerStatsFeeling(Block->ReputationCount, RepTable);
}
