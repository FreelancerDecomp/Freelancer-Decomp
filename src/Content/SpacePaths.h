// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU53 (SpaceSupport's TU, 06f5c760-06f6b840): the path and route helpers the
// TU37 guides, the mission NN path (TU38) and the random missions (TU47) use. Lane CNT-AI
// owns this file (the rest of TU53 is SpaceSupport.cpp / DockQueues.cpp, lane CNT-GS).
//
// The NN path builder (BuildNnPath, FUN_06f5ea40) plans a trip through a system over its
// trade lanes: each lane is a leg of kind 0 between its two end rings, the free flight
// before / after the lanes a leg of kind 1. The legs are FNnPathLeg (MissionObject.h).

#include "common_pub.h"   // Vector

#include <list>
#include <vector>

class FContentNpcShip;
struct FSystemInfo;          // SystemInfo.h (TU58)
class FObjListDirective;      // ObjListDirective.h
struct FNnPathLeg;            // MissionObject.h
struct FTradeLaneRing;        // SystemInfo.h

// FUN_06f60d60 (cdecl): GSpaceSupportGuidedShips.push_back(Ship) (SpaceSupport.h): the ship's
// guide wants every SpaceSupport message.
void __cdecl RegisterGuidedShip(FContentNpcShip* Ship);
// FUN_06f60da0 (cdecl): the first GSpaceSupportGuidedShips entry equal to Ship is set to
// null (SpaceSupport erases null entries after forwarding a message).
void __cdecl UnregisterGuidedShip(FContentNpcShip* Ship);

// FUN_06f60e10 (cdecl): false when a space object (pub::System::ScanObjects around the
// middle of the segment, radius max(length, 2000), type mask 0xfdff) lies closer to the
// segment From-To than its radius (+ 600 for the types with bit 0 or 1 set); true otherwise
// and whenever the solar detection of SpacePop is off.
bool __cdecl IsSegmentClear(Vector const* From, Vector const* To, FSystemInfo* System);

// FUN_06f60fc0 (cdecl): the objects of the system within reach of Center (ScanObjects radius
// (1000 + Range) / 2, mask 0xfdff; an object counts when its centre is closer than
// Range / 2 + its radius (+ 400 for the types with bit 0 or 1 set)). *OutCenter = the mean
// of their centres (0 when none). True when there is at least one.
bool __cdecl FindObstacleCenter(unsigned int SystemId, Vector Center, float Range,
                                Vector* OutCenter);

// FUN_06f61430 (cdecl): 16 points around Center in the xz plane, at the angles i * pi / 8,
// each at its own random distance MinRange + rand() / 32768 * (MaxRange - MinRange).
void __cdecl PickPointsAround(Vector const* Center, float const* MinRange, float MaxRange,
                              Vector* OutPoints);

// FUN_06f62440 (cdecl): the ring of Rings nearest to Position; when Target is given and a
// ring's plane (its -z axis through its position) separates Position from Target, the nearest
// such ring instead (when its id is not 0).
void __cdecl FindEntryRing(FTradeLaneRing* Out, Vector const* Position,
                           std::vector<FTradeLaneRing> const* Rings, Vector const* Target);

// FUN_06f6cb60 (cdecl): *Reputation's affiliation has a [BaseFaction] entry at *BaseId
// (FContentBase::FindFaction; the base is not null-checked).
bool __cdecl IsBaseOfAffiliation(int const* Reputation, unsigned int const* BaseId);

// The 0x5f3759df square-root estimate the TU53 path code and the TU37 goto guide inline
// (06f5f510, 06f08564): y = the bit guess from the float of LengthSquared, then
// ((1.5 - y * y * LengthSquared * 0.5) * y) * LengthSquared, all on the x87 stack.
double __cdecl FastLength(double LengthSquared);

// FUN_06f5ea40 (cdecl; callers TU37 guides, TU38 06f1c5e0 NN path, TU47 06f4b9e0, TU53
// 06f5e938): plan From -> To through System's trade lanes into Legs (which the caller passes
// empty or cleared). Flags (callers pass 0x19) and the seventh argument (0 or 1) are not
// read; *Range: a trip shorter than it needs no path (false). Returns false when no path is
// needed, true otherwise (Legs then ends with the kind 1 leg to To).
bool __cdecl BuildNnPath(FSystemInfo* System, Vector const* From, Vector const* To,
                         std::list<FNnPathLeg>* Legs, int Flags, float const* Range,
                         int Unused7);

// FUN_06f60020 (cdecl): BuildNnPath, then one directive per leg appended to Directives (the
// list holds one reference on each): kind 0 -> Dock (0x13) at the leg's first ring towards its
// second, kind 1 -> GotoVec (5) to the leg's end with range *Range; both with BaseOp
// Field_08 = 1.
void __cdecl BuildPathDirectives(FSystemInfo* System, Vector const* From, Vector const* To,
                                 std::list<FObjListDirective*>* Directives, int Flags,
                                 float const* Range);

class FContentShipBase;       // BehaviorCmds.h

// FUN_06f68270 (cdecl; callers TU37 06f0d75f, 06f0daa1): OtherId is a ship of Ship's system
// (FindShip) that flies in Ship's formation group (+0x154, not -1) or is Ship itself; false
// when no such ship is known.
bool __cdecl IsSameShipOrFormation(FContentShipBase* Ship, unsigned int OtherId);

// FUN_06f67d80 (cdecl; callers TU37 06f0d618, 06f0db34): pub::Reputation::SetAttitude from
// ShipId towards OtherId (and back when bMutual). bFormations: every member of both
// formation groups (CollectFormationMembers, or the ship alone outside a group; OtherId's
// group only when OtherId is a ship, else OtherId's own reputation); an NPC member's
// reputation is its handle's (+0x08), a player member's pub::SpaceObj::GetRep. Without
// bFormations only the two objects, and never from a player ship.
void __cdecl SetFormationAttitudes(FSystemInfo* System, unsigned int ShipId, unsigned int OtherId,
                                   bool bFormations, bool bMutual, float Attitude);
