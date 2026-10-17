// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "space_state.h"
#include "space_app.h"

#include "character_info.h"         // ResetCharacterScripts (FUN_00435630)
#include "chat_init.h"              // ChatShowMsgWin (FUN_0046a4a0)
#include "login_mgr.h"              // WaitForServerResponse (FUN_005a89b0), DAT_0067ecbe
#include "main_command_handler.h"   // SetClientQuitRequested (DAT_0067ecbd)
#include "nn_arrival_text.h"        // NnArrivalTextShowSystemNow (FUN_0045b490)
#include "space_scene_load.h"       // FUN_005020a0 / FUN_00510f90
#include "hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740)
#include "common_misc.h"            // Geometry::Sphere, SinglePlayer, GetFileSysPath, GetFullFilename
#include "common_universe.h"        // ISpatialPartition, SpatialFill
#include "dacom.h"                  // FDUMP
#include "character_select_equip.h" // FUN_004c4a30 / FUN_004c4a60
#include "client_equip_fx.h"
#include "client_frame_tick.h"      // FUN_00424f30 / FUN_00425080 / FUN_00425140, FUN_0041b2e0
#include "client_tick_profile.h"    // Port-only owner-requested diagnostics
#include "client_fuse.h"
#include "client_gameobj.h"         // FClientGameobj (the player's object, FUN_0054a940)
#include "client_objdb.h"
#include "cockpit_load.h"
#include "effect_type_list.h"
#include "engine_light.h"
#include "fade_to_black.h"
#include "game_options.h"
#include "hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "hud_element_list.h"
#include "hud_status_core.h"        // GetHudStatus (DAT_00674aa0) +0x5b9 / +0x5ba
#include "hud_targetable_objects.h" // FUN_004ec400
#include "input_manager.h"
#include "gameflow.h"               // ResetEngineInstanceCache (FUN_004348b0 on DAT_00667e50)
#include "gate_tunnel.h"            // FUN_004fe180
#include "jump_effect.h"            // FUN_00502aa0
#include "loading_screen.h"         // LoadingScreenReset (FUN_00579190)
#include "menu_keymap_config.h"
#include "menu_sound_arch.h"
#include "menu_ui_widgets.h"
#include "nn_exit.h"                // NnExitPostShipLostMessage (FUN_00471e80)
#include "nn_modal.h"
#include "player_behavior.h"
#include "player_spawn.h"
#include "sound_config.h"
#include "space_camera.h"
#include "space_camera_game.h"
#include "space_letterbox.h"        // IsLetterboxVisible / SetLetterboxTarget (FUN_0041a3e0 / 0041a0f0)
#include "space_scene_obj_draw.h"   // LightGridRebuild (FUN_00537c30), SpaceDrawQueueRelease (FUN_0051fbc0)
#include "star_sphere.h"
#include "star_sys.h"
#include "tradelane_fx.h"
#include "win3d.h"                  // Win3DDrawAll (FUN_0059e300)
#include "win3d_renderables.h"
#include "common_animdb.h"
#include "common_iface.h"
#include "common_physys.h"
#include "DALib.h"
#include "globals.h"
#include "render/IRenderPipeline.h"
#include "win3d_render.h"
#include "common_cobject.h"
#include "common_ini.h"     // RefreshLoadingProgress
#include "common_server.h"  // IServer (DAT_0067ecd0)
#include "server_connect.h" // GetLocalServerInterface

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // DAT_00679778: the value the space frame returns -- the next client state, 0 to stay.
    // The frame zeroes it at 0x0054b02b and raises 6 from +0x0a (0x0054b208); the space init
    // zeroes it (0x0054abec / 0x0054ac03 / 0x0054ae5d) and FUN_0054bc60 stores 4.
    int G_SpaceNextState = 0;

    // DAT_00679784: the base a landing ship asked for (FUN_00548ef0). The space frame reads it
    // at 0x0054aaea (ProcessPendingSystemSwitch: FUN_004c4910 with it, FUN_004c4a10(0), next
    // state 3, then zeroes it).
    unsigned G_PendingLandBase = 0;

    // DAT_0067977c / DAT_00679780: the system a jumping ship asked for and a byte beside it
    // (FUN_00548ed0, the Ship's jump state 3 at 0x005439d6 and the IClient system-switch-out
    // handler FUN_00543750 at 0x0054378a). ProcessPendingSystemSwitch consumes the system and
    // zeroes both; nothing in the listing reads DAT_00679780 (XREFs: 0x00548ee1 W, 0x0054aad9 W).
    unsigned G_PendingJumpSystem = 0;
    unsigned G_PendingJumpFlag = 0;

    // The player's ship as a CEqObj through the watcher DAT_00679744: minus eight, the +0xc'd
    // pointer's +0x4, ship class mask 0x503 -- the chain FUN_0054b230 walks twice. Null when any
    // link is missing.
    CEqObj* PlayerShipEqObj()
    {
        void* const ObjRW = GetPlayerObjRW();
        if (ObjRW == nullptr || ObjRW == reinterpret_cast<void*>(-0xc))
            return nullptr;
        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj == nullptr ||
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return EqObj;
    }

    // FUN_00543f00 (cdecl, one caller 0x0054b40d): the docking port's camera hardpoint
    // "HPDockCam" + ('A' + PortIndex) (sprintf "%s%c" into a 0x40-byte buffer), read through the
    // port's IObjInspect slot 48 get_hardpoint(Name, &Csys[9] position, &Csys[0] orientation);
    // true when that answers 0 (NEG / SBB / INC).
    bool GetDockCameraCsys(IObjRW const* Port, int PortIndex, float* Csys12)
    {
        char Name[0x40];
        sprintf_s(Name, "%s%c", "HPDockCam", PortIndex + 0x41);
        return Port->get_hardpoint(Name, reinterpret_cast<Vector*>(Csys12 + 9),
                                   reinterpret_cast<Matrix*>(Csys12)) == 0;
    }

    // FUN_0054b230 (thiscall on the state, 0x0054b08d).
    void UpdateSpaceDocking(SpaceApp* State)
    {
        if (State->bSpaceReady != 0)
        {
            // 0x0054b242..0x0054b2b6: once the ship is no longer excluded from control bit 4
            // the launch is over -- +0xba9 drops and, unless +0xba8 holds it, the camera goes
            // back to "player" (attach target 0).
            CEqObj* const EqObj = PlayerShipEqObj();
            if (EqObj != nullptr && !EqObj->is_control_excluded(4u))
            {
                State->bSpaceReady = 0;
                if (State->bScriptCameraLatch == 0)
                    SwitchSpaceCameraMode("player", nullptr);
            }
        }
        else if (State->bDockingPending == 0)
        {
            // 0x0054b2c9..0x0054b2e1: the object the active camera (+0xba0, not tested again
            // after the frame's own test) watches through its +0xc0 BaseWatcher, minus eight;
            // null when nothing is watched.
            FGameCamera* const Cam = reinterpret_cast<FGameCamera*>(GetActiveSpaceCamera());
            Watchable const* const Watched = Cam->Watched.target();
            void const* const WatchedObjRW =
                Watched != nullptr ? reinterpret_cast<unsigned char const*>(Watched) - 8
                                   : nullptr;
            // 0x0054b2e1..0x0054b325: the player's ship CEqObj (the chain above) -- and the
            // camera must be watching exactly that object.
            CEqObj* const EqObj = PlayerShipEqObj();
            if (EqObj != nullptr && WatchedObjRW == GetPlayerObjRW())
            {
                // 0x0054b32b..0x0054b34d: CEqObj::get_behavior_interface, then
                // IBehaviorManager::get_docking_port(&Port, &Index); its answer is +0xbaa.
                IObjRW const* Port = nullptr;
                int PortIndex = 0;
                State->bDockingPending = static_cast<char>(
                    EqObj->get_behavior_interface()->get_docking_port(Port, PortIndex) ? 1 : 0);
                if (State->bDockingPending != 0)
                {
                    // 0x0054b353..0x0054b36d: the server (DAT_0067ecd0, no test) slot 57
                    // SPRequestInvincibility(ship +0xb0 id, 1, 2, DAT_00673344).
                    static_cast<IServer*>(GetLocalServerInterface())
                        ->SPRequestInvincibility(static_cast<CSimple*>(EqObj)->Id, true,
                                                 INVINCIBILITY_REASON_2,
                                                 GetPlayerConnectionId());
                    // 0x0054b373..0x0054b39c: the port's type (IObjInspect slot 34 get_type
                    // into a zeroed local, skipped without a port) tested for 0x120.
                    unsigned PortType = 0;
                    if (Port != nullptr)
                        Port->get_type(PortType);
                    if ((PortType & 0x120u) != 0)
                    {
                        // 0x0054b3a2..0x0054b40d: an identity Csys, FUN_00543f00 fills it from
                        // the port's HPDockCam hardpoint.
                        float Csys[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                        if (GetDockCameraCsys(Port, PortIndex, Csys))
                        {
                            // 0x0054b419..0x0054b447: the body of FUN_0054bb90 inlined -- the
                            // docking camera's Csys (DAT_00679638) and view-dirty byte
                            // (DAT_00679668), then with DAT_00678ba0 FUN_00549e70("docking", 0).
                            SetDockingCameraTransform(Csys);
                        }
                    }
                    // 0x0054b44c..0x0054b481: the rear view (+0x5ba == 1) and the turret view
                    // (+0x5b9 == 1) of HUD_Status (DAT_00674aa0, no test) are cancelled
                    // through FUN_005763b0(0x39, 0) / (0xbf, 0).
                    if (GetHudStatus()->bRearView == 1)
                        HudPostCommand(0x39u, 0);
                    if (GetHudStatus()->bTurretView == 1)
                        HudPostCommand(0xbfu, 0);
                    // 0x0054b484..0x0054b4a3: no letterbox yet (FUN_0041a3e0) -> bars to 1.0 at
                    // 3.0 per second (FUN_0041a0f0) and the software cursor off
                    // (FUN_0041dda0(0)).
                    if (!IsLetterboxVisible())
                    {
                        SetLetterboxTarget(1.0f, 3.0f);
                        SetClientMouseCursorDrawFlag(0);
                    }
                    // 0x0054b4a6..0x0054b4aa: CEqObj::set_control_exclusion(4).
                    EqObj->set_control_exclusion(4u);
                }
            }
        }

        // 0x0054b4b6..0x0054b4f6: while docking (+0xbaa) with a player object, the ship
        // Gameobj's +0x158 state (FUN_005437d0) steers +0xbab: raised once the state leaves 0,
        // and when it is back at 0 with +0xbab raised both bytes drop.
        void* const PlayerObjRW = GetPlayerObjRW();
        if (State->bDockingPending != 0 && PlayerObjRW != nullptr)
        {
            FClientShipGameobj const* const Ship = static_cast<FClientShipGameobj const*>(
                static_cast<IObjRW const*>(PlayerObjRW));
            int const DockState = Ship->Field_0x158;
            if (State->bDockingActive != 0)
            {
                if (DockState == 0)
                {
                    State->bDockingActive = 0;
                    State->bDockingPending = 0;
                }
            }
            else if (DockState != 0)
            {
                State->bDockingActive = 1;
            }
        }
    }
}

// DAT_00678b98. Its static initializer LAB_00548f00 runs FUN_00549300 and registers
// LAB_00548f20 (-> FUN_00548f30) with atexit -- a C++ global with a ctor and a dtor.
SpaceApp G_SpaceApp;

SpaceApp::SpaceApp()
{
    // FUN_00549300. 0x0054931f: the Event::Handler face's base vptr 0x005cbed8 -- the Events
    // member, constructed before this body.

    // 0x00549328..0x005493d7: the eight cameras, in this order.
    FirstPersonCameraConstruct(&FirstPerson); // FUN_00517180 on +0x00c
    ThirdPersonCameraConstruct(&ThirdPerson); // FUN_00518600 on +0x1b0
    TurretCameraConstruct(&Turret);           // FUN_0051a210 on +0x3c8
    RearViewCameraConstruct(&RearView);       // FUN_0051ad30 on +0x500
    DeathCameraConstruct(&Death);             // FUN_0051b2f0, 4 floats, vt 0x005e061c (+0x60c)
    ScriptCameraConstruct(&Script);           // FUN_0051e100 on +0x760
    StaticCameraConstruct(&Static);           // FUN_0051b2f0, vt 0x005e0594 (+0x9a4)
    DockingCameraConstruct(&Docking);         // FUN_0051ea20 on +0xa9c

    // 0x005493dc / 0x005493e2: SpaceApp's own two vptrs 0x005e0554 / 0x005e0548 -- the C++
    // vptrs of IEnvironment and of the Events face.

    // 0x005493e9..0x005493f5.
    bCameraReady = 0;
    ActiveCamera = nullptr;
    bPlayerLostHandled = 0;
    bLeaveRequested = 0;

    // 0x005493f8..0x0054940b: DAT_00679744 unhooked when it holds something.
    ClearPlayerShipWatcher();

    // 0x00549415..0x00549431.
    bDockingPending = 0;
    bDockingActive = 0;
    bSpaceReady = 0;
    bScriptCameraLatch = 0;
    bInitInProgress = 0;
    PlayerPreference = 1;
}

SpaceApp::~SpaceApp()
{
    // Member destruction runs Docking, Static, Script, Death, RearView, Turret, ThirdPerson,
    // FirstPerson, then Events, matching FUN_00548f30. Each HUD camera destroys its
    // EventHandler base before FGameCamera's watcher.
}

IEnvironment* GetSpaceState()
{
    // FUN_0054bd30: MOV EAX,0x00678b98 / RET.
    return &G_SpaceApp;
}

// Slots 0, 2, 3 and 4 (LAB_0054a0e0 RET 0x10, LAB_0054a210 RET 0xc, LAB_0054a2e0 RET 0x10,
// LAB_0054a450 RET 0xc): window-message handlers that build an input event and offer it to the
// active camera (+0xba0) slots 0x70 / 0x74 and the watched gameobj's slot 0x1cc. Nothing in the
// exe calls slots 0..6 of any state (every holder of the state pointer read -- tracker
// LAB_0054a0e0 has the survey), so their bodies are not ported (tracker: skipped, dead in
// retail); these overrides only fill the slots and answer false.
bool SpaceApp::Slot00(unsigned A, unsigned Message, unsigned B, unsigned C)
{
    (void)A;
    (void)Message;
    (void)B;
    (void)C;
    return false;
}

// Slot 1 (0x004b50e0): XOR AL,AL / RET 0xc.
bool SpaceApp::Slot01(unsigned A, unsigned B, unsigned C)
{
    (void)A;
    (void)B;
    (void)C;
    return false;
}

bool SpaceApp::Slot02(unsigned A, unsigned B, unsigned long Packed)
{
    (void)A;
    (void)B;
    (void)Packed;
    return false;
}

bool SpaceApp::Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed)
{
    (void)A;
    (void)Message;
    (void)B;
    (void)Packed;
    return false;
}

bool SpaceApp::Slot04(unsigned A, unsigned B, unsigned C)
{
    (void)A;
    (void)B;
    (void)C;
    return false;
}

// Slot 5 (0x00401e90): a bare RET.
void SpaceApp::Slot05()
{
}

// Slot 6 (0x0053e4a0): a bare RET 4.
void SpaceApp::Slot06(unsigned A)
{
    (void)A;
}

// Slot 7 (0x004b5010): XOR EAX,EAX / RET -- environment type 0.
int SpaceApp::GetEnvironmentType()
{
    return 0;
}

// Slot 8 (0x00401e90): a bare RET (InitializeEngine 0x005b1e9c..0x005b1ea5 calls it).
void SpaceApp::PreInit()
{
}

// Slot 9: FUN_0054ab20.
void SpaceApp::Shutdown()
{
    ShutdownSpaceState(this);
}

// Slot 10 (0x00401e90): a bare RET (FUN_005b2a30 case 5 calls it).
void SpaceApp::Slot10()
{
}

// LAB_0054a540 -- slot 11 (+0x2c), thiscall, RET 8. Its caller is the render surface reset
// 0x00424c23 (Width, Height). With an active camera (+0xba0): a local rectangle {0, 0,
// Width - 1, Height - 1} handed to the camera's slot 0x5c (the viewport apply). No active
// camera: nothing. `this` is read only for +0xba0.
void SpaceApp::Resize(int Width, int Height)
{
    FSpaceCamera* const Active = ActiveCamera;
    if (Active == nullptr)
        return;
    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;  // 0x0054a55f DEC EAX -> [ESP + 0x8]
    ViewRect[3] = Height - 1; // 0x0054a560 DEC EDX -> [ESP + 0xc]
    ((FGameCamera*)Active)->ApplyViewport(ViewRect);
}

// Slot 12: FUN_0054ab40.
bool SpaceApp::Init(int Param)
{
    return InitSpaceState(this, Param);
}

// Slot 13: FUN_0054aec0.
void SpaceApp::Destroy()
{
    DestroySpaceState(this);
}

// Slot 14: LAB_0054b000.
int SpaceApp::Update(float DeltaSeconds)
{
    return UpdateSpaceState(this, DeltaSeconds);
}

namespace
{
    // The two-way tail LAB_0054a6b3 / LAB_0054a77b of the face's handler: back to "turret" when
    // the turret view is still up (HUD_Status +0x5b9), else to "player" -- each only when the
    // controller byte +0x08 is up -- then HUD_Status FUN_004dca20.
    void SpaceAppRestoreViewAfterRear(SpaceApp* App)
    {
        HUD_Status* const Status = GetHudStatus();
        if (Status->bTurretView != 0)
        {
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("turret", nullptr);
        }
        else if (App->bCameraReady != 0)
        {
            SwitchSpaceCameraMode("player", nullptr);
        }
        GetHudStatus()->UpdateSpecialModeCaptions();
    }
}

bool FSpaceAppEventFace::OnEvent(unsigned MsgId)
{
    // LAB_0054a580 -- thiscall on the face (+4), RET 4. Every controller field is read through
    // the face: [face + 0xba0] is SpaceApp + 0xba4, [face + 0xb9c] + 0xba0, [face + 0xba6] + 0xbaa,
    // face + 0x4fc the rear-view camera at + 0x500. The switch (0x0054a587..0x0054a59f) takes
    // MsgId - 0xf through the byte table 0x0054a888: 0x0f, 0x31, 0x38, 0x39, 0xbf; every other id
    // answers false (0x0054a867).
    SpaceApp* const App = reinterpret_cast<SpaceApp*>(reinterpret_cast<unsigned char*>(this) -
                                                      offsetof(SpaceApp, Events));
    HUD_Status* Status = nullptr;

    switch (MsgId)
    {
    case 0x31:
        // 0x0054a5a6..0x0054a635: the first/third-person toggle, refused while the mouse pick
        // is blocked (FUN_0045a740).
        if (HudIsMousePickBlocked())
            return false;
        if (App->PlayerPreference == 0)
        {
            App->PlayerPreference = 1;
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("third_person", nullptr);
            SetGameSessionFlags(8, false); // FUN_004c8120(8, 0)
            return true;
        }
        if (App->PlayerPreference == 1)
        {
            App->PlayerPreference = 0;
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("first_person", nullptr);
            SetGameSessionFlags(8, true); // FUN_004c8120(8, 1)
            return true;
        }
        // 0x0054a5f9 -> 0x0054a6d3: any other preference -- consumed, nothing done.
        return true;

    case 0xbf:
        // 0x0054a638..0x0054a6d9: the turret view toggle (HUD_Status +0x5b9), refused while the
        // mouse pick is blocked.
        if (HudIsMousePickBlocked())
            return false;
        Status = GetHudStatus();
        Status->bTurretView = Status->bTurretView == 0 ? 1 : 0;
        if (GetHudStatus()->bTurretView != 0)
        {
            // 0x0054a66a..0x0054a68a: the player's behaviour manager (DAT_00679744 minus eight,
            // or null) drops its steering direction.
            IBehaviorManager* const Manager =
                GetBehaviorManager(static_cast<IObjRW*>(GetPlayerObjRW()));
            if (Manager != nullptr)
                Manager->reset_current_behavior_direction();
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("turret", nullptr);
        }
        else
        {
            // 0x0054a6a2..0x0054a6c3: FUN_005763b0(0x36, 0), then "player".
            HudPostCommand(0x36u, 0);
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("player", nullptr);
        }
        GetHudStatus()->UpdateSpecialModeCaptions(); // 0x0054a6ce FUN_004dca20
        return true;

    case 0x38:
        // 0x0054a6dc..0x0054a74a: the rear view follows the key (the command record's key-down
        // byte, FUN_00576240 +0x18). With the mouse pick blocked only a rear view already up
        // gets through (to be released).
        if (HudIsMousePickBlocked() && GetHudStatus()->bRearView == 0)
            return false;
        GetHudStatus()->bRearView = HudCommandRecord()->Byte18 != 0 ? 1 : 0;
        if (GetHudStatus()->bRearView != 0)
        {
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("rearview", nullptr);
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        // 0x0054a732..0x0054a740: released -- only when the rear-view camera is the active one
        // does the view go back.
        if (App->ActiveCamera != SpaceCameraOf(&App->RearView))
        {
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        SpaceAppRestoreViewAfterRear(App);
        return true;

    case 0x39:
        // 0x0054a74c..0x0054a795: rear view off (+0x5ba = 0); the same tail as 0x38's release.
        GetHudStatus()->bRearView = 0;
        if (App->ActiveCamera != SpaceCameraOf(&App->RearView))
        {
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        SpaceAppRestoreViewAfterRear(App);
        return true;

    case 0x0f:
    {
        // 0x0054a79a..0x0054a864: single player only, while docking (+0xbaa): the player ship's
        // docking port (IBehaviorManager::get_docking_port with a null port and index -1, the
        // answer unread), that port's dock target (IObjInspect slot 36), and when it names one
        // IServer (DAT_0067ecd0, no test) slot 91 Dock(target, DAT_00673344).
        if (!SinglePlayer())
            return false;
        if (App->bDockingPending == 0)
            return false;
        CEqObj* const EqObj = PlayerShipEqObj();
        if (EqObj == nullptr)
            return false;
        IBehaviorManager* const Manager = EqObj->get_behavior_interface();
        if (Manager == nullptr)
            return false;
        IObjRW const* Port = nullptr;
        int PortIndex = -1;
        Manager->get_docking_port(Port, PortIndex);
        if (Port == nullptr)
            return false;
        unsigned DockTarget = 0;
        Port->get_dock_target(DockTarget);
        if (DockTarget == 0)
            return false;
        const unsigned Connection = GetPlayerConnectionId();
        static_cast<IServer*>(GetLocalServerInterface())->Dock(DockTarget, Connection);
        return true;
    }

    default:
        return false;
    }
}

void SpaceAppLoadCamerasIni(SpaceApp* App)
{
    // FUN_00549830 (thiscall, no arguments). 0x00549855..0x00549882: GetFullFilename
    // ("cameras.ini"), a stack INI_Reader, open(path, false).
    INI_Reader Reader;
    if (Reader.open(GetFullFilename("cameras.ini"), false))
    {
        // 0x00549890..0x005499ef: every section.
        while (Reader.read_header())
        {
            // 0x005498a2..0x00549932: the camera a section names. [WinCamera] is the Win3D
            // camera DAT_0067dbf8; the rest are the controller's own.
            FSpaceCamera* Camera = nullptr;
            if (Reader.is_header("WinCamera"))
                Camera = Win3DGetCamera();
            else if (Reader.is_header("CockpitCamera"))
                Camera = SpaceCameraOf(&App->FirstPerson);
            else if (Reader.is_header("ThirdPersonCamera"))
                Camera = SpaceCameraOf(&App->ThirdPerson);
            else if (Reader.is_header("TurretCamera"))
                Camera = SpaceCameraOf(&App->Turret);
            else if (Reader.is_header("RearViewCamera"))
                Camera = SpaceCameraOf(&App->RearView);
            else if (Reader.is_header("DeathCamera"))
                Camera = SpaceCameraOf(&App->Death);
            else
            {
                // 0x005499b8..0x005499e0: space.cpp line 0x17e.
                FDUMP(0x100002, "%s(%d) : *** WARNING: Unknown camera type? (%s)",
                      "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x17e,
                      Reader.get_header_ptr());
                continue;
            }

            // 0x00549938..0x005499ae: every entry to the camera's slot 0x4c (SetProperty); a
            // refused one warns with space.cpp line 0x18a. (The [WinCamera] path jumps straight
            // here, past the null test the others make at 0x00549930 -- they are never null.)
            while (Reader.read_value())
            {
                const char* const Name = Reader.get_name_ptr();
                const char* const Value = Reader.get_value_string();
                // 0x00549968..0x00549973: slot 0x4c __thiscall (ECX = camera, key and value
                // pushed), AL tested -- ICamera::SetProperty.
                if (static_cast<unsigned char>(AsCamera(Camera)->SetProperty(Name, Value)) == 0)
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: [%s] unknown property '%s = %s'",
                          "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x18a,
                          Reader.get_header_ptr(), Name, Value);
                }
            }
        }
    }
    // 0x005499f5..0x00549bb5: the INI_Reader's inlined destructor -- ~INI_Reader here.
}

void SpaceAppArmDeathCamera()
{
    // FUN_0054b900 (cdecl, no arguments). The death camera is SpaceApp + 0x60c = 0x006791a4:
    // its +0x04 World is DAT_006791a8, +0x34 DAT_006791d8, +0x110 DAT_006792b4, +0x134
    // DAT_006792d8, +0x140..+0x150 DAT_006792e4..DAT_006792f4.
    FDeathCamera* const Death = &G_SpaceApp.Death;

    // 0x0054b90a..0x0054b923: the client ObjDB's spatial bucket (slot 0xc, mask 0xfdff).
    ISpatialPartition* const Bucket =
        static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(0xfdff));
    bool bCrowded = false;
    if (Bucket != nullptr)
    {
        // 0x0054b929..0x0054b97d: everything within 50 (DAT_00614030) of the active camera's
        // position (slot 0x0c; +0xba0 is not tested), capacity 0x800.
        Geometry::Sphere Query;
        const float* const CameraPos = AsCamera(G_SpaceApp.ActiveCamera)->GetPosition();
        Query.Center.x = CameraPos[0];
        Query.Center.y = CameraPos[1];
        Query.Center.z = CameraPos[2];
        Query.Radius = 50.0f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Bucket, &Query, &Result) != 0)
        {
            // 0x0054b97f..0x0054b9bf: something close -- the rear view (0x39) and the turret
            // view (0xbf) closed when up, and the drift limited to 100.
            if (GetHudStatus()->bRearView == 1)
                HudPostCommand(0x39u, 0);
            if (GetHudStatus()->bTurretView == 1)
                HudPostCommand(0xbfu, 0);
            Death->MaxDriftSpeed = 100.0f; // 0x0054b9b5
            bCrowded = true;
        }
        else
        {
            // 0x0054b9c1..0x0054b9d8: nothing close -- only the rear view is closed.
            if (GetHudStatus()->bRearView == 1)
                HudPostCommand(0x39u, 0);
        }
    }
    if (!bCrowded)
        Death->MaxDriftSpeed = 200.0f; // 0x0054b9db

    // 0x0054b9e5..0x0054ba16: +0x14c = 0.5; a first-person view goes to "third_person" on the
    // player's object first.
    Death->DriftDamping = 0.5f;
    if (G_SpaceApp.ActiveCamera == SpaceCameraOf((FHudCamera*)&G_SpaceApp.FirstPerson))
        SwitchSpaceCameraMode("third_person", GetPlayerObjRW());

    // 0x0054ba1b..0x0054ba78: the listener camera's (FUN_0040f130) orientation + position
    // (slot 0x10, twelve floats) into the death camera's World, its 3x3 into the shake matrix,
    // its position into the drift accumulator; view dirty, timer = duration, drift speed 1.0.
    FSpaceCamera* const Listener = static_cast<FSpaceCamera*>(GetClientSoundListenerObject());
    const float* const Src = AsCamera(Listener)->GetOrientation();
    FSpaceCamera* const DeathBase = SpaceCameraOf((FGameCamera*)Death);
    DeathBase->bViewDirty = 1;
    memcpy(DeathBase->World, Src, 9 * sizeof(float));
    memcpy(DeathBase->WorldPos, Src + 9, 3 * sizeof(float));
    memcpy(Death->ShakeOrientation, Src, 9 * sizeof(float));
    Death->DriftPosition[0] = Src[9];
    Death->DriftPosition[1] = Src[10];
    Death->DriftPosition[2] = Src[11];
    Death->ShakeTimer = Death->ShakeDuration;
    Death->DriftSpeed = 1.0f;

    // 0x0054ba6b..0x0054babc: the player's object narrowed with mask 0x103 (a CEqObj); control
    // exclusion 3 set unless it already is.
    void* const PlayerRW = GetPlayerObjRW();
    if (PlayerRW != nullptr && PlayerRW != reinterpret_cast<void*>(-0xc))
    {
        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(PlayerRW) + 0x10);
        if (EqObj != nullptr &&
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x103u) == 0x103u &&
            !EqObj->is_control_excluded(3u))
        {
            EqObj->set_control_exclusion(3u);
        }
    }

    // 0x0054bac2..0x0054bade: "death" on the player's object (re-read).
    SwitchSpaceCameraMode("death", GetPlayerObjRW());
}

void SetStaticCameraTransform(const float* Csys12)
{
    // FUN_0054bb50 (cdecl). 0x0054bb56..0x0054bb67: twelve dwords into the static camera's
    // Csys (DAT_00679540 = SpaceApp + 0x9a4 + 0x04) and its view-dirty byte (DAT_00679570 =
    // + 0x34) -- before the guard.
    FSpaceCamera* const Static = SpaceCameraOf((FGameCamera*)&G_SpaceApp.Static);
    Static->bViewDirty = 1;
    memcpy(Static->World, Csys12, 9 * sizeof(float));
    memcpy(Static->WorldPos, Csys12 + 9, 3 * sizeof(float));

    // 0x0054bb69..0x0054bb80: DAT_00678ba0, then FUN_00549e70("static", 0).
    if (G_SpaceApp.bCameraReady != 0)
        SwitchSpaceCameraMode("static", nullptr);
}

void SetPendingLandBase(unsigned const* BaseId)
{
    // FUN_00548ef0 (cdecl): DAT_00679784 = *BaseId, no null test.
    G_PendingLandBase = *BaseId;
}

void EnterBaseFromDock(unsigned const* BaseId)
{
    // FUN_0054bc60 (cdecl): FUN_004c4910(BaseId), FUN_004c4a10(0), DAT_00679778 = 4.
    SetPlayerBaseIdRetail(*BaseId);
    SetPlayerFactionScratchRetail(0);
    G_SpaceNextState = 4;
}

void SetPendingJumpSystem(unsigned const* SystemId, bool bFlag)
{
    // FUN_00548ed0 (cdecl): DAT_0067977c = *SystemId (no null test), DAT_00679780 = the byte
    // argument zero-extended (MOVZX).
    G_PendingJumpSystem = *SystemId;
    G_PendingJumpFlag = bFlag ? 1u : 0u;
}

namespace
{
    // FUN_0054a940 (thiscall on the space state; `this` is never read). Called once per space
    // frame at 0x0054b177.
    void ProcessPendingSystemSwitch()
    {
        if (G_PendingJumpSystem != 0)
        {
            // 0x0054a953..0x0054a97e: the player's object through the BaseWatcher DAT_00679744
            // (minus eight = the IObjRW, +0xc the CBase, +4 its object) when its class has bit 0
            // set, else null -- used below with no test.
            IObjRW* const PlayerRW = static_cast<IObjRW*>(GetPlayerObjRW());
            CObject* Object = nullptr;
            if (PlayerRW != nullptr)
            {
                CObject* const Candidate = static_cast<FClientGameobj*>(PlayerRW)->GetObject();
                if (Candidate != nullptr &&
                    (static_cast<unsigned>(Candidate->ObjectClass) & 1u) == 1u)
                {
                    Object = Candidate;
                }
            }

            RefreshLoadingProgress(); // 0x0054a97e

            // 0x0054a984..0x0054a9e2: the re-make parameters -- mass 1.0f, the default
            // dampings, system 0, type 2, flag 0, collisions on, empty name, no inertia.
            PhySys::CreateParms Parms;
            Parms.Mass = 1.0f;
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Parms.CollisionGroupId = 0;
            Parms.Type = 2;
            Parms.bFlag1c = 0;
            Parms.bEnableCollisions = 1;
            Parms.Name[0] = '\0';
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;

            // 0x0054a9e6..0x0054a9f1: the object's mass (vt+0x6c), then the inertia flag.
            Parms.Mass = Object->get_mass();
            Parms.bHasInertia = 1;

            // 0x0054a9f6..0x0054aa39: the moment of inertia when the object has a body (+0x54),
            // else zero.
            Vector Inertia;
            if (Object->BodyPtr != nullptr)
            {
                Inertia = PhySys::GetMomentOfInertia(Object);
            }
            else
            {
                Inertia.x = 0.0f;
                Inertia.y = 0.0f;
                Inertia.z = 0.0f;
            }
            Parms.Inertia = Inertia;

            // 0x0054aa30..0x0054aa6e: the collision state, the dampings and the collision group
            // name (strcpy, unbounded, as retail) off the object as it is now.
            Parms.bEnableCollisions = PhySys::GetCollisionState(Object) ? 1 : 0;
            Vector AngularDamping;
            AngularDamping.x = Parms.AngularDamping[0];
            AngularDamping.y = Parms.AngularDamping[1];
            AngularDamping.z = Parms.AngularDamping[2];
            PhySys::GetDamping(Object, Parms.LinearDamping, AngularDamping);
            Parms.AngularDamping[0] = AngularDamping.x;
            Parms.AngularDamping[1] = AngularDamping.y;
            Parms.AngularDamping[2] = AngularDamping.z;
            strcpy(Parms.Name, PhySys::GetCollisionGroup(Object));

            // 0x0054aa70..0x0054aaa0: out of the physics (vt+0x74), out of the old system with
            // the player gameobj (FUN_00554ab0), the new system recorded (FUN_004c4840) and
            // entered (FUN_005545a0).
            Object->unmake_physical();
            StarSysLeave(PlayerRW);
            SetPlayerStarSystemIdRetail(G_PendingJumpSystem);
            StarSysEnter(G_PendingJumpSystem);

            // 0x0054aaa5..0x0054aabb: back into the physics in the new system, radius 0.
            Parms.CollisionGroupId = G_PendingJumpSystem;
            Object->remake_physical(Parms, 0.0f);

            // 0x0054aabe..0x0054aad2: IServer (DAT_0067ecd0) slot 47 with the player's ship id
            // (DAT_00679774) and connection (DAT_00673344).
            static_cast<IServer*>(GetLocalServerInterface())
                ->SystemSwitchOutComplete(GetPlayerShipId(), GetPlayerConnectionId());

            // 0x0054aad9..0x0054aadf.
            G_PendingJumpFlag = 0;
            G_PendingJumpSystem = 0;
            return;
        }

        // 0x0054aaea..0x0054ab0f: a landing ship's base -- FUN_004c4910(&base),
        // FUN_004c4a10(0), next state 3 (enter the base), and the request consumed.
        if (G_PendingLandBase != 0)
        {
            SetPlayerBaseIdRetail(G_PendingLandBase);
            SetPlayerFactionScratchRetail(0);
            G_SpaceNextState = 3;
            G_PendingLandBase = 0;
        }
    }
}

namespace
{
    // The local at [EBP-0x234] of FUN_0054ab40: a pointer to a byte that is raised on
    // construction (0x0054ab64 stores &this->+0x0b, 0x0054ab68 writes 1, then the try level
    // becomes 0) and dropped on scope exit. FUN_0054aeb0 (MOV EAX,[ECX]; MOV byte [EAX],0; RET)
    // is its destructor, reached only through the unwind funclet Unwind@005c12b0 (LEA ECX,
    // [EBP-0x234]); both normal exits inline it (0x0054abf2 / 0x0054ac09 MOV [EBP],BL and
    // 0x0054ae8b MOV [EDX],BL after the result is computed).
    struct FScopedFlagByte
    {
        explicit FScopedFlagByte(unsigned char* InFlag)
            : Flag(InFlag)
        {
            *Flag = 1;
        }

        ~FScopedFlagByte()
        {
            // FUN_0054aeb0.
            *Flag = 0;
        }

        FScopedFlagByte(FScopedFlagByte const&) = delete;
        FScopedFlagByte& operator=(FScopedFlagByte const&) = delete;

        unsigned char* Flag;
    };
}

bool InitSpaceState(SpaceApp* State, int Param)
{
    // FUN_0054ab40 -- slot 12 (+0x30), thiscall RET 4; the argument is never read.
    (void)Param;

    // 0x0054ab60..0x0054ab68: +0x0b raised for the whole init (FUN_0054bd40 reads it) and
    // dropped by the guard's destructor on every exit, the exception path included.
    FScopedFlagByte InitInProgress(&State->bInitInProgress);

    // 0x0054ab75..0x0054ab87: the loading screen (FUN_00579290), the NN system (FUN_0045a100(1)),
    // RefreshLoadingProgress.
    LoadingScreenStart();
    NnSystemInit(true);
    RefreshLoadingProgress();

    // 0x0054ab89..0x0054ab92: +0x08 = 1 between the push of "IDR_SPACE" and FUN_00577490.
    State->bCameraReady = 1;
    RegisterAcceleratorGroup("IDR_SPACE");

    // 0x0054ab97..0x0054aba9: IServer (DAT_0067ecd0, no test) slot 64 RequestCreateShip with
    // the connection DAT_00673344.
    static_cast<IServer*>(GetLocalServerInterface())->RequestCreateShip(GetPlayerConnectionId());

    // 0x0054abaf..0x0054abca: DAT_00678b95 / 94 cleared, then FUN_005a89b0(&DAT_00678b95,
    // 5.0f, 60.0f) -- the pushes are 60.0f (0x42700000), 5.0f (0x40a00000), the address.
    ClearPlayerCreateShipResponse();
    WaitForServerResponse(GetPlayerCreateShipAnsweredByte(), 5.0f, 60.0f);

    // 0x0054abcf..0x0054ac0e: no answer -- single player raises DAT_0067ecbd (leave the frame
    // loop), multiplayer DAT_0067ecbe (disconnected); next state 0, +0x0b dropped, return 1.
    if (*GetPlayerCreateShipAnsweredByte() == 0)
    {
        if (SinglePlayer())
            SetClientQuitRequested(true);
        else
            G_bServerDisconnected = true;
        G_SpaceNextState = 0;
        return true; // 0x0054abf2 / 0x0054ac09: +0x0b = 0 (the guard's destructor, inlined)
    }

    // 0x0054ac13..0x0054ac1f: RefreshLoadingProgress, FUN_005020a0, FUN_00510f90,
    // RefreshLoadingProgress (LoadSpaceSceneSlice is exactly those three after the first).
    RefreshLoadingProgress();
    LoadSpaceSceneSlice();

    // 0x0054ac21..0x0054ac56: DAT_00679788 is a pending background loader -- waited for,
    // deleted and cleared when set. Its XREF list is 0x0054ac21 (R), 0x0054ac3c (R) and
    // 0x0054ac50 (W, the clear): nothing in the exe ever stores one, so the other branch is the
    // only one retail takes.
    // 0x0054ac58..0x0054ac9e: lightanim.ini (FUN_00538b20), the fuse DB (FUN_0052a7a0), the gate
    // tunnels from [Data] gate_tunnels (DAT_0067a22c, FUN_004fe1c0), FUN_0042acb0(1).
    {
        char LightAnimPath[MAX_PATH];
        GetFileSysPath(LightAnimPath, "fx\\lightanim.ini", nullptr);
        LightAnimationLoad(LightAnimPath);
    }
    ClientFuseDB::LoadAll();
    {
        char GateTunnelPath[MAX_PATH];
        GetFileSysPath(GateTunnelPath, g_display.gate_tunnels.Text, nullptr);
        GateTunnelIniLoad(GateTunnelPath);
    }
    PreloadClientSoundArchesByCategory(1);

    // 0x0054aca1..0x0054acb1: RefreshLoadingProgress, FUN_005545a0(FUN_004c4830()),
    // RefreshLoadingProgress.
    RefreshLoadingProgress();
    StarSysEnter(GetPlayerStarSystemIdRetail());
    RefreshLoadingProgress();

    // 0x0054acb3: FUN_0054b520, the result dropped.
    SpawnPlayerShip();

    // 0x0054acb8..0x0054acc5: RefreshLoadingProgress, IServer slot 11 (+0x2c) Update,
    // RefreshLoadingProgress.
    RefreshLoadingProgress();
    static_cast<IServer*>(GetLocalServerInterface())->Update();
    RefreshLoadingProgress();

    // 0x0054acc7..0x0054ad62: the cockpit ini, the four cockpit camera loaders, FUN_004c73d0.
    InitPlayerCockpitSlice();

    // 0x0054ad67..0x0054ad82: +0xbaa = 0, RefreshLoadingProgress, FUN_00549bc0(this, 0, 0), the
    // cockpit shake amplitude DAT_00675294 = 0, RefreshLoadingProgress.
    State->bDockingPending = 0;
    RefreshLoadingProgress();
    InstallSpaceCamera(nullptr, false);
    ResetCameraShakeAmount();
    RefreshLoadingProgress();

    // 0x0054ad84..0x0054ad8b: FUN_00549830 (cameras.ini), RefreshLoadingProgress.
    SpaceAppLoadCamerasIni(State);
    RefreshLoadingProgress();

    // 0x0054ad8d: FUN_004120d0(1) -- fade in from a covered screen.
    FadeIn(true);

    // 0x0054ad97 / 0x0054ad9a.
    State->bPlayerLostHandled = 0;
    State->bLeaveRequested = 0;

    // 0x0054ad9d..0x0054ade8: the seven ids of a stack array registered on the Event::Handler
    // face at +4 (FUN_005760d0), in this order.
    {
        const unsigned EventIds[7] = {0xc3, 0xc4, 0xf, 0x31, 0x39, 0x38, 0xbf};
        for (int Index = 0; Index < 7; ++Index)
            State->Events.RegisterEvent(EventIds[Index]);
    }

    // 0x0054adea..0x0054adec: RefreshLoadingProgress, FUN_0045b490 (the system-arrival caption).
    RefreshLoadingProgress();
    NnArrivalTextShowSystemNow();

    // 0x0054adf1..0x0054ae06: +0xba9 = 1; FUN_004c8110(8) -- session flag 8 -> +0xba4 = 0 (first
    // person); otherwise the preference stays (the ctor's 1, or the last choice).
    State->bSpaceReady = 1;
    if (TestGameSessionFlags(8))
        State->PlayerPreference = 0;

    // 0x0054ae0c..0x0054ae1c: no camera installed yet -> FUN_00549e70(this, "player", 0).
    if (State->ActiveCamera == nullptr)
        SwitchSpaceCameraMode("player", nullptr);

    // 0x0054ae21..0x0054ae56: FUN_005a89b0(&DAT_00678b96, 5.0f, 60.0f); no launch answer ->
    // DAT_0067ecbd (single player) or DAT_0067ecbe.
    WaitForServerResponse(GetPlayerLaunchAcknowledgedByte(), 5.0f, 60.0f);
    if (*GetPlayerLaunchAcknowledgedByte() == 0)
    {
        if (SinglePlayer())
            SetClientQuitRequested(true);
        else
            G_bServerDisconnected = true;
    }

    // 0x0054ae5d..0x0054ae78: next state 0, and outside single player (the next state being 0,
    // which it now always is) FUN_0046a4a0(1) shows the chat window.
    G_SpaceNextState = 0;
    if (!SinglePlayer() && G_SpaceNextState == 0)
        ChatShowMsgWin(true);

    // 0x0054ae7b..0x0054ae8b: RefreshLoadingProgress, the answer (next state == 0), then +0x0b = 0
    // (the guard's destructor, after the SETZ).
    RefreshLoadingProgress();
    return G_SpaceNextState == 0;
}

int UpdateSpaceState(SpaceApp* State, float DeltaSeconds)
{
    // LAB_0054b000 -- slot 14 (+0x38) of the space state vftable .rdata 0x005e0554, thiscall,
    // RET 4. The whole in-space frame: simulation, camera, sound, then the render bracket
    // FUN_00424f30 .. FUN_00425080 / FUN_00425140 with the scene drawn through the active
    // camera's slot 0x78. Returns DAT_00679778, the next client state (0 = stay).

    // 0x0054b004..0x0054b015: no active camera (+0xba0) -> 6, leave space.
    if (GetActiveSpaceCamera() == nullptr)
        return 6;

    // 0x0054b019..0x0054b0ae: the simulation half, skipped while FUN_0042d740 holds the local
    // server.
    if (!ShouldSkipLocalServerTick())
    {
        // 0x0054b02b.
        G_SpaceNextState = 0;

        // 0x0054b035: CALL 0x00401e90 (Dt) -- the shared empty `RET` (ICF-folded
        // IDPMsgHandler::OnDisconnect). Nothing to do.

        // 0x0054b03b: FUN_00541380(Dt) -- the per-bucket Gameobj update. Each ship's slot 93
        // (FUN_00545050, FClientShipGameobj::update) runs its CShip's CObject::update with
        // UPDATE_DEFAULT for the local player's objects, so the player CShip is ticked there,
        // not here (W3-A 2026-09-26: the "Update slice" CShip::update(Dt, 0) scaffold removed).
        FClientTickScope ObjectsProfile(EClientTickSection::Objects);
        ClientObjDb_UpdateAll(DeltaSeconds);
        ObjectsProfile.Stop();

        // 0x0054b041.
        FClientTickScope AnimationProfile(EClientTickSection::WorldFx);
        AnimDB::Update(DeltaSeconds);
        AnimationProfile.Stop();
        // 0x0054b04b..0x0054b051: PUSH Dt, then FUN_00554e90's system id.
        FClientTickScope PhysicsProfile(EClientTickSection::Physics);
        FClientPhysicsProfileScope PhysicsDetails;
        FClientTickScope StepProfile(EClientTickSection::PhysicsStep);
        PhySys::Update(StarSysGetActiveSystemId(), DeltaSeconds);
        StepProfile.Stop();

        // 0x0054b058: FUN_00540bd0(Dt) -- CBeam::move on every bucket-3 Gameobj's beam.
        FClientTickScope BeamsProfile(EClientTickSection::MoveBeams);
        ClientObjDb_MoveBeams(DeltaSeconds);
        BeamsProfile.Stop();
        // 0x0054b05d: FUN_0052fc20() -- the same list through PhySys::GenerateCollisions, then
        // the PhySys collision list resolved into Gameobj hit pairs (FUN_0052fe60, slot 0x194).
        ClientObjDb_ProcessCollisions();
        PhysicsDetails.Stop();
        PhysicsProfile.Stop();

        // 0x0054b062..0x0054b06a: FUN_005135b0(Dt, camera); the callee reads only Dt.
        FClientTickScope WorldProfile(EClientTickSection::WorldFx);
        TradeLaneDetachTick(DeltaSeconds);

        // 0x0054b06f..0x0054b086: LIGHTING (option 0x18) -> FUN_00538390(Dt), FUN_00537c30.
        if (GetGameOption(kGameOptLighting) != 0)
        {
            GameLightTickAll(DeltaSeconds);
            LightGridRebuild();
        }

        // 0x0054b08b..0x0054b08d: FUN_0054b230.
        UpdateSpaceDocking(State);

        // 0x0054b092..0x0054b09f: the active camera's slot 0x6c (Dt), re-read and tested.
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr)
            ((FGameCamera*)Cam)->Update(DeltaSeconds);

        // 0x0054b0a3: FUN_00554eb0(Dt).
        StarSysUpdate(DeltaSeconds);
        // 0x0054b0a9: FUN_0041b2e0(Dt).
        TickMaterialAnimationPlayback(DeltaSeconds);
        WorldProfile.Stop();
    }

    // 0x0054b0b1..0x0054b0ca: the sound environment from the camera's slot 0x7c into
    // FUN_00428030, then the mixer FUN_00428040(camera, Dt). No null test: +0xba0 was checked
    // on entry.
    {
        FClientTickScope SoundProfile(EClientTickSection::Sound);
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        SetClientAudioMode(
            ((FGameCamera*)Cam)->GetSoundMode());
        TickClientSoundMixer(GetActiveSpaceCamera(), DeltaSeconds);
    }

    // 0x0054b0d2..0x0054b0d9: FUN_00424f30, kept in a stack byte for the present below.
    const bool bSceneBegun = BeginClientRenderFrame();
    if (bSceneBegun)
    {
        // 0x0054b0df..0x0054b0ee.
        Win3DImmedBatchReset();       // FUN_005a29b0
        ButtonMontageEnsure();        // FUN_00559c10
        UpdateMouseFlightDirection(); // FUN_004c7cb0
        // 0x0054b0ee: FUN_004ec400 -- with DAT_00674b70 (the Targetable HUD) and the letterbox
        // up (FUN_0041a3e0), clear the low two bits of +0x6c on its four alert children.
        HudTargetableObjectsHideAlertsInLetterbox();

        // 0x0054b0f3..0x0054b111: the caller-side test of the active camera, then FUN_0051b4b0
        // (letterbox, clear, viewport, Win3D camera) and the camera's slot 0x78 (0xffff, 0).
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr)
        {
            GameCameraBeginFrame(reinterpret_cast<FGameCamera*>(Cam));
            FSpaceCamera* const DrawCam = GetActiveSpaceCamera();
            ((FGameCamera*)DrawCam)->DrawScene(0xffffu, 0u);
        }

        // 0x0054b114..0x0054b123.
        Win3DImmedBatchFlushAll();    // FUN_005a2a20
        ButtonMontageFlushBatch();    // FUN_00559c20
        UiOverlayPrepPipeline();      // FUN_00417f10
        Win3DDrawAll();               // FUN_0059e300 (NN_Pause is one of its roots)

        // 0x0054b128..0x0054b158: with Dt > 0 (FCOMP 0.0f / TEST AH,0x41 / JNZ) and the server
        // tick not paused (FUN_0042d730), the fade: FUN_00412360(Dt), FUN_00412430(camera).
        if (DeltaSeconds > 0.0f && !IsClientServerTickPaused())
        {
            FSpaceCamera* const FadeCam = GetActiveSpaceCamera();
            UpdateFade(DeltaSeconds);
            DrawFade(AsCamera(FadeCam));
        }

        // 0x0054b159: FUN_00425080.
        EndClientRenderScene();
    }

    // 0x0054b15e: FUN_00541510() -- FUN_005424e0 on each Gameobj bucket (DAT_00678aa8 ..
    // DAT_00678a48): every Gameobj whose slot 0x158 reports it done is unregistered and deleted.
    ClientObjDb_Reap();
    // 0x0054b163..0x0054b16d: unless FUN_0042d740, FUN_00541580(Dt) -- multiplayer only
    // (returns at once under SinglePlayer), walks DAT_00678a34.
    if (!ShouldSkipLocalServerTick())
        ClientObjDb_UpdateRemotePhysics(DeltaSeconds);
    // 0x0054b175..0x0054b177: FUN_0054a940(this) -- a pending system switch (DAT_0067977c) or
    // landing (DAT_00679784).
    ProcessPendingSystemSwitch();

    // 0x0054b17c..0x0054b1b5: the player's last position (slot 0) and orientation (slot 0xc)
    // into DAT_006732e4 / DAT_006732c0 (FUN_004c4a30 / FUN_004c4a60).
    {
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player != nullptr)
        {
            SetPlayerLastPositionRetail(&Player->get_position().x);
            IObjRW* const Again = static_cast<IObjRW*>(GetPlayerObjRW());
            SetPlayerLastOrientationRetail(&Again->get_transform().Rotation.E[0][0]);
        }
    }

    // 0x0054b1b8..0x0054b1fa: when the active camera reports it is finished (slot 0x60), either
    // the player ship is gone -- FUN_00471e80 once, latched in +0x09 -- or the camera returns to
    // "player", attached to the player object, and +0xba8 drops.
    {
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr &&
            ((FGameCamera*)Cam)->IsFinished())
        {
            void* const Player = GetPlayerObjRW();
            if (Player == nullptr)
            {
                if (State->bPlayerLostHandled == 0)
                {
                    NnExitPostShipLostMessage(); // 0x0054b1e2: FUN_00471e80
                    State->bPlayerLostHandled = 1;
                }
            }
            else
            {
                SwitchSpaceCameraMode("player", Player);
                State->bScriptCameraLatch = 0;
            }
        }
    }

    // 0x0054b201..0x0054b208.
    if (State->bLeaveRequested != 0)
        G_SpaceNextState = 6;

    // 0x0054b212..0x0054b21a: FUN_00425140, only when FUN_00424f30 began the scene.
    if (bSceneBegun)
        PresentClientRenderFrame();

    // 0x0054b21f: FUN_00579190 -- the loading screen's (DAT_0067c3b8) timer reset:
    // Timing::read_ticks into DAT_0067c3c8/cc, DAT_0067c3e0 = DAT_0067c3d9 = 0,
    // DAT_0067c3da = 1.
    LoadingScreenReset();

    // 0x0054b224.
    return G_SpaceNextState;
}

void DestroySpaceState(SpaceApp* State)
{
    // FUN_0054aec0 -- slot 13 (+0x34) of the space state vftable .rdata 0x005e0554, thiscall on
    // the in-flight controller DAT_00678b98. No null test on `this`: the client state driver
    // only calls slot 13 on a live state.

    // 0x0054aec3: FUN_00577590("IDR_SPACE") -- drop the space accelerator group the init
    // registered (0x0054ab92).
    UnregisterAcceleratorGroup("IDR_SPACE");

    // 0x0054aed2: FUN_00576190 on controller + 0x04, the Event::Handler face -- drop every
    // registration of this handler (the init's seven, 0x0054addc).
    State->Events.UnregisterAllEvents();

    // 0x0054aeda: FUN_004348b0 on DAT_00667e50 (engine-instance table: FUN_0041b030 on each
    // +0x400 slot, IEngine slot 0x3c on each instance, then every slot and the count reset).
    ResetEngineInstanceCache();

    // 0x0054aee4: FUN_00435630 -- every character script's reference count back to 0 and a
    // fresh IAnimation2 script for the gf\CharacterInfo.cpp database (character_info.cpp).
    ResetCharacterScripts();

    // 0x0054aee9: FUN_00549bc0(this, 0, 0) -- deactivate the active camera.
    InstallSpaceCamera(nullptr, false);

    // 0x0054aef4..0x0054af18: FUN_00518410 on + 0x00c, OnDisconnect (empty) on + 0x1b0 /
    // + 0x3c8 / + 0x500.
    ReleaseSpaceCameraControllerParts();

    // 0x0054af1d: FUN_004c77a0.
    HudElementListShutdown();

    // 0x0054af22..0x0054af42: the four state bytes and DAT_0067dcc8. +0xbaa is tested and only
    // then cleared (the compare is on the value before the other two stores).
    State->bSpaceReady = 0;        // +0xba9
    State->bScriptCameraLatch = 0; // +0xba8
    if (State->bDockingPending != 0)
        State->bDockingPending = 0; // +0xbaa
    G_bNnScreenUp = false;      // DAT_0067dcc8
    State->bDockingActive = 0;  // +0xbab

    // 0x0054af48..0x0054af86: the player's ship through the watcher DAT_00679744 (minus eight),
    // its CEqObj at ObjRW + 0x10 (the +0xc'd pointer's +0x4, with the -0xc sentinel), the ship
    // class mask 0x503 at CObject + 0x4c; when control exclusion 7 is set it is cleared (0).
    void* const ObjRW = GetPlayerObjRW();
    if (ObjRW != nullptr && ObjRW != reinterpret_cast<void*>(-0xc))
    {
        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj != nullptr &&
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u &&
            EqObj->is_control_excluded(7u))
        {
            EqObj->set_control_exclusion(0u);
        }
    }

    // 0x0054af8d: controller + 0x08 (DAT_00678ba0) = 0.
    State->bCameraReady = 0;

    // 0x0054af8c / 0x0054af90: FUN_00554ab0(0).
    StarSysLeave(nullptr);

    // 0x0054af95..0x0054af9c: FUN_0041dde0("Arrow", 1).
    MouseSetCursor("Arrow", true);

    // 0x0054afa1: CObject::ReleaseAll.
    CObject::ReleaseAll();

    // 0x0054afa7: FUN_0051fbc0 (Heap::Shrink on DAT_00675350 / DAT_00675340, then free() every
    // queued ZItem of the DAT_006752f0.. vectors and empty them).
    SpaceDrawQueueRelease();

    // 0x0054afac: FUN_0052a690.
    ClientFuseDB::Clear();

    // 0x0054afb1: FUN_00502aa0 (unlink and free every node of the DAT_00674fd8 list, then free
    // DAT_00674fd0).
    JumpEffectDescsClear();

    // 0x0054afb6: FUN_00511010.
    ClearSolarStarArchShapes();

    // 0x0054afbb: FUN_004fe180 (virtual-delete every entry of the DAT_00674f9c vector, then
    // end = begin).
    GateTunnelsClear();

    // 0x0054afc0: FUN_004f73c0.
    EffectTypeListKillAll();

    // 0x0054afc5: FUN_005382f0 (reset the light-grid root DAT_00678980..9c and rethread the two
    // node pools DAT_00675578 / DAT_0067697c).
    LightGridReset();
    // 0x0054afca: FUN_00538520 (drop one reference on every GameLight of DAT_00675570,
    // virtual-delete at zero).
    GameLightReleaseAll();

    // 0x0054afcf: FUN_00537ab0; 0x0054afd4: FUN_00538ab0.
    GameLightHeapShrink();
    LightAnimationFreeAll();

    // 0x0054afd9: FUN_004284f0 -- every sound stopped, the live-source leak warning.
    StopAllClientSounds();
    // 0x0054afde: FUN_0042ad30(1) (walk the DAT_00667cc4 sound list by the category mask).
    UnloadClientSoundArchesByCategory(1);

    // 0x0054afe8: AnimDB::Shutdown.
    AnimDB::Shutdown();

    // 0x0054aff1: JMP FUN_0045a140 -- the NN system teardown.
    NnSystemShutdown();
}

void ShutdownSpaceState(SpaceApp* State)
{
    // FUN_0054ab20 (slot 9, +0x24), thiscall, no arguments.
    // 0x0054ab23: FUN_00554e80 -- FUN_00554ab0(0), leave the system without a player.
    StarSysLeaveWithoutPlayer();
    // 0x0054ab28..0x0054ab2c: this->vftable[13] (+0x34) = FUN_0054aec0. Retail calls it even
    // when the atexit teardown already ran slot 13 on the current state (0x005b21ac).
    State->Destroy();
    // 0x0054ab2f: CObject::ReleaseAll.
    CObject::ReleaseAll();
    // 0x0054ab36: JMP FUN_0051fbc0 -- the ZItem heaps and draw queues released.
    SpaceDrawQueueRelease();
}
