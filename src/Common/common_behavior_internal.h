// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by common_behavior.cpp, common_basefollowerbehavior.cpp,
// common_formationbehavior.cpp and common_gunconvergence.cpp. CMN-SPLIT stage 1 (2026-10-14):
// these were file-static in the one common_behavior.cpp; the split made them shared, so they are
// declared here in a named namespace and defined in common_behavior.cpp. The SystemManager+0x1364
// accessors once declared here (FollowerDirectiveRecord_*, FollowerDirectiveGateB) are
// FGunConvergenceStateBlock members now (common_systemmanager.h, common_gunconvergence.cpp).

#include "common_systemmanager.h" // SystemManager, IObjRW, CObject

namespace CmnBehavior
{
    // IObjInspect::is_player on Sys->ExternalIface (no null tests, as retail).
    bool FollowerExternalIfaceIsPlayer(SystemManager* Sys);

    // `[obj+0x4c] & 0x503 == 0x503` -- the object if it is a ship, else null.
    CObject const* FollowerPlayerShipOrNull(CObject const* Object);
}
