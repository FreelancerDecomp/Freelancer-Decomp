// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_environment.h"

// Retail client state machine FUN_005b2a30 / DAT_0067ecac. The state objects are IEnvironment
// classes (client_environment.h): FUN_0054bd30 SpaceApp, FUN_00437420 = DAT_006686a0 GF (the
// base, COL 0x005e8734), FUN_00575430 IntroFocus, FUN_0043d560 = DAT_00668774 MissionBriefing
// (COL 0x005e88d4).
// 1 quit, 2 space Init(0), 3 GF Init(from intro), 4 GF slot 10 (state kept), 5 space slot 10
// (state kept), 6 IntroFocus Init(0), 7 reconnect on DAT_0067ea78 then GF Init(1) or space
// Init(0), 8 MissionBriefing Init(0) (mission_briefing.cpp). The callers store the answer in
// DAT_0067ecac (g_client.current_state).
IEnvironment* EnterClientState(int StateId, IEnvironment* PreviousState = nullptr);

// DAT_0067ea78 -- the character state 7 reconnects with (written by FUN_005b2c70).
struct CHARACTER_ID;
extern CHARACTER_ID G_LoadGameCharacterId;
// DAT_0067ecc4 -- a state id the frame enters instead of the current state's answer.
extern int G_PendingClientStateId;
// FUN_005b2c70 (cdecl) -- a non-empty CharId: DAT_0067ecc4 = 7, DAT_0067ea78 = *CharId, and with
// bSkipAutosave IServer::CharacterSkipAutosave. Callers: NN_LoadSave's load 0x00488144 and
// 0x004bc8d5.
void QueueLoadGameCharacter(CHARACTER_ID const* CharId, bool bSkipAutosave);
// The frame's 0x005b29b3..0x005b29be: a queued state (DAT_0067ecc4) replaces StateId and is
// cleared.
int ConsumePendingClientState(int StateId);

// The call NN_Prompt's modal loops (004740b6, 00474220, 0047439b) and ServerFilterDialog's
// (00563872) inline: DAT_0067ecac != 0 -> DAT_0067ecac->Update (slot 14, +0x38) with the frame
// time as a float, the answer dropped.
void CallClientStateUpdateSlot(float DeltaSeconds);

// FUN_005b0880: DAT_0067ecac != 0 && DAT_0067ecac->GetEnvironmentType() (slot 7) == 2. The types:
// 0 space (0x005e0554 -> 004b5010), 1 GF (0x005cae24 -> 0051ca40), 2 IntroFocus (0x005e3034 ->
// 00517320), 3 MissionBriefing (0x005cb094 -> 0043d2e0). So it is "the current state is the
// intro menu".
bool IsClientIntroEnvironment();

// FUN_00575430 -- the IntroFocus singleton DAT_0067a598 (client_state.cpp).
IEnvironment* GetIntroFocusState();
