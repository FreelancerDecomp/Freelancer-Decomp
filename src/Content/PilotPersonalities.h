// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU45 (06f36f10-06f46940): pilot personalities from
// missions\pilots_population.ini / missions\pilots_story.ini.
//
// Retail keeps seventeen file-static std::map objects (the pilot map
// DAT_06fd0aa0 and one map per *Block section, DAT_06fd0a8c..DAT_06fd0960) and
// exposes the functions below to the rest of Content.dll.

#include "common_id.h"
#include "common_pub.h"

class INI_Reader;

// FUN_06f3d400: walk every header of an already opened pilots ini and file
// each *Block section / [Pilot] into its map. Returns true (retail AL = 1).
bool __cdecl LoadPilotPersonalities(INI_Reader* Reader);

// FUN_06f3a040: erase every entry of the seventeen maps.
void __cdecl ClearPilotPersonalities();

// FUN_06f3df10: copy the JobBlock named by JobId into Personality->JobParams.
// false when JobId is empty or no such JobBlock was loaded.
bool __cdecl ApplyPilotJob(ID_String* JobId, pub::AI::Personality* OutPersonality);

// FUN_06f3e020: fill a Personality from the [Pilot] named by PilotId.
// false when the pilot is unknown (Personality untouched).
bool __cdecl GetPilotPersonality(unsigned int* PilotId, pub::AI::Personality* OutPersonality);

// FUN_06f3dfd0: GetPilotPersonality, then ApplyPilotJob(JobId) when JobId is
// not empty. false when the pilot, or a non-empty job, is unknown.
bool __cdecl GetPilotPersonalityWithJob(unsigned int* PilotId, ID_String* JobId,
                                        pub::AI::Personality* OutPersonality);
