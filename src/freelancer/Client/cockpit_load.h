// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_mesh_load.h"

#include <cstddef>

// The cockpit half of the in-flight init FUN_0054ab40 (0x0054acc7 .. 0x0054ad73) and the ship
// LoadSystem stub of FUN_0054b900.
//
// The cockpit MESH is not loaded here: retail loads it inside the first-person camera's ini
// loader FUN_005173c0 (space_camera_first.cpp), on the FUN_00420d40 holder at camera +0x190.
// This file used to carry its own twin of FUN_00420dd0 (an [Cockpit] re-parse, an fopen probe
// and a deferred engine-bind retry); that twin was removed on 2026-09-25.

// The fallback uses the same ObjectHandle class as every other cockpit mesh owner.
using FCockpitMeshHolder = FMeshHolder;

// The fallback holder player_spawn.cpp hands out when it has no manager object. Always empty
// (instance -1): nothing in this file binds a mesh any more.
const FCockpitMeshHolder& GetCockpitMeshHolder();

// 0x0054acc7 .. 0x0054ad62 of FUN_0054ab40: resolve the player ship's cockpit ini (or the
// "cockpits\cockpit.ini" fallback with a warning), run the four cockpit camera loaders over
// it and load the HUD elements (FUN_004c73d0). The rest of the init is InitSpaceState.
bool InitPlayerCockpitSlice();

// FUN_004c7290 — .data:00673560. Read by HUD_Button::Update (LAB_004c93d0, 0x004c93d0: the
// button is visible only while it is set). Set from two call sites
// around the "IDR_COCKPIT" resource-string-table load/unload (0x0051cad0 pushes true, 0x0051cb70
// pushes false); neither caller is ported. Kept as a plain flag so whichever of them lands later
// has somewhere real to write.
void SetCockpitResourceStringLoading(bool bLoading);
bool IsCockpitResourceStringLoading();
