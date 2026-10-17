// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// GF -- the client state for being docked at a base (RTTI .?AUGF@@, COL 0x005e8734), and the
// Support-file functions it runs.
//
// The state object is DAT_006686a0 (FUN_00437420 returns its address), struct GF :
// IEnvironment (client_environment.h), vftable 0x005cae24, source
// E:\FL\Scratch\Source\Client\gf\Gameflow.cpp (FDUMP at line 0xa7). Layout: the vptr, then
// three bytes -- +0x04 "first frame pending" (Init sets it, the first Update clears it), +0x05
// "base entered", +0x06 the Init argument. The main loop's state machine (FUN_005b2a30,
// client_state.cpp) enters it for code 3 (a base is set) and code 7 on a base, sends it slot 10
// for code 4, and slot 13 / slot 14 like every state; InitializeEngine sends it slot 8
// (0x005b1f47..0x005b1f50) and the atexit teardown slot 9 (0x005b21de..0x005b21e7).
//
// Its slot bodies call into E:\FL\Scratch\Source\Client\supt\Support.cpp (the viewport at
// 0x00558c89 names it, line 0x222), freelancer.exe 0x00558870..0x005592aa: entering and leaving
// a base (FUN_005588a0 / FUN_00558a50), the base frame (FUN_00558c10) and its tick
// (FUN_00558ac0), and the mouse tracking over room hotspots (gf_hotspot.h).

#include "client_environment.h"

class FHotspot;

// FUN_00437420 -- &DAT_006686a0, the GF state object. Its slots: PreInit (8, LAB_00437270)
// = FUN_00558850 with DAT_0067eca0, then +0x04 = +0x05 = 0; Shutdown (9, LAB_00437290) = JMP
// FUN_00558870; the rest gameflow.cpp.
IEnvironment* GetGameflowState();

// FUN_00558850 (cdecl, one ignored argument) -- IDPMsgHandler::OnDisconnect (a bare RET),
// FUN_004393b0 (the LocationController maps), FUN_004354e0 (the CharacterInfo database), JMP
// FUN_004355e0 (its preload scripts).
void InitGameflowData();

// FUN_00558870 (cdecl) -- FUN_00435500 (CharacterInfo emptied), FUN_00439260 on DAT_00668708
// (the location maps), FUN_00559380 (the RoomObject list), FUN_004348b0 on DAT_00667e50 and
// JMP FUN_00556200 (the hotspot leak check, whose answer is the return value).
bool ShutdownGameflow();

// FUN_004348b0 (thiscall) on DAT_00667e50, the engine-instance table: 256 engine handles
// (+0x000, -1 = none), 256 material-cache entries (+0x400) and a count (+0x800,
// DAT_00668650). Each of the Count entries has its material slot released (FUN_0041b030) and
// its handle given back (IEngine slot 15), then every slot is reset to -1 / null and Count to 0.
// Nothing in freelancer.exe ever adds an entry (raw PE scan of 0x00667e50 / 0x00668250 /
// 0x00668650: only the static initializer LAB_00434920, its atexit LAB_00434950 and the three
// callers), so in practice this only rewrites the initial image. Callers: 0x0054aeda (space
// exit), 0x00558884 (ShutdownGameflow), 0x00558a8f (LeaveGameflowBase).
void ResetEngineInstanceCache();

// FUN_005588a0 (cdecl) -- enter a base. LocationId 0 goes through FUN_0043b290 (the base's own
// start location); otherwise FUN_0043b570 + FUN_0043bab0 + FUN_0043b6b0 on it. bFromIntro is
// GF's +0x06: when clear the arrival caption is shown. Returns true.
bool EnterGameflowBase(unsigned BaseId, unsigned LocationId, bool bFromIntro);

// FUN_00558a30 -- cut to black (FUN_00412070(1.0)) and make the base frame answer 8 (the
// MissionBriefing state, FUN_0043d560). Callers: 0x00440fc0, 0x00447d62, 0x00448283,
// 0x00455536.
void RequestGameflowMissionBriefing();

// FUN_00558a50 (cdecl) -- leave the base (GF slot 13).
void LeaveGameflowBase();

// FUN_00558bf0 -- DAT_006799e8 = frame count + 2: the base frame passes a zero frame time until
// the frame counter has moved past it. Callers: 0x0044988c, 0x00455097, 0x00455548.
void HoldGameflowTime();

// DAT_006799e4 -- the arrival caption's delay in milliseconds. The Gf script sequence's
// FUN_00454eb0 (0x00454f0e) stores each added script's duration there.
void SetGameflowArrivalDelayMs(unsigned DelayMs);

// FUN_00558c00 -- make the next base frame answer 5 (once). Caller: 0x00447d58.
void RequestGameflowStateFive();

// FUN_0043d590 (cdecl) -- DAT_00668784 = BaseId. Callers: FUN_005588a0, FUN_0043b6b0.
// DAT_00668784 is the MissionBriefing state's +0x10: its update LAB_0043d360 passes it to
// FUN_004c4910 (0x0043d440, a LEA, not a MOV read).
void SetGameflowLocationBase(unsigned BaseId);
// The MissionBriefing state's read of DAT_00668784 (mission_briefing.cpp).
unsigned GetGameflowLocationBase();

// The MissionBriefing state object DAT_00668774 (FUN_0043d560) keeps two bytes the nav map
// reads and writes through the accessors below: +0x0c (DAT_00668780) is 1 while its slot 14
// (LAB_0043d360, 0x0043d45c) builds the NeuroNetNavMap, +0x0d (DAT_00668781) is the
// "continue" request. The state itself is mission_briefing.cpp; these three members of it
// (+0x0c, +0x0d, +0x10) stay globals here, where their accessors already were.
extern unsigned char G_bMissionBriefingNavMapUp;    // DAT_00668780 (state +0x0c)
extern unsigned char G_bMissionBriefingContinue;    // DAT_00668781 (state +0x0d)
// FUN_0043d570 (cdecl) -- MOV AL,[DAT_00668780]. Caller: NeuroNetNavMap::Create 0x0048ebac.
unsigned char IsMissionBriefingNavMapUp();
// FUN_0043d580 (cdecl) -- DAT_00668781 = 1. Caller: NeuroNetNavMap::OnEvent 0x0049e1a4.
void RequestMissionBriefingContinue();

// FUN_00558d60 (cdecl) -- with a room camera: FUN_0041a3d0 (forget the letterbox camera) and
// FUN_0041a140(camera, 0). Callers: GF slot 11, 0x004b1caf.
void ResetGameflowRoomLetterbox();

// FUN_00558db0 -- the hotspot tracking reset: DAT_00679a04 = 0, DAT_00679a08 = 0,
// DAT_00679a0c = 0.
void ResetHotspotTracking();
// FUN_00558dd0 -- DAT_00679a08 = 0 (the hovered hotspot forgotten, no leave event). Caller:
// 0x00449891.
void ClearHoveredHotspot();
// FUN_00558de0 (cdecl) -- the same, only when Hotspot is the hovered one. Callers: the Hotspot
// destructors FUN_00555e3a / FUN_00555eaa.
void ClearHoveredHotspotIf(FHotspot* Hotspot);
// FUN_00558e90 -- stop tracking: the hovered hotspot (if any) gets a leave with point {0, 0}.
void ReleaseHoveredHotspot();
// FUN_00559290 / FUN_005592a0 -- DAT_00679a0c = 1 / 0: while it is set the tracking ignores the
// mouse. Callers: 0x00455497 / 0x00449896, 0x004555b6.
void LockHotspotInput();
void UnlockHotspotInput();
