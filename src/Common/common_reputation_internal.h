// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common-internal declarations shared by the three TUs of retail's Reputation.cpp /
// CharDescrip.cpp cluster: common_costume.cpp, common_reputation.cpp and common_vibe.cpp.
// Nothing here is exported; everything is defined in common_reputation.cpp.

#include "common_reputation.h"

#include <map>
#include <vector>

namespace CmnReputation
{
    // A record of the group registry DAT_064018ec (std::map keyed by the 16-bit MakeId value;
    // record = node+0x10): +0x00 nickname TString<16>, +0x14 ids_name, +0x18 ids_info,
    // +0x1c ids_short_name, +0x20 feelings map (Reputation::Startup 0631eb11-0631eba1).
    struct FGroup
    {
        TString<16> Nickname;
        int IdsName;
        int IdsInfo;
        int IdsShortName;
        std::map<unsigned, float> Feelings;
    };

    // The story-faction ids, filled lazily by EnsureStoryFactionIds.
    extern std::vector<unsigned> GStoryFactionIds;

    FGroup const* FindGroupConst(unsigned GroupId);
    void EnsureStoryFactionIds();
}

// The DACOM.DLL stricmp import retail calls (common_dacom_stricmp.cpp).
int CallDacomStricmp(const char* Left, const char* Right);
