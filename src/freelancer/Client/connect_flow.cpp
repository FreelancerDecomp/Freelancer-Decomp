// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "connect_flow.h"

#include "chat_init.h"      // ChatResetForConnect (FUN_0046b2b0)
#include "common_ini.h"     // RefreshLoadingProgress
#include "common_server.h"  // IServer
#include "common_misc.h"    // SinglePlayer
#include "connect_lists.h"  // PreConnectReset (FUN_004c4a80)
#include "login_mgr.h"
#include "nn_error.h"       // NnErrorRunModal (FUN_00473cb0)
#include "player_spawn.h"   // GetPlayerConnectionId (DAT_00673344)
#include "server_connect.h"

// FUN_005a8770 (cdecl).
bool ConnectToServer(char const* IniPath)
{
    RefreshLoadingProgress();
    PreConnectReset();                      // FUN_004c4a80

    if (StartServer(IniPath))
    {
        int const Result = G_LoginMgr.Connect(true);
        if (Result == 1)
        {
            return true;
        }
        // .text:005a87a4-005a87d2: the NN_Error modal FUN_00473cb0(ids, 1, 0) -- 0x73e for 4 and
        // 0x73f for 6 unconditionally; any other result shows 0x73d unless the server already
        // dropped us (DAT_0067ecbe).
        if (Result == 4)
        {
            NnErrorRunModal(0x73e, true, nullptr);
        }
        else if (Result == 6)
        {
            NnErrorRunModal(0x73f, true, nullptr);
        }
        else if (!G_bServerDisconnected)
        {
            NnErrorRunModal(0x73d, true, nullptr);
        }
    }

    // .text:005a87da-005a882a: ShutdownServerConnection's body, inlined.
    ShutdownServerConnection();
    return false;
}

// FUN_005a8840 (cdecl).
bool LoginToServer()
{
    if (G_Server == nullptr)
    {
        return false;
    }

    int Result;
    if (SinglePlayer())
    {
        G_LoginMgr.bCreateAccount = 0;      // .text:005a885f DAT_0067e960
        Result = G_LoginMgr.Login();
        if (Result != 1 && G_LoginMgr.LoginStatus == 2)
        {
            G_LoginMgr.bCreateAccount = 1;  // .text:005a887c
            Result = G_LoginMgr.Login();
        }
    }
    else
    {
        Result = G_LoginMgr.Login();
    }

    if (Result == 1)
    {
        ChatResetForConnect(); // .text:005a894c FUN_0046b2b0 (returns at once in single player)
        return true;
    }

    if (Result != 2)
    {
        // .text:005a889b-005a88c6: the NN_Error modal FUN_00473cb0(ids, 1, 0) keyed on the login
        // status byte (DAT_0067e969): 0x742 for 0, 0x753 for 1, 0x743 for 7 unconditionally;
        // any other status shows 0x739 unless the server already dropped us (DAT_0067ecbe).
        if (G_LoginMgr.LoginStatus == 0)
        {
            NnErrorRunModal(0x742, true, nullptr);
        }
        else if (G_LoginMgr.LoginStatus == 1)
        {
            NnErrorRunModal(0x753, true, nullptr);
        }
        else if (G_LoginMgr.LoginStatus == 7)
        {
            NnErrorRunModal(0x743, true, nullptr);
        }
        else if (!G_bServerDisconnected)
        {
            NnErrorRunModal(0x739, true, nullptr);
        }
    }

    // .text:005a88ce-005a891f: ShutdownServerConnection's body, inlined.
    ShutdownServerConnection();
    return false;
}

// FUN_005a8960 (cdecl).
bool ConnectAndLogin(char const* IniPath)
{
    if (!ConnectToServer(IniPath))
    {
        return false;
    }
    return LoginToServer();
}
