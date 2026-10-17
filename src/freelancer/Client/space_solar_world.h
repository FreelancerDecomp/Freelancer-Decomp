// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h"

// Retail FUN_00548a20 — system INI [Object] → real CSolar in client IObjDB.
// Sole become-real path for system objects (FSpaceSceneObject proxies removed).
// Runs during LoadSpaceSceneSlice (connect), before any BeginScene.

// Returns true when a FClientSolarGameobj was created, inited, and linked.
bool SpawnWorldSolarFromObjectSection(INI_Reader& Reader);

// FUN_005488a0 (cdecl) -- push a planet-sphere detail level into every solar's ISphereMesh:
// T = clamp(2 * Detail - 1, 0, 1); Slot07(ftol(3T + 7)) and Slot03(float(ftol(3.5 - 2T))).
// Caller: the level_of_detail option apply LAB_005b4560 (perf_options.cpp).
void SolarSpheresApplyDetail(float Detail);
