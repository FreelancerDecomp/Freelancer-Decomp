// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_environment.h"

// Retail in-space client state singleton FUN_0054bd30 / DAT_00678b98 -- SpaceApp (space_app.h),
// an IEnvironment. Its callers (the state machine FUN_005b2a30, InitializeEngine 0x005b1e9c,
// the teardown 0x005b21ea) only send it IEnvironment slots.
struct SpaceApp;

IEnvironment* GetSpaceState();
// FUN_0054ab40 -- slot 12 (+0x30), thiscall RET 4. True when the space init finished with no
// next state pending (DAT_00679778 == 0).
bool InitSpaceState(SpaceApp* State, int Param);
int UpdateSpaceState(SpaceApp* State, float DeltaSeconds);
void DestroySpaceState(SpaceApp* State);
// FUN_0054ab20 -- slot 9 (+0x24) of the space state vftable .rdata 0x005e0554: FUN_00554e80,
// this->slot 13 (DestroySpaceState), CObject::ReleaseAll, JMP FUN_0051fbc0. Caller: the client
// atexit teardown (0x005b21ea..0x005b21f3, on the FUN_0054bd30 singleton).
void ShutdownSpaceState(SpaceApp* State);

// FUN_00548ef0 -- store the base a landing ship asked for (DAT_00679784). Callers: the Ship's
// land request (0x00543d12) and the Land action's update (0x00542ddd).
void SetPendingLandBase(unsigned const* BaseId);

// FUN_00548ed0 -- store the system a jumping ship asked for (DAT_0067977c) and the byte argument
// (DAT_00679780). Callers: the Ship's jump state 3 (0x005439d6) and the IClient
// system-switch-out handler's helper FUN_00543750 (0x0054378a); both pass false. The space frame
// consumes it (FUN_0054a940).
void SetPendingJumpSystem(unsigned const* SystemId, bool bFlag);

// FUN_0054bc60 (cdecl) -- the player's ship landed on a dock the ObjDB knows: the base recorded
// (FUN_004c4910), FUN_004c4a10(0), and the space frame's next state set to 4. Caller: the
// IClient land handler (0x0053d782) with &dock CEqObj::DockingPointBaseId (+0x164).
void EnterBaseFromDock(unsigned const* BaseId);

