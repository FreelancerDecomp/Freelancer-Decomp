// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_maneuvers.h"

#include "common_cobject.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_pub.h"
#include "common_server.h"
#include "game_options.h"
#include "globals.h"
#include "hud_command_dispatch.h"
#include "hud_status_core.h"
#include "hud_target.h"
#include "input_manager.h"
#include "login_mgr.h"
#include "menu_keymap_config.h"
#include "nn_messaging.h"
#include "nn_modal.h"
#include "nn_button_control.h"   // NnButtonControl::MontageTile (+0x338)
#include "nn_montage_draw.h"     // FindButtonMontageTile (FUN_0055a360)
#include "nn_ui_pulse.h"
#include "perf_options.h"
#include "player_behavior.h"
#include "player_spawn.h"
#include "space_camera.h"
#include "space_letterbox.h"
#include "win3d.h"
#include "win3d_engine_submit.h"

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // DAT_00674a58.
    HUD_Maneuvers* G_HudManeuvers = nullptr;

    // LAB_004d75a0 / LAB_004d75c0 (static initializers): DAT_00674a18, DAT_00674a14.
    unsigned const G_ManeuverVoiceCancelled = CreateID("cancelled");
    unsigned const G_ManeuverVoiceTradeLaneDisrupted = CreateID("nnvoice_trade_lane_disrupted");

    // DAT_00612638 -- the behaviour name -> button command table (four entries).
    FHudNamedValue const kManeuverCommands[4] = {
        {"Dock", 0x79},
        {"Goto", 0x7a},
        {"Formation", 0x7f},
        {"FreeFlight", 0xc6},
    };

    // DAT_006125fc -- the four buttons' rest offsets (12 bytes each; the loop at 0x004d7c03
    // walks EDI from 0x00612600 to 0x00612630).
    float const kButtonHomeOffsets[4][3] = {
        {0.0f, 0.231f, 0.3f},
        {-0.12f, 0.231f, 0.015f},
        {-0.07f, 0.231f, 0.015f},
        {-0.03f, 0.231f, 0.015f},
    };

    // DAT_00612654 -- the frame mesh indexed by the button count (clamped to 9). Entry 0 is the
    // last dword of kManeuverCommands (0xc6), which retail would read as a string pointer; the
    // only caller (UpdateButtons) counts every one of the four buttons, so index 0 is never
    // loaded -- the ctor leaves FrameButtonCount at 0, which the equality test skips.
    char const* const kFrameMeshes[10] = {
        nullptr,
        "Interface\\HUD\\HUD_maneuverbox2.cmp",
        "Interface\\HUD\\HUD_maneuverbox3.cmp",
        "Interface\\HUD\\HUD_maneuverbox4.cmp",
        "Interface\\HUD\\HUD_maneuverbox5.cmp",
        "Interface\\HUD\\HUD_maneuverbox6.cmp",
        "Interface\\HUD\\HUD_maneuverbox7.cmp",
        "Interface\\HUD\\HUD_maneuverbox8.cmp",
        "Interface\\HUD\\HUD_maneuverbox9.cmp",
        "Interface\\HUD\\HUD_maneuverbox9.cmp",
    };

    // DAT_0061262c -- the tab mesh UpdateTabAndCruise puts back.
    char const* const kTabMesh = "Interface\\Baseside\\CITY_maneuvertab1.cmp";

    // DAT_005d8274 (.rdata, a zero dword) -- the second id of the formation request.
    unsigned const kFormationRequestSecondId = 0;

    // FUN_00489e80(Id, 0) -- the NN voice queue (nn_messaging.cpp).
    void QueueManeuverVoice(unsigned VoiceId)
    {
        NnVoiceEnqueue(VoiceId, 0);
    }

    // The IDirectiveInfo words {Kind, 0, BehaviorId} RequestManeuver builds on its stack.
    IDirectiveInfo MakeDirectiveInfo(unsigned Kind, int BehaviorId)
    {
        IDirectiveInfo Info;
        unsigned const Words[3] = {Kind, 0u, static_cast<unsigned>(BehaviorId)};
        memcpy(Info.Data, Words, sizeof(Words));
        return Info;
    }

    // The ship's +0x10 / +0x1c / +0x28 (its orientation's third column) scaled by Scale, plus
    // its position. The x product is stored as a float before the add; y and z are added
    // unrounded (0x004d8fa6.. / 0x004d913e..).
    void PointAlongShipZ(CShip const* Ship, float Scale, float* Out)
    {
        float const Cx = Ship->Orientation.E[0][2];
        float const Cy = Ship->Orientation.E[1][2];
        float const Cz = Ship->Orientation.E[2][2];
        float const ScaledX = Cx * Scale;
        Out[0] = ScaledX + Ship->WorldPosition.x;
        Out[1] = static_cast<float>(static_cast<double>(Cy) * Scale + Ship->WorldPosition.y);
        Out[2] = static_cast<float>(static_cast<double>(Cz) * Scale + Ship->WorldPosition.z);
    }

    // The box transform the three hover tests share (0x004d86c6.., 0x004d8801.., 0x004d983f..):
    // identity with a zero position; a bound instance supplies its whole transform (Engine
    // +0xa0), otherwise the position is +0x60 + +0xa8; then +0xc0 is added. Unlike
    // WinControl::HitTest there is no +0x2d3 orientation reset.
    void ControlBoxTransform(WinControl* Control, float* Out12)
    {
        Out12[0] = 1.0f;
        Out12[1] = 0.0f;
        Out12[2] = 0.0f;
        Out12[3] = 0.0f;
        Out12[4] = 1.0f;
        Out12[5] = 0.0f;
        Out12[6] = 0.0f;
        Out12[7] = 0.0f;
        Out12[8] = 1.0f;
        Out12[9] = 0.0f;
        Out12[10] = 0.0f;
        Out12[11] = 0.0f;
        if (ClientMeshHolderHasInstance(&Control->MeshHolder))
        {
            float const* const Source =
                DALib::Engine->GetInstanceTransform(Control->MeshHolder.EngineInstance);
            memcpy(Out12, Source, 12 * sizeof(float));
        }
        else
        {
            Out12[9] = Control->Position[0] + Control->Offset[0];
            Out12[10] = Control->Position[1] + Control->Offset[1];
            Out12[11] = Control->Position[2] + Control->Offset[2];
        }
        Out12[9] = Out12[9] + Control->HitOffset[0];
        Out12[10] = Out12[10] + Control->HitOffset[1];
        Out12[11] = Out12[11] + Control->HitOffset[2];
    }

    // The player IObjRW's ship (FUN_0054baf0, +0xc, CBase::Object, class & 0x503), or null.
    CShip* PlayerShip(IObjRW* Player)
    {
        return HudShipOfObjRw(Player);
    }

    IObjRW* WatchedObjRw(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}

bool HudLookupNamedValue(FHudNamedValue const* Table, char const* Name, int* OutValue,
                         unsigned Count)
{
    // FUN_004d9ca0.
    for (unsigned i = 0; i < Count; ++i)
    {
        if (Table[i].Name != nullptr && _stricmp(Name, Table[i].Name) == 0)
        {
            *OutValue = Table[i].Value;
            return true;
        }
    }
    return false;
}

HUD_Maneuvers* GetHudManeuvers()
{
    return G_HudManeuvers;
}

HUD_Maneuvers::HUD_Maneuvers(INI_Reader& Reader)
{
    // FUN_004d75e0: HUDItem inlined (watcher FUN_004c95b0(0)), WinControl FUN_0059f860, the
    // empty vector, the watcher at +0x390 zeroed, the four vptrs, DAT_00674a58 = this.
    G_HudManeuvers = this;

    // 0x004d76c1..0x004d7924
    while (Reader.read_value())
    {
        if (!Reader.is_value("maneuver"))
            continue;

        // 0x004d76e5..0x004d7718: a local with the three strings emptied, appended
        // (FUN_004d9a70 is vector::insert at end); the rest is written on the new element.
        FHudManeuver Local;
        memset(&Local, 0, sizeof(Local));
        Maneuvers.push_back(Local);
        FHudManeuver& Maneuver = Maneuvers.back();

        Maneuver.Name = Reader.get_value_string(0);
        Maneuver.ToolTipIds = static_cast<int>(Reader.get_value_float(1));  // ftol
        Maneuver.Ids2 = static_cast<int>(Reader.get_value_float(2));        // ftol
        Maneuver.Mesh = Reader.get_value_string(3);

        // 0x004d77a5..0x004d77c1: NEG / SBB / AND -- the value when found, else 0.
        int Command = 0;
        bool const bFound = HudLookupNamedValue(kManeuverCommands, Maneuver.Name.Data, &Command, 4);
        Maneuver.CommandId = bFound ? Command : 0;

        Maneuver.BehaviorId = BehaviorTypes::get_behavior_id(Maneuver.Name.Data);
        Maneuver.GrayMesh = Reader.get_value_string(4);

        // 0x004d7811..0x004d7912: FUN_0055a360(std::string(Mesh)), then (GrayMesh) -- the
        // ButtonMontage tile of each path (the first lookup loads the montage).
        Maneuver.MontageTile = FindButtonMontageTile(std::string(Maneuver.Mesh.Data));
        Maneuver.GrayMontageTile = FindButtonMontageTile(std::string(Maneuver.GrayMesh.Data));
    }

    // 0x004d792a..0x004d7a28
    HUDItem::Name = "Maneuvers";
    HUD_Maneuvers::Create(nullptr, nullptr);   // direct call 0x004d7945
    Win3D::SetName("HUD_Maneuvers");
    ClassName = "HUD_Maneuvers";
    FormationState = 0;
    FormationTarget.set(nullptr);
    Field398[0] = 0.039f;                      // 0x3d1fbe77
    Field398[1] = 0.039f;
    Field398[2] = 0.0f;
    Field3a4 = -1;
    Field3a8 = -1;
    TrailField14 = 0.0f;
    FrameButtonCount = 0;                      // Create left 9 here
    ThrottleRate = 0.0f;
    TrailDistance = 100.0f;                    // DAT_005d828c
    bTabMeshReset = 1;
    Field3c8 = 0x13;
    Byte3cc = 0;
    Byte3cd = 0;
}

HUD_Maneuvers::~HUD_Maneuvers()
{
    // FUN_004d7a70: DAT_00674a58 = 0, then the +0x390 watcher (set_pointer(0) when watching),
    // the vector freed, Event::Handler FUN_00576010, ~WinControl FUN_0059fa50 and HUDItem's
    // watcher -- the member and base destructors, in that order.
    G_HudManeuvers = nullptr;
}

bool HUD_Maneuvers::Create(Win3D* NewParent, void* Data)
{
    // FUN_004d7b50 (this = the WinControl face).
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float const FramePos[3] = {0.0f, 0.176f, -0.5f};          // 0x3e343958, 0xbf000000
    Frame = CreateStaticControl("ManeuverFrame", "Interface\\HUD\\HUD_maneuverbox9.cmp", FramePos,
                                nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    FrameButtonCount = 9;
    WinControlHalfExtentsFromMesh(FrameHalfExtents, &Frame->MeshHolder);

    // 0x004d7bf4..0x004d7cda: the first four elements, whatever the vector holds.
    float const ButtonHalf[3] = {0.014f, 0.014f, 0.0f};        // 0x3c656042
    for (int i = 0; i < 4; ++i)
    {
        char ButtonName[0x64];
        sprintf(ButtonName, "ManeuverButton%d", i);
        float const Pos[3] = {kButtonHomeOffsets[i][0], kButtonHomeOffsets[i][1],
                              kButtonHomeOffsets[i][2]};
        FHudManeuver const& Maneuver = Maneuvers.data()[i];
        WinControl* const Button =
            CreateButtonControl(ButtonName, Maneuver.Mesh.Data, nullptr, "ui_select_item", nullptr,
                                Pos, nullptr, ButtonHalf, 0, 1, 0, 0, Maneuver.ToolTipIds, 0,
                                Maneuver.CommandId);
        Buttons[i] = Button;
        Button->HomeOffset[0] = Pos[0];
        Button->HomeOffset[1] = Pos[1];
        Button->HomeOffset[2] = Pos[2];
        Button->SetProperty(1, 1, 0);
    }

    float const TabPos[3] = {0.0f, 0.292f, -1.0f};             // 0x3e958106, 0xbf800000
    Tab = CreateStaticControl("ManeuverTab", "Interface\\Baseside\\CITY_maneuvertab1.cmp", TabPos,
                              nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    Tab->SetProperty(1, 1, 0);
    WinControlHalfExtentsFromMesh(TabHalfExtents, &Tab->MeshHolder);

    float const CruisePos[3] = {0.0f, 0.297f, 0.0f};           // 0x3e981062
    CruiseButton = CreateButtonControl("ManeuverCruiseButton", "interface\\HUD\\hud_mnvrwarp.3db",
                                       nullptr, "ui_select_item", nullptr, CruisePos, nullptr,
                                       ButtonHalf, 0, 1, 0, 0, 0xbf9, 0, 0x45);
    CruiseButton->SetProperty(1, 1, 0);

    // 0x004d7e08..0x004d7ef2: FUN_005760d0 on the handler at +0x364, in this order.
    unsigned const Commands[0x18] = {
        0x74, 0x76, 0x79, 0x7a, 0x7b, 0x77, 0x78, 0x7e, 0x7f, 0x80, 0xc6, 0x45,
        0x81, 0x82, 0x83, 0x84, 0x86, 0x85, 0x87, 0x88, 0x43, 0x44, 0x04, 0x0f,
    };
    for (unsigned i = 0; i < 0x18; ++i)
        RegisterEvent(Commands[i]);

    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    return Win3D::Create(NewParent, Data);     // FUN_004b5060, SETNZ
}

bool HUD_Maneuvers::OnEvent(unsigned MsgId)
{
    // LAB_004d7f60 (this = the handler face at +0x364).
    // 0x004d7f66..0x004d7f89: brake key up.
    if (MsgId == 0x78 && HudCommandRecord()->Byte18 == 0)
    {
        SetPlayerBrakeReverse(false);
        return true;
    }

    // 0x004d7f8c..0x004d7fa0 -> 0x004d8414: with the pick blocked or the letterbox up only the
    // strafe key-ups get through.
    if (HudIsMousePickBlocked() || IsLetterboxVisible())
    {
        if (MsgId != 0x85 && MsgId != 0x86 && MsgId != 0x87 && MsgId != 0x88)
            return false;
        if (HudCommandRecord()->Byte18 != 0)
            return false;
        SetPlayerStrafeBurst(0);
        return true;
    }

    // 0x004d7fa6..0x004d7fec: the player's ship and its behaviour manager, or nothing.
    CShip* const Ship = PlayerShip(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Ship == nullptr)
        return false;
    IBehaviorManager* const Behavior = Ship->get_behavior_interface();
    if (Behavior == nullptr)
        return false;
    int const Current = Behavior->CurrentBehaviorIndex;

    switch (MsgId)
    {
    case 0x81:
        // 0x004d800f: trail closer (FST, then FCOMP of the unrounded difference, TEST 5 / JP).
        if (Current == 3)
        {
            double const Closer = static_cast<double>(TrailDistance) - 20.0;   // DAT_005d8290
            TrailDistance = static_cast<float>(Closer);
            if (Closer < 20.0)                                                // DAT_005d8284
                TrailDistance = 20.0f;
            SetPlayerBehaviorDirectionGated(0.0f, 0.0f, TrailDistance);
        }
        return true;

    case 0x82:
        // 0x004d804e: trail farther (TEST 0x41 / JNZ: clamped only when above 2000).
        if (Current == 3)
        {
            double const Farther = 20.0 + static_cast<double>(TrailDistance);
            TrailDistance = static_cast<float>(Farther);
            if (Farther > 2000.0)                                             // DAT_005d8288
                TrailDistance = 2000.0f;
            SetPlayerBehaviorDirectionGated(0.0f, 0.0f, TrailDistance);
        }
        return true;

    case 0x83:
        RequestManeuver(5, false);
        return true;

    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
        // 0x004d80a4: key down -- the direction of the key; key up -- the first strafe key
        // still held (KeyMap FUN_005784a0), else none.
        if (HudCommandRecord()->Byte18 != 0)
        {
            SetPlayerStrafeBurst(static_cast<int>(MsgId) - 0x84);
            return true;
        }
        if (KeyMapIsCommandHeld(0x85))
        {
            SetPlayerStrafeBurst(1);
            return true;
        }
        if (KeyMapIsCommandHeld(0x86))
        {
            SetPlayerStrafeBurst(2);
            return true;
        }
        if (KeyMapIsCommandHeld(0x87))
        {
            SetPlayerStrafeBurst(3);
            return true;
        }
        if (KeyMapIsCommandHeld(0x88))
        {
            SetPlayerStrafeBurst(4);
            return true;
        }
        SetPlayerStrafeBurst(0);
        return true;

    case 0x45:
        SetPlayerCruise(!GetPlayerEngineCruiseActive());
        return true;

    case 0x0f:
    {
        // 0x004d81d9: USER_CANCEL.
        if (NnMainButtonHasActivePanel())
            return false;
        if (!HudStatusIsRearView() && HudStatusIsTurretView())
        {
            HudPostCommand(0xbf, 0);
            return true;
        }
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player == nullptr)
            return true;
        // 0x004d8240: a non-ship player object gives a null CShip, which retail hands to
        // is_launching as it is.
        CShip* const PlayerShipNow = PlayerShip(Player);
        if (PlayerShipNow->is_launching())
            return true;
        bool bCancelled = false;
        int const Now = PlayerShipNow->get_behavior_interface()->CurrentBehaviorIndex;
        if (Now != -1 && Now != 0x13)
        {
            if (!RequestManeuver(0x13, false))
                return false;
            bCancelled = true;
        }
        if (GetPlayerEngineCruiseActive())
        {
            SetPlayerCruise(false);
            QueueManeuverVoice(G_ManeuverVoiceCancelled);
        }
        else if (bCancelled)
        {
            QueueManeuverVoice(G_ManeuverVoiceCancelled);
        }
        return true;
    }

    case 0x77:
        SetPlayerEngageEngine(!GetPlayerEngineIsActive());
        return true;

    case 0x78:
        // Key up was handled above; the record is read again here.
        if (HudCommandRecord()->Byte18 == 0)
        {
            SetPlayerBrakeReverse(false);
            return true;
        }
        SetPlayerBrakeReverse(true);
        return true;

    case 0x43:
        ThrottleRate = HudCommandRecord()->Byte18 != 0 ? 1.0f : 0.0f;
        return true;

    case 0x44:
        if (GetPlayerEngineCruiseActive())
            SetPlayerCruise(false);
        ThrottleRate = HudCommandRecord()->Byte18 != 0 ? -1.0f : 0.0f;
        return true;

    case 0x04:
    {
        // 0x004d8354: the mouse wheel steps the throttle by 0.2 (DAT_005d8298).
        int const Wheel = GetClientMouseWheel();
        if (Wheel < 0)
        {
            double const Lower = static_cast<double>(GetLastPlayerThrottle()) -
                                 static_cast<double>(0.2f);
            float Throttle = static_cast<float>(Lower);
            if (Lower < 0.0)                                   // TEST 5 / JP
                Throttle = 0.0f;
            SetPlayerThrottle(Throttle);
        }
        else if (Wheel > 0)
        {
            double const Higher = static_cast<double>(GetLastPlayerThrottle()) +
                                  static_cast<double>(0.2f);
            float Throttle = static_cast<float>(Higher);
            if (Higher > 1.0)                                  // TEST 0x41 / JNZ
                Throttle = 1.0f;
            SetPlayerThrottle(Throttle);
        }
        return true;
    }

    case 0x74:
    case 0x76:
    case 0x79:
    case 0x7a:
    case 0x7b:
    case 0x7e:
    case 0x7f:
    case 0x80:
    case 0xc6:
    {
        // 0x004d83d1: the first of the first four maneuvers whose command this is.
        FHudManeuver* const First = Maneuvers.data();
        for (int i = 0; i < 4; ++i)
        {
            if (First[i].CommandId == static_cast<int>(MsgId))
            {
                ActivateManeuver(&First[i]);
                return true;
            }
        }
        return true;
    }

    default:
        // Includes 0x84: registered by Create, but its jump-table byte is the default.
        return false;
    }
}

bool HUD_Maneuvers::OnResult(int Param1, int Param2, int Param3)
{
    // LAB_004d9410 (this = the RequestCB face at +0x368).
    (void)Param3;
    if (Param1 != 1)
        return false;
    if (Param2 == 0)
    {
        FormationState = 2;
        RequestManeuver(0xa, true);
        return false;
    }
    if (Param2 == -1)
    {
        FormationState = 2;
        return false;
    }
    FormationState = 3;
    RequestManeuver(-1, false);
    return false;
}

void HUD_Maneuvers::UpdateInactive()
{
    // FUN_004d8570 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr &&
        !IsLetterboxVisible())
    {
        SetHiddenWithChildren(false);          // FUN_005a0c30(0)
    }
    // 0x004d8597..0x004d85b4: LAB_004682f0 inlined.
    Win3D::UpdateInactive();
}

void HUD_Maneuvers::Update()
{
    // LAB_004d85c0 (this = the WinControl face).
    // 0x004d85c4: TEST AH,0x44 / JNP -- skipped only when the rate compares equal to 0.
    if (!(ThrottleRate == 0.0f))
    {
        float const Last = GetLastPlayerThrottle();
        double const Sum =
            GetClientFrameTimeCapped() * static_cast<double>(ThrottleRate) + static_cast<double>(Last);
        float Throttle = static_cast<float>(Sum);
        if (Sum > 1.0)                          // TEST 0x41 / JNZ
            Throttle = 1.0f;
        if (static_cast<double>(Throttle) < 0.0) // TEST 5 / JP
            Throttle = 0.0f;
        SetPlayerThrottle(Throttle);
    }

    if (GetClientMouseCursorDrawFlag() != 0 && GetGameOption(kGameOptInterfaceGroup) != 0 &&
        GetGameOption(kGameOptHud) != 0 && GetPlayerObjRW() != nullptr && !IsLetterboxVisible())
    {
        UpdateButtons();
        Win3D::Update();                        // tail jump FUN_005a17b0
        return;
    }
    SetHiddenWithChildren(true);                // FUN_005a0c30(1)
    Win3D::Update();                            // tail jump FUN_005a17b0
}

void HUD_Maneuvers::Draw()
{
    // LAB_004d8690 (this = the WinControl face).
    if (IsSpaceInitInProgress())
        return;

    // 0x004d86a5..0x004d86c0: without the auto-hide option (Applied <= 0.999, or NaN) the bar
    // is drawn as it is.
    if (!(static_cast<double>(FindPerfOption("autohide_maneuver_bar")->Applied) > 0.999))
    {
        WinControl::Draw();                     // FUN_005a0760
        return;
    }

    // 0x004d86c6..0x004d88d7: otherwise only while the mouse ray is over the frame or the tab.
    float Transform[12];
    float Ray[3];
    ControlBoxTransform(Frame, Transform);
    if (Win3DRayHitsAabbHalfExtents(FrameHalfExtents, Transform, Win3DGetMouseRay(Ray)))
    {
        WinControl::Draw();
        return;
    }
    ControlBoxTransform(Tab, Transform);
    if (Win3DRayHitsAabbHalfExtents(TabHalfExtents, Transform, Win3DGetMouseRay(Ray)))
        WinControl::Draw();
}

bool HUD_Maneuvers::HitTest(float const* Ray)
{
    // LAB_004d9820 (this = the WinControl face).
    if (WinControl::HitTest(Ray))               // FUN_005a0340
        return true;
    float Transform[12];
    ControlBoxTransform(Tab, Transform);
    return Win3DRayHitsAabbHalfExtents(TabHalfExtents, Transform, Ray);
}

intptr_t HUD_Maneuvers::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004d8da0 (this = the WinControl face). Only 0x26 is handled; nothing is forwarded to
    // WinControl.
    (void)B;
    if (Msg != 0x26)
        return 0;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return 0;
    // 0x004d8db3..0x004d8dd2: the behaviour manager is fetched and not used.
    CShip* const Ship = PlayerShip(Player);
    (void)Ship->get_behavior_interface();

    for (int i = 0; i < 4; ++i)
    {
        if (reinterpret_cast<uintptr_t>(Buttons[i]) == A)
        {
            ActivateManeuver(&Maneuvers.data()[i]);
            return 0;
        }
    }
    return 0;
}

void HUD_Maneuvers::UpdateButtons()
{
    // FUN_004d88f0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    IBehaviorManager* const Behavior = PlayerShip(Player)->get_behavior_interface();
    int const Current = Behavior->CurrentBehaviorIndex;
    int const ManeuverCount = static_cast<int>(Maneuvers.size());

    // 0x004d8974: buttons 0, 2, 1, 3.
    int const Order[4] = {0, 2, 1, 3};
    FHudManeuver* LastShown = nullptr;
    int Shown = 0;
    for (int i = 0; i < 4; ++i)
    {
        int const Index = Order[i];
        FHudManeuver* const Maneuver = &Maneuvers.data()[Index];
        int const Id = Maneuver->BehaviorId;
        Maneuver->bSelected = 0;

        // 0x004d89bb..0x004d8a02: available unless (-1 / 0x12 and not the current behaviour),
        // or the manager answers 2 (EvalSkip) for it -- which a disrupted trade-lane dock and the
        // current behaviour still pass.
        bool bAvailable = true;
        if ((Id == -1 || Id == 0x12) && Current != Id)
        {
            bAvailable = false;
        }
        else if (Behavior->external_player_evaluate(Id) == EvalSkip)
        {
            if (!(Maneuver->BehaviorId == 7 && IsTargetTradeLaneDisrupted()) &&
                Current != Maneuver->BehaviorId)
            {
                bAvailable = false;
            }
        }

        WinControl* const Button = Buttons[Index];
        if (bAvailable)
        {
            // 0x004d8a08..0x004d8ab2
            ++Shown;
            Button->SetProperty(1, 0, 0);
            LastShown = Maneuver;
            Button->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679b94), 0);
            bool const bCurrent = Maneuver->BehaviorId == Current;
            Button->SetProperty(0xc, bCurrent ? 1u : 0u, 0);

            bool bGray = bCurrent;
            if (!bGray && !HudIsMousePickBlocked())
            {
                float Ray[3];
                bGray = Button->HitTest(Win3DGetMouseRay(Ray));   // slot 4
            }
            // The buttons are ButtonControls (CreateButtonControl, "ButtonControl" factory); the
            // montage tile goes straight into their +0x338.
            if (bGray && Maneuver->GrayMesh.Length != 0)
            {
                Button->SetProperty(4, reinterpret_cast<uintptr_t>(Maneuver->GrayMesh.Data), 0);
                static_cast<NnButtonControl*>(Button)->MontageTile = Maneuver->GrayMontageTile;
            }
            else
            {
                Button->SetProperty(4, reinterpret_cast<uintptr_t>(Maneuver->Mesh.Data), 0);
                static_cast<NnButtonControl*>(Button)->MontageTile = Maneuver->MontageTile;
            }
        }
        else
        {
            // 0x004d8ad4..0x004d8b1f: shown too, gray, and tinted with B = 1. The +0x338 store
            // sits between the mesh and the colour call.
            Button->SetProperty(1, 0, 0);
            Button->SetProperty(4, reinterpret_cast<uintptr_t>(Maneuver->GrayMesh.Data), 0);
            static_cast<NnButtonControl*>(Button)->MontageTile = Maneuver->GrayMontageTile;
            Button->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679b94), 1);
            ++Shown;
        }
    }
    if (LastShown != nullptr)
        LastShown->bSelected = 1;

    SetFrameButtonCount(Shown);

    // 0x004d8b50..0x004d8bd3: the visible buttons laid out left to right, 0.04 apart
    // (DAT_005d845c), centred (an even count starts half a step, DAT_005d1fe4, to the right).
    float const Start = (Shown & 1) != 0 ? 0.0f : 0.02f;
    float X = static_cast<float>(static_cast<double>(Start) -
                                 static_cast<double>(Shown / 2) * static_cast<double>(0.04f));
    for (int i = 0; i < ManeuverCount; ++i)
    {
        // Retail walks Buttons[] as far as the vector's size (hud.ini has four lines).
        WinControl* const Button = Buttons[i];
        if ((Button->Flags & kWin3DVisible) == 0)
            continue;
        float const Pos[3] = {X, 0.1777f, 0.5f};    // 0x3e35f6fd, 0x3f000000
        Button->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
        X = X + 0.04f;
    }

    UpdateTabAndCruise(Current);
}

void HUD_Maneuvers::UpdateTabAndCruise(int BehaviorId)
{
    // FUN_004d8bf0.
    if (BehaviorId != 0x13 && BehaviorId != 2 && BehaviorId != 3 && BehaviorId != 5 &&
        BehaviorId != 7)
    {
        Tab->SetProperty(1, 1, 0);
        CruiseButton->SetProperty(1, 1, 0);
        return;
    }

    Tab->SetProperty(1, 0, 0);

    // 0x004d8c4d..0x004d8c90: the offset of the button the tab sits under (Goto: button 1,
    // Dock: button 2, anything else: button 0), read through property 7.
    float Offset[3] = {0.0f, 0.0f, 0.0f};
    WinControl* Anchor = Buttons[0];
    if (BehaviorId == 2)
        Anchor = Buttons[1];
    else if (BehaviorId == 7)
        Anchor = Buttons[2];
    Anchor->SetProperty(7, reinterpret_cast<uintptr_t>(Offset), 0);

    if (bTabMeshReset != 1)
    {
        bTabMeshReset = 1;
        Tab->SetProperty(4, reinterpret_cast<uintptr_t>(kTabMesh), 0);
        WinControlHalfExtentsFromMesh(TabHalfExtents, &Tab->MeshHolder);
    }

    // 0x004d8cd4..0x004d8cff
    Offset[0] = Offset[0] + Offset[0];
    Offset[1] = 0.294f;                          // 0x3e966cf5
    Offset[2] = -1.0f;
    Tab->SetProperty(6, reinterpret_cast<uintptr_t>(Offset), 0);

    // 0x004d8d05..0x004d8d4a: the cruise button is shown either way; away from Goto, FreeFlight
    // and Dock it moves left by 0.032 (DAT_005d69c0).
    CruiseButton->SetProperty(1, 0, 0);
    if (BehaviorId != 2 && BehaviorId != 0x13 && BehaviorId != 7)
        Offset[0] = Offset[0] - 0.032f;

    Offset[1] = 0.297f;                          // 0x3e981062
    Offset[2] = 0.0f;
    CruiseButton->SetProperty(6, reinterpret_cast<uintptr_t>(Offset), 0);
    CruiseButton->SetProperty(0xc, GetPlayerEngineCruiseActive() ? 1u : 0u, 0);
}

bool HUD_Maneuvers::RequestManeuver(int BehaviorId, bool bFlag)
{
    // FUN_004d8e20.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return false;
    // 0x004d8e46..0x004d8e67: a non-ship player object gives a null CShip, which retail passes
    // to get_behavior_interface as it is.
    CShip* const Ship = PlayerShip(Player);
    IBehaviorManager* const Behavior = Ship->get_behavior_interface();

    IDirectiveInfo const Info = MakeDirectiveInfo(1, BehaviorId);   // +0x24 {1, 0, id}
    bool bResult = false;

    // 0x004d8e7c: switch on BehaviorId + 1 (byte table 0x004d93ec, eleven cases).
    switch (BehaviorId)
    {
    case -1:
    case 0x13:
    {
        // 0x004d8ea5: free flight -- {1, 0, 0x13} with no op, then the steering reset.
        IDirectiveInfo const FreeFlight = MakeDirectiveInfo(1, 0x13);
        bResult = Behavior->set_current_directive(FreeFlight, nullptr);
        ResetPlayerBehaviorDirection();
        break;
    }

    case 3:
    {
        // 0x004d8ed4: trail the ship's target.
        pub::AI::DirectiveTrailOp Op;
        IObjRW* const Target = Ship->get_target();
        if (Target == nullptr)
            return false;
        Op.TargetId = Target->get_id();
        Op.TrailDistance = TrailDistance;
        Op.Field_14 = TrailField14;
        bResult = Behavior->set_current_directive(Info, &Op);
        break;
    }

    case 0xd:
    {
        // 0x004d8f2b: strafe past the target.
        pub::AI::DirectiveStrafeOp Op;
        IObjRW* const Target = Ship->get_target();
        if (Target == nullptr)
            return false;
        Op.TargetId = Target->get_id();
        bResult = Behavior->set_current_directive(Info, &Op);
        break;
    }

    case 0xf:
    {
        // 0x004d8f6a: face the target (mode 1), or a point 10000 ahead (mode 2, DAT_005ca21c).
        pub::AI::DirectiveFaceOp Op;
        IObjRW* const Target = Ship->get_target();
        if (Target != nullptr)
        {
            Op.TargetId = Target->get_id();
            Op.Mode = 1;
            Op.Field_44 = 1;
        }
        else
        {
            float Point[3];
            PointAlongShipZ(Ship, -10000.0f, Point);
            Op.Mode = 2;
            Op.TargetId = 0;
            Op.Field_44 = 1;
            Op.FacePoint.x = Point[0];
            Op.FacePoint.y = Point[1];
            Op.FacePoint.z = Point[2];
        }
        bResult = Behavior->set_current_directive(Info, &Op);
        break;
    }

    case 2:
    {
        // 0x004d902f: go to the target, or to a point 100000 ahead (DAT_005d8460).
        pub::AI::DirectiveGotoOp Op;
        Op.Field_58 = 0;
        IObjRW* const Target = Ship->get_target();
        if (Target != nullptr)
        {
            Op.Field_0C = 0;
            Op.Field_1C = static_cast<int32_t>(Target->get_id());
            float Radius = 0.0f;
            Vector Center;
            Target->get_physical_radius(Radius, Center);
            // get_type's result is not tested; the type word is read either way (retail reads
            // an unwritten stack slot when the call fails; the port starts it at 0).
            unsigned Type = 0;
            Target->get_type(Type);
            float Factor = 1.0f;                                   // 0x004d908b
            if ((Type & 0x30000u) != 0)
                Factor = 5.0f;                                     // DAT_005d8464
            else if ((Type & 0x140000u) != 0)
                Factor = 2.0f;                                     // DAT_005d55b4
            else if ((Type & 0x680000u) != 0)
                Factor = 1.0f;                                     // DAT_005c75dc
            else if ((Type & 0xfu) != 0)
                Factor = 0.5f;                                     // DAT_005c75e0
            else if ((Type & 0xfdffu) != 0)
                Factor = 1.0f;                                     // DAT_005c75dc
            double const Range = static_cast<double>(Factor) * static_cast<double>(Radius);
            Op.Field_50 = static_cast<float>(Range);
            if (Range < 100.0)                                     // DAT_005ca320, TEST 5 / JP
                Op.Field_50 = 100.0f;
            if (Type == 0x200u)
                Op.Field_50 = 100.0f;
            else if (Type == 0x80u)
                Op.Field_50 = 750.0f;                              // 0x443b8000
        }
        else
        {
            Op.Field_0C = 1;
            Op.Field_50 = 500.0f;                                  // 0x43fa0000
            float Point[3];
            PointAlongShipZ(Ship, -100000.0f, Point);
            Op.Destination.x = Point[0];
            Op.Destination.y = Point[1];
            Op.Destination.z = Point[2];
        }
        bResult = Behavior->set_current_directive(Info, &Op);
        if (Target == nullptr)
            ResetPlayerBehaviorDirection();
        break;
    }

    case 0xa:
    {
        // 0x004d91d6: formation.
        if (bFlag)
        {
            // 0x004d91e1: the accepted request -- the op is set, the result is not kept.
            pub::AI::DirectiveFormationOp Op;
            Behavior->set_current_directive(Info, &Op);
            break;
        }
        if (FormationState == 1)
            break;

        // 0x004d9212..0x004d92f2: watch the target (or its formation leader), then ask the
        // player's ship for a formation request (IObjAffect slot 23) and return at once.
        IObjRW* const Target = Ship->get_target();
        FormationTarget.set(Target != nullptr ? static_cast<Watchable const*>(Target) : nullptr);
        if (FormationTarget.target() == nullptr || WatchedObjRw(FormationTarget) == nullptr)
            return true;
        IObjRW* Leader = nullptr;
        WatchedObjRw(FormationTarget)->get_formation_leader(Leader);
        if (Leader != nullptr)
            FormationTarget.set(static_cast<Watchable const*>(Leader));
        FormationState = 1;
        if (Behavior->CurrentBehaviorIndex == 7)
        {
            IDirectiveInfo const Undock = MakeDirectiveInfo(0, BehaviorId);
            Behavior->set_current_directive(Undock, nullptr);
        }
        unsigned const TargetId = WatchedObjRw(FormationTarget)->get_id();
        IObjRW* const Self = WatchedObjRw(HUDItem::Target);
        static_cast<IObjAffect*>(Self)->slot_5c(1, &TargetId, &kFormationRequestSecondId, 0,
                                                static_cast<IObjAffect::RequestCB*>(this));
        return true;
    }

    case 7:
    {
        // 0x004d9304: dock with the target.
        pub::AI::DirectiveDockOp Op;
        IObjRW* const Target = Ship->get_target();
        if (Target == nullptr)
            return false;
        Op.DockObjectId = Target->get_id();
        bResult = Behavior->set_current_directive(Info, &Op);
        break;
    }

    case 0xc:
    case 0x10:
        // 0x004d9340 / 0x004d9349: the directive with no op.
        bResult = Behavior->set_current_directive(Info, nullptr);
        break;

    default:
        // Any other id (5 among them, from OnEvent 0x83): no directive, only the server call.
        break;
    }

    // 0x004d935a..0x004d93ac: with a target, IServer::SetManeuver (slot 66, +0x108) on the
    // connection DAT_00673344 with {this panel's object id, the target's id, BehaviorId}.
    IObjRW* const Target = Ship->get_target();
    if (Target != nullptr)
    {
        XSetManeuver Maneuver;
        Maneuver.Field_0x00 = WatchedObjRw(HUDItem::Target)->get_id();
        Maneuver.ObjectId = Target->get_id();
        Maneuver.Field_0x08 = static_cast<unsigned>(BehaviorId);
        G_Server->SetManeuver(GetPlayerConnectionId(), Maneuver);
    }
    return bResult;
}

void HUD_Maneuvers::ActivateManeuver(FHudManeuver* Maneuver)
{
    // FUN_004d9470.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    if (HudIsMousePickBlocked())
        return;
    Field3a4 = -1;
    Field3a8 = -1;

    if (Maneuver->BehaviorId == -1)
    {
        RequestManeuver(-1, false);
        QueueManeuverVoice(G_ManeuverVoiceCancelled);
        return;
    }

    IBehaviorManager* const Behavior = PlayerShip(Player)->get_behavior_interface();
    int const Current = Behavior->CurrentBehaviorIndex;
    int const Id = Maneuver->BehaviorId;
    if (Current == Id)
    {
        // 0x004d950a: the running maneuver again -- re-sent unless it is none / free flight.
        if (Current == -1 || Current == 0x13)
            return;
        RequestManeuver(Id, false);
        return;
    }

    if (Behavior->external_player_evaluate(Id) != EvalSkip)
    {
        // 0x004d953a: FUN_00489ea0(Name) -- CreateID(Name) into the voice queue.
        NnVoiceEnqueueByName(Maneuver->Name.Data);
        RequestManeuver(Maneuver->BehaviorId, false);
        if (Maneuver->BehaviorId == 3)
        {
            TrailDistance = 100.0f;              // DAT_005d828c
            TrailField14 = 0.0f;
        }
        return;
    }

    // 0x004d957b: refused -- a dock at a trade-lane ring with its shield down says why.
    if (Maneuver->BehaviorId == 7 && IsTargetTradeLaneDisrupted())
        QueueManeuverVoice(G_ManeuverVoiceTradeLaneDisrupted);
}

void HUD_Maneuvers::SetFrameButtonCount(int Count)
{
    // FUN_004d95b0.
    if (Count == -1)
    {
        // 0x004d95c4..0x004d9663: count the maneuvers the manager does not refuse.
        Count = 0;
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player == nullptr)
            return;
        IBehaviorManager* const Behavior = PlayerShip(Player)->get_behavior_interface();
        int const ManeuverCount = static_cast<int>(Maneuvers.size());
        for (int i = 0; i < ManeuverCount; ++i)
        {
            int const Id = Maneuvers[i].BehaviorId;
            if (Id != -1 && Behavior->external_player_evaluate(Id) != EvalSkip)
                ++Count;
        }
    }

    if (FrameButtonCount == Count)
        return;
    FrameButtonCount = Count;
    if (Count > 9)
        FrameButtonCount = 9;
    Frame->SetProperty(4, reinterpret_cast<uintptr_t>(kFrameMeshes[FrameButtonCount]), 0);
    WinControlHalfExtentsFromMesh(FrameHalfExtents, &Frame->MeshHolder);

    // 0x004d96c2..0x004d973f: the frame box grows (x1.1, y2.5); this window's own box and hit
    // offset are fixed.
    FrameHalfExtents[1] = FrameHalfExtents[1] * 2.5f;     // DAT_005d74c8
    HalfExtents[0] = 0.174f;                               // 0x3e322d0e
    FrameHalfExtents[0] = FrameHalfExtents[0] * 1.1f;     // DAT_005d8468
    HalfExtents[1] = 0.036f;                               // 0x3d1374bc
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.002f;                                 // 0x3b03126f
    HitOffset[1] = 0.351f;                                 // 0x3eb3b646
    HitOffset[2] = 0.0f;
}

bool HUD_Maneuvers::IsTargetTradeLaneDisrupted()
{
    // FUN_004d9750.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return false;
    CShip* const Ship = PlayerShip(Player);
    if (Ship == nullptr)
        return false;
    IObjRW* const Target = Ship->get_target();
    if (Target == nullptr)
        return false;
    unsigned Type = 0;
    if (Target->get_type(Type) != 0)
        return false;
    // 0x004d97b5: JS on the low byte -- type bit 0x80 (a trade-lane ring).
    if ((Type & 0x80u) == 0)
        return false;
    CObject const* const Object = Target->cobject();                  // slot +0x150
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
        return false;
    float MaxShield = 0.0f;
    float CurShield = 0.0f;
    HudSampleShieldHitPoints(const_cast<CEqObj*>(static_cast<CEqObj const*>(Object)), &MaxShield,
                             &CurShield, nullptr);
    // TEST 0x41 / JNZ: true when the shield is at or below zero (or NaN).
    return !(static_cast<double>(CurShield) > 0.0);
}

namespace
{
    HUDFACTORY<HUD_Maneuvers> G_HudManeuversFactory("Maneuvers");
}
