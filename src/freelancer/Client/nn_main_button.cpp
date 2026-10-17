// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_main_button.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "client_mesh_load.h"    // ClientUiMeshHolderRelease (FUN_0059eee0), ClientMeshLoadFromPath (FUN_0059ec80)
#include "globals.h"             // IsClientServerTickPaused (FUN_0042d730), GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_command_dispatch.h" // HudPostCommand (FUN_005763b0), Hud{Push,Remove}CancelHandler
#include "hud_target.h"          // HudIsMousePickBlocked (FUN_0045a740)
#include "menu_universe_base.h"  // IsClientLocationInTransition (DAT_00668761)
#include "input_manager.h"       // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0), MouseSetCursor
#include "login_mgr.h"           // G_Server (DAT_0067ecd0)
#include "menu_ui_init.h"        // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "nav_bar.h"             // NavBarCloseService (FUN_004424f0)
#include "nn_messaging.h"        // NnVoiceTick (FUN_00489e00)
#include "nn_modal.h"            // G_NnMainButton, G_bNnScreenUp, NnFloatBits
#include "nn_navmap.h"           // NavMapPostPendingOpen (FUN_004a2a80)
#include "nn_story_star.h"       // StoryStarTick (FUN_004bc990), StoryStarRequeueLastObjective
#include "nn_trade.h"            // G_NnTrade (DAT_006724b4)
#include "nn_ui_pulse.h"         // G_NnPaletteArgb_679ba8 (DAT_00679ba8)
#include "player_spawn.h"        // GetPlayerConnectionId (DAT_00673344)
#include "space_letterbox.h"     // SetLetterboxTarget (FUN_0041a0f0), IsLetterboxVisible (FUN_0041a3e0)
#include "win3d.h"               // Win3DFindByName, Win3DReparent, Win3DDeleteTree, Win3DDrawChildrenOf
#include "win_factory.h"         // WINFACTORY, WinFactoryCreate (FUN_0059db20)

#include "DALib.h"
#include "common_csys.h"         // Acquire3DMathEngine (FUN_005b73e0)
#include "common_server.h"       // IServer
#include "common_misc.h"         // SinglePlayer
#include "engine/engbase.h"
#include "x86math.h"

#include <string.h>
#include <windows.h>

unsigned char G_bNnKeepPanelOpen = 0; // DAT_0066d359

// Retail DAT_0066d39c -- WINFACTORY<NN_MainButton> static (LAB_0045b6d0 -> FUN_0045dce0; Create
// FUN_0045dd50; atexit LAB_0045b6f0 -> FUN_00579380).
WINFACTORY<NN_MainButton> G_NnMainButtonFactory("NN_MainButton");

namespace
{
    // DAT_0066d3ec -- MakeScaledBackground's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_NnMainButtonMath = nullptr;

    // DAT_005cea10 -- 0.3f, the duration of every slide and scale.
    float const kAnimSeconds = 0.3f;

    // DAT_006114f8[7] -- the "ScaledBackground" mesh of each panel slot.
    char const* const kScaledBackgroundMeshes[7] = {
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\navmap_background.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Chat\\CHAT_background.cmp",
        "Interface\\intro\\objects\\MULTI_attentionreq'd.cmp"};

    // The switch every method inlines (e.g. 0x0045c7a0..0x0045c7dd, jump table 0x0045c800):
    // 0x66 -> 1, 0x67 -> 2, 0x65 -> 3, 0x6a -> 4, 0x68 -> 5, 0x69 -> 6; 0x63, 0x64 and any id
    // outside 0x63..0x6a -> 0.
    int NnPanelSlot(int PanelId)
    {
        switch (PanelId)
        {
        case 0x66:
            return 1;
        case 0x67:
            return 2;
        case 0x65:
            return 3;
        case 0x6a:
            return 4;
        case 0x68:
            return 5;
        case 0x69:
            return 6;
        default:
            return 0;
        }
    }

    // The window class each panel id opens (0x0045cfb7..0x0045cfe1, 0x0045d358..0x0045d382);
    // null for 0x64 and every id outside 0x63..0x6a (those paths return).
    char const* NnPanelWindowName(int PanelId)
    {
        switch (PanelId)
        {
        case 0x63:
            return "NeuroNetNavMap";
        case 0x66:
            return "NN_Inventory";
        case 0x67:
            return "NeuroNetStoryStar";
        case 0x65:
            return "NN_PlayerInfo";
        case 0x6a:
            return "NN_Help";
        case 0x68:
            return "ChatControl";
        case 0x69:
            return "NN_GroupControl";
        default:
            return nullptr;
        }
    }

    // A plain icon button and its "Root" twin, the pair Create builds five (seven in
    // multiplayer) times (e.g. 0x0045b9da..0x0045bb08): the plain button at Position with no
    // tooltip and no command, its +0x2d4 rest offset = Position, hidden (flags &= 0xfc); then the
    // root with ToolTipIds and the command id in Dword34c, and its property 0x36 = 1. Neither
    // result is null-tested.
    void NnMainButtonCreatePair(NN_MainButton* Bar, char const* Name, char const* RootName,
                                char const* MeshPath, float X, int ToolTipIds, int CommandId,
                                WinControl** OutButton, WinControl** OutRoot)
    {
        float const Position[3] = {X, NnFloatBits(0x3eeb020c), NnFloatBits(0xbfa00000)};
        float const HalfExtents[3] = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};

        WinControl* const Button =
            Bar->CreateButtonControl(Name, MeshPath, nullptr, "ui_select_item", nullptr, Position,
                                     nullptr, HalfExtents, 0, 0, 0, 0, 0, 0, 0);
        *OutButton = Button;
        Button->HomeOffset[0] = Position[0];
        Button->HomeOffset[1] = Position[1];
        Button->HomeOffset[2] = Position[2];
        Button->Flags = static_cast<unsigned char>(Button->Flags & 0xfc);

        WinControl* const Root =
            Bar->CreateButtonControl(RootName, MeshPath, nullptr, "ui_select_item", nullptr,
                                     Position, nullptr, HalfExtents, 0, 0, 0, 0, ToolTipIds, 0,
                                     CommandId);
        *OutRoot = Root;
        Root->SetProperty(0x36, 1, 0);
    }

    void NnSetVector(float* Out, unsigned XBits, unsigned YBits, unsigned ZBits)
    {
        Out[0] = NnFloatBits(XBits);
        Out[1] = NnFloatBits(YBits);
        Out[2] = NnFloatBits(ZBits);
    }
}

NN_MainButton::NN_MainButton()
{
    // FUN_0045b700: FUN_0059f860 (WinControl), Event::Handler vptr, FUN_0055dec0 (ButtonMover
    // +0x330), FUN_0055e340 (ButtonScaler +0x350), then the zeroed fields.
    ClosingStart[0] = 0.0f;
    ClosingStart[1] = 0.0f;
    ClosingStart[2] = 0.0f;
    OpenButton = nullptr;
    ClosingButton = nullptr;
    OpenPanelId = 0;
    ClosingPanelId = 0;
    ScaledBackground = nullptr;
    QueuedButton = nullptr;
    bInfoCardLayout = 0;
    bInventoryAfterDealer = 0;
    bInventoryWithDealer = 0;
    // 0x0045b7cc: DAT_0066d3f0 = this.
    G_NnMainButton = this;
    bForceMissionLog = 0;
}

NN_MainButton::~NN_MainButton()
{
    // FUN_0045b820: the vptrs, DAT_0066d3f0 = 0 when it is this; the scaler (+0x358) and mover
    // (+0x338) lists, FUN_00576010 (Event::Handler) and FUN_0059fa50 (WinControl) follow as the
    // base destructors.
    if (G_NnMainButton == this)
    {
        G_NnMainButton = nullptr;
    }
}

bool NN_MainButton::Create(Win3D* NewParent, void* Data)
{
    // LAB_0045b8f0 (RET 8).
    // 0x0045b8f6..0x0045b9e8: "NNBackground" -- HUD_nnbox5 at (0.403, 0.366, -1) in single
    // player, HUD_nnbox7 at (0.362, 0.366, -1) otherwise; arguments 0, {0,0,0}, 0,0,0,1,0,0,1,
    // 0,0,0, {0,0,0}.
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    float const Zero338[3] = {0.0f, 0.0f, 0.0f};
    if (SinglePlayer())
    {
        float const Position[3] = {NnFloatBits(0x3ece5604), NnFloatBits(0x3ebb645a),
                                   NnFloatBits(0xbf800000)};
        Background = CreateStaticControl("NNBackground", "Interface\\HUD\\HUD_nnbox5.cmp",
                                         Position, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                         Zero338);
    }
    else
    {
        float const Position[3] = {NnFloatBits(0x3eb95810), NnFloatBits(0x3ebb645a),
                                   NnFloatBits(0xbf800000)};
        Background = CreateStaticControl("NNBackground", "Interface\\HUD\\HUD_nnbox7.cmp",
                                         Position, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                         Zero338);
    }

    // 0x0045b9da..0x0045bfa0: the five common pairs, y 0.459, z -1.25, half size 0.018.
    NnMainButtonCreatePair(this, "NNNavmapButton", "NNNavmapButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_Map.3db", NnFloatBits(0x3ee978d6),
                           0x3fc, 0x63, &NavmapButton, &NavmapRoot);
    NnMainButtonCreatePair(this, "NNInventoryButton", "NNInventoryButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_Inventory.3db",
                           NnFloatBits(0x3f010625), 0x3ed, 0x66, &InventoryButton,
                           &InventoryRoot);
    NnMainButtonCreatePair(this, "NNMissionLogButton", "NNMissionLogButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_storystar.3db",
                           NnFloatBits(0x3ed0e561), 0x421, 0x67, &MissionLogButton,
                           &MissionLogRoot);
    NnMainButtonCreatePair(this, "NNPlayerInfoButton", "NNPlayerInfoButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_PlayerStatus.3db",
                           NnFloatBits(0x3f0d4fe0), 0x55f, 0x65, &PlayerInfoButton,
                           &PlayerInfoRoot);
    NnMainButtonCreatePair(this, "NNHelpButton", "NNHelpButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_info.3db", NnFloatBits(0x3f19999a),
                           0x570, 0x6a, &HelpButton, &HelpRoot);

    // 0x0045bfa0..0x0045c216: multiplayer adds chat and group; single player zeroes them.
    if (!SinglePlayer())
    {
        NnMainButtonCreatePair(this, "NNChatButton", "NNChatButtonRoot",
                               "interface\\NeuroNet\\icons\\NN_Chat.3db", NnFloatBits(0x3eb851ec),
                               0x435, 0x68, &ChatButton, &ChatRoot);
        NnMainButtonCreatePair(this, "NNGroupButton", "NNGroupButtonRoot",
                               "interface\\NeuroNet\\icons\\NN_request.3db",
                               NnFloatBits(0x3e9fbe78), 0x560, 0x69, &GroupButton, &GroupRoot);
    }
    else
    {
        ChatButton = nullptr;
        GroupButton = nullptr;
        ChatRoot = nullptr;
        GroupRoot = nullptr;
    }

    // 0x0045c220..0x0045c280: FUN_005760d0 on the Event::Handler face for 0x63, 0x66, 0x67,
    // 0x65, 0x6a, 0x68, 0x69, 0x6e, 0x6b.
    unsigned const Events[9] = {0x63, 0x66, 0x67, 0x65, 0x6a, 0x68, 0x69, 0x6e, 0x6b};
    for (int Index = 0; Index < 9; ++Index)
    {
        RegisterEvent(Events[Index]);
    }

    // 0x0045c282..0x0045c2f0: the button and root tables in slot order.
    Buttons[0] = NavmapButton;
    Buttons[1] = InventoryButton;
    Buttons[2] = MissionLogButton;
    Buttons[3] = PlayerInfoButton;
    Buttons[4] = HelpButton;
    Buttons[5] = ChatButton;
    Buttons[6] = GroupButton;
    Roots[0] = NavmapRoot;
    Roots[1] = InventoryRoot;
    Roots[2] = MissionLogRoot;
    Roots[3] = PlayerInfoRoot;
    Roots[4] = HelpRoot;
    Roots[5] = ChatRoot;
    Roots[6] = GroupRoot;

    // Where each button slides to when its panel opens.
    NnSetVector(OpenTarget[0], 0x00000000, 0x3d75c28f, 0xbfa00000); // (0, 0.06, -1.25)
    NnSetVector(OpenTarget[1], 0x00000000, 0x3d8f5c29, 0xbfa00000); // (0, 0.07, -1.25)
    NnSetVector(OpenTarget[2], 0x00000000, 0x3d8f5c29, 0xbfa00000);
    NnSetVector(OpenTarget[3], 0x00000000, 0x3da3d70a, 0xbfa00000); // (0, 0.08, -1.25)
    NnSetVector(OpenTarget[4], 0x00000000, 0x3da3d70a, 0xbfa00000);
    NnSetVector(OpenTarget[5], 0x00000000, 0x3d6147ae, 0xbf800000); // (0, 0.055, -1)
    NnSetVector(OpenTarget[6], 0x00000000, 0x3d4ccccd, 0xbf800000); // (0, 0.05, -1)
    // Where the open button sits while its panel shows (FUN_0045c7a0 overrides them).
    NnSetVector(PanelOrigin[0], 0xbf1eb852, 0x3e99999a, 0xbfa00000); // (-0.62, 0.3, -1.25)
    NnSetVector(PanelOrigin[1], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f); // (0, 0.076, -1.288)
    NnSetVector(PanelOrigin[2], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(PanelOrigin[3], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(PanelOrigin[4], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(PanelOrigin[5], 0x00000000, 0x00000000, 0xbf800000); // (0, 0, -1)
    NnSetVector(PanelOrigin[6], 0x00000000, 0x00000000, 0xbf800000);
    // "ScaledBackground" positions.
    NnSetVector(BackgroundPosition[0], 0x00000000, 0x3d8f5c29, 0xbfbc28f6); // (0, 0.07, -1.47)
    NnSetVector(BackgroundPosition[1], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f); // (0, 0.076, -1.288)
    NnSetVector(BackgroundPosition[2], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(BackgroundPosition[3], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(BackgroundPosition[4], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(BackgroundPosition[5], 0x00000000, 0x3d6147ae, 0xbf800000); // (0, 0.055, -1)
    NnSetVector(BackgroundPosition[6], 0x00000000, 0x3d4ccccd, 0xbf800000); // (0, 0.05, -1)
    // 0x0045c744..0x0045c776: the background's collapsed scale per slot.
    PanelScale[0] = NnFloatBits(0x3da3d70a); // 0.08
    PanelScale[1] = NnFloatBits(0x3d4ccccd); // 0.05
    PanelScale[2] = NnFloatBits(0x3d4ccccd);
    PanelScale[3] = NnFloatBits(0x3d4ccccd);
    PanelScale[4] = NnFloatBits(0x3d4ccccd);
    PanelScale[5] = NnFloatBits(0x3da3d70a);
    PanelScale[6] = NnFloatBits(0x3da3d70a);

    // 0x0045c77c..0x0045c78d: FUN_004b5060 (Win3D::Create), SETNE.
    return Win3D::Create(NewParent, Data);
}

void NN_MainButton::Update()
{
    // LAB_0045c8c0.
    // 0x0045c8c5..0x0045c921: multiplayer with +0x52f set, no open panel and the server tick
    // running -- drop the letterbox (FUN_0041a0f0(0, 3.0f)), force the cursor drawn
    // (FUN_0041dda0(1)) as "Arrow" (FUN_0041dde0), and open the mission log through the
    // Event::Handler face (slot 0 with 0x67) with DAT_0067dcc8 lowered around the call.
    if (!SinglePlayer() && bForceMissionLog != 0 && OpenButton == nullptr &&
        !IsClientServerTickPaused())
    {
        SetLetterboxTarget(0.0f, 3.0f);
        SetClientMouseCursorDrawFlag(1);
        MouseSetCursor("Arrow", false);
        G_bNnScreenUp = false;
        OnEvent(0x67);
        G_bNnScreenUp = true;
    }

    // 0x0045c928: FUN_005a17b0 (the base update), then the two animations.
    Win3D::Update();
    ButtonMover::Update();  // FUN_0055e170
    ButtonScaler::Update(); // FUN_0055e5e0

    // 0x0045c949: FUN_004bc990 -- the story star's per-frame decision whether to show itself
    // (NN_StoryStar.cpp).
    StoryStarTick();

    // 0x0045c94e..0x0045c957: FUN_00489e00(float(FUN_0042d680())) -- the NN voice queue.
    NnVoiceTick(static_cast<float>(GetClientFrameTimeCapped()));

    // 0x0045c95c..0x0045c9a3: the inventory re-open once the dealer window has gone.
    if (bInventoryAfterDealer != 0 && !HudIsMousePickBlocked() &&
        Win3DFindByName("NN_Dealer") == nullptr && !ButtonMover::IsActive() &&
        !ButtonScaler::IsActive())
    {
        bInventoryAfterDealer = 0;
        HudPostCommand(0x66, 0);
    }

    // 0x0045c9ab..0x0045c9ef: the same while the dealer window is still there.
    if (bInventoryWithDealer != 0 && !HudIsMousePickBlocked() &&
        Win3DFindByName("NN_Dealer") != nullptr && !ButtonMover::IsActive() &&
        !ButtonScaler::IsActive())
    {
        bInventoryWithDealer = 0;
        HudPostCommand(0x66, 0);
    }
}

void NN_MainButton::Draw()
{
    // LAB_0045ca00.
    // 0x0045ca03..0x0045ca46: the HUD stopped picking (and no NeuroNet screen holds it): close
    // the open panel unless the mover runs or DAT_0066d359 holds it, and draw nothing.
#ifdef FREELANCER_WALK_MODE
    if (HudIsMousePickBlocked() && !G_bNnScreenUp && !BaseWalkShouldDrawInterface())
#else
    if (HudIsMousePickBlocked() && !G_bNnScreenUp)
#endif
    {
        if (OpenButton != nullptr && !ButtonMover::IsActive() && G_bNnKeepPanelOpen == 0)
        {
            ClosePanel();
        }
        return;
    }

    // 0x0045ca4b..0x0045ca6f: hidden under the letterbox (unless +0x52f), and while a NeuroNet
    // screen is up with no panel open.
    if (IsLetterboxVisible() && bForceMissionLog == 0)
    {
        return;
    }
    if (G_bNnScreenUp && OpenButton == nullptr)
    {
        return;
    }

    // 0x0045ca71..0x0045cacb: the children in list order. The closing button is skipped while
    // the background shrinks; the open button, once it has arrived, draws only its children
    // (FUN_0059e6e0, the panel window reparented under it); the rest draw when visible.
    Win3D* Child = GetChildHead();
    while (Child != nullptr)
    {
        if (Child == ClosingButton && ButtonScaler::IsActive())
        {
        }
        else if (Child == OpenButton && !ButtonMover::IsActive())
        {
            Win3DDrawChildrenOf(Child);
        }
        else if ((Child->Flags & kWin3DVisible) != 0)
        {
            Child->Draw();
        }
        Child = Child->GetNextSibling();
    }

    // 0x0045cacf: JMP FUN_004a2a80.
    NavMapPostPendingOpen();
}

intptr_t NN_MainButton::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0045d550 (RET 0xc).
    if (Msg == 0x4f && (ButtonScaler::IsActive() || ButtonMover::IsActive()))
    {
        return 1;
    }
    return WinControl::SetProperty(Msg, A, B);
}

bool NN_MainButton::OnEvent(unsigned MsgId)
{
    // FUN_0045cae0 (RET 4; this = the +0x32c face).
    if (MsgId == 0x67)
    {
        // 0x0045caee..0x0045cb10: the mission log is refused only by the screen/cursor/pause
        // gates, not by the docked / no-ship half of FUN_0045a740.
        if (G_bNnScreenUp || GetClientMouseCursorDrawFlag() == 0 || IsClientServerTickPaused() ||
            IsClientLocationInTransition()) // DAT_00668761 (0x0045cb09)
        {
            return false;
        }
    }
    else if (HudIsMousePickBlocked())
    {
        return false;
    }

    // 0x0045cb23..0x0045cb32: the byte-indexed switch over MsgId - 0x10 (0x0045cc6c).
    switch (MsgId)
    {
    case 0x6e:
        // 0x0045cb39: FUN_004b9b40 -- queue the current mission objective (DAT_0067230c) on the
        // story star's pending list.
        StoryStarRequeueLastObjective();
        return true;

    case 0x10:
        // 0x0045cb46: USER_CANCEL through the cancel stack.
        ClosePanel();
        return true;

    case 0x6b:
        // 0x0045cb59..0x0045cb8d: toggle the inventory.
        if (OpenButton == Buttons[1])
        {
            ClosePanel();
            return true;
        }
        bInventoryAfterDealer = 0;
        OpenPanel(Buttons[1], 0x66);
        return true;

    case 0x63:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    {
        WinControl* const Button = Buttons[NnPanelSlot(static_cast<int>(MsgId))];
        if (OpenButton == Button)
        {
            // 0x0045cbd2..0x0045cbff: clicking the open button closes it; with the dealer up
            // the inventory comes back once the dealer goes.
            if (Win3DFindByName("NN_Dealer") != nullptr)
            {
                bInventoryAfterDealer = 1;
            }
            ClosePanel();
            return true;
        }

        // 0x0045cc02..0x0045cc40
        bInventoryAfterDealer = 0;
        bInventoryWithDealer = 0;
        if (Win3DFindByName("NN_Dealer") != nullptr &&
            (ButtonMover::IsActive() || ButtonScaler::IsActive()))
        {
            bInventoryWithDealer = 1;
            return false;
        }

        // 0x0045cc43..0x0045cc55
        OpenPanel(Button, static_cast<int>(MsgId));
        return true;
    }

    default:
        // 0x64, 0x6c, 0x6d and every other id -> 0x0045cb12.
        return false;
    }
}

void NN_MainButton::OnMoveFinished()
{
    // LAB_0045d5a0 (this = the +0x330 face).
    // 0x0045d5a6..0x0045d5b9: a scale still running is finished first (slot 0 of the scaler
    // face).
    if (ButtonScaler::IsActive())
    {
        OnScaleFinished();
    }

    // 0x0045d5bb..0x0045d610: a closing button is back home -- hide it.
    if (ClosingButton != nullptr)
    {
        ClosingButton->Flags = static_cast<unsigned char>(ClosingButton->Flags & 0xfc);
        ClosingButton = nullptr;
        UpdateRootHighlights();
    }

    if (OpenButton != nullptr)
    {
        // 0x0045d61d..0x0045d6d6: the opening button arrived -- grow the background from the
        // slot's collapsed scale (0.05 in the info-card layout) to 1.
        int const Slot = NnPanelSlot(OpenPanelId);
        float Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[Slot];
        MakeScaledBackground(Scale);
        ButtonScaler::Start(kAnimSeconds);
        Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[Slot];
        ButtonScaler::AddButton(ScaledBackground, Scale, 1.0f);
        return;
    }

    // 0x0045d6d7..0x0045d713: nothing open -- drop the background.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // 0x0045d71d..0x0045d736: a panel queued while another was closing opens now.
    if (QueuedButton != nullptr)
    {
        OpenPanel(QueuedButton, QueuedPanelId);
        QueuedButton = nullptr;
    }
}

void NN_MainButton::OnScaleFinished()
{
    // LAB_0045d210 (this = the +0x350 face).
    if (ClosingButton != nullptr)
    {
        // 0x0045d229..0x0045d2ea: the background has shrunk -- delete it and slide the closing
        // button home from where it sat, with its tooltip back.
        if (ScaledBackground != nullptr)
        {
            Win3DDeleteTree(ScaledBackground);
            ScaledBackground = nullptr;
        }
        if (!ButtonMover::IsActive())
        {
            ButtonMover::Start(kAnimSeconds);
        }
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        ClosingButton->SetPosition(Zero);
        ButtonMover::AddButton(ClosingButton, ClosingStart, ClosingButton->HomeOffset);
        ClosingButton->ToolTipIds = ClosingToolTipIds;
        HudRemoveCancelHandler(AsHudCmdHandler(), 0);
        return;
    }

    if (OpenButton == nullptr)
    {
        return;
    }

    // 0x0045d2f4..0x0045d336: the background has grown. It stays only for the inventory in the
    // normal layout.
    if (OpenPanelId != 0x66 || bInfoCardLayout != 0)
    {
        if (ScaledBackground != nullptr)
        {
            Win3DDeleteTree(ScaledBackground);
            ScaledBackground = nullptr;
        }
    }
    bInfoCardLayout = 0;

    char const* const Name = NnPanelWindowName(OpenPanelId);
    if (Name == nullptr)
    {
        return;
    }

    // 0x0045d387..0x0045d3e8: a NeuroNet screen took over meanwhile (and +0x52f is clear) --
    // drop the open button without a window.
    if (bForceMissionLog == 0 && G_bNnScreenUp)
    {
        OpenButton->Flags = static_cast<unsigned char>(OpenButton->Flags & 0xfc);
        OpenButton = nullptr;
        UpdateRootHighlights();
        return;
    }

    // 0x0045d3e9..0x0045d42c: the panel window, made under the open button or moved there.
    Win3D* Window = Win3DFindByName(Name);
    if (Window == nullptr)
    {
        Window = WinFactoryCreate(Name, Name, OpenButton, nullptr);
    }
    else
    {
        Win3DReparent(Window, OpenButton);
    }
    SavedToolTipIds = OpenButton->ToolTipIds;
    OpenButton->ToolTipIds = 0;
    if (Window == nullptr)
    {
        return;
    }

    // 0x0045d432..0x0045d4ff
    OpenButton->SetPosition(PanelOrigin[NnPanelSlot(OpenPanelId)]);
    Window->Slot23(0xef01, 0, 0);
    Window->Flags = static_cast<unsigned char>(Window->Flags | 3);
    PlayOrRewindUiSoundSlot(2);
    HudPushCancelHandler(AsHudCmdHandler(), 0);
    UpdateRootHighlights();
}

void NN_MainButton::SetPanelOrigin(int PanelId, float const* Origin)
{
    // FUN_0045c7a0 (RET 8).
    float* const Out = PanelOrigin[NnPanelSlot(PanelId)];
    Out[0] = Origin[0];
    Out[1] = Origin[1];
    Out[2] = Origin[2];
}

WinControl* NN_MainButton::GetPanelButton(int PanelId) const
{
    // FUN_0045c820 (RET 4).
    return Buttons[NnPanelSlot(PanelId)];
}

void NN_MainButton::OpenPanel(WinControl* Button, int PanelId)
{
    // FUN_0045ccf0 (RET 8).
    // 0x0045ccf7..0x0045cd31: nothing while a NeuroNet screen is up (unless this open was
    // queued) or while either animation runs.
    if (QueuedButton == nullptr && G_bNnScreenUp)
    {
        return;
    }
    if (ButtonMover::IsActive() || ButtonScaler::IsActive())
    {
        return;
    }

    // 0x0045cd37..0x0045cd68: an open trade ends -- IServer slot 80 (+0x140) TerminateTrade(
    // DAT_00673344, 0), then the trade window's property 0xef02.
    if (G_NnTrade != nullptr)
    {
        G_Server->TerminateTrade(GetPlayerConnectionId(), 0);
        G_NnTrade->SetProperty(0xef02, 0, 0);
    }

    if (Button == OpenButton)
    {
        return;
    }

    // 0x0045cd7e: FUN_004424f0(1) -- the NavBar's open service tab closes.
    NavBarCloseService(true);

    // 0x0045cd85..0x0045cdaf: another panel is open -- queue this one and close that.
    if (OpenButton != nullptr)
    {
        QueuedPanelId = PanelId;
        QueuedButton = Button;
        ClosePanel();
        return;
    }

    // 0x0045cdb2..0x0045ce78: slide the button from its rest offset to the slot's open target
    // ({0, -0.075, -1.25} in the info-card layout).
    PlayOrRewindUiSoundSlot(0x1b);
    Button->Flags = static_cast<unsigned char>(Button->Flags | 3);
    ButtonMover::Start(kAnimSeconds);
    int const Slot = NnPanelSlot(PanelId);
    float Target[3];
    if (bInfoCardLayout != 0)
    {
        Target[0] = 0.0f;
        Target[1] = NnFloatBits(0xbd99999a);
        Target[2] = NnFloatBits(0xbfa00000);
    }
    else
    {
        Target[0] = OpenTarget[Slot][0];
        Target[1] = OpenTarget[Slot][1];
        Target[2] = OpenTarget[Slot][2];
    }
    ButtonMover::AddButton(Button, Button->HomeOffset, Target);
    OpenButton = Button;
    OpenPanelId = PanelId;
    HudPushCancelHandler(AsHudCmdHandler(), 0);
    UpdateRootHighlights();
}

void NN_MainButton::ResetPanel()
{
    // FUN_0045cef0.
    if (OpenButton != nullptr)
    {
        OpenButton->Flags = static_cast<unsigned char>(OpenButton->Flags & 0xfc);
    }
    OpenButton = nullptr;
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }
    // 0x0045cf54: JMP FUN_0055e290 on the mover.
    ButtonMover::Finish();
}

void NN_MainButton::ClosePanel()
{
    // FUN_0045cf60.
    if (ButtonMover::IsActive() || ButtonScaler::IsActive() || OpenButton == nullptr)
    {
        return;
    }
    char const* const Name = NnPanelWindowName(OpenPanelId);
    if (Name == nullptr)
    {
        return;
    }

    // 0x0045cfe6..0x0045d0a1: remember the closing button, where it sits and its tooltip.
    PlayOrRewindUiSoundSlot(0x1b);
    ClosingButton = OpenButton;
    ClosingPanelId = OpenPanelId;
    ClosingToolTipIds = SavedToolTipIds;
    if (bInfoCardLayout != 0)
    {
        ClosingStart[0] = 0.0f;
        ClosingStart[1] = NnFloatBits(0xbd99999a);
        ClosingStart[2] = NnFloatBits(0xbfa00000);
    }
    else
    {
        int const Slot = NnPanelSlot(OpenPanelId);
        ClosingStart[0] = OpenTarget[Slot][0];
        ClosingStart[1] = OpenTarget[Slot][1];
        ClosingStart[2] = OpenTarget[Slot][2];
    }

    // 0x0045d0a4..0x0045d0be: the panel window closes itself (slot 23, 0xef02).
    Win3D* const Window = Win3DFindByName(Name);
    if (Window != nullptr)
    {
        Window->Slot23(0xef02, 0, 0);
    }

    // 0x0045d0c1..0x0045d14d: a full-size background shrinks to the slot's scale.
    MakeScaledBackground(1.0f);
    int const ClosingSlot = NnPanelSlot(ClosingPanelId);
    ButtonScaler::Start(kAnimSeconds);
    float const Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[ClosingSlot];
    ButtonScaler::AddButton(ScaledBackground, 1.0f, Scale);

    OpenButton = nullptr;
    UpdateRootHighlights();
}

void NN_MainButton::MakeScaledBackground(float Scale)
{
    // FUN_0045d770 (RET 4).
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // 0x0045d7bc..0x0045d8ca: "ScaledBackground" -- the info card at (0, -0.086, -1.288) or the
    // slot's mesh at its BackgroundPosition; arguments 0, {0,0,0}, 0,0,0,1,0,0,1,0,0,0, {0,0,0}.
    int const Slot = NnPanelSlot(OpenPanelId);
    float const HalfExtents[3] = {0.0f, 0.0f, 0.0f};
    float const Zero338[3] = {0.0f, 0.0f, 0.0f};
    if (bInfoCardLayout != 0)
    {
        float const Position[3] = {0.0f, NnFloatBits(0xbdb020c5), NnFloatBits(0xbfa4dd2f)};
        ScaledBackground = CreateStaticControl(
            "ScaledBackground", "Interface\\NeuroNet\\Inventory\\INV_biginforcard_center.cmp",
            Position, nullptr, HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero338);
    }
    else
    {
        ScaledBackground = CreateStaticControl("ScaledBackground", kScaledBackgroundMeshes[Slot],
                                               BackgroundPosition[Slot], nullptr, HalfExtents, 0,
                                               0, 0, 1, 0, 0, 1, 0, 0, 0, Zero338);
    }

    // 0x0045d8d0..0x0045d9ca: the instance's rotation reset to identity scaled by Scale (the
    // translation kept), SetInstanceMatrix, UpdateInstance. No null or -1 test.
    void* const Instance = ScaledBackground->MeshHolder.EngineInstance;
    FTransform Transform;
    Transform.Rotation.D[0][0] = 1.0f;
    Transform.Rotation.D[0][1] = 0.0f;
    Transform.Rotation.D[0][2] = 0.0f;
    Transform.Rotation.D[1][0] = 0.0f;
    Transform.Rotation.D[1][1] = 1.0f;
    Transform.Rotation.D[1][2] = 0.0f;
    Transform.Rotation.D[2][0] = 0.0f;
    Transform.Rotation.D[2][1] = 0.0f;
    Transform.Rotation.D[2][2] = 1.0f;
    float const* const Current = DALib::Engine->GetInstanceTransform(Instance);
    Transform.Translation.X = Current[9];
    Transform.Translation.Y = Current[10];
    Transform.Translation.Z = Current[11];
    if (G_NnMainButtonMath == nullptr)
    {
        G_NnMainButtonMath = Acquire3DMathEngine();
    }
    G_NnMainButtonMath->Scale(Transform.Rotation, Transform.Rotation, Scale);
    DALib::Engine->SetInstanceMatrix(Instance, &Transform);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

bool NN_MainButton::HasActivePanel() const
{
    // FUN_0045da00.
    return OpenButton != nullptr;
}

void NN_MainButton::SetGroupButtonAlert(bool bAlert)
{
    // FUN_0045da10 (RET 4): property 0x33 (&DAT_00679ba8, bAlert) on the group button and its
    // root. No null test (only the multiplayer group control calls it).
    uintptr_t const Flag = bAlert ? 1u : 0u;
    GroupButton->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba8), Flag);
    GroupRoot->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba8), Flag);
}

void NN_MainButton::SetGroupButtonMesh(int Mode)
{
    // FUN_0045da50 (RET 4).
    char const* Path = nullptr;
    switch (Mode)
    {
    case 0:
        Path = "interface\\NeuroNet\\icons\\NN_request.3db"; // 0x005ceb1c
        break;
    case 1:
        Path = "interface\\NeuroNet\\icons\\NN_traderequest.3db"; // 0x005cedd8
        break;
    case 2:
        Path = "interface\\NeuroNet\\icons\\NN_grouprequest.3db"; // 0x005cee08
        break;
    default:
        return;
    }

    // One transform buffer for both controls: identity with zero translation, overwritten by a
    // bound instance's current transform (12 dwords) before the mesh is swapped and put back
    // after. The root reuses whatever the button left in it.
    FTransform Saved;
    Saved.Rotation.D[0][0] = 1.0f;
    Saved.Rotation.D[0][1] = 0.0f;
    Saved.Rotation.D[0][2] = 0.0f;
    Saved.Rotation.D[1][0] = 0.0f;
    Saved.Rotation.D[1][1] = 1.0f;
    Saved.Rotation.D[1][2] = 0.0f;
    Saved.Rotation.D[2][0] = 0.0f;
    Saved.Rotation.D[2][1] = 0.0f;
    Saved.Rotation.D[2][2] = 1.0f;
    Saved.Translation.X = 0.0f;
    Saved.Translation.Y = 0.0f;
    Saved.Translation.Z = 0.0f;
    void* const NoInstance = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));

    WinControl* const Controls[2] = {GroupButton, GroupRoot};
    for (int Index = 0; Index < 2; ++Index)
    {
        WinControl* const Control = Controls[Index];
        if (Control->MeshHolder.EngineInstance != NoInstance)
        {
            memcpy(&Saved, DALib::Engine->GetInstanceTransform(Control->MeshHolder.EngineInstance),
                   sizeof(Saved));
        }
        strcpy(Control->MeshPath, Path);
        ClientUiMeshHolderRelease(&Control->MeshHolder);
        ClientMeshLoadFromPath(&Control->MeshHolder, Path, 0, false);
        void* const Instance = Control->MeshHolder.EngineInstance;
        if (Instance != NoInstance)
        {
            DALib::Engine->SetInstanceMatrix(Instance, &Saved);
            DALib::Engine->UpdateInstance(Instance, 0, nullptr);
        }
    }
}

void NN_MainButton::FreeScaledBackground()
{
    // FUN_0045dc60.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }
}

void NN_MainButton::SetInfoCardLayout()
{
    // FUN_0045dcb0.
    bInfoCardLayout = 1;
}

void NN_MainButton::SetForceMissionLog(bool bForce)
{
    // FUN_0045dcc0 (RET 4).
    bForceMissionLog = bForce ? 1 : 0;
}

bool NN_MainButton::GetForceMissionLog() const
{
    // FUN_0045dcd0.
    return bForceMissionLog != 0;
}

void NN_MainButton::UpdateRootHighlights()
{
    for (int Index = 0; Index < 7; ++Index)
    {
        WinControl* const Root = Roots[Index];
        if (Root == nullptr)
        {
            continue;
        }
        bool const bLit = Buttons[Index] == OpenButton || Buttons[Index] == ClosingButton;
        Root->SetProperty(0xc, bLit ? 1u : 0u, 0);
    }
}
