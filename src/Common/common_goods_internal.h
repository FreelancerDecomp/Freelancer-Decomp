// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll goods family -- helpers shared by several of the TUs split out of common_goods.cpp
// (formerly that file's anonymous namespace). Common-internal: nothing here is exported.

#include "common_misc.h"       // malloc_strcpy
#include "common_reputation.h" // Costume

namespace CmnGoods
{
    // Costume's inline operator=, as retail expands it wherever a Costume is assigned
    // (CostumeHint::operator= .text:063112b6-063112fe, CharacterProperties::init
    // .text:0632dc47-0632dc87, operator= .text:0632db9b-0632dbde, unmarshal
    // .text:0632da9a-0632dad3): the four part ids, the count, then only `count` accessory
    // ids (a signed loop, JLE / JL). There is no bound: a count above 8 writes past
    // AccessoryIds exactly as retail does; ids past the count keep their old values.
    inline void AssignCostume(Costume& Dest, Costume const& Src)
    {
        Dest.PartIds[0] = Src.PartIds[0];
        Dest.PartIds[1] = Src.PartIds[1];
        Dest.PartIds[2] = Src.PartIds[2];
        Dest.PartIds[3] = Src.PartIds[3];
        Dest.AccessoryCount = Src.AccessoryCount;
        for (int Index = 0; Index < (int)Dest.AccessoryCount; ++Index)
            Dest.AccessoryIds[Index] = Src.AccessoryIds[Index];
    }

    const unsigned kPathChars = 0x104;

    // Every room / setpoint / placement / hotspot / character-property string is written in
    // retail with malloc_strcpy(&Field, Text) and released with malloc_strfree: the listing
    // ranges 06273300-0627b8e1 (room records), 06261330-06261d80 (RoomData copy / assign)
    // and 0632d890-0632e700 (CharacterProperties) contain no StringAlloc call at all. malloc_strcpy (06334400) is _strdup: a null source
    // gives null, "" gives a fresh "". It never frees the previous value.
    inline char const* MallocCopy(char const* Text)
    {
        char* Copy = nullptr;
        malloc_strcpy(&Copy, Text);
        return Copy;
    }
}
