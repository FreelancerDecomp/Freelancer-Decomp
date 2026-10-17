// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "space_scene_load.h"
#include "jump_effect.h"
#include "star_sphere.h"

#include "common_ini.h"

// 2026-09-25 (EXE-W2-E): the system INI load that used to live here -- a hand-rolled reading
// of [System]/[Archetype]/[Asteroids]/[Object]/[Nebula] with an invented "deferred spawn
// queue" standing in for the [Archetype] preload list, a fake current-system id
// (CreateID("Li01")) and a second copy of DAT_00679954 -- is gone. Retail's owner of all of
// it is StarSys.cpp: FUN_005545a0 / FUN_00553740 in star_sys.cpp, reached from the connect
// slice FUN_0054ab40 (player_spawn.cpp) with the real id from FUN_004c4830.
// The scaffold that hung off it (space_scene_spawn.*, space_scene_objects.*,
// space_equipment_spawn.*, the external draw vtable and the world-position lookups) had no
// caller left and was removed with it.
//
// What remains here is the part of FUN_0054ab40 that precedes the system enter:
// FUN_005020a0 (jump-gate FX INI) and FUN_00510f90 (solar star archetypes).

bool LoadSpaceSceneSlice()
{
    // 0x0054ac15: FUN_005020a0 -- the jump-gate FX INI (JumpGateEffect / JumpShipEffect
    // sections).
    JumpEffectIniLoad();
    ::LoadSolarStarArchIni(); // FUN_00510f90
    RefreshLoadingProgress();
    return true;
}
