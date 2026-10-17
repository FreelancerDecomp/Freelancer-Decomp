// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The MissionBriefing client state (RTTI COL 0x005e88d4), the singleton DAT_00668774 returned
// by FUN_0043d560 -- client state code 8. The GF base frame answers 8 after FUN_00558a30
// (RequestGameflowMissionBriefing); FUN_005b2a30 case 8 then destroys GF and Inits this state
// with 0. It hides the base, shows the "BriefingParentWindow" StaticControl with the
// NeuroNetNavMap in briefing mode inside it, and answers 2 (back to space) once the nav map
// asks to continue (FUN_0043d580) and the UI sounds have finished.
//
// vftable 0x005cb094 (static init LAB_0043d120), fifteen slots:
//   0 LAB_0043d130 (RET 0x10)  key message: FUN_0046a480 test, GetKeyState x2, true
//   1 LAB_0043d160 (RET 0xc)   true
//   2 LAB_0043d160 (RET 0xc)   true
//   3 LAB_0043d150 (RET 0x10)  true
//   4 LAB_0043d160 (RET 0xc)   true
//   5 0x00401e90               bare RET
//   6 0x0053e4a0 (RET 4)       bare RET 4 (ICF-folded with IDPMsgHandler::OnAddHost)
//   7 LAB_0043d2e0             3 (the environment type)
//   8 LAB_0043d170             pre-init: the BriefingParentWindow
//   9 LAB_0043d260             shutdown: delete the window tree
//  10 0x00401e90               bare RET
//  11 0x0053aaa0 (RET 8)       bare RET
//  12 LAB_0043d2f0 (RET 4)     Init
//  13 LAB_0043d300             Destroy
//  14 LAB_0043d360 (RET 4)     Update
//
// Layout (0x18):
//   +0x00 vptr
//   +0x04 byte  the first frame is still pending (1 from pre-init and every exit)
//   +0x08 Win3D* the "BriefingParentWindow" StaticControl
//   +0x0c byte  DAT_00668780 -- 1 while the NeuroNetNavMap is being built (FUN_0043d570)
//   +0x0d byte  DAT_00668781 -- the nav map's continue request (FUN_0043d580)
//   +0x10 dword DAT_00668784 -- the base FUN_0043d590 stored
//   +0x14 byte  idle: 1 from pre-init and every exit, 0 from Init
// The three members at +0x0c / +0x0d / +0x10 live in gameflow.cpp, where their accessors are.

#include "client_environment.h"

// FUN_0043d560 -- &DAT_00668774, struct MissionBriefing : IEnvironment (mission_briefing.cpp).
// Slot 8 is sent by InitializeEngine (0x005b1f53..0x005b1f5c), slot 9 by the client teardown
// (0x005b21f6..0x005b21ff).
IEnvironment* GetMissionBriefingState();
