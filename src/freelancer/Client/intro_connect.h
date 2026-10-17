// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FUN_005b20a0: WinMain 0x005b3324 hands its result to FUN_005b2a30 (EnterClientState).
// Returns the first client state: 6 = IntroFocus menu (the normal case), 2/3 = [Login]
// auto-login succeeded (in game / FUN_004c4810 variant), 1 = auto-login failed.
int ComputeInitialEnvironment();
