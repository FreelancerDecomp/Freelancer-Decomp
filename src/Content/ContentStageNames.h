// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail .data table PTR_s_base_0_rank_06fce5d0: 43 {name, value} pairs mapping a story
// stage name to the story mission number ("base_0_rank" = 0 ... "mission_end" = 0x2a),
// last entry {null, 0}. Read by name-lookup loops that walk all 43 entries
// (loop bound 0x158 bytes): TU19 06eb418e / 06eb41da, TU22 06ed4029 / 06ed4070,
// TU47 FUN_06f4a9f0 (06f4aa5f). One object in retail -> declared once here,
// defined in RandomMissionDB.cpp.
struct FContentStageName
{
    char const* Name;
    int Value;
};

extern FContentStageName const GContentStageNames[43];
