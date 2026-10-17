// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The InfocardMap (freelancer.exe 0x004582e0..0x004588ea, the singleton DAT_0066d320) -- the
// table interface\InfocardMap.ini ([InfocardMapTable], `map = key, ids[, a, b, c]`) loads. The
// key is an object's ids_info; the entry names a second ids appended to that object's info card
// and switches the three base sections FUN_00476080 builds (the NavMap info card FUN_00497970
// and the two CHAR_infowindow builders 0x004750ec / 0x00475c39 are the readers).
//
// Retail object: operator_new(0x14) -- a VC6 std::map (allocator byte, head node +0x04, own nil
// node +0x08, +0x0c byte, size +0x10) of key (node +0x0c) -> FInfocardMapEntry (node +0x10).

#include <map>

// The value of one `map` line (node +0x10, 0x10 bytes). Each field is ftol of the line's value;
// values 2..4 default to 1 when the line leaves them empty (0x004584a6..0x0045851e).
struct FInfocardMapEntry
{
    int ExtraIds;   // +0x00 value 1: appended after the info text (FUN_00497970, visit bit 2)
    int Field_0x04; // +0x04 value 2: (visit & 8) && it -> FUN_00476080's 4th argument, the
                    //       equipment list (ids 0x66a; NnHelpAppendBaseGoods)
    int Field_0x08; // +0x08 value 3: (visit & 4) && it -> 3rd argument, the commodity lists
                    //       (ids 0x668 / 0x667)
    int Field_0x0c; // +0x0c value 4: (visit & 0x10) && it -> 5th argument, the ships for sale
                    //       (ids 0x669)
};

struct FInfocardMap
{
    // FUN_004583a0 -- read interface\InfocardMap.ini into Entries (first line of a key wins).
    void Load();

    // FUN_004582e0 is the map's clear, run by FUN_00458860 before the delete: the destructor.
    std::map<unsigned, FInfocardMapEntry> Entries;
};

// DAT_0066d320.
extern FInfocardMap* G_InfocardMap;

// FUN_00458790 (cdecl) -- make and load the singleton when there is none. Caller: FUN_0045a100.
void InfocardMapCreate();
// FUN_00458860 (cdecl) -- free the singleton. Caller: FUN_0045a140.
void InfocardMapDestroy();
// FUN_00458890 (cdecl) -- the entry of Key into *OutEntry; false without the singleton or the
// key. Callers: 0x004750ec, 0x00475c39, FUN_00497970 (0x00497b45).
bool InfocardMapFind(unsigned Key, FInfocardMapEntry const** OutEntry);
