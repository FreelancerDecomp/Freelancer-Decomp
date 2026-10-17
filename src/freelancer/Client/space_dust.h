// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// SpaceDust -- retail Client\Space\SpaceDust.cpp (freelancer.exe 0x0054bda0..0x0054c361).
// Three ParticleEffects follow the camera: the system's own dust ([Dust] spacedust= of the
// system INI) and, while the camera sits in a PROP_DUST_ZONE zone, that zone's dust, with the
// previous zone's dust fading out. All state is file-level (.bss 0x00679790..0x006797c0).

#include "common_archetype.h" // CacheString
#include "common_universe.h"

// FUN_0054bde0 (cdecl) -- StarSysLoadIni's [Dust] section (00553d6d): remember the system dust
// name (pointer copy into DAT_00679794) and reset the zone-dust bookkeeping. No effect is
// created here; the first SpaceDustUpdate does that.
void SpaceDustSetSystemDust(CacheString const* Dust);

// FUN_0054be20 -- StarSysLeave (00554c0b): release all three effects, clear both names.
void SpaceDustShutdown();

// FUN_0054beb0 (cdecl) -- StarSysUpdate (00554ecb) with DAT_00679954 and the active camera.
void SpaceDustUpdate(Universe::ISystem const* System, void* Camera);
