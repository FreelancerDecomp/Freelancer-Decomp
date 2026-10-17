// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Universe\MissionCreatedSolars.ini -- the positions and infocards of the bases that missions
// create at run time (they have no [Object] in a system file). Loaded once, on first use, into
// the vector DAT_0066dc08 (static init LAB_00476bf0, atexit LAB_00476c20).
//
//   [MissionCreatedSolar]
//   base     = <base nickname>
//   ids_info = <ids>
//   pos      = x, y, z
//
// Readers: the nav map's base / solar position lookups (FUN_0048d780, FUN_00491e10) and the
// NN base-info code at 0x0047503c.

#include <vector>

// One record, 0x14 bytes (FUN_00476c40 0x00476dd5..0x00476e03).
struct FMissionCreatedSolar
{
    unsigned BaseId; // +0x00 CreateID(base)
    float Pos[3];    // +0x04 pos
    int IdsInfo;     // +0x10 ftol(ids_info)
};

// DAT_0066dc08 (allocator byte) / DAT_0066dc0c.. (first, last, end).
extern std::vector<FMissionCreatedSolar> G_MissionCreatedSolars;
// DAT_0066dc59 -- the load latch (set before the file is opened, so a missing file is not
// retried).
extern unsigned char G_bMissionCreatedSolarsLoaded;

// FUN_00476c40 -- once: read MissionCreatedSolars.ini; a record is pushed as soon as a section
// has given all three keys (and the three flags start over).
void LoadMissionCreatedSolars();

// FUN_00476fe0 (cdecl) -- load, then the first record for BaseId: its position into OutPos
// (3 floats) and its ids_info into OutIdsInfo. False (nothing written) when there is none.
bool FindMissionCreatedSolar(unsigned BaseId, float* OutPos, int* OutIdsInfo);
