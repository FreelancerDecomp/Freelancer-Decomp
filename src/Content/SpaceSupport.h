// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU53 (06f5c760-06f6b840): what the SpaceSupport controller (SpaceSupport.cpp)
// shares with the rest of the module. The dock queues of the same TU are in DockQueues.h.

#include <list>
#include <vector>

class FContentNpcShip;
struct FContentNpcShipParams;      // NpcShip.h (TU43)
struct FSpacePopExclusionSphere;   // Populators.h
struct FSpawnPlan;                 // Guides.h (FGuideDesc +0x10)
struct FSpawnRequest;              // Populators.h
struct FSystemInfo;                // SystemInfo.h (TU58)
struct FSystemZone;                // SystemInfo.h (TU64)

// FUN_06f65f10 (cdecl; caller TU29 FUN_06ef4eb0 06ef4f69; SpawnPlacement.cpp): places the
// formation Blocks for Request. Arrival (the formation's +0x44 flags, by value: the callee
// frees it) is copied; an arrival adds cruising in (7) and -- unless the leader is a patrol-
// path leader -- buzzing (8) instead of the trade lane (1). While Plan->Kind is 0 a random set
// method is tried and cleared: 1 trade lane, 2..6 dock launch (all five cleared), 7 cruise,
// 8 buzz. False when no method is left.
bool __cdecl PlaceSpawnFormation(FSpawnRequest const* Request, FSystemInfo* System,
                                 FSystemZone* Zone, double const* ZoneCreationDistance,
                                 std::vector<unsigned char> Arrival, FSpawnPlan* Plan,
                                 unsigned int const* Faction,
                                 std::list<FContentNpcShipParams>* Blocks);

// FUN_06f63cc0 (cdecl; callers TU29 FUN_06ef4eb0 06ef4f8f, TU60 06f83a38; SpawnPlacement.cpp):
// the two trade targets of the leader's guide description (an FGuideDesc, not null-tested;
// FGuideDesc::SetTargets) from the plan: a dock launch starts at the launcher; a trade-lane
// arrival picks near the lane's far end, then a second target near its entry end or at the
// far end of another lane within 20000; a path formation picks along the zone path (writing
// Plan->PathPoint); otherwise near the leader or anywhere in the system (type mask
// 0x600d60). A wander leader (type 4) gets one target twice.
void __cdecl SetLeaderGuideTargets(FSystemInfo* System, FSpawnPlan* Plan,
                                   FContentNpcShipParams* Leader);

// DAT_06fd1b9c (list object; head DAT_06fd1ba0, size DAT_06fd1ba4; static init LAB_06f5cc60,
// atexit FUN_06f5cca0): NPC ships whose TU37 guide wants every SpaceSupport message. The TU37
// guides register their ship (FUN_06f60d60, push_back) and unregister it by nulling its entry
// (FUN_06f60da0); SpaceSupport's HandleMessage tail forwards each message to the non-null
// entries (FContentNpcShip::ForwardToGuide, FUN_06f34c30) and then erases the null ones.
extern std::list<FContentNpcShip*> GSpaceSupportGuidedShips;

// FUN_06f616e0 (cdecl; callers SpacePop 06f5b767 / 06f5bfff with (1, 0), TU53 06f61b4f,
// 06f61b7c, 06f61e08, 06f61e30, 06f65be2, 06f65dd6): Position against the spheres (a point is
// inside a sphere when its distance is strictly below the radius). No vector -> true.
// bAll && bInside: inside every sphere; bAll && !bInside: outside every sphere; !bAll &&
// bInside: inside at least one; !bAll && !bInside: outside at least one.
bool __cdecl TestExclusionSpheres(float const* Position,
                                  std::vector<FSpacePopExclusionSphere> const* Spheres,
                                  bool const* bAll, bool const* bInside);

struct FNnPathLeg;                 // MissionObject.h
class Vector;                      // common_ini.h

// FUN_06f5e910 (cdecl; only caller GameSupport msg 0x31 06eff291): BuildNnPath (SpacePaths.h,
// FUN_06f5ea40) with the last seven arguments; when it returns false (no lane path needed)
// Legs is emptied and receives the single kind-1 leg From -> To (objects 0). Returns
// BuildNnPath's result. The first argument is not read.
bool __cdecl BuildBestPathLegs(unsigned int Unused, FSystemInfo* System, Vector const* From,
                               Vector const* To, std::list<FNnPathLeg>* Legs, int Flags,
                               float const* Range, int Unused7);
