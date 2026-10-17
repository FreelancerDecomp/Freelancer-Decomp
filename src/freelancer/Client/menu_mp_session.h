// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

struct FServerInfo;

// FUN_005accb0 (cdecl, one argument): true when the server record's +0x28 and +0x98 are both
// non-zero. Sole caller: FUN_005b20a0 0x005b210e with &DAT_0067e84c, the login manager's
// current server (CLoginMgr +0x94, login_mgr.h).
bool MenuMpSessionIsReady(FServerInfo const* Server);
