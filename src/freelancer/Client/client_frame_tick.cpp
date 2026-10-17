// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_frame_tick.h"
#include "client_tick_profile.h"

#include "client_equip_fx.h"
#include "client_state.h"
#include "menu_ui_widgets.h"
#include "cockpit_load.h"
#include "gameflow.h"
#include "menu_sound_arch.h"
#include "menu_universe_base.h"
#include "sound_config.h"
#include "game_options.h"
#include "font_atlas.h"
#include "font_imp.h"
#include "font_string.h"
#include "globals.h"
#include "hud_element_list.h"
#include "login_mgr.h"             // G_Server (DAT_0067ecd0), DAT_0067ecbe / DAT_0067ecbf
#include "main_command_handler.h"  // DAT_0067ecbd (FUN_005b2c50 / FUN_005b2c60)
#include "menu_enter_state.h"      // FUN_00564b10 / FUN_00564b20
#include "nn_error.h"              // FUN_00473cb0
#include "player_behavior.h"
#include "server_connect.h"
#include "space_camera.h"
#include "space_scene_draw.h"
#include "space_scene_load.h"
#include "space_scene_obj_draw.h"
#include "engine_light.h"
#include "star_sys.h"
#include "tradelane_fx.h"
#include "win3d.h"
#include "win3d_immediate.h"
#include "win3d_render.h"
#include "win3d_renderables.h"

#include "DALib.h"
#include "alchemy.h"
#include "common_animdb.h"
#include "common_physys.h"
#include "common_misc.h"   // SinglePlayer
#include "common_timing.h" // Timing::UpdateGlobalTime
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"

#include <float.h>
#include <stdio.h>
#include <windows.h>

namespace
{
    // DAT_0061085c / DAT_00610860 are RenderMgr.cpp globals: G_bRenderDither (the DitherControl
    // apply LAB_00424630) and G_RenderMipFilter (the MipFilterControlCallback apply
    // LAB_004246d0), defined in engine_init.cpp.

    // DAT_0067ecb0 -- the IServer* the frame saw last (.bss 0). Only FUN_005b2890 touches it
    // (0x005b28bb read, 0x005b28ca store).
    IServer* G_FrameLastServer = nullptr;

    IRenderPipeline* FramePipeline()
    {
        // Every pipeline call in FUN_00424f30 / FUN_00425080 / FUN_00425140 re-reads the DALib
        // import (MOV EAX,[->DALib::Pipeline]; MOV EAX,[EAX]) -- no null test anywhere.
        return DALib::Pipeline;
    }

    // FUN_00424f30 (.text:00424f30-0042507f), cdecl, returns DAT_006679f8.
    bool TryBeginFrame()
    {
        // 0x00424f30..0x00424f3f: DAT_006679f8 = 0; an inactive application (DAT_00610865)
        // returns that 0 straight away.
        G_bRenderFrameBegun = false;
        if (!ClientAppIsActive())
            return false;

        // 0x00424f45..0x00424f55: FUN_005b27c0(0) behind DAT_0067ecbc.
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(false);

        // 0x00424f58..0x00424f91: when the device changed the precision (DAT_0061084c differs
        // from DAT_00610848), the frame runs with the one the device left: EAX is loaded from
        // DAT_0061084c before the 0x18 / 0x35 tests.
        if (G_FpuPrecisionFound != G_FpuPrecisionWanted)
        {
            if (G_FpuPrecisionFound == 0x18)
                _control87(0x20000, 0x30000);
            else if (G_FpuPrecisionFound == 0x35)
                _control87(0x10000, 0x30000);
            else
                _control87(0, 0);
        }

        // 0x00424f94..0x00424faf: with DAT_00610864, DAT_006679f8 = (BeginScene +0xe4 >= 0).
        if (G_bRenderPostCreate)
            G_bRenderFrameBegun = FramePipeline()->BeginScene() >= 0;

        if (!G_bRenderFrameBegun)
        {
            // 0x00424fbf..0x00424fdb: Sleep(1000), then FUN_004249c0(DAT_006679f4, options,
            // options).
            Sleep(1000);
            if (RenderMgrCreateBuffers(G_RenderMgrWindow, &g_display, &g_display))
            {
                // 0x00424fe7..0x00425020: Clear(3, 0) +0x3c, SwapBuffers +0x40; only a
                // non-negative swap is followed by BeginScene, whose result is stored.
                FramePipeline()->ClearBuffers(3, nullptr);
                if (FramePipeline()->SwapBuffers() >= 0)
                    G_bRenderFrameBegun = FramePipeline()->BeginScene() >= 0;
            }
            // 0x00425027..0x0042502e.
            if (!G_bRenderFrameBegun)
                return false;
        }

        // 0x00425030..0x00425059: SetRenderState(0x1a D3DRS_DITHERENABLE) +0xf0, 1 iff the
        // options object's color_bpp (DAT_00679bd0) is 16 AND DAT_0061085c != 0.
        const int DitherEnable = (g_display.color_bpp == 0x10 && G_bRenderDither != 0) ? 1 : 0;
        FramePipeline()->SetRenderState(D3DRS_DITHERENABLE, DitherEnable);
        // 0x0042505f..0x00425074: SetTextureStageState(0, 0x12, DAT_00610860) +0xf8.
        FramePipeline()->SetTextureStageState(0, 0x12, G_RenderMipFilter);

        // 0x0042507a.
        return G_bRenderFrameBegun;
    }

    // FUN_00425080 (.text:00425080-00425138), void -- flush the material batcher, EndScene, put
    // the render states the next frame starts from back, re-arm the FP trap, restore the FPU
    // precision. Unconditional: its callers only reach it when FUN_00424f30 returned true.
    void EndSceneHalf()
    {
        // 0x00425080..0x0042508c: MaterialBatcher slot 0x28 (1), no null test.
        DALib::MaterialBatcher->FlushAll(1);

        // Intentional port-only addition: CLIENT-BETA-LABEL, removal only by Giperion.
        DrawClientTickProfile();

        // 0x0042508f..0x0042509a: Pipeline slot 0xe8 (EndScene); EAX is not read.
        FramePipeline()->EndScene();

        // 0x004250a0..0x004250d1: the state the next frame starts from, through the OpenGL.cpp
        // layer -- glDisable(0xb60), depth write on, glDisable(GL_BLEND), glEnable(GL_DEPTH_TEST),
        // depth func 4 (LESSEQUAL), glEnable(GL_CULL_FACE).
        Win3DDisable(kWin3DFog);            // FUN_00421ed0(0xb60)
        Win3DSetDepthWrite(1);              // FUN_00422190(1)
        Win3DDisable(kWin3DBlend);          // FUN_00421ed0(0xbe2)
        Win3DEnable(kWin3DDepthTest);       // FUN_00421dd0(0xb71)
        Win3DSetDepthFunc(4);               // FUN_00422120(4)
        Win3DEnable(kWin3DCullFace);        // FUN_00421dd0(0xb44)

        // 0x004250d6..0x004250e4: FUN_005b27c0(1) behind DAT_0067ecbc.
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(true);

        // 0x004250ec..0x00425138: when the precision FUN_004249c0 found (DAT_0061084c) differs
        // from the one the renderer wants (DAT_00610848), put the wanted one back -- EAX is
        // loaded from DAT_00610848 here: 24-bit, 53-bit, or the (0, 0) query for anything else.
        if (G_FpuPrecisionFound != G_FpuPrecisionWanted)
        {
            if (G_FpuPrecisionWanted == 0x18)
                _control87(0x20000, 0x30000);
            else if (G_FpuPrecisionWanted == 0x35)
                _control87(0x10000, 0x30000);
            else
                _control87(0, 0);
        }
    }

    // FUN_00425140 (.text:00425140-0042515d), void.
    void PresentHalf()
    {
        FClientTickScope PresentProfile(EClientTickSection::Present);
        // 0x00425140: FUN_004214a0.
        Win3DOnPresentFrame();
        // 0x00425145..0x00425158: SwapBuffers +0x40, then NEG EAX / SBB AL,AL / INC AL --
        // DAT_00610864 = (result == 0), not (result >= 0).
        G_bRenderPostCreate = FramePipeline()->SwapBuffers() == 0;
    }
}

// FUN_005b2890, cdecl(double dt) -- WinMain passes FUN_0042d680 read before the message pump
// (0x005b3347). Listing-traced 0x005b2890..0x005b2a1f. Every state (space LAB_0054b000, GF
// FUN_00558c10, IntroFocus FUN_00573eb0, MissionBriefing LAB_0043d360) begins, draws and
// presents its own frame inside its slot 14; the frame function only runs the timers, the
// disconnect check, the Win3D roots, the local server and the state machine.
void TickClientFrame(double DeltaSeconds)
{
    // Port-only QPC diagnostics requested by Giperion; CLIENT-BETA-LABEL.
    FClientTickProfileFrame ProfileFrame;
    // 0x005b289a..0x005b28b3: Timing::UpdateGlobalTime(dt) unless FUN_0042d740.
    if (!ShouldSkipLocalServerTick())
        Timing::UpdateGlobalTime(DeltaSeconds);

    // 0x005b28b6..0x005b28cf: BL = the server pointer changed since the last frame (DAT_0067ecb0
    // against DAT_0067ecd0); the new one is remembered.
    bool bServerChanged = false;
    if (G_FrameLastServer != G_Server)
    {
        bServerChanged = true;
        G_FrameLastServer = G_Server;
    }

    // 0x005b28cf..0x005b292b: no quit pending (DAT_0067ecbd), multiplayer, and the server dropped
    // us (DAT_0067ecbe): the "connection lost" box FUN_00473cb0(0x737, 0, 0) when the connect
    // response had arrived (DAT_0067ecbf); both flags cleared; FUN_005a8260; unless the server
    // select page is up (FUN_00564b20), the intro mode request 3 (FUN_00564b10) and the quit flag.
    if (!ClientQuitRequested() && !SinglePlayer() && G_bServerDisconnected)
    {
        if (G_bServerConnected)
            NnErrorRunModal(0x737, false, nullptr);
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        ShutdownServerConnection();
        if (!MenuNavIsServerSelectActive())
        {
            MenuNavSetIntroModeRequest(3);
            SetClientQuitRequested(true);
        }
    }

    // 0x005b2936..0x005b2938: FUN_0059e080(dt) -- the only per-frame Update of the registered
    // Win3D roots (modal dialogs included); the NN modal loops call it themselves (FUN_004739xx).
    FClientTickScope UiProfile(EClientTickSection::Ui);
    Win3DUpdateAll(DeltaSeconds);
    UiProfile.Stop();

    // 0x005b293d..0x005b296d: DAT_0067ecd0 non-null and FUN_0042d740 false -> IServer slot 11
    // Update, slot 12 ElapseTime((float)dt) -- both inside TickLocalServer.
    FClientTickScope ServerProfile(EClientTickSection::Server);
    TickLocalServer(static_cast<float>(DeltaSeconds));
    ServerProfile.Stop();

    // EBP: the state to enter (0 = stay); ECX: DAT_0067ecac as last read.
    int NextState = 0;
    IEnvironment* CurrentState = g_client.current_state;
    bool bForceIntro = false;

    // 0x005b2970..0x005b29a1: the server went away this frame (changed and now null) while the
    // current state is not the IntroFocus (FUN_00575430): in multiplayer the intro mode request
    // 3, then back to the intro.
    if (bServerChanged && G_Server == nullptr && CurrentState != GetIntroFocusState())
    {
        if (!SinglePlayer())
            MenuNavSetIntroModeRequest(3);
        bForceIntro = true;
    }
    else
    {
        // 0x005b29f4..0x005b2a14: a live state runs its slot 14 (+0x38) with (float)dt unless a
        // quit is pending; DAT_0067ecac is read again after it.
        if (CurrentState != nullptr)
        {
            if (ClientQuitRequested())
            {
                bForceIntro = true;
            }
            else
            {
                FClientTickScope GameProfile(EClientTickSection::Game);
                NextState = CurrentState->Update(static_cast<float>(DeltaSeconds));
                GameProfile.Stop();
                CurrentState = g_client.current_state;
            }
        }
        // 0x005b2a16..0x005b2a1f: a quit pending now (set before or by the slot 14) forces
        // the intro.
        if (!bForceIntro && ClientQuitRequested())
            bForceIntro = true;
    }

    // 0x005b29a7..0x005b29ac: state 6 (IntroFocus) and the quit flag cleared.
    if (bForceIntro)
    {
        NextState = 6;
        SetClientQuitRequested(false);
    }

    // 0x005b29b3..0x005b29be: a state queued by FUN_005b2c70 (DAT_0067ecc4) wins and is
    // cleared.
    NextState = ConsumePendingClientState(NextState);

    // 0x005b29c8..0x005b29d8: DAT_0067ecac = FUN_005b2a30(NextState, DAT_0067ecac).
    if (NextState != 0)
    {
        CurrentState = EnterClientState(NextState, CurrentState);
        g_client.current_state = CurrentState;
    }

    // 0x005b29de..0x005b29e6: no state left -> PostQuitMessage(0).
    if (CurrentState == nullptr)
        PostQuitMessage(0);
}

void TickMaterialAnimationPlayback(float DeltaSeconds)
{
    // FUN_0041b2e0. No null tests: both DALib globals are live from engine init on.
    DALib::TextureLib->TickAllPlayback(0, DeltaSeconds);   // slot 0x20 (0, dt)
    DALib::MatAnimLibrary->TickAnimationInstances(-1, DeltaSeconds);         // slot 0x18 (-1, dt)
}

void EndClientRenderScene()
{
    EndSceneHalf();
    EndClientProfileRender();
}

void PresentClientRenderFrame()
{
    // FUN_00425140 -- every retail caller sits inside its FUN_00424f30 success branch.
    PresentHalf();
}

bool BeginClientRenderFrame()
{
    // FUN_00424f30; the result is DAT_006679f8 (G_bRenderFrameBegun).
    BeginClientProfileRender();
    const bool bBegun = TryBeginFrame();
    if (!bBegun)
        EndClientProfileRender();
    return bBegun;
}

void EndClientRenderFrame()
{
    // FUN_00425080 + FUN_00425140 back to back.
    EndSceneHalf();
    EndClientProfileRender();
    PresentHalf();
}
