// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The KnowledgeMap (freelancer.exe 0x00459060..0x00459d80, the singleton DAT_0066d334) -- the
// table interface\KnowledgeMap.ini ([KnowledgeMapTable], `map = key, nickname, flags`) loads:
// reading a text whose ids is a key (a mission offer, a news item, a NeuroNet caption) makes
// the player "know" every object listed under that key, i.e. sets its visit flags
// (FUN_004c4e00, IServer::SetVisitedState).
//
// Retail object: operator_new(0x14) -- a VC6 std::map (allocator byte, head node +0x04, own nil
// node +0x08, +0x0c byte, size +0x10) of key -> {Id, Flags byte, Next}. The first entry of a
// key lives in the tree node (+0x10 Id, +0x14 Flags, +0x18 Next); every further line with the
// same key is a 0xc-byte {Id, Flags, Next} node linked in right after it (0x0045963a..).

#include <forward_list>
#include <map>

struct FmtStr;

// One further `map` line of a key (retail 0xc-byte link node).
struct FKnowledgeMapLink
{
    unsigned Id;          // +0x00
    unsigned char Flags;  // +0x04
};

// The tree node's value: the key's first line and the lines linked after it.
struct FKnowledgeMapEntry
{
    unsigned Id;                              // node +0x10
    unsigned char Flags;                      // node +0x14
    std::forward_list<FKnowledgeMapLink> More; // node +0x18 -- newest line first
};

struct FKnowledgeMap
{
    // FUN_00459470 -- read interface\KnowledgeMap.ini into Entries.
    void Load();
    // FUN_00459210 (thiscall, RET 4) -- every object listed under Key gets its visit flags
    // raised to include the line's flags (server notified).
    void MarkKnown(unsigned Key);
    // FUN_004592b0 (thiscall, RET 0xc) -- the first line of the key Text names: its mad-lib ids,
    // else (in list order) a raw-ids value (marked known first), a system's / base's strid_name
    // or a faction's name. True with *OutId / *OutFlags on a hit.
    bool Resolve(FmtStr const* Text, unsigned* OutId, unsigned char* OutFlags);

    std::map<unsigned, FKnowledgeMapEntry> Entries;
};

// DAT_0066d334.
extern FKnowledgeMap* G_KnowledgeMap;

// FUN_00459830 (cdecl) -- make and load the singleton when there is none. Caller: FUN_0045a100.
void KnowledgeMapCreate();
// FUN_00459900 (cdecl) -- free the singleton (FUN_00459060 + delete). Caller: FUN_0045a140.
void KnowledgeMapDestroy();
// FUN_00459930 (cdecl) -- MarkKnown(Key) when the singleton exists. Callers 0x004b5f48,
// 0x004b611e, 0x004b624e (the ToolTip / NN caption code, not ported yet).
void KnowledgeMapReveal(unsigned Key);
// FUN_00459950 (cdecl) -- with the singleton: MarkKnown on Text's mad-lib ids, then on every
// value: 's' the raw ids, 'S' the system's strid_name, 'B' the base's strid_name, 'F' the
// faction's name, 'Z' a found zone's name; a nested FmtStr ('!') recurses.
void KnowledgeMapRevealFmtStr(FmtStr const* Text);
// FUN_00459ad0 (cdecl) -- Resolve on the singleton; false without one. Caller 0x00455202.
bool KnowledgeMapLookupFmtStr(FmtStr const* Text, unsigned* OutId, unsigned char* OutFlags);
