// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU34 (06f00670-06f03fa0): generic bar scripts (scripts\gcs\genericScripts.ini,
// [GenericScripts]) and the per-voice GCS exclusions (scripts\GCS\GCSExcl.ini,
// [GCS_Exclusions]).

#include "BaseDB.h"     // FBarCharacterList

#include <map>
#include <string>
#include <vector>

struct CharacterBehaviorDescription;   // common_iface.h

// DAT_06fcf478: 0x108 vectors of script names, index Posture + 2 * (Gender + 2 * Segment)
// (0x42 segments x 2 genders x 2 postures). Constructed by LAB_06f01c00 (eh vector ctor,
// element ctor LAB_06f01c50), destroyed by the atexit FUN_06f01c30.
extern std::vector<std::string> GGenericScripts[0x108];

// DAT_06fd04f8: voice id -> script names that voice must not play. Constructed by
// LAB_06f01af0, destroyed by the atexit FUN_06f01b20.
extern std::map<unsigned int, std::vector<std::string>> GGcsExclusions;

// FUN_06f01d70: fill GGenericScripts from the given ini (BaseSupport ctor).
void __cdecl LoadGenericScripts(char const* Path);
// FUN_06f01c70: clear every GGenericScripts vector (BaseSupport dtor).
void __cdecl UnloadGenericScripts();
// FUN_06f02330: fill GGcsExclusions from the given ini (LoadContentGameTables).
void __cdecl LoadGcsExclusions(char const* Path);

// FUN_06f020c0: a random script of the (Segment, Gender, Posture) list that VoiceId is not
// excluded from; null when there is none. Gender 1 selects the "male" lists, Posture 0 the
// "stand" lists.
char const* __cdecl PickGenericScript(int Segment, int Gender, int Posture, unsigned int VoiceId);
// FUN_06f020a0: PickGenericScript(0 /* FIDGET */, Gender, Posture, 0).
char const* __cdecl PickFidgetScript(int Gender, int Posture);

// FUN_06f01ab0 (thiscall on the description, RET 8; callers FUN_06f00b90, inlined seven
// times in the bar scene creator FUN_06ea9640): Text is appended to the second string array
// with Value when it is non-null and non-empty. No bound check (32 slots), as in retail.
void AppendBehaviorText(CharacterBehaviorDescription* Behavior, char const* Text,
                        unsigned int Value);

// FUN_06f00b60 (cdecl): a character on a placement gets the behavior built by FUN_06f00b90.
void __cdecl StartBarCharacterScript(FBarCharacterList::iterator Character);
