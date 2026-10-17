// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "connect_handshake.h"

#include "client_frame_tick.h"    // BeginClientRenderFrame / EndClientRenderFrame
#include "common_server.h"        // IServer
#include "common_misc.h"          // SinglePlayer
#include "common_timing.h"        // Timing::read
#include "globals.h"              // PumpMessages, GetClientFrameTimeCapped
#include "hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "login_mgr.h"
#include "menu_enter_state.h"     // FUN_00564b10 / FUN_00564b20
#include "server_connect.h"       // ShutdownServerConnection (FUN_005a8260)
#include "nn_blocking_dialog.h"   // FUN_00474520 / FUN_00474590
#include "nn_network_operation.h" // FUN_00473ad0 / FUN_00473b60 / FUN_00473be0
#include "win3d.h"                // Win3DUpdateAll / Win3DDrawAll

// Retail .data:0067ea50 -- the DirectPlay connect HRESULT HandleConnectError records; zeroed
// and polled by WaitForServerResponse.
long G_ConnectResult = 0;

// Retail .data:0067ea54 -- 1 while WaitForServerResponse runs. Nothing in freelancer.exe
// reads it (XREF: 005a89bb / 005a8b6a / 005a8b83 / 005a8bc8, all writes).
bool G_bWaitingForServer = false;

// Retail .text:005a8760 HandleConnectError.
extern "C" __declspec(dllexport) void __cdecl HandleConnectError(long Result)
{
    G_ConnectResult = Result;
}

// Retail .text:005a89b0 (cdecl).
int WaitForServerResponse(unsigned char* bResponse, float MpTimeout, float SpTimeout)
{
    G_bWaitingForServer = true;
    int Result = 1;
    G_ConnectResult = 0;
    float const StartTime = static_cast<float>(Timing::read()); // FSTP float

    if (!SinglePlayer())
    {
        BlockingDialogOpen(); // .text:005a89e5 FUN_00474520
    }

    // .text:005a89ed-005a8a45. The x87 test `TEST AH,5; JNP` leaves the loop only when
    // Timeout < Elapsed; equal or unordered keeps pumping.
    while (*bResponse == 0 && !G_bServerDisconnected && G_ConnectResult == 0 && G_Server != nullptr)
    {
        G_Server->Update(); // +0x2c
        double const Elapsed = Timing::read() - static_cast<double>(StartTime);
        double const Timeout = static_cast<double>(SinglePlayer() ? SpTimeout : MpTimeout);
        if (Timeout < Elapsed)
        {
            break;
        }
    }

    if (!SinglePlayer())
    {
        BlockingDialogClose(); // .text:005a8a51 FUN_00474590
    }

    if (!SinglePlayer() && *bResponse == 0)
    {
        if (G_bServerDisconnected)
        {
            Result = 0;             // .text:005a8a74 -> 005a8b4e
        }
        else if (G_ConnectResult == 0)
        {
            // .text:005a8a87-005a8b40: the NN_NetworkOperation wait.
            NnNetworkOperationOpen(0x736); // FUN_00473ad0
            int ExitCode = 0;              // [ESP+0xc], FUN_005b0b60's out slot
            while (*bResponse == 0)        // 005a8a91 / 005a8b2c
            {
                // 005a8aa0..005a8ab4: a drop or a DirectPlay error ends the wait as it is.
                if (G_bServerDisconnected || G_ConnectResult != 0)
                {
                    break;
                }
                // 005a8aba..005a8ad2: the dialog was dismissed, or WM_QUIT -> 2 (005a8b37).
                if (!NnNetworkOperationIsOpen() || !PumpMessages(&ExitCode))
                {
                    Result = 2;
                    break;
                }
                // 005a8ad4..005a8adc: no server object -> 0 (005a8b3e).
                if (G_Server == nullptr)
                {
                    Result = 0;
                    break;
                }
                G_Server->Update(); // +0x2c
                // 005a8ae3..005a8af7: FSUB the float start, FLD SpTimeout (param_3), FCOMPP,
                // `TEST AH,5; JNP` -> 0 when SpTimeout < Elapsed; equal or unordered goes on.
                double const Elapsed = Timing::read() - static_cast<double>(StartTime);
                if (static_cast<double>(SpTimeout) < Elapsed)
                {
                    Result = 0;
                    break;
                }
                Win3DUpdateAll(GetClientFrameTimeCapped()); // FUN_0059e080(FUN_0042d680())
                if (BeginClientRenderFrame())               // FUN_00424f30
                {
                    Win3DDrawAll();         // FUN_0059e300
                    EndClientRenderFrame(); // FUN_00425080 + FUN_00425140
                }
            }
            NnNetworkOperationClose(); // 005a8b40 FUN_00473b60
            if (G_bServerDisconnected)
            {
                Result = 0;            // 005a8b45 -> 005a8b4e
            }
        }
    }
    else if (G_bServerDisconnected)
    {
        Result = 0;                 // .text:005a8b45
    }

    if (G_ConnectResult != 0)
    {
        // .text:005a8b59-005a8b74: DPNERR 0x80158410 -> 6, 0x80158610 -> 4, else 5.
        if (G_ConnectResult == static_cast<long>(0x80158410))
        {
            Result = 6;
        }
        else
        {
            Result = (G_ConnectResult != static_cast<long>(0x80158610)) ? 5 : 4;
        }
        G_bWaitingForServer = false;
        return Result;
    }

    if (Result == 2 && !SinglePlayer())
    {
        // .text:005a8ba1-005a8bbd: the player dismissed the NN_NetworkOperation wait. Unless the
        // server-select page is up (FUN_00564b20, DAT_0067c204), drop the connection and send
        // the menu back to mode 3.
        if (!MenuNavIsServerSelectActive())
        {
            ShutdownServerConnection();    // FUN_005a8260
            MenuNavSetIntroModeRequest(3); // FUN_00564b10(3)
            HudPostCommand(0xc5, 0);       // FUN_005763b0(0xc5, 0)
        }
    }

    G_bWaitingForServer = false;
    return Result;
}
