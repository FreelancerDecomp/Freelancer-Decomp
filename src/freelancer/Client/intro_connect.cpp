// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "intro_connect.h"

#include "character_select_equip.h" // GetPlayerBaseIdRetail (FUN_004c4810's DAT_00673358)
#include "connect_flow.h"
#include "globals.h"
#include "login_mgr.h"
#include "menu_mp_session.h"

#include "common_misc.h" // SinglePlayer
#include "dacom.h" // FDUMP

#include <string.h>
#include <windows.h>

int ComputeInitialEnvironment()
{
    // FUN_005b20a0 (0x005b20a0..0x005b218d). Default result 6 (menu, IntroFocus). The port-only
    // -spquickstart prefix that used to run here is gone: the retail [Login] auto-login below is
    // the same quick start (it needs [Login] char just as the shortcut did).
    int Result = 6;
    // 0x005b20a5..0x005b20ce: the [Login] auto-login is on when the login name (+0x188, the
    // copy FUN_005a96b0 returns) is not empty and the character name (DAT_0067e9a0, +0x1e8) is
    // not empty. CLoginMgr::LoadConfig (FUN_005a82d0, main.cpp) fills both from [Login].
    bool const bAutoLogin =
        !G_LoginMgr.LoginName.empty() && G_LoginMgr.SelectCharacterName[0] != 0;
    if (bAutoLogin)
    {
        // 0x005b20ff..0x005b2118: SinglePlayer() || FUN_005accb0(&DAT_0067e84c) -- the
        // current server (+0x94) has its +0x28 and +0x98 fields set.
        if (SinglePlayer() || MenuMpSessionIsReady(&G_LoginMgr.CurrentServer))
        {
            Result = 1;
            // 0x005b211a..0x005b215a.
            if (ConnectAndLogin(g_display.ini_path.Text) &&        // FUN_005a8960
                G_LoginMgr.RequestCharacterInfo() != 0 &&          // FUN_005aa680
                G_LoginMgr.SelectAutoCharacter())                  // FUN_005ab0f0
            {
                // FUN_004c4810: the player's base (DAT_00673358 & 0x3fffffff) is set.
                Result = (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0 ? 3 : 2;
            }
            if (Result == 1)
            {
                // 0x005b2161..0x005b2181.
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Client\\main.cpp",
                      0x476, "ComputeInitialEnvironment() failed - exiting Freelancer");
            }
        }
    }
    return Result;
}
