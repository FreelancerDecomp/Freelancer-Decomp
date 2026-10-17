// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The head of the connect slice FUN_0054ab40 before the system enter: FUN_005020a0
// (JumpEffectIniLoad, jump_effect.cpp) and FUN_00510f90. The system enter itself is StarSysEnter (star_sys.h).

bool LoadSpaceSceneSlice();
