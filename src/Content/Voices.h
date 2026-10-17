// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU53 (06f5c760-06f6b840), voice-line durations: [Voice] / [Sound] sections of
// audio\voices_space_male.ini, voices_space_female.ini and voices_recognizable.ini.

#include <vector>

class INI_Reader;

// One [Sound] line, 12 bytes (vector DAT_06fd1ba8..DAT_06fd1bb4), sorted by
// (VoiceId, MsgId) unsigned.
struct FVoiceLine
{
    unsigned int VoiceId;                                    // +0x00 pub::GetVoiceID of [Voice] nickname
    unsigned int MsgId;                                      // +0x04 pub::GetNicknameId of msg
    float Duration;                                          // +0x08
};
static_assert(sizeof(FVoiceLine) == 0xc, "retail voice line is 12 bytes");

// DAT_06fd1ba8: constructed by LAB_06f5cbf0, freed by the atexit FUN_06f5cc20.
extern std::vector<FVoiceLine> GVoiceLines;

// FUN_06f67840: append this file's lines, re-sort the table, return its size.
int __cdecl LoadVoiceLines(INI_Reader* Reader);
// FUN_06f67780: the duration of (VoiceId, MsgId), or -1.0f.
float __cdecl FindVoiceLineDuration(unsigned int const* VoiceId, unsigned int const* MsgId);
