// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The client's mission objectives, retail DAT_0067294c: a VC6 std::map<unsigned, value> (head
// node pointer DAT_00672950; static init LAB_004c3bd0 = FUN_004c59a0 + atexit LAB_004c3c00).
// Its FIRST entry (the smallest key, head->_Left) is the route currently shown: the nav map's
// best path, its objectives and the HUD waypoint. The accessors sit in the player-state block
// at 0x004c4fb0..0x004c58e0.
//
// Node layout (retail): +0x00 _Left, +0x04 _Parent, +0x08 _Right, +0x0c the key, +0x10 the
// value FBestPath (FUN_004c5040 returns node+0x10). The port keeps it as a std::map with the
// same key order (no retail ABI).
//
// Writers: IClient slot 72 (SETMISSIONOBJECTIVES, LAB_005a5bc0: clear, one entry, then
// FUN_004bcbf0), slot 71 (REPLACEMISSIONOBJECTIVE, FUN_005a5b60: operator[] and one record),
// slot 73 (SETMISSIONOBJECTIVESTATE, LAB_005a5e80: one record's +0x18) and the connect-list
// clears FUN_004c4b11 / 0x004c4c0f (FUN_004c5a80 erase(begin, end)).

#include "common_text.h" // FmtStr

#include <map>
#include <vector>

struct IObjRW;
class Vector;

// One 0x1c-byte waypoint record. FUN_004c5110 / FUN_004c52d0 walk +0x00 as a FmtStr::Val chain.
// The chain is {next, data, ushort tag}, with tags 'B' (0x42) base, 'R' (0x52) object and 'N'
// (0x4e) data block, so +0x00..+0x17 is a FmtStr (0x18 bytes).
struct FBestPathWaypoint
{
    FmtStr Marker;                  // +0x00
    // +0x18 -- from the server (IClient slots 71 / 72 / 73). The low nibble is the record kind
    // the nav map's objective list (FUN_0049f7a0) reads: 9 is not listed, 0xa is a numbered
    // stop (ids 0x4df), anything else ids 0x331c; FUN_004c5490 looks for the first one not 3.
    // Bits 0x80 / 0x20 / 0x10 are one-shot requests FUN_004bcbf0 serves and clears (story star
    // reset, mission-log history FUN_004ba2a0, mission-log queue FUN_004b9f90).
    unsigned Field_0x18;
};

// The value of one map entry (node +0x10, 0x44 bytes).
struct FBestPath
{
    std::vector<FBestPathWaypoint> Waypoints; // node +0x10 (vector: +0x14 first, +0x18 last)
    // node +0x20 / +0x38 -- the mission title and text the nav map's info panel formats with
    // FUN_0041c970 (FUN_0049a580 0x0049a714 / 0x0049a73f). Slot 72 copies them from its
    // message's +0x0c / +0x24.
    FmtStr Title;
    FmtStr Text;
    // node +0x50 -- read as a byte through FUN_004c5040 (+0x40): 7 and 8 hide the nav map's
    // abort-mission button (0x0049a866); any other value is the mission id NeuroNetNavMap's
    // abort sends (IServer::AbortMission, 0x0049f70e). Slot 72 copies it from +0x3c.
    unsigned char Field_0x50;
};

// DAT_0067294c.
extern std::map<unsigned, FBestPath> G_BestPaths;

// FUN_004c4fb0 (cdecl) -- the number of waypoints in the first route; 0 with no route or an
// unallocated vector.
int BestPathGetWaypointCount();

// FUN_004c4ff0 (cdecl) -- waypoint Index of the first route; null with no route, a negative
// index, an unallocated vector or an index past the end (unsigned compare).
FBestPathWaypoint* BestPathGetWaypoint(int Index);

// FUN_004c5040 (cdecl) -- the first route, null with none. Retail returns the address of the
// node's +0x10 (the waypoint vector) and its callers index from there (+0x40 = node +0x50); the
// port hands out the route itself.
FBestPath* BestPathGetFirst();

// FUN_004c5490 (cdecl) -- the index of the first route's first waypoint whose +0x18 dword is
// not 3; -1 with no route, an unallocated or empty vector, or when every one is 3. Only
// caller: NeuroNetNavMap::Draw (0x00492bc1), which drops the answer.
int BestPathFirstWaypointNotKind3();

// FUN_004c58e0 (cdecl) -- the first route has waypoints and its Text's MadLibId (node +0x3c)
// is neither 0 nor 1. Callers: NeuroNetNavMap FUN_00496e50 (0x00497290, 0x004972b5).
bool BestPathHasText();

// FUN_004c5530 (cdecl) -- a hash of waypoint Index's tokens (0 without the record): from its
// MadLibId, rotate left 1 per token and add its data unless the tag is 'R' or 'N'. The nav
// map's FUN_004a0800 caches its resolved position under it.
unsigned BestPathWaypointHash(int Index);

// FUN_004c5110 (thiscall on the record) -- the object the first 'B' or 'R' token names: a base
// through the cached base watcher DAT_006728d8 (the base's docking solar, searched in ObjDB
// bucket 2 only while the base's system is the player's), an object id through FUN_00541890;
// null otherwise.
IObjRW* BestPathWaypointResolveObject(FBestPathWaypoint const* Waypoint);

// FUN_004c52d0 (thiscall on the record, RET 8) -- the position and star system of the record:
// the 'B' / 'R' object's position in the player's system, else the first 'N' block's {system,
// -, pos}; false when neither.
bool BestPathWaypointResolvePosition(FBestPathWaypoint const* Waypoint, Vector* OutPos,
                                     unsigned* OutSystem);

// FUN_004bcbf0 (cdecl) -- the objectives changed (IClient slot 72): raise the nav map's
// (DAT_006720c9) and the contact list's (DAT_0067494c) refresh flags; with a route, drop the
// player's waypoints and serve every record's one-shot bits.
void BestPathObjectivesChanged();

struct FServerReplaceMissionObjectiveInfo; // rpc_iclient_args.h
struct FServerSetMissionObjectivesInfo;
struct FServerMissionObjectiveStateInfo;

// FUN_005a5b60 -- IClient slot 71 (REPLACEMISSIONOBJECTIVE). Info: +0x00 key, +0x04 FmtStr,
// +0x1c the record's +0x18, +0x20 byte record index.
void BestPathReplaceObjective(FServerReplaceMissionObjectiveInfo const* Info);
// LAB_005a5bc0 -- IClient slot 72 (SETMISSIONOBJECTIVES). Info: +0x00 key, +0x04 record array
// (0x1c each), +0x08 count, +0x0c FmtStr title, +0x24 FmtStr text, +0x3c byte.
void BestPathSetObjectives(FServerSetMissionObjectivesInfo const* Info);
// LAB_005a5e80 -- IClient slot 73 (SETMISSIONOBJECTIVESTATE). Info: +0x00 key, +0x04 byte
// record index, +0x08 the record's new +0x18.
void BestPathSetObjectiveState(FServerMissionObjectiveStateInfo const* Info);
